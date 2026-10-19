/*
 * drag and drop between host programs and the guest, on wayland: a drag over a host window reaches the guest as
 * events and a drag that leaves the guest's windows becomes a drag of the host.
 *
 * sdl only knows drops of files and text, delivered when the button goes up, and cannot start a drag. this file
 * has its own data device on the seat, on the default queue sdl's event loop dispatches, so its handlers run on
 * the main thread. sdl's own acceptance of drops is turned off, or both would answer the compositor. the
 * compositor sends a drag to every data device of the client.
 *
 * a drag over the guest: the types it offers say which kinds the guest can have (MIME_CLIP_*); the data is read
 * from the offer when the guest asks for a kind, whenever that is, and converted as the clipboard's. the guest
 * answers enter and over with the effect its target chooses, which becomes the accepted type and action, and the
 * drop with the effect it ended with.
 *
 * a drag of the guest: the guest starts it with the kinds it can render and the effects it allows; it needs the
 * serial of the button press that began it, which this file saw on a pointer of its own. when another program
 * reads a type, the guest is asked for the kind and the answer is converted.
 */
#include "convert.h"
#include "ui_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#define KINDS 7
#define GROUPS 4
#define PREFETCH_MAX (64u << 20)
#define MAX_BYTES (256u << 20)
#define READ_TIMEOUT_MS 5000
#define REQUEST_TIMEOUT_MS 5000

#define EFFECT_COPY 1
#define EFFECT_MOVE 2
#define EFFECT_LINK 4

/* ------------------------------------------------------------------ what other programs offer */

typedef struct Offer {
    struct wl_data_offer *offer;
    char **mimes;
    size_t mime_count;
    uint32_t source_actions;
    uint32_t id;     /* the session, 0 while it is no drag over the guest */
    uint32_t serial; /* of the enter */
    uint32_t kinds;
    bool dropped;
    struct Reader *readers[GROUPS];
    struct {
        bool ready;
        uint8_t *data;
        size_t size;
    } cache[KINDS];
} Offer;

static struct wl_display *g_display;
static struct wl_seat *g_seat;
static struct wl_data_device_manager *g_manager;
static struct wl_data_device *g_device;
static struct wl_pointer *g_pointer;
static uint32_t g_next_session;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Offer *g_target;           /* the offer of the drag over the guest, if any */
static uint32_t g_press_serial;   /* of the last press of a pointer button */
static void *g_press_surface;     /* and the surface the pointer was over */
static void *g_pointer_surface;

static int kind_index(uint32_t kind)
{
    for (int i = 0; i < KINDS; i++)
        if (kind == 1u << i)
            return i;
    return -1;
}

/*
 * the data of a drag is read as soon as the drag enters, not when the guest asks. sdl's own data device is a
 * second client of the same drag, and when it drops it destroys the offer it declined, which the compositor
 * takes for the end of the drop: the source is then told it finished and may drop its data before a request
 * made after the drop is served. what is asked for first is served. the kinds that share a type share a read.
 */
typedef struct Reader {
    int fd;
    char *mime;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    uint8_t *data;
    size_t size;
    bool done, ok, cancel;
    int refs;
} Reader;

static void reader_release(Reader *r)
{
    if (__atomic_sub_fetch(&r->refs, 1, __ATOMIC_ACQ_REL))
        return;
    free(r->data);
    free(r->mime);
    free(r);
}

