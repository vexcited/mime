/*
 * query pools. occlusion results are Metal visibility results the render passes accumulate into the pool's
 * buffer; a query is available once the queue passes the submission that ended it.
 */
#include "vkm_cmd.h"

static uint32_t values_of(VkQueryType type, VkQueryPipelineStatisticFlags statistics)
{
    if (type == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT)
        return 2;
    return type == VK_QUERY_TYPE_PIPELINE_STATISTICS ? MIME_MAX(__builtin_popcount(statistics), 1) : 1;
}

/* a run of count free queries in the device's occlusion buffer, or -1 when it has none left. */
static int64_t occlusion_take(VkDevice device, uint32_t count)
{
    int64_t found = -1;
    pthread_mutex_lock(&device->occlusion_lock);
    if (!device->occlusion) {
        device->occlusion = [device->mtl newBufferWithLength:VKM_OCCLUSION_QUERIES * 8
                                                     options:MTLResourceStorageModeShared |
                                                             MTLResourceHazardTrackingModeUntracked];
        if (device->occlusion)
            vkm_resident(device, device->occlusion);
    }
    uint32_t run = 0;
    for (uint32_t i = 0; device->occlusion && count && i < VKM_OCCLUSION_QUERIES && found < 0; i++) {
        run = device->occlusion_map[i / 64] >> (i % 64) & 1 ? 0 : run + 1;
        if (run == count)
            found = (int64_t)i + 1 - count;
    }
    for (uint32_t i = 0; found >= 0 && i < count; i++)
        device->occlusion_map[(found + i) / 64] |= 1ull << ((found + i) % 64);
    pthread_mutex_unlock(&device->occlusion_lock);
    return found;
}

static void occlusion_give(VkDevice device, uint64_t first, uint32_t count)
{
    pthread_mutex_lock(&device->occlusion_lock);
    for (uint32_t i = 0; i < count; i++)
        device->occlusion_map[(first + i) / 64] &= ~(1ull << ((first + i) % 64));
    pthread_mutex_unlock(&device->occlusion_lock);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *info,
                                                      const VkAllocationCallbacks *a, VkQueryPool *out)
{
    switch (info->queryType) {
    case VK_QUERY_TYPE_OCCLUSION:
    case VK_QUERY_TYPE_TIMESTAMP:
    case VK_QUERY_TYPE_PIPELINE_STATISTICS:
    case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
    case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR:
    case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR:
    case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SIZE_KHR:
    case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_BOTTOM_LEVEL_POINTERS_KHR: break;
    default: return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    VkQueryPool pool = vkm_zalloc(sizeof(*pool));
    if (!pool)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->type = info->queryType;
    pool->count = info->queryCount;
    pool->statistics = info->pipelineStatistics;
    pool->queue = &device->queue;
    uint64_t size = (uint64_t)MIME_MAX(info->queryCount, 1u) * 8 * values_of(pool->type, pool->statistics);
    int64_t taken = pool->type == VK_QUERY_TYPE_OCCLUSION ? occlusion_take(device, info->queryCount) : -1;
    if (taken >= 0) {
        pool->buffer = [device->occlusion retain];
        pool->base = (uint64_t)taken * 8;
        pool->pooled = true;
    } else {
        /* queries of such a pool and of another cannot share a render pass. */
        pool->buffer = [device->mtl newBufferWithLength:size
                                                options:MTLResourceStorageModeShared |
                                                        MTLResourceHazardTrackingModeUntracked];
    }
    pool->written = vkm_zalloc(sizeof(uint64_t) * MIME_MAX(info->queryCount, 1u));
    if (!pool->buffer || !pool->written) {
        if (pool->pooled)
            occlusion_give(device, pool->base / 8, pool->count);
        [pool->buffer release];
        free(pool->written);
        free(pool);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    pool->values = (uint64_t *)((uint8_t *)pool->buffer.contents + pool->base);
    memset(pool->values, 0, size);
    if (!pool->pooled)
        vkm_resident(device, pool->buffer);
    *out = pool;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyQueryPool(VkDevice device, VkQueryPool pool, const VkAllocationCallbacks *a)
{
    if (!pool)
        return;
    if (pool->pooled) {
        occlusion_give(device, pool->base / 8, pool->count);
        [pool->buffer release];
    } else {
        vkm_release_later(device, pool->buffer, true);
    }
    free(pool->written);
    free(pool);
}

static VKAPI_ATTR void VKAPI_CALL ResetQueryPool(VkDevice device, VkQueryPool pool, uint32_t first, uint32_t count)
{
    uint32_t n = values_of(pool->type, pool->statistics);
    for (uint32_t i = 0; i < count && first + i < pool->count; i++) {
        pool->written[first + i] = 0;
        memset(&pool->values[(uint64_t)(first + i) * n], 0, 8 * n);
    }
}

void vkm_query_written(VkQueryPool pool, uint32_t first, uint32_t count, VkQueue queue, uint64_t value)
{
    pool->queue = queue;
    for (uint32_t i = 0; i < count && first + i < pool->count; i++)
        pool->written[first + i] = value;
}

static bool available(VkQueryPool pool, uint32_t query, bool wait)
{
    uint64_t value = __atomic_load_n(&pool->written[query], __ATOMIC_ACQUIRE);
    if (!value)
        return false;
    return vkm_event_wait(pool->queue->event, value, wait ? UINT64_MAX : 0);
}

static VKAPI_ATTR VkResult VKAPI_CALL GetQueryPoolResults(VkDevice device, VkQueryPool pool, uint32_t first,
                                                          uint32_t count, size_t size, void *data, VkDeviceSize stride,
                                                          VkQueryResultFlags flags)
{
    VkResult result = VK_SUCCESS;
    uint32_t n = values_of(pool->type, pool->statistics);
    bool wide = flags & VK_QUERY_RESULT_64_BIT;
    for (uint32_t i = 0; i < count && first + i < pool->count; i++) {
        uint8_t *out = (uint8_t *)data + (uint64_t)i * stride;
        /* a query no submission ended would be waited on forever. */
        bool ready = available(pool, first + i, (flags & VK_QUERY_RESULT_WAIT_BIT) && pool->written[first + i]);
        if (!ready)
            result = VK_NOT_READY;
        for (uint32_t k = 0; k < n; k++) {
            if (!ready && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
                continue;
            uint64_t v = pool->values[(uint64_t)(first + i) * n + k];
            if (wide)
                memcpy(out + k * 8, &v, 8);
            else
                *(uint32_t *)(out + k * 4) = (uint32_t)v;
        }
        if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
            if (wide)
                *(uint64_t *)(out + n * 8) = ready;
            else
                *(uint32_t *)(out + n * 4) = ready;
        }
    }
    return result;
}

const VkmEntry vkm_query_entries[] = {
    VKM_ENTRY(CreateQueryPool),
    VKM_ENTRY(DestroyQueryPool),
    VKM_ENTRY(ResetQueryPool),
    VKM_ALIAS("vkResetQueryPoolEXT", ResetQueryPool),
    VKM_ENTRY(GetQueryPoolResults),
    {nullptr, nullptr},
};
