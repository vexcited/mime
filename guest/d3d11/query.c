/*
 * queries: events, occlusion, pipeline statistics and timestamps.
 *
 * vulkan queries come from pools of the device, each slot with readback
 * memory its result is copied to, so GetData only reads memory once the
 * submission that ended the query is done. a slot given up is reset from
 * the host when the gpu is done with it, and handed out again: no reset is
 * recorded, so a query may begin inside a render pass instance.
 */
#include "d3d11_private.h"

#define VTBL_FN(f) ((void *)(f))
#define POOL_SLOTS RTL_NUMBER_OF(((D3dQueryPool *)0)->refs)

static const UINT RESULT_STRIDE[D3D_QUERY_KINDS] = {0, 8, 11 * 8, 8, 16, 16, 16, 16};
#define STREAM_KINDS (((1u << D3D11_SO_STREAM_COUNT) - 1) << D3D_QUERY_STREAM0)

static D3dQuery *query_impl(void *iface) { return CONTAINING_RECORD(iface, D3dQuery, iface); }

D3dQuery *D3dQueryFrom(ID3D11Asynchronous *iface) { return iface ? query_impl(iface) : NULL; }

/* ------------------------------------------------------------------ slots */

static D3dQueryPool *new_pool(D3dDevice *device, D3dQueryKind kind)
{
    D3dVk *vk = &device->vk;
    D3dQueryPool *pool = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pool));
    if (!pool)
        return NULL;
    VkQueryPoolCreateInfo info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryCount = POOL_SLOTS;
    pool->kind = kind;
    if (kind == D3D_QUERY_OCCLUSION) {
        info.queryType = VK_QUERY_TYPE_OCCLUSION;
    } else if (kind == D3D_QUERY_STATISTICS) {
        /* the eleven counters in bit order are direct3d's in the order of its structure. */
        info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        info.pipelineStatistics = (1u << 11) - 1;
    } else if (kind >= D3D_QUERY_STREAM0) {
        /* primitives written, then primitives that would have been: direct3d's order too. */
        info.queryType = VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;
    } else {
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    }
    if (vk->vkCreateQueryPool(vk->device, &info, NULL, &pool->pool) != VK_SUCCESS)
        goto fail;
    if (FAILED(D3dAllocBuffer(device, D3D_MEMORY_READBACK, POOL_SLOTS * RESULT_STRIDE[kind], 16, &pool->results)))
        goto fail;
    vk->vkResetQueryPool(vk->device, pool->pool, 0, POOL_SLOTS);
    return pool;
fail:
    if (pool->pool)
        vk->vkDestroyQueryPool(vk->device, pool->pool, NULL);
    HeapFree(GetProcessHeap(), 0, pool);
    return NULL;
}

/* a slot reset and ready to begin: one the gpu is done with, or a new one. */
static BOOL alloc_slot(D3dDevice *device, D3dQueryKind kind, D3dQuerySlot *out)
{
    BOOL ok = TRUE;
    AcquireSRWLockExclusive(&device->query_lock);
    D3dRetiredSlots *retired = &device->query_retired[kind];
    D3dQueryPool *pool = device->query_pools[kind];
    if (retired->count && retired->list[retired->head].serial <= device->completed) {
        *out = retired->list[retired->head].slot;
        retired->head = (retired->head + 1) % retired->capacity;
        retired->count--;
        device->vk.vkResetQueryPool(device->vk.device, out->pool->pool, out->index, 1);
    } else if (pool && pool->used < POOL_SLOTS) {
        *out = (D3dQuerySlot){pool, pool->used++};
    } else if ((pool = new_pool(device, kind))) {
        pool->next = device->query_pools[kind];
        device->query_pools[kind] = pool;
        *out = (D3dQuerySlot){pool, pool->used++};
    } else {
        ok = FALSE;
    }
    ReleaseSRWLockExclusive(&device->query_lock);
    return ok;
}

