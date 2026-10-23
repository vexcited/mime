/*
 * IDirect3DTexture and IDirect3DTexture2: an object aggregated in a surface
 * of a first version directdraw object on query, sharing the surface's
 * refcount. a first interface device names it through a handle, one per
 * device.
 */
#include "d3d_private.h"

static const IDirect3DTextureVtbl texture1_vtbl;
static const IDirect3DTexture2Vtbl texture2_vtbl;
static const IUnknownVtbl inner_vtbl;

D3DTEXOBJ *D3dTextureFrom(void *iface)
{
    void **vtbl = iface;
    if (!vtbl)
        return nullptr;
    if (*vtbl == &texture1_vtbl)
        return CONTAINING_RECORD(iface, D3DTEXOBJ, iface1);
    if (*vtbl == &texture2_vtbl)
        return CONTAINING_RECORD(iface, D3DTEXOBJ, iface2);
    return nullptr;
}

static IUnknown *outer_of(D3DTEXOBJ *t)
{
    return DdSurfIface(t->surface, DDV1);
}

HRESULT D3dCreateTextureForSurface(DDSURF *surface, REFIID riid, IUnknown **inner)
{
    *inner = nullptr;
    if (!IsEqualGUID(riid, &IID_IDirect3DTexture) && !IsEqualGUID(riid, &IID_IDirect3DTexture2))
        return E_NOINTERFACE;
    D3DTEXOBJ *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    t->iface1.lpVtbl = &texture1_vtbl;
    t->iface2.lpVtbl = &texture2_vtbl;
    t->inner.lpVtbl = &inner_vtbl;
    t->ref = 1;
    t->surface = surface;
    InitializeListHead(&t->handles);
    *inner = &t->inner;
    return D3D_OK;
}

/* materials naming the handle forget it, and so does the device's texture state. */
static void free_handle(D3DTEXHANDLE *h)
{
    D3DDEV *dev = h->dev;
    DWORD value = (DWORD)(ULONG_PTR)h;
    for (LIST_ENTRY *e = dev->legacy->materials.Flink; e != &dev->legacy->materials; e = e->Flink) {
        D3DMATHANDLE *m = CONTAINING_RECORD(e, D3DMATHANDLE, dev_links);
        if (m->data.hTexture == value)
            m->data.hTexture = 0;
    }
    if (dev->rs[D3DRENDERSTATE_TEXTUREHANDLE] == value)
        dev->rs[D3DRENDERSTATE_TEXTUREHANDLE] = 0;
    for (DWORD i = 0; i < D3D_STAGES; i++)
        if (dev->textures[i] == h->texture->surface && (dev->weak_textures >> i & 1))
            D3dSetTexture(dev, i, nullptr);
    RemoveEntryList(&h->tex_links);
    RemoveEntryList(&h->dev_links);
    HeapFree(GetProcessHeap(), 0, h);
}

void D3dTextureDeviceGone(D3DTEXHANDLE *h)
{
    free_handle(h);
}

