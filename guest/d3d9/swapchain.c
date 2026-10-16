/*
 * IDirect3DSwapChain9Ex: the back buffers programs render to, typeless so
 * srgb writes may view them, and a dxgi swap chain on the window, whose
 * buffer the first back buffer is blitted to when presented, resolved
 * when multisampled. dxgi scales it to the window's client area, as
 * direct3d 9 does in windows.
 */
#include "d3d9_private.h"

static D9SwapChain *impl(IDirect3DSwapChain9Ex *iface) { return CONTAINING_RECORD(iface, D9SwapChain, iface); }

/* what direct3d 9 fills in itself: sizes of the window, the display's format, one buffer. */
static HRESULT normalize(D9Device *device, D3DPRESENT_PARAMETERS *params, HWND *window)
{
    *window = params->hDeviceWindow ? params->hDeviceWindow : device->creation.hFocusWindow;
    if (!*window)
        return D3DERR_INVALIDCALL;
    if (params->BackBufferCount > (device->ex ? D3DPRESENT_BACK_BUFFERS_MAX_EX : D3DPRESENT_BACK_BUFFERS_MAX))
        return D3DERR_INVALIDCALL;
    if (params->SwapEffect < D3DSWAPEFFECT_DISCARD || params->SwapEffect > D3DSWAPEFFECT_FLIPEX ||
        (params->SwapEffect == D3DSWAPEFFECT_COPY && params->BackBufferCount > 1))
        return D3DERR_INVALIDCALL;
    if (params->Windowed) {
        RECT client = {0};
        GetClientRect(*window, &client);
        if (!params->BackBufferWidth)
            params->BackBufferWidth = D9_MAX(client.right, 1);
        if (!params->BackBufferHeight)
            params->BackBufferHeight = D9_MAX(client.bottom, 1);
        if (params->BackBufferFormat == D3DFMT_UNKNOWN)
            params->BackBufferFormat = D3DFMT_X8R8G8B8;
    } else if (!params->BackBufferWidth || !params->BackBufferHeight ||
               params->BackBufferFormat == D3DFMT_UNKNOWN) {
        return D3DERR_INVALIDCALL;
    }
    if (!params->BackBufferCount)
        params->BackBufferCount = 1;
    const D9Format *f = D9GetFormat(params->BackBufferFormat);
    if (!(f->flags & D9_FORMAT_RENDER) || (f->flags & (D9_FORMAT_DEPTH | D9_FORMAT_NULL)))
        return D3DERR_INVALIDCALL;
    return D3D_OK;
}

static void release_buffers(D9SwapChain *sc)
{
    for (UINT i = 0; i < sc->back_buffer_count; i++)
        D9DestroyImplicitSurface(sc->back_buffers[i]);
    sc->back_buffer_count = 0;
    if (sc->dxgi_buffer)
        sc->dxgi_buffer->lpVtbl->Release(sc->dxgi_buffer);
    sc->dxgi_buffer = NULL;
}

static HRESULT make_buffers(D9SwapChain *sc)
{
    D3DPRESENT_PARAMETERS *p = &sc->params;
    HRESULT hr;
    for (UINT i = 0; i < p->BackBufferCount; i++) {
        D9Surface *s;
        if (FAILED(hr = D9NewSurface(sc->device, p->BackBufferFormat, D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT,
                                     p->BackBufferWidth, p->BackBufferHeight, &s)))
            return hr;
        s->base.implicit = TRUE;
        s->swapchain = sc;
        s->samples = p->MultiSampleType;
        s->quality = p->MultiSampleQuality;
        s->lockable = !!(p->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER);
        sc->back_buffers[sc->back_buffer_count++] = s;
        if (FAILED(hr = D9SurfaceMakeTexture(s)))
            return hr;
    }
    return sc->dxgi->lpVtbl->GetBuffer(sc->dxgi, 0, &IID_ID3D11Texture2D, (void **)&sc->dxgi_buffer);
}

