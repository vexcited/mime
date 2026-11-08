/*
 * dispatcher objects and waits.
 */
#include "../ntos.h"

/* ------------------------------------------------------------ objects */

void NTAPI KeInitializeEvent(PRKEVENT event, EVENT_TYPE type, BOOLEAN state)
{
    event->Header.Type = type == NotificationEvent ? EventNotificationObject : EventSynchronizationObject;
    event->Header.Size = sizeof(KEVENT) / 8;
    event->Header.Signalling = 0;
    event->Header.SignalState = state ? 1 : 0;
    InitializeListHead(&event->Header.WaitListHead);
}

LONG NTAPI KeSetEvent(PRKEVENT event, KPRIORITY increment, BOOLEAN wait)
{
    ULONG64 s = KiAcquireDispatcherLock();
    LONG old = event->Header.SignalState;
    if (!old) {
        event->Header.SignalState = 1;
        KiSignalWaiters(&event->Header, increment);
    }
    KiReleaseDispatcherLock(s);
    if (!wait)
        KiCheckPreemption();
    return old;
}

LONG NTAPI KePulseEvent(PRKEVENT event, KPRIORITY increment, BOOLEAN wait)
{
    ULONG64 s = KiAcquireDispatcherLock();
    LONG old = event->Header.SignalState;
    event->Header.SignalState = 1;
    KiSignalWaiters(&event->Header, increment);
    event->Header.SignalState = 0;
    KiReleaseDispatcherLock(s);
    return old;
}

LONG NTAPI KeResetEvent(PRKEVENT event)
{
    ULONG64 s = KiAcquireDispatcherLock();
    LONG old = event->Header.SignalState;
    event->Header.SignalState = 0;
    KiReleaseDispatcherLock(s);
    return old;
}

void NTAPI KeClearEvent(PRKEVENT event)
{
    event->Header.SignalState = 0;
}

LONG NTAPI KeReadStateEvent(PRKEVENT event)
{
    return event->Header.SignalState;
}

void NTAPI KeInitializeSemaphore(PRKSEMAPHORE sem, LONG count, LONG limit)
{
    sem->Header.Type = SemaphoreObject;
    sem->Header.Size = sizeof(KSEMAPHORE) / 8;
    sem->Header.SignalState = count;
    InitializeListHead(&sem->Header.WaitListHead);
    sem->Limit = limit;
}

LONG NTAPI KeReleaseSemaphore(PRKSEMAPHORE sem, KPRIORITY increment, LONG adjustment, BOOLEAN wait)
{
    ULONG64 s = KiAcquireDispatcherLock();
    LONG old = sem->Header.SignalState;
    if (adjustment <= 0 || old + adjustment > sem->Limit || old + adjustment < old) {
        KiReleaseDispatcherLock(s);
        return -1;
    }
    sem->Header.SignalState = old + adjustment;
    if (!old)
        KiSignalWaiters(&sem->Header, increment);
    KiReleaseDispatcherLock(s);
    return old;
}

void NTAPI KeInitializeMutant(PRKMUTANT mutant, BOOLEAN initial_owner)
{
    mutant->Header.Type = MutantObject;
    mutant->Header.Size = sizeof(KMUTANT) / 8;
    InitializeListHead(&mutant->Header.WaitListHead);
    mutant->Abandoned = FALSE;
    mutant->ApcDisable = 0;
    if (initial_owner) {
        PKTHREAD self = KiCurrentThread();
        mutant->Header.SignalState = 0;
        mutant->OwnerThread = self;
        ULONG64 s = KiAcquireDispatcherLock();
        InsertHeadList(&self->MutantListHead, &mutant->MutantListEntry);
        KiReleaseDispatcherLock(s);
    } else {
        mutant->Header.SignalState = 1;
        mutant->OwnerThread = nullptr;
    }
}

void NTAPI KeInitializeMutex(PRKMUTEX mutex, ULONG level)
{
    KeInitializeMutant(mutex, FALSE);
    mutex->ApcDisable = 1;
}

LONG NTAPI KeReleaseMutant(PRKMUTANT mutant, KPRIORITY increment, BOOLEAN abandoned, BOOLEAN wait)
{
    ULONG64 s = KiAcquireDispatcherLock();
    PKTHREAD owner = mutant->OwnerThread;
    LONG old = mutant->Header.SignalState;
    if (!abandoned && mutant->OwnerThread != KiCurrentThread()) {
        KiReleaseDispatcherLock(s);
        return (LONG)0x80000000; /* the caller maps this to STATUS_MUTANT_NOT_OWNED. */
    }
    if (abandoned) {
        mutant->Header.SignalState = 1;
        mutant->Abandoned = TRUE;
    } else {
        mutant->Header.SignalState++;
    }
    if (mutant->Header.SignalState == 1) {
        if (owner) {
            RemoveEntryList(&mutant->MutantListEntry);
            owner->KernelApcDisable += mutant->ApcDisable;
        }
        mutant->OwnerThread = nullptr;
        KiSignalWaiters(&mutant->Header, increment);
    }
    KiReleaseDispatcherLock(s);
    if (owner == KiCurrentThread() && !owner->KernelApcDisable && !owner->SpecialApcDisable &&
        owner->KernelApcPending && KeGetPcr()->Irql == PASSIVE_LEVEL)
        KiDeliverApcs(KernelMode, nullptr);
    return old;
}

