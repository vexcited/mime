/*
 * IDirect3DSwapChain9Ex: the back buffers programs render to, typeless so
 * srgb writes may view them, and a dxgi swap chain on the window whose
 * buffer is the size of the window's client area. presents stretch the
 * first back buffer, or the rectangle asked, over the client area or the
 * rectangle asked, as direct3d 9 does in windows.
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

static void release_output_buffers(D9Output *o)
{
    if (o->buffer)
        o->buffer->lpVtbl->Release(o->buffer);
    if (o->front)
        o->front->lpVtbl->Release(o->front);
    o->buffer = o->front = NULL;
}

static void destroy_output(D9Output *o)
{
    release_output_buffers(o);
    if (o->dxgi) {
        o->dxgi->lpVtbl->SetFullscreenState(o->dxgi, FALSE, NULL);
        o->dxgi->lpVtbl->Release(o->dxgi);
    }
    o->dxgi = NULL;
}

static void release_buffers(D9SwapChain *sc)
{
    for (UINT i = 0; i < sc->back_buffer_count; i++)
        D9DestroyImplicitSurface(sc->back_buffers[i]);
    sc->back_buffer_count = 0;
    release_output_buffers(&sc->output);
    if (sc->resolved)
        sc->resolved->lpVtbl->Release(sc->resolved);
    sc->resolved = NULL;
}

static DXGI_FORMAT dxgi_format(D3DFORMAT format)
{
    return format == D3DFMT_A2R10G10B10 || format == D3DFMT_A2B10G10R10 ? DXGI_FORMAT_R10G10B10A2_UNORM
                                                                        : DXGI_FORMAT_B8G8R8A8_UNORM;
}

/* the size a window's buffers have: its client area in windows, the back buffer's in fullscreen. */
static void output_size(const D9SwapChain *sc, HWND window, UINT *width, UINT *height)
{
    RECT client = {0};
    if (sc->params.Windowed && GetClientRect(window, &client)) {
        *width = D9_MAX(client.right - client.left, 1);
        *height = D9_MAX(client.bottom - client.top, 1);
    } else {
        *width = sc->params.BackBufferWidth;
        *height = sc->params.BackBufferHeight;
    }
}

static HRESULT make_output(D9SwapChain *sc, HWND window, D9Output *o)
{
    D3DPRESENT_PARAMETERS *params = &sc->params;
    memset(o, 0, sizeof(*o));
    o->window = window;
    output_size(sc, window, &o->width, &o->height);
    DXGI_SWAP_CHAIN_DESC desc = {0};
    desc.BufferDesc.Width = o->width;
    desc.BufferDesc.Height = o->height;
    desc.BufferDesc.RefreshRate.Numerator = params->FullScreen_RefreshRateInHz;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferDesc.Format = dxgi_format(params->BackBufferFormat);
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = window;
    desc.Windowed = window == sc->window ? params->Windowed : TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    IDXGIFactory *factory = sc->device->factory;
    HRESULT hr = factory->lpVtbl->CreateSwapChain(factory, (IUnknown *)sc->device->d3d11, &desc, &o->dxgi);
    if (FAILED(hr))
        return hr;
    factory->lpVtbl->MakeWindowAssociation(factory, window, DXGI_MWA_NO_WINDOW_CHANGES);
    return o->dxgi->lpVtbl->GetBuffer(o->dxgi, 0, &IID_ID3D11Texture2D, (void **)&o->buffer);
}

