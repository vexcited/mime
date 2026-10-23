/*
 * IDirect3DDevice8 over a Direct3D 9 device: lifetime, presentation and reset, resource creation with Direct3D 8's
 * checks, surface copies, render targets, scenes and clears.
 */
#include "d3d8_private.h"

static D8Device *impl(IDirect3DDevice8 *iface) { return CONTAINING_RECORD(iface, D8Device, iface); }

D8Device *D8DeviceFrom(IDirect3DDevice8 *iface) { return impl(iface); }

void D8Enter(D8Device *d)
{
    if (d->multithreaded)
        EnterCriticalSection(&d->cs);
}

void D8Leave(D8Device *d)
{
    if (d->multithreaded)
        LeaveCriticalSection(&d->cs);
}

/* without D3DCREATE_FPU_PRESERVE the thread's x87 unit goes to single precision, rounding to nearest, quiet. */
static void set_fpu(D8Device *d)
{
    if (d->flags & D3DCREATE_FPU_PRESERVE)
        return;
#if defined(__i386__)
    WORD cw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    cw = (cw & ~0x0F00) | 0x003F;
    __asm__ volatile("fldcw %0" : : "m"(cw));
#endif
}

/* ------------------------------------------------------------------ presentation parameters */

HRESULT D8ConvertPresent(D8Device *d, D8Direct3D *d3d, UINT adapter, D3DDEVTYPE type, HWND focus,
                         D3DPRESENT_PARAMETERS8 *in, D3DPRESENT_PARAMETERS *out)
{
    IDirect3D8 *i8 = &d3d->iface;
    DWORD effect = in->SwapEffect;
    if (effect < D3DSWAPEFFECT_DISCARD || effect > D3DSWAPEFFECT_COPY_VSYNC)
        return D3DERR_INVALIDCALL;
    /* windows fixes up the program's count before failing for it. */
    if (!in->BackBufferCount)
        in->BackBufferCount = 1;
    if ((effect == D3DSWAPEFFECT_COPY || effect == D3DSWAPEFFECT_COPY_VSYNC) && in->BackBufferCount > 1) {
        in->BackBufferCount = 1;
        return D3DERR_INVALIDCALL;
    }
    if (in->BackBufferCount > 3) {
        in->BackBufferCount = 3;
        return D3DERR_INVALIDCALL;
    }
    if (in->MultiSampleType != D3DMULTISAMPLE_NONE &&
        (effect != D3DSWAPEFFECT_DISCARD ||
         FAILED(i8->lpVtbl->CheckDeviceMultiSampleType(i8, adapter, type, in->BackBufferFormat, in->Windowed,
                                                       in->MultiSampleType))))
        return D3DERR_INVALIDCALL;
    D3DFORMAT display = D8DisplayFormat(d3d, adapter);
    if (in->Windowed) {
        D3DFORMAT f = in->BackBufferFormat;
        if (D8NoAlpha(f) != display)
            return D3DERR_INVALIDCALL;
        if ((f != D3DFMT_A8R8G8B8 && f != D3DFMT_X8R8G8B8 && f != D3DFMT_R5G6B5 && f != D3DFMT_X1R5G5B5 &&
             f != D3DFMT_A1R5G5B5) ||
            FAILED(i8->lpVtbl->CheckDeviceFormat(i8, adapter, type, display, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE,
                                                 f)))
            return D3DERR_INVALIDCALL;
    } else if (FAILED(i8->lpVtbl->CheckDeviceType(i8, adapter, type, D8NoAlpha(in->BackBufferFormat),
                                                  in->BackBufferFormat, FALSE))) {
        return D3DERR_INVALIDCALL;
    }
    memset(out, 0, sizeof(*out));
    out->BackBufferWidth = in->BackBufferWidth;
    out->BackBufferHeight = in->BackBufferHeight;
    out->BackBufferFormat = in->BackBufferFormat;
    out->BackBufferCount = in->BackBufferCount;
    out->MultiSampleType = in->MultiSampleType;
    out->SwapEffect = effect == D3DSWAPEFFECT_COPY_VSYNC ? D3DSWAPEFFECT_COPY : effect;
    out->hDeviceWindow = in->hDeviceWindow ? in->hDeviceWindow : focus;
    out->Windowed = in->Windowed;
    out->EnableAutoDepthStencil = in->EnableAutoDepthStencil;
    out->AutoDepthStencilFormat = in->AutoDepthStencilFormat;
    out->Flags = in->Flags;
    out->FullScreen_RefreshRateInHz = in->FullScreen_RefreshRateInHz;
    /* windowed chains blit at once, except with COPY_VSYNC, which waits for the vertical blank. */
    if (in->Windowed)
        out->PresentationInterval =
            effect == D3DSWAPEFFECT_COPY_VSYNC ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    else
        out->PresentationInterval = in->FullScreen_PresentationInterval;
    return D3D_OK;
}

static BOOL valid_interval(UINT interval)
{
    return interval == D3DPRESENT_INTERVAL_DEFAULT || interval == D3DPRESENT_INTERVAL_ONE ||
           interval == D3DPRESENT_INTERVAL_TWO || interval == D3DPRESENT_INTERVAL_THREE ||
           interval == D3DPRESENT_INTERVAL_FOUR || interval == D3DPRESENT_INTERVAL_IMMEDIATE;
}

/* the implicit swap chain's back buffers and the automatic depth buffer, bound as on a new device. */
static HRESULT take_buffers(D8Device *d, const D3DPRESENT_PARAMETERS *pp)
{
    IDirect3DSwapChain9 *sc9;
    HRESULT hr = d->d9->lpVtbl->GetSwapChain(d->d9, 0, &sc9);
    if (FAILED(hr))
        return hr;
    if (d->swapchain) {
        d->swapchain->d9 = (IUnknown *)sc9;
        hr = D8SwapChainTake(d->swapchain, pp);
    } else {
        hr = D8WrapSwapChain(d, sc9, pp, TRUE, &d->swapchain);
    }
    if (FAILED(hr))
        return hr;
    if (pp->EnableAutoDepthStencil) {
        IDirect3DSurface9 *s9;
        hr = d->d9->lpVtbl->GetDepthStencilSurface(d->d9, &s9);
        if (SUCCEEDED(hr))
            hr = D8WrapSurface(d, s9, D3DUSAGE_DEPTHSTENCIL, TRUE, &d->auto_depth);
        if (FAILED(hr))
            return hr;
        d->auto_depth->lockable = pp->AutoDepthStencilFormat == D3DFMT_D16_LOCKABLE;
    }
    D8Bind(&d->target, d->swapchain->buffers[0]);
    D8Bind(&d->depth, d->auto_depth);
    return D3D_OK;
}

