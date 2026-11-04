#include "coremessaging_private.h"

static const WCHAR TIMER_CLASS[] = L"Windows.System.DispatcherQueueTimer";

/* the queue's lock guards the state of its timers. */
struct CM_TIMER {
    CM_OBJECT Object;
    CM_QUEUE *Queue;
    CM_TIMER *Next, *Prev;
    PCM_TP_TIMER Pool;
    UINT32 Id;
    UINT32 Generation;
    INT64 Interval;
    BOOL Repeating, Running;
    CM_EVENT Tick;
};

static volatile LONG g_ids;

CM_INSPECTABLE_THUNKS(IDispatcherQueueTimer)

static CM_TIMER *timer_of(void *iface)
{
    return (CM_TIMER *)CmOwner(iface);
}

static void arm(CM_TIMER *timer)
{
    /* a negative due time is relative, in units of 100 ns. */
    LARGE_INTEGER due;
    due.QuadPart = -timer->Interval;
    FILETIME time = {due.LowPart, (DWORD)due.HighPart};
    SetThreadpoolTimer(timer->Pool, &time, 0, 0);
}

static void disarm(CM_TIMER *timer)
{
    timer->Running = FALSE;
    timer->Generation++;
    SetThreadpoolTimer(timer->Pool, nullptr, 0, 0);
}

void CmTimersStopAll(CM_QUEUE *queue)
{
    for (CM_TIMER *timer = queue->Timers; timer; timer = timer->Next)
        disarm(timer);
}

/* on the queue's thread: the tick of a timer still running in the same generation that fired. */
static void tick(CM_QUEUE *queue, void *context, UINT64 value)
{
    UINT32 id = (UINT32)value, generation = (UINT32)(value >> 32);
    CM_TIMER *timer = nullptr;
    AcquireSRWLockExclusive(&queue->Lock);
    for (CM_TIMER *t = queue->Timers; t; t = t->Next) {
        if (t->Id == id) {
            if (t->Running && t->Generation == generation && CmTryAddRef(&t->Object))
                timer = t;
            break;
        }
    }
    if (timer && !timer->Repeating)
        timer->Running = FALSE;
    ReleaseSRWLockExclusive(&queue->Lock);
    if (!timer)
        return;
    CmEventInvoke(&timer->Tick, CmIface(&timer->Object), nullptr);
    AcquireSRWLockExclusive(&queue->Lock);
    if (timer->Running && timer->Repeating && timer->Generation == generation)
        arm(timer);
    ReleaseSRWLockExclusive(&queue->Lock);
    CmRelease((WRT_IFACE *)CmIface(&timer->Object));
}

/* on a pool thread, which only hands the tick to the queue: the timer outlives it, see timer_destroy. */
static void CALLBACK fire(void *instance, void *context, PCM_TP_TIMER pool)
{
    CM_TIMER *timer = context;
    CM_QUEUE *queue = timer->Queue;
    AcquireSRWLockShared(&queue->Lock);
    BOOL running = timer->Running;
    UINT64 value = timer->Id | (UINT64)timer->Generation << 32;
    ReleaseSRWLockShared(&queue->Lock);
    if (running)
        CmQueuePost(queue, CM_NORMAL, tick, nullptr, value, FALSE);
}

