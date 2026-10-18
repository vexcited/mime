/* psapi counters use the native query interfaces and propagate missing counters. */
#include "kernelbase_private.h"

typedef struct {
    DWORD cb;
    SIZE_T CommitTotal, CommitLimit, CommitPeak, PhysicalTotal, PhysicalAvailable;
    SIZE_T SystemCache, KernelTotal, KernelPaged, KernelNonpaged, PageSize;
    DWORD HandleCount, ProcessCount, ThreadCount;
} PERFORMANCE_INFORMATION;

typedef struct {
    DWORD cb, PageFaultCount;
    SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage, PrivateUsage;
    ULONGLONG PrivateWorkingSetSize, SharedCommitUsage;
} PROCESS_MEMORY_COUNTERS_EX2;

typedef struct {
    SIZE_T PeakVirtualSize, VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage, PrivateUsage;
    ULONGLONG PrivateWorkingSetSize, SharedCommitUsage;
} VM_COUNTERS_EX2;

DECLSPEC_EXPORT BOOL WINAPI K32GetPerformanceInfo(PERFORMANCE_INFORMATION *info, DWORD size)
{
    if (!info || size < sizeof(*info)) {
        SetLastError(ERROR_BAD_LENGTH);
        return FALSE;
    }
    SYSTEM_BASIC_INFORMATION basic;
    UCHAR perf[0x158];
    NTSTATUS status = NtQuerySystemInformation(SystemBasicInformation, &basic, sizeof(basic), nullptr);
    if (NT_SUCCESS(status))
        status = NtQuerySystemInformation(SystemPerformanceInformation, perf, sizeof(perf), nullptr);
    if (!NT_SUCCESS(status)) {
        SetLastError(RtlNtStatusToDosError(status));
        return FALSE;
    }
    memset(info, 0, sizeof(*info));
    info->cb = sizeof(*info);
    info->PageSize = basic.PageSize;
    info->PhysicalTotal = basic.NumberOfPhysicalPages;
    info->PhysicalAvailable = *(ULONG *)(perf + 0x4c);
    info->CommitTotal = *(ULONG *)(perf + 0x50);
    info->CommitLimit = *(ULONG *)(perf + 0x54);
    info->CommitPeak = *(ULONG *)(perf + 0x58);
    info->KernelPaged = *(ULONG *)(perf + 0x70);
    info->KernelNonpaged = *(ULONG *)(perf + 0x74);
    info->KernelTotal = info->KernelPaged + info->KernelNonpaged;
    return TRUE;
}

