/*
 * the worker factory the thread pool of the system dll stands on, measured on windows 11 26300. a factory is bound to a
 * completion port and a process, and starts the threads that wait on the port: up to its minimum at once, one more when
 * a packet comes while the binding count is not zero and no worker waits, and one for a release that finds no waiter. a
 * worker counts as pending until it says it is ready, a waiter over the minimum leaves with STATUS_TIMEOUT once idle,
 * and a thread that returns from its routine without that is counted on, as windows counts it.
 */
#include "../ntos.h"

POBJECT_TYPE ExWorkerFactoryType;

#define WORKER_FACTORY_RELEASE_WORKER 0x01
#define WORKER_FACTORY_WAIT 0x02
#define WORKER_FACTORY_SET_INFORMATION 0x04
#define WORKER_FACTORY_QUERY_INFORMATION 0x08
#define WORKER_FACTORY_READY_WORKER 0x10
#define WORKER_FACTORY_SHUTDOWN 0x20
#define WORKER_FACTORY_ALL_ACCESS 0xF00FF

/* a worker left idle goes after 67 seconds, and the time is kept between a second and ten minutes. */
#define DEFAULT_IDLE_TIMEOUT (-670000000ll)
#define LONGEST_IDLE_TIMEOUT (-6000000000ll)
#define SHORTEST_IDLE_TIMEOUT (-10000000ll)
#define WAIT_PACKETS 64

typedef struct WORKER_FACTORY {
    volatile LONG Lock;
    volatile LONG CheckQueued; /* on the list of the balancer. */
    LIST_ENTRY CheckLinks;
    PVOID Port;
    PEPROCESS Process;
    PVOID StartRoutine, StartParameter;
    SIZE_T StackReserve, StackCommit;
    LONGLONG IdleTimeout;
    LONG BindingCount;
    ULONG ThreadMinimum, ThreadMaximum;
    ULONG Pending, Waiting, Total;
    ULONG Starting; /* started and not at their first wait yet, so a packet that comes meanwhile starts no other. */
    ULONG Requests; /* workers asked for by a release or by class 9. */
    BOOLEAN Shutdown;
    NTSTATUS LastThreadCreationStatus;
} WORKER_FACTORY;

typedef struct WORKER_FACTORY_BASIC_INFORMATION {
    LARGE_INTEGER Timeout, RetryTimeout, IdleTimeout;
    BOOLEAN Paused, TimerSet, QueuedToExWorker, MayCreate, CreateInProgress, InsertedIntoQueue, Shutdown;
    ULONG BindingCount, ThreadMinimum, ThreadMaximum, PendingWorkerCount, WaitingWorkerCount, TotalWorkerCount;
    ULONG ReleaseCount;
    LONGLONG InfiniteWaitGoal;
    PVOID StartRoutine, StartParameter;
    HANDLE ProcessId;
    SIZE_T StackReserve, StackCommit;
    NTSTATUS LastThreadCreationStatus;
} WORKER_FACTORY_BASIC_INFORMATION;
static_assert(sizeof(WORKER_FACTORY_BASIC_INFORMATION) == 0x78);

NTSTATUS NTAPI NtAlpcSendWaitReceivePort(HANDLE handle, ULONG flags, PVOID send, PVOID send_attributes, PVOID receive,
                                         PSIZE_T length, PVOID receive_attributes, PLARGE_INTEGER timeout);

typedef struct WORKER_FACTORY_DEFERRED_WORK {
    PVOID AlpcSendMessage;
    PVOID AlpcSendMessagePort;
    ULONG AlpcSendMessageFlags;
    ULONG Flags;
} WORKER_FACTORY_DEFERRED_WORK;

typedef struct FILE_IO_COMPLETION_INFORMATION {
    PVOID KeyContext;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock;
} FILE_IO_COMPLETION_INFORMATION;

/* ------------------------------------------------------------------ the balancer */

/*
 * a packet is queued from wherever a request completes, where no thread can be made. the factory goes on a list and
 * one system thread starts the workers.
 */
