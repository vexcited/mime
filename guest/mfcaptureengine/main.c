/*
 * the capture engine over a media source: it starts the source for a preview and hands its frames to the preview
 * sink stream's callback, in the format the stream was given. a camera source converts frames itself when it can,
 * see mime/win32/mimecamera.h, so nothing is decoded or reshuffled here. recording and photos would need encoders
 * and a camera photo stream, and there are none.
 */
#define MIME_BUILD_MFCAPTUREENGINE
#include <mime/nt.h>
#include <mime/win32/winbase.h>
#include <mime/win32/mfapi.h>
#include <mime/win32/mfcaptureengine.h>
#include <mime/win32/mimecamera.h>

/* the compiler references it from code using floating point, without a crt to define it. */
int _fltused = 0x9875;

#define MAX_SINK_STREAMS 4

enum { ENGINE_NEW, ENGINE_INITIALIZING, ENGINE_READY, ENGINE_FAILED };

typedef struct Engine Engine;

/* a sink stream: the source stream it takes, its sample type and who gets the samples. */
typedef struct SinkStream {
    BOOL used;
    DWORD source_stream;
    IMFMediaType *type;
    GUID subtype;
    IMFCaptureEngineOnSampleCallback *callback;
} SinkStream;

typedef struct Sink {
    Engine *engine;
    SinkStream streams[MAX_SINK_STREAMS];
} Sink;

typedef struct PreviewSink {
    IMFCapturePreviewSink iface;
    Sink sink;
} PreviewSink;

typedef struct RecordSink {
    IMFCaptureRecordSink iface;
    Sink sink;
} RecordSink;

typedef struct PhotoSink {
    IMFCapturePhotoSink iface;
    Sink sink;
    IMFCaptureEngineOnSampleCallback *callback;
} PhotoSink;

/* receives source and stream events. detached when the engine goes, pending reads are then ignored. */
typedef struct EventHandler {
    IMFAsyncCallback iface;
    LONG refs;
    CRITICAL_SECTION lock;
    Engine *engine;
    BOOL stream;
} EventHandler;

/* events for the program's callback, delivered in order on a thread of their own. */
typedef struct Dispatch {
    IMFAsyncCallback iface;
    LONG refs;
    IMFCaptureEngineOnEventCallback *callback;
} Dispatch;

typedef struct CaptureSource {
    IMFCaptureSource iface;
    Engine *engine;
} CaptureSource;

struct Engine {
    IMFCaptureEngine iface;
    LONG refs;
    CRITICAL_SECTION lock;
    int state;
    IMFCaptureEngineOnEventCallback *event_callback;
    Dispatch *dispatch;
    DWORD queue;
    BOOL queue_held;
    IMFMediaSource *source;
    IMFActivate *activate;
    IMimeCaptureSource *mime;
    IMFPresentationDescriptor *descriptor;
    IMFMediaTypeHandler *handler;
    IMFMediaStream *stream;
    EventHandler *source_events;
    EventHandler *stream_events;
    BOOL previewing;
    BOOL starting;
    BOOL stopping;
    CaptureSource capture_source;
    PreviewSink preview;
    RecordSink record;
    PhotoSink photo;
};

static void *alloc_zero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

static void heap_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

static BOOL same_guid(const GUID *a, const GUID *b)
{
    return !memcmp(a, b, sizeof(GUID));
}

/* ------------------------------------------------------------------ events for the program */

static HRESULT STDMETHODCALLTYPE dispatch_query_interface(IMFAsyncCallback *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFAsyncCallback)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dispatch_add_ref(IMFAsyncCallback *iface)
{
    return (ULONG)InterlockedIncrement(&CONTAINING_RECORD(iface, Dispatch, iface)->refs);
}