/* the buffers follow the window's client area, which programs resize without telling direct3d. */
static HRESULT fit_output(D9SwapChain *sc, D9Output *o)
{
    UINT width, height;
    output_size(sc, o->window, &width, &height);
    if (o->buffer && width == o->width && height == o->height)
        return S_OK;
    release_output_buffers(o);
    HRESULT hr = o->dxgi->lpVtbl->ResizeBuffers(o->dxgi, 0, width, height,
                                                dxgi_format(sc->params.BackBufferFormat), 0);
    if (FAILED(hr))
        return hr;
    o->width = width;
    o->height = height;
    return o->dxgi->lpVtbl->GetBuffer(o->dxgi, 0, &IID_ID3D11Texture2D, (void **)&o->buffer);
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
    return fit_output(sc, &sc->output);
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
    if (FAILED(hr = make_output(sc, window, &sc->output)) || FAILED(hr = make_buffers(sc))) {
        D9DestroySwapChain(sc);
        return hr;
    }
    *out = sc;
    return D3D_OK;
}

void D9DestroySwapChain(D9SwapChain *sc)
{
    release_buffers(sc);
    destroy_output(&sc->output);
    for (UINT i = 0; i < sc->other_count; i++)
        destroy_output(&sc->others[i]);
    HeapFree(GetProcessHeap(), 0, sc->others);
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
        sc->output.dxgi->lpVtbl->SetFullscreenState(sc->output.dxgi, !params->Windowed, NULL);
    sc->params = *params;
    if (window != sc->window) {
        destroy_output(&sc->output);
        sc->window = window;
        if (FAILED(hr = make_output(sc, window, &sc->output)))
            return hr;
    }
    return make_buffers(sc);
}

/* the output of a window presents name, made the first time. */
static D9Output *output_of(D9SwapChain *sc, HWND window)
{
    if (!window || window == sc->window)
        return &sc->output;
    for (UINT i = 0; i < sc->other_count; i++)
        if (sc->others[i].window == window)
            return &sc->others[i];
    D9Output *list = HeapAlloc(GetProcessHeap(), 0, (sc->other_count + 1) * sizeof(*list));
    if (!list)
        return NULL;
    memcpy(list, sc->others, sc->other_count * sizeof(*list));
    if (FAILED(make_output(sc, window, &list[sc->other_count]))) {
        destroy_output(&list[sc->other_count]);
        HeapFree(GetProcessHeap(), 0, list);
        return NULL;
    }
    HeapFree(GetProcessHeap(), 0, sc->others);
    sc->others = list;
    return &sc->others[sc->other_count++];
}

/*
 * the parts of the source and destination rectangles that are inside
 * their surfaces, cut in proportion so the stretch stays the same.
 */
static BOOL clip_stretch(RECT *src, RECT *dst, LONG src_w, LONG src_h, LONG dst_w, LONG dst_h)
{
    if (src->right <= src->left || src->bottom <= src->top || dst->right <= dst->left || dst->bottom <= dst->top)
        return FALSE;
    LONG *s[4] = {&src->left, &src->top, &src->right, &src->bottom};
    LONG *d[4] = {&dst->left, &dst->top, &dst->right, &dst->bottom};
    const LONG src_size[2] = {src_w, src_h}, dst_size[2] = {dst_w, dst_h};
    for (UINT axis = 0; axis < 2; axis++) {
        float scale = (float)(*s[axis + 2] - *s[axis]) / (float)(*d[axis + 2] - *d[axis]);
        if (*d[axis] < 0) {
            *s[axis] += (LONG)(-*d[axis] * scale + 0.5f);
            *d[axis] = 0;
        }
        if (*d[axis + 2] > dst_size[axis]) {
            *s[axis + 2] -= (LONG)((*d[axis + 2] - dst_size[axis]) * scale + 0.5f);
            *d[axis + 2] = dst_size[axis];
        }
        if (*s[axis] < 0) {
            *d[axis] += (LONG)(-*s[axis] / scale + 0.5f);
            *s[axis] = 0;
        }
        if (*s[axis + 2] > src_size[axis]) {
            *d[axis + 2] -= (LONG)((*s[axis + 2] - src_size[axis]) / scale + 0.5f);
            *s[axis + 2] = src_size[axis];
        }
    }
    return src->right > src->left && src->bottom > src->top && dst->right > dst->left && dst->bottom > dst->top;
}

