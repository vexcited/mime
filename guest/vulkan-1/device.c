/*
 * devices and the objects the wire describes by hand: queues, memory, command
 * buffers and their streams, submission, synchronization, descriptor sets and
 * query results.
 */
#include "mvk_guest.h"

/* ------------------------------------------------------------------ guest side maps */

/* maps wire ids to guest records, for objects the guest keeps state about. */
typedef struct MapEntry {
    uint64_t id;
    void *value;
} MapEntry;

typedef struct Map {
    SRWLOCK lock;
    MapEntry *entries;
    size_t size, used;
} Map;

static uint64_t mix(uint64_t v)
{
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdull;
    return v ^ (v >> 33);
}

static void map_put_locked(Map *m, uint64_t id, void *value);

static void map_grow(Map *m)
{
    size_t old = m->size;
    MapEntry *prev = m->entries;
    m->size = old ? old * 2 : 256;
    m->entries = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, m->size * sizeof(MapEntry));
    m->used = 0;
    for (size_t i = 0; i < old; i++)
        if (prev[i].id && prev[i].value)
            map_put_locked(m, prev[i].id, prev[i].value);
    if (prev)
        HeapFree(GetProcessHeap(), 0, prev);
}

static void map_put_locked(Map *m, uint64_t id, void *value)
{
    if ((m->used + 1) * 2 > m->size)
        map_grow(m);
    size_t mask = m->size - 1;
    for (size_t i = mix(id) & mask;; i = (i + 1) & mask) {
        if (!m->entries[i].id || m->entries[i].id == id) {
            if (!m->entries[i].id)
                m->used++;
            m->entries[i].id = id;
            m->entries[i].value = value;
            return;
        }
    }
}

static void map_put(Map *m, uint64_t id, void *value)
{
    AcquireSRWLockExclusive(&m->lock);
    map_put_locked(m, id, value);
    ReleaseSRWLockExclusive(&m->lock);
}

static void *map_get(Map *m, uint64_t id)
{
    void *v = nullptr;
    AcquireSRWLockShared(&m->lock);
    if (m->size) {
        size_t mask = m->size - 1;
        for (size_t i = mix(id) & mask; m->entries[i].id; i = (i + 1) & mask)
            if (m->entries[i].id == id) {
                v = m->entries[i].value;
                break;
            }
    }
    ReleaseSRWLockShared(&m->lock);
    return v;
}

/* removal keeps the slot so probe chains stay whole; a later put reuses it. */
static void *map_take(Map *m, uint64_t id)
{
    void *v = nullptr;
    AcquireSRWLockExclusive(&m->lock);
    if (m->size) {
        size_t mask = m->size - 1;
        for (size_t i = mix(id) & mask; m->entries[i].id; i = (i + 1) & mask)
            if (m->entries[i].id == id) {
                v = m->entries[i].value;
                m->entries[i].value = nullptr;
                break;
            }
    }
    ReleaseSRWLockExclusive(&m->lock);
    return v;
}

static void *zalloc(size_t n)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n);
}

static void zfree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* ------------------------------------------------------------------ devices and queues */

VkResult mvk_device_extensions(VkPhysicalDevice physical, VkExtensionProperties **list, uint32_t *count);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo *info,
                                              const VkAllocationCallbacks *allocator, VkDevice *out)
{
    VkExtensionProperties *list;
    uint32_t n;
    VkResult r = mvk_device_extensions(physical, &list, &n);
    if (r != VK_SUCCESS)
        return r;
    for (uint32_t i = 0; i < info->enabledExtensionCount; i++) {
        BOOL found = FALSE;
        for (uint32_t j = 0; j < n && !found; j++)
            found = !strcmp(list[j].extensionName, info->ppEnabledExtensionNames[i]);
        if (!found)
            return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    VkDevice device = zalloc(sizeof(*device));
    if (!device)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    device->obj.loader_magic = MVK_LOADER_MAGIC;
    device->obj.id = mvk_new_id();
    device->physical = physical;
    MvkEncoder *e = mvk_call_begin(MVK_OP_CREATE_DEVICE);
    mvk_put_u64(e, physical->obj.id);
    mvk_put_u64(e, device->obj.id);
    mvk_put_VkDeviceCreateInfo(e, info);
    uint32_t queues = 0;
    for (uint32_t i = 0; i < info->queueCreateInfoCount; i++)
        queues += info->pQueueCreateInfos[i].queueCount;
    mvk_put_u32(e, queues);
    for (uint32_t i = 0; i < info->queueCreateInfoCount; i++) {
        const VkDeviceQueueCreateInfo *q = &info->pQueueCreateInfos[i];
        for (uint32_t k = 0; k < q->queueCount; k++) {
            struct VkQueue_T *queue = zalloc(sizeof(*queue));
            if (!queue)
                continue;
            queue->obj.loader_magic = MVK_LOADER_MAGIC;
            queue->obj.id = mvk_new_id();
            queue->device = device;
            queue->family = q->queueFamilyIndex;
            queue->index = k;
            queue->flags = q->flags;
            queue->next = device->queues;
            device->queues = queue;
            mvk_put_u32(e, q->queueFamilyIndex);
            mvk_put_u32(e, k);
            mvk_put_u64(e, queue->obj.id);
        }
    }
    MvkDecoder *d = mvk_call_wait(e);
    r = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_INITIALIZATION_FAILED;
    mvk_call_done(d);
    if (r != VK_SUCCESS) {
        while (device->queues) {
            struct VkQueue_T *q = device->queues;
            device->queues = q->next;
            zfree(q);
        }
        zfree(device);
        return r;
    }
    *out = device;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *allocator)
{
    if (!device)
        return;
    for (struct VkQueue_T *q = device->queues; q; q = q->next)
        if (q->fence_state > 0)
            vkDestroySemaphore(device, (VkSemaphore)q->fence_timeline, nullptr);
    MvkEncoder *e = mvk_call_begin(MVK_OP_DESTROY_DEVICE);
    mvk_put_u64(e, device->obj.id);
    mvk_call_end(e);
    while (device->queues) {
        struct VkQueue_T *q = device->queues;
        device->queues = q->next;
        zfree(q);
    }
    zfree(device);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue *out)
{
    *out = nullptr;
    for (struct VkQueue_T *q = device->queues; q; q = q->next)
        if (q->family == family && q->index == index && !q->flags) {
            *out = q;
            return;
        }
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *info, VkQueue *out)
{
    *out = nullptr;
    for (struct VkQueue_T *q = device->queues; q; q = q->next)
        if (q->family == info->queueFamilyIndex && q->index == info->queueIndex && q->flags == info->flags) {
            *out = q;
            return;
        }
}

/* ------------------------------------------------------------------ memory */

typedef struct Memory {
    void *base;      /* guest ram the host imported, NULL when the guest cannot map it. */
    VkDeviceSize size;
} Memory;

static Map g_memory;

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *info,
                                                const VkAllocationCallbacks *allocator, VkDeviceMemory *out)
{
    VkPhysicalDevice physical = device->physical;
    if (info->memoryTypeIndex >= physical->memory.memoryTypeCount)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    VkMemoryPropertyFlags flags = physical->memory.memoryTypes[info->memoryTypeIndex].propertyFlags;
    Memory *m = zalloc(sizeof(*m));
    if (!m)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    m->size = info->allocationSize;
    ULONG64 pa = 0;
    VkDeviceSize span = 0;
    if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        /* fresh kernel pages come zeroed, and d3d12 relies on that for its buffers. */
        span = (info->allocationSize + 0xFFFF) & ~(VkDeviceSize)0xFFFF;
        m->base = mvk_block_alloc((SIZE_T)span, &pa);
        if (!m->base) {
            zfree(m);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
    }
    uint64_t id = mvk_new_id();
    MvkEncoder *e = mvk_call_begin(MVK_OP_ALLOCATE_MEMORY);
    mvk_put_u64(e, id);
    mvk_put_VkMemoryAllocateInfo(e, info);
    mvk_put_u64(e, pa);
    mvk_put_u64(e, span);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult r = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    mvk_call_done(d);
    if (r != VK_SUCCESS) {
        if (m->base)
            mvk_block_free(m->base);
        zfree(m);
        mvk_free_id(id);
        return r;
    }
    map_put(&g_memory, id, m);
    mvk_memory_changed();
    *out = (VkDeviceMemory)id;
    return VK_SUCCESS;
}