static void drop_buffers(D8Device *d)
{
    D8Bind(&d->target, nullptr);
    D8Bind(&d->depth, nullptr);
    if (d->auto_depth)
        D8ObjectUnuse(d->auto_depth);
    d->auto_depth = nullptr;
    if (d->swapchain)
        D8SwapChainDrop(d->swapchain);
}

static void default_gamma(D8Device *d)
{
    for (UINT i = 0; i < 256; i++)
        d->gamma.red[i] = d->gamma.green[i] = d->gamma.blue[i] = i;
}

static void device_caps(D8Device *d)
{
    D8GetCaps(d->d3d, d->adapter, d->type, &d->caps);
    if ((d->caps.DevCaps & D3DDEVCAPS_RTPATCHES) && !(d->flags & D3DCREATE_PUREDEVICE))
        d->caps.DevCaps |= D3DDEVCAPS_NPATCHES;
}

/* ------------------------------------------------------------------ lifetime */

static void destroy_device(D8Device *d)
{
    D8ClearState(d);
    D8DeleteShaders(d);
    drop_buffers(d);
    if (d->swapchain)
        D8ObjectUnuse(d->swapchain);
    d->d9->lpVtbl->Release(d->d9);
    d->d3d->iface.lpVtbl->Release(&d->d3d->iface);
    D8TableFree(&d->vertex_shaders);
    D8TableFree(&d->pixel_shaders);
    D8TableFree(&d->blocks);
    DeleteCriticalSection(&d->cs);
    HeapFree(GetProcessHeap(), 0, d);
}

static HRESULT STDMETHODCALLTYPE device_query(IDirect3DDevice8 *iface, REFIID riid, void **out)
{
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DDevice8)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return D3D_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE device_add_ref(IDirect3DDevice8 *iface) { return InterlockedIncrement(&impl(iface)->refs); }

static ULONG STDMETHODCALLTYPE device_release(IDirect3DDevice8 *iface)
{
    D8Device *d = impl(iface);
    ULONG refs = InterlockedDecrement(&d->refs);
    if (!refs)
        destroy_device(d);
    return refs;
}

/* ------------------------------------------------------------------ simple methods */

static HRESULT STDMETHODCALLTYPE device_test_cooperative_level(IDirect3DDevice8 *iface)
{
    IDirect3DDevice9 *d9 = impl(iface)->d9;
    return d9->lpVtbl->TestCooperativeLevel(d9);
}

/* windows rounds what is left to the nearest mebibyte. */
static UINT STDMETHODCALLTYPE device_get_available_texture_mem(IDirect3DDevice8 *iface)
{
    IDirect3DDevice9 *d9 = impl(iface)->d9;
    return (d9->lpVtbl->GetAvailableTextureMem(d9) + 0x80000) & 0xFFF00000;
}

