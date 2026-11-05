/*
 * a shader visible cbv, srv and uav heap is a descriptor buffer in guest memory: first the descriptor of its
 * records, then one mutable descriptor per slot, an image or a texel buffer, then the records describing the
 * buffers shaders read by device address: constant buffers, raw and structured views. a sampler heap is an array
 * of sampler descriptors. vkGetDescriptorEXT is a host round trip, so each view's bytes are made once and kept.
 */
#include "d3d12_private.h"

#define PAGE_HEAPS 65536

/* ------------------------------------------------------------------ the heap table */

/* heaps by id, in pages added as ids grow, so a handle finds its heap without a lock. */
static D12DescriptorHeap **heap_slot(D12Device *device, UINT id, BOOL make)
{
    UINT page = id / PAGE_HEAPS;
    if (page >= 256)
        return nullptr;
    D12DescriptorHeap ***pages = (D12DescriptorHeap ***)device->heaps;
    if (!pages[page]) {
        if (!make)
            return nullptr;
        pages[page] = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, PAGE_HEAPS * sizeof(D12DescriptorHeap *));
        if (!pages[page])
            return nullptr;
    }
    return &pages[page][id % PAGE_HEAPS];
}

static UINT heap_register(D12Device *device, D12DescriptorHeap *heap)
{
    AcquireSRWLockExclusive(&device->heap_lock);
    UINT id = device->heap_free_count ? device->heap_free_ids[--device->heap_free_count] : device->heap_next_id;
    D12DescriptorHeap **slot = id < D12_MAX_HEAPS ? heap_slot(device, id, TRUE) : nullptr;
    if (slot) {
        *slot = heap;
        if (id == device->heap_next_id)
            device->heap_next_id++;
    } else {
        id = 0;
    }
    ReleaseSRWLockExclusive(&device->heap_lock);
    return id;
}

static void heap_unregister(D12Device *device, UINT id)
{
    AcquireSRWLockExclusive(&device->heap_lock);
    D12DescriptorHeap **slot = heap_slot(device, id, FALSE);
    if (slot)
        *slot = nullptr;
    if (device->heap_free_count == device->heap_free_capacity) {
        UINT capacity = device->heap_free_capacity ? device->heap_free_capacity * 2 : 64;
        UINT *ids = device->heap_free_ids
                        ? HeapReAlloc(GetProcessHeap(), 0, device->heap_free_ids, capacity * sizeof(UINT))
                        : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(UINT));
        if (ids) {
            device->heap_free_ids = ids;
            device->heap_free_capacity = capacity;
        }
    }
    if (device->heap_free_count < device->heap_free_capacity)
        device->heap_free_ids[device->heap_free_count++] = id;
    ReleaseSRWLockExclusive(&device->heap_lock);
}

D12DescriptorHeap *D12HandleHeap(D12Device *device, UINT64 ptr, UINT *index)
{
    UINT id = (UINT)(ptr >> D12_HANDLE_SHIFT);
    D12DescriptorHeap **slot = heap_slot(device, id, FALSE);
    D12DescriptorHeap *heap = slot ? *slot : nullptr;
    if (!heap)
        return nullptr;
    *index = (UINT)((ptr & (((UINT64)1 << D12_HANDLE_SHIFT) - 1)) >> D12_INCREMENT_SHIFT);
    return *index < heap->desc.NumDescriptors ? heap : nullptr;
}

/* ------------------------------------------------------------------ descriptor bytes */

static UINT descriptor_size(D12Device *device, VkDescriptorType type)
{
    const VkPhysicalDeviceDescriptorBufferPropertiesEXT *p = &device->db;
    switch (type) {
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        return (UINT)p->sampledImageDescriptorSize;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        return (UINT)p->storageImageDescriptorSize;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        return (UINT)p->robustUniformTexelBufferDescriptorSize;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        return (UINT)p->robustStorageTexelBufferDescriptorSize;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        return (UINT)p->robustStorageBufferDescriptorSize;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        return (UINT)p->samplerDescriptorSize;
    default:
        return 0;
    }
}

