/*
 * the video sample of MFCreateVideoSampleFromSurface, evr's CMFVideoSample: evr's own sample class, not mfplat's, with
 * IMFDesiredSample and IMFTrackedSample, and the buffer that only carries the surface. Read from evr.dll of windows 11
 * and measured with evrsamp.exe.
 */
#include "evr_private.h"

#define MF_E_NOTACCEPTING_ ((HRESULT)0xC00D36B5L)
#define MF_E_NOT_AVAILABLE_ ((HRESULT)0xC00D36D6L)
#define MF_E_NO_SAMPLE_TIMESTAMP_ ((HRESULT)0xC00D36C8L)
#define MF_E_NO_SAMPLE_DURATION_ ((HRESULT)0xC00D36C9L)
#define MF_E_BUFFERTOOSMALL_ ((HRESULT)0xC00D36B1L)

/* ------------------------------------------------------------------ the work queue */

typedef struct WorkItem {
    struct WorkItem *next;
    IMFAsyncCallback *callback;
    IMFAsyncResult *result;
} WorkItem;

static CRITICAL_SECTION g_queue_lock;
static INIT_ONCE g_queue_once = INIT_ONCE_STATIC_INIT;
static LONG g_queue_users;
static HANDLE g_queue_thread, g_queue_wake, g_queue_quit;
static WorkItem *g_queue_head, *g_queue_tail;

static BOOL CALLBACK queue_init(INIT_ONCE *once, void *parameter, void **context)
{
    InitializeCriticalSection(&g_queue_lock);
    return TRUE;
}

static WorkItem *queue_take(void)
{
    EnterCriticalSection(&g_queue_lock);
    WorkItem *item = g_queue_head;
    if (item) {
        g_queue_head = item->next;
        if (!g_queue_head)
            g_queue_tail = nullptr;
    }
    LeaveCriticalSection(&g_queue_lock);
    return item;
}

static DWORD WINAPI queue_thread(void *parameter)
{
    HANDLE events[2] = {g_queue_quit, g_queue_wake};
    while (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        WorkItem *item;
        while ((item = queue_take())) {
            item->callback->lpVtbl->Invoke(item->callback, item->result);
            item->result->lpVtbl->Release(item->result);
            item->callback->lpVtbl->Release(item->callback);
            HeapFree(GetProcessHeap(), 0, item);
        }
    }
    CloseHandle(events[0]);
    CloseHandle(events[1]);
    return 0;
}

/* every sample holds the queue, and the thread ends with the last one. */
HRESULT EvrWorkQueueStart(void)
{
    InitOnceExecuteOnce(&g_queue_once, queue_init, nullptr, nullptr);
    HRESULT hr = S_OK;
    EnterCriticalSection(&g_queue_lock);
    g_queue_users++;
    if (!g_queue_thread) {
        g_queue_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_queue_quit = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (g_queue_wake && g_queue_quit)
            g_queue_thread = CreateThread(nullptr, 0, queue_thread, nullptr, 0, nullptr);
        if (!g_queue_thread) {
            if (g_queue_wake)
                CloseHandle(g_queue_wake);
            if (g_queue_quit)
                CloseHandle(g_queue_quit);
            g_queue_wake = g_queue_quit = nullptr;
            hr = E_OUTOFMEMORY;
        }
    }
    LeaveCriticalSection(&g_queue_lock);
    return hr;
}

void EvrWorkQueueStop(void)
{
    EnterCriticalSection(&g_queue_lock);
    if (!--g_queue_users && g_queue_thread) {
        WorkItem *item = g_queue_head;
        g_queue_head = g_queue_tail = nullptr;
        while (item) {
            WorkItem *next = item->next;
            item->result->lpVtbl->Release(item->result);
            item->callback->lpVtbl->Release(item->callback);
            HeapFree(GetProcessHeap(), 0, item);
            item = next;
        }
        SetEvent(g_queue_quit);
        CloseHandle(g_queue_thread);
        g_queue_thread = g_queue_wake = g_queue_quit = nullptr;
    }
    LeaveCriticalSection(&g_queue_lock);
}

