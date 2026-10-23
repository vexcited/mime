/*
 * keyboard and mouse input: a worker thread with a hidden window registered
 * for raw input, state shared by every keyboard and every mouse device of
 * the process, events buffered per acquired device, and sequence numbers
 * shared by every process. also the system wide table of exclusive
 * acquisitions and the hook that unacquires foreground devices when their
 * window loses activation.
 */
#include "dinput_private.h"

BYTE DiKeyState[256];
DIMOUSE DiMouseState;

static LIST_ENTRY g_acquired = {&g_acquired, &g_acquired};
static LONG g_keyboards = -1, g_mice = -1; /* acquired instances, -1 before the first. */
static LONG g_exclusive_keyboards, g_exclusive_mice;
HANDLE DiWorker;
static HWND g_window;
static BOOL g_pending_e1;

/* windows' default scan code translation: numlock and pause swapped, right shift as left. */
static BYTE translate(BYTE sc)
{
    switch (sc) {
    case 0x45:
        return 0xC5;
    case 0xC5:
        return 0x45;
    case 0xB6:
        return 0x36;
    default:
        return sc;
    }
}

/* ------------------------------------------------------------------ shared memory */

#define SHARED_SIZE 4096
#define EXCLUSIVE_AT 208
#define EXCLUSIVE_MAX 121

typedef struct EXCLUSIVE {
    GUID guid;
    HWND window;
    DWORD pid;
    DWORD flags;
} EXCLUSIVE;

static BYTE *g_shared;
static HANDLE g_mutex;
static LONG g_local_sequence;

static BOOL shared(void)
{
    if (g_shared)
        return TRUE;
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, SHARED_SIZE,
                                        L"DirectInput.{5944E681-C92E-11CF-BFC7-444553540000}");
    if (!section)
        return FALSE;
    BYTE *view = MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, SHARED_SIZE);
    CloseHandle(section);
    if (!view)
        return FALSE;
    g_mutex = CreateMutexW(nullptr, FALSE, L"DirectInput.{89521361-AA8A-11CF-BFC7-444553540000}");
    g_shared = view;
    return TRUE;
}

/* a sequence number per input report across every device and process, never 0. */
DWORD DiNextSequence(void)
{
    LONG *counter = shared() ? (LONG *)(g_shared + 4) : &g_local_sequence;
    LONG v = InterlockedIncrement(counter);
    if (!v)
        v = InterlockedIncrement(counter);
    return (DWORD)v;
}

/* the clock sequence of the guids dinput makes, shared like the sequence numbers. */
LONG DiNextUuidCounter(void)
{
    static LONG local;
    LONG *counter = shared() ? (LONG *)(g_shared + 8) : &local;
    return InterlockedIncrement(counter);
}

static EXCLUSIVE *find_exclusive(const GUID *guid)
{
    LONG count = *(LONG *)g_shared;
    EXCLUSIVE *e = (EXCLUSIVE *)(g_shared + EXCLUSIVE_AT);
    for (LONG i = 0; i < count; i++)
        if (IsEqualGUID(&e[i].guid, guid))
            return &e[i];
    return nullptr;
}

static DWORD window_pid(HWND window)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    return pid;
}

/*
 * the window must be ours. a record of the same instance is taken over
 * unless its owner is alive, foreground, and its window is not the
 * foreground one.
 */
BOOL DiExclusiveTake(DIDEV *dev, HRESULT *hr)
{
    if (window_pid(dev->window) != GetCurrentProcessId()) {
        *hr = (HRESULT)0x80070578;
        return FALSE;
    }
    if (!shared())
        return TRUE;
    WaitForSingleObject(g_mutex, INFINITE);
    EXCLUSIVE *e = find_exclusive(&dev->instance);
    *hr = DI_OK;
    if (e) {
        if (window_pid(e->window) == e->pid && (e->flags & DISCL_FOREGROUND) && e->window != GetForegroundWindow())
            *hr = DIERR_OTHERAPPHASPRIO;
    } else {
        LONG *count = (LONG *)g_shared;
        if (*count >= EXCLUSIVE_MAX)
            *hr = E_FAIL;
        else
            e = (EXCLUSIVE *)(g_shared + EXCLUSIVE_AT) + (*count)++;
    }
    if (SUCCEEDED(*hr))
        *e = (EXCLUSIVE){dev->instance, dev->window, GetCurrentProcessId(), dev->coop};
    ReleaseMutex(g_mutex);
    return SUCCEEDED(*hr);
}

