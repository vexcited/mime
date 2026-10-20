/*
 * the window manager's part of a shutdown. csrss gives each process of a level to winsrv, which asks its gui
 * threads with a message nobody sends by hand, WM_CLIENTSHUTDOWN, whose default processing sends the windows of
 * the thread WM_QUERYENDSESSION and then WM_ENDSESSION. the flags of that message, the order of the threads and
 * the waits are those of winsrvext.dll and win32kfull.sys of build 26300; the dialogs windows shows for a program
 * that does not answer are not here, see docs/shutdown.md.
 */
#include "win32k.h"

#define MAX_THREADS 64
#define MAX_WINDOWS 128

/* how a thread answered. */
enum {
    NOTIFY_OK = 1,
    NOTIFY_REFUSED,   /* WM_QUERYENDSESSION returned FALSE. */
    NOTIFY_HUNG,      /* no answer in time. */
    NOTIFY_GONE,      /* the thread or its window ended. */
    NOTIFY_NOWINDOW,  /* nothing to ask. */
    NOTIFY_CANCELLED, /* the shutdown was aborted. */
};

/* the windows the shutdown asks, which are the top level windows of the thread that are not message only. */
static BOOLEAN askable(WND *c, W32THREAD *t)
{
    return c->Thread == t && !(c->Flags & WNDF_DESTROYING) && !(c->ExStyle & 0x80000000u);
}

/* IME windows and tool windows of the shell are not asked first; this is the filter winsrv's FindWindowFromThread has. */
static BOOLEAN thread_qualifies(W32THREAD *t)
{
    WND *desktop = WndDesktop();
    for (WND *c = desktop ? desktop->FirstChild : nullptr; c; c = c->Next)
        if (askable(c, t))
            return TRUE;
    return FALSE;
}

/*
 * the window a thread is asked through: the topmost one, then the visible owner of it and so on up, so that
 * the owner of a dialog hears first.
 */
static WND *shutdown_window(W32THREAD *t)
{
    WND *desktop = WndDesktop();
    WND *found = nullptr;
    for (WND *c = desktop ? desktop->FirstChild : nullptr; c; c = c->Next) {
        if (!askable(c, t) || (c->Class && !wcsncmp(c->Class->Name, L"IME", 3)))
            continue;
        found = c;
        break;
    }
    while (found && found->Owner && WndIsVisible(found->Owner) && askable(found->Owner, t))
        found = found->Owner;
    return found;
}

/* whether a window of the thread says it blocks the shutdown, with ShutdownBlockReasonCreate. */
static BOOLEAN thread_blocks(W32THREAD *t)
{
    WND *desktop = WndDesktop();
    for (WND *c = desktop ? desktop->FirstChild : nullptr; c; c = c->Next)
        if (askable(c, t) && c->ShutdownReason)
            return TRUE;
    return FALSE;
}

/*
 * the flags of WM_CLIENTSHUTDOWN for a request, from winsrvext's UserClientShutdown. shutting down is
 * 0x4, ending the user's programs 0x8, a program that must be asked again without a dialog 0x80, and whether
 * the session is really ending 0x100, which is the wparam of WM_ENDSESSION. the lparam of both messages
 * is the top bits and the lowest of them: logoff, forced, close for a restart.
 */
