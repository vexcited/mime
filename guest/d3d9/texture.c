/*
 * IDirect3DTexture9, IDirect3DCubeTexture9 and IDirect3DVolumeTexture9: a
 * d3d11 texture with a surface or volume object per level and face, which
 * use the texture's references. system memory textures are only bytes.
 * managed ones also keep a copy in memory, which locks change and unlocks
 * upload. textures that make their own mips show programs one level.
 */
#include "d3d9_private.h"

static D9Texture *impl(IDirect3DBaseTexture9 *iface) { return CONTAINING_RECORD(iface, D9Texture, iface.base); }

D9Texture *D9TextureFrom(IDirect3DBaseTexture9 *iface) { return iface ? impl(iface) : nullptr; }

static UINT sub_count(const D9Texture *t) { return t->levels * t->faces; }

static void destroy(D9Texture *t)
{
    for (UINT i = 0; t->surfaces && i < sub_count(t); i++) {
        D9Surface *s = t->surfaces[i];
        if (!s)
            continue;
        if (s->rtv)
            s->rtv->lpVtbl->Release(s->rtv);
        if (s->rtv_srgb)
            s->rtv_srgb->lpVtbl->Release(s->rtv_srgb);
        if (s->dsv)
            s->dsv->lpVtbl->Release(s->dsv);
        if (s->dc)
            DeleteDC(s->dc);
        if (s->bitmap)
            DeleteObject(s->bitmap);
        D9ResourceClear(&s->base);
        HeapFree(GetProcessHeap(), 0, s);
    }
    for (UINT i = 0; t->volumes && i < t->levels; i++) {
        if (t->volumes[i]) {
            MimePrivateClear(&t->volumes[i]->data);
            HeapFree(GetProcessHeap(), 0, t->volumes[i]);
        }
    }
    for (UINT i = 0; t->memory && i < sub_count(t); i++)
        D9FreeMemory(&t->memory[i]);
    HeapFree(GetProcessHeap(), 0, t->surfaces);
    HeapFree(GetProcessHeap(), 0, t->volumes);
    HeapFree(GetProcessHeap(), 0, t->memory);
    if (t->srv)
        t->srv->lpVtbl->Release(t->srv);
    if (t->srv_srgb)
        t->srv_srgb->lpVtbl->Release(t->srv_srgb);
    if (t->resource)
        t->resource->lpVtbl->Release(t->resource);
    D9ResourceClear(&t->base);
    HeapFree(GetProcessHeap(), 0, t);
}

static void destroy_resource(D9Resource *r) { destroy(CONTAINING_RECORD(r, D9Texture, base)); }

/* ------------------------------------------------------------------ views */

/* starts at the set level of detail, swizzled as the format needs. */
ID3D11ShaderResourceView *D9TextureSrv(D9Texture *t, BOOL srgb)
{
    const D9Format *f = t->base.format;
    srgb = srgb && f->srgb != DXGI_FORMAT_UNKNOWN;
    ID3D11ShaderResourceView **slot = srgb ? &t->srv_srgb : &t->srv;
    if (*slot || !t->resource)
        return *slot;
    D9Device *device = t->base.device;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {srgb ? f->srgb : f->view};
    UINT lod = D9_MIN(t->lod, t->d3d_levels - 1);
    if (t->base.type == D3DRTYPE_VOLUMETEXTURE) {
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MostDetailedMip = lod;
        desc.Texture3D.MipLevels = t->d3d_levels - lod;
    } else if (t->faces == 6) {
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        desc.TextureCube.MostDetailedMip = lod;
        desc.TextureCube.MipLevels = t->d3d_levels - lod;
    } else {
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MostDetailedMip = lod;
        desc.Texture2D.MipLevels = t->d3d_levels - lod;
    }
    HRESULT hr;
    if (f->swizzle)
        hr = device->mime->lpVtbl->CreateSwizzledView(device->mime, t->resource, &desc, f->swizzle, slot);
    else
        hr = device->d3d11->lpVtbl->CreateShaderResourceView(device->d3d11, t->resource, &desc, slot);
    if (FAILED(hr))
        *slot = nullptr;
    return *slot;
}

static void drop_views(D9Texture *t)
{
    if (t->srv)
        t->srv->lpVtbl->Release(t->srv);
    if (t->srv_srgb)
        t->srv_srgb->lpVtbl->Release(t->srv_srgb);
    t->srv = t->srv_srgb = nullptr;
}