static ULONG STDMETHODCALLTYPE dispatch_release(IMFAsyncCallback *iface)
{
    Dispatch *d = CONTAINING_RECORD(iface, Dispatch, iface);
    LONG refs = InterlockedDecrement(&d->refs);
    if (!refs) {
        d->callback->lpVtbl->Release(d->callback);
        heap_free(d);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE dispatch_get_parameters(IMFAsyncCallback *iface, DWORD *flags, DWORD *queue)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dispatch_invoke(IMFAsyncCallback *iface, IMFAsyncResult *result)
{
    Dispatch *d = CONTAINING_RECORD(iface, Dispatch, iface);
    IUnknown *state = nullptr;
    if (SUCCEEDED(result->lpVtbl->GetState(result, &state)) && state) {
        IMFMediaEvent *event = nullptr;
        if (SUCCEEDED(state->lpVtbl->QueryInterface(state, &IID_IMFMediaEvent, (void **)&event))) {
            d->callback->lpVtbl->OnEvent(d->callback, event);
            event->lpVtbl->Release(event);
        }
        state->lpVtbl->Release(state);
    }
    return S_OK;
}

static const IMFAsyncCallbackVtbl DISPATCH_VTBL = {
    dispatch_query_interface, dispatch_add_ref, dispatch_release, dispatch_get_parameters, dispatch_invoke,
};

/* the event type is the extended one, the status is the outcome. */
static void post_event(Engine *e, REFGUID kind, HRESULT status, DWORD stream_index)
{
    Dispatch *d;
    DWORD queue;
    EnterCriticalSection(&e->lock);
    d = e->dispatch;
    queue = e->queue;
    if (d)
        InterlockedIncrement(&d->refs);
    LeaveCriticalSection(&e->lock);
    if (!d)
        return;
    IMFMediaEvent *event = nullptr;
    PROPVARIANT value = {.vt = VT_EMPTY};
    if (SUCCEEDED(MFCreateMediaEvent(MEExtendedType, kind, status, &value, &event))) {
        if (stream_index != ~0u)
            ((IMFAttributes *)event)->lpVtbl->SetUINT32((IMFAttributes *)event, &MF_CAPTURE_ENGINE_EVENT_STREAM_INDEX,
                                                         stream_index);
        MFPutWorkItem(queue, &d->iface, (IUnknown *)event);
        event->lpVtbl->Release(event);
    }
    d->iface.lpVtbl->Release(&d->iface);
}

/* ------------------------------------------------------------------ the source's events */

static void engine_destroy(Engine *e);

static void engine_hold(Engine *e)
{
    InterlockedIncrement(&e->refs);
}

static void engine_drop(Engine *e)
{
    if (!InterlockedDecrement(&e->refs))
        engine_destroy(e);
}

static HRESULT STDMETHODCALLTYPE handler_query_interface(IMFAsyncCallback *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFAsyncCallback)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE handler_add_ref(IMFAsyncCallback *iface)
{
    return (ULONG)InterlockedIncrement(&CONTAINING_RECORD(iface, EventHandler, iface)->refs);
}

static ULONG STDMETHODCALLTYPE handler_release(IMFAsyncCallback *iface)
{
    EventHandler *h = CONTAINING_RECORD(iface, EventHandler, iface);
    LONG refs = InterlockedDecrement(&h->refs);
    if (!refs) {
        DeleteCriticalSection(&h->lock);
        heap_free(h);
    }
    return (ULONG)refs;
}

/* events are read on threads that may block, the program's callbacks run on them. */
static HRESULT STDMETHODCALLTYPE handler_get_parameters(IMFAsyncCallback *iface, DWORD *flags, DWORD *queue)
{
    *flags = 0;
    *queue = MFASYNC_CALLBACK_QUEUE_MULTITHREADED;
    return S_OK;
}

static void source_event(Engine *e, IMFMediaEvent *event)
{
    MediaEventType type = MEUnknown;
    HRESULT status = S_OK;
    PROPVARIANT value = {};
    event->lpVtbl->GetType(event, &type);
    event->lpVtbl->GetStatus(event, &status);
    event->lpVtbl->GetValue(event, &value);
    if ((type == MENewStream || type == MEUpdatedStream) && value.vt == VT_UNKNOWN && value.punkVal) {
        IMFMediaStream *stream = nullptr;
        if (SUCCEEDED(value.punkVal->lpVtbl->QueryInterface(value.punkVal, &IID_IMFMediaStream, (void **)&stream))) {
            EnterCriticalSection(&e->lock);
            BOOL attach = e->stream != stream;
            if (attach) {
                if (e->stream)
                    e->stream->lpVtbl->Release(e->stream);
                e->stream = stream;
                stream->lpVtbl->AddRef(stream);
            }
            EventHandler *h = e->stream_events;
            if (h)
                InterlockedIncrement(&h->refs);
            LeaveCriticalSection(&e->lock);
            if (attach && h)
                stream->lpVtbl->BeginGetEvent(stream, &h->iface, nullptr);
            if (h)
                h->iface.lpVtbl->Release(&h->iface);
            stream->lpVtbl->Release(stream);
        }
    } else if (type == MESourceStarted) {
        EnterCriticalSection(&e->lock);
        BOOL was_starting = e->starting;
        e->starting = FALSE;
        if (was_starting && SUCCEEDED(status))
            e->previewing = TRUE;
        LeaveCriticalSection(&e->lock);
        if (was_starting)
            post_event(e, &MF_CAPTURE_ENGINE_PREVIEW_STARTED, status, ~0u);
    } else if (type == MESourceStopped) {
        EnterCriticalSection(&e->lock);
        BOOL was_stopping = e->stopping;
        e->stopping = FALSE;
        e->previewing = FALSE;
        LeaveCriticalSection(&e->lock);
        if (was_stopping)
            post_event(e, &MF_CAPTURE_ENGINE_PREVIEW_STOPPED, status, ~0u);
    } else if (type == MEError) {
        EnterCriticalSection(&e->lock);
        BOOL was_starting = e->starting;
        e->starting = FALSE;
        e->state = ENGINE_FAILED;
        LeaveCriticalSection(&e->lock);
        /* like windows, a camera that cannot start is reported started first, then the error follows. */
        if (was_starting)
            post_event(e, &MF_CAPTURE_ENGINE_PREVIEW_STARTED, S_OK, ~0u);
        post_event(e, &MF_CAPTURE_ENGINE_ERROR, FAILED(status) ? status : E_FAIL, ~0u);
    }
    PropVariantClear(&value);
}

static void stream_event(Engine *e, IMFMediaEvent *event)
{
    MediaEventType type = MEUnknown;
    HRESULT status = S_OK;
    PROPVARIANT value = {};
    event->lpVtbl->GetType(event, &type);
    event->lpVtbl->GetStatus(event, &status);
    event->lpVtbl->GetValue(event, &value);
    if (type == MEMediaSample && value.vt == VT_UNKNOWN && value.punkVal) {
        IMFSample *sample = nullptr;
        IMFCaptureEngineOnSampleCallback *callback = nullptr;
        EnterCriticalSection(&e->lock);
        if (e->previewing || e->starting) {
            SinkStream *s = &e->preview.sink.streams[0];
            for (DWORD i = 0; i < MAX_SINK_STREAMS; i++)
                if (e->preview.sink.streams[i].used && e->preview.sink.streams[i].callback) {
                    s = &e->preview.sink.streams[i];
                    break;
                }
            callback = s->callback;
            if (callback)
                callback->lpVtbl->AddRef(callback);
        }
        LeaveCriticalSection(&e->lock);
        if (callback && SUCCEEDED(value.punkVal->lpVtbl->QueryInterface(value.punkVal, &IID_IMFSample, (void **)&sample))) {
            callback->lpVtbl->OnSample(callback, sample);
            sample->lpVtbl->Release(sample);
        }
        if (callback)
            callback->lpVtbl->Release(callback);
    } else if (type == MEError) {
        EnterCriticalSection(&e->lock);
        e->state = ENGINE_FAILED;
        LeaveCriticalSection(&e->lock);
        post_event(e, &MF_CAPTURE_ENGINE_ERROR, FAILED(status) ? status : E_FAIL, ~0u);
    }
    PropVariantClear(&value);
}

static HRESULT STDMETHODCALLTYPE handler_invoke(IMFAsyncCallback *iface, IMFAsyncResult *result)
{
    EventHandler *h = CONTAINING_RECORD(iface, EventHandler, iface);
    Engine *e;
    IMFMediaEventGenerator *generator = nullptr;
    EnterCriticalSection(&h->lock);
    e = h->engine;
    if (e) {
        engine_hold(e);
        generator = h->stream ? (IMFMediaEventGenerator *)e->stream : (IMFMediaEventGenerator *)e->source;
        if (generator)
            generator->lpVtbl->AddRef(generator);
    }
    LeaveCriticalSection(&h->lock);
    if (!e)
        return S_OK;
    IMFMediaEvent *event = nullptr;
    if (generator && SUCCEEDED(generator->lpVtbl->EndGetEvent(generator, result, &event))) {
        if (h->stream)
            stream_event(e, event);
        else
            source_event(e, event);
        event->lpVtbl->Release(event);
        /* read the next event, unless the stream was replaced. */
        EnterCriticalSection(&h->lock);
        BOOL again = h->engine == e && (h->stream ? e->stream == (IMFMediaStream *)generator : TRUE);
        LeaveCriticalSection(&h->lock);
        if (again)
            generator->lpVtbl->BeginGetEvent(generator, iface, nullptr);
    }
    if (generator)
        generator->lpVtbl->Release(generator);
    engine_drop(e);
    return S_OK;
}

static const IMFAsyncCallbackVtbl HANDLER_VTBL = {
    handler_query_interface, handler_add_ref, handler_release, handler_get_parameters, handler_invoke,
};

static EventHandler *new_handler(Engine *e, BOOL stream)
{
    EventHandler *h = alloc_zero(sizeof(*h));
    if (!h)
        return nullptr;
    h->iface.lpVtbl = &HANDLER_VTBL;
    h->refs = 1;
    h->engine = e;
    h->stream = stream;
    InitializeCriticalSection(&h->lock);
    return h;
}

static void detach_handler(EventHandler *h)
{
    if (!h)
        return;
    EnterCriticalSection(&h->lock);
    h->engine = nullptr;
    LeaveCriticalSection(&h->lock);
    h->iface.lpVtbl->Release(&h->iface);
}

/* ------------------------------------------------------------------ the source of the engine */

static CaptureSource *csrc_of(IMFCaptureSource *iface)
{
    return CONTAINING_RECORD(iface, CaptureSource, iface);
}

static HRESULT STDMETHODCALLTYPE csrc_query_interface(IMFCaptureSource *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFCaptureSource)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE csrc_add_ref(IMFCaptureSource *iface)
{
    return (ULONG)InterlockedIncrement(&csrc_of(iface)->engine->refs);
}

static ULONG STDMETHODCALLTYPE csrc_release(IMFCaptureSource *iface)
{
    Engine *e = csrc_of(iface)->engine;
    LONG refs = InterlockedDecrement(&e->refs);
    if (!refs)
        engine_destroy(e);
    return (ULONG)refs;
}

/* the source's only stream index, or the special values meaning the preferred one. */
static BOOL resolve_stream(DWORD index, DWORD *resolved)
{
    if (index == 0 || index == MF_CAPTURE_ENGINE_FIRST_SOURCE_INDEX ||
        index == MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_VIDEO_PREVIEW ||
        index == MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_VIDEO_RECORD) {
        *resolved = 0;
        return TRUE;
    }
    return FALSE;
}

static HRESULT ready(Engine *e)
{
    HRESULT hr = e->state == ENGINE_READY ? S_OK : e->state == ENGINE_NEW ? MF_E_NOT_INITIALIZED : MF_E_INVALIDREQUEST;
    return hr;
}

static HRESULT STDMETHODCALLTYPE csrc_get_device_source(IMFCaptureSource *iface, MF_CAPTURE_ENGINE_DEVICE_TYPE type,
                                                        IMFMediaSource **source)
{
    Engine *e = csrc_of(iface)->engine;
    if (!source)
        return E_POINTER;
    *source = nullptr;
    if (type != MF_CAPTURE_ENGINE_DEVICE_TYPE_VIDEO && type != MF_CAPTURE_ENGINE_DEVICE_TYPE_AUDIO)
        return E_INVALIDARG;
    if (type == MF_CAPTURE_ENGINE_DEVICE_TYPE_AUDIO)
        return MF_E_CAPTURE_SOURCE_NO_AUDIO_STREAM_PRESENT;
    HRESULT hr = ready(e);
    if (SUCCEEDED(hr)) {
        *source = e->source;
        e->source->lpVtbl->AddRef(e->source);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE csrc_get_device_activate(IMFCaptureSource *iface, MF_CAPTURE_ENGINE_DEVICE_TYPE type,
                                                          IMFActivate **activate)
{
    Engine *e = csrc_of(iface)->engine;
    if (!activate)
        return E_POINTER;
    *activate = nullptr;
    if (type == MF_CAPTURE_ENGINE_DEVICE_TYPE_AUDIO)
        return MF_E_CAPTURE_SOURCE_NO_AUDIO_STREAM_PRESENT;
    HRESULT hr = ready(e);
    if (FAILED(hr))
        return hr;
    if (!e->activate)
        return MF_E_NOT_AVAILABLE;
    *activate = e->activate;
    e->activate->lpVtbl->AddRef(e->activate);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE csrc_get_service(IMFCaptureSource *iface, REFGUID service, REFIID riid, IUnknown **object)
{
    Engine *e = csrc_of(iface)->engine;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    HRESULT hr = ready(e);
    if (FAILED(hr))
        return hr;
    IMFGetService *get_service = nullptr;
    if (FAILED(e->source->lpVtbl->QueryInterface(e->source, &IID_IMFGetService, (void **)&get_service)))
        return MF_E_UNSUPPORTED_SERVICE;
    hr = get_service->lpVtbl->GetService(get_service, service, riid, (void **)object);
    get_service->lpVtbl->Release(get_service);
    return hr;
}

/* effects are transforms, of which there are none. */
static HRESULT STDMETHODCALLTYPE csrc_add_effect(IMFCaptureSource *iface, DWORD stream, IUnknown *effect)
{
    DWORD resolved;
    if (!effect)
        return E_INVALIDARG;
    return resolve_stream(stream, &resolved) ? MF_E_INVALIDMEDIATYPE : MF_E_INVALIDSTREAMNUMBER;
}

static HRESULT STDMETHODCALLTYPE csrc_remove_effect(IMFCaptureSource *iface, DWORD stream, IUnknown *effect)
{
    DWORD resolved;
    if (!effect)
        return E_INVALIDARG;
    return resolve_stream(stream, &resolved) ? MF_E_NOT_FOUND : MF_E_INVALIDSTREAMNUMBER;
}

static HRESULT STDMETHODCALLTYPE csrc_remove_all_effects(IMFCaptureSource *iface, DWORD stream)
{
    DWORD resolved;
    return resolve_stream(stream, &resolved) ? S_OK : MF_E_INVALIDSTREAMNUMBER;
}

static HRESULT STDMETHODCALLTYPE csrc_get_available_type(IMFCaptureSource *iface, DWORD stream, DWORD index, IMFMediaType **type)
{
    Engine *e = csrc_of(iface)->engine;
    if (!type)
        return E_POINTER;
    *type = nullptr;
    HRESULT hr = ready(e);
    DWORD resolved;
    if (FAILED(hr))
        return hr;
    if (!resolve_stream(stream, &resolved))
        return MF_E_INVALIDSTREAMNUMBER;
    DWORD count = 0;
    IMFMediaType *native = nullptr;
    e->handler->lpVtbl->GetMediaTypeCount(e->handler, &count);
    if (index >= count)
        return MF_E_NO_MORE_TYPES;
    hr = e->handler->lpVtbl->GetMediaTypeByIndex(e->handler, index, &native);
    if (FAILED(hr))
        return hr;
    /* a copy, so a program that changes it leaves the camera's types alone. */
    hr = MFCreateMediaType(type);
    if (SUCCEEDED(hr))
        hr = ((IMFAttributes *)native)->lpVtbl->CopyAllItems((IMFAttributes *)native, (IMFAttributes *)*type);
    if (FAILED(hr) && *type) {
        (*type)->lpVtbl->Release(*type);
        *type = nullptr;
    }
    native->lpVtbl->Release(native);
    return hr;
}

static void restart_preview(Engine *e);

static HRESULT STDMETHODCALLTYPE csrc_set_current_type(IMFCaptureSource *iface, DWORD stream, IMFMediaType *type)
{
    Engine *e = csrc_of(iface)->engine;
    DWORD resolved;
    if (!type)
        return E_POINTER;
    HRESULT hr = ready(e);
    if (FAILED(hr))
        return hr;
    if (!resolve_stream(stream, &resolved))
        return MF_E_INVALIDSTREAMNUMBER;
    IMFMediaType *closest = nullptr;
    hr = e->handler->lpVtbl->IsMediaTypeSupported(e->handler, type, &closest);
    if (closest)
        closest->lpVtbl->Release(closest);
    if (FAILED(hr))
        return MF_E_INVALIDMEDIATYPE;
    hr = e->handler->lpVtbl->SetCurrentMediaType(e->handler, type);
    if (FAILED(hr))
        return hr;
    EnterCriticalSection(&e->lock);
    BOOL running = e->previewing;
    LeaveCriticalSection(&e->lock);
    if (running)
        restart_preview(e);
    post_event(e, &MF_CAPTURE_SOURCE_CURRENT_DEVICE_MEDIA_TYPE_SET, S_OK, 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE csrc_get_current_type(IMFCaptureSource *iface, DWORD stream, IMFMediaType **type)
{
    Engine *e = csrc_of(iface)->engine;
    DWORD resolved;
    if (!type)
        return E_POINTER;
    *type = nullptr;
    HRESULT hr = ready(e);
    if (FAILED(hr))
        return hr;
    if (!resolve_stream(stream, &resolved))
        return MF_E_INVALIDSTREAMNUMBER;
    IMFMediaType *current = nullptr;
    hr = e->handler->lpVtbl->GetCurrentMediaType(e->handler, &current);
    if (FAILED(hr))
        return hr;
    hr = MFCreateMediaType(type);
    if (SUCCEEDED(hr))
        hr = ((IMFAttributes *)current)->lpVtbl->CopyAllItems((IMFAttributes *)current, (IMFAttributes *)*type);
    current->lpVtbl->Release(current);
    if (FAILED(hr) && *type) {
        (*type)->lpVtbl->Release(*type);
        *type = nullptr;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE csrc_get_stream_count(IMFCaptureSource *iface, DWORD *count)
{
    Engine *e = csrc_of(iface)->engine;
    if (!count)
        return E_POINTER;
    *count = 0;
    HRESULT hr = ready(e);
    if (SUCCEEDED(hr))
        *count = 1;
    return hr;
}

/* a camera's one stream is the preview stream. */
static HRESULT STDMETHODCALLTYPE csrc_get_stream_category(IMFCaptureSource *iface, DWORD stream,
                                                          MF_CAPTURE_ENGINE_STREAM_CATEGORY *category)
{
    Engine *e = csrc_of(iface)->engine;
    if (!category)
        return E_POINTER;
    HRESULT hr = ready(e);
    if (FAILED(hr))
        return hr;
    if (stream != 0)
        return MF_E_INVALIDSTREAMNUMBER;
    *category = MF_CAPTURE_ENGINE_STREAM_CATEGORY_VIDEO_PREVIEW;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE csrc_get_mirror(IMFCaptureSource *iface, DWORD stream, BOOL *mirror)
{
    if (!mirror)
        return E_POINTER;
    *mirror = FALSE;
    return stream == 0 ? S_OK : MF_E_INVALIDSTREAMNUMBER;
}

static HRESULT STDMETHODCALLTYPE csrc_set_mirror(IMFCaptureSource *iface, DWORD stream, BOOL mirror)
{
    if (stream != 0)
        return MF_E_INVALIDSTREAMNUMBER;
    return mirror ? E_NOTIMPL : S_OK;
}

static HRESULT STDMETHODCALLTYPE csrc_get_stream_from_name(IMFCaptureSource *iface, UINT32 name, DWORD *stream)
{
    if (!stream)
        return E_POINTER;
    *stream = 0;
    return MF_E_NOT_FOUND;
}

static const IMFCaptureSourceVtbl CAPTURE_SOURCE_VTBL = {
    csrc_query_interface, csrc_add_ref, csrc_release, csrc_get_device_source, csrc_get_device_activate,
    csrc_get_service, csrc_add_effect, csrc_remove_effect, csrc_remove_all_effects, csrc_get_available_type,
    csrc_set_current_type, csrc_get_current_type, csrc_get_stream_count, csrc_get_stream_category, csrc_get_mirror,
    csrc_set_mirror, csrc_get_stream_from_name,
};

/* ------------------------------------------------------------------ the sinks */

static HRESULT sink_query_interface(void *iface, Engine *e, REFIID riid, void **out, const GUID *own)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFCaptureSink) || IsEqualIID(riid, own)) {
        *out = iface;
        InterlockedIncrement(&e->refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG sink_add_ref(Engine *e)
{
    return (ULONG)InterlockedIncrement(&e->refs);
}

static ULONG sink_release(Engine *e)
{
    LONG refs = InterlockedDecrement(&e->refs);
    if (!refs)
        engine_destroy(e);
    return (ULONG)refs;
}

/* what a sink stream can be given depends on the camera's current type. */
static BOOL current_subtype(Engine *e, GUID *subtype)
{
    IMFMediaType *type = nullptr;
    if (FAILED(e->handler->lpVtbl->GetCurrentMediaType(e->handler, &type)))
        return FALSE;
    HRESULT hr = ((IMFAttributes *)type)->lpVtbl->GetGUID((IMFAttributes *)type, &MF_MT_SUBTYPE, subtype);
    type->lpVtbl->Release(type);
    return SUCCEEDED(hr);
}

static HRESULT can_deliver(Engine *e, const GUID *sink_subtype)
{
    GUID source_subtype;
    if (!current_subtype(e, &source_subtype))
        return MF_E_INVALIDMEDIATYPE;
    if (same_guid(&source_subtype, sink_subtype))
        return S_OK;
    return e->mime && e->mime->lpVtbl->CanConvertTo(e->mime, 0, &source_subtype, sink_subtype) == S_OK
               ? S_OK
               : MF_E_INVALIDMEDIATYPE;
}

static HRESULT sink_get_output_type(Sink *s, DWORD index, IMFMediaType **type)
{
    if (!type)
        return E_POINTER;
    *type = nullptr;
    if (index >= MAX_SINK_STREAMS || !s->streams[index].used)
        return MF_E_INVALIDSTREAMNUMBER;
    HRESULT hr = MFCreateMediaType(type);
    if (SUCCEEDED(hr))
        hr = ((IMFAttributes *)s->streams[index].type)->lpVtbl->CopyAllItems((IMFAttributes *)s->streams[index].type,
                                                                           (IMFAttributes *)*type);
    if (FAILED(hr) && *type) {
        (*type)->lpVtbl->Release(*type);
        *type = nullptr;
    }
    return hr;
}

static HRESULT sink_add_stream(Sink *s, DWORD source_stream, IMFMediaType *type, IMFAttributes *attributes, DWORD *sink_stream)
{
    Engine *e = s->engine;
    DWORD resolved;
    if (!type)
        return E_INVALIDARG;
    HRESULT hr = ready(e);
    if (FAILED(hr))
        return hr;
    if (!resolve_stream(source_stream, &resolved))
        return MF_E_INVALIDSTREAMNUMBER;
    EnterCriticalSection(&e->lock);
    BOOL busy = e->previewing || e->starting;
    LeaveCriticalSection(&e->lock);
    if (busy)
        return MF_E_CAPTURE_ENGINE_INVALID_OP;
    IMFAttributes *a = (IMFAttributes *)type;
    GUID major, subtype;
    if (FAILED(a->lpVtbl->GetGUID(a, &MF_MT_MAJOR_TYPE, &major)) || !same_guid(&major, &MFMediaType_Video) ||
        FAILED(a->lpVtbl->GetGUID(a, &MF_MT_SUBTYPE, &subtype)))
        return MF_E_INVALIDMEDIATYPE;
    /* a size other than the camera's would need scaling. */
    UINT64 size = 0;
    IMFMediaType *current = nullptr;
    if (SUCCEEDED(a->lpVtbl->GetUINT64(a, &MF_MT_FRAME_SIZE, &size)) &&
        SUCCEEDED(e->handler->lpVtbl->GetCurrentMediaType(e->handler, &current))) {
        UINT64 camera = 0;
        BOOL differs = SUCCEEDED(((IMFAttributes *)current)->lpVtbl->GetUINT64((IMFAttributes *)current, &MF_MT_FRAME_SIZE,
                                                                               &camera)) && camera != size;
        current->lpVtbl->Release(current);
        if (differs)
            return MF_E_INVALIDMEDIATYPE;
    }
    hr = can_deliver(e, &subtype);
    if (FAILED(hr))
        return hr;
    IMFMediaType *copy = nullptr;
    hr = MFCreateMediaType(&copy);
    if (SUCCEEDED(hr))
        hr = a->lpVtbl->CopyAllItems(a, (IMFAttributes *)copy);
    if (FAILED(hr)) {
        if (copy)
            copy->lpVtbl->Release(copy);
        return hr;
    }
    EnterCriticalSection(&e->lock);
    DWORD index = 0;
    while (index < MAX_SINK_STREAMS && s->streams[index].used)
        index++;
    if (index == MAX_SINK_STREAMS) {
        LeaveCriticalSection(&e->lock);
        copy->lpVtbl->Release(copy);
        return MF_E_CAPTURE_ENGINE_INVALID_OP;
    }
    s->streams[index].used = TRUE;
    s->streams[index].source_stream = resolved;
    s->streams[index].type = copy;
    s->streams[index].subtype = subtype;
    s->streams[index].callback = nullptr;
    LeaveCriticalSection(&e->lock);
    if (sink_stream)
        *sink_stream = index;
    return S_OK;
}

static HRESULT sink_remove_all(Sink *s)
{
    Engine *e = s->engine;
    EnterCriticalSection(&e->lock);
    if (e->previewing || e->starting) {
        LeaveCriticalSection(&e->lock);
        return MF_E_CAPTURE_ENGINE_INVALID_OP;
    }
    for (DWORD i = 0; i < MAX_SINK_STREAMS; i++) {
        if (!s->streams[i].used)
            continue;
        s->streams[i].used = FALSE;
        if (s->streams[i].type)
            s->streams[i].type->lpVtbl->Release(s->streams[i].type);
        if (s->streams[i].callback)
            s->streams[i].callback->lpVtbl->Release(s->streams[i].callback);
        s->streams[i].type = nullptr;
        s->streams[i].callback = nullptr;
    }
    LeaveCriticalSection(&e->lock);
    return S_OK;
}

static HRESULT sink_set_callback(Sink *s, DWORD index, IMFCaptureEngineOnSampleCallback *callback)
{
    Engine *e = s->engine;
    if (!callback)
        return E_INVALIDARG;
    EnterCriticalSection(&e->lock);
    if (index >= MAX_SINK_STREAMS || !s->streams[index].used) {
        LeaveCriticalSection(&e->lock);
        return MF_E_INVALIDSTREAMNUMBER;
    }
    IMFCaptureEngineOnSampleCallback *old = s->streams[index].callback;
    s->streams[index].callback = callback;
    callback->lpVtbl->AddRef(callback);
    LeaveCriticalSection(&e->lock);
    if (old)
        old->lpVtbl->Release(old);
    return S_OK;
}

/* the preview sink. */
#define PREVIEW(i) (CONTAINING_RECORD((i), PreviewSink, iface))

static HRESULT STDMETHODCALLTYPE pv_query_interface(IMFCapturePreviewSink *i, REFIID riid, void **out)
{
    return sink_query_interface(i, PREVIEW(i)->sink.engine, riid, out, &IID_IMFCapturePreviewSink);
}
static ULONG STDMETHODCALLTYPE pv_add_ref(IMFCapturePreviewSink *i)
{
    return sink_add_ref(PREVIEW(i)->sink.engine);
}
static ULONG STDMETHODCALLTYPE pv_release(IMFCapturePreviewSink *i)
{
    return sink_release(PREVIEW(i)->sink.engine);
}
static HRESULT STDMETHODCALLTYPE pv_get_output_type(IMFCapturePreviewSink *i, DWORD index, IMFMediaType **type)
{
    return sink_get_output_type(&PREVIEW(i)->sink, index, type);
}
static HRESULT STDMETHODCALLTYPE pv_get_service(IMFCapturePreviewSink *i, DWORD index, REFGUID service, REFIID riid,
                                                IUnknown **object)
{
    if (object)
        *object = nullptr;
    return MF_E_UNSUPPORTED_SERVICE;
}
static HRESULT STDMETHODCALLTYPE pv_add_stream(IMFCapturePreviewSink *i, DWORD source, IMFMediaType *type,
                                               IMFAttributes *attributes, DWORD *sink_stream)
{
    return sink_add_stream(&PREVIEW(i)->sink, source, type, attributes, sink_stream);
}
static HRESULT STDMETHODCALLTYPE pv_prepare(IMFCapturePreviewSink *i)
{
    Engine *e = PREVIEW(i)->sink.engine;
    HRESULT hr = ready(e);
    if (SUCCEEDED(hr))
        post_event(e, &MF_CAPTURE_SINK_PREPARED, S_OK, ~0u);
    return hr;
}
static HRESULT STDMETHODCALLTYPE pv_remove_all(IMFCapturePreviewSink *i)
{
    return sink_remove_all(&PREVIEW(i)->sink);
}
/* drawing into a window needs the video renderer, which does not exist. */
static HRESULT STDMETHODCALLTYPE pv_set_render_handle(IMFCapturePreviewSink *i, HANDLE handle)
{
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE pv_set_render_surface(IMFCapturePreviewSink *i, IUnknown *surface)
{
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE pv_update_video(IMFCapturePreviewSink *i, const MFVideoNormalizedRect *source,
                                                 const RECT *destination, const COLORREF *border)
{
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE pv_set_sample_callback(IMFCapturePreviewSink *i, DWORD index,
                                                        IMFCaptureEngineOnSampleCallback *callback)
{
    return sink_set_callback(&PREVIEW(i)->sink, index, callback);
}
static HRESULT STDMETHODCALLTYPE pv_get_mirror(IMFCapturePreviewSink *i, BOOL *mirror)
{
    if (!mirror)
        return E_POINTER;
    *mirror = FALSE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_set_mirror(IMFCapturePreviewSink *i, BOOL mirror)
{
    return mirror ? E_NOTIMPL : S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_rotation(IMFCapturePreviewSink *i, DWORD stream, DWORD *rotation)
{
    if (!rotation)
        return E_POINTER;
    *rotation = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_set_rotation(IMFCapturePreviewSink *i, DWORD stream, DWORD rotation)
{
    return rotation ? E_NOTIMPL : S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_set_custom_sink(IMFCapturePreviewSink *i, IMFMediaSink *sink)
{
    return E_NOTIMPL;
}

static const IMFCapturePreviewSinkVtbl PREVIEW_VTBL = {
    pv_query_interface, pv_add_ref, pv_release, pv_get_output_type, pv_get_service, pv_add_stream, pv_prepare,
    pv_remove_all, pv_set_render_handle, pv_set_render_surface, pv_update_video, pv_set_sample_callback,
    pv_get_mirror, pv_set_mirror, pv_get_rotation, pv_set_rotation, pv_set_custom_sink,
};

/* the record sink: there is no encoder to write a file. */
#define RECORD(i) (CONTAINING_RECORD((i), RecordSink, iface))

static HRESULT STDMETHODCALLTYPE rc_query_interface(IMFCaptureRecordSink *i, REFIID riid, void **out)
{
    return sink_query_interface(i, RECORD(i)->sink.engine, riid, out, &IID_IMFCaptureRecordSink);
}
static ULONG STDMETHODCALLTYPE rc_add_ref(IMFCaptureRecordSink *i)
{
    return sink_add_ref(RECORD(i)->sink.engine);
}
static ULONG STDMETHODCALLTYPE rc_release(IMFCaptureRecordSink *i)
{
    return sink_release(RECORD(i)->sink.engine);
}
static HRESULT STDMETHODCALLTYPE rc_get_output_type(IMFCaptureRecordSink *i, DWORD index, IMFMediaType **type)
{
    return sink_get_output_type(&RECORD(i)->sink, index, type);
}
static HRESULT STDMETHODCALLTYPE rc_get_service(IMFCaptureRecordSink *i, DWORD index, REFGUID service, REFIID riid,
                                                IUnknown **object)
{
    if (object)
        *object = nullptr;
    return MF_E_UNSUPPORTED_SERVICE;
}
static HRESULT STDMETHODCALLTYPE rc_add_stream(IMFCaptureRecordSink *i, DWORD source, IMFMediaType *type,
                                               IMFAttributes *attributes, DWORD *sink_stream)
{
    return sink_add_stream(&RECORD(i)->sink, source, type, attributes, sink_stream);
}
static HRESULT STDMETHODCALLTYPE rc_prepare(IMFCaptureRecordSink *i)
{
    return MF_E_CAPTURE_SINK_OUTPUT_NOT_SET;
}
static HRESULT STDMETHODCALLTYPE rc_remove_all(IMFCaptureRecordSink *i)
{
    return sink_remove_all(&RECORD(i)->sink);
}
static HRESULT STDMETHODCALLTYPE rc_set_output_byte_stream(IMFCaptureRecordSink *i, IMFByteStream *stream, REFGUID container)
{
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE rc_set_output_file_name(IMFCaptureRecordSink *i, LPCWSTR name)
{
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE rc_set_sample_callback(IMFCaptureRecordSink *i, DWORD index,
                                                        IMFCaptureEngineOnSampleCallback *callback)
{
    return sink_set_callback(&RECORD(i)->sink, index, callback);
}
static HRESULT STDMETHODCALLTYPE rc_set_custom_sink(IMFCaptureRecordSink *i, IMFMediaSink *sink)
{
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE rc_get_rotation(IMFCaptureRecordSink *i, DWORD stream, DWORD *rotation)
{
    if (!rotation)
        return E_POINTER;
    *rotation = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE rc_set_rotation(IMFCaptureRecordSink *i, DWORD stream, DWORD rotation)
{
    return rotation ? E_NOTIMPL : S_OK;
}

static const IMFCaptureRecordSinkVtbl RECORD_VTBL = {
    rc_query_interface, rc_add_ref, rc_release, rc_get_output_type, rc_get_service, rc_add_stream, rc_prepare,
    rc_remove_all, rc_set_output_byte_stream, rc_set_output_file_name, rc_set_sample_callback, rc_set_custom_sink,
    rc_get_rotation, rc_set_rotation,
};

/* the photo sink: a camera with one stream has no photo stream of its own. */
#define PHOTO(i) (CONTAINING_RECORD((i), PhotoSink, iface))

static HRESULT STDMETHODCALLTYPE ph_query_interface(IMFCapturePhotoSink *i, REFIID riid, void **out)
{
    return sink_query_interface(i, PHOTO(i)->sink.engine, riid, out, &IID_IMFCapturePhotoSink);
}
static ULONG STDMETHODCALLTYPE ph_add_ref(IMFCapturePhotoSink *i)
{
    return sink_add_ref(PHOTO(i)->sink.engine);
}
static ULONG STDMETHODCALLTYPE ph_release(IMFCapturePhotoSink *i)
{
    return sink_release(PHOTO(i)->sink.engine);
}
static HRESULT STDMETHODCALLTYPE ph_get_output_type(IMFCapturePhotoSink *i, DWORD index, IMFMediaType **type)
{
    return sink_get_output_type(&PHOTO(i)->sink, index, type);
}
static HRESULT STDMETHODCALLTYPE ph_get_service(IMFCapturePhotoSink *i, DWORD index, REFGUID service, REFIID riid,
                                                IUnknown **object)
{
    if (object)
        *object = nullptr;
    return MF_E_UNSUPPORTED_SERVICE;
}
static HRESULT STDMETHODCALLTYPE ph_add_stream(IMFCapturePhotoSink *i, DWORD source, IMFMediaType *type,
                                               IMFAttributes *attributes, DWORD *sink_stream)
{
    return MF_E_CAPTURE_SOURCE_NO_INDEPENDENT_PHOTO_STREAM_PRESENT;
}
static HRESULT STDMETHODCALLTYPE ph_prepare(IMFCapturePhotoSink *i)
{
    return MF_E_CAPTURE_SINK_OUTPUT_NOT_SET;
}
static HRESULT STDMETHODCALLTYPE ph_remove_all(IMFCapturePhotoSink *i)
{
    return sink_remove_all(&PHOTO(i)->sink);
}
static HRESULT STDMETHODCALLTYPE ph_set_output_file_name(IMFCapturePhotoSink *i, LPCWSTR name)
{
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE ph_set_sample_callback(IMFCapturePhotoSink *i, IMFCaptureEngineOnSampleCallback *callback)
{
    PhotoSink *p = PHOTO(i);
    if (!callback)
        return E_INVALIDARG;
    EnterCriticalSection(&p->sink.engine->lock);
    IMFCaptureEngineOnSampleCallback *old = p->callback;
    p->callback = callback;
    callback->lpVtbl->AddRef(callback);
    LeaveCriticalSection(&p->sink.engine->lock);
    if (old)
        old->lpVtbl->Release(old);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ph_set_output_byte_stream(IMFCapturePhotoSink *i, IMFByteStream *stream)
{
    return E_NOTIMPL;
}

static const IMFCapturePhotoSinkVtbl PHOTO_VTBL = {
    ph_query_interface, ph_add_ref, ph_release, ph_get_output_type, ph_get_service, ph_add_stream, ph_prepare,
    ph_remove_all, ph_set_output_file_name, ph_set_sample_callback, ph_set_output_byte_stream,
};

/* ------------------------------------------------------------------ the engine */

static Engine *engine_of(IMFCaptureEngine *iface)
{
    return CONTAINING_RECORD(iface, Engine, iface);
}

static HRESULT STDMETHODCALLTYPE eng_query_interface(IMFCaptureEngine *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFCaptureEngine)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE eng_add_ref(IMFCaptureEngine *iface)
{
    return (ULONG)InterlockedIncrement(&engine_of(iface)->refs);
}

static ULONG STDMETHODCALLTYPE eng_release(IMFCaptureEngine *iface)
{
    Engine *e = engine_of(iface);
    LONG refs = InterlockedDecrement(&e->refs);
    if (!refs)
        engine_destroy(e);
    return (ULONG)refs;
}

/* the last reference: the source stops, and nothing the engine started reaches the program again. */
static void engine_destroy(Engine *e)
{
    detach_handler(e->source_events);
    detach_handler(e->stream_events);
    e->source_events = e->stream_events = nullptr;
    if (e->source && e->state != ENGINE_NEW)
        e->source->lpVtbl->Stop(e->source);
    for (int k = 0; k < 3; k++) {
        Sink *sink = k == 0 ? &e->preview.sink : k == 1 ? &e->record.sink : &e->photo.sink;
        for (DWORD i = 0; i < MAX_SINK_STREAMS; i++) {
            if (sink->streams[i].type)
                sink->streams[i].type->lpVtbl->Release(sink->streams[i].type);
            if (sink->streams[i].callback)
                sink->streams[i].callback->lpVtbl->Release(sink->streams[i].callback);
        }
    }
    if (e->photo.callback)
        e->photo.callback->lpVtbl->Release(e->photo.callback);
    if (e->stream)
        e->stream->lpVtbl->Release(e->stream);
    if (e->handler)
        e->handler->lpVtbl->Release(e->handler);
    if (e->descriptor)
        e->descriptor->lpVtbl->Release(e->descriptor);
    if (e->mime)
        e->mime->lpVtbl->Release(e->mime);
    if (e->activate)
        e->activate->lpVtbl->Release(e->activate);
    if (e->source)
        e->source->lpVtbl->Release(e->source);
    if (e->dispatch)
        e->dispatch->iface.lpVtbl->Release(&e->dispatch->iface);
    if (e->event_callback)
        e->event_callback->lpVtbl->Release(e->event_callback);
    if (e->queue_held)
        MFUnlockWorkQueue(e->queue);
    DeleteCriticalSection(&e->lock);
    heap_free(e);
}

/* Initialize's work, on the event queue: the source, its descriptors and what the engine listens to. */
static HRESULT initialize_source(Engine *e, IUnknown *video)
{
    HRESULT hr;
    IMFMediaSource *source = nullptr;
    if (video) {
        if (SUCCEEDED(video->lpVtbl->QueryInterface(video, &IID_IMFMediaSource, (void **)&source))) {
            /* a source of the program's own. */
        } else {
            IMFActivate *activate = nullptr;
            hr = video->lpVtbl->QueryInterface(video, &IID_IMFActivate, (void **)&activate);
            if (FAILED(hr))
                return E_INVALIDARG;
            hr = activate->lpVtbl->ActivateObject(activate, &IID_IMFMediaSource, (void **)&source);
            if (FAILED(hr)) {
                activate->lpVtbl->Release(activate);
                return hr;
            }
            e->activate = activate;
        }
    } else {
        /* the host's first camera is the default. */
        IMFAttributes *attributes;
        IMFActivate **activates = nullptr;
        UINT32 count = 0;
        hr = MFCreateAttributes(&attributes, 1);
        if (FAILED(hr))
            return hr;
        attributes->lpVtbl->SetGUID(attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                    &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        hr = MFEnumDeviceSources(attributes, &activates, &count);
        attributes->lpVtbl->Release(attributes);
        if (FAILED(hr))
            return hr;
        if (!count)
            return MF_E_NO_CAPTURE_DEVICES_AVAILABLE;
        hr = activates[0]->lpVtbl->ActivateObject(activates[0], &IID_IMFMediaSource, (void **)&source);
        for (UINT32 i = 1; i < count; i++)
            activates[i]->lpVtbl->Release(activates[i]);
        if (SUCCEEDED(hr))
            e->activate = activates[0];
        else
            activates[0]->lpVtbl->Release(activates[0]);
        CoTaskMemFree(activates);
        if (FAILED(hr))
            return hr;
    }
    e->source = source;
    source->lpVtbl->QueryInterface(source, &IID_IMimeCaptureSource, (void **)&e->mime);
    hr = source->lpVtbl->CreatePresentationDescriptor(source, &e->descriptor);
    if (SUCCEEDED(hr)) {
        BOOL selected;
        IMFStreamDescriptor *sd = nullptr;
        hr = e->descriptor->lpVtbl->GetStreamDescriptorByIndex(e->descriptor, 0, &selected, &sd);
        if (SUCCEEDED(hr)) {
            hr = sd->lpVtbl->GetMediaTypeHandler(sd, &e->handler);
            sd->lpVtbl->Release(sd);
        }
    }
    if (SUCCEEDED(hr)) {
        e->source_events = new_handler(e, FALSE);
        e->stream_events = new_handler(e, TRUE);
        if (!e->source_events || !e->stream_events)
            hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr))
        hr = source->lpVtbl->BeginGetEvent(source, &e->source_events->iface, nullptr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE eng_initialize(IMFCaptureEngine *iface, IMFCaptureEngineOnEventCallback *callback,
                                                IMFAttributes *attributes, IUnknown *audio, IUnknown *video)
{
    Engine *e = engine_of(iface);
    if (!callback)
        return E_INVALIDARG;
    EnterCriticalSection(&e->lock);
    if (e->state != ENGINE_NEW) {
        LeaveCriticalSection(&e->lock);
        return MF_E_ALREADY_INITIALIZED;
    }
    e->state = ENGINE_INITIALIZING;
    e->event_callback = callback;
    callback->lpVtbl->AddRef(callback);
    Dispatch *d = alloc_zero(sizeof(*d));
    HRESULT hr = d ? S_OK : E_OUTOFMEMORY;
    if (d) {
        d->iface.lpVtbl = &DISPATCH_VTBL;
        d->refs = 1;
        d->callback = callback;
        callback->lpVtbl->AddRef(callback);
        e->dispatch = d;
        hr = MFAllocateWorkQueue(&e->queue);
        e->queue_held = SUCCEEDED(hr);
    }
    LeaveCriticalSection(&e->lock);
    /* only video is served, a program that wants audio gets none. */
    UINT32 video_only = 0;
    if (SUCCEEDED(hr) && attributes)
        attributes->lpVtbl->GetUINT32(attributes, &MF_CAPTURE_ENGINE_USE_AUDIO_DEVICE_ONLY, &video_only);
    if (SUCCEEDED(hr) && video_only)
        hr = MF_E_CAPTURE_SOURCE_NO_VIDEO_STREAM_PRESENT;
    if (SUCCEEDED(hr))
        hr = initialize_source(e, video);
    EnterCriticalSection(&e->lock);
    e->state = SUCCEEDED(hr) ? ENGINE_READY : ENGINE_FAILED;
    LeaveCriticalSection(&e->lock);
    /* the outcome comes as an event either way. */
    post_event(e, &MF_CAPTURE_ENGINE_INITIALIZED, hr, ~0u);
    return S_OK;
}

/* the preview sink's first stream, or a default one made from the camera's type. */
static HRESULT prepare_preview(Engine *e)
{
    SinkStream *s = nullptr;
    for (DWORD i = 0; i < MAX_SINK_STREAMS && !s; i++)
        if (e->preview.sink.streams[i].used)
            s = &e->preview.sink.streams[i];
    GUID source_subtype;
    if (!current_subtype(e, &source_subtype))
        return MF_E_INVALIDMEDIATYPE;
    if (!s) {
        /* a sink with no stream gets the camera's own format when raw, else the common one. */
        IMFMediaType *type = nullptr;
        HRESULT hr = MFCreateMediaType(&type);
        if (FAILED(hr))
            return hr;
        GUID target = source_subtype;
        if (!e->mime || e->mime->lpVtbl->CanConvertTo(e->mime, 0, &source_subtype, &source_subtype) != S_OK)
            target = source_subtype;
        else if (source_subtype.Data1 == 0x47504A4D)
            target = MFVideoFormat_I420;
        ((IMFAttributes *)type)->lpVtbl->SetGUID((IMFAttributes *)type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        ((IMFAttributes *)type)->lpVtbl->SetGUID((IMFAttributes *)type, &MF_MT_SUBTYPE, &target);
        DWORD index;
        hr = sink_add_stream(&e->preview.sink, 0, type, nullptr, &index);
        type->lpVtbl->Release(type);
        if (FAILED(hr))
            return hr;
        s = &e->preview.sink.streams[index];
    }
    /* the types may have changed since the stream was added. */
    HRESULT hr = can_deliver(e, &s->subtype);
    if (FAILED(hr))
        return hr;
    if (e->mime)
        hr = e->mime->lpVtbl->SetOutputSubtype(e->mime, 0, same_guid(&source_subtype, &s->subtype) ? &GUID_NULL : &s->subtype);
    return hr;
}

static HRESULT start_source(Engine *e)
{
    IMFPresentationDescriptor *pd = nullptr;
    HRESULT hr = e->source->lpVtbl->CreatePresentationDescriptor(e->source, &pd);
    if (FAILED(hr))
        return hr;
    PROPVARIANT start = {.vt = VT_EMPTY};
    hr = e->source->lpVtbl->Start(e->source, pd, &GUID_NULL, &start);
    pd->lpVtbl->Release(pd);
    return hr;
}

static HRESULT STDMETHODCALLTYPE eng_start_preview(IMFCaptureEngine *iface)
{
    Engine *e = engine_of(iface);
    EnterCriticalSection(&e->lock);
    HRESULT hr = ready(e);
    if (SUCCEEDED(hr) && (e->previewing || e->starting))
        hr = MF_E_CAPTURE_ENGINE_INVALID_OP;
    if (SUCCEEDED(hr))
        e->starting = TRUE;
    LeaveCriticalSection(&e->lock);
    if (FAILED(hr))
        return hr;
    hr = prepare_preview(e);
    if (SUCCEEDED(hr))
        hr = start_source(e);
    if (FAILED(hr)) {
        EnterCriticalSection(&e->lock);
        e->starting = FALSE;
        LeaveCriticalSection(&e->lock);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE eng_stop_preview(IMFCaptureEngine *iface)
{
    Engine *e = engine_of(iface);
    EnterCriticalSection(&e->lock);
    HRESULT hr = ready(e);
    if (SUCCEEDED(hr) && !e->previewing && !e->starting)
        hr = MF_E_CAPTURE_ENGINE_INVALID_OP;
    if (SUCCEEDED(hr)) {
        e->stopping = TRUE;
        e->starting = FALSE;
    }
    LeaveCriticalSection(&e->lock);
    if (FAILED(hr))
        return hr;
    hr = e->source->lpVtbl->Stop(e->source);
    if (FAILED(hr)) {
        EnterCriticalSection(&e->lock);
        e->stopping = FALSE;
        LeaveCriticalSection(&e->lock);
    }
    return hr;
}

/* the camera's mode changed while running: restart the source with the new one. */
static void restart_preview(Engine *e)
{
    if (SUCCEEDED(prepare_preview(e)))
        start_source(e);
}

static HRESULT STDMETHODCALLTYPE eng_start_record(IMFCaptureEngine *iface)
{
    HRESULT hr = ready(engine_of(iface));
    return FAILED(hr) ? hr : MF_E_CAPTURE_SINK_OUTPUT_NOT_SET;
}

static HRESULT STDMETHODCALLTYPE eng_stop_record(IMFCaptureEngine *iface, BOOL finalize, BOOL flush)
{
    HRESULT hr = ready(engine_of(iface));
    return FAILED(hr) ? hr : MF_E_CAPTURE_ENGINE_INVALID_OP;
}

static HRESULT STDMETHODCALLTYPE eng_take_photo(IMFCaptureEngine *iface)
{
    HRESULT hr = ready(engine_of(iface));
    return FAILED(hr) ? hr : MF_E_CAPTURE_SOURCE_NO_INDEPENDENT_PHOTO_STREAM_PRESENT;
}

static HRESULT STDMETHODCALLTYPE eng_get_sink(IMFCaptureEngine *iface, MF_CAPTURE_ENGINE_SINK_TYPE type, IMFCaptureSink **sink)
{
    Engine *e = engine_of(iface);
    if (!sink)
        return E_POINTER;
    *sink = nullptr;
    HRESULT hr = ready(e);
    if (FAILED(hr))
        return hr;
    switch (type) {
    case MF_CAPTURE_ENGINE_SINK_TYPE_PREVIEW:
        *sink = (IMFCaptureSink *)&e->preview.iface;
        break;
    case MF_CAPTURE_ENGINE_SINK_TYPE_RECORD:
        *sink = (IMFCaptureSink *)&e->record.iface;
        break;
    case MF_CAPTURE_ENGINE_SINK_TYPE_PHOTO:
        *sink = (IMFCaptureSink *)&e->photo.iface;
        break;
    default:
        return E_INVALIDARG;
    }
    InterlockedIncrement(&e->refs);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE eng_get_source(IMFCaptureEngine *iface, IMFCaptureSource **source)
{
    Engine *e = engine_of(iface);
    if (!source)
        return E_POINTER;
    *source = nullptr;
    HRESULT hr = ready(e);
    if (FAILED(hr))
        return hr;
    *source = &e->capture_source.iface;
    InterlockedIncrement(&e->refs);
    return S_OK;
}

static const IMFCaptureEngineVtbl ENGINE_VTBL = {
    eng_query_interface, eng_add_ref, eng_release, eng_initialize, eng_start_preview, eng_stop_preview, eng_start_record,
    eng_stop_record, eng_take_photo, eng_get_sink, eng_get_source,
};

static HRESULT create_engine(Engine **out)
{
    Engine *e = alloc_zero(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->iface.lpVtbl = &ENGINE_VTBL;
    e->refs = 1;
    e->capture_source.iface.lpVtbl = &CAPTURE_SOURCE_VTBL;
    e->capture_source.engine = e;
    e->preview.iface.lpVtbl = &PREVIEW_VTBL;
    e->preview.sink.engine = e;
    e->record.iface.lpVtbl = &RECORD_VTBL;
    e->record.sink.engine = e;
    e->photo.iface.lpVtbl = &PHOTO_VTBL;
    e->photo.sink.engine = e;
    InitializeCriticalSection(&e->lock);
    *out = e;
    return S_OK;
}

/* ------------------------------------------------------------------ creation */

HRESULT WINAPI MFCreateCaptureEngine(IMFCaptureEngine **engine)
{
    if (!engine)
        return E_POINTER;
    Engine *e;
    HRESULT hr = create_engine(&e);
    *engine = SUCCEEDED(hr) ? &e->iface : nullptr;
    return hr;
}

/* the class object a program asks for first, and the engines it makes. */
typedef struct Factory {
    IMFCaptureEngineClassFactory iface;
} Factory;

static HRESULT make_object(REFIID riid, void **out);

static HRESULT STDMETHODCALLTYPE fac_query_interface(IMFCaptureEngineClassFactory *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFCaptureEngineClassFactory)) {
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE fac_add_ref(IMFCaptureEngineClassFactory *iface)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE fac_release(IMFCaptureEngineClassFactory *iface)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE fac_create_instance(IMFCaptureEngineClassFactory *iface, REFCLSID clsid, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualGUID(clsid, &CLSID_MFCaptureEngine))
        return CLASS_E_CLASSNOTAVAILABLE;
    Engine *e;
    HRESULT hr = create_engine(&e);
    if (FAILED(hr))
        return hr;
    hr = e->iface.lpVtbl->QueryInterface(&e->iface, riid, out);
    e->iface.lpVtbl->Release(&e->iface);
    return hr;
}

static const IMFCaptureEngineClassFactoryVtbl FACTORY_VTBL = {fac_query_interface, fac_add_ref, fac_release,
                                                              fac_create_instance};
static Factory g_factory = {{&FACTORY_VTBL}};

/* a class factory or an engine, whichever the interface names. */
static HRESULT make_object(REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IMFCaptureEngineClassFactory)) {
        *out = &g_factory.iface;
        return S_OK;
    }
    Engine *e;
    HRESULT hr = create_engine(&e);
    if (FAILED(hr))
        return hr;
    hr = e->iface.lpVtbl->QueryInterface(&e->iface, riid, out);
    e->iface.lpVtbl->Release(&e->iface);
    return hr;
}

typedef struct ClassObject {
    IClassFactory iface;
} ClassObject;

static HRESULT STDMETHODCALLTYPE cf_query_interface(IClassFactory *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE cf_add_ref(IClassFactory *iface)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE cf_release(IClassFactory *iface)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE cf_create_instance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return make_object(riid, out);
}

static HRESULT STDMETHODCALLTYPE cf_lock_server(IClassFactory *iface, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl CLASS_VTBL = {cf_query_interface, cf_add_ref, cf_release, cf_create_instance,
                                             cf_lock_server};
static ClassObject g_class = {{&CLASS_VTBL}};

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualGUID(clsid, &CLSID_MFCaptureEngine))
        return CLASS_E_CLASSNOTAVAILABLE;
    return g_class.iface.lpVtbl->QueryInterface(&g_class.iface, riid, out);
}

/* engines hold their own references, the module stays loaded for programs that hold them. */
HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    return TRUE;
}
