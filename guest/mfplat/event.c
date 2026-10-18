/*
 * media events and the event queue every event generator keeps. a queue
 * hands its events to one reader at a time, waiting in GetEvent or through
 * a BeginGetEvent callback run on the callback's work queue.
 */
#include "mfplat_private.h"

typedef struct {
    MfObject base;
    MediaEventType type;
    GUID extended;
    HRESULT status;
    PROPVARIANT value;
} MediaEvent;

typedef struct {
    IMFAttributesVtbl attributes;
    HRESULT(STDMETHODCALLTYPE *GetType)(IMFMediaEvent *This, MediaEventType *type);
    HRESULT(STDMETHODCALLTYPE *GetExtendedType)(IMFMediaEvent *This, GUID *type);
    HRESULT(STDMETHODCALLTYPE *GetStatus)(IMFMediaEvent *This, HRESULT *status);
    HRESULT(STDMETHODCALLTYPE *GetValue)(IMFMediaEvent *This, PROPVARIANT *value);
} MediaEventVtbl;

static MediaEvent *media_event(IMFMediaEvent *iface)
{
    return (MediaEvent *)iface;
}

static void event_destroy(MfObject *object)
{
    PropVariantClear(&((MediaEvent *)object)->value);
}

static const GUID *const EVENT_IIDS[] = {&IID_IMFMediaEvent, nullptr};
static const MfClass EVENT_CLASS = {EVENT_IIDS, event_destroy};

