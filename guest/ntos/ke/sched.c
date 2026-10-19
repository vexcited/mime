/*
 * thread scheduler: ready queues, dispatching and dpcs.
 */
#include "../ntos.h"

#define KI_DEFAULT_QUANTUM_HNS 300000 /* 30 ms, desktop style quanta */

LIST_ENTRY KiReadyListHead[MAXIMUM_PRIORITY];
ULONG KiReadySummary;
PKPRCB KiProcessorBlock[MAXIMUM_PROCESSORS];
ULONG KeNumberProcessors = 1;
volatile ULONG KiKernelLock[8] __attribute__((aligned(64))) = {0, ~0u, 0, 0};
/* a hold of the lock longer than this many ticks is counted by what held it, see KiNoteLongHold. */
ULONG64 KiLongHoldTicks = ~0ull;
ULONG64 KiKernelLockPhys;
ULONG KiOptions;
/* the kernel option nolockflush: the lock no longer settles the user translations other processors changed. */
UCHAR KiNoLockFlush;
/* acquisitions, those that had to wait, ticks waited, the longest wait: a row of eight for each processor, see entry.S */
ULONG64 KiLockStats[MAXIMUM_PROCESSORS][8];

ULONG64 HalHypercallUnlocked(ULONG64 nr, ULONG64 a1, ULONG64 a2, ULONG64 a3, ULONG64 a4, ULONG64 a5, ULONG64 a6,
                             PULONG64 value)
{
    /* interrupts stay off: an interrupt taken meanwhile would run without the lock. */
    ULONG64 state = KiDisableInterrupts();
    BOOLEAN held = KeGetPcr()->Irql < DISPATCH_LEVEL && KiKernelLock[1] == KeGetCurrentPrcb()->Number;
    if (!held) {
        KiRestoreInterrupts(state);
        return HalHypercall(nr, a1, a2, a3, a4, a5, a6, value);
    }
    ULONG64 start = HalReadCounter();
    KiReleaseKernelLock();
    ULONG64 status = HalHypercall(nr, a1, a2, a3, a4, a5, a6, value);
    KiAcquireKernelLock();
    /* the time away is not time the lock was held, which the statistics of the services want to know. */
    KiCurrentThread()->BlockedTicks += HalReadCounter() - start;
    KiRestoreInterrupts(state);
    return status;
}

void KiInitializeScheduler(void)
{
    PKPRCB prcb = KeGetCurrentPrcb();
    for (int i = 0; i < MAXIMUM_PRIORITY; i++)
        InitializeListHead(&KiReadyListHead[i]);
    KiReadySummary = 0;
    prcb->DpcHead.Next = nullptr;
    prcb->DpcTail = nullptr;
    KiProcessorBlock[0] = prcb;
}

/* the processors of the machine as an affinity mask. */
ULONG64 KiAllProcessors(void)
{
    return KeNumberProcessors >= 64 ? ~0ull : (1ull << (KeNumberProcessors ? KeNumberProcessors : 1)) - 1;
}

/* the processors a process is limited to, all of them when it was never limited. */
ULONG64 KiProcessAffinity(PKPROCESS process)
{
    return process->Affinity ? process->Affinity : KiAllProcessors();
}

void KiInitializeThread(PKTHREAD thread, PKPROCESS process, PVOID stack_top, PVOID stack_limit,
                        PKSTART_ROUTINE routine, PVOID context)
{
    thread->Header.Type = ThreadObject;
    thread->Header.Size = (UCHAR)(sizeof(KTHREAD) / 8);
    thread->Header.SignalState = 0;
    InitializeListHead(&thread->Header.WaitListHead);
    thread->InitialStack = stack_top;
    thread->StackLimit = stack_limit;
    thread->Process = process;
    thread->ApcStateProcess = process;
    InitializeListHead(&thread->MutantListHead);
    InitializeListHead(&thread->ApcListHead[KernelMode]);
    InitializeListHead(&thread->ApcListHead[UserMode]);
    KeInitializeTimer(&thread->Timer);
    KeInitializeSemaphore(&thread->SuspendSemaphore, 0, 2);
    KeInitializeEvent(&thread->SuspendEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&thread->AlertByIdEvent, SynchronizationEvent, FALSE);
    thread->Priority = process->BasePriority ? process->BasePriority : 8;
    thread->BasePriority = thread->Priority;
    thread->Affinity = KiProcessAffinity(process);
    thread->IdealProcessor = (UCHAR)((ULONG)process->ActiveThreads % (KeNumberProcessors ? KeNumberProcessors : 1));
    thread->Quantum = KI_DEFAULT_QUANTUM_HNS;
    thread->State = Initialized;
    thread->PreviousMode = KernelMode;

    /* the user trap frame (if any) sits at the top, the switch frame below it. */
    KiInitializeSwitchFrame(thread, routine, context);

    ULONG64 s = KiAcquireDispatcherLock();
    InsertTailList(&process->ThreadListHead, &thread->ThreadListEntry);
    process->ActiveThreads++;
    KiReleaseDispatcherLock(s);
}