static void *reader_thread(void *arg)
{
    Reader *r = arg;
    mime_set_thread_name("mime-dnd-read");
    size_t used = 0, capacity = 0;
    uint8_t *data = nullptr;
    bool ok = true;
    int idle = 0;
    while (!r->cancel) {
        struct pollfd pfd = {r->fd, POLLIN, 0};
        int n = poll(&pfd, 1, 100);
        if (n < 0 && errno != EINTR) {
            ok = false;
            break;
        }
        if (n <= 0) {
            if (++idle > READ_TIMEOUT_MS / 100 * 3) {
                ok = false;
                break;
            }
            continue;
        }
        idle = 0;
        if (used + 65536 > capacity) {
            capacity = capacity ? capacity * 2 : 65536;
            uint8_t *grown = realloc(data, capacity);
            if (!grown) {
                ok = false;
                break;
            }
            data = grown;
        }
        ssize_t got = read(r->fd, data + used, capacity - used);
        if (got < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (got <= 0)
            break;
        used += (size_t)got;
        if (used > PREFETCH_MAX) {
            ok = false;
            break;
        }
    }
    close(r->fd);
    pthread_mutex_lock(&r->lock);
    r->ok = ok && used && !r->cancel;
    if (r->ok) {
        r->data = data;
        r->size = used;
    } else {
        free(data);
    }
    r->done = true;
    pthread_cond_broadcast(&r->cond);
    pthread_mutex_unlock(&r->lock);
    reader_release(r);
    return nullptr;
}

static void offer_free(Offer *o)
{
    for (int i = 0; i < GROUPS; i++)
        if (o->readers[i]) {
            o->readers[i]->cancel = true;
            reader_release(o->readers[i]);
        }
    for (size_t i = 0; i < o->mime_count; i++)
        free(o->mimes[i]);
    free(o->mimes);
    for (int i = 0; i < KINDS; i++)
        free(o->cache[i].data);
    free(o);
}

static void offer_handle_offer(void *data, struct wl_data_offer *offer, const char *mime)
{
    Offer *o = data;
    char **grown = realloc(o->mimes, (o->mime_count + 1) * sizeof(char *));
    if (!grown)
        return;
    o->mimes = grown;
    char *copy = strdup(mime);
    if (copy)
        o->mimes[o->mime_count++] = copy;
}

static void offer_handle_source_actions(void *data, struct wl_data_offer *offer, uint32_t actions)
{
    ((Offer *)data)->source_actions = actions;
}

static void offer_handle_action(void *data, struct wl_data_offer *offer, uint32_t action)
{
}

static const struct wl_data_offer_listener offer_listener = {offer_handle_offer, offer_handle_source_actions,
                                                             offer_handle_action};

/* ends the offer: it goes away on the compositor's side too. */
static void offer_destroy(Offer *o)
{
    wl_data_offer_destroy(o->offer);
    offer_free(o);
}

static uint32_t effects_of_actions(uint32_t actions)
{
    uint32_t effects = 0;
    if (actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY)
        effects |= EFFECT_COPY;
    if (actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE)
        effects |= EFFECT_MOVE;
    if (actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)
        effects |= EFFECT_LINK;
    return effects;
}

static uint32_t actions_of_effects(uint32_t effects)
{
    uint32_t actions = 0;
    if (effects & EFFECT_COPY)
        actions |= WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
    if (effects & EFFECT_MOVE)
        actions |= WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
    if (effects & EFFECT_LINK)
        actions |= WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
    return actions;
}

static int group_of(uint32_t kind)
{
    switch (kind) {
    case MIME_CLIP_TEXT:
        return 0;
    case MIME_CLIP_DIB:
    case MIME_CLIP_DIBV5:
    case MIME_CLIP_PNG:
        return 1;
    case MIME_CLIP_HTML:
        return 2;
    case MIME_CLIP_HDROP:
    case MIME_CLIP_URL:
        return 3;
    }
    return -1;
}

/* asks the source for every type the guest can make a kind of; main thread. */
static void prefetch(Offer *o)
{
    static const uint32_t first_of_group[GROUPS] = {MIME_CLIP_TEXT, MIME_CLIP_PNG, MIME_CLIP_HTML, MIME_CLIP_HDROP};
    for (int g = 0; g < GROUPS; g++) {
        uint32_t kinds = 0;
        for (uint32_t k = 1; k <= MIME_CLIP_URL; k <<= 1)
            if (group_of(k) == g)
                kinds |= k;
        if (!(o->kinds & kinds))
            continue;
        const char *type = mime_conv_read_type(first_of_group[g], o->mimes, o->mime_count);
        int fds[2];
        if (!type || pipe2(fds, O_CLOEXEC) != 0)
            continue;
        Reader *r = calloc(1, sizeof(*r));
        pthread_t thread;
        if (!r) {
            close(fds[0]);
            close(fds[1]);
            continue;
        }
        *r = (Reader){.fd = fds[0], .mime = strdup(type), .lock = PTHREAD_MUTEX_INITIALIZER,
                      .cond = PTHREAD_COND_INITIALIZER, .refs = 2};
        wl_data_offer_receive(o->offer, type, fds[1]);
        close(fds[1]);
        if (pthread_create(&thread, nullptr, reader_thread, r) != 0) {
            close(fds[0]);
            free(r->mime);
            free(r);
            continue;
        }
        pthread_detach(thread);
        o->readers[g] = r;
    }
    wl_display_flush(g_display);
}

static void device_data_offer(void *data, struct wl_data_device *device, struct wl_data_offer *id)
{
    Offer *o = calloc(1, sizeof(*o));
    if (!o) {
        wl_data_offer_destroy(id);
        return;
    }
    o->offer = id;
    o->source_actions = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
    wl_data_offer_set_user_data(id, o);
    wl_data_offer_add_listener(id, &offer_listener, o);
}

static void drop_target(void)
{
    pthread_mutex_lock(&g_lock);
    Offer *o = g_target;
    g_target = nullptr;
    pthread_mutex_unlock(&g_lock);
    if (o)
        offer_destroy(o);
}

static void emit_drag(uint32_t type, const Offer *o, uint32_t window, int32_t x, int32_t y)
{
    MimeInputEvent ev = {.type = type, .window_id = window, .x = x, .y = y, .flags = o->kinds,
                         .wheel = (int32_t)effects_of_actions(o->source_actions), .modifiers = mime_ui_modifiers(),
                         .pad = o->id};
    mime_ui_emit(&ev);
}

static uint32_t g_target_window;

static void device_enter(void *data, struct wl_data_device *device, uint32_t serial, struct wl_surface *surface,
                         wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *id)
{
    drop_target();
    Offer *o = id ? wl_data_offer_get_user_data(id) : nullptr;
    uint32_t window;
    int32_t gx, gy;
    if (!o)
        return;
    if (!surface || !mime_ui_surface_point(surface, wl_fixed_to_double(x), wl_fixed_to_double(y), &window, &gx, &gy) ||
        !(o->kinds = mime_conv_kinds(o->mimes, o->mime_count))) {
        /* nothing here takes it: decline, and keep the offer until the drag leaves. */
        wl_data_offer_accept(id, serial, nullptr);
        if (wl_data_offer_get_version(id) >= WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION)
            wl_data_offer_set_actions(id, 0, 0);
        pthread_mutex_lock(&g_lock);
        g_target = o;
        pthread_mutex_unlock(&g_lock);
        return;
    }
    o->serial = serial;
    pthread_mutex_lock(&g_lock);
    o->id = ++g_next_session ? g_next_session : (g_next_session = 1);
    g_target = o;
    pthread_mutex_unlock(&g_lock);
    g_target_window = window;
    prefetch(o);
    emit_drag(MIME_INPUT_DND_ENTER, o, window, gx, gy);
}

static void device_motion(void *data, struct wl_data_device *device, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    pthread_mutex_lock(&g_lock);
    Offer *o = g_target;
    pthread_mutex_unlock(&g_lock);
    if (!o || !o->id || o->dropped)
        return;
    /* the surface is the one of the enter: a drag stays on it until it leaves. */
    uint32_t window = g_target_window;
    void *surface = mime_ui_window_surface(window);
    int32_t gx, gy;
    if (!surface || !mime_ui_surface_point(surface, wl_fixed_to_double(x), wl_fixed_to_double(y), &window, &gx, &gy))
        return;
    emit_drag(MIME_INPUT_DND_OVER, o, window, gx, gy);
}

static void device_leave(void *data, struct wl_data_device *device)
{
    pthread_mutex_lock(&g_lock);
    Offer *o = g_target;
    pthread_mutex_unlock(&g_lock);
    if (!o)
        return;
    /* a drop is followed by a leave, and the guest still has the offer to read until it finishes. */
    if (o->dropped)
        return;
    if (o->id)
        emit_drag(MIME_INPUT_DND_LEAVE, o, g_target_window, 0, 0);
    drop_target();
}

static void device_drop(void *data, struct wl_data_device *device)
{
    pthread_mutex_lock(&g_lock);
    Offer *o = g_target;
    pthread_mutex_unlock(&g_lock);
    if (!o)
        return;
    if (!o->id) {
        drop_target();
        return;
    }
    o->dropped = true;
    emit_drag(MIME_INPUT_DND_DROP, o, g_target_window, 0, 0);
}

static void device_selection(void *data, struct wl_data_device *device, struct wl_data_offer *id)
{
    /* the clipboard is sdl's; this device only sees its offers, which would pile up on the compositor. */
    if (id)
        offer_destroy(wl_data_offer_get_user_data(id));
}

static const struct wl_data_device_listener device_listener = {device_data_offer, device_enter, device_leave,
                                                               device_motion, device_drop, device_selection};

/* what the guest's target answered to the drag: the effect, as the type accepted and the actions. */
static void effect_main(void *arg)
{
    uint64_t packed = (uint64_t)(uintptr_t)arg;
    uint32_t session = (uint32_t)(packed >> 32), effect = (uint32_t)packed;
    pthread_mutex_lock(&g_lock);
    Offer *o = g_target;
    pthread_mutex_unlock(&g_lock);
    if (!o || o->id != session || o->dropped)
        return;
    uint32_t actions = actions_of_effects(effect);
    if (!actions) {
        wl_data_offer_accept(o->offer, o->serial, nullptr);
        if (wl_data_offer_get_version(o->offer) >= WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION)
            wl_data_offer_set_actions(o->offer, 0, 0);
        return;
    }
    /* any type tells the compositor it is wanted: the one of the first kind there is. */
    const char *type = nullptr;
    for (int i = 0; i < KINDS && !type; i++)
        if (o->kinds & (1u << i))
            type = mime_conv_read_type(1u << i, o->mimes, o->mime_count);
    wl_data_offer_accept(o->offer, o->serial, type);
    if (wl_data_offer_get_version(o->offer) >= WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION) {
        uint32_t preferred = actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY ? WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY
                           : actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE ? WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE
                                                                              : WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
        wl_data_offer_set_actions(o->offer, actions, preferred);
    }
}

uint64_t mime_ui_dnd_effect(uint32_t session, uint32_t effect)
{
    if (!mime_ui_has_video())
        return MIME_HC_STATUS_OK;
    mime_ui_call_main(effect_main, (void *)(uintptr_t)(((uint64_t)session << 32) | effect));
    return MIME_HC_STATUS_OK;
}

static void finish_main(void *arg)
{
    uint64_t packed = (uint64_t)(uintptr_t)arg;
    uint32_t session = (uint32_t)(packed >> 32), effect = (uint32_t)packed;
    pthread_mutex_lock(&g_lock);
    Offer *o = g_target;
    pthread_mutex_unlock(&g_lock);
    if (!o || o->id != session)
        return;
    if (effect && o->dropped && wl_data_offer_get_version(o->offer) >= WL_DATA_OFFER_FINISH_SINCE_VERSION)
        wl_data_offer_finish(o->offer);
    drop_target();
}

uint64_t mime_ui_dnd_finish(uint32_t session, uint32_t effect)
{
    if (!mime_ui_has_video())
        return MIME_HC_STATUS_OK;
    mime_ui_call_main(finish_main, (void *)(uintptr_t)(((uint64_t)session << 32) | effect));
    return MIME_HC_STATUS_OK;
}

uint64_t mime_ui_dnd_fetch(uint32_t session, uint32_t kind, void *dst, uint64_t capacity, uint64_t offset,
                           uint64_t *total)
{
    *total = 0;
    int i = kind_index(kind), group = group_of(kind);
    if (i < 0 || group < 0)
        return MIME_HC_STATUS_INVALID;
    pthread_mutex_lock(&g_lock);
    Offer *o = g_target;
    Reader *r = o && o->id == session && (o->kinds & kind) ? o->readers[group] : nullptr;
    bool ready = r && o->cache[i].ready;
    if (r && !ready)
        __atomic_add_fetch(&r->refs, 1, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&g_lock);
    if (!r)
        return MIME_HC_STATUS_NOT_FOUND;
    uint8_t *data = nullptr;
    size_t size = 0;
    if (!ready) {
        /* the source is given time to finish what it writes. */
        pthread_mutex_lock(&r->lock);
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 20;
        int rc = 0;
        while (!r->done && rc == 0)
            rc = pthread_cond_timedwait(&r->cond, &r->lock, &deadline);
        bool ok = r->done && r->ok;
        uint8_t *raw = ok ? r->data : nullptr;
        size_t raw_size = ok ? r->size : 0;
        pthread_mutex_unlock(&r->lock);
        if (raw)
            data = mime_conv_to_guest(kind, raw, raw_size, &size);
        reader_release(r);
        pthread_mutex_lock(&g_lock);
        o = g_target;
        if (o && o->id == session && !o->cache[i].ready) {
            o->cache[i].ready = true;
            o->cache[i].data = data;
            o->cache[i].size = data ? size : 0;
        } else {
            free(data);
        }
        pthread_mutex_unlock(&g_lock);
    }
    pthread_mutex_lock(&g_lock);
    o = g_target;
    uint64_t status = MIME_HC_STATUS_NOT_FOUND;
    if (o && o->id == session && o->cache[i].ready) {
        *total = o->cache[i].size;
        if (dst && capacity && offset < *total)
            memcpy(dst, o->cache[i].data + offset, (size_t)MIME_MIN(capacity, *total - offset));
        status = MIME_HC_STATUS_OK;
    }
    pthread_mutex_unlock(&g_lock);
    return status;
}

/* ------------------------------------------------------------------ the guest's drag */

typedef struct Drag {
    struct wl_data_source *source;
    uint32_t id;
    uint32_t kinds;
    uint32_t action;    /* the last the compositor chose */
    bool performed;
    bool ended;
    int pending;        /* host programs reading from the guest right now */
    int refs;           /* the drag itself, and each of those reads */
} Drag;

static Drag *g_drag;

typedef struct Send {
    Drag *drag;
    uint32_t kinds;
    char *mime;
    int fd;
} Send;

static void drag_unref(Drag *d)
{
    if (__atomic_sub_fetch(&d->refs, 1, __ATOMIC_ACQ_REL))
        return;
    free(d);
}

/* the guest's answer to a host program reading a type, converted and written to its pipe; a thread of its own. */
static void *send_thread(void *arg)
{
    Send *s = arg;
    mime_set_thread_name("mime-dnd-send");
    uint32_t kind = mime_conv_pick_kind(s->mime, s->kinds);
    size_t raw_size = 0, size = 0;
    uint8_t *raw = kind ? mime_ui_request_guest(MIME_INPUT_DND_REQUEST, kind, REQUEST_TIMEOUT_MS, &raw_size) : nullptr;
    uint8_t *data = raw ? mime_conv_from_guest(s->mime, kind, raw, raw_size, &size) : nullptr;
    free(raw);
    size_t done = 0;
    while (data && done < size) {
        ssize_t n = write(s->fd, data + done, size - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN) {
            struct pollfd pfd = {s->fd, POLLOUT, 0};
            poll(&pfd, 1, REQUEST_TIMEOUT_MS);
            continue;
        }
        if (n <= 0)
            break;
        done += (size_t)n;
    }
    free(data);
    close(s->fd);
    free(s->mime);
    pthread_mutex_lock(&g_lock);
    s->drag->pending--;
    pthread_mutex_unlock(&g_lock);
    drag_unref(s->drag);
    free(s);
    return nullptr;
}

static void source_handle_target(void *data, struct wl_data_source *source, const char *mime)
{
}

static void source_handle_send(void *data, struct wl_data_source *source, const char *mime, int32_t fd)
{
    Drag *d = data;
    Send *s = calloc(1, sizeof(*s));
    pthread_t thread;
    if (s) {
        *s = (Send){.drag = d, .kinds = d->kinds, .mime = strdup(mime), .fd = fd};
        pthread_mutex_lock(&g_lock);
        d->pending++;
        pthread_mutex_unlock(&g_lock);
        __atomic_add_fetch(&d->refs, 1, __ATOMIC_RELAXED);
        if (s->mime && pthread_create(&thread, nullptr, send_thread, s) == 0) {
            pthread_detach(thread);
            return;
        }
        pthread_mutex_lock(&g_lock);
        d->pending--;
        pthread_mutex_unlock(&g_lock);
        drag_unref(d);
        free(s->mime);
        free(s);
    }
    close(fd);
}

static void drag_end(Drag *d, bool dropped)
{
    pthread_mutex_lock(&g_lock);
    bool first = !d->ended;
    d->ended = true;
    pthread_mutex_unlock(&g_lock);
    if (!first)
        return;
    MimeInputEvent ev = {.type = MIME_INPUT_DND_END, .pad = d->id, .x = dropped,
                         .flags = dropped ? effects_of_actions(d->action) : 0};
    mime_ui_emit(&ev);
}

/* the end of a finished drop waits for the programs still reading from the guest, so the guest's data lives. */
static void *end_thread(void *arg)
{
    Drag *d = arg;
    mime_set_thread_name("mime-dnd-end");
    /* sdl's data device of a target ends a drop before the reads that came with it. */
    struct timespec grace = {0, 150 * 1000 * 1000};
    nanosleep(&grace, nullptr);
    for (int i = 0; i < 500; i++) {
        pthread_mutex_lock(&g_lock);
        int pending = d->pending;
        pthread_mutex_unlock(&g_lock);
        if (!pending)
            break;
        struct timespec step = {0, 10 * 1000 * 1000};
        nanosleep(&step, nullptr);
    }
    drag_end(d, true);
    drag_unref(d);
    return nullptr;
}

static void source_handle_cancelled(void *data, struct wl_data_source *source)
{
    drag_end(data, false);
}

static void source_handle_dnd_drop_performed(void *data, struct wl_data_source *source)
{
    ((Drag *)data)->performed = true;
}

static void source_handle_dnd_finished(void *data, struct wl_data_source *source)
{
    /*
     * the drop is done as far as the compositor knows, but a target may still read what it was offered: sdl's data
     * device of a target that is one of our own windows ends the drop early. the source stays until the next drag.
     */
    Drag *d = data;
    pthread_t thread;
    __atomic_add_fetch(&d->refs, 1, __ATOMIC_RELAXED);
    if (pthread_create(&thread, nullptr, end_thread, d) == 0)
        pthread_detach(thread);
    else {
        drag_unref(d);
        drag_end(d, true);
    }
}

static void source_handle_action(void *data, struct wl_data_source *source, uint32_t action)
{
    ((Drag *)data)->action = action;
}

static const struct wl_data_source_listener source_listener = {source_handle_target, source_handle_send,
                                                               source_handle_cancelled,
                                                               source_handle_dnd_drop_performed,
                                                               source_handle_dnd_finished, source_handle_action};

typedef struct Start {
    uint32_t kinds, effects;
    uint32_t session;
    bool ok;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool done;
} Start;

static void start_main(void *arg)
{
    Start *s = arg;
    const char *types[16];
    size_t n = mime_conv_offer_types(s->kinds, types);
    if (g_drag && g_drag->ended) {
        wl_data_source_destroy(g_drag->source);
        drag_unref(g_drag);
        g_drag = nullptr;
    }
    if (n && g_manager && g_device && g_press_surface && g_press_serial && !g_drag) {
        Drag *d = calloc(1, sizeof(*d));
        if (d) {
            d->source = wl_data_device_manager_create_data_source(g_manager);
            d->id = ++g_next_session ? g_next_session : (g_next_session = 1);
            d->kinds = s->kinds;
            d->refs = 1;
            wl_data_source_add_listener(d->source, &source_listener, d);
            for (size_t i = 0; i < n; i++)
                wl_data_source_offer(d->source, types[i]);
            if (wl_data_source_get_version(d->source) >= WL_DATA_SOURCE_SET_ACTIONS_SINCE_VERSION)
                wl_data_source_set_actions(d->source, actions_of_effects(s->effects));
            wl_data_device_start_drag(g_device, d->source, g_press_surface, nullptr, g_press_serial);
            wl_display_flush(g_display);
            g_drag = d;
            s->session = d->id;
            s->ok = true;
        }
    }
    pthread_mutex_lock(&s->lock);
    s->done = true;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);
}

