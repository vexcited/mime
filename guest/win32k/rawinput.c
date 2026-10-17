/*
 * raw input and the cursor's confinement. every keyboard and mouse event
 * becomes a packet that processes registered for its device get as
 * WM_INPUT, before the legacy messages, which a registration with
 * RIDEV_NOLEGACY suppresses. packets live in a ring: a handle stays valid
 * until the ring comes round, where windows frees it once the message is
 * done (guessed: no program reads a packet that late).
 *
 * the host pointer follows what the foreground program asks of the
 * cursor: kept in the ClipCursor rectangle, or locked to report motion
 * alone when the program hides the cursor and confines it, captures the
 * mouse through raw input or keeps moving it back.
 */
#include "win32k.h"

/* the handles and names of the system's keyboard and mouse (guessed: windows' depend on the hardware). */
#define KEYBOARD_HANDLE ((HANDLE)(ULONG_PTR)0x10041)
#define MOUSE_HANDLE ((HANDLE)(ULONG_PTR)0x10043)
static const WCHAR KEYBOARD_NAME[] = L"\\\\?\\ACPI#MIME0303#0#{884b96c3-56ef-11d1-bc8c-00a0c91405dd}";
static const WCHAR MOUSE_NAME[] = L"\\\\?\\ACPI#MIME0F13#0#{378de44c-56ef-11d1-bc8c-00a0c91405dd}";

#define MAX_REGISTRATIONS 64
#define RING 1024

typedef struct W32RAW {
    RAWINPUTDEVICE Devices[MAX_REGISTRATIONS];
    ULONG Count;
    BOOLEAN Warped; /* moved the hidden cursor with SetCursorPos */
} W32RAW;

typedef struct PACKET {
    ULONG Serial;
    DWORD Type;
    HANDLE Device;
    WPARAM Wparam;
    union {
        RAWMOUSE Mouse;
        RAWKEYBOARD Keyboard;
    };
} PACKET;

static PACKET g_ring[RING];
static ULONG g_serial;

static RECT g_clip;
static BOOLEAN g_clipped;
static W32PROCESS *g_clip_owner;
static ULONG g_pointer = MIME_POINTER_FREE, g_pointer_window;
static RECT g_pointer_rect;

/* ------------------------------------------------------------------ the cursor */

static RECT screen(void)
{
    return (RECT){0, 0, W32Display.Width, W32Display.Height};
}

/* where the cursor may go: the clip rectangle, else the screen; its right and bottom edges excluded. */
POINT InputClampCursor(POINT pt)
{
    RECT r = g_clipped ? g_clip : screen();
    if (pt.x < r.left)
        pt.x = r.left;
    if (pt.x >= r.right)
        pt.x = r.right - 1;
    if (pt.y < r.top)
        pt.y = r.top;
    if (pt.y >= r.bottom)
        pt.y = r.bottom - 1;
    return pt;
}

static BOOLEAN captures_mouse(const W32RAW *raw)
{
    for (ULONG i = 0; raw && i < raw->Count; i++) {
        const RAWINPUTDEVICE *d = &raw->Devices[i];
        if (d->usUsagePage == 1 && d->usUsage == 2 &&
            ((d->dwFlags & RIDEV_EXMODEMASK) == RIDEV_NOLEGACY || (d->dwFlags & RIDEV_CAPTUREMOUSE)))
            return TRUE;
    }
    return FALSE;
}

/* what the host pointer does, from the foreground program's cursor. */
void InputUpdatePointer(void)
{
    ULONG mode = MIME_POINTER_FREE, window = 0;
    RECT rect = {};
    WND *fg = InputForeground;
    WND *top = fg ? WndTopLevel(fg) : nullptr;
    if (top && (top->Flags & WNDF_HOST_WINDOW) && fg->Thread) {
        W32PROCESS *p = fg->Thread->Process;
        W32RAW *raw = p ? p->Raw : nullptr;
        BOOLEAN clip = g_clipped && g_clip_owner == p;
        if (InputCursorHidden && (clip || captures_mouse(raw) || (raw && raw->Warped)))
            mode = MIME_POINTER_RELATIVE;
        else if (clip)
            mode = MIME_POINTER_CONFINED;
        window = (ULONG)(ULONG_PTR)WndHandle(top);
        rect = g_clip;
    }
    if (mode == g_pointer && window == g_pointer_window &&
        (mode != MIME_POINTER_CONFINED || !memcmp(&rect, &g_pointer_rect, sizeof(rect))))
        return;
    g_pointer = mode;
    g_pointer_window = window;
    g_pointer_rect = rect;
    HalMimeHypercall(MIME_HC_POINTER, mode, window, (ULONG64)(LONG64)rect.left, (ULONG64)(LONG64)rect.top,
                     (ULONG64)(LONG64)rect.right, (ULONG64)(LONG64)rect.bottom, nullptr);
}