HRESULT EvrWorkQueuePut(IMFAsyncCallback *callback, IMFAsyncResult *result)
{
    WorkItem *item = HeapAlloc(GetProcessHeap(), 0, sizeof(*item));
    if (!item)
        return E_OUTOFMEMORY;
    item->next = nullptr;
    item->callback = callback;
    item->result = result;
    callback->lpVtbl->AddRef(callback);
    result->lpVtbl->AddRef(result);
    EnterCriticalSection(&g_queue_lock);
    if (g_queue_tail)
        g_queue_tail->next = item;
    else
        g_queue_head = item;
    g_queue_tail = item;
    if (g_queue_wake)
        SetEvent(g_queue_wake);
    LeaveCriticalSection(&g_queue_lock);
    return S_OK;
}

/* ------------------------------------------------------------------ the async result */

typedef struct {
    IMFAsyncResult iface;
    LONG refs;
    IUnknown *state, *object;
    IMFAsyncCallback *callback;
    HRESULT status;
} AsyncResult;

static AsyncResult *async_result(IMFAsyncResult *iface)
{
    return CONTAINING_RECORD(iface, AsyncResult, iface);
}

static HRESULT STDMETHODCALLTYPE result_query_interface(IMFAsyncResult *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
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
    AsyncResult *r = async_result(iface);
    ULONG refs = InterlockedDecrement(&r->refs);
    if (!refs) {
        if (r->object)
            r->object->lpVtbl->Release(r->object);
        if (r->state)
            r->state->lpVtbl->Release(r->state);
        if (r->callback)
            r->callback->lpVtbl->Release(r->callback);
        HeapFree(GetProcessHeap(), 0, r);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE result_get_state(IMFAsyncResult *iface, IUnknown **state)
{
    AsyncResult *r = async_result(iface);
    if (!r->state || !state)
        return E_POINTER;
    *state = r->state;
    r->state->lpVtbl->AddRef(r->state);
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
    AsyncResult *r = async_result(iface);
    if (!object || !r->object)
        return E_POINTER;
    *object = r->object;
    r->object->lpVtbl->AddRef(r->object);
    return S_OK;
}

static IUnknown *STDMETHODCALLTYPE result_get_state_no_add_ref(IMFAsyncResult *iface)
{
    return async_result(iface)->state;
}

static const IMFAsyncResultVtbl RESULT_VTBL = {
    result_query_interface, result_add_ref,    result_release,
    result_get_state,       result_get_status, result_set_status,
    result_get_object,      result_get_state_no_add_ref,
};

/* ------------------------------------------------------------------ the surface buffer */

typedef struct {
    IMFMediaBuffer iface;
    IMFGetService service;
    LONG refs;
    DWORD length;
    IUnknown *surface;
} SurfaceBuffer;

static SurfaceBuffer *surface_buffer(IMFMediaBuffer *iface)
{
    return CONTAINING_RECORD(iface, SurfaceBuffer, iface);
}

static HRESULT STDMETHODCALLTYPE sb_query_interface(IMFMediaBuffer *iface, REFIID riid, void **out)
{
    SurfaceBuffer *b = surface_buffer(iface);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFMediaBuffer))
        *out = &b->iface;
    else if (IsEqualGUID(riid, &IID_IMFGetService))
        *out = &b->service;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&b->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE sb_add_ref(IMFMediaBuffer *iface)
{
    return InterlockedIncrement(&surface_buffer(iface)->refs);
}

static ULONG STDMETHODCALLTYPE sb_release(IMFMediaBuffer *iface)
{
    SurfaceBuffer *b = surface_buffer(iface);
    ULONG refs = InterlockedDecrement(&b->refs);
    if (!refs) {
        if (b->surface)
            b->surface->lpVtbl->Release(b->surface);
        HeapFree(GetProcessHeap(), 0, b);
        EvrObjectDestroyed();
    }
    return refs;
}

/* the memory of the surface is not reachable through this buffer. */
static HRESULT STDMETHODCALLTYPE sb_lock(IMFMediaBuffer *iface, BYTE **data, DWORD *max_length, DWORD *current_length)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sb_unlock(IMFMediaBuffer *iface)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sb_get_current_length(IMFMediaBuffer *iface, DWORD *length)
{
    if (!length)
        return E_POINTER;
    *length = surface_buffer(iface)->length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sb_set_current_length(IMFMediaBuffer *iface, DWORD length)
{
    surface_buffer(iface)->length = length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sb_get_max_length(IMFMediaBuffer *iface, DWORD *length)
{
    return E_NOTIMPL;
}

static const IMFMediaBufferVtbl SURFACE_BUFFER_VTBL = {
    sb_query_interface,     sb_add_ref,
    sb_release,             sb_lock,
    sb_unlock,              sb_get_current_length,
    sb_set_current_length,  sb_get_max_length,
};

static SurfaceBuffer *surface_buffer_of_service(IMFGetService *iface)
{
    return CONTAINING_RECORD(iface, SurfaceBuffer, service);
}

static HRESULT STDMETHODCALLTYPE sbs_query_interface(IMFGetService *iface, REFIID riid, void **out)
{
    return sb_query_interface(&surface_buffer_of_service(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE sbs_add_ref(IMFGetService *iface)
{
    return sb_add_ref(&surface_buffer_of_service(iface)->iface);
}

static ULONG STDMETHODCALLTYPE sbs_release(IMFGetService *iface)
{
    return sb_release(&surface_buffer_of_service(iface)->iface);
}

static HRESULT STDMETHODCALLTYPE sbs_get_service(IMFGetService *iface, REFGUID service, REFIID riid, void **out)
{
    SurfaceBuffer *b = surface_buffer_of_service(iface);
    if (!IsEqualGUID(service, &MR_BUFFER_SERVICE))
        return MF_E_UNSUPPORTED_SERVICE;
    return b->surface->lpVtbl->QueryInterface(b->surface, riid, out);
}

static const IMFGetServiceVtbl SURFACE_SERVICE_VTBL = {
    sbs_query_interface,
    sbs_add_ref,
    sbs_release,
    sbs_get_service,
};

/* ------------------------------------------------------------------ the sample */

typedef struct {
    IMFSample iface;
    IMFDesiredSample desired;
    IMFTrackedSample tracked;
    LONG refs;
    IMFAttributes *attributes;
    DWORD flags;
    /* bit 0 says the time was set, bit 4 the duration. */
    DWORD has;
    LONGLONG time, duration;
    IMFMediaBuffer **buffers;
    DWORD count, capacity;
    BOOL has_desired;
    LONGLONG desired_time, desired_duration;
    /* the result handed to the allocator when the references fall to notify_at. */
    IMFAsyncResult *allocator;
    LONG notify_at;
} VideoSample;

static VideoSample *video_sample(IMFSample *iface)
{
    return CONTAINING_RECORD(iface, VideoSample, iface);
}

static IMFAttributes *sample_store(IMFSample *iface)
{
    return video_sample(iface)->attributes;
}

EVR_ATTRIBUTE_FORWARDERS(sample_attr, IMFSample, sample_store)

static HRESULT STDMETHODCALLTYPE sample_query_interface(IMFSample *iface, REFIID riid, void **out)
{
    VideoSample *s = video_sample(iface);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IMFSample) || IsEqualGUID(riid, &IID_IMFAttributes) || IsEqualGUID(riid, &IID_IUnknown))
        *out = &s->iface;
    else if (IsEqualGUID(riid, &IID_IMFTrackedSample))
        *out = &s->tracked;
    else if (IsEqualGUID(riid, &IID_IMFDesiredSample))
        *out = &s->desired;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&s->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE sample_add_ref(IMFSample *iface)
{
    return InterlockedIncrement(&video_sample(iface)->refs);
}

static void sample_destroy(VideoSample *s)
{
    if (s->allocator)
        s->allocator->lpVtbl->Release(s->allocator);
    for (DWORD i = 0; i < s->count; i++)
        s->buffers[i]->lpVtbl->Release(s->buffers[i]);
    HeapFree(GetProcessHeap(), 0, s->buffers);
    if (s->attributes)
        s->attributes->lpVtbl->Release(s->attributes);
    HeapFree(GetProcessHeap(), 0, s);
    EvrWorkQueueStop();
    EvrObjectDestroyed();
}

/* the allocator gets the sample back on the work queue when only its own references are left. */
static ULONG STDMETHODCALLTYPE sample_release(IMFSample *iface)
{
    VideoSample *s = video_sample(iface);
    LONG notify_at = s->notify_at;
    LONG refs = InterlockedDecrement(&s->refs);
    if (refs == notify_at) {
        IMFAsyncResult *result = InterlockedExchangePointer((void **)&s->allocator, nullptr);
        if (result) {
            EvrWorkQueuePut(async_result(result)->callback, result);
            result->lpVtbl->Release(result);
        }
        return (ULONG)refs;
    }
    if (!refs)
        sample_destroy(s);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE sample_get_flags(IMFSample *iface, DWORD *flags)
{
    if (!flags)
        return E_INVALIDARG;
    *flags = video_sample(iface)->flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_set_flags(IMFSample *iface, DWORD flags)
{
    video_sample(iface)->flags = flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_get_time(IMFSample *iface, LONGLONG *time)
{
    VideoSample *s = video_sample(iface);
    if (!time)
        return E_INVALIDARG;
    if (!(s->has & 1))
        return MF_E_NO_SAMPLE_TIMESTAMP_;
    *time = s->time;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_set_time(IMFSample *iface, LONGLONG time)
{
    VideoSample *s = video_sample(iface);
    s->has |= 1;
    s->time = time;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_get_duration(IMFSample *iface, LONGLONG *duration)
{
    VideoSample *s = video_sample(iface);
    if (!duration)
        return E_INVALIDARG;
    if (!(s->has & 0x10))
        return MF_E_NO_SAMPLE_DURATION_;
    *duration = s->duration;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_set_duration(IMFSample *iface, LONGLONG duration)
{
    VideoSample *s = video_sample(iface);
    s->has |= 0x10;
    s->duration = duration;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_get_buffer_count(IMFSample *iface, DWORD *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = video_sample(iface)->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_get_buffer_by_index(IMFSample *iface, DWORD index, IMFMediaBuffer **buffer)
{
    VideoSample *s = video_sample(iface);
    if (index >= s->count || !buffer)
        return E_INVALIDARG;
    *buffer = s->buffers[index];
    (*buffer)->lpVtbl->AddRef(*buffer);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_add_buffer(IMFSample *iface, IMFMediaBuffer *buffer)
{
    VideoSample *s = video_sample(iface);
    if (!buffer)
        return E_INVALIDARG;
    if (s->count >= s->capacity) {
        DWORD capacity = s->count + 3;
        IMFMediaBuffer **buffers = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*buffers));
        if (!buffers)
            return E_OUTOFMEMORY;
        if (s->count)
            memcpy(buffers, s->buffers, s->count * sizeof(*buffers));
        HeapFree(GetProcessHeap(), 0, s->buffers);
        s->buffers = buffers;
        s->capacity = capacity;
    }
    s->buffers[s->count++] = buffer;
    buffer->lpVtbl->AddRef(buffer);
    return S_OK;
}

/* read from evr: the length of the buffer is asked first, and a failure keeps it in the sample. */
static HRESULT STDMETHODCALLTYPE sample_remove_buffer_by_index(IMFSample *iface, DWORD index)
{
    VideoSample *s = video_sample(iface);
    if (index >= s->count)
        return E_INVALIDARG;
    IMFMediaBuffer *buffer = s->buffers[index];
    DWORD length = 0;
    HRESULT hr = buffer->lpVtbl->GetCurrentLength(buffer, &length);
    if (FAILED(hr))
        return hr;
    memmove(&s->buffers[index], &s->buffers[index + 1], (s->count - index - 1) * sizeof(*s->buffers));
    s->count--;
    buffer->lpVtbl->Release(buffer);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sample_remove_all_buffers(IMFSample *iface)
{
    VideoSample *s = video_sample(iface);
    for (DWORD i = 0; i < s->count; i++)
        s->buffers[i]->lpVtbl->Release(s->buffers[i]);
    s->count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_get_total_length(IMFSample *iface, DWORD *length)
{
    VideoSample *s = video_sample(iface);
    if (!length)
        return E_INVALIDARG;
    DWORD total = 0;
    for (DWORD i = 0; i < s->count; i++) {
        DWORD current = 0;
        HRESULT hr = s->buffers[i]->lpVtbl->GetCurrentLength(s->buffers[i], &current);
        if (FAILED(hr))
            return hr;
        total += current;
    }
    *length = total;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_copy_to_buffer(IMFSample *iface, IMFMediaBuffer *dest)
{
    VideoSample *s = video_sample(iface);
    if (!dest)
        return E_INVALIDARG;
    BYTE *out = nullptr;
    DWORD current = 0, max = 0, total = 0;
    HRESULT hr = dest->lpVtbl->Lock(dest, &out, nullptr, &current);
    if (SUCCEEDED(hr))
        hr = dest->lpVtbl->GetMaxLength(dest, &max);
    if (SUCCEEDED(hr))
        hr = sample_get_total_length(iface, &total);
    if (SUCCEEDED(hr) && total > max)
        hr = MF_E_BUFFERTOOSMALL_;
    for (DWORD i = 0; SUCCEEDED(hr) && i < s->count; i++) {
        BYTE *data = nullptr;
        DWORD length = 0;
        hr = s->buffers[i]->lpVtbl->Lock(s->buffers[i], &data, nullptr, &length);
        if (FAILED(hr))
            break;
        memcpy(out, data, length);
        out += length;
        s->buffers[i]->lpVtbl->Unlock(s->buffers[i]);
    }
    if (SUCCEEDED(hr))
        hr = dest->lpVtbl->SetCurrentLength(dest, total);
    else
        dest->lpVtbl->SetCurrentLength(dest, 0);
    dest->lpVtbl->Unlock(dest);
    return hr;
}

/* a stereo multiview sample keeps its first view only. */
static BOOL stereo_multiview(VideoSample *s)
{
    UINT32 stereo = 0, format = 0;
    if (FAILED(s->attributes->lpVtbl->GetUINT32(s->attributes, &MFSampleExtension_3DVideo, &stereo)) || !stereo)
        return FALSE;
    if (FAILED(s->attributes->lpVtbl->GetUINT32(s->attributes, &MFSampleExtension_3DVideo_SampleFormat, &format)))
        format = 0;
    return format == 1;
}

/* no buffer gives no buffer and success, as evr does. */
static HRESULT STDMETHODCALLTYPE sample_convert_to_contiguous_buffer(IMFSample *iface, IMFMediaBuffer **out)
{
    VideoSample *s = video_sample(iface);
    IMFMediaBuffer *buffer = nullptr;
    HRESULT hr = S_OK;
    if (stereo_multiview(s)) {
        while (s->count > 1)
            if (FAILED(hr = sample_remove_buffer_by_index(iface, 1)))
                return hr;
    }
    if (s->count == 1) {
        buffer = s->buffers[0];
        buffer->lpVtbl->AddRef(buffer);
    } else if (s->count > 1) {
        DWORD total = 0;
        hr = sample_get_total_length(iface, &total);
        if (SUCCEEDED(hr))
            hr = MFCreateMemoryBuffer(total, &buffer);
        if (SUCCEEDED(hr))
            hr = sample_copy_to_buffer(iface, buffer);
        if (SUCCEEDED(hr))
            hr = sample_remove_all_buffers(iface);
        if (SUCCEEDED(hr))
            hr = sample_add_buffer(iface, buffer);
    }
    if (SUCCEEDED(hr) && out) {
        *out = buffer;
        return hr;
    }
    if (buffer)
        buffer->lpVtbl->Release(buffer);
    return hr;
}

static const IMFSampleVtbl SAMPLE_VTBL = {
    sample_query_interface,
    sample_add_ref,
    sample_release,
    EVR_ATTRIBUTE_METHODS(sample_attr),
    sample_get_flags,
    sample_set_flags,
    sample_get_time,
    sample_set_time,
    sample_get_duration,
    sample_set_duration,
    sample_get_buffer_count,
    sample_get_buffer_by_index,
    sample_convert_to_contiguous_buffer,
    sample_add_buffer,
    sample_remove_buffer_by_index,
    sample_remove_all_buffers,
    sample_get_total_length,
    sample_copy_to_buffer,
};

static VideoSample *of_desired(IMFDesiredSample *iface)
{
    return CONTAINING_RECORD(iface, VideoSample, desired);
}

static HRESULT STDMETHODCALLTYPE desired_query_interface(IMFDesiredSample *iface, REFIID riid, void **out)
{
    return sample_query_interface(&of_desired(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE desired_add_ref(IMFDesiredSample *iface)
{
    return sample_add_ref(&of_desired(iface)->iface);
}

static ULONG STDMETHODCALLTYPE desired_release(IMFDesiredSample *iface)
{
    return sample_release(&of_desired(iface)->iface);
}

static HRESULT STDMETHODCALLTYPE desired_get(IMFDesiredSample *iface, LONGLONG *time, LONGLONG *duration)
{
    VideoSample *s = of_desired(iface);
    if (!time || !duration)
        return E_POINTER;
    if (!s->has_desired)
        return MF_E_NOT_AVAILABLE_;
    *time = s->desired_time;
    *duration = s->desired_duration;
    return S_OK;
}

static void STDMETHODCALLTYPE desired_set(IMFDesiredSample *iface, LONGLONG time, LONGLONG duration)
{
    VideoSample *s = of_desired(iface);
    s->desired_time = time;
    s->desired_duration = duration;
    s->has_desired = TRUE;
}

/* forgets the desired times, the time, duration, flags and attributes, and keeps the buffers. */
static void STDMETHODCALLTYPE desired_clear(IMFDesiredSample *iface)
{
    VideoSample *s = of_desired(iface);
    s->has_desired = FALSE;
    s->has = 0;
    s->time = 0;
    s->duration = 0;
    sample_set_flags(&s->iface, 0);
    s->attributes->lpVtbl->DeleteAllItems(s->attributes);
}

static const IMFDesiredSampleVtbl DESIRED_VTBL = {
    desired_query_interface, desired_add_ref, desired_release, desired_get, desired_set, desired_clear,
};

static VideoSample *of_tracked(IMFTrackedSample *iface)
{
    return CONTAINING_RECORD(iface, VideoSample, tracked);
}

static HRESULT STDMETHODCALLTYPE tracked_query_interface(IMFTrackedSample *iface, REFIID riid, void **out)
{
    return sample_query_interface(&of_tracked(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE tracked_add_ref(IMFTrackedSample *iface)
{
    return sample_add_ref(&of_tracked(iface)->iface);
}

static ULONG STDMETHODCALLTYPE tracked_release(IMFTrackedSample *iface)
{
    return sample_release(&of_tracked(iface)->iface);
}

/*
 * the result names the IMFTrackedSample part as its object and holds a reference on it, so the allocator is called
 * when the references drop to that one, or to two when the state is the sample itself.
 */
static HRESULT STDMETHODCALLTYPE tracked_set_allocator(IMFTrackedSample *iface, IMFAsyncCallback *callback,
                                                       IUnknown *state)
{
    VideoSample *s = of_tracked(iface);
    if (!callback)
        return E_POINTER;
    if (s->allocator)
        return MF_E_NOTACCEPTING_;
    AsyncResult *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*r));
    if (!r)
        return E_OUTOFMEMORY;
    r->iface.lpVtbl = &RESULT_VTBL;
    r->refs = 1;
    r->state = state;
    r->object = (IUnknown *)&s->tracked;
    r->callback = callback;
    tracked_add_ref(iface);
    if (state)
        state->lpVtbl->AddRef(state);
    callback->lpVtbl->AddRef(callback);
    LONG notify_at = 1;
    HRESULT hr = S_OK;
    if (state) {
        IUnknown *unknown = nullptr;
        hr = state->lpVtbl->QueryInterface(state, &IID_IUnknown, (void **)&unknown);
        if (SUCCEEDED(hr)) {
            notify_at = unknown == (IUnknown *)&s->iface ? 2 : 1;
            unknown->lpVtbl->Release(unknown);
        }
    }
    if (SUCCEEDED(hr)) {
        s->notify_at = notify_at;
        s->allocator = &r->iface;
        return S_OK;
    }
    result_release(&r->iface);
    return hr;
}

static const IMFTrackedSampleVtbl TRACKED_VTBL = {
    tracked_query_interface,
    tracked_add_ref,
    tracked_release,
    tracked_set_allocator,
};

HRESULT EvrCreateVideoSample(IUnknown *surface, IMFSample **out)
{
    VideoSample *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->iface.lpVtbl = &SAMPLE_VTBL;
    s->desired.lpVtbl = &DESIRED_VTBL;
    s->tracked.lpVtbl = &TRACKED_VTBL;
    s->refs = 1;
    s->notify_at = -1;
    HRESULT hr = MFCreateAttributes(&s->attributes, 0);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, s);
        return hr;
    }
    EvrObjectCreated();
    EvrWorkQueueStart();
    if (surface) {
        SurfaceBuffer *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
        hr = E_OUTOFMEMORY;
        if (b) {
            b->iface.lpVtbl = &SURFACE_BUFFER_VTBL;
            b->service.lpVtbl = &SURFACE_SERVICE_VTBL;
            b->refs = 1;
            EvrObjectCreated();
            b->surface = surface;
            surface->lpVtbl->AddRef(surface);
            hr = sample_add_buffer(&s->iface, &b->iface);
            sb_release(&b->iface);
        }
        if (FAILED(hr))
            goto failed;
    }
    hr = E_INVALIDARG;
    if (!out)
        goto failed;
    *out = &s->iface;
    return S_OK;
failed:
    sample_release(&s->iface);
    return hr;
}

HRESULT WINAPI MFCreateVideoSampleFromSurface(IUnknown *surface, IMFSample **sample)
{
    return EvrCreateVideoSample(surface, sample);
}
