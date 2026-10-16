/*
 * job objects: a set of processes, their limits and the completion port
 * that hears about them. a process belongs to the job of the process that
 * created it unless it breaks away, and leaves it when it exits. closing
 * the last handle of a job with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE ends
 * every process in it, as programs that run children in a job expect.
 *
 * nested jobs are flattened: a process assigned to a second job moves to
 * it. limits are recorded and reported but not enforced, except the kill
 * on close and the breakaway flags.
 */
#include "../ntos.h"

#define JOB_OBJECT_ASSIGN_PROCESS 0x0001
#define JOB_OBJECT_SET_ATTRIBUTES 0x0002
#define JOB_OBJECT_QUERY 0x0004
#define JOB_OBJECT_TERMINATE 0x0008
#define JOB_OBJECT_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3F)

#define JOB_OBJECT_LIMIT_BREAKAWAY_OK 0x00000800
#define JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK 0x00001000
#define JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE 0x00002000

#define JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO 4
#define JOB_OBJECT_MSG_NEW_PROCESS 6
#define JOB_OBJECT_MSG_EXIT_PROCESS 7
#define JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS 8

enum {
    JobObjectBasicAccountingInformation = 1,
    JobObjectBasicLimitInformation = 2,
    JobObjectBasicProcessIdList = 3,
    JobObjectBasicUIRestrictions = 4,
    JobObjectAssociateCompletionPortInformation = 7,
    JobObjectBasicAndIoAccountingInformation = 8,
    JobObjectExtendedLimitInformation = 9,
};

typedef struct JOB_BASIC_ACCOUNTING {
    LARGE_INTEGER TotalUserTime, TotalKernelTime, ThisPeriodTotalUserTime, ThisPeriodTotalKernelTime;
    ULONG TotalPageFaultCount, TotalProcesses, ActiveProcesses, TotalTerminatedProcesses;
} JOB_BASIC_ACCOUNTING;

typedef struct JOB_BASIC_LIMIT {
    LARGE_INTEGER PerProcessUserTimeLimit, PerJobUserTimeLimit;
    ULONG LimitFlags;
    SIZE_T MinimumWorkingSetSize, MaximumWorkingSetSize;
    ULONG ActiveProcessLimit;
    ULONG_PTR Affinity;
    ULONG PriorityClass, SchedulingClass;
} JOB_BASIC_LIMIT;

typedef struct JOB_IO_COUNTERS {
    ULONGLONG ReadOperationCount, WriteOperationCount, OtherOperationCount;
    ULONGLONG ReadTransferCount, WriteTransferCount, OtherTransferCount;
} JOB_IO_COUNTERS;

typedef struct JOB_EXTENDED_LIMIT {
    JOB_BASIC_LIMIT BasicLimitInformation;
    JOB_IO_COUNTERS IoInfo;
    SIZE_T ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
} JOB_EXTENDED_LIMIT;

typedef struct JOB_COMPLETION_PORT {
    PVOID CompletionKey;
    HANDLE CompletionPort;
} JOB_COMPLETION_PORT;

typedef struct EJOB {
    KEVENT Event; /* waitable, as on windows; signaled only by end of job time, which never comes */
    KSPIN_LOCK Lock;
    LIST_ENTRY Processes;
    ULONG Active, Total, Terminated;
    JOB_EXTENDED_LIMIT Limits;
    ULONG UiRestrictions;
    PVOID Port; /* referenced io completion object */
    PVOID Key;
} EJOB;

POBJECT_TYPE PsJobType;

NTSTATUS IopQueueCompletion(PVOID port, PVOID key, PVOID apc_context, NTSTATUS status, ULONG_PTR information);

static void post(EJOB *job, ULONG message, PEPROCESS process)
{
    if (job->Port)
        IopQueueCompletion(job->Port, job->Key, process ? process->UniqueProcessId : nullptr, STATUS_SUCCESS, message);
}

/* every process of the job, referenced. */
static ULONG snapshot(EJOB *job, PEPROCESS *out, ULONG max)
{
    ULONG n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&job->Lock, &irql);
    for (PLIST_ENTRY e = job->Processes.Flink; e != &job->Processes && n < max; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, JobLinks);
        ObReferenceObject(p);
        out[n++] = p;
    }
    KeReleaseSpinLock(&job->Lock, irql);
    return n;
}

