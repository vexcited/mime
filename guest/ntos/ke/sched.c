/*
 * thread scheduler: ready queues, dispatching and dpcs.
 */
#include "../ntos.h"

#define KI_DEFAULT_QUANTUM_HNS 300000 /* 30 ms, desktop style quanta */

void KiInitializeScheduler(void)
{
    PKPRCB prcb = KeGetCurrentPrcb();
    for (int i = 0; i < MAXIMUM_PRIORITY; i++)
        InitializeListHead(&prcb->ReadyListHead[i]);
    prcb->ReadySummary = 0;
    prcb->DpcHead.Next = NULL;
    prcb->DpcTail = NULL;
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
    thread->Priority = 8;
    thread->BasePriority = 8;
    thread->Quantum = KI_DEFAULT_QUANTUM_HNS;
    thread->State = Initialized;
    thread->PreviousMode = KernelMode;

    /* the user trap frame (if any) sits at the top, the switch frame below it. */
    ULONG64 *frame = (ULONG64 *)((PUCHAR)stack_top - KTRAP_FRAME_SIZE - KSWITCH_FRAME_SIZE);
    memset(frame, 0, KSWITCH_FRAME_SIZE);
    frame[0] = (ULONG64)routine;      /* x19 */
    frame[1] = (ULONG64)context;      /* x20 */
    frame[11] = (ULONG64)KiThreadStartup; /* x30 */
    thread->KernelSp = (ULONG64)frame;
    thread->TrapFrame = (PKTRAP_FRAME)((PUCHAR)stack_top - KTRAP_FRAME_SIZE);

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
void KiReadyThread(PKTHREAD thread)
{
    PKPRCB prcb = KeGetCurrentPrcb();
    if (thread == prcb->IdleThread) {
        thread->State = Ready;
        return;
    }
    int prio = thread->Priority;
    thread->State = Ready;
    InsertTailList(&prcb->ReadyListHead[prio], &thread->ReadyListEntry);
    prcb->ReadySummary |= 1u << prio;
    PKTHREAD cur = prcb->CurrentThread;
    if (cur == prcb->IdleThread || prio > cur->Priority)
        prcb->DispatchPending = TRUE;
}

void KiStartThread(PKTHREAD thread)
{
    ULONG64 s = KiAcquireDispatcherLock();
    KiReadyThread(thread);
    KiReleaseDispatcherLock(s);
}

static PKTHREAD select_next(PKPRCB prcb)
{
    if (!prcb->ReadySummary)
        return prcb->IdleThread;
    int prio = 31 - __builtin_clz(prcb->ReadySummary);
    PLIST_ENTRY e = RemoveHeadList(&prcb->ReadyListHead[prio]);
    if (IsListEmpty(&prcb->ReadyListHead[prio]))
        prcb->ReadySummary &= ~(1u << prio);
    return CONTAINING_RECORD(e, KTHREAD, ReadyListEntry);
}

void KiSwitchAddressSpace(PKPROCESS process)
{
    KI_WRITE_SYSREG(ttbr0_el1, process->Ttbr0);
    KiInstructionBarrier();
}

/*
 * switches to the best ready thread. the caller holds the dispatcher lock
 * and has already moved the current thread out of the running state.
 */
void KiSwapThread(void)
{
    PKPRCB prcb = KeGetCurrentPrcb();
    PKTHREAD old = prcb->CurrentThread;
    PKTHREAD next = select_next(prcb);
    prcb->DispatchPending = FALSE;
    if (next == old) {
        old->State = Running;
        return;
    }
    next->State = Running;
    next->Quantum = KI_DEFAULT_QUANTUM_HNS;
    prcb->CurrentThread = next;
    prcb->ContextSwitches++;
    next->ContextSwitches++;
    if (next->ApcStateProcess != old->ApcStateProcess && next->ApcStateProcess->Ttbr0)
        KiSwitchAddressSpace(next->ApcStateProcess);
    KiSwapContextInternal(&old->KernelSp, next->KernelSp);
}

/* moves a still running thread back to the ready queue and dispatches. */
static void preempt_current(PKPRCB prcb)
{
    PKTHREAD cur = prcb->CurrentThread;
    if (cur->State == Running && cur != prcb->IdleThread) {
        int prio = cur->Priority;
        cur->State = Ready;
        InsertTailList(&prcb->ReadyListHead[prio], &cur->ReadyListEntry);
        prcb->ReadySummary |= 1u << prio;
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
    dpc->DpcListEntry.Next = NULL;
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
    PKPRCB prcb = KeGetCurrentPrcb();
    ULONG64 s = KiAcquireDispatcherLock();
    BOOLEAN removed = FALSE;
    for (PSINGLE_LIST_ENTRY *link = &prcb->DpcHead.Next; *link; link = &(*link)->Next) {
        if (*link == &dpc->DpcListEntry) {
            *link = dpc->DpcListEntry.Next;
            dpc->Inserted = 0;
            removed = TRUE;
            break;
        }
    }
    KiReleaseDispatcherLock(s);
    return removed;
}

/* dispatcher lock held, irql DISPATCH_LEVEL. */
void KiDeliverDpcs(void)
{
    PKPRCB prcb = KeGetCurrentPrcb();
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
