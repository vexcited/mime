/*
 * surfaces, volumes, textures, buffers and swap chains, each over its Direct3D 9 object. a texture makes its level
 * objects once, so asking for a level returns the same pointer every time, and levels count references with it.
 */
#include "d3d8_private.h"

#define DXGI_ERROR_NOT_FOUND ((HRESULT)0x887A0002)
#define DXGI_ERROR_MORE_DATA ((HRESULT)0x887A0003)

D8Object *D8ObjectFrom(void *iface) { return iface; }

static void destroy(D8Object *o);

ULONG D8ObjectAddRef(D8Object *o)
{
    if (o->container)
        return D8ObjectAddRef(o->container);
    ULONG refs = InterlockedIncrement(&o->refs);
    if (refs == 1)
        o->device->iface.lpVtbl->AddRef(&o->device->iface);
    return refs;
}

/* the last reference lets go of the device; the object itself stays while the device binds it. */
ULONG D8ObjectRelease(D8Object *o)
{
    if (o->container)
        return D8ObjectRelease(o->container);
    if (!o->refs)
        return 0;
    ULONG refs = InterlockedDecrement(&o->refs);
    if (!refs) {
        D8Device *d = o->device;
        if (!o->uses)
            destroy(o);
        d->iface.lpVtbl->Release(&d->iface);
    }
    return refs;
}

void D8ObjectUse(D8Object *o)
{
    if (o->container)
        o = o->container;
    InterlockedIncrement(&o->uses);
}

void D8ObjectUnuse(D8Object *o)
{
    if (o->container)
        o = o->container;
    if (!InterlockedDecrement(&o->uses) && !o->refs)
        destroy(o);
}

void D8Bind(D8Object **slot, D8Object *o)
{
    D8Object *old = *slot;
    if (old == o)
        return;
    if (o)
        D8ObjectUse(o);
    *slot = o;
    if (old)
        D8ObjectUnuse(old);
}

D8Object *D8Check(D8Device *d, void *iface, UINT kinds)
{
    D8Object *o = iface;
    if (!o || o->device != d || !(kinds & D8_KIND(o->kind)))
        return nullptr;
    return o;
}

static void free_object(D8Object *o)
{
    MimePrivateClear(&o->data);
    if (o->d9)
        o->d9->lpVtbl->Release(o->d9);
    HeapFree(GetProcessHeap(), 0, o);
}

static void destroy(D8Object *o)
{
    if (o->subs) {
        UINT count = o->kind == D8_CUBE_TEXTURE ? 6 * o->levels : o->levels;
        for (UINT i = 0; i < count; i++)
            if (o->subs[i])
                free_object(o->subs[i]);
        HeapFree(GetProcessHeap(), 0, o->subs);
    }
    if (o->kind == D8_SWAPCHAIN)
        D8SwapChainDrop(o);
    free_object(o);
}

static D8Object *new_object(D8Device *d, const void *vtbl, UINT kind, BOOL internal)
{
    D8Object *o = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*o));
    if (!o)
        return nullptr;
    o->vtbl = vtbl;
    o->kind = kind;
    o->device = d;
    if (internal) {
        o->uses = 1;
    } else {
        o->refs = 1;
        d->iface.lpVtbl->AddRef(&d->iface);
    }
    return o;
}

/* ------------------------------------------------------------------ shared methods */

static HRESULT query(D8Object *o, REFIID riid, void **out)
{
    *out = nullptr;
    BOOL ok = IsEqualGUID(riid, &IID_IUnknown);
    switch (o->kind) {
    case D8_SURFACE:
        ok |= IsEqualGUID(riid, &IID_IDirect3DSurface8);
        break;
    case D8_VOLUME:
        ok |= IsEqualGUID(riid, &IID_IDirect3DVolume8);
        break;
    case D8_SWAPCHAIN:
        ok |= IsEqualGUID(riid, &IID_IDirect3DSwapChain8);
        break;
    case D8_VERTEX_BUFFER:
        ok |= IsEqualGUID(riid, &IID_IDirect3DVertexBuffer8) || IsEqualGUID(riid, &IID_IDirect3DResource8);
        break;
    case D8_INDEX_BUFFER:
        ok |= IsEqualGUID(riid, &IID_IDirect3DIndexBuffer8) || IsEqualGUID(riid, &IID_IDirect3DResource8);
        break;
    default:
        ok |= IsEqualGUID(riid, &IID_IDirect3DResource8) || IsEqualGUID(riid, &IID_IDirect3DBaseTexture8) ||
              IsEqualGUID(riid, o->kind == D8_TEXTURE        ? &IID_IDirect3DTexture8
                                : o->kind == D8_CUBE_TEXTURE ? &IID_IDirect3DCubeTexture8
                                                             : &IID_IDirect3DVolumeTexture8);
        break;
    }
    if (!ok)
        return E_NOINTERFACE;
    *out = o;
    D8ObjectAddRef(o);
    return D3D_OK;
}

static HRESULT get_device(D8Object *o, IDirect3DDevice8 **out)
{
    IDirect3DDevice8 *d = &o->device->iface;
    return d->lpVtbl->QueryInterface(d, &IID_IDirect3DDevice8, (void **)out);
}

