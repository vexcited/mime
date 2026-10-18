/*
 * threads: creation, user mode entry, termination and reaping.
 */
#include "../ntos.h"
#include "../se/se.h"
#include <mime/mx64.h>
#include <mime/wow32.h>

#define KERNEL_STACK_SIZE (64 * 1024)
#define MIME_DEFAULT_STACK_RESERVE (1024 * 1024)

static LIST_ENTRY g_dead_threads;
static ETHREAD g_idle_thread;

static void initialize_idle(PETHREAD idle, PVOID stack_top, PVOID stack_limit, PKPRCB prcb)
{
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
    t->InitialStack = stack_top;
    t->StackLimit = stack_limit;
    t->State = Running;
    t->Priority = 0;
    t->BasePriority = 0;
    t->IdleThread = TRUE;
    t->Processor = (UCHAR)prcb->Number;
    idle->SystemThread = TRUE;
    prcb->CurrentThread = t;
    prcb->IdleThread = t;
}

void PsInitializeIdleThread(void)
{
    InitializeListHead(&g_dead_threads);
    initialize_idle(&g_idle_thread,
                    (PVOID)(MIME_KVA_PHYSMAP + HalBootInfo->boot_stack_phys + HalBootInfo->boot_stack_size),
                    (PVOID)(MIME_KVA_PHYSMAP + HalBootInfo->boot_stack_phys), KeGetCurrentPrcb());
}

/* the idle thread of a secondary processor; returns the top of its stack, where the processor starts. */
PVOID PsCreateIdleThread(PKPRCB prcb)
{
    PETHREAD idle = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(ETHREAD), KI_TAG('I', 'd', 'l', 'e'));
    PVOID limit;
    PVOID alloc = idle ? MmAllocateKernelStack(KERNEL_STACK_SIZE, &limit) : nullptr;
    if (!alloc) {
        if (idle)
            ExFreePool(idle);
        return nullptr;
    }
    PVOID top = (PUCHAR)limit + KERNEL_STACK_SIZE;
    initialize_idle(idle, top, limit, prcb);
    idle->Tcb.StackAllocation = alloc;
    idle->Tcb.StackAllocationSize = KERNEL_STACK_SIZE;
    return top;
}

