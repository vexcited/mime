/*
 * the media source of a camera: one video stream offering the camera's formats, started and stopped like any live
 * source, whose samples are frames of the memory the host fills (mime/camera.h). the samples point into that memory,
 * and a frame goes back to the host when the program has let go of its sample. the controls of the camera answer
 * through the camera control and the video processing amplifier interfaces. see docs/camera.md.
 */
#include "camera.h"

#define MAX_SLOTS 6
#define MIN_SLOTS 3
/* a stream's memory is one run of contiguous pages: large frames get fewer slots to keep it under this. */
#define STREAM_BUDGET (64u << 20)
#define MAX_TOKENS 16

enum { STATE_STOPPED, STATE_STARTED, STATE_SHUTDOWN };

typedef struct CamSource CamSource;

/* a stream opened in the kernel, kept as long as a program holds one of its samples. */
typedef struct CamContext {
    LONG refs;
    CamDevice *device;
    HANDLE event;
    MimeCameraHeader *hdr;
    BYTE *base;
    DWORD size;
    LONG outstanding;
    DWORD max_outstanding; /* the samples a program may hold at once, the host keeps the other slots to write in */
    UINT64 mf_open;     /* MFGetSystemTime when the stream opened, the clock the samples' reference times are on */
    BOOL open;
    DWORD format_index;  /* of the device's formats, the mode it runs */
    DWORD output_fourcc;
} CamContext;

typedef struct CamStream {
    IMFMediaStream iface;
    CamSource *source;
    IMFMediaEventQueue *queue;
    IMFStreamDescriptor *descriptor;
    CRITICAL_SECTION lock;        /* the tokens, and what the pump knows between frames */
    IUnknown *tokens[MAX_TOKENS];
    DWORD token_count;
    UINT64 last_sequence;
} CamStream;

struct CamSource {
    IMFMediaSourceEx source;
    IMFGetService service;
    IAMCameraControl camera;
    IAMVideoProcAmp procamp;
    IKsControl ks;
    IMFShutdown shutdown;
    IMimeCaptureSource mime;
    LONG refs;
    CRITICAL_SECTION lock;
    int state;
    BOOL announced;               /* MENewStream was sent */
    IMFMediaEventQueue *queue;
    IMFAttributes *attributes;
    IMFPresentationDescriptor *descriptor;
    CamStream stream;
    CamDevice *device;
    DWORD *type_format;           /* the device format of each media type, the default type first */
    DWORD type_count;
    CamContext *context;
    HANDLE pump;
    HANDLE stop_event;
    DWORD output_fourcc;          /* 0 for as the camera sends it */
    IUnknown *d3d_manager;
};

/* ------------------------------------------------------------------ memory buffers over the camera's frames */

typedef struct CamBuffer {
    IMFMediaBuffer iface;
    LONG refs;
    CamContext *context;
    DWORD slot;
    BYTE *data;
    DWORD length;
    BOOL locked;
} CamBuffer;

static void context_release(CamContext *c);

static void context_add_ref(CamContext *c)
{
    InterlockedIncrement(&c->refs);
}

static HRESULT STDMETHODCALLTYPE buf_query_interface(IMFMediaBuffer *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFMediaBuffer)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE buf_add_ref(IMFMediaBuffer *iface)
{
    return (ULONG)InterlockedIncrement(&CONTAINING_RECORD(iface, CamBuffer, iface)->refs);
}

