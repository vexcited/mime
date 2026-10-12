/*
 * critical sections, slim reader/writer locks, condition variables,
 * one-time initialization and interlocked singly linked lists.
 */
#include "ntdll_private.h"

/* ------------------------------------------------------------------ critical sections */

/*
 * LockCount is -1 when free and counts the owner plus waiters otherwise.
 * contended threads sleep on an auto-reset event created on first need.
 */
static HANDLE get_event(PRTL_CRITICAL_SECTION cs)
{
    HANDLE ev = __atomic_load_n(&cs->LockSemaphore, __ATOMIC_ACQUIRE);
    if (ev)
        return ev;
    HANDLE fresh;
    if (!NT_SUCCESS(NtCreateEvent(&fresh, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE)))
        RtlRaiseStatus(STATUS_NO_MEMORY);
    HANDLE expected = NULL;
    if (!__atomic_compare_exchange_n(&cs->LockSemaphore, &expected, fresh, FALSE, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE)) {
        NtClose(fresh);
        return expected;
    }
    return fresh;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlInitializeCriticalSectionEx(PRTL_CRITICAL_SECTION cs, ULONG spin, ULONG flags)
{
    cs->DebugInfo = NULL;
    cs->LockCount = -1;
    cs->RecursionCount = 0;
    cs->OwningThread = NULL;
    cs->LockSemaphore = NULL;
    cs->SpinCount = spin & 0x00FFFFFF;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlInitializeCriticalSectionAndSpinCount(PRTL_CRITICAL_SECTION cs, ULONG spin)
{
    return RtlInitializeCriticalSectionEx(cs, spin, 0);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION cs)
{
    return RtlInitializeCriticalSectionEx(cs, 0, 0);
}

NTDLL_EXPORT ULONG NTAPI RtlSetCriticalSectionSpinCount(PRTL_CRITICAL_SECTION cs, ULONG spin)
{
    ULONG old = (ULONG)cs->SpinCount;
    cs->SpinCount = spin;
    return old;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlDeleteCriticalSection(PRTL_CRITICAL_SECTION cs)
{
    if (cs->LockSemaphore)
        NtClose(cs->LockSemaphore);
    cs->LockSemaphore = NULL;
    cs->LockCount = -1;
    cs->RecursionCount = 0;
    cs->OwningThread = NULL;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlEnterCriticalSection(PRTL_CRITICAL_SECTION cs)
{
    HANDLE self = NtCurrentThreadId();
    if (cs->OwningThread == self) {
        cs->RecursionCount++;
        return STATUS_SUCCESS;
    }
    for (ULONG spin = (ULONG)cs->SpinCount; spin; spin--) {
        LONG expected = -1;
        if (__atomic_compare_exchange_n(&cs->LockCount, &expected, 0, FALSE, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            goto owned;
        __asm__ volatile("yield");
    }
    if (__atomic_add_fetch(&cs->LockCount, 1, __ATOMIC_ACQUIRE) != 0)
        NtWaitForSingleObject(get_event(cs), FALSE, NULL);
owned:
    cs->OwningThread = self;
    cs->RecursionCount = 1;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlTryEnterCriticalSection(PRTL_CRITICAL_SECTION cs)
{
    HANDLE self = NtCurrentThreadId();
    if (cs->OwningThread == self) {
        cs->RecursionCount++;
        return TRUE;
    }
    LONG expected = -1;
    if (!__atomic_compare_exchange_n(&cs->LockCount, &expected, 0, FALSE, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return FALSE;
    cs->OwningThread = self;
    cs->RecursionCount = 1;
    return TRUE;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlLeaveCriticalSection(PRTL_CRITICAL_SECTION cs)
{
    if (--cs->RecursionCount > 0)
        return STATUS_SUCCESS;
    cs->OwningThread = NULL;
    if (__atomic_sub_fetch(&cs->LockCount, 1, __ATOMIC_RELEASE) >= 0)
        NtSetEvent(get_event(cs), NULL);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT ULONG NTAPI RtlIsCriticalSectionLocked(PRTL_CRITICAL_SECTION cs)
{
    return cs->RecursionCount != 0;
}

NTDLL_EXPORT ULONG NTAPI RtlIsCriticalSectionLockedByThread(PRTL_CRITICAL_SECTION cs)
{
    return cs->OwningThread == NtCurrentThreadId() && cs->RecursionCount;
}

/* ------------------------------------------------------------------ srw locks */

/*
 * the word holds an exclusive bit and a reader count. contention backs off
 * by yielding the processor; the scheduler has a single cpu for now, so a
 * yield always lets the owner make progress.
 */
#define SRW_EXCLUSIVE ((ULONG_PTR)1)
#define SRW_READER ((ULONG_PTR)2)

static void srw_backoff(ULONG *spins)
{
    if (++*spins < 16) {
        __asm__ volatile("yield");
        return;
    }
    NtYieldExecution();
    if (*spins > 64) {
        LARGE_INTEGER t = {.QuadPart = -10000}; /* 1 ms */
        NtDelayExecution(FALSE, &t);
    }
}

NTDLL_EXPORT void NTAPI RtlInitializeSRWLock(PRTL_SRWLOCK lock)
{
    lock->Ptr = NULL;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlTryAcquireSRWLockExclusive(PRTL_SRWLOCK lock)
{
    ULONG_PTR expected = 0;
    return __atomic_compare_exchange_n((ULONG_PTR *)&lock->Ptr, &expected, SRW_EXCLUSIVE, FALSE, __ATOMIC_ACQUIRE,
                                       __ATOMIC_RELAXED);
}

NTDLL_EXPORT void NTAPI RtlAcquireSRWLockExclusive(PRTL_SRWLOCK lock)
{
    ULONG spins = 0;
    while (!RtlTryAcquireSRWLockExclusive(lock))
        srw_backoff(&spins);
}

NTDLL_EXPORT void NTAPI RtlReleaseSRWLockExclusive(PRTL_SRWLOCK lock)
{
    __atomic_store_n((ULONG_PTR *)&lock->Ptr, 0, __ATOMIC_RELEASE);
}

NTDLL_EXPORT BOOLEAN NTAPI RtlTryAcquireSRWLockShared(PRTL_SRWLOCK lock)
{
    ULONG_PTR v = __atomic_load_n((ULONG_PTR *)&lock->Ptr, __ATOMIC_RELAXED);
    while (!(v & SRW_EXCLUSIVE)) {
        if (__atomic_compare_exchange_n((ULONG_PTR *)&lock->Ptr, &v, v + SRW_READER, TRUE, __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED))
            return TRUE;
    }
    return FALSE;
}

NTDLL_EXPORT void NTAPI RtlAcquireSRWLockShared(PRTL_SRWLOCK lock)
{
    ULONG spins = 0;
    while (!RtlTryAcquireSRWLockShared(lock))
        srw_backoff(&spins);
}

NTDLL_EXPORT void NTAPI RtlReleaseSRWLockShared(PRTL_SRWLOCK lock)
{
    __atomic_sub_fetch((ULONG_PTR *)&lock->Ptr, SRW_READER, __ATOMIC_RELEASE);
}

/* ------------------------------------------------------------------ condition variables */

/* a generation counter: waiters sleep until it moves past the value they saw. */
NTDLL_EXPORT void NTAPI RtlInitializeConditionVariable(PRTL_CONDITION_VARIABLE cv)
{
    cv->Ptr = NULL;
}

NTDLL_EXPORT void NTAPI RtlWakeConditionVariable(PRTL_CONDITION_VARIABLE cv)
{
    __atomic_add_fetch((ULONG_PTR *)&cv->Ptr, 1, __ATOMIC_RELEASE);
}

NTDLL_EXPORT void NTAPI RtlWakeAllConditionVariable(PRTL_CONDITION_VARIABLE cv)
{
    __atomic_add_fetch((ULONG_PTR *)&cv->Ptr, 1, __ATOMIC_RELEASE);
}

static NTSTATUS wait_generation(PRTL_CONDITION_VARIABLE cv, ULONG_PTR seen, const LARGE_INTEGER *timeout)
{
    LARGE_INTEGER start;
    NtQueryPerformanceCounter(&start, NULL);
    ULONG spins = 0;
    while (__atomic_load_n((ULONG_PTR *)&cv->Ptr, __ATOMIC_ACQUIRE) == seen) {
        if (timeout) {
            if (timeout->QuadPart == 0)
                return STATUS_TIMEOUT;
            LARGE_INTEGER now, freq;
            NtQueryPerformanceCounter(&now, &freq);
            LONGLONG elapsed = (now.QuadPart - start.QuadPart) * 10000000 / freq.QuadPart;
            LONGLONG limit = timeout->QuadPart < 0 ? -timeout->QuadPart : 0;
            if (elapsed >= limit)
                return STATUS_TIMEOUT;
        }
        srw_backoff(&spins);
    }
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlSleepConditionVariableCS(PRTL_CONDITION_VARIABLE cv, PRTL_CRITICAL_SECTION cs,
                                                       const LARGE_INTEGER *timeout)
{
    ULONG_PTR seen = __atomic_load_n((ULONG_PTR *)&cv->Ptr, __ATOMIC_ACQUIRE);
    LONG recursion = cs->RecursionCount;
    cs->RecursionCount = 1;
    RtlLeaveCriticalSection(cs);
    NTSTATUS status = wait_generation(cv, seen, timeout);
    RtlEnterCriticalSection(cs);
    cs->RecursionCount = recursion;
    return status;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlSleepConditionVariableSRW(PRTL_CONDITION_VARIABLE cv, PRTL_SRWLOCK lock,
                                                        const LARGE_INTEGER *timeout, ULONG flags)
{
    ULONG_PTR seen = __atomic_load_n((ULONG_PTR *)&cv->Ptr, __ATOMIC_ACQUIRE);
    BOOLEAN shared = flags & 1; /* CONDITION_VARIABLE_LOCKMODE_SHARED */
    if (shared)
        RtlReleaseSRWLockShared(lock);
    else
        RtlReleaseSRWLockExclusive(lock);
    NTSTATUS status = wait_generation(cv, seen, timeout);
    if (shared)
        RtlAcquireSRWLockShared(lock);
    else
        RtlAcquireSRWLockExclusive(lock);
    return status;
}

/* ------------------------------------------------------------------ run once */

#define RUN_ONCE_DONE ((ULONG_PTR)2)
#define RUN_ONCE_BUSY ((ULONG_PTR)1)
#define RTL_RUN_ONCE_CHECK_ONLY 1
#define RTL_RUN_ONCE_ASYNC 2

NTDLL_EXPORT void NTAPI RtlRunOnceInitialize(PRTL_RUN_ONCE once)
{
    once->Ptr = NULL;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlRunOnceBeginInitialize(PRTL_RUN_ONCE once, ULONG flags, PVOID *context)
{
    ULONG spins = 0;
    for (;;) {
        ULONG_PTR v = __atomic_load_n((ULONG_PTR *)&once->Ptr, __ATOMIC_ACQUIRE);
        if ((v & 3) == RUN_ONCE_DONE) {
            if (context)
                *context = (PVOID)(v & ~(ULONG_PTR)3);
            return STATUS_SUCCESS;
        }
        if (flags & RTL_RUN_ONCE_CHECK_ONLY)
            return STATUS_UNSUCCESSFUL;
        if (v == 0) {
            ULONG_PTR expected = 0;
            if (__atomic_compare_exchange_n((ULONG_PTR *)&once->Ptr, &expected, RUN_ONCE_BUSY, FALSE,
                                            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
                return STATUS_PENDING;
            continue;
        }
        srw_backoff(&spins);
    }
}

NTDLL_EXPORT NTSTATUS NTAPI RtlRunOnceComplete(PRTL_RUN_ONCE once, ULONG flags, PVOID context)
{
    if (flags & 4) { /* RTL_RUN_ONCE_INIT_FAILED */
        __atomic_store_n((ULONG_PTR *)&once->Ptr, 0, __ATOMIC_RELEASE);
        return STATUS_SUCCESS;
    }
    __atomic_store_n((ULONG_PTR *)&once->Ptr, ((ULONG_PTR)context & ~(ULONG_PTR)3) | RUN_ONCE_DONE,
                     __ATOMIC_RELEASE);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlRunOnceExecuteOnce(PRTL_RUN_ONCE once, PRTL_RUN_ONCE_INIT_FN fn, PVOID parameter,
                                                  PVOID *context)
{
    NTSTATUS status = RtlRunOnceBeginInitialize(once, 0, context);
    if (status != STATUS_PENDING)
        return status;
    PVOID ctx = NULL;
    if (!fn(once, parameter, &ctx)) {
        RtlRunOnceComplete(once, 4, NULL);
        return STATUS_UNSUCCESSFUL;
    }
    RtlRunOnceComplete(once, 0, ctx);
    if (context)
        *context = ctx;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ wait on address */

NTDLL_EXPORT NTSTATUS NTAPI RtlWaitOnAddress(const void *address, const void *compare, SIZE_T size,
                                             const LARGE_INTEGER *timeout)
{
    LARGE_INTEGER start, now, freq;
    NtQueryPerformanceCounter(&start, &freq);
    ULONG spins = 0;
    while (!memcmp(address, compare, size)) {
        if (timeout) {
            NtQueryPerformanceCounter(&now, NULL);
            LONGLONG elapsed = (now.QuadPart - start.QuadPart) * 10000000 / freq.QuadPart;
            if (timeout->QuadPart == 0 || (timeout->QuadPart < 0 && elapsed >= -timeout->QuadPart))
                return STATUS_TIMEOUT;
        }
        srw_backoff(&spins);
    }
    return STATUS_SUCCESS;
}

NTDLL_EXPORT void NTAPI RtlWakeAddressSingle(const void *address)
{
}

NTDLL_EXPORT void NTAPI RtlWakeAddressAll(const void *address)
{
}

/* ------------------------------------------------------------------ slists */

NTDLL_EXPORT void NTAPI RtlInitializeSListHead(PSLIST_HEADER head)
{
    head->Whole = 0;
}

static BOOLEAN cas_header(PSLIST_HEADER head, SLIST_HEADER *expected, SLIST_HEADER desired)
{
    return __atomic_compare_exchange_n(&head->Whole, &expected->Whole, desired.Whole, FALSE, __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE);
}

NTDLL_EXPORT PSLIST_ENTRY NTAPI RtlInterlockedPushEntrySList(PSLIST_HEADER head, PSLIST_ENTRY entry)
{
    SLIST_HEADER old, next;
    old.Whole = __atomic_load_n(&head->Whole, __ATOMIC_ACQUIRE);
    do {
        entry->Next = old.S.Next;
        next.S.Next = entry;
        next.S.DepthSequence = ((old.S.DepthSequence + 1) & 0xFFFF) | ((old.S.DepthSequence + 0x10000) & ~0xFFFFull);
    } while (!cas_header(head, &old, next));
    return old.S.Next;
}

NTDLL_EXPORT PSLIST_ENTRY NTAPI RtlInterlockedPopEntrySList(PSLIST_HEADER head)
{
    SLIST_HEADER old, next;
    old.Whole = __atomic_load_n(&head->Whole, __ATOMIC_ACQUIRE);
    do {
        if (!old.S.Next)
            return NULL;
        next.S.Next = old.S.Next->Next;
        next.S.DepthSequence = ((old.S.DepthSequence - 1) & 0xFFFF) | ((old.S.DepthSequence + 0x10000) & ~0xFFFFull);
    } while (!cas_header(head, &old, next));
    return old.S.Next;
}

NTDLL_EXPORT PSLIST_ENTRY NTAPI RtlInterlockedFlushSList(PSLIST_HEADER head)
{
    SLIST_HEADER old, next;
    old.Whole = __atomic_load_n(&head->Whole, __ATOMIC_ACQUIRE);
    do {
        if (!old.S.Next)
            return NULL;
        next.S.Next = NULL;
        next.S.DepthSequence = (old.S.DepthSequence + 0x10000) & ~0xFFFFull;
    } while (!cas_header(head, &old, next));
    return old.S.Next;
}

NTDLL_EXPORT USHORT NTAPI RtlQueryDepthSList(PSLIST_HEADER head)
{
    return (USHORT)(head->S.DepthSequence & 0xFFFF);
}

NTDLL_EXPORT PSLIST_ENTRY NTAPI RtlFirstEntrySList(const SLIST_HEADER *head)
{
    return head->S.Next;
}
