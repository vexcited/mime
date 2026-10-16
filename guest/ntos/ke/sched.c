/*
 * thread scheduler: ready queues, dispatching and dpcs.
 */
#include "../ntos.h"

#define KI_DEFAULT_QUANTUM_HNS 300000 /* 30 ms, desktop style quanta */

LIST_ENTRY KiReadyListHead[MAXIMUM_PRIORITY];
ULONG KiReadySummary;
PKPRCB KiProcessorBlock[MAXIMUM_PROCESSORS];
ULONG KeNumberProcessors = 1;
volatile ULONG KiKernelLock[4] = {0, ~0u, 0, 0};
ULONG64 KiKernelLockPhys;

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
    thread->Priority = 8;
    thread->BasePriority = 8;
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
    KiSwapContext(old, next);
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
