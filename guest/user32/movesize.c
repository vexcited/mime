/*
 * the move and size loop DefWindowProc runs for SC_MOVE and SC_SIZE, as
 * windows does: a press on a sizing border becomes SC_SIZE with the edge,
 * then the window follows the mouse until the button goes up. the program
 * sees WM_ENTERSIZEMOVE, WM_SIZING or WM_MOVING at each step and
 * WM_EXITSIZEMOVE at the end; Escape puts the window back. started from the
 * keyboard, the arrows move or size by 8 pixels and Enter ends.
 */
#include "user32_private.h"

#define KEYBOARD_STEP 8

typedef struct MOVESIZE {
    HWND Window;
    HWND Parent; /* child windows are placed in their parent's client coordinates */
    BOOL Size;
    UINT Edge; /* WMSZ_ value, 0 until a key picks one */
    RECT Start;
    POINT Origin;
    POINT Offset; /* keyboard steps */
    BOOL Tracked; /* the pointer moved during the loop */
    MINMAXINFO Limits;
} MOVESIZE;

static BOOL has_left(UINT e)
{
    return e == WMSZ_LEFT || e == WMSZ_TOPLEFT || e == WMSZ_BOTTOMLEFT;
}

static BOOL has_right(UINT e)
{
    return e == WMSZ_RIGHT || e == WMSZ_TOPRIGHT || e == WMSZ_BOTTOMRIGHT;
}

static BOOL has_top(UINT e)
{
    return e == WMSZ_TOP || e == WMSZ_TOPLEFT || e == WMSZ_TOPRIGHT;
}

static BOOL has_bottom(UINT e)
{
    return e == WMSZ_BOTTOM || e == WMSZ_BOTTOMLEFT || e == WMSZ_BOTTOMRIGHT;
}

static LONG clamp(LONG v, LONG low, LONG high)
{
    return v < low ? low : v > high ? high : v;
}

static void set_cursor(const MOVESIZE *ms)
{
    LPCWSTR id = IDC_SIZEALL;
    if (ms->Size) {
        switch (ms->Edge) {
        case WMSZ_LEFT:
        case WMSZ_RIGHT:
            id = IDC_SIZEWE;
            break;
        case WMSZ_TOP:
        case WMSZ_BOTTOM:
            id = IDC_SIZENS;
            break;
        case WMSZ_TOPLEFT:
        case WMSZ_BOTTOMRIGHT:
            id = IDC_SIZENWSE;
            break;
        case WMSZ_TOPRIGHT:
        case WMSZ_BOTTOMLEFT:
            id = IDC_SIZENESW;
            break;
        }
    }
    SetCursor(LoadCursorW(NULL, id));
}

/* places the window for the mouse at pt, in screen coordinates. */
static void track(MOVESIZE *ms, POINT pt)
{
    LONG dx = pt.x - ms->Origin.x + ms->Offset.x, dy = pt.y - ms->Origin.y + ms->Offset.y;
    RECT r = ms->Start;
    if (!ms->Size) {
        OffsetRect(&r, dx, dy);
        SendMessageW(ms->Window, WM_MOVING, 0, (LPARAM)&r);
    } else {
        LONG min_w = ms->Limits.ptMinTrackSize.x, max_w = ms->Limits.ptMaxTrackSize.x;
        LONG min_h = ms->Limits.ptMinTrackSize.y, max_h = ms->Limits.ptMaxTrackSize.y;
        LONG width = ms->Start.right - ms->Start.left, height = ms->Start.bottom - ms->Start.top;
        if (has_left(ms->Edge))
            r.left = r.right - clamp(width - dx, min_w, max_w);
        if (has_right(ms->Edge))
            r.right = r.left + clamp(width + dx, min_w, max_w);
        if (has_top(ms->Edge))
            r.top = r.bottom - clamp(height - dy, min_h, max_h);
        if (has_bottom(ms->Edge))
            r.bottom = r.top + clamp(height + dy, min_h, max_h);
        SendMessageW(ms->Window, WM_SIZING, ms->Edge, (LPARAM)&r);
    }
    if (ms->Parent)
        MapWindowPoints(NULL, ms->Parent, (POINT *)&r, 2);
    RECT now;
    GetWindowRect(ms->Window, &now);
    if (ms->Parent)
        MapWindowPoints(NULL, ms->Parent, (POINT *)&now, 2);
    if (EqualRect(&now, &r))
        return;
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    if (!ms->Size)
        flags |= SWP_NOSIZE;
    SetWindowPos(ms->Window, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top, flags);
    /* show each step before the next: the mouse keeps moving while the program paints. */
    UpdateWindow(ms->Window);
}

/* an arrow key: picks the edge to size first, then steps. returns FALSE for other keys. */
static BOOL arrow(MOVESIZE *ms, UINT vk)
{
    LONG dx = 0, dy = 0;
    UINT edge;
    switch (vk) {
    case VK_LEFT:
        dx = -KEYBOARD_STEP;
        edge = WMSZ_LEFT;
        break;
    case VK_RIGHT:
        dx = KEYBOARD_STEP;
        edge = WMSZ_RIGHT;
        break;
    case VK_UP:
        dy = -KEYBOARD_STEP;
        edge = WMSZ_TOP;
        break;
    case VK_DOWN:
        dy = KEYBOARD_STEP;
        edge = WMSZ_BOTTOM;
        break;
    default:
        return FALSE;
    }
    if (ms->Size && !ms->Edge) {
        ms->Edge = edge;
        set_cursor(ms);
        return TRUE;
    }
    ms->Offset.x += dx;
    ms->Offset.y += dy;
    POINT pt;
    GetCursorPos(&pt);
    track(ms, pt);
    return TRUE;
}

