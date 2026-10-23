/*
 * shares clipboard formats with the desktop using SDL. offers advertise types; data is read and converted on demand
 * through convert.c. on Wayland, selection ownership and visibility require a window with keyboard focus.
 */
#include "convert.h"
#include "ui_internal.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KINDS 8
#define MAX_BYTES (256u << 20)
#define REQUEST_TIMEOUT_MS 5000

/* the kinds as bits 0 to 7 of a mask. */
static int kind_index(uint32_t kind)
{
    for (int i = 0; i < KINDS; i++)
        if (kind == 1u << i)
            return i;
    return -1;
}

typedef struct Cached {
    bool ready;
    uint8_t *data;
    size_t size;
} Cached;

typedef struct Request {
    struct Request *next;
    uint32_t id;
    uint8_t *data;
    uint64_t total, received;
    bool done, failed;
    struct timespec deadline;
    int timeout_ms;
    pthread_cond_t cond;
} Request;

static struct {
    pthread_mutex_t lock;
    char **mimes;             /* what the host clipboard offers. */
    size_t mime_count;
    uint32_t mask;            /* the MIME_CLIP_* kinds that gives. */
    uint32_t generation;
    Cached cache[KINDS];      /* converted contents of this generation. */
    uint32_t offered;         /* kinds the guest renders. */
    bool owned;               /* the guest's offer is the host selection. */
    uint32_t next_request;
    Request *requests;
    uint8_t *served;          /* the last answer given to a host program, kept while it reads it. */
    char *legacy;             /* the text set through the text only calls. */
} g = {.lock = PTHREAD_MUTEX_INITIALIZER};

static uint32_t mask_of_locked(void)
{
    return mime_conv_clipboard_kinds(g.mimes, g.mime_count);
}

static void free_cache_locked(void)
{
    for (int i = 0; i < KINDS; i++) {
        free(g.cache[i].data);
        g.cache[i] = (Cached){};
    }
}

static void free_mimes_locked(void)
{
    for (size_t i = 0; i < g.mime_count; i++)
        free(g.mimes[i]);
    free(g.mimes);
    g.mimes = nullptr;
    g.mime_count = 0;
}

/* ------------------------------------------------------------------ the host's clipboard */

void mime_clipboard_init(void)
{
}

void mime_clipboard_event(const SDL_ClipboardEvent *e)
{
    /* our own offer going out. */
    if (e->owner)
        return;
    pthread_mutex_lock(&g.lock);
    g.owned = false;
    /*
     * SDL reports an empty selection when keyboard focus is lost. keep guest contents until another program actually
     * copies.
     */
    if (e->num_mime_types <= 0) {
        pthread_mutex_unlock(&g.lock);
        return;
    }
    free_mimes_locked();
    g.mimes = calloc((size_t)e->num_mime_types, sizeof(char *));
    if (g.mimes)
        for (int i = 0; i < e->num_mime_types; i++)
            if (e->mime_types[i] && (g.mimes[g.mime_count] = strdup(e->mime_types[i])))
                g.mime_count++;
    free_cache_locked();
    g.mask = mask_of_locked();
    uint32_t generation = ++g.generation;
    uint32_t mask = g.mask;
    pthread_mutex_unlock(&g.lock);
    MimeInputEvent ev = {.type = MIME_INPUT_CLIPBOARD, .x = (int32_t)mask, .y = (int32_t)generation};
    mime_ui_emit(&ev);
}

/* reads host MIME-type data on the main thread, returning null if unavailable. */
static uint8_t *read_mime(const char *mime, size_t *size)
{
    void *raw = SDL_GetClipboardData(mime, size);
    if (!raw)
        return nullptr;
    if (!*size || *size > MAX_BYTES) {
        SDL_free(raw);
        return nullptr;
    }
    uint8_t *copy = malloc(*size);
    if (copy)
        memcpy(copy, raw, *size);
    SDL_free(raw);
    return copy;
}

/* converts host data to a guest format on the main thread. */
static uint8_t *read_host(uint32_t kind, size_t *out_size)
{
    pthread_mutex_lock(&g.lock);
    const char *type = mime_conv_read_type(kind, g.mimes, g.mime_count);
    pthread_mutex_unlock(&g.lock);
    if (!type)
        return nullptr;
    size_t size = 0;
    uint8_t *raw = read_mime(type, &size);
    if (!raw)
        return nullptr;
    uint8_t *out = mime_conv_to_guest(kind, raw, size, out_size);
    free(raw);
    return out;
}

typedef struct Fetch {
    uint32_t kind, generation;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool done;
} Fetch;

