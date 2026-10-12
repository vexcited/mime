/*
 * input: a system thread drains host input events and queues them raw to
 * the thread owning the target window. the receiving thread turns them into
 * messages, so hit testing and activation run in the right context.
 */
#include "win32k.h"

POINT InputCursorPos;
UCHAR InputAsyncKeys[256];
WND *InputForeground;

#define INPUT_BATCH (4096 / sizeof(MimeInputEvent))

static MimeInputEvent *g_events;

/* double click tracking. */
static struct {
    HWND Hwnd;
    UINT Message;
    DWORD Time;
    POINT Pt;
    LONG Hit;
} g_last_click;

#define DOUBLE_CLICK_MS 500

void InputSetForeground(WND *w)
{
    InputForeground = w;
}

void InputSetCapture(W32THREAD *t, WND *w, ULONG nc_hit)
{
    HWND old = t->Capture;
    t->Capture = WndHandle(w);
    t->CaptureNc = w ? nc_hit : 0;
    if (old && old != t->Capture) {
        WND *ow = WndLookup(old);
        if (ow)
            MsgSend(ow, WM_CAPTURECHANGED, 0, (LPARAM)t->Capture);
    }
}

HWND NTAPI NtUserSetCapture(HWND hwnd)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    HWND old = t ? t->Capture : NULL;
    WND *w = hwnd ? WndLookup(hwnd) : NULL;
    if (t && (!hwnd || (w && w->Thread == t)))
        InputSetCapture(t, w, 0);
    UserLeave();
    return old;
}

SHORT NTAPI NtUserGetKeyState(int key)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    SHORT r = 0;
    if (t && key >= 0 && key < 256) {
        /* the state byte sign extended, like windows: callers test bit 15 or bit 7. */
        r = (SHORT)(signed char)(t->KeyState[key] & 0x81);
    }
    UserLeave();
    return r;
}

SHORT NTAPI NtUserGetAsyncKeyState(int key)
{
    if (key < 0 || key >= 256)
        return 0;
    return (InputAsyncKeys[key] & 0x80) ? (SHORT)0x8000 : 0;
}

BOOL NTAPI NtUserGetCursorPos(LPPOINT point)
{
    POINT p = InputCursorPos;
    return NT_SUCCESS(MimeCopyToUser(point, &p, sizeof(p)));
}

BOOL NTAPI NtUserTrackMouseEvent(TRACKMOUSEEVENT *user_track)
{
    TRACKMOUSEEVENT tme;
    if (!NT_SUCCESS(MimeCopyFromUser(&tme, user_track, sizeof(tme))) || tme.cbSize != sizeof(tme))
        return FALSE;
    UserEnter();
    W32THREAD *t = W32GetThread();
    BOOL ok = FALSE;
    if (t) {
        if (tme.dwFlags & TME_QUERY) {
            tme = t->Track;
            tme.cbSize = sizeof(tme);
            ok = NT_SUCCESS(MimeCopyToUser(user_track, &tme, sizeof(tme)));
        } else if (tme.dwFlags & TME_CANCEL) {
            if (t->Track.hwndTrack == tme.hwndTrack)
                t->Track.dwFlags &= ~(tme.dwFlags & ~TME_CANCEL);
            ok = TRUE;
        } else {
            WND *w = WndLookup(tme.hwndTrack);
            if (w && w->Thread == t) {
                t->Track = tme;
                ok = TRUE;
                /* asking to track a window the mouse is not over yields an immediate leave. */
                if ((tme.dwFlags & TME_LEAVE) && t->MouseWindow != tme.hwndTrack) {
                    QueuePost(t, tme.hwndTrack, (tme.dwFlags & TME_NONCLIENT) ? WM_NCMOUSELEAVE : WM_MOUSELEAVE, 0, 0);
                    t->Track.dwFlags &= ~TME_LEAVE;
                }
            }
        }
    }
    UserLeave();
    return ok;
}

/* ------------------------------------------------------------------ the input thread */

static W32THREAD *mouse_target_thread(const MimeInputEvent *ev)
{
    if (InputForeground && InputForeground->Thread && InputForeground->Thread->Capture)
        return InputForeground->Thread;
    WND *w = WndLookup((HWND)(ULONG_PTR)ev->window_id);
    return w ? w->Thread : NULL;
}

