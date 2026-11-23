#include "evr_private.h"

#define MAX_STREAMS 16
#define MAX_SAMPLES 32

typedef struct {
    DWORD id;
    DWORD index;
    IMFAttributes *attributes;
    IUnknown *type;
    const MFVIDEOFORMAT *format;
    IMFSample *current, *next;
    MFVideoNormalizedRect rect;
    BOOL enabled;
    BOOL used; /* the substream's picture went into an output and a new one was asked for. */
    DWORD requests;
} MixerStream;

typedef struct {
    IUnknown inner;
    IMFTransform transform;
    IMFVideoDeviceID device_id;
    IMFTopologyServiceLookupClient lookup;
    IMFGetService get_service;
    IMFVideoMixerControl2 control;
    IMFVideoPositionMapper mapper;
    IMFVideoProcessor processor;
    IMFVideoMixerBitmap bitmap;
    IMFQualityAdvise quality;
    IMFClockStateSink clock;
    IEVRTrustedVideoPlugin trusted;
    IDirect3DDeviceManager9 manager_iface;
    IMFAttributes attributes;
    IUnknown *outer;
    LONG refs;
    CRITICAL_SECTION lock;

    IMFAttributes *store, *transform_store;
    MixerStream *streams[MAX_STREAMS];
    DWORD count, max_streams;
    IMediaEventSink *sink;

    IDirect3DDeviceManager9 *manager;
    HANDLE handle, lock_handle;
    IDirect3DDevice9 *locked_device;
    D3DCAPS9 device_caps;
    IDirectXVideoProcessorService *service;
    IDirectXVideoProcessor *vp;

    DWORD state;
    BOOL streaming, draining;
    IUnknown *output_type;
    const MFVIDEOFORMAT *output_format;
    MFVIDEOFORMAT presentation;
    D3DFORMAT *targets;
    UINT target_count;
    GUID *modes;
    UINT mode_count;
    D3DFORMAT *substream_formats;
    UINT substream_format_count;
    DXVA2_VideoProcessorCaps caps;
    GUID mode, device;
    BOOL mode_set;
    DXVA2_ValueRange procamp_ranges[4], filter_ranges[12];
    DXVA2_Fixed32 procamp[4], filters[12];
    COLORREF background;
    DXVA2_AYUVSample16 background_yuv;
    DWORD constriction;
    DWORD prefs;
    MF_QUALITY_DROP_MODE drop_mode;
    MF_QUALITY_LEVEL quality_level;
    LONGLONG drop_time, lower_bound, upper_bound;
    DWORD drop_samples;
    float rate;

    BOOL consumed; /* the reference picture went out, and stays for the next picture or a repaint. */

    UINT32 *app_image;
    RECT app_src;
    DWORD app_width, app_height, app_kind;
    MFVideoNormalizedRect app_dest;
    float app_alpha;
    COLORREF app_key;
    DWORD app_filter;
} Mixer;

#define IMPL(iface) CONTAINING_RECORD(This, Mixer, iface)

static HRESULT verify_device(Mixer *m);
static void free_resources(Mixer *m);
static void update_state(Mixer *m);

/* ------------------------------------------------------------------ streams */

static MixerStream *find_stream(Mixer *m, DWORD id, DWORD *position)
{
    for (DWORD i = 0; i < m->count; i++)
        if (m->streams[i]->id == id) {
            if (position)
                *position = i;
            return m->streams[i];
        }
    return nullptr;
}

static MixerStream *reference(Mixer *m)
{
    return find_stream(m, 0, nullptr);
}

static void stream_free(MixerStream *s)
{
    if (s->type)
        s->type->lpVtbl->Release(s->type);
    if (s->attributes)
        s->attributes->lpVtbl->Release(s->attributes);
    if (s->current)
        s->current->lpVtbl->Release(s->current);
    if (s->next)
        s->next->lpVtbl->Release(s->next);
    HeapFree(GetProcessHeap(), 0, s);
}

static HRESULT add_stream(Mixer *m, DWORD id, BOOL enabled)
{
    MixerStream *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->id = id;
    s->rect.right = s->rect.bottom = 1.0f;
    HRESULT hr = MFCreateAttributes(&s->attributes, 1);
    if (SUCCEEDED(hr))
        hr = s->attributes->lpVtbl->SetUINT32(s->attributes, &MF_SA_REQUIRED_SAMPLE_COUNT, 1);
    if (FAILED(hr)) {
        stream_free(s);
        return hr;
    }
    s->index = m->count;
    s->enabled = enabled;
    m->streams[m->count++] = s;
    return S_OK;
}

static void remove_at(Mixer *m, DWORD position)
{
    memmove(&m->streams[position], &m->streams[position + 1], (m->count - position - 1) * sizeof(*m->streams));
    m->count--;
}

/* a stream with a sample counts as enabled. */
static BOOL stream_enabled(MixerStream *s)
{
    return s->current || s->next ? TRUE : s->enabled;
}

static HRESULT request_sample(Mixer *m, MixerStream *s)
{
    s->requests++;
    if (!m->sink)
        return MF_E_NOT_AVAILABLE;
    return m->sink->lpVtbl->Notify(m->sink, EC_SAMPLE_NEEDED, s->id, 0);
}

static void set_stream_type(MixerStream *s, IUnknown *type)
{
    if (s->type)
        s->type->lpVtbl->Release(s->type);
    s->type = type;
    s->format = nullptr;
    if (type) {
        type->lpVtbl->AddRef(type);
        s->format = EvrVideoFormat(type);
    }
}

static void clear_output_type(Mixer *m)
{
    if (m->output_type) {
        m->output_type->lpVtbl->Release(m->output_type);
        m->output_type = nullptr;
    }
    m->output_format = nullptr;
}

static void internal_flush(Mixer *m)
{
    m->consumed = FALSE;
    for (DWORD i = 0; i < m->count; i++) {
        MixerStream *s = m->streams[i];
        if (s->current)
            s->current->lpVtbl->Release(s->current);
        if (s->next)
            s->next->lpVtbl->Release(s->next);
        s->current = s->next = nullptr;
        s->used = FALSE;
    }
}

/* ------------------------------------------------------------------ the presentation format */

/* evr's InitializePresentationMediaType: the picture output types are made from, as large as every stream. */
static HRESULT init_presentation(Mixer *m)
{
    MixerStream *ref = reference(m);
    if (!ref || !ref->format)
        return S_OK;
    const MFVIDEOFORMAT *in = ref->format;
    RECT rc = {0};
    for (DWORD i = 0; i < m->count; i++) {
        if (!m->streams[i]->format)
            continue;
        RECT r;
        EvrReferenceSourceRect(m->streams[i]->format, in, &r);
        if (rc.right <= r.right - r.left)
            rc.right = r.right - r.left;
        if (rc.bottom <= r.bottom - r.top)
            rc.bottom = r.bottom - r.top;
    }
    if (rc.right <= 0 || rc.bottom <= 0)
        return MF_E_INVALIDMEDIATYPE;
    if (m->state >= 2 && m->output_type &&
        m->presentation.videoInfo.PixelAspectRatio.Numerator == in->videoInfo.PixelAspectRatio.Numerator &&
        m->presentation.videoInfo.PixelAspectRatio.Denominator == in->videoInfo.PixelAspectRatio.Denominator &&
        m->presentation.videoInfo.FramesPerSecond.Numerator == in->videoInfo.FramesPerSecond.Numerator &&
        m->presentation.videoInfo.FramesPerSecond.Denominator == in->videoInfo.FramesPerSecond.Denominator &&
        (LONG)m->presentation.videoInfo.dwWidth == rc.right && (LONG)m->presentation.videoInfo.dwHeight == rc.bottom)
        return S_OK;
    HRESULT hr = MFInitVideoFormat_RGB(&m->presentation, rc.right, rc.bottom, 0);
    if (FAILED(hr))
        return hr;
    MFVIDEOINFO *out = &m->presentation.videoInfo;
    if (EvrIsFormatYUV(in->surfaceInfo.Format)) {
        out->TransferMatrix = 1;
        out->TransferFunction = 5;
        out->ColorPrimaries = 2;
        out->NominalRange = 2;
        out->SourceLighting = 3;
    } else {
        out->TransferMatrix = in->videoInfo.TransferMatrix ? in->videoInfo.TransferMatrix : 1;
        out->TransferFunction = in->videoInfo.TransferFunction ? in->videoInfo.TransferFunction : 7;
        out->ColorPrimaries = in->videoInfo.ColorPrimaries ? in->videoInfo.ColorPrimaries : 2;
        out->NominalRange = in->videoInfo.NominalRange ? in->videoInfo.NominalRange : 1;
        out->SourceLighting = in->videoInfo.SourceLighting ? in->videoInfo.SourceLighting : 2;
    }
    out->PixelAspectRatio = in->videoInfo.PixelAspectRatio;
    out->FramesPerSecond = in->videoInfo.FramesPerSecond;
    if (in->videoInfo.InterlaceMode - 3 <= 1)
        out->FramesPerSecond.Numerator *= 2;
    clear_output_type(m);
    return S_OK;
}

/* evr's SetInputTypePrivate. */
static HRESULT set_input_type_private(Mixer *m, DWORD id, IUnknown *type)
{
    MixerStream *s = find_stream(m, id, nullptr);
    HRESULT hr = MF_E_INVALIDSTREAMNUMBER;
    if (s) {
        if (type && !EvrVideoFormat(type)) {
            hr = E_INVALIDARG;
        } else {
            set_stream_type(s, type);
            if (!type) {
                update_state(m);
                return S_OK;
            }
            hr = init_presentation(m);
            if (SUCCEEDED(hr)) {
                update_state(m);
                return hr;
            }
        }
    }
    set_input_type_private(m, id, nullptr);
    clear_output_type(m);
    update_state(m);
    return hr;
}

/* ------------------------------------------------------------------ the video processor service */

static BOOL format_listed(const D3DFORMAT *list, UINT count, D3DFORMAT f)
{
    for (UINT i = 0; i < count; i++)
        if (list[i] == f)
            return TRUE;
    return FALSE;
}

static HRESULT add_format(D3DFORMAT f, UINT *count, D3DFORMAT **list)
{
    if (format_listed(*list, *count, f))
        return S_OK;
    D3DFORMAT *grown = HeapAlloc(GetProcessHeap(), 0, (*count + 1) * sizeof(*grown));
    if (!grown)
        return E_OUTOFMEMORY;
    if (*count)
        memcpy(grown, *list, *count * sizeof(*grown));
    HeapFree(GetProcessHeap(), 0, *list);
    grown[(*count)++] = f;
    *list = grown;
    return S_OK;
}

/* evr's GatherVideoProcessorRenderTargets: the formats some device can write for this input, the mode first. */
static HRESULT gather_targets(Mixer *m, const MFVIDEOFORMAT *format, const GUID *mode, UINT *out_count,
                              D3DFORMAT **out)
{
    DXVA2_VideoDesc desc;
    UINT count = 0, found = 0;
    GUID *guids = nullptr;
    D3DFORMAT *list = nullptr;
    EvrVideoDesc(format, &desc);
    HRESULT hr = m->service->lpVtbl->GetVideoProcessorDeviceGuids(m->service, &desc, &count, &guids);
    if (FAILED(hr))
        return hr;
    UINT total = count + (mode ? 1 : 0);
    for (UINT i = 0; i < total && SUCCEEDED(hr); i++) {
        const GUID *device = mode ? (i ? &guids[i - 1] : mode) : &guids[i];
        UINT n = 0;
        D3DFORMAT *formats = nullptr;
        if (FAILED(m->service->lpVtbl->GetVideoProcessorRenderTargets(m->service, device, &desc, &n, &formats)))
            continue;
        for (UINT k = 0; k < n; k++) {
            if (desc.Format == D3DFMT_A8R8G8B8) {
                DXVA2_VideoProcessorCaps caps = {0};
                if (FAILED(m->service->lpVtbl->GetVideoProcessorCaps(m->service, device, &desc, formats[k], &caps)) ||
                    (caps.DeviceCaps & DXVA2_VPDev_SoftwareDevice))
                    continue;
            }
            hr = add_format(formats[k], &found, &list);
            if (FAILED(hr))
                break;
        }
        CoTaskMemFree(formats);
    }
    CoTaskMemFree(guids);
    if (SUCCEEDED(hr) && !found)
        hr = MF_E_INVALIDMEDIATYPE;
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, list);
        return hr;
    }
    *out_count = found;
    *out = list;
    return hr;
}