void PspThreadDelete(PVOID object)
{
    PETHREAD thread = object;
    if (thread->Tcb.StackAllocation)
        MmFreeKernelStack(thread->Tcb.StackAllocation, thread->Tcb.StackAllocationSize);
    if (thread->Cid.UniqueThread)
        PspFreeId(thread->Cid.UniqueThread);
    SepFreeThreadToken(thread);
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
    NTSTATUS status = ObCreateObject(PsThreadTypeObject, nullptr, KernelMode, sizeof(ETHREAD), (PVOID *)&thread);
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

/* enters user mode through the trap frame PspCreateUserThread built. */
static void user_thread_startup(PVOID context)
{
    PKTHREAD thread = KiCurrentThread();
    PKTRAP_FRAME frame = thread->TrapFrame;
    ((PEPROCESS)thread->Process)->TokenFrozen = TRUE;
    thread->PreviousMode = UserMode;
    KiExitToUserMode(frame);
    KiEnterUserMode(frame);
}

#define MIME_NATIVE_STACK_SIZE (1024 * 1024)

/*
 * the 32-bit half of a wow64 thread, as windows lays it out: the 32-bit teb
 * 0x2000 bytes after the 64-bit one, each pointing at the other, the 64-bit
 * teb's exception list holding the 32-bit teb, its stack the 64-bit stack
 * and the 32-bit teb's the program's, and the cpu area at the top of the
 * 64-bit stack.
 */
static void setup_wow64_teb(PEPROCESS process, PETHREAD thread, PTEB teb, PVOID stack, SIZE_T stack_size,
                            ULONG_PTR native_top, ULONG_PTR *native_base)
{
    PUCHAR teb32 = (PUCHAR)teb + MIME_TEB_SIZE;
    PULONG t = (PULONG)teb32;
    ULONG_PTR cpu = (native_top - sizeof(WOW64_CPURESERVED)) & ~(ULONG_PTR)63;
    PWOW64_CPURESERVED area = (PWOW64_CPURESERVED)cpu;
    memset(area, 0, sizeof(*area));
    area->Machine = IMAGE_FILE_MACHINE_I386;
    teb->TlsSlots[WOW64_TLS_CPURESERVED] = area;
    teb->WowTebOffset = MIME_TEB_SIZE;
    teb->NtTib.ExceptionList = (PVOID)teb32;
    *native_base = cpu;

    t[MIME_TEB32_EXCEPTION_LIST / 4] = 0xFFFFFFFFu;
    t[MIME_TEB32_STACK_BASE / 4] = (ULONG)((ULONG_PTR)stack + stack_size);
    t[MIME_TEB32_STACK_LIMIT / 4] = (ULONG)(ULONG_PTR)stack;
    t[MIME_TEB32_SELF / 4] = (ULONG)(ULONG_PTR)teb32;
    t[MIME_TEB32_CLIENT_ID / 4] = (ULONG)(ULONG_PTR)thread->Cid.UniqueProcess;
    t[MIME_TEB32_CLIENT_ID / 4 + 1] = (ULONG)(ULONG_PTR)thread->Cid.UniqueThread;
    t[MIME_TEB32_REAL_CLIENT_ID / 4] = (ULONG)(ULONG_PTR)thread->Cid.UniqueProcess;
    t[MIME_TEB32_REAL_CLIENT_ID / 4 + 1] = (ULONG)(ULONG_PTR)thread->Cid.UniqueThread;
    t[MIME_TEB32_PEB / 4] = (ULONG)(ULONG_PTR)process->Wow64Peb;
    t[MIME_TEB32_LOCALE / 4] = 0x0409;
    t[MIME_TEB32_DEALLOCATION_STACK / 4] = (ULONG)(ULONG_PTR)stack;
    t[MIME_TEB32_WOW_TEB_OFFSET / 4] = (ULONG)-MIME_TEB_SIZE;
    MIME_STRING32 *string = (MIME_STRING32 *)(teb32 + MIME_TEB32_STATIC_STRING);
    string->MaximumLength = 522;
    string->Buffer = (ULONG)(ULONG_PTR)teb32 + MIME_TEB32_STATIC_BUFFER;
    t[MIME_TEB32_TLS_LINKS / 4] = t[MIME_TEB32_TLS_LINKS / 4 + 1] = (ULONG)(ULONG_PTR)teb32 + MIME_TEB32_TLS_LINKS;
    thread->Tcb.Teb32 = teb32;
}

/* allocates the user stack and teb, then creates the kernel half of the thread. */
NTSTATUS PspCreateUserThread(PEPROCESS process, PVOID start, PVOID parameter, BOOLEAN suspended,
                             SIZE_T stack_reserve, SIZE_T stack_commit, PETHREAD *out)
{
    PPSP_SYSTEM_DLL dll = process->SystemDll;
    if (!dll)
        return STATUS_DLL_NOT_FOUND;
    PVOID ldr_thunk = dll->LdrInitializeThunk;
    PVOID user_start = dll->RtlUserThreadStart;
    BOOLEAN wow64 = PspIsWow64Process(process);
    /* a wow64 thread runs 64-bit code on a stack of its own too, like the translator. */
    BOOLEAN translated = process->Machine != MIME_NATIVE_MACHINE;
    if (!stack_reserve)
        stack_reserve = MIME_DEFAULT_STACK_RESERVE;
    stack_reserve = (stack_reserve + 0xFFFF) & ~(SIZE_T)0xFFFF;

    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    KiAttachProcess(&process->Pcb);

    PVOID stack = nullptr, teb_va = nullptr;
    SIZE_T size = stack_reserve;
    NTSTATUS status = MiAllocateVirtual(process, &stack, &size, MEM_RESERVE | MEM_COMMIT | (process->Machine == IMAGE_FILE_MACHINE_I386 ? 0 : MEM_TOP_DOWN), PAGE_READWRITE);
    /* the 32-bit teb that follows a wow64 thread's must be reachable from 32-bit code. */
    SIZE_T teb_size = MIME_TEB_SIZE + (wow64 ? MIME_TEB32_SIZE : 0);
    if (NT_SUCCESS(status))
        status = MiAllocateVirtual(process, &teb_va, &teb_size, MEM_RESERVE | MEM_COMMIT | (wow64 ? 0 : MEM_TOP_DOWN),
                                   PAGE_READWRITE);
    /* the translator runs on a stack of its own, the program keeps the thread stack. */
    PVOID native_stack = nullptr;
    SIZE_T native_size = MIME_NATIVE_STACK_SIZE;
    if (NT_SUCCESS(status) && translated)
        status = MiAllocateVirtual(process, &native_stack, &native_size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN,
                                   PAGE_READWRITE);
    if (!NT_SUCCESS(status)) {
        KiAttachProcess(saved);
        return status;
    }

    PETHREAD thread;
    status = PspCreateThread(process, FALSE, user_thread_startup, nullptr, &thread);
    if (!NT_SUCCESS(status)) {
        KiAttachProcess(saved);
        return status;
    }
    thread->UserStackAllocation = stack;
    thread->UserStackSize = stack_reserve;
    thread->NativeStackAllocation = native_stack;
    thread->NativeStackSize = native_stack ? native_size : 0;
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
    ULONG64 sp = native_stack ? (ULONG64)native_stack + native_size : ctx_va;
    if (wow64) {
        /* the 64-bit teb describes the 64-bit stack, which holds the cpu area and the 64-bit start context. */
        ULONG_PTR native_base;
        setup_wow64_teb(process, thread, teb, stack, stack_reserve, (ULONG_PTR)native_stack + native_size,
                        &native_base);
        teb->NtTib.StackBase = (PVOID)native_base;
        teb->NtTib.StackLimit = native_stack;
        teb->DeallocationStack = native_stack;
        ctx_va = (native_base - sizeof(CONTEXT)) & ~15ull;
        sp = ctx_va;
    }
    KiInitializeUserContext((PCONTEXT)ctx_va, (ULONG64)user_start, ctx_va, (ULONG64)start, (ULONG64)parameter,
                            (ULONG64)teb_va);
    KiInitializeUserFrame(thread->Tcb.TrapFrame, (ULONG64)ldr_thunk, sp, ctx_va, (ULONG64)process->NtdllBase,
                          (ULONG64)teb_va);
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
    if (process->DirectoryTableBase)
        KiSwitchAddressSpace(process);
    KiReleaseDispatcherLock(s);
    if (process == t->Process)
        PspCheckForSuspendApc(t);
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

/* a list link of the program: eight bytes, or four for a wow64 program. */
static ULONG64 link_get(const UCHAR *raw, ULONG width)
{
    return width == 4 ? *(const ULONG *)raw : *(const ULONG64 *)raw;
}

static void link_put(ULONG64 user, ULONG64 value, ULONG width)
{
    ULONG narrow = (ULONG)value;
    KiCopyToUser((PVOID)(ULONG_PTR)user, width == 4 ? (PVOID)&narrow : (PVOID)&value, width);
}

/*
 * kernelbase keeps the tebs of a process in a list through TlsLinks. a thread
 * that is terminated ends without leaving it, and the next thread to join
 * would write through its teb, freed below.
 */
static void unlink_tls_links(PVOID links, ULONG width)
{
    UCHAR raw[16];
    if (!links || !NT_SUCCESS(KiCopyFromUser(raw, links, 2 * width)))
        return;
    ULONG64 self = (ULONG64)(ULONG_PTR)links, next = link_get(raw, width), previous = link_get(raw + width, width);
    if (!next || !previous || next == self || !KiIsUserRange((PVOID)(ULONG_PTR)previous, 2 * width) ||
        !KiIsUserRange((PVOID)(ULONG_PTR)next, 2 * width))
        return;
    link_put(previous, next, width);
    link_put(next + width, previous, width);
    link_put(self, self, width);
    link_put(self + width, self, width);
}

/*
 * the stacks and the teb of a thread that ends. the program cannot free
 * them, as it runs on one until it is gone, so the kernel does, from the
 * thread's own process while it is still whole.
 */
static void release_user_memory(PEPROCESS process, PETHREAD thread)
{
    if (thread->Tcb.Teb) {
        unlink_tls_links(&((PTEB)thread->Tcb.Teb)->TlsLinks, sizeof(ULONG64));
        if (thread->Tcb.Teb32)
            unlink_tls_links((PUCHAR)thread->Tcb.Teb32 + MIME_TEB32_TLS_LINKS, sizeof(ULONG));
    }
    PVOID regions[] = {thread->UserStackAllocation, thread->NativeStackAllocation, thread->Tcb.Teb};
    for (ULONG i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
        PVOID base = regions[i];
        SIZE_T size = 0;
        if (base)
            MiFreeVirtual(process, &base, &size, MEM_RELEASE);
    }
    thread->UserStackAllocation = nullptr;
    thread->NativeStackAllocation = nullptr;
}

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
    CmNotifyRunDown(thread);

    /* abandon owned mutants. */
    while (!IsListEmpty(&thread->Tcb.MutantListHead)) {
        PKMUTANT m = CONTAINING_RECORD(thread->Tcb.MutantListHead.Flink, KMUTANT, MutantListEntry);
        KeReleaseMutant(m, 0, TRUE, FALSE);
    }

    if (!process->SystemProcess)
        release_user_memory(process, thread);

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
    else if (thread->Tcb.State == Initialized)
        KiReadyThread(&thread->Tcb);
    else
        KiInterruptThread(&thread->Tcb);
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
    /* the descriptor a creator asks for is worked out first, so a refused one starts no thread. */
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    SD_BLOB *sd = nullptr;
    status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (NT_SUCCESS(status))
        status = ObComputeSecurity(PsThreadTypeObject, attributes ? &oa : nullptr, nullptr, KeGetPreviousMode(), &sd);
    KiFreeCapturedString(&name);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(process);
        return status;
    }
    PETHREAD thread;
    status = PspCreateUserThread(process, start, parameter, (flags & THREAD_CREATE_FLAGS_CREATE_SUSPENDED) != 0,
                                 max_stack_size, stack_size, &thread);
    ObDereferenceObject(process);
    if (!NT_SUCCESS(status)) {
        if (sd)
            SeSdDereference(sd);
        return status;
    }
    ObAttachSecurity(thread, sd);
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
    if (previous) {
        ULONG zero = 0;
        status = KiCopyToUser(previous, &zero, sizeof(zero));
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(thread);
            return status;
        }
    }
    ULONG64 s = KiAcquireDispatcherLock();
    ULONG old = thread->Tcb.SuspendCount;
    if (old) {
        thread->Tcb.SuspendCount--;
        if (old == 1)
            KeSetEvent(&thread->Tcb.SuspendEvent, 0, FALSE);
        if (old == 1 && thread->Tcb.State == Initialized)
            KiReadyThread(&thread->Tcb);
        else if (old == 1 && thread->Tcb.Suspended)
            KeReleaseSemaphore(&thread->Tcb.SuspendSemaphore, 0, 1, FALSE);
    }
    KiReleaseDispatcherLock(s);
    if (previous)
        status = KiCopyToUser(previous, &old, sizeof(old));
    ObDereferenceObject(thread);
    return status;
}