static HRESULT set_private(D8Object *o, REFGUID guid, const void *data, DWORD size, DWORD flags)
{
    if (flags & ~D3DSPD_IUNKNOWN)
        return D3DERR_INVALIDCALL;
    HRESULT hr;
    if (flags & D3DSPD_IUNKNOWN) {
        if (size != sizeof(IUnknown *))
            return D3DERR_INVALIDCALL;
        hr = MimePrivateSetInterface(&o->data, guid, data);
    } else {
        hr = MimePrivateSet(&o->data, guid, size, data);
    }
    return hr == E_OUTOFMEMORY ? hr : FAILED(hr) ? D3DERR_INVALIDCALL : D3D_OK;
}

static HRESULT get_private(D8Object *o, REFGUID guid, void *data, DWORD *size)
{
    UINT n = *size;
    HRESULT hr = MimePrivateGet(&o->data, guid, &n, data);
    if (hr == DXGI_ERROR_NOT_FOUND)
        return D3DERR_NOTFOUND;
    *size = n;
    if (hr == DXGI_ERROR_MORE_DATA)
        return D3DERR_MOREDATA;
    return FAILED(hr) ? D3DERR_INVALIDCALL : D3D_OK;
}

static HRESULT free_private(D8Object *o, REFGUID guid)
{
    UINT n = 0;
    if (MimePrivateGet(&o->data, guid, &n, nullptr) == DXGI_ERROR_NOT_FOUND)
        return D3DERR_NOTFOUND;
    MimePrivateSet(&o->data, guid, 0, nullptr);
    return D3D_OK;
}

static BOOL managed(const D8Object *o) { return o->pool == D3DPOOL_MANAGED; }

static IDirect3DResource9 *resource9(D8Object *o) { return (IDirect3DResource9 *)o->d9; }

static DWORD set_priority(D8Object *o, DWORD priority)
{
    if (!managed(o))
        return 0;
    DWORD old = o->priority;
    o->priority = priority;
    resource9(o)->lpVtbl->SetPriority(resource9(o), priority);
    return old;
}

static DWORD get_priority(D8Object *o) { return managed(o) ? o->priority : 0; }

static void preload(D8Object *o) { resource9(o)->lpVtbl->PreLoad(resource9(o)); }

static IDirect3DBaseTexture9 *texture9(D8Object *o) { return (IDirect3DBaseTexture9 *)o->d9; }

static DWORD set_lod(D8Object *o, DWORD lod)
{
    if (!managed(o))
        return 0;
    DWORD old = o->lod;
    o->lod = D8_MIN(lod, o->levels - 1);
    texture9(o)->lpVtbl->SetLOD(texture9(o), o->lod);
    return old;
}