/* evr's GatherVideoProcessorModesAndCaps: the devices that can write the output format, and the first one's caps. */
static HRESULT gather_modes(Mixer *m, const MFVIDEOFORMAT *format, D3DFORMAT target, DXVA2_VideoProcessorCaps *caps,
                            UINT *mode_count, GUID **modes, UINT *sub_count, D3DFORMAT **subs)
{
    DXVA2_VideoDesc desc;
    UINT count = 0, kept = 0, nsub = 0;
    GUID *guids = nullptr, *list = nullptr;
    D3DFORMAT *sublist = nullptr;
    EvrVideoDesc(format, &desc);
    HRESULT hr = m->service->lpVtbl->GetVideoProcessorDeviceGuids(m->service, &desc, &count, &guids);
    if (FAILED(hr))
        return hr;
    if (count && !(list = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*list)))) {
        CoTaskMemFree(guids);
        return E_OUTOFMEMORY;
    }
    for (UINT i = 0; i < count; i++) {
        UINT n = 0;
        D3DFORMAT *formats = nullptr;
        if (FAILED(m->service->lpVtbl->GetVideoProcessorRenderTargets(m->service, &guids[i], &desc, &n, &formats)))
            continue;
        BOOL listed = format_listed(formats, n, target);
        CoTaskMemFree(formats);
        DXVA2_VideoProcessorCaps c = {0};
        if (!listed || FAILED(m->service->lpVtbl->GetVideoProcessorCaps(m->service, &guids[i], &desc, target, &c)) ||
            c.NumBackwardRefSamples + c.NumForwardRefSamples > 31 ||
            (desc.Format == D3DFMT_A8R8G8B8 && (c.DeviceCaps & DXVA2_VPDev_SoftwareDevice)))
            continue;
        UINT ns = 0;
        hr = m->service->lpVtbl->GetVideoProcessorSubStreamFormats(m->service, &guids[i], &desc, target, &ns, nullptr);
        if (hr != S_OK)
            continue;
        if (ns) {
            D3DFORMAT *sf = nullptr;
            if (FAILED(m->service->lpVtbl->GetVideoProcessorSubStreamFormats(m->service, &guids[i], &desc, target, &ns,
                                                                             &sf)))
                continue;
            for (UINT k = 0; k < ns && SUCCEEDED(hr); k++)
                hr = add_format(sf[k], &nsub, &sublist);
            CoTaskMemFree(sf);
            if (FAILED(hr))
                break;
        }
        list[kept++] = guids[i];
        if (kept == 1 && caps)
            *caps = c;
    }
    CoTaskMemFree(guids);
    if (SUCCEEDED(hr) && !kept)
        hr = MF_E_INVALIDMEDIATYPE;
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, list);
        HeapFree(GetProcessHeap(), 0, sublist);
        return hr;
    }
    *mode_count = kept;
    *modes = list;
    *sub_count = nsub;
    *subs = sublist;
    return S_OK;
}

static void uninitialize_device(Mixer *m)
{
    m->state = 0;
    clear_output_type(m);
    memset(&m->device_caps, 0, sizeof(m->device_caps));
    if (m->service) {
        m->service->lpVtbl->Release(m->service);
        m->service = nullptr;
    }
    free_resources(m);
}

/* a new device forgets every input type. */
static HRESULT set_video_device(Mixer *m, IDirectXVideoProcessorService *service)
{
    uninitialize_device(m);
    if (service) {
        m->service = service;
        service->lpVtbl->AddRef(service);
        m->state = 1;
        for (DWORD i = 0; i < m->count; i++)
            set_input_type_private(m, m->streams[i]->id, nullptr);
        internal_flush(m);
    }
    update_state(m);
    return S_OK;
}

/* evr's VerifyDeviceExists: the processor service comes from the manager, reopening the handle when it went stale. */
static HRESULT verify_device(Mixer *m)
{
    HRESULT hr;
    BOOL opened = FALSE;
    IDirectXVideoProcessorService *service = nullptr;
    if (!m->manager) {
        set_video_device(m, nullptr);
        return MF_E_NOT_INITIALIZED;
    }
    for (int attempt = 0;; attempt++) {
        if (!m->handle) {
            hr = m->manager->lpVtbl->OpenDeviceHandle(m->manager, &m->handle);
            if (FAILED(hr))
                break;
            opened = TRUE;
        }
        hr = m->manager->lpVtbl->GetVideoService(m->manager, m->handle, &IID_IDirectXVideoProcessorService,
                                                 (void **)&service);
        if (SUCCEEDED(hr) || attempt)
            break;
        hr = m->manager->lpVtbl->CloseDeviceHandle(m->manager, m->handle);
        if (FAILED(hr))
            break;
        m->handle = nullptr;
    }
    if (SUCCEEDED(hr) && opened)
        hr = set_video_device(m, service);
    if (service)
        service->lpVtbl->Release(service);
    if (FAILED(hr))
        set_video_device(m, nullptr);
    return hr;
}

static BOOL range_check(DXVA2_Fixed32 v, const DXVA2_ValueRange *r)
{
    if (v.ll > r->MaxValue.ll || v.Value < r->MinValue.Value)
        return FALSE;
    if (v.Value == r->MinValue.Value)
        return v.Fraction >= r->MinValue.Fraction;
    return TRUE;
}

static void free_resources(Mixer *m)
{
    internal_flush(m);
    if (m->vp) {
        m->vp->lpVtbl->Release(m->vp);
        m->vp = nullptr;
    }
}

/* evr's AllocateVideoProcessorResourcesWorker: one device made for the reference format and the output format. */
static HRESULT allocate_worker(Mixer *m, const DXVA2_VideoDesc *desc, const GUID *device)
{
    D3DFORMAT target = (D3DFORMAT)m->output_format->surfaceInfo.Format;
    UINT n = 0;
    D3DFORMAT *formats = nullptr;
    HRESULT hr = m->service->lpVtbl->GetVideoProcessorRenderTargets(m->service, device, desc, &n, &formats);
    if (FAILED(hr))
        goto fail;
    BOOL listed = format_listed(formats, n, target);
    CoTaskMemFree(formats);
    if (!listed) {
        hr = E_FAIL;
        goto fail;
    }
    DXVA2_VideoProcessorCaps caps = {0};
    if (desc->Format == D3DFMT_A8R8G8B8) {
        hr = m->service->lpVtbl->GetVideoProcessorCaps(m->service, device, desc, target, &caps);
        if (FAILED(hr))
            goto fail;
        if (caps.DeviceCaps & DXVA2_VPDev_SoftwareDevice) {
            hr = E_FAIL;
            goto fail;
        }
    }
    memset(&caps, 0, sizeof(caps));
    hr = m->service->lpVtbl->GetVideoProcessorCaps(m->service, device, desc, target, &caps);
    if (FAILED(hr))
        goto fail;
    UINT substreams = caps.VideoProcessorOperations & 0x60 ? MAX_STREAMS : 1;
    if (m->vp) {
        m->vp->lpVtbl->Release(m->vp);
        m->vp = nullptr;
    }
    hr = m->service->lpVtbl->CreateVideoProcessor(m->service, device, desc, target, substreams - 1, &m->vp);
    if (FAILED(hr))
        goto fail;
    for (UINT i = 0, bit = 1; i < 4; i++, bit <<= 1) {
        if (!(caps.ProcAmpControlCaps & bit))
            continue;
        DXVA2_ValueRange r;
        hr = m->service->lpVtbl->GetProcAmpRange(m->service, device, desc, target, bit, &r);
        if (FAILED(hr))
            goto fail;
        if (memcmp(&m->procamp_ranges[i], &r, sizeof(r))) {
            m->procamp[i] = r.DefaultValue;
            m->procamp_ranges[i] = r;
        }
    }
    UINT first = 0, settings = 0;
    if (caps.VideoProcessorOperations & DXVA2_VideoProcess_DetailFilter)
        settings = 6, first = 7;
    if (caps.VideoProcessorOperations & DXVA2_VideoProcess_NoiseFilter)
        first = 1, settings = caps.VideoProcessorOperations & DXVA2_VideoProcess_DetailFilter ? 12 : 6;
    for (UINT k = 0; k < settings; k++) {
        UINT setting = first + k;
        DXVA2_ValueRange r;
        hr = m->service->lpVtbl->GetFilterPropertyRange(m->service, device, desc, target, setting, &r);
        if (FAILED(hr))
            goto fail;
        if (memcmp(&m->filter_ranges[setting - 1], &r, sizeof(r))) {
            m->filters[setting - 1] = r.DefaultValue;
            m->filter_ranges[setting - 1] = r;
        }
    }
    UINT refs = caps.NumBackwardRefSamples + caps.NumForwardRefSamples + 1;
    if (refs > 32) {
        hr = (HRESULT)0x88760827;
        goto fail;
    }
    MixerStream *ref = reference(m);
    hr = ref->attributes->lpVtbl->SetUINT32(ref->attributes, &MF_SA_REQUIRED_SAMPLE_COUNT, refs);
    if (FAILED(hr))
        goto fail;
    m->device = *device;
    m->caps = caps;
    return S_OK;
fail:
    free_resources(m);
    return hr;
}

static HRESULT allocate_resources(Mixer *m)
{
    free_resources(m);
    MixerStream *ref = reference(m);
    if (!ref || !ref->format || !m->output_type)
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    DXVA2_VideoDesc desc;
    EvrVideoDesc(ref->format, &desc);
    HRESULT hr;
    if (m->mode_set) {
        hr = allocate_worker(m, &desc, &m->mode);
        if (SUCCEEDED(hr))
            return hr;
    }
    if ((desc.SampleFormat.value & 0xFF) == DXVA2_SampleProgressiveFrame) {
        hr = allocate_worker(m, &desc, &DXVA2_VideoProcProgressiveDevice);
        if (FAILED(hr))
            hr = allocate_worker(m, &desc, &DXVA2_VideoProcSoftwareDevice);
    } else {
        UINT count = 0;
        GUID *guids = nullptr;
        hr = m->service->lpVtbl->GetVideoProcessorDeviceGuids(m->service, &desc, &count, &guids);
        for (UINT i = 0; SUCCEEDED(hr) && i < count; i++) {
            hr = allocate_worker(m, &desc, &guids[i]);
            if (SUCCEEDED(hr))
                break;
        }
        CoTaskMemFree(guids);
    }
    if (FAILED(hr))
        free_resources(m);
    return hr;
}

/* 0 without a device, 1 with one, 2 with the reference type, 3 with the output type, 4 with a video processor. */
static void update_state(Mixer *m)
{
    MixerStream *ref = reference(m);
    if (!m->service)
        m->state = 0;
    else if (!ref || !ref->type)
        m->state = 1;
    else if (!m->output_type)
        m->state = 2;
    else
        m->state = m->vp ? 4 : 3;
}

/* evr's GetTextureFilterModeSupported: the best filter that minifies and magnifies. */
static DWORD best_filter(const D3DCAPS9 *caps)
{
    DWORD f = caps->TextureFilterCaps;
    if ((f & 0x10001000) == 0x10001000)
        return 7;
    if ((f & 0x8000800) == 0x8000800)
        return 6;
    if ((f & 0x4000400) == 0x4000400)
        return 3;
    if ((f & 0x2000200) == 0x2000200)
        return 2;
    return (f & 0x1000100) == 0x1000100;
}

/* the mixer's own lock of the manager's device, with the caps read the first time. */
static HRESULT lock_device(Mixer *m)
{
    if (!m->manager)
        return MF_E_NOT_INITIALIZED;
    HRESULT hr;
    BOOL opened = FALSE;
    IDirect3DDevice9 *device = nullptr;
    for (int attempt = 0;; attempt++) {
        if (!m->lock_handle) {
            hr = m->manager->lpVtbl->OpenDeviceHandle(m->manager, &m->lock_handle);
            if (FAILED(hr))
                return hr;
            opened = TRUE;
        }
        hr = m->manager->lpVtbl->LockDevice(m->manager, m->lock_handle, &device, TRUE);
        if (SUCCEEDED(hr) || attempt)
            break;
        hr = m->manager->lpVtbl->CloseDeviceHandle(m->manager, m->lock_handle);
        m->lock_handle = nullptr;
        if (FAILED(hr))
            return hr;
    }
    if (FAILED(hr))
        return hr;
    if (opened) {
        hr = device->lpVtbl->GetDeviceCaps(device, &m->device_caps);
        m->app_filter = best_filter(&m->device_caps);
    }
    if (SUCCEEDED(hr)) {
        m->locked_device = device;
        device->lpVtbl->AddRef(device);
    } else {
        m->manager->lpVtbl->UnlockDevice(m->manager, m->lock_handle, FALSE);
    }
    device->lpVtbl->Release(device);
    return hr;
}

static void unlock_device(Mixer *m)
{
    if (m->locked_device) {
        m->locked_device->lpVtbl->Release(m->locked_device);
        m->locked_device = nullptr;
    }
    m->manager->lpVtbl->UnlockDevice(m->manager, m->lock_handle, FALSE);
}

/* evr's ConvertRGBToYCrCb, the studio range bt.601 colour of the background. */
static DXVA2_AYUVSample16 rgb_to_ycrcb(int r, int g, int b)
{
    DXVA2_AYUVSample16 s;
    s.Cr = (USHORT)((int)((float)r * 0.439f - (float)g * 0.368f - (float)b * 0.071f + 128.5f) << 8);
    s.Cb = (USHORT)((int)((float)r * -0.148f - (float)g * 0.291f + (float)b * 0.439f + 128.5f) << 8);
    s.Y = (USHORT)((int)((float)g * 0.504f + (float)r * 0.257f + (float)b * 0.098f + 16.5f) << 8);
    s.Alpha = 0xFFFF;
    return s;
}

/* ------------------------------------------------------------------ IUnknown */

