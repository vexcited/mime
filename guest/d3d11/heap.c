/*
 * the descriptor heap: one update-after-bind array per descriptor kind, so a view's descriptor is written once when
 * the view is made, without waiting. index 0 of each array is a null descriptor, so unbound slots read zeros.
 *
 * also deferred destruction of objects the gpu may still use.
 */
#include "d3d11_private.h"

typedef struct D3dCbvEntry {
    VkBuffer buffer;
    VkDeviceSize offset, size;
    UINT index;
} D3dCbvEntry;

UINT D3dHeapAlloc(D3dDevice *device, UINT kind)
{
    D3dHeap *heap = &device->heap;
    UINT index = 0;
    AcquireSRWLockExclusive(&heap->lock);
    UINT64 completed = device->completed;
    /* reuse a freed index the gpu is done with, else take a new one. */
    for (UINT i = 0; i < heap->freed[kind].count; i++)
        if (heap->freed[kind].serial[i] <= completed) {
            index = heap->freed[kind].index[i];
            heap->freed[kind].count--;
            heap->freed[kind].index[i] = heap->freed[kind].index[heap->freed[kind].count];
            heap->freed[kind].serial[i] = heap->freed[kind].serial[heap->freed[kind].count];
            break;
        }
    if (!index && heap->next[kind] < heap->capacity[kind])
        index = heap->next[kind]++;
    ReleaseSRWLockExclusive(&heap->lock);
    if (!index)
        D3dLog("d3d11: descriptor heap %u is full", kind);
    return index;
}

UINT D3dHeapAllocRange(D3dDevice *device, UINT kind, UINT count)
{
    D3dHeap *heap = &device->heap;
    UINT index = 0;
    AcquireSRWLockExclusive(&heap->lock);
    if (heap->next[kind] + count <= heap->capacity[kind]) {
        index = heap->next[kind];
        heap->next[kind] += count;
    }
    ReleaseSRWLockExclusive(&heap->lock);
    return index;
}

static void heap_release(D3dDevice *device, UINT kind, UINT index, UINT64 serial)
{
    D3dHeap *heap = &device->heap;
    AcquireSRWLockExclusive(&heap->lock);
    if (heap->freed[kind].count == heap->freed[kind].capacity) {
        UINT capacity = heap->freed[kind].capacity ? heap->freed[kind].capacity * 2 : 256;
        UINT *index_array = heap->freed[kind].index
                                ? HeapReAlloc(GetProcessHeap(), 0, heap->freed[kind].index, capacity * sizeof(UINT))
                                : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(UINT));
        if (index_array)
            heap->freed[kind].index = index_array;
        UINT64 *serial_array =
            heap->freed[kind].serial
                ? HeapReAlloc(GetProcessHeap(), 0, heap->freed[kind].serial, capacity * sizeof(UINT64))
                : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(UINT64));
        if (serial_array)
            heap->freed[kind].serial = serial_array;
        if (index_array && serial_array)
            heap->freed[kind].capacity = capacity;
    }
    if (heap->freed[kind].count < heap->freed[kind].capacity) {
        heap->freed[kind].index[heap->freed[kind].count] = index;
        heap->freed[kind].serial[heap->freed[kind].count] = serial;
        heap->freed[kind].count++;
    }
    ReleaseSRWLockExclusive(&heap->lock);
}

/* the index is reused once what is recorded now has run. */
void D3dHeapFree(D3dDevice *device, UINT kind, UINT index)
{
    if (index)
        heap_release(device, kind, index, device->next_serial);
}

static const VkDescriptorType HEAP_TYPES[D3D_HEAP_KINDS] = {
    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
    VK_DESCRIPTOR_TYPE_SAMPLER,              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
};

static void write_one(D3dDevice *device, UINT kind, UINT index, const VkDescriptorImageInfo *image,
                      const VkBufferView *texel, const VkDescriptorBufferInfo *buffer)
{
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = device->heap.set;
    write.dstBinding = kind;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = HEAP_TYPES[kind];
    write.pImageInfo = image;
    write.pTexelBufferView = texel;
    write.pBufferInfo = buffer;
    device->vk.vkUpdateDescriptorSets(device->vk.device, 1, &write, 0, nullptr);
}

void D3dHeapWriteImage(D3dDevice *device, UINT kind, UINT index, VkImageView view)
{
    VkDescriptorImageInfo info = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
    write_one(device, kind, index, &info, nullptr, nullptr);
}

void D3dHeapWriteTexel(D3dDevice *device, UINT kind, UINT index, VkBufferView view)
{
    write_one(device, kind, index, nullptr, &view, nullptr);
}

