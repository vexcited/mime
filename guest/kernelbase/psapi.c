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
} PROCESS_MEMORY_COUNTERS_EX;

typedef struct {
    SIZE_T PeakVirtualSize, VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage, PrivateUsage;
} VM_COUNTERS_EX;

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

DECLSPEC_EXPORT BOOL WINAPI K32GetProcessMemoryInfo(HANDLE process, PROCESS_MEMORY_COUNTERS_EX *info, DWORD size)
{
    SIZE_T minimum = __builtin_offsetof(PROCESS_MEMORY_COUNTERS_EX, PrivateUsage);
    if (!info || size < minimum) {
        SetLastError(ERROR_BAD_LENGTH);
        return FALSE;
    }
    VM_COUNTERS_EX counters;
    NTSTATUS status = NtQueryInformationProcess(process, (PROCESSINFOCLASS)3, &counters, sizeof(counters), nullptr);
    if (!NT_SUCCESS(status)) {
        SetLastError(RtlNtStatusToDosError(status));
        return FALSE;
    }
    memset(info, 0, size >= sizeof(*info) ? sizeof(*info) : minimum);
    info->cb = size >= sizeof(*info) ? sizeof(*info) : (DWORD)minimum;
    info->PageFaultCount = counters.PageFaultCount;
    memcpy(&info->PeakWorkingSetSize, &counters.PeakWorkingSetSize, 8 * sizeof(SIZE_T));
    if (size >= sizeof(*info))
        info->PrivateUsage = counters.PrivateUsage;
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
