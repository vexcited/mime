#include "dsound_private.h"
#include "../lib/audio/devices.h"
#include <mime/ntuser.h>

/* how often the recording thread moves what the host recorded into the buffer. */
#define CAPTURE_MS 5

typedef struct CAPTURE {
    IDirectSoundCapture iface;
    LONG ref;
    BOOL v8;
    BOOL initialized;
    BOOL follows; /* the default device, whichever it is when a buffer opens. */
    GUID guid;
} CAPTURE;

typedef struct CBUFFER {
    IDirectSoundCaptureBuffer8 iface;
    IDirectSoundNotify notify_iface;
    LONG ref;
    RTL_CRITICAL_SECTION lock;
    WAVEFORMATEX *format;
    DWORD flags;
    BYTE *data;
    DWORD size;
    DWORD position;  /* the next byte recorded: windows' read cursor. */
    BOOL capturing, looping;
    BOOL narrow8;    /* recorded in sixteen bits, kept in eight. */
    MimeAudioHeader *hdr;
    BYTE *ring;
    ULONG slot;
    UINT32 ring_frames, ring_frame;
    NOTIFY *notifies;
    DWORD notify_count;
    HANDLE thread, wake;
    volatile BOOL quit;
} CBUFFER;

static HANDLE heap(void)
{
    return GetProcessHeap();
}

/* ------------------------------------------------------------------ the buffer */

static CBUFFER *buffer_from(IDirectSoundCaptureBuffer8 *iface)
{
    return CONTAINING_RECORD(iface, CBUFFER, iface);
}

static CBUFFER *buffer_from_notify(IDirectSoundNotify *iface)
{
    return CONTAINING_RECORD(iface, CBUFFER, notify_iface);
}

static void signal_span(const CBUFFER *b, DWORD from, DWORD bytes)
{
    for (DWORD i = 0; i < b->notify_count; i++) {
        DWORD offset = b->notifies[i].offset;
        if (offset != DSBPN_OFFSETSTOP && offset >= from && offset - from < bytes)
            SetEvent(b->notifies[i].event);
    }
}

static void signal_stop(const CBUFFER *b)
{
    for (DWORD i = 0; i < b->notify_count; i++) {
        if (b->notifies[i].offset == DSBPN_OFFSETSTOP)
            SetEvent(b->notifies[i].event);
    }
}

/* called with the lock. a buffer that does not loop stops once it is full, with its cursor back at the start. */
static void drain(CBUFFER *b)
{
    DWORD align = b->format->nBlockAlign;
    UINT64 written = __atomic_load_n(&b->hdr->written, __ATOMIC_ACQUIRE), played = b->hdr->played;
    if (written - played > b->ring_frames)
        played = written - b->ring_frames;
    while (played != written && b->capturing) {
        UINT32 start = (UINT32)(played % b->ring_frames);
        UINT64 frames = written - played;
        if (frames > b->ring_frames - start)
            frames = b->ring_frames - start;
        if (frames > (b->size - b->position) / align)
            frames = (b->size - b->position) / align;
        const BYTE *src = b->ring + (SIZE_T)start * b->ring_frame;
        BYTE *dst = b->data + b->position;
        if (b->narrow8) {
            UINT32 samples = (UINT32)frames * b->format->nChannels;
            for (UINT32 i = 0; i < samples; i++)
                dst[i] = (BYTE)((((const INT16 *)src)[i] >> 8) + 128);
        } else {
            memcpy(dst, src, (SIZE_T)frames * align);
        }
        DWORD bytes = (DWORD)frames * align;
        played += frames;
        /* an offset is reached when the cursor passes it, so the one at zero waits for the wrap. */
        signal_span(b, b->position + 1, bytes);
        b->position += bytes;
        if (b->size - b->position < align) {
            b->position = 0;
            signal_span(b, 0, 1);
            if (!b->looping) {
                b->capturing = FALSE;
                NtGdiMimeAudioControl(b->slot, MIME_AUDIO_STOP);
                signal_stop(b);
            }
        }
    }
    if (!b->capturing)
        played = written;
    __atomic_store_n(&b->hdr->played, played, __ATOMIC_RELEASE);
}

