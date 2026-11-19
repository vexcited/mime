/*
 * the move and size loop of DefWindowProc, which user32 leaves to win32k: SC_MOVE and SC_SIZE. like windows, a press on
 * a caption or a sizing border becomes the command, then the window follows the mouse until the button goes up. the
 * program gets WM_ENTERSIZEMOVE, WM_MOVING or WM_SIZING at each step and WM_EXITSIZEMOVE at the end. Escape restores
 * the window. from the keyboard, the arrows move or size by 8 pixels and Enter ends.
 *
 * a host that moves windows itself, like a Wayland compositor, takes over once the pointer drags past the drag
 * distance. the loop then ends with the button, which win32k releases for the guest.
 *
 * all rectangles are physical pixels, the ones of WndSetPos: of the screen for a top level window, of the parent's
 * client area for a child.
 */
#include "win32k.h"

#define KEYBOARD_STEP 8

typedef struct MOVESIZE {
    HWND Window;
    BOOLEAN Child;
    BOOLEAN Size;
    UINT Edge; /* WMSZ_ value, 0 until a key picks one. */
    RECT Start;
    POINT Origin;
    POINT Offset; /* keyboard steps. */
    BOOLEAN Tracked; /* the pointer moved during the loop. */
    BOOLEAN Mouse; /* a press started it. */
    BOOLEAN Host; /* the host moves or sizes the window. */
    BOOLEAN HostRefused;
    BOOLEAN Maximized;
    BOOLEAN RestoredMaximized;
    RECT Normal;
    MINMAXINFO Limits;
} MOVESIZE;

static BOOLEAN has_left(UINT e)
{
    return e == WMSZ_LEFT || e == WMSZ_TOPLEFT || e == WMSZ_BOTTOMLEFT;
}

static BOOLEAN has_right(UINT e)
{
    return e == WMSZ_RIGHT || e == WMSZ_TOPRIGHT || e == WMSZ_BOTTOMRIGHT;
}

static BOOLEAN has_top(UINT e)
{
    return e == WMSZ_TOP || e == WMSZ_TOPLEFT || e == WMSZ_TOPRIGHT;
}

static BOOLEAN has_bottom(UINT e)
{
    return e == WMSZ_BOTTOM || e == WMSZ_BOTTOMLEFT || e == WMSZ_BOTTOMRIGHT;
}

static LONG clamp(LONG v, LONG low, LONG high)
{
    return v < low ? low : v > high ? high : v;
}

static void set_cursor(const MOVESIZE *ms)
{
    ULONG id = MIME_CURSOR_SIZE_ALL;
    if (ms->Size) {
        if (ms->Edge == WMSZ_LEFT || ms->Edge == WMSZ_RIGHT)
            id = MIME_CURSOR_SIZE_WE;
        else if (ms->Edge == WMSZ_TOP || ms->Edge == WMSZ_BOTTOM)
            id = MIME_CURSOR_SIZE_NS;
        else if (ms->Edge == WMSZ_TOPLEFT || ms->Edge == WMSZ_BOTTOMRIGHT)
            id = MIME_CURSOR_SIZE_NWSE;
        else if (ms->Edge == WMSZ_TOPRIGHT || ms->Edge == WMSZ_BOTTOMLEFT)
            id = MIME_CURSOR_SIZE_NESW;
    }
    HostSetCursor(id);
}

/* the window while it still exists and belongs to this thread. */
static WND *window_of(const MOVESIZE *ms)
{
    WND *w = WndLookup(ms->Window);
    return w && !w->Header.Destroyed && w->Thread == W32GetThread() ? w : nullptr;
}

/* the window rectangle the way WndSetPos takes it. */
static RECT placed_rect(WND *w)
{
    RECT r = w->WindowRect;
    if (!WndIsTopLevel(w) && w->Parent)
        RectOffset(&r, -w->Parent->ClientRect.left, -w->Parent->ClientRect.top);
    return r;
}

/*
 * sizing is handed over at once since the edge was pressed, moving only after a drag, so clicks and double clicks on
 * a caption stay the program's. TRUE when the host took over.
 */
static BOOLEAN host_takes_over(MOVESIZE *ms, POINT pt)
{
    WND *w = window_of(ms);
    if (!w || !ms->Mouse || ms->Child || (ms->Size && !ms->Edge))
        return FALSE;
    if (!ms->Size) {
        LONG threshold = MetricsGet(SM_CXDRAG, WndFrameDpi(w));
        LONG dx = pt.x - ms->Origin.x, dy = pt.y - ms->Origin.y;
        if (dx > -threshold && dx < threshold && dy > -threshold && dy < threshold)
            return FALSE;
    }
    if (HostMoveSize(w, ms->Size ? ms->Edge : 0)) {
        ms->Host = TRUE;
        return TRUE;
    }
    ms->HostRefused = TRUE;
    return FALSE;
}

