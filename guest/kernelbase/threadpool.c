/*
 * the thread pool: worker threads running work, timer, wait and i/o callbacks from a queue. the legacy work item,
 * registered wait, timer queue and bound completion functions use the default pool.
 *
 * one lock guards everything and is never held while a callback runs. one thread sleeps until the nearest timer,
 * wait threads watch up to 63 objects each, and each pool has a thread draining its completion port.
 */
#include "kernelbase_private.h"

typedef struct TP_POOL TP_POOL;
typedef struct TP_OBJECT TP_OBJECT;
typedef struct TP_GROUP TP_GROUP;
typedef struct TP_INSTANCE TP_INSTANCE;

typedef void(WINAPI *TP_SIMPLE)(TP_INSTANCE *, void *);
typedef void(WINAPI *TP_WORK_CALLBACK)(TP_INSTANCE *, void *, void *);
typedef void(WINAPI *TP_WAIT_CALLBACK)(TP_INSTANCE *, void *, void *, DWORD);
typedef void(WINAPI *TP_IO_CALLBACK)(TP_INSTANCE *, void *, void *, ULONG, ULONG_PTR, void *);
typedef void(WINAPI *TP_CANCEL_CALLBACK)(void *, void *);
typedef void(WINAPI *TP_FINALIZATION)(TP_INSTANCE *, void *);
typedef void(WINAPI *TP_BOUND_CALLBACK)(DWORD, DWORD, LPOVERLAPPED);

/* TP_CALLBACK_ENVIRON, versions 1 and 3 share this layout. */
typedef struct KB_TP_ENV {
    DWORD Version;
    TP_POOL *Pool;
    TP_GROUP *CleanupGroup;
    TP_CANCEL_CALLBACK CancelCallback;
    void *RaceDll;
    void *ActivationContext;
    TP_FINALIZATION Finalization;
    DWORD Flags;
    DWORD Priority;
    DWORD Size;
} KB_TP_ENV;

typedef struct KB_TP_STACK {
    SIZE_T Reserve;
    SIZE_T Commit;
} KB_TP_STACK;

enum {
    TPK_SIMPLE = 1,
    TPK_WORK,
    TPK_TIMER,
    TPK_WAIT,
    TPK_IO,
    TPK_ITEM,
};

/* object flags on top of the legacy WT_ ones. */
#define TPF_LEGACY 0x80000000u
#define TPF_UNREGISTERED 0x40000000u
#define WT_EXECUTEINTIMERTHREAD_ 0x00000020u

#define IDLE_THREAD_MS 10000
#define STARVATION_MS 20
#define DEFAULT_MAXIMUM_THREADS 500

typedef struct TS_ENTRY TS_ENTRY;
struct TS_ENTRY {
    LIST_ENTRY Link;
    ULONGLONG Due; /* 100 ns units on the now_hns clock */
    void (*Fire)(TS_ENTRY *);
    BOOL Linked;
};

struct TP_GROUP {
    LIST_ENTRY Members;
};

struct TP_POOL {
    LONG Refs;
    LIST_ENTRY Queue;
    LONG Queued;
    CONDITION_VARIABLE Work;
    LONG Threads, Idle, Min, Max;
    BOOL Closed;
    KB_TP_STACK Stack;
    TS_ENTRY Monitor;
    HANDLE Port; /* shared by the pool's i/o objects */
};

typedef struct TIMER_QUEUE {
    LIST_ENTRY Timers;
    BOOL Deleted;
    HANDLE Completion;
} TIMER_QUEUE;

typedef struct WAIT_THREAD WAIT_THREAD;

struct TP_OBJECT {
    LONG Kind;
    LONG Refs;
    TP_POOL *Pool;
    TP_GROUP *Group;
    LIST_ENTRY GroupLink;
    void *Callback;
    void *Context;
    TP_FINALIZATION Finalization;
    TP_CANCEL_CALLBACK CancelCallback;
    LONG Pending; /* callbacks queued */
    LONG Running; /* callbacks running */
    BOOL Closed;
    ULONG Flags;
    HANDLE Completion; /* signaled when a deleted legacy object's callbacks are done */
    /* timers. */
    TS_ENTRY Timer;
    ULONG Period;
    BOOL Armed;
    /* waits. */
    HANDLE Handle;
    BOOL OwnsHandle;
    ULONGLONG WaitDue; /* 0 waits forever */
    DWORD Timeout;
    WAIT_THREAD *Home;
    ULONG Generation;
    /* i/o. */
    LONG Started;
    /* timer queue timers. */
    TIMER_QUEUE *Queue;
    LIST_ENTRY QueueLink;
};

struct TP_INSTANCE {
    TP_OBJECT *Object;
    TP_POOL *Pool;
    HANDLE Event;
    HANDLE Semaphore;
    LONG SemaphoreCount;
    HANDLE Mutex;
    CRITICAL_SECTION *Section;
    HMODULE Library;
    BOOL Disassociated;
};

typedef struct TP_JOB {
    LIST_ENTRY Link;
    TP_OBJECT *Object;
    ULONG_PTR Arg[3];
} TP_JOB;

struct WAIT_THREAD {
    LIST_ENTRY Link;
    HANDLE Control;
    LONG Count;
    TP_OBJECT *Waits[MAXIMUM_WAIT_OBJECTS - 1];
};

static SRWLOCK g_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE g_done = CONDITION_VARIABLE_INIT;    /* an object finished its callbacks */
static CONDITION_VARIABLE g_timer_changed = CONDITION_VARIABLE_INIT;
static LIST_ENTRY g_timers;
static LIST_ENTRY g_wait_threads;
static BOOL g_ready, g_timer_thread;
static DWORD g_current = TLS_OUT_OF_INDEXES; /* tls slot for the instance of the running callback */
static TP_POOL g_default;
static ULONGLONG g_frequency;

/* ------------------------------------------------------------------ the lock and the clock */

static void init_pool(TP_POOL *pool, LONG maximum)
{
    InitializeListHead(&pool->Queue);
    pool->Refs = 1;
    pool->Max = maximum;
}

