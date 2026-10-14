/*
 * audio clients of the render endpoint. a client owns one host stream
 * (mime/audio.h): GetBuffer hands out the ring in place and ReleaseBuffer
 * publishes the frames by advancing written, while the host's output
 * callback advances played. a request that would wrap around the end of the
 * ring goes through a bounce buffer instead. event driven clients get their
 * event set once per engine period while they run.
 */
#include "mmdevapi_private.h"

#include <mime/audio.h>

#define PERIOD (REFERENCE_TIME)100000      /* 10 ms, in 100 ns units */
#define MIN_BUFFER (REFERENCE_TIME)400000  /* 40 ms, room for a guest busy elsewhere */
#define MAX_BUFFER (REFERENCE_TIME)20000000

typedef struct CLIENT {
    const IAudioClient3Vtbl *client;
    const IAudioRenderClientVtbl *render;
    const IAudioClockVtbl *clock;
    const ISimpleAudioVolumeVtbl *simple;
    const IAudioStreamVolumeVtbl *stream;
    LONG refs;
    SRWLOCK lock;
    BOOL initialized, started;
    DWORD flags;
    WAVEFORMATEXTENSIBLE format;
    UINT32 rate, channels, frame_bytes, buffer_frames, ring_frames;
    ULONG slot;
    MimeAudioHeader *hdr;
    BYTE *ring;
    HANDLE event, thread;
    volatile LONG quit;
    UINT32 pending; /* frames handed out by GetBuffer */
    BYTE *scratch;
    BOOL bounced;
    float master, channel[8];
    BOOL mute;
} CLIENT;

#define FROM(field, This) ((CLIENT *)((BYTE *)(This) - offsetof(CLIENT, field)))

static HANDLE heap(void) { return GetProcessHeap(); }

/* ------------------------------------------------------------------ formats */

typedef struct {
    UINT32 rate, channels, bits;
    BOOL is_float;
} FORMAT;

static BOOL parse_format(const WAVEFORMATEX *f, FORMAT *out)
{
    if (!f)
        return FALSE;
    BOOL is_float;
    if (f->wFormatTag == WAVE_FORMAT_PCM) {
        is_float = FALSE;
    } else if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        is_float = TRUE;
    } else if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const GUID *sub = &((const WAVEFORMATEXTENSIBLE *)f)->SubFormat;
        if (IsEqualGUID(sub, &KSDATAFORMAT_SUBTYPE_PCM))
            is_float = FALSE;
        else if (IsEqualGUID(sub, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
            is_float = TRUE;
        else
            return FALSE;
    } else {
        return FALSE;
    }
    UINT32 bits = f->wBitsPerSample;
    BOOL bits_ok = is_float ? bits == 32 : bits == 16 || bits == 24 || bits == 32;
    if (!bits_ok || f->nChannels < 1 || f->nChannels > 8 || f->nSamplesPerSec < 8000 || f->nSamplesPerSec > 192000 ||
        f->nBlockAlign != f->nChannels * bits / 8)
        return FALSE;
    out->rate = f->nSamplesPerSec;
    out->channels = f->nChannels;
    out->bits = bits;
    out->is_float = is_float;
    return TRUE;
}