static DXGI_FORMAT dxgi_format(D3DFORMAT format)
{
    return format == D3DFMT_A2R10G10B10 || format == D3DFMT_A2B10G10R10 ? DXGI_FORMAT_R10G10B10A2_UNORM
                                                                        : DXGI_FORMAT_B8G8R8A8_UNORM;
}

HRESULT D9CreateSwapChain(D9Device *device, D3DPRESENT_PARAMETERS *params, D9SwapChain **out)
{
    HWND window;
    HRESULT hr = normalize(device, params, &window);
    if (FAILED(hr))
        return hr;
    D9SwapChain *sc = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sc));
    if (!sc)
        return E_OUTOFMEMORY;
    extern const IDirect3DSwapChain9ExVtbl D9SwapChainVtbl;
    sc->iface.lpVtbl = &D9SwapChainVtbl;
    sc->device = device;
    sc->params = *params;
    sc->window = window;
    for (UINT i = 0; i < 256; i++)
        sc->gamma.red[i] = sc->gamma.green[i] = sc->gamma.blue[i] = (WORD)(i * 257);
    DXGI_SWAP_CHAIN_DESC desc = {0};
    desc.BufferDesc.Width = params->BackBufferWidth;
    desc.BufferDesc.Height = params->BackBufferHeight;
    desc.BufferDesc.RefreshRate.Numerator = params->FullScreen_RefreshRateInHz;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferDesc.Format = dxgi_format(params->BackBufferFormat);
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = window;
    desc.Windowed = params->Windowed;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    IDXGIFactory *factory = device->factory;
    if (FAILED(hr = factory->lpVtbl->CreateSwapChain(factory, (IUnknown *)device->d3d11, &desc, &sc->dxgi)) ||
        FAILED(hr = make_buffers(sc))) {
        D9DestroySwapChain(sc);
        return hr;
    }
    factory->lpVtbl->MakeWindowAssociation(factory, window, DXGI_MWA_NO_WINDOW_CHANGES);
    *out = sc;
    return D3D_OK;
}

void D9DestroySwapChain(D9SwapChain *sc)
{
    release_buffers(sc);
    if (sc->dxgi) {
        sc->dxgi->lpVtbl->SetFullscreenState(sc->dxgi, FALSE, NULL);
        sc->dxgi->lpVtbl->Release(sc->dxgi);
    }
    HeapFree(GetProcessHeap(), 0, sc);
}

/* whether a program still holds a back buffer, which Reset refuses. */
BOOL D9SwapChainInUse(const D9SwapChain *sc)
{
    for (UINT i = 0; i < sc->back_buffer_count; i++)
        if (sc->back_buffers[i]->base.refs)
            return TRUE;
    return FALSE;
}

HRESULT D9ResetSwapChain(D9SwapChain *sc, D3DPRESENT_PARAMETERS *params)
{
    HWND window;
    HRESULT hr = normalize(sc->device, params, &window);
    if (FAILED(hr))
        return hr;
    release_buffers(sc);
    if (params->Windowed != sc->params.Windowed)
        sc->dxgi->lpVtbl->SetFullscreenState(sc->dxgi, !params->Windowed, NULL);
    sc->params = *params;
    sc->window = window;
    if (FAILED(hr = sc->dxgi->lpVtbl->ResizeBuffers(sc->dxgi, 0, params->BackBufferWidth, params->BackBufferHeight,
                                                    dxgi_format(params->BackBufferFormat), 0)))
        return hr;
    return make_buffers(sc);
}