void DiExclusiveRelease(DIDEV *dev)
{
    if (!g_shared)
        return;
    WaitForSingleObject(g_mutex, INFINITE);
    EXCLUSIVE *e = find_exclusive(&dev->instance);
    if (e && e->window == dev->window && e->pid == GetCurrentProcessId()) {
        LONG *count = (LONG *)g_shared;
        EXCLUSIVE *last = (EXCLUSIVE *)(g_shared + EXCLUSIVE_AT) + --*count;
        *e = *last;
        memset(last, 0, sizeof(*last));
    }
    ReleaseMutex(g_mutex);
}

/* ------------------------------------------------------------------ delivering input */

static BOOL is_keyboard(DIDEV *dev)
{
    return dev->cls->keyboard;
}

static void signal_class(BOOL keyboard)
{
    for (LIST_ENTRY *e = g_acquired.Flink; e != &g_acquired; e = e->Flink) {
        DIDEV *dev = CONTAINING_RECORD(e, DIDEV, links);
        if (is_keyboard(dev) == keyboard)
            DiSignal(dev);
    }
}

/* the scan code and its prefixes become a DIK code. */
static void keyboard_input(const RAWKEYBOARD *k)
{
    if (g_keyboards <= 0)
        return;
    DWORD sequence = DiNextSequence();
    BYTE sc = (BYTE)k->MakeCode;
    if (k->Flags & RI_KEY_E0) {
        /* the shift windows fakes around extended keys. */
        if (sc == 0x2A)
            return;
        sc |= 0x80;
        g_pending_e1 = FALSE;
    } else if (k->Flags & RI_KEY_E1) {
        g_pending_e1 = TRUE;
        if (sc == 0x1D)
            return;
        sc |= 0x80;
        g_pending_e1 = FALSE;
    } else if (g_pending_e1) {
        sc |= 0x80;
        g_pending_e1 = FALSE;
    }
    BYTE dik = sc == 0x45 || sc == 0xC5 ? sc : translate(sc);
    BYTE value = (k->Flags & RI_KEY_BREAK) ? 0x00 : 0x80;
    if (DiKeyState[dik] == value)
        return;
    DiKeyState[dik] = value;
    DWORD time = GetTickCount();
    for (LIST_ENTRY *e = g_acquired.Flink; e != &g_acquired; e = e->Flink) {
        DIDEV *dev = CONTAINING_RECORD(e, DIDEV, links);
        if (is_keyboard(dev))
            DiBufferEvent(dev, dik, value, time, sequence);
    }
    signal_class(TRUE);
}

static DIDEV *first_mouse(void)
{
    for (LIST_ENTRY *e = g_acquired.Flink; e != &g_acquired; e = e->Flink) {
        DIDEV *dev = CONTAINING_RECORD(e, DIDEV, links);
        if (!is_keyboard(dev))
            return dev;
    }
    return nullptr;
}

