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
        mutant->OwnerThread = NULL;
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
    LONG old = mutant->Header.SignalState;
    if (!abandoned && mutant->OwnerThread != KiCurrentThread()) {
        KiReleaseDispatcherLock(s);
        return (LONG)0x80000000; /* caller maps to STATUS_MUTANT_NOT_OWNED */
    }
    if (abandoned) {
        mutant->Header.SignalState = 1;
        mutant->Abandoned = TRUE;
    } else {
        mutant->Header.SignalState++;
    }
    if (mutant->Header.SignalState == 1) {
        if (mutant->OwnerThread)
            RemoveEntryList(&mutant->MutantListEntry);
        mutant->OwnerThread = NULL;
        KiSignalWaiters(&mutant->Header, increment);
    }
    KiReleaseDispatcherLock(s);
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

/* consumes the signal of a satisfied wait. */
static void acquire_object(PDISPATCHER_HEADER h, PKTHREAD thread)
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
            InsertHeadList(&thread->MutantListHead, &m->MutantListEntry);
        }
        break;
    }
    default:
        break;
    }
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
    thread->WaitBlockList = NULL;
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
            if (b->WaitKey != STATUS_TIMEOUT)
                acquire_object(object, thread);
            KiUnwaitThread(thread, b->WaitKey);
        } else if (thread_all_signaled(thread)) {
            for (PKWAIT_BLOCK w = thread->WaitBlockList; w; w = w->NextWaitBlock)
                if (w->WaitKey != STATUS_TIMEOUT)
                    acquire_object(w->Object, thread);
            KiUnwaitThread(thread, STATUS_WAIT_0);
        }
    }
}

static void cancel_thread_timer(PKTHREAD thread)
{
    if (thread->Timer.TimerListEntry.Flink) {
        RemoveEntryList(&thread->Timer.TimerListEntry);
        thread->Timer.TimerListEntry.Flink = NULL;
    }
}

void KiInsertTimer(PKTIMER timer, ULONG64 due_interrupt_time);

NTSTATUS KiWaitObjects(ULONG count, PVOID objects[], WAIT_TYPE type, KWAIT_REASON reason, KPROCESSOR_MODE mode,
                       BOOLEAN alertable, PLARGE_INTEGER timeout, PKWAIT_BLOCK blocks)
{
    PKTHREAD thread = KiCurrentThread();
    if (KeGetPcr()->Irql > DISPATCH_LEVEL ||
        (KeGetPcr()->Irql == DISPATCH_LEVEL && (!timeout || timeout->QuadPart != 0)))
        KeBugCheckEx(0x0000000A, (ULONG_PTR)objects, KeGetPcr()->Irql, 0, 0);
    if (count > MAXIMUM_WAIT_OBJECTS)
        return STATUS_INVALID_PARAMETER;
    if (!blocks)
        blocks = thread->WaitBlock;

    ULONG64 due = 0;
    if (timeout && timeout->QuadPart != 0) {
        if (timeout->QuadPart < 0)
            due = KiQueryInterruptTime() + (ULONG64)(-timeout->QuadPart);
        else
            due = (ULONG64)timeout->QuadPart > KeBootFiletime ? (ULONG64)timeout->QuadPart - KeBootFiletime : 0;
    }

    for (;;) {
        ULONG64 s = KiAcquireDispatcherLock();

        if (thread->KernelApcPending && !thread->KernelApcDisable && KeGetPcr()->Irql == PASSIVE_LEVEL) {
            KiReleaseDispatcherLock(s);
            KiDeliverApcs(KernelMode, NULL);
            continue;
        }

        if (type == WaitAny) {
            for (ULONG i = 0; i < count; i++) {
                PDISPATCHER_HEADER h = objects[i];
                if (is_signaled(h, thread)) {
                    acquire_object(h, thread);
                    BOOLEAN abandoned = h->Type == MutantObject && ((PKMUTANT)h)->Abandoned;
                    KiReleaseDispatcherLock(s);
                    return (abandoned ? STATUS_ABANDONED : STATUS_WAIT_0) + (NTSTATUS)i;
                }
            }
        } else {
            BOOLEAN all = TRUE;
            for (ULONG i = 0; i < count && all; i++)
                all = is_signaled(objects[i], thread);
            if (all) {
                for (ULONG i = 0; i < count; i++)
                    acquire_object(objects[i], thread);
                KiReleaseDispatcherLock(s);
                return STATUS_WAIT_0;
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

        PKWAIT_BLOCK prev = NULL;
        thread->WaitBlockList = NULL;
        for (ULONG i = 0; i < count; i++) {
            PKWAIT_BLOCK b = &blocks[i];
            b->Thread = thread;
            b->Object = objects[i];
            b->WaitKey = (USHORT)i;
            b->WaitType = (UCHAR)type;
            b->BlockState = 1;
            b->NextWaitBlock = NULL;
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
            tb->NextWaitBlock = NULL;
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
        if (status >= 0 && status < (LONG_PTR)count) {
            PDISPATCHER_HEADER h = objects[status];
            if (h->Type == MutantObject && ((PKMUTANT)h)->Abandoned)
                return STATUS_ABANDONED + (NTSTATUS)status;
        }
        return (NTSTATUS)status;
    }
}

NTSTATUS NTAPI KeWaitForSingleObject(PVOID object, KWAIT_REASON reason, KPROCESSOR_MODE mode, BOOLEAN alertable,
                                     PLARGE_INTEGER timeout)
{
    PVOID objects[1] = {object};
    return KiWaitObjects(1, objects, WaitAny, reason, mode, alertable, timeout, NULL);
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
        PKPRCB prcb = KeGetCurrentPrcb();
        if (prcb->ReadySummary) {
            thread->State = Ready;
            InsertTailList(&prcb->ReadyListHead[thread->Priority], &thread->ReadyListEntry);
            prcb->ReadySummary |= 1u << thread->Priority;
            KiSwapThread();
        }
        KiReleaseDispatcherLock(s);
        return STATUS_SUCCESS;
    }
    NTSTATUS status = KiWaitObjects(0, NULL, WaitAny, DelayExecution, mode, alertable, interval, NULL);
    return status == STATUS_TIMEOUT ? STATUS_SUCCESS : status;
}