HRESULT D9Present(D9SwapChain *sc, const RECT *source, const RECT *dest, HWND window, DWORD flags)
{
    D9Device *device = sc->device;
    D9Surface *back = sc->back_buffers[0];
    if ((source || dest || (window && window != sc->window)) && !sc->warned_rects) {
        D9Log("d3d9: present rectangles and other windows are not supported yet");
        sc->warned_rects = TRUE;
    }
    if (back->texture && sc->dxgi_buffer)
        device->mime->lpVtbl->Blit(device->mime, (ID3D11Resource *)sc->dxgi_buffer, 0, NULL,
                                   (ID3D11Resource *)back->texture, 0, NULL, FALSE);
    UINT interval;
    switch (sc->params.PresentationInterval) {
    case D3DPRESENT_INTERVAL_IMMEDIATE:
        interval = 0;
        break;
    case D3DPRESENT_INTERVAL_TWO:
        interval = 2;
        break;
    case D3DPRESENT_INTERVAL_THREE:
        interval = 3;
        break;
    case D3DPRESENT_INTERVAL_FOUR:
        interval = 4;
        break;
    default:
        interval = 1;
        break;
    }
    if (flags & D3DPRESENT_FORCEIMMEDIATE)
        interval = 0;
    HRESULT hr = sc->dxgi->lpVtbl->Present(sc->dxgi, interval, 0);
    sc->present_count++;
    for (UINT i = 0; i < sc->back_buffer_count; i++)
        D9SurfaceWritten(sc->back_buffers[i]);
    if (hr == DXGI_STATUS_OCCLUDED)
        return S_PRESENT_OCCLUDED;
    return FAILED(hr) ? D3DERR_DEVICELOST : D3D_OK;
}

/* ------------------------------------------------------------------ IDirect3DSwapChain9Ex */

static HRESULT STDMETHODCALLTYPE sc_query(IDirect3DSwapChain9Ex *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DSwapChain9) ||
        (impl(iface)->device->ex && IsEqualGUID(riid, &IID_IDirect3DSwapChain9Ex))) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

/* the implicit swap chain lives as long as its device: its references are the device's. */
static ULONG STDMETHODCALLTYPE sc_add_ref(IDirect3DSwapChain9Ex *iface)
{
    D9SwapChain *sc = impl(iface);
    ULONG refs = InterlockedIncrement(&sc->refs);
    if (refs == 1)
        sc->device->iface.lpVtbl->AddRef(&sc->device->iface);
    return refs;
}

