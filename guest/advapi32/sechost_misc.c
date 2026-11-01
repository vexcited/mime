/*
 * event trace consumers and the isolation functions of sechost.dll. a consumer opens the event log file or session of a
 * trace, and the machine has neither a trace session nor the parser of the log format. containers that isolate a
 * process need app container profiles, which it does not have either.
 */
#define MIME_BUILD_ADVAPI32 1
#include "advapi32_private.h"

#define INVALID_PROCESSTRACE_HANDLE_ ((ULONGLONG)-1)
#define EVENT_TRACE_REAL_TIME_MODE_ 0x00000100

/* the start of EVENT_TRACE_LOGFILE: the file and session names, then the mode of the log. */
typedef struct TRACE_LOGFILEW_START {
    PWSTR LogFileName;
    PWSTR LoggerName;
    LONGLONG CurrentTime;
    ULONG BuffersRead;
    ULONG LogFileMode;
} TRACE_LOGFILEW_START;

typedef struct TRACE_LOGFILEA_START {
    PSTR LogFileName;
    PSTR LoggerName;
    LONGLONG CurrentTime;
    ULONG BuffersRead;
    ULONG LogFileMode;
} TRACE_LOGFILEA_START;

static ULONGLONG trace_failed(DWORD error)
{
    SetLastError(error);
    return INVALID_PROCESSTRACE_HANDLE_;
}

/* a log file must exist to be opened; reading its format is beyond this machine. */
static ULONGLONG open_trace_file(PCWSTR name)
{
    if (!name)
        return trace_failed(ERROR_INVALID_PARAMETER);
    HANDLE file = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return trace_failed(GetLastError());
    CloseHandle(file);
    return trace_failed(ERROR_NOT_SUPPORTED);
}

ADVAPI ULONGLONG WINAPI OpenTraceW(TRACE_LOGFILEW_START *logfile)
{
    if (!logfile)
        return trace_failed(ERROR_INVALID_PARAMETER);
    /* a real time trace needs the session, and no session can be started. */
    if (logfile->LogFileMode & EVENT_TRACE_REAL_TIME_MODE_)
        return trace_failed(ERROR_NOT_SUPPORTED);
    return open_trace_file(logfile->LogFileName);
}

ADVAPI ULONGLONG WINAPI OpenTraceA(TRACE_LOGFILEA_START *logfile)
{
    if (!logfile)
        return trace_failed(ERROR_INVALID_PARAMETER);
    if (logfile->LogFileMode & EVENT_TRACE_REAL_TIME_MODE_)
        return trace_failed(ERROR_NOT_SUPPORTED);
    PWSTR name = AdvAnsiToWide(logfile->LogFileName);
    if (logfile->LogFileName && !name)
        return trace_failed(ERROR_NOT_ENOUGH_MEMORY);
    ULONGLONG result = open_trace_file(name);
    DWORD error = GetLastError();
    AdvFree(name);
    SetLastError(error);
    return result;
}

ADVAPI ULONGLONG WINAPI OpenTraceFromFile(PCWSTR name, PVOID options, PVOID header)
{
    (void)header;
    if (!options)
        return trace_failed(ERROR_INVALID_PARAMETER);
    return open_trace_file(name);
}

ADVAPI ULONGLONG WINAPI OpenTraceFromBufferStream(PVOID options, PVOID callback, PVOID context)
{
    (void)context;
    if (!options || !callback)
        return trace_failed(ERROR_INVALID_PARAMETER);
    return trace_failed(ERROR_NOT_SUPPORTED);
}

ADVAPI ULONGLONG WINAPI OpenTraceFromRealTimeLoggerWithAllocationOptions(PCWSTR name, PVOID options, ULONG_PTR a3,
                                                                         ULONG_PTR a4, PVOID header)
{
    (void)a3;
    (void)a4;
    (void)header;
    if (!name)
        return trace_failed(ERROR_INVALID_NAME);
    if (!options)
        return trace_failed(ERROR_INVALID_PARAMETER);
    return trace_failed(ERROR_NOT_SUPPORTED);
}

ADVAPI ULONGLONG WINAPI OpenTraceFromRealTimeLogger(PCWSTR name, PVOID options, PVOID header)
{
    return OpenTraceFromRealTimeLoggerWithAllocationOptions(name, options, 0, 0, header);
}

/* ------------------------------------------------------------------ isolation containers */

/* what an isolation container holds that is allocated: its name and the sids it grants. */
typedef struct ISOLATION_CONTAINER_ {
    PWSTR Name;
    DWORD Reserved;
    DWORD Kind;
    PVOID Policy;
    SID_AND_ATTRIBUTES *Sids;
    DWORD SidCount;
} ISOLATION_CONTAINER_;

ADVAPI NTSTATUS WINAPI FreeContainer(ISOLATION_CONTAINER_ *container)
{
    if (container->Name) {
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, container->Name);
        container->Name = nullptr;
    }
    for (DWORD i = 0; container->Sids && i < container->SidCount; i++)
        if (container->Sids[i].Sid)
            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, container->Sids[i].Sid);
    if (container->Sids)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, container->Sids);
    container->Sids = nullptr;
    container->SidCount = 0;
    return STATUS_SUCCESS;
}

/* the manifests of the containers name app container profiles, and profiles are not available. */
ADVAPI BOOL WINAPI CreateIsolationContainer(PCWSTR a1, PCWSTR a2, PCWSTR container, PVOID options)
{
    (void)a1;
    (void)a2;
    (void)options;
    SetLastError(container && *container ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
    return FALSE;
}

ADVAPI BOOL WINAPI DeleteIsolationContainer(PCWSTR a1, PCWSTR a2, PCWSTR container)
{
    (void)a1;
    (void)a2;
    SetLastError(container && *container ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
    return FALSE;
}

ADVAPI BOOL WINAPI CreateIsolatedProcess(PCWSTR a1, PCWSTR a2, PCWSTR container, PCWSTR command_line, PVOID options,
                                         PVOID process_options, PVOID process_information)
{
    (void)a1;
    (void)a2;
    (void)command_line;
    (void)options;
    (void)process_options;
    (void)process_information;
    SetLastError(container && *container ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
    return FALSE;
}

/* the policies that come with the container manifest are read by a parser that is not here. */
ADVAPI NTSTATUS WINAPI GetEmbeddedContainerIsolationPolicy(PUNICODE_STRING manifest, PVOID container)
{
    (void)container;
    if (!manifest || !manifest->Buffer || !manifest->Length)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

ADVAPI NTSTATUS WINAPI GetEmbeddedImageMitigationPolicy(PUNICODE_STRING manifest, PVOID options, PVOID filter, int *found)
{
    (void)options;
    (void)filter;
    if (!manifest)
        return STATUS_INVALID_PARAMETER;
    if (!manifest->Buffer || !manifest->Length) {
        *found = 0;
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}
