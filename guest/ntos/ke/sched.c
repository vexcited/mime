/*
 * thread scheduler: ready queues, dispatching and dpcs.
 */
#include "../ntos.h"

#define KI_DEFAULT_QUANTUM_HNS 300000 /* 30 ms, desktop style quanta. */

LIST_ENTRY KiReadyListHead[MAXIMUM_PRIORITY];
ULONG KiReadySummary;
PKPRCB KiProcessorBlock[MAXIMUM_PROCESSORS];
ULONG KeNumberProcessors = 1;
volatile ULONG KiKernelLock[32] __attribute__((aligned(64))) = {[KI_LOCK_OWNER] = ~0u};
/* lock holds longer than this many ticks are counted by what held them, see KiNoteLongHold. */
ULONG64 KiLongHoldTicks = ~0ull;
ULONG64 KiKernelLockPhys;
ULONG KiOptions;
/* kernel option nolockflush: the lock no longer settles user translations other processors changed. */
UCHAR KiNoLockFlush;
/* a row of eight per processor: acquisitions, those that waited, ticks waited, the longest wait. see entry.S. */
ULONG64 KiLockStats[MAXIMUM_PROCESSORS][8];

#ifdef __x86_64__
/* a cache line to itself: the waiters read it while they spin. */
volatile ULONG KiDispatcherLock __attribute__((aligned(64)));

void KiDispatcherLockContended(void)
{
    PKPCR pcr = KeGetPcr();
    ULONG64 start = HalReadCounter();
    do {
        while (__atomic_load_n(&KiDispatcherLock, __ATOMIC_RELAXED)) {
            /* a processor that changed translations may be waiting for this one to take the change in. */
            if (pcr->FlushPending)
                KiSettleTlbFlush();
            KiPause();
        }
    } while (__atomic_exchange_n(&KiDispatcherLock, 1, __ATOMIC_ACQUIRE));
    pcr->Prcb.DispatcherWaited++;
    pcr->Prcb.DispatcherWaitTicks += HalReadCounter() - start;
}

/* the lock is let go and interrupts are still off. */
void KiDispatcherUnlockWork(PKPRCB prcb)
{
    UCHAR work = prcb->UnlockWork;
    prcb->UnlockWork = 0;
    if ((work & KI_UNLOCK_TIMER) && prcb->TimerArmed)
        HalArmTimer(prcb->TimerArmed);
    /* with the kernel lock held, they go once that is let go. */
    if ((work & KI_UNLOCK_IPI) && KiKernelLock[KI_LOCK_OWNER] != prcb->Number) {
        ULONG64 mask = __atomic_exchange_n(&prcb->DeferredIpis, 0, __ATOMIC_RELAXED);
        if (mask)
            HalSendIpiMask(mask);
    }
}
#endif

/* said once: a caller that works on the scheduler's structures without the dispatcher lock. */
void KiDispatcherLockMissing(const char *what, PVOID caller)
{
    static BOOLEAN said;
    if (!said) {
        said = TRUE;
        KiLogPrintf("%s without the dispatcher lock, from %p", what, caller);
    }
}

ULONG64 HalHypercallUnlocked(ULONG64 nr, ULONG64 a1, ULONG64 a2, ULONG64 a3, ULONG64 a4, ULONG64 a5, ULONG64 a6,
                             PULONG64 value)
{
    /* interrupts stay off: an interrupt taken meanwhile would run without the lock. */
    ULONG64 state = KiDisableInterrupts();
    /* the kernel lock is never taken with the dispatcher lock held, so a holder of both keeps both. */
    BOOLEAN held = KeGetPcr()->Irql < DISPATCH_LEVEL && KiKernelLock[KI_LOCK_OWNER] == KeGetCurrentPrcb()->Number &&
                   !KiDispatcherLockHeld();
    if (!held) {
        KiRestoreInterrupts(state);
        return HalHypercall(nr, a1, a2, a3, a4, a5, a6, value);
    }
    ULONG64 start = HalReadCounter();
    /* the release forgets what the lock was held for. */
    ULONG reason = KiKernelLock[KI_LOCK_REASON];
    KiReleaseKernelLock();
    ULONG64 status = HalHypercall(nr, a1, a2, a3, a4, a5, a6, value);
    KiAcquireKernelLock();
    KiKernelLock[KI_LOCK_REASON] = reason;
    /* time away does not count as holding the lock in the service statistics. */
    KiCurrentThread()->BlockedTicks += HalReadCounter() - start;
    KiRestoreInterrupts(state);
    return status;
}

