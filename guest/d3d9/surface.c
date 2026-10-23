/*
 * IDirect3DSurface9. a gpu surface is a d3d11 texture or a subresource of
 * its texture, system memory and scratch surfaces are only bytes. lockable
 * surfaces keep a copy in memory: unlock uploads the locked part, and
 * locking a surface the gpu rendered to reads it back first.
 */
#include "d3d9_private.h"

D9Surface *D9SurfaceFrom(IDirect3DSurface9 *iface) { return iface ? CONTAINING_RECORD(iface, D9Surface, iface) : nullptr; }

static D9Surface *impl(IDirect3DSurface9 *iface) { return CONTAINING_RECORD(iface, D9Surface, iface); }

static void destroy(D9Surface *s)
{
    D9Device *device = s->base.device;
    if (s->dc)
        DeleteDC(s->dc);
    if (s->bitmap)
        DeleteObject(s->bitmap);
    if (s->rtv)
        s->rtv->lpVtbl->Release(s->rtv);
    if (s->rtv_srgb)
        s->rtv_srgb->lpVtbl->Release(s->rtv_srgb);
    if (s->dsv)
        s->dsv->lpVtbl->Release(s->dsv);
    if (s->texture && !s->container)
        s->texture->lpVtbl->Release(s->texture);
    if (s->memory == &s->own_memory)
        D9FreeMemory(&s->own_memory);
    D9ResourceClear(&s->base);
    HeapFree(GetProcessHeap(), 0, s);
    (void)device;
}

static void destroy_resource(D9Resource *r) { destroy(CONTAINING_RECORD(r, D9Surface, base)); }

/* a texture level lives as long as its texture, which holds its references. */
ULONG D9SurfaceAddRef(D9Surface *s)
{
    if (s->container)
        return s->container->iface.base.lpVtbl->AddRef(&s->container->iface.base);
    return D9ResourceAddRef(&s->base);
}

ULONG D9SurfaceRelease(D9Surface *s)
{
    if (s->container)
        return s->container->iface.base.lpVtbl->Release(&s->container->iface.base);
    return D9ResourceRelease(&s->base);
}

void D9DestroyImplicitSurface(D9Surface *s)
{
    if (s)
        destroy(s);
}

/* ------------------------------------------------------------------ views */

static BOOL is_cube(const D9Surface *s) { return s->container && s->container->faces == 6; }

ID3D11RenderTargetView *D9SurfaceRtv(D9Surface *s, BOOL srgb)
{
    const D9Format *f = s->base.format;
    srgb = srgb && f->srgb != DXGI_FORMAT_UNKNOWN;
    ID3D11RenderTargetView **slot = srgb ? &s->rtv_srgb : &s->rtv;
    if (*slot || !s->texture)
        return *slot;
    D3D11_RENDER_TARGET_VIEW_DESC desc = {srgb ? f->srgb : f->view};
    if (s->samples > 1) {
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
    } else if (is_cube(s)) {
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipSlice = s->level;
        desc.Texture2DArray.FirstArraySlice = s->face;
        desc.Texture2DArray.ArraySize = 1;
    } else {
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = s->level;
    }
    ID3D11Device1 *d3d11 = s->base.device->d3d11;
    if (FAILED(d3d11->lpVtbl->CreateRenderTargetView(d3d11, (ID3D11Resource *)s->texture, &desc, slot)))
        *slot = nullptr;
    return *slot;
}

ID3D11DepthStencilView *D9SurfaceDsv(D9Surface *s)
{
    const D9Format *f = s->base.format;
    if (s->dsv || !s->texture || f->depth == DXGI_FORMAT_UNKNOWN)
        return s->dsv;
    D3D11_DEPTH_STENCIL_VIEW_DESC desc = {f->depth};
    if (s->samples > 1) {
        desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
    } else if (is_cube(s)) {
        desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipSlice = s->level;
        desc.Texture2DArray.FirstArraySlice = s->face;
        desc.Texture2DArray.ArraySize = 1;
    } else {
        desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = s->level;
    }
    ID3D11Device1 *d3d11 = s->base.device->d3d11;
    if (FAILED(d3d11->lpVtbl->CreateDepthStencilView(d3d11, (ID3D11Resource *)s->texture, &desc, &s->dsv)))
        s->dsv = nullptr;
    return s->dsv;
}

/* ------------------------------------------------------------------ memory */

static BOOL in_gpu(const D9Surface *s) { return s->texture != nullptr; }

/* whether the memory copy is current without reading back from the gpu. */
static BOOL memory_current(const D9Surface *s)
{
    return !in_gpu(s) || s->base.pool == D3DPOOL_MANAGED || s->memory_valid;
}

