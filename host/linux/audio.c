#include "../audio/audio.h"
#include "pipewire.h"

#include <mime/audio.h>

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_STREAMS 64

typedef struct Stream {
    MimePwStream *stream;
    MimeAudioHeader *hdr;
    uint8_t *ring;
    uint32_t frames, frame_bytes;
    uint32_t channels; /* as checked when the stream opened: the header is the guest's to change. */
    uint32_t bits;
    bool is_float;
    bool running;
    bool capture;
    bool failed; /* the server gave the stream up, and the guest is told. */
    uint32_t device; /* the one the guest named, 0 when the stream follows the default one. */
} Stream;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Stream *g_streams[MAX_STREAMS];
static bool g_audio;
static bool g_watcher;
static MimeVm *g_vm;
static MimeAudioDeviceList g_known[2]; /* output and input, at the watcher's last look. */

static bool is_socket(const char *dir, const char *name)
{
    char path[PATH_MAX];
    struct stat st;
    if (!dir || snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path))
        return false;
    return stat(path, &st) == 0 && S_ISSOCK(st.st_mode);
}

/*
 * the sound server listens in the user's own runtime directory. a launcher that points XDG_RUNTIME_DIR elsewhere would
 * leave the machine without it, so name it directly while the daemon is still single threaded.
 */
void mime_audio_prepare(void)
{
    char own[64];
    snprintf(own, sizeof(own), "/run/user/%u", (unsigned)getuid());
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime && !strcmp(runtime, own))
        return;
    if (!getenv("PIPEWIRE_RUNTIME_DIR") && !is_socket(runtime, "pipewire-0") && is_socket(own, "pipewire-0"))
        setenv("PIPEWIRE_RUNTIME_DIR", own, 1);
}

/* the guest's volume, 65536 for all of it, on samples of the stream's format. */
static void scale(const Stream *s, uint8_t *data, uint32_t frames)
{
    uint32_t volume = __atomic_load_n(&s->hdr->volume, __ATOMIC_RELAXED);
    if (volume >= 65536)
        return;
    uint32_t samples = frames * s->channels;
    if (s->is_float) {
        float gain = volume / 65536.0f, v;
        for (uint32_t i = 0; i < samples; i++, data += 4) {
            memcpy(&v, data, 4);
            v *= gain;
            memcpy(data, &v, 4);
        }
    } else if (s->bits == 16) {
        int16_t v;
        for (uint32_t i = 0; i < samples; i++, data += 2) {
            memcpy(&v, data, 2);
            v = (int16_t)(v * (int32_t)volume >> 16);
            memcpy(data, &v, 2);
        }
    } else if (s->bits == 24) {
        for (uint32_t i = 0; i < samples; i++, data += 3) {
            int32_t v = (int32_t)((uint32_t)data[0] << 8 | (uint32_t)data[1] << 16 | (uint32_t)data[2] << 24) >> 8;
            v = (int32_t)(v * (int64_t)volume >> 16);
            data[0] = (uint8_t)v;
            data[1] = (uint8_t)(v >> 8);
            data[2] = (uint8_t)(v >> 16);
        }
    } else {
        int32_t v;
        for (uint32_t i = 0; i < samples; i++, data += 4) {
            memcpy(&v, data, 4);
            v = (int32_t)(v * (int64_t)volume >> 16);
            memcpy(data, &v, 4);
        }
    }
}

static void render(void *ref, uint8_t *out, uint32_t count)
{
    Stream *s = ref;
    MimeAudioHeader *h = s->hdr;
    uint64_t played = __atomic_load_n(&h->played, __ATOMIC_RELAXED);
    uint64_t queued = __atomic_load_n(&h->written, __ATOMIC_ACQUIRE) - played;
    /* the guest owns written: a value past the ring only garbles its own sound. */
    if (queued > s->frames)
        queued = s->frames;
    uint32_t n = queued < count ? (uint32_t)queued : count;
    uint32_t at = (uint32_t)(played % s->frames);
    uint32_t first = s->frames - at < n ? s->frames - at : n;
    memcpy(out, s->ring + (size_t)at * s->frame_bytes, (size_t)first * s->frame_bytes);
    memcpy(out + (size_t)first * s->frame_bytes, s->ring, (size_t)(n - first) * s->frame_bytes);
    memset(out + (size_t)n * s->frame_bytes, 0, (size_t)(count - n) * s->frame_bytes);
    __atomic_store_n(&h->played, played + n, __ATOMIC_RELEASE);
    if (n < count)
        __atomic_fetch_add(&h->underruns, 1, __ATOMIC_RELAXED);
    scale(s, out, n);
}