/* the counters in the plain size, with the private bytes, or with the private working set and shared commit too. */
DECLSPEC_EXPORT BOOL WINAPI K32GetProcessMemoryInfo(HANDLE process, PROCESS_MEMORY_COUNTERS_EX2 *info, DWORD size)
{
    SIZE_T minimum = __builtin_offsetof(PROCESS_MEMORY_COUNTERS_EX2, PrivateUsage);
    if (!info || size < minimum) {
        SetLastError(ERROR_BAD_LENGTH);
        return FALSE;
    }
    VM_COUNTERS_EX2 counters;
    NTSTATUS status = NtQueryInformationProcess(process, (PROCESSINFOCLASS)3, &counters, sizeof(counters), nullptr);
    if (!NT_SUCCESS(status)) {
        SetLastError(RtlNtStatusToDosError(status));
        return FALSE;
    }
    DWORD filled = (DWORD)minimum;
    if (size >= sizeof(*info))
        filled = sizeof(*info);
    else if (size >= minimum + sizeof(SIZE_T))
        filled = (DWORD)(minimum + sizeof(SIZE_T));
    memset(info, 0, filled);
    info->cb = filled;
    info->PageFaultCount = counters.PageFaultCount;
    memcpy(&info->PeakWorkingSetSize, &counters.PeakWorkingSetSize, 8 * sizeof(SIZE_T));
    if (filled > minimum)
        info->PrivateUsage = counters.PrivateUsage;
    if (filled == sizeof(*info)) {
        info->PrivateWorkingSetSize = counters.PrivateWorkingSetSize;
        info->SharedCommitUsage = counters.SharedCommitUsage;
    }
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI K32EnumProcesses(DWORD *ids, DWORD size, DWORD *needed)
{
    if (!needed || (size && !ids)) {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    ULONG length = 0x10000;
    PUCHAR buffer;
    NTSTATUS status;
    for (;;) {
        buffer = KbAlloc(length);
        if (!buffer) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        ULONG returned = 0;
        status = NtQuerySystemInformation(SystemProcessInformation, buffer, length, &returned);
        if (status != STATUS_INFO_LENGTH_MISMATCH)
            break;
        KbFree(buffer);
        length = returned > length ? returned + 0x1000 : length * 2;
    }
    if (!NT_SUCCESS(status)) {
        KbFree(buffer);
        KbSetStatus(status);
        return FALSE;
    }
    DWORD count = 0, capacity = size / sizeof(DWORD);
    for (SYSTEM_PROCESS_INFORMATION *info = (SYSTEM_PROCESS_INFORMATION *)buffer;;
         info = (SYSTEM_PROCESS_INFORMATION *)((PUCHAR)info + info->NextEntryOffset)) {
        if (count < capacity)
            ids[count++] = (DWORD)(ULONG_PTR)info->UniqueProcessId;
        if (!info->NextEntryOffset)
            break;
    }
    KbFree(buffer);
    *needed = count * sizeof(DWORD);
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI K32QueryWorkingSetEx(HANDLE process, PVOID info, DWORD size)
{
    NTSTATUS status = NtQueryVirtualMemory(process, nullptr, MemoryWorkingSetExInformation, info, size, nullptr);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

DECLSPEC_EXPORT BOOL WINAPI K32QueryWorkingSet(HANDLE process, PVOID info, DWORD size)
{
    NTSTATUS status = NtQueryVirtualMemory(process, nullptr, MemoryWorkingSetInformation, info, size, nullptr);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

/* the nt path of the file mapped at an address, cut to the buffer with ERROR_INSUFFICIENT_BUFFER. */
DECLSPEC_EXPORT DWORD WINAPI K32GetMappedFileNameW(HANDLE process, LPVOID address, LPWSTR name, DWORD size)
{
    if (!size) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    struct {
        UNICODE_STRING Name;
        WCHAR Buffer[MAX_PATH + 1];
    } info;
    SIZE_T returned = 0;
    NTSTATUS status = NtQueryVirtualMemory(process, address, MemoryMappedFilenameInformation, &info, sizeof(info),
                                           &returned);
    if (!NT_SUCCESS(status)) {
        SetLastError(RtlNtStatusToDosError(status));
        return 0;
    }
    DWORD n = info.Name.Length / sizeof(WCHAR), copy = n;
    if (size < n + 1)
        copy = size - 1;
    SetLastError(size < n + 1 ? ERROR_INSUFFICIENT_BUFFER : ERROR_SUCCESS);
    memcpy(name, info.Name.Buffer, copy * sizeof(WCHAR));
    name[copy] = 0;
    return size < n + 1 ? size : n;
}

DECLSPEC_EXPORT DWORD WINAPI K32GetMappedFileNameA(HANDLE process, LPVOID address, LPSTR name, DWORD size)
{
    WCHAR wide[MAX_PATH + 1];
    DWORD n = K32GetMappedFileNameW(process, address, wide, RTL_NUMBER_OF(wide));
    if (!n)
        return 0;
    if (!size) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    int bytes = WideCharToMultiByte(CP_ACP, 0, wide, (int)n, nullptr, 0, nullptr, nullptr);
    DWORD copy = (DWORD)bytes < size ? (DWORD)bytes : size - 1;
    WideCharToMultiByte(CP_ACP, 0, wide, (int)n, name, (int)copy, nullptr, nullptr);
    name[copy] = 0;
    SetLastError((DWORD)bytes < size ? ERROR_SUCCESS : ERROR_INSUFFICIENT_BUFFER);
    return (DWORD)bytes < size ? (DWORD)bytes : size;
}
