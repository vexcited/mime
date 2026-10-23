/*
 * critical sections, slim reader/writer locks, condition variables,
 * one-time initialization and interlocked singly linked lists.
 */
#include "ntdll_private.h"

/* ------------------------------------------------------------------ critical sections */

/*
 * LockCount is -1 when free and counts the owner plus waiters otherwise.
 * contended threads spin a while, then sleep on an auto-reset event created
 * on first need. without a spin count, a section spins as long as windows'
 * heap and c library locks do: most holds are shorter than a kernel round
 * trip.
 */
#define DEFAULT_SPIN 4000
static HANDLE get_event(PRTL_CRITICAL_SECTION cs)
{
    HANDLE ev = __atomic_load_n(&cs->LockSemaphore, __ATOMIC_ACQUIRE);
    if (ev)
        return ev;
    HANDLE fresh;
    if (!NT_SUCCESS(NtCreateEvent(&fresh, EVENT_ALL_ACCESS, nullptr, SynchronizationEvent, FALSE)))
        RtlRaiseStatus(STATUS_NO_MEMORY);
    HANDLE expected = nullptr;
    if (!__atomic_compare_exchange_n(&cs->LockSemaphore, &expected, fresh, FALSE, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE)) {
        NtClose(fresh);
        return expected;
    }
    return fresh;
}

/*
 * like windows 11: DebugInfo is -1 unless the process asks for it with a flag or a global flag. the spin count
 * is 24 bits with the section flags above. a single processor never spins, otherwise the default is 2000.
 * crash reporters follow DebugInfo of the loader and PEB sections when it is a pointer and skip -1. a null there
 * showed up as an unreadable range at address 0 in every minidump.
 */
#define CS_DEFAULT_SPIN 0x20007D0
#define CS_NOT_A_POINTER ((PRTL_CRITICAL_SECTION_DEBUG)(LONG_PTR)-1)

static LIST_ENTRY g_cs_list = {&g_cs_list, &g_cs_list};
static RTL_SRWLOCK g_cs_list_lock;

/* linked in the process list. without memory the section simply gets none. */
static void add_debug_info(PRTL_CRITICAL_SECTION cs)
{
    PRTL_CRITICAL_SECTION_DEBUG debug = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, sizeof(*debug));
    if (!debug) {
        cs->SpinCount |= RTL_CRITICAL_SECTION_FLAG_NO_DEBUG_INFO;
        return;
    }
    debug->Type = (cs->SpinCount & RTL_CRITICAL_SECTION_FLAG_RESOURCE_TYPE) ? 1 : 0;
    debug->CriticalSection = cs;
    debug->Identifier = 0x4353;
    RtlAcquireSRWLockExclusive(&g_cs_list_lock);
    InsertTailList(&g_cs_list, &debug->ProcessLocksList);
    RtlReleaseSRWLockExclusive(&g_cs_list_lock);
    cs->DebugInfo = debug;
}

