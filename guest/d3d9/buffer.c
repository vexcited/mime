/*
 * IDirect3DVertexBuffer9 and IDirect3DIndexBuffer9. dynamic buffers are
 * d3d11 dynamic buffers: D3DLOCK_DISCARD maps them anew, which renames
 * them rather than waiting, and D3DLOCK_NOOVERWRITE maps what the gpu may
 * still read. other buffers keep their bytes in memory, which locks hand
 * out and unlocking uploads in the order of the gpu's work, so a lock
 * never waits either.
 */
#include "d3d9_private.h"

static D9Buffer *impl(void *iface) { return CONTAINING_RECORD((IDirect3DVertexBuffer9 *)iface, D9Buffer, iface.vb); }

D9Buffer *D9BufferFrom(void *iface) { return iface ? impl(iface) : NULL; }

static void destroy(D9Buffer *b)
{
    if (b->buffer)
        b->buffer->lpVtbl->Release(b->buffer);
    if (b->memory)
        VirtualFree(b->memory, 0, MEM_RELEASE);
    D9ResourceClear(&b->base);
    HeapFree(GetProcessHeap(), 0, b);
}

static void destroy_resource(D9Resource *r) { destroy(CONTAINING_RECORD(r, D9Buffer, base)); }

/* whether the gpu reads the buffer's own memory: dynamic buffers, mapped. */
static BOOL mapped(const D9Buffer *b) { return b->memory == NULL; }

/* the gpu is done with what it was given: a plain lock of a dynamic buffer it used waits for that. */
static void wait_idle(D9Device *d)
{
    D3D11_QUERY_DESC desc = {D3D11_QUERY_EVENT};
    ID3D11Query *query;
    if (FAILED(d->d3d11->lpVtbl->CreateQuery(d->d3d11, &desc, &query)))
        return;
    d->context->lpVtbl->End(d->context, (ID3D11Asynchronous *)query);
    d->context->lpVtbl->Flush(d->context);
    BOOL done = FALSE;
    while (d->context->lpVtbl->GetData(d->context, (ID3D11Asynchronous *)query, &done, sizeof(done), 0) == S_FALSE)
        SwitchToThread();
    query->lpVtbl->Release(query);
}

static HRESULT lock(D9Buffer *b, UINT offset, UINT size, void **data, DWORD flags)
{
    D9Device *d = b->base.device;
    if (!data)
        return D3DERR_INVALIDCALL;
    *data = NULL;
    if (offset > b->size || (size && offset + size > b->size))
        return D3DERR_INVALIDCALL;
    if (!size)
        size = b->size - offset;
    if (!size && !offset)
        size = b->size;
    if (mapped(b)) {
        if (!b->locks) {
            D3D11_MAP map = D3D11_MAP_WRITE_NO_OVERWRITE;
            if ((flags & D3DLOCK_DISCARD) || !b->mapped_once) {
                map = D3D11_MAP_WRITE_DISCARD;
            } else if (!(flags & D3DLOCK_NOOVERWRITE) && b->used) {
                wait_idle(d);
            }
            D3D11_MAPPED_SUBRESOURCE m;
            HRESULT hr = d->context->lpVtbl->Map(d->context, (ID3D11Resource *)b->buffer, 0, map, 0, &m);
            if (FAILED(hr))
                return D3DERR_INVALIDCALL;
            b->mapped_bytes = m.pData;
            b->mapped_once = TRUE;
            if (map == D3D11_MAP_WRITE_DISCARD)
                b->used = FALSE;
        }
        b->locks++;
        *data = b->mapped_bytes + offset;
        return D3D_OK;
    }
    /* the dirty range grows to cover every lock until the last unlock. */
    if (!(flags & D3DLOCK_READONLY)) {
        b->dirty_lo = b->locks || b->dirty_hi ? D9_MIN(b->dirty_lo, offset) : offset;
        b->dirty_hi = D9_MAX(b->dirty_hi, offset + size);
    }
    b->locks++;
    *data = b->memory + offset;
    return D3D_OK;
}

