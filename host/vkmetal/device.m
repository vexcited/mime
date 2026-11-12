/*
 * devices and their queue: submissions, fences and semaphores. a queue is a Metal 4 command queue with a shared
 * event it signals after each submission; fences wait for that value. semaphores are shared events too, so
 * waits and signals are the queue's own event commands, across devices as well.
 */
#include "vkm.h"

#include <time.h>

/* ------------------------------------------------------------------ hashing and caches. */

uint64_t vkm_hash(const void *data, size_t size, uint64_t seed)
{
    const uint8_t *p = data;
    uint64_t h = seed ^ 0x9e3779b97f4a7c15ull;
    while (size >= 8) {
        uint64_t v;
        memcpy(&v, p, 8);
        h = (h ^ (v * 0xbf58476d1ce4e5b9ull)) * 0x94d049bb133111ebull;
        h ^= h >> 29;
        p += 8;
        size -= 8;
    }
    while (size--)
        h = (h ^ *p++) * 0x100000001b3ull;
    return h ^ (h >> 32);
}

#define CACHE_BUCKETS 1024

typedef struct VkmCacheEntry {
    struct VkmCacheEntry *next;
    uint64_t hash;
    size_t size;
    id object;
    uint8_t key[];
} VkmCacheEntry;

struct VkmCache {
    VkmCacheEntry *buckets[CACHE_BUCKETS];
};

id vkm_cache_find(VkmCache *cache, const void *key, size_t size, uint64_t hash)
{
    if (!cache)
        return nullptr;
    for (VkmCacheEntry *e = cache->buckets[hash % CACHE_BUCKETS]; e; e = e->next)
        if (e->hash == hash && e->size == size && !memcmp(e->key, key, size))
            return e->object;
    return nullptr;
}

void vkm_cache_add(VkmCache **cache, const void *key, size_t size, uint64_t hash, id object)
{
    if (!*cache && !(*cache = vkm_zalloc(sizeof(**cache))))
        return;
    VkmCacheEntry *e = malloc(sizeof(*e) + size);
    if (!e)
        return;
    e->hash = hash;
    e->size = size;
    e->object = object;
    memcpy(e->key, key, size);
    e->next = (*cache)->buckets[hash % CACHE_BUCKETS];
    (*cache)->buckets[hash % CACHE_BUCKETS] = e;
}

void vkm_cache_free(VkmCache *cache)
{
    if (!cache)
        return;
    for (size_t i = 0; i < CACHE_BUCKETS; i++)
        for (VkmCacheEntry *e = cache->buckets[i], *next; e; e = next) {
            next = e->next;
            [e->object release];
            free(e);
        }
    free(cache);
}

/* ------------------------------------------------------------------ residency. */

void vkm_resident(VkDevice device, id<MTLAllocation> allocation)
{
    pthread_mutex_lock(&device->residency_lock);
    [device->residency addAllocation:allocation];
    device->residency_dirty = true;
    pthread_mutex_unlock(&device->residency_lock);
}

void vkm_unresident(VkDevice device, id<MTLAllocation> allocation)
{
    pthread_mutex_lock(&device->residency_lock);
    [device->residency removeAllocation:allocation];
    device->residency_dirty = true;
    pthread_mutex_unlock(&device->residency_lock);
}

void vkm_residency_commit(VkDevice device)
{
    pthread_mutex_lock(&device->residency_lock);
    if (device->residency_dirty) {
        [device->residency commit];
        device->residency_dirty = false;
    }
    pthread_mutex_unlock(&device->residency_lock);
}

/* ------------------------------------------------------------------ deferred releases. */

typedef struct VkmGrave {
    id object;
    uint64_t value;
    bool resident;
} VkmGrave;

void vkm_release_later(VkDevice device, id object, bool resident)
{
    if (!object)
        return;
    /* a submission being encoded now ends at the next value. */
    uint64_t value = __atomic_load_n(&device->queue.submitted, __ATOMIC_ACQUIRE) + 1;
    pthread_mutex_lock(&device->grave_lock);
    if (device->grave_count == device->grave_capacity) {
        uint32_t capacity = MIME_MAX(device->grave_capacity * 2, 64u);
        VkmGrave *grown = realloc(device->graves, sizeof(*grown) * capacity);
        if (!grown) {
            /* leaking beats freeing what the gpu reads. */
            pthread_mutex_unlock(&device->grave_lock);
            return;
        }
        device->graves = grown;
        device->grave_capacity = capacity;
    }
    device->graves[device->grave_count++] = (VkmGrave){object, value, resident};
    pthread_mutex_unlock(&device->grave_lock);
}

