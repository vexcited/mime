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

static VKAPI_ATTR VkResult VKAPI_CALL CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *info,
                                                      const VkAllocationCallbacks *a, VkQueryPool *out)
{
    switch (info->queryType) {
    case VK_QUERY_TYPE_OCCLUSION:
    case VK_QUERY_TYPE_TIMESTAMP:
    case VK_QUERY_TYPE_PIPELINE_STATISTICS:
    case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT: break;
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
    pool->buffer = [device->mtl newBufferWithLength:size
                                            options:MTLResourceStorageModeShared |
                                                    MTLResourceHazardTrackingModeUntracked];
    pool->written = vkm_zalloc(sizeof(uint64_t) * MIME_MAX(info->queryCount, 1u));
    if (!pool->buffer || !pool->written) {
        [pool->buffer release];
        free(pool->written);
        free(pool);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    pool->values = pool->buffer.contents;
    memset(pool->values, 0, size);
    vkm_resident(device, pool->buffer);
    *out = pool;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyQueryPool(VkDevice device, VkQueryPool pool, const VkAllocationCallbacks *a)
{
    if (!pool)
        return;
    vkm_release_later(device, pool->buffer, true);
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
