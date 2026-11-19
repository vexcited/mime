#include "evr_private.h"

#define POOL 3
#define MF_E_UNSUPPORTED_RATE_ ((HRESULT)0xC00D36D0)
#define EC_COMPLETE 0x01
#define MF_E_LICENSE_INCORRECT_RIGHTS_ ((HRESULT)0xC00D7148)

typedef struct {
    IMFSample *sample;
    IDirect3DSurface9 *surface;
    BOOL busy;
} Slot;

typedef struct {
    IUnknown inner;
    IMFVideoDeviceID device_id;
    IMFVideoPresenter presenter;
    IMFVideoDisplayControl display;
    IMFRateSupport rate;
    IMFGetService get_service;
    IMFTopologyServiceLookupClient lookup;
    IMFVideoPositionMapper mapper;
    IQualProp qualprop;
    IEVRTrustedVideoPlugin trusted;
    IMFQualityAdvise quality;
    IMFQualityAdviseLimits limits;
    IDirect3DDeviceManager9 manager_iface;
    IPresenterStatistics statistics;
    IUnknown *outer;
    LONG refs;
    BOOL flip;
    CRITICAL_SECTION lock;

    IDirect3D9Ex *d3d;
    IDirect3DDevice9Ex *device;
    IDirect3DDeviceManager9 *manager;
    IEVRTearlessWindowPresenter9 *helper;
    UINT token;
    HWND focus;

    IMFClock *clock;
    IMFTransform *mixer;
    IMediaEventSink *sink;
    DWORD lookup_state;

    HWND window;
    IDirect3DSwapChain9 *chain;
    UINT chain_width, chain_height;
    MFVideoNormalizedRect src;
    RECT dst;
    DWORD aspect, prefs;
    COLORREF border;
    BOOL fullscreen;

    IUnknown *mixer_type, *type;
    SIZE native;
    MFRatio par;
    LONGLONG frame;
    float rate_now;
    MF_QUALITY_DROP_MODE drop_mode;
    MF_QUALITY_LEVEL quality_level;

    Slot slots[POOL];
    BOOL streaming, has_input, end_of_stream, prerolled, no_export;
    DWORD presents;
    DWORD state;
    LONG constriction;
    IMFSample *queue[POOL];
    DWORD queued;
    IDirect3DSurface9 *last;
    LONGLONG last_time;
    HANDLE thread, wake;
    BOOL quit;
} Presenter;

/* evr's render states. */
enum { RENDER_STARTED, RENDER_STOPPED, RENDER_PAUSED };

#define IMPL(iface) CONTAINING_RECORD(This, Presenter, iface)

static HRESULT renegotiate(Presenter *p);
static HRESULT process_output_loop(Presenter *p);

/* ------------------------------------------------------------------ the device */

static LRESULT CALLBACK focus_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(window, message, wparam, lparam);
}

/*
 * like evr, a Direct3D 9Ex device on a window of its own with an 8 by 8 back buffer, made with the presenter; the
 * pictures go to a swap chain of the video window.
 */
static HRESULT make_device(Presenter *p)
{
    static ATOM atom;
    if (!atom) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = focus_proc;
        wc.hInstance = GetModuleHandleW(L"evr.dll");
        wc.lpszClassName = L"EVR Presenter Window";
        atom = RegisterClassW(&wc);
    }
    p->focus = CreateWindowExW(0, L"EVR Presenter Window", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                               GetModuleHandleW(L"evr.dll"), nullptr);
    if (!p->focus)
        return HRESULT_FROM_WIN32(GetLastError());
    HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &p->d3d);
    if (FAILED(hr))
        return hr;
    D3DPRESENT_PARAMETERS pp = {0};
    pp.BackBufferWidth = 8;
    pp.BackBufferHeight = 8;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_COPY;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = p->focus;
    pp.Flags = D3DPRESENTFLAG_VIDEO;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    hr = p->d3d->lpVtbl->CreateDeviceEx(p->d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, p->focus,
                                        D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED |
                                            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES |
                                            D3DCREATE_DISABLE_PSGP_THREADING | D3DCREATE_ENABLE_PRESENTSTATS,
                                        &pp, nullptr, &p->device);
    if (FAILED(hr))
        return hr;
    hr = DXVA2CreateDirect3DDeviceManager9(&p->token, &p->manager);
    if (SUCCEEDED(hr))
        hr = p->manager->lpVtbl->ResetDevice(p->manager, (IDirect3DDevice9 *)p->device, p->token);
    return hr;
}

static void free_slots(Presenter *p)
{
    for (int i = 0; i < POOL; i++) {
        if (p->slots[i].sample)
            p->slots[i].sample->lpVtbl->Release(p->slots[i].sample);
        if (p->slots[i].surface)
            p->slots[i].surface->lpVtbl->Release(p->slots[i].surface);
        memset(&p->slots[i], 0, sizeof(p->slots[i]));
    }
    for (DWORD i = 0; i < p->queued; i++)
        p->queue[i]->lpVtbl->Release(p->queue[i]);
    p->queued = 0;
}

/* the samples the mixer writes into: render targets of the media type's size. */
static HRESULT make_slots(Presenter *p, UINT width, UINT height, D3DFORMAT format)
{
    free_slots(p);
    for (int i = 0; i < POOL; i++) {
        HRESULT hr = p->device->lpVtbl->CreateRenderTarget(p->device, width, height, format, D3DMULTISAMPLE_NONE, 0,
                                                           FALSE, &p->slots[i].surface, nullptr);
        if (SUCCEEDED(hr))
            hr = EvrCreateVideoSample((IUnknown *)p->slots[i].surface, &p->slots[i].sample);
        if (FAILED(hr)) {
            free_slots(p);
            return hr;
        }
    }
    return S_OK;
}

/* the swap chain of the video window follows its client area. */
static HRESULT window_chain(Presenter *p)
{
    RECT client;
    if (!p->window || !GetClientRect(p->window, &client) || client.right <= 0 || client.bottom <= 0)
        return MF_E_INVALIDREQUEST;
    if (p->chain && p->chain_width == (UINT)client.right && p->chain_height == (UINT)client.bottom)
        return S_OK;
    if (p->chain) {
        p->chain->lpVtbl->Release(p->chain);
        p->chain = nullptr;
    }
    D3DPRESENT_PARAMETERS pp = {0};
    pp.BackBufferWidth = client.right;
    pp.BackBufferHeight = client.bottom;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_COPY;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = p->window;
    pp.Flags = D3DPRESENTFLAG_VIDEO;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT hr = p->device->lpVtbl->CreateAdditionalSwapChain(p->device, &pp, &p->chain);
    if (SUCCEEDED(hr)) {
        p->chain_width = client.right;
        p->chain_height = client.bottom;
    }
    return hr;
}

