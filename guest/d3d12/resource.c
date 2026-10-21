/*
 * memory and resources. a heap is one vulkan allocation with, unless it
 * denies buffers, a buffer over all of it: a buffer placed in it is a range
 * of that buffer, its gpu virtual address the buffer's device address plus
 * the offset. committed resources are ranges of heaps the device makes
 * itself. textures are vulkan images bound to heap memory, living in the
 * general layout from their first commands on.
 */
#include "d3d12_private.h"

#define INTERNAL_HEAP_SIZE (64ull << 20)

typedef struct D12Range {
    UINT64 offset, size;
} D12Range;

/* ------------------------------------------------------------------ memory types */

static int pick_type(D12Device *device, D12MemoryKind kind, UINT allowed)
{
    static const VkMemoryPropertyFlags wanted[D12_MEMORY_KINDS][3] = {
        [D12_MEMORY_DEVICE] = {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, 0},
        [D12_MEMORY_UPLOAD] = {VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, 0},
        [D12_MEMORY_READBACK] = {VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                     VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0},
    };
    for (UINT w = 0; w < 3 && wanted[kind][w]; w++)
        for (UINT i = 0; i < device->memory.memoryTypeCount; i++) {
            VkMemoryPropertyFlags flags = device->memory.memoryTypes[i].propertyFlags;
            /* upload memory the cpu writes and the gpu reads is better not in video memory the cpu cannot see. */
            if ((allowed & (1u << i)) && (flags & wanted[kind][w]) == wanted[kind][w])
                return (int)i;
        }
    if (kind == D12_MEMORY_DEVICE)
        for (UINT i = 0; i < device->memory.memoryTypeCount; i++)
            if (allowed & (1u << i))
                return (int)i;
    return -1;
}

/* the kind of memory heap properties ask for, custom heaps by their cpu page property. */
D12MemoryKind D12HeapKind(D12Device *device, const D3D12_HEAP_PROPERTIES *props)
{
    switch (props->Type) {
    case D3D12_HEAP_TYPE_UPLOAD:
    case D3D12_HEAP_TYPE_GPU_UPLOAD:
        return D12_MEMORY_UPLOAD;
    case D3D12_HEAP_TYPE_READBACK:
        return D12_MEMORY_READBACK;
    case D3D12_HEAP_TYPE_CUSTOM:
        if (props->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE)
            return D12_MEMORY_UPLOAD;
        if (props->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK)
            return D12_MEMORY_READBACK;
        return D12_MEMORY_DEVICE;
    default:
        return D12_MEMORY_DEVICE;
    }
}

static VkBufferUsageFlags buffer_usage(D12Device *device)
{
    VkBufferUsageFlags usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (device->has_transform_feedback)
        usage |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;
    if (device->has_conditional)
        usage |= VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;
    if (device->has_rt)
        usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (device->has_rt_pipeline)
        usage |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
    return usage;
}

/* ------------------------------------------------------------------ gpu virtual addresses */

/* heaps with buffers, sorted by address, so an address finds its buffer by bisection. */
void D12AddressAdd(D12Device *device, D12Heap *heap)
{
    AcquireSRWLockExclusive(&device->address_lock);
    if (device->address_count == device->address_capacity) {
        UINT capacity = device->address_capacity ? device->address_capacity * 2 : 64;
        D12Heap **list = device->address_heaps
                             ? HeapReAlloc(GetProcessHeap(), 0, device->address_heaps, capacity * sizeof(*list))
                             : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
        if (!list) {
            ReleaseSRWLockExclusive(&device->address_lock);
            return;
        }
        device->address_heaps = list;
        device->address_capacity = capacity;
    }
    UINT at = device->address_count;
    while (at && device->address_heaps[at - 1]->va > heap->va) {
        device->address_heaps[at] = device->address_heaps[at - 1];
        at--;
    }
    device->address_heaps[at] = heap;
    device->address_count++;
    ReleaseSRWLockExclusive(&device->address_lock);
}

void D12AddressRemove(D12Device *device, D12Heap *heap)
{
    AcquireSRWLockExclusive(&device->address_lock);
    for (UINT i = 0; i < device->address_count; i++)
        if (device->address_heaps[i] == heap) {
            memmove(&device->address_heaps[i], &device->address_heaps[i + 1],
                    (device->address_count - i - 1) * sizeof(D12Heap *));
            device->address_count--;
            break;
        }
    ReleaseSRWLockExclusive(&device->address_lock);
}

BOOL D12AddressLookup(D12Device *device, D3D12_GPU_VIRTUAL_ADDRESS va, VkBuffer *buffer, VkDeviceSize *offset,
                      UINT64 *available)
{
    BOOL found = FALSE;
    AcquireSRWLockShared(&device->address_lock);
    UINT lo = 0, hi = device->address_count;
    while (lo < hi) {
        UINT mid = (lo + hi) / 2;
        D12Heap *h = device->address_heaps[mid];
        if (va < h->va)
            hi = mid;
        else if (va >= h->va + h->desc.SizeInBytes)
            lo = mid + 1;
        else {
            *buffer = h->buffer;
            *offset = va - h->va;
            if (available)
                *available = h->desc.SizeInBytes - *offset;
            found = TRUE;
            break;
        }
    }
    ReleaseSRWLockShared(&device->address_lock);
    return found;
}

/* ------------------------------------------------------------------ heaps */

D12Heap *D12HeapFrom(ID3D12Heap *iface) { return iface ? CONTAINING_RECORD(iface, D12Heap, iface) : nullptr; }
static D12Heap *heap_impl(ID3D12Heap1 *iface) { return CONTAINING_RECORD(iface, D12Heap, iface); }

static void heap_destroy(D12Heap *heap)
{
    D12Device *device = heap->child.device;
    if (heap->buffer) {
        D12AccelerationStructuresForget(device, heap->va, heap->desc.SizeInBytes);
        D12AddressRemove(device, heap);
        device->vk.vkDestroyBuffer(device->vk.device, heap->buffer, nullptr);
    }
    if (heap->memory)
        device->vk.vkFreeMemory(device->vk.device, heap->memory, nullptr);
    HeapFree(GetProcessHeap(), 0, heap->free);
}

static HRESULT STDMETHODCALLTYPE heap_query(ID3D12Heap1 *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceChild) || IsEqualGUID(riid, &IID_ID3D12Pageable) ||
        IsEqualGUID(riid, &IID_ID3D12Heap) || IsEqualGUID(riid, &IID_ID3D12Heap1)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE heap_add_ref(ID3D12Heap1 *iface) { return InterlockedIncrement(&heap_impl(iface)->child.refs); }

