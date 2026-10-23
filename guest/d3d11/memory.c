/*
 * memory: large sub-allocated blocks, kept for the device's life. a buffer block is one vulkan buffer over the
 * whole block, so a direct3d buffer is just a range and needs no vulkan object or round trip of its own. image
 * blocks hold images bound at offsets.
 *
 * the ring is upload memory for one command buffer: binding tables and UpdateSubresource bytes. a chunk is reused
 * once the submissions that used it are done.
 */
#include "d3d11_private.h"

#define BLOCK_SIZE (64ull << 20)
#define RING_CHUNK_SIZE (4ull << 20)

typedef struct D3dRange {
    VkDeviceSize offset, size;
} D3dRange;

struct D3dBlock {
    D3dBlock *next;
    D3dMemoryKind kind;
    BOOL image;
    UINT type;
    VkDeviceMemory memory;
    VkBuffer buffer;
    BYTE *mapped;
    VkDeviceSize size;
    D3dRange *free;
    UINT free_count, free_capacity;
};

struct D3dRingChunk {
    D3dRingChunk *next;
    D3dAllocation alloc;
    VkDeviceSize head;
    UINT64 serial;
    VkDescriptorSet table_set;
};

static const VkBufferUsageFlags BUFFER_USAGE =
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
    VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

static VkBufferUsageFlags buffer_usage(D3dDevice *device)
{
    VkBufferUsageFlags usage = BUFFER_USAGE;
    if (device->has_transform_feedback)
        usage |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;
    if (device->has_conditional)
        usage |= VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;
    return usage;
}

static int pick_type(D3dDevice *device, D3dMemoryKind kind, UINT allowed)
{
    static const VkMemoryPropertyFlags wanted[D3D_MEMORY_KINDS][3] = {
        [D3D_MEMORY_DEVICE] = {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, 0},
        [D3D_MEMORY_UPLOAD] = {VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, 0},
        [D3D_MEMORY_READBACK] = {VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                     VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0},
    };
    for (UINT w = 0; w < 3 && wanted[kind][w]; w++)
        for (UINT i = 0; i < device->memory.memoryTypeCount; i++) {
            VkMemoryPropertyFlags flags = device->memory.memoryTypes[i].propertyFlags;
            if ((allowed & (1u << i)) && (flags & wanted[kind][w]) == wanted[kind][w])
                return (int)i;
        }
    /* device memory falls back to any type, for gpus whose memory is all host visible. */
    if (kind == D3D_MEMORY_DEVICE)
        for (UINT i = 0; i < device->memory.memoryTypeCount; i++)
            if (allowed & (1u << i))
                return (int)i;
    return -1;
}

static BOOL add_free(D3dBlock *block, VkDeviceSize offset, VkDeviceSize size)
{
    if (block->free_count == block->free_capacity) {
        UINT capacity = block->free_capacity ? block->free_capacity * 2 : 16;
        D3dRange *ranges = block->free ? HeapReAlloc(GetProcessHeap(), 0, block->free, capacity * sizeof(*ranges))
                                       : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*ranges));
        if (!ranges)
            return FALSE;
        block->free = ranges;
        block->free_capacity = capacity;
    }
    /* kept sorted by offset and merged with neighbours. */
    UINT at = 0;
    while (at < block->free_count && block->free[at].offset < offset)
        at++;
    if (at && block->free[at - 1].offset + block->free[at - 1].size == offset) {
        block->free[at - 1].size += size;
        if (at < block->free_count && offset + size == block->free[at].offset) {
            block->free[at - 1].size += block->free[at].size;
            memmove(&block->free[at], &block->free[at + 1], (block->free_count - at - 1) * sizeof(D3dRange));
            block->free_count--;
        }
        return TRUE;
    }
    if (at < block->free_count && offset + size == block->free[at].offset) {
        block->free[at].offset = offset;
        block->free[at].size += size;
        return TRUE;
    }
    memmove(&block->free[at + 1], &block->free[at], (block->free_count - at) * sizeof(D3dRange));
    block->free[at].offset = offset;
    block->free[at].size = size;
    block->free_count++;
    return TRUE;
}

/* first fit. */
static BOOL take_range(D3dBlock *block, VkDeviceSize size, VkDeviceSize align, VkDeviceSize *out)
{
    for (UINT i = 0; i < block->free_count; i++) {
        D3dRange r = block->free[i];
        VkDeviceSize start = D3D_ALIGN(r.offset, align);
        if (start + size > r.offset + r.size)
            continue;
        VkDeviceSize tail = r.offset + r.size - (start + size);
        if (start == r.offset) {
            block->free[i].offset += size;
            block->free[i].size -= size;
            if (!block->free[i].size) {
                memmove(&block->free[i], &block->free[i + 1], (block->free_count - i - 1) * sizeof(D3dRange));
                block->free_count--;
            }
        } else {
            block->free[i].size = start - r.offset;
            if (tail && !add_free(block, start + size, tail))
                return FALSE;
        }
        *out = start;
        return TRUE;
    }
    return FALSE;
}