/* first c code of every thread; interrupts are still masked by the switch. */
void KiThreadStartupC(PKSTART_ROUTINE routine, PVOID context)
{
    KeGetPcr()->Irql = PASSIVE_LEVEL;
    KiEnableInterrupts();
    routine(context);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* dispatcher lock held. */
void KiInsertReadyThread(PKTHREAD thread)
{
    int prio = thread->Priority;
    thread->State = Ready;
    InsertTailList(&KiReadyListHead[prio], &thread->ReadyListEntry);
    KiReadySummary |= 1u << prio;
    if (SharedUserData)
        SharedUserData->MimeReadyThreads = KiReadySummary;
}

/* asks a processor to pick its next thread, waking or interrupting it when it is another one. */
void KiRequestDispatch(ULONG number)
{
    PKPRCB prcb = KiProcessorBlock[number];
    prcb->DispatchPending = TRUE;
    if (prcb != KeGetCurrentPrcb()) {
        /* a processor that spins while idle sees the request by itself; one that stops spinning looks again. */
        KiMemoryBarrierFull();
        if (prcb->IdleState == KI_IDLE_POLLING)
            return;
        prcb->IpiSent++;
        HalSendIpi(number);
    }
}

/* makes a thread running on another processor enter the kernel, where it sees new apcs or termination. */
void KiInterruptThread(PKTHREAD thread)
{
    if (thread->State == Running && thread->Processor != KeGetCurrentPrcb()->Number)
        HalSendIpi(thread->Processor);
}

/*
 * the processor that should run a thread just made ready: an idle one,
 * this one or the one it last ran on first, else the one running the
 * lowest priority below the thread's. none leaves it queued.
 */
static PKPRCB pick_processor(PKTHREAD thread)
{
    PKPRCB self = KeGetCurrentPrcb(), idle = nullptr, low = nullptr;
    int low_prio = thread->Priority;
    for (ULONG i = 0; i < KeNumberProcessors; i++) {
        PKPRCB p = KiProcessorBlock[i];
        if (!p || p->DispatchPending)
            continue;
        PKTHREAD cur = p->CurrentThread;
        if (cur == p->IdleThread) {
            if (!idle || p == self || (i == thread->Processor && idle != self))
                idle = p;
        } else if (cur->Priority < low_prio || (cur->Priority == low_prio && low && p == self)) {
            low = p;
            low_prio = cur->Priority;
        }
    }
    return idle ? idle : low;
}

/* dispatcher lock held. */
void KiReadyThread(PKTHREAD thread)
{
    KI_ASSERT_KERNEL_LOCK();
    if (thread->IdleThread) {
        thread->State = Ready;
        return;
    }
    KiInsertReadyThread(thread);
    PKPRCB target = pick_processor(thread);
    if (target)
        KiRequestDispatch(target->Number);
}

/*
 * moves a thread to another priority. a queued thread changes queue and a
 * running one that dropped below a waiting thread gives its processor up.
 * dispatcher lock held.
 */
static void set_priority(PKTHREAD thread, int priority)
{
    int old = thread->Priority;
    if (old == priority)
        return;
    if (thread->State == Ready && !thread->IdleThread) {
        RemoveEntryList(&thread->ReadyListEntry);
        if (IsListEmpty(&KiReadyListHead[old])) {
            KiReadySummary &= ~(1u << old);
            if (SharedUserData)
                SharedUserData->MimeReadyThreads = KiReadySummary;
        }
        thread->Priority = (SCHAR)priority;
        KiReadyThread(thread);
        return;
    }
    thread->Priority = (SCHAR)priority;
    if (thread->State == Running && priority < old && (KiReadySummary >> priority) > 1)
        KiRequestDispatch(thread->Processor);
}

/*
 * the base priority of a thread of a process with a base priority, for an
 * increment that is a step or, at 16 and beyond, a limit. the dynamic
 * range is 1 to 15 and the real time one 16 to 31.
 */
static SCHAR base_for(SCHAR process_base, LONG increment, SCHAR *saturation)
{
    LONG magnitude = increment < 0 ? -increment : increment;
    *saturation = magnitude >= 16 ? (increment > 0 ? 1 : -1) : 0;
    LONG base = process_base + increment;
    if (process_base >= LOW_REALTIME_PRIORITY) {
        if (base < LOW_REALTIME_PRIORITY)
            base = LOW_REALTIME_PRIORITY;
        if (base > HIGH_PRIORITY)
            base = HIGH_PRIORITY;
    } else {
        if (base >= LOW_REALTIME_PRIORITY)
            base = LOW_REALTIME_PRIORITY - 1;
        if (base < 1)
            base = 1;
    }
    return (SCHAR)base;
}

/* the increment of a thread over its process, what GetThreadPriority reports. */
LONG KiQueryBasePriorityThread(PKTHREAD thread)
{
    PKPROCESS process = thread->Process;
    return thread->Saturation ? 16 * thread->Saturation : thread->BasePriority - process->BasePriority;
}

void KiSetBasePriorityThread(PKTHREAD thread, LONG increment)
{
    ULONG64 s = KiAcquireDispatcherLock();
    SCHAR base = base_for(thread->Process->BasePriority, increment, &thread->Saturation);
    thread->BasePriority = base;
    set_priority(thread, base);
    KiReleaseDispatcherLock(s);
}

/* an absolute priority, which also ends a saturation. */
void KiSetPriorityThread(PKTHREAD thread, int priority)
{
    ULONG64 s = KiAcquireDispatcherLock();
    thread->Saturation = 0;
    thread->BasePriority = (SCHAR)priority;
    set_priority(thread, priority);
    KiReleaseDispatcherLock(s);
}

/* a new base priority of a process, which its threads keep their increment over. */
void KiSetBasePriorityProcess(PKPROCESS process, int base)
{
    ULONG64 s = KiAcquireDispatcherLock();
    SCHAR old = process->BasePriority;
    process->BasePriority = (SCHAR)base;
    for (PLIST_ENTRY e = process->ThreadListHead.Flink; e != &process->ThreadListHead; e = e->Flink) {
        PKTHREAD thread = CONTAINING_RECORD(e, KTHREAD, ThreadListEntry);
        LONG increment = thread->Saturation ? 16 * thread->Saturation : thread->BasePriority - old;
        SCHAR saturation;
        thread->BasePriority = base_for((SCHAR)base, increment, &saturation);
        thread->Saturation = saturation;
        set_priority(thread, thread->BasePriority);
    }
    KiReleaseDispatcherLock(s);
}

void KiStartThread(PKTHREAD thread)
{
    ULONG64 s = KiAcquireDispatcherLock();
    KiReadyThread(thread);
    KiReleaseDispatcherLock(s);
}

static PKTHREAD select_next(PKPRCB prcb)
{
    if (!KiReadySummary)
        return prcb->IdleThread;
    int prio = 31 - __builtin_clz(KiReadySummary);
    PLIST_ENTRY e = RemoveHeadList(&KiReadyListHead[prio]);
    if (IsListEmpty(&KiReadyListHead[prio])) {
        KiReadySummary &= ~(1u << prio);
        /* user mode skips yields that would find no other thread to run. */
        if (SharedUserData)
            SharedUserData->MimeReadyThreads = KiReadySummary;
    }
    return CONTAINING_RECORD(e, KTHREAD, ReadyListEntry);
}

/*
 * switches to the best ready thread. the caller holds the dispatcher lock
 * and has already moved the current thread out of the running state.
 */
void KiSwapThread(void)
{
    KI_ASSERT_KERNEL_LOCK();
    PKPRCB prcb = KeGetCurrentPrcb();
    PKTHREAD old = prcb->CurrentThread;
    PKTHREAD next = select_next(prcb);
    prcb->DispatchPending = FALSE;
    if (next == old) {
        old->State = Running;
        return;
    }
    next->State = Running;
    next->Processor = (UCHAR)prcb->Number;
    next->Quantum = KI_DEFAULT_QUANTUM_HNS;
    prcb->CurrentThread = next;
    prcb->ContextSwitches++;
    next->ContextSwitches++;
    /* a busy processor ticks for its quanta, an idle one sleeps until its next timer. */
    if ((old == prcb->IdleThread) != (next == prcb->IdleThread)) {
        if (old == prcb->IdleThread)
            prcb->TickTime = KiQueryInterruptTime();
        KiProgramNextTimer();
    }
    if (next->ApcStateProcess != old->ApcStateProcess && next->ApcStateProcess->DirectoryTableBase)
        KiSwitchAddressSpace(next->ApcStateProcess);
    ULONG64 switched_out = HalReadCounter();
    KiSwapContext(old, next);
    old->BlockedTicks += HalReadCounter() - switched_out;
}

/* moves a still running thread back to the ready queue and dispatches, unless it keeps the processor. */
static void preempt_current(PKPRCB prcb)
{
    PKTHREAD cur = prcb->CurrentThread;
    if (cur->State == Running && cur != prcb->IdleThread) {
        int top = KiReadySummary ? 31 - __builtin_clz(KiReadySummary) : -1;
        if (top < cur->Priority || (top == cur->Priority && cur->Quantum > 0)) {
            prcb->DispatchPending = FALSE;
            return;
        }
        KiInsertReadyThread(cur);
    }
    KiSwapThread();
}

/* runs dpcs and pending reschedules; callable with interrupts masked or not. */
void KiDispatchPendingWork(void)
{
    PKPCR pcr = KeGetPcr();
    PKPRCB prcb = pcr->CurrentPrcb;
    if (pcr->Irql >= DISPATCH_LEVEL)
        return;
    ULONG64 s = KiAcquireDispatcherLock();
    if (prcb->DpcPending) {
        KIRQL old = pcr->Irql;
        pcr->Irql = DISPATCH_LEVEL;
        KiDeliverDpcs();
        pcr->Irql = old;
    }
    if (prcb->DispatchPending)
        preempt_current(prcb);
    KiReleaseDispatcherLock(s);
}

void KiCheckPreemption(void)
{
    PKPRCB prcb = KeGetCurrentPrcb();
    if (prcb->DispatchPending && KeGetPcr()->Irql < DISPATCH_LEVEL)
        preempt_current(prcb);
}

/* ------------------------------------------------------------------ dpcs */

void NTAPI KeInitializeDpc(PRKDPC dpc, PKDEFERRED_ROUTINE routine, PVOID context)
{
    memset(dpc, 0, sizeof(*dpc));
    dpc->Type = DpcObject;
    dpc->DeferredRoutine = routine;
    dpc->DeferredContext = context;
}

BOOLEAN NTAPI KeInsertQueueDpc(PRKDPC dpc, PVOID arg1, PVOID arg2)
{
    PKPRCB prcb = KeGetCurrentPrcb();
    ULONG64 s = KiAcquireDispatcherLock();
    if (dpc->Inserted) {
        KiReleaseDispatcherLock(s);
        return FALSE;
    }
    dpc->Inserted = 1;
    dpc->SystemArgument1 = arg1;
    dpc->SystemArgument2 = arg2;
    dpc->DpcListEntry.Next = nullptr;
    /* append at the tail so dpcs run in queue order. */
    PSINGLE_LIST_ENTRY *link = &prcb->DpcHead.Next;
    while (*link)
        link = &(*link)->Next;
    *link = &dpc->DpcListEntry;
    prcb->DpcPending = TRUE;
    KiReleaseDispatcherLock(s);
    return TRUE;
}

BOOLEAN NTAPI KeRemoveQueueDpc(PRKDPC dpc)
{
    ULONG64 s = KiAcquireDispatcherLock();
    BOOLEAN removed = FALSE;
    for (ULONG i = 0; i < KeNumberProcessors && dpc->Inserted && !removed; i++) {
        PKPRCB prcb = KiProcessorBlock[i];
        for (PSINGLE_LIST_ENTRY *link = &prcb->DpcHead.Next; *link; link = &(*link)->Next) {
            if (*link == &dpc->DpcListEntry) {
                *link = dpc->DpcListEntry.Next;
                dpc->Inserted = 0;
                removed = TRUE;
                break;
            }
        }
    }
    KiReleaseDispatcherLock(s);
    return removed;
}

/* dispatcher lock held, irql DISPATCH_LEVEL. */
void KiDeliverDpcs(void)
{
    PKPRCB prcb = KeGetCurrentPrcb();
    ULONG64 start = HalReadCounter();
    while (prcb->DpcHead.Next) {
        PSINGLE_LIST_ENTRY e = prcb->DpcHead.Next;
        prcb->DpcHead.Next = e->Next;
        PKDPC dpc = CONTAINING_RECORD(e, KDPC, DpcListEntry);
        dpc->Inserted = 0;
        prcb->DpcCount++;
        PKDEFERRED_ROUTINE routine = dpc->DeferredRoutine;
        PVOID ctx = dpc->DeferredContext, a1 = dpc->SystemArgument1, a2 = dpc->SystemArgument2;
        routine(dpc, ctx, a1, a2);
    }
    prcb->DpcPending = FALSE;
    KiChargeServiceTime(prcb, start, TRUE);
}

ULONG NTAPI KeGetCurrentProcessorNumber(void)
{
    return KeGetCurrentPrcb()->Number;
}

PKTHREAD NTAPI KeGetCurrentThread(void)
{
    return KiCurrentThread();
}

KPROCESSOR_MODE NTAPI KeGetPreviousMode(void)
{
    return KiCurrentThread()->PreviousMode;
}

KPROCESSOR_MODE NTAPI ExGetPreviousMode(void)
{
    return KiCurrentThread()->PreviousMode;
}
