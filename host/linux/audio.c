/*
 * each guest stream uses an SDL stream on the device it names or on the default one, reading directly from the ring
 * in mime/audio.h. SDL converts format, rate and channels through PipeWire, PulseAudio or ALSA.
 */
#include "../audio/audio.h"

#include <mime/audio.h>

#include <SDL3/SDL.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_STREAMS 64
#define CHUNK_BYTES 8192

typedef struct Stream {
    SDL_AudioStream *stream;
    MimeAudioHeader *hdr;
    uint8_t *ring;
    uint32_t frames, frame_bytes, out_frame_bytes;
    bool packed24; /* 24 bit samples, which SDL takes widened to 32 bits. */
    uint32_t volume;
    bool running;
    bool capture;
    uint32_t device; /* the one the guest named, 0 when the stream follows the default one. */
    bool silent; /* no host device: a clock plays the ring into nothing or records silence. */
    uint64_t paced_ns;
    uint64_t paced_frames;
} Stream;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Stream *g_streams[MAX_STREAMS];
static bool g_audio;
static bool g_pacer;
static bool g_warned;
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
 * leave SDL without it, so name it directly while the daemon is still single threaded.
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
    if (!getenv("PULSE_SERVER") && !is_socket(runtime, "pulse/native") && is_socket(own, "pulse/native")) {
        char server[96];
        snprintf(server, sizeof(server), "unix:%s/pulse/native", own);
        setenv("PULSE_SERVER", server, 1);
    }
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* moves a silent stream on by the frames its rate owes since the last tick, as a device would. */
static void pace_stream(Stream *s, uint64_t now)
{
    MimeAudioHeader *h = s->hdr;
    uint64_t due = (now - s->paced_ns) * h->rate / 1000000000ull;
    if (!due)
        return;
    s->paced_ns += due * 1000000000ull / h->rate;
    s->paced_frames += due;
    uint64_t played = __atomic_load_n(&h->played, __ATOMIC_RELAXED);
    uint64_t written = __atomic_load_n(&h->written, __ATOMIC_ACQUIRE);
    if (s->capture) {
        uint64_t used = written - played;
        uint64_t room = used >= s->frames ? 0 : s->frames - used;
        uint64_t n = MIME_MIN(due, room);
        for (uint64_t i = 0; i < n; i++)
            memset(s->ring + (size_t)((written + i) % s->frames) * s->frame_bytes, 0, s->frame_bytes);
        if (n < due)
            __atomic_fetch_add(&h->underruns, (uint32_t)(due - n), __ATOMIC_RELAXED);
        __atomic_store_n(&h->written, written + n, __ATOMIC_RELEASE);
        return;
    }
    uint64_t queued = MIME_MIN(written - played, (uint64_t)s->frames);
    uint64_t n = MIME_MIN(due, queued);
    __atomic_store_n(&h->played, played + n, __ATOMIC_RELEASE);
    if (n < due)
        __atomic_fetch_add(&h->underruns, 1, __ATOMIC_RELAXED);
}

static void *pacer(void *arg)
{
    struct timespec tick = {0, 5000000};
    for (;;) {
        nanosleep(&tick, nullptr);
        uint64_t now = now_ns();
        pthread_mutex_lock(&g_lock);
        for (uint32_t i = 1; i < MAX_STREAMS; i++)
            if (g_streams[i] && g_streams[i]->silent && g_streams[i]->running)
                pace_stream(g_streams[i], now);
        pthread_mutex_unlock(&g_lock);
    }
    return nullptr;
}

/* copies n frames from the ring at frame `at` into out, widening 24 bit samples. */
static void copy_frames(Stream *s, uint8_t *out, uint32_t at, uint32_t n)
{
    const uint8_t *in = s->ring + (size_t)at * s->frame_bytes;
    if (!s->packed24) {
        memcpy(out, in, (size_t)n * s->frame_bytes);
        return;
    }
    uint32_t samples = n * s->hdr->channels;
    for (uint32_t i = 0; i < samples; i++, in += 3, out += 4) {
        out[0] = 0;
        out[1] = in[0];
        out[2] = in[1];
        out[3] = in[2];
    }
}