static void initialize(PRTL_CRITICAL_SECTION cs, ULONG_PTR spin_count)
{
    cs->LockCount = -1;
    cs->RecursionCount = 0;
    cs->OwningThread = nullptr;
    cs->LockSemaphore = nullptr;
    cs->SpinCount = NtCurrentPeb()->NumberOfProcessors <= 1 ? (spin_count & 0xFF000000) : spin_count;
    cs->DebugInfo = CS_NOT_A_POINTER;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlInitializeCriticalSectionEx(PRTL_CRITICAL_SECTION cs, ULONG spin, ULONG flags)
{
    if ((flags & 0xE0000000) || (flags & (RTL_CRITICAL_SECTION_FLAG_NO_DEBUG_INFO | RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO)) ==
                                    (RTL_CRITICAL_SECTION_FLAG_NO_DEBUG_INFO | RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO))
        return STATUS_INVALID_PARAMETER_3;
    if (spin & 0xFF000000)
        return STATUS_INVALID_PARAMETER_2;
    /* a statically initialized, all zero section is left as is. */
    if (flags & RTL_CRITICAL_SECTION_FLAG_STATIC_INIT)
        return STATUS_SUCCESS;
    ULONG_PTR count = (flags & RTL_CRITICAL_SECTION_FLAG_DYNAMIC_SPIN) || !spin ? CS_DEFAULT_SPIN : spin;
    initialize(cs, count | (flags & (RTL_CRITICAL_SECTION_FLAG_NO_DEBUG_INFO | RTL_CRITICAL_SECTION_FLAG_RESOURCE_TYPE)));
    if (flags & RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO)
        add_debug_info(cs);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlInitializeCriticalSectionAndSpinCount(PRTL_CRITICAL_SECTION cs, ULONG spin)
{
    initialize(cs, (spin & 0x00FFFFFF) ? (spin & 0x00FFFFFF) : CS_DEFAULT_SPIN);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION cs)
{
    initialize(cs, CS_DEFAULT_SPIN);
    return STATUS_SUCCESS;
}

/* only the low 24 bits change, the flags above stay. a single processor keeps only the flags. */
NTDLL_EXPORT ULONG NTAPI RtlSetCriticalSectionSpinCount(PRTL_CRITICAL_SECTION cs, ULONG spin)
{
    ULONG old = (ULONG)cs->SpinCount;
    ULONG updated = old ^ ((spin ^ old) & 0x00FFFFFF);
    cs->SpinCount = NtCurrentPeb()->NumberOfProcessors == 1 ? (updated & 0xFF000000) : updated;
    return old & 0x00FFFFFF;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlDeleteCriticalSection(PRTL_CRITICAL_SECTION cs)
{
    NTSTATUS status = STATUS_SUCCESS;
    if ((ULONG_PTR)cs->LockSemaphore - 1 <= (ULONG_PTR)-3)
        status = NtClose(cs->LockSemaphore);
    PRTL_CRITICAL_SECTION_DEBUG debug = cs->DebugInfo;
    if ((ULONG_PTR)debug - 1 <= (ULONG_PTR)-3) {
        RtlAcquireSRWLockExclusive(&g_cs_list_lock);
        if (debug->ProcessLocksList.Flink)
            RemoveEntryList(&debug->ProcessLocksList);
        RtlReleaseSRWLockExclusive(&g_cs_list_lock);
        /* a never initialized section has no heap block behind it. */
        if (!(cs->SpinCount & RTL_CRITICAL_SECTION_FLAG_STATIC_INIT))
            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, debug);
    }
    cs->DebugInfo = nullptr;
    cs->LockCount = 0;
    cs->RecursionCount = 0;
    cs->OwningThread = nullptr;
    cs->LockSemaphore = nullptr;
    cs->SpinCount = 0;
    return status;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlEnterCriticalSection(PRTL_CRITICAL_SECTION cs)
{
    HANDLE self = NtCurrentThreadId();
    if (cs->OwningThread == self) {
        cs->RecursionCount++;
        return STATUS_SUCCESS;
    }
    ULONG spin = (ULONG)cs->SpinCount & 0x00FFFFFF;
    if ((!spin || (cs->SpinCount & RTL_CRITICAL_SECTION_FLAG_DYNAMIC_SPIN)) && NtCurrentPeb()->NumberOfProcessors > 1)
        spin = DEFAULT_SPIN;
    for (; spin; spin--) {
        LONG expected = -1;
        if (__atomic_load_n(&cs->LockCount, __ATOMIC_RELAXED) == -1 &&
            __atomic_compare_exchange_n(&cs->LockCount, &expected, 0, FALSE, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            goto owned;
        YieldProcessor();
    }
    if (__atomic_add_fetch(&cs->LockCount, 1, __ATOMIC_ACQUIRE) != 0)
        NtWaitForSingleObject(get_event(cs), FALSE, nullptr);
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
    cs->OwningThread = nullptr;
    if (__atomic_sub_fetch(&cs->LockCount, 1, __ATOMIC_RELEASE) >= 0)
        NtSetEvent(get_event(cs), nullptr);
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

/* ------------------------------------------------------------------ wait on address */

/*
 * like windows, waiters park in a table keyed by address: a waker unlinks
 * them and alerts each by thread id, so only a wake that finds a waiter
 * enters the kernel. the kernel keeps an alert sent before its thread
 * sleeps, the extra wake it may cause later is allowed.
 */
typedef struct ADDRESS_WAITER {
    struct ADDRESS_WAITER *Next;
    const volatile void *Address;
    HANDLE Thread;
    volatile LONG Queued;
} ADDRESS_WAITER;

#define WAIT_BUCKETS 128

static struct {
    volatile LONG Lock;
    ADDRESS_WAITER *Head;
} __attribute__((aligned(64))) g_wait_buckets[WAIT_BUCKETS];

static ULONG bucket_of(const volatile void *address)
{
    return (ULONG)(((ULONG_PTR)address * 0x9E3779B97F4A7C15ull) >> 57) % WAIT_BUCKETS;
}

/* held for a few instructions, a preempted owner gets the processor back by yielding. */
static void bucket_lock(volatile LONG *lock)
{
    ULONG spins = 0;
    while (__atomic_exchange_n(lock, 1, __ATOMIC_SEQ_CST)) {
        while (__atomic_load_n(lock, __ATOMIC_RELAXED)) {
            if (++spins < 128 || !USER_SHARED_DATA->MimeReadyThreads)
                YieldProcessor();
            else
                NtYieldExecution();
        }
    }
}

static void bucket_unlock(volatile LONG *lock)
{
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}

static BOOLEAN same_value(const volatile void *address, const void *compare, SIZE_T size)
{
    switch (size) {
    case 1:
        return *(const volatile UCHAR *)address == *(const UCHAR *)compare;
    case 2:
        return *(const volatile USHORT *)address == *(const USHORT *)compare;
    case 4:
        return *(const volatile ULONG *)address == *(const ULONG *)compare;
    default:
        return *(const volatile ULONG64 *)address == *(const ULONG64 *)compare;
    }
}

NTDLL_EXPORT NTSTATUS NTAPI RtlWaitOnAddress(const void *address, const void *compare, SIZE_T size,
                                             const LARGE_INTEGER *timeout)
{
    if (size != 1 && size != 2 && size != 4 && size != 8)
        return STATUS_INVALID_PARAMETER;
    ADDRESS_WAITER w = {nullptr, address, NtCurrentThreadId(), 1};
    ULONG b = bucket_of(address);
    bucket_lock(&g_wait_buckets[b].Lock);
    /* queue before comparing: a waker stores the value, then looks for waiters. */
    w.Next = g_wait_buckets[b].Head;
    g_wait_buckets[b].Head = &w;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (!same_value(address, compare, size)) {
        g_wait_buckets[b].Head = w.Next;
        bucket_unlock(&g_wait_buckets[b].Lock);
        return STATUS_SUCCESS;
    }
    bucket_unlock(&g_wait_buckets[b].Lock);

    NTSTATUS status = STATUS_TIMEOUT;
    if (!timeout || timeout->QuadPart)
        status = NtWaitForAlertByThreadId((PVOID)address, (PLARGE_INTEGER)timeout);
    if (__atomic_load_n(&w.Queued, __ATOMIC_ACQUIRE)) {
        bucket_lock(&g_wait_buckets[b].Lock);
        for (ADDRESS_WAITER **p = &g_wait_buckets[b].Head; *p; p = &(*p)->Next) {
            if (*p == &w) {
                *p = w.Next;
                break;
            }
        }
        bucket_unlock(&g_wait_buckets[b].Lock);
    }
    return status == STATUS_TIMEOUT ? STATUS_TIMEOUT : STATUS_SUCCESS;
}

static void wake_address(const void *address, BOOLEAN all)
{
    ULONG b = bucket_of(address);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (!__atomic_load_n(&g_wait_buckets[b].Head, __ATOMIC_RELAXED))
        return;
    HANDLE threads[64];
    ULONG count;
    do {
        count = 0;
        bucket_lock(&g_wait_buckets[b].Lock);
        ADDRESS_WAITER **p = &g_wait_buckets[b].Head;
        while (*p && count < RTL_NUMBER_OF(threads)) {
            ADDRESS_WAITER *w = *p;
            if (w->Address != address) {
                p = &w->Next;
                continue;
            }
            *p = w->Next;
            threads[count++] = w->Thread;
            /* the waiter's stack may go as soon as it sees this. */
            __atomic_store_n(&w->Queued, 0, __ATOMIC_RELEASE);
            if (!all)
                break;
        }
        bucket_unlock(&g_wait_buckets[b].Lock);
        for (ULONG i = 0; i < count; i++)
            NtAlertThreadByThreadId(threads[i]);
    } while (all && count == RTL_NUMBER_OF(threads));
}

NTDLL_EXPORT void NTAPI RtlWakeAddressSingle(const void *address)
{
    wake_address(address, FALSE);
}

NTDLL_EXPORT void NTAPI RtlWakeAddressAll(const void *address)
{
    wake_address(address, TRUE);
}

/* ------------------------------------------------------------------ srw locks */

/*
 * the word holds an exclusive bit, a waiters bit and a reader count.
 * contention spins briefly, then sets the waiters bit and sleeps on the
 * word. releasing a lock with waiters wakes them all to compete again.
 */
#define SRW_EXCLUSIVE ((ULONG_PTR)1)
#define SRW_WAITING ((ULONG_PTR)2)
#define SRW_READER ((ULONG_PTR)4)
#define SRW_SPINS 64

/* sleeps until the word changes, as long as the busy bits still block the caller. */
static void srw_sleep(PRTL_SRWLOCK lock, ULONG_PTR busy)
{
    ULONG_PTR v = __atomic_load_n((ULONG_PTR *)&lock->Ptr, __ATOMIC_RELAXED);
    if (!(v & busy))
        return;
    if ((v & SRW_WAITING) ||
        __atomic_compare_exchange_n((ULONG_PTR *)&lock->Ptr, &v, v | SRW_WAITING, FALSE, __ATOMIC_RELAXED,
                                    __ATOMIC_RELAXED)) {
        v |= SRW_WAITING;
        RtlWaitOnAddress(&lock->Ptr, &v, sizeof(v), nullptr);
    }
}

NTDLL_EXPORT void NTAPI RtlInitializeSRWLock(PRTL_SRWLOCK lock)
{
    lock->Ptr = nullptr;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlTryAcquireSRWLockExclusive(PRTL_SRWLOCK lock)
{
    ULONG_PTR v = __atomic_load_n((ULONG_PTR *)&lock->Ptr, __ATOMIC_RELAXED);
    while (!(v & ~SRW_WAITING)) {
        if (__atomic_compare_exchange_n((ULONG_PTR *)&lock->Ptr, &v, v | SRW_EXCLUSIVE, TRUE, __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED))
            return TRUE;
    }
    return FALSE;
}

NTDLL_EXPORT void NTAPI RtlAcquireSRWLockExclusive(PRTL_SRWLOCK lock)
{
    for (ULONG spins = 0;; spins++) {
        if (RtlTryAcquireSRWLockExclusive(lock))
            return;
        if (spins < SRW_SPINS)
            YieldProcessor();
        else
            srw_sleep(lock, ~SRW_WAITING);
    }
}

NTDLL_EXPORT void NTAPI RtlReleaseSRWLockExclusive(PRTL_SRWLOCK lock)
{
    if (__atomic_exchange_n((ULONG_PTR *)&lock->Ptr, 0, __ATOMIC_RELEASE) & SRW_WAITING)
        RtlWakeAddressAll(&lock->Ptr);
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
    for (ULONG spins = 0;; spins++) {
        if (RtlTryAcquireSRWLockShared(lock))
            return;
        if (spins < SRW_SPINS)
            YieldProcessor();
        else
            srw_sleep(lock, SRW_EXCLUSIVE);
    }
}

NTDLL_EXPORT void NTAPI RtlReleaseSRWLockShared(PRTL_SRWLOCK lock)
{
    ULONG_PTR v = __atomic_sub_fetch((ULONG_PTR *)&lock->Ptr, SRW_READER, __ATOMIC_RELEASE);
    /* the last reader out hands the lock to the sleepers. */
    if (v == SRW_WAITING &&
        __atomic_compare_exchange_n((ULONG_PTR *)&lock->Ptr, &v, 0, FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
        RtlWakeAddressAll(&lock->Ptr);
}

/* ------------------------------------------------------------------ condition variables */

/* waiters sleep until the generation counter moves past the value they saw. */
NTDLL_EXPORT void NTAPI RtlInitializeConditionVariable(PRTL_CONDITION_VARIABLE cv)
{
    cv->Ptr = nullptr;
}

NTDLL_EXPORT void NTAPI RtlWakeConditionVariable(PRTL_CONDITION_VARIABLE cv)
{
    __atomic_add_fetch((ULONG_PTR *)&cv->Ptr, 1, __ATOMIC_RELEASE);
    RtlWakeAddressSingle(&cv->Ptr);
}

NTDLL_EXPORT void NTAPI RtlWakeAllConditionVariable(PRTL_CONDITION_VARIABLE cv)
{
    __atomic_add_fetch((ULONG_PTR *)&cv->Ptr, 1, __ATOMIC_RELEASE);
    RtlWakeAddressAll(&cv->Ptr);
}

/* condition variables may wake spuriously, so one wait is enough. */
static NTSTATUS wait_generation(PRTL_CONDITION_VARIABLE cv, ULONG_PTR seen, const LARGE_INTEGER *timeout)
{
    return RtlWaitOnAddress(&cv->Ptr, &seen, sizeof(seen), timeout);
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
    once->Ptr = nullptr;
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
        if (++spins < SRW_SPINS)
            YieldProcessor();
        else
            RtlWaitOnAddress(&once->Ptr, &v, sizeof(v), nullptr);
    }
}

NTDLL_EXPORT NTSTATUS NTAPI RtlRunOnceComplete(PRTL_RUN_ONCE once, ULONG flags, PVOID context)
{
    if (flags & 4) /* RTL_RUN_ONCE_INIT_FAILED */
        __atomic_store_n((ULONG_PTR *)&once->Ptr, 0, __ATOMIC_RELEASE);
    else
        __atomic_store_n((ULONG_PTR *)&once->Ptr, ((ULONG_PTR)context & ~(ULONG_PTR)3) | RUN_ONCE_DONE,
                         __ATOMIC_RELEASE);
    RtlWakeAddressAll(&once->Ptr);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlRunOnceExecuteOnce(PRTL_RUN_ONCE once, PRTL_RUN_ONCE_INIT_FN fn, PVOID parameter,
                                                  PVOID *context)
{
    NTSTATUS status = RtlRunOnceBeginInitialize(once, 0, context);
    if (status != STATUS_PENDING)
        return status;
    PVOID ctx = nullptr;
    if (!fn(once, parameter, &ctx)) {
        RtlRunOnceComplete(once, 4, nullptr);
        return STATUS_UNSUCCESSFUL;
    }
    RtlRunOnceComplete(once, 0, ctx);
    if (context)
        *context = ctx;
    return STATUS_SUCCESS;
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
            return nullptr;
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
            return nullptr;
        next.S.Next = nullptr;
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
