/*
 * processes and threads.
 */
#include "kernelbase_private.h"
#include <mime/mitigation.h>

#ifndef TOKEN_DUPLICATE
#define TOKEN_DUPLICATE 0x0002
#define TOKEN_QUERY 0x0008
#define TOKEN_ALL_ACCESS 0xF01FF
#endif

NTSYSCALLAPI NTSTATUS NTAPI NtAdjustPrivilegesToken(HANDLE token, BOOLEAN disable_all, PVOID state, ULONG length,
                                                    PVOID previous, PULONG returned);

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

HANDLE WINAPI OpenThread(DWORD access, BOOL inherit, DWORD id)
{
    CLIENT_ID cid = {nullptr, (HANDLE)(ULONG_PTR)id};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, nullptr, inherit ? OBJ_INHERIT : 0, nullptr, nullptr);
    HANDLE handle;
    NTSTATUS status = NtOpenThread(&handle, access, &oa, &cid);
    if (!NT_SUCCESS(status)) {
        KbSetError(status == STATUS_INVALID_CID ? ERROR_INVALID_PARAMETER : RtlNtStatusToDosError(status));
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
    /* the thread starts suspended when it must first inherit its creator's activation context. */
    PACTIVATION_CONTEXT context;
    BOOL inherit = KbInheritActivationContext(process, &context);
    HANDLE handle;
    NTSTATUS status = NtCreateThreadEx(&handle, THREAD_ALL_ACCESS, &oa, process, (PVOID)start, parameter,
                                       ((flags & CREATE_SUSPENDED) || inherit) ? THREAD_CREATE_FLAGS_CREATE_SUSPENDED : 0,
                                       0, commit, reserve, nullptr);
    if (!NT_SUCCESS(status)) {
        if (inherit)
            RtlReleaseActivationContext(context);
        KbSetStatus(status);
        return nullptr;
    }
    THREAD_BASIC_INFORMATION info;
    BOOL known = NT_SUCCESS(NtQueryInformationThread(handle, ThreadBasicInformation, &info, sizeof(info), nullptr));
    if (inherit) {
        if (known)
            MimeSxsInheritActivationContext(info.ClientId.UniqueThread, context);
        RtlReleaseActivationContext(context);
        if (!(flags & CREATE_SUSPENDED))
            NtResumeThread(handle, nullptr);
    }
    if (id)
        *id = known ? (DWORD)(ULONG_PTR)info.ClientId.UniqueThread : 0;
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
    /* the kernel reports the step over the process, and 16 either way for the limits. */
    if (info.BasePriority == 16)
        return THREAD_PRIORITY_TIME_CRITICAL;
    if (info.BasePriority == -16)
        return THREAD_PRIORITY_IDLE;
    return (int)info.BasePriority;
}