/* a picture into the destination rectangle of the window, the rest of the client area in the border colour. */
static HRESULT present_surface(Presenter *p, IDirect3DSurface9 *surface)
{
    if (IsRectEmpty(&p->dst))
        return S_OK;
    HRESULT hr = window_chain(p);
    if (FAILED(hr))
        return hr;
    IDirect3DSurface9 *back = nullptr;
    hr = p->chain->lpVtbl->GetBackBuffer(p->chain, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    if (FAILED(hr))
        return hr;
    D3DCOLOR border = 0xFF000000 | (p->border & 0xFF) << 16 | (p->border & 0xFF00) | (p->border >> 16 & 0xFF);
    if (!(p->prefs & MFVideoRenderPrefs_DoNotRenderBorder))
        p->device->lpVtbl->ColorFill(p->device, back, nullptr, border);
    RECT dst = p->dst;
    if (dst.right > (LONG)p->chain_width)
        dst.right = p->chain_width;
    if (dst.bottom > (LONG)p->chain_height)
        dst.bottom = p->chain_height;
    D3DSURFACE_DESC desc;
    surface->lpVtbl->GetDesc(surface, &desc);
    RECT src = {0, 0, (LONG)desc.Width, (LONG)desc.Height};
    if (!IsRectEmpty(&dst))
        hr = p->device->lpVtbl->StretchRect(p->device, surface, &src, back, &dst, D3DTEXF_POINT);
    back->lpVtbl->Release(back);
    if (SUCCEEDED(hr))
        hr = p->helper->lpVtbl->Present(p->helper, p->chain, p->sink, nullptr, nullptr, p->window, 0, ~0u, TRUE,
                                        nullptr);
    if (SUCCEEDED(hr))
        p->presents++;
    return hr;
}

static void show(Presenter *p, Slot *slot)
{
    present_surface(p, slot->surface);
    if (p->last)
        p->last->lpVtbl->Release(p->last);
    p->last = slot->surface;
    p->last->lpVtbl->AddRef(p->last);
    LONGLONG time = 0;
    slot->sample->lpVtbl->GetSampleTime(slot->sample, &time);
    p->last_time = time;
    slot->busy = FALSE;
}

static Slot *slot_of(Presenter *p, IMFSample *sample)
{
    for (int i = 0; i < POOL; i++)
        if (p->slots[i].sample == sample)
            return &p->slots[i];
    return nullptr;
}

/* the presentation time of the clock, in 100 ns. */
static LONGLONG clock_time(Presenter *p)
{
    LONGLONG now = 0;
    MFTIME system;
    if (!p->clock || FAILED(p->clock->lpVtbl->GetCorrelatedTime(p->clock, 0, &now, &system)))
        return 0;
    return now;
}

static void notify(Presenter *p, LONG code, LONG_PTR a, LONG_PTR b)
{
    if (p->sink)
        p->sink->lpVtbl->Notify(p->sink, code, a, b);
}

/* evr's CheckForEndOfStream: complete once the mixer is dry and every picture is out. */
static void check_end_of_stream(Presenter *p)
{
    if (p->end_of_stream && !p->has_input && !p->queued) {
        p->end_of_stream = FALSE;
        notify(p, EC_COMPLETE, S_OK, 0);
    }
}

static void flush(Presenter *p)
{
    for (DWORD i = 0; i < p->queued; i++) {
        Slot *slot = slot_of(p, p->queue[i]);
        if (slot)
            slot->busy = FALSE;
        p->queue[i]->lpVtbl->Release(p->queue[i]);
    }
    p->queued = 0;
}

/* pictures whose time has not come wait here while the clock runs. */
static DWORD WINAPI scheduler(void *param)
{
    Presenter *p = param;
    for (;;) {
        DWORD wait = INFINITE;
        EnterCriticalSection(&p->lock);
        if (p->quit) {
            LeaveCriticalSection(&p->lock);
            return 0;
        }
        while (p->queued && p->state == RENDER_STARTED) {
            IMFSample *sample = p->queue[0];
            LONGLONG time = 0;
            if (p->clock && SUCCEEDED(sample->lpVtbl->GetSampleTime(sample, &time))) {
                LONGLONG ahead = time - clock_time(p);
                float rate = p->rate_now > 0 ? p->rate_now : 1.0f;
                if (ahead > 10000) {
                    wait = (DWORD)(ahead / 10000 / rate);
                    break;
                }
            }
            memmove(p->queue, p->queue + 1, --p->queued * sizeof(*p->queue));
            Slot *slot = slot_of(p, sample);
            if (slot)
                show(p, slot);
            sample->lpVtbl->Release(sample);
            process_output_loop(p);
        }
        LeaveCriticalSection(&p->lock);
        WaitForSingleObject(p->wake, wait);
    }
}

static void clear_type(Presenter *p)
{
    flush(p);
    free_slots(p);
    if (p->type)
        p->type->lpVtbl->Release(p->type);
    if (p->mixer_type)
        p->mixer_type->lpVtbl->Release(p->mixer_type);
    p->type = nullptr;
    p->mixer_type = nullptr;
}

/*
 * evr's ProcessOutput: one free sample filled by the mixer and handed on. a stopped or paused presenter shows one
 * picture, the preroll. without a clock a picture shows at once.
 */
static HRESULT process_output(Presenter *p)
{
    if (p->state != RENDER_STARTED && p->prerolled)
        return S_FALSE;
    if (!p->mixer)
        return MF_E_INVALIDREQUEST;
    Slot *slot = nullptr;
    for (int i = 0; i < POOL && !slot; i++)
        if (!p->slots[i].busy && p->slots[i].sample)
            slot = &p->slots[i];
    if (!slot)
        return S_FALSE;
    IMFDesiredSample *desired = nullptr;
    if (SUCCEEDED(slot->sample->lpVtbl->QueryInterface(slot->sample, &IID_IMFDesiredSample, (void **)&desired))) {
        desired->lpVtbl->Clear(desired);
        desired->lpVtbl->Release(desired);
    }
    MFT_OUTPUT_DATA_BUFFER buffer = {0, slot->sample, 0, nullptr};
    DWORD status = 0;
    HRESULT hr = p->mixer->lpVtbl->ProcessOutput(p->mixer, 0, 1, &buffer, &status);
    if (buffer.pEvents)
        buffer.pEvents->lpVtbl->Release(buffer.pEvents);
    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        clear_type(p);
        return hr;
    }
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        p->has_input = FALSE;
    if (FAILED(hr))
        return hr;
    p->prerolled = TRUE;
    LONGLONG time = 0;
    if (p->clock && p->state == RENDER_STARTED &&
        SUCCEEDED(slot->sample->lpVtbl->GetSampleTime(slot->sample, &time)) && time - clock_time(p) > 10000) {
        slot->busy = TRUE;
        slot->sample->lpVtbl->AddRef(slot->sample);
        p->queue[p->queued++] = slot->sample;
        SetEvent(p->wake);
    } else {
        show(p, slot);
    }
    return S_OK;
}

/* evr's ProcessOutputLoop. */
static HRESULT process_output_loop(Presenter *p)
{
    HRESULT hr;
    do {
        if (!p->has_input) {
            check_end_of_stream(p);
            return MF_E_TRANSFORM_NEED_MORE_INPUT;
        }
        hr = process_output(p);
        if (hr == MF_E_TRANSFORM_TYPE_NOT_SET && p->mixer) {
            hr = renegotiate(p);
            if (FAILED(hr))
                return hr;
        }
    } while (hr == S_OK);
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        check_end_of_stream(p);
    return hr == S_FALSE ? S_OK : hr;
}

/* ------------------------------------------------------------------ media types */

static void monitor_rect(Presenter *p, RECT *rect)
{
    MONITORINFO mi = {sizeof(mi)};
    SetRectEmpty(rect);
    if (GetMonitorInfoW(p->d3d->lpVtbl->GetAdapterMonitor(p->d3d, D3DADAPTER_DEFAULT), &mi))
        *rect = mi.rcMonitor;
}

/* evr's GetBoundedRect: the rectangle shrunk to fit the bounds, its proportions kept. */
static void bounded_rect(const RECT *bounds, RECT *rect)
{
    float bw = (float)(bounds->right - bounds->left), bh = (float)(bounds->bottom - bounds->top);
    float w = (float)(rect->right - rect->left), h = (float)(rect->bottom - rect->top);
    if (bw <= 0.0f || bh <= 0.0f || w <= 0.0f || h <= 0.0f)
        return;
    if (w > bw) {
        float k = w / bw;
        w /= k;
        h /= k;
    }
    if (h > bh) {
        float k = h / bh;
        w /= k;
        h /= k;
    }
    SetRect(rect, 0, 0, (int)w, (int)h);
}

/* evr's GetReferenceSourceAndTargetRectFromVF: the input size, and the same picture in the pixels of the output. */
static void reference_target(const MFVIDEOFORMAT *in, const MFVIDEOFORMAT *out, RECT *src, RECT *target)
{
    double w = in->videoInfo.dwWidth, h = in->videoInfo.dwHeight;
    SetRectEmpty(src);
    SetRectEmpty(target);
    if (w <= 0 || h <= 0)
        return;
    LONG n = (LONG)in->videoInfo.PixelAspectRatio.Numerator, d = (LONG)in->videoInfo.PixelAspectRatio.Denominator;
    if (n < d)
        h = d * h / n;
    else if (n > d)
        w = n * w / d;
    n = (LONG)out->videoInfo.PixelAspectRatio.Numerator;
    d = (LONG)out->videoInfo.PixelAspectRatio.Denominator;
    if (n < d)
        h = n * h / d;
    else if (n > d)
        w = d * w / n;
    SetRect(src, 0, 0, (int)(in->videoInfo.dwWidth + 0.5), (int)(in->videoInfo.dwHeight + 0.5));
    SetRect(target, 0, 0, (int)(w + 0.5), (int)(h + 0.5));
}

