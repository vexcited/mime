/*
 * input: a system thread drains host input events and queues them raw to the thread owning the target window. the
 * receiving thread turns them into messages, so hit testing and activation run in the right context.
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
    WND *old = InputForeground;
    InputForeground = w;
    if (old != w)
        InputForegroundChanged();
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
    HWND old = t ? t->Capture : nullptr;
    WND *w = hwnd ? WndLookup(hwnd) : nullptr;
    if (t && (!hwnd || (w && w->Thread == t)))
        InputSetCapture(t, w, 0);
    UserLeave();
    return old;
}

/*
 * a thread that keyboard messages do not reach reads the live key state, as on windows: only the thread taking the
 * keys sees them change message by message. games poll keys from threads of their own this way.
 */
static void sync_with_async(W32THREAD *t)
{
    t = W32InputOwner(t);
    W32THREAD *keys = InputForeground ? W32InputOwner(InputForeground->Thread) : nullptr;
    if (t == keys || (InputFocusOwner && W32InputOwner(InputFocusOwner) == t))
        return;
    for (int i = 0; i < 256; i++) {
        UCHAR down = InputAsyncKeys[i] & 0x80;
        if (down && !(t->KeyState[i] & 0x80))
            t->KeyState[i] ^= 1;
        t->KeyState[i] = (UCHAR)((t->KeyState[i] & 1) | down);
    }
}

SHORT NTAPI NtUserGetKeyState(int key)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    SHORT r = 0;
    if (t)
        sync_with_async(t);
    if (t && key >= 0 && key < 256) {
        /* the state byte sign extended, like windows: callers test bit 15 or bit 7. */
        r = (SHORT)(signed char)(W32InputOwner(t)->KeyState[key] & 0x81);
    }
    UserLeave();
    return r;
}