/* generates pending mips before the texture is drawn with. */
void D9TexturePrepare(D9Texture *t)
{
    if (!t->autogen_dirty || !(t->base.usage & D3DUSAGE_AUTOGENMIPMAP))
        return;
    t->autogen_dirty = FALSE;
    ID3D11ShaderResourceView *srv = D9TextureSrv(t, FALSE);
    if (srv && t->d3d_levels > 1)
        t->base.device->context->lpVtbl->GenerateMips(t->base.device->context, srv);
}

/* ------------------------------------------------------------------ IDirect3DBaseTexture9 */

static HRESULT STDMETHODCALLTYPE texture_query(IDirect3DBaseTexture9 *iface, REFIID riid, void **out)
{
    D9Texture *t = impl(iface);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    const IID *own = t->base.type == D3DRTYPE_CUBETEXTURE     ? &IID_IDirect3DCubeTexture9
                     : t->base.type == D3DRTYPE_VOLUMETEXTURE ? &IID_IDirect3DVolumeTexture9
                                                              : &IID_IDirect3DTexture9;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DResource9) ||
        IsEqualGUID(riid, &IID_IDirect3DBaseTexture9) || IsEqualGUID(riid, own)) {
        *out = iface;
        D9ResourceAddRef(&t->base);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE texture_add_ref(IDirect3DBaseTexture9 *iface) { return D9ResourceAddRef(&impl(iface)->base); }

static ULONG STDMETHODCALLTYPE texture_release(IDirect3DBaseTexture9 *iface)
{
    return D9ResourceRelease(&impl(iface)->base);
}

static HRESULT STDMETHODCALLTYPE texture_get_device(IDirect3DBaseTexture9 *iface, IDirect3DDevice9 **device)
{
    return D9ResourceGetDevice(&impl(iface)->base, device);
}

static HRESULT STDMETHODCALLTYPE texture_set_private_data(IDirect3DBaseTexture9 *iface, REFGUID guid,
                                                          const void *data, DWORD size, DWORD flags)
{
    return D9ResourceSetPrivateData(&impl(iface)->base, guid, data, size, flags);
}

static HRESULT STDMETHODCALLTYPE texture_get_private_data(IDirect3DBaseTexture9 *iface, REFGUID guid, void *data,
                                                          DWORD *size)
{
    return D9ResourceGetPrivateData(&impl(iface)->base, guid, data, size);
}

static HRESULT STDMETHODCALLTYPE texture_free_private_data(IDirect3DBaseTexture9 *iface, REFGUID guid)
{
    return D9ResourceFreePrivateData(&impl(iface)->base, guid);
}

static DWORD STDMETHODCALLTYPE texture_set_priority(IDirect3DBaseTexture9 *iface, DWORD priority)
{
    return D9ResourceSetPriority(&impl(iface)->base, priority);
}

static DWORD STDMETHODCALLTYPE texture_get_priority(IDirect3DBaseTexture9 *iface) { return impl(iface)->base.priority; }

static void STDMETHODCALLTYPE texture_pre_load(IDirect3DBaseTexture9 *iface) {}

static D3DRESOURCETYPE STDMETHODCALLTYPE texture_get_type(IDirect3DBaseTexture9 *iface) { return impl(iface)->base.type; }

/* only managed textures take a level of detail. */
static DWORD STDMETHODCALLTYPE texture_set_lod(IDirect3DBaseTexture9 *iface, DWORD lod)
{
    D9Texture *t = impl(iface);
    if (t->base.pool != D3DPOOL_MANAGED)
        return 0;
    D9Lock(t->base.device);
    DWORD old = t->lod;
    lod = D9_MIN(lod, t->levels - 1);
    if (lod != t->lod) {
        t->lod = lod;
        drop_views(t);
        t->base.device->textures_dirty = ~0u;
    }
    D9Unlock(t->base.device);
    return old;
}

static DWORD STDMETHODCALLTYPE texture_get_lod(IDirect3DBaseTexture9 *iface) { return impl(iface)->lod; }

static DWORD STDMETHODCALLTYPE texture_get_level_count(IDirect3DBaseTexture9 *iface) { return impl(iface)->levels; }

static HRESULT STDMETHODCALLTYPE texture_set_autogen_filter_type(IDirect3DBaseTexture9 *iface,
                                                                 D3DTEXTUREFILTERTYPE filter)
{
    D9Texture *t = impl(iface);
    if (filter == D3DTEXF_NONE || filter > D3DTEXF_ANISOTROPIC)
        return D3DERR_INVALIDCALL;
    t->autogen_filter = filter;
    return D3D_OK;
}

static D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE texture_get_autogen_filter_type(IDirect3DBaseTexture9 *iface)
{
    return impl(iface)->autogen_filter;
}

static void STDMETHODCALLTYPE texture_generate_mip_sub_levels(IDirect3DBaseTexture9 *iface)
{
    D9Texture *t = impl(iface);
    D9Lock(t->base.device);
    t->autogen_dirty = TRUE;
    D9TexturePrepare(t);
    D9Unlock(t->base.device);
}

/* ------------------------------------------------------------------ levels */

static D9Surface *level_surface(D9Texture *t, UINT face, UINT level)
{
    if (face >= t->faces || level >= t->levels || !t->surfaces)
        return nullptr;
    return t->surfaces[face * t->levels + level];
}

static HRESULT level_desc(D9Texture *t, UINT level, D3DSURFACE_DESC *desc)
{
    D9Surface *s = level_surface(t, 0, level);
    if (!s || !desc)
        return D3DERR_INVALIDCALL;
    return s->iface.lpVtbl->GetDesc(&s->iface, desc);
}

static HRESULT get_surface(D9Texture *t, UINT face, UINT level, IDirect3DSurface9 **out)
{
    D9Surface *s = level_surface(t, face, level);
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = s ? &s->iface : nullptr;
    if (!s)
        return D3DERR_INVALIDCALL;
    D9ResourceAddRef(&t->base);
    return D3D_OK;
}

static HRESULT lock_level(D9Texture *t, UINT face, UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
    D9Surface *s = level_surface(t, face, level);
    if (!s) {
        if (locked)
            locked->pBits = nullptr, locked->Pitch = 0;
        return D3DERR_INVALIDCALL;
    }
    D9Lock(t->base.device);
    HRESULT hr = D9SurfaceLock(s, locked, rect, flags);
    D9Unlock(t->base.device);
    return hr;
}

static HRESULT unlock_level(D9Texture *t, UINT face, UINT level)
{
    D9Surface *s = level_surface(t, face, level);
    if (!s)
        return D3DERR_INVALIDCALL;
    D9Lock(t->base.device);
    HRESULT hr = D9SurfaceUnlock(s);
    D9Unlock(t->base.device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE tex_get_level_desc(IDirect3DTexture9 *iface, UINT level, D3DSURFACE_DESC *desc)
{
    return level_desc(impl((IDirect3DBaseTexture9 *)iface), level, desc);
}

static HRESULT STDMETHODCALLTYPE tex_get_surface_level(IDirect3DTexture9 *iface, UINT level, IDirect3DSurface9 **out)
{
    return get_surface(impl((IDirect3DBaseTexture9 *)iface), 0, level, out);
}

static HRESULT STDMETHODCALLTYPE tex_lock_rect(IDirect3DTexture9 *iface, UINT level, D3DLOCKED_RECT *locked,
                                               const RECT *rect, DWORD flags)
{
    return lock_level(impl((IDirect3DBaseTexture9 *)iface), 0, level, locked, rect, flags);
}

static HRESULT STDMETHODCALLTYPE tex_unlock_rect(IDirect3DTexture9 *iface, UINT level)
{
    return unlock_level(impl((IDirect3DBaseTexture9 *)iface), 0, level);
}

static HRESULT STDMETHODCALLTYPE tex_add_dirty_rect(IDirect3DTexture9 *iface, const RECT *rect) { return D3D_OK; }

static HRESULT STDMETHODCALLTYPE cube_get_level_desc(IDirect3DCubeTexture9 *iface, UINT level,
                                                     D3DSURFACE_DESC *desc)
{
    return level_desc(impl((IDirect3DBaseTexture9 *)iface), level, desc);
}

static HRESULT STDMETHODCALLTYPE cube_get_cube_map_surface(IDirect3DCubeTexture9 *iface, D3DCUBEMAP_FACES face,
                                                           UINT level, IDirect3DSurface9 **out)
{
    return get_surface(impl((IDirect3DBaseTexture9 *)iface), face, level, out);
}

static HRESULT STDMETHODCALLTYPE cube_lock_rect(IDirect3DCubeTexture9 *iface, D3DCUBEMAP_FACES face, UINT level,
                                                D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
    return lock_level(impl((IDirect3DBaseTexture9 *)iface), face, level, locked, rect, flags);
}

static HRESULT STDMETHODCALLTYPE cube_unlock_rect(IDirect3DCubeTexture9 *iface, D3DCUBEMAP_FACES face, UINT level)
{
    return unlock_level(impl((IDirect3DBaseTexture9 *)iface), face, level);
}

static HRESULT STDMETHODCALLTYPE cube_add_dirty_rect(IDirect3DCubeTexture9 *iface, D3DCUBEMAP_FACES face,
                                                     const RECT *rect)
{
    return D3D_OK;
}

/* ------------------------------------------------------------------ volumes */

static D9Volume *volume_impl(IDirect3DVolume9 *iface) { return CONTAINING_RECORD(iface, D9Volume, iface); }

static HRESULT STDMETHODCALLTYPE volume_query(IDirect3DVolume9 *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DVolume9)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE volume_add_ref(IDirect3DVolume9 *iface)
{
    return D9ResourceAddRef(&volume_impl(iface)->container->base);
}

static ULONG STDMETHODCALLTYPE volume_release(IDirect3DVolume9 *iface)
{
    return D9ResourceRelease(&volume_impl(iface)->container->base);
}

static HRESULT STDMETHODCALLTYPE volume_get_device(IDirect3DVolume9 *iface, IDirect3DDevice9 **device)
{
    return D9ResourceGetDevice(&volume_impl(iface)->container->base, device);
}

static HRESULT STDMETHODCALLTYPE volume_set_private_data(IDirect3DVolume9 *iface, REFGUID guid, const void *data,
                                                         DWORD size, DWORD flags)
{
    D9Resource r = {};
    r.data = volume_impl(iface)->data;
    HRESULT hr = D9ResourceSetPrivateData(&r, guid, data, size, flags);
    volume_impl(iface)->data = r.data;
    return hr;
}

static HRESULT STDMETHODCALLTYPE volume_get_private_data(IDirect3DVolume9 *iface, REFGUID guid, void *data,
                                                         DWORD *size)
{
    D9Resource r = {};
    r.data = volume_impl(iface)->data;
    return D9ResourceGetPrivateData(&r, guid, data, size);
}

static HRESULT STDMETHODCALLTYPE volume_free_private_data(IDirect3DVolume9 *iface, REFGUID guid)
{
    D9Resource r = {};
    r.data = volume_impl(iface)->data;
    HRESULT hr = D9ResourceFreePrivateData(&r, guid);
    volume_impl(iface)->data = r.data;
    return hr;
}

static HRESULT STDMETHODCALLTYPE volume_get_container(IDirect3DVolume9 *iface, REFIID riid, void **out)
{
    D9Texture *t = volume_impl(iface)->container;
    if (!out)
        return D3DERR_INVALIDCALL;
    return t->iface.base.lpVtbl->QueryInterface(&t->iface.base, riid, out);
}

static HRESULT STDMETHODCALLTYPE volume_get_desc(IDirect3DVolume9 *iface, D3DVOLUME_DESC *desc)
{
    D9Volume *v = volume_impl(iface);
    D9Texture *t = v->container;
    if (!desc)
        return D3DERR_INVALIDCALL;
    *desc = (D3DVOLUME_DESC){t->base.d3d_format, D3DRTYPE_VOLUME, t->base.usage, t->base.pool, v->width, v->height,
                             v->depth};
    return D3D_OK;
}

static HRESULT lock_box(D9Volume *v, D3DLOCKED_BOX *locked, const D3DBOX *box, DWORD flags)
{
    D9Texture *t = v->container;
    const D9Format *f = t->base.format;
    D9Memory *m = &t->memory[v->level];
    if (!locked)
        return D3DERR_INVALIDCALL;
    locked->pBits = nullptr;
    locked->RowPitch = locked->SlicePitch = 0;
    D3DBOX b = box ? *box : (D3DBOX){0, 0, v->width, v->height, 0, v->depth};
    if (m->locked || b.Left >= b.Right || b.Top >= b.Bottom || b.Front >= b.Back || b.Right > v->width ||
        b.Bottom > v->height || b.Back > v->depth ||
        (t->resource && t->base.pool != D3DPOOL_MANAGED && !(t->base.usage & D3DUSAGE_DYNAMIC)))
        return D3DERR_INVALIDCALL;
    if (!m->bytes && FAILED(D9AllocMemory(m, f, v->width, v->height, v->depth)))
        return E_OUTOFMEMORY;
    m->locked = TRUE;
    m->lock_flags = flags;
    m->lock_rect = (RECT){(LONG)b.Left, (LONG)b.Top, (LONG)b.Right, (LONG)b.Bottom};
    m->lock_front = b.Front;
    m->lock_back = b.Back;
    locked->pBits = D9MemoryAt(m, f, b.Left, b.Top, b.Front);
    locked->RowPitch = m->pitch;
    locked->SlicePitch = m->slice;
    return D3D_OK;
}

/* uploads in gpu order, after the work queued before it. */
static void upload_box(D9Texture *t, UINT level, const RECT *r, UINT front, UINT back)
{
    const D9Format *f = t->base.format;
    D9Memory *m = &t->memory[level];
    D3D11_BOX box = {(UINT)r->left, (UINT)r->top, front, (UINT)r->right, (UINT)r->bottom, back};
    UINT width = r->right - r->left, rows = D9FormatRows(f, r->bottom - r->top);
    const BYTE *bytes = D9MemoryAt(m, f, r->left, r->top, front);
    UINT pitch = m->pitch, slice = m->slice;
    BYTE *converted = nullptr;
    if (f->stored != f->bytes) {
        UINT out_pitch = D9_ALIGN(width * f->stored, 4), slices = back - front;
        converted = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)out_pitch * rows * slices);
        if (!converted)
            return;
        for (UINT z = 0; z < slices; z++)
            D9ConvertUpload(f, bytes + (SIZE_T)z * slice, pitch, converted + (SIZE_T)z * out_pitch * rows, out_pitch,
                            width, rows);
        bytes = converted;
        pitch = out_pitch;
        slice = out_pitch * rows;
    }
    ID3D11DeviceContext1 *ctx = t->base.device->context;
    ctx->lpVtbl->UpdateSubresource(ctx, t->resource, level, &box, bytes, pitch, slice);
    HeapFree(GetProcessHeap(), 0, converted);
}

static HRESULT unlock_box(D9Volume *v)
{
    D9Texture *t = v->container;
    D9Memory *m = &t->memory[v->level];
    if (!m->locked)
        return D3DERR_INVALIDCALL;
    m->locked = FALSE;
    if (t->resource && !(m->lock_flags & D3DLOCK_READONLY))
        upload_box(t, v->level, &m->lock_rect, m->lock_front, m->lock_back);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE volume_lock_box(IDirect3DVolume9 *iface, D3DLOCKED_BOX *locked, const D3DBOX *box,
                                                 DWORD flags)
{
    D9Volume *v = volume_impl(iface);
    D9Lock(v->container->base.device);
    HRESULT hr = lock_box(v, locked, box, flags);
    D9Unlock(v->container->base.device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE volume_unlock_box(IDirect3DVolume9 *iface)
{
    D9Volume *v = volume_impl(iface);
    D9Lock(v->container->base.device);
    HRESULT hr = unlock_box(v);
    D9Unlock(v->container->base.device);
    return hr;
}

static const IDirect3DVolume9Vtbl volume_vtbl = {
    volume_query,          volume_add_ref,  volume_release,  volume_get_device,
    volume_set_private_data, volume_get_private_data, volume_free_private_data, volume_get_container,
    volume_get_desc,       volume_lock_box, volume_unlock_box,
};

static HRESULT STDMETHODCALLTYPE vol_get_level_desc(IDirect3DVolumeTexture9 *iface, UINT level, D3DVOLUME_DESC *desc)
{
    D9Texture *t = impl((IDirect3DBaseTexture9 *)iface);
    if (level >= t->levels)
        return D3DERR_INVALIDCALL;
    return volume_get_desc(&t->volumes[level]->iface, desc);
}

static HRESULT STDMETHODCALLTYPE vol_get_volume_level(IDirect3DVolumeTexture9 *iface, UINT level,
                                                      IDirect3DVolume9 **out)
{
    D9Texture *t = impl((IDirect3DBaseTexture9 *)iface);
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = nullptr;
    if (level >= t->levels)
        return D3DERR_INVALIDCALL;
    *out = &t->volumes[level]->iface;
    D9ResourceAddRef(&t->base);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE vol_lock_box(IDirect3DVolumeTexture9 *iface, UINT level, D3DLOCKED_BOX *locked,
                                              const D3DBOX *box, DWORD flags)
{
    D9Texture *t = impl((IDirect3DBaseTexture9 *)iface);
    if (level >= t->levels)
        return D3DERR_INVALIDCALL;
    return volume_lock_box(&t->volumes[level]->iface, locked, box, flags);
}

static HRESULT STDMETHODCALLTYPE vol_unlock_box(IDirect3DVolumeTexture9 *iface, UINT level)
{
    D9Texture *t = impl((IDirect3DBaseTexture9 *)iface);
    if (level >= t->levels)
        return D3DERR_INVALIDCALL;
    return volume_unlock_box(&t->volumes[level]->iface);
}

static HRESULT STDMETHODCALLTYPE vol_add_dirty_box(IDirect3DVolumeTexture9 *iface, const D3DBOX *box) { return D3D_OK; }

/* ------------------------------------------------------------------ vtables */

#define BASE_METHODS(T)                                                                                                \
    (void *)texture_query, (void *)texture_add_ref, (void *)texture_release, (void *)texture_get_device,              \
        (void *)texture_set_private_data, (void *)texture_get_private_data, (void *)texture_free_private_data,        \
        (void *)texture_set_priority, (void *)texture_get_priority, (void *)texture_pre_load,                         \
        (void *)texture_get_type, (void *)texture_set_lod, (void *)texture_get_lod, (void *)texture_get_level_count,  \
        (void *)texture_set_autogen_filter_type, (void *)texture_get_autogen_filter_type,                             \
        (void *)texture_generate_mip_sub_levels

static const IDirect3DTexture9Vtbl texture_vtbl = {
    BASE_METHODS(IDirect3DTexture9), tex_get_level_desc, tex_get_surface_level, tex_lock_rect, tex_unlock_rect,
    tex_add_dirty_rect,
};

static const IDirect3DCubeTexture9Vtbl cube_vtbl = {
    BASE_METHODS(IDirect3DCubeTexture9), cube_get_level_desc, cube_get_cube_map_surface, cube_lock_rect,
    cube_unlock_rect, cube_add_dirty_rect,
};

static const IDirect3DVolumeTexture9Vtbl volume_texture_vtbl = {
    BASE_METHODS(IDirect3DVolumeTexture9), vol_get_level_desc, vol_get_volume_level, vol_lock_box, vol_unlock_box,
    vol_add_dirty_box,
};

/* ------------------------------------------------------------------ creation */

static UINT full_chain(UINT width, UINT height, UINT depth)
{
    UINT levels = 1, size = D9_MAX(D9_MAX(width, height), depth);
    while (size > 1) {
        size >>= 1;
        levels++;
    }
    return levels;
}

static HRESULT make_resource(D9Texture *t)
{
    const D9Format *f = t->base.format;
    DWORD usage = t->base.usage;
    UINT bind = D3D11_BIND_SHADER_RESOURCE, misc = 0;
    if (usage & D3DUSAGE_RENDERTARGET)
        bind |= D3D11_BIND_RENDER_TARGET;
    if (usage & D3DUSAGE_DEPTHSTENCIL)
        bind |= D3D11_BIND_DEPTH_STENCIL;
    if (usage & D3DUSAGE_AUTOGENMIPMAP) {
        bind |= D3D11_BIND_RENDER_TARGET;
        misc |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
    }
    if (t->faces == 6)
        misc |= D3D11_RESOURCE_MISC_TEXTURECUBE;
    ID3D11Device1 *d3d11 = t->base.device->d3d11;
    if (t->base.type == D3DRTYPE_VOLUMETEXTURE) {
        D3D11_TEXTURE3D_DESC desc = {t->width, t->height, t->depth, t->d3d_levels, f->resource, D3D11_USAGE_DEFAULT,
                                     bind, 0, misc};
        return d3d11->lpVtbl->CreateTexture3D(d3d11, &desc, nullptr, (ID3D11Texture3D **)&t->resource);
    }
    D3D11_TEXTURE2D_DESC desc = {t->width, t->height, t->d3d_levels, t->faces, f->resource, {1, 0},
                                 D3D11_USAGE_DEFAULT, bind, 0, misc};
    return d3d11->lpVtbl->CreateTexture2D(d3d11, &desc, nullptr, (ID3D11Texture2D **)&t->resource);
}

static UINT mip(UINT size, UINT level) { return D9_MAX(size >> level, 1); }

HRESULT D9CreateTexture(D9Device *device, D3DRESOURCETYPE type, UINT width, UINT height, UINT depth, UINT levels,
                        DWORD usage, D3DFORMAT format, D3DPOOL pool, D9Texture **out)
{
    const D9Format *f = D9GetFormat(format);
    if (!width || !height || !depth || f->d3d == D3DFMT_UNKNOWN || (f->flags & D9_FORMAT_NULL))
        return D3DERR_INVALIDCALL;
    if ((usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) && pool != D3DPOOL_DEFAULT)
        return D3DERR_INVALIDCALL;
    if ((usage & D3DUSAGE_RENDERTARGET) && !(f->flags & D9_FORMAT_RENDER))
        return D3DERR_INVALIDCALL;
    if ((usage & D3DUSAGE_DEPTHSTENCIL) && !(f->flags & D9_FORMAT_DEPTH))
        return D3DERR_INVALIDCALL;
    if ((f->flags & D9_FORMAT_DEPTH) && (!(usage & D3DUSAGE_DEPTHSTENCIL) || type == D3DRTYPE_VOLUMETEXTURE))
        return D3DERR_INVALIDCALL;
    if ((usage & D3DUSAGE_DYNAMIC) && (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)))
        return D3DERR_INVALIDCALL;
    if ((usage & D3DUSAGE_DYNAMIC) && pool == D3DPOOL_MANAGED)
        return D3DERR_INVALIDCALL;
    if ((usage & D3DUSAGE_AUTOGENMIPMAP) &&
        (pool == D3DPOOL_SYSTEMMEM || levels > 1 || !(f->flags & D9_FORMAT_RENDER) || f->block > 1))
        return D3DERR_INVALIDCALL;
    UINT chain = full_chain(width, height, type == D3DRTYPE_VOLUMETEXTURE ? depth : 1);
    if (!levels)
        levels = chain;
    if (levels > chain)
        return D3DERR_INVALIDCALL;
    D9Texture *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    t->iface.base.lpVtbl = type == D3DRTYPE_CUBETEXTURE     ? (const void *)&cube_vtbl
                           : type == D3DRTYPE_VOLUMETEXTURE ? (const void *)&volume_texture_vtbl
                                                            : (const void *)&texture_vtbl;
    D9ResourceInit(&t->base, device, type, pool, usage, format, destroy_resource);
    t->width = width;
    t->height = height;
    t->depth = type == D3DRTYPE_VOLUMETEXTURE ? depth : 1;
    t->faces = type == D3DRTYPE_CUBETEXTURE ? 6 : 1;
    /* textures that make their own mips show programs only the first level. */
    t->d3d_levels = usage & D3DUSAGE_AUTOGENMIPMAP ? chain : levels;
    t->levels = usage & D3DUSAGE_AUTOGENMIPMAP ? 1 : levels;
    t->autogen_filter = D3DTEXF_LINEAR;
    HRESULT hr = D3D_OK;
    UINT subs = t->levels * t->faces;
    t->memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, subs * sizeof(*t->memory));
    if (!t->memory)
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && pool != D3DPOOL_SYSTEMMEM && pool != D3DPOOL_SCRATCH)
        hr = make_resource(t);
    if (SUCCEEDED(hr) && type == D3DRTYPE_VOLUMETEXTURE) {
        t->volumes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, t->levels * sizeof(*t->volumes));
        for (UINT l = 0; t->volumes && SUCCEEDED(hr) && l < t->levels; l++) {
            D9Volume *v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*v));
            if (!v) {
                hr = E_OUTOFMEMORY;
                break;
            }
            v->iface.lpVtbl = &volume_vtbl;
            v->container = t;
            v->level = l;
            v->width = mip(width, l), v->height = mip(height, l), v->depth = mip(depth, l);
            t->volumes[l] = v;
        }
        if (!t->volumes)
            hr = E_OUTOFMEMORY;
    } else if (SUCCEEDED(hr)) {
        t->surfaces = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, subs * sizeof(*t->surfaces));
        for (UINT face = 0; t->surfaces && SUCCEEDED(hr) && face < t->faces; face++)
            for (UINT l = 0; SUCCEEDED(hr) && l < t->levels; l++) {
                D9Surface *s;
                if (FAILED(hr = D9NewSurface(device, format, usage, pool, mip(width, l), mip(height, l), &s)))
                    break;
                s->container = t;
                s->texture = (ID3D11Texture2D *)t->resource;
                s->level = l;
                s->face = face;
                s->memory = &t->memory[face * t->levels + l];
                t->surfaces[face * t->levels + l] = s;
            }
        if (!t->surfaces)
            hr = E_OUTOFMEMORY;
    }
    /* managed textures keep a copy in memory, zeroed at first like the gpu's. */
    for (UINT i = 0; SUCCEEDED(hr) && i < subs && pool != D3DPOOL_DEFAULT; i++) {
        UINT l = i % t->levels;
        hr = D9AllocMemory(&t->memory[i], f, mip(width, l), mip(height, l), mip(t->depth, l));
    }
    if (FAILED(hr)) {
        destroy(t);
        device->iface.lpVtbl->Release(&device->iface);
        return hr == E_OUTOFMEMORY ? hr : D3DERR_OUTOFVIDEOMEMORY;
    }
    *out = t;
    return D3D_OK;
}