static void terminate_all(EJOB *job, NTSTATUS status)
{
    PEPROCESS processes[256];
    ULONG n = snapshot(job, processes, 256);
    for (ULONG i = 0; i < n; i++) {
        if (processes[i] != PsCurrentProcess())
            PspTerminateProcess(processes[i], status);
        ObDereferenceObject(processes[i]);
    }
}

static void job_close(PEPROCESS process, PVOID object, ULONG handle_count)
{
    EJOB *job = object;
    if (handle_count == 1 && (job->Limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE))
        terminate_all(job, STATUS_SUCCESS);
}

static void job_delete(PVOID object)
{
    EJOB *job = object;
    if (job->Port)
        ObDereferenceObject(job->Port);
}

void PspInitializeJobs(void)
{
    OBJECT_TYPE_INITIALIZER init = {};
    init.Waitable = TRUE;
    init.ValidAccessMask = JOB_OBJECT_ALL_ACCESS;
    init.CloseProcedure = job_close;
    init.DeleteProcedure = job_delete;
    PsJobType = ObCreateObjectType(L"Job", &init);
}

/* puts a process in a job, leaving the one it was in. */
static void join(EJOB *job, PEPROCESS process)
{
    EJOB *old = process->Job;
    if (old == job)
        return;
    KIRQL irql;
    if (old) {
        KeAcquireSpinLock(&old->Lock, &irql);
        RemoveEntryList(&process->JobLinks);
        old->Active--;
        KeReleaseSpinLock(&old->Lock, irql);
        ObDereferenceObject(old);
    }
    ObReferenceObject(job);
    KeAcquireSpinLock(&job->Lock, &irql);
    InsertTailList(&job->Processes, &process->JobLinks);
    job->Active++;
    job->Total++;
    process->Job = job;
    KeReleaseSpinLock(&job->Lock, irql);
    post(job, JOB_OBJECT_MSG_NEW_PROCESS, process);
}

/* a new process joins its creator's job, unless it may break away and asked to, or must. */
void PspInheritJob(PEPROCESS process, PEPROCESS parent, ULONG flags)
{
    EJOB *job = parent ? parent->Job : nullptr;
    if (!job)
        return;
    ULONG limits = job->Limits.BasicLimitInformation.LimitFlags;
    if (limits & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)
        return;
    if ((flags & PROCESS_CREATE_FLAGS_BREAKAWAY) && (limits & JOB_OBJECT_LIMIT_BREAKAWAY_OK))
        return;
    join(job, process);
}

/* the last thread of a process left: the job counts it out. */
void PspJobProcessExit(PEPROCESS process)
{
    EJOB *job = process->Job;
    if (!job)
        return;
    KIRQL irql;
    KeAcquireSpinLock(&job->Lock, &irql);
    RemoveEntryList(&process->JobLinks);
    BOOLEAN last = --job->Active == 0;
    job->Terminated++;
    process->Job = nullptr;
    KeReleaseSpinLock(&job->Lock, irql);
    BOOLEAN abnormal = ((ULONG)process->ExitStatus & 0xC0000000u) == 0xC0000000u;
    post(job, abnormal ? JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS : JOB_OBJECT_MSG_EXIT_PROCESS, process);
    if (last)
        post(job, JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO, nullptr);
    ObDereferenceObject(job);
}

static void init_job(EJOB *job)
{
    memset(job, 0, sizeof(*job));
    KeInitializeEvent(&job->Event, NotificationEvent, FALSE);
    KeInitializeSpinLock(&job->Lock);
    InitializeListHead(&job->Processes);
}

NTSTATUS NTAPI NtCreateJobObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    EJOB *job;
    status = ObCreateObject(PsJobType, attributes ? &oa : nullptr, KeGetPreviousMode(), sizeof(*job), (PVOID *)&job);
    if (NT_SUCCESS(status)) {
        init_job(job);
        HANDLE h;
        status = ObInsertObjectNamed(job, attributes ? &oa : nullptr, access, KeGetPreviousMode(), &h);
        if (NT_SUCCESS(status)) {
            NTSTATUS st = ObStoreHandle(handle, h);
            if (!NT_SUCCESS(st))
                status = st;
        }
    }
    KiFreeCapturedString(&name);
    return status;
}