/* the 256 state bytes of the calling thread: bit 7 down, bit 0 toggled. */
BOOL NTAPI NtUserGetKeyboardState(PBYTE state)
{
    UCHAR copy[256];
    UserEnter();
    W32THREAD *t = W32GetThread();
    if (t)
        sync_with_async(t);
    for (int i = 0; i < 256; i++)
        copy[i] = t ? (UCHAR)(W32InputOwner(t)->KeyState[i] & 0x81) : 0;
    UserLeave();
    if (!NT_SUCCESS(MimeCopyToUser(state, copy, sizeof(copy)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    return TRUE;
}

BOOL NTAPI NtUserSetKeyboardState(PBYTE state)
{
    UCHAR copy[256];
    if (!NT_SUCCESS(MimeCopyFromUser(copy, state, sizeof(copy)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    if (t)
        for (int i = 0; i < 256; i++)
            W32InputOwner(t)->KeyState[i] = copy[i] & 0x81;
    UserLeave();
    return TRUE;
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
    DpiPixelConvert(&p, DPI_PHYSICAL, DpiCurrentAwareness());
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
    return w ? w->Thread : nullptr;
}

/*
 * the left and right modifier keys follow the modifier state each event carries, like the windows keyboard driver
 * keeps them: a press the host never forwarded, such as one made while another application had the focus, still
 * shows in GetKeyState(VK_LSHIFT).
 */
static void sync_async_sides(BOOLEAN on, UINT left, UINT right, UINT generic)
{
    if (!on)
        InputAsyncKeys[left] = InputAsyncKeys[right] = 0;
    else if (!(InputAsyncKeys[left] & 0x80) && !(InputAsyncKeys[right] & 0x80))
        InputAsyncKeys[left] = 0x80;
    InputAsyncKeys[generic] = on ? 0x80 : 0;
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
    sync_async_sides((ev->modifiers & MIME_MOD_SHIFT) != 0, VK_LSHIFT, VK_RSHIFT, VK_SHIFT);
    sync_async_sides((ev->modifiers & MIME_MOD_CONTROL) != 0, VK_LCONTROL, VK_RCONTROL, VK_CONTROL);
    sync_async_sides((ev->modifiers & MIME_MOD_ALT) != 0, VK_LMENU, VK_RMENU, VK_MENU);
}

static WPARAM mouse_key_state(const MimeInputEvent *ev)
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
    if (ev->modifiers & MIME_MOD_SHIFT)
        s |= MK_SHIFT;
    if (ev->modifiers & MIME_MOD_CONTROL)
        s |= MK_CONTROL;
    return s;
}

static UINT button_message(UINT button, BOOLEAN down, BOOLEAN nc);

/* low level hooks see input before it reaches any queue; a nonzero answer drops it. */
static BOOLEAN low_level_key(const MimeInputEvent *ev, BOOLEAN injected, ULONG_PTR extra)
{
    if (!HookActive(WH_KEYBOARD_LL, nullptr))
        return FALSE;
    KBDLLHOOKSTRUCT k = {};
    BOOLEAN up = ev->type == MIME_INPUT_KEY_UP;
    BOOLEAN alt = (ev->modifiers & MIME_MOD_ALT) != 0;
    if (ev->type == MIME_INPUT_CHAR) {
        /* a character of its own, like KEYEVENTF_UNICODE input. */
        k.vkCode = VK_PACKET;
        k.scanCode = ev->ch <= 0xFFFF ? ev->ch : '?';
    } else {
        k.vkCode = ev->vk;
        k.scanCode = ev->scancode & 0xFF;
        if (ev->scancode & 0x100)
            k.flags |= LLKHF_EXTENDED;
    }
    if (alt)
        k.flags |= LLKHF_ALTDOWN;
    if (up)
        k.flags |= LLKHF_UP;
    if (injected)
        k.flags |= LLKHF_INJECTED;
    k.time = W32TickCount();
    k.dwExtraInfo = extra;
    BOOLEAN sys = (alt && !(ev->modifiers & MIME_MOD_CONTROL)) || ev->vk == VK_F10;
    WPARAM msg = sys ? (up ? WM_SYSKEYUP : WM_SYSKEYDOWN) : (up ? WM_KEYUP : WM_KEYDOWN);
    return HookCall(WH_KEYBOARD_LL, nullptr, HC_ACTION, msg, (LPARAM)&k, sizeof(k), nullptr) != 0;
}

static BOOLEAN low_level_mouse(const MimeInputEvent *ev, BOOLEAN injected, ULONG_PTR extra)
{
    if (!HookActive(WH_MOUSE_LL, nullptr))
        return FALSE;
    MSLLHOOKSTRUCT m = {};
    m.pt = (POINT){ev->x, ev->y};
    UINT msg = WM_MOUSEMOVE;
    if (ev->type == MIME_INPUT_MOUSE_DOWN || ev->type == MIME_INPUT_MOUSE_UP) {
        msg = button_message(ev->button, ev->type == MIME_INPUT_MOUSE_DOWN, FALSE);
        if (ev->button == MIME_MOUSE_X1 || ev->button == MIME_MOUSE_X2)
            m.mouseData = (DWORD)(ev->button == MIME_MOUSE_X1 ? XBUTTON1 : XBUTTON2) << 16;
    } else if (ev->type == MIME_INPUT_MOUSE_WHEEL) {
        msg = (ev->flags & 1) ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL;
        m.mouseData = (DWORD)(USHORT)(SHORT)ev->wheel << 16;
    }
    if (injected)
        m.flags |= LLMHF_INJECTED;
    m.time = W32TickCount();
    m.dwExtraInfo = extra;
    return HookCall(WH_MOUSE_LL, nullptr, HC_ACTION, msg, (LPARAM)&m, sizeof(m), nullptr) != 0;
}

/* the characters the host sends right after a key press share its fate. */
static BOOLEAN g_chars_follow_key;
static BOOLEAN g_key_dropped;

DWORD InputLastTime;
BOOLEAN InputPointerOutside;
BOOLEAN InputCursorByProgram;

static void dispatch_event(const MimeInputEvent *in, BOOLEAN injected, ULONG_PTR extra)
{
    W32THREAD *t = nullptr;
    InputLastTime = W32TickCount();
    MimeInputEvent copy = *in;
    const MimeInputEvent *ev = &copy;
    switch (copy.type) {
    case MIME_INPUT_MOUSE_MOVE:
    case MIME_INPUT_MOUSE_DOWN:
    case MIME_INPUT_MOUSE_UP:
    case MIME_INPUT_MOUSE_WHEEL: {
        /*
         * a program put the cursor where it is, and a host window that the pointer enters without moving does not
         * take it back: hardware that does not move changes nothing on windows.
         */
        if (injected)
            InputCursorByProgram = TRUE;
        else if (copy.type == MIME_INPUT_MOUSE_MOVE && !(copy.flags & 4) && !copy.dx && !copy.dy && InputCursorByProgram)
            return;
        else
            InputCursorByProgram = FALSE;
        InputPointerOutside = (copy.flags & 8) != 0;
        /* a locked pointer reports motion alone, which moves the cursor from where it is. */
        POINT pt = (copy.flags & 4) ? (POINT){InputCursorPos.x + copy.dx, InputCursorPos.y + copy.dy}
                                    : (POINT){copy.x, copy.y};
        pt = InputClampCursor(pt);
        copy.x = pt.x;
        copy.y = pt.y;
        break;
    }
    }
    if (ev->type == MIME_INPUT_CLIPBOARD) {
        ClipboardHostChanged((ULONG)ev->x, (ULONG)ev->y);
        return;
    }
    if (ev->type == MIME_INPUT_IME) {
        ImeHostEvent(ev);
        return;
    }
    if (ev->type >= MIME_INPUT_DND_ENTER && ev->type <= MIME_INPUT_DND_END) {
        DndHostEvent(ev);
        return;
    }
    if (ev->type == MIME_INPUT_CLIP_REQUEST) {
        ClipboardHostRequest((ULONG)ev->x, ev->button);
        return;
    }
    if (ev->type == MIME_INPUT_DISPLAY) {
        MetricsDisplayChanged();
        return;
    }
    /* user32 reads the layout from the boot information when it translates keys. */
    if (ev->type == MIME_INPUT_KEYMAP)
        return;
    if (ev->type == MIME_INPUT_TRAY) {
        TrayHostEvent(ev);
        return;
    }
    if (ev->type == MIME_INPUT_SHELL) {
        ShellHostEvent(ev);
        return;
    }
    if (ev->type == MIME_INPUT_CAMERA) {
        MimeCameraEvent(ev);
        return;
    }
    if (ev->type >= MIME_INPUT_PAD_ADDED && ev->type <= MIME_INPUT_PAD_STATE) {
        /* game controllers are devices of the kernel, which raw input reads too. */
        MimePadEvent(ev);
        RawPadEvent(ev);
        return;
    }
    switch (ev->type) {
    case MIME_INPUT_KEY_DOWN:
    case MIME_INPUT_KEY_UP:
        g_key_dropped = low_level_key(ev, injected, extra);
        g_chars_follow_key = ev->type == MIME_INPUT_KEY_DOWN;
        if (g_key_dropped)
            return;
        break;
    case MIME_INPUT_CHAR:
        if (g_chars_follow_key ? g_key_dropped : low_level_key(ev, injected, extra))
            return;
        break;
    case MIME_INPUT_MOUSE_MOVE:
    case MIME_INPUT_MOUSE_DOWN:
    case MIME_INPUT_MOUSE_UP:
    case MIME_INPUT_MOUSE_WHEEL:
        g_chars_follow_key = FALSE;
        if (low_level_mouse(ev, injected, extra))
            return;
        break;
    }
    BOOLEAN no_legacy = RawInputEvent(ev, injected);
    if (no_legacy && (ev->type == MIME_INPUT_KEY_DOWN || ev->type == MIME_INPUT_KEY_UP))
        g_key_dropped = TRUE;
    update_async_keys(ev);
    WPARAM mouse_state = mouse_key_state(ev);
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
        t = InputForeground ? InputForeground->Thread : nullptr;
        break;
    default: {
        WND *w = WndLookup((HWND)(ULONG_PTR)ev->window_id);
        t = w ? w->Thread : nullptr;
        break;
    }
    }
    if (!t || no_legacy)
        return;
    /* coalesce consecutive mouse moves still waiting in the queue. */
    if (ev->type == MIME_INPUT_MOUSE_MOVE && !IsListEmpty(&t->Input)) {
        QUEUED_MESSAGE *last = CONTAINING_RECORD(t->Input.Blink, QUEUED_MESSAGE, Links);
        if (last->Input.type == MIME_INPUT_MOUSE_MOVE && last->Input.window_id == ev->window_id) {
            last->Input = *ev;
            last->Msg.wParam = mouse_state;
            last->Origin = injected ? IMO_INJECTED : IMO_HARDWARE;
            last->ExtraInfo = extra;
            QueueWake(t, QS_MOUSEMOVE);
            return;
        }
    }
    QueuePostInput(t, ev, mouse_state, injected ? IMO_INJECTED : IMO_HARDWARE, extra);
}

/* events carry the left or right modifier key, never the generic one. */
static UINT specific_vk(const KEYBDINPUT *ki)
{
    BOOLEAN ext = (ki->dwFlags & KEYEVENTF_EXTENDEDKEY) != 0;
    switch (ki->wVk) {
    case VK_SHIFT: return ki->wScan == 0x36 ? VK_RSHIFT : VK_LSHIFT;
    case VK_CONTROL: return ext ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU: return ext ? VK_RMENU : VK_LMENU;
    }
    return ki->wVk;
}

static BOOLEAN key_down_after(UINT key, UINT vk, BOOLEAN down)
{
    return key == vk ? down : (InputAsyncKeys[key] & 0x80) != 0;
}

/* turns one injected input into the event the host would have sent. */
static BOOLEAN injected_event(const INPUT *in, MimeInputEvent *ev)
{
    memset(ev, 0, sizeof(*ev));
    if (in->type == INPUT_KEYBOARD) {
        const KEYBDINPUT *ki = &in->ki;
        if (ki->dwFlags & KEYEVENTF_UNICODE) {
            if (ki->dwFlags & KEYEVENTF_KEYUP)
                return FALSE;
            ev->type = MIME_INPUT_CHAR;
            ev->ch = ki->wScan;
        } else {
            UINT vk = specific_vk(ki);
            BOOLEAN down = !(ki->dwFlags & KEYEVENTF_KEYUP);
            ev->type = down ? MIME_INPUT_KEY_DOWN : MIME_INPUT_KEY_UP;
            ev->vk = vk;
            ev->scancode = (ki->wScan & 0xFF) | ((ki->dwFlags & KEYEVENTF_EXTENDEDKEY) ? 0x100 : 0);
            if (down && (InputAsyncKeys[vk & 0xFF] & 0x80))
                ev->flags = 2;
            if (key_down_after(VK_LSHIFT, vk, down) || key_down_after(VK_RSHIFT, vk, down))
                ev->modifiers |= MIME_MOD_SHIFT;
            if (key_down_after(VK_LCONTROL, vk, down) || key_down_after(VK_RCONTROL, vk, down))
                ev->modifiers |= MIME_MOD_CONTROL;
            if (key_down_after(VK_LMENU, vk, down) || key_down_after(VK_RMENU, vk, down))
                ev->modifiers |= MIME_MOD_ALT;
        }
        ev->window_id = InputForeground ? (uint32_t)(ULONG_PTR)WndHandle(InputForeground) : 0;
        return TRUE;
    }
    if (in->type != INPUT_MOUSE)
        return FALSE;
    if (InputAsyncKeys[VK_SHIFT] & 0x80)
        ev->modifiers |= MIME_MOD_SHIFT;
    if (InputAsyncKeys[VK_CONTROL] & 0x80)
        ev->modifiers |= MIME_MOD_CONTROL;
    if (InputAsyncKeys[VK_MENU] & 0x80)
        ev->modifiers |= MIME_MOD_ALT;
    const MOUSEINPUT *mi = &in->mi;
    POINT pt = InputCursorPos;
    if (mi->dwFlags & MOUSEEVENTF_MOVE) {
        if (mi->dwFlags & MOUSEEVENTF_ABSOLUTE) {
            /* the primary monitor, or all of them. */
            RECT area = (mi->dwFlags & MOUSEEVENTF_VIRTUALDESK) ? MonitorVirtualScreen()
                                                                : (RECT){0, 0, W32Display.Width, W32Display.Height};
            pt.x = area.left + (LONG)(((LONG64)mi->dx * (area.right - area.left) + 32767) / 65536);
            pt.y = area.top + (LONG)(((LONG64)mi->dy * (area.bottom - area.top) + 32767) / 65536);
        } else {
            pt.x += mi->dx;
            pt.y += mi->dy;
        }
    }
    static const struct {
        DWORD Flag;
        UINT Type, Button;
    } buttons[] = {
        {MOUSEEVENTF_LEFTDOWN, MIME_INPUT_MOUSE_DOWN, MIME_MOUSE_LEFT},
        {MOUSEEVENTF_LEFTUP, MIME_INPUT_MOUSE_UP, MIME_MOUSE_LEFT},
        {MOUSEEVENTF_RIGHTDOWN, MIME_INPUT_MOUSE_DOWN, MIME_MOUSE_RIGHT},
        {MOUSEEVENTF_RIGHTUP, MIME_INPUT_MOUSE_UP, MIME_MOUSE_RIGHT},
        {MOUSEEVENTF_MIDDLEDOWN, MIME_INPUT_MOUSE_DOWN, MIME_MOUSE_MIDDLE},
        {MOUSEEVENTF_MIDDLEUP, MIME_INPUT_MOUSE_UP, MIME_MOUSE_MIDDLE},
    };
    ev->type = MIME_INPUT_MOUSE_MOVE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(buttons); i++) {
        if (mi->dwFlags & buttons[i].Flag) {
            ev->type = buttons[i].Type;
            ev->button = buttons[i].Button;
            break;
        }
    }
    if (mi->dwFlags & (MOUSEEVENTF_WHEEL | MOUSEEVENTF_HWHEEL)) {
        ev->type = MIME_INPUT_MOUSE_WHEEL;
        ev->wheel = (LONG)mi->mouseData;
        ev->flags = (mi->dwFlags & MOUSEEVENTF_HWHEEL) ? 1 : 0;
    }
    ev->x = pt.x;
    ev->y = pt.y;
    if (mi->dwFlags & MOUSEEVENTF_MOVE) {
        ev->dx = (mi->dwFlags & MOUSEEVENTF_ABSOLUTE) ? pt.x - InputCursorPos.x : mi->dx;
        ev->dy = (mi->dwFlags & MOUSEEVENTF_ABSOLUTE) ? pt.y - InputCursorPos.y : mi->dy;
    }
    WND *top = WndChildFromPoint(WndDesktop(), pt, FALSE);
    ev->window_id = top ? (uint32_t)(ULONG_PTR)WndHandle(top) : 0;
    return TRUE;
}

/* a drag the host took over ends without the guest seeing the button go up. */
void InputReleaseLeftButton(void)
{
    if (!(InputAsyncKeys[VK_LBUTTON] & 0x80))
        return;
    MimeInputEvent ev = {.type = MIME_INPUT_MOUSE_UP, .button = MIME_MOUSE_LEFT, .x = InputCursorPos.x,
                         .y = InputCursorPos.y};
    WND *top = WndChildFromPoint(WndDesktop(), InputCursorPos, FALSE);
    ev.window_id = top ? (uint32_t)(ULONG_PTR)WndHandle(top) : 0;
    ev.time_ms = (uint64_t)(KeQueryInterruptTime() / 10000);
    dispatch_event(&ev, FALSE, 0);
}

/* the inputs are queued in order, like the hardware events of the same desktop. */
UINT NTAPI NtUserSendInput(UINT count, const INPUT *user_inputs, int size)
{
    if (size != sizeof(INPUT)) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT done = 0;
    for (; done < count; done++) {
        INPUT in;
        if (!NT_SUCCESS(MimeCopyFromUser(&in, &user_inputs[done], sizeof(in)))) {
            W32SetLastError(ERROR_NOACCESS);
            break;
        }
        MimeInputEvent ev;
        UserEnter();
        if (injected_event(&in, &ev)) {
            ev.time_ms = (uint64_t)(KeQueryInterruptTime() / 10000);
            ULONG_PTR extra = in.type == INPUT_KEYBOARD ? in.ki.dwExtraInfo : in.mi.dwExtraInfo;
            dispatch_event(&ev, TRUE, extra);
        }
        UserLeave();
    }
    return done;
}

HANDLE InputThreadId, InputProcessId;

/*
 * the host stamps events with its own clock, while messages carry the tick count of GetTickCount and GetMessageTime.
 * events move to that clock by an offset that keeps their spacing, taken again when an event would land in the future
 * or long past, as after the machine was suspended.
 */
static void guest_time(MimeInputEvent *ev)
{
    static LONG64 offset;
    static BOOLEAN known;
    LONG64 now = (LONG64)W32TickCount(), at = (LONG64)ev->time_ms + offset;
    if (!known || at > now || now - at > 5000) {
        offset = now - (LONG64)ev->time_ms;
        known = TRUE;
        at = now;
    }
    ev->time_ms = (uint64_t)at;
}

static void NTAPI input_thread(PVOID context)
{
    InputThreadId = PsGetCurrentThreadId();
    InputProcessId = PsGetCurrentProcessId();
    PKEVENT ready = HalMimeGetInputEvent();
    ULONG64 pa = (ULONG64)MmGetPhysicalAddress(g_events).QuadPart;
    for (;;) {
        KeWaitForSingleObject(ready, Executive, KernelMode, FALSE, nullptr);
        for (;;) {
            ULONG64 count = 0;
            if (HalMimeHypercall(MIME_HC_INPUT_RECV, pa, INPUT_BATCH, 0, 0, 0, 0, &count) != MIME_HC_STATUS_OK ||
                !count)
                break;
            if (count > INPUT_BATCH)
                count = INPUT_BATCH;
            UserEnter();
            for (ULONG64 i = 0; i < count; i++) {
                guest_time(&g_events[i]);
                dispatch_event(&g_events[i], FALSE, 0);
            }
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
    NTSTATUS status = PsCreateSystemThread(&thread, 0, nullptr, nullptr, nullptr, input_thread, nullptr);
    if (NT_SUCCESS(status))
        ZwClose(thread);
    else
        DbgPrint("win32k: cannot start the input thread: %08x\n", status);
}

/* ------------------------------------------------------------------ translation in the receiving thread */

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

/* the window under pt and its hit test result, skipping transparent children. */
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
            return nullptr;
        if (h != HTTRANSPARENT || !w->Parent || WndIsTopLevel(w)) {
            *hit = h == HTTRANSPARENT ? HTNOWHERE : h;
            return w;
        }
        /* look for a sibling below the transparent window before falling back to the parent. */
        WND *next = nullptr;
        for (WND *s = w->Next; s; s = s->Next)
            if ((s->Style & WS_VISIBLE) && !(s->Style & WS_DISABLED) && RectContains(&s->WindowRect, pt.x, pt.y)) {
                next = s;
                break;
            }
        w = next ? WndFromPoint(next, pt) : w->Parent;
    }
    return nullptr;
}

static void track_mouse(W32THREAD *t, WND *w, LONG hit)
{
    HWND h = w ? WndHandle(w) : nullptr;
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

static void set_key_state(W32THREAD *t, UINT vk, BOOLEAN down)
{
    if (vk >= 256)
        return;
    t = W32InputOwner(t);
    if (down && !(t->KeyState[vk] & 0x80))
        t->KeyState[vk] ^= 1;
    t->KeyState[vk] = (UCHAR)((t->KeyState[vk] & 1) | (down ? 0x80 : 0));
}

/* the thread side of sync_async_sides. */
static void sync_sides(W32THREAD *t, BOOLEAN on, UINT left, UINT right, UINT generic)
{
    t = W32InputOwner(t);
    if (!on) {
        set_key_state(t, left, FALSE);
        set_key_state(t, right, FALSE);
    } else if (!(t->KeyState[left] & 0x80) && !(t->KeyState[right] & 0x80)) {
        set_key_state(t, left, TRUE);
    }
    set_key_state(t, generic, on);
}

static void sync_mouse_state(W32THREAD *t, const MimeInputEvent *ev, WPARAM state)
{
    static const struct {
        UINT Key;
        WPARAM Mask;
    } buttons[] = {{VK_LBUTTON, MK_LBUTTON}, {VK_RBUTTON, MK_RBUTTON}, {VK_MBUTTON, MK_MBUTTON},
                   {VK_XBUTTON1, MK_XBUTTON1}, {VK_XBUTTON2, MK_XBUTTON2}};
    UCHAR *keys = W32InputOwner(t)->KeyState;
    for (ULONG i = 0; i < RTL_NUMBER_OF(buttons); i++) {
        UINT key = buttons[i].Key;
        keys[key] = (UCHAR)((keys[key] & 1) | ((state & buttons[i].Mask) ? 0x80 : 0));
    }
    sync_sides(t, (ev->modifiers & MIME_MOD_SHIFT) != 0, VK_LSHIFT, VK_RSHIFT, VK_SHIFT);
    sync_sides(t, (ev->modifiers & MIME_MOD_CONTROL) != 0, VK_LCONTROL, VK_RCONTROL, VK_CONTROL);
    sync_sides(t, (ev->modifiers & MIME_MOD_ALT) != 0, VK_LMENU, VK_RMENU, VK_MENU);
}

/*
 * a window of another thread under a window of this one, such as the browser a program hosts in a window of its own, has
 * its mouse messages from the queue of its own thread: this thread received the event for the top level window and hands
 * the message on.
 */
static BOOLEAN of_another_thread(W32THREAD *t, const WND *w)
{
    return w->Thread && w->Thread != t;
}

static BOOLEAN translate_mouse(W32THREAD *t, QUEUED_MESSAGE *qm, MSG *out, HWND filter, UINT min, UINT max,
                               BOOLEAN remove, BOOLEAN *consumed)
{
    const MimeInputEvent *ev = &qm->Input;
    /* the pointer is in physical pixels, a window sees it in its own. */
    POINT pt = {ev->x, ev->y};
    WND *w;
    LONG hit;
    WND *capture = WndLookup(t->Capture);
    if (capture) {
        w = capture;
        hit = t->CaptureNc ? (LONG)t->CaptureNc : HTCLIENT;
        DpiPixelConvert(&pt, DPI_PHYSICAL, capture->Awareness);
    } else {
        WND *top = WndLookup((HWND)(ULONG_PTR)ev->window_id);
        if (!top || top->Thread != t || !(top->Style & WS_VISIBLE)) {
            *consumed = TRUE;
            return FALSE;
        }
        DpiPixelConvert(&pt, DPI_PHYSICAL, top->Awareness);
        /* the host gave the event to a layered window whose pixel there is clear: the window below gets it. */
        if (WndIsTopLevel(top) && (top->ExStyle & WS_EX_LAYERED) && !LayerHitTest(top, pt)) {
            POINT physical = {ev->x, ev->y};
            WND *below = WndChildFromPoint(WndDesktop(), physical, FALSE);
            if (below && below != top && below->Thread == t) {
                top = below;
                pt = physical;
                DpiPixelConvert(&pt, DPI_PHYSICAL, top->Awareness);
            }
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
    if (remove && ev->type == MIME_INPUT_MOUSE_DOWN)
        t->AltAlone = FALSE;
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
        wparam = MAKEWPARAM((WORD)qm->Msg.wParam, (SHORT)ev->wheel);
        lparam = MAKELPARAM(pt.x, pt.y);
        if (of_another_thread(t, w)) {
            if (remove) {
                sync_mouse_state(t, ev, qm->Msg.wParam);
                QueuePost(w->Thread, WndHandle(w), msg, wparam, lparam);
                *consumed = TRUE;
            }
            return FALSE;
        }
        BOOLEAN matches = emit(out, WndHandle(w), msg, wparam, lparam, ev, filter, min, max);
        if (matches && remove)
            sync_mouse_state(t, ev, qm->Msg.wParam);
        return matches;
    }

    if (hit == HTERROR) {
        /* disabled window: a click on a window disabled by a modal popup brings the popup forward. */
        if (remove && ev->type == MIME_INPUT_MOUSE_DOWN) {
            WND *top = WndTopLevel(w);
            WND *popup = nullptr;
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
        LONG dx = MetricsGetFor(SM_CXDOUBLECLK, 0, w->Awareness) / 2,
             dy = MetricsGetFor(SM_CYDOUBLECLK, 0, w->Awareness) / 2;
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
        wparam = qm->Msg.wParam;
        if (ev->button >= MIME_MOUSE_X1 && msg != WM_MOUSEMOVE)
            wparam = MAKEWPARAM(wparam, ev->button == MIME_MOUSE_X1 ? XBUTTON1 : XBUTTON2);
        lparam = MAKELPARAM(pt.x - w->ClientRect.left, pt.y - w->ClientRect.top);
    }
    if (dbl)
        msg += WM_LBUTTONDBLCLK - WM_LBUTTONDOWN;

    if (!MsgFilterMatches(WndHandle(w), msg, filter, min, max))
        return FALSE;

    if (remove) {
        sync_mouse_state(t, ev, qm->Msg.wParam);
        UserReference(&w->Header);
        if (ev->type == MIME_INPUT_MOUSE_DOWN) {
            g_last_click.Hwnd = dbl ? nullptr : WndHandle(w);
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
            track_mouse(of_another_thread(t, w) ? w->Thread : t, w, hit);
            if (ev->type != MIME_INPUT_MOUSE_UP)
                MsgSend(w, WM_SETCURSOR, (WPARAM)WndHandle(w), MAKELPARAM(hit, msg));
        } else if (!w->Header.Destroyed) {
            /* under capture the mouse window is still the one under the pointer, without hit test messages. */
            WND *top = WndLookup((HWND)(ULONG_PTR)ev->window_id);
            POINT under_pt = {ev->x, ev->y};
            if (top)
                DpiPixelConvert(&under_pt, DPI_PHYSICAL, top->Awareness);
            WND *under = top && top->Thread == t && (top->Style & WS_VISIBLE) ? WndFromPoint(top, under_pt) : nullptr;
            track_mouse(t, under,
                        under && RectContains(&under->ClientRect, under_pt.x, under_pt.y) ? HTCLIENT : HTNOWHERE);
        }
        BOOLEAN dead = w->Header.Destroyed;
        BOOLEAN handed_on = !dead && !capture && of_another_thread(t, w);
        if (handed_on)
            QueuePost(w->Thread, WndHandle(w), msg, wparam, lparam);
        UserDereference(&w->Header);
        if (dead || handed_on) {
            *consumed = TRUE;
            return FALSE;
        }
    }
    /* a message that is only looked at is not handed on, it stays where it is. */
    if (!remove && !capture && of_another_thread(t, w))
        return FALSE;
    return emit(out, WndHandle(w), msg, wparam, lparam, ev, filter, min, max);
}

W32THREAD *InputFocusOwner;

/*
 * a window of another thread with the focus inside the foreground window, such as the page of a browser a program hosts:
 * the keys are for the thread that holds the focus, as Windows gives them once the threads of a window and its child
 * share their input. The thread that set the focus last holds it.
 */
static W32THREAD *foreign_focus_thread(W32THREAD *t)
{
    W32THREAD *owner = InputFocusOwner;
    if (!owner || owner == t || owner->Destroying || !InputForeground)
        return nullptr;
    WND *focus = WndLookup(owner->Focus);
    if (!focus || focus->Thread != owner || WndTopLevel(focus) != InputForeground)
        return nullptr;
    return owner;
}

static BOOLEAN translate_key(W32THREAD *t, QUEUED_MESSAGE *qm, MSG *out, HWND filter, UINT min, UINT max,
                             BOOLEAN remove, BOOLEAN *consumed)
{
    const MimeInputEvent *ev = &qm->Input;
    W32THREAD *owner = foreign_focus_thread(t);
    WND *target = WndLookup(owner ? owner->Focus : t->Focus);
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
        if (owner) {
            if (remove) {
                QueuePost(owner, WndHandle(target), msg, ch, lparam);
                *consumed = TRUE;
            }
            return FALSE;
        }
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
    if (!owner && !MsgFilterMatches(WndHandle(target), msg, filter, min, max))
        return FALSE;
    if (owner && !remove)
        return FALSE;
    if (remove) {
        /* both threads see the keys that go down, the one that gets them and the one that is the queue's. */
        for (int i = 0; i < 2; i++) {
            W32THREAD *state = i ? owner : t;
            /* attached threads share one state, which takes the key once. */
            if (!state || (i && W32InputOwner(owner) == W32InputOwner(t)))
                continue;
            set_key_state(state, ev->vk, down);
            sync_sides(state, (ev->modifiers & MIME_MOD_SHIFT) != 0, VK_LSHIFT, VK_RSHIFT, VK_SHIFT);
            sync_sides(state, (ev->modifiers & MIME_MOD_CONTROL) != 0, VK_LCONTROL, VK_RCONTROL, VK_CONTROL);
            sync_sides(state, alt, VK_LMENU, VK_RMENU, VK_MENU);
            if (ev->modifiers & MIME_MOD_CAPS)
                W32InputOwner(state)->KeyState[VK_CAPITAL] |= 1;
            else
                W32InputOwner(state)->KeyState[VK_CAPITAL] &= ~1;
        }
    }
    if (remove) {
        /* a menu bar opens on the release of Alt or F10, when no other key or button came between. */
        for (int i = 0; i < 2; i++) {
            W32THREAD *state = i ? owner : t;
            /* attached threads share one state, which takes the key once. */
            if (!state || (i && W32InputOwner(owner) == W32InputOwner(t)))
                continue;
            if (ev->vk == VK_LMENU || ev->vk == VK_RMENU || ev->vk == VK_MENU) {
                if (down && !(ev->flags & 2))
                    state->AltAlone = !(ev->modifiers & MIME_MOD_CONTROL);
            } else if (ev->vk == VK_F10) {
                if (down)
                    state->AltAlone = !(ev->modifiers & MIME_MOD_SHIFT);
            } else if (down) {
                state->AltAlone = FALSE;
            }
        }
    }
    UINT vk = ev->vk;
    /* applications see the generic modifier keys. */
    if (vk == VK_LSHIFT || vk == VK_RSHIFT)
        vk = VK_SHIFT;
    else if (vk == VK_LCONTROL || vk == VK_RCONTROL)
        vk = VK_CONTROL;
    else if (vk == VK_LMENU || vk == VK_RMENU)
        vk = VK_MENU;
    if (owner) {
        QueuePost(owner, WndHandle(target), msg, vk, lparam);
        *consumed = TRUE;
        return FALSE;
    }
    return emit(out, WndHandle(target), msg, vk, lparam, ev, filter, min, max);
}

/* a host focus event for a window of the thread that is still waiting in its queue. */
static BOOLEAN focus_gain_queued(W32THREAD *t)
{
    for (PLIST_ENTRY e = t->Input.Flink; e != &t->Input; e = e->Flink) {
        QUEUED_MESSAGE *q = CONTAINING_RECORD(e, QUEUED_MESSAGE, Links);
        if (q->Kind != QM_INPUT || q->Input.type != MIME_INPUT_WINDOW_FOCUS || !q->Input.x)
            continue;
        WND *gained = WndLookup((HWND)(ULONG_PTR)q->Input.window_id);
        if (gained && gained->Thread == t)
            return TRUE;
    }
    return FALSE;
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
    case MIME_INPUT_WINDOW_STATE: {
        UINT command;
        switch (ev->x) {
        case SW_MINIMIZE:
        case SW_SHOWMINIMIZED:
            command = SC_MINIMIZE;
            break;
        case SW_MAXIMIZE:
            command = SC_MAXIMIZE;
            break;
        case SW_RESTORE:
            command = SC_RESTORE;
            break;
        default:
            return FALSE;
        }
        UserReference(&w->Header);
        MsgSend(w, WM_SYSCOMMAND, command, 0);
        if (!w->Header.Destroyed) {
            if ((LONG)(ev->state_request - w->HostStateRequest) > 0)
                w->HostStateRequest = ev->state_request;
            WndUpdateHostConfig(w, InputForeground == w && !(w->Style & WS_MINIMIZE));
        }
        UserDereference(&w->Header);
        break;
    }
    case MIME_INPUT_WINDOW_FULLSCREEN: {
        RECT rect = {ev->x, ev->y, ev->x + ev->width, ev->y + ev->height};
        DpiRectConvert(&rect, DPI_PHYSICAL, w->Awareness);
        WndFullscreen(w, (ev->flags & 1) != 0, &rect);
        break;
    }
    case MIME_INPUT_WINDOW_MOVED:
        w->Flags |= WNDF_HOST_MOVING;
        POINT moved = {ev->x, ev->y};
        DpiPointConvert(&moved, DPI_PHYSICAL, w->Awareness);
        WndSetPos(w, nullptr, moved.x, moved.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (!w->Header.Destroyed)
            w->Flags &= ~WNDF_HOST_MOVING;
        break;
    case MIME_INPUT_WINDOW_DISPLAY:
        WndHostDisplay(w, (ULONG)ev->x);
        break;
    case MIME_INPUT_WINDOW_SIZE:
        WndHostSize(w, ev->width, ev->height);
        break;
    case MIME_INPUT_WINDOW_FOCUS:
        if (ev->x) {
            if (InputForeground != w && !(w->Style & WS_DISABLED)) {
                WndActivatedByHost = TRUE;
                WndActivate(w, TRUE);
                WndActivatedByHost = FALSE;
            }
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
            InputSetForeground(nullptr);
            t->Active = nullptr;
            ImeFocusChanged();
            MsgSendEvent(w, WM_NCACTIVATE, FALSE, 0);
            if (!w->Header.Destroyed)
                MsgSendEvent(w, WM_ACTIVATE, WA_INACTIVE, 0);
            /* the host moves its focus from one window to the next with a loss and a gain, which windows never tells the program. */
            if (!w->Header.Destroyed && !focus_gain_queued(t))
                WndSendActivateApp(w->Thread, FALSE, nullptr);
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
        t->MouseWindow = nullptr;
        break;
    }
    return FALSE;
}
