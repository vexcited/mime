/*
 * processes and threads.
 */
#include "kernelbase_private.h"

#define THREAD_CREATE_FLAGS_CREATE_SUSPENDED 0x00000001
#define DEFAULT_STACK_RESERVE (1024 * 1024)

/* ------------------------------------------------------------------ identity */

HANDLE WINAPI GetCurrentProcess(void)
{
    return NtCurrentProcess();
}

HANDLE WINAPI GetCurrentThread(void)
{
    return NtCurrentThread();
}

DWORD WINAPI GetCurrentProcessId(void)
{
    return (DWORD)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;
}

DWORD WINAPI GetCurrentThreadId(void)
{
    return (DWORD)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread;
}

static BOOL query_process(HANDLE process, PROCESS_BASIC_INFORMATION *info)
{
    NTSTATUS status = NtQueryInformationProcess(process, ProcessBasicInformation, info, sizeof(*info), nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

static BOOL query_thread(HANDLE thread, THREAD_BASIC_INFORMATION *info)
{
    NTSTATUS status = NtQueryInformationThread(thread, ThreadBasicInformation, info, sizeof(*info), nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

DWORD WINAPI GetProcessId(HANDLE process)
{
    PROCESS_BASIC_INFORMATION info;
    if (!query_process(process, &info))
        return 0;
    return (DWORD)info.UniqueProcessId;
}

DWORD WINAPI GetThreadId(HANDLE thread)
{
    THREAD_BASIC_INFORMATION info;
    if (!query_thread(thread, &info))
        return 0;
    return (DWORD)(ULONG_PTR)info.ClientId.UniqueThread;
}

DWORD WINAPI GetProcessIdOfThread(HANDLE thread)
{
    THREAD_BASIC_INFORMATION info;
    if (!query_thread(thread, &info))
        return 0;
    return (DWORD)(ULONG_PTR)info.ClientId.UniqueProcess;
}

BOOL WINAPI GetExitCodeProcess(HANDLE process, LPDWORD code)
{
    PROCESS_BASIC_INFORMATION info;
    if (!query_process(process, &info))
        return FALSE;
    if (code)
        *code = (DWORD)info.ExitStatus;
    return TRUE;
}

BOOL WINAPI GetExitCodeThread(HANDLE thread, LPDWORD code)
{
    THREAD_BASIC_INFORMATION info;
    if (!query_thread(thread, &info))
        return FALSE;
    if (code)
        *code = (DWORD)info.ExitStatus;
    return TRUE;
}

HANDLE WINAPI OpenProcess(DWORD access, BOOL inherit, DWORD id)
{
    CLIENT_ID cid = {(HANDLE)(ULONG_PTR)id, nullptr};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, nullptr, inherit ? OBJ_INHERIT : 0, nullptr, nullptr);
    HANDLE handle;
    NTSTATUS status = NtOpenProcess(&handle, access, &oa, &cid);
    if (!NT_SUCCESS(status)) {
        KbSetError(status == STATUS_INVALID_CID ? ERROR_INVALID_PARAMETER : RtlNtStatusToDosError(status));
        return nullptr;
    }
    return handle;
}

/* the kernel has no NtOpenThread yet: only the calling thread can be opened. */
HANDLE WINAPI OpenThread(DWORD access, BOOL inherit, DWORD id)
{
    if (id != GetCurrentThreadId()) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    HANDLE handle;
    NTSTATUS status = NtDuplicateObject(NtCurrentProcess(), NtCurrentThread(), NtCurrentProcess(), &handle, access,
                                        inherit ? OBJ_INHERIT : 0, 0);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return nullptr;
    }
    return handle;
}

/* ------------------------------------------------------------------ termination */

void WINAPI ExitProcess(UINT code)
{
    RtlExitUserProcess((NTSTATUS)code);
}

BOOL WINAPI TerminateProcess(HANDLE process, UINT code)
{
    if (!process) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    NTSTATUS status = NtTerminateProcess(process, (NTSTATUS)code);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

void WINAPI ExitThread(DWORD code)
{
    RtlExitUserThread((NTSTATUS)code);
}

BOOL WINAPI TerminateThread(HANDLE thread, DWORD code)
{
    if (!thread) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    NTSTATUS status = NtTerminateThread(thread, (NTSTATUS)code);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

/* ------------------------------------------------------------------ thread creation */

HANDLE WINAPI CreateRemoteThreadEx(HANDLE process, LPSECURITY_ATTRIBUTES sa, SIZE_T stack,
                                   LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD flags,
                                   LPPROC_THREAD_ATTRIBUTE_LIST attributes, LPDWORD id)
{
    if (!start) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    SIZE_T commit = 0, reserve = 0;
    if (flags & STACK_SIZE_PARAM_IS_A_RESERVATION) {
        reserve = stack;
    } else {
        commit = stack;
        if (commit > DEFAULT_STACK_RESERVE)
            reserve = (commit + DEFAULT_STACK_RESERVE - 1) & ~(SIZE_T)(DEFAULT_STACK_RESERVE - 1);
    }
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, nullptr, (sa && sa->bInheritHandle) ? OBJ_INHERIT : 0, nullptr,
                               sa ? sa->lpSecurityDescriptor : nullptr);
    HANDLE handle;
    NTSTATUS status = NtCreateThreadEx(&handle, THREAD_ALL_ACCESS, &oa, process, (PVOID)start, parameter,
                                       (flags & CREATE_SUSPENDED) ? THREAD_CREATE_FLAGS_CREATE_SUSPENDED : 0, 0, commit,
                                       reserve, nullptr);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return nullptr;
    }
    if (id) {
        THREAD_BASIC_INFORMATION info;
        *id = NT_SUCCESS(NtQueryInformationThread(handle, ThreadBasicInformation, &info, sizeof(info), nullptr))
                  ? (DWORD)(ULONG_PTR)info.ClientId.UniqueThread
                  : 0;
    }
    return handle;
}

HANDLE WINAPI CreateRemoteThread(HANDLE process, LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE start,
                                 LPVOID parameter, DWORD flags, LPDWORD id)
{
    return CreateRemoteThreadEx(process, sa, stack, start, parameter, flags, nullptr, id);
}

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID parameter,
                           DWORD flags, LPDWORD id)
{
    return CreateRemoteThreadEx(NtCurrentProcess(), sa, stack, start, parameter, flags, nullptr, id);
}

DWORD WINAPI SuspendThread(HANDLE thread)
{
    ULONG previous = 0;
    NTSTATUS status = NtSuspendThread(thread, &previous);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return (DWORD)-1;
    }
    return previous;
}