static HRESULT ensure_memory(D9Surface *s)
{
    if (s->memory->bytes)
        return D3D_OK;
    return D9AllocMemory(s->memory, s->base.format, s->width, s->height, 1);
}

UINT D9SurfaceSubresource(const D9Surface *s)
{
    return s->container ? s->face * s->container->d3d_levels + s->level : 0;
}

/* multisampled surfaces are resolved first. */
HRESULT D9SurfaceDownload(D9Surface *s)
{
    D9Device *device = s->base.device;
    const D9Format *f = s->base.format;
    HRESULT hr = ensure_memory(s);
    if (FAILED(hr) || !in_gpu(s))
        return hr;
    ID3D11Device1 *d3d11 = device->d3d11;
    ID3D11DeviceContext1 *ctx = device->context;
    D3D11_TEXTURE2D_DESC desc = {s->width, s->height, 1, 1, f->resource, {1, 0}, D3D11_USAGE_STAGING, 0,
                                 D3D11_CPU_ACCESS_READ};
    ID3D11Texture2D *staging, *resolved = nullptr;
    if (FAILED(hr = d3d11->lpVtbl->CreateTexture2D(d3d11, &desc, nullptr, &staging)))
        return hr;
    ID3D11Resource *source = (ID3D11Resource *)s->texture;
    UINT sub = D9SurfaceSubresource(s);
    if (s->samples > 1) {
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags = 0;
        if (SUCCEEDED(d3d11->lpVtbl->CreateTexture2D(d3d11, &desc, nullptr, &resolved))) {
            ctx->lpVtbl->ResolveSubresource(ctx, (ID3D11Resource *)resolved, 0, source, 0, f->view);
            source = (ID3D11Resource *)resolved;
            sub = 0;
        }
    }
    ctx->lpVtbl->CopySubresourceRegion(ctx, (ID3D11Resource *)staging, 0, 0, 0, 0, source, sub, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(hr = ctx->lpVtbl->Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        UINT rows = D9FormatRows(f, s->height);
        UINT bytes = D9_MIN(s->memory->pitch, mapped.RowPitch);
        for (UINT y = 0; y < rows; y++)
            memcpy(s->memory->bytes + (SIZE_T)y * s->memory->pitch, (BYTE *)mapped.pData + (SIZE_T)y * mapped.RowPitch,
                   bytes);
        ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)staging, 0);
        s->memory_valid = TRUE;
    }
    if (resolved)
        resolved->lpVtbl->Release(resolved);
    staging->lpVtbl->Release(staging);
    return hr;
}

/* widens a rectangle to whole blocks. */
RECT D9SurfaceBlockRect(const D9Surface *s, const RECT *rect)
{
    const D9Format *f = s->base.format;
    RECT r = rect ? *rect : (RECT){0, 0, (LONG)s->width, (LONG)s->height};
    if (f->block > 1) {
        r.left &= ~(LONG)(f->block - 1);
        r.top &= ~(LONG)(f->block - 1);
        r.right = D9_MIN((LONG)D9_ALIGN((UINT)r.right, f->block), (LONG)D9_ALIGN(s->width, f->block));
        r.bottom = D9_MIN((LONG)D9_ALIGN((UINT)r.bottom, f->block), (LONG)D9_ALIGN(s->height, f->block));
    }
    return r;
}

/* uploads in gpu order, after the work queued before it. */
void D9SurfaceUpload(D9Surface *s, const RECT *rect)
{
    if (!in_gpu(s) || !s->memory->bytes)
        return;
    RECT r = D9SurfaceBlockRect(s, rect);
    D9SurfaceUploadBytes(s, &r, D9MemoryAt(s->memory, s->base.format, r.left, r.top, 0), s->memory->pitch);
}

/* bytes in the locked layout, to a block aligned rectangle of the texture. */
void D9SurfaceUploadBytes(D9Surface *s, const RECT *r, const BYTE *bytes, UINT pitch)
{
    const D9Format *f = s->base.format;
    D3D11_BOX box = {(UINT)r->left, (UINT)r->top, 0, (UINT)r->right, (UINT)r->bottom, 1};
    UINT width = r->right - r->left, rows = D9FormatRows(f, r->bottom - r->top);
    BYTE *converted = nullptr;
    if (f->stored != f->bytes) {
        UINT out_pitch = D9_ALIGN(width * f->stored, 4);
        converted = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)out_pitch * rows);
        if (!converted)
            return;
        D9ConvertUpload(f, bytes, pitch, converted, out_pitch, width, rows);
        bytes = converted;
        pitch = out_pitch;
    }
    ID3D11DeviceContext1 *ctx = s->base.device->context;
    ctx->lpVtbl->UpdateSubresource(ctx, (ID3D11Resource *)s->texture, D9SurfaceSubresource(s), &box, bytes, pitch, 0);
    HeapFree(GetProcessHeap(), 0, converted);
}