NTSTATUS NTAPI NtOpenJobObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    HANDLE h;
    status = ObOpenObjectByName(&oa, PsJobType, KeGetPreviousMode(), access, nullptr, &h);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    KiFreeCapturedString(&name);
    return status;
}

NTSTATUS NTAPI NtAssignProcessToJobObject(HANDLE job_handle, HANDLE process_handle)
{
    EJOB *job;
    NTSTATUS status = ObReferenceHandle(job_handle, PsJobType, KeGetPreviousMode(), (PVOID *)&job, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS process;
    status = PsReferenceProcessHandle(process_handle, PROCESS_SET_QUOTA | PROCESS_TERMINATE, &process);
    if (NT_SUCCESS(status)) {
        if (process->Terminating || process->SystemProcess)
            status = STATUS_PROCESS_IS_TERMINATING;
        else
            join(job, process);
        ObDereferenceObject(process);
    }
    ObDereferenceObject(job);
    return status;
}

NTSTATUS NTAPI NtIsProcessInJob(HANDLE process_handle, HANDLE job_handle)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_QUERY_LIMITED_INFORMATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    EJOB *job = nullptr;
    if (job_handle)
        status = ObReferenceHandle(job_handle, PsJobType, KeGetPreviousMode(), (PVOID *)&job, nullptr);
    if (NT_SUCCESS(status))
        status = process->Job && (!job || process->Job == job) ? STATUS_PROCESS_IN_JOB : STATUS_PROCESS_NOT_IN_JOB;
    if (job)
        ObDereferenceObject(job);
    ObDereferenceObject(process);
    return status;
}

