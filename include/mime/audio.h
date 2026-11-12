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
    uint32_t device;   /* an id of MimeAudioDeviceList, or 0 to follow the host's default device as it changes. */
    uint64_t written;  /* frames written by the guest for output, host for capture. */
    uint64_t played;   /* frames consumed by the host for output, guest for capture. */
    uint64_t underruns; /* output: host callbacks that found less than they needed; input: frames dropped. */
    uint32_t lost;     /* set by the host when the device the stream named is gone. the ring keeps its pace. */
    uint32_t reserved;
} MimeAudioHeader;

/*
 * the host's devices of one direction, written over the start of the block of a stream that is not open. the host
 * raises MIME_IRQ_AUDIO when the list, a device or the default device changes. an id names its device until the host
 * loses it. a host without a device of the direction gives an empty list, and a stream that names none then fails to
 * open.
 */
#define MIME_AUDIO_MAX_DEVICES 31
#define MIME_AUDIO_DEVICE_DEFAULT 1u /* the one the host plays to or records from when none is named. */

/* what the device is, with the numbers of the EndpointFormFactor enumeration of windows. */
#define MIME_AUDIO_FORM_UNKNOWN 0u /* the host does not tell. */
#define MIME_AUDIO_FORM_SPEAKERS 1u
#define MIME_AUDIO_FORM_LINE 2u
#define MIME_AUDIO_FORM_HEADPHONES 3u
#define MIME_AUDIO_FORM_MICROPHONE 4u
#define MIME_AUDIO_FORM_HEADSET 5u
#define MIME_AUDIO_FORM_HANDSET 6u
#define MIME_AUDIO_FORM_SPDIF 8u
#define MIME_AUDIO_FORM_DISPLAY 9u

typedef struct MimeAudioDevice {
    uint32_t id;
    uint32_t flags;
    /* the format the device runs at. */
    uint32_t rate;
    uint32_t channels;
    uint32_t bits;         /* of a sample as it is stored. */
    uint32_t is_float;
    uint32_t valid_bits;   /* of those that carry sound, 0 when all of them do. */
    uint32_t channel_mask; /* the speakers of the channels in order, as the SPEAKER_* bits of windows. 0 when unknown. */
    uint32_t form;         /* MIME_AUDIO_FORM_*. */
    uint32_t reserved;
    /* UTF-8, each ends with a zero. */
    char name[104];
    char adapter[64]; /* the card the device belongs to, empty when the host does not tell. */
    char port[48];    /* the connector in use, such as "Speakers", empty when the host does not tell. */
} MimeAudioDevice;

typedef struct MimeAudioDeviceList {
    uint32_t count;
    uint32_t reserved;
    MimeAudioDevice devices[MIME_AUDIO_MAX_DEVICES];
} MimeAudioDeviceList;

/*
 * the guid windows programs know a device by, made from the host's name so it stays the same across runs. twin
 * counts the devices of the same name before it in the list.
 */
static inline uint64_t mime_audio_name_hash(const char *text, uint64_t seed)
{
    uint64_t h = seed;
    for (; *text; text++)
        h = (h ^ (uint8_t)*text) * 0x100000001B3ull;
    return h;
}

static inline void mime_audio_device_guid(uint8_t guid[16], const char *name, uint32_t capture, uint32_t twin)
{
    uint64_t low = mime_audio_name_hash(name, 0xCBF29CE484222325ull + twin);
    uint64_t high = mime_audio_name_hash(name, 0x84222325CBF29CE4ull + twin * 31 + (capture ? 1 : 0));
    for (int i = 0; i < 8; i++) {
        guid[i] = (uint8_t)(low >> (i * 8));
        guid[8 + i] = (uint8_t)(high >> (i * 8));
    }
    /* a version 4 guid. the third field is stored low byte first. */
    guid[7] = (guid[7] & 0x0F) | 0x40;
    guid[8] = (guid[8] & 0x3F) | 0x80;
}

#endif