static void fetch_main(void *arg)
{
    Fetch *f = arg;
    size_t size = 0;
    uint8_t *data = read_host(f->kind, &size);
    pthread_mutex_lock(&g.lock);
    int i = kind_index(f->kind);
    if (i >= 0 && f->generation == g.generation && !g.cache[i].ready) {
        g.cache[i] = (Cached){true, data, data ? size : 0};
        data = nullptr;
    }
    pthread_mutex_unlock(&g.lock);
    free(data);
    pthread_mutex_lock(&f->lock);
    f->done = true;
    pthread_cond_signal(&f->cond);
    pthread_mutex_unlock(&f->lock);
}

uint64_t mime_ui_clipboard_fetch(uint32_t kind, uint32_t generation, void *dst, uint64_t capacity, uint64_t offset,
                              uint64_t *total)
{
    *total = 0;
    int i = kind_index(kind);
    if (i < 0)
        return MIME_HC_STATUS_INVALID;
    pthread_mutex_lock(&g.lock);
    if (generation != g.generation || !(g.mask & kind)) {
        pthread_mutex_unlock(&g.lock);
        return MIME_HC_STATUS_NOT_FOUND;
    }
    if (!g.cache[i].ready) {
        pthread_mutex_unlock(&g.lock);
        Fetch f = {.kind = kind, .generation = generation, .lock = PTHREAD_MUTEX_INITIALIZER,
                   .cond = PTHREAD_COND_INITIALIZER};
        /* the main thread never waits for a vcpu, so this cannot deadlock. */
        if (!mime_ui_call_main(fetch_main, &f))
            return MIME_HC_STATUS_NOT_FOUND;
        pthread_mutex_lock(&f.lock);
        while (!f.done)
            pthread_cond_wait(&f.cond, &f.lock);
        pthread_mutex_unlock(&f.lock);
        pthread_mutex_lock(&g.lock);
        if (generation != g.generation) {
            pthread_mutex_unlock(&g.lock);
            return MIME_HC_STATUS_NOT_FOUND;
        }
    }
    const Cached *c = &g.cache[i];
    *total = c->ready ? c->size : 0;
    if (dst && capacity && offset < *total)
        memcpy(dst, c->data + offset, (size_t)MIME_MIN(capacity, *total - offset));
    pthread_mutex_unlock(&g.lock);
    return MIME_HC_STATUS_OK;
}

/* ------------------------------------------------------------------ the guest's clipboard */

uint8_t *mime_ui_request_guest(uint32_t event, uint32_t kind, int timeout_ms, size_t *size)
{
    Request *r = calloc(1, sizeof(*r));
    if (!r)
        return nullptr;
    pthread_cond_init(&r->cond, nullptr);
    r->timeout_ms = timeout_ms;
    pthread_mutex_lock(&g.lock);
    do
        r->id = ++g.next_request;
    while (!r->id);
    r->next = g.requests;
    g.requests = r;
    clock_gettime(CLOCK_REALTIME, &r->deadline);
    r->deadline.tv_sec += timeout_ms / 1000;
    pthread_mutex_unlock(&g.lock);
    MimeInputEvent ev = {.type = event, .x = (int32_t)kind, .button = r->id};
    mime_ui_emit(&ev);
    pthread_mutex_lock(&g.lock);
    int rc = 0;
    /* the deadline moves on with every chunk, so a big answer that keeps coming is not cut off. */
    while (!r->done && rc == 0)
        rc = pthread_cond_timedwait(&r->cond, &g.lock, &r->deadline);
    for (Request **p = &g.requests; *p; p = &(*p)->next)
        if (*p == r) {
            *p = r->next;
            break;
        }
    uint8_t *data = r->done && !r->failed ? r->data : nullptr;
    *size = (size_t)r->total;
    if (!data)
        free(r->data);
    pthread_mutex_unlock(&g.lock);
    pthread_cond_destroy(&r->cond);
    free(r);
    return data;
}

uint64_t mime_ui_clipboard_provide(uint32_t id, const void *src, uint64_t length, uint64_t offset, uint64_t total)
{
    pthread_mutex_lock(&g.lock);
    Request *r = g.requests;
    while (r && r->id != id)
        r = r->next;
    uint64_t status = MIME_HC_STATUS_OK;
    if (!r) {
        pthread_mutex_unlock(&g.lock);
        return MIME_HC_STATUS_NOT_FOUND;
    }
    if (total == MIME_CLIP_NONE || total == 0) {
        r->failed = total == MIME_CLIP_NONE;
        r->done = true;
    } else if (total > MAX_BYTES || offset > total || length > total - offset) {
        status = MIME_HC_STATUS_INVALID;
        r->failed = r->done = true;
    } else {
        if (offset == 0 && !r->data) {
            r->data = malloc((size_t)total);
            r->total = total;
        }
        if (!r->data || r->total != total) {
            status = r->data ? MIME_HC_STATUS_INVALID : MIME_HC_STATUS_NO_MEMORY;
            r->failed = r->done = true;
        } else {
            memcpy(r->data + offset, src, (size_t)length);
            r->received = offset + length;
            if (r->received == total)
                r->done = true;
            clock_gettime(CLOCK_REALTIME, &r->deadline);
            r->deadline.tv_sec += r->timeout_ms / 1000;
        }
    }
    if (r->done)
        pthread_cond_signal(&r->cond);
    pthread_mutex_unlock(&g.lock);
    return status;
}