/* every mouse merged into one: axes then buttons, one sequence number per report. */
static void mouse_input(const RAWMOUSE *m)
{
    static RAWMOUSE previous;
    static BOOL have_previous;
    if (g_mice <= 0)
        return;
    LONG dx = m->lLastX, dy = m->lLastY;
    SHORT wheel = (m->usButtonFlags & RI_MOUSE_WHEEL) ? (SHORT)m->usButtonData : 0;
    if (m->usFlags & MOUSE_MOVE_ABSOLUTE) {
        /* tablets and remote sessions report absolute positions: take the difference from the last report. */
        RAWMOUSE prev = previous;
        BOOL had = have_previous;
        previous = *m;
        have_previous = TRUE;
        if (!had)
            return;
        dx -= prev.lLastX;
        dy -= prev.lLastY;
        if (m->usButtonFlags & RI_MOUSE_WHEEL)
            wheel = (SHORT)(m->usButtonData - prev.usButtonData);
    }
    DIMOUSE next = DiMouseState;
    next.x += dx;
    next.y += dy;
    DIDEV *any = first_mouse();
    BOOL has_z = any && any->axes >= 3;
    if ((m->usButtonFlags & RI_MOUSE_WHEEL) && has_z)
        next.z += wheel;
    static const USHORT DOWN[5] = {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_DOWN,
                                   RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_5_DOWN};
    for (int i = 0; i < 5; i++) {
        if (m->usButtonFlags & DOWN[i])
            next.buttons[i] = 0x80;
        else if (m->usButtonFlags & (DOWN[i] << 1))
            next.buttons[i] = 0x00;
    }
    DWORD sequence = DiNextSequence();
    DWORD time = GetTickCount();
    LONG old_axes[3] = {DiMouseState.x, DiMouseState.y, DiMouseState.z};
    LONG new_axes[3] = {next.x, next.y, next.z};
    BOOL changed = FALSE;
    for (int i = 0; i < 3; i++) {
        if (old_axes[i] == new_axes[i])
            continue;
        changed = TRUE;
        for (LIST_ENTRY *e = g_acquired.Flink; e != &g_acquired; e = e->Flink) {
            DIDEV *dev = CONTAINING_RECORD(e, DIDEV, links);
            if (!is_keyboard(dev))
                DiBufferEvent(dev, (DWORD)i * 4,
                              (DWORD)(dev->relative ? new_axes[i] - old_axes[i] : new_axes[i]), time, sequence);
        }
    }
    for (int i = 0; i < 8; i++) {
        if (DiMouseState.buttons[i] == next.buttons[i])
            continue;
        changed = TRUE;
        for (LIST_ENTRY *e = g_acquired.Flink; e != &g_acquired; e = e->Flink) {
            DIDEV *dev = CONTAINING_RECORD(e, DIDEV, links);
            if (!is_keyboard(dev))
                DiBufferEvent(dev, 12 + (DWORD)i, next.buttons[i], time, sequence);
        }
    }
    DiMouseState = next;
    if (changed)
        signal_class(FALSE);
}

static void raw_input(HRAWINPUT handle)
{
    union {
        RAWINPUT input;
        BYTE bytes[0x200];
    } buffer;
    UINT size = sizeof(buffer);
    if (GetRawInputData(handle, RID_INPUT, &buffer, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;
    RtlEnterCriticalSection(&DiLock);
    if (buffer.input.header.dwType == RIM_TYPEKEYBOARD)
        keyboard_input(&buffer.input.data.keyboard);
    else if (buffer.input.header.dwType == RIM_TYPEMOUSE)
        mouse_input(&buffer.input.data.mouse);
    RtlLeaveCriticalSection(&DiLock);
}

/* ------------------------------------------------------------------ the worker */

static DWORD WINAPI worker(void *ready)
{
    WNDCLASSW wc = {.lpfnWndProc = DefWindowProcW,
                    .hInstance = DiModule,
                    .hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW),
                    .hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION),
                    .lpszClassName = L"DIEmWin"};
    RegisterClassW(&wc);
    g_window = CreateWindowExW(0, L"DIEmWin", L"DIEmWin", WS_OVERLAPPEDWINDOW, -1, -1, 1, 1, nullptr, nullptr,
                               DiModule, nullptr);
    SetEvent(ready);
    for (;;) {
        while (MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_ALERTABLE) == WAIT_IO_COMPLETION)
            ;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_INPUT && msg.wParam <= RIM_INPUTSINK)
                raw_input((HRAWINPUT)msg.lParam);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}