/* places the window for the mouse at pt, in screen coordinates. */
static void track(MOVESIZE *ms, POINT pt)
{
    if (ms->Host || (!ms->HostRefused && host_takes_over(ms, pt)))
        return;
    WND *w = window_of(ms);
    if (!w)
        return;
    LONG dx = pt.x - ms->Origin.x + ms->Offset.x, dy = pt.y - ms->Origin.y + ms->Offset.y;
    if (ms->Maximized) {
        UINT dpi = WndFrameDpi(w);
        LONG threshold = MetricsGet(SM_CXDRAG, dpi);
        if (dx > -threshold && dx < threshold && dy > -threshold && dy < threshold)
            return;
        LONG max_width = ms->Start.right - ms->Start.left;
        LONG pointer_x = ms->Origin.x - ms->Start.left, pointer_y = ms->Origin.y - ms->Start.top;
        WndShow(w, SW_RESTORE);
        w = window_of(ms);
        if (!w || (w->Style & WS_MAXIMIZE))
            return;
        RECT restored = placed_rect(w);
        LONG width = restored.right - restored.left, height = restored.bottom - restored.top;
        LONG offset_x = max_width > 0 ? (LONG)((LONGLONG)pointer_x * width / max_width) : width / 2;
        LONG offset_y = clamp(pointer_y, 0, MetricsGet(SM_CYCAPTION, dpi) + MetricsGet(SM_CYFRAME, dpi));
        ms->Start = (RECT){pt.x - offset_x, pt.y - offset_y, pt.x - offset_x + width, pt.y - offset_y + height};
        ms->Origin = pt;
        ms->Offset = (POINT){0, 0};
        ms->Maximized = FALSE;
        ms->RestoredMaximized = TRUE;
        dx = dy = 0;
    }
    RECT r = ms->Start;
    if (!ms->Size) {
        RectOffset(&r, dx, dy);
        MsgSendKernel(w, WM_MOVING, 0, (LPARAM)&r, MIME_LPARAM_INOUT, sizeof(r));
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
        MsgSendKernel(w, WM_SIZING, ms->Edge, (LPARAM)&r, MIME_LPARAM_INOUT, sizeof(r));
    }
    w = window_of(ms);
    if (!w)
        return;
    RECT now = placed_rect(w);
    if (!memcmp(&now, &r, sizeof(r)))
        return;
    WndSetPos(w, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
              SWP_NOZORDER | SWP_NOACTIVATE | (ms->Size ? 0 : SWP_NOSIZE));
}

/* an arrow key: picks the edge to size first, then steps. FALSE for other keys. */
static BOOLEAN arrow(MOVESIZE *ms, UINT vk)
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
    track(ms, InputCursorPos);
    return TRUE;
}

