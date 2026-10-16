/*
 * buffers and textures. a buffer is a range of a buffer block; dynamic
 * ones move to a new range when the program discards them while the gpu
 * may still read the old one, so mapping never waits. textures are images
 * in the general layout, except staging textures, which are their
 * subresources packed in mapped memory, as the cpu reads and writes them.
 */
#include "d3d11_private.h"

#define VTBL_FN(f) ((void *)(f))

typedef struct D3dSpare {
    D3dAllocation alloc;
    UINT64 serial;
    /* the descriptor of the whole range, kept with it. */
    UINT cbv;
} D3dSpare;

/*
 * the renamed ranges of dynamic buffers, waiting for the gpu to be done with
 * them: a ring in the order they were given up, so in serial order.
 */
typedef struct D3dSpares {
    D3dSpare *list;
    UINT head, count, capacity;
} D3dSpares;

static D3dResource *impl(ID3D11Resource *iface)
{
    return CONTAINING_RECORD((void *)iface, D3dResource, u);
}

D3dResource *D3dResourceFrom(ID3D11Resource *iface) { return iface ? impl(iface) : NULL; }

UINT D3dSubresourceCount(const D3dResource *r)
{
    return r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER ? 1 : r->tex.mips * r->tex.layers;
}

void D3dSubresourceDecompose(const D3dResource *r, UINT index, UINT *mip, UINT *layer)
{
    *mip = index % r->tex.mips;
    *layer = index / r->tex.mips;
}

VkExtent3D D3dMipExtent(const D3dResource *r, UINT mip)
{
    VkExtent3D e = {D3D_MAX(r->tex.width >> mip, 1), D3D_MAX(r->tex.height >> mip, 1),
                    D3D_MAX(r->tex.depth >> mip, 1)};
    return e;
}

void D3dResourceAddRef(D3dResource *r) { InterlockedIncrement(&r->child.refs); }

/* ------------------------------------------------------------------ destruction */

static void free_spares(D3dResource *r);

static void free_cbv(D3dDevice *device, UINT64 serial, UINT cbv)
{
    if (cbv)
        D3dDefer(device, serial, D3D_GARBAGE_HEAP, (UINT64)D3D_HEAP_UNIFORM_BUFFER << 32 | cbv);
}

static void destroy(D3dResource *r)
{
    D3dDevice *device = r->child.device;
    UINT64 serial = D3D_MAX(r->last_use, device->next_serial);
    if (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
        D3dDeferFree(device, serial, &r->buf.alloc);
        free_cbv(device, serial, r->buf.cbv);
        free_spares(r);
    } else {
        D3dDefer(device, serial, D3D_GARBAGE_IMAGE, (UINT64)r->tex.image);
        D3dDeferFree(device, serial, &r->tex.alloc);
        D3dDeferFree(device, serial, &r->tex.staging);
        HeapFree(GetProcessHeap(), 0, r->tex.sub_offset);
    }
    D3dChildRelease(&r->child);
    HeapFree(GetProcessHeap(), 0, r);
}

void D3dResourceRelease(D3dResource *r)
{
    if (!InterlockedDecrement(&r->child.refs))
        destroy(r);
}

/* ------------------------------------------------------------------ dynamic buffer slices */

static D3dSpares *spares_of(D3dResource *r) { return (D3dSpares *)r->buf.spares; }

/*
 * a fresh range for a discarded dynamic buffer: one the gpu is done with,
 * or a new one. the old range waits for what was recorded up to now.
 */
BOOL D3dBufferRename(D3dDevice *device, D3dResource *r)
{
    D3dSpares *spares = spares_of(r);
    if (!spares) {
        spares = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*spares));
        if (!spares)
            return FALSE;
        r->buf.spares = spares;
    }
    /* asking the gpu where it is costs a round trip: only before growing. */
    BOOL reuse = spares->count && (spares->list[spares->head].serial <= device->completed ||
                                   spares->list[spares->head].serial <= D3dCompleted(device));
    D3dAllocation next;
    UINT cbv = 0;
    if (reuse) {
        next = spares->list[spares->head].alloc;
        cbv = spares->list[spares->head].cbv;
        spares->head = (spares->head + 1) % spares->capacity;
        spares->count--;
    } else if (FAILED(D3dAllocBuffer(device, D3D_MEMORY_UPLOAD, r->buf.size, 256, &next))) {
        return FALSE;
    } else {
        device->renames++;
    }
    if (spares->count == spares->capacity) {
        UINT capacity = spares->capacity ? spares->capacity * 2 : 4;
        D3dSpare *list = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
        if (!list) {
            D3dFree(device, &next);
            free_cbv(device, 0, cbv);
            return FALSE;
        }
        for (UINT i = 0; i < spares->count; i++)
            list[i] = spares->list[(spares->head + i) % spares->capacity];
        HeapFree(GetProcessHeap(), 0, spares->list);
        spares->list = list;
        spares->head = 0;
        spares->capacity = capacity;
    }
    D3dSpare *tail = &spares->list[(spares->head + spares->count++) % spares->capacity];
    tail->alloc = r->buf.alloc;
    tail->serial = D3D_MAX(r->last_use, device->next_serial);
    tail->cbv = r->buf.cbv;
    r->buf.cbv = cbv;
    r->buf.alloc = next;
    r->buf.buffer = next.buffer;
    r->buf.offset = next.offset;
    r->buf.mapped = next.mapped;
    r->buf.version++;
    return TRUE;
}