BOOL WINAPI SetThreadPriority(HANDLE thread, int priority)
{
    if (priority == THREAD_MODE_BACKGROUND_BEGIN || priority == THREAD_MODE_BACKGROUND_END)
        return TRUE;
    LONG increment = priority == THREAD_PRIORITY_TIME_CRITICAL ? 16 : priority == THREAD_PRIORITY_IDLE ? -16 : priority;
    NTSTATUS status = NtSetInformationThread(thread, ThreadBasePriority, &increment, sizeof(increment));
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI GetThreadPriorityBoost(HANDLE thread, PBOOL disabled)
{
    ULONG value = 0;
    NTSTATUS status = NtQueryInformationThread(thread, ThreadPriorityBoost, &value, sizeof(value), nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *disabled = value != 0;
    return TRUE;
}

BOOL WINAPI SetThreadPriorityBoost(HANDLE thread, BOOL disable)
{
    ULONG value = disable;
    NTSTATUS status = NtSetInformationThread(thread, ThreadPriorityBoost, &value, sizeof(value));
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

DWORD WINAPI GetPriorityClass(HANDLE process)
{
    PROCESS_PRIORITY_CLASS value = {};
    NTSTATUS status = NtQueryInformationProcess(process, ProcessPriorityClass, &value, sizeof(value), nullptr);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return 0;
    }
    switch (value.PriorityClass) {
    case PROCESS_PRIORITY_CLASS_IDLE:
        return IDLE_PRIORITY_CLASS;
    case PROCESS_PRIORITY_CLASS_HIGH:
        return HIGH_PRIORITY_CLASS;
    case PROCESS_PRIORITY_CLASS_REALTIME:
        return REALTIME_PRIORITY_CLASS;
    case PROCESS_PRIORITY_CLASS_BELOW_NORMAL:
        return BELOW_NORMAL_PRIORITY_CLASS;
    case PROCESS_PRIORITY_CLASS_ABOVE_NORMAL:
        return ABOVE_NORMAL_PRIORITY_CLASS;
    default:
        return NORMAL_PRIORITY_CLASS;
    }
}

/* the class a priority class flag names, 0 for none. */
static UCHAR class_of_flag(DWORD flag)
{
    if (flag & IDLE_PRIORITY_CLASS)
        return PROCESS_PRIORITY_CLASS_IDLE;
    if (flag & BELOW_NORMAL_PRIORITY_CLASS)
        return PROCESS_PRIORITY_CLASS_BELOW_NORMAL;
    if (flag & NORMAL_PRIORITY_CLASS)
        return PROCESS_PRIORITY_CLASS_NORMAL;
    if (flag & ABOVE_NORMAL_PRIORITY_CLASS)
        return PROCESS_PRIORITY_CLASS_ABOVE_NORMAL;
    if (flag & HIGH_PRIORITY_CLASS)
        return PROCESS_PRIORITY_CLASS_HIGH;
    if (flag & REALTIME_PRIORITY_CLASS)
        return PROCESS_PRIORITY_CLASS_REALTIME;
    return 0;
}

/* sets the class of a process, and the real time one only with the privilege of raising priorities. */
static NTSTATUS set_class(HANDLE process, UCHAR klass)
{
    ULONG value = PROCESS_PRIORITY_CLASS_EX_CLASS | ((ULONG)klass << 16);
    NTSTATUS status = NtSetInformationProcess(process, (PROCESSINFOCLASS)MX_PROCESS_PRIORITY_CLASS_EX, &value, sizeof(value));
    if (status != STATUS_PRIVILEGE_NOT_HELD || klass != PROCESS_PRIORITY_CLASS_REALTIME)
        return status;
    /* the privilege is there to be enabled for the call, as the user of a limited token has none. */
    struct {
        ULONG Count;
        ULONG Low;
        LONG High;
        ULONG Attributes;
    } enable = {1, 14, 0, 2 /* SE_PRIVILEGE_ENABLED. */}, before = {};
    HANDLE token = nullptr;
    ULONG returned = 0;
    NTSTATUS adjusted = NtOpenProcessToken(NtCurrentProcess(), 0x20 /* TOKEN_ADJUST_PRIVILEGES. */ | TOKEN_QUERY, &token);
    if (NT_SUCCESS(adjusted)) {
        adjusted = NtAdjustPrivilegesToken(token, FALSE, &enable, sizeof(enable),
                                           &before, &returned);
    }
    if (adjusted == STATUS_SUCCESS) {
        value = PROCESS_PRIORITY_CLASS_EX_CLASS | ((ULONG)PROCESS_PRIORITY_CLASS_REALTIME << 16);
        status = NtSetInformationProcess(process, (PROCESSINFOCLASS)MX_PROCESS_PRIORITY_CLASS_EX, &value, sizeof(value));
        if (before.Count)
            NtAdjustPrivilegesToken(token, FALSE, &before, sizeof(before), nullptr, &returned);
    } else {
        value = PROCESS_PRIORITY_CLASS_EX_CLASS | ((ULONG)PROCESS_PRIORITY_CLASS_HIGH << 16);
        status = NtSetInformationProcess(process, (PROCESSINFOCLASS)MX_PROCESS_PRIORITY_CLASS_EX, &value, sizeof(value));
    }
    if (token)
        NtClose(token);
    return status;
}

BOOL WINAPI SetPriorityClass(HANDLE process, DWORD flag)
{
    if (flag == PROCESS_MODE_BACKGROUND_BEGIN || flag == PROCESS_MODE_BACKGROUND_END)
        return TRUE;
    UCHAR klass = class_of_flag(flag);
    if (!klass) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    NTSTATUS status = set_class(process, klass);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

BOOL WINAPI GetProcessPriorityBoost(HANDLE process, PBOOL disabled)
{
    ULONG value = 0;
    NTSTATUS status = NtQueryInformationProcess(process, ProcessPriorityBoost, &value, sizeof(value), nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *disabled = value != 0;
    return TRUE;
}

BOOL WINAPI SetProcessPriorityBoost(HANDLE process, BOOL disable)
{
    ULONG value = disable;
    NTSTATUS status = NtSetInformationProcess(process, ProcessPriorityBoost, &value, sizeof(value));
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
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
        *process_mask = info.AffinityMask;
    if (system_mask)
        *system_mask = system_affinity();
    return TRUE;
}

BOOL WINAPI SetProcessAffinityMask(HANDLE process, DWORD_PTR mask)
{
    ULONG_PTR value = mask;
    NTSTATUS status = NtSetInformationProcess(process, ProcessAffinityMask, &value, sizeof(value));
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

DWORD_PTR WINAPI SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask)
{
    THREAD_BASIC_INFORMATION info;
    if (!query_thread(thread, &info))
        return 0;
    ULONG_PTR value = mask;
    NTSTATUS status = NtSetInformationThread(thread, ThreadAffinityMask, &value, sizeof(value));
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return 0;
    }
    return info.AffinityMask;
}

DWORD WINAPI SetThreadIdealProcessor(HANDLE thread, DWORD processor)
{
    /* the status of the call is the processor that was the ideal before. */
    NTSTATUS status = NtSetInformationThread(thread, ThreadIdealProcessor, &processor, sizeof(processor));
    if (NT_SUCCESS(status))
        return (DWORD)status;
    KbSetStatus(status);
    return (DWORD)-1;
}

BOOL WINAPI SetThreadIdealProcessorEx(HANDLE thread, PPROCESSOR_NUMBER ideal, PPROCESSOR_NUMBER previous)
{
    if (!ideal) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PROCESSOR_NUMBER number = *ideal;
    NTSTATUS status = NtSetInformationThread(thread, ThreadIdealProcessorEx, &number, sizeof(number));
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    if (previous)
        *previous = number;
    return TRUE;
}

BOOL WINAPI GetThreadIdealProcessorEx(HANDLE thread, PPROCESSOR_NUMBER ideal)
{
    if (!ideal) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    NTSTATUS status = NtQueryInformationThread(thread, ThreadIdealProcessorEx, ideal, sizeof(*ideal), nullptr);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

BOOL WINAPI GetThreadGroupAffinity(HANDLE thread, PGROUP_AFFINITY affinity)
{
    if (!affinity) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    THREAD_BASIC_INFORMATION info;
    if (!query_thread(thread, &info))
        return FALSE;
    memset(affinity, 0, sizeof(*affinity));
    affinity->Mask = info.AffinityMask;
    return TRUE;
}

BOOL WINAPI SetThreadGroupAffinity(HANDLE thread, const GROUP_AFFINITY *affinity, PGROUP_AFFINITY previous)
{
    if (!affinity || affinity->Group) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    THREAD_BASIC_INFORMATION info;
    if (!query_thread(thread, &info))
        return FALSE;
    ULONG_PTR value = affinity->Mask;
    NTSTATUS status = NtSetInformationThread(thread, ThreadAffinityMask, &value, sizeof(value));
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    if (previous) {
        memset(previous, 0, sizeof(*previous));
        previous->Mask = info.AffinityMask;
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
 * the list has windows' layout: how many attributes it holds, how many it has, which numbers they are,
 * the slot of the extended flags, then attributes of 24 bytes. a value stays with the caller, only the
 * extended flags are kept in the list.
 */
typedef struct KB_ATTRIBUTE {
    DWORD_PTR Attribute;
    SIZE_T Size;
    PVOID Value;
} KB_ATTRIBUTE;

struct _PROC_THREAD_ATTRIBUTE_LIST {
    DWORD Mask;
    DWORD Capacity;
    DWORD Used;
    DWORD MaskHigh;
    KB_ATTRIBUTE *Extended;
    KB_ATTRIBUTE Attributes[1];
};

#define KB_ATTRIBUTE_MAX 0x25
#define KB_ATTRIBUTE_ADDITIVE 0x40000
#define KB_ATTRIBUTE_INPUT 0x20000

/* ------------------------------------------------------------------ the attributes of a new process */

#define KB_PS_PARENT_PROCESS 0x00060000u
#define KB_PS_TOKEN 0x00060002u
#define KB_PS_HANDLE_LIST 0x0002000Bu
#define KB_PS_GROUP_AFFINITY 0x0003000Cu
#define KB_PS_PREFERRED_NODE 0x0002000Du
#define KB_PS_IDEAL_PROCESSOR 0x0003000Eu
#define KB_PS_MITIGATION_OPTIONS 0x00020010u
#define KB_PS_PROTECTION_LEVEL 0x00060011u
#define KB_PS_JOB_LIST 0x00020013u
#define KB_PS_CHILD_PROCESS_POLICY 0x00020014u
#define KB_PS_ALL_APPLICATION_PACKAGES_POLICY 0x00020015u
#define KB_PS_WIN32K_FILTER 0x00020016u
#define KB_PS_DESKTOP_APP_POLICY 0x00020019u
#define KB_PS_MACHINE_TYPE 0x0006001Cu
#define KB_PS_COMPONENT_FILTER 0x0002001Du
#define KB_PS_ENABLE_OPTIONAL_XSTATE_FEATURES 0x0003001Eu

typedef struct KB_SID_ATTRIBUTES {
    PVOID Sid;
    DWORD Attributes;
} KB_SID_ATTRIBUTES;

typedef struct KB_SECURITY_CAPABILITIES {
    PVOID AppContainerSid;
    KB_SID_ATTRIBUTES *Capabilities;
    DWORD CapabilityCount, Reserved;
} KB_SECURITY_CAPABILITIES;

typedef struct KB_PROCESS_ATTRIBUTES {
    SIZE_T TotalLength;
    PS_ATTRIBUTE Attributes[24];
    ULONG Count;
    ULONG64 Mitigation[3];
    DWORD Win32k[2];
    UCHAR Protection;
    const KB_SECURITY_CAPABILITIES *Capabilities;
} KB_PROCESS_ATTRIBUTES;

/* the creation flags of a program as the three words the kernel takes, or an error. */
static BOOL pack_mitigation(const KB_ATTRIBUTE *a, ULONG64 packed[3])
{
    static const MIME_MITIGATION_FIELD fields[] = {MIME_MITIGATION_FIELDS};
    ULONG64 w[2] = {};
    memcpy(w, a->Value, a->Size == 4 ? 4 : a->Size);
    if ((w[0] & MIME_MITIGATION_INVALID0) || (w[1] & MIME_MITIGATION_INVALID1))
        return FALSE;
    ULONG dep = (ULONG)w[0] & 3;
    packed[0] = (dep == 1 ? 1 : dep == 3 ? 3 : 2) | ((w[0] & 4) ? 0x10 : 0x30);
    packed[1] = packed[2] = 0;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(fields); i++) {
        const MIME_MITIGATION_FIELD *f = &fields[i];
        ULONG64 value = (w[f->Word] >> f->Shift) & 3;
        if (value == 3 && !f->AllowThree)
            return FALSE;
        packed[f->KernelWord] |= value << f->KernelShift;
    }
    return TRUE;
}

static BOOL add_ps(KB_PROCESS_ATTRIBUTES *out, ULONG number, SIZE_T size, ULONG_PTR value)
{
    if (out->Count >= RTL_NUMBER_OF(out->Attributes))
        return FALSE;
    PS_ATTRIBUTE *a = &out->Attributes[out->Count++];
    a->Attribute = number;
    a->Size = size;
    a->Value = value;
    a->ReturnLength = nullptr;
    return TRUE;
}

/* the PS_ATTRIBUTE_PROTECTION values of the levels of PROCESS_CREATION_PROTECTION. */
static BOOL protection_value(DWORD level, UCHAR *value)
{
    static const UCHAR values[] = {0x61, 0x52, 0x51, 0x31, 0x41, 0x62, 0x21, 0x12, 0x81};
    if (level == 0xFFFFFFFFu) {
        *value = 0;
        return NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), (PROCESSINFOCLASS)61, value, 1, nullptr));
    }
    if (level >= RTL_NUMBER_OF(values))
        return FALSE;
    *value = values[level];
    return TRUE;
}

/*
 * what the attribute list of an extended startup information asks of the new process, as the attributes
 * the kernel takes. a handle list needs a creation that inherits handles.
 */
static DWORD convert_attributes(LPPROC_THREAD_ATTRIBUTE_LIST list, BOOL inherit, KB_PROCESS_ATTRIBUTES *out)
{
    for (DWORD i = 0; i < list->Used; i++) {
        const KB_ATTRIBUTE *a = &list->Attributes[i];
        switch (a->Attribute) {
        case PROC_THREAD_ATTRIBUTE_PARENT_PROCESS:
            if (!add_ps(out, KB_PS_PARENT_PROCESS, sizeof(HANDLE), (ULONG_PTR)*(HANDLE *)a->Value))
                return ERROR_INVALID_PARAMETER;
            break;
        case PROC_THREAD_ATTRIBUTE_HANDLE_LIST:
            if (!inherit || !add_ps(out, KB_PS_HANDLE_LIST, a->Size, (ULONG_PTR)a->Value))
                return ERROR_INVALID_PARAMETER;
            break;
        case PROC_THREAD_ATTRIBUTE_PREFERRED_NODE:
            add_ps(out, KB_PS_PREFERRED_NODE, a->Size, (ULONG_PTR)a->Value);
            break;
        case PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY:
            if (!pack_mitigation(a, out->Mitigation) ||
                !add_ps(out, KB_PS_MITIGATION_OPTIONS, sizeof(out->Mitigation), (ULONG_PTR)out->Mitigation))
                return ERROR_INVALID_PARAMETER;
            break;
        case PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES:
            out->Capabilities = a->Value;
            break;
        case PROC_THREAD_ATTRIBUTE_PROTECTION_LEVEL:
            if (!protection_value(*(DWORD *)a->Value, &out->Protection) ||
                !add_ps(out, KB_PS_PROTECTION_LEVEL, 1, out->Protection))
                return ERROR_INVALID_PARAMETER;
            break;
        case PROC_THREAD_ATTRIBUTE_JOB_LIST:
            if (!add_ps(out, KB_PS_JOB_LIST, a->Size, (ULONG_PTR)a->Value))
                return ERROR_INVALID_PARAMETER;
            break;
        case PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY:
            add_ps(out, KB_PS_CHILD_PROCESS_POLICY, 4, (ULONG_PTR)a->Value);
            break;
        case PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY:
            add_ps(out, KB_PS_ALL_APPLICATION_PACKAGES_POLICY, 4, (ULONG_PTR)a->Value);
            break;
        case PROC_THREAD_ATTRIBUTE_WIN32K_FILTER:
            add_ps(out, KB_PS_WIN32K_FILTER, 8, (ULONG_PTR)a->Value);
            break;
        case PROC_THREAD_ATTRIBUTE_DESKTOP_APP_POLICY:
            add_ps(out, KB_PS_DESKTOP_APP_POLICY, 4, (ULONG_PTR)a->Value);
            break;
        case PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER:
            add_ps(out, KB_PS_COMPONENT_FILTER, 4, (ULONG_PTR)a->Value);
            break;
        case PROC_THREAD_ATTRIBUTE_MACHINE_TYPE:
            add_ps(out, KB_PS_MACHINE_TYPE, 2, *(USHORT *)a->Value);
            break;
        case PROC_THREAD_ATTRIBUTE_GROUP_AFFINITY:
            add_ps(out, KB_PS_GROUP_AFFINITY, 16, (ULONG_PTR)a->Value);
            break;
        case PROC_THREAD_ATTRIBUTE_IDEAL_PROCESSOR:
            add_ps(out, KB_PS_IDEAL_PROCESSOR, 4, (ULONG_PTR)a->Value);
            break;
        case PROC_THREAD_ATTRIBUTE_ENABLE_OPTIONAL_XSTATE_FEATURES:
            add_ps(out, KB_PS_ENABLE_OPTIONAL_XSTATE_FEATURES, 8, (ULONG_PTR)a->Value);
            break;
        default:
            /* the pseudo console and the rest do nothing here. */
            break;
        }
    }
    return 0;
}

/* a token for an app container, made from the process token or the one of CreateProcessAsUser. */
static NTSTATUS create_container_token(HANDLE base, const KB_SECURITY_CAPABILITIES *c, PHANDLE out)
{
    HANDLE own = nullptr, source = base;
    if (!source) {
        NTSTATUS status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &own);
        if (!NT_SUCCESS(status))
            return status;
        source = own;
    }
    NTSTATUS status = NtCreateLowBoxToken(out, source, TOKEN_ALL_ACCESS, nullptr, c->AppContainerSid, c->CapabilityCount,
                                          c->Capabilities, 0, nullptr);
    if (own)
        NtClose(own);
    return status;
}

/*
 * the program, then its parameters as kernelbase hands them to the kernel:
 * environment, standard handles, window and console choices, title,
 * desktop and the c library's runtime data. there is no console window to
 * make, so a new console is the creator's; DETACHED_PROCESS gives none.
 * priority classes and the attribute list of an extended startup
 * information are accepted and not applied.
 */
BOOL WINAPI CreateProcessInternalW(HANDLE token, LPCWSTR application, LPWSTR command_line,
                                   LPSECURITY_ATTRIBUTES process_sa, LPSECURITY_ATTRIBUTES thread_sa, BOOL inherit,
                                   DWORD flags, LPVOID environment, LPCWSTR directory, LPSTARTUPINFOW startup,
                                   LPPROCESS_INFORMATION info, PHANDLE new_token)
{
    if (new_token)
        *new_token = nullptr;
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
    /* the token of CreateProcessAsUser is an attribute of the process to make, so are those of the extended startup information. */
    KB_PROCESS_ATTRIBUTES attributes = {};
    HANDLE container = nullptr;
    if ((flags & EXTENDED_STARTUPINFO_PRESENT) && startup->cb >= sizeof(STARTUPINFOEXW) &&
        ((LPSTARTUPINFOEXW)startup)->lpAttributeList) {
        DWORD error = convert_attributes(((LPSTARTUPINFOEXW)startup)->lpAttributeList, inherit, &attributes);
        if (error) {
            KbFree(converted);
            KbSetError(error);
            return FALSE;
        }
        if (attributes.Capabilities) {
            NTSTATUS made = create_container_token(token, attributes.Capabilities, &container);
            if (!NT_SUCCESS(made)) {
                KbFree(converted);
                return KbSetStatus(made);
            }
            token = container;
        }
    }
    if (token && !add_ps(&attributes, PS_ATTRIBUTE_TOKEN, sizeof(HANDLE), (ULONG_PTR)token)) {
        KbFree(converted);
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    attributes.TotalLength = sizeof(SIZE_T) + attributes.Count * sizeof(PS_ATTRIBUTE);
    /* the descriptors of the security attributes are the new objects' own. */
    OBJECT_ATTRIBUTES process_oa, thread_oa;
    InitializeObjectAttributes(&process_oa, nullptr, 0, nullptr, process_sa ? process_sa->lpSecurityDescriptor : nullptr);
    InitializeObjectAttributes(&thread_oa, nullptr, 0, nullptr, thread_sa ? thread_sa->lpSecurityDescriptor : nullptr);
    NTSTATUS status = NtCreateUserProcess(&info->hProcess, &info->hThread, PROCESS_ALL_ACCESS, THREAD_ALL_ACCESS,
                                           process_oa.SecurityDescriptor ? &process_oa : nullptr,
                                           thread_oa.SecurityDescriptor ? &thread_oa : nullptr, process_flags, (flags & CREATE_SUSPENDED) || class_of_flag(flags) ? 1 : 0, &pp, nullptr,
                                           attributes.Count ? (PS_ATTRIBUTE_LIST *)&attributes : nullptr);
    if (container)
        NtClose(container);
    KbFree(converted);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    if (process_sa && process_sa->bInheritHandle)
        SetHandleInformation(info->hProcess, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    if (thread_sa && thread_sa->bInheritHandle)
        SetHandleInformation(info->hThread, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    /* the class a creation flag names is set before the program has run for long. */
    UCHAR klass = class_of_flag(flags);
    if (klass) {
        set_class(info->hProcess, klass);
        if (!(flags & CREATE_SUSPENDED))
            NtResumeThread(info->hThread, nullptr);
    }
    info->dwProcessId = GetProcessId(info->hProcess);
    info->dwThreadId = GetThreadId(info->hThread);
    return TRUE;
}

BOOL WINAPI CreateProcessW(LPCWSTR application, LPWSTR command_line, LPSECURITY_ATTRIBUTES process_sa,
                           LPSECURITY_ATTRIBUTES thread_sa, BOOL inherit, DWORD flags, LPVOID environment,
                           LPCWSTR directory, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION info)
{
    return CreateProcessInternalW(nullptr, application, command_line, process_sa, thread_sa, inherit, flags,
                                  environment, directory, startup, info, nullptr);
}

BOOL WINAPI CreateProcessAsUserW(HANDLE token, LPCWSTR application, LPWSTR command_line,
                                 LPSECURITY_ATTRIBUTES process_sa, LPSECURITY_ATTRIBUTES thread_sa, BOOL inherit,
                                 DWORD flags, LPVOID environment, LPCWSTR directory, LPSTARTUPINFOW startup,
                                 LPPROCESS_INFORMATION info)
{
    return CreateProcessInternalW(token, application, command_line, process_sa, thread_sa, inherit, flags,
                                  environment, directory, startup, info, nullptr);
}

BOOL WINAPI CreateProcessInternalA(HANDLE token, LPCSTR application, LPSTR command_line,
                                   LPSECURITY_ATTRIBUTES process_sa, LPSECURITY_ATTRIBUTES thread_sa, BOOL inherit,
                                   DWORD flags, LPVOID environment, LPCSTR directory, LPSTARTUPINFOA startup,
                                   LPPROCESS_INFORMATION info, PHANDLE new_token)
{
    if (new_token)
        *new_token = nullptr;
    if (!startup || !info || (!application && !command_line)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR app = KbAnsiToWideCp(CP_ACP, application), cmd = KbAnsiToWideCp(CP_ACP, command_line);
    PWSTR dir = KbAnsiToWideCp(CP_ACP, directory);
    STARTUPINFOEXW sx = {};
    STARTUPINFOW *sw = &sx.StartupInfo;
    memcpy(sw, startup, sizeof(*sw));
    sw->cb = sizeof(sx);
    sw->lpDesktop = KbAnsiToWideCp(CP_ACP, startup->lpDesktop);
    sw->lpTitle = KbAnsiToWideCp(CP_ACP, startup->lpTitle);
    sw->lpReserved = nullptr;
    if ((flags & EXTENDED_STARTUPINFO_PRESENT) && startup->cb >= sizeof(STARTUPINFOEXA))
        sx.lpAttributeList = ((LPSTARTUPINFOEXA)startup)->lpAttributeList;
    BOOL ok = CreateProcessInternalW(token, app, cmd, process_sa, thread_sa, inherit, flags, environment, dir, sw, info,
                                     nullptr);
    KbFree(app);
    KbFree(cmd);
    KbFree(dir);
    KbFree(sw->lpDesktop);
    KbFree(sw->lpTitle);
    return ok;
}

BOOL WINAPI CreateProcessA(LPCSTR application, LPSTR command_line, LPSECURITY_ATTRIBUTES process_sa,
                           LPSECURITY_ATTRIBUTES thread_sa, BOOL inherit, DWORD flags, LPVOID environment,
                           LPCSTR directory, LPSTARTUPINFOA startup, LPPROCESS_INFORMATION info)
{
    return CreateProcessInternalA(nullptr, application, command_line, process_sa, thread_sa, inherit, flags,
                                  environment, directory, startup, info, nullptr);
}

BOOL WINAPI CreateProcessAsUserA(HANDLE token, LPCSTR application, LPSTR command_line,
                                 LPSECURITY_ATTRIBUTES process_sa, LPSECURITY_ATTRIBUTES thread_sa, BOOL inherit,
                                 DWORD flags, LPVOID environment, LPCSTR directory, LPSTARTUPINFOA startup,
                                 LPPROCESS_INFORMATION info)
{
    return CreateProcessInternalA(token, application, command_line, process_sa, thread_sa, inherit, flags,
                                  environment, directory, startup, info, nullptr);
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
    NTSTATUS status = NtTestAlert();
    return status == STATUS_USER_APC || (ULONG_PTR)*(volatile PVOID *)counter != before;
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

/* a 32-bit process on a 64-bit machine has a 32-bit peb, which the kernel reports. */
static NTSTATUS wow64_peb(HANDLE process, ULONG_PTR *peb)
{
    *peb = 0;
    return NtQueryInformationProcess(process, ProcessWow64Information, peb, sizeof(*peb), nullptr);
}

BOOL WINAPI IsWow64Process(HANDLE process, PBOOL wow64)
{
    if (!wow64) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG_PTR peb;
    NTSTATUS status = wow64_peb(process, &peb);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *wow64 = peb != 0;
    return TRUE;
}

/* the machine of a process when it runs translated, unknown when native, and the machine itself. */
BOOL WINAPI IsWow64Process2(HANDLE process, USHORT *process_machine, USHORT *native_machine)
{
    if (!process_machine) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG_PTR peb;
    NTSTATUS status = wow64_peb(process, &peb);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *process_machine = peb ? IMAGE_FILE_MACHINE_I386 : IMAGE_FILE_MACHINE_UNKNOWN;
    if (native_machine)
        *native_machine = USER_SHARED_DATA->ImageNumberHigh;
    return TRUE;
}

/* ------------------------------------------------------------------ miscellaneous process information */

BOOL WINAPI GetProcessHandleCount(HANDLE process, PDWORD count)
{
    if (!count) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD counts[2];
    NTSTATUS status = NtQueryInformationProcess(process, ProcessHandleCount, counts, sizeof(counts), nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *count = counts[0];
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

/* the working set limits bind nothing, the flags of a process that has no hard limits. */
DECLSPEC_EXPORT BOOL WINAPI GetProcessWorkingSetSizeEx(HANDLE process, PSIZE_T minimum, PSIZE_T maximum, PDWORD flags)
{
    if (!minimum || !maximum || !flags) {
        KbSetError(ERROR_NOACCESS);
        return FALSE;
    }
    *minimum = g_ws_min;
    *maximum = g_ws_max;
    *flags = 0xA;
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
    if (process == (DWORD)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess) {
        *session = KbPeb()->SessionId;
        return TRUE;
    }
    /* another process says which session it is in, when it may be asked. */
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
    if (!handle)
        return FALSE;
    ULONG value = 0;
    NTSTATUS status = NtQueryInformationProcess(handle, ProcessSessionInformation, &value, sizeof(value), nullptr);
    CloseHandle(handle);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *session = value;
    return TRUE;
}

DWORD WINAPI WTSGetActiveConsoleSessionId(void)
{
    return USER_SHARED_DATA->ActiveConsoleId;
}

/*
 * the level orders the process in a shutdown, the highest first, and the one flag, SHUTDOWN_NORETRY, spares it
 * the dialog of a program that does not answer. the kernel keeps both, as csrss does on windows, and answers
 * any other flag with the status csrss does, which the status table turns into error 641.
 */
BOOL WINAPI SetProcessShutdownParameters(DWORD level, DWORD flags)
{
    MX_SHUTDOWN_PARAMETERS parameters = {level, flags};
    NTSTATUS status = NtSetInformationProcess(NtCurrentProcess(), (PROCESSINFOCLASS)MX_PROCESS_SHUTDOWN_PARAMETERS,
                                              &parameters, sizeof(parameters));
    return NT_SUCCESS(status) || KbSetStatus(status);
}

BOOL WINAPI GetProcessShutdownParameters(LPDWORD level, LPDWORD flags)
{
    MX_SHUTDOWN_PARAMETERS parameters;
    NTSTATUS status = NtQueryInformationProcess(NtCurrentProcess(), (PROCESSINFOCLASS)MX_PROCESS_SHUTDOWN_PARAMETERS,
                                                &parameters, sizeof(parameters), nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *level = parameters.Level;
    *flags = parameters.Flags;
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
    if (klass == ThreadDynamicCodePolicy) {
        if (size != sizeof(DWORD)) {
            KbSetError(ERROR_BAD_LENGTH);
            return FALSE;
        }
        NTSTATUS status = NtSetInformationThread(thread, ThreadDynamicCodePolicyInfo, info, size);
        return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
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

BOOL WINAPI InitializeProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD count, DWORD flags,
                                              PSIZE_T size)
{
    if (flags || count > KB_ATTRIBUTE_MAX || !size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SIZE_T need = offsetof(struct _PROC_THREAD_ATTRIBUTE_LIST, Attributes) + (SIZE_T)count * sizeof(KB_ATTRIBUTE);
    BOOL ok = list && *size >= need;
    if (ok) {
        list->Mask = list->MaskHigh = list->Used = 0;
        list->Extended = nullptr;
        list->Capacity = count;
    } else {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
    }
    *size = need;
    return ok;
}

/* the size an attribute's value must have, 0 for one that has no fixed size; or an error. */
static DWORD attribute_size_error(DWORD_PTR attribute, PVOID value, SIZE_T size)
{
    switch (attribute) {
    case PROC_THREAD_ATTRIBUTE_GROUP_AFFINITY:
        return size == sizeof(GROUP_AFFINITY) ? 0 : ERROR_BAD_LENGTH;
    case PROC_THREAD_ATTRIBUTE_PARENT_PROCESS:
    case 0x2000A:
    case 0x2000C:
    case 0x20015:
    case PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE:
    case 0x20023:
        return size == sizeof(PVOID) ? 0 : ERROR_BAD_LENGTH;
    case PROC_THREAD_ATTRIBUTE_WIN32K_FILTER:
    case PROC_THREAD_ATTRIBUTE_ENABLE_OPTIONAL_XSTATE_FEATURES:
        return size == 8 ? 0 : ERROR_BAD_LENGTH;
    case PROC_THREAD_ATTRIBUTE_HANDLE_LIST:
    case PROC_THREAD_ATTRIBUTE_JOB_LIST:
        return size && !(size & (sizeof(HANDLE) - 1)) ? 0 : ERROR_BAD_LENGTH;
    case PROC_THREAD_ATTRIBUTE_PREFERRED_NODE:
    case PROC_THREAD_ATTRIBUTE_MACHINE_TYPE:
        return size == 2 ? 0 : ERROR_BAD_LENGTH;
    case PROC_THREAD_ATTRIBUTE_IDEAL_PROCESSOR:
        return size == 4 ? 0 : ERROR_BAD_LENGTH;
    case PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY:
    case 0x20018:
        return (size == 4 || size == 8 || size == 16) ? 0 : ERROR_BAD_LENGTH;
    case 0x20008:
    case 0x20014:
        return !size || (size & 1) || size >= 0xFFFF ? ERROR_INVALID_PARAMETER : 0;
    case PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES:
        return size == sizeof(KB_SECURITY_CAPABILITIES) && !((KB_SECURITY_CAPABILITIES *)value)->Reserved
                   ? 0
                   : ERROR_INVALID_PARAMETER;
    case PROC_THREAD_ATTRIBUTE_PROTECTION_LEVEL:
    case PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY:
    case PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY:
    case PROC_THREAD_ATTRIBUTE_DESKTOP_APP_POLICY:
    case PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER:
    case 0x2001C:
        return size == 4 ? 0 : ERROR_BAD_LENGTH;
    case 0x20011:
        return size == 524 ? 0 : ERROR_BAD_LENGTH;
    case 0x20013:
        return size == 276 ? 0 : ERROR_BAD_LENGTH;
    case 0x20017:
        return size == 56 ? 0 : ERROR_BAD_LENGTH;
    case 0x2001D:
        return size ? 0 : ERROR_BAD_LENGTH;
    default:
        return ERROR_NOT_SUPPORTED;
    }
}

BOOL WINAPI UpdateProcThreadAttribute(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD flags, DWORD_PTR attribute,
                                      PVOID value, SIZE_T size, PVOID previous, PSIZE_T returned)
{
    if (flags & ~(DWORD)PROC_THREAD_ATTRIBUTE_REPLACE_VALUE) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG64 bit = 1ull << (attribute & 63);
    DWORD error;
    if (!(attribute & KB_ATTRIBUTE_ADDITIVE)) {
        if (list->Used == list->Capacity) {
            error = ERROR_GEN_FAILURE;
        } else if ((list->Mask | (ULONG64)list->MaskHigh << 32) & bit) {
            error = ERROR_OBJECT_NAME_EXISTS;
        } else if (previous || (flags & PROC_THREAD_ATTRIBUTE_REPLACE_VALUE)) {
            error = ERROR_INVALID_PARAMETER;
        } else {
            error = 0;
        }
        if (error) {
            KbSetError(error);
            return FALSE;
        }
    }
    if ((attribute & KB_ATTRIBUTE_INPUT) && returned) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KB_ATTRIBUTE *slot = &list->Attributes[list->Used];
    if (attribute == PROC_THREAD_ATTRIBUTE_EXTENDED_FLAGS) {
        /* the one attribute that adds up: its bits are kept in the list, in a slot of its own. */
        if (size != 4) {
            KbSetError(ERROR_BAD_LENGTH);
            return FALSE;
        }
        DWORD old = 0, bits = *(DWORD *)value;
        if (list->Extended) {
            slot = list->Extended;
            bit = 0;
            old = (DWORD)(ULONG_PTR)slot->Value;
        } else {
            list->Extended = slot;
        }
        if (bits & ~7u) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (!(flags & PROC_THREAD_ATTRIBUTE_REPLACE_VALUE))
            bits |= old;
        if (previous)
            *(DWORD *)previous = old;
        slot->Value = (PVOID)(ULONG_PTR)bits;
    } else {
        error = attribute_size_error(attribute, value, size);
        if (error) {
            KbSetError(error);
            return FALSE;
        }
        slot->Value = value;
    }
    if (bit) {
        slot->Attribute = attribute;
        slot->Size = size;
        list->Mask |= (DWORD)bit;
        list->MaskHigh |= (DWORD)(bit >> 32);
        list->Used++;
    }
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

#define KB_PROCESS_MITIGATION_POLICY_INFORMATION 52

typedef struct KB_MITIGATION_REQUEST {
    DWORD Policy, Flags;
} KB_MITIGATION_REQUEST;

BOOL WINAPI SetProcessMitigationPolicy(PROCESS_MITIGATION_POLICY policy, PVOID buffer, SIZE_T length)
{
    /* DEP is set by SetProcessDEPPolicy; the filters and payload restrictions only by creation. */
    switch ((int)policy) {
    case ProcessDEPPolicy:
        KbSetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    case ProcessASLRPolicy:
    case ProcessDynamicCodePolicy:
    case ProcessStrictHandleCheckPolicy:
    case ProcessSystemCallDisablePolicy:
    case ProcessExtensionPointDisablePolicy:
    case ProcessControlFlowGuardPolicy:
    case ProcessSignaturePolicy:
    case ProcessFontDisablePolicy:
    case ProcessImageLoadPolicy:
    case ProcessChildProcessPolicy:
    case ProcessSideChannelIsolationPolicy:
    case ProcessUserShadowStackPolicy:
    case ProcessRedirectionTrustPolicy:
        break;
    default:
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (length != 4) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KB_MITIGATION_REQUEST request = {(DWORD)policy, *(DWORD *)buffer};
    NTSTATUS status = NtSetInformationProcess(NtCurrentProcess(), (PROCESSINFOCLASS)KB_PROCESS_MITIGATION_POLICY_INFORMATION,
                                              &request, sizeof(request));
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

/*
 * the mask of the options a program can ask for: the same two words as the creation flags, a field set
 * to 3 where windows takes it. the words are made here, not by the kernel.
 */
static void mitigation_options_mask(ULONG64 *mask, SIZE_T length)
{
    static const ULONG64 template[2] = {0x3333333333333307ull, 0x0333333333333330ull};
    memset(mask, 0, length);
    SIZE_T fields = length * 8 / 4;
    for (SIZE_T i = 0; i < fields; i++) {
        SIZE_T word = i * 4 / 64, shift = i * 4 % 64;
        if (word < 2 && ((template[word] >> shift) & 3))
            mask[word] |= (ULONG64)(i < 40 ? 3 : 0) << shift;
    }
}

BOOL WINAPI GetProcessMitigationPolicy(HANDLE process, PROCESS_MITIGATION_POLICY policy, PVOID buffer, SIZE_T length)
{
    int klass = (int)policy;
    if (klass == ProcessMitigationOptionsMask) {
        if (length < 8) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        mitigation_options_mask(buffer, length);
        return TRUE;
    }
    if (klass == ProcessDEPPolicy) {
        DWORD execute = 0;
        if (length != 8) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        NTSTATUS status = NtQueryInformationProcess(process, ProcessExecuteFlags, &execute, sizeof(execute), nullptr);
        if (!NT_SUCCESS(status))
            return KbSetStatus(status);
        DWORD flags = 0;
        if (!(execute & 2))
            flags = (execute & 4) ? 3 : 1;
        *(DWORD *)buffer = flags;
        ((BYTE *)buffer)[4] = (execute & 8) != 0;
        return TRUE;
    }
    if (klass == ProcessSEHOPPolicy) {
        DWORD execute = 0;
        if (length != 4) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        NTSTATUS status = NtQueryInformationProcess(process, ProcessExecuteFlags, &execute, sizeof(execute), nullptr);
        if (!NT_SUCCESS(status))
            return KbSetStatus(status);
        *(DWORD *)buffer = !(execute & 0x40);
        return TRUE;
    }
    switch (klass) {
    case ProcessASLRPolicy:
    case ProcessDynamicCodePolicy:
    case ProcessStrictHandleCheckPolicy:
    case ProcessSystemCallDisablePolicy:
    case ProcessExtensionPointDisablePolicy:
    case ProcessControlFlowGuardPolicy:
    case ProcessSignaturePolicy:
    case ProcessFontDisablePolicy:
    case ProcessImageLoadPolicy:
    case ProcessSystemCallFilterPolicy:
    case ProcessPayloadRestrictionPolicy:
    case ProcessChildProcessPolicy:
    case ProcessSideChannelIsolationPolicy:
    case ProcessUserShadowStackPolicy:
    case ProcessRedirectionTrustPolicy:
    case ProcessUserPointerAuthPolicy:
        break;
    default:
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (length != 4) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KB_MITIGATION_REQUEST request = {(DWORD)klass, 0};
    NTSTATUS status = NtQueryInformationProcess(process, (PROCESSINFOCLASS)KB_PROCESS_MITIGATION_POLICY_INFORMATION,
                                                &request, sizeof(request), nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *(DWORD *)buffer = request.Flags;
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