/* imported from another process's device, so the guest cannot map it. */
VkResult mvk_memory_imported(VkDeviceMemory memory, VkDeviceSize size)
{
    Memory *m = zalloc(sizeof(*m));
    if (!m)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    m->size = size;
    map_put(&g_memory, (uint64_t)memory, m);
    mvk_memory_changed();
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *allocator)
{
    if (!memory)
        return;
    Memory *m = map_take(&g_memory, (uint64_t)memory);
    MvkEncoder *e = mvk_call_begin(MVK_OP_FREE_MEMORY);
    mvk_put_u64(e, (uint64_t)memory);
    mvk_call_end(e);
    mvk_memory_changed();
    /* the ram is released once the host freed its import. */
    if (m && m->base)
        mvk_block_free_after(m->base, mvk_last_serial());
    zfree(m);
    mvk_free_id((uint64_t)memory);
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
                                           VkDeviceSize size, VkMemoryMapFlags flags, void **data)
{
    Memory *m = map_get(&g_memory, (uint64_t)memory);
    if (!m || !m->base) {
        *data = nullptr;
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    *data = (uint8_t *)m->base + offset;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2(VkDevice device, const VkMemoryMapInfo *info, void **data)
{
    return vkMapMemory(device, info->memory, info->offset, info->size, info->flags, data);
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory)
{
}

VKAPI_ATTR VkResult VKAPI_CALL vkUnmapMemory2(VkDevice device, const VkMemoryUnmapInfo *info)
{
    return VK_SUCCESS;
}

/* guest and host share the same ram, so there is nothing to flush or invalidate. */
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice device, uint32_t count,
                                                         const VkMappedMemoryRange *ranges)
{
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t count,
                                                              const VkMappedMemoryRange *ranges)
{
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize *bytes)
{
    Memory *m = map_get(&g_memory, (uint64_t)memory);
    *bytes = m ? m->size : 0;
}

/* ------------------------------------------------------------------ command buffer streams */

#define STREAM_BLOCK (4u << 20)

typedef struct Chunk {
    struct Chunk *next;
    MvkStreamChunk *memory;
    uint64_t pa;
    uint64_t serial; /* the call the host read it in. */
} Chunk;

/* free chunks of every device, and chunks the host has yet to read. */
static SRWLOCK g_chunk_lock = SRWLOCK_INIT;
static Chunk *g_free_chunks, *g_pending_chunks;

static Chunk *chunk_alloc(void)
{
    AcquireSRWLockExclusive(&g_chunk_lock);
    if (g_pending_chunks) {
        /* chunks the host read are free again. */
        extern uint64_t mvk_done_serial(void);
        uint64_t done = mvk_done_serial();
        for (Chunk **link = &g_pending_chunks; *link;) {
            Chunk *c = *link;
            if (c->serial <= done) {
                *link = c->next;
                c->next = g_free_chunks;
                g_free_chunks = c;
            } else {
                link = &c->next;
            }
        }
    }
    if (!g_free_chunks) {
        ULONG64 pa;
        uint8_t *block = mvk_block_alloc(STREAM_BLOCK, &pa);
        for (uint32_t i = 0; block && i < STREAM_BLOCK / MVK_STREAM_CHUNK; i++) {
            Chunk *c = zalloc(sizeof(*c));
            if (!c)
                break;
            c->memory = (MvkStreamChunk *)(block + (size_t)i * MVK_STREAM_CHUNK);
            c->pa = pa + (uint64_t)i * MVK_STREAM_CHUNK;
            c->next = g_free_chunks;
            g_free_chunks = c;
        }
    }
    Chunk *c = g_free_chunks;
    if (c)
        g_free_chunks = c->next;
    ReleaseSRWLockExclusive(&g_chunk_lock);
    if (c) {
        c->next = nullptr;
        c->memory->next_pa = 0;
        c->memory->used = 0;
    }
    return c;
}

typedef struct Stream {
    Chunk *first, *last;
    uint64_t read_serial; /* the call the host reads the stream in, 0 while recording. */
} Stream;

static Stream *stream_of(VkCommandBuffer buffer)
{
    return buffer->stream;
}

/* chunks are reused once the host read them. */
static void stream_release(Stream *s)
{
    if (!s->first)
        return;
    AcquireSRWLockExclusive(&g_chunk_lock);
    for (Chunk *c = s->first, *next; c; c = next) {
        next = c->next;
        c->serial = s->read_serial ? s->read_serial : 0;
        if (s->read_serial) {
            c->next = g_pending_chunks;
            g_pending_chunks = c;
        } else {
            c->next = g_free_chunks;
            g_free_chunks = c;
        }
    }
    ReleaseSRWLockExclusive(&g_chunk_lock);
    s->first = s->last = nullptr;
    s->read_serial = 0;
}

static void stream_append(VkCommandBuffer buffer, const uint8_t *data, size_t n)
{
    Stream *s = stream_of(buffer);
    if (!s)
        return;
    while (n) {
        Chunk *c = s->last;
        size_t room = c ? MVK_STREAM_CHUNK - sizeof(MvkStreamChunk) - c->memory->used : 0;
        if (!room) {
            Chunk *next = chunk_alloc();
            if (!next) {
                mvk_log("vulkan: out of command stream memory");
                return;
            }
            if (c) {
                c->next = next;
                c->memory->next_pa = next->pa;
            } else {
                s->first = next;
            }
            s->last = c = next;
            room = MVK_STREAM_CHUNK - sizeof(MvkStreamChunk);
        }
        size_t take = n < room ? n : room;
        memcpy((uint8_t *)(c->memory + 1) + c->memory->used, data, take);
        c->memory->used += (uint32_t)take;
        buffer->bytes += take;
        data += take;
        n -= take;
    }
}

static int cmd_grow(MvkEncoder *e, size_t n)
{
    VkCommandBuffer buffer = e->owner;
    size_t used = (size_t)(e->p - e->start);
    size_t want = buffer->scratch_size ? buffer->scratch_size : 4096;
    while (want - used < n + 64)
        want *= 2;
    uint8_t *scratch = HeapAlloc(GetProcessHeap(), 0, want);
    if (!scratch)
        return 0;
    if (buffer->scratch) {
        memcpy(scratch, buffer->scratch, used);
        HeapFree(GetProcessHeap(), 0, buffer->scratch);
    }
    buffer->scratch = scratch;
    buffer->scratch_size = want;
    e->start = scratch;
    e->p = scratch + used;
    e->end = scratch + want;
    return 1;
}

/* a record that outgrows the chunk continues in scratch memory. */
static int cmd_spill(MvkEncoder *e, size_t n)
{
    VkCommandBuffer buffer = e->owner;
    if (!buffer->in_place)
        return cmd_grow(e, n);
    size_t used = (size_t)(e->p - e->start);
    MvkEncoder scratch = {buffer->scratch, buffer->scratch, buffer->scratch, 0, buffer, cmd_grow};
    if (!buffer->scratch || buffer->scratch_size < used + n + 64) {
        scratch.p = scratch.start;
        if (!cmd_grow(&scratch, used + n))
            return 0;
    }
    memcpy(buffer->scratch, e->start, used);
    e->start = buffer->scratch;
    e->p = buffer->scratch + used;
    e->end = buffer->scratch + buffer->scratch_size;
    e->grow = cmd_grow;
    buffer->in_place = 0;
    return 1;
}

/* a command that fits in the stream's last chunk is encoded there without a copy. */
MvkEncoder *mvk_cmd_begin(VkCommandBuffer buffer, uint32_t op)
{
    if (!buffer || !buffer->recording || !buffer->stream)
        return nullptr;
    Chunk *c = buffer->stream->last;
    size_t room = c ? MVK_STREAM_CHUNK - sizeof(MvkStreamChunk) - c->memory->used : 0;
    if (room >= 256) {
        uint8_t *at = (uint8_t *)(c->memory + 1) + c->memory->used;
        buffer->enc = (MvkEncoder){at, at, at + room, 0, buffer, cmd_spill};
        buffer->in_place = 1;
    } else {
        if (!buffer->scratch) {
            buffer->enc = (MvkEncoder){0};
            buffer->enc.owner = buffer;
            if (!cmd_grow(&buffer->enc, 4096))
                return nullptr;
        }
        buffer->enc = (MvkEncoder){buffer->scratch, buffer->scratch, buffer->scratch + buffer->scratch_size, 0,
                                   buffer, cmd_grow};
        buffer->in_place = 0;
    }
    MvkStreamCommand *h = mvk_reserve(&buffer->enc, sizeof(MvkStreamCommand), 8);
    if (h)
        h->op = op;
    return &buffer->enc;
}

void mvk_cmd_end(VkCommandBuffer buffer, MvkEncoder *e)
{
    mvk_align(e, 8);
    if (e->overflow) {
        mvk_log("vulkan: a recorded command does not fit");
        return;
    }
    MvkStreamCommand *h = (MvkStreamCommand *)e->start;
    h->size = (uint32_t)(e->p - e->start);
    if (buffer->in_place) {
        buffer->stream->last->memory->used += h->size;
        buffer->bytes += h->size;
    } else {
        stream_append(buffer, e->start, h->size);
    }
}

/* ------------------------------------------------------------------ command pools and buffers */

typedef struct Pool {
    VkCommandBuffer buffers;
} Pool;

static Map g_pools;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *info,
                                                   const VkAllocationCallbacks *allocator, VkCommandPool *pool)
{
    Pool *p = zalloc(sizeof(*p));
    if (!p)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkResult r = mvk_raw_vkCreateCommandPool(device, info, allocator, pool);
    if (r != VK_SUCCESS) {
        zfree(p);
        return r;
    }
    map_put(&g_pools, (uint64_t)*pool, p);
    return VK_SUCCESS;
}

