/*
 * IDirect3DVertexBuffer7, read from d3dim700.dll of build 26300: system
 * memory vertices a device draws from or ProcessVertices writes. windows
 * keeps them in a locked execute buffer surface; nothing of that is seen
 * but the pointer, which stays the same for the buffer's life.
 */
#include "d3d_private.h"

static const IDirect3DVertexBuffer7Vtbl vb7_vtbl;
static const IDirect3DVertexBufferVtbl vb1_vtbl;

D3DVB *D3dVbFromIface7(void *iface)
{
    IDirect3DVertexBuffer7 *i = iface;
    return i && i->lpVtbl == &vb7_vtbl ? CONTAINING_RECORD(i, D3DVB, iface7) : nullptr;
}

static D3DVB *create(D3DOBJ *d3d, const D3DVERTEXBUFFERDESC *desc, HRESULT *hr)
{
    *hr = DDERR_OUTOFMEMORY;
    D3DVB *vb = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*vb));
    if (!vb)
        return nullptr;
    vb->ref = 1;
    vb->d3d = d3d;
    vb->desc = *desc;
    vb->stride = D3dFvfSize(desc->dwFVF);
    vb->data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)vb->stride * desc->dwNumVertices);
    if (!vb->data) {
        HeapFree(GetProcessHeap(), 0, vb);
        return nullptr;
    }
    InsertHeadList(&d3d->buffers, &vb->links);
    *hr = D3D_OK;
    return vb;
}

HRESULT D3dCreateVertexBuffer7(D3DOBJ *d3d, const D3DVERTEXBUFFERDESC *desc, DWORD flags,
                               IDirect3DVertexBuffer7 **out)
{
    *out = nullptr;
    if (desc->dwNumVertices > 0xFFFF)
        return D3DERR_TOOMANYVERTICES;
    if (desc->dwCaps & D3DVBCAPS_OPTIMIZED)
        return DDERR_INVALIDPARAMS;
    /* a surface of no width, which ddraw refuses, guessed. */
    if (!desc->dwNumVertices)
        return DDERR_INVALIDPARAMS;
    HRESULT hr;
    D3DVB *vb = create(d3d, desc, &hr);
    if (vb) {
        vb->iface7.lpVtbl = &vb7_vtbl;
        *out = &vb->iface7;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE vb7_query(IDirect3DVertexBuffer7 *This, REFIID riid, void **out)
{
    D3DVB *vb = D3dVbFromIface7(This);
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    if (!vb)
        return DDERR_INVALIDOBJECT;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IDirect3DVertexBuffer7))
        return E_NOINTERFACE;
    DdEnter();
    vb->ref++;
    DdLeave(D3D_OK);
    *out = &vb->iface7;
    return D3D_OK;
}

static ULONG STDMETHODCALLTYPE vb7_add_ref(IDirect3DVertexBuffer7 *This)
{
    D3DVB *vb = D3dVbFromIface7(This);
    if (!vb)
        return 0;
    DdEnter();
    ULONG ref = (ULONG)++vb->ref;
    DdLeave(D3D_OK);
    return ref;
}

static ULONG STDMETHODCALLTYPE vb7_release(IDirect3DVertexBuffer7 *This)
{
    D3DVB *vb = D3dVbFromIface7(This);
    if (!vb)
        return 0;
    DdEnter();
    ULONG ref = (ULONG)--vb->ref;
    if (!ref) {
        RemoveEntryList(&vb->links);
        HeapFree(GetProcessHeap(), 0, vb->data);
        vb->iface7.lpVtbl = nullptr;
        vb->iface1.lpVtbl = nullptr;
        HeapFree(GetProcessHeap(), 0, vb);
    }
    DdLeave(D3D_OK);
    return ref;
}