static DWORD WINAPI capture_thread(void *arg)
{
    CBUFFER *b = arg;
    MimeOutputPeriodBegin();
    while (!b->quit) {
        WaitForSingleObject(b->wake, b->capturing ? CAPTURE_MS : INFINITE);
        RtlEnterCriticalSection(&b->lock);
        if (b->capturing && !b->quit)
            drain(b);
        RtlLeaveCriticalSection(&b->lock);
    }
    MimeOutputPeriodEnd();
    return 0;
}

static void buffer_destroy(CBUFFER *b)
{
    if (b->thread) {
        b->quit = TRUE;
        SetEvent(b->wake);
        WaitForSingleObject(b->thread, INFINITE);
        CloseHandle(b->thread);
    }
    if (b->wake)
        CloseHandle(b->wake);
    if (b->hdr) {
        NtGdiMimeAudioControl(b->slot, MIME_AUDIO_STOP);
        NtGdiMimeAudioControl(b->slot, MIME_AUDIO_CLOSE);
    }
    RtlDeleteCriticalSection(&b->lock);
    HeapFree(heap(), 0, b->notifies);
    HeapFree(heap(), 0, b->format);
    HeapFree(heap(), 0, b->data);
    HeapFree(heap(), 0, b);
}

static HRESULT buffer_query_any(CBUFFER *b, REFIID riid, void **out)
{
    if (!out)
        return DSERR_INVALIDPARAM;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirectSoundCaptureBuffer) ||
        IsEqualGUID(riid, &IID_IDirectSoundCaptureBuffer8))
        *out = &b->iface;
    else if (IsEqualGUID(riid, &IID_IDirectSoundNotify))
        *out = &b->notify_iface;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&b->ref);
    return S_OK;
}

static ULONG buffer_release_any(CBUFFER *b)
{
    LONG ref = InterlockedDecrement(&b->ref);
    if (!ref)
        buffer_destroy(b);
    return ref;
}

static HRESULT STDMETHODCALLTYPE buffer_query(IDirectSoundCaptureBuffer8 *This, REFIID riid, void **out)
{
    return buffer_query_any(buffer_from(This), riid, out);
}

static ULONG STDMETHODCALLTYPE buffer_add_ref(IDirectSoundCaptureBuffer8 *This)
{
    return InterlockedIncrement(&buffer_from(This)->ref);
}

static ULONG STDMETHODCALLTYPE buffer_release(IDirectSoundCaptureBuffer8 *This)
{
    return buffer_release_any(buffer_from(This));
}

static HRESULT STDMETHODCALLTYPE buffer_get_caps(IDirectSoundCaptureBuffer8 *This, LPDSCBCAPS caps)
{
    CBUFFER *b = buffer_from(This);
    if (!caps || caps->dwSize < sizeof(DSCBCAPS))
        return DSERR_INVALIDPARAM;
    caps->dwSize = sizeof(DSCBCAPS);
    caps->dwFlags = b->flags;
    caps->dwBufferBytes = b->size;
    caps->dwReserved = 0;
    return DS_OK;
}