/* the query lets go of its segments; one no query holds goes back to the pool once the gpu is done with it. */
static void retire_slots(D3dDevice *device, D3dQuery *q)
{
    AcquireSRWLockExclusive(&device->query_lock);
    for (UINT i = 0; i < q->slot_count; i++) {
        if (InterlockedDecrement(&q->slots[i].pool->refs[q->slots[i].index]))
            continue;
        D3dRetiredSlots *retired = &device->query_retired[q->slots[i].pool->kind];
        if (retired->count == retired->capacity) {
            UINT capacity = retired->capacity ? retired->capacity * 2 : 64;
            struct D3dRetiredSlot *list = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
            if (!list)
                continue;
            for (UINT k = 0; k < retired->count; k++)
                list[k] = retired->list[(retired->head + k) % retired->capacity];
            HeapFree(GetProcessHeap(), 0, retired->list);
            retired->list = list;
            retired->head = 0;
            retired->capacity = capacity;
        }
        struct D3dRetiredSlot *r = &retired->list[(retired->head + retired->count++) % retired->capacity];
        r->slot = q->slots[i];
        /* what is recorded now may still use it. */
        r->serial = device->next_serial;
    }
    ReleaseSRWLockExclusive(&device->query_lock);
    q->slot_count = 0;
}

static BOOL push_slot(D3dQuery *q, const D3dQuerySlot *slot)
{
    if (q->slot_count == q->slot_capacity) {
        UINT capacity = q->slot_capacity ? q->slot_capacity * 2 : 2;
        D3dQuerySlot *list = q->slots ? HeapReAlloc(GetProcessHeap(), 0, q->slots, capacity * sizeof(*list))
                                      : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
        if (!list)
            return FALSE;
        q->slots = list;
        q->slot_capacity = capacity;
    }
    q->slots[q->slot_count++] = *slot;
    InterlockedIncrement(&slot->pool->refs[slot->index]);
    return TRUE;
}

void D3dQueryPoolsDestroy(D3dDevice *device)
{
    D3dVk *vk = &device->vk;
    for (UINT k = 0; k < D3D_QUERY_KINDS; k++) {
        while (device->query_pools[k]) {
            D3dQueryPool *pool = device->query_pools[k];
            device->query_pools[k] = pool->next;
            vk->vkDestroyQueryPool(vk->device, pool->pool, NULL);
            D3dFree(device, &pool->results);
            HeapFree(GetProcessHeap(), 0, pool);
        }
        HeapFree(GetProcessHeap(), 0, device->query_retired[k].list);
        device->query_retired[k] = (D3dRetiredSlots){0};
    }
}

/* ------------------------------------------------------------------ segments */

static void copy_later(D3dContext *c, const D3dQuerySlot *slot)
{
    if (c->query_copy_count == c->query_copy_capacity) {
        UINT capacity = c->query_copy_capacity ? c->query_copy_capacity * 2 : 32;
        D3dQuerySlot *list = c->query_copies
                                 ? HeapReAlloc(GetProcessHeap(), 0, c->query_copies, capacity * sizeof(*list))
                                 : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
        if (!list)
            return;
        c->query_copies = list;
        c->query_copy_capacity = capacity;
    }
    c->query_copies[c->query_copy_count++] = *slot;
}

/* a segment of a kind, counting for every query of the kind begun. */
static void open_segment(D3dContext *c, VkCommandBuffer cmd, D3dQueryKind kind)
{
    D3dDevice *device = c->child.device;
    D3dQuerySlot slot;
    BOOL wanted = FALSE;
    for (UINT i = 0; i < c->query_count && !wanted; i++)
        wanted = (c->queries[i]->kinds >> kind) & 1;
    if (!wanted || !alloc_slot(device, kind, &slot))
        return;
    for (UINT i = 0; i < c->query_count; i++)
        if ((c->queries[i]->kinds >> kind) & 1)
            push_slot(c->queries[i], &slot);
    VkQueryControlFlags flags = kind == D3D_QUERY_OCCLUSION && device->has_precise_occlusion
                                    ? VK_QUERY_CONTROL_PRECISE_BIT
                                    : 0;
    if (kind >= D3D_QUERY_STREAM0)
        device->vk.vkCmdBeginQueryIndexedEXT(cmd, slot.pool->pool, slot.index, flags, kind - D3D_QUERY_STREAM0);
    else
        device->vk.vkCmdBeginQuery(cmd, slot.pool->pool, slot.index, flags);
    c->segments[kind] = slot;
    c->segment_open[kind] = TRUE;
}

