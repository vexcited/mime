#include "shell32_private.h"
#include <mime/traydata.h>

WINUSERAPI BOOL WINAPI AllowSetForegroundWindow(DWORD process);

/* the sizes a program gives for the structure: the first one, with the balloon, with the guid, and whole. */
#define V1_SIZE (offsetof(NOTIFYICONDATAW, szTip) + 64 * sizeof(WCHAR))
#define V2_SIZE offsetof(NOTIFYICONDATAW, guidItem)
#define V3_SIZE offsetof(NOTIFYICONDATAW, hBalloonIcon)
#define V4_SIZE sizeof(NOTIFYICONDATAW)
#define ERROR_INCORRECT_SIZE 1462L

/* the picture of a balloon has the size of a small icon, or at least that of an icon when it is a large one. */
static BOOL balloon_icon_fits(const NOTIFYICONDATAW *data)
{
    HICON icon = data->hBalloonIcon ? data->hBalloonIcon : data->hIcon;
    ICONINFO info;
    BITMAP bitmap;
    BOOL fits = TRUE;
    if (!icon)
        return TRUE;
    if (!GetIconInfo(icon, &info)) {
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    if (GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap)) {
        BOOL large = (data->dwInfoFlags & NIIF_LARGE_ICON) != 0;
        if (!data->hBalloonIcon)
            fits = !(large && (bitmap.bmWidth < 32 || bitmap.bmHeight < 32));
        else if (large)
            fits = bitmap.bmWidth >= GetSystemMetrics(SM_CXICON) && bitmap.bmHeight >= GetSystemMetrics(SM_CYICON);
        else
            fits = bitmap.bmWidth == GetSystemMetrics(SM_CXSMICON) && bitmap.bmHeight == GetSystemMetrics(SM_CYSMICON);
    }
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    if (!fits)
        SetLastError(ERROR_INCORRECT_SIZE);
    return fits;
}

/*
 * Shell_NotifyIconW of shell32 26300: the request goes to the taskbar of the shell, and the answer is whether it
 * took it. a size that is none of the known ones is the first one, and the flags a size cannot carry are dropped.
 * every refusal leaves E_FAIL as the last error.
 */
SHSTDAPI BOOL WINAPI Shell_NotifyIconW(DWORD message, PNOTIFYICONDATAW data)
{
    static UINT taskbar_created;
    if (!data) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SetLastError(ERROR_SUCCESS);
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD size = data->cbSize;
    UINT allowed = 0x1FF;
    BOOL ok = taskbar != nullptr;
    if (ok && size == V4_SIZE) {
        if (message <= NIM_MODIFY && (data->uFlags & NIF_INFO) && (data->dwInfoFlags & NIIF_USER))
            ok = balloon_icon_fits(data);
    } else if (size == V3_SIZE) {
        allowed = 0x3F;
    } else if (size == V2_SIZE) {
        allowed = 0x1F;
    } else if (size != V4_SIZE) {
        size = V1_SIZE;
        allowed = 7;
    }
    if (ok) {
        TRAY_DATA request;
        memset(&request, 0, sizeof(request));
        request.Signature = TRAY_SIGNATURE;
        request.Message = message;
        request.Size = offsetof(TRAY_DATA, Path) - offsetof(TRAY_DATA, Size);
        request.Window = (ULONG)(ULONG_PTR)data->hWnd;
        request.Id = data->uID;
        request.Flags = data->uFlags & allowed;
        request.Callback = data->uCallbackMessage;
        request.Icon = (ULONG)(ULONG_PTR)data->hIcon;
        memcpy(request.Tip, data->szTip, (size == V1_SIZE ? 64 : 128) * sizeof(WCHAR));
        if (size >= V2_SIZE) {
            request.State = data->dwState;
            request.StateMask = data->dwStateMask;
            memcpy(request.Info, data->szInfo, sizeof(request.Info));
            request.Version = data->uVersion;
            memcpy(request.InfoTitle, data->szInfoTitle, sizeof(request.InfoTitle));
            request.InfoFlags = data->dwInfoFlags;
        }
        if (size >= V3_SIZE)
            request.Guid = data->guidItem;
        if (size >= V4_SIZE)
            request.BalloonIcon = (ULONG)(ULONG_PTR)data->hBalloonIcon;
        if (request.Flags & NIF_TIP)
            request.Tip[RTL_NUMBER_OF(request.Tip) - 1] = 0;
        if (size >= V2_SIZE && (request.Flags & NIF_INFO)) {
            request.Info[RTL_NUMBER_OF(request.Info) - 1] = 0;
            request.InfoTitle[RTL_NUMBER_OF(request.InfoTitle) - 1] = 0;
        }
        if (message == NIM_SETFOCUS) {
            DWORD process = 0;
            GetWindowThreadProcessId(taskbar, &process);
            AllowSetForegroundWindow(process);
        }
        if (!taskbar_created)
            taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        if (message == NIM_ADD)
            GetModuleFileNameW(nullptr, request.Path, RTL_NUMBER_OF(request.Path));
        /* the taskbar, of lower integrity than an elevated program, is let through for what it sends the window. */
        if (message <= NIM_DELETE && (request.Flags & NIF_MESSAGE) && data->hWnd) {
            DWORD action = message == NIM_DELETE ? MSGFLT_DISALLOW : MSGFLT_ALLOW;
            ChangeWindowMessageFilterEx(data->hWnd, request.Callback, action, nullptr);
            ChangeWindowMessageFilterEx(data->hWnd, taskbar_created, action, nullptr);
        }
        COPYDATASTRUCT copy = {TRAY_COPYDATA_NOTIFY, sizeof(request), &request};
        DWORD_PTR result = 0;
        BOOL sent = SendMessageTimeoutW(taskbar, WM_COPYDATA, (WPARAM)data->hWnd, (LPARAM)&copy,
                                        SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG, 7000, &result) != 0;
        if (sent) {
            ok = result == 1;
            SetLastError(ok ? ERROR_SUCCESS : (DWORD)E_FAIL);
        } else {
            if (!GetLastError())
                SetLastError(ERROR_TIMEOUT);
            ok = FALSE;
        }
    }
    if (!ok && !GetLastError())
        SetLastError((DWORD)E_FAIL);
    return ok;
}

