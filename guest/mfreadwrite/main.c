/*
 * mfreadwrite.dll: the source reader over a media source. the source starts
 * at the first read, each stream is asked for samples and what they send is
 * queued. there is no decoder, so a stream's types are the ones its source
 * offers. reads block, or with MF_SOURCE_READER_ASYNC_CALLBACK run on the
 * reader's work queue and end in the callback's OnReadSample.
 */
#define MIME_BUILD_MFREADWRITE
#include <mime/nt.h>
#include <mime/win32/winbase.h>
#include <mime/win32/mfapi.h>
#include <mime/win32/mfreadwrite.h>

#define CURRENT_TYPE_INDEX 0xFFFFFFFF
/* event callbacks run on their own queue, apart from the reads that wait for them. */
#define EVENT_QUEUE MFASYNC_CALLBACK_QUEUE_MULTITHREADED

typedef struct SourceReader SourceReader;

typedef struct QueuedSample {
    struct QueuedSample *next;
    IMFSample *sample;
    DWORD flags;
    LONGLONG timestamp;
} QueuedSample;

typedef struct {
    IMFAsyncCallback callback;
    SourceReader *reader;
    DWORD index;
    IMFStreamDescriptor *descriptor;
    IMFMediaTypeHandler *handler;
    IMFMediaType *current;
    DWORD identifier;
    BOOL selected;
    IMFMediaStream *stream;
    QueuedSample *head, *tail;
    BOOL requested;
    BOOL ended;
} ReaderStream;

/* an asynchronous read, run on the reader's work queue. */
typedef struct {
    IUnknown iface;
    LONG refs;
    DWORD index;
    DWORD control;
} ReadRequest;

struct SourceReader {
    IMFSourceReader iface;
    IMFAsyncCallback source_callback;
    IMFAsyncCallback read_callback;
    /* refs counts the program's references, total adds those of pending callbacks. */
    LONG refs;
    LONG total;
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE changed;
    IMFMediaSource *source;
    IMFPresentationDescriptor *descriptor;
    ReaderStream *streams;
    DWORD count;
    IMFSourceReaderCallback *callback;
    DWORD work_queue;
    BOOL keep_source;
    BOOL started;
    BOOL starting;
    BOOL shutdown;
    HRESULT error;
    /* where the first start begins. VT_EMPTY means the source's current position. */
    PROPVARIANT position;
};

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    return TRUE;
}

static void *alloc_zero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

static void heap_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

static void reader_free(SourceReader *reader);

static void hold(SourceReader *reader)
{
    InterlockedIncrement(&reader->total);
}

static void drop(SourceReader *reader)
{
    if (!InterlockedDecrement(&reader->total))
        reader_free(reader);
}

static HRESULT copy_type(IMFMediaType *source, IMFMediaType **out)
{
    IMFMediaType *copy;
    HRESULT hr = MFCreateMediaType(&copy);
    if (FAILED(hr))
        return hr;
    hr = source->lpVtbl->CopyAllItems(source, (IMFAttributes *)copy);
    if (FAILED(hr)) {
        copy->lpVtbl->Release(copy);
        return hr;
    }
    *out = copy;
    return S_OK;
}

static void clear_samples(ReaderStream *stream)
{
    while (stream->head) {
        QueuedSample *entry = stream->head;
        stream->head = entry->next;
        if (entry->sample)
            entry->sample->lpVtbl->Release(entry->sample);
        heap_free(entry);
    }
    stream->tail = nullptr;
}

static void push_sample(ReaderStream *stream, IMFSample *sample, DWORD flags, LONGLONG timestamp)
{
    QueuedSample *entry = alloc_zero(sizeof(*entry));
    if (!entry)
        return;
    entry->sample = sample;
    if (sample)
        sample->lpVtbl->AddRef(sample);
    entry->flags = flags;
    entry->timestamp = timestamp;
    if (stream->tail)
        stream->tail->next = entry;
    else
        stream->head = entry;
    stream->tail = entry;
}

/* the index is a stream number, or names the first video or audio stream. */
static ReaderStream *find_stream(SourceReader *reader, DWORD index)
{
    if (index == MF_SOURCE_READER_FIRST_VIDEO_STREAM || index == MF_SOURCE_READER_FIRST_AUDIO_STREAM) {
        const GUID *major = index == MF_SOURCE_READER_FIRST_VIDEO_STREAM ? &MFMediaType_Video : &MFMediaType_Audio;
        for (DWORD i = 0; i < reader->count; i++) {
            GUID type;
            IMFMediaTypeHandler *handler = reader->streams[i].handler;
            if (SUCCEEDED(handler->lpVtbl->GetMajorType(handler, &type)) && IsEqualGUID(&type, major))
                return &reader->streams[i];
        }
        return nullptr;
    }
    return index < reader->count ? &reader->streams[index] : nullptr;
}

