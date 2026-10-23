/* camera discovery, hotplug and frame delivery to the guest. mime/camera.h defines shared stream memory. */
#include "camera.h"

#include "backend.h"
#include "convert.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_STREAMS 16

typedef struct Stream {
    bool used;
    bool running;
    uint32_t handle;
    uint32_t camera;
    CameraSource source; /* a copy for the backend, without formats. */
    MimeCameraFormat format;
    uint32_t out_fourcc;
    bool convert;
    uint32_t frame_bytes;
    MimeCameraHeader *hdr;
    uint8_t *data;
    uint32_t slots, slot_bytes;
    CameraConverter converter;
    void *backend_stream;
    uint64_t frames;     /* published, touched by the delivery thread only. */
    uint64_t last_wake_ns;
    uint32_t next_slot;
    pthread_mutex_t lock; /* start and stop. */
} Stream;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_scanned = PTHREAD_COND_INITIALIZER;
static CameraList g_cameras;
static uint32_t g_next_id = 1;
static MimeCameraNotify g_notify;
static bool g_started, g_have_scan;
static Stream g_streams[MAX_STREAMS];

static const CameraBackend *const g_backends[] = {
    &camera_backend_fake,
#ifdef __linux__
    &camera_backend_v4l2,
#endif
};

static unsigned backend_index(const CameraBackend *b)
{
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_backends); i++)
        if (g_backends[i] == b)
            return (unsigned)i;
    return 0;
}

uint64_t camera_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

CameraSource *camera_list_add(CameraList *list)
{
    if (list->count == list->capacity) {
        uint32_t capacity = list->capacity ? list->capacity * 2 : 4;
        CameraSource *p = realloc(list->source, capacity * sizeof(*p));
        if (!p)
            return nullptr;
        list->source = p;
        list->capacity = capacity;
    }
    CameraSource *s = &list->source[list->count++];
    memset(s, 0, sizeof(*s));
    return s;
}

static void list_free(CameraList *list)
{
    for (uint32_t i = 0; i < list->count; i++)
        free(list->source[i].formats);
    free(list->source);
    memset(list, 0, sizeof(*list));
}

static bool same_formats(const CameraSource *a, const CameraSource *b)
{
    return a->format_count == b->format_count &&
           !memcmp(a->formats, b->formats, a->format_count * sizeof(*a->formats));
}

static CameraSource *find_source(uint32_t id)
{
    for (uint32_t i = 0; i < g_cameras.count; i++)
        if (g_cameras.source[i].info.id == id)
            return &g_cameras.source[i];
    return nullptr;
}

static Stream *find_stream(uint64_t handle)
{
    if (handle == 0 || handle > MAX_STREAMS)
        return nullptr;
    Stream *s = &g_streams[handle - 1];
    return s->used ? s : nullptr;
}

/* ------------------------------------------------------------------ the cameras */

/* caches each backend's last scan so one backend's changes do not rescan the others. */
static CameraList g_found[MIME_ARRAY_COUNT(g_backends)];

static CameraList combine(void)
{
    CameraList list = {};
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_backends); i++) {
        for (uint32_t k = 0; k < g_found[i].count; k++) {
            CameraSource *s = camera_list_add(&list);
            if (!s)
                continue;
            *s = g_found[i].source[k];
            s->formats = malloc(s->format_count * sizeof(*s->formats) + 1);
            if (s->formats)
                memcpy(s->formats, g_found[i].source[k].formats, s->format_count * sizeof(*s->formats));
            else
                s->format_count = 0;
        }
    }
    return list;
}