static void graves_release(VkDevice device, uint64_t done)
{
    pthread_mutex_lock(&device->grave_lock);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < device->grave_count; i++) {
        VkmGrave *g = &device->graves[i];
        if (g->value > done) {
            device->graves[kept++] = *g;
            continue;
        }
        if (g->resident)
            vkm_unresident(device, g->object);
        [g->object release];
    }
    device->grave_count = kept;
    pthread_mutex_unlock(&device->grave_lock);
}

/* ------------------------------------------------------------------ new textures. */

typedef struct VkmInit {
    id<MTLTexture> texture;
    uint32_t levels, layers, samples;
    VkExtent3D extent;
    VkImageAspectFlags aspects;
    uint32_t bytes; /* of a texel. */
    bool is1d, is3d;
} VkmInit;

void vkm_image_init(VkDevice device, VkImage image)
{
    /* only render targets can be cleared by a pass; the others are written whole before they are read. */
    if (!(image->texture.usage & MTLTextureUsageRenderTarget))
        return;
    pthread_mutex_lock(&device->init_lock);
    if (device->init_count == device->init_capacity) {
        uint32_t capacity = MIME_MAX(device->init_capacity * 2, 16u);
        VkmInit *grown = realloc(device->inits, sizeof(*grown) * capacity);
        if (!grown) {
            pthread_mutex_unlock(&device->init_lock);
            return;
        }
        device->inits = grown;
        device->init_capacity = capacity;
    }
    device->inits[device->init_count++] = (VkmInit){[image->texture retain], image->levels, image->layers,
                                                    image->samples, image->extent, image->fi->aspects,
                                                    image->fi->bytes, image->type == VK_IMAGE_TYPE_1D,
                                                    image->type == VK_IMAGE_TYPE_3D};
    pthread_mutex_unlock(&device->init_lock);
}

static void clear_pass(id<MTL4CommandBuffer> cmd, const VkmInit *t, uint32_t level, uint32_t slice, uint32_t width,
                       uint32_t height)
{
    MTL4RenderPassDescriptor *desc = [[MTL4RenderPassDescriptor alloc] init];
    MTLRenderPassAttachmentDescriptor *a = nil;
    if (t->aspects & VK_IMAGE_ASPECT_COLOR_BIT)
        a = desc.colorAttachments[0];
    for (int k = 0; k < 2; k++) {
        if (!(t->aspects & VK_IMAGE_ASPECT_COLOR_BIT)) {
            if (!(t->aspects & (k ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT)))
                continue;
            a = k ? (MTLRenderPassAttachmentDescriptor *)desc.stencilAttachment : desc.depthAttachment;
        } else if (k) {
            break;
        }
        a.texture = t->texture;
        a.level = level;
        if (t->is3d)
            a.depthPlane = slice;
        else
            a.slice = slice;
        a.loadAction = MTLLoadActionClear;
        a.storeAction = MTLStoreActionStore;
    }
    desc.renderTargetWidth = width;
    desc.renderTargetHeight = height;
    desc.defaultRasterSampleCount = MIME_MAX(t->samples, 1u);
    id<MTL4RenderCommandEncoder> encoder = [cmd renderCommandEncoderWithDescriptor:desc];
    if (encoder)
        [encoder endEncoding];
    [desc release];
}

/* metal renders to no 1d texture, so its texels are copied from the zero buffer, as many at a time as it holds. */
static void zero_1d(VkDevice device, id<MTL4ComputeCommandEncoder> encoder, const VkmInit *t)
{
    if (!t->bytes || !(t->aspects & VK_IMAGE_ASPECT_COLOR_BIT))
        return;
    uint32_t run = MIME_MAX((uint32_t)device->zero.length / t->bytes, 1u);
    for (uint32_t level = 0; level < t->levels; level++) {
        uint32_t width = MIME_MAX(t->extent.width >> level, 1u);
        for (uint32_t slice = 0; slice < t->layers; slice++)
            for (uint32_t x = 0; x < width; x += run) {
                uint32_t n = MIME_MIN(run, width - x);
                [encoder copyFromBuffer:device->zero
                           sourceOffset:0
                      sourceBytesPerRow:n * t->bytes
                    sourceBytesPerImage:0
                             sourceSize:MTLSizeMake(n, 1, 1)
                              toTexture:t->texture
                       destinationSlice:slice
                       destinationLevel:level
                      destinationOrigin:MTLOriginMake(x, 0, 0)];
            }
    }
}