/* lparam is the press that started the loop in physical screen pixels, or zero for a posted command. */
static void move_size(WND *w, WPARAM command, BOOLEAN pressed, POINT press)
{
    BOOLEAN size = (command & 0xFFF0) == SC_SIZE;
    if ((w->Style & WS_MINIMIZE) || (size && (!(w->Style & WS_THICKFRAME) || (w->Style & WS_MAXIMIZE))) || !WndIsVisible(w))
        return;
    W32THREAD *t = W32GetThread();
    if (!t || w->Thread != t)
        return;
    MOVESIZE ms;
    memset(&ms, 0, sizeof(ms));
    ms.Window = WndHandle(w);
    ms.Child = !WndIsTopLevel(w);
    ms.Size = size;
    ms.Maximized = (w->Style & WS_MAXIMIZE) != 0;
    ms.Normal = w->NormalRect;
    ms.Edge = size && (command & 0xF) <= WMSZ_BOTTOMRIGHT ? (UINT)(command & 0xF) : 0;
    ms.Start = placed_rect(w);
    ms.Origin = pressed ? press : InputCursorPos;
    WndGetMinMaxInfo(w, &ms.Limits);
    /* a press on the frame names the edge or the caption. a menu or keyboard command does not. */
    ms.Mouse = (command & 0xF) != 0;

    InputSetCapture(t, w, 0);
    set_cursor(&ms);
    MsgSend(w, WM_ENTERSIZEMOVE, 0, 0);
    if (size)
        host_takes_over(&ms, ms.Origin);
    BOOLEAN cancel = FALSE;
    for (;;) {
        if (!window_of(&ms) || t->Capture != ms.Window)
            break;
        MSG msg;
        memset(&msg, 0, sizeof(msg));
        if (!MsgPeek(t, &msg, nullptr, 0, 0, PM_REMOVE, TRUE))
            break;
        if (msg.message == WM_QUIT) {
            /* WM_QUIT ends the loop and stays in the queue for the caller. */
            t->QuitPosted = TRUE;
            t->QuitCode = (int)msg.wParam;
            break;
        }
        BOOLEAN done = FALSE;
        switch (msg.message) {
        case WM_MOUSEMOVE: {
            /* only the latest position matters. */
            MSG next;
            while (MsgPeek(t, &next, nullptr, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE, FALSE))
                msg = next;
            if (ms.Size && !ms.Edge)
                break;
            ms.Tracked = TRUE;
            track(&ms, msg.pt);
            break;
        }
        case WM_LBUTTONUP:
            /* a release without movement leaves alone a window the host moved itself. */
            if (ms.Mouse && ms.Tracked)
                track(&ms, msg.pt);
            done = ms.Mouse;
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            done = !ms.Mouse;
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (msg.wParam == VK_ESCAPE)
                done = cancel = TRUE;
            else if (msg.wParam == VK_RETURN)
                done = TRUE;
            else
                arrow(&ms, (UINT)msg.wParam);
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR:
            break;
        default: {
            WND *target = WndLookup(msg.hwnd);
            if (target && target->Thread == t) {
                MsgCallWindowProc(target, target->Proc, target->Ansi, msg.message, msg.wParam, msg.lParam, MIME_LPARAM_VALUE, 0);
                if (msg.message == WM_PAINT)
                    MsgPaintDispatched(target);
            }
            break;
        }
        }
        if (done)
            break;
    }
    w = window_of(&ms);
    if (cancel && w && !ms.Host) {
        if (ms.RestoredMaximized) {
            WINDOWPLACEMENT placement = {.length = sizeof(placement), .showCmd = SW_SHOWMAXIMIZED, .rcNormalPosition = ms.Normal};
            WndSetPlacement(w, &placement);
        } else {
            WndSetPos(w, nullptr, ms.Start.left, ms.Start.top, ms.Start.right - ms.Start.left, ms.Start.bottom - ms.Start.top,
                      SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    if (t->Capture == ms.Window)
        InputSetCapture(t, nullptr, 0);
    /* a WH_CBT hook sees where the window ends and may change it. */
    w = window_of(&ms);
    if (!cancel && w) {
        RECT end = w->WindowRect, hooked = end;
        HookCbt(w, HCBT_MOVESIZE, (WPARAM)ms.Window, (LPARAM)&hooked, sizeof(hooked));
        w = window_of(&ms);
        if (w && memcmp(&hooked, &end, sizeof(end))) {
            if (ms.Child && w->Parent)
                RectOffset(&hooked, -w->Parent->ClientRect.left, -w->Parent->ClientRect.top);
            WndSetPos(w, nullptr, hooked.left, hooked.top, hooked.right - hooked.left, hooked.bottom - hooked.top,
                      SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    w = window_of(&ms);
    if (w) {
        MsgSend(w, WM_EXITSIZEMOVE, 0, 0);
        w = window_of(&ms);
    }
    if (w) {
        POINT pt = InputCursorPos;
        DpiPixelConvert(&pt, DPI_PHYSICAL, w->Awareness);
        LRESULT hit = MsgSend(w, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
        w = window_of(&ms);
        if (w)
            MsgSend(w, WM_SETCURSOR, (WPARAM)ms.Window, MAKELPARAM(hit, WM_MOUSEMOVE));
    }
}

/*
 * the parts of DefWindowProc that move and size: presses on a caption or a sizing border, and the two commands. TRUE
 * when the message was handled. the point of the message is in the units of the window.
 */
BOOLEAN MoveSizeDefault(WND *w, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_NCLBUTTONDOWN:
        if (wparam >= HTLEFT && wparam <= HTBOTTOMRIGHT) {
            if (!(w->Style & WS_THICKFRAME) || (w->Style & WS_MAXIMIZE))
                return FALSE;
            MsgSend(w, WM_SYSCOMMAND, SC_SIZE + (wparam - HTLEFT + WMSZ_LEFT), lparam);
            return TRUE;
        }
        /* a release already queued is a click, which stays the program's. */
        if (wparam == HTCAPTION && InputHostPress) {
            MsgSend(w, WM_SYSCOMMAND, SC_MOVE + HTCAPTION, lparam);
            return TRUE;
        }
        return FALSE;
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == SC_SIZE || (wparam & 0xFFF0) == SC_MOVE) {
            POINT press = {(SHORT)LOWORD(lparam), (SHORT)HIWORD(lparam)};
            DpiPixelConvert(&press, w->Awareness, DPI_PHYSICAL);
            move_size(w, wparam, lparam != 0, press);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}
