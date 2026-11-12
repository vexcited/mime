#ifndef MIME_LIB_AUDIO_DEVICES_H
#define MIME_LIB_AUDIO_DEVICES_H

#include <mime/win32/winbase.h>
#include <mime/audio.h>

#define MIME_AUDIO_ENDPOINTS MIME_AUDIO_MAX_DEVICES

typedef struct MimeAudioEndpoint {
    UINT32 host; /* the device of MimeAudioHeader. */
    BOOL is_default;
    GUID guid; /* made from the host's name, so it stays the same across runs. */
    /* the format the host runs the device at. bits are those of a stored sample, valid_bits those that carry sound. */
    UINT32 rate, channels, bits, valid_bits;
    BOOL is_float;
    DWORD channel_mask; /* the speakers of the channels. */
    UINT32 form;        /* an EndpointFormFactor. */
    WCHAR name[112];    /* as the host names it. */
    WCHAR adapter[64];  /* the card it belongs to, or the name again when the host does not tell. */
    WCHAR port[48];     /* the connector in use, such as "Speakers", or the name again. */
} MimeAudioEndpoint;

/* the speakers windows gives a count of channels by default. */
DWORD MimeAudioChannelMask(UINT32 channels);
/*
 * fills up to MIME_AUDIO_ENDPOINTS entries, the default device first, and returns their count. a host without a
 * device of the direction gives none, as windows does without a sound card.
 */
UINT32 MimeAudioEndpoints(BOOL capture, MimeAudioEndpoint *out);
/* the host device with this guid now, or FALSE when the host no longer has it. */
BOOL MimeAudioEndpointHost(BOOL capture, const GUID *guid, UINT32 *host);
/* the default device of the direction, or FALSE when there is none. */
BOOL MimeAudioDefaultEndpoint(BOOL capture, MimeAudioEndpoint *out);
/* "{0.0.0.00000000}.{guid}", the id mmdevapi gives the endpoint. needs 56 characters. */
void MimeAudioEndpointId(BOOL capture, const GUID *guid, WCHAR *out);

#endif