/* clears the textures placed since the last submission, before its first command buffer. */
static void encode_inits(VkQueue q, id<MTL4CommandBuffer> cmd)
{
    VkDevice device = q->device;
    pthread_mutex_lock(&device->init_lock);
    VkmInit *inits = device->inits;
    uint32_t count = device->init_count;
    device->inits = nullptr;
    device->init_count = device->init_capacity = 0;
    pthread_mutex_unlock(&device->init_lock);
    id<MTL4ComputeCommandEncoder> copies = nil;
    for (uint32_t i = 0; i < count; i++)
        if (inits[i].is1d && device->zero) {
            if (!copies)
                copies = [cmd computeCommandEncoder];
            zero_1d(device, copies, &inits[i]);
        }
    [copies endEncoding];
    for (uint32_t i = 0; i < count; i++) {
        const VkmInit *t = &inits[i];
        for (uint32_t level = 0; !t->is1d && level < t->levels; level++) {
            uint32_t w = MIME_MAX(t->extent.width >> level, 1u), h = MIME_MAX(t->extent.height >> level, 1u);
            uint32_t slices = t->is3d ? MIME_MAX(t->extent.depth >> level, 1u) : t->layers;
            for (uint32_t slice = 0; slice < slices; slice++)
                clear_pass(cmd, t, level, slice, w, h);
        }
        [t->texture release];
    }
    free(inits);
}

/* ------------------------------------------------------------------ upload memory. */

#define CHUNK_SIZE (256u << 10)

