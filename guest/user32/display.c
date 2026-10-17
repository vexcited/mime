/* display settings for the single virtual monitor. */
#include "user32_private.h"
#include <mime/win32/winspool.h>

/* the display modes live in win32k, which also changes them; see guest/win32k/display.c. */
WINUSERAPI BOOL WINAPI EnumDisplaySettingsExW(LPCWSTR device, DWORD index, DEVMODEW *mode, DWORD flags)
{
    if (!mode) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UNICODE_STRING name;
    if (device)
        RtlInitUnicodeString(&name, device);
    return NT_SUCCESS(NtUserEnumDisplaySettings(device ? &name : nullptr, index, mode, flags));
}

WINUSERAPI BOOL WINAPI EnumDisplaySettingsW(LPCWSTR device, DWORD index, DEVMODEW *mode)
{
    return EnumDisplaySettingsExW(device, index, mode, 0);
}

/* the ansi DEVMODE: the same fields, the two names in bytes. */
static void mode_to_ansi(const DEVMODEW *w, DEVMODEA *a, WORD size)
{
    DEVMODEA m;
    memset(&m, 0, sizeof(m));
    WideCharToMultiByte(CP_ACP, 0, w->dmDeviceName, -1, (LPSTR)m.dmDeviceName, sizeof(m.dmDeviceName), nullptr,
                        nullptr);
    memcpy(&m.dmSpecVersion, &w->dmSpecVersion, offsetof(DEVMODEW, dmFormName) - offsetof(DEVMODEW, dmSpecVersion));
    WideCharToMultiByte(CP_ACP, 0, w->dmFormName, -1, (LPSTR)m.dmFormName, sizeof(m.dmFormName), nullptr, nullptr);
    memcpy(&m.dmLogPixels, &w->dmLogPixels, sizeof(DEVMODEW) - offsetof(DEVMODEW, dmLogPixels));
    m.dmSize = size < sizeof(m) ? size : sizeof(m);
    m.dmDriverExtra = 0;
    memcpy(a, &m, m.dmSize);
}

static void mode_from_ansi(const DEVMODEA *a, DEVMODEW *w)
{
    DEVMODEA m;
    memset(&m, 0, sizeof(m));
    memcpy(&m, a, a->dmSize < sizeof(m) ? a->dmSize : sizeof(m));
    memset(w, 0, sizeof(*w));
    MultiByteToWideChar(CP_ACP, 0, (LPCSTR)m.dmDeviceName, sizeof(m.dmDeviceName), w->dmDeviceName,
                        RTL_NUMBER_OF(w->dmDeviceName));
    w->dmDeviceName[RTL_NUMBER_OF(w->dmDeviceName) - 1] = 0;
    memcpy(&w->dmSpecVersion, &m.dmSpecVersion, offsetof(DEVMODEA, dmFormName) - offsetof(DEVMODEA, dmSpecVersion));
    MultiByteToWideChar(CP_ACP, 0, (LPCSTR)m.dmFormName, sizeof(m.dmFormName), w->dmFormName,
                        RTL_NUMBER_OF(w->dmFormName));
    w->dmFormName[RTL_NUMBER_OF(w->dmFormName) - 1] = 0;
    memcpy(&w->dmLogPixels, &m.dmLogPixels, sizeof(DEVMODEA) - offsetof(DEVMODEA, dmLogPixels));
    /* an ansi mode's size counts byte names; the wide one's counts wide names. */
    w->dmSize = a->dmSize >= sizeof(DEVMODEA) ? sizeof(DEVMODEW) : a->dmSize + sizeof(DEVMODEW) - sizeof(DEVMODEA);
    w->dmDriverExtra = 0;
}

WINUSERAPI BOOL WINAPI EnumDisplaySettingsExA(LPCSTR device, DWORD index, DEVMODEA *mode, DWORD flags)
{
    if (!mode) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LPWSTR name = UserAtoW(device);
    DEVMODEW w = {.dmSize = sizeof(w)};
    BOOL ok = EnumDisplaySettingsExW(name, index, &w, flags);
    UserFreeConverted(name, device);
    if (ok)
        mode_to_ansi(&w, mode, mode->dmSize);
    return ok;
}

WINUSERAPI BOOL WINAPI EnumDisplaySettingsA(LPCSTR device, DWORD index, DEVMODEA *mode)
{
    return EnumDisplaySettingsExA(device, index, mode, 0);
}

WINUSERAPI LONG WINAPI ChangeDisplaySettingsExW(LPCWSTR device, DEVMODEW *mode, HWND window, DWORD flags,
                                                LPVOID param)
{
    UNICODE_STRING name;
    if (device)
        RtlInitUnicodeString(&name, device);
    return NtUserChangeDisplaySettings(device ? &name : nullptr, mode, flags, param);
}

WINUSERAPI LONG WINAPI ChangeDisplaySettingsW(DEVMODEW *mode, DWORD flags)
{
    return ChangeDisplaySettingsExW(nullptr, mode, nullptr, flags, nullptr);
}

WINUSERAPI LONG WINAPI ChangeDisplaySettingsExA(LPCSTR device, DEVMODEA *mode, HWND window, DWORD flags,
                                                LPVOID param)
{
    DEVMODEW w;
    if (mode)
        mode_from_ansi(mode, &w);
    LPWSTR name = UserAtoW(device);
    LONG result = ChangeDisplaySettingsExW(name, mode ? &w : nullptr, window, flags, param);
    UserFreeConverted(name, device);
    return result;
}

WINUSERAPI LONG WINAPI ChangeDisplaySettingsA(DEVMODEA *mode, DWORD flags)
{
    return ChangeDisplaySettingsExA(nullptr, mode, nullptr, flags, nullptr);
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