static void close_segment(D3dContext *c, VkCommandBuffer cmd, D3dQueryKind kind)
{
    if (!c->segment_open[kind])
        return;
    D3dQuerySlot *slot = &c->segments[kind];
    if (kind >= D3D_QUERY_STREAM0)
        c->child.device->vk.vkCmdEndQueryIndexedEXT(cmd, slot->pool->pool, slot->index, kind - D3D_QUERY_STREAM0);
    else
        c->child.device->vk.vkCmdEndQuery(cmd, slot->pool->pool, slot->index);
    copy_later(c, slot);
    c->segment_open[kind] = FALSE;
}

/*
 * begins the segments that count what comes: draws inside a render pass
 * instance, or for pipeline statistics dispatches outside one too.
 */
void D3dQueriesOpen(D3dContext *c, VkCommandBuffer cmd, BOOL drawing)
{
    for (UINT kind = D3D_QUERY_OCCLUSION; kind < D3D_QUERY_KINDS; kind++)
        if (kind != D3D_QUERY_TIMESTAMP && (drawing || kind == D3D_QUERY_STATISTICS) && !c->segment_open[kind])
            open_segment(c, cmd, kind);
}

/* ends the open segments, before the render pass instance or the command buffer ends. */
void D3dQueriesClose(D3dContext *c, VkCommandBuffer cmd)
{
    for (UINT kind = D3D_QUERY_OCCLUSION; kind < D3D_QUERY_KINDS; kind++)
        close_segment(c, cmd, kind);
}

/* what was counted until now is not a query begun or ended now: the next draw begins new segments. */
static void close_kinds(D3dContext *c, UINT kinds)
{
    for (UINT kind = D3D_QUERY_OCCLUSION; kind < D3D_QUERY_KINDS; kind++)
        if (((kinds >> kind) & 1) && c->segment_open[kind])
            close_segment(c, c->cmd->buffer, kind);
}

/* copies the results of the ended segments to their readback memory; outside render pass instances. */
void D3dQueriesCopy(D3dContext *c, VkCommandBuffer cmd)
{
    D3dVk *vk = &c->child.device->vk;
    for (UINT i = 0; i < c->query_copy_count;) {
        D3dQuerySlot *slot = &c->query_copies[i];
        UINT count = 1;
        while (i + count < c->query_copy_count && c->query_copies[i + count].pool == slot->pool &&
               c->query_copies[i + count].index == slot->index + count)
            count++;
        UINT stride = RESULT_STRIDE[slot->pool->kind];
        vk->vkCmdCopyQueryPoolResults(cmd, slot->pool->pool, slot->index, count, slot->pool->results.buffer,
                                      slot->pool->results.offset + (VkDeviceSize)slot->index * stride, stride,
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        i += count;
    }
    c->query_copy_count = 0;
}

/* the context's queries left begun, at its destruction. */
void D3dQueriesRelease(D3dContext *c)
{
    while (c->query_count) {
        D3dQuery *q = c->queries[--c->query_count];
        q->begun = FALSE;
        q->iface.lpVtbl->Release(&q->iface);
    }
    HeapFree(GetProcessHeap(), 0, c->queries);
    HeapFree(GetProcessHeap(), 0, c->query_copies);
    c->queries = NULL;
    c->query_copies = NULL;
    c->query_capacity = c->query_copy_capacity = c->query_copy_count = 0;
}

/* ------------------------------------------------------------------ begin, end and data */

void D3dQueryBegin(D3dContext *c, D3dQuery *q)
{
    /* events and timestamps have no beginning. */
    if (q->desc.Query == D3D11_QUERY_EVENT || q->desc.Query == D3D11_QUERY_TIMESTAMP)
        return;
    if (q->begun)
        D3dQueryEnd(c, q);
    q->begun = TRUE;
    if (c->type == D3D11_DEVICE_CONTEXT_DEFERRED || !q->kinds)
        return;
    retire_slots(c->child.device, q);
    if (c->query_count == c->query_capacity) {
        UINT capacity = c->query_capacity ? c->query_capacity * 2 : 16;
        D3dQuery **list = c->queries ? HeapReAlloc(GetProcessHeap(), 0, c->queries, capacity * sizeof(*list))
                                     : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
        if (!list)
            return;
        c->queries = list;
        c->query_capacity = capacity;
    }
    close_kinds(c, q->kinds);
    q->iface.lpVtbl->AddRef(&q->iface);
    c->queries[c->query_count++] = q;
}

/* the end of a query: the submission that carries what came before it gives its result. */
void D3dQueryEnd(D3dContext *c, D3dQuery *q)
{
    D3dDevice *device = c->child.device;
    BOOL was_begun = q->begun;
    q->begun = FALSE;
    q->ended = TRUE;
    q->polls = 0;
    /* the queries of deferred contexts are not measured. */
    if (c->type == D3D11_DEVICE_CONTEXT_DEFERRED)
        return;
    if (q->kinds == 1u << D3D_QUERY_TIMESTAMP) {
        D3dQuerySlot slot;
        VkCommandBuffer cmd = D3dContextCommands(c, FALSE);
        retire_slots(device, q);
        if (cmd && alloc_slot(device, D3D_QUERY_TIMESTAMP, &slot) && push_slot(q, &slot)) {
            device->vk.vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, slot.pool->pool, slot.index);
            copy_later(c, &slot);
        }
    } else if (q->kinds) {
        if (was_begun) {
            /* the segment counted for this query ends with it; the others go on in a new one. */
            close_kinds(c, q->kinds);
            for (UINT i = 0; i < c->query_count; i++)
                if (c->queries[i] == q) {
                    c->queries[i] = c->queries[--c->query_count];
                    q->iface.lpVtbl->Release(&q->iface);
                    break;
                }
        } else {
            /* an end without a beginning counts nothing. */
            retire_slots(device, q);
        }
    }
    /* with nothing recorded since, what was submitted last gives the result. */
    q->serial = c->cmd ? device->next_serial : device->next_serial - 1;
}