/* the gpu wrote the surface, so its memory copy is stale. */
void D9SurfaceWritten(D9Surface *s)
{
    if (s->base.pool != D3DPOOL_MANAGED)
        s->memory_valid = FALSE;
}

/* ------------------------------------------------------------------ IDirect3DSurface9 */

static HRESULT STDMETHODCALLTYPE surface_query(IDirect3DSurface9 *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DResource9) ||
        IsEqualGUID(riid, &IID_IDirect3DSurface9)) {
        *out = iface;
        D9SurfaceAddRef(impl(iface));
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE surface_add_ref(IDirect3DSurface9 *iface) { return D9SurfaceAddRef(impl(iface)); }

static ULONG STDMETHODCALLTYPE surface_release(IDirect3DSurface9 *iface) { return D9SurfaceRelease(impl(iface)); }

static HRESULT STDMETHODCALLTYPE surface_get_device(IDirect3DSurface9 *iface, IDirect3DDevice9 **device)
{
    return D9ResourceGetDevice(&impl(iface)->base, device);
}

static HRESULT STDMETHODCALLTYPE surface_set_private_data(IDirect3DSurface9 *iface, REFGUID guid, const void *data,
                                                          DWORD size, DWORD flags)
{
    return D9ResourceSetPrivateData(&impl(iface)->base, guid, data, size, flags);
}

static HRESULT STDMETHODCALLTYPE surface_get_private_data(IDirect3DSurface9 *iface, REFGUID guid, void *data,
                                                          DWORD *size)
{
    return D9ResourceGetPrivateData(&impl(iface)->base, guid, data, size);
}

static HRESULT STDMETHODCALLTYPE surface_free_private_data(IDirect3DSurface9 *iface, REFGUID guid)
{
    return D9ResourceFreePrivateData(&impl(iface)->base, guid);
}

static DWORD STDMETHODCALLTYPE surface_set_priority(IDirect3DSurface9 *iface, DWORD priority)
{
    return D9ResourceSetPriority(&impl(iface)->base, priority);
}

static DWORD STDMETHODCALLTYPE surface_get_priority(IDirect3DSurface9 *iface) { return impl(iface)->base.priority; }

static void STDMETHODCALLTYPE surface_pre_load(IDirect3DSurface9 *iface) {}

static D3DRESOURCETYPE STDMETHODCALLTYPE surface_get_type(IDirect3DSurface9 *iface) { return D3DRTYPE_SURFACE; }

static HRESULT STDMETHODCALLTYPE surface_get_container(IDirect3DSurface9 *iface, REFIID riid, void **out)
{
    D9Surface *s = impl(iface);
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = nullptr;
    if (s->container)
        return s->container->iface.base.lpVtbl->QueryInterface(&s->container->iface.base, riid, out);
    if (s->swapchain)
        return s->swapchain->iface.lpVtbl->QueryInterface(&s->swapchain->iface, riid, out);
    return s->base.device->iface.lpVtbl->QueryInterface(&s->base.device->iface, riid, out);
}

static HRESULT STDMETHODCALLTYPE surface_get_desc(IDirect3DSurface9 *iface, D3DSURFACE_DESC *desc)
{
    D9Surface *s = impl(iface);
    if (!desc)
        return D3DERR_INVALIDCALL;
    desc->Format = s->base.d3d_format;
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = s->base.usage;
    desc->Pool = s->base.pool;
    desc->MultiSampleType = s->samples;
    desc->MultiSampleQuality = s->quality;
    desc->Width = s->width;
    desc->Height = s->height;
    return D3D_OK;
}

static BOOL lockable(const D9Surface *s)
{
    if (s->samples > 1)
        return FALSE;
    if (!in_gpu(s) || s->base.pool == D3DPOOL_MANAGED || (s->base.usage & D3DUSAGE_DYNAMIC))
        return TRUE;
    return s->lockable;
}

HRESULT D9SurfaceLock(D9Surface *s, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
    const D9Format *f = s->base.format;
    if (!locked)
        return D3DERR_INVALIDCALL;
    locked->pBits = nullptr;
    locked->Pitch = 0;
    RECT r;
    if (s->memory->locked || !lockable(s) || (f->flags & D9_FORMAT_NULL) ||
        !D9LockRect(f, s->width, s->height, rect, &r))
        return D3DERR_INVALIDCALL;
    HRESULT hr;
    if (!memory_current(s) && !(flags & D3DLOCK_DISCARD))
        hr = D9SurfaceDownload(s);
    else
        hr = ensure_memory(s);
    if (FAILED(hr))
        return hr;
    s->memory->locked = TRUE;
    s->memory->lock_flags = flags;
    s->memory->lock_rect = r;
    locked->pBits = D9MemoryAt(s->memory, f, r.left, r.top, 0);
    locked->Pitch = s->memory->pitch;
    return D3D_OK;
}

HRESULT D9SurfaceUnlock(D9Surface *s)
{
    if (!s->memory->locked)
        return D3DERR_INVALIDCALL;
    s->memory->locked = FALSE;
    if (in_gpu(s) && !(s->memory->lock_flags & D3DLOCK_READONLY)) {
        D9SurfaceUpload(s, &s->memory->lock_rect);
        s->memory_valid = TRUE;
    }
    if (s->container && !(s->memory->lock_flags & (D3DLOCK_READONLY | D3DLOCK_NO_DIRTY_UPDATE)))
        s->container->autogen_dirty = TRUE;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE surface_lock_rect(IDirect3DSurface9 *iface, D3DLOCKED_RECT *locked,
                                                   const RECT *rect, DWORD flags)
{
    D9Surface *s = impl(iface);
    D9Lock(s->base.device);
    HRESULT hr = D9SurfaceLock(s, locked, rect, flags);
    D9Unlock(s->base.device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surface_unlock_rect(IDirect3DSurface9 *iface)
{
    D9Surface *s = impl(iface);
    D9Lock(s->base.device);
    HRESULT hr = D9SurfaceUnlock(s);
    D9Unlock(s->base.device);
    return hr;
}

/* only for the 32 and 16-bit formats gdi draws. */
static HRESULT STDMETHODCALLTYPE surface_get_dc(IDirect3DSurface9 *iface, HDC *dc)
{
    D9Surface *s = impl(iface);
    D3DFORMAT format = s->base.d3d_format;
    if (!dc)
        return D3DERR_INVALIDCALL;
    *dc = nullptr;
    if (s->dc || (format != D3DFMT_X8R8G8B8 && format != D3DFMT_A8R8G8B8 && format != D3DFMT_R5G6B5 &&
                  format != D3DFMT_X1R5G5B5))
        return D3DERR_INVALIDCALL;
    D3DLOCKED_RECT locked;
    HRESULT hr = D9SurfaceLock(s, &locked, nullptr, 0);
    if (FAILED(hr))
        return hr;
    struct {
        BITMAPINFOHEADER header;
        DWORD masks[3];
    } info = {{sizeof(BITMAPINFOHEADER), (LONG)s->width, -(LONG)s->height, 1, 32, BI_RGB}};
    if (format == D3DFMT_R5G6B5) {
        info.header.biBitCount = 16;
        info.header.biCompression = BI_BITFIELDS;
        info.masks[0] = 0xF800, info.masks[1] = 0x07E0, info.masks[2] = 0x001F;
    } else if (format == D3DFMT_X1R5G5B5) {
        info.header.biBitCount = 16;
    }
    void *bits;
    s->bitmap = CreateDIBSection(nullptr, (BITMAPINFO *)&info, DIB_RGB_COLORS, &bits, nullptr, 0);
    s->dc = s->bitmap ? CreateCompatibleDC(nullptr) : nullptr;
    if (!s->dc) {
        if (s->bitmap)
            DeleteObject(s->bitmap);
        s->bitmap = nullptr;
        D9SurfaceUnlock(s);
        return D3DERR_INVALIDCALL;
    }
    UINT row = s->width * (info.header.biBitCount / 8), pitch = D9_ALIGN(row, 4);
    for (UINT y = 0; y < s->height; y++)
        memcpy((BYTE *)bits + (SIZE_T)y * pitch, (BYTE *)locked.pBits + (SIZE_T)y * locked.Pitch, row);
    SelectObject(s->dc, s->bitmap);
    *dc = s->dc;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE surface_release_dc(IDirect3DSurface9 *iface, HDC dc)
{
    D9Surface *s = impl(iface);
    if (!s->dc || dc != s->dc)
        return D3DERR_INVALIDCALL;
    GdiFlush();
    DIBSECTION section;
    if (GetObjectW(s->bitmap, sizeof(section), &section) == sizeof(section)) {
        UINT row = s->width * (section.dsBm.bmBitsPixel / 8);
        for (UINT y = 0; y < s->height; y++)
            memcpy(s->memory->bytes + (SIZE_T)y * s->memory->pitch,
                   (BYTE *)section.dsBm.bmBits + (SIZE_T)y * section.dsBm.bmWidthBytes, row);
    }
    DeleteDC(s->dc);
    DeleteObject(s->bitmap);
    s->dc = nullptr;
    s->bitmap = nullptr;
    return D9SurfaceUnlock(s);
}

static const IDirect3DSurface9Vtbl surface_vtbl = {
    surface_query,
    surface_add_ref,
    surface_release,
    surface_get_device,
    surface_set_private_data,
    surface_get_private_data,
    surface_free_private_data,
    surface_set_priority,
    surface_get_priority,
    surface_pre_load,
    surface_get_type,
    surface_get_container,
    surface_get_desc,
    surface_lock_rect,
    surface_unlock_rect,
    surface_get_dc,
    surface_release_dc,
};

/* ------------------------------------------------------------------ creation */

HRESULT D9SurfaceMakeTexture(D9Surface *s)
{
    const D9Format *f = s->base.format;
    if (f->flags & D9_FORMAT_NULL)
        return D3D_OK;
    D3D11_TEXTURE2D_DESC desc = {s->width, s->height, 1, 1, f->resource, {D9_MAX((UINT)s->samples, 1), 0},
                                 D3D11_USAGE_DEFAULT};
    if (f->flags & D9_FORMAT_DEPTH)
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    else if (f->flags & D9_FORMAT_RENDER)
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    else
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (s->samples == D3DMULTISAMPLE_NONMASKABLE)
        desc.SampleDesc.Count = 2u << D9_MIN(s->quality, 2);
    ID3D11Device1 *d3d11 = s->base.device->d3d11;
    return d3d11->lpVtbl->CreateTexture2D(d3d11, &desc, nullptr, &s->texture);
}

HRESULT D9NewSurface(D9Device *device, D3DFORMAT format, DWORD usage, D3DPOOL pool, UINT width, UINT height,
                     D9Surface **out)
{
    D9Surface *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->iface.lpVtbl = &surface_vtbl;
    s->base.destroy = destroy_resource;
    s->base.device = device;
    s->base.type = D3DRTYPE_SURFACE;
    s->base.pool = pool;
    s->base.usage = usage;
    s->base.d3d_format = format;
    s->base.format = D9GetFormat(format);
    s->width = width;
    s->height = height;
    s->memory = &s->own_memory;
    *out = s;
    return D3D_OK;
}

HRESULT D9CreateSurface(D9Device *device, UINT width, UINT height, D3DFORMAT format, DWORD usage, D3DPOOL pool,
                        D3DMULTISAMPLE_TYPE samples, DWORD quality, BOOL lockable_flag, D9Surface **out)
{
    const D9Format *f = D9GetFormat(format);
    if (!width || !height || f->d3d == D3DFMT_UNKNOWN || samples > D3DMULTISAMPLE_16_SAMPLES)
        return D3DERR_INVALIDCALL;
    if ((usage & D3DUSAGE_DEPTHSTENCIL) && !(f->flags & D9_FORMAT_DEPTH))
        return D3DERR_INVALIDCALL;
    if ((usage & D3DUSAGE_RENDERTARGET) && !(f->flags & D9_FORMAT_RENDER))
        return D3DERR_INVALIDCALL;
    if ((f->flags & D9_FORMAT_NULL) && !(usage & D3DUSAGE_RENDERTARGET))
        return D3DERR_INVALIDCALL;
    D9Surface *s;
    HRESULT hr = D9NewSurface(device, format, usage, pool, width, height, &s);
    if (FAILED(hr))
        return hr;
    D9ResourceInit(&s->base, device, D3DRTYPE_SURFACE, pool, usage, format, destroy_resource);
    s->samples = samples;
    s->quality = quality;
    /* plain surfaces and lockable depth formats can always be locked, render targets only when asked. */
    s->lockable = lockable_flag || !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) ||
                  (f->flags & D9_FORMAT_LOCKABLE);
    if (pool == D3DPOOL_DEFAULT || pool == D3DPOOL_MANAGED)
        hr = D9SurfaceMakeTexture(s);
    else
        hr = D9AllocMemory(s->memory, f, width, height, 1);
    if (FAILED(hr)) {
        destroy(s);
        device->iface.lpVtbl->Release(&device->iface);
        return hr == E_OUTOFMEMORY ? hr : D3DERR_OUTOFVIDEOMEMORY;
    }
    *out = s;
    return D3D_OK;
}
