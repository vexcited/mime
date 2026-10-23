/*
 * each guest stream uses an SDL stream on the default output, reading directly from the ring in mime/audio.h. SDL
 * converts format, rate and channels through PipeWire, PulseAudio or ALSA.
 */
#include "../audio/audio.h"

#include <mime/audio.h>

#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

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
} Stream;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Stream *g_streams[MAX_STREAMS];
static bool g_audio;

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

    pthread_mutex_lock(&g_lock);
    if (!g_audio)
        g_audio = SDL_InitSubSystem(SDL_INIT_AUDIO);
    pthread_mutex_unlock(&g_lock);
    if (!g_audio) {
        mime_warn("audio: no audio (%s)", SDL_GetError());
        return MIME_HC_STATUS_UNSUPPORTED;
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
    SDL_AudioSpec spec = {
        .format = h.is_float ? SDL_AUDIO_F32 : h.bits == 16 ? SDL_AUDIO_S16 : SDL_AUDIO_S32,
        .channels = (int)h.channels,
        .freq = (int)h.rate,
    };
    s->stream = s->capture ? SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, capture_render, s)
                           : SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, render, s);
    if (!s->stream) {
        mime_warn("audio: cannot open a %u Hz, %u channel, %u bit %s stream (%s)", h.rate, h.channels, h.bits,
                  s->capture ? "input" : "output", SDL_GetError());
        destroy(s);
        return MIME_HC_STATUS_UNSUPPORTED;
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
    for (uint64_t i = 0; i < MAX_STREAMS; i++)
        mime_audio_close(i);
}
