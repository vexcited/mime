#include "kernelbase_private.h"

DECLSPEC_EXPORT BOOL WINAPI GetProcessGroupAffinity(HANDLE process, USHORT *count, USHORT *groups)
{
    if (!count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PROCESS_BASIC_INFORMATION info;
    NTSTATUS status = NtQueryInformationProcess(process, ProcessBasicInformation, &info, sizeof(info), nullptr);
    if (!NT_SUCCESS(status)) {
        SetLastError(RtlNtStatusToDosError(status));
        return FALSE;
    }
    if (!*count) {
        *count = 1;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!groups) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *groups = 0;
    *count = 1;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI GetNumaProcessorNodeEx(const PROCESSOR_NUMBER *processor, USHORT *node)
{
    if (!processor || !node || processor->Group || processor->Number >= USER_SHARED_DATA->ActiveProcessorCount) {
        if (node)
            *node = 0xffff;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *node = 0;
    return TRUE;
}

DECLSPEC_EXPORT PVOID WINAPI VirtualAllocExNuma(HANDLE process, PVOID address, SIZE_T size, DWORD type,
                                               DWORD protection, DWORD node)
{
    if (node != 0 && node != 0xffffffffu) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return VirtualAllocEx(process, address, size, type, protection);
}


/* the machine has memory to spare and is not short of it, so the high condition is set and the low one is not. */
DECLSPEC_EXPORT HANDLE WINAPI CreateMemoryResourceNotification(int kind)
{
    if (kind != 0 && kind != 1) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return CreateEventW(nullptr, TRUE, kind == 1, nullptr);
}
