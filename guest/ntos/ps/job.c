/*
 * job objects: a set of processes, their limits and the completion port that hears about them. a process
 * belongs to the job of the process that created it unless it breaks away, and leaves it when it exits.
 * closing the last handle of a job with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE ends every process in it, as
 * programs that run children in a job expect.
 *
 * jobs nest, as on windows 8 and later: a process that is in a job and is assigned to another one makes the
 * second a child of the first (when it has no parent, no process and no child of its own), and goes on being
 * in the first. a process is in its job and every job above it: each job counts the processes of the jobs
 * below, hears of them on its port, ends them with its own end and keeps the limit of active processes over
 * all of them. limits are recorded and reported; enforced are the kill on close, the breakaway flags and the
 * limit of active processes. a process that would be one too many is not created, or not assigned, with
 * STATUS_QUOTA_EXCEEDED, and the port is told as windows does: the limit was hit, and the process that was
 * refused left.
 */
#include "../ntos.h"

#define JOB_OBJECT_ASSIGN_PROCESS 0x0001
#define JOB_OBJECT_SET_ATTRIBUTES 0x0002
#define JOB_OBJECT_QUERY 0x0004
#define JOB_OBJECT_TERMINATE 0x0008
#define JOB_OBJECT_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3F)

#define JOB_OBJECT_LIMIT_ACTIVE_PROCESS 0x00000008
#define JOB_OBJECT_LIMIT_VALID 0x00007FFF
#define JOB_OBJECT_LIMIT_BREAKAWAY_OK 0x00000800
#define JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK 0x00001000
#define JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE 0x00002000

#define JOB_OBJECT_MSG_ACTIVE_PROCESS_LIMIT 3
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
    LIST_ENTRY Processes;   /* the processes whose own job this is, not those of the jobs below */
    LIST_ENTRY Children;    /* the jobs below */
    LIST_ENTRY Siblings;    /* in the parent's Children */
    struct EJOB *Parent;    /* referenced */
    ULONG Active, Total, Terminated; /* of this job and the ones below it */
    JOB_EXTENDED_LIMIT Limits;
    ULONG UiRestrictions;
    PVOID Port; /* referenced io completion object */
    PVOID Key;
} EJOB;

POBJECT_TYPE PsJobType;

/* one lock for the shape of the jobs, their lists and their counts: they change rarely. */
static KSPIN_LOCK g_lock;

NTSTATUS IopQueueCompletion(PVOID port, PVOID key, PVOID apc_context, NTSTATUS status, ULONG_PTR information);

static void post(EJOB *job, ULONG message, PEPROCESS process)
{
    if (job->Port)
        IopQueueCompletion(job->Port, job->Key, process ? process->UniqueProcessId : nullptr, STATUS_SUCCESS, message);
}

/* a job by handle, which must grant the rights asked of a caller in user mode. */
static NTSTATUS reference_job(HANDLE handle, ACCESS_MASK access, EJOB **job)
{
    ACCESS_MASK granted = 0;
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    NTSTATUS status = ObReferenceHandle(handle, PsJobType, mode, (PVOID *)job, &granted);
    if (NT_SUCCESS(status) && mode == UserMode && (granted & access) != access) {
        ObDereferenceObject(*job);
        status = STATUS_ACCESS_DENIED;
    }
    return status;
}

#define JOB_CHAIN_MAX 16

/* the jobs from this one up to the root, at most JOB_CHAIN_MAX; call with the lock. */
static ULONG chain_of(EJOB *job, EJOB **chain)
{
    ULONG n = 0;
    for (; job && n < JOB_CHAIN_MAX; job = job->Parent)
        chain[n++] = job;
    return n;
}

/* every process of the job and the jobs below it, referenced; call with the lock. */
static void collect(EJOB *job, PEPROCESS *out, ULONG *n, ULONG max)
{
    for (PLIST_ENTRY e = job->Processes.Flink; e != &job->Processes && *n < max; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, JobLinks);
        ObReferenceObject(p);
        out[(*n)++] = p;
    }
    for (PLIST_ENTRY e = job->Children.Flink; e != &job->Children; e = e->Flink)
        collect(CONTAINING_RECORD(e, EJOB, Siblings), out, n, max);
}

static ULONG snapshot(EJOB *job, PEPROCESS *out, ULONG max)
{
    ULONG n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    collect(job, out, &n, max);
    KeReleaseSpinLock(&g_lock, irql);
    return n;
}

