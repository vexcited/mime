#include "wg_private.h"

#define WM_WG_RECHECK (WM_APP + 1)

typedef struct THREAD_HOOK {
    struct THREAD_HOOK *Next;
    DWORD Thread;
    HHOOK Hook;
    ULONG Users;
} THREAD_HOOK;

static SRWLOCK g_lock = SRWLOCK_INIT;
static WG_DISPLAY *g_watched;
static THREAD_HOOK *g_hooks;
static HWND g_window;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static void recheck_all(void)
{
    WG_DISPLAY *list[64];
    ULONG count = 0;
    AcquireSRWLockShared(&g_lock);
    for (WG_DISPLAY *d = g_watched; d && count < RTL_NUMBER_OF(list); d = d->NextWatched)
        if (WrtAgileTryAddRef(&d->Object))
            list[count++] = d;
    ReleaseSRWLockShared(&g_lock);
    for (ULONG i = 0; i < count; i++) {
        WgDisplayRefresh(list[i]);
        WrtAgileRelease(&list[i]->Object.Base.Ifaces[0]);
    }
}

static LRESULT CALLBACK watcher_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
    case WM_DPICHANGED:
    case WM_WG_RECHECK:
        recheck_all();
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static DWORD WINAPI watcher_thread(void *ready)
{
    WNDCLASSW class = {.lpfnWndProc = watcher_proc, .hInstance = WgModule, .lpszClassName = L"WgDisplayWatcher"};
    RegisterClassW(&class);
    g_window = CreateWindowExW(WS_EX_TOOLWINDOW, class.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                               WgModule, nullptr);
    SetEvent(ready);
    if (!g_window)
        return 1;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        DispatchMessageW(&msg);
    return 0;
}

static BOOL CALLBACK start_watcher(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready)
        return FALSE;
    HANDLE thread = CreateThread(nullptr, 0, watcher_thread, ready, 0, nullptr);
    if (thread) {
        WaitForSingleObject(ready, INFINITE);
        CloseHandle(thread);
    }
    CloseHandle(ready);
    return g_window != nullptr;
}

static BOOL watched_window(HWND window)
{
    BOOL found = FALSE;
    AcquireSRWLockShared(&g_lock);
    for (WG_DISPLAY *d = g_watched; d && !found; d = d->NextWatched)
        found = d->Window == window;
    ReleaseSRWLockShared(&g_lock);
    return found;
}

static LRESULT CALLBACK window_hook(int code, WPARAM wparam, LPARAM lparam)
{
    const CWPRETSTRUCT *call = (const CWPRETSTRUCT *)lparam;
    if (code == HC_ACTION && call &&
        (call->message == WM_WINDOWPOSCHANGED || call->message == WM_DPICHANGED || call->message == WM_MOVE) &&
        watched_window(call->hwnd))
        PostMessageW(g_window, WM_WG_RECHECK, 0, 0);
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

/*
 * Windows hears of display changes through notifications of the system and of moves of the window. here a hidden
 * window of a thread of its own hears the display, dpi and setting changes broadcast to top level windows, and a
 * hook on the thread of each watched window hears it move. either makes every watched display compare its state.
 */
HRESULT WgWatchStart(WG_DISPLAY *display)
{
    if (!InitOnceExecuteOnce(&g_once, start_watcher, nullptr, nullptr))
        return E_FAIL;
    AcquireSRWLockExclusive(&g_lock);
    HRESULT hr = S_OK;
    if (display->Window) {
        THREAD_HOOK *h = g_hooks;
        while (h && h->Thread != display->Thread)
            h = h->Next;
        if (!h && (h = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h)))) {
            h->Thread = display->Thread;
            h->Hook = SetWindowsHookExW(WH_CALLWNDPROCRET, window_hook, WgModule, display->Thread);
            if (h->Hook) {
                h->Next = g_hooks;
                g_hooks = h;
            } else {
                HeapFree(GetProcessHeap(), 0, h);
                h = nullptr;
            }
        }
        if (h)
            h->Users++;
        else
            hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) {
        display->NextWatched = g_watched;
        g_watched = display;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return hr;
}

void WgWatchStop(WG_DISPLAY *display)
{
    HHOOK unhook = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    WG_DISPLAY **link = &g_watched;
    while (*link && *link != display)
        link = &(*link)->NextWatched;
    if (*link) {
        *link = display->NextWatched;
        THREAD_HOOK **hook = &g_hooks;
        while (display->Window && *hook && (*hook)->Thread != display->Thread)
            hook = &(*hook)->Next;
        if (display->Window && *hook && !--(*hook)->Users) {
            THREAD_HOOK *h = *hook;
            *hook = h->Next;
            unhook = h->Hook;
            HeapFree(GetProcessHeap(), 0, h);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (unhook)
        UnhookWindowsHookEx(unhook);
}
