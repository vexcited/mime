/*
 * devices and what hangs from them that the wire describes by hand:
 * queues, memory, command buffers and their streams, submission,
 * synchronization, descriptor sets and query results.
 */
#include "mvk_guest.h"

/* ------------------------------------------------------------------ guest side maps */

/* a small map from wire ids to guest records, for the objects the guest keeps state about. */
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
    void *v = NULL;
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

/* removal keeps the slot, so probe chains stay whole; a later put reuses it. */
static void *map_take(Map *m, uint64_t id)
{
    void *v = NULL;
    AcquireSRWLockExclusive(&m->lock);
    if (m->size) {
        size_t mask = m->size - 1;
        for (size_t i = mix(id) & mask; m->entries[i].id; i = (i + 1) & mask)
            if (m->entries[i].id == id) {
                v = m->entries[i].value;
                m->entries[i].value = NULL;
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
    *out = NULL;
    for (struct VkQueue_T *q = device->queues; q; q = q->next)
        if (q->family == family && q->index == index && !q->flags) {
            *out = q;
            return;
        }
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *info, VkQueue *out)
{
    *out = NULL;
    for (struct VkQueue_T *q = device->queues; q; q = q->next)
        if (q->family == info->queueFamilyIndex && q->index == info->queueIndex && q->flags == info->flags) {
            *out = q;
            return;
        }
}

/* ------------------------------------------------------------------ memory */

typedef struct Memory {
    void *base;      /* guest ram the host imported, NULL for memory the guest cannot map */
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
    *out = (VkDeviceMemory)id;
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
    /* the ram goes back once the host freed its import. */
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
        *data = NULL;
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

/* the guest and the host share the same ram: nothing to flush or invalidate between them. */
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
    uint64_t serial; /* the host read it by this call */
} Chunk;

/* free chunks of every device, and chunks waiting for the host to read them. */
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
        c->next = NULL;
        c->memory->next_pa = 0;
        c->memory->used = 0;
    }
    return c;
}

typedef struct Stream {
    Chunk *first, *last;
    uint64_t read_serial; /* the call the host reads the stream in, 0 while still recording */
} Stream;

static Map g_streams;

static Stream *stream_of(VkCommandBuffer buffer)
{
    return map_get(&g_streams, buffer->obj.id);
}

/* hands the chunks back, for reuse once the host read them. */
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
    s->first = s->last = NULL;
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

MvkEncoder *mvk_cmd_begin(VkCommandBuffer buffer, uint32_t op)
{
    if (!buffer || !buffer->recording)
        return NULL;
    if (!buffer->scratch) {
        buffer->enc = (MvkEncoder){0};
        buffer->enc.owner = buffer;
        if (!cmd_grow(&buffer->enc, 4096))
            return NULL;
    }
    buffer->enc = (MvkEncoder){buffer->scratch, buffer->scratch, buffer->scratch + buffer->scratch_size, 0, buffer,
                               cmd_grow};
    MvkCallHeader *h = mvk_reserve(&buffer->enc, sizeof(MvkCallHeader), 8);
    h->op = op;
    h->serial = 0;
    h->reply = MVK_NO_REPLY;
    h->reply_size = 0;
    return &buffer->enc;
}

void mvk_cmd_end(VkCommandBuffer buffer, MvkEncoder *e)
{
    mvk_align(e, 8);
    if (e->overflow) {
        mvk_log("vulkan: a recorded command does not fit");
        return;
    }
    MvkCallHeader *h = (MvkCallHeader *)e->start;
    h->size = (uint32_t)(e->p - e->start);
    stream_append(buffer, e->start, h->size);
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
    Stream *s = map_take(&g_streams, buffer->obj.id);
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
    for (VkCommandBuffer b = p ? p->buffers : NULL; b; b = b->pool_next) {
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
        map_put(&g_streams, b->obj.id, s);
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
        n += buffers[i] != NULL;
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
        for (VkCommandBuffer *link = p ? &p->buffers : NULL; link && *link; link = &(*link)->pool_next)
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
    buffer->begin.pNext = NULL;
    buffer->has_inheritance = info->pInheritanceInfo != NULL;
    if (info->pInheritanceInfo) {
        buffer->inheritance = *info->pInheritanceInfo;
        buffer->inheritance.pNext = NULL;
        buffer->begin.pInheritanceInfo = &buffer->inheritance;
    }
    buffer->recording = 1;
    return VK_SUCCESS;
}

/* the host records the whole buffer from its stream now. */
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

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo *submits, VkFence fence)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_QUEUE_SUBMIT);
    mvk_put_u64(e, queue->obj.id);
    mvk_put_u32(e, count);
    for (uint32_t i = 0; i < count; i++)
        mvk_put_VkSubmitInfo(e, &submits[i]);
    mvk_put_u64(e, (uint64_t)fence);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2 *submits,
                                              VkFence fence)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_QUEUE_SUBMIT2);
    mvk_put_u64(e, queue->obj.id);
    mvk_put_u32(e, count);
    for (uint32_t i = 0; i < count; i++)
        mvk_put_VkSubmitInfo2(e, &submits[i]);
    mvk_put_u64(e, (uint64_t)fence);
    mvk_call_end(e);
    return VK_SUCCESS;
}

