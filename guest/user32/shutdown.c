/* ExitWindowsEx and shutdown block reasons. the kernel does the work, playing csrss and winlogon. */
#include "user32_private.h"

/* what the kernel answers a thread that owns windows. */
#ifndef STATUS_CANT_WAIT
#define STATUS_CANT_WAIT ((NTSTATUS)0xC00000D8L)
#endif

typedef struct EXIT_REQUEST {
    UINT Flags;
    DWORD Reason;
} EXIT_REQUEST;

static BOOL ask_for_shutdown(UINT flags, DWORD reason, BOOL from_helper);

/* returns 0 on success, else the last error. */
static DWORD WINAPI helper_thread(LPVOID parameter)
{
    const EXIT_REQUEST *request = parameter;
    if (ask_for_shutdown(request->Flags, request->Reason, TRUE))
        return 0;
    return RtlGetLastWin32Error();
}

/* a thread with windows cannot wait for the shutdown, so another thread asks while this one pumps messages. */
static BOOL ask_from_helper(UINT flags, DWORD reason)
{
    EXIT_REQUEST request = {flags, reason};
    HANDLE thread = CreateThread(nullptr, 0, helper_thread, &request, 0, nullptr);
    if (!thread)
        return FALSE;
    for (;;) {
        DWORD woken = MsgWaitForMultipleObjectsEx(1, &thread, INFINITE, QS_ALLINPUT | QS_ALLPOSTMESSAGE | QS_SENDMESSAGE,
                                                  MWMO_INPUTAVAILABLE);
        if (woken == WAIT_OBJECT_0)
            break;
        if (woken == WAIT_FAILED) {
            WaitForSingleObject(thread, INFINITE);
            break;
        }
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    DWORD code = 0;
    GetExitCodeThread(thread, &code);
    CloseHandle(thread);
    if (code) {
        RtlSetLastWin32Error(code);
        return FALSE;
    }
    return TRUE;
}

static BOOL ask_for_shutdown(UINT flags, DWORD reason, BOOL from_helper)
{
    /* a job may forbid logoff, and the process is not told why. */
    if (!NtUserPrepareForLogoff())
        return FALSE;
    LONG status = NtUserMimeExitWindows(flags, reason);
    if (status >= 0)
        return TRUE;
    if (status == STATUS_CANT_WAIT && !from_helper)
        return ask_from_helper(flags, reason);
    if (status != STATUS_CANCELLED)
        RtlSetLastWin32Error(RtlNtStatusToDosError(status));
    return FALSE;
}

WINUSERAPI BOOL WINAPI ExitWindowsEx(UINT flags, DWORD reason)
{
    return ask_for_shutdown(flags, reason, FALSE);
}

/* there is no secure desktop to lock the session to. */
WINUSERAPI BOOL WINAPI LockWorkStation(void)
{
    RtlSetLastWin32Error(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/* ------------------------------------------------------------------ block reasons */

/*
 * the reason is required and at most 256 characters. for a window of another process the kernel answers 5,
 * which the call returns as is.
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
