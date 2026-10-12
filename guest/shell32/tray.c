/*
 * the notification area and app bars.
 *
 * mime has no taskbar yet. notification icons are accepted so programs
 * keep running, but nothing shows them and they have no position. app bar
 * queries describe the edge the host reserves outside the work area, which
 * is where the taskbar would be.
 */
#include "shell32_private.h"

SHSTDAPI BOOL WINAPI Shell_NotifyIconW(DWORD message, PNOTIFYICONDATAW data)
{
    if (!data || !data->cbSize)
        return FALSE;
    switch (message) {
    case NIM_ADD:
    case NIM_MODIFY:
    case NIM_DELETE:
    case NIM_SETFOCUS:
    case NIM_SETVERSION:
        return TRUE;
    default:
        return FALSE;
    }
}

SHSTDAPI HRESULT WINAPI Shell_NotifyIconGetRect(const NOTIFYICONIDENTIFIER *identifier, RECT *rect)
{
    if (!identifier || !rect || identifier->cbSize != sizeof(*identifier))
        return E_INVALIDARG;
    memset(rect, 0, sizeof(*rect));
    return E_FAIL;
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