static void forget_buffer(VkCommandBuffer buffer)
{
    Stream *s = buffer->stream;
    if (s) {
        stream_release(s);
        zfree(s);
    }
    zfree(buffer->scratch);
    mvk_free_id(buffer->obj.id);
    zfree(buffer);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice device, VkCommandPool pool,
                                                const VkAllocationCallbacks *allocator)
{
    if (!pool)
        return;
    MvkEncoder *e = mvk_call_begin(MVK_OP_DESTROY_COMMAND_POOL);
    mvk_put_u64(e, (uint64_t)pool);
    mvk_call_end(e);
    Pool *p = map_take(&g_pools, (uint64_t)pool);
    while (p && p->buffers) {
        VkCommandBuffer b = p->buffers;
        p->buffers = b->pool_next;
        forget_buffer(b);
    }
    zfree(p);
    mvk_free_id((uint64_t)pool);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(VkDevice device, VkCommandPool pool, VkCommandPoolResetFlags flags)
{
    Pool *p = map_get(&g_pools, (uint64_t)pool);
    for (VkCommandBuffer b = p ? p->buffers : nullptr; b; b = b->pool_next) {
        Stream *s = stream_of(b);
        if (s)
            stream_release(s);
        b->recording = 0;
        b->bytes = 0;
    }
    MvkEncoder *e = mvk_call_begin(MVK_OP_RESET_COMMAND_POOL);
    mvk_put_u64(e, (uint64_t)pool);
    mvk_put_u32(e, flags);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *info,
                                                        VkCommandBuffer *out)
{
    Pool *p = map_get(&g_pools, (uint64_t)info->commandPool);
    for (uint32_t i = 0; i < info->commandBufferCount; i++) {
        VkCommandBuffer b = zalloc(sizeof(*b));
        Stream *s = zalloc(sizeof(*s));
        if (!b || !s) {
            zfree(b);
            zfree(s);
            for (uint32_t k = 0; k < i; k++)
                forget_buffer(out[k]);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        b->obj.loader_magic = MVK_LOADER_MAGIC;
        b->obj.id = mvk_new_id();
        b->device = device;
        b->pool = (uint64_t)info->commandPool;
        b->stream = s;
        out[i] = b;
    }
    MvkEncoder *e = mvk_call_begin(MVK_OP_ALLOCATE_COMMAND_BUFFERS);
    mvk_put_VkCommandBufferAllocateInfo(e, info);
    for (uint32_t i = 0; i < info->commandBufferCount; i++)
        mvk_put_u64(e, out[i]->obj.id);
    mvk_call_end(e);
    for (uint32_t i = 0; p && i < info->commandBufferCount; i++) {
        out[i]->pool_next = p->buffers;
        p->buffers = out[i];
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device, VkCommandPool pool, uint32_t count,
                                                const VkCommandBuffer *buffers)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_FREE_COMMAND_BUFFERS);
    mvk_put_u64(e, (uint64_t)pool);
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++)
        n += buffers[i] != nullptr;
    mvk_put_u32(e, n);
    for (uint32_t i = 0; i < count; i++)
        if (buffers[i])
            mvk_put_u64(e, buffers[i]->obj.id);
    mvk_call_end(e);
    Pool *p = map_get(&g_pools, (uint64_t)pool);
    for (uint32_t i = 0; i < count; i++) {
        VkCommandBuffer b = buffers[i];
        if (!b)
            continue;
        for (VkCommandBuffer *link = p ? &p->buffers : nullptr; link && *link; link = &(*link)->pool_next)
            if (*link == b) {
                *link = b->pool_next;
                break;
            }
        forget_buffer(b);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer buffer, const VkCommandBufferBeginInfo *info)
{
    Stream *s = stream_of(buffer);
    if (s)
        stream_release(s);
    buffer->bytes = 0;
    buffer->begin = *info;
    buffer->begin.pNext = nullptr;
    buffer->has_inheritance = info->pInheritanceInfo != nullptr;
    if (info->pInheritanceInfo) {
        buffer->inheritance = *info->pInheritanceInfo;
        buffer->inheritance.pNext = nullptr;
        buffer->begin.pInheritanceInfo = &buffer->inheritance;
    }
    buffer->recording = 1;
    return VK_SUCCESS;
}

/* the host records the whole buffer from its stream here. */
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer buffer)
{
    Stream *s = stream_of(buffer);
    buffer->recording = 0;
    MvkEncoder *e = mvk_call_begin(MVK_OP_RECORD);
    mvk_put_u64(e, buffer->obj.id);
    mvk_put_VkCommandBufferBeginInfo(e, &buffer->begin);
    mvk_put_u64(e, s && s->first ? s->first->pa : 0);
    mvk_put_u64(e, buffer->bytes);
    mvk_call_end(e);
    if (s)
        s->read_serial = mvk_last_serial();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer buffer, VkCommandBufferResetFlags flags)
{
    Stream *s = stream_of(buffer);
    if (s)
        stream_release(s);
    buffer->recording = 0;
    buffer->bytes = 0;
    MvkEncoder *e = mvk_call_begin(MVK_OP_RESET_COMMAND_BUFFER);
    mvk_put_u64(e, buffer->obj.id);
    mvk_put_u32(e, flags);
    mvk_call_end(e);
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ submission */

static BOOL fence_timeline(VkQueue queue);
static void fence_submitted(VkFence fence, VkQueue queue, uint64_t value);

/*
 * a fenced submit also signals the queue's timeline semaphore in a last batch of its own, with the next value. its
 * mirror then answers for the fence, see vkWaitForFences. protected batches are left alone.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo *submits, VkFence fence)
{
    BOOL tracked = fence != VK_NULL_HANDLE;
    for (uint32_t i = 0; i < count && tracked; i++)
        for (const VkBaseInStructure *p = submits[i].pNext; p; p = p->pNext)
            if (p->sType == VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO && ((const VkProtectedSubmitInfo *)p)->protectedSubmit)
                tracked = FALSE;
    tracked = tracked && fence_timeline(queue);
    uint64_t value = tracked ? queue->fence_value + 1 : 0;
    MvkEncoder *e = mvk_call_begin(MVK_OP_QUEUE_SUBMIT);
    mvk_put_u64(e, queue->obj.id);
    mvk_put_u32(e, count + (tracked ? 1 : 0));
    for (uint32_t i = 0; i < count; i++)
        mvk_put_VkSubmitInfo(e, &submits[i]);
    if (tracked) {
        VkSemaphore timeline = (VkSemaphore)queue->fence_timeline;
        VkTimelineSemaphoreSubmitInfo values = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr, 0, nullptr, 1,
                                                &value};
        VkSubmitInfo signal = {VK_STRUCTURE_TYPE_SUBMIT_INFO, &values, 0, nullptr, nullptr, 0, nullptr, 1, &timeline};
        mvk_put_VkSubmitInfo(e, &signal);
    }
    mvk_put_u64(e, (uint64_t)fence);
    mvk_call_end(e);
    if (tracked)
        fence_submitted(fence, queue, value);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2 *submits,
                                              VkFence fence)
{
    BOOL tracked = fence != VK_NULL_HANDLE;
    for (uint32_t i = 0; i < count && tracked; i++)
        if (submits[i].flags & VK_SUBMIT_PROTECTED_BIT)
            tracked = FALSE;
    tracked = tracked && fence_timeline(queue);
    uint64_t value = tracked ? queue->fence_value + 1 : 0;
    MvkEncoder *e = mvk_call_begin(MVK_OP_QUEUE_SUBMIT2);
    mvk_put_u64(e, queue->obj.id);
    mvk_put_u32(e, count + (tracked ? 1 : 0));
    for (uint32_t i = 0; i < count; i++)
        mvk_put_VkSubmitInfo2(e, &submits[i]);
    if (tracked) {
        VkSemaphoreSubmitInfo timeline = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
                                          (VkSemaphore)queue->fence_timeline, value,
                                          VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0};
        VkSubmitInfo2 signal = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0, 0, nullptr, 0, nullptr, 1, &timeline};
        mvk_put_VkSubmitInfo2(e, &signal);
    }
    mvk_put_u64(e, (uint64_t)fence);
    mvk_call_end(e);
    if (tracked)
        fence_submitted(fence, queue, value);
    return VK_SUCCESS;
}

static VkResult result_call(MvkEncoder *e)
{
    MvkDecoder *d = mvk_call_wait(e);
    VkResult r = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    mvk_call_done(d);
    return r;
}

static void fence_idle(VkQueue queue);

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_QUEUE_WAIT_IDLE);
    mvk_put_u64(e, queue->obj.id);
    VkResult r = result_call(e);
    if (r == VK_SUCCESS)
        fence_idle(queue);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_DEVICE_WAIT_IDLE);
    mvk_put_u64(e, device->obj.id);
    VkResult r = result_call(e);
    for (struct VkQueue_T *q = device->queues; q && r == VK_SUCCESS; q = q->next)
        fence_idle(q);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueBindSparse(VkQueue queue, uint32_t count, const VkBindSparseInfo *info,
                                                 VkFence fence)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_QUEUE_BIND_SPARSE);
    mvk_put_u64(e, queue->obj.id);
    mvk_put_u32(e, count);
    for (uint32_t i = 0; i < count; i++)
        mvk_put_VkBindSparseInfo(e, &info[i]);
    mvk_put_u64(e, (uint64_t)fence);
    mvk_call_end(e);
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ synchronization */

static VkResult create_object(uint32_t op, uint64_t *out, void (*put)(MvkEncoder *, const void *), const void *info)
{
    uint64_t id = mvk_new_id();
    MvkEncoder *e = mvk_call_begin(op);
    mvk_put_u64(e, id);
    put(e, info);
    mvk_call_end(e);
    *out = id;
    return VK_SUCCESS;
}

static void destroy_object(uint32_t op, uint64_t id)
{
    if (!id)
        return;
    MvkEncoder *e = mvk_call_begin(op);
    mvk_put_u64(e, id);
    mvk_call_end(e);
    mvk_free_id(id);
}

/*
 * the host keeps timeline semaphore values in header slots, so reading one
 * costs no call. a slot is reused once the host destroyed its last semaphore
 * and writes there no more.
 */
typedef struct Mirror {
    uint64_t semaphore;
    uint64_t freed;
    uint64_t signaled;
} Mirror;

static Mirror g_mirrors[MVK_MIRRORS];
static SRWLOCK g_mirror_lock;

static int find_mirror(uint64_t semaphore)
{
    for (int i = 0; i < MVK_MIRRORS; i++)
        if (g_mirrors[i].semaphore == semaphore)
            return i;
    return -1;
}

/*
 * what the guest knows of a fence. one submitted with its queue's timeline is signaled once that semaphore's mirror
 * reaches the value of the submit, and a fence made signaled stays so until a reset. any other is the host's to answer.
 */
enum { FENCE_HOST, FENCE_SIGNALED, FENCE_PENDING };

typedef struct Fence {
    int state;
    int slot;
    uint64_t timeline, value;
} Fence;

static Map g_fences;
/* guards the records, which a wait reads while another thread may reset or destroy the fence. */
static SRWLOCK g_fence_lock;

static Fence fence_read(VkFence fence)
{
    Fence f = {FENCE_HOST, -1, 0, 0};
    AcquireSRWLockShared(&g_fence_lock);
    Fence *r = map_get(&g_fences, (uint64_t)fence);
    if (r)
        f = *r;
    ReleaseSRWLockShared(&g_fence_lock);
    return f;
}

static void fence_set(VkFence fence, Fence f)
{
    AcquireSRWLockExclusive(&g_fence_lock);
    Fence *r = map_get(&g_fences, (uint64_t)fence);
    if (r)
        *r = f;
    ReleaseSRWLockExclusive(&g_fence_lock);
}

static void fence_submitted(VkFence fence, VkQueue queue, uint64_t value)
{
    queue->fence_value = value;
    fence_set(fence, (Fence){FENCE_PENDING, queue->fence_slot, queue->fence_timeline, value});
}

static BOOL fence_reached(const Fence *f)
{
    return f->state == FENCE_SIGNALED ||
           (f->state == FENCE_PENDING && (__atomic_load_n(&mvk_mirrors()[f->slot], __ATOMIC_ACQUIRE) >= f->value ||
                                          __atomic_load_n(&g_mirrors[f->slot].signaled, __ATOMIC_ACQUIRE) >= f->value));
}

/* an idle queue has signaled every fence submitted to it, whether or not the host's watcher has moved the mirror yet. */
static void fence_idle(VkQueue queue)
{
    if (queue->fence_state <= 0)
        return;
    AcquireSRWLockExclusive(&g_mirror_lock);
    if (g_mirrors[queue->fence_slot].signaled < queue->fence_value)
        __atomic_store_n(&g_mirrors[queue->fence_slot].signaled, queue->fence_value, __ATOMIC_RELEASE);
    ReleaseSRWLockExclusive(&g_mirror_lock);
}

/*
 * the host signals a fence just after the timeline semaphore that answered for it, so before a reset or a destroy
 * it waits for those the guest called signaled. that call has no reply and runs ahead of the next one in the ring.
 */
static void fence_settle(uint32_t count, const VkFence *fences)
{
    VkFence pending[64];
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (fence_read(fences[i]).state == FENCE_PENDING)
            pending[n++] = fences[i];
        if (n == 64 || (n && i + 1 == count)) {
            MvkEncoder *e = mvk_call_begin(MVK_OP_WAIT_FENCES);
            mvk_put_u32(e, n);
            for (uint32_t k = 0; k < n; k++)
                mvk_put_u64(e, (uint64_t)pending[k]);
            mvk_put_u32(e, VK_TRUE);
            mvk_put_u64(e, 1000000000ull);
            mvk_call_end(e);
            n = 0;
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo *info,
                                             const VkAllocationCallbacks *allocator, VkFence *fence)
{
    VkResult r = create_object(MVK_OP_CREATE_FENCE, (uint64_t *)fence,
                               (void (*)(MvkEncoder *, const void *))mvk_put_VkFenceCreateInfo, info);
    Fence *f = zalloc(sizeof(*f));
    if (f) {
        *f = (Fence){(info->flags & VK_FENCE_CREATE_SIGNALED_BIT) ? FENCE_SIGNALED : FENCE_HOST, -1, 0, 0};
        AcquireSRWLockExclusive(&g_fence_lock);
        map_put(&g_fences, (uint64_t)*fence, f);
        ReleaseSRWLockExclusive(&g_fence_lock);
    }
    return r;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator)
{
    if (!fence)
        return;
    fence_settle(1, &fence);
    AcquireSRWLockExclusive(&g_fence_lock);
    zfree(map_take(&g_fences, (uint64_t)fence));
    ReleaseSRWLockExclusive(&g_fence_lock);
    destroy_object(MVK_OP_DESTROY_FENCE, (uint64_t)fence);
}

/* timeout is in nanoseconds; INFINITE for the longest. */
static ULONG remaining_ms(uint64_t timeout, ULONGLONG start)
{
    if (timeout == UINT64_MAX)
        return INFINITE;
    ULONGLONG total = timeout / 1000000 + 1, spent = GetTickCount64() - start;
    return spent >= total ? 0 : (ULONG)(total - spent);
}

/*
 * a host wait would block every call of the process, so the guest polls with
 * zero timeouts: fences after short sleeps, semaphores after the host's
 * watcher reports one reached its value.
 */
static VkResult wait_fences_now(uint32_t count, const VkFence *fences, VkBool32 all)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_WAIT_FENCES);
    mvk_put_u32(e, count);
    for (uint32_t i = 0; i < count; i++)
        mvk_put_u64(e, (uint64_t)fences[i]);
    mvk_put_u32(e, all);
    mvk_put_u64(e, 0);
    return result_call(e);
}