static void terminate_all(EJOB *job, NTSTATUS status)
{
    PEPROCESS processes[256];
    ULONG n = snapshot(job, processes, 256);
    for (ULONG i = 0; i < n; i++) {
        if (processes[i] != PsCurrentProcess())
            PspTerminateProcess(processes[i], status);
        /* only the processes the job ended are counted, not those that ended by themselves. */
        __atomic_add_fetch(&job->Terminated, 1, __ATOMIC_RELAXED);
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
    if (job->Parent) {
        KIRQL irql;
        KeAcquireSpinLock(&g_lock, &irql);
        RemoveEntryList(&job->Siblings);
        KeReleaseSpinLock(&g_lock, irql);
        ObDereferenceObject(job->Parent);
    }
    if (job->Port)
        ObDereferenceObject(job->Port);
}

void PspInitializeJobs(void)
{
    KeInitializeSpinLock(&g_lock);
    OBJECT_TYPE_INITIALIZER init = {};
    init.Waitable = TRUE;
    init.ValidAccessMask = JOB_OBJECT_ALL_ACCESS;
    init.CloseProcedure = job_close;
    init.DeleteProcedure = job_delete;
    ObSetGenericMapping(&init, 0x20004, 0x2000B, 0x120000, 0x1F003F);
    PsJobType = ObCreateObjectType(L"Job", &init);
}

/*
 * puts a process in a job. one that is in a job goes down into a job below its own, or a job that has nothing
 * yet hangs from its own; any other job is refused with STATUS_ACCESS_DENIED, so is the same job twice. the jobs
 * the process newly enters, from the job up to but not including the one it was in, each count it, so a job
 * at its limit of processes refuses it.
 */
static NTSTATUS join(EJOB *job, PEPROCESS process)
{
    EJOB *chain[JOB_CHAIN_MAX];
    ULONG entered = 0;
    BOOLEAN adopted = FALSE;
    NTSTATUS status = STATUS_SUCCESS;
    EJOB *refused = nullptr;
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    EJOB *old = process->Job;
    if (old == job) {
        status = STATUS_ACCESS_DENIED;
    } else {
        ULONG all = chain_of(job, chain);
        if (old) {
            ULONG at = 0;
            while (at < all && chain[at] != old)
                at++;
            if (at < all) {
                entered = at;
            } else if (!job->Parent && !job->Active && IsListEmpty(&job->Children) && all == 1) {
                /* the job comes under the one the process is in. */
                entered = 1;
                adopted = TRUE;
            } else {
                status = STATUS_ACCESS_DENIED;
            }
        } else {
            entered = all;
        }
        for (ULONG i = 0; NT_SUCCESS(status) && i < entered; i++) {
            if ((chain[i]->Limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_ACTIVE_PROCESS) &&
                chain[i]->Active >= chain[i]->Limits.BasicLimitInformation.ActiveProcessLimit) {
                status = STATUS_QUOTA_EXCEEDED;
                refused = chain[i];
            }
        }
        if (NT_SUCCESS(status)) {
            if (adopted) {
                ObReferenceObject(old);
                job->Parent = old;
                InsertTailList(&old->Children, &job->Siblings);
            }
            for (ULONG i = 0; i < entered; i++) {
                chain[i]->Active++;
                chain[i]->Total++;
            }
            ObReferenceObject(job);
            if (old)
                RemoveEntryList(&process->JobLinks);
            InsertTailList(&job->Processes, &process->JobLinks);
            process->Job = job;
        }
    }
    KeReleaseSpinLock(&g_lock, irql);
    if (refused)
        post(refused, JOB_OBJECT_MSG_ACTIVE_PROCESS_LIMIT, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    if (old)
        ObDereferenceObject(old);
    for (ULONG i = 0; i < entered; i++)
        post(chain[i], JOB_OBJECT_MSG_NEW_PROCESS, process);
    return STATUS_SUCCESS;
}

/*
 * a new process joins its creator's job, unless it may break away and asked to, or must. a job at its
 * limit refuses it, and then it has left again as far as the port is concerned.
 */
NTSTATUS PspInheritJob(PEPROCESS process, PEPROCESS parent, ULONG flags)
{
    EJOB *job = parent ? parent->Job : nullptr;
    if (!job)
        return STATUS_SUCCESS;
    ULONG limits = job->Limits.BasicLimitInformation.LimitFlags;
    if (limits & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)
        return STATUS_SUCCESS;
    if ((flags & PROCESS_CREATE_FLAGS_BREAKAWAY) && (limits & JOB_OBJECT_LIMIT_BREAKAWAY_OK))
        return STATUS_SUCCESS;
    NTSTATUS status = join(job, process);
    if (!NT_SUCCESS(status))
        post(job, JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS, process);
    return status;
}

/* the job a list of handles puts a new process in, which is the last, and every one must take it. */
NTSTATUS PspReferenceJobForCreation(const HANDLE *list, ULONG count, struct EJOB **out)
{
    EJOB *job = nullptr;
    for (ULONG i = 0; i < count; i++) {
        EJOB *next;
        NTSTATUS status = reference_job(list[i], JOB_OBJECT_ASSIGN_PROCESS, &next);
        if (job)
            ObDereferenceObject(job);
        if (!NT_SUCCESS(status))
            return status;
        job = next;
    }
    *out = (struct EJOB *)job;
    return STATUS_SUCCESS;
}

NTSTATUS PspAssignNewProcessToJob(struct EJOB *job, PEPROCESS process)
{
    NTSTATUS status = join((EJOB *)job, process);
    if (!NT_SUCCESS(status))
        post((EJOB *)job, JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS, process);
    return status;
}

/* the last thread of a process left: its job and every one above count it out. */
void PspJobProcessExit(PEPROCESS process)
{
    EJOB *chain[JOB_CHAIN_MAX];
    BOOLEAN emptied[JOB_CHAIN_MAX];
    ULONG n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    EJOB *leaf = process->Job;
    if (leaf) {
        n = chain_of(leaf, chain);
        for (ULONG i = 0; i < n; i++)
            emptied[i] = --chain[i]->Active == 0;
        RemoveEntryList(&process->JobLinks);
        process->Job = nullptr;
    }
    KeReleaseSpinLock(&g_lock, irql);
    if (!leaf)
        return;
    BOOLEAN abnormal = ((ULONG)process->ExitStatus & 0xC0000000u) == 0xC0000000u;
    for (ULONG i = 0; i < n; i++) {
        post(chain[i], abnormal ? JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS : JOB_OBJECT_MSG_EXIT_PROCESS, process);
        if (emptied[i])
            post(chain[i], JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO, nullptr);
    }
    ObDereferenceObject(leaf);
}

static void init_job(EJOB *job)
{
    memset(job, 0, sizeof(*job));
    KeInitializeEvent(&job->Event, NotificationEvent, FALSE);
    InitializeListHead(&job->Processes);
    InitializeListHead(&job->Children);
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
    NTSTATUS status = reference_job(job_handle, JOB_OBJECT_ASSIGN_PROCESS, &job);
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS process;
    status = PsReferenceProcessHandle(process_handle, PROCESS_SET_QUOTA | PROCESS_TERMINATE, &process);
    if (NT_SUCCESS(status)) {
        if (process->Terminating || process->SystemProcess)
            status = STATUS_PROCESS_IS_TERMINATING;
        else
            status = join(job, process);
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
        status = reference_job(job_handle, JOB_OBJECT_QUERY, &job);
    if (NT_SUCCESS(status)) {
        /* in the job when it is the process's own or one above it. */
        BOOLEAN in = FALSE;
        KIRQL irql;
        KeAcquireSpinLock(&g_lock, &irql);
        for (EJOB *j = process->Job; j; j = j->Parent)
            if (!job || j == job)
                in = TRUE;
        KeReleaseSpinLock(&g_lock, irql);
        status = in ? STATUS_PROCESS_IN_JOB : STATUS_PROCESS_NOT_IN_JOB;
    }
    if (job)
        ObDereferenceObject(job);
    ObDereferenceObject(process);
    return status;
}

NTSTATUS NTAPI NtTerminateJobObject(HANDLE job_handle, NTSTATUS exit_status)
{
    EJOB *job;
    NTSTATUS status = reference_job(job_handle, JOB_OBJECT_TERMINATE, &job);
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
    NTSTATUS status = reference_job(job_handle, JOB_OBJECT_SET_ATTRIBUTES, &job);
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
            if (in.basic.LimitFlags & ~JOB_OBJECT_LIMIT_VALID)
                status = STATUS_INVALID_PARAMETER;
            else
                job->Limits.BasicLimitInformation = in.basic;
            break;
        case JobObjectExtendedLimitInformation:
            if (in.extended.BasicLimitInformation.LimitFlags & ~JOB_OBJECT_LIMIT_VALID) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
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
        status = reference_job(job_handle, JOB_OBJECT_QUERY, &job);
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