static void move_size(HWND hwnd, WPARAM command, LPARAM lparam)
{
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    BOOL size = (command & 0xFFF0) == SC_SIZE;
    if ((style & (WS_MAXIMIZE | WS_MINIMIZE)) || (size && !(style & WS_THICKFRAME)) || !IsWindowVisible(hwnd))
        return;
    MOVESIZE ms;
    memset(&ms, 0, sizeof(ms));
    ms.Window = hwnd;
    ms.Parent = (style & WS_CHILD) ? GetParent(hwnd) : NULL;
    ms.Size = size;
    ms.Edge = size && (command & 0xF) <= WMSZ_BOTTOMRIGHT ? (UINT)(command & 0xF) : 0;
    GetWindowRect(hwnd, &ms.Start);
    /* the press that started the loop, or where the pointer is for a posted command. */
    if (lparam) {
        ms.Origin.x = (SHORT)LOWORD(lparam);
        ms.Origin.y = (SHORT)HIWORD(lparam);
    } else {
        GetCursorPos(&ms.Origin);
    }
    UINT dpi = GetDpiForWindow(hwnd);
    ms.Limits.ptMinTrackSize.x = GetSystemMetricsForDpi(SM_CXMINTRACK, dpi);
    ms.Limits.ptMinTrackSize.y = GetSystemMetricsForDpi(SM_CYMINTRACK, dpi);
    ms.Limits.ptMaxTrackSize.x = GetSystemMetricsForDpi(SM_CXMAXTRACK, dpi);
    ms.Limits.ptMaxTrackSize.y = GetSystemMetricsForDpi(SM_CYMAXTRACK, dpi);
    SendMessageW(hwnd, WM_GETMINMAXINFO, 0, (LPARAM)&ms.Limits);
    if (ms.Limits.ptMaxTrackSize.x < ms.Limits.ptMinTrackSize.x)
        ms.Limits.ptMaxTrackSize.x = ms.Limits.ptMinTrackSize.x;
    if (ms.Limits.ptMaxTrackSize.y < ms.Limits.ptMinTrackSize.y)
        ms.Limits.ptMaxTrackSize.y = ms.Limits.ptMinTrackSize.y;
    /* a press on the frame names the edge or the caption; a menu or keyboard command does not. */
    BOOL mouse = (command & 0xF) != 0;

    SetCapture(hwnd);
    set_cursor(&ms);
    SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
    BOOL cancel = FALSE;
    for (;;) {
        if (GetCapture() != hwnd)
            break;
        MSG msg;
        if (!GetMessageW(&msg, NULL, 0, 0)) {
            /* WM_QUIT ends the loop and stays in the queue for the caller. */
            PostQuitMessage((int)msg.wParam);
            break;
        }
        BOOL done = FALSE;
        switch (msg.message) {
        case WM_MOUSEMOVE: {
            /* only the latest position matters. */
            while (PeekMessageW(&msg, NULL, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE))
                ;
            if (ms.Size && !ms.Edge)
                break;
            ms.Tracked = TRUE;
            track(&ms, msg.pt);
            break;
        }
        case WM_LBUTTONUP:
            /* a release without movement leaves alone a window macos moved itself. */
            if (mouse && ms.Tracked)
                track(&ms, msg.pt);
            done = mouse;
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            done = !mouse;
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (msg.wParam == VK_ESCAPE)
                done = cancel = TRUE;
            else if (msg.wParam == VK_RETURN)
                done = TRUE;
            else if (!arrow(&ms, (UINT)msg.wParam)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR:
            break;
        default:
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            break;
        }
        if (done)
            break;
    }
    if (cancel) {
        RECT r = ms.Start;
        if (ms.Parent)
            MapWindowPoints(NULL, ms.Parent, (POINT *)&r, 2);
        SetWindowPos(hwnd, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (GetCapture() == hwnd)
        ReleaseCapture();
    if (IsWindow(hwnd))
        SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
}

/*
 * the parts of DefWindowProc that run in user32: sizing border presses and
 * the move and size commands. returns TRUE when the message was handled.
 */
BOOL UserDefMoveSize(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    switch (msg) {
    case WM_NCLBUTTONDOWN: {
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        if (wparam >= HTLEFT && wparam <= HTBOTTOMRIGHT) {
            if (!(style & WS_THICKFRAME) || (style & WS_MAXIMIZE))
                return FALSE;
            SendMessageW(hwnd, WM_SYSCOMMAND, SC_SIZE + (wparam - HTLEFT + WMSZ_LEFT), lparam);
            *result = 0;
            return TRUE;
        }
        /*
         * a caption the host drags itself reports the release with the press;
         * the others, such as a client area answering HTCAPTION, move here.
         */
        MSG up;
        if (wparam == HTCAPTION && !(style & WS_MAXIMIZE) &&
            !PeekMessageW(&up, hwnd, WM_NCLBUTTONUP, WM_NCLBUTTONUP, PM_NOREMOVE) &&
            !PeekMessageW(&up, hwnd, WM_LBUTTONUP, WM_LBUTTONUP, PM_NOREMOVE)) {
            SendMessageW(hwnd, WM_SYSCOMMAND, SC_MOVE + HTCAPTION, lparam);
            *result = 0;
            return TRUE;
        }
        return FALSE;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == SC_SIZE || (wparam & 0xFFF0) == SC_MOVE) {
            move_size(hwnd, wparam, lparam);
            *result = 0;
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}