uint64_t mime_ui_dnd_start(uint32_t kinds, uint32_t effects, uint64_t *session)
{
    *session = 0;
    if (!mime_ui_has_video() || !g_device)
        return MIME_HC_STATUS_UNSUPPORTED;
    Start s = {.kinds = kinds, .effects = effects, .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};
    if (!mime_ui_call_main(start_main, &s))
        return MIME_HC_STATUS_UNSUPPORTED;
    pthread_mutex_lock(&s.lock);
    while (!s.done)
        pthread_cond_wait(&s.cond, &s.lock);
    pthread_mutex_unlock(&s.lock);
    *session = s.session;
    return s.ok ? MIME_HC_STATUS_OK : MIME_HC_STATUS_INVALID;
}

static void stop_main(void *arg)
{
    uint32_t session = (uint32_t)(uintptr_t)arg;
    Drag *d = g_drag;
    if (d && d->id == session) {
        /* the source goes away and the compositor ends the drag. */
        g_drag = nullptr;
        d->ended = true;
        wl_data_source_destroy(d->source);
        drag_unref(d);
    }
}

uint64_t mime_ui_dnd_stop(uint32_t session)
{
    if (mime_ui_has_video())
        mime_ui_call_main(stop_main, (void *)(uintptr_t)session);
    return MIME_HC_STATUS_OK;
}

