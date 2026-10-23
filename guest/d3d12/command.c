/*
 * command allocators and lists. an allocator is a vulkan command pool, and a list records into one of its command
 * buffers, which mime's vulkan-1 writes to guest memory and sends at Close. rendering begins at the first draw
 * after the targets change and ends before anything that may not run inside it. root arguments are push constants,
 * pushed for the bind point that draws or dispatches. the heaps are descriptor buffers.
 */
#include "d3d12_private.h"

/* ------------------------------------------------------------------ allocators */

static D12CommandAllocator *allocator_impl(ID3D12CommandAllocator *iface)
{
    return CONTAINING_RECORD(iface, D12CommandAllocator, iface);
}

D12CommandAllocator *D12AllocatorFrom(ID3D12CommandAllocator *iface) { return iface ? allocator_impl(iface) : nullptr; }

static HRESULT STDMETHODCALLTYPE allocator_query(ID3D12CommandAllocator *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceChild) || IsEqualGUID(riid, &IID_ID3D12Pageable) ||
        IsEqualGUID(riid, &IID_ID3D12CommandAllocator)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE allocator_add_ref(ID3D12CommandAllocator *iface)
{
    return InterlockedIncrement(&allocator_impl(iface)->child.refs);
}

/* ------------------------------------------------------------------ scratch memory */

BOOL D12ScratchAlloc(D12CommandAllocator *allocator, UINT64 size, UINT64 alignment, UINT type_bits,
                     VkDeviceAddress *address)
{
    D12Device *device = allocator->child.device;
    D12Vk *vk = &device->vk;
    alignment = alignment ? alignment : 1;
    for (D12Scratch *b = allocator->scratch; b; b = b->next) {
        UINT64 at = (b->used + alignment - 1) / alignment * alignment;
        if ((type_bits & (1u << b->type)) && at + size <= b->size && !((b->address + at) % alignment)) {
            b->used = at + size;
            *address = b->address + at;
            return TRUE;
        }
    }
    D12Scratch *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b)
        return FALSE;
    b->size = D12_MAX(size + alignment, (UINT64)1 << 20);
    /* used for generated command preprocessing, indirect arguments shaders write, and clear sources. */
    VkBufferUsageFlags2CreateInfoKHR usage = {VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO_KHR};
    usage.usage = VK_BUFFER_USAGE_2_PREPROCESS_BUFFER_BIT_EXT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR |
                  VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR |
                  VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR;
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, device->has_dgc ? &usage : nullptr};
    info.usage = device->has_dgc ? 0
                                 : VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.size = b->size;
    VkMemoryRequirements req;
    int type = -1;
    if (vk->vkCreateBuffer(vk->device, &info, nullptr, &b->buffer) == VK_SUCCESS) {
        vk->vkGetBufferMemoryRequirements(vk->device, b->buffer, &req);
        for (UINT i = 0; i < device->memory.memoryTypeCount; i++) {
            BOOL local = (device->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
            if ((req.memoryTypeBits & type_bits & (1u << i)) && (type < 0 || local))
                type = (int)i;
            if (type == (int)i && local)
                break;
        }
    }
    VkMemoryAllocateFlagsInfo flags = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags};
    alloc.allocationSize = type >= 0 ? req.size : 0;
    alloc.memoryTypeIndex = (UINT)type;
    if (type < 0 || vk->vkAllocateMemory(vk->device, &alloc, nullptr, &b->memory) != VK_SUCCESS) {
        if (b->buffer)
            vk->vkDestroyBuffer(vk->device, b->buffer, nullptr);
        HeapFree(GetProcessHeap(), 0, b);
        return FALSE;
    }
    vk->vkBindBufferMemory(vk->device, b->buffer, b->memory, 0);
    VkBufferDeviceAddressInfo where = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    where.buffer = b->buffer;
    b->address = vk->vkGetBufferDeviceAddress(vk->device, &where);
    b->type = (UINT)type;
    b->next = allocator->scratch;
    allocator->scratch = b;
    UINT64 at = (alignment - b->address % alignment) % alignment;
    b->used = at + size;
    *address = b->address + at;
    return TRUE;
}

BOOL D12QueryAlloc(D12CommandAllocator *allocator, VkQueryType type, UINT count, VkQueryPool *pool, UINT *first)
{
    D12Vk *vk = &allocator->child.device->vk;
    for (D12QueryBlock *q = allocator->queries; q; q = q->next)
        if (q->type == type && q->used + count <= 256) {
            *pool = q->pool;
            *first = q->used;
            q->used += count;
            return TRUE;
        }
    if (count > 256)
        return FALSE;
    D12QueryBlock *q = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*q));
    if (!q)
        return FALSE;
    VkQueryPoolCreateInfo info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = type;
    info.queryCount = 256;
    if (vk->vkCreateQueryPool(vk->device, &info, nullptr, &q->pool) != VK_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, q);
        return FALSE;
    }
    q->type = type;
    q->used = count;
    q->next = allocator->queries;
    allocator->queries = q;
    *pool = q->pool;
    *first = 0;
    return TRUE;
}

static void scratch_free(D12CommandAllocator *allocator)
{
    D12Vk *vk = &allocator->child.device->vk;
    while (allocator->scratch) {
        D12Scratch *b = allocator->scratch;
        allocator->scratch = b->next;
        vk->vkDestroyBuffer(vk->device, b->buffer, nullptr);
        vk->vkFreeMemory(vk->device, b->memory, nullptr);
        HeapFree(GetProcessHeap(), 0, b);
    }
    while (allocator->queries) {
        D12QueryBlock *q = allocator->queries;
        allocator->queries = q->next;
        vk->vkDestroyQueryPool(vk->device, q->pool, nullptr);
        HeapFree(GetProcessHeap(), 0, q);
    }
}