static UINT data_size(D3D11_QUERY query)
{
    switch (query) {
    case D3D11_QUERY_OCCLUSION:
    case D3D11_QUERY_TIMESTAMP:
        return sizeof(UINT64);
    case D3D11_QUERY_TIMESTAMP_DISJOINT:
        return sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT);
    case D3D11_QUERY_PIPELINE_STATISTICS:
        return sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS);
    case D3D11_QUERY_SO_STATISTICS:
    case D3D11_QUERY_SO_STATISTICS_STREAM0:
    case D3D11_QUERY_SO_STATISTICS_STREAM1:
    case D3D11_QUERY_SO_STATISTICS_STREAM2:
    case D3D11_QUERY_SO_STATISTICS_STREAM3:
        return sizeof(D3D11_QUERY_DATA_SO_STATISTICS);
    default:
        return sizeof(BOOL);
    }
}

/* the sum of a query's segments, counter by counter. */
static void sum_slots(const D3dQuery *q, UINT64 *out, UINT counters)
{
    memset(out, 0, counters * sizeof(UINT64));
    for (UINT i = 0; i < q->slot_count; i++) {
        const D3dQueryPool *pool = q->slots[i].pool;
        const UINT64 *values =
            (const UINT64 *)(pool->results.mapped + (SIZE_T)q->slots[i].index * RESULT_STRIDE[pool->kind]);
        for (UINT k = 0; k < counters; k++)
            out[k] += values[k];
    }
}

/*
 * as d3d11.dll checks before asking the driver: no flags but
 * D3D11_ASYNC_GETDATA_DONOTFLUSH, no size without data, the exact size with
 * data, and nothing while the query is begun.
 */