static VkmChunk *chunk_take(VkDevice device, uint64_t need)
{
    pthread_mutex_lock(&device->chunk_lock);
    for (VkmChunk **link = &device->chunks; *link; link = &(*link)->next)
        if ((*link)->size >= need) {
            VkmChunk *c = *link;
            *link = c->next;
            pthread_mutex_unlock(&device->chunk_lock);
            c->next = nullptr;
            c->used = 0;
            return c;
        }
    pthread_mutex_unlock(&device->chunk_lock);
    VkmChunk *c = vkm_zalloc(sizeof(*c));
    if (!c)
        return nullptr;
    c->size = MIME_MAX(need, (uint64_t)CHUNK_SIZE);
    c->buffer = [device->mtl newBufferWithLength:c->size
                                         options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
    if (!c->buffer) {
        free(c);
        return nullptr;
    }
    c->map = c->buffer.contents;
    c->gpu = c->buffer.gpuAddress;
    vkm_resident(device, c->buffer);
    return c;
}

void vkm_chunks_release(VkDevice device, VkmChunk *chunks)
{
    if (!chunks)
        return;
    VkmChunk *last = chunks;
    while (last->next)
        last = last->next;
    pthread_mutex_lock(&device->chunk_lock);
    last->next = device->chunks;
    device->chunks = chunks;
    pthread_mutex_unlock(&device->chunk_lock);
}

void *vkm_upload(VkCommandBuffer cmd, uint64_t size, uint64_t align, uint64_t *gpu, id<MTLBuffer> *buffer,
                 uint64_t *offset)
{
    VkmChunk *c = cmd->chunks;
    uint64_t at = c ? MIME_ALIGN_UP(c->used, align) : 0;
    if (!c || at + size > c->size) {
        c = chunk_take(cmd->device, size + align);
        if (!c) {
            cmd->status = VK_ERROR_OUT_OF_DEVICE_MEMORY;
            return nullptr;
        }
        c->next = cmd->chunks;
        cmd->chunks = c;
        at = 0;
    }
    c->used = at + size;
    if (gpu)
        *gpu = c->gpu + at;
    if (buffer)
        *buffer = c->buffer;
    if (offset)
        *offset = at;
    return c->map + at;
}

/* ------------------------------------------------------------------ devices. */

static id<MTL4ArgumentTable> argument_table(id<MTLDevice> mtl)
{
    MTL4ArgumentTableDescriptor *desc = [[MTL4ArgumentTableDescriptor alloc] init];
    desc.maxBufferBindCount = 31;
    desc.maxTextureBindCount = 16;
    desc.maxSamplerStateBindCount = 16;
    desc.supportAttributeStrides = true;
    NSError *error = nil;
    id<MTL4ArgumentTable> table = [mtl newArgumentTableWithDescriptor:desc error:&error];
    [desc release];
    if (!table)
        VKM_ERROR("no argument table: %s", vkm_error_text(error));
    return table;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo *info,
                                                   const VkAllocationCallbacks *a, VkDevice *out)
{
    for (uint32_t i = 0; i < info->enabledExtensionCount; i++) {
        if (!vkm_device_extension(info->ppEnabledExtensionNames[i])) {
            VKM_ERROR("no device extension %s", info->ppEnabledExtensionNames[i]);
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    VkDevice device = vkm_zalloc(sizeof(*device));
    if (!device)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkResult result = VK_SUCCESS;
    @autoreleasepool {
        device->physical = physical;
        device->mtl = [physical->mtl retain];
        pthread_mutex_init(&device->residency_lock, nullptr);
        pthread_mutex_init(&device->chunk_lock, nullptr);
        pthread_mutex_init(&device->init_lock, nullptr);
        pthread_mutex_init(&device->grave_lock, nullptr);
        pthread_mutex_init(&device->memory_lock, nullptr);
        NSError *error = nil;
        device->pipelines = mime_pipeline_cache(device->mtl, "vulkan");
        MTLResidencySetDescriptor *residency_desc = [[MTLResidencySetDescriptor alloc] init];
        residency_desc.initialCapacity = 256;
        device->residency = [device->mtl newResidencySetWithDescriptor:residency_desc error:&error];
        [residency_desc release];
        VkQueue q = &device->queue;
        q->device = device;
        pthread_mutex_init(&q->lock, nullptr);
        q->queue = [device->mtl newMTL4CommandQueue];
        q->event = [device->mtl newSharedEvent];
        q->tables[0] = argument_table(device->mtl);
        q->tables[1] = argument_table(device->mtl);
        device->zero = [device->mtl newBufferWithLength:256
                                                options:MTLResourceStorageModeShared |
                                                        MTLResourceHazardTrackingModeUntracked];
        if (!device->pipelines || !device->residency || !q->queue || !q->event || !q->tables[0] || !q->tables[1]) {
            VKM_ERROR("no Metal 4 device: %s", vkm_error_text(error));
            result = VK_ERROR_INITIALIZATION_FAILED;
        } else {
            [q->queue addResidencySet:device->residency];
            device->zero_gpu = device->zero ? device->zero.gpuAddress : 0;
            if (device->zero)
                vkm_resident(device, device->zero);
        }
    }
    if (result != VK_SUCCESS) {
        void DestroyDevice(VkDevice, const VkAllocationCallbacks *);
        DestroyDevice(device, nullptr);
        return result;
    }
    *out = device;
    return VK_SUCCESS;
}

static void flights_free(VkmFlight *f)
{
    while (f) {
        VkmFlight *next = f->next;
        [f->allocator release];
        for (uint32_t i = 0; i < f->buffer_count; i++)
            [f->buffers[i] release];
        free(f);
        f = next;
    }
}

VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks *a)
{
    if (!device)
        return;
    VkQueue q = &device->queue;
    if (q->event && q->submitted)
        vkm_event_wait(q->event, q->submitted, UINT64_MAX);
    graves_release(device, UINT64_MAX);
    free(device->graves);
    flights_free(q->flights);
    flights_free(q->spare);
    flights_free(q->open);
    for (uint32_t i = 0; i < device->init_count; i++)
        [device->inits[i].texture release];
    free(device->inits);
    for (VkmChunk *c = device->chunks, *next; c; c = next) {
        next = c->next;
        [c->buffer release];
        free(c);
    }
    free(device->memories);
    [device->zero release];
    [q->tables[0] release];
    [q->tables[1] release];
    [q->event release];
    [q->queue release];
    [device->residency release];
    mime_pipeline_cache_flush(device->pipelines);
    [device->mtl release];
    free(device);
}

static VKAPI_ATTR void VKAPI_CALL GetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue *out)
{
    *out = family == 0 && index == 0 ? &device->queue : VK_NULL_HANDLE;
}

static VKAPI_ATTR void VKAPI_CALL GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *info, VkQueue *out)
{
    GetDeviceQueue(device, info->queueFamilyIndex, info->queueIndex, out);
}

/* ------------------------------------------------------------------ waiting. */

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

