/* threads: creation, user mode entry, termination and reaping. */
#include "../ntos.h"
#include "classref.h"
#include "../se/se.h"
#include <mime/mx64.h>
#include <mime/wow32.h>

#define KERNEL_STACK_SIZE (64 * 1024)
/*
 * nested callbacks into user mode keep their kernel frames, about 5 KB for each level of a message sent to a window.
 * the room is address space only, enough for the user stack to be what ends a deep nest as on windows.
 */
#define KERNEL_STACK_RESERVE (4 * 1024 * 1024)
#define MIME_DEFAULT_STACK_RESERVE (1024 * 1024)
/* stack below the guard page kept for stack overflow handlers, a quarter of a small stack. */
#define MIME_STACK_OVERFLOW_ZONE (32 * 1024)

static LIST_ENTRY g_dead_threads;
static KEVENT g_reap_event;
static BOOLEAN g_reaper_started;
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
    KeInitializeEvent(&g_reap_event, SynchronizationEvent, FALSE);
    initialize_idle(&g_idle_thread,
                    (PVOID)(MIME_KVA_PHYSMAP + HalBootInfo->boot_stack_phys + HalBootInfo->boot_stack_size),
                    (PVOID)(MIME_KVA_PHYSMAP + HalBootInfo->boot_stack_phys), KeGetCurrentPrcb());
}

/* returns the top of the idle thread's stack, where the secondary processor starts. */
PVOID PsCreateIdleThread(PKPRCB prcb)
{
    PETHREAD idle = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(ETHREAD), KI_TAG('I', 'd', 'l', 'e'));
    PVOID limit, top;
    PVOID alloc = idle ? MmAllocateKernelStack(KERNEL_STACK_SIZE, KERNEL_STACK_SIZE, &limit, &top) : nullptr;
    if (!alloc) {
        if (idle)
            ExFreePool(idle);
        return nullptr;
    }
    initialize_idle(idle, top, limit, prcb);
    idle->Tcb.StackAllocation = alloc;
    idle->Tcb.StackAllocationSize = KERNEL_STACK_SIZE;
    return top;
}

void PspThreadDelete(PVOID object)
{
    PETHREAD thread = object;
    if (thread->Tcb.StackAllocation)
        MmFreeKernelStack(thread->Tcb.StackAllocation, thread->Tcb.StackLimit, thread->Tcb.StackAllocationSize);
    if (thread->Cid.UniqueThread)
        PspFreeId(thread->Cid.UniqueThread);
    SepFreeThreadToken(thread);
    if (thread->Tcb.Process)
        ObDereferenceObject(thread->Tcb.Process);
}

/*
 * threads cannot free their own stack, so a system thread releases dead ones. the last reference to a thread can take
 * its process, sections and files with it, whose teardown waits on locks, which the idle loop must never do: an idle
 * thread that waits finds nothing else to run and goes on as if the wait were satisfied.
 */
static void NTAPI reaper(PVOID context)
{
    for (;;) {
        KeWaitForSingleObject(&g_reap_event, Executive, KernelMode, FALSE, nullptr);
        for (;;) {
            ULONG64 s = KiAcquireDispatcherLock();
            if (IsListEmpty(&g_dead_threads)) {
                KiReleaseDispatcherLock(s);
                break;
            }
            PETHREAD t = CONTAINING_RECORD(RemoveHeadList(&g_dead_threads), ETHREAD, ThreadListEntry);
            KiReleaseDispatcherLock(s);
            ObDereferenceObject(t);
        }
    }
}

void PspStartReaper(void)
{
    if (NT_SUCCESS(PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, reaper, nullptr)))
        __atomic_store_n(&g_reaper_started, TRUE, __ATOMIC_RELEASE);
}

/* the idle loop wakes the reaper when threads died. */
void PspReapDeadThreads(void)
{
    if (__atomic_load_n(&g_reaper_started, __ATOMIC_ACQUIRE) && !IsListEmpty(&g_dead_threads))
        KeSetEvent(&g_reap_event, 0, TRUE);
}