static void enter(void)
{
    AcquireSRWLockExclusive(&g_lock);
    if (g_ready)
        return;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    g_frequency = (ULONGLONG)frequency.QuadPart;
    g_current = TlsAlloc();
    InitializeListHead(&g_timers);
    InitializeListHead(&g_wait_threads);
    init_pool(&g_default, DEFAULT_MAXIMUM_THREADS);
    g_default.Refs = 1 << 20;
    g_ready = TRUE;
}

static void leave(void)
{
    ReleaseSRWLockExclusive(&g_lock);
}

static ULONGLONG now_hns(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    ULONGLONG ticks = (ULONGLONG)counter.QuadPart;
    return ticks / g_frequency * 10000000ull + ticks % g_frequency * 10000000ull / g_frequency;
}

static DWORD milliseconds_until(ULONGLONG due, ULONGLONG now)
{
    if (due <= now)
        return 0;
    ULONGLONG ms = (due - now + 9999) / 10000;
    return ms >= INFINITE ? INFINITE - 1 : (DWORD)ms;
}

/* a negative time is relative, a positive one an absolute file time. returns 100 ns units from now. */
static ULONGLONG delay_of(LONGLONG ticks)
{
    if (ticks < 0)
        return (ULONGLONG)-ticks;
    FILETIME current;
    GetSystemTimeAsFileTime(&current);
    ULONGLONG now = ((ULONGLONG)current.dwHighDateTime << 32) | current.dwLowDateTime;
    return (ULONGLONG)ticks > now ? (ULONGLONG)ticks - now : 0;
}

/* ------------------------------------------------------------------ objects */

static void timer_unlink(TS_ENTRY *entry);

/* lock held. frees the pool once no reference or thread is left. */
static void check_pool(TP_POOL *pool)
{
    if (pool->Refs || pool->Threads)
        return;
    timer_unlink(&pool->Monitor);
    if (pool->Port)
        NtClose(pool->Port);
    KbFree(pool);
}

static void release_pool(TP_POOL *pool)
{
    pool->Refs--;
    check_pool(pool);
}

static void free_object(TP_OBJECT *object)
{
    if (object->OwnsHandle && object->Handle)
        NtClose(object->Handle);
    if (object->Group)
        RemoveEntryList(&object->GroupLink);
    TP_POOL *pool = object->Pool;
    KbFree(object);
    if (pool)
        release_pool(pool);
}

static void unref(TP_OBJECT *object)
{
    if (!--object->Refs)
        free_object(object);
}

static TP_OBJECT *new_object(LONG kind, void *callback, void *context, const KB_TP_ENV *env)
{
    TP_OBJECT *object = KbAllocZero(sizeof(*object));
    if (!object) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    object->Kind = kind;
    object->Refs = 1;
    object->Callback = callback;
    object->Context = context;
    enter();
    TP_POOL *pool = env && env->Pool ? env->Pool : &g_default;
    object->Pool = pool;
    pool->Refs++;
    if (env) {
        object->Finalization = env->Finalization;
        if (env->CleanupGroup) {
            object->Group = env->CleanupGroup;
            object->CancelCallback = env->CancelCallback;
            InsertTailList(&env->CleanupGroup->Members, &object->GroupLink);
        }
    }
    InitializeListHead(&object->Timer.Link);
    InitializeListHead(&object->QueueLink);
    leave();
    return object;
}

/* ------------------------------------------------------------------ workers */

static DWORD WINAPI worker_main(void *parameter);

/* lock held. */
static BOOL start_worker(TP_POOL *pool)
{
    pool->Threads++;
    HANDLE thread = CreateThread(nullptr, pool->Stack.Reserve, worker_main, pool,
                                 pool->Stack.Reserve ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0, nullptr);
    if (!thread) {
        pool->Threads--;
        return FALSE;
    }
    NtClose(thread);
    return TRUE;
}

static void timer_link(TS_ENTRY *entry);
static void start_timer_thread(void);

static void monitor_fire(TS_ENTRY *entry)
{
    TP_POOL *pool = CONTAINING_RECORD(entry, TP_POOL, Monitor);
    if (pool->Queued <= pool->Idle)
        return;
    /* every thread is busy with work waiting, so the callbacks probably block: add threads. */
    ULONG cpus = USER_SHARED_DATA->ActiveProcessorCount ? USER_SHARED_DATA->ActiveProcessorCount : 1;
    for (ULONG n = 0; n < cpus && pool->Queued > pool->Idle + (LONG)n && pool->Threads < pool->Max; n++)
        if (!start_worker(pool))
            break;
    if (pool->Queued > pool->Idle && pool->Threads < pool->Max) {
        entry->Due = now_hns() + STARVATION_MS * 10000ull;
        timer_link(entry);
    }
}

/* lock held. wakes or starts a worker, or lets the monitor start some later. */
static void ensure_worker(TP_POOL *pool)
{
    if (pool->Queued <= pool->Idle) {
        WakeConditionVariable(&pool->Work);
        return;
    }
    if (pool->Threads >= pool->Max)
        return;
    ULONG cpus = USER_SHARED_DATA->ActiveProcessorCount ? USER_SHARED_DATA->ActiveProcessorCount : 1;
    if ((ULONG)pool->Threads < cpus || pool->Threads < pool->Min) {
        start_worker(pool);
        return;
    }
    if (!pool->Monitor.Linked) {
        pool->Monitor.Fire = monitor_fire;
        pool->Monitor.Due = now_hns() + STARVATION_MS * 10000ull;
        timer_link(&pool->Monitor);
    }
}

/* lock held. */
static void enqueue(TP_OBJECT *object, ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2)
{
    TP_JOB *job = KbAlloc(sizeof(*job));
    if (!job)
        return;
    job->Object = object;
    job->Arg[0] = a0;
    job->Arg[1] = a1;
    job->Arg[2] = a2;
    object->Refs++;
    object->Pending++;
    TP_POOL *pool = object->Pool;
    InsertTailList(&pool->Queue, &job->Link);
    pool->Queued++;
    ensure_worker(pool);
}