/*
 * a copy between kernel memory that may wait for the host, as one out of a file window does while the host reads the
 * file: made like the hypercall above, in pieces so that interrupts are not off for the whole of it.
 */
void KiCopyUnlocked(void *to, const void *from, SIZE_T length)
{
    while (length) {
        SIZE_T n = length < 0x10000 ? length : 0x10000;
        ULONG64 state = KiDisableInterrupts();
        BOOLEAN held = KeGetPcr()->Irql < DISPATCH_LEVEL && KiKernelLock[KI_LOCK_OWNER] == KeGetCurrentPrcb()->Number &&
                       !KiDispatcherLockHeld();
        ULONG64 start = held ? HalReadCounter() : 0;
        ULONG reason = held ? KiKernelLock[KI_LOCK_REASON] : 0;
        if (held)
            KiReleaseKernelLock();
        memcpy(to, from, n);
        if (held) {
            KiAcquireKernelLock();
            KiKernelLock[KI_LOCK_REASON] = reason;
            KiCurrentThread()->BlockedTicks += HalReadCounter() - start;
        }
        KiRestoreInterrupts(state);
        to = (PUCHAR)to + n;
        from = (const UCHAR *)from + n;
        length -= n;
    }
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

ULONG64 KiAllProcessors(void)
{
    return KeNumberProcessors >= 64 ? ~0ull : (1ull << (KeNumberProcessors ? KeNumberProcessors : 1)) - 1;
}

/* all processors when the process was never limited. */
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

    /* the user trap frame, if any, sits at the top and the switch frame below it. */
    KiInitializeSwitchFrame(thread, routine, context);

    ULONG64 s = KiAcquireDispatcherLock();
    InsertTailList(&process->ThreadListHead, &thread->ThreadListEntry);
    process->ActiveThreads++;
    KiReleaseDispatcherLock(s);
}

/* first c code of every thread; interrupts are still masked by the switch. */
void KiThreadStartupC(PKSTART_ROUTINE routine, PVOID context)
{
#ifdef __x86_64__
    /* the switch came with the dispatcher lock held, which a thread that ran before lets go of in KiSwapThread's caller. */
    KiReleaseDispatcherLock(0);
    /* a processor that was idle comes without the kernel lock, and one left by a fast service has UserMode set. */
    if (KiKernelLock[KI_LOCK_OWNER] != KeGetCurrentPrcb()->Number) {
        KeGetPcr()->UserMode = 0;
        KiAcquireKernelLock();
    }
#endif
    KeGetPcr()->Irql = PASSIVE_LEVEL;
    KiEnableInterrupts();
#ifdef __x86_64__
    if (KiForeignUnlock && KiIsForeignCode((ULONG_PTR)routine))
        KiForeignRun(routine, context);
    else
#endif
        routine(context);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* dispatcher lock held. */
void KiInsertReadyThread(PKTHREAD thread)
{
    KI_ASSERT_DISPATCHER_LOCK("a thread queued");
    /* a thread queued twice would tie the queue in a loop: said once, and the second time ignored. */
    if (thread->State == Ready && thread->ReadyListEntry.Flink && thread->ReadyListEntry.Flink != &thread->ReadyListEntry) {
        static BOOLEAN said;
        if (!said) {
            said = TRUE;
            KiLogPrintf("thread %p is made ready again from %p", thread, __builtin_return_address(0));
        }
        return;
    }
    int prio = thread->Priority;
    thread->State = Ready;
    InsertTailList(&KiReadyListHead[prio], &thread->ReadyListEntry);
    KiReadySummary |= 1u << prio;
    if (SharedUserData)
        SharedUserData->MimeReadyThreads = KiReadySummary;
}

/*
 * the interrupt waits for this processor to let go of the kernel lock: its target could only wait for the lock
 * until then, and sending it is an exit to the host that would count in the time the lock is held. the same
 * goes for the dispatcher lock.
 */
void KiSendIpi(ULONG processor)
{
    ULONG64 state = KiDisableInterrupts();
    PKPRCB prcb = KeGetCurrentPrcb();
    BOOLEAN kernel = KiKernelLock[KI_LOCK_OWNER] == prcb->Number;
    if (kernel || KiDispatcherLockHeld()) {
        __atomic_fetch_or(&prcb->DeferredIpis, 1ull << processor, __ATOMIC_RELAXED);
#ifdef __x86_64__
        if (!kernel)
            prcb->UnlockWork |= KI_UNLOCK_IPI;
#endif
    } else {
        HalSendIpi(processor);
    }
    KiRestoreInterrupts(state);
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
        KiSendIpi(number);
    }
}

