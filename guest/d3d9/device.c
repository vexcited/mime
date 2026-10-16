/*
 * IDirect3DDevice9Ex: a d3d11 device and its immediate context, the
 * implicit swap chain and the state direct3d 9 keeps. methods take the
 * device's lock; a device made without D3DCREATE_MULTITHREADED does not
 * need it, but direct3d 9 programs call from several threads anyway.
 */
#include "d3d9_private.h"

D9Device *D9DeviceFrom(IDirect3DDevice9 *iface)
{
    return iface ? CONTAINING_RECORD((IDirect3DDevice9Ex *)iface, D9Device, iface) : NULL;
}

static D9Device *impl(IDirect3DDevice9Ex *iface) { return CONTAINING_RECORD(iface, D9Device, iface); }

void D9Lock(D9Device *device) { EnterCriticalSection(&device->lock); }

void D9Unlock(D9Device *device) { LeaveCriticalSection(&device->lock); }

static HRESULT not_yet(D9Device *device, const char *what)
{
    D9Log("d3d9: %s is not supported yet", what);
    return D3DERR_INVALIDCALL;
}

static DWORD dbits(float f)
{
    DWORD v;
    memcpy(&v, &f, sizeof(v));
    return v;
}

/* ------------------------------------------------------------------ state */

static const D3DMATRIX IDENTITY = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};

