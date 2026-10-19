/*
 * small functions of kernelbase that the api sets of windows name and no other file holds: delay load failures,
 * quirks, the presence of an api set, procedure addresses for a caller and the fate of a process out of memory.
 */
#include "kernelbase_private.h"

#define DELAY_LOAD_FAILURE_EXCEPTION 0x505
#define STATUS_PROCESS_MEMORY_EXHAUSTED_ ((NTSTATUS)0xC00001AD)

NTSYSAPI NTSTATUS NTAPI RtlIsApiSetImplemented(PCSTR contract);

/*
 * what a delay load that failed gets: windows keeps a table of modules and the stand-in functions they get, so
 * that a program survives a missing optional dll. Mime keeps none, and the failure raises the exception that
 * the table's absence would, with the module and the procedure as its arguments.
 */
DECLSPEC_EXPORT PVOID WINAPI DelayLoadFailureHook(LPCSTR dll, LPCSTR procedure)
{
    ULONG_PTR arguments[2] = {(ULONG_PTR)dll, (ULONG_PTR)procedure};
    RaiseException(DELAY_LOAD_FAILURE_EXCEPTION, EXCEPTION_NONCONTINUABLE, 2, arguments);
    return nullptr;
}

/* no package says which quirks it wants, so none is enabled. */
BOOL WINAPI QuirkIsEnabled(UINT quirk)
{
    return FALSE;
}

HRESULT WINAPI QuirkIsEnabledForProcess(HANDLE process, UINT quirk, PBOOL enabled)
{
    return E_NOTIMPL;
}

BOOL WINAPI IsApiSetImplemented(PCSTR contract)
{
    return RtlIsApiSetImplemented(contract) >= 0;
}

/*
 * the address of an export, with the caller that asks. A null module is the program; a module handle with its
 * low bits set is a file mapped as data and has no exports to give.
 */
FARPROC WINAPI GetProcAddressForCaller(HMODULE module, LPCSTR name, PVOID caller)
{
    if (!module)
        module = (HMODULE)NtCurrentPeb()->ImageBaseAddress;
    else if ((ULONG_PTR)module & 3) {
        KbSetError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    return GetProcAddress(module, name);
}

/*
 * ends the process for want of memory, after reporting what the allocation asked for and what the process and its
 * job had: the fifth parameter tells a process that had its memory priority lowered. The values after the size
 * are a guess at what Windows' fields hold: the commit charge, the shared commit and the commit limit of the job.
 */
void WINAPI TerminateProcessOnMemoryExhaustion(SIZE_T failed_size)
{
    ULONG_PTR commit = 0, shared = 0, limit = 0;
    struct {
        SIZE_T Fields0[9];
        SIZE_T PagefileUsage;
        SIZE_T PeakPagefileUsage;
        SIZE_T PrivateUsage;
        SIZE_T PrivateWorkingSetSize;
        ULONGLONG SharedCommitUsage;
    } counters;
    ULONG returned;
    memset(&counters, 0, sizeof(counters));
    if (NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessVmCounters, &counters, sizeof(counters), &returned))) {
        commit = counters.PagefileUsage;
        shared = counters.SharedCommitUsage;
    }
    EXCEPTION_RECORD record;
    memset(&record, 0, sizeof(record));
    record.ExceptionCode = STATUS_PROCESS_MEMORY_EXHAUSTED_;
    record.ExceptionAddress = __builtin_return_address(0);
    record.NumberParameters = 5;
    record.ExceptionInformation[0] = failed_size;
    record.ExceptionInformation[1] = commit;
    record.ExceptionInformation[2] = shared;
    record.ExceptionInformation[3] = limit;
    record.ExceptionInformation[4] = 0;
    RaiseFailFastException(&record, nullptr, 0);
}

DWORD WINAPI GetTempPath2A(DWORD size, LPSTR buffer)
{
    return GetTempPathA(size, buffer);
}

BOOL WINAPI NeedCurrentDirectoryForExePathA(LPCSTR name)
{
    return strchr(name, '\\') != nullptr || GetEnvironmentVariableW(L"NoDefaultCurrentDirectoryInExePath", nullptr, 0) == 0;
}

/* the System32 of the machine the program runs on, or the directory of the 32-bit or 32-bit ARM files. */
UINT WINAPI GetSystemWow64Directory2W(LPWSTR buffer, UINT size, WORD machine)
{
    PCWSTR suffix;
    switch (machine) {
    case 1: /* IMAGE_FILE_MACHINE_TARGET_HOST */
        suffix = L"\\System32";
        break;
    case 0x14c:
        suffix = L"\\SysWOW64";
        break;
    case 0x1c4:
        suffix = L"\\SysArm32";
        break;
    default:
        KbSetError(ERROR_BAD_ARGUMENTS);
        return 0;
    }
    UINT root = GetSystemWindowsDirectoryW(nullptr, 0);
    if (!root)
        return 0;
    UINT needed = root + (UINT)wcslen(suffix);
    if (size < needed)
        return needed;
    UINT length = GetSystemWindowsDirectoryW(buffer, size);
    if (!length)
        return 0;
    wcscpy(buffer + length, suffix);
    return length + (UINT)wcslen(suffix);
}
