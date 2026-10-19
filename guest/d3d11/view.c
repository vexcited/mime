/*
 * shader resource, render target, depth stencil and unordered access
 * views. each is a vulkan view and, for shaders, a descriptor in the heap.
 * views of dynamic buffers follow their renames: a new texel view and
 * descriptor for each new range, the old ones freed once the gpu is done.
 */
#include "d3d11_private.h"

#define VTBL_FN(f) ((void *)(f))

static D3dView *impl(void *iface) { return CONTAINING_RECORD(iface, D3dView, iface); }

D3dView *D3dViewFrom(void *iface, D3dViewKind kind)
{
    if (!iface)
        return nullptr;
    D3dView *view = impl(iface);
    return view->kind == kind ? view : nullptr;
}

static const GUID *const SRV_IIDS[] = {&IID_ID3D11View, &IID_ID3D11ShaderResourceView,
                                       &IID_ID3D11ShaderResourceView1};
static const GUID *const RTV_IIDS[] = {&IID_ID3D11View, &IID_ID3D11RenderTargetView, &IID_ID3D11RenderTargetView1};
static const GUID *const DSV_IIDS[] = {&IID_ID3D11View, &IID_ID3D11DepthStencilView, &IID_ID3D11DepthStencilView};
static const GUID *const UAV_IIDS[] = {&IID_ID3D11View, &IID_ID3D11UnorderedAccessView,
                                       &IID_ID3D11UnorderedAccessView1};

static HRESULT STDMETHODCALLTYPE view_query(void *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    D3dView *view = impl(iface);
    static const GUID *const *const IIDS[] = {SRV_IIDS, RTV_IIDS, DSV_IIDS, UAV_IIDS};
    BOOL match = IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild);
    for (UINT i = 0; i < 3 && !match; i++)
        match = IsEqualGUID(riid, IIDS[view->kind][i]);
    void *d3d10 = match ? nullptr : D3d10ViewInterface(view, riid);
    if (!match && !d3d10) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&view->child.refs);
    *out = d3d10 ? d3d10 : iface;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE view_add_ref(void *iface) { return InterlockedIncrement(&impl(iface)->child.refs); }

static void view_destroy(D3dView *view)
{
    D3dDevice *device = view->child.device;
    UINT64 serial = D3D_MAX(view->resource->last_use, device->next_serial);
    D3dDefer(device, serial, D3D_GARBAGE_IMAGE_VIEW, (UINT64)view->image_view);
    D3dDefer(device, serial, D3D_GARBAGE_BUFFER_VIEW, (UINT64)view->buffer_view);
    if (view->heap_index)
        D3dHeapFree(device, view->heap_kind, view->heap_index);
    if (view->counter.block) {
        D3dDefer(device, serial, D3D_GARBAGE_BUFFER_VIEW, (UINT64)view->counter_view);
        D3dHeapFree(device, D3D_HEAP_STORAGE_TEXEL, view->counter_index);
        D3dDeferFree(device, serial, &view->counter);
    }
    D3dResourceRelease(view->resource);
    D3dChildRelease(&view->child);
    HeapFree(GetProcessHeap(), 0, view);
}

static ULONG STDMETHODCALLTYPE view_release(void *iface)
{
    D3dView *view = impl(iface);
    ULONG refs = InterlockedDecrement(&view->child.refs);
    if (!refs)
        view_destroy(view);
    return refs;
}

static void STDMETHODCALLTYPE view_get_device(void *iface, ID3D11Device **device)
{
    D3dDevice *d = impl(iface)->child.device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = (ID3D11Device *)&d->iface;
}

