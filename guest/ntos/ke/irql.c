/*
 * irql, spin locks and fast mutexes.
 *
 * irql up to DISPATCH_LEVEL is a per processor software value that only
 * defers dispatching and dpcs. higher levels also mask interrupts.
 */
#include "../ntos.h"

KIRQL NTAPI KeGetCurrentIrql(void)
{
    return KiCurrentIrql();
}

/*
 * code below dispatch level can be preempted and moved to another processor, so the level is read and written with
 * interrupts off: otherwise a move between the two raises the processor the thread left.
 */
KIRQL NTAPI KfRaiseIrql(KIRQL irql)
{
    ULONG64 state = KiDisableInterrupts();
    PKPCR pcr = KeGetPcr();
    KIRQL old = pcr->Irql;
    if (irql < old)
        KeBugCheckEx(0x00000009, old, irql, 0, 0); /* IRQL_NOT_GREATER_OR_EQUAL. */
    pcr->Irql = irql;
    if (irql > DISPATCH_LEVEL && old <= DISPATCH_LEVEL)
        pcr->SavedInterruptState = state;
    else
        KiRestoreInterrupts(state);
    return old;
}

KIRQL NTAPI KeRaiseIrqlToDpcLevel(void)
{
    return KfRaiseIrql(DISPATCH_LEVEL);
}

void NTAPI KeLowerIrql(KIRQL irql)
{
    ULONG64 state = KiDisableInterrupts();
    PKPCR pcr = KeGetPcr();
    KIRQL old = pcr->Irql;
    if (irql > old)
        KeBugCheckEx(0x0000000A, old, irql, 0, 0); /* IRQL_NOT_LESS_OR_EQUAL. */
    pcr->Irql = irql;
    if (old > DISPATCH_LEVEL && irql <= DISPATCH_LEVEL)
        KiRestoreInterrupts(pcr->SavedInterruptState);
    else if (old <= DISPATCH_LEVEL)
        KiRestoreInterrupts(state);
    if (irql < DISPATCH_LEVEL && (pcr->CurrentPrcb->DpcPending || pcr->CurrentPrcb->DispatchPending) &&
        KiInterruptsEnabled())
        KiDispatchPendingWork();
}

/* spin locks: the kernel lock already excludes other processors, raising irql covers this one. */
KIRQL NTAPI KeAcquireSpinLockRaiseToDpc(PKSPIN_LOCK lock)
{
    KIRQL old = KfRaiseIrql(DISPATCH_LEVEL);
    *lock = 1;
    return old;
}

void NTAPI KeReleaseSpinLock(PKSPIN_LOCK lock, KIRQL irql)
{
    *lock = 0;
    KeLowerIrql(irql);
}

void NTAPI KeAcquireSpinLockAtDpcLevel(PKSPIN_LOCK lock)
{
    *lock = 1;
}

void NTAPI KeReleaseSpinLockFromDpcLevel(PKSPIN_LOCK lock)
{
    *lock = 0;
}

void NTAPI KeEnterCriticalRegion(void)
{
    KiCurrentThread()->KernelApcDisable--;
}

void NTAPI KeLeaveCriticalRegion(void)
{
    PKTHREAD t = KiCurrentThread();
    if (++t->KernelApcDisable == 0 && t->KernelApcPending && KeGetPcr()->Irql == PASSIVE_LEVEL)
        KiDeliverApcs(KernelMode, nullptr);
}

void NTAPI KeEnterGuardedRegion(void)
{
    KiCurrentThread()->SpecialApcDisable--;
}

void NTAPI KeLeaveGuardedRegion(void)
{
    PKTHREAD t = KiCurrentThread();
    if (++t->SpecialApcDisable == 0 && t->KernelApcPending && KeGetPcr()->Irql == PASSIVE_LEVEL)
        KiDeliverApcs(KernelMode, nullptr);
}

/* fast mutexes are a count backed by a synchronization event. */
void NTAPI ExInitializeFastMutex(PFAST_MUTEX mutex)
{
    mutex->Count = 1;
    mutex->Owner = nullptr;
    mutex->Contention = 0;
    KeInitializeEvent(&mutex->Event, SynchronizationEvent, FALSE);
}

