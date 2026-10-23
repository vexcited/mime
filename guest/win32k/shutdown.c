/*
 * winsrv asks GUI threads to shut down with WM_CLIENTSHUTDOWN. its default handler sends WM_QUERYENDSESSION and
 * WM_ENDSESSION. csrss groups processes by shutdown level; dialogs for unresponsive programs are not implemented.
 */
#include "win32k.h"

#define MAX_THREADS 64
#define MAX_WINDOWS 128

enum {
    NOTIFY_OK = 1,
    NOTIFY_REFUSED,   /* WM_QUERYENDSESSION returned FALSE. */
    NOTIFY_HUNG,      /* no answer in time. */
    NOTIFY_GONE,      /* the thread or its window ended. */
    NOTIFY_NOWINDOW,  /* nothing to ask. */
    NOTIFY_CANCELLED, /* the shutdown was aborted. */
};

/* which window is asked. */

#define MESSAGE_ONLY_STYLE 0x80000000u
#define IME_CLASS_PREFIX_CHARS 3

static BOOLEAN class_is_ime(const WND *w)
{
    static const WCHAR prefix[IME_CLASS_PREFIX_CHARS] = {L'I', L'M', L'E'};
    const WCHAR *name = w->Class ? w->Class->Name : nullptr;
    if (!name)
        return FALSE;
    for (ULONG i = 0; i < IME_CLASS_PREFIX_CHARS; i++)
        if (name[i] != prefix[i])
            return FALSE;
    return TRUE;
}

/* eligible windows are alive, receive ordinary messages and are not input method windows. */
static BOOLEAN askable(WND *w, W32THREAD *t)
{
    return w->Thread == t && !(w->Flags & WNDF_DESTROYING) && !(w->ExStyle & MESSAGE_ONLY_STYLE) && !class_is_ime(w);
}

/* use the thread's top window, or its owner if eligible. */
static WND *shutdown_window(W32THREAD *t)
{
    WND *desktop = WndDesktop();
    WND *chosen = desktop ? desktop->FirstChild : nullptr;
    while (chosen && !askable(chosen, t))
        chosen = chosen->Next;
    for (ULONG depth = 0; chosen && chosen->Owner && depth < 64; depth++) {
        WND *owner = chosen->Owner;
        if (!WndIsVisible(owner) || !askable(owner, t))
            break;
        chosen = owner;
    }
    return chosen;
}

#define EWX_MACHINE_BITS 0x2040580Bu
#define PROCESS_SHUTDOWN_NORETRY 0x1u

static ULONG client_flags(ULONG ewx, ULONG process_flags)
{
    static const struct {
        ULONG Request;
        ULONG Word;
    } carried[] = {
        {EWX_FORCEIFHUNG, MX_WMCS_FORCEIFHUNG}, {EWX_RESTARTAPPS, MX_WMCS_CLOSEAPP},
        {EWX_FORCE, MX_WMCS_CRITICAL},          {EWX_SHUTDOWN, MX_WMCS_SHUTDOWN},
        {EWX_REBOOT, MX_WMCS_REBOOT},           {EWX_QUICKRESOLVE, MX_WMCS_QUICK},
    };
    ULONG word = MX_WMCS_ENDING | MX_WMCS_USERPROCESS;
    for (ULONG i = 0; i < RTL_NUMBER_OF(carried); i++)
        if (ewx & carried[i].Request)
            word |= carried[i].Word;
    if ((process_flags & PROCESS_SHUTDOWN_NORETRY) || (ewx & EWX_FORCE))
        word |= MX_WMCS_NORETRY;
    if (!(ewx & EWX_MACHINE_BITS))
        word |= MX_WMCS_LOGOFF;
    return word;
}

/* do not ask IME or shell tool windows first. */
static BOOLEAN thread_qualifies(W32THREAD *t)
{
    WND *desktop = WndDesktop();
    for (WND *c = desktop ? desktop->FirstChild : nullptr; c; c = c->Next)
        if (askable(c, t))
            return TRUE;
    return FALSE;
}