static ULONG STDMETHODCALLTYPE buf_release(IMFMediaBuffer *iface)
{
    CamBuffer *b = CONTAINING_RECORD(iface, CamBuffer, iface);
    LONG refs = InterlockedDecrement(&b->refs);
    if (!refs) {
        CamContext *c = b->context;
        /* the frame goes back to the host, and the pump is woken: frames may be waiting for the room. */
        __atomic_store_n(&c->hdr->slot[b->slot].state, MIME_CAMERA_SLOT_FREE, __ATOMIC_RELEASE);
        InterlockedDecrement(&c->outstanding);
        SetEvent(c->event);
        context_release(c);
        CamFree(b);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE buf_lock(IMFMediaBuffer *iface, BYTE **data, DWORD *max, DWORD *current)
{
    CamBuffer *b = CONTAINING_RECORD(iface, CamBuffer, iface);
    if (!data)
        return E_POINTER;
    *data = b->data;
    if (max)
        *max = b->length;
    if (current)
        *current = b->length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE buf_unlock(IMFMediaBuffer *iface)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE buf_get_current_length(IMFMediaBuffer *iface, DWORD *length)
{
    if (!length)
        return E_POINTER;
    *length = CONTAINING_RECORD(iface, CamBuffer, iface)->length;
    return S_OK;
}

/* the frame is the host's, written once: a program may shorten what it sees of it. */
static HRESULT STDMETHODCALLTYPE buf_set_current_length(IMFMediaBuffer *iface, DWORD length)
{
    CamBuffer *b = CONTAINING_RECORD(iface, CamBuffer, iface);
    if (length > b->length)
        return E_INVALIDARG;
    b->length = length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE buf_get_max_length(IMFMediaBuffer *iface, DWORD *length)
{
    if (!length)
        return E_POINTER;
    *length = CONTAINING_RECORD(iface, CamBuffer, iface)->length;
    return S_OK;
}

static const IMFMediaBufferVtbl BUFFER_VTBL = {
    buf_query_interface, buf_add_ref, buf_release,         buf_lock,
    buf_unlock,          buf_get_current_length, buf_set_current_length, buf_get_max_length,
};

/* ------------------------------------------------------------------ the stream in the kernel */

static void context_release(CamContext *c)
{
    if (InterlockedDecrement(&c->refs))
        return;
    if (c->open)
        CamIoctl(c->device, MIME_CAMERA_IOCTL(MIME_CAMERA_IOCTL_CLOSE), nullptr, 0, nullptr, 0, nullptr);
    if (c->event)
        CloseHandle(c->event);
    CamDeviceRelease(c->device);
    CamFree(c);
}

/* what failing to start a camera is called: the other program has it, or it is gone. */
static HRESULT start_error(HRESULT win32)
{
    if (win32 == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED) ||
        win32 == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        return MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED;
    return MF_E_HW_MFT_FAILED_START_STREAMING;
}

static HRESULT open_context(CamSource *s, DWORD format_index, DWORD output, CamContext **out)
{
    CamContext *c = CamAlloc(sizeof(*c));
    if (!c)
        return E_OUTOFMEMORY;
    c->refs = 1;
    c->device = s->device;
    CamDeviceAddRef(s->device);
    c->format_index = format_index;
    c->output_fourcc = output;
    c->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!c->event) {
        context_release(c);
        return E_OUTOFMEMORY;
    }
    MimeCameraOpen request = {};
    request.format = s->device->formats[format_index];
    request.output_fourcc = output;
    /* the room of a frame, as the kernel sizes a slot, decides how many fit the budget. */
    DWORD frame = mime_camera_frame_bytes(output ? output : request.format.fourcc, request.format.width,
                                          request.format.height);
    DWORD slot = frame ? frame : request.format.width * request.format.height + 65536;
    DWORD slots = STREAM_BUDGET / (((slot + 4095) & ~4095u) ? ((slot + 4095) & ~4095u) : 4096);
    request.slots = slots > MAX_SLOTS ? MAX_SLOTS : slots < MIN_SLOTS ? MIN_SLOTS : slots;
    c->max_outstanding = request.slots / 2;
    request.event = (UINT64)(ULONG_PTR)c->event;
    MimeCameraOpened opened = {};
    BYTE buffer[sizeof(MimeCameraOpen) > sizeof(MimeCameraOpened) ? sizeof(MimeCameraOpen) : sizeof(MimeCameraOpened)];
    memcpy(buffer, &request, sizeof(request));
    HRESULT hr = CamIoctl(s->device, MIME_CAMERA_IOCTL(MIME_CAMERA_IOCTL_OPEN), buffer, sizeof(request), buffer,
                          sizeof(opened), nullptr);
    if (FAILED(hr)) {
        context_release(c);
        return hr;
    }
    memcpy(&opened, buffer, sizeof(opened));
    c->open = TRUE;
    c->base = (BYTE *)(ULONG_PTR)opened.base;
    c->size = opened.size;
    c->hdr = (MimeCameraHeader *)c->base;
    c->mf_open = MFGetSystemTime();
    *out = c;
    return S_OK;
}

static HRESULT run_context(CamContext *c, BOOL run)
{
    DWORD value = run ? 1 : 0;
    return CamIoctl(c->device, MIME_CAMERA_IOCTL(MIME_CAMERA_IOCTL_START), &value, sizeof(value), nullptr, 0, nullptr);
}

/* ------------------------------------------------------------------ samples */

/* a sample is an attribute store: its attribute methods lead its vtable. */
#define SAMPLE_ATTRS(sample) ((IMFAttributes *)(sample))

static HRESULT make_sample(CamSource *s, CamContext *c, DWORD slot, BOOL discontinuity, IMFSample **out)
{
    CamStream *stream = &s->stream;
    MimeCameraHeader *h = c->hdr;
    const MimeCameraSlot *m = &h->slot[slot];
    CamBuffer *b = CamAlloc(sizeof(*b));
    if (!b) {
        __atomic_store_n(&c->hdr->slot[slot].state, MIME_CAMERA_SLOT_FREE, __ATOMIC_RELEASE);
        return E_OUTOFMEMORY;
    }
    b->iface.lpVtbl = &BUFFER_VTBL;
    b->refs = 1;
    b->context = c;
    b->slot = slot;
    b->data = c->base + h->data_offset + (SIZE_T)slot * h->slot_bytes;
    b->length = m->length;
    context_add_ref(c);
    InterlockedIncrement(&c->outstanding);
    IMFSample *sample = nullptr;
    HRESULT hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr))
        hr = sample->lpVtbl->AddBuffer(sample, &b->iface);
    /* the sample has it now, or the buffer goes back to the host. */
    b->iface.lpVtbl->Release(&b->iface);
    if (FAILED(hr)) {
        if (sample)
            sample->lpVtbl->Release(sample);
        return hr;
    }
    LONGLONG since = (LONGLONG)(m->host_ns - h->open_ns) / 100;
    if (since < 0)
        since = 0;
    sample->lpVtbl->SetSampleTime(sample, since);
    sample->lpVtbl->SetSampleDuration(sample, (LONGLONG)h->interval_100ns);
    SAMPLE_ATTRS(sample)->lpVtbl->SetUINT64(SAMPLE_ATTRS(sample), &MFSampleExtension_DeviceReferenceSystemTime,
                              c->mf_open + (UINT64)since);
    SAMPLE_ATTRS(sample)->lpVtbl->SetUINT32(SAMPLE_ATTRS(sample), &MFSampleExtension_CleanPoint, 1);
    if (discontinuity)
        SAMPLE_ATTRS(sample)->lpVtbl->SetUINT32(SAMPLE_ATTRS(sample), &MFSampleExtension_Discontinuity, 1);
    if (m->flags & MIME_CAMERA_FRAME_CORRUPT)
        SAMPLE_ATTRS(sample)->lpVtbl->SetUINT32(SAMPLE_ATTRS(sample), &MFSampleExtension_FrameCorruption, 1);
    /* a program that asked for a sample with a token gets its token back on the one that answers. */
    EnterCriticalSection(&stream->lock);
    IUnknown *token = nullptr;
    if (stream->token_count) {
        token = stream->tokens[0];
        memmove(stream->tokens, stream->tokens + 1, (stream->token_count - 1) * sizeof(stream->tokens[0]));
        stream->token_count--;
    }
    LeaveCriticalSection(&stream->lock);
    if (token) {
        SAMPLE_ATTRS(sample)->lpVtbl->SetUnknown(SAMPLE_ATTRS(sample), &MFSampleExtension_Token, token);
        token->lpVtbl->Release(token);
    }
    *out = sample;
    return S_OK;
}

/* the frames that are ready, newest first taken, the older ones given back: a program that cannot keep up sees the
   present, not the past. */
static void deliver_frames(CamSource *s, CamContext *c)
{
    CamStream *stream = &s->stream;
    MimeCameraHeader *h = c->hdr;
    while ((DWORD)c->outstanding < c->max_outstanding) {
        int best = -1;
        UINT64 best_sequence = 0;
        for (DWORD i = 0; i < h->slots; i++)
            if (__atomic_load_n(&h->slot[i].state, __ATOMIC_ACQUIRE) == MIME_CAMERA_SLOT_READY &&
                h->slot[i].sequence > best_sequence) {
                best = (int)i;
                best_sequence = h->slot[i].sequence;
            }
        if (best < 0)
            return;
        uint32_t expected = MIME_CAMERA_SLOT_READY;
        if (!__atomic_compare_exchange_n(&h->slot[best].state, &expected, MIME_CAMERA_SLOT_HELD, false,
                                         __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            continue;
        UINT64 dropped = 0;
        for (DWORD i = 0; i < h->slots; i++) {
            if ((int)i == best || h->slot[i].sequence >= best_sequence)
                continue;
            uint32_t older = MIME_CAMERA_SLOT_READY;
            if (__atomic_compare_exchange_n(&h->slot[i].state, &older, MIME_CAMERA_SLOT_FREE, false, __ATOMIC_ACQ_REL,
                                            __ATOMIC_RELAXED))
                dropped++;
        }
        BOOL gap = stream->last_sequence && (best_sequence != stream->last_sequence + 1 || dropped);
        stream->last_sequence = best_sequence;
        IMFSample *sample = nullptr;
        if (FAILED(make_sample(s, c, (DWORD)best, gap, &sample)))
            return;
        stream->queue->lpVtbl->QueueEventParamUnk(stream->queue, MEMediaSample, &GUID_NULL, S_OK, (IUnknown *)sample);
        sample->lpVtbl->Release(sample);
    }
}

static DWORD WINAPI pump_thread(void *param)
{
    CamSource *s = param;
    CamContext *c = s->context;
    HANDLE wait[2] = {c->event, s->stop_event};
    BOOL reported = FALSE;
    for (;;) {
        DWORD r = WaitForMultipleObjects(2, wait, FALSE, 1000);
        if (r == WAIT_OBJECT_0 + 1)
            break;
        uint32_t error = __atomic_load_n(&c->hdr->error, __ATOMIC_ACQUIRE);
        if (error != MIME_CAMERA_ERROR_NONE && !reported) {
            /* the camera went or stopped: the program is told once, and the frames already there are still given. */
            reported = TRUE;
            s->queue->lpVtbl->QueueEventParamVar(s->queue, MEError, &GUID_NULL,
                                                 error == MIME_CAMERA_ERROR_LOST ? MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED
                                                                                 : MF_E_HW_MFT_FAILED_START_STREAMING,
                                                 nullptr);
        }
        deliver_frames(s, c);
    }
    return 0;
}

static void stop_pump(CamSource *s)
{
    if (!s->pump)
        return;
    SetEvent(s->stop_event);
    WaitForSingleObject(s->pump, INFINITE);
    CloseHandle(s->pump);
    s->pump = nullptr;
}

/* ------------------------------------------------------------------ media types */

static void set_size(IMFAttributes *a, REFGUID key, UINT32 high, UINT32 low)
{
    a->lpVtbl->SetUINT64(a, key, (UINT64)high << 32 | low);
}

static HRESULT make_type(const MimeCameraFormat *f, IMFMediaType **out)
{
    IMFMediaType *type;
    HRESULT hr = MFCreateMediaType(&type);
    if (FAILED(hr))
        return hr;
    IMFAttributes *a = (IMFAttributes *)type;
    GUID subtype;
    CamSubtype(f->fourcc, &subtype);
    BOOL compressed = f->fourcc == MIME_CAMERA_FOURCC_MJPG;
    DWORD frame = mime_camera_frame_bytes(f->fourcc, f->width, f->height);
    DWORD sample_size = compressed ? f->width * f->height * 3 : frame;
    a->lpVtbl->SetGUID(a, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    a->lpVtbl->SetGUID(a, &MF_MT_SUBTYPE, &subtype);
    set_size(a, &MF_MT_FRAME_SIZE, f->width, f->height);
    set_size(a, &MF_MT_FRAME_RATE, f->fps_num, f->fps_den);
    set_size(a, &MF_MT_FRAME_RATE_RANGE_MAX, f->fps_num, f->fps_den);
    set_size(a, &MF_MT_FRAME_RATE_RANGE_MIN, f->fps_num, f->fps_den);
    set_size(a, &MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    a->lpVtbl->SetUINT32(a, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    a->lpVtbl->SetUINT32(a, &MF_MT_ALL_SAMPLES_INDEPENDENT, 1);
    a->lpVtbl->SetUINT32(a, &MF_MT_FIXED_SIZE_SAMPLES, compressed ? 0 : 1);
    a->lpVtbl->SetUINT32(a, &MF_MT_SAMPLE_SIZE, sample_size);
    a->lpVtbl->SetUINT32(a, &MF_MT_AVG_BITRATE,
                         (UINT32)((UINT64)sample_size * 8 * f->fps_num / (f->fps_den ? f->fps_den : 1)));
    if (compressed) {
        a->lpVtbl->SetUINT32(a, &MF_MT_COMPRESSED, 1);
        a->lpVtbl->SetUINT32(a, &MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_0_255);
    } else {
        a->lpVtbl->SetUINT32(a, &MF_MT_DEFAULT_STRIDE,
                             f->fourcc == MIME_CAMERA_FOURCC_YUY2 || f->fourcc == MIME_CAMERA_FOURCC_UYVY
                                 ? (f->width + 1) / 2 * 4
                                 : f->width);
        a->lpVtbl->SetUINT32(a, &MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    }
    *out = type;
    return S_OK;
}

/* the mode a program that picks none gets: the usual 640 by 480 at 30 frames in a raw format, else the first. */
static DWORD default_format(const CamDevice *d)
{
    DWORD best = 0;
    LONGLONG best_score = -1;
    for (DWORD i = 0; i < d->format_count; i++) {
        const MimeCameraFormat *f = &d->formats[i];
        LONGLONG dw = (LONGLONG)f->width - 640, dh = (LONGLONG)f->height - 480;
        LONGLONG dr = (LONGLONG)(f->fps_num / (f->fps_den ? f->fps_den : 1)) - 30;
        LONGLONG score = 1000000 - (dw < 0 ? -dw : dw) - (dh < 0 ? -dh : dh) - 100 * (dr < 0 ? -dr : dr) -
                         (f->fourcc == MIME_CAMERA_FOURCC_MJPG ? 50000 : 0);
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

/* the format of the device a media type names. */
static BOOL find_format(CamSource *s, IMFMediaType *type, DWORD *index)
{
    IMFAttributes *a = (IMFAttributes *)type;
    GUID major, subtype;
    UINT64 size = 0, rate = 0;
    DWORD fourcc;
    if (FAILED(a->lpVtbl->GetGUID(a, &MF_MT_MAJOR_TYPE, &major)) || !IsEqualGUID(&major, &MFMediaType_Video) ||
        FAILED(a->lpVtbl->GetGUID(a, &MF_MT_SUBTYPE, &subtype)) || !CamFourcc(&subtype, &fourcc) ||
        FAILED(a->lpVtbl->GetUINT64(a, &MF_MT_FRAME_SIZE, &size)))
        return FALSE;
    BOOL has_rate = SUCCEEDED(a->lpVtbl->GetUINT64(a, &MF_MT_FRAME_RATE, &rate));
    for (DWORD i = 0; i < s->type_count; i++) {
        const MimeCameraFormat *f = &s->device->formats[s->type_format[i]];
        if (f->fourcc != fourcc || (UINT64)f->width << 32 != (size & 0xFFFFFFFF00000000ull) ||
            f->height != (UINT32)size)
            continue;
        if (has_rate && (UINT64)(rate >> 32) * f->fps_den != (UINT64)(UINT32)rate * f->fps_num)
            continue;
        *index = s->type_format[i];
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ the stream */

static CamStream *stream_of(IMFMediaStream *iface)
{
    return CONTAINING_RECORD(iface, CamStream, iface);
}

static HRESULT STDMETHODCALLTYPE strm_query_interface(IMFMediaStream *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFMediaEventGenerator) ||
        IsEqualIID(riid, &IID_IMFMediaStream)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

/* the stream lives with its source: they are counted as one. */
static ULONG STDMETHODCALLTYPE strm_add_ref(IMFMediaStream *iface)
{
    return (ULONG)InterlockedIncrement(&stream_of(iface)->source->refs);
}

static ULONG STDMETHODCALLTYPE source_release(IMFMediaSourceEx *iface);

static ULONG STDMETHODCALLTYPE strm_release(IMFMediaStream *iface)
{
    return source_release(&stream_of(iface)->source->source);
}

static HRESULT STDMETHODCALLTYPE strm_get_event(IMFMediaStream *iface, DWORD flags, IMFMediaEvent **event)
{
    CamStream *s = stream_of(iface);
    return s->queue->lpVtbl->GetEvent(s->queue, flags, event);
}

static HRESULT STDMETHODCALLTYPE strm_begin_get_event(IMFMediaStream *iface, IMFAsyncCallback *callback, IUnknown *state)
{
    CamStream *s = stream_of(iface);
    return s->queue->lpVtbl->BeginGetEvent(s->queue, callback, state);
}

static HRESULT STDMETHODCALLTYPE strm_end_get_event(IMFMediaStream *iface, IMFAsyncResult *result, IMFMediaEvent **event)
{
    CamStream *s = stream_of(iface);
    return s->queue->lpVtbl->EndGetEvent(s->queue, result, event);
}

static HRESULT STDMETHODCALLTYPE strm_queue_event(IMFMediaStream *iface, MediaEventType type, REFGUID extended,
                                                  HRESULT status, const PROPVARIANT *value)
{
    CamStream *s = stream_of(iface);
    return s->queue->lpVtbl->QueueEventParamVar(s->queue, type, extended, status, value);
}

static HRESULT STDMETHODCALLTYPE strm_get_media_source(IMFMediaStream *iface, IMFMediaSource **source)
{
    CamStream *s = stream_of(iface);
    if (!source)
        return E_POINTER;
    if (s->source->state == STATE_SHUTDOWN) {
        *source = nullptr;
        return MF_E_SHUTDOWN;
    }
    *source = (IMFMediaSource *)&s->source->source;
    s->source->source.lpVtbl->AddRef(&s->source->source);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE strm_get_stream_descriptor(IMFMediaStream *iface, IMFStreamDescriptor **descriptor)
{
    CamStream *s = stream_of(iface);
    if (!descriptor)
        return E_POINTER;
    if (s->source->state == STATE_SHUTDOWN) {
        *descriptor = nullptr;
        return MF_E_SHUTDOWN;
    }
    *descriptor = s->descriptor;
    s->descriptor->lpVtbl->AddRef(s->descriptor);
    return S_OK;
}

/* a live stream sends its frames as they come: a request only leaves its token to go with one. */
static HRESULT STDMETHODCALLTYPE strm_request_sample(IMFMediaStream *iface, IUnknown *token)
{
    CamStream *s = stream_of(iface);
    if (s->source->state == STATE_SHUTDOWN)
        return MF_E_SHUTDOWN;
    if (s->source->state != STATE_STARTED)
        return MF_E_INVALIDREQUEST;
    if (token) {
        EnterCriticalSection(&s->lock);
        if (s->token_count < MAX_TOKENS) {
            s->tokens[s->token_count++] = token;
            token->lpVtbl->AddRef(token);
        }
        LeaveCriticalSection(&s->lock);
    }
    /* a frame may be waiting for the request. */
    if (s->source->context)
        SetEvent(s->source->context->event);
    return S_OK;
}

static const IMFMediaStreamVtbl STREAM_VTBL = {
    strm_query_interface, strm_add_ref,           strm_release,         strm_get_event,
    strm_begin_get_event, strm_end_get_event,     strm_queue_event,     strm_get_media_source,
    strm_get_stream_descriptor, strm_request_sample,
};

/* ------------------------------------------------------------------ the source */

static CamSource *source_of(IMFMediaSourceEx *iface)
{
    return CONTAINING_RECORD(iface, CamSource, source);
}

static HRESULT STDMETHODCALLTYPE src_query_interface(IMFMediaSourceEx *iface, REFIID riid, void **out)
{
    CamSource *s = source_of(iface);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFMediaEventGenerator) ||
        IsEqualIID(riid, &IID_IMFMediaSource) || IsEqualIID(riid, &IID_IMFMediaSourceEx))
        *out = &s->source;
    else if (IsEqualIID(riid, &IID_IMFGetService))
        *out = &s->service;
    else if (IsEqualIID(riid, &IID_IAMCameraControl))
        *out = &s->camera;
    else if (IsEqualIID(riid, &IID_IAMVideoProcAmp))
        *out = &s->procamp;
    else if (IsEqualIID(riid, &IID_IKsControl))
        *out = &s->ks;
    else if (IsEqualIID(riid, &IID_IMFShutdown))
        *out = &s->shutdown;
    else if (IsEqualIID(riid, &IID_IMimeCaptureSource))
        *out = &s->mime;
    if (!*out)
        return E_NOINTERFACE;
    InterlockedIncrement(&s->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE src_add_ref(IMFMediaSourceEx *iface)
{
    return (ULONG)InterlockedIncrement(&source_of(iface)->refs);
}

static void source_stop_locked(CamSource *s, BOOL send_events)
{
    if (s->state != STATE_STARTED)
        return;
    stop_pump(s);
    if (s->context)
        run_context(s->context, FALSE);
    s->state = STATE_STOPPED;
    if (send_events) {
        PROPVARIANT v = {};
        s->stream.queue->lpVtbl->QueueEventParamVar(s->stream.queue, MEStreamStopped, &GUID_NULL, S_OK, &v);
        s->queue->lpVtbl->QueueEventParamVar(s->queue, MESourceStopped, &GUID_NULL, S_OK, &v);
    }
}

/* the end of the source: the stream stops, the memory the frames were in is given back once the last sample is. */
static HRESULT source_shutdown_locked(CamSource *s)
{
    if (s->state == STATE_SHUTDOWN)
        return MF_E_SHUTDOWN;
    source_stop_locked(s, FALSE);
    s->state = STATE_SHUTDOWN;
    if (s->context) {
        context_release(s->context);
        s->context = nullptr;
    }
    if (s->queue)
        s->queue->lpVtbl->Shutdown(s->queue);
    if (s->stream.queue)
        s->stream.queue->lpVtbl->Shutdown(s->stream.queue);
    return S_OK;
}

static void source_free(CamSource *s)
{
    EnterCriticalSection(&s->lock);
    source_shutdown_locked(s);
    LeaveCriticalSection(&s->lock);
    for (DWORD i = 0; i < s->stream.token_count; i++)
        s->stream.tokens[i]->lpVtbl->Release(s->stream.tokens[i]);
    if (s->stream.descriptor)
        s->stream.descriptor->lpVtbl->Release(s->stream.descriptor);
    if (s->stream.queue)
        s->stream.queue->lpVtbl->Release(s->stream.queue);
    if (s->descriptor)
        s->descriptor->lpVtbl->Release(s->descriptor);
    if (s->queue)
        s->queue->lpVtbl->Release(s->queue);
    if (s->attributes)
        s->attributes->lpVtbl->Release(s->attributes);
    if (s->d3d_manager)
        s->d3d_manager->lpVtbl->Release(s->d3d_manager);
    if (s->stop_event)
        CloseHandle(s->stop_event);
    if (s->device)
        CamDeviceRelease(s->device);
    CamFree(s->type_format);
    DeleteCriticalSection(&s->stream.lock);
    DeleteCriticalSection(&s->lock);
    CamFree(s);
}

static ULONG STDMETHODCALLTYPE source_release(IMFMediaSourceEx *iface)
{
    CamSource *s = source_of(iface);
    LONG refs = InterlockedDecrement(&s->refs);
    if (!refs)
        source_free(s);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE src_get_event(IMFMediaSourceEx *iface, DWORD flags, IMFMediaEvent **event)
{
    CamSource *s = source_of(iface);
    return s->queue->lpVtbl->GetEvent(s->queue, flags, event);
}

static HRESULT STDMETHODCALLTYPE src_begin_get_event(IMFMediaSourceEx *iface, IMFAsyncCallback *callback, IUnknown *state)
{
    CamSource *s = source_of(iface);
    return s->queue->lpVtbl->BeginGetEvent(s->queue, callback, state);
}

static HRESULT STDMETHODCALLTYPE src_end_get_event(IMFMediaSourceEx *iface, IMFAsyncResult *result, IMFMediaEvent **event)
{
    CamSource *s = source_of(iface);
    return s->queue->lpVtbl->EndGetEvent(s->queue, result, event);
}

static HRESULT STDMETHODCALLTYPE src_queue_event(IMFMediaSourceEx *iface, MediaEventType type, REFGUID extended,
                                                 HRESULT status, const PROPVARIANT *value)
{
    CamSource *s = source_of(iface);
    return s->queue->lpVtbl->QueueEventParamVar(s->queue, type, extended, status, value);
}

static HRESULT STDMETHODCALLTYPE src_get_characteristics(IMFMediaSourceEx *iface, DWORD *characteristics)
{
    if (!characteristics)
        return E_POINTER;
    if (source_of(iface)->state == STATE_SHUTDOWN)
        return MF_E_SHUTDOWN;
    *characteristics = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE src_create_presentation_descriptor(IMFMediaSourceEx *iface,
                                                                    IMFPresentationDescriptor **descriptor)
{
    CamSource *s = source_of(iface);
    if (!descriptor)
        return E_POINTER;
    *descriptor = nullptr;
    if (s->state == STATE_SHUTDOWN)
        return MF_E_SHUTDOWN;
    return s->descriptor->lpVtbl->Clone(s->descriptor, descriptor);
}

static HRESULT STDMETHODCALLTYPE src_start(IMFMediaSourceEx *iface, IMFPresentationDescriptor *pd, const GUID *time_format,
                                           const PROPVARIANT *position)
{
    CamSource *s = source_of(iface);
    if (!pd || !position)
        return E_INVALIDARG;
    if (time_format && !IsEqualGUID(time_format, &GUID_NULL))
        return MF_E_UNSUPPORTED_TIME_FORMAT;
    if (position->vt != VT_EMPTY && position->vt != VT_I8)
        return E_INVALIDARG;
    BOOL selected = FALSE;
    IMFStreamDescriptor *sd = nullptr;
    HRESULT hr = pd->lpVtbl->GetStreamDescriptorByIndex(pd, 0, &selected, &sd);
    if (FAILED(hr))
        return hr;
    EnterCriticalSection(&s->lock);
    if (s->state == STATE_SHUTDOWN) {
        LeaveCriticalSection(&s->lock);
        sd->lpVtbl->Release(sd);
        return MF_E_SHUTDOWN;
    }
    /* the type the program chose on the stream's handler is the mode to run. */
    DWORD format = 0;
    hr = S_OK;
    if (selected) {
        IMFMediaTypeHandler *handler = nullptr;
        IMFMediaType *type = nullptr;
        hr = sd->lpVtbl->GetMediaTypeHandler(sd, &handler);
        if (SUCCEEDED(hr))
            hr = handler->lpVtbl->GetCurrentMediaType(handler, &type);
        if (SUCCEEDED(hr) && !find_format(s, type, &format))
            hr = MF_E_INVALIDMEDIATYPE;
        if (type)
            type->lpVtbl->Release(type);
        if (handler)
            handler->lpVtbl->Release(handler);
    }
    sd->lpVtbl->Release(sd);
    PROPVARIANT started = {.vt = VT_I8};
    if (position->vt == VT_I8)
        started.hVal = position->hVal;
    if (FAILED(hr)) {
        s->queue->lpVtbl->QueueEventParamVar(s->queue, MEError, &GUID_NULL, hr, nullptr);
        LeaveCriticalSection(&s->lock);
        return S_OK;
    }
    if (!selected) {
        source_stop_locked(s, FALSE);
        s->queue->lpVtbl->QueueEventParamVar(s->queue, MESourceStarted, &GUID_NULL, S_OK, &started);
        LeaveCriticalSection(&s->lock);
        return S_OK;
    }
    /* a stream running another mode, or delivering another format, is opened again. */
    DWORD wanted = s->output_fourcc ? s->output_fourcc : s->device->formats[format].fourcc;
    BOOL running = s->state == STATE_STARTED;
    if (s->context && (s->context->format_index != format || s->context->output_fourcc != wanted)) {
        if (running)
            source_stop_locked(s, FALSE);
        running = FALSE;
        context_release(s->context);
        s->context = nullptr;
    }
    if (!s->context) {
        DWORD convert = s->output_fourcc && s->output_fourcc != s->device->formats[format].fourcc ? s->output_fourcc : 0;
        hr = open_context(s, format, convert, &s->context);
        if (SUCCEEDED(hr))
            s->context->output_fourcc = wanted;
    }
    if (SUCCEEDED(hr) && !running) {
        s->stream.last_sequence = 0;
        hr = run_context(s->context, TRUE);
        if (FAILED(hr)) {
            hr = start_error(hr);
            context_release(s->context);
            s->context = nullptr;
        }
    }
    if (FAILED(hr)) {
        s->queue->lpVtbl->QueueEventParamVar(s->queue, MEError, &GUID_NULL, hr, nullptr);
        LeaveCriticalSection(&s->lock);
        return S_OK;
    }
    /* the events come before the first frame. */
    PROPVARIANT value = {.vt = VT_UNKNOWN, .punkVal = (IUnknown *)&s->stream.iface};
    s->queue->lpVtbl->QueueEventParamVar(s->queue, s->announced ? MEUpdatedStream : MENewStream, &GUID_NULL, S_OK, &value);
    s->announced = TRUE;
    s->queue->lpVtbl->QueueEventParamVar(s->queue, MESourceStarted, &GUID_NULL, S_OK, &started);
    s->stream.queue->lpVtbl->QueueEventParamVar(s->stream.queue, MEStreamStarted, &GUID_NULL, S_OK, &started);
    if (!running) {
        ResetEvent(s->stop_event);
        s->pump = CreateThread(nullptr, 0, pump_thread, s, 0, nullptr);
        if (!s->pump) {
            run_context(s->context, FALSE);
            s->queue->lpVtbl->QueueEventParamVar(s->queue, MEError, &GUID_NULL, E_OUTOFMEMORY, nullptr);
            LeaveCriticalSection(&s->lock);
            return S_OK;
        }
    }
    s->state = STATE_STARTED;
    LeaveCriticalSection(&s->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE src_stop(IMFMediaSourceEx *iface)
{
    CamSource *s = source_of(iface);
    EnterCriticalSection(&s->lock);
    HRESULT hr = S_OK;
    if (s->state == STATE_SHUTDOWN)
        hr = MF_E_SHUTDOWN;
    else
        source_stop_locked(s, TRUE);
    LeaveCriticalSection(&s->lock);
    return hr;
}

/* a live source cannot pause: the frames would be lost. */
static HRESULT STDMETHODCALLTYPE src_pause(IMFMediaSourceEx *iface)
{
    CamSource *s = source_of(iface);
    return s->state == STATE_SHUTDOWN ? MF_E_SHUTDOWN : MF_E_INVALID_STATE_TRANSITION;
}

static HRESULT STDMETHODCALLTYPE src_shutdown(IMFMediaSourceEx *iface)
{
    CamSource *s = source_of(iface);
    EnterCriticalSection(&s->lock);
    HRESULT hr = source_shutdown_locked(s);
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE src_get_source_attributes(IMFMediaSourceEx *iface, IMFAttributes **attributes)
{
    CamSource *s = source_of(iface);
    if (!attributes)
        return E_POINTER;
    *attributes = nullptr;
    if (s->state == STATE_SHUTDOWN)
        return MF_E_SHUTDOWN;
    *attributes = s->attributes;
    s->attributes->lpVtbl->AddRef(s->attributes);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE src_get_stream_attributes(IMFMediaSourceEx *iface, DWORD stream, IMFAttributes **attributes)
{
    CamSource *s = source_of(iface);
    if (!attributes)
        return E_POINTER;
    *attributes = nullptr;
    if (s->state == STATE_SHUTDOWN)
        return MF_E_SHUTDOWN;
    if (stream != 0)
        return MF_E_INVALIDSTREAMNUMBER;
    return MFCreateAttributes(attributes, 0);
}

/* the manager is kept and not used: the frames are in memory, and a program that wants textures copies them. */
static HRESULT STDMETHODCALLTYPE src_set_d3d_manager(IMFMediaSourceEx *iface, IUnknown *manager)
{
    CamSource *s = source_of(iface);
    if (s->state == STATE_SHUTDOWN)
        return MF_E_SHUTDOWN;
    EnterCriticalSection(&s->lock);
    IUnknown *old = s->d3d_manager;
    s->d3d_manager = manager;
    if (manager)
        manager->lpVtbl->AddRef(manager);
    LeaveCriticalSection(&s->lock);
    if (old)
        old->lpVtbl->Release(old);
    return S_OK;
}

static const IMFMediaSourceExVtbl SOURCE_VTBL = {
    src_query_interface, src_add_ref, source_release, src_get_event, src_begin_get_event, src_end_get_event,
    src_queue_event, src_get_characteristics, src_create_presentation_descriptor, src_start, src_stop, src_pause,
    src_shutdown, src_get_source_attributes, src_get_stream_attributes, src_set_d3d_manager,
};

/* ------------------------------------------------------------------ the other interfaces of the source */

#define SUB_SOURCE(Type, member) (&CONTAINING_RECORD((Type *)iface, CamSource, member)->source)
#define FORWARD_IUNKNOWN(prefix, Type, member)                                                                         \
    static HRESULT STDMETHODCALLTYPE prefix##_query_interface(Type *iface, REFIID riid, void **out)                    \
    {                                                                                                                  \
        return src_query_interface(SUB_SOURCE(Type, member), riid, out);                                               \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_add_ref(Type *iface)                                                       \
    {                                                                                                                  \
        return src_add_ref(SUB_SOURCE(Type, member));                                                                  \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_release(Type *iface)                                                       \
    {                                                                                                                  \
        return source_release(SUB_SOURCE(Type, member));                                                               \
    }

FORWARD_IUNKNOWN(svc, IMFGetService, service)
FORWARD_IUNKNOWN(cam, IAMCameraControl, camera)
FORWARD_IUNKNOWN(amp, IAMVideoProcAmp, procamp)
FORWARD_IUNKNOWN(ks, IKsControl, ks)
FORWARD_IUNKNOWN(sd, IMFShutdown, shutdown)
FORWARD_IUNKNOWN(mime, IMimeCaptureSource, mime)

/* the services of a camera that windows has, such as its extended controls, are not here. */
static HRESULT STDMETHODCALLTYPE svc_get_service(IMFGetService *iface, REFGUID service, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return MF_E_UNSUPPORTED_SERVICE;
}

static const IMFGetServiceVtbl SERVICE_VTBL = {svc_query_interface, svc_add_ref, svc_release, svc_get_service};

static HRESULT STDMETHODCALLTYPE sd_shutdown(IMFShutdown *iface)
{
    return src_shutdown(SUB_SOURCE(IMFShutdown, shutdown));
}

static HRESULT STDMETHODCALLTYPE sd_get_status(IMFShutdown *iface, DWORD *status)
{
    if (!status)
        return E_POINTER;
    *status = CONTAINING_RECORD(iface, CamSource, shutdown)->state == STATE_SHUTDOWN ? 1 : 0; /* MFSHUTDOWN_COMPLETED */
    return S_OK;
}

static const IMFShutdownVtbl SHUTDOWN_VTBL = {sd_query_interface, sd_add_ref, sd_release, sd_shutdown, sd_get_status};

/* kernel streaming's own requests are for the driver's properties, which this camera has none of. */
static HRESULT STDMETHODCALLTYPE ks_property(IKsControl *iface, PKSPROPERTY property, ULONG length, void *data,
                                             ULONG data_length, ULONG *returned)
{
    if (returned)
        *returned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

static HRESULT STDMETHODCALLTYPE ks_method(IKsControl *iface, PKSMETHOD method, ULONG length, void *data,
                                           ULONG data_length, ULONG *returned)
{
    if (returned)
        *returned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

static HRESULT STDMETHODCALLTYPE ks_event(IKsControl *iface, PKSEVENT event, ULONG length, void *data, ULONG data_length,
                                          ULONG *returned)
{
    if (returned)
        *returned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

static const IKsControlVtbl KS_VTBL = {ks_query_interface, ks_add_ref, ks_release, ks_property, ks_method, ks_event};

/* the controls: a request to the camera, whose answer says what the camera could do. */
static HRESULT control(CamSource *s, DWORD set, LONG property, DWORD operation, MimeCameraControl *c)
{
    if (s->state == STATE_SHUTDOWN)
        return MF_E_SHUTDOWN;
    if (property < 0)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    c->set = set;
    c->property = (DWORD)property;
    c->operation = operation;
    return CamIoctl(s->device, MIME_CAMERA_IOCTL(MIME_CAMERA_IOCTL_CONTROL), c, sizeof(*c), c, sizeof(*c), nullptr);
}

static HRESULT get_range(CamSource *s, DWORD set, LONG property, LONG *min, LONG *max, LONG *step, LONG *def, LONG *caps)
{
    if (!min || !max || !step || !def || !caps)
        return E_POINTER;
    MimeCameraControl c = {};
    HRESULT hr = control(s, set, property, MIME_CAMERA_CONTROL_RANGE, &c);
    if (FAILED(hr))
        return hr;
    *min = c.min;
    *max = c.max;
    *step = c.step;
    *def = c.default_value;
    *caps = (c.flags & MIME_CAMERA_CONTROL_FLAG_AUTO ? 1 : 0) | (c.flags & MIME_CAMERA_CONTROL_FLAG_MANUAL ? 2 : 0);
    return S_OK;
}

static HRESULT get_value(CamSource *s, DWORD set, LONG property, LONG *value, LONG *flags)
{
    if (!value || !flags)
        return E_POINTER;
    MimeCameraControl c = {};
    HRESULT hr = control(s, set, property, MIME_CAMERA_CONTROL_GET, &c);
    if (FAILED(hr))
        return hr;
    *value = c.value;
    *flags = c.flags & MIME_CAMERA_CONTROL_FLAG_AUTO ? 1 : 2;
    return S_OK;
}

static HRESULT set_value(CamSource *s, DWORD set, LONG property, LONG value, LONG flags)
{
    if (flags != 1 && flags != 2)
        return E_INVALIDARG;
    MimeCameraControl c = {};
    c.value = value;
    c.flags = flags == 1 ? MIME_CAMERA_CONTROL_FLAG_AUTO : MIME_CAMERA_CONTROL_FLAG_MANUAL;
    return control(s, set, property, MIME_CAMERA_CONTROL_SET, &c);
}

static HRESULT STDMETHODCALLTYPE cam_get_range(IAMCameraControl *iface, LONG property, LONG *min, LONG *max, LONG *step,
                                               LONG *def, LONG *flags)
{
    return get_range(CONTAINING_RECORD(iface, CamSource, camera), MIME_CAMERA_CONTROL_CAMERA, property, min, max, step,
                     def, flags);
}
static HRESULT STDMETHODCALLTYPE cam_set(IAMCameraControl *iface, LONG property, LONG value, LONG flags)
{
    return set_value(CONTAINING_RECORD(iface, CamSource, camera), MIME_CAMERA_CONTROL_CAMERA, property, value, flags);
}
static HRESULT STDMETHODCALLTYPE cam_get(IAMCameraControl *iface, LONG property, LONG *value, LONG *flags)
{
    return get_value(CONTAINING_RECORD(iface, CamSource, camera), MIME_CAMERA_CONTROL_CAMERA, property, value, flags);
}
static const IAMCameraControlVtbl CAMERA_VTBL = {cam_query_interface, cam_add_ref, cam_release, cam_get_range, cam_set,
                                                 cam_get};

static HRESULT STDMETHODCALLTYPE amp_get_range(IAMVideoProcAmp *iface, LONG property, LONG *min, LONG *max, LONG *step,
                                               LONG *def, LONG *flags)
{
    return get_range(CONTAINING_RECORD(iface, CamSource, procamp), MIME_CAMERA_CONTROL_PROCAMP, property, min, max, step,
                     def, flags);
}
static HRESULT STDMETHODCALLTYPE amp_set(IAMVideoProcAmp *iface, LONG property, LONG value, LONG flags)
{
    return set_value(CONTAINING_RECORD(iface, CamSource, procamp), MIME_CAMERA_CONTROL_PROCAMP, property, value, flags);
}
static HRESULT STDMETHODCALLTYPE amp_get(IAMVideoProcAmp *iface, LONG property, LONG *value, LONG *flags)
{
    return get_value(CONTAINING_RECORD(iface, CamSource, procamp), MIME_CAMERA_CONTROL_PROCAMP, property, value, flags);
}
static const IAMVideoProcAmpVtbl PROCAMP_VTBL = {amp_query_interface, amp_add_ref, amp_release, amp_get_range, amp_set,
                                                 amp_get};

/* the capture engine asks for the format it will hand to its sinks. */
static HRESULT STDMETHODCALLTYPE mime_set_output_subtype(IMimeCaptureSource *iface, DWORD stream, const GUID *subtype)
{
    CamSource *s = CONTAINING_RECORD(iface, CamSource, mime);
    DWORD fourcc = 0;
    if (stream != 0)
        return MF_E_INVALIDSTREAMNUMBER;
    if (subtype && !IsEqualGUID(subtype, &GUID_NULL) && !CamFourcc(subtype, &fourcc))
        return MF_E_INVALIDMEDIATYPE;
    if (fourcc && !MIME_CAMERA_CONVERTS_TO(fourcc))
        return MF_E_INVALIDMEDIATYPE;
    EnterCriticalSection(&s->lock);
    s->output_fourcc = fourcc;
    LeaveCriticalSection(&s->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mime_can_convert_to(IMimeCaptureSource *iface, DWORD stream, const GUID *from,
                                                     const GUID *to)
{
    DWORD a, b;
    if (stream != 0)
        return MF_E_INVALIDSTREAMNUMBER;
    if (!from || !to || !CamFourcc(from, &a) || !CamFourcc(to, &b))
        return MF_E_INVALIDMEDIATYPE;
    if (a == b)
        return S_OK;
    BOOL source_ok = a == MIME_CAMERA_FOURCC_YUY2 || a == MIME_CAMERA_FOURCC_UYVY || a == MIME_CAMERA_FOURCC_NV12 ||
                     a == MIME_CAMERA_FOURCC_I420 || a == MIME_CAMERA_FOURCC_YV12 || a == MIME_CAMERA_FOURCC_MJPG;
    return source_ok && MIME_CAMERA_CONVERTS_TO(b) ? S_OK : S_FALSE;
}

static const IMimeCaptureSourceVtbl MIME_VTBL = {mime_query_interface, mime_add_ref, mime_release,
                                                 mime_set_output_subtype, mime_can_convert_to};

/* ------------------------------------------------------------------ creation */

HRESULT CamCreateSource(IMFAttributes *attributes, IMFMediaSource **out)
{
    *out = nullptr;
    WCHAR *link = nullptr;
    UINT32 chars = 0;
    HRESULT hr = attributes->lpVtbl->GetAllocatedString(attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                                        &link, &chars);
    if (FAILED(hr))
        return hr;
    CamDevice *device = nullptr;
    hr = CamOpenDevice(link, &device);
    CoTaskMemFree(link);
    if (FAILED(hr))
        return hr;
    if (!device->format_count) {
        CamDeviceRelease(device);
        return MF_E_NO_CAPTURE_DEVICES_AVAILABLE;
    }
    CamSource *s = CamAlloc(sizeof(*s));
    if (!s) {
        CamDeviceRelease(device);
        return E_OUTOFMEMORY;
    }
    s->source.lpVtbl = &SOURCE_VTBL;
    s->service.lpVtbl = &SERVICE_VTBL;
    s->camera.lpVtbl = &CAMERA_VTBL;
    s->procamp.lpVtbl = &PROCAMP_VTBL;
    s->ks.lpVtbl = &KS_VTBL;
    s->shutdown.lpVtbl = &SHUTDOWN_VTBL;
    s->mime.lpVtbl = &MIME_VTBL;
    s->refs = 1;
    s->device = device;
    s->stream.iface.lpVtbl = &STREAM_VTBL;
    s->stream.source = s;
    InitializeCriticalSection(&s->lock);
    InitializeCriticalSection(&s->stream.lock);
    s->stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    hr = s->stop_event ? S_OK : E_OUTOFMEMORY;

    IMFMediaType **types = nullptr;
    if (SUCCEEDED(hr)) {
        types = CamAlloc(device->format_count * sizeof(*types));
        s->type_format = CamAlloc(device->format_count * sizeof(DWORD));
        if (!types || !s->type_format)
            hr = E_OUTOFMEMORY;
    }
    /* the default mode first, the others in the order the camera lists them. */
    if (SUCCEEDED(hr)) {
        DWORD first = default_format(device), n = 0;
        s->type_format[n++] = first;
        for (DWORD i = 0; i < device->format_count; i++)
            if (i != first)
                s->type_format[n++] = i;
        s->type_count = n;
        for (DWORD i = 0; i < n && SUCCEEDED(hr); i++)
            hr = make_type(&device->formats[s->type_format[i]], &types[i]);
    }
    if (SUCCEEDED(hr))
        hr = MFCreateEventQueue(&s->queue);
    if (SUCCEEDED(hr))
        hr = MFCreateEventQueue(&s->stream.queue);
    if (SUCCEEDED(hr))
        hr = MFCreateStreamDescriptor(0, s->type_count, types, &s->stream.descriptor);
    if (SUCCEEDED(hr)) {
        IMFMediaTypeHandler *handler;
        hr = s->stream.descriptor->lpVtbl->GetMediaTypeHandler(s->stream.descriptor, &handler);
        if (SUCCEEDED(hr)) {
            hr = handler->lpVtbl->SetCurrentMediaType(handler, types[0]);
            handler->lpVtbl->Release(handler);
        }
    }
    if (SUCCEEDED(hr))
        hr = MFCreatePresentationDescriptor(1, &s->stream.descriptor, &s->descriptor);
    if (SUCCEEDED(hr))
        hr = s->descriptor->lpVtbl->SelectStream(s->descriptor, 0);
    if (SUCCEEDED(hr))
        hr = MFCreateAttributes(&s->attributes, 4);
    if (SUCCEEDED(hr)) {
        s->attributes->lpVtbl->CopyAllItems(attributes, s->attributes);
        WCHAR name[128];
        if (SUCCEEDED(CamFriendlyName(device->path, name, RTL_NUMBER_OF(name))))
            s->attributes->lpVtbl->SetString(s->attributes, &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, name);
        s->attributes->lpVtbl->SetGUID(s->attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                       &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    }
    for (DWORD i = 0; types && i < device->format_count; i++)
        if (types[i])
            types[i]->lpVtbl->Release(types[i]);
    CamFree(types);
    if (FAILED(hr)) {
        s->source.lpVtbl->Release(&s->source);
        return hr;
    }
    *out = (IMFMediaSource *)&s->source;
    return S_OK;
}