/* renders a requested host type from guest formats on the main thread. */
static const void *serve(void *userdata, const char *mime, size_t *size)
{
    *size = 0;
    pthread_mutex_lock(&g.lock);
    uint32_t offered = g.offered;
    pthread_mutex_unlock(&g.lock);
    uint32_t kind = mime_conv_pick_kind(mime, offered);
    if (!kind)
        return nullptr;
    size_t raw_size = 0, out_size = 0;
    /* file-manager MIME types distinguish cut and copy; get the guest's drop effect first. */
    uint32_t effect = 0;
    if ((offered & MIME_CLIP_DROPEFFECT) && mime_conv_needs_effect(mime)) {
        size_t effect_size = 0;
        uint8_t *bytes = mime_ui_request_guest(MIME_INPUT_CLIP_REQUEST, MIME_CLIP_DROPEFFECT, REQUEST_TIMEOUT_MS,
                                               &effect_size);
        if (bytes && effect_size >= 4)
            memcpy(&effect, bytes, 4);
        free(bytes);
    }
    uint8_t *raw = mime_ui_request_guest(MIME_INPUT_CLIP_REQUEST, kind, REQUEST_TIMEOUT_MS, &raw_size);
    if (!raw)
        return nullptr;
    uint8_t *out = mime_conv_from_guest(mime, kind, raw, raw_size, effect, &out_size);
    free(raw);
    pthread_mutex_lock(&g.lock);
    free(g.served);
    g.served = out;
    pthread_mutex_unlock(&g.lock);
    *size = out ? out_size : 0;
    return out;
}

static void serve_cleanup(void *userdata)
{
    pthread_mutex_lock(&g.lock);
    free(g.served);
    g.served = nullptr;
    pthread_mutex_unlock(&g.lock);
}

static void offer_main(void *arg)
{
    uint32_t mask = (uint32_t)(uintptr_t)arg;
    const char *mimes[16];
    size_t n = mime_conv_offer_types(mask, mimes);
    pthread_mutex_lock(&g.lock);
    g.offered = mask;
    bool owned = g.owned;
    pthread_mutex_unlock(&g.lock);
    if (!n) {
        /* emptying the guest clipboard also clears contents owned by host programs. */
        pthread_mutex_lock(&g.lock);
        bool held = g.mask != 0;
        g.mask = 0;
        free_cache_locked();
        pthread_mutex_unlock(&g.lock);
        if (owned) {
            SDL_ClearClipboardData();
        } else if (held) {
            /*
             * SDL cannot clear another client's selection. replace it with an unreadable type so pasting is empty
             * and the old owner releases it.
             */
            static const char *const nothing[] = {"application/x-mime-empty"};
            bool ok = SDL_SetClipboardData(serve, serve_cleanup, nullptr, nothing, 1);
            pthread_mutex_lock(&g.lock);
            g.owned = ok;
            pthread_mutex_unlock(&g.lock);
        }
        return;
    }
    bool ok = SDL_SetClipboardData(serve, serve_cleanup, nullptr, mimes, n);
    pthread_mutex_lock(&g.lock);
    g.owned = ok;
    pthread_mutex_unlock(&g.lock);
}

uint64_t mime_ui_clipboard_offer(uint32_t mask)
{
    if (!mime_ui_has_video())
        return MIME_HC_STATUS_OK;
    pthread_mutex_lock(&g.lock);
    g.offered = mask;
    pthread_mutex_unlock(&g.lock);
    mime_ui_call_main(offer_main, (void *)(uintptr_t)mask);
    return MIME_HC_STATUS_OK;
}

/* ------------------------------------------------------------------ the text only calls */

/* legacy text-only clipboard read. */
size_t mime_ui_clipboard_read(uint16_t *dst, size_t offset, size_t count)
{
    pthread_mutex_lock(&g.lock);
    uint32_t generation = g.generation;
    pthread_mutex_unlock(&g.lock);
    uint64_t total = 0;
    if (mime_ui_clipboard_fetch(MIME_CLIP_TEXT, generation, dst, count * 2, offset * 2, &total) != MIME_HC_STATUS_OK)
        return 0;
    return (size_t)(total / 2);
}

static void legacy_main(void *arg)
{
    char *utf8 = arg;
    SDL_SetClipboardText(utf8);
    free(utf8);
}

/* legacy text-only clipboard write. */
void mime_ui_clipboard_set(const uint16_t *text, size_t chars)
{
    size_t len;
    char *utf8 = mime_conv_text_from_guest(text, chars, &len);
    if (!utf8)
        return;
    if (!mime_ui_has_video() || !mime_ui_call_main(legacy_main, utf8))
        free(utf8);
}
