/*
 * threads: creation, user mode entry, termination and reaping.
 */
#include "../ntos.h"

#define KERNEL_STACK_SIZE (64 * 1024)
#define MIME_DEFAULT_STACK_RESERVE (1024 * 1024)

static LIST_ENTRY g_dead_threads;
static ETHREAD g_idle_thread;
PVOID PspSystemDllBase;

void PsInitializeIdleThread(void)
{
    InitializeListHead(&g_dead_threads);
    PETHREAD idle = &g_idle_thread;
    memset(idle, 0, sizeof(*idle));
    PKTHREAD t = &idle->Tcb;
    t->Header.Type = ThreadObject;
    InitializeListHead(&t->Header.WaitListHead);
    InitializeListHead(&t->MutantListHead);
    InitializeListHead(&t->ApcListHead[0]);
    InitializeListHead(&t->ApcListHead[1]);
    KeInitializeTimer(&t->Timer);
    t->Process = &PsInitialSystemProcess->Pcb;
    t->ApcStateProcess = t->Process;
    t->InitialStack = (PVOID)(MIME_KVA_PHYSMAP + HalBootInfo->boot_stack_phys + HalBootInfo->boot_stack_size);
    t->StackLimit = (PVOID)(MIME_KVA_PHYSMAP + HalBootInfo->boot_stack_phys);
    t->State = Running;
    t->Priority = 0;
    t->BasePriority = 0;
    idle->SystemThread = TRUE;
    PKPRCB prcb = KeGetCurrentPrcb();
    prcb->CurrentThread = t;
    prcb->IdleThread = t;
}

void PspThreadDelete(PVOID object)
{
    PETHREAD thread = object;
    if (thread->Tcb.StackAllocation)
        MmFreeKernelStack(thread->Tcb.StackAllocation, thread->Tcb.StackAllocationSize);
    if (thread->Cid.UniqueThread)
        PspFreeId(thread->Cid.UniqueThread);
    if (thread->Tcb.Process)
        ObDereferenceObject(thread->Tcb.Process);
}

/* threads cannot free their own stack; the idle loop releases dead ones. */
void PspReapDeadThreads(void)
{
    for (;;) {
        ULONG64 s = KiAcquireDispatcherLock();
        if (IsListEmpty(&g_dead_threads)) {
            KiReleaseDispatcherLock(s);
            return;
        }
        PETHREAD t = CONTAINING_RECORD(RemoveHeadList(&g_dead_threads), ETHREAD, ThreadListEntry);
        KiReleaseDispatcherLock(s);
        ObDereferenceObject(t);
    }
}

