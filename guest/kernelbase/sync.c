/* synchronization: kernel objects, waits, and the user mode primitives ntdll implements. */
#include "kernelbase_private.h"
#include <mime/ntrtlsec.h>

/* ------------------------------------------------------------------ helpers */

static HANDLE finish_create(NTSTATUS status, HANDLE handle)
{
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return nullptr;
    }
    KbSetError(status == STATUS_OBJECT_NAME_EXISTS ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return handle;
}

/* takes the handle by address because the open call must run before it is read. */
static HANDLE finish_open(NTSTATUS status, const HANDLE *handle)
{
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return nullptr;
    }
    return *handle;
}

static BOOL prepare_open(POBJECT_ATTRIBUTES oa, PUNICODE_STRING us, LPCWSTR name, BOOL inherit)
{
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return KbNamedObjectAttributes(oa, us, name, inherit, nullptr);
}

static BOOL prepare_create(POBJECT_ATTRIBUTES oa, PUNICODE_STRING us, LPCWSTR name, const SECURITY_ATTRIBUTES *sa)
{
    if (!KbNamedObjectAttributes(oa, us, name, FALSE, sa))
        return FALSE;
    if (name)
        oa->Attributes |= OBJ_OPENIF;
    return TRUE;
}

/* ansi object names use the ansi code page, not the file api one. */
#define WITH_WIDE_NAME(name, expr)                                                                                \
    do {                                                                                                          \
        PWSTR wname_ = nullptr;                                                                                      \
        if (name) {                                                                                               \
            wname_ = KbAnsiToWideCp(CP_ACP, name);                                                                \
            if (!wname_)                                                                                          \
                return nullptr;                                                                                      \
        }                                                                                                         \
        HANDLE result_ = (expr);                                                                                  \
        KbFree(wname_);                                                                                           \
        return result_;                                                                                           \
    } while (0)

/* ------------------------------------------------------------------ events */

HANDLE WINAPI CreateEventExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access)
{
    if (flags & ~(DWORD)(CREATE_EVENT_MANUAL_RESET | CREATE_EVENT_INITIAL_SET)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    if (!prepare_create(&oa, &us, name, sa))
        return nullptr;
    HANDLE h = nullptr;
    NTSTATUS status = NtCreateEvent(&h, access, &oa,
                                    (flags & CREATE_EVENT_MANUAL_RESET) ? NotificationEvent : SynchronizationEvent,
                                    (flags & CREATE_EVENT_INITIAL_SET) != 0);
    return finish_create(status, h);
}

HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCWSTR name)
{
    DWORD flags = (manual ? CREATE_EVENT_MANUAL_RESET : 0) | (initial ? CREATE_EVENT_INITIAL_SET : 0);
    return CreateEventExW(sa, name, flags, EVENT_ALL_ACCESS);
}

HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCSTR name)
{
    WITH_WIDE_NAME(name, CreateEventW(sa, manual, initial, wname_));
}

HANDLE WINAPI CreateEventExA(LPSECURITY_ATTRIBUTES sa, LPCSTR name, DWORD flags, DWORD access)
{
    WITH_WIDE_NAME(name, CreateEventExW(sa, wname_, flags, access));
}

HANDLE WINAPI OpenEventW(DWORD access, BOOL inherit, LPCWSTR name)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    if (!prepare_open(&oa, &us, name, inherit))
        return nullptr;
    HANDLE h = nullptr;
    return finish_open(NtOpenEvent(&h, access, &oa), &h);
}

HANDLE WINAPI OpenEventA(DWORD access, BOOL inherit, LPCSTR name)
{
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    WITH_WIDE_NAME(name, OpenEventW(access, inherit, wname_));
}

