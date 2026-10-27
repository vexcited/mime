/*
 * session and machine shutdown. on windows winlogon starts it, csrss walks the processes from the highest shutdown
 * level down and has winsrv tell the windows of each, then the console handlers of what is left, before services
 * and the kernel finish. here the kernel does it all on one system thread, with a worker thread per process of a
 * level. the window manager's part is a callout.
 */
#include "../ntos.h"
#include "../cm/cm.h"
#include "../se/se.h"

#define CTRL_LOGOFF_EVENT 5
#define CTRL_SHUTDOWN_EVENT 6
#define MAX_BATCH 16
#define MAX_WAIT 5
/* the exit code csrss gives a process whose windows were told. */
#define STATUS_DBG_TERMINATE_PROCESS ((NTSTATUS)0x40010004L)
#define SHUTDOWN_PRIVILEGE 19

/* per process shutdown state: 0 not yet, 1 taken in a batch. */
#define PS_SHUTDOWN_TAKEN 1

typedef struct SHUTDOWN_SETTINGS {
    ULONG HungAppTimeout;
    ULONG WaitToKillTimeout;
    ULONG QuickTimeout;
    ULONG TerminateTimeout;
    ULONG ServiceTimeout; /* WaitToKillServiceTimeout. */
    BOOLEAN AutoEndTasks;
    BOOLEAN AllowBlocking;
} SHUTDOWN_SETTINGS;

static struct {
    FAST_MUTEX Lock;
    BOOLEAN Ready;
    BOOLEAN Active;  /* a request is pending or running. */
    BOOLEAN Pending; /* still waiting for its delay. */
    BOOLEAN Refused; /* a window answered no to the query. */
    ULONG Generation;
    BOOLEAN Stopping; /* the machine is going down. */
    ULONG Flags;
    ULONG Reason;
    ULONG_PTR Requester;     /* the process that asked, for the log. */
    CHAR RequesterName[16];
    ULONG Control;
    ULONG DelayMs;
    ULONG LimitMs; /* limit for the whole shutdown, 0 for none. */
    NTSTATUS Result;
    KEVENT Cancel;   /* the request was aborted. */
    KEVENT Expire;   /* time ran out: everything left ends now. */
    KEVENT Finished; /* the thread is done. */
    SHUTDOWN_SETTINGS Settings;
    ULONG64 Deadline; /* milliseconds of interrupt time, 0 for none. */
} g;

typedef struct BATCH {
    LONG Pending;
    KEVENT Done;
    ULONG Cancelled;
} BATCH;

typedef struct WORKER {
    BATCH *Batch;
    PEPROCESS Process;
} WORKER;

static ULONG64 now_ms(void)
{
    return KeQueryInterruptTime() / 10000;
}

void PsShutdownInitialize(void)
{
    ExInitializeFastMutex(&g.Lock);
    KeInitializeEvent(&g.Cancel, NotificationEvent, FALSE);
    KeInitializeEvent(&g.Expire, NotificationEvent, FALSE);
    KeInitializeEvent(&g.Finished, NotificationEvent, TRUE);
    g.Ready = TRUE;
}

/* ------------------------------------------------------------------ settings */

/* the timeout keys hold either a string or a dword. */
static ULONG read_number(PCWSTR key, PCWSTR name, ULONG fallback)
{
    UCHAR data[64];
    ULONG type = 0, length = sizeof(data) - sizeof(WCHAR);
    memset(data, 0, sizeof(data));
    if (!NT_SUCCESS(CmQueryValue(key, name, &type, data, &length)))
        return fallback;
    if (type == REG_DWORD && length >= sizeof(ULONG))
        return *(PULONG)data;
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        UNICODE_STRING text;
        ULONG value;
        RtlInitUnicodeString(&text, (PCWSTR)data);
        if (NT_SUCCESS(RtlUnicodeStringToInteger(&text, 10, &value)))
            return value;
    }
    return fallback;
}

#define KEY_DESKTOP_FORMAT L"\\REGISTRY\\USER\\S-1-5-21-%u-%u-%u-1001\\Control Panel\\Desktop"
#define KEY_CONTROL L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control"
#define KEY_POLICY L"\\REGISTRY\\MACHINE\\Software\\Policies\\Microsoft\\Windows\\System"
#define SERVICE_TIMEOUT_CAP 30000

