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

#define WDA_NONE 0x0
#define WDA_MONITOR 0x1
#define WDA_EXCLUDEFROMCAPTURE 0x11

/*
 * the affinity is kept on the window and honored by every capture of the
 * screen: windows 11 shows a window of WDA_MONITOR as black and leaves a
 * window of WDA_EXCLUDEFROMCAPTURE out. only a window of the calling process
 * may be changed.
 */
WINUSERAPI BOOL WINAPI SetWindowDisplayAffinity(HWND window, DWORD affinity)
{
    DWORD process = 0;
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (affinity != WDA_NONE && affinity != WDA_MONITOR && affinity != WDA_EXCLUDEFROMCAPTURE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    NtUserCallHwndParam(window, affinity, MIME_HWNDPARAM_SETAFFINITY);
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetWindowDisplayAffinity(HWND window, DWORD *affinity)
{
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (!affinity) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *affinity = (DWORD)NtUserCallHwndParam(window, 0, MIME_HWNDPARAM_GETAFFINITY);
    return TRUE;
}

/* there is no snap layout, so no window is ever arranged. */
WINUSERAPI BOOL WINAPI IsWindowArranged(HWND window)
{
    return FALSE;
}

#define PW_CLIENTONLY 0x1

/*
 * windows has the window render into a redirection surface; mime keeps
 * the pixels of every window in its top level surface, so the copy is
 * what the window last drew, the frame included unless asked otherwise.
 */
WINUSERAPI BOOL WINAPI PrintWindow(HWND window, HDC dc, UINT flags)
{
    if (!IsWindow(window) || !dc) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RECT r;
    HDC source;
    if (flags & PW_CLIENTONLY) {
        GetClientRect(window, &r);
        source = GetDC(window);
    } else {
        GetWindowRect(window, &r);
        OffsetRect(&r, -r.left, -r.top);
        source = GetWindowDC(window);
    }
    if (!source)
        return FALSE;
    BOOL ok = BitBlt(dc, 0, 0, r.right - r.left, r.bottom - r.top, source, 0, 0, SRCCOPY);
    ReleaseDC(window, source);
    return ok;
}

/* ------------------------------------------------------------------ pointers and power */

#define PT_MOUSE 4
#define MOUSE_POINTER_ID 1

/* the machine has a mouse and no touch screen or pen. */
WINUSERAPI BOOL WINAPI GetPointerType(UINT32 id, int *type)
{
    if (!type || id != MOUSE_POINTER_ID) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *type = PT_MOUSE;
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetPointerDevices(UINT32 *count, void *devices)
{
    if (!count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *count = 0;
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetPointerDevice(HANDLE device, void *info)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINUSERAPI BOOL WINAPI GetPointerPenInfo(UINT32 id, void *info)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINUSERAPI BOOL WINAPI GetPointerTouchInfo(UINT32 id, void *info)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINUSERAPI BOOL WINAPI RegisterPointerDeviceNotifications(HWND window, BOOL range)
{
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    return TRUE;
}

#ifndef DEVICE_NOTIFY_CALLBACK
#define DEVICE_NOTIFY_CALLBACK 0x2
#endif

/* the machine never sleeps, so the registration stands but nothing is ever sent. */
WINUSERAPI HANDLE WINAPI RegisterSuspendResumeNotification(HANDLE recipient, DWORD flags)
{
    if (!recipient || (flags != DEVICE_NOTIFY_WINDOW_HANDLE && flags != DEVICE_NOTIFY_CALLBACK)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    HANDLE *registration = HeapAlloc(GetProcessHeap(), 0, sizeof(HANDLE));
    if (registration)
        *registration = recipient;
    return registration;
}

WINUSERAPI BOOL WINAPI UnregisterSuspendResumeNotification(HANDLE registration)
{
    if (!registration) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, registration);
    return TRUE;
}