/* ------------------------------------------------------------------ the pointer, for the serial of a press */

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t x, wl_fixed_t y)
{
    g_pointer_surface = surface;
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface)
{
    if (g_pointer_surface == surface)
        g_pointer_surface = nullptr;
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state)
{
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        g_press_serial = serial;
        g_press_surface = g_pointer_surface;
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source)
{
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis)
{
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete)
{
}

static void pointer_axis_value120(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t value120)
{
}

static void pointer_axis_direction(void *data, struct wl_pointer *pointer, uint32_t axis, uint32_t direction)
{
}

static void pointer_warp(void *data, struct wl_pointer *pointer, wl_fixed_t x, wl_fixed_t y)
{
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_enter,         pointer_leave,        pointer_motion,        pointer_button,
    pointer_axis,          pointer_frame,        pointer_axis_source,   pointer_axis_stop,
    pointer_axis_discrete, pointer_axis_value120, pointer_axis_direction, pointer_warp};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !g_pointer) {
        g_pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(g_pointer, &pointer_listener, nullptr);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
}

static const struct wl_seat_listener seat_listener = {seat_capabilities, seat_name};

static bool g_text_input;

bool mime_ui_has_text_input(void)
{
    return g_text_input;
}

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                            uint32_t version)
{
    if (!strcmp(interface, "zwp_text_input_manager_v3"))
        g_text_input = true;
    if (!strcmp(interface, wl_seat_interface.name) && !g_seat)
        g_seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    else if (!strcmp(interface, wl_data_device_manager_interface.name))
        g_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, MIME_MIN(version, 3u));
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {registry_global, registry_remove};

