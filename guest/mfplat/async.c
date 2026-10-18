/*
 * asynchronous results and the work queues that invoke their callbacks.
 * each queue is a thread of its own, started at its first item; the
 * standard queues share their ids with windows, the ones a program
 * allocates get ids of their own. see docs/mfplat.md.
 */
#include "mfplat_private.h"

typedef struct {
    IMFAsyncResult iface;
    LONG refs;
    IUnknown *object;
    IMFAsyncCallback *callback;
    IUnknown *state;
    HRESULT status;
} AsyncResult;

typedef struct WorkItem {
    struct WorkItem *next;
    IMFAsyncResult *result;
    /* scheduled items only. */
    ULONGLONG due;
    MFWORKITEM_KEY key;
    DWORD queue;
} WorkItem;

typedef struct WorkQueue {
    struct WorkQueue *next;
    DWORD id;
    LONG locks;
    WorkItem *head, *tail;
    CONDITION_VARIABLE ready;
    HANDLE thread;
    DWORD thread_id;
    /* unlinked while its thread runs, which frees it on its way out. */
    BOOL closed;
} WorkQueue;

#define FIRST_PRIVATE_QUEUE 0x00010000

static CRITICAL_SECTION g_lock;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static WorkQueue *g_queues;
static DWORD g_next_queue = FIRST_PRIVATE_QUEUE;
static BOOL g_stopping;
static WorkItem *g_scheduled;
static CONDITION_VARIABLE g_timer_ready;
static HANDLE g_timer_thread;
static MFWORKITEM_KEY g_next_key = 1;

static BOOL CALLBACK init_once(INIT_ONCE *once, void *parameter, void **context)
{
    InitializeCriticalSection(&g_lock);
    InitializeConditionVariable(&g_timer_ready);
    return TRUE;
}

static void init(void)
{
    InitOnceExecuteOnce(&g_once, init_once, nullptr, nullptr);
}

/* ------------------------------------------------------------------ async results */

static AsyncResult *async_result(IMFAsyncResult *iface)
{
    return CONTAINING_RECORD(iface, AsyncResult, iface);
}

static HRESULT STDMETHODCALLTYPE result_query_interface(IMFAsyncResult *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFAsyncResult)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE result_add_ref(IMFAsyncResult *iface)
{
    return InterlockedIncrement(&async_result(iface)->refs);
}