DWORD WINAPI ResumeThread(HANDLE thread)
{
    ULONG previous = 0;
    NTSTATUS status = NtResumeThread(thread, &previous);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return (DWORD)-1;
    }
    return previous;
}

/* ------------------------------------------------------------------ waiting and yielding */

DWORD WINAPI SleepEx(DWORD milliseconds, BOOL alertable)
{
    LARGE_INTEGER interval;
    if (milliseconds == INFINITE)
        interval.QuadPart = (LONGLONG)0x8000000000000000ull;
    else
        interval.QuadPart = -(LONGLONG)milliseconds * 10000;
    if (alertable && KbRunPendingApcs())
        return WAIT_IO_COMPLETION;
    /* the kernel shows whether any thread waits for a processor: a yield to nobody stays in user mode. */
    if (!milliseconds && !alertable && !USER_SHARED_DATA->MimeReadyThreads)
        return 0;
    NTSTATUS status;
    do {
        status = NtDelayExecution((BOOLEAN)alertable, &interval);
    } while (status == STATUS_ALERTED && alertable && milliseconds == INFINITE);
    return status == STATUS_USER_APC || status == STATUS_ALERTED ? WAIT_IO_COMPLETION : 0;
}

void WINAPI Sleep(DWORD milliseconds)
{
    SleepEx(milliseconds, FALSE);
}

BOOL WINAPI SwitchToThread(void)
{
    if (!USER_SHARED_DATA->MimeReadyThreads)
        return FALSE;
    return NtYieldExecution() != STATUS_NO_YIELD_PERFORMED;
}

/* ------------------------------------------------------------------ priorities */

int WINAPI GetThreadPriority(HANDLE thread)
{
    THREAD_BASIC_INFORMATION info;
    if (!query_thread(thread, &info))
        return THREAD_PRIORITY_ERROR_RETURN;
    LONG base = info.BasePriority;
    if (base >= 15)
        return THREAD_PRIORITY_TIME_CRITICAL;
    if (base <= 1)
        return THREAD_PRIORITY_IDLE;
    LONG delta = base - 8;
    if (delta > THREAD_PRIORITY_HIGHEST)
        delta = THREAD_PRIORITY_HIGHEST;
    if (delta < THREAD_PRIORITY_LOWEST)
        delta = THREAD_PRIORITY_LOWEST;
    return (int)delta;
}

