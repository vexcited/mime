/*
 * the clipboard shared with the host programs of the desktop. a host program's copy tells the guest which kinds
 * of contents there are, and the data is read and converted only when a guest program asks for a kind; the
 * guest's copy offers kinds to the host in the same way and is asked for the data of one when another program
 * pastes.
 *
 * the formats and their conversions are in convert.c. sdl does the protocol: with wayland the host holds the
 * selection only while one of its windows has the keyboard, which is also when the desktop shows its contents to
 * the guest.
 */
#include "convert.h"
#include "ui_internal.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KINDS 7
#define MAX_BYTES (256u << 20)
#define REQUEST_TIMEOUT_MS 5000

/* the kinds as bits 0 to 6 of a mask. */
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
    pthread_cond_t cond;
} Request;

static struct {
    pthread_mutex_t lock;
    char **mimes;             /* what the host clipboard offers */
    size_t mime_count;
    uint32_t mask;            /* the MIME_CLIP_* kinds that gives */
    uint32_t generation;
    Cached cache[KINDS];      /* converted contents of this generation */
    uint32_t offered;         /* kinds the guest renders */
    bool owned;               /* the guest's offer is the host selection */
    uint32_t next_request;
    Request *requests;
    uint8_t *served;          /* the last answer given to a host program, kept while it reads it */
    char *legacy;             /* the text set through the text only calls */
} g = {.lock = PTHREAD_MUTEX_INITIALIZER};

static const char *const k_text[] = {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "TEXT", "STRING"};
static const char *const k_png[] = {"image/png"};
static const char *const k_bmp[] = {"image/bmp", "image/x-bmp", "image/x-ms-bmp"};
#define MIME_HTML "text/html"
#define MIME_URIS "text/uri-list"
#define MIME_GNOME_FILES "x-special/gnome-copied-files"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static bool has_mime_locked(const char *mime)
{
    for (size_t i = 0; i < g.mime_count; i++)
        if (!strcmp(g.mimes[i], mime))
            return true;
    return false;
}

static const char *find_mime_locked(const char *const *list, size_t count)
{
    for (size_t i = 0; i < count; i++)
        if (has_mime_locked(list[i]))
            return list[i];
    return nullptr;
}