/* appends captured frames to the ring. excess frames are dropped and counted. the server's own buffer is not written. */
static void capture_render(void *ref, uint8_t *in, uint32_t frames)
{
    Stream *s = ref;
    MimeAudioHeader *h = s->hdr;
    uint64_t written = __atomic_load_n(&h->written, __ATOMIC_RELAXED);
    uint64_t used = written - __atomic_load_n(&h->played, __ATOMIC_ACQUIRE);
    uint64_t room = used >= s->frames ? 0 : s->frames - used;
    uint32_t n = frames < room ? frames : (uint32_t)room;
    uint32_t at = (uint32_t)(written % s->frames);
    uint32_t first = s->frames - at < n ? s->frames - at : n;
    memcpy(s->ring + (size_t)at * s->frame_bytes, in, (size_t)first * s->frame_bytes);
    memcpy(s->ring, in + (size_t)first * s->frame_bytes, (size_t)(n - first) * s->frame_bytes);
    scale(s, s->ring + (size_t)at * s->frame_bytes, first);
    scale(s, s->ring, n - first);
    if (n < frames)
        __atomic_fetch_add(&h->underruns, frames - n, __ATOMIC_RELAXED);
    __atomic_store_n(&h->written, written + n, __ATOMIC_RELEASE);
}

static void destroy(Stream *s)
{
    /* closing waits for a callback in progress, after which the guest may reuse the memory. */
    if (s->stream)
        mime_pipewire_close(s->stream);
    free(s);
}

static void look(bool capture, MimeAudioDeviceList *out)
{
    mime_pipewire_list(capture, out);
}

static bool listed(const MimeAudioDeviceList *list, uint32_t id)
{
    for (uint32_t i = 0; i < list->count; i++)
        if (list->devices[i].id == id)
            return true;
    return false;
}

/* the lists are compared a few times a second, which also tells of a change of the default device. */
static void *watcher(void *arg)
{
    struct timespec tick = {0, 200000000};
    for (;;) {
        nanosleep(&tick, nullptr);
        MimeAudioDeviceList now[2];
        look(false, &now[0]);
        look(true, &now[1]);
        pthread_mutex_lock(&g_lock);
        bool changed = memcmp(now, g_known, sizeof(now)) != 0;
        bool failed = false;
        for (uint32_t i = 1; i < MAX_STREAMS; i++) {
            Stream *s = g_streams[i];
            if (!s || s->failed || !mime_pipewire_failed(s->stream))
                continue;
            s->failed = failed = true;
            mime_warn("audio: the sound server gave up an %s stream", s->capture ? "input" : "output");
            __atomic_store_n(&s->hdr->lost, 1, __ATOMIC_RELEASE);
        }
        if (changed) {
            memcpy(g_known, now, sizeof(now));
            /* the stream of a device that is gone runs into nothing. */
            for (uint32_t i = 1; i < MAX_STREAMS; i++) {
                Stream *s = g_streams[i];
                if (!s)
                    continue;
                const MimeAudioDeviceList *list = &g_known[s->capture];
                /* a stream that follows the default device has none to move to when the last one goes. */
                if (s->device ? !listed(list, s->device) : !list->count)
                    __atomic_store_n(&s->hdr->lost, 1, __ATOMIC_RELEASE);
            }
        }
        if ((changed || failed) && g_vm)
            mime_vm_raise_irq(g_vm, MIME_IRQ_AUDIO);
        pthread_mutex_unlock(&g_lock);
    }
    return nullptr;
}

static bool audio_ready(MimeVm *vm)
{
    pthread_mutex_lock(&g_lock);
    g_vm = vm;
    if (!g_audio)
        g_audio = mime_pipewire_start();
    if (g_audio && !g_watcher) {
        look(false, &g_known[0]);
        look(true, &g_known[1]);
        pthread_t thread;
        g_watcher = pthread_create(&thread, nullptr, watcher, nullptr) == 0;
        if (g_watcher)
            pthread_detach(thread);
    }
    bool ready = g_audio;
    pthread_mutex_unlock(&g_lock);
    return ready;
}