BOOL WINAPI SetThreadPriority(HANDLE thread, int priority)
{
    if (priority == THREAD_MODE_BACKGROUND_BEGIN || priority == THREAD_MODE_BACKGROUND_END)
        return TRUE;
    NTSTATUS status;
    if (priority == THREAD_PRIORITY_TIME_CRITICAL || priority == THREAD_PRIORITY_IDLE) {
        LONG absolute = priority == THREAD_PRIORITY_TIME_CRITICAL ? 15 : 1;
        status = NtSetInformationThread(thread, ThreadPriority, &absolute, sizeof(absolute));
    } else if (priority >= THREAD_PRIORITY_LOWEST && priority <= THREAD_PRIORITY_HIGHEST) {
        LONG increment = priority;
        status = NtSetInformationThread(thread, ThreadBasePriority, &increment, sizeof(increment));
    } else {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI GetThreadPriorityBoost(HANDLE thread, PBOOL disabled)
{
    if (!disabled) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *disabled = FALSE;
    return TRUE;
}

BOOL WINAPI SetThreadPriorityBoost(HANDLE thread, BOOL disable)
{
    return TRUE;
}

static DWORD g_priority_class = NORMAL_PRIORITY_CLASS;

DWORD WINAPI GetPriorityClass(HANDLE process)
{
    PROCESS_BASIC_INFORMATION info;
    if (!query_process(process, &info))
        return 0;
    return info.UniqueProcessId == GetCurrentProcessId() ? g_priority_class : NORMAL_PRIORITY_CLASS;
}

BOOL WINAPI SetPriorityClass(HANDLE process, DWORD klass)
{
    switch (klass) {
    case IDLE_PRIORITY_CLASS:
    case BELOW_NORMAL_PRIORITY_CLASS:
    case NORMAL_PRIORITY_CLASS:
    case ABOVE_NORMAL_PRIORITY_CLASS:
    case HIGH_PRIORITY_CLASS:
    case REALTIME_PRIORITY_CLASS:
        g_priority_class = klass;
        return TRUE;
    case PROCESS_MODE_BACKGROUND_BEGIN:
    case PROCESS_MODE_BACKGROUND_END:
        return TRUE;
    default:
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
}

BOOL WINAPI GetProcessPriorityBoost(HANDLE process, PBOOL disabled)
{
    if (!disabled) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *disabled = FALSE;
    return TRUE;
}

BOOL WINAPI SetProcessPriorityBoost(HANDLE process, BOOL disable)
{
    return TRUE;
}

/* ------------------------------------------------------------------ affinity */

static DWORD_PTR system_affinity(void)
{
    ULONG count = USER_SHARED_DATA->ActiveProcessorCount;
    if (!count)
        count = 1;
    return count >= 64 ? ~(DWORD_PTR)0 : (((DWORD_PTR)1 << count) - 1);
}

BOOL WINAPI GetProcessAffinityMask(HANDLE process, PDWORD_PTR process_mask, PDWORD_PTR system_mask)
{
    PROCESS_BASIC_INFORMATION info;
    if (!query_process(process, &info))
        return FALSE;
    if (process_mask)
        *process_mask = system_affinity();
    if (system_mask)
        *system_mask = system_affinity();
    return TRUE;
}

BOOL WINAPI SetProcessAffinityMask(HANDLE process, DWORD_PTR mask)
{
    if (!mask || (mask & ~system_affinity())) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

DWORD_PTR WINAPI SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask)
{
    if (!mask || (mask & ~system_affinity())) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    THREAD_BASIC_INFORMATION info;
    if (!query_thread(thread, &info))
        return 0;
    return system_affinity();
}

DWORD WINAPI SetThreadIdealProcessor(HANDLE thread, DWORD processor)
{
    if (processor != 64 /* MAXIMUM_PROCESSORS. */ && processor >= USER_SHARED_DATA->ActiveProcessorCount) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return (DWORD)-1;
    }
    return 0;
}

BOOL WINAPI SetThreadIdealProcessorEx(HANDLE thread, PPROCESSOR_NUMBER ideal, PPROCESSOR_NUMBER previous)
{
    if (!ideal || ideal->Group || ideal->Number >= USER_SHARED_DATA->ActiveProcessorCount) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (previous)
        memset(previous, 0, sizeof(*previous));
    return TRUE;
}

BOOL WINAPI GetThreadIdealProcessorEx(HANDLE thread, PPROCESSOR_NUMBER ideal)
{
    if (!ideal) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(ideal, 0, sizeof(*ideal));
    return TRUE;
}

BOOL WINAPI GetThreadGroupAffinity(HANDLE thread, PGROUP_AFFINITY affinity)
{
    if (!affinity) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(affinity, 0, sizeof(*affinity));
    affinity->Mask = system_affinity();
    return TRUE;
}

BOOL WINAPI SetThreadGroupAffinity(HANDLE thread, const GROUP_AFFINITY *affinity, PGROUP_AFFINITY previous)
{
    if (!affinity || affinity->Group || !affinity->Mask || (affinity->Mask & ~system_affinity())) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (previous) {
        memset(previous, 0, sizeof(*previous));
        previous->Mask = system_affinity();
    }
    return TRUE;
}

/* the kernel keeps the processor number in TPIDRRO_EL0, as windows on arm64 does. */
DWORD WINAPI GetCurrentProcessorNumber(void)
{
#ifdef __aarch64__
    ULONG64 v;
    __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(v));
    return (DWORD)v;
#else
    return NtGetCurrentProcessorNumber();
#endif
}

void WINAPI GetCurrentProcessorNumberEx(PPROCESSOR_NUMBER number)
{
    number->Group = 0;
    number->Number = (BYTE)GetCurrentProcessorNumber();
    number->Reserved = 0;
}

/* ------------------------------------------------------------------ startup information and command line */

static LPSTR g_command_line_a;
static STARTUPINFOA g_startup_a;
static LONGLONG g_process_start_time;

void KbInitializeCommandLine(void)
{
    PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
    if (pp->CommandLine.Buffer) {
        int n = WideCharToMultiByte(CP_ACP, 0, pp->CommandLine.Buffer, pp->CommandLine.Length / sizeof(WCHAR), nullptr, 0,
                                    nullptr, nullptr);
        g_command_line_a = KbAlloc((SIZE_T)n + 1);
        if (g_command_line_a) {
            if (n)
                WideCharToMultiByte(CP_ACP, 0, pp->CommandLine.Buffer, pp->CommandLine.Length / sizeof(WCHAR),
                                    g_command_line_a, n, nullptr, nullptr);
            g_command_line_a[n] = 0;
        }
    }
    LARGE_INTEGER now;
    NtQuerySystemTime(&now);
    g_process_start_time = now.QuadPart;
}

LPWSTR WINAPI GetCommandLineW(void)
{
    return KbParams()->CommandLine.Buffer;
}

LPSTR WINAPI GetCommandLineA(void)
{
    static char empty[1];
    return g_command_line_a ? g_command_line_a : empty;
}

void WINAPI GetStartupInfoW(LPSTARTUPINFOW info)
{
    PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
    memset(info, 0, sizeof(*info));
    info->cb = sizeof(*info);
    info->lpDesktop = pp->DesktopInfo.Buffer;
    info->lpTitle = pp->WindowTitle.Buffer;
    info->dwX = pp->StartingX;
    info->dwY = pp->StartingY;
    info->dwXSize = pp->CountX;
    info->dwYSize = pp->CountY;
    info->dwXCountChars = pp->CountCharsX;
    info->dwYCountChars = pp->CountCharsY;
    info->dwFillAttribute = pp->FillAttribute;
    info->dwFlags = pp->WindowFlags;
    info->wShowWindow = (WORD)pp->ShowWindowFlags;
    info->cbReserved2 = pp->RuntimeData.Length;
    info->lpReserved2 = pp->RuntimeData.Length ? (LPBYTE)pp->RuntimeData.Buffer : nullptr;
    if (info->dwFlags & STARTF_USESTDHANDLES) {
        info->hStdInput = pp->StandardInput;
        info->hStdOutput = pp->StandardOutput;
        info->hStdError = pp->StandardError;
    }
}

void WINAPI GetStartupInfoA(LPSTARTUPINFOA info)
{
    RtlAcquirePebLock();
    if (!g_startup_a.cb) {
        STARTUPINFOW w;
        GetStartupInfoW(&w);
        memcpy(&g_startup_a, &w, sizeof(g_startup_a));
        g_startup_a.cb = sizeof(g_startup_a);
        g_startup_a.lpReserved = nullptr;
        g_startup_a.lpDesktop = nullptr;
        g_startup_a.lpTitle = nullptr;
        if (w.lpDesktop) {
            int n = WideCharToMultiByte(CP_ACP, 0, w.lpDesktop, -1, nullptr, 0, nullptr, nullptr);
            g_startup_a.lpDesktop = KbAlloc((SIZE_T)n);
            if (g_startup_a.lpDesktop)
                WideCharToMultiByte(CP_ACP, 0, w.lpDesktop, -1, g_startup_a.lpDesktop, n, nullptr, nullptr);
        }
        if (w.lpTitle) {
            int n = WideCharToMultiByte(CP_ACP, 0, w.lpTitle, -1, nullptr, 0, nullptr, nullptr);
            g_startup_a.lpTitle = KbAlloc((SIZE_T)n);
            if (g_startup_a.lpTitle)
                WideCharToMultiByte(CP_ACP, 0, w.lpTitle, -1, g_startup_a.lpTitle, n, nullptr, nullptr);
        }
    }
    *info = g_startup_a;
    RtlReleasePebLock();
}

/* ------------------------------------------------------------------ process creation */

/* an ansi environment block as a unicode one, both ending with an empty string. */
static PWSTR wide_environment(const char *ansi, SIZE_T *bytes)
{
    SIZE_T n = 0;
    while (ansi[n] || ansi[n + 1])
        n++;
    n += 2;
    int chars = MultiByteToWideChar(CP_ACP, 0, ansi, (int)n, nullptr, 0);
    PWSTR wide = chars > 0 ? KbAlloc((SIZE_T)chars * sizeof(WCHAR)) : nullptr;
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, ansi, (int)n, wide, chars);
    *bytes = (SIZE_T)chars * sizeof(WCHAR);
    return wide;
}

static SIZE_T environment_bytes(PCWSTR env)
{
    SIZE_T n = 0;
    while (env[n] || env[n + 1])
        n++;
    return (n + 2) * sizeof(WCHAR);
}