/* rescans backends selected by mask and reports changes in cameras or modes. */
static bool rescan(unsigned dirty)
{
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_backends); i++) {
        if (!(dirty & 1u << i))
            continue;
        CameraList fresh = {};
        g_backends[i]->scan(&fresh);
        for (uint32_t k = 0; k < fresh.count; k++)
            fresh.source[k].backend = g_backends[i];
        pthread_mutex_lock(&g_lock);
        CameraList old = g_found[i];
        g_found[i] = fresh;
        pthread_mutex_unlock(&g_lock);
        list_free(&old);
    }
    pthread_mutex_lock(&g_lock);
    CameraList list = combine();
    pthread_mutex_unlock(&g_lock);
    if (list.count > MIME_CAMERA_MAX)
        list.count = MIME_CAMERA_MAX;
    bool changed = false;
    pthread_mutex_lock(&g_lock);
    for (uint32_t i = 0; i < list.count; i++) {
        CameraSource *n = &list.source[i];
        CameraSource *old = nullptr;
        for (uint32_t k = 0; k < g_cameras.count; k++)
            if (!strcmp(g_cameras.source[k].key, n->key))
                old = &g_cameras.source[k];
        if (old) {
            n->info.id = old->info.id;
            n->info.generation = old->info.generation;
            if (!same_formats(old, n))
                changed = true;
        } else {
            n->info.id = g_next_id++;
            n->info.generation = 1;
            changed = true;
            mime_info("camera: %s (%s) plugged in", n->info.name, n->key);
        }
        n->info.formats = n->format_count;
    }
    for (uint32_t k = 0; k < g_cameras.count; k++) {
        bool there = false;
        for (uint32_t i = 0; i < list.count; i++)
            there |= !strcmp(g_cameras.source[k].key, list.source[i].key);
        if (!there) {
            changed = true;
            mime_info("camera: %s unplugged", g_cameras.source[k].info.name);
        }
    }
    CameraList old = g_cameras;
    g_cameras = list;
    g_have_scan = true;
    pthread_cond_broadcast(&g_scanned);
    /* marks removed cameras' streams and wakes waiting readers. */
    for (int i = 0; i < MAX_STREAMS; i++) {
        Stream *s = &g_streams[i];
        if (s->used && !find_source(s->camera)) {
            __atomic_store_n(&s->hdr->error, MIME_CAMERA_ERROR_LOST, __ATOMIC_RELEASE);
            if (g_notify)
                g_notify(MIME_CAMERA_EVENT_FRAME, s->handle);
        }
    }
    pthread_mutex_unlock(&g_lock);
    list_free(&old);
    return changed;
}

static void *monitor(void *arg)
{
    mime_set_thread_name("mime-camera");
    rescan((1u << MIME_ARRAY_COUNT(g_backends)) - 1);
    uint64_t next_poll[MIME_ARRAY_COUNT(g_backends)] = {};
    for (;;) {
        struct pollfd fds[MIME_ARRAY_COUNT(g_backends)];
        const CameraBackend *owner[MIME_ARRAY_COUNT(g_backends)];
        int n = 0, timeout = -1;
        uint64_t now = camera_now_ns();
        for (size_t i = 0; i < MIME_ARRAY_COUNT(g_backends); i++) {
            const CameraBackend *b = g_backends[i];
            int fd = b->watch_fd ? b->watch_fd() : -1;
            if (fd >= 0) {
                fds[n] = (struct pollfd){.fd = fd, .events = POLLIN};
                owner[n++] = b;
            }
            /* timed backends have independent deadlines, unaffected by events from other backends. */
            int period = b->poll_ms ? b->poll_ms() : 0;
            if (!period) {
                next_poll[i] = 0;
                continue;
            }
            if (!next_poll[i])
                next_poll[i] = now + (uint64_t)period * 1000000ull;
            int left = next_poll[i] > now ? (int)((next_poll[i] - now + 999999ull) / 1000000ull) : 0;
            if (timeout < 0 || left < timeout)
                timeout = left;
        }
        int ready = poll(fds, (nfds_t)n, timeout);
        if (ready < 0 && errno != EINTR)
            usleep(200000);
        unsigned dirty = 0;
        bool woke = false;
        for (int i = 0; i < n; i++)
            if (ready > 0 && fds[i].revents) {
                woke = true;
                if (!owner[i]->watch_drain || owner[i]->watch_drain())
                    dirty |= 1u << backend_index(owner[i]);
            }
        /* a device node is made before udev gives it its permissions: wait for the burst to be over. */
        if (woke && dirty) {
            usleep(150000);
            for (int i = 0; i < n; i++)
                if (owner[i]->watch_drain)
                    owner[i]->watch_drain();
        }
        now = camera_now_ns();
        for (size_t i = 0; i < MIME_ARRAY_COUNT(g_backends); i++)
            if (next_poll[i] && now >= next_poll[i]) {
                dirty |= 1u << i;
                next_poll[i] = 0;
            }
        if (dirty && rescan(dirty) && g_notify)
            g_notify(MIME_CAMERA_EVENT_CHANGED, 0);
    }
    return nullptr;
}

void mime_camera_init(MimeCameraNotify notify)
{
    pthread_mutex_lock(&g_lock);
    g_notify = notify;
    bool start = !g_started;
    g_started = true;
    pthread_mutex_unlock(&g_lock);
    if (!start)
        return;
    for (int i = 0; i < MAX_STREAMS; i++)
        pthread_mutex_init(&g_streams[i].lock, nullptr);
    pthread_t t;
    if (pthread_create(&t, nullptr, monitor, nullptr) == 0)
        pthread_detach(t);
}