static volatile LONG g_check_lock;
static LIST_ENTRY g_checks;
static KSEMAPHORE g_check_count;

/* called by the port under its lock for each packet: the factory to check once the lock is let go, referenced. */
PVOID ExWorkerFactoryPacketQueued(PVOID object)
{
    WORKER_FACTORY *factory = object;
    if (!factory->BindingCount || factory->Waiting || factory->Shutdown ||
        __atomic_exchange_n(&factory->CheckQueued, 1, __ATOMIC_ACQ_REL))
        return nullptr;
    ObReferenceObject(factory);
    return factory;
}

void ExWorkerFactoryCheck(PVOID object)
{
    WORKER_FACTORY *factory = object;
    ULONG64 state = KiAcquireRawLock(&g_check_lock);
    InsertTailList(&g_checks, &factory->CheckLinks);
    KiReleaseRawLock(&g_check_lock, state);
    KeReleaseSemaphore(&g_check_count, 0, 1, FALSE);
}

static BOOLEAN wants_worker(WORKER_FACTORY *factory)
{
    if (factory->Shutdown || factory->Total + factory->Pending >= factory->ThreadMaximum)
        return FALSE;
    if (factory->Total + factory->Pending < factory->ThreadMinimum || factory->Requests)
        return TRUE;
    return factory->BindingCount && !factory->Waiting && !factory->Starting && IopCompletionDepth(factory->Port) > 0;
}

static void balance(WORKER_FACTORY *factory)
{
    for (;;) {
        ULONG64 state = KiAcquireRawLock(&factory->Lock);
        BOOLEAN start = wants_worker(factory);
        if (start) {
            factory->Pending++;
            factory->Starting++;
            if (factory->Requests)
                factory->Requests--;
        } else {
            factory->Requests = 0;
        }
        KiReleaseRawLock(&factory->Lock, state);
        if (!start)
            return;
        PETHREAD thread;
        NTSTATUS status = PspCreateUserThread(factory->Process, factory->StartRoutine, factory->StartParameter, 1,
                                              factory->StackReserve, factory->StackCommit, &thread);
        if (NT_SUCCESS(status)) {
            /* the thread knows its factory before it runs. */
            thread->WorkerFactory = factory;
            ULONG64 s = KiAcquireDispatcherLock();
            thread->Tcb.SuspendCount = 0;
            KiReleaseDispatcherLock(s);
            KiStartThread(&thread->Tcb);
            ObDereferenceObject(thread);
        }
        state = KiAcquireRawLock(&factory->Lock);
        factory->LastThreadCreationStatus = status;
        if (!NT_SUCCESS(status)) {
            factory->Pending--;
            factory->Starting--;
        }
        KiReleaseRawLock(&factory->Lock, state);
        if (!NT_SUCCESS(status))
            return;
    }
}

static void NTAPI balancer(PVOID context)
{
    for (;;) {
        KeWaitForSingleObject(&g_check_count, Executive, KernelMode, FALSE, nullptr);
        ULONG64 state = KiAcquireRawLock(&g_check_lock);
        WORKER_FACTORY *factory = IsListEmpty(&g_checks)
                                      ? nullptr
                                      : CONTAINING_RECORD(RemoveHeadList(&g_checks), WORKER_FACTORY, CheckLinks);
        KiReleaseRawLock(&g_check_lock, state);
        if (!factory)
            continue;
        __atomic_store_n(&factory->CheckQueued, 0, __ATOMIC_RELEASE);
        balance(factory);
        ObDereferenceObject(factory);
    }
}

/* ------------------------------------------------------------------ the object */

/* the waiters are let go with an empty packet each, and their next wait is told the factory is gone. */
static void shut_down(WORKER_FACTORY *factory)
{
    ULONG64 state = KiAcquireRawLock(&factory->Lock);
    ULONG waiting = factory->Shutdown ? 0 : factory->Waiting;
    factory->Shutdown = TRUE;
    factory->ThreadMinimum = 0;
    factory->ThreadMaximum = 0;
    KiReleaseRawLock(&factory->Lock, state);
    for (ULONG i = 0; i < waiting; i++)
        IopQueueCompletion(factory->Port, nullptr, nullptr, 0, 0);
}