static void image_descriptor(D12Device *device, VkDescriptorType type, VkImageView view, BYTE *out)
{
    VkDescriptorImageInfo image = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorGetInfoEXT info = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    info.type = type;
    if (type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
        info.data.pSampledImage = view ? &image : nullptr;
    else
        info.data.pStorageImage = view ? &image : nullptr;
    device->vk.vkGetDescriptorEXT(device->vk.device, &info, descriptor_size(device, type), out);
}

static void address_descriptor(D12Device *device, VkDescriptorType type, VkDeviceAddress address,
                               VkDeviceSize range, VkFormat format, BYTE *out)
{
    VkDescriptorAddressInfoEXT addr = {VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT};
    addr.address = address;
    addr.range = range;
    addr.format = format;
    VkDescriptorGetInfoEXT info = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    info.type = type;
    if (type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)
        info.data.pUniformTexelBuffer = address ? &addr : nullptr;
    else if (type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
        info.data.pStorageTexelBuffer = address ? &addr : nullptr;
    else
        info.data.pStorageBuffer = address ? &addr : nullptr;
    device->vk.vkGetDescriptorEXT(device->vk.device, &info, descriptor_size(device, type), out);
}

/* the descriptor of a typed buffer view, cached by address, range and format. */
static void texel_descriptor(D12Device *device, BOOL storage, VkDeviceAddress address, VkDeviceSize range,
                             VkFormat format, BYTE *out)
{
    AcquireSRWLockExclusive(&device->texel_lock);
    for (UINT i = device->texel_count; i-- > 0;) {
        D12TexelEntry *e = &device->texels[i];
        if (e->address == address && e->range == range && e->format == format && e->storage == storage) {
            memcpy(out, e->descriptor, sizeof(e->descriptor));
            ReleaseSRWLockExclusive(&device->texel_lock);
            return;
        }
    }
    address_descriptor(device,
                       storage ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                       address, range, format, out);
    if (device->texel_count == device->texel_capacity) {
        UINT capacity = device->texel_capacity ? device->texel_capacity * 2 : 256;
        D12TexelEntry *list = device->texels
                                  ? HeapReAlloc(GetProcessHeap(), 0, device->texels, capacity * sizeof(*list))
                                  : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
        if (!list) {
            ReleaseSRWLockExclusive(&device->texel_lock);
            return;
        }
        device->texels = list;
        device->texel_capacity = capacity;
    }
    D12TexelEntry *e = &device->texels[device->texel_count++];
    e->address = address;
    e->range = range;
    e->format = format;
    e->storage = (BYTE)storage;
    memcpy(e->descriptor, out, sizeof(e->descriptor));
    ReleaseSRWLockExclusive(&device->texel_lock);
}

static VkSamplerAddressMode address_mode(D3D12_TEXTURE_ADDRESS_MODE mode)
{
    switch (mode) {
    case D3D12_TEXTURE_ADDRESS_MODE_MIRROR:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case D3D12_TEXTURE_ADDRESS_MODE_CLAMP:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case D3D12_TEXTURE_ADDRESS_MODE_BORDER:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE:
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static VkSampler create_sampler(D12Device *device, const D3D12_SAMPLER_DESC2 *d)
{
    VkSamplerCustomBorderColorCreateInfoEXT border = {VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
    VkSamplerReductionModeCreateInfo reduction = {VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO};
    VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    UINT filter = d->Filter;
    info.minFilter = filter & 0x10 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.magFilter = filter & 0x04 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.mipmapMode = filter & 0x01 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = address_mode(d->AddressU);
    info.addressModeV = address_mode(d->AddressV);
    info.addressModeW = address_mode(d->AddressW);
    info.mipLodBias = d->MipLODBias;
    if (filter & 0x40) {
        info.anisotropyEnable = TRUE;
        info.maxAnisotropy = (float)D12_MAX(1u, D12_MIN(d->MaxAnisotropy, 16u));
        info.minFilter = info.magFilter = VK_FILTER_LINEAR;
        if (filter & 0x01)
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    UINT kind = (filter >> 7) & 3;
    if (kind == 1) {
        info.compareEnable = TRUE;
        info.compareOp = d->ComparisonFunc >= 1 && d->ComparisonFunc <= 8 ? (VkCompareOp)(d->ComparisonFunc - 1)
                                                                          : VK_COMPARE_OP_NEVER;
    } else if (kind >= 2) {
        reduction.reductionMode = kind == 2 ? VK_SAMPLER_REDUCTION_MODE_MIN : VK_SAMPLER_REDUCTION_MODE_MAX;
        info.pNext = &reduction;
    }
    info.minLod = d->MinLOD;
    info.maxLod = d->MaxLOD >= D3D12_FLOAT32_MAX ? VK_LOD_CLAMP_NONE : d->MaxLOD;
    info.unnormalizedCoordinates = (d->Flags & D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES) != 0;
    if (d->Flags & D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR) {
        const UINT *c = d->UintBorderColor;
        if (!c[0] && !c[1] && !c[2] && !c[3])
            info.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
        else if (!c[0] && !c[1] && !c[2] && c[3] == 1)
            info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        else if (c[0] == 1 && c[1] == 1 && c[2] == 1 && c[3] == 1)
            info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        else if (device->has_custom_border) {
            info.borderColor = VK_BORDER_COLOR_INT_CUSTOM_EXT;
            memcpy(border.customBorderColor.uint32, c, sizeof(UINT) * 4);
            border.pNext = (void *)info.pNext;
            info.pNext = &border;
        } else {
            info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        }
    } else {
        const FLOAT *c = d->FloatBorderColor;
        if (c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] == 0)
            info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        else if (c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] == 1)
            info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        else if (c[0] == 1 && c[1] == 1 && c[2] == 1 && c[3] == 1)
            info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        else if (device->has_custom_border) {
            info.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
            memcpy(border.customBorderColor.float32, c, sizeof(FLOAT) * 4);
            border.pNext = (void *)info.pNext;
            info.pNext = &border;
        } else {
            info.borderColor = c[3] > 0.5f ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        }
    }
    VkSampler sampler = VK_NULL_HANDLE;
    device->vk.vkCreateSampler(device->vk.device, &info, nullptr, &sampler);
    return sampler;
}

/* samplers are made once per description: Vulkan allows few and programs make the same ones over and over. */
VkSampler D12Sampler(D12Device *device, const D3D12_SAMPLER_DESC2 *desc, const BYTE **descriptor)
{
    D3D12_SAMPLER_DESC2 key = *desc;
    AcquireSRWLockExclusive(&device->sampler_lock);
    for (UINT i = 0; i < device->sampler_count; i++) {
        D12SamplerEntry *e = &device->samplers[i];
        if (!memcmp(&e->desc, &key, sizeof(key))) {
            if (descriptor)
                *descriptor = e->descriptor;
            ReleaseSRWLockExclusive(&device->sampler_lock);
            return e->sampler;
        }
    }
    VkSampler sampler = VK_NULL_HANDLE;
    /* entries never move once handed out, so the list is sized to Vulkan's sampler limit. */
    if (!device->samplers) {
        device->sampler_capacity = device->properties.limits.maxSamplerAllocationCount;
        device->samplers = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, device->sampler_capacity * sizeof(D12SamplerEntry));
        if (!device->samplers)
            device->sampler_capacity = 0;
    }
    if (device->sampler_count < device->sampler_capacity && (sampler = create_sampler(device, &key))) {
        D12SamplerEntry *e = &device->samplers[device->sampler_count++];
        e->desc = key;
        e->sampler = sampler;
        VkDescriptorGetInfoEXT info = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
        info.type = VK_DESCRIPTOR_TYPE_SAMPLER;
        info.data.pSampler = &e->sampler;
        device->vk.vkGetDescriptorEXT(device->vk.device, &info, device->sampler_size, e->descriptor);
        if (descriptor)
            *descriptor = e->descriptor;
    } else if (descriptor) {
        *descriptor = device->null_descriptors[D12_DESC_NONE];
    }
    ReleaseSRWLockExclusive(&device->sampler_lock);
    return sampler;
}

/* ------------------------------------------------------------------ setup */

static HRESULT create_heap(D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, BOOL internal,
                           D12DescriptorHeap **out);

/*
 * without null descriptors, as on macOS, a null view is a zeroed stand in of its dimension: Metal reads resource id 0
 * as whatever texture holds that id, and a shader reads a view as the kind of texture it declares.
 */
static HRESULT null_image(D12Device *device, UINT index, VkImageType type, UINT layers, VkSampleCountFlagBits samples)
{
    D12Vk *vk = &device->vk;
    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = layers == 6 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.imageType = type;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = (VkExtent3D){1, 1, 1};
    info.mipLevels = 1;
    info.arrayLayers = layers;
    info.samples = samples;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 (samples == VK_SAMPLE_COUNT_1_BIT ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    if (vk->vkCreateImage(vk->device, &info, nullptr, &device->null_images[index]) != VK_SUCCESS)
        return E_FAIL;
    VkMemoryRequirements req;
    vk->vkGetImageMemoryRequirements(vk->device, device->null_images[index], &req);
    HRESULT hr = D12Alloc(device, D12_MEMORY_DEVICE, req.memoryTypeBits, req.size, req.alignment, TRUE,
                          &device->null_image_memory[index]);
    if (FAILED(hr))
        return hr;
    D12Allocation *a = &device->null_image_memory[index];
    return vk->vkBindImageMemory(vk->device, device->null_images[index], a->heap->memory, a->offset) == VK_SUCCESS
               ? S_OK
               : E_FAIL;
}

static HRESULT null_views(D12Device *device)
{
    D12Vk *vk = &device->vk;
    for (UINT i = 0; i < D12_NULL_DIMENSIONS; i++) {
        memcpy(device->null_views[0][i], device->null_descriptors[D12_DESC_SAMPLED_IMAGE], 64);
        memcpy(device->null_views[1][i], device->null_descriptors[D12_DESC_STORAGE_IMAGE], 64);
    }
    memcpy(device->null_views[0][D3D12_SRV_DIMENSION_BUFFER], device->null_descriptors[D12_DESC_UNIFORM_TEXEL], 64);
    memcpy(device->null_views[1][D3D12_UAV_DIMENSION_BUFFER], device->null_descriptors[D12_DESC_STORAGE_TEXEL], 64);
    if (device->has_null_descriptor)
        return S_OK;
    HRESULT hr;
    if (FAILED(hr = null_image(device, 0, VK_IMAGE_TYPE_1D, 1, VK_SAMPLE_COUNT_1_BIT)) ||
        FAILED(hr = null_image(device, 1, VK_IMAGE_TYPE_2D, 6, VK_SAMPLE_COUNT_1_BIT)) ||
        FAILED(hr = null_image(device, 2, VK_IMAGE_TYPE_3D, 1, VK_SAMPLE_COUNT_1_BIT)) ||
        FAILED(hr = null_image(device, 3, VK_IMAGE_TYPE_2D, 1, VK_SAMPLE_COUNT_4_BIT)))
        return hr;
    /* each dimension: its image, view type and layers. the numbers are those of both kinds of view. */
    static const struct {
        UINT image;
        VkImageViewType type;
        UINT layers;
    } DIMENSIONS[D12_NULL_DIMENSIONS] = {
        [D3D12_SRV_DIMENSION_TEXTURE1D] = {0, VK_IMAGE_VIEW_TYPE_1D, 1},
        [D3D12_SRV_DIMENSION_TEXTURE1DARRAY] = {0, VK_IMAGE_VIEW_TYPE_1D_ARRAY, 1},
        [D3D12_SRV_DIMENSION_TEXTURE2D] = {1, VK_IMAGE_VIEW_TYPE_2D, 1},
        [D3D12_SRV_DIMENSION_TEXTURE2DARRAY] = {1, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 1},
        [D3D12_SRV_DIMENSION_TEXTURE2DMS] = {3, VK_IMAGE_VIEW_TYPE_2D, 1},
        [D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY] = {3, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 1},
        [D3D12_SRV_DIMENSION_TEXTURE3D] = {2, VK_IMAGE_VIEW_TYPE_3D, 1},
        [D3D12_SRV_DIMENSION_TEXTURECUBE] = {1, VK_IMAGE_VIEW_TYPE_CUBE, 6},
        [D3D12_SRV_DIMENSION_TEXTURECUBEARRAY] = {1, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, 6},
    };
    for (UINT i = 0; i < D12_NULL_DIMENSIONS; i++) {
        if (!DIMENSIONS[i].layers)
            continue;
        VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = device->null_images[DIMENSIONS[i].image];
        view.viewType = DIMENSIONS[i].type;
        view.format = VK_FORMAT_R8G8B8A8_UNORM;
        view.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, DIMENSIONS[i].layers};
        if (vk->vkCreateImageView(vk->device, &view, nullptr, &device->null_image_views[i]) != VK_SUCCESS)
            return E_FAIL;
        image_descriptor(device, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, device->null_image_views[i], device->null_views[0][i]);
        /* a multisampled image has no storage view, the unordered access view of that number is another. */
        if (DIMENSIONS[i].image != 3)
            image_descriptor(device, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, device->null_image_views[i],
                             device->null_views[1][i]);
    }
    texel_descriptor(device, FALSE, device->null_address, 256, VK_FORMAT_R32_UINT,
                     device->null_views[0][D3D12_SRV_DIMENSION_BUFFER]);
    texel_descriptor(device, TRUE, device->null_address, 256, VK_FORMAT_R32_UINT,
                     device->null_views[1][D3D12_UAV_DIMENSION_BUFFER]);
    /* the images enter the general layout, and the buffer the texel views read is zeroed. */
    VkCommandBuffer cmd = D12InitCommands(device);
    VkImageMemoryBarrier2 barriers[D12_NULL_IMAGES];
    for (UINT i = 0; i < D12_NULL_IMAGES; i++) {
        barriers[i] = (VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barriers[i].dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barriers[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[i].srcQueueFamilyIndex = barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].image = device->null_images[i];
        barriers[i].subresourceRange =
            (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS};
    }
    VkDependencyInfo dependency = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = D12_NULL_IMAGES;
    dependency.pImageMemoryBarriers = barriers;
    vk->vkCmdPipelineBarrier2(cmd, &dependency);
    vk->vkCmdFillBuffer(cmd, device->null_buffer.heap->buffer, device->null_buffer.offset, 256, 0);
    D12InitDone(device);
    return S_OK;
}

HRESULT D12DescriptorsInit(D12Device *device)
{
    D12Vk *vk = &device->vk;
    device->heaps = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 256 * sizeof(void *));
    if (!device->heaps)
        return E_OUTOFMEMORY;
    /* id 0 means no heap, so no handle is 0. */
    device->heap_next_id = 1;

    const VkPhysicalDeviceLimits *limits = &device->properties.limits;
    UINT count = 1000000;
    count = D12_MIN(count, limits->maxDescriptorSetSampledImages);
    count = D12_MIN(count, limits->maxDescriptorSetStorageImages);
    count = D12_MIN(count, limits->maxPerStageDescriptorSampledImages);
    count = D12_MIN(count, limits->maxPerStageDescriptorStorageImages);

    static const VkDescriptorType mutable_types[] = {
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
    };
    VkMutableDescriptorTypeListEXT list = {RTL_NUMBER_OF(mutable_types), mutable_types};
    VkMutableDescriptorTypeCreateInfoEXT mutable_info = {VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT};
    mutable_info.mutableDescriptorTypeListCount = 1;
    mutable_info.pMutableDescriptorTypeLists = &list;
    /* variable count bindings, so heaps of any size fit. */
    VkDescriptorBindingFlags partial =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, &mutable_info};
    binding_flags.bindingCount = 1;
    binding_flags.pBindingFlags = &partial;
    VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_MUTABLE_EXT, count, VK_SHADER_STAGE_ALL, nullptr};
    VkDescriptorSetLayoutCreateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &binding_flags};
    info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    info.bindingCount = 1;
    info.pBindings = &binding;
    if (vk->vkCreateDescriptorSetLayout(vk->device, &info, nullptr, &device->resource_layout) != VK_SUCCESS)
        return E_FAIL;

    VkDeviceSize layout_size = 0;
    vk->vkGetDescriptorSetLayoutSizeEXT(vk->device, device->resource_layout, &layout_size);

    binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    binding.descriptorCount = D12_MIN(D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE, limits->maxDescriptorSetSamplers);
    binding_flags.pNext = nullptr;
    if (vk->vkCreateDescriptorSetLayout(vk->device, &info, nullptr, &device->sampler_layout) != VK_SUCCESS)
        return E_FAIL;

    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    partial = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    if (vk->vkCreateDescriptorSetLayout(vk->device, &info, nullptr, &device->record_layout) != VK_SUCCESS)
        return E_FAIL;

    /* 64-bit texel views, which typed views' 64-bit atomics go through. */
    if (device->has_atomic64) {
        static const VkDescriptorType atomic_types[] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                        VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER};
        VkMutableDescriptorTypeListEXT atomic_list = {RTL_NUMBER_OF(atomic_types), atomic_types};
        mutable_info.pMutableDescriptorTypeLists = &atomic_list;
        binding_flags.pNext = &mutable_info;
        partial = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
        binding.descriptorCount = count;
        VkDeviceSize atomic_size = 0;
        if (vk->vkCreateDescriptorSetLayout(vk->device, &info, nullptr, &device->atomic_layout) != VK_SUCCESS)
            return E_FAIL;
        vk->vkGetDescriptorSetLayoutSizeEXT(vk->device, device->atomic_layout, &atomic_size);
        device->atomic_stride = (UINT)(atomic_size / count);
        if (!device->atomic_stride || device->atomic_stride > 64)
            return DXGI_ERROR_UNSUPPORTED;
    }

    info.pNext = nullptr;
    info.bindingCount = 0;
    if (vk->vkCreateDescriptorSetLayout(vk->device, &info, nullptr, &device->empty_layout) != VK_SUCCESS)
        return E_FAIL;

    /* a mutable descriptor is as large as the largest type it may hold, per the layout's size. */
    UINT stride = 0;
    for (UINT i = 0; i < RTL_NUMBER_OF(mutable_types); i++)
        stride = D12_MAX(stride, descriptor_size(device, mutable_types[i]));
    if (layout_size && layout_size / count != stride)
        D12Log("d3d12: mutable descriptors take %u bytes, not %u", (UINT)(layout_size / count), stride);
    if (layout_size)
        stride = (UINT)(layout_size / count);
    device->mutable_stride = stride;
    UINT align = (UINT)D12_MAX(device->db.descriptorBufferOffsetAlignment, 16);
    device->record_offset = (UINT)D12_ALIGN(descriptor_size(device, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), align);
    device->sampler_size = descriptor_size(device, VK_DESCRIPTOR_TYPE_SAMPLER);
    if (stride > 64 || device->sampler_size > 64 || device->record_offset > 4096) {
        D12Log("d3d12: descriptors larger than expected: %u bytes", stride);
        return DXGI_ERROR_UNSUPPORTED;
    }

    image_descriptor(device, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE,
                     device->null_descriptors[D12_DESC_SAMPLED_IMAGE]);
    image_descriptor(device, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE,
                     device->null_descriptors[D12_DESC_STORAGE_IMAGE]);
    address_descriptor(device, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 0, 0, VK_FORMAT_UNDEFINED,
                       device->null_descriptors[D12_DESC_UNIFORM_TEXEL]);
    address_descriptor(device, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 0, 0, VK_FORMAT_UNDEFINED,
                       device->null_descriptors[D12_DESC_STORAGE_TEXEL]);
    memcpy(device->null_descriptors[D12_DESC_NONE], device->null_descriptors[D12_DESC_SAMPLED_IMAGE], 64);

    /* what null views' records point to: zeros, which the stand ins of null texel views read. */
    HRESULT hr = D12Alloc(device, D12_MEMORY_DEVICE, ~0u, 256, 256, FALSE, &device->null_buffer);
    if (FAILED(hr))
        return hr;
    device->null_address = device->null_buffer.heap->va + device->null_buffer.offset;
    if (FAILED(hr = null_views(device)))
        return hr;
    for (UINT i = 0; i < 2; i++) {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {i ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                           1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 1};
        if (FAILED(hr = create_heap(device, &desc, TRUE, &device->dummy_heaps[i])))
            return hr;
    }
    return S_OK;
}