/*
 * a hung camera driver must not block guest boot. limits the initial scan wait to a few seconds, then returns the
 * partial list; later discoveries arrive as hotplug events.
 */
static void wait_for_scan(void)
{
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += 5;
    pthread_mutex_lock(&g_lock);
    while (g_started && !g_have_scan && pthread_cond_timedwait(&g_scanned, &g_lock, &until) == 0)
        ;
    pthread_mutex_unlock(&g_lock);
}

uint64_t mime_camera_list(MimeVm *vm, uint64_t pa, uint64_t capacity, uint64_t *count)
{
    wait_for_scan();
    if (capacity > MIME_CAMERA_MAX)
        capacity = MIME_CAMERA_MAX;
    MimeCameraInfo *dst = capacity ? mime_vm_phys(vm, pa, capacity * sizeof(*dst)) : nullptr;
    if (capacity && !dst)
        return MIME_HC_STATUS_INVALID;
    pthread_mutex_lock(&g_lock);
    *count = g_cameras.count;
    for (uint32_t i = 0; i < g_cameras.count && i < capacity; i++)
        dst[i] = g_cameras.source[i].info;
    pthread_mutex_unlock(&g_lock);
    return MIME_HC_STATUS_OK;
}

uint64_t mime_camera_formats(MimeVm *vm, uint64_t camera, uint64_t pa, uint64_t capacity, uint64_t *count)
{
    if (capacity > MIME_CAMERA_MAX_FORMATS)
        capacity = MIME_CAMERA_MAX_FORMATS;
    MimeCameraFormat *dst = capacity ? mime_vm_phys(vm, pa, capacity * sizeof(*dst)) : nullptr;
    if (capacity && !dst)
        return MIME_HC_STATUS_INVALID;
    pthread_mutex_lock(&g_lock);
    CameraSource *s = find_source((uint32_t)camera);
    uint64_t status = MIME_HC_STATUS_NOT_FOUND;
    if (s) {
        *count = s->format_count;
        if (dst)
            memcpy(dst, s->formats, MIME_MIN(capacity, s->format_count) * sizeof(*dst));
        status = MIME_HC_STATUS_OK;
    }
    pthread_mutex_unlock(&g_lock);
    return status;
}

/* ------------------------------------------------------------------ streams */

static bool same_rate(const MimeCameraFormat *a, const MimeCameraFormat *b)
{
    return (uint64_t)a->fps_num * b->fps_den == (uint64_t)b->fps_num * a->fps_den;
}