static D3dBlock *new_block(D3dDevice *device, D3dMemoryKind kind, BOOL image, UINT type, VkDeviceSize size)
{
    D3dVk *vk = &device->vk;
    D3dBlock *block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*block));
    if (!block)
        return nullptr;
    block->kind = kind;
    block->image = image;
    block->type = type;
    block->size = size;
    VkMemoryAllocateFlagsInfo flags = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    VkMemoryAllocateInfo info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags, size, type};
    if (vk->vkAllocateMemory(vk->device, &info, nullptr, &block->memory) != VK_SUCCESS)
        goto fail;
    if (device->memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        void *mapped = nullptr;
        if (vk->vkMapMemory(vk->device, block->memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
            goto fail;
        block->mapped = mapped;
    }
    if (!image) {
        VkBufferCreateInfo buffer = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size = size;
        buffer.usage = buffer_usage(device);
        if (vk->vkCreateBuffer(vk->device, &buffer, nullptr, &block->buffer) != VK_SUCCESS)
            goto fail;
        vk->vkBindBufferMemory(vk->device, block->buffer, block->memory, 0);
    }
    if (!add_free(block, 0, size))
        goto fail;
    block->next = device->blocks;
    device->blocks = block;
    return block;
fail:
    if (block->buffer)
        vk->vkDestroyBuffer(vk->device, block->buffer, nullptr);
    if (block->memory)
        vk->vkFreeMemory(vk->device, block->memory, nullptr);
    HeapFree(GetProcessHeap(), 0, block);
    return nullptr;
}

static HRESULT allocate(D3dDevice *device, D3dMemoryKind kind, BOOL image, UINT type, VkDeviceSize size,
                        VkDeviceSize align, D3dAllocation *out)
{
    memset(out, 0, sizeof(*out));
    if (!size)
        size = 1;
    AcquireSRWLockExclusive(&device->memory_lock);
    D3dBlock *block = nullptr;
    VkDeviceSize offset = 0;
    for (D3dBlock *b = device->blocks; b; b = b->next)
        if (b->kind == kind && b->image == image && b->type == type && take_range(b, size, align, &offset)) {
            block = b;
            break;
        }
    if (!block) {
        block = new_block(device, kind, image, type, D3D_MAX(BLOCK_SIZE, D3D_ALIGN(size, 1 << 16)));
        if (!block || !take_range(block, size, align, &offset)) {
            ReleaseSRWLockExclusive(&device->memory_lock);
            return E_OUTOFMEMORY;
        }
    }
    ReleaseSRWLockExclusive(&device->memory_lock);
    out->block = block;
    out->memory = block->memory;
    out->buffer = block->buffer;
    out->offset = offset;
    out->size = size;
    out->mapped = block->mapped ? block->mapped + offset : nullptr;
    return S_OK;
}

HRESULT D3dAllocBuffer(D3dDevice *device, D3dMemoryKind kind, VkDeviceSize size, VkDeviceSize align,
                       D3dAllocation *out)
{
    UINT type = device->buffer_types[kind];
    if (type == ~0u)
        return E_OUTOFMEMORY;
    /*
     * texel views, uniform buffers and whole-texel copies all align to 256 bytes. sizes round up too, so small
     * buffers leave no gaps in the free list.
     */
    align = D3D_MAX(align, 256);
    return allocate(device, kind, FALSE, type, D3D_ALIGN(size, align), align, out);
}

HRESULT D3dAllocImage(D3dDevice *device, VkImage image, D3dAllocation *out)
{
    D3dVk *vk = &device->vk;
    VkMemoryRequirements req;
    vk->vkGetImageMemoryRequirements(vk->device, image, &req);
    int type = pick_type(device, D3D_MEMORY_DEVICE, req.memoryTypeBits);
    if (type < 0)
        return E_OUTOFMEMORY;
    HRESULT hr = allocate(device, D3D_MEMORY_DEVICE, TRUE, (UINT)type, req.size, req.alignment, out);
    if (FAILED(hr))
        return hr;
    if (vk->vkBindImageMemory(vk->device, image, out->memory, out->offset) != VK_SUCCESS) {
        D3dFree(device, out);
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

/*
 * shared images get dedicated memory that other devices import under the share object's id. the first device
 * exports it, the others import it with the exporter's memory type and size.
 */
HRESULT D3dAllocImageShared(D3dDevice *device, VkImage image, UINT64 import_id, const MimeShareBlob *blob,
                            D3dAllocation *out, UINT *type_index)
{
    D3dVk *vk = &device->vk;
    VkMemoryRequirements req;
    vk->vkGetImageMemoryRequirements(vk->device, image, &req);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    UINT type = 0;
    VkDeviceSize size = req.size;
    if (import_id) {
        type = blob->type_index;
        size = blob->memory_size;
        if (vk->vkMimeShareImportMemory(vk->device, import_id, image, size, type, &memory) != VK_SUCCESS)
            return E_FAIL;
    } else {
        int picked = pick_type(device, D3D_MEMORY_DEVICE, req.memoryTypeBits);
        if (picked < 0)
            return E_OUTOFMEMORY;
        type = (UINT)picked;
        VkMemoryDedicatedAllocateInfo dedicated = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, nullptr, image};
        VkExportMemoryAllocateInfo export = {VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, &dedicated,
                                             VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
        VkMemoryAllocateInfo info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &export, size, type};
        if (vk->vkAllocateMemory(vk->device, &info, nullptr, &memory) != VK_SUCCESS)
            return E_OUTOFMEMORY;
    }
    if (vk->vkBindImageMemory(vk->device, image, memory, 0) != VK_SUCCESS) {
        vk->vkFreeMemory(vk->device, memory, nullptr);
        return E_OUTOFMEMORY;
    }
    memset(out, 0, sizeof(*out));
    out->memory = memory;
    out->size = size;
    *type_index = type;
    return S_OK;
}

void D3dFree(D3dDevice *device, D3dAllocation *alloc)
{
    if (!alloc->block) {
        /* dedicated memory of a shared image. */
        if (alloc->memory) {
            device->vk.vkFreeMemory(device->vk.device, alloc->memory, nullptr);
            memset(alloc, 0, sizeof(*alloc));
        }
        return;
    }
    AcquireSRWLockExclusive(&device->memory_lock);
    add_free(alloc->block, alloc->offset, alloc->size);
    ReleaseSRWLockExclusive(&device->memory_lock);
    memset(alloc, 0, sizeof(*alloc));
}

/* picks each kind's buffer memory type from a probe buffer's requirements. */
HRESULT D3dMemoryInit(D3dDevice *device)
{
    D3dVk *vk = &device->vk;
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = 65536;
    info.usage = buffer_usage(device);
    VkBuffer probe;
    if (vk->vkCreateBuffer(vk->device, &info, nullptr, &probe) != VK_SUCCESS)
        return E_FAIL;
    VkMemoryRequirements req;
    vk->vkGetBufferMemoryRequirements(vk->device, probe, &req);
    vk->vkDestroyBuffer(vk->device, probe, nullptr);
    for (UINT kind = 0; kind < D3D_MEMORY_KINDS; kind++) {
        int type = pick_type(device, kind, req.memoryTypeBits);
        device->buffer_types[kind] = type < 0 ? ~0u : (UINT)type;
    }
    if (device->buffer_types[D3D_MEMORY_DEVICE] == ~0u || device->buffer_types[D3D_MEMORY_UPLOAD] == ~0u)
        return E_FAIL;
    if (device->buffer_types[D3D_MEMORY_READBACK] == ~0u)
        device->buffer_types[D3D_MEMORY_READBACK] = device->buffer_types[D3D_MEMORY_UPLOAD];
    return S_OK;
}

/* ------------------------------------------------------------------ the ring */

static D3dRingChunk *ring_chunk(D3dDevice *device, VkDeviceSize size)
{
    UINT64 completed = D3dCompleted(device);
    for (D3dRingChunk **link = &device->ring_chunks; *link; link = &(*link)->next) {
        D3dRingChunk *chunk = *link;
        if (chunk->serial <= completed && chunk->alloc.size >= size) {
            *link = chunk->next;
            chunk->next = nullptr;
            chunk->head = 0;
            return chunk;
        }
    }
    D3dRingChunk *chunk = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*chunk));
    if (!chunk)
        return nullptr;
    if (FAILED(D3dAllocBuffer(device, D3D_MEMORY_UPLOAD, D3D_MAX(RING_CHUNK_SIZE, size), 256, &chunk->alloc))) {
        HeapFree(GetProcessHeap(), 0, chunk);
        return nullptr;
    }
    return chunk;
}