static float square_root(float v)
{
    if (v <= 0.0f)
        return 0.0f;
    float r = v > 1.0f ? v : 1.0f;
    for (int i = 0; i < 40; i++)
        r = 0.5f * (r + v / r);
    return r;
}

/*
 * evr's CreateOptimalType: the mixer's type at the size of the destination rectangle, or of the picture bounded by
 * the monitor. a constriction shrinks the geometric aperture. keeping pixels gives the monitor's own ratio, no aspect
 * mode at all a ratio that stretches the picture over the rectangle.
 */
static HRESULT optimal_type(Presenter *p, IUnknown *proposed, IMFMediaType **out)
{
    const MFVIDEOFORMAT *in = EvrVideoFormat(proposed);
    if (!in)
        return MF_E_UNEXPECTED;
    RECT mon, src, rect;
    monitor_rect(p, &mon);
    LONG mw = mon.right - mon.left, mh = mon.bottom - mon.top;
    MFVIDEOFORMAT f = *in;
    if (p->aspect & MFVideoARMode_PreservePixel) {
        f.videoInfo.PixelAspectRatio.Numerator = mw * mh;
        f.videoInfo.PixelAspectRatio.Denominator = mh * mw;
    } else {
        f.videoInfo.PixelAspectRatio.Numerator = 1;
        f.videoInfo.PixelAspectRatio.Denominator = 1;
    }
    reference_target(in, &f, &src, &rect);
    if (IsRectEmpty(&p->dst))
        bounded_rect(&mon, &rect);
    else
        rect = p->dst;
    LONG width = rect.right - rect.left, height = rect.bottom - rect.top;
    if (!width || !height)
        return MF_E_UNEXPECTED;
    float scale = 1.0f;
    if (p->constriction != -1) {
        float area = (float)(p->constriction ? 1000 * p->constriction : 144);
        LONG pixels = width * height;
        scale = square_root(area / (float)(pixels < 0 ? -pixels : pixels));
    }
    if (!p->aspect) {
        LONG n = (LONG)in->videoInfo.PixelAspectRatio.Numerator, d = (LONG)in->videoInfo.PixelAspectRatio.Denominator;
        if (!n && !d)
            return MF_E_UNEXPECTED;
        float x = (float)height * ((p->src.right - p->src.left) * (float)(src.right - src.left)) * (float)n;
        float y = (float)width * ((p->src.bottom - p->src.top) * (float)(src.bottom - src.top)) * (float)d;
        float most = x > y ? x : y;
        if (most == 0.0f)
            return MF_E_UNEXPECTED;
        LONG big = mh * mw > mw * mh ? mh * mw : mw * mh;
        float k = (float)big / most;
        x *= k;
        y *= k;
        if (x <= 0.0f || y <= 0.0f || !(int)(y + 0.5f) || !(int)(x + 0.5f))
            return MF_E_UNEXPECTED;
        f.videoInfo.PixelAspectRatio.Numerator = (int)(x + 0.5f);
        f.videoInfo.PixelAspectRatio.Denominator = (int)(y + 0.5f);
    }
    f.videoInfo.ColorPrimaries = MFVideoPrimaries_BT709;
    f.videoInfo.TransferMatrix = MFVideoTransferMatrix_Unknown;
    f.videoInfo.TransferFunction = in->videoInfo.TransferFunction ? in->videoInfo.TransferFunction : MFVideoTransFunc_709;
    f.videoInfo.NominalRange = in->videoInfo.NominalRange ? in->videoInfo.NominalRange : MFNominalRange_Wide;
    f.videoInfo.SourceLighting = in->videoInfo.SourceLighting ? in->videoInfo.SourceLighting : MFVideoLighting_dim;
    if (f.videoInfo.TransferFunction == MFVideoTransFunc_709 && f.videoInfo.NominalRange == MFNominalRange_Wide &&
        f.videoInfo.SourceLighting == MFVideoLighting_dim) {
        f.videoInfo.TransferFunction = MFVideoTransFunc_Unknown;
        f.videoInfo.NominalRange = MFNominalRange_Unknown;
        f.videoInfo.SourceLighting = MFVideoLighting_Unknown;
    }
    f.videoInfo.dwWidth = width;
    f.videoInfo.dwHeight = height;
    memset(&f.videoInfo.GeometricAperture, 0, sizeof(MFVideoArea));
    memset(&f.videoInfo.MinimumDisplayAperture, 0, sizeof(MFVideoArea));
    memset(&f.videoInfo.PanScanAperture, 0, sizeof(MFVideoArea));
    f.videoInfo.MinimumDisplayAperture.Area.cx = width;
    f.videoInfo.MinimumDisplayAperture.Area.cy = height;
    f.videoInfo.PanScanAperture.Area = f.videoInfo.MinimumDisplayAperture.Area;
    f.videoInfo.GeometricAperture.Area.cx = (int)((float)width * scale);
    f.videoInfo.GeometricAperture.Area.cy = (int)((float)height * scale);
    return MFCreateVideoMediaType(&f, out);
}

static BOOL supported(Presenter *p, IUnknown *type)
{
    if (FAILED(EvrValidateVideoType(type)))
        return FALSE;
    const MFVIDEOFORMAT *f = EvrVideoFormat(type);
    return f && f->videoInfo.InterlaceMode == MFVideoInterlace_Progressive &&
           (f->surfaceInfo.Format == D3DFMT_X8R8G8B8 || f->surfaceInfo.Format == D3DFMT_A8R8G8B8);
}

/* evr's RenegotiateMediaType: the first output type of the mixer the presenter can show. */
static HRESULT renegotiate(Presenter *p)
{
    if (!p->mixer)
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    HRESULT hr = MF_E_NO_MORE_TYPES;
    for (DWORD i = 0;; i++) {
        IMFMediaType *available = nullptr, *optimal = nullptr;
        IUnknown *video = nullptr;
        HRESULT got = p->mixer->lpVtbl->GetOutputAvailableType(p->mixer, 0, i, &available);
        if (FAILED(got)) {
            if (got != MF_E_NO_MORE_TYPES)
                hr = got;
            break;
        }
        hr = available->lpVtbl->QueryInterface(available, &IID_IMFVideoMediaType, (void **)&video);
        if (SUCCEEDED(hr))
            hr = optimal_type(p, video, &optimal);
        if (SUCCEEDED(hr) && !supported(p, (IUnknown *)optimal))
            hr = MF_E_INVALIDMEDIATYPE;
        if (SUCCEEDED(hr))
            hr = p->mixer->lpVtbl->SetOutputType(p->mixer, 0, optimal, MFT_SET_TYPE_TEST_ONLY);
        if (hr == S_OK) {
            const MFVIDEOFORMAT *f = EvrVideoFormat((IUnknown *)optimal), *native = EvrVideoFormat(video);
            hr = make_slots(p, f->videoInfo.dwWidth, f->videoInfo.dwHeight, (D3DFORMAT)f->surfaceInfo.Format);
            if (SUCCEEDED(hr))
                hr = p->mixer->lpVtbl->SetOutputType(p->mixer, 0, optimal, 0);
            if (SUCCEEDED(hr)) {
                if (p->type)
                    p->type->lpVtbl->Release(p->type);
                if (p->mixer_type)
                    p->mixer_type->lpVtbl->Release(p->mixer_type);
                optimal->lpVtbl->QueryInterface(optimal, &IID_IMFVideoMediaType, (void **)&p->type);
                p->mixer_type = video;
                video = nullptr;
                p->native.cx = native->videoInfo.dwWidth;
                p->native.cy = native->videoInfo.dwHeight;
                p->par = native->videoInfo.PixelAspectRatio;
                DWORD n = native->videoInfo.FramesPerSecond.Numerator,
                      d = native->videoInfo.FramesPerSecond.Denominator;
                p->frame = n && d ? MulDiv(10000000, d, n) : 333333;
            }
        }
        if (optimal)
            optimal->lpVtbl->Release(optimal);
        if (video)
            video->lpVtbl->Release(video);
        available->lpVtbl->Release(available);
        if (SUCCEEDED(hr))
            break;
    }
    return hr;
}