/* lock held, takes over the caller's reference to the object. */
static void run_callback(TP_OBJECT *object, ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2)
{
    TP_INSTANCE instance = {.Object = object, .Pool = object->Pool};
    object->Running++;
    TP_INSTANCE *previous = TlsGetValue(g_current);
    TlsSetValue(g_current, &instance);
    ReleaseSRWLockExclusive(&g_lock);

    BOOL legacy = object->Flags & TPF_LEGACY;
    switch (object->Kind) {
    case TPK_SIMPLE:
        ((TP_SIMPLE)object->Callback)(&instance, object->Context);
        break;
    case TPK_ITEM:
        ((LPTHREAD_START_ROUTINE)object->Callback)(object->Context);
        break;
    case TPK_WORK:
        ((TP_WORK_CALLBACK)object->Callback)(&instance, object->Context, object);
        break;
    case TPK_TIMER:
        if (legacy)
            ((WAITORTIMERCALLBACK)object->Callback)(object->Context, TRUE);
        else
            ((TP_WORK_CALLBACK)object->Callback)(&instance, object->Context, object);
        break;
    case TPK_WAIT:
        if (legacy)
            ((WAITORTIMERCALLBACK)object->Callback)(object->Context, a0 == WAIT_TIMEOUT);
        else
            ((TP_WAIT_CALLBACK)object->Callback)(&instance, object->Context, object, (DWORD)a0);
        break;
    case TPK_IO:
        if (legacy)
            ((TP_BOUND_CALLBACK)object->Callback)((DWORD)a1, (DWORD)a2, (LPOVERLAPPED)a0);
        else
            ((TP_IO_CALLBACK)object->Callback)(&instance, object->Context, (void *)a0, (ULONG)a1, a2, object);
        break;
    }
    if (object->Finalization)
        object->Finalization(&instance, object->Context);

    /* the *WhenCallbackReturns actions. */
    if (instance.Section)
        LeaveCriticalSection(instance.Section);
    if (instance.Mutex)
        ReleaseMutex(instance.Mutex);
    if (instance.Semaphore)
        ReleaseSemaphore(instance.Semaphore, instance.SemaphoreCount, nullptr);
    if (instance.Event)
        SetEvent(instance.Event);
    if (instance.Library)
        FreeLibrary(instance.Library);

    AcquireSRWLockExclusive(&g_lock);
    TlsSetValue(g_current, previous);
    if (!instance.Disassociated)
        object->Running--;
}

static void attach_wait(TP_OBJECT *object);

/* lock held. bookkeeping after a callback ends, drops the job's reference. */
static void settle(TP_OBJECT *object)
{
    if (object->Kind == TPK_WAIT && (object->Flags & TPF_LEGACY) && !(object->Flags & (TPF_UNREGISTERED | WT_EXECUTEONLYONCE)) &&
        !object->Armed && !object->Closed) {
        object->WaitDue = object->Timeout == INFINITE ? 0 : now_hns() + (ULONGLONG)object->Timeout * 10000;
        attach_wait(object);
    }
    if (!object->Pending && !object->Running) {
        WakeAllConditionVariable(&g_done);
        if (object->Completion && (object->Closed || (object->Flags & TPF_UNREGISTERED))) {
            SetEvent(object->Completion);
            object->Completion = nullptr;
        }
    }
    unref(object);
}

static DWORD WINAPI worker_main(void *parameter)
{
    TP_POOL *pool = parameter;
    AcquireSRWLockExclusive(&g_lock);
    for (;;) {
        while (IsListEmpty(&pool->Queue)) {
            if (pool->Closed)
                goto done;
            pool->Idle++;
            BOOL woken = SleepConditionVariableSRW(&pool->Work, &g_lock, IDLE_THREAD_MS, 0);
            pool->Idle--;
            if (!woken && IsListEmpty(&pool->Queue) && pool->Threads > pool->Min)
                goto done;
        }
        TP_JOB *job = CONTAINING_RECORD(RemoveHeadList(&pool->Queue), TP_JOB, Link);
        pool->Queued--;
        TP_OBJECT *object = job->Object;
        ULONG_PTR a0 = job->Arg[0], a1 = job->Arg[1], a2 = job->Arg[2];
        KbFree(job);
        object->Pending--;
        run_callback(object, a0, a1, a2);
        settle(object);
    }
done:
    pool->Threads--;
    check_pool(pool);
    leave();
    return 0;
}

/* lock held. runs the callback on the thread that found it due, or queues it. */
static void dispatch(TP_OBJECT *object, ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2)
{
    if (object->Flags & (WT_EXECUTEINWAITTHREAD | WT_EXECUTEINTIMERTHREAD_)) {
        object->Refs++;
        run_callback(object, a0, a1, a2);
        settle(object);
        return;
    }
    enqueue(object, a0, a1, a2);
}

/* ------------------------------------------------------------------ pools */

DECLSPEC_EXPORT TP_POOL *WINAPI CreateThreadpool(void *reserved)
{
    TP_POOL *pool = KbAllocZero(sizeof(*pool));
    if (!pool) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    init_pool(pool, DEFAULT_MAXIMUM_THREADS);
    InitializeListHead(&pool->Monitor.Link);
    return pool;
}

DECLSPEC_EXPORT void WINAPI CloseThreadpool(TP_POOL *pool)
{
    enter();
    pool->Closed = TRUE;
    WakeAllConditionVariable(&pool->Work);
    release_pool(pool);
    leave();
}

DECLSPEC_EXPORT BOOL WINAPI SetThreadpoolThreadMinimum(TP_POOL *pool, DWORD minimum)
{
    enter();
    BOOL ok = TRUE;
    pool->Min = (LONG)minimum;
    if (pool->Max < pool->Min)
        pool->Max = pool->Min;
    while (pool->Threads < pool->Min && ok)
        ok = start_worker(pool);
    leave();
    if (!ok)
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
    return ok;
}

DECLSPEC_EXPORT void WINAPI SetThreadpoolThreadMaximum(TP_POOL *pool, DWORD maximum)
{
    enter();
    pool->Max = maximum > 0x7FFFFFFF ? 0x7FFFFFFF : (LONG)maximum;
    if (pool->Min > pool->Max)
        pool->Min = pool->Max;
    WakeAllConditionVariable(&pool->Work);
    leave();
}