/* the capture cursor runs ahead of the read cursor by what the device holds and has not handed over yet. */
static HRESULT STDMETHODCALLTYPE buffer_get_position(IDirectSoundCaptureBuffer8 *This, LPDWORD capture, LPDWORD read)
{
    CBUFFER *b = buffer_from(This);
    RtlEnterCriticalSection(&b->lock);
    if (b->capturing)
        drain(b);
    DWORD position = b->position, ahead = 0;
    if (b->capturing) {
        DWORD align = b->format->nBlockAlign;
        ahead = b->format->nSamplesPerSec / 100 * align;
        if (ahead >= b->size)
            ahead = b->size - align;
    }
    RtlLeaveCriticalSection(&b->lock);
    if (capture)
        *capture = (position + ahead) % b->size;
    if (read)
        *read = position;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buffer_get_format(IDirectSoundCaptureBuffer8 *This, LPWAVEFORMATEX format,
                                                   DWORD allocated, LPDWORD written)
{
    CBUFFER *b = buffer_from(This);
    if (!format && !written)
        return DSERR_INVALIDPARAM;
    DWORD size = b->format->wFormatTag == WAVE_FORMAT_PCM ? sizeof(WAVEFORMATEX) : DsFormatSize(b->format);
    if (format) {
        if (allocated < size)
            return DSERR_INVALIDPARAM;
        memcpy(format, b->format, size);
    }
    if (written)
        *written = size;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buffer_get_status(IDirectSoundCaptureBuffer8 *This, LPDWORD status)
{
    CBUFFER *b = buffer_from(This);
    if (!status)
        return DSERR_INVALIDPARAM;
    RtlEnterCriticalSection(&b->lock);
    if (b->capturing)
        drain(b);
    *status = b->capturing ? DSCBSTATUS_CAPTURING | (b->looping ? DSCBSTATUS_LOOPING : 0) : 0;
    RtlLeaveCriticalSection(&b->lock);
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buffer_initialize(IDirectSoundCaptureBuffer8 *This, LPDIRECTSOUNDCAPTURE capture,
                                                   LPCDSCBUFFERDESC desc)
{
    return DSERR_ALREADYINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE buffer_lock(IDirectSoundCaptureBuffer8 *This, DWORD offset, DWORD bytes,
                                             LPVOID *audio1, LPDWORD bytes1, LPVOID *audio2, LPDWORD bytes2,
                                             DWORD flags)
{
    CBUFFER *b = buffer_from(This);
    if (!audio1 || !bytes1)
        return DSERR_INVALIDPARAM;
    if (flags & DSCBLOCK_ENTIREBUFFER)
        bytes = b->size;
    if (offset >= b->size || bytes > b->size)
        return DSERR_INVALIDPARAM;
    RtlEnterCriticalSection(&b->lock);
    if (b->capturing)
        drain(b);
    RtlLeaveCriticalSection(&b->lock);
    DWORD first = bytes < b->size - offset ? bytes : b->size - offset;
    *audio1 = b->data + offset;
    *bytes1 = first;
    if (audio2)
        *audio2 = bytes > first ? b->data : nullptr;
    if (bytes2)
        *bytes2 = audio2 ? bytes - first : 0;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buffer_start(IDirectSoundCaptureBuffer8 *This, DWORD flags)
{
    CBUFFER *b = buffer_from(This);
    RtlEnterCriticalSection(&b->lock);
    if (!b->capturing) {
        __atomic_store_n(&b->hdr->played, __atomic_load_n(&b->hdr->written, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
        if (!NT_SUCCESS(NtGdiMimeAudioControl(b->slot, MIME_AUDIO_START))) {
            RtlLeaveCriticalSection(&b->lock);
            return DSERR_NODRIVER;
        }
        b->capturing = TRUE;
    }
    b->looping = (flags & DSCBSTART_LOOPING) != 0;
    RtlLeaveCriticalSection(&b->lock);
    SetEvent(b->wake);
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buffer_stop(IDirectSoundCaptureBuffer8 *This)
{
    CBUFFER *b = buffer_from(This);
    RtlEnterCriticalSection(&b->lock);
    if (b->capturing) {
        drain(b);
        if (b->capturing) {
            b->capturing = FALSE;
            NtGdiMimeAudioControl(b->slot, MIME_AUDIO_STOP);
            signal_stop(b);
        }
    }
    RtlLeaveCriticalSection(&b->lock);
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buffer_unlock(IDirectSoundCaptureBuffer8 *This, LPVOID audio1, DWORD bytes1,
                                               LPVOID audio2, DWORD bytes2)
{
    return audio1 ? DS_OK : DSERR_INVALIDPARAM;
}

static HRESULT STDMETHODCALLTYPE buffer_get_object(IDirectSoundCaptureBuffer8 *This, REFGUID object, DWORD index,
                                                   REFGUID iid, LPVOID *out)
{
    if (!out)
        return DSERR_INVALIDPARAM;
    *out = nullptr;
    return buffer_from(This)->flags & DSCBCAPS_CTRLFX ? DSERR_OBJECTNOTFOUND : DSERR_CONTROLUNAVAIL;
}

static HRESULT STDMETHODCALLTYPE buffer_get_fx_status(IDirectSoundCaptureBuffer8 *This, DWORD count, LPDWORD results)
{
    return DSERR_INVALIDPARAM;
}

static const IDirectSoundCaptureBuffer8Vtbl buffer_vtbl = {
    buffer_query,      buffer_add_ref, buffer_release, buffer_get_caps, buffer_get_position, buffer_get_format,
    buffer_get_status, buffer_initialize, buffer_lock, buffer_start,    buffer_stop,         buffer_unlock,
    buffer_get_object, buffer_get_fx_status,
};

static HRESULT STDMETHODCALLTYPE notify_query(IDirectSoundNotify *This, REFIID riid, void **out)
{
    return buffer_query_any(buffer_from_notify(This), riid, out);
}

static ULONG STDMETHODCALLTYPE notify_add_ref(IDirectSoundNotify *This)
{
    return InterlockedIncrement(&buffer_from_notify(This)->ref);
}

static ULONG STDMETHODCALLTYPE notify_release(IDirectSoundNotify *This)
{
    return buffer_release_any(buffer_from_notify(This));
}

static HRESULT STDMETHODCALLTYPE notify_set(IDirectSoundNotify *This, DWORD count, LPCDSBPOSITIONNOTIFY notifies)
{
    CBUFFER *b = buffer_from_notify(This);
    if (count > DSBNOTIFICATIONS_MAX || (count && !notifies))
        return DSERR_INVALIDPARAM;
    NOTIFY *list = count ? HeapAlloc(heap(), 0, count * sizeof(*list)) : nullptr;
    if (count && !list)
        return DSERR_OUTOFMEMORY;
    DWORD align = b->format->nBlockAlign;
    for (DWORD i = 0; i < count; i++) {
        list[i].offset = notifies[i].dwOffset;
        list[i].event = notifies[i].hEventNotify;
        if (list[i].offset != DSBPN_OFFSETSTOP)
            list[i].offset -= list[i].offset % align;
        if (!list[i].event || (list[i].offset != DSBPN_OFFSETSTOP && list[i].offset >= b->size)) {
            HeapFree(heap(), 0, list);
            return DSERR_INVALIDPARAM;
        }
    }
    RtlEnterCriticalSection(&b->lock);
    if (b->capturing) {
        RtlLeaveCriticalSection(&b->lock);
        HeapFree(heap(), 0, list);
        return DSERR_INVALIDCALL;
    }
    HeapFree(heap(), 0, b->notifies);
    b->notifies = list;
    b->notify_count = count;
    RtlLeaveCriticalSection(&b->lock);
    return DS_OK;
}

static const IDirectSoundNotifyVtbl notify_vtbl = {notify_query, notify_add_ref, notify_release, notify_set};

/* the host records in the format asked for, so the buffer holds what the stream gives. */
static HRESULT buffer_create(const CAPTURE *c, const DSCBUFFERDESC *desc, CBUFFER **out)
{
    const WAVEFORMATEX *format = desc->lpwfxFormat;
    WORD tag = DsFormatTag(format);
    if (tag != WAVE_FORMAT_PCM && tag != WAVE_FORMAT_IEEE_FLOAT)
        return DSERR_BADFORMAT;
    if (!DsValidFormat(format) || !format->nBlockAlign || desc->dwBufferBytes < format->nBlockAlign)
        return DSERR_INVALIDPARAM;
    UINT32 host = 0;
    if (c->follows) {
        MimeAudioEndpoint e;
        if (!MimeAudioDefaultEndpoint(TRUE, &e))
            return DSERR_NODRIVER;
    } else if (!MimeAudioEndpointHost(TRUE, &c->guid, &host)) {
        return DSERR_NODRIVER;
    }
    CBUFFER *b = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b)
        return DSERR_OUTOFMEMORY;
    RtlInitializeCriticalSection(&b->lock);
    b->iface.lpVtbl = &buffer_vtbl;
    b->notify_iface.lpVtbl = &notify_vtbl;
    b->ref = 1;
    b->flags = desc->dwFlags;
    b->size = desc->dwBufferBytes - desc->dwBufferBytes % format->nBlockAlign;
    DWORD format_size = DsFormatSize(format);
    b->format = HeapAlloc(heap(), HEAP_ZERO_MEMORY, format_size < sizeof(WAVEFORMATEX) ? sizeof(WAVEFORMATEX) : format_size);
    b->data = HeapAlloc(heap(), 0, b->size);
    b->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!b->format || !b->data || !b->wake) {
        buffer_destroy(b);
        return DSERR_OUTOFMEMORY;
    }
    DsCopyFormat(b->format, format);
    memset(b->data, format->wBitsPerSample == 8 ? 0x80 : 0, b->size);
    b->narrow8 = format->wBitsPerSample == 8;
    UINT32 bits = b->narrow8 ? 16 : format->wBitsPerSample;
    PVOID base;
    NTSTATUS status = NtGdiMimeAudioCreate(&base, &b->slot);
    if (!NT_SUCCESS(status)) {
        buffer_destroy(b);
        return DSERR_ALLOCATED;
    }
    MimeAudioHeader *h = base;
    b->ring_frame = format->nChannels * bits / 8;
    b->ring_frames = (MIME_AUDIO_STREAM_SIZE - MIME_AUDIO_HEADER_SIZE) / b->ring_frame;
    h->rate = format->nSamplesPerSec;
    h->channels = format->nChannels;
    h->bits = bits;
    h->is_float = tag == WAVE_FORMAT_IEEE_FLOAT;
    h->frames = b->ring_frames;
    h->volume = 65536;
    h->capture = 1;
    h->device = host;
    status = NtGdiMimeAudioControl(b->slot, MIME_AUDIO_OPEN);
    if (!NT_SUCCESS(status)) {
        NtGdiMimeAudioControl(b->slot, MIME_AUDIO_CLOSE);
        buffer_destroy(b);
        return status == STATUS_NO_SUCH_DEVICE ? DSERR_NODRIVER : DSERR_BADFORMAT;
    }
    b->hdr = h;
    b->ring = (BYTE *)base + MIME_AUDIO_HEADER_SIZE;
    b->thread = CreateThread(nullptr, 0, capture_thread, b, 0, nullptr);
    if (!b->thread) {
        buffer_destroy(b);
        return DSERR_OUTOFMEMORY;
    }
    SetThreadPriority(b->thread, THREAD_PRIORITY_TIME_CRITICAL);
    *out = b;
    return DS_OK;
}

/* ------------------------------------------------------------------ the device */

static CAPTURE *capture_from(IDirectSoundCapture *iface)
{
    return CONTAINING_RECORD(iface, CAPTURE, iface);
}

static HRESULT STDMETHODCALLTYPE capture_query(IDirectSoundCapture *This, REFIID riid, void **out)
{
    if (!out)
        return DSERR_INVALIDPARAM;
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IDirectSoundCapture))
        return E_NOINTERFACE;
    InterlockedIncrement(&capture_from(This)->ref);
    *out = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE capture_add_ref(IDirectSoundCapture *This)
{
    return InterlockedIncrement(&capture_from(This)->ref);
}

static ULONG STDMETHODCALLTYPE capture_release(IDirectSoundCapture *This)
{
    CAPTURE *c = capture_from(This);
    LONG ref = InterlockedDecrement(&c->ref);
    if (!ref)
        HeapFree(heap(), 0, c);
    return ref;
}

static HRESULT STDMETHODCALLTYPE capture_create_buffer(IDirectSoundCapture *This, LPCDSCBUFFERDESC desc,
                                                       LPDIRECTSOUNDCAPTUREBUFFER *buffer, IUnknown *outer)
{
    CAPTURE *c = capture_from(This);
    if (!desc || !buffer)
        return DSERR_INVALIDPARAM;
    *buffer = nullptr;
    if (outer)
        return DSERR_NOAGGREGATION;
    if (!c->initialized)
        return DSERR_UNINITIALIZED;
    if ((desc->dwSize != sizeof(DSCBUFFERDESC) && desc->dwSize != sizeof(DSCBUFFERDESC1)) || !desc->dwBufferBytes ||
        !desc->lpwfxFormat)
        return DSERR_INVALIDPARAM;
    /* the effects of windows are the echo canceller and the noise filter of its own voice capture, which a device
     * without them refuses the same way. */
    if (desc->dwSize == sizeof(DSCBUFFERDESC) && ((desc->dwFlags & DSCBCAPS_CTRLFX) || desc->dwFXCount)) {
        if (!(desc->dwFlags & DSCBCAPS_CTRLFX) || !desc->dwFXCount || !desc->lpDSCFXDesc)
            return DSERR_INVALIDPARAM;
        return DSERR_FXUNAVAILABLE;
    }
    CBUFFER *b;
    HRESULT hr = buffer_create(c, desc, &b);
    if (SUCCEEDED(hr))
        *buffer = (IDirectSoundCaptureBuffer *)&b->iface;
    return hr;
}

static HRESULT STDMETHODCALLTYPE capture_get_caps(IDirectSoundCapture *This, LPDSCCAPS caps)
{
    CAPTURE *c = capture_from(This);
    if (!c->initialized)
        return DSERR_UNINITIALIZED;
    if (!caps || caps->dwSize != sizeof(DSCCAPS))
        return DSERR_INVALIDPARAM;
    caps->dwFlags = DSCCAPS_MULTIPLECAPTURE;
    /* every WAVE_FORMAT_ rate and width of winmm, since the engine converts. */
    caps->dwFormats = 0x000FFFFF;
    caps->dwChannels = 2;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE capture_initialize(IDirectSoundCapture *This, LPCGUID device)
{
    CAPTURE *c = capture_from(This);
    if (c->initialized)
        return DSERR_ALREADYINITIALIZED;
    if (!DsResolveDevice(device, &c->guid, TRUE, &c->follows))
        return DSERR_NODRIVER;
    c->initialized = TRUE;
    return DS_OK;
}

static const IDirectSoundCaptureVtbl capture_vtbl = {capture_query,         capture_add_ref,  capture_release,
                                                      capture_create_buffer, capture_get_caps, capture_initialize};

HRESULT DsCreateCapture(LPCGUID guid, IDirectSoundCapture **out, BOOL v8, BOOL initialize)
{
    *out = nullptr;
    CAPTURE *c = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c)
        return DSERR_OUTOFMEMORY;
    c->iface.lpVtbl = &capture_vtbl;
    c->ref = 1;
    c->v8 = v8;
    if (initialize) {
        HRESULT hr = capture_initialize(&c->iface, guid);
        if (FAILED(hr)) {
            HeapFree(heap(), 0, c);
            return hr;
        }
    }
    *out = &c->iface;
    return DS_OK;
}

/* ------------------------------------------------------------------ full duplex */

typedef struct DUPLEX {
    IDirectSoundFullDuplex iface;
    LONG ref;
    IDirectSound8 *render;
    IDirectSoundCapture *capture;
} DUPLEX;

static DUPLEX *duplex_from(IDirectSoundFullDuplex *iface)
{
    return CONTAINING_RECORD(iface, DUPLEX, iface);
}

/* the playback and the capture device answer for their own interfaces once the object is initialized. */
static HRESULT STDMETHODCALLTYPE duplex_query(IDirectSoundFullDuplex *This, REFIID riid, void **out)
{
    DUPLEX *d = duplex_from(This);
    if (!out)
        return DSERR_INVALIDPARAM;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirectSoundFullDuplex)) {
        InterlockedIncrement(&d->ref);
        *out = This;
        return S_OK;
    }
    if (d->render && (IsEqualGUID(riid, &IID_IDirectSound) || IsEqualGUID(riid, &IID_IDirectSound8)))
        return d->render->lpVtbl->QueryInterface(d->render, riid, out);
    if (d->capture && IsEqualGUID(riid, &IID_IDirectSoundCapture))
        return d->capture->lpVtbl->QueryInterface(d->capture, riid, out);
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE duplex_add_ref(IDirectSoundFullDuplex *This)
{
    return InterlockedIncrement(&duplex_from(This)->ref);
}

static void duplex_drop(DUPLEX *d)
{
    if (d->render)
        d->render->lpVtbl->Release(d->render);
    if (d->capture)
        d->capture->lpVtbl->Release(d->capture);
    d->render = nullptr;
    d->capture = nullptr;
}

static ULONG STDMETHODCALLTYPE duplex_release(IDirectSoundFullDuplex *This)
{
    DUPLEX *d = duplex_from(This);
    LONG ref = InterlockedDecrement(&d->ref);
    if (!ref) {
        duplex_drop(d);
        HeapFree(heap(), 0, d);
    }
    return ref;
}

static HRESULT STDMETHODCALLTYPE duplex_initialize(IDirectSoundFullDuplex *This, LPCGUID capture_device,
                                                   LPCGUID render_device, LPCDSCBUFFERDESC capture_desc,
                                                   LPCDSBUFFERDESC render_desc, HWND window, DWORD level,
                                                   LPDIRECTSOUNDCAPTUREBUFFER8 *capture_buffer,
                                                   LPDIRECTSOUNDBUFFER8 *render_buffer)
{
    DUPLEX *d = duplex_from(This);
    if (capture_buffer)
        *capture_buffer = nullptr;
    if (render_buffer)
        *render_buffer = nullptr;
    if (!capture_desc || !render_desc || !capture_buffer || !render_buffer)
        return DSERR_INVALIDPARAM;
    if (d->render || d->capture)
        return DSERR_ALREADYINITIALIZED;
    IDirectSoundCaptureBuffer *recorded = nullptr;
    IDirectSoundBuffer *played = nullptr;
    HRESULT hr = DirectSoundCreate8(render_device, &d->render, nullptr);
    if (SUCCEEDED(hr))
        hr = d->render->lpVtbl->SetCooperativeLevel(d->render, window, level);
    if (SUCCEEDED(hr))
        hr = d->render->lpVtbl->CreateSoundBuffer(d->render, render_desc, &played, nullptr);
    if (SUCCEEDED(hr))
        hr = DsCreateCapture(capture_device, &d->capture, TRUE, TRUE);
    if (SUCCEEDED(hr))
        hr = d->capture->lpVtbl->CreateCaptureBuffer(d->capture, capture_desc, &recorded, nullptr);
    if (SUCCEEDED(hr))
        hr = played->lpVtbl->QueryInterface(played, &IID_IDirectSoundBuffer8, (void **)render_buffer);
    if (SUCCEEDED(hr))
        *capture_buffer = (IDirectSoundCaptureBuffer8 *)recorded;
    else if (recorded)
        recorded->lpVtbl->Release(recorded);
    if (played)
        played->lpVtbl->Release(played);
    if (FAILED(hr))
        duplex_drop(d);
    return hr;
}

static const IDirectSoundFullDuplexVtbl duplex_vtbl = {duplex_query, duplex_add_ref, duplex_release,
                                                        duplex_initialize};

HRESULT DsCreateDuplex(IDirectSoundFullDuplex **out)
{
    *out = nullptr;
    DUPLEX *d = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return DSERR_OUTOFMEMORY;
    d->iface.lpVtbl = &duplex_vtbl;
    d->ref = 1;
    *out = &d->iface;
    return DS_OK;
}