static HRESULT alloc_mix_format(WAVEFORMATEX **format)
{
    WAVEFORMATEXTENSIBLE *f = MmTaskAlloc(sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    MmMixFormat(f);
    *format = &f->Format;
    return S_OK;
}

/* ------------------------------------------------------------------ the stream */

static UINT32 padding(CLIENT *c)
{
    UINT64 queued = c->hdr->written - __atomic_load_n(&c->hdr->played, __ATOMIC_ACQUIRE);
    return queued > c->buffer_frames ? c->buffer_frames : (UINT32)queued;
}

static void apply_volume(CLIENT *c)
{
    float level = 0;
    for (UINT32 i = 0; i < c->channels; i++)
        level = c->channel[i] > level ? c->channel[i] : level;
    level = c->mute ? 0 : level * c->master;
    __atomic_store_n(&c->hdr->volume, (UINT32)(level * 65536.0f + 0.5f), __ATOMIC_RELAXED);
}

static DWORD WINAPI event_thread(void *arg)
{
    CLIENT *c = arg;
    while (!c->quit) {
        Sleep((DWORD)(PERIOD / 10000));
        if (c->started && c->event)
            SetEvent(c->event);
    }
    return 0;
}

static HRESULT initialize(CLIENT *c, AUDCLNT_SHAREMODE mode, DWORD flags, REFERENCE_TIME duration,
                          const WAVEFORMATEX *format)
{
    FORMAT f;
    if (!format)
        return E_POINTER;
    if (mode != AUDCLNT_SHAREMODE_SHARED && mode != AUDCLNT_SHAREMODE_EXCLUSIVE)
        return E_INVALIDARG;
    if (flags & AUDCLNT_STREAMFLAGS_LOOPBACK)
        return AUDCLNT_E_WRONG_ENDPOINT_TYPE;
    if (!parse_format(format, &f))
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    AcquireSRWLockExclusive(&c->lock);
    if (c->initialized) {
        ReleaseSRWLockExclusive(&c->lock);
        return AUDCLNT_E_ALREADY_INITIALIZED;
    }
    UINT32 frame_bytes = f.channels * f.bits / 8;
    UINT32 ring_frames = (MIME_AUDIO_STREAM_SIZE - MIME_AUDIO_HEADER_SIZE) / frame_bytes;
    if (duration < MIN_BUFFER)
        duration = MIN_BUFFER;
    if (duration > MAX_BUFFER)
        duration = MAX_BUFFER;
    UINT64 frames = (UINT64)duration * f.rate / 10000000;
    if (frames > ring_frames)
        frames = ring_frames;

    PVOID base;
    ULONG slot;
    NTSTATUS status = NtGdiMimeAudioCreate(&base, &slot);
    if (NT_SUCCESS(status)) {
        MimeAudioHeader *h = base;
        h->rate = f.rate;
        h->channels = f.channels;
        h->bits = f.bits;
        h->is_float = f.is_float;
        h->frames = ring_frames;
        h->volume = 65536;
        status = NtGdiMimeAudioControl(slot, MIME_AUDIO_OPEN);
        if (!NT_SUCCESS(status))
            NtGdiMimeAudioControl(slot, MIME_AUDIO_CLOSE);
        else {
            c->hdr = h;
            c->ring = (BYTE *)base + MIME_AUDIO_HEADER_SIZE;
        }
    }
    if (!NT_SUCCESS(status)) {
        ReleaseSRWLockExclusive(&c->lock);
        return status == STATUS_NOT_SUPPORTED ? AUDCLNT_E_UNSUPPORTED_FORMAT : AUDCLNT_E_SERVICE_NOT_RUNNING;
    }
    c->slot = slot;
    c->flags = flags;
    c->rate = f.rate;
    c->channels = f.channels;
    c->frame_bytes = frame_bytes;
    c->ring_frames = ring_frames;
    c->buffer_frames = (UINT32)frames;
    memcpy(&c->format, format, format->wFormatTag == WAVE_FORMAT_EXTENSIBLE ? sizeof(WAVEFORMATEXTENSIBLE) : sizeof(WAVEFORMATEX));
    c->initialized = TRUE;
    ReleaseSRWLockExclusive(&c->lock);
    return S_OK;
}

/* ------------------------------------------------------------------ IAudioClient3 */

static HRESULT STDMETHODCALLTYPE client_query(void *This, REFIID riid, void **object);

static ULONG STDMETHODCALLTYPE client_add_ref(void *This)
{
    return (ULONG)InterlockedIncrement(&FROM(client, This)->refs);
}

static ULONG STDMETHODCALLTYPE client_release(void *This)
{
    CLIENT *c = FROM(client, This);
    LONG refs = InterlockedDecrement(&c->refs);
    if (refs)
        return (ULONG)refs;
    if (c->thread) {
        c->quit = 1;
        WaitForSingleObject(c->thread, INFINITE);
        CloseHandle(c->thread);
    }
    if (c->initialized)
        NtGdiMimeAudioControl(c->slot, MIME_AUDIO_CLOSE);
    if (c->scratch)
        HeapFree(heap(), 0, c->scratch);
    HeapFree(heap(), 0, c);
    return 0;
}

static HRESULT STDMETHODCALLTYPE client_initialize(void *This, AUDCLNT_SHAREMODE mode, DWORD flags,
                                                   REFERENCE_TIME duration, REFERENCE_TIME period,
                                                   const WAVEFORMATEX *format, LPCGUID session)
{
    return initialize(FROM(client, This), mode, flags, duration, format);
}

static HRESULT STDMETHODCALLTYPE client_buffer_size(void *This, UINT32 *frames)
{
    CLIENT *c = FROM(client, This);
    if (!frames)
        return E_POINTER;
    if (!c->initialized)
        return AUDCLNT_E_NOT_INITIALIZED;
    *frames = c->buffer_frames;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE client_latency(void *This, REFERENCE_TIME *latency)
{
    CLIENT *c = FROM(client, This);
    if (!latency)
        return E_POINTER;
    if (!c->initialized)
        return AUDCLNT_E_NOT_INITIALIZED;
    *latency = PERIOD;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE client_padding(void *This, UINT32 *frames)
{
    CLIENT *c = FROM(client, This);
    if (!frames)
        return E_POINTER;
    if (!c->initialized)
        return AUDCLNT_E_NOT_INITIALIZED;
    *frames = padding(c);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE client_is_supported(void *This, AUDCLNT_SHAREMODE mode, const WAVEFORMATEX *format,
                                                     WAVEFORMATEX **closest)
{
    FORMAT f;
    if (closest)
        *closest = NULL;
    if (!format || (mode == AUDCLNT_SHAREMODE_SHARED && !closest))
        return E_POINTER;
    if (mode != AUDCLNT_SHAREMODE_SHARED && mode != AUDCLNT_SHAREMODE_EXCLUSIVE)
        return E_INVALIDARG;
    /* core audio converts any of the formats the host accepts. */
    if (parse_format(format, &f))
        return S_OK;
    if (mode == AUDCLNT_SHAREMODE_EXCLUSIVE)
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    HRESULT hr = alloc_mix_format(closest);
    return FAILED(hr) ? hr : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE client_mix_format(void *This, WAVEFORMATEX **format)
{
    if (!format)
        return E_POINTER;
    return alloc_mix_format(format);
}

static HRESULT STDMETHODCALLTYPE client_period(void *This, REFERENCE_TIME *default_period, REFERENCE_TIME *minimum)
{
    if (!default_period && !minimum)
        return E_POINTER;
    if (default_period)
        *default_period = PERIOD;
    if (minimum)
        *minimum = PERIOD;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE client_start(void *This)
{
    CLIENT *c = FROM(client, This);
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&c->lock);
    if (!c->initialized)
        hr = AUDCLNT_E_NOT_INITIALIZED;
    else if (c->started)
        hr = AUDCLNT_E_NOT_STOPPED;
    else if ((c->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) && !c->event)
        hr = AUDCLNT_E_EVENTHANDLE_NOT_SET;
    else if (!NT_SUCCESS(NtGdiMimeAudioControl(c->slot, MIME_AUDIO_START)))
        hr = AUDCLNT_E_DEVICE_INVALIDATED;
    if (SUCCEEDED(hr)) {
        c->started = TRUE;
        if (c->event && !c->thread)
            c->thread = CreateThread(NULL, 0, event_thread, c, 0, NULL);
        if (c->event)
            SetEvent(c->event);
    }
    ReleaseSRWLockExclusive(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE client_stop(void *This)
{
    CLIENT *c = FROM(client, This);
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&c->lock);
    if (!c->initialized)
        hr = AUDCLNT_E_NOT_INITIALIZED;
    else if (!c->started)
        hr = S_FALSE;
    else {
        NtGdiMimeAudioControl(c->slot, MIME_AUDIO_STOP);
        c->started = FALSE;
    }
    ReleaseSRWLockExclusive(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE client_reset(void *This)
{
    CLIENT *c = FROM(client, This);
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&c->lock);
    if (!c->initialized)
        hr = AUDCLNT_E_NOT_INITIALIZED;
    else if (c->started)
        hr = AUDCLNT_E_NOT_STOPPED;
    else if (c->pending)
        hr = ((HRESULT)0x8889000B); /* AUDCLNT_E_BUFFER_OPERATION_PENDING */
    else
        __atomic_store_n(&c->hdr->written, __atomic_load_n(&c->hdr->played, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
    ReleaseSRWLockExclusive(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE client_set_event(void *This, HANDLE event)
{
    CLIENT *c = FROM(client, This);
    if (!event)
        return E_INVALIDARG;
    if (!c->initialized)
        return AUDCLNT_E_NOT_INITIALIZED;
    if (!(c->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK))
        return AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED;
    c->event = event;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE client_get_service(void *This, REFIID iid, void **object)
{
    CLIENT *c = FROM(client, This);
    if (!iid || !object)
        return E_POINTER;
    *object = NULL;
    if (!c->initialized)
        return AUDCLNT_E_NOT_INITIALIZED;
    if (IsEqualGUID(iid, &IID_IAudioRenderClient))
        *object = &c->render;
    else if (IsEqualGUID(iid, &IID_IAudioClock))
        *object = &c->clock;
    else if (IsEqualGUID(iid, &IID_ISimpleAudioVolume))
        *object = &c->simple;
    else if (IsEqualGUID(iid, &IID_IAudioStreamVolume))
        *object = &c->stream;
    else
        return E_NOINTERFACE;
    client_add_ref(This);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE client_offload(void *This, int category, BOOL *capable)
{
    if (!capable)
        return E_POINTER;
    *capable = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE client_properties(void *This, const void *properties)
{
    if (!properties)
        return E_POINTER;
    return FROM(client, This)->initialized ? AUDCLNT_E_ALREADY_INITIALIZED : S_OK;
}

static HRESULT STDMETHODCALLTYPE client_size_limits(void *This, const WAVEFORMATEX *format, BOOL event_driven,
                                                    REFERENCE_TIME *minimum, REFERENCE_TIME *maximum)
{
    FORMAT f;
    if (!format || !minimum || !maximum)
        return E_POINTER;
    if (!parse_format(format, &f))
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    *minimum = MIN_BUFFER;
    *maximum = MAX_BUFFER;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE client_engine_period(void *This, const WAVEFORMATEX *format, UINT32 *default_period,
                                                      UINT32 *fundamental, UINT32 *minimum, UINT32 *maximum)
{
    FORMAT f;
    if (!format || !default_period || !fundamental || !minimum || !maximum)
        return E_POINTER;
    if (!parse_format(format, &f))
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    *default_period = *fundamental = *minimum = *maximum = (UINT32)(f.rate * PERIOD / 10000000);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE client_current_period(void *This, WAVEFORMATEX **format, UINT32 *period)
{
    if (!format || !period)
        return E_POINTER;
    HRESULT hr = alloc_mix_format(format);
    if (SUCCEEDED(hr))
        *period = (UINT32)((*format)->nSamplesPerSec * PERIOD / 10000000);
    return hr;
}

static HRESULT STDMETHODCALLTYPE client_init_shared(void *This, DWORD flags, UINT32 period, const WAVEFORMATEX *format,
                                                    LPCGUID session)
{
    return initialize(FROM(client, This), AUDCLNT_SHAREMODE_SHARED, flags, 0, format);
}

static const IAudioClient3Vtbl client_vtbl = {
    client_query,          client_add_ref,       client_release,       client_initialize,  client_buffer_size,
    client_latency,        client_padding,       client_is_supported,  client_mix_format,  client_period,
    client_start,          client_stop,          client_reset,         client_set_event,   client_get_service,
    client_offload,        client_properties,    client_size_limits,   client_engine_period,
    client_current_period, client_init_shared,
};

/* ------------------------------------------------------------------ IAudioRenderClient */

static HRESULT STDMETHODCALLTYPE render_query(void *This, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IAudioRenderClient)) {
        *object = This;
        client_add_ref(&FROM(render, This)->client);
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE render_add_ref(void *This) { return client_add_ref(&FROM(render, This)->client); }
static ULONG STDMETHODCALLTYPE render_release(void *This) { return client_release(&FROM(render, This)->client); }

/* copies or clears frames at a ring position, wrapping at its end. */
static void ring_write(CLIENT *c, UINT64 at, const BYTE *src, UINT32 frames)
{
    UINT32 start = (UINT32)(at % c->ring_frames);
    UINT32 first = c->ring_frames - start < frames ? c->ring_frames - start : frames;
    BYTE *dst = c->ring + (SIZE_T)start * c->frame_bytes;
    SIZE_T a = (SIZE_T)first * c->frame_bytes, b = (SIZE_T)(frames - first) * c->frame_bytes;
    if (src) {
        memcpy(dst, src, a);
        memcpy(c->ring, src + a, b);
    } else {
        memset(dst, 0, a);
        memset(c->ring, 0, b);
    }
}

static HRESULT STDMETHODCALLTYPE render_get_buffer(void *This, UINT32 frames, BYTE **data)
{
    CLIENT *c = FROM(render, This);
    if (!data)
        return E_POINTER;
    *data = NULL;
    if (c->pending)
        return AUDCLNT_E_OUT_OF_ORDER;
    if (!frames)
        return S_OK;
    if (frames > c->buffer_frames - padding(c))
        return AUDCLNT_E_BUFFER_TOO_LARGE;
    UINT32 start = (UINT32)(c->hdr->written % c->ring_frames);
    c->bounced = start + frames > c->ring_frames;
    if (c->bounced) {
        if (!c->scratch)
            c->scratch = HeapAlloc(heap(), 0, (SIZE_T)c->buffer_frames * c->frame_bytes);
        if (!c->scratch)
            return E_OUTOFMEMORY;
        *data = c->scratch;
    } else {
        *data = c->ring + (SIZE_T)start * c->frame_bytes;
    }
    c->pending = frames;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE render_release_buffer(void *This, UINT32 frames, DWORD flags)
{
    CLIENT *c = FROM(render, This);
    if (frames > c->pending)
        return AUDCLNT_E_INVALID_SIZE;
    if (!c->pending)
        return frames ? AUDCLNT_E_OUT_OF_ORDER : S_OK;
    UINT64 written = c->hdr->written;
    if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
        ring_write(c, written, NULL, frames);
    else if (c->bounced)
        ring_write(c, written, c->scratch, frames);
    c->pending = 0;
    __atomic_store_n(&c->hdr->written, written + frames, __ATOMIC_RELEASE);
    return S_OK;
}

static const IAudioRenderClientVtbl render_vtbl = {
    render_query, render_add_ref, render_release, render_get_buffer, render_release_buffer,
};

/* ------------------------------------------------------------------ IAudioClock */

static HRESULT STDMETHODCALLTYPE clock_query(void *This, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IAudioClock)) {
        *object = This;
        client_add_ref(&FROM(clock, This)->client);
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE clock_add_ref(void *This) { return client_add_ref(&FROM(clock, This)->client); }
static ULONG STDMETHODCALLTYPE clock_release(void *This) { return client_release(&FROM(clock, This)->client); }

/* positions count frames, so the frequency is the frame rate. */
static HRESULT STDMETHODCALLTYPE clock_frequency(void *This, UINT64 *frequency)
{
    if (!frequency)
        return E_POINTER;
    *frequency = FROM(clock, This)->rate;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE clock_position(void *This, UINT64 *position, UINT64 *qpc)
{
    CLIENT *c = FROM(clock, This);
    if (!position)
        return E_POINTER;
    *position = __atomic_load_n(&c->hdr->played, __ATOMIC_ACQUIRE);
    if (qpc) {
        LARGE_INTEGER now, frequency;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&frequency);
        *qpc = (UINT64)((double)now.QuadPart * 10000000.0 / (double)frequency.QuadPart);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE clock_characteristics(void *This, DWORD *characteristics)
{
    if (!characteristics)
        return E_POINTER;
    *characteristics = 0;
    return S_OK;
}

static const IAudioClockVtbl clock_vtbl = {
    clock_query, clock_add_ref, clock_release, clock_frequency, clock_position, clock_characteristics,
};

/* ------------------------------------------------------------------ volumes */

static HRESULT STDMETHODCALLTYPE simple_query(void *This, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ISimpleAudioVolume)) {
        *object = This;
        client_add_ref(&FROM(simple, This)->client);
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE simple_add_ref(void *This) { return client_add_ref(&FROM(simple, This)->client); }
static ULONG STDMETHODCALLTYPE simple_release(void *This) { return client_release(&FROM(simple, This)->client); }

static HRESULT STDMETHODCALLTYPE simple_set_master(void *This, float level, LPCGUID context)
{
    CLIENT *c = FROM(simple, This);
    if (!(level >= 0.0f && level <= 1.0f))
        return E_INVALIDARG;
    c->master = level;
    apply_volume(c);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE simple_get_master(void *This, float *level)
{
    if (!level)
        return E_POINTER;
    *level = FROM(simple, This)->master;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE simple_set_mute(void *This, BOOL mute, LPCGUID context)
{
    CLIENT *c = FROM(simple, This);
    c->mute = mute != FALSE;
    apply_volume(c);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE simple_get_mute(void *This, BOOL *mute)
{
    if (!mute)
        return E_POINTER;
    *mute = FROM(simple, This)->mute;
    return S_OK;
}

static const ISimpleAudioVolumeVtbl simple_vtbl = {
    simple_query, simple_add_ref, simple_release, simple_set_master, simple_get_master, simple_set_mute, simple_get_mute,
};

static HRESULT STDMETHODCALLTYPE stream_query(void *This, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IAudioStreamVolume)) {
        *object = This;
        client_add_ref(&FROM(stream, This)->client);
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE stream_add_ref(void *This) { return client_add_ref(&FROM(stream, This)->client); }
static ULONG STDMETHODCALLTYPE stream_release(void *This) { return client_release(&FROM(stream, This)->client); }

static HRESULT STDMETHODCALLTYPE stream_count(void *This, UINT32 *count)
{
    if (!count)
        return E_POINTER;
    *count = FROM(stream, This)->channels;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_set(void *This, UINT32 index, float level)
{
    CLIENT *c = FROM(stream, This);
    if (index >= c->channels || !(level >= 0.0f && level <= 1.0f))
        return E_INVALIDARG;
    c->channel[index] = level;
    apply_volume(c);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_get(void *This, UINT32 index, float *level)
{
    CLIENT *c = FROM(stream, This);
    if (!level)
        return E_POINTER;
    if (index >= c->channels)
        return E_INVALIDARG;
    *level = c->channel[index];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_set_all(void *This, UINT32 count, const float *levels)
{
    CLIENT *c = FROM(stream, This);
    if (!levels)
        return E_POINTER;
    if (count != c->channels)
        return E_INVALIDARG;
    for (UINT32 i = 0; i < count; i++)
        if (!(levels[i] >= 0.0f && levels[i] <= 1.0f))
            return E_INVALIDARG;
    memcpy(c->channel, levels, count * sizeof(float));
    apply_volume(c);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_get_all(void *This, UINT32 count, float *levels)
{
    CLIENT *c = FROM(stream, This);
    if (!levels)
        return E_POINTER;
    if (count != c->channels)
        return E_INVALIDARG;
    memcpy(levels, c->channel, count * sizeof(float));
    return S_OK;
}

static const IAudioStreamVolumeVtbl stream_vtbl = {
    stream_query, stream_add_ref, stream_release, stream_count, stream_set, stream_get, stream_set_all, stream_get_all,
};

/* ------------------------------------------------------------------ creation */

static HRESULT STDMETHODCALLTYPE client_query(void *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IAudioClient) ||
        IsEqualGUID(riid, &IID_IAudioClient2) || IsEqualGUID(riid, &IID_IAudioClient3)) {
        *object = This;
        client_add_ref(This);
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

HRESULT MmCreateAudioClient(void **object)
{
    CLIENT *c = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(CLIENT));
    if (!c)
        return E_OUTOFMEMORY;
    c->client = &client_vtbl;
    c->render = &render_vtbl;
    c->clock = &clock_vtbl;
    c->simple = &simple_vtbl;
    c->stream = &stream_vtbl;
    c->refs = 1;
    InitializeSRWLock(&c->lock);
    c->master = 1.0f;
    for (int i = 0; i < 8; i++)
        c->channel[i] = 1.0f;
    *object = &c->client;
    return S_OK;
}