NTSTATUS PspCreateThread(PEPROCESS process, BOOLEAN system, PKSTART_ROUTINE routine, PVOID context, PETHREAD *out)
{
    PETHREAD thread;
    NTSTATUS status = ObCreateObject(PsThreadTypeObject, NULL, KernelMode, sizeof(ETHREAD), (PVOID *)&thread);
    if (!NT_SUCCESS(status))
        return status;
    memset(thread, 0, sizeof(*thread));
    PVOID limit;
    PVOID alloc = MmAllocateKernelStack(KERNEL_STACK_SIZE, &limit);
    if (!alloc) {
        ObDereferenceObject(thread);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    thread->Tcb.StackAllocation = alloc;
    thread->Tcb.StackAllocationSize = KERNEL_STACK_SIZE;
    ObReferenceObject(process);
    thread->Cid.UniqueProcess = process->UniqueProcessId;
    status = PspAllocateId(thread, &thread->Cid.UniqueThread);
    if (!NT_SUCCESS(status)) {
        thread->Tcb.Process = &process->Pcb;
        ObDereferenceObject(thread);
        return status;
    }
    thread->SystemThread = system;
    thread->StartAddress = (PVOID)routine;
    KeQuerySystemTime((PLARGE_INTEGER)&thread->CreateTime);
    KiInitializeThread(&thread->Tcb, &process->Pcb, (PUCHAR)limit + KERNEL_STACK_SIZE, limit, routine, context);

    ULONG64 s = KiAcquireDispatcherLock();
    InsertTailList(&process->ThreadListHead, &thread->ThreadListEntry);
    process->ThreadCount++;
    KiReleaseDispatcherLock(s);

    /* one reference stays with the running thread until it exits. */
    ObReferenceObject(thread);
    *out = thread;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PsCreateSystemThread(PHANDLE handle, ULONG access, POBJECT_ATTRIBUTES attributes, HANDLE process,
                                    PCLIENT_ID client, PKSTART_ROUTINE routine, PVOID context)
{
    PETHREAD thread;
    NTSTATUS status = PspCreateThread(PsInitialSystemProcess, TRUE, routine, context, &thread);
    if (!NT_SUCCESS(status))
        return status;
    if (client)
        *client = thread->Cid;
    KiStartThread(&thread->Tcb);
    if (handle)
        return ObInsertObject(thread, access, OBJ_KERNEL_HANDLE, handle);
    ObDereferenceObject(thread);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PsTerminateSystemThread(NTSTATUS status)
{
    PspExitThread(status);
}

/* builds the user trap frame of a new user thread and enters user mode. */
typedef struct USER_START {
    ULONG64 Pc;
    ULONG64 Sp;
    ULONG64 X0;
    ULONG64 X1;
    ULONG64 Teb;
} USER_START;

static void user_thread_startup(PVOID context)
{
    PKTHREAD thread = KiCurrentThread();
    USER_START start = *(USER_START *)context;
    ExFreePool(context);
    PKTRAP_FRAME frame = thread->TrapFrame;
    memset(frame, 0, sizeof(*frame));
    frame->Pc = start.Pc;
    frame->Sp = start.Sp;
    frame->X[0] = start.X0;
    frame->X[1] = start.X1;
    frame->X[18] = start.Teb;
    frame->Spsr = PSR_MODE_EL0;
    frame->Flags = KTRAP_FLAG_FP_SAVED; /* start with zeroed fp state */
    thread->PreviousMode = UserMode;
    KiExitToUserMode(frame);
    KiEnterUserMode(frame);
}

PVOID PspLookupSystemDllExport(const char *name);

/* allocates the user stack and teb, then creates the kernel half of the thread. */
NTSTATUS PspCreateUserThread(PEPROCESS process, PVOID start, PVOID parameter, BOOLEAN suspended,
                             SIZE_T stack_reserve, SIZE_T stack_commit, PETHREAD *out)
{
    PVOID ldr_thunk = PspLookupSystemDllExport("LdrInitializeThunk");
    PVOID user_start = PspLookupSystemDllExport("RtlUserThreadStart");
    if (!ldr_thunk || !user_start)
        return STATUS_DLL_NOT_FOUND;
    if (!stack_reserve)
        stack_reserve = MIME_DEFAULT_STACK_RESERVE;
    stack_reserve = (stack_reserve + 0xFFFF) & ~(SIZE_T)0xFFFF;

    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    KiAttachProcess(&process->Pcb);

    PVOID stack = NULL, teb_va = NULL;
    SIZE_T size = stack_reserve;
    NTSTATUS status = MiAllocateVirtual(process, &stack, &size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
    SIZE_T teb_size = MIME_TEB_SIZE;
    if (NT_SUCCESS(status))
        status = MiAllocateVirtual(process, &teb_va, &teb_size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
    if (!NT_SUCCESS(status)) {
        KiAttachProcess(saved);
        return status;
    }

    PETHREAD thread;
    USER_START *ctx = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(USER_START), KI_TAG('P', 's', 'U', 's'));
    if (!ctx) {
        KiAttachProcess(saved);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = PspCreateThread(process, FALSE, user_thread_startup, ctx, &thread);
    if (!NT_SUCCESS(status)) {
        ExFreePool(ctx);
        KiAttachProcess(saved);
        return status;
    }
    thread->UserStackAllocation = stack;
    thread->UserStackSize = stack_reserve;
    thread->Win32StartAddress = start;
    thread->Tcb.Teb = teb_va;

    PTEB teb = teb_va;
    ULONG_PTR stack_base = (ULONG_PTR)stack + stack_reserve;
    teb->NtTib.StackBase = (PVOID)stack_base;
    teb->NtTib.StackLimit = stack;
    teb->NtTib.Self = &teb->NtTib;
    teb->DeallocationStack = stack;
    teb->ClientId = thread->Cid;
    teb->RealClientId = thread->Cid;
    teb->ProcessEnvironmentBlock = process->Peb;
    teb->CurrentLocale = 0x0409;
    teb->StaticUnicodeString.MaximumLength = sizeof(teb->StaticUnicodeBuffer);
    teb->StaticUnicodeString.Buffer = teb->StaticUnicodeBuffer;
    InitializeListHead(&teb->TlsLinks);

    /* initial context consumed by LdrInitializeThunk, then NtContinue'd into RtlUserThreadStart. */
    ULONG64 ctx_va = (stack_base - sizeof(CONTEXT)) & ~15ull;
    PCONTEXT context = (PCONTEXT)ctx_va;
    memset(context, 0, sizeof(*context));
    context->ContextFlags = CONTEXT_FULL;
    context->Pc = (ULONG64)user_start;
    context->Sp = ctx_va;
    context->X0 = (ULONG64)start;
    context->X1 = (ULONG64)parameter;
    context->X18 = (ULONG64)teb_va;
    context->Cpsr = PSR_MODE_EL0;

    ctx->Pc = (ULONG64)ldr_thunk;
    ctx->Sp = ctx_va;
    ctx->X0 = ctx_va;
    ctx->X1 = (ULONG64)process->NtdllBase;
    ctx->Teb = (ULONG64)teb_va;
    KiAttachProcess(saved);

    if (!suspended)
        KiStartThread(&thread->Tcb);
    else
        thread->Tcb.SuspendCount = 1;
    *out = thread;
    return STATUS_SUCCESS;
}

/* temporarily switches the current thread to another address space. */
void KiAttachProcess(PKPROCESS process)
{
    PKTHREAD t = KiCurrentThread();
    ULONG64 s = KiAcquireDispatcherLock();
    t->ApcStateProcess = process;
    if (process->Ttbr0)
        KiSwitchAddressSpace(process);
    KiReleaseDispatcherLock(s);
}

void NTAPI KeStackAttachProcess(PRKPROCESS process, PVOID state)
{
    *(PKPROCESS *)state = KiCurrentThread()->ApcStateProcess;
    KiAttachProcess(process);
}

void NTAPI KeUnstackDetachProcess(PVOID state)
{
    KiAttachProcess(*(PKPROCESS *)state);
}

void PspProcessExit(PEPROCESS process);

/* terminates the current thread; the last thread also ends its process. */
void PspExitThread(NTSTATUS status)
{
    PETHREAD thread = PsCurrentThread();
    PEPROCESS process = PsCurrentProcess();
    KeGetPcr()->Irql = PASSIVE_LEVEL;
    KiEnableInterrupts();

    thread->ExitStatus = status;
    thread->Tcb.Terminating = TRUE;
    if (thread->Tcb.Win32Thread)
        PsWin32ThreadCallout(thread, FALSE);

    /* abandon owned mutants. */
    while (!IsListEmpty(&thread->Tcb.MutantListHead)) {
        PKMUTANT m = CONTAINING_RECORD(thread->Tcb.MutantListHead.Flink, KMUTANT, MutantListEntry);
        KeReleaseMutant(m, 0, TRUE, FALSE);
    }

    ULONG64 s = KiAcquireDispatcherLock();
    RemoveEntryList(&thread->ThreadListEntry);
    BOOLEAN last = --process->ThreadCount == 0;
    if (last && !process->Terminating) {
        process->Terminating = TRUE;
        process->ExitStatus = status;
    }
    KiReleaseDispatcherLock(s);

    if (last && !process->SystemProcess)
        PspProcessExit(process);

    KeQuerySystemTime((PLARGE_INTEGER)&thread->ExitTime);
    s = KiAcquireDispatcherLock();
    RemoveEntryList(&thread->Tcb.ThreadListEntry);
    thread->Tcb.Process->ActiveThreads--;
    thread->HasTerminated = TRUE;
    thread->Tcb.Header.SignalState = 1;
    KiSignalWaiters(&thread->Tcb.Header, 0);
    InsertTailList(&g_dead_threads, &thread->ThreadListEntry);
    thread->Tcb.State = Terminated;
    KiSwapThread();
    KeBugCheckEx(0x00000001, (ULONG_PTR)thread, 0, 0, 0);
}

void KiTerminateCurrentThread(NTSTATUS status)
{
    PspExitThread(status);
}

NTSTATUS NTAPI NtTerminateThread(HANDLE handle, NTSTATUS exit_status)
{
    if (!handle || handle == NtCurrentThread())
        PspExitThread(exit_status);
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_TERMINATE, &thread);
    if (!NT_SUCCESS(status))
        return status;
    if (thread == PsCurrentThread()) {
        ObDereferenceObject(thread);
        PspExitThread(exit_status);
    }
    ULONG64 s = KiAcquireDispatcherLock();
    thread->Tcb.Terminating = TRUE;
    thread->ExitStatus = exit_status;
    if (thread->Tcb.State == Waiting)
        KiUnwaitThread(&thread->Tcb, STATUS_KERNEL_APC);
    KiReleaseDispatcherLock(s);
    ObDereferenceObject(thread);
    return STATUS_SUCCESS;
}

#define THREAD_CREATE_FLAGS_CREATE_SUSPENDED 0x00000001

NTSTATUS NTAPI NtCreateThreadEx(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, HANDLE process_handle,
                                PVOID start, PVOID parameter, ULONG flags, SIZE_T zero_bits, SIZE_T stack_size,
                                SIZE_T max_stack_size, PVOID attribute_list)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_CREATE_THREAD, &process);
    if (!NT_SUCCESS(status))
        return status;
    PETHREAD thread;
    status = PspCreateUserThread(process, start, parameter, (flags & THREAD_CREATE_FLAGS_CREATE_SUSPENDED) != 0,
                                 max_stack_size, stack_size, &thread);
    ObDereferenceObject(process);
    if (!NT_SUCCESS(status))
        return status;
    HANDLE h;
    status = ObInsertObject(thread, access ? access : THREAD_ALL_ACCESS, 0, &h);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    return status;
}

NTSTATUS NTAPI NtResumeThread(HANDLE handle, PULONG previous)
{
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_SUSPEND_RESUME, &thread);
    if (!NT_SUCCESS(status))
        return status;
    ULONG64 s = KiAcquireDispatcherLock();
    ULONG old = thread->Tcb.SuspendCount;
    if (old) {
        thread->Tcb.SuspendCount--;
        if (old == 1 && thread->Tcb.State == Initialized)
            KiReadyThread(&thread->Tcb);
    }
    KiReleaseDispatcherLock(s);
    if (previous)
        KiCopyToUser(previous, &old, sizeof(old));
    ObDereferenceObject(thread);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQueryInformationThread(HANDLE handle, THREADINFOCLASS klass, PVOID info, ULONG length,
                                        PULONG returned)
{
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_QUERY_INFORMATION, &thread);
    if (!NT_SUCCESS(status))
        return status;
    union {
        THREAD_BASIC_INFORMATION basic;
        ULONG64 times[4];
        ULONG u32;
        PVOID ptr;
    } out;
    ULONG size = 0;
    memset(&out, 0, sizeof(out));
    switch (klass) {
    case ThreadBasicInformation:
        out.basic.ExitStatus = thread->HasTerminated ? thread->ExitStatus : STATUS_PENDING;
        out.basic.TebBaseAddress = thread->Tcb.Teb;
        out.basic.ClientId = thread->Cid;
        out.basic.AffinityMask = 1;
        out.basic.Priority = thread->Tcb.Priority;
        out.basic.BasePriority = thread->Tcb.BasePriority;
        size = sizeof(out.basic);
        break;
    case ThreadTimes:
        out.times[0] = thread->CreateTime;
        out.times[1] = thread->ExitTime;
        out.times[2] = thread->Tcb.KernelTime;
        out.times[3] = thread->Tcb.UserTime;
        size = sizeof(out.times);
        break;
    case ThreadQuerySetWin32StartAddress:
        out.ptr = thread->Win32StartAddress;
        size = sizeof(PVOID);
        break;
    case ThreadIsTerminated:
        out.u32 = thread->HasTerminated;
        size = sizeof(ULONG);
        break;
    case ThreadAmILastThread: {
        PEPROCESS process = (PEPROCESS)KiCurrentThread()->Process;
        out.u32 = process->ThreadCount <= 1;
        size = sizeof(ULONG);
        break;
    }
    default:
        status = STATUS_INVALID_INFO_CLASS;
        break;
    }
    if (NT_SUCCESS(status)) {
        if (length < size)
            status = STATUS_INFO_LENGTH_MISMATCH;
        else
            status = KiCopyToUser(info, &out, size);
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
    }
    ObDereferenceObject(thread);
    return status;
}

NTSTATUS NTAPI NtSetInformationThread(HANDLE handle, THREADINFOCLASS klass, PVOID info, ULONG length)
{
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_SET_INFORMATION, &thread);
    if (!NT_SUCCESS(status))
        return status;
    if (klass == ThreadPriority && length >= sizeof(LONG)) {
        LONG prio;
        if (NT_SUCCESS(KiCopyFromUser(&prio, info, sizeof(prio))) && prio > 0 && prio < 32)
            thread->Tcb.Priority = thread->Tcb.BasePriority = (SCHAR)prio;
    } else if (klass == ThreadBasePriority && length >= sizeof(LONG)) {
        LONG inc;
        if (NT_SUCCESS(KiCopyFromUser(&inc, info, sizeof(inc))) && inc >= -2 && inc <= 2)
            thread->Tcb.Priority = thread->Tcb.BasePriority = (SCHAR)(8 + inc);
    }
    ObDereferenceObject(thread);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtYieldExecution(void)
{
    LARGE_INTEGER zero = {0};
    KeDelayExecutionThread(UserMode, FALSE, &zero);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtDelayExecution(BOOLEAN alertable, PLARGE_INTEGER interval)
{
    LARGE_INTEGER t;
    if (!NT_SUCCESS(KiCopyFromUser(&t, interval, sizeof(t))))
        return STATUS_ACCESS_VIOLATION;
    return KeDelayExecutionThread(UserMode, alertable, &t);
}

NTSTATUS NTAPI NtAlertThread(HANDLE handle)
{
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, 0, &thread);
    if (!NT_SUCCESS(status))
        return status;
    ULONG64 s = KiAcquireDispatcherLock();
    thread->Tcb.Alerted[UserMode] = TRUE;
    if (thread->Tcb.State == Waiting && thread->Tcb.Alertable)
        KiUnwaitThread(&thread->Tcb, STATUS_ALERTED);
    KiReleaseDispatcherLock(s);
    ObDereferenceObject(thread);
    return STATUS_SUCCESS;
}


static void NTAPI user_apc_kernel_routine(PKAPC apc, PKNORMAL_ROUTINE *normal, PVOID *context, PVOID *a1, PVOID *a2)
{
    /* the apc object is freed once the user mode part is scheduled. */
    ExFreePool(apc);
}

NTSTATUS NTAPI NtQueueApcThread(HANDLE handle, PPS_APC_ROUTINE routine, PVOID arg1, PVOID arg2, PVOID arg3)
{
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_SET_CONTEXT, &thread);
    if (!NT_SUCCESS(status))
        return status;
    PKAPC apc = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(KAPC), KI_TAG('P', 's', 'A', 'p'));
    if (!apc) {
        ObDereferenceObject(thread);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeApc(apc, &thread->Tcb, OriginalApcEnvironment, user_apc_kernel_routine, NULL,
                    (PKNORMAL_ROUTINE)routine, UserMode, arg1);
    if (!KeInsertQueueApc(apc, arg2, arg3, 0)) {
        ExFreePool(apc);
        status = STATUS_UNSUCCESSFUL;
    }
    ObDereferenceObject(thread);
    return status;
}