static void heap_destroy(D12DescriptorHeap *heap);

void D12DescriptorsDestroy(D12Device *device)
{
    D12Vk *vk = &device->vk;
    for (UINT i = 0; i < D12_NULL_DIMENSIONS; i++)
        if (device->null_image_views[i])
            vk->vkDestroyImageView(vk->device, device->null_image_views[i], nullptr);
    for (UINT i = 0; i < D12_NULL_IMAGES; i++)
        if (device->null_images[i]) {
            vk->vkDestroyImage(vk->device, device->null_images[i], nullptr);
            D12Free(device, &device->null_image_memory[i]);
        }
    for (UINT i = 0; i < 2; i++)
        if (device->dummy_heaps[i]) {
            heap_destroy(device->dummy_heaps[i]);
            HeapFree(GetProcessHeap(), 0, device->dummy_heaps[i]);
        }
    for (UINT i = 0; i < device->sampler_count; i++)
        vk->vkDestroySampler(vk->device, device->samplers[i].sampler, nullptr);
    HeapFree(GetProcessHeap(), 0, device->samplers);
    HeapFree(GetProcessHeap(), 0, device->texels);
    vk->vkDestroyDescriptorSetLayout(vk->device, device->resource_layout, nullptr);
    vk->vkDestroyDescriptorSetLayout(vk->device, device->sampler_layout, nullptr);
    vk->vkDestroyDescriptorSetLayout(vk->device, device->record_layout, nullptr);
    if (device->atomic_layout)
        vk->vkDestroyDescriptorSetLayout(vk->device, device->atomic_layout, nullptr);
    vk->vkDestroyDescriptorSetLayout(vk->device, device->empty_layout, nullptr);
    if (device->heaps) {
        D12DescriptorHeap ***pages = (D12DescriptorHeap ***)device->heaps;
        for (UINT i = 0; i < 256; i++)
            HeapFree(GetProcessHeap(), 0, pages[i]);
        HeapFree(GetProcessHeap(), 0, device->heaps);
    }
    HeapFree(GetProcessHeap(), 0, device->heap_free_ids);
}