DECLSPEC_EXPORT BOOL WINAPI SetThreadpoolStackInformation(TP_POOL *pool, KB_TP_STACK *stack)
{
    if (!pool || !stack) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    enter();
    pool->Stack = *stack;
    leave();
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI QueryThreadpoolStackInformation(TP_POOL *pool, KB_TP_STACK *stack)
{
    if (!pool || !stack) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    enter();
    *stack = pool->Stack;
    leave();
    return TRUE;
}

/* ------------------------------------------------------------------ cleanup groups */

DECLSPEC_EXPORT TP_GROUP *WINAPI CreateThreadpoolCleanupGroup(void)
{
    TP_GROUP *group = KbAllocZero(sizeof(*group));
    if (!group) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    InitializeListHead(&group->Members);
    return group;
}

/* lock held. ignores the callback this thread is running for the object. */
static BOOL idle(TP_OBJECT *object)
{
    TP_INSTANCE *own = g_current == TLS_OUT_OF_INDEXES ? nullptr : TlsGetValue(g_current);
    LONG running = object->Running - (own && own->Object == object && !own->Disassociated ? 1 : 0);
    return !object->Pending && running <= 0;
}

static void wait_idle(TP_OBJECT *object)
{
    while (!idle(object))
        SleepConditionVariableSRW(&g_done, &g_lock, INFINITE, 0);
}

static void disarm(TP_OBJECT *object);

/* lock held. drops the callbacks not yet started and disarms the object. */
static LONG cancel_pending(TP_OBJECT *object)
{
    LONG dropped = 0;
    TP_POOL *pool = object->Pool;
    for (PLIST_ENTRY e = pool->Queue.Flink; e != &pool->Queue;) {
        TP_JOB *job = CONTAINING_RECORD(e, TP_JOB, Link);
        e = e->Flink;
        if (job->Object != object)
            continue;
        RemoveEntryList(&job->Link);
        pool->Queued--;
        object->Pending--;
        if (object->Kind == TPK_IO && !(object->Flags & TPF_LEGACY) && object->Started > 0)
            object->Started--;
        KbFree(job);
        dropped++;
        unref(object);
    }
    disarm(object);
    return dropped;
}

static void close_object(TP_OBJECT *object)
{
    if (object->Closed)
        return;
    object->Closed = TRUE;
    disarm(object);
    /* a queued one-shot callback stays in its group until it is done. */
    BOOL single = object->Kind == TPK_SIMPLE || object->Kind == TPK_ITEM;
    if (object->Group && !(single && (object->Pending || object->Running))) {
        RemoveEntryList(&object->GroupLink);
        object->Group = nullptr;
    }
    unref(object);
}

DECLSPEC_EXPORT void WINAPI CloseThreadpoolCleanupGroupMembers(TP_GROUP *group, BOOL cancel, void *context)
{
    enter();
    /* drop every queued callback before waiting on any, or a freed worker would pick them up. */
    if (cancel) {
        for (PLIST_ENTRY e = group->Members.Flink; e != &group->Members; e = e->Flink)
            cancel_pending(CONTAINING_RECORD(e, TP_OBJECT, GroupLink));
    }
    while (!IsListEmpty(&group->Members)) {
        TP_OBJECT *object = CONTAINING_RECORD(group->Members.Flink, TP_OBJECT, GroupLink);
        object->Refs++;
        wait_idle(object);
        if (object->Group == group) {
            if (cancel && !object->Closed && object->CancelCallback) {
                TP_CANCEL_CALLBACK callback = object->CancelCallback;
                void *argument = object->Context;
                leave();
                callback(argument, context);
                enter();
            }
            if (object->Group == group) {
                /* leave the group for good, closing the object unless its owner already did. */
                if (object->Closed) {
                    RemoveEntryList(&object->GroupLink);
                    object->Group = nullptr;
                } else {
                    close_object(object);
                }
            }
        }
        unref(object);
    }
    leave();
}

DECLSPEC_EXPORT void WINAPI CloseThreadpoolCleanupGroup(TP_GROUP *group)
{
    /* remaining objects simply leave the group. */
    enter();
    while (!IsListEmpty(&group->Members)) {
        TP_OBJECT *object = CONTAINING_RECORD(RemoveHeadList(&group->Members), TP_OBJECT, GroupLink);
        object->Group = nullptr;
    }
    leave();
    KbFree(group);
}

/* ------------------------------------------------------------------ callback instances */

DECLSPEC_EXPORT void WINAPI SetEventWhenCallbackReturns(TP_INSTANCE *instance, HANDLE event)
{
    instance->Event = event;
}

DECLSPEC_EXPORT void WINAPI ReleaseSemaphoreWhenCallbackReturns(TP_INSTANCE *instance, HANDLE semaphore, DWORD count)
{
    instance->Semaphore = semaphore;
    instance->SemaphoreCount = (LONG)count;
}

DECLSPEC_EXPORT void WINAPI ReleaseMutexWhenCallbackReturns(TP_INSTANCE *instance, HANDLE mutex)
{
    instance->Mutex = mutex;
}

DECLSPEC_EXPORT void WINAPI LeaveCriticalSectionWhenCallbackReturns(TP_INSTANCE *instance, CRITICAL_SECTION *section)
{
    instance->Section = section;
}

DECLSPEC_EXPORT void WINAPI FreeLibraryWhenCallbackReturns(TP_INSTANCE *instance, HMODULE module)
{
    instance->Library = module;
}

/* releases waiters and runs the requested actions now instead of on return. */
DECLSPEC_EXPORT void WINAPI DisassociateCurrentThreadFromCallback(TP_INSTANCE *instance)
{
    if (instance->Disassociated)
        return;
    if (instance->Section)
        LeaveCriticalSection(instance->Section);
    if (instance->Mutex)
        ReleaseMutex(instance->Mutex);
    if (instance->Semaphore)
        ReleaseSemaphore(instance->Semaphore, instance->SemaphoreCount, nullptr);
    if (instance->Event)
        SetEvent(instance->Event);
    instance->Section = nullptr;
    instance->Mutex = instance->Semaphore = instance->Event = nullptr;
    enter();
    instance->Disassociated = TRUE;
    instance->Object->Running--;
    if (!instance->Object->Pending && !instance->Object->Running)
        WakeAllConditionVariable(&g_done);
    leave();
}

/* TRUE when the pool can add a thread to cover for this long running one. */
DECLSPEC_EXPORT BOOL WINAPI CallbackMayRunLong(TP_INSTANCE *instance)
{
    TP_POOL *pool = instance->Pool;
    enter();
    BOOL room = pool->Threads < pool->Max;
    if (room && pool->Queued > pool->Idle)
        start_worker(pool);
    leave();
    return room;
}

/* ------------------------------------------------------------------ work */

DECLSPEC_EXPORT TP_OBJECT *WINAPI CreateThreadpoolWork(TP_WORK_CALLBACK callback, void *context, KB_TP_ENV *env)
{
    return new_object(TPK_WORK, callback, context, env);
}

DECLSPEC_EXPORT void WINAPI SubmitThreadpoolWork(TP_OBJECT *work)
{
    enter();
    enqueue(work, 0, 0, 0);
    leave();
}

DECLSPEC_EXPORT void WINAPI WaitForThreadpoolWorkCallbacks(TP_OBJECT *work, BOOL cancel)
{
    enter();
    if (cancel)
        cancel_pending(work);
    wait_idle(work);
    leave();
}

DECLSPEC_EXPORT void WINAPI CloseThreadpoolWork(TP_OBJECT *work)
{
    enter();
    close_object(work);
    leave();
}

DECLSPEC_EXPORT BOOL WINAPI TrySubmitThreadpoolCallback(TP_SIMPLE callback, void *context, KB_TP_ENV *env)
{
    TP_OBJECT *object = new_object(TPK_SIMPLE, callback, context, env);
    if (!object)
        return FALSE;
    enter();
    /* the object lives for this one callback. */
    enqueue(object, 0, 0, 0);
    close_object(object);
    leave();
    return TRUE;
}

/* ------------------------------------------------------------------ timers */

/* lock held. */
static void timer_link(TS_ENTRY *entry)
{
    if (entry->Linked) {
        RemoveEntryList(&entry->Link);
        entry->Linked = FALSE;
    }
    PLIST_ENTRY e = g_timers.Blink;
    while (e != &g_timers && CONTAINING_RECORD(e, TS_ENTRY, Link)->Due > entry->Due)
        e = e->Blink;
    InsertHeadList(e, &entry->Link);
    entry->Linked = TRUE;
    WakeConditionVariable(&g_timer_changed);
    start_timer_thread();
}

static void timer_unlink(TS_ENTRY *entry)
{
    if (entry->Linked) {
        RemoveEntryList(&entry->Link);
        entry->Linked = FALSE;
    }
}

static void timer_fire(TS_ENTRY *entry)
{
    TP_OBJECT *timer = CONTAINING_RECORD(entry, TP_OBJECT, Timer);
    if (timer->Period) {
        /* periods count from the due time, and missed ticks after a stall are not replayed. */
        ULONGLONG now = now_hns();
        entry->Due += (ULONGLONG)timer->Period * 10000;
        if (entry->Due <= now)
            entry->Due = now + (ULONGLONG)timer->Period * 10000;
        timer_link(entry);
    } else {
        timer->Armed = FALSE;
    }
    dispatch(timer, 0, 0, 0);
}

static DWORD WINAPI timer_service(void *unused)
{
    enter();
    for (;;) {
        if (IsListEmpty(&g_timers)) {
            SleepConditionVariableSRW(&g_timer_changed, &g_lock, INFINITE, 0);
            continue;
        }
        TS_ENTRY *first = CONTAINING_RECORD(g_timers.Flink, TS_ENTRY, Link);
        ULONGLONG now = now_hns();
        if (first->Due > now) {
            SleepConditionVariableSRW(&g_timer_changed, &g_lock, milliseconds_until(first->Due, now), 0);
            continue;
        }
        RemoveEntryList(&first->Link);
        first->Linked = FALSE;
        first->Fire(first);
    }
}

/* lock held. */
static void start_timer_thread(void)
{
    if (g_timer_thread)
        return;
    HANDLE thread = CreateThread(nullptr, 0, timer_service, nullptr, 0, nullptr);
    if (thread) {
        g_timer_thread = TRUE;
        NtClose(thread);
    }
}

DECLSPEC_EXPORT TP_OBJECT *WINAPI CreateThreadpoolTimer(TP_WORK_CALLBACK callback, void *context, KB_TP_ENV *env)
{
    TP_OBJECT *timer = new_object(TPK_TIMER, callback, context, env);
    if (timer)
        timer->Timer.Fire = timer_fire;
    return timer;
}

/* lock held. */
static void arm_timer(TP_OBJECT *timer, ULONGLONG delay, ULONG period)
{
    timer->Period = period;
    timer->Armed = TRUE;
    timer->Timer.Due = now_hns() + delay;
    timer_link(&timer->Timer);
}

DECLSPEC_EXPORT void WINAPI SetThreadpoolTimer(TP_OBJECT *timer, const FILETIME *due, DWORD period, DWORD window)
{
    enter();
    timer_unlink(&timer->Timer);
    timer->Armed = FALSE;
    if (due)
        arm_timer(timer, delay_of(*(const LONGLONG *)due), period);
    leave();
}

DECLSPEC_EXPORT BOOL WINAPI SetThreadpoolTimerEx(TP_OBJECT *timer, const FILETIME *due, DWORD period, DWORD window)
{
    SetThreadpoolTimer(timer, due, period, window);
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI IsThreadpoolTimerSet(TP_OBJECT *timer)
{
    enter();
    BOOL armed = timer->Armed;
    leave();
    return armed;
}

DECLSPEC_EXPORT void WINAPI WaitForThreadpoolTimerCallbacks(TP_OBJECT *timer, BOOL cancel)
{
    enter();
    if (cancel)
        cancel_pending(timer);
    wait_idle(timer);
    leave();
}

DECLSPEC_EXPORT void WINAPI CloseThreadpoolTimer(TP_OBJECT *timer)
{
    enter();
    close_object(timer);
    leave();
}

/* ------------------------------------------------------------------ waits */

static DWORD WINAPI wait_service(void *parameter);

/* lock held. picks a wait thread with no other wait on the same handle. */
static void attach_wait(TP_OBJECT *wait)
{
    WAIT_THREAD *home = nullptr;
    for (PLIST_ENTRY e = g_wait_threads.Flink; e != &g_wait_threads && !home; e = e->Flink) {
        WAIT_THREAD *thread = CONTAINING_RECORD(e, WAIT_THREAD, Link);
        if (thread->Count >= MAXIMUM_WAIT_OBJECTS - 1)
            continue;
        BOOL same = FALSE;
        for (LONG i = 0; i < thread->Count; i++)
            same |= thread->Waits[i]->Handle == wait->Handle;
        if (!same)
            home = thread;
    }
    if (!home) {
        home = KbAllocZero(sizeof(*home));
        if (!home)
            return;
        home->Control = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE thread = home->Control ? CreateThread(nullptr, 0, wait_service, home, 0, nullptr) : nullptr;
        if (!thread) {
            if (home->Control)
                NtClose(home->Control);
            KbFree(home);
            return;
        }
        NtClose(thread);
        InsertTailList(&g_wait_threads, &home->Link);
    }
    wait->Home = home;
    wait->Armed = TRUE;
    wait->Generation++;
    wait->Refs++;
    home->Waits[home->Count++] = wait;
    SetEvent(home->Control);
}

/* lock held. removes the wait from its thread and drops the thread's reference. */
static void detach_wait(TP_OBJECT *wait)
{
    WAIT_THREAD *home = wait->Home;
    if (!home)
        return;
    for (LONG i = 0; i < home->Count; i++) {
        if (home->Waits[i] == wait) {
            home->Waits[i] = home->Waits[--home->Count];
            break;
        }
    }
    wait->Home = nullptr;
    wait->Armed = FALSE;
    wait->Generation++;
    SetEvent(home->Control);
    unref(wait);
}

/* lock held. the wait was satisfied or timed out. */
static void fire_wait(TP_OBJECT *wait, DWORD result)
{
    TP_OBJECT *hold = wait;
    hold->Refs++;
    detach_wait(wait);
    dispatch(hold, result, 0, 0);
    unref(hold);
}

static DWORD WINAPI wait_service(void *parameter)
{
    WAIT_THREAD *thread = parameter;
    enter();
    for (;;) {
        if (!thread->Count) {
            RemoveEntryList(&thread->Link);
            leave();
            NtClose(thread->Control);
            KbFree(thread);
            return 0;
        }
        HANDLE handles[MAXIMUM_WAIT_OBJECTS];
        TP_OBJECT *snapshot[MAXIMUM_WAIT_OBJECTS];
        ULONG generations[MAXIMUM_WAIT_OBJECTS];
        LONG count = thread->Count;
        ULONGLONG now = now_hns(), nearest = 0;
        handles[0] = thread->Control;
        for (LONG i = 0; i < count; i++) {
            TP_OBJECT *wait = thread->Waits[i];
            snapshot[i] = wait;
            generations[i] = wait->Generation;
            handles[i + 1] = wait->Handle;
            if (wait->WaitDue && (!nearest || wait->WaitDue < nearest))
                nearest = wait->WaitDue;
        }
        DWORD timeout = nearest ? milliseconds_until(nearest, now) : INFINITE;
        leave();
        DWORD result = WaitForMultipleObjects((DWORD)count + 1, handles, FALSE, timeout);
        enter();
        if (result == WAIT_OBJECT_0)
            continue;
        if (result > WAIT_OBJECT_0 && result <= WAIT_OBJECT_0 + (DWORD)count) {
            TP_OBJECT *wait = snapshot[result - WAIT_OBJECT_0 - 1];
            if (wait->Home == thread && wait->Generation == generations[result - WAIT_OBJECT_0 - 1])
                fire_wait(wait, WAIT_OBJECT_0);
        } else if (result >= WAIT_ABANDONED_0 && result <= WAIT_ABANDONED_0 + (DWORD)count) {
            TP_OBJECT *wait = snapshot[result - WAIT_ABANDONED_0 - 1];
            if (wait->Home == thread && wait->Generation == generations[result - WAIT_ABANDONED_0 - 1])
                fire_wait(wait, WAIT_ABANDONED_0);
        } else if (result == WAIT_TIMEOUT) {
            now = now_hns();
            for (LONG i = 0; i < count; i++) {
                TP_OBJECT *wait = snapshot[i];
                if (wait->Home == thread && wait->Generation == generations[i] && wait->WaitDue && wait->WaitDue <= now)
                    fire_wait(wait, WAIT_TIMEOUT);
            }
        } else {
            /* drop waits whose handle was closed under them. */
            for (LONG i = 0; i < count; i++) {
                TP_OBJECT *wait = snapshot[i];
                DWORD flags;
                if (wait->Home == thread && wait->Generation == generations[i] && !GetHandleInformation(wait->Handle, &flags))
                    detach_wait(wait);
            }
        }
    }
}

DECLSPEC_EXPORT TP_OBJECT *WINAPI CreateThreadpoolWait(TP_WAIT_CALLBACK callback, void *context, KB_TP_ENV *env)
{
    return new_object(TPK_WAIT, callback, context, env);
}

/* lock held. */
static void disarm(TP_OBJECT *object)
{
    if (object->Kind == TPK_TIMER) {
        timer_unlink(&object->Timer);
        object->Armed = FALSE;
    } else if (object->Kind == TPK_WAIT) {
        detach_wait(object);
    }
}

DECLSPEC_EXPORT void WINAPI SetThreadpoolWait(TP_OBJECT *wait, HANDLE handle, const FILETIME *timeout)
{
    enter();
    detach_wait(wait);
    if (handle) {
        wait->Handle = handle;
        wait->WaitDue = timeout ? now_hns() + delay_of(*(const LONGLONG *)timeout) : 0;
        if (timeout && !wait->WaitDue)
            wait->WaitDue = 1;
        attach_wait(wait);
    }
    leave();
}

DECLSPEC_EXPORT BOOL WINAPI SetThreadpoolWaitEx(TP_OBJECT *wait, HANDLE handle, const FILETIME *timeout, void *reserved)
{
    SetThreadpoolWait(wait, handle, timeout);
    return TRUE;
}

DECLSPEC_EXPORT void WINAPI WaitForThreadpoolWaitCallbacks(TP_OBJECT *wait, BOOL cancel)
{
    enter();
    if (cancel)
        cancel_pending(wait);
    wait_idle(wait);
    leave();
}

DECLSPEC_EXPORT void WINAPI CloseThreadpoolWait(TP_OBJECT *wait)
{
    enter();
    close_object(wait);
    leave();
}

/* ------------------------------------------------------------------ i/o */

static DWORD WINAPI io_service(void *parameter)
{
    HANDLE port = parameter;
    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED overlapped = nullptr;
        BOOL ok = GetQueuedCompletionStatus(port, &bytes, &key, &overlapped, INFINITE);
        DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (!overlapped) {
            if (!ok)
                return 0;
            continue;
        }
        TP_OBJECT *io = (TP_OBJECT *)key;
        enter();
        if (!(io->Flags & TPF_LEGACY) && io->Started > 0)
            io->Started--;
        enqueue(io, (ULONG_PTR)overlapped, error, bytes);
        if (!io->Started)
            WakeAllConditionVariable(&g_done);
        if (!(io->Flags & TPF_LEGACY))
            unref(io);
        leave();
    }
}

/* lock held. */
static HANDLE pool_port(TP_POOL *pool)
{
    if (!pool->Port) {
        HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (!port)
            return nullptr;
        HANDLE thread = CreateThread(nullptr, 0, io_service, port, 0, nullptr);
        if (!thread) {
            NtClose(port);
            return nullptr;
        }
        NtClose(thread);
        pool->Port = port;
    }
    return pool->Port;
}

/* completions on file become callbacks of the returned object. */
static TP_OBJECT *new_io(HANDLE file, void *callback, void *context, const KB_TP_ENV *env, ULONG flags)
{
    TP_OBJECT *io = new_object(TPK_IO, callback, context, env);
    if (!io)
        return nullptr;
    io->Flags = flags;
    enter();
    HANDLE port = pool_port(io->Pool);
    io->Refs++; /* the port key points at the object until the handle is closed */
    leave();
    if (!port || !CreateIoCompletionPort(file, port, (ULONG_PTR)io, 0)) {
        DWORD error = port ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
        enter();
        close_object(io);
        leave();
        KbSetError(error);
        return nullptr;
    }
    return io;
}

DECLSPEC_EXPORT TP_OBJECT *WINAPI CreateThreadpoolIo(HANDLE file, TP_IO_CALLBACK callback, void *context, KB_TP_ENV *env)
{
    return new_io(file, callback, context, env, 0);
}

DECLSPEC_EXPORT void WINAPI StartThreadpoolIo(TP_OBJECT *io)
{
    enter();
    io->Started++;
    io->Refs++;
    leave();
}

DECLSPEC_EXPORT void WINAPI CancelThreadpoolIo(TP_OBJECT *io)
{
    enter();
    if (io->Started > 0) {
        io->Started--;
        if (!io->Started)
            WakeAllConditionVariable(&g_done);
        unref(io);
    }
    leave();
}

DECLSPEC_EXPORT void WINAPI WaitForThreadpoolIoCallbacks(TP_OBJECT *io, BOOL cancel)
{
    enter();
    if (cancel)
        cancel_pending(io);
    while (!idle(io) || io->Started > 0)
        SleepConditionVariableSRW(&g_done, &g_lock, INFINITE, 0);
    leave();
}

DECLSPEC_EXPORT void WINAPI CloseThreadpoolIo(TP_OBJECT *io)
{
    enter();
    close_object(io);
    leave();
}

WINBASEAPI BOOL WINAPI BindIoCompletionCallback(HANDLE file, TP_BOUND_CALLBACK callback, ULONG flags)
{
    return new_io(file, callback, nullptr, nullptr, TPF_LEGACY) != nullptr;
}

/* ------------------------------------------------------------------ work items */

WINBASEAPI BOOL WINAPI QueueUserWorkItem(LPTHREAD_START_ROUTINE function, PVOID context, ULONG flags)
{
    if (!function) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    TP_OBJECT *item = new_object(TPK_ITEM, function, context, nullptr);
    if (!item)
        return FALSE;
    enter();
    enqueue(item, 0, 0, 0);
    close_object(item);
    leave();
    return TRUE;
}

/* ------------------------------------------------------------------ registered waits */

WINBASEAPI BOOL WINAPI RegisterWaitForSingleObject(PHANDLE result, HANDLE object, WAITORTIMERCALLBACK callback,
                                                   PVOID context, ULONG timeout, ULONG flags)
{
    if (!result || !callback) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HANDLE copy;
    if (!DuplicateHandle(GetCurrentProcess(), object, GetCurrentProcess(), &copy, SYNCHRONIZE, FALSE, 0))
        return FALSE;
    TP_OBJECT *wait = new_object(TPK_WAIT, callback, context, nullptr);
    if (!wait) {
        NtClose(copy);
        return FALSE;
    }
    wait->Flags = TPF_LEGACY | (flags & (WT_EXECUTEINWAITTHREAD | WT_EXECUTEONLYONCE));
    wait->Handle = copy;
    wait->OwnsHandle = TRUE;
    wait->Timeout = timeout;
    wait->WaitDue = timeout == INFINITE ? 0 : now_hns() + (ULONGLONG)timeout * 10000;
    enter();
    attach_wait(wait);
    BOOL attached = wait->Armed;
    if (!attached)
        close_object(wait);
    leave();
    if (!attached) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    *result = wait;
    return TRUE;
}

WINBASEAPI HANDLE WINAPI RegisterWaitForSingleObjectEx(HANDLE object, WAITORTIMERCALLBACK callback, PVOID context,
                                                       ULONG timeout, ULONG flags)
{
    HANDLE handle;
    return RegisterWaitForSingleObject(&handle, object, callback, context, timeout, flags) ? handle : nullptr;
}

/*
 * completion NULL returns at once, INVALID_HANDLE_VALUE waits for running callbacks, and an event is signaled when
 * they are done. the registration is gone either way.
 */
WINBASEAPI BOOL WINAPI UnregisterWaitEx(HANDLE handle, HANDLE completion)
{
    TP_OBJECT *wait = handle;
    if (!wait) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    enter();
    wait->Flags |= TPF_UNREGISTERED;
    detach_wait(wait);
    BOOL busy = !idle(wait);
    BOOL ok = TRUE;
    if (completion == INVALID_HANDLE_VALUE) {
        wait_idle(wait);
    } else if (completion) {
        if (busy)
            wait->Completion = completion;
        else
            SetEvent(completion);
    } else if (busy) {
        ok = FALSE;
    }
    close_object(wait);
    leave();
    if (!ok)
        KbSetError(ERROR_IO_PENDING);
    return ok;
}

WINBASEAPI BOOL WINAPI UnregisterWait(HANDLE handle)
{
    return UnregisterWaitEx(handle, nullptr);
}

/* ------------------------------------------------------------------ timer queues */

DECLSPEC_EXPORT HANDLE WINAPI CreateTimerQueue(void)
{
    TIMER_QUEUE *queue = KbAllocZero(sizeof(*queue));
    if (!queue) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    InitializeListHead(&queue->Timers);
    return queue;
}

static TIMER_QUEUE g_default_queue;

/* lock held. */
static TIMER_QUEUE *queue_of(HANDLE handle)
{
    if (handle)
        return handle;
    if (!g_default_queue.Timers.Flink)
        InitializeListHead(&g_default_queue.Timers);
    return &g_default_queue;
}

DECLSPEC_EXPORT BOOL WINAPI CreateTimerQueueTimer(PHANDLE result, HANDLE queue, WAITORTIMERCALLBACK callback, PVOID context,
                                                  DWORD due, DWORD period, ULONG flags)
{
    if (!result || !callback) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    TP_OBJECT *timer = new_object(TPK_TIMER, callback, context, nullptr);
    if (!timer)
        return FALSE;
    timer->Timer.Fire = timer_fire;
    timer->Flags = TPF_LEGACY | (flags & (WT_EXECUTEINTIMERTHREAD_ | WT_EXECUTEONLYONCE));
    enter();
    TIMER_QUEUE *q = queue_of(queue);
    if (q->Deleted) {
        close_object(timer);
        leave();
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    timer->Queue = q;
    InsertTailList(&q->Timers, &timer->QueueLink);
    arm_timer(timer, (ULONGLONG)due * 10000, (flags & WT_EXECUTEONLYONCE) ? 0 : period);
    leave();
    *result = timer;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI ChangeTimerQueueTimer(HANDLE queue, HANDLE handle, ULONG due, ULONG period)
{
    TP_OBJECT *timer = handle;
    if (!timer || !(timer->Flags & TPF_LEGACY) || timer->Kind != TPK_TIMER) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    enter();
    BOOL gone = timer->Closed || (timer->Flags & TPF_UNREGISTERED);
    if (!gone) {
        timer_unlink(&timer->Timer);
        arm_timer(timer, (ULONGLONG)due * 10000, (timer->Flags & WT_EXECUTEONLYONCE) ? 0 : period);
    }
    leave();
    if (gone)
        KbSetError(ERROR_INVALID_HANDLE);
    return !gone;
}

/* lock held. removes a timer from its queue and stops it. */
static BOOL delete_timer(TP_OBJECT *timer, HANDLE completion)
{
    timer->Flags |= TPF_UNREGISTERED;
    timer_unlink(&timer->Timer);
    timer->Armed = FALSE;
    if (!IsListEmpty(&timer->QueueLink))
        RemoveEntryList(&timer->QueueLink);
    InitializeListHead(&timer->QueueLink);
    BOOL busy = !idle(timer);
    BOOL ok = TRUE;
    if (completion == INVALID_HANDLE_VALUE) {
        wait_idle(timer);
    } else if (completion) {
        if (busy)
            timer->Completion = completion;
        else
            SetEvent(completion);
    } else if (busy) {
        ok = FALSE;
    }
    close_object(timer);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI DeleteTimerQueueTimer(HANDLE queue, HANDLE handle, HANDLE completion)
{
    TP_OBJECT *timer = handle;
    if (!timer) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    enter();
    BOOL ok = delete_timer(timer, completion);
    leave();
    if (!ok)
        KbSetError(ERROR_IO_PENDING);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI CancelTimerQueueTimer(HANDLE queue, HANDLE handle)
{
    TP_OBJECT *timer = handle;
    if (!timer) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    enter();
    timer_unlink(&timer->Timer);
    timer->Armed = FALSE;
    leave();
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI DeleteTimerQueueEx(HANDLE handle, HANDLE completion)
{
    enter();
    TIMER_QUEUE *queue = queue_of(handle);
    queue->Deleted = handle != nullptr;
    BOOL ok = TRUE;
    while (!IsListEmpty(&queue->Timers)) {
        TP_OBJECT *timer = CONTAINING_RECORD(queue->Timers.Flink, TP_OBJECT, QueueLink);
        timer->Refs++;
        ok &= delete_timer(timer, completion ? INVALID_HANDLE_VALUE : nullptr);
        unref(timer);
    }
    if (completion && completion != INVALID_HANDLE_VALUE)
        SetEvent(completion);
    leave();
    if (handle)
        KbFree(queue);
    if (!ok)
        KbSetError(ERROR_IO_PENDING);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI DeleteTimerQueue(HANDLE queue)
{
    return DeleteTimerQueueEx(queue, nullptr);
}