static ULONG STDMETHODCALLTYPE sc_release(IDirect3DSwapChain9Ex *iface)
{
    D9SwapChain *sc = impl(iface);
    ULONG refs = InterlockedDecrement(&sc->refs);
    if (!refs) {
        D9Device *device = sc->device;
        if (!sc->implicit) {
            D9Lock(device);
            D9ForgetSwapChain(device, sc);
            D9DestroySwapChain(sc);
            D9Unlock(device);
        }
        device->iface.lpVtbl->Release(&device->iface);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE sc_present(IDirect3DSwapChain9Ex *iface, const RECT *source, const RECT *dest,
                                            HWND window, const RGNDATA *dirty, DWORD flags)
{
    D9SwapChain *sc = impl(iface);
    D9Lock(sc->device);
    HRESULT hr = D9Present(sc, source, dest, window, flags);
    D9Unlock(sc->device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sc_get_front_buffer_data(IDirect3DSwapChain9Ex *iface, IDirect3DSurface9 *dest)
{
    D9SwapChain *sc = impl(iface);
    D9Surface *d = D9SurfaceFrom(dest), *back = sc->back_buffers[0];
    if (!d || d->base.pool != D3DPOOL_SYSTEMMEM || d->width < back->width || d->height < back->height)
        return D3DERR_INVALIDCALL;
    D9Lock(sc->device);
    /* the last frame presented is what the back buffer held; its pixels in the destination's 32-bit format. */
    HRESULT hr = D9SurfaceDownload(back);
    if (SUCCEEDED(hr))
        for (UINT y = 0; y < back->height; y++)
            memcpy(d->memory->bytes + (SIZE_T)y * d->memory->pitch, back->memory->bytes + (SIZE_T)y * back->memory->pitch,
                   (SIZE_T)back->width * D9_MIN(back->base.format->bytes, d->base.format->bytes));
    D9Unlock(sc->device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sc_get_back_buffer(IDirect3DSwapChain9Ex *iface, UINT index,
                                                    D3DBACKBUFFER_TYPE type, IDirect3DSurface9 **out)
{
    D9SwapChain *sc = impl(iface);
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = NULL;
    if (index >= sc->back_buffer_count || type > D3DBACKBUFFER_TYPE_RIGHT)
        return D3DERR_INVALIDCALL;
    *out = &sc->back_buffers[index]->iface;
    D9SurfaceAddRef(sc->back_buffers[index]);
    return D3D_OK;
}

/* the scanline from the time within the refresh: a guess, with no vertical blank to read. */
HRESULT D9RasterStatus(D9SwapChain *sc, D3DRASTER_STATUS *status)
{
    if (!status)
        return D3DERR_INVALIDCALL;
    DEVMODEW dm = {0};
    dm.dmSize = sizeof(dm);
    UINT rate = EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1
                    ? dm.dmDisplayFrequency
                    : 60;
    UINT lines = dm.dmPelsHeight ? dm.dmPelsHeight : 1080;
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    UINT64 period = freq.QuadPart / rate;
    UINT64 at = period ? now.QuadPart % period : 0;
    UINT total = lines + lines / 20;
    UINT line = period ? (UINT)(at * total / period) : 0;
    status->InVBlank = line >= lines;
    status->ScanLine = status->InVBlank ? 0 : line;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_raster_status(IDirect3DSwapChain9Ex *iface, D3DRASTER_STATUS *status)
{
    return D9RasterStatus(impl(iface), status);
}

static HRESULT STDMETHODCALLTYPE sc_get_display_mode(IDirect3DSwapChain9Ex *iface, D3DDISPLAYMODE *mode)
{
    D9SwapChain *sc = impl(iface);
    return sc->device->d3d->lpVtbl->GetAdapterDisplayMode(sc->device->d3d, sc->device->creation.AdapterOrdinal,
                                                          mode);
}

static HRESULT STDMETHODCALLTYPE sc_get_device(IDirect3DSwapChain9Ex *iface, IDirect3DDevice9 **device)
{
    D9SwapChain *sc = impl(iface);
    if (!device)
        return D3DERR_INVALIDCALL;
    *device = (IDirect3DDevice9 *)&sc->device->iface;
    sc->device->iface.lpVtbl->AddRef(&sc->device->iface);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_present_parameters(IDirect3DSwapChain9Ex *iface,
                                                           D3DPRESENT_PARAMETERS *params)
{
    if (!params)
        return D3DERR_INVALIDCALL;
    *params = impl(iface)->params;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_last_present_count(IDirect3DSwapChain9Ex *iface, UINT *count)
{
    if (!count)
        return D3DERR_INVALIDCALL;
    *count = impl(iface)->present_count;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_present_stats(IDirect3DSwapChain9Ex *iface, D3DPRESENTSTATS *stats)
{
    if (!stats)
        return D3DERR_INVALIDCALL;
    memset(stats, 0, sizeof(*stats));
    stats->PresentCount = impl(iface)->present_count;
    QueryPerformanceCounter(&stats->SyncQPCTime);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_display_mode_ex(IDirect3DSwapChain9Ex *iface, D3DDISPLAYMODEEX *mode,
                                                        D3DDISPLAYROTATION *rotation)
{
    D9SwapChain *sc = impl(iface);
    return sc->device->d3d->lpVtbl->GetAdapterDisplayModeEx(sc->device->d3d, sc->device->creation.AdapterOrdinal,
                                                            mode, rotation);
}

const IDirect3DSwapChain9ExVtbl D9SwapChainVtbl = {
    sc_query,
    sc_add_ref,
    sc_release,
    sc_present,
    sc_get_front_buffer_data,
    sc_get_back_buffer,
    sc_get_raster_status,
    sc_get_display_mode,
    sc_get_device,
    sc_get_present_parameters,
    sc_get_last_present_count,
    sc_get_present_stats,
    sc_get_display_mode_ex,
};