static void NTAPI suspend_kernel_routine(PKAPC apc, PKNORMAL_ROUTINE *normal, PVOID *context, PVOID *a1, PVOID *a2)
{
}

static void NTAPI suspend_normal_routine(PVOID context, PVOID arg1, PVOID arg2)
{
    PKTHREAD thread = KiCurrentThread();
    ULONG64 state = KiAcquireDispatcherLock();
    if (thread->SuspendApcActive || thread->ApcStateProcess != thread->Process) {
        KiReleaseDispatcherLock(state);
        return;
    }
    thread->SuspendApcActive = TRUE;
    KiReleaseDispatcherLock(state);
    if (thread->UserSuspendGate) {
        MX_SUSPEND_GATE gate;
        NTSTATUS status = KiCopyFromUser(&gate, thread->UserSuspendGate, sizeof(gate));
        ULONG pending = NT_SUCCESS(status) && gate.Critical ? 1 : 0;
        if (NT_SUCCESS(status))
            status = KiCopyToUser((PVOID)&((MX_SUSPEND_GATE *)thread->UserSuspendGate)->Pending, &pending, sizeof(pending));
        if (!NT_SUCCESS(status) || pending) {
            thread->SuspendApcActive = FALSE;
            if (!NT_SUCCESS(status))
                PspExitThread(STATUS_ACCESS_VIOLATION);
            return;
        }
    }
    for (;;) {
        state = KiAcquireDispatcherLock();
        if (!thread->SuspendCount || thread->Terminating) {
            thread->Suspended = FALSE;
            thread->SuspendApcActive = FALSE;
            KiReleaseDispatcherLock(state);
            return;
        }
        thread->Suspended = TRUE;
        KeSetEvent(&thread->SuspendEvent, 0, FALSE);
        KiReleaseDispatcherLock(state);
        KeWaitForSingleObject(&thread->SuspendSemaphore, Suspended, UserMode, FALSE, nullptr);
    }
}