/* ------------------------------------------------------------------ events */

static HRESULT STDMETHODCALLTYPE callback_query_interface(IMFAsyncCallback *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFAsyncCallback)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE callback_get_parameters(IMFAsyncCallback *iface, DWORD *flags, DWORD *queue)
{
    *flags = 0;
    *queue = EVENT_QUEUE;
    return S_OK;
}

static SourceReader *source_callback_reader(IMFAsyncCallback *iface)
{
    return CONTAINING_RECORD(iface, SourceReader, source_callback);
}

static ULONG STDMETHODCALLTYPE source_callback_add_ref(IMFAsyncCallback *iface)
{
    hold(source_callback_reader(iface));
    return 2;
}

static ULONG STDMETHODCALLTYPE source_callback_release(IMFAsyncCallback *iface)
{
    drop(source_callback_reader(iface));
    return 1;
}

/* matches the stream by its descriptor's identifier. */
static void attach_stream(SourceReader *reader, IMFMediaStream *media_stream)
{
    IMFStreamDescriptor *descriptor = nullptr;
    DWORD identifier = 0;
    if (FAILED(media_stream->lpVtbl->GetStreamDescriptor(media_stream, &descriptor)))
        return;
    descriptor->lpVtbl->GetStreamIdentifier(descriptor, &identifier);
    descriptor->lpVtbl->Release(descriptor);
    EnterCriticalSection(&reader->lock);
    for (DWORD i = 0; i < reader->count; i++) {
        ReaderStream *stream = &reader->streams[i];
        if (stream->identifier != identifier)
            continue;
        if (stream->stream != media_stream) {
            if (stream->stream)
                stream->stream->lpVtbl->Release(stream->stream);
            stream->stream = media_stream;
            media_stream->lpVtbl->AddRef(media_stream);
            media_stream->lpVtbl->BeginGetEvent(media_stream, &stream->callback, nullptr);
        }
        stream->ended = FALSE;
        stream->requested = FALSE;
        break;
    }
    WakeAllConditionVariable(&reader->changed);
    LeaveCriticalSection(&reader->lock);
}

static HRESULT STDMETHODCALLTYPE source_callback_invoke(IMFAsyncCallback *iface, IMFAsyncResult *result)
{
    SourceReader *reader = source_callback_reader(iface);
    IMFMediaEvent *event = nullptr;
    IMFMediaSource *source = reader->source;
    if (FAILED(source->lpVtbl->EndGetEvent(source, result, &event)))
        return S_OK;
    MediaEventType type = MEUnknown;
    HRESULT status = S_OK;
    PROPVARIANT value = {};
    event->lpVtbl->GetType(event, &type);
    event->lpVtbl->GetStatus(event, &status);
    event->lpVtbl->GetValue(event, &value);
    if ((type == MENewStream || type == MEUpdatedStream) && value.vt == VT_UNKNOWN && value.punkVal) {
        IMFMediaStream *media_stream = nullptr;
        if (SUCCEEDED(value.punkVal->lpVtbl->QueryInterface(value.punkVal, &IID_IMFMediaStream,
                                                             (void **)&media_stream))) {
            attach_stream(reader, media_stream);
            media_stream->lpVtbl->Release(media_stream);
        }
    }
    EnterCriticalSection(&reader->lock);
    if (type == MESourceStarted || type == MESourceSeeked) {
        reader->starting = FALSE;
        reader->started = SUCCEEDED(status);
        if (FAILED(status))
            reader->error = status;
    } else if (type == MEError) {
        reader->error = FAILED(status) ? status : E_FAIL;
        reader->starting = FALSE;
    }
    WakeAllConditionVariable(&reader->changed);
    BOOL listen = !reader->shutdown;
    LeaveCriticalSection(&reader->lock);
    PropVariantClear(&value);
    event->lpVtbl->Release(event);
    if (listen)
        source->lpVtbl->BeginGetEvent(source, &reader->source_callback, nullptr);
    return S_OK;
}

static const IMFAsyncCallbackVtbl SOURCE_CALLBACK_VTBL = {
    callback_query_interface, source_callback_add_ref, source_callback_release,
    callback_get_parameters,  source_callback_invoke,
};

static ReaderStream *stream_callback_stream(IMFAsyncCallback *iface)
{
    return CONTAINING_RECORD(iface, ReaderStream, callback);
}

static ULONG STDMETHODCALLTYPE stream_callback_add_ref(IMFAsyncCallback *iface)
{
    hold(stream_callback_stream(iface)->reader);
    return 2;
}

static ULONG STDMETHODCALLTYPE stream_callback_release(IMFAsyncCallback *iface)
{
    drop(stream_callback_stream(iface)->reader);
    return 1;
}