HANDLE NTAPI PsGetThreadId(PETHREAD thread)
{
    return thread->Cid.UniqueThread;
}

HANDLE NTAPI PsGetThreadProcessId(PETHREAD thread)
{
    return thread->Cid.UniqueProcess;
}

PEPROCESS NTAPI PsGetThreadProcess(PETHREAD thread)
{
    return (PEPROCESS)thread->Tcb.Process;
}

PVOID NTAPI PsGetThreadWin32Thread(PETHREAD thread)
{
    return thread->Tcb.Win32Thread;
}

PVOID NTAPI PsGetCurrentThreadWin32Thread(void)
{
    return KiCurrentThread()->Win32Thread;
}

PVOID NTAPI PsSetThreadWin32Thread(PETHREAD thread, PVOID win32, PVOID old_win32)
{
    PVOID old = thread->Tcb.Win32Thread;
    thread->Tcb.Win32Thread = win32;
    return old;
}

PVOID NTAPI PsGetThreadTeb(PETHREAD thread)
{
    return thread->Tcb.Teb;
}

PVOID NTAPI PsGetCurrentThreadTeb(void)
{
    return KiCurrentThread()->Teb;
}

BOOLEAN NTAPI PsIsThreadTerminating(PETHREAD thread)
{
    return thread->Tcb.Terminating;
}

NTSTATUS NTAPI PsSetCreateThreadNotifyRoutine(PCREATE_THREAD_NOTIFY_ROUTINE routine)
{
    return STATUS_SUCCESS;
}
