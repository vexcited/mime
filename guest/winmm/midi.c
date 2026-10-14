/* no midi devices are exposed by the multimedia backend. */
#define MIME_BUILD_WINMM
#include <mime/win32/winbase.h>
#include <mime/win32/mmsystem.h>

WINMMAPI MMRESULT WINAPI midiInOpen(HANDLE *out, UINT id, DWORD_PTR callback, DWORD_PTR instance, DWORD flags)
{
    if (!out)
        return MMSYSERR_INVALPARAM;
    *out = NULL;
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
    static const char *messages[] = {
        "The specified command was carried out.", "Unspecified error.",
        "A device ID has been used that is out of range for your system.",
        "The driver was not enabled.", "The device is already in use.",
        "The specified device handle is invalid.", "There is no driver installed on your system.",
        "Not enough memory available for this task.", "This function is not supported.",
        "An error number was specified that is not defined in the system.",
        "An invalid flag was passed to a system function.", "An invalid parameter was passed to a system function."
    };
    if (!text)
        return MMSYSERR_INVALPARAM;
    if (error >= RTL_NUMBER_OF(messages))
        return MMSYSERR_BADERRNUM;
    if (size)
        lstrcpynA(text, messages[error], (int)size);
    return MMSYSERR_NOERROR;
}