static HRESULT unlock(D9Buffer *b)
{
    D9Device *d = b->base.device;
    if (!b->locks)
        return D3DERR_INVALIDCALL;
    if (--b->locks)
        return D3D_OK;
    if (mapped(b)) {
        d->context->lpVtbl->Unmap(d->context, (ID3D11Resource *)b->buffer, 0);
        b->mapped_bytes = NULL;
        return D3D_OK;
    }
    if (b->buffer && b->dirty_hi > b->dirty_lo) {
        D3D11_BOX box = {b->dirty_lo, 0, 0, b->dirty_hi, 1, 1};
        d->context->lpVtbl->UpdateSubresource(d->context, (ID3D11Resource *)b->buffer, 0, &box,
                                              b->memory + b->dirty_lo, 0, 0);
    }
    b->dirty_lo = b->dirty_hi = 0;
    return D3D_OK;
}

/* ------------------------------------------------------------------ methods */

static HRESULT STDMETHODCALLTYPE buffer_query(IDirect3DVertexBuffer9 *iface, REFIID riid, void **out)
{
    D9Buffer *b = impl(iface);
    if (!out)
        return E_POINTER;
    *out = NULL;
    const IID *own = b->base.type == D3DRTYPE_INDEXBUFFER ? &IID_IDirect3DIndexBuffer9 : &IID_IDirect3DVertexBuffer9;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DResource9) || IsEqualGUID(riid, own)) {
        *out = iface;
        D9ResourceAddRef(&b->base);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE buffer_add_ref(IDirect3DVertexBuffer9 *iface) { return D9ResourceAddRef(&impl(iface)->base); }

static ULONG STDMETHODCALLTYPE buffer_release(IDirect3DVertexBuffer9 *iface) { return D9ResourceRelease(&impl(iface)->base); }

static HRESULT STDMETHODCALLTYPE buffer_get_device(IDirect3DVertexBuffer9 *iface, IDirect3DDevice9 **device)
{
    return D9ResourceGetDevice(&impl(iface)->base, device);
}

static HRESULT STDMETHODCALLTYPE buffer_set_private_data(IDirect3DVertexBuffer9 *iface, REFGUID guid,
                                                         const void *data, DWORD size, DWORD flags)
{
    return D9ResourceSetPrivateData(&impl(iface)->base, guid, data, size, flags);
}

static HRESULT STDMETHODCALLTYPE buffer_get_private_data(IDirect3DVertexBuffer9 *iface, REFGUID guid, void *data,
                                                         DWORD *size)
{
    return D9ResourceGetPrivateData(&impl(iface)->base, guid, data, size);
}

static HRESULT STDMETHODCALLTYPE buffer_free_private_data(IDirect3DVertexBuffer9 *iface, REFGUID guid)
{
    return D9ResourceFreePrivateData(&impl(iface)->base, guid);
}

static DWORD STDMETHODCALLTYPE buffer_set_priority(IDirect3DVertexBuffer9 *iface, DWORD priority)
{
    return D9ResourceSetPriority(&impl(iface)->base, priority);
}

static DWORD STDMETHODCALLTYPE buffer_get_priority(IDirect3DVertexBuffer9 *iface) { return impl(iface)->base.priority; }

static void STDMETHODCALLTYPE buffer_pre_load(IDirect3DVertexBuffer9 *iface) {}

static D3DRESOURCETYPE STDMETHODCALLTYPE buffer_get_type(IDirect3DVertexBuffer9 *iface) { return impl(iface)->base.type; }

static HRESULT STDMETHODCALLTYPE buffer_lock(IDirect3DVertexBuffer9 *iface, UINT offset, UINT size, void **data,
                                             DWORD flags)
{
    D9Buffer *b = impl(iface);
    D9Lock(b->base.device);
    HRESULT hr = lock(b, offset, size, data, flags);
    D9Unlock(b->base.device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE buffer_unlock(IDirect3DVertexBuffer9 *iface)
{
    D9Buffer *b = impl(iface);
    D9Lock(b->base.device);
    HRESULT hr = unlock(b);
    D9Unlock(b->base.device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE vb_get_desc(IDirect3DVertexBuffer9 *iface, D3DVERTEXBUFFER_DESC *desc)
{
    D9Buffer *b = impl(iface);
    if (!desc)
        return D3DERR_INVALIDCALL;
    *desc = (D3DVERTEXBUFFER_DESC){D3DFMT_VERTEXDATA, D3DRTYPE_VERTEXBUFFER, b->base.usage, b->base.pool, b->size,
                                   b->fvf};
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE ib_get_desc(IDirect3DIndexBuffer9 *iface, D3DINDEXBUFFER_DESC *desc)
{
    D9Buffer *b = impl(iface);
    if (!desc)
        return D3DERR_INVALIDCALL;
    *desc = (D3DINDEXBUFFER_DESC){b->base.d3d_format, D3DRTYPE_INDEXBUFFER, b->base.usage, b->base.pool, b->size};
    return D3D_OK;
}

static const IDirect3DVertexBuffer9Vtbl vb_vtbl = {
    buffer_query,        buffer_add_ref,          buffer_release,          buffer_get_device,
    buffer_set_private_data, buffer_get_private_data, buffer_free_private_data, buffer_set_priority,
    buffer_get_priority, buffer_pre_load,         buffer_get_type,         buffer_lock,
    buffer_unlock,       vb_get_desc,
};

static const IDirect3DIndexBuffer9Vtbl ib_vtbl = {
    (void *)buffer_query,        (void *)buffer_add_ref,          (void *)buffer_release,
    (void *)buffer_get_device,   (void *)buffer_set_private_data, (void *)buffer_get_private_data,
    (void *)buffer_free_private_data, (void *)buffer_set_priority, (void *)buffer_get_priority,
    (void *)buffer_pre_load,     (void *)buffer_get_type,         (void *)buffer_lock,
    (void *)buffer_unlock,       ib_get_desc,
};

HRESULT D9CreateBuffer(D9Device *device, BOOL index, UINT size, DWORD usage, DWORD fvf, D3DFORMAT format,
                       D3DPOOL pool, D9Buffer **out)
{
    if (!size || (index && format != D3DFMT_INDEX16 && format != D3DFMT_INDEX32))
        return D3DERR_INVALIDCALL;
    if ((usage & D3DUSAGE_DYNAMIC) && pool == D3DPOOL_MANAGED)
        return D3DERR_INVALIDCALL;
    D9Buffer *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b)
        return E_OUTOFMEMORY;
    b->iface.vb.lpVtbl = index ? (const void *)&ib_vtbl : (const void *)&vb_vtbl;
    D9ResourceInit(&b->base, device, index ? D3DRTYPE_INDEXBUFFER : D3DRTYPE_VERTEXBUFFER, pool, usage,
                   index ? format : D3DFMT_VERTEXDATA, destroy_resource);
    b->size = size;
    b->fvf = index ? 0 : fvf;
    /* dynamic buffers that are only written are mapped; the others keep their bytes in memory. */
    BOOL dynamic = (usage & D3DUSAGE_DYNAMIC) && (usage & D3DUSAGE_WRITEONLY) && pool == D3DPOOL_DEFAULT;
    D3D11_BUFFER_DESC desc = {D9_ALIGN(size, 4), dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT,
                              index ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER,
                              dynamic ? D3D11_CPU_ACCESS_WRITE : 0};
    HRESULT hr = device->d3d11->lpVtbl->CreateBuffer(device->d3d11, &desc, NULL, &b->buffer);
    if (SUCCEEDED(hr) && !dynamic) {
        b->memory = VirtualAlloc(NULL, desc.ByteWidth, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!b->memory)
            hr = E_OUTOFMEMORY;
    }
    if (FAILED(hr)) {
        destroy(b);
        device->iface.lpVtbl->Release(&device->iface);
        return hr == E_OUTOFMEMORY ? hr : D3DERR_OUTOFVIDEOMEMORY;
    }
    *out = b;
    return D3D_OK;
}