void D3dHeapWriteSampler(D3dDevice *device, UINT index, VkSampler sampler)
{
    VkDescriptorImageInfo info = {sampler};
    write_one(device, D3D_HEAP_SAMPLER, index, &info, nullptr, nullptr);
}

void D3dHeapWriteBuffer(D3dDevice *device, UINT index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
{
    VkDescriptorBufferInfo info = {buffer, offset, range};
    write_one(device, D3D_HEAP_UNIFORM_BUFFER, index, nullptr, nullptr, &info);
}

void D3dHeapWriteBuffers(D3dDevice *device, UINT index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize stride,
                         VkDeviceSize range, UINT count)
{
    VkDescriptorBufferInfo *infos = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*infos));
    if (!infos)
        return;
    for (UINT i = 0; i < count; i++)
        infos[i] = (VkDescriptorBufferInfo){buffer, offset + i * stride, range};
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = device->heap.set;
    write.dstBinding = D3D_HEAP_UNIFORM_BUFFER;
    write.dstArrayElement = index;
    write.descriptorCount = count;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = infos;
    device->vk.vkUpdateDescriptorSets(device->vk.device, 1, &write, 0, nullptr);
    HeapFree(GetProcessHeap(), 0, infos);
}

/* the heap is sized by the gpu's limits. direct3d allows only 4096 sampler states. */
HRESULT D3dHeapInit(D3dDevice *device)
{
    D3dVk *vk = &device->vk;
    D3dHeap *heap = &device->heap;
    VkPhysicalDeviceVulkan12Properties p12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
    VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &p12};
    vk->vkGetPhysicalDeviceProperties2(vk->physical, &props);
    UINT limits[D3D_HEAP_KINDS] = {
        p12.maxDescriptorSetUpdateAfterBindSampledImages, p12.maxDescriptorSetUpdateAfterBindStorageImages,
        p12.maxDescriptorSetUpdateAfterBindSampledImages, p12.maxDescriptorSetUpdateAfterBindStorageImages,
        p12.maxDescriptorSetUpdateAfterBindSamplers,      p12.maxDescriptorSetUpdateAfterBindUniformBuffers,
    };
    static const UINT wanted[D3D_HEAP_KINDS] = {1 << 18, 1 << 16, 1 << 16, 1 << 16, 4096 + 1, 1 << 17};
    VkDescriptorSetLayoutBinding bindings[D3D_HEAP_KINDS];
    VkDescriptorBindingFlags flags[D3D_HEAP_KINDS];
    VkDescriptorPoolSize sizes[D3D_HEAP_KINDS];
    for (UINT i = 0; i < D3D_HEAP_KINDS; i++) {
        heap->capacity[i] = D3D_MIN(wanted[i], limits[i]);
        heap->next[i] = 1;
        bindings[i] = (VkDescriptorSetLayoutBinding){i, HEAP_TYPES[i], heap->capacity[i], VK_SHADER_STAGE_ALL};
        flags[i] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                   VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
        sizes[i] = (VkDescriptorPoolSize){HEAP_TYPES[i], heap->capacity[i]};
    }
    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    binding_flags.bindingCount = D3D_HEAP_KINDS;
    binding_flags.pBindingFlags = flags;
    VkDescriptorSetLayoutCreateInfo layout = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &binding_flags};
    layout.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layout.bindingCount = D3D_HEAP_KINDS;
    layout.pBindings = bindings;
    if (vk->vkCreateDescriptorSetLayout(vk->device, &layout, nullptr, &device->heap_layout) != VK_SUCCESS)
        return E_FAIL;

    VkDescriptorPoolCreateInfo pool = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pool.maxSets = 1;
    pool.poolSizeCount = D3D_HEAP_KINDS;
    pool.pPoolSizes = sizes;
    if (vk->vkCreateDescriptorPool(vk->device, &pool, nullptr, &device->heap_pool) != VK_SUCCESS)
        return E_FAIL;
    VkDescriptorSetAllocateInfo alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = device->heap_pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &device->heap_layout;
    if (vk->vkAllocateDescriptorSets(vk->device, &alloc, &heap->set) != VK_SUCCESS)
        return E_FAIL;

    /* binding tables: a dynamic uniform buffer per stage. */
    VkDescriptorSetLayoutBinding tables[D3D_STAGES];
    for (UINT i = 0; i < D3D_STAGES; i++)
        tables[i] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_ALL};
    VkDescriptorSetLayoutCreateInfo table_layout = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    table_layout.bindingCount = D3D_STAGES;
    table_layout.pBindings = tables;
    if (vk->vkCreateDescriptorSetLayout(vk->device, &table_layout, nullptr, &device->table_layout) != VK_SUCCESS)
        return E_FAIL;
    VkDescriptorPoolSize table_size = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024 * D3D_STAGES};
    VkDescriptorPoolCreateInfo table_pool = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    table_pool.maxSets = 1024;
    table_pool.poolSizeCount = 1;
    table_pool.pPoolSizes = &table_size;
    if (vk->vkCreateDescriptorPool(vk->device, &table_pool, nullptr, &device->table_pool) != VK_SUCCESS)
        return E_FAIL;

    VkDescriptorSetLayout sets[2] = {device->heap_layout, device->table_layout};
    VkPipelineLayoutCreateInfo pipeline = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline.setLayoutCount = 2;
    pipeline.pSetLayouts = sets;
    if (vk->vkCreatePipelineLayout(vk->device, &pipeline, nullptr, &device->pipeline_layout) != VK_SUCCESS)
        return E_FAIL;

    /* the null sampler is direct3d's default sampler state. */
    D3dHeapWriteImage(device, D3D_HEAP_SAMPLED_IMAGE, 0, VK_NULL_HANDLE);
    D3dHeapWriteImage(device, D3D_HEAP_STORAGE_IMAGE, 0, VK_NULL_HANDLE);
    D3dHeapWriteTexel(device, D3D_HEAP_UNIFORM_TEXEL, 0, VK_NULL_HANDLE);
    D3dHeapWriteTexel(device, D3D_HEAP_STORAGE_TEXEL, 0, VK_NULL_HANDLE);
    D3dHeapWriteBuffer(device, 0, VK_NULL_HANDLE, 0, VK_WHOLE_SIZE);
    VkSamplerCreateInfo sampler = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    if (vk->vkCreateSampler(vk->device, &sampler, nullptr, &device->null_sampler) != VK_SUCCESS)
        return E_FAIL;
    D3dHeapWriteSampler(device, 0, device->null_sampler);
    return S_OK;
}