static uint32_t mask_of_locked(void)
{
    uint32_t mask = 0;
    if (find_mime_locked(k_text, COUNT(k_text)))
        mask |= MIME_CLIP_TEXT;
    if (find_mime_locked(k_png, COUNT(k_png)) || find_mime_locked(k_bmp, COUNT(k_bmp)))
        mask |= MIME_CLIP_DIB | MIME_CLIP_DIBV5 | MIME_CLIP_PNG;
    if (has_mime_locked(MIME_HTML))
        mask |= MIME_CLIP_HTML;
    /* a list of uris holds files, urls or both: only reading it tells. */
    if (has_mime_locked(MIME_URIS))
        mask |= MIME_CLIP_HDROP | MIME_CLIP_URL;
    return mask;
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
     * a window that loses the keyboard loses sight of the selection, which sdl reports as a change to nothing. the
     * guest keeps what it shows until another program really copies.
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

/* the bytes of a host mime type, null when the program does not give them; main thread. */
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

/* the host contents in a kind, converted for the guest; main thread. */
static uint8_t *read_host(uint32_t kind, size_t *out_size)
{
    pthread_mutex_lock(&g.lock);
    const char *text = find_mime_locked(k_text, COUNT(k_text));
    const char *image = find_mime_locked(k_png, COUNT(k_png));
    if (!image)
        image = find_mime_locked(k_bmp, COUNT(k_bmp));
    bool html = has_mime_locked(MIME_HTML), uris = has_mime_locked(MIME_URIS);
    pthread_mutex_unlock(&g.lock);

    const char *mime = nullptr;
    switch (kind) {
    case MIME_CLIP_TEXT:
        mime = text;
        break;
    case MIME_CLIP_DIB:
    case MIME_CLIP_DIBV5:
    case MIME_CLIP_PNG:
        mime = image;
        break;
    case MIME_CLIP_HTML:
        mime = html ? MIME_HTML : nullptr;
        break;
    case MIME_CLIP_HDROP:
    case MIME_CLIP_URL:
        mime = uris ? MIME_URIS : nullptr;
        break;
    }
    if (!mime)
        return nullptr;
    size_t size = 0;
    uint8_t *raw = read_mime(mime, &size);
    if (!raw)
        return nullptr;
    uint8_t *out = nullptr;
    switch (kind) {
    case MIME_CLIP_TEXT:
        out = mime_conv_text_to_guest((const char *)raw, size, out_size);
        break;
    case MIME_CLIP_DIB:
    case MIME_CLIP_DIBV5:
        out = mime_conv_image_to_dib(raw, size, kind == MIME_CLIP_DIBV5, out_size);
        break;
    case MIME_CLIP_PNG:
        out = mime_conv_image_to_png(raw, size, out_size);
        break;
    case MIME_CLIP_HTML:
        out = mime_conv_html_to_guest(raw, size, out_size);
        break;
    case MIME_CLIP_HDROP:
        out = mime_conv_uris_to_hdrop(mime_daemon_paths(), (const char *)raw, size, out_size);
        break;
    case MIME_CLIP_URL:
        out = mime_conv_uris_to_url((const char *)raw, size, out_size);
        break;
    }
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

/* asks the guest for a kind and waits for the chunks; main thread. */
static uint8_t *request_guest(uint32_t kind, size_t *size)
{
    Request *r = calloc(1, sizeof(*r));
    if (!r)
        return nullptr;
    pthread_cond_init(&r->cond, nullptr);
    pthread_mutex_lock(&g.lock);
    do
        r->id = ++g.next_request;
    while (!r->id);
    r->next = g.requests;
    g.requests = r;
    clock_gettime(CLOCK_REALTIME, &r->deadline);
    r->deadline.tv_sec += REQUEST_TIMEOUT_MS / 1000;
    pthread_mutex_unlock(&g.lock);
    MimeInputEvent ev = {.type = MIME_INPUT_CLIP_REQUEST, .x = (int32_t)kind, .button = r->id};
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
        /* it timed out already. */
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
            r->deadline.tv_sec += REQUEST_TIMEOUT_MS / 1000;
        }
    }
    if (r->done)
        pthread_cond_signal(&r->cond);
    pthread_mutex_unlock(&g.lock);
    return status;
}

/* the kind to ask the guest for a host mime type, and how to turn its answer into that type. */
typedef enum Target { T_TEXT, T_PNG, T_BMP, T_HTML, T_URIS, T_GNOME } Target;

static bool target_of(const char *mime, Target *target)
{
    for (size_t i = 0; i < COUNT(k_text); i++)
        if (!strcmp(mime, k_text[i])) {
            *target = T_TEXT;
            return true;
        }
    if (!strcmp(mime, "image/png"))
        *target = T_PNG;
    else if (!strcmp(mime, "image/bmp"))
        *target = T_BMP;
    else if (!strcmp(mime, MIME_HTML))
        *target = T_HTML;
    else if (!strcmp(mime, MIME_URIS))
        *target = T_URIS;
    else if (!strcmp(mime, MIME_GNOME_FILES))
        *target = T_GNOME;
    else
        return false;
    return true;
}

