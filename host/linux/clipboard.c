#include "convert.h"
#include "ui_internal.h"
#include "wayland.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#define KINDS 8
#define MAX_BYTES (256u << 20)
#define REQUEST_TIMEOUT_MS 5000
#define READ_TIMEOUT_MS 5000
/* a type no program reads, which tells an offer of the selection is the machine's own. */
#define OWN_TYPE "application/x-mime-owner"

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
    struct wl_data_offer *offer;   /* what the host clipboard offers, on the main thread. */
    struct wl_data_source *source; /* the guest's offer, on the main thread. */
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

/* main thread. an offer without a type, or the machine's own, changes nothing. */
void mime_clipboard_selection(struct wl_data_offer *offer, char **mimes, size_t count)
{
    bool own = false;
    for (size_t i = 0; i < count; i++)
        own = own || !strcmp(mimes[i], OWN_TYPE);
    if (!offer && !count) {
        /* the selection is gone, as when the program that held it ended: what the guest knew of it is too. */
        pthread_mutex_lock(&g.lock);
        if (!g.offer) {
            pthread_mutex_unlock(&g.lock);
            return;
        }
        wl_data_offer_destroy(g.offer);
        g.offer = nullptr;
        free_mimes_locked();
        free_cache_locked();
        g.mask = 0;
        uint32_t generation = ++g.generation;
        pthread_mutex_unlock(&g.lock);
        MimeInputEvent ev = {.type = MIME_INPUT_CLIPBOARD, .y = (int32_t)generation};
        mime_ui_emit(&ev);
        return;
    }
    if (!offer || !count || own) {
        if (offer)
            wl_data_offer_destroy(offer);
        for (size_t i = 0; i < count; i++)
            free(mimes[i]);
        free(mimes);
        return;
    }
    pthread_mutex_lock(&g.lock);
    g.owned = false;
    if (g.offer)
        wl_data_offer_destroy(g.offer);
    g.offer = offer;
    free_mimes_locked();
    g.mimes = mimes;
    g.mime_count = count;
    free_cache_locked();
    g.mask = mask_of_locked();
    uint32_t generation = ++g.generation;
    uint32_t mask = g.mask;
    pthread_mutex_unlock(&g.lock);
    MimeInputEvent ev = {.type = MIME_INPUT_CLIPBOARD, .x = (int32_t)mask, .y = (int32_t)generation};
    mime_ui_emit(&ev);
}

