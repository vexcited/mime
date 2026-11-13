/*
 * the immediate context. bindings are kept as direct3d sees them, holding a
 * reference on what is bound. a draw turns what changed into vulkan commands:
 * shaders bound as shader objects, fixed function state as dynamic state, and
 * per stage a table of the heap indices its shader reads, uploaded to the
 * ring when it changes.
 *
 * rendering to the bound targets stays open across draws and closes for
 * anything that cannot happen inside it. closing it, a copy or a dispatch
 * leaves a full barrier for whatever comes next.
 */
#include "d3d11_private.h"

#define VTBL_FN(f) ((void *)(f))

static D3dContext *impl(ID3D11DeviceContext4 *iface) { return CONTAINING_RECORD(iface, D3dContext, iface); }

D3dContext *D3dContextFrom(ID3D11DeviceContext *iface)
{
    return iface ? CONTAINING_RECORD((void *)iface, D3dContext, iface) : nullptr;
}

/* context_copy.c. */
void D3dCtxCopySubresourceRegion(D3dContext *c, ID3D11Resource *dst, UINT dst_sub, UINT x, UINT y, UINT z,
                                 ID3D11Resource *src, UINT src_sub, const D3D11_BOX *box, UINT flags);
void D3dCtxUpdateSubresource(D3dContext *c, ID3D11Resource *dst, UINT dst_sub, const D3D11_BOX *box,
                             const void *data, UINT row_pitch, UINT depth_pitch, UINT flags);
void D3dCtxCopyStructureCount(D3dContext *c, ID3D11Buffer *dst, UINT offset, ID3D11UnorderedAccessView *src);
void D3dCtxClearRenderTargetView(D3dContext *c, ID3D11RenderTargetView *view, const FLOAT color[4]);
void D3dCtxClearUnorderedAccessViewUint(D3dContext *c, ID3D11UnorderedAccessView *view, const UINT values[4]);
void D3dCtxClearUnorderedAccessViewFloat(D3dContext *c, ID3D11UnorderedAccessView *view, const FLOAT values[4]);
void D3dCtxClearDepthStencilView(D3dContext *c, ID3D11DepthStencilView *view, UINT flags, FLOAT depth,
                                 UINT8 stencil);
void D3dCtxClearView(D3dContext *c, ID3D11View *view, const FLOAT color[4], const D3D11_RECT *rects, UINT count);
void D3dCtxGenerateMips(D3dContext *c, ID3D11ShaderResourceView *view);
void D3dCtxResolveSubresource(D3dContext *c, ID3D11Resource *dst, UINT dst_sub, ID3D11Resource *src, UINT src_sub,
                              DXGI_FORMAT format);
HRESULT D3dCtxMap(D3dContext *c, ID3D11Resource *resource, UINT sub, D3D11_MAP type, UINT flags,
                  D3D11_MAPPED_SUBRESOURCE *mapped);
void D3dCtxUnmap(D3dContext *c, ID3D11Resource *resource, UINT sub);

/* ------------------------------------------------------------------ references on what is bound */

static void ref_resource(D3dResource **slot, D3dResource *r)
{
    if (*slot == r)
        return;
    if (r)
        D3dResourceAddRef(r);
    if (*slot)
        D3dResourceRelease(*slot);
    *slot = r;
}

/* on a deferred context, the command list being recorded keeps the reference instead. */
static void drop(D3dContext *c, IUnknown *o)
{
    if (c->type == D3D11_DEVICE_CONTEXT_DEFERRED) {
        if (c->list_held_count == c->list_held_capacity) {
            UINT capacity = c->list_held_capacity ? c->list_held_capacity * 2 : 64;
            IUnknown **list = c->list_held ? HeapReAlloc(GetProcessHeap(), 0, c->list_held, capacity * sizeof(*list))
                                           : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
            if (list) {
                c->list_held = list;
                c->list_held_capacity = capacity;
            }
        }
        if (c->list_held_count < c->list_held_capacity) {
            c->list_held[c->list_held_count++] = o;
            return;
        }
    }
    o->lpVtbl->Release(o);
}

static void hold(D3dContext *c, IUnknown *o)
{
    if (!o)
        return;
    o->lpVtbl->AddRef(o);
    drop(c, o);
}

#define REF_OBJECT(type, field_iface)                                                                                  \
    static void ref_##type(D3dContext *c, type **slot, type *o)                                                        \
    {                                                                                                                  \
        if (*slot == o)                                                                                                \
            return;                                                                                                    \
        if (o)                                                                                                         \
            o->field_iface.lpVtbl->AddRef((void *)&o->field_iface);                                                    \
        if (*slot)                                                                                                     \
            drop(c, (IUnknown *)&(*slot)->field_iface);                                                                \
        *slot = o;                                                                                                     \
    }

REF_OBJECT(D3dShader, iface.vs)
REF_OBJECT(D3dInputLayout, iface)
REF_OBJECT(D3dBlendState, iface)
REF_OBJECT(D3dRasterizerState, iface)
REF_OBJECT(D3dDepthStencilState, iface)
REF_OBJECT(D3dSamplerState, iface)

static void ref_view(D3dContext *c, D3dView **slot, D3dView *v)
{
    if (*slot == v)
        return;
    if (v)
        InterlockedIncrement(&v->child.refs);
    if (*slot)
        drop(c, (IUnknown *)&(*slot)->iface);
    *slot = v;
}

static void update_vb_high(D3dContext *c)
{
    UINT high = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
    while (high && !c->vb[high - 1])
        high--;
    c->vb_high = high;
}

/* the command list's execution tells when the gpu is done with the resource. */
void D3dContextUseList(D3dContext *c, D3dResource *r)
{
    if (r->list_mark == c->list_id)
        return;
    if (c->list_resource_count == c->list_resource_capacity) {
        UINT capacity = c->list_resource_capacity ? c->list_resource_capacity * 2 : 64;
        D3dResource **list = c->list_resources
                                 ? HeapReAlloc(GetProcessHeap(), 0, c->list_resources, capacity * sizeof(*list))
                                 : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
        if (!list)
            return;
        c->list_resources = list;
        c->list_resource_capacity = capacity;
    }
    r->list_mark = c->list_id;
    D3dResourceAddRef(r);
    c->list_resources[c->list_resource_count++] = r;
}

