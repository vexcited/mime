/* small api set functions with no better home: delay load failures, quirks, api set presence and so on. */
#include "kernelbase_private.h"

#define DELAY_LOAD_FAILURE_EXCEPTION 0x505
#define STATUS_PROCESS_MEMORY_EXHAUSTED_ ((NTSTATUS)0xC00001AD)

NTSYSAPI NTSTATUS NTAPI RtlIsApiSetImplemented(PCSTR contract);

/*
 * windows keeps a table of stub functions so a program survives a missing optional dll. mime has none, so this raises
 * the exception a missing table entry would, with the module and procedure as arguments.
 */
DECLSPEC_EXPORT PVOID WINAPI DelayLoadFailureHook(LPCSTR dll, LPCSTR procedure)
{
    ULONG_PTR arguments[2] = {(ULONG_PTR)dll, (ULONG_PTR)procedure};
    char text[200];
    if (!IsBadReadPtr(dll, 1) && !IsBadReadPtr(procedure, 1)) {
        _snprintf(text, sizeof(text), "kernelbase: the delay load of %s from %s failed\n", procedure, dll);
        OutputDebugStringA(text);
    }
    RaiseException(DELAY_LOAD_FAILURE_EXCEPTION, EXCEPTION_NONCONTINUABLE, 2, arguments);
    return nullptr;
}

/* no package asks for quirks, so none is enabled. */
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

/* a NULL module is the program; a handle with low bits set is a data mapped file and has no exports. */
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
 * raises a fail fast with the failed size and the process and job memory figures; the fifth parameter flags a
 * lowered memory priority. the values after the size are a guess: commit charge, shared commit and job commit limit.
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

/* System32 for the host machine, or the directory of the 32-bit or 32-bit ARM files. */
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
