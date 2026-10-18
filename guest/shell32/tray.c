/*
 * the notification area and app bars.
 *
 * mime has no taskbar yet. notification icons are accepted so programs
 * keep running, but nothing shows them and they have no position. app bar
 * queries describe the edge the host reserves outside the work area, which
 * is where the taskbar would be.
 */
#include "shell32_private.h"

/* the icons of the process, which stay until deleted or until their window is gone. */
#define MAX_ICONS 256
#define NIF_MESSAGE 0x01
#define NIF_ICON 0x02
#define NIF_TIP 0x04
#define NIF_STATE 0x08
#define NIF_GUID 0x20
#define NIS_HIDDEN 0x01
#define NOTIFYICON_VERSION_4 4

typedef struct _TRAY_ICON {
    BOOL Used;
    HWND Window;
    UINT Id;
    GUID Guid;
    BOOL HasGuid;
    UINT Callback;
    HICON Icon;
    WCHAR Tip[128];
    DWORD State;
    UINT Version;
} TRAY_ICON;

static SRWLOCK g_icon_lock = SRWLOCK_INIT;
static TRAY_ICON g_icons[MAX_ICONS];

/* the oldest size of the structure, whose tip is 64 characters. */
#define V1_SIZE (offsetof(NOTIFYICONDATAW, szTip) + 64 * sizeof(WCHAR))

static TRAY_ICON *find_icon(HWND window, UINT id, const GUID *guid)
{
    for (int i = 0; i < MAX_ICONS; i++) {
        TRAY_ICON *icon = &g_icons[i];
        if (!icon->Used)
            continue;
        /* the window of an icon that went away takes it along. */
        if (!IsWindow(icon->Window)) {
            memset(icon, 0, sizeof(*icon));
            continue;
        }
        if (guid ? icon->HasGuid && IsEqualGUID(&icon->Guid, guid) : !icon->HasGuid && icon->Window == window && icon->Id == id)
            return icon;
    }
    return nullptr;
}

static void apply_icon(TRAY_ICON *icon, const NOTIFYICONDATAW *data, SIZE_T size)
{
    if (data->uFlags & NIF_MESSAGE)
        icon->Callback = data->uCallbackMessage;
    if (data->uFlags & NIF_ICON)
        icon->Icon = data->hIcon;
    if (data->uFlags & NIF_TIP) {
        SIZE_T chars = size >= sizeof(NOTIFYICONDATAW) - sizeof(HICON) - sizeof(GUID) ? 128 : 64;
        SIZE_T n = 0;
        while (n + 1 < chars && data->szTip[n]) {
            icon->Tip[n] = data->szTip[n];
            n++;
        }
        icon->Tip[n] = 0;
    }
    if (data->uFlags & NIF_STATE)
        icon->State = (icon->State & ~data->dwStateMask) | (data->dwState & data->dwStateMask);
}

/*
 * the notification area is not shown: a program that adds an icon finds it, can modify and delete it, but
 * nothing draws it and no click reaches the program. windows answers FALSE when the taskbar is not running.
 */
