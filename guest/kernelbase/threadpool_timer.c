/* default timers and waits use one worker each and serialize their callbacks. */
#include "kernelbase_private.h"

/* what a callback asked to happen once it returns. */
typedef struct {
    HMODULE FreeLibrary;
} CALLBACK_INSTANCE;

static void finish_callback(CALLBACK_INSTANCE *instance)
{
    if (instance->FreeLibrary)
        FreeLibrary(instance->FreeLibrary);
}

DECLSPEC_EXPORT void WINAPI FreeLibraryWhenCallbackReturns(CALLBACK_INSTANCE *instance, HMODULE module)
{
    instance->FreeLibrary = module;
}

typedef void (WINAPI *TIMER_CALLBACK)(void *, void *, void *);
typedef struct {
    SRWLOCK Lock;
    CONDITION_VARIABLE Changed;
    TIMER_CALLBACK Callback;
    void *Context;
    ULONGLONG Due;
    DWORD Period;
    LONG Refs;
    BOOL Armed;
    BOOL Running;
    BOOL Closed;
} POOL_TIMER;

static void release_timer(POOL_TIMER *timer)
{
    if (!InterlockedDecrement(&timer->Refs))
        KbFree(timer);
}

static DWORD WINAPI timer_worker(void *parameter)
{
    POOL_TIMER *timer = parameter;
    AcquireSRWLockExclusive(&timer->Lock);
    while (!timer->Closed) {
        ULONGLONG now = GetTickCount64();
        if (!timer->Armed || now < timer->Due) {
            ULONGLONG delay = timer->Armed ? timer->Due - now : INFINITE;
            SleepConditionVariableSRW(&timer->Changed, &timer->Lock,
                delay >= INFINITE ? INFINITE - 1 : (DWORD)delay, 0);
            continue;
        }
        timer->Armed = timer->Period != 0;
        timer->Due = now + timer->Period;
        timer->Running = TRUE;
        ReleaseSRWLockExclusive(&timer->Lock);
        CALLBACK_INSTANCE instance = {0};
        timer->Callback(&instance, timer->Context, timer);
        finish_callback(&instance);
        AcquireSRWLockExclusive(&timer->Lock);
        timer->Running = FALSE;
        WakeAllConditionVariable(&timer->Changed);
    }
    ReleaseSRWLockExclusive(&timer->Lock);
    release_timer(timer);
    return 0;
}

