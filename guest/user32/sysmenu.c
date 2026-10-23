/*
 * the window's system menu, opened from the caption, the icon or Alt+Space. it runs in user32 because the menu
 * runs a loop. the choice becomes WM_SYSCOMMAND.
 */
#include "user32_private.h"

static void track(HWND hwnd, int x, int y)
{
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    HMENU menu = (style & WS_SYSMENU) ? GetSystemMenu(hwnd, FALSE) : nullptr;
    if (!menu || (style & WS_CHILD))
        return;
    /* the window comes forward, as it does for a click. */
    SetForegroundWindow(hwnd);
    UINT cmd = (UINT)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_RIGHTBUTTON, x, y, hwnd,
                                      nullptr);
    if (cmd && IsWindow(hwnd))
        SendMessageW(hwnd, WM_SYSCOMMAND, cmd, MAKELPARAM(x, y));
}

/* opens the menu below the icon, for a click on it or Alt+Space. */
static void track_at_icon(HWND hwnd)
{
    RECT r;
    UINT dpi = GetDpiForWindow(hwnd);
    if (!GetWindowRect(hwnd, &r))
        return;
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    int frame = (style & WS_MAXIMIZE) ? 0 : GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi);
    track(hwnd, r.left + frame, r.top + frame + GetSystemMetricsForDpi(SM_CYCAPTION, dpi));
}

BOOL UserDefSystemMenu(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    switch (msg) {
    case WM_NCRBUTTONUP:
        /* the program first gets a context menu request, which it may handle itself. */
        if (wparam == HTCAPTION || wparam == HTSYSMENU) {
            SendMessageW(hwnd, WM_CONTEXTMENU, (WPARAM)hwnd, lparam);
            *result = 0;
            return TRUE;
        }
        return FALSE;
    case WM_CONTEXTMENU: {
        if (GetWindowLongW(hwnd, GWL_STYLE) & WS_CHILD)
            return FALSE;
        LRESULT hit = SendMessageW(hwnd, WM_NCHITTEST, 0, lparam);
        if (hit != HTCAPTION && hit != HTSYSMENU)
            return FALSE;
        track(hwnd, (short)LOWORD(lparam), (short)HIWORD(lparam));
        *result = 0;
        return TRUE;
    }
    case WM_NCLBUTTONDOWN:
        if (wparam == HTSYSMENU) {
            track_at_icon(hwnd);
            *result = 0;
            return TRUE;
        }
        return FALSE;
    case WM_SYSCHAR:
        if (wparam == ' ' && (GetWindowLongW(hwnd, GWL_STYLE) & WS_SYSMENU)) {
            track_at_icon(hwnd);
            *result = 0;
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}
