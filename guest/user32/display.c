/* display settings for the single virtual monitor. */
#include "user32_private.h"
#include <mime/win32/winspool.h>

WINUSERAPI BOOL WINAPI EnumDisplaySettingsW(LPCWSTR device, DWORD index, DEVMODEW *mode)
{
    if (!mode || mode->dmSize < sizeof(*mode) ||
        (index != 0 && index != (DWORD)-1 && index != (DWORD)-2)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DISPLAY_DEVICEW display = {};
    display.cb = sizeof(display);
    if (!EnumDisplayDevicesW(nullptr, 0, &display, 0) ||
        (device && *device && _wcsicmp(device, display.DeviceName)))
        return FALSE;
    memset(mode, 0, sizeof(*mode));
    mode->dmSize = sizeof(*mode);
    mode->dmSpecVersion = 0x401;
    lstrcpynW(mode->dmDeviceName, display.DeviceName, CCHDEVICENAME);
    mode->dmFields = 0x005c0020;
    mode->dmBitsPerPel = 32;
    mode->dmPelsWidth = GetSystemMetrics(SM_CXSCREEN);
    mode->dmPelsHeight = GetSystemMetrics(SM_CYSCREEN);
    mode->dmDisplayFrequency = 60;
    return TRUE;
}

WINUSERAPI LONG WINAPI GetDisplayConfigBufferSizes(UINT flags, UINT *paths, UINT *modes)
{
    if (!paths || !modes)
        return ERROR_INVALID_PARAMETER;
    *paths = *modes = 0;
    return ERROR_NOT_SUPPORTED;
}

WINUSERAPI LONG WINAPI QueryDisplayConfig(UINT flags, UINT *paths, void *path_data, UINT *modes,
                                         void *mode_data, UINT *topology)
{
    if (!paths || !modes)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_SUPPORTED;
}

WINUSERAPI LONG WINAPI DisplayConfigGetDeviceInfo(void *info)
{
    return info ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

WINUSERAPI BOOL WINAPI SetWindowDisplayAffinity(HWND window, DWORD affinity)
{
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (!affinity)
        return TRUE;
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