uint64_t mime_camera_open(MimeVm *vm, uint64_t camera, uint64_t pa, uint64_t size, uint64_t *stream)
{
    if (size < MIME_CAMERA_HEADER_SIZE || (pa & 4095))
        return MIME_HC_STATUS_INVALID;
    MimeCameraHeader *hdr = mime_vm_phys(vm, pa, size);
    if (!hdr)
        return MIME_HC_STATUS_INVALID;
    /* snapshots the request because the guest can change shared memory concurrently. */
    MimeCameraHeader req;
    memcpy(&req, hdr, offsetof(MimeCameraHeader, frame_fourcc));
    MimeCameraFormat want = {req.source_fourcc, req.width, req.height, req.fps_num, req.fps_den};
    if (!req.slots || req.slots > MIME_CAMERA_MAX_SLOTS || !req.slot_bytes || (req.slot_bytes & 4095) ||
        (req.data_offset & 4095) || req.data_offset < MIME_CAMERA_HEADER_SIZE ||
        (uint64_t)req.data_offset + (uint64_t)req.slots * req.slot_bytes > size || !want.fps_num || !want.fps_den)
        return MIME_HC_STATUS_INVALID;

    pthread_mutex_lock(&g_lock);
    CameraSource *src = find_source((uint32_t)camera);
    if (!src) {
        pthread_mutex_unlock(&g_lock);
        return MIME_HC_STATUS_NOT_FOUND;
    }
    bool offered = false;
    for (uint32_t i = 0; i < src->format_count && !offered; i++) {
        const MimeCameraFormat *f = &src->formats[i];
        offered = f->fourcc == want.fourcc && f->width == want.width && f->height == want.height && same_rate(f, &want);
    }
    uint32_t out = req.output_fourcc ? req.output_fourcc : want.fourcc;
    bool convert = out != want.fourcc;
    uint32_t frame_bytes = want.fourcc == MIME_CAMERA_FOURCC_MJPG && !convert ? 0 : mime_camera_frame_bytes(out, want.width, want.height);
    bool supported = offered && (convert ? camera_can_convert(want.fourcc, out) : true) &&
                     (want.fourcc != MIME_CAMERA_FOURCC_MJPG || convert || out == MIME_CAMERA_FOURCC_MJPG) &&
                     (frame_bytes ? req.slot_bytes >= frame_bytes : true);
    Stream *s = nullptr;
    for (int i = 0; i < MAX_STREAMS && supported && !s; i++)
        if (!g_streams[i].used)
            s = &g_streams[i];
    if (!supported || !s) {
        pthread_mutex_unlock(&g_lock);
        return !offered || !supported ? MIME_HC_STATUS_UNSUPPORTED : MIME_HC_STATUS_NO_MEMORY;
    }
    pthread_mutex_lock(&s->lock);
    memset(&s->converter, 0, sizeof(s->converter));
    s->camera = src->info.id;
    s->source = *src;
    s->source.formats = nullptr;
    s->format = want;
    s->out_fourcc = out;
    s->convert = convert;
    s->frame_bytes = frame_bytes;
    s->hdr = hdr;
    s->data = (uint8_t *)hdr + req.data_offset;
    s->slots = req.slots;
    s->slot_bytes = req.slot_bytes;
    s->frames = 0;
    s->last_wake_ns = 0;
    s->next_slot = 0;
    s->running = false;
    s->backend_stream = nullptr;
    s->handle = (uint32_t)(s - g_streams) + 1;
    s->used = true;
    pthread_mutex_unlock(&s->lock);
    pthread_mutex_unlock(&g_lock);

    uint32_t stride = out == MIME_CAMERA_FOURCC_YUY2 || out == MIME_CAMERA_FOURCC_UYVY ? (want.width + 1) / 2 * 4 : want.width;
    memset((uint8_t *)hdr + offsetof(MimeCameraHeader, frame_fourcc), 0,
           sizeof(MimeCameraHeader) - offsetof(MimeCameraHeader, frame_fourcc));
    hdr->frame_fourcc = out;
    hdr->frame_bytes = frame_bytes;
    hdr->stride = stride;
    hdr->open_ns = camera_now_ns();
    hdr->interval_100ns = (uint64_t)10000000 * want.fps_den / want.fps_num;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    *stream = s->handle;
    return MIME_HC_STATUS_OK;
}