/* the immediate context allocates from the ring, a deferred one from its own chunks. */
BOOL D3dContextRingAlloc(D3dContext *c, VkDeviceSize size, VkDeviceSize align, D3dRingSlice *out)
{
    D3dDevice *device = c->child.device;
    if (c->type == D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return D3dRingAlloc(device, size, align, out);
    if (c->list_chunk_count && D3dRingChunkAlloc(c->list_chunks[c->list_chunk_count - 1], size, align, out))
        return TRUE;
    if (c->list_chunk_count == c->list_chunk_capacity) {
        UINT capacity = c->list_chunk_capacity ? c->list_chunk_capacity * 2 : 8;
        D3dRingChunk **list = c->list_chunks ? HeapReAlloc(GetProcessHeap(), 0, c->list_chunks, capacity * sizeof(*list))
                                             : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
        if (!list)
            return FALSE;
        c->list_chunks = list;
        c->list_chunk_capacity = capacity;
    }
    D3dRingChunk *chunk = D3dRingChunkNew(device, size + align);
    if (!chunk)
        return FALSE;
    c->list_chunks[c->list_chunk_count++] = chunk;
    return D3dRingChunkAlloc(chunk, size, align, out);
}

const D3dRingSlice *D3dContextListRange(const D3dContext *c, const D3dResource *r)
{
    if (c->type == D3D11_DEVICE_CONTEXT_IMMEDIATE || r->usage != D3D11_USAGE_DYNAMIC)
        return nullptr;
    for (UINT i = 0; i < c->list_range_count; i++)
        if (c->list_ranges[i].resource == r)
            return &c->list_ranges[i].slice;
    return nullptr;
}

/* a deferred discard gives the buffer new bytes owned by the list, which later commands read. */
const D3dRingSlice *D3dContextListDiscard(D3dContext *c, D3dResource *r)
{
    D3dRingSlice slice;
    if (!D3dContextRingAlloc(c, r->buf.size, 256, &slice))
        return nullptr;
    D3dContextUse(c, r);
    c->ia_dirty = TRUE;
    for (UINT i = 0; i < D3D_STAGES; i++)
        c->stages[i].table_valid = FALSE;
    for (UINT i = 0; i < c->list_range_count; i++)
        if (c->list_ranges[i].resource == r) {
            c->list_ranges[i].slice = slice;
            return &c->list_ranges[i].slice;
        }
    if (c->list_range_count == c->list_range_capacity) {
        UINT capacity = c->list_range_capacity ? c->list_range_capacity * 2 : 16;
        struct D3dListRange *list =
            c->list_ranges ? HeapReAlloc(GetProcessHeap(), 0, c->list_ranges, capacity * sizeof(*list))
                           : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
        if (!list)
            return nullptr;
        c->list_ranges = list;
        c->list_range_capacity = capacity;
    }
    c->list_ranges[c->list_range_count].resource = r;
    c->list_ranges[c->list_range_count].slice = slice;
    return &c->list_ranges[c->list_range_count++].slice;
}

void D3dContextBufferRange(const D3dContext *c, const D3dResource *r, VkBuffer *buffer, VkDeviceSize *offset)
{
    const D3dRingSlice *slice = D3dContextListRange(c, r);
    *buffer = slice ? slice->buffer : r->buf.buffer;
    *offset = slice ? slice->offset : r->buf.offset;
}

static volatile LONG64 g_list_ids;

/* ------------------------------------------------------------------ recording */

/* records into a command buffer from a pool of its own, which the command list takes over. */
static BOOL begin_list(D3dContext *c)
{
    D3dDevice *device = c->child.device;
    D3dVk *vk = &device->vk;
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = device->queue_family;
    if (vk->vkCreateCommandPool(vk->device, &pool, nullptr, &c->list_pool) != VK_SUCCESS)
        return FALSE;
    VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = c->list_pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    if (vk->vkAllocateCommandBuffers(vk->device, &alloc, &c->list_cmd.buffer) != VK_SUCCESS)
        return FALSE;
    /* a list may be executed again while it still runs. */
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    vk->vkBeginCommandBuffer(c->list_cmd.buffer, &begin);
    c->list_cmd.serial = UINT64_MAX;
    c->list_id = (UINT64)InterlockedIncrement64(&g_list_ids);
    c->cmd = &c->list_cmd;
    D3dBarrier(c->cmd->buffer, device);
    return TRUE;
}

static void begin_commands(D3dContext *c)
{
    if (c->cmd)
        return;
    D3dDevice *device = c->child.device;
    D3dVk *vk = &device->vk;
    if (c->type == D3D11_DEVICE_CONTEXT_DEFERRED) {
        if (!begin_list(c))
            return;
        goto fresh;
    }
    UINT64 completed = device->completed;
    D3dCommandBuffer *cmd = nullptr;
    for (UINT pass = 0; pass < 2 && !cmd; pass++) {
        for (UINT i = 0; i < c->buffer_count && !cmd; i++)
            if (c->buffers[i].serial <= completed)
                cmd = &c->buffers[i];
        if (!cmd && !pass && c->buffer_count >= 4)
            completed = D3dCompleted(device);
        else
            break;
    }
    if (!cmd) {
        D3dCommandBuffer *list = c->buffers ? HeapReAlloc(GetProcessHeap(), 0, c->buffers,
                                                          (c->buffer_count + 1) * sizeof(*list))
                                            : HeapAlloc(GetProcessHeap(), 0, sizeof(*list));
        if (!list)
            return;
        c->buffers = list;
        cmd = &c->buffers[c->buffer_count++];
        VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = c->pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        if (vk->vkAllocateCommandBuffers(vk->device, &alloc, &cmd->buffer) != VK_SUCCESS) {
            c->buffer_count--;
            return;
        }
    } else {
        vk->vkResetCommandBuffer(cmd->buffer, 0);
    }
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk->vkBeginCommandBuffer(cmd->buffer, &begin);
    cmd->serial = UINT64_MAX;
    c->cmd = cmd;
fresh:
    /* a new command buffer has no state: set everything and upload the tables again. */
    c->state_dirty = TRUE;
    c->shaders_dirty = TRUE;
    c->ia_dirty = c->vertex_input_dirty = TRUE;
    c->rs_dirty = c->om_dirty = c->viewports_dirty = TRUE;
    c->heap_bound[0] = c->heap_bound[1] = FALSE;
    c->tables_bound[0] = c->tables_bound[1] = FALSE;
    c->compute_bound = FALSE;
    c->table_chunk = nullptr;
    for (UINT i = 0; i < D3D_STAGES; i++)
        c->stages[i].table_valid = FALSE;
}

static void end_stream_output(D3dContext *c, VkCommandBuffer cmd);

static void end_rendering(D3dContext *c)
{
    if (!c->rendering)
        return;
    D3dQueriesClose(c, c->cmd->buffer);
    if (c->xfb_active)
        end_stream_output(c, c->cmd->buffer);
    c->child.device->vk.vkCmdEndRendering(c->cmd->buffer);
    c->rendering = FALSE;
    c->pending_barrier = TRUE;
}

VkCommandBuffer D3dContextCommands(D3dContext *c, BOOL transfer)
{
    begin_commands(c);
    if (!c->cmd)
        return VK_NULL_HANDLE;
    if (transfer)
        end_rendering(c);
    if (c->pending_barrier && !c->rendering) {
        D3dBarrier(c->cmd->buffer, c->child.device);
        c->pending_barrier = FALSE;
    }
    return c->cmd->buffer;
}

/* returns the serial the submission signals. */
UINT64 D3dContextSubmit(D3dContext *c, const VkSemaphoreSubmitInfo *wait, const VkSemaphoreSubmitInfo *signal)
{
    D3dDevice *device = c->child.device;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (c->cmd) {
        end_rendering(c);
        D3dQueriesClose(c, c->cmd->buffer);
        /*
         * only the processor reads the results, once the submission is done, so their copies wait for its end: one
         * between two render pass instances keeps a tile based gpu from drawing them as one.
         */
        D3dQueriesCopy(c, c->cmd->buffer);
        D3dBarrier(c->cmd->buffer, device);
        c->pending_barrier = FALSE;
        device->vk.vkEndCommandBuffer(c->cmd->buffer);
        cmd = c->cmd->buffer;
    }
    UINT64 serial = D3dSubmit(device, cmd, wait, signal);
    if (c->cmd) {
        c->cmd->serial = serial;
        c->cmd = nullptr;
    }
    return serial;
}

void D3dContextFlush(D3dContext *c)
{
    if (c->cmd && c->type == D3D11_DEVICE_CONTEXT_IMMEDIATE)
        D3dContextSubmit(c, nullptr, nullptr);
}

/* ------------------------------------------------------------------ hazards */

/* like direct3d, binding a resource for output unbinds it from shader inputs. */
static void unbind_inputs(D3dContext *c, D3dResource *r)
{
    if (!r)
        return;
    for (UINT s = 0; s < D3D_STAGES; s++)
        for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
            if (c->stages[s].srv[i] && c->stages[s].srv[i]->resource == r) {
                ref_view(c, &c->stages[s].srv[i], nullptr);
                c->stages[s].table_valid = FALSE;
            }
    if (r->dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
        return;
    for (UINT s = 0; s < D3D_STAGES; s++)
        for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
            if (c->stages[s].cb[i] == r) {
                ref_resource(&c->stages[s].cb[i], nullptr);
                c->stages[s].table_valid = FALSE;
            }
    for (UINT i = 0; i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; i++)
        if (c->vb[i] == r) {
            ref_resource(&c->vb[i], nullptr);
            c->ia_dirty = TRUE;
            update_vb_high(c);
        }
    if (c->ib == r) {
        ref_resource(&c->ib, nullptr);
        c->ia_dirty = TRUE;
    }
}

/* rebuilt after output bindings change. */
static void gather_outputs(D3dContext *c)
{
    UINT n = 0;
    for (UINT i = 0; i < c->rtv_count; i++)
        if (c->rtv[i])
            c->outputs[n++] = c->rtv[i]->resource;
    if (c->dsv)
        c->outputs[n++] = c->dsv->resource;
    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++) {
        if (c->om_uav[i])
            c->outputs[n++] = c->om_uav[i]->resource;
        if (c->stages[D3D_CS].uav[i])
            c->outputs[n++] = c->stages[D3D_CS].uav[i]->resource;
    }
    for (UINT i = 0; i < D3D11_SO_BUFFER_SLOT_COUNT; i++)
        if (c->so[i])
            c->outputs[n++] = c->so[i];
    c->output_count = n;
    c->outputs_dirty = FALSE;
}

/* a resource bound for output is refused as a shader input. */
static BOOL bound_as_output(D3dContext *c, D3dResource *r)
{
    if (!r)
        return FALSE;
    if (c->outputs_dirty)
        gather_outputs(c);
    for (UINT i = 0; i < c->output_count; i++)
        if (c->outputs[i] == r)
            return TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ binding methods */

static void set_constant_buffers(D3dContext *c, D3dStage stage, UINT slot, UINT count,
                                 ID3D11Buffer *const *buffers, const UINT *first, const UINT *num)
{
    if (slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
        return;
    count = D3D_MIN(count, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - slot);
    D3dStageState *s = &c->stages[stage];
    for (UINT i = 0; i < count; i++) {
        D3dResource *r = buffers ? D3dResourceFrom((ID3D11Resource *)buffers[i]) : nullptr;
        ref_resource(&s->cb[slot + i], bound_as_output(c, r) ? nullptr : r);
        s->cb_first[slot + i] = first ? first[i] : 0;
        s->cb_count[slot + i] = num ? num[i] : D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT;
    }
    s->table_valid = FALSE;
}

static void set_shader_resources(D3dContext *c, D3dStage stage, UINT slot, UINT count,
                                 ID3D11ShaderResourceView *const *views)
{
    if (slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
        return;
    count = D3D_MIN(count, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - slot);
    D3dStageState *s = &c->stages[stage];
    for (UINT i = 0; i < count; i++) {
        D3dView *v = views ? D3dViewFrom(views[i], D3D_VIEW_SRV) : nullptr;
        if (v && bound_as_output(c, v->resource))
            v = nullptr;
        ref_view(c, &s->srv[slot + i], v);
    }
    s->table_valid = FALSE;
}

static void set_samplers(D3dContext *c, D3dStage stage, UINT slot, UINT count, ID3D11SamplerState *const *samplers)
{
    if (slot >= D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT)
        return;
    count = D3D_MIN(count, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - slot);
    D3dStageState *s = &c->stages[stage];
    for (UINT i = 0; i < count; i++)
        ref_D3dSamplerState(c, &s->sampler[slot + i], samplers ? D3dSamplerFrom(samplers[i]) : nullptr);
    s->table_valid = FALSE;
}

static void set_shader(D3dContext *c, D3dStage stage, void *iface)
{
    D3dShader *shader = D3dShaderFrom(iface, stage);
    if (c->stages[stage].shader == shader)
        return;
    /* transform feedback ends with the geometry shader it began for. */
    if (stage == D3D_GS && c->xfb_active)
        end_rendering(c);
    ref_D3dShader(c, &c->stages[stage].shader, shader);
    c->stages[stage].table_valid = FALSE;
    c->shaders_dirty = TRUE;
    if (stage == D3D_HS || stage == D3D_GS)
        c->rs_dirty = TRUE;
}

static void get_constant_buffers(D3dContext *c, D3dStage stage, UINT slot, UINT count, ID3D11Buffer **buffers,
                                 UINT *first, UINT *num)
{
    for (UINT i = 0; i < count; i++) {
        UINT k = slot + i;
        D3dResource *r = k < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT ? c->stages[stage].cb[k] : nullptr;
        if (buffers) {
            buffers[i] = r ? &r->u.buffer : nullptr;
            if (r)
                D3dResourceAddRef(r);
        }
        if (first)
            first[i] = r ? c->stages[stage].cb_first[k] : 0;
        if (num)
            num[i] = r ? c->stages[stage].cb_count[k] : 0;
    }
}

static void get_shader_resources(D3dContext *c, D3dStage stage, UINT slot, UINT count,
                                 ID3D11ShaderResourceView **views)
{
    for (UINT i = 0; i < count; i++) {
        D3dView *v = slot + i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT ? c->stages[stage].srv[slot + i] : nullptr;
        views[i] = v ? (ID3D11ShaderResourceView *)&v->iface : nullptr;
        if (v)
            InterlockedIncrement(&v->child.refs);
    }
}

static void get_samplers(D3dContext *c, D3dStage stage, UINT slot, UINT count, ID3D11SamplerState **samplers)
{
    for (UINT i = 0; i < count; i++) {
        D3dSamplerState *s = slot + i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT ? c->stages[stage].sampler[slot + i]
                                                                               : nullptr;
        samplers[i] = s ? (ID3D11SamplerState *)&s->iface : nullptr;
        if (s)
            s->iface.lpVtbl->AddRef((void *)&s->iface);
    }
}

static void get_shader(D3dContext *c, D3dStage stage, void **shader, ID3D11ClassInstance **instances, UINT *count)
{
    D3dShader *s = c->stages[stage].shader;
    if (shader) {
        *shader = s ? &s->iface : nullptr;
        if (s)
            InterlockedIncrement(&s->child.refs);
    }
    if (count)
        *count = 0;
}

/* per stage methods, identical but for the stage. */
#define STAGE_METHODS(prefix, stage, type)                                                                             \
    static void STDMETHODCALLTYPE prefix##_set_constant_buffers(ID3D11DeviceContext4 *iface, UINT slot, UINT count,    \
                                                                ID3D11Buffer *const *buffers)                          \
    {                                                                                                                  \
        D3D_SYNC(impl(iface));                                                                                         \
        set_constant_buffers(impl(iface), stage, slot, count, buffers, nullptr, nullptr);                              \
    }                                                                                                                  \
    static void STDMETHODCALLTYPE prefix##_set_constant_buffers1(ID3D11DeviceContext4 *iface, UINT slot, UINT count,   \
                                                                 ID3D11Buffer *const *buffers, const UINT *first,      \
                                                                 const UINT *num)                                      \
    {                                                                                                                  \
        D3D_SYNC(impl(iface));                                                                                         \
        set_constant_buffers(impl(iface), stage, slot, count, buffers, first, num);                                    \
    }                                                                                                                  \
    static void STDMETHODCALLTYPE prefix##_set_shader_resources(ID3D11DeviceContext4 *iface, UINT slot, UINT count,    \
                                                                ID3D11ShaderResourceView *const *views)                \
    {                                                                                                                  \
        D3D_SYNC(impl(iface));                                                                                         \
        set_shader_resources(impl(iface), stage, slot, count, views);                                                  \
    }                                                                                                                  \
    static void STDMETHODCALLTYPE prefix##_set_samplers(ID3D11DeviceContext4 *iface, UINT slot, UINT count,            \
                                                        ID3D11SamplerState *const *samplers)                           \
    {                                                                                                                  \
        D3D_SYNC(impl(iface));                                                                                         \
        set_samplers(impl(iface), stage, slot, count, samplers);                                                       \
    }                                                                                                                  \
    static void STDMETHODCALLTYPE prefix##_set_shader(ID3D11DeviceContext4 *iface, type *shader,                       \
                                                      ID3D11ClassInstance *const *instances, UINT count)               \
    {                                                                                                                  \
        D3D_SYNC(impl(iface));                                                                                         \
        set_shader(impl(iface), stage, shader);                                                                        \
    }                                                                                                                  \
    static void STDMETHODCALLTYPE prefix##_get_constant_buffers(ID3D11DeviceContext4 *iface, UINT slot, UINT count,    \
                                                                ID3D11Buffer **buffers)                                \
    {                                                                                                                  \
        D3D_SYNC(impl(iface));                                                                                         \
        get_constant_buffers(impl(iface), stage, slot, count, buffers, nullptr, nullptr);                              \
    }                                                                                                                  \
    static void STDMETHODCALLTYPE prefix##_get_constant_buffers1(ID3D11DeviceContext4 *iface, UINT slot, UINT count,   \
                                                                 ID3D11Buffer **buffers, UINT *first, UINT *num)       \
    {                                                                                                                  \
        D3D_SYNC(impl(iface));                                                                                         \
        get_constant_buffers(impl(iface), stage, slot, count, buffers, first, num);                                    \
    }                                                                                                                  \
    static void STDMETHODCALLTYPE prefix##_get_shader_resources(ID3D11DeviceContext4 *iface, UINT slot, UINT count,    \
                                                                ID3D11ShaderResourceView **views)                      \
    {                                                                                                                  \
        D3D_SYNC(impl(iface));                                                                                         \
        get_shader_resources(impl(iface), stage, slot, count, views);                                                  \
    }                                                                                                                  \
    static void STDMETHODCALLTYPE prefix##_get_samplers(ID3D11DeviceContext4 *iface, UINT slot, UINT count,            \
                                                        ID3D11SamplerState **samplers)                                 \
    {                                                                                                                  \
        D3D_SYNC(impl(iface));                                                                                         \
        get_samplers(impl(iface), stage, slot, count, samplers);                                                       \
    }                                                                                                                  \
    static void STDMETHODCALLTYPE prefix##_get_shader(ID3D11DeviceContext4 *iface, type **shader,                      \
                                                      ID3D11ClassInstance **instances, UINT *count)                    \
    {                                                                                                                  \
        D3D_SYNC(impl(iface));                                                                                         \
        get_shader(impl(iface), stage, (void **)shader, instances, count);                                             \
    }

STAGE_METHODS(vs, D3D_VS, ID3D11VertexShader)
STAGE_METHODS(hs, D3D_HS, ID3D11HullShader)
STAGE_METHODS(ds, D3D_DS, ID3D11DomainShader)
STAGE_METHODS(gs, D3D_GS, ID3D11GeometryShader)
STAGE_METHODS(ps, D3D_PS, ID3D11PixelShader)
STAGE_METHODS(cs, D3D_CS, ID3D11ComputeShader)

/* ------------------------------------------------------------------ the input assembler */

static void STDMETHODCALLTYPE ia_set_input_layout(ID3D11DeviceContext4 *iface, ID3D11InputLayout *layout)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    D3dInputLayout *l = D3dInputLayoutFrom(layout);
    if (c->layout != l) {
        ref_D3dInputLayout(c, &c->layout, l);
        c->vertex_input_dirty = TRUE;
    }
}

static void STDMETHODCALLTYPE ia_set_vertex_buffers(ID3D11DeviceContext4 *iface, UINT slot, UINT count,
                                                    ID3D11Buffer *const *buffers, const UINT *strides,
                                                    const UINT *offsets)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (slot >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
        return;
    count = D3D_MIN(count, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - slot);
    for (UINT i = 0; i < count; i++) {
        UINT k = slot + i;
        D3dResource *r = buffers ? D3dResourceFrom((ID3D11Resource *)buffers[i]) : nullptr;
        if (bound_as_output(c, r))
            r = nullptr;
        UINT stride = strides ? strides[i] : 0;
        if (c->vb_stride[k] != stride)
            c->vertex_input_dirty = TRUE;
        ref_resource(&c->vb[k], r);
        c->vb_stride[k] = stride;
        c->vb_offset[k] = offsets ? offsets[i] : 0;
        c->vb_version[k] = ~0u;
    }
    update_vb_high(c);
    c->ia_dirty = TRUE;
}

static void STDMETHODCALLTYPE ia_set_index_buffer(ID3D11DeviceContext4 *iface, ID3D11Buffer *buffer,
                                                  DXGI_FORMAT format, UINT offset)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    D3dResource *r = D3dResourceFrom((ID3D11Resource *)buffer);
    ref_resource(&c->ib, bound_as_output(c, r) ? nullptr : r);
    c->ib_format = format;
    c->ib_offset = offset;
    c->ib_version = ~0u;
    c->ia_dirty = TRUE;
}

static void STDMETHODCALLTYPE ia_set_primitive_topology(ID3D11DeviceContext4 *iface,
                                                        D3D11_PRIMITIVE_TOPOLOGY topology)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (c->topology != topology) {
        c->topology = topology;
        c->rs_dirty = TRUE;
    }
}

static void STDMETHODCALLTYPE ia_get_input_layout(ID3D11DeviceContext4 *iface, ID3D11InputLayout **layout)
{
    D3D_SYNC(impl(iface));
    D3dInputLayout *l = impl(iface)->layout;
    *layout = l ? &l->iface : nullptr;
    if (l)
        InterlockedIncrement(&l->child.refs);
}

static void STDMETHODCALLTYPE ia_get_vertex_buffers(ID3D11DeviceContext4 *iface, UINT slot, UINT count,
                                                    ID3D11Buffer **buffers, UINT *strides, UINT *offsets)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    for (UINT i = 0; i < count; i++) {
        UINT k = slot + i;
        BOOL valid = k < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        D3dResource *r = valid ? c->vb[k] : nullptr;
        if (buffers) {
            buffers[i] = r ? &r->u.buffer : nullptr;
            if (r)
                D3dResourceAddRef(r);
        }
        if (strides)
            strides[i] = valid ? c->vb_stride[k] : 0;
        if (offsets)
            offsets[i] = valid ? c->vb_offset[k] : 0;
    }
}