/* started at the first acquisition, kept for the process's life. */
BOOL DiStartWorker(void)
{
    if (DiWorker)
        return g_window != nullptr;
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready)
        return FALSE;
    DiWorker = CreateThread(nullptr, 0, worker, ready, 0, nullptr);
    if (DiWorker) {
        SetThreadPriority(DiWorker, THREAD_PRIORITY_HIGHEST);
        HANDLE both[2] = {ready, DiWorker};
        WaitForMultipleObjects(2, both, FALSE, INFINITE);
    }
    CloseHandle(ready);
    return g_window != nullptr;
}

/*
 * like windows: an input sink when shared; no legacy messages, hot keys or
 * mouse capture when exclusive; only no hot keys for NOWINKEY. the latest
 * acquisition wins.
 */
static void register_device(BOOL keyboard, DWORD em)
{
    RAWINPUTDEVICE d = {1, (USHORT)(keyboard ? 6 : 2), RIDEV_INPUTSINK, g_window};
    if (em & DIEM_EXCLUSIVE)
        d.dwFlags = RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE;
    else if (keyboard && (em & DIEM_NOWINKEY))
        d.dwFlags = RIDEV_NOHOTKEYS;
    RegisterRawInputDevices(&d, 1, sizeof(d));
}

static void unregister_device(BOOL keyboard, LONG remaining)
{
    RAWINPUTDEVICE d = {1, (USHORT)(keyboard ? 6 : 2), RIDEV_REMOVE, nullptr};
    RegisterRawInputDevices(&d, 1, sizeof(d));
    if (remaining > 0)
        register_device(keyboard, 0);
}

/* the process's first mouse: buttons as they are, axes from an arbitrary origin, as windows does. */
static void seed_mouse(void)
{
    DiMouseState.buttons[0] = GetAsyncKeyState(VK_LBUTTON) < 0 ? 0x80 : 0;
    DiMouseState.buttons[1] = GetAsyncKeyState(VK_RBUTTON) < 0 ? 0x80 : 0;
    DiMouseState.buttons[2] = GetAsyncKeyState(VK_MBUTTON) < 0 ? 0x80 : 0;
    if (DiMouseState.x)
        return;
    DWORD tick = GetTickCount();
    LONG y = (LONG)((tick >> 16) | (tick << 16));
    LONG x = (LONG)((DWORD)(ULONG_PTR)&tick * (DWORD)y);
    while (!x || !y) {
        y = (LONG)GetTickCount() | 1;
        x = (LONG)((DWORD)(ULONG_PTR)&tick * (DWORD)y) | 1;
    }
    DiMouseState.x = x;
    DiMouseState.y = y;
}

HRESULT DiEmulAcquire(DIDEV *dev)
{
    RtlEnterCriticalSection(&DiLock);
    BOOL keyboard = is_keyboard(dev);
    LONG *count = keyboard ? &g_keyboards : &g_mice;
    if (*count <= 0) {
        if (keyboard)
            memset(DiKeyState, 0, sizeof(DiKeyState));
        else
            seed_mouse();
    }
    if (!DiStartWorker()) {
        RtlLeaveCriticalSection(&DiLock);
        return E_FAIL;
    }
    *count = *count < 0 ? 1 : *count + 1;
    if (dev->em & DIEM_EXCLUSIVE)
        ++*(keyboard ? &g_exclusive_keyboards : &g_exclusive_mice);
    InsertTailList(&g_acquired, &dev->links);
    register_device(keyboard, dev->em);
    RtlLeaveCriticalSection(&DiLock);
    return DI_OK;
}

static void forget_foreground(DIDEV *dev);

void DiEmulUnacquire(DIDEV *dev)
{
    RtlEnterCriticalSection(&DiLock);
    if (IsListEmpty(&dev->links)) {
        RtlLeaveCriticalSection(&DiLock);
        return;
    }
    BOOL keyboard = is_keyboard(dev);
    RemoveEntryList(&dev->links);
    InitializeListHead(&dev->links);
    LONG *count = keyboard ? &g_keyboards : &g_mice;
    --*count;
    if (dev->em & DIEM_EXCLUSIVE)
        --*(keyboard ? &g_exclusive_keyboards : &g_exclusive_mice);
    forget_foreground(dev);
    unregister_device(keyboard, *count);
    RtlLeaveCriticalSection(&DiLock);
}

