/*
 * the software device: what D3D11CreateDevice gives for warp when the machine has no gpu, as on a mac, where the
 * other driver types fail. windows always has warp, and programs such as Chromium's software compositor count on
 * it to put cpu drawn frames on screen through a composition swap chain.
 *
 * it keeps 32-bit textures in memory and does what such programs ask of them: Map, copies and updates, and
 * composition swap chains whose frames dcomp.dll composes with the cpu. it cannot draw or run shaders; those calls
 * fail with E_NOTIMPL, or do nothing when they return nothing.
 */
#include "d3d11_private.h"

#define MAX_LISTENERS 4

typedef struct WarpDevice {
    ID3D11Device iface;
    ID3D11DeviceContext context;
    IMimeDXGIPresenter presenter;
    LONG refs;
    UINT flags;
    D3D_FEATURE_LEVEL level;
    CRITICAL_SECTION lock;
    MimePrivateData data;
} WarpDevice;

typedef struct WarpTexture {
    ID3D11Texture2D iface;
    IMimeSoftwareTexture software;
    LONG refs;
    WarpDevice *device;
    D3D11_TEXTURE2D_DESC desc;
    UINT pitch;
    BYTE *bits;
    MimePrivateData data;
} WarpTexture;

typedef struct WarpSwapChain {
    IDXGISwapChain1 iface;
    IMimeDXGICompositionSource source;
    LONG refs;
    WarpDevice *device;
    IDXGIFactory *factory;
    DXGI_SWAP_CHAIN_DESC1 desc;
    WarpTexture *back;
    /* the last presented frame, under the device lock. */
    WarpTexture *front;
    UINT64 serial;
    UINT presents;
    DXGI_RGBA background;
    struct {
        MIME_COMPOSITION_LISTENER function;
        void *context;
    } listeners[MAX_LISTENERS];
    MimePrivateData data;
} WarpSwapChain;

/*
 * the methods the device leaves out. calling it through another signature is how com stubs work on these
 * processors: the arguments are ignored and the result is E_NOTIMPL, or ignored by a method returning nothing.
 */
static HRESULT STDMETHODCALLTYPE not_implemented(void) { return E_NOTIMPL; }

static void fill_vtbl(void *vtbl, SIZE_T size)
{
    void **slots = vtbl;
    for (SIZE_T i = 0; i < size / sizeof(void *); i++)
        if (!slots[i])
            slots[i] = (void *)not_implemented;
}

static WarpDevice *device_impl(ID3D11Device *iface) { return CONTAINING_RECORD(iface, WarpDevice, iface); }
static WarpDevice *context_impl(ID3D11DeviceContext *iface) { return CONTAINING_RECORD(iface, WarpDevice, context); }
static WarpDevice *presenter_impl(IMimeDXGIPresenter *iface)
{
    return CONTAINING_RECORD(iface, WarpDevice, presenter);
}
static WarpTexture *texture_impl(ID3D11Texture2D *iface) { return CONTAINING_RECORD(iface, WarpTexture, iface); }
static WarpTexture *software_impl(IMimeSoftwareTexture *iface)
{
    return CONTAINING_RECORD(iface, WarpTexture, software);
}
static WarpSwapChain *chain_impl(IDXGISwapChain1 *iface) { return CONTAINING_RECORD(iface, WarpSwapChain, iface); }
static WarpSwapChain *source_impl(IMimeDXGICompositionSource *iface)
{
    return CONTAINING_RECORD(iface, WarpSwapChain, source);
}

static ID3D11DeviceVtbl device_vtbl;
static ID3D11DeviceContextVtbl context_vtbl;
static ID3D11Texture2DVtbl texture_vtbl;
static IDXGISwapChain1Vtbl chain_vtbl;

/* ------------------------------------------------------------------ formats */

static BOOL supported_format(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return TRUE;
    default:
        return FALSE;
    }
}

/* ------------------------------------------------------------------ textures */

static HRESULT STDMETHODCALLTYPE texture_query(ID3D11Texture2D *iface, REFIID riid, void **out)
{
    WarpTexture *t = texture_impl(iface);
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild) ||
        IsEqualGUID(riid, &IID_ID3D11Resource) || IsEqualGUID(riid, &IID_ID3D11Texture2D)) {
        *out = iface;
    } else if (IsEqualGUID(riid, &IID_IMimeSoftwareTexture)) {
        *out = &t->software;
    } else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&t->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE texture_add_ref(ID3D11Texture2D *iface)
{
    return InterlockedIncrement(&texture_impl(iface)->refs);
}