static BOOLEAN thread_blocks(W32THREAD *t)
{
    WND *desktop = WndDesktop();
    for (WND *c = desktop ? desktop->FirstChild : nullptr; c; c = c->Next)
        if (askable(c, t) && c->ShutdownReason)
            return TRUE;
    return FALSE;
}

static ULONG64 ms_left(const MX_SHUTDOWN_CONTEXT *ctx, ULONG64 wanted)
{
    if (!ctx->Deadline)
        return wanted;
    ULONG64 now = KeQueryInterruptTime() / 10000;
    ULONG64 left = ctx->Deadline > now ? ctx->Deadline - now : 0;
    return left < wanted ? left : wanted;
}

/*
 * send WM_CLIENTSHUTDOWN to one thread and wait for its windows to answer. skip hung threads unless a window has a
 * shutdown block reason, in which case use the wait-to-kill timeout.
 */
static ULONG notify_thread(HANDLE tid, ULONG flags, const MX_SHUTDOWN_CONTEXT *ctx, BOOLEAN wait)
{
    UserEnter();
    W32THREAD *t = W32LookupThread(tid);
    WND *target = t ? shutdown_window(t) : nullptr;
    HWND hwnd = target ? WndHandle(target) : nullptr;
    BOOLEAN blocks = t && thread_blocks(t);
    BOOLEAN hung = t && MsgThreadHung(t);
    UserLeave();
    if (!t)
        return NOTIFY_GONE;
    if (!hwnd)
        return NOTIFY_NOWINDOW;
    if (hung && !blocks)
        return NOTIFY_HUNG;

    ULONG64 timeout = ctx->HungTimeout;
    if (flags & MX_WMCS_QUICK)
        timeout = (flags & MX_WMCS_QUERY) ? ctx->QuickTimeout / 2 : ctx->QuickTimeout;
    else if (blocks)
        timeout = ctx->KillTimeout;
    timeout = ms_left(ctx, timeout);
    PKEVENT events[2] = {ctx->Cancel, ctx->Expire};
    LRESULT result = 0;
    ULONG r = MsgSendSystem(tid, hwnd, MX_WM_CLIENTSHUTDOWN, flags, 0, wait ? timeout : 0, events, 2, &result);
    switch (r) {
    case MSGSYS_OK:
        return result == 3 ? NOTIFY_REFUSED : NOTIFY_OK;
    case MSGSYS_GONE:
        return NOTIFY_GONE;
    case MSGSYS_EVENT0:
        return NOTIFY_CANCELLED;
    default:
        return NOTIFY_HUNG;
    }
}

ULONG NTAPI W32ShutdownProcess(PEPROCESS process, const MX_SHUTDOWN_CONTEXT *ctx)
{
    HANDLE tids[MAX_THREADS];
    ULONG count = 0;
    UserEnter();
    W32PROCESS *wp = PsGetProcessWin32Process(process);
    if (wp) {
        HANDLE all[MAX_THREADS];
        ULONG n = W32ProcessThreads(wp, all, MAX_THREADS);
        for (ULONG i = 0; i < n; i++) {
            W32THREAD *t = W32LookupThread(all[i]);
            if (t && thread_qualifies(t))
                tids[count++] = all[i];
        }
    }
    UserLeave();
    if (!count)
        return MX_SHUTDOWN_PROCESS_PASS;

    ULONG flags = client_flags(ctx->Flags, MimeProcessShutdownFlags(process));
    ULONG asked = 0;
    BOOLEAN refused = FALSE, hung = FALSE, cancelled = FALSE;
    for (ULONG i = 0; i < count && !refused && !hung && !cancelled; i++) {
        ULONG r = notify_thread(tids[i], flags | MX_WMCS_QUERY, ctx, TRUE);
        switch (r) {
        case NOTIFY_OK:
            asked = i + 1;
            break;
        case NOTIFY_REFUSED:
            asked = i + 1;
            /* forced shutdown ignores refusal but still notifies windows. */
            refused = !(ctx->Flags & EWX_FORCE);
            break;
        case NOTIFY_HUNG:
            hung = TRUE;
            break;
        case NOTIFY_CANCELLED:
            cancelled = TRUE;
            break;
        }
    }
    if (hung)
        return MX_SHUTDOWN_PROCESS_DONE;
    if (refused || cancelled) {
        /* notify queried windows that shutdown was canceled. */
        for (ULONG i = 0; i < asked; i++)
            notify_thread(tids[i], flags & ~(ULONG)MX_WMCS_ENDING, ctx, FALSE);
        return MX_SHUTDOWN_PROCESS_CANCEL;
    }
    for (ULONG i = 0; i < count; i++)
        if (notify_thread(tids[i], flags, ctx, TRUE) == NOTIFY_HUNG)
            break;
    return MX_SHUTDOWN_PROCESS_DONE;
}