static void timer_destroy(WRT_OBJECT *object)
{
    CM_TIMER *timer = (CM_TIMER *)object;
    CM_QUEUE *queue = timer->Queue;
    AcquireSRWLockExclusive(&queue->Lock);
    if (timer->Prev)
        timer->Prev->Next = timer->Next;
    else if (queue->Timers == timer)
        queue->Timers = timer->Next;
    if (timer->Next)
        timer->Next->Prev = timer->Prev;
    ReleaseSRWLockExclusive(&queue->Lock);
    if (timer->Pool) {
        SetThreadpoolTimer(timer->Pool, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(timer->Pool, TRUE);
        CloseThreadpoolTimer(timer->Pool);
    }
    CmEventClear(&timer->Tick);
    CmRelease((WRT_IFACE *)CmIface(&queue->Object));
    HeapFree(GetProcessHeap(), 0, timer);
}

static HRESULT STDMETHODCALLTYPE timer_get_interval(IDispatcherQueueTimer *This, INT64 *value)
{
    CM_TIMER *timer = timer_of(This);
    if (!value)
        return E_POINTER;
    AcquireSRWLockShared(&timer->Queue->Lock);
    *value = timer->Interval;
    ReleaseSRWLockShared(&timer->Queue->Lock);
    return S_OK;
}

/* a running timer starts over with the new interval. */
static HRESULT STDMETHODCALLTYPE timer_put_interval(IDispatcherQueueTimer *This, INT64 value)
{
    CM_TIMER *timer = timer_of(This);
    if (value < 0)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&timer->Queue->Lock);
    timer->Interval = value;
    if (timer->Running) {
        timer->Generation++;
        arm(timer);
    }
    ReleaseSRWLockExclusive(&timer->Queue->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE timer_get_running(IDispatcherQueueTimer *This, BOOLEAN *value)
{
    CM_TIMER *timer = timer_of(This);
    if (!value)
        return E_POINTER;
    AcquireSRWLockShared(&timer->Queue->Lock);
    *value = timer->Running != FALSE;
    ReleaseSRWLockShared(&timer->Queue->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE timer_get_repeating(IDispatcherQueueTimer *This, BOOLEAN *value)
{
    CM_TIMER *timer = timer_of(This);
    if (!value)
        return E_POINTER;
    AcquireSRWLockShared(&timer->Queue->Lock);
    *value = timer->Repeating != FALSE;
    ReleaseSRWLockShared(&timer->Queue->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE timer_put_repeating(IDispatcherQueueTimer *This, BOOLEAN value)
{
    CM_TIMER *timer = timer_of(This);
    AcquireSRWLockExclusive(&timer->Queue->Lock);
    timer->Repeating = value != 0;
    ReleaseSRWLockExclusive(&timer->Queue->Lock);
    return S_OK;
}

/* starting a running timer starts it over. a queue that shut down starts nothing and says nothing. */
static HRESULT STDMETHODCALLTYPE timer_start(IDispatcherQueueTimer *This)
{
    CM_TIMER *timer = timer_of(This);
    AcquireSRWLockExclusive(&timer->Queue->Lock);
    if (timer->Queue->State != CM_SHUT) {
        timer->Running = TRUE;
        timer->Generation++;
        arm(timer);
    }
    ReleaseSRWLockExclusive(&timer->Queue->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE timer_stop(IDispatcherQueueTimer *This)
{
    CM_TIMER *timer = timer_of(This);
    AcquireSRWLockExclusive(&timer->Queue->Lock);
    disarm(timer);
    ReleaseSRWLockExclusive(&timer->Queue->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE timer_add_tick(IDispatcherQueueTimer *This, ICmEventHandler *handler,
                                                EventRegistrationToken *token)
{
    return CmEventAdd(&timer_of(This)->Tick, handler, token);
}

static HRESULT STDMETHODCALLTYPE timer_remove_tick(IDispatcherQueueTimer *This, EventRegistrationToken token)
{
    return CmEventRemove(&timer_of(This)->Tick, token);
}

static const IDispatcherQueueTimerVtbl timer_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IDispatcherQueueTimer),
    timer_get_interval,
    timer_put_interval,
    timer_get_running,
    timer_get_repeating,
    timer_put_repeating,
    timer_start,
    timer_stop,
    timer_add_tick,
    timer_remove_tick,
};
static const WRT_CLASS timer_class = {
    TIMER_CLASS, 2, {&IID_IDispatcherQueueTimer, &IID_CmWeakReferenceSource}, 1 << 1, timer_destroy};

/* a timer repeats until told otherwise. one made after the queue shut down never ticks. */
HRESULT CmTimerCreate(CM_QUEUE *queue, IDispatcherQueueTimer **result)
{
    *result = nullptr;
    CM_TIMER *timer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*timer));
    if (!timer)
        return E_OUTOFMEMORY;
    const void *vtables[2] = {&timer_vtbl, &CmWeakSourceVtbl};
    CmInit(&timer->Object, &timer_class, vtables);
    InitializeSRWLock(&timer->Tick.Lock);
    CmAddRef((WRT_IFACE *)CmIface(&queue->Object));
    timer->Queue = queue;
    timer->Repeating = TRUE;
    timer->Id = (UINT32)InterlockedIncrement(&g_ids);
    timer->Pool = CreateThreadpoolTimer(fire, timer, nullptr);
    if (!timer->Pool) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CmRelease((WRT_IFACE *)CmIface(&timer->Object));
        return FAILED(hr) ? hr : E_OUTOFMEMORY;
    }
    AcquireSRWLockExclusive(&queue->Lock);
    if (queue->State != CM_SHUT) {
        timer->Next = queue->Timers;
        if (queue->Timers)
            queue->Timers->Prev = timer;
        queue->Timers = timer;
    }
    ReleaseSRWLockExclusive(&queue->Lock);
    *result = (IDispatcherQueueTimer *)CmIface(&timer->Object);
    return S_OK;
}