static ULONG STDMETHODCALLTYPE texture_release(ID3D11Texture2D *iface)
{
    WarpTexture *t = texture_impl(iface);
    ULONG refs = InterlockedDecrement(&t->refs);
    if (refs)
        return refs;
    WarpDevice *device = t->device;
    MimePrivateClear(&t->data);
    VirtualFree(t->bits, 0, MEM_RELEASE);
    HeapFree(GetProcessHeap(), 0, t);
    device->iface.lpVtbl->Release(&device->iface);
    return 0;
}

static void STDMETHODCALLTYPE texture_get_device(ID3D11Texture2D *iface, ID3D11Device **device)
{
    WarpDevice *d = texture_impl(iface)->device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = &d->iface;
}

static HRESULT STDMETHODCALLTYPE texture_get_private_data(ID3D11Texture2D *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&texture_impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE texture_set_private_data(ID3D11Texture2D *iface, REFGUID guid, UINT size,
                                                          const void *data)
{
    return MimePrivateSet(&texture_impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE texture_set_private_data_interface(ID3D11Texture2D *iface, REFGUID guid,
                                                                    const IUnknown *data)
{
    return MimePrivateSetInterface(&texture_impl(iface)->data, guid, data);
}

static void STDMETHODCALLTYPE texture_get_type(ID3D11Texture2D *iface, D3D11_RESOURCE_DIMENSION *dimension)
{
    *dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
}

static void STDMETHODCALLTYPE texture_set_eviction_priority(ID3D11Texture2D *iface, UINT priority) {}

static UINT STDMETHODCALLTYPE texture_get_eviction_priority(ID3D11Texture2D *iface)
{
    return DXGI_RESOURCE_PRIORITY_NORMAL;
}

static void STDMETHODCALLTYPE texture_get_desc(ID3D11Texture2D *iface, D3D11_TEXTURE2D_DESC *desc)
{
    *desc = texture_impl(iface)->desc;
}

static ID3D11Texture2DVtbl texture_vtbl = {
    texture_query,
    texture_add_ref,
    texture_release,
    texture_get_device,
    texture_get_private_data,
    texture_set_private_data,
    texture_set_private_data_interface,
    texture_get_type,
    texture_set_eviction_priority,
    texture_get_eviction_priority,
    texture_get_desc,
};

static HRESULT STDMETHODCALLTYPE software_query(IMimeSoftwareTexture *iface, REFIID riid, void **out)
{
    return texture_query(&software_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE software_add_ref(IMimeSoftwareTexture *iface)
{
    return texture_add_ref(&software_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE software_release(IMimeSoftwareTexture *iface)
{
    return texture_release(&software_impl(iface)->iface);
}

static HRESULT STDMETHODCALLTYPE software_lock(IMimeSoftwareTexture *iface, const BYTE **bits, UINT *pitch)
{
    WarpTexture *t = software_impl(iface);
    EnterCriticalSection(&t->device->lock);
    *bits = t->bits;
    *pitch = t->pitch;
    return S_OK;
}

static void STDMETHODCALLTYPE software_unlock(IMimeSoftwareTexture *iface)
{
    LeaveCriticalSection(&software_impl(iface)->device->lock);
}

static const IMimeSoftwareTextureVtbl software_vtbl = {
    software_query, software_add_ref, software_release, software_lock, software_unlock,
};

static HRESULT create_texture(WarpDevice *device, const D3D11_TEXTURE2D_DESC *desc, WarpTexture **out)
{
    *out = nullptr;
    if (!desc->Width || !desc->Height || desc->Width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        desc->Height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        return E_INVALIDARG;
    if (desc->MipLevels > 1 || desc->ArraySize > 1 || desc->SampleDesc.Count > 1 || !supported_format(desc->Format)) {
        D3dLog("d3d11: the software device has no %ux%u texture of format %u, %u levels, %u layers, %u samples",
               desc->Width, desc->Height, desc->Format, desc->MipLevels, desc->ArraySize, desc->SampleDesc.Count);
        return E_NOTIMPL;
    }
    WarpTexture *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    t->pitch = desc->Width * 4;
    t->bits = VirtualAlloc(nullptr, (SIZE_T)t->pitch * desc->Height, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!t->bits) {
        HeapFree(GetProcessHeap(), 0, t);
        return E_OUTOFMEMORY;
    }
    t->iface.lpVtbl = &texture_vtbl;
    t->software.lpVtbl = &software_vtbl;
    t->refs = 1;
    t->device = device;
    t->desc = *desc;
    t->desc.MipLevels = 1;
    t->desc.ArraySize = 1;
    t->desc.SampleDesc.Count = 1;
    t->desc.SampleDesc.Quality = 0;
    device->iface.lpVtbl->AddRef(&device->iface);
    *out = t;
    return S_OK;
}

static WarpTexture *as_texture(ID3D11Resource *resource)
{
    if (!resource || resource->lpVtbl != (const void *)&texture_vtbl)
        return nullptr;
    return texture_impl((ID3D11Texture2D *)resource);
}

/* ------------------------------------------------------------------ the immediate context */

static HRESULT STDMETHODCALLTYPE context_query(ID3D11DeviceContext *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild) ||
        IsEqualGUID(riid, &IID_ID3D11DeviceContext)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

/* the immediate context lives as long as its device. */
static ULONG STDMETHODCALLTYPE context_add_ref(ID3D11DeviceContext *iface)
{
    WarpDevice *d = context_impl(iface);
    return d->iface.lpVtbl->AddRef(&d->iface);
}

static ULONG STDMETHODCALLTYPE context_release(ID3D11DeviceContext *iface)
{
    WarpDevice *d = context_impl(iface);
    return d->iface.lpVtbl->Release(&d->iface);
}

static void STDMETHODCALLTYPE context_get_device(ID3D11DeviceContext *iface, ID3D11Device **device)
{
    WarpDevice *d = context_impl(iface);
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = &d->iface;
}

static HRESULT STDMETHODCALLTYPE context_get_private_data(ID3D11DeviceContext *iface, REFGUID guid, UINT *size,
                                                          void *data)
{
    return MimePrivateGet(&context_impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE context_set_private_data(ID3D11DeviceContext *iface, REFGUID guid, UINT size,
                                                          const void *data)
{
    return MimePrivateSet(&context_impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE context_set_private_data_interface(ID3D11DeviceContext *iface, REFGUID guid,
                                                                    const IUnknown *data)
{
    return MimePrivateSetInterface(&context_impl(iface)->data, guid, data);
}

static HRESULT STDMETHODCALLTYPE context_map(ID3D11DeviceContext *iface, ID3D11Resource *resource, UINT subresource,
                                             D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped)
{
    WarpTexture *t = as_texture(resource);
    if (!t || subresource || !mapped)
        return E_INVALIDARG;
    mapped->pData = t->bits;
    mapped->RowPitch = t->pitch;
    mapped->DepthPitch = t->pitch * t->desc.Height;
    return S_OK;
}

static void STDMETHODCALLTYPE context_unmap(ID3D11DeviceContext *iface, ID3D11Resource *resource, UINT subresource) {}

static void copy_rect(WarpTexture *dst, UINT x, UINT y, WarpTexture *src, const D3D11_BOX *box)
{
    UINT left = box ? box->left : 0, top = box ? box->top : 0;
    UINT right = box ? box->right : src->desc.Width, bottom = box ? box->bottom : src->desc.Height;
    if (right > src->desc.Width)
        right = src->desc.Width;
    if (bottom > src->desc.Height)
        bottom = src->desc.Height;
    if (left >= right || top >= bottom || x >= dst->desc.Width || y >= dst->desc.Height)
        return;
    UINT width = D3D_MIN(right - left, dst->desc.Width - x), height = D3D_MIN(bottom - top, dst->desc.Height - y);
    EnterCriticalSection(&dst->device->lock);
    for (UINT row = 0; row < height; row++)
        memmove(dst->bits + (SIZE_T)(y + row) * dst->pitch + x * 4,
                src->bits + (SIZE_T)(top + row) * src->pitch + left * 4, (SIZE_T)width * 4);
    LeaveCriticalSection(&dst->device->lock);
}

static void STDMETHODCALLTYPE context_copy_subresource_region(ID3D11DeviceContext *iface, ID3D11Resource *dst,
                                                              UINT dst_sub, UINT x, UINT y, UINT z,
                                                              ID3D11Resource *src, UINT src_sub, const D3D11_BOX *box)
{
    WarpTexture *d = as_texture(dst), *s = as_texture(src);
    if (d && s && !dst_sub && !src_sub && !z)
        copy_rect(d, x, y, s, box);
}

static void STDMETHODCALLTYPE context_copy_resource(ID3D11DeviceContext *iface, ID3D11Resource *dst,
                                                    ID3D11Resource *src)
{
    WarpTexture *d = as_texture(dst), *s = as_texture(src);
    if (d && s)
        copy_rect(d, 0, 0, s, nullptr);
}

static void STDMETHODCALLTYPE context_update_subresource(ID3D11DeviceContext *iface, ID3D11Resource *dst, UINT sub,
                                                         const D3D11_BOX *box, const void *data, UINT row_pitch,
                                                         UINT depth_pitch)
{
    WarpTexture *t = as_texture(dst);
    if (!t || sub || !data)
        return;
    UINT left = box ? box->left : 0, top = box ? box->top : 0;
    UINT right = box ? D3D_MIN(box->right, t->desc.Width) : t->desc.Width;
    UINT bottom = box ? D3D_MIN(box->bottom, t->desc.Height) : t->desc.Height;
    if (left >= right || top >= bottom)
        return;
    EnterCriticalSection(&t->device->lock);
    for (UINT row = top; row < bottom; row++)
        memcpy(t->bits + (SIZE_T)row * t->pitch + left * 4, (const BYTE *)data + (SIZE_T)(row - top) * row_pitch,
               (SIZE_T)(right - left) * 4);
    LeaveCriticalSection(&t->device->lock);
}

static void STDMETHODCALLTYPE context_nothing(ID3D11DeviceContext *iface) {}

static D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE context_get_type(ID3D11DeviceContext *iface)
{
    return D3D11_DEVICE_CONTEXT_IMMEDIATE;
}

static UINT STDMETHODCALLTYPE context_get_flags(ID3D11DeviceContext *iface) { return 0; }

static ID3D11DeviceContextVtbl context_vtbl = {
    .QueryInterface = context_query,
    .AddRef = context_add_ref,
    .Release = context_release,
    .GetDevice = context_get_device,
    .GetPrivateData = context_get_private_data,
    .SetPrivateData = context_set_private_data,
    .SetPrivateDataInterface = context_set_private_data_interface,
    .Map = context_map,
    .Unmap = context_unmap,
    .CopySubresourceRegion = context_copy_subresource_region,
    .CopyResource = context_copy_resource,
    .UpdateSubresource = context_update_subresource,
    .Flush = context_nothing,
    .ClearState = context_nothing,
    .GetType = context_get_type,
    .GetContextFlags = context_get_flags,
};

/* ------------------------------------------------------------------ composition swap chains */

static HRESULT STDMETHODCALLTYPE chain_query(IDXGISwapChain1 *iface, REFIID riid, void **out)
{
    WarpSwapChain *sc = chain_impl(iface);
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDXGIObject) ||
        IsEqualGUID(riid, &IID_IDXGIDeviceSubObject) || IsEqualGUID(riid, &IID_IDXGISwapChain) ||
        IsEqualGUID(riid, &IID_IDXGISwapChain1)) {
        *out = iface;
    } else if (IsEqualGUID(riid, &IID_IMimeDXGICompositionSource)) {
        *out = &sc->source;
    } else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&sc->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE chain_add_ref(IDXGISwapChain1 *iface)
{
    return InterlockedIncrement(&chain_impl(iface)->refs);
}

static ULONG STDMETHODCALLTYPE chain_release(IDXGISwapChain1 *iface)
{
    WarpSwapChain *sc = chain_impl(iface);
    ULONG refs = InterlockedDecrement(&sc->refs);
    if (refs)
        return refs;
    WarpDevice *device = sc->device;
    if (sc->back)
        sc->back->iface.lpVtbl->Release(&sc->back->iface);
    if (sc->front)
        sc->front->iface.lpVtbl->Release(&sc->front->iface);
    MimePrivateClear(&sc->data);
    sc->factory->lpVtbl->Release(sc->factory);
    HeapFree(GetProcessHeap(), 0, sc);
    device->iface.lpVtbl->Release(&device->iface);
    return 0;
}

static HRESULT STDMETHODCALLTYPE chain_set_private_data(IDXGISwapChain1 *iface, REFGUID guid, UINT size,
                                                        const void *data)
{
    return MimePrivateSet(&chain_impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE chain_set_private_data_interface(IDXGISwapChain1 *iface, REFGUID guid,
                                                                  const IUnknown *unknown)
{
    return MimePrivateSetInterface(&chain_impl(iface)->data, guid, unknown);
}

static HRESULT STDMETHODCALLTYPE chain_get_private_data(IDXGISwapChain1 *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&chain_impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE chain_get_parent(IDXGISwapChain1 *iface, REFIID riid, void **parent)
{
    IDXGIFactory *factory = chain_impl(iface)->factory;
    return factory->lpVtbl->QueryInterface(factory, riid, parent);
}

static HRESULT STDMETHODCALLTYPE chain_get_device(IDXGISwapChain1 *iface, REFIID riid, void **device)
{
    WarpDevice *d = chain_impl(iface)->device;
    return d->iface.lpVtbl->QueryInterface(&d->iface, riid, device);
}

/* the back buffer becomes the frame dcomp.dll shows; a new size gets a new texture, so it sees the change. */
static HRESULT present(WarpSwapChain *sc, UINT flags)
{
    if (flags & DXGI_PRESENT_TEST)
        return S_OK;
    WarpTexture *back = sc->back;
    EnterCriticalSection(&sc->device->lock);
    WarpTexture *front = sc->front;
    if (front && (front->desc.Width != back->desc.Width || front->desc.Height != back->desc.Height)) {
        sc->front = nullptr;
        front->iface.lpVtbl->Release(&front->iface);
        front = nullptr;
    }
    HRESULT hr = S_OK;
    if (!front)
        hr = create_texture(sc->device, &back->desc, &front);
    if (SUCCEEDED(hr)) {
        memcpy(front->bits, back->bits, (SIZE_T)back->pitch * back->desc.Height);
        sc->front = front;
        sc->serial++;
        sc->presents++;
    }
    LeaveCriticalSection(&sc->device->lock);
    if (FAILED(hr))
        return hr;
    struct {
        MIME_COMPOSITION_LISTENER function;
        void *context;
    } listeners[MAX_LISTENERS];
    EnterCriticalSection(&sc->device->lock);
    memcpy(listeners, sc->listeners, sizeof(listeners));
    LeaveCriticalSection(&sc->device->lock);
    for (UINT i = 0; i < MAX_LISTENERS; i++)
        if (listeners[i].function)
            listeners[i].function(listeners[i].context, (IUnknown *)&sc->iface);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE chain_present(IDXGISwapChain1 *iface, UINT interval, UINT flags)
{
    return present(chain_impl(iface), flags);
}

static HRESULT STDMETHODCALLTYPE chain_present1(IDXGISwapChain1 *iface, UINT interval, UINT flags,
                                                const DXGI_PRESENT_PARAMETERS *parameters)
{
    return present(chain_impl(iface), flags);
}

/* like flip model swap chains of direct3d 11, the program sees one back buffer. */
static HRESULT STDMETHODCALLTYPE chain_get_buffer(IDXGISwapChain1 *iface, UINT index, REFIID riid, void **surface)
{
    WarpSwapChain *sc = chain_impl(iface);
    if (!surface)
        return DXGI_ERROR_INVALID_CALL;
    *surface = nullptr;
    if (index >= sc->desc.BufferCount)
        return DXGI_ERROR_INVALID_CALL;
    return texture_query(&sc->back->iface, riid, surface);
}

static HRESULT STDMETHODCALLTYPE chain_get_fullscreen_state(IDXGISwapChain1 *iface, BOOL *fullscreen,
                                                            IDXGIOutput **target)
{
    if (fullscreen)
        *fullscreen = FALSE;
    if (target)
        *target = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE chain_get_desc(IDXGISwapChain1 *iface, DXGI_SWAP_CHAIN_DESC *desc)
{
    WarpSwapChain *sc = chain_impl(iface);
    if (!desc)
        return DXGI_ERROR_INVALID_CALL;
    memset(desc, 0, sizeof(*desc));
    desc->BufferDesc.Width = sc->desc.Width;
    desc->BufferDesc.Height = sc->desc.Height;
    desc->BufferDesc.Format = sc->desc.Format;
    desc->SampleDesc = sc->desc.SampleDesc;
    desc->BufferUsage = sc->desc.BufferUsage;
    desc->BufferCount = sc->desc.BufferCount;
    desc->Windowed = TRUE;
    desc->SwapEffect = sc->desc.SwapEffect;
    desc->Flags = sc->desc.Flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE chain_get_desc1(IDXGISwapChain1 *iface, DXGI_SWAP_CHAIN_DESC1 *desc)
{
    if (!desc)
        return DXGI_ERROR_INVALID_CALL;
    *desc = chain_impl(iface)->desc;
    return S_OK;
}

static HRESULT make_back(WarpSwapChain *sc)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = sc->desc.Width;
    desc.Height = sc->desc.Height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = sc->desc.Format;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    WarpTexture *back;
    HRESULT hr = create_texture(sc->device, &desc, &back);
    if (FAILED(hr))
        return hr;
    if (sc->back)
        sc->back->iface.lpVtbl->Release(&sc->back->iface);
    sc->back = back;
    return S_OK;
}

/* a composition swap chain has no window, so a zero size keeps the current one. */
static HRESULT STDMETHODCALLTYPE chain_resize_buffers(IDXGISwapChain1 *iface, UINT count, UINT width, UINT height,
                                                      DXGI_FORMAT format, UINT flags)
{
    WarpSwapChain *sc = chain_impl(iface);
    if (format != DXGI_FORMAT_UNKNOWN && !supported_format(format))
        return DXGI_ERROR_INVALID_CALL;
    if (count == 1 || count > DXGI_MAX_SWAP_CHAIN_BUFFERS)
        return DXGI_ERROR_INVALID_CALL;
    DXGI_SWAP_CHAIN_DESC1 saved = sc->desc;
    if (count)
        sc->desc.BufferCount = count;
    if (width)
        sc->desc.Width = width;
    if (height)
        sc->desc.Height = height;
    if (format != DXGI_FORMAT_UNKNOWN)
        sc->desc.Format = format;
    sc->desc.Flags = flags;
    HRESULT hr = make_back(sc);
    if (FAILED(hr))
        sc->desc = saved;
    return hr;
}

static HRESULT STDMETHODCALLTYPE chain_get_last_present_count(IDXGISwapChain1 *iface, UINT *count)
{
    if (!count)
        return DXGI_ERROR_INVALID_CALL;
    *count = chain_impl(iface)->presents;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE chain_get_hwnd(IDXGISwapChain1 *iface, HWND *hwnd)
{
    if (hwnd)
        *hwnd = nullptr;
    return DXGI_ERROR_INVALID_CALL;
}

static BOOL STDMETHODCALLTYPE chain_is_temporary_mono_supported(IDXGISwapChain1 *iface) { return FALSE; }

static HRESULT STDMETHODCALLTYPE chain_get_restrict_to_output(IDXGISwapChain1 *iface, IDXGIOutput **output)
{
    if (output)
        *output = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE chain_set_background_color(IDXGISwapChain1 *iface, const DXGI_RGBA *color)
{
    if (!color)
        return DXGI_ERROR_INVALID_CALL;
    chain_impl(iface)->background = *color;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE chain_get_background_color(IDXGISwapChain1 *iface, DXGI_RGBA *color)
{
    if (!color)
        return DXGI_ERROR_INVALID_CALL;
    *color = chain_impl(iface)->background;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE chain_get_rotation(IDXGISwapChain1 *iface, DXGI_MODE_ROTATION *rotation)
{
    if (!rotation)
        return DXGI_ERROR_INVALID_CALL;
    *rotation = DXGI_MODE_ROTATION_IDENTITY;
    return S_OK;
}

static IDXGISwapChain1Vtbl chain_vtbl = {
    .QueryInterface = chain_query,
    .AddRef = chain_add_ref,
    .Release = chain_release,
    .SetPrivateData = chain_set_private_data,
    .SetPrivateDataInterface = chain_set_private_data_interface,
    .GetPrivateData = chain_get_private_data,
    .GetParent = chain_get_parent,
    .GetDevice = chain_get_device,
    .Present = chain_present,
    .GetBuffer = chain_get_buffer,
    .GetFullscreenState = chain_get_fullscreen_state,
    .GetDesc = chain_get_desc,
    .ResizeBuffers = chain_resize_buffers,
    .GetLastPresentCount = chain_get_last_present_count,
    .GetDesc1 = chain_get_desc1,
    .GetHwnd = chain_get_hwnd,
    .Present1 = chain_present1,
    .IsTemporaryMonoSupported = chain_is_temporary_mono_supported,
    .GetRestrictToOutput = chain_get_restrict_to_output,
    .SetBackgroundColor = chain_set_background_color,
    .GetBackgroundColor = chain_get_background_color,
    .GetRotation = chain_get_rotation,
};

static HRESULT STDMETHODCALLTYPE source_query(IMimeDXGICompositionSource *iface, REFIID riid, void **out)
{
    return chain_query(&source_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE source_add_ref(IMimeDXGICompositionSource *iface)
{
    return chain_add_ref(&source_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE source_release(IMimeDXGICompositionSource *iface)
{
    return chain_release(&source_impl(iface)->iface);
}

/* S_FALSE until the first Present. */
static HRESULT STDMETHODCALLTYPE source_get_frame(IMimeDXGICompositionSource *iface, MIME_COMPOSITION_FRAME *frame)
{
    WarpSwapChain *sc = source_impl(iface);
    if (!frame)
        return E_INVALIDARG;
    memset(frame, 0, sizeof(*frame));
    EnterCriticalSection(&sc->device->lock);
    WarpTexture *front = sc->front;
    if (front) {
        front->iface.lpVtbl->AddRef(&front->iface);
        frame->Texture = (IUnknown *)&front->iface;
        frame->Width = front->desc.Width;
        frame->Height = front->desc.Height;
    }
    frame->Serial = sc->serial;
    LeaveCriticalSection(&sc->device->lock);
    if (!front)
        return S_FALSE;
    frame->Source.right = (LONG)frame->Width;
    frame->Source.bottom = (LONG)frame->Height;
    switch (sc->desc.Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        frame->ViewFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        frame->ViewFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    default:
        frame->ViewFormat = sc->desc.Format;
        break;
    }
    frame->AlphaMode = sc->desc.AlphaMode;
    frame->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    frame->Matrix._11 = frame->Matrix._22 = 1.0f;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE source_add_listener(IMimeDXGICompositionSource *iface,
                                                     MIME_COMPOSITION_LISTENER function, void *context)
{
    WarpSwapChain *sc = source_impl(iface);
    HRESULT hr = E_OUTOFMEMORY;
    EnterCriticalSection(&sc->device->lock);
    for (UINT i = 0; i < MAX_LISTENERS && FAILED(hr); i++)
        if (!sc->listeners[i].function) {
            sc->listeners[i].function = function;
            sc->listeners[i].context = context;
            hr = S_OK;
        }
    LeaveCriticalSection(&sc->device->lock);
    return hr;
}

static void STDMETHODCALLTYPE source_remove_listener(IMimeDXGICompositionSource *iface, void *context)
{
    WarpSwapChain *sc = source_impl(iface);
    EnterCriticalSection(&sc->device->lock);
    for (UINT i = 0; i < MAX_LISTENERS; i++)
        if (sc->listeners[i].function && sc->listeners[i].context == context)
            memset(&sc->listeners[i], 0, sizeof(sc->listeners[i]));
    LeaveCriticalSection(&sc->device->lock);
}

static const IMimeDXGICompositionSourceVtbl source_vtbl = {
    source_query, source_add_ref, source_release, source_get_frame, source_add_listener, source_remove_listener,
};

/* ------------------------------------------------------------------ the device */

static HRESULT STDMETHODCALLTYPE device_query(ID3D11Device *iface, REFIID riid, void **out)
{
    WarpDevice *d = device_impl(iface);
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11Device)) {
        *out = iface;
    } else if (IsEqualGUID(riid, &IID_IMimeDXGIPresenter)) {
        *out = &d->presenter;
    } else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&d->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE device_add_ref(ID3D11Device *iface)
{
    return InterlockedIncrement(&device_impl(iface)->refs);
}

static ULONG STDMETHODCALLTYPE device_release(ID3D11Device *iface)
{
    WarpDevice *d = device_impl(iface);
    ULONG refs = InterlockedDecrement(&d->refs);
    if (refs)
        return refs;
    MimePrivateClear(&d->data);
    DeleteCriticalSection(&d->lock);
    HeapFree(GetProcessHeap(), 0, d);
    return 0;
}

static HRESULT STDMETHODCALLTYPE device_create_texture2d(ID3D11Device *iface, const D3D11_TEXTURE2D_DESC *desc,
                                                         const D3D11_SUBRESOURCE_DATA *data, ID3D11Texture2D **out)
{
    if (!desc)
        return E_INVALIDARG;
    WarpTexture *t;
    HRESULT hr = create_texture(device_impl(iface), desc, &t);
    if (FAILED(hr))
        return hr;
    if (data && data->pSysMem)
        for (UINT row = 0; row < desc->Height; row++)
            memcpy(t->bits + (SIZE_T)row * t->pitch, (const BYTE *)data->pSysMem + (SIZE_T)row * data->SysMemPitch,
                   t->pitch);
    if (!out) {
        t->iface.lpVtbl->Release(&t->iface);
        return S_FALSE;
    }
    *out = &t->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_check_format_support(ID3D11Device *iface, DXGI_FORMAT format, UINT *support)
{
    if (!support)
        return E_INVALIDARG;
    *support = supported_format(format) ? D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_CPU_LOCKABLE |
                                              D3D11_FORMAT_SUPPORT_DISPLAY
                                        : 0;
    return *support ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE device_get_private_data(ID3D11Device *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&device_impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE device_set_private_data(ID3D11Device *iface, REFGUID guid, UINT size,
                                                         const void *data)
{
    return MimePrivateSet(&device_impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE device_set_private_data_interface(ID3D11Device *iface, REFGUID guid,
                                                                   const IUnknown *data)
{
    return MimePrivateSetInterface(&device_impl(iface)->data, guid, data);
}

static D3D_FEATURE_LEVEL STDMETHODCALLTYPE device_get_feature_level(ID3D11Device *iface)
{
    return device_impl(iface)->level;
}

static UINT STDMETHODCALLTYPE device_get_creation_flags(ID3D11Device *iface) { return device_impl(iface)->flags; }

static HRESULT STDMETHODCALLTYPE device_get_removed_reason(ID3D11Device *iface) { return S_OK; }

static void STDMETHODCALLTYPE device_get_immediate_context(ID3D11Device *iface, ID3D11DeviceContext **context)
{
    WarpDevice *d = device_impl(iface);
    InterlockedIncrement(&d->refs);
    *context = &d->context;
}

static HRESULT STDMETHODCALLTYPE device_set_exception_mode(ID3D11Device *iface, UINT flags) { return S_OK; }

static UINT STDMETHODCALLTYPE device_get_exception_mode(ID3D11Device *iface) { return 0; }

static ID3D11DeviceVtbl device_vtbl = {
    .QueryInterface = device_query,
    .AddRef = device_add_ref,
    .Release = device_release,
    .CreateTexture2D = device_create_texture2d,
    .CheckFormatSupport = device_check_format_support,
    .GetPrivateData = device_get_private_data,
    .SetPrivateData = device_set_private_data,
    .SetPrivateDataInterface = device_set_private_data_interface,
    .GetFeatureLevel = device_get_feature_level,
    .GetCreationFlags = device_get_creation_flags,
    .GetDeviceRemovedReason = device_get_removed_reason,
    .GetImmediateContext = device_get_immediate_context,
    .SetExceptionMode = device_set_exception_mode,
    .GetExceptionMode = device_get_exception_mode,
};

static HRESULT STDMETHODCALLTYPE presenter_query(IMimeDXGIPresenter *iface, REFIID riid, void **out)
{
    return device_query(&presenter_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE presenter_add_ref(IMimeDXGIPresenter *iface)
{
    return device_add_ref(&presenter_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE presenter_release(IMimeDXGIPresenter *iface)
{
    return device_release(&presenter_impl(iface)->iface);
}

/* window swap chains need a gpu to present with. */
static HRESULT STDMETHODCALLTYPE presenter_create_swap_chain(IMimeDXGIPresenter *iface, IDXGIFactory *factory,
                                                             HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
                                                             const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,
                                                             IDXGIOutput *restrict_to, IDXGISwapChain1 **swapchain)
{
    D3dLog("d3d11: the software device has no window swap chains");
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE presenter_create_composition_swap_chain(IMimeDXGIPresenter *iface,
                                                                         IDXGIFactory *factory, HANDLE surface,
                                                                         const DXGI_SWAP_CHAIN_DESC1 *desc,
                                                                         IDXGIOutput *restrict_to,
                                                                         IDXGISwapChain1 **swapchain)
{
    WarpDevice *device = presenter_impl(iface);
    if (surface) {
        D3dLog("d3d11: the software device has no swap chains for composition surface handles");
        return DXGI_ERROR_UNSUPPORTED;
    }
    if (!desc->Width || !desc->Height || !supported_format(desc->Format) || desc->SampleDesc.Count != 1 ||
        desc->BufferCount < 2 || desc->BufferCount > DXGI_MAX_SWAP_CHAIN_BUFFERS || desc->Stereo ||
        desc->AlphaMode == DXGI_ALPHA_MODE_STRAIGHT || desc->AlphaMode > DXGI_ALPHA_MODE_IGNORE ||
        (desc->SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL && desc->SwapEffect != DXGI_SWAP_EFFECT_FLIP_DISCARD))
        return DXGI_ERROR_INVALID_CALL;
    WarpSwapChain *sc = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sc));
    if (!sc)
        return E_OUTOFMEMORY;
    sc->iface.lpVtbl = &chain_vtbl;
    sc->source.lpVtbl = &source_vtbl;
    sc->refs = 1;
    sc->device = device;
    sc->factory = factory;
    sc->desc = *desc;
    HRESULT hr = make_back(sc);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, sc);
        return hr;
    }
    factory->lpVtbl->AddRef(factory);
    device->iface.lpVtbl->AddRef(&device->iface);
    *swapchain = &sc->iface;
    return S_OK;
}

static const IMimeDXGIPresenterVtbl presenter_vtbl = {
    presenter_query,  presenter_add_ref, presenter_release, presenter_create_swap_chain,
    presenter_create_composition_swap_chain,
};

static BOOL CALLBACK fill_vtbls(INIT_ONCE *once, void *parameter, void **context)
{
    fill_vtbl(&device_vtbl, sizeof(device_vtbl));
    fill_vtbl(&context_vtbl, sizeof(context_vtbl));
    fill_vtbl(&chain_vtbl, sizeof(chain_vtbl));
    return TRUE;
}

/* warp's feature levels reach 12.1; the first asked for is the one given. */
HRESULT D3dCreateSoftwareDevice(UINT flags, const D3D_FEATURE_LEVEL *levels, UINT count, ID3D11Device **out,
                                D3D_FEATURE_LEVEL *level)
{
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, fill_vtbls, nullptr, nullptr);
    D3D_FEATURE_LEVEL chosen = D3D_FEATURE_LEVEL_11_0;
    if (levels && count) {
        UINT i = 0;
        while (i < count && (levels[i] < D3D_FEATURE_LEVEL_9_1 || levels[i] > D3D_FEATURE_LEVEL_12_1))
            i++;
        if (i == count)
            return E_INVALIDARG;
        chosen = levels[i];
    }
    WarpDevice *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return E_OUTOFMEMORY;
    d->iface.lpVtbl = &device_vtbl;
    d->context.lpVtbl = &context_vtbl;
    d->presenter.lpVtbl = &presenter_vtbl;
    d->refs = 1;
    d->flags = flags;
    d->level = chosen;
    InitializeCriticalSection(&d->lock);
    static LONG logged;
    if (!InterlockedExchange(&logged, 1))
        D3dLog("d3d11: no gpu, warp is the software device");
    *out = &d->iface;
    if (level)
        *level = chosen;
    return S_OK;
}