uint64_t mime_audio_devices(MimeVm *vm, uint64_t pa, uint64_t size, bool capture)
{
    MimeAudioDeviceList *list = nullptr;
    if (size >= sizeof(*list))
        list = mime_vm_phys(vm, pa, sizeof(*list));
    if (!list)
        return MIME_HC_STATUS_INVALID;
    MimeAudioDeviceList found = {0};
    if (audio_ready(vm))
        look(capture, &found);
    *list = found;
    return MIME_HC_STATUS_OK;
}

uint64_t mime_audio_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *stream)
{
    if (size != MIME_AUDIO_STREAM_SIZE)
        return MIME_HC_STATUS_INVALID;
    uint8_t *base = mime_vm_phys(vm, pa, size);
    if (!base)
        return MIME_HC_STATUS_INVALID;
    MimeAudioHeader h = *(MimeAudioHeader *)base;
    bool valid_bits = h.is_float ? h.bits == 32 : h.bits == 16 || h.bits == 24 || h.bits == 32;
    if (!valid_bits || h.channels < 1 || h.channels > 8 || h.rate < 8000 || h.rate > 192000)
        return MIME_HC_STATUS_UNSUPPORTED;
    uint32_t frame_bytes = h.channels * h.bits / 8;
    if (!h.frames || h.frames > (size - MIME_AUDIO_HEADER_SIZE) / frame_bytes)
        return MIME_HC_STATUS_INVALID;

    /* a host without a device of the direction has none to follow either. */
    MimeAudioDeviceList now;
    if (!audio_ready(vm))
        return MIME_HC_STATUS_NOT_FOUND;
    look(h.capture != 0, &now);
    if (h.device ? !listed(&now, h.device) : !now.count)
        return MIME_HC_STATUS_NOT_FOUND;

    Stream *s = calloc(1, sizeof(Stream));
    s->hdr = (MimeAudioHeader *)base;
    s->ring = base + MIME_AUDIO_HEADER_SIZE;
    s->frames = h.frames;
    s->frame_bytes = frame_bytes;
    s->channels = h.channels;
    s->bits = h.bits;
    s->is_float = h.is_float != 0;
    s->capture = h.capture != 0;
    s->device = h.device;
    s->stream = mime_pipewire_open(s->capture, h.device, h.rate, h.channels, h.bits, s->is_float,
                                   s->capture ? capture_render : render, s);
    if (!s->stream) {
        mime_warn("audio: the host %s device does not open", s->capture ? "input" : "output");
        free(s);
        return MIME_HC_STATUS_IO_ERROR;
    }
    pthread_mutex_lock(&g_lock);
    for (uint32_t i = 1; i < MAX_STREAMS; i++) {
        if (!g_streams[i]) {
            g_streams[i] = s;
            *stream = i;
            pthread_mutex_unlock(&g_lock);
            return MIME_HC_STATUS_OK;
        }
    }
    pthread_mutex_unlock(&g_lock);
    destroy(s);
    return MIME_HC_STATUS_NO_MEMORY;
}

uint64_t mime_audio_start(uint64_t stream, uint64_t running)
{
    pthread_mutex_lock(&g_lock);
    Stream *s = stream < MAX_STREAMS ? g_streams[stream] : nullptr;
    bool ok = true;
    if (s && s->running != (running != 0)) {
        ok = mime_pipewire_run(s->stream, running != 0);
        if (ok)
            s->running = running != 0;
    }
    pthread_mutex_unlock(&g_lock);
    if (!s)
        return MIME_HC_STATUS_INVALID;
    return ok ? MIME_HC_STATUS_OK : MIME_HC_STATUS_IO_ERROR;
}

void mime_audio_close(uint64_t stream)
{
    pthread_mutex_lock(&g_lock);
    Stream *s = stream < MAX_STREAMS ? g_streams[stream] : nullptr;
    if (s)
        g_streams[stream] = nullptr;
    pthread_mutex_unlock(&g_lock);
    if (s)
        destroy(s);
}

void mime_audio_close_all(void)
{
    pthread_mutex_lock(&g_lock);
    g_vm = nullptr;
    pthread_mutex_unlock(&g_lock);
    for (uint64_t i = 0; i < MAX_STREAMS; i++)
        mime_audio_close(i);
}