static HRESULT STDMETHODCALLTYPE device_resource_manager_discard_bytes(IDirect3DDevice8 *iface, DWORD bytes)
{
    IDirect3DDevice9 *d9 = impl(iface)->d9;
    d9->lpVtbl->EvictManagedResources(d9);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_direct3d(IDirect3DDevice8 *iface, IDirect3D8 **out)
{
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = &impl(iface)->d3d->iface;
    (*out)->lpVtbl->AddRef(*out);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_device_caps(IDirect3DDevice8 *iface, D3DCAPS8 *caps)
{
    if (!caps)
        return D3DERR_INVALIDCALL;
    *caps = impl(iface)->caps;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_display_mode(IDirect3DDevice8 *iface, D3DDISPLAYMODE *mode)
{
    IDirect3DDevice9 *d9 = impl(iface)->d9;
    D3DDISPLAYMODE m;
    if (SUCCEEDED(d9->lpVtbl->GetDisplayMode(d9, 0, &m))) {
        m.Format = D8NoAlpha(m.Format);
        *mode = m;
    }
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_creation_parameters(IDirect3DDevice8 *iface,
                                                               D3DDEVICE_CREATION_PARAMETERS *params)
{
    D8Device *d = impl(iface);
    *params = (D3DDEVICE_CREATION_PARAMETERS){d->adapter, d->type, d->focus, d->creation_flags};
    return D3D_OK;
}

static BOOL power_of_two(UINT v) { return v && !(v & (v - 1)); }

static HRESULT STDMETHODCALLTYPE device_set_cursor_properties(IDirect3DDevice8 *iface, UINT x, UINT y,
                                                             IDirect3DSurface8 *bitmap)
{
    D8Device *d = impl(iface);
    D8Object *s = D8Check(d, bitmap, D8_KIND(D8_SURFACE));
    if (!s)
        return D3DERR_INVALIDCALL;
    D3DDISPLAYMODE mode;
    d->d9->lpVtbl->GetDisplayMode(d->d9, 0, &mode);
    if (s->format != D3DFMT_A8R8G8B8 || !power_of_two(s->width) || !power_of_two(s->height) ||
        s->width > mode.Width || s->height > mode.Height || x >= s->width || y >= s->height)
        return D3DERR_INVALIDCALL;
    return d->d9->lpVtbl->SetCursorProperties(d->d9, x, y, (IDirect3DSurface9 *)s->d9);
}

static void STDMETHODCALLTYPE device_set_cursor_position(IDirect3DDevice8 *iface, int x, int y, DWORD flags)
{
    IDirect3DDevice9 *d9 = impl(iface)->d9;
    d9->lpVtbl->SetCursorPosition(d9, x, y, flags);
}

static BOOL STDMETHODCALLTYPE device_show_cursor(IDirect3DDevice8 *iface, BOOL show)
{
    IDirect3DDevice9 *d9 = impl(iface)->d9;
    return d9->lpVtbl->ShowCursor(d9, show);
}

static BOOL fullscreen(D8Device *d)
{
    IDirect3DSwapChain9 *sc9 = d->swapchain ? (IDirect3DSwapChain9 *)d->swapchain->d9 : nullptr;
    D3DPRESENT_PARAMETERS pp;
    return sc9 && SUCCEEDED(sc9->lpVtbl->GetPresentParameters(sc9, &pp)) && !pp.Windowed;
}

static HRESULT STDMETHODCALLTYPE device_create_additional_swap_chain(IDirect3DDevice8 *iface,
                                                                    D3DPRESENT_PARAMETERS8 *params,
                                                                    IDirect3DSwapChain8 **out)
{
    D8Device *d = impl(iface);
    *out = nullptr;
    if (!d->swapchain || params->BackBufferFormat == D3DFMT_UNKNOWN || fullscreen(d) || !params->Windowed ||
        (!params->hDeviceWindow && !d->focus))
        return D3DERR_INVALIDCALL;
    D3DPRESENT_PARAMETERS pp;
    HRESULT hr = D8ConvertPresent(d, d->d3d, d->adapter, d->type, d->focus, params, &pp);
    if (FAILED(hr))
        return hr;
    IDirect3DSwapChain9 *sc9;
    hr = d->d9->lpVtbl->CreateAdditionalSwapChain(d->d9, &pp, &sc9);
    if (FAILED(hr))
        return hr;
    D8Object *sc;
    hr = D8WrapSwapChain(d, sc9, &pp, FALSE, &sc);
    if (SUCCEEDED(hr))
        *out = D8_IFACE(sc);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_reset(IDirect3DDevice8 *iface, D3DPRESENT_PARAMETERS8 *params)
{
    D8Device *d = impl(iface);
    if (GetCurrentThreadId() != d->thread)
        return D3DERR_INVALIDCALL;
    if ((!d->focus && (!params->Windowed || !params->hDeviceWindow)) || params->BackBufferFormat == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    D3DPRESENT_PARAMETERS pp;
    HRESULT hr = D8ConvertPresent(d, d->d3d, d->adapter, d->type, d->focus, params, &pp);
    if (FAILED(hr))
        return hr;
    if (d->d9->lpVtbl->TestCooperativeLevel(d->d9) == D3DERR_DEVICELOST)
        return D3DERR_DEVICELOST;
    D8Enter(d);
    if (d->d3d->sdk == 120)
        D8DeleteShaders(d);
    D8ClearState(d);
    d->d9->lpVtbl->SetVertexShader(d->d9, nullptr);
    d->d9->lpVtbl->SetPixelShader(d->d9, nullptr);
    d->d9->lpVtbl->SetDepthStencilSurface(d->d9, nullptr);
    drop_buffers(d);
    /* video memory the program still holds keeps Direct3D 9 from resetting, and the device stays lost. */
    hr = d->d9->lpVtbl->Reset(d->d9, &pp);
    if (FAILED(hr)) {
        D8Leave(d);
        return hr == D3DERR_INVALIDCALL ? D3DERR_DEVICELOST : hr;
    }
    hr = take_buffers(d, &pp);
    if (SUCCEEDED(hr)) {
        if (d->flags & D3DCREATE_MIXED_VERTEXPROCESSING)
            d->d9->lpVtbl->SetSoftwareVertexProcessing(d->d9, FALSE);
        device_caps(d);
        D8DefaultState(d);
        default_gamma(d);
        set_fpu(d);
    }
    D8Leave(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_present(IDirect3DDevice8 *iface, const RECT *source, const RECT *dest,
                                                HWND window, const RGNDATA *dirty)
{
    D8Device *d = impl(iface);
    D8Object *sc = d->swapchain;
    if (!sc || !sc->buffer_count)
        return D3DERR_DEVICELOST;
    if (dirty || (sc->flip && (source || dest)))
        return D3DERR_INVALIDCALL;
    for (UINT i = 0; i < sc->buffer_count; i++)
        if (sc->buffers[i]->locks)
            return D3DERR_INVALIDCALL;
    return d->d9->lpVtbl->Present(d->d9, source, dest, window, nullptr);
}

static HRESULT STDMETHODCALLTYPE device_get_back_buffer(IDirect3DDevice8 *iface, UINT index, D3DBACKBUFFER_TYPE type,
                                                       IDirect3DSurface8 **out)
{
    D8Device *d = impl(iface);
    *out = nullptr;
    return d->swapchain ? D8SwapChainBackBuffer(d->swapchain, index, out) : D3DERR_INVALIDCALL;
}

static HRESULT STDMETHODCALLTYPE device_get_raster_status(IDirect3DDevice8 *iface, D3DRASTER_STATUS *status)
{
    D8Device *d = impl(iface);
    if (!(d->caps.Caps & D3DCAPS_READ_SCANLINE) ||
        FAILED(d->d9->lpVtbl->GetRasterStatus(d->d9, 0, status))) {
        status->InVBlank = FALSE;
        status->ScanLine = 0;
        return d->caps.Caps & D3DCAPS_READ_SCANLINE ? D3DERR_NOTAVAILABLE : D3DERR_INVALIDCALL;
    }
    if (status->InVBlank)
        status->ScanLine = 0;
    return D3D_OK;
}

/* a program reads back the ramp it set; the initial one counts from 0 to 255. */
static void STDMETHODCALLTYPE device_set_gamma_ramp(IDirect3DDevice8 *iface, DWORD flags, const D3DGAMMARAMP *ramp)
{
    D8Device *d = impl(iface);
    if (!ramp)
        return;
    d->gamma = *ramp;
    d->d9->lpVtbl->SetGammaRamp(d->d9, 0, flags, ramp);
}

static void STDMETHODCALLTYPE device_get_gamma_ramp(IDirect3DDevice8 *iface, D3DGAMMARAMP *ramp)
{
    if (ramp)
        *ramp = impl(iface)->gamma;
}

/* ------------------------------------------------------------------ resources */

static BOOL plain_depth(D3DFORMAT f) { return D8IsDepthFormat(f) && f != D3DFMT_D16_LOCKABLE; }

static UINT log2_levels(UINT size)
{
    UINT levels = 0;
    while (size) {
        levels++;
        size >>= 1;
    }
    return levels;
}

/* checks every texture gets before its kind's own rules, like windows' runtime. */
static HRESULT check_texture(D8Device *d, D3DRESOURCETYPE type, DWORD usage, D3DFORMAT format, D3DPOOL pool)
{
    if ((UINT)pool > D3DPOOL_SCRATCH)
        return D3DERR_INVALIDCALL;
    if (pool == D3DPOOL_SCRATCH)
        return !usage && D8FormatBytes(format) && !plain_depth(format) ? D3D_OK : D3DERR_INVALIDCALL;
    if ((usage & ~(D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_DYNAMIC)) ||
        ((usage & D3DUSAGE_DYNAMIC) && pool == D3DPOOL_MANAGED) ||
        ((usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) && pool != D3DPOOL_DEFAULT))
        return D3DERR_INVALIDCALL;
    if ((pool == D3DPOOL_MANAGED || pool == D3DPOOL_SYSTEMMEM) && (!D8FormatBytes(format) || plain_depth(format)))
        return D3DERR_INVALIDCALL;
    IDirect3D8 *i8 = &d->d3d->iface;
    if (FAILED(i8->lpVtbl->CheckDeviceFormat(i8, d->adapter, d->type, D8DisplayFormat(d->d3d, d->adapter), usage,
                                             type, format)))
        return D3DERR_INVALIDCALL;
    return D3D_OK;
}

static HRESULT wrap_texture(D8Device *d, UINT kind, HRESULT hr, IUnknown *t9, DWORD usage, D3DPOOL pool,
                            D3DFORMAT format, void **out)
{
    if (FAILED(hr))
        return hr;
    D8Object *t;
    hr = D8WrapTexture(d, kind, t9, usage, pool, format, &t);
    if (SUCCEEDED(hr))
        *out = D8_IFACE(t);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_create_texture(IDirect3DDevice8 *iface, UINT width, UINT height,
                                                       UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                                       IDirect3DTexture8 **out)
{
    D8Device *d = impl(iface);
    if (format == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    *out = nullptr;
    HRESULT hr = check_texture(d, D3DRTYPE_TEXTURE, usage, format, pool);
    if (FAILED(hr))
        return hr;
    DWORD caps = d->caps.TextureCaps;
    BOOL scratch = pool == D3DPOOL_SCRATCH;
    if (!levels)
        levels = !scratch && !(caps & D3DPTEXTURECAPS_MIPMAP) ? 1 : log2_levels(D8_MAX(width, height));
    if (levels > 32 || (levels > 1 && !(width >> (levels - 1)) && !(height >> (levels - 1))))
        return D3DERR_INVALIDCALL;
    if (!scratch) {
        BOOL pow2 = (caps & D3DPTEXTURECAPS_POW2) && (!(caps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) || levels > 1);
        if ((levels > 1 && !(caps & D3DPTEXTURECAPS_MIPMAP)) ||
            (pow2 && (!power_of_two(width) || !power_of_two(height))) ||
            ((caps & D3DPTEXTURECAPS_SQUAREONLY) && width != height) || width > d->caps.MaxTextureWidth ||
            height > d->caps.MaxTextureHeight)
            return D3DERR_INVALIDCALL;
    }
    if ((D8IsDxt(format) && ((width & 3) || (height & 3))) || !width || !height)
        return D3DERR_INVALIDCALL;
    IDirect3DTexture9 *t9;
    hr = d->d9->lpVtbl->CreateTexture(d->d9, width, height, levels, usage, format, pool, &t9, nullptr);
    return wrap_texture(d, D8_TEXTURE, hr, (IUnknown *)t9, usage, pool, format, (void **)out);
}

static HRESULT STDMETHODCALLTYPE device_create_volume_texture(IDirect3DDevice8 *iface, UINT width, UINT height,
                                                              UINT depth, UINT levels, DWORD usage,
                                                              D3DFORMAT format, D3DPOOL pool,
                                                              IDirect3DVolumeTexture8 **out)
{
    D8Device *d = impl(iface);
    if (format == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    *out = nullptr;
    HRESULT hr = check_texture(d, D3DRTYPE_VOLUMETEXTURE, usage, format, pool);
    if (FAILED(hr) || (usage & ~D3DUSAGE_DYNAMIC))
        return D3DERR_INVALIDCALL;
    DWORD caps = d->caps.TextureCaps;
    BOOL scratch = pool == D3DPOOL_SCRATCH;
    if (!levels)
        levels = scratch || (caps & D3DPTEXTURECAPS_MIPVOLUMEMAP)
                     ? log2_levels(D8_MAX(D8_MAX(width, height), depth))
                     : 1;
    if (levels > 32 ||
        (levels > 1 && !(width >> (levels - 1)) && !(height >> (levels - 1)) && !(depth >> (levels - 1))))
        return D3DERR_INVALIDCALL;
    if (!scratch) {
        UINT max = d->caps.MaxVolumeExtent;
        if (((caps & D3DPTEXTURECAPS_VOLUMEMAP_POW2) &&
             (!power_of_two(width) || !power_of_two(height) || !power_of_two(depth))) ||
            width > max || height > max || depth > max || !(caps & D3DPTEXTURECAPS_VOLUMEMAP) ||
            (levels > 1 && !(caps & D3DPTEXTURECAPS_MIPVOLUMEMAP)))
            return D3DERR_INVALIDCALL;
    }
    if ((D8IsDxt(format) && ((width & 3) || (height & 3))) || !width || !height || !depth)
        return D3DERR_INVALIDCALL;
    IDirect3DVolumeTexture9 *t9;
    hr = d->d9->lpVtbl->CreateVolumeTexture(d->d9, width, height, depth, levels, usage, format, pool, &t9, nullptr);
    return wrap_texture(d, D8_VOLUME_TEXTURE, hr, (IUnknown *)t9, usage, pool, format, (void **)out);
}

static HRESULT STDMETHODCALLTYPE device_create_cube_texture(IDirect3DDevice8 *iface, UINT edge, UINT levels,
                                                            DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                                            IDirect3DCubeTexture8 **out)
{
    D8Device *d = impl(iface);
    if (format == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    *out = nullptr;
    HRESULT hr = check_texture(d, D3DRTYPE_CUBETEXTURE, usage, format, pool);
    if (FAILED(hr))
        return hr;
    DWORD caps = d->caps.TextureCaps;
    BOOL scratch = pool == D3DPOOL_SCRATCH;
    if (!levels)
        levels = scratch || (caps & D3DPTEXTURECAPS_MIPCUBEMAP) ? log2_levels(edge) : 1;
    if (levels > 32 || (levels > 1 && !(edge >> (levels - 1))))
        return D3DERR_INVALIDCALL;
    if (!scratch && (((caps & D3DPTEXTURECAPS_CUBEMAP_POW2) && !power_of_two(edge)) ||
                     edge > d->caps.MaxTextureWidth || edge > d->caps.MaxTextureHeight ||
                     !(caps & D3DPTEXTURECAPS_CUBEMAP) || (levels > 1 && !(caps & D3DPTEXTURECAPS_MIPCUBEMAP))))
        return D3DERR_INVALIDCALL;
    if ((D8IsDxt(format) && (edge & 3)) || !edge)
        return D3DERR_INVALIDCALL;
    IDirect3DCubeTexture9 *t9;
    hr = d->d9->lpVtbl->CreateCubeTexture(d->d9, edge, levels, usage, format, pool, &t9, nullptr);
    return wrap_texture(d, D8_CUBE_TEXTURE, hr, (IUnknown *)t9, usage, pool, format, (void **)out);
}

static UINT fvf_size(DWORD fvf)
{
    static const BYTE position[8] = {12, 12, 16, 16, 20, 24, 28, 32};
    UINT size = position[(fvf & 0xE) >> 1];
    if (fvf & D3DFVF_NORMAL)
        size += 12;
    if (fvf & D3DFVF_PSIZE)
        size += 4;
    if (fvf & D3DFVF_DIFFUSE)
        size += 4;
    if (fvf & D3DFVF_SPECULAR)
        size += 4;
    if (fvf & 0x2000)
        size += 4;
    UINT sets = (fvf >> 8) & 0xF;
    for (UINT i = 0; i < sets; i++) {
        static const BYTE coords[4] = {8, 12, 16, 4};
        size += coords[(fvf >> (16 + 2 * i)) & 3];
    }
    return size;
}

static HRESULT check_buffer(D8Device *d, UINT length, DWORD usage, D3DPOOL pool)
{
    if (!length || (UINT)pool > D3DPOOL_SYSTEMMEM ||
        ((usage & D3DUSAGE_SOFTWAREPROCESSING) &&
         !(d->flags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING))) ||
        ((usage & D3DUSAGE_DYNAMIC) && pool == D3DPOOL_MANAGED))
        return D3DERR_INVALIDCALL;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_create_vertex_buffer(IDirect3DDevice8 *iface, UINT length, DWORD usage,
                                                             DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer8 **out)
{
    D8Device *d = impl(iface);
    if (usage & 0xFFFFFC07)
        return D3DERR_INVALIDCALL;
    *out = nullptr;
    HRESULT hr = check_buffer(d, length, usage, pool);
    if (FAILED(hr) || (fvf && length < fvf_size(fvf)))
        return D3DERR_INVALIDCALL;
    IDirect3DVertexBuffer9 *b9;
    hr = d->d9->lpVtbl->CreateVertexBuffer(d->d9, length, usage, fvf, pool, &b9, nullptr);
    if (FAILED(hr))
        return hr;
    D8Object *b;
    hr = D8WrapBuffer(d, D8_VERTEX_BUFFER, (IUnknown *)b9, length, usage, fvf, pool, D3DFMT_VERTEXDATA, &b);
    if (SUCCEEDED(hr))
        *out = D8_IFACE(b);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_create_index_buffer(IDirect3DDevice8 *iface, UINT length, DWORD usage,
                                                            D3DFORMAT format, D3DPOOL pool,
                                                            IDirect3DIndexBuffer8 **out)
{
    D8Device *d = impl(iface);
    if (usage & 0xFFFFFC07)
        return D3DERR_INVALIDCALL;
    *out = nullptr;
    if ((format != D3DFMT_INDEX16 && format != D3DFMT_INDEX32) || length < (format == D3DFMT_INDEX16 ? 2u : 4u))
        return D3DERR_INVALIDCALL;
    HRESULT hr = check_buffer(d, length, usage, pool);
    if (FAILED(hr))
        return hr;
    IDirect3DIndexBuffer9 *b9;
    hr = d->d9->lpVtbl->CreateIndexBuffer(d->d9, length, usage, format, pool, &b9, nullptr);
    if (FAILED(hr))
        return hr;
    D8Object *b;
    hr = D8WrapBuffer(d, D8_INDEX_BUFFER, (IUnknown *)b9, length, usage, 0, pool, format, &b);
    if (SUCCEEDED(hr))
        *out = D8_IFACE(b);
    return hr;
}

static BOOL swapchain_windowed(D8Device *d) { return !fullscreen(d); }

/* standalone render targets and depth buffers, in video memory. */
static HRESULT create_surface(D8Device *d, UINT width, UINT height, D3DFORMAT format, DWORD usage,
                              D3DMULTISAMPLE_TYPE samples, BOOL lockable, IDirect3DSurface8 **out)
{
    *out = nullptr;
    if ((D8IsDxt(format) && ((width & 3) || (height & 3))) || !width || !height)
        return D3DERR_INVALIDCALL;
    IDirect3D8 *i8 = &d->d3d->iface;
    if (FAILED(i8->lpVtbl->CheckDeviceFormat(i8, d->adapter, d->type, D8DisplayFormat(d->d3d, d->adapter), usage,
                                             D3DRTYPE_SURFACE, format)))
        return D3DERR_INVALIDCALL;
    if (usage & D3DUSAGE_DEPTHSTENCIL)
        lockable = !plain_depth(format);
    if (samples != D3DMULTISAMPLE_NONE) {
        if (lockable)
            return D3DERR_INVALIDCALL;
        HRESULT hr = i8->lpVtbl->CheckDeviceMultiSampleType(i8, d->adapter, d->type, format, swapchain_windowed(d),
                                                            samples);
        if (FAILED(hr))
            return hr;
    }
    IDirect3DSurface9 *s9;
    HRESULT hr = usage & D3DUSAGE_RENDERTARGET
                     ? d->d9->lpVtbl->CreateRenderTarget(d->d9, width, height, format, samples, 0, lockable, &s9,
                                                         nullptr)
                     : d->d9->lpVtbl->CreateDepthStencilSurface(d->d9, width, height, format, samples, 0, FALSE, &s9,
                                                                nullptr);
    if (FAILED(hr))
        return hr;
    D8Object *s;
    hr = D8WrapSurface(d, s9, usage, FALSE, &s);
    if (FAILED(hr))
        return hr;
    s->lockable = lockable;
    *out = D8_IFACE(s);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_create_render_target(IDirect3DDevice8 *iface, UINT width, UINT height,
                                                             D3DFORMAT format, D3DMULTISAMPLE_TYPE samples,
                                                             BOOL lockable, IDirect3DSurface8 **out)
{
    if (format == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    return create_surface(impl(iface), width, height, format, D3DUSAGE_RENDERTARGET, samples, lockable, out);
}

static HRESULT STDMETHODCALLTYPE device_create_depth_stencil_surface(IDirect3DDevice8 *iface, UINT width,
                                                                     UINT height, D3DFORMAT format,
                                                                     D3DMULTISAMPLE_TYPE samples,
                                                                     IDirect3DSurface8 **out)
{
    if (format == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    return create_surface(impl(iface), width, height, format, D3DUSAGE_DEPTHSTENCIL, samples, FALSE, out);
}

/* memory surfaces take any format the runtime knows the size of, without asking the device. */
static HRESULT STDMETHODCALLTYPE device_create_image_surface(IDirect3DDevice8 *iface, UINT width, UINT height,
                                                             D3DFORMAT format, IDirect3DSurface8 **out)
{
    D8Device *d = impl(iface);
    *out = nullptr;
    if (!D8FormatBytes(format) || plain_depth(format) || (D8IsDxt(format) && ((width & 3) || (height & 3))) ||
        !width || !height)
        return D3DERR_INVALIDCALL;
    IDirect3DSurface9 *s9;
    HRESULT hr =
        d->d9->lpVtbl->CreateOffscreenPlainSurface(d->d9, width, height, format, D3DPOOL_SYSTEMMEM, &s9, nullptr);
    if (FAILED(hr))
        return hr == E_OUTOFMEMORY ? hr : D3DERR_INVALIDCALL;
    D8Object *s;
    hr = D8WrapSurface(d, s9, 0, FALSE, &s);
    if (FAILED(hr))
        return hr;
    s->lockable = TRUE;
    *out = D8_IFACE(s);
    return D3D_OK;
}

/* ------------------------------------------------------------------ copies */

static IDirect3DSurface9 *surface9(D8Object *s) { return (IDirect3DSurface9 *)s->d9; }

/* copies a rectangle's rows between two locked surfaces, in blocks for compressed formats. */
static HRESULT cpu_copy(IDirect3DSurface9 *src, const RECT *r, IDirect3DSurface9 *dst, POINT pt, D3DFORMAT format)
{
    RECT to = {pt.x, pt.y, pt.x + (r->right - r->left), pt.y + (r->bottom - r->top)};
    D3DLOCKED_RECT a, b;
    HRESULT hr = src->lpVtbl->LockRect(src, &a, r, D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK);
    if (FAILED(hr))
        return hr;
    hr = dst->lpVtbl->LockRect(dst, &b, &to, D3DLOCK_NOSYSLOCK);
    if (FAILED(hr)) {
        src->lpVtbl->UnlockRect(src);
        return hr;
    }
    UINT width = r->right - r->left, rows = r->bottom - r->top, bytes = D8FormatBytes(format);
    UINT row = width * bytes;
    if (D8IsDxt(format)) {
        row = (width + 3) / 4 * bytes;
        rows = (rows + 3) / 4;
    }
    for (UINT y = 0; y < rows; y++)
        memcpy((BYTE *)b.pBits + (SIZE_T)y * b.Pitch, (const BYTE *)a.pBits + (SIZE_T)y * a.Pitch, row);
    dst->lpVtbl->UnlockRect(dst);
    src->lpVtbl->UnlockRect(src);
    return D3D_OK;
}

static HRESULT copy_rect(D8Device *d, D8Object *src, const RECT *r, D8Object *dst, POINT pt)
{
    IDirect3DDevice9 *d9 = d->d9;
    UINT width = r->right - r->left, height = r->bottom - r->top;
    if (dst->pool == D3DPOOL_DEFAULT) {
        if (src->pool == D3DPOOL_SYSTEMMEM)
            return d9->lpVtbl->UpdateSurface(d9, surface9(src), r, surface9(dst), &pt);
        if (src->pool == D3DPOOL_DEFAULT) {
            RECT to = {pt.x, pt.y, pt.x + width, pt.y + height};
            return d9->lpVtbl->StretchRect(d9, surface9(src), r, surface9(dst), &to, D3DTEXF_NONE);
        }
        /* managed and scratch surfaces go through memory Direct3D 9 updates from. */
        IDirect3DSurface9 *tmp;
        HRESULT hr = d9->lpVtbl->CreateOffscreenPlainSurface(d9, width, height, src->format, D3DPOOL_SYSTEMMEM, &tmp,
                                                             nullptr);
        if (FAILED(hr))
            return hr;
        hr = cpu_copy(surface9(src), r, tmp, (POINT){0, 0}, src->format);
        if (SUCCEEDED(hr))
            hr = d9->lpVtbl->UpdateSurface(d9, tmp, nullptr, surface9(dst), &pt);
        tmp->lpVtbl->Release(tmp);
        return hr;
    }
    if (src->pool == D3DPOOL_DEFAULT) {
        IDirect3DSurface9 *tmp;
        HRESULT hr = d9->lpVtbl->CreateOffscreenPlainSurface(d9, src->width, src->height, src->format,
                                                             D3DPOOL_SYSTEMMEM, &tmp, nullptr);
        if (FAILED(hr))
            return hr;
        hr = d9->lpVtbl->GetRenderTargetData(d9, surface9(src), tmp);
        if (SUCCEEDED(hr))
            hr = cpu_copy(tmp, r, surface9(dst), pt, src->format);
        tmp->lpVtbl->Release(tmp);
        /* surfaces that are not render targets may still lock. */
        if (FAILED(hr))
            hr = cpu_copy(surface9(src), r, surface9(dst), pt, src->format);
        return hr;
    }
    return cpu_copy(surface9(src), r, surface9(dst), pt, src->format);
}

static HRESULT STDMETHODCALLTYPE device_copy_rects(IDirect3DDevice8 *iface, IDirect3DSurface8 *source,
                                                   const RECT *rects, UINT count, IDirect3DSurface8 *dest,
                                                   const POINT *points)
{
    D8Device *d = impl(iface);
    D8Object *src = D8Check(d, source, D8_KIND(D8_SURFACE)), *dst = D8Check(d, dest, D8_KIND(D8_SURFACE));
    if (!src || !dst || src->locks || dst->locks || src->format != dst->format || D8IsDepthFormat(src->format) ||
        (count && !rects))
        return D3DERR_INVALIDCALL;
    if (rects && !count)
        return D3D_OK;
    RECT whole = {0, 0, src->width, src->height};
    if (!rects) {
        if (src->width > dst->width || src->height > dst->height)
            return D3DERR_INVALIDCALL;
        rects = &whole;
        count = 1;
    }
    BOOL dxt = D8IsDxt(src->format);
    for (UINT i = 0; i < count; i++) {
        const RECT *r = &rects[i];
        POINT pt = points ? points[i] : (POINT){r->left, r->top};
        LONG w = r->right - r->left, h = r->bottom - r->top;
        if (r->left >= r->right || r->top >= r->bottom || r->left < 0 || r->top < 0 ||
            (UINT)r->right > src->width || (UINT)r->bottom > src->height || pt.x < 0 || pt.y < 0 ||
            (UINT)(pt.x + w) > dst->width || (UINT)(pt.y + h) > dst->height)
            return D3DERR_INVALIDCALL;
        if (dxt && ((r->left & 3) || (r->top & 3) || ((r->right & 3) && (UINT)r->right != src->width) ||
                    ((r->bottom & 3) && (UINT)r->bottom != src->height) || (pt.x & 3) || (pt.y & 3)))
            return D3DERR_INVALIDCALL;
    }
    D8Enter(d);
    HRESULT hr = D3D_OK;
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++)
        hr = copy_rect(d, src, &rects[i], dst, points ? points[i] : (POINT){rects[i].left, rects[i].top});
    D8Leave(d);
    return FAILED(hr) && hr != D3DERR_DEVICELOST && hr != E_OUTOFMEMORY ? D3DERR_INVALIDCALL : hr;
}

static HRESULT STDMETHODCALLTYPE device_update_texture(IDirect3DDevice8 *iface, IDirect3DBaseTexture8 *source,
                                                       IDirect3DBaseTexture8 *dest)
{
    D8Device *d = impl(iface);
    D8Object *src = D8Check(d, source, D8_TEXTURES), *dst = D8Check(d, dest, D8_TEXTURES);
    if (!src || !dst || src->pool != D3DPOOL_SYSTEMMEM || dst->pool != D3DPOOL_DEFAULT || src->kind != dst->kind ||
        src->levels < dst->levels)
        return D3DERR_INVALIDCALL;
    UINT skip = src->levels - dst->levels;
    if (D8_MAX(1, src->width >> skip) != dst->width || D8_MAX(1, src->height >> skip) != dst->height ||
        D8_MAX(1, src->depth >> skip) != dst->depth)
        return D3DERR_INVALIDCALL;
    return d->d9->lpVtbl->UpdateTexture(d->d9, (IDirect3DBaseTexture9 *)src->d9, (IDirect3DBaseTexture9 *)dst->d9);
}

/* the adapter's whole screen, into a memory surface at least that large. */
static HRESULT STDMETHODCALLTYPE device_get_front_buffer(IDirect3DDevice8 *iface, IDirect3DSurface8 *dest)
{
    D8Device *d = impl(iface);
    D8Object *s = D8Check(d, dest, D8_KIND(D8_SURFACE));
    D3DDISPLAYMODE mode;
    if (!s || FAILED(d->d9->lpVtbl->GetDisplayMode(d->d9, 0, &mode)) || s->format != D3DFMT_A8R8G8B8 ||
        (s->pool != D3DPOOL_SYSTEMMEM && s->pool != D3DPOOL_SCRATCH) || s->width < mode.Width ||
        s->height < mode.Height || !d->swapchain)
        return D3DERR_INVALIDCALL;
    if (!d->swapchain->buffer_count)
        return D3DERR_DEVICELOST;
    return d->d9->lpVtbl->GetFrontBufferData(d->d9, 0, surface9(s));
}

/* ------------------------------------------------------------------ targets */

void D8ResetViewport(D8Device *d)
{
    d->viewport = (D3DVIEWPORT8){0, 0, d->target ? d->target->width : 0, d->target ? d->target->height : 0, 0.0f,
                                 1.0f};
}

HRESULT D8SetRenderTarget(D8Device *d, D8Object *target, D8Object *depth)
{
    if (!target)
        target = d->target;
    if (!target)
        return D3DERR_INVALIDCALL;
    if (depth) {
        if (!(depth->usage & D3DUSAGE_DEPTHSTENCIL) || depth->samples != target->samples ||
            depth->width < target->width || depth->height < target->height)
            return D3DERR_INVALIDCALL;
        IDirect3D8 *i8 = &d->d3d->iface;
        if ((depth->format == D3DFMT_D16_LOCKABLE || D8HasStencil(depth->format)) &&
            FAILED(i8->lpVtbl->CheckDepthStencilMatch(i8, d->adapter, d->type, D8DisplayFormat(d->d3d, d->adapter),
                                                      target->format, depth->format)))
            return D3DERR_INVALIDCALL;
    }
    HRESULT hr = d->d9->lpVtbl->SetRenderTarget(d->d9, 0, surface9(target));
    if (FAILED(hr))
        return hr;
    d->d9->lpVtbl->SetDepthStencilSurface(d->d9, depth ? surface9(depth) : nullptr);
    D8Bind(&d->target, target);
    D8Bind(&d->depth, depth);
    D8ResetViewport(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_render_target(IDirect3DDevice8 *iface, IDirect3DSurface8 *target,
                                                          IDirect3DSurface8 *depth)
{
    D8Device *d = impl(iface);
    D8Object *t = D8Check(d, target, D8_KIND(D8_SURFACE)), *z = D8Check(d, depth, D8_KIND(D8_SURFACE));
    if ((target && (!t || !(t->usage & D3DUSAGE_RENDERTARGET))) || (depth && !z))
        return D3DERR_INVALIDCALL;
    D8Enter(d);
    HRESULT hr = D8SetRenderTarget(d, t, z);
    D8Leave(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_get_render_target(IDirect3DDevice8 *iface, IDirect3DSurface8 **out)
{
    D8Device *d = impl(iface);
    if (!out || !d->target)
        return D3DERR_INVALIDCALL;
    *out = D8_IFACE(d->target);
    D8ObjectAddRef(d->target);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_depth_stencil_surface(IDirect3DDevice8 *iface, IDirect3DSurface8 **out)
{
    D8Device *d = impl(iface);
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = nullptr;
    if (!d->depth)
        return D3DERR_NOTFOUND;
    *out = D8_IFACE(d->depth);
    D8ObjectAddRef(d->depth);
    return D3D_OK;
}

/* ------------------------------------------------------------------ scenes */

static HRESULT STDMETHODCALLTYPE device_begin_scene(IDirect3DDevice8 *iface)
{
    D8Device *d = impl(iface);
    if (d->in_scene || d->recording)
        return D3DERR_INVALIDCALL;
    d->in_scene = TRUE;
    d->d9->lpVtbl->BeginScene(d->d9);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_end_scene(IDirect3DDevice8 *iface)
{
    D8Device *d = impl(iface);
    if (!d->in_scene || d->recording)
        return D3DERR_INVALIDCALL;
    d->in_scene = FALSE;
    d->d9->lpVtbl->EndScene(d->d9);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_clear(IDirect3DDevice8 *iface, DWORD count, const D3DRECT *rects,
                                              DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    D8Device *d = impl(iface);
    if (count > 0x1000)
        return D3DERR_INVALIDCALL;
    if (flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) {
        if (!d->depth)
            return D3DERR_INVALIDCALL;
        if ((flags & D3DCLEAR_STENCIL) && D8FormatBytes(d->depth->format) && !D8HasStencil(d->depth->format))
            return D3DERR_INVALIDCALL;
    }
    if (!(flags & 7))
        return D3DERR_INVALIDCALL;
    /* rectangles given but none counted clear nothing. */
    if (rects && !count)
        return D3D_OK;
    d->d9->lpVtbl->Clear(d->d9, rects ? count : 0, rects, flags & 7, color, z, stencil);
    return D3D_OK;
}

/* DirectX 7 driver queries fail, and drivers answer none of the others. */
static HRESULT STDMETHODCALLTYPE device_get_info(IDirect3DDevice8 *iface, DWORD id, void *info, DWORD size)
{
    if (!info || !size)
        return D3DERR_INVALIDCALL;
    memset(info, 0, size);
    return id < D3DDEVINFOID_VCACHE ? E_FAIL : S_FALSE;
}

/* ------------------------------------------------------------------ creation */

static IDirect3DDevice8Vtbl device_vtbl = {
    .QueryInterface = device_query,
    .AddRef = device_add_ref,
    .Release = device_release,
    .TestCooperativeLevel = device_test_cooperative_level,
    .GetAvailableTextureMem = device_get_available_texture_mem,
    .ResourceManagerDiscardBytes = device_resource_manager_discard_bytes,
    .GetDirect3D = device_get_direct3d,
    .GetDeviceCaps = device_get_device_caps,
    .GetDisplayMode = device_get_display_mode,
    .GetCreationParameters = device_get_creation_parameters,
    .SetCursorProperties = device_set_cursor_properties,
    .SetCursorPosition = device_set_cursor_position,
    .ShowCursor = device_show_cursor,
    .CreateAdditionalSwapChain = device_create_additional_swap_chain,
    .Reset = device_reset,
    .Present = device_present,
    .GetBackBuffer = device_get_back_buffer,
    .GetRasterStatus = device_get_raster_status,
    .SetGammaRamp = device_set_gamma_ramp,
    .GetGammaRamp = device_get_gamma_ramp,
    .CreateTexture = device_create_texture,
    .CreateVolumeTexture = device_create_volume_texture,
    .CreateCubeTexture = device_create_cube_texture,
    .CreateVertexBuffer = device_create_vertex_buffer,
    .CreateIndexBuffer = device_create_index_buffer,
    .CreateRenderTarget = device_create_render_target,
    .CreateDepthStencilSurface = device_create_depth_stencil_surface,
    .CreateImageSurface = device_create_image_surface,
    .CopyRects = device_copy_rects,
    .UpdateTexture = device_update_texture,
    .GetFrontBuffer = device_get_front_buffer,
    .SetRenderTarget = device_set_render_target,
    .GetRenderTarget = device_get_render_target,
    .GetDepthStencilSurface = device_get_depth_stencil_surface,
    .BeginScene = device_begin_scene,
    .EndScene = device_end_scene,
    .Clear = device_clear,
    .GetInfo = device_get_info,
};

static INIT_ONCE vtbl_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK fill_vtbl(INIT_ONCE *once, void *param, void **context)
{
    D8DeviceStateMethods(&device_vtbl);
    D8DeviceShaderMethods(&device_vtbl);
    return TRUE;
}

HRESULT D8CreateDevice(D8Direct3D *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                       D3DPRESENT_PARAMETERS8 *params, IDirect3DDevice8 **out)
{
    *out = nullptr;
    if (params->Windowed ? params->FullScreen_RefreshRateInHz || params->FullScreen_PresentationInterval
                         : !valid_interval(params->FullScreen_PresentationInterval))
        return D3DERR_INVALIDCALL;
    if (params->BackBufferFormat == D3DFMT_UNKNOWN || (params->Flags & ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) ||
        (focus && !IsWindow(focus)) || (params->hDeviceWindow && !IsWindow(params->hDeviceWindow)) ||
        (!focus && (!params->Windowed || !params->hDeviceWindow)) || adapter >= d3d->adapter_count)
        return D3DERR_INVALIDCALL;
    DWORD processing = flags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_HARDWARE_VERTEXPROCESSING |
                                D3DCREATE_MIXED_VERTEXPROCESSING);
    if ((flags & 0x3FFFFE09) ||
        (processing != D3DCREATE_SOFTWARE_VERTEXPROCESSING && processing != D3DCREATE_HARDWARE_VERTEXPROCESSING &&
         processing != D3DCREATE_MIXED_VERTEXPROCESSING) ||
        type == D3DDEVTYPE_SW)
        return D3DERR_INVALIDCALL;
    D3DCAPS8 caps;
    HRESULT hr = D8GetCaps(d3d, adapter, type, &caps);
    if (FAILED(hr))
        return hr == D3DERR_INVALIDDEVICE ? D3DERR_INVALIDCALL : hr;
    if ((processing == D3DCREATE_SOFTWARE_VERTEXPROCESSING && (flags & D3DCREATE_PUREDEVICE)) ||
        (processing != D3DCREATE_SOFTWARE_VERTEXPROCESSING && !(caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT)) ||
        (processing == D3DCREATE_MIXED_VERTEXPROCESSING && (flags & D3DCREATE_PUREDEVICE)) ||
        ((flags & D3DCREATE_PUREDEVICE) && !(caps.DevCaps & D3DDEVCAPS_PUREDEVICE)))
        return D3DERR_INVALIDCALL;
    D3DPRESENT_PARAMETERS pp;
    hr = D8ConvertPresent(nullptr, d3d, adapter, type, focus, params, &pp);
    if (FAILED(hr))
        return hr;
    IDirect3D8 *i8 = &d3d->iface;
    if (params->EnableAutoDepthStencil) {
        hr = i8->lpVtbl->CheckDepthStencilMatch(i8, adapter, type, D8DisplayFormat(d3d, adapter),
                                                params->BackBufferFormat, params->AutoDepthStencilFormat);
        if (FAILED(hr))
            return hr;
    }
    InitOnceExecuteOnce(&vtbl_once, fill_vtbl, nullptr, nullptr);
    D8Device *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return E_OUTOFMEMORY;
    /* drivers that run Direct3D 9 shaders never make pure devices, though the flag is reported. */
    DWORD flags9 = flags & ~(D3DCREATE_PUREDEVICE | 0xC0000000);
    hr = d3d->d9->lpVtbl->CreateDevice(d3d->d9, adapter, D8DeviceType9(type), focus, flags9, &pp, &d->d9);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, d);
        return hr;
    }
    d->iface.lpVtbl = &device_vtbl;
    d->refs = 1;
    d->d3d = d3d;
    i8->lpVtbl->AddRef(i8);
    InitializeCriticalSection(&d->cs);
    d->multithreaded = (flags & D3DCREATE_MULTITHREADED) != 0;
    d->adapter = adapter;
    d->type = type;
    d->focus = focus;
    d->flags = flags9;
    d->creation_flags = flags;
    d->thread = GetCurrentThreadId();
    device_caps(d);
    hr = take_buffers(d, &pp);
    if (FAILED(hr)) {
        device_release(&d->iface);
        return hr;
    }
    D8DefaultState(d);
    default_gamma(d);
    set_fpu(d);
    *out = &d->iface;
    return D3D_OK;
}
