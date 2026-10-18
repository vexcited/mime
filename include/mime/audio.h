/*
 * audio output stream shared by a guest process and the host.
 *
 * a stream is one physically contiguous block of guest memory, mapped in the
 * process and seen by the host in place: a header, then a ring of frames in
 * the format the header describes. the guest writes frames and advances
 * written, the host's output callback consumes them and advances played, so
 * written - played is what is still queued. nothing crosses the hypercall
 * interface per buffer.
 *
 * an input stream (capture set) flips the ownership: the host's capture
 * callback writes frames and advances written, the guest reads them and
 * advances played, so written - played is what is waiting to be read, and
 * underruns counts the frames the host had to drop for want of room.
 */
#ifndef MIME_AUDIO_H
#define MIME_AUDIO_H

#include <stdint.h>

#define MIME_AUDIO_HEADER_SIZE 4096u
#define MIME_AUDIO_STREAM_SIZE (1u << 20) /* the header, then the ring */
#define MIME_AUDIO_MAX_STREAMS 8          /* per process */

typedef struct MimeAudioHeader {
    /* the format, read by the host when the stream opens. */
    uint32_t rate;     /* frames per second */
    uint32_t channels;
    uint32_t bits;     /* per sample: 16, 24 or 32 */
    uint32_t is_float; /* 32 bit ieee samples */
    uint32_t frames;   /* ring capacity */
    uint32_t volume;   /* 0 to 65536, applied by the host */
    uint32_t capture;  /* 1 for an input stream, 0 for an output stream */
    uint32_t reserved; /* keeps the counters at the same offsets for 32 and 64 bit processes */
    uint64_t written;  /* frames written, advanced by the guest only */
    uint64_t played;   /* frames consumed, advanced by the host only */
    uint64_t underruns; /* output: host callbacks that found less than they needed; input: frames dropped */
} MimeAudioHeader;

#endif