void PspCheckForSuspendApc(PKTHREAD thread)
{
    ULONG64 state = KiAcquireDispatcherLock();
    if (thread->SuspendCount && !thread->Terminating && thread->State != Initialized &&
        thread->ApcStateProcess == thread->Process &&
        !thread->SuspendApcActive && !thread->SuspendApc.Inserted) {
        KeInitializeApc(&thread->SuspendApc, thread, OriginalApcEnvironment, suspend_kernel_routine, nullptr,
                        suspend_normal_routine, KernelMode, nullptr);
        KeInsertQueueApc(&thread->SuspendApc, nullptr, nullptr, 0);
    }
    KiReleaseDispatcherLock(state);
}

NTSTATUS PspWaitForSuspension(PETHREAD thread)
{
    for (;;) {
        ULONG64 state = KiAcquireDispatcherLock();
        BOOLEAN terminated = thread->Tcb.Terminating;
        BOOLEAN ready = !thread->Tcb.SuspendCount || thread->Tcb.Suspended || thread->Tcb.State == Initialized ||
                        thread == PsCurrentThread();
        KiReleaseDispatcherLock(state);
        if (terminated) return STATUS_THREAD_IS_TERMINATING;
        if (ready) return STATUS_SUCCESS;
        PVOID objects[2] = {thread, &thread->Tcb.SuspendEvent};
        NTSTATUS status = KeWaitForMultipleObjects(2, objects, WaitAny, Executive, KernelMode, FALSE, nullptr, nullptr);
        if (status != STATUS_WAIT_0 && status != STATUS_WAIT_0 + 1)
            return status;
    }
}