BOOL D3dRingAlloc(D3dDevice *device, VkDeviceSize size, VkDeviceSize align, D3dRingSlice *out)
{
    AcquireSRWLockExclusive(&device->ring_lock);
    D3dRingChunk *chunk = device->ring_current;
    if (!chunk || D3D_ALIGN(chunk->head, align) + size > chunk->alloc.size) {
        if (chunk) {
            /* reusable once what is recorded now has run. */
            chunk->serial = device->next_serial;
            chunk->next = device->ring_chunks;
            device->ring_chunks = chunk;
        }
        chunk = device->ring_current = ring_chunk(device, size);
        if (!chunk) {
            ReleaseSRWLockExclusive(&device->ring_lock);
            return FALSE;
        }
    }
    VkDeviceSize offset = D3D_ALIGN(chunk->head, align);
    chunk->head = offset + size;
    chunk->serial = device->next_serial;
    out->chunk = chunk;
    out->buffer = chunk->alloc.buffer;
    out->offset = chunk->alloc.offset + offset;
    out->chunk_offset = offset;
    out->mapped = chunk->alloc.mapped + offset;
    ReleaseSRWLockExclusive(&device->ring_lock);
    return TRUE;
}

D3dRingChunk *D3dRingChunkNew(D3dDevice *device, VkDeviceSize size)
{
    AcquireSRWLockExclusive(&device->ring_lock);
    D3dRingChunk *chunk = ring_chunk(device, size);
    ReleaseSRWLockExclusive(&device->ring_lock);
    return chunk;
}