static ULONG client_flags(ULONG ewx, ULONG process_flags)
{
    ULONG f = MX_WMCS_ENDING;
    if ((process_flags & MX_SHUTDOWN_NORETRY) || (ewx & EWX_FORCE))
        f |= MX_WMCS_NORETRY;
    if (!(ewx & (MX_EWX_MACHINE | 0x2040580B)))
        f |= MX_WMCS_LOGOFF;
    if (ewx & EWX_FORCEIFHUNG)
        f |= MX_WMCS_FORCEIFHUNG;
    f |= MX_WMCS_USERPROCESS;
    if (ewx & EWX_RESTARTAPPS)
        f |= MX_WMCS_CLOSEAPP;
    if (ewx & EWX_FORCE)
        f |= MX_WMCS_CRITICAL;
    if (ewx & EWX_SHUTDOWN)
        f |= MX_WMCS_SHUTDOWN;
    if (ewx & EWX_REBOOT)
        f |= MX_WMCS_REBOOT;
    if (ewx & EWX_QUICKRESOLVE)
        f |= MX_WMCS_QUICK;
    return f;
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
 * asks one thread: WM_CLIENTSHUTDOWN to its window, the thread answering once its windows have. a thread that
 * has not looked at its queue for the hung time is not waited for, except one that has a window with a block
 * reason, which gets the wait to kill time instead.
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
            /* a forced shutdown does not take a no for an answer, but the windows are told it was asked for. */
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
        /* the session is not ending after all: the windows that were asked hear it. */
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
 * the default processing of WM_CLIENTSHUTDOWN, in the thread that is asked: the query goes to the windows of
 * the thread one at a time, from the top of the stack of windows, and stops at the first that refuses; the end of
 * the session goes to all of them, after which the timers of a window that was told the session ends are gone.
 */
LRESULT ClientShutdown(WND *w, ULONG flags)
{
    W32THREAD *t = w->Thread;
    WND *desktop = WndDesktop();
    HWND list[MAX_WINDOWS];
    ULONG n = 0;
    for (WND *c = desktop ? desktop->FirstChild : nullptr; c && n < MAX_WINDOWS; c = c->Next)
        if (askable(c, t))
            list[n++] = WndHandle(c);
    if (!n)
        return 1;
    LPARAM lparam = (LPARAM)(flags & (MX_WMCS_LOGOFF | MX_WMCS_CRITICAL | MX_WMCS_CLOSEAPP));
    BOOLEAN query = (flags & MX_WMCS_QUERY) != 0;
    for (ULONG i = 0; i < n; i++) {
        WND *c = WndLookup(list[i]);
        if (!c || c->Thread != t)
            continue;
        if (query) {
            if (!MsgSend(c, WM_QUERYENDSESSION, 0, lparam))
                return 3;
        } else {
            MsgSend(c, WM_ENDSESSION, (flags & MX_WMCS_ENDING) != 0, lparam);
            if ((flags & (MX_WMCS_ENDING | MX_WMCS_USERPROCESS)) == (MX_WMCS_ENDING | MX_WMCS_USERPROCESS) && (c = WndLookup(list[i])) != nullptr)
                TimerRemoveWindow(c);
        }
    }
    return query ? 1 : 2;
}

/* ------------------------------------------------------------------ block reasons */

static BOOLEAN own_window(WND *w)
{
    W32PROCESS *p = PsGetProcessWin32Process(PsGetCurrentProcess());
    return p && w->Thread && w->Thread->Process == p;
}

/*
 * ShutdownBlockReasonCreate. a window of another process is answered with 5 and nothing is done, which is
 * what win32kfull.sys returns there without setting an error, so the caller sees a nonzero result.
 */
ULONG NTAPI NtUserShutdownBlockReasonCreate(HWND hwnd, LPCWSTR reason, ULONG length)
{
    ULONG r = 0;
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (!w) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    } else if (!own_window(w)) {
        r = 5;
    } else if (w->Parent != WndDesktop() || length > 0x101) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
    } else {
        PWSTR copy = W32Alloc((length + 1) * sizeof(WCHAR));
        if (!copy) {
            W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        } else if (length && !NT_SUCCESS(MimeCopyFromUser(copy, reason, length * sizeof(WCHAR)))) {
            W32Free(copy);
            W32SetLastError(ERROR_INVALID_PARAMETER);
        } else {
            W32Free(w->ShutdownReason);
            w->ShutdownReason = copy;
            r = 1;
        }
    }
    UserLeave();
    return r;
}

ULONG NTAPI NtUserShutdownReasonDestroy(HWND hwnd)
{
    ULONG r = 0;
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (!w) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    } else if (!own_window(w)) {
        r = 5;
    } else if (!w->ShutdownReason) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
    } else {
        W32Free(w->ShutdownReason);
        w->ShutdownReason = nullptr;
        r = 1;
    }
    UserLeave();
    return r;
}

/*
 * ShutdownBlockReasonQuery of any window of any process. the buffer gets as much of the text as fits, the count
 * is always the length it needs, and a count of 0 with a buffer is an error.
 */
ULONG NTAPI NtUserShutdownBlockReasonQuery(HWND hwnd, LPWSTR buffer, PULONG count)
{
    ULONG have;
    if (!count || !NT_SUCCESS(MimeCopyFromUser(&have, count, sizeof(have)))) {
        W32SetLastError(ERROR_NOACCESS);
        return 0;
    }
    if (have > 0x101)
        have = 0x101;
    ULONG r = 0;
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (!w) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    } else if (!w->ShutdownReason) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
    } else if (buffer && have == 0) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
    } else {
        ULONG length = (ULONG)wcslen(w->ShutdownReason);
        BOOLEAN ok = TRUE;
        if (buffer) {
            ULONG copy = length < have ? length : have - 1;
            WCHAR *tmp = W32Alloc((copy + 1) * sizeof(WCHAR));
            if (tmp) {
                memcpy(tmp, w->ShutdownReason, copy * sizeof(WCHAR));
                ok = NT_SUCCESS(MimeCopyToUser(buffer, tmp, (copy + 1) * sizeof(WCHAR)));
                W32Free(tmp);
            } else {
                ok = FALSE;
            }
        }
        ULONG needed = length + 1;
        if (ok && NT_SUCCESS(MimeCopyToUser(count, &needed, sizeof(needed))))
            r = 1;
        else
            W32SetLastError(ERROR_NOACCESS);
    }
    UserLeave();
    return r;
}

/* ------------------------------------------------------------------ ExitWindowsEx */

/* JOB_OBJECT_UILIMIT_EXITWINDOWS: a process in a job with it cannot start a shutdown, and is told no without an error. */
#define UILIMIT_EXITWINDOWS W32_UILIMIT_EXITWINDOWS

ULONG NTAPI NtUserPrepareForLogoff(void)
{
    return (MimeProcessUiLimits(PsGetCurrentProcess()) & UILIMIT_EXITWINDOWS) ? 0 : 1;
}

/*
 * what ExitWindowsEx asks of the machine, once user32 has made its checks: the answer is the status
 * winsrv gives, STATUS_CANT_WAIT for a thread that has windows, which user32 answers by asking again from
 * another thread while this one serves its messages.
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