/* written once per interface, since each vtable takes its own types. */
#define COMMON_METHODS(prefix, I)                                                                                      \
    static HRESULT STDMETHODCALLTYPE prefix##_query(I *iface, REFIID riid, void **out)                                 \
    {                                                                                                                  \
        return query(D8ObjectFrom(iface), riid, out);                                                                  \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_add_ref(I *iface) { return D8ObjectAddRef(D8ObjectFrom(iface)); }          \
    static ULONG STDMETHODCALLTYPE prefix##_release(I *iface) { return D8ObjectRelease(D8ObjectFrom(iface)); }         \
    static HRESULT STDMETHODCALLTYPE prefix##_get_device(I *iface, IDirect3DDevice8 **out)                             \
    {                                                                                                                  \
        return get_device(D8ObjectFrom(iface), out);                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_set_private(I *iface, REFGUID guid, const void *data, DWORD size,        \
                                                          DWORD flags)                                                 \
    {                                                                                                                  \
        return set_private(D8ObjectFrom(iface), guid, data, size, flags);                                              \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_get_private(I *iface, REFGUID guid, void *data, DWORD *size)             \
    {                                                                                                                  \
        return get_private(D8ObjectFrom(iface), guid, data, size);                                                     \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_free_private(I *iface, REFGUID guid)                                     \
    {                                                                                                                  \
        return free_private(D8ObjectFrom(iface), guid);                                                                \
    }

#define RESOURCE_METHODS(prefix, I)                                                                                    \
    COMMON_METHODS(prefix, I)                                                                                          \
    static DWORD STDMETHODCALLTYPE prefix##_set_priority(I *iface, DWORD priority)                                     \
    {                                                                                                                  \
        return set_priority(D8ObjectFrom(iface), priority);                                                            \
    }                                                                                                                  \
    static DWORD STDMETHODCALLTYPE prefix##_get_priority(I *iface) { return get_priority(D8ObjectFrom(iface)); }       \
    static void STDMETHODCALLTYPE prefix##_preload(I *iface) { preload(D8ObjectFrom(iface)); }                         \
    static D3DRESOURCETYPE STDMETHODCALLTYPE prefix##_get_type(I *iface) { return D8ObjectFrom(iface)->type; }

#define TEXTURE_METHODS(prefix, I)                                                                                     \
    RESOURCE_METHODS(prefix, I)                                                                                        \
    static DWORD STDMETHODCALLTYPE prefix##_set_lod(I *iface, DWORD lod) { return set_lod(D8ObjectFrom(iface), lod); } \
    static DWORD STDMETHODCALLTYPE prefix##_get_lod(I *iface)                                                          \
    {                                                                                                                  \
        D8Object *o = D8ObjectFrom(iface);                                                                             \
        return managed(o) ? o->lod : 0;                                                                                \
    }                                                                                                                  \
    static DWORD STDMETHODCALLTYPE prefix##_get_level_count(I *iface) { return D8ObjectFrom(iface)->levels; }

#define COMMON_ENTRIES(prefix)                                                                                         \
    prefix##_query, prefix##_add_ref, prefix##_release, prefix##_get_device, prefix##_set_private,                     \
        prefix##_get_private, prefix##_free_private
#define RESOURCE_ENTRIES(prefix)                                                                                       \
    COMMON_ENTRIES(prefix), prefix##_set_priority, prefix##_get_priority, prefix##_preload, prefix##_get_type
#define TEXTURE_ENTRIES(prefix) RESOURCE_ENTRIES(prefix), prefix##_set_lod, prefix##_get_lod, prefix##_get_level_count

/* ------------------------------------------------------------------ rectangles */

/* inside the level, on whole blocks for compressed formats. */
static BOOL valid_rect(const D8Object *o, const RECT *r, UINT width, UINT height)
{
    width = D8_MAX(width, 1);
    height = D8_MAX(height, 1);
    if (r->left >= r->right || r->top >= r->bottom || r->left < 0 || r->top < 0 || (UINT)r->right > width ||
        (UINT)r->bottom > height)
        return FALSE;
    if (D8IsDxt(o->format) && ((r->left & 3) || (r->top & 3) || ((r->right & 3) && (UINT)r->right != width) ||
                               ((r->bottom & 3) && (UINT)r->bottom != height)))
        return FALSE;
    return TRUE;
}

static BOOL valid_box(const D8Object *o, const D3DBOX *b, UINT width, UINT height, UINT depth)
{
    width = D8_MAX(width, 1);
    height = D8_MAX(height, 1);
    depth = D8_MAX(depth, 1);
    if (b->Left >= b->Right || b->Top >= b->Bottom || b->Front >= b->Back || b->Right > width || b->Bottom > height ||
        b->Back > depth)
        return FALSE;
    if (D8IsDxt(o->format) && ((b->Left & 3) || (b->Top & 3) || ((b->Right & 3) && b->Right != width) ||
                               ((b->Bottom & 3) && b->Bottom != height)))
        return FALSE;
    return TRUE;
}

#define PLAIN_LOCKS (D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK | D3DLOCK_NO_DIRTY_UPDATE)

/* other flags lock only the first level of a dynamic texture whole, to discard it. */
static BOOL valid_flags(const D8Object *o, DWORD flags, BOOL whole)
{
    if (!(flags & ~PLAIN_LOCKS))
        return TRUE;
    D8Object *t = o->container;
    return (flags & D3DLOCK_DISCARD) && !(flags & D3DLOCK_READONLY) && t && (t->usage & D3DUSAGE_DYNAMIC) &&
           !o->level && !o->face && whole;
}

/* ------------------------------------------------------------------ surfaces */

static IDirect3DSurface9 *surface9(D8Object *o) { return (IDirect3DSurface9 *)o->d9; }

HRESULT D8SurfaceDesc(D8Object *s, D3D8_SURFACE_DESC *desc)
{
    desc->Format = s->format;
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = s->usage & 0x3FB;
    desc->Pool = s->pool;
    desc->Size = D8LevelSize(s->format, s->width, s->height, 1) * D8_MAX(s->samples, 1);
    desc->MultiSampleType = s->samples;
    desc->Width = s->width;
    desc->Height = s->height;
    return D3D_OK;
}

COMMON_METHODS(surface, IDirect3DSurface8)

static HRESULT STDMETHODCALLTYPE surface_get_container(IDirect3DSurface8 *iface, REFIID riid, void **out)
{
    D8Object *s = D8ObjectFrom(iface);
    if (s->container)
        return query(s->container, riid, out);
    IDirect3DDevice8 *d = &s->device->iface;
    return d->lpVtbl->QueryInterface(d, riid, out);
}

static HRESULT STDMETHODCALLTYPE surface_get_desc(IDirect3DSurface8 *iface, D3D8_SURFACE_DESC *desc)
{
    return D8SurfaceDesc(D8ObjectFrom(iface), desc);
}

static HRESULT lock_surface(D8Object *s, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
    if (s->lenient) {
        if (!s->lockable || s->locks)
            return D3DERR_INVALIDCALL;
    } else {
        memset(locked, 0, sizeof(*locked));
        if ((rect && !valid_rect(s, rect, s->width, s->height)) || !valid_flags(s, flags, !rect) || !s->lockable ||
            s->locks)
            return D3DERR_INVALIDCALL;
    }
    HRESULT hr = surface9(s)->lpVtbl->LockRect(surface9(s), locked, rect, flags & (PLAIN_LOCKS | D3DLOCK_DISCARD));
    if (FAILED(hr))
        return hr == E_OUTOFMEMORY ? hr : D3DERR_INVALIDCALL;
    s->locks = 1;
    return D3D_OK;
}

/* a texture unlocks its levels in Direct3D 9 even when not locked, as windows passes it to its driver. */
static HRESULT unlock_surface(D8Object *s, BOOL checked)
{
    if (!s->locks)
        return s->lenient ? D3D_OK : checked ? D3DERR_INVALIDCALL : surface9(s)->lpVtbl->UnlockRect(surface9(s));
    s->locks = 0;
    return surface9(s)->lpVtbl->UnlockRect(surface9(s));
}

static HRESULT STDMETHODCALLTYPE surface_lock_rect(IDirect3DSurface8 *iface, D3DLOCKED_RECT *locked,
                                                   const RECT *rect, DWORD flags)
{
    return lock_surface(D8ObjectFrom(iface), locked, rect, flags);
}

static HRESULT STDMETHODCALLTYPE surface_unlock_rect(IDirect3DSurface8 *iface)
{
    return unlock_surface(D8ObjectFrom(iface), TRUE);
}

static const IDirect3DSurface8Vtbl surface_vtbl = {
    COMMON_ENTRIES(surface), surface_get_container, surface_get_desc, surface_lock_rect, surface_unlock_rect,
};

HRESULT D8WrapSurface(D8Device *d, IDirect3DSurface9 *s9, DWORD usage, BOOL internal, D8Object **out)
{
    D3DSURFACE_DESC desc;
    s9->lpVtbl->GetDesc(s9, &desc);
    D8Object *s = new_object(d, &surface_vtbl, D8_SURFACE, internal);
    if (!s) {
        s9->lpVtbl->Release(s9);
        return E_OUTOFMEMORY;
    }
    s->d9 = (IUnknown *)s9;
    s->type = D3DRTYPE_SURFACE;
    s->format = desc.Format;
    s->usage = usage;
    s->pool = desc.Pool;
    s->width = desc.Width;
    s->height = desc.Height;
    s->samples = desc.MultiSampleType;
    *out = s;
    return D3D_OK;
}

/* ------------------------------------------------------------------ volumes */

static IDirect3DVolume9 *volume9(D8Object *o) { return (IDirect3DVolume9 *)o->d9; }

static void volume_desc(D8Object *v, D3D8_VOLUME_DESC *desc)
{
    desc->Format = v->format;
    desc->Type = D3DRTYPE_VOLUME;
    desc->Usage = v->usage & 0x3FB;
    desc->Pool = v->pool;
    desc->Size = D8LevelSize(v->format, v->width, v->height, v->depth);
    desc->Width = v->width;
    desc->Height = v->height;
    desc->Depth = v->depth;
}

COMMON_METHODS(volume, IDirect3DVolume8)

static HRESULT STDMETHODCALLTYPE volume_get_container(IDirect3DVolume8 *iface, REFIID riid, void **out)
{
    return query(D8ObjectFrom(iface)->container, riid, out);
}

static HRESULT STDMETHODCALLTYPE volume_get_desc(IDirect3DVolume8 *iface, D3D8_VOLUME_DESC *desc)
{
    volume_desc(D8ObjectFrom(iface), desc);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE volume_lock_box(IDirect3DVolume8 *iface, D3DLOCKED_BOX *locked, const D3DBOX *box,
                                                 DWORD flags)
{
    D8Object *v = D8ObjectFrom(iface);
    memset(locked, 0, sizeof(*locked));
    if ((box && !valid_box(v, box, v->width, v->height, v->depth)) || !valid_flags(v, flags, !box) || !v->lockable ||
        v->locks)
        return D3DERR_INVALIDCALL;
    HRESULT hr = volume9(v)->lpVtbl->LockBox(volume9(v), locked, box, flags & (PLAIN_LOCKS | D3DLOCK_DISCARD));
    if (FAILED(hr))
        return hr == E_OUTOFMEMORY ? hr : D3DERR_INVALIDCALL;
    v->locks = 1;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE volume_unlock_box(IDirect3DVolume8 *iface)
{
    D8Object *v = D8ObjectFrom(iface);
    if (!v->locks)
        return D3DERR_INVALIDCALL;
    v->locks = 0;
    return volume9(v)->lpVtbl->UnlockBox(volume9(v));
}

static const IDirect3DVolume8Vtbl volume_vtbl = {
    COMMON_ENTRIES(volume), volume_get_container, volume_get_desc, volume_lock_box, volume_unlock_box,
};

/* ------------------------------------------------------------------ textures */

TEXTURE_METHODS(texture, IDirect3DTexture8)

static HRESULT STDMETHODCALLTYPE texture_get_level_desc(IDirect3DTexture8 *iface, UINT level, D3D8_SURFACE_DESC *desc)
{
    D8Object *t = D8ObjectFrom(iface);
    if (level >= t->levels)
        return D3DERR_INVALIDCALL;
    return D8SurfaceDesc(t->subs[level], desc);
}

static HRESULT STDMETHODCALLTYPE texture_get_surface_level(IDirect3DTexture8 *iface, UINT level,
                                                           IDirect3DSurface8 **out)
{
    D8Object *t = D8ObjectFrom(iface);
    if (level >= t->levels) {
        *out = nullptr;
        return D3DERR_INVALIDCALL;
    }
    *out = D8_IFACE(t->subs[level]);
    D8ObjectAddRef(t);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE texture_lock_rect(IDirect3DTexture8 *iface, UINT level, D3DLOCKED_RECT *locked,
                                                   const RECT *rect, DWORD flags)
{
    D8Object *t = D8ObjectFrom(iface);
    if (level >= t->levels)
        return D3DERR_INVALIDCALL;
    return lock_surface(t->subs[level], locked, rect, flags);
}

static HRESULT STDMETHODCALLTYPE texture_unlock_rect(IDirect3DTexture8 *iface, UINT level)
{
    D8Object *t = D8ObjectFrom(iface);
    if (level >= t->levels)
        return D3DERR_INVALIDCALL;
    return unlock_surface(t->subs[level], FALSE);
}

static HRESULT STDMETHODCALLTYPE texture_add_dirty_rect(IDirect3DTexture8 *iface, const RECT *rect)
{
    D8Object *t = D8ObjectFrom(iface);
    if (rect && !valid_rect(t, rect, t->width, t->height))
        return D3DERR_INVALIDCALL;
    IDirect3DTexture9 *t9 = (IDirect3DTexture9 *)t->d9;
    t9->lpVtbl->AddDirtyRect(t9, rect);
    return D3D_OK;
}

static const IDirect3DTexture8Vtbl texture_vtbl = {
    TEXTURE_ENTRIES(texture), texture_get_level_desc, texture_get_surface_level,
    texture_lock_rect,        texture_unlock_rect,    texture_add_dirty_rect,
};

TEXTURE_METHODS(cube, IDirect3DCubeTexture8)

static HRESULT STDMETHODCALLTYPE cube_get_level_desc(IDirect3DCubeTexture8 *iface, UINT level,
                                                     D3D8_SURFACE_DESC *desc)
{
    D8Object *t = D8ObjectFrom(iface);
    if (level >= t->levels)
        return D3DERR_INVALIDCALL;
    return D8SurfaceDesc(t->subs[level], desc);
}

static HRESULT STDMETHODCALLTYPE cube_get_cube_map_surface(IDirect3DCubeTexture8 *iface, D3DCUBEMAP_FACES face,
                                                           UINT level, IDirect3DSurface8 **out)
{
    D8Object *t = D8ObjectFrom(iface);
    *out = nullptr;
    if (level >= t->levels || (UINT)face >= 6)
        return D3DERR_INVALIDCALL;
    *out = D8_IFACE(t->subs[face * t->levels + level]);
    D8ObjectAddRef(t);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE cube_lock_rect(IDirect3DCubeTexture8 *iface, D3DCUBEMAP_FACES face, UINT level,
                                                D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
    D8Object *t = D8ObjectFrom(iface);
    if (!locked || (UINT)face >= 6 || level >= t->levels)
        return D3DERR_INVALIDCALL;
    return lock_surface(t->subs[face * t->levels + level], locked, rect, flags);
}

static HRESULT STDMETHODCALLTYPE cube_unlock_rect(IDirect3DCubeTexture8 *iface, D3DCUBEMAP_FACES face, UINT level)
{
    D8Object *t = D8ObjectFrom(iface);
    if ((UINT)face >= 6 || level >= t->levels)
        return D3DERR_INVALIDCALL;
    return unlock_surface(t->subs[face * t->levels + level], TRUE);
}

static HRESULT STDMETHODCALLTYPE cube_add_dirty_rect(IDirect3DCubeTexture8 *iface, D3DCUBEMAP_FACES face,
                                                     const RECT *rect)
{
    D8Object *t = D8ObjectFrom(iface);
    if ((UINT)face >= 6 || (rect && !valid_rect(t, rect, t->width, t->width)))
        return D3DERR_INVALIDCALL;
    IDirect3DCubeTexture9 *t9 = (IDirect3DCubeTexture9 *)t->d9;
    t9->lpVtbl->AddDirtyRect(t9, face, rect);
    return D3D_OK;
}

static const IDirect3DCubeTexture8Vtbl cube_vtbl = {
    TEXTURE_ENTRIES(cube), cube_get_level_desc, cube_get_cube_map_surface,
    cube_lock_rect,        cube_unlock_rect,    cube_add_dirty_rect,
};

TEXTURE_METHODS(volume_texture, IDirect3DVolumeTexture8)

static HRESULT STDMETHODCALLTYPE volume_texture_get_level_desc(IDirect3DVolumeTexture8 *iface, UINT level,
                                                               D3D8_VOLUME_DESC *desc)
{
    D8Object *t = D8ObjectFrom(iface);
    if (level >= t->levels)
        return D3DERR_INVALIDCALL;
    volume_desc(t->subs[level], desc);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE volume_texture_get_volume_level(IDirect3DVolumeTexture8 *iface, UINT level,
                                                                 IDirect3DVolume8 **out)
{
    D8Object *t = D8ObjectFrom(iface);
    if (level >= t->levels) {
        *out = nullptr;
        return D3DERR_INVALIDCALL;
    }
    *out = D8_IFACE(t->subs[level]);
    D8ObjectAddRef(t);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE volume_texture_lock_box(IDirect3DVolumeTexture8 *iface, UINT level,
                                                         D3DLOCKED_BOX *locked, const D3DBOX *box, DWORD flags)
{
    D8Object *t = D8ObjectFrom(iface);
    if (level >= t->levels)
        return D3DERR_INVALIDCALL;
    return volume_lock_box(D8_IFACE(t->subs[level]), locked, box, flags);
}

static HRESULT STDMETHODCALLTYPE volume_texture_unlock_box(IDirect3DVolumeTexture8 *iface, UINT level)
{
    D8Object *t = D8ObjectFrom(iface);
    if (level >= t->levels)
        return D3DERR_INVALIDCALL;
    return volume_unlock_box(D8_IFACE(t->subs[level]));
}

static HRESULT STDMETHODCALLTYPE volume_texture_add_dirty_box(IDirect3DVolumeTexture8 *iface, const D3DBOX *box)
{
    D8Object *t = D8ObjectFrom(iface);
    if (box && !valid_box(t, box, t->width, t->height, t->depth))
        return D3DERR_INVALIDCALL;
    IDirect3DVolumeTexture9 *t9 = (IDirect3DVolumeTexture9 *)t->d9;
    t9->lpVtbl->AddDirtyBox(t9, box);
    return D3D_OK;
}

static const IDirect3DVolumeTexture8Vtbl volume_texture_vtbl = {
    TEXTURE_ENTRIES(volume_texture), volume_texture_get_level_desc, volume_texture_get_volume_level,
    volume_texture_lock_box,         volume_texture_unlock_box,     volume_texture_add_dirty_box,
};

/* a level gets its own object over Direct3D 9's, counted with the texture. */
static D8Object *new_level(D8Object *t, const void *vtbl, UINT kind, IUnknown *l9, UINT level, UINT face)
{
    D8Object *l = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*l));
    if (!l) {
        l9->lpVtbl->Release(l9);
        return nullptr;
    }
    l->vtbl = vtbl;
    l->kind = kind;
    l->device = t->device;
    l->container = t;
    l->d9 = l9;
    l->type = kind == D8_VOLUME ? D3DRTYPE_VOLUME : D3DRTYPE_SURFACE;
    l->format = t->format;
    l->usage = t->usage;
    l->pool = t->pool;
    l->lockable = t->lockable;
    l->lenient = t->kind == D8_TEXTURE && t->pool != D3DPOOL_DEFAULT;
    l->level = level;
    l->face = face;
    l->width = D8_MAX(1, t->width >> level);
    l->height = D8_MAX(1, t->height >> level);
    l->depth = D8_MAX(1, t->depth >> level);
    return l;
}

/* programs may lock textures in memory, dynamic ones, and those in formats the runtime has no size for. */
static BOOL texture_lockable(D3DPOOL pool, DWORD usage, D3DFORMAT format)
{
    if (pool != D3DPOOL_DEFAULT)
        return !D8IsDepthFormat(format) || format == D3DFMT_D16_LOCKABLE;
    return (usage & D3DUSAGE_DYNAMIC) || !D8FormatBytes(format);
}

HRESULT D8WrapTexture(D8Device *d, UINT kind, IUnknown *t9, DWORD usage, D3DPOOL pool, D3DFORMAT format,
                      D8Object **out)
{
    static const void *const vtbls[] = {[D8_TEXTURE] = &texture_vtbl,
                                        [D8_CUBE_TEXTURE] = &cube_vtbl,
                                        [D8_VOLUME_TEXTURE] = &volume_texture_vtbl};
    D8Object *t = new_object(d, vtbls[kind], kind, FALSE);
    if (!t) {
        t9->lpVtbl->Release(t9);
        return E_OUTOFMEMORY;
    }
    IDirect3DBaseTexture9 *b9 = (IDirect3DBaseTexture9 *)t9;
    t->d9 = t9;
    t->type = kind == D8_TEXTURE ? D3DRTYPE_TEXTURE : kind == D8_CUBE_TEXTURE ? D3DRTYPE_CUBETEXTURE
                                                                               : D3DRTYPE_VOLUMETEXTURE;
    t->format = format;
    t->usage = usage;
    t->pool = pool;
    t->lockable = texture_lockable(pool, usage, format);
    t->levels = b9->lpVtbl->GetLevelCount(b9);
    UINT count = kind == D8_CUBE_TEXTURE ? 6 * t->levels : t->levels;
    t->subs = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*t->subs));
    HRESULT hr = t->subs ? D3D_OK : E_OUTOFMEMORY;
    if (kind == D8_TEXTURE) {
        IDirect3DTexture9 *x = (IDirect3DTexture9 *)t9;
        D3DSURFACE_DESC desc;
        x->lpVtbl->GetLevelDesc(x, 0, &desc);
        t->width = desc.Width;
        t->height = desc.Height;
        t->depth = 1;
        for (UINT i = 0; SUCCEEDED(hr) && i < t->levels; i++) {
            IDirect3DSurface9 *s9;
            hr = x->lpVtbl->GetSurfaceLevel(x, i, &s9);
            if (SUCCEEDED(hr) && !(t->subs[i] = new_level(t, &surface_vtbl, D8_SURFACE, (IUnknown *)s9, i, 0)))
                hr = E_OUTOFMEMORY;
        }
    } else if (kind == D8_CUBE_TEXTURE) {
        IDirect3DCubeTexture9 *x = (IDirect3DCubeTexture9 *)t9;
        D3DSURFACE_DESC desc;
        x->lpVtbl->GetLevelDesc(x, 0, &desc);
        t->width = t->height = desc.Width;
        t->depth = 1;
        for (UINT f = 0; f < 6; f++)
            for (UINT i = 0; SUCCEEDED(hr) && i < t->levels; i++) {
                IDirect3DSurface9 *s9;
                hr = x->lpVtbl->GetCubeMapSurface(x, f, i, &s9);
                D8Object **slot = &t->subs[f * t->levels + i];
                if (SUCCEEDED(hr) && !(*slot = new_level(t, &surface_vtbl, D8_SURFACE, (IUnknown *)s9, i, f)))
                    hr = E_OUTOFMEMORY;
            }
    } else {
        IDirect3DVolumeTexture9 *x = (IDirect3DVolumeTexture9 *)t9;
        D3DVOLUME_DESC desc;
        x->lpVtbl->GetLevelDesc(x, 0, &desc);
        t->width = desc.Width;
        t->height = desc.Height;
        t->depth = desc.Depth;
        for (UINT i = 0; SUCCEEDED(hr) && i < t->levels; i++) {
            IDirect3DVolume9 *v9;
            hr = x->lpVtbl->GetVolumeLevel(x, i, &v9);
            if (SUCCEEDED(hr) && !(t->subs[i] = new_level(t, &volume_vtbl, D8_VOLUME, (IUnknown *)v9, i, 0)))
                hr = E_OUTOFMEMORY;
        }
    }
    if (FAILED(hr)) {
        D8ObjectRelease(t);
        return hr;
    }
    *out = t;
    return D3D_OK;
}

/* ------------------------------------------------------------------ buffers */

static IDirect3DVertexBuffer9 *buffer9(D8Object *o) { return (IDirect3DVertexBuffer9 *)o->d9; }

/* windows checks nothing on lock: the range is kept inside the buffer and the pointer is where asked. */
static HRESULT lock_buffer(D8Object *b, UINT offset, UINT size, BYTE **data, DWORD flags)
{
    UINT at = D8_MIN(offset, b->size);
    UINT length = size ? D8_MIN(size, b->size - at) : 0;
    if (size && !length)
        at = 0;
    if (offset)
        flags &= ~D3DLOCK_DISCARD;
    void *bytes;
    HRESULT hr = buffer9(b)->lpVtbl->Lock(buffer9(b), at, length, &bytes,
                                          flags & (D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK | D3DLOCK_DISCARD |
                                                   D3DLOCK_NOOVERWRITE));
    if (FAILED(hr)) {
        *data = nullptr;
        return hr;
    }
    *data = (BYTE *)bytes + (offset - at);
    InterlockedIncrement(&b->locks);
    return D3D_OK;
}

/* unlocking what is not locked succeeds, which programs count on. */
static HRESULT unlock_buffer(D8Object *b)
{
    if (b->locks <= 0)
        return D3D_OK;
    HRESULT hr = buffer9(b)->lpVtbl->Unlock(buffer9(b));
    if (SUCCEEDED(hr))
        InterlockedDecrement(&b->locks);
    return hr;
}

RESOURCE_METHODS(vb, IDirect3DVertexBuffer8)

static HRESULT STDMETHODCALLTYPE vb_lock(IDirect3DVertexBuffer8 *iface, UINT offset, UINT size, BYTE **data,
                                         DWORD flags)
{
    return lock_buffer(D8ObjectFrom(iface), offset, size, data, flags);
}

static HRESULT STDMETHODCALLTYPE vb_unlock(IDirect3DVertexBuffer8 *iface) { return unlock_buffer(D8ObjectFrom(iface)); }

static HRESULT STDMETHODCALLTYPE vb_get_desc(IDirect3DVertexBuffer8 *iface, D3DVERTEXBUFFER_DESC *desc)
{
    D8Object *b = D8ObjectFrom(iface);
    *desc = (D3DVERTEXBUFFER_DESC){D3DFMT_VERTEXDATA, D3DRTYPE_VERTEXBUFFER, b->usage, b->pool, b->size, b->fvf};
    return D3D_OK;
}

static const IDirect3DVertexBuffer8Vtbl vb_vtbl = {
    RESOURCE_ENTRIES(vb),
    vb_lock,
    vb_unlock,
    vb_get_desc,
};

RESOURCE_METHODS(ib, IDirect3DIndexBuffer8)

static HRESULT STDMETHODCALLTYPE ib_lock(IDirect3DIndexBuffer8 *iface, UINT offset, UINT size, BYTE **data,
                                         DWORD flags)
{
    return lock_buffer(D8ObjectFrom(iface), offset, size, data, flags);
}

static HRESULT STDMETHODCALLTYPE ib_unlock(IDirect3DIndexBuffer8 *iface) { return unlock_buffer(D8ObjectFrom(iface)); }

static HRESULT STDMETHODCALLTYPE ib_get_desc(IDirect3DIndexBuffer8 *iface, D3DINDEXBUFFER_DESC *desc)
{
    D8Object *b = D8ObjectFrom(iface);
    *desc = (D3DINDEXBUFFER_DESC){b->format, D3DRTYPE_INDEXBUFFER, b->usage, b->pool, b->size};
    return D3D_OK;
}

static const IDirect3DIndexBuffer8Vtbl ib_vtbl = {
    RESOURCE_ENTRIES(ib),
    ib_lock,
    ib_unlock,
    ib_get_desc,
};

HRESULT D8WrapBuffer(D8Device *d, UINT kind, IUnknown *b9, UINT size, DWORD usage, DWORD fvf, D3DPOOL pool,
                     D3DFORMAT format, D8Object **out)
{
    D8Object *b = new_object(d, kind == D8_VERTEX_BUFFER ? (const void *)&vb_vtbl : &ib_vtbl, kind, FALSE);
    if (!b) {
        b9->lpVtbl->Release(b9);
        return E_OUTOFMEMORY;
    }
    b->d9 = b9;
    b->type = kind == D8_VERTEX_BUFFER ? D3DRTYPE_VERTEXBUFFER : D3DRTYPE_INDEXBUFFER;
    b->size = size;
    b->usage = usage;
    b->fvf = fvf;
    b->pool = pool;
    b->format = format;
    *out = b;
    return D3D_OK;
}

/* ------------------------------------------------------------------ swap chains */

static IDirect3DSwapChain9 *swapchain9(D8Object *o) { return (IDirect3DSwapChain9 *)o->d9; }

HRESULT D8SwapChainPresent(D8Object *sc, const RECT *source, const RECT *dest, HWND window, const RGNDATA *dirty)
{
    if (dirty || (sc->flip && (source || dest)))
        return D3DERR_INVALIDCALL;
    for (UINT i = 0; i < sc->buffer_count; i++)
        if (sc->buffers[i]->locks)
            return D3DERR_INVALIDCALL;
    return swapchain9(sc)->lpVtbl->Present(swapchain9(sc), source, dest, window, nullptr, 0);
}

HRESULT D8SwapChainBackBuffer(D8Object *sc, UINT index, IDirect3DSurface8 **out)
{
    if (!out || index >= sc->buffer_count)
        return D3DERR_INVALIDCALL;
    *out = D8_IFACE(sc->buffers[index]);
    D8ObjectAddRef(sc->buffers[index]);
    return D3D_OK;
}

void D8SwapChainDrop(D8Object *sc)
{
    for (UINT i = 0; i < sc->buffer_count; i++)
        D8ObjectUnuse(sc->buffers[i]);
    HeapFree(GetProcessHeap(), 0, sc->buffers);
    sc->buffers = nullptr;
    sc->buffer_count = 0;
    if (sc->d9)
        sc->d9->lpVtbl->Release(sc->d9);
    sc->d9 = nullptr;
}

/* takes the back buffers of the Direct3D 9 swap chain after its creation or a reset. */
HRESULT D8SwapChainTake(D8Object *sc, const D3DPRESENT_PARAMETERS *params)
{
    IDirect3DSwapChain9 *sc9 = swapchain9(sc);
    UINT count = D8_MAX(params->BackBufferCount, 1);
    sc->flip = params->SwapEffect == D3DSWAPEFFECT_FLIP ||
               (params->SwapEffect == D3DSWAPEFFECT_DISCARD && !(params->Windowed && count == 1));
    sc->buffers = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*sc->buffers));
    if (!sc->buffers)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < count; i++) {
        IDirect3DSurface9 *s9;
        HRESULT hr = sc9->lpVtbl->GetBackBuffer(sc9, i, D3DBACKBUFFER_TYPE_MONO, &s9);
        if (SUCCEEDED(hr))
            hr = D8WrapSurface(sc->device, s9, D3DUSAGE_RENDERTARGET, TRUE, &sc->buffers[i]);
        if (FAILED(hr))
            return hr;
        sc->buffers[i]->lockable =
            (params->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) && params->MultiSampleType == D3DMULTISAMPLE_NONE;
        sc->buffer_count++;
    }
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE swapchain_query(IDirect3DSwapChain8 *iface, REFIID riid, void **out)
{
    return query(D8ObjectFrom(iface), riid, out);
}

static ULONG STDMETHODCALLTYPE swapchain_add_ref(IDirect3DSwapChain8 *iface)
{
    return D8ObjectAddRef(D8ObjectFrom(iface));
}

static ULONG STDMETHODCALLTYPE swapchain_release(IDirect3DSwapChain8 *iface)
{
    return D8ObjectRelease(D8ObjectFrom(iface));
}

static HRESULT STDMETHODCALLTYPE swapchain_present(IDirect3DSwapChain8 *iface, const RECT *source, const RECT *dest,
                                                   HWND window, const RGNDATA *dirty)
{
    return D8SwapChainPresent(D8ObjectFrom(iface), source, dest, window, dirty);
}

static HRESULT STDMETHODCALLTYPE swapchain_get_back_buffer(IDirect3DSwapChain8 *iface, UINT index,
                                                           D3DBACKBUFFER_TYPE type, IDirect3DSurface8 **out)
{
    return D8SwapChainBackBuffer(D8ObjectFrom(iface), index, out);
}

static const IDirect3DSwapChain8Vtbl swapchain_vtbl = {
    swapchain_query, swapchain_add_ref, swapchain_release, swapchain_present, swapchain_get_back_buffer,
};

HRESULT D8WrapSwapChain(D8Device *d, IDirect3DSwapChain9 *sc9, const D3DPRESENT_PARAMETERS *params, BOOL internal,
                        D8Object **out)
{
    D8Object *sc = new_object(d, &swapchain_vtbl, D8_SWAPCHAIN, internal);
    if (!sc) {
        sc9->lpVtbl->Release(sc9);
        return E_OUTOFMEMORY;
    }
    sc->d9 = (IUnknown *)sc9;
    HRESULT hr = D8SwapChainTake(sc, params);
    if (FAILED(hr)) {
        if (internal)
            D8ObjectUnuse(sc);
        else
            D8ObjectRelease(sc);
        return hr;
    }
    *out = sc;
    return D3D_OK;
}