/* ------------------------------------------------------------------ ID3D12DescriptorHeap */

D12DescriptorHeap *D12DescriptorHeapFrom(ID3D12DescriptorHeap *iface)
{
    return iface ? CONTAINING_RECORD(iface, D12DescriptorHeap, iface) : nullptr;
}

static D12DescriptorHeap *impl(ID3D12DescriptorHeap *iface)
{
    return CONTAINING_RECORD(iface, D12DescriptorHeap, iface);
}

static HRESULT STDMETHODCALLTYPE heap_query(ID3D12DescriptorHeap *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceChild) || IsEqualGUID(riid, &IID_ID3D12Pageable) ||
        IsEqualGUID(riid, &IID_ID3D12DescriptorHeap)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE heap_add_ref(ID3D12DescriptorHeap *iface)
{
    return InterlockedIncrement(&impl(iface)->child.refs);
}

static void heap_destroy(D12DescriptorHeap *heap)
{
    D12Device *device = heap->child.device;
    if (heap->id)
        heap_unregister(device, heap->id);
    if (heap->buffer)
        device->vk.vkDestroyBuffer(device->vk.device, heap->buffer, nullptr);
    if (heap->memory)
        device->vk.vkFreeMemory(device->vk.device, heap->memory, nullptr);
    else
        VirtualFree(heap->base, 0, MEM_RELEASE);
    HeapFree(GetProcessHeap(), 0, heap->infos);
    HeapFree(GetProcessHeap(), 0, heap->targets);
}

