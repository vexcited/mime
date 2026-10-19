/*
 * committed textures that are shared: made with D3D12_HEAP_FLAG_SHARED they own memory that other devices import,
 * and CreateSharedHandle gives them a kernel object carrying what they were described with and the id the host
 * keeps the memory under, see mime/gfxshare.h. A texture of direct3d 11 that was shared opens here as a committed
 * texture of the same memory.
 */
#include "d3d12_private.h"

/* direct3d 11's bind flags, which a texture made here is described to it with. */
#define BIND_SHADER_RESOURCE 0x8
#define BIND_RENDER_TARGET 0x20
#define BIND_DEPTH_STENCIL 0x40
#define BIND_UNORDERED_ACCESS 0x80

void D12ShareDescribe(D12Resource *r, const VkImageCreateInfo *info, UINT type_index, UINT64 memory_size)
{
    D12Device *device = r->child.device;
    MimeShareBlob *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b)
        return;
    const D3D12_RESOURCE_DESC1 *d = &r->desc;
    UINT flags = d->Flags;
    b->magic = MIME_SHARE_MAGIC;
    b->kind = MIME_SHARE_TEXTURE;
    b->caps = ((flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ? MIME_SHARE_CAN_RENDER : 0) |
              ((flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ? MIME_SHARE_CAN_DEPTH : 0) |
              ((flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) ? MIME_SHARE_CAN_WRITE : 0) |
              ((flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ? 0 : MIME_SHARE_CAN_READ) |
              MIME_SHARE_ORIGIN_D3D12 | MIME_SHARE_NT_HANDLE;
    b->dimension = d->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ? 1 : d->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ? 2 : 3;
    b->format = d->Format;
    b->width = (UINT32)d->Width;
    b->height = d->Height;
    b->depth = d->DepthOrArraySize;
    b->mips = d->MipLevels;
    b->samples = d->SampleDesc.Count;
    b->quality = d->SampleDesc.Quality;
    b->d3d11_bind = ((b->caps & MIME_SHARE_CAN_RENDER) ? BIND_RENDER_TARGET : 0) |
                    ((b->caps & MIME_SHARE_CAN_DEPTH) ? BIND_DEPTH_STENCIL : 0) |
                    ((b->caps & MIME_SHARE_CAN_WRITE) ? BIND_UNORDERED_ACCESS : 0) |
                    ((b->caps & MIME_SHARE_CAN_READ) ? BIND_SHADER_RESOURCE : 0);
    b->d3d12_flags = flags;
    b->d3d12_heap_flags = r->heap_flags;
    b->vk_format = info->format;
    b->vk_usage = info->usage;
    b->vk_flags = info->flags;
    b->vk_layers = info->arrayLayers;
    b->type_index = type_index;
    b->memory_size = memory_size;
    b->adapter = device->adapter_luid;
    r->share_blob = b;
}

void D12ShareDestroy(D12Resource *r)
{
    MimeShareClose(&r->share);
    HeapFree(GetProcessHeap(), 0, r->share_blob);
    r->share_blob = nullptr;
}

/* the handle to a committed texture made to be shared; the first one makes the object. */
HRESULT D12ResourceShare(D12Resource *r, const SECURITY_ATTRIBUTES *attributes, LPCWSTR name, HANDLE *out)
{
    D12Device *device = r->child.device;
    if (!r->share_blob)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&r->share_lock);
    if (!r->share.handle) {
        hr = MimeShareCreate(&r->share, MIME_GFXSHARE_RESOURCE, r->share_blob, sizeof(*r->share_blob), 0, name,
                             attributes);
        if (SUCCEEDED(hr) &&
            device->vk.vkMimeShareExportMemory(device->vk.device, r->own_memory, r->share.id) != VK_SUCCESS) {
            MimeShareClose(&r->share);
            hr = E_FAIL;
        }
    } else if (name) {
        hr = DXGI_ERROR_NAME_ALREADY_EXISTS;
    }
    ReleaseSRWLockExclusive(&r->share_lock);
    if (SUCCEEDED(hr))
        hr = MimeShareHandle(&r->share, 0, attributes, out);
    return hr;
}

/*
 * The texture a handle names, as a committed texture of this device in the common state. A texture of direct3d 11
 * has the flags its bind flags imply: render target, depth stencil and unordered access allow those, a depth
 * stencil that is not bound for shaders denies them.
 */
HRESULT D12OpenSharedResource(D12Device *device, HANDLE handle, REFIID riid, void **out)
{
    MimeShareBlob blob = {};
    MimeShare share;
    UINT size = 0;
    HRESULT hr = MimeShareOpen(handle, MIME_GFXSHARE_RESOURCE, &share, &blob, sizeof(blob), &size);
    if (FAILED(hr))
        return hr;
    if (size != sizeof(blob) || blob.magic != MIME_SHARE_MAGIC || blob.kind != MIME_SHARE_TEXTURE ||
        blob.adapter.LowPart != device->adapter_luid.LowPart || blob.adapter.HighPart != device->adapter_luid.HighPart) {
        MimeShareClose(&share);
        return E_INVALIDARG;
    }
    D3D12_RESOURCE_DESC1 desc = {};
    desc.Dimension = blob.dimension == 1   ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
                     : blob.dimension == 2 ? D3D12_RESOURCE_DIMENSION_TEXTURE2D
                                           : D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = blob.width;
    desc.Height = blob.height;
    desc.DepthOrArraySize = (UINT16)blob.depth;
    desc.MipLevels = (UINT16)blob.mips;
    desc.Format = (DXGI_FORMAT)blob.format;
    desc.SampleDesc.Count = blob.samples;
    desc.SampleDesc.Quality = blob.quality;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    /* an opened shared texture is accessible from any queue and device, which its flags say. */
    desc.Flags = (D3D12_RESOURCE_FLAGS)(blob.d3d12_flags | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
    D3D12_HEAP_PROPERTIES props = {};
    props.Type = D3D12_HEAP_TYPE_DEFAULT;
    props.CreationNodeMask = props.VisibleNodeMask = 1;
    D12Resource *r;
    hr = D12CreateResourceImported(device, &props, D3D12_HEAP_FLAG_SHARED, &desc, &blob, &share, &r);
    if (FAILED(hr)) {
        MimeShareClose(&share);
        return hr;
    }
    hr = r->iface.lpVtbl->QueryInterface(&r->iface, riid, out);
    r->iface.lpVtbl->Release(&r->iface);
    return hr;
}
