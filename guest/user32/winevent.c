/*
 * win event hooks. a hook only hears events this process raises with
 * NotifyWinEvent, called at once on the raising thread like an in context
 * hook on windows; out of context hooks are served the same way. events of
 * other processes and system events are not delivered, there is no relay yet.
 */
#include "user32_private.h"

typedef void(CALLBACK *WINEVENTPROC)(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object, LONG child,
                                     DWORD thread, DWORD time);

#define WINEVENT_SKIPOWNTHREAD 0x1
#define WINEVENT_SKIPOWNPROCESS 0x2
#define WINEVENT_INCONTEXT 0x4
#define EVENT_MIN 0x00000001
#define EVENT_MAX 0x7FFFFFFF

typedef struct WIN_EVENT_HOOK {
    struct WIN_EVENT_HOOK *Next;
    ULONG_PTR Id;
    DWORD Min, Max;
    HMODULE Module;
    WINEVENTPROC Proc;
    DWORD Process, Thread, Flags;
    DWORD Owner;
} WIN_EVENT_HOOK;

static SRWLOCK g_hooks_lock = SRWLOCK_INIT;
static WIN_EVENT_HOOK *g_hooks;
static ULONG_PTR g_next_hook = 0x10000;

WINUSERAPI HWINEVENTHOOK WINAPI SetWinEventHook(DWORD min, DWORD max, HMODULE module, WINEVENTPROC proc,
                                                DWORD process, DWORD thread, DWORD flags)
{
    if (!proc || min > max || (flags & ~(WINEVENT_SKIPOWNTHREAD | WINEVENT_SKIPOWNPROCESS | WINEVENT_INCONTEXT)) ||
        ((flags & WINEVENT_INCONTEXT) && !module)) {
        SetLastError((flags & WINEVENT_INCONTEXT) && !module ? ERROR_HOOK_NEEDS_HMOD : ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    WIN_EVENT_HOOK *h = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h));
    if (!h) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    *h = (WIN_EVENT_HOOK){nullptr, 0, min, max, module, proc, process, thread, flags, GetCurrentThreadId()};
    AcquireSRWLockExclusive(&g_hooks_lock);
    h->Id = g_next_hook++;
    h->Next = g_hooks;
    g_hooks = h;
    ReleaseSRWLockExclusive(&g_hooks_lock);
    return (HWINEVENTHOOK)h->Id;
}

WINUSERAPI BOOL WINAPI UnhookWinEvent(HWINEVENTHOOK hook)
{
    WIN_EVENT_HOOK *found = nullptr;
    AcquireSRWLockExclusive(&g_hooks_lock);
    for (WIN_EVENT_HOOK **p = &g_hooks; *p; p = &(*p)->Next) {
        if ((*p)->Id == (ULONG_PTR)hook) {
            found = *p;
            *p = found->Next;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_hooks_lock);
    if (!found) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, found);
    return TRUE;
}

/* matching hooks are copied first, so a callback may unhook or hook again. */
WINUSERAPI void WINAPI NotifyWinEvent(DWORD event, HWND hwnd, LONG object, LONG child)
{
    if (!g_hooks)
        return;
    DWORD process = GetCurrentProcessId(), thread = GetCurrentThreadId(), time = GetTickCount();
    WIN_EVENT_HOOK matches[16];
    ULONG n = 0;
    AcquireSRWLockShared(&g_hooks_lock);
    for (WIN_EVENT_HOOK *h = g_hooks; h && n < RTL_NUMBER_OF(matches); h = h->Next) {
        if (event < h->Min || event > h->Max)
            continue;
        if ((h->Process && h->Process != process) || (h->Thread && h->Thread != thread))
            continue;
        if (h->Flags & WINEVENT_SKIPOWNPROCESS)
            continue;
        if ((h->Flags & WINEVENT_SKIPOWNTHREAD) && h->Owner == thread)
            continue;
        matches[n++] = *h;
    }
    ReleaseSRWLockShared(&g_hooks_lock);
    for (ULONG i = 0; i < n; i++)
        matches[i].Proc((HWINEVENTHOOK)matches[i].Id, event, hwnd, object, child, thread, time);
}

WINUSERAPI BOOL WINAPI IsWinEventHookInstalled(DWORD event)
{
    BOOL found = FALSE;
    AcquireSRWLockShared(&g_hooks_lock);
    for (WIN_EVENT_HOOK *h = g_hooks; h && !found; h = h->Next)
        found = event >= h->Min && event <= h->Max;
    ReleaseSRWLockShared(&g_hooks_lock);
    return found;
}