bool vkm_event_wait(id<MTLSharedEvent> event, uint64_t value, uint64_t timeout_ns)
{
    if (event.signaledValue >= value)
        return true;
    if (!timeout_ns)
        return false;
    uint64_t ms = timeout_ns == UINT64_MAX ? UINT64_MAX : (timeout_ns + 999999) / 1000000;
    return [event waitUntilSignaledValue:value timeoutMS:ms];
}

/* moves finished submissions' objects back for reuse. */
static void flights_retire(VkQueue q)
{
    uint64_t done = q->event.signaledValue;
    graves_release(q->device, done);
    for (VkmFlight **link = &q->flights; *link;) {
        VkmFlight *f = *link;
        if (f->value <= done) {
            *link = f->next;
            [f->allocator reset];
            for (uint32_t i = 0; i < f->buffer_count; i++)
                [f->buffers[i] release];
            f->buffer_count = 0;
            f->next = q->spare;
            q->spare = f;
        } else {
            link = &f->next;
        }
    }
}

static VkmFlight *flight_take(VkQueue q)
{
    VkmFlight *f = q->spare;
    if (f) {
        q->spare = f->next;
        f->next = nullptr;
        return f;
    }
    f = vkm_zalloc(sizeof(*f));
    if (f)
        f->allocator = [q->device->mtl newCommandAllocator];
    return f;
}

/* ------------------------------------------------------------------ submission. */

typedef struct Wait {
    VkSemaphore semaphore;
    uint64_t value;
} Wait;

void vkm_semaphore_wait(VkQueue q, VkSemaphore s, uint64_t value)
{
    if (s->type == VK_SEMAPHORE_TYPE_BINARY)
        value = s->binary;
    if (value)
        [q->queue waitForEvent:s->event value:value];
}

void vkm_semaphore_signal(VkQueue q, VkSemaphore s, uint64_t value)
{
    if (s->type == VK_SEMAPHORE_TYPE_BINARY)
        value = ++s->binary;
    [q->queue signalEvent:s->event value:value];
}

id<MTL4CommandBuffer> vkm_queue_begin(VkQueue q)
{
    if (!q->open) {
        flights_retire(q);
        q->open = flight_take(q);
    }
    VkmFlight *f = q->open;
    if (!f || f->buffer_count == MIME_ARRAY_COUNT(f->buffers))
        return nullptr;
    id<MTL4CommandBuffer> cmd = [q->device->mtl newCommandBuffer];
    [cmd beginCommandBufferWithAllocator:f->allocator];
    f->buffers[f->buffer_count++] = cmd;
    return cmd;
}

void vkm_queue_commit(VkQueue q, id<MTL4CommandBuffer> cmd)
{
    [cmd endCommandBuffer];
    [q->queue commit:&cmd count:1];
}

uint64_t vkm_queue_signal(VkQueue q)
{
    q->submitted++;
    [q->queue signalEvent:q->event value:q->submitted];
    VkmFlight *f = q->open;
    if (f) {
        f->value = q->submitted;
        f->next = q->flights;
        q->flights = f;
        q->open = nullptr;
    }
    return q->submitted;
}