static VkResult result_call(MvkEncoder *e)
{
    MvkDecoder *d = mvk_call_wait(e);
    VkResult r = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    mvk_call_done(d);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_QUEUE_WAIT_IDLE);
    mvk_put_u64(e, queue->obj.id);
    return result_call(e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_DEVICE_WAIT_IDLE);
    mvk_put_u64(e, device->obj.id);
    return result_call(e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueBindSparse(VkQueue queue, uint32_t count, const VkBindSparseInfo *info,
                                                 VkFence fence)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
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

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo *info,
                                             const VkAllocationCallbacks *allocator, VkFence *fence)
{
    return create_object(MVK_OP_CREATE_FENCE, (uint64_t *)fence, (void (*)(MvkEncoder *, const void *))mvk_put_VkFenceCreateInfo,
                         info);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator)
{
    destroy_object(MVK_OP_DESTROY_FENCE, (uint64_t)fence);
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t count, const VkFence *fences, VkBool32 all,
                                               uint64_t timeout)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_WAIT_FENCES);
    mvk_put_u32(e, count);
    for (uint32_t i = 0; i < count; i++)
        mvk_put_u64(e, (uint64_t)fences[i]);
    mvk_put_u32(e, all);
    mvk_put_u64(e, timeout);
    return result_call(e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device, VkFence fence)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_FENCE_STATUS);
    mvk_put_u64(e, (uint64_t)fence);
    return result_call(e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device, uint32_t count, const VkFence *fences)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_RESET_FENCES);
    mvk_put_u32(e, count);
    for (uint32_t i = 0; i < count; i++)
        mvk_put_u64(e, (uint64_t)fences[i]);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *info,
                                                 const VkAllocationCallbacks *allocator, VkSemaphore *semaphore)
{
    return create_object(MVK_OP_CREATE_SEMAPHORE, (uint64_t *)semaphore,
                         (void (*)(MvkEncoder *, const void *))mvk_put_VkSemaphoreCreateInfo, info);
}

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(VkDevice device, VkSemaphore semaphore,
                                              const VkAllocationCallbacks *allocator)
{
    destroy_object(MVK_OP_DESTROY_SEMAPHORE, (uint64_t)semaphore);
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo *info, uint64_t timeout)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_WAIT_SEMAPHORES);
    mvk_put_VkSemaphoreWaitInfo(e, info);
    mvk_put_u64(e, timeout);
    return result_call(e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t *value)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_SEMAPHORE_VALUE);
    mvk_put_u64(e, (uint64_t)semaphore);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult r = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    *value = d ? mvk_get_u64(d) : 0;
    mvk_call_done(d);
    return r;
}

/*
 * the bytes of a descriptor: what the union points to goes across, ids for
 * the sampler and view, and the host gives the bytes its driver made.
 */
VKAPI_ATTR void VKAPI_CALL vkGetDescriptorEXT(VkDevice device, const VkDescriptorGetInfoEXT *info, size_t size,
                                              void *out)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_GET_DESCRIPTOR);
    mvk_put_u32(e, (uint32_t)info->type);
    const VkDescriptorImageInfo *image = NULL;
    const VkDescriptorAddressInfoEXT *address = NULL;
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

/* capture and replay of descriptors needs ids that last across runs, which the protocol does not keep. */
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

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *info,
                                                      const VkAllocationCallbacks *allocator, VkDescriptorPool *pool)
{
    return create_object(MVK_OP_CREATE_DESCRIPTOR_POOL, (uint64_t *)pool,
                         (void (*)(MvkEncoder *, const void *))mvk_put_VkDescriptorPoolCreateInfo, info);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool pool,
                                                   const VkAllocationCallbacks *allocator)
{
    destroy_object(MVK_OP_DESTROY_DESCRIPTOR_POOL, (uint64_t)pool);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice device, VkDescriptorPool pool,
                                                     VkDescriptorPoolResetFlags flags)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_RESET_DESCRIPTOR_POOL);
    mvk_put_u64(e, (uint64_t)pool);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *info,
                                                        VkDescriptorSet *sets)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_ALLOCATE_DESCRIPTOR_SETS);
    mvk_put_VkDescriptorSetAllocateInfo(e, info);
    for (uint32_t i = 0; i < info->descriptorSetCount; i++) {
        sets[i] = (VkDescriptorSet)mvk_new_id();
        mvk_put_u64(e, (uint64_t)sets[i]);
    }
    VkResult r = result_call(e);
    if (r != VK_SUCCESS)
        for (uint32_t i = 0; i < info->descriptorSetCount; i++) {
            mvk_free_id((uint64_t)sets[i]);
            sets[i] = VK_NULL_HANDLE;
        }
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice device, VkDescriptorPool pool, uint32_t count,
                                                    const VkDescriptorSet *sets)
{
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