NTSTATUS NTAPI NtSuspendThread(HANDLE handle, PULONG previous)
{
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_SUSPEND_RESUME, &thread);
    if (!NT_SUCCESS(status)) return status;
    if (previous) {
        ULONG zero = 0;
        status = KiCopyToUser(previous, &zero, sizeof(zero));
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(thread);
            return status;
        }
    }
    ULONG64 state = KiAcquireDispatcherLock();
    ULONG old = thread->Tcb.SuspendCount;
    if (thread->SystemThread)
        status = STATUS_ACCESS_DENIED;
    else if (thread->Tcb.Terminating)
        status = STATUS_THREAD_IS_TERMINATING;
    else if (old == 127)
        status = (NTSTATUS)0xC000004AL;
    else {
        thread->Tcb.SuspendCount++;
        if (!old)
            KeClearEvent(&thread->Tcb.SuspendEvent);
        PspCheckForSuspendApc(&thread->Tcb);
    }
    KiReleaseDispatcherLock(state);
    if (NT_SUCCESS(status) && previous)
        status = KiCopyToUser(previous, &old, sizeof(old));
    ObDereferenceObject(thread);
    return status;
}

/* every thread of a process suspended or resumed once more, as the debuggers' calls do. */
static NTSTATUS suspend_or_resume_process(HANDLE handle, BOOLEAN suspend)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(handle, PROCESS_SUSPEND_RESUME, &process);
    if (!NT_SUCCESS(status))
        return status;
    if (process->SystemProcess) {
        ObDereferenceObject(process);
        return STATUS_ACCESS_DENIED;
    }
    if (process->Terminating) {
        ObDereferenceObject(process);
        return STATUS_PROCESS_IS_TERMINATING;
    }
    ULONG64 state = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = process->ThreadListHead.Flink; e != &process->ThreadListHead; e = e->Flink) {
        PETHREAD thread = CONTAINING_RECORD(e, ETHREAD, ThreadListEntry);
        if (thread->Tcb.Terminating || thread->SystemThread)
            continue;
        ULONG old = thread->Tcb.SuspendCount;
        if (suspend) {
            if (old >= 127)
                continue;
            thread->Tcb.SuspendCount++;
            if (!old)
                KeClearEvent(&thread->Tcb.SuspendEvent);
            PspCheckForSuspendApc(&thread->Tcb);
        } else if (old) {
            thread->Tcb.SuspendCount--;
            if (old == 1) {
                KeSetEvent(&thread->Tcb.SuspendEvent, 0, FALSE);
                if (thread->Tcb.State == Initialized)
                    KiReadyThread(&thread->Tcb);
                else if (thread->Tcb.Suspended)
                    KeReleaseSemaphore(&thread->Tcb.SuspendSemaphore, 0, 1, FALSE);
            }
        }
    }
    KiReleaseDispatcherLock(state);
    ObDereferenceObject(process);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtSuspendProcess(HANDLE handle)
{
    return suspend_or_resume_process(handle, TRUE);
}