/* uses a free slot or replaces the oldest frame the guest has not taken. */
static int take_slot(Stream *s)
{
    MimeCameraHeader *h = s->hdr;
    for (uint32_t i = 0; i < s->slots; i++) {
        uint32_t k = (s->next_slot + i) % s->slots;
        uint32_t expected = MIME_CAMERA_SLOT_FREE;
        if (__atomic_compare_exchange_n(&h->slot[k].state, &expected, MIME_CAMERA_SLOT_WRITING, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            s->next_slot = (k + 1) % s->slots;
            return (int)k;
        }
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        int oldest = -1;
        uint64_t oldest_sequence = UINT64_MAX;
        for (uint32_t i = 0; i < s->slots; i++) {
            if (__atomic_load_n(&h->slot[i].state, __ATOMIC_ACQUIRE) == MIME_CAMERA_SLOT_READY &&
                h->slot[i].sequence < oldest_sequence) {
                oldest = (int)i;
                oldest_sequence = h->slot[i].sequence;
            }
        }
        if (oldest < 0)
            return -1;
        uint32_t expected = MIME_CAMERA_SLOT_READY;
        if (__atomic_compare_exchange_n(&h->slot[oldest].state, &expected, MIME_CAMERA_SLOT_WRITING, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            s->next_slot = ((uint32_t)oldest + 1) % s->slots;
            return oldest;
        }
    }
    return -1;
}

static void wake_guest(Stream *s)
{
    uint64_t now = camera_now_ns();
    uint32_t was = __atomic_exchange_n(&s->hdr->wake, 1, __ATOMIC_ACQ_REL);
    /* repeats notifications periodically so a dropped interrupt cannot leave the guest waiting forever. */
    if (!was || now - s->last_wake_ns > 250000000ull) {
        s->last_wake_ns = now;
        if (g_notify)
            g_notify(MIME_CAMERA_EVENT_FRAME, s->handle);
    }
}

static void deliver(void *ctx, const uint8_t *data, size_t length, uint64_t host_ns, uint32_t flags)
{
    Stream *s = ctx;
    MimeCameraHeader *h = s->hdr;
    int k = take_slot(s);
    if (k < 0) {
        __atomic_fetch_add(&h->dropped, 1, __ATOMIC_RELAXED);
        return;
    }
    uint8_t *dst = s->data + (size_t)k * s->slot_bytes;
    size_t out_length;
    if (s->convert) {
        int result = camera_convert(&s->converter, s->format.fourcc, data, length, s->format.width, s->format.height,
                                    s->out_fourcc, dst);
        if (result == CAMERA_CONVERT_FAILED) {
            __atomic_store_n(&h->slot[k].state, MIME_CAMERA_SLOT_FREE, __ATOMIC_RELEASE);
            __atomic_fetch_add(&h->dropped, 1, __ATOMIC_RELAXED);
            return;
        }
        if (result == CAMERA_CONVERT_DAMAGED)
            flags |= MIME_CAMERA_FRAME_CORRUPT;
        out_length = s->frame_bytes;
    } else {
        out_length = s->frame_bytes ? s->frame_bytes : length;
        if (length < out_length || out_length > s->slot_bytes) {
            __atomic_store_n(&h->slot[k].state, MIME_CAMERA_SLOT_FREE, __ATOMIC_RELEASE);
            __atomic_fetch_add(&h->dropped, 1, __ATOMIC_RELAXED);
            return;
        }
        memcpy(dst, data, out_length);
    }
    MimeCameraSlot *m = &h->slot[k];
    m->length = (uint32_t)out_length;
    m->host_ns = host_ns;
    m->flags = flags;
    m->sequence = ++s->frames;
    __atomic_store_n(&h->sequence, s->frames, __ATOMIC_RELAXED);
    __atomic_store_n(&m->state, MIME_CAMERA_SLOT_READY, __ATOMIC_RELEASE);
    wake_guest(s);
}

static void failed(void *ctx, bool lost)
{
    Stream *s = ctx;
    __atomic_store_n(&s->hdr->error, lost ? MIME_CAMERA_ERROR_LOST : MIME_CAMERA_ERROR_FAILED, __ATOMIC_RELEASE);
    wake_guest(s);
}

uint64_t mime_camera_start(uint64_t handle, uint64_t running)
{
    pthread_mutex_lock(&g_lock);
    Stream *s = find_stream(handle);
    if (!s) {
        pthread_mutex_unlock(&g_lock);
        return MIME_HC_STATUS_INVALID;
    }
    pthread_mutex_lock(&s->lock);
    pthread_mutex_unlock(&g_lock);
    uint64_t status = MIME_HC_STATUS_OK;
    if (running && !s->running) {
        __atomic_store_n(&s->hdr->error, MIME_CAMERA_ERROR_NONE, __ATOMIC_RELAXED);
        s->backend_stream = s->source.backend->start(&s->source, &s->format, deliver, failed, s, &status);
        if (s->backend_stream)
            s->running = true;
        else if (status == MIME_HC_STATUS_OK)
            status = MIME_HC_STATUS_IO_ERROR;
    } else if (!running && s->running) {
        s->source.backend->stop(s->backend_stream);
        s->backend_stream = nullptr;
        s->running = false;
    }
    pthread_mutex_unlock(&s->lock);
    return status;
}

void mime_camera_close(uint64_t handle)
{
    pthread_mutex_lock(&g_lock);
    Stream *s = find_stream(handle);
    if (!s) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    pthread_mutex_lock(&s->lock);
    /* removes the stream from lookup and waits for its backend thread to stop. */
    s->used = false;
    pthread_mutex_unlock(&g_lock);
    if (s->running)
        s->source.backend->stop(s->backend_stream);
    s->running = false;
    s->backend_stream = nullptr;
    camera_converter_free(&s->converter);
    s->hdr = nullptr;
    pthread_mutex_unlock(&s->lock);
}

void mime_camera_close_all(void)
{
    for (uint64_t i = 1; i <= MAX_STREAMS; i++)
        mime_camera_close(i);
}

uint64_t mime_camera_control(MimeVm *vm, uint64_t pa)
{
    MimeCameraControl *c = mime_vm_phys(vm, pa, sizeof(*c));
    if (!c)
        return MIME_HC_STATUS_INVALID;
    MimeCameraControl req = *c;
    req.status = MIME_HC_STATUS_NOT_FOUND;
    pthread_mutex_lock(&g_lock);
    CameraSource *src = find_source(req.id);
    CameraSource copy = {};
    if (src) {
        copy = *src;
        copy.formats = nullptr;
    }
    pthread_mutex_unlock(&g_lock);
    if (src) {
        req.status = MIME_HC_STATUS_UNSUPPORTED;
        if (copy.backend->control)
            copy.backend->control(&copy, &req);
    }
    *c = req;
    return MIME_HC_STATUS_OK;
}