static HRESULT query(Mixer *m, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown))
        *out = &m->inner;
    else if (IsEqualGUID(riid, &IID_IMFVideoMixerControl) || IsEqualGUID(riid, &IID_IMFVideoMixerControl2))
        *out = &m->control;
    else if (IsEqualGUID(riid, &IID_IMFAttributes))
        *out = &m->attributes;
    else if (IsEqualGUID(riid, &IID_IMFTopologyServiceLookupClient))
        *out = &m->lookup;
    else if (IsEqualGUID(riid, &IID_IMFVideoDeviceID))
        *out = &m->device_id;
    else if (IsEqualGUID(riid, &IID_IMFTransform))
        *out = &m->transform;
    else if (IsEqualGUID(riid, &IID_IMFVideoPositionMapper))
        *out = &m->mapper;
    else if (IsEqualGUID(riid, &IID_IMFClockStateSink))
        *out = &m->clock;
    else if (IsEqualGUID(riid, &IID_IMFGetService_))
        *out = &m->get_service;
    else if (IsEqualGUID(riid, &IID_IMFQualityAdvise))
        *out = &m->quality;
    else if (IsEqualGUID(riid, &IID_IMFVideoMixerBitmap))
        *out = &m->bitmap;
    else if (IsEqualGUID(riid, &IID_IDirect3DDeviceManager9))
        *out = &m->manager_iface;
    else if (IsEqualGUID(riid, &IID_IMFVideoProcessor))
        *out = &m->processor;
    else if (IsEqualGUID(riid, &IID_IEVRTrustedVideoPlugin))
        *out = &m->trusted;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ((IUnknown *)*out)->lpVtbl->AddRef((IUnknown *)*out);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE inner_QueryInterface(IUnknown *This, REFIID riid, void **out)
{
    return query(IMPL(inner), riid, out);
}

static ULONG STDMETHODCALLTYPE inner_AddRef(IUnknown *This)
{
    return InterlockedIncrement(&IMPL(inner)->refs);
}

static ULONG STDMETHODCALLTYPE inner_Release(IUnknown *This)
{
    Mixer *m = IMPL(inner);
    ULONG refs = InterlockedDecrement(&m->refs);
    if (refs)
        return refs;
    m->refs = 1;
    internal_flush(m);
    for (DWORD i = 0; i < m->count; i++)
        stream_free(m->streams[i]);
    clear_output_type(m);
    if (m->vp)
        m->vp->lpVtbl->Release(m->vp);
    if (m->service)
        m->service->lpVtbl->Release(m->service);
    if (m->sink)
        m->sink->lpVtbl->Release(m->sink);
    if (m->manager)
        m->manager->lpVtbl->Release(m->manager);
    if (m->store)
        m->store->lpVtbl->Release(m->store);
    if (m->transform_store)
        m->transform_store->lpVtbl->Release(m->transform_store);
    HeapFree(GetProcessHeap(), 0, m->targets);
    HeapFree(GetProcessHeap(), 0, m->modes);
    HeapFree(GetProcessHeap(), 0, m->substream_formats);
    HeapFree(GetProcessHeap(), 0, m->app_image);
    DeleteCriticalSection(&m->lock);
    HeapFree(GetProcessHeap(), 0, m);
    EvrObjectDestroyed();
    return 0;
}

static const IUnknownVtbl inner_vtbl = {inner_QueryInterface, inner_AddRef, inner_Release};