NTSTATUS NTAPI NtResumeProcess(HANDLE handle)
{
    return suspend_or_resume_process(handle, FALSE);
}

NTSTATUS NTAPI NtQueryInformationThread(HANDLE handle, THREADINFOCLASS klass, PVOID info, ULONG length,
                                        PULONG returned)
{
    /* what anyone who can see a thread may know of it, with either the full or the limited right. */
    ACCESS_MASK rights = THREAD_QUERY_INFORMATION;
    switch (klass) {
    case ThreadBasicInformation:
    case ThreadTimes:
    case ThreadPriority:
    case ThreadPriorityBoost:
    case ThreadIdealProcessorEx:
    case ThreadIsTerminated:
        rights = THREAD_QUERY_LIMITED_INFORMATION;
        break;
    default:
        break;
    }
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, rights, &thread);
    if (status == STATUS_ACCESS_DENIED && rights != THREAD_QUERY_INFORMATION)
        status = PsReferenceThreadHandle(handle, THREAD_QUERY_INFORMATION, &thread);
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
        out.basic.AffinityMask = thread->Tcb.Affinity;
        out.basic.Priority = thread->Tcb.Priority;
        out.basic.BasePriority = KiQueryBasePriorityThread(&thread->Tcb);
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
    case ThreadPriority:
        out.u32 = (ULONG)thread->Tcb.BasePriority;
        size = sizeof(ULONG);
        break;
    case ThreadPriorityBoost:
        out.u32 = thread->Tcb.DisableBoost;
        size = sizeof(ULONG);
        break;
    case ThreadIdealProcessorEx:
        /* a processor number: the group, the processor and a reserved byte. */
        out.u32 = (ULONG)thread->Tcb.IdealProcessor << 16;
        size = sizeof(ULONG);
        break;
    case ThreadIsTerminated:
        out.u32 = thread->HasTerminated;
        size = sizeof(ULONG);
        break;
    case ThreadHideFromDebugger:
        out.u32 = thread->HideFromDebugger;
        size = 1;
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
    if (klass == ThreadImpersonationToken)
        return SepSetThreadToken(handle, info, length);
    /* the scheduling classes need a length and a value, then a handle that may set them. */
    ACCESS_MASK rights = THREAD_SET_INFORMATION;
    ULONG want = 0;
    switch (klass) {
    case ThreadPriority:
    case ThreadBasePriority:
    case ThreadPriorityBoost:
    case ThreadIdealProcessor:
    case ThreadIdealProcessorEx:
        want = sizeof(ULONG);
        break;
    case ThreadAffinityMask:
        want = sizeof(ULONG64);
        break;
    default:
        break;
    }
    if (klass != ThreadIdealProcessor && klass != ThreadIdealProcessorEx && want)
        rights = THREAD_SET_LIMITED_INFORMATION;
    ULONG64 value = 0;
    if (want) {
        if (length != want)
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!NT_SUCCESS(KiCopyFromUser(&value, info, want)))
            return STATUS_ACCESS_VIOLATION;
    }
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, rights, &thread);
    if (status == STATUS_ACCESS_DENIED && rights != THREAD_SET_INFORMATION)
        status = PsReferenceThreadHandle(handle, THREAD_SET_INFORMATION, &thread);
    if (!NT_SUCCESS(status))
        return status;
    if ((ULONG)klass == MX_THREAD_SUSPEND_GATE) {
        PVOID gate = nullptr;
        MX_SUSPEND_GATE values;
        if (thread != PsCurrentThread())
            status = STATUS_ACCESS_DENIED;
        else if (length != sizeof(gate))
            status = STATUS_INFO_LENGTH_MISMATCH;
        else {
            status = KiCopyFromUser(&gate, info, sizeof(gate));
            if (NT_SUCCESS(status) && gate) {
                status = KiCopyFromUser(&values, gate, sizeof(values));
                ULONG pending = 0;
                if (NT_SUCCESS(status))
                    status = KiCopyToUser((PVOID)&((MX_SUSPEND_GATE *)gate)->Pending, &pending, sizeof(pending));
            }
            if (NT_SUCCESS(status))
                thread->Tcb.UserSuspendGate = gate;
        }
    } else if (klass == ThreadDynamicCodePolicyInfo) {
        status = PspSetThreadDynamicCodePolicy(thread, info, length);
    } else if (klass == ThreadHideFromDebugger) {
        /* the call carries nothing, and there is no debugger here to hide from. */
        if (length)
            status = STATUS_INFO_LENGTH_MISMATCH;
        else
            thread->HideFromDebugger = TRUE;
    } else if (klass == ThreadPriority) {
        LONG priority = (LONG)(ULONG)value;
        if (priority < 1 || priority > 31)
            status = STATUS_INVALID_PARAMETER;
        else if (priority >= 16 && !SepSinglePrivilegeCheck(SE_INC_BASE_PRIORITY_PRIVILEGE, KeGetPreviousMode()))
            status = STATUS_PRIVILEGE_NOT_HELD;
        else
            KiSetPriorityThread(&thread->Tcb, priority);
    } else if (klass == ThreadBasePriority) {
        /* a step of two either way, or a limit, are what anyone may ask for. */
        LONG increment = (LONG)(ULONG)value;
        if ((increment < -2 || increment > 2) && increment != 16 && increment != -16)
            status = STATUS_INVALID_PARAMETER;
        else
            KiSetBasePriorityThread(&thread->Tcb, increment);
    } else if (klass == ThreadPriorityBoost) {
        thread->Tcb.DisableBoost = (ULONG)value != 0;
    } else if (klass == ThreadAffinityMask) {
        PEPROCESS process = (PEPROCESS)thread->Tcb.Process;
        if (!value || (value & ~KiProcessAffinity(&process->Pcb)))
            status = STATUS_INVALID_PARAMETER;
        else
            thread->Tcb.Affinity = value;
    } else if (klass == ThreadIdealProcessor) {
        /* the previous one is the status, and a processor that does not exist leaves it alone. */
        ULONG ideal = (ULONG)value;
        if (ideal > 64) {
            status = STATUS_INVALID_PARAMETER;
        } else {
            status = (NTSTATUS)thread->Tcb.IdealProcessor;
            if (ideal < KeNumberProcessors)
                thread->Tcb.IdealProcessor = (UCHAR)ideal;
        }
    } else if (klass == ThreadIdealProcessorEx) {
        /* a processor number goes in, the previous one comes back in its place. */
        USHORT group = (USHORT)value;
        UCHAR number = (UCHAR)(value >> 16);
        if (group || number >= KeNumberProcessors) {
            status = STATUS_INVALID_PARAMETER;
        } else {
            ULONG previous = (ULONG)thread->Tcb.IdealProcessor << 16;
            thread->Tcb.IdealProcessor = number;
            status = KiCopyToUser(info, &previous, sizeof(previous));
        }
    }
    ObDereferenceObject(thread);
    return status;
}