/*
 * WM_CLIENTSHUTDOWN runs in the target thread. send WM_QUERYENDSESSION from top to bottom, including hidden windows,
 * until one refuses; send WM_ENDSESSION to all.
 */
#define CLIENT_QUERIED 1
#define CLIENT_ENDED 2
#define CLIENT_REFUSED 3

LRESULT ClientShutdown(WND *w, ULONG flags)
{
    W32THREAD *t = w->Thread;
    WND *desktop = WndDesktop();
    ULONG capacity = 0, count = 0;
    for (WND *c = desktop ? desktop->FirstChild : nullptr; c; c = c->Next)
        capacity++;
    HWND *windows = capacity ? W32Alloc(capacity * sizeof(HWND)) : nullptr;
    for (WND *c = windows ? desktop->FirstChild : nullptr; c && count < capacity; c = c->Next)
        if (c->Thread == t && !(c->Flags & WNDF_DESTROYING) && !(c->ExStyle & MESSAGE_ONLY_STYLE))
            windows[count++] = WndHandle(c);
    if (!count) {
        W32Free(windows);
        return CLIENT_QUERIED;
    }

    LPARAM reasons = flags & (MX_WMCS_LOGOFF | MX_WMCS_CRITICAL | MX_WMCS_CLOSEAPP);
    LRESULT result = (flags & MX_WMCS_QUERY) ? CLIENT_QUERIED : CLIENT_ENDED;
    for (ULONG i = 0; i < count && result != CLIENT_REFUSED; i++) {
        WND *target = WndLookup(windows[i]);
        if (!target || target->Thread != t)
            continue;
        if (flags & MX_WMCS_QUERY) {
            if (!MsgSend(target, WM_QUERYENDSESSION, 0, reasons))
                result = CLIENT_REFUSED;
            continue;
        }
        MsgSend(target, WM_ENDSESSION, (flags & MX_WMCS_ENDING) ? 1 : 0, reasons);
        /* do not schedule timers for a terminating process; the window may already be gone. */
        target = WndLookup(windows[i]);
        if (target && (flags & MX_WMCS_ENDING) && (flags & MX_WMCS_USERPROCESS))
            TimerRemoveWindow(target);
    }
    W32Free(windows);
    return result;
}

/* block reasons. */

/* a window of another process answers with 5 and no error: the caller sees a result that looks like success. */
#define BLOCK_FOREIGN_RESULT 5
#define MAX_BLOCK_REASON_CHARS 0x101

static WND *reason_window(HWND hwnd, BOOLEAN own_process_only, ULONG *result)
{
    WND *w = WndLookup(hwnd);
    *result = 0;
    if (!w) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    if (own_process_only && (!w->Thread || w->Thread->Process != W32GetProcess())) {
        *result = BLOCK_FOREIGN_RESULT;
        return nullptr;
    }
    return w;
}