/* the state a device starts with and Reset returns to, as direct3d 9 documents it. */
static void default_state(D9Device *d)
{
    DWORD *rs = d->render_states;
    memset(rs, 0, sizeof(d->render_states));
    rs[D3DRS_ZENABLE] = d->auto_depth ? D3DZB_TRUE : D3DZB_FALSE;
    rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
    rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
    rs[D3DRS_ZWRITEENABLE] = TRUE;
    rs[D3DRS_LASTPIXEL] = TRUE;
    rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    rs[D3DRS_CULLMODE] = D3DCULL_CCW;
    rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_FOGEND] = dbits(1.0f);
    rs[D3DRS_FOGDENSITY] = dbits(1.0f);
    rs[D3DRS_STENCILFAIL] = rs[D3DRS_STENCILZFAIL] = rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_STENCILMASK] = rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
    rs[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF;
    rs[D3DRS_CLIPPING] = TRUE;
    rs[D3DRS_LIGHTING] = TRUE;
    rs[D3DRS_COLORVERTEX] = TRUE;
    rs[D3DRS_LOCALVIEWER] = TRUE;
    rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    rs[D3DRS_AMBIENTMATERIALSOURCE] = rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs[D3DRS_POINTSIZE] = dbits(1.0f);
    rs[D3DRS_POINTSIZE_MIN] = dbits(1.0f);
    rs[D3DRS_POINTSCALE_A] = dbits(1.0f);
    rs[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
    rs[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFF;
    rs[D3DRS_DEBUGMONITORTOKEN] = D3DDMT_ENABLE;
    rs[D3DRS_POINTSIZE_MAX] = dbits(256.0f);
    rs[D3DRS_COLORWRITEENABLE] = rs[D3DRS_COLORWRITEENABLE1] = rs[D3DRS_COLORWRITEENABLE2] =
        rs[D3DRS_COLORWRITEENABLE3] = 0xF;
    rs[D3DRS_BLENDOP] = rs[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;
    rs[D3DRS_POSITIONDEGREE] = D3DDEGREE_CUBIC;
    rs[D3DRS_NORMALDEGREE] = D3DDEGREE_LINEAR;
    rs[D3DRS_MINTESSELLATIONLEVEL] = rs[D3DRS_MAXTESSELLATIONLEVEL] = dbits(1.0f);
    rs[D3DRS_ADAPTIVETESS_Z] = dbits(1.0f);
    rs[D3DRS_CCW_STENCILFAIL] = rs[D3DRS_CCW_STENCILZFAIL] = rs[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs[D3DRS_CCW_STENCILFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_BLENDFACTOR] = 0xFFFFFFFF;
    rs[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
    for (UINT i = 0; i < D9_MAX_STAGES; i++) {
        DWORD *ts = d->stage_states[i];
        memset(ts, 0, sizeof(d->stage_states[i]));
        ts[D3DTSS_COLOROP] = i ? D3DTOP_DISABLE : D3DTOP_MODULATE;
        ts[D3DTSS_ALPHAOP] = i ? D3DTOP_DISABLE : D3DTOP_SELECTARG1;
        ts[D3DTSS_COLORARG1] = ts[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        ts[D3DTSS_COLORARG2] = ts[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        ts[D3DTSS_COLORARG0] = ts[D3DTSS_ALPHAARG0] = ts[D3DTSS_RESULTARG] = D3DTA_CURRENT;
        ts[D3DTSS_TEXCOORDINDEX] = i;
    }
    for (UINT i = 0; i < D9_MAX_SAMPLERS; i++) {
        DWORD *ss = d->sampler_states[i];
        memset(ss, 0, sizeof(d->sampler_states[i]));
        ss[D3DSAMP_ADDRESSU] = ss[D3DSAMP_ADDRESSV] = ss[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        ss[D3DSAMP_MAGFILTER] = ss[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
        ss[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
        ss[D3DSAMP_MAXANISOTROPY] = 1;
    }
    for (UINT i = 0; i < RTL_NUMBER_OF(d->transforms); i++)
        d->transforms[i] = IDENTITY;
    memset(&d->material, 0, sizeof(d->material));
    memset(d->clip_planes, 0, sizeof(d->clip_planes));
    D9Surface *rt = d->targets[0];
    d->viewport = (D3DVIEWPORT9){0, 0, rt ? rt->width : 0, rt ? rt->height : 0, 0.0f, 1.0f};
    d->scissor = (RECT){0, 0, rt ? (LONG)rt->width : 0, rt ? (LONG)rt->height : 0};
}

/* binds a surface as the device holds them, the old one let go. */
static void set_surface(D9Surface **slot, D9Surface *s)
{
    if (*slot == s)
        return;
    if (s)
        D9Bind(&s->base);
    D9Surface *old = *slot;
    *slot = s;
    if (old)
        D9Unbind(&old->base);
}

static void set_texture(D9Texture **slot, D9Texture *t)
{
    D9Resource *old = *slot ? &(*slot)->base : NULL, *r = t ? &t->base : NULL;
    if (old == r)
        return;
    D9Bind(r);
    *slot = t;
    D9Unbind(old);
}

static void set_buffer(D9Buffer **slot, D9Buffer *b)
{
    D9Resource *old = *slot ? &(*slot)->base : NULL, *r = b ? &b->base : NULL;
    if (old == r)
        return;
    D9Bind(r);
    *slot = b;
    D9Unbind(old);
}

/* what Reset and the device's end let go: every binding but the targets. */
static void unbind_resources(D9Device *d)
{
    for (UINT i = 0; i < D9_MAX_SAMPLERS; i++)
        set_texture(&d->textures[i], NULL);
    for (UINT i = 0; i < D9_MAX_STREAMS; i++) {
        set_buffer(&d->streams[i].buffer, NULL);
        d->streams[i].offset = d->streams[i].stride = 0;
        d->stream_freq[i] = 1;
    }
    set_buffer(&d->indices, NULL);
    d->textures_dirty = ~0u;
    d->streams_dirty = ~0u;
    d->indices_dirty = TRUE;
}

/* ------------------------------------------------------------------ lifetime */

static void destroy_device(D9Device *d)
{
    unbind_resources(d);
    for (UINT i = 0; i < D9_MAX_TARGETS; i++)
        set_surface(&d->targets[i], NULL);
    set_surface(&d->depth_stencil, NULL);
    D9DestroyImplicitSurface(d->auto_depth);
    for (UINT i = 0; i < d->swapchain_count; i++)
        D9DestroySwapChain(d->swapchains[i]);
    HeapFree(GetProcessHeap(), 0, d->swapchains);
    HeapFree(GetProcessHeap(), 0, d->lights);
    if (d->context) {
        d->context->lpVtbl->ClearState(d->context);
        d->context->lpVtbl->Flush(d->context);
        d->context->lpVtbl->Release(d->context);
    }
    if (d->mime)
        d->mime->lpVtbl->Release(d->mime);
    if (d->d3d11)
        d->d3d11->lpVtbl->Release(d->d3d11);
    if (d->factory)
        d->factory->lpVtbl->Release(d->factory);
    if (d->d3d)
        d->d3d->lpVtbl->Release(d->d3d);
    DeleteCriticalSection(&d->lock);
    HeapFree(GetProcessHeap(), 0, d);
}

static HRESULT make_auto_depth(D9Device *d, const D3DPRESENT_PARAMETERS *p)
{
    if (!p->EnableAutoDepthStencil)
        return D3D_OK;
    const D9Format *f = D9GetFormat(p->AutoDepthStencilFormat);
    if (!(f->flags & D9_FORMAT_DEPTH))
        return D3DERR_INVALIDCALL;
    D9Surface *s;
    HRESULT hr = D9NewSurface(d, p->AutoDepthStencilFormat, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT,
                              p->BackBufferWidth, p->BackBufferHeight, &s);
    if (FAILED(hr))
        return hr;
    s->base.implicit = TRUE;
    s->samples = p->MultiSampleType;
    s->quality = p->MultiSampleQuality;
    s->lockable = !!(f->flags & D9_FORMAT_LOCKABLE);
    if (FAILED(hr = D9SurfaceMakeTexture(s))) {
        D9DestroyImplicitSurface(s);
        return hr;
    }
    d->auto_depth = s;
    return D3D_OK;
}

HRESULT D9CreateDevice(IDirect3D9Ex *d3d, BOOL ex, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                       D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *mode, D9Device **out)
{
    typedef HRESULT(WINAPI * Create)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT,
                                     UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    if (!(flags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_HARDWARE_VERTEXPROCESSING |
                   D3DCREATE_MIXED_VERTEXPROCESSING)))
        return D3DERR_INVALIDCALL;
    if (!focus && !params->hDeviceWindow)
        return D3DERR_INVALIDCALL;
    HMODULE module = LoadLibraryW(L"d3d11.dll");
    Create create = module ? (Create)(void *)GetProcAddress(module, "D3D11CreateDevice") : NULL;
    IDXGIAdapter1 *dxgi_adapter;
    if (!create || FAILED(D9GetOutput(adapter, &dxgi_adapter, NULL)))
        return D3DERR_NOTAVAILABLE;
    D9Device *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d) {
        dxgi_adapter->lpVtbl->Release(dxgi_adapter);
        return E_OUTOFMEMORY;
    }
    extern const IDirect3DDevice9ExVtbl D9DeviceVtbl;
    d->iface.lpVtbl = &D9DeviceVtbl;
    d->refs = 1;
    d->d3d = d3d;
    d3d->lpVtbl->AddRef(d3d);
    d->ex = ex;
    d->creation = (D3DDEVICE_CREATION_PARAMETERS){adapter, type, focus, flags};
    d->multithreaded = !!(flags & D3DCREATE_MULTITHREADED);
    d->max_frame_latency = 3;
    InitializeCriticalSection(&d->lock);
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device *d3d11 = NULL;
    ID3D11DeviceContext *context = NULL;
    HRESULT hr = create((IDXGIAdapter *)dxgi_adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                        &level, 1, D3D11_SDK_VERSION, &d3d11, NULL, &context);
    dxgi_adapter->lpVtbl->Release(dxgi_adapter);
    if (SUCCEEDED(hr)) {
        d3d11->lpVtbl->QueryInterface(d3d11, &IID_ID3D11Device1, (void **)&d->d3d11);
        d3d11->lpVtbl->QueryInterface(d3d11, &IID_IMimeD3D11Device, (void **)&d->mime);
        context->lpVtbl->QueryInterface(context, &IID_ID3D11DeviceContext1, (void **)&d->context);
        d3d11->lpVtbl->Release(d3d11);
        context->lpVtbl->Release(context);
        d->factory = (IDXGIFactory *)D9Factory();
        d->factory->lpVtbl->AddRef(d->factory);
    }
    if (FAILED(hr) || !d->d3d11 || !d->mime || !d->context) {
        destroy_device(d);
        return D3DERR_NOTAVAILABLE;
    }
    D9SwapChain *sc;
    d->swapchains = HeapAlloc(GetProcessHeap(), 0, sizeof(*d->swapchains));
    if (!d->swapchains || FAILED(hr = D9CreateSwapChain(d, params, &sc))) {
        destroy_device(d);
        return d->swapchains ? hr : E_OUTOFMEMORY;
    }
    sc->implicit = TRUE;
    d->swapchains[d->swapchain_count++] = sc;
    if (FAILED(hr = make_auto_depth(d, params))) {
        destroy_device(d);
        return hr;
    }
    set_surface(&d->targets[0], sc->back_buffers[0]);
    set_surface(&d->depth_stencil, d->auto_depth);
    for (UINT i = 0; i < D9_MAX_STREAMS; i++)
        d->stream_freq[i] = 1;
    default_state(d);
    *out = d;
    return D3D_OK;
}

void D9ForgetSwapChain(D9Device *d, D9SwapChain *sc)
{
    for (UINT i = 0; i < d->swapchain_count; i++)
        if (d->swapchains[i] == sc) {
            d->swapchains[i] = d->swapchains[--d->swapchain_count];
            return;
        }
}

/* ------------------------------------------------------------------ IUnknown */

static HRESULT STDMETHODCALLTYPE device_query(IDirect3DDevice9Ex *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DDevice9) ||
        (impl(iface)->ex && IsEqualGUID(riid, &IID_IDirect3DDevice9Ex))) {
        *out = iface;
        InterlockedIncrement(&impl(iface)->refs);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE device_add_ref(IDirect3DDevice9Ex *iface)
{
    return InterlockedIncrement(&impl(iface)->refs);
}

static ULONG STDMETHODCALLTYPE device_release(IDirect3DDevice9Ex *iface)
{
    ULONG refs = InterlockedDecrement(&impl(iface)->refs);
    if (!refs)
        destroy_device(impl(iface));
    return refs;
}

/* ------------------------------------------------------------------ the device */

static HRESULT STDMETHODCALLTYPE device_test_cooperative_level(IDirect3DDevice9Ex *iface) { return D3D_OK; }

/* the gpu's memory, in bytes, as a 32-bit count rounded to whole mebibytes. */
static UINT STDMETHODCALLTYPE device_get_available_texture_mem(IDirect3DDevice9Ex *iface)
{
    IDXGIAdapter1 *adapter;
    UINT64 bytes = 512ull << 20;
    if (SUCCEEDED(D9GetOutput(impl(iface)->creation.AdapterOrdinal, &adapter, NULL))) {
        DXGI_ADAPTER_DESC1 desc = {0};
        adapter->lpVtbl->GetDesc1(adapter, &desc);
        adapter->lpVtbl->Release(adapter);
        bytes = desc.DedicatedVideoMemory + desc.SharedSystemMemory;
    }
    bytes = D9_MIN(bytes, 4095ull << 20);
    return (UINT)(bytes & ~((1ull << 20) - 1));
}

static HRESULT STDMETHODCALLTYPE device_evict_managed_resources(IDirect3DDevice9Ex *iface) { return D3D_OK; }

static HRESULT STDMETHODCALLTYPE device_get_direct3d(IDirect3DDevice9Ex *iface, IDirect3D9 **d3d)
{
    if (!d3d)
        return D3DERR_INVALIDCALL;
    *d3d = (IDirect3D9 *)impl(iface)->d3d;
    impl(iface)->d3d->lpVtbl->AddRef(impl(iface)->d3d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_device_caps(IDirect3DDevice9Ex *iface, D3DCAPS9 *caps)
{
    if (!caps)
        return D3DERR_INVALIDCALL;
    D9FillCaps(impl(iface)->creation.AdapterOrdinal, caps);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_display_mode(IDirect3DDevice9Ex *iface, UINT swapchain,
                                                         D3DDISPLAYMODE *mode)
{
    D9Device *d = impl(iface);
    if (swapchain >= d->swapchain_count)
        return D3DERR_INVALIDCALL;
    return d->d3d->lpVtbl->GetAdapterDisplayMode(d->d3d, d->creation.AdapterOrdinal, mode);
}

static HRESULT STDMETHODCALLTYPE device_get_creation_parameters(IDirect3DDevice9Ex *iface,
                                                                D3DDEVICE_CREATION_PARAMETERS *params)
{
    if (!params)
        return D3DERR_INVALIDCALL;
    *params = impl(iface)->creation;
    return D3D_OK;
}

/* the cursor is windows' own: its image is the surface's, made a cursor of gdi. */
static HRESULT STDMETHODCALLTYPE device_set_cursor_properties(IDirect3DDevice9Ex *iface, UINT x, UINT y,
                                                              IDirect3DSurface9 *bitmap)
{
    D9Device *d = impl(iface);
    D9Surface *s = D9SurfaceFrom(bitmap);
    if (!s || s->base.d3d_format != D3DFMT_A8R8G8B8 || s->width > 256 || s->height > 256)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    D3DLOCKED_RECT locked;
    HRESULT hr = D9SurfaceLock(s, &locked, NULL, D3DLOCK_READONLY);
    if (SUCCEEDED(hr)) {
        BYTE *bits = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)s->width * s->height * 4);
        if (bits) {
            for (UINT row = 0; row < s->height; row++)
                memcpy(bits + (SIZE_T)row * s->width * 4, (BYTE *)locked.pBits + (SIZE_T)row * locked.Pitch,
                       (SIZE_T)s->width * 4);
            HBITMAP color = CreateBitmap(s->width, s->height, 1, 32, bits);
            HBITMAP mask = CreateBitmap(s->width, s->height, 1, 1, NULL);
            ICONINFO info = {FALSE, x, y, mask, color};
            HCURSOR cursor = color && mask ? CreateIconIndirect(&info) : NULL;
            if (cursor) {
                if (d->cursor)
                    DestroyCursor(d->cursor);
                d->cursor = cursor;
                if (d->cursor_shown)
                    SetCursor(cursor);
            }
            if (color)
                DeleteObject(color);
            if (mask)
                DeleteObject(mask);
            HeapFree(GetProcessHeap(), 0, bits);
        }
        D9SurfaceUnlock(s);
    }
    D9Unlock(d);
    return hr;
}

static void STDMETHODCALLTYPE device_set_cursor_position(IDirect3DDevice9Ex *iface, int x, int y, DWORD flags)
{
    SetCursorPos(x, y);
}

static BOOL STDMETHODCALLTYPE device_show_cursor(IDirect3DDevice9Ex *iface, BOOL show)
{
    D9Device *d = impl(iface);
    BOOL old = d->cursor_shown;
    d->cursor_shown = show;
    if (show && d->cursor)
        SetCursor(d->cursor);
    return old;
}

static HRESULT STDMETHODCALLTYPE device_create_additional_swap_chain(IDirect3DDevice9Ex *iface,
                                                                     D3DPRESENT_PARAMETERS *params,
                                                                     IDirect3DSwapChain9 **out)
{
    D9Device *d = impl(iface);
    if (!params || !out)
        return D3DERR_INVALIDCALL;
    *out = NULL;
    D9Lock(d);
    D9SwapChain **list = HeapReAlloc(GetProcessHeap(), 0, d->swapchains, (d->swapchain_count + 1) * sizeof(*list));
    HRESULT hr = list ? D3D_OK : E_OUTOFMEMORY;
    D9SwapChain *sc = NULL;
    if (list) {
        d->swapchains = list;
        hr = D9CreateSwapChain(d, params, &sc);
    }
    if (SUCCEEDED(hr)) {
        d->swapchains[d->swapchain_count++] = sc;
        sc->iface.lpVtbl->AddRef(&sc->iface);
        *out = (IDirect3DSwapChain9 *)&sc->iface;
    }
    D9Unlock(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_get_swap_chain(IDirect3DDevice9Ex *iface, UINT index,
                                                       IDirect3DSwapChain9 **out)
{
    D9Device *d = impl(iface);
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = NULL;
    if (index >= d->swapchain_count)
        return D3DERR_INVALIDCALL;
    D9SwapChain *sc = d->swapchains[index];
    sc->iface.lpVtbl->AddRef(&sc->iface);
    *out = (IDirect3DSwapChain9 *)&sc->iface;
    return D3D_OK;
}

static UINT STDMETHODCALLTYPE device_get_number_of_swap_chains(IDirect3DDevice9Ex *iface)
{
    return impl(iface)->swapchain_count;
}

static HRESULT reset(D9Device *d, D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *mode)
{
    if (!params)
        return D3DERR_INVALIDCALL;
    D9SwapChain *sc = d->swapchains[0];
    if (D9SwapChainInUse(sc) || (d->auto_depth && d->auto_depth->base.refs))
        return D3DERR_INVALIDCALL;
    for (UINT i = 0; i < D9_MAX_TARGETS; i++)
        set_surface(&d->targets[i], NULL);
    set_surface(&d->depth_stencil, NULL);
    unbind_resources(d);
    d->context->lpVtbl->ClearState(d->context);
    D9DestroyImplicitSurface(d->auto_depth);
    d->auto_depth = NULL;
    HRESULT hr = D9ResetSwapChain(sc, params);
    if (SUCCEEDED(hr))
        hr = make_auto_depth(d, params);
    if (FAILED(hr))
        return D3DERR_DEVICELOST;
    set_surface(&d->targets[0], sc->back_buffers[0]);
    set_surface(&d->depth_stencil, d->auto_depth);
    default_state(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_reset(IDirect3DDevice9Ex *iface, D3DPRESENT_PARAMETERS *params)
{
    D9Device *d = impl(iface);
    D9Lock(d);
    HRESULT hr = reset(d, params, NULL);
    D9Unlock(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_present(IDirect3DDevice9Ex *iface, const RECT *source, const RECT *dest,
                                                HWND window, const RGNDATA *dirty)
{
    D9Device *d = impl(iface);
    D9Lock(d);
    HRESULT hr = D9Present(d->swapchains[0], source, dest, window, 0);
    D9Unlock(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_get_back_buffer(IDirect3DDevice9Ex *iface, UINT swapchain, UINT index,
                                                        D3DBACKBUFFER_TYPE type, IDirect3DSurface9 **out)
{
    D9Device *d = impl(iface);
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = NULL;
    if (swapchain >= d->swapchain_count)
        return D3DERR_INVALIDCALL;
    D9SwapChain *sc = d->swapchains[swapchain];
    return sc->iface.lpVtbl->GetBackBuffer(&sc->iface, index, type, out);
}

static HRESULT STDMETHODCALLTYPE device_get_raster_status(IDirect3DDevice9Ex *iface, UINT swapchain,
                                                          D3DRASTER_STATUS *status)
{
    D9Device *d = impl(iface);
    if (swapchain >= d->swapchain_count)
        return D3DERR_INVALIDCALL;
    return D9RasterStatus(d->swapchains[swapchain], status);
}

static HRESULT STDMETHODCALLTYPE device_set_dialog_box_mode(IDirect3DDevice9Ex *iface, BOOL enable)
{
    return D3D_OK;
}

static void STDMETHODCALLTYPE device_set_gamma_ramp(IDirect3DDevice9Ex *iface, UINT swapchain, DWORD flags,
                                                    const D3DGAMMARAMP *ramp)
{
    D9Device *d = impl(iface);
    if (swapchain < d->swapchain_count && ramp)
        d->swapchains[swapchain]->gamma = *ramp;
}

static void STDMETHODCALLTYPE device_get_gamma_ramp(IDirect3DDevice9Ex *iface, UINT swapchain, D3DGAMMARAMP *ramp)
{
    D9Device *d = impl(iface);
    if (swapchain < d->swapchain_count && ramp)
        *ramp = d->swapchains[swapchain]->gamma;
}

/* ------------------------------------------------------------------ resources */

static HRESULT create_texture(D9Device *d, D3DRESOURCETYPE type, UINT width, UINT height, UINT depth, UINT levels,
                              DWORD usage, D3DFORMAT format, D3DPOOL pool, void **out, HANDLE *shared)
{
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = NULL;
    if ((shared && !d->ex) || (pool == D3DPOOL_MANAGED && d->ex))
        return D3DERR_INVALIDCALL;
    D9Texture *t;
    D9Lock(d);
    HRESULT hr = D9CreateTexture(d, type, width, height, depth, levels, usage, format, pool, &t);
    D9Unlock(d);
    if (SUCCEEDED(hr))
        *out = &t->iface.base;
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_create_texture(IDirect3DDevice9Ex *iface, UINT width, UINT height,
                                                       UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                                       IDirect3DTexture9 **texture, HANDLE *shared)
{
    return create_texture(impl(iface), D3DRTYPE_TEXTURE, width, height, 1, levels, usage, format, pool,
                          (void **)texture, shared);
}

static HRESULT STDMETHODCALLTYPE device_create_volume_texture(IDirect3DDevice9Ex *iface, UINT width, UINT height,
                                                              UINT depth, UINT levels, DWORD usage, D3DFORMAT format,
                                                              D3DPOOL pool, IDirect3DVolumeTexture9 **texture,
                                                              HANDLE *shared)
{
    return create_texture(impl(iface), D3DRTYPE_VOLUMETEXTURE, width, height, depth, levels, usage, format, pool,
                          (void **)texture, shared);
}

static HRESULT STDMETHODCALLTYPE device_create_cube_texture(IDirect3DDevice9Ex *iface, UINT edge, UINT levels,
                                                            DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                                            IDirect3DCubeTexture9 **texture, HANDLE *shared)
{
    return create_texture(impl(iface), D3DRTYPE_CUBETEXTURE, edge, edge, 1, levels, usage, format, pool,
                          (void **)texture, shared);
}

static HRESULT create_buffer(D9Device *d, BOOL index, UINT length, DWORD usage, DWORD fvf, D3DFORMAT format,
                             D3DPOOL pool, void **out, HANDLE *shared)
{
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = NULL;
    if ((shared && !d->ex) || (pool == D3DPOOL_MANAGED && d->ex))
        return D3DERR_INVALIDCALL;
    D9Buffer *b;
    D9Lock(d);
    HRESULT hr = D9CreateBuffer(d, index, length, usage, fvf, format, pool, &b);
    D9Unlock(d);
    if (SUCCEEDED(hr))
        *out = &b->iface.vb;
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_create_vertex_buffer(IDirect3DDevice9Ex *iface, UINT length, DWORD usage,
                                                             DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9 **buffer,
                                                             HANDLE *shared)
{
    return create_buffer(impl(iface), FALSE, length, usage, fvf, D3DFMT_VERTEXDATA, pool, (void **)buffer, shared);
}

static HRESULT STDMETHODCALLTYPE device_create_index_buffer(IDirect3DDevice9Ex *iface, UINT length, DWORD usage,
                                                            D3DFORMAT format, D3DPOOL pool,
                                                            IDirect3DIndexBuffer9 **buffer, HANDLE *shared)
{
    return create_buffer(impl(iface), TRUE, length, usage, 0, format, pool, (void **)buffer, shared);
}

static HRESULT create_surface(D9Device *d, UINT width, UINT height, D3DFORMAT format, DWORD usage, D3DPOOL pool,
                              D3DMULTISAMPLE_TYPE samples, DWORD quality, BOOL lockable, IDirect3DSurface9 **out,
                              HANDLE *shared)
{
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = NULL;
    if (shared && !d->ex)
        return D3DERR_INVALIDCALL;
    D9Surface *s;
    D9Lock(d);
    HRESULT hr = D9CreateSurface(d, width, height, format, usage, pool, samples, quality, lockable, &s);
    D9Unlock(d);
    if (SUCCEEDED(hr))
        *out = &s->iface;
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_create_render_target(IDirect3DDevice9Ex *iface, UINT width, UINT height,
                                                             D3DFORMAT format, D3DMULTISAMPLE_TYPE samples,
                                                             DWORD quality, BOOL lockable, IDirect3DSurface9 **out,
                                                             HANDLE *shared)
{
    return create_surface(impl(iface), width, height, format, D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT, samples,
                          quality, lockable, out, shared);
}

static HRESULT STDMETHODCALLTYPE device_create_depth_stencil_surface(IDirect3DDevice9Ex *iface, UINT width,
                                                                     UINT height, D3DFORMAT format,
                                                                     D3DMULTISAMPLE_TYPE samples, DWORD quality,
                                                                     BOOL discard, IDirect3DSurface9 **out,
                                                                     HANDLE *shared)
{
    return create_surface(impl(iface), width, height, format, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT, samples,
                          quality, FALSE, out, shared);
}

/* a system memory surface's rectangle to a surface in the gpu's memory. */
static HRESULT STDMETHODCALLTYPE device_update_surface(IDirect3DDevice9Ex *iface, IDirect3DSurface9 *source,
                                                       const RECT *rect, IDirect3DSurface9 *dest, const POINT *point)
{
    D9Device *d = impl(iface);
    D9Surface *s = D9SurfaceFrom(source), *t = D9SurfaceFrom(dest);
    if (!s || !t || s->base.pool != D3DPOOL_SYSTEMMEM || t->base.pool != D3DPOOL_DEFAULT ||
        s->base.d3d_format != t->base.d3d_format || !t->texture)
        return D3DERR_INVALIDCALL;
    RECT r;
    if (!D9LockRect(s->base.format, s->width, s->height, rect, &r))
        return D3DERR_INVALIDCALL;
    POINT at = point ? *point : (POINT){0, 0};
    LONG w = r.right - r.left, h = r.bottom - r.top;
    if (at.x < 0 || at.y < 0 || at.x + w > (LONG)t->width || at.y + h > (LONG)t->height)
        return D3DERR_INVALIDCALL;
    const D9Format *f = s->base.format;
    D9Lock(d);
    D3D11_BOX box = {(UINT)at.x, (UINT)at.y, 0, (UINT)(at.x + w), (UINT)(at.y + h), 1};
    if (f->block > 1) {
        box.right = D9_MIN(D9_ALIGN(box.right, f->block), D9_ALIGN(t->width, f->block));
        box.bottom = D9_MIN(D9_ALIGN(box.bottom, f->block), D9_ALIGN(t->height, f->block));
    }
    const BYTE *bytes = D9MemoryAt(s->memory, f, r.left, r.top, 0);
    UINT pitch = s->memory->pitch;
    BYTE *converted = NULL;
    if (f->stored != f->bytes) {
        UINT rows = D9FormatRows(f, h), out_pitch = D9_ALIGN(w * f->stored, 4);
        converted = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)out_pitch * rows);
        if (converted)
            D9ConvertUpload(f, bytes, pitch, converted, out_pitch, w, rows);
        bytes = converted;
        pitch = out_pitch;
    }
    if (bytes)
        d->context->lpVtbl->UpdateSubresource(d->context, (ID3D11Resource *)t->texture, D9SurfaceSubresource(t), &box,
                                              bytes, pitch, 0);
    HeapFree(GetProcessHeap(), 0, converted);
    D9SurfaceWritten(t);
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_update_texture(IDirect3DDevice9Ex *iface, IDirect3DBaseTexture9 *source,
                                                       IDirect3DBaseTexture9 *dest)
{
    D9Device *d = impl(iface);
    D9Lock(d);
    HRESULT hr = D9UpdateTexture(d, D9TextureFrom(source), D9TextureFrom(dest));
    D9Unlock(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_get_render_target_data(IDirect3DDevice9Ex *iface,
                                                               IDirect3DSurface9 *target, IDirect3DSurface9 *dest)
{
    D9Device *d = impl(iface);
    D9Surface *s = D9SurfaceFrom(target), *t = D9SurfaceFrom(dest);
    if (!s || !t || s == t || t->base.pool != D3DPOOL_SYSTEMMEM || s->width != t->width || s->height != t->height ||
        s->base.d3d_format != t->base.d3d_format || !s->texture)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    HRESULT hr = D9SurfaceDownload(s);
    if (SUCCEEDED(hr)) {
        UINT rows = D9FormatRows(s->base.format, s->height);
        for (UINT y = 0; y < rows; y++)
            memcpy(t->memory->bytes + (SIZE_T)y * t->memory->pitch, s->memory->bytes + (SIZE_T)y * s->memory->pitch,
                   D9_MIN(s->memory->pitch, t->memory->pitch));
    }
    D9Unlock(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_get_front_buffer_data(IDirect3DDevice9Ex *iface, UINT swapchain,
                                                              IDirect3DSurface9 *dest)
{
    D9Device *d = impl(iface);
    if (swapchain >= d->swapchain_count)
        return D3DERR_INVALIDCALL;
    D9SwapChain *sc = d->swapchains[swapchain];
    return sc->iface.lpVtbl->GetFrontBufferData(&sc->iface, dest);
}

static HRESULT STDMETHODCALLTYPE device_stretch_rect(IDirect3DDevice9Ex *iface, IDirect3DSurface9 *source,
                                                     const RECT *source_rect, IDirect3DSurface9 *dest,
                                                     const RECT *dest_rect, D3DTEXTUREFILTERTYPE filter)
{
    D9Device *d = impl(iface);
    D9Surface *s = D9SurfaceFrom(source), *t = D9SurfaceFrom(dest);
    if (!s || !t || s == t || !s->texture || !t->texture || t->base.pool != D3DPOOL_DEFAULT ||
        s->base.pool != D3DPOOL_DEFAULT)
        return D3DERR_INVALIDCALL;
    RECT sr = source_rect ? *source_rect : (RECT){0, 0, (LONG)s->width, (LONG)s->height};
    RECT dr = dest_rect ? *dest_rect : (RECT){0, 0, (LONG)t->width, (LONG)t->height};
    if (sr.left < 0 || sr.top < 0 || sr.right > (LONG)s->width || sr.bottom > (LONG)s->height ||
        sr.left >= sr.right || sr.top >= sr.bottom || dr.left < 0 || dr.top < 0 || dr.right > (LONG)t->width ||
        dr.bottom > (LONG)t->height || dr.left >= dr.right || dr.top >= dr.bottom)
        return D3DERR_INVALIDCALL;
    BOOL depth = (s->base.format->flags | t->base.format->flags) & D9_FORMAT_DEPTH;
    if (depth && (s->base.d3d_format != t->base.d3d_format ||
                  sr.right - sr.left != dr.right - dr.left || sr.bottom - sr.top != dr.bottom - dr.top))
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    d->mime->lpVtbl->Blit(d->mime, (ID3D11Resource *)t->texture, D9SurfaceSubresource(t), &dr,
                          (ID3D11Resource *)s->texture, D9SurfaceSubresource(s), &sr, filter == D3DTEXF_LINEAR);
    D9SurfaceWritten(t);
    D9Unlock(d);
    return D3D_OK;
}

static void color_floats(D3DCOLOR color, FLOAT out[4])
{
    out[0] = ((color >> 16) & 0xFF) / 255.0f;
    out[1] = ((color >> 8) & 0xFF) / 255.0f;
    out[2] = (color & 0xFF) / 255.0f;
    out[3] = (color >> 24) / 255.0f;
}

static HRESULT STDMETHODCALLTYPE device_color_fill(IDirect3DDevice9Ex *iface, IDirect3DSurface9 *surface,
                                                   const RECT *rect, D3DCOLOR color)
{
    D9Device *d = impl(iface);
    D9Surface *s = D9SurfaceFrom(surface);
    if (!s || s->base.pool != D3DPOOL_DEFAULT || (s->base.format->flags & D9_FORMAT_DEPTH))
        return D3DERR_INVALIDCALL;
    RECT r = rect ? *rect : (RECT){0, 0, (LONG)s->width, (LONG)s->height};
    if (r.left < 0 || r.top < 0 || r.right > (LONG)s->width || r.bottom > (LONG)s->height)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    ID3D11RenderTargetView *rtv = D9SurfaceRtv(s, FALSE);
    HRESULT hr = D3D_OK;
    if (rtv) {
        FLOAT c[4];
        color_floats(color, c);
        D3D11_RECT box = {r.left, r.top, r.right, r.bottom};
        d->context->lpVtbl->ClearView(d->context, (ID3D11View *)rtv, c, &box, 1);
        D9SurfaceWritten(s);
    } else if (s->texture) {
        hr = not_yet(d, "ColorFill of surfaces that are not render targets");
    }
    D9Unlock(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_create_offscreen_plain_surface(IDirect3DDevice9Ex *iface, UINT width,
                                                                      UINT height, D3DFORMAT format, D3DPOOL pool,
                                                                      IDirect3DSurface9 **out, HANDLE *shared)
{
    if (pool == D3DPOOL_MANAGED)
        return D3DERR_INVALIDCALL;
    return create_surface(impl(iface), width, height, format, 0, pool, D3DMULTISAMPLE_NONE, 0, TRUE, out, shared);
}

/* ------------------------------------------------------------------ targets */

static HRESULT STDMETHODCALLTYPE device_set_render_target(IDirect3DDevice9Ex *iface, DWORD index,
                                                          IDirect3DSurface9 *surface)
{
    D9Device *d = impl(iface);
    D9Surface *s = D9SurfaceFrom(surface);
    if (index >= D9_MAX_TARGETS || (!index && !s) || (s && !(s->base.usage & D3DUSAGE_RENDERTARGET)))
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    set_surface(&d->targets[index], s);
    /* the first target's whole area becomes the viewport and scissor rectangle. */
    if (!index) {
        d->viewport = (D3DVIEWPORT9){0, 0, s->width, s->height, 0.0f, 1.0f};
        d->scissor = (RECT){0, 0, (LONG)s->width, (LONG)s->height};
    }
    d->targets_dirty = TRUE;
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_render_target(IDirect3DDevice9Ex *iface, DWORD index,
                                                          IDirect3DSurface9 **out)
{
    D9Device *d = impl(iface);
    if (!out || index >= D9_MAX_TARGETS)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    D9Surface *s = d->targets[index];
    *out = s ? &s->iface : NULL;
    if (s)
        D9SurfaceAddRef(s);
    D9Unlock(d);
    return s ? D3D_OK : D3DERR_NOTFOUND;
}

static HRESULT STDMETHODCALLTYPE device_set_depth_stencil_surface(IDirect3DDevice9Ex *iface,
                                                                  IDirect3DSurface9 *surface)
{
    D9Device *d = impl(iface);
    D9Surface *s = D9SurfaceFrom(surface);
    if (s && !(s->base.usage & D3DUSAGE_DEPTHSTENCIL))
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    set_surface(&d->depth_stencil, s);
    d->targets_dirty = TRUE;
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_depth_stencil_surface(IDirect3DDevice9Ex *iface,
                                                                  IDirect3DSurface9 **out)
{
    D9Device *d = impl(iface);
    if (!out)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    D9Surface *s = d->depth_stencil;
    *out = s ? &s->iface : NULL;
    if (s)
        D9SurfaceAddRef(s);
    D9Unlock(d);
    return s ? D3D_OK : D3DERR_NOTFOUND;
}

static HRESULT STDMETHODCALLTYPE device_begin_scene(IDirect3DDevice9Ex *iface)
{
    D9Device *d = impl(iface);
    if (d->in_scene)
        return D3DERR_INVALIDCALL;
    d->in_scene = TRUE;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_end_scene(IDirect3DDevice9Ex *iface)
{
    D9Device *d = impl(iface);
    if (!d->in_scene)
        return D3DERR_INVALIDCALL;
    d->in_scene = FALSE;
    /* the scene's work goes to the gpu, as drivers start it at EndScene. */
    D9Lock(d);
    d->context->lpVtbl->Flush(d->context);
    D9Unlock(d);
    return D3D_OK;
}

/* the rectangles a clear covers: the viewport, the scissor rectangle if enabled, each rectangle given. */
static UINT clear_rects(D9Device *d, DWORD count, const D3DRECT *rects, D3D11_RECT *out, UINT max)
{
    D3D11_RECT bound = {(LONG)d->viewport.X, (LONG)d->viewport.Y, (LONG)(d->viewport.X + d->viewport.Width),
                        (LONG)(d->viewport.Y + d->viewport.Height)};
    if (d->render_states[D3DRS_SCISSORTESTENABLE]) {
        bound.left = D9_MAX(bound.left, d->scissor.left);
        bound.top = D9_MAX(bound.top, d->scissor.top);
        bound.right = D9_MIN(bound.right, d->scissor.right);
        bound.bottom = D9_MIN(bound.bottom, d->scissor.bottom);
    }
    if (!rects || !count) {
        out[0] = bound;
        return bound.right > bound.left && bound.bottom > bound.top;
    }
    UINT n = 0;
    for (UINT i = 0; i < count && n < max; i++) {
        D3D11_RECT r = {D9_MAX(rects[i].x1, bound.left), D9_MAX(rects[i].y1, bound.top),
                        D9_MIN(rects[i].x2, bound.right), D9_MIN(rects[i].y2, bound.bottom)};
        if (r.right > r.left && r.bottom > r.top)
            out[n++] = r;
    }
    return n;
}

static HRESULT STDMETHODCALLTYPE device_clear(IDirect3DDevice9Ex *iface, DWORD count, const D3DRECT *rects,
                                              DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    D9Device *d = impl(iface);
    if ((count && !rects) || !(flags & (D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)))
        return D3DERR_INVALIDCALL;
    if ((flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) && !d->depth_stencil)
        return D3DERR_INVALIDCALL;
    if ((flags & D3DCLEAR_STENCIL) && !(d->depth_stencil->base.format->flags & D9_FORMAT_STENCIL))
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    D3D11_RECT *boxes = HeapAlloc(GetProcessHeap(), 0, (count ? count : 1) * sizeof(*boxes));
    UINT n = boxes ? clear_rects(d, count, rects, boxes, count ? count : 1) : 0;
    if (n && (flags & D3DCLEAR_TARGET)) {
        FLOAT c[4];
        color_floats(color, c);
        for (UINT i = 0; i < D9_MAX_TARGETS; i++) {
            D9Surface *s = d->targets[i];
            ID3D11RenderTargetView *rtv = s ? D9SurfaceRtv(s, d->render_states[D3DRS_SRGBWRITEENABLE]) : NULL;
            if (!rtv)
                continue;
            for (UINT j = 0; j < n; j += 16)
                d->context->lpVtbl->ClearView(d->context, (ID3D11View *)rtv, c, boxes + j, D9_MIN(n - j, 16));
            D9SurfaceWritten(s);
        }
    }
    ID3D11DepthStencilView *dsv = d->depth_stencil ? D9SurfaceDsv(d->depth_stencil) : NULL;
    if (n && dsv && (flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL))) {
        UINT which = (flags & D3DCLEAR_ZBUFFER ? D3D11_CLEAR_DEPTH : 0) |
                     (flags & D3DCLEAR_STENCIL ? D3D11_CLEAR_STENCIL : 0);
        for (UINT j = 0; j < n; j += 16)
            d->mime->lpVtbl->ClearDepthStencil(d->mime, dsv, which, z, (UINT8)stencil, boxes + j, D9_MIN(n - j, 16));
        D9SurfaceWritten(d->depth_stencil);
    }
    HeapFree(GetProcessHeap(), 0, boxes);
    D9Unlock(d);
    return D3D_OK;
}

/* ------------------------------------------------------------------ fixed function state */

static D3DMATRIX *transform_slot(D9Device *d, D3DTRANSFORMSTATETYPE state)
{
    return (UINT)state < RTL_NUMBER_OF(d->transforms) ? &d->transforms[state] : NULL;
}

static HRESULT STDMETHODCALLTYPE device_set_transform(IDirect3DDevice9Ex *iface, D3DTRANSFORMSTATETYPE state,
                                                      const D3DMATRIX *matrix)
{
    D9Device *d = impl(iface);
    D3DMATRIX *m = transform_slot(d, state);
    if (!matrix)
        return D3DERR_INVALIDCALL;
    if (m) {
        D9Lock(d);
        *m = *matrix;
        d->ff_dirty = TRUE;
        D9Unlock(d);
    }
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_transform(IDirect3DDevice9Ex *iface, D3DTRANSFORMSTATETYPE state,
                                                      D3DMATRIX *matrix)
{
    D9Device *d = impl(iface);
    D3DMATRIX *m = transform_slot(d, state);
    if (!matrix)
        return D3DERR_INVALIDCALL;
    *matrix = m ? *m : IDENTITY;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_multiply_transform(IDirect3DDevice9Ex *iface, D3DTRANSFORMSTATETYPE state,
                                                           const D3DMATRIX *matrix)
{
    D9Device *d = impl(iface);
    D3DMATRIX *m = transform_slot(d, state);
    if (!matrix)
        return D3DERR_INVALIDCALL;
    if (!m)
        return D3D_OK;
    D9Lock(d);
    D3DMATRIX r;
    for (UINT i = 0; i < 4; i++)
        for (UINT j = 0; j < 4; j++)
            r.m[i][j] = matrix->m[i][0] * m->m[0][j] + matrix->m[i][1] * m->m[1][j] + matrix->m[i][2] * m->m[2][j] +
                        matrix->m[i][3] * m->m[3][j];
    *m = r;
    d->ff_dirty = TRUE;
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_viewport(IDirect3DDevice9Ex *iface, const D3DVIEWPORT9 *viewport)
{
    D9Device *d = impl(iface);
    D9Surface *rt = d->targets[0];
    if (!viewport || (rt && (viewport->X + viewport->Width > rt->width || viewport->Y + viewport->Height > rt->height)))
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    d->viewport = *viewport;
    d->viewport_dirty = TRUE;
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_viewport(IDirect3DDevice9Ex *iface, D3DVIEWPORT9 *viewport)
{
    if (!viewport)
        return D3DERR_INVALIDCALL;
    *viewport = impl(iface)->viewport;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_material(IDirect3DDevice9Ex *iface, const D3DMATERIAL9 *material)
{
    D9Device *d = impl(iface);
    if (!material)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    d->material = *material;
    d->ff_dirty = TRUE;
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_material(IDirect3DDevice9Ex *iface, D3DMATERIAL9 *material)
{
    if (!material)
        return D3DERR_INVALIDCALL;
    *material = impl(iface)->material;
    return D3D_OK;
}

/* lights are kept in a list as long as the highest index set, as programs set them sparsely. */
static D9Light *light_slot(D9Device *d, DWORD index, BOOL grow)
{
    for (UINT i = 0; i < d->light_count; i++)
        if (d->lights[i].index == index)
            return &d->lights[i];
    if (!grow)
        return NULL;
    D9Light *lights = d->lights ? HeapReAlloc(GetProcessHeap(), 0, d->lights, (d->light_count + 1) * sizeof(*lights))
                                : HeapAlloc(GetProcessHeap(), 0, sizeof(*lights));
    if (!lights)
        return NULL;
    d->lights = lights;
    D9Light *l = &lights[d->light_count++];
    memset(l, 0, sizeof(*l));
    l->index = index;
    /* a light enabled before it was set is a white directional light down z. */
    l->light.Type = D3DLIGHT_DIRECTIONAL;
    l->light.Diffuse = (D3DCOLORVALUE){1.0f, 1.0f, 1.0f, 0.0f};
    l->light.Direction = (D3DVECTOR){0.0f, 0.0f, 1.0f};
    return l;
}

static HRESULT STDMETHODCALLTYPE device_set_light(IDirect3DDevice9Ex *iface, DWORD index, const D3DLIGHT9 *light)
{
    D9Device *d = impl(iface);
    if (!light || light->Type < D3DLIGHT_POINT || light->Type > D3DLIGHT_DIRECTIONAL)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    D9Light *l = light_slot(d, index, TRUE);
    if (l)
        l->light = *light;
    d->ff_dirty = TRUE;
    D9Unlock(d);
    return l ? D3D_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE device_get_light(IDirect3DDevice9Ex *iface, DWORD index, D3DLIGHT9 *light)
{
    D9Device *d = impl(iface);
    D9Lock(d);
    D9Light *l = light_slot(d, index, FALSE);
    if (l && light)
        *light = l->light;
    D9Unlock(d);
    return l && light ? D3D_OK : D3DERR_INVALIDCALL;
}

static HRESULT STDMETHODCALLTYPE device_light_enable(IDirect3DDevice9Ex *iface, DWORD index, BOOL enable)
{
    D9Device *d = impl(iface);
    D9Lock(d);
    D9Light *l = light_slot(d, index, TRUE);
    if (l)
        l->enabled = !!enable;
    d->ff_dirty = TRUE;
    D9Unlock(d);
    return l ? D3D_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE device_get_light_enable(IDirect3DDevice9Ex *iface, DWORD index, BOOL *enable)
{
    D9Device *d = impl(iface);
    D9Lock(d);
    D9Light *l = light_slot(d, index, FALSE);
    if (l && enable)
        *enable = l->enabled ? 128 : 0;
    D9Unlock(d);
    return l && enable ? D3D_OK : D3DERR_INVALIDCALL;
}

static HRESULT STDMETHODCALLTYPE device_set_clip_plane(IDirect3DDevice9Ex *iface, DWORD index, const float *plane)
{
    D9Device *d = impl(iface);
    if (index >= 6 || !plane)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    memcpy(d->clip_planes[index], plane, sizeof(d->clip_planes[index]));
    d->ff_dirty = TRUE;
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_clip_plane(IDirect3DDevice9Ex *iface, DWORD index, float *plane)
{
    D9Device *d = impl(iface);
    if (index >= 6 || !plane)
        return D3DERR_INVALIDCALL;
    memcpy(plane, d->clip_planes[index], sizeof(d->clip_planes[index]));
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_render_state(IDirect3DDevice9Ex *iface, D3DRENDERSTATETYPE state,
                                                         DWORD value)
{
    D9Device *d = impl(iface);
    if ((UINT)state >= RTL_NUMBER_OF(d->render_states))
        return D3D_OK;
    D9Lock(d);
    if (d->render_states[state] != value) {
        d->render_states[state] = value;
        d->states_dirty = TRUE;
    }
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_render_state(IDirect3DDevice9Ex *iface, D3DRENDERSTATETYPE state,
                                                         DWORD *value)
{
    D9Device *d = impl(iface);
    if (!value)
        return D3DERR_INVALIDCALL;
    *value = (UINT)state < RTL_NUMBER_OF(d->render_states) ? d->render_states[state] : 0;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_create_state_block(IDirect3DDevice9Ex *iface, D3DSTATEBLOCKTYPE type,
                                                           IDirect3DStateBlock9 **block)
{
    return not_yet(impl(iface), "CreateStateBlock");
}

static HRESULT STDMETHODCALLTYPE device_begin_state_block(IDirect3DDevice9Ex *iface)
{
    return not_yet(impl(iface), "BeginStateBlock");
}

static HRESULT STDMETHODCALLTYPE device_end_state_block(IDirect3DDevice9Ex *iface, IDirect3DStateBlock9 **block)
{
    return not_yet(impl(iface), "EndStateBlock");
}

static HRESULT STDMETHODCALLTYPE device_set_clip_status(IDirect3DDevice9Ex *iface, const D3DCLIPSTATUS9 *status)
{
    if (!status)
        return D3DERR_INVALIDCALL;
    impl(iface)->clip_status = *status;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_clip_status(IDirect3DDevice9Ex *iface, D3DCLIPSTATUS9 *status)
{
    if (!status)
        return D3DERR_INVALIDCALL;
    *status = impl(iface)->clip_status;
    return D3D_OK;
}

/* the sampler slots: 16 of pixel shaders, the displacement map's, then the 4 of vertex shaders. */
static int sampler_slot(DWORD sampler)
{
    if (sampler < 16)
        return sampler;
    if (sampler >= D3DDMAPSAMPLER && sampler <= D3DVERTEXTEXTURESAMPLER3)
        return 16 + sampler - D3DDMAPSAMPLER;
    return -1;
}

static HRESULT STDMETHODCALLTYPE device_get_texture(IDirect3DDevice9Ex *iface, DWORD stage,
                                                    IDirect3DBaseTexture9 **texture)
{
    D9Device *d = impl(iface);
    int slot = sampler_slot(stage);
    if (!texture)
        return D3DERR_INVALIDCALL;
    *texture = NULL;
    if (slot < 0)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    D9Texture *t = d->textures[slot];
    if (t) {
        *texture = &t->iface.base;
        D9ResourceAddRef(&t->base);
    }
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_texture(IDirect3DDevice9Ex *iface, DWORD stage,
                                                    IDirect3DBaseTexture9 *texture)
{
    D9Device *d = impl(iface);
    int slot = sampler_slot(stage);
    D9Texture *t = D9TextureFrom(texture);
    if (slot < 0 || (t && (t->base.pool == D3DPOOL_SYSTEMMEM || t->base.pool == D3DPOOL_SCRATCH)))
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    if (d->textures[slot] != t) {
        set_texture(&d->textures[slot], t);
        d->textures_dirty |= 1u << slot;
    }
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_texture_stage_state(IDirect3DDevice9Ex *iface, DWORD stage,
                                                                D3DTEXTURESTAGESTATETYPE type, DWORD *value)
{
    D9Device *d = impl(iface);
    if (!value || stage >= D9_MAX_STAGES || (UINT)type >= RTL_NUMBER_OF(d->stage_states[0]))
        return D3DERR_INVALIDCALL;
    *value = d->stage_states[stage][type];
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_texture_stage_state(IDirect3DDevice9Ex *iface, DWORD stage,
                                                                D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
    D9Device *d = impl(iface);
    if (stage >= D9_MAX_STAGES || (UINT)type >= RTL_NUMBER_OF(d->stage_states[0]))
        return D3D_OK;
    D9Lock(d);
    if (d->stage_states[stage][type] != value) {
        d->stage_states[stage][type] = value;
        d->ff_dirty = TRUE;
    }
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_sampler_state(IDirect3DDevice9Ex *iface, DWORD sampler,
                                                          D3DSAMPLERSTATETYPE type, DWORD *value)
{
    D9Device *d = impl(iface);
    int slot = sampler_slot(sampler);
    if (!value || slot < 0 || (UINT)type >= RTL_NUMBER_OF(d->sampler_states[0]))
        return D3DERR_INVALIDCALL;
    *value = d->sampler_states[slot][type];
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_sampler_state(IDirect3DDevice9Ex *iface, DWORD sampler,
                                                          D3DSAMPLERSTATETYPE type, DWORD value)
{
    D9Device *d = impl(iface);
    int slot = sampler_slot(sampler);
    if (slot < 0 || (UINT)type >= RTL_NUMBER_OF(d->sampler_states[0]))
        return D3D_OK;
    D9Lock(d);
    if (d->sampler_states[slot][type] != value) {
        d->sampler_states[slot][type] = value;
        d->samplers_dirty |= 1u << slot;
    }
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_validate_device(IDirect3DDevice9Ex *iface, DWORD *passes)
{
    if (passes)
        *passes = 1;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_palette_entries(IDirect3DDevice9Ex *iface, UINT palette,
                                                            const PALETTEENTRY *entries)
{
    return entries ? D3D_OK : D3DERR_INVALIDCALL;
}

static HRESULT STDMETHODCALLTYPE device_get_palette_entries(IDirect3DDevice9Ex *iface, UINT palette,
                                                            PALETTEENTRY *entries)
{
    return D3DERR_INVALIDCALL;
}

static HRESULT STDMETHODCALLTYPE device_set_current_texture_palette(IDirect3DDevice9Ex *iface, UINT palette)
{
    impl(iface)->palette = palette;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_current_texture_palette(IDirect3DDevice9Ex *iface, UINT *palette)
{
    if (!palette)
        return D3DERR_INVALIDCALL;
    *palette = impl(iface)->palette;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_scissor_rect(IDirect3DDevice9Ex *iface, const RECT *rect)
{
    D9Device *d = impl(iface);
    if (!rect)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    d->scissor = *rect;
    d->viewport_dirty = TRUE;
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_scissor_rect(IDirect3DDevice9Ex *iface, RECT *rect)
{
    if (!rect)
        return D3DERR_INVALIDCALL;
    *rect = impl(iface)->scissor;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_software_vertex_processing(IDirect3DDevice9Ex *iface, BOOL software)
{
    D9Device *d = impl(iface);
    if (!(d->creation.BehaviorFlags & D3DCREATE_MIXED_VERTEXPROCESSING))
        return D3DERR_INVALIDCALL;
    d->software_vertex_processing = software;
    return D3D_OK;
}

static BOOL STDMETHODCALLTYPE device_get_software_vertex_processing(IDirect3DDevice9Ex *iface)
{
    D9Device *d = impl(iface);
    return d->software_vertex_processing || (d->creation.BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING);
}

static HRESULT STDMETHODCALLTYPE device_set_npatch_mode(IDirect3DDevice9Ex *iface, float segments)
{
    impl(iface)->npatch_segments = segments;
    return D3D_OK;
}

static float STDMETHODCALLTYPE device_get_npatch_mode(IDirect3DDevice9Ex *iface) { return impl(iface)->npatch_segments; }

/* ------------------------------------------------------------------ draws */

static HRESULT STDMETHODCALLTYPE device_draw_primitive(IDirect3DDevice9Ex *iface, D3DPRIMITIVETYPE type, UINT start,
                                                       UINT count)
{
    return not_yet(impl(iface), "DrawPrimitive");
}

static HRESULT STDMETHODCALLTYPE device_draw_indexed_primitive(IDirect3DDevice9Ex *iface, D3DPRIMITIVETYPE type,
                                                               INT base, UINT min_index, UINT vertices, UINT start,
                                                               UINT count)
{
    return not_yet(impl(iface), "DrawIndexedPrimitive");
}

static HRESULT STDMETHODCALLTYPE device_draw_primitive_up(IDirect3DDevice9Ex *iface, D3DPRIMITIVETYPE type,
                                                          UINT count, const void *data, UINT stride)
{
    return not_yet(impl(iface), "DrawPrimitiveUP");
}

static HRESULT STDMETHODCALLTYPE device_draw_indexed_primitive_up(IDirect3DDevice9Ex *iface, D3DPRIMITIVETYPE type,
                                                                  UINT min_index, UINT vertices, UINT count,
                                                                  const void *indices, D3DFORMAT index_format,
                                                                  const void *data, UINT stride)
{
    return not_yet(impl(iface), "DrawIndexedPrimitiveUP");
}

static HRESULT STDMETHODCALLTYPE device_process_vertices(IDirect3DDevice9Ex *iface, UINT source, UINT dest,
                                                         UINT count, IDirect3DVertexBuffer9 *buffer,
                                                         IDirect3DVertexDeclaration9 *declaration, DWORD flags)
{
    return not_yet(impl(iface), "ProcessVertices");
}

static HRESULT STDMETHODCALLTYPE device_create_vertex_declaration(IDirect3DDevice9Ex *iface,
                                                                  const D3DVERTEXELEMENT9 *elements,
                                                                  IDirect3DVertexDeclaration9 **declaration)
{
    return not_yet(impl(iface), "CreateVertexDeclaration");
}

static HRESULT STDMETHODCALLTYPE device_set_vertex_declaration(IDirect3DDevice9Ex *iface,
                                                               IDirect3DVertexDeclaration9 *declaration)
{
    return declaration ? not_yet(impl(iface), "SetVertexDeclaration") : D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_vertex_declaration(IDirect3DDevice9Ex *iface,
                                                               IDirect3DVertexDeclaration9 **declaration)
{
    if (!declaration)
        return D3DERR_INVALIDCALL;
    *declaration = NULL;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_fvf(IDirect3DDevice9Ex *iface, DWORD fvf)
{
    impl(iface)->fvf = fvf;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_fvf(IDirect3DDevice9Ex *iface, DWORD *fvf)
{
    if (!fvf)
        return D3DERR_INVALIDCALL;
    *fvf = impl(iface)->fvf;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_create_vertex_shader(IDirect3DDevice9Ex *iface, const DWORD *code,
                                                             IDirect3DVertexShader9 **shader)
{
    return not_yet(impl(iface), "CreateVertexShader");
}

static HRESULT STDMETHODCALLTYPE device_set_vertex_shader(IDirect3DDevice9Ex *iface, IDirect3DVertexShader9 *shader)
{
    return shader ? not_yet(impl(iface), "SetVertexShader") : D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_vertex_shader(IDirect3DDevice9Ex *iface, IDirect3DVertexShader9 **shader)
{
    if (!shader)
        return D3DERR_INVALIDCALL;
    *shader = NULL;
    return D3D_OK;
}

/* shader constants: kept in the device and uploaded with the draws that read them. */
static HRESULT set_constants(D9Device *d, void *bank, UINT size, UINT max, UINT start, const void *data, UINT count,
                             BOOL *dirty)
{
    if (!data && count)
        return D3DERR_INVALIDCALL;
    if (start >= max || count > max - start)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    memcpy((BYTE *)bank + (SIZE_T)start * size, data, (SIZE_T)count * size);
    *dirty = TRUE;
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT get_constants(const void *bank, UINT size, UINT max, UINT start, void *data, UINT count)
{
    if (!data || start >= max || count > max - start)
        return D3DERR_INVALIDCALL;
    memcpy(data, (const BYTE *)bank + (SIZE_T)start * size, (SIZE_T)count * size);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_vs_constant_f(IDirect3DDevice9Ex *iface, UINT start, const float *data,
                                                          UINT count)
{
    D9Device *d = impl(iface);
    return set_constants(d, d->vs_f, 16, D9_VS_CONSTANTS, start, data, count, &d->vs_consts_dirty);
}

static HRESULT STDMETHODCALLTYPE device_get_vs_constant_f(IDirect3DDevice9Ex *iface, UINT start, float *data,
                                                          UINT count)
{
    return get_constants(impl(iface)->vs_f, 16, D9_VS_CONSTANTS, start, data, count);
}

static HRESULT STDMETHODCALLTYPE device_set_vs_constant_i(IDirect3DDevice9Ex *iface, UINT start, const int *data,
                                                          UINT count)
{
    D9Device *d = impl(iface);
    return set_constants(d, d->vs_i, 16, 16, start, data, count, &d->vs_consts_dirty);
}

static HRESULT STDMETHODCALLTYPE device_get_vs_constant_i(IDirect3DDevice9Ex *iface, UINT start, int *data,
                                                          UINT count)
{
    return get_constants(impl(iface)->vs_i, 16, 16, start, data, count);
}

static HRESULT STDMETHODCALLTYPE device_set_vs_constant_b(IDirect3DDevice9Ex *iface, UINT start, const BOOL *data,
                                                          UINT count)
{
    D9Device *d = impl(iface);
    return set_constants(d, d->vs_b, 4, 16, start, data, count, &d->vs_consts_dirty);
}

static HRESULT STDMETHODCALLTYPE device_get_vs_constant_b(IDirect3DDevice9Ex *iface, UINT start, BOOL *data,
                                                          UINT count)
{
    return get_constants(impl(iface)->vs_b, 4, 16, start, data, count);
}

static HRESULT STDMETHODCALLTYPE device_set_stream_source(IDirect3DDevice9Ex *iface, UINT stream,
                                                          IDirect3DVertexBuffer9 *buffer, UINT offset, UINT stride)
{
    D9Device *d = impl(iface);
    D9Buffer *b = D9BufferFrom(buffer);
    if (stream >= D9_MAX_STREAMS || (b && b->base.type != D3DRTYPE_VERTEXBUFFER))
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    set_buffer(&d->streams[stream].buffer, b);
    d->streams[stream].offset = b ? offset : 0;
    d->streams[stream].stride = b ? stride : 0;
    d->streams_dirty |= 1u << stream;
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_stream_source(IDirect3DDevice9Ex *iface, UINT stream,
                                                          IDirect3DVertexBuffer9 **buffer, UINT *offset, UINT *stride)
{
    D9Device *d = impl(iface);
    if (stream >= D9_MAX_STREAMS || !buffer || !offset || !stride)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    D9Buffer *b = d->streams[stream].buffer;
    *buffer = b ? &b->iface.vb : NULL;
    if (b)
        D9ResourceAddRef(&b->base);
    *offset = d->streams[stream].offset;
    *stride = d->streams[stream].stride;
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_stream_source_freq(IDirect3DDevice9Ex *iface, UINT stream, UINT setting)
{
    D9Device *d = impl(iface);
    if (stream >= D9_MAX_STREAMS)
        return D3DERR_INVALIDCALL;
    d->stream_freq[stream] = setting;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_stream_source_freq(IDirect3DDevice9Ex *iface, UINT stream, UINT *setting)
{
    D9Device *d = impl(iface);
    if (stream >= D9_MAX_STREAMS || !setting)
        return D3DERR_INVALIDCALL;
    *setting = d->stream_freq[stream];
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_indices(IDirect3DDevice9Ex *iface, IDirect3DIndexBuffer9 *buffer)
{
    D9Device *d = impl(iface);
    D9Buffer *b = D9BufferFrom(buffer);
    if (b && b->base.type != D3DRTYPE_INDEXBUFFER)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    set_buffer(&d->indices, b);
    d->indices_dirty = TRUE;
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_indices(IDirect3DDevice9Ex *iface, IDirect3DIndexBuffer9 **buffer)
{
    D9Device *d = impl(iface);
    if (!buffer)
        return D3DERR_INVALIDCALL;
    D9Lock(d);
    D9Buffer *b = d->indices;
    *buffer = b ? &b->iface.ib : NULL;
    if (b)
        D9ResourceAddRef(&b->base);
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_create_pixel_shader(IDirect3DDevice9Ex *iface, const DWORD *code,
                                                            IDirect3DPixelShader9 **shader)
{
    return not_yet(impl(iface), "CreatePixelShader");
}

static HRESULT STDMETHODCALLTYPE device_set_pixel_shader(IDirect3DDevice9Ex *iface, IDirect3DPixelShader9 *shader)
{
    return shader ? not_yet(impl(iface), "SetPixelShader") : D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_pixel_shader(IDirect3DDevice9Ex *iface, IDirect3DPixelShader9 **shader)
{
    if (!shader)
        return D3DERR_INVALIDCALL;
    *shader = NULL;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_ps_constant_f(IDirect3DDevice9Ex *iface, UINT start, const float *data,
                                                          UINT count)
{
    D9Device *d = impl(iface);
    return set_constants(d, d->ps_f, 16, D9_PS_CONSTANTS, start, data, count, &d->ps_consts_dirty);
}

static HRESULT STDMETHODCALLTYPE device_get_ps_constant_f(IDirect3DDevice9Ex *iface, UINT start, float *data,
                                                          UINT count)
{
    return get_constants(impl(iface)->ps_f, 16, D9_PS_CONSTANTS, start, data, count);
}

static HRESULT STDMETHODCALLTYPE device_set_ps_constant_i(IDirect3DDevice9Ex *iface, UINT start, const int *data,
                                                          UINT count)
{
    D9Device *d = impl(iface);
    return set_constants(d, d->ps_i, 16, 16, start, data, count, &d->ps_consts_dirty);
}

static HRESULT STDMETHODCALLTYPE device_get_ps_constant_i(IDirect3DDevice9Ex *iface, UINT start, int *data,
                                                          UINT count)
{
    return get_constants(impl(iface)->ps_i, 16, 16, start, data, count);
}

static HRESULT STDMETHODCALLTYPE device_set_ps_constant_b(IDirect3DDevice9Ex *iface, UINT start, const BOOL *data,
                                                          UINT count)
{
    D9Device *d = impl(iface);
    return set_constants(d, d->ps_b, 4, 16, start, data, count, &d->ps_consts_dirty);
}

static HRESULT STDMETHODCALLTYPE device_get_ps_constant_b(IDirect3DDevice9Ex *iface, UINT start, BOOL *data,
                                                          UINT count)
{
    return get_constants(impl(iface)->ps_b, 4, 16, start, data, count);
}

/* patches: no gpu tessellates direct3d 9's, as no driver of windows 10 does. */
static HRESULT STDMETHODCALLTYPE device_draw_rect_patch(IDirect3DDevice9Ex *iface, UINT handle,
                                                        const float *segments, const D3DRECTPATCH_INFO *info)
{
    return D3DERR_INVALIDCALL;
}

static HRESULT STDMETHODCALLTYPE device_draw_tri_patch(IDirect3DDevice9Ex *iface, UINT handle, const float *segments,
                                                       const D3DTRIPATCH_INFO *info)
{
    return D3DERR_INVALIDCALL;
}

static HRESULT STDMETHODCALLTYPE device_delete_patch(IDirect3DDevice9Ex *iface, UINT handle)
{
    return D3DERR_INVALIDCALL;
}

static HRESULT STDMETHODCALLTYPE device_create_query(IDirect3DDevice9Ex *iface, D3DQUERYTYPE type,
                                                     IDirect3DQuery9 **query)
{
    return not_yet(impl(iface), "CreateQuery");
}

/* ------------------------------------------------------------------ IDirect3DDevice9Ex */

static HRESULT STDMETHODCALLTYPE device_set_convolution_mono_kernel(IDirect3DDevice9Ex *iface, UINT width,
                                                                    UINT height, float *rows, float *columns)
{
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_compose_rects(IDirect3DDevice9Ex *iface, IDirect3DSurface9 *source,
                                                      IDirect3DSurface9 *dest, IDirect3DVertexBuffer9 *source_rects,
                                                      UINT count, IDirect3DVertexBuffer9 *dest_rects,
                                                      D3DCOMPOSERECTSOP op, int x, int y)
{
    return not_yet(impl(iface), "ComposeRects");
}

static HRESULT STDMETHODCALLTYPE device_present_ex(IDirect3DDevice9Ex *iface, const RECT *source, const RECT *dest,
                                                   HWND window, const RGNDATA *dirty, DWORD flags)
{
    D9Device *d = impl(iface);
    D9Lock(d);
    HRESULT hr = D9Present(d->swapchains[0], source, dest, window, flags);
    D9Unlock(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_get_gpu_thread_priority(IDirect3DDevice9Ex *iface, INT *priority)
{
    if (!priority)
        return D3DERR_INVALIDCALL;
    *priority = impl(iface)->gpu_priority;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_gpu_thread_priority(IDirect3DDevice9Ex *iface, INT priority)
{
    if (priority < -7 || priority > 7)
        return D3DERR_INVALIDCALL;
    impl(iface)->gpu_priority = priority;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_wait_for_vblank(IDirect3DDevice9Ex *iface, UINT swapchain)
{
    D9Device *d = impl(iface);
    if (swapchain >= d->swapchain_count)
        return D3DERR_INVALIDCALL;
    IDXGIOutput *output = NULL;
    IDXGISwapChain *sc = d->swapchains[swapchain]->dxgi;
    if (SUCCEEDED(sc->lpVtbl->GetContainingOutput(sc, &output)) && output) {
        output->lpVtbl->WaitForVBlank(output);
        output->lpVtbl->Release(output);
    }
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_check_resource_residency(IDirect3DDevice9Ex *iface,
                                                                 IDirect3DResource9 **resources, UINT32 count)
{
    return count > 65535 ? D3DERR_INVALIDCALL : D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_maximum_frame_latency(IDirect3DDevice9Ex *iface, UINT latency)
{
    if (latency > 30)
        return D3DERR_INVALIDCALL;
    impl(iface)->max_frame_latency = latency ? latency : 3;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_maximum_frame_latency(IDirect3DDevice9Ex *iface, UINT *latency)
{
    if (!latency)
        return D3DERR_INVALIDCALL;
    *latency = impl(iface)->max_frame_latency;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_check_device_state(IDirect3DDevice9Ex *iface, HWND window)
{
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_create_render_target_ex(IDirect3DDevice9Ex *iface, UINT width, UINT height,
                                                                D3DFORMAT format, D3DMULTISAMPLE_TYPE samples,
                                                                DWORD quality, BOOL lockable,
                                                                IDirect3DSurface9 **out, HANDLE *shared, DWORD usage)
{
    return create_surface(impl(iface), width, height, format, D3DUSAGE_RENDERTARGET | usage, D3DPOOL_DEFAULT,
                          samples, quality, lockable, out, shared);
}

static HRESULT STDMETHODCALLTYPE device_create_offscreen_plain_surface_ex(IDirect3DDevice9Ex *iface, UINT width,
                                                                         UINT height, D3DFORMAT format,
                                                                         D3DPOOL pool, IDirect3DSurface9 **out,
                                                                         HANDLE *shared, DWORD usage)
{
    if (pool == D3DPOOL_MANAGED)
        return D3DERR_INVALIDCALL;
    return create_surface(impl(iface), width, height, format, usage, pool, D3DMULTISAMPLE_NONE, 0, TRUE, out,
                          shared);
}

static HRESULT STDMETHODCALLTYPE device_create_depth_stencil_surface_ex(IDirect3DDevice9Ex *iface, UINT width,
                                                                       UINT height, D3DFORMAT format,
                                                                       D3DMULTISAMPLE_TYPE samples, DWORD quality,
                                                                       BOOL discard, IDirect3DSurface9 **out,
                                                                       HANDLE *shared, DWORD usage)
{
    return create_surface(impl(iface), width, height, format, D3DUSAGE_DEPTHSTENCIL | usage, D3DPOOL_DEFAULT,
                          samples, quality, FALSE, out, shared);
}

static HRESULT STDMETHODCALLTYPE device_reset_ex(IDirect3DDevice9Ex *iface, D3DPRESENT_PARAMETERS *params,
                                                 D3DDISPLAYMODEEX *mode)
{
    D9Device *d = impl(iface);
    D9Lock(d);
    HRESULT hr = reset(d, params, mode);
    D9Unlock(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_get_display_mode_ex(IDirect3DDevice9Ex *iface, UINT swapchain,
                                                            D3DDISPLAYMODEEX *mode, D3DDISPLAYROTATION *rotation)
{
    D9Device *d = impl(iface);
    if (swapchain >= d->swapchain_count)
        return D3DERR_INVALIDCALL;
    return d->d3d->lpVtbl->GetAdapterDisplayModeEx(d->d3d, d->creation.AdapterOrdinal, mode, rotation);
}

const IDirect3DDevice9ExVtbl D9DeviceVtbl = {
    device_query,
    device_add_ref,
    device_release,
    device_test_cooperative_level,
    device_get_available_texture_mem,
    device_evict_managed_resources,
    device_get_direct3d,
    device_get_device_caps,
    device_get_display_mode,
    device_get_creation_parameters,
    device_set_cursor_properties,
    device_set_cursor_position,
    device_show_cursor,
    device_create_additional_swap_chain,
    device_get_swap_chain,
    device_get_number_of_swap_chains,
    device_reset,
    device_present,
    device_get_back_buffer,
    device_get_raster_status,
    device_set_dialog_box_mode,
    device_set_gamma_ramp,
    device_get_gamma_ramp,
    device_create_texture,
    device_create_volume_texture,
    device_create_cube_texture,
    device_create_vertex_buffer,
    device_create_index_buffer,
    device_create_render_target,
    device_create_depth_stencil_surface,
    device_update_surface,
    device_update_texture,
    device_get_render_target_data,
    device_get_front_buffer_data,
    device_stretch_rect,
    device_color_fill,
    device_create_offscreen_plain_surface,
    device_set_render_target,
    device_get_render_target,
    device_set_depth_stencil_surface,
    device_get_depth_stencil_surface,
    device_begin_scene,
    device_end_scene,
    device_clear,
    device_set_transform,
    device_get_transform,
    device_multiply_transform,
    device_set_viewport,
    device_get_viewport,
    device_set_material,
    device_get_material,
    device_set_light,
    device_get_light,
    device_light_enable,
    device_get_light_enable,
    device_set_clip_plane,
    device_get_clip_plane,
    device_set_render_state,
    device_get_render_state,
    device_create_state_block,
    device_begin_state_block,
    device_end_state_block,
    device_set_clip_status,
    device_get_clip_status,
    device_get_texture,
    device_set_texture,
    device_get_texture_stage_state,
    device_set_texture_stage_state,
    device_get_sampler_state,
    device_set_sampler_state,
    device_validate_device,
    device_set_palette_entries,
    device_get_palette_entries,
    device_set_current_texture_palette,
    device_get_current_texture_palette,
    device_set_scissor_rect,
    device_get_scissor_rect,
    device_set_software_vertex_processing,
    device_get_software_vertex_processing,
    device_set_npatch_mode,
    device_get_npatch_mode,
    device_draw_primitive,
    device_draw_indexed_primitive,
    device_draw_primitive_up,
    device_draw_indexed_primitive_up,
    device_process_vertices,
    device_create_vertex_declaration,
    device_set_vertex_declaration,
    device_get_vertex_declaration,
    device_set_fvf,
    device_get_fvf,
    device_create_vertex_shader,
    device_set_vertex_shader,
    device_get_vertex_shader,
    device_set_vs_constant_f,
    device_get_vs_constant_f,
    device_set_vs_constant_i,
    device_get_vs_constant_i,
    device_set_vs_constant_b,
    device_get_vs_constant_b,
    device_set_stream_source,
    device_get_stream_source,
    device_set_stream_source_freq,
    device_get_stream_source_freq,
    device_set_indices,
    device_get_indices,
    device_create_pixel_shader,
    device_set_pixel_shader,
    device_get_pixel_shader,
    device_set_ps_constant_f,
    device_get_ps_constant_f,
    device_set_ps_constant_i,
    device_get_ps_constant_i,
    device_set_ps_constant_b,
    device_get_ps_constant_b,
    device_draw_rect_patch,
    device_draw_tri_patch,
    device_delete_patch,
    device_create_query,
    device_set_convolution_mono_kernel,
    device_compose_rects,
    device_present_ex,
    device_get_gpu_thread_priority,
    device_set_gpu_thread_priority,
    device_wait_for_vblank,
    device_check_resource_residency,
    device_set_maximum_frame_latency,
    device_get_maximum_frame_latency,
    device_check_device_state,
    device_create_render_target_ex,
    device_create_offscreen_plain_surface_ex,
    device_create_depth_stencil_surface_ex,
    device_reset_ex,
    device_get_display_mode_ex,
};