BOOL WINAPI SetEvent(HANDLE event)
{
    NTSTATUS status = NtSetEvent(event, nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI ResetEvent(HANDLE event)
{
    NTSTATUS status = NtResetEvent(event, nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI PulseEvent(HANDLE event)
{
    NTSTATUS status = NtPulseEvent(event, nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

/* ------------------------------------------------------------------ mutexes */

HANDLE WINAPI CreateMutexExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access)
{
    if (flags & ~(DWORD)CREATE_MUTEX_INITIAL_OWNER) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    if (!prepare_create(&oa, &us, name, sa))
        return nullptr;
    HANDLE h = nullptr;
    NTSTATUS status = NtCreateMutant(&h, access, &oa, (flags & CREATE_MUTEX_INITIAL_OWNER) != 0);
    return finish_create(status, h);
}

HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCWSTR name)
{
    return CreateMutexExW(sa, name, owner ? CREATE_MUTEX_INITIAL_OWNER : 0, MUTEX_ALL_ACCESS);
}

HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name)
{
    WITH_WIDE_NAME(name, CreateMutexW(sa, owner, wname_));
}

HANDLE WINAPI CreateMutexExA(LPSECURITY_ATTRIBUTES sa, LPCSTR name, DWORD flags, DWORD access)
{
    WITH_WIDE_NAME(name, CreateMutexExW(sa, wname_, flags, access));
}

HANDLE WINAPI OpenMutexW(DWORD access, BOOL inherit, LPCWSTR name)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    if (!prepare_open(&oa, &us, name, inherit))
        return nullptr;
    HANDLE h = nullptr;
    return finish_open(NtOpenMutant(&h, access, &oa), &h);
}

HANDLE WINAPI OpenMutexA(DWORD access, BOOL inherit, LPCSTR name)
{
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    WITH_WIDE_NAME(name, OpenMutexW(access, inherit, wname_));
}

BOOL WINAPI ReleaseMutex(HANDLE mutex)
{
    NTSTATUS status = NtReleaseMutant(mutex, nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

/* ------------------------------------------------------------------ semaphores */

HANDLE WINAPI CreateSemaphoreExW(LPSECURITY_ATTRIBUTES sa, LONG initial, LONG maximum, LPCWSTR name, DWORD flags,
                                 DWORD access)
{
    if (maximum <= 0 || initial < 0 || initial > maximum || flags) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    if (!prepare_create(&oa, &us, name, sa))
        return nullptr;
    HANDLE h = nullptr;
    NTSTATUS status = NtCreateSemaphore(&h, access, &oa, initial, maximum);
    return finish_create(status, h);
}

HANDLE WINAPI CreateSemaphoreW(LPSECURITY_ATTRIBUTES sa, LONG initial, LONG maximum, LPCWSTR name)
{
    return CreateSemaphoreExW(sa, initial, maximum, name, 0, SEMAPHORE_ALL_ACCESS);
}

HANDLE WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES sa, LONG initial, LONG maximum, LPCSTR name)
{
    WITH_WIDE_NAME(name, CreateSemaphoreW(sa, initial, maximum, wname_));
}

HANDLE WINAPI CreateSemaphoreExA(LPSECURITY_ATTRIBUTES sa, LONG initial, LONG maximum, LPCSTR name, DWORD flags,
                                 DWORD access)
{
    WITH_WIDE_NAME(name, CreateSemaphoreExW(sa, initial, maximum, wname_, flags, access));
}

HANDLE WINAPI OpenSemaphoreW(DWORD access, BOOL inherit, LPCWSTR name)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    if (!prepare_open(&oa, &us, name, inherit))
        return nullptr;
    HANDLE h = nullptr;
    return finish_open(NtOpenSemaphore(&h, access, &oa), &h);
}

HANDLE WINAPI OpenSemaphoreA(DWORD access, BOOL inherit, LPCSTR name)
{
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    WITH_WIDE_NAME(name, OpenSemaphoreW(access, inherit, wname_));
}

BOOL WINAPI ReleaseSemaphore(HANDLE semaphore, LONG count, LPLONG previous)
{
    if (count <= 0) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    NTSTATUS status = NtReleaseSemaphore(semaphore, count, previous);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

/* ------------------------------------------------------------------ waitable timers */

HANDLE WINAPI CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access)
{
    if (flags & ~(DWORD)(CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    if (!prepare_create(&oa, &us, name, sa))
        return nullptr;
    HANDLE h = nullptr;
    NTSTATUS status = NtCreateTimer(&h, access, &oa,
                                    (flags & CREATE_WAITABLE_TIMER_MANUAL_RESET) ? NotificationTimer : SynchronizationTimer);
    return finish_create(status, h);
}

HANDLE WINAPI CreateWaitableTimerExA(LPSECURITY_ATTRIBUTES sa, LPCSTR name, DWORD flags, DWORD access)
{
    PWSTR wide = name ? KbAnsiToWide(name) : nullptr;
    if (name && !wide)
        return nullptr;
    HANDLE h = CreateWaitableTimerExW(sa, wide, flags, access);
    KbFree(wide);
    return h;
}

HANDLE WINAPI CreateWaitableTimerW(LPSECURITY_ATTRIBUTES sa, BOOL manual, LPCWSTR name)
{
    return CreateWaitableTimerExW(sa, name, manual ? CREATE_WAITABLE_TIMER_MANUAL_RESET : 0, TIMER_ALL_ACCESS);
}

HANDLE WINAPI CreateWaitableTimerA(LPSECURITY_ATTRIBUTES sa, BOOL manual, LPCSTR name)
{
    WITH_WIDE_NAME(name, CreateWaitableTimerW(sa, manual, wname_));
}

HANDLE WINAPI OpenWaitableTimerW(DWORD access, BOOL inherit, LPCWSTR name)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    if (!prepare_open(&oa, &us, name, inherit))
        return nullptr;
    HANDLE h = nullptr;
    return finish_open(NtOpenTimer(&h, access, &oa), &h);
}

/* completion routines need kernel timer apcs, which NtSetTimer does not deliver yet. */
BOOL WINAPI SetWaitableTimerEx(HANDLE timer, const LARGE_INTEGER *due, LONG period, PTIMERAPCROUTINE completion,
                               LPVOID arg, PREASON_CONTEXT reason, ULONG tolerable_delay)
{
    if (!due || period < 0) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LARGE_INTEGER d = *due;
    NTSTATUS status = NtSetTimer(timer, &d, (PVOID)completion, arg, FALSE, period, nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI SetWaitableTimer(HANDLE timer, const LARGE_INTEGER *due, LONG period, PTIMERAPCROUTINE completion,
                             LPVOID arg, BOOL resume)
{
    if (!SetWaitableTimerEx(timer, due, period, completion, arg, nullptr, 0))
        return FALSE;
    /* windows reports resume support this way too. */
    if (resume)
        KbSetError(ERROR_NOT_SUPPORTED);
    return TRUE;
}

BOOL WINAPI CancelWaitableTimer(HANDLE timer)
{
    NTSTATUS status = NtCancelTimer(timer, nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

/* ------------------------------------------------------------------ waits */

static DWORD wait_result(NTSTATUS status)
{
    if (status == STATUS_TIMEOUT)
        return WAIT_TIMEOUT;
    if (status == STATUS_USER_APC || status == STATUS_ALERTED)
        return WAIT_IO_COMPLETION;
    if ((ULONG)status < MAXIMUM_WAIT_OBJECTS)
        return (DWORD)status;
    if ((ULONG)status >= (ULONG)STATUS_ABANDONED && (ULONG)status < (ULONG)STATUS_ABANDONED + MAXIMUM_WAIT_OBJECTS)
        return WAIT_ABANDONED_0 + ((ULONG)status - (ULONG)STATUS_ABANDONED);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return WAIT_FAILED;
    }
    return (DWORD)status;
}

DWORD WINAPI WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL alertable)
{
    /* the NUL device is always signaled. */
    if (KbIsPseudoHandle(handle))
        return WAIT_OBJECT_0;
    LARGE_INTEGER storage;
    PLARGE_INTEGER timeout = KbTimeout(&storage, milliseconds);
    NTSTATUS status;
    if (alertable) {
        /* a signaled object wins over pending apcs, like on windows. */
        LARGE_INTEGER zero = {.QuadPart = 0};
        status = NtWaitForSingleObject(handle, FALSE, &zero);
        if (status != STATUS_TIMEOUT)
            return wait_result(status);
        if (KbRunPendingApcs())
            return WAIT_IO_COMPLETION;
    }
    do {
        status = NtWaitForSingleObject(handle, (BOOLEAN)alertable, timeout);
    } while (status == STATUS_ALERTED && alertable && milliseconds == INFINITE);
    return wait_result(status);
}

DWORD WINAPI WaitForSingleObject(HANDLE handle, DWORD milliseconds)
{
    return WaitForSingleObjectEx(handle, milliseconds, FALSE);
}

DWORD WINAPI WaitForMultipleObjectsEx(DWORD count, const HANDLE *handles, BOOL all, DWORD milliseconds, BOOL alertable)
{
    if (!count || count > MAXIMUM_WAIT_OBJECTS || !handles) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }
    HANDLE copy[MAXIMUM_WAIT_OBJECTS];
    DWORD map[MAXIMUM_WAIT_OBJECTS];
    DWORD n = 0;
    for (DWORD i = 0; i < count; i++) {
        HANDLE h = handles[i];
        if (KbIsPseudoHandle(h)) {
            if (!all)
                return WAIT_OBJECT_0 + i;
            continue;
        }
        map[n] = i;
        copy[n++] = h;
    }
    if (!n)
        return WAIT_OBJECT_0;
    LARGE_INTEGER storage;
    PLARGE_INTEGER timeout = KbTimeout(&storage, milliseconds);
    NTSTATUS status = STATUS_TIMEOUT;
    if (alertable) {
        LARGE_INTEGER zero = {.QuadPart = 0};
        status = NtWaitForMultipleObjects(n, copy, all ? WaitAll : WaitAny, FALSE, &zero);
        if (status == STATUS_TIMEOUT && KbRunPendingApcs())
            return WAIT_IO_COMPLETION;
    }
    while (status == STATUS_TIMEOUT && (!alertable || timeout == nullptr || timeout->QuadPart != 0)) {
        status = NtWaitForMultipleObjects(n, copy, all ? WaitAll : WaitAny, (BOOLEAN)alertable, timeout);
        if (!(status == STATUS_ALERTED && alertable && milliseconds == INFINITE))
            break;
        status = STATUS_TIMEOUT;
    }
    DWORD r = wait_result(status);
    if (r < WAIT_OBJECT_0 + MAXIMUM_WAIT_OBJECTS && r - WAIT_OBJECT_0 < n)
        return WAIT_OBJECT_0 + map[r - WAIT_OBJECT_0];
    if (r >= WAIT_ABANDONED_0 && r < WAIT_ABANDONED_0 + n)
        return WAIT_ABANDONED_0 + map[r - WAIT_ABANDONED_0];
    return r;
}

DWORD WINAPI WaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL all, DWORD milliseconds)
{
    return WaitForMultipleObjectsEx(count, handles, all, milliseconds, FALSE);
}

DWORD WINAPI SignalObjectAndWait(HANDLE signal, HANDLE wait, DWORD milliseconds, BOOL alertable)
{
    LARGE_INTEGER storage;
    PLARGE_INTEGER timeout = KbTimeout(&storage, milliseconds);
    if (alertable && KbRunPendingApcs()) {
        /* the signal still happens, only the wait is cut short. */
        NTSTATUS status = NtSignalAndWaitForSingleObject(signal, wait, FALSE, &(LARGE_INTEGER){.QuadPart = 0});
        if (!NT_SUCCESS(status))
            return wait_result(status);
        return status == STATUS_TIMEOUT ? WAIT_IO_COMPLETION : wait_result(status);
    }
    NTSTATUS status = NtSignalAndWaitForSingleObject(signal, wait, (BOOLEAN)alertable, timeout);
    return wait_result(status);
}

/* ------------------------------------------------------------------ critical sections */

void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION cs)
{
    NTSTATUS status = RtlInitializeCriticalSection(cs);
    if (!NT_SUCCESS(status))
        RtlRaiseStatus(status);
}

BOOL WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD spin)
{
    NTSTATUS status = RtlInitializeCriticalSectionAndSpinCount(cs, spin);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI InitializeCriticalSectionEx(LPCRITICAL_SECTION cs, DWORD spin, DWORD flags)
{
    NTSTATUS status = RtlInitializeCriticalSectionEx(cs, spin, flags);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

DWORD WINAPI SetCriticalSectionSpinCount(LPCRITICAL_SECTION cs, DWORD spin)
{
    return RtlSetCriticalSectionSpinCount(cs, spin);
}

void WINAPI EnterCriticalSection(LPCRITICAL_SECTION cs)
{
    RtlEnterCriticalSection(cs);
}

BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION cs)
{
    return RtlTryEnterCriticalSection(cs);
}

void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION cs)
{
    RtlLeaveCriticalSection(cs);
}

void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION cs)
{
    RtlDeleteCriticalSection(cs);
}

