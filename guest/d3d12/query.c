/*
 * query heaps and command signatures. a query heap is a vulkan query pool,
 * pools per stream for stream output statistics. queries are reset when
 * the heap is made and after each resolve, outside any render pass, so a
 * query is ready for its next use once its result was copied out.
 */
#include "d3d12_private.h"

/* ------------------------------------------------------------------ query heaps */

static D12QueryHeap *heap_impl(ID3D12QueryHeap *iface) { return CONTAINING_RECORD(iface, D12QueryHeap, iface); }

D12QueryHeap *D12QueryHeapFrom(ID3D12QueryHeap *iface) { return iface ? heap_impl(iface) : NULL; }

static HRESULT STDMETHODCALLTYPE heap_query(ID3D12QueryHeap *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceChild) || IsEqualGUID(riid, &IID_ID3D12Pageable) ||
        IsEqualGUID(riid, &IID_ID3D12QueryHeap)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE heap_add_ref(ID3D12QueryHeap *iface) { return InterlockedIncrement(&heap_impl(iface)->child.refs); }

static void heap_destroy(D12QueryHeap *heap)
{
    D12Device *device = heap->child.device;
    if (heap->pool)
        device->vk.vkDestroyQueryPool(device->vk.device, heap->pool, NULL);
    for (UINT i = 0; i < 4; i++)
        if (heap->so_pools[i])
            device->vk.vkDestroyQueryPool(device->vk.device, heap->so_pools[i], NULL);
}

