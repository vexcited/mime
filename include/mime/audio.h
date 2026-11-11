/*
 * shared audio ring in one physically contiguous guest block: a header followed by frames in the declared
 * format. output has the guest advance written and the host callback advance played; written - played counts
 * queued frames. no per-buffer hypercall is needed.
 *
 * with capture set, the host writes and the guest consumes. written - played counts readable frames, and
 * underruns counts frames dropped for lack of room.
 */
#ifndef MIME_AUDIO_H
#define MIME_AUDIO_H

#include <stdint.h>

#define MIME_AUDIO_HEADER_SIZE 4096u
#define MIME_AUDIO_STREAM_SIZE (1u << 20) /* the header, then the ring. */
#define MIME_AUDIO_MAX_STREAMS 8          /* per process. */

typedef struct MimeAudioHeader {
    /* the format, read by the host when the stream opens. */
    uint32_t rate;     /* frames per second. */
    uint32_t channels;
    uint32_t bits;     /* per sample: 16, 24 or 32. */
    uint32_t is_float; /* 32 bit ieee samples. */
    uint32_t frames;   /* ring capacity. */
    uint32_t volume;   /* 0 to 65536, applied by the host. */
    uint32_t capture;  /* 1 for an input stream, 0 for an output stream. */
    uint32_t device;   /* a host device from MimeAudioDeviceList, 0 for the default one of the direction. */
    uint64_t written;  /* frames written by the guest for output, host for capture. */
    uint64_t played;   /* frames consumed by the host for output, guest for capture. */
    uint64_t underruns; /* output: host callbacks that found less than they needed; input: frames dropped. */
} MimeAudioHeader;

/*
 * the host's devices of one direction besides the default one, written over the header of a stream that is not open.
 * an id names its device until the host loses it, after which a stream asking for it gets the default one.
 */
#define MIME_AUDIO_MAX_DEVICES 31

typedef struct MimeAudioDevice {
    uint32_t id;
    char name[124]; /* UTF-8, ends with a zero. */
} MimeAudioDevice;

typedef struct MimeAudioDeviceList {
    uint32_t count;
    uint32_t reserved;
    MimeAudioDevice devices[MIME_AUDIO_MAX_DEVICES];
} MimeAudioDeviceList;

#endif