void mime_dnd_init(void)
{
    if (!SDL_GetCurrentVideoDriver() || strcmp(SDL_GetCurrentVideoDriver(), "wayland"))
        return;
    g_display = SDL_GetPointerProperty(SDL_GetGlobalProperties(), SDL_PROP_GLOBAL_VIDEO_WAYLAND_WL_DISPLAY_POINTER,
                                       nullptr);
    if (!g_display)
        return;
    /* sdl's own handling declines what it does not take, which would race with the answers given here. */
    SDL_SetEventEnabled(SDL_EVENT_DROP_BEGIN, false);
    SDL_SetEventEnabled(SDL_EVENT_DROP_POSITION, false);
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, false);
    SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, false);
    SDL_SetEventEnabled(SDL_EVENT_DROP_COMPLETE, false);
    struct wl_registry *registry = wl_display_get_registry(g_display);
    wl_registry_add_listener(registry, &registry_listener, nullptr);
    wl_display_roundtrip(g_display);
    if (!g_seat || !g_manager) {
        mime_warn("the compositor has no data device: no drag and drop with the desktop");
        return;
    }
    wl_seat_add_listener(g_seat, &seat_listener, nullptr);
    g_device = wl_data_device_manager_get_data_device(g_manager, g_seat);
    wl_data_device_add_listener(g_device, &device_listener, nullptr);
    wl_display_roundtrip(g_display);
}