static ULONG STDMETHODCALLTYPE heap_release(ID3D12QueryHeap *iface)
{
    D12QueryHeap *heap = heap_impl(iface);
    ULONG refs = InterlockedDecrement(&heap->child.refs);
    if (!refs) {
        heap_destroy(heap);
        D12ChildRelease(&heap->child);
        HeapFree(GetProcessHeap(), 0, heap);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE heap_get_private_data(ID3D12QueryHeap *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&heap_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE heap_set_private_data(ID3D12QueryHeap *iface, REFGUID guid, UINT size,
                                                       const void *data)
{
    return MimePrivateSet(&heap_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE heap_set_private_data_interface(ID3D12QueryHeap *iface, REFGUID guid,
                                                                 const IUnknown *data)
{
    return MimePrivateSetInterface(&heap_impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE heap_set_name(ID3D12QueryHeap *iface, LPCWSTR name) { return S_OK; }

static HRESULT STDMETHODCALLTYPE heap_get_device(ID3D12QueryHeap *iface, REFIID riid, void **device)
{
    return D12ChildGetDevice(&heap_impl(iface)->child, riid, device);
}

static const struct ID3D12QueryHeapVtbl g_heap_vtbl = {
    heap_query,
    heap_add_ref,
    heap_release,
    heap_get_private_data,
    heap_set_private_data,
    heap_set_private_data_interface,
    heap_set_name,
    heap_get_device,
};

/* the statistics direct3d 12 counts, in the order vulkan writes them, which is direct3d's own. */
static const VkQueryPipelineStatisticFlags STATISTICS =
    VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT | VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
    VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
    VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT |
    VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
    VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
    VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT |
    VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT |
    VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;

static VkQueryPool make_pool(D12Device *device, VkQueryType type, UINT count)
{
    VkQueryPoolCreateInfo info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = type;
    info.queryCount = count;
    if (type == VK_QUERY_TYPE_PIPELINE_STATISTICS)
        info.pipelineStatistics = STATISTICS;
    VkQueryPool pool = VK_NULL_HANDLE;
    if (device->vk.vkCreateQueryPool(device->vk.device, &info, NULL, &pool) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    device->vk.vkResetQueryPool(device->vk.device, pool, 0, count);
    return pool;
}

HRESULT D12CreateQueryHeap(D12Device *device, const D3D12_QUERY_HEAP_DESC *desc, D12QueryHeap **out)
{
    if (!desc->Count)
        return E_INVALIDARG;
    D12QueryHeap *heap = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*heap));
    if (!heap)
        return E_OUTOFMEMORY;
    heap->iface.lpVtbl = &g_heap_vtbl;
    heap->child.device = device;
    heap->desc = *desc;
    HRESULT hr = S_OK;
    switch (desc->Type) {
    case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
        heap->pool = make_pool(device, VK_QUERY_TYPE_OCCLUSION, desc->Count);
        break;
    case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:
    case D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP:
        heap->pool = make_pool(device, VK_QUERY_TYPE_TIMESTAMP, desc->Count);
        break;
    case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
        if (device->has_statistics)
            heap->pool = make_pool(device, VK_QUERY_TYPE_PIPELINE_STATISTICS, desc->Count);
        else
            hr = E_INVALIDARG;
        break;
    case D3D12_QUERY_HEAP_TYPE_SO_STATISTICS:
        if (!device->has_so_queries) {
            hr = E_INVALIDARG;
            break;
        }
        for (UINT i = 0; i < 4; i++)
            if (!(heap->so_pools[i] = make_pool(device, VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT, desc->Count)))
                hr = E_OUTOFMEMORY;
        heap->pool = heap->so_pools[0];
        break;
    default:
        hr = E_INVALIDARG;
        break;
    }
    if (SUCCEEDED(hr) && !heap->pool)
        hr = E_OUTOFMEMORY;
    if (FAILED(hr)) {
        heap->pool = VK_NULL_HANDLE;
        heap_destroy(heap);
        HeapFree(GetProcessHeap(), 0, heap);
        return hr;
    }
    if (desc->Type == D3D12_QUERY_HEAP_TYPE_SO_STATISTICS)
        heap->pool = VK_NULL_HANDLE;
    heap->child.refs = 1;
    D12ChildInit(&heap->child, device);
    *out = heap;
    return S_OK;
}

/* the pool a query of a type lives in. */
static VkQueryPool query_pool(D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT *stream)
{
    *stream = 0;
    if (type >= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 && type <= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3) {
        *stream = type - D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
        return heap->so_pools[*stream];
    }
    return heap->pool;
}

void D12BeginQuery(D12CommandList *list, D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    D12Vk *vk = &list->child.device->vk;
    UINT stream;
    VkQueryPool pool = query_pool(heap, type, &stream);
    if (!pool || index >= heap->desc.Count || type == D3D12_QUERY_TYPE_TIMESTAMP)
        return;
    if (type >= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 && type <= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3)
        vk->vkCmdBeginQueryIndexedEXT(list->cmd, pool, index, 0, stream);
    else
        vk->vkCmdBeginQuery(list->cmd, pool, index,
                            type == D3D12_QUERY_TYPE_OCCLUSION ? VK_QUERY_CONTROL_PRECISE_BIT : 0);
}

void D12EndQuery(D12CommandList *list, D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    D12Vk *vk = &list->child.device->vk;
    UINT stream;
    VkQueryPool pool = query_pool(heap, type, &stream);
    if (!pool || index >= heap->desc.Count)
        return;
    if (type == D3D12_QUERY_TYPE_TIMESTAMP)
        vk->vkCmdWriteTimestamp2(list->cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool, index);
    else if (type >= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 && type <= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3)
        vk->vkCmdEndQueryIndexedEXT(list->cmd, pool, index, stream);
    else
        vk->vkCmdEndQuery(list->cmd, pool, index);
}

/* copies results as direct3d lays them out, waiting for them, then resets the queries for their next use. */
void D12ResolveQueries(D12CommandList *list, D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start, UINT count,
                       D12Resource *dst, UINT64 offset)
{
    D12Device *device = list->child.device;
    D12Vk *vk = &device->vk;
    UINT stream;
    VkQueryPool pool = query_pool(heap, type, &stream);
    if (!pool || !dst || !dst->buffer || start + count > heap->desc.Count)
        return;
    UINT stride = 8;
    if (type == D3D12_QUERY_TYPE_PIPELINE_STATISTICS)
        stride = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
    else if (type >= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 && type <= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3)
        stride = sizeof(D3D12_QUERY_DATA_SO_STATISTICS);
    D12Barrier(device, list->cmd);
    vk->vkCmdCopyQueryPoolResults(list->cmd, pool, start, count, dst->buffer, dst->offset + offset, stride,
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    vk->vkCmdResetQueryPool(list->cmd, pool, start, count);
    /* binary occlusion asks for 0 or 1: any nonzero count is clamped by a min with 1 through a fill, not done; the count stays. */
}

/* ------------------------------------------------------------------ command signatures */

static D12CommandSignature *signature_impl(ID3D12CommandSignature *iface)
{
    return CONTAINING_RECORD(iface, D12CommandSignature, iface);
}

D12CommandSignature *D12CommandSignatureFrom(ID3D12CommandSignature *iface)
{
    return iface ? signature_impl(iface) : NULL;
}

static HRESULT STDMETHODCALLTYPE signature_query(ID3D12CommandSignature *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceChild) || IsEqualGUID(riid, &IID_ID3D12Pageable) ||
        IsEqualGUID(riid, &IID_ID3D12CommandSignature)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE signature_add_ref(ID3D12CommandSignature *iface)
{
    return InterlockedIncrement(&signature_impl(iface)->child.refs);
}

static ULONG STDMETHODCALLTYPE signature_release(ID3D12CommandSignature *iface)
{
    D12CommandSignature *signature = signature_impl(iface);
    ULONG refs = InterlockedDecrement(&signature->child.refs);
    if (!refs) {
        if (signature->root)
            signature->root->iface.lpVtbl->Release(&signature->root->iface);
        HeapFree(GetProcessHeap(), 0, signature->args);
        D12ChildRelease(&signature->child);
        HeapFree(GetProcessHeap(), 0, signature);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE signature_get_private_data(ID3D12CommandSignature *iface, REFGUID guid, UINT *size,
                                                            void *data)
{
    return MimePrivateGet(&signature_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE signature_set_private_data(ID3D12CommandSignature *iface, REFGUID guid, UINT size,
                                                            const void *data)
{
    return MimePrivateSet(&signature_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE signature_set_private_data_interface(ID3D12CommandSignature *iface, REFGUID guid,
                                                                      const IUnknown *data)
{
    return MimePrivateSetInterface(&signature_impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE signature_set_name(ID3D12CommandSignature *iface, LPCWSTR name) { return S_OK; }

static HRESULT STDMETHODCALLTYPE signature_get_device(ID3D12CommandSignature *iface, REFIID riid, void **device)
{
    return D12ChildGetDevice(&signature_impl(iface)->child, riid, device);
}

static const struct ID3D12CommandSignatureVtbl g_signature_vtbl = {
    signature_query,
    signature_add_ref,
    signature_release,
    signature_get_private_data,
    signature_set_private_data,
    signature_set_private_data_interface,
    signature_set_name,
    signature_get_device,
};

HRESULT D12CreateCommandSignature(D12Device *device, const D3D12_COMMAND_SIGNATURE_DESC *desc,
                                  D12RootSignature *root, D12CommandSignature **out)
{
    if (!desc->NumArgumentDescs || !desc->pArgumentDescs)
        return E_INVALIDARG;
    /* the last argument is what each command does; the ones before it change state, which needs the root signature. */
    D3D12_INDIRECT_ARGUMENT_TYPE last = desc->pArgumentDescs[desc->NumArgumentDescs - 1].Type;
    if (last != D3D12_INDIRECT_ARGUMENT_TYPE_DRAW && last != D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED &&
        last != D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH && last != D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH &&
        last != D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS)
        return E_INVALIDARG;
    BOOL state = FALSE;
    for (UINT i = 0; i + 1 < desc->NumArgumentDescs; i++) {
        D3D12_INDIRECT_ARGUMENT_TYPE t = desc->pArgumentDescs[i].Type;
        if (t == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT || t == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW ||
            t == D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW ||
            t == D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW ||
            t == D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT)
            state = TRUE;
    }
    if (state && !root)
        return E_INVALIDARG;
    D12CommandSignature *signature = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*signature));
    if (!signature)
        return E_OUTOFMEMORY;
    signature->args = HeapAlloc(GetProcessHeap(), 0, desc->NumArgumentDescs * sizeof(*signature->args));
    if (!signature->args) {
        HeapFree(GetProcessHeap(), 0, signature);
        return E_OUTOFMEMORY;
    }
    memcpy(signature->args, desc->pArgumentDescs, desc->NumArgumentDescs * sizeof(*signature->args));
    signature->iface.lpVtbl = &g_signature_vtbl;
    signature->stride = desc->ByteStride;
    signature->arg_count = desc->NumArgumentDescs;
    signature->root = root;
    if (root)
        root->iface.lpVtbl->AddRef(&root->iface);
    signature->child.refs = 1;
    D12ChildInit(&signature->child, device);
    *out = signature;
    return S_OK;
}