DECLSPEC_EXPORT void *WINAPI CreateThreadpoolTimer(TIMER_CALLBACK callback, void *context, void *environment)
{
    if (!callback || environment) {
        SetLastError(environment ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
        return NULL;
    }
    POOL_TIMER *timer = KbAllocZero(sizeof(*timer));
    if (!timer) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    timer->Callback = callback;
    timer->Context = context;
    timer->Refs = 2;
    HANDLE thread = CreateThread(NULL, 0, timer_worker, timer, 0, NULL);
    if (!thread) {
        KbFree(timer);
        return NULL;
    }
    CloseHandle(thread);
    return timer;
}

DECLSPEC_EXPORT void WINAPI SetThreadpoolTimer(POOL_TIMER *timer, const FILETIME *due, DWORD period, DWORD window)
{
    AcquireSRWLockExclusive(&timer->Lock);
    timer->Armed = due != NULL;
    timer->Period = period;
    if (due) {
        LONGLONG ticks = (LONGLONG)(((ULONGLONG)due->dwHighDateTime << 32) | due->dwLowDateTime);
        ULONGLONG delay;
        if (ticks < 0) {
            delay = 0ull - (ULONGLONG)ticks;
        } else {
            FILETIME current;
            GetSystemTimeAsFileTime(&current);
            ULONGLONG now = ((ULONGLONG)current.dwHighDateTime << 32) | current.dwLowDateTime;
            delay = (ULONGLONG)ticks > now ? (ULONGLONG)ticks - now : 0;
        }
        timer->Due = GetTickCount64() + (delay + 9999) / 10000;
    }
    WakeAllConditionVariable(&timer->Changed);
    ReleaseSRWLockExclusive(&timer->Lock);
}

DECLSPEC_EXPORT BOOL WINAPI IsThreadpoolTimerSet(POOL_TIMER *timer)
{
    AcquireSRWLockShared(&timer->Lock);
    BOOL armed = timer->Armed;
    ReleaseSRWLockShared(&timer->Lock);
    return armed;
}

DECLSPEC_EXPORT void WINAPI WaitForThreadpoolTimerCallbacks(POOL_TIMER *timer, BOOL cancel_pending)
{
    AcquireSRWLockExclusive(&timer->Lock);
    while (timer->Running)
        SleepConditionVariableSRW(&timer->Changed, &timer->Lock, INFINITE, 0);
    ReleaseSRWLockExclusive(&timer->Lock);
}

DECLSPEC_EXPORT void WINAPI CloseThreadpoolTimer(POOL_TIMER *timer)
{
    AcquireSRWLockExclusive(&timer->Lock);
    timer->Closed = TRUE;
    timer->Armed = FALSE;
    WakeAllConditionVariable(&timer->Changed);
    ReleaseSRWLockExclusive(&timer->Lock);
    release_timer(timer);
}

typedef void (WINAPI *WAIT_CALLBACK)(void *, void *, void *, DWORD);
typedef struct {
    SRWLOCK Lock;
    CONDITION_VARIABLE Changed;
    HANDLE Wake; /* interrupts the worker when the wait is set again or closed. */
    WAIT_CALLBACK Callback;
    void *Context;
    HANDLE Object;
    ULONGLONG Due; /* 0 waits forever. */
    LONG Refs;
    BOOL Armed;
    BOOL Running;
    BOOL Closed;
} POOL_WAIT;

static void release_wait(POOL_WAIT *wait)
{
    if (!InterlockedDecrement(&wait->Refs)) {
        CloseHandle(wait->Wake);
        KbFree(wait);
    }
}

static DWORD WINAPI wait_worker(void *parameter)
{
    POOL_WAIT *wait = parameter;
    AcquireSRWLockExclusive(&wait->Lock);
    while (!wait->Closed) {
        if (!wait->Armed) {
            SleepConditionVariableSRW(&wait->Changed, &wait->Lock, INFINITE, 0);
            continue;
        }
        HANDLE objects[2] = {wait->Wake, wait->Object};
        ULONGLONG due = wait->Due, now = GetTickCount64();
        DWORD timeout = !due ? INFINITE : due > now ? (DWORD)(due - now < INFINITE ? due - now : INFINITE - 1) : 0;
        ReleaseSRWLockExclusive(&wait->Lock);
        DWORD result = WaitForMultipleObjects(2, objects, FALSE, timeout);
        AcquireSRWLockExclusive(&wait->Lock);
        if (result == WAIT_OBJECT_0 || !wait->Armed || wait->Closed || wait->Object != objects[1] || wait->Due != due)
            continue;
        if (result == WAIT_OBJECT_0 + 1)
            result = WAIT_OBJECT_0;
        else if (result == WAIT_ABANDONED_0 + 1)
            result = WAIT_ABANDONED_0;
        else if (result != WAIT_TIMEOUT)
            continue;
        /* a wait fires once, the caller sets it again for another round. */
        wait->Armed = FALSE;
        wait->Running = TRUE;
        ReleaseSRWLockExclusive(&wait->Lock);
        CALLBACK_INSTANCE instance = {0};
        wait->Callback(&instance, wait->Context, wait, result);
        finish_callback(&instance);
        AcquireSRWLockExclusive(&wait->Lock);
        wait->Running = FALSE;
        WakeAllConditionVariable(&wait->Changed);
    }
    ReleaseSRWLockExclusive(&wait->Lock);
    release_wait(wait);
    return 0;
}

DECLSPEC_EXPORT void *WINAPI CreateThreadpoolWait(WAIT_CALLBACK callback, void *context, void *environment)
{
    if (!callback || environment) {
        SetLastError(environment ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
        return NULL;
    }
    POOL_WAIT *wait = KbAllocZero(sizeof(*wait));
    if (!wait) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    wait->Wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!wait->Wake) {
        KbFree(wait);
        return NULL;
    }
    wait->Callback = callback;
    wait->Context = context;
    wait->Refs = 2;
    HANDLE thread = CreateThread(NULL, 0, wait_worker, wait, 0, NULL);
    if (!thread) {
        CloseHandle(wait->Wake);
        KbFree(wait);
        return NULL;
    }
    CloseHandle(thread);
    return wait;
}

DECLSPEC_EXPORT void WINAPI SetThreadpoolWait(POOL_WAIT *wait, HANDLE object, const FILETIME *timeout)
{
    AcquireSRWLockExclusive(&wait->Lock);
    wait->Armed = object != NULL;
    wait->Object = object;
    wait->Due = 0;
    if (object && timeout) {
        LONGLONG ticks = (LONGLONG)(((ULONGLONG)timeout->dwHighDateTime << 32) | timeout->dwLowDateTime);
        ULONGLONG delay;
        if (ticks < 0) {
            delay = 0ull - (ULONGLONG)ticks;
        } else {
            FILETIME current;
            GetSystemTimeAsFileTime(&current);
            ULONGLONG now = ((ULONGLONG)current.dwHighDateTime << 32) | current.dwLowDateTime;
            delay = (ULONGLONG)ticks > now ? (ULONGLONG)ticks - now : 0;
        }
        /* a due tick of 0 means forever, so an elapsed timeout rounds up to the next tick. */
        wait->Due = GetTickCount64() + (delay + 9999) / 10000;
        if (!wait->Due)
            wait->Due = 1;
    }
    SetEvent(wait->Wake);
    WakeAllConditionVariable(&wait->Changed);
    ReleaseSRWLockExclusive(&wait->Lock);
}

DECLSPEC_EXPORT void WINAPI WaitForThreadpoolWaitCallbacks(POOL_WAIT *wait, BOOL cancel_pending)
{
    AcquireSRWLockExclusive(&wait->Lock);
    if (cancel_pending)
        wait->Armed = FALSE;
    while (wait->Running)
        SleepConditionVariableSRW(&wait->Changed, &wait->Lock, INFINITE, 0);
    ReleaseSRWLockExclusive(&wait->Lock);
}

DECLSPEC_EXPORT void WINAPI CloseThreadpoolWait(POOL_WAIT *wait)
{
    AcquireSRWLockExclusive(&wait->Lock);
    wait->Closed = TRUE;
    wait->Armed = FALSE;
    SetEvent(wait->Wake);
    WakeAllConditionVariable(&wait->Changed);
    ReleaseSRWLockExclusive(&wait->Lock);
    release_wait(wait);
}