D3DTEXHANDLE *D3dTextureHandle(D3DDEV *dev, DWORD handle)
{
    if (!handle || !dev->legacy)
        return nullptr;
    for (LIST_ENTRY *e = dev->legacy->textures.Flink; e != &dev->legacy->textures; e = e->Flink) {
        D3DTEXHANDLE *h = CONTAINING_RECORD(e, D3DTEXHANDLE, dev_links);
        if ((DWORD)(ULONG_PTR)h == handle)
            return h;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ the inner unknown, the surface's */

static D3DTEXOBJ *from_inner(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, D3DTEXOBJ, inner);
}

static HRESULT STDMETHODCALLTYPE inner_query(IUnknown *This, REFIID riid, void **out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    D3DTEXOBJ *t = from_inner(This);
    if (IsEqualGUID(riid, &IID_IUnknown))
        *out = &t->inner;
    else if (IsEqualGUID(riid, &IID_IDirect3DTexture))
        *out = &t->iface1;
    else if (IsEqualGUID(riid, &IID_IDirect3DTexture2))
        *out = &t->iface2;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    if (*out == &t->inner)
        t->ref++;
    else
        ((IUnknown *)*out)->lpVtbl->AddRef(*out);
    return D3D_OK;
}

static ULONG STDMETHODCALLTYPE inner_add_ref(IUnknown *This)
{
    return (ULONG)++from_inner(This)->ref;
}

static ULONG STDMETHODCALLTYPE inner_release(IUnknown *This)
{
    D3DTEXOBJ *t = from_inner(This);
    ULONG ref = (ULONG)--t->ref;
    if (!ref) {
        DdEnter();
        while (!IsListEmpty(&t->handles))
            free_handle(CONTAINING_RECORD(t->handles.Flink, D3DTEXHANDLE, tex_links));
        t->iface1.lpVtbl = nullptr;
        t->iface2.lpVtbl = nullptr;
        HeapFree(GetProcessHeap(), 0, t);
        DdLeave(D3D_OK);
    }
    return ref;
}

static const IUnknownVtbl inner_vtbl = {inner_query, inner_add_ref, inner_release};

/* ------------------------------------------------------------------ the interfaces, whose IUnknown is the surface */

static HRESULT get_handle(D3DTEXOBJ *t, D3DDEV *dev, D3DTEXTUREHANDLE *handle, BOOL check_size)
{
    if (!dev || !handle || !dev->legacy)
        return DDERR_INVALIDOBJECT;
    if (t->surface->desc.ddsCaps.dwCaps2 & DDSCAPS2_TEXTUREMANAGE)
        return DDERR_INVALIDOBJECT;
    *handle = 0;
    DdEnter();
    for (LIST_ENTRY *e = t->handles.Flink; e != &t->handles; e = e->Flink) {
        D3DTEXHANDLE *h = CONTAINING_RECORD(e, D3DTEXHANDLE, tex_links);
        if (h->dev == dev) {
            *handle = (DWORD)(ULONG_PTR)h;
            return DdLeave(D3D_OK);
        }
    }
    /* the second interface checks the size against the device's caps. */
    if (check_size) {
        const D3DPRIMCAPS *caps = dev->legacy->hw.dcmColorModel ? &dev->legacy->hw.dpcTriCaps
                                                                 : &dev->legacy->sw.dpcTriCaps;
        DWORD w = t->surface->desc.dwWidth, h = t->surface->desc.dwHeight;
        BOOL pow2 = !(w & (w - 1)) && !(h & (h - 1));
        if ((caps->dwTextureCaps & D3DPTEXTURECAPS_POW2) && !(caps->dwTextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) &&
            !pow2)
            return DdLeave(D3DERR_TEXTURE_BADSIZE);
        if ((caps->dwTextureCaps & D3DPTEXTURECAPS_SQUAREONLY) && w != h)
            return DdLeave(D3DERR_TEXTURE_BADSIZE);
    }
    D3DTEXHANDLE *h = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h));
    if (!h)
        return DdLeave(E_OUTOFMEMORY);
    h->texture = t;
    h->dev = dev;
    InsertTailList(&t->handles, &h->tex_links);
    InsertTailList(&dev->legacy->textures, &h->dev_links);
    *handle = (DWORD)(ULONG_PTR)h;
    return DdLeave(D3D_OK);
}

/* copies the levels, palettes and color keys included. */
HRESULT D3dTextureLoad(DDSURF *dst, DDSURF *src)
{
    DWORD entries = (dst->format.pf.dwFlags & DDPF_PALETTEINDEXED8)   ? 256
                    : (dst->format.pf.dwFlags & DDPF_PALETTEINDEXED4) ? 16
                                                                      : 0;
    if (entries && src->palette) {
        if (!dst->palette)
            return DDERR_NOPALETTEATTACHED;
        memcpy(dst->palette->entries, src->palette->entries, entries * sizeof(PALETTEENTRY));
        dst->palette->uniqueness++;
    }
    while (src && dst) {
        HRESULT hr = DdBlt(dst, nullptr, src, DDV1, nullptr, DDBLT_WAIT, nullptr, DDV1);
        if (FAILED(hr))
            return hr;
        if (src->ckeys & DDSD_CKSRCBLT) {
            dst->desc.ddckCKSrcBlt = src->desc.ddckCKSrcBlt;
            dst->ckeys |= DDSD_CKSRCBLT;
        }
        if (src->ckeys & DDSD_CKDESTBLT) {
            dst->desc.ddckCKDestBlt = src->desc.ddckCKDestBlt;
            dst->ckeys |= DDSD_CKDESTBLT;
        }
        DDSURF *sn = nullptr, *dn = nullptr;
        for (LIST_ENTRY *e = src->attached.Flink; e != &src->attached; e = e->Flink) {
            DDSURF *a = CONTAINING_RECORD(e, DDATTACH, links)->surface;
            if ((a->desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP) && a->desc.dwWidth <= src->desc.dwWidth && a != src)
                sn = a;
        }
        if (!sn)
            return D3D_OK;
        for (LIST_ENTRY *e = dst->attached.Flink; e != &dst->attached; e = e->Flink) {
            DDSURF *a = CONTAINING_RECORD(e, DDATTACH, links)->surface;
            if ((a->desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP) && a->desc.dwWidth <= dst->desc.dwWidth && a != dst)
                dn = a;
        }
        if (!dn)
            return DDERR_NOTFOUND;
        src = sn;
        dst = dn;
    }
    return D3D_OK;
}