/* the back buffer as a single sampled texture, resolved when it is multisampled. */
static ID3D11Resource *present_source(D9SwapChain *sc, D9Surface *back)
{
    if (!back->texture)
        return NULL;
    D3D11_TEXTURE2D_DESC desc;
    back->texture->lpVtbl->GetDesc(back->texture, &desc);
    if (desc.SampleDesc.Count <= 1)
        return (ID3D11Resource *)back->texture;
    D9Device *device = sc->device;
    if (!sc->resolved) {
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = 0;
        if (FAILED(device->d3d11->lpVtbl->CreateTexture2D(device->d3d11, &desc, NULL, &sc->resolved)))
            return NULL;
    }
    device->mime->lpVtbl->Blit(device->mime, (ID3D11Resource *)sc->resolved, 0, NULL, (ID3D11Resource *)back->texture,
                               0, NULL, FALSE);
    return (ID3D11Resource *)sc->resolved;
}

HRESULT D9Present(D9SwapChain *sc, const RECT *source, const RECT *dest, HWND window, const RGNDATA *dirty,
                  DWORD flags)
{
    D9Device *device = sc->device;
    D9Surface *back = sc->back_buffers[0];
    D3DSWAPEFFECT effect = sc->params.SwapEffect;
    /* as d3d9.dll checks: flips take no rectangles, and only copies and flipex dirty regions. */
    if ((effect == D3DSWAPEFFECT_FLIP && (source || dest || dirty)) ||
        (effect == D3DSWAPEFFECT_FLIPEX && (source || dest || window)) ||
        (dirty && effect != D3DSWAPEFFECT_COPY && effect != D3DSWAPEFFECT_FLIPEX))
        return D3DERR_INVALIDCALL;
    D9Output *o = output_of(sc, window);
    if (!o)
        return D3DERR_INVALIDCALL;
    if (FAILED(fit_output(sc, o)))
        return D3DERR_DEVICELOST;
    ID3D11Resource *src = present_source(sc, back);
    if (src && o->buffer) {
        RECT sr = source ? *source : (RECT){0, 0, (LONG)back->width, (LONG)back->height};
        RECT dr = dest ? *dest : (RECT){0, 0, (LONG)o->width, (LONG)o->height};
        BOOL whole = dr.left <= 0 && dr.top <= 0 && dr.right >= (LONG)o->width && dr.bottom >= (LONG)o->height;
        if (clip_stretch(&sr, &dr, back->width, back->height, o->width, o->height)) {
            BOOL linear = sr.right - sr.left != dr.right - dr.left || sr.bottom - sr.top != dr.bottom - dr.top;
            if (!whole && !o->front) {
                /* what the window showed is gone with dxgi's discarded buffers: the back buffer stands for it, a guess. */
                D3D11_TEXTURE2D_DESC desc;
                o->buffer->lpVtbl->GetDesc(o->buffer, &desc);
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                desc.MiscFlags = 0;
                if (SUCCEEDED(device->d3d11->lpVtbl->CreateTexture2D(device->d3d11, &desc, NULL, &o->front)))
                    device->mime->lpVtbl->Blit(device->mime, (ID3D11Resource *)o->front, 0, NULL, src, 0, NULL, TRUE);
            }
            if (o->front) {
                device->mime->lpVtbl->Blit(device->mime, (ID3D11Resource *)o->front, 0, &dr, src, 0, &sr, linear);
                device->context->lpVtbl->CopyResource(device->context, (ID3D11Resource *)o->buffer,
                                                      (ID3D11Resource *)o->front);
            } else {
                device->mime->lpVtbl->Blit(device->mime, (ID3D11Resource *)o->buffer, 0, &dr, src, 0, &sr, linear);
            }
        }
    }
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
    HRESULT hr = o->dxgi->lpVtbl->Present(o->dxgi, interval, 0);
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
    HRESULT hr = D9Present(sc, source, dest, window, dirty, flags);
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