LONG NTAPI KeReleaseMutex(PRKMUTEX mutex, BOOLEAN wait)
{
    return KeReleaseMutant(mutex, 1, FALSE, wait);
}

/* -------------------------------------------------------------- waits */

static BOOLEAN is_signaled(PDISPATCHER_HEADER h, PKTHREAD thread)
{
    if (h->Type == MutantObject)
        return h->SignalState > 0 || ((PKMUTANT)h)->OwnerThread == thread;
    return h->SignalState > 0;
}

/* consumes the signal of a satisfied wait. returns whether it took a mutant its owner ended without releasing. */
static BOOLEAN acquire_object(PDISPATCHER_HEADER h, PKTHREAD thread)
{
    switch (h->Type) {
    case EventSynchronizationObject:
    case TimerSynchronizationObject:
        h->SignalState = 0;
        break;
    case SemaphoreObject:
        h->SignalState--;
        break;
    case MutantObject: {
        PKMUTANT m = (PKMUTANT)h;
        h->SignalState--;
        if (h->SignalState == 0) {
            m->OwnerThread = thread;
            thread->KernelApcDisable -= m->ApcDisable;
            InsertHeadList(&thread->MutantListHead, &m->MutantListEntry);
        }
        /* the abandoned state is told once, to the wait that takes the mutant. */
        BOOLEAN abandoned = m->Abandoned;
        m->Abandoned = FALSE;
        return abandoned;
    }
    default:
        break;
    }
    return FALSE;
}

/* dispatcher lock held. an object that would satisfy the thread's wait right now, a sign of a lost wake, or null. */
PVOID KiSatisfiedWaitObject(PKTHREAD thread)
{
    if (thread->State != Waiting || !thread->WaitBlockList)
        return nullptr;
    BOOLEAN all = thread->WaitBlockList->WaitType == WaitAll;
    PVOID first = nullptr;
    for (PKWAIT_BLOCK b = thread->WaitBlockList; b; b = b->NextWaitBlock) {
        if (b->WaitKey == STATUS_TIMEOUT)
            continue;
        BOOLEAN signaled = is_signaled(b->Object, thread);
        if (all && !signaled)
            return nullptr;
        if (signaled && !first)
            first = b->Object;
    }
    return first;
}

static BOOLEAN thread_all_signaled(PKTHREAD thread)
{
    for (PKWAIT_BLOCK b = thread->WaitBlockList; b; b = b->NextWaitBlock) {
        if (b->WaitKey == STATUS_TIMEOUT)
            continue;
        if (!is_signaled(b->Object, thread))
            return FALSE;
    }
    return TRUE;
}

static void cancel_thread_timer(PKTHREAD thread);

/* dispatcher lock held. */
void KiUnwaitThread(PKTHREAD thread, LONG_PTR status)
{
    for (PKWAIT_BLOCK b = thread->WaitBlockList; b; b = b->NextWaitBlock) {
        if (b->BlockState) {
            RemoveEntryList(&b->WaitListEntry);
            b->BlockState = 0;
        }
    }
    cancel_thread_timer(thread);
    thread->WaitBlockList = nullptr;
    thread->WaitStatus = status;
    KiReadyThread(thread);
}

/* dispatcher lock held. wakes threads whose wait the object now satisfies. */
void KiSignalWaiters(PDISPATCHER_HEADER object, KPRIORITY increment)
{
    PLIST_ENTRY e = object->WaitListHead.Flink;
    while (e != &object->WaitListHead && object->SignalState > 0) {
        PKWAIT_BLOCK b = CONTAINING_RECORD(e, KWAIT_BLOCK, WaitListEntry);
        e = e->Flink;
        PKTHREAD thread = b->Thread;
        if (thread->State != Waiting)
            continue;
        if (b->WaitType == WaitAny) {
            LONG_PTR status = b->WaitKey;
            if (b->WaitKey != STATUS_TIMEOUT && acquire_object(object, thread))
                status += STATUS_ABANDONED;
            KiUnwaitThread(thread, status);
        } else if (thread_all_signaled(thread)) {
            LONG_PTR status = STATUS_WAIT_0;
            for (PKWAIT_BLOCK w = thread->WaitBlockList; w; w = w->NextWaitBlock)
                if (w->WaitKey != STATUS_TIMEOUT && acquire_object(w->Object, thread))
                    status = STATUS_ABANDONED;
            KiUnwaitThread(thread, status);
        }
    }
}