NTSTATUS PspCreateThread(PEPROCESS process, BOOLEAN system, PKSTART_ROUTINE routine, PVOID context, PETHREAD *out)
{
    PETHREAD thread;
    NTSTATUS status = ObCreateObject(PsThreadTypeObject, nullptr, KernelMode, sizeof(ETHREAD), (PVOID *)&thread);
    if (!NT_SUCCESS(status))
        return status;
    memset(thread, 0, sizeof(*thread));
    PVOID limit, top;
    PVOID alloc = MmAllocateKernelStack(KERNEL_STACK_SIZE, KERNEL_STACK_RESERVE, &limit, &top);
    if (!alloc) {
        ObDereferenceObject(thread);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    thread->Tcb.StackAllocation = alloc;
    thread->Tcb.StackAllocationSize = KERNEL_STACK_RESERVE;
    thread->Tcb.StackLimit = limit;
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
    KiInitializeThread(&thread->Tcb, &process->Pcb, top, limit, routine, context);

    ULONG64 s = KiAcquireDispatcherLock();
    InsertTailList(&process->ThreadListHead, &thread->ThreadListEntry);
    process->ThreadCount++;
    KiReleaseDispatcherLock(s);

    /* one reference stays with the running thread until it exits. */
    ObReferenceObject(thread);
    PspNotifyThread(thread, TRUE);
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
    PEPROCESS process = (PEPROCESS)thread->Process;
    PETHREAD ethread = (PETHREAD)thread;
    process->TokenFrozen = TRUE;
    thread->PreviousMode = UserMode;
    if (ethread->FirstEntryContext) {
        CONTEXT start;
#ifdef __x86_64__
        /* like windows, a thread starts without the state beyond sse another thread may have set before it ran. */
        KiResetXstate(thread);
#endif
        KiTrapFrameToContext(frame, &start, CONTEXT_FULL);
        start.ContextFlags = CONTEXT_FULL;
        KiCopyToUser((PVOID)ethread->FirstEntryContext, &start, sizeof(start));
        KiInitializeUserFrame(frame, (ULONG64)process->SystemDll->LdrInitializeThunk, ethread->FirstEntryStack,
                              ethread->FirstEntryContext, (ULONG64)process->NtdllBase, (ULONG64)thread->Teb);
        ethread->FirstEntryContext = 0;
        /* like windows, a thread of a process with an instrumentation callback starts there, r10 the thunk. */
        thread->InstrumentReturn = TRUE;
    }
    KiExitToUserMode(frame);
    KiEnterUserMode(frame);
}

#define MIME_NATIVE_STACK_SIZE (1024 * 1024)

/* RtlpGetEntireXStateAreaLength2: the bytes an xsave of the features takes, as the shared page describes them. */
static ULONG xstate_area_length(ULONG64 features)
{
    const XSTATE_CONFIGURATION *x = &SharedUserData->XState;
    if (!(x->ControlFlags & 2))
        return x->Size;
    ULONG length = 576;
    for (ULONG i = 2; i < 64; i++) {
        if (!(features >> i & 1))
            continue;
        if (x->AlignedFeatures >> i & 1)
            length = (length + 63) & ~63u;
        length += x->AllFeatures[i];
    }
    return length;
}

/*
 * the cpu area of a 32-bit thread, read from PspWow64SetupCpuArea, PspWow64InitThread and PspWow64InitThreadGuestx86
 * of ntoskrnl 26300 and RtlInitializeExtendedContext2 of its ntdll: the flags and machine, the x86 context, a
 * CONTEXT_EX and, when the processor has extended state x86 code may use, an xsave area. it sits at the top of the
 * 64-bit stack, which ends below it. ntdll's RtlWow64GetCpuAreaInfo finds each part from the shared page the same way.
 */
#define WOW64_X86_FEATURES 0x40000000000009FCull
#define I386_CONTEXT_LENGTH 0x2CC

/* CONTEXT_EX of 26300, which ends with a chunk for the kernel's shadow stack. */
typedef struct {
    CONTEXT_CHUNK All, Legacy, XState, KernelCet;
} CONTEXT_EX_CHUNKS;

static ULONG64 wow64_cpu_features(ULONG *flags)
{
    ULONG64 features = SharedUserData->XState.EnabledFeatures & WOW64_X86_FEATURES;
    *flags = CONTEXT_I386 | (features ? 0x40 : 0); /* CONTEXT_XSTATE */
    return features;
}

/* RtlGetExtendedContextLength3 for an x86 context. */
static ULONG wow64_context_length(ULONG flags, ULONG64 features)
{
    ULONG length = I386_CONTEXT_LENGTH + sizeof(CONTEXT_EX_CHUNKS);
    if (flags & 0x40)
        length = xstate_area_length(features) + ((length + 3) & ~3u) - 4 - 448;
    return length + 3;
}

/* PspWow64SetupCpuArea: the area below the top of the 64-bit stack. */
static ULONG_PTR wow64_cpu_area(ULONG_PTR top)
{
    ULONG flags;
    ULONG64 features = wow64_cpu_features(&flags);
    ULONG_PTR below = ((((top - 4) & ~(ULONG_PTR)3) - 5) & ~(ULONG_PTR)7) - wow64_context_length(flags, features);
    return ((below & ~(ULONG_PTR)3) - 4) & ~(ULONG_PTR)15;
}

/* RtlInitializeExtendedContext2 for the x86 context of the area, which it returns. */
static I386_CONTEXT *init_wow64_context(PWOW64_CPURESERVED area)
{
    ULONG flags;
    ULONG64 features = wow64_cpu_features(&flags);
    PUCHAR context = (PUCHAR)(((ULONG_PTR)area + 4 + 3) & ~(ULONG_PTR)3);
    CONTEXT_EX_CHUNKS *ex = (CONTEXT_EX_CHUNKS *)(context + I386_CONTEXT_LENGTH);
    memset(context, 0, I386_CONTEXT_LENGTH + sizeof(*ex));
    *(PULONG)context = flags;
    ex->All.Offset = ex->Legacy.Offset = -I386_CONTEXT_LENGTH;
    ex->All.Length = I386_CONTEXT_LENGTH + sizeof(*ex);
    /* without the extended registers flag the legacy part of an x86 context ends before them. */
    ex->Legacy.Length = 0xCC;
    ex->XState.Offset = 0x21;
    if (flags & 0x40) {
        PUCHAR xsave = (PUCHAR)(((ULONG_PTR)ex + 95) & ~(ULONG_PTR)63);
        ULONG length = xstate_area_length(features) - 512;
        memset(xsave, 0, length);
        if (SharedUserData->XState.ControlFlags & 2)
            *(PULONG64)(xsave + 8) = features | 1ull << 63;
        ex->XState.Offset = (LONG)(xsave - (PUCHAR)ex);
        ex->XState.Length = length;
        ex->All.Length = (ULONG)(xsave + length - context);
    }
    return (I386_CONTEXT *)context;
}

/*
 * the 32-bit half of a wow64 thread, laid out like windows: the 32-bit teb sits 0x2000 bytes after the 64-bit one
 * and each points at the other. the 64-bit teb's exception list holds the 32-bit teb, its stack fields describe the
 * 64-bit stack, the 32-bit teb's the program's, and the cpu area is at the top of the 64-bit stack.
 */
static void setup_wow64_teb(PEPROCESS process, PETHREAD thread, PTEB teb, PVOID stack, PVOID stack_limit, SIZE_T stack_size,
                            ULONG_PTR native_top, ULONG_PTR *native_base, PVOID start, PVOID parameter)
{
    PUCHAR teb32 = (PUCHAR)teb + MIME_TEB_SIZE;
    PULONG t = (PULONG)teb32;
    ULONG_PTR cpu = wow64_cpu_area(native_top);
    PWOW64_CPURESERVED area = (PWOW64_CPURESERVED)cpu;
    area->Flags = 0;
    area->Machine = IMAGE_FILE_MACHINE_I386;
    I386_CONTEXT *c = init_wow64_context(area);
    /*
     * PspWow64InitThreadGuestx86: the thread starts in the 32-bit RtlUserThreadStart with the entry in eax and the
     * argument in ebx, on its stack less a random multiple of 4 bytes under 2 KiB, and with the floating point
     * state of a reset. the state is there before the thread runs, so another thread can read and change it while
     * this one is suspended.
     */
    ULONG ntdll32 = (ULONG)(ULONG_PTR)process->Peb->SparePointers[0], thread_start = PspWow64NtdllExport("RtlUserThreadStart");
    if (ntdll32 && thread_start) {
        ULONG argument = parameter == process->Peb ? (ULONG)(ULONG_PTR)process->Wow64Peb : (ULONG)(ULONG_PTR)parameter;
        ULONG top = (ULONG)((ULONG_PTR)stack + stack_size) - 4 * (ULONG)(MiRandom() & 0x1FF);
        c->ContextFlags |= CONTEXT_I386_FULL | CONTEXT_I386_FLOATING_POINT | CONTEXT_I386_DEBUG_REGISTERS |
                           CONTEXT_I386_EXTENDED_REGISTERS;
        c->FloatSave.ControlWord = 0x27F;
        c->FloatSave.TagWord = 0xFFFF;
        *(PUSHORT)&c->ExtendedRegisters[0] = 0x27F;
        *(PULONG)&c->ExtendedRegisters[0x18] = 0x1F80;
        *(PULONG)&c->ExtendedRegisters[0x1C] = 0xFFFF;
        c->Eip = ntdll32 + thread_start;
        c->Eax = (ULONG)(ULONG_PTR)start;
        c->Ebx = argument;
        c->Esp = top - 16;
        c->EFlags = 0x202;
        c->SegCs = 0x23;
        c->SegDs = c->SegEs = c->SegSs = c->SegGs = 0x2B;
        c->SegFs = 0x53;
    }
    teb->TlsSlots[WOW64_TLS_CPURESERVED] = area;
    teb->WowTebOffset = MIME_TEB_SIZE;
    teb->NtTib.ExceptionList = (PVOID)teb32;
    *native_base = cpu;

    t[MIME_TEB32_EXCEPTION_LIST / 4] = 0xFFFFFFFFu;
    t[MIME_TEB32_STACK_BASE / 4] = (ULONG)((ULONG_PTR)stack + stack_size);
    t[MIME_TEB32_STACK_LIMIT / 4] = (ULONG)(ULONG_PTR)stack_limit;
    t[MIME_TEB32_SELF / 4] = (ULONG)(ULONG_PTR)teb32;
    t[MIME_TEB32_CLIENT_ID / 4] = (ULONG)(ULONG_PTR)thread->Cid.UniqueProcess;
    t[MIME_TEB32_CLIENT_ID / 4 + 1] = (ULONG)(ULONG_PTR)thread->Cid.UniqueThread;
    t[MIME_TEB32_REAL_CLIENT_ID / 4] = (ULONG)(ULONG_PTR)thread->Cid.UniqueProcess;
    t[MIME_TEB32_REAL_CLIENT_ID / 4 + 1] = (ULONG)(ULONG_PTR)thread->Cid.UniqueThread;
    t[MIME_TEB32_PEB / 4] = (ULONG)(ULONG_PTR)process->Wow64Peb;
    t[MIME_TEB32_LOCALE / 4] = HalBootInfo->locale_lcid ? HalBootInfo->locale_lcid : 0x0409;
    t[MIME_TEB32_DEALLOCATION_STACK / 4] = (ULONG)(ULONG_PTR)stack;
    t[MIME_TEB32_WOW_TEB_OFFSET / 4] = (ULONG)-MIME_TEB_SIZE;
    MIME_STRING32 *string = (MIME_STRING32 *)(teb32 + MIME_TEB32_STATIC_STRING);
    string->MaximumLength = 522;
    string->Buffer = (ULONG)(ULONG_PTR)teb32 + MIME_TEB32_STATIC_BUFFER;
    t[MIME_TEB32_TLS_LINKS / 4] = t[MIME_TEB32_TLS_LINKS / 4 + 1] = (ULONG)(ULONG_PTR)teb32 + MIME_TEB32_TLS_LINKS;
    thread->Tcb.Teb32 = teb32;
}

extern FAST_MUTEX PspSharedSlotLock;

/*
 * the slot of 64 bytes each thread has in a page of zeros mapped once into its process, which the teb names in
 * SchedulerSharedDataSlot on 26300. called in the address space of the process.
 */
NTSTATUS PspAssignSharedSlot(PEPROCESS process, PETHREAD thread)
{
    NTSTATUS status = STATUS_SUCCESS;
    if (thread->SharedSlot)
        return status;
    ExAcquireFastMutex(&PspSharedSlotLock);
    if (!process->ThreadSharedPage) {
        PSECTION section;
        status = MiCreateDataSection(nullptr, PAGE_SIZE, PAGE_READWRITE, SEC_COMMIT, &section);
        if (NT_SUCCESS(status)) {
            PVOID base = nullptr;
            SIZE_T view = PAGE_SIZE;
            status = MiMapViewOfSection(section, process, &base, 0, &view, PAGE_READWRITE, 0);
            ObDereferenceObject(section);
            if (NT_SUCCESS(status))
                process->ThreadSharedPage = base;
        }
    }
    if (process->ThreadSharedPage && process->ThreadSharedSlots < PAGE_SIZE / 64)
        thread->SharedSlot = (PUCHAR)process->ThreadSharedPage + 64 * process->ThreadSharedSlots++;
    else if (NT_SUCCESS(status))
        status = STATUS_INSUFFICIENT_RESOURCES;
    ExReleaseFastMutex(&PspSharedSlotLock);
    return status;
}

#define THREAD_CREATE_FLAGS_CREATE_SUSPENDED 0x00000001
#define THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH 0x00000002
#define THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER 0x00000004
#define THREAD_CREATE_FLAGS_LOADER_WORKER 0x00000010
#define THREAD_CREATE_FLAGS_SKIP_LOADER_INIT 0x00000020

/* allocates the user stack and teb, then creates the kernel half of the thread. flags are NtCreateThreadEx's. */
NTSTATUS PspCreateUserThread(PEPROCESS process, PVOID start, PVOID parameter, ULONG flags,
                             SIZE_T stack_reserve, SIZE_T stack_commit, PETHREAD *out)
{
    PPSP_SYSTEM_DLL dll = process->SystemDll;
    if (!dll)
        return STATUS_DLL_NOT_FOUND;
    PVOID user_start = dll->RtlUserThreadStart;
    BOOLEAN wow64 = PspIsWow64Process(process);
    /* a wow64 thread runs 64-bit code on a stack of its own too, like the translator. */
    BOOLEAN translated = process->Machine != MIME_NATIVE_MACHINE;
    /*
     * like RtlCreateUserStack: a size left zero is the image's, a commit of zero is 16 KB, a commit as large as the
     * reservation makes the reservation that commit rounded to a megabyte, and the reservation is 64 KB aligned. a
     * game whose image asks for 4 MB stacks gets them for every thread made without a size.
     */
    if (!stack_commit || !stack_reserve) {
        PSECTION image = process->ImageSection;
        SIZE_T image_reserve = image ? image->Image.StackReserve : MIME_DEFAULT_STACK_RESERVE;
        SIZE_T image_commit = image ? image->Image.StackCommit : 0;
        if (!stack_commit)
            stack_commit = image_commit;
        if (!stack_reserve)
            stack_reserve = image_reserve;
    }
    if (!stack_commit)
        stack_commit = 0x4000;
    if (stack_commit >= stack_reserve)
        stack_reserve = (stack_commit + 0xFFFFF) & ~(SIZE_T)0xFFFFF;
    stack_reserve = (stack_reserve + 0xFFFF) & ~(SIZE_T)0xFFFF;

    BOOLEAN first = IsListEmpty(&process->ThreadListHead);
    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    KiAttachProcess(&process->Pcb);

    PVOID stack = nullptr, teb_va = nullptr;
    SIZE_T size = stack_reserve;
    NTSTATUS status = process->Machine == IMAGE_FILE_MACHINE_I386
                          ? MiAllocateVirtual(process, &stack, &size, MEM_RESERVE, PAGE_READWRITE)
                          : MiAllocateThreadMemory(process, &stack, &size, MEM_RESERVE, PAGE_READWRITE);
    /*
     * the 32-bit teb that follows a wow64 thread's must be reachable from 32-bit code. every other teb sits with the
     * peb, like on windows, see MiAllocatePebOrTeb.
     */
    SIZE_T teb_size = MIME_TEB_SIZE + (wow64 ? MIME_TEB32_SIZE : 0);
    if (NT_SUCCESS(status)) {
        if (wow64)
            status = MiAllocateVirtual(process, &teb_va, &teb_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        else if (process->Machine == IMAGE_FILE_MACHINE_I386)
            status = MiAllocateThreadMemory(process, &teb_va, &teb_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        else
            status = MiAllocatePebOrTeb(process, teb_size, FALSE, &teb_va);
    }
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
    /*
     * like RtlCreateUserStack and MiCheckForUserStackOverflow: the commit is at the top with a guard page below it,
     * and touching the guard commits it and moves the guard one page down, see MiGrowUserStack. the bottom of the
     * reservation stays reserved, as a walk of a windows stack shows it: a guard that would reach it is a stack
     * overflow, and the bytes below are committed then, for the exception to be dispatched on. the stack limit is
     * the committed top.
     */
    SIZE_T zone = stack_reserve >= 4 * MIME_STACK_OVERFLOW_ZONE ? MIME_STACK_OVERFLOW_ZONE : ROUND_TO_PAGES(stack_reserve / 4);
    ULONG_PTR stack_top = (ULONG_PTR)stack + stack_reserve, last_guard = (ULONG_PTR)stack + zone;
    SIZE_T commit = ROUND_TO_PAGES(stack_commit);
    if (commit > stack_top - last_guard - PAGE_SIZE)
        commit = stack_top - last_guard - PAGE_SIZE;
    ULONG_PTR committed = stack_top - commit;
    struct {
        ULONG_PTR Start;
        SIZE_T Size;
        ULONG Protect;
    } parts[] = {
        {committed, commit, PAGE_READWRITE},
        {committed - MI_STACK_GUARD_SIZE, committed - MI_STACK_GUARD_SIZE > last_guard ? MI_STACK_GUARD_SIZE : 0,
         PAGE_READWRITE | PAGE_GUARD},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(parts) && NT_SUCCESS(status); i++) {
        PVOID at = (PVOID)parts[i].Start;
        SIZE_T part = parts[i].Size;
        if (part)
            status = MiAllocateVirtual(process, &at, &part, MEM_COMMIT, parts[i].Protect);
    }
    if (!NT_SUCCESS(status)) {
        KiAttachProcess(saved);
        return status;
    }
    thread->UserStackGuard = last_guard;
    PVOID stack_limit = (PVOID)committed;
    thread->NativeStackAllocation = native_stack;
    thread->NativeStackSize = native_stack ? native_size : 0;
    thread->Win32StartAddress = start;
    thread->Tcb.Teb = teb_va;

    PTEB teb = teb_va;
    ULONG_PTR stack_base = (ULONG_PTR)stack + stack_reserve;
    teb->NtTib.StackBase = (PVOID)stack_base;
    teb->NtTib.StackLimit = stack_limit;
    teb->NtTib.Self = &teb->NtTib;
    teb->DeallocationStack = stack;
    teb->ClientId = thread->Cid;
    teb->RealClientId = thread->Cid;
    teb->ProcessEnvironmentBlock = process->Peb;
    teb->CurrentLocale = HalBootInfo->locale_lcid ? HalBootInfo->locale_lcid : 0x0409;
    teb->StaticUnicodeString.MaximumLength = sizeof(teb->StaticUnicodeBuffer);
    teb->StaticUnicodeString.Buffer = teb->StaticUnicodeBuffer;
    /* a 64-bit kernelbase leaves TlsLinks empty, like windows. a 32-bit one links the tebs through it. */
    if (process->Machine == IMAGE_FILE_MACHINE_I386)
        InitializeListHead(&teb->TlsLinks);
    /* like MmCreateTeb on 26300: the OS/2 version of the tib, no transaction context, and a state for RtlRandom. */
    teb->NtTib.FiberData = (PVOID)0x1e00;
    teb->TxFsContext = 0xfffe;
    teb->RngState[0] = MiRandom();
    teb->RngState[1] = MiRandom();
    teb->HeapData = (ULONG)MiRandom() & 0xffff0000;
    if (process->Machine != IMAGE_FILE_MACHINE_I386 && NT_SUCCESS(PspAssignSharedSlot(process, thread)))
        teb->SchedulerSharedDataSlot = thread->SharedSlot;
    *(PULONG64)teb->PrimaryGroupAffinity = KiProcessAffinity(&process->Pcb);
    /*
     * like PspAllocateThread, the creation flags the loader reads become teb flags: SkipThreadAttach, LoaderWorker and
     * SkipLoaderInit. a thread that skips the loader's initialization never takes the loader lock, so its creator may
     * suspend it at once.
     */
    USHORT same = (flags & THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH ? 0x0008 : 0) |
                  (flags & THREAD_CREATE_FLAGS_LOADER_WORKER ? 0x2000 : 0) |
                  (flags & THREAD_CREATE_FLAGS_SKIP_LOADER_INIT ? 0x4000 : 0) |
                  (first ? 0x0400 : 0); /* InitialThread. */
    teb->SameTebFlags = same;
    if (flags & THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER)
        thread->HideFromDebugger = TRUE;

    /*
     * initial context consumed by LdrInitializeThunk, then NtContinue'd into RtlUserThreadStart. windows 11 starts a
     * thread 0x28 bytes under the top of its stack and a random number of 16 byte steps more, 0 to 127, measured
     * with scratch/dlls/crash3/rsp2.c of the research repository. the context goes under that.
     */
    ULONG64 start_sp = stack_base - 0x20 - ((ULONG64)(MiRandom() & 127) << 4);
    ULONG64 ctx_va = (start_sp - sizeof(CONTEXT)) & ~15ull;
    ULONG64 sp = native_stack ? (ULONG64)native_stack + native_size : ctx_va;
    if (wow64) {
        /* the 64-bit teb describes the 64-bit stack, which holds the cpu area and the 64-bit start context. */
        ULONG_PTR native_base;
        setup_wow64_teb(process, thread, teb, stack, stack_limit, stack_reserve, (ULONG_PTR)native_stack + native_size,
                        &native_base, start, parameter);
        *(PUSHORT)((PUCHAR)teb + MIME_TEB_SIZE + 0xFCA) = same;
        teb->NtTib.StackBase = (PVOID)native_base;
        teb->NtTib.StackLimit = native_stack;
        teb->DeallocationStack = native_stack;
        ctx_va = (native_base - sizeof(CONTEXT)) & ~15ull;
        sp = start_sp = ctx_va;
    }
    /*
     * as on windows the trap frame holds the RtlUserThreadStart state until the thread runs, so the context another
     * thread reads or changes meanwhile is that one. the first entry to user mode saves it for LdrInitializeThunk.
     */
    KiInitializeUserFrame(thread->Tcb.TrapFrame, (ULONG64)user_start, start_sp, (ULONG64)start, (ULONG64)parameter,
                          (ULONG64)teb_va);
    thread->FirstEntryContext = ctx_va;
    thread->FirstEntryStack = sp;
    KiAttachProcess(saved);

    if (!(flags & THREAD_CREATE_FLAGS_CREATE_SUSPENDED))
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

/* a list link in the program: 8 bytes, or 4 for wow64. */
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
 * kernelbase links the tebs of a process through TlsLinks. a terminated thread never unlinks itself, and the next
 * thread to join would write through its teb, freed below.
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
 * the program cannot free the stacks and teb it runs on, so the kernel does, from the thread's own process while
 * it is still whole.
 */
static void release_user_memory(PEPROCESS process, PETHREAD thread)
{
    if (thread->Tcb.Teb) {
        unlink_tls_links(&((PTEB)thread->Tcb.Teb)->TlsLinks, sizeof(ULONG64));
        if (thread->Tcb.Teb32)
            unlink_tls_links((PUCHAR)thread->Tcb.Teb32 + MIME_TEB32_TLS_LINKS, sizeof(ULONG));
    }
    /* a teb among the others gives its pages back, one that is an allocation of its own goes whole. */
    PVOID teb = thread->Tcb.Teb;
    if (teb && MiFreeTeb(process, teb, MIME_TEB_SIZE))
        teb = nullptr;
    PVOID regions[] = {thread->UserStackAllocation, thread->NativeStackAllocation, teb};
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
    PspNotifyThread(thread, FALSE);
    if (thread->Tcb.Win32Thread)
        PsWin32ThreadCallout(thread, FALSE);
    CmNotifyRunDown(thread);

    /* abandon owned mutants. */
    while (!IsListEmpty(&thread->Tcb.MutantListHead)) {
        PLIST_ENTRY first = thread->Tcb.MutantListHead.Flink;
        PKMUTANT m = CONTAINING_RECORD(first, KMUTANT, MutantListEntry);
        KeReleaseMutant(m, 0, TRUE, FALSE);
        /* an entry the release did not take away would keep the thread from ever ending. */
        if (!IsListEmpty(&thread->Tcb.MutantListHead) && thread->Tcb.MutantListHead.Flink == first)
            RemoveEntryList(first);
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


NTSTATUS NTAPI NtCreateThreadEx(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, HANDLE process_handle,
                                PVOID start, PVOID parameter, ULONG flags, SIZE_T zero_bits, SIZE_T stack_size,
                                SIZE_T max_stack_size, PVOID attribute_list)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_CREATE_THREAD, &process);
    if (!NT_SUCCESS(status))
        return status;
    /* the descriptor and the attribute list are worked out first, so a refused one starts no thread. */
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    SD_BLOB *sd = nullptr;
    PVOID list = nullptr;
    status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (NT_SUCCESS(status))
        status = ObComputeSecurity(PsThreadTypeObject, attributes ? &oa : nullptr, nullptr, KeGetPreviousMode(), &sd);
    KiFreeCapturedString(&name);
    if (NT_SUCCESS(status))
        status = PspCaptureThreadAttributes(attribute_list, &list);
    if (!NT_SUCCESS(status)) {
        if (sd)
            SeSdDereference(sd);
        ObDereferenceObject(process);
        return status;
    }
    PETHREAD thread;
    status = PspCreateUserThread(process, start, parameter, flags,
                                 max_stack_size, stack_size, &thread);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(process);
        PspFreeThreadAttributes(list);
        if (sd)
            SeSdDereference(sd);
        return status;
    }
    ObAttachSecurity(thread, sd);
    PspReturnThreadAttributes(list, process, thread);
    ObDereferenceObject(process);
    PspFreeThreadAttributes(list);
    HANDLE h;
    status = ObInsertObject(thread, access ? access : THREAD_ALL_ACCESS, 0, &h);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    return status;
}

NTSTATUS NTAPI NtResumeThread(HANDLE handle, PULONG previous)
{
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_RESUME, &thread);
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

/* PsSuspendThread for the kernel's own use, which a PspResumeThread undoes: the thread stops before user mode. */
NTSTATUS PspSuspendThread(PETHREAD thread)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG64 state = KiAcquireDispatcherLock();
    ULONG old = thread->Tcb.SuspendCount;
    if (thread->Tcb.Terminating) {
        status = STATUS_THREAD_IS_TERMINATING;
    } else if (old == 127) {
        status = (NTSTATUS)0xC000004AL;
    } else {
        thread->Tcb.SuspendCount++;
        if (!old)
            KeClearEvent(&thread->Tcb.SuspendEvent);
        PspCheckForSuspendApc(&thread->Tcb);
    }
    KiReleaseDispatcherLock(state);
    return status;
}

void PspResumeThread(PETHREAD thread)
{
    ULONG64 state = KiAcquireDispatcherLock();
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
    KiReleaseDispatcherLock(state);
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

/* suspends or resumes every thread of a process once more, as debuggers do. */
/* every thread of the process, the caller holding a reference to it. */
NTSTATUS PspSuspendResumeProcess(PEPROCESS process, BOOLEAN suspend)
{
    if (process->SystemProcess)
        return STATUS_ACCESS_DENIED;
    if (process->Terminating)
        return STATUS_PROCESS_IS_TERMINATING;
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
    return STATUS_SUCCESS;
}

static NTSTATUS suspend_or_resume_process(HANDLE handle, BOOLEAN suspend)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(handle, PROCESS_SUSPEND_RESUME, &process);
    if (!NT_SUCCESS(status))
        return status;
    status = PspSuspendResumeProcess(process, suspend);
    ObDereferenceObject(process);
    return status;
}

NTSTATUS NTAPI PsSuspendProcess(PEPROCESS process)
{
    return PspSuspendResumeProcess(process, TRUE);
}

NTSTATUS NTAPI PsResumeProcess(PEPROCESS process)
{
    return PspSuspendResumeProcess(process, FALSE);
}

NTSTATUS NTAPI NtSuspendProcess(HANDLE handle)
{
    return suspend_or_resume_process(handle, TRUE);
}

NTSTATUS NTAPI NtResumeProcess(HANDLE handle)
{
    return suspend_or_resume_process(handle, FALSE);
}

static NTSTATUS query_thread(HANDLE handle, THREADINFOCLASS klass, PVOID info, ULONG length, PULONG returned);

/* like NtQueryInformationProcess: the probes, the length windows takes for the class, then the thread. */
NTSTATUS NTAPI NtQueryInformationThread(HANDLE handle, THREADINFOCLASS klass, PVOID info, ULONG length,
                                        PULONG returned)
{
    /* windows asks no range of this buffer up front. */
    ULONG alignment = (ULONG)klass == 38 ? 8 : (ULONG)klass == 49 || length >= 4 ? 4 : 1;
    NTSTATUS status = KiProbeQueryBuffer(info, length, alignment, FALSE);
    if (NT_SUCCESS(status))
        status = KiProbeReturnLength(returned);
    if (!NT_SUCCESS(status))
        return status;
    if ((ULONG)klass == 29) { /* ThreadWow64Context */
        PETHREAD thread;
        status = PsReferenceThreadHandle(handle, THREAD_GET_CONTEXT, &thread);
        if (!NT_SUCCESS(status))
            return status;
        status = PspWow64GetContextThread(thread, info, length);
        ObDereferenceObject(thread);
        ULONG size = 0x2CC;
        if (NT_SUCCESS(status) && returned)
            KiCopyToUser(returned, &size, sizeof(size));
        return status;
    }
    PS_CLASS_ANSWER reference;
    if (KeGetPreviousMode() != UserMode ||
        !PspClassAnswer(PspThreadClassRuns, RTL_NUMBER_OF(PspThreadClassRuns), (ULONG)klass, length, &reference))
        return query_thread(handle, klass, info, length, returned);
    if (reference.Decided) {
        if (returned && reference.Returned != PS_CLASS_NO_LENGTH)
            KiCopyToUser(returned, &reference.Returned, sizeof(ULONG));
        return reference.Status;
    }
    ULONG before = 0;
    BOOLEAN fixed = returned && !reference.Variable;
    if (fixed)
        KiCopyFromUser(&before, returned, sizeof(before));
    status = query_thread(handle, klass, info, length, returned);
    if (status == STATUS_INVALID_INFO_CLASS)
        return PspClassDefault(&reference, info, length, returned);
    if (fixed && NT_SUCCESS(status))
        KiCopyToUser(returned, reference.Returned != PS_CLASS_NO_LENGTH ? &reference.Returned : &before, sizeof(ULONG));
    return status;
}

FAST_MUTEX PspSharedSlotLock;

static NTSTATUS query_thread(HANDLE handle, THREADINFOCLASS klass, PVOID info, ULONG length, PULONG returned)
{
    NTSTATUS status;
    /* what anyone who can see a thread may know, with the full or the limited right. */
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
    /* ThreadSuspendCount: like windows, an exact length is checked before the handle. */
    if ((ULONG)klass == 35 && length != sizeof(ULONG))
        return STATUS_INFO_LENGTH_MISMATCH;
    PETHREAD thread;
    status = PsReferenceThreadHandle(handle, rights, &thread);
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
    switch ((ULONG)klass) {
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
        /* windows charges whole clock ticks. */
        out.times[2] = thread->Tcb.KernelTime - thread->Tcb.KernelTime % KI_CLOCK_TICK_HNS;
        out.times[3] = thread->Tcb.UserTime - thread->Tcb.UserTime % KI_CLOCK_TICK_HNS;
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
    case 22: /* ThreadIoPriority: normal. */
        out.u32 = 2;
        size = sizeof(ULONG);
        break;
    case 24: /* ThreadPagePriority: normal. */
        out.u32 = 5;
        size = sizeof(ULONG);
        break;
    case 25: /* ThreadActualBasePriority */
        out.u32 = (ULONG)thread->Tcb.BasePriority;
        size = sizeof(ULONG);
        break;
    case 38: { /* ThreadNameInformation: no name, a string of no length whose text would follow it. */
        ULONG lengths = 0, need = sizeof(UNICODE_STRING);
        PVOID text = (PUCHAR)info + sizeof(UNICODE_STRING);
        ObDereferenceObject(thread);
        if (returned)
            KiCopyToUser(returned, &need, sizeof(need));
        if (length < need)
            return STATUS_BUFFER_TOO_SMALL;
        status = KiCopyToUser(info, &lengths, sizeof(lengths));
        return NT_SUCCESS(status) ? KiCopyToUser((PUCHAR)info + 8, &text, sizeof(text)) : status;
    }
    case 54: /* the io and page priorities in effect: normal. */
    case 55:
        out.u32 = (ULONG)klass == 54 ? 2 : 5;
        size = sizeof(ULONG);
        break;
    case 36: /* ThreadHeterogeneousCpuPolicy: dynamic. */
        out.u32 = 5;
        size = sizeof(ULONG);
        break;
    case 59: /* ThreadIndexInformation: 1 and 1 for the first thread on windows 11 26300. */
        out.times[0] = 0x100000001ull;
        size = sizeof(ULONG64);
        break;
    case 35: /* ThreadSuspendCount */
        out.u32 = thread->Tcb.SuspendCount;
        size = sizeof(ULONG);
        break;
    case ThreadAmILastThread: {
        PEPROCESS process = (PEPROCESS)KiCurrentThread()->Process;
        out.u32 = process->ThreadCount <= 1;
        size = sizeof(ULONG);
        break;
    }
    case 57: {
        /* measured on windows 11 26300: a thread asks for itself only, and gets a slot of 64 bytes in a page of
           zeros mapped once into its process, the same slot each time. */
        if (thread != PsCurrentThread()) {
            status = STATUS_NOT_SUPPORTED;
            break;
        }
        status = PspAssignSharedSlot(PsCurrentProcess(), thread);
        out.ptr = thread->SharedSlot;
        size = sizeof(PVOID);
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

#define TLS_MINIMUM_AVAILABLE 64
#define TLS_EXPANSION_SLOTS 1024

/* ThreadZeroTlsCell: TlsFree clears the slot it frees in the teb of every thread of the process. */
static NTSTATUS zero_tls_cell(PEPROCESS process, ULONG index)
{
    if (index >= TLS_MINIMUM_AVAILABLE + TLS_EXPANSION_SLOTS)
        return STATUS_INVALID_PARAMETER;
    if (process->Machine == IMAGE_FILE_MACHINE_I386)
        return STATUS_SUCCESS;
    ULONG capacity = (ULONG)process->ThreadCount + 16, count = 0;
    PVOID *tebs = ExAllocatePool2(POOL_FLAG_NON_PAGED, capacity * sizeof(PVOID), KI_TAG('P', 's', 'T', 'z'));
    if (!tebs)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG64 state = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = process->ThreadListHead.Flink; e != &process->ThreadListHead && count < capacity; e = e->Flink) {
        PETHREAD thread = CONTAINING_RECORD(e, ETHREAD, ThreadListEntry);
        if (thread->Tcb.Teb && !thread->Tcb.Terminating)
            tebs[count++] = thread->Tcb.Teb;
    }
    KiReleaseDispatcherLock(state);
    PVOID zero = nullptr;
    for (ULONG i = 0; i < count; i++) {
        PTEB teb = tebs[i];
        if (index < TLS_MINIMUM_AVAILABLE) {
            KiCopyToUser(&teb->TlsSlots[index], &zero, sizeof(zero));
            continue;
        }
        PVOID *expansion = nullptr;
        if (NT_SUCCESS(KiCopyFromUser(&expansion, &teb->TlsExpansionSlots, sizeof(expansion))) && expansion)
            KiCopyToUser(&expansion[index - TLS_MINIMUM_AVAILABLE], &zero, sizeof(zero));
    }
    ExFreePool(tebs);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtSetInformationThread(HANDLE handle, THREADINFOCLASS klass, PVOID info, ULONG length)
{
    if (klass == ThreadImpersonationToken)
        return SepSetThreadToken(handle, info, length);
    if ((ULONG)klass == 29) { /* ThreadWow64Context */
        PETHREAD thread;
        NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_SET_CONTEXT, &thread);
        if (!NT_SUCCESS(status))
            return status;
        status = PspWow64SetContextThread(thread, info, length);
        ObDereferenceObject(thread);
        return status;
    }
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
    case ThreadEnableAlignmentFaultFixup:
        want = sizeof(BOOLEAN);
        break;
    case ThreadZeroTlsCell:
        want = sizeof(ULONG);
        break;
    default:
        break;
    }
    if (klass != ThreadIdealProcessor && klass != ThreadIdealProcessorEx && klass != ThreadEnableAlignmentFaultFixup && want)
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
    } else if (klass == ThreadZeroTlsCell) {
        PEPROCESS process = CONTAINING_RECORD(thread->Tcb.Process, EPROCESS, Pcb);
        status = process == PsCurrentProcess() ? zero_tls_cell(process, (ULONG)value) : STATUS_ACCESS_DENIED;
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
    } else if (klass == ThreadEnableAlignmentFaultFixup) {
        thread->Tcb.AutoAlignment = (UCHAR)value != 0;
    } else if (klass == ThreadAffinityMask) {
        PEPROCESS process = (PEPROCESS)thread->Tcb.Process;
        /* like KiSetLegacyAffinityThread, processors that do not exist are dropped from the mask before it is checked. */
        value &= KiAllProcessors();
        if (!value || (value & ~KiProcessAffinity(&process->Pcb)))
            status = STATUS_INVALID_PARAMETER;
        else
            KiSetAffinityThread(&thread->Tcb, value);
    } else if (klass == ThreadIdealProcessor) {
        /* the previous ideal processor is returned as the status. a processor that does not exist changes nothing. */
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
 * thread parking, as on windows 8 and later, which ntdll builds address waits, srw locks and condition variables
 * on. an alert sent before the wait is kept, so waking never races with going to sleep.
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

/*
 * the locks of the ntdll of windows 11 wake with this one and name the lock. measured on windows 11 26300 with
 * scratch/dlls/probe/alertex.c of the research repository: the pointer is never read, not even a bad one, and
 * the rest is NtAlertThreadByThreadId.
 */
NTSTATUS NTAPI NtAlertThreadByThreadIdEx(HANDLE id, PVOID lock)
{
    return NtAlertThreadByThreadId(id);
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

#define QUEUE_USER_APC_FLAGS_SPECIAL_USER_APC 0x1

/*
 * QueueUserAPC of the kernelbase of windows 11 comes here, with no reserve object and no flags. mime has no reserve
 * objects, so any handle in their place is not one, and no special user apcs, which run without an alertable wait.
 */
NTSTATUS NTAPI NtQueueApcThreadEx2(HANDLE handle, HANDLE reserve, ULONG flags, PPS_APC_ROUTINE routine, PVOID arg1,
                                   PVOID arg2, PVOID arg3)
{
    if (flags & ~QUEUE_USER_APC_FLAGS_SPECIAL_USER_APC)
        return STATUS_INVALID_PARAMETER;
    if (reserve)
        return STATUS_INVALID_HANDLE;
    if (flags)
        return STATUS_NOT_SUPPORTED;
    return NtQueueApcThread(handle, routine, arg1, arg2, arg3);
}

/* the second argument is a reserve object, or 1 for a special user apc. */
NTSTATUS NTAPI NtQueueApcThreadEx(HANDLE handle, HANDLE option, PPS_APC_ROUTINE routine, PVOID arg1, PVOID arg2,
                                  PVOID arg3)
{
    if ((ULONG_PTR)option == QUEUE_USER_APC_FLAGS_SPECIAL_USER_APC)
        return NtQueueApcThreadEx2(handle, nullptr, QUEUE_USER_APC_FLAGS_SPECIAL_USER_APC, routine, arg1, arg2, arg3);
    return NtQueueApcThreadEx2(handle, option, 0, routine, arg1, arg2, arg3);
}

typedef struct KCONTINUE_ARGUMENT {
    ULONG ContinueType; /* unwind, resume, long jump or set, which only the shadow stack tells apart. */
    ULONG ContinueFlags;
    ULONG64 Reserved[2];
} KCONTINUE_ARGUMENT;

#define KCONTINUE_LAST 4
#define KCONTINUE_FLAG_TEST_ALERT 0x1
#define KCONTINUE_FLAG_DELIVER_APC 0x2

/*
 * the argument is NtContinue's alert flag, or a KCONTINUE_ARGUMENT. KiUserApcDispatcher passes the one the kernel
 * left in its frame and starts over when this returns success with the next apc in that frame. here the next apc
 * gets a frame of its own from the same stack pointer as the thread returns to user mode, which comes to the same.
 */
NTSTATUS NTAPI NtContinueEx(PCONTEXT context, PVOID argument)
{
    if ((ULONG_PTR)argument < 0x10000)
        return NtContinue(context, (BOOLEAN)(ULONG_PTR)argument);
    KCONTINUE_ARGUMENT arg;
    if (!NT_SUCCESS(KiCopyFromUser(&arg, argument, sizeof(arg))))
        return STATUS_ACCESS_VIOLATION;
    if (arg.ContinueType >= KCONTINUE_LAST || (arg.ContinueFlags & ~(KCONTINUE_FLAG_TEST_ALERT | KCONTINUE_FLAG_DELIVER_APC)))
        return STATUS_INVALID_PARAMETER;
    return NtContinue(context, (arg.ContinueFlags & (KCONTINUE_FLAG_TEST_ALERT | KCONTINUE_FLAG_DELIVER_APC)) != 0);
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

/*
 * logs every thread of every process, for a hung machine: its state, what it waits for, where its user code entered
 * the kernel and the return address on top of the user stack there. passive level, since it attaches to each
 * process to read its stack.
 */
void PsDumpThreads(void)
{
    static const char *const states[] = {"initialized", "ready", "running", "standby", "terminated", "waiting"};
    enum { MAX = 512 };
    static PETHREAD threads[MAX];
    static PVOID satisfied[MAX];
    ULONG n = 0;
    ULONG64 s = KiAcquireDispatcherLock();
    for (PLIST_ENTRY p = PsActiveProcessHead.Flink; p != &PsActiveProcessHead; p = p->Flink) {
        PEPROCESS process = CONTAINING_RECORD(p, EPROCESS, ActiveProcessLinks);
        for (PLIST_ENTRY t = process->ThreadListHead.Flink; t != &process->ThreadListHead && n < MAX; t = t->Flink) {
            threads[n] = CONTAINING_RECORD(t, ETHREAD, ThreadListEntry);
            satisfied[n] = KiSatisfiedWaitObject(&threads[n]->Tcb);
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
        /* a wait that ends by itself says when, which tells a thread stuck for good from one that sleeps. */
        char due[32] = "";
        if (k->State == Waiting && k->Timer.TimerListEntry.Flink) {
            LONG64 left = (LONG64)(k->Timer.DueTime.QuadPart - KeQueryInterruptTime()) / 10000;
            _snprintf(due, sizeof(due), " timeout in %lld ms", (long long)left);
        }
        KiLogPrintf("%s %u thread %u %s reason %u object %p type %d pc %p sp %p return %p%s", process->ImageFileName,
                    (ULONG)(ULONG_PTR)process->UniqueProcessId, (ULONG)(ULONG_PTR)threads[i]->Cid.UniqueThread, state,
                    k->WaitReason, object, object ? ((DISPATCHER_HEADER *)object)->Type : -1, (PVOID)pc, (PVOID)sp,
                    (PVOID)top, due);
        if (chain[0])
            KiLogPrintf("  calls%s", chain);
        if (satisfied[i])
            KiLogPrintf("  lost wake: thread %u sleeps although object %p is signaled", (ULONG)(ULONG_PTR)threads[i]->Cid.UniqueThread,
                        satisfied[i]);
        ObDereferenceObject(threads[i]);
    }
    /* the root table of each process, which the host's profiler records with every sample, and who holds its loader lock. */
    for (PLIST_ENTRY p = PsActiveProcessHead.Flink; p != &PsActiveProcessHead; p = p->Flink) {
        PEPROCESS process = CONTAINING_RECORD(p, EPROCESS, ActiveProcessLinks);
        if (!process->SystemProcess && process->Peb && !process->Terminating && !IsListEmpty(&process->ThreadListHead)) {
            PKPROCESS saved;
            /* the critical section's LockCount, RecursionCount and OwningThread. */
            struct { PVOID DebugInfo; LONG LockCount, RecursionCount; HANDLE OwningThread; } lock = {0};
            PVOID at = nullptr;
            KeStackAttachProcess(&process->Pcb, &saved);
            if (NT_SUCCESS(KiCopyFromUser(&at, &process->Peb->LoaderLock, sizeof(at))) && at &&
                NT_SUCCESS(KiCopyFromUser(&lock, at, sizeof(lock))) && lock.OwningThread)
                KiLogPrintf("process %s %u loader lock owner %u count %d recursion %d", process->ImageFileName,
                            (ULONG)(ULONG_PTR)process->UniqueProcessId, (ULONG)(ULONG_PTR)lock.OwningThread, lock.LockCount,
                            lock.RecursionCount);
            KeUnstackDetachProcess(&saved);
        }
        KiLogPrintf("process %s %u table %llx", process->ImageFileName, (ULONG)(ULONG_PTR)process->UniqueProcessId,
                    (unsigned long long)process->Pcb.DirectoryTableBase);
    }
    NpfsDumpState();
}