/* one submission: waits, the command buffers in one Metal command buffer, signals, then the queue's value. */
static VkResult submit_one(VkQueue q, uint32_t wait_count, const Wait *waits, uint32_t cmd_count,
                           const VkCommandBuffer *cmds, uint32_t signal_count, const Wait *signals, VkFence fence)
{
    for (uint32_t i = 0; i < wait_count; i++)
        vkm_semaphore_wait(q, waits[i].semaphore, waits[i].value);
    bool any = false;
    for (uint32_t i = 0; i < cmd_count; i++)
        any |= cmds[i]->cmd_size > 0;
    if (any) {
        id<MTL4CommandBuffer> mtl_cmd = vkm_queue_begin(q);
        if (!mtl_cmd)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        encode_inits(q, mtl_cmd);
        for (uint32_t i = 0; i < cmd_count; i++)
            vkm_encode(q, cmds[i], mtl_cmd);
        vkm_queue_commit(q, mtl_cmd);
    }
    for (uint32_t i = 0; i < signal_count; i++)
        vkm_semaphore_signal(q, signals[i].semaphore, signals[i].value);
    uint64_t value = vkm_queue_signal(q);
    if (fence) {
        fence->queue = q;
        fence->value = value;
        fence->signaled = false;
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit2(VkQueue q, uint32_t count, const VkSubmitInfo2 *submits,
                                                   VkFence fence)
{
    @autoreleasepool {
        pthread_mutex_lock(&q->lock);
        vkm_residency_commit(q->device);
        VkResult result = VK_SUCCESS;
        for (uint32_t s = 0; s < count && result == VK_SUCCESS; s++) {
            const VkSubmitInfo2 *si = &submits[s];
            Wait waits[64], signals[64];
            VkCommandBuffer cmds[256];
            uint32_t nw = MIME_MIN(si->waitSemaphoreInfoCount, 64u), ns = MIME_MIN(si->signalSemaphoreInfoCount, 64u);
            uint32_t nc = MIME_MIN(si->commandBufferInfoCount, 256u);
            for (uint32_t i = 0; i < nw; i++)
                waits[i] = (Wait){si->pWaitSemaphoreInfos[i].semaphore, si->pWaitSemaphoreInfos[i].value};
            for (uint32_t i = 0; i < ns; i++)
                signals[i] = (Wait){si->pSignalSemaphoreInfos[i].semaphore, si->pSignalSemaphoreInfos[i].value};
            for (uint32_t i = 0; i < nc; i++)
                cmds[i] = si->pCommandBufferInfos[i].commandBuffer;
            result = submit_one(q, nw, waits, nc, cmds, ns, signals, s + 1 == count ? fence : VK_NULL_HANDLE);
        }
        if (!count && fence)
            result = submit_one(q, 0, nullptr, 0, nullptr, 0, nullptr, fence);
        pthread_mutex_unlock(&q->lock);
        return result;
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue q, uint32_t count, const VkSubmitInfo *submits,
                                                  VkFence fence)
{
    @autoreleasepool {
        pthread_mutex_lock(&q->lock);
        vkm_residency_commit(q->device);
        VkResult result = VK_SUCCESS;
        for (uint32_t s = 0; s < count && result == VK_SUCCESS; s++) {
            const VkSubmitInfo *si = &submits[s];
            const VkTimelineSemaphoreSubmitInfo *values =
                vkm_find(si->pNext, VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO);
            Wait waits[64], signals[64];
            uint32_t nw = MIME_MIN(si->waitSemaphoreCount, 64u), ns = MIME_MIN(si->signalSemaphoreCount, 64u);
            for (uint32_t i = 0; i < nw; i++)
                waits[i] = (Wait){si->pWaitSemaphores[i],
                                  values && i < values->waitSemaphoreValueCount ? values->pWaitSemaphoreValues[i] : 0};
            for (uint32_t i = 0; i < ns; i++)
                signals[i] = (Wait){si->pSignalSemaphores[i], values && i < values->signalSemaphoreValueCount
                                                                  ? values->pSignalSemaphoreValues[i]
                                                                  : 0};
            result = submit_one(q, nw, waits, si->commandBufferCount, si->pCommandBuffers, ns, signals,
                                s + 1 == count ? fence : VK_NULL_HANDLE);
        }
        if (!count && fence)
            result = submit_one(q, 0, nullptr, 0, nullptr, 0, nullptr, fence);
        pthread_mutex_unlock(&q->lock);
        return result;
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL QueueBindSparse(VkQueue q, uint32_t count, const VkBindSparseInfo *info,
                                                      VkFence fence)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

static VKAPI_ATTR VkResult VKAPI_CALL QueueWaitIdle(VkQueue q)
{
    pthread_mutex_lock(&q->lock);
    uint64_t value = q->submitted;
    pthread_mutex_unlock(&q->lock);
    if (value)
        vkm_event_wait(q->event, value, UINT64_MAX);
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL DeviceWaitIdle(VkDevice device)
{
    return QueueWaitIdle(&device->queue);
}

/* ------------------------------------------------------------------ fences. */

static VKAPI_ATTR VkResult VKAPI_CALL CreateFence(VkDevice device, const VkFenceCreateInfo *info,
                                                  const VkAllocationCallbacks *a, VkFence *out)
{
    VkFence fence = vkm_zalloc(sizeof(*fence));
    if (!fence)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    fence->signaled = info->flags & VK_FENCE_CREATE_SIGNALED_BIT;
    *out = fence;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *a)
{
    free(fence);
}

static VKAPI_ATTR VkResult VKAPI_CALL ResetFences(VkDevice device, uint32_t count, const VkFence *fences)
{
    for (uint32_t i = 0; i < count; i++) {
        fences[i]->signaled = false;
        fences[i]->value = 0;
        fences[i]->queue = nullptr;
    }
    return VK_SUCCESS;
}

static bool fence_done(VkFence f, uint64_t timeout_ns)
{
    if (f->signaled)
        return true;
    if (!f->queue)
        return false;
    if (vkm_event_wait(f->queue->event, f->value, timeout_ns)) {
        f->signaled = true;
        return true;
    }
    return false;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetFenceStatus(VkDevice device, VkFence fence)
{
    return fence_done(fence, 0) ? VK_SUCCESS : VK_NOT_READY;
}

static VKAPI_ATTR VkResult VKAPI_CALL WaitForFences(VkDevice device, uint32_t count, const VkFence *fences,
                                                    VkBool32 all, uint64_t timeout)
{
    uint64_t end = timeout == UINT64_MAX ? UINT64_MAX : now_ns() + timeout;
    for (;;) {
        uint32_t done = 0;
        for (uint32_t i = 0; i < count; i++)
            done += fence_done(fences[i], 0);
        if (all ? done == count : done > 0)
            return VK_SUCCESS;
        uint64_t now = now_ns();
        if (now >= end)
            return VK_TIMEOUT;
        /* block on the first pending fence a while, then look at all of them again. */
        for (uint32_t i = 0; i < count; i++)
            if (!fences[i]->signaled && fences[i]->queue) {
                uint64_t slice = all ? end - now : MIME_MIN(end - now, 1000000ull);
                fence_done(fences[i], slice);
                break;
            } else if (!fences[i]->signaled && i + 1 == count) {
                struct timespec ts = {0, 200000};
                nanosleep(&ts, nullptr);
            }
    }
}

/* ------------------------------------------------------------------ semaphores. */

static VKAPI_ATTR VkResult VKAPI_CALL CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *info,
                                                      const VkAllocationCallbacks *a, VkSemaphore *out)
{
    VkSemaphore s = vkm_zalloc(sizeof(*s));
    if (!s)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    const VkSemaphoreTypeCreateInfo *type = vkm_find(info->pNext, VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    const VkImportMetalSharedEventInfoEXT *import =
        vkm_find(info->pNext, VK_STRUCTURE_TYPE_IMPORT_METAL_SHARED_EVENT_INFO_EXT);
    s->type = type ? type->semaphoreType : VK_SEMAPHORE_TYPE_BINARY;
    if (import && import->mtlSharedEvent) {
        s->event = [import->mtlSharedEvent retain];
        s->binary = s->event.signaledValue;
    } else {
        s->event = [device->mtl newSharedEvent];
        if (type && type->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE)
            s->event.signaledValue = type->initialValue;
    }
    if (!s->event) {
        free(s);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    *out = s;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroySemaphore(VkDevice device, VkSemaphore s, const VkAllocationCallbacks *a)
{
    if (!s)
        return;
    [s->event release];
    free(s);
}

static VKAPI_ATTR VkResult VKAPI_CALL GetSemaphoreCounterValue(VkDevice device, VkSemaphore s, uint64_t *value)
{
    *value = s->event.signaledValue;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL SignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo *info)
{
    info->semaphore->event.signaledValue = info->value;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL WaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo *info,
                                                     uint64_t timeout)
{
    bool any = info->flags & VK_SEMAPHORE_WAIT_ANY_BIT;
    uint64_t end = timeout == UINT64_MAX ? UINT64_MAX : now_ns() + timeout;
    for (;;) {
        uint32_t done = 0;
        for (uint32_t i = 0; i < info->semaphoreCount; i++)
            done += info->pSemaphores[i]->event.signaledValue >= info->pValues[i];
        if (any ? done > 0 : done == info->semaphoreCount)
            return VK_SUCCESS;
        uint64_t now = now_ns();
        if (now >= end)
            return VK_TIMEOUT;
        for (uint32_t i = 0; i < info->semaphoreCount; i++)
            if (info->pSemaphores[i]->event.signaledValue < info->pValues[i]) {
                vkm_event_wait(info->pSemaphores[i]->event, info->pValues[i],
                               any ? MIME_MIN(end - now, 1000000ull) : end - now);
                break;
            }
    }
}

/* ------------------------------------------------------------------ events. */

static VKAPI_ATTR VkResult VKAPI_CALL CreateEvent(VkDevice device, const VkEventCreateInfo *info,
                                                  const VkAllocationCallbacks *a, VkEvent *out)
{
    *out = vkm_zalloc(sizeof(**out));
    return *out ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

static VKAPI_ATTR void VKAPI_CALL DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks *a)
{
    free(event);
}

static VKAPI_ATTR VkResult VKAPI_CALL GetEventStatus(VkDevice device, VkEvent event)
{
    return __atomic_load_n(&event->set, __ATOMIC_ACQUIRE) ? VK_EVENT_SET : VK_EVENT_RESET;
}

static VKAPI_ATTR VkResult VKAPI_CALL SetEvent(VkDevice device, VkEvent event)
{
    __atomic_store_n(&event->set, 1, __ATOMIC_RELEASE);
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL ResetEvent(VkDevice device, VkEvent event)
{
    __atomic_store_n(&event->set, 0, __ATOMIC_RELEASE);
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ Metal objects. */

static VKAPI_ATTR void VKAPI_CALL ExportMetalObjectsEXT(VkDevice device, VkExportMetalObjectsInfoEXT *info)
{
    for (VkBaseOutStructure *s = (VkBaseOutStructure *)info->pNext; s; s = s->pNext) {
        switch (s->sType) {
        case VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT:
            ((VkExportMetalDeviceInfoEXT *)s)->mtlDevice = (MTLDevice_id)device->mtl;
            break;
        case VK_STRUCTURE_TYPE_EXPORT_METAL_COMMAND_QUEUE_INFO_EXT:
            ((VkExportMetalCommandQueueInfoEXT *)s)->mtlCommandQueue = (MTLCommandQueue_id)device->queue.queue;
            break;
        case VK_STRUCTURE_TYPE_EXPORT_METAL_SHARED_EVENT_INFO_EXT: {
            VkExportMetalSharedEventInfoEXT *x = (void *)s;
            x->mtlSharedEvent = x->semaphore ? (MTLSharedEvent_id)x->semaphore->event : nullptr;
            break;
        }
        case VK_STRUCTURE_TYPE_EXPORT_METAL_BUFFER_INFO_EXT: {
            VkExportMetalBufferInfoEXT *x = (void *)s;
            x->mtlBuffer = (MTLBuffer_id)x->memory->buffer;
            break;
        }
        case VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT: {
            VkExportMetalTextureInfoEXT *x = (void *)s;
            x->mtlTexture = x->image ? (MTLTexture_id)x->image->texture
                                     : x->imageView ? (MTLTexture_id)x->imageView->texture : nullptr;
            break;
        }
        default:
            break;
        }
    }
}

const VkmEntry vkm_device_entries[] = {
    VKM_ENTRY(CreateDevice),
    VKM_ENTRY(DestroyDevice),
    VKM_ENTRY(GetDeviceQueue),
    VKM_ENTRY(GetDeviceQueue2),
    VKM_ENTRY(QueueSubmit),
    VKM_ENTRY(QueueSubmit2),
    VKM_ALIAS("vkQueueSubmit2KHR", QueueSubmit2),
    VKM_ENTRY(QueueBindSparse),
    VKM_ENTRY(QueueWaitIdle),
    VKM_ENTRY(DeviceWaitIdle),
    VKM_ENTRY(CreateFence),
    VKM_ENTRY(DestroyFence),
    VKM_ENTRY(ResetFences),
    VKM_ENTRY(GetFenceStatus),
    VKM_ENTRY(WaitForFences),
    VKM_ENTRY(CreateSemaphore),
    VKM_ENTRY(DestroySemaphore),
    VKM_ENTRY(GetSemaphoreCounterValue),
    VKM_ALIAS("vkGetSemaphoreCounterValueKHR", GetSemaphoreCounterValue),
    VKM_ENTRY(SignalSemaphore),
    VKM_ALIAS("vkSignalSemaphoreKHR", SignalSemaphore),
    VKM_ENTRY(WaitSemaphores),
    VKM_ALIAS("vkWaitSemaphoresKHR", WaitSemaphores),
    VKM_ENTRY(CreateEvent),
    VKM_ENTRY(DestroyEvent),
    VKM_ENTRY(GetEventStatus),
    VKM_ENTRY(SetEvent),
    VKM_ENTRY(ResetEvent),
    VKM_ENTRY(ExportMetalObjectsEXT),
    {nullptr, nullptr},
};