/* fences the guest answers for wait as their timeline semaphores do: on the mirrors, then for the host's events. */
static BOOL wait_fences_mirrored(VkDevice device, uint32_t count, const VkFence *fences, VkBool32 all,
                                 uint64_t timeout, VkResult *result)
{
    VkSemaphore timelines[64];
    uint64_t values[64];
    uint32_t left = 0;
    if (!count || count > 64)
        return FALSE;
    for (uint32_t i = 0; i < count; i++) {
        Fence f = fence_read(fences[i]);
        if (f.state == FENCE_HOST)
            return FALSE;
        BOOL reached = fence_reached(&f);
        if (reached && !all) {
            *result = VK_SUCCESS;
            return TRUE;
        }
        if (!reached) {
            timelines[left] = (VkSemaphore)f.timeline;
            values[left++] = f.value;
        }
    }
    if (!left)
        *result = VK_SUCCESS;
    else if (mvk_lost())
        *result = VK_ERROR_DEVICE_LOST;
    else if (!timeout)
        *result = VK_TIMEOUT;
    else {
        VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, all ? 0 : VK_SEMAPHORE_WAIT_ANY_BIT,
                                    left, timelines, values};
        *result = vkWaitSemaphores(device, &wait, timeout);
    }
    return TRUE;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t count, const VkFence *fences, VkBool32 all,
                                               uint64_t timeout)
{
    VkResult mirrored;
    if (wait_fences_mirrored(device, count, fences, all, timeout, &mirrored))
        return mirrored;
    ULONGLONG start = GetTickCount64();
    for (UINT round = 0;; round++) {
        VkResult r = wait_fences_now(count, fences, all);
        if (r != VK_TIMEOUT || !timeout || !remaining_ms(timeout, start))
            return r;
        if (round < 64)
            YieldProcessor();
        else if (round < 128)
            SwitchToThread();
        else
            Sleep(1);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device, VkFence fence)
{
    Fence f = fence_read(fence);
    if (f.state != FENCE_HOST)
        return fence_reached(&f) ? VK_SUCCESS : mvk_lost() ? VK_ERROR_DEVICE_LOST : VK_NOT_READY;
    MvkEncoder *e = mvk_call_begin(MVK_OP_FENCE_STATUS);
    mvk_put_u64(e, (uint64_t)fence);
    return result_call(e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device, uint32_t count, const VkFence *fences)
{
    fence_settle(count, fences);
    for (uint32_t i = 0; i < count; i++)
        fence_set(fences[i], (Fence){FENCE_HOST, -1, 0, 0});
    MvkEncoder *e = mvk_call_begin(MVK_OP_RESET_FENCES);
    mvk_put_u32(e, count);
    for (uint32_t i = 0; i < count; i++)
        mvk_put_u64(e, (uint64_t)fences[i]);
    mvk_call_end(e);
    return VK_SUCCESS;
}

static void mirror(uint64_t semaphore, uint64_t initial)
{
    volatile uint64_t *slots = mvk_mirrors();
    if (!slots)
        return;
    uint64_t done = mvk_done_serial();
    AcquireSRWLockExclusive(&g_mirror_lock);
    int slot = -1;
    for (int i = 0; i < MVK_MIRRORS && slot < 0; i++)
        if (!g_mirrors[i].semaphore && g_mirrors[i].freed <= done)
            slot = i;
    if (slot >= 0) {
        g_mirrors[slot] = (Mirror){semaphore, 0, initial};
        __atomic_store_n(&slots[slot], initial, __ATOMIC_RELEASE);
        MvkEncoder *e = mvk_call_begin(MVK_OP_MIRROR_SEMAPHORE);
        mvk_put_u64(e, semaphore);
        mvk_put_u32(e, (uint32_t)slot);
        mvk_call_end(e);
    }
    ReleaseSRWLockExclusive(&g_mirror_lock);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *info,
                                                 const VkAllocationCallbacks *allocator, VkSemaphore *semaphore)
{
    VkResult r = create_object(MVK_OP_CREATE_SEMAPHORE, (uint64_t *)semaphore,
                               (void (*)(MvkEncoder *, const void *))mvk_put_VkSemaphoreCreateInfo, info);
    for (const VkBaseInStructure *p = info->pNext; p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO &&
            ((const VkSemaphoreTypeCreateInfo *)p)->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE)
            mirror((uint64_t)*semaphore, ((const VkSemaphoreTypeCreateInfo *)p)->initialValue);
    return r;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(VkDevice device, VkSemaphore semaphore,
                                              const VkAllocationCallbacks *allocator)
{
    if (!semaphore)
        return;
    AcquireSRWLockExclusive(&g_mirror_lock);
    int slot = find_mirror((uint64_t)semaphore);
    destroy_object(MVK_OP_DESTROY_SEMAPHORE, (uint64_t)semaphore);
    if (slot >= 0)
        g_mirrors[slot] = (Mirror){0, mvk_last_serial(), 0};
    ReleaseSRWLockExclusive(&g_mirror_lock);
}

/* the queue's timeline semaphore for fences, made at its first fenced submit where the device has timelines. */
static BOOL fence_timeline(VkQueue queue)
{
    if (!queue->fence_state) {
        queue->fence_state = -1;
        VkPhysicalDeviceTimelineSemaphoreFeatures timeline = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
        VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &timeline};
        vkGetPhysicalDeviceFeatures2(queue->device->physical, &features);
        VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr, VK_SEMAPHORE_TYPE_TIMELINE, 0};
        VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type, 0};
        VkSemaphore made;
        if (timeline.timelineSemaphore && mvk_mirrors() &&
            vkCreateSemaphore(queue->device, &info, nullptr, &made) == VK_SUCCESS) {
            AcquireSRWLockShared(&g_mirror_lock);
            int slot = find_mirror((uint64_t)made);
            ReleaseSRWLockShared(&g_mirror_lock);
            if (slot >= 0) {
                queue->fence_timeline = (uint64_t)made;
                queue->fence_slot = slot;
                queue->fence_state = 1;
            } else {
                vkDestroySemaphore(queue->device, made, nullptr);
            }
        }
    }
    return queue->fence_state > 0;
}