NTSTATUS NTAPI NtTerminateJobObject(HANDLE job_handle, NTSTATUS exit_status)
{
    EJOB *job;
    NTSTATUS status = ObReferenceHandle(job_handle, PsJobType, KeGetPreviousMode(), (PVOID *)&job, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    terminate_all(job, exit_status);
    BOOLEAN self = PsCurrentProcess()->Job == job;
    ObDereferenceObject(job);
    if (self) {
        PspTerminateProcess(PsCurrentProcess(), exit_status);
        PspExitThread(exit_status);
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtSetInformationJobObject(HANDLE job_handle, ULONG klass, PVOID info, ULONG length)
{
    EJOB *job;
    NTSTATUS status = ObReferenceHandle(job_handle, PsJobType, KeGetPreviousMode(), (PVOID *)&job, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    union {
        JOB_BASIC_LIMIT basic;
        JOB_EXTENDED_LIMIT extended;
        JOB_COMPLETION_PORT port;
        ULONG ui;
    } in;
    ULONG want;
    switch (klass) {
    case JobObjectBasicLimitInformation: want = sizeof(in.basic); break;
    case JobObjectExtendedLimitInformation: want = sizeof(in.extended); break;
    case JobObjectAssociateCompletionPortInformation: want = sizeof(in.port); break;
    case JobObjectBasicUIRestrictions: want = sizeof(in.ui); break;
    default:
        /* cpu rates, notification limits and the like are accepted and not enforced. */
        ObDereferenceObject(job);
        return STATUS_SUCCESS;
    }
    if (length < want)
        status = STATUS_INFO_LENGTH_MISMATCH;
    else
        status = KiCopyFromUser(&in, info, want);
    if (NT_SUCCESS(status)) {
        switch (klass) {
        case JobObjectBasicLimitInformation:
            job->Limits.BasicLimitInformation = in.basic;
            break;
        case JobObjectExtendedLimitInformation:
            job->Limits.BasicLimitInformation = in.extended.BasicLimitInformation;
            job->Limits.ProcessMemoryLimit = in.extended.ProcessMemoryLimit;
            job->Limits.JobMemoryLimit = in.extended.JobMemoryLimit;
            break;
        case JobObjectAssociateCompletionPortInformation: {
            PVOID port;
            if (job->Port) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            status = ObReferenceHandle(in.port.CompletionPort, IoCompletionObjectType, KeGetPreviousMode(), &port,
                                       nullptr);
            if (NT_SUCCESS(status)) {
                job->Port = port;
                job->Key = in.port.CompletionKey;
                /* the processes already in the job are announced, as windows does. */
                PEPROCESS processes[256];
                ULONG n = snapshot(job, processes, 256);
                for (ULONG i = 0; i < n; i++) {
                    post(job, JOB_OBJECT_MSG_NEW_PROCESS, processes[i]);
                    ObDereferenceObject(processes[i]);
                }
            }
            break;
        }
        case JobObjectBasicUIRestrictions:
            job->UiRestrictions = in.ui;
            break;
        }
    }
    ObDereferenceObject(job);
    return status;
}

NTSTATUS NTAPI NtQueryInformationJobObject(HANDLE job_handle, ULONG klass, PVOID info, ULONG length,
                                           PULONG returned)
{
    EJOB *job = nullptr;
    NTSTATUS status;
    if (job_handle) {
        status = ObReferenceHandle(job_handle, PsJobType, KeGetPreviousMode(), (PVOID *)&job, nullptr);
    } else {
        /* no handle asks about the job of the calling process. */
        job = PsCurrentProcess()->Job;
        status = job ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
        if (job)
            ObReferenceObject(job);
    }
    if (!NT_SUCCESS(status))
        return status;
    JOB_BASIC_ACCOUNTING accounting = {};
    accounting.TotalProcesses = job->Total;
    accounting.ActiveProcesses = job->Active;
    accounting.TotalTerminatedProcesses = job->Terminated;
    union {
        JOB_BASIC_ACCOUNTING accounting;
        JOB_BASIC_LIMIT basic;
        JOB_EXTENDED_LIMIT extended;
        struct {
            JOB_BASIC_ACCOUNTING basic;
            JOB_IO_COUNTERS io;
        } both;
        ULONG ui;
        struct {
            ULONG Assigned, Listed;
            ULONG_PTR Ids[256];
        } ids;
    } out;
    memset(&out, 0, sizeof(out));
    ULONG size = 0;
    switch (klass) {
    case JobObjectBasicAccountingInformation:
        out.accounting = accounting;
        size = sizeof(out.accounting);
        break;
    case JobObjectBasicAndIoAccountingInformation:
        out.both.basic = accounting;
        size = sizeof(out.both);
        break;
    case JobObjectBasicLimitInformation:
        out.basic = job->Limits.BasicLimitInformation;
        size = sizeof(out.basic);
        break;
    case JobObjectExtendedLimitInformation:
        out.extended = job->Limits;
        size = sizeof(out.extended);
        break;
    case JobObjectBasicUIRestrictions:
        out.ui = job->UiRestrictions;
        size = sizeof(out.ui);
        break;
    case JobObjectBasicProcessIdList: {
        PEPROCESS processes[256];
        ULONG n = snapshot(job, processes, 256);
        ULONG room = length >= 8 ? (length - 8) / sizeof(ULONG_PTR) : 0;
        out.ids.Assigned = n;
        out.ids.Listed = n < room ? n : room;
        for (ULONG i = 0; i < n; i++) {
            out.ids.Ids[i] = (ULONG_PTR)processes[i]->UniqueProcessId;
            ObDereferenceObject(processes[i]);
        }
        size = 8 + out.ids.Listed * sizeof(ULONG_PTR);
        if (out.ids.Listed < n && length >= 8) {
            status = KiCopyToUser(info, &out, size);
            if (NT_SUCCESS(status) && returned)
                status = KiCopyToUser(returned, &size, sizeof(size));
            ObDereferenceObject(job);
            return NT_SUCCESS(status) ? STATUS_MORE_ENTRIES : status;
        }
        break;
    }
    default:
        ObDereferenceObject(job);
        return STATUS_INVALID_INFO_CLASS;
    }
    ObDereferenceObject(job);
    if (length < size)
        return STATUS_INFO_LENGTH_MISMATCH;
    status = KiCopyToUser(info, &out, size);
    if (NT_SUCCESS(status) && returned)
        status = KiCopyToUser(returned, &size, sizeof(size));
    return status;
}
