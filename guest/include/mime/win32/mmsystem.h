/*
 * multimedia timers and sounds, the subset of winmm.dll mime provides.
 */
#ifndef MIME_WIN32_MMSYSTEM_H
#define MIME_WIN32_MMSYSTEM_H

#include <mime/win32/winbase.h>

#ifdef MIME_BUILD_WINMM
#define WINMMAPI __declspec(dllexport)
#else
#define WINMMAPI DECLSPEC_IMPORT
#endif

typedef UINT MMRESULT;
typedef UINT MMVERSION;

#define MMSYSERR_BASE 0
#define MMSYSERR_NOERROR 0
#define MMSYSERR_ERROR 1
#define MMSYSERR_BADDEVICEID 2
#define MMSYSERR_NOTENABLED 3
#define MMSYSERR_ALLOCATED 4
#define MMSYSERR_INVALHANDLE 5
#define MMSYSERR_NODRIVER 6
#define MMSYSERR_NOMEM 7
#define MMSYSERR_NOTSUPPORTED 8
#define MMSYSERR_BADERRNUM 9
#define MMSYSERR_INVALFLAG 10
#define MMSYSERR_INVALPARAM 11

#define TIMERR_BASE 96
#define TIMERR_NOERROR 0
#define TIMERR_NOCANDO (TIMERR_BASE + 1)
#define TIMERR_STRUCT (TIMERR_BASE + 33)

/* timeSetEvent flags. */
#define TIME_ONESHOT 0x0000
#define TIME_PERIODIC 0x0001
#define TIME_CALLBACK_FUNCTION 0x0000
#define TIME_CALLBACK_EVENT_SET 0x0010
#define TIME_CALLBACK_EVENT_PULSE 0x0020
#define TIME_KILL_SYNCHRONOUS 0x0100

typedef void(CALLBACK *LPTIMECALLBACK)(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR dw1, DWORD_PTR dw2);

typedef struct timecaps_tag {
    UINT wPeriodMin;
    UINT wPeriodMax;
} TIMECAPS, *PTIMECAPS, *LPTIMECAPS;

/* MMTIME formats. */
#define TIME_MS 0x0001
#define TIME_SAMPLES 0x0002
#define TIME_BYTES 0x0004
#define TIME_SMPTE 0x0008
#define TIME_MIDI 0x0010
#define TIME_TICKS 0x0020

typedef struct mmtime_tag {
    UINT wType;
    union {
        DWORD ms;
        DWORD sample;
        DWORD cb;
        DWORD ticks;
        struct {
            BYTE hour, min, sec, frame, fps, dummy, pad[2];
        } smpte;
        struct {
            DWORD songptrpos;
        } midi;
    } u;
} MMTIME, *PMMTIME, *LPMMTIME;

/* PlaySound flags. */
#define SND_SYNC 0x0000
#define SND_ASYNC 0x0001
#define SND_NODEFAULT 0x0002
#define SND_MEMORY 0x0004
#define SND_LOOP 0x0008
#define SND_NOSTOP 0x0010
#define SND_NOWAIT 0x00002000L
#define SND_ALIAS 0x00010000L
#define SND_ALIAS_ID 0x00110000L
#define SND_FILENAME 0x00020000L
#define SND_RESOURCE 0x00040004L
#define SND_PURGE 0x0040
#define SND_APPLICATION 0x0080
#define SND_SENTRY 0x00080000L
#define SND_RING 0x00100000L
#define SND_SYSTEM 0x00200000L

WINMMAPI DWORD WINAPI timeGetTime(void);
WINMMAPI MMRESULT WINAPI timeGetSystemTime(LPMMTIME time, UINT size);
WINMMAPI MMRESULT WINAPI timeGetDevCaps(LPTIMECAPS caps, UINT size);
WINMMAPI MMRESULT WINAPI timeBeginPeriod(UINT period);
WINMMAPI MMRESULT WINAPI timeEndPeriod(UINT period);
WINMMAPI MMRESULT WINAPI timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user,
                                      UINT flags);
WINMMAPI MMRESULT WINAPI timeKillEvent(UINT id);
WINMMAPI BOOL WINAPI PlaySoundW(LPCWSTR sound, HMODULE module, DWORD flags);
WINMMAPI BOOL WINAPI PlaySoundA(LPCSTR sound, HMODULE module, DWORD flags);
WINMMAPI BOOL WINAPI sndPlaySoundW(LPCWSTR sound, UINT flags);
WINMMAPI BOOL WINAPI sndPlaySoundA(LPCSTR sound, UINT flags);
WINMMAPI UINT WINAPI waveOutGetNumDevs(void);
WINMMAPI UINT WINAPI waveInGetNumDevs(void);
WINMMAPI UINT WINAPI midiOutGetNumDevs(void);
WINMMAPI UINT WINAPI midiInGetNumDevs(void);
WINMMAPI UINT WINAPI mixerGetNumDevs(void);
WINMMAPI UINT WINAPI auxGetNumDevs(void);

#endif