static void SDLCALL render(void *ref, SDL_AudioStream *stream, int additional, int total)
{
    Stream *s = ref;
    MimeAudioHeader *h = s->hdr;
    uint8_t chunk[CHUNK_BYTES];
    uint32_t volume = __atomic_load_n(&h->volume, __ATOMIC_RELAXED);
    if (volume != s->volume) {
        s->volume = volume;
        SDL_SetAudioStreamGain(stream, volume > 65536 ? 1.0f : volume / 65536.0f);
    }
    while (additional > 0) {
        uint32_t count = (uint32_t)MIME_MIN((size_t)additional, sizeof(chunk)) / s->out_frame_bytes;
        if (!count)
            break;
        uint64_t played = __atomic_load_n(&h->played, __ATOMIC_RELAXED);
        uint64_t queued = __atomic_load_n(&h->written, __ATOMIC_ACQUIRE) - played;
        /* the guest owns written: a value past the ring only garbles its own sound. */
        if (queued > s->frames)
            queued = s->frames;
        uint32_t n = queued < count ? (uint32_t)queued : count;
        uint32_t at = (uint32_t)(played % s->frames);
        uint32_t first = s->frames - at < n ? s->frames - at : n;
        copy_frames(s, chunk, at, first);
        copy_frames(s, chunk + (size_t)first * s->out_frame_bytes, 0, n - first);
        memset(chunk + (size_t)n * s->out_frame_bytes, 0, (size_t)(count - n) * s->out_frame_bytes);
        __atomic_store_n(&h->played, played + n, __ATOMIC_RELEASE);
        if (n < count)
            __atomic_fetch_add(&h->underruns, 1, __ATOMIC_RELAXED);
        SDL_PutAudioStreamData(stream, chunk, (int)(count * s->out_frame_bytes));
        additional -= (int)(count * s->out_frame_bytes);
    }
}

/*
 * appends captured frames to the ring, narrowing 24-bit samples to three bytes. excess frames are dropped and
 * counted.
 */
static void SDLCALL capture_render(void *ref, SDL_AudioStream *stream, int additional, int total)
{
    Stream *s = ref;
    MimeAudioHeader *h = s->hdr;
    uint8_t chunk[CHUNK_BYTES];
    uint32_t volume = __atomic_load_n(&h->volume, __ATOMIC_RELAXED);
    if (volume != s->volume) {
        s->volume = volume;
        SDL_SetAudioStreamGain(stream, volume > 65536 ? 1.0f : volume / 65536.0f);
    }
    while (additional > 0) {
        int want = (int)(MIME_MIN((size_t)additional, sizeof(chunk)) / s->out_frame_bytes * s->out_frame_bytes);
        if (want <= 0)
            break;
        int got = SDL_GetAudioStreamData(stream, chunk, want);
        if (got <= 0)
            break;
        uint32_t frames = (uint32_t)got / s->out_frame_bytes;
        uint64_t written = __atomic_load_n(&h->written, __ATOMIC_RELAXED);
        uint64_t used = written - __atomic_load_n(&h->played, __ATOMIC_ACQUIRE);
        uint64_t room = used >= s->frames ? 0 : s->frames - used;
        uint32_t n = frames < room ? frames : (uint32_t)room;
        uint32_t at = (uint32_t)(written % s->frames);
        uint32_t first = s->frames - at < n ? s->frames - at : n;
        for (int part = 0; part < 2; part++) {
            uint32_t count = part ? n - first : first;
            const uint8_t *in = chunk + (part ? (size_t)first * s->out_frame_bytes : 0);
            uint8_t *out = s->ring + (part ? 0 : (size_t)at * s->frame_bytes);
            if (!s->packed24) {
                memcpy(out, in, (size_t)count * s->frame_bytes);
            } else {
                uint32_t samples = count * s->hdr->channels;
                for (uint32_t i = 0; i < samples; i++, in += 4, out += 3) {
                    out[0] = in[1];
                    out[1] = in[2];
                    out[2] = in[3];
                }
            }
        }
        if (n < frames)
            __atomic_fetch_add(&h->underruns, frames - n, __ATOMIC_RELAXED);
        __atomic_store_n(&h->written, written + n, __ATOMIC_RELEASE);
        additional -= got;
    }
}

static void destroy(Stream *s)
{
    /* destroying waits for a callback in progress, after which the guest may reuse the memory. */
    if (s->stream)
        SDL_DestroyAudioStream(s->stream);
    free(s);
}

/*
 * SDL names the default device without telling which one of the list it is, so the first of that name is taken. the
 * list is read as it stands, since SDL keeps it up to date from the sound server's own thread.
 */
