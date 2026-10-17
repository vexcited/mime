/* no midi devices are exposed by the multimedia backend. */
#define MIME_BUILD_WINMM
#include "winmm_private.h"

WINMMAPI MMRESULT WINAPI midiInOpen(HANDLE *out, UINT id, DWORD_PTR callback, DWORD_PTR instance, DWORD flags)
{
    if (!out)
        return MMSYSERR_INVALPARAM;
    *out = nullptr;
    return MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI midiInClose(HANDLE device)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI midiInStart(HANDLE device)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI midiInStop(HANDLE device)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI midiInGetDevCapsA(UINT_PTR device, void *caps, UINT size)
{
    if (!size)
        return MMSYSERR_NOERROR;
    if (!caps)
        return MMSYSERR_INVALPARAM;
    return MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI midiInGetErrorTextA(MMRESULT error, LPSTR text, UINT size)
{
    return MmErrorTextA(error, text, size);
}

WINMMAPI UINT WINAPI midiOutGetNumDevs(void)
{
    return 0;
}

WINMMAPI UINT WINAPI midiInGetNumDevs(void)
{
    return 0;
}