static ULONG STDMETHODCALLTYPE heap_release(ID3D12Heap1 *iface)
{
    D12Heap *heap = heap_impl(iface);
    ULONG refs = InterlockedDecrement(&heap->child.refs);
    if (!refs) {
        heap_destroy(heap);
        D12ChildRelease(&heap->child);
        HeapFree(GetProcessHeap(), 0, heap);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE heap_get_private_data(ID3D12Heap1 *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&heap_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE heap_set_private_data(ID3D12Heap1 *iface, REFGUID guid, UINT size, const void *data)
{
    return MimePrivateSet(&heap_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE heap_set_private_data_interface(ID3D12Heap1 *iface, REFGUID guid,
                                                                 const IUnknown *data)
{
    return MimePrivateSetInterface(&heap_impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE heap_set_name(ID3D12Heap1 *iface, LPCWSTR name) { return S_OK; }

static HRESULT STDMETHODCALLTYPE heap_get_device(ID3D12Heap1 *iface, REFIID riid, void **device)
{
    return D12ChildGetDevice(&heap_impl(iface)->child, riid, device);
}

static D3D12_HEAP_DESC *STDMETHODCALLTYPE heap_get_desc(ID3D12Heap1 *iface, D3D12_HEAP_DESC *ret)
{
    *ret = heap_impl(iface)->desc;
    return ret;
}

static HRESULT STDMETHODCALLTYPE heap_get_protected_session(ID3D12Heap1 *iface, REFIID riid, void **session)
{
    if (session)
        *session = nullptr;
    return DXGI_ERROR_NOT_FOUND;
}

static const struct ID3D12Heap1Vtbl g_heap_vtbl = {
    heap_query,
    heap_add_ref,
    heap_release,
    heap_get_private_data,
    heap_set_private_data,
    heap_set_private_data_interface,
    heap_set_name,
    heap_get_device,
    heap_get_desc,
    heap_get_protected_session,
};

/* the vulkan side of a heap: its memory of a type, a buffer over it, its address and mapping. */
static HRESULT heap_init(D12Device *device, D12Heap *heap, D12MemoryKind kind, int type)
{
    D12Vk *vk = &device->vk;
    heap->kind = kind;
    if (type < 0) {
        VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = heap->desc.SizeInBytes;
        info.usage = buffer_usage(device);
        VkBuffer probe;
        if (vk->vkCreateBuffer(vk->device, &info, nullptr, &probe) != VK_SUCCESS)
            return E_OUTOFMEMORY;
        VkMemoryRequirements req;
        vk->vkGetBufferMemoryRequirements(vk->device, probe, &req);
        vk->vkDestroyBuffer(vk->device, probe, nullptr);
        type = device->buffer_types[kind] != ~0u ? (int)device->buffer_types[kind] : pick_type(device, kind, req.memoryTypeBits);
        if (type < 0)
            return E_OUTOFMEMORY;
        device->buffer_types[kind] = type;
    }
    heap->type = type;
    VkMemoryAllocateFlagsInfo flags = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags};
    alloc.allocationSize = heap->desc.SizeInBytes;
    alloc.memoryTypeIndex = type;
    if (vk->vkAllocateMemory(vk->device, &alloc, nullptr, &heap->memory) != VK_SUCCESS)
        return E_OUTOFMEMORY;
    if (kind != D12_MEMORY_DEVICE &&
        vk->vkMapMemory(vk->device, heap->memory, 0, VK_WHOLE_SIZE, 0, (void **)&heap->mapped) != VK_SUCCESS)
        return E_OUTOFMEMORY;
    if (!(heap->desc.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS)) {
        VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = heap->desc.SizeInBytes;
        info.usage = buffer_usage(device);
        if (vk->vkCreateBuffer(vk->device, &info, nullptr, &heap->buffer) != VK_SUCCESS)
            return E_OUTOFMEMORY;
        vk->vkBindBufferMemory(vk->device, heap->buffer, heap->memory, 0);
        VkBufferDeviceAddressInfo address = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        address.buffer = heap->buffer;
        heap->va = vk->vkGetBufferDeviceAddress(vk->device, &address);
        D12AddressAdd(device, heap);
    }
    return S_OK;
}

static HRESULT heap_create(D12Device *device, const D3D12_HEAP_DESC *desc, BOOL internal, int type, D12Heap **out)
{
    D12Heap *heap = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*heap));
    if (!heap)
        return E_OUTOFMEMORY;
    heap->iface.lpVtbl = &g_heap_vtbl;
    heap->child.refs = 1;
    heap->child.device = device;
    heap->desc = *desc;
    heap->internal = internal;
    if (!heap->desc.Alignment)
        heap->desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    if (!heap->desc.Properties.CreationNodeMask)
        heap->desc.Properties.CreationNodeMask = 1;
    if (!heap->desc.Properties.VisibleNodeMask)
        heap->desc.Properties.VisibleNodeMask = 1;
    HRESULT hr = heap_init(device, heap, D12HeapKind(device, &desc->Properties), type);
    if (FAILED(hr)) {
        heap_destroy(heap);
        HeapFree(GetProcessHeap(), 0, heap);
        return hr;
    }
    if (!internal)
        D12ChildInit(&heap->child, device);
    *out = heap;
    return S_OK;
}

HRESULT D12CreateHeap(D12Device *device, const D3D12_HEAP_DESC *desc, D12Heap **out)
{
    if (!desc->SizeInBytes)
        return E_INVALIDARG;
    if (desc->Properties.Type == D3D12_HEAP_TYPE_CUSTOM &&
        desc->Properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_UNKNOWN)
        return E_INVALIDARG;
    return heap_create(device, desc, FALSE, -1, out);
}

/* ------------------------------------------------------------------ the device's own heaps */

static BOOL add_free(D12Heap *heap, UINT64 offset, UINT64 size)
{
    if (heap->free_count == heap->free_capacity) {
        UINT capacity = heap->free_capacity ? heap->free_capacity * 2 : 16;
        D12Range *ranges = heap->free ? HeapReAlloc(GetProcessHeap(), 0, heap->free, capacity * sizeof(*ranges))
                                      : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*ranges));
        if (!ranges)
            return FALSE;
        heap->free = ranges;
        heap->free_capacity = capacity;
    }
    UINT at = 0;
    while (at < heap->free_count && heap->free[at].offset < offset)
        at++;
    if (at && heap->free[at - 1].offset + heap->free[at - 1].size == offset) {
        heap->free[at - 1].size += size;
        if (at < heap->free_count && offset + size == heap->free[at].offset) {
            heap->free[at - 1].size += heap->free[at].size;
            memmove(&heap->free[at], &heap->free[at + 1], (heap->free_count - at - 1) * sizeof(D12Range));
            heap->free_count--;
        }
        return TRUE;
    }
    if (at < heap->free_count && offset + size == heap->free[at].offset) {
        heap->free[at].offset = offset;
        heap->free[at].size += size;
        return TRUE;
    }
    memmove(&heap->free[at + 1], &heap->free[at], (heap->free_count - at) * sizeof(D12Range));
    heap->free[at].offset = offset;
    heap->free[at].size = size;
    heap->free_count++;
    return TRUE;
}

static BOOL take_range(D12Heap *heap, UINT64 size, UINT64 align, UINT64 *out)
{
    for (UINT i = 0; i < heap->free_count; i++) {
        D12Range r = heap->free[i];
        UINT64 start = D12_ALIGN(r.offset, align);
        if (start + size > r.offset + r.size)
            continue;
        UINT64 tail = r.offset + r.size - (start + size);
        if (start == r.offset) {
            heap->free[i].offset += size;
            heap->free[i].size -= size;
            if (!heap->free[i].size) {
                memmove(&heap->free[i], &heap->free[i + 1], (heap->free_count - i - 1) * sizeof(D12Range));
                heap->free_count--;
            }
        } else {
            heap->free[i].size = start - r.offset;
            if (tail && !add_free(heap, start + size, tail))
                return FALSE;
        }
        *out = start;
        return TRUE;
    }
    return FALSE;
}

/* a range of the device's heaps of a kind whose memory type the resource allows. */
HRESULT D12Alloc(D12Device *device, D12MemoryKind kind, UINT type_bits, UINT64 size, UINT64 align, BOOL image,
                 D12Allocation *out)
{
    memset(out, 0, sizeof(*out));
    size = D12_ALIGN(size, 256);
    AcquireSRWLockExclusive(&device->memory_lock);
    for (D12Heap *heap = device->internal_heaps; heap; heap = heap->next) {
        if (heap->kind != kind || !(type_bits & (1u << heap->type)) || (!image && !heap->buffer))
            continue;
        UINT64 offset;
        if (take_range(heap, size, align, &offset)) {
            *out = (D12Allocation){heap, offset, size, offset >= heap->handed_out};
            heap->handed_out = D12_MAX(heap->handed_out, offset + size);
            ReleaseSRWLockExclusive(&device->memory_lock);
            return S_OK;
        }
    }
    D3D12_HEAP_DESC desc = {};
    desc.SizeInBytes = D12_MAX(INTERNAL_HEAP_SIZE, D12_ALIGN(size, 1 << 20));
    desc.Properties.Type = kind == D12_MEMORY_UPLOAD     ? D3D12_HEAP_TYPE_UPLOAD
                           : kind == D12_MEMORY_READBACK ? D3D12_HEAP_TYPE_READBACK
                                                         : D3D12_HEAP_TYPE_DEFAULT;
    /* the type buffers take, unless the resource needs another. */
    UINT buffer_type = device->buffer_types[kind];
    int type = buffer_type != ~0u && (type_bits & (1u << buffer_type)) ? (int)buffer_type
               : type_bits == ~0u                                       ? -1
                                                                        : pick_type(device, kind, type_bits);
    if (type >= 0 && (UINT)type != buffer_type)
        desc.Flags = D3D12_HEAP_FLAG_DENY_BUFFERS;
    D12Heap *heap;
    HRESULT hr = heap_create(device, &desc, TRUE, type, &heap);
    if (FAILED(hr)) {
        ReleaseSRWLockExclusive(&device->memory_lock);
        return hr;
    }
    add_free(heap, 0, heap->desc.SizeInBytes);
    heap->next = device->internal_heaps;
    device->internal_heaps = heap;
    UINT64 offset = 0;
    take_range(heap, size, align, &offset);
    *out = (D12Allocation){heap, offset, size, offset >= heap->handed_out};
    heap->handed_out = D12_MAX(heap->handed_out, offset + size);
    ReleaseSRWLockExclusive(&device->memory_lock);
    return S_OK;
}

void D12Free(D12Device *device, D12Allocation *alloc)
{
    if (!alloc->heap)
        return;
    AcquireSRWLockExclusive(&device->memory_lock);
    add_free(alloc->heap, alloc->offset, alloc->size);
    ReleaseSRWLockExclusive(&device->memory_lock);
    memset(alloc, 0, sizeof(*alloc));
}

void D12MemoryDestroy(D12Device *device)
{
    for (D12Heap *heap = device->internal_heaps, *next; heap; heap = next) {
        next = heap->next;
        heap_destroy(heap);
        HeapFree(GetProcessHeap(), 0, heap);
    }
    device->internal_heaps = nullptr;
}

/* ------------------------------------------------------------------ images */

VkImageUsageFlags D12ImageUsage(D3D12_RESOURCE_FLAGS flags, const D3dFormat *fmt)
{
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!(flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    return usage;
}

/* the vulkan image a texture description makes; FALSE for what vulkan cannot make. */
BOOL D12IsFeedback(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE ||
           format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE;
}

/* a feedback map's size in regions of its texture's first mip, 4x4 texels when the description says none. */
VkExtent2D D12FeedbackExtent(const D3D12_RESOURCE_DESC1 *desc)
{
    UINT rw = desc->SamplerFeedbackMipRegion.Width ? desc->SamplerFeedbackMipRegion.Width : 4;
    UINT rh = desc->SamplerFeedbackMipRegion.Height ? desc->SamplerFeedbackMipRegion.Height : 4;
    return (VkExtent2D){(UINT)((desc->Width + rw - 1) / rw), (desc->Height + rh - 1) / rh};
}

/*
 * a feedback map is an image of 32-bit words, one a region: the least mip
 * sampled for min mip maps, 0xFF when none, a bit a mip for mip region used
 * maps. shaders reach it as a storage image.
 */
static BOOL feedback_info(const D3D12_RESOURCE_DESC1 *desc, VkImageCreateInfo *info)
{
    if (desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        return FALSE;
    VkExtent2D e = D12FeedbackExtent(desc);
    memset(info, 0, sizeof(*info));
    info->sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info->imageType = VK_IMAGE_TYPE_2D;
    info->format = VK_FORMAT_R32_UINT;
    info->extent = (VkExtent3D){e.width, e.height, 1};
    info->mipLevels = 1;
    info->arrayLayers = desc->DepthOrArraySize ? desc->DepthOrArraySize : 1;
    info->samples = VK_SAMPLE_COUNT_1_BIT;
    info->tiling = VK_IMAGE_TILING_OPTIMAL;
    info->usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return TRUE;
}

static BOOL image_info(D12Device *device, const D3D12_RESOURCE_DESC1 *desc, VkImageCreateInfo *info)
{
    if (D12IsFeedback(desc->Format))
        return feedback_info(desc, info);
    const D3dFormat *f = D3dGetFormat(desc->Format);
    if (f->vk == VK_FORMAT_UNDEFINED)
        return FALSE;
    memset(info, 0, sizeof(*info));
    info->sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    BOOL depth = (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) && f->depth;
    info->format = depth ? f->depth : f->vk;
    info->extent.width = (UINT)desc->Width;
    info->extent.height = desc->Height ? desc->Height : 1;
    info->extent.depth = 1;
    info->arrayLayers = 1;
    switch (desc->Dimension) {
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        info->imageType = VK_IMAGE_TYPE_1D;
        info->extent.height = 1;
        info->arrayLayers = desc->DepthOrArraySize ? desc->DepthOrArraySize : 1;
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        info->imageType = VK_IMAGE_TYPE_2D;
        info->arrayLayers = desc->DepthOrArraySize ? desc->DepthOrArraySize : 1;
        if (info->arrayLayers >= 6 && info->extent.width == info->extent.height && desc->SampleDesc.Count <= 1)
            info->flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
        info->imageType = VK_IMAGE_TYPE_3D;
        info->extent.depth = desc->DepthOrArraySize ? desc->DepthOrArraySize : 1;
        info->flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
        break;
    default:
        return FALSE;
    }
    UINT max_mips = 1, largest = D12_MAX(D12_MAX(info->extent.width, info->extent.height), info->extent.depth);
    while (largest >> max_mips)
        max_mips++;
    info->mipLevels = desc->MipLevels ? D12_MIN(desc->MipLevels, max_mips) : max_mips;
    info->samples = desc->SampleDesc.Count > 1 ? desc->SampleDesc.Count : VK_SAMPLE_COUNT_1_BIT;
    info->tiling = VK_IMAGE_TILING_OPTIMAL;
    info->usage = D12ImageUsage(desc->Flags, f);
    if (depth)
        info->usage &= ~VK_IMAGE_USAGE_STORAGE_BIT;
    /* any 2D texture of R8_UINT may be a shading rate image. */
    if (device->vrs_tier >= D3D12_VARIABLE_SHADING_RATE_TIER_2 && desc->Format == DXGI_FORMAT_R8_UINT &&
        info->imageType == VK_IMAGE_TYPE_2D && info->samples == VK_SAMPLE_COUNT_1_BIT)
        info->usage |= VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
    /* views of other formats of the same size: typeless formats and the casting direct3d 12 allows. */
    if (!(f->flags & (D3D_FORMAT_DEPTH | D3D_FORMAT_STENCIL)) || !depth) {
        info->flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
        if (f->block > 1)
            info->flags |= VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT;
    }
    info->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return TRUE;
}

/* allocation infos asked for, kept by description: each costs a round trip to the host. */
typedef struct InfoEntry {
    D3D12_RESOURCE_DESC1 desc;
    D3D12_RESOURCE_ALLOCATION_INFO info;
    UINT type_bits;
} InfoEntry;

static SRWLOCK g_info_lock = SRWLOCK_INIT;
static InfoEntry *g_infos;
static UINT g_info_count, g_info_capacity;

static UINT desc_hash(const D3D12_RESOURCE_DESC1 *d)
{
    const BYTE *p = (const BYTE *)d;
    UINT h = 2166136261u;
    for (UINT i = 0; i < offsetof(D3D12_RESOURCE_DESC1, SamplerFeedbackMipRegion); i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static BOOL find_info(const D3D12_RESOURCE_DESC1 *key, D3D12_RESOURCE_ALLOCATION_INFO *info, UINT *type_bits)
{
    BOOL found = FALSE;
    AcquireSRWLockShared(&g_info_lock);
    if (g_info_capacity)
        for (UINT i = desc_hash(key) & (g_info_capacity - 1);; i = (i + 1) & (g_info_capacity - 1)) {
            InfoEntry *e = &g_infos[i];
            if (!e->desc.Dimension)
                break;
            if (!memcmp(&e->desc, key, sizeof(*key))) {
                *info = e->info;
                *type_bits = e->type_bits;
                found = TRUE;
                break;
            }
        }
    ReleaseSRWLockShared(&g_info_lock);
    return found;
}

static void insert_info(InfoEntry *table, UINT capacity, const InfoEntry *e)
{
    for (UINT i = desc_hash(&e->desc) & (capacity - 1);; i = (i + 1) & (capacity - 1))
        if (!table[i].desc.Dimension) {
            table[i] = *e;
            return;
        }
}

static void add_info(const D3D12_RESOURCE_DESC1 *key, D3D12_RESOURCE_ALLOCATION_INFO info, UINT type_bits)
{
    AcquireSRWLockExclusive(&g_info_lock);
    if ((g_info_count + 1) * 2 > g_info_capacity) {
        UINT capacity = g_info_capacity ? g_info_capacity * 2 : 256;
        InfoEntry *table = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(*table));
        if (!table) {
            ReleaseSRWLockExclusive(&g_info_lock);
            return;
        }
        for (UINT i = 0; i < g_info_capacity; i++)
            if (g_infos[i].desc.Dimension)
                insert_info(table, capacity, &g_infos[i]);
        HeapFree(GetProcessHeap(), 0, g_infos);
        g_infos = table;
        g_info_capacity = capacity;
    }
    InfoEntry e = {*key, info, type_bits};
    insert_info(g_infos, g_info_capacity, &e);
    g_info_count++;
    ReleaseSRWLockExclusive(&g_info_lock);
}

/* the size and alignment a resource takes in a heap, and for textures the memory types they allow. */
static D3D12_RESOURCE_ALLOCATION_INFO allocation_info(D12Device *device, const D3D12_RESOURCE_DESC1 *desc,
                                                      UINT *type_bits)
{
    D3D12_RESOURCE_ALLOCATION_INFO info = {~0ull, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT};
    *type_bits = ~0u;
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        if (!desc->Width || desc->Height != 1 || desc->DepthOrArraySize != 1 || desc->MipLevels != 1)
            return info;
        info.SizeInBytes = D12_ALIGN(desc->Width, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
        return info;
    }
    D3D12_RESOURCE_DESC1 key = *desc;
    memset(&key.SamplerFeedbackMipRegion, 0, sizeof(key.SamplerFeedbackMipRegion));
    if (find_info(&key, &info, type_bits))
        return info;
    VkImageCreateInfo image;
    if (!image_info(device, desc, &image))
        return (D3D12_RESOURCE_ALLOCATION_INFO){~0ull, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT};
    VkDeviceImageMemoryRequirements req = {VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS};
    req.pCreateInfo = &image;
    VkMemoryRequirements2 out = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    device->vk.vkGetDeviceImageMemoryRequirements(device->vk.device, &req, &out);
    UINT64 align = desc->SampleDesc.Count > 1 ? D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT
                                              : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    /* small alignment asked for and possible: small textures that are not targets. */
    if (desc->Alignment && desc->Alignment < align && out.memoryRequirements.alignment <= desc->Alignment &&
        !(desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)))
        align = desc->Alignment;
    align = D12_MAX(align, out.memoryRequirements.alignment);
    info.Alignment = align;
    info.SizeInBytes = D12_ALIGN(out.memoryRequirements.size, align);
    *type_bits = out.memoryRequirements.memoryTypeBits;
    add_info(&key, info, *type_bits);
    return info;
}

D3D12_RESOURCE_ALLOCATION_INFO D12AllocationInfo(D12Device *device, const D3D12_RESOURCE_DESC1 *desc)
{
    UINT bits;
    return allocation_info(device, desc, &bits);
}

/* ------------------------------------------------------------------ subresources */

UINT D12SubresourceCount(const D12Resource *resource)
{
    if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return 1;
    UINT planes = (resource->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) && (resource->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) ? 2 : 1;
    return resource->mips * resource->layers * planes;
}

void D12SubresourceDecompose(const D12Resource *resource, UINT index, UINT *mip, UINT *layer, UINT *plane)
{
    UINT mips = resource->mips ? resource->mips : 1, layers = resource->layers ? resource->layers : 1;
    *mip = index % mips;
    *layer = (index / mips) % layers;
    if (plane)
        *plane = index / (mips * layers);
}

VkExtent3D D12MipExtent(const D12Resource *resource, UINT mip)
{
    VkExtent3D e = {D12_MAX((UINT)(resource->desc.Width >> mip), 1u), D12_MAX(resource->desc.Height >> mip, 1u), 1};
    if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        e.depth = D12_MAX((UINT)resource->desc.DepthOrArraySize >> mip, 1u);
    if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
        e.height = 1;
    return e;
}

/* the aspect a plane of a texture is: depth then stencil for depth stencil formats. */
VkImageAspectFlags D12PlaneAspect(const D12Resource *resource, UINT plane)
{
    if (resource->aspect == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
        return plane ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
    return resource->aspect;
}

/* ------------------------------------------------------------------ copyable footprints */

/* the typeless format of a depth stencil format, whose two planes copy as R32 or R16 and R8. */
static BOOL planar_format(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        return TRUE;
    default:
        return FALSE;
    }
}

void D12Footprints(D12Device *device, const D3D12_RESOURCE_DESC1 *desc, UINT first, UINT count, UINT64 base,
                   D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts, UINT *rows, UINT64 *row_size, UINT64 *total)
{
    UINT64 offset = ~0ull;
    UINT done = 0;
    const D3dFormat *f = D3dGetFormat(desc->Format);
    BOOL buffer = desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    BOOL planar = !buffer && planar_format(desc->Format);
    UINT mips = desc->MipLevels ? desc->MipLevels : 1;
    if (!buffer && !desc->MipLevels) {
        UINT64 largest = D12_MAX(desc->Width, desc->Height);
        if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
            largest = D12_MAX(largest, desc->DepthOrArraySize);
        while (largest >> mips)
            mips++;
    }
    UINT layers = buffer || desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc->DepthOrArraySize;
    UINT subresources = mips * layers * (planar ? 2 : 1);
    BOOL valid = desc->Dimension >= D3D12_RESOURCE_DIMENSION_BUFFER &&
                 desc->Dimension <= D3D12_RESOURCE_DIMENSION_TEXTURE3D && first < subresources &&
                 count <= subresources - first && (buffer || f->vk != VK_FORMAT_UNDEFINED) && layers;
    if (valid) {
        offset = 0;
        UINT block = buffer ? 1 : f->block, block_bytes = buffer ? 1 : f->block_bytes;
        for (; done < count; done++) {
            UINT sub = first + done, mip = sub % mips, plane = sub / (mips * layers);
            offset = D12_ALIGN(offset, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
            UINT width = buffer ? (UINT)desc->Width : D12_MAX((UINT)(desc->Width >> mip), 1u);
            UINT height = buffer ? 1 : D12_MAX(desc->Height >> mip, 1u);
            UINT depth = desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                             ? D12_MAX((UINT)desc->DepthOrArraySize >> mip, 1u)
                             : 1;
            width = (UINT)D12_ALIGN(width, block);
            height = (UINT)D12_ALIGN(height, block);
            DXGI_FORMAT format = desc->Format;
            UINT texel_bytes = block_bytes;
            if (planar) {
                format = plane ? DXGI_FORMAT_R8_TYPELESS : DXGI_FORMAT_R32_TYPELESS;
                texel_bytes = plane ? 1 : 4;
            }
            UINT min_pitch = width / block * texel_bytes;
            UINT pitch = (UINT)D12_ALIGN(min_pitch, planar ? 512 : D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
            UINT row_count = height / block;
            if (layouts) {
                layouts[done].Offset = base + offset;
                layouts[done].Footprint.Format = buffer ? DXGI_FORMAT_UNKNOWN : format;
                layouts[done].Footprint.Width = width;
                layouts[done].Footprint.Height = height;
                layouts[done].Footprint.Depth = depth;
                layouts[done].Footprint.RowPitch = buffer ? (UINT)D12_ALIGN(desc->Width, 256) : pitch;
            }
            if (rows)
                rows[done] = row_count;
            if (row_size)
                row_size[done] = buffer ? desc->Width : min_pitch;
            offset += buffer ? desc->Width : (UINT64)pitch * (row_count * depth - 1) + min_pitch;
        }
    }
    for (UINT i = done; i < count; i++) {
        if (layouts)
            memset(&layouts[i], 0xFF, sizeof(layouts[i]));
        if (rows)
            rows[i] = ~0u;
        if (row_size)
            row_size[i] = ~0ull;
    }
    if (total)
        *total = offset;
}

/* ------------------------------------------------------------------ image views */

/* the view a description makes of a texture, made once and kept with it. */
D12ImageView *D12ResourceView(D12Resource *resource, const VkImageViewCreateInfo *info, VkImageUsageFlags usage)
{
    D12Device *device = resource->child.device;
    AcquireSRWLockShared(&resource->views_lock);
    for (D12ImageView *v = resource->views; v; v = v->next)
        if (v->usage.usage == usage && v->info.viewType == info->viewType && v->info.format == info->format &&
            !memcmp(&v->info.components, &info->components, sizeof(info->components)) &&
            !memcmp(&v->info.subresourceRange, &info->subresourceRange, sizeof(info->subresourceRange))) {
            ReleaseSRWLockShared(&resource->views_lock);
            return v;
        }
    ReleaseSRWLockShared(&resource->views_lock);

    D12ImageView *view = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*view));
    if (!view)
        return nullptr;
    view->info = *info;
    view->info.image = resource->image;
    view->usage.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
    view->usage.usage = usage;
    VkImageViewCreateInfo create = view->info;
    create.pNext = &view->usage;
    if (device->vk.vkCreateImageView(device->vk.device, &create, nullptr, &view->view) != VK_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, view);
        return nullptr;
    }
    view->info.pNext = nullptr;
    AcquireSRWLockExclusive(&resource->views_lock);
    view->next = resource->views;
    resource->views = view;
    ReleaseSRWLockExclusive(&resource->views_lock);
    return view;
}

/* ------------------------------------------------------------------ ID3D12Resource2 */

D12Resource *D12ResourceFrom(ID3D12Resource *iface)
{
    return iface ? CONTAINING_RECORD(iface, D12Resource, iface) : nullptr;
}

static D12Resource *impl(ID3D12Resource2 *iface) { return CONTAINING_RECORD(iface, D12Resource, iface); }

static void resource_destroy(D12Resource *resource)
{
    D12Device *device = resource->child.device;
    D12Vk *vk = &device->vk;
    for (D12ImageView *v = resource->views, *next; v; v = next) {
        next = v->next;
        vk->vkDestroyImageView(vk->device, v->view, nullptr);
        HeapFree(GetProcessHeap(), 0, v);
    }
    if (resource->image && !resource->swapchain)
        vk->vkDestroyImage(vk->device, resource->image, nullptr);
    if (resource->buffer)
        D12AccelerationStructuresForget(device, resource->va, resource->desc.Width);
    if (resource->reserved)
        D12FreeTiles(resource);
    if (resource->committed)
        D12Free(device, &resource->alloc);
    if (resource->own_memory)
        D12Defer(device, VK_OBJECT_TYPE_DEVICE_MEMORY, (UINT64)resource->own_memory);
    D12ShareDestroy(resource);
    if (resource->heap && !resource->committed)
        resource->heap->iface.lpVtbl->Release(&resource->heap->iface);
}

static HRESULT STDMETHODCALLTYPE resource_query(ID3D12Resource2 *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceChild) || IsEqualGUID(riid, &IID_ID3D12Pageable) ||
        IsEqualGUID(riid, &IID_ID3D12Resource) || IsEqualGUID(riid, &IID_ID3D12Resource1) ||
        IsEqualGUID(riid, &IID_ID3D12Resource2)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE resource_add_ref(ID3D12Resource2 *iface)
{
    return InterlockedIncrement(&impl(iface)->child.refs);
}

static ULONG STDMETHODCALLTYPE resource_release(ID3D12Resource2 *iface)
{
    D12Resource *resource = impl(iface);
    ULONG refs = InterlockedDecrement(&resource->child.refs);
    if (!refs) {
        resource_destroy(resource);
        D12ChildRelease(&resource->child);
        HeapFree(GetProcessHeap(), 0, resource);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE resource_get_private_data(ID3D12Resource2 *iface, REFGUID guid, UINT *size,
                                                           void *data)
{
    return MimePrivateGet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE resource_set_private_data(ID3D12Resource2 *iface, REFGUID guid, UINT size,
                                                           const void *data)
{
    return MimePrivateSet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE resource_set_private_data_interface(ID3D12Resource2 *iface, REFGUID guid,
                                                                     const IUnknown *data)
{
    return MimePrivateSetInterface(&impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE resource_set_name(ID3D12Resource2 *iface, LPCWSTR name) { return S_OK; }

static HRESULT STDMETHODCALLTYPE resource_get_device(ID3D12Resource2 *iface, REFIID riid, void **device)
{
    return D12ChildGetDevice(&impl(iface)->child, riid, device);
}

/* buffers in heaps the cpu sees map to the guest memory the gpu reads; nothing is copied. */
static HRESULT STDMETHODCALLTYPE resource_map(ID3D12Resource2 *iface, UINT subresource, const D3D12_RANGE *read_range,
                                              void **data)
{
    D12Resource *resource = impl(iface);
    if (!resource->mapped) {
        if (data)
            *data = nullptr;
        return E_INVALIDARG;
    }
    if (subresource)
        return E_INVALIDARG;
    InterlockedIncrement(&resource->map_count);
    if (data)
        *data = resource->mapped;
    return S_OK;
}

static void STDMETHODCALLTYPE resource_unmap(ID3D12Resource2 *iface, UINT subresource,
                                             const D3D12_RANGE *written_range)
{
    D12Resource *resource = impl(iface);
    if (resource->map_count > 0)
        InterlockedDecrement(&resource->map_count);
}

static D3D12_RESOURCE_DESC *STDMETHODCALLTYPE resource_get_desc(ID3D12Resource2 *iface, D3D12_RESOURCE_DESC *ret)
{
    memcpy(ret, &impl(iface)->desc, sizeof(*ret));
    return ret;
}

static D3D12_RESOURCE_DESC1 *STDMETHODCALLTYPE resource_get_desc1(ID3D12Resource2 *iface, D3D12_RESOURCE_DESC1 *ret)
{
    *ret = impl(iface)->desc;
    return ret;
}

static D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE resource_get_gpu_virtual_address(ID3D12Resource2 *iface)
{
    return impl(iface)->va;
}

/* textures the cpu reads and writes would need linear images, which are not made. */
static HRESULT STDMETHODCALLTYPE resource_write_to_subresource(ID3D12Resource2 *iface, UINT dst_subresource,
                                                               const D3D12_BOX *dst_box, const void *src,
                                                               UINT src_row_pitch, UINT src_depth_pitch)
{
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE resource_read_from_subresource(ID3D12Resource2 *iface, void *dst, UINT dst_row_pitch,
                                                                UINT dst_depth_pitch, UINT src_subresource,
                                                                const D3D12_BOX *src_box)
{
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE resource_get_heap_properties(ID3D12Resource2 *iface, D3D12_HEAP_PROPERTIES *props,
                                                              D3D12_HEAP_FLAGS *flags)
{
    D12Resource *resource = impl(iface);
    if (resource->swapchain)
        return E_FAIL;
    if (!resource->committed && !resource->heap)
        return E_INVALIDARG;
    if (props)
        *props = resource->heap_props;
    if (flags)
        *flags = resource->heap_flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE resource_get_protected_session(ID3D12Resource2 *iface, REFIID riid, void **session)
{
    if (session)
        *session = nullptr;
    return DXGI_ERROR_NOT_FOUND;
}

static const struct ID3D12Resource2Vtbl g_resource_vtbl = {
    resource_query,
    resource_add_ref,
    resource_release,
    resource_get_private_data,
    resource_set_private_data,
    resource_set_private_data_interface,
    resource_set_name,
    resource_get_device,
    resource_map,
    resource_unmap,
    resource_get_desc,
    resource_get_gpu_virtual_address,
    resource_write_to_subresource,
    resource_read_from_subresource,
    resource_get_heap_properties,
    resource_get_protected_session,
    resource_get_desc1,
};

/* the first commands of a new image: into the general layout, zeroed when it is a target. */
static void init_image(D12Device *device, D12Resource *resource, BOOL clear)
{
    VkCommandBuffer cmd = D12InitCommands(device);
    VkImageSubresourceRange range = {resource->aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    VkImageMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = resource->image;
    barrier.subresourceRange = range;
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    device->vk.vkCmdPipelineBarrier2(cmd, &dep);
    if (clear) {
        if (resource->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
            VkClearDepthStencilValue zero = {};
            device->vk.vkCmdClearDepthStencilImage(cmd, resource->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        } else {
            VkClearColorValue zero = {};
            device->vk.vkCmdClearColorImage(cmd, resource->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        }
    }
    D12InitDone(device);
}

/* the aspects of the format an image is made in. */
static VkImageAspectFlags format_aspect(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    case VK_FORMAT_S8_UINT:
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

static HRESULT create_texture(D12Device *device, D12Resource *resource, D12Heap *heap, UINT64 offset)
{
    D12Vk *vk = &device->vk;
    const D3D12_RESOURCE_DESC1 *desc = &resource->desc;
    VkImageCreateInfo info;
    if (!image_info(device, desc, &info))
        return E_INVALIDARG;
    BOOL feedback = D12IsFeedback(desc->Format);
    resource->fmt = D3dGetFormat(feedback ? DXGI_FORMAT_R32_UINT : desc->Format);
    resource->mips = info.mipLevels;
    resource->layers = info.arrayLayers;
    resource->samples = info.samples;
    resource->usage = info.usage;
    /* a feedback map keeps its texture's mips in its description. */
    if (!feedback)
        resource->desc.MipLevels = (UINT16)info.mipLevels;
    /* a committed texture made to be shared, or opened from one that was, has memory of its own. */
    BOOL shared = !heap && ((resource->heap_flags & D3D12_HEAP_FLAG_SHARED) || resource->import_blob);
    VkExternalMemoryImageCreateInfo external = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, nullptr,
                                                VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
    if (shared) {
        if (resource->import_blob) {
            info.format = (VkFormat)resource->import_blob->vk_format;
            info.usage = resource->import_blob->vk_usage;
            info.flags = resource->import_blob->vk_flags;
            info.arrayLayers = resource->import_blob->vk_layers;
        }
        info.flags |= VK_IMAGE_CREATE_ALIAS_BIT;
        info.pNext = &external;
    }
    resource->aspect = format_aspect(info.format);
    if (vk->vkCreateImage(vk->device, &info, nullptr, &resource->image) != VK_SUCCESS)
        return E_OUTOFMEMORY;
    VkMemoryRequirements req;
    vk->vkGetImageMemoryRequirements(vk->device, resource->image, &req);
    if (shared) {
        UINT type = 0;
        VkDeviceSize size = req.size;
        if (resource->import_blob) {
            type = resource->import_blob->type_index;
            size = resource->import_blob->memory_size;
            if (vk->vkMimeShareImportMemory(vk->device, resource->import_share.id, resource->image, size, type,
                                            &resource->own_memory) != VK_SUCCESS)
                return E_FAIL;
        } else {
            int picked = pick_type(device, D12HeapKind(device, &resource->heap_props), req.memoryTypeBits);
            if (picked < 0)
                return E_OUTOFMEMORY;
            type = (UINT)picked;
            VkMemoryDedicatedAllocateInfo dedicated = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, nullptr,
                                                       resource->image};
            VkExportMemoryAllocateInfo export = {VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, &dedicated,
                                                 VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
            VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &export, size, type};
            if (vk->vkAllocateMemory(vk->device, &alloc, nullptr, &resource->own_memory) != VK_SUCCESS)
                return E_OUTOFMEMORY;
        }
        resource->committed = TRUE;
        if (vk->vkBindImageMemory(vk->device, resource->image, resource->own_memory, 0) != VK_SUCCESS)
            return E_OUTOFMEMORY;
        if (resource->import_blob) {
            resource->share = resource->import_share;
            resource->share_blob = HeapAlloc(GetProcessHeap(), 0, sizeof(*resource->share_blob));
            if (!resource->share_blob)
                return E_OUTOFMEMORY;
            *resource->share_blob = *resource->import_blob;
        } else {
            info.pNext = nullptr;
            D12ShareDescribe(resource, &info, type, size);
        }
        init_image(device, resource, !resource->import_blob && !(resource->heap_flags & D3D12_HEAP_FLAG_CREATE_NOT_ZEROED));
        return S_OK;
    }
    if (heap) {
        if (!(req.memoryTypeBits & (1u << heap->type)) || offset + req.size > heap->desc.SizeInBytes)
            return E_INVALIDARG;
        if (vk->vkBindImageMemory(vk->device, resource->image, heap->memory, offset) != VK_SUCCESS)
            return E_OUTOFMEMORY;
    } else {
        HRESULT hr = D12Alloc(device, D12HeapKind(device, &resource->heap_props), req.memoryTypeBits, req.size,
                              req.alignment, TRUE, &resource->alloc);
        if (FAILED(hr))
            return hr;
        resource->committed = TRUE;
        if (vk->vkBindImageMemory(vk->device, resource->image, resource->alloc.heap->memory, resource->alloc.offset) !=
            VK_SUCCESS)
            return E_OUTOFMEMORY;
    }
    BOOL clear = !heap && !(resource->heap_flags & D3D12_HEAP_FLAG_CREATE_NOT_ZEROED) &&
                 (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) && !feedback;
    init_image(device, resource, clear);
    return S_OK;
}

static HRESULT create_buffer(D12Device *device, D12Resource *resource, D12Heap *heap, UINT64 offset)
{
    UINT64 size = resource->desc.Width;
    if (heap) {
        if (!heap->buffer || offset + size > heap->desc.SizeInBytes)
            return E_INVALIDARG;
        resource->buffer = heap->buffer;
        resource->offset = offset;
        resource->va = heap->va + offset;
        resource->mapped = heap->mapped ? heap->mapped + offset : nullptr;
        return S_OK;
    }
    /* committed buffers: aligned as windows aligns them, small ones as small resources are. */
    UINT64 align = size <= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT ? D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT
                                                                      : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    D12MemoryKind kind = D12HeapKind(device, &resource->heap_props);
    HRESULT hr = D12Alloc(device, kind, ~0u, size, align, FALSE, &resource->alloc);
    if (FAILED(hr))
        return hr;
    resource->committed = TRUE;
    D12Heap *h = resource->alloc.heap;
    resource->buffer = h->buffer;
    resource->offset = resource->alloc.offset;
    resource->va = h->va + resource->alloc.offset;
    resource->mapped = h->mapped ? h->mapped + resource->alloc.offset : nullptr;
    /*
     * committed resources start zeroed, as windows makes them, even where memory is reused. mapped memory that no resource
     * had before is zero already, vulkan-1.dll makes it of new pages, and the pass over it would fault in what the
     * program has not asked for yet and may write over at once, as an upload of 64 MiB does.
     */
    if (resource->mapped && resource->alloc.fresh)
        ;
    else if (resource->mapped)
        memset(resource->mapped, 0, (SIZE_T)size);
    else if (!(resource->heap_flags & D3D12_HEAP_FLAG_CREATE_NOT_ZEROED)) {
        VkCommandBuffer cmd = D12InitCommands(device);
        device->vk.vkCmdFillBuffer(cmd, resource->buffer, resource->offset, D12_ALIGN(size, 4), 0);
        D12InitDone(device);
    }
    return S_OK;
}

static D12Resource *new_resource(D12Device *device, const D3D12_RESOURCE_DESC1 *desc)
{
    D12Resource *resource = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*resource));
    if (!resource)
        return nullptr;
    resource->iface.lpVtbl = &g_resource_vtbl;
    resource->child.refs = 1;
    resource->child.device = device;
    resource->desc = *desc;
    InitializeSRWLock(&resource->views_lock);
    return resource;
}

static HRESULT create_resource(D12Device *device, const D3D12_HEAP_PROPERTIES *props, D3D12_HEAP_FLAGS flags,
                               const D3D12_RESOURCE_DESC1 *desc, D12Heap *heap, UINT64 offset,
                               D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear,
                               const MimeShareBlob *import_blob, const MimeShare *import_share, D12Resource **out)
{
    if (desc->Dimension < D3D12_RESOURCE_DIMENSION_BUFFER || desc->Dimension > D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        return E_INVALIDARG;
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
        (!desc->Width || desc->Height != 1 || desc->DepthOrArraySize != 1 || desc->MipLevels != 1 ||
         desc->Format != DXGI_FORMAT_UNKNOWN || desc->Layout != D3D12_TEXTURE_LAYOUT_ROW_MAJOR))
        return E_INVALIDARG;
    if (heap && offset % (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
                                                                              : D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT))
        return E_INVALIDARG;
    D12Resource *resource = new_resource(device, desc);
    if (!resource)
        return E_OUTOFMEMORY;
    resource->heap_props = *props;
    if (!resource->heap_props.CreationNodeMask)
        resource->heap_props.CreationNodeMask = 1;
    if (!resource->heap_props.VisibleNodeMask)
        resource->heap_props.VisibleNodeMask = 1;
    resource->heap_flags = flags;
    resource->initial_state = state;
    resource->heap = heap;
    resource->heap_offset = offset;
    resource->import_blob = import_blob;
    if (import_share)
        resource->import_share = *import_share;
    if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && !resource->desc.Alignment)
        resource->desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    HRESULT hr = desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? create_buffer(device, resource, heap, offset)
                                                                    : create_texture(device, resource, heap, offset);
    if (FAILED(hr)) {
        resource->heap = nullptr;
        resource_destroy(resource);
        HeapFree(GetProcessHeap(), 0, resource);
        return hr;
    }
    resource->import_blob = nullptr;
    if (heap)
        heap->iface.lpVtbl->AddRef(&heap->iface);
    /* direct3d makes the memory of acceleration structures resources of their own, in their own state. */
    if (resource->buffer && device->has_rt &&
        (state == D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE ||
         (desc->Flags & D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE)))
        D12AccelerationRegionAdd(device, resource->va, desc->Width);
    D12ChildInit(&resource->child, device);
    *out = resource;
    return S_OK;
}

HRESULT D12CreateResource(D12Device *device, const D3D12_HEAP_PROPERTIES *props, D3D12_HEAP_FLAGS flags,
                          const D3D12_RESOURCE_DESC1 *desc, D12Heap *heap, UINT64 offset,
                          D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear, D12Resource **out)
{
    return create_resource(device, props, flags, desc, heap, offset, state, clear, nullptr, nullptr, out);
}

HRESULT D12CreateResourceImported(D12Device *device, const D3D12_HEAP_PROPERTIES *props, D3D12_HEAP_FLAGS flags,
                                  const D3D12_RESOURCE_DESC1 *desc, const MimeShareBlob *blob, const MimeShare *share,
                                  D12Resource **out)
{
    return create_resource(device, props, flags, desc, nullptr, 0, D3D12_RESOURCE_STATE_COMMON, nullptr, blob, share,
                           out);
}

/* a swap chain's image: owned by the swap chain, already in the general layout once acquired. */
HRESULT D12CreateSwapchainResource(D12Device *device, VkImage image, const D3D12_RESOURCE_DESC1 *desc,
                                   D12Resource **out)
{
    D12Resource *resource = new_resource(device, desc);
    if (!resource)
        return E_OUTOFMEMORY;
    resource->swapchain = TRUE;
    resource->image = image;
    resource->fmt = D3dGetFormat(desc->Format);
    resource->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    resource->mips = resource->layers = resource->samples = 1;
    resource->usage = D12ImageUsage(desc->Flags, resource->fmt);
    resource->heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    D12ChildInit(&resource->child, device);
    *out = resource;
    return S_OK;
}

/* ------------------------------------------------------------------ reserved resources */

/*
 * a reserved resource is a sparse vulkan buffer or image whose 64 KiB
 * tiles map heap memory with vkQueueBindSparse. a texture's tiles are
 * numbered as direct3d numbers them: layer after layer, each layer's
 * standard mips mip after mip, each in rows of tiles, then the layer's
 * packed mips, vulkan's mip tail. a mip tail all layers share is the first
 * layer's.
 */
#define TILE_SIZE 65536u

static UINT tiles_of(UINT texels, UINT shape) { return (texels + shape - 1) / shape; }

/* a standard mip's size in tiles. */
static UINT mip_tiles(const D12Resource *r, UINT mip, UINT *w, UINT *h, UINT *d)
{
    VkExtent3D e = D12MipExtent(r, mip);
    *w = tiles_of(e.width, r->tile_shape.width);
    *h = tiles_of(e.height, r->tile_shape.height);
    *d = tiles_of(e.depth, r->tile_shape.depth);
    return *w * *h * *d;
}

static UINT standard_tiles(const D12Resource *r)
{
    UINT n = 0, w, h, d;
    for (UINT m = 0; m < r->standard_mips; m++)
        n += mip_tiles(r, m, &w, &h, &d);
    return n;
}

static UINT layer_packed(const D12Resource *r, UINT layer)
{
    return r->single_tail && layer ? 0 : r->packed_tiles;
}

static UINT layer_start(const D12Resource *r, UINT layer)
{
    UINT packed = r->single_tail ? (layer ? r->packed_tiles : 0) : layer * r->packed_tiles;
    return layer * standard_tiles(r) + packed;
}

UINT D12TileIndex(const D12Resource *r, const D3D12_TILED_RESOURCE_COORDINATE *at)
{
    if (r->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return at->X < r->tile_count ? at->X : ~0u;
    UINT mip, layer, w, h, d;
    D12SubresourceDecompose(r, at->Subresource, &mip, &layer, nullptr);
    if (layer >= r->layers)
        return ~0u;
    UINT start = layer_start(r, layer);
    if (mip >= r->standard_mips)
        return at->X < layer_packed(r, layer) ? start + standard_tiles(r) + at->X : ~0u;
    for (UINT m = 0; m < mip; m++)
        start += mip_tiles(r, m, &w, &h, &d);
    mip_tiles(r, mip, &w, &h, &d);
    if (at->X >= w || at->Y >= h || at->Z >= d)
        return ~0u;
    return start + at->X + at->Y * w + at->Z * w * h;
}

BOOL D12TileAt(const D12Resource *r, UINT tile, D12TilePlace *place)
{
    memset(place, 0, sizeof(*place));
    if (tile >= r->tile_count)
        return FALSE;
    if (r->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        place->packed = TRUE;
        place->opaque = (VkDeviceSize)tile * TILE_SIZE;
        return TRUE;
    }
    UINT standard = standard_tiles(r), layer = 0;
    while (layer + 1 < r->layers && layer_start(r, layer + 1) <= tile)
        layer++;
    UINT rest = tile - layer_start(r, layer), w, h, d;
    place->layer = layer;
    if (rest >= standard) {
        place->packed = TRUE;
        place->mip = r->standard_mips;
        place->opaque = r->tail_offset + (r->single_tail ? 0 : layer * r->tail_stride) +
                        (VkDeviceSize)(rest - standard) * TILE_SIZE;
        return TRUE;
    }
    UINT mip = 0;
    for (UINT n; rest >= (n = mip_tiles(r, mip, &w, &h, &d)); mip++)
        rest -= n;
    mip_tiles(r, mip, &w, &h, &d);
    VkExtent3D e = D12MipExtent(r, mip);
    place->mip = mip;
    place->at = (VkOffset3D){(INT)((rest % w) * r->tile_shape.width), (INT)((rest / w % h) * r->tile_shape.height),
                             (INT)((rest / (w * h)) * r->tile_shape.depth)};
    place->extent = (VkExtent3D){D12_MIN(r->tile_shape.width, e.width - (UINT)place->at.x),
                                 D12_MIN(r->tile_shape.height, e.height - (UINT)place->at.y),
                                 D12_MIN(r->tile_shape.depth, e.depth - (UINT)place->at.z)};
    return TRUE;
}

UINT D12RegionTiles(const D12Resource *r, const D3D12_TILED_RESOURCE_COORDINATE *start,
                    const D3D12_TILE_REGION_SIZE *size, UINT *tiles, UINT max)
{
    D3D12_TILED_RESOURCE_COORDINATE zero = {};
    D3D12_TILE_REGION_SIZE one = {1};
    start = start ? start : &zero;
    size = size ? size : &one;
    UINT n = 0;
    if (size->UseBox) {
        for (UINT z = 0; z < size->Depth; z++)
            for (UINT y = 0; y < size->Height; y++)
                for (UINT x = 0; x < size->Width && n < max; x++) {
                    D3D12_TILED_RESOURCE_COORDINATE at = {start->X + x, start->Y + y, start->Z + z, start->Subresource};
                    UINT tile = D12TileIndex(r, &at);
                    if (tile != ~0u)
                        tiles[n++] = tile;
                }
        return n;
    }
    UINT first = D12TileIndex(r, start);
    for (UINT i = 0; first != ~0u && i < size->NumTiles && first + i < r->tile_count && n < max; i++)
        tiles[n++] = first + i;
    return n;
}

void D12ResourceTiling(D12Resource *r, UINT *tile_count, D3D12_PACKED_MIP_INFO *packed, D3D12_TILE_SHAPE *shape,
                       UINT *subresource_count, UINT first, D3D12_SUBRESOURCE_TILING *tilings)
{
    BOOL buffer = r->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    if (tile_count)
        *tile_count = r->reserved ? r->tile_count : 0;
    if (packed) {
        memset(packed, 0, sizeof(*packed));
        if (r->reserved && !buffer) {
            packed->NumStandardMips = (UINT8)r->standard_mips;
            packed->NumPackedMips = (UINT8)(r->mips - r->standard_mips);
            packed->NumTilesForPackedMips = r->packed_tiles;
            packed->StartTileIndexInOverallResource = r->packed_tiles ? standard_tiles(r) : 0;
        }
    }
    if (shape) {
        *shape = (D3D12_TILE_SHAPE){0};
        if (r->reserved)
            *shape = buffer ? (D3D12_TILE_SHAPE){TILE_SIZE, 1, 1}
                            : (D3D12_TILE_SHAPE){r->tile_shape.width, r->tile_shape.height, r->tile_shape.depth};
    }
    if (!subresource_count)
        return;
    UINT count = buffer ? 1 : r->mips * r->layers;
    UINT n = first < count ? D12_MIN(*subresource_count, count - first) : 0;
    for (UINT i = 0; tilings && i < n; i++) {
        UINT sub = first + i, mip, layer, w = 0, h = 0, d = 0;
        D12SubresourceDecompose(r, sub, &mip, &layer, nullptr);
        D3D12_SUBRESOURCE_TILING *t = &tilings[i];
        *t = (D3D12_SUBRESOURCE_TILING){0};
        if (!r->reserved)
            continue;
        if (buffer) {
            *t = (D3D12_SUBRESOURCE_TILING){r->tile_count, 1, 1, 0};
        } else if (mip >= r->standard_mips) {
            t->StartTileIndexInOverallResource = 0xFFFFFFFF;
        } else {
            D3D12_TILED_RESOURCE_COORDINATE at = {0, 0, 0, sub};
            mip_tiles(r, mip, &w, &h, &d);
            *t = (D3D12_SUBRESOURCE_TILING){w, (UINT16)h, (UINT16)d, D12TileIndex(r, &at)};
        }
    }
    *subresource_count = n;
}

void D12FreeTiles(D12Resource *r)
{
    D12Device *device = r->child.device;
    if (r->sparse) {
        D12AddressRemove(device, r->sparse);
        HeapFree(GetProcessHeap(), 0, r->sparse);
        r->sparse = nullptr;
    }
    if (r->reserved && r->buffer)
        device->vk.vkDestroyBuffer(device->vk.device, r->buffer, nullptr);
    r->buffer = VK_NULL_HANDLE;
    HeapFree(GetProcessHeap(), 0, r->tiles);
    r->tiles = nullptr;
}

/* a sparse buffer, its addresses its own, found as a heap's. */
static HRESULT create_reserved_buffer(D12Device *device, D12Resource *r)
{
    D12Vk *vk = &device->vk;
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.flags = VK_BUFFER_CREATE_SPARSE_BINDING_BIT | VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT |
                 VK_BUFFER_CREATE_SPARSE_ALIASED_BIT;
    info.size = D12_ALIGN(r->desc.Width, TILE_SIZE);
    info.usage = buffer_usage(device);
    if (vk->vkCreateBuffer(vk->device, &info, nullptr, &r->buffer) != VK_SUCCESS)
        return E_OUTOFMEMORY;
    VkMemoryRequirements req;
    vk->vkGetBufferMemoryRequirements(vk->device, r->buffer, &req);
    if (TILE_SIZE % req.alignment)
        return E_INVALIDARG;
    D12Heap *h = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h));
    if (!h)
        return E_OUTOFMEMORY;
    h->child.device = device;
    h->desc.SizeInBytes = info.size;
    h->internal = TRUE;
    h->buffer = r->buffer;
    VkBufferDeviceAddressInfo address = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    address.buffer = r->buffer;
    h->va = vk->vkGetBufferDeviceAddress(vk->device, &address);
    r->sparse = h;
    r->va = h->va;
    r->tile_count = (UINT)(info.size / TILE_SIZE);
    D12AddressAdd(device, h);
    return S_OK;
}

/* a sparse image of direct3d's tile shapes, which vulkan's standard block shapes are. */
static HRESULT create_reserved_texture(D12Device *device, D12Resource *r)
{
    D12Vk *vk = &device->vk;
    VkImageCreateInfo info;
    if (!image_info(device, &r->desc, &info))
        return E_INVALIDARG;
    info.flags |= VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
                  VK_IMAGE_CREATE_SPARSE_ALIASED_BIT;
    r->fmt = D3dGetFormat(r->desc.Format);
    r->aspect = format_aspect(info.format);
    if (r->aspect != VK_IMAGE_ASPECT_COLOR_BIT || info.samples != VK_SAMPLE_COUNT_1_BIT)
        return E_INVALIDARG;
    VkSparseImageFormatProperties props[4];
    UINT count = RTL_NUMBER_OF(props);
    vk->vkGetPhysicalDeviceSparseImageFormatProperties(vk->physical, info.format, info.imageType, info.samples,
                                                       info.usage, info.tiling, &count, props);
    if (!count || (props[0].flags & VK_SPARSE_IMAGE_FORMAT_NONSTANDARD_BLOCK_SIZE_BIT))
        return E_INVALIDARG;
    r->mips = info.mipLevels;
    r->layers = info.arrayLayers;
    r->samples = info.samples;
    r->usage = info.usage;
    r->desc.MipLevels = (UINT16)info.mipLevels;
    if (vk->vkCreateImage(vk->device, &info, nullptr, &r->image) != VK_SUCCESS)
        return E_OUTOFMEMORY;
    VkMemoryRequirements req;
    vk->vkGetImageMemoryRequirements(vk->device, r->image, &req);
    VkSparseImageMemoryRequirements sparse[4];
    count = RTL_NUMBER_OF(sparse);
    vk->vkGetImageSparseMemoryRequirements(vk->device, r->image, &count, sparse);
    if (!count || TILE_SIZE % req.alignment || req.alignment != TILE_SIZE)
        return E_INVALIDARG;
    const VkSparseImageMemoryRequirements *s = &sparse[0];
    r->tile_shape = s->formatProperties.imageGranularity;
    r->standard_mips = D12_MIN(s->imageMipTailFirstLod, r->mips);
    r->packed_tiles = r->standard_mips < r->mips ? (UINT)((s->imageMipTailSize + TILE_SIZE - 1) / TILE_SIZE) : 0;
    r->tail_offset = s->imageMipTailOffset;
    r->tail_stride = s->imageMipTailStride;
    r->single_tail = (s->formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT) != 0;
    r->tile_count = layer_start(r, r->layers);
    init_image(device, r, FALSE);
    return S_OK;
}

HRESULT D12CreateReservedResource(D12Device *device, const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES state,
                                  D12Resource **out)
{
    if (!device->tiled_tier || desc->Dimension < D3D12_RESOURCE_DIMENSION_BUFFER ||
        desc->Dimension > D3D12_RESOURCE_DIMENSION_TEXTURE3D || desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
        (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D && device->tiled_tier < D3D12_TILED_RESOURCES_TIER_3))
        return E_INVALIDARG;
    BOOL buffer = desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    if (buffer && (!desc->Width || desc->Height != 1 || desc->DepthOrArraySize != 1 || desc->MipLevels != 1 ||
                   desc->Layout != D3D12_TEXTURE_LAYOUT_ROW_MAJOR))
        return E_INVALIDARG;
    D12Resource *r = new_resource(device, desc);
    if (!r)
        return E_OUTOFMEMORY;
    r->reserved = TRUE;
    InitializeSRWLock(&r->tiles_lock);
    r->heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    r->heap_props.CreationNodeMask = r->heap_props.VisibleNodeMask = 1;
    r->initial_state = state;
    if (buffer && !r->desc.Alignment)
        r->desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    HRESULT hr = buffer ? create_reserved_buffer(device, r) : create_reserved_texture(device, r);
    if (SUCCEEDED(hr) && !(r->tiles = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)r->tile_count * sizeof(D12Tile))))
        hr = E_OUTOFMEMORY;
    if (FAILED(hr)) {
        resource_destroy(r);
        HeapFree(GetProcessHeap(), 0, r);
        return hr;
    }
    D12ChildInit(&r->child, device);
    *out = r;
    return S_OK;
}