static void free_spares(D3dResource *r)
{
    D3dSpares *spares = spares_of(r);
    if (!spares)
        return;
    for (UINT i = 0; i < spares->count; i++) {
        D3dSpare *spare = &spares->list[(spares->head + i) % spares->capacity];
        D3dDeferFree(r->child.device, spare->serial, &spare->alloc);
        free_cbv(r->child.device, spare->serial, spare->cbv);
    }
    HeapFree(GetProcessHeap(), 0, spares->list);
    HeapFree(GetProcessHeap(), 0, spares);
    r->buf.spares = NULL;
}

/* the uniform buffer descriptor of a range of the buffer's current bytes. */
UINT D3dBufferCbv(D3dResource *r, UINT offset, UINT size)
{
    if (offset >= r->buf.size)
        return 0;
    size = D3D_MIN(size, r->buf.size - offset);
    UINT whole = D3D_MIN(r->buf.size, D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16);
    if (offset || D3D_MIN(size, whole) != whole)
        return D3dCbvIndex(r->child.device, r->buf.buffer, r->buf.offset + offset, size);
    /* the whole range, the usual binding, has its own descriptor that renames carry along. */
    if (r->buf.cbv)
        return r->buf.cbv;
    D3dDevice *device = r->child.device;
    UINT index = D3dHeapAlloc(device, D3D_HEAP_UNIFORM_BUFFER);
    if (!index)
        return 0;
    D3dHeapWriteBuffer(device, index, r->buf.buffer, r->buf.offset, whole);
    UINT old = (UINT)InterlockedCompareExchange((LONG volatile *)&r->buf.cbv, (LONG)index, 0);
    if (old) {
        D3dHeapFree(device, D3D_HEAP_UNIFORM_BUFFER, index);
        return old;
    }
    return index;
}

/* ------------------------------------------------------------------ the common methods */

static HRESULT resource_query_common(D3dResource *r, REFIID riid, void **out)
{
    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild) ||
        IsEqualGUID(riid, &IID_ID3D11Resource))
        *out = r->iface;
    else if (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER && IsEqualGUID(riid, &IID_ID3D11Buffer))
        *out = r->iface;
    else if (r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D && IsEqualGUID(riid, &IID_ID3D11Texture1D))
        *out = r->iface;
    else if (r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D &&
             (IsEqualGUID(riid, &IID_ID3D11Texture2D) || IsEqualGUID(riid, &IID_ID3D11Texture2D1)))
        *out = r->iface;
    else if (r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D &&
             (IsEqualGUID(riid, &IID_ID3D11Texture3D) || IsEqualGUID(riid, &IID_ID3D11Texture3D1)))
        *out = r->iface;
    else if (IsEqualGUID(riid, &IID_IDXGIObject) || IsEqualGUID(riid, &IID_IDXGIDeviceSubObject) ||
             IsEqualGUID(riid, &IID_IDXGIResource) || IsEqualGUID(riid, &IID_IDXGIResource1))
        *out = &r->dxgi;
    else if (r->dimension != D3D11_RESOURCE_DIMENSION_BUFFER &&
             (IsEqualGUID(riid, &IID_IDXGISurface) || IsEqualGUID(riid, &IID_IDXGISurface1) ||
              IsEqualGUID(riid, &IID_IDXGISurface2)))
        *out = &r->surface;
    else
        *out = D3d10ResourceInterface(r, riid);
    if (!*out)
        return E_NOINTERFACE;
    D3dResourceAddRef(r);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE resource_query(ID3D11Resource *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    return resource_query_common(impl(iface), riid, out);
}

static ULONG STDMETHODCALLTYPE resource_add_ref(ID3D11Resource *iface)
{
    return InterlockedIncrement(&impl(iface)->child.refs);
}

static ULONG STDMETHODCALLTYPE resource_release(ID3D11Resource *iface)
{
    D3dResource *r = impl(iface);
    ULONG refs = InterlockedDecrement(&r->child.refs);
    if (!refs)
        destroy(r);
    return refs;
}

static void STDMETHODCALLTYPE resource_get_device(ID3D11Resource *iface, ID3D11Device **device)
{
    D3dDevice *d = impl(iface)->child.device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = (ID3D11Device *)&d->iface;
}

