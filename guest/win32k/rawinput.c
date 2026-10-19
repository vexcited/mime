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
#define MAX_PADS 16
#define MAX_HID_DATA 32
/* the room a packet takes as the caller reads it. */
#define MAX_PACKET (sizeof(RAWINPUTHEADER) + 8 + MAX_HID_DATA)

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
        struct {
            DWORD SizeHid, Count;
            BYTE Data[MAX_HID_DATA];
        } Hid;
    };
} PACKET;

/* a game controller's hid collection, a device of raw input. */
typedef struct HID_DEVICE {
    BOOLEAN Used;
    ULONG HostId;
    HANDLE Handle;
    MIME_PAD_HID Info;
} HID_DEVICE;

static HID_DEVICE g_hid[MAX_PADS];
static ULONG g_hid_handles;

static PACKET g_ring[RING];
static ULONG g_serial;

static RECT g_clip;
static BOOLEAN g_clipped;
static W32PROCESS *g_clip_owner;
static ULONG g_pointer = MIME_POINTER_FREE, g_pointer_window;
static RECT g_pointer_rect;

/* ------------------------------------------------------------------ the cursor */

/* all of the monitors. */
static RECT screen(void)
{
    return MonitorVirtualScreen();
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

/* what a registration for an exact usage hears of the devices present when it is made or dropped. */
static void notify_device(const RAWINPUTDEVICE *d, HWND target, BOOLEAN arrival)
{
    WND *w = WndLookup(target);
    if (!w)
        return;
    WPARAM change = arrival ? GIDC_ARRIVAL : GIDC_REMOVAL;
    if (usage_matches(d, 1, 6))
        QueuePost(w->Thread, target, WM_INPUT_DEVICE_CHANGE, change, (LPARAM)KEYBOARD_HANDLE);
    if (usage_matches(d, 1, 2))
        QueuePost(w->Thread, target, WM_INPUT_DEVICE_CHANGE, change, (LPARAM)MOUSE_HANDLE);
    for (ULONG i = 0; i < MAX_PADS; i++)
        if (g_hid[i].Used && d->usUsagePage == g_hid[i].Info.UsagePage && d->usUsage == g_hid[i].Info.Usage)
            QueuePost(w->Thread, target, WM_INPUT_DEVICE_CHANGE, change, (LPARAM)g_hid[i].Handle);
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
static BOOLEAN deliver_usage(PACKET *packet, USHORT page, USHORT usage)
{
    WND *fg = InputForeground;
    W32THREAD *fg_thread = fg ? fg->Thread : nullptr;
    W32PROCESS *fg_process = fg_thread ? fg_thread->Process : nullptr;
    BOOLEAN no_legacy = FALSE;
    BOOLEAN fg_registered = fg_process && registration(fg_process->Raw, page, usage);
    for (PLIST_ENTRY e = W32Processes.Flink; e != &W32Processes; e = e->Flink) {
        W32PROCESS *p = CONTAINING_RECORD(e, W32PROCESS, Links);
        const RAWINPUTDEVICE *d = registration(p->Raw, page, usage);
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

static BOOLEAN deliver(PACKET *packet, USHORT usage)
{
    return deliver_usage(packet, 1, usage);
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
    if (p->Type == RIM_TYPEHID)
        return 8 + p->Hid.SizeHid * p->Hid.Count;
    return p->Type == RIM_TYPEMOUSE ? sizeof(RAWMOUSE) : sizeof(RAWKEYBOARD);
}

/* a packet as the caller reads it: header, then its device's data. */
static UINT render(const PACKET *p, BYTE *out)
{
    RAWINPUTHEADER h = {p->Type, (DWORD)(sizeof(h) + body_size(p)), p->Device, p->Wparam};
    memcpy(out, &h, sizeof(h));
    if (p->Type == RIM_TYPEMOUSE)
        memcpy(out + sizeof(h), &p->Mouse, sizeof(p->Mouse));
    else if (p->Type == RIM_TYPEHID)
        memcpy(out + sizeof(h), &p->Hid, body_size(p));
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
    BYTE buffer[MAX_PACKET];
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
    BYTE buffer[MAX_PACKET];
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

static HID_DEVICE *hid_device(HANDLE handle)
{
    for (ULONG i = 0; i < MAX_PADS; i++)
        if (g_hid[i].Used && g_hid[i].Handle == handle)
            return &g_hid[i];
    return nullptr;
}

/* the keyboard and the mouse, then the hid collections in the order they came. */
UINT NTAPI NtUserGetRawInputDeviceList(PRAWINPUTDEVICELIST user_list, PUINT user_count, UINT size)
{
    RAWINPUTDEVICELIST devices[2 + MAX_PADS] = {{KEYBOARD_HANDLE, RIM_TYPEKEYBOARD}, {MOUSE_HANDLE, RIM_TYPEMOUSE}};
    UINT have = 2, count;
    if (size != sizeof(RAWINPUTDEVICELIST) || !user_count) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return (UINT)-1;
    }
    UserEnter();
    HID_DEVICE *order[MAX_PADS];
    UINT pads = 0;
    for (ULONG i = 0; i < MAX_PADS; i++)
        if (g_hid[i].Used)
            order[pads++] = &g_hid[i];
    /* handles grow with each arrival, which is the order windows lists them in. */
    for (UINT i = 1; i < pads; i++)
        for (UINT j = i; j && order[j - 1]->Handle > order[j]->Handle; j--) {
            HID_DEVICE *t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    for (UINT i = 0; i < pads; i++)
        devices[have++] = (RAWINPUTDEVICELIST){order[i]->Handle, RIM_TYPEHID};
    UserLeave();
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
    if (!NT_SUCCESS(MimeCopyToUser(user_list, devices, have * sizeof(*devices)))) {
        W32SetLastError(ERROR_NOACCESS);
        return (UINT)-1;
    }
    return have;
}

/* the name in characters, the rest in bytes; null data asks for the size. */
UINT NTAPI NtUserGetRawInputDeviceInfo(HANDLE device, UINT command, PVOID user_data, PUINT user_size)
{
    UINT size;
    if (!user_size || !NT_SUCCESS(MimeCopyFromUser(&size, user_size, sizeof(size)))) {
        W32SetLastError(ERROR_NOACCESS);
        return (UINT)-1;
    }
    UserEnter();
    HID_DEVICE *hid = hid_device(device);
    HID_DEVICE copy;
    if (hid)
        copy = *hid;
    UserLeave();
    if (device != KEYBOARD_HANDLE && device != MOUSE_HANDLE && !hid) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return (UINT)-1;
    }
    BOOLEAN keyboard = device == KEYBOARD_HANDLE;
    const void *data;
    UINT need, units;
    RID_DEVICE_INFO info = {sizeof(info)};
    switch (command) {
    case RIDI_DEVICENAME:
        if (hid) {
            data = copy.Info.Path;
            units = (UINT)wcslen(copy.Info.Path) + 1;
        } else {
            data = keyboard ? KEYBOARD_NAME : MOUSE_NAME;
            units = keyboard ? RTL_NUMBER_OF(KEYBOARD_NAME) : RTL_NUMBER_OF(MOUSE_NAME);
        }
        need = units * sizeof(WCHAR);
        break;
    case RIDI_DEVICEINFO:
        if (hid) {
            info.dwType = RIM_TYPEHID;
            info.hid = (RID_DEVICE_INFO_HID){copy.Info.VendorId, copy.Info.ProductId, copy.Info.Version,
                                             copy.Info.UsagePage, copy.Info.Usage};
        } else if (keyboard) {
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
        /* the bytes HidD_GetPreparsedData gives; the keyboard and mouse are no hid devices to their readers. */
        data = hid ? copy.Info.Preparsed : nullptr;
        units = need = hid ? copy.Info.PreparsedLength : 0;
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

/* ------------------------------------------------------------------ game controllers */

/* tells every process whose registration takes a collection's usage that it came or went. */
static void notify_all(const HID_DEVICE *d, BOOLEAN arrival)
{
    for (PLIST_ENTRY e = W32Processes.Flink; e != &W32Processes; e = e->Flink) {
        W32PROCESS *p = CONTAINING_RECORD(e, W32PROCESS, Links);
        const RAWINPUTDEVICE *r = registration(p->Raw, d->Info.UsagePage, d->Info.Usage);
        WND *w = r && (r->dwFlags & RIDEV_DEVNOTIFY) && r->hwndTarget ? WndLookup(r->hwndTarget) : nullptr;
        if (w)
            QueuePost(w->Thread, r->hwndTarget, WM_INPUT_DEVICE_CHANGE, arrival ? GIDC_ARRIVAL : GIDC_REMOVAL,
                      (LPARAM)d->Handle);
    }
}

/* a controller's collection comes and goes with it, and each new state is one report to its readers. */
void RawPadEvent(const MimeInputEvent *ev)
{
    HID_DEVICE *d = nullptr;
    for (ULONG i = 0; i < MAX_PADS && !d; i++)
        if (g_hid[i].Used && g_hid[i].HostId == ev->pad)
            d = &g_hid[i];
    switch (ev->type) {
    case MIME_INPUT_PAD_ADDED:
        for (ULONG i = 0; i < MAX_PADS && !d; i++)
            if (!g_hid[i].Used)
                d = &g_hid[i];
        if (!d || d->Used || !MimePadHidInfo(ev->pad, &d->Info))
            return;
        d->Used = TRUE;
        d->HostId = ev->pad;
        /* windows' user handles of devices, guessed. */
        d->Handle = (HANDLE)(ULONG_PTR)(0x10101 + 2 * g_hid_handles++);
        notify_all(d, TRUE);
        return;
    case MIME_INPUT_PAD_REMOVED:
        if (!d)
            return;
        notify_all(d, FALSE);
        d->Used = FALSE;
        return;
    case MIME_INPUT_PAD_STATE: {
        MIME_PAD_HID now;
        if (!d || !MimePadHidInfo(ev->pad, &now))
            return;
        PACKET packet = {};
        packet.Type = RIM_TYPEHID;
        packet.Device = d->Handle;
        packet.Hid.SizeHid = sizeof(now.Report);
        packet.Hid.Count = 1;
        memcpy(packet.Hid.Data, now.Report, sizeof(now.Report));
        deliver_usage(&packet, d->Info.UsagePage, d->Info.Usage);
        return;
    }
    }
}