static void STDMETHODCALLTYPE ia_get_index_buffer(ID3D11DeviceContext4 *iface, ID3D11Buffer **buffer,
                                                  DXGI_FORMAT *format, UINT *offset)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (buffer) {
        *buffer = c->ib ? &c->ib->u.buffer : nullptr;
        if (c->ib)
            D3dResourceAddRef(c->ib);
    }
    if (format)
        *format = c->ib_format;
    if (offset)
        *offset = c->ib_offset;
}

static void STDMETHODCALLTYPE ia_get_primitive_topology(ID3D11DeviceContext4 *iface,
                                                        D3D11_PRIMITIVE_TOPOLOGY *topology)
{
    D3D_SYNC(impl(iface));
    *topology = impl(iface)->topology;
}

/* ------------------------------------------------------------------ the rasterizer and output merger */

static void STDMETHODCALLTYPE rs_set_state(ID3D11DeviceContext4 *iface, ID3D11RasterizerState *state)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    D3dRasterizerState *s = D3dRasterizerFrom(state);
    if (c->rasterizer != s) {
        ref_D3dRasterizerState(c, &c->rasterizer, s);
        c->rs_dirty = c->viewports_dirty = TRUE;
    }
}

static void STDMETHODCALLTYPE rs_set_viewports(ID3D11DeviceContext4 *iface, UINT count,
                                               const D3D11_VIEWPORT *viewports)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    count = D3D_MIN(count, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    memcpy(c->viewports, viewports, count * sizeof(*viewports));
    c->viewport_count = count;
    c->viewports_dirty = TRUE;
}

static void STDMETHODCALLTYPE rs_set_scissor_rects(ID3D11DeviceContext4 *iface, UINT count, const D3D11_RECT *rects)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    count = D3D_MIN(count, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    memcpy(c->scissors, rects, count * sizeof(*rects));
    c->scissor_count = count;
    c->viewports_dirty = TRUE;
}

static void STDMETHODCALLTYPE rs_get_state(ID3D11DeviceContext4 *iface, ID3D11RasterizerState **state)
{
    D3D_SYNC(impl(iface));
    D3dRasterizerState *s = impl(iface)->rasterizer;
    *state = s ? (ID3D11RasterizerState *)&s->iface : nullptr;
    if (s)
        InterlockedIncrement(&s->child.refs);
}

static void STDMETHODCALLTYPE rs_get_viewports(ID3D11DeviceContext4 *iface, UINT *count, D3D11_VIEWPORT *viewports)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (viewports)
        for (UINT i = 0; i < *count; i++)
            viewports[i] = i < c->viewport_count ? c->viewports[i] : (D3D11_VIEWPORT){0};
    else
        *count = c->viewport_count;
}

static void STDMETHODCALLTYPE rs_get_scissor_rects(ID3D11DeviceContext4 *iface, UINT *count, D3D11_RECT *rects)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (rects)
        for (UINT i = 0; i < *count; i++)
            rects[i] = i < c->scissor_count ? c->scissors[i] : (D3D11_RECT){0};
    else
        *count = c->scissor_count;
}

static void STDMETHODCALLTYPE om_set_blend_state(ID3D11DeviceContext4 *iface, ID3D11BlendState *state,
                                                 const FLOAT factor[4], UINT mask)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    ref_D3dBlendState(c, &c->blend, D3dBlendFrom(state));
    static const FLOAT ones[4] = {1, 1, 1, 1};
    memcpy(c->blend_factor, factor ? factor : ones, sizeof(c->blend_factor));
    c->sample_mask = mask;
    c->om_dirty = TRUE;
}

static void STDMETHODCALLTYPE om_set_depth_stencil_state(ID3D11DeviceContext4 *iface, ID3D11DepthStencilState *state,
                                                         UINT ref)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    ref_D3dDepthStencilState(c, &c->depth_stencil, D3dDepthStencilFrom(state));
    c->stencil_ref = ref;
    c->om_dirty = TRUE;
}

static void set_render_targets(D3dContext *c, UINT count, ID3D11RenderTargetView *const *views,
                               ID3D11DepthStencilView *depth)
{
    count = D3D_MIN(count, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);
    D3dView *d = D3dViewFrom(depth, D3D_VIEW_DSV);
    /*
     * the same targets again change nothing, and the rendering to them goes on. without targets, the rendering takes
     * the extent of the viewport, so it starts again.
     */
    BOOL same = c->rtv_count == count && c->dsv == d;
    BOOL bound = d != nullptr;
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
        D3dView *v = i < count && views ? D3dViewFrom(views[i], D3D_VIEW_RTV) : nullptr;
        if (v) {
            unbind_inputs(c, v->resource);
            bound = TRUE;
        }
        same = same && c->rtv[i] == v;
        ref_view(c, &c->rtv[i], v);
    }
    c->rtv_count = count;
    if (d)
        unbind_inputs(c, d->resource);
    ref_view(c, &c->dsv, d);
    if (same && bound)
        return;
    c->targets_dirty = TRUE;
    c->outputs_dirty = TRUE;
}

/*
 * what the context has bound of a resource is let go. a swap chain does this before it resizes: windows counts the
 * references of the program only, so a view of the back buffer that the program released but left bound does not
 * keep the buffers.
 */
void D3dContextUnbindResource(D3dContext *c, D3dResource *r)
{
    D3D_SYNC(c);
    unbind_inputs(c, r);
    BOOL outputs = FALSE;
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
        if (c->rtv[i] && c->rtv[i]->resource == r) {
            ref_view(c, &c->rtv[i], nullptr);
            outputs = TRUE;
        }
    if (c->dsv && c->dsv->resource == r) {
        ref_view(c, &c->dsv, nullptr);
        outputs = TRUE;
    }
    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++) {
        if (c->om_uav[i] && c->om_uav[i]->resource == r) {
            ref_view(c, &c->om_uav[i], nullptr);
            c->stages[D3D_PS].table_valid = FALSE;
            outputs = TRUE;
        }
        for (UINT stage = 0; stage < D3D_STAGES; stage++)
            if (c->stages[stage].uav[i] && c->stages[stage].uav[i]->resource == r) {
                ref_view(c, &c->stages[stage].uav[i], nullptr);
                c->stages[stage].table_valid = FALSE;
            }
    }
    if (outputs) {
        c->targets_dirty = TRUE;
        c->outputs_dirty = TRUE;
    }
}

static void set_om_uavs(D3dContext *c, UINT start, UINT count, ID3D11UnorderedAccessView *const *views,
                        const UINT *initial_counts)
{
    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++) {
        D3dView *v = i >= start && i < start + count && views ? D3dViewFrom(views[i - start], D3D_VIEW_UAV) : nullptr;
        if (v)
            unbind_inputs(c, v->resource);
        ref_view(c, &c->om_uav[i], v);
    }
    c->om_uav_start = start;
    c->stages[D3D_PS].table_valid = FALSE;
    c->targets_dirty = TRUE;
    c->outputs_dirty = TRUE;
}

static void STDMETHODCALLTYPE om_set_render_targets(ID3D11DeviceContext4 *iface, UINT count,
                                                    ID3D11RenderTargetView *const *views,
                                                    ID3D11DepthStencilView *depth)
{
    D3D_SYNC(impl(iface));
    set_render_targets(impl(iface), count, views, depth);
}

static void STDMETHODCALLTYPE om_set_render_targets_and_uavs(ID3D11DeviceContext4 *iface, UINT count,
                                                             ID3D11RenderTargetView *const *views,
                                                             ID3D11DepthStencilView *depth, UINT uav_slot,
                                                             UINT uav_count,
                                                             ID3D11UnorderedAccessView *const *uavs,
                                                             const UINT *initial_counts)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (count != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
        set_render_targets(c, count, views, depth);
    if (uav_count != D3D11_KEEP_UNORDERED_ACCESS_VIEWS)
        set_om_uavs(c, uav_slot, uav_count, uavs, initial_counts);
}

static void STDMETHODCALLTYPE om_get_render_targets(ID3D11DeviceContext4 *iface, UINT count,
                                                    ID3D11RenderTargetView **views, ID3D11DepthStencilView **depth)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (views)
        for (UINT i = 0; i < count; i++) {
            D3dView *v = i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT ? c->rtv[i] : nullptr;
            views[i] = v ? (ID3D11RenderTargetView *)&v->iface : nullptr;
            if (v)
                InterlockedIncrement(&v->child.refs);
        }
    if (depth) {
        *depth = c->dsv ? (ID3D11DepthStencilView *)&c->dsv->iface : nullptr;
        if (c->dsv)
            InterlockedIncrement(&c->dsv->child.refs);
    }
}

static void STDMETHODCALLTYPE om_get_render_targets_and_uavs(ID3D11DeviceContext4 *iface, UINT count,
                                                             ID3D11RenderTargetView **views,
                                                             ID3D11DepthStencilView **depth, UINT uav_slot,
                                                             UINT uav_count, ID3D11UnorderedAccessView **uavs)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    om_get_render_targets(iface, count, views, depth);
    if (uavs)
        for (UINT i = 0; i < uav_count; i++) {
            D3dView *v = uav_slot + i < D3D11_1_UAV_SLOT_COUNT ? c->om_uav[uav_slot + i] : nullptr;
            uavs[i] = v ? (ID3D11UnorderedAccessView *)&v->iface : nullptr;
            if (v)
                InterlockedIncrement(&v->child.refs);
        }
}

static void STDMETHODCALLTYPE om_get_blend_state(ID3D11DeviceContext4 *iface, ID3D11BlendState **state,
                                                 FLOAT factor[4], UINT *mask)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (state) {
        *state = c->blend ? (ID3D11BlendState *)&c->blend->iface : nullptr;
        if (c->blend)
            InterlockedIncrement(&c->blend->child.refs);
    }
    if (factor)
        memcpy(factor, c->blend_factor, sizeof(c->blend_factor));
    if (mask)
        *mask = c->sample_mask;
}

static void STDMETHODCALLTYPE om_get_depth_stencil_state(ID3D11DeviceContext4 *iface,
                                                         ID3D11DepthStencilState **state, UINT *ref)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (state) {
        *state = c->depth_stencil ? &c->depth_stencil->iface : nullptr;
        if (c->depth_stencil)
            InterlockedIncrement(&c->depth_stencil->child.refs);
    }
    if (ref)
        *ref = c->stencil_ref;
}

static void STDMETHODCALLTYPE cs_set_unordered_access_views(ID3D11DeviceContext4 *iface, UINT slot, UINT count,
                                                            ID3D11UnorderedAccessView *const *views,
                                                            const UINT *initial_counts)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (slot >= D3D11_1_UAV_SLOT_COUNT)
        return;
    count = D3D_MIN(count, D3D11_1_UAV_SLOT_COUNT - slot);
    for (UINT i = 0; i < count; i++) {
        D3dView *v = views ? D3dViewFrom(views[i], D3D_VIEW_UAV) : nullptr;
        if (v)
            unbind_inputs(c, v->resource);
        ref_view(c, &c->stages[D3D_CS].uav[slot + i], v);
    }
    c->stages[D3D_CS].table_valid = FALSE;
    c->outputs_dirty = TRUE;
}

static void STDMETHODCALLTYPE cs_get_unordered_access_views(ID3D11DeviceContext4 *iface, UINT slot, UINT count,
                                                            ID3D11UnorderedAccessView **views)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    for (UINT i = 0; i < count; i++) {
        D3dView *v = slot + i < D3D11_1_UAV_SLOT_COUNT ? c->stages[D3D_CS].uav[slot + i] : nullptr;
        views[i] = v ? (ID3D11UnorderedAccessView *)&v->iface : nullptr;
        if (v)
            InterlockedIncrement(&v->child.refs);
    }
}

/*
 * new targets end the old ones' transform feedback. an offset other than -1
 * sets the buffer's count of bytes written, -1 appends after what was written.
 */
static void STDMETHODCALLTYPE so_set_targets(ID3D11DeviceContext4 *iface, UINT count, ID3D11Buffer *const *buffers,
                                             const UINT *offsets)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    D3dDevice *device = c->child.device;
    if (c->xfb_active)
        end_rendering(c);
    for (UINT i = 0; i < D3D11_SO_BUFFER_SLOT_COUNT; i++) {
        D3dResource *r = i < count && buffers ? D3dResourceFrom((ID3D11Resource *)buffers[i]) : nullptr;
        if (r && !r->buf.so_counter)
            r = nullptr;
        unbind_inputs(c, r);
        ref_resource(&c->so[i], r);
        c->outputs_dirty = TRUE;
        c->so_offset[i] = i < count && offsets ? offsets[i] : 0;
        if (r && c->so_offset[i] != ~0u) {
            VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
            if (!cmd)
                continue;
            device->vk.vkCmdUpdateBuffer(cmd, r->buf.buffer, r->buf.offset + r->buf.so_counter, 4, &c->so_offset[i]);
            D3dContextUse(c, r);
            c->pending_barrier = TRUE;
        }
    }
}

static void STDMETHODCALLTYPE so_get_targets(ID3D11DeviceContext4 *iface, UINT count, ID3D11Buffer **buffers)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    for (UINT i = 0; i < count; i++) {
        D3dResource *r = i < D3D11_SO_BUFFER_SLOT_COUNT ? c->so[i] : nullptr;
        buffers[i] = r ? &r->u.buffer : nullptr;
        if (r)
            D3dResourceAddRef(r);
    }
}

/* ------------------------------------------------------------------ state as vulkan dynamic state */

static VkBlendFactor blend_factor(D3D11_BLEND b, BOOL alpha)
{
    switch (b) {
    case D3D11_BLEND_ZERO:
        return VK_BLEND_FACTOR_ZERO;
    case D3D11_BLEND_ONE:
        return VK_BLEND_FACTOR_ONE;
    case D3D11_BLEND_SRC_COLOR:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case D3D11_BLEND_INV_SRC_COLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case D3D11_BLEND_SRC_ALPHA:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case D3D11_BLEND_INV_SRC_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case D3D11_BLEND_DEST_ALPHA:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case D3D11_BLEND_INV_DEST_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case D3D11_BLEND_DEST_COLOR:
        return VK_BLEND_FACTOR_DST_COLOR;
    case D3D11_BLEND_INV_DEST_COLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case D3D11_BLEND_SRC_ALPHA_SAT:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case D3D11_BLEND_BLEND_FACTOR:
        return alpha ? VK_BLEND_FACTOR_CONSTANT_ALPHA : VK_BLEND_FACTOR_CONSTANT_COLOR;
    case D3D11_BLEND_INV_BLEND_FACTOR:
        return alpha ? VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case D3D11_BLEND_SRC1_COLOR:
        return VK_BLEND_FACTOR_SRC1_COLOR;
    case D3D11_BLEND_INV_SRC1_COLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    case D3D11_BLEND_SRC1_ALPHA:
        return VK_BLEND_FACTOR_SRC1_ALPHA;
    case D3D11_BLEND_INV_SRC1_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    default:
        return VK_BLEND_FACTOR_ONE;
    }
}