ULONG NTAPI NtUserShutdownBlockReasonCreate(HWND hwnd, LPCWSTR reason, ULONG length)
{
    UserEnter();
    ULONG result;
    WND *w = reason_window(hwnd, TRUE, &result);
    if (w) {
        PWSTR copy = nullptr;
        ULONG error = 0;
        if (!WndIsTopLevel(w) || length > MAX_BLOCK_REASON_CHARS) {
            error = ERROR_INVALID_PARAMETER;
        } else if (!(copy = W32Alloc((length + 1) * sizeof(WCHAR)))) {
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else if (length && !NT_SUCCESS(MimeCopyFromUser(copy, reason, length * sizeof(WCHAR)))) {
            error = ERROR_INVALID_PARAMETER;
            W32Free(copy);
        }
        if (error) {
            W32SetLastError(error);
        } else {
            W32Free(w->ShutdownReason);
            w->ShutdownReason = copy;
            result = 1;
        }
    }
    UserLeave();
    return result;
}

ULONG NTAPI NtUserShutdownReasonDestroy(HWND hwnd)
{
    UserEnter();
    ULONG result;
    WND *w = reason_window(hwnd, TRUE, &result);
    if (w && !w->ShutdownReason) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
    } else if (w) {
        W32Free(w->ShutdownReason);
        w->ShutdownReason = nullptr;
        result = 1;
    }
    UserLeave();
    return result;
}

/* read any process's window reason up to the buffer size, always returning the required size. */
ULONG NTAPI NtUserShutdownBlockReasonQuery(HWND hwnd, LPWSTR buffer, PULONG count)
{
    UserEnter();
    ULONG result;
    WND *w = reason_window(hwnd, FALSE, &result);
    ULONG room = 0;
    if (w && !w->ShutdownReason) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        w = nullptr;
    }
    if (w && (!count || !NT_SUCCESS(MimeCopyFromUser(&room, count, sizeof(room))))) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        w = nullptr;
    }
    if (w) {
        if (room > MAX_BLOCK_REASON_CHARS)
            room = MAX_BLOCK_REASON_CHARS;
        ULONG length = (ULONG)wcslen(w->ShutdownReason);
        ULONG needed = length + 1;
        if (buffer && !room) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
        } else {
            ULONG copied = buffer ? (length < room - 1 ? length : room - 1) : 0;
            WCHAR end = 0;
            BOOLEAN ok = !buffer || (NT_SUCCESS(MimeCopyToUser(buffer, w->ShutdownReason, copied * sizeof(WCHAR))) &&
                                     NT_SUCCESS(MimeCopyToUser(buffer + copied, &end, sizeof(end))));
            if (ok && NT_SUCCESS(MimeCopyToUser(count, &needed, sizeof(needed))))
                result = 1;
            else
                W32SetLastError(ERROR_INVALID_PARAMETER);
        }
    }
    UserLeave();
    return result;
}

/* ExitWindowsEx. */

/* JOB_OBJECT_UILIMIT_EXITWINDOWS denies shutdown without setting an error. */
#define UILIMIT_EXITWINDOWS W32_UILIMIT_EXITWINDOWS

ULONG NTAPI NtUserPrepareForLogoff(void)
{
    return (MimeProcessUiLimits(PsGetCurrentProcess()) & UILIMIT_EXITWINDOWS) ? 0 : 1;
}

/*
 * return the winsrv status after user32 validation. STATUS_CANT_WAIT means the caller owns windows; user32 retries on
 * another thread while this one pumps messages.
 */
LONG NTAPI NtUserMimeExitWindows(ULONG flags, ULONG reason)
{
    if (flags & (MX_EWX_MACHINE) && !MimeCurrentHasPrivilege(19))
        return STATUS_PRIVILEGE_NOT_HELD;
    UserEnter();
    W32THREAD *t = W32GetThread();
    BOOLEAN windows = t && WndThreadHasWindows(t);
    UserLeave();
    if (windows)
        return STATUS_CANT_WAIT;
    return MimeShutdownStart(flags, reason, 0, 0, 0, FALSE, TRUE);
}