/* ------------------------------------------------------------------ IUnknown */

static HRESULT query(Presenter *p, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown))
        *out = &p->inner;
    else if (IsEqualGUID(riid, &IID_IMFVideoPresenter) || IsEqualGUID(riid, &IID_IMFClockStateSink))
        *out = &p->presenter;
    else if (IsEqualGUID(riid, &IID_IMFVideoDeviceID))
        *out = &p->device_id;
    else if (IsEqualGUID(riid, &IID_IMFVideoDisplayControl))
        *out = &p->display;
    else if (IsEqualGUID(riid, &IID_IMFRateSupport))
        *out = &p->rate;
    else if (IsEqualGUID(riid, &IID_IMFVideoPositionMapper))
        *out = &p->mapper;
    else if (IsEqualGUID(riid, &IID_IMFTopologyServiceLookupClient))
        *out = &p->lookup;
    else if (IsEqualGUID(riid, &IID_IMFGetService_))
        *out = &p->get_service;
    else if (IsEqualGUID(riid, &IID_IQualProp))
        *out = &p->qualprop;
    else if (IsEqualGUID(riid, &IID_IEVRTrustedVideoPlugin))
        *out = &p->trusted;
    else if (IsEqualGUID(riid, &IID_IDirect3DDeviceManager9))
        *out = &p->manager_iface;
    else if (IsEqualGUID(riid, &IID_IMFQualityAdvise))
        *out = &p->quality;
    else if (IsEqualGUID(riid, &IID_IPresenterStatistics))
        *out = &p->statistics;
    else if (IsEqualGUID(riid, &IID_IMFQualityAdviseLimits))
        *out = &p->limits;
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

static void release_services(Presenter *p)
{
    if (p->clock)
        p->clock->lpVtbl->Release(p->clock);
    if (p->mixer)
        p->mixer->lpVtbl->Release(p->mixer);
    if (p->sink)
        p->sink->lpVtbl->Release(p->sink);
    p->clock = nullptr;
    p->mixer = nullptr;
    p->sink = nullptr;
}