/* read when a shutdown starts, with a default for each missing value. */
static void load_settings(SHUTDOWN_SETTINGS *s)
{
    WCHAR desktop[128];
    _snwprintf(desktop, RTL_NUMBER_OF(desktop) - 1, KEY_DESKTOP_FORMAT, HalBootInfo->user_sid[0],
               HalBootInfo->user_sid[1], HalBootInfo->user_sid[2]);
    desktop[RTL_NUMBER_OF(desktop) - 1] = 0;

    s->HungAppTimeout = read_number(desktop, L"HungAppTimeout", 5000);
    s->WaitToKillTimeout = read_number(desktop, L"WaitToKillAppTimeout", 20000);
    s->AutoEndTasks = read_number(desktop, L"AutoEndTasks", 0) != 0;
    s->QuickTimeout = read_number(desktop, L"CriticalAppShutdownTimeout", 500);
    if (!s->QuickTimeout)
        s->QuickTimeout = 500;

    s->TerminateTimeout = read_number(KEY_CONTROL, L"ProcessTerminateTimeout", 500);
    s->ServiceTimeout = read_number(KEY_CONTROL, L"WaitToKillServiceTimeout", 20000);
    if (s->ServiceTimeout > SERVICE_TIMEOUT_CAP)
        s->ServiceTimeout = SERVICE_TIMEOUT_CAP;
    s->AllowBlocking = read_number(KEY_POLICY, L"AllowBlockingAppsAtShutdown", 0) != 0;
}

/* ------------------------------------------------------------------ waiting */

static NTSTATUS wait_objects(PVOID *objects, ULONG count, ULONG64 timeout_ms)
{
    LARGE_INTEGER t;
    KWAIT_BLOCK blocks[MAX_WAIT];
    if (count > MAX_WAIT)
        return STATUS_INVALID_PARAMETER;
    t.QuadPart = -(LONGLONG)(timeout_ms * 10000);
    return KeWaitForMultipleObjects(count, objects, WaitAny, Executive, KernelMode, FALSE,
                                    timeout_ms == ~0ull ? nullptr : &t, blocks);
}

/* clips a wait to the shutdown's own limit. */
static ULONG64 allowed(ULONG64 wanted)
{
    if (!g.Deadline)
        return wanted;
    ULONG64 now = now_ms();
    ULONG64 left = g.Deadline > now ? g.Deadline - now : 0;
    return left < wanted ? left : wanted;
}

/* ------------------------------------------------------------------ one process */

static void kill_process(PEPROCESS process, NTSTATUS status)
{
    KiLogPrintf("shutdown: ending process %u (%s) level %x", (ULONG)(ULONG_PTR)process->UniqueProcessId,
                process->ImageFileName, process->ShutdownLevel);
    PspTerminateProcess(process, status);
    if (process->Exited)
        return;
    /* windows waits ProcessTerminateTimeout for it and goes on whatever happens. */
    PVOID objects[2] = {&process->Pcb, &g.Cancel};
    wait_objects(objects, 1, g.Settings.TerminateTimeout);
    if (!process->Exited)
        KiLogPrintf("shutdown: process %u (%s) is still there after %u ms", (ULONG)(ULONG_PTR)process->UniqueProcessId,
                    process->ImageFileName, g.Settings.TerminateTimeout);
}

/* extra time for the service control manager on top of its service timeout: services are asked to prepare first. */
#define SERVICE_PREPARE_ALLOWANCE_MS 10000
#define HANDLER_EXIT_GRACE_MS 1000

/*
 * a process with no window, or whose windows did not end it, gets its console handler run on a new thread. that
 * thread exiting with the event as its code means the handler took the event, and the process is then ended here.
 * a process with no handler, or that does not end in time, is ended at once.
 */