static HRESULT STDMETHODCALLTYPE view_get_private_data(void *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE view_set_private_data(void *iface, REFGUID guid, UINT size, const void *data)
{
    return MimePrivateSet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE view_set_private_data_interface(void *iface, REFGUID guid, const IUnknown *data)
{
    return MimePrivateSetInterface(&impl(iface)->child.data, guid, data);
}

static void STDMETHODCALLTYPE view_get_resource(void *iface, ID3D11Resource **resource)
{
    D3dResource *r = impl(iface)->resource;
    D3dResourceAddRef(r);
    *resource = r->iface;
}

static void STDMETHODCALLTYPE srv_get_desc1(void *iface, D3D11_SHADER_RESOURCE_VIEW_DESC1 *desc)
{
    *desc = impl(iface)->desc.srv;
}

/* the 11.0 description: the plane slices of the 11.3 one dropped. */
static void STDMETHODCALLTYPE srv_get_desc(void *iface, D3D11_SHADER_RESOURCE_VIEW_DESC *desc)
{
    const D3D11_SHADER_RESOURCE_VIEW_DESC1 *d = &impl(iface)->desc.srv;
    memset(desc, 0, sizeof(*desc));
    desc->Format = d->Format;
    desc->ViewDimension = d->ViewDimension;
    if (d->ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D) {
        desc->Texture2D.MostDetailedMip = d->Texture2D.MostDetailedMip;
        desc->Texture2D.MipLevels = d->Texture2D.MipLevels;
    } else if (d->ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DARRAY) {
        memcpy(&desc->Texture2DArray, &d->Texture2DArray, sizeof(desc->Texture2DArray));
    } else {
        memcpy(&desc->Buffer, &d->Buffer, sizeof(desc->TextureCubeArray));
    }
}

static void STDMETHODCALLTYPE rtv_get_desc1(void *iface, D3D11_RENDER_TARGET_VIEW_DESC1 *desc)
{
    *desc = impl(iface)->desc.rtv;
}

static void STDMETHODCALLTYPE rtv_get_desc(void *iface, D3D11_RENDER_TARGET_VIEW_DESC *desc)
{
    const D3D11_RENDER_TARGET_VIEW_DESC1 *d = &impl(iface)->desc.rtv;
    memset(desc, 0, sizeof(*desc));
    desc->Format = d->Format;
    desc->ViewDimension = d->ViewDimension;
    if (d->ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D)
        desc->Texture2D.MipSlice = d->Texture2D.MipSlice;
    else if (d->ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY)
        memcpy(&desc->Texture2DArray, &d->Texture2DArray, sizeof(desc->Texture2DArray));
    else
        memcpy(&desc->Buffer, &d->Buffer, sizeof(desc->Texture3D));
}

static void STDMETHODCALLTYPE dsv_get_desc(void *iface, D3D11_DEPTH_STENCIL_VIEW_DESC *desc)
{
    *desc = impl(iface)->desc.dsv;
}

static void STDMETHODCALLTYPE uav_get_desc1(void *iface, D3D11_UNORDERED_ACCESS_VIEW_DESC1 *desc)
{
    *desc = impl(iface)->desc.uav;
}

static void STDMETHODCALLTYPE uav_get_desc(void *iface, D3D11_UNORDERED_ACCESS_VIEW_DESC *desc)
{
    const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *d = &impl(iface)->desc.uav;
    memset(desc, 0, sizeof(*desc));
    desc->Format = d->Format;
    desc->ViewDimension = d->ViewDimension;
    if (d->ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2D)
        desc->Texture2D.MipSlice = d->Texture2D.MipSlice;
    else if (d->ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2DARRAY)
        memcpy(&desc->Texture2DArray, &d->Texture2DArray, sizeof(desc->Texture2DArray));
    else
        memcpy(&desc->Buffer, &d->Buffer, sizeof(desc->Texture3D));
}

#define VIEW_METHODS                                                                                                   \
    VTBL_FN(view_query), VTBL_FN(view_add_ref), VTBL_FN(view_release), VTBL_FN(view_get_device),                     \
        VTBL_FN(view_get_private_data), VTBL_FN(view_set_private_data), VTBL_FN(view_set_private_data_interface),     \
        VTBL_FN(view_get_resource)

static const ID3D11ShaderResourceView1Vtbl srv_vtbl = {VIEW_METHODS, VTBL_FN(srv_get_desc), VTBL_FN(srv_get_desc1)};
static const ID3D11RenderTargetView1Vtbl rtv_vtbl = {VIEW_METHODS, VTBL_FN(rtv_get_desc), VTBL_FN(rtv_get_desc1)};
static const ID3D11DepthStencilViewVtbl dsv_vtbl = {VIEW_METHODS, VTBL_FN(dsv_get_desc)};
static const ID3D11UnorderedAccessView1Vtbl uav_vtbl = {VIEW_METHODS, VTBL_FN(uav_get_desc), VTBL_FN(uav_get_desc1)};

/* ------------------------------------------------------------------ defaults */

/* the view a null description asks for: the whole resource, in its format. */
static void default_desc(D3dViewKind kind, const D3dResource *r, void *out)
{
    UINT layers = r->tex.layers;
    BOOL array = layers > 1, ms = r->tex.samples > 1;
    switch (kind) {
    case D3D_VIEW_SRV: {
        D3D11_SHADER_RESOURCE_VIEW_DESC1 *d = out;
        memset(d, 0, sizeof(*d));
        if (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
            d->ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            d->Buffer.NumElements = r->buf.stride ? r->buf.size / r->buf.stride : 0;
            return;
        }
        d->Format = r->tex.format;
        if (r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D) {
            d->ViewDimension = array ? D3D11_SRV_DIMENSION_TEXTURE1DARRAY : D3D11_SRV_DIMENSION_TEXTURE1D;
            d->Texture1DArray.MipLevels = r->tex.mips;
            d->Texture1DArray.ArraySize = layers;
        } else if (r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
            d->ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
            d->Texture3D.MipLevels = r->tex.mips;
        } else if (r->misc & D3D11_RESOURCE_MISC_TEXTURECUBE) {
            d->ViewDimension = layers > 6 ? D3D11_SRV_DIMENSION_TEXTURECUBEARRAY : D3D11_SRV_DIMENSION_TEXTURECUBE;
            d->TextureCubeArray.MipLevels = r->tex.mips;
            d->TextureCubeArray.NumCubes = layers / 6;
        } else if (ms) {
            d->ViewDimension = array ? D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D11_SRV_DIMENSION_TEXTURE2DMS;
            d->Texture2DMSArray.ArraySize = layers;
        } else {
            d->ViewDimension = array ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D;
            d->Texture2DArray.MipLevels = r->tex.mips;
            d->Texture2DArray.ArraySize = layers;
        }
        return;
    }
    case D3D_VIEW_RTV: {
        D3D11_RENDER_TARGET_VIEW_DESC1 *d = out;
        memset(d, 0, sizeof(*d));
        d->Format = r->tex.format;
        if (r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D) {
            d->ViewDimension = array ? D3D11_RTV_DIMENSION_TEXTURE1DARRAY : D3D11_RTV_DIMENSION_TEXTURE1D;
            d->Texture1DArray.ArraySize = layers;
        } else if (r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
            d->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
            d->Texture3D.WSize = r->tex.depth;
        } else if (ms) {
            d->ViewDimension = array ? D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D11_RTV_DIMENSION_TEXTURE2DMS;
            d->Texture2DMSArray.ArraySize = layers;
        } else {
            d->ViewDimension = array ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;
            d->Texture2DArray.ArraySize = layers;
        }
        return;
    }
    case D3D_VIEW_DSV: {
        D3D11_DEPTH_STENCIL_VIEW_DESC *d = out;
        memset(d, 0, sizeof(*d));
        d->Format = r->tex.format;
        if (r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D) {
            d->ViewDimension = array ? D3D11_DSV_DIMENSION_TEXTURE1DARRAY : D3D11_DSV_DIMENSION_TEXTURE1D;
            d->Texture1DArray.ArraySize = layers;
        } else if (ms) {
            d->ViewDimension = array ? D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D11_DSV_DIMENSION_TEXTURE2DMS;
            d->Texture2DMSArray.ArraySize = layers;
        } else {
            d->ViewDimension = array ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D;
            d->Texture2DArray.ArraySize = layers;
        }
        return;
    }
    case D3D_VIEW_UAV: {
        D3D11_UNORDERED_ACCESS_VIEW_DESC1 *d = out;
        memset(d, 0, sizeof(*d));
        if (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
            d->ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            d->Buffer.NumElements = r->buf.stride ? r->buf.size / r->buf.stride : 0;
            return;
        }
        d->Format = r->tex.format;
        if (r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D) {
            d->ViewDimension = array ? D3D11_UAV_DIMENSION_TEXTURE1DARRAY : D3D11_UAV_DIMENSION_TEXTURE1D;
            d->Texture1DArray.ArraySize = layers;
        } else if (r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
            d->ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
            d->Texture3D.WSize = r->tex.depth;
        } else {
            d->ViewDimension = array ? D3D11_UAV_DIMENSION_TEXTURE2DARRAY : D3D11_UAV_DIMENSION_TEXTURE2D;
            d->Texture2DArray.ArraySize = layers;
        }
        return;
    }
    }
}

/* ------------------------------------------------------------------ vulkan views */

/* the view's format and aspect on its image, depth formats seen through their depth or stencil plane. */
static BOOL view_format(const D3dResource *r, DXGI_FORMAT format, BOOL attachment, VkFormat *vk,
                        VkImageAspectFlags *aspect, VkComponentMapping *swizzle)
{
    const D3dFormat *f = D3dGetFormat(format);
    if (f->vk == VK_FORMAT_UNDEFINED)
        return FALSE;
    *swizzle = (VkComponentMapping){0};
    *aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    *vk = f->vk;
    if (r->tex.aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
        if (!f->depth)
            return FALSE;
        *vk = f->depth;
        if (attachment) {
            *aspect = r->tex.aspect;
        } else if (f->flags & D3D_FORMAT_STENCIL && !(f->flags & D3D_FORMAT_DEPTH)) {
            /* the stencil reads in green, as direct3d's G8 says. */
            *aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
            *swizzle = (VkComponentMapping){VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_R,
                                            VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE};
        } else {
            *aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        return TRUE;
    }
    if (attachment)
        return TRUE;
    if (format == DXGI_FORMAT_A8_UNORM)
        *swizzle = (VkComponentMapping){VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO,
                                        VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_R};
    else if (f->flags & D3D_FORMAT_BGRX)
        swizzle->a = VK_COMPONENT_SWIZZLE_ONE;
    return TRUE;
}

static VkResult image_view(D3dDevice *device, D3dView *view, VkImageViewType type, VkFormat format,
                           VkComponentMapping swizzle, VkImageUsageFlags usage)
{
    VkImageViewUsageCreateInfo usage_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO};
    usage_info.usage = usage;
    VkImageViewCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, &usage_info};
    info.image = view->resource->tex.image;
    info.viewType = type;
    info.format = format;
    info.components = swizzle;
    info.subresourceRange = view->range;
    view->format = format;
    return device->vk.vkCreateImageView(device->vk.device, &info, nullptr, &view->image_view);
}

/* a texel view of the buffer's current range, written to the view's own heap index. */
static BOOL buffer_view(D3dDevice *device, D3dView *view)
{
    D3dResource *r = view->resource;
    VkBufferViewCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
    info.buffer = r->buf.buffer;
    info.format = view->format;
    info.offset = r->buf.offset + view->buffer_offset;
    info.range = view->buffer_size;
    if (device->vk.vkCreateBufferView(device->vk.device, &info, nullptr, &view->buffer_view) != VK_SUCCESS)
        return FALSE;
    view->buffer_version = r->buf.version;
    D3dHeapWriteTexel(device, view->heap_kind, view->heap_index, view->buffer_view);
    return TRUE;
}

/* follows a rename of the buffer: a new view and descriptor, the old ones kept until the gpu is done. */
void D3dViewRefresh(D3dView *view)
{
    D3dResource *r = view->resource;
    if (r->dimension != D3D11_RESOURCE_DIMENSION_BUFFER || view->buffer_version == r->buf.version)
        return;
    D3dDevice *device = view->child.device;
    D3dDefer(device, device->next_serial, D3D_GARBAGE_BUFFER_VIEW, (UINT64)view->buffer_view);
    D3dHeapFree(device, view->heap_kind, view->heap_index);
    view->buffer_view = VK_NULL_HANDLE;
    view->heap_index = D3dHeapAlloc(device, view->heap_kind);
    if (view->heap_index)
        buffer_view(device, view);
}

/* the bytes a buffer view covers and its texel format: raw and structured views read 4 byte words. */
static BOOL buffer_range(D3dView *view, DXGI_FORMAT format, UINT first, UINT count, BOOL raw)
{
    D3dResource *r = view->resource;
    UINT element;
    if (raw) {
        view->format = VK_FORMAT_R32_UINT;
        element = 4;
    } else if (r->misc & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) {
        view->format = VK_FORMAT_R32_UINT;
        element = r->buf.stride;
    } else {
        const D3dFormat *f = D3dGetFormat(format);
        if (f->vk == VK_FORMAT_UNDEFINED || f->block != 1)
            return FALSE;
        view->format = f->vk;
        element = f->block_bytes;
    }
    UINT64 offset = (UINT64)first * element, size = (UINT64)count * element;
    if (!count || offset + size > r->buf.size)
        return FALSE;
    view->buffer_offset = (UINT)offset;
    view->buffer_size = (UINT)size;
    return TRUE;
}

/*
 * a view of a video format reads one plane, which its format names: the luma plane in one channel, the chroma
 * plane in two, as the format of NV12 or P010 is only a resource's and never a shader's. a plane slice that
 * says otherwise is refused.
 */
static BOOL planar_plane_format(const D3dResource *r, DXGI_FORMAT view_format, UINT slice, VkFormat *format,
                                VkImageAspectFlags *aspect)
{
    BOOL eight = r->tex.format == DXGI_FORMAT_NV12;
    UINT plane;
    switch (view_format) {
    case DXGI_FORMAT_R8_UNORM:
        plane = 0, *format = VK_FORMAT_R8_UNORM;
        break;
    case DXGI_FORMAT_R8G8_UNORM:
        plane = 1, *format = VK_FORMAT_R8G8_UNORM;
        break;
    case DXGI_FORMAT_R16_UNORM:
        plane = 0, *format = VK_FORMAT_R16_UNORM;
        break;
    case DXGI_FORMAT_R16G16_UNORM:
        plane = 1, *format = VK_FORMAT_R16G16_UNORM;
        break;
    default:
        return FALSE;
    }
    if ((view_format == DXGI_FORMAT_R8_UNORM || view_format == DXGI_FORMAT_R8G8_UNORM) != eight)
        return FALSE;
    if (slice && slice != plane)
        return FALSE;
    *aspect = plane ? VK_IMAGE_ASPECT_PLANE_1_BIT : VK_IMAGE_ASPECT_PLANE_0_BIT;
    return TRUE;
}

static BOOL planar_view_format(const D3dResource *r, const D3D11_SHADER_RESOURCE_VIEW_DESC1 *d, VkFormat *format,
                               VkImageAspectFlags *aspect)
{
    UINT slice = d->ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D        ? d->Texture2D.PlaneSlice
                 : d->ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DARRAY ? d->Texture2DArray.PlaneSlice
                                                                          : 0;
    return planar_plane_format(r, d->Format, slice, format, aspect);
}

static HRESULT init_srv(D3dDevice *device, D3dView *view, const D3D11_SHADER_RESOURCE_VIEW_DESC1 *d, UINT swizzle)
{
    D3dResource *r = view->resource;
    if (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
        BOOL raw = d->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX && (d->BufferEx.Flags & D3D11_BUFFEREX_SRV_FLAG_RAW);
        UINT first = d->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX ? d->BufferEx.FirstElement : d->Buffer.FirstElement;
        UINT count = d->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX ? d->BufferEx.NumElements : d->Buffer.NumElements;
        if ((d->ViewDimension != D3D11_SRV_DIMENSION_BUFFER && d->ViewDimension != D3D11_SRV_DIMENSION_BUFFEREX) ||
            !buffer_range(view, d->Format, first, count, raw))
            return E_INVALIDARG;
        view->heap_kind = D3D_HEAP_UNIFORM_TEXEL;
        if (!(view->heap_index = D3dHeapAlloc(device, view->heap_kind)) || !buffer_view(device, view))
            return E_OUTOFMEMORY;
        return S_OK;
    }
    VkFormat format;
    VkImageAspectFlags aspect;
    VkComponentMapping mapping;
    if (r->tex.fmt->flags & D3D_FORMAT_PLANAR) {
        if (!planar_view_format(r, d, &format, &aspect))
            return E_INVALIDARG;
        mapping = (VkComponentMapping){0};
    } else if (!view_format(r, d->Format, FALSE, &format, &aspect, &mapping)) {
        return E_INVALIDARG;
    }
    /* d3d9.dll's swizzles take the place of the format's own. */
    if (swizzle)
        mapping = (VkComponentMapping){swizzle & 0xF, (swizzle >> 4) & 0xF, (swizzle >> 8) & 0xF, (swizzle >> 12) & 0xF};
    UINT mip = 0, mips = 1, layer = 0, layers = 1;
    VkImageViewType type;
    switch (d->ViewDimension) {
    case D3D11_SRV_DIMENSION_TEXTURE1D:
        type = VK_IMAGE_VIEW_TYPE_1D, mip = d->Texture1D.MostDetailedMip, mips = d->Texture1D.MipLevels;
        break;
    case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
        type = VK_IMAGE_VIEW_TYPE_1D_ARRAY, mip = d->Texture1DArray.MostDetailedMip;
        mips = d->Texture1DArray.MipLevels, layer = d->Texture1DArray.FirstArraySlice;
        layers = d->Texture1DArray.ArraySize;
        break;
    case D3D11_SRV_DIMENSION_TEXTURE2D:
        type = VK_IMAGE_VIEW_TYPE_2D, mip = d->Texture2D.MostDetailedMip, mips = d->Texture2D.MipLevels;
        break;
    case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
        type = VK_IMAGE_VIEW_TYPE_2D_ARRAY, mip = d->Texture2DArray.MostDetailedMip;
        mips = d->Texture2DArray.MipLevels, layer = d->Texture2DArray.FirstArraySlice;
        layers = d->Texture2DArray.ArraySize;
        break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMS:
        type = VK_IMAGE_VIEW_TYPE_2D;
        break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
        type = VK_IMAGE_VIEW_TYPE_2D_ARRAY, layer = d->Texture2DMSArray.FirstArraySlice;
        layers = d->Texture2DMSArray.ArraySize;
        break;
    case D3D11_SRV_DIMENSION_TEXTURE3D:
        type = VK_IMAGE_VIEW_TYPE_3D, mip = d->Texture3D.MostDetailedMip, mips = d->Texture3D.MipLevels;
        break;
    case D3D11_SRV_DIMENSION_TEXTURECUBE:
        type = VK_IMAGE_VIEW_TYPE_CUBE, mip = d->TextureCube.MostDetailedMip, mips = d->TextureCube.MipLevels;
        layers = 6;
        break;
    case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
        type = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, mip = d->TextureCubeArray.MostDetailedMip;
        mips = d->TextureCubeArray.MipLevels, layer = d->TextureCubeArray.First2DArrayFace;
        layers = d->TextureCubeArray.NumCubes * 6;
        break;
    default:
        return E_INVALIDARG;
    }
    if (mips == ~0u)
        mips = r->tex.mips - mip;
    if (layers == ~0u)
        layers = r->tex.layers - layer;
    if (mip + mips > r->tex.mips || layer + layers > r->tex.layers || !mips || !layers)
        return E_INVALIDARG;
    view->range = (VkImageSubresourceRange){aspect, mip, mips, layer, layers};
    if (image_view(device, view, type, format, mapping, VK_IMAGE_USAGE_SAMPLED_BIT) != VK_SUCCESS)
        return E_INVALIDARG;
    view->heap_kind = D3D_HEAP_SAMPLED_IMAGE;
    if (!(view->heap_index = D3dHeapAlloc(device, view->heap_kind)))
        return E_OUTOFMEMORY;
    D3dHeapWriteImage(device, view->heap_kind, view->heap_index, view->image_view);
    return S_OK;
}

/* the mip and layers of a render target or depth view, from the description's union. */
static HRESULT attachment_range(D3dView *view, UINT dimension_1d, UINT dimension, UINT mip, UINT layer, UINT layers,
                                VkImageViewType *type)
{
    D3dResource *r = view->resource;
    if (layers == ~0u)
        layers = r->tex.layers - layer;
    if (r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
        /* slices of a volume, seen as layers of a 2d array. */
        VkExtent3D e = D3dMipExtent(r, mip);
        if (layers == ~0u || layer + layers > e.depth)
            layers = e.depth - layer;
        *type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    } else {
        *type = dimension_1d ? (layers > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D)
                             : (dimension ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
        if (mip >= r->tex.mips || layer + layers > r->tex.layers || !layers)
            return E_INVALIDARG;
    }
    view->range.baseMipLevel = mip;
    view->range.levelCount = 1;
    view->range.baseArrayLayer = r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D ? 0 : layer;
    view->range.layerCount = r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D ? 1 : layers;
    view->extent = D3dMipExtent(r, mip);
    view->extent.depth = layers;
    view->samples = r->tex.samples;
    return S_OK;
}

static HRESULT init_rtv(D3dDevice *device, D3dView *view, const D3D11_RENDER_TARGET_VIEW_DESC1 *d)
{
    D3dResource *r = view->resource;
    if (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER || !(r->bind & D3D11_BIND_RENDER_TARGET))
        return E_INVALIDARG;
    VkFormat format;
    VkImageAspectFlags aspect;
    VkComponentMapping swizzle = {};
    if (r->tex.fmt->flags & D3D_FORMAT_PLANAR) {
        /* a video texture is drawn to a plane at a time, as the format of the view says. */
        UINT slice = d->ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D        ? d->Texture2D.PlaneSlice
                     : d->ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY ? d->Texture2DArray.PlaneSlice
                                                                              : 0;
        if (!planar_plane_format(r, d->Format, slice, &format, &aspect))
            return E_INVALIDARG;
    } else if (!view_format(r, d->Format, TRUE, &format, &aspect, &swizzle)) {
        return E_INVALIDARG;
    }
    view->range.aspectMask = aspect;
    VkImageViewType type;
    HRESULT hr;
    switch (d->ViewDimension) {
    case D3D11_RTV_DIMENSION_TEXTURE1D:
        hr = attachment_range(view, 1, 0, d->Texture1D.MipSlice, 0, 1, &type);
        break;
    case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
        hr = attachment_range(view, 1, 1, d->Texture1DArray.MipSlice, d->Texture1DArray.FirstArraySlice,
                              d->Texture1DArray.ArraySize, &type);
        break;
    case D3D11_RTV_DIMENSION_TEXTURE2D:
        hr = attachment_range(view, 0, 0, d->Texture2D.MipSlice, 0, 1, &type);
        break;
    case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
        hr = attachment_range(view, 0, 1, d->Texture2DArray.MipSlice, d->Texture2DArray.FirstArraySlice,
                              d->Texture2DArray.ArraySize, &type);
        break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMS:
        hr = attachment_range(view, 0, 0, 0, 0, 1, &type);
        break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
        hr = attachment_range(view, 0, 1, 0, d->Texture2DMSArray.FirstArraySlice, d->Texture2DMSArray.ArraySize,
                              &type);
        break;
    case D3D11_RTV_DIMENSION_TEXTURE3D:
        hr = attachment_range(view, 0, 1, d->Texture3D.MipSlice, d->Texture3D.FirstWSlice, d->Texture3D.WSize,
                              &type);
        if (SUCCEEDED(hr)) {
            /* a volume's slices are the layers of its 2d array view. */
            view->range.baseArrayLayer = d->Texture3D.FirstWSlice;
            view->range.layerCount = view->extent.depth;
        }
        break;
    default:
        hr = E_INVALIDARG;
        break;
    }
    if (FAILED(hr))
        return hr;
    if (image_view(device, view, type, format, swizzle, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != VK_SUCCESS)
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT init_dsv(D3dDevice *device, D3dView *view, const D3D11_DEPTH_STENCIL_VIEW_DESC *d)
{
    D3dResource *r = view->resource;
    if (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER || r->dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D ||
        !(r->bind & D3D11_BIND_DEPTH_STENCIL))
        return E_INVALIDARG;
    VkFormat format;
    VkImageAspectFlags aspect;
    VkComponentMapping swizzle;
    if (!view_format(r, d->Format, TRUE, &format, &aspect, &swizzle))
        return E_INVALIDARG;
    view->range.aspectMask = aspect;
    VkImageViewType type;
    HRESULT hr;
    switch (d->ViewDimension) {
    case D3D11_DSV_DIMENSION_TEXTURE1D:
        hr = attachment_range(view, 1, 0, d->Texture1D.MipSlice, 0, 1, &type);
        break;
    case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
        hr = attachment_range(view, 1, 1, d->Texture1DArray.MipSlice, d->Texture1DArray.FirstArraySlice,
                              d->Texture1DArray.ArraySize, &type);
        break;
    case D3D11_DSV_DIMENSION_TEXTURE2D:
        hr = attachment_range(view, 0, 0, d->Texture2D.MipSlice, 0, 1, &type);
        break;
    case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
        hr = attachment_range(view, 0, 1, d->Texture2DArray.MipSlice, d->Texture2DArray.FirstArraySlice,
                              d->Texture2DArray.ArraySize, &type);
        break;
    case D3D11_DSV_DIMENSION_TEXTURE2DMS:
        hr = attachment_range(view, 0, 0, 0, 0, 1, &type);
        break;
    case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
        hr = attachment_range(view, 0, 1, 0, d->Texture2DMSArray.FirstArraySlice, d->Texture2DMSArray.ArraySize,
                              &type);
        break;
    default:
        hr = E_INVALIDARG;
        break;
    }
    if (FAILED(hr))
        return hr;
    if (image_view(device, view, type, format, swizzle, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != VK_SUCCESS)
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT init_uav(D3dDevice *device, D3dView *view, const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *d)
{
    D3dResource *r = view->resource;
    if (!(r->bind & D3D11_BIND_UNORDERED_ACCESS))
        return E_INVALIDARG;
    if (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
        if (d->ViewDimension != D3D11_UAV_DIMENSION_BUFFER ||
            !buffer_range(view, d->Format, d->Buffer.FirstElement, d->Buffer.NumElements,
                          d->Buffer.Flags & D3D11_BUFFER_UAV_FLAG_RAW))
            return E_INVALIDARG;
        view->heap_kind = D3D_HEAP_STORAGE_TEXEL;
        if (!(view->heap_index = D3dHeapAlloc(device, view->heap_kind)) || !buffer_view(device, view))
            return E_OUTOFMEMORY;
        if (d->Buffer.Flags & (D3D11_BUFFER_UAV_FLAG_APPEND | D3D11_BUFFER_UAV_FLAG_COUNTER)) {
            /* the hidden counter: a word of the gpu's memory, a texel view for the shader's atomics. */
            if (FAILED(D3dAllocBuffer(device, D3D_MEMORY_DEVICE, 16, 256, &view->counter)))
                return E_OUTOFMEMORY;
            VkBufferViewCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
            info.buffer = view->counter.buffer;
            info.format = VK_FORMAT_R32_UINT;
            info.offset = view->counter.offset;
            info.range = 4;
            if (device->vk.vkCreateBufferView(device->vk.device, &info, nullptr, &view->counter_view) != VK_SUCCESS ||
                !(view->counter_index = D3dHeapAlloc(device, D3D_HEAP_STORAGE_TEXEL)))
                return E_OUTOFMEMORY;
            D3dHeapWriteTexel(device, D3D_HEAP_STORAGE_TEXEL, view->counter_index, view->counter_view);
            VkCommandBuffer cmd = D3dInitCommands(device);
            device->vk.vkCmdFillBuffer(cmd, view->counter.buffer, view->counter.offset, 16, 0);
            D3dInitDone(device);
        }
        return S_OK;
    }
    VkFormat format;
    VkImageAspectFlags aspect;
    VkComponentMapping swizzle;
    if (!view_format(r, d->Format, TRUE, &format, &aspect, &swizzle))
        return E_INVALIDARG;
    UINT mip = 0, layer = 0, layers = 1;
    VkImageViewType type;
    switch (d->ViewDimension) {
    case D3D11_UAV_DIMENSION_TEXTURE1D:
        type = VK_IMAGE_VIEW_TYPE_1D, mip = d->Texture1D.MipSlice;
        break;
    case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
        type = VK_IMAGE_VIEW_TYPE_1D_ARRAY, mip = d->Texture1DArray.MipSlice;
        layer = d->Texture1DArray.FirstArraySlice, layers = d->Texture1DArray.ArraySize;
        break;
    case D3D11_UAV_DIMENSION_TEXTURE2D:
        type = VK_IMAGE_VIEW_TYPE_2D, mip = d->Texture2D.MipSlice;
        break;
    case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
        type = VK_IMAGE_VIEW_TYPE_2D_ARRAY, mip = d->Texture2DArray.MipSlice;
        layer = d->Texture2DArray.FirstArraySlice, layers = d->Texture2DArray.ArraySize;
        break;
    case D3D11_UAV_DIMENSION_TEXTURE3D:
        /* the whole volume: shaders address its slices themselves. */
        type = VK_IMAGE_VIEW_TYPE_3D, mip = d->Texture3D.MipSlice;
        break;
    default:
        return E_INVALIDARG;
    }
    if (layers == ~0u)
        layers = r->tex.layers - layer;
    if (mip >= r->tex.mips || layer + layers > r->tex.layers || !layers)
        return E_INVALIDARG;
    view->range = (VkImageSubresourceRange){aspect, mip, 1, layer, layers};
    if (image_view(device, view, type, format, swizzle, VK_IMAGE_USAGE_STORAGE_BIT) != VK_SUCCESS)
        return E_INVALIDARG;
    view->heap_kind = D3D_HEAP_STORAGE_IMAGE;
    if (!(view->heap_index = D3dHeapAlloc(device, view->heap_kind)))
        return E_OUTOFMEMORY;
    D3dHeapWriteImage(device, view->heap_kind, view->heap_index, view->image_view);
    return S_OK;
}

HRESULT D3dCreateView(D3dDevice *device, D3dViewKind kind, ID3D11Resource *resource, const void *desc,
                      UINT swizzle, D3dView **out)
{
    D3dResource *r = D3dResourceFrom(resource);
    if (!r || r->usage == D3D11_USAGE_STAGING)
        return E_INVALIDARG;
    static const UINT BIND[] = {D3D11_BIND_SHADER_RESOURCE, D3D11_BIND_RENDER_TARGET, D3D11_BIND_DEPTH_STENCIL,
                                D3D11_BIND_UNORDERED_ACCESS};
    if (!(r->bind & BIND[kind]))
        return E_INVALIDARG;
    union {
        D3D11_SHADER_RESOURCE_VIEW_DESC1 srv;
        D3D11_RENDER_TARGET_VIEW_DESC1 rtv;
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv;
        D3D11_UNORDERED_ACCESS_VIEW_DESC1 uav;
    } d;
    if (desc) {
        static const SIZE_T SIZES[] = {sizeof(d.srv), sizeof(d.rtv), sizeof(d.dsv), sizeof(d.uav)};
        memcpy(&d, desc, SIZES[kind]);
    } else {
        default_desc(kind, r, &d);
    }
    /* an unknown format is the resource's own. */
    DXGI_FORMAT *format = &d.srv.Format;
    if (*format == DXGI_FORMAT_UNKNOWN && r->dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
        *format = r->tex.format;
    if (!out)
        return S_FALSE;

    D3dView *view = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*view));
    if (!view)
        return E_OUTOFMEMORY;
    D3dChildInit(&view->child, device);
    view->kind = kind;
    view->resource = r;
    D3dResourceAddRef(r);
    static const void *const VTBLS[] = {&srv_vtbl, &rtv_vtbl, &dsv_vtbl, &uav_vtbl};
    *(const void **)&view->iface = VTBLS[kind];
    HRESULT hr;
    switch (kind) {
    case D3D_VIEW_SRV:
        view->desc.srv = d.srv;
        hr = init_srv(device, view, &d.srv, swizzle);
        break;
    case D3D_VIEW_RTV:
        view->desc.rtv = d.rtv;
        hr = init_rtv(device, view, &d.rtv);
        break;
    case D3D_VIEW_DSV:
        view->desc.dsv = d.dsv;
        hr = init_dsv(device, view, &d.dsv);
        break;
    default:
        view->desc.uav = d.uav;
        hr = init_uav(device, view, &d.uav);
        break;
    }
    if (FAILED(hr)) {
        view_destroy(view);
        return hr;
    }
    *out = view;
    return S_OK;
}
