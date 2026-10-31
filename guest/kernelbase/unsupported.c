/*
 * calls that need parts of the system that are not here, and answer with the error that says so: process snapshots,
 * which copy the memory and handles of a process, mailslots, which need the mailslot file system, and the scatter and
 * gather file calls, which need the kernel to read into and write from page segments.
 */
#include "kernelbase_private.h"

typedef PVOID HPSS;

WINBASEAPI DWORD WINAPI PssCaptureSnapshot(HANDLE process, DWORD flags, DWORD context_flags, HPSS *snapshot)
{
    if (!snapshot)
        return ERROR_INVALID_PARAMETER;
    *snapshot = nullptr;
    if (!process)
        return ERROR_INVALID_HANDLE;
    return ERROR_NOT_SUPPORTED;
}

/* no snapshot is ever made, so none is handed back. */
WINBASEAPI DWORD WINAPI PssFreeSnapshot(HANDLE process, HPSS snapshot)
{
    return ERROR_INVALID_PARAMETER;
}

WINBASEAPI DWORD WINAPI PssQuerySnapshot(HPSS snapshot, DWORD information_class, PVOID buffer, DWORD length)
{
    return ERROR_INVALID_PARAMETER;
}

WINBASEAPI HANDLE WINAPI CreateMailslotW(LPCWSTR name, DWORD maximum_message, DWORD timeout,
                                         LPSECURITY_ATTRIBUTES attributes)
{
    if (!name) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    KbSetError(ERROR_NOT_SUPPORTED);
    return INVALID_HANDLE_VALUE;
}

WINBASEAPI HANDLE WINAPI CreateMailslotA(LPCSTR name, DWORD maximum_message, DWORD timeout,
                                         LPSECURITY_ATTRIBUTES attributes)
{
    if (!name) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    KbSetError(ERROR_NOT_SUPPORTED);
    return INVALID_HANDLE_VALUE;
}

/* a handle is a mailslot's when the mailslot file system made it, which it never did. */
WINBASEAPI BOOL WINAPI GetMailslotInfo(HANDLE mailslot, LPDWORD maximum_message, LPDWORD next_size, LPDWORD count,
                                       LPDWORD timeout)
{
    KbSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

WINBASEAPI BOOL WINAPI SetMailslotInfo(HANDLE mailslot, DWORD timeout)
{
    KbSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}