static void ctrl_shutdown(PEPROCESS process)
{
    ULONG event = (g.Flags & MX_EWX_MACHINE) ? CTRL_SHUTDOWN_EVENT : CTRL_LOGOFF_EVENT;
    PETHREAD handler = nullptr;
    PVOID routine = process->ConsoleCtrlRoutine;
    if (!routine || !NT_SUCCESS(PspCreateUserThread(process, routine, (PVOID)(ULONG_PTR)event, FALSE, 0, 0, &handler))) {
        kill_process(process, STATUS_CONTROL_C_EXIT);
        return;
    }

    ULONG64 wait = g.Settings.WaitToKillTimeout;
    if ((g.Flags & EWX_QUICKRESOLVE) && g.Settings.QuickTimeout < wait)
        wait = g.Settings.QuickTimeout;
    if (process->ServiceController)
        wait = (ULONG64)g.Settings.ServiceTimeout + SERVICE_PREPARE_ALLOWANCE_MS;

    enum { PROCESS_ENDED, HANDLER_ENDED };
    PVOID objects[4] = {&process->Pcb, &handler->Tcb, &g.Cancel, &g.Expire};
    NTSTATUS woken = wait_objects(objects, 4, allowed(wait));
    if (woken == STATUS_WAIT_0 + PROCESS_ENDED) {
        ObDereferenceObject(handler);
        return;
    }
    if (woken == STATUS_WAIT_0 + HANDLER_ENDED) {
        if ((ULONG)handler->ExitStatus == event) {
            ObDereferenceObject(handler);
            PspTerminateProcess(process, STATUS_CONTROL_C_EXIT);
            return;
        }
        /* the handler ended without taking the event: the program may still be leaving by itself. */
        PVOID leaving[1] = {&process->Pcb};
        wait_objects(leaving, 1, HANDLER_EXIT_GRACE_MS);
    }
    ObDereferenceObject(handler);
    if (!process->Exited)
        kill_process(process, STATUS_CONTROL_C_EXIT);
}

static void NTAPI process_worker(PVOID context)
{
    WORKER *w = context;
    PEPROCESS process = w->Process;
    MX_SHUTDOWN_CONTEXT ctx = {};
    ctx.Flags = g.Flags;
    ctx.HungTimeout = g.Settings.HungAppTimeout;
    ctx.KillTimeout = g.Settings.WaitToKillTimeout;
    ctx.QuickTimeout = g.Settings.QuickTimeout;
    ctx.AutoEndTasks = g.Settings.AutoEndTasks;
    ctx.AllowBlocking = g.Settings.AllowBlocking;
    ctx.Cancel = &g.Cancel;
    ctx.Expire = &g.Expire;
    ctx.Deadline = g.Deadline;
    ULONG result = PsWin32ShutdownProcess(process, &ctx);
    KiLogPrintf("shutdown: process %u (%s) level %x windows answered %u", (ULONG)(ULONG_PTR)process->UniqueProcessId,
                process->ImageFileName, process->ShutdownLevel, result);
    if (!process->Exited) {
        if (result == MX_SHUTDOWN_PROCESS_PASS)
            ctrl_shutdown(process);
        else if (result == MX_SHUTDOWN_PROCESS_DONE)
            kill_process(process, STATUS_DBG_TERMINATE_PROCESS);
        else if (result == MX_SHUTDOWN_PROCESS_CANCEL) {
            w->Batch->Cancelled = TRUE;
            g.Refused = TRUE;
            KeSetEvent(&g.Cancel, 0, FALSE);
        }
    }
    BATCH *batch = w->Batch;
    ObDereferenceObject(process);
    ExFreePool(w);
    if (__atomic_sub_fetch(&batch->Pending, 1, __ATOMIC_SEQ_CST) == 0)
        KeSetEvent(&batch->Done, 0, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* ------------------------------------------------------------------ levels */

/* session 0 processes, the services, take part only in a machine shutdown, not a logoff. */
static BOOLEAN takes_part(PEPROCESS p)
{
    return p->SessionId != 0 || (g.Flags & MX_EWX_MACHINE);
}

/* takes the processes of the highest level not yet taken, and marks them. */
static ULONG take_batch(PEPROCESS *out)
{
    ULONG count = 0, level = 0;
    ULONG64 s = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->SystemProcess || p->Terminating || p->Exited || p->ShutdownState || !takes_part(p))
            continue;
        if (!count || p->ShutdownLevel > level)
            level = p->ShutdownLevel;
        count++;
    }
    count = 0;
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead && count < MAX_BATCH; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->SystemProcess || p->Terminating || p->Exited || p->ShutdownState || p->ShutdownLevel != level ||
            !takes_part(p))
            continue;
        p->ShutdownState = PS_SHUTDOWN_TAKEN;
        ObReferenceObject(p);
        out[count++] = p;
    }
    KiReleaseDispatcherLock(s);
    return count;
}