static VkStencilOp stencil_op(D3D11_STENCIL_OP op)
{
    static const VkStencilOp OPS[] = {VK_STENCIL_OP_KEEP,
                                      VK_STENCIL_OP_KEEP,
                                      VK_STENCIL_OP_ZERO,
                                      VK_STENCIL_OP_REPLACE,
                                      VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                      VK_STENCIL_OP_DECREMENT_AND_CLAMP,
                                      VK_STENCIL_OP_INVERT,
                                      VK_STENCIL_OP_INCREMENT_AND_WRAP,
                                      VK_STENCIL_OP_DECREMENT_AND_WRAP};
    return (UINT)op < RTL_NUMBER_OF(OPS) ? OPS[op] : VK_STENCIL_OP_KEEP;
}

static VkPrimitiveTopology topology(D3D11_PRIMITIVE_TOPOLOGY t)
{
    if ((UINT)t == MIME_TOPOLOGY_TRIANGLEFAN)
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    switch ((UINT)t & ~MIME_TOPOLOGY_D3D9) {
    case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
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

/* direct3d's default states, used when null is bound. */
static const D3D11_RASTERIZER_DESC2 DEFAULT_RS = {D3D11_FILL_SOLID, D3D11_CULL_BACK, FALSE, 0, 0, 0, TRUE};
static const D3D11_DEPTH_STENCIL_DESC DEFAULT_DS = {
    TRUE, D3D11_DEPTH_WRITE_MASK_ALL, D3D11_COMPARISON_LESS, FALSE, 0xFF, 0xFF,
    {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS},
    {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS}};

static void emit_viewports(D3dContext *c, VkCommandBuffer cmd, VkExtent2D area)
{
    D3dVk *vk = &c->child.device->vk;
    const D3D11_RASTERIZER_DESC2 *rs = c->rasterizer ? &c->rasterizer->desc : &DEFAULT_RS;
    UINT count = D3D_MAX(c->viewport_count, 1);
    VkViewport viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    VkRect2D scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    for (UINT i = 0; i < count; i++) {
        D3D11_VIEWPORT v = i < c->viewport_count ? c->viewports[i] : (D3D11_VIEWPORT){0, 0, 0, 0, 0, 1};
        /* direct3d's y points down, so a negative height viewport flips vulkan's. */
        if (v.Width <= 0 || v.Height <= 0)
            v.Width = v.Height = 1;
        viewports[i] = (VkViewport){v.TopLeftX, v.TopLeftY + v.Height, v.Width, -v.Height,
                                    D3D_MAX(0.0f, D3D_MIN(v.MinDepth, 1.0f)), D3D_MAX(0.0f, D3D_MIN(v.MaxDepth, 1.0f))};
        if (rs->ScissorEnable) {
            D3D11_RECT r = i < c->scissor_count ? c->scissors[i] : (D3D11_RECT){0};
            LONG left = D3D_MAX(r.left, 0), top = D3D_MAX(r.top, 0);
            scissors[i] = (VkRect2D){{left, top},
                                     {(UINT)D3D_MAX(r.right - left, 0), (UINT)D3D_MAX(r.bottom - top, 0)}};
        } else {
            scissors[i] = (VkRect2D){{0, 0}, area};
        }
    }
    vk->vkCmdSetViewportWithCount(cmd, count, viewports);
    vk->vkCmdSetScissorWithCount(cmd, count, scissors);
}

/* stores the value when it differs from the command buffer's. in a new command buffer everything differs. */
#define DYN_CHANGED(field, value) (c->state_dirty || c->dyn.field != (value) ? (c->dyn.field = (value), TRUE) : FALSE)

static inline __attribute__((always_inline)) BOOL dyn_changed_bytes(D3dContext *c, void *field, const void *value,
                                                                  SIZE_T size)
{
    if (!c->state_dirty && !memcmp(field, value, size))
        return FALSE;
    memcpy(field, value, size);
    return TRUE;
}

static void emit_rasterizer(D3dContext *c, VkCommandBuffer cmd)
{
    D3dDevice *device = c->child.device;
    D3dVk *vk = &device->vk;
    const D3D11_RASTERIZER_DESC2 *rs = c->rasterizer ? &c->rasterizer->desc : &DEFAULT_RS;
    static const VkCullModeFlags CULL[] = {VK_CULL_MODE_NONE, VK_CULL_MODE_NONE, VK_CULL_MODE_FRONT_BIT,
                                           VK_CULL_MODE_BACK_BIT};
    /* stream output may rasterize a stream other than the first, or none. */
    D3dShader *gs = c->stages[D3D_GS].shader;
    UINT stream = gs && gs->so ? gs->rasterized_stream : 0;
    if (DYN_CHANGED(discard, (UINT)(stream == D3D11_SO_NO_RASTERIZED_STREAM)))
        vk->vkCmdSetRasterizerDiscardEnable(cmd, c->dyn.discard);
    if (DYN_CHANGED(cull, (UINT)CULL[rs->CullMode]))
        vk->vkCmdSetCullMode(cmd, c->dyn.cull);
    if (DYN_CHANGED(front, (UINT)(rs->FrontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE)))
        vk->vkCmdSetFrontFace(cmd, (VkFrontFace)c->dyn.front);
    if (DYN_CHANGED(polygon, (UINT)(rs->FillMode == D3D11_FILL_WIREFRAME ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL)))
        vk->vkCmdSetPolygonModeEXT(cmd, (VkPolygonMode)c->dyn.polygon);
    if (DYN_CHANGED(bias_enable, (UINT)(rs->DepthBias || rs->SlopeScaledDepthBias)))
        vk->vkCmdSetDepthBiasEnable(cmd, c->dyn.bias_enable);
    float bias[3] = {(float)rs->DepthBias, rs->DepthBiasClamp, rs->SlopeScaledDepthBias};
    if (dyn_changed_bytes(c, c->dyn.bias, bias, sizeof(bias)))
        vk->vkCmdSetDepthBias(cmd, bias[0], bias[1], bias[2]);
    /* direct3d always clamps depth to the viewport, so disabling clipping leaves only the clamp. */
    if (device->has_depth_clip) {
        if (DYN_CHANGED(clamp, TRUE))
            vk->vkCmdSetDepthClampEnableEXT(cmd, TRUE);
        if (DYN_CHANGED(clip, (UINT)rs->DepthClipEnable))
            vk->vkCmdSetDepthClipEnableEXT(cmd, c->dyn.clip);
    } else if (DYN_CHANGED(clamp, (UINT)!rs->DepthClipEnable)) {
        vk->vkCmdSetDepthClampEnableEXT(cmd, c->dyn.clamp);
    }
    if (c->state_dirty) {
        vk->vkCmdSetLineWidth(cmd, 1.0f);
        if (device->has_tessellation)
            vk->vkCmdSetTessellationDomainOriginEXT(cmd, VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT);
        /* features enabled for direct3d 12 whose state shader objects need set. */
        if (device->has_conservative)
            vk->vkCmdSetConservativeRasterizationModeEXT(cmd, VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT);
        if (device->has_vrs) {
            const VkExtent2D one = {1, 1};
            const VkFragmentShadingRateCombinerOpKHR keep[2] = {VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR,
                                                                VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR};
            vk->vkCmdSetFragmentShadingRateKHR(cmd, &one, keep);
        }
    }
    VkPrimitiveTopology t = topology(c->topology);
    if (DYN_CHANGED(topology, (UINT)t))
        vk->vkCmdSetPrimitiveTopology(cmd, t);
    /* direct3d 10 and later always cut strips at the all ones index, direct3d 9 does not. */
    UINT restart = !(c->topology & MIME_TOPOLOGY_D3D9) &&
                   (t == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP || t == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
                    t == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY ||
                    t == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY);
    if (DYN_CHANGED(restart, restart))
        vk->vkCmdSetPrimitiveRestartEnable(cmd, restart);
    if (device->has_tessellation) {
        UINT points = t == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST ? c->topology - D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1 : 1;
        if (DYN_CHANGED(points, points))
            vk->vkCmdSetPatchControlPointsEXT(cmd, points);
    }
    if (device->has_transform_feedback && DYN_CHANGED(stream, stream < 4 ? stream : 0))
        vk->vkCmdSetRasterizationStreamEXT(cmd, c->dyn.stream);
}

static void emit_output_merger(D3dContext *c, VkCommandBuffer cmd)
{
    D3dDevice *device = c->child.device;
    D3dVk *vk = &device->vk;
    const D3D11_DEPTH_STENCIL_DESC *ds = c->depth_stencil ? &c->depth_stencil->desc : &DEFAULT_DS;
    BOOL has_depth = c->dsv && (c->dsv->range.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT);
    BOOL has_stencil = c->dsv && (c->dsv->range.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT);
    if (DYN_CHANGED(depth_test, (UINT)(ds->DepthEnable && has_depth)))
        vk->vkCmdSetDepthTestEnable(cmd, c->dyn.depth_test);
    if (DYN_CHANGED(depth_write, (UINT)(ds->DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL && has_depth &&
                                        !(c->dsv->desc.dsv.Flags & D3D11_DSV_READ_ONLY_DEPTH))))
        vk->vkCmdSetDepthWriteEnable(cmd, c->dyn.depth_write);
    if (DYN_CHANGED(depth_op, (UINT)(ds->DepthFunc - 1)))
        vk->vkCmdSetDepthCompareOp(cmd, (VkCompareOp)c->dyn.depth_op);
    if (c->state_dirty)
        vk->vkCmdSetDepthBoundsTestEnable(cmd, FALSE);
    if (DYN_CHANGED(stencil_test, (UINT)(ds->StencilEnable && has_stencil)))
        vk->vkCmdSetStencilTestEnable(cmd, c->dyn.stencil_test);
    const D3D11_DEPTH_STENCILOP_DESC *faces[2] = {&ds->FrontFace, &ds->BackFace};
    for (UINT i = 0; i < 2; i++) {
        UINT ops[4] = {stencil_op(faces[i]->StencilFailOp), stencil_op(faces[i]->StencilPassOp),
                       stencil_op(faces[i]->StencilDepthFailOp), faces[i]->StencilFunc - 1};
        if (dyn_changed_bytes(c, c->dyn.stencil_ops[i], ops, sizeof(ops)))
            vk->vkCmdSetStencilOp(cmd, i ? VK_STENCIL_FACE_BACK_BIT : VK_STENCIL_FACE_FRONT_BIT, (VkStencilOp)ops[0],
                                  (VkStencilOp)ops[1], (VkStencilOp)ops[2], (VkCompareOp)ops[3]);
    }
    if (DYN_CHANGED(compare_mask, (UINT)ds->StencilReadMask))
        vk->vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, c->dyn.compare_mask);
    BOOL stencil_writes = !(c->dsv && (c->dsv->desc.dsv.Flags & D3D11_DSV_READ_ONLY_STENCIL));
    if (DYN_CHANGED(write_mask, (UINT)(stencil_writes ? ds->StencilWriteMask : 0)))
        vk->vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, c->dyn.write_mask);
    if (DYN_CHANGED(reference, c->stencil_ref & 0xFF))
        vk->vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, c->dyn.reference);

    const D3D11_BLEND_DESC1 *blend = c->blend ? &c->blend->desc : nullptr;
    VkBool32 enables[8];
    VkColorBlendEquationEXT equations[8];
    VkColorComponentFlags masks[8];
    VkBool32 logic = FALSE;
    VkLogicOp op = VK_LOGIC_OP_NO_OP;
    for (UINT i = 0; i < 8; i++) {
        const D3D11_RENDER_TARGET_BLEND_DESC1 *t = blend ? &blend->RenderTarget[i] : nullptr;
        enables[i] = t && t->BlendEnable;
        equations[i] = (VkColorBlendEquationEXT){
            t ? blend_factor(t->SrcBlend, FALSE) : VK_BLEND_FACTOR_ONE,
            t ? blend_factor(t->DestBlend, FALSE) : VK_BLEND_FACTOR_ZERO,
            t ? (VkBlendOp)(t->BlendOp - 1) : VK_BLEND_OP_ADD,
            t ? blend_factor(t->SrcBlendAlpha, TRUE) : VK_BLEND_FACTOR_ONE,
            t ? blend_factor(t->DestBlendAlpha, TRUE) : VK_BLEND_FACTOR_ZERO,
            t ? (VkBlendOp)(t->BlendOpAlpha - 1) : VK_BLEND_OP_ADD};
        masks[i] = t ? t->RenderTargetWriteMask & 0xF : 0xF;
        if (t && t->LogicOpEnable) {
            logic = TRUE;
            static const VkLogicOp LOGIC[] = {
                VK_LOGIC_OP_CLEAR,      VK_LOGIC_OP_SET,           VK_LOGIC_OP_COPY,         VK_LOGIC_OP_COPY_INVERTED,
                VK_LOGIC_OP_NO_OP,      VK_LOGIC_OP_INVERT,        VK_LOGIC_OP_AND,          VK_LOGIC_OP_NAND,
                VK_LOGIC_OP_OR,         VK_LOGIC_OP_NOR,           VK_LOGIC_OP_XOR,          VK_LOGIC_OP_EQUIVALENT,
                VK_LOGIC_OP_AND_REVERSE, VK_LOGIC_OP_AND_INVERTED, VK_LOGIC_OP_OR_REVERSE,  VK_LOGIC_OP_OR_INVERTED};
            op = LOGIC[t->LogicOp & 15];
        }
    }
    if (dyn_changed_bytes(c, c->dyn.blend_enables, enables, sizeof(enables)))
        vk->vkCmdSetColorBlendEnableEXT(cmd, 0, 8, enables);
    if (dyn_changed_bytes(c, c->dyn.equations, equations, sizeof(equations)))
        vk->vkCmdSetColorBlendEquationEXT(cmd, 0, 8, equations);
    if (dyn_changed_bytes(c, c->dyn.color_masks, masks, sizeof(masks)))
        vk->vkCmdSetColorWriteMaskEXT(cmd, 0, 8, masks);
    if (device->has_logic_op) {
        if (DYN_CHANGED(logic, (UINT)logic))
            vk->vkCmdSetLogicOpEnableEXT(cmd, logic);
        if (DYN_CHANGED(logic_op, (UINT)op))
            vk->vkCmdSetLogicOpEXT(cmd, op);
    }
    if (dyn_changed_bytes(c, c->dyn.blend_constants, c->blend_factor, sizeof(c->dyn.blend_constants)))
        vk->vkCmdSetBlendConstants(cmd, c->blend_factor);
    if (DYN_CHANGED(alpha_to_coverage, (UINT)(blend && blend->AlphaToCoverageEnable)))
        vk->vkCmdSetAlphaToCoverageEnableEXT(cmd, c->dyn.alpha_to_coverage);
    VkSampleMask mask = c->sample_mask;
    UINT samples = c->render_samples ? c->render_samples : 1;
    BOOL samples_changed = DYN_CHANGED(samples, samples);
    if (samples_changed)
        vk->vkCmdSetRasterizationSamplesEXT(cmd, (VkSampleCountFlagBits)samples);
    if (DYN_CHANGED(sample_mask, (UINT)mask) || samples_changed)
        vk->vkCmdSetSampleMaskEXT(cmd, (VkSampleCountFlagBits)samples, &mask);
}