void FASTCALL ExAcquireFastMutexUnsafe(PFAST_MUTEX mutex)
{
    PKTHREAD self = KiCurrentThread();
    if (mutex->Owner == self)
        KeBugCheckEx(0x000000E5, (ULONG_PTR)mutex, 0, 0, 0); /* recursive acquisition. */
    if (__atomic_sub_fetch(&mutex->Count, 1, __ATOMIC_ACQUIRE) != 0) {
        mutex->Contention++;
        KeWaitForSingleObject(&mutex->Event, WrFastMutex, KernelMode, FALSE, nullptr);
    }
    mutex->Owner = self;
}

void FASTCALL ExReleaseFastMutexUnsafe(PFAST_MUTEX mutex)
{
    mutex->Owner = nullptr;
    if (__atomic_add_fetch(&mutex->Count, 1, __ATOMIC_RELEASE) != 1)
        KeSetEvent(&mutex->Event, 0, FALSE);
}

void FASTCALL ExAcquireFastMutex(PFAST_MUTEX mutex)
{
    KeEnterCriticalRegion();
    ExAcquireFastMutexUnsafe(mutex);
}

void FASTCALL ExReleaseFastMutex(PFAST_MUTEX mutex)
{
    ExReleaseFastMutexUnsafe(mutex);
    KeLeaveCriticalRegion();
}

void NTAPI KeInitializeGuardedMutex(PKGUARDED_MUTEX mutex)
{
    ExInitializeFastMutex(mutex);
}

void NTAPI KeAcquireGuardedMutex(PKGUARDED_MUTEX mutex)
{
    KeEnterGuardedRegion();
    ExAcquireFastMutexUnsafe(mutex);
}

void NTAPI KeReleaseGuardedMutex(PKGUARDED_MUTEX mutex)
{
    ExReleaseFastMutexUnsafe(mutex);
    KeLeaveGuardedRegion();
}

/* push locks and eresources, exclusive only for now. */
void NTAPI ExInitializePushLock(PEX_PUSH_LOCK lock)
{
    lock->Value = 0;
}

void FASTCALL ExfAcquirePushLockExclusive(PEX_PUSH_LOCK lock)
{
    while (__atomic_exchange_n(&lock->Value, 1, __ATOMIC_ACQUIRE)) {
        LARGE_INTEGER t;
        t.QuadPart = -10000;
        KeDelayExecutionThread(KernelMode, FALSE, &t);
    }
}

void FASTCALL ExfReleasePushLockExclusive(PEX_PUSH_LOCK lock)
{
    __atomic_store_n(&lock->Value, 0, __ATOMIC_RELEASE);
}

void FASTCALL ExfAcquirePushLockShared(PEX_PUSH_LOCK lock)
{
    ExfAcquirePushLockExclusive(lock);
}

void FASTCALL ExfReleasePushLockShared(PEX_PUSH_LOCK lock)
{
    ExfReleasePushLockExclusive(lock);
}

NTSTATUS NTAPI ExInitializeResourceLite(PERESOURCE resource)
{
    memset(resource, 0, sizeof(*resource));
    resource->ExclusiveWaiters = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(KEVENT), KI_TAG('E', 'r', 'e', 's'));
    if (!resource->ExclusiveWaiters)
        return STATUS_INSUFFICIENT_RESOURCES;
    KeInitializeEvent(resource->ExclusiveWaiters, SynchronizationEvent, TRUE);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ExDeleteResourceLite(PERESOURCE resource)
{
    ExFreePool(resource->ExclusiveWaiters);
    resource->ExclusiveWaiters = nullptr;
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI ExAcquireResourceExclusiveLite(PERESOURCE resource, BOOLEAN wait)
{
    PKTHREAD self = KiCurrentThread();
    if (resource->Address == self) {
        resource->ActiveCount++;
        return TRUE;
    }
    if (!wait) {
        LARGE_INTEGER zero = {};
        if (KeWaitForSingleObject(resource->ExclusiveWaiters, Executive, KernelMode, FALSE, &zero) != STATUS_SUCCESS)
            return FALSE;
    } else {
        KeWaitForSingleObject(resource->ExclusiveWaiters, Executive, KernelMode, FALSE, nullptr);
    }
    resource->Address = self;
    resource->ActiveCount = 1;
    return TRUE;
}

BOOLEAN NTAPI ExAcquireResourceSharedLite(PERESOURCE resource, BOOLEAN wait)
{
    return ExAcquireResourceExclusiveLite(resource, wait);
}

void FASTCALL ExReleaseResourceLite(PERESOURCE resource)
{
    if (--resource->ActiveCount == 0) {
        resource->Address = nullptr;
        KeSetEvent(resource->ExclusiveWaiters, 0, FALSE);
    }
}