static void update_async_keys(const MimeInputEvent *ev)
{
    static const UCHAR button_vk[] = {0, VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    switch (ev->type) {
    case MIME_INPUT_MOUSE_DOWN:
    case MIME_INPUT_MOUSE_UP:
        if (ev->button < RTL_NUMBER_OF(button_vk) && button_vk[ev->button])
            InputAsyncKeys[button_vk[ev->button]] = ev->type == MIME_INPUT_MOUSE_DOWN ? 0x80 : 0;
        break;
    case MIME_INPUT_KEY_DOWN:
    case MIME_INPUT_KEY_UP:
        if (ev->vk < 256)
            InputAsyncKeys[ev->vk] = ev->type == MIME_INPUT_KEY_DOWN ? 0x80 : 0;
        break;
    }
    InputAsyncKeys[VK_SHIFT] = (ev->modifiers & MIME_MOD_SHIFT) ? 0x80 : 0;
    InputAsyncKeys[VK_CONTROL] = (ev->modifiers & MIME_MOD_CONTROL) ? 0x80 : 0;
    InputAsyncKeys[VK_MENU] = (ev->modifiers & MIME_MOD_ALT) ? 0x80 : 0;
}

static void dispatch_event(const MimeInputEvent *ev)
{
    W32THREAD *t = NULL;
    if (ev->type == MIME_INPUT_CLIPBOARD) {
        ClipboardHostChanged();
        return;
    }
    update_async_keys(ev);
    switch (ev->type) {
    case MIME_INPUT_MOUSE_MOVE:
    case MIME_INPUT_MOUSE_DOWN:
    case MIME_INPUT_MOUSE_UP:
    case MIME_INPUT_MOUSE_WHEEL:
        InputCursorPos = (POINT){ev->x, ev->y};
        t = mouse_target_thread(ev);
        break;
    case MIME_INPUT_KEY_DOWN:
    case MIME_INPUT_KEY_UP:
    case MIME_INPUT_CHAR:
        t = InputForeground ? InputForeground->Thread : NULL;
        break;
    default: {
        WND *w = WndLookup((HWND)(ULONG_PTR)ev->window_id);
        t = w ? w->Thread : NULL;
        break;
    }
    }
    if (!t)
        return;
    /* coalesce consecutive mouse moves still waiting in the queue. */
    if (ev->type == MIME_INPUT_MOUSE_MOVE && !IsListEmpty(&t->Input)) {
        QUEUED_MESSAGE *last = CONTAINING_RECORD(t->Input.Blink, QUEUED_MESSAGE, Links);
        if (last->Input.type == MIME_INPUT_MOUSE_MOVE && last->Input.window_id == ev->window_id) {
            last->Input = *ev;
            QueueWake(t, QS_MOUSEMOVE);
            return;
        }
    }
    QueuePostInput(t, ev);
}

static void NTAPI input_thread(PVOID context)
{
    PKEVENT ready = HalMimeGetInputEvent();
    ULONG64 pa = (ULONG64)MmGetPhysicalAddress(g_events).QuadPart;
    for (;;) {
        KeWaitForSingleObject(ready, Executive, KernelMode, FALSE, NULL);
        for (;;) {
            ULONG64 count = 0;
            if (HalMimeHypercall(MIME_HC_INPUT_RECV, pa, INPUT_BATCH, 0, 0, 0, 0, &count) != MIME_HC_STATUS_OK ||
                !count)
                break;
            if (count > INPUT_BATCH)
                count = INPUT_BATCH;
            UserEnter();
            for (ULONG64 i = 0; i < count; i++)
                dispatch_event(&g_events[i]);
            UserLeave();
        }
    }
    (void)context;
}

void InputInitialize(void)
{
    g_events = MmAllocateContiguousPages(4096);
    if (!g_events) {
        DbgPrint("win32k: no memory for the input buffer\n");
        return;
    }
    const MimeBootInfo *boot = HalMimeGetBootInfo();
    InputCursorPos = (POINT){(LONG)boot->display.width / 2, (LONG)boot->display.height / 2};
    HANDLE thread;
    NTSTATUS status = PsCreateSystemThread(&thread, 0, NULL, NULL, NULL, input_thread, NULL);
    if (NT_SUCCESS(status))
        ZwClose(thread);
    else
        DbgPrint("win32k: cannot start the input thread: %08x\n", status);
}

/* ------------------------------------------------------------------ translation in the receiving thread */

static WPARAM mouse_key_state(W32THREAD *t)
{
    WPARAM s = 0;
    if (InputAsyncKeys[VK_LBUTTON] & 0x80)
        s |= MK_LBUTTON;
    if (InputAsyncKeys[VK_RBUTTON] & 0x80)
        s |= MK_RBUTTON;
    if (InputAsyncKeys[VK_MBUTTON] & 0x80)
        s |= MK_MBUTTON;
    if (InputAsyncKeys[VK_XBUTTON1] & 0x80)
        s |= MK_XBUTTON1;
    if (InputAsyncKeys[VK_XBUTTON2] & 0x80)
        s |= MK_XBUTTON2;
    if (InputAsyncKeys[VK_SHIFT] & 0x80)
        s |= MK_SHIFT;
    if (InputAsyncKeys[VK_CONTROL] & 0x80)
        s |= MK_CONTROL;
    (void)t;
    return s;
}

static UINT button_message(UINT button, BOOLEAN down, BOOLEAN nc)
{
    UINT base;
    switch (button) {
    case MIME_MOUSE_LEFT:
        base = down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
        break;
    case MIME_MOUSE_RIGHT:
        base = down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
        break;
    case MIME_MOUSE_MIDDLE:
        base = down ? WM_MBUTTONDOWN : WM_MBUTTONUP;
        break;
    default:
        base = down ? WM_XBUTTONDOWN : WM_XBUTTONUP;
        break;
    }
    /* WM_NC* mouse messages sit at a fixed distance below their client counterparts. */
    return nc ? base - (WM_LBUTTONDOWN - WM_NCLBUTTONDOWN) : base;
}

/* window under pt and its hit test result, skipping transparent children. */
static WND *hit_test(WND *top, POINT pt, LONG *hit)
{
    WND *w = WndFromPoint(top, pt);
    while (w) {
        if (w->Style & WS_DISABLED) {
            *hit = HTERROR;
            return w;
        }
        LONG h = (LONG)MsgSend(w, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
        if (w->Header.Destroyed)
            return NULL;
        if (h != HTTRANSPARENT || !w->Parent || WndIsTopLevel(w)) {
            *hit = h == HTTRANSPARENT ? HTNOWHERE : h;
            return w;
        }
        /* look for a sibling below the transparent window before falling back to the parent. */
        WND *next = NULL;
        for (WND *s = w->Next; s; s = s->Next)
            if ((s->Style & WS_VISIBLE) && RectContains(&s->WindowRect, pt.x, pt.y)) {
                next = s;
                break;
            }
        w = next ? WndFromPoint(next, pt) : w->Parent;
    }
    return NULL;
}

static void track_mouse(W32THREAD *t, WND *w, LONG hit)
{
    HWND h = WndHandle(w);
    BOOLEAN nc = hit != HTCLIENT;
    WND *old = WndLookup(t->MouseWindow);
    if (old && (old != w || (old->CaptionHot && !nc))) {
        if (old->CaptionHot)
            MsgSend(old, WM_NCMOUSELEAVE, 0, 0);
    }
    if (t->MouseWindow != h && (t->Track.dwFlags & TME_LEAVE) && t->Track.hwndTrack == t->MouseWindow &&
        t->MouseWindow) {
        QueuePost(t, t->MouseWindow, (t->Track.dwFlags & TME_NONCLIENT) ? WM_NCMOUSELEAVE : WM_MOUSELEAVE, 0, 0);
        t->Track.dwFlags &= ~TME_LEAVE;
    }
    t->MouseWindow = h;
}

static BOOLEAN emit(MSG *out, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, const MimeInputEvent *ev,
                    HWND filter, UINT min, UINT max)
{
    out->hwnd = hwnd;
    out->message = msg;
    out->wParam = wparam;
    out->lParam = lparam;
    out->time = (DWORD)ev->time_ms;
    out->pt = (POINT){ev->x, ev->y};
    if (ev->type >= MIME_INPUT_KEY_DOWN && ev->type <= MIME_INPUT_CHAR)
        out->pt = InputCursorPos;
    return MsgFilterMatches(hwnd, msg, filter, min, max);
}

static BOOLEAN translate_mouse(W32THREAD *t, QUEUED_MESSAGE *qm, MSG *out, HWND filter, UINT min, UINT max,
                               BOOLEAN remove, BOOLEAN *consumed)
{
    const MimeInputEvent *ev = &qm->Input;
    POINT pt = {ev->x, ev->y};
    WND *w;
    LONG hit;
    WND *capture = WndLookup(t->Capture);
    if (capture) {
        w = capture;
        hit = t->CaptureNc ? (LONG)t->CaptureNc : HTCLIENT;
    } else {
        WND *top = WndLookup((HWND)(ULONG_PTR)ev->window_id);
        if (!top || top->Thread != t || !(top->Style & WS_VISIBLE)) {
            *consumed = TRUE;
            return FALSE;
        }
        w = hit_test(top, pt, &hit);
        if (!w) {
            *consumed = TRUE;
            return FALSE;
        }
    }
    BOOLEAN nc = hit != HTCLIENT;
    UINT msg;
    WPARAM wparam;
    LPARAM lparam;
    switch (ev->type) {
    case MIME_INPUT_MOUSE_MOVE:
        msg = nc ? WM_NCMOUSEMOVE : WM_MOUSEMOVE;
        break;
    case MIME_INPUT_MOUSE_DOWN:
        msg = button_message(ev->button, TRUE, nc);
        break;
    case MIME_INPUT_MOUSE_UP:
        msg = button_message(ev->button, FALSE, nc);
        break;
    default:
        msg = (ev->flags & 1) ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL;
        break;
    }

    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
        WND *focus = WndLookup(t->Focus);
        if (focus && !capture && !RectContains(&w->WindowRect, pt.x, pt.y))
            w = focus;
        wparam = MAKEWPARAM((WORD)mouse_key_state(t), (SHORT)ev->wheel);
        lparam = MAKELPARAM(pt.x, pt.y);
        return emit(out, WndHandle(w), msg, wparam, lparam, ev, filter, min, max);
    }

    if (hit == HTERROR) {
        /* disabled window: a click on a window disabled by a modal popup brings the popup forward. */
        if (remove && ev->type == MIME_INPUT_MOUSE_DOWN) {
            WND *top = WndTopLevel(w);
            WND *popup = NULL;
            WND *desktop = WndDesktop();
            for (WND *c = desktop->FirstChild; c; c = c->Next)
                if (c->Owner == top && (c->Style & WS_VISIBLE) && !(c->Style & WS_DISABLED))
                    popup = c;
            HostBeep(MB_OK);
            if (popup)
                WndActivate(popup, TRUE);
            MsgSend(w, WM_SETCURSOR, (WPARAM)WndHandle(w), MAKELPARAM(HTERROR, msg));
        } else if (remove) {
            HostSetCursor(MIME_CURSOR_ARROW);
        }
        *consumed = TRUE;
        return FALSE;
    }

    BOOLEAN dbl = FALSE;
    if (ev->type == MIME_INPUT_MOUSE_DOWN) {
        BOOLEAN class_dbl = nc || (w->Class && (w->Class->Style & CS_DBLCLKS));
        LONG dx = MetricsGet(SM_CXDOUBLECLK, 0) / 2, dy = MetricsGet(SM_CYDOUBLECLK, 0) / 2;
        if (class_dbl && g_last_click.Hwnd == WndHandle(w) && g_last_click.Message == msg &&
            (DWORD)ev->time_ms - g_last_click.Time <= DOUBLE_CLICK_MS && g_last_click.Hit == hit &&
            pt.x >= g_last_click.Pt.x - dx && pt.x <= g_last_click.Pt.x + dx && pt.y >= g_last_click.Pt.y - dy &&
            pt.y <= g_last_click.Pt.y + dy)
            dbl = TRUE;
    }

    if (nc) {
        wparam = (WPARAM)hit;
        lparam = MAKELPARAM(pt.x, pt.y);
        if (ev->button >= MIME_MOUSE_X1 && msg != WM_NCMOUSEMOVE)
            wparam = MAKEWPARAM(hit, ev->button == MIME_MOUSE_X1 ? XBUTTON1 : XBUTTON2);
    } else {
        wparam = mouse_key_state(t);
        if (ev->button >= MIME_MOUSE_X1 && msg != WM_MOUSEMOVE)
            wparam = MAKEWPARAM(wparam, ev->button == MIME_MOUSE_X1 ? XBUTTON1 : XBUTTON2);
        lparam = MAKELPARAM(pt.x - w->ClientRect.left, pt.y - w->ClientRect.top);
    }
    if (dbl)
        msg += WM_LBUTTONDBLCLK - WM_LBUTTONDOWN;

    if (!MsgFilterMatches(WndHandle(w), msg, filter, min, max))
        return FALSE;

    if (remove) {
        UserReference(&w->Header);
        if (ev->type == MIME_INPUT_MOUSE_DOWN) {
            g_last_click.Hwnd = dbl ? NULL : WndHandle(w);
            g_last_click.Message = dbl ? 0 : msg;
            g_last_click.Time = (DWORD)ev->time_ms;
            g_last_click.Pt = pt;
            g_last_click.Hit = hit;
            WND *top = WndTopLevel(w);
            if (!capture && top && top != InputForeground && !(top->ExStyle & WS_EX_NOACTIVATE)) {
                LRESULT ma = MsgSend(w, WM_MOUSEACTIVATE, (WPARAM)WndHandle(top), MAKELPARAM(hit, msg));
                if (ma == MA_ACTIVATE || ma == MA_ACTIVATEANDEAT || !ma)
                    WndActivate(top, TRUE);
                if (ma == MA_ACTIVATEANDEAT || ma == MA_NOACTIVATEANDEAT) {
                    UserDereference(&w->Header);
                    *consumed = TRUE;
                    return FALSE;
                }
            }
        }
        if (!w->Header.Destroyed && !capture) {
            track_mouse(t, w, hit);
            if (ev->type != MIME_INPUT_MOUSE_UP)
                MsgSend(w, WM_SETCURSOR, (WPARAM)WndHandle(w), MAKELPARAM(hit, msg));
        }
        BOOLEAN dead = w->Header.Destroyed;
        UserDereference(&w->Header);
        if (dead) {
            *consumed = TRUE;
            return FALSE;
        }
        static const UCHAR button_vk[] = {0, VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
        if (ev->type != MIME_INPUT_MOUSE_MOVE && ev->button < RTL_NUMBER_OF(button_vk) && button_vk[ev->button])
            t->KeyState[button_vk[ev->button]] = ev->type == MIME_INPUT_MOUSE_DOWN ? 0x80 : 0;
    }
    return emit(out, WndHandle(w), msg, wparam, lparam, ev, filter, min, max);
}

static void set_key_state(W32THREAD *t, UINT vk, BOOLEAN down)
{
    if (vk >= 256)
        return;
    if (down && !(t->KeyState[vk] & 0x80))
        t->KeyState[vk] ^= 1;
    t->KeyState[vk] = (UCHAR)((t->KeyState[vk] & 1) | (down ? 0x80 : 0));
}

static BOOLEAN translate_key(W32THREAD *t, QUEUED_MESSAGE *qm, MSG *out, HWND filter, UINT min, UINT max,
                             BOOLEAN remove, BOOLEAN *consumed)
{
    const MimeInputEvent *ev = &qm->Input;
    WND *target = WndLookup(t->Focus);
    BOOLEAN to_active = FALSE;
    if (!target) {
        target = WndLookup(t->Active);
        to_active = TRUE;
    }
    if (!target) {
        *consumed = TRUE;
        return FALSE;
    }
    BOOLEAN alt = (ev->modifiers & MIME_MOD_ALT) != 0;
    BOOLEAN sys = (alt && !(ev->modifiers & MIME_MOD_CONTROL)) || to_active || ev->vk == VK_F10;
    if (ev->type == MIME_INPUT_CHAR) {
        WCHAR ch = ev->ch <= 0xFFFF ? (WCHAR)ev->ch : L'?';
        UINT msg = sys && alt ? WM_SYSCHAR : WM_CHAR;
        LPARAM lparam = 1 | ((LPARAM)(ev->scancode & 0xFF) << 16) | (alt ? (1 << 29) : 0);
        return emit(out, WndHandle(target), msg, ch, lparam, ev, filter, min, max);
    }
    BOOLEAN down = ev->type == MIME_INPUT_KEY_DOWN;
    UINT msg = sys ? (down ? WM_SYSKEYDOWN : WM_SYSKEYUP) : (down ? WM_KEYDOWN : WM_KEYUP);
    LPARAM lparam = 1 | ((LPARAM)(ev->scancode & 0xFF) << 16);
    if (ev->scancode & 0x100)
        lparam |= 1 << 24;
    if (alt)
        lparam |= 1 << 29;
    if (!down)
        lparam |= (LPARAM)3 << 30;
    else if (ev->flags & 2)
        lparam |= (LPARAM)1 << 30;
    if (!MsgFilterMatches(WndHandle(target), msg, filter, min, max))
        return FALSE;
    if (remove) {
        set_key_state(t, ev->vk, down);
        set_key_state(t, VK_SHIFT, (ev->modifiers & MIME_MOD_SHIFT) != 0);
        set_key_state(t, VK_CONTROL, (ev->modifiers & MIME_MOD_CONTROL) != 0);
        set_key_state(t, VK_MENU, alt);
        if (ev->vk == VK_LSHIFT || ev->vk == VK_RSHIFT || ev->vk == VK_LCONTROL || ev->vk == VK_RCONTROL ||
            ev->vk == VK_LMENU || ev->vk == VK_RMENU)
            set_key_state(t, ev->vk, down);
        if (ev->modifiers & MIME_MOD_CAPS)
            t->KeyState[VK_CAPITAL] |= 1;
        else
            t->KeyState[VK_CAPITAL] &= ~1;
    }
    UINT vk = ev->vk;
    /* applications see the generic modifier keys. */
    if (vk == VK_LSHIFT || vk == VK_RSHIFT)
        vk = VK_SHIFT;
    else if (vk == VK_LCONTROL || vk == VK_RCONTROL)
        vk = VK_CONTROL;
    else if (vk == VK_LMENU || vk == VK_RMENU)
        vk = VK_MENU;
    return emit(out, WndHandle(target), msg, vk, lparam, ev, filter, min, max);
}

BOOLEAN InputTranslate(W32THREAD *t, QUEUED_MESSAGE *qm, MSG *out, HWND filter, UINT min, UINT max, BOOLEAN remove,
                       BOOLEAN *consumed)
{
    MimeInputEvent *ev = &qm->Input;
    *consumed = FALSE;
    switch (ev->type) {
    case MIME_INPUT_MOUSE_MOVE:
    case MIME_INPUT_MOUSE_DOWN:
    case MIME_INPUT_MOUSE_UP:
    case MIME_INPUT_MOUSE_WHEEL:
        return translate_mouse(t, qm, out, filter, min, max, remove, consumed);
    case MIME_INPUT_KEY_DOWN:
    case MIME_INPUT_KEY_UP:
    case MIME_INPUT_CHAR:
        return translate_key(t, qm, out, filter, min, max, remove, consumed);
    }

    /* window events are handled once, whatever the filter. */
    *consumed = TRUE;
    WND *w = WndLookup((HWND)(ULONG_PTR)ev->window_id);
    if (!w || w->Thread != t)
        return FALSE;
    switch (ev->type) {
    case MIME_INPUT_WINDOW_MOVED:
        w->Flags |= WNDF_HOST_MOVING;
        WndSetPos(w, NULL, ev->x, ev->y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (!w->Header.Destroyed)
            w->Flags &= ~WNDF_HOST_MOVING;
        break;
    case MIME_INPUT_WINDOW_FOCUS:
        if (ev->x) {
            if (InputForeground != w && !(w->Style & WS_DISABLED))
                WndActivate(w, TRUE);
            else if (InputForeground != w) {
                /* a disabled owner: hand activation to its enabled popup. */
                WND *desktop = WndDesktop();
                for (WND *c = desktop->FirstChild; c; c = c->Next)
                    if (c->Owner == w && (c->Style & WS_VISIBLE) && !(c->Style & WS_DISABLED)) {
                        WndActivate(c, TRUE);
                        break;
                    }
            }
        } else if (InputForeground == w) {
            UserReference(&w->Header);
            InputSetForeground(NULL);
            t->Active = NULL;
            MsgSend(w, WM_NCACTIVATE, FALSE, 0);
            if (!w->Header.Destroyed)
                MsgSend(w, WM_ACTIVATE, WA_INACTIVE, 0);
            if (!w->Header.Destroyed)
                MsgSend(w, WM_ACTIVATEAPP, FALSE, 0);
            UserDereference(&w->Header);
        }
        break;
    case MIME_INPUT_WINDOW_CLOSE:
        *consumed = FALSE;
        if (!emit(out, WndHandle(w), WM_SYSCOMMAND, SC_CLOSE, 0, ev, filter, min, max))
            return FALSE;
        *consumed = TRUE;
        return TRUE;
    case MIME_INPUT_MOUSE_LEAVE:
        if (w->CaptionHot)
            MsgSend(w, WM_NCMOUSELEAVE, 0, 0);
        if (t->MouseWindow && (t->Track.dwFlags & TME_LEAVE) && t->Track.hwndTrack == t->MouseWindow) {
            QueuePost(t, t->MouseWindow, (t->Track.dwFlags & TME_NONCLIENT) ? WM_NCMOUSELEAVE : WM_MOUSELEAVE, 0, 0);
            t->Track.dwFlags &= ~TME_LEAVE;
        }
        t->MouseWindow = NULL;
        break;
    }
    return FALSE;
}