/* ------------------------------------------------------------------ constant buffer descriptors */

/* cached per range, made the first time it is bound. */
UINT D3dCbvIndex(D3dDevice *device, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
{
    size = D3D_MIN(size, D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16);
    UINT64 hash = ((UINT64)(UINT_PTR)buffer * 0x9E3779B97F4A7C15ull) ^ (offset * 0xC2B2AE3D27D4EB4Full) ^ size;
    AcquireSRWLockShared(&device->cbv_lock);
    if (device->cbv_capacity) {
        UINT mask = device->cbv_capacity - 1;
        for (UINT i = (UINT)(hash >> 32) & mask;; i = (i + 1) & mask) {
            D3dCbvEntry *e = &device->cbv[i];
            if (!e->buffer)
                break;
            if (e->buffer == buffer && e->offset == offset && e->size == size) {
                UINT index = e->index;
                ReleaseSRWLockShared(&device->cbv_lock);
                return index;
            }
        }
    }
    ReleaseSRWLockShared(&device->cbv_lock);

    UINT index = D3dHeapAlloc(device, D3D_HEAP_UNIFORM_BUFFER);
    if (!index)
        return 0;
    D3dHeapWriteBuffer(device, index, buffer, offset, size);
    AcquireSRWLockExclusive(&device->cbv_lock);
    if ((device->cbv_count + 1) * 2 > device->cbv_capacity) {
        UINT capacity = device->cbv_capacity ? device->cbv_capacity * 2 : 1024;
        D3dCbvEntry *table = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(*table));
        if (table) {
            for (UINT i = 0; i < device->cbv_capacity; i++) {
                D3dCbvEntry *e = &device->cbv[i];
                if (!e->buffer)
                    continue;
                UINT64 h = ((UINT64)(UINT_PTR)e->buffer * 0x9E3779B97F4A7C15ull) ^
                           (e->offset * 0xC2B2AE3D27D4EB4Full) ^ e->size;
                UINT j = (UINT)(h >> 32) & (capacity - 1);
                while (table[j].buffer)
                    j = (j + 1) & (capacity - 1);
                table[j] = *e;
            }
            HeapFree(GetProcessHeap(), 0, device->cbv);
            device->cbv = table;
            device->cbv_capacity = capacity;
        }
    }
    if ((device->cbv_count + 1) * 2 <= device->cbv_capacity) {
        UINT mask = device->cbv_capacity - 1;
        UINT i = (UINT)(hash >> 32) & mask;
        while (device->cbv[i].buffer && !(device->cbv[i].buffer == buffer && device->cbv[i].offset == offset &&
                                          device->cbv[i].size == size))
            i = (i + 1) & mask;
        if (!device->cbv[i].buffer) {
            device->cbv[i] = (D3dCbvEntry){buffer, offset, size, index};
            device->cbv_count++;
        } else {
            /* another thread made it first. */
            D3dHeapFree(device, D3D_HEAP_UNIFORM_BUFFER, index);
            index = device->cbv[i].index;
        }
    }
    ReleaseSRWLockExclusive(&device->cbv_lock);
    return index;
}