HRESULT D3dQueryGetData(D3dContext *c, D3dQuery *q, void *data, UINT size, UINT flags)
{
    D3dDevice *device = c->child.device;
    if (flags & ~D3D11_ASYNC_GETDATA_DONOTFLUSH)
        return E_INVALIDARG;
    if (!data && size)
        return E_INVALIDARG;
    if (data && size && size != data_size(q->desc.Query))
        return E_INVALIDARG;
    if (!size)
        data = NULL;
    if (q->begun)
        return DXGI_ERROR_INVALID_CALL;
    /* polling without flushes flushes anyway after 254 calls that found nothing. */
    if (q->polls == 0xFE)
        flags &= ~D3D11_ASYNC_GETDATA_DONOTFLUSH;
    if (q->desc.Query != D3D11_QUERY_TIMESTAMP_DISJOINT) {
        if (!q->ended)
            return DXGI_ERROR_INVALID_CALL;
        BOOL ready = TRUE;
        if (q->serial >= device->next_serial) {
            if (flags & D3D11_ASYNC_GETDATA_DONOTFLUSH)
                ready = FALSE;
            else
                D3dContextFlush(c);
        }
        if (ready && D3dCompleted(device) < q->serial)
            ready = FALSE;
        if (!(flags & D3D11_ASYNC_GETDATA_DONOTFLUSH))
            q->polls = 0xFF;
        else if (!ready && q->polls < 0xFE)
            q->polls++;
        if (!ready)
            return S_FALSE;
    }
    if (!data)
        return S_OK;
    memset(data, 0, size);
    switch (q->desc.Query) {
    case D3D11_QUERY_EVENT:
        *(BOOL *)data = TRUE;
        break;
    case D3D11_QUERY_OCCLUSION:
        if (q->kinds)
            sum_slots(q, data, 1);
        else
            *(UINT64 *)data = 1;
        break;
    case D3D11_QUERY_OCCLUSION_PREDICATE: {
        UINT64 samples = 1;
        if (q->kinds)
            sum_slots(q, &samples, 1);
        *(BOOL *)data = samples != 0;
        break;
    }
    case D3D11_QUERY_TIMESTAMP:
        sum_slots(q, data, 1);
        break;
    case D3D11_QUERY_TIMESTAMP_DISJOINT: {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT *d = data;
        d->Frequency = (UINT64)(1e9 / device->properties.limits.timestampPeriod);
        d->Disjoint = FALSE;
        break;
    }
    case D3D11_QUERY_PIPELINE_STATISTICS: {
        D3D11_QUERY_DATA_PIPELINE_STATISTICS *d = data;
        if (q->kinds)
            sum_slots(q, (UINT64 *)d, 11);
        /* below feature level 11_0 there are no hull, domain and compute shaders to count. */
        if (device->feature_level < D3D_FEATURE_LEVEL_11_0)
            d->HSInvocations = d->DSInvocations = d->CSInvocations = 0;
        break;
    }
    case D3D11_QUERY_SO_STATISTICS:
    case D3D11_QUERY_SO_STATISTICS_STREAM0:
    case D3D11_QUERY_SO_STATISTICS_STREAM1:
    case D3D11_QUERY_SO_STATISTICS_STREAM2:
    case D3D11_QUERY_SO_STATISTICS_STREAM3:
        sum_slots(q, data, 2);
        break;
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3: {
        /* a stream overflowed when it needed more room than it wrote; summed over streams, the same. */
        UINT64 counts[2];
        sum_slots(q, counts, 2);
        *(BOOL *)data = counts[1] > counts[0];
        break;
    }
    default:
        break;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ the object */

static BOOL is_predicate(D3D11_QUERY query)
{
    return query == D3D11_QUERY_OCCLUSION_PREDICATE || query == D3D11_QUERY_SO_OVERFLOW_PREDICATE ||
           (query >= D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0 && (query & 1));
}

static HRESULT STDMETHODCALLTYPE query_query(void *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    D3dQuery *q = query_impl(iface);
    void *d3d10 = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild) ||
        IsEqualGUID(riid, &IID_ID3D11Asynchronous) || IsEqualGUID(riid, &IID_ID3D11Query) ||
        IsEqualGUID(riid, &IID_ID3D11Query1) || (is_predicate(q->desc.Query) && IsEqualGUID(riid, &IID_ID3D11Predicate)) ||
        (d3d10 = D3d10AsyncInterface(q, riid))) {
        InterlockedIncrement(&q->child.refs);
        *out = d3d10 ? d3d10 : iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE query_add_ref(void *iface) { return InterlockedIncrement(&query_impl(iface)->child.refs); }

static ULONG STDMETHODCALLTYPE query_release(void *iface)
{
    D3dQuery *q = query_impl(iface);
    ULONG refs = InterlockedDecrement(&q->child.refs);
    if (!refs) {
        retire_slots(q->child.device, q);
        HeapFree(GetProcessHeap(), 0, q->slots);
        D3dChildRelease(&q->child);
        HeapFree(GetProcessHeap(), 0, q);
    }
    return refs;
}

static void STDMETHODCALLTYPE query_get_device(void *iface, ID3D11Device **device)
{
    D3dDevice *d = query_impl(iface)->child.device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = (ID3D11Device *)&d->iface;
}

static HRESULT STDMETHODCALLTYPE query_get_private_data(void *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&query_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE query_set_private_data(void *iface, REFGUID guid, UINT size, const void *data)
{
    return MimePrivateSet(&query_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE query_set_private_data_interface(void *iface, REFGUID guid, const IUnknown *data)
{
    return MimePrivateSetInterface(&query_impl(iface)->child.data, guid, data);
}

static UINT STDMETHODCALLTYPE query_get_data_size(void *iface) { return data_size(query_impl(iface)->desc.Query); }

static void STDMETHODCALLTYPE query_get_desc(void *iface, D3D11_QUERY_DESC *desc)
{
    D3dQuery *q = query_impl(iface);
    desc->Query = q->desc.Query;
    desc->MiscFlags = q->desc.MiscFlags;
}

static void STDMETHODCALLTYPE query_get_desc1(void *iface, D3D11_QUERY_DESC1 *desc) { *desc = query_impl(iface)->desc; }

static const ID3D11Query1Vtbl query_vtbl = {
    VTBL_FN(query_query),          VTBL_FN(query_add_ref),          VTBL_FN(query_release),
    VTBL_FN(query_get_device),     VTBL_FN(query_get_private_data), VTBL_FN(query_set_private_data),
    VTBL_FN(query_set_private_data_interface), VTBL_FN(query_get_data_size), VTBL_FN(query_get_desc),
    VTBL_FN(query_get_desc1),
};

/* what vulkan counts a query with; none for those only waited on, or what the gpu cannot count. */
/* the kinds of vulkan queries a query counts with; none for those only waited on, or what the gpu cannot count. */
static UINT query_kinds(D3dDevice *device, D3D11_QUERY query)
{
    switch (query) {
    case D3D11_QUERY_OCCLUSION:
    case D3D11_QUERY_OCCLUSION_PREDICATE:
        return 1u << D3D_QUERY_OCCLUSION;
    case D3D11_QUERY_PIPELINE_STATISTICS:
        return device->has_statistics ? 1u << D3D_QUERY_STATISTICS : 0;
    case D3D11_QUERY_TIMESTAMP:
        return 1u << D3D_QUERY_TIMESTAMP;
    case D3D11_QUERY_SO_STATISTICS:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE:
        return device->has_so_queries ? STREAM_KINDS : 0;
    case D3D11_QUERY_SO_STATISTICS_STREAM0:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0:
    case D3D11_QUERY_SO_STATISTICS_STREAM1:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1:
    case D3D11_QUERY_SO_STATISTICS_STREAM2:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2:
    case D3D11_QUERY_SO_STATISTICS_STREAM3:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3:
        return device->has_so_queries ? 1u << (D3D_QUERY_STREAM0 + (query - D3D11_QUERY_SO_STATISTICS_STREAM0) / 2)
                                      : 0;
    default:
        return 0;
    }
}

HRESULT D3dCreateQuery(D3dDevice *device, const D3D11_QUERY_DESC1 *desc, D3dQuery **out)
{
    if (desc->Query > D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3)
        return E_INVALIDARG;
    if (!out)
        return S_FALSE;
    D3dQuery *q = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*q));
    if (!q)
        return E_OUTOFMEMORY;
    q->iface.lpVtbl = &query_vtbl;
    q->desc = *desc;
    q->kinds = query_kinds(device, desc->Query);
    D3dChildInit(&q->child, device);
    *out = q;
    return S_OK;
}
