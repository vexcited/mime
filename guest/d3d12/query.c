/*
 * query heaps and command signatures. a query heap is a Vulkan query pool, with a pool per stream for stream output
 * statistics. queries are reset at heap creation and after each resolve, outside render passes, so a query is
 * ready again once its result was copied out.
 */
#include "d3d12_private.h"

/* ------------------------------------------------------------------ query heaps */

static D12QueryHeap *heap_impl(ID3D12QueryHeap *iface) { return CONTAINING_RECORD(iface, D12QueryHeap, iface); }

D12QueryHeap *D12QueryHeapFrom(ID3D12QueryHeap *iface) { return iface ? heap_impl(iface) : nullptr; }

static HRESULT STDMETHODCALLTYPE heap_query(ID3D12QueryHeap *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceChild) || IsEqualGUID(riid, &IID_ID3D12Pageable) ||
        IsEqualGUID(riid, &IID_ID3D12QueryHeap)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE heap_add_ref(ID3D12QueryHeap *iface) { return InterlockedIncrement(&heap_impl(iface)->child.refs); }

static void heap_destroy(D12QueryHeap *heap)
{
    D12Device *device = heap->child.device;
    if (heap->pool)
        device->vk.vkDestroyQueryPool(device->vk.device, heap->pool, nullptr);
    for (UINT i = 0; i < 4; i++)
        if (heap->so_pools[i])
            device->vk.vkDestroyQueryPool(device->vk.device, heap->so_pools[i], nullptr);
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

/* the statistics Direct3D 12 counts, in the order Vulkan writes them, which matches Direct3D's. */
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
    if (device->vk.vkCreateQueryPool(device->vk.device, &info, nullptr, &pool) != VK_SUCCESS)
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

/* copies results in Direct3D's layout, waiting for them, then resets the queries for reuse. */
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
    /* binary occlusion should give 0 or 1, but nonzero counts are not clamped to 1. */
}

/* ------------------------------------------------------------------ command signatures */

static D12CommandSignature *signature_impl(ID3D12CommandSignature *iface)
{
    return CONTAINING_RECORD(iface, D12CommandSignature, iface);
}