static NTSTATUS run_batch(PEPROCESS *processes, ULONG count)
{
    BATCH batch = {};
    batch.Pending = (LONG)count;
    KeInitializeEvent(&batch.Done, NotificationEvent, FALSE);
    for (ULONG i = 0; i < count; i++) {
        WORKER *w = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*w), KI_TAG('S', 'h', 'W', 'k'));
        if (w) {
            w->Batch = &batch;
            w->Process = processes[i];
            if (NT_SUCCESS(PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, process_worker, w)))
                continue;
            ExFreePool(w);
        }
        PspTerminateProcess(processes[i], STATUS_CONTROL_C_EXIT);
        ObDereferenceObject(processes[i]);
        if (__atomic_sub_fetch(&batch.Pending, 1, __ATOMIC_SEQ_CST) == 0)
            KeSetEvent(&batch.Done, 0, FALSE);
    }
    PVOID objects[2] = {&batch.Done, &g.Expire};
    if (wait_objects(objects, KeReadStateEvent(&g.Expire) ? 1 : 2, ~0ull) != STATUS_WAIT_0) {
        /* time is up: the workers end what they wait for and finish soon. */
        PVOID done[1] = {&batch.Done};
        if (wait_objects(done, 1, 10000) != STATUS_WAIT_0) {
            for (ULONG i = 0; i < count; i++)
                if (!processes[i]->Exited)
                    PspTerminateProcess(processes[i], STATUS_SYSTEM_SHUTDOWN);
            wait_objects(done, 1, 10000);
        }
    }
    return batch.Cancelled ? STATUS_CANCELLED : STATUS_SUCCESS;
}

/* arms the limit of the whole shutdown, which ends every wait. */
static void NTAPI limit_thread(PVOID context)
{
    ULONG64 limit = (ULONG64)(ULONG_PTR)context;
    ULONG generation = g.Generation;
    PVOID objects[1] = {&g.Finished};
    NTSTATUS status = wait_objects(objects, 1, limit);
    if (status == STATUS_TIMEOUT && generation == g.Generation) {
        KiLogPrintf("shutdown: %u ms are over, what is left ends now", (ULONG)limit);
        KeSetEvent(&g.Expire, 0, FALSE);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* processes still alive are to be asked again. */
static void reset_taken(void)
{
    ULONG64 s = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead; e = e->Flink)
        CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks)->ShutdownState = 0;
    KiReleaseDispatcherLock(s);
}

static NTSTATUS user_phase(void)
{
    PEPROCESS batch[MAX_BATCH];
    if (g.Control & MX_SHUTDOWN_IMMEDIATE)
        return STATUS_SUCCESS;
    ULONG count;
    while ((count = take_batch(batch)) != 0) {
        NTSTATUS status = run_batch(batch, count);
        if (status == STATUS_CANCELLED)
            return status;
        if (KeReadStateEvent(&g.Cancel))
            return STATUS_CANCELLED;
    }
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ the machine */

/* ends what is left and waits a while, so that handles get closed and files written. */
static void end_everything(void)
{
    ULONG64 start = now_ms();
    for (;;) {
        PEPROCESS wait_for = nullptr;
        ULONG64 s = KiAcquireDispatcherLock();
        for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead; e = e->Flink) {
            PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
            if (p->SystemProcess || p->Exited)
                continue;
            if (!p->Terminating)
                PspTerminateProcess(p, STATUS_SYSTEM_SHUTDOWN);
            if (!wait_for) {
                wait_for = p;
                ObReferenceObject(p);
            }
        }
        KiReleaseDispatcherLock(s);
        if (!wait_for)
            break;
        LARGE_INTEGER t = {.QuadPart = -500000};
        KeWaitForSingleObject(&wait_for->Pcb, Executive, KernelMode, FALSE, &t);
        ObDereferenceObject(wait_for);
        if (now_ms() - start > 5000)
            break;
    }
}

[[noreturn]] void PsShutdownSystem(ULONG action)
{
    ExAcquireFastMutex(&g.Lock);
    BOOLEAN second = g.Stopping;
    g.Stopping = TRUE;
    ExReleaseFastMutex(&g.Lock);
    if (second) {
        /* another thread is already doing it. */
        KEVENT never;
        KeInitializeEvent(&never, NotificationEvent, FALSE);
        for (;;)
            KeWaitForSingleObject(&never, Executive, KernelMode, FALSE, nullptr);
    }
    KiLogPrintf("shutting down, action %u", action);
    end_everything();
    /* flush host files still held by handles, then the registry journal, so the drive sync covers both. */
    FsWbFlushAll();
    CmpJournalShutdown();
    HalHypercall(MIME_HC_FS_SYNC, 0, 0, 0, 0, 0, 0, nullptr);
    HalHypercall(MIME_HC_HALT, 0, 0, 0, 0, 0, 0, nullptr);
    for (;;)
        KiPause();
}

