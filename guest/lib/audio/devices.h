#ifndef MIME_LIB_AUDIO_DEVICES_H
#define MIME_LIB_AUDIO_DEVICES_H

#include <mime/win32/winbase.h>
#include <mime/audio.h>

#if defined(__aarch64__)
#define MIME_AUDIO_BRAND L"Qualcomm Audio"
#else
#define MIME_AUDIO_BRAND L"Realtek(R) Audio"
#endif

#define MIME_AUDIO_ENDPOINTS (MIME_AUDIO_MAX_DEVICES + 1)

typedef struct MimeAudioEndpoint {
    UINT32 host;          /* the device of MimeAudioHeader, 0 for the default one. */
    GUID guid;            /* made from the host's name, so it stays the same across runs. zero for the default one. */
    WCHAR name[144];      /* "Speakers (Realtek(R) Audio)" */
    WCHAR interface[128]; /* "Realtek(R) Audio" */
} MimeAudioEndpoint;

/* fills up to MIME_AUDIO_ENDPOINTS entries, the default device first, and returns their count, which is at least 1. */
UINT32 MimeAudioEndpoints(BOOL capture, MimeAudioEndpoint *out);
/* the host device with this guid now, or FALSE when the host no longer has it. */
BOOL MimeAudioEndpointHost(BOOL capture, const GUID *guid, UINT32 *host);

#endif
