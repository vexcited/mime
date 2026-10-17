/* the texts of winmm's errors, those of windows' winmmbase. */
#define MIME_BUILD_WINMM
#include "winmm_private.h"

static const WCHAR *const g_system[] = {
    L"The specified command was carried out.",
    L"Undefined external error.",
    L"A device ID has been used that is out of range for your system.",
    L"The driver was not enabled.",
    L"The specified device is already in use.  Wait until it is free, and then try again.",
    L"The specified device handle is invalid.",
    L"There is no driver installed on your system.",
    L"There is not enough memory available for this task.  Quit one or more applications to increase available memory, "
    L"and then try again.",
    L"This function is not supported.  Use the Capabilities function to determine which functions and messages the "
    L"driver supports.",
    L"An error number was specified that is not defined in the system.",
    L"An invalid flag was passed to a system function.",
    L"An invalid parameter was passed to a system function.",
    L"Handle being used simultaneously on another thread (eg callback).",
    L"Specified alias not found in WIN.INI.",
    L"The registry database is corrupt.",
    L"The specified registry key was not found.",
    L"The registry could not be opened or could not be read.",
    L"The registry could not be written to.",
    L"The specified registry key could not be deleted.",
    L"The specified registry key value could not be found.",
    L"The driver did not generate a valid OPEN callback.",
};

static const WCHAR *const g_wave[] = {
    L"The specified format is not supported or cannot be translated.  Use the Capabilities function to determine the "
    L"supported formats.",
    L"Cannot perform this operation while media data is still playing.  Reset the device, or wait until the data is "
    L"finished playing.",
    L"The wave header was not prepared.  Use the Prepare function to prepare the header, and then try again.",
    L"Cannot open the device without using the WAVE_ALLOWSYNC flag.  Use the flag, and then try again.",
};

static const WCHAR *const g_midi[] = {
    L"The MIDI header was not prepared.  Use the Prepare function to prepare the header, and then try again.",
    L"Cannot perform this operation while media data is still playing.  Reset the device, or wait until the data is "
    L"finished playing.",
    L"A MIDI map was not found.  There may be a problem with the driver, or the MIDIMAP.CFG file may be corrupt or "
    L"missing.",
    L"The port is transmitting data to the device.  Wait until the data has been transmitted, and then try again.",
    L"The current MIDI Mapper setup refers to a MIDI device that is not installed on the system.  Use MIDI Mapper to "
    L"edit the setup.",
    L"The current MIDI setup is damaged.  Copy the original MIDIMAP.CFG file to the Windows SYSTEM directory, and then "
    L"try again.",
    L"A MIDI call was made which is invalid with the current open mode. Reopen the device with the correct mode.",
    L"Driver condition - do not callback this input event",
};

static const WCHAR *error_text(MMRESULT error)
{
    if (error < RTL_NUMBER_OF(g_system))
        return g_system[error];
    if (error >= 32 && error < 32 + RTL_NUMBER_OF(g_wave))
        return g_wave[error - 32];
    if (error >= 64 && error < 64 + RTL_NUMBER_OF(g_midi))
        return g_midi[error - 64];
    return nullptr;
}

MMRESULT MmErrorTextW(MMRESULT error, LPWSTR text, UINT size)
{
    if (!text || !size)
        return MMSYSERR_INVALPARAM;
    const WCHAR *s = error_text(error);
    if (!s) {
        text[0] = 0;
        return MMSYSERR_BADERRNUM;
    }
    lstrcpynW(text, s, (int)size);
    return MMSYSERR_NOERROR;
}

MMRESULT MmErrorTextA(MMRESULT error, LPSTR text, UINT size)
{
    if (!text || !size)
        return MMSYSERR_INVALPARAM;
    const WCHAR *s = error_text(error);
    if (!s) {
        text[0] = 0;
        return MMSYSERR_BADERRNUM;
    }
    WideCharToMultiByte(CP_ACP, 0, s, -1, text, (int)size, nullptr, nullptr);
    text[size - 1] = 0;
    return MMSYSERR_NOERROR;
}
