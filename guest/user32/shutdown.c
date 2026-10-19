/*
 * ExitWindowsEx and the reasons a window gives for blocking a shutdown. the work is the kernel's, which plays
 * csrss and winlogon: see docs/shutdown.md for the order of things.
 */
#include "user32_private.h"

/* the status the kernel answers a thread that has windows of its own with. */
#ifndef STATUS_CANT_WAIT
#define STATUS_CANT_WAIT ((NTSTATUS)0xC00000D8L)
#endif

typedef struct EXIT_REQUEST {
    UINT Flags;
    DWORD Reason;
} EXIT_REQUEST;

static BOOL exit_windows_worker(UINT flags, DWORD reason, BOOL helper);

static DWORD WINAPI exit_windows_thread(LPVOID parameter)
{
    EXIT_REQUEST *request = parameter;
    BOOL ok = exit_windows_worker(request->Flags, request->Reason, TRUE);
    DWORD error = 0;
    if (!ok)
        error = GetLastError();
    ExitThread(error);
}

/*
 * a thread with windows cannot wait for the shutdown, which sends them messages: the work goes to a thread of
 * its own while this one serves its messages. that thread's last error is what comes back, so a shutdown the
 * windows refused, which sets none, answers TRUE here.
 */
static BOOL exit_windows_worker(UINT flags, DWORD reason, BOOL helper)
{
    if (!NtUserPrepareForLogoff())
        return FALSE;
    LONG status = NtUserMimeExitWindows(flags, reason);
    if (status >= 0)
        return TRUE;
    if (status != STATUS_CANT_WAIT || helper) {
        /* a shutdown that was cancelled leaves the last error as it was. */
        if (status != STATUS_CANCELLED)
            RtlSetLastWin32Error(RtlNtStatusToDosError(status));
        return FALSE;
    }
    EXIT_REQUEST request = {flags, reason};
    DWORD id;
    HANDLE thread = CreateThread(nullptr, 0, exit_windows_thread, &request, 0, &id);
    if (!thread)
        return FALSE;
    while (MsgWaitForMultipleObjectsEx(1, &thread, INFINITE, QS_ALLINPUT | QS_ALLPOSTMESSAGE | QS_SENDMESSAGE, 0) !=
           WAIT_OBJECT_0) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
    }
    DWORD code = ERROR_GEN_FAILURE;
    GetExitCodeThread(thread, &code);
    CloseHandle(thread);
    if (code) {
        SetLastError(code);
        return FALSE;
    }
    return TRUE;
}

WINUSERAPI BOOL WINAPI ExitWindowsEx(UINT flags, DWORD reason)
{
    return exit_windows_worker(flags, reason, FALSE);
}

/* ------------------------------------------------------------------ block reasons */

/*
 * the reason may be 256 characters at most and must be given. the kernel answers a window of another process
 * with 5, a result that is not zero and that the call hands on as it is.
 */
WINUSERAPI BOOL WINAPI ShutdownBlockReasonCreate(HWND hwnd, LPCWSTR reason)
{
    if (reason) {
        ULONG length = 0;
        while (length < 257 && reason[length])
            length++;
        if (length < 257)
            return (BOOL)NtUserShutdownBlockReasonCreate(hwnd, reason, length);
    }
    RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINUSERAPI BOOL WINAPI ShutdownBlockReasonDestroy(HWND hwnd)
{
    return (BOOL)NtUserShutdownReasonDestroy(hwnd);
}

WINUSERAPI BOOL WINAPI ShutdownBlockReasonQuery(HWND hwnd, LPWSTR buffer, DWORD *count)
{
    return (BOOL)NtUserShutdownBlockReasonQuery(hwnd, buffer, count);
}