static ULONG STDMETHODCALLTYPE result_release(IMFAsyncResult *iface)
{
    AsyncResult *result = async_result(iface);
    ULONG refs = InterlockedDecrement(&result->refs);
    if (!refs) {
        if (result->object)
            result->object->lpVtbl->Release(result->object);
        if (result->callback)
            result->callback->lpVtbl->Release(result->callback);
        if (result->state)
            result->state->lpVtbl->Release(result->state);
        MfFree(result);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE result_get_state(IMFAsyncResult *iface, IUnknown **state)
{
    AsyncResult *result = async_result(iface);
    if (!state)
        return E_INVALIDARG;
    if (!result->state)
        return E_POINTER;
    *state = result->state;
    result->state->lpVtbl->AddRef(result->state);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE result_get_status(IMFAsyncResult *iface)
{
    return async_result(iface)->status;
}

static HRESULT STDMETHODCALLTYPE result_set_status(IMFAsyncResult *iface, HRESULT status)
{
    async_result(iface)->status = status;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE result_get_object(IMFAsyncResult *iface, IUnknown **object)
{
    AsyncResult *result = async_result(iface);
    if (!object)
        return E_INVALIDARG;
    if (!result->object) {
        *object = nullptr;
        return E_POINTER;
    }
    *object = result->object;
    result->object->lpVtbl->AddRef(result->object);
    return S_OK;
}

static IUnknown *STDMETHODCALLTYPE result_get_state_no_add_ref(IMFAsyncResult *iface)
{
    return async_result(iface)->state;
}

static const IMFAsyncResultVtbl ASYNC_RESULT_VTBL = {
    result_query_interface, result_add_ref,    result_release,
    result_get_state,       result_get_status, result_set_status,
    result_get_object,      result_get_state_no_add_ref,
};

HRESULT MfAsyncResultCreate(IUnknown *object, IMFAsyncCallback *callback, IUnknown *state, IMFAsyncResult **out)
{
    AsyncResult *result = MfAllocZero(sizeof(*result));
    if (!result)
        return E_OUTOFMEMORY;
    result->iface.lpVtbl = &ASYNC_RESULT_VTBL;
    result->refs = 1;
    if ((result->object = object))
        object->lpVtbl->AddRef(object);
    if ((result->callback = callback))
        callback->lpVtbl->AddRef(callback);
    if ((result->state = state))
        state->lpVtbl->AddRef(state);
    *out = &result->iface;
    return S_OK;
}

IMFAsyncCallback *MfAsyncResultCallback(IMFAsyncResult *iface)
{
    return iface->lpVtbl == &ASYNC_RESULT_VTBL ? async_result(iface)->callback : nullptr;
}

HRESULT WINAPI MFCreateAsyncResult(IUnknown *object, IMFAsyncCallback *callback, IUnknown *state,
                                   IMFAsyncResult **result)
{
    if (!result)
        return E_INVALIDARG;
    return MfAsyncResultCreate(object, callback, state, result);
}

/* the callback of a result mfplat created; other results have none it knows of. */
HRESULT WINAPI MFInvokeCallback(IMFAsyncResult *result)
{
    if (!result)
        return E_INVALIDARG;
    IMFAsyncCallback *callback = MfAsyncResultCallback(result);
    if (!callback)
        return E_INVALIDARG;
    callback->lpVtbl->Invoke(callback, result);
    return S_OK;
}

/* ------------------------------------------------------------------ queues */

static BOOL standard_queue(DWORD id)
{
    return id >= MFASYNC_CALLBACK_QUEUE_STANDARD && id <= MFASYNC_CALLBACK_QUEUE_LONG_FUNCTION;
}

/* the queue of id under the lock, the shared ones made at their first use. */
static WorkQueue *find_queue(DWORD id)
{
    if (id == MFASYNC_CALLBACK_QUEUE_UNDEFINED)
        id = MFASYNC_CALLBACK_QUEUE_STANDARD;
    for (WorkQueue *queue = g_queues; queue; queue = queue->next) {
        if (queue->id == id)
            return queue;
    }
    if (!standard_queue(id))
        return nullptr;
    WorkQueue *queue = MfAllocZero(sizeof(*queue));
    if (!queue)
        return nullptr;
    queue->id = id;
    InitializeConditionVariable(&queue->ready);
    queue->next = g_queues;
    g_queues = queue;
    return queue;
}

static void free_items(WorkItem *item)
{
    while (item) {
        WorkItem *next = item->next;
        item->result->lpVtbl->Release(item->result);
        MfFree(item);
        item = next;
    }
}

static DWORD WINAPI queue_thread(void *parameter)
{
    WorkQueue *queue = parameter;
    EnterCriticalSection(&g_lock);
    for (;;) {
        while (!queue->head && !g_stopping && !queue->closed)
            SleepConditionVariableCS(&queue->ready, &g_lock, INFINITE);
        if (g_stopping || !queue->head)
            break;
        WorkItem *item = queue->head;
        queue->head = item->next;
        if (!queue->head)
            queue->tail = nullptr;
        LeaveCriticalSection(&g_lock);
        IMFAsyncCallback *callback = MfAsyncResultCallback(item->result);
        if (callback)
            callback->lpVtbl->Invoke(callback, item->result);
        item->result->lpVtbl->Release(item->result);
        MfFree(item);
        EnterCriticalSection(&g_lock);
    }
    if (queue->closed) {
        free_items(queue->head);
        CloseHandle(queue->thread);
        MfFree(queue);
    }
    LeaveCriticalSection(&g_lock);
    return 0;
}

/* the result to the queue's thread, which calls its callback's Invoke. */
static HRESULT queue_result_locked(DWORD id, IMFAsyncResult *result)
{
    if (!MfStarted() || g_stopping)
        return MF_E_SHUTDOWN;
    WorkQueue *queue = find_queue(id);
    if (!queue)
        return E_INVALIDARG;
    if (!queue->thread) {
        queue->thread = CreateThread(nullptr, 0, queue_thread, queue, 0, &queue->thread_id);
        if (!queue->thread)
            return HRESULT_FROM_WIN32(GetLastError());
    }
    WorkItem *item = MfAllocZero(sizeof(*item));
    if (!item)
        return E_OUTOFMEMORY;
    item->result = result;
    result->lpVtbl->AddRef(result);
    if (queue->tail)
        queue->tail->next = item;
    else
        queue->head = item;
    queue->tail = item;
    WakeConditionVariable(&queue->ready);
    return S_OK;
}

HRESULT MfQueueResult(DWORD queue, IMFAsyncResult *result)
{
    init();
    EnterCriticalSection(&g_lock);
    HRESULT hr = queue_result_locked(queue, result);
    LeaveCriticalSection(&g_lock);
    return hr;
}

/* stops every thread, dropping what was queued; MFStartup starts them again. */
void MfWorkQueuesShutdown(void)
{
    init();
    EnterCriticalSection(&g_lock);
    g_stopping = TRUE;
    HANDLE threads[64];
    DWORD count = 0;
    WakeAllConditionVariable(&g_timer_ready);
    if (g_timer_thread)
        threads[count++] = g_timer_thread;
    g_timer_thread = nullptr;
    WorkQueue **link = &g_queues;
    while (*link) {
        WorkQueue *queue = *link;
        WakeConditionVariable(&queue->ready);
        if (queue->thread && queue->thread_id == GetCurrentThreadId()) {
            /* shut down from one of its items: the queue's own thread frees it once that returns. */
            *link = queue->next;
            queue->closed = TRUE;
            continue;
        }
        if (queue->thread && count < RTL_NUMBER_OF(threads))
            threads[count++] = queue->thread;
        else if (queue->thread)
            CloseHandle(queue->thread);
        queue->thread = nullptr;
        link = &queue->next;
    }
    LeaveCriticalSection(&g_lock);
    for (DWORD i = 0; i < count; i++) {
        WaitForSingleObject(threads[i], 5000);
        CloseHandle(threads[i]);
    }
    EnterCriticalSection(&g_lock);
    while (g_queues) {
        WorkQueue *queue = g_queues;
        g_queues = queue->next;
        free_items(queue->head);
        MfFree(queue);
    }
    free_items(g_scheduled);
    g_scheduled = nullptr;
    g_stopping = FALSE;
    LeaveCriticalSection(&g_lock);
}

HRESULT WINAPI MFAllocateWorkQueue(DWORD *id)
{
    if (!id)
        return E_INVALIDARG;
    init();
    EnterCriticalSection(&g_lock);
    HRESULT hr = MfStarted() ? S_OK : MF_E_SHUTDOWN;
    WorkQueue *queue = SUCCEEDED(hr) ? MfAllocZero(sizeof(*queue)) : nullptr;
    if (SUCCEEDED(hr) && !queue)
        hr = E_OUTOFMEMORY;
    if (queue) {
        queue->id = g_next_queue++;
        queue->locks = 1;
        InitializeConditionVariable(&queue->ready);
        queue->next = g_queues;
        g_queues = queue;
        *id = queue->id;
    }
    LeaveCriticalSection(&g_lock);
    return hr;
}

HRESULT WINAPI MFAllocateWorkQueueEx(DWORD type, DWORD *id)
{
    return MFAllocateWorkQueue(id);
}

HRESULT WINAPI MFLockWorkQueue(DWORD id)
{
    init();
    EnterCriticalSection(&g_lock);
    WorkQueue *queue = find_queue(id);
    if (queue)
        queue->locks++;
    LeaveCriticalSection(&g_lock);
    return queue ? S_OK : E_INVALIDARG;
}

/* a private queue ends with its last lock, after the item it runs. */
HRESULT WINAPI MFUnlockWorkQueue(DWORD id)
{
    init();
    EnterCriticalSection(&g_lock);
    WorkQueue *queue = find_queue(id);
    HRESULT hr = queue ? S_OK : E_INVALIDARG;
    if (queue && --queue->locks <= 0 && !standard_queue(queue->id)) {
        WorkQueue **link = &g_queues;
        while (*link != queue)
            link = &(*link)->next;
        *link = queue->next;
        if (queue->thread) {
            queue->closed = TRUE;
            WakeConditionVariable(&queue->ready);
        } else {
            free_items(queue->head);
            MfFree(queue);
        }
    }
    LeaveCriticalSection(&g_lock);
    return hr;
}

HRESULT WINAPI MFPutWorkItemEx(DWORD queue, IMFAsyncResult *result)
{
    if (!result)
        return E_INVALIDARG;
    return MfQueueResult(queue, result);
}

HRESULT WINAPI MFPutWorkItemEx2(DWORD queue, LONG priority, IMFAsyncResult *result)
{
    return MFPutWorkItemEx(queue, result);
}

HRESULT WINAPI MFPutWorkItem(DWORD queue, IMFAsyncCallback *callback, IUnknown *state)
{
    IMFAsyncResult *result;
    HRESULT hr = MfAsyncResultCreate(nullptr, callback, state, &result);
    if (FAILED(hr))
        return hr;
    hr = MfQueueResult(queue, result);
    result->lpVtbl->Release(result);
    return hr;
}

HRESULT WINAPI MFPutWorkItem2(DWORD queue, LONG priority, IMFAsyncCallback *callback, IUnknown *state)
{
    return MFPutWorkItem(queue, callback, state);
}

/* ------------------------------------------------------------------ scheduled items */

static DWORD WINAPI timer_thread(void *parameter)
{
    EnterCriticalSection(&g_lock);
    while (!g_stopping) {
        ULONGLONG now = GetTickCount64();
        if (g_scheduled && g_scheduled->due <= now) {
            WorkItem *item = g_scheduled;
            g_scheduled = item->next;
            queue_result_locked(MFASYNC_CALLBACK_QUEUE_TIMER, item->result);
            item->result->lpVtbl->Release(item->result);
            MfFree(item);
            continue;
        }
        DWORD wait = g_scheduled ? (DWORD)(g_scheduled->due - now) : INFINITE;
        SleepConditionVariableCS(&g_timer_ready, &g_lock, wait);
    }
    LeaveCriticalSection(&g_lock);
    return 0;
}

/* timeouts are negative milliseconds, the key cancels the item before it runs. */
HRESULT WINAPI MFScheduleWorkItemEx(IMFAsyncResult *result, INT64 timeout, MFWORKITEM_KEY *key)
{
    if (!result)
        return E_INVALIDARG;
    init();
    EnterCriticalSection(&g_lock);
    HRESULT hr = MfStarted() && !g_stopping ? S_OK : MF_E_SHUTDOWN;
    WorkItem *item = SUCCEEDED(hr) ? MfAllocZero(sizeof(*item)) : nullptr;
    if (SUCCEEDED(hr) && !item)
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && !g_timer_thread) {
        g_timer_thread = CreateThread(nullptr, 0, timer_thread, nullptr, 0, nullptr);
        if (!g_timer_thread)
            hr = HRESULT_FROM_WIN32(GetLastError());
    }
    if (SUCCEEDED(hr)) {
        item->result = result;
        result->lpVtbl->AddRef(result);
        item->due = GetTickCount64() + (ULONGLONG)(timeout < 0 ? -timeout : timeout);
        item->key = g_next_key++;
        WorkItem **link = &g_scheduled;
        while (*link && (*link)->due <= item->due)
            link = &(*link)->next;
        item->next = *link;
        *link = item;
        if (key)
            *key = item->key;
        WakeAllConditionVariable(&g_timer_ready);
    } else {
        MfFree(item);
    }
    LeaveCriticalSection(&g_lock);
    return hr;
}

HRESULT WINAPI MFScheduleWorkItem(IMFAsyncCallback *callback, IUnknown *state, INT64 timeout, MFWORKITEM_KEY *key)
{
    IMFAsyncResult *result;
    HRESULT hr = MfAsyncResultCreate(nullptr, callback, state, &result);
    if (FAILED(hr))
        return hr;
    hr = MFScheduleWorkItemEx(result, timeout, key);
    result->lpVtbl->Release(result);
    return hr;
}

HRESULT WINAPI MFCancelWorkItem(MFWORKITEM_KEY key)
{
    init();
    EnterCriticalSection(&g_lock);
    WorkItem *found = nullptr;
    for (WorkItem **link = &g_scheduled; *link; link = &(*link)->next) {
        if ((*link)->key == key) {
            found = *link;
            *link = found->next;
            break;
        }
    }
    LeaveCriticalSection(&g_lock);
    if (!found)
        return MF_E_NOT_FOUND;
    found->result->lpVtbl->Release(found->result);
    MfFree(found);
    return S_OK;
}