/* UpdateTexture: system memory levels to the matching levels of a gpu texture. */
HRESULT D9UpdateTexture(D9Device *device, D9Texture *s, D9Texture *t)
{
    if (!s || !t || s->base.pool != D3DPOOL_SYSTEMMEM || t->base.pool != D3DPOOL_DEFAULT ||
        s->base.type != t->base.type || s->base.d3d_format != t->base.d3d_format || !t->resource)
        return D3DERR_INVALIDCALL;
    /* the source may have more levels: skip to the one matching the destination's top size. */
    UINT skip = 0;
    while (skip < s->levels && mip(s->width, skip) > t->width)
        skip++;
    if (skip >= s->levels || mip(s->width, skip) != t->width || mip(s->height, skip) != t->height ||
        mip(s->depth, skip) != t->depth || s->levels - skip < t->levels)
        return D3DERR_INVALIDCALL;
    for (UINT face = 0; face < t->faces; face++)
        for (UINT l = 0; l < t->levels; l++) {
            D9Memory *m = &s->memory[face * s->levels + l + skip];
            UINT w = mip(t->width, l), h = mip(t->height, l), dd = mip(t->depth, l);
            RECT r = {0, 0, (LONG)w, (LONG)h};
            D9Memory *saved = &t->memory[face * t->levels + l];
            D9Memory keep = *saved;
            /* upload the destination level straight from the source's memory. */
            *saved = *m;
            if (t->base.format->block > 1) {
                r.right = D9_ALIGN(w, t->base.format->block);
                r.bottom = D9_ALIGN(h, t->base.format->block);
            }
            UINT sub = face * t->d3d_levels + l;
            if (t->base.type == D3DRTYPE_VOLUMETEXTURE)
                upload_box(t, l, &r, 0, dd);
            else
                D9UploadLevel(t, sub, face * t->levels + l, &r);
            *saved = keep;
        }
    t->autogen_dirty = TRUE;
    return D3D_OK;
}

/* index is the level's place in memory, sub its subresource. */
void D9UploadLevel(D9Texture *t, UINT sub, UINT index, const RECT *r)
{
    const D9Format *f = t->base.format;
    D9Memory *m = &t->memory[index];
    D3D11_BOX box = {(UINT)r->left, (UINT)r->top, 0, (UINT)r->right, (UINT)r->bottom, 1};
    UINT width = r->right - r->left, rows = D9FormatRows(f, r->bottom - r->top);
    const BYTE *bytes = D9MemoryAt(m, f, r->left, r->top, 0);
    UINT pitch = m->pitch;
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
    ID3D11DeviceContext1 *ctx = t->base.device->context;
    ctx->lpVtbl->UpdateSubresource(ctx, t->resource, sub, &box, bytes, pitch, 0);
    HeapFree(GetProcessHeap(), 0, converted);
}