SHSTDAPI BOOL WINAPI Shell_NotifyIconW(DWORD message, PNOTIFYICONDATAW data)
{
    SetLastError(ERROR_SUCCESS);
    if (!data || data->cbSize < V1_SIZE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = FALSE;
    DWORD error = ERROR_SUCCESS;
    const GUID *guid = (data->uFlags & NIF_GUID) && data->cbSize >= sizeof(NOTIFYICONDATAW) - sizeof(HICON) ? &data->guidItem : nullptr;
    AcquireSRWLockExclusive(&g_icon_lock);
    TRAY_ICON *icon = find_icon(data->hWnd, data->uID, guid);
    switch (message) {
    case NIM_ADD: {
        if (!IsWindow(data->hWnd)) {
            error = ERROR_INVALID_WINDOW_HANDLE;
            break;
        }
        if (icon) {
            error = ERROR_ALREADY_EXISTS;
            break;
        }
        for (int i = 0; i < MAX_ICONS && !icon; i++)
            if (!g_icons[i].Used)
                icon = &g_icons[i];
        if (!icon) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        memset(icon, 0, sizeof(*icon));
        icon->Used = TRUE;
        icon->Window = data->hWnd;
        icon->Id = data->uID;
        if (guid) {
            icon->Guid = *guid;
            icon->HasGuid = TRUE;
        }
        apply_icon(icon, data, data->cbSize);
        ok = TRUE;
        break;
    }
    case NIM_MODIFY:
        if (!icon) {
            error = ERROR_NOT_FOUND;
            break;
        }
        apply_icon(icon, data, data->cbSize);
        ok = TRUE;
        break;
    case NIM_DELETE:
        if (!icon) {
            error = ERROR_NOT_FOUND;
            break;
        }
        memset(icon, 0, sizeof(*icon));
        ok = TRUE;
        break;
    case NIM_SETFOCUS:
        ok = icon != nullptr;
        if (!ok)
            error = ERROR_NOT_FOUND;
        break;
    case NIM_SETVERSION:
        if (!icon) {
            error = ERROR_NOT_FOUND;
        } else if (data->uVersion > NOTIFYICON_VERSION_4) {
            error = ERROR_INVALID_PARAMETER;
        } else {
            icon->Version = data->uVersion;
            ok = TRUE;
        }
        break;
    default:
        error = ERROR_INVALID_PARAMETER;
    }
    ReleaseSRWLockExclusive(&g_icon_lock);
    if (error)
        SetLastError(error);
    return ok;
}

/* where an icon would sit, which is the end of the taskbar strip when the display reserves one. */
static BOOL tray_corner(RECT *rect);

SHSTDAPI HRESULT WINAPI Shell_NotifyIconGetRect(const NOTIFYICONIDENTIFIER *identifier, RECT *rect)
{
    if (!identifier || !rect || identifier->cbSize != sizeof(*identifier))
        return E_INVALIDARG;
    memset(rect, 0, sizeof(*rect));
    AcquireSRWLockExclusive(&g_icon_lock);
    TRAY_ICON *icon = find_icon(identifier->hWnd, identifier->uID, IsEqualGUID(&identifier->guidItem, &GUID_NULL) ? nullptr : &identifier->guidItem);
    BOOL hidden = icon && (icon->State & NIS_HIDDEN);
    ReleaseSRWLockExclusive(&g_icon_lock);
    if (!icon || hidden)
        return E_FAIL;
    return tray_corner(rect) ? S_OK : E_FAIL;
}

/* the strip of the primary monitor outside its work area, as a taskbar edge. */
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

static BOOL tray_corner(RECT *rect)
{
    UINT edge;
    RECT strip;
    const int size = 16;
    if (taskbar_position(&edge, &strip)) {
        if (edge == ABE_BOTTOM || edge == ABE_TOP) {
            rect->right = strip.right - 4;
            rect->left = rect->right - size;
            rect->top = (strip.top + strip.bottom - size) / 2;
        } else {
            rect->bottom = strip.bottom - 4;
            rect->top = rect->bottom - size;
            rect->left = (strip.left + strip.right - size) / 2;
        }
    } else {
        POINT origin = {0, 0};
        MONITORINFO info = {sizeof(info)};
        if (!GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &info))
            return FALSE;
        rect->right = info.rcWork.right - 4;
        rect->left = rect->right - size;
        rect->bottom = info.rcWork.bottom - 4;
        rect->top = rect->bottom - size;
    }
    rect->bottom = rect->top + size;
    rect->right = rect->left + size;
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
        /* app bars are accepted without reserving space; positions stay as asked. */
        return TRUE;
    case ABM_SETAUTOHIDEBAR:
    case ABM_SETAUTOHIDEBAREX:
        return FALSE;
    default:
        return 0;
    }
}