NTSTATUS NTAPI NtYieldExecution(void)
{
    LARGE_INTEGER zero = {};
    return KeDelayExecutionThread(UserMode, FALSE, &zero);
}

NTSTATUS NTAPI NtDelayExecution(BOOLEAN alertable, PLARGE_INTEGER interval)
{
    LARGE_INTEGER t;
    if (!NT_SUCCESS(KiCopyFromUser(&t, interval, sizeof(t))))
        return STATUS_ACCESS_VIOLATION;
    NTSTATUS status = KeDelayExecutionThread(UserMode, alertable, &t);
    return status == STATUS_NO_YIELD_PERFORMED ? STATUS_SUCCESS : status;
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


/*
 * the thread parking of windows 8 and later, which ntdll builds address
 * waits, srw locks and condition variables on. an alert sent before the
 * wait is kept, so waking never races with going to sleep.
 */
NTSTATUS NTAPI NtAlertThreadByThreadId(HANDLE id)
{
    PETHREAD thread;
    NTSTATUS status = PsLookupThreadByThreadId(id, &thread);
    if (!NT_SUCCESS(status))
        return status;
    if (thread->Tcb.Process == KiCurrentThread()->Process)
        KeSetEvent(&thread->Tcb.AlertByIdEvent, 0, FALSE);
    else
        status = STATUS_ACCESS_DENIED;
    ObDereferenceObject(thread);
    return status;
}

NTSTATUS NTAPI NtWaitForAlertByThreadId(PVOID hint, PLARGE_INTEGER timeout)
{
    LARGE_INTEGER t;
    if (timeout && !NT_SUCCESS(KiCopyFromUser(&t, timeout, sizeof(t))))
        return STATUS_ACCESS_VIOLATION;
    NTSTATUS status =
        KeWaitForSingleObject(&KiCurrentThread()->AlertByIdEvent, UserRequest, UserMode, FALSE, timeout ? &t : nullptr);
    return status == STATUS_WAIT_0 ? STATUS_ALERTED : status;
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
    KeInitializeApc(apc, &thread->Tcb, OriginalApcEnvironment, user_apc_kernel_routine, nullptr,
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

/*
 * every thread of every process in the log, for a machine that hangs: its
 * state, what it waits for, where its user code called the kernel and the
 * return address on top of the user stack there. passive level: it
 * attaches to each process to read its stack.
 */
void PsDumpThreads(void)
{
    static const char *const states[] = {"initialized", "ready", "running", "standby", "terminated", "waiting"};
    enum { MAX = 512 };
    static PETHREAD threads[MAX];
    ULONG n = 0;
    ULONG64 s = KiAcquireDispatcherLock();
    for (PLIST_ENTRY p = PsActiveProcessHead.Flink; p != &PsActiveProcessHead; p = p->Flink) {
        PEPROCESS process = CONTAINING_RECORD(p, EPROCESS, ActiveProcessLinks);
        for (PLIST_ENTRY t = process->ThreadListHead.Flink; t != &process->ThreadListHead && n < MAX; t = t->Flink) {
            threads[n] = CONTAINING_RECORD(t, ETHREAD, ThreadListEntry);
            ObReferenceObject(threads[n++]);
        }
    }
    KiReleaseDispatcherLock(s);
    for (ULONG i = 0; i < n; i++) {
        PKTHREAD k = &threads[i]->Tcb;
        PEPROCESS process = CONTAINING_RECORD(k->Process, EPROCESS, Pcb);
        const char *state = k->State < RTL_NUMBER_OF(states) ? states[k->State] : "?";
        PVOID object = k->State == Waiting && k->WaitBlockList ? k->WaitBlockList->Object : nullptr;
        PKTRAP_FRAME frame = k->TrapFrame;
        ULONG64 pc = 0, sp = 0, top = 0;
        char chain[512];
        chain[0] = 0;
        if (frame && KI_TRAP_FROM_USER(frame) && !process->SystemProcess) {
            pc = KI_TRAP_PC(frame);
            sp = KI_TRAP_SP(frame);
            PKPROCESS saved;
            KeStackAttachProcess(&process->Pcb, &saved);
            if (!NT_SUCCESS(KiCopyFromUser(&top, (PVOID)sp, sizeof(top))))
                top = 0;
            KiFormatUserStack(process, sp, chain, sizeof(chain));
            KeUnstackDetachProcess(&saved);
        }
        KiLogPrintf("%s %u thread %u %s reason %u object %p type %d pc %p sp %p return %p", process->ImageFileName,
                    (ULONG)(ULONG_PTR)process->UniqueProcessId, (ULONG)(ULONG_PTR)threads[i]->Cid.UniqueThread, state,
                    k->WaitReason, object, object ? ((DISPATCHER_HEADER *)object)->Type : -1, (PVOID)pc, (PVOID)sp,
                    (PVOID)top);
        if (chain[0])
            KiLogPrintf("  calls%s", chain);
        ObDereferenceObject(threads[i]);
    }
}