/* ------------------------------------------------------------------ foreground devices */

#define MAX_FOREGROUND 64
#define MAX_WATCHED 16

static DIDEV *g_foreground[MAX_FOREGROUND];
static DWORD g_foreground_count;

/* one hook per thread owning a window that foreground devices cooperate with. */
static struct {
    DWORD tid;
    HHOOK hook;
    BOOL focus_lost;
} g_watched[MAX_WATCHED];

static void forget_foreground(DIDEV *dev)
{
    for (DWORD i = 0; i < g_foreground_count; i++)
        if (g_foreground[i] == dev) {
            g_foreground[i] = g_foreground[--g_foreground_count];
            return;
        }
}

/*
 * activation changing either way, or the window minimized after losing
 * focus, unacquires every foreground device of the process and removes
 * the hook.
 */
static LRESULT CALLBACK watch(int code, WPARAM wparam, LPARAM lparam)
{
    DWORD tid = GetCurrentThreadId();
    int slot = -1;
    RtlEnterCriticalSection(&DiLock);
    for (int i = 0; i < MAX_WATCHED; i++)
        if (g_watched[i].tid == tid)
            slot = i;
    HHOOK hook = slot >= 0 ? g_watched[slot].hook : nullptr;
    BOOL trigger = FALSE;
    if (slot >= 0 && code == HC_ACTION) {
        const CWPSTRUCT *cwp = (const CWPSTRUCT *)lparam;
        if (cwp->message == WM_KILLFOCUS)
            g_watched[slot].focus_lost = TRUE;
        else if (cwp->message == WM_SETFOCUS)
            g_watched[slot].focus_lost = FALSE;
        else if (cwp->message == WM_ACTIVATE ||
                 (cwp->message == WM_SIZE && cwp->wParam == SIZE_MINIMIZED && g_watched[slot].focus_lost))
            trigger = TRUE;
    }
    RtlLeaveCriticalSection(&DiLock);
    LRESULT r = CallNextHookEx(hook, code, wparam, lparam);
    if (!trigger)
        return r;
    DIDEV *lost[MAX_FOREGROUND];
    DWORD n = 0;
    RtlEnterCriticalSection(&DiLock);
    for (DWORD i = 0; i < g_foreground_count; i++) {
        lost[n] = g_foreground[i];
        InterlockedIncrement(&lost[n++]->ref);
    }
    g_foreground_count = 0;
    for (int i = 0; i < MAX_WATCHED; i++)
        if (g_watched[i].hook) {
            UnhookWindowsHookEx(g_watched[i].hook);
            g_watched[i].hook = nullptr;
            g_watched[i].tid = 0;
        }
    RtlLeaveCriticalSection(&DiLock);
    for (DWORD i = 0; i < n; i++) {
        DiDeviceLost(lost[i]);
        IUnknown *unknown = (IUnknown *)&lost[i]->vtbl_w;
        unknown->lpVtbl->Release(unknown);
    }
    return r;
}

void DiEmulForeground(DIDEV *dev)
{
    DWORD tid = GetWindowThreadProcessId(dev->window, nullptr);
    RtlEnterCriticalSection(&DiLock);
    if (g_foreground_count < MAX_FOREGROUND)
        g_foreground[g_foreground_count++] = dev;
    BOOL hooked = FALSE;
    int free_slot = -1;
    for (int i = 0; i < MAX_WATCHED; i++) {
        if (g_watched[i].hook && g_watched[i].tid == tid)
            hooked = TRUE;
        if (!g_watched[i].hook && free_slot < 0)
            free_slot = i;
    }
    if (!hooked && free_slot >= 0) {
        g_watched[free_slot].hook = SetWindowsHookExW(WH_CALLWNDPROC, watch, DiModule, tid);
        g_watched[free_slot].tid = g_watched[free_slot].hook ? tid : 0;
        g_watched[free_slot].focus_lost = FALSE;
    }
    RtlLeaveCriticalSection(&DiLock);
}