static void cancel_thread_timer(PKTHREAD thread)
{
    if (thread->Timer.TimerListEntry.Flink) {
        RemoveEntryList(&thread->Timer.TimerListEntry);
        thread->Timer.TimerListEntry.Flink = nullptr;
    }
}

void KiInsertTimer(PKTIMER timer, ULONG64 due_interrupt_time);

/*
 * a process that never asked for a finer timer resolution has the waits of its user mode threads end on the clock
 * tick, 15.625 ms, whatever the other processes asked for. a one millisecond sleep lasts until the next tick, as on
 * Windows since version 2004, where the resolution a process asked for is its own and not the system's.
 */
static ULONG64 round_to_tick(PKTHREAD thread, KPROCESSOR_MODE mode, ULONG64 due)
{
    if (mode != UserMode || !due || ((PEPROCESS)thread->Process)->TimerResolution || (KiOptions & KI_OPT_PRECISE_TIMERS))
        return due;
    return (due + KI_CLOCK_TICK_HNS - 1) / KI_CLOCK_TICK_HNS * KI_CLOCK_TICK_HNS;
}

NTSTATUS KiWaitObjects(ULONG count, PVOID objects[], WAIT_TYPE type, KWAIT_REASON reason, KPROCESSOR_MODE mode,
                       BOOLEAN alertable, PLARGE_INTEGER timeout, PKWAIT_BLOCK blocks)
{
    PKTHREAD thread = KiCurrentThread();
    KIRQL irql = KiCurrentIrql();
    if (irql > DISPATCH_LEVEL || (irql == DISPATCH_LEVEL && (!timeout || timeout->QuadPart != 0)))
        KeBugCheckEx(0x0000000A, (ULONG_PTR)objects, irql, 0, 0);
    if (count > MAXIMUM_WAIT_OBJECTS)
        return STATUS_INVALID_PARAMETER;
    if (!blocks)
        blocks = thread->WaitBlock;
    /* an idle thread has nothing to give its processor to, so its wait would return as if satisfied. */
    if (thread->IdleThread) {
        static BOOLEAN said;
        if (!said) {
            said = TRUE;
            KiLogPrintf("an idle thread waits, from %p", __builtin_return_address(0));
        }
    }

    ULONG64 due = 0;
    if (timeout && timeout->QuadPart != 0) {
        if (timeout->QuadPart < 0)
            due = KiQueryInterruptTime() + (ULONG64)(-timeout->QuadPart);
        else
            due = (ULONG64)timeout->QuadPart > KeBootFiletime ? (ULONG64)timeout->QuadPart - KeBootFiletime : 1;
        due = round_to_tick(thread, mode, due);
    }

    for (;;) {
        ULONG64 s = KiAcquireDispatcherLock();

        if (thread->KernelApcPending && !thread->KernelApcDisable && !thread->SpecialApcDisable &&
            KeGetPcr()->Irql == PASSIVE_LEVEL) {
            KiReleaseDispatcherLock(s);
            KiDeliverApcs(KernelMode, nullptr);
            continue;
        }

        if (type == WaitAny) {
            for (ULONG i = 0; i < count; i++) {
                PDISPATCHER_HEADER h = objects[i];
                if (is_signaled(h, thread)) {
                    BOOLEAN abandoned = acquire_object(h, thread);
                    KiReleaseDispatcherLock(s);
                    return (abandoned ? STATUS_ABANDONED : STATUS_WAIT_0) + (NTSTATUS)i;
                }
            }
        } else {
            BOOLEAN all = TRUE;
            for (ULONG i = 0; i < count && all; i++)
                all = is_signaled(objects[i], thread);
            if (all) {
                BOOLEAN abandoned = FALSE;
                for (ULONG i = 0; i < count; i++)
                    abandoned |= acquire_object(objects[i], thread);
                KiReleaseDispatcherLock(s);
                return abandoned ? STATUS_ABANDONED : STATUS_WAIT_0;
            }
        }

        /* user apcs queued while the thread was running are delivered by the first alertable wait. */
        if (mode == UserMode && alertable && !IsListEmpty(&thread->ApcListHead[UserMode]))
            thread->UserApcPending = TRUE;
        if (mode == UserMode && alertable && (thread->UserApcPending || thread->Alerted[UserMode])) {
            thread->Alerted[UserMode] = FALSE;
            KiReleaseDispatcherLock(s);
            return thread->UserApcPending ? STATUS_USER_APC : STATUS_ALERTED;
        }
        if (thread->Terminating && mode == UserMode) {
            KiReleaseDispatcherLock(s);
            return STATUS_THREAD_IS_TERMINATING;
        }
        if (timeout && (timeout->QuadPart == 0 || (due && KiQueryInterruptTime() >= due))) {
            KiReleaseDispatcherLock(s);
            return STATUS_TIMEOUT;
        }

        PKWAIT_BLOCK prev = nullptr;
        thread->WaitBlockList = nullptr;
        for (ULONG i = 0; i < count; i++) {
            PKWAIT_BLOCK b = &blocks[i];
            b->Thread = thread;
            b->Object = objects[i];
            b->WaitKey = (USHORT)i;
            b->WaitType = (UCHAR)type;
            b->BlockState = 1;
            b->NextWaitBlock = nullptr;
            InsertTailList(&((PDISPATCHER_HEADER)objects[i])->WaitListHead, &b->WaitListEntry);
            if (prev)
                prev->NextWaitBlock = b;
            else
                thread->WaitBlockList = b;
            prev = b;
        }
        if (due) {
            PKWAIT_BLOCK tb = &thread->WaitBlock[TIMER_WAIT_BLOCK];
            tb->Thread = thread;
            tb->Object = &thread->Timer;
            tb->WaitKey = (USHORT)STATUS_TIMEOUT;
            tb->WaitType = WaitAny;
            tb->BlockState = 1;
            tb->NextWaitBlock = nullptr;
            thread->Timer.Header.SignalState = 0;
            InsertTailList(&thread->Timer.Header.WaitListHead, &tb->WaitListEntry);
            if (prev)
                prev->NextWaitBlock = tb;
            else
                thread->WaitBlockList = tb;
            KiInsertTimer(&thread->Timer, due);
        }

        thread->WaitMode = mode;
        thread->Alertable = alertable;
        thread->WaitReason = (UCHAR)reason;
        thread->WaitStatus = 0;
        thread->State = Waiting;
        KiSwapThread();
        LONG_PTR status = thread->WaitStatus;
        KiReleaseDispatcherLock(s);

        if (status == STATUS_KERNEL_APC)
            continue;
        return (NTSTATUS)status;
    }
}

