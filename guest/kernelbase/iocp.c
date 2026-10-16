/*
 * i/o completion ports over the kernel's IoCompletion objects.
 */
#include "kernelbase_private.h"

typedef struct KB_FILE_COMPLETION_INFORMATION {
    HANDLE Port;
    PVOID Key;
} KB_FILE_COMPLETION_INFORMATION;

typedef struct KB_FILE_IO_COMPLETION_INFORMATION {
    PVOID KeyContext;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock;
} KB_FILE_IO_COMPLETION_INFORMATION;

WINBASEAPI HANDLE WINAPI CreateIoCompletionPort(HANDLE file, HANDLE existing, ULONG_PTR key, DWORD concurrency)
{
    HANDLE port = existing;
    if (!existing) {
        NTSTATUS status = NtCreateIoCompletion(&port, STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 3, nullptr, concurrency);
        if (!NT_SUCCESS(status)) {
            KbSetStatus(status);
            return nullptr;
        }
    }
    if (file != INVALID_HANDLE_VALUE) {
        KB_FILE_COMPLETION_INFORMATION info = {port, (PVOID)key};
        IO_STATUS_BLOCK iosb;
        NTSTATUS status = NtSetInformationFile(file, &iosb, &info, sizeof(info), FileCompletionInformation);
        if (!NT_SUCCESS(status)) {
            if (!existing)
                NtClose(port);
            KbSetStatus(status);
            return nullptr;
        }
    }
    return port;
}

WINBASEAPI BOOL WINAPI GetQueuedCompletionStatus(HANDLE port, LPDWORD bytes, PULONG_PTR key, LPOVERLAPPED *overlapped,
                                      DWORD milliseconds)
{
    if (!bytes || !key || !overlapped) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LARGE_INTEGER t;
    PVOID k, ctx;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtRemoveIoCompletion(port, &k, &ctx, &iosb, KbTimeout(&t, milliseconds));
    if (status != STATUS_SUCCESS) {
        *overlapped = nullptr;
        if (status == STATUS_TIMEOUT)
            KbSetError(WAIT_TIMEOUT);
        else
            KbSetStatus(status);
        return FALSE;
    }
    *key = (ULONG_PTR)k;
    *overlapped = ctx;
    *bytes = (DWORD)iosb.Information;
    if (!NT_SUCCESS(iosb.Status))
        return KbSetStatus(iosb.Status);
    return TRUE;
}

WINBASEAPI BOOL WINAPI GetQueuedCompletionStatusEx(HANDLE port, LPOVERLAPPED_ENTRY entries, ULONG count, PULONG removed,
                                        DWORD milliseconds, BOOL alertable)
{
    if (!entries || !count || !removed) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LARGE_INTEGER t;
    /* OVERLAPPED_ENTRY and the kernel's packet share one layout. */
    NTSTATUS status = NtRemoveIoCompletionEx(port, entries, count, removed, KbTimeout(&t, milliseconds), (BOOLEAN)alertable);
    if (status != STATUS_SUCCESS) {
        *removed = 0;
        if (status == STATUS_TIMEOUT)
            KbSetError(WAIT_TIMEOUT);
        else if (status == STATUS_USER_APC)
            KbSetError(WAIT_IO_COMPLETION);
        else
            KbSetStatus(status);
        return FALSE;
    }
    for (ULONG i = 0; i < *removed; i++) {
        KB_FILE_IO_COMPLETION_INFORMATION *p = (KB_FILE_IO_COMPLETION_INFORMATION *)&entries[i];
        ULONG_PTR information = p->IoStatusBlock.Information;
        entries[i].Internal = (ULONG_PTR)(LONG_PTR)p->IoStatusBlock.Status;
        entries[i].dwNumberOfBytesTransferred = (DWORD)information;
    }
    return TRUE;
}

WINBASEAPI BOOL WINAPI PostQueuedCompletionStatus(HANDLE port, DWORD bytes, ULONG_PTR key, LPOVERLAPPED overlapped)
{
    NTSTATUS status = NtSetIoCompletion(port, (PVOID)key, overlapped, STATUS_SUCCESS, bytes);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}