static void look(bool capture, MimeAudioDeviceList *out)
{
    memset(out, 0, sizeof(*out));
    int count = 0;
    const char *default_name =
        SDL_GetAudioDeviceName(capture ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
    SDL_AudioDeviceID *ids = capture ? SDL_GetAudioRecordingDevices(&count) : SDL_GetAudioPlaybackDevices(&count);
    bool chosen = false;
    for (int i = 0; ids && i < count && out->count < MIME_AUDIO_MAX_DEVICES; i++) {
        const char *name = SDL_GetAudioDeviceName(ids[i]);
        if (!name || !name[0])
            continue;
        MimeAudioDevice *d = &out->devices[out->count++];
        d->id = ids[i];
        /* the cut may fall inside a character, which the guest's conversion replaces. */
        snprintf(d->name, sizeof(d->name), "%s", name);
        SDL_AudioSpec spec;
        if (SDL_GetAudioDeviceFormat(ids[i], &spec, nullptr)) {
            d->rate = (uint32_t)spec.freq;
            d->channels = (uint32_t)spec.channels;
            d->bits = SDL_AUDIO_BITSIZE(spec.format);
            d->is_float = SDL_AUDIO_ISFLOAT(spec.format);
        }
        if (!chosen && default_name && !strcmp(name, default_name)) {
            d->flags |= MIME_AUDIO_DEVICE_DEFAULT;
            chosen = true;
        }
    }
    if (!chosen && out->count)
        out->devices[0].flags |= MIME_AUDIO_DEVICE_DEFAULT;
    SDL_free(ids);
}

static bool listed(const MimeAudioDeviceList *list, uint32_t id)
{
    for (uint32_t i = 0; i < list->count; i++)
        if (list->devices[i].id == id)
            return true;
    return false;
}

/* SDL has no event for a change of the default device, so the lists are compared a few times a second. */
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
        if (changed) {
            memcpy(g_known, now, sizeof(now));
            /* SDL keeps the stream of a device that is gone running into nothing. */
            for (uint32_t i = 1; i < MAX_STREAMS; i++) {
                Stream *s = g_streams[i];
                if (s && s->device && !listed(&g_known[s->capture], s->device))
                    __atomic_store_n(&s->hdr->lost, 1, __ATOMIC_RELEASE);
            }
            if (g_vm)
                mime_vm_raise_irq(g_vm, MIME_IRQ_AUDIO);
        }
        pthread_mutex_unlock(&g_lock);
    }
    return nullptr;
}

static bool audio_ready(MimeVm *vm)
{
    pthread_mutex_lock(&g_lock);
    g_vm = vm;
    if (!g_audio)
        g_audio = SDL_InitSubSystem(SDL_INIT_AUDIO);
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

    bool ready = audio_ready(vm);
    if (h.device) {
        MimeAudioDeviceList now;
        if (ready)
            look(h.capture != 0, &now);
        if (!ready || !listed(&now, h.device))
            return MIME_HC_STATUS_NOT_FOUND;
    }

    Stream *s = calloc(1, sizeof(Stream));
    s->hdr = (MimeAudioHeader *)base;
    s->ring = base + MIME_AUDIO_HEADER_SIZE;
    s->frames = h.frames;
    s->frame_bytes = frame_bytes;
    s->packed24 = h.bits == 24;
    s->out_frame_bytes = s->packed24 ? h.channels * 4 : frame_bytes;
    s->volume = 65536;
    s->capture = h.capture != 0;
    s->device = h.device;
    SDL_AudioSpec spec = {
        .format = h.is_float ? SDL_AUDIO_F32 : h.bits == 16 ? SDL_AUDIO_S16 : SDL_AUDIO_S32,
        .channels = (int)h.channels,
        .freq = (int)h.rate,
    };
    SDL_AudioDeviceID followed = s->capture ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (ready)
        s->stream = SDL_OpenAudioDeviceStream(h.device ? h.device : followed, &spec, s->capture ? capture_render : render, s);
    if (!s->stream && h.device) {
        free(s);
        return MIME_HC_STATUS_IO_ERROR;
    }
    /* without a host device the guest keeps a working one whose sound goes nowhere. */
    if (!s->stream) {
        s->silent = true;
        if (!__atomic_exchange_n(&g_warned, true, __ATOMIC_RELAXED))
            mime_warn("audio: no host %s device (%s), the guest's sound is discarded", s->capture ? "input" : "output",
                      SDL_GetError());
    }
    pthread_mutex_lock(&g_lock);
    if (s->silent && !g_pacer) {
        pthread_t thread;
        g_pacer = pthread_create(&thread, nullptr, pacer, nullptr) == 0;
        if (g_pacer)
            pthread_detach(thread);
    }
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
        if (s->silent)
            s->paced_ns = now_ns();
        else
            ok = running ? SDL_ResumeAudioStreamDevice(s->stream) : SDL_PauseAudioStreamDevice(s->stream);
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