static VkResult get_semaphore_value(VkDevice device, VkSemaphore semaphore, uint64_t *value);

/* when the mirrored values already satisfy the wait, no call is needed. */
static BOOL mirrors_reached(VkDevice device, const VkSemaphoreWaitInfo *info)
{
    BOOL any = (info->flags & VK_SEMAPHORE_WAIT_ANY_BIT) != 0;
    for (uint32_t i = 0; i < info->semaphoreCount; i++) {
        AcquireSRWLockShared(&g_mirror_lock);
        int slot = find_mirror((uint64_t)info->pSemaphores[i]);
        ReleaseSRWLockShared(&g_mirror_lock);
        uint64_t value = 0;
        if (slot < 0 || get_semaphore_value(device, info->pSemaphores[i], &value) != VK_SUCCESS)
            return FALSE;
        BOOL reached = value >= info->pValues[i];
        if (any && reached)
            return TRUE;
        if (!any && !reached)
            return FALSE;
    }
    return !any && info->semaphoreCount;
}

#define MIRROR_WAIT 64

/* the slots of a wait whose semaphores are all mirrored. */
static BOOL mirror_slots(const VkSemaphoreWaitInfo *info, int *slots)
{
    if (!mvk_mirrors() || !info->semaphoreCount || info->semaphoreCount > MIRROR_WAIT)
        return FALSE;
    BOOL all = TRUE;
    AcquireSRWLockShared(&g_mirror_lock);
    for (uint32_t i = 0; i < info->semaphoreCount && all; i++)
        all = info->pSemaphores[i] && (slots[i] = find_mirror((uint64_t)info->pSemaphores[i])) >= 0;
    ReleaseSRWLockShared(&g_mirror_lock);
    return all;
}