/* queues samples, ticks and end of stream for the reads. */
static HRESULT STDMETHODCALLTYPE stream_callback_invoke(IMFAsyncCallback *iface, IMFAsyncResult *result)
{
    ReaderStream *stream = stream_callback_stream(iface);
    SourceReader *reader = stream->reader;
    EnterCriticalSection(&reader->lock);
    IMFMediaStream *media_stream = stream->stream;
    if (media_stream)
        media_stream->lpVtbl->AddRef(media_stream);
    LeaveCriticalSection(&reader->lock);
    if (!media_stream)
        return S_OK;
    IMFMediaEvent *event = nullptr;
    if (FAILED(media_stream->lpVtbl->EndGetEvent(media_stream, result, &event))) {
        media_stream->lpVtbl->Release(media_stream);
        return S_OK;
    }
    MediaEventType type = MEUnknown;
    HRESULT status = S_OK;
    PROPVARIANT value = {};
    event->lpVtbl->GetType(event, &type);
    event->lpVtbl->GetStatus(event, &status);
    event->lpVtbl->GetValue(event, &value);
    EnterCriticalSection(&reader->lock);
    if (type == MEMediaSample) {
        IMFSample *sample = nullptr;
        if (value.vt == VT_UNKNOWN && value.punkVal)
            value.punkVal->lpVtbl->QueryInterface(value.punkVal, &IID_IMFSample, (void **)&sample);
        if (sample) {
            LONGLONG time = 0;
            sample->lpVtbl->GetSampleTime(sample, &time);
            push_sample(stream, sample, 0, time);
            sample->lpVtbl->Release(sample);
        }
        stream->requested = FALSE;
    } else if (type == MEStreamTick) {
        push_sample(stream, nullptr, MF_SOURCE_READERF_STREAMTICK, value.vt == VT_I8 ? value.hVal.QuadPart : 0);
        stream->requested = FALSE;
    } else if (type == MEEndOfStream) {
        stream->ended = TRUE;
        stream->requested = FALSE;
    } else if (type == MEError) {
        reader->error = FAILED(status) ? status : E_FAIL;
    }
    WakeAllConditionVariable(&reader->changed);
    BOOL listen = !reader->shutdown && stream->stream == media_stream;
    LeaveCriticalSection(&reader->lock);
    PropVariantClear(&value);
    event->lpVtbl->Release(event);
    if (listen)
        media_stream->lpVtbl->BeginGetEvent(media_stream, &stream->callback, nullptr);
    media_stream->lpVtbl->Release(media_stream);
    return S_OK;
}

static const IMFAsyncCallbackVtbl STREAM_CALLBACK_VTBL = {
    callback_query_interface, stream_callback_add_ref, stream_callback_release,
    callback_get_parameters,  stream_callback_invoke,
};

/* ------------------------------------------------------------------ reading */

/* called at the first read. waits for the source to start, under the lock. */
static HRESULT start_locked(SourceReader *reader)
{
    if (reader->started)
        return S_OK;
    if (!reader->starting) {
        reader->starting = TRUE;
        PROPVARIANT position = reader->position;
        LeaveCriticalSection(&reader->lock);
        HRESULT hr = reader->source->lpVtbl->Start(reader->source, reader->descriptor, &GUID_NULL, &position);
        EnterCriticalSection(&reader->lock);
        if (FAILED(hr)) {
            reader->starting = FALSE;
            return hr;
        }
    }
    while (reader->starting && !reader->shutdown && SUCCEEDED(reader->error))
        SleepConditionVariableCS(&reader->changed, &reader->lock, INFINITE);
    if (reader->shutdown)
        return MF_E_SHUTDOWN;
    return reader->started ? S_OK : reader->error;
}

/* only for a stream that has nothing queued. */
static void request_locked(SourceReader *reader, ReaderStream *stream)
{
    if (stream->head || stream->ended || stream->requested || !stream->stream)
        return;
    stream->requested = TRUE;
    IMFMediaStream *media_stream = stream->stream;
    media_stream->lpVtbl->AddRef(media_stream);
    LeaveCriticalSection(&reader->lock);
    HRESULT hr = media_stream->lpVtbl->RequestSample(media_stream, nullptr);
    EnterCriticalSection(&reader->lock);
    media_stream->lpVtbl->Release(media_stream);
    if (hr == MF_E_END_OF_STREAM)
        stream->ended = TRUE;
    if (FAILED(hr))
        stream->requested = FALSE;
}