/* the layout's slots with the bound strides, and the attributes. */
static void emit_vertex_input(D3dContext *c, VkCommandBuffer cmd)
{
    D3dDevice *device = c->child.device;
    D3dVk *vk = &device->vk;
    VkVertexInputBindingDescription2EXT bindings[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT count = 0;
    D3dInputLayout *layout = c->layout;
    if (layout)
        for (UINT i = 0; i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; i++) {
            if (!(layout->slot_mask & (1u << i)))
                continue;
            VkVertexInputBindingDescription2EXT *b = &bindings[count++];
            b->sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT;
            b->pNext = nullptr;
            b->binding = i;
            b->stride = c->vb_stride[i];
            b->inputRate = layout->instanced[i] ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
            /* a step rate of 0 gives every instance the first element. */
            UINT step = layout->instanced[i] ? layout->step_rate[i] : 1;
            b->divisor = (step > 1 && device->has_divisor) || (!step && device->has_zero_divisor) ? step : 1;
        }
    vk->vkCmdSetVertexInputEXT(cmd, count, bindings, layout ? layout->count : 0, layout ? layout->attributes : nullptr);
}

static void emit_buffers(D3dContext *c, VkCommandBuffer cmd)
{
    D3dVk *vk = &c->child.device->vk;
    for (UINT i = 0; i < c->vb_high; i++) {
        D3dResource *r = c->vb[i];
        if (r && c->vb_version[i] == r->buf.version && !c->ia_dirty)
            continue;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0, size = VK_WHOLE_SIZE;
        if (r && c->vb_offset[i] < r->buf.size) {
            D3dContextBufferRange(c, r, &buffer, &offset);
            offset += c->vb_offset[i];
            size = r->buf.size - c->vb_offset[i];
            c->vb_version[i] = r->buf.version;
        } else if (!r) {
            continue;
        }
        /* the stride again, which the vertex input already gives: some drivers read it here. */
        VkDeviceSize stride = c->vb_stride[i];
        if (buffer)
            vk->vkCmdBindVertexBuffers2(cmd, i, 1, &buffer, &offset, &size, &stride);
    }
    if (c->ib && (c->ia_dirty || c->ib_version != c->ib->buf.version) && c->ib_offset < c->ib->buf.size) {
        VkBuffer buffer;
        VkDeviceSize offset;
        D3dContextBufferRange(c, c->ib, &buffer, &offset);
        vk->vkCmdBindIndexBuffer(cmd, buffer, offset + c->ib_offset,
                                 c->ib_format == DXGI_FORMAT_R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        c->ib_version = c->ib->buf.version;
    }
    c->ia_dirty = FALSE;
}

/* ------------------------------------------------------------------ binding tables */

/* also returns the resource behind the entry. */
static UINT table_value(D3dContext *c, D3dStage stage, UINT entry, D3dResource **resource)
{
    D3dStageState *s = &c->stages[stage];
    const DxbcShaderInfo *info = &s->shader->info;
    *resource = nullptr;
    if (entry == DXBC_TABLE_SAMPLE_COUNT)
        return c->render_samples ? c->render_samples : 1;
    if (entry < DXBC_TABLE_SAMPLER) {
        UINT i = entry - DXBC_TABLE_CB;
        D3dResource *r = s->cb[i];
        if (!r)
            return 0;
        *resource = r;
        const D3dRingSlice *slice = D3dContextListRange(c, r);
        UINT first = s->cb_first[i] * 16;
        if (!slice)
            return D3dBufferCbv(r, first, s->cb_count[i] * 16);
        return first < r->buf.size ? D3dCbvIndex(c->child.device, slice->buffer, slice->offset + first,
                                                 D3D_MIN(s->cb_count[i] * 16, r->buf.size - first))
                                   : 0;
    }
    if (entry < DXBC_TABLE_SRV) {
        D3dSamplerState *sampler = s->sampler[entry - DXBC_TABLE_SAMPLER];
        return sampler ? sampler->heap_index : 0;
    }
    if (entry < DXBC_TABLE_UAV) {
        UINT i = entry - DXBC_TABLE_SRV;
        D3dView *v = s->srv[i];
        if (!v)
            return 0;
        D3dViewRefresh(v);
        *resource = v->resource;
        /* a view of the wrong kind for the shader's declaration reads as null. */
        UINT kind = info->srv_kind[i] == DXBC_SLOT_BUFFER ? D3D_HEAP_UNIFORM_TEXEL : D3D_HEAP_SAMPLED_IMAGE;
        return v->heap_kind == kind ? v->heap_index : 0;
    }
    D3dView *const *uavs = stage == D3D_CS ? s->uav : c->om_uav;
    if (stage != D3D_CS && stage != D3D_PS)
        return 0;
    BOOL counter = entry >= DXBC_TABLE_UAV_COUNTER;
    UINT i = counter ? entry - DXBC_TABLE_UAV_COUNTER : entry - DXBC_TABLE_UAV;
    D3dView *v = uavs[i];
    if (!v)
        return 0;
    D3dViewRefresh(v);
    *resource = v->resource;
    if (counter)
        return v->counter_index;
    UINT kind = info->uav_kind[i] == DXBC_SLOT_BUFFER ? D3D_HEAP_STORAGE_TEXEL : D3D_HEAP_STORAGE_IMAGE;
    return v->heap_kind == kind ? v->heap_index : 0;
}

static void build_table(D3dContext *c, D3dStage stage, D3dBindingTable *t)
{
    D3dStageState *s = &c->stages[stage];
    const DxbcShaderInfo *info = &s->shader->info;
    for (UINT k = 0; k < info->table_count; k++) {
        D3dResource *r;
        t->entries[k] = table_value(c, stage, info->table_entries[k], &r);
        s->table_resources[k] = r;
        if (r && r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
            s->table_versions[k] = r->buf.version;
        D3dContextUse(c, r);
    }
}

/* stale when a buffer the table read was renamed since. */
static BOOL table_stale(const D3dStageState *s)
{
    for (UINT k = 0; k < s->shader->info.table_count; k++) {
        const D3dResource *r = s->table_resources[k];
        if (r && r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER && r->buf.version != s->table_versions[k])
            return TRUE;
    }
    return FALSE;
}

/*
 * uploads and binds the tables of stages whose bindings changed. the other
 * stages only mark their resources used, once per submission or list.
 */
static BOOL emit_tables(D3dContext *c, VkCommandBuffer cmd, BOOL compute)
{
    D3dDevice *device = c->child.device;
    BOOL changed = FALSE;
    UINT first = compute ? D3D_CS : D3D_VS, last = compute ? D3D_CS : D3D_PS;
    UINT64 epoch = c->type == D3D11_DEVICE_CONTEXT_IMMEDIATE ? device->next_serial : c->list_id;
    for (UINT stage = first; stage <= last; stage++) {
        D3dStageState *s = &c->stages[stage];
        if (!s->shader)
            continue;
        if (s->table_valid && !table_stale(s)) {
            if (s->table_epoch != epoch) {
                for (UINT k = 0; k < s->shader->info.table_count; k++)
                    D3dContextUse(c, s->table_resources[k]);
                s->table_epoch = epoch;
            }
            continue;
        }
        UINT count = s->shader->info.table_count;
        D3dBindingTable table;
        build_table(c, stage, &table);
        s->table_epoch = epoch;
        /* the set's descriptors span a whole table: reserve room for one, give back the rest. */
        D3dRingSlice slice;
        if (!D3dContextRingAlloc(c, sizeof(table), device->properties.limits.minUniformBufferOffsetAlignment, &slice))
            return FALSE;
        memcpy(slice.mapped, table.entries, count * sizeof(UINT));
        D3dRingTrim(device, &slice, sizeof(table), D3D_ALIGN(count * sizeof(UINT), 16));
        s->table_valid = TRUE;
        if (slice.chunk != c->table_chunk) {
            /* a new chunk needs its own set, and the other stages' tables uploaded into it again. */
            c->table_chunk = slice.chunk;
            c->table_set = D3dRingTableSet(device, slice.chunk);
            for (UINT k = 0; k < D3D_STAGES; k++)
                if (k != stage)
                    c->stages[k].table_valid = FALSE;
            c->tables_bound[0] = c->tables_bound[1] = FALSE;
            if (stage != first) {
                /* the stages before this one have tables in the old chunk: start over. */
                return emit_tables(c, cmd, compute);
            }
        }
        c->table_offsets[stage] = (UINT)slice.chunk_offset;
        changed = TRUE;
    }
    VkPipelineBindPoint point = compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    D3dVk *vk = &device->vk;
    if (!c->heap_bound[compute]) {
        vk->vkCmdBindDescriptorSets(cmd, point, device->pipeline_layout, 0, 1, &device->heap.set, 0, nullptr);
        c->heap_bound[compute] = TRUE;
    }
    if (c->table_set && (changed || !c->tables_bound[compute])) {
        vk->vkCmdBindDescriptorSets(cmd, point, device->pipeline_layout, 1, 1, &c->table_set, D3D_STAGES,
                                    c->table_offsets);
        c->tables_bound[compute] = TRUE;
        c->tables_bound[!compute] = FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ rendering */

/* the area all bound targets cover, and the layer count. */
static VkExtent2D render_area(D3dContext *c, UINT *layers)
{
    VkExtent2D area = {~0u, ~0u};
    UINT l = ~0u;
    for (UINT i = 0; i < c->rtv_count; i++)
        if (c->rtv[i]) {
            area.width = D3D_MIN(area.width, c->rtv[i]->extent.width);
            area.height = D3D_MIN(area.height, c->rtv[i]->extent.height);
            l = D3D_MIN(l, c->rtv[i]->range.layerCount);
        }
    if (c->dsv) {
        area.width = D3D_MIN(area.width, c->dsv->extent.width);
        area.height = D3D_MIN(area.height, c->dsv->extent.height);
        l = D3D_MIN(l, c->dsv->range.layerCount);
    }
    if (area.width == ~0u) {
        /* without targets, rendering that only writes views uses the viewport's extent. */
        area.width = c->viewport_count ? (UINT)D3D_MAX(c->viewports[0].TopLeftX + c->viewports[0].Width, 1) : 1;
        area.height = c->viewport_count ? (UINT)D3D_MAX(c->viewports[0].TopLeftY + c->viewports[0].Height, 1) : 1;
        l = 1;
    }
    *layers = l;
    return area;
}

static void begin_rendering(D3dContext *c, VkCommandBuffer cmd)
{
    D3dDevice *device = c->child.device;
    VkRenderingAttachmentInfo colors[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    VkRenderingAttachmentInfo depth = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    UINT layers;
    VkExtent2D area = render_area(c, &layers);
    UINT samples = 0;
    for (UINT i = 0; i < c->rtv_count; i++) {
        colors[i] = (VkRenderingAttachmentInfo){VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        D3dView *v = c->rtv[i];
        if (!v)
            continue;
        colors[i].imageView = v->image_view;
        colors[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        samples = v->samples;
        D3dContextUse(c, v->resource);
    }
    VkRenderingInfo info = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent = area;
    info.layerCount = layers;
    info.colorAttachmentCount = c->rtv_count;
    info.pColorAttachments = colors;
    if (c->dsv) {
        depth.imageView = c->dsv->image_view;
        depth.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        samples = c->dsv->samples;
        if (c->dsv->range.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
            info.pDepthAttachment = &depth;
        if (c->dsv->range.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
            info.pStencilAttachment = &depth;
        D3dContextUse(c, c->dsv->resource);
    }
    if (!samples)
        samples = c->rasterizer && c->rasterizer->desc.ForcedSampleCount ? c->rasterizer->desc.ForcedSampleCount : 1;
    /* query segments begun for dispatches end outside the render pass instance. */
    D3dQueriesClose(c, cmd);
    device->vk.vkCmdBeginRendering(cmd, &info);
    c->rendering = TRUE;
    c->targets_dirty = FALSE;
    if (c->render_samples != samples || c->render_color_count != c->rtv_count)
        c->om_dirty = TRUE;
    /* pixel shaders may read the sample count from their table. */
    if (c->render_samples != samples)
        c->stages[D3D_PS].table_valid = FALSE;
    c->render_samples = samples;
    c->render_color_count = c->rtv_count;
    c->viewports_dirty = TRUE;
}

/* ------------------------------------------------------------------ stream output */

/* needs a geometry shader with a declaration and buffers to write. */
static BOOL streams_out(D3dContext *c)
{
    D3dShader *gs = c->stages[D3D_GS].shader;
    if (!gs || !gs->so || !c->child.device->has_transform_feedback)
        return FALSE;
    for (UINT i = 0; i < D3D11_SO_BUFFER_SLOT_COUNT; i++)
        if (c->so[i])
            return TRUE;
    return FALSE;
}

/* the targets and their written byte counters, with the dummy for empty slots. */
static void stream_output_buffers(D3dContext *c, VkBuffer *buffers, VkDeviceSize *offsets, VkDeviceSize *sizes,
                                  VkBuffer *counters, VkDeviceSize *counter_offsets)
{
    D3dDevice *device = c->child.device;
    for (UINT i = 0; i < D3D11_SO_BUFFER_SLOT_COUNT; i++) {
        D3dResource *r = c->so[i];
        buffers[i] = r ? r->buf.buffer : device->so_dummy.buffer;
        offsets[i] = r ? r->buf.offset : device->so_dummy.offset;
        sizes[i] = r ? r->buf.size : 4;
        counters[i] = r ? r->buf.buffer : VK_NULL_HANDLE;
        counter_offsets[i] = r ? r->buf.offset + r->buf.so_counter : 0;
    }
}

/* starts inside a render pass instance, at each buffer's written byte count. */
static void begin_stream_output(D3dContext *c, VkCommandBuffer cmd)
{
    D3dVk *vk = &c->child.device->vk;
    VkBuffer buffers[4], counters[4];
    VkDeviceSize offsets[4], sizes[4], counter_offsets[4];
    stream_output_buffers(c, buffers, offsets, sizes, counters, counter_offsets);
    vk->vkCmdBindTransformFeedbackBuffersEXT(cmd, 0, 4, buffers, offsets, sizes);
    vk->vkCmdBeginTransformFeedbackEXT(cmd, 0, 4, counters, counter_offsets);
    for (UINT i = 0; i < D3D11_SO_BUFFER_SLOT_COUNT; i++)
        D3dContextUse(c, c->so[i]);
    c->xfb_active = TRUE;
}

static void end_stream_output(D3dContext *c, VkCommandBuffer cmd)
{
    VkBuffer buffers[4], counters[4];
    VkDeviceSize offsets[4], sizes[4], counter_offsets[4];
    stream_output_buffers(c, buffers, offsets, sizes, counters, counter_offsets);
    c->child.device->vk.vkCmdEndTransformFeedbackEXT(cmd, 0, 4, counters, counter_offsets);
    c->xfb_active = FALSE;
}

/* the geometry shader input primitive, numbered like dcl_inputprimitive. */
static UINT input_primitive(D3dContext *c)
{
    switch (c->topology) {
    case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
        return 1;
    case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
    case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
        return 2;
    case D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:
    case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
        return 6;
    case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:
    case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:
        return 7;
    default:
        break;
    }
    /* with patches, the tessellator output decides. */
    D3dShader *hs = c->stages[D3D_HS].shader;
    if (c->topology >= D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST && hs)
        return hs->info.tess_output_primitive <= 2 ? hs->info.tess_output_primitive : 3;
    return 3;
}

static BOOL writes_views(D3dContext *c)
{
    D3dShader *ps = c->stages[D3D_PS].shader;
    return ps && ps->info.uav_mask;
}

/* FALSE when the draw cannot happen. */
static BOOL prepare_draw(D3dContext *c)
{
    D3dDevice *device = c->child.device;
    D3dVk *vk = &device->vk;
    if (!c->stages[D3D_VS].shader)
        return FALSE;
    begin_commands(c);
    if (!c->cmd)
        return FALSE;
    VkCommandBuffer cmd = c->cmd->buffer;
    BOOL so = streams_out(c);
    if ((c->targets_dirty || (c->xfb_active && !so)) && c->rendering)
        end_rendering(c);
    if (!c->rendering) {
        if (c->pending_barrier) {
            D3dBarrier(cmd, device);
            c->pending_barrier = FALSE;
        }
        begin_rendering(c, cmd);
    }
    if (c->query_count)
        D3dQueriesOpen(c, cmd, TRUE);
    if (!emit_tables(c, cmd, FALSE))
        return FALSE;
    D3dShader *gs = c->stages[D3D_GS].shader;
    VkShaderEXT gs_shader = gs ? D3dShaderVariant(gs, input_primitive(c)) : VK_NULL_HANDLE;
    if (gs_shader != c->bound_gs)
        c->shaders_dirty = TRUE;
    if (c->shaders_dirty || c->compute_bound) {
        VkShaderStageFlagBits stages[7] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT,
                                           VK_SHADER_STAGE_GEOMETRY_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                           VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, VK_SHADER_STAGE_TASK_BIT_EXT,
                                           VK_SHADER_STAGE_MESH_BIT_EXT};
        VkShaderEXT shaders[7] = {c->stages[D3D_VS].shader->shader,
                                  c->stages[D3D_PS].shader ? c->stages[D3D_PS].shader->shader : VK_NULL_HANDLE,
                                  gs_shader,
                                  c->stages[D3D_HS].shader ? c->stages[D3D_HS].shader->shader : VK_NULL_HANDLE,
                                  c->stages[D3D_DS].shader ? c->stages[D3D_DS].shader->shader : VK_NULL_HANDLE};
        UINT count = 2 + (device->has_geometry ? 1 : 0);
        if (device->has_tessellation) {
            if (!device->has_geometry) {
                stages[2] = stages[3], shaders[2] = shaders[3];
                stages[3] = stages[4], shaders[3] = shaders[4];
            }
            count += 2;
        }
        /* a device with mesh shaders needs those stages bound too, to nothing. */
        if (device->has_mesh) {
            stages[count] = VK_SHADER_STAGE_TASK_BIT_EXT;
            shaders[count++] = VK_NULL_HANDLE;
            stages[count] = VK_SHADER_STAGE_MESH_BIT_EXT;
            shaders[count++] = VK_NULL_HANDLE;
        }
        vk->vkCmdBindShadersEXT(cmd, count, stages, shaders);
        c->bound_gs = gs_shader;
        c->shaders_dirty = FALSE;
        c->compute_bound = FALSE;
    }
    if (c->state_dirty || c->rs_dirty) {
        emit_rasterizer(c, cmd);
        c->rs_dirty = FALSE;
    }
    if (c->state_dirty || c->om_dirty) {
        emit_output_merger(c, cmd);
        c->om_dirty = FALSE;
    }
    if (c->state_dirty || c->viewports_dirty) {
        UINT layers;
        emit_viewports(c, cmd, render_area(c, &layers));
        c->viewports_dirty = FALSE;
    }
    if (c->state_dirty || c->vertex_input_dirty) {
        emit_vertex_input(c, cmd);
        c->vertex_input_dirty = FALSE;
    }
    c->state_dirty = FALSE;
    if (so && !c->xfb_active)
        begin_stream_output(c, cmd);
    emit_buffers(c, cmd);
    for (UINT i = 0; i < c->vb_high; i++)
        D3dContextUse(c, c->vb[i]);
    D3dContextUse(c, c->ib);
    return TRUE;
}

/* a draw that wrote views ends rendering with a barrier, so what follows sees its writes. */
static void finish_draw(D3dContext *c)
{
    if (writes_views(c))
        end_rendering(c);
}

static void STDMETHODCALLTYPE draw(ID3D11DeviceContext4 *iface, UINT count, UINT start)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (!count || !prepare_draw(c))
        return;
    c->child.device->vk.vkCmdDraw(c->cmd->buffer, count, 1, start, 0);
    finish_draw(c);
}

static void STDMETHODCALLTYPE draw_indexed(ID3D11DeviceContext4 *iface, UINT count, UINT start, INT base)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (!count || !c->ib || !prepare_draw(c))
        return;
    c->child.device->vk.vkCmdDrawIndexed(c->cmd->buffer, count, 1, start, base, 0);
    finish_draw(c);
}

static void STDMETHODCALLTYPE draw_instanced(ID3D11DeviceContext4 *iface, UINT count, UINT instances, UINT start,
                                             UINT start_instance)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (!count || !instances || !prepare_draw(c))
        return;
    c->child.device->vk.vkCmdDraw(c->cmd->buffer, count, instances, start, start_instance);
    finish_draw(c);
}

static void STDMETHODCALLTYPE draw_indexed_instanced(ID3D11DeviceContext4 *iface, UINT count, UINT instances,
                                                     UINT start, INT base, UINT start_instance)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (!count || !instances || !c->ib || !prepare_draw(c))
        return;
    c->child.device->vk.vkCmdDrawIndexed(c->cmd->buffer, count, instances, start, base, start_instance);
    finish_draw(c);
}

/* the vertex count is what stream output wrote to the first vertex buffer, past its offset. */
static void STDMETHODCALLTYPE draw_auto(ID3D11DeviceContext4 *iface)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    D3dResource *r = c->vb[0];
    if (!r || !r->buf.so_counter || !c->vb_stride[0] || !c->child.device->has_transform_feedback || !prepare_draw(c))
        return;
    c->child.device->vk.vkCmdDrawIndirectByteCountEXT(c->cmd->buffer, 1, 0, r->buf.buffer,
                                                      r->buf.offset + r->buf.so_counter, c->vb_offset[0],
                                                      c->vb_stride[0]);
    finish_draw(c);
}

static void STDMETHODCALLTYPE draw_indexed_instanced_indirect(ID3D11DeviceContext4 *iface, ID3D11Buffer *buffer,
                                                              UINT offset)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    D3dResource *r = D3dResourceFrom((ID3D11Resource *)buffer);
    if (!r || !c->ib || !prepare_draw(c))
        return;
    D3dContextUse(c, r);
    VkBuffer args;
    VkDeviceSize base;
    D3dContextBufferRange(c, r, &args, &base);
    c->child.device->vk.vkCmdDrawIndexedIndirect(c->cmd->buffer, args, base + offset, 1, 20);
    finish_draw(c);
}

static void STDMETHODCALLTYPE draw_instanced_indirect(ID3D11DeviceContext4 *iface, ID3D11Buffer *buffer,
                                                      UINT offset)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    D3dResource *r = D3dResourceFrom((ID3D11Resource *)buffer);
    if (!r || !prepare_draw(c))
        return;
    D3dContextUse(c, r);
    VkBuffer args;
    VkDeviceSize base;
    D3dContextBufferRange(c, r, &args, &base);
    c->child.device->vk.vkCmdDrawIndirect(c->cmd->buffer, args, base + offset, 1, 16);
    finish_draw(c);
}

/* ------------------------------------------------------------------ compute */

static BOOL prepare_dispatch(D3dContext *c)
{
    D3dDevice *device = c->child.device;
    if (!c->stages[D3D_CS].shader)
        return FALSE;
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd || !emit_tables(c, cmd, TRUE))
        return FALSE;
    if (c->query_count)
        D3dQueriesOpen(c, cmd, FALSE);
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_COMPUTE_BIT;
    device->vk.vkCmdBindShadersEXT(cmd, 1, &stage, &c->stages[D3D_CS].shader->shader);
    return TRUE;
}

static void STDMETHODCALLTYPE dispatch(ID3D11DeviceContext4 *iface, UINT x, UINT y, UINT z)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (!x || !y || !z || !prepare_dispatch(c))
        return;
    c->child.device->vk.vkCmdDispatch(c->cmd->buffer, x, y, z);
    c->pending_barrier = TRUE;
}