/* the taskbar answers the size of the icon, then its place. no size is an icon it does not show. */
SHSTDAPI HRESULT WINAPI Shell_NotifyIconGetRect(const NOTIFYICONIDENTIFIER *identifier, RECT *rect)
{
    if (!identifier || !rect)
        return E_POINTER;
    memset(rect, 0, sizeof(*rect));
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar)
        return E_FAIL;
    if (identifier->cbSize != sizeof(*identifier))
        return E_INVALIDARG;
    TRAY_RECT_DATA request = {TRAY_SIGNATURE, TRAY_RECT_SIZE, 32, 0, (ULONG)(ULONG_PTR)identifier->hWnd, identifier->uID,
                              identifier->guidItem};
    COPYDATASTRUCT copy = {TRAY_COPYDATA_RECT, sizeof(request), &request};
    DWORD_PTR size = 0, place = 0;
    if (!SendMessageTimeoutW(taskbar, WM_COPYDATA, (WPARAM)identifier->hWnd, (LPARAM)&copy, SMTO_BLOCK | SMTO_ABORTIFHUNG,
                             7000, &size))
        return E_FAIL;
    if ((ULONG)size == 0xFFFFFFFF)
        return S_FALSE;
    if (!size)
        return E_FAIL;
    request.Kind = TRAY_RECT_POSITION;
    if (!SendMessageTimeoutW(taskbar, WM_COPYDATA, (WPARAM)identifier->hWnd, (LPARAM)&copy, SMTO_BLOCK | SMTO_ABORTIFHUNG,
                             7000, &place))
        return E_FAIL;
    rect->left = (SHORT)LOWORD(place);
    rect->top = (SHORT)HIWORD(place);
    rect->right = rect->left + (SHORT)LOWORD(size);
    rect->bottom = rect->top + (SHORT)HIWORD(size);
    return S_OK;
}

/* the strip of the primary monitor outside its work area stands in for the taskbar. */
static BOOL taskbar_position(UINT *edge, RECT *rect)
{
    POINT origin = {0, 0};
    MONITORINFO info = {sizeof(info)};
    if (!GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &info))
        return FALSE;
    RECT m = info.rcMonitor, w = info.rcWork;
    *rect = m;
    if (w.bottom < m.bottom) {
        *edge = ABE_BOTTOM;
        rect->top = w.bottom;
    } else if (w.top > m.top) {
        *edge = ABE_TOP;
        rect->bottom = w.top;
    } else if (w.left > m.left) {
        *edge = ABE_LEFT;
        rect->right = w.left;
    } else if (w.right < m.right) {
        *edge = ABE_RIGHT;
        rect->left = w.right;
    } else {
        return FALSE;
    }
    return TRUE;
}

SHSTDAPI UINT_PTR WINAPI SHAppBarMessage(DWORD message, PAPPBARDATA data)
{
    if (!data)
        return 0;
    switch (message) {
    case ABM_GETTASKBARPOS: {
        UINT edge;
        RECT rect;
        if (!taskbar_position(&edge, &rect))
            return FALSE;
        data->uEdge = edge;
        data->rc = rect;
        return TRUE;
    }
    case ABM_GETSTATE:
        /* the reserved edge never hides and stays above windows. */
        return ABS_ALWAYSONTOP;
    case ABM_GETAUTOHIDEBAR:
    case ABM_GETAUTOHIDEBAREX:
        return 0;
    case ABM_NEW:
    case ABM_REMOVE:
    case ABM_QUERYPOS:
    case ABM_SETPOS:
    case ABM_ACTIVATE:
    case ABM_WINDOWPOSCHANGED:
    case ABM_SETSTATE:
        /* app bars are accepted without reserving space, positions stay as asked. */
        return TRUE;
    case ABM_SETAUTOHIDEBAR:
    case ABM_SETAUTOHIDEBAREX:
        return FALSE;
    default:
        return 0;
    }
}
