/*
 * audio output streams. each guest stream is a default output unit whose
 * render callback reads frames straight from the guest's ring (mime/audio.h).
 * core audio converts the guest's format, rate and channels to the device.
 */
#include "audio.h"

#include <mime/audio.h>

#include <AudioToolbox/AudioToolbox.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STREAMS 64

typedef struct Stream {
    AudioComponentInstance unit;
    MimeAudioHeader *hdr;
    uint8_t *ring;
    uint32_t frames, frame_bytes;
    uint32_t volume;
    bool running;
} Stream;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Stream *g_streams[MAX_STREAMS];

static OSStatus render(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32 bus,
                       UInt32 count, AudioBufferList *io)
{
    Stream *s = ref;
    MimeAudioHeader *h = s->hdr;
    uint8_t *out = io->mBuffers[0].mData;
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
    uint32_t volume = __atomic_load_n(&h->volume, __ATOMIC_RELAXED);
    if (volume != s->volume) {
        s->volume = volume;
        AudioUnitSetParameter(s->unit, kHALOutputParam_Volume, kAudioUnitScope_Global, 0,
                              volume > 65536 ? 1.0f : volume / 65536.0f, 0);
    }
    return noErr;
}

static void destroy(Stream *s)
{
    if (s->unit) {
        AudioOutputUnitStop(s->unit);
        AudioUnitUninitialize(s->unit);
        AudioComponentInstanceDispose(s->unit);
    }
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

    Stream *s = calloc(1, sizeof(Stream));
    s->hdr = (MimeAudioHeader *)base;
    s->ring = base + MIME_AUDIO_HEADER_SIZE;
    s->frames = h.frames;
    s->frame_bytes = frame_bytes;
    s->volume = 65536;
    AudioComponentDescription desc = {kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
                                      kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    AudioStreamBasicDescription format = {
        .mSampleRate = h.rate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsPacked | (h.is_float ? kAudioFormatFlagIsFloat : kAudioFormatFlagIsSignedInteger),
        .mBytesPerPacket = frame_bytes,
        .mFramesPerPacket = 1,
        .mBytesPerFrame = frame_bytes,
        .mChannelsPerFrame = h.channels,
        .mBitsPerChannel = h.bits,
    };
    AURenderCallbackStruct callback = {render, s};
    OSStatus err = component ? AudioComponentInstanceNew(component, &s->unit) : -1;
    if (!err)
        err = AudioUnitSetProperty(s->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format,
                                   sizeof(format));
    if (!err)
        err = AudioUnitSetProperty(s->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback,
                                   sizeof(callback));
    if (!err)
        err = AudioUnitInitialize(s->unit);
    if (err) {
        mime_warn("audio: cannot open a %u Hz, %u channel, %u bit stream (%d)", h.rate, h.channels, h.bits, (int)err);
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
    OSStatus err = 0;
    if (s && s->running != (running != 0)) {
        err = running ? AudioOutputUnitStart(s->unit) : AudioOutputUnitStop(s->unit);
        if (!err)
            s->running = running != 0;
    }
    pthread_mutex_unlock(&g_lock);
    if (!s)
        return MIME_HC_STATUS_INVALID;
    return err ? MIME_HC_STATUS_IO_ERROR : MIME_HC_STATUS_OK;
}

void mime_audio_close(uint64_t stream)
{
    pthread_mutex_lock(&g_lock);
    Stream *s = stream < MAX_STREAMS ? g_streams[stream] : nullptr;
    if (s)
        g_streams[stream] = nullptr;
    pthread_mutex_unlock(&g_lock);
    /* disposing waits for a callback in progress, after which the guest may reuse the memory. */
    if (s)
        destroy(s);
}