D12CommandSignature *D12CommandSignatureFrom(ID3D12CommandSignature *iface)
{
    return iface ? signature_impl(iface) : nullptr;
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
    *out = nullptr;
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
        D12Device *device = signature->child.device;
        if (signature->layout)
            device->vk.vkDestroyIndirectCommandsLayoutEXT(device->vk.device, signature->layout, nullptr);
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

/* the size of an argument in the argument buffer. */
static UINT argument_size(const D3D12_INDIRECT_ARGUMENT_DESC *a)
{
    switch (a->Type) {
    case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW: return sizeof(D3D12_DRAW_ARGUMENTS);
    case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED: return sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
    case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH: return sizeof(D3D12_DISPATCH_ARGUMENTS);
    case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW: return sizeof(D3D12_VERTEX_BUFFER_VIEW);
    case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW: return sizeof(D3D12_INDEX_BUFFER_VIEW);
    case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT: return a->Constant.Num32BitValuesToSet * 4;
    case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
    case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
    case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW: return sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
    case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS: return sizeof(D3D12_DISPATCH_RAYS_DESC);
    default: return 0;
    }
}

/*
 * generated commands doing what a signature does: root arguments are push constants at their parameter's offset,
 * vertex and index buffer views use Vulkan's layout with dxgi index formats, and the draw or dispatch comes last.
 * NULL when the device has no generated commands or an argument cannot be expressed.
 */
static VkIndirectCommandsLayoutEXT command_layout(D12Device *device, const D3D12_COMMAND_SIGNATURE_DESC *desc,
                                                 D12RootSignature *root, VkShaderStageFlags *stages)
{
    D3D12_INDIRECT_ARGUMENT_TYPE last = desc->pArgumentDescs[desc->NumArgumentDescs - 1].Type;
    if (!device->has_dgc || desc->NumArgumentDescs > 16 ||
        (last == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS && !device->has_rt_pipeline))
        return VK_NULL_HANDLE;
    VkIndirectCommandsLayoutTokenEXT tokens[16];
    VkIndirectCommandsPushConstantTokenEXT pushes[16];
    VkIndirectCommandsVertexBufferTokenEXT vertex[16];
    VkIndirectCommandsIndexBufferTokenEXT index = {VK_INDIRECT_COMMANDS_INPUT_MODE_DXGI_INDEX_BUFFER_EXT};
    UINT offset = 0;
    for (UINT i = 0; i < desc->NumArgumentDescs; i++) {
        const D3D12_INDIRECT_ARGUMENT_DESC *a = &desc->pArgumentDescs[i];
        VkIndirectCommandsLayoutTokenEXT *t = &tokens[i];
        memset(t, 0, sizeof(*t));
        t->sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT;
        t->offset = offset;
        UINT param = ~0u, words = 0, dest = 0;
        switch (a->Type) {
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW: t->type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_EXT; break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED: t->type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_EXT; break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH: t->type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT; break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH: t->type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_EXT; break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS: t->type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_TRACE_RAYS2_EXT; break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
            t->type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_EXT;
            vertex[i].vertexBindingUnit = a->VertexBuffer.Slot;
            t->data.pVertexBuffer = &vertex[i];
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
            t->type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_EXT;
            t->data.pIndexBuffer = &index;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
            param = a->Constant.RootParameterIndex;
            dest = a->Constant.DestOffsetIn32BitValues;
            words = a->Constant.Num32BitValuesToSet;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
        case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
        case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
            param = a->ConstantBufferView.RootParameterIndex;
            words = 2;
            break;
        default: return VK_NULL_HANDLE;
        }
        if (param != ~0u) {
            if (!root || param >= root->param_count || !words)
                return VK_NULL_HANDLE;
            t->type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT;
            pushes[i].updateRange = (VkPushConstantRange){VK_SHADER_STAGE_ALL, root->params[param].push_offset + dest * 4,
                                                           words * 4};
            if (pushes[i].updateRange.offset + pushes[i].updateRange.size > root->push_size)
                return VK_NULL_HANDLE;
            t->data.pPushConstant = &pushes[i];
        }
        offset += argument_size(a);
    }
    VkShaderStageFlags rays = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
                              VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
                              VK_SHADER_STAGE_INTERSECTION_BIT_KHR | VK_SHADER_STAGE_CALLABLE_BIT_KHR;
    VkShaderStageFlags wanted = last == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH        ? VK_SHADER_STAGE_COMPUTE_BIT
                                : last == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS ? rays
                                : last == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH
                                    ? VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT
                                    : VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                          (device->has_tessellation ? VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                                                          VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT
                                                                    : 0) |
                                          (device->has_geometry ? VK_SHADER_STAGE_GEOMETRY_BIT : 0);
    *stages = wanted & device->dgc_stages;
    if (last == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS && *stages != rays)
        return VK_NULL_HANDLE;
    VkIndirectCommandsLayoutCreateInfoEXT info = {VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_EXT};
    info.shaderStages = *stages;
    info.indirectStride = D12_MAX(desc->ByteStride, offset);
    info.pipelineLayout = root ? root->layout : VK_NULL_HANDLE;
    info.tokenCount = desc->NumArgumentDescs;
    info.pTokens = tokens;
    VkIndirectCommandsLayoutEXT layout = VK_NULL_HANDLE;
    if (device->vk.vkCreateIndirectCommandsLayoutEXT(device->vk.device, &info, nullptr, &layout) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return layout;
}

HRESULT D12CreateCommandSignature(D12Device *device, const D3D12_COMMAND_SIGNATURE_DESC *desc,
                                  D12RootSignature *root, D12CommandSignature **out)
{
    if (!desc->NumArgumentDescs || !desc->pArgumentDescs)
        return E_INVALIDARG;
    /* the last argument is the command itself; any state changing ones before it need the root signature. */
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
    /* a lone dispatch maps to Vulkan directly except for its count, which needs generated commands. */
    signature->layout = command_layout(device, desc, root, &signature->stages);
    signature->child.refs = 1;
    D12ChildInit(&signature->child, device);
    *out = signature;
    return S_OK;
}