static void finish(NTSTATUS status)
{
    ExAcquireFastMutex(&g.Lock);
    g.Active = FALSE;
    g.Pending = FALSE;
    g.Result = status;
    KeSetEvent(&g.Finished, 0, FALSE);
    ExReleaseFastMutex(&g.Lock);
}

static void NTAPI shutdown_thread(PVOID context)
{
    if (g.DelayMs) {
        PVOID objects[2] = {&g.Cancel, &g.Expire};
        if (wait_objects(objects, 2, g.DelayMs) != STATUS_TIMEOUT) {
            finish(STATUS_CANCELLED);
            PsTerminateSystemThread(STATUS_SUCCESS);
        }
        ExAcquireFastMutex(&g.Lock);
        g.Pending = FALSE;
        ExReleaseFastMutex(&g.Lock);
    }
    load_settings(&g.Settings);
    KiLogPrintf("shutdown: flags %08x reason %08x from process %u (%.15s), hung %u wait to kill %u", g.Flags, g.Reason,
                (ULONG)g.Requester, g.RequesterName, g.Settings.HungAppTimeout, g.Settings.WaitToKillTimeout);
    if (g.LimitMs) {
        g.Deadline = now_ms() + g.LimitMs;
        PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, limit_thread, (PVOID)(ULONG_PTR)g.LimitMs);
    }
    NTSTATUS status = user_phase();
    if (status == STATUS_CANCELLED && g.Refused && (g.Control & MX_SHUTDOWN_RETRY_FORCED) && !g.Stopping) {
        /* nobody is there to choose to force it anyway, so a refusal acts as that choice. */
        KiLogPrintf("shutdown: a window refused, going on forced");
        reset_taken();
        KeClearEvent(&g.Cancel);
        g.Refused = FALSE;
        g.Flags |= EWX_FORCE;
        status = user_phase();
    }
    if (NT_SUCCESS(status) && (g.Flags & MX_EWX_MACHINE))
        PsShutdownSystem((g.Flags & EWX_REBOOT) ? 1 : (g.Flags & (EWX_POWEROFF | EWX_HYBRID_SHUTDOWN)) ? 2 : 0);
    if (NT_SUCCESS(status) && (g.Control & MX_SHUTDOWN_IMMEDIATE))
        end_everything();
    finish(status);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* ------------------------------------------------------------------ requests */

/* a shutdown can be aborted during its countdown, not once programs are being told. */
NTSTATUS MimeShutdownAbort(void)
{
    ExAcquireFastMutex(&g.Lock);
    BOOLEAN active = g.Active && g.Pending && !g.Stopping;
    if (active) {
        g.Pending = FALSE;
        KeSetEvent(&g.Cancel, 0, FALSE);
    }
    ExReleaseFastMutex(&g.Lock);
    if (active) {
        /* the next request finds nothing left of this one. */
        PVOID done[1] = {&g.Finished};
        wait_objects(done, 1, 5000);
    }
    return active ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

/*
 * a second request while one runs does nothing if it asks for no more than the first, as windows answers 259 then.
 * it ends the first and starts over if it adds shutdown, reboot or force, and is busy if it is a logoff during a
 * shutdown. strict requests, from InitiateSystemShutdownEx, fail instead of joining.
 */
NTSTATUS MimeShutdownStart(ULONG flags, ULONG reason, ULONG delay_seconds, ULONG control, ULONG limit_ms,
                           BOOLEAN strict, BOOLEAN wait)
{
    if (!g.Ready)
        return STATUS_NOT_IMPLEMENTED;
    if (flags & (EWX_REBOOT | EWX_POWEROFF | EWX_HYBRID_SHUTDOWN))
        flags |= EWX_SHUTDOWN;
    for (ULONG attempt = 0;; attempt++) {
        ExAcquireFastMutex(&g.Lock);
        if (g.Stopping) {
            ExReleaseFastMutex(&g.Lock);
            return STATUS_SHUTDOWN_IN_PROGRESS;
        }
        if (!g.Active)
            break;
        NTSTATUS answer = STATUS_RETRY;
        if (strict)
            answer = STATUS_SHUTDOWN_IN_PROGRESS;
        else if (!(flags & EWX_SHUTDOWN) && (g.Flags & EWX_SHUTDOWN))
            answer = STATUS_DEVICE_BUSY;
        else if (!(flags & ~g.Flags & (EWX_SHUTDOWN | EWX_REBOOT | EWX_FORCE)))
            answer = STATUS_SUCCESS;
        if (answer != STATUS_RETRY || attempt) {
            ExReleaseFastMutex(&g.Lock);
            return answer;
        }
        /* it asks for more than the running one: that one ends and this one replaces it. */
        KeSetEvent(&g.Cancel, 0, FALSE);
        ExReleaseFastMutex(&g.Lock);
        PVOID done[1] = {&g.Finished};
        wait_objects(done, 1, 30000);
    }
    g.Active = TRUE;
    g.Pending = delay_seconds != 0;
    g.Flags = flags;
    g.Reason = reason;
    PEPROCESS requester = PsGetCurrentProcess();
    g.Requester = (ULONG_PTR)requester->UniqueProcessId;
    memcpy(g.RequesterName, requester->ImageFileName, sizeof(g.RequesterName));
    g.Control = control;
    g.DelayMs = delay_seconds * 1000;
    g.LimitMs = limit_ms;
    g.Deadline = 0;
    g.Refused = FALSE;
    g.Generation++;
    g.Result = STATUS_PENDING;
    KeClearEvent(&g.Cancel);
    KeClearEvent(&g.Expire);
    KeClearEvent(&g.Finished);
    NTSTATUS status = PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, shutdown_thread, nullptr);
    if (!NT_SUCCESS(status)) {
        g.Active = FALSE;
        KeSetEvent(&g.Finished, 0, FALSE);
    }
    ExReleaseFastMutex(&g.Lock);
    if (!NT_SUCCESS(status) || !wait)
        return status;
    PVOID done[1] = {&g.Finished};
    /* a user mode wait, so the caller can be ended by the shutdown too. */
    NTSTATUS waited = KeWaitForMultipleObjects(1, done, WaitAny, UserRequest, UserMode, FALSE, nullptr, nullptr);
    return waited == STATUS_WAIT_0 ? g.Result : waited;
}