/* a program moved the cursor; one that hid it is recentering it to read motion. */
void InputCursorMoved(void)
{
    InputCursorPos = InputClampCursor(InputCursorPos);
    W32PROCESS *p = W32GetProcess();
    if (p && InputCursorHidden && InputForeground && InputForeground->Thread &&
        InputForeground->Thread->Process == p) {
        if (!p->Raw)
            p->Raw = W32Alloc(sizeof(W32RAW));
        if (p->Raw && !p->Raw->Warped) {
            p->Raw->Warped = TRUE;
            InputUpdatePointer();
        }
    }
}

/* the cursor shows again: moving it is no longer a way of reading motion. */
void InputCursorShown(void)
{
    WND *fg = InputForeground;
    W32PROCESS *p = fg && fg->Thread ? fg->Thread->Process : nullptr;
    if (p && p->Raw)
        p->Raw->Warped = FALSE;
    InputUpdatePointer();
}

/* the foreground went to another program: the clip it did not set goes (guessed: windows keeps it). */
void InputForegroundChanged(void)
{
    WND *fg = InputForeground;
    W32PROCESS *p = fg && fg->Thread ? fg->Thread->Process : nullptr;
    if (g_clipped && p && g_clip_owner != p) {
        g_clipped = FALSE;
        g_clip_owner = nullptr;
    }
    InputUpdatePointer();
}