/* locks nest without limit and always give the same memory. */
static HRESULT STDMETHODCALLTYPE vb7_lock(IDirect3DVertexBuffer7 *This, DWORD flags, LPVOID *data, LPDWORD size)
{
    D3DVB *vb = D3dVbFromIface7(This);
    if (!vb)
        return DDERR_INVALIDOBJECT;
    if (vb->optimized)
        return D3DERR_VERTEXBUFFEROPTIMIZED;
    vb->locks++;
    if (data)
        *data = vb->data;
    if (size)
        *size = vb->stride * vb->desc.dwNumVertices;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE vb7_unlock(IDirect3DVertexBuffer7 *This)
{
    D3DVB *vb = D3dVbFromIface7(This);
    if (!vb)
        return DDERR_INVALIDOBJECT;
    if (vb->locks)
        vb->locks--;
    return D3D_OK;
}

static HRESULT process(D3DVB *dst, D3DDEV *dev, DWORD op, DWORD dst_index, DWORD count, const BYTE *src,
                       DWORD src_fvf, DWORD flags)
{
    if (dst_index >= dst->desc.dwNumVertices)
        return D3D_OK;
    count = count < dst->desc.dwNumVertices - dst_index ? count : dst->desc.dwNumVertices - dst_index;
    return D3dProcess(dev, src, src_fvf, dst->data + (SIZE_T)dst_index * dst->stride, dst->desc.dwFVF, count, op, flags);
}

/* the source's vertices transformed, and lit if asked, into this buffer's layout. */
static HRESULT STDMETHODCALLTYPE vb7_process_vertices(IDirect3DVertexBuffer7 *This, DWORD op, DWORD dst_index,
                                                      DWORD count, LPDIRECT3DVERTEXBUFFER7 src_iface, DWORD src_index,
                                                      LPDIRECT3DDEVICE7 device, DWORD flags)
{
    D3DVB *dst = D3dVbFromIface7(This);
    D3DDEV *dev = D3dDevFromIface7(device);
    if (!dst || !dev)
        return DDERR_INVALIDOBJECT;
    if (dst->optimized)
        return D3DERR_VERTEXBUFFEROPTIMIZED;
    DdEnter();
    D3DVB *src = D3dVbFromIface7(src_iface);
    /* without a source only clip codes are made, which nothing keeps here. */
    if (!src || src_index >= src->desc.dwNumVertices)
        return DdLeave(D3D_OK);
    count = count < src->desc.dwNumVertices - src_index ? count : src->desc.dwNumVertices - src_index;
    HRESULT hr = process(dst, dev, op, dst_index, count, src->data + (SIZE_T)src_index * src->stride,
                         src->desc.dwFVF, flags);
    return DdLeave(hr);
}

static HRESULT STDMETHODCALLTYPE vb7_process_vertices_strided(IDirect3DVertexBuffer7 *This, DWORD op,
                                                              DWORD dst_index, DWORD count,
                                                              LPD3DDRAWPRIMITIVESTRIDEDDATA data, DWORD fvf,
                                                              LPDIRECT3DDEVICE7 device, DWORD flags)
{
    D3DVB *dst = D3dVbFromIface7(This);
    D3DDEV *dev = D3dDevFromIface7(device);
    if (!dst || !dev)
        return DDERR_INVALIDOBJECT;
    if (dst->optimized)
        return D3DERR_VERTEXBUFFEROPTIMIZED;
    if (!data)
        return DDERR_INVALIDPARAMS;
    DdEnter();
    const BYTE *packed = D3dPackStrided(dev, fvf, data, count);
    HRESULT hr = packed ? process(dst, dev, op, dst_index, count, packed, fvf, flags) : DDERR_OUTOFMEMORY;
    return DdLeave(hr);
}

static HRESULT STDMETHODCALLTYPE vb7_get_desc(IDirect3DVertexBuffer7 *This, LPD3DVERTEXBUFFERDESC desc)
{
    D3DVB *vb = D3dVbFromIface7(This);
    if (!vb)
        return DDERR_INVALIDOBJECT;
    if (!desc)
        return DDERR_INVALIDPARAMS;
    desc->dwCaps = vb->desc.dwCaps | (vb->optimized ? D3DVBCAPS_OPTIMIZED : 0);
    desc->dwFVF = vb->desc.dwFVF;
    desc->dwNumVertices = vb->desc.dwNumVertices;
    return D3D_OK;
}

/* the format checks of d3dim700's ValidateFVF. */
static BOOL valid_fvf(DWORD fvf)
{
    DWORD sets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (sets < 8 && (fvf >> (16 + 2 * sets)))
        return FALSE;
    if (fvf & (D3DFVF_RESERVED0 | D3DFVF_RESERVED2))
        return FALSE;
    if ((fvf & D3DFVF_RESERVED1) && !(fvf & 0x1E2))
        return FALSE;
    DWORD position = fvf & D3DFVF_POSITION_MASK;
    if (!position)
        return FALSE;
    return !(position == D3DFVF_XYZRHW && (fvf & D3DFVF_NORMAL));
}

static HRESULT STDMETHODCALLTYPE vb7_optimize(IDirect3DVertexBuffer7 *This, LPDIRECT3DDEVICE7 device, DWORD flags)
{
    D3DVB *vb = D3dVbFromIface7(This);
    if (!vb)
        return E_INVALIDARG;
    if (!D3dDevFromIface7(device))
        return DDERR_INVALIDOBJECT;
    if (flags || !valid_fvf(vb->desc.dwFVF))
        return E_INVALIDARG;
    if (vb->optimized)
        return D3DERR_VERTEXBUFFEROPTIMIZED;
    if (vb->locks)
        return D3DERR_VERTEXBUFFERLOCKED;
    vb->optimized = TRUE;
    return D3D_OK;
}

static const IDirect3DVertexBuffer7Vtbl vb7_vtbl = {
    vb7_query,
    vb7_add_ref,
    vb7_release,
    vb7_lock,
    vb7_unlock,
    vb7_process_vertices,
    vb7_get_desc,
    vb7_optimize,
    vb7_process_vertices_strided,
};

/* ------------------------------------------------------------------ the first interface's, d3dim.dll's */

D3DVB *D3dVbFromIface1(void *iface)
{
    IDirect3DVertexBuffer *i = iface;
    return i && i->lpVtbl == &vb1_vtbl ? CONTAINING_RECORD(i, D3DVB, iface1) : nullptr;
}

/* only the low twelve bits of a format, a stride made of them. */
HRESULT D3dCreateVertexBuffer1(D3DOBJ *d3d, const D3DVERTEXBUFFERDESC *desc, DWORD flags,
                               IDirect3DVertexBuffer **out)
{
    *out = nullptr;
    if (desc->dwNumVertices > 0xFFFF)
        return D3DERR_TOOMANYVERTICES;
    if (desc->dwCaps & D3DVBCAPS_OPTIMIZED)
        return DDERR_INVALIDPARAMS;
    if ((desc->dwFVF & 0xFFFFF000) || !D3dFvfSize(desc->dwFVF) || (desc->dwFVF & D3DFVF_POSITION_MASK) > D3DFVF_XYZRHW)
        return D3DERR_INVALIDVERTEXFORMAT;
    if (!desc->dwNumVertices)
        return DDERR_INVALIDPARAMS;
    HRESULT hr;
    D3DVB *vb = create(d3d, desc, &hr);
    /* the seventh's methods serve it too, through a vtable no caller sees. */
    if (vb) {
        vb->iface7.lpVtbl = &vb7_vtbl;
        vb->iface1.lpVtbl = &vb1_vtbl;
        *out = &vb->iface1;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE vb1_query(IDirect3DVertexBuffer *This, REFIID riid, void **out)
{
    D3DVB *vb = D3dVbFromIface1(This);
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    if (!vb)
        return DDERR_INVALIDOBJECT;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IDirect3DVertexBuffer))
        return E_NOINTERFACE;
    DdEnter();
    vb->ref++;
    DdLeave(D3D_OK);
    *out = &vb->iface1;
    return D3D_OK;
}

static ULONG STDMETHODCALLTYPE vb1_add_ref(IDirect3DVertexBuffer *This)
{
    D3DVB *vb = D3dVbFromIface1(This);
    return vb ? vb7_add_ref(&vb->iface7) : 0;
}

static ULONG STDMETHODCALLTYPE vb1_release(IDirect3DVertexBuffer *This)
{
    D3DVB *vb = D3dVbFromIface1(This);
    return vb ? vb7_release(&vb->iface7) : 0;
}

static HRESULT STDMETHODCALLTYPE vb1_lock(IDirect3DVertexBuffer *This, DWORD flags, LPVOID *data, LPDWORD size)
{
    D3DVB *vb = D3dVbFromIface1(This);
    return vb ? vb7_lock(&vb->iface7, flags, data, size) : DDERR_INVALIDOBJECT;
}

static HRESULT STDMETHODCALLTYPE vb1_unlock(IDirect3DVertexBuffer *This)
{
    D3DVB *vb = D3dVbFromIface1(This);
    return vb ? vb7_unlock(&vb->iface7) : DDERR_INVALIDOBJECT;
}

/* through the device's current viewport, lights and material; transforming always. */
static HRESULT STDMETHODCALLTYPE vb1_process_vertices(IDirect3DVertexBuffer *This, DWORD op, DWORD dst_index,
                                                      DWORD count, LPDIRECT3DVERTEXBUFFER src_iface, DWORD src_index,
                                                      LPDIRECT3DDEVICE3 device, DWORD flags)
{
    D3DVB *dst = D3dVbFromIface1(This), *src = D3dVbFromIface1(src_iface);
    D3DDEV *dev = D3dDevFromLegacy(device);
    if (!dst || !src || !dev)
        return DDERR_INVALIDOBJECT;
    DdEnter();
    HRESULT hr = D3dLegacyDownloadCurrent(dev);
    if (FAILED(hr))
        return DdLeave(hr);
    if (src_index >= src->desc.dwNumVertices || dst_index >= dst->desc.dwNumVertices)
        return DdLeave(D3D_OK);
    count = count < src->desc.dwNumVertices - src_index ? count : src->desc.dwNumVertices - src_index;
    count = count < dst->desc.dwNumVertices - dst_index ? count : dst->desc.dwNumVertices - dst_index;
    hr = D3dLegacyProcess(dev, src->data + (SIZE_T)src_index * src->stride, src->desc.dwFVF,
                          dst->data + (SIZE_T)dst_index * dst->stride, dst->desc.dwFVF, count, op);
    return DdLeave(hr);
}

static HRESULT STDMETHODCALLTYPE vb1_get_desc(IDirect3DVertexBuffer *This, LPD3DVERTEXBUFFERDESC desc)
{
    D3DVB *vb = D3dVbFromIface1(This);
    return vb ? vb7_get_desc(&vb->iface7, desc) : DDERR_INVALIDOBJECT;
}

static HRESULT STDMETHODCALLTYPE vb1_optimize(IDirect3DVertexBuffer *This, LPDIRECT3DDEVICE3 device, DWORD flags)
{
    D3DVB *vb = D3dVbFromIface1(This);
    if (!vb)
        return DDERR_INVALIDPARAMS;
    if (!D3dDevFromLegacy(device))
        return DDERR_INVALIDOBJECT;
    if (flags)
        return DDERR_INVALIDPARAMS;
    if (vb->optimized)
        return D3DERR_VERTEXBUFFEROPTIMIZED;
    if (vb->locks)
        return D3DERR_VERTEXBUFFERLOCKED;
    vb->optimized = TRUE;
    return D3D_OK;
}

static const IDirect3DVertexBufferVtbl vb1_vtbl = {
    vb1_query,  vb1_add_ref, vb1_release, vb1_lock, vb1_unlock, vb1_process_vertices, vb1_get_desc, vb1_optimize,
};