/* ------------------------------------------------------------------ slim locks and condition variables */

void WINAPI InitializeSRWLock(PSRWLOCK lock)
{
    RtlInitializeSRWLock(lock);
}

void WINAPI AcquireSRWLockExclusive(PSRWLOCK lock)
{
    RtlAcquireSRWLockExclusive(lock);
}

void WINAPI AcquireSRWLockShared(PSRWLOCK lock)
{
    RtlAcquireSRWLockShared(lock);
}

void WINAPI ReleaseSRWLockExclusive(PSRWLOCK lock)
{
    RtlReleaseSRWLockExclusive(lock);
}

void WINAPI ReleaseSRWLockShared(PSRWLOCK lock)
{
    RtlReleaseSRWLockShared(lock);
}

BOOLEAN WINAPI TryAcquireSRWLockExclusive(PSRWLOCK lock)
{
    return RtlTryAcquireSRWLockExclusive(lock);
}

BOOLEAN WINAPI TryAcquireSRWLockShared(PSRWLOCK lock)
{
    return RtlTryAcquireSRWLockShared(lock);
}

void WINAPI InitializeConditionVariable(PCONDITION_VARIABLE cv)
{
    RtlInitializeConditionVariable(cv);
}

static BOOL sleep_result(NTSTATUS status)
{
    if (status == STATUS_TIMEOUT) {
        KbSetError(ERROR_TIMEOUT);
        return FALSE;
    }
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI SleepConditionVariableCS(PCONDITION_VARIABLE cv, PCRITICAL_SECTION cs, DWORD milliseconds)
{
    LARGE_INTEGER storage;
    return sleep_result(RtlSleepConditionVariableCS(cv, cs, KbTimeout(&storage, milliseconds)));
}

BOOL WINAPI SleepConditionVariableSRW(PCONDITION_VARIABLE cv, PSRWLOCK lock, DWORD milliseconds, ULONG flags)
{
    LARGE_INTEGER storage;
    return sleep_result(RtlSleepConditionVariableSRW(cv, lock, KbTimeout(&storage, milliseconds), flags));
}

void WINAPI WakeConditionVariable(PCONDITION_VARIABLE cv)
{
    RtlWakeConditionVariable(cv);
}

void WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE cv)
{
    RtlWakeAllConditionVariable(cv);
}