/* makes a thread running on another processor enter the kernel, where it sees new apcs or termination. */
void KiInterruptThread(PKTHREAD thread)
{
    if (thread->State == Running && thread->Processor != KeGetCurrentPrcb()->Number)
        KiSendIpi(thread->Processor);
}

/*
 * the processor that should run a thread just made ready: an idle one first, else the one running the lowest
 * priority below the thread's. none leaves it queued.
 *
 * among the idle ones this processor comes first, which needs no waking, then those still watching their flags,
 * see KiIdleWait, which need no interrupt, then the halted ones. the processor the thread last ran on wins a tie.
 * only the processors of the thread's affinity count.
 */
static PKPRCB pick_processor(PKTHREAD thread)
{
    PKPRCB self = KeGetCurrentPrcb(), idle = nullptr, low = nullptr;
    int low_prio = thread->Priority, best = 0;
    for (ULONG i = 0; i < KeNumberProcessors; i++) {
        PKPRCB p = KiProcessorBlock[i];
        if (!p || p->DispatchPending || !(thread->Affinity >> i & 1))
            continue;
        PKTHREAD cur = p->CurrentThread;
        if (cur == p->IdleThread) {
            int rank = p == self ? 4 : (p->IdleState == KI_IDLE_POLLING ? 2 : 0) + (i == thread->Processor);
            if (!idle || rank > best) {
                idle = p;
                best = rank;
            }
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
#ifndef __x86_64__
    KI_ASSERT_KERNEL_LOCK();
#endif
    KI_ASSERT_DISPATCHER_LOCK("a thread made ready");
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
        InitializeListHead(&thread->ReadyListEntry);
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

/* dispatcher lock held. the current thread may come back on another processor, with the lock let go meanwhile. */
static void set_affinity(PKTHREAD thread, ULONG64 mask)
{
    thread->Affinity = mask;
    if (thread->IdleThread)
        return;
    if (thread->State == Ready) {
        /* it may wait for a processor it can use now. */
        PKPRCB target = pick_processor(thread);
        if (target)
            KiRequestDispatch(target->Number);
    } else if (thread->State == Running && !(mask >> thread->Processor & 1)) {
        if (thread == KeGetCurrentPrcb()->CurrentThread) {
            KiReadyThread(thread);
            KiSwapThread();
        } else {
            KiRequestDispatch(thread->Processor);
        }
    }
}

/*
 * holds a thread to the processors of a mask. like windows, a thread that sets its own is on one of them when the
 * call returns, and another thread leaves a processor it may no longer use as soon as that processor is told.
 */
void KiSetAffinityThread(PKTHREAD thread, ULONG64 mask)
{
    ULONG64 s = KiAcquireDispatcherLock();
    set_affinity(thread, mask);
    KiReleaseDispatcherLock(s);
}

/* threads are held to the process mask, and a thread left with no processor in it gets the whole mask. */
void KiSetAffinityProcess(PKPROCESS process, ULONG64 mask)
{
    ULONG64 s = KiAcquireDispatcherLock();
    PKTHREAD self = KeGetCurrentPrcb()->CurrentThread;
    process->Affinity = mask;
    for (PLIST_ENTRY e = process->ThreadListHead.Flink; e != &process->ThreadListHead; e = e->Flink) {
        PKTHREAD thread = CONTAINING_RECORD(e, KTHREAD, ThreadListEntry);
        if (thread != self)
            set_affinity(thread, (thread->Affinity & mask) ? (thread->Affinity & mask) : mask);
    }
    /* the caller last: its move lets go of the lock, and the list may change then. */
    if (self->Process == process)
        set_affinity(self, (self->Affinity & mask) ? (self->Affinity & mask) : mask);
    KiReleaseDispatcherLock(s);
}

void KiStartThread(PKTHREAD thread)
{
    ULONG64 s = KiAcquireDispatcherLock();
    KiReadyThread(thread);
    KiReleaseDispatcherLock(s);
}

/*
 * the best ready thread a processor may run: the first of the highest queue, unless its affinity holds it to other
 * processors, which leaves it queued for them.
 */
static PKTHREAD first_ready(PKPRCB prcb)
{
    for (ULONG summary = KiReadySummary; summary;) {
        int prio = 31 - __builtin_clz(summary);
        PLIST_ENTRY head = &KiReadyListHead[prio];
        if (IsListEmpty(head)) {
            KiLogPrintf("ready queue %d empty although marked", prio);
            KiReadySummary &= ~(1u << prio);
        }
        for (PLIST_ENTRY e = head->Flink; e != head; e = e->Flink) {
            PKTHREAD thread = CONTAINING_RECORD(e, KTHREAD, ReadyListEntry);
            if (thread->Affinity >> prcb->Number & 1)
                return thread;
        }
        summary &= ~(1u << prio);
    }
    return nullptr;
}

static PKTHREAD select_next(PKPRCB prcb)
{
    KI_ASSERT_DISPATCHER_LOCK("a thread chosen");
    PKTHREAD thread = first_ready(prcb);
    if (!thread)
        return prcb->IdleThread;
    int prio = thread->Priority;
    RemoveEntryList(&thread->ReadyListEntry);
    InitializeListHead(&thread->ReadyListEntry);
    if (IsListEmpty(&KiReadyListHead[prio])) {
        KiReadySummary &= ~(1u << prio);
        /* user mode skips yields that would find no other thread to run. */
        if (SharedUserData)
            SharedUserData->MimeReadyThreads = KiReadySummary;
    }
    return thread;
}

#ifdef __x86_64__
/*
 * a thread that held the kernel lock when it was switched away from resumes on a processor without it. the
 * dispatcher lock is let go meanwhile: the wait for the kernel lock can be long, and its holder may need this one.
 * the thread is running by now, so nothing the caller left under the lock is the scheduler's to change.
 */
static void take_kernel_lock_back(PKTHREAD thread)
{
    KiReleaseDispatcherLock(0);
    /*
     * the thread before may be in a service without the kernel lock, see KiFastSystemService, which leaves UserMode
     * set. a processor that changes an address space waits for those, and the wait for the lock answers none.
     */
    KeGetPcr()->UserMode = 0;
    KiAcquireKernelLock();
    KiKernelLock[KI_LOCK_REASON] = thread->LockReason;
    KiNoteLockWait(0);
    (void)KiAcquireDispatcherLock();
}
#endif

/*
 * switches to the best ready thread. the caller holds the dispatcher lock
 * and has already moved the current thread out of the running state.
 *
 * on x64 the kernel lock stays with the processor, as the dispatcher lock does. an idle processor has neither, so
 * the thread it switches to takes the kernel lock itself, when it held it.
 */
void KiSwapThread(void)
{
#ifndef __x86_64__
    KI_ASSERT_KERNEL_LOCK();
#endif
    KI_ASSERT_DISPATCHER_LOCK("a thread switched");
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
        if (old == prcb->IdleThread) {
            prcb->TickTime = KiQueryInterruptTime();
            /* an idle wait left in an interrupt: the processor no longer watches its flags, see KiRequestDispatch. */
            prcb->IdleState = KI_IDLE_RUNNING;
        }
        KiProgramNextTimer();
    }
    if (next->ApcStateProcess != old->ApcStateProcess && next->ApcStateProcess->DirectoryTableBase)
        KiSwitchAddressSpace(next->ApcStateProcess);
    ULONG64 switched_out = HalReadCounter();
    /* the level is the thread's while it runs: the next thread finds its own when it resumes, as windows keeps a wait irql. */
    KIRQL irql = KeGetPcr()->Irql;
#ifdef __x86_64__
    /*
     * the next thread lets go of the lock once, so a thread that took it more than once keeps the count for when it
     * resumes. there should be none.
     */
    ULONG depth = prcb->DispatcherDepth;
    if (depth != 1) {
        static BOOLEAN said;
        if (!said) {
            said = TRUE;
            KiLogPrintf("thread %p switched away with the dispatcher lock taken %u times, from %p", old, depth,
                        __builtin_return_address(0));
        }
        prcb->DispatcherDepth = 1;
    }
    BOOLEAN owned = KiKernelLock[KI_LOCK_OWNER] == prcb->Number;
    old->KernelLocked = owned && old != prcb->IdleThread;
    if (owned)
        old->LockReason = KiKernelLock[KI_LOCK_REASON];
#endif
    KiSwapContext(old, next);
    /* the thread may resume on another processor. */
    PKPCR pcr = KeGetPcr();
#ifdef __x86_64__
    if (old->KernelLocked) {
        if (KiKernelLock[KI_LOCK_OWNER] != pcr->Prcb.Number)
            take_kernel_lock_back(old);
        else
            KiKernelLock[KI_LOCK_REASON] = old->LockReason;
    }
    pcr->Prcb.DispatcherDepth = depth;
#endif
    pcr->Irql = irql;
    /* the wait for the kernel lock counts as time switched out. */
    old->BlockedTicks += HalReadCounter() - switched_out;
}

/* moves a still running thread back to the ready queue and dispatches, unless it keeps the processor. */
static void preempt_current(PKPRCB prcb)
{
    PKTHREAD cur = prcb->CurrentThread;
    if (cur->State == Running && cur != prcb->IdleThread) {
        /* a thread whose affinity no longer holds this processor leaves it whatever waits. */
        if (cur->Affinity >> prcb->Number & 1) {
            PKTHREAD first = first_ready(prcb);
            int top = first ? first->Priority : -1;
            if (top < cur->Priority || (top == cur->Priority && cur->Quantum > 0)) {
                prcb->DispatchPending = FALSE;
                return;
            }
        }
        /* another processor may take it at once, as when a thread held to this one took its place. */
        KiReadyThread(cur);
    }
    KiSwapThread();
}

/*
 * runs dpcs and pending reschedules; callable with interrupts masked or not. a caller that holds the dispatcher
 * lock is in the middle of the scheduler's work: it is left alone, and the request stays for the next interrupt
 * or return to user mode.
 */
void KiDispatchPendingWork(void)
{
    /* the processor is read with interrupts off, which keeps the thread on it. */
    ULONG64 s = KiDisableInterrupts();
    PKPCR pcr = KeGetPcr();
    PKPRCB prcb = pcr->CurrentPrcb;
    if (pcr->Irql < DISPATCH_LEVEL && !KiDispatcherLockHeld()) {
        if (prcb->DpcPending) {
            KIRQL old = pcr->Irql;
            pcr->Irql = DISPATCH_LEVEL;
            KiDeliverDpcs();
            pcr->Irql = old;
        }
        if (prcb->DispatchPending) {
            ULONG64 d = KiAcquireDispatcherLock();
            preempt_current(prcb);
            KiReleaseDispatcherLock(d);
        }
    }
    KiRestoreInterrupts(s);
}

/* callers like KeSetEvent come here with interrupts on: the ready queues change with them off. */
void KiCheckPreemption(void)
{
    /* the processor is read with interrupts off, so a move of the thread cannot make it another's. */
    ULONG64 s = KiDisableInterrupts();
    PKPRCB prcb = KeGetCurrentPrcb();
    if (prcb->DispatchPending && KeGetPcr()->Irql < DISPATCH_LEVEL && !KiDispatcherLockHeld()) {
        ULONG64 d = KiAcquireDispatcherLock();
        preempt_current(prcb);
        KiReleaseDispatcherLock(d);
    }
    KiRestoreInterrupts(s);
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