static ULONG STDMETHODCALLTYPE allocator_release(ID3D12CommandAllocator *iface)
{
    D12CommandAllocator *allocator = allocator_impl(iface);
    ULONG refs = InterlockedDecrement(&allocator->child.refs);
    if (!refs) {
        D12Device *device = allocator->child.device;
        scratch_free(allocator);
        device->vk.vkDestroyCommandPool(device->vk.device, allocator->pool, nullptr);
        HeapFree(GetProcessHeap(), 0, allocator->buffers);
        D12ChildRelease(&allocator->child);
        HeapFree(GetProcessHeap(), 0, allocator);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE allocator_get_private_data(ID3D12CommandAllocator *iface, REFGUID guid, UINT *size,
                                                            void *data)
{
    return MimePrivateGet(&allocator_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE allocator_set_private_data(ID3D12CommandAllocator *iface, REFGUID guid, UINT size,
                                                            const void *data)
{
    return MimePrivateSet(&allocator_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE allocator_set_private_data_interface(ID3D12CommandAllocator *iface, REFGUID guid,
                                                                      const IUnknown *data)
{
    return MimePrivateSetInterface(&allocator_impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE allocator_set_name(ID3D12CommandAllocator *iface, LPCWSTR name) { return S_OK; }

static HRESULT STDMETHODCALLTYPE allocator_get_device(ID3D12CommandAllocator *iface, REFIID riid, void **device)
{
    return D12ChildGetDevice(&allocator_impl(iface)->child, riid, device);
}

/* the program must have waited for the gpu to finish every list the allocator recorded. */
static HRESULT STDMETHODCALLTYPE allocator_reset(ID3D12CommandAllocator *iface)
{
    D12CommandAllocator *allocator = allocator_impl(iface);
    D12Device *device = allocator->child.device;
    if (allocator->recording)
        return E_FAIL;
    device->vk.vkResetCommandPool(device->vk.device, allocator->pool, 0);
    allocator->used = 0;
    for (D12Scratch *b = allocator->scratch; b; b = b->next)
        b->used = 0;
    for (D12QueryBlock *q = allocator->queries; q; q = q->next)
        q->used = 0;
    return S_OK;
}

static const struct ID3D12CommandAllocatorVtbl g_allocator_vtbl = {
    allocator_query,
    allocator_add_ref,
    allocator_release,
    allocator_get_private_data,
    allocator_set_private_data,
    allocator_set_private_data_interface,
    allocator_set_name,
    allocator_get_device,
    allocator_reset,
};

HRESULT D12CreateCommandAllocator(D12Device *device, D3D12_COMMAND_LIST_TYPE type, D12CommandAllocator **out)
{
    if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_BUNDLE &&
        type != D3D12_COMMAND_LIST_TYPE_COMPUTE && type != D3D12_COMMAND_LIST_TYPE_COPY)
        return E_INVALIDARG;
    D12CommandAllocator *allocator = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*allocator));
    if (!allocator)
        return E_OUTOFMEMORY;
    allocator->iface.lpVtbl = &g_allocator_vtbl;
    allocator->type = type;
    VkCommandPoolCreateInfo info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.queueFamilyIndex = device->family;
    if (device->vk.vkCreateCommandPool(device->vk.device, &info, nullptr, &allocator->pool) != VK_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, allocator);
        return E_OUTOFMEMORY;
    }
    allocator->child.refs = 1;
    D12ChildInit(&allocator->child, device);
    *out = allocator;
    return S_OK;
}

/* returns a command buffer not yet recorded since the last reset. */
static VkCommandBuffer allocator_take(D12CommandAllocator *allocator)
{
    D12Device *device = allocator->child.device;
    if (allocator->used == allocator->count) {
        if (allocator->count == allocator->capacity) {
            UINT capacity = allocator->capacity ? allocator->capacity * 2 : 4;
            VkCommandBuffer *list = allocator->buffers
                                        ? HeapReAlloc(GetProcessHeap(), 0, allocator->buffers, capacity * sizeof(*list))
                                        : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
            if (!list)
                return VK_NULL_HANDLE;
            allocator->buffers = list;
            allocator->capacity = capacity;
        }
        VkCommandBufferAllocateInfo info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        info.commandPool = allocator->pool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = 1;
        if (device->vk.vkAllocateCommandBuffers(device->vk.device, &info, &allocator->buffers[allocator->count]) !=
            VK_SUCCESS)
            return VK_NULL_HANDLE;
        allocator->count++;
    }
    return allocator->buffers[allocator->used++];
}

/* ------------------------------------------------------------------ the list */

static D12CommandList *impl(ID3D12GraphicsCommandList10 *iface) { return CONTAINING_RECORD(iface, D12CommandList, iface); }

D12CommandList *D12CommandListFrom(ID3D12CommandList *iface)
{
    return iface ? CONTAINING_RECORD(iface, D12CommandList, iface) : nullptr;
}

/* NULL while the list is open or failed. */
VkCommandBuffer D12CommandListBuffer(ID3D12CommandList *iface)
{
    D12CommandList *list = D12CommandListFrom(iface);
    return list->open || FAILED(list->error) ? VK_NULL_HANDLE : list->cmd;
}

static D12Vk *vk_of(D12CommandList *list) { return &list->child.device->vk; }

static void end_stream_output(D12CommandList *list);

static void end_rendering(D12CommandList *list)
{
    if (!list->rendering)
        return;
    if (list->so_active)
        end_stream_output(list);
    vk_of(list)->vkCmdEndRendering(list->cmd);
    list->rendering = FALSE;
}

/* a full barrier outside rendering: everything before is written and visible to everything after. */
static void barrier(D12CommandList *list)
{
    end_rendering(list);
    D12Barrier(list->child.device, list->cmd);
    list->written_count = 0;
}

/*
 * direct3d orders draws and clears to a target, vulkan only within one rendering. so before attachments or a clear
 * write an image an earlier rendering or clear wrote, a barrier goes between them, outside rendering.
 */
static void order_targets(D12CommandList *list, const VkImage *images, UINT count)
{
    BOOL again = FALSE;
    for (UINT i = 0; i < count && !again; i++)
        for (UINT k = 0; k < list->written_count && !again; k++)
            again = images[i] && images[i] == list->written[k];
    if (again || list->written_count + count > RTL_NUMBER_OF(list->written)) {
        const VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                                             VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                             VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                                             VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        VkMemoryBarrier2 b = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        b.srcStageMask = b.dstStageMask = stages;
        b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.dstAccessMask = b.srcAccessMask | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &b;
        vk_of(list)->vkCmdPipelineBarrier2(list->cmd, &dep);
        list->written_count = 0;
    }
    for (UINT i = 0; i < count; i++)
        if (images[i])
            list->written[list->written_count++] = images[i];
}

static void reset_state(D12CommandList *list)
{
    list->pipeline = nullptr;
    memset(&list->graphics, 0, sizeof(list->graphics));
    memset(&list->compute, 0, sizeof(list->compute));
    list->pushed_compute = -1;
    list->heaps[0] = list->heaps[1] = nullptr;
    list->heaps_dirty = TRUE;
    list->topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    memset(list->vb, 0, sizeof(list->vb));
    list->vb_dirty = ~0u;
    memset(&list->ib, 0, sizeof(list->ib));
    list->ib_dirty = FALSE;
    list->strip_cut = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    list->viewport_count = list->scissor_count = 0;
    list->viewports_dirty = TRUE;
    for (UINT i = 0; i < 4; i++)
        list->blend_factor[i] = 0;
    list->stencil_ref[0] = list->stencil_ref[1] = 0;
    list->depth_bounds[0] = 0;
    list->depth_bounds[1] = 1;
    list->depth_bias_set = FALSE;
    list->rtv_count = 0;
    list->has_dsv = FALSE;
    list->rendering = FALSE;
    list->targets_dirty = TRUE;
    list->state_dirty = TRUE;
    list->shading_rate = D3D12_SHADING_RATE_1X1;
    list->rate_combiners[0] = list->rate_combiners[1] = D3D12_SHADING_RATE_COMBINER_PASSTHROUGH;
    list->rate_image = nullptr;
    list->rate_dirty = TRUE;
    list->bound_graphics = nullptr;
    list->bound_cs = VK_NULL_HANDLE;
    list->rt_state = nullptr;
    list->bound_rt = VK_NULL_HANDLE;
    memset(list->so, 0, sizeof(list->so));
    list->so_active = FALSE;
    list->predicated = FALSE;
}

static void set_pipeline(D12CommandList *list, D12PipelineState *state)
{
    list->pipeline = state;
    if (!state)
        return;
    if (!state->compute) {
        list->state_dirty = TRUE;
        list->vertex_input_dirty = TRUE;
    }
}

/* ------------------------------------------------------------------ root arguments and heaps */

static D12RootState *root_state(D12CommandList *list, BOOL compute) { return compute ? &list->compute : &list->graphics; }

static void set_root_signature(D12CommandList *list, BOOL compute, ID3D12RootSignature *iface)
{
    D12RootState *s = root_state(list, compute);
    D12RootSignature *root = D12RootSignatureFrom(iface);
    if (s->root == root)
        return;
    s->root = root;
    memset(s->push, 0, sizeof(s->push));
    s->push_dirty = TRUE;
    s->sets_dirty = TRUE;
    if (list->pushed_compute == compute)
        list->pushed_compute = -1;
}

static void set_root_words(D12CommandList *list, BOOL compute, UINT index, UINT offset, UINT count, const void *data)
{
    D12RootState *s = root_state(list, compute);
    if (!s->root || index >= s->root->param_count)
        return;
    UINT at = s->root->params[index].push_offset / 4 + offset;
    if (at + count > 64)
        return;
    memcpy(&s->push[at], data, count * 4);
    if (s->push_hi <= s->push_lo) {
        s->push_lo = at;
        s->push_hi = at + count;
    } else {
        s->push_lo = D12_MIN(s->push_lo, at);
        s->push_hi = D12_MAX(s->push_hi, at + count);
    }
    s->push_dirty = TRUE;
}

static void set_root_table(D12CommandList *list, BOOL compute, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE base)
{
    UINT word = (UINT)((base.ptr & (((UINT64)1 << D12_HANDLE_SHIFT) - 1)) >> D12_INCREMENT_SHIFT);
    set_root_words(list, compute, index, 0, 1, &word);
}

static void set_root_address(D12CommandList *list, BOOL compute, UINT index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    set_root_words(list, compute, index, 0, 2, &address);
}

static void set_offsets(D12CommandList *list, VkPipelineBindPoint point, D12RootSignature *root);

/* binds the heaps' descriptor buffers, then points the bind point's sets at them per its root signature. */
static void bind_sets(D12CommandList *list, BOOL compute)
{
    D12Device *device = list->child.device;
    D12Vk *vk = &device->vk;
    D12RootState *s = root_state(list, compute);
    if (list->heaps_dirty) {
        D12DescriptorHeap *resources = list->heaps[0] ? list->heaps[0] : device->dummy_heaps[0];
        D12DescriptorHeap *samplers = list->heaps[1] ? list->heaps[1] : device->dummy_heaps[1];
        VkDescriptorBufferBindingInfoEXT infos[2] = {{VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT},
                                                     {VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT}};
        infos[0].address = resources->address;
        infos[0].usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        infos[1].address = samplers->address;
        infos[1].usage = VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        vk->vkCmdBindDescriptorBuffersEXT(list->cmd, 2, infos);
        list->heaps_dirty = FALSE;
        list->graphics.sets_dirty = list->compute.sets_dirty = TRUE;
    }
    if (!s->sets_dirty || !s->root)
        return;
    set_offsets(list, compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, s->root);
    s->sets_dirty = FALSE;
}

static void set_offsets(D12CommandList *list, VkPipelineBindPoint point, D12RootSignature *root)
{
    D12Device *device = list->child.device;
    D12Vk *vk = &device->vk;
    UINT32 indices[2] = {0, 1};
    VkDeviceSize offsets[2] = {device->record_offset, 0};
    vk->vkCmdSetDescriptorBufferOffsetsEXT(list->cmd, point, root->layout, 0, 2, indices, offsets);
    if (root->static_layout)
        vk->vkCmdBindDescriptorBufferEmbeddedSamplersEXT(list->cmd, point, root->layout, 2);
    UINT32 record_index = 0;
    VkDeviceSize record_offset = 0;
    vk->vkCmdSetDescriptorBufferOffsetsEXT(list->cmd, point, root->layout, 3, 1, &record_index, &record_offset);
    if (device->atomic_layout) {
        D12DescriptorHeap *resources = list->heaps[0] ? list->heaps[0] : device->dummy_heaps[0];
        VkDeviceSize atomic_offset = resources->atomic_offset;
        vk->vkCmdSetDescriptorBufferOffsetsEXT(list->cmd, point, root->layout, 4, 1, &record_index, &atomic_offset);
    }
}

/* push constants hold the root arguments of one bind point at a time: the one about to run. */
static void push_root(D12CommandList *list, BOOL compute)
{
    D12RootState *s = root_state(list, compute);
    if (!s->root || !s->root->push_size || (list->pushed_compute == compute && !s->push_dirty))
        return;
    /* push everything if the other bind point pushed last, otherwise only the changed words. */
    UINT lo = 0, hi = s->root->push_size;
    if (list->pushed_compute == compute && s->push_hi > s->push_lo) {
        lo = s->push_lo * 4;
        hi = D12_MIN(s->push_hi * 4, s->root->push_size);
    }
    if (hi > lo)
        vk_of(list)->vkCmdPushConstants(list->cmd, s->root->layout, VK_SHADER_STAGE_ALL, lo, hi - lo,
                                        (const BYTE *)s->push + lo);
    list->pushed_compute = compute;
    s->push_dirty = FALSE;
    s->push_lo = s->push_hi = 0;
}

/* ------------------------------------------------------------------ fixed function state */

static VkPrimitiveTopology topology(D3D_PRIMITIVE_TOPOLOGY t)
{
    switch (t) {
    case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
    case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
    case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
    case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
    default:
        if (t >= D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST && t <= D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST)
            return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

static VkExtent2D render_area(D12CommandList *list, UINT *layers)
{
    VkExtent2D area = {~0u, ~0u};
    UINT l = ~0u;
    for (UINT i = 0; i < list->rtv_count; i++)
        if (list->rtv[i].view) {
            area.width = D12_MIN(area.width, list->rtv[i].extent.width);
            area.height = D12_MIN(area.height, list->rtv[i].extent.height);
            l = D12_MIN(l, list->rtv[i].range.layerCount);
        }
    if (list->has_dsv) {
        area.width = D12_MIN(area.width, list->dsv.extent.width);
        area.height = D12_MIN(area.height, list->dsv.extent.height);
        l = D12_MIN(l, list->dsv.range.layerCount);
    }
    if (area.width == ~0u) {
        area.width = list->viewport_count ? (UINT)D12_MAX(list->viewports[0].x + list->viewports[0].width, 1.0f) : 1;
        area.height = list->viewport_count ? (UINT)D12_MAX(list->viewports[0].y, 1.0f) : 1;
        l = 1;
    }
    *layers = l;
    return area;
}

static void emit_viewports(D12CommandList *list)
{
    D12Vk *vk = vk_of(list);
    UINT count = D12_MAX(list->viewport_count, 1u);
    VkViewport viewports[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    VkRect2D scissors[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    for (UINT i = 0; i < count; i++) {
        viewports[i] = i < list->viewport_count ? list->viewports[i] : (VkViewport){0, 1, 1, -1, 0, 1};
        /* direct3d 12 has no scissor enable: unset rectangles clip everything. */
        scissors[i] = i < list->scissor_count ? list->scissors[i] : (VkRect2D){{0, 0}, {0, 0}};
    }
    vk->vkCmdSetViewportWithCount(list->cmd, count, viewports);
    vk->vkCmdSetScissorWithCount(list->cmd, count, scissors);
}

/* the dynamic parts of the graphics state: depth bias, and depth and stencil tests as the targets allow. */
static void emit_state(D12CommandList *list)
{
    D12Device *device = list->child.device;
    D12Vk *vk = &device->vk;
    D12PipelineState *p = list->pipeline;
    VkCommandBuffer cmd = list->cmd;
    const D3D12_RASTERIZER_DESC2 *rs = &p->rasterizer;
    FLOAT bias = list->depth_bias_set ? list->depth_bias[0] : rs->DepthBias;
    FLOAT clamp = list->depth_bias_set ? list->depth_bias[1] : rs->DepthBiasClamp;
    FLOAT slope = list->depth_bias_set ? list->depth_bias[2] : rs->SlopeScaledDepthBias;
    vk->vkCmdSetDepthBiasEnable(cmd, bias != 0 || slope != 0);
    vk->vkCmdSetDepthBias(cmd, bias, clamp, slope);
    const D3D12_DEPTH_STENCIL_DESC2 *ds = &p->depth_stencil;
    BOOL has_depth = list->has_dsv && (list->dsv.range.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) && p->depth_format;
    BOOL has_stencil = list->has_dsv && (list->dsv.range.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) && p->stencil_format;
    vk->vkCmdSetDepthTestEnable(cmd, ds->DepthEnable && has_depth);
    vk->vkCmdSetDepthWriteEnable(cmd, ds->DepthEnable && ds->DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL &&
                                          has_depth && !list->dsv.read_only_depth);
    vk->vkCmdSetStencilTestEnable(cmd, ds->StencilEnable && has_stencil);
    vk->vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_BIT,
                                 list->dsv.read_only_stencil ? 0 : ds->FrontFace.StencilWriteMask);
    vk->vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_BACK_BIT,
                                 list->dsv.read_only_stencil ? 0 : ds->BackFace.StencilWriteMask);
    vk->vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, list->stencil_ref[0] & 0xFF);
    vk->vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_BACK_BIT, list->stencil_ref[1] & 0xFF);
    vk->vkCmdSetBlendConstants(cmd, list->blend_factor);
    if (p->depth_bounds)
        vk->vkCmdSetDepthBounds(cmd, list->depth_bounds[0], list->depth_bounds[1]);
}

static void emit_topology(D12CommandList *list)
{
    D12Vk *vk = vk_of(list);
    VkPrimitiveTopology t = topology(list->topology);
    vk->vkCmdSetPrimitiveTopology(list->cmd, t);
    BOOL strip = t == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP || t == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
                 t == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY ||
                 t == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE cut = list->strip_cut ? list->strip_cut : list->pipeline->strip_cut;
    vk->vkCmdSetPrimitiveRestartEnable(list->cmd, strip && cut != D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED);
    if (list->child.device->has_patch_points && t == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST)
        vk->vkCmdSetPatchControlPointsEXT(list->cmd, list->topology - D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1);
}

static void emit_buffers(D12CommandList *list)
{
    D12Device *device = list->child.device;
    D12Vk *vk = &device->vk;
    for (UINT i = 0; list->vb_dirty && i < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; i++) {
        if (!(list->vb_dirty & (1u << i)))
            continue;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0, size = 0;
        UINT64 available;
        if (list->vb[i].BufferLocation &&
            D12AddressLookup(device, list->vb[i].BufferLocation, &buffer, &offset, &available))
            size = D12_MIN((UINT64)list->vb[i].SizeInBytes, available);
        if (!size)
            buffer = VK_NULL_HANDLE, offset = 0, size = VK_WHOLE_SIZE;
        VkDeviceSize stride = list->vb[i].StrideInBytes;
        vk->vkCmdBindVertexBuffers2(list->cmd, i, 1, &buffer, &offset, buffer ? &size : nullptr, &stride);
    }
    list->vb_dirty = 0;
    if (list->ib_dirty && list->ib.BufferLocation) {
        VkBuffer buffer;
        VkDeviceSize offset;
        if (D12AddressLookup(device, list->ib.BufferLocation, &buffer, &offset, nullptr))
            vk->vkCmdBindIndexBuffer(list->cmd, buffer, offset,
                                     list->ib.Format == DXGI_FORMAT_R16_UINT ? VK_INDEX_TYPE_UINT16
                                                                             : VK_INDEX_TYPE_UINT32);
    }
    list->ib_dirty = FALSE;
}

/* ------------------------------------------------------------------ rendering */

/* uses the attachment count and formats of the graphics state. */
static void begin_rendering(D12CommandList *list)
{
    D12Device *device = list->child.device;
    D12PipelineState *p = list->pipeline;
    VkRenderingAttachmentInfo colors[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    VkRenderingAttachmentInfo depth = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VkRenderingAttachmentInfo stencil = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    UINT layers;
    VkExtent2D area = render_area(list, &layers);
    for (UINT i = 0; i < p->rt_count; i++) {
        colors[i] = (VkRenderingAttachmentInfo){VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        const D12TargetView *v = i < list->rtv_count ? &list->rtv[i] : nullptr;
        if (!v || !v->view)
            continue;
        colors[i].imageView = v->view->view;
        colors[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    VkRenderingInfo info = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent = area;
    info.layerCount = layers;
    info.colorAttachmentCount = p->rt_count;
    info.pColorAttachments = colors;
    if (list->has_dsv) {
        depth.imageView = list->dsv.view->view;
        depth.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        stencil = depth;
    }
    if (p->depth_format && list->has_dsv && (list->dsv.range.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT))
        info.pDepthAttachment = &depth;
    if (p->stencil_format && list->has_dsv && (list->dsv.range.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT))
        info.pStencilAttachment = &stencil;
    /* the shading rate image, if it covers the render area. */
    VkRenderingFragmentShadingRateAttachmentInfoKHR rates = {
        VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR};
    D12Resource *r = list->rate_image;
    if (r && device->vrs_tile && (UINT64)r->desc.Width * device->vrs_tile >= area.width &&
        (UINT64)r->desc.Height * device->vrs_tile >= area.height) {
        VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R8_UINT;
        view.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        D12ImageView *v = D12ResourceView(r, &view, VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR);
        if (v) {
            rates.imageView = v->view;
            rates.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            rates.shadingRateAttachmentTexelSize = (VkExtent2D){device->vrs_tile, device->vrs_tile};
            info.pNext = &rates;
        }
    }
    VkImage images[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1];
    UINT n = 0;
    for (UINT i = 0; i < p->rt_count; i++)
        if (colors[i].imageView)
            images[n++] = list->rtv[i].resource->image;
    if (info.pDepthAttachment || info.pStencilAttachment)
        images[n++] = list->dsv.resource->image;
    order_targets(list, images, n);
    device->vk.vkCmdBeginRendering(list->cmd, &info);
    list->rendering = TRUE;
    list->targets_dirty = FALSE;
    list->render_rt_count = p->rt_count;
    list->render_depth_format = p->depth_format;
    list->render_stencil_format = p->stencil_format;
    list->viewports_dirty = TRUE;
}

/* ------------------------------------------------------------------ stream output */

static void begin_stream_output(D12CommandList *list)
{
    D12Device *device = list->child.device;
    VkBuffer buffers[4], counters[4];
    VkDeviceSize offsets[4], sizes[4], counter_offsets[4];
    for (UINT i = 0; i < 4; i++) {
        const D3D12_STREAM_OUTPUT_BUFFER_VIEW *v = &list->so[i];
        UINT64 available = 0;
        if (!v->BufferLocation || !D12AddressLookup(device, v->BufferLocation, &buffers[i], &offsets[i], &available)) {
            buffers[i] = device->null_buffer.heap->buffer;
            offsets[i] = device->null_buffer.offset;
            available = 4;
        }
        sizes[i] = D12_MIN(v->SizeInBytes ? v->SizeInBytes : 4, available);
        if (!v->BufferFilledSizeLocation ||
            !D12AddressLookup(device, v->BufferFilledSizeLocation, &counters[i], &counter_offsets[i], nullptr))
            counters[i] = VK_NULL_HANDLE, counter_offsets[i] = 0;
    }
    device->vk.vkCmdBindTransformFeedbackBuffersEXT(list->cmd, 0, 4, buffers, offsets, sizes);
    device->vk.vkCmdBeginTransformFeedbackEXT(list->cmd, 0, 4, counters, counter_offsets);
    list->so_active = TRUE;
}

static void end_stream_output(D12CommandList *list)
{
    D12Device *device = list->child.device;
    VkBuffer counters[4];
    VkDeviceSize counter_offsets[4];
    for (UINT i = 0; i < 4; i++) {
        const D3D12_STREAM_OUTPUT_BUFFER_VIEW *v = &list->so[i];
        if (!v->BufferFilledSizeLocation ||
            !D12AddressLookup(device, v->BufferFilledSizeLocation, &counters[i], &counter_offsets[i], nullptr))
            counters[i] = VK_NULL_HANDLE, counter_offsets[i] = 0;
    }
    device->vk.vkCmdEndTransformFeedbackEXT(list->cmd, 0, 4, counters, counter_offsets);
    list->so_active = FALSE;
}

/* ------------------------------------------------------------------ draws and dispatches */

/* direct3d's combiners map to vulkan's, its sum of log2 rates being a product of sizes. */
static void emit_shading_rate(D12CommandList *list)
{
    static const VkFragmentShadingRateCombinerOpKHR OPS[] = {
        VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR, VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR,
        VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MIN_KHR, VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MAX_KHR,
        VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MUL_KHR};
    D12Device *device = list->child.device;
    VkExtent2D size = {1u << (list->shading_rate >> 2 & 3), 1u << (list->shading_rate & 3)};
    VkFragmentShadingRateCombinerOpKHR ops[2];
    for (UINT i = 0; i < 2; i++)
        ops[i] = device->vrs_tier >= D3D12_VARIABLE_SHADING_RATE_TIER_2 && list->rate_combiners[i] < RTL_NUMBER_OF(OPS)
                     ? OPS[list->rate_combiners[i]]
                     : VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
    device->vk.vkCmdSetFragmentShadingRateKHR(list->cmd, &size, ops);
}

static BOOL prepare_draw(D12CommandList *list)
{
    D12Device *device = list->child.device;
    D12Vk *vk = &device->vk;
    D12PipelineState *p = list->pipeline;
    if (!list->open || !p || p->compute || !list->graphics.root)
        return FALSE;
    BOOL so = p->stream_output && device->has_transform_feedback;
    /* a pipeline with other attachment formats than the current rendering needs a new rendering. */
    BOOL layout = list->rendering && (list->render_rt_count != p->rt_count || list->render_depth_format != p->depth_format ||
                                      list->render_stencil_format != p->stencil_format);
    if (list->rendering && (list->targets_dirty || layout || (list->so_active && !so)))
        end_rendering(list);
    if (!list->rendering)
        begin_rendering(list);
    if (list->bound_graphics != p) {
        vk->vkCmdBindPipeline(list->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
        list->bound_graphics = p;
        list->state_dirty = TRUE;
    }
    /* mesh pipelines have no topology or vertex buffers, those wait for the next vertex pipeline. */
    if (list->state_dirty) {
        emit_state(list);
        if (!p->mesh) {
            emit_topology(list);
            list->topology_dirty = FALSE;
        }
    } else if (list->topology_dirty && !p->mesh) {
        emit_topology(list);
        list->topology_dirty = FALSE;
    }
    if (list->state_dirty || list->viewports_dirty) {
        emit_viewports(list);
        list->viewports_dirty = FALSE;
    }
    if (device->vrs_tier && (list->state_dirty || list->rate_dirty)) {
        emit_shading_rate(list);
        list->rate_dirty = FALSE;
    }
    list->state_dirty = FALSE;
    bind_sets(list, FALSE);
    push_root(list, FALSE);
    if (!p->mesh)
        emit_buffers(list);
    if (so && !list->so_active)
        begin_stream_output(list);
    return TRUE;
}

static BOOL prepare_dispatch(D12CommandList *list)
{
    D12PipelineState *p = list->pipeline;
    if (!list->open || !p || !p->compute || !list->compute.root)
        return FALSE;
    end_rendering(list);
    if (list->bound_cs != p->pipeline) {
        vk_of(list)->vkCmdBindPipeline(list->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
        list->bound_cs = p->pipeline;
    }
    bind_sets(list, TRUE);
    push_root(list, TRUE);
    return TRUE;
}

static void STDMETHODCALLTYPE list_draw_instanced(ID3D12GraphicsCommandList10 *iface, UINT vertices, UINT instances,
                                                  UINT start_vertex, UINT start_instance)
{
    D12CommandList *list = impl(iface);
    if (!vertices || !instances || !prepare_draw(list))
        return;
    vk_of(list)->vkCmdDraw(list->cmd, vertices, instances, start_vertex, start_instance);
}

static void STDMETHODCALLTYPE list_draw_indexed_instanced(ID3D12GraphicsCommandList10 *iface, UINT indices,
                                                          UINT instances, UINT start_index, INT base_vertex,
                                                          UINT start_instance)
{
    D12CommandList *list = impl(iface);
    if (!indices || !instances || !list->ib.BufferLocation || !prepare_draw(list))
        return;
    vk_of(list)->vkCmdDrawIndexed(list->cmd, indices, instances, start_index, base_vertex, start_instance);
}

static void STDMETHODCALLTYPE list_dispatch(ID3D12GraphicsCommandList10 *iface, UINT x, UINT y, UINT z)
{
    D12CommandList *list = impl(iface);
    if (!x || !y || !z || !prepare_dispatch(list))
        return;
    vk_of(list)->vkCmdDispatch(list->cmd, x, y, z);
}

/* ------------------------------------------------------------------ IUnknown and ID3D12Object */

static HRESULT STDMETHODCALLTYPE list_query(ID3D12GraphicsCommandList10 *iface, REFIID riid, void **out)
{
    static const GUID *const ids[] = {
        &IID_IUnknown,
        &IID_ID3D12Object,
        &IID_ID3D12DeviceChild,
        &IID_ID3D12CommandList,
        &IID_ID3D12GraphicsCommandList,
        &IID_ID3D12GraphicsCommandList1,
        &IID_ID3D12GraphicsCommandList2,
        &IID_ID3D12GraphicsCommandList3,
        &IID_ID3D12GraphicsCommandList4,
        &IID_ID3D12GraphicsCommandList5,
        &IID_ID3D12GraphicsCommandList6,
        &IID_ID3D12GraphicsCommandList7,
        &IID_ID3D12GraphicsCommandList8,
        &IID_ID3D12GraphicsCommandList9,
        &IID_ID3D12GraphicsCommandList10,
    };
    for (UINT i = 0; i < RTL_NUMBER_OF(ids); i++)
        if (IsEqualGUID(riid, ids[i])) {
            iface->lpVtbl->AddRef(iface);
            *out = iface;
            return S_OK;
        }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE list_add_ref(ID3D12GraphicsCommandList10 *iface)
{
    return InterlockedIncrement(&impl(iface)->child.refs);
}

static ULONG STDMETHODCALLTYPE list_release(ID3D12GraphicsCommandList10 *iface)
{
    D12CommandList *list = impl(iface);
    ULONG refs = InterlockedDecrement(&list->child.refs);
    if (!refs) {
        if (list->open && list->allocator)
            list->allocator->recording = nullptr;
        if (list->allocator)
            list->allocator->iface.lpVtbl->Release(&list->allocator->iface);
        HeapFree(GetProcessHeap(), 0, list->calls);
        D12ChildRelease(&list->child);
        HeapFree(GetProcessHeap(), 0, list);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE list_get_private_data(ID3D12GraphicsCommandList10 *iface, REFGUID guid, UINT *size,
                                                       void *data)
{
    return MimePrivateGet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE list_set_private_data(ID3D12GraphicsCommandList10 *iface, REFGUID guid, UINT size,
                                                       const void *data)
{
    return MimePrivateSet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE list_set_private_data_interface(ID3D12GraphicsCommandList10 *iface, REFGUID guid,
                                                                 const IUnknown *data)
{
    return MimePrivateSetInterface(&impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE list_set_name(ID3D12GraphicsCommandList10 *iface, LPCWSTR name) { return S_OK; }

static HRESULT STDMETHODCALLTYPE list_get_device(ID3D12GraphicsCommandList10 *iface, REFIID riid, void **device)
{
    return D12ChildGetDevice(&impl(iface)->child, riid, device);
}

static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE list_get_type(ID3D12GraphicsCommandList10 *iface)
{
    return impl(iface)->type;
}

/* ------------------------------------------------------------------ recording */

static HRESULT STDMETHODCALLTYPE list_close(ID3D12GraphicsCommandList10 *iface)
{
    D12CommandList *list = impl(iface);
    if (!list->open)
        return E_FAIL;
    end_rendering(list);
    if (list->predicated) {
        vk_of(list)->vkCmdEndConditionalRenderingEXT(list->cmd);
        list->predicated = FALSE;
    }
    if (vk_of(list)->vkEndCommandBuffer(list->cmd) != VK_SUCCESS && SUCCEEDED(list->error))
        list->error = E_OUTOFMEMORY;
    list->open = FALSE;
    if (list->allocator)
        list->allocator->recording = nullptr;
    return list->error;
}

static HRESULT begin(D12CommandList *list, D12CommandAllocator *allocator, D12PipelineState *initial)
{
    if (list->open || !allocator || allocator->type != list->type)
        return E_INVALIDARG;
    if (allocator->recording)
        return E_INVALIDARG;
    VkCommandBuffer cmd = allocator_take(allocator);
    if (!cmd)
        return E_OUTOFMEMORY;
    VkCommandBufferBeginInfo info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vk_of(list)->vkBeginCommandBuffer(cmd, &info) != VK_SUCCESS)
        return E_OUTOFMEMORY;
    if (list->allocator != allocator) {
        allocator->iface.lpVtbl->AddRef(&allocator->iface);
        if (list->allocator)
            list->allocator->iface.lpVtbl->Release(&list->allocator->iface);
        list->allocator = allocator;
    }
    allocator->recording = list;
    list->cmd = cmd;
    list->open = TRUE;
    list->error = S_OK;
    list->render_samples = 0;
    list->written_count = 0;
    reset_state(list);
    set_pipeline(list, initial);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE list_reset(ID3D12GraphicsCommandList10 *iface, ID3D12CommandAllocator *allocator,
                                            ID3D12PipelineState *initial)
{
    return begin(impl(iface), D12AllocatorFrom(allocator), D12PipelineFrom(initial));
}

static void STDMETHODCALLTYPE list_clear_state(ID3D12GraphicsCommandList10 *iface, ID3D12PipelineState *state)
{
    D12CommandList *list = impl(iface);
    end_rendering(list);
    reset_state(list);
    set_pipeline(list, D12PipelineFrom(state));
}

/* ------------------------------------------------------------------ copies */

static void STDMETHODCALLTYPE list_copy_buffer_region(ID3D12GraphicsCommandList10 *iface, ID3D12Resource *dst,
                                                      UINT64 dst_offset, ID3D12Resource *src, UINT64 src_offset,
                                                      UINT64 bytes)
{
    D12CommandList *list = impl(iface);
    D12Resource *d = D12ResourceFrom(dst), *s = D12ResourceFrom(src);
    if (!list->open || !d || !s || !d->buffer || !s->buffer || !bytes)
        return;
    end_rendering(list);
    VkBufferCopy region = {s->offset + src_offset, d->offset + dst_offset, bytes};
    vk_of(list)->vkCmdCopyBuffer(list->cmd, s->buffer, d->buffer, 1, &region);
}

/* only the aspect of the subresource's plane. */
static VkImageSubresourceLayers subresource_layers(const D12Resource *r, UINT index)
{
    UINT mip, layer, plane;
    D12SubresourceDecompose(r, index, &mip, &layer, &plane);
    return (VkImageSubresourceLayers){D12PlaneAspect(r, plane), mip, layer, 1};
}

static VkBufferImageCopy footprint_region(const D12Resource *buffer, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *fp,
                                          const D12Resource *image, UINT sub, VkOffset3D at, const D3D12_BOX *box,
                                          BOOL box_in_buffer)
{
    const D3dFormat *f = D3dGetFormat(fp->Footprint.Format);
    UINT block = f->vk != VK_FORMAT_UNDEFINED ? f->block : 1;
    UINT block_bytes = f->vk != VK_FORMAT_UNDEFINED ? f->block_bytes : 4;
    VkBufferImageCopy region = {};
    region.bufferOffset = buffer->offset + fp->Offset;
    region.bufferRowLength = fp->Footprint.RowPitch / block_bytes * block;
    region.bufferImageHeight = fp->Footprint.Height;
    region.imageSubresource = subresource_layers(image, sub);
    region.imageOffset = at;
    VkExtent3D extent = {fp->Footprint.Width, fp->Footprint.Height, fp->Footprint.Depth};
    if (box) {
        extent = (VkExtent3D){box->right - box->left, box->bottom - box->top, box->back - box->front};
        if (box_in_buffer)
            region.bufferOffset += (UINT64)box->front * fp->Footprint.RowPitch * (fp->Footprint.Height / block) +
                                   (UINT64)(box->top / block) * fp->Footprint.RowPitch +
                                   (UINT64)(box->left / block) * block_bytes;
        else
            region.imageOffset = (VkOffset3D){(INT)box->left, (INT)box->top, (INT)box->front};
    }
    /* block compressed mips smaller than a block copy at the mip's own size. */
    VkExtent3D mip = D12MipExtent(image, region.imageSubresource.mipLevel);
    extent.width = D12_MIN(extent.width, mip.width - (UINT)region.imageOffset.x);
    extent.height = D12_MIN(extent.height, mip.height - (UINT)region.imageOffset.y);
    extent.depth = D12_MIN(extent.depth, mip.depth - (UINT)region.imageOffset.z);
    region.imageExtent = extent;
    return region;
}

static BOOL scratch_buffer(D12CommandAllocator *allocator, VkDeviceAddress address, VkBuffer *buffer,
                           VkDeviceSize *offset)
{
    for (D12Scratch *b = allocator->scratch; b; b = b->next)
        if (address >= b->address && address < b->address + b->size) {
            *buffer = b->buffer;
            *offset = address - b->address;
            return TRUE;
        }
    return FALSE;
}

static void transfer_barrier(D12CommandList *list)
{
    VkMemoryBarrier2 b = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    vk_of(list)->vkCmdPipelineBarrier2(list->cmd, &dep);
}

/* bytes per texel of one aspect in a buffer. */
static UINT aspect_bytes(const D12Resource *r, VkImageAspectFlags aspect)
{
    if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT)
        return 1;
    if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
        return r->fmt->block_bytes == 2 ? 2 : 4;
    return r->fmt->block_bytes;
}

/*
 * direct3d also copies between a depth or stencil plane and a same sized color texture. vulkan does that with
 * VK_KHR_maintenance8, otherwise it goes through the allocator's memory.
 */
static void copy_image(D12CommandList *list, D12Resource *s, D12Resource *d, const VkImageCopy *region)
{
    D12Vk *vk = vk_of(list);
    BOOL src_color = region->srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT;
    BOOL dst_color = region->dstSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT;
    if (src_color == dst_color || list->child.device->has_maintenance8) {
        vk->vkCmdCopyImage(list->cmd, s->image, VK_IMAGE_LAYOUT_GENERAL, d->image, VK_IMAGE_LAYOUT_GENERAL, 1, region);
        return;
    }
    UINT bytes = aspect_bytes(s, region->srcSubresource.aspectMask);
    if (bytes != aspect_bytes(d, region->dstSubresource.aspectMask))
        return;
    VkDeviceAddress address;
    VkBuffer buffer;
    VkDeviceSize offset;
    UINT64 size = (UINT64)region->extent.width * region->extent.height * region->extent.depth * bytes;
    if (!D12ScratchAlloc(list->allocator, size, 16, ~0u, &address) ||
        !scratch_buffer(list->allocator, address, &buffer, &offset))
        return;
    VkBufferImageCopy through = {offset, 0, 0, region->srcSubresource, region->srcOffset, region->extent};
    vk->vkCmdCopyImageToBuffer(list->cmd, s->image, VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &through);
    transfer_barrier(list);
    through.imageSubresource = region->dstSubresource;
    through.imageOffset = region->dstOffset;
    vk->vkCmdCopyBufferToImage(list->cmd, buffer, d->image, VK_IMAGE_LAYOUT_GENERAL, 1, &through);
}

static void STDMETHODCALLTYPE list_copy_texture_region(ID3D12GraphicsCommandList10 *iface,
                                                       const D3D12_TEXTURE_COPY_LOCATION *dst, UINT x, UINT y, UINT z,
                                                       const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *box)
{
    D12CommandList *list = impl(iface);
    D12Resource *d = D12ResourceFrom(dst->pResource), *s = D12ResourceFrom(src->pResource);
    if (!list->open || !d || !s)
        return;
    end_rendering(list);
    D12Vk *vk = vk_of(list);
    VkOffset3D at = {(INT)x, (INT)y, (INT)z};
    if (dst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX &&
        src->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT) {
        if (!d->image || !s->buffer)
            return;
        VkBufferImageCopy region = footprint_region(s, &src->PlacedFootprint, d, dst->SubresourceIndex, at, box, TRUE);
        if (region.imageExtent.width && region.imageExtent.height && region.imageExtent.depth)
            vk->vkCmdCopyBufferToImage(list->cmd, s->buffer, d->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    } else if (dst->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT &&
               src->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX) {
        if (!s->image || !d->buffer)
            return;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = dst->PlacedFootprint;
        VkBufferImageCopy region = footprint_region(d, &fp, s, src->SubresourceIndex, (VkOffset3D){0}, box, FALSE);
        /* the destination point offsets into the buffer, in blocks. */
        const D3dFormat *f = D3dGetFormat(fp.Footprint.Format);
        UINT block = f->vk != VK_FORMAT_UNDEFINED ? f->block : 1, block_bytes = f->vk != VK_FORMAT_UNDEFINED ? f->block_bytes : 4;
        region.bufferOffset += (UINT64)z * fp.Footprint.RowPitch * (fp.Footprint.Height / block) +
                               (UINT64)(y / block) * fp.Footprint.RowPitch + (UINT64)(x / block) * block_bytes;
        if (region.imageExtent.width && region.imageExtent.height && region.imageExtent.depth)
            vk->vkCmdCopyImageToBuffer(list->cmd, s->image, VK_IMAGE_LAYOUT_GENERAL, d->buffer, 1, &region);
    } else if (dst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX &&
               src->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX) {
        if (!s->image || !d->image)
            return;
        VkImageCopy region = {};
        region.srcSubresource = subresource_layers(s, src->SubresourceIndex);
        region.dstSubresource = subresource_layers(d, dst->SubresourceIndex);
        region.dstOffset = at;
        VkExtent3D e = D12MipExtent(s, region.srcSubresource.mipLevel);
        if (box) {
            region.srcOffset = (VkOffset3D){(INT)box->left, (INT)box->top, (INT)box->front};
            e = (VkExtent3D){box->right - box->left, box->bottom - box->top, box->back - box->front};
        }
        VkExtent3D de = D12MipExtent(d, region.dstSubresource.mipLevel);
        /* block compressed to uncompressed copies count the destination in blocks. */
        if (s->fmt->block > d->fmt->block)
            de = (VkExtent3D){de.width * s->fmt->block, de.height * s->fmt->block, de.depth};
        e.width = D12_MIN(e.width, de.width - x);
        e.height = D12_MIN(e.height, de.height - y);
        e.depth = D12_MIN(e.depth, de.depth - z);
        region.extent = e;
        if (e.width && e.height && e.depth)
            copy_image(list, s, d, &region);
    }
}

static void STDMETHODCALLTYPE list_copy_resource(ID3D12GraphicsCommandList10 *iface, ID3D12Resource *dst,
                                                 ID3D12Resource *src)
{
    D12CommandList *list = impl(iface);
    D12Resource *d = D12ResourceFrom(dst), *s = D12ResourceFrom(src);
    if (!list->open || !d || !s)
        return;
    end_rendering(list);
    D12Vk *vk = vk_of(list);
    if (d->buffer && s->buffer) {
        VkBufferCopy region = {s->offset, d->offset, D12_MIN(s->desc.Width, d->desc.Width)};
        vk->vkCmdCopyBuffer(list->cmd, s->buffer, d->buffer, 1, &region);
        return;
    }
    if (!d->image || !s->image)
        return;
    UINT count = D12_MIN(D12SubresourceCount(s), D12SubresourceCount(d));
    for (UINT i = 0; i < count; i++) {
        VkImageCopy region = {};
        region.srcSubresource = subresource_layers(s, i);
        region.dstSubresource = subresource_layers(d, i);
        region.extent = D12MipExtent(s, region.srcSubresource.mipLevel);
        copy_image(list, s, d, &region);
    }
}

/*
 * tiles to or from a buffer, 64 KiB per tile in region order. a texture tile is laid out as rows of the tile
 * shape, a buffer or mip tail tile as its bytes.
 */
static void STDMETHODCALLTYPE list_copy_tiles(ID3D12GraphicsCommandList10 *iface, ID3D12Resource *tiled,
                                              const D3D12_TILED_RESOURCE_COORDINATE *start,
                                              const D3D12_TILE_REGION_SIZE *size, ID3D12Resource *buffer,
                                              UINT64 buffer_offset, D3D12_TILE_COPY_FLAGS flags)
{
    D12CommandList *list = impl(iface);
    D12Resource *r = D12ResourceFrom(tiled), *b = D12ResourceFrom(buffer);
    if (!list->open || !r || !r->reserved || !b || !b->buffer || !size)
        return;
    UINT total = size->UseBox ? size->Width * size->Height * size->Depth : size->NumTiles;
    UINT *tiles = HeapAlloc(GetProcessHeap(), 0, (total + 1) * sizeof(UINT));
    if (!tiles)
        return;
    end_rendering(list);
    BOOL to_buffer = (flags & D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER) != 0;
    UINT n = D12RegionTiles(r, start, size, tiles, total);
    D12Vk *vk = vk_of(list);
    for (UINT i = 0; i < n; i++) {
        D12TilePlace place;
        if (!D12TileAt(r, tiles[i], &place))
            continue;
        VkDeviceSize at = b->offset + buffer_offset + (VkDeviceSize)i * 65536;
        if (!r->image) {
            VkBufferCopy region = {to_buffer ? place.opaque : at, to_buffer ? at : place.opaque, 65536};
            vk->vkCmdCopyBuffer(list->cmd, to_buffer ? r->buffer : b->buffer, to_buffer ? b->buffer : r->buffer, 1,
                                &region);
            continue;
        }
        /* direct3d defines no layout for packed mips, so their tiles do not copy. */
        if (place.packed)
            continue;
        VkBufferImageCopy region = {at, r->tile_shape.width, r->tile_shape.height,
                                    {VK_IMAGE_ASPECT_COLOR_BIT, place.mip, place.layer, 1}, place.at, place.extent};
        if (to_buffer)
            vk->vkCmdCopyImageToBuffer(list->cmd, r->image, VK_IMAGE_LAYOUT_GENERAL, b->buffer, 1, &region);
        else
            vk->vkCmdCopyBufferToImage(list->cmd, b->buffer, r->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    }
    HeapFree(GetProcessHeap(), 0, tiles);
}

static void resolve(D12CommandList *list, D12Resource *d, UINT dst_sub, UINT dst_x, UINT dst_y, D12Resource *s,
                    UINT src_sub, const D3D12_RECT *rect)
{
    if (!list->open || !d || !s || !d->image || !s->image)
        return;
    end_rendering(list);
    VkImageResolve region = {};
    region.srcSubresource = subresource_layers(s, src_sub);
    region.dstSubresource = subresource_layers(d, dst_sub);
    region.extent = D12MipExtent(s, region.srcSubresource.mipLevel);
    region.dstOffset = (VkOffset3D){(INT)dst_x, (INT)dst_y, 0};
    if (rect) {
        region.srcOffset = (VkOffset3D){rect->left, rect->top, 0};
        region.extent = (VkExtent3D){(UINT)(rect->right - rect->left), (UINT)(rect->bottom - rect->top), 1};
    }
    vk_of(list)->vkCmdResolveImage(list->cmd, s->image, VK_IMAGE_LAYOUT_GENERAL, d->image, VK_IMAGE_LAYOUT_GENERAL, 1,
                                   &region);
}

static void STDMETHODCALLTYPE list_resolve_subresource(ID3D12GraphicsCommandList10 *iface, ID3D12Resource *dst,
                                                       UINT dst_sub, ID3D12Resource *src, UINT src_sub,
                                                       DXGI_FORMAT format)
{
    resolve(impl(iface), D12ResourceFrom(dst), dst_sub, 0, 0, D12ResourceFrom(src), src_sub, nullptr);
}

/*
 * decodes a min mip feedback map to an R8_UINT texture of its regions, or encodes it back, with a blit. mip region
 * used maps are not decoded.
 */
static void code_feedback(D12CommandList *list, D12Resource *d, UINT dst_sub, D12Resource *s, UINT src_sub, BOOL decode)
{
    D12Resource *map = decode ? s : d, *bytes = decode ? d : s;
    if (!list->open || !map->image || !bytes->image)
        return;
    if (map->desc.Format != DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE) {
        D12Log("d3d12: mip region used feedback maps are not decoded");
        return;
    }
    end_rendering(list);
    VkExtent2D e = D12FeedbackExtent(&map->desc);
    VkImageSubresourceLayers map_layers = {VK_IMAGE_ASPECT_COLOR_BIT, 0, (decode ? src_sub : dst_sub) % map->layers, 1};
    VkImageSubresourceLayers byte_layers = subresource_layers(bytes, decode ? dst_sub : src_sub);
    VkExtent3D b = D12MipExtent(bytes, byte_layers.mipLevel);
    VkOffset3D size = {(INT)D12_MIN(e.width, b.width), (INT)D12_MIN(e.height, b.height), 1};
    VkImageBlit blit = {decode ? map_layers : byte_layers, {{0}, size}, decode ? byte_layers : map_layers, {{0}, size}};
    vk_of(list)->vkCmdBlitImage(list->cmd, s->image, VK_IMAGE_LAYOUT_GENERAL, d->image, VK_IMAGE_LAYOUT_GENERAL, 1,
                                &blit, VK_FILTER_NEAREST);
}

static void STDMETHODCALLTYPE list_resolve_subresource_region(ID3D12GraphicsCommandList10 *iface, ID3D12Resource *dst,
                                                              UINT dst_sub, UINT dst_x, UINT dst_y,
                                                              ID3D12Resource *src, UINT src_sub, D3D12_RECT *rect,
                                                              DXGI_FORMAT format, D3D12_RESOLVE_MODE mode)
{
    D12Resource *d = D12ResourceFrom(dst), *s = D12ResourceFrom(src);
    if ((mode == D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK || mode == D3D12_RESOLVE_MODE_ENCODE_SAMPLER_FEEDBACK) &&
        d && s) {
        code_feedback(impl(iface), d, dst_sub, s, src_sub, mode == D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK);
        return;
    }
    resolve(impl(iface), d, dst_sub, dst_x, dst_y, s, src_sub, rect);
}

/* ------------------------------------------------------------------ input assembler, rasterizer, output merger */

static void STDMETHODCALLTYPE list_ia_set_primitive_topology(ID3D12GraphicsCommandList10 *iface,
                                                             D3D_PRIMITIVE_TOPOLOGY t)
{
    D12CommandList *list = impl(iface);
    if (list->topology != t) {
        list->topology = t;
        list->topology_dirty = TRUE;
    }
}

static void STDMETHODCALLTYPE list_rs_set_viewports(ID3D12GraphicsCommandList10 *iface, UINT count,
                                                    const D3D12_VIEWPORT *viewports)
{
    D12CommandList *list = impl(iface);
    count = D12_MIN(count, (UINT)D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    for (UINT i = 0; i < count; i++) {
        D3D12_VIEWPORT v = viewports[i];
        if (v.Width <= 0)
            v.Width = 1;
        /* direct3d's y points down, so a negative height viewport flips vulkan's. */
        list->viewports[i] = (VkViewport){v.TopLeftX, v.TopLeftY + v.Height, v.Width, -v.Height, v.MinDepth, v.MaxDepth};
        if (v.Height == 0)
            list->viewports[i].height = -1, list->viewports[i].y = v.TopLeftY + 1;
    }
    list->viewport_count = count;
    list->viewports_dirty = TRUE;
}

static void STDMETHODCALLTYPE list_rs_set_scissor_rects(ID3D12GraphicsCommandList10 *iface, UINT count,
                                                        const D3D12_RECT *rects)
{
    D12CommandList *list = impl(iface);
    count = D12_MIN(count, (UINT)D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    for (UINT i = 0; i < count; i++) {
        LONG left = D12_MAX(rects[i].left, 0), top = D12_MAX(rects[i].top, 0);
        list->scissors[i] = (VkRect2D){{left, top},
                                       {(UINT)D12_MAX(rects[i].right - left, 0), (UINT)D12_MAX(rects[i].bottom - top, 0)}};
    }
    list->scissor_count = count;
    list->viewports_dirty = TRUE;
}

static void STDMETHODCALLTYPE list_om_set_blend_factor(ID3D12GraphicsCommandList10 *iface, const FLOAT factor[4])
{
    D12CommandList *list = impl(iface);
    for (UINT i = 0; i < 4; i++)
        list->blend_factor[i] = factor ? factor[i] : 1.0f;
    if (list->open)
        vk_of(list)->vkCmdSetBlendConstants(list->cmd, list->blend_factor);
}

static void STDMETHODCALLTYPE list_om_set_stencil_ref(ID3D12GraphicsCommandList10 *iface, UINT ref)
{
    D12CommandList *list = impl(iface);
    list->stencil_ref[0] = list->stencil_ref[1] = ref;
    if (list->open)
        vk_of(list)->vkCmdSetStencilReference(list->cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ref & 0xFF);
}

static void STDMETHODCALLTYPE list_om_set_front_and_back_stencil_ref(ID3D12GraphicsCommandList10 *iface, UINT front,
                                                                     UINT back)
{
    D12CommandList *list = impl(iface);
    list->stencil_ref[0] = front;
    list->stencil_ref[1] = back;
    if (list->open) {
        vk_of(list)->vkCmdSetStencilReference(list->cmd, VK_STENCIL_FACE_FRONT_BIT, front & 0xFF);
        vk_of(list)->vkCmdSetStencilReference(list->cmd, VK_STENCIL_FACE_BACK_BIT, back & 0xFF);
    }
}

static void STDMETHODCALLTYPE list_om_set_depth_bounds(ID3D12GraphicsCommandList10 *iface, FLOAT min, FLOAT max)
{
    D12CommandList *list = impl(iface);
    list->depth_bounds[0] = min;
    list->depth_bounds[1] = max;
    if (list->open && list->child.device->has_depth_bounds)
        vk_of(list)->vkCmdSetDepthBounds(list->cmd, min, max);
}

static void STDMETHODCALLTYPE list_rs_set_depth_bias(ID3D12GraphicsCommandList10 *iface, FLOAT bias, FLOAT clamp,
                                                     FLOAT slope)
{
    D12CommandList *list = impl(iface);
    list->depth_bias[0] = bias;
    list->depth_bias[1] = clamp;
    list->depth_bias[2] = slope;
    list->depth_bias_set = TRUE;
    list->state_dirty = TRUE;
}

static void STDMETHODCALLTYPE list_ia_set_strip_cut(ID3D12GraphicsCommandList10 *iface,
                                                    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE value)
{
    D12CommandList *list = impl(iface);
    list->strip_cut = value;
    list->topology_dirty = TRUE;
}

static void STDMETHODCALLTYPE list_set_pipeline_state(ID3D12GraphicsCommandList10 *iface, ID3D12PipelineState *state)
{
    D12CommandList *list = impl(iface);
    D12PipelineState *p = D12PipelineFrom(state);
    if (p != list->pipeline)
        set_pipeline(list, p);
}

static void STDMETHODCALLTYPE list_ia_set_index_buffer(ID3D12GraphicsCommandList10 *iface,
                                                       const D3D12_INDEX_BUFFER_VIEW *view)
{
    D12CommandList *list = impl(iface);
    if (view)
        list->ib = *view;
    else
        memset(&list->ib, 0, sizeof(list->ib));
    list->ib_dirty = TRUE;
}

static void STDMETHODCALLTYPE list_ia_set_vertex_buffers(ID3D12GraphicsCommandList10 *iface, UINT start, UINT count,
                                                         const D3D12_VERTEX_BUFFER_VIEW *views)
{
    D12CommandList *list = impl(iface);
    for (UINT i = 0; i < count && start + i < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; i++) {
        D3D12_VERTEX_BUFFER_VIEW v = views ? views[i] : (D3D12_VERTEX_BUFFER_VIEW){0};
        D3D12_VERTEX_BUFFER_VIEW *slot = &list->vb[start + i];
        if (v.StrideInBytes != slot->StrideInBytes)
            list->vertex_input_dirty = TRUE;
        *slot = v;
        list->vb_dirty |= 1u << (start + i);
    }
}

static void STDMETHODCALLTYPE list_so_set_targets(ID3D12GraphicsCommandList10 *iface, UINT start, UINT count,
                                                  const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views)
{
    D12CommandList *list = impl(iface);
    if (list->so_active) {
        end_stream_output(list);
    }
    for (UINT i = 0; i < count && start + i < D3D12_SO_BUFFER_SLOT_COUNT; i++)
        list->so[start + i] = views ? views[i] : (D3D12_STREAM_OUTPUT_BUFFER_VIEW){0};
}

static void STDMETHODCALLTYPE list_om_set_render_targets(ID3D12GraphicsCommandList10 *iface, UINT count,
                                                         const D3D12_CPU_DESCRIPTOR_HANDLE *targets, BOOL single_range,
                                                         const D3D12_CPU_DESCRIPTOR_HANDLE *depth)
{
    D12CommandList *list = impl(iface);
    D12Device *device = list->child.device;
    count = D12_MIN(count, (UINT)D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
    for (UINT i = 0; i < count; i++) {
        D3D12_CPU_DESCRIPTOR_HANDLE h = single_range ? (D3D12_CPU_DESCRIPTOR_HANDLE){targets[0].ptr + i * D12_INCREMENT}
                                                     : targets[i];
        const D12TargetView *t = D12TargetFrom(device, h);
        list->rtv[i] = t ? *t : (D12TargetView){0};
    }
    list->rtv_count = count;
    const D12TargetView *d = depth ? D12TargetFrom(device, *depth) : nullptr;
    list->has_dsv = d && d->view;
    list->dsv = list->has_dsv ? *d : (D12TargetView){0};
    list->targets_dirty = TRUE;
    list->state_dirty = TRUE;
}

/* ------------------------------------------------------------------ clears */

static BOOL is_sint(VkFormat f)
{
    switch (f) {
    case VK_FORMAT_R8_SINT:
    case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32B32_SINT:
    case VK_FORMAT_R32G32B32A32_SINT:
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL is_uint(VkFormat f)
{
    switch (f) {
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32B32_UINT:
    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:
        return TRUE;
    default:
        return FALSE;
    }
}

/* clears rectangles of an attachment in a rendering of its own. */
static void clear_attachment_rects(D12CommandList *list, const D12TargetView *t, const VkClearAttachment *clear,
                                   UINT rect_count, const D3D12_RECT *rects)
{
    D12Vk *vk = vk_of(list);
    end_rendering(list);
    VkRenderingAttachmentInfo att = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = t->view->view;
    att.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent = t->extent;
    info.layerCount = t->range.layerCount;
    order_targets(list, &t->resource->image, 1);
    if (clear->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &att;
    } else {
        if (t->range.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
            info.pDepthAttachment = &att;
        if (t->range.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
            info.pStencilAttachment = &att;
    }
    vk->vkCmdBeginRendering(list->cmd, &info);
    for (UINT i = 0; i < rect_count; i++) {
        LONG left = D12_MAX(rects[i].left, 0), top = D12_MAX(rects[i].top, 0);
        LONG right = D12_MIN(rects[i].right, (LONG)t->extent.width), bottom = D12_MIN(rects[i].bottom, (LONG)t->extent.height);
        if (right <= left || bottom <= top)
            continue;
        VkClearRect r = {{{left, top}, {(UINT)(right - left), (UINT)(bottom - top)}}, 0, t->range.layerCount};
        vk->vkCmdClearAttachments(list->cmd, 1, clear, 1, &r);
    }
    vk->vkCmdEndRendering(list->cmd);
}

static void STDMETHODCALLTYPE list_clear_render_target_view(ID3D12GraphicsCommandList10 *iface,
                                                            D3D12_CPU_DESCRIPTOR_HANDLE view, const FLOAT color[4],
                                                            UINT rect_count, const D3D12_RECT *rects)
{
    D12CommandList *list = impl(iface);
    const D12TargetView *t = D12TargetFrom(list->child.device, view);
    if (!list->open || !t || !t->view)
        return;
    VkClearColorValue value;
    for (UINT i = 0; i < 4; i++) {
        if (is_uint(t->format))
            value.uint32[i] = (UINT)color[i];
        else if (is_sint(t->format))
            value.int32[i] = (INT)color[i];
        else
            value.float32[i] = color[i];
    }
    if (rect_count && rects) {
        VkClearAttachment clear = {VK_IMAGE_ASPECT_COLOR_BIT, 0, {.color = value}};
        clear_attachment_rects(list, t, &clear, rect_count, rects);
        return;
    }
    end_rendering(list);
    VkImageSubresourceRange range = t->range;
    if (t->resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
        range.baseArrayLayer = 0;
        range.layerCount = 1;
    }
    order_targets(list, &t->resource->image, 1);
    vk_of(list)->vkCmdClearColorImage(list->cmd, t->resource->image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
}

static void STDMETHODCALLTYPE list_clear_depth_stencil_view(ID3D12GraphicsCommandList10 *iface,
                                                            D3D12_CPU_DESCRIPTOR_HANDLE view, D3D12_CLEAR_FLAGS flags,
                                                            FLOAT depth, UINT8 stencil, UINT rect_count,
                                                            const D3D12_RECT *rects)
{
    D12CommandList *list = impl(iface);
    const D12TargetView *t = D12TargetFrom(list->child.device, view);
    if (!list->open || !t || !t->view)
        return;
    VkImageAspectFlags aspects = 0;
    if (flags & D3D12_CLEAR_FLAG_DEPTH)
        aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (flags & D3D12_CLEAR_FLAG_STENCIL)
        aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
    aspects &= t->range.aspectMask;
    if (!aspects)
        return;
    VkClearDepthStencilValue value = {depth, stencil};
    if (rect_count && rects) {
        VkClearAttachment clear = {aspects, 0, {.depthStencil = value}};
        clear_attachment_rects(list, t, &clear, rect_count, rects);
        return;
    }
    end_rendering(list);
    VkImageSubresourceRange range = t->range;
    range.aspectMask = aspects;
    order_targets(list, &t->resource->image, 1);
    vk_of(list)->vkCmdClearDepthStencilImage(list->cmd, t->resource->image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
}

/* patterns of up to 16 bytes, filled a word at a time when the pattern fits in one. */
static void fill_pattern(D12CommandList *list, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
                         const BYTE *pattern, UINT pattern_size)
{
    D12Vk *vk = vk_of(list);
    UINT word;
    if (pattern_size <= 4 && 4 % pattern_size == 0) {
        BYTE w[4];
        for (UINT i = 0; i < 4; i++)
            w[i] = pattern[i % pattern_size];
        memcpy(&word, w, 4);
        vk->vkCmdFillBuffer(list->cmd, buffer, offset, size & ~3ull, word);
        return;
    }
    BYTE chunk[4096];
    UINT chunk_size = sizeof(chunk) / pattern_size * pattern_size;
    for (UINT i = 0; i < chunk_size; i += pattern_size)
        memcpy(chunk + i, pattern, pattern_size);
    for (VkDeviceSize done = 0; done < size;) {
        VkDeviceSize n = D12_MIN((VkDeviceSize)chunk_size, size - done) & ~3ull;
        if (!n)
            break;
        vk->vkCmdUpdateBuffer(list->cmd, buffer, offset + done, n, chunk);
        done += n;
    }
}

/*
 * a storage image may not be a render target, so rectangles are cleared by filling rows of the packed value in
 * scratch memory once and copying them over each rectangle in bands of that many rows.
 */
static void clear_uav_rects(D12CommandList *list, const D12DescriptorInfo *info, const UINT *uints,
                            const FLOAT *floats, UINT rect_count, const D3D12_RECT *rects)
{
    D12Resource *r = info->resource;
    BYTE pattern[16];
    UINT size, ints[4];
    if (!uints && (is_uint(info->format) || is_sint(info->format))) {
        for (UINT i = 0; i < 4; i++)
            ints[i] = is_uint(info->format) ? (UINT)floats[i] : (UINT)(INT)floats[i];
        uints = ints, floats = nullptr;
    }
    if (!D3dPackClear(info->format, uints, uints ? nullptr : floats, pattern, &size))
        return;
    UINT mip = info->range.baseMipLevel;
    BOOL volume = r->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    LONG width = (LONG)D12_MAX(r->desc.Width >> mip, 1ull), height = (LONG)D12_MAX(r->desc.Height >> mip, 1u);
    UINT depth = volume ? D12_MAX((UINT)r->desc.DepthOrArraySize >> mip, 1u) : 1;
    UINT widest = 0;
    for (UINT i = 0; i < rect_count; i++)
        widest = D12_MAX(widest, (UINT)D12_MAX(D12_MIN(rects[i].right, width) - D12_MAX(rects[i].left, 0), 0));
    if (!widest)
        return;
    UINT rows = D12_MAX(65536 / (widest * size), 1u);
    VkDeviceAddress address;
    VkBuffer buffer;
    VkDeviceSize offset;
    UINT64 bytes = ((UINT64)widest * rows * size + 3) & ~3ull;
    if (!D12ScratchAlloc(list->allocator, bytes, 16, ~0u, &address) ||
        !scratch_buffer(list->allocator, address, &buffer, &offset))
        return;
    fill_pattern(list, buffer, offset, bytes, pattern, size);
    transfer_barrier(list);
    for (UINT i = 0; i < rect_count; i++) {
        LONG left = D12_MAX(rects[i].left, 0), top = D12_MAX(rects[i].top, 0);
        LONG right = D12_MIN(rects[i].right, width), bottom = D12_MIN(rects[i].bottom, height);
        if (right <= left || bottom <= top)
            continue;
        for (LONG y = top; y < bottom; y += (LONG)rows)
            for (UINT z = 0; z < depth; z++) {
                VkBufferImageCopy region = {offset, (UINT)(right - left), 0};
                region.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, mip,
                                                                     info->range.baseArrayLayer,
                                                                     volume ? 1 : info->range.layerCount};
                region.imageOffset = (VkOffset3D){left, y, (INT)z};
                region.imageExtent = (VkExtent3D){(UINT)(right - left), (UINT)D12_MIN((LONG)rows, bottom - y), 1};
                vk_of(list)->vkCmdCopyBufferToImage(list->cmd, buffer, r->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
            }
    }
}

static void clear_uav(D12CommandList *list, D3D12_CPU_DESCRIPTOR_HANDLE cpu, const UINT *uints, const FLOAT *floats,
                      UINT rect_count, const D3D12_RECT *rects)
{
    const D12DescriptorInfo *info = D12DescriptorInfoFrom(list->child.device, cpu);
    if (!list->open || !info || !info->resource)
        return;
    end_rendering(list);
    D12Resource *r = info->resource;
    /* feedback maps clear to nothing sampled, whatever the values. */
    static const UINT none_min[4] = {0xFF}, none_used[4] = {};
    if (D12IsFeedback(r->desc.Format)) {
        uints = r->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE ? none_min : none_used;
        floats = nullptr;
        rect_count = 0;
    }
    if (r->buffer) {
        BYTE pattern[16];
        UINT size = 4;
        if (info->format == VK_FORMAT_R32_UINT || info->format == VK_FORMAT_UNDEFINED) {
            UINT v = uints ? uints[0] : (UINT)floats[0];
            memcpy(pattern, &v, 4);
        } else if (!D3dPackClear(info->format, uints, floats, pattern, &size)) {
            return;
        }
        fill_pattern(list, r->buffer, r->offset + info->offset, info->size, pattern, size);
        return;
    }
    VkClearColorValue value;
    for (UINT i = 0; i < 4; i++) {
        if (uints)
            value.uint32[i] = uints[i];
        else if (is_uint(info->format))
            value.uint32[i] = (UINT)floats[i];
        else if (is_sint(info->format))
            value.int32[i] = (INT)floats[i];
        else
            value.float32[i] = floats[i];
    }
    /* uint clears of non-integer formats write the raw bits of the values, as the format stores them. */
    if (uints && !is_uint(info->format) && !is_sint(info->format)) {
        BYTE packed[16];
        UINT size;
        if (D3dPackClear(info->format, uints, nullptr, packed, &size))
            D3dClearValueOfBits(info->format, packed, &value);
    }
    VkImageSubresourceRange range = info->range;
    if (rect_count && rects) {
        clear_uav_rects(list, info, uints, floats, rect_count, rects);
        return;
    }
    vk_of(list)->vkCmdClearColorImage(list->cmd, r->image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
}

static void STDMETHODCALLTYPE list_clear_uav_uint(ID3D12GraphicsCommandList10 *iface, D3D12_GPU_DESCRIPTOR_HANDLE gpu,
                                                  D3D12_CPU_DESCRIPTOR_HANDLE cpu, ID3D12Resource *resource,
                                                  const UINT values[4], UINT rect_count, const D3D12_RECT *rects)
{
    clear_uav(impl(iface), cpu, values, nullptr, rect_count, rects);
}

static void STDMETHODCALLTYPE list_clear_uav_float(ID3D12GraphicsCommandList10 *iface, D3D12_GPU_DESCRIPTOR_HANDLE gpu,
                                                   D3D12_CPU_DESCRIPTOR_HANDLE cpu, ID3D12Resource *resource,
                                                   const FLOAT values[4], UINT rect_count, const D3D12_RECT *rects)
{
    clear_uav(impl(iface), cpu, nullptr, values, rect_count, rects);
}

/* discarded contents may be lost, so nothing needs doing. */
static void STDMETHODCALLTYPE list_discard_resource(ID3D12GraphicsCommandList10 *iface, ID3D12Resource *resource,
                                                    const D3D12_DISCARD_REGION *region)
{
}

/* ------------------------------------------------------------------ barriers */

static VkPipelineStageFlags2 shader_stages(D12Device *device)
{
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    if (device->has_geometry)
        stages |= VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT;
    if (device->has_tessellation)
        stages |= VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
                  VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT;
    if (device->has_mesh)
        stages |= VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
    if (device->has_rt_pipeline)
        stages |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    return stages;
}

/*
 * the stages and accesses a resource state covers, including the vulkan commands mime runs for it: clears and
 * copies are transfers, uav clears included. common, or a state with unknown bits, covers every command.
 */
static void state_scope(D12Device *device, UINT state, VkPipelineStageFlags2 *stages, VkAccessFlags2 *access)
{
    const UINT known = 0x3FFF | D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE |
                       D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
    VkPipelineStageFlags2 shaders = shader_stages(device), s = 0;
    VkAccessFlags2 a = 0;
    if (!state || (state & ~known)) {
        *stages |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        *access |= VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        return;
    }
    if (state & D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) {
        s |= VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | shaders;
        a |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;
    }
    if (state & D3D12_RESOURCE_STATE_INDEX_BUFFER) {
        s |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        a |= VK_ACCESS_2_INDEX_READ_BIT;
    }
    if (state & D3D12_RESOURCE_STATE_RENDER_TARGET) {
        s |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        a |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
             VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if (state & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        s |= shaders | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        a |= VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT |
             VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if (state & (D3D12_RESOURCE_STATE_DEPTH_WRITE | D3D12_RESOURCE_STATE_DEPTH_READ)) {
        s |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        a |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }
    if (state & D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        s |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        a |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if (state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        s |= shaders & ~VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        a |= VK_ACCESS_2_SHADER_READ_BIT;
        /* acceleration structure build inputs are in this state. */
        if (device->has_rt)
            s |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    }
    if (state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        s |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        a |= VK_ACCESS_2_SHADER_READ_BIT;
    }
    if (state & D3D12_RESOURCE_STATE_STREAM_OUT) {
        s |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        a |= VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
        if (device->has_transform_feedback) {
            s |= VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT;
            a |= VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT | VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT |
                 VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;
        }
    }
    /* indirect arguments and predicates: read by draws, command generation and mime's compute shaders. */
    if (state & D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) {
        s |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
             VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        a |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
        if (device->has_conditional) {
            s |= VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT;
            a |= VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT;
        }
        if (device->has_dgc) {
            s |= VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_EXT;
            a |= VK_ACCESS_2_COMMAND_PREPROCESS_READ_BIT_EXT;
        }
    }
    if (state & (D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_RESOLVE_DEST)) {
        s |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        a |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if (state & (D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_RESOLVE_SOURCE)) {
        s |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        a |= VK_ACCESS_2_TRANSFER_READ_BIT;
    }
    if (state & D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE) {
        if (device->vrs_tier >= D3D12_VARIABLE_SHADING_RATE_TIER_2) {
            s |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
            a |= VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
        } else {
            s |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            a |= VK_ACCESS_2_MEMORY_READ_BIT;
        }
    }
    if (state & D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE) {
        s |= shaders;
        a |= VK_ACCESS_2_SHADER_READ_BIT;
        if (device->has_rt) {
            s |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                 VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR;
            a |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        }
    }
    *stages |= s;
    *access |= a;
}

/*
 * every image stays in the general layout, so barriers only order memory, all of a call's in one: a transition
 * from the work of the state before to that of the state after, a uav barrier between view and acceleration
 * structure work, an aliasing barrier between all work.
 */
static void STDMETHODCALLTYPE list_resource_barrier(ID3D12GraphicsCommandList10 *iface, UINT count,
                                                    const D3D12_RESOURCE_BARRIER *barriers)
{
    D12CommandList *list = impl(iface);
    if (!list->open || !count)
        return;
    D12Device *device = list->child.device;
    VkMemoryBarrier2 b = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    for (UINT i = 0; i < count; i++) {
        const D3D12_RESOURCE_BARRIER *r = &barriers[i];
        if (r->Flags & D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY)
            continue;
        switch (r->Type) {
        case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
            state_scope(device, r->Transition.StateBefore, &b.srcStageMask, &b.srcAccessMask);
            state_scope(device, r->Transition.StateAfter, &b.dstStageMask, &b.dstAccessMask);
            break;
        case D3D12_RESOURCE_BARRIER_TYPE_UAV:
            state_scope(device, D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                        &b.srcStageMask, &b.srcAccessMask);
            state_scope(device, D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                        &b.dstStageMask, &b.dstAccessMask);
            break;
        default:
            state_scope(device, 0, &b.srcStageMask, &b.srcAccessMask);
            state_scope(device, 0, &b.dstStageMask, &b.dstAccessMask);
            break;
        }
    }
    if (!b.srcStageMask)
        return;
    end_rendering(list);
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    vk_of(list)->vkCmdPipelineBarrier2(list->cmd, &dep);
    if (b.srcStageMask & b.dstStageMask & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
        list->written_count = 0;
}

/* vulkan stages for enhanced barrier sync scopes, including the commands mime runs for them. */
static VkPipelineStageFlags2 sync_stages(D12Device *device, UINT sync)
{
    VkPipelineStageFlags2 shaders = shader_stages(device), s = 0;
    if (sync & (D3D12_BARRIER_SYNC_ALL | D3D12_BARRIER_SYNC_VIDEO_DECODE | D3D12_BARRIER_SYNC_VIDEO_PROCESS |
                D3D12_BARRIER_SYNC_VIDEO_ENCODE | D3D12_BARRIER_SYNC_SPLIT))
        return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    if (sync & D3D12_BARRIER_SYNC_DRAW)
        s |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    if (sync & D3D12_BARRIER_SYNC_INDEX_INPUT)
        s |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    if (sync & D3D12_BARRIER_SYNC_VERTEX_SHADING) {
        s |= VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | (shaders & ~(VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                                                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                                                           VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR));
        if (device->has_transform_feedback)
            s |= VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT;
    }
    if (sync & D3D12_BARRIER_SYNC_PIXEL_SHADING) {
        s |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        if (device->vrs_tier >= D3D12_VARIABLE_SHADING_RATE_TIER_2)
            s |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
    }
    if (sync & D3D12_BARRIER_SYNC_DEPTH_STENCIL)
        s |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
             VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    if (sync & D3D12_BARRIER_SYNC_RENDER_TARGET)
        s |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    if (sync & D3D12_BARRIER_SYNC_COMPUTE_SHADING)
        s |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    if ((sync & D3D12_BARRIER_SYNC_RAYTRACING) && device->has_rt_pipeline)
        s |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    if (sync & (D3D12_BARRIER_SYNC_COPY | D3D12_BARRIER_SYNC_RESOLVE | D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW))
        s |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    /* indirect arguments and predicates: read by draws, command generation and mime's compute shaders. */
    if (sync & D3D12_BARRIER_SYNC_EXECUTE_INDIRECT) {
        s |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
             VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        if (device->has_conditional)
            s |= VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT;
        if (device->has_dgc)
            s |= VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_EXT;
    }
    if (sync & D3D12_BARRIER_SYNC_ALL_SHADING)
        s |= shaders;
    if (sync & D3D12_BARRIER_SYNC_NON_PIXEL_SHADING)
        s |= shaders & ~VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    if (device->has_rt && (sync & (D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO |
                                   D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
                                   D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE)))
        s |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
             VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    return s;
}

/* accesses that write. */
#define WRITE_ACCESSES                                                                                                 \
    (D3D12_BARRIER_ACCESS_RENDER_TARGET | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |                                      \
     D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE | D3D12_BARRIER_ACCESS_STREAM_OUTPUT | D3D12_BARRIER_ACCESS_COPY_DEST |  \
     D3D12_BARRIER_ACCESS_RESOLVE_DEST | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE |                \
     D3D12_BARRIER_ACCESS_VIDEO_DECODE_WRITE | D3D12_BARRIER_ACCESS_VIDEO_PROCESS_WRITE |                              \
     D3D12_BARRIER_ACCESS_VIDEO_ENCODE_WRITE)

/*
 * folds one enhanced barrier into a memory barrier: the scopes come from its syncs, and its writes are made
 * available as a whole and visible to every later access, since images stay in the general layout. a split
 * barrier only takes effect at its end, waiting for everything before.
 */
static void add_enhanced(D12Device *device, VkMemoryBarrier2 *b, UINT sync_before, UINT sync_after,
                         UINT access_before, UINT access_after)
{
    if (sync_after == (UINT)D3D12_BARRIER_SYNC_SPLIT)
        return;
    VkPipelineStageFlags2 src = sync_stages(device, sync_before), dst = sync_stages(device, sync_after);
    b->srcStageMask |= src;
    b->dstStageMask |= dst;
    if (src && !(access_before & D3D12_BARRIER_ACCESS_NO_ACCESS) &&
        (access_before == D3D12_BARRIER_ACCESS_COMMON || (access_before & WRITE_ACCESSES)))
        b->srcAccessMask |= VK_ACCESS_2_MEMORY_WRITE_BIT;
    if (dst && !(access_after & D3D12_BARRIER_ACCESS_NO_ACCESS))
        b->dstAccessMask |= VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
}

static void STDMETHODCALLTYPE list_barrier(ID3D12GraphicsCommandList10 *iface, UINT32 count,
                                           const D3D12_BARRIER_GROUP *groups)
{
    D12CommandList *list = impl(iface);
    if (!list->open || !count || !groups)
        return;
    D12Device *device = list->child.device;
    VkMemoryBarrier2 b = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    for (UINT32 g = 0; g < count; g++) {
        for (UINT32 i = 0; i < groups[g].NumBarriers; i++) {
            switch (groups[g].Type) {
            case D3D12_BARRIER_TYPE_GLOBAL: {
                const D3D12_GLOBAL_BARRIER *x = &groups[g].pGlobalBarriers[i];
                add_enhanced(device, &b, x->SyncBefore, x->SyncAfter, x->AccessBefore, x->AccessAfter);
                break;
            }
            case D3D12_BARRIER_TYPE_TEXTURE: {
                const D3D12_TEXTURE_BARRIER *x = &groups[g].pTextureBarriers[i];
                add_enhanced(device, &b, x->SyncBefore, x->SyncAfter, x->AccessBefore, x->AccessAfter);
                break;
            }
            case D3D12_BARRIER_TYPE_BUFFER: {
                const D3D12_BUFFER_BARRIER *x = &groups[g].pBufferBarriers[i];
                add_enhanced(device, &b, x->SyncBefore, x->SyncAfter, x->AccessBefore, x->AccessAfter);
                break;
            }
            default:
                add_enhanced(device, &b, D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_SYNC_ALL, 0, 0);
                break;
            }
        }
    }
    if (!b.srcStageMask && !b.dstStageMask)
        return;
    /* with no work before or after, vulkan still takes a dependency from the top or to the bottom. */
    if (!b.srcStageMask)
        b.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    end_rendering(list);
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    vk_of(list)->vkCmdPipelineBarrier2(list->cmd, &dep);
    if (b.srcStageMask & b.dstStageMask & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
        list->written_count = 0;
}

/* ------------------------------------------------------------------ root signatures and arguments */

static void STDMETHODCALLTYPE list_set_descriptor_heaps(ID3D12GraphicsCommandList10 *iface, UINT count,
                                                        ID3D12DescriptorHeap *const *heaps)
{
    D12CommandList *list = impl(iface);
    for (UINT i = 0; i < count; i++) {
        D12DescriptorHeap *heap = D12DescriptorHeapFrom(heaps[i]);
        if (!heap || !(heap->desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
            continue;
        UINT which = heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        if (list->heaps[which] != heap) {
            list->heaps[which] = heap;
            list->heaps_dirty = TRUE;
        }
    }
}

static void STDMETHODCALLTYPE list_set_compute_root_signature(ID3D12GraphicsCommandList10 *iface,
                                                              ID3D12RootSignature *root)
{
    set_root_signature(impl(iface), TRUE, root);
}

static void STDMETHODCALLTYPE list_set_graphics_root_signature(ID3D12GraphicsCommandList10 *iface,
                                                               ID3D12RootSignature *root)
{
    set_root_signature(impl(iface), FALSE, root);
}

static void STDMETHODCALLTYPE list_set_compute_root_descriptor_table(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                                     D3D12_GPU_DESCRIPTOR_HANDLE base)
{
    set_root_table(impl(iface), TRUE, index, base);
}

static void STDMETHODCALLTYPE list_set_graphics_root_descriptor_table(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                                      D3D12_GPU_DESCRIPTOR_HANDLE base)
{
    set_root_table(impl(iface), FALSE, index, base);
}

static void STDMETHODCALLTYPE list_set_compute_root_32bit_constant(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                                   UINT data, UINT offset)
{
    set_root_words(impl(iface), TRUE, index, offset, 1, &data);
}

static void STDMETHODCALLTYPE list_set_graphics_root_32bit_constant(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                                    UINT data, UINT offset)
{
    set_root_words(impl(iface), FALSE, index, offset, 1, &data);
}

static void STDMETHODCALLTYPE list_set_compute_root_32bit_constants(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                                    UINT count, const void *data, UINT offset)
{
    set_root_words(impl(iface), TRUE, index, offset, count, data);
}

static void STDMETHODCALLTYPE list_set_graphics_root_32bit_constants(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                                     UINT count, const void *data, UINT offset)
{
    set_root_words(impl(iface), FALSE, index, offset, count, data);
}

static void STDMETHODCALLTYPE list_set_compute_root_view(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                         D3D12_GPU_VIRTUAL_ADDRESS address)
{
    set_root_address(impl(iface), TRUE, index, address);
}

static void STDMETHODCALLTYPE list_set_graphics_root_view(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                          D3D12_GPU_VIRTUAL_ADDRESS address)
{
    set_root_address(impl(iface), FALSE, index, address);
}

/* ------------------------------------------------------------------ queries and predication */

void D12BeginQuery(D12CommandList *list, D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index);
void D12EndQuery(D12CommandList *list, D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index);
void D12ResolveQueries(D12CommandList *list, D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start, UINT count,
                       D12Resource *dst, UINT64 offset);

static void STDMETHODCALLTYPE list_begin_query(ID3D12GraphicsCommandList10 *iface, ID3D12QueryHeap *heap,
                                               D3D12_QUERY_TYPE type, UINT index)
{
    D12CommandList *list = impl(iface);
    if (list->open && heap)
        D12BeginQuery(list, D12QueryHeapFrom(heap), type, index);
}

static void STDMETHODCALLTYPE list_end_query(ID3D12GraphicsCommandList10 *iface, ID3D12QueryHeap *heap,
                                             D3D12_QUERY_TYPE type, UINT index)
{
    D12CommandList *list = impl(iface);
    if (list->open && heap)
        D12EndQuery(list, D12QueryHeapFrom(heap), type, index);
}

static void STDMETHODCALLTYPE list_resolve_query_data(ID3D12GraphicsCommandList10 *iface, ID3D12QueryHeap *heap,
                                                      D3D12_QUERY_TYPE type, UINT start, UINT count,
                                                      ID3D12Resource *dst, UINT64 offset)
{
    D12CommandList *list = impl(iface);
    if (!list->open || !heap || !dst || !count)
        return;
    end_rendering(list);
    D12ResolveQueries(list, D12QueryHeapFrom(heap), type, start, count, D12ResourceFrom(dst), offset);
}

/* uses vulkan conditional rendering, which reads the predicate's low 32 bits. */
static void STDMETHODCALLTYPE list_set_predication(ID3D12GraphicsCommandList10 *iface, ID3D12Resource *buffer,
                                                   UINT64 offset, D3D12_PREDICATION_OP op)
{
    D12CommandList *list = impl(iface);
    D12Device *device = list->child.device;
    if (!list->open || !device->has_conditional)
        return;
    end_rendering(list);
    if (list->predicated) {
        device->vk.vkCmdEndConditionalRenderingEXT(list->cmd);
        list->predicated = FALSE;
    }
    D12Resource *r = D12ResourceFrom(buffer);
    if (!r || !r->buffer)
        return;
    VkConditionalRenderingBeginInfoEXT info = {VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT};
    info.buffer = r->buffer;
    info.offset = r->offset + offset;
    info.flags = op == D3D12_PREDICATION_OP_NOT_EQUAL_ZERO ? VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT : 0;
    device->vk.vkCmdBeginConditionalRenderingEXT(list->cmd, &info);
    list->predicated = TRUE;
}

static void STDMETHODCALLTYPE list_set_marker(ID3D12GraphicsCommandList10 *iface, UINT metadata, const void *data,
                                              UINT size)
{
}

static void STDMETHODCALLTYPE list_end_event(ID3D12GraphicsCommandList10 *iface) {}

/* ------------------------------------------------------------------ indirect */

static VkPipeline prepare_rays(D12CommandList *list);

/*
 * device generated commands from the argument buffer, each changing root arguments, vertex or index buffers before
 * its draw or dispatch. root arguments are push constants and direct3d addresses are vulkan addresses, so the
 * arguments are read as they are. the state they change is undefined afterwards, so the next draw or dispatch sets
 * it again.
 */
static void generated_commands(D12CommandList *list, D12CommandSignature *signature, D12Resource *args,
                               UINT64 arguments_offset, UINT max_count, D12Resource *counts, UINT64 count_offset)
{
    D12Device *device = list->child.device;
    D12Vk *vk = &device->vk;
    BOOL rays = (signature->stages & VK_SHADER_STAGE_RAYGEN_BIT_KHR) != 0;
    BOOL compute = rays || signature->stages == VK_SHADER_STAGE_COMPUTE_BIT;
    VkGeneratedCommandsPipelineInfoEXT pipeline = {VK_STRUCTURE_TYPE_GENERATED_COMMANDS_PIPELINE_INFO_EXT};
    if (rays) {
        if (!(pipeline.pipeline = prepare_rays(list)))
            return;
    } else {
        if (compute ? !prepare_dispatch(list) : !prepare_draw(list))
            return;
        pipeline.pipeline = list->pipeline->pipeline;
    }
    VkGeneratedCommandsMemoryRequirementsInfoEXT req_info = {
        VK_STRUCTURE_TYPE_GENERATED_COMMANDS_MEMORY_REQUIREMENTS_INFO_EXT, &pipeline};
    req_info.indirectCommandsLayout = signature->layout;
    req_info.maxSequenceCount = max_count;
    VkMemoryRequirements2 req = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    vk->vkGetGeneratedCommandsMemoryRequirementsEXT(vk->device, &req_info, &req);
    VkDeviceAddress preprocess = 0;
    if (req.memoryRequirements.size &&
        !D12ScratchAlloc(list->allocator, req.memoryRequirements.size, req.memoryRequirements.alignment,
                         req.memoryRequirements.memoryTypeBits, &preprocess)) {
        D12Log("d3d12: no memory for generated commands");
        return;
    }
    VkGeneratedCommandsInfoEXT info = {VK_STRUCTURE_TYPE_GENERATED_COMMANDS_INFO_EXT, &pipeline};
    info.shaderStages = signature->stages;
    info.indirectCommandsLayout = signature->layout;
    info.indirectAddress = args->va + arguments_offset;
    info.indirectAddressSize = (VkDeviceSize)signature->stride * max_count;
    info.preprocessAddress = preprocess;
    info.preprocessSize = req.memoryRequirements.size;
    info.maxSequenceCount = max_count;
    if (counts)
        info.sequenceCountAddress = counts->va + count_offset;
    vk->vkCmdExecuteGeneratedCommandsEXT(list->cmd, VK_FALSE, &info);
    D12RootState *s = compute ? &list->compute : &list->graphics;
    s->push_dirty = TRUE;
    list->pushed_compute = -1;
    if (!compute) {
        list->vb_dirty = ~0u;
        list->ib_dirty = TRUE;
    }
}

static void STDMETHODCALLTYPE list_execute_indirect(ID3D12GraphicsCommandList10 *iface,
                                                    ID3D12CommandSignature *signature_iface, UINT max_count,
                                                    ID3D12Resource *arguments, UINT64 arguments_offset,
                                                    ID3D12Resource *count, UINT64 count_offset)
{
    D12CommandList *list = impl(iface);
    D12CommandSignature *signature = D12CommandSignatureFrom(signature_iface);
    D12Resource *args = D12ResourceFrom(arguments), *counts = D12ResourceFrom(count);
    if (!list->open || !signature || !args || !args->buffer || !max_count)
        return;
    D12Vk *vk = vk_of(list);
    const D3D12_INDIRECT_ARGUMENT_DESC *last = &signature->args[signature->arg_count - 1];
    if (signature->layout && (signature->arg_count > 1 || (counts && last->Type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH))) {
        generated_commands(list, signature, args, arguments_offset, max_count, counts, count_offset);
        return;
    }
    if (signature->arg_count != 1) {
        D12Log("d3d12: command signatures changing state per command are not supported without generated commands");
        return;
    }
    VkDeviceSize offset = args->offset + arguments_offset;
    switch (last->Type) {
    case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
        if (!prepare_draw(list))
            return;
        if (counts)
            vk->vkCmdDrawIndirectCount(list->cmd, args->buffer, offset, counts->buffer, counts->offset + count_offset,
                                       max_count, signature->stride);
        else
            vk->vkCmdDrawIndirect(list->cmd, args->buffer, offset, max_count, signature->stride);
        break;
    case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
        if (!list->ib.BufferLocation || !prepare_draw(list))
            return;
        if (counts)
            vk->vkCmdDrawIndexedIndirectCount(list->cmd, args->buffer, offset, counts->buffer,
                                              counts->offset + count_offset, max_count, signature->stride);
        else
            vk->vkCmdDrawIndexedIndirect(list->cmd, args->buffer, offset, max_count, signature->stride);
        break;
    case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH:
        if (!list->pipeline || !list->pipeline->mesh || !prepare_draw(list))
            return;
        if (counts)
            vk->vkCmdDrawMeshTasksIndirectCountEXT(list->cmd, args->buffer, offset, counts->buffer,
                                                   counts->offset + count_offset, max_count, signature->stride);
        else
            vk->vkCmdDrawMeshTasksIndirectEXT(list->cmd, args->buffer, offset, max_count, signature->stride);
        break;
    case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
        if (!prepare_dispatch(list))
            return;
        /* a guess: the count buffer is not read, every command up to the maximum runs. */
        for (UINT i = 0; i < max_count; i++)
            vk->vkCmdDispatchIndirect(list->cmd, args->buffer, offset + (VkDeviceSize)i * signature->stride);
        break;
    case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS: {
        /* the arguments match VkTraceRaysIndirectCommand2KHR, limited to the count by a copy. */
        VkDeviceAddress at = args->va + arguments_offset;
        VkDeviceSize stride = signature->stride;
        if (counts) {
            VkDeviceAddress limited;
            if (!list->open || !D12ScratchAlloc(list->allocator, (UINT64)max_count * sizeof(D3D12_DISPATCH_RAYS_DESC), 16,
                                                ~0u, &limited))
                return;
            end_rendering(list);
            D12LimitRays(list->child.device, list->cmd, at, signature->stride, max_count, counts->va + count_offset,
                         limited);
            list->bound_cs = VK_NULL_HANDLE;
            list->pushed_compute = -1;
            list->compute.sets_dirty = list->compute.push_dirty = TRUE;
            barrier(list);
            at = limited;
            stride = sizeof(D3D12_DISPATCH_RAYS_DESC);
        }
        if (!prepare_rays(list))
            return;
        for (UINT i = 0; i < max_count; i++)
            vk->vkCmdTraceRaysIndirect2KHR(list->cmd, at + i * stride);
        break;
    }
    default:
        D12Log("d3d12: indirect argument type %u is not supported", last->Type);
        break;
    }
}

/* ------------------------------------------------------------------ newer list methods */

static void atomic_copy(D12CommandList *list, ID3D12Resource *dst, UINT64 dst_offset, ID3D12Resource *src,
                        UINT64 src_offset, UINT64 size)
{
    D12Resource *d = D12ResourceFrom(dst), *s = D12ResourceFrom(src);
    if (!list->open || !d || !s || !d->buffer || !s->buffer)
        return;
    end_rendering(list);
    VkBufferCopy region = {s->offset + src_offset, d->offset + dst_offset, size};
    vk_of(list)->vkCmdCopyBuffer(list->cmd, s->buffer, d->buffer, 1, &region);
}

static void STDMETHODCALLTYPE list_atomic_copy_uint(ID3D12GraphicsCommandList10 *iface, ID3D12Resource *dst,
                                                    UINT64 dst_offset, ID3D12Resource *src, UINT64 src_offset,
                                                    UINT dependencies, ID3D12Resource *const *resources,
                                                    const void *ranges)
{
    atomic_copy(impl(iface), dst, dst_offset, src, src_offset, 4);
}

static void STDMETHODCALLTYPE list_atomic_copy_uint64(ID3D12GraphicsCommandList10 *iface, ID3D12Resource *dst,
                                                      UINT64 dst_offset, ID3D12Resource *src, UINT64 src_offset,
                                                      UINT dependencies, ID3D12Resource *const *resources,
                                                      const void *ranges)
{
    atomic_copy(impl(iface), dst, dst_offset, src, src_offset, 8);
}

static void STDMETHODCALLTYPE list_set_sample_positions(ID3D12GraphicsCommandList10 *iface, UINT samples, UINT pixels,
                                                        const void *positions)
{
}

static void STDMETHODCALLTYPE list_set_view_instance_mask(ID3D12GraphicsCommandList10 *iface, UINT mask) {}

/* each parameter is a destination address and a value. */
static void STDMETHODCALLTYPE list_write_buffer_immediate(ID3D12GraphicsCommandList10 *iface, UINT count,
                                                          const void *parameters, const void *modes)
{
    D12CommandList *list = impl(iface);
    if (!list->open)
        return;
    end_rendering(list);
    const BYTE *p = parameters;
    for (UINT i = 0; i < count; i++, p += 16) {
        D3D12_GPU_VIRTUAL_ADDRESS address;
        UINT value;
        memcpy(&address, p, 8);
        memcpy(&value, p + 8, 4);
        VkBuffer buffer;
        VkDeviceSize offset;
        if (D12AddressLookup(list->child.device, address, &buffer, &offset, nullptr))
            vk_of(list)->vkCmdUpdateBuffer(list->cmd, buffer, offset, 4, &value);
    }
}

static void STDMETHODCALLTYPE list_set_protected_session(ID3D12GraphicsCommandList10 *iface, void *session) {}

/* sets the targets, clears them as asked, and resolves them at the end. */
static void STDMETHODCALLTYPE list_begin_render_pass(ID3D12GraphicsCommandList10 *iface, UINT count,
                                                     const D3D12_RENDER_PASS_RENDER_TARGET_DESC *targets,
                                                     const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *depth,
                                                     D3D12_RENDER_PASS_FLAGS flags)
{
    D12CommandList *list = impl(iface);
    D3D12_CPU_DESCRIPTOR_HANDLE handles[8];
    count = D12_MIN(count, 8u);
    for (UINT i = 0; i < count; i++)
        handles[i] = targets[i].cpuDescriptor;
    list_om_set_render_targets(iface, count, handles, FALSE, depth ? &depth->cpuDescriptor : nullptr);
    for (UINT i = 0; i < count; i++)
        if (targets[i].BeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
            list_clear_render_target_view(iface, handles[i], targets[i].BeginningAccess.Clear.ClearValue.Color, 0, nullptr);
    if (depth) {
        D3D12_CLEAR_FLAGS clear = 0;
        if (depth->DepthBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
            clear |= D3D12_CLEAR_FLAG_DEPTH;
        if (depth->StencilBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
            clear |= D3D12_CLEAR_FLAG_STENCIL;
        if (clear)
            list_clear_depth_stencil_view(iface, depth->cpuDescriptor, clear,
                                          depth->DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth,
                                          depth->StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil, 0, nullptr);
    }
    memcpy(list->pass_targets, targets, count * sizeof(*targets));
    list->pass_count = count;
}

static void STDMETHODCALLTYPE list_end_render_pass(ID3D12GraphicsCommandList10 *iface)
{
    D12CommandList *list = impl(iface);
    end_rendering(list);
    for (UINT i = 0; i < list->pass_count; i++) {
        const D3D12_RENDER_PASS_ENDING_ACCESS *e = &list->pass_targets[i].EndingAccess;
        if (e->Type != D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
            continue;
        for (UINT k = 0; k < e->Resolve.SubresourceCount; k++) {
            const D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS *p = &e->Resolve.pSubresourceParameters[k];
            resolve(list, D12ResourceFrom(e->Resolve.pDstResource), p->DstSubresource, p->DstX, p->DstY,
                    D12ResourceFrom(e->Resolve.pSrcResource), p->SrcSubresource, &p->SrcRect);
        }
    }
    list->pass_count = 0;
}

static void STDMETHODCALLTYPE list_meta_command(ID3D12GraphicsCommandList10 *iface, void *command, const void *data,
                                                SIZE_T size)
{
}

static void STDMETHODCALLTYPE list_build_acceleration_structure(
    ID3D12GraphicsCommandList10 *iface, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *desc, UINT count,
    const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *postbuild)
{
    D12CommandList *list = impl(iface);
    D12Device *device = list->child.device;
    if (!list->open || !desc || !device->has_rt)
        return;
    end_rendering(list);
    D12BuildAccelerationStructure(device, list->cmd, desc);
    if (!count || !postbuild)
        return;
    /* postbuild info needs the finished build. */
    barrier(list);
    for (UINT i = 0; i < count; i++)
        D12EmitPostbuildInfo(list->allocator, list->cmd, &postbuild[i], 1, &desc->DestAccelerationStructureData);
}

static void STDMETHODCALLTYPE list_emit_postbuild_info(
    ID3D12GraphicsCommandList10 *iface, const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc,
    UINT count, const D3D12_GPU_VIRTUAL_ADDRESS *src)
{
    D12CommandList *list = impl(iface);
    if (!list->open || !desc || !src || !list->child.device->has_rt)
        return;
    end_rendering(list);
    D12EmitPostbuildInfo(list->allocator, list->cmd, desc, count, src);
}

static void STDMETHODCALLTYPE list_copy_acceleration_structure(ID3D12GraphicsCommandList10 *iface,
                                                               D3D12_GPU_VIRTUAL_ADDRESS dst,
                                                               D3D12_GPU_VIRTUAL_ADDRESS src, UINT mode)
{
    D12CommandList *list = impl(iface);
    if (!list->open || !list->child.device->has_rt)
        return;
    end_rendering(list);
    D12CopyAccelerationStructure(list->child.device, list->cmd, dst, src, mode);
}

static void STDMETHODCALLTYPE list_set_pipeline_state1(ID3D12GraphicsCommandList10 *iface, ID3D12StateObject *state)
{
    D12CommandList *list = impl(iface);
    if (list->open)
        list->rt_state = D12StateObjectFrom(state);
}

/* binds the ray tracing pipeline and compute root arguments, returns VK_NULL_HANDLE if there is no pipeline. */
static VkPipeline prepare_rays(D12CommandList *list)
{
    VkPipeline pipeline = list->rt_state ? D12StateObjectPipeline(list->rt_state) : VK_NULL_HANDLE;
    if (!list->open || !pipeline || !list->compute.root)
        return VK_NULL_HANDLE;
    D12Vk *vk = vk_of(list);
    end_rendering(list);
    if (list->bound_rt != pipeline) {
        vk->vkCmdBindPipeline(list->cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
        list->bound_rt = pipeline;
    }
    bind_sets(list, TRUE);
    set_offsets(list, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, list->compute.root);
    push_root(list, TRUE);
    vk->vkCmdSetRayTracingPipelineStackSizeKHR(list->cmd, (UINT32)D12StateObjectStackSize(list->rt_state));
    return pipeline;
}

/* shader tables map directly. the ray generation record is a region of one record. */
static void STDMETHODCALLTYPE list_dispatch_rays(ID3D12GraphicsCommandList10 *iface, const D3D12_DISPATCH_RAYS_DESC *desc)
{
    D12CommandList *list = impl(iface);
    if (!desc || !desc->Width || !desc->Height || !desc->Depth || !prepare_rays(list))
        return;
    D12Vk *vk = vk_of(list);
    VkStridedDeviceAddressRegionKHR raygen = {desc->RayGenerationShaderRecord.StartAddress,
                                              desc->RayGenerationShaderRecord.SizeInBytes,
                                              desc->RayGenerationShaderRecord.SizeInBytes};
    VkStridedDeviceAddressRegionKHR miss = {desc->MissShaderTable.StartAddress, desc->MissShaderTable.StrideInBytes,
                                            desc->MissShaderTable.SizeInBytes};
    VkStridedDeviceAddressRegionKHR hit = {desc->HitGroupTable.StartAddress, desc->HitGroupTable.StrideInBytes,
                                           desc->HitGroupTable.SizeInBytes};
    VkStridedDeviceAddressRegionKHR callable = {desc->CallableShaderTable.StartAddress,
                                                desc->CallableShaderTable.StrideInBytes,
                                                desc->CallableShaderTable.SizeInBytes};
    vk->vkCmdTraceRaysKHR(list->cmd, &raygen, &miss, &hit, &callable, desc->Width, desc->Height, desc->Depth);
}

static void STDMETHODCALLTYPE list_rs_set_shading_rate(ID3D12GraphicsCommandList10 *iface, UINT rate,
                                                       const UINT *combiners)
{
    D12CommandList *list = impl(iface);
    list->shading_rate = rate;
    list->rate_combiners[0] = combiners ? combiners[0] : D3D12_SHADING_RATE_COMBINER_PASSTHROUGH;
    list->rate_combiners[1] = combiners ? combiners[1] : D3D12_SHADING_RATE_COMBINER_PASSTHROUGH;
    list->rate_dirty = TRUE;
}

/* the image is a rendering attachment, so rendering restarts with it. */
static void STDMETHODCALLTYPE list_rs_set_shading_rate_image(ID3D12GraphicsCommandList10 *iface, ID3D12Resource *image)
{
    D12CommandList *list = impl(iface);
    D12Resource *r = D12ResourceFrom(image);
    if (r && (!r->image || !(r->usage & VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR)))
        r = nullptr;
    if (r != list->rate_image) {
        list->rate_image = r;
        list->targets_dirty = TRUE;
    }
}

static void STDMETHODCALLTYPE list_dispatch_mesh(ID3D12GraphicsCommandList10 *iface, UINT x, UINT y, UINT z)
{
    D12CommandList *list = impl(iface);
    if (!x || !y || !z || !list->pipeline || !list->pipeline->mesh || !prepare_draw(list))
        return;
    vk_of(list)->vkCmdDrawMeshTasksEXT(list->cmd, x, y, z);
}

static void STDMETHODCALLTYPE list_set_program(ID3D12GraphicsCommandList10 *iface, const void *desc) {}

/* ------------------------------------------------------------------ bundles */

/*
 * a bundle records its calls and the executing list replays them, so like direct3d it inherits that list's root
 * signatures, arguments and heaps, and leaves its own state behind.
 */
enum {
    CALL_DRAW,
    CALL_DRAW_INDEXED,
    CALL_DISPATCH,
    CALL_DISPATCH_MESH,
    CALL_TOPOLOGY,
    CALL_BLEND_FACTOR,
    CALL_STENCIL_REF,
    CALL_FRONT_BACK_STENCIL_REF,
    CALL_DEPTH_BOUNDS,
    CALL_DEPTH_BIAS,
    CALL_STRIP_CUT,
    CALL_PIPELINE,
    CALL_HEAPS,
    CALL_ROOT_SIGNATURE,
    CALL_ROOT_TABLE,
    CALL_ROOT_CONSTANTS,
    CALL_ROOT_VIEW,
    CALL_INDEX_BUFFER,
    CALL_VERTEX_BUFFERS,
    CALL_EXECUTE_INDIRECT,
    CALL_STATE_OBJECT,
    CALL_DISPATCH_RAYS,
};

typedef struct BundleCall {
    UINT op;
    /* in bytes, header included. */
    UINT size;
    /* compute bind point for root calls. */
    UINT compute;
    UINT count;
    UINT64 a[6];
} BundleCall;

/* extra is room for bytes after the call. NULL when out of memory. */
static BundleCall *record(D12CommandList *list, UINT op, UINT extra)
{
    if (!list->recording)
        return nullptr;
    SIZE_T size = D12_ALIGN(sizeof(BundleCall) + extra, 8);
    if (list->call_size + size > list->call_capacity) {
        SIZE_T capacity = D12_MAX(list->call_capacity * 2, list->call_size + size + 4096);
        BYTE *calls = list->calls ? HeapReAlloc(GetProcessHeap(), 0, list->calls, capacity)
                                  : HeapAlloc(GetProcessHeap(), 0, capacity);
        if (!calls) {
            list->error = E_OUTOFMEMORY;
            return nullptr;
        }
        list->calls = calls;
        list->call_capacity = capacity;
    }
    BundleCall *call = (BundleCall *)(list->calls + list->call_size);
    memset(call, 0, sizeof(*call));
    call->op = op;
    call->size = (UINT)size;
    list->call_size += size;
    return call;
}

static void STDMETHODCALLTYPE bundle_draw_instanced(ID3D12GraphicsCommandList10 *iface, UINT vertices, UINT instances,
                                                    UINT start_vertex, UINT start_instance)
{
    BundleCall *c = record(impl(iface), CALL_DRAW, 0);
    if (c)
        c->a[0] = vertices, c->a[1] = instances, c->a[2] = start_vertex, c->a[3] = start_instance;
}

static void STDMETHODCALLTYPE bundle_draw_indexed_instanced(ID3D12GraphicsCommandList10 *iface, UINT indices,
                                                            UINT instances, UINT start_index, INT base_vertex,
                                                            UINT start_instance)
{
    BundleCall *c = record(impl(iface), CALL_DRAW_INDEXED, 0);
    if (c)
        c->a[0] = indices, c->a[1] = instances, c->a[2] = start_index, c->a[3] = (UINT)base_vertex,
        c->a[4] = start_instance;
}

static void STDMETHODCALLTYPE bundle_dispatch(ID3D12GraphicsCommandList10 *iface, UINT x, UINT y, UINT z)
{
    BundleCall *c = record(impl(iface), CALL_DISPATCH, 0);
    if (c)
        c->a[0] = x, c->a[1] = y, c->a[2] = z;
}

static void STDMETHODCALLTYPE bundle_dispatch_mesh(ID3D12GraphicsCommandList10 *iface, UINT x, UINT y, UINT z)
{
    BundleCall *c = record(impl(iface), CALL_DISPATCH_MESH, 0);
    if (c)
        c->a[0] = x, c->a[1] = y, c->a[2] = z;
}

static void STDMETHODCALLTYPE bundle_set_pipeline_state1(ID3D12GraphicsCommandList10 *iface, ID3D12StateObject *state)
{
    BundleCall *c = record(impl(iface), CALL_STATE_OBJECT, 0);
    if (c)
        c->a[0] = (ULONG_PTR)state;
}

static void STDMETHODCALLTYPE bundle_dispatch_rays(ID3D12GraphicsCommandList10 *iface, const D3D12_DISPATCH_RAYS_DESC *desc)
{
    BundleCall *c = desc ? record(impl(iface), CALL_DISPATCH_RAYS, sizeof(*desc)) : nullptr;
    if (c)
        memcpy(c + 1, desc, sizeof(*desc));
}

static void STDMETHODCALLTYPE bundle_ia_set_primitive_topology(ID3D12GraphicsCommandList10 *iface,
                                                               D3D_PRIMITIVE_TOPOLOGY t)
{
    BundleCall *c = record(impl(iface), CALL_TOPOLOGY, 0);
    if (c)
        c->a[0] = t;
}

static void STDMETHODCALLTYPE bundle_om_set_blend_factor(ID3D12GraphicsCommandList10 *iface, const FLOAT factor[4])
{
    BundleCall *c = record(impl(iface), CALL_BLEND_FACTOR, 16);
    if (c) {
        c->count = factor != nullptr;
        if (factor)
            memcpy(c + 1, factor, 16);
    }
}

static void STDMETHODCALLTYPE bundle_om_set_stencil_ref(ID3D12GraphicsCommandList10 *iface, UINT ref)
{
    BundleCall *c = record(impl(iface), CALL_STENCIL_REF, 0);
    if (c)
        c->a[0] = ref;
}

static void STDMETHODCALLTYPE bundle_om_set_front_and_back_stencil_ref(ID3D12GraphicsCommandList10 *iface,
                                                                       UINT front, UINT back)
{
    BundleCall *c = record(impl(iface), CALL_FRONT_BACK_STENCIL_REF, 0);
    if (c)
        c->a[0] = front, c->a[1] = back;
}

static void STDMETHODCALLTYPE bundle_om_set_depth_bounds(ID3D12GraphicsCommandList10 *iface, FLOAT min, FLOAT max)
{
    BundleCall *c = record(impl(iface), CALL_DEPTH_BOUNDS, 8);
    if (c) {
        FLOAT v[2] = {min, max};
        memcpy(c + 1, v, 8);
    }
}

static void STDMETHODCALLTYPE bundle_rs_set_depth_bias(ID3D12GraphicsCommandList10 *iface, FLOAT bias, FLOAT clamp,
                                                       FLOAT slope)
{
    BundleCall *c = record(impl(iface), CALL_DEPTH_BIAS, 12);
    if (c) {
        FLOAT v[3] = {bias, clamp, slope};
        memcpy(c + 1, v, 12);
    }
}

static void STDMETHODCALLTYPE bundle_ia_set_strip_cut(ID3D12GraphicsCommandList10 *iface,
                                                      D3D12_INDEX_BUFFER_STRIP_CUT_VALUE value)
{
    BundleCall *c = record(impl(iface), CALL_STRIP_CUT, 0);
    if (c)
        c->a[0] = value;
}

static void STDMETHODCALLTYPE bundle_set_pipeline_state(ID3D12GraphicsCommandList10 *iface, ID3D12PipelineState *state)
{
    BundleCall *c = record(impl(iface), CALL_PIPELINE, 0);
    if (c)
        c->a[0] = (UINT64)(ULONG_PTR)state;
}

static void STDMETHODCALLTYPE bundle_set_descriptor_heaps(ID3D12GraphicsCommandList10 *iface, UINT count,
                                                          ID3D12DescriptorHeap *const *heaps)
{
    BundleCall *c = record(impl(iface), CALL_HEAPS, 0);
    if (c) {
        c->count = D12_MIN(count, 2u);
        for (UINT i = 0; i < c->count; i++)
            c->a[i] = (UINT64)(ULONG_PTR)heaps[i];
    }
}

static void bundle_root_signature(ID3D12GraphicsCommandList10 *iface, BOOL compute, ID3D12RootSignature *root)
{
    BundleCall *c = record(impl(iface), CALL_ROOT_SIGNATURE, 0);
    if (c)
        c->compute = compute, c->a[0] = (UINT64)(ULONG_PTR)root;
}

static void STDMETHODCALLTYPE bundle_set_compute_root_signature(ID3D12GraphicsCommandList10 *iface,
                                                                ID3D12RootSignature *root)
{
    bundle_root_signature(iface, TRUE, root);
}

static void STDMETHODCALLTYPE bundle_set_graphics_root_signature(ID3D12GraphicsCommandList10 *iface,
                                                                 ID3D12RootSignature *root)
{
    bundle_root_signature(iface, FALSE, root);
}

static void bundle_root_table(ID3D12GraphicsCommandList10 *iface, BOOL compute, UINT index,
                              D3D12_GPU_DESCRIPTOR_HANDLE base)
{
    BundleCall *c = record(impl(iface), CALL_ROOT_TABLE, 0);
    if (c)
        c->compute = compute, c->a[0] = index, c->a[1] = base.ptr;
}

static void STDMETHODCALLTYPE bundle_set_compute_root_descriptor_table(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                                       D3D12_GPU_DESCRIPTOR_HANDLE base)
{
    bundle_root_table(iface, TRUE, index, base);
}

static void STDMETHODCALLTYPE bundle_set_graphics_root_descriptor_table(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                                        D3D12_GPU_DESCRIPTOR_HANDLE base)
{
    bundle_root_table(iface, FALSE, index, base);
}

static void bundle_root_constants(ID3D12GraphicsCommandList10 *iface, BOOL compute, UINT index, UINT count,
                                  const void *data, UINT offset)
{
    BundleCall *c = record(impl(iface), CALL_ROOT_CONSTANTS, count * 4);
    if (c) {
        c->compute = compute, c->count = count, c->a[0] = index, c->a[1] = offset;
        memcpy(c + 1, data, count * 4);
    }
}

static void STDMETHODCALLTYPE bundle_set_compute_root_32bit_constant(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                                     UINT data, UINT offset)
{
    bundle_root_constants(iface, TRUE, index, 1, &data, offset);
}

static void STDMETHODCALLTYPE bundle_set_graphics_root_32bit_constant(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                                      UINT data, UINT offset)
{
    bundle_root_constants(iface, FALSE, index, 1, &data, offset);
}

static void STDMETHODCALLTYPE bundle_set_compute_root_32bit_constants(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                                      UINT count, const void *data, UINT offset)
{
    bundle_root_constants(iface, TRUE, index, count, data, offset);
}

static void STDMETHODCALLTYPE bundle_set_graphics_root_32bit_constants(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                                       UINT count, const void *data, UINT offset)
{
    bundle_root_constants(iface, FALSE, index, count, data, offset);
}

static void bundle_root_view(ID3D12GraphicsCommandList10 *iface, BOOL compute, UINT index,
                             D3D12_GPU_VIRTUAL_ADDRESS address)
{
    BundleCall *c = record(impl(iface), CALL_ROOT_VIEW, 0);
    if (c)
        c->compute = compute, c->a[0] = index, c->a[1] = address;
}

static void STDMETHODCALLTYPE bundle_set_compute_root_view(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                           D3D12_GPU_VIRTUAL_ADDRESS address)
{
    bundle_root_view(iface, TRUE, index, address);
}

static void STDMETHODCALLTYPE bundle_set_graphics_root_view(ID3D12GraphicsCommandList10 *iface, UINT index,
                                                            D3D12_GPU_VIRTUAL_ADDRESS address)
{
    bundle_root_view(iface, FALSE, index, address);
}

static void STDMETHODCALLTYPE bundle_ia_set_index_buffer(ID3D12GraphicsCommandList10 *iface,
                                                         const D3D12_INDEX_BUFFER_VIEW *view)
{
    BundleCall *c = record(impl(iface), CALL_INDEX_BUFFER, sizeof(*view));
    if (c) {
        c->count = view != nullptr;
        if (view)
            memcpy(c + 1, view, sizeof(*view));
    }
}

static void STDMETHODCALLTYPE bundle_ia_set_vertex_buffers(ID3D12GraphicsCommandList10 *iface, UINT start, UINT count,
                                                           const D3D12_VERTEX_BUFFER_VIEW *views)
{
    count = D12_MIN(count, (UINT)D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT);
    BundleCall *c = record(impl(iface), CALL_VERTEX_BUFFERS, count * sizeof(*views));
    if (c) {
        c->count = count, c->a[0] = start, c->a[1] = views != nullptr;
        if (views)
            memcpy(c + 1, views, count * sizeof(*views));
    }
}

static void STDMETHODCALLTYPE bundle_execute_indirect(ID3D12GraphicsCommandList10 *iface,
                                                      ID3D12CommandSignature *signature, UINT max_count,
                                                      ID3D12Resource *arguments, UINT64 arguments_offset,
                                                      ID3D12Resource *count, UINT64 count_offset)
{
    BundleCall *c = record(impl(iface), CALL_EXECUTE_INDIRECT, 0);
    if (c)
        c->a[0] = (UINT64)(ULONG_PTR)signature, c->a[1] = max_count, c->a[2] = (UINT64)(ULONG_PTR)arguments,
        c->a[3] = arguments_offset, c->a[4] = (UINT64)(ULONG_PTR)count, c->a[5] = count_offset;
}

static HRESULT bundle_begin(D12CommandList *list, D12CommandAllocator *allocator, D12PipelineState *initial)
{
    if (list->recording || !allocator || allocator->type != D3D12_COMMAND_LIST_TYPE_BUNDLE)
        return E_INVALIDARG;
    if (list->allocator != allocator) {
        allocator->iface.lpVtbl->AddRef(&allocator->iface);
        if (list->allocator)
            list->allocator->iface.lpVtbl->Release(&list->allocator->iface);
        list->allocator = allocator;
    }
    list->recording = TRUE;
    list->error = S_OK;
    list->call_size = 0;
    list->bundle_initial = initial;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bundle_close(ID3D12GraphicsCommandList10 *iface)
{
    D12CommandList *list = impl(iface);
    if (!list->recording)
        return E_FAIL;
    list->recording = FALSE;
    return list->error;
}

static HRESULT STDMETHODCALLTYPE bundle_reset(ID3D12GraphicsCommandList10 *iface, ID3D12CommandAllocator *allocator,
                                              ID3D12PipelineState *initial)
{
    return bundle_begin(impl(iface), D12AllocatorFrom(allocator), D12PipelineFrom(initial));
}

static void STDMETHODCALLTYPE bundle_clear_state(ID3D12GraphicsCommandList10 *iface, ID3D12PipelineState *state) {}

/* replays the bundle's calls on the executing list. */
static void STDMETHODCALLTYPE list_execute_bundle(ID3D12GraphicsCommandList10 *iface, ID3D12GraphicsCommandList *bundle_iface)
{
    D12CommandList *list = impl(iface), *bundle = bundle_iface ? impl((ID3D12GraphicsCommandList10 *)bundle_iface) : nullptr;
    if (!list->open || !bundle || bundle->type != D3D12_COMMAND_LIST_TYPE_BUNDLE || bundle->recording)
        return;
    const struct ID3D12GraphicsCommandList10Vtbl *v = iface->lpVtbl;
    if (bundle->bundle_initial)
        v->SetPipelineState(iface, &bundle->bundle_initial->iface);
    for (SIZE_T at = 0; at + sizeof(BundleCall) <= bundle->call_size;) {
        const BundleCall *c = (const BundleCall *)(bundle->calls + at);
        const void *extra = c + 1;
        at += c->size;
        switch (c->op) {
        case CALL_DRAW: v->DrawInstanced(iface, (UINT)c->a[0], (UINT)c->a[1], (UINT)c->a[2], (UINT)c->a[3]); break;
        case CALL_DRAW_INDEXED:
            v->DrawIndexedInstanced(iface, (UINT)c->a[0], (UINT)c->a[1], (UINT)c->a[2], (INT)(UINT)c->a[3],
                                    (UINT)c->a[4]);
            break;
        case CALL_DISPATCH: v->Dispatch(iface, (UINT)c->a[0], (UINT)c->a[1], (UINT)c->a[2]); break;
        case CALL_DISPATCH_MESH: v->DispatchMesh(iface, (UINT)c->a[0], (UINT)c->a[1], (UINT)c->a[2]); break;
        case CALL_TOPOLOGY: v->IASetPrimitiveTopology(iface, (D3D_PRIMITIVE_TOPOLOGY)c->a[0]); break;
        case CALL_BLEND_FACTOR: v->OMSetBlendFactor(iface, c->count ? extra : nullptr); break;
        case CALL_STENCIL_REF: v->OMSetStencilRef(iface, (UINT)c->a[0]); break;
        case CALL_FRONT_BACK_STENCIL_REF: v->OMSetFrontAndBackStencilRef(iface, (UINT)c->a[0], (UINT)c->a[1]); break;
        case CALL_DEPTH_BOUNDS: {
            FLOAT b[2];
            memcpy(b, extra, 8);
            v->OMSetDepthBounds(iface, b[0], b[1]);
            break;
        }
        case CALL_DEPTH_BIAS: {
            FLOAT b[3];
            memcpy(b, extra, 12);
            v->RSSetDepthBias(iface, b[0], b[1], b[2]);
            break;
        }
        case CALL_STRIP_CUT: v->IASetIndexBufferStripCutValue(iface, (D3D12_INDEX_BUFFER_STRIP_CUT_VALUE)c->a[0]); break;
        case CALL_PIPELINE: v->SetPipelineState(iface, (ID3D12PipelineState *)(ULONG_PTR)c->a[0]); break;
        case CALL_HEAPS: {
            ID3D12DescriptorHeap *heaps[2] = {(ID3D12DescriptorHeap *)(ULONG_PTR)c->a[0],
                                              (ID3D12DescriptorHeap *)(ULONG_PTR)c->a[1]};
            v->SetDescriptorHeaps(iface, c->count, heaps);
            break;
        }
        case CALL_ROOT_SIGNATURE:
            if (c->compute)
                v->SetComputeRootSignature(iface, (ID3D12RootSignature *)(ULONG_PTR)c->a[0]);
            else
                v->SetGraphicsRootSignature(iface, (ID3D12RootSignature *)(ULONG_PTR)c->a[0]);
            break;
        case CALL_ROOT_TABLE: {
            D3D12_GPU_DESCRIPTOR_HANDLE base = {c->a[1]};
            if (c->compute)
                v->SetComputeRootDescriptorTable(iface, (UINT)c->a[0], base);
            else
                v->SetGraphicsRootDescriptorTable(iface, (UINT)c->a[0], base);
            break;
        }
        case CALL_ROOT_CONSTANTS:
            if (c->compute)
                v->SetComputeRoot32BitConstants(iface, (UINT)c->a[0], c->count, extra, (UINT)c->a[1]);
            else
                v->SetGraphicsRoot32BitConstants(iface, (UINT)c->a[0], c->count, extra, (UINT)c->a[1]);
            break;
        case CALL_ROOT_VIEW:
            if (c->compute)
                v->SetComputeRootConstantBufferView(iface, (UINT)c->a[0], c->a[1]);
            else
                v->SetGraphicsRootConstantBufferView(iface, (UINT)c->a[0], c->a[1]);
            break;
        case CALL_INDEX_BUFFER: v->IASetIndexBuffer(iface, c->count ? extra : nullptr); break;
        case CALL_VERTEX_BUFFERS: v->IASetVertexBuffers(iface, (UINT)c->a[0], c->count, c->a[1] ? extra : nullptr); break;
        case CALL_EXECUTE_INDIRECT:
            v->ExecuteIndirect(iface, (ID3D12CommandSignature *)(ULONG_PTR)c->a[0], (UINT)c->a[1],
                               (ID3D12Resource *)(ULONG_PTR)c->a[2], c->a[3], (ID3D12Resource *)(ULONG_PTR)c->a[4],
                               c->a[5]);
            break;
        case CALL_STATE_OBJECT: v->SetPipelineState1(iface, (ID3D12StateObject *)(ULONG_PTR)c->a[0]); break;
        case CALL_DISPATCH_RAYS: v->DispatchRays(iface, (const D3D12_DISPATCH_RAYS_DESC *)extra); break;
        default: break;
        }
    }
}

static const struct ID3D12GraphicsCommandList10Vtbl g_list_vtbl = {
    list_query,
    list_add_ref,
    list_release,
    list_get_private_data,
    list_set_private_data,
    list_set_private_data_interface,
    list_set_name,
    list_get_device,
    list_get_type,
    list_close,
    list_reset,
    list_clear_state,
    list_draw_instanced,
    list_draw_indexed_instanced,
    list_dispatch,
    list_copy_buffer_region,
    list_copy_texture_region,
    list_copy_resource,
    list_copy_tiles,
    list_resolve_subresource,
    list_ia_set_primitive_topology,
    list_rs_set_viewports,
    list_rs_set_scissor_rects,
    list_om_set_blend_factor,
    list_om_set_stencil_ref,
    list_set_pipeline_state,
    list_resource_barrier,
    list_execute_bundle,
    list_set_descriptor_heaps,
    list_set_compute_root_signature,
    list_set_graphics_root_signature,
    list_set_compute_root_descriptor_table,
    list_set_graphics_root_descriptor_table,
    list_set_compute_root_32bit_constant,
    list_set_graphics_root_32bit_constant,
    list_set_compute_root_32bit_constants,
    list_set_graphics_root_32bit_constants,
    list_set_compute_root_view,
    list_set_graphics_root_view,
    list_set_compute_root_view,
    list_set_graphics_root_view,
    list_set_compute_root_view,
    list_set_graphics_root_view,
    list_ia_set_index_buffer,
    list_ia_set_vertex_buffers,
    list_so_set_targets,
    list_om_set_render_targets,
    list_clear_depth_stencil_view,
    list_clear_render_target_view,
    list_clear_uav_uint,
    list_clear_uav_float,
    list_discard_resource,
    list_begin_query,
    list_end_query,
    list_resolve_query_data,
    list_set_predication,
    list_set_marker,
    list_set_marker,
    list_end_event,
    list_execute_indirect,
    list_atomic_copy_uint,
    list_atomic_copy_uint64,
    list_om_set_depth_bounds,
    list_set_sample_positions,
    list_resolve_subresource_region,
    list_set_view_instance_mask,
    list_write_buffer_immediate,
    list_set_protected_session,
    list_begin_render_pass,
    list_end_render_pass,
    list_meta_command,
    list_meta_command,
    list_build_acceleration_structure,
    list_emit_postbuild_info,
    list_copy_acceleration_structure,
    list_set_pipeline_state1,
    list_dispatch_rays,
    list_rs_set_shading_rate,
    list_rs_set_shading_rate_image,
    list_dispatch_mesh,
    list_barrier,
    list_om_set_front_and_back_stencil_ref,
    list_rs_set_depth_bias,
    list_ia_set_strip_cut,
    list_set_program,
    list_set_program,
};

/* a bundle records the calls it may hold. everything else does nothing, since no open list runs it. */
static const struct ID3D12GraphicsCommandList10Vtbl g_bundle_vtbl = {
    list_query,
    list_add_ref,
    list_release,
    list_get_private_data,
    list_set_private_data,
    list_set_private_data_interface,
    list_set_name,
    list_get_device,
    list_get_type,
    bundle_close,
    bundle_reset,
    bundle_clear_state,
    bundle_draw_instanced,
    bundle_draw_indexed_instanced,
    bundle_dispatch,
    list_copy_buffer_region,
    list_copy_texture_region,
    list_copy_resource,
    list_copy_tiles,
    list_resolve_subresource,
    bundle_ia_set_primitive_topology,
    list_rs_set_viewports,
    list_rs_set_scissor_rects,
    bundle_om_set_blend_factor,
    bundle_om_set_stencil_ref,
    bundle_set_pipeline_state,
    list_resource_barrier,
    list_execute_bundle,
    bundle_set_descriptor_heaps,
    bundle_set_compute_root_signature,
    bundle_set_graphics_root_signature,
    bundle_set_compute_root_descriptor_table,
    bundle_set_graphics_root_descriptor_table,
    bundle_set_compute_root_32bit_constant,
    bundle_set_graphics_root_32bit_constant,
    bundle_set_compute_root_32bit_constants,
    bundle_set_graphics_root_32bit_constants,
    bundle_set_compute_root_view,
    bundle_set_graphics_root_view,
    bundle_set_compute_root_view,
    bundle_set_graphics_root_view,
    bundle_set_compute_root_view,
    bundle_set_graphics_root_view,
    bundle_ia_set_index_buffer,
    bundle_ia_set_vertex_buffers,
    list_so_set_targets,
    list_om_set_render_targets,
    list_clear_depth_stencil_view,
    list_clear_render_target_view,
    list_clear_uav_uint,
    list_clear_uav_float,
    list_discard_resource,
    list_begin_query,
    list_end_query,
    list_resolve_query_data,
    list_set_predication,
    list_set_marker,
    list_set_marker,
    list_end_event,
    bundle_execute_indirect,
    list_atomic_copy_uint,
    list_atomic_copy_uint64,
    bundle_om_set_depth_bounds,
    list_set_sample_positions,
    list_resolve_subresource_region,
    list_set_view_instance_mask,
    list_write_buffer_immediate,
    list_set_protected_session,
    list_begin_render_pass,
    list_end_render_pass,
    list_meta_command,
    list_meta_command,
    list_build_acceleration_structure,
    list_emit_postbuild_info,
    list_copy_acceleration_structure,
    bundle_set_pipeline_state1,
    bundle_dispatch_rays,
    list_rs_set_shading_rate,
    list_rs_set_shading_rate_image,
    bundle_dispatch_mesh,
    list_barrier,
    bundle_om_set_front_and_back_stencil_ref,
    bundle_rs_set_depth_bias,
    bundle_ia_set_strip_cut,
    list_set_program,
    list_set_program,
};


HRESULT D12CreateCommandList(D12Device *device, D3D12_COMMAND_LIST_TYPE type, D12CommandAllocator *allocator,
                             D12PipelineState *initial, D12CommandList **out)
{
    if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_BUNDLE &&
        type != D3D12_COMMAND_LIST_TYPE_COMPUTE && type != D3D12_COMMAND_LIST_TYPE_COPY)
        return E_INVALIDARG;
    D12CommandList *list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*list));
    if (!list)
        return E_OUTOFMEMORY;
    BOOL bundle = type == D3D12_COMMAND_LIST_TYPE_BUNDLE;
    list->iface.lpVtbl = bundle ? &g_bundle_vtbl : &g_list_vtbl;
    list->child.refs = 1;
    list->child.device = device;
    list->type = type;
    if (allocator) {
        HRESULT hr = bundle ? bundle_begin(list, allocator, initial) : begin(list, allocator, initial);
        if (FAILED(hr)) {
            if (list->allocator)
                list->allocator->iface.lpVtbl->Release(&list->allocator->iface);
            HeapFree(GetProcessHeap(), 0, list);
            return hr;
        }
    }
    D12ChildInit(&list->child, device);
    *out = list;
    return S_OK;
}