/* ------------------------------------------------------------------ services */

BOOLEAN NTAPI MimeCurrentHasPrivilege(ULONG privilege)
{
    return SepSinglePrivilegeCheck(privilege, UserMode);
}

ULONG NTAPI MimeProcessShutdownFlags(PEPROCESS process)
{
    return process->ShutdownFlags;
}

NTSTATUS NTAPI NtShutdownSystem(SHUTDOWN_ACTION action)
{
    if ((ULONG)action > ShutdownPowerOff)
        return STATUS_INVALID_PARAMETER;
    if (!SepSinglePrivilegeCheck(SHUTDOWN_PRIVILEGE, KeGetPreviousMode()))
        return STATUS_PRIVILEGE_NOT_HELD;
    PsShutdownSystem((ULONG)action);
}

/*
 * sleep, hibernate or shutdown. it needs the shutdown privilege. there is no sleep or hibernate state here, so an
 * allowed request is refused as not supported, a guess at what a machine without those states answers.
 */
NTSTATUS NTAPI NtInitiatePowerAction(ULONG action, ULONG lightest_state, ULONG flags, BOOLEAN asynchronous)
{
    if (action > 8 || lightest_state > 7)
        return STATUS_INVALID_PARAMETER;
    if (!SepSinglePrivilegeCheck(SHUTDOWN_PRIVILEGE, KeGetPreviousMode()))
        return STATUS_PRIVILEGE_NOT_HELD;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI NtSetSystemInformation(SYSTEM_INFORMATION_CLASS klass, PVOID info, ULONG length)
{
    if ((ULONG)klass != MX_SYSTEM_SHUTDOWN_REQUEST)
        return STATUS_INVALID_INFO_CLASS;
    MX_SHUTDOWN_REQUEST request;
    if (length != sizeof(request))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (!NT_SUCCESS(KiCopyFromUser(&request, info, sizeof(request))))
        return STATUS_ACCESS_VIOLATION;
    if (!SepSinglePrivilegeCheck(SHUTDOWN_PRIVILEGE, KeGetPreviousMode()))
        return STATUS_PRIVILEGE_NOT_HELD;
    if (request.Control & MX_SHUTDOWN_ABORT)
        return MimeShutdownAbort();
    if (request.Flags & ~(ULONG)MX_EWX_VALID)
        return STATUS_INVALID_PARAMETER;
    return MimeShutdownStart(request.Flags, request.Reason, request.Delay, request.Control & ~MX_SHUTDOWN_ABORT, 0, TRUE,
                             FALSE);
}