#define DELEGATE_IUNKNOWN(prefix, type, iface)                                                                         \
    static HRESULT STDMETHODCALLTYPE prefix##_QueryInterface(type *This, REFIID riid, void **out)                      \
    {                                                                                                                  \
        return IMPL(iface)->outer->lpVtbl->QueryInterface(IMPL(iface)->outer, riid, out);                              \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_AddRef(type *This)                                                         \
    {                                                                                                                  \
        return IMPL(iface)->outer->lpVtbl->AddRef(IMPL(iface)->outer);                                                 \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_Release(type *This)                                                        \
    {                                                                                                                  \
        return IMPL(iface)->outer->lpVtbl->Release(IMPL(iface)->outer);                                                \
    }

/* ------------------------------------------------------------------ IMFAttributes */

DELEGATE_IUNKNOWN(attributes, IMFAttributes, attributes)
#define STORE_OF(This) (IMPL(attributes)->store)
EVR_ATTRIBUTE_FORWARDERS(attributes, IMFAttributes, STORE_OF)

static const IMFAttributesVtbl attributes_vtbl = {
    attributes_QueryInterface, attributes_AddRef, attributes_Release, EVR_ATTRIBUTE_METHODS(attributes),
};

/* ------------------------------------------------------------------ IMFTransform */

DELEGATE_IUNKNOWN(transform, IMFTransform, transform)

static HRESULT STDMETHODCALLTYPE transform_GetStreamLimits(IMFTransform *This, DWORD *in_min, DWORD *in_max,
                                                           DWORD *out_min, DWORD *out_max)
{
    if (!in_min || !in_max || !out_min || !out_max)
        return E_POINTER;
    *in_min = 1;
    *in_max = IMPL(transform)->max_streams;
    *out_min = *out_max = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transform_GetStreamCount(IMFTransform *This, DWORD *in, DWORD *out)
{
    Mixer *m = IMPL(transform);
    if (!in || !out)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    *in = m->count;
    *out = 1;
    LeaveCriticalSection(&m->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transform_GetStreamIDs(IMFTransform *This, DWORD in_size, DWORD *in, DWORD out_size,
                                                        DWORD *out)
{
    Mixer *m = IMPL(transform);
    HRESULT hr = S_OK;
    if (!in || !out)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    if (m->count > in_size || !out_size) {
        hr = MF_E_BUFFERTOOSMALL;
    } else {
        for (DWORD i = 0; i < m->count; i++)
            in[i] = m->streams[i]->id;
        *out = 0;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE transform_GetInputStreamInfo(IMFTransform *This, DWORD id, MFT_INPUT_STREAM_INFO *info)
{
    Mixer *m = IMPL(transform);
    HRESULT hr = S_OK;
    if (!info)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    if (!find_stream(m, id, nullptr)) {
        hr = MF_E_INVALIDSTREAMNUMBER;
    } else {
        memset(info, 0, sizeof(*info));
        info->dwFlags = id ? 0x60F : 0xF;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE transform_GetOutputStreamInfo(IMFTransform *This, DWORD id,
                                                               MFT_OUTPUT_STREAM_INFO *info)
{
    if (!info)
        return E_POINTER;
    if (id)
        return MF_E_INVALIDSTREAMNUMBER;
    info->cbSize = info->cbAlignment = 0;
    info->dwFlags = 7;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transform_GetAttributes(IMFTransform *This, IMFAttributes **attributes)
{
    if (!attributes)
        return E_POINTER;
    *attributes = IMPL(transform)->transform_store;
    (*attributes)->lpVtbl->AddRef(*attributes);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transform_GetInputStreamAttributes(IMFTransform *This, DWORD id,
                                                                    IMFAttributes **attributes)
{
    if (!attributes)
        return E_POINTER;
    MixerStream *s = find_stream(IMPL(transform), id, nullptr);
    if (!s)
        return MF_E_INVALIDSTREAMNUMBER;
    *attributes = s->attributes;
    s->attributes->lpVtbl->AddRef(s->attributes);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transform_GetOutputStreamAttributes(IMFTransform *This, DWORD id,
                                                                     IMFAttributes **attributes)
{
    return E_NOTIMPL;
}

/* a stream that still holds a sample is taken out of the list but not freed, as windows does. */
static HRESULT STDMETHODCALLTYPE transform_DeleteInputStream(IMFTransform *This, DWORD id)
{
    Mixer *m = IMPL(transform);
    HRESULT hr = S_OK;
    DWORD position;
    EnterCriticalSection(&m->lock);
    MixerStream *s = id ? find_stream(m, id, &position) : nullptr;
    if (!s) {
        hr = MF_E_INVALIDSTREAMNUMBER;
    } else {
        remove_at(m, position);
        if (s->current) {
            hr = MF_E_TRANSFORM_INPUT_REMAINING;
        } else {
            DWORD index = s->index;
            stream_free(s);
            for (DWORD i = 0; i < m->count; i++)
                if (m->streams[i]->index > index)
                    m->streams[i]->index--;
        }
    }
    update_state(m);
    LeaveCriticalSection(&m->lock);
    return hr;
}

/* the same id may come twice in one call, as windows allows. */
static HRESULT STDMETHODCALLTYPE transform_AddInputStreams(IMFTransform *This, DWORD count, DWORD *ids)
{
    Mixer *m = IMPL(transform);
    HRESULT hr = S_OK;
    if (!ids)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    if (!count || count > m->max_streams - m->count) {
        hr = E_INVALIDARG;
        goto done;
    }
    for (DWORD i = 0; i < count; i++)
        if (find_stream(m, ids[i], nullptr)) {
            hr = E_INVALIDARG;
            goto done;
        }
    DWORD added = 0;
    for (; added < count; added++) {
        hr = add_stream(m, ids[added], FALSE);
        if (FAILED(hr))
            break;
    }
    if (FAILED(hr))
        while (added--)
            stream_free(m->streams[--m->count]);
done:
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE transform_GetInputAvailableType(IMFTransform *This, DWORD id, DWORD index,
                                                                 IMFMediaType **type)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE transform_GetOutputAvailableType(IMFTransform *This, DWORD id, DWORD index,
                                                                  IMFMediaType **type)
{
    Mixer *m = IMPL(transform);
    HRESULT hr;
    if (!type)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    if (id) {
        hr = MF_E_INVALIDSTREAMNUMBER;
    } else if (FAILED(verify_device(m)) || m->state < 2) {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    } else if (index >= m->target_count) {
        hr = MF_E_NO_MORE_TYPES;
    } else {
        m->presentation.surfaceInfo.Format = m->targets[index];
        m->presentation.guidFormat.Data1 = m->targets[index];
        IMFMediaType *made = nullptr;
        hr = MFCreateVideoMediaType(&m->presentation, &made);
        if (SUCCEEDED(hr)) {
            hr = made->lpVtbl->QueryInterface(made, &IID_IMFMediaType, (void **)type);
            made->lpVtbl->Release(made);
        }
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static BOOL same_desc(const MFVIDEOFORMAT *a, const MFVIDEOFORMAT *b, BOOL deinterlacing)
{
    DXVA2_VideoDesc x, y;
    EvrVideoDesc(a, &x);
    EvrVideoDesc(b, &y);
    UINT fa = x.SampleFormat.value & 0xFF, fb = y.SampleFormat.value & 0xFF;
    if (deinterlacing && (((fa - 3 <= 1 || fa == 2) && fb - 3 <= 1) || fb == 2)) {
        x.SampleFormat.value = (x.SampleFormat.value & ~0xFFu) | 3;
        y.SampleFormat.value = (y.SampleFormat.value & ~0xFFu) | 3;
        x.OutputFrameFreq.Numerator = y.OutputFrameFreq.Numerator = 2 * x.InputSampleFreq.Numerator;
        x.OutputFrameFreq.Denominator = y.OutputFrameFreq.Denominator = x.InputSampleFreq.Denominator;
    } else if (fa - 5 <= 1 && fb - 5 <= 1) {
        x.SampleFormat.value = (x.SampleFormat.value & ~0xFFu) | 5;
        y.SampleFormat.value = (y.SampleFormat.value & ~0xFFu) | 5;
    }
    return !memcmp(&x, &y, sizeof(x));
}

static HRESULT STDMETHODCALLTYPE transform_SetInputType(IMFTransform *This, DWORD id, IMFMediaType *type, DWORD flags)
{
    Mixer *m = IMPL(transform);
    HRESULT hr = S_OK;
    IUnknown *video = nullptr;
    D3DFORMAT *targets = nullptr;
    UINT target_count = 0;
    BOOL changed = TRUE;
    EnterCriticalSection(&m->lock);
    if (type) {
        if (FAILED(type->lpVtbl->QueryInterface(type, &IID_IMFVideoMediaType, (void **)&video))) {
            hr = MF_E_INVALIDMEDIATYPE;
            goto done;
        }
        const MFVIDEOFORMAT *format = EvrVideoFormat(video);
        if (!format) {
            hr = MF_E_INVALIDMEDIATYPE;
            goto done;
        }
        hr = verify_device(m);
        if (FAILED(hr))
            goto done;
        if (!id) {
            MixerStream *ref = reference(m);
            if (ref && ref->format && same_desc(format, ref->format, FALSE)) {
                changed = FALSE;
            } else {
                hr = gather_targets(m, format, m->mode_set ? &m->mode : nullptr, &target_count, &targets);
                if (FAILED(hr))
                    goto done;
            }
        } else {
            if (m->state < 3 && !(flags & MFT_SET_TYPE_TEST_ONLY)) {
                hr = MF_E_TRANSFORM_TYPE_NOT_SET;
                goto done;
            }
            UINT32 interlace = 0;
            if (!(m->caps.VideoProcessorOperations & 0x60) ||
                (SUCCEEDED(type->lpVtbl->GetUINT32(type, &MF_MT_INTERLACE_MODE, &interlace)) && (interlace & ~2u)) ||
                !format_listed(m->substream_formats, m->substream_format_count,
                               (D3DFORMAT)format->surfaceInfo.Format)) {
                hr = MF_E_INVALIDMEDIATYPE;
                goto done;
            }
        }
    }
    if (!find_stream(m, id, nullptr)) {
        hr = MF_E_INVALIDSTREAMNUMBER;
    } else if (flags >= 2 || (!type && (flags & MFT_SET_TYPE_TEST_ONLY))) {
        hr = E_INVALIDARG;
    } else {
        if (type) {
            hr = EvrValidateVideoType(video);
            if (SUCCEEDED(hr) && !id && SUCCEEDED(type->lpVtbl->GetItem(type, &MF_MT_PALETTE, nullptr)))
                hr = MF_E_INVALIDMEDIATYPE;
        }
        if (SUCCEEDED(hr) && !(flags & MFT_SET_TYPE_TEST_ONLY))
            hr = set_input_type_private(m, id, video);
    }
    if (SUCCEEDED(hr) && !id && changed && !(flags & MFT_SET_TYPE_TEST_ONLY)) {
        HeapFree(GetProcessHeap(), 0, m->targets);
        HeapFree(GetProcessHeap(), 0, m->modes);
        HeapFree(GetProcessHeap(), 0, m->substream_formats);
        m->targets = nullptr;
        m->modes = nullptr;
        m->substream_formats = nullptr;
        m->target_count = m->mode_count = m->substream_format_count = 0;
        free_resources(m);
        if (type) {
            m->targets = targets;
            m->target_count = target_count;
            targets = nullptr;
        }
    }
done:
    update_state(m);
    if (video)
        video->lpVtbl->Release(video);
    HeapFree(GetProcessHeap(), 0, targets);
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE transform_SetOutputType(IMFTransform *This, DWORD id, IMFMediaType *type,
                                                         DWORD flags)
{
    Mixer *m = IMPL(transform);
    HRESULT hr = S_OK;
    IUnknown *video = nullptr;
    GUID *modes = nullptr;
    D3DFORMAT *subs = nullptr;
    UINT mode_count = 0, sub_count = 0;
    DXVA2_VideoProcessorCaps caps = {0};
    EnterCriticalSection(&m->lock);
    if (type) {
        if (FAILED(type->lpVtbl->QueryInterface(type, &IID_IMFVideoMediaType, (void **)&video))) {
            hr = MF_E_INVALIDMEDIATYPE;
            goto done;
        }
        const MFVIDEOFORMAT *format = EvrVideoFormat(video);
        if (!format || !format_listed(m->targets, m->target_count, (D3DFORMAT)format->surfaceInfo.Format)) {
            hr = MF_E_INVALIDMEDIATYPE;
            goto done;
        }
        MixerStream *ref = reference(m);
        if (!ref || !ref->format || FAILED(verify_device(m))) {
            hr = MF_E_TRANSFORM_TYPE_NOT_SET;
            goto done;
        }
        hr = gather_modes(m, ref->format, (D3DFORMAT)format->surfaceInfo.Format, &caps, &mode_count, &modes,
                          &sub_count, &subs);
        if (FAILED(hr))
            goto done;
    }
    if (id) {
        hr = MF_E_INVALIDSTREAMNUMBER;
        goto done;
    }
    if (flags >= 2 || (!type && (flags & MFT_SET_TYPE_TEST_ONLY))) {
        hr = E_INVALIDARG;
        goto done;
    }
    if (!type) {
        clear_output_type(m);
        goto done;
    }
    if (m->state < 2) {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
        goto done;
    }
    hr = EvrValidateVideoType(video);
    if (FAILED(hr))
        goto done;
    const MFVIDEOFORMAT *format = EvrVideoFormat(video);
    if (format->videoInfo.GeometricAperture.OffsetX.fract || format->videoInfo.GeometricAperture.OffsetY.fract ||
        (format->videoInfo.VideoFlags & 0x20000) ||
        format->videoInfo.FramesPerSecond.Numerator != m->presentation.videoInfo.FramesPerSecond.Numerator ||
        format->videoInfo.FramesPerSecond.Denominator != m->presentation.videoInfo.FramesPerSecond.Denominator) {
        hr = MF_E_INVALIDMEDIATYPE;
        goto done;
    }
    if (flags & MFT_SET_TYPE_TEST_ONLY)
        goto done;
    clear_output_type(m);
    m->output_type = video;
    video->lpVtbl->AddRef(video);
    m->output_format = format;
    update_state(m);
    if (m->state < 4 && m->streaming) {
        hr = allocate_resources(m);
        if (FAILED(hr))
            goto done;
    }
    HeapFree(GetProcessHeap(), 0, m->modes);
    HeapFree(GetProcessHeap(), 0, m->substream_formats);
    m->modes = modes;
    m->mode_count = mode_count;
    m->substream_formats = subs;
    m->substream_format_count = sub_count;
    modes = nullptr;
    subs = nullptr;
    m->caps = caps;
    m->max_streams = caps.VideoProcessorOperations & 0x60 ? MAX_STREAMS : 1;
    MixerStream *ref = reference(m);
    hr = ref->attributes->lpVtbl->SetUINT32(ref->attributes, &MF_SA_REQUIRED_SAMPLE_COUNT,
                                            caps.NumBackwardRefSamples + caps.NumForwardRefSamples + 1);
done:
    update_state(m);
    LeaveCriticalSection(&m->lock);
    if (video)
        video->lpVtbl->Release(video);
    HeapFree(GetProcessHeap(), 0, modes);
    HeapFree(GetProcessHeap(), 0, subs);
    return hr;
}

static HRESULT STDMETHODCALLTYPE transform_GetInputCurrentType(IMFTransform *This, DWORD id, IMFMediaType **type)
{
    Mixer *m = IMPL(transform);
    HRESULT hr;
    EnterCriticalSection(&m->lock);
    MixerStream *s = find_stream(m, id, nullptr);
    if (!s)
        hr = MF_E_INVALIDSTREAMNUMBER;
    else if (!s->type)
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    else
        hr = s->type->lpVtbl->QueryInterface(s->type, &IID_IMFMediaType, (void **)type);
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE transform_GetOutputCurrentType(IMFTransform *This, DWORD id, IMFMediaType **type)
{
    Mixer *m = IMPL(transform);
    HRESULT hr;
    EnterCriticalSection(&m->lock);
    if (id)
        hr = MF_E_INVALIDSTREAMNUMBER;
    else if (!type)
        hr = E_POINTER;
    else if (m->state < 2 || !m->output_type)
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    else
        hr = m->output_type->lpVtbl->QueryInterface(m->output_type, &IID_IMFMediaType, (void **)type);
    LeaveCriticalSection(&m->lock);
    return hr;
}

/*
 * the reference stream holds the picture it is on and one more: a picture that went out stays until the next one
 * replaces it. a substream takes a sample when it has none.
 */
static BOOL accepts(Mixer *m, MixerStream *s)
{
    if (m->draining)
        return TRUE;
    if (!s->id)
        return !s->current || (m->consumed && !s->next);
    /* a substream waits for the reference stream's first picture. */
    MixerStream *ref = reference(m);
    if (!ref->current && stream_enabled(ref))
        return FALSE;
    return !s->current || s->used;
}

static HRESULT STDMETHODCALLTYPE transform_GetInputStatus(IMFTransform *This, DWORD id, DWORD *flags)
{
    Mixer *m = IMPL(transform);
    HRESULT hr = S_OK;
    EnterCriticalSection(&m->lock);
    MixerStream *s;
    if (!flags)
        hr = E_POINTER;
    else if (FAILED(verify_device(m)) || m->state < 3)
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    else if (!(s = find_stream(m, id, nullptr)))
        hr = MF_E_INVALIDSTREAMNUMBER;
    else if (!s->type)
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    else
        *flags = accepts(m, s) ? MFT_INPUT_STATUS_ACCEPT_DATA : 0;
    LeaveCriticalSection(&m->lock);
    return hr;
}

/* the reference picture, and a fresh picture of every substream that is enabled. */
static BOOL output_ready(Mixer *m)
{
    MixerStream *ref = reference(m);
    if (!ref || !ref->current || (m->consumed && !ref->next))
        return FALSE;
    for (DWORD i = 0; i < m->count; i++) {
        MixerStream *s = m->streams[i];
        if (s->id && (s->enabled || s->current) && (!s->current || s->used))
            return FALSE;
    }
    return TRUE;
}

static HRESULT STDMETHODCALLTYPE transform_GetOutputStatus(IMFTransform *This, DWORD *flags)
{
    Mixer *m = IMPL(transform);
    if (!flags)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    *flags = output_ready(m) ? MFT_OUTPUT_STATUS_SAMPLE_READY : 0;
    LeaveCriticalSection(&m->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transform_SetOutputBounds(IMFTransform *This, LONGLONG lower, LONGLONG upper)
{
    Mixer *m = IMPL(transform);
    EnterCriticalSection(&m->lock);
    m->lower_bound = lower;
    m->upper_bound = upper;
    LeaveCriticalSection(&m->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transform_ProcessEvent(IMFTransform *This, DWORD id, IMFMediaEvent *event)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE transform_ProcessMessage(IMFTransform *This, MFT_MESSAGE_TYPE message,
                                                          ULONG_PTR param)
{
    Mixer *m = IMPL(transform);
    HRESULT hr = S_OK;
    MixerStream *s;
    EnterCriticalSection(&m->lock);
    switch (message) {
    case MFT_MESSAGE_SET_D3D_MANAGER:
        if (m->manager) {
            m->manager->lpVtbl->Release(m->manager);
            m->manager = nullptr;
        }
        if (param)
            hr = ((IUnknown *)param)->lpVtbl->QueryInterface((IUnknown *)param, &IID_IDirect3DDeviceManager9,
                                                             (void **)&m->manager);
        break;
    case MFT_MESSAGE_COMMAND_FLUSH:
        if (m->streaming)
            internal_flush(m);
        break;
    case MFT_MESSAGE_COMMAND_DRAIN:
        if (param < 2)
            m->draining = TRUE;
        else
            hr = E_INVALIDARG;
        break;
    case MFT_MESSAGE_DROP_SAMPLES:
        m->drop_samples += (DWORD)param;
        break;
    case MFT_MESSAGE_NOTIFY_BEGIN_STREAMING:
        if (m->state != 4) {
            hr = allocate_resources(m);
            if (SUCCEEDED(hr)) {
                m->streaming = TRUE;
                update_state(m);
                if ((s = reference(m)))
                    request_sample(m, s);
            }
        }
        break;
    case MFT_MESSAGE_NOTIFY_END_STREAMING:
        free_resources(m);
        update_state(m);
        m->streaming = FALSE;
        break;
    case MFT_MESSAGE_NOTIFY_END_OF_STREAM:
        if ((s = find_stream(m, (DWORD)param, nullptr)))
            s->enabled = FALSE;
        else
            hr = MF_E_INVALIDSTREAMNUMBER;
        break;
    case MFT_MESSAGE_NOTIFY_START_OF_STREAM:
        if (m->state < 3)
            hr = MF_E_TRANSFORM_TYPE_NOT_SET;
        else if (!(s = find_stream(m, (DWORD)param, nullptr)))
            hr = MF_E_INVALIDSTREAMNUMBER;
        else if (!s->type)
            hr = MF_E_TRANSFORM_TYPE_NOT_SET;
        else
            s->enabled = TRUE;
        break;
    default:
        break;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE transform_ProcessInput(IMFTransform *This, DWORD id, IMFSample *sample, DWORD flags)
{
    Mixer *m = IMPL(transform);
    HRESULT hr = S_OK;
    if (flags)
        return E_INVALIDARG;
    if (!sample)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    MixerStream *s = find_stream(m, id, nullptr), *ref = reference(m);
    if (!s) {
        hr = MF_E_INVALIDSTREAMNUMBER;
    } else if (id && ref && !stream_enabled(ref)) {
        request_sample(m, s);
    } else if (m->state < 3 || !s->type) {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    } else if (!id && !ref->type) {
        hr = MF_E_UNEXPECTED;
    } else if (!accepts(m, s)) {
        hr = MF_E_NOTACCEPTING;
    } else {
        if (m->state < 4) {
            hr = allocate_resources(m);
            if (FAILED(hr))
                goto done;
            update_state(m);
        }
        m->streaming = TRUE;
        if (!id && !s->current) {
            s->current = sample;
            sample->lpVtbl->AddRef(sample);
            m->consumed = FALSE;
            if (!m->caps.NumForwardRefSamples)
                for (DWORD i = 0; i < m->count; i++)
                    if (m->streams[i]->id)
                        request_sample(m, m->streams[i]);
        } else if (!id) {
            if (s->next)
                s->next->lpVtbl->Release(s->next);
            s->next = sample;
            sample->lpVtbl->AddRef(sample);
        } else {
            if (s->current)
                s->current->lpVtbl->Release(s->current);
            s->current = sample;
            sample->lpVtbl->AddRef(sample);
            s->used = FALSE;
            s->enabled = TRUE;
        }
    }
done:
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT surface_of_sample(IMFSample *sample, IDirect3DSurface9 **surface)
{
    IMFMediaBuffer *buffer = nullptr;
    IMFGetService *service = nullptr;
    HRESULT hr = sample->lpVtbl->GetBufferByIndex(sample, 0, &buffer);
    if (SUCCEEDED(hr))
        hr = buffer->lpVtbl->QueryInterface(buffer, &IID_IMFGetService_, (void **)&service);
    if (SUCCEEDED(hr))
        hr = service->lpVtbl->GetService(service, &MR_BUFFER_SERVICE, &IID_IDirect3DSurface9, (void **)surface);
    if (service)
        service->lpVtbl->Release(service);
    if (buffer)
        buffer->lpVtbl->Release(buffer);
    return hr;
}

static BOOL sample_empty(IMFSample *sample)
{
    IMFMediaBuffer *buffer = nullptr;
    DWORD length = 0;
    BOOL empty = FALSE;
    if (SUCCEEDED(sample->lpVtbl->GetBufferByIndex(sample, 0, &buffer)) &&
        SUCCEEDED(buffer->lpVtbl->GetCurrentLength(buffer, &length)))
        empty = !length;
    if (buffer)
        buffer->lpVtbl->Release(buffer);
    return empty;
}

static void blend_app_image(Mixer *m, IDirect3DSurface9 *target);

static float square_root(float v)
{
    float x = v > 1.0f ? v : 1.0f;
    for (int i = 0; i < 32; i++)
        x = 0.5f * (x + v / x);
    return x;
}

/* evr's ProcessVideo: one blit of the reference picture and every substream with a sample, in z order. */
static HRESULT process_video(Mixer *m, LONGLONG time, BOOL timed, IMFGetService *target_service)
{
    IDirect3DSurface9 *target = nullptr;
    HRESULT hr = target_service->lpVtbl->GetService(target_service, &MR_BUFFER_SERVICE, &IID_IDirect3DSurface9,
                                                    (void **)&target);
    if (FAILED(hr))
        return hr;
    MixerStream *ref = reference(m);
    if (!ref->format) {
        target->lpVtbl->Release(target);
        return MF_E_UNEXPECTED;
    }
    MFVideoNormalizedRect zoom = {0, 0, 1, 1};
    UINT32 size = 0;
    if (SUCCEEDED(m->transform_store->lpVtbl->GetBlob(m->transform_store, &VIDEO_ZOOM_RECT, (UINT8 *)&zoom,
                                                      sizeof(zoom), &size)) &&
        (size != 16 || zoom.left < 0 || zoom.right > 1 || zoom.top < 0 || zoom.bottom > 1 || zoom.left > zoom.right ||
         zoom.top > zoom.bottom))
        zoom = (MFVideoNormalizedRect){0, 0, 1, 1};
    DXVA2_VideoSample samples[MAX_SAMPLES];
    memset(samples, 0, sizeof(samples));
    UINT n = 0;
    for (DWORD i = 0; i < m->count && n < MAX_SAMPLES; i++) {
        MixerStream *s = m->streams[i];
        IMFSample *sample = s->current;
        if (!sample || !stream_enabled(s) || (s->id && sample_empty(sample)))
            continue;
        if (!s->format) {
            hr = MF_E_UNEXPECTED;
            break;
        }
        DXVA2_VideoSample *v = &samples[n];
        hr = surface_of_sample(sample, &v->SrcSurface);
        if (FAILED(hr))
            break;
        EvrSourceAndDestRects(s->format, m->output_format, &m->presentation, &zoom, &s->rect, &v->SrcRect,
                              &v->DstRect);
        v->SampleData = 4;
        UINT32 bff = 0, rff = 0;
        if (SUCCEEDED(sample->lpVtbl->GetUINT32(sample, &MFSampleExtension_BottomFieldFirst, &bff))) {
            if (!bff)
                v->SampleData |= 2;
            if (SUCCEEDED(sample->lpVtbl->GetUINT32(sample, &MFSampleExtension_RepeatFirstField, &rff)) && rff)
                v->SampleData |= 1;
        }
        v->PlanarAlpha.ll = 0x10000;
        if (!s->id) {
            LONGLONG start = 0, duration = 0;
            sample->lpVtbl->GetSampleTime(sample, &start);
            sample->lpVtbl->GetSampleDuration(sample, &duration);
            v->Start = start;
            v->End = start + duration;
            v->SampleFormat.value = EvrExtendedFormat(s->format, s->format->videoInfo.InterlaceMode, FALSE);
        } else {
            v->SampleFormat.value = DXVA2_SampleSubStream;
        }
        n++;
    }
    if (SUCCEEDED(hr)) {
        const MFVIDEOFORMAT *out = m->output_format;
        const MFVideoArea *g = &out->videoInfo.GeometricAperture;
        DXVA2_VideoProcessBltParams params;
        memset(&params, 0, sizeof(params));
        params.TargetFrame = timed ? time : 0;
        params.TargetRect.left = g->OffsetX.value;
        params.TargetRect.top = g->OffsetY.value;
        params.TargetRect.right = g->OffsetX.value + g->Area.cx;
        params.TargetRect.bottom = g->OffsetY.value + g->Area.cy;
        LONG w = params.TargetRect.right - params.TargetRect.left, h = params.TargetRect.bottom - params.TargetRect.top;
        if (m->constriction == (DWORD)-1 || 1000ull * m->constriction >= (ULONGLONG)(w * h)) {
            params.ConstrictionSize.cx = w;
            params.ConstrictionSize.cy = h;
        } else if (m->constriction) {
            float k = square_root((float)m->constriction * 1000.0f / (float)(w * h));
            params.ConstrictionSize.cx = (LONG)((float)w * k);
            params.ConstrictionSize.cy = (LONG)((float)h * k);
        } else {
            params.ConstrictionSize.cx = params.ConstrictionSize.cy = 1;
        }
        params.BackgroundColor = m->background_yuv;
        params.DestFormat.value = EvrExtendedFormat(out, out->videoInfo.InterlaceMode, TRUE);
        params.ProcAmpValues.Brightness = m->procamp[0];
        params.ProcAmpValues.Contrast = m->procamp[1];
        params.ProcAmpValues.Hue = m->procamp[2];
        params.ProcAmpValues.Saturation = m->procamp[3];
        params.Alpha.ll = 0x10000;
        memcpy(&params.NoiseFilterLuma, &m->filters[0], sizeof(DXVA2_Fixed32) * 3);
        memcpy(&params.NoiseFilterChroma, &m->filters[3], sizeof(DXVA2_Fixed32) * 3);
        memcpy(&params.DetailFilterLuma, &m->filters[6], sizeof(DXVA2_Fixed32) * 3);
        memcpy(&params.DetailFilterChroma, &m->filters[9], sizeof(DXVA2_Fixed32) * 3);
        hr = m->vp ? m->vp->lpVtbl->VideoProcessBlt(m->vp, target, &params, samples, n, nullptr)
                   : MF_E_TRANSFORM_TYPE_NOT_SET;
        if (SUCCEEDED(hr))
            blend_app_image(m, target);
    }
    for (UINT i = 0; i < n; i++)
        if (samples[i].SrcSurface)
            samples[i].SrcSurface->lpVtbl->Release(samples[i].SrcSurface);
    target->lpVtbl->Release(target);
    return hr;
}

/* evr's PaintDDrawSurfaceBlack, what a desired sample gets before streaming starts. */
static HRESULT paint_black(IMFGetService *service)
{
    IDirect3DSurface9 *surface = nullptr;
    IDirect3DDevice9 *device = nullptr;
    D3DSURFACE_DESC desc;
    HRESULT hr = service->lpVtbl->GetService(service, &MR_BUFFER_SERVICE, &IID_IDirect3DSurface9, (void **)&surface);
    if (SUCCEEDED(hr))
        hr = surface->lpVtbl->GetDesc(surface, &desc);
    if (SUCCEEDED(hr))
        hr = surface->lpVtbl->GetDevice(surface, &device);
    if (SUCCEEDED(hr))
        hr = device->lpVtbl->ColorFill(device, surface, nullptr,
                                       EvrIsFormatYUV(desc.Format) && !(desc.Usage & D3DUSAGE_RENDERTARGET)
                                           ? 0xFF108080
                                           : 0xFF000000);
    if (device)
        device->lpVtbl->Release(device);
    if (surface)
        surface->lpVtbl->Release(surface);
    return hr;
}

/* the next reference picture takes the place of one that already went out. */
static void advance_reference(Mixer *m)
{
    MixerStream *ref = reference(m);
    if (!m->consumed || !ref->next)
        return;
    ref->current->lpVtbl->Release(ref->current);
    ref->current = ref->next;
    ref->next = nullptr;
    m->consumed = FALSE;
}

/* the substreams' pictures went out with the frame: each asks for its next one. */
static void use_substreams(Mixer *m)
{
    for (DWORD i = 0; i < m->count; i++) {
        MixerStream *s = m->streams[i];
        if (!s->id || !s->current || s->used)
            continue;
        s->used = TRUE;
        request_sample(m, s);
    }
}

static HRESULT STDMETHODCALLTYPE transform_ProcessOutput(IMFTransform *This, DWORD flags, DWORD count,
                                                         MFT_OUTPUT_DATA_BUFFER *buffers, DWORD *status)
{
    Mixer *m = IMPL(transform);
    HRESULT hr;
    IMFMediaBuffer *buffer = nullptr;
    IMFGetService *service = nullptr;
    IUnknown *desired = nullptr;
    if (!buffers || !status)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    IMFSample *sample = buffers->pSample;
    if (count != 1 || flags || !sample) {
        hr = E_INVALIDARG;
        goto done;
    }
    if (buffers->dwStreamID) {
        hr = MF_E_INVALIDSTREAMNUMBER;
        goto done;
    }
    hr = sample->lpVtbl->GetBufferByIndex(sample, 0, &buffer);
    if (SUCCEEDED(hr))
        hr = buffer->lpVtbl->QueryInterface(buffer, &IID_IMFGetService_, (void **)&service);
    if (FAILED(hr))
        goto done;
    /* a sample counts as desired only once its desired time was set. */
    if (SUCCEEDED(sample->lpVtbl->QueryInterface(sample, &IID_IMFDesiredSample_, (void **)&desired))) {
        LONGLONG when, length;
        if (FAILED(((IMFDesiredSample *)desired)->lpVtbl->GetDesiredSampleTimeAndDuration((IMFDesiredSample *)desired,
                                                                                          &when, &length))) {
            desired->lpVtbl->Release(desired);
            desired = nullptr;
        }
    }
    if (!m->streaming) {
        if (desired) {
            hr = paint_black(service);
            if (SUCCEEDED(hr))
                *status = 0;
        } else {
            hr = MF_E_TRANSFORM_NEED_MORE_INPUT;
        }
        goto done;
    }
    if (!output_ready(m)) {
        if (m->draining)
            m->draining = FALSE;
        hr = MF_E_TRANSFORM_NEED_MORE_INPUT;
        goto done;
    }
    advance_reference(m);
    IMFSample *ref = reference(m)->current;
    LONGLONG time = 0, duration = 0;
    BOOL timed = TRUE;
    hr = ref->lpVtbl->GetSampleTime(ref, &time);
    if (SUCCEEDED(hr))
        hr = ref->lpVtbl->GetSampleDuration(ref, &duration);
    if (hr == MF_E_NO_SAMPLE_TIMESTAMP) {
        timed = FALSE;
        hr = S_OK;
    } else if (hr == MF_E_NO_SAMPLE_DURATION) {
        duration = 0;
        hr = S_OK;
    }
    if (FAILED(hr))
        goto done;
    BOOL dropped = FALSE;
    if (!desired && m->drop_samples) {
        m->drop_samples--;
        dropped = TRUE;
    } else {
        hr = process_video(m, time, timed, service);
    }
    if (FAILED(hr) && !desired) {
        m->consumed = TRUE;
        request_sample(m, reference(m));
        if (m->drop_samples)
            m->drop_samples--;
        goto done;
    }
    if (!dropped) {
        hr = buffer->lpVtbl->SetCurrentLength(buffer, m->output_format->videoInfo.GeometricAperture.Area.cx *
                                                          m->output_format->videoInfo.GeometricAperture.Area.cy);
        if (FAILED(hr))
            goto done;
    }
    if (!desired && timed) {
        hr = sample->lpVtbl->SetSampleTime(sample, time);
        if (SUCCEEDED(hr))
            hr = sample->lpVtbl->SetSampleDuration(sample, duration);
        if (FAILED(hr))
            goto done;
    }
    if (!desired) {
        m->consumed = TRUE;
        request_sample(m, reference(m));
        use_substreams(m);
    }
    m->lower_bound = time;
    *status = 0;
done:
    if (desired)
        desired->lpVtbl->Release(desired);
    if (service)
        service->lpVtbl->Release(service);
    if (buffer)
        buffer->lpVtbl->Release(buffer);
    LeaveCriticalSection(&m->lock);
    return hr;
}

static const IMFTransformVtbl transform_vtbl = {
    transform_QueryInterface,          transform_AddRef,
    transform_Release,                 transform_GetStreamLimits,
    transform_GetStreamCount,          transform_GetStreamIDs,
    transform_GetInputStreamInfo,      transform_GetOutputStreamInfo,
    transform_GetAttributes,           transform_GetInputStreamAttributes,
    transform_GetOutputStreamAttributes, transform_DeleteInputStream,
    transform_AddInputStreams,         transform_GetInputAvailableType,
    transform_GetOutputAvailableType,  transform_SetInputType,
    transform_SetOutputType,           transform_GetInputCurrentType,
    transform_GetOutputCurrentType,    transform_GetInputStatus,
    transform_GetOutputStatus,         transform_SetOutputBounds,
    transform_ProcessEvent,            transform_ProcessMessage,
    transform_ProcessInput,            transform_ProcessOutput,
};

/* ------------------------------------------------------------------ IMFVideoDeviceID */

DELEGATE_IUNKNOWN(device_id, IMFVideoDeviceID, device_id)

static HRESULT STDMETHODCALLTYPE device_id_GetDeviceID(IMFVideoDeviceID *This, IID *device)
{
    if (!device)
        return E_POINTER;
    *device = IID_IDirect3DDevice9;
    return S_OK;
}

static const IMFVideoDeviceIDVtbl device_id_vtbl = {
    device_id_QueryInterface, device_id_AddRef, device_id_Release, device_id_GetDeviceID,
};

/* ------------------------------------------------------------------ IMFTopologyServiceLookupClient */

DELEGATE_IUNKNOWN(lookup, IMFTopologyServiceLookupClient, lookup)

/* the sink is looked up once and kept, even when the lookup fails later. */
static HRESULT STDMETHODCALLTYPE lookup_InitServicePointers(IMFTopologyServiceLookupClient *This,
                                                            IMFTopologyServiceLookup *lookup)
{
    Mixer *m = IMPL(lookup);
    HRESULT hr = S_OK;
    EnterCriticalSection(&m->lock);
    if (!lookup) {
        hr = E_POINTER;
        if (m->sink) {
            m->sink->lpVtbl->Release(m->sink);
            m->sink = nullptr;
        }
    } else if (!m->sink) {
        DWORD count = 1;
        IMediaEventSink *sink = nullptr;
        lookup->lpVtbl->LookupService(lookup, MF_SERVICE_LOOKUP_GLOBAL, 0, &MR_VIDEO_RENDER_SERVICE,
                                      &IID_IMediaEventSink, (void **)&sink, &count);
        m->sink = sink;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE lookup_ReleaseServicePointers(IMFTopologyServiceLookupClient *This)
{
    Mixer *m = IMPL(lookup);
    EnterCriticalSection(&m->lock);
    if (m->sink) {
        m->sink->lpVtbl->Release(m->sink);
        m->sink = nullptr;
    }
    LeaveCriticalSection(&m->lock);
    return S_OK;
}

static const IMFTopologyServiceLookupClientVtbl lookup_vtbl = {
    lookup_QueryInterface, lookup_AddRef, lookup_Release, lookup_InitServicePointers, lookup_ReleaseServicePointers,
};

/* ------------------------------------------------------------------ IMFGetService */

DELEGATE_IUNKNOWN(get_service, IMFGetService, get_service)

static HRESULT STDMETHODCALLTYPE get_service_GetService(IMFGetService *This, REFGUID service, REFIID riid, void **out)
{
    Mixer *m = IMPL(get_service);
    HRESULT hr;
    if (!IsEqualGUID(service, &MR_VIDEO_MIXER_SERVICE))
        return MF_E_UNSUPPORTED_SERVICE;
    if (!out)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFVideoDeviceID) ||
        IsEqualGUID(riid, &IID_IMFVideoMixerControl) || IsEqualGUID(riid, &IID_IMFVideoMixerControl2) ||
        IsEqualGUID(riid, &IID_IMFVideoPositionMapper) || IsEqualGUID(riid, &IID_IMFVideoProcessor) ||
        IsEqualGUID(riid, &IID_IMFVideoMixerBitmap)) {
        hr = query(m, riid, out);
    } else if (IsEqualGUID(riid, &IID_IDirect3DDevice9)) {
        hr = lock_device(m);
        if (SUCCEEDED(hr)) {
            *out = m->locked_device;
            m->locked_device->lpVtbl->AddRef(m->locked_device);
            unlock_device(m);
        }
    } else {
        hr = E_NOINTERFACE;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static const IMFGetServiceVtbl get_service_vtbl = {
    get_service_QueryInterface, get_service_AddRef, get_service_Release, get_service_GetService,
};

/* ------------------------------------------------------------------ IMFVideoMixerControl2 */

DELEGATE_IUNKNOWN(control, IMFVideoMixerControl2, control)

/* the z order of a stream is its place in the list, the reference stream always first. */
static HRESULT STDMETHODCALLTYPE control_SetStreamZOrder(IMFVideoMixerControl2 *This, DWORD id, DWORD z)
{
    Mixer *m = IMPL(control);
    HRESULT hr = S_OK;
    DWORD position;
    EnterCriticalSection(&m->lock);
    if (z >= m->count) {
        hr = E_INVALIDARG;
    } else if (!id != !z) {
        hr = MF_E_INVALIDREQUEST;
    } else {
        MixerStream *s = find_stream(m, id, &position);
        if (!s) {
            hr = MF_E_INVALIDSTREAMNUMBER;
        } else {
            remove_at(m, position);
            memmove(&m->streams[z + 1], &m->streams[z], (m->count - z) * sizeof(*m->streams));
            m->streams[z] = s;
            m->count++;
        }
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE control_GetStreamZOrder(IMFVideoMixerControl2 *This, DWORD id, DWORD *z)
{
    Mixer *m = IMPL(control);
    HRESULT hr = S_OK;
    DWORD position;
    if (!z)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    if (find_stream(m, id, &position))
        *z = position;
    else
        hr = MF_E_INVALIDSTREAMNUMBER;
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE control_SetStreamOutputRect(IMFVideoMixerControl2 *This, DWORD id,
                                                             const MFVideoNormalizedRect *rect)
{
    Mixer *m = IMPL(control);
    HRESULT hr = S_OK;
    if (!rect)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    MixerStream *s = find_stream(m, id, nullptr);
    if (!s)
        hr = MF_E_INVALIDSTREAMNUMBER;
    else if (rect->left >= 0.0f && rect->top >= 0.0f && rect->right <= 1.0f && rect->bottom <= 1.0f &&
             rect->left <= rect->right && rect->top <= rect->bottom)
        s->rect = *rect;
    else
        hr = E_INVALIDARG;
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE control_GetStreamOutputRect(IMFVideoMixerControl2 *This, DWORD id,
                                                             MFVideoNormalizedRect *rect)
{
    Mixer *m = IMPL(control);
    HRESULT hr = S_OK;
    if (!rect)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    MixerStream *s = find_stream(m, id, nullptr);
    if (s)
        *rect = s->rect;
    else
        hr = MF_E_INVALIDSTREAMNUMBER;
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE control_SetMixingPrefs(IMFVideoMixerControl2 *This, DWORD prefs)
{
    Mixer *m = IMPL(control);
    if (prefs & ~0x1Fu)
        return E_INVALIDARG;
    EnterCriticalSection(&m->lock);
    m->prefs = prefs;
    LeaveCriticalSection(&m->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_GetMixingPrefs(IMFVideoMixerControl2 *This, DWORD *prefs)
{
    if (!prefs)
        return E_POINTER;
    *prefs = IMPL(control)->prefs;
    return S_OK;
}

static const IMFVideoMixerControl2Vtbl control_vtbl = {
    control_QueryInterface,      control_AddRef,         control_Release,
    control_SetStreamZOrder,     control_GetStreamZOrder, control_SetStreamOutputRect,
    control_GetStreamOutputRect, control_SetMixingPrefs,  control_GetMixingPrefs,
};

/* ------------------------------------------------------------------ IMFVideoPositionMapper */

DELEGATE_IUNKNOWN(mapper, IMFVideoPositionMapper, mapper)

/* windows compares the input stream with the number of streams and looks the stream up by the order it was added. */
static HRESULT STDMETHODCALLTYPE mapper_MapOutputCoordinateToInputStream(IMFVideoPositionMapper *This, float x,
                                                                         float y, DWORD out_id, DWORD in_id,
                                                                         float *in_x, float *in_y)
{
    Mixer *m = IMPL(mapper);
    HRESULT hr = S_OK;
    if (!in_x || !in_y)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    MixerStream *s = nullptr;
    if (out_id || in_id >= m->count) {
        hr = MF_E_INVALIDSTREAMNUMBER;
    } else if (m->state < 3) {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    } else {
        for (DWORD i = 0; i < m->count && !s; i++)
            if (m->streams[i]->index == in_id)
                s = m->streams[i];
        if (!s) {
            hr = MF_E_INVALIDSTREAMNUMBER;
        } else if (!s->format) {
            hr = MF_E_INVALIDREQUEST;
        } else {
            MFVideoNormalizedRect zoom = {0, 0, 1, 1};
            UINT32 size = 0;
            if (SUCCEEDED(m->transform_store->lpVtbl->GetBlob(m->transform_store, &VIDEO_ZOOM_RECT, (UINT8 *)&zoom,
                                                              sizeof(zoom), &size)) &&
                (size != 16 || zoom.left < 0 || zoom.right > 1 || zoom.top < 0 || zoom.bottom > 1 ||
                 zoom.left > zoom.right || zoom.top > zoom.bottom))
                zoom = (MFVideoNormalizedRect){0, 0, 1, 1};
            RECT src, dst;
            EvrSourceAndDestRects(s->format, m->output_format, &m->presentation, &zoom, &s->rect, &src, &dst);
            float w = (float)m->output_format->videoInfo.GeometricAperture.Area.cx;
            float h = (float)m->output_format->videoInfo.GeometricAperture.Area.cy;
            float sw = (float)(src.right - src.left), sh = (float)(src.bottom - src.top);
            *in_x = (x - (float)dst.left / w) * w / (float)(dst.right - dst.left) * sw / sw + (float)src.left / sw;
            *in_y = (y - (float)dst.top / h) * h / (float)(dst.bottom - dst.top) * sh / sh + (float)src.top / sh;
        }
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static const IMFVideoPositionMapperVtbl mapper_vtbl = {
    mapper_QueryInterface, mapper_AddRef, mapper_Release, mapper_MapOutputCoordinateToInputStream,
};

/* ------------------------------------------------------------------ IMFVideoProcessor */

DELEGATE_IUNKNOWN(processor, IMFVideoProcessor, processor)

static HRESULT STDMETHODCALLTYPE processor_GetAvailableVideoProcessorModes(IMFVideoProcessor *This, UINT *count,
                                                                           GUID **modes)
{
    Mixer *m = IMPL(processor);
    HRESULT hr = S_OK;
    if (!count)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    if (m->state < 3) {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    } else {
        if (modes) {
            *modes = CoTaskMemAlloc(m->mode_count * sizeof(GUID));
            if (!*modes)
                hr = E_OUTOFMEMORY;
            else
                memcpy(*modes, m->modes, m->mode_count * sizeof(GUID));
        }
        if (SUCCEEDED(hr))
            *count = m->mode_count;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE processor_GetVideoProcessorCaps(IMFVideoProcessor *This, GUID *mode,
                                                                 DXVA2_VideoProcessorCaps *caps)
{
    Mixer *m = IMPL(processor);
    HRESULT hr;
    EnterCriticalSection(&m->lock);
    MixerStream *ref = reference(m);
    if (m->state >= 3 && ref && ref->format) {
        DXVA2_VideoDesc desc;
        EvrVideoDesc(ref->format, &desc);
        hr = m->service->lpVtbl->GetVideoProcessorCaps(m->service, mode, &desc,
                                                       (D3DFORMAT)m->output_format->surfaceInfo.Format, caps);
    } else {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE processor_GetVideoProcessorMode(IMFVideoProcessor *This, GUID *mode)
{
    Mixer *m = IMPL(processor);
    HRESULT hr = S_OK;
    if (!mode)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    if (m->state < 3) {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    } else if (m->mode_set) {
        *mode = m->mode;
    } else {
        memset(mode, 0, sizeof(*mode));
        hr = S_FALSE;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE processor_SetVideoProcessorMode(IMFVideoProcessor *This, GUID *mode)
{
    Mixer *m = IMPL(processor);
    HRESULT hr;
    UINT n = 0, found = 0;
    D3DFORMAT *formats = nullptr, *targets = nullptr;
    if (!mode)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    MixerStream *ref = reference(m);
    if (m->state < 2 || !ref || !ref->format) {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
        goto done;
    }
    if (m->state >= 4) {
        hr = MF_E_INVALIDREQUEST;
        goto done;
    }
    DXVA2_VideoDesc desc;
    EvrVideoDesc(ref->format, &desc);
    hr = m->service->lpVtbl->GetVideoProcessorRenderTargets(m->service, mode, &desc, &n, &formats);
    if (FAILED(hr))
        goto done;
    if (!n) {
        hr = E_FAIL;
        goto done;
    }
    if (m->state >= 3) {
        if (format_listed(formats, n, (D3DFORMAT)m->output_format->surfaceInfo.Format)) {
            hr = allocate_worker(m, &desc, mode);
            if (FAILED(hr))
                goto done;
        } else {
            m->state = 2;
            HeapFree(GetProcessHeap(), 0, m->modes);
            HeapFree(GetProcessHeap(), 0, m->substream_formats);
            m->modes = nullptr;
            m->substream_formats = nullptr;
            m->mode_count = m->substream_format_count = 0;
            clear_output_type(m);
        }
        free_resources(m);
        hr = gather_targets(m, ref->format, mode, &found, &targets);
        if (FAILED(hr))
            goto done;
        HeapFree(GetProcessHeap(), 0, m->targets);
        m->targets = targets;
        m->target_count = found;
    }
    m->mode_set = TRUE;
    m->mode = *mode;
done:
    CoTaskMemFree(formats);
    update_state(m);
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE processor_GetProcAmpRange(IMFVideoProcessor *This, DWORD property,
                                                           DXVA2_ValueRange *range)
{
    Mixer *m = IMPL(processor);
    HRESULT hr;
    static const GUID NONE = {0};
    if (!range)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    MixerStream *ref = reference(m);
    if (m->state < 3 || !ref || !ref->format) {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    } else if (IsEqualGUID(&m->device, &NONE)) {
        hr = MF_E_INVALIDREQUEST;
    } else if (property != 1 && property != 2 && property != 4 && property != 8) {
        hr = E_INVALIDARG;
    } else {
        DXVA2_VideoDesc desc;
        EvrVideoDesc(ref->format, &desc);
        hr = m->service->lpVtbl->GetProcAmpRange(m->service, &m->device, &desc,
                                                 (D3DFORMAT)m->output_format->surfaceInfo.Format, property, range);
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE processor_GetProcAmpValues(IMFVideoProcessor *This, DWORD flags,
                                                            DXVA2_ProcAmpValues *values)
{
    Mixer *m = IMPL(processor);
    HRESULT hr = S_OK;
    if (!values)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    if (m->state < 3) {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    } else {
        DXVA2_Fixed32 *out = &values->Brightness;
        for (int i = 0; i < 4; i++)
            if (flags & (1u << i))
                out[i] = m->procamp[i];
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE processor_SetProcAmpValues(IMFVideoProcessor *This, DWORD flags,
                                                            DXVA2_ProcAmpValues *values)
{
    Mixer *m = IMPL(processor);
    HRESULT hr = S_OK;
    if (!values)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    DXVA2_Fixed32 *in = &values->Brightness;
    if (!flags || (flags & ~0xFu)) {
        hr = E_INVALIDARG;
    } else if (m->state < 3) {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    } else {
        for (int i = 0; i < 4; i++)
            if ((flags & (1u << i)) && !range_check(in[i], &m->procamp_ranges[i]))
                hr = E_INVALIDARG;
        for (int i = 0; SUCCEEDED(hr) && i < 4; i++) {
            if (!(flags & (1u << i)))
                continue;
            LONG v = in[i].ll;
            if (v > m->procamp_ranges[i].MaxValue.ll)
                v = m->procamp_ranges[i].MaxValue.ll;
            if (v < m->procamp_ranges[i].MinValue.ll)
                v = m->procamp_ranges[i].MinValue.ll;
            m->procamp[i].ll = v;
        }
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE processor_GetFilteringRange(IMFVideoProcessor *This, DWORD setting,
                                                             DXVA2_ValueRange *range)
{
    Mixer *m = IMPL(processor);
    HRESULT hr;
    static const GUID NONE = {0};
    if (!range)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    MixerStream *ref = reference(m);
    if (m->state < 3 || !ref || !ref->format) {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    } else if (IsEqualGUID(&m->device, &NONE)) {
        hr = MF_E_INVALIDREQUEST;
    } else {
        DXVA2_VideoDesc desc;
        EvrVideoDesc(ref->format, &desc);
        hr = m->service->lpVtbl->GetFilterPropertyRange(m->service, &m->device, &desc,
                                                        (D3DFORMAT)m->output_format->surfaceInfo.Format, setting,
                                                        range);
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE processor_GetFilteringValue(IMFVideoProcessor *This, DWORD setting,
                                                             DXVA2_Fixed32 *value)
{
    Mixer *m = IMPL(processor);
    HRESULT hr = S_OK;
    if (!value)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    if (m->state < 3)
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    else if (setting - 1 > 11)
        hr = E_INVALIDARG;
    else
        *value = m->filters[setting - 1];
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE processor_SetFilteringValue(IMFVideoProcessor *This, DWORD setting,
                                                             DXVA2_Fixed32 *value)
{
    Mixer *m = IMPL(processor);
    HRESULT hr = S_OK;
    if (!value)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    if (m->state < 3)
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    else if (setting - 1 > 11 || !range_check(*value, &m->filter_ranges[setting - 1]))
        hr = E_INVALIDARG;
    else
        m->filters[setting - 1] = *value;
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE processor_GetBackgroundColor(IMFVideoProcessor *This, COLORREF *color)
{
    Mixer *m = IMPL(processor);
    if (!color)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    *color = m->background;
    LeaveCriticalSection(&m->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE processor_SetBackgroundColor(IMFVideoProcessor *This, COLORREF color)
{
    Mixer *m = IMPL(processor);
    EnterCriticalSection(&m->lock);
    m->background = color;
    m->background_yuv = rgb_to_ycrcb(color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF);
    LeaveCriticalSection(&m->lock);
    return S_OK;
}

static const IMFVideoProcessorVtbl processor_vtbl = {
    processor_QueryInterface,       processor_AddRef,
    processor_Release,              processor_GetAvailableVideoProcessorModes,
    processor_GetVideoProcessorCaps, processor_GetVideoProcessorMode,
    processor_SetVideoProcessorMode, processor_GetProcAmpRange,
    processor_GetProcAmpValues,     processor_SetProcAmpValues,
    processor_GetFilteringRange,    processor_GetFilteringValue,
    processor_SetFilteringValue,    processor_GetBackgroundColor,
    processor_SetBackgroundColor,
};

/* ------------------------------------------------------------------ IMFVideoMixerBitmap */

DELEGATE_IUNKNOWN(bitmap, IMFVideoMixerBitmap, bitmap)

static BOOL valid_rect(const RECT *r, int width, int height)
{
    return r->left >= 0 && r->top >= 0 && r->right >= r->left && r->bottom >= r->top &&
           (width == -1 || r->right - r->left <= width) && (height == -1 || r->bottom - r->top <= height);
}

/* evr's SetAppImageColorKey: pixels of the key colour become transparent, every other one opaque. */
static void color_key(DWORD width, DWORD height, const UINT32 *in, UINT32 *out, COLORREF key)
{
    for (DWORD i = 0; i < width * height; i++) {
        if (key & 0xFF000000) {
            out[i] = in[i] | 0xFF000000;
        } else {
            UINT32 p = in[i] & 0xFFFFFF;
            out[i] = p != key ? p | 0xFF000000 : p;
        }
    }
}

static HRESULT validate_bitmap(const MFVideoAlphaBitmap *b)
{
    if (!b)
        return E_POINTER;
    if (b->params.dwFlags & ~0x3Fu)
        return E_INVALIDARG;
    if ((b->params.dwFlags & MFVideoAlphaBitmap_FilterMode) && b->params.dwFilterMode > 7)
        return E_INVALIDARG;
    if (!b->GetBitmapFromDC) {
        if (!b->bitmap.pDDS)
            return E_INVALIDARG;
        if (!(b->params.dwFlags & MFVideoAlphaBitmap_EntireDDS) && !valid_rect(&b->params.rcSrc, -1, -1))
            return E_INVALIDARG;
    } else if ((b->params.dwFlags & MFVideoAlphaBitmap_EntireDDS) || !b->bitmap.hdc ||
               !valid_rect(&b->params.rcSrc, -1, -1)) {
        return E_INVALIDARG;
    }
    if ((b->params.dwFlags & MFVideoAlphaBitmap_Alpha) && !(b->params.fAlpha >= 0.0f && b->params.fAlpha <= 1.0f))
        return E_INVALIDARG;
    return S_OK;
}

/* evr's IsTextureFilterModeSupported. */
static BOOL filter_supported(const D3DCAPS9 *caps, DWORD mode)
{
    static const DWORD NEEDS[8] = {0, 0x1000100, 0x2000200, 0x4000400, 0, 0, 0x8000800, 0x10001000};
    return mode < 8 && NEEDS[mode] && (caps->TextureFilterCaps & NEEDS[mode]) == NEEDS[mode];
}

/* the bitmap is copied out at once, bottom row first, as windows keeps it. */
static HRESULT STDMETHODCALLTYPE bitmap_SetAlphaBitmap(IMFVideoMixerBitmap *This, const MFVideoAlphaBitmap *b)
{
    Mixer *m = IMPL(bitmap);
    HRESULT hr;
    IDirect3DSurface9 *surface = nullptr;
    HDC source = nullptr, memory = nullptr;
    HBITMAP section = nullptr;
    UINT32 *copy = nullptr;
    BOOL locked = FALSE;
    DWORD kind = 4, filter;
    RECT rc = {0};
    EnterCriticalSection(&m->lock);
    hr = validate_bitmap(b);
    if (FAILED(hr))
        goto done;
    hr = lock_device(m);
    if (FAILED(hr))
        goto done;
    locked = TRUE;
    filter = m->app_filter;
    if (!b->GetBitmapFromDC) {
        D3DSURFACE_DESC desc;
        hr = ((IUnknown *)b->bitmap.pDDS)->lpVtbl->QueryInterface((IUnknown *)b->bitmap.pDDS, &IID_IDirect3DSurface9,
                                                                 (void **)&surface);
        if (SUCCEEDED(hr))
            hr = surface->lpVtbl->GetDesc(surface, &desc);
        if (FAILED(hr))
            goto done;
        if (desc.Pool != D3DPOOL_SYSTEMMEM || (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8)) {
            hr = E_INVALIDARG;
            goto done;
        }
        if (desc.Format == D3DFMT_A8R8G8B8) {
            if (b->params.dwFlags & MFVideoAlphaBitmap_SrcColorKey) {
                hr = E_INVALIDARG;
                goto done;
            }
            kind = 1;
        } else {
            kind = 2;
        }
        if (b->params.dwFlags & MFVideoAlphaBitmap_EntireDDS) {
            rc.right = desc.Width;
            rc.bottom = desc.Height;
        } else if (valid_rect(&b->params.rcSrc, desc.Width, desc.Height)) {
            rc = b->params.rcSrc;
        } else {
            hr = E_INVALIDARG;
            goto done;
        }
    } else {
        rc = b->params.rcSrc;
    }
    if (b->params.dwFlags & MFVideoAlphaBitmap_FilterMode) {
        if (!filter_supported(&m->device_caps, b->params.dwFilterMode)) {
            hr = E_INVALIDARG;
            goto done;
        }
        filter = b->params.dwFilterMode;
    }
    DWORD width = rc.right - rc.left, height = rc.bottom - rc.top;
    if (!width || !height || rc.right <= rc.left || rc.bottom <= rc.top || width > m->device_caps.MaxTextureWidth ||
        height > m->device_caps.MaxTextureHeight) {
        hr = E_INVALIDARG;
        goto done;
    }
    copy = HeapAlloc(GetProcessHeap(), 0, 4 * width * height);
    if (!copy) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (kind == 1) {
        D3DLOCKED_RECT lr;
        hr = surface->lpVtbl->LockRect(surface, &lr, nullptr, D3DLOCK_READONLY);
        if (SUCCEEDED(hr)) {
            const BYTE *row = (const BYTE *)lr.pBits + rc.top * lr.Pitch + 4 * rc.left;
            for (DWORD y = 0; y < height; y++, row += lr.Pitch)
                memcpy(copy + (height - 1 - y) * width, row, 4 * width);
            surface->lpVtbl->UnlockRect(surface);
        }
        goto done;
    }
    if (kind == 4) {
        source = b->bitmap.hdc;
    } else {
        hr = surface->lpVtbl->GetDC(surface, &source);
        if (FAILED(hr))
            goto done;
    }
    memory = CreateCompatibleDC(nullptr);
    if (!memory) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = nullptr;
    section = CreateDIBSection(memory, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!section) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    HGDIOBJ old = SelectObject(memory, section);
    if (!BitBlt(memory, 0, 0, width, height, source, rc.left, rc.top, SRCCOPY))
        hr = MF_E_UNEXPECTED;
    SelectObject(memory, old);
    if (SUCCEEDED(hr))
        color_key(width, height, bits, copy,
                  b->params.dwFlags & MFVideoAlphaBitmap_SrcColorKey ? b->params.clrSrcKey & 0xFFFFFF : 0xFFFFFFFF);
done:
    if (locked)
        unlock_device(m);
    if (section)
        DeleteObject(section);
    if (source && kind == 2)
        surface->lpVtbl->ReleaseDC(surface, source);
    if (memory)
        DeleteDC(memory);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, copy);
    } else {
        HeapFree(GetProcessHeap(), 0, m->app_image);
        m->app_image = copy;
        m->app_key = b->params.dwFlags & MFVideoAlphaBitmap_SrcColorKey ? b->params.clrSrcKey : 0xFFFFFFFF;
        if (b->params.dwFlags & MFVideoAlphaBitmap_DestRect)
            m->app_dest = b->params.nrcDest;
        else
            m->app_dest = (MFVideoNormalizedRect){0, 0, 1, 1};
        m->app_alpha = b->params.dwFlags & MFVideoAlphaBitmap_Alpha ? b->params.fAlpha : 1.0f;
        m->app_src = rc;
        m->app_width = rc.right - rc.left;
        m->app_height = rc.bottom - rc.top;
        m->app_filter = filter;
        m->app_kind = kind;
    }
    if (surface)
        surface->lpVtbl->Release(surface);
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bitmap_ClearAlphaBitmap(IMFVideoMixerBitmap *This)
{
    Mixer *m = IMPL(bitmap);
    HRESULT hr = S_OK;
    EnterCriticalSection(&m->lock);
    if (m->app_image) {
        HeapFree(GetProcessHeap(), 0, m->app_image);
        m->app_image = nullptr;
    } else {
        hr = MF_E_NOT_INITIALIZED;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bitmap_UpdateAlphaBitmapParameters(IMFVideoMixerBitmap *This,
                                                                    const MFVideoAlphaBitmapParams *p)
{
    Mixer *m = IMPL(bitmap);
    HRESULT hr = S_OK;
    if (!p)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    if (!m->app_image) {
        hr = MF_E_NOT_INITIALIZED;
        goto done;
    }
    DWORD filter = m->app_filter;
    RECT src = m->app_src;
    if (p->dwFlags & MFVideoAlphaBitmap_SrcRect) {
        if (!valid_rect(&p->rcSrc, m->app_width, m->app_height)) {
            hr = E_INVALIDARG;
            goto done;
        }
        src = p->rcSrc;
    }
    if (p->dwFlags & MFVideoAlphaBitmap_FilterMode) {
        filter = p->dwFilterMode;
        if (filter > 7) {
            hr = E_INVALIDARG;
            goto done;
        }
    }
    if (!(p->dwFlags & MFVideoAlphaBitmap_SrcColorKey)) {
        BOOL changed = m->app_key != 0xFFFFFFFF;
        m->app_key = 0xFFFFFFFF;
        if (changed)
            color_key(m->app_width, m->app_height, m->app_image, m->app_image, 0xFFFFFFFF);
    } else if (m->app_kind == 1) {
        hr = E_INVALIDARG;
        goto done;
    } else if (m->app_key != p->clrSrcKey) {
        m->app_key = p->clrSrcKey;
        color_key(m->app_width, m->app_height, m->app_image, m->app_image, p->clrSrcKey & 0xFFFFFF);
    }
    if (p->dwFlags & MFVideoAlphaBitmap_DestRect)
        m->app_dest = p->nrcDest;
    if (p->dwFlags & MFVideoAlphaBitmap_Alpha)
        m->app_alpha = p->fAlpha;
    m->app_filter = filter;
    m->app_src = src;
done:
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bitmap_GetAlphaBitmapParameters(IMFVideoMixerBitmap *This,
                                                                 MFVideoAlphaBitmapParams *p)
{
    Mixer *m = IMPL(bitmap);
    HRESULT hr = S_OK;
    if (!p)
        return E_POINTER;
    EnterCriticalSection(&m->lock);
    if (m->app_image) {
        memset(p, 0, sizeof(*p));
        p->rcSrc = m->app_src;
        p->nrcDest = m->app_dest;
        p->fAlpha = m->app_alpha;
        p->clrSrcKey = m->app_key;
        p->dwFilterMode = m->app_filter;
    } else {
        hr = MF_E_NOT_INITIALIZED;
    }
    LeaveCriticalSection(&m->lock);
    return hr;
}

/*
 * the bitmap laid over the output after each blit: its rectangle of the stored copy, scaled with the nearest pixel
 * onto its place in the output, weighted by its own alpha and the overall alpha.
 */
static void blend_app_image(Mixer *m, IDirect3DSurface9 *target)
{
    if (!m->app_image)
        return;
    D3DSURFACE_DESC desc;
    D3DLOCKED_RECT lr;
    if (FAILED(target->lpVtbl->GetDesc(target, &desc)) ||
        (desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8))
        return;
    LONG l = (LONG)(m->app_dest.left * desc.Width + 0.5f), t = (LONG)(m->app_dest.top * desc.Height + 0.5f);
    LONG r = (LONG)(m->app_dest.right * desc.Width + 0.5f), b = (LONG)(m->app_dest.bottom * desc.Height + 0.5f);
    LONG sw = m->app_src.right - m->app_src.left, sh = m->app_src.bottom - m->app_src.top;
    if (r <= l || b <= t || sw <= 0 || sh <= 0)
        return;
    if (FAILED(target->lpVtbl->LockRect(target, &lr, nullptr, 0)))
        return;
    UINT alpha = (UINT)(m->app_alpha * 255.0f + 0.5f);
    for (LONG y = t; y < b; y++) {
        UINT32 *row = (UINT32 *)((BYTE *)lr.pBits + y * lr.Pitch);
        LONG sy = m->app_src.top + (y - t) * sh / (b - t);
        const UINT32 *src = m->app_image + (m->app_height - 1 - sy) * m->app_width;
        for (LONG x = l; x < r; x++) {
            UINT32 p = src[m->app_src.left + (x - l) * sw / (r - l)];
            UINT a = (p >> 24) * alpha / 255;
            UINT32 d = row[x];
            UINT32 out = 0xFF000000;
            for (int c = 0; c < 24; c += 8) {
                UINT s = (p >> c) & 0xFF, o = (d >> c) & 0xFF;
                out |= ((s * a + o * (255 - a) + 127) / 255) << c;
            }
            row[x] = out;
        }
    }
    target->lpVtbl->UnlockRect(target);
}

static const IMFVideoMixerBitmapVtbl bitmap_vtbl = {
    bitmap_QueryInterface,       bitmap_AddRef,
    bitmap_Release,              bitmap_SetAlphaBitmap,
    bitmap_ClearAlphaBitmap,     bitmap_UpdateAlphaBitmapParameters,
    bitmap_GetAlphaBitmapParameters,
};

/* ------------------------------------------------------------------ IMFQualityAdvise */

DELEGATE_IUNKNOWN(quality, IMFQualityAdvise, quality)

static HRESULT STDMETHODCALLTYPE quality_SetDropMode(IMFQualityAdvise *This, MF_QUALITY_DROP_MODE mode)
{
    Mixer *m = IMPL(quality);
    HRESULT hr = S_OK;
    EnterCriticalSection(&m->lock);
    if (mode == MF_DROP_MODE_NONE || (mode == MF_DROP_MODE_1 && (m->prefs & 2)))
        m->drop_mode = mode;
    else
        hr = MF_E_DROPTIME_NOT_SUPPORTED_;
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE quality_SetQualityLevel(IMFQualityAdvise *This, MF_QUALITY_LEVEL level)
{
    Mixer *m = IMPL(quality);
    HRESULT hr = S_OK;
    EnterCriticalSection(&m->lock);
    if (level == MF_QUALITY_NORMAL || (level == MF_QUALITY_NORMAL_MINUS_1 && (m->prefs & 4)))
        m->quality_level = level;
    else
        hr = MF_E_QUALITYLEVEL_NOT_SUPPORTED_;
    LeaveCriticalSection(&m->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE quality_GetDropMode(IMFQualityAdvise *This, MF_QUALITY_DROP_MODE *mode)
{
    if (!mode)
        return E_POINTER;
    *mode = IMPL(quality)->drop_mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE quality_GetQualityLevel(IMFQualityAdvise *This, MF_QUALITY_LEVEL *level)
{
    if (!level)
        return E_POINTER;
    *level = IMPL(quality)->quality_level;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE quality_DropTime(IMFQualityAdvise *This, LONGLONG amount)
{
    Mixer *m = IMPL(quality);
    EnterCriticalSection(&m->lock);
    m->drop_time = amount;
    LeaveCriticalSection(&m->lock);
    return S_OK;
}

static const IMFQualityAdviseVtbl quality_vtbl = {
    quality_QueryInterface, quality_AddRef,          quality_Release,         quality_SetDropMode,
    quality_SetQualityLevel, quality_GetDropMode,    quality_GetQualityLevel, quality_DropTime,
};

/* ------------------------------------------------------------------ IMFClockStateSink */

DELEGATE_IUNKNOWN(clock, IMFClockStateSink, clock)

static HRESULT STDMETHODCALLTYPE clock_OnClockStart(IMFClockStateSink *This, MFTIME system, LONGLONG offset)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE clock_OnClockOther(IMFClockStateSink *This, MFTIME system)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE clock_OnClockSetRate(IMFClockStateSink *This, MFTIME system, float rate)
{
    Mixer *m = IMPL(clock);
    EnterCriticalSection(&m->lock);
    m->rate = rate;
    LeaveCriticalSection(&m->lock);
    return S_OK;
}

static const IMFClockStateSinkVtbl clock_vtbl = {
    clock_QueryInterface, clock_AddRef,       clock_Release,      clock_OnClockStart,
    clock_OnClockOther,   clock_OnClockOther, clock_OnClockOther, clock_OnClockSetRate,
};

/* ------------------------------------------------------------------ IEVRTrustedVideoPlugin */

DELEGATE_IUNKNOWN(trusted, IEVRTrustedVideoPlugin, trusted)

static HRESULT STDMETHODCALLTYPE trusted_IsInTrustedVideoMode(IEVRTrustedVideoPlugin *This, BOOL *trusted)
{
    if (!trusted)
        return E_POINTER;
    *trusted = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE trusted_CanConstrict(IEVRTrustedVideoPlugin *This, BOOL *can)
{
    Mixer *m = IMPL(trusted);
    if (m->state < 3)
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    if (!can)
        return E_POINTER;
    *can = (m->caps.VideoProcessorOperations & DXVA2_VideoProcess_Constriction) != 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE trusted_SetConstriction(IEVRTrustedVideoPlugin *This, DWORD kpixels)
{
    IMPL(trusted)->constriction = kpixels;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE trusted_DisableImageExport(IEVRTrustedVideoPlugin *This, BOOL disable)
{
    return S_OK;
}

static const IEVRTrustedVideoPluginVtbl trusted_vtbl = {
    trusted_QueryInterface,  trusted_AddRef,          trusted_Release,
    trusted_IsInTrustedVideoMode, trusted_CanConstrict, trusted_SetConstriction,
    trusted_DisableImageExport,
};

/* ------------------------------------------------------------------ IDirect3DDeviceManager9 */

DELEGATE_IUNKNOWN(manager, IDirect3DDeviceManager9, manager_iface)

/* each call goes to the manager the mixer was given, outside the mixer's lock. */
static IDirect3DDeviceManager9 *current_manager(Mixer *m)
{
    EnterCriticalSection(&m->lock);
    IDirect3DDeviceManager9 *manager = m->manager;
    if (manager)
        manager->lpVtbl->AddRef(manager);
    LeaveCriticalSection(&m->lock);
    return manager;
}

#define FORWARD_MANAGER(call)                                                                                          \
    IDirect3DDeviceManager9 *manager = current_manager(IMPL(manager_iface));                                          \
    if (!manager)                                                                                                      \
        return MF_E_UNSUPPORTED_SERVICE;                                                                               \
    HRESULT hr = manager->lpVtbl->call;                                                                                \
    manager->lpVtbl->Release(manager);                                                                                 \
    return hr;

static HRESULT STDMETHODCALLTYPE manager_ResetDevice(IDirect3DDeviceManager9 *This, IDirect3DDevice9 *device,
                                                     UINT token)
{
    FORWARD_MANAGER(ResetDevice(manager, device, token))
}

static HRESULT STDMETHODCALLTYPE manager_OpenDeviceHandle(IDirect3DDeviceManager9 *This, HANDLE *handle)
{
    FORWARD_MANAGER(OpenDeviceHandle(manager, handle))
}

static HRESULT STDMETHODCALLTYPE manager_CloseDeviceHandle(IDirect3DDeviceManager9 *This, HANDLE handle)
{
    FORWARD_MANAGER(CloseDeviceHandle(manager, handle))
}

static HRESULT STDMETHODCALLTYPE manager_TestDevice(IDirect3DDeviceManager9 *This, HANDLE handle)
{
    FORWARD_MANAGER(TestDevice(manager, handle))
}

static HRESULT STDMETHODCALLTYPE manager_LockDevice(IDirect3DDeviceManager9 *This, HANDLE handle,
                                                    IDirect3DDevice9 **device, BOOL block)
{
    FORWARD_MANAGER(LockDevice(manager, handle, device, block))
}

static HRESULT STDMETHODCALLTYPE manager_UnlockDevice(IDirect3DDeviceManager9 *This, HANDLE handle, BOOL save)
{
    FORWARD_MANAGER(UnlockDevice(manager, handle, save))
}

static HRESULT STDMETHODCALLTYPE manager_GetVideoService(IDirect3DDeviceManager9 *This, HANDLE handle, REFIID riid,
                                                         void **service)
{
    FORWARD_MANAGER(GetVideoService(manager, handle, riid, service))
}

static const IDirect3DDeviceManager9Vtbl manager_vtbl = {
    manager_QueryInterface,   manager_AddRef,      manager_Release,      manager_ResetDevice,
    manager_OpenDeviceHandle, manager_CloseDeviceHandle, manager_TestDevice, manager_LockDevice,
    manager_UnlockDevice,     manager_GetVideoService,
};

/* ------------------------------------------------------------------ creation */

HRESULT EvrCreateMixer(IUnknown *outer, REFIID riid, void **out)
{
    Mixer *m = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*m));
    if (!m)
        return E_OUTOFMEMORY;
    m->inner.lpVtbl = &inner_vtbl;
    m->transform.lpVtbl = &transform_vtbl;
    m->device_id.lpVtbl = &device_id_vtbl;
    m->lookup.lpVtbl = &lookup_vtbl;
    m->get_service.lpVtbl = &get_service_vtbl;
    m->control.lpVtbl = &control_vtbl;
    m->mapper.lpVtbl = &mapper_vtbl;
    m->processor.lpVtbl = &processor_vtbl;
    m->bitmap.lpVtbl = &bitmap_vtbl;
    m->quality.lpVtbl = &quality_vtbl;
    m->clock.lpVtbl = &clock_vtbl;
    m->trusted.lpVtbl = &trusted_vtbl;
    m->manager_iface.lpVtbl = &manager_vtbl;
    m->attributes.lpVtbl = &attributes_vtbl;
    m->outer = outer ? outer : &m->inner;
    m->refs = 1;
    m->max_streams = MAX_STREAMS;
    m->constriction = (DWORD)-1;
    m->app_filter = D3DTEXF_POINT;
    m->rate = 1.0f;
    m->background_yuv = rgb_to_ycrcb(0, 0, 0);
    InitializeCriticalSection(&m->lock);
    EvrObjectCreated();
    MFVideoNormalizedRect full = {0, 0, 1, 1};
    HRESULT hr = add_stream(m, 0, TRUE);
    if (SUCCEEDED(hr))
        hr = MFCreateAttributes(&m->transform_store, 1);
    if (SUCCEEDED(hr))
        hr = m->transform_store->lpVtbl->SetBlob(m->transform_store, &VIDEO_ZOOM_RECT, (const UINT8 *)&full,
                                                 sizeof(full));
    if (SUCCEEDED(hr))
        hr = MFCreateAttributes(&m->store, 1);
    if (SUCCEEDED(hr))
        hr = m->store->lpVtbl->SetUINT32(m->store, &MF_SA_D3D_AWARE, 1);
    if (SUCCEEDED(hr))
        hr = query(m, riid, out);
    m->inner.lpVtbl->Release(&m->inner);
    return hr;
}