static void factory_close(PEPROCESS process, PVOID object, ULONG handle_count)
{
    WORKER_FACTORY *factory = object;
    if (handle_count > 1)
        return;
    /* no packet reaches a factory without a handle. */
    IopBindCompletionFactory(factory->Port, factory, FALSE);
    shut_down(factory);
}

static void factory_delete(PVOID object)
{
    WORKER_FACTORY *factory = object;
    if (factory->Port)
        ObDereferenceObject(factory->Port);
    if (factory->Process)
        ObDereferenceObject(factory->Process);
}

void ExInitializeWorkerFactory(void)
{
    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = WORKER_FACTORY_ALL_ACCESS;
    init.CloseProcedure = factory_close;
    init.DeleteProcedure = factory_delete;
    ObSetGenericMapping(&init, 0x20008, 0x20004, 0x20003, WORKER_FACTORY_ALL_ACCESS);
    ExWorkerFactoryType = ObCreateObjectType(L"TpWorkerFactory", &init);
    InitializeListHead(&g_checks);
    KeInitializeSemaphore(&g_check_count, 0, 0x7FFFFFFF);
    PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, balancer, nullptr);
}

static NTSTATUS probe_write(PVOID address, SIZE_T length)
{
    UCHAR bytes[8];
    NTSTATUS status = KiCopyFromUser(bytes, address, length);
    return NT_SUCCESS(status) ? KiCopyToUser(address, bytes, length) : status;
}

/*
 * the handle pointer is tried first, then the port and the process, whose handle must let threads be made. the start
 * routine is not looked at, a count of 0 threads is taken, and sizes of 0 stand for a stack of 64 KB with a page
 * committed.
 */
NTSTATUS NTAPI NtCreateWorkerFactory(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, HANDLE port_handle,
                                     HANDLE process_handle, PVOID start, PVOID parameter, ULONG maximum,
                                     SIZE_T stack_reserve, SIZE_T stack_commit)
{
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    if (mode == UserMode && !NT_SUCCESS(probe_write(handle, sizeof(*handle))))
        return STATUS_ACCESS_VIOLATION;
    PVOID port;
    NTSTATUS status = ObReferenceHandleFor(port_handle, 0x2, IoCompletionObjectType, mode, &port);
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS process;
    status = PsReferenceProcessHandle(process_handle, PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(port);
        return status;
    }
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    status = mode == UserMode && ((ULONG_PTR)attributes & 7) ? STATUS_DATATYPE_MISALIGNMENT
                                                              : ObCaptureObjectAttributes(attributes, &oa, &name);
    WORKER_FACTORY *factory = nullptr;
    if (NT_SUCCESS(status)) {
        status = ObCreateObject(ExWorkerFactoryType, attributes ? &oa : nullptr, mode, sizeof(*factory), (PVOID *)&factory);
        KiFreeCapturedString(&name);
    }
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(process);
        ObDereferenceObject(port);
        return status;
    }
    memset(factory, 0, sizeof(*factory));
    factory->Port = port;
    factory->Process = process;
    factory->StartRoutine = start;
    factory->StartParameter = parameter;
    factory->ThreadMaximum = maximum;
    factory->StackReserve = stack_reserve ? stack_reserve : 0x10000;
    factory->StackCommit = stack_commit ? stack_commit : PAGE_SIZE;
    factory->IdleTimeout = DEFAULT_IDLE_TIMEOUT;
    IopBindCompletionFactory(port, factory, TRUE);
    /* the rights the type does not have are dropped from what is asked. */
    HANDLE h;
    status = ObInsertObject(factory, access & (0xFF000000 | WORKER_FACTORY_ALL_ACCESS), 0, &h);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    return status;
}