static void STDMETHODCALLTYPE dispatch_indirect(ID3D11DeviceContext4 *iface, ID3D11Buffer *buffer, UINT offset)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    D3dResource *r = D3dResourceFrom((ID3D11Resource *)buffer);
    if (!r || !prepare_dispatch(c))
        return;
    D3dContextUse(c, r);
    VkBuffer args;
    VkDeviceSize base;
    D3dContextBufferRange(c, r, &args, &base);
    c->child.device->vk.vkCmdDispatchIndirect(c->cmd->buffer, args, base + offset);
    c->pending_barrier = TRUE;
}

/* ------------------------------------------------------------------ queries and predication */

static void STDMETHODCALLTYPE begin(ID3D11DeviceContext4 *iface, ID3D11Asynchronous *async)
{
    D3D_SYNC(impl(iface));
    D3dQuery *q = D3dQueryFrom(async);
    if (q)
        D3dQueryBegin(impl(iface), q);
}

static void STDMETHODCALLTYPE end(ID3D11DeviceContext4 *iface, ID3D11Asynchronous *async)
{
    D3D_SYNC(impl(iface));
    D3dQuery *q = D3dQueryFrom(async);
    if (q)
        D3dQueryEnd(impl(iface), q);
}

static HRESULT STDMETHODCALLTYPE get_data(ID3D11DeviceContext4 *iface, ID3D11Asynchronous *async, void *data,
                                          UINT size, UINT flags)
{
    D3D_SYNC(impl(iface));
    D3dQuery *q = D3dQueryFrom(async);
    return q ? D3dQueryGetData(impl(iface), q, data, size, flags) : E_INVALIDARG;
}

static void STDMETHODCALLTYPE set_predication(ID3D11DeviceContext4 *iface, ID3D11Predicate *predicate, BOOL value)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (predicate)
        predicate->lpVtbl->AddRef(predicate);
    if (c->predicate)
        c->predicate->lpVtbl->Release(c->predicate);
    c->predicate = predicate;
    c->predicate_value = value;
}

static void STDMETHODCALLTYPE get_predication(ID3D11DeviceContext4 *iface, ID3D11Predicate **predicate,
                                              BOOL *value)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (predicate) {
        *predicate = c->predicate;
        if (c->predicate)
            c->predicate->lpVtbl->AddRef(c->predicate);
    }
    if (value)
        *value = c->predicate_value;
}

/* ------------------------------------------------------------------ the rest of ID3D11DeviceContext */

static void STDMETHODCALLTYPE map_unmap(ID3D11DeviceContext4 *iface, ID3D11Resource *resource, UINT sub)
{
    D3D_SYNC(impl(iface));
    D3dCtxUnmap(impl(iface), resource, sub);
}

static HRESULT STDMETHODCALLTYPE map(ID3D11DeviceContext4 *iface, ID3D11Resource *resource, UINT sub,
                                     D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped)
{
    D3D_SYNC(impl(iface));
    return D3dCtxMap(impl(iface), resource, sub, type, flags, mapped);
}

static void STDMETHODCALLTYPE copy_subresource_region(ID3D11DeviceContext4 *iface, ID3D11Resource *dst,
                                                      UINT dst_sub, UINT x, UINT y, UINT z, ID3D11Resource *src,
                                                      UINT src_sub, const D3D11_BOX *box)
{
    D3D_SYNC(impl(iface));
    D3dCtxCopySubresourceRegion(impl(iface), dst, dst_sub, x, y, z, src, src_sub, box, 0);
}

static void STDMETHODCALLTYPE copy_subresource_region1(ID3D11DeviceContext4 *iface, ID3D11Resource *dst,
                                                       UINT dst_sub, UINT x, UINT y, UINT z, ID3D11Resource *src,
                                                       UINT src_sub, const D3D11_BOX *box, UINT flags)
{
    D3D_SYNC(impl(iface));
    D3dCtxCopySubresourceRegion(impl(iface), dst, dst_sub, x, y, z, src, src_sub, box, flags);
}

static void STDMETHODCALLTYPE copy_resource(ID3D11DeviceContext4 *iface, ID3D11Resource *dst, ID3D11Resource *src)
{
    D3D_SYNC(impl(iface));
    D3dCtxCopyResource(impl(iface), dst, src);
}

static void STDMETHODCALLTYPE update_subresource(ID3D11DeviceContext4 *iface, ID3D11Resource *dst, UINT dst_sub,
                                                 const D3D11_BOX *box, const void *data, UINT row_pitch,
                                                 UINT depth_pitch)
{
    D3D_SYNC(impl(iface));
    D3dCtxUpdateSubresource(impl(iface), dst, dst_sub, box, data, row_pitch, depth_pitch, 0);
}

static void STDMETHODCALLTYPE update_subresource1(ID3D11DeviceContext4 *iface, ID3D11Resource *dst, UINT dst_sub,
                                                  const D3D11_BOX *box, const void *data, UINT row_pitch,
                                                  UINT depth_pitch, UINT flags)
{
    D3D_SYNC(impl(iface));
    D3dCtxUpdateSubresource(impl(iface), dst, dst_sub, box, data, row_pitch, depth_pitch, flags);
}

static void STDMETHODCALLTYPE copy_structure_count(ID3D11DeviceContext4 *iface, ID3D11Buffer *dst, UINT offset,
                                                   ID3D11UnorderedAccessView *src)
{
    D3D_SYNC(impl(iface));
    D3dCtxCopyStructureCount(impl(iface), dst, offset, src);
}