static HRESULT STDMETHODCALLTYPE resource_get_private_data(ID3D11Resource *iface, REFGUID guid, UINT *size,
                                                           void *data)
{
    return MimePrivateGet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE resource_set_private_data(ID3D11Resource *iface, REFGUID guid, UINT size,
                                                           const void *data)
{
    return MimePrivateSet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE resource_set_private_data_interface(ID3D11Resource *iface, REFGUID guid,
                                                                     const IUnknown *data)
{
    return MimePrivateSetInterface(&impl(iface)->child.data, guid, data);
}

static void STDMETHODCALLTYPE resource_get_type(ID3D11Resource *iface, D3D11_RESOURCE_DIMENSION *dimension)
{
    *dimension = impl(iface)->dimension;
}

static void STDMETHODCALLTYPE resource_set_eviction_priority(ID3D11Resource *iface, UINT priority)
{
    impl(iface)->eviction_priority = priority;
}

static UINT STDMETHODCALLTYPE resource_get_eviction_priority(ID3D11Resource *iface)
{
    return impl(iface)->eviction_priority;
}

static void STDMETHODCALLTYPE buffer_get_desc(ID3D11Buffer *iface, D3D11_BUFFER_DESC *desc)
{
    *desc = impl((ID3D11Resource *)iface)->desc.buffer;
}

static void STDMETHODCALLTYPE texture1d_get_desc(ID3D11Texture1D *iface, D3D11_TEXTURE1D_DESC *desc)
{
    *desc = impl((ID3D11Resource *)iface)->desc.texture1d;
}

static void STDMETHODCALLTYPE texture2d_get_desc1(ID3D11Texture2D1 *iface, D3D11_TEXTURE2D_DESC1 *desc)
{
    *desc = impl((ID3D11Resource *)iface)->desc.texture2d;
}

static void STDMETHODCALLTYPE texture2d_get_desc(ID3D11Texture2D1 *iface, D3D11_TEXTURE2D_DESC *desc)
{
    memcpy(desc, &impl((ID3D11Resource *)iface)->desc.texture2d, sizeof(*desc));
}

static void STDMETHODCALLTYPE texture3d_get_desc1(ID3D11Texture3D1 *iface, D3D11_TEXTURE3D_DESC1 *desc)
{
    *desc = impl((ID3D11Resource *)iface)->desc.texture3d;
}

static void STDMETHODCALLTYPE texture3d_get_desc(ID3D11Texture3D1 *iface, D3D11_TEXTURE3D_DESC *desc)
{
    memcpy(desc, &impl((ID3D11Resource *)iface)->desc.texture3d, sizeof(*desc));
}

#define RESOURCE_METHODS                                                                                               \
    VTBL_FN(resource_query), VTBL_FN(resource_add_ref), VTBL_FN(resource_release), VTBL_FN(resource_get_device),     \
        VTBL_FN(resource_get_private_data), VTBL_FN(resource_set_private_data),                                        \
        VTBL_FN(resource_set_private_data_interface), VTBL_FN(resource_get_type),                                      \
        VTBL_FN(resource_set_eviction_priority), VTBL_FN(resource_get_eviction_priority)

static const ID3D11BufferVtbl buffer_vtbl = {RESOURCE_METHODS, buffer_get_desc};
static const ID3D11Texture1DVtbl texture1d_vtbl = {RESOURCE_METHODS, texture1d_get_desc};
static const ID3D11Texture2D1Vtbl texture2d_vtbl = {RESOURCE_METHODS, texture2d_get_desc, texture2d_get_desc1};
static const ID3D11Texture3D1Vtbl texture3d_vtbl = {RESOURCE_METHODS, texture3d_get_desc, texture3d_get_desc1};

/* ------------------------------------------------------------------ IDXGIResource1 and IDXGISurface2 */

static D3dResource *dxgi_impl(IDXGIResource1 *iface) { return CONTAINING_RECORD(iface, D3dResource, dxgi); }

static HRESULT STDMETHODCALLTYPE dxgi_query(IDXGIResource1 *iface, REFIID riid, void **out)
{
    return resource_query_common(dxgi_impl(iface), riid, out);
}

static ULONG STDMETHODCALLTYPE dxgi_add_ref(IDXGIResource1 *iface) { return resource_add_ref(dxgi_impl(iface)->iface); }

static ULONG STDMETHODCALLTYPE dxgi_release(IDXGIResource1 *iface) { return resource_release(dxgi_impl(iface)->iface); }

static HRESULT STDMETHODCALLTYPE dxgi_set_private_data(IDXGIResource1 *iface, REFGUID guid, UINT size,
                                                       const void *data)
{
    return MimePrivateSet(&dxgi_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_set_private_data_interface(IDXGIResource1 *iface, REFGUID guid,
                                                                 const IUnknown *data)
{
    return MimePrivateSetInterface(&dxgi_impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_get_private_data(IDXGIResource1 *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&dxgi_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_get_parent(IDXGIResource1 *iface, REFIID riid, void **parent)
{
    D3dDevice *device = dxgi_impl(iface)->child.device;
    IDXGIAdapter *adapter = device->adapter;
    return adapter->lpVtbl->QueryInterface(adapter, riid, parent);
}

static HRESULT STDMETHODCALLTYPE dxgi_get_device(IDXGIResource1 *iface, REFIID riid, void **device)
{
    D3dDevice *d = dxgi_impl(iface)->child.device;
    return d->iface.lpVtbl->QueryInterface(&d->iface, riid, device);
}

static HRESULT STDMETHODCALLTYPE dxgi_get_shared_handle(IDXGIResource1 *iface, HANDLE *handle)
{
    if (!handle)
        return E_INVALIDARG;
    *handle = NULL;
    return dxgi_impl(iface)->misc & D3D11_RESOURCE_MISC_SHARED ? E_NOTIMPL : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE dxgi_get_usage(IDXGIResource1 *iface, DXGI_USAGE *usage)
{
    D3dResource *r = dxgi_impl(iface);
    DXGI_USAGE u = 0;
    if (r->bind & D3D11_BIND_SHADER_RESOURCE)
        u |= DXGI_USAGE_SHADER_INPUT;
    if (r->bind & D3D11_BIND_RENDER_TARGET)
        u |= DXGI_USAGE_RENDER_TARGET_OUTPUT;
    if (r->bind & D3D11_BIND_UNORDERED_ACCESS)
        u |= DXGI_USAGE_UNORDERED_ACCESS;
    if (r->swapchain)
        u |= DXGI_USAGE_BACK_BUFFER;
    *usage = u;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_set_eviction_priority(IDXGIResource1 *iface, UINT priority)
{
    dxgi_impl(iface)->eviction_priority = priority;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_get_eviction_priority(IDXGIResource1 *iface, UINT *priority)
{
    *priority = dxgi_impl(iface)->eviction_priority;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_create_subresource_surface(IDXGIResource1 *iface, UINT index,
                                                                 IDXGISurface2 **surface)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_create_shared_handle(IDXGIResource1 *iface,
                                                           const SECURITY_ATTRIBUTES *attributes, DWORD access,
                                                           LPCWSTR name, HANDLE *handle)
{
    return E_NOTIMPL;
}

static const IDXGIResource1Vtbl dxgi_vtbl = {
    dxgi_query,
    dxgi_add_ref,
    dxgi_release,
    dxgi_set_private_data,
    dxgi_set_private_data_interface,
    dxgi_get_private_data,
    dxgi_get_parent,
    dxgi_get_device,
    dxgi_get_shared_handle,
    dxgi_get_usage,
    dxgi_set_eviction_priority,
    dxgi_get_eviction_priority,
    dxgi_create_subresource_surface,
    dxgi_create_shared_handle,
};

static D3dResource *surface_impl(IDXGISurface2 *iface) { return CONTAINING_RECORD(iface, D3dResource, surface); }

static HRESULT STDMETHODCALLTYPE surface_query(IDXGISurface2 *iface, REFIID riid, void **out)
{
    return resource_query_common(surface_impl(iface), riid, out);
}

static ULONG STDMETHODCALLTYPE surface_add_ref(IDXGISurface2 *iface)
{
    return resource_add_ref(surface_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE surface_release(IDXGISurface2 *iface)
{
    return resource_release(surface_impl(iface)->iface);
}

static HRESULT STDMETHODCALLTYPE surface_set_private_data(IDXGISurface2 *iface, REFGUID guid, UINT size,
                                                          const void *data)
{
    return MimePrivateSet(&surface_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE surface_set_private_data_interface(IDXGISurface2 *iface, REFGUID guid,
                                                                    const IUnknown *data)
{
    return MimePrivateSetInterface(&surface_impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE surface_get_private_data(IDXGISurface2 *iface, REFGUID guid, UINT *size,
                                                          void *data)
{
    return MimePrivateGet(&surface_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE surface_get_parent(IDXGISurface2 *iface, REFIID riid, void **parent)
{
    return dxgi_get_parent(&surface_impl(iface)->dxgi, riid, parent);
}

static HRESULT STDMETHODCALLTYPE surface_get_device(IDXGISurface2 *iface, REFIID riid, void **device)
{
    return dxgi_get_device(&surface_impl(iface)->dxgi, riid, device);
}

static HRESULT STDMETHODCALLTYPE surface_get_desc(IDXGISurface2 *iface, DXGI_SURFACE_DESC *desc)
{
    D3dResource *r = surface_impl(iface);
    if (!desc)
        return E_INVALIDARG;
    desc->Width = r->tex.width;
    desc->Height = r->tex.height;
    desc->Format = r->tex.format;
    desc->SampleDesc.Count = r->tex.samples;
    desc->SampleDesc.Quality = 0;
    return S_OK;
}

HRESULT D3dContextMapSurface(D3dContext *context, D3dResource *r, DXGI_MAPPED_RECT *rect, UINT flags);
void D3dContextUnmapSurface(D3dContext *context, D3dResource *r);

static HRESULT STDMETHODCALLTYPE surface_map(IDXGISurface2 *iface, DXGI_MAPPED_RECT *rect, UINT flags)
{
    D3dResource *r = surface_impl(iface);
    return D3dContextMapSurface(r->child.device->immediate, r, rect, flags);
}

static HRESULT STDMETHODCALLTYPE surface_unmap(IDXGISurface2 *iface)
{
    D3dResource *r = surface_impl(iface);
    D3dContextUnmapSurface(r->child.device->immediate, r);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_get_dc(IDXGISurface2 *iface, BOOL discard, HDC *dc)
{
    D3dLog("d3d11: GetDC on surfaces is not implemented");
    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE surface_release_dc(IDXGISurface2 *iface, RECT *dirty)
{
    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE surface_get_resource(IDXGISurface2 *iface, REFIID riid, void **parent, UINT *index)
{
    if (index)
        *index = 0;
    return resource_query_common(surface_impl(iface), riid, parent);
}

static const IDXGISurface2Vtbl surface_vtbl = {
    surface_query,
    surface_add_ref,
    surface_release,
    surface_set_private_data,
    surface_set_private_data_interface,
    surface_get_private_data,
    surface_get_parent,
    surface_get_device,
    surface_get_desc,
    surface_map,
    surface_unmap,
    surface_get_dc,
    surface_release_dc,
    surface_get_resource,
};

/* ------------------------------------------------------------------ creation */

static D3dResource *new_resource(D3dDevice *device, D3D11_RESOURCE_DIMENSION dimension)
{
    D3dResource *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*r));
    if (!r)
        return NULL;
    D3dChildInit(&r->child, device);
    r->dimension = dimension;
    r->iface = (ID3D11Resource *)&r->u;
    r->dxgi.lpVtbl = &dxgi_vtbl;
    r->surface.lpVtbl = &surface_vtbl;
    r->eviction_priority = DXGI_RESOURCE_PRIORITY_NORMAL;
    return r;
}

/* copies the program's bytes to device memory through the ring, in the init commands. */
static BOOL upload_buffer(D3dDevice *device, VkBuffer buffer, VkDeviceSize offset, const void *data, UINT size)
{
    D3dRingSlice slice;
    if (!D3dRingAlloc(device, size, 16, &slice))
        return FALSE;
    memcpy(slice.mapped, data, size);
    VkCommandBuffer cmd = D3dInitCommands(device);
    VkBufferCopy copy = {slice.offset, offset, size};
    device->vk.vkCmdCopyBuffer(cmd, slice.buffer, buffer, 1, &copy);
    D3dInitDone(device);
    return TRUE;
}

HRESULT D3dCreateBuffer(D3dDevice *device, const D3D11_BUFFER_DESC *desc, const D3D11_SUBRESOURCE_DATA *data,
                        D3dResource **out)
{
    if (!desc->ByteWidth || desc->Usage > D3D11_USAGE_STAGING)
        return E_INVALIDARG;
    if ((desc->MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) &&
        (!desc->StructureByteStride || desc->StructureByteStride % 4 || desc->ByteWidth % desc->StructureByteStride))
        return E_INVALIDARG;
    if ((desc->BindFlags & D3D11_BIND_CONSTANT_BUFFER) && desc->ByteWidth % 16)
        return E_INVALIDARG;
    if (desc->Usage == D3D11_USAGE_IMMUTABLE && (!data || !data->pSysMem))
        return E_INVALIDARG;
    if (desc->Usage == D3D11_USAGE_DYNAMIC && desc->CPUAccessFlags != D3D11_CPU_ACCESS_WRITE)
        return E_INVALIDARG;
    if (desc->Usage == D3D11_USAGE_STAGING && (desc->BindFlags || !desc->CPUAccessFlags))
        return E_INVALIDARG;
    if (!out)
        return S_FALSE;

    D3dResource *r = new_resource(device, D3D11_RESOURCE_DIMENSION_BUFFER);
    if (!r)
        return E_OUTOFMEMORY;
    r->iface = (ID3D11Resource *)&r->u.buffer;
    r->u.buffer.lpVtbl = &buffer_vtbl;
    r->desc.buffer = *desc;
    r->usage = desc->Usage;
    r->bind = desc->BindFlags;
    r->cpu_access = desc->CPUAccessFlags;
    r->misc = desc->MiscFlags;
    r->buf.size = desc->ByteWidth;
    r->buf.stride = desc->StructureByteStride;
    D3dMemoryKind kind = D3D_MEMORY_DEVICE;
    /*
     * constant buffers alone live in mapped memory: UpdateSubresource then
     * renames them as discards do, without stopping rendering for a copy.
     */
    if (desc->Usage == D3D11_USAGE_DYNAMIC ||
        (desc->Usage == D3D11_USAGE_DEFAULT && desc->BindFlags == D3D11_BIND_CONSTANT_BUFFER))
        kind = D3D_MEMORY_UPLOAD;
    else if (desc->Usage == D3D11_USAGE_STAGING)
        kind = desc->CPUAccessFlags & D3D11_CPU_ACCESS_READ ? D3D_MEMORY_READBACK : D3D_MEMORY_UPLOAD;
    /* a uav counter or raw views may read the whole buffer as 4 byte texels. */
    UINT size = (UINT)D3D_ALIGN(desc->ByteWidth, 16);
    if (desc->BindFlags & D3D11_BIND_STREAM_OUTPUT) {
        r->buf.so_counter = size;
        size += 16;
    }
    HRESULT hr = D3dAllocBuffer(device, kind, size, 256, &r->buf.alloc);
    if (FAILED(hr)) {
        destroy(r);
        return hr;
    }
    r->buf.buffer = r->buf.alloc.buffer;
    r->buf.offset = r->buf.alloc.offset;
    r->buf.mapped = r->buf.alloc.mapped;
    if (data && data->pSysMem) {
        if (r->buf.mapped)
            memcpy(r->buf.mapped, data->pSysMem, desc->ByteWidth);
        else if (!upload_buffer(device, r->buf.buffer, r->buf.offset, data->pSysMem, desc->ByteWidth)) {
            destroy(r);
            return E_OUTOFMEMORY;
        }
    } else if (!r->buf.mapped) {
        /* direct3d hands out zeroed buffers. */
        VkCommandBuffer cmd = D3dInitCommands(device);
        device->vk.vkCmdFillBuffer(cmd, r->buf.buffer, r->buf.offset, size, 0);
        D3dInitDone(device);
    } else {
        memset(r->buf.mapped, 0, size);
    }
    if (r->buf.so_counter && data && data->pSysMem) {
        /* nothing streamed out yet. */
        VkCommandBuffer cmd = D3dInitCommands(device);
        device->vk.vkCmdFillBuffer(cmd, r->buf.buffer, r->buf.offset + r->buf.so_counter, 4, 0);
        D3dInitDone(device);
    }
    *out = r;
    return S_OK;
}

/* the packed layout of a staging texture: each subresource's offset and pitches. */
static BOOL layout_staging(D3dResource *r, VkDeviceSize *total)
{
    UINT count = D3dSubresourceCount(r);
    r->tex.sub_offset = HeapAlloc(GetProcessHeap(), 0, count * 3 * sizeof(UINT));
    if (!r->tex.sub_offset)
        return FALSE;
    r->tex.sub_row_pitch = r->tex.sub_offset + count;
    r->tex.sub_slice_pitch = r->tex.sub_offset + 2 * count;
    const D3dFormat *f = r->tex.fmt;
    UINT align = f->block_bytes % 16 ? f->block_bytes * 16 : f->block_bytes;
    VkDeviceSize offset = 0;
    for (UINT i = 0; i < count; i++) {
        UINT mip, layer;
        D3dSubresourceDecompose(r, i, &mip, &layer);
        VkExtent3D e = D3dMipExtent(r, mip);
        UINT row = (e.width + f->block - 1) / f->block * f->block_bytes;
        UINT rows = (e.height + f->block - 1) / f->block;
        offset = D3D_ALIGN(offset, align);
        r->tex.sub_offset[i] = (UINT)offset;
        r->tex.sub_row_pitch[i] = row;
        r->tex.sub_slice_pitch[i] = row * rows;
        offset += (VkDeviceSize)row * rows * e.depth;
    }
    *total = offset;
    return TRUE;
}

/* the copy region of a subresource between its image and its packed bytes. */
static VkBufferImageCopy staging_region(const D3dResource *r, UINT index, VkDeviceSize base)
{
    UINT mip, layer;
    D3dSubresourceDecompose(r, index, &mip, &layer);
    VkBufferImageCopy c = {0};
    c.bufferOffset = base + r->tex.sub_offset[index];
    c.bufferRowLength = r->tex.sub_row_pitch[index] / r->tex.fmt->block_bytes * r->tex.fmt->block;
    c.bufferImageHeight = r->tex.sub_slice_pitch[index] / r->tex.sub_row_pitch[index] * r->tex.fmt->block;
    c.imageSubresource.aspectMask = r->tex.aspect;
    c.imageSubresource.mipLevel = mip;
    c.imageSubresource.baseArrayLayer = layer;
    c.imageSubresource.layerCount = 1;
    c.imageExtent = D3dMipExtent(r, mip);
    return c;
}

/* depth and stencil copy one aspect at a time; buffers hold the depth before the stencil. */
void D3dImageCopyRegions(const D3dResource *r, UINT index, VkDeviceSize base, VkBufferImageCopy *regions,
                         UINT *count)
{
    regions[0] = staging_region(r, index, base);
    *count = 1;
    if (r->tex.aspect == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
        regions[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        regions[1] = regions[0];
        regions[1].imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        *count = 2;
    }
}

static HRESULT upload_texture(D3dDevice *device, D3dResource *r, const D3D11_SUBRESOURCE_DATA *data)
{
    UINT count = D3dSubresourceCount(r);
    const D3dFormat *f = r->tex.fmt;
    for (UINT i = 0; i < count; i++) {
        UINT mip, layer;
        D3dSubresourceDecompose(r, i, &mip, &layer);
        VkExtent3D e = D3dMipExtent(r, mip);
        UINT row = (e.width + f->block - 1) / f->block * f->block_bytes;
        UINT rows = (e.height + f->block - 1) / f->block;
        if (!data[i].pSysMem)
            return E_INVALIDARG;
        if (r->tex.staging.mapped && !r->tex.image) {
            BYTE *dst = r->tex.staging.mapped + r->tex.sub_offset[i];
            for (UINT z = 0; z < e.depth; z++)
                for (UINT y = 0; y < rows; y++)
                    memcpy(dst + (SIZE_T)z * r->tex.sub_slice_pitch[i] + (SIZE_T)y * r->tex.sub_row_pitch[i],
                           (const BYTE *)data[i].pSysMem + (SIZE_T)z * data[i].SysMemSlicePitch +
                               (SIZE_T)y * data[i].SysMemPitch,
                           row);
            continue;
        }
        VkDeviceSize size = (VkDeviceSize)row * rows * e.depth;
        D3dRingSlice slice;
        if (!D3dRingAlloc(device, size, D3D_MAX(f->block_bytes, 16), &slice))
            return E_OUTOFMEMORY;
        for (UINT z = 0; z < e.depth; z++)
            for (UINT y = 0; y < rows; y++)
                memcpy(slice.mapped + (SIZE_T)z * row * rows + (SIZE_T)y * row,
                       (const BYTE *)data[i].pSysMem + (SIZE_T)z * data[i].SysMemSlicePitch +
                           (SIZE_T)y * data[i].SysMemPitch,
                       row);
        VkBufferImageCopy copy = {slice.offset, 0, 0, {r->tex.aspect & ~VK_IMAGE_ASPECT_STENCIL_BIT, mip, layer, 1},
                                  {0, 0, 0}, e};
        if (r->tex.aspect == VK_IMAGE_ASPECT_STENCIL_BIT)
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        VkCommandBuffer cmd = D3dInitCommands(device);
        device->vk.vkCmdCopyBufferToImage(cmd, slice.buffer, r->tex.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        D3dInitDone(device);
    }
    return S_OK;
}

static UINT full_mips(UINT width, UINT height, UINT depth)
{
    UINT size = D3D_MAX(D3D_MAX(width, height), depth), mips = 1;
    while (size > 1) {
        size >>= 1;
        mips++;
    }
    return mips;
}

HRESULT D3dCreateTexture(D3dDevice *device, D3D11_RESOURCE_DIMENSION dimension, const void *desc,
                         const D3D11_SUBRESOURCE_DATA *data, D3dResource **out)
{
    UINT width, height = 1, depth = 1, mips, layers = 1, samples = 1, bind, cpu, misc;
    D3D11_USAGE usage;
    DXGI_FORMAT format;
    switch (dimension) {
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D: {
        const D3D11_TEXTURE1D_DESC *d = desc;
        width = d->Width, mips = d->MipLevels, layers = d->ArraySize, format = d->Format;
        usage = d->Usage, bind = d->BindFlags, cpu = d->CPUAccessFlags, misc = d->MiscFlags;
        break;
    }
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D: {
        const D3D11_TEXTURE2D_DESC1 *d = desc;
        width = d->Width, height = d->Height, mips = d->MipLevels, layers = d->ArraySize, format = d->Format;
        samples = d->SampleDesc.Count;
        usage = d->Usage, bind = d->BindFlags, cpu = d->CPUAccessFlags, misc = d->MiscFlags;
        break;
    }
    default: {
        const D3D11_TEXTURE3D_DESC1 *d = desc;
        width = d->Width, height = d->Height, depth = d->Depth, mips = d->MipLevels, format = d->Format;
        usage = d->Usage, bind = d->BindFlags, cpu = d->CPUAccessFlags, misc = d->MiscFlags;
        break;
    }
    }
    const D3dFormat *f = D3dGetFormat(format);
    if (!width || !height || !depth || !layers || !samples || f->vk == VK_FORMAT_UNDEFINED)
        return E_INVALIDARG;
    if (mips > full_mips(width, height, depth))
        return E_INVALIDARG;
    if (!mips)
        mips = samples > 1 ? 1 : full_mips(width, height, depth);
    if (usage == D3D11_USAGE_IMMUTABLE && !data)
        return E_INVALIDARG;
    if ((misc & D3D11_RESOURCE_MISC_TEXTURECUBE) && layers % 6)
        return E_INVALIDARG;
    if (usage == D3D11_USAGE_STAGING && bind)
        return E_INVALIDARG;
    if (!out)
        return S_FALSE;

    D3dResource *r = new_resource(device, dimension);
    if (!r)
        return E_OUTOFMEMORY;
    switch (dimension) {
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        r->iface = (ID3D11Resource *)&r->u.texture1d;
        r->u.texture1d.lpVtbl = &texture1d_vtbl;
        r->desc.texture1d = *(const D3D11_TEXTURE1D_DESC *)desc;
        r->desc.texture1d.MipLevels = mips;
        break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        r->iface = (ID3D11Resource *)&r->u.texture2d;
        r->u.texture2d.lpVtbl = &texture2d_vtbl;
        r->desc.texture2d = *(const D3D11_TEXTURE2D_DESC1 *)desc;
        r->desc.texture2d.MipLevels = mips;
        break;
    default:
        r->iface = (ID3D11Resource *)&r->u.texture3d;
        r->u.texture3d.lpVtbl = &texture3d_vtbl;
        r->desc.texture3d = *(const D3D11_TEXTURE3D_DESC1 *)desc;
        r->desc.texture3d.MipLevels = mips;
        break;
    }
    r->usage = usage;
    r->bind = bind;
    r->cpu_access = cpu;
    r->misc = misc;
    r->tex.format = format;
    r->tex.fmt = f;
    r->tex.width = width;
    r->tex.height = height;
    r->tex.depth = depth;
    r->tex.mips = mips;
    r->tex.layers = layers;
    r->tex.samples = samples;
    BOOL depth_format = (bind & D3D11_BIND_DEPTH_STENCIL) || (f->flags & (D3D_FORMAT_DEPTH | D3D_FORMAT_STENCIL));
    VkFormat vk_format = depth_format && f->depth ? f->depth : f->vk;
    r->tex.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (depth_format && f->depth) {
        r->tex.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (vk_format == VK_FORMAT_D24_UNORM_S8_UINT || vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT)
            r->tex.aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    HRESULT hr = S_OK;
    VkDeviceSize staging_size = 0;
    if (usage == D3D11_USAGE_STAGING || usage == D3D11_USAGE_DYNAMIC) {
        if (!layout_staging(r, &staging_size)) {
            destroy(r);
            return E_OUTOFMEMORY;
        }
        D3dMemoryKind kind = cpu & D3D11_CPU_ACCESS_READ ? D3D_MEMORY_READBACK : D3D_MEMORY_UPLOAD;
        if (FAILED(hr = D3dAllocBuffer(device, kind, staging_size, 256, &r->tex.staging))) {
            destroy(r);
            return hr;
        }
        memset(r->tex.staging.mapped, 0, staging_size);
    }
    if (usage != D3D11_USAGE_STAGING) {
        VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D   ? VK_IMAGE_TYPE_1D
                         : dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D ? VK_IMAGE_TYPE_2D
                                                                           : VK_IMAGE_TYPE_3D;
        info.format = vk_format;
        info.extent = (VkExtent3D){width, height, depth};
        info.mipLevels = mips;
        info.arrayLayers = layers;
        info.samples = samples;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = D3dImageUsage(bind, misc, usage);
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if ((f->flags & D3D_FORMAT_TYPELESS) || (f->flags & D3D_FORMAT_SRGB) || (bind & D3D11_BIND_RENDER_TARGET))
            info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
        if ((misc & D3D11_RESOURCE_MISC_TEXTURECUBE) && layers >= 6)
            info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D && (bind & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS)))
            info.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
        if (device->vk.vkCreateImage(device->vk.device, &info, NULL, &r->tex.image) != VK_SUCCESS) {
            destroy(r);
            return E_INVALIDARG;
        }
        if (FAILED(hr = D3dAllocImage(device, r->tex.image, &r->tex.alloc))) {
            destroy(r);
            return hr;
        }
        /* the general layout for good, and zeroed, as direct3d hands out textures. */
        VkCommandBuffer cmd = D3dInitCommands(device);
        VkImageMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = r->tex.image;
        barrier.subresourceRange = (VkImageSubresourceRange){r->tex.aspect, 0, mips, 0, layers};
        VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        device->vk.vkCmdPipelineBarrier2(cmd, &dep);
        if (!data) {
            VkImageSubresourceRange range = barrier.subresourceRange;
            if (r->tex.aspect & VK_IMAGE_ASPECT_COLOR_BIT) {
                VkClearColorValue zero = {{0}};
                if (f->block == 1)
                    device->vk.vkCmdClearColorImage(cmd, r->tex.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
            } else {
                VkClearDepthStencilValue zero = {0, 0};
                device->vk.vkCmdClearDepthStencilImage(cmd, r->tex.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1,
                                                       &range);
            }
            D3dBarrier(cmd, device);
        }
        D3dInitDone(device);
    }
    if (data && FAILED(hr = upload_texture(device, r, data))) {
        destroy(r);
        return hr;
    }
    *out = r;
    return S_OK;
}