/* only the basic information is told, in a record of its exact size on an 8 byte boundary. */
NTSTATUS NTAPI NtQueryInformationWorkerFactory(HANDLE handle, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    if (klass != 7)
        return STATUS_INVALID_INFO_CLASS;
    ULONG size = sizeof(WORKER_FACTORY_BASIC_INFORMATION);
    if (returned && KeGetPreviousMode() == UserMode && !NT_SUCCESS(probe_write(returned, sizeof(*returned))))
        return STATUS_ACCESS_VIOLATION;
    if (length != size) {
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    if (KeGetPreviousMode() == UserMode) {
        if ((ULONG_PTR)info & 7)
            return STATUS_DATATYPE_MISALIGNMENT;
        if (!NT_SUCCESS(probe_write(info, 1)))
            return STATUS_ACCESS_VIOLATION;
    }
    WORKER_FACTORY *factory;
    NTSTATUS status = ObReferenceHandleFor(handle, WORKER_FACTORY_QUERY_INFORMATION, ExWorkerFactoryType,
                                           KeGetPreviousMode(), &factory);
    if (!NT_SUCCESS(status))
        return status;
    WORKER_FACTORY_BASIC_INFORMATION basic = {};
    ULONG64 state = KiAcquireRawLock(&factory->Lock);
    basic.IdleTimeout.QuadPart = factory->IdleTimeout;
    basic.MayCreate = !factory->Waiting;
    basic.CreateInProgress = factory->Pending != 0;
    basic.Shutdown = factory->Shutdown;
    basic.BindingCount = (ULONG)factory->BindingCount;
    basic.ThreadMinimum = factory->ThreadMinimum;
    basic.ThreadMaximum = factory->ThreadMaximum;
    basic.PendingWorkerCount = factory->Pending;
    basic.WaitingWorkerCount = factory->Waiting;
    basic.TotalWorkerCount = factory->Total;
    basic.StartRoutine = factory->StartRoutine;
    basic.StartParameter = factory->StartParameter;
    basic.ProcessId = factory->Process->UniqueProcessId;
    basic.StackReserve = factory->StackReserve;
    basic.StackCommit = factory->StackCommit;
    basic.LastThreadCreationStatus = factory->LastThreadCreationStatus;
    KiReleaseRawLock(&factory->Lock, state);
    ObDereferenceObject(factory);
    status = KiCopyToUser(info, &basic, sizeof(basic));
    if (NT_SUCCESS(status) && returned)
        status = KiCopyToUser(returned, &size, sizeof(size));
    return status;
}

/*
 * the classes and the one length each takes: the idle timeout 8 bytes, the stack 16, the processor sets any number of
 * 8, the others 4. the two other timeouts, the basic information and what lies past class 15 are no classes to set,
 * and the pause is not implemented.
 */
NTSTATUS NTAPI NtSetInformationWorkerFactory(HANDLE handle, ULONG klass, PVOID info, ULONG length)
{
    if (klass < 2 || klass == 7 || klass > 15)
        return STATUS_INVALID_INFO_CLASS;
    if (klass == 6)
        return STATUS_NOT_IMPLEMENTED;
    ULONG64 value[2] = {};
    if (klass == 15) {
        if (length & 7)
            return STATUS_INFO_LENGTH_MISMATCH;
        if (length > 8)
            return (NTSTATUS)0xC00001AF;
    } else if (length != (klass == 2 ? 8u : klass == 10 ? 16u : 4u)) {
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    if (length && !NT_SUCCESS(KiCopyFromUser(value, info, length)))
        return STATUS_ACCESS_VIOLATION;
    WORKER_FACTORY *factory;
    NTSTATUS status = ObReferenceHandleFor(handle, WORKER_FACTORY_SET_INFORMATION, ExWorkerFactoryType,
                                           KeGetPreviousMode(), &factory);
    if (!NT_SUCCESS(status))
        return status;
    BOOLEAN check = FALSE;
    ULONG number = (ULONG)value[0];
    ULONG64 state = KiAcquireRawLock(&factory->Lock);
    switch (klass) {
    case 2: /* the idle timeout, a relative time */
        if ((LONGLONG)value[0] >= 0)
            status = STATUS_INVALID_PARAMETER;
        else
            factory->IdleTimeout = (LONGLONG)value[0] < LONGEST_IDLE_TIMEOUT    ? LONGEST_IDLE_TIMEOUT
                                   : (LONGLONG)value[0] > SHORTEST_IDLE_TIMEOUT ? SHORTEST_IDLE_TIMEOUT
                                                                                : (LONGLONG)value[0];
        break;
    case 3: /* a count above 0 is added, a 0 makes the count -1 for good, and a count under 0 is left out */
        if (!number)
            factory->BindingCount = -1;
        else if ((LONG)number > 0 && factory->BindingCount != -1)
            factory->BindingCount += (LONG)number;
        check = TRUE;
        break;
    case 4: /* the minimum, which the maximum follows up */
        if (factory->Shutdown) {
            status = STATUS_ABANDONED;
            break;
        }
        factory->ThreadMinimum = number;
        if (factory->ThreadMaximum < number)
            factory->ThreadMaximum = number;
        check = TRUE;
        break;
    case 5:
        factory->ThreadMaximum = number;
        break;
    case 9: /* a worker is asked for */
        if (!number) {
            status = STATUS_UNSUCCESSFUL;
            break;
        }
        if (!factory->Waiting && !factory->Starting)
            factory->Requests++;
        check = TRUE;
        break;
    case 10:
        factory->StackReserve = value[0];
        factory->StackCommit = value[1];
        break;
    default: /* the goal, the priority, the timeout waiters, the flags, the soft maximum and the processor sets */
        break;
    }
    KiReleaseRawLock(&factory->Lock, state);
    if (check && NT_SUCCESS(status))
        balance(factory);
    ObDereferenceObject(factory);
    return status;
}

/* the count of pending workers is a pointer that must hold, and is left as it is. */
NTSTATUS NTAPI NtShutdownWorkerFactory(HANDLE handle, volatile LONG *pending)
{
    if (KeGetPreviousMode() == UserMode) {
        if (pending && ((ULONG_PTR)pending & 3))
            return STATUS_DATATYPE_MISALIGNMENT;
        if (!NT_SUCCESS(probe_write((PVOID)pending, sizeof(*pending))))
            return STATUS_ACCESS_VIOLATION;
    }
    WORKER_FACTORY *factory;
    NTSTATUS status = ObReferenceHandleFor(handle, WORKER_FACTORY_SHUTDOWN, ExWorkerFactoryType, KeGetPreviousMode(),
                                           &factory);
    if (!NT_SUCCESS(status))
        return status;
    shut_down(factory);
    ObDereferenceObject(factory);
    return STATUS_SUCCESS;
}

/* an empty packet for a worker, and a worker for the packet when none waits. */
NTSTATUS NTAPI NtReleaseWorkerFactoryWorker(HANDLE handle)
{
    WORKER_FACTORY *factory;
    NTSTATUS status = ObReferenceHandleFor(handle, WORKER_FACTORY_RELEASE_WORKER, ExWorkerFactoryType,
                                           KeGetPreviousMode(), &factory);
    if (!NT_SUCCESS(status))
        return status;
    ULONG64 state = KiAcquireRawLock(&factory->Lock);
    BOOLEAN gone = factory->Shutdown;
    if (!gone && !factory->Waiting && !factory->Starting)
        factory->Requests++;
    KiReleaseRawLock(&factory->Lock, state);
    if (gone) {
        status = STATUS_ABANDONED;
    } else {
        status = IopQueueCompletion(factory->Port, nullptr, nullptr, 0, 0);
        balance(factory);
    }
    ObDereferenceObject(factory);
    return status;
}

/* a thread the factory started says so once, and no other thread can. */
NTSTATUS NTAPI NtWorkerFactoryWorkerReady(HANDLE handle)
{
    WORKER_FACTORY *factory;
    NTSTATUS status = ObReferenceHandleFor(handle, WORKER_FACTORY_READY_WORKER, ExWorkerFactoryType, KeGetPreviousMode(),
                                           &factory);
    if (!NT_SUCCESS(status))
        return status;
    PETHREAD thread = PsGetCurrentThread();
    ULONG64 state = KiAcquireRawLock(&factory->Lock);
    if (thread->WorkerFactory != factory || thread->WorkerReady) {
        status = STATUS_UNSUCCESSFUL;
    } else {
        thread->WorkerReady = TRUE;
        factory->Pending--;
        factory->Total++;
    }
    KiReleaseRawLock(&factory->Lock, state);
    ObDereferenceObject(factory);
    return status;
}

/*
 * takes up to count packets of the port. a count of 0 is refused, and the three pointers must hold. a worker waits
 * its idle time over and over while the factory is at its minimum, and any other thread waits like on the port.
 */
NTSTATUS NTAPI NtWaitForWorkViaWorkerFactory(HANDLE handle, PVOID packets, ULONG count, PULONG removed, PVOID deferred)
{
    if (!count)
        return STATUS_INVALID_PARAMETER;
    WORKER_FACTORY_DEFERRED_WORK work;
    if (!NT_SUCCESS(KiCopyFromUser(&work, deferred, sizeof(work))) || !NT_SUCCESS(probe_write(packets, 1)) ||
        !NT_SUCCESS(probe_write(removed, sizeof(*removed))))
        return STATUS_ACCESS_VIOLATION;
    WORKER_FACTORY *factory;
    NTSTATUS status = ObReferenceHandleFor(handle, WORKER_FACTORY_WAIT, ExWorkerFactoryType, KeGetPreviousMode(), &factory);
    if (!NT_SUCCESS(status))
        return status;
    /*
     * the reply of an alpc callback goes out before the worker waits again: ntdll's TpCallbackSendAlpcMessageOnCompletion
     * leaves it here, and rpcrt4's servers answer every call that way.
     */
    if (work.AlpcSendMessage)
        NtAlpcSendWaitReceivePort((HANDLE)work.AlpcSendMessagePort, work.AlpcSendMessageFlags, work.AlpcSendMessage,
                                  nullptr, nullptr, nullptr, nullptr, nullptr);
    if (count > WAIT_PACKETS)
        count = WAIT_PACKETS;
    FILE_IO_COMPLETION_INFORMATION *taken =
        ExAllocatePool2(POOL_FLAG_NON_PAGED, count * sizeof(*taken), KI_TAG('W', 'f', 'W', 't'));
    if (!taken) {
        ObDereferenceObject(factory);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    PETHREAD thread = PsGetCurrentThread();
    BOOLEAN worker = thread->WorkerFactory == factory;
    ULONG got = 0;
    for (;;) {
        ULONG64 state = KiAcquireRawLock(&factory->Lock);
        if (factory->Shutdown) {
            KiReleaseRawLock(&factory->Lock, state);
            status = STATUS_ABANDONED;
            break;
        }
        LARGE_INTEGER idle = {.QuadPart = factory->IdleTimeout};
        if (worker) {
            factory->Waiting++;
            if (!thread->WorkerWaited) {
                thread->WorkerWaited = TRUE;
                factory->Starting--;
            }
        }
        KiReleaseRawLock(&factory->Lock, state);
        status = IopRemoveCompletionPackets(factory->Port, taken, count, &got, worker ? &idle : nullptr);
        state = KiAcquireRawLock(&factory->Lock);
        if (worker)
            factory->Waiting--;
        BOOLEAN leave = status == STATUS_TIMEOUT && worker && thread->WorkerReady &&
                        factory->Total > factory->ThreadMinimum;
        if (leave)
            factory->Total--;
        KiReleaseRawLock(&factory->Lock, state);
        if (status != STATUS_TIMEOUT || leave)
            break;
    }
    if (got) {
        status = KiCopyToUser(packets, taken, got * sizeof(*taken));
        if (NT_SUCCESS(status))
            status = KiCopyToUser(removed, &got, sizeof(got));
    }
    ExFreePool(taken);
    ObDereferenceObject(factory);
    return status;
}
