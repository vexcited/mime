/* display settings for the single virtual monitor. */
#include "user32_private.h"
#include <mime/win32/winspool.h>

/* win32k owns and changes the display modes, see guest/win32k/display.c. */
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

/* DEVMODEA has the same fields, with the two names as byte strings. */
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
    /* dmSize counts byte names for ansi and wide names for wide. */
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

/* the display configuration cannot be read, so nothing in it can be changed. */
WINUSERAPI LONG WINAPI DisplayConfigSetDeviceInfo(void *info)
{
    return info ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

#define WDA_NONE 0x0
#define WDA_MONITOR 0x1
#define WDA_EXCLUDEFROMCAPTURE 0x11

/*
 * every screen capture honors the affinity: like windows 11, WDA_MONITOR shows as black and WDA_EXCLUDEFROMCAPTURE
 * is left out. only windows of the calling process may be changed.
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
#define PW_RENDERFULLCONTENT 0x2

/*
 * win32k captures the window's own pixels, whatever covers it: its redirection surface and, for
 * PW_RENDERFULLCONTENT, the image its process presented through the host, which covers windows filled by a Direct3D
 * or DirectComposition swap chain. the window is painted first, as UpdateWindow does. a missing device context or an
 * empty capture is not a failure. the desktop cannot be printed and fails without an error.
 */
WINUSERAPI BOOL WINAPI PrintWindow(HWND window, HDC dc, UINT flags)
{
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (window == GetDesktopWindow())
        return FALSE;
    if (flags & ~(PW_CLIENTONLY | PW_RENDERFULLCONTENT)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RedrawWindow(window, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
    MIME_CAPTURE_WINDOW info = {.Flags = flags};
    if (NtUserMimeCapture(MIME_CAPOP_WINDOW, (ULONG)(ULONG_PTR)window, (PUCHAR)&info, nullptr, 0) < 0)
        return FALSE;
    if (!dc || !info.Width || !info.Height)
        return TRUE;
    SIZE_T size = (SIZE_T)info.Width * info.Height * 4;
    void *bits = HeapAlloc(GetProcessHeap(), 0, size);
    if (!bits)
        return FALSE;
    BOOL ok = NtUserMimeCapture(MIME_CAPOP_WINDOW, (ULONG)(ULONG_PTR)window, (PUCHAR)&info, bits, (ULONG)size) >= 0;
    if (ok) {
        BITMAPINFO bmi = {.bmiHeader = {sizeof(BITMAPINFOHEADER), (LONG)info.Width, -(LONG)info.Height, 1, 32, BI_RGB}};
        SetDIBitsToDevice(dc, 0, 0, info.Width, info.Height, 0, 0, 0, info.Height, bits, &bmi, DIB_RGB_COLORS);
    }
    HeapFree(GetProcessHeap(), 0, bits);
    return ok;
}

/*
 * DefWindowProc's WM_PRINT: the device context holds the window from its top left corner. the frame comes from what the
 * window shows, the client area from WM_ERASEBKGND and WM_PRINTCLIENT at its origin, then the children from the bottom
 * of the z order up.
 */
void UserDefPrint(HWND hwnd, HDC dc, DWORD flags)
{
    if (!dc || ((flags & PRF_CHECKVISIBLE) && !IsWindowVisible(hwnd)))
        return;
    RECT window, client;
    GetWindowRect(hwnd, &window);
    GetClientRect(hwnd, &client);
    POINT origin = {0, 0};
    ClientToScreen(hwnd, &origin);
    int cx = origin.x - window.left, cy = origin.y - window.top;
    if ((flags & PRF_NONCLIENT) && (cx || cy || client.right != window.right - window.left ||
                                     client.bottom != window.bottom - window.top)) {
        MIME_CAPTURE_WINDOW info = {0};
        if (NtUserMimeCapture(MIME_CAPOP_WINDOW, (ULONG)(ULONG_PTR)hwnd, (PUCHAR)&info, nullptr, 0) >= 0 &&
            info.Width && info.Height) {
            SIZE_T size = (SIZE_T)info.Width * info.Height * 4;
            void *bits = HeapAlloc(GetProcessHeap(), 0, size);
            if (bits && NtUserMimeCapture(MIME_CAPOP_WINDOW, (ULONG)(ULONG_PTR)hwnd, (PUCHAR)&info, bits, (ULONG)size) >= 0) {
                int saved = SaveDC(dc);
                ExcludeClipRect(dc, cx, cy, cx + client.right, cy + client.bottom);
                BITMAPINFO bmi = {.bmiHeader = {sizeof(BITMAPINFOHEADER), (LONG)info.Width, -(LONG)info.Height, 1, 32,
                                                BI_RGB}};
                SetDIBitsToDevice(dc, 0, 0, info.Width, info.Height, 0, 0, 0, info.Height, bits, &bmi, DIB_RGB_COLORS);
                RestoreDC(dc, saved);
            }
            HeapFree(GetProcessHeap(), 0, bits);
        }
    }
    if (flags & (PRF_ERASEBKGND | PRF_CLIENT)) {
        int saved = SaveDC(dc);
        OffsetViewportOrgEx(dc, cx, cy, nullptr);
        IntersectClipRect(dc, 0, 0, client.right, client.bottom);
        if (flags & PRF_ERASEBKGND)
            SendMessageW(hwnd, WM_ERASEBKGND, (WPARAM)dc, 0);
        if (flags & PRF_CLIENT)
            SendMessageW(hwnd, WM_PRINTCLIENT, (WPARAM)dc, flags);
        RestoreDC(dc, saved);
    }
    if (!(flags & PRF_CHILDREN))
        return;
    for (HWND child = GetWindow(GetWindow(hwnd, GW_CHILD), GW_HWNDLAST); child; child = GetWindow(child, GW_HWNDPREV)) {
        if (!IsWindowVisible(child))
            continue;
        RECT r;
        GetWindowRect(child, &r);
        int saved = SaveDC(dc);
        OffsetViewportOrgEx(dc, r.left - window.left, r.top - window.top, nullptr);
        IntersectClipRect(dc, 0, 0, r.right - r.left, r.bottom - r.top);
        SendMessageW(child, WM_PRINT, (WPARAM)dc, flags & ~PRF_CHECKVISIBLE);
        RestoreDC(dc, saved);
    }
}

/* ------------------------------------------------------------------ pointers and power */

#define FEEDBACK_MAX 12
#define GWFS_INCLUDE_ANCESTORS 1

/* the setting of a window, kept as a property holding the value plus one so that zero means none. */
static void feedback_name(WCHAR name[24], UINT feedback)
{
    wsprintfW(name, L"MimeFeedback%u", feedback);
}

/* the visual feedback of touch and pen input. a window has none set until a program sets one. */
WINUSERAPI BOOL WINAPI SetWindowFeedbackSetting(HWND window, UINT feedback, DWORD flags, UINT32 size,
                                                const void *configuration)
{
    WCHAR name[24];
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (!feedback || feedback > FEEDBACK_MAX || flags || (configuration ? size != sizeof(BOOL) : size != 0)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    feedback_name(name, feedback);
    if (!configuration) {
        RemovePropW(window, name);
        return TRUE;
    }
    return SetPropW(window, name, (HANDLE)(ULONG_PTR)(!!*(const BOOL *)configuration + 1));
}

WINUSERAPI BOOL WINAPI GetWindowFeedbackSetting(HWND window, UINT feedback, DWORD flags, UINT32 *size,
                                                void *configuration)
{
    WCHAR name[24];
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (!feedback || feedback > FEEDBACK_MAX || (flags & ~GWFS_INCLUDE_ANCESTORS) || !size ||
        (configuration && *size < sizeof(BOOL))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    feedback_name(name, feedback);
    ULONG_PTR value = 0;
    for (HWND w = window; w && !value; w = flags & GWFS_INCLUDE_ANCESTORS ? GetAncestor(w, GA_PARENT) : nullptr)
        value = (ULONG_PTR)GetPropW(w, name);
    if (!value)
        return FALSE;
    *size = sizeof(BOOL);
    if (configuration)
        *(BOOL *)configuration = (BOOL)(value - 1);
    return TRUE;
}


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
