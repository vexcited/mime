#ifndef MIME_LIB_AUDIO_DEVICES_H
#define MIME_LIB_AUDIO_DEVICES_H

#include <mime/win32/winbase.h>
#include <mime/audio.h>

#define MIME_AUDIO_ENDPOINTS MIME_AUDIO_MAX_DEVICES

typedef struct MimeAudioEndpoint {
    UINT32 host; /* the device of MimeAudioHeader. 0 for the silent one that stands for a host without any. */
    BOOL is_default;
    GUID guid; /* made from the host's name, so it stays the same across runs. */
    /* the format the host runs the device at. */
    UINT32 rate, channels, bits;
    BOOL is_float;
    WCHAR name[112]; /* as the host names it. */
} MimeAudioEndpoint;

/* fills up to MIME_AUDIO_ENDPOINTS entries, the default device first, and returns their count, which is at least 1. */
UINT32 MimeAudioEndpoints(BOOL capture, MimeAudioEndpoint *out);
/* the host device with this guid now, or FALSE when the host no longer has it. */
BOOL MimeAudioEndpointHost(BOOL capture, const GUID *guid, UINT32 *host);
/* the default device of the direction. */
void MimeAudioDefaultEndpoint(BOOL capture, MimeAudioEndpoint *out);
/* "{0.0.0.00000000}.{guid}", the id mmdevapi gives the endpoint. needs 56 characters. */
void MimeAudioEndpointId(BOOL capture, const GUID *guid, WCHAR *out);

#endif