static HRESULT STDMETHODCALLTYPE event_get_type(IMFMediaEvent *iface, MediaEventType *type)
{
    if (!type)
        return E_POINTER;
    *type = media_event(iface)->type;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE event_get_extended_type(IMFMediaEvent *iface, GUID *type)
{
    if (!type)
        return E_POINTER;
    *type = media_event(iface)->extended;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE event_get_status(IMFMediaEvent *iface, HRESULT *status)
{
    if (!status)
        return E_POINTER;
    *status = media_event(iface)->status;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE event_get_value(IMFMediaEvent *iface, PROPVARIANT *value)
{
    if (!value)
        return E_POINTER;
    return PropVariantCopy(value, &media_event(iface)->value);
}

static const MediaEventVtbl MEDIA_EVENT_VTBL = {
    MF_ATTRIBUTES_VTBL, event_get_type, event_get_extended_type, event_get_status, event_get_value,
};

HRESULT MfMediaEventCreate(MediaEventType type, REFGUID extended, HRESULT status, const PROPVARIANT *value,
                           IMFMediaEvent **out)
{
    MediaEvent *event = MfAllocZero(sizeof(*event));
    if (!event)
        return E_OUTOFMEMORY;
    MfObjectInit(&event->base, &MEDIA_EVENT_VTBL, &EVENT_CLASS, 0);
    event->type = type;
    event->extended = extended ? *extended : GUID_NULL;
    event->status = status;
    if (value) {
        HRESULT hr = PropVariantCopy(&event->value, value);
        if (FAILED(hr)) {
            MfObjectRelease((IMFAttributes *)event);
            return hr;
        }
    }
    *out = (IMFMediaEvent *)event;
    return S_OK;
}

HRESULT WINAPI MFCreateMediaEvent(MediaEventType type, REFGUID extended, HRESULT status, const PROPVARIANT *value,
                                  IMFMediaEvent **event)
{
    if (!event)
        return E_POINTER;
    return MfMediaEventCreate(type, extended, status, value, event);
}

/* ------------------------------------------------------------------ the queue */

typedef struct QueuedEvent {
    struct QueuedEvent *next;
    IMFMediaEvent *event;
} QueuedEvent;

enum { READER_NONE, READER_DISPATCHED, READER_WAITING };

typedef struct {
    IMFMediaEventQueue iface;
    LONG refs;
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE arrived;
    QueuedEvent *head, *tail;
    BOOL shutdown;
    /* who reads next: nobody, a callback already given the head event, or a GetEvent or BeginGetEvent waiting. */
    int reader;
    IMFAsyncCallback *callback;
    IUnknown *state;
    IMFAsyncResult *dispatched;
} EventQueue;

static EventQueue *event_queue(IMFMediaEventQueue *iface)
{
    return CONTAINING_RECORD(iface, EventQueue, iface);
}

static IMFMediaEvent *pop_event(EventQueue *queue)
{
    QueuedEvent *head = queue->head;
    IMFMediaEvent *event = head->event;
    queue->head = head->next;
    if (!queue->head)
        queue->tail = nullptr;
    MfFree(head);
    return event;
}

static void release_reader(EventQueue *queue)
{
    if (queue->callback)
        queue->callback->lpVtbl->Release(queue->callback);
    if (queue->state)
        queue->state->lpVtbl->Release(queue->state);
    queue->callback = nullptr;
    queue->state = nullptr;
}

/* the head event to whoever waits for one: GetEvent wakes, a callback gets a result on its work queue. */
static void dispatch(EventQueue *queue)
{
    if (!queue->head || queue->reader != READER_WAITING)
        return;
    queue->reader = READER_DISPATCHED;
    if (!queue->callback) {
        WakeAllConditionVariable(&queue->arrived);
        return;
    }
    IMFAsyncResult *result;
    if (SUCCEEDED(MfAsyncResultCreate(nullptr, queue->callback, queue->state, &result))) {
        DWORD flags = 0, id = MFASYNC_CALLBACK_QUEUE_STANDARD;
        if (FAILED(queue->callback->lpVtbl->GetParameters(queue->callback, &flags, &id)))
            id = MFASYNC_CALLBACK_QUEUE_STANDARD;
        if (queue->dispatched)
            queue->dispatched->lpVtbl->Release(queue->dispatched);
        queue->dispatched = result;
        result->lpVtbl->AddRef(result);
        MfQueueResult(id, result);
        result->lpVtbl->Release(result);
    }
    release_reader(queue);
}

static HRESULT STDMETHODCALLTYPE queue_query_interface(IMFMediaEventQueue *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFMediaEventQueue)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE queue_add_ref(IMFMediaEventQueue *iface)
{
    return InterlockedIncrement(&event_queue(iface)->refs);
}

static void flush(EventQueue *queue)
{
    while (queue->head) {
        IMFMediaEvent *event = pop_event(queue);
        event->lpVtbl->Release(event);
    }
    release_reader(queue);
    if (queue->dispatched)
        queue->dispatched->lpVtbl->Release(queue->dispatched);
    queue->dispatched = nullptr;
    queue->reader = READER_NONE;
}

static ULONG STDMETHODCALLTYPE queue_release(IMFMediaEventQueue *iface)
{
    EventQueue *queue = event_queue(iface);
    ULONG refs = InterlockedDecrement(&queue->refs);
    if (!refs) {
        flush(queue);
        DeleteCriticalSection(&queue->lock);
        MfFree(queue);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE queue_get_event(IMFMediaEventQueue *iface, DWORD flags, IMFMediaEvent **event)
{
    EventQueue *queue = event_queue(iface);
    if (!event)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    EnterCriticalSection(&queue->lock);
    if (queue->shutdown) {
        hr = MF_E_SHUTDOWN;
    } else if (queue->reader != READER_NONE) {
        hr = MF_E_MULTIPLE_SUBSCRIBERS;
    } else {
        queue->reader = READER_WAITING;
        if (!(flags & MF_EVENT_FLAG_NO_WAIT)) {
            while (!queue->head && !queue->shutdown)
                SleepConditionVariableCS(&queue->arrived, &queue->lock, INFINITE);
        }
        if (queue->shutdown) {
            hr = MF_E_SHUTDOWN;
        } else if (!queue->head) {
            hr = MF_E_NO_EVENTS_AVAILABLE;
        } else {
            *event = pop_event(queue);
        }
        queue->reader = READER_NONE;
    }
    LeaveCriticalSection(&queue->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE queue_begin_get_event(IMFMediaEventQueue *iface, IMFAsyncCallback *callback,
                                                       IUnknown *state)
{
    EventQueue *queue = event_queue(iface);
    if (!callback)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    EnterCriticalSection(&queue->lock);
    if (queue->shutdown) {
        hr = MF_E_SHUTDOWN;
    } else if (queue->reader != READER_NONE) {
        if (callback != queue->callback)
            hr = MF_E_MULTIPLE_SUBSCRIBERS;
        else
            hr = state == queue->state ? MF_S_MULTIPLE_BEGIN : MF_E_MULTIPLE_BEGIN;
    } else {
        queue->callback = callback;
        callback->lpVtbl->AddRef(callback);
        queue->state = state;
        if (state)
            state->lpVtbl->AddRef(state);
        queue->reader = READER_WAITING;
        dispatch(queue);
    }
    LeaveCriticalSection(&queue->lock);
    return hr;
}

/* the head event, for the result its callback was given. */
static HRESULT STDMETHODCALLTYPE queue_end_get_event(IMFMediaEventQueue *iface, IMFAsyncResult *result,
                                                     IMFMediaEvent **event)
{
    EventQueue *queue = event_queue(iface);
    if (!result || !event)
        return E_INVALIDARG;
    HRESULT hr = E_FAIL;
    EnterCriticalSection(&queue->lock);
    if (queue->shutdown) {
        hr = MF_E_SHUTDOWN;
    } else if (queue->reader == READER_DISPATCHED && queue->head && result == queue->dispatched) {
        *event = pop_event(queue);
        queue->dispatched->lpVtbl->Release(queue->dispatched);
        queue->dispatched = nullptr;
        queue->reader = READER_NONE;
        hr = S_OK;
    }
    LeaveCriticalSection(&queue->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE queue_queue_event(IMFMediaEventQueue *iface, IMFMediaEvent *event)
{
    EventQueue *queue = event_queue(iface);
    if (!event)
        return E_INVALIDARG;
    QueuedEvent *entry = MfAlloc(sizeof(*entry));
    if (!entry)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    EnterCriticalSection(&queue->lock);
    if (queue->shutdown) {
        MfFree(entry);
        hr = MF_E_SHUTDOWN;
    } else {
        entry->next = nullptr;
        entry->event = event;
        event->lpVtbl->AddRef(event);
        if (queue->tail)
            queue->tail->next = entry;
        else
            queue->head = entry;
        queue->tail = entry;
        dispatch(queue);
    }
    LeaveCriticalSection(&queue->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE queue_queue_event_param_var(IMFMediaEventQueue *iface, MediaEventType type,
                                                             REFGUID extended, HRESULT status,
                                                             const PROPVARIANT *value)
{
    EventQueue *queue = event_queue(iface);
    if (queue->shutdown)
        return MF_E_SHUTDOWN;
    IMFMediaEvent *event;
    HRESULT hr = MfMediaEventCreate(type, extended, status, value, &event);
    if (FAILED(hr))
        return hr;
    hr = queue_queue_event(iface, event);
    event->lpVtbl->Release(event);
    return hr;
}

static HRESULT STDMETHODCALLTYPE queue_queue_event_param_unk(IMFMediaEventQueue *iface, MediaEventType type,
                                                             REFGUID extended, HRESULT status, IUnknown *object)
{
    PROPVARIANT value = {};
    value.vt = VT_UNKNOWN;
    value.punkVal = object;
    return queue_queue_event_param_var(iface, type, extended, status, &value);
}

/* drops the events and a pending callback without calling it, and wakes GetEvent. */
static HRESULT STDMETHODCALLTYPE queue_shutdown(IMFMediaEventQueue *iface)
{
    EventQueue *queue = event_queue(iface);
    EnterCriticalSection(&queue->lock);
    flush(queue);
    queue->shutdown = TRUE;
    WakeAllConditionVariable(&queue->arrived);
    LeaveCriticalSection(&queue->lock);
    return S_OK;
}

static const IMFMediaEventQueueVtbl EVENT_QUEUE_VTBL = {
    queue_query_interface,       queue_add_ref,
    queue_release,               queue_get_event,
    queue_begin_get_event,       queue_end_get_event,
    queue_queue_event,           queue_queue_event_param_var,
    queue_queue_event_param_unk, queue_shutdown,
};

HRESULT WINAPI MFCreateEventQueue(IMFMediaEventQueue **out)
{
    if (!out)
        return E_POINTER;
    EventQueue *queue = MfAllocZero(sizeof(*queue));
    if (!queue)
        return E_OUTOFMEMORY;
    queue->iface.lpVtbl = &EVENT_QUEUE_VTBL;
    queue->refs = 1;
    InitializeCriticalSection(&queue->lock);
    InitializeConditionVariable(&queue->arrived);
    *out = &queue->iface;
    return S_OK;
}