NTSTATUS NTAPI KeWaitForSingleObject(PVOID object, KWAIT_REASON reason, KPROCESSOR_MODE mode, BOOLEAN alertable,
                                     PLARGE_INTEGER timeout)
{
    PVOID objects[1] = {object};
    return KiWaitObjects(1, objects, WaitAny, reason, mode, alertable, timeout, nullptr);
}

NTSTATUS NTAPI KeWaitForMultipleObjects(ULONG count, PVOID objects[], WAIT_TYPE type, KWAIT_REASON reason,
                                        KPROCESSOR_MODE mode, BOOLEAN alertable, PLARGE_INTEGER timeout,
                                        PVOID wait_blocks)
{
    if (count > THREAD_WAIT_OBJECTS && !wait_blocks)
        return STATUS_INVALID_PARAMETER;
    return KiWaitObjects(count, objects, type, reason, mode, alertable, timeout, wait_blocks);
}

NTSTATUS NTAPI KeDelayExecutionThread(KPROCESSOR_MODE mode, BOOLEAN alertable, PLARGE_INTEGER interval)
{
    PKTHREAD thread = KiCurrentThread();
    if (interval && interval->QuadPart == 0) {
        /* a zero delay yields to threads of equal priority, or delivers pending user apcs when alertable. */
        ULONG64 s = KiAcquireDispatcherLock();
        if (mode == UserMode && alertable && (!IsListEmpty(&thread->ApcListHead[UserMode]) || thread->Alerted[UserMode])) {
            BOOLEAN apc = !IsListEmpty(&thread->ApcListHead[UserMode]);
            if (apc)
                thread->UserApcPending = TRUE;
            thread->Alerted[UserMode] = FALSE;
            KiReleaseDispatcherLock(s);
            return apc ? STATUS_USER_APC : STATUS_ALERTED;
        }
        BOOLEAN yielded = KiReadySummary != 0;
        if (yielded) {
            KiInsertReadyThread(thread);
            KiSwapThread();
        }
        KiReleaseDispatcherLock(s);
        return yielded ? STATUS_SUCCESS : STATUS_NO_YIELD_PERFORMED;
    }
    NTSTATUS status = KiWaitObjects(0, nullptr, WaitAny, DelayExecution, mode, alertable, interval, nullptr);
    return status == STATUS_TIMEOUT ? STATUS_SUCCESS : status;
}