BOOL D3dRingChunkAlloc(D3dRingChunk *chunk, VkDeviceSize size, VkDeviceSize align, D3dRingSlice *out)
{
    VkDeviceSize offset = D3D_ALIGN(chunk->head, align);
    if (offset + size > chunk->alloc.size)
        return FALSE;
    chunk->head = offset + size;
    out->chunk = chunk;
    out->buffer = chunk->alloc.buffer;
    out->offset = chunk->alloc.offset + offset;
    out->chunk_offset = offset;
    out->mapped = chunk->alloc.mapped + offset;
    return TRUE;
}

/* ------------------------------------------------------------------ the constant buffer ring */

#define CB_CHUNK_SIZE (4ull << 20)
#define CB_WINDOW ((VkDeviceSize)D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16)
#define CB_STEP 256
/* each chunk takes an eighth of the uniform buffer heap in descriptors, so at most six. */
#define CB_CHUNKS_MAX 6

struct D3dCbChunk {
    D3dCbChunk *next;
    D3dAllocation alloc;
    VkDeviceSize head;
    UINT64 serial;
    LONG pins;
    UINT cbv_base;
};

static D3dCbChunk *cb_chunk(D3dDevice *device)
{
    UINT64 completed = D3dCompleted(device);
    for (D3dCbChunk **link = &device->cb_chunks; *link; link = &(*link)->next) {
        D3dCbChunk *chunk = *link;
        if (chunk->serial <= completed && !chunk->pins) {
            *link = chunk->next;
            chunk->next = nullptr;
            chunk->head = 0;
            return chunk;
        }
    }
    if (device->cb_chunk_count >= CB_CHUNKS_MAX)
        return nullptr;
    D3dCbChunk *chunk = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*chunk));
    if (!chunk)
        return nullptr;
    /* an extra window past the last slice keeps every descriptor inside the buffer. */
    UINT count = (UINT)(CB_CHUNK_SIZE / CB_STEP);
    if (FAILED(D3dAllocBuffer(device, D3D_MEMORY_UPLOAD, CB_CHUNK_SIZE + CB_WINDOW, CB_STEP, &chunk->alloc))) {
        HeapFree(GetProcessHeap(), 0, chunk);
        return nullptr;
    }
    chunk->cbv_base = D3dHeapAllocRange(device, D3D_HEAP_UNIFORM_BUFFER, count);
    if (!chunk->cbv_base) {
        D3dFree(device, &chunk->alloc);
        HeapFree(GetProcessHeap(), 0, chunk);
        return nullptr;
    }
    D3dHeapWriteBuffers(device, chunk->cbv_base, chunk->alloc.buffer, chunk->alloc.offset, CB_STEP, CB_WINDOW, count);
    device->cb_chunk_count++;
    return chunk;
}