/* what a program writes to a pipe until it closes it, null for nothing or too much. */
static uint8_t *read_pipe(int fd, size_t *size)
{
    uint8_t *data = nullptr;
    size_t used = 0, capacity = 0;
    bool ok = true;
    for (;;) {
        struct pollfd pfd = {fd, POLLIN, 0};
        int n = poll(&pfd, 1, READ_TIMEOUT_MS);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        if (used + 65536 > capacity) {
            capacity = capacity ? capacity * 2 : 65536;
            uint8_t *grown = realloc(data, capacity);
            if (!grown) {
                ok = false;
                break;
            }
            data = grown;
        }
        ssize_t got = read(fd, data + used, capacity - used);
        if (got < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (got <= 0) {
            ok = got == 0;
            break;
        }
        used += (size_t)got;
        if (used > MAX_BYTES) {
            ok = false;
            break;
        }
    }
    close(fd);
    if (!ok || !used) {
        free(data);
        return nullptr;
    }
    *size = used;
    return data;
}

typedef struct Fetch {
    uint32_t kind, generation;
    int fd; /* where the program that holds the selection writes, -1 for none. */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool done;
} Fetch;

/* asks the program that holds the selection for a type of it. the processor that waits reads the answer. */
static void fetch_main(void *arg)
{
    Fetch *f = arg;
    int fds[2];
    pthread_mutex_lock(&g.lock);
    const char *type = f->generation == g.generation && g.offer ? mime_conv_read_type(f->kind, g.mimes, g.mime_count)
                                                                 : nullptr;
    if (type && pipe2(fds, O_CLOEXEC) == 0) {
        wl_data_offer_receive(g.offer, type, fds[1]);
        close(fds[1]);
        f->fd = fds[0];
    }
    pthread_mutex_unlock(&g.lock);
    wl_display_flush(mime_wl_display());
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
        Fetch f = {.kind = kind, .generation = generation, .fd = -1, .lock = PTHREAD_MUTEX_INITIALIZER,
                   .cond = PTHREAD_COND_INITIALIZER};
        if (!mime_ui_call_main(fetch_main, &f))
            return MIME_HC_STATUS_NOT_FOUND;
        pthread_mutex_lock(&f.lock);
        while (!f.done)
            pthread_cond_wait(&f.cond, &f.lock);
        pthread_mutex_unlock(&f.lock);
        size_t raw_size = 0, size = 0;
        uint8_t *raw = f.fd >= 0 ? read_pipe(f.fd, &raw_size) : nullptr;
        uint8_t *data = raw ? mime_conv_to_guest(kind, raw, raw_size, &size) : nullptr;
        free(raw);
        pthread_mutex_lock(&g.lock);
        if (generation != g.generation) {
            pthread_mutex_unlock(&g.lock);
            free(data);
            return MIME_HC_STATUS_NOT_FOUND;
        }
        if (!g.cache[i].ready) {
            g.cache[i] = (Cached){true, data, data ? size : 0};
            data = nullptr;
        }
        free(data);
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

/* renders a requested host type from guest formats, on a thread of the request. */
static uint8_t *serve(const char *mime, size_t *size)
{
    *size = 0;
    pthread_mutex_lock(&g.lock);
    uint32_t offered = g.offered;
    pthread_mutex_unlock(&g.lock);
    uint32_t kind = mime_conv_pick_kind(mime, offered);
    if (!kind)
        return nullptr;
    size_t raw_size = 0;
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
    uint8_t *out = mime_conv_from_guest(mime, kind, raw, raw_size, effect, size);
    free(raw);
    return out;
}

typedef struct Send {
    char *mime;
    int fd;
    bool legacy;
} Send;

/* a paste waits for the guest, which no thread of the windows may do. */
static void *send_thread(void *arg)
{
    Send *s = arg;
    mime_set_thread_name("mime-clip-send");
    size_t size = 0, done = 0;
    uint8_t *data = nullptr;
    if (s->legacy) {
        pthread_mutex_lock(&g.lock);
        if (g.legacy && (data = (uint8_t *)strdup(g.legacy)))
            size = strlen(g.legacy);
        pthread_mutex_unlock(&g.lock);
    } else {
        data = serve(s->mime, &size);
    }
    /* a program that asks and then does not read would hold the thread for good. */
    int flags = fcntl(s->fd, F_GETFL);
    if (flags >= 0)
        fcntl(s->fd, F_SETFL, flags | O_NONBLOCK);
    while (data && done < size) {
        ssize_t n = write(s->fd, data + done, size - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN) {
            struct pollfd pfd = {s->fd, POLLOUT, 0};
            int ready = poll(&pfd, 1, REQUEST_TIMEOUT_MS);
            if (ready > 0 || (ready < 0 && errno == EINTR))
                continue;
            break;
        }
        if (n <= 0)
            break;
        done += (size_t)n;
    }
    free(data);
    close(s->fd);
    free(s->mime);
    free(s);
    return nullptr;
}

static void source_target(void *data, struct wl_data_source *source, const char *mime)
{
}

static void source_send(void *data, struct wl_data_source *source, const char *mime, int32_t fd)
{
    Send *s = calloc(1, sizeof(*s));
    pthread_t thread;
    if (s) {
        *s = (Send){.mime = strdup(mime), .fd = fd, .legacy = data != nullptr};
        if (s->mime && pthread_create(&thread, nullptr, send_thread, s) == 0) {
            pthread_detach(thread);
            return;
        }
        free(s->mime);
        free(s);
    }
    close(fd);
}

/* another program copied: the guest's offer is no longer the selection. */
static void source_cancelled(void *data, struct wl_data_source *source)
{
    if (g.source == source)
        g.source = nullptr;
    wl_data_source_destroy(source);
    pthread_mutex_lock(&g.lock);
    if (!g.source)
        g.owned = false;
    pthread_mutex_unlock(&g.lock);
}

static void source_dropped(void *data, struct wl_data_source *source)
{
}

static void source_action(void *data, struct wl_data_source *source, uint32_t action)
{
}

static const struct wl_data_source_listener source_listener = {source_target, source_send,    source_cancelled,
                                                               source_dropped, source_dropped, source_action};

/* makes the types the selection of the desktop, none of them to empty it. main thread. */
static bool set_selection(const char *const *mimes, size_t count, bool legacy)
{
    struct wl_data_device *device = mime_dnd_device();
    struct wl_data_device_manager *manager = mime_dnd_manager();
    if (!device || !manager)
        return false;
    struct wl_data_source *old = g.source;
    g.source = nullptr;
    if (count) {
        g.source = wl_data_device_manager_create_data_source(manager);
        if (!g.source)
            return false;
        wl_data_source_add_listener(g.source, &source_listener, legacy ? &g : nullptr);
        for (size_t i = 0; i < count; i++)
            wl_data_source_offer(g.source, mimes[i]);
        wl_data_source_offer(g.source, OWN_TYPE);
    }
    /* the compositor takes a selection from the program the last input went to. */
    wl_data_device_set_selection(device, g.source, mime_wl_selection_serial());
    if (old)
        wl_data_source_destroy(old);
    wl_display_flush(mime_wl_display());
    return g.source != nullptr;
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
        bool ok = false;
        if (owned) {
            set_selection(nullptr, 0, false);
        } else if (held) {
            /*
             * no program clears the selection of another. it is replaced with an unreadable type so pasting is empty
             * and the old owner releases it.
             */
            static const char *const nothing[] = {"application/x-mime-empty"};
            ok = set_selection(nothing, 1, false);
        } else {
            return;
        }
        pthread_mutex_lock(&g.lock);
        g.owned = ok;
        pthread_mutex_unlock(&g.lock);
        return;
    }
    bool ok = set_selection(mimes, n, false);
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
    static const char *const text[] = {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "STRING", "TEXT"};
    pthread_mutex_lock(&g.lock);
    free(g.legacy);
    g.legacy = arg;
    pthread_mutex_unlock(&g.lock);
    bool ok = set_selection(text, MIME_ARRAY_COUNT(text), true);
    pthread_mutex_lock(&g.lock);
    g.owned = ok;
    pthread_mutex_unlock(&g.lock);
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