static BOOL slots_reached(const VkSemaphoreWaitInfo *info, const int *slots)
{
    BOOL any = (info->flags & VK_SEMAPHORE_WAIT_ANY_BIT) != 0;
    volatile uint64_t *host = mvk_mirrors();
    for (uint32_t i = 0; i < info->semaphoreCount; i++) {
        BOOL reached = __atomic_load_n(&host[slots[i]], __ATOMIC_ACQUIRE) >= info->pValues[i] ||
                       __atomic_load_n(&g_mirrors[slots[i]].signaled, __ATOMIC_ACQUIRE) >= info->pValues[i];
        if (any && reached)
            return TRUE;
        if (!any && !reached)
            return FALSE;
    }
    return !any;
}

/*
 * the host's watcher moves a mirror before it counts the event of a watch, so a wait on mirrored semaphores reads
 * them and needs no reply: a reply waits behind everything the ring holds. the host is still asked after a
 * round without news, since it is the one that learns of a lost device.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo *info, uint64_t timeout)
{
    int slots[MIRROR_WAIT];
    BOOL mirrored = mirror_slots(info, slots);
    if (mirrored) {
        if (slots_reached(info, slots))
            return VK_SUCCESS;
        if (!timeout)
            return VK_TIMEOUT;
        /* a short spin catches work about to finish without sleeping. */
        for (int i = 0; i < 20000; i++) {
            YieldProcessor();
            if (slots_reached(info, slots))
                return VK_SUCCESS;
        }
    } else if (mirrors_reached(device, info)) {
        return VK_SUCCESS;
    }
    ULONGLONG start = GetTickCount64();
    for (BOOL ask = !mirrored;;) {
        uint64_t seen = mvk_events();
        VkResult r = VK_TIMEOUT;
        if (mirrored && slots_reached(info, slots))
            return VK_SUCCESS;
        if (ask) {
            MvkEncoder *e = mvk_call_begin(MVK_OP_WAIT_SEMAPHORES);
            mvk_put_VkSemaphoreWaitInfo(e, info);
            mvk_put_u64(e, 0);
            r = result_call(e);
        }
        ULONG left = timeout ? remaining_ms(timeout, start) : 0;
        if (r != VK_TIMEOUT || !left)
            return r;
        MvkEncoder *e = mvk_call_begin(MVK_OP_WATCH_SEMAPHORES);
        mvk_put_u32(e, info->semaphoreCount);
        for (uint32_t i = 0; i < info->semaphoreCount; i++) {
            mvk_put_u64(e, (uint64_t)info->pSemaphores[i]);
            mvk_put_u64(e, info->pValues[i]);
        }
        mvk_call_end(e);
        /* poll at least every 100 ms in case a wake up is lost. */
        BOOL news = mvk_wait_events(seen, left < 100 ? left : 100, !mirrored);
        ask = !mirrored || !news;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t *value)
{
    return get_semaphore_value(device, semaphore, value);
}