static void STDMETHODCALLTYPE clear_render_target_view(ID3D11DeviceContext4 *iface, ID3D11RenderTargetView *view,
                                                       const FLOAT color[4])
{
    D3D_SYNC(impl(iface));
    D3dCtxClearRenderTargetView(impl(iface), view, color);
}

static void STDMETHODCALLTYPE clear_uav_uint(ID3D11DeviceContext4 *iface, ID3D11UnorderedAccessView *view,
                                             const UINT values[4])
{
    D3D_SYNC(impl(iface));
    D3dCtxClearUnorderedAccessViewUint(impl(iface), view, values);
}

static void STDMETHODCALLTYPE clear_uav_float(ID3D11DeviceContext4 *iface, ID3D11UnorderedAccessView *view,
                                              const FLOAT values[4])
{
    D3D_SYNC(impl(iface));
    D3dCtxClearUnorderedAccessViewFloat(impl(iface), view, values);
}

static void STDMETHODCALLTYPE clear_depth_stencil_view(ID3D11DeviceContext4 *iface, ID3D11DepthStencilView *view,
                                                       UINT flags, FLOAT depth, UINT8 stencil)
{
    D3D_SYNC(impl(iface));
    D3dCtxClearDepthStencilView(impl(iface), view, flags, depth, stencil);
}

static void STDMETHODCALLTYPE generate_mips(ID3D11DeviceContext4 *iface, ID3D11ShaderResourceView *view)
{
    D3D_SYNC(impl(iface));
    D3dCtxGenerateMips(impl(iface), view);
}

static void STDMETHODCALLTYPE set_resource_min_lod(ID3D11DeviceContext4 *iface, ID3D11Resource *resource, FLOAT lod)
{
}

static FLOAT STDMETHODCALLTYPE get_resource_min_lod(ID3D11DeviceContext4 *iface, ID3D11Resource *resource)
{
    return 0.0f;
}

static void STDMETHODCALLTYPE resolve_subresource(ID3D11DeviceContext4 *iface, ID3D11Resource *dst, UINT dst_sub,
                                                  ID3D11Resource *src, UINT src_sub, DXGI_FORMAT format)
{
    D3D_SYNC(impl(iface));
    D3dCtxResolveSubresource(impl(iface), dst, dst_sub, src, src_sub, format);
}

/* ------------------------------------------------------------------ command lists */

typedef struct D3dCommandList {
    ID3D11CommandList iface;
    D3dChild child;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    D3dRingChunk **chunks;
    UINT chunk_count;
    D3dResource **resources;
    UINT resource_count;
    IUnknown **held;
    UINT held_count;
    UINT64 last_serial;
} D3dCommandList;

static D3dCommandList *list_impl(void *iface) { return CONTAINING_RECORD(iface, D3dCommandList, iface); }

static HRESULT STDMETHODCALLTYPE list_query(void *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild) ||
        IsEqualGUID(riid, &IID_ID3D11CommandList)) {
        InterlockedIncrement(&list_impl(iface)->child.refs);
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE list_add_ref(void *iface) { return InterlockedIncrement(&list_impl(iface)->child.refs); }

/* what the list owns is freed once its last execution is done. */
static ULONG STDMETHODCALLTYPE list_release(void *iface)
{
    D3dCommandList *list = list_impl(iface);
    ULONG refs = InterlockedDecrement(&list->child.refs);
    if (refs)
        return refs;
    D3dDevice *device = list->child.device;
    for (UINT i = 0; i < list->chunk_count; i++)
        D3dRingChunkRelease(device, list->chunks[i], list->last_serial);
    for (UINT i = 0; i < list->resource_count; i++)
        D3dResourceRelease(list->resources[i]);
    for (UINT i = 0; i < list->held_count; i++)
        list->held[i]->lpVtbl->Release(list->held[i]);
    D3dDefer(device, list->last_serial, D3D_GARBAGE_COMMAND_POOL, (UINT64)list->pool);
    HeapFree(GetProcessHeap(), 0, list->held);
    HeapFree(GetProcessHeap(), 0, list->chunks);
    HeapFree(GetProcessHeap(), 0, list->resources);
    D3dChildRelease(&list->child);
    HeapFree(GetProcessHeap(), 0, list);
    return 0;
}

static void STDMETHODCALLTYPE list_get_device(void *iface, ID3D11Device **device)
{
    D3dDevice *d = list_impl(iface)->child.device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = (ID3D11Device *)&d->iface;
}

static HRESULT STDMETHODCALLTYPE list_get_private_data(void *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&list_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE list_set_private_data(void *iface, REFGUID guid, UINT size, const void *data)
{
    return MimePrivateSet(&list_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE list_set_private_data_interface(void *iface, REFGUID guid, const IUnknown *data)
{
    return MimePrivateSetInterface(&list_impl(iface)->child.data, guid, data);
}

static UINT STDMETHODCALLTYPE list_get_context_flags(void *iface) { return 0; }

static const ID3D11CommandListVtbl list_vtbl = {
    VTBL_FN(list_query),           VTBL_FN(list_add_ref),          VTBL_FN(list_release),
    VTBL_FN(list_get_device),      VTBL_FN(list_get_private_data), VTBL_FN(list_set_private_data),
    VTBL_FN(list_set_private_data_interface), VTBL_FN(list_get_context_flags),
};

static void clear_state(D3dContext *c);

/*
 * submits what the immediate context recorded so far, then the list's
 * command buffer, whose serial the list's resources take.
 */
static void STDMETHODCALLTYPE execute_command_list(ID3D11DeviceContext4 *iface, ID3D11CommandList *iface_list,
                                                   BOOL restore)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    if (!iface_list)
        return;
    D3dCommandList *list = list_impl(iface_list);
    if (c->type != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        D3dLog("d3d11: command lists executed by deferred contexts are not implemented");
        return;
    }
    D3dContextFlush(c);
    UINT64 serial = D3dSubmit(c->child.device, list->cmd, nullptr, nullptr);
    list->last_serial = serial;
    for (UINT i = 0; i < list->resource_count; i++)
        if (list->resources[i]->last_use < serial)
            list->resources[i]->last_use = serial;
    if (!restore)
        clear_state(c);
}

static void clear_state(D3dContext *c)
{
    for (UINT s = 0; s < D3D_STAGES; s++) {
        D3dStageState *st = &c->stages[s];
        ref_D3dShader(c, &st->shader, nullptr);
        for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
            ref_resource(&st->cb[i], nullptr);
        for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
            ref_view(c, &st->srv[i], nullptr);
        for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
            ref_D3dSamplerState(c, &st->sampler[i], nullptr);
        for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
            ref_view(c, &st->uav[i], nullptr);
        st->table_valid = FALSE;
    }
    ref_D3dInputLayout(c, &c->layout, nullptr);
    for (UINT i = 0; i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; i++) {
        ref_resource(&c->vb[i], nullptr);
        c->vb_stride[i] = c->vb_offset[i] = 0;
    }
    c->vb_high = 0;
    ref_resource(&c->ib, nullptr);
    c->ib_format = DXGI_FORMAT_UNKNOWN;
    c->ib_offset = 0;
    c->topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ref_D3dRasterizerState(c, &c->rasterizer, nullptr);
    ref_D3dBlendState(c, &c->blend, nullptr);
    ref_D3dDepthStencilState(c, &c->depth_stencil, nullptr);
    for (UINT i = 0; i < 4; i++)
        c->blend_factor[i] = 1.0f;
    c->sample_mask = 0xFFFFFFFF;
    c->stencil_ref = 0;
    c->viewport_count = c->scissor_count = 0;
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
        ref_view(c, &c->rtv[i], nullptr);
    c->rtv_count = 0;
    ref_view(c, &c->dsv, nullptr);
    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
        ref_view(c, &c->om_uav[i], nullptr);
    for (UINT i = 0; i < D3D11_SO_BUFFER_SLOT_COUNT; i++)
        ref_resource(&c->so[i], nullptr);
    set_predication(&c->iface, nullptr, FALSE);
    c->outputs_dirty = TRUE;
    c->targets_dirty = c->shaders_dirty = c->ia_dirty = c->vertex_input_dirty = TRUE;
    c->rs_dirty = c->om_dirty = c->viewports_dirty = TRUE;
}

static void STDMETHODCALLTYPE context_clear_state(ID3D11DeviceContext4 *iface) {
    D3D_SYNC(impl(iface));
    clear_state(impl(iface));
}

static void STDMETHODCALLTYPE flush(ID3D11DeviceContext4 *iface) {
    D3D_SYNC(impl(iface));
    D3dContextFlush(impl(iface));
}

static D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE get_type(ID3D11DeviceContext4 *iface) { return impl(iface)->type; }

static UINT STDMETHODCALLTYPE get_context_flags(ID3D11DeviceContext4 *iface) { return 0; }

/* the recording becomes a command list and the context starts a new one. */
static HRESULT STDMETHODCALLTYPE finish_command_list(ID3D11DeviceContext4 *iface, BOOL restore,
                                                     ID3D11CommandList **out)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    D3dDevice *device = c->child.device;
    if (out)
        *out = nullptr;
    if (c->type != D3D11_DEVICE_CONTEXT_DEFERRED || !out)
        return DXGI_ERROR_INVALID_CALL;
    begin_commands(c);
    if (!c->cmd)
        return E_OUTOFMEMORY;
    D3dCommandList *list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*list));
    if (!list)
        return E_OUTOFMEMORY;
    if (c->rendering) {
        device->vk.vkCmdEndRendering(c->cmd->buffer);
        c->rendering = FALSE;
    }
    D3dBarrier(c->cmd->buffer, device);
    device->vk.vkEndCommandBuffer(c->cmd->buffer);
    list->iface.lpVtbl = &list_vtbl;
    D3dChildInit(&list->child, device);
    list->pool = c->list_pool;
    list->cmd = c->list_cmd.buffer;
    list->chunks = c->list_chunks;
    list->chunk_count = c->list_chunk_count;
    list->resources = c->list_resources;
    list->resource_count = c->list_resource_count;
    c->list_pool = VK_NULL_HANDLE;
    c->cmd = nullptr;
    /* the list also owns what stays bound: it holds a reference, or clearing the state hands one over. */
    if (restore) {
        for (UINT s = 0; s < D3D_STAGES; s++) {
            D3dStageState *st = &c->stages[s];
            if (st->shader)
                hold(c, (IUnknown *)&st->shader->iface.vs);
            for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
                if (st->srv[i])
                    hold(c, (IUnknown *)&st->srv[i]->iface);
            for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
                if (st->sampler[i])
                    hold(c, (IUnknown *)&st->sampler[i]->iface);
            for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
                if (st->uav[i])
                    hold(c, (IUnknown *)&st->uav[i]->iface);
        }
        for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
            if (c->rtv[i])
                hold(c, (IUnknown *)&c->rtv[i]->iface);
        if (c->dsv)
            hold(c, (IUnknown *)&c->dsv->iface);
        for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
            if (c->om_uav[i])
                hold(c, (IUnknown *)&c->om_uav[i]->iface);
    } else {
        clear_state(c);
    }
    list->held = c->list_held;
    list->held_count = c->list_held_count;
    c->list_held = nullptr;
    c->list_held_count = c->list_held_capacity = 0;
    c->list_chunks = nullptr;
    c->list_chunk_count = c->list_chunk_capacity = 0;
    c->list_resources = nullptr;
    c->list_resource_count = c->list_resource_capacity = 0;
    c->list_map_count = 0;
    c->list_range_count = 0;
    c->pending_barrier = FALSE;
    *out = &list->iface;
    return S_OK;
}

/* ------------------------------------------------------------------ ID3D11DeviceContext1 to 4 */

static void STDMETHODCALLTYPE discard_resource(ID3D11DeviceContext4 *iface, ID3D11Resource *resource) {}

static void STDMETHODCALLTYPE discard_view(ID3D11DeviceContext4 *iface, ID3D11View *view) {}

static void STDMETHODCALLTYPE discard_view1(ID3D11DeviceContext4 *iface, ID3D11View *view, const D3D11_RECT *rects,
                                            UINT count)
{
    D3D_SYNC(impl(iface));
}

static void STDMETHODCALLTYPE swap_device_context_state(ID3D11DeviceContext4 *iface, ID3DDeviceContextState *state,
                                                        ID3DDeviceContextState **previous)
{
    if (previous)
        *previous = nullptr;
}

static void STDMETHODCALLTYPE clear_view(ID3D11DeviceContext4 *iface, ID3D11View *view, const FLOAT color[4],
                                         const D3D11_RECT *rects, UINT count)
{
    D3D_SYNC(impl(iface));
    D3dCtxClearView(impl(iface), view, color, rects, count);
}