/* the answer for a host program that pastes, from the guest's kinds; main thread. */
static const void *serve(void *userdata, const char *mime, size_t *size)
{
    Target target;
    *size = 0;
    if (!target_of(mime, &target))
        return nullptr;
    pthread_mutex_lock(&g.lock);
    uint32_t offered = g.offered;
    pthread_mutex_unlock(&g.lock);

    uint8_t *raw = nullptr, *out = nullptr;
    size_t raw_size = 0, out_size = 0;
    uint32_t kind = 0;
    switch (target) {
    case T_TEXT:
        kind = MIME_CLIP_TEXT;
        break;
    case T_PNG:
        kind = offered & MIME_CLIP_PNG ? MIME_CLIP_PNG : offered & MIME_CLIP_DIBV5 ? MIME_CLIP_DIBV5 : MIME_CLIP_DIB;
        break;
    case T_BMP:
        kind = offered & MIME_CLIP_DIBV5 ? MIME_CLIP_DIBV5 : offered & MIME_CLIP_DIB ? MIME_CLIP_DIB : MIME_CLIP_PNG;
        break;
    case T_HTML:
        kind = MIME_CLIP_HTML;
        break;
    case T_URIS:
        kind = offered & MIME_CLIP_HDROP ? MIME_CLIP_HDROP : MIME_CLIP_URL;
        break;
    case T_GNOME:
        kind = MIME_CLIP_HDROP;
        break;
    }
    if (!(offered & kind))
        return nullptr;
    raw = request_guest(kind, &raw_size);
    if (!raw)
        return nullptr;
    switch (kind) {
    case MIME_CLIP_TEXT:
        out = (uint8_t *)mime_conv_text_from_guest((const uint16_t *)raw, raw_size / 2, &out_size);
        break;
    case MIME_CLIP_PNG:
        if (target == T_PNG) {
            out = raw;
            out_size = raw_size;
            raw = nullptr;
        } else {
            /* a bmp file from a png: through the pixels. */
            size_t dib_size;
            uint8_t *dib = mime_conv_image_to_dib(raw, raw_size, true, &dib_size);
            if (dib) {
                out = mime_conv_dib_to_bmp(dib, dib_size, &out_size);
                free(dib);
            }
        }
        break;
    case MIME_CLIP_DIB:
    case MIME_CLIP_DIBV5:
        out = target == T_PNG ? mime_conv_dib_to_png(raw, raw_size, &out_size)
                              : mime_conv_dib_to_bmp(raw, raw_size, &out_size);
        break;
    case MIME_CLIP_HTML:
        out = mime_conv_html_from_guest(raw, raw_size, &out_size);
        break;
    case MIME_CLIP_HDROP:
        out = (uint8_t *)mime_conv_hdrop_to_uris(mime_daemon_paths(), raw, raw_size, &out_size);
        if (out && target == T_GNOME) {
            /* "copy", then one uri on each line without a line end after the last. */
            uint8_t *gnome = malloc(out_size + 8);
            if (gnome) {
                memcpy(gnome, "copy\n", 5);
                size_t n = 5;
                for (size_t i = 0; i < out_size; i++)
                    if (out[i] != '\r')
                        gnome[n++] = out[i];
                while (n && gnome[n - 1] == '\n')
                    n--;
                free(out);
                out = gnome;
                out_size = n;
            }
        }
        break;
    case MIME_CLIP_URL:
        out = (uint8_t *)mime_conv_url_to_uris(raw, raw_size, true, &out_size);
        break;
    }
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
    size_t n = 0;
    if (mask & MIME_CLIP_TEXT) {
        mimes[n++] = k_text[0];
        mimes[n++] = k_text[1];
    }
    if (mask & (MIME_CLIP_PNG | MIME_CLIP_DIB | MIME_CLIP_DIBV5)) {
        mimes[n++] = "image/png";
        mimes[n++] = "image/bmp";
    }
    if (mask & MIME_CLIP_HTML)
        mimes[n++] = MIME_HTML;
    if (mask & (MIME_CLIP_HDROP | MIME_CLIP_URL))
        mimes[n++] = MIME_URIS;
    if (mask & MIME_CLIP_HDROP)
        mimes[n++] = MIME_GNOME_FILES;
    pthread_mutex_lock(&g.lock);
    g.offered = mask;
    bool owned = g.owned;
    pthread_mutex_unlock(&g.lock);
    if (!n) {
        if (owned)
            SDL_ClearClipboardData();
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

/* the text of the host clipboard, for guests that know nothing of kinds. */
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

/* sets the host clipboard to text at once, for guests that know nothing of kinds. */
void mime_ui_clipboard_set(const uint16_t *text, size_t chars)
{
    size_t len;
    char *utf8 = mime_conv_text_from_guest(text, chars, &len);
    if (!utf8)
        return;
    if (!mime_ui_has_video() || !mime_ui_call_main(legacy_main, utf8))
        free(utf8);
}