static ULONG STDMETHODCALLTYPE heap_release(ID3D12DescriptorHeap *iface)
{
    D12DescriptorHeap *heap = impl(iface);
    ULONG refs = InterlockedDecrement(&heap->child.refs);
    if (!refs) {
        heap_destroy(heap);
        D12ChildRelease(&heap->child);
        HeapFree(GetProcessHeap(), 0, heap);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE heap_get_private_data(ID3D12DescriptorHeap *iface, REFGUID guid, UINT *size,
                                                       void *data)
{
    return MimePrivateGet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE heap_set_private_data(ID3D12DescriptorHeap *iface, REFGUID guid, UINT size,
                                                       const void *data)
{
    return MimePrivateSet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE heap_set_private_data_interface(ID3D12DescriptorHeap *iface, REFGUID guid,
                                                                 const IUnknown *data)
{
    return MimePrivateSetInterface(&impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE heap_set_name(ID3D12DescriptorHeap *iface, LPCWSTR name) { return S_OK; }

static HRESULT STDMETHODCALLTYPE heap_get_device(ID3D12DescriptorHeap *iface, REFIID riid, void **device)
{
    return D12ChildGetDevice(&impl(iface)->child, riid, device);
}

static D3D12_DESCRIPTOR_HEAP_DESC *STDMETHODCALLTYPE heap_get_desc(ID3D12DescriptorHeap *iface,
                                                                   D3D12_DESCRIPTOR_HEAP_DESC *ret)
{
    *ret = impl(iface)->desc;
    return ret;
}

static D3D12_CPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE heap_get_cpu_start(ID3D12DescriptorHeap *iface,
                                                                         D3D12_CPU_DESCRIPTOR_HANDLE *ret)
{
    ret->ptr = (SIZE_T)impl(iface)->id << D12_HANDLE_SHIFT;
    return ret;
}

static D3D12_GPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE heap_get_gpu_start(ID3D12DescriptorHeap *iface,
                                                                         D3D12_GPU_DESCRIPTOR_HANDLE *ret)
{
    D12DescriptorHeap *heap = impl(iface);
    ret->ptr = heap->desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE ? (UINT64)heap->id << D12_HANDLE_SHIFT : 0;
    return ret;
}

static const struct ID3D12DescriptorHeapVtbl g_heap_vtbl = {
    heap_query,
    heap_add_ref,
    heap_release,
    heap_get_private_data,
    heap_set_private_data,
    heap_set_private_data_interface,
    heap_set_name,
    heap_get_device,
    heap_get_desc,
    heap_get_cpu_start,
    heap_get_gpu_start,
};

/* guest memory the gpu reads descriptors from, with a buffer over it whose address shaders know. */
static HRESULT descriptor_buffer(D12Device *device, D12DescriptorHeap *heap, UINT64 size, BOOL samplers)
{
    D12Vk *vk = &device->vk;
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                 (samplers ? VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT
                           : VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (vk->vkCreateBuffer(vk->device, &info, nullptr, &heap->buffer) != VK_SUCCESS)
        return E_OUTOFMEMORY;
    VkMemoryRequirements req;
    vk->vkGetBufferMemoryRequirements(vk->device, heap->buffer, &req);
    int type = -1;
    for (UINT i = 0; i < device->memory.memoryTypeCount && type < 0; i++) {
        VkMemoryPropertyFlags f = device->memory.memoryTypes[i].propertyFlags;
        if ((req.memoryTypeBits & (1u << i)) &&
            (f & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            type = (int)i;
    }
    if (type < 0)
        return E_OUTOFMEMORY;
    VkMemoryAllocateFlagsInfo flags = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    if (vk->vkAllocateMemory(vk->device, &alloc, nullptr, &heap->memory) != VK_SUCCESS)
        return E_OUTOFMEMORY;
    vk->vkBindBufferMemory(vk->device, heap->buffer, heap->memory, 0);
    if (vk->vkMapMemory(vk->device, heap->memory, 0, VK_WHOLE_SIZE, 0, (void **)&heap->base) != VK_SUCCESS)
        return E_OUTOFMEMORY;
    VkBufferDeviceAddressInfo address = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    address.buffer = heap->buffer;
    heap->address = vk->vkGetBufferDeviceAddress(vk->device, &address);
    return S_OK;
}

static HRESULT create_heap(D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, BOOL internal,
                           D12DescriptorHeap **out)
{
    BOOL visible = (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
    if (desc->Type >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES || !desc->NumDescriptors ||
        (visible && desc->Type >= D3D12_DESCRIPTOR_HEAP_TYPE_RTV) ||
        (visible && desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER &&
         desc->NumDescriptors > D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE) ||
        (UINT64)desc->NumDescriptors > ((UINT64)1 << (D12_HANDLE_SHIFT - D12_INCREMENT_SHIFT)) - 1)
        return E_INVALIDARG;
    D12DescriptorHeap *heap = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*heap));
    if (!heap)
        return E_OUTOFMEMORY;
    heap->iface.lpVtbl = &g_heap_vtbl;
    heap->child.refs = 1;
    heap->child.device = device;
    heap->desc = *desc;
    if (!heap->desc.NodeMask)
        heap->desc.NodeMask = 1;
    UINT n = desc->NumDescriptors;
    HRESULT hr = S_OK;
    UINT64 size = 0, records = 0;
    switch (desc->Type) {
    case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
        heap->stride = device->mutable_stride;
        records = D12_ALIGN(device->record_offset + (UINT64)n * heap->stride, 64);
        size = records + (UINT64)n * sizeof(DxbcRecord);
        if (device->atomic_stride) {
            heap->atomic_offset = D12_ALIGN(size, D12_MAX(device->db.descriptorBufferOffsetAlignment, 64));
            size = heap->atomic_offset + (UINT64)n * device->atomic_stride;
        }
        break;
    case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
        heap->stride = device->sampler_size;
        size = (UINT64)n * heap->stride;
        break;
    default:
        heap->targets = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)n * sizeof(*heap->targets));
        if (!heap->targets)
            hr = E_OUTOFMEMORY;
        break;
    }
    if (SUCCEEDED(hr) && size) {
        if (visible)
            hr = descriptor_buffer(device, heap, size, desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        else if (!(heap->base = VirtualAlloc(nullptr, (SIZE_T)size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)))
            hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr) && desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        heap->descriptors = heap->base + device->record_offset;
        heap->records = (DxbcRecord *)(heap->base + records);
        if (heap->atomic_offset)
            heap->atomics = heap->base + heap->atomic_offset;
        if (!visible && !(heap->infos = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)n * sizeof(*heap->infos))))
            hr = E_OUTOFMEMORY;
        if (SUCCEEDED(hr) && visible)
            address_descriptor(device, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, heap->address + records,
                               (UINT64)n * sizeof(DxbcRecord), VK_FORMAT_UNDEFINED, heap->base);
        /* every descriptor starts null: its records read nothing. */
        for (UINT i = 0; SUCCEEDED(hr) && i < n; i++) {
            memcpy(heap->descriptors + (SIZE_T)i * heap->stride, device->null_descriptors[D12_DESC_SAMPLED_IMAGE],
                   heap->stride);
            heap->records[i] = (DxbcRecord){device->null_address, 0, 0, device->null_address, 0};
            if (heap->atomics)
                memcpy(heap->atomics + (SIZE_T)i * device->atomic_stride,
                       device->null_descriptors[D12_DESC_STORAGE_IMAGE], device->atomic_stride);
        }
    } else if (SUCCEEDED(hr)) {
        heap->descriptors = heap->base;
    }
    if (SUCCEEDED(hr) && !(heap->id = heap_register(device, heap)))
        hr = E_OUTOFMEMORY;
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

HRESULT D12CreateDescriptorHeap(D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, D12DescriptorHeap **out)
{
    return create_heap(device, desc, FALSE, out);
}

/* ------------------------------------------------------------------ writing descriptors */

/* a cbv, srv or uav write fills its Vulkan descriptor, its record, its 64-bit texel view and the cpu heap info. */
static void write_atomic_view(D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE dest, const BYTE *descriptor,
                              const DxbcRecord *record, const D12DescriptorInfo *info, const BYTE *atomic)
{
    UINT index;
    D12DescriptorHeap *heap = D12HandleHeap(device, dest.ptr, &index);
    if (!heap || heap->desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        return;
    memcpy(heap->descriptors + (SIZE_T)index * heap->stride, descriptor, heap->stride);
    heap->records[index] = *record;
    if (heap->atomics)
        memcpy(heap->atomics + (SIZE_T)index * device->atomic_stride,
               atomic ? atomic : device->null_descriptors[D12_DESC_STORAGE_IMAGE], device->atomic_stride);
    if (heap->infos) {
        if (info)
            heap->infos[index] = *info;
        else
            memset(&heap->infos[index], 0, sizeof(heap->infos[index]));
    }
}

static void write_view(D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE dest, const BYTE *descriptor,
                       const DxbcRecord *record, const D12DescriptorInfo *info)
{
    write_atomic_view(device, dest, descriptor, record, info, nullptr);
}

static DxbcRecord null_record(D12Device *device)
{
    return (DxbcRecord){device->null_address, 0, 0, device->null_address, 0};
}

void D12CreateCbv(D12Device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
    DxbcRecord record = null_record(device);
    if (desc && desc->BufferLocation) {
        record.address = desc->BufferLocation;
        record.size = desc->SizeInBytes;
    }
    write_view(device, dest, device->null_descriptors[D12_DESC_SAMPLED_IMAGE], &record, nullptr);
}

/* depth formats go through their depth or stencil plane. */
static BOOL view_format(const D12Resource *r, DXGI_FORMAT format, BOOL attachment, UINT plane, VkFormat *vk,
                        VkImageAspectFlags *aspect, VkComponentMapping *swizzle)
{
    if (format == DXGI_FORMAT_UNKNOWN)
        format = r->desc.Format;
    const D3dFormat *f = D3dGetFormat(format);
    if (f->vk == VK_FORMAT_UNDEFINED)
        return FALSE;
    *swizzle = (VkComponentMapping){0};
    *aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    *vk = f->vk;
    if (r->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
        if (!f->depth)
            return FALSE;
        *vk = f->depth;
        if (attachment) {
            *aspect = r->aspect;
        } else if ((f->flags & D3D_FORMAT_STENCIL && !(f->flags & D3D_FORMAT_DEPTH)) || plane == 1) {
            /* stencil reads in green, like Direct3D's G8. */
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

/* the srv's component mapping, applied on top of the format's swizzle. */
static VkComponentMapping apply_mapping(VkComponentMapping base, UINT mapping)
{
    const VkComponentSwizzle *b = &base.r;
    VkComponentSwizzle from[4];
    for (UINT i = 0; i < 4; i++)
        from[i] = b[i] == VK_COMPONENT_SWIZZLE_IDENTITY ? (VkComponentSwizzle)(VK_COMPONENT_SWIZZLE_R + i) : b[i];
    VkComponentSwizzle out[4];
    for (UINT i = 0; i < 4; i++) {
        UINT c = D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(i, mapping);
        out[i] = c < 4 ? from[c]
                 : c == D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0 ? VK_COMPONENT_SWIZZLE_ZERO
                                                                     : VK_COMPONENT_SWIZZLE_ONE;
    }
    return (VkComponentMapping){out[0], out[1], out[2], out[3]};
}

/* a null description means the whole texture in its format. */
static void default_srv(const D12Resource *r, D3D12_SHADER_RESOURCE_VIEW_DESC *d)
{
    memset(d, 0, sizeof(*d));
    d->Format = r->desc.Format;
    d->Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    BOOL array = r->layers > 1, ms = r->samples > 1;
    switch (r->desc.Dimension) {
    case D3D12_RESOURCE_DIMENSION_BUFFER:
        d->ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        d->ViewDimension = array ? D3D12_SRV_DIMENSION_TEXTURE1DARRAY : D3D12_SRV_DIMENSION_TEXTURE1D;
        d->Texture1DArray.MipLevels = ~0u;
        d->Texture1DArray.ArraySize = r->layers;
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
        d->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        d->Texture3D.MipLevels = ~0u;
        break;
    default:
        if (ms) {
            d->ViewDimension = array ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D12_SRV_DIMENSION_TEXTURE2DMS;
            d->Texture2DMSArray.ArraySize = r->layers;
        } else {
            d->ViewDimension = array ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
            d->Texture2DArray.MipLevels = ~0u;
            d->Texture2DArray.ArraySize = r->layers;
        }
        break;
    }
}

/* fetched from the host once per view and type. */
static const BYTE *view_descriptor(D12Device *device, D12ImageView *view, BOOL storage)
{
    if (!view->have[storage]) {
        image_descriptor(device, storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                         view->view, view->descriptor[storage]);
        view->have[storage] = TRUE;
    }
    return view->descriptor[storage];
}

/* a structure, a raw dword or a texel of the format. */
static UINT element_size(DXGI_FORMAT format, UINT stride, BOOL raw)
{
    if (stride)
        return stride;
    if (raw)
        return 4;
    const D3dFormat *f = D3dGetFormat(format);
    return f->vk != VK_FORMAT_UNDEFINED && f->block == 1 ? f->block_bytes : 0;
}

void D12CreateSrv(D12Device *device, D12Resource *resource, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
                  D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
    DxbcRecord record = null_record(device);
    /* shaders read the address of an acceleration structure from its record. */
    if (desc && desc->ViewDimension == D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE) {
        record.address = desc->RaytracingAccelerationStructure.Location;
        write_view(device, dest, device->null_descriptors[D12_DESC_UNIFORM_TEXEL], &record, nullptr);
        return;
    }
    if (!resource) {
        UINT dimension = desc && desc->ViewDimension < D12_NULL_DIMENSIONS ? desc->ViewDimension : 0;
        write_view(device, dest, device->null_views[0][dimension], &record, nullptr);
        return;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC d;
    if (desc)
        d = *desc;
    else
        default_srv(resource, &d);
    if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        if (d.ViewDimension != D3D12_SRV_DIMENSION_BUFFER)
            return;
        BOOL raw = (d.Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW) != 0;
        UINT element = element_size(d.Format, d.Buffer.StructureByteStride, raw);
        if (!element)
            return;
        record.address = resource->va + d.Buffer.FirstElement * element;
        record.size = d.Buffer.NumElements * element;
        record.stride = d.Buffer.StructureByteStride;
        const BYTE *descriptor = device->null_descriptors[D12_DESC_UNIFORM_TEXEL];
        BYTE copy[64];
        if (!raw && !d.Buffer.StructureByteStride) {
            texel_descriptor(device, FALSE, record.address, record.size, D3dGetFormat(d.Format)->vk, copy);
            descriptor = copy;
        }
        D12DescriptorInfo info = {resource, nullptr, VK_FORMAT_UNDEFINED, {0}, record.address - resource->va, record.size,
                                  D12_DESC_UNIFORM_TEXEL};
        write_view(device, dest, descriptor, &record, &info);
        return;
    }
    UINT mip = 0, mips = 1, layer = 0, layers = 1, plane = 0;
    FLOAT min_lod = 0;
    VkImageViewType type;
    switch (d.ViewDimension) {
    case D3D12_SRV_DIMENSION_TEXTURE1D:
        type = VK_IMAGE_VIEW_TYPE_1D, mip = d.Texture1D.MostDetailedMip, mips = d.Texture1D.MipLevels;
        min_lod = d.Texture1D.ResourceMinLODClamp;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
        type = VK_IMAGE_VIEW_TYPE_1D_ARRAY, mip = d.Texture1DArray.MostDetailedMip, mips = d.Texture1DArray.MipLevels;
        layer = d.Texture1DArray.FirstArraySlice, layers = d.Texture1DArray.ArraySize;
        min_lod = d.Texture1DArray.ResourceMinLODClamp;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2D:
        type = VK_IMAGE_VIEW_TYPE_2D, mip = d.Texture2D.MostDetailedMip, mips = d.Texture2D.MipLevels;
        plane = d.Texture2D.PlaneSlice, min_lod = d.Texture2D.ResourceMinLODClamp;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
        type = VK_IMAGE_VIEW_TYPE_2D_ARRAY, mip = d.Texture2DArray.MostDetailedMip, mips = d.Texture2DArray.MipLevels;
        layer = d.Texture2DArray.FirstArraySlice, layers = d.Texture2DArray.ArraySize;
        plane = d.Texture2DArray.PlaneSlice, min_lod = d.Texture2DArray.ResourceMinLODClamp;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2DMS:
        type = VK_IMAGE_VIEW_TYPE_2D;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
        type = VK_IMAGE_VIEW_TYPE_2D_ARRAY, layer = d.Texture2DMSArray.FirstArraySlice;
        layers = d.Texture2DMSArray.ArraySize;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE3D:
        type = VK_IMAGE_VIEW_TYPE_3D, mip = d.Texture3D.MostDetailedMip, mips = d.Texture3D.MipLevels;
        min_lod = d.Texture3D.ResourceMinLODClamp;
        break;
    case D3D12_SRV_DIMENSION_TEXTURECUBE:
        type = VK_IMAGE_VIEW_TYPE_CUBE, mip = d.TextureCube.MostDetailedMip, mips = d.TextureCube.MipLevels;
        layers = 6, min_lod = d.TextureCube.ResourceMinLODClamp;
        break;
    case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
        type = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, mip = d.TextureCubeArray.MostDetailedMip;
        mips = d.TextureCubeArray.MipLevels, layer = d.TextureCubeArray.First2DArrayFace;
        layers = d.TextureCubeArray.NumCubes * 6, min_lod = d.TextureCubeArray.ResourceMinLODClamp;
        break;
    default:
        return;
    }
    (void)min_lod;
    if (mips == ~0u)
        mips = resource->mips - mip;
    if (layers == ~0u)
        layers = resource->layers - layer;
    if (!mips || !layers || mip + mips > resource->mips || layer + layers > resource->layers)
        return;
    VkImageViewCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VkImageAspectFlags aspect;
    VkComponentMapping swizzle;
    if (!view_format(resource, d.Format, FALSE, plane, &info.format, &aspect, &swizzle))
        return;
    info.viewType = type;
    info.components = apply_mapping(swizzle, d.Shader4ComponentMapping);
    info.subresourceRange = (VkImageSubresourceRange){aspect, mip, mips, layer, layers};
    D12ImageView *view = D12ResourceView(resource, &info, VK_IMAGE_USAGE_SAMPLED_BIT);
    if (!view)
        return;
    D12DescriptorInfo di = {resource, view, info.format, info.subresourceRange, 0, 0, D12_DESC_SAMPLED_IMAGE};
    write_view(device, dest, view_descriptor(device, view, FALSE), &record, &di);
}

void D12CreateUav(D12Device *device, D12Resource *resource, D12Resource *counter,
                  const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
    DxbcRecord record = null_record(device);
    if (!resource) {
        UINT dimension = desc && desc->ViewDimension < D12_NULL_DIMENSIONS ? desc->ViewDimension : 0;
        write_view(device, dest, device->null_views[1][dimension], &record, nullptr);
        return;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC d;
    if (desc) {
        d = *desc;
    } else {
        memset(&d, 0, sizeof(d));
        d.Format = resource->desc.Format;
        if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        else if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
            d.ViewDimension = resource->layers > 1 ? D3D12_UAV_DIMENSION_TEXTURE1DARRAY : D3D12_UAV_DIMENSION_TEXTURE1D;
        else if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
            d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D, d.Texture3D.WSize = ~0u;
        else
            d.ViewDimension = resource->layers > 1 ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
        d.Texture2DArray.ArraySize = resource->layers;
    }
    if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        if (d.ViewDimension != D3D12_UAV_DIMENSION_BUFFER)
            return;
        BOOL raw = (d.Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW) != 0;
        UINT element = element_size(d.Format, d.Buffer.StructureByteStride, raw);
        if (!element)
            return;
        record.address = resource->va + d.Buffer.FirstElement * element;
        record.size = d.Buffer.NumElements * element;
        record.stride = d.Buffer.StructureByteStride;
        if (counter)
            record.counter = counter->va + d.Buffer.CounterOffsetInBytes;
        const BYTE *descriptor = device->null_descriptors[D12_DESC_STORAGE_TEXEL];
        BYTE copy[64];
        VkFormat format = VK_FORMAT_R32_UINT;
        BYTE atomic[64];
        BOOL wide = FALSE;
        if (!raw && !d.Buffer.StructureByteStride) {
            format = D3dGetFormat(d.Format)->vk;
            texel_descriptor(device, TRUE, record.address, record.size, format, copy);
            descriptor = copy;
            if (device->atomic_stride && (format == VK_FORMAT_R32G32_UINT || format == VK_FORMAT_R32G32_SINT)) {
                texel_descriptor(device, TRUE, record.address, record.size,
                                 format == VK_FORMAT_R32G32_UINT ? VK_FORMAT_R64_UINT : VK_FORMAT_R64_SINT, atomic);
                wide = TRUE;
            }
        }
        D12DescriptorInfo info = {resource, nullptr, format, {0}, record.address - resource->va, record.size,
                                  D12_DESC_STORAGE_TEXEL};
        write_atomic_view(device, dest, descriptor, &record, &info, wide ? atomic : nullptr);
        return;
    }
    UINT mip = 0, layer = 0, layers = 1, plane = 0;
    VkImageViewType type;
    switch (d.ViewDimension) {
    case D3D12_UAV_DIMENSION_TEXTURE1D:
        type = VK_IMAGE_VIEW_TYPE_1D, mip = d.Texture1D.MipSlice;
        break;
    case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
        type = VK_IMAGE_VIEW_TYPE_1D_ARRAY, mip = d.Texture1DArray.MipSlice;
        layer = d.Texture1DArray.FirstArraySlice, layers = d.Texture1DArray.ArraySize;
        break;
    case D3D12_UAV_DIMENSION_TEXTURE2D:
        type = VK_IMAGE_VIEW_TYPE_2D, mip = d.Texture2D.MipSlice, plane = d.Texture2D.PlaneSlice;
        break;
    case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
        type = VK_IMAGE_VIEW_TYPE_2D_ARRAY, mip = d.Texture2DArray.MipSlice;
        layer = d.Texture2DArray.FirstArraySlice, layers = d.Texture2DArray.ArraySize;
        plane = d.Texture2DArray.PlaneSlice;
        break;
    case D3D12_UAV_DIMENSION_TEXTURE2DMS:
        type = VK_IMAGE_VIEW_TYPE_2D;
        break;
    case D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY:
        type = VK_IMAGE_VIEW_TYPE_2D_ARRAY, layer = d.Texture2DMSArray.FirstArraySlice;
        layers = d.Texture2DMSArray.ArraySize;
        break;
    case D3D12_UAV_DIMENSION_TEXTURE3D: {
        type = VK_IMAGE_VIEW_TYPE_3D, mip = d.Texture3D.MipSlice;
        break;
    }
    default:
        return;
    }
    if (layers == ~0u)
        layers = resource->layers - layer;
    if (!layers || mip >= resource->mips || layer + layers > resource->layers)
        return;
    VkImageViewCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VkImageAspectFlags aspect;
    VkComponentMapping swizzle;
    if (!view_format(resource, d.Format, FALSE, plane, &info.format, &aspect, &swizzle))
        return;
    info.viewType = type;
    info.subresourceRange = (VkImageSubresourceRange){aspect, mip, 1, layer, layers};
    D12ImageView *view = D12ResourceView(resource, &info, VK_IMAGE_USAGE_STORAGE_BIT);
    if (!view)
        return;
    /* two dwords per texel: also make a 64-bit view for 64-bit atomics. */
    const BYTE *atomic = nullptr;
    if (device->atomic_stride && (info.format == VK_FORMAT_R32G32_UINT || info.format == VK_FORMAT_R32G32_SINT)) {
        VkImageViewCreateInfo wide = info;
        wide.format = info.format == VK_FORMAT_R32G32_UINT ? VK_FORMAT_R64_UINT : VK_FORMAT_R64_SINT;
        D12ImageView *view64 = D12ResourceView(resource, &wide, VK_IMAGE_USAGE_STORAGE_BIT);
        if (view64)
            atomic = view_descriptor(device, view64, TRUE);
    }
    D12DescriptorInfo di = {resource, view, info.format, info.subresourceRange, 0, 0, D12_DESC_STORAGE_IMAGE};
    write_atomic_view(device, dest, view_descriptor(device, view, TRUE), &record, &di, atomic);
}

/* ------------------------------------------------------------------ render targets and depth stencils */

static D12TargetView *target_slot(D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                  D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    UINT index;
    D12DescriptorHeap *heap = D12HandleHeap(device, handle.ptr, &index);
    return heap && heap->desc.Type == type ? &heap->targets[index] : nullptr;
}

const D12TargetView *D12TargetFrom(D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    UINT index;
    D12DescriptorHeap *heap = D12HandleHeap(device, handle.ptr, &index);
    return heap && heap->targets ? &heap->targets[index] : nullptr;
}

const D12DescriptorInfo *D12DescriptorInfoFrom(D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    UINT index;
    D12DescriptorHeap *heap = D12HandleHeap(device, handle.ptr, &index);
    return heap && heap->infos ? &heap->infos[index] : nullptr;
}

/* slices of a volume become layers of a 2d array view. */
static BOOL attachment(D12Resource *r, UINT mip, UINT layer, UINT layers, BOOL array, BOOL one_d, D12TargetView *t,
                       VkImageViewCreateInfo *info)
{
    if (mip >= r->mips)
        return FALSE;
    VkExtent3D e = D12MipExtent(r, mip);
    if (r->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
        if (layers == ~0u || layer + layers > e.depth)
            layers = layer < e.depth ? e.depth - layer : 0;
        info->viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    } else {
        if (layers == ~0u)
            layers = r->layers - layer;
        if (layer + layers > r->layers)
            return FALSE;
        info->viewType = one_d ? (array ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D)
                               : (array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
    }
    if (!layers)
        return FALSE;
    info->subresourceRange.baseMipLevel = mip;
    info->subresourceRange.levelCount = 1;
    info->subresourceRange.baseArrayLayer = layer;
    info->subresourceRange.layerCount = layers;
    t->extent = (VkExtent2D){e.width, e.height};
    t->samples = r->samples;
    return TRUE;
}

void D12CreateRtv(D12Device *device, D12Resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC *desc,
                  D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
    D12TargetView *slot = target_slot(device, dest, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (!slot)
        return;
    memset(slot, 0, sizeof(*slot));
    if (!resource || resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return;
    D3D12_RENDER_TARGET_VIEW_DESC d;
    if (desc) {
        d = *desc;
    } else {
        memset(&d, 0, sizeof(d));
        d.Format = resource->desc.Format;
        BOOL array = resource->layers > 1;
        if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
            d.ViewDimension = array ? D3D12_RTV_DIMENSION_TEXTURE1DARRAY : D3D12_RTV_DIMENSION_TEXTURE1D;
        else if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
            d.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D, d.Texture3D.WSize = ~0u;
        else if (resource->samples > 1)
            d.ViewDimension = array ? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D12_RTV_DIMENSION_TEXTURE2DMS;
        else
            d.ViewDimension = array ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY : D3D12_RTV_DIMENSION_TEXTURE2D;
        d.Texture2DArray.ArraySize = resource->layers;
        if (d.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY)
            d.Texture2DMSArray.ArraySize = resource->layers;
    }
    VkImageViewCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VkImageAspectFlags aspect;
    VkComponentMapping swizzle;
    D12TargetView t = {};
    if (!view_format(resource, d.Format, TRUE, 0, &info.format, &aspect, &swizzle))
        return;
    info.subresourceRange.aspectMask = aspect;
    BOOL ok;
    switch (d.ViewDimension) {
    case D3D12_RTV_DIMENSION_TEXTURE1D:
        ok = attachment(resource, d.Texture1D.MipSlice, 0, 1, FALSE, TRUE, &t, &info);
        break;
    case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
        ok = attachment(resource, d.Texture1DArray.MipSlice, d.Texture1DArray.FirstArraySlice,
                        d.Texture1DArray.ArraySize, TRUE, TRUE, &t, &info);
        break;
    case D3D12_RTV_DIMENSION_TEXTURE2D:
        ok = attachment(resource, d.Texture2D.MipSlice, 0, 1, FALSE, FALSE, &t, &info);
        break;
    case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
        ok = attachment(resource, d.Texture2DArray.MipSlice, d.Texture2DArray.FirstArraySlice,
                        d.Texture2DArray.ArraySize, TRUE, FALSE, &t, &info);
        break;
    case D3D12_RTV_DIMENSION_TEXTURE2DMS:
        ok = attachment(resource, 0, 0, 1, FALSE, FALSE, &t, &info);
        break;
    case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY:
        ok = attachment(resource, 0, d.Texture2DMSArray.FirstArraySlice, d.Texture2DMSArray.ArraySize, TRUE, FALSE,
                        &t, &info);
        break;
    case D3D12_RTV_DIMENSION_TEXTURE3D:
        ok = attachment(resource, d.Texture3D.MipSlice, d.Texture3D.FirstWSlice, d.Texture3D.WSize, TRUE, FALSE, &t,
                        &info);
        break;
    default:
        ok = FALSE;
        break;
    }
    if (!ok)
        return;
    t.view = D12ResourceView(resource, &info, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    if (!t.view)
        return;
    t.resource = resource;
    t.format = info.format;
    t.range = info.subresourceRange;
    *slot = t;
}

void D12CreateDsv(D12Device *device, D12Resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
                  D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
    D12TargetView *slot = target_slot(device, dest, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    if (!slot)
        return;
    memset(slot, 0, sizeof(*slot));
    if (!resource || resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ||
        resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        return;
    D3D12_DEPTH_STENCIL_VIEW_DESC d;
    if (desc) {
        d = *desc;
    } else {
        memset(&d, 0, sizeof(d));
        d.Format = resource->desc.Format;
        BOOL array = resource->layers > 1;
        if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
            d.ViewDimension = array ? D3D12_DSV_DIMENSION_TEXTURE1DARRAY : D3D12_DSV_DIMENSION_TEXTURE1D;
        else if (resource->samples > 1)
            d.ViewDimension = array ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DMS;
        else
            d.ViewDimension = array ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY : D3D12_DSV_DIMENSION_TEXTURE2D;
        d.Texture2DArray.ArraySize = resource->layers;
        if (d.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY)
            d.Texture2DMSArray.ArraySize = resource->layers;
    }
    VkImageViewCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VkImageAspectFlags aspect;
    VkComponentMapping swizzle;
    D12TargetView t = {};
    if (!view_format(resource, d.Format, TRUE, 0, &info.format, &aspect, &swizzle))
        return;
    info.subresourceRange.aspectMask = aspect;
    BOOL ok;
    switch (d.ViewDimension) {
    case D3D12_DSV_DIMENSION_TEXTURE1D:
        ok = attachment(resource, d.Texture1D.MipSlice, 0, 1, FALSE, TRUE, &t, &info);
        break;
    case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
        ok = attachment(resource, d.Texture1DArray.MipSlice, d.Texture1DArray.FirstArraySlice,
                        d.Texture1DArray.ArraySize, TRUE, TRUE, &t, &info);
        break;
    case D3D12_DSV_DIMENSION_TEXTURE2D:
        ok = attachment(resource, d.Texture2D.MipSlice, 0, 1, FALSE, FALSE, &t, &info);
        break;
    case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
        ok = attachment(resource, d.Texture2DArray.MipSlice, d.Texture2DArray.FirstArraySlice,
                        d.Texture2DArray.ArraySize, TRUE, FALSE, &t, &info);
        break;
    case D3D12_DSV_DIMENSION_TEXTURE2DMS:
        ok = attachment(resource, 0, 0, 1, FALSE, FALSE, &t, &info);
        break;
    case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY:
        ok = attachment(resource, 0, d.Texture2DMSArray.FirstArraySlice, d.Texture2DMSArray.ArraySize, TRUE, FALSE,
                        &t, &info);
        break;
    default:
        ok = FALSE;
        break;
    }
    if (!ok)
        return;
    t.view = D12ResourceView(resource, &info, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    if (!t.view)
        return;
    t.resource = resource;
    t.format = info.format;
    t.range = info.subresourceRange;
    t.read_only_depth = (d.Flags & D3D12_DSV_FLAG_READ_ONLY_DEPTH) != 0;
    t.read_only_stencil = (d.Flags & D3D12_DSV_FLAG_READ_ONLY_STENCIL) != 0;
    *slot = t;
}

void D12CreateSampler(D12Device *device, const D3D12_SAMPLER_DESC2 *desc, D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
    UINT index;
    D12DescriptorHeap *heap = D12HandleHeap(device, dest.ptr, &index);
    if (!heap || heap->desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
        return;
    const BYTE *descriptor;
    D12Sampler(device, desc, &descriptor);
    memcpy(heap->descriptors + (SIZE_T)index * heap->stride, descriptor, heap->stride);
}

/* ------------------------------------------------------------------ copies */

void D12CopyDescriptors(D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE dest, D3D12_CPU_DESCRIPTOR_HANDLE src,
                        UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    UINT di, si;
    D12DescriptorHeap *d = D12HandleHeap(device, dest.ptr, &di), *s = D12HandleHeap(device, src.ptr, &si);
    if (!d || !s || d->desc.Type != type || s->desc.Type != type || !count)
        return;
    count = D12_MIN(count, D12_MIN(d->desc.NumDescriptors - di, s->desc.NumDescriptors - si));
    if (d->targets) {
        memmove(&d->targets[di], &s->targets[si], count * sizeof(*d->targets));
        return;
    }
    memmove(d->descriptors + (SIZE_T)di * d->stride, s->descriptors + (SIZE_T)si * s->stride, (SIZE_T)count * d->stride);
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        return;
    memmove(&d->records[di], &s->records[si], count * sizeof(DxbcRecord));
    if (d->atomics && s->atomics)
        memmove(d->atomics + (SIZE_T)di * device->atomic_stride, s->atomics + (SIZE_T)si * device->atomic_stride,
                (SIZE_T)count * device->atomic_stride);
    if (d->infos) {
        if (s->infos)
            memmove(&d->infos[di], &s->infos[si], count * sizeof(*d->infos));
        else
            memset(&d->infos[di], 0, count * sizeof(*d->infos));
    }
}