/* ------------------------------------------------------------------ deferred destruction */

static void destroy(D3dDevice *device, D3dGarbage *g)
{
    D3dVk *vk = &device->vk;
    switch (g->type) {
    case D3D_GARBAGE_IMAGE:
        vk->vkDestroyImage(vk->device, (VkImage)g->handle, nullptr);
        break;
    case D3D_GARBAGE_IMAGE_VIEW:
        vk->vkDestroyImageView(vk->device, (VkImageView)g->handle, nullptr);
        break;
    case D3D_GARBAGE_BUFFER_VIEW:
        vk->vkDestroyBufferView(vk->device, (VkBufferView)g->handle, nullptr);
        break;
    case D3D_GARBAGE_SAMPLER:
        vk->vkDestroySampler(vk->device, (VkSampler)g->handle, nullptr);
        break;
    case D3D_GARBAGE_SHADER:
        vk->vkDestroyShaderEXT(vk->device, (VkShaderEXT)g->handle, nullptr);
        break;
    case D3D_GARBAGE_QUERY_POOL:
        vk->vkDestroyQueryPool(vk->device, (VkQueryPool)g->handle, nullptr);
        break;
    case D3D_GARBAGE_MEMORY:
        D3dFree(device, &g->alloc);
        break;
    case D3D_GARBAGE_HEAP:
        heap_release(device, g->heap_kind, (UINT)g->handle, 0);
        break;
    case D3D_GARBAGE_COMMAND_POOL:
        vk->vkDestroyCommandPool(vk->device, (VkCommandPool)g->handle, nullptr);
        break;
    }
}

static void defer(D3dDevice *device, const D3dGarbage *g)
{
    if (g->serial <= device->completed) {
        D3dGarbage copy = *g;
        destroy(device, &copy);
        return;
    }
    AcquireSRWLockExclusive(&device->garbage_lock);
    if (device->garbage_count == device->garbage_capacity) {
        UINT capacity = device->garbage_capacity ? device->garbage_capacity * 2 : 256;
        D3dGarbage *list = device->garbage
                               ? HeapReAlloc(GetProcessHeap(), 0, device->garbage, capacity * sizeof(*list))
                               : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
        if (!list) {
            ReleaseSRWLockExclusive(&device->garbage_lock);
            /* leak rather than free something still in use. */
            return;
        }
        device->garbage = list;
        device->garbage_capacity = capacity;
    }
    device->garbage[device->garbage_count++] = *g;
    ReleaseSRWLockExclusive(&device->garbage_lock);
}

void D3dDefer(D3dDevice *device, UINT64 serial, D3dGarbageType type, UINT64 handle)
{
    if (!handle)
        return;
    D3dGarbage g = {serial, type, handle};
    if (type == D3D_GARBAGE_HEAP) {
        g.heap_kind = (UINT)(handle >> 32);
        g.handle = (UINT)handle;
    }
    defer(device, &g);
}

void D3dDeferFree(D3dDevice *device, UINT64 serial, D3dAllocation *alloc)
{
    if (!alloc->block && !alloc->memory)
        return;
    D3dGarbage g = {serial, D3D_GARBAGE_MEMORY};
    g.alloc = *alloc;
    memset(alloc, 0, sizeof(*alloc));
    defer(device, &g);
}

void D3dCollectGarbage(D3dDevice *device)
{
    AcquireSRWLockExclusive(&device->garbage_lock);
    UINT64 completed = device->completed;
    UINT kept = 0;
    for (UINT i = 0; i < device->garbage_count; i++) {
        if (device->garbage[i].serial <= completed)
            destroy(device, &device->garbage[i]);
        else
            device->garbage[kept++] = device->garbage[i];
    }
    device->garbage_count = kept;
    ReleaseSRWLockExclusive(&device->garbage_lock);
}

void D3dHeapDestroy(D3dDevice *device)
{
    for (UINT i = 0; i < D3D_HEAP_KINDS; i++) {
        HeapFree(GetProcessHeap(), 0, device->heap.freed[i].index);
        HeapFree(GetProcessHeap(), 0, device->heap.freed[i].serial);
    }
    HeapFree(GetProcessHeap(), 0, device->cbv);
    HeapFree(GetProcessHeap(), 0, device->garbage);
}