/* ------------------------------------------------------------------ one time initialization */

void WINAPI InitOnceInitialize(PINIT_ONCE once)
{
    RtlRunOnceInitialize(once);
}

BOOL WINAPI InitOnceExecuteOnce(PINIT_ONCE once, PINIT_ONCE_FN fn, PVOID parameter, LPVOID *context)
{
    return NT_SUCCESS(RtlRunOnceExecuteOnce(once, (PRTL_RUN_ONCE_INIT_FN)fn, parameter, context));
}

BOOL WINAPI InitOnceBeginInitialize(LPINIT_ONCE once, DWORD flags, PBOOL pending, LPVOID *context)
{
    if (!pending || (flags & ~(DWORD)(INIT_ONCE_CHECK_ONLY | INIT_ONCE_ASYNC))) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    NTSTATUS status = RtlRunOnceBeginInitialize(once, flags, context);
    if (status == STATUS_PENDING) {
        *pending = TRUE;
        return TRUE;
    }
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *pending = FALSE;
    return TRUE;
}

BOOL WINAPI InitOnceComplete(LPINIT_ONCE once, DWORD flags, LPVOID context)
{
    if ((flags & INIT_ONCE_INIT_FAILED) && context) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    NTSTATUS status = RtlRunOnceComplete(once, flags, context);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

/* ------------------------------------------------------------------ wait on address */

BOOL WINAPI WaitOnAddress(volatile void *address, PVOID compare, SIZE_T size, DWORD milliseconds)
{
    if (size != 1 && size != 2 && size != 4 && size != 8) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LARGE_INTEGER storage;
    NTSTATUS status = RtlWaitOnAddress((const void *)address, compare, size, KbTimeout(&storage, milliseconds));
    return sleep_result(status);
}

void WINAPI WakeByAddressSingle(PVOID address)
{
    RtlWakeAddressSingle(address);
}

void WINAPI WakeByAddressAll(PVOID address)
{
    RtlWakeAddressAll(address);
}

/* ------------------------------------------------------------------ synchronization barriers */

typedef struct BARRIER {
    volatile LONG Total;
    volatile LONG Remaining;
    volatile ULONG_PTR Generation;
    ULONG_PTR Unused;
    LONG Spin;
    LONG Pad;
} BARRIER;
MIME_STATIC_ASSERT(sizeof(BARRIER) <= sizeof(SYNCHRONIZATION_BARRIER), "BARRIER");

BOOL WINAPI InitializeSynchronizationBarrier(LPSYNCHRONIZATION_BARRIER barrier, LONG total, LONG spin)
{
    if (!barrier || total < 1) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BARRIER *b = (BARRIER *)barrier;
    b->Total = total;
    b->Remaining = total;
    b->Generation = 0;
    b->Unused = 0;
    b->Spin = spin < 0 ? 2000 : spin;
    b->Pad = 0;
    return TRUE;
}

BOOL WINAPI EnterSynchronizationBarrier(LPSYNCHRONIZATION_BARRIER barrier, DWORD flags)
{
    BARRIER *b = (BARRIER *)barrier;
    ULONG_PTR generation = __atomic_load_n(&b->Generation, __ATOMIC_ACQUIRE);
    if (__atomic_sub_fetch(&b->Remaining, 1, __ATOMIC_ACQ_REL) == 0) {
        __atomic_store_n(&b->Remaining, b->Total, __ATOMIC_RELAXED);
        __atomic_add_fetch(&b->Generation, 1, __ATOMIC_RELEASE);
        return TRUE;
    }
    LONG spins = (flags & SYNCHRONIZATION_BARRIER_FLAGS_BLOCK_ONLY) ? 0 : b->Spin;
    while (__atomic_load_n(&b->Generation, __ATOMIC_ACQUIRE) == generation) {
        if (spins > 0) {
            spins--;
            YieldProcessor();
        } else {
            NtYieldExecution();
        }
    }
    return FALSE;
}

BOOL WINAPI DeleteSynchronizationBarrier(LPSYNCHRONIZATION_BARRIER barrier)
{
    return TRUE;
}

/* ------------------------------------------------------------------ interlocked lists */

void WINAPI InitializeSListHead(PSLIST_HEADER head)
{
    RtlInitializeSListHead(head);
}

PSLIST_ENTRY WINAPI InterlockedPushEntrySList(PSLIST_HEADER head, PSLIST_ENTRY entry)
{
    return RtlInterlockedPushEntrySList(head, entry);
}

PSLIST_ENTRY WINAPI InterlockedPushListSListEx(PSLIST_HEADER head, PSLIST_ENTRY first, PSLIST_ENTRY last, ULONG count)
{
    /* the entries are already chained, so one exchange splices them all in. */
    SLIST_HEADER old, next;
    old.Whole = __atomic_load_n(&head->Whole, __ATOMIC_ACQUIRE);
    do {
        last->Next = old.S.Next;
        next.S.Next = first;
        next.S.DepthSequence =
            ((old.S.DepthSequence + count) & 0xFFFF) | ((old.S.DepthSequence + 0x10000) & ~0xFFFFull);
    } while (!__atomic_compare_exchange_n(&head->Whole, &old.Whole, next.Whole, FALSE, __ATOMIC_ACQ_REL,
                                          __ATOMIC_ACQUIRE));
    return old.S.Next;
}

PSLIST_ENTRY WINAPI InterlockedPopEntrySList(PSLIST_HEADER head)
{
    return RtlInterlockedPopEntrySList(head);
}

PSLIST_ENTRY WINAPI InterlockedFlushSList(PSLIST_HEADER head)
{
    return RtlInterlockedFlushSList(head);
}

USHORT WINAPI QueryDepthSList(PSLIST_HEADER head)
{
    return RtlQueryDepthSList(head);
}

#ifdef __x86_64__
/* the 32-bit form is fastcall on windows and is not exported here. */
PSLIST_ENTRY WINAPI InterlockedPushListSList(PSLIST_HEADER head, PSLIST_ENTRY first, PSLIST_ENTRY last, ULONG count)
{
    return InterlockedPushListSListEx(head, first, last, count);
}
#endif

/*
 * kernelbase also exports the operations compilers inline. the header already uses these names for the inline
 * versions, so the functions get other names here and take the export symbols as asm labels.
 */
#ifdef __i386__
#define EXPORT_LABEL(name, bytes) "_" name "@" #bytes
#else
#define EXPORT_LABEL(name, bytes) name
#endif

DECLSPEC_EXPORT LONG WINAPI KbInterlockedIncrement(LONG volatile *p) __asm__(EXPORT_LABEL("InterlockedIncrement", 4));
DECLSPEC_EXPORT LONG WINAPI KbInterlockedDecrement(LONG volatile *p) __asm__(EXPORT_LABEL("InterlockedDecrement", 4));
DECLSPEC_EXPORT LONG WINAPI KbInterlockedExchange(LONG volatile *p, LONG value)
    __asm__(EXPORT_LABEL("InterlockedExchange", 8));
DECLSPEC_EXPORT LONG WINAPI KbInterlockedExchangeAdd(LONG volatile *p, LONG value)
    __asm__(EXPORT_LABEL("InterlockedExchangeAdd", 8));
DECLSPEC_EXPORT LONG WINAPI KbInterlockedCompareExchange(LONG volatile *p, LONG exchange, LONG comparand)
    __asm__(EXPORT_LABEL("InterlockedCompareExchange", 12));
DECLSPEC_EXPORT LONG64 WINAPI KbInterlockedCompareExchange64(LONG64 volatile *p, LONG64 exchange, LONG64 comparand)
    __asm__(EXPORT_LABEL("InterlockedCompareExchange64", 20));

LONG WINAPI KbInterlockedIncrement(LONG volatile *p)
{
    return InterlockedIncrement(p);
}

LONG WINAPI KbInterlockedDecrement(LONG volatile *p)
{
    return InterlockedDecrement(p);
}

LONG WINAPI KbInterlockedExchange(LONG volatile *p, LONG value)
{
    return InterlockedExchange(p, value);
}

LONG WINAPI KbInterlockedExchangeAdd(LONG volatile *p, LONG value)
{
    return InterlockedExchangeAdd(p, value);
}

LONG WINAPI KbInterlockedCompareExchange(LONG volatile *p, LONG exchange, LONG comparand)
{
    return InterlockedCompareExchange(p, exchange, comparand);
}

LONG64 WINAPI KbInterlockedCompareExchange64(LONG64 volatile *p, LONG64 exchange, LONG64 comparand)
{
    __atomic_compare_exchange_n(p, &comparand, exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}

/* ------------------------------------------------------------------ private namespaces */

/*
 * a boundary descriptor is a name plus the sids a token must hold. a private namespace is a kernel directory named
 * after the descriptor, with an alias chosen by the creator: object names starting with the alias and a backslash are
 * looked up in that directory. the directory lives in the session's base named objects for as long as a handle to it
 * is open. the sids are part of the directory name, so other sids give another namespace, but token membership is not
 * checked. windows would probably fail with ERROR_ACCESS_DENIED for a group the token is not in.
 */
#define KB_BOUNDARY_MAGIC 0x444E4F42u /* BOND */
#define KB_NAMESPACE_ALIASES 32

typedef struct KB_BOUNDARY {
    ULONG Magic, Flags, SidCount, Capacity;
    PSID *Sids;
    WCHAR Name[1];
} KB_BOUNDARY;

typedef struct KB_NAMESPACE_ALIAS {
    WCHAR Alias[64];
    HANDLE Directory;
} KB_NAMESPACE_ALIAS;

static SRWLOCK g_namespace_lock = SRWLOCK_INIT;
static KB_NAMESPACE_ALIAS g_namespaces[KB_NAMESPACE_ALIASES];

HANDLE KbPrivateNamespaceLookup(LPCWSTR *name)
{
    LPCWSTR slash = wcschr(*name, L'\\');
    if (!slash || slash == *name)
        return nullptr;
    SIZE_T length = (SIZE_T)(slash - *name);
    HANDLE found = nullptr;
    AcquireSRWLockShared(&g_namespace_lock);
    for (ULONG i = 0; i < KB_NAMESPACE_ALIASES && !found; i++) {
        if (g_namespaces[i].Directory && wcslen(g_namespaces[i].Alias) == length &&
            !_wcsnicmp(g_namespaces[i].Alias, *name, length))
            found = g_namespaces[i].Directory;
    }
    ReleaseSRWLockShared(&g_namespace_lock);
    if (found)
        *name = slash + 1;
    return found;
}

HANDLE WINAPI CreateBoundaryDescriptorW(LPCWSTR name, ULONG flags)
{
    if (flags & ~CREATE_BOUNDARY_DESCRIPTOR_ADD_APPCONTAINER_SID)
        return nullptr;
    if (!name || !*name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    SIZE_T length = wcslen(name);
    KB_BOUNDARY *boundary = KbAllocZero(sizeof(*boundary) + length * sizeof(WCHAR));
    if (!boundary) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    boundary->Magic = KB_BOUNDARY_MAGIC;
    boundary->Flags = flags;
    wcscpy(boundary->Name, name);
    return boundary;
}

HANDLE WINAPI CreateBoundaryDescriptorA(LPCSTR name, ULONG flags)
{
    PWSTR wide = name ? KbAnsiToWide(name) : nullptr;
    if (!wide) {
        KbSetError(name ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    HANDLE boundary = CreateBoundaryDescriptorW(wide, flags);
    KbFree(wide);
    return boundary;
}

static KB_BOUNDARY *boundary_of(HANDLE handle)
{
    KB_BOUNDARY *boundary = handle;
    return boundary && boundary->Magic == KB_BOUNDARY_MAGIC ? boundary : nullptr;
}

BOOL WINAPI AddSIDToBoundaryDescriptor(HANDLE *descriptor, PSID sid)
{
    KB_BOUNDARY *boundary = descriptor ? boundary_of(*descriptor) : nullptr;
    if (!boundary || !sid || !RtlValidSid(sid)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG length = RtlLengthSid(sid);
    PSID copy = KbAlloc(length);
    if (!copy) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memcpy(copy, sid, length);
    if (boundary->SidCount == boundary->Capacity) {
        ULONG capacity = boundary->Capacity ? boundary->Capacity * 2 : 4;
        PSID *grown = KbAlloc(capacity * sizeof(PSID));
        if (!grown) {
            KbFree(copy);
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        if (boundary->SidCount)
            memcpy(grown, boundary->Sids, boundary->SidCount * sizeof(PSID));
        KbFree(boundary->Sids);
        boundary->Sids = grown;
        boundary->Capacity = capacity;
    }
    boundary->Sids[boundary->SidCount++] = copy;
    return TRUE;
}

BOOL WINAPI AddIntegrityLabelToBoundaryDescriptor(HANDLE *descriptor, PSID label)
{
    return AddSIDToBoundaryDescriptor(descriptor, label);
}

VOID WINAPI DeleteBoundaryDescriptor(HANDLE descriptor)
{
    KB_BOUNDARY *boundary = boundary_of(descriptor);
    if (!boundary)
        return;
    for (ULONG i = 0; i < boundary->SidCount; i++)
        KbFree(boundary->Sids[i]);
    KbFree(boundary->Sids);
    boundary->Magic = 0;
    KbFree(boundary);
}

/* the descriptor name followed by its sid bytes, encoded in characters a name allows. */
static BOOL namespace_directory_name(KB_BOUNDARY *boundary, PSID package, WCHAR *out, SIZE_T capacity)
{
    SIZE_T length = 0;
    static const WCHAR prefix[] = L"PrivateNamespace.";
    static const WCHAR digits[] = L"0123456789ABCDEF";
    SIZE_T need = wcslen(prefix) + wcslen(boundary->Name) + 1;
    for (ULONG i = 0; i < boundary->SidCount; i++)
        need += 1 + 2 * (SIZE_T)RtlLengthSid(boundary->Sids[i]);
    if (package)
        need += 1 + 2 * (SIZE_T)RtlLengthSid(package);
    if (need > capacity)
        return FALSE;
    wcscpy(out, prefix);
    length = wcslen(out);
    for (const WCHAR *c = boundary->Name; *c; c++)
        out[length++] = *c == L'\\' ? L'/' : *c;
    for (ULONG i = 0; i < boundary->SidCount + (package != nullptr); i++) {
        out[length++] = L'.';
        PSID sid = i < boundary->SidCount ? boundary->Sids[i] : package;
        const UCHAR *bytes = sid;
        for (ULONG j = 0, n = RtlLengthSid(sid); j < n; j++) {
            out[length++] = digits[bytes[j] >> 4];
            out[length++] = digits[bytes[j] & 15];
        }
    }
    out[length] = 0;
    return TRUE;
}

/* capture the effective package at namespace use, leaving the caller's descriptor unchanged. */
static BOOL namespace_package(KB_BOUNDARY *boundary, BOOL create, PVOID buffer, ULONG size, PSID *added)
{
    *added = nullptr;
    BOOL append = (boundary->Flags & CREATE_BOUNDARY_DESCRIPTOR_ADD_APPCONTAINER_SID) != 0;
    if (!append && !create)
        return TRUE;
    ULONG required;
    NTSTATUS status = NtQueryInformationToken((HANDLE)(LONG_PTR)-6, 31, buffer, size, &required);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    PSID package = *(PSID *)buffer;
    ULONG package_count = 0;
    BOOL own_package = FALSE;
    for (ULONG i = 0; i < boundary->SidCount; i++) {
        PSID sid = boundary->Sids[i];
        if (package && RtlEqualSid(sid, package)) {
            if (append) {
                KbSetError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            own_package = TRUE;
        }
        if (create && RtlIsPackageSid(sid)) {
            package_count++;
            if (package && !RtlEqualSid(sid, package)) {
                KbSetError(ERROR_ACCESS_DENIED);
                return FALSE;
            }
        }
    }
    if (create) {
        if (package_count + (append && package != nullptr) > 1) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (package && !append && !own_package) {
            KbSetError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
    }
    if (append)
        *added = package;
    return TRUE;
}

static HANDLE open_namespace(HANDLE descriptor, LPCWSTR alias, BOOL create, const SECURITY_ATTRIBUTES *sa)
{
    KB_BOUNDARY *boundary = boundary_of(descriptor);
    if (!boundary || !alias || !*alias || wcslen(alias) >= RTL_NUMBER_OF(g_namespaces[0].Alias) || wcschr(alias, L'\\')) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    union {
        PVOID Align;
        UCHAR Bytes[sizeof(PVOID) + 68];
    } package_info;
    PSID package;
    if (!namespace_package(boundary, create, &package_info, sizeof(package_info), &package))
        return nullptr;
    WCHAR directory[512];
    if (!namespace_directory_name(boundary, package, directory, RTL_NUMBER_OF(directory))) {
        KbSetError(ERROR_FILENAME_EXCED_RANGE);
        return nullptr;
    }
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    RtlInitUnicodeString(&us, directory);
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, KbNamedObjectRoot(), sa ? sa->lpSecurityDescriptor : nullptr);
    if (!oa.RootDirectory) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return nullptr;
    }
    HANDLE handle = nullptr;
    ACCESS_MASK access = DIRECTORY_QUERY | DIRECTORY_TRAVERSE | DIRECTORY_CREATE_OBJECT | DIRECTORY_CREATE_SUBDIRECTORY |
                         DELETE;
    NTSTATUS status = create ? NtCreateDirectoryObject(&handle, access, &oa) : NtOpenDirectoryObject(&handle, access, &oa);
    if (create && status == STATUS_OBJECT_NAME_COLLISION) {
        KbSetError(ERROR_ALREADY_EXISTS);
        return nullptr;
    }
    if (!NT_SUCCESS(status)) {
        if (!create && (status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_PATH_NOT_FOUND))
            KbSetError(ERROR_FILE_NOT_FOUND);
        else
            KbSetStatus(status);
        return nullptr;
    }
    BOOL placed = FALSE, duplicate = FALSE;
    AcquireSRWLockExclusive(&g_namespace_lock);
    for (ULONG i = 0; i < KB_NAMESPACE_ALIASES; i++)
        duplicate |= g_namespaces[i].Directory && !_wcsicmp(g_namespaces[i].Alias, alias);
    for (ULONG i = 0; i < KB_NAMESPACE_ALIASES && !duplicate && !placed; i++) {
        if (g_namespaces[i].Directory)
            continue;
        wcscpy(g_namespaces[i].Alias, alias);
        g_namespaces[i].Directory = handle;
        placed = TRUE;
    }
    ReleaseSRWLockExclusive(&g_namespace_lock);
    if (!placed) {
        NtClose(handle);
        KbSetError(duplicate ? ERROR_ALREADY_EXISTS : ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    return handle;
}

HANDLE WINAPI CreatePrivateNamespaceW(LPSECURITY_ATTRIBUTES sa, LPVOID descriptor, LPCWSTR alias)
{
    return open_namespace(descriptor, alias, TRUE, sa);
}

HANDLE WINAPI OpenPrivateNamespaceW(LPVOID descriptor, LPCWSTR alias)
{
    return open_namespace(descriptor, alias, FALSE, nullptr);
}

HANDLE WINAPI CreatePrivateNamespaceA(LPSECURITY_ATTRIBUTES sa, LPVOID descriptor, LPCSTR alias)
{
    PWSTR wide = alias ? KbAnsiToWide(alias) : nullptr;
    if (!wide) {
        KbSetError(alias ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    HANDLE handle = CreatePrivateNamespaceW(sa, descriptor, wide);
    KbFree(wide);
    return handle;
}

HANDLE WINAPI OpenPrivateNamespaceA(LPVOID descriptor, LPCSTR alias)
{
    PWSTR wide = alias ? KbAnsiToWide(alias) : nullptr;
    if (!wide) {
        KbSetError(alias ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    HANDLE handle = OpenPrivateNamespaceW(descriptor, wide);
    KbFree(wide);
    return handle;
}

BOOLEAN WINAPI ClosePrivateNamespace(HANDLE handle, ULONG flags)
{
    if (flags & ~1u) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    AcquireSRWLockExclusive(&g_namespace_lock);
    for (ULONG i = 0; i < KB_NAMESPACE_ALIASES; i++)
        if (g_namespaces[i].Directory == handle)
            g_namespaces[i].Directory = nullptr;
    ReleaseSRWLockExclusive(&g_namespace_lock);
    NTSTATUS status = NtClose(handle);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}