BOOL D3dCbAlloc(D3dDevice *device, VkDeviceSize size, D3dCbSlice *out, BOOL *retired)
{
    *retired = FALSE;
    if (size > CB_WINDOW)
        return FALSE;
    size = D3D_ALIGN(size, CB_STEP);
    AcquireSRWLockExclusive(&device->cb_lock);
    D3dCbChunk *chunk = device->cb_current;
    if (!chunk || chunk->head + size > CB_CHUNK_SIZE) {
        D3dCbChunk *next = cb_chunk(device);
        if (!next) {
            ReleaseSRWLockExclusive(&device->cb_lock);
            return FALSE;
        }
        if (chunk) {
            chunk->serial = D3D_MAX(chunk->serial, device->next_serial);
            chunk->next = device->cb_chunks;
            device->cb_chunks = chunk;
            *retired = TRUE;
        }
        chunk = device->cb_current = next;
    }
    VkDeviceSize offset = chunk->head;
    chunk->head += size;
    chunk->pins++;
    out->chunk = chunk;
    out->buffer = chunk->alloc.buffer;
    out->offset = chunk->alloc.offset + offset;
    out->mapped = chunk->alloc.mapped + offset;
    out->cbv = chunk->cbv_base + (UINT)(offset / CB_STEP);
    ReleaseSRWLockExclusive(&device->cb_lock);
    return TRUE;
}

/* the gpu may read the chunk until serial. */
void D3dCbUnpin(D3dDevice *device, D3dCbChunk *chunk, UINT64 serial)
{
    if (!chunk)
        return;
    AcquireSRWLockExclusive(&device->cb_lock);
    chunk->serial = D3D_MAX(chunk->serial, serial);
    chunk->pins--;
    ReleaseSRWLockExclusive(&device->cb_lock);
}

/* gives back the unused end of the chunk's last slice. */
void D3dRingTrim(D3dDevice *device, const D3dRingSlice *slice, VkDeviceSize reserved, VkDeviceSize used)
{
    AcquireSRWLockExclusive(&device->ring_lock);
    if (slice->chunk->head == slice->chunk_offset + reserved)
        slice->chunk->head = slice->chunk_offset + used;
    ReleaseSRWLockExclusive(&device->ring_lock);
}

/* reused once the command list's last execution is done. */
void D3dRingChunkRelease(D3dDevice *device, D3dRingChunk *chunk, UINT64 serial)
{
    AcquireSRWLockExclusive(&device->ring_lock);
    chunk->serial = serial;
    chunk->next = device->ring_chunks;
    device->ring_chunks = chunk;
    ReleaseSRWLockExclusive(&device->ring_lock);
}

/* binding tables: one dynamic uniform buffer per stage. */
VkDescriptorSet D3dRingTableSet(D3dDevice *device, D3dRingChunk *chunk)
{
    if (chunk->table_set)
        return chunk->table_set;
    D3dVk *vk = &device->vk;
    VkDescriptorSetAllocateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool = device->table_pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &device->table_layout;
    if (vk->vkAllocateDescriptorSets(vk->device, &info, &chunk->table_set) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    VkDescriptorBufferInfo buffers[D3D_STAGES];
    VkWriteDescriptorSet writes[D3D_STAGES];
    for (UINT i = 0; i < D3D_STAGES; i++) {
        buffers[i] = (VkDescriptorBufferInfo){chunk->alloc.buffer, chunk->alloc.offset, DXBC_TABLE_DWORDS * 4};
        writes[i] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = chunk->table_set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[i].pBufferInfo = &buffers[i];
    }
    vk->vkUpdateDescriptorSets(vk->device, D3D_STAGES, writes, 0, nullptr);
    return chunk->table_set;
}

void D3dMemoryDestroy(D3dDevice *device)
{
    D3dVk *vk = &device->vk;
    while (device->blocks) {
        D3dBlock *block = device->blocks;
        device->blocks = block->next;
        if (block->buffer)
            vk->vkDestroyBuffer(vk->device, block->buffer, nullptr);
        vk->vkFreeMemory(vk->device, block->memory, nullptr);
        HeapFree(GetProcessHeap(), 0, block->free);
        HeapFree(GetProcessHeap(), 0, block);
    }
    if (device->ring_current) {
        device->ring_current->next = device->ring_chunks;
        device->ring_chunks = device->ring_current;
        device->ring_current = nullptr;
    }
    while (device->ring_chunks) {
        D3dRingChunk *chunk = device->ring_chunks;
        device->ring_chunks = chunk->next;
        HeapFree(GetProcessHeap(), 0, chunk);
    }
    if (device->cb_current) {
        device->cb_current->next = device->cb_chunks;
        device->cb_chunks = device->cb_current;
        device->cb_current = nullptr;
    }
    while (device->cb_chunks) {
        D3dCbChunk *chunk = device->cb_chunks;
        device->cb_chunks = chunk->next;
        HeapFree(GetProcessHeap(), 0, chunk);
    }
}