#define CONSOLE_DETACHED ((HANDLE)(LONG_PTR)-1)
#define PROCESS_CREATE_FLAGS_BREAKAWAY 0x00000001
#define PROCESS_CREATE_FLAGS_INHERIT_HANDLES 0x00000004

/*
 * the program, then its parameters as kernelbase hands them to the kernel:
 * environment, standard handles, window and console choices, title,
 * desktop and the c library's runtime data. there is no console window to
 * make, so a new console is the creator's; DETACHED_PROCESS gives none.
 * priority classes and the attribute list of an extended startup
 * information are accepted and not applied.
 */
BOOL WINAPI CreateProcessW(LPCWSTR application, LPWSTR command_line, LPSECURITY_ATTRIBUTES process_sa,
                           LPSECURITY_ATTRIBUTES thread_sa, BOOL inherit, DWORD flags, LPVOID environment,
                           LPCWSTR directory, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION info)
{
    if (!startup || !info || (!application && !command_line)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(info, 0, sizeof(*info));
    if (flags & (DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS)) {
        KbSetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if ((flags & DETACHED_PROCESS) && (flags & CREATE_NEW_CONSOLE)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR executable[MAX_PATH], full[MAX_PATH], cwd[MAX_PATH];
    if (application) {
        if (wcslen(application) >= MAX_PATH) {
            KbSetError(ERROR_FILENAME_EXCED_RANGE);
            return FALSE;
        }
        wcscpy(executable, application);
    } else {
        PCWSTR source = command_line;
        while (*source == L' ' || *source == L'\t')
            source++;
        BOOL quoted = *source == L'"';
        if (quoted)
            source++;
        SIZE_T n = 0;
        while (*source && (quoted ? *source != L'"' : *source != L' ' && *source != L'\t')) {
            if (n + 1 == MAX_PATH) {
                KbSetError(ERROR_FILENAME_EXCED_RANGE);
                return FALSE;
            }
            executable[n++] = *source++;
        }
        executable[n] = 0;
    }
    DWORD n = SearchPathW(nullptr, executable, L".exe", MAX_PATH, full, nullptr);
    if (!n || n >= MAX_PATH) {
        KbSetError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    n = directory ? GetFullPathNameW(directory, MAX_PATH, cwd, nullptr) : GetCurrentDirectoryW(MAX_PATH, cwd);
    if (!n || n >= MAX_PATH) {
        if (n >= MAX_PATH)
            KbSetError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    WCHAR default_command[MAX_PATH + 3];
    if (!command_line) {
        default_command[0] = L'"';
        wcscpy(default_command + 1, full);
        wcscat(default_command, L"\"");
        command_line = default_command;
    }
    PWSTR converted = nullptr;
    SIZE_T env_bytes = 0;
    if (environment && !(flags & CREATE_UNICODE_ENVIRONMENT)) {
        converted = wide_environment(environment, &env_bytes);
        if (!converted) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        environment = converted;
    } else if (environment) {
        env_bytes = environment_bytes(environment);
    }

    RTL_USER_PROCESS_PARAMETERS pp = {};
    pp.MaximumLength = pp.Length = sizeof(pp);
    pp.Flags = RTL_USER_PROC_PARAMS_NORMALIZED;
    RtlInitUnicodeString(&pp.ImagePathName, full);
    RtlInitUnicodeString(&pp.CommandLine, command_line);
    RtlInitUnicodeString(&pp.CurrentDirectory.DosPath, cwd);
    pp.Environment = environment;
    pp.EnvironmentSize = env_bytes;
    pp.WindowFlags = startup->dwFlags;
    pp.ShowWindowFlags = startup->wShowWindow;
    pp.StartingX = startup->dwX;
    pp.StartingY = startup->dwY;
    pp.CountX = startup->dwXSize;
    pp.CountY = startup->dwYSize;
    pp.CountCharsX = startup->dwXCountChars;
    pp.CountCharsY = startup->dwYCountChars;
    pp.FillAttribute = startup->dwFillAttribute;
    if (startup->dwFlags & STARTF_USESTDHANDLES) {
        pp.StandardInput = startup->hStdInput;
        pp.StandardOutput = startup->hStdOutput;
        pp.StandardError = startup->hStdError;
    }
    if (startup->lpTitle)
        RtlInitUnicodeString(&pp.WindowTitle, startup->lpTitle);
    if (startup->lpDesktop)
        RtlInitUnicodeString(&pp.DesktopInfo, startup->lpDesktop);
    if (startup->cbReserved2 && startup->lpReserved2) {
        pp.RuntimeData.Buffer = (PWSTR)startup->lpReserved2;
        pp.RuntimeData.Length = pp.RuntimeData.MaximumLength = startup->cbReserved2;
    }
    if (flags & DETACHED_PROCESS)
        pp.ConsoleHandle = CONSOLE_DETACHED;
    /* a new process group starts with ctrl+c ignored, as on windows. */
    if (flags & CREATE_NEW_PROCESS_GROUP)
        pp.ConsoleFlags = 1;
    else
        pp.ConsoleFlags = KbParams()->ConsoleFlags;

    ULONG process_flags = ((flags & CREATE_BREAKAWAY_FROM_JOB) ? PROCESS_CREATE_FLAGS_BREAKAWAY : 0) |
                          (inherit ? PROCESS_CREATE_FLAGS_INHERIT_HANDLES : 0);
    NTSTATUS status = NtCreateUserProcess(&info->hProcess, &info->hThread, PROCESS_ALL_ACCESS, THREAD_ALL_ACCESS,
                                           nullptr, nullptr, process_flags, (flags & CREATE_SUSPENDED) ? 1 : 0, &pp, nullptr,
                                           nullptr);
    KbFree(converted);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    if (process_sa && process_sa->bInheritHandle)
        SetHandleInformation(info->hProcess, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    if (thread_sa && thread_sa->bInheritHandle)
        SetHandleInformation(info->hThread, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    info->dwProcessId = GetProcessId(info->hProcess);
    info->dwThreadId = GetThreadId(info->hThread);
    return TRUE;
}

BOOL WINAPI CreateProcessA(LPCSTR application, LPSTR command_line, LPSECURITY_ATTRIBUTES process_sa,
                           LPSECURITY_ATTRIBUTES thread_sa, BOOL inherit, DWORD flags, LPVOID environment,
                           LPCSTR directory, LPSTARTUPINFOA startup, LPPROCESS_INFORMATION info)
{
    if (!startup || !info || (!application && !command_line)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR app = KbAnsiToWideCp(CP_ACP, application), cmd = KbAnsiToWideCp(CP_ACP, command_line);
    PWSTR dir = KbAnsiToWideCp(CP_ACP, directory);
    STARTUPINFOW sw;
    memcpy(&sw, startup, sizeof(sw));
    sw.cb = sizeof(sw);
    sw.lpDesktop = KbAnsiToWideCp(CP_ACP, startup->lpDesktop);
    sw.lpTitle = KbAnsiToWideCp(CP_ACP, startup->lpTitle);
    sw.lpReserved = nullptr;
    BOOL ok = CreateProcessW(app, cmd, process_sa, thread_sa, inherit, flags, environment, dir, &sw, info);
    KbFree(app);
    KbFree(cmd);
    KbFree(dir);
    KbFree(sw.lpDesktop);
    KbFree(sw.lpTitle);
    return ok;
}

UINT WINAPI WinExec(LPCSTR command_line, UINT show)
{
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = (WORD)show;
    SIZE_T n = command_line ? strlen(command_line) : 0;
    LPSTR copy = KbAlloc(n + 1);
    if (!copy)
        return 0;
    if (command_line)
        memcpy(copy, command_line, n + 1);
    else
        copy[0] = 0;
    BOOL ok = CreateProcessA(nullptr, copy, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    KbFree(copy);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 33;
    }
    DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? error : 0;
}

/* ------------------------------------------------------------------ times */

static void to_filetime(LPFILETIME ft, ULONGLONG v)
{
    if (ft) {
        ft->dwLowDateTime = (DWORD)v;
        ft->dwHighDateTime = (DWORD)(v >> 32);
    }
}

BOOL WINAPI GetThreadTimes(HANDLE thread, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user)
{
    ULONGLONG times[4];
    NTSTATUS status = NtQueryInformationThread(thread, ThreadTimes, times, sizeof(times), nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    to_filetime(creation, times[0]);
    to_filetime(exit, times[1]);
    to_filetime(kernel, times[2]);
    to_filetime(user, times[3]);
    return TRUE;
}

BOOL WINAPI GetProcessTimes(HANDLE process, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user)
{
    ULONGLONG times[4];
    NTSTATUS status = NtQueryInformationProcess(process, ProcessTimes, times, sizeof(times), nullptr);
    if (NT_SUCCESS(status)) {
        to_filetime(creation, times[0]);
        to_filetime(exit, times[1]);
        to_filetime(kernel, times[2]);
        to_filetime(user, times[3]);
        return TRUE;
    }
    /* without ProcessTimes, approximate the calling process from its own data. */
    PROCESS_BASIC_INFORMATION info;
    if (!query_process(process, &info))
        return FALSE;
    if (info.UniqueProcessId != GetCurrentProcessId()) {
        KbSetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    ULONGLONG thread_times[4] = {};
    NtQueryInformationThread(NtCurrentThread(), ThreadTimes, thread_times, sizeof(thread_times), nullptr);
    to_filetime(creation, (ULONGLONG)g_process_start_time);
    to_filetime(exit, 0);
    to_filetime(kernel, thread_times[2]);
    to_filetime(user, thread_times[3]);
    return TRUE;
}

BOOL WINAPI GetSystemTimes(LPFILETIME idle, LPFILETIME kernel, LPFILETIME user)
{
    volatile KUSER_SHARED_DATA *k = USER_SHARED_DATA;
    ULONGLONG uptime;
    LONG high;
    do {
        high = k->InterruptTime.High1Time;
        uptime = ((ULONGLONG)(ULONG)high << 32) | k->InterruptTime.LowPart;
    } while (high != k->InterruptTime.High2Time);
    ULONGLONG total = uptime * (k->ActiveProcessorCount ? k->ActiveProcessorCount : 1);
    to_filetime(idle, total);
    to_filetime(kernel, total);
    to_filetime(user, 0);
    return TRUE;
}

BOOL WINAPI QueryProcessCycleTime(HANDLE process, PULONG64 cycles)
{
    LARGE_INTEGER counter;
    NtQueryPerformanceCounter(&counter, nullptr);
    *cycles = (ULONG64)counter.QuadPart;
    return TRUE;
}

BOOL WINAPI QueryThreadCycleTime(HANDLE thread, PULONG64 cycles)
{
    return QueryProcessCycleTime(nullptr, cycles);
}

/* ------------------------------------------------------------------ apcs */

static DWORD g_apc_slot = TLS_OUT_OF_INDEXES;

void KbInitializeApc(void)
{
    DWORD slot = TlsAlloc();
    if (slot < RTL_NUMBER_OF(NtCurrentTeb()->TlsSlots))
        g_apc_slot = slot;
}

void KbNoteApc(void)
{
    if (g_apc_slot != TLS_OUT_OF_INDEXES) {
        PVOID *counter = &NtCurrentTeb()->TlsSlots[g_apc_slot];
        *counter = (PVOID)((ULONG_PTR)*counter + 1);
    }
}

/*
 * the kernel ignores user apcs queued while the target thread runs until
 * something tests for them, so an alertable wait would sleep through them.
 * NtTestAlert delivers them on its way back; the counter tells whether any ran.
 */
BOOL KbRunPendingApcs(void)
{
    if (g_apc_slot == TLS_OUT_OF_INDEXES)
        return FALSE;
    PVOID *counter = &NtCurrentTeb()->TlsSlots[g_apc_slot];
    ULONG_PTR before = (ULONG_PTR)*counter;
    NtTestAlert();
    return (ULONG_PTR)*(volatile PVOID *)counter != before;
}

static void NTAPI user_apc(PVOID function, PVOID data, PVOID unused)
{
    KbNoteApc();
    ((PAPCFUNC)function)((ULONG_PTR)data);
}

DWORD WINAPI QueueUserAPC(PAPCFUNC apc, HANDLE thread, ULONG_PTR data)
{
    NTSTATUS status = NtQueueApcThread(thread, user_apc, (PVOID)apc, (PVOID)data, nullptr);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ wow64 and machine */

BOOL WINAPI IsWow64Process(HANDLE process, PBOOL wow64)
{
    if (!wow64) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *wow64 = FALSE;
    return TRUE;
}

BOOL WINAPI IsWow64Process2(HANDLE process, USHORT *process_machine, USHORT *native_machine)
{
    if (!process_machine) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *process_machine = IMAGE_FILE_MACHINE_UNKNOWN;
    if (native_machine)
        *native_machine = IMAGE_FILE_MACHINE_ARM64;
    return TRUE;
}

/* ------------------------------------------------------------------ miscellaneous process information */

BOOL WINAPI GetProcessHandleCount(HANDLE process, PDWORD count)
{
    if (!count) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *count = 32;
    return TRUE;
}

DWORD WINAPI GetProcessVersion(DWORD id)
{
    if (id && id != GetCurrentProcessId()) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    PIMAGE_NT_HEADERS nt = KbImageNtHeader(KbPeb()->ImageBaseAddress);
    if (!nt)
        return 0;
    return ((DWORD)nt->OptionalHeader.MajorSubsystemVersion << 16) | nt->OptionalHeader.MinorSubsystemVersion;
}

static SIZE_T g_ws_min = 204800, g_ws_max = 1413120;

BOOL WINAPI GetProcessWorkingSetSize(HANDLE process, PSIZE_T minimum, PSIZE_T maximum)
{
    if (minimum)
        *minimum = g_ws_min;
    if (maximum)
        *maximum = g_ws_max;
    return TRUE;
}

BOOL WINAPI SetProcessWorkingSetSizeEx(HANDLE process, SIZE_T minimum, SIZE_T maximum, DWORD flags)
{
    if (minimum == (SIZE_T)-1 && maximum == (SIZE_T)-1)
        return TRUE;
    if (minimum > maximum) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    g_ws_min = minimum;
    g_ws_max = maximum;
    return TRUE;
}

BOOL WINAPI SetProcessWorkingSetSize(HANDLE process, SIZE_T minimum, SIZE_T maximum)
{
    return SetProcessWorkingSetSizeEx(process, minimum, maximum, 0);
}

BOOL WINAPI ProcessIdToSessionId(DWORD process, DWORD *session)
{
    if (!session) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *session = KbPeb()->SessionId;
    return TRUE;
}

DWORD WINAPI WTSGetActiveConsoleSessionId(void)
{
    return USER_SHARED_DATA->ActiveConsoleId;
}

static DWORD g_shutdown_level = 0x280, g_shutdown_flags;

BOOL WINAPI SetProcessShutdownParameters(DWORD level, DWORD flags)
{
    g_shutdown_level = level;
    g_shutdown_flags = flags;
    return TRUE;
}

BOOL WINAPI GetProcessShutdownParameters(LPDWORD level, LPDWORD flags)
{
    *level = g_shutdown_level;
    *flags = g_shutdown_flags;
    return TRUE;
}


BOOL WINAPI IsProcessCritical(HANDLE process, PBOOL critical)
{
    if (!critical) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *critical = FALSE;
    return TRUE;
}

typedef struct KB_MEMORY_PRIORITY_INFORMATION {
    ULONG MemoryPriority;
} KB_MEMORY_PRIORITY_INFORMATION;

typedef struct KB_PROCESS_MACHINE_INFORMATION {
    USHORT ProcessMachine;
    USHORT Res0;
    ULONG MachineAttributes;
} KB_PROCESS_MACHINE_INFORMATION;

BOOL WINAPI GetProcessInformation(HANDLE process, PROCESS_INFORMATION_CLASS klass, LPVOID info, DWORD size)
{
    switch (klass) {
    case ProcessMemoryPriority:
        if (size < sizeof(KB_MEMORY_PRIORITY_INFORMATION))
            break;
        ((KB_MEMORY_PRIORITY_INFORMATION *)info)->MemoryPriority = 5;
        return TRUE;
    case ProcessMachineTypeInfo:
        if (size < sizeof(KB_PROCESS_MACHINE_INFORMATION))
            break;
#ifdef __i386__
        ((KB_PROCESS_MACHINE_INFORMATION *)info)->ProcessMachine = IMAGE_FILE_MACHINE_I386;
#elifdef __x86_64__
        ((KB_PROCESS_MACHINE_INFORMATION *)info)->ProcessMachine = IMAGE_FILE_MACHINE_AMD64;
#else
        ((KB_PROCESS_MACHINE_INFORMATION *)info)->ProcessMachine = IMAGE_FILE_MACHINE_ARM64;
#endif
        ((KB_PROCESS_MACHINE_INFORMATION *)info)->Res0 = 0;
        ((KB_PROCESS_MACHINE_INFORMATION *)info)->MachineAttributes = 3; /* user and kernel enabled. */
        return TRUE;
    default:
        break;
    }
    KbSetError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL WINAPI SetProcessInformation(HANDLE process, PROCESS_INFORMATION_CLASS klass, LPVOID info, DWORD size)
{
    if (klass >= ProcessInformationClassMax || !info) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI GetThreadInformation(HANDLE thread, THREAD_INFORMATION_CLASS klass, LPVOID info, DWORD size)
{
    if (klass == ThreadMemoryPriority && size >= sizeof(KB_MEMORY_PRIORITY_INFORMATION)) {
        ((KB_MEMORY_PRIORITY_INFORMATION *)info)->MemoryPriority = 5;
        return TRUE;
    }
    KbSetError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL WINAPI SetThreadInformation(HANDLE thread, THREAD_INFORMATION_CLASS klass, LPVOID info, DWORD size)
{
    if (klass >= ThreadInformationClassMax || !info) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ thread descriptions */

typedef struct THREAD_DESCRIPTION {
    struct THREAD_DESCRIPTION *Next;
    DWORD ThreadId;
    WCHAR Text[1];
} THREAD_DESCRIPTION;

static THREAD_DESCRIPTION *g_descriptions;
static RTL_SRWLOCK g_description_lock;

HRESULT WINAPI SetThreadDescription(HANDLE thread, PCWSTR description)
{
    DWORD id = GetThreadId(thread);
    if (!id)
        return HRESULT_FROM_WIN32(GetLastError());
    if (!description)
        return E_INVALIDARG;
    SIZE_T n = wcslen(description);
    THREAD_DESCRIPTION *d = KbAlloc(offsetof(THREAD_DESCRIPTION, Text) + (n + 1) * sizeof(WCHAR));
    if (!d)
        return E_OUTOFMEMORY;
    d->ThreadId = id;
    memcpy(d->Text, description, (n + 1) * sizeof(WCHAR));
    UNICODE_STRING us = {(USHORT)(n * sizeof(WCHAR)), (USHORT)(n * sizeof(WCHAR)), d->Text};
    NtSetInformationThread(thread, ThreadDescription, &us, sizeof(us));
    RtlAcquireSRWLockExclusive(&g_description_lock);
    for (THREAD_DESCRIPTION **p = &g_descriptions; *p; p = &(*p)->Next) {
        if ((*p)->ThreadId == id) {
            THREAD_DESCRIPTION *old = *p;
            *p = old->Next;
            KbFree(old);
            break;
        }
    }
    d->Next = g_descriptions;
    g_descriptions = d;
    RtlReleaseSRWLockExclusive(&g_description_lock);
    return S_OK;
}

HRESULT WINAPI GetThreadDescription(HANDLE thread, PWSTR *description)
{
    if (!description)
        return E_INVALIDARG;
    *description = nullptr;
    DWORD id = GetThreadId(thread);
    if (!id)
        return HRESULT_FROM_WIN32(GetLastError());
    RtlAcquireSRWLockShared(&g_description_lock);
    PCWSTR text = L"";
    for (THREAD_DESCRIPTION *d = g_descriptions; d; d = d->Next)
        if (d->ThreadId == id)
            text = d->Text;
    SIZE_T n = wcslen(text);
    PWSTR copy = LocalAlloc(LMEM_FIXED, (n + 1) * sizeof(WCHAR));
    if (copy)
        memcpy(copy, text, (n + 1) * sizeof(WCHAR));
    RtlReleaseSRWLockShared(&g_description_lock);
    if (!copy)
        return E_OUTOFMEMORY;
    *description = copy;
    return S_OK;
}

/* ------------------------------------------------------------------ stacks */

BOOL WINAPI SetThreadStackGuarantee(PULONG size)
{
    if (!size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PTEB teb = NtCurrentTeb();
    ULONG previous = teb->GuaranteedStackBytes;
    if (*size) {
        ULONG_PTR stack = (ULONG_PTR)teb->NtTib.StackBase - (ULONG_PTR)teb->DeallocationStack;
        if (*size > stack / 2) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (*size > previous)
            teb->GuaranteedStackBytes = (*size + 0xFFF) & ~0xFFFu;
    }
    *size = previous;
    return TRUE;
}

void WINAPI GetCurrentThreadStackLimits(PULONG_PTR low, PULONG_PTR high)
{
    PTEB teb = NtCurrentTeb();
    *low = (ULONG_PTR)teb->DeallocationStack;
    *high = (ULONG_PTR)teb->NtTib.StackBase;
}

BOOL WINAPI GetThreadIOPendingFlag(HANDLE thread, PBOOL pending)
{
    if (!pending) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *pending = FALSE;
    return TRUE;
}

/* ------------------------------------------------------------------ attribute lists */

typedef struct KB_ATTRIBUTE {
    DWORD_PTR Attribute;
    SIZE_T Size;
    PVOID Value;
} KB_ATTRIBUTE;

struct _PROC_THREAD_ATTRIBUTE_LIST {
    DWORD Mask;
    DWORD Size;
    DWORD Count;
    DWORD Pad;
    KB_ATTRIBUTE Attributes[1];
};

BOOL WINAPI InitializeProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD count, DWORD flags,
                                              PSIZE_T size)
{
    if (flags || !size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SIZE_T need = offsetof(struct _PROC_THREAD_ATTRIBUTE_LIST, Attributes) + (count ? count : 1) * sizeof(KB_ATTRIBUTE);
    if (!list || *size < need) {
        *size = need;
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    list->Mask = 0;
    list->Size = count;
    list->Count = 0;
    list->Pad = 0;
    return TRUE;
}

BOOL WINAPI UpdateProcThreadAttribute(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD flags, DWORD_PTR attribute,
                                      PVOID value, SIZE_T size, PVOID previous, PSIZE_T returned)
{
    if (!list || flags || list->Count >= list->Size) {
        KbSetError(list && list->Count >= list->Size ? ERROR_GEN_FAILURE : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KB_ATTRIBUTE *a = &list->Attributes[list->Count++];
    a->Attribute = attribute;
    a->Size = size;
    a->Value = value;
    list->Mask |= 1u << (attribute & 0x1F);
    return TRUE;
}

void WINAPI DeleteProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST list)
{
}

/* ------------------------------------------------------------------ mitigation policies */

static DWORD g_dep_flags = PROCESS_DEP_ENABLE;

BOOL WINAPI SetProcessDEPPolicy(DWORD flags)
{
    if (flags & ~(DWORD)(PROCESS_DEP_ENABLE | PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    g_dep_flags = flags | PROCESS_DEP_ENABLE;
    return TRUE;
}

BOOL WINAPI GetProcessDEPPolicy(HANDLE process, LPDWORD flags, PBOOL permanent)
{
    if (flags)
        *flags = g_dep_flags;
    if (permanent)
        *permanent = TRUE;
    return TRUE;
}

DEP_SYSTEM_POLICY_TYPE WINAPI GetSystemDEPPolicy(void)
{
    return DEPPolicyAlwaysOn;
}

BOOL WINAPI SetProcessMitigationPolicy(PROCESS_MITIGATION_POLICY policy, PVOID buffer, SIZE_T length)
{
    if (!buffer) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI GetProcessMitigationPolicy(HANDLE process, PROCESS_MITIGATION_POLICY policy, PVOID buffer, SIZE_T length)
{
    if (!buffer || length < sizeof(DWORD)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(buffer, 0, length);
    if (policy == ProcessDEPPolicy)
        *(PDWORD)buffer = 3; /* the enable and permanent flags. */
    return TRUE;
}

/* ------------------------------------------------------------------ fibers */

#define FIBER_FLAG_FLOAT_SWITCH 0x1

typedef struct FIBER {
    PVOID Parameter;
    PVOID StackBase;
    PVOID StackLimit;
    PVOID DeallocationStack;
    PVOID FlsData;
    LPFIBER_START_ROUTINE Start;
    BOOL ConvertedThread;
    volatile LONG Resumed;
    CONTEXT Context;
} FIBER;

#define HAS_FIBER_DATA 0x0004 /* TEB SameTebFlags. */

LPVOID WINAPI ConvertThreadToFiberEx(LPVOID parameter, DWORD flags)
{
    PTEB teb = NtCurrentTeb();
    if (teb->SameTebFlags & HAS_FIBER_DATA) {
        KbSetError(ERROR_ALREADY_FIBER);
        return nullptr;
    }
    FIBER *f = KbAllocZero(sizeof(FIBER));
    if (!f) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    f->Parameter = parameter;
    f->StackBase = teb->NtTib.StackBase;
    f->StackLimit = teb->NtTib.StackLimit;
    f->DeallocationStack = teb->DeallocationStack;
    f->FlsData = teb->FlsData;
    f->ConvertedThread = TRUE;
    teb->NtTib.FiberData = f;
    teb->SameTebFlags |= HAS_FIBER_DATA;
    return f;
}

LPVOID WINAPI ConvertThreadToFiber(LPVOID parameter)
{
    return ConvertThreadToFiberEx(parameter, 0);
}

BOOL WINAPI ConvertFiberToThread(void)
{
    PTEB teb = NtCurrentTeb();
    if (!(teb->SameTebFlags & HAS_FIBER_DATA)) {
        KbSetError(ERROR_ALREADY_THREAD);
        return FALSE;
    }
    FIBER *f = teb->NtTib.FiberData;
    teb->SameTebFlags &= (USHORT)~HAS_FIBER_DATA;
    teb->NtTib.FiberData = nullptr;
    if (f && f->ConvertedThread)
        KbFree(f);
    return TRUE;
}

static void fiber_entry(FIBER *f)
{
    f->Start(f->Parameter);
    ExitThread(0);
}

LPVOID WINAPI CreateFiberEx(SIZE_T commit, SIZE_T reserve, DWORD flags, LPFIBER_START_ROUTINE start, LPVOID parameter)
{
    if (!start) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (!reserve)
        reserve = DEFAULT_STACK_RESERVE;
    if (reserve < commit)
        reserve = commit;
    reserve = (reserve + 0xFFFF) & ~(SIZE_T)0xFFFF;
    FIBER *f = KbAllocZero(sizeof(FIBER));
    if (!f) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    PVOID stack = VirtualAlloc(nullptr, reserve, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!stack) {
        KbFree(f);
        return nullptr;
    }
    f->Parameter = parameter;
    f->Start = start;
    f->DeallocationStack = stack;
    f->StackLimit = stack;
    f->StackBase = (PUCHAR)stack + reserve;
    f->Context.ContextFlags = CONTEXT_FULL;
#ifdef __i386__
    f->Context.Esp = ((ULONG_PTR)f->StackBase - 64) & ~(ULONG_PTR)15;
    f->Context.Eip = (ULONG_PTR)fiber_entry;
    ((PULONG)(ULONG_PTR)f->Context.Esp)[1] = (ULONG_PTR)f;
    f->Context.EFlags = 0x202;
#elifdef __x86_64__
    /* entered like a call: the return address slot below a 16 byte aligned frame. */
    f->Context.Rsp = (((ULONG_PTR)f->StackBase - 64) & ~(ULONG_PTR)15) - 8;
    f->Context.Rip = (ULONG64)fiber_entry;
    f->Context.Rcx = (ULONG64)f;
    f->Context.EFlags = 0x202;
    f->Context.MxCsr = 0x1F80;
    f->Context.FltSave.ControlWord = 0x27F;
    f->Context.FltSave.MxCsr = 0x1F80;
#else
    f->Context.Sp = ((ULONG_PTR)f->StackBase - 64) & ~(ULONG_PTR)15;
    f->Context.Pc = (ULONG64)fiber_entry;
    f->Context.X0 = (ULONG64)f;
    f->Context.X18 = (ULONG64)NtCurrentTeb();
    f->Context.Fpcr = 0;
#endif
    f->Resumed = 1;
    return f;
}

LPVOID WINAPI CreateFiber(SIZE_T stack, LPFIBER_START_ROUTINE start, LPVOID parameter)
{
    return CreateFiberEx(stack, 0, 0, start, parameter);
}

void WINAPI DeleteFiber(LPVOID fiber)
{
    FIBER *f = fiber;
    if (!f)
        return;
    PTEB teb = NtCurrentTeb();
    if (teb->NtTib.FiberData == f)
        ExitThread(1);
    if (!f->ConvertedThread)
        VirtualFree(f->DeallocationStack, 0, MEM_RELEASE);
    KbFree(f);
}

/* saves the running fiber with RtlCaptureContext and resumes the target with NtContinue. */
void WINAPI SwitchToFiber(LPVOID fiber)
{
    PTEB teb = NtCurrentTeb();
    FIBER *current = teb->NtTib.FiberData;
    FIBER *target = fiber;
    if (!current || current == target)
        return;
    current->Resumed = 0;
    current->FlsData = teb->FlsData;
    current->StackBase = teb->NtTib.StackBase;
    current->StackLimit = teb->NtTib.StackLimit;
    current->DeallocationStack = teb->DeallocationStack;
    RtlCaptureContext(&current->Context);
    if (((FIBER *)NtCurrentTeb()->NtTib.FiberData)->Resumed)
        return;
    current->Resumed = 1;
    teb->NtTib.FiberData = target;
    teb->NtTib.StackBase = target->StackBase;
    teb->NtTib.StackLimit = target->StackLimit;
    teb->DeallocationStack = target->DeallocationStack;
    teb->FlsData = target->FlsData;
#ifdef __aarch64__
    target->Context.X18 = (ULONG64)teb;
#endif
    NtContinue(&target->Context, FALSE);
}

BOOL WINAPI IsThreadAFiber(void)
{
    return (NtCurrentTeb()->SameTebFlags & HAS_FIBER_DATA) != 0;
}

/* the counters of the kernel when it keeps them, else zeros: no i/o is counted per process yet. */
BOOL WINAPI GetProcessIoCounters(HANDLE process, PIO_COUNTERS counters)
{
    if (!counters) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    NTSTATUS status = NtQueryInformationProcess(process, ProcessIoCounters, counters, sizeof(*counters), nullptr);
    if (status == STATUS_INVALID_INFO_CLASS || status == STATUS_NOT_IMPLEMENTED) {
        memset(counters, 0, sizeof(*counters));
        return TRUE;
    }
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

typedef struct WORK_ITEM {
    LPTHREAD_START_ROUTINE Function;
    PVOID Context;
} WORK_ITEM;

static DWORD WINAPI run_work_item(LPVOID arg)
{
    WORK_ITEM item = *(WORK_ITEM *)arg;
    KbFree(arg);
    return item.Function(item.Context);
}

/* each item runs on a thread of its own, which every WT_EXECUTE flag allows. */
BOOL WINAPI QueueUserWorkItem(LPTHREAD_START_ROUTINE function, PVOID context, ULONG flags)
{
    WORK_ITEM *item = KbAlloc(sizeof(*item));
    if (!item) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    item->Function = function;
    item->Context = context;
    HANDLE thread = CreateThread(nullptr, 0, run_work_item, item, 0, nullptr);
    if (!thread) {
        KbFree(item);
        return FALSE;
    }
    CloseHandle(thread);
    return TRUE;
}