/* blocks until the stream, or any selected stream, has a sample. */
static HRESULT read_sample(SourceReader *reader, DWORD index, DWORD control, DWORD *actual, DWORD *flags,
                           LONGLONG *timestamp, IMFSample **sample)
{
    *actual = index;
    *flags = 0;
    *timestamp = 0;
    *sample = nullptr;
    EnterCriticalSection(&reader->lock);
    HRESULT hr = S_OK;
    ReaderStream *only = nullptr;
    if (index != MF_SOURCE_READER_ANY_STREAM) {
        only = find_stream(reader, index);
        if (!only) {
            hr = MF_E_INVALIDSTREAMNUMBER;
            goto done;
        }
        *actual = only->index;
        if (!only->selected) {
            hr = MF_E_INVALIDREQUEST;
            goto done;
        }
    }
    hr = start_locked(reader);
    if (FAILED(hr))
        goto done;
    for (;;) {
        if (reader->shutdown) {
            hr = MF_E_SHUTDOWN;
            goto done;
        }
        if (FAILED(reader->error)) {
            *flags = MF_SOURCE_READERF_ERROR;
            hr = reader->error;
            goto done;
        }
        BOOL all_ended = TRUE;
        for (DWORD i = 0; i < reader->count; i++) {
            ReaderStream *stream = &reader->streams[i];
            if ((only && stream != only) || !stream->selected)
                continue;
            if (stream->head) {
                QueuedSample *entry = stream->head;
                stream->head = entry->next;
                if (!stream->head)
                    stream->tail = nullptr;
                *actual = stream->index;
                *flags = entry->flags;
                *timestamp = entry->timestamp;
                *sample = entry->sample;
                heap_free(entry);
                goto done;
            }
            if (!stream->ended)
                all_ended = FALSE;
        }
        if (all_ended) {
            *flags = MF_SOURCE_READERF_ENDOFSTREAM;
            goto done;
        }
        for (DWORD i = 0; i < reader->count; i++) {
            ReaderStream *stream = &reader->streams[i];
            if ((!only || stream == only) && stream->selected)
                request_locked(reader, stream);
        }
        BOOL waiting = FALSE;
        for (DWORD i = 0; i < reader->count; i++) {
            ReaderStream *stream = &reader->streams[i];
            if ((!only || stream == only) && stream->selected && !stream->head && !stream->ended)
                waiting = TRUE;
        }
        if (waiting)
            SleepConditionVariableCS(&reader->changed, &reader->lock, INFINITE);
    }
done:
    LeaveCriticalSection(&reader->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE request_query_interface(IUnknown *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE request_add_ref(IUnknown *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, ReadRequest, iface)->refs);
}

static ULONG STDMETHODCALLTYPE request_release(IUnknown *iface)
{
    ReadRequest *request = CONTAINING_RECORD(iface, ReadRequest, iface);
    ULONG refs = InterlockedDecrement(&request->refs);
    if (!refs)
        heap_free(request);
    return refs;
}

static const IUnknownVtbl READ_REQUEST_VTBL = {request_query_interface, request_add_ref, request_release};

static SourceReader *read_callback_reader(IMFAsyncCallback *iface)
{
    return CONTAINING_RECORD(iface, SourceReader, read_callback);
}

static ULONG STDMETHODCALLTYPE read_callback_add_ref(IMFAsyncCallback *iface)
{
    hold(read_callback_reader(iface));
    return 2;
}

static ULONG STDMETHODCALLTYPE read_callback_release(IMFAsyncCallback *iface)
{
    drop(read_callback_reader(iface));
    return 1;
}

static HRESULT STDMETHODCALLTYPE read_callback_get_parameters(IMFAsyncCallback *iface, DWORD *flags, DWORD *queue)
{
    *flags = 0;
    *queue = read_callback_reader(iface)->work_queue;
    return S_OK;
}

/* a request without a stream is a flush. */
static HRESULT STDMETHODCALLTYPE read_callback_invoke(IMFAsyncCallback *iface, IMFAsyncResult *result)
{
    SourceReader *reader = read_callback_reader(iface);
    ReadRequest *request = CONTAINING_RECORD(result->lpVtbl->GetStateNoAddRef(result), ReadRequest, iface);
    IMFSourceReaderCallback *callback = reader->callback;
    if (request->control == ~0u) {
        callback->lpVtbl->OnFlush(callback, request->index);
        return S_OK;
    }
    DWORD actual, flags;
    LONGLONG timestamp;
    IMFSample *sample;
    HRESULT hr = read_sample(reader, request->index, request->control, &actual, &flags, &timestamp, &sample);
    if (hr == MF_E_SHUTDOWN)
        return S_OK;
    callback->lpVtbl->OnReadSample(callback, hr, actual, flags, timestamp, sample);
    if (sample)
        sample->lpVtbl->Release(sample);
    return S_OK;
}

static const IMFAsyncCallbackVtbl READ_CALLBACK_VTBL = {
    callback_query_interface,     read_callback_add_ref, read_callback_release,
    read_callback_get_parameters, read_callback_invoke,
};

static HRESULT queue_request(SourceReader *reader, DWORD index, DWORD control)
{
    ReadRequest *request = alloc_zero(sizeof(*request));
    if (!request)
        return E_OUTOFMEMORY;
    request->iface.lpVtbl = &READ_REQUEST_VTBL;
    request->refs = 1;
    request->index = index;
    request->control = control;
    HRESULT hr = MFPutWorkItem(reader->work_queue, &reader->read_callback, &request->iface);
    request_release(&request->iface);
    return hr;
}

/* ------------------------------------------------------------------ IMFSourceReader */

static SourceReader *source_reader(IMFSourceReader *iface)
{
    return CONTAINING_RECORD(iface, SourceReader, iface);
}

static HRESULT STDMETHODCALLTYPE reader_query_interface(IMFSourceReader *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFSourceReader)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE reader_add_ref(IMFSourceReader *iface)
{
    SourceReader *reader = source_reader(iface);
    hold(reader);
    return InterlockedIncrement(&reader->refs);
}

static void reader_free(SourceReader *reader)
{
    for (DWORD i = 0; i < reader->count; i++) {
        ReaderStream *stream = &reader->streams[i];
        clear_samples(stream);
        if (stream->stream)
            stream->stream->lpVtbl->Release(stream->stream);
        if (stream->current)
            stream->current->lpVtbl->Release(stream->current);
        if (stream->handler)
            stream->handler->lpVtbl->Release(stream->handler);
        if (stream->descriptor)
            stream->descriptor->lpVtbl->Release(stream->descriptor);
    }
    heap_free(reader->streams);
    if (reader->descriptor)
        reader->descriptor->lpVtbl->Release(reader->descriptor);
    if (reader->source)
        reader->source->lpVtbl->Release(reader->source);
    if (reader->callback)
        reader->callback->lpVtbl->Release(reader->callback);
    DeleteCriticalSection(&reader->lock);
    heap_free(reader);
}

/*
 * the last release shuts the source down unless the attributes say to keep
 * it. the reader is freed once its pending callbacks return.
 */
static ULONG STDMETHODCALLTYPE reader_release(IMFSourceReader *iface)
{
    SourceReader *reader = source_reader(iface);
    ULONG refs = InterlockedDecrement(&reader->refs);
    if (!refs) {
        EnterCriticalSection(&reader->lock);
        reader->shutdown = TRUE;
        WakeAllConditionVariable(&reader->changed);
        LeaveCriticalSection(&reader->lock);
        if (!reader->keep_source)
            reader->source->lpVtbl->Shutdown(reader->source);
        if (reader->work_queue)
            MFUnlockWorkQueue(reader->work_queue);
    }
    drop(reader);
    return refs;
}

static HRESULT STDMETHODCALLTYPE reader_get_stream_selection(IMFSourceReader *iface, DWORD index, BOOL *selected)
{
    SourceReader *reader = source_reader(iface);
    if (!selected)
        return E_POINTER;
    EnterCriticalSection(&reader->lock);
    ReaderStream *stream = find_stream(reader, index);
    if (stream)
        *selected = stream->selected;
    LeaveCriticalSection(&reader->lock);
    return stream ? S_OK : MF_E_INVALIDSTREAMNUMBER;
}

static void select_locked(SourceReader *reader, ReaderStream *stream, BOOL selected)
{
    stream->selected = selected != FALSE;
    if (selected)
        reader->descriptor->lpVtbl->SelectStream(reader->descriptor, stream->index);
    else
        reader->descriptor->lpVtbl->DeselectStream(reader->descriptor, stream->index);
}

static HRESULT STDMETHODCALLTYPE reader_set_stream_selection(IMFSourceReader *iface, DWORD index, BOOL selected)
{
    SourceReader *reader = source_reader(iface);
    HRESULT hr = S_OK;
    EnterCriticalSection(&reader->lock);
    if (index == MF_SOURCE_READER_ALL_STREAMS) {
        for (DWORD i = 0; i < reader->count; i++)
            select_locked(reader, &reader->streams[i], selected);
    } else {
        ReaderStream *stream = find_stream(reader, index);
        if (stream)
            select_locked(reader, stream, selected);
        else
            hr = MF_E_INVALIDSTREAMNUMBER;
    }
    LeaveCriticalSection(&reader->lock);
    return hr;
}

/* returns a copy of a source type, or of the stream's current one. */
static HRESULT STDMETHODCALLTYPE reader_get_native_media_type(IMFSourceReader *iface, DWORD index, DWORD type_index,
                                                             IMFMediaType **type)
{
    SourceReader *reader = source_reader(iface);
    if (!type)
        return E_POINTER;
    *type = nullptr;
    EnterCriticalSection(&reader->lock);
    ReaderStream *stream = find_stream(reader, index);
    IMFMediaType *native = nullptr;
    HRESULT hr = MF_E_INVALIDSTREAMNUMBER;
    if (stream && type_index == CURRENT_TYPE_INDEX)
        hr = stream->handler->lpVtbl->GetCurrentMediaType(stream->handler, &native);
    else if (stream)
        hr = stream->handler->lpVtbl->GetMediaTypeByIndex(stream->handler, type_index, &native);
    LeaveCriticalSection(&reader->lock);
    if (SUCCEEDED(hr)) {
        hr = copy_type(native, type);
        native->lpVtbl->Release(native);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE reader_get_current_media_type(IMFSourceReader *iface, DWORD index,
                                                              IMFMediaType **type)
{
    SourceReader *reader = source_reader(iface);
    if (!type)
        return E_POINTER;
    *type = nullptr;
    EnterCriticalSection(&reader->lock);
    ReaderStream *stream = find_stream(reader, index);
    HRESULT hr = MF_E_INVALIDSTREAMNUMBER;
    if (stream)
        hr = stream->current ? copy_type(stream->current, type) : MF_E_NOT_INITIALIZED;
    LeaveCriticalSection(&reader->lock);
    return hr;
}

/*
 * selects the native type that shares every attribute of the given type.
 * other subtypes would need a decoder, and there is none.
 */
static HRESULT STDMETHODCALLTYPE reader_set_current_media_type(IMFSourceReader *iface, DWORD index, DWORD *reserved,
                                                              IMFMediaType *type)
{
    SourceReader *reader = source_reader(iface);
    if (!type)
        return E_POINTER;
    GUID major, subtype;
    if (FAILED(type->lpVtbl->GetMajorType(type, &major)))
        return MF_E_INVALIDMEDIATYPE;
    BOOL has_subtype = SUCCEEDED(type->lpVtbl->GetGUID(type, &MF_MT_SUBTYPE, &subtype));
    EnterCriticalSection(&reader->lock);
    ReaderStream *stream = find_stream(reader, index);
    HRESULT hr = stream ? MF_E_INVALIDMEDIATYPE : MF_E_INVALIDSTREAMNUMBER;
    DWORD count = 0;
    if (stream)
        stream->handler->lpVtbl->GetMediaTypeCount(stream->handler, &count);
    for (DWORD i = 0; stream && i < count; i++) {
        IMFMediaType *native = nullptr;
        if (FAILED(stream->handler->lpVtbl->GetMediaTypeByIndex(stream->handler, i, &native)))
            continue;
        GUID native_major, native_subtype;
        BOOL same = FALSE;
        if (SUCCEEDED(native->lpVtbl->GetMajorType(native, &native_major)) && IsEqualGUID(&major, &native_major)) {
            if (has_subtype && SUCCEEDED(native->lpVtbl->GetGUID(native, &MF_MT_SUBTYPE, &native_subtype)) &&
                !IsEqualGUID(&subtype, &native_subtype)) {
                hr = MF_E_TOPO_CODEC_NOT_FOUND;
            } else if (SUCCEEDED(type->lpVtbl->Compare(type, (IMFAttributes *)native, MF_ATTRIBUTES_MATCH_OUR_ITEMS,
                                                        &same)) &&
                       same) {
                hr = stream->handler->lpVtbl->SetCurrentMediaType(stream->handler, native);
                if (SUCCEEDED(hr)) {
                    if (stream->current)
                        stream->current->lpVtbl->Release(stream->current);
                    stream->current = native;
                    native->lpVtbl->AddRef(native);
                }
                native->lpVtbl->Release(native);
                break;
            }
        }
        native->lpVtbl->Release(native);
    }
    LeaveCriticalSection(&reader->lock);
    return hr;
}

static void flush_locked(SourceReader *reader, DWORD index)
{
    for (DWORD i = 0; i < reader->count; i++) {
        if (index == MF_SOURCE_READER_ALL_STREAMS || reader->streams[i].index == index)
            clear_samples(&reader->streams[i]);
    }
}

/* the position is in 100 ns units. queued samples are dropped. */
static HRESULT STDMETHODCALLTYPE reader_set_current_position(IMFSourceReader *iface, REFGUID format,
                                                            const PROPVARIANT *position)
{
    SourceReader *reader = source_reader(iface);
    if (!position)
        return E_POINTER;
    if (!IsEqualGUID(format, &GUID_NULL))
        return MF_E_UNSUPPORTED_TIME_FORMAT;
    if (position->vt != VT_I8)
        return E_INVALIDARG;
    EnterCriticalSection(&reader->lock);
    flush_locked(reader, MF_SOURCE_READER_ALL_STREAMS);
    for (DWORD i = 0; i < reader->count; i++) {
        reader->streams[i].ended = FALSE;
        reader->streams[i].requested = FALSE;
    }
    BOOL started = reader->started;
    if (started)
        reader->starting = TRUE;
    else
        reader->position = *position;
    LeaveCriticalSection(&reader->lock);
    if (!started)
        return S_OK;
    return reader->source->lpVtbl->Start(reader->source, reader->descriptor, &GUID_NULL, position);
}

static HRESULT STDMETHODCALLTYPE reader_read_sample(IMFSourceReader *iface, DWORD index, DWORD control,
                                                   DWORD *actual, DWORD *flags, LONGLONG *timestamp,
                                                   IMFSample **sample)
{
    SourceReader *reader = source_reader(iface);
    if (control & ~MF_SOURCE_READER_CONTROLF_DRAIN)
        return E_INVALIDARG;
    if (reader->shutdown)
        return MF_E_SHUTDOWN;
    if (reader->callback) {
        if (actual || flags || timestamp || sample)
            return E_INVALIDARG;
        if (index != MF_SOURCE_READER_ANY_STREAM && !find_stream(reader, index))
            return MF_E_INVALIDSTREAMNUMBER;
        return queue_request(reader, index, control);
    }
    if (!flags || !sample)
        return E_POINTER;
    DWORD actual_index;
    LONGLONG time;
    HRESULT hr = read_sample(reader, index, control, &actual_index, flags, &time, sample);
    if (actual)
        *actual = actual_index;
    if (timestamp)
        *timestamp = time;
    return hr;
}

/* an asynchronous reader also calls the callback's OnFlush. */
static HRESULT STDMETHODCALLTYPE reader_flush(IMFSourceReader *iface, DWORD index)
{
    SourceReader *reader = source_reader(iface);
    EnterCriticalSection(&reader->lock);
    ReaderStream *stream = index == MF_SOURCE_READER_ALL_STREAMS ? nullptr : find_stream(reader, index);
    HRESULT hr = S_OK;
    if (index != MF_SOURCE_READER_ALL_STREAMS && !stream)
        hr = MF_E_INVALIDSTREAMNUMBER;
    else
        flush_locked(reader, stream ? stream->index : index);
    LeaveCriticalSection(&reader->lock);
    if (SUCCEEDED(hr) && reader->callback)
        hr = queue_request(reader, stream ? stream->index : index, ~0u);
    return hr;
}

/* only the source's own services: streams have no transform to ask. */
static HRESULT STDMETHODCALLTYPE reader_get_service_for_stream(IMFSourceReader *iface, DWORD index, REFGUID service,
                                                              REFIID riid, void **object)
{
    SourceReader *reader = source_reader(iface);
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (index != MF_SOURCE_READER_MEDIASOURCE)
        return find_stream(reader, index) ? MF_E_UNSUPPORTED_SERVICE : MF_E_INVALIDSTREAMNUMBER;
    if (IsEqualGUID(service, &GUID_NULL))
        return reader->source->lpVtbl->QueryInterface(reader->source, riid, object);
    IMFGetService *get_service = nullptr;
    if (FAILED(reader->source->lpVtbl->QueryInterface(reader->source, &IID_IMFGetService, (void **)&get_service)))
        return MF_E_UNSUPPORTED_SERVICE;
    HRESULT hr = get_service->lpVtbl->GetService(get_service, service, riid, object);
    get_service->lpVtbl->Release(get_service);
    return hr;
}

static HRESULT STDMETHODCALLTYPE reader_get_presentation_attribute(IMFSourceReader *iface, DWORD index,
                                                                  REFGUID attribute, PROPVARIANT *value)
{
    SourceReader *reader = source_reader(iface);
    if (!value)
        return E_POINTER;
    if (index == MF_SOURCE_READER_MEDIASOURCE) {
        if (IsEqualGUID(attribute, &MF_SOURCE_READER_MEDIASOURCE_CHARACTERISTICS)) {
            DWORD characteristics = 0;
            HRESULT hr = reader->source->lpVtbl->GetCharacteristics(reader->source, &characteristics);
            if (SUCCEEDED(hr)) {
                value->vt = VT_UI4;
                value->ulVal = characteristics;
            }
            return hr;
        }
        return reader->descriptor->lpVtbl->GetItem(reader->descriptor, attribute, value);
    }
    ReaderStream *stream = find_stream(reader, index);
    if (!stream)
        return MF_E_INVALIDSTREAMNUMBER;
    return stream->descriptor->lpVtbl->GetItem(stream->descriptor, attribute, value);
}

static const IMFSourceReaderVtbl SOURCE_READER_VTBL = {
    reader_query_interface,
    reader_add_ref,
    reader_release,
    reader_get_stream_selection,
    reader_set_stream_selection,
    reader_get_native_media_type,
    reader_get_current_media_type,
    reader_set_current_media_type,
    reader_set_current_position,
    reader_read_sample,
    reader_flush,
    reader_get_service_for_stream,
    reader_get_presentation_attribute,
};

static HRESULT reader_init(SourceReader *reader, IMFAttributes *attributes)
{
    HRESULT hr = reader->source->lpVtbl->CreatePresentationDescriptor(reader->source, &reader->descriptor);
    if (FAILED(hr))
        return hr;
    hr = reader->descriptor->lpVtbl->GetStreamDescriptorCount(reader->descriptor, &reader->count);
    if (FAILED(hr))
        return hr;
    reader->streams = alloc_zero((SIZE_T)reader->count * sizeof(ReaderStream));
    if (!reader->streams)
        return E_OUTOFMEMORY;
    for (DWORD i = 0; i < reader->count; i++) {
        ReaderStream *stream = &reader->streams[i];
        stream->callback.lpVtbl = &STREAM_CALLBACK_VTBL;
        stream->reader = reader;
        stream->index = i;
        hr = reader->descriptor->lpVtbl->GetStreamDescriptorByIndex(reader->descriptor, i, &stream->selected,
                                                                   &stream->descriptor);
        if (SUCCEEDED(hr))
            hr = stream->descriptor->lpVtbl->GetStreamIdentifier(stream->descriptor, &stream->identifier);
        if (SUCCEEDED(hr))
            hr = stream->descriptor->lpVtbl->GetMediaTypeHandler(stream->descriptor, &stream->handler);
        if (FAILED(hr))
            return hr;
        /* every stream starts with the source's current type, else its first one. */
        if (FAILED(stream->handler->lpVtbl->GetCurrentMediaType(stream->handler, &stream->current)) &&
            SUCCEEDED(stream->handler->lpVtbl->GetMediaTypeByIndex(stream->handler, 0, &stream->current)))
            stream->handler->lpVtbl->SetCurrentMediaType(stream->handler, stream->current);
    }
    if (attributes) {
        UINT32 keep = 0;
        if (SUCCEEDED(attributes->lpVtbl->GetUINT32(attributes, &MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN,
                                                    &keep)))
            reader->keep_source = keep != 0;
        if (SUCCEEDED(attributes->lpVtbl->GetUnknown(attributes, &MF_SOURCE_READER_ASYNC_CALLBACK,
                                                     &IID_IMFSourceReaderCallback, (void **)&reader->callback))) {
            hr = MFAllocateWorkQueue(&reader->work_queue);
            if (FAILED(hr))
                return hr;
        }
    }
    return reader->source->lpVtbl->BeginGetEvent(reader->source, &reader->source_callback, nullptr);
}

#define MF_E_UNSUPPORTED_BYTESTREAM_TYPE_ ((HRESULT)0xC00D36C4)

HRESULT WINAPI MFCreateSourceReaderFromMediaSource(IMFMediaSource *source, IMFAttributes *attributes,
                                                   IMFSourceReader **out)
{
    if (!source) {
        if (out)
            *out = nullptr;
        return E_INVALIDARG;
    }
    if (!out)
        return E_POINTER;
    *out = nullptr;
    SourceReader *reader = alloc_zero(sizeof(*reader));
    if (!reader)
        return E_OUTOFMEMORY;
    reader->iface.lpVtbl = &SOURCE_READER_VTBL;
    reader->source_callback.lpVtbl = &SOURCE_CALLBACK_VTBL;
    reader->read_callback.lpVtbl = &READ_CALLBACK_VTBL;
    reader->refs = 1;
    reader->total = 1;
    InitializeCriticalSection(&reader->lock);
    InitializeConditionVariable(&reader->changed);
    reader->source = source;
    source->lpVtbl->AddRef(source);
    HRESULT hr = reader_init(reader, attributes);
    if (FAILED(hr)) {
        reader->keep_source = TRUE;
        reader_release(&reader->iface);
        return hr;
    }
    *out = &reader->iface;
    return S_OK;
}

/*
 * a sink writer encodes samples into a container, which needs the media sinks for the containers and the encoders for
 * the streams, and none of them is here. a url or byte stream names a container no sink is known for, and a media
 * sink given would need a writer made around it.
 */
HRESULT WINAPI MFCreateSinkWriterFromURL(LPCWSTR url, IMFByteStream *stream, IMFAttributes *attributes, void **writer)
{
    if (!writer)
        return E_POINTER;
    *writer = nullptr;
    if (!url && !stream)
        return E_INVALIDARG;
    return MF_E_UNSUPPORTED_BYTESTREAM_TYPE_;
}

HRESULT WINAPI MFCreateSinkWriterFromMediaSink(IUnknown *sink, IMFAttributes *attributes, void **writer)
{
    if (!writer)
        return E_POINTER;
    *writer = nullptr;
    if (!sink)
        return E_INVALIDARG;
    return E_NOTIMPL;
}