BOOL NTAPI NtUserClipCursor(const RECT *user_rect)
{
    RECT r;
    if (user_rect && !NT_SUCCESS(MimeCopyFromUser(&r, user_rect, sizeof(r)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    UserEnter();
    if (!user_rect) {
        g_clipped = FALSE;
        g_clip_owner = nullptr;
    } else {
        /* windows orders the edges and keeps the result on the screen. */
        if (r.left > r.right) {
            LONG t = r.left;
            r.left = r.right;
            r.right = t;
        }
        if (r.top > r.bottom) {
            LONG t = r.top;
            r.top = r.bottom;
            r.bottom = t;
        }
        RECT s = screen();
        RECT c = {r.left > s.left ? r.left : s.left, r.top > s.top ? r.top : s.top,
                  r.right < s.right ? r.right : s.right, r.bottom < s.bottom ? r.bottom : s.bottom};
        if (c.right <= c.left)
            c.right = c.left + 1;
        if (c.bottom <= c.top)
            c.bottom = c.top + 1;
        g_clip = c;
        g_clipped = TRUE;
        g_clip_owner = W32GetProcess();
        InputCursorPos = InputClampCursor(InputCursorPos);
    }
    InputUpdatePointer();
    UserLeave();
    return TRUE;
}

BOOL NTAPI NtUserGetClipCursor(LPRECT user_rect)
{
    UserEnter();
    RECT r = g_clipped ? g_clip : screen();
    UserLeave();
    if (!user_rect || !NT_SUCCESS(MimeCopyToUser(user_rect, &r, sizeof(r)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ registration */

static BOOLEAN usage_matches(const RAWINPUTDEVICE *d, USHORT page, USHORT usage)
{
    if (d->usUsagePage != page)
        return FALSE;
    return d->usUsage == usage || ((d->dwFlags & RIDEV_EXMODEMASK) == RIDEV_PAGEONLY && !d->usUsage);
}

/* the registration of a process for a device, an exact usage before a whole page; null when excluded. */
static const RAWINPUTDEVICE *registration(const W32RAW *raw, USHORT page, USHORT usage)
{
    const RAWINPUTDEVICE *page_only = nullptr;
    for (ULONG i = 0; raw && i < raw->Count; i++) {
        const RAWINPUTDEVICE *d = &raw->Devices[i];
        if (d->usUsagePage != page)
            continue;
        if (d->usUsage == usage)
            return (d->dwFlags & RIDEV_EXMODEMASK) == RIDEV_EXCLUDE ? nullptr : d;
        if (!d->usUsage && (d->dwFlags & RIDEV_EXMODEMASK) == RIDEV_PAGEONLY)
            page_only = d;
    }
    return page_only;
}

static void notify_device(const RAWINPUTDEVICE *d, HWND target, BOOLEAN arrival)
{
    WND *w = WndLookup(target);
    if (!w)
        return;
    if (usage_matches(d, 1, 6))
        QueuePost(w->Thread, target, WM_INPUT_DEVICE_CHANGE, arrival ? GIDC_ARRIVAL : GIDC_REMOVAL,
                  (LPARAM)KEYBOARD_HANDLE);
    if (usage_matches(d, 1, 2))
        QueuePost(w->Thread, target, WM_INPUT_DEVICE_CHANGE, arrival ? GIDC_ARRIVAL : GIDC_REMOVAL,
                  (LPARAM)MOUSE_HANDLE);
}

/* windows' checks of one entry, then each replaces the process's registration of its usage. */
BOOL NTAPI NtUserRegisterRawInputDevices(PCRAWINPUTDEVICE user_devices, UINT count, UINT size)
{
    if (size != sizeof(RAWINPUTDEVICE) || !count || count > MAX_REGISTRATIONS) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RAWINPUTDEVICE devices[MAX_REGISTRATIONS];
    if (!user_devices || !NT_SUCCESS(MimeCopyFromUser(devices, user_devices, count * sizeof(*devices)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    BOOL ok = p != nullptr;
    for (UINT i = 0; ok && i < count; i++) {
        const RAWINPUTDEVICE *d = &devices[i];
        DWORD mode = d->dwFlags & RIDEV_EXMODEMASK;
        ULONG error = 0;
        if (d->dwFlags & ~(RIDEV_REMOVE | RIDEV_EXMODEMASK | RIDEV_INPUTSINK | RIDEV_CAPTUREMOUSE | RIDEV_APPKEYS |
                           RIDEV_EXINPUTSINK | RIDEV_DEVNOTIFY))
            error = ERROR_INVALID_FLAGS;
        else if (mode && mode != RIDEV_EXCLUDE && mode != RIDEV_PAGEONLY && mode != RIDEV_NOLEGACY)
            error = ERROR_INVALID_FLAGS;
        else if ((mode == RIDEV_PAGEONLY) != (d->usUsage == 0) && !(d->dwFlags & RIDEV_REMOVE))
            error = ERROR_INVALID_PARAMETER;
        else if ((d->dwFlags & RIDEV_REMOVE) && d->hwndTarget)
            error = ERROR_INVALID_PARAMETER;
        else if ((d->dwFlags & (RIDEV_INPUTSINK | RIDEV_EXINPUTSINK)) && !d->hwndTarget)
            error = ERROR_INVALID_PARAMETER;
        else if ((d->dwFlags & RIDEV_CAPTUREMOUSE) && !(d->usUsagePage == 1 && d->usUsage == 6) &&
                 mode != RIDEV_NOLEGACY)
            error = ERROR_INVALID_FLAGS;
        else if (d->hwndTarget) {
            WND *w = WndLookup(d->hwndTarget);
            if (!w || !w->Thread || w->Thread->Process != p)
                error = ERROR_INVALID_WINDOW_HANDLE;
        }
        if (error) {
            W32SetLastError(error);
            ok = FALSE;
        }
    }
    if (ok && !p->Raw && !(p->Raw = W32Alloc(sizeof(W32RAW)))) {
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        ok = FALSE;
    }
    for (UINT i = 0; ok && i < count; i++) {
        const RAWINPUTDEVICE *d = &devices[i];
        W32RAW *raw = p->Raw;
        ULONG at = 0;
        while (at < raw->Count &&
               (raw->Devices[at].usUsagePage != d->usUsagePage || raw->Devices[at].usUsage != d->usUsage))
            at++;
        if (d->dwFlags & RIDEV_REMOVE) {
            if (at < raw->Count) {
                RAWINPUTDEVICE old = raw->Devices[at];
                raw->Devices[at] = raw->Devices[--raw->Count];
                if ((old.dwFlags & RIDEV_DEVNOTIFY) && old.hwndTarget)
                    notify_device(&old, old.hwndTarget, FALSE);
            }
            continue;
        }
        if (at == raw->Count) {
            if (raw->Count == MAX_REGISTRATIONS) {
                W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                ok = FALSE;
                break;
            }
            raw->Count++;
        }
        raw->Devices[at] = *d;
        if ((d->dwFlags & RIDEV_DEVNOTIFY) && d->hwndTarget)
            notify_device(d, d->hwndTarget, TRUE);
    }
    InputUpdatePointer();
    UserLeave();
    return ok;
}

UINT NTAPI NtUserGetRegisteredRawInputDevices(PRAWINPUTDEVICE user_devices, PUINT user_count, UINT size)
{
    UINT count;
    if (size != sizeof(RAWINPUTDEVICE) || !user_count || !NT_SUCCESS(MimeCopyFromUser(&count, user_count, sizeof(count)))) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return (UINT)-1;
    }
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    W32RAW *raw = p ? p->Raw : nullptr;
    UINT have = raw ? raw->Count : 0;
    RAWINPUTDEVICE devices[MAX_REGISTRATIONS];
    if (have)
        memcpy(devices, raw->Devices, have * sizeof(*devices));
    UserLeave();
    if (!user_devices) {
        MimeCopyToUser(user_count, &have, sizeof(have));
        return 0;
    }
    if (count < have) {
        MimeCopyToUser(user_count, &have, sizeof(have));
        W32SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return (UINT)-1;
    }
    if (have && !NT_SUCCESS(MimeCopyToUser(user_devices, devices, have * sizeof(*devices)))) {
        W32SetLastError(ERROR_NOACCESS);
        return (UINT)-1;
    }
    return have;
}

void RawFreeProcess(W32PROCESS *p)
{
    if (g_clip_owner == p) {
        g_clipped = FALSE;
        g_clip_owner = nullptr;
    }
    W32Free(p->Raw);
    p->Raw = nullptr;
    InputUpdatePointer();
}

/* ------------------------------------------------------------------ delivery */

/*
 * a packet to every process registered for its device: the foreground
 * one at its target or focus window, the others when they asked for
 * input in the background. true when the foreground process wants no
 * legacy messages for it.
 */
static BOOLEAN deliver(PACKET *packet, USHORT usage)
{
    WND *fg = InputForeground;
    W32THREAD *fg_thread = fg ? fg->Thread : nullptr;
    W32PROCESS *fg_process = fg_thread ? fg_thread->Process : nullptr;
    BOOLEAN no_legacy = FALSE;
    BOOLEAN fg_registered = fg_process && registration(fg_process->Raw, 1, usage);
    for (PLIST_ENTRY e = W32Processes.Flink; e != &W32Processes; e = e->Flink) {
        W32PROCESS *p = CONTAINING_RECORD(e, W32PROCESS, Links);
        const RAWINPUTDEVICE *d = registration(p->Raw, 1, usage);
        if (!d)
            continue;
        HWND target = d->hwndTarget;
        WPARAM code;
        if (p == fg_process) {
            code = RIM_INPUT;
            if (!target)
                target = fg_thread->Focus ? fg_thread->Focus : WndHandle(fg);
            if ((d->dwFlags & RIDEV_EXMODEMASK) == RIDEV_NOLEGACY)
                no_legacy = TRUE;
        } else if ((d->dwFlags & RIDEV_INPUTSINK) || ((d->dwFlags & RIDEV_EXINPUTSINK) && !fg_registered)) {
            code = RIM_INPUTSINK;
        } else {
            continue;
        }
        WND *w = target ? WndLookup(target) : nullptr;
        if (!w || !w->Thread)
            continue;
        ULONG serial = ++g_serial ? g_serial : ++g_serial;
        PACKET *slot = &g_ring[serial % RING];
        *slot = *packet;
        slot->Serial = serial;
        slot->Wparam = code;
        QueuePost(w->Thread, target, WM_INPUT, code, (LPARAM)(ULONG_PTR)serial);
    }
    return no_legacy;
}

/* the raw form of a host event, false to keep its legacy messages. */
BOOLEAN RawInputEvent(const MimeInputEvent *ev, BOOLEAN injected)
{
    PACKET packet = {};
    packet.Device = injected ? nullptr : (ev->type == MIME_INPUT_KEY_DOWN || ev->type == MIME_INPUT_KEY_UP)
                                             ? KEYBOARD_HANDLE
                                             : MOUSE_HANDLE;
    switch (ev->type) {
    case MIME_INPUT_KEY_DOWN:
    case MIME_INPUT_KEY_UP: {
        BOOLEAN up = ev->type == MIME_INPUT_KEY_UP;
        packet.Type = RIM_TYPEKEYBOARD;
        UINT vk = ev->vk;
        /* raw input reports the generic modifier keys. */
        if (vk == VK_LSHIFT || vk == VK_RSHIFT)
            vk = VK_SHIFT;
        else if (vk == VK_LCONTROL || vk == VK_RCONTROL)
            vk = VK_CONTROL;
        else if (vk == VK_LMENU || vk == VK_RMENU)
            vk = VK_MENU;
        BOOLEAN alt = (ev->modifiers & MIME_MOD_ALT) || vk == VK_MENU || vk == VK_F10;
        packet.Keyboard.VKey = (USHORT)vk;
        packet.Keyboard.Message = alt ? (up ? WM_SYSKEYUP : WM_SYSKEYDOWN) : (up ? WM_KEYUP : WM_KEYDOWN);
        if (vk == VK_PAUSE) {
            /* pause is e1 1d 45, two packets. */
            packet.Keyboard.MakeCode = 0x1D;
            packet.Keyboard.Flags = RI_KEY_E1 | (up ? RI_KEY_BREAK : 0);
            deliver(&packet, 6);
            packet.Keyboard.MakeCode = 0x45;
            packet.Keyboard.Flags = up ? RI_KEY_BREAK : RI_KEY_MAKE;
            return deliver(&packet, 6);
        }
        packet.Keyboard.MakeCode = (USHORT)(ev->scancode & 0xFF);
        packet.Keyboard.Flags = (USHORT)((up ? RI_KEY_BREAK : 0) | ((ev->scancode & 0x100) ? RI_KEY_E0 : 0));
        return deliver(&packet, 6);
    }
    case MIME_INPUT_MOUSE_MOVE:
        if (!ev->dx && !ev->dy)
            return (ev->flags & 4) != 0;
        packet.Type = RIM_TYPEMOUSE;
        packet.Mouse.usFlags = MOUSE_MOVE_RELATIVE;
        packet.Mouse.lLastX = ev->dx;
        packet.Mouse.lLastY = ev->dy;
        return deliver(&packet, 2);
    case MIME_INPUT_MOUSE_DOWN:
    case MIME_INPUT_MOUSE_UP: {
        static const USHORT DOWN[] = {0, RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_DOWN,
                                      RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_5_DOWN};
        if (ev->button < 1 || ev->button > 5)
            return FALSE;
        packet.Type = RIM_TYPEMOUSE;
        packet.Mouse.usButtonFlags = (USHORT)(ev->type == MIME_INPUT_MOUSE_DOWN ? DOWN[ev->button] : DOWN[ev->button] << 1);
        return deliver(&packet, 2);
    }
    case MIME_INPUT_MOUSE_WHEEL:
        packet.Type = RIM_TYPEMOUSE;
        packet.Mouse.usButtonFlags = (ev->flags & 1) ? RI_MOUSE_HWHEEL : RI_MOUSE_WHEEL;
        packet.Mouse.usButtonData = (USHORT)(SHORT)ev->wheel;
        return deliver(&packet, 2);
    default:
        return FALSE;
    }
}

/* ------------------------------------------------------------------ reading packets */

static PACKET *lookup(HRAWINPUT input)
{
    ULONG serial = (ULONG)(ULONG_PTR)input;
    if (!serial || (ULONG_PTR)input > 0xFFFFFFFFu)
        return nullptr;
    PACKET *p = &g_ring[serial % RING];
    return p->Serial == serial ? p : nullptr;
}

static UINT body_size(const PACKET *p)
{
    return p->Type == RIM_TYPEMOUSE ? sizeof(RAWMOUSE) : sizeof(RAWKEYBOARD);
}

/* a packet as the caller reads it: header, then its device's data. */
static UINT render(const PACKET *p, BYTE *out)
{
    RAWINPUTHEADER h = {p->Type, (DWORD)(sizeof(h) + body_size(p)), p->Device, p->Wparam};
    memcpy(out, &h, sizeof(h));
    if (p->Type == RIM_TYPEMOUSE)
        memcpy(out + sizeof(h), &p->Mouse, sizeof(p->Mouse));
    else
        memcpy(out + sizeof(h), &p->Keyboard, sizeof(p->Keyboard));
    return h.dwSize;
}

UINT NTAPI NtUserGetRawInputData(HRAWINPUT input, UINT command, PVOID user_data, PUINT user_size, UINT header_size)
{
    if (header_size != sizeof(RAWINPUTHEADER) || (command != RID_INPUT && command != RID_HEADER) || !user_size) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return (UINT)-1;
    }
    BYTE buffer[sizeof(RAWINPUT)];
    UserEnter();
    PACKET *p = lookup(input);
    UINT need = p ? (command == RID_HEADER ? sizeof(RAWINPUTHEADER) : render(p, buffer)) : 0;
    if (p && command == RID_HEADER)
        render(p, buffer);
    UserLeave();
    if (!p) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return (UINT)-1;
    }
    if (!user_data) {
        if (!NT_SUCCESS(MimeCopyToUser(user_size, &need, sizeof(need)))) {
            W32SetLastError(ERROR_NOACCESS);
            return (UINT)-1;
        }
        return 0;
    }
    UINT size;
    if (!NT_SUCCESS(MimeCopyFromUser(&size, user_size, sizeof(size)))) {
        W32SetLastError(ERROR_NOACCESS);
        return (UINT)-1;
    }
    if (size < need) {
        W32SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return (UINT)-1;
    }
    if (!NT_SUCCESS(MimeCopyToUser(user_data, buffer, need))) {
        W32SetLastError(ERROR_NOACCESS);
        return (UINT)-1;
    }
    return need;
}

/* the WM_INPUT messages of the calling thread, as many as fit, taken off its queue. */
UINT NTAPI NtUserGetRawInputBuffer(PRAWINPUT user_data, PUINT user_size, UINT header_size)
{
    if (header_size != sizeof(RAWINPUTHEADER) || !user_size) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return (UINT)-1;
    }
    UINT size;
    if (!NT_SUCCESS(MimeCopyFromUser(&size, user_size, sizeof(size)))) {
        W32SetLastError(ERROR_NOACCESS);
        return (UINT)-1;
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    UINT count = 0, used = 0, first = 0;
    BYTE buffer[sizeof(RAWINPUT)];
    UINT result = 0;
    for (PLIST_ENTRY e = t ? t->Posted.Flink : nullptr; e && e != &t->Posted;) {
        QUEUED_MESSAGE *qm = CONTAINING_RECORD(e, QUEUED_MESSAGE, Links);
        e = e->Flink;
        if (qm->Kind != QM_POSTED || qm->Msg.message != WM_INPUT)
            continue;
        PACKET *p = lookup((HRAWINPUT)qm->Msg.lParam);
        if (!p) {
            RemoveEntryList(&qm->Links);
            W32Free(qm);
            continue;
        }
        UINT n = render(p, buffer);
        UINT aligned = (n + 7) & ~7u;
        if (!first)
            first = n;
        if (!user_data)
            break;
        if (used + n > size)
            break;
        if (!NT_SUCCESS(MimeCopyToUser((BYTE *)user_data + used, buffer, n))) {
            W32SetLastError(ERROR_NOACCESS);
            result = (UINT)-1;
            break;
        }
        used += aligned;
        count++;
        RemoveEntryList(&qm->Links);
        W32Free(qm);
    }
    UserLeave();
    if (result == (UINT)-1)
        return result;
    if (!user_data) {
        MimeCopyToUser(user_size, &first, sizeof(first));
        return 0;
    }
    if (!count && first) {
        MimeCopyToUser(user_size, &first, sizeof(first));
        W32SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return (UINT)-1;
    }
    return count;
}

/* ------------------------------------------------------------------ devices */

UINT NTAPI NtUserGetRawInputDeviceList(PRAWINPUTDEVICELIST user_list, PUINT user_count, UINT size)
{
    static const RAWINPUTDEVICELIST DEVICES[] = {{KEYBOARD_HANDLE, RIM_TYPEKEYBOARD}, {MOUSE_HANDLE, RIM_TYPEMOUSE}};
    UINT have = RTL_NUMBER_OF(DEVICES), count;
    if (size != sizeof(RAWINPUTDEVICELIST) || !user_count) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return (UINT)-1;
    }
    if (!user_list) {
        if (!NT_SUCCESS(MimeCopyToUser(user_count, &have, sizeof(have)))) {
            W32SetLastError(ERROR_NOACCESS);
            return (UINT)-1;
        }
        return 0;
    }
    if (!NT_SUCCESS(MimeCopyFromUser(&count, user_count, sizeof(count)))) {
        W32SetLastError(ERROR_NOACCESS);
        return (UINT)-1;
    }
    if (count < have) {
        MimeCopyToUser(user_count, &have, sizeof(have));
        W32SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return (UINT)-1;
    }
    if (!NT_SUCCESS(MimeCopyToUser(user_list, DEVICES, sizeof(DEVICES)))) {
        W32SetLastError(ERROR_NOACCESS);
        return (UINT)-1;
    }
    return have;
}

/* the name in characters, the info in bytes; null data asks for the size. */
UINT NTAPI NtUserGetRawInputDeviceInfo(HANDLE device, UINT command, PVOID user_data, PUINT user_size)
{
    if (device != KEYBOARD_HANDLE && device != MOUSE_HANDLE) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return (UINT)-1;
    }
    BOOLEAN keyboard = device == KEYBOARD_HANDLE;
    UINT size;
    if (!user_size || !NT_SUCCESS(MimeCopyFromUser(&size, user_size, sizeof(size)))) {
        W32SetLastError(ERROR_NOACCESS);
        return (UINT)-1;
    }
    const void *data;
    UINT need, units;
    RID_DEVICE_INFO info = {sizeof(info)};
    switch (command) {
    case RIDI_DEVICENAME:
        data = keyboard ? KEYBOARD_NAME : MOUSE_NAME;
        units = keyboard ? RTL_NUMBER_OF(KEYBOARD_NAME) : RTL_NUMBER_OF(MOUSE_NAME);
        need = units * sizeof(WCHAR);
        break;
    case RIDI_DEVICEINFO:
        if (keyboard) {
            info.dwType = RIM_TYPEKEYBOARD;
            info.keyboard = (RID_DEVICE_INFO_KEYBOARD){4, 0, 1, 12, 3, 101};
        } else {
            info.dwType = RIM_TYPEMOUSE;
            info.mouse = (RID_DEVICE_INFO_MOUSE){256, 5, 0, TRUE};
        }
        data = &info;
        units = need = sizeof(info);
        if (user_data && size >= sizeof(info)) {
            DWORD cb;
            if (!NT_SUCCESS(MimeCopyFromUser(&cb, user_data, sizeof(cb)))) {
                W32SetLastError(ERROR_NOACCESS);
                return (UINT)-1;
            }
            if (cb != sizeof(info)) {
                W32SetLastError(ERROR_INVALID_PARAMETER);
                return (UINT)-1;
            }
        }
        break;
    case RIDI_PREPARSEDDATA:
        /* the keyboard and mouse are no hid devices to their readers. */
        units = need = 0;
        data = nullptr;
        break;
    default:
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return (UINT)-1;
    }
    if (!user_data || size < units) {
        MimeCopyToUser(user_size, &units, sizeof(units));
        if (!user_data)
            return 0;
        W32SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return (UINT)-1;
    }
    if (need && !NT_SUCCESS(MimeCopyToUser(user_data, data, need))) {
        W32SetLastError(ERROR_NOACCESS);
        return (UINT)-1;
    }
    return units;
}