static HRESULT STDMETHODCALLTYPE update_tile_mappings(ID3D11DeviceContext4 *iface, ID3D11Resource *resource,
                                                      UINT region_count,
                                                      const D3D11_TILED_RESOURCE_COORDINATE *coordinates,
                                                      const D3D11_TILE_REGION_SIZE *sizes, ID3D11Buffer *pool,
                                                      UINT range_count, const UINT *range_flags,
                                                      const UINT *pool_starts, const UINT *range_tiles, UINT flags)
{
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE copy_tile_mappings(ID3D11DeviceContext4 *iface, ID3D11Resource *dst,
                                                    const D3D11_TILED_RESOURCE_COORDINATE *dst_start,
                                                    ID3D11Resource *src,
                                                    const D3D11_TILED_RESOURCE_COORDINATE *src_start,
                                                    const D3D11_TILE_REGION_SIZE *size, UINT flags)
{
    return E_INVALIDARG;
}

static void STDMETHODCALLTYPE copy_tiles(ID3D11DeviceContext4 *iface, ID3D11Resource *resource,
                                         const D3D11_TILED_RESOURCE_COORDINATE *start,
                                         const D3D11_TILE_REGION_SIZE *size, ID3D11Buffer *buffer, UINT64 offset,
                                         UINT flags)
{
}

static void STDMETHODCALLTYPE update_tiles(ID3D11DeviceContext4 *iface, ID3D11Resource *resource,
                                           const D3D11_TILED_RESOURCE_COORDINATE *start,
                                           const D3D11_TILE_REGION_SIZE *size, const void *data, UINT flags)
{
}

static HRESULT STDMETHODCALLTYPE resize_tile_pool(ID3D11DeviceContext4 *iface, ID3D11Buffer *pool, UINT64 size)
{
    return E_INVALIDARG;
}

static void STDMETHODCALLTYPE tiled_resource_barrier(ID3D11DeviceContext4 *iface, ID3D11DeviceChild *before,
                                                     ID3D11DeviceChild *after)
{
}

static BOOL STDMETHODCALLTYPE is_annotation_enabled(ID3D11DeviceContext4 *iface) { return FALSE; }

static void STDMETHODCALLTYPE set_marker_int(ID3D11DeviceContext4 *iface, LPCWSTR label, INT data) {}

static void STDMETHODCALLTYPE begin_event_int(ID3D11DeviceContext4 *iface, LPCWSTR label, INT data) {}

static void STDMETHODCALLTYPE end_event(ID3D11DeviceContext4 *iface) {}

static void STDMETHODCALLTYPE flush1(ID3D11DeviceContext4 *iface, D3D11_CONTEXT_TYPE type, HANDLE event)
{
    D3D_SYNC(impl(iface));
    D3dContext *c = impl(iface);
    D3dContextFlush(c);
    if (event) {
        D3dWaitSerial(c->child.device, c->child.device->next_serial - 1);
        SetEvent(event);
    }
}

static void STDMETHODCALLTYPE set_hardware_protection_state(ID3D11DeviceContext4 *iface, BOOL enable) {}

static void STDMETHODCALLTYPE get_hardware_protection_state(ID3D11DeviceContext4 *iface, BOOL *enabled)
{
    if (enabled)
        *enabled = FALSE;
}

static HRESULT STDMETHODCALLTYPE signal_fence(ID3D11DeviceContext4 *iface, ID3D11Fence *fence, UINT64 value)
{
    D3D_SYNC(impl(iface));
    return D3dFenceSignal(impl(iface), fence, value);
}

static HRESULT STDMETHODCALLTYPE wait_fence(ID3D11DeviceContext4 *iface, ID3D11Fence *fence, UINT64 value)
{
    D3D_SYNC(impl(iface));
    return D3dFenceWait(impl(iface), fence, value);
}

/* ------------------------------------------------------------------ IUnknown and ID3D11DeviceChild */

static HRESULT STDMETHODCALLTYPE context_query(ID3D11DeviceContext4 *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    D3dContext *c = impl(iface);
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild) ||
        IsEqualGUID(riid, &IID_ID3D11DeviceContext) || IsEqualGUID(riid, &IID_ID3D11DeviceContext1) ||
        IsEqualGUID(riid, &IID_ID3D11DeviceContext2) || IsEqualGUID(riid, &IID_ID3D11DeviceContext3) ||
        IsEqualGUID(riid, &IID_ID3D11DeviceContext4))
        *out = &c->iface;
    else if (IsEqualGUID(riid, &IID_ID3DUserDefinedAnnotation))
        *out = &c->annotation;
    else if (IsEqualGUID(riid, &IID_ID3D11Multithread))
        *out = D3dMultithreadInterface(c);
    else
        *out = D3dVideoContextInterface(c, riid);
    if (!*out)
        return E_NOINTERFACE;
    iface->lpVtbl->AddRef(iface);
    return S_OK;
}

/* the immediate context shares its device's reference count, deferred ones have their own. */
static ULONG STDMETHODCALLTYPE context_add_ref(ID3D11DeviceContext4 *iface)
{
    D3dContext *c = impl(iface);
    D3dDevice *device = c->child.device;
    if (c->type == D3D11_DEVICE_CONTEXT_DEFERRED)
        return InterlockedIncrement(&c->child.refs);
    return device->iface.lpVtbl->AddRef(&device->iface);
}

static ULONG STDMETHODCALLTYPE context_release(ID3D11DeviceContext4 *iface)
{
    D3dContext *c = impl(iface);
    D3dDevice *device = c->child.device;
    if (c->type == D3D11_DEVICE_CONTEXT_DEFERRED) {
        ULONG refs = InterlockedDecrement(&c->child.refs);
        if (!refs) {
            D3dContextDestroy(c);
            device->iface.lpVtbl->Release(&device->iface);
        }
        return refs;
    }
    return device->iface.lpVtbl->Release(&device->iface);
}

static void STDMETHODCALLTYPE context_get_device(ID3D11DeviceContext4 *iface, ID3D11Device **device)
{
    D3dDevice *d = impl(iface)->child.device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = (ID3D11Device *)&d->iface;
}

static HRESULT STDMETHODCALLTYPE context_get_private_data(ID3D11DeviceContext4 *iface, REFGUID guid, UINT *size,
                                                          void *data)
{
    return MimePrivateGet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE context_set_private_data(ID3D11DeviceContext4 *iface, REFGUID guid, UINT size,
                                                          const void *data)
{
    return MimePrivateSet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE context_set_private_data_interface(ID3D11DeviceContext4 *iface, REFGUID guid,
                                                                    const IUnknown *data)
{
    return MimePrivateSetInterface(&impl(iface)->child.data, guid, data);
}

static const ID3D11DeviceContext4Vtbl context_vtbl = {
    context_query,
    context_add_ref,
    context_release,
    context_get_device,
    context_get_private_data,
    context_set_private_data,
    context_set_private_data_interface,
    vs_set_constant_buffers,
    ps_set_shader_resources,
    ps_set_shader,
    ps_set_samplers,
    vs_set_shader,
    draw_indexed,
    draw,
    map,
    map_unmap,
    ps_set_constant_buffers,
    ia_set_input_layout,
    ia_set_vertex_buffers,
    ia_set_index_buffer,
    draw_indexed_instanced,
    draw_instanced,
    gs_set_constant_buffers,
    gs_set_shader,
    ia_set_primitive_topology,
    vs_set_shader_resources,
    vs_set_samplers,
    begin,
    end,
    get_data,
    set_predication,
    gs_set_shader_resources,
    gs_set_samplers,
    om_set_render_targets,
    om_set_render_targets_and_uavs,
    om_set_blend_state,
    om_set_depth_stencil_state,
    so_set_targets,
    draw_auto,
    draw_indexed_instanced_indirect,
    draw_instanced_indirect,
    dispatch,
    dispatch_indirect,
    rs_set_state,
    rs_set_viewports,
    rs_set_scissor_rects,
    copy_subresource_region,
    copy_resource,
    update_subresource,
    copy_structure_count,
    clear_render_target_view,
    clear_uav_uint,
    clear_uav_float,
    clear_depth_stencil_view,
    generate_mips,
    set_resource_min_lod,
    get_resource_min_lod,
    resolve_subresource,
    execute_command_list,
    hs_set_shader_resources,
    hs_set_shader,
    hs_set_samplers,
    hs_set_constant_buffers,
    ds_set_shader_resources,
    ds_set_shader,
    ds_set_samplers,
    ds_set_constant_buffers,
    cs_set_shader_resources,
    cs_set_unordered_access_views,
    cs_set_shader,
    cs_set_samplers,
    cs_set_constant_buffers,
    vs_get_constant_buffers,
    ps_get_shader_resources,
    ps_get_shader,
    ps_get_samplers,
    vs_get_shader,
    ps_get_constant_buffers,
    ia_get_input_layout,
    ia_get_vertex_buffers,
    ia_get_index_buffer,
    gs_get_constant_buffers,
    gs_get_shader,
    ia_get_primitive_topology,
    vs_get_shader_resources,
    vs_get_samplers,
    get_predication,
    gs_get_shader_resources,
    gs_get_samplers,
    om_get_render_targets,
    om_get_render_targets_and_uavs,
    om_get_blend_state,
    om_get_depth_stencil_state,
    so_get_targets,
    rs_get_state,
    rs_get_viewports,
    rs_get_scissor_rects,
    hs_get_shader_resources,
    hs_get_shader,
    hs_get_samplers,
    hs_get_constant_buffers,
    ds_get_shader_resources,
    ds_get_shader,
    ds_get_samplers,
    ds_get_constant_buffers,
    cs_get_shader_resources,
    cs_get_unordered_access_views,
    cs_get_shader,
    cs_get_samplers,
    cs_get_constant_buffers,
    context_clear_state,
    flush,
    get_type,
    get_context_flags,
    finish_command_list,
    copy_subresource_region1,
    update_subresource1,
    discard_resource,
    discard_view,
    vs_set_constant_buffers1,
    hs_set_constant_buffers1,
    ds_set_constant_buffers1,
    gs_set_constant_buffers1,
    ps_set_constant_buffers1,
    cs_set_constant_buffers1,
    vs_get_constant_buffers1,
    hs_get_constant_buffers1,
    ds_get_constant_buffers1,
    gs_get_constant_buffers1,
    ps_get_constant_buffers1,
    cs_get_constant_buffers1,
    swap_device_context_state,
    clear_view,
    discard_view1,
    update_tile_mappings,
    copy_tile_mappings,
    copy_tiles,
    update_tiles,
    resize_tile_pool,
    tiled_resource_barrier,
    is_annotation_enabled,
    set_marker_int,
    begin_event_int,
    end_event,
    flush1,
    set_hardware_protection_state,
    get_hardware_protection_state,
    signal_fence,
    wait_fence,
};

/* ------------------------------------------------------------------ ID3DUserDefinedAnnotation */

static D3dContext *annotation_impl(ID3DUserDefinedAnnotation *iface)
{
    return CONTAINING_RECORD(iface, D3dContext, annotation);
}

static HRESULT STDMETHODCALLTYPE annotation_query(ID3DUserDefinedAnnotation *iface, REFIID riid, void **out)
{
    return context_query(&annotation_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE annotation_add_ref(ID3DUserDefinedAnnotation *iface)
{
    return context_add_ref(&annotation_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE annotation_release(ID3DUserDefinedAnnotation *iface)
{
    return context_release(&annotation_impl(iface)->iface);
}

/* annotations only matter to graphics debuggers, which never attach here, so no event depth is kept. */
static INT STDMETHODCALLTYPE annotation_begin_event(ID3DUserDefinedAnnotation *iface, LPCWSTR name) { return -1; }

static INT STDMETHODCALLTYPE annotation_end_event(ID3DUserDefinedAnnotation *iface) { return -1; }

static void STDMETHODCALLTYPE annotation_set_marker(ID3DUserDefinedAnnotation *iface, LPCWSTR name) {}

static BOOL STDMETHODCALLTYPE annotation_get_status(ID3DUserDefinedAnnotation *iface) { return FALSE; }

static const ID3DUserDefinedAnnotationVtbl annotation_vtbl = {
    annotation_query,       annotation_add_ref,    annotation_release,
    annotation_begin_event, annotation_end_event,  annotation_set_marker,
    annotation_get_status,
};

/* ------------------------------------------------------------------ ID3D11Multithread */

static D3dContext *mt_impl(ID3D11Multithread *iface) { return CONTAINING_RECORD(iface, D3dContext, multithread); }

static HRESULT STDMETHODCALLTYPE mt_query(ID3D11Multithread *iface, REFIID riid, void **out)
{
    return context_query(&mt_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE mt_add_ref(ID3D11Multithread *iface) { return context_add_ref(&mt_impl(iface)->iface); }

static ULONG STDMETHODCALLTYPE mt_release(ID3D11Multithread *iface) { return context_release(&mt_impl(iface)->iface); }

static void STDMETHODCALLTYPE mt_enter(ID3D11Multithread *iface) { EnterCriticalSection(&mt_impl(iface)->lock); }

static void STDMETHODCALLTYPE mt_leave(ID3D11Multithread *iface) { LeaveCriticalSection(&mt_impl(iface)->lock); }

BOOL D3dSetProtected(D3dContext *c, BOOL protect)
{
    EnterCriticalSection(&c->lock);
    BOOL old = c->protected_;
    __atomic_store_n(&c->protected_, protect != 0, __ATOMIC_RELAXED);
    LeaveCriticalSection(&c->lock);
    return old;
}

static BOOL STDMETHODCALLTYPE mt_set_protected(ID3D11Multithread *iface, BOOL protect)
{
    return D3dSetProtected(mt_impl(iface), protect);
}

static BOOL STDMETHODCALLTYPE mt_get_protected(ID3D11Multithread *iface)
{
    return __atomic_load_n(&mt_impl(iface)->protected_, __ATOMIC_RELAXED) != 0;
}

/* only the immediate context of a device that is not single threaded has one. */
void *D3dMultithreadInterface(D3dContext *c)
{
    if (c->type != D3D11_DEVICE_CONTEXT_IMMEDIATE || (c->child.device->flags & D3D11_CREATE_DEVICE_SINGLETHREADED))
        return nullptr;
    return &c->multithread;
}

static const ID3D11MultithreadVtbl mt_vtbl = {
    mt_query, mt_add_ref, mt_release, mt_enter, mt_leave, mt_set_protected, mt_get_protected,
};

/* ------------------------------------------------------------------ creation */

HRESULT D3dCreateContext(D3dDevice *device, D3D11_DEVICE_CONTEXT_TYPE type, D3dContext **out)
{
    D3dContext *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c)
        return E_OUTOFMEMORY;
    c->iface.lpVtbl = &context_vtbl;
    c->annotation.lpVtbl = &annotation_vtbl;
    c->multithread.lpVtbl = &mt_vtbl;
    D3dVideoContextInit(c);
    c->type = type;
    /* the immediate context holds no reference on its device; deferred ones do. */
    c->child.device = device;
    c->child.refs = 1;
    if (type == D3D11_DEVICE_CONTEXT_DEFERRED)
        device->iface.lpVtbl->AddRef(&device->iface);
    InitializeCriticalSection(&c->lock);
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = device->queue_family;
    if (device->vk.vkCreateCommandPool(device->vk.device, &pool, nullptr, &c->pool) != VK_SUCCESS) {
        DeleteCriticalSection(&c->lock);
        HeapFree(GetProcessHeap(), 0, c);
        return E_FAIL;
    }
    clear_state(c);
    *out = c;
    return S_OK;
}

void D3dContextDestroy(D3dContext *c)
{
    D3dDevice *device = c->child.device;
    clear_state(c);
    D3dQueriesRelease(c);
    /* an unfinished recording never reached the gpu. */
    for (UINT i = 0; i < c->list_chunk_count; i++)
        D3dRingChunkRelease(device, c->list_chunks[i], 0);
    for (UINT i = 0; i < c->list_resource_count; i++)
        D3dResourceRelease(c->list_resources[i]);
    if (c->list_pool)
        device->vk.vkDestroyCommandPool(device->vk.device, c->list_pool, nullptr);
    HeapFree(GetProcessHeap(), 0, c->list_chunks);
    HeapFree(GetProcessHeap(), 0, c->list_resources);
    HeapFree(GetProcessHeap(), 0, c->list_ranges);
    for (UINT i = 0; i < c->list_held_count; i++)
        c->list_held[i]->lpVtbl->Release(c->list_held[i]);
    HeapFree(GetProcessHeap(), 0, c->list_held);
    device->vk.vkDestroyCommandPool(device->vk.device, c->pool, nullptr);
    HeapFree(GetProcessHeap(), 0, c->buffers);
    MimePrivateClear(&c->child.data);
    DeleteCriticalSection(&c->lock);
    HeapFree(GetProcessHeap(), 0, c);
}
