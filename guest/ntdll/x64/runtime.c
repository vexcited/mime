/*
 * the visual c++ runtime that a redistributable puts in System32 is arm64x, as on windows on arm, and only its arm64
 * half runs here: mime runs x64 programs with the x64 system dlls of SysX64. when such a runtime is installed and an x64
 * program cannot find a dll of it, features.exe takes the x64 runtime out of Microsoft's redistributable into SysX64,
 * once a process, and the loader looks again.
 */
#include "../ntdll_private.h"

#ifndef MIME_X64_NATIVE

#define CONSOLE_DETACHED ((HANDLE)(LONG_PTR)-1)
/* the names of mime/feature.h, which needs winbase.h. */
#define FEATURE_PROGRAM L"\\System32\\features.exe"
#define FEATURE_NAME L"VCRuntime.x64"
#define FEATURE_BUSY L"MIME_FEATURE_BUSY"

static const WCHAR *const g_runtime[] = {L"vcruntime140", L"msvcp140", L"concrt140", L"vccorlib140",
                                         L"vcamp140",     L"vcomp140", L"mfc140",    L"mfcm140"};

static BOOLEAN is_runtime(PCWSTR name)
{
    SIZE_T length = wcslen(name);
    if (length < 5 || _wcsicmp(name + length - 4, L".dll"))
        return FALSE;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_runtime); i++)
        if (!_wcsnicmp(name, g_runtime[i], wcslen(g_runtime[i])))
            return TRUE;
    return FALSE;
}

static BOOLEAN in_system32(PCWSTR name)
{
    WCHAR path[MAX_PATH];
    int n = _snwprintf(path, RTL_NUMBER_OF(path), L"\\??\\%ls\\System32\\%ls", USER_SHARED_DATA->NtSystemRoot, name);
    if (n <= 0 || n >= (int)RTL_NUMBER_OF(path))
        return FALSE;
    UNICODE_STRING nt;
    RtlInitUnicodeString(&nt, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    FILE_BASIC_INFORMATION info;
    return NT_SUCCESS(NtQueryAttributesFile(&oa, &info));
}

/* what features.exe sets for the programs it runs, which must not ask for what it is installing. */
static BOOLEAN busy(void)
{
    WCHAR value[4];
    UNICODE_STRING name = RTL_CONSTANT_STRING(FEATURE_BUSY);
    UNICODE_STRING text = {0, sizeof(value), value};
    NTSTATUS status = RtlQueryEnvironmentVariable_U(nullptr, &name, &text);
    return NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL;
}

static NTSTATUS run_features(void)
{
    PRTL_USER_PROCESS_PARAMETERS own = NtCurrentPeb()->ProcessParameters;
    PCWSTR caller = own->ImagePathName.Buffer + own->ImagePathName.Length / sizeof(WCHAR);
    while (caller > own->ImagePathName.Buffer && caller[-1] != L'\\')
        caller--;
    WCHAR image[MAX_PATH], line[MAX_PATH * 2];
    _snwprintf(image, RTL_NUMBER_OF(image), L"%ls" FEATURE_PROGRAM, USER_SHARED_DATA->NtSystemRoot);
    int n = _snwprintf(line, RTL_NUMBER_OF(line), L"\"%ls\" /enable-feature:" FEATURE_NAME L" /caller-pid:%lu /caller-name:",
                       image, (ULONG)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess);
    if (n <= 0)
        return STATUS_NAME_TOO_LONG;
    /* the name is one word, as kernelbase passes it. */
    for (; *caller && caller < own->ImagePathName.Buffer + own->ImagePathName.Length / sizeof(WCHAR) &&
           n < (int)RTL_NUMBER_OF(line) - 1;
         caller++)
        line[n++] = *caller == L' ' ? L'_' : *caller;
    line[n] = 0;
    RTL_USER_PROCESS_PARAMETERS pp = {};
    pp.MaximumLength = pp.Length = sizeof(pp);
    pp.Flags = RTL_USER_PROC_PARAMS_NORMALIZED;
    RtlInitUnicodeString(&pp.ImagePathName, image);
    RtlInitUnicodeString(&pp.CommandLine, line);
    pp.CurrentDirectory.DosPath = own->CurrentDirectory.DosPath;
    pp.Environment = own->Environment;
    pp.EnvironmentSize = own->EnvironmentSize;
    pp.ConsoleHandle = CONSOLE_DETACHED;
    HANDLE process, thread;
    NTSTATUS status = NtCreateUserProcess(&process, &thread, PROCESS_ALL_ACCESS, THREAD_ALL_ACCESS, nullptr, nullptr, 0, 0,
                                          &pp, nullptr, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    NtClose(thread);
    NtWaitForSingleObject(process, FALSE, nullptr);
    PROCESS_BASIC_INFORMATION info;
    status = NtQueryInformationProcess(process, ProcessBasicInformation, &info, sizeof(info), nullptr);
    NtClose(process);
    return NT_SUCCESS(status) ? info.ExitStatus : status;
}

BOOLEAN LdrpRequestX64Runtime(PCWSTR name)
{
    static LONG asked;
    if (!is_runtime(name) || !in_system32(name) || busy() || __atomic_exchange_n(&asked, 1, __ATOMIC_SEQ_CST))
        return FALSE;
    NTSTATUS status = run_features();
    if (status)
        DbgPrint("ntdll: the x64 visual c++ runtime for %ls did not arrive (%08x)\n", name, status);
    return status == STATUS_SUCCESS;
}

#endif