static ULONG STDMETHODCALLTYPE inner_Release(IUnknown *This)
{
    Presenter *p = IMPL(inner);
    ULONG refs = InterlockedDecrement(&p->refs);
    if (refs)
        return refs;
    p->refs = 1;
    if (p->thread) {
        EnterCriticalSection(&p->lock);
        p->quit = TRUE;
        LeaveCriticalSection(&p->lock);
        SetEvent(p->wake);
        WaitForSingleObject(p->thread, INFINITE);
        CloseHandle(p->thread);
    }
    if (p->wake)
        CloseHandle(p->wake);
    free_slots(p);
    release_services(p);
    if (p->last)
        p->last->lpVtbl->Release(p->last);
    if (p->type)
        p->type->lpVtbl->Release(p->type);
    if (p->mixer_type)
        p->mixer_type->lpVtbl->Release(p->mixer_type);
    if (p->chain)
        p->chain->lpVtbl->Release(p->chain);
    if (p->manager)
        p->manager->lpVtbl->Release(p->manager);
    if (p->device)
        p->device->lpVtbl->Release(p->device);
    if (p->d3d)
        p->d3d->lpVtbl->Release(p->d3d);
    if (p->focus)
        DestroyWindow(p->focus);
    if (p->helper)
        p->helper->lpVtbl->Release(p->helper);
    DeleteCriticalSection(&p->lock);
    HeapFree(GetProcessHeap(), 0, p);
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

/* ------------------------------------------------------------------ IMFVideoPresenter */

DELEGATE_IUNKNOWN(presenter, IMFVideoPresenter, presenter)

/* a start from a new position drops what waits; a start from stop does not. */
static HRESULT STDMETHODCALLTYPE presenter_OnClockStart(IMFVideoPresenter *This, MFTIME system, LONGLONG offset)
{
    Presenter *p = IMPL(presenter);
    EnterCriticalSection(&p->lock);
    DWORD was = p->state;
    p->state = RENDER_STARTED;
    if (was != RENDER_STOPPED && offset != PRESENTATION_CURRENT_POSITION)
        flush(p);
    if (p->rate_now != 0.0f)
        process_output_loop(p);
    SetEvent(p->wake);
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE presenter_OnClockStop(IMFVideoPresenter *This, MFTIME system)
{
    Presenter *p = IMPL(presenter);
    EnterCriticalSection(&p->lock);
    p->prerolled = FALSE;
    if (p->state != RENDER_STOPPED) {
        p->state = RENDER_STOPPED;
        flush(p);
    }
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE presenter_OnClockPause(IMFVideoPresenter *This, MFTIME system)
{
    Presenter *p = IMPL(presenter);
    EnterCriticalSection(&p->lock);
    p->state = RENDER_PAUSED;
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE presenter_OnClockRestart(IMFVideoPresenter *This, MFTIME system)
{
    Presenter *p = IMPL(presenter);
    EnterCriticalSection(&p->lock);
    p->state = RENDER_STARTED;
    if (p->rate_now != 0.0f)
        process_output_loop(p);
    SetEvent(p->wake);
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE presenter_OnClockSetRate(IMFVideoPresenter *This, MFTIME system, float rate)
{
    Presenter *p = IMPL(presenter);
    EnterCriticalSection(&p->lock);
    p->rate_now = rate;
    SetEvent(p->wake);
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE presenter_ProcessMessage(IMFVideoPresenter *This, MFVP_MESSAGE_TYPE message,
                                                          ULONG_PTR param)
{
    Presenter *p = IMPL(presenter);
    HRESULT hr = S_OK;
    EnterCriticalSection(&p->lock);
    switch (message) {
    case MFVP_MESSAGE_FLUSH:
        p->has_input = FALSE;
        flush(p);
        break;
    case MFVP_MESSAGE_INVALIDATEMEDIATYPE:
        if (!p->mixer)
            hr = MF_E_INVALIDREQUEST;
        else if (FAILED(renegotiate(p)))
            hr = MF_E_INVALIDMEDIATYPE;
        break;
    case MFVP_MESSAGE_PROCESSINPUTNOTIFY:
        p->has_input = TRUE;
        if (p->mixer)
            process_output_loop(p);
        else
            hr = MF_E_TRANSFORM_TYPE_NOT_SET;
        break;
    case MFVP_MESSAGE_BEGINSTREAMING:
        p->streaming = TRUE;
        break;
    case MFVP_MESSAGE_ENDSTREAMING:
        p->streaming = FALSE;
        break;
    case MFVP_MESSAGE_ENDOFSTREAM:
        p->end_of_stream = TRUE;
        check_end_of_stream(p);
        break;
    case MFVP_MESSAGE_STEP:
    case MFVP_MESSAGE_CANCELSTEP:
        break;
    default:
        hr = E_INVALIDARG;
        break;
    }
    LeaveCriticalSection(&p->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE presenter_GetCurrentMediaType(IMFVideoPresenter *This, IUnknown **type)
{
    Presenter *p = IMPL(presenter);
    HRESULT hr;
    if (!type)
        return E_POINTER;
    EnterCriticalSection(&p->lock);
    *type = nullptr;
    if (!p->type)
        hr = MF_E_NOT_INITIALIZED;
    else
        hr = p->type->lpVtbl->QueryInterface(p->type, &IID_IMFVideoMediaType, (void **)type);
    LeaveCriticalSection(&p->lock);
    return hr;
}

static const IMFVideoPresenterVtbl presenter_vtbl = {
    presenter_QueryInterface, presenter_AddRef,         presenter_Release,
    presenter_OnClockStart,   presenter_OnClockStop,    presenter_OnClockPause,
    presenter_OnClockRestart, presenter_OnClockSetRate, presenter_ProcessMessage,
    presenter_GetCurrentMediaType,
};

/* ------------------------------------------------------------------ IMFVideoDisplayControl */

DELEGATE_IUNKNOWN(display, IMFVideoDisplayControl, display)

static HRESULT STDMETHODCALLTYPE display_GetNativeVideoSize(IMFVideoDisplayControl *This, SIZE *video, SIZE *aspect)
{
    Presenter *p = IMPL(display);
    if (!video && !aspect)
        return E_POINTER;
    EnterCriticalSection(&p->lock);
    if (video)
        *video = p->native;
    if (aspect) {
        /* the picture aspect ratio reduced as evr's PixelAspectToPictureAspect does. */
        LONGLONG x = (LONGLONG)p->native.cx * (p->par.Numerator ? p->par.Numerator : 1);
        LONGLONG y = (LONGLONG)p->native.cy * (p->par.Denominator ? p->par.Denominator : 1);
        LONGLONG a = x, b = y;
        while (b) {
            LONGLONG t = a % b;
            a = b;
            b = t;
        }
        aspect->cx = a ? (LONG)(x / a) : 0;
        aspect->cy = a ? (LONG)(y / a) : 0;
    }
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE display_GetIdealVideoSize(IMFVideoDisplayControl *This, SIZE *min, SIZE *max)
{
    Presenter *p = IMPL(display);
    if (!min && !max)
        return E_POINTER;
    HMONITOR monitor = p->d3d->lpVtbl->GetAdapterMonitor(p->d3d, D3DADAPTER_DEFAULT);
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoW(monitor, &mi))
        return MF_E_INVALIDREQUEST;
    if (min) {
        min->cx = 1;
        min->cy = 1;
    }
    if (max) {
        max->cx = mi.rcMonitor.right - mi.rcMonitor.left;
        max->cy = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }
    return S_OK;
}

static HRESULT check_src(const MFVideoNormalizedRect *r)
{
    if (r->left < 0.0f || r->top < 0.0f || r->right > 1.0f || r->bottom > 1.0f || r->left >= r->right ||
        r->top >= r->bottom)
        return E_INVALIDARG;
    return S_OK;
}

/* the source rectangle goes to the mixer as its zoom, unless the presenter stretches the picture itself. */
static HRESULT mixer_zoom(Presenter *p, IMFTransform *mixer, const MFVideoNormalizedRect *src)
{
    static const MFVideoNormalizedRect full = {0, 0, 1, 1};
    if (!mixer)
        return S_OK;
    IMFAttributes *attributes = nullptr;
    HRESULT hr = mixer->lpVtbl->GetAttributes(mixer, &attributes);
    if (SUCCEEDED(hr)) {
        if (p->aspect & MFVideoARMode_NonLinearStretch)
            src = &full;
        hr = attributes->lpVtbl->SetBlob(attributes, &VIDEO_ZOOM_RECT, (const UINT8 *)src, sizeof(*src));
        attributes->lpVtbl->Release(attributes);
    }
    return hr;
}

/* evr's InitMixer: a Direct3D 9 mixer gets the zoom and the mixing preference of the presenter's own surfaces. */
static HRESULT init_mixer(Presenter *p, IMFTransform *mixer)
{
    IMFVideoDeviceID *id = nullptr;
    IMFVideoMixerControl2 *control = nullptr;
    IID device;
    HRESULT hr = mixer->lpVtbl->QueryInterface(mixer, &IID_IMFVideoDeviceID, (void **)&id);
    if (SUCCEEDED(hr)) {
        hr = id->lpVtbl->GetDeviceID(id, &device);
        id->lpVtbl->Release(id);
    }
    if (SUCCEEDED(hr) && !IsEqualGUID(&device, &IID_IDirect3DDevice9))
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr))
        hr = mixer_zoom(p, mixer, &p->src);
    if (SUCCEEDED(hr) &&
        SUCCEEDED(mixer->lpVtbl->QueryInterface(mixer, &IID_IMFVideoMixerControl2, (void **)&control))) {
        DWORD prefs = 0;
        if (SUCCEEDED(control->lpVtbl->GetMixingPrefs(control, &prefs)))
            control->lpVtbl->SetMixingPrefs(control, prefs | MFVideoMixPrefs_EnableRotation);
        control->lpVtbl->Release(control);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE display_SetVideoPosition(IMFVideoDisplayControl *This,
                                                          const MFVideoNormalizedRect *src, const RECT *dst)
{
    Presenter *p = IMPL(display);
    HRESULT hr = S_OK;
    EnterCriticalSection(&p->lock);
    if ((!IsWindow(p->window) && dst) || (!src && !dst)) {
        hr = E_POINTER;
    } else if (dst && (dst->left > dst->right || dst->top > dst->bottom)) {
        hr = E_INVALIDARG;
    } else if (src && FAILED(hr = check_src(src))) {
    } else {
        BOOL changed = FALSE;
        if (src && memcmp(src, &p->src, sizeof(*src))) {
            hr = mixer_zoom(p, p->mixer, src);
            if (SUCCEEDED(hr)) {
                p->src = *src;
                changed = TRUE;
            }
        }
        if (SUCCEEDED(hr) && dst) {
            if (dst->right - dst->left != p->dst.right - p->dst.left ||
                dst->bottom - dst->top != p->dst.bottom - p->dst.top)
                changed = TRUE;
            p->dst = *dst;
        }
        if (SUCCEEDED(hr) && changed && p->mixer && p->type) {
            HRESULT r = renegotiate(p);
            if (FAILED(r) && r != MF_E_TRANSFORM_TYPE_NOT_SET)
                hr = r;
        }
        if (SUCCEEDED(hr) && p->window)
            InvalidateRect(p->window, nullptr, FALSE);
    }
    LeaveCriticalSection(&p->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE display_GetVideoPosition(IMFVideoDisplayControl *This, MFVideoNormalizedRect *src,
                                                          RECT *dst)
{
    Presenter *p = IMPL(display);
    if (!src || !dst)
        return E_POINTER;
    EnterCriticalSection(&p->lock);
    *src = p->src;
    *dst = p->dst;
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

/* the flip mode presenter takes no stretching mode. */
static HRESULT STDMETHODCALLTYPE display_SetAspectRatioMode(IMFVideoDisplayControl *This, DWORD mode)
{
    Presenter *p = IMPL(display);
    if (mode & ~7u || (p->flip && mode > 3))
        return E_INVALIDARG;
    EnterCriticalSection(&p->lock);
    p->aspect = mode;
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE display_GetAspectRatioMode(IMFVideoDisplayControl *This, DWORD *mode)
{
    if (!mode)
        return E_POINTER;
    *mode = IMPL(display)->aspect;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE display_SetVideoWindow(IMFVideoDisplayControl *This, HWND window)
{
    Presenter *p = IMPL(display);
    if (!IsWindow(window))
        return E_INVALIDARG;
    EnterCriticalSection(&p->lock);
    if (p->window != window && p->chain) {
        p->chain->lpVtbl->Release(p->chain);
        p->chain = nullptr;
    }
    p->window = window;
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE display_GetVideoWindow(IMFVideoDisplayControl *This, HWND *window)
{
    if (!window)
        return E_POINTER;
    *window = IMPL(display)->window;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE display_RepaintVideo(IMFVideoDisplayControl *This)
{
    Presenter *p = IMPL(display);
    EnterCriticalSection(&p->lock);
    if (p->last)
        present_surface(p, p->last);
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

/*
 * evr's CopyToNewBuffer: the rows from the bottom up, every pixel as 32-bit RGB with a zero top byte. a format it
 * does not know is left as allocated.
 */
static void copy_to_buffer(UINT width, UINT height, D3DFORMAT format, const BYTE *bits, int pitch, BYTE *out)
{
    for (UINT y = 0; y < height; y++) {
        const BYTE *row = bits + (SSIZE_T)y * pitch;
        DWORD *to = (DWORD *)(out + 4ull * width * (height - 1 - y));
        for (UINT x = 0; x < width; x++) {
            DWORD d = ((const DWORD *)row)[x], w = ((const WORD *)row)[x], t;
            switch (format) {
            case D3DFMT_R8G8B8:
                to[x] = row[3 * x] | row[3 * x + 1] << 8 | row[3 * x + 2] << 16;
                break;
            case D3DFMT_A8R8G8B8:
            case D3DFMT_X8R8G8B8:
                to[x] = d & 0xFFFFFF;
                break;
            case D3DFMT_R5G6B5:
                t = 8 * ((w & 0x1F) | 4 * ((w & 0x7E0) | 8 * (w & 0xF800)));
                to[x] = ((((t >> 5) & 0x70007) | t) >> 6 & 0x300) | ((t >> 5) & 0x70007) | t;
                break;
            case D3DFMT_X1R5G5B5:
            case D3DFMT_A1R5G5B5:
                t = 8 * ((w & 0x1F) | 8 * ((w & 0x3E0) | 8 * (w & 0x7C00)));
                to[x] = ((t >> 5) & 0x70707) | t;
                break;
            case D3DFMT_A2B10G10R10:
                to[x] = (d & 0x3FC) << 14 | ((d & 0xFF000) | ((d >> 18) & 0xFF0)) >> 4;
                break;
            case D3DFMT_A2R10G10B10:
                to[x] = ((d & 0x3FC) | ((d & 0xFF000) | ((d >> 2) & 0xFF00000)) >> 2) >> 2;
                break;
            default:
                break;
            }
        }
    }
}

/*
 * the last picture shown as a 32-bit DIB the caller frees: bottom up, or top down from the flip mode presenter, which
 * knows its last picture only from its second present on.
 */
static HRESULT STDMETHODCALLTYPE display_GetCurrentImage(IMFVideoDisplayControl *This, BITMAPINFOHEADER *header,
                                                         BYTE **dib, DWORD *size, LONGLONG *time)
{
    Presenter *p = IMPL(display);
    HRESULT hr = MF_E_INVALIDREQUEST;
    IDirect3DSurface9 *copy = nullptr;
    EnterCriticalSection(&p->lock);
    if (p->no_export)
        hr = MF_E_LICENSE_INCORRECT_RIGHTS_;
    else if (!dib || !header || !size)
        hr = E_POINTER;
    else if (header->biSize != sizeof(*header))
        hr = E_INVALIDARG;
    else
        *dib = nullptr;
    if (hr == MF_E_INVALIDREQUEST && p->last && (!p->flip || p->presents >= 2)) {
        D3DSURFACE_DESC desc;
        p->last->lpVtbl->GetDesc(p->last, &desc);
        hr = p->device->lpVtbl->CreateOffscreenPlainSurface(p->device, desc.Width, desc.Height, desc.Format,
                                                           D3DPOOL_SYSTEMMEM, &copy, nullptr);
        if (SUCCEEDED(hr))
            hr = p->device->lpVtbl->GetRenderTargetData(p->device, p->last, copy);
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(hr))
            hr = copy->lpVtbl->LockRect(copy, &lr, nullptr, D3DLOCK_READONLY);
        if (SUCCEEDED(hr)) {
            DWORD bytes = 4 * desc.Width * desc.Height;
            BYTE *out = CoTaskMemAlloc(bytes);
            if (!out) {
                hr = E_OUTOFMEMORY;
            } else {
                if (p->flip) {
                    for (UINT y = 0; y < desc.Height; y++)
                        memcpy(out + 4ull * desc.Width * y, (BYTE *)lr.pBits + (SIZE_T)y * lr.Pitch,
                               4ull * desc.Width);
                } else {
                    copy_to_buffer(desc.Width, desc.Height, desc.Format, lr.pBits, lr.Pitch, out);
                }
                memset(header, 0, sizeof(*header));
                header->biSize = sizeof(*header);
                header->biWidth = desc.Width;
                header->biHeight = p->flip ? -(LONG)desc.Height : (LONG)desc.Height;
                header->biPlanes = 1;
                header->biBitCount = 32;
                header->biSizeImage = bytes;
                *dib = out;
                *size = bytes;
                if (time)
                    *time = p->last_time;
            }
            copy->lpVtbl->UnlockRect(copy);
        }
    }
    if (copy)
        copy->lpVtbl->Release(copy);
    LeaveCriticalSection(&p->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE display_SetBorderColor(IMFVideoDisplayControl *This, COLORREF color)
{
    IMPL(display)->border = color;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE display_GetBorderColor(IMFVideoDisplayControl *This, COLORREF *color)
{
    if (!color)
        return E_POINTER;
    *color = IMPL(display)->border;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE display_SetRenderingPrefs(IMFVideoDisplayControl *This, DWORD prefs)
{
    if (prefs & ~(DWORD)MFVideoRenderPrefs_Mask)
        return E_INVALIDARG;
    IMPL(display)->prefs = prefs;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE display_GetRenderingPrefs(IMFVideoDisplayControl *This, DWORD *prefs)
{
    if (!prefs)
        return E_POINTER;
    *prefs = IMPL(display)->prefs;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE display_SetFullscreen(IMFVideoDisplayControl *This, BOOL fullscreen)
{
    Presenter *p = IMPL(display);
    if (p->flip)
        return E_NOTIMPL;
    p->fullscreen = fullscreen;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE display_GetFullscreen(IMFVideoDisplayControl *This, BOOL *fullscreen)
{
    if (!fullscreen)
        return E_POINTER;
    *fullscreen = IMPL(display)->fullscreen;
    return S_OK;
}

static const IMFVideoDisplayControlVtbl display_vtbl = {
    display_QueryInterface,    display_AddRef,           display_Release,          display_GetNativeVideoSize,
    display_GetIdealVideoSize, display_SetVideoPosition, display_GetVideoPosition, display_SetAspectRatioMode,
    display_GetAspectRatioMode, display_SetVideoWindow,  display_GetVideoWindow,   display_RepaintVideo,
    display_GetCurrentImage,   display_SetBorderColor,   display_GetBorderColor,   display_SetRenderingPrefs,
    display_GetRenderingPrefs, display_SetFullscreen,    display_GetFullscreen,
};

/* ------------------------------------------------------------------ IMFRateSupport */

DELEGATE_IUNKNOWN(rate, IMFRateSupport, rate)

/* evr's GetMaxRate: any rate thinned, else the display's refresh over the frame rate. */
static HRESULT max_rate(Presenter *p, BOOL thin, float *rate)
{
    if (!p->type)
        return MF_E_INVALIDREQUEST;
    if (thin) {
        *rate = 3.4028235e38f;
        return S_OK;
    }
    const MFVIDEOFORMAT *f = EvrVideoFormat(p->mixer_type);
    D3DDISPLAYMODE mode = {0};
    p->d3d->lpVtbl->GetAdapterDisplayMode(p->d3d, D3DADAPTER_DEFAULT, &mode);
    DWORD n = f ? f->videoInfo.FramesPerSecond.Numerator : 0, d = f ? f->videoInfo.FramesPerSecond.Denominator : 0;
    *rate = n && d ? (float)MulDiv(mode.RefreshRate ? mode.RefreshRate : 60, d, n) : 3.4028235e38f;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rate_GetSlowestRate(IMFRateSupport *This, MFRATE_DIRECTION direction, BOOL thin,
                                                     float *rate)
{
    if (!rate)
        return E_POINTER;
    *rate = 0.0f;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rate_GetFastestRate(IMFRateSupport *This, MFRATE_DIRECTION direction, BOOL thin,
                                                     float *rate)
{
    Presenter *p = IMPL(rate);
    if (!rate)
        return E_POINTER;
    EnterCriticalSection(&p->lock);
    float r = 0.0f;
    HRESULT hr = max_rate(p, thin, &r);
    if (SUCCEEDED(hr))
        *rate = direction == MFRATE_FORWARD ? r : -r;
    LeaveCriticalSection(&p->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE rate_IsRateSupported(IMFRateSupport *This, BOOL thin, float rate, float *nearest)
{
    Presenter *p = IMPL(rate);
    HRESULT hr = S_OK;
    EnterCriticalSection(&p->lock);
    if (!thin) {
        float r = 0.0f;
        hr = max_rate(p, FALSE, &r);
        if (SUCCEEDED(hr) && ((rate > 0.0f && rate > r) || (rate < 0.0f && -r > rate))) {
            hr = MF_E_UNSUPPORTED_RATE_;
            rate = rate < 0.0f ? -r : r;
        }
    }
    LeaveCriticalSection(&p->lock);
    if (nearest)
        *nearest = rate;
    return hr;
}

static const IMFRateSupportVtbl rate_vtbl = {
    rate_QueryInterface, rate_AddRef, rate_Release, rate_GetSlowestRate, rate_GetFastestRate, rate_IsRateSupported,
};

/* ------------------------------------------------------------------ IMFGetService */

DELEGATE_IUNKNOWN(get_service, IMFGetService, get_service)

static HRESULT STDMETHODCALLTYPE get_service_GetService(IMFGetService *This, REFGUID service, REFIID riid, void **out)
{
    Presenter *p = IMPL(get_service);
    if (!IsEqualGUID(service, &MR_VIDEO_RENDER_SERVICE) && !IsEqualGUID(service, &MR_VIDEO_ACCELERATION_SERVICE))
        return MF_E_UNSUPPORTED_SERVICE;
    return query(p, riid, out);
}

static const IMFGetServiceVtbl get_service_vtbl = {
    get_service_QueryInterface, get_service_AddRef, get_service_Release, get_service_GetService,
};

/* ------------------------------------------------------------------ IMFTopologyServiceLookupClient */

DELEGATE_IUNKNOWN(lookup, IMFTopologyServiceLookupClient, lookup)

/*
 * the clock is optional; the mixer and the event sink are not. the device manager reaches the mixer through the
 * media sink, not through the presenter.
 */
static HRESULT STDMETHODCALLTYPE lookup_InitServicePointers(IMFTopologyServiceLookupClient *This,
                                                            IMFTopologyServiceLookup *lookup)
{
    Presenter *p = IMPL(lookup);
    HRESULT hr = S_OK;
    IMFClock *clock = nullptr;
    IMFTransform *mixer = nullptr;
    IMediaEventSink *sink = nullptr;
    DWORD count;
    EnterCriticalSection(&p->lock);
    if (p->lookup_state != 1 && p->lookup_state != 3) {
        hr = MF_E_INVALIDREQUEST;
        goto done;
    }
    if (!lookup) {
        hr = E_POINTER;
        goto done;
    }
    if (p->clock) {
        clock = p->clock;
        clock->lpVtbl->AddRef(clock);
    } else {
        count = 1;
        lookup->lpVtbl->LookupService(lookup, MF_SERVICE_LOOKUP_GLOBAL, 0, &MR_VIDEO_RENDER_SERVICE, &IID_IMFClock,
                                      (void **)&clock, &count);
    }
    if (p->mixer) {
        mixer = p->mixer;
        mixer->lpVtbl->AddRef(mixer);
    } else {
        count = 1;
        hr = lookup->lpVtbl->LookupService(lookup, MF_SERVICE_LOOKUP_UPSTREAM_DIRECT, 0, &MR_VIDEO_MIXER_SERVICE,
                                           &IID_IMFTransform, (void **)&mixer, &count);
        if (FAILED(hr))
            goto done;
        hr = init_mixer(p, mixer);
        if (FAILED(hr))
            goto done;
    }
    if (p->sink) {
        sink = p->sink;
        sink->lpVtbl->AddRef(sink);
    } else {
        count = 1;
        hr = lookup->lpVtbl->LookupService(lookup, MF_SERVICE_LOOKUP_UPSTREAM_DIRECT, 0, &MR_VIDEO_RENDER_SERVICE,
                                           &IID_IMediaEventSink, (void **)&sink, &count);
        if (FAILED(hr))
            goto done;
    }
    release_services(p);
    p->clock = clock;
    p->mixer = mixer;
    p->sink = sink;
    clock = nullptr;
    mixer = nullptr;
    sink = nullptr;
    p->lookup_state = 1;
    renegotiate(p);
done:
    if (clock)
        clock->lpVtbl->Release(clock);
    if (mixer)
        mixer->lpVtbl->Release(mixer);
    if (sink)
        sink->lpVtbl->Release(sink);
    LeaveCriticalSection(&p->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE lookup_ReleaseServicePointers(IMFTopologyServiceLookupClient *This)
{
    Presenter *p = IMPL(lookup);
    EnterCriticalSection(&p->lock);
    p->lookup_state = 3;
    p->streaming = FALSE;
    release_services(p);
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

static const IMFTopologyServiceLookupClientVtbl lookup_vtbl = {
    lookup_QueryInterface, lookup_AddRef, lookup_Release, lookup_InitServicePointers, lookup_ReleaseServicePointers,
};

/* ------------------------------------------------------------------ IMFVideoPositionMapper */

DELEGATE_IUNKNOWN(mapper, IMFVideoPositionMapper, mapper)

/* the output picture is the source rectangle of the video, so a point maps through it. */
static HRESULT STDMETHODCALLTYPE mapper_MapOutputCoordinateToInputStream(IMFVideoPositionMapper *This, float x,
                                                                         float y, DWORD out_id, DWORD in_id,
                                                                         float *in_x, float *in_y)
{
    Presenter *p = IMPL(mapper);
    if (!in_x || !in_y)
        return E_POINTER;
    if (out_id || in_id)
        return MF_E_INVALIDSTREAMNUMBER;
    EnterCriticalSection(&p->lock);
    *in_x = p->src.left + x * (p->src.right - p->src.left);
    *in_y = p->src.top + y * (p->src.bottom - p->src.top);
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

static const IMFVideoPositionMapperVtbl mapper_vtbl = {
    mapper_QueryInterface, mapper_AddRef, mapper_Release, mapper_MapOutputCoordinateToInputStream,
};

/* ------------------------------------------------------------------ IQualProp */

DELEGATE_IUNKNOWN(qualprop, IQualProp, qualprop)

/* windows 11's presenter answers none of the old DirectShow quality figures. */
static HRESULT STDMETHODCALLTYPE qualprop_get(IQualProp *This, int *value)
{
    return E_NOTIMPL;
}

static const IQualPropVtbl qualprop_vtbl = {
    qualprop_QueryInterface, qualprop_AddRef, qualprop_Release, qualprop_get,
    qualprop_get,            qualprop_get,    qualprop_get,     qualprop_get,
    qualprop_get,
};

/* ------------------------------------------------------------------ IEVRTrustedVideoPlugin */

DELEGATE_IUNKNOWN(trusted, IEVRTrustedVideoPlugin, trusted)

static HRESULT STDMETHODCALLTYPE trusted_IsInTrustedVideoMode(IEVRTrustedVideoPlugin *This, BOOL *trusted)
{
    if (!trusted)
        return E_POINTER;
    *trusted = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE trusted_CanConstrict(IEVRTrustedVideoPlugin *This, BOOL *can)
{
    if (!can)
        return E_POINTER;
    *can = TRUE;
    return S_OK;
}

/* the presenter renegotiates at once; a mixer with no type yet is no failure. */
static HRESULT STDMETHODCALLTYPE trusted_SetConstriction(IEVRTrustedVideoPlugin *This, DWORD kpixels)
{
    Presenter *p = IMPL(trusted);
    EnterCriticalSection(&p->lock);
    p->constriction = (LONG)kpixels;
    HRESULT hr = renegotiate(p);
    LeaveCriticalSection(&p->lock);
    return hr == MF_E_TRANSFORM_TYPE_NOT_SET ? S_OK : hr;
}

static HRESULT STDMETHODCALLTYPE trusted_DisableImageExport(IEVRTrustedVideoPlugin *This, BOOL disable)
{
    Presenter *p = IMPL(trusted);
    EnterCriticalSection(&p->lock);
    p->no_export = disable;
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

static const IEVRTrustedVideoPluginVtbl trusted_vtbl = {
    trusted_QueryInterface,       trusted_AddRef,       trusted_Release,
    trusted_IsInTrustedVideoMode, trusted_CanConstrict, trusted_SetConstriction,
    trusted_DisableImageExport,
};

/* ------------------------------------------------------------------ IMFQualityAdvise and its limits */

DELEGATE_IUNKNOWN(quality, IMFQualityAdvise, quality)

static HRESULT STDMETHODCALLTYPE quality_SetDropMode(IMFQualityAdvise *This, MF_QUALITY_DROP_MODE mode)
{
    if (mode != MF_DROP_MODE_NONE)
        return MF_E_DROPTIME_NOT_SUPPORTED_;
    IMPL(quality)->drop_mode = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE quality_SetQualityLevel(IMFQualityAdvise *This, MF_QUALITY_LEVEL level)
{
    if (level != MF_QUALITY_NORMAL)
        return MF_E_QUALITYLEVEL_NOT_SUPPORTED_;
    IMPL(quality)->quality_level = level;
    return S_OK;
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
    return E_NOTIMPL;
}

static const IMFQualityAdviseVtbl quality_vtbl = {
    quality_QueryInterface,  quality_AddRef,      quality_Release,         quality_SetDropMode,
    quality_SetQualityLevel, quality_GetDropMode, quality_GetQualityLevel, quality_DropTime,
};

DELEGATE_IUNKNOWN(limits, IMFQualityAdviseLimits, limits)

static HRESULT STDMETHODCALLTYPE limits_GetMaximumDropMode(IMFQualityAdviseLimits *This, MF_QUALITY_DROP_MODE *mode)
{
    if (!mode)
        return E_POINTER;
    *mode = MF_DROP_MODE_NONE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE limits_GetMinimumQualityLevel(IMFQualityAdviseLimits *This, MF_QUALITY_LEVEL *level)
{
    if (!level)
        return E_POINTER;
    *level = MF_QUALITY_NORMAL;
    return S_OK;
}

static const IMFQualityAdviseLimitsVtbl limits_vtbl = {
    limits_QueryInterface, limits_AddRef, limits_Release, limits_GetMaximumDropMode, limits_GetMinimumQualityLevel,
};

/* ------------------------------------------------------------------ IPresenterStatistics */

DELEGATE_IUNKNOWN(statistics, IPresenterStatistics, statistics)

/* the tearless presenter's figures, then the adapter: its description, driver, driver version and identifier. */
static HRESULT STDMETHODCALLTYPE statistics_GetPresenterStatistics(IPresenterStatistics *This, DWORD *stats)
{
    Presenter *p = IMPL(statistics);
    if (!stats)
        return E_POINTER;
    memset(stats, 0, EVR_STATISTICS_SIZE);
    IPresenterStatistics *inner = nullptr;
    HRESULT hr = p->helper->lpVtbl->QueryInterface(p->helper, &IID_IPresenterStatistics, (void **)&inner);
    if (FAILED(hr))
        return hr;
    hr = inner->lpVtbl->GetPresenterStatistics(inner, stats);
    inner->lpVtbl->Release(inner);
    if (FAILED(hr))
        return hr;
    D3DADAPTER_IDENTIFIER9 id;
    memset(&id, 0, sizeof(id));
    if (SUCCEEDED(p->d3d->lpVtbl->GetAdapterIdentifier(p->d3d, D3DADAPTER_DEFAULT, 0, &id))) {
        char *text = (char *)stats;
        for (int i = 0; i < 63 && id.Description[i]; i++)
            text[72 + i] = id.Description[i];
        for (int i = 0; i < 63 && id.Driver[i]; i++)
            text[136 + i] = id.Driver[i];
        stats[50] = id.DriverVersion.LowPart;
        stats[51] = (DWORD)id.DriverVersion.HighPart;
        memcpy(stats + 52, &id.DeviceIdentifier, sizeof(GUID));
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE statistics_ResetStatistics(IPresenterStatistics *This)
{
    Presenter *p = IMPL(statistics);
    IPresenterStatistics *inner = nullptr;
    HRESULT hr = p->helper->lpVtbl->QueryInterface(p->helper, &IID_IPresenterStatistics, (void **)&inner);
    if (SUCCEEDED(hr)) {
        hr = inner->lpVtbl->ResetStatistics(inner);
        inner->lpVtbl->Release(inner);
    }
    return hr;
}

static const IPresenterStatisticsVtbl statistics_vtbl = {
    statistics_QueryInterface,          statistics_AddRef,          statistics_Release,
    statistics_GetPresenterStatistics, statistics_ResetStatistics,
};

/* ------------------------------------------------------------------ IDirect3DDeviceManager9 */

DELEGATE_IUNKNOWN(manager, IDirect3DDeviceManager9, manager_iface)

#define MANAGER(call)                                                                                                  \
    IDirect3DDeviceManager9 *m = IMPL(manager_iface)->manager;                                                         \
    return m->lpVtbl->call;

static HRESULT STDMETHODCALLTYPE manager_ResetDevice(IDirect3DDeviceManager9 *This, IDirect3DDevice9 *device,
                                                     UINT token)
{
    MANAGER(ResetDevice(m, device, token))
}

static HRESULT STDMETHODCALLTYPE manager_OpenDeviceHandle(IDirect3DDeviceManager9 *This, HANDLE *handle)
{
    MANAGER(OpenDeviceHandle(m, handle))
}

static HRESULT STDMETHODCALLTYPE manager_CloseDeviceHandle(IDirect3DDeviceManager9 *This, HANDLE handle)
{
    MANAGER(CloseDeviceHandle(m, handle))
}

static HRESULT STDMETHODCALLTYPE manager_TestDevice(IDirect3DDeviceManager9 *This, HANDLE handle)
{
    MANAGER(TestDevice(m, handle))
}

static HRESULT STDMETHODCALLTYPE manager_LockDevice(IDirect3DDeviceManager9 *This, HANDLE handle,
                                                    IDirect3DDevice9 **device, BOOL block)
{
    MANAGER(LockDevice(m, handle, device, block))
}

static HRESULT STDMETHODCALLTYPE manager_UnlockDevice(IDirect3DDeviceManager9 *This, HANDLE handle, BOOL save)
{
    MANAGER(UnlockDevice(m, handle, save))
}

static HRESULT STDMETHODCALLTYPE manager_GetVideoService(IDirect3DDeviceManager9 *This, HANDLE handle, REFIID riid,
                                                         void **service)
{
    MANAGER(GetVideoService(m, handle, riid, service))
}

static const IDirect3DDeviceManager9Vtbl manager_vtbl = {
    manager_QueryInterface,   manager_AddRef,            manager_Release,    manager_ResetDevice,
    manager_OpenDeviceHandle, manager_CloseDeviceHandle, manager_TestDevice, manager_LockDevice,
    manager_UnlockDevice,     manager_GetVideoService,
};

/* ------------------------------------------------------------------ creation */

HRESULT EvrCreatePresenter(IUnknown *outer, REFIID riid, void **out, BOOL flip)
{
    Presenter *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p)
        return E_OUTOFMEMORY;
    p->inner.lpVtbl = &inner_vtbl;
    p->device_id.lpVtbl = &device_id_vtbl;
    p->presenter.lpVtbl = &presenter_vtbl;
    p->display.lpVtbl = &display_vtbl;
    p->rate.lpVtbl = &rate_vtbl;
    p->get_service.lpVtbl = &get_service_vtbl;
    p->lookup.lpVtbl = &lookup_vtbl;
    p->mapper.lpVtbl = &mapper_vtbl;
    p->qualprop.lpVtbl = &qualprop_vtbl;
    p->trusted.lpVtbl = &trusted_vtbl;
    p->quality.lpVtbl = &quality_vtbl;
    p->limits.lpVtbl = &limits_vtbl;
    p->manager_iface.lpVtbl = &manager_vtbl;
    p->statistics.lpVtbl = &statistics_vtbl;
    p->outer = outer ? outer : &p->inner;
    p->refs = 1;
    p->flip = flip;
    p->lookup_state = 1;
    p->aspect = MFVideoARMode_PreservePicture | MFVideoARMode_PreservePixel;
    p->src = (MFVideoNormalizedRect){0, 0, 1, 1};
    p->rate_now = 1.0f;
    p->frame = 333333;
    p->state = RENDER_STOPPED;
    p->constriction = -1;
    InitializeCriticalSection(&p->lock);
    EvrObjectCreated();
    HRESULT hr = EvrCreateTearless(nullptr, &IID_IEVRTearlessWindowPresenter9, (void **)&p->helper);
    if (SUCCEEDED(hr))
        hr = make_device(p);
    if (SUCCEEDED(hr)) {
        p->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        p->thread = p->wake ? CreateThread(nullptr, 0, scheduler, p, 0, nullptr) : nullptr;
        if (!p->thread)
            hr = HRESULT_FROM_WIN32(GetLastError());
    }
    if (SUCCEEDED(hr))
        hr = query(p, riid, out);
    p->inner.lpVtbl->Release(&p->inner);
    return hr;
}