static VkResult get_semaphore_value(VkDevice device, VkSemaphore semaphore, uint64_t *value)
{
    AcquireSRWLockShared(&g_mirror_lock);
    int slot = semaphore ? find_mirror((uint64_t)semaphore) : -1;
    if (slot >= 0) {
        uint64_t v = __atomic_load_n(&mvk_mirrors()[slot], __ATOMIC_ACQUIRE);
        *value = v > g_mirrors[slot].signaled ? v : g_mirrors[slot].signaled;
        ReleaseSRWLockShared(&g_mirror_lock);
        return VK_SUCCESS;
    }
    ReleaseSRWLockShared(&g_mirror_lock);
    MvkEncoder *e = mvk_call_begin(MVK_OP_SEMAPHORE_VALUE);
    mvk_put_u64(e, (uint64_t)semaphore);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult r = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    *value = d ? mvk_get_u64(d) : 0;
    mvk_call_done(d);
    return r;
}

/* the union's target crosses the wire, with ids for the sampler and view; the host returns its driver's bytes. */
VKAPI_ATTR void VKAPI_CALL vkGetDescriptorEXT(VkDevice device, const VkDescriptorGetInfoEXT *info, size_t size,
                                              void *out)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_GET_DESCRIPTOR);
    mvk_put_u32(e, (uint32_t)info->type);
    const VkDescriptorImageInfo *image = nullptr;
    const VkDescriptorAddressInfoEXT *address = nullptr;
    switch (info->type) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        mvk_put_u64(e, info->data.pSampler ? (uint64_t)*info->data.pSampler : 0);
        break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        image = info->data.pCombinedImageSampler;
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        image = info->data.pSampledImage;
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        image = info->data.pStorageImage;
        break;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        image = info->data.pInputAttachmentImage;
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        address = info->data.pUniformTexelBuffer;
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        address = info->data.pStorageTexelBuffer;
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        address = info->data.pUniformBuffer;
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        address = info->data.pStorageBuffer;
        break;
    case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
    case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV:
        mvk_put_u64(e, info->data.accelerationStructure);
        break;
    default:
        break;
    }
    if (info->type <= VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT && info->type != VK_DESCRIPTOR_TYPE_SAMPLER) {
        if (image || address) {
            mvk_put_u32(e, 1);
            if (image) {
                mvk_put_u64(e, (uint64_t)image->sampler);
                mvk_put_u64(e, (uint64_t)image->imageView);
                mvk_put_u32(e, (uint32_t)image->imageLayout);
            } else {
                mvk_put_u64(e, address->address);
                mvk_put_u64(e, address->range);
                mvk_put_u32(e, (uint32_t)address->format);
            }
        } else {
            mvk_put_u32(e, 0);
        }
    }
    mvk_put_u64(e, (uint64_t)size);
    MvkDecoder *d = mvk_call_wait(e);
    if (d)
        mvk_get_bytes(d, out, size);
    else
        memset(out, 0, size);
    mvk_call_done(d);
}

/* capture and replay needs ids stable across runs, which the protocol does not keep. */
VKAPI_ATTR VkResult VKAPI_CALL vkGetBufferOpaqueCaptureDescriptorDataEXT(VkDevice device,
                                                                         const VkBufferCaptureDescriptorDataInfoEXT *info,
                                                                         void *data)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageOpaqueCaptureDescriptorDataEXT(VkDevice device,
                                                                        const VkImageCaptureDescriptorDataInfoEXT *info,
                                                                        void *data)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageViewOpaqueCaptureDescriptorDataEXT(
    VkDevice device, const VkImageViewCaptureDescriptorDataInfoEXT *info, void *data)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSamplerOpaqueCaptureDescriptorDataEXT(
    VkDevice device, const VkSamplerCaptureDescriptorDataInfoEXT *info, void *data)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT(
    VkDevice device, const VkAccelerationStructureCaptureDescriptorDataInfoEXT *info, void *data)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo *info)
{
    AcquireSRWLockExclusive(&g_mirror_lock);
    int slot = find_mirror((uint64_t)info->semaphore);
    if (slot >= 0 && info->value > g_mirrors[slot].signaled)
        g_mirrors[slot].signaled = info->value;
    ReleaseSRWLockExclusive(&g_mirror_lock);
    MvkEncoder *e = mvk_call_begin(MVK_OP_SIGNAL_SEMAPHORE);
    mvk_put_VkSemaphoreSignalInfo(e, info);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateEvent(VkDevice device, const VkEventCreateInfo *info,
                                             const VkAllocationCallbacks *allocator, VkEvent *event)
{
    return create_object(MVK_OP_CREATE_EVENT, (uint64_t *)event, (void (*)(MvkEncoder *, const void *))mvk_put_VkEventCreateInfo,
                         info);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks *allocator)
{
    destroy_object(MVK_OP_DESTROY_EVENT, (uint64_t)event);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(VkDevice device, VkEvent event)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_EVENT_STATUS);
    mvk_put_u64(e, (uint64_t)event);
    return result_call(e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(VkDevice device, VkEvent event)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_SET_EVENT);
    mvk_put_u64(e, (uint64_t)event);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(VkDevice device, VkEvent event)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_RESET_EVENT);
    mvk_put_u64(e, (uint64_t)event);
    mvk_call_end(e);
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ descriptors */

/*
 * the guest counts what a pool has left, so an allocation that fits is not waited for: a pool with no set freed since
 * it was made or reset cannot refuse one within its sets and its descriptors of each type. the count is kept for the
 * types a descriptor of a set takes one of. inline uniform blocks are counted in bytes, a mutable descriptor takes
 * from several types, a combined image sampler with an immutable sampler may take several for a multi planar format
 * and a binding of variable size takes what the allocation says: a layout or a pool with one of those asks the host.
 */
#define POOL_TYPES 24

typedef struct Counts {
    uint32_t types;
    struct {
        VkDescriptorType type;
        uint32_t count;
    } of[POOL_TYPES];
} Counts;

typedef struct SetLayout {
    BOOL counted;
    Counts needs;
} SetLayout;

typedef struct SetPool {
    BOOL counted;
    /* no set freed and none of a layout that is not counted, since the pool was made or reset. */
    BOOL exact;
    uint32_t max_sets, sets;
    Counts sizes, used;
} SetPool;

static Map g_set_layouts, g_set_pools;