static HRESULT load(D3DTEXOBJ *t, D3DTEXOBJ *src)
{
    if (!t || !src)
        return DDERR_INVALIDOBJECT;
    DdEnter();
    return DdLeave(D3dTextureLoad(t->surface, src->surface));
}

static HRESULT STDMETHODCALLTYPE tex1_query(IDirect3DTexture *This, REFIID riid, void **out)
{
    IUnknown *outer = outer_of(CONTAINING_RECORD(This, D3DTEXOBJ, iface1));
    return outer->lpVtbl->QueryInterface(outer, riid, out);
}

static ULONG STDMETHODCALLTYPE tex1_add_ref(IDirect3DTexture *This)
{
    IUnknown *outer = outer_of(CONTAINING_RECORD(This, D3DTEXOBJ, iface1));
    return outer->lpVtbl->AddRef(outer);
}

static ULONG STDMETHODCALLTYPE tex1_release(IDirect3DTexture *This)
{
    IUnknown *outer = outer_of(CONTAINING_RECORD(This, D3DTEXOBJ, iface1));
    return outer->lpVtbl->Release(outer);
}

static HRESULT STDMETHODCALLTYPE tex1_initialize(IDirect3DTexture *This, LPDIRECT3DDEVICE device,
                                                 LPDIRECTDRAWSURFACE surface)
{
    return DDERR_ALREADYINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE tex1_get_handle(IDirect3DTexture *This, LPDIRECT3DDEVICE device,
                                                 LPD3DTEXTUREHANDLE handle)
{
    return get_handle(CONTAINING_RECORD(This, D3DTEXOBJ, iface1), D3dDevFromLegacy(device), handle, FALSE);
}

static HRESULT STDMETHODCALLTYPE tex1_palette_changed(IDirect3DTexture *This, DWORD start, DWORD count)
{
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE tex1_load(IDirect3DTexture *This, LPDIRECT3DTEXTURE source)
{
    return load(CONTAINING_RECORD(This, D3DTEXOBJ, iface1), D3dTextureFrom(source));
}

static HRESULT STDMETHODCALLTYPE tex1_unload(IDirect3DTexture *This)
{
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE tex2_query(IDirect3DTexture2 *This, REFIID riid, void **out)
{
    IUnknown *outer = outer_of(CONTAINING_RECORD(This, D3DTEXOBJ, iface2));
    return outer->lpVtbl->QueryInterface(outer, riid, out);
}

static ULONG STDMETHODCALLTYPE tex2_add_ref(IDirect3DTexture2 *This)
{
    IUnknown *outer = outer_of(CONTAINING_RECORD(This, D3DTEXOBJ, iface2));
    return outer->lpVtbl->AddRef(outer);
}

static ULONG STDMETHODCALLTYPE tex2_release(IDirect3DTexture2 *This)
{
    IUnknown *outer = outer_of(CONTAINING_RECORD(This, D3DTEXOBJ, iface2));
    return outer->lpVtbl->Release(outer);
}

static HRESULT STDMETHODCALLTYPE tex2_get_handle(IDirect3DTexture2 *This, LPDIRECT3DDEVICE2 device,
                                                 LPD3DTEXTUREHANDLE handle)
{
    return get_handle(CONTAINING_RECORD(This, D3DTEXOBJ, iface2), D3dDevFromLegacy(device), handle, TRUE);
}

static HRESULT STDMETHODCALLTYPE tex2_palette_changed(IDirect3DTexture2 *This, DWORD start, DWORD count)
{
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE tex2_load(IDirect3DTexture2 *This, LPDIRECT3DTEXTURE2 source)
{
    return load(CONTAINING_RECORD(This, D3DTEXOBJ, iface2), D3dTextureFrom(source));
}

static const IDirect3DTextureVtbl texture1_vtbl = {
    tex1_query, tex1_add_ref, tex1_release, tex1_initialize, tex1_get_handle, tex1_palette_changed, tex1_load,
    tex1_unload,
};

static const IDirect3DTexture2Vtbl texture2_vtbl = {
    tex2_query, tex2_add_ref, tex2_release, tex2_get_handle, tex2_palette_changed, tex2_load,
};
