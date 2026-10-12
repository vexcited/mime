/*
 * common controls initialization and version.
 *
 * mime registers no common control window classes yet; initialization
 * validates its argument and succeeds so programs keep going.
 */
#include "comctl32_private.h"

#define MAKEDLLVERULL(major, minor, build, qfe)                                                                   \
    (((ULONGLONG)(major) << 48) | ((ULONGLONG)(minor) << 32) | ((ULONGLONG)(build) << 16) | (ULONGLONG)(qfe))

WINCOMMCTRLAPI void WINAPI InitCommonControls(void)
{
}

WINCOMMCTRLAPI BOOL WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX *init)
{
    if (!init || init->dwSize != sizeof(*init))
        return FALSE;
    return TRUE;
}

/* the version of the windows 11 comctl32 6 side by side assembly; the build is mime's. */
WINCOMMCTRLAPI HRESULT WINAPI DllGetVersion(DLLVERSIONINFO *info)
{
    if (!info || (info->cbSize != sizeof(DLLVERSIONINFO) && info->cbSize != sizeof(DLLVERSIONINFO2)))
        return E_INVALIDARG;
    info->dwMajorVersion = 6;
    info->dwMinorVersion = 10;
    info->dwBuildNumber = 26300;
    info->dwPlatformID = DLLVER_PLATFORM_NT;
    if (info->cbSize == sizeof(DLLVERSIONINFO2)) {
        DLLVERSIONINFO2 *info2 = (DLLVERSIONINFO2 *)info;
        info2->dwFlags = 0;
        info2->ullVersion = MAKEDLLVERULL(6, 10, 26300, 0);
    }
    return S_OK;
}

WINCOMMCTRLAPI BOOL WINAPI _TrackMouseEvent(LPTRACKMOUSEEVENT track)
{
    return TrackMouseEvent(track);
}