static BOOL counts_add(Counts *c, VkDescriptorType type, uint32_t count)
{
    if (type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK || type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT)
        return FALSE;
    for (uint32_t i = 0; i < c->types; i++)
        if (c->of[i].type == type) {
            c->of[i].count += count;
            return TRUE;
        }
    if (c->types == POOL_TYPES)
        return FALSE;
    c->of[c->types].type = type;
    c->of[c->types++].count = count;
    return TRUE;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *info,
                                                           const VkAllocationCallbacks *allocator,
                                                           VkDescriptorSetLayout *layout)
{
    VkResult r = mvk_raw_vkCreateDescriptorSetLayout(device, info, allocator, layout);
    SetLayout *l = r == VK_SUCCESS ? zalloc(sizeof(*l)) : nullptr;
    if (!l)
        return r;
    l->counted = !(info->flags & ~(VkDescriptorSetLayoutCreateFlags)VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT);
    for (const VkBaseInStructure *p = info->pNext; p; p = p->pNext) {
        const VkDescriptorSetLayoutBindingFlagsCreateInfo *flags = (const void *)p;
        if (p->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO)
            continue;
        for (uint32_t i = 0; i < flags->bindingCount; i++)
            if (flags->pBindingFlags[i] & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)
                l->counted = FALSE;
    }
    for (uint32_t i = 0; i < info->bindingCount && l->counted; i++) {
        const VkDescriptorSetLayoutBinding *b = &info->pBindings[i];
        if (!b->descriptorCount)
            continue;
        if (b->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && b->pImmutableSamplers)
            l->counted = FALSE;
        else
            l->counted = counts_add(&l->needs, b->descriptorType, b->descriptorCount);
    }
    map_put(&g_set_layouts, (uint64_t)*layout, l);
    return r;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout layout,
                                                        const VkAllocationCallbacks *allocator)
{
    if (layout)
        zfree(map_take(&g_set_layouts, (uint64_t)layout));
    mvk_raw_vkDestroyDescriptorSetLayout(device, layout, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *info,
                                                      const VkAllocationCallbacks *allocator, VkDescriptorPool *pool)
{
    VkResult r = create_object(MVK_OP_CREATE_DESCRIPTOR_POOL, (uint64_t *)pool,
                               (void (*)(MvkEncoder *, const void *))mvk_put_VkDescriptorPoolCreateInfo, info);
    SetPool *p = zalloc(sizeof(*p));
    if (p) {
        p->counted = p->exact = TRUE;
        p->max_sets = info->maxSets;
        for (uint32_t i = 0; i < info->poolSizeCount && p->counted; i++)
            p->counted = counts_add(&p->sizes, info->pPoolSizes[i].type, info->pPoolSizes[i].descriptorCount);
        map_put(&g_set_pools, (uint64_t)*pool, p);
    }
    return r;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool pool,
                                                   const VkAllocationCallbacks *allocator)
{
    if (pool)
        zfree(map_take(&g_set_pools, (uint64_t)pool));
    destroy_object(MVK_OP_DESTROY_DESCRIPTOR_POOL, (uint64_t)pool);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice device, VkDescriptorPool pool,
                                                     VkDescriptorPoolResetFlags flags)
{
    SetPool *p = map_get(&g_set_pools, (uint64_t)pool);
    if (p) {
        p->sets = 0;
        p->used = (Counts){};
        p->exact = TRUE;
    }
    MvkEncoder *e = mvk_call_begin(MVK_OP_RESET_DESCRIPTOR_POOL);
    mvk_put_u64(e, (uint64_t)pool);
    mvk_call_end(e);
    return VK_SUCCESS;
}

/* what the sets asked for take from the pool, FALSE when one of their layouts is not counted. */
static BOOL sets_need(const VkDescriptorSetAllocateInfo *info, Counts *need)
{
    *need = (Counts){};
    if (info->pNext)
        return FALSE;
    for (uint32_t i = 0; i < info->descriptorSetCount; i++) {
        const SetLayout *l = map_get(&g_set_layouts, (uint64_t)info->pSetLayouts[i]);
        if (!l || !l->counted)
            return FALSE;
        for (uint32_t k = 0; k < l->needs.types; k++)
            if (!counts_add(need, l->needs.of[k].type, l->needs.of[k].count))
                return FALSE;
    }
    return TRUE;
}

static BOOL pool_fits(const SetPool *p, uint32_t sets, const Counts *need)
{
    if (!p->counted || !p->exact || p->sets > p->max_sets || sets > p->max_sets - p->sets)
        return FALSE;
    for (uint32_t i = 0; i < need->types; i++) {
        uint32_t size = 0, used = 0;
        for (uint32_t k = 0; k < p->sizes.types; k++)
            if (p->sizes.of[k].type == need->of[i].type)
                size = p->sizes.of[k].count;
        for (uint32_t k = 0; k < p->used.types; k++)
            if (p->used.of[k].type == need->of[i].type)
                used = p->used.of[k].count;
        if (used > size || need->of[i].count > size - used)
            return FALSE;
    }
    return TRUE;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *info,
                                                        VkDescriptorSet *sets)
{
    SetPool *p = map_get(&g_set_pools, (uint64_t)info->descriptorPool);
    Counts need;
    BOOL known = p && sets_need(info, &need);
    BOOL fits = known && pool_fits(p, info->descriptorSetCount, &need);
    MvkEncoder *e = mvk_call_begin(MVK_OP_ALLOCATE_DESCRIPTOR_SETS);
    mvk_put_VkDescriptorSetAllocateInfo(e, info);
    for (uint32_t i = 0; i < info->descriptorSetCount; i++) {
        sets[i] = (VkDescriptorSet)mvk_new_id();
        mvk_put_u64(e, (uint64_t)sets[i]);
    }
    VkResult r = VK_SUCCESS;
    if (fits)
        mvk_call_end(e);
    else
        r = result_call(e);
    if (r != VK_SUCCESS) {
        for (uint32_t i = 0; i < info->descriptorSetCount; i++) {
            mvk_free_id((uint64_t)sets[i]);
            sets[i] = VK_NULL_HANDLE;
        }
        return r;
    }
    if (p && known) {
        p->sets += info->descriptorSetCount;
        for (uint32_t i = 0; i < need.types; i++)
            if (!counts_add(&p->used, need.of[i].type, need.of[i].count))
                p->exact = FALSE;
    } else if (p) {
        p->exact = FALSE;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice device, VkDescriptorPool pool, uint32_t count,
                                                    const VkDescriptorSet *sets)
{
    SetPool *p = map_get(&g_set_pools, (uint64_t)pool);
    if (p)
        p->exact = FALSE;
    MvkEncoder *e = mvk_call_begin(MVK_OP_FREE_DESCRIPTOR_SETS);
    mvk_put_u64(e, (uint64_t)pool);
    mvk_put_u32(e, count);
    for (uint32_t i = 0; i < count; i++)
        mvk_put_u64(e, (uint64_t)sets[i]);
    mvk_call_end(e);
    for (uint32_t i = 0; i < count; i++)
        mvk_free_id((uint64_t)sets[i]);
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ queries */

VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice device, VkQueryPool pool, uint32_t first,
                                                     uint32_t count, size_t size, void *data, VkDeviceSize stride,
                                                     VkQueryResultFlags flags)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_QUERY_POOL_RESULTS);
    mvk_put_u64(e, (uint64_t)pool);
    mvk_put_u32(e, first);
    mvk_put_u32(e, count);
    mvk_put_u64(e, size);
    mvk_put_u64(e, stride);
    mvk_put_u32(e, flags);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult r = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        size_t n = 0;
        const void *bytes = mvk_get_blob(d, &n);
        if (bytes && n <= size)
            memcpy(data, bytes, n);
    }
    mvk_call_done(d);
    return r;
}
