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

DECLSPEC_EXPORT BOOL WINAPI QueryWorkingSetEx(HANDLE process, PVOID info, DWORD size)
{
    return K32QueryWorkingSetEx(process, info, size);
}

DECLSPEC_EXPORT BOOL WINAPI GetProcessMemoryInfo(HANDLE process, PROCESS_MEMORY_COUNTERS_EX2 *info, DWORD size)
{
    return K32GetProcessMemoryInfo(process, info, size);
}

DECLSPEC_EXPORT BOOL WINAPI K32QueryWorkingSet(HANDLE process, PVOID info, DWORD size)
{
    NTSTATUS status = NtQueryVirtualMemory(process, nullptr, MemoryWorkingSetInformation, info, size, nullptr);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

DECLSPEC_EXPORT BOOL WINAPI QueryWorkingSet(HANDLE process, PVOID info, DWORD size)
{
    return K32QueryWorkingSet(process, info, size);
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

/* ------------------------------------------------------------------ device drivers */

/* the record of one module of SystemModuleInformation and the list it comes in. */
typedef struct {
    PVOID Section, MappedBase, ImageBase;
    ULONG ImageSize, Flags;
    USHORT LoadOrderIndex, InitOrderIndex, LoadCount, OffsetToFileName;
    UCHAR FullPathName[256];
} KB_MODULE;

typedef struct {
    ULONG NumberOfModules;
    KB_MODULE Modules[1];
} KB_MODULES;

/* the modules of the kernel, in a buffer that grows until they all fit; the buffer is the caller's to free. */
static KB_MODULES *query_modules(void)
{
    ULONG length = 1328;
    for (;;) {
        KB_MODULES *list = KbAlloc(length);
        if (!list) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        }
        ULONG returned = 0;
        NTSTATUS status = NtQuerySystemInformation(SystemModuleInformation, list, length, &returned);
        if (NT_SUCCESS(status))
            return list;
        ULONG need = offsetof(KB_MODULES, Modules) + list->NumberOfModules * sizeof(KB_MODULE);
        KbFree(list);
        /* a list that still does not fit once sized by its count is an error. */
        if (status != STATUS_INFO_LENGTH_MISMATCH || need <= length) {
            KbSetStatus(status);
            return nullptr;
        }
        length = need;
    }
}

/* the base address of every module, as many as the buffer takes, and the bytes all of them need. */
DECLSPEC_EXPORT BOOL WINAPI K32EnumDeviceDrivers(LPVOID *bases, DWORD size, LPDWORD needed)
{
    KB_MODULES *list = query_modules();
    if (!list)
        return FALSE;
    DWORD capacity = size / sizeof(PVOID);
    for (DWORD i = 0; i < list->NumberOfModules && i < capacity; i++)
        bases[i] = list->Modules[i].ImageBase;
    *needed = list->NumberOfModules * sizeof(PVOID);
    KbFree(list);
    return TRUE;
}

/* the module that was loaded at the address: ERROR_INVALID_HANDLE when there is none. */
static BOOL find_driver(LPVOID base, KB_MODULE *module)
{
    KB_MODULES *list = query_modules();
    if (!list)
        return FALSE;
    BOOL found = FALSE;
    for (DWORD i = 0; i < list->NumberOfModules; i++) {
        if (list->Modules[i].ImageBase == base) {
            *module = list->Modules[i];
            found = TRUE;
            break;
        }
    }
    KbFree(list);
    if (!found)
        SetLastError(ERROR_INVALID_HANDLE);
    return found;
}

/* the text copied with its terminator up to the size; a cut one gives the size, a whole one its length. */
static DWORD copy_driver_text(const UCHAR *text, LPSTR out, DWORD size)
{
    DWORD length = (DWORD)strlen((const char *)text) + 1, copy = length < size ? length : size;
    memcpy(out, text, copy);
    return copy == length ? copy - 1 : copy;
}

DECLSPEC_EXPORT DWORD WINAPI K32GetDeviceDriverFileNameA(LPVOID base, LPSTR name, DWORD size)
{
    KB_MODULE module;
    if (!find_driver(base, &module))
        return 0;
    return copy_driver_text(module.FullPathName, name, size);
}

DECLSPEC_EXPORT DWORD WINAPI K32GetDeviceDriverBaseNameA(LPVOID base, LPSTR name, DWORD size)
{
    KB_MODULE module;
    if (!find_driver(base, &module))
        return 0;
    return copy_driver_text(module.FullPathName + module.OffsetToFileName, name, size);
}

/* the wide forms ask for the narrow text and widen it. */
static DWORD widen_driver_text(DWORD narrow, const char *text, LPWSTR out, DWORD size)
{
    if (!narrow)
        return 0;
    int count = narrow >= size ? (int)narrow : (int)narrow + 1;
    return MultiByteToWideChar(CP_ACP, 0, text, count, out, (int)size) ? narrow : 0;
}

DECLSPEC_EXPORT DWORD WINAPI K32GetDeviceDriverFileNameW(LPVOID base, LPWSTR name, DWORD size)
{
    char *text = KbAlloc(size ? size : 1);
    if (!text)
        return 0;
    DWORD n = widen_driver_text(K32GetDeviceDriverFileNameA(base, text, size), text, name, size);
    KbFree(text);
    return n;
}

DECLSPEC_EXPORT DWORD WINAPI K32GetDeviceDriverBaseNameW(LPVOID base, LPWSTR name, DWORD size)
{
    char *text = KbAlloc(size ? size : 1);
    if (!text)
        return 0;
    DWORD n = widen_driver_text(K32GetDeviceDriverBaseNameA(base, text, size), text, name, size);
    KbFree(text);
    return n;
}

/* ------------------------------------------------------------------ page files */

typedef struct {
    DWORD cb, Reserved;
    SIZE_T TotalSize, TotalInUse, PeakUsage;
} ENUM_PAGE_FILE_INFORMATION;

typedef BOOL(CALLBACK *ENUM_PAGE_FILE_CALLBACKW)(LPVOID context, ENUM_PAGE_FILE_INFORMATION *info, LPCWSTR name);
typedef BOOL(CALLBACK *ENUM_PAGE_FILE_CALLBACKA)(LPVOID context, ENUM_PAGE_FILE_INFORMATION *info, LPCSTR name);

typedef struct {
    ULONG NextEntryOffset, TotalSize, TotalInUse, PeakUsage;
    UNICODE_STRING PageFileName;
} KB_PAGEFILE;

/* the callback gets each paging file once, with its name cut to the drive letter and what follows. */
DECLSPEC_EXPORT BOOL WINAPI K32EnumPageFilesW(ENUM_PAGE_FILE_CALLBACKW callback, LPVOID context)
{
    ULONG length = 4096, returned = 0;
    PUCHAR buffer;
    for (;;) {
        buffer = KbAlloc(length);
        if (!buffer) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        NTSTATUS status = NtQuerySystemInformation(SystemPageFileInformation, buffer, length, &returned);
        if (NT_SUCCESS(status))
            break;
        KbFree(buffer);
        if (status != STATUS_INFO_LENGTH_MISMATCH)
            return KbSetStatus(status);
        length = returned > length ? returned : length + 4096;
    }
    PUCHAR at = buffer;
    for (ULONG left = returned; left >= sizeof(KB_PAGEFILE);) {
        KB_PAGEFILE *file = (KB_PAGEFILE *)at;
        ENUM_PAGE_FILE_INFORMATION info = {sizeof(info), 0, file->TotalSize, file->TotalInUse, file->PeakUsage};
        WCHAR *colon = file->PageFileName.Buffer ? wcschr(file->PageFileName.Buffer, ':') : nullptr;
        if (colon && colon > file->PageFileName.Buffer)
            callback(context, &info, colon - 1);
        if (!file->NextEntryOffset || file->NextEntryOffset > left)
            break;
        left -= file->NextEntryOffset;
        at += file->NextEntryOffset;
    }
    KbFree(buffer);
    return TRUE;
}

typedef struct {
    LPVOID Context;
    ENUM_PAGE_FILE_CALLBACKA Callback;
    DWORD Error;
} PAGE_FILE_ANSI;

static BOOL CALLBACK page_file_ansi(LPVOID context, ENUM_PAGE_FILE_INFORMATION *info, LPCWSTR name)
{
    PAGE_FILE_ANSI *state = context;
    char text[MAX_PATH];
    if (!WideCharToMultiByte(CP_ACP, 0, name, -1, text, sizeof(text), nullptr, nullptr)) {
        state->Error = GetLastError();
        return FALSE;
    }
    return state->Callback(state->Context, info, text);
}

DECLSPEC_EXPORT BOOL WINAPI K32EnumPageFilesA(ENUM_PAGE_FILE_CALLBACKA callback, LPVOID context)
{
    PAGE_FILE_ANSI state = {context, callback, 0};
    BOOL ok = K32EnumPageFilesW(page_file_ansi, &state);
    if (ok && state.Error) {
        SetLastError(state.Error);
        return FALSE;
    }
    return ok;
}

/* ------------------------------------------------------------------ working set watch */

/*
 * the kernel does not record the pages a process faults in, so asking it to start or to report is refused with the
 * status the process information service gives for a class it does not know.
 */
DECLSPEC_EXPORT BOOL WINAPI K32InitializeProcessForWsWatch(HANDLE process)
{
    NTSTATUS status = NtSetInformationProcess(process, ProcessWorkingSetWatch, nullptr, 0);
    /* a process already watched is fine. */
    if (NT_SUCCESS(status) || status == STATUS_PORT_ALREADY_SET)
        return TRUE;
    return KbSetStatus(status);
}

DECLSPEC_EXPORT BOOL WINAPI K32GetWsChanges(HANDLE process, PVOID info, DWORD size)
{
    NTSTATUS status = NtQueryInformationProcess(process, ProcessWorkingSetWatch, info, size, nullptr);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

DECLSPEC_EXPORT BOOL WINAPI K32GetWsChangesEx(HANDLE process, PVOID info, PDWORD size)
{
    NTSTATUS status = NtQueryInformationProcess(process, ProcessWorkingSetWatchEx, info, *size, size);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}
