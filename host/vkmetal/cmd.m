/*
 * command pools and buffers, and the recording of state, rendering, draws and dispatches. a draw resolves its
 * Metal pipeline and depth stencil states here, on the thread that records, and emits only what changed since
 * the last draw of the render pass, so encoding a submission is a replay.
 */
#include "vkm_cmd.h"

/* ------------------------------------------------------------------ the command list. */

void *vkm_emit(VkCommandBuffer cmd, uint32_t op, size_t size)
{
    size = MIME_ALIGN_UP(size, (size_t)8);
    if (cmd->cmd_size + size > cmd->cmd_capacity) {
        size_t capacity = MIME_MAX(cmd->cmd_capacity * 2, cmd->cmd_size + size + 4096);
        uint8_t *grown = realloc(cmd->cmds, capacity);
        if (!grown) {
            cmd->status = VK_ERROR_OUT_OF_HOST_MEMORY;
            return nullptr;
        }
        cmd->cmds = grown;
        cmd->cmd_capacity = capacity;
    }
    VkmOp *o = (VkmOp *)(cmd->cmds + cmd->cmd_size);
    memset(o, 0, size);
    o->op = op;
    o->size = (uint32_t)size;
    cmd->cmd_size += size;
    return o;
}

void vkm_forget(VkCommandBuffer cmd)
{
    VkmGfxState *s = cmd->state;
    s->set_pipeline = nullptr;
    s->set_depth = nullptr;
    s->set_compute = nullptr;
    memset(s->set_buffers, 0, sizeof(s->set_buffers));
    memset(s->set_strides, 0, sizeof(s->set_strides));
    s->pipeline_dirty = s->depth_dirty = s->raster_dirty = s->viewport_dirty = s->scissor_dirty = true;
    s->ref_dirty = s->constants_dirty = true;
    s->push_dirty[VKM_GFX] = s->push_dirty[VKM_COMPUTE] = true;
}

static void state_reset(VkmGfxState *s)
{
    void *legacy = s->legacy;
    memset(s, 0, sizeof(*s));
    s->legacy = legacy;
    s->dyn.samples = 1;
    s->dyn.sample_mask = ~0u;
    s->dyn.depth_clip = 1;
    for (uint32_t i = 0; i < VKM_COLORS; i++)
        s->dyn.blend[i].write = 0xf;
    s->index_type = MTLIndexTypeUInt16;
}

/* ------------------------------------------------------------------ pools and buffers. */

static VKAPI_ATTR VkResult VKAPI_CALL CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *info,
                                                        const VkAllocationCallbacks *a, VkCommandPool *out)
{
    VkCommandPool pool = vkm_zalloc(sizeof(*pool));
    if (!pool)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->device = device;
    *out = pool;
    return VK_SUCCESS;
}

static void buffer_reset(VkCommandBuffer cmd)
{
    for (VkmIcb *b = cmd->icbs; b; b = b->next)
        b->used = false;
    cmd->cmd_size = 0;
    vkm_chunks_release(cmd->device, cmd->chunks);
    cmd->chunks = nullptr;
    cmd->status = VK_SUCCESS;
    state_reset(cmd->state);
}

static void buffer_free(VkCommandBuffer cmd)
{
    for (VkmIcb *b = cmd->icbs, *next; b; b = next) {
        next = b->next;
        vkm_unresident(cmd->device, b->icb);
        [b->icb release];
        free(b);
    }
    vkm_chunks_release(cmd->device, cmd->chunks);
    free(cmd->cmds);
    free(cmd->state->legacy);
    free(cmd->state);
    free(cmd);
}

static VKAPI_ATTR void VKAPI_CALL DestroyCommandPool(VkDevice device, VkCommandPool pool,
                                                     const VkAllocationCallbacks *a)
{
    if (!pool)
        return;
    for (VkCommandBuffer c = pool->buffers, next; c; c = next) {
        next = c->next;
        buffer_free(c);
    }
    free(pool);
}

static VKAPI_ATTR VkResult VKAPI_CALL ResetCommandPool(VkDevice device, VkCommandPool pool,
                                                       VkCommandPoolResetFlags flags)
{
    for (VkCommandBuffer c = pool->buffers; c; c = c->next)
        buffer_reset(c);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL TrimCommandPool(VkDevice device, VkCommandPool pool, VkCommandPoolTrimFlags flags)
{
}

static VKAPI_ATTR VkResult VKAPI_CALL AllocateCommandBuffers(VkDevice device,
                                                             const VkCommandBufferAllocateInfo *info,
                                                             VkCommandBuffer *out)
{
    for (uint32_t i = 0; i < info->commandBufferCount; i++) {
        VkCommandBuffer c = vkm_zalloc(sizeof(*c));
        if (c && !(c->state = vkm_zalloc(sizeof(*c->state)))) {
            free(c);
            c = nullptr;
        }
        if (!c) {
            for (uint32_t k = 0; k < i; k++)
                out[k] = VK_NULL_HANDLE;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        c->device = device;
        c->pool = info->commandPool;
        c->level = info->level;
        state_reset(c->state);
        c->next = info->commandPool->buffers;
        info->commandPool->buffers = c;
        out[i] = c;
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL FreeCommandBuffers(VkDevice device, VkCommandPool pool, uint32_t count,
                                                     const VkCommandBuffer *buffers)
{
    for (uint32_t i = 0; i < count; i++) {
        VkCommandBuffer c = buffers[i];
        if (!c)
            continue;
        for (VkCommandBuffer *link = &pool->buffers; *link; link = &(*link)->next)
            if (*link == c) {
                *link = c->next;
                break;
            }
        buffer_free(c);
    }
}

static void begin_inherited(VkCommandBuffer cmd, const VkCommandBufferInheritanceInfo *inherit);

static VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer cmd, const VkCommandBufferBeginInfo *info)
{
    buffer_reset(cmd);
    if (cmd->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY && info->pInheritanceInfo &&
        (info->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT))
        begin_inherited(cmd, info->pInheritanceInfo);
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL EndCommandBuffer(VkCommandBuffer cmd)
{
    return cmd->status;
}

static VKAPI_ATTR VkResult VKAPI_CALL ResetCommandBuffer(VkCommandBuffer cmd, VkCommandBufferResetFlags flags)
{
    buffer_reset(cmd);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL CmdExecuteCommands(VkCommandBuffer cmd, uint32_t count,
                                                     const VkCommandBuffer *secondaries)
{
    for (uint32_t i = 0; i < count; i++) {
        VkCommandBuffer s = secondaries[i];
        if (!s->cmd_size)
            continue;
        uint8_t *at = vkm_emit(cmd, 0, s->cmd_size);
        if (!at)
            return;
        /* the copy keeps the secondary's ops and sizes; its upload memory stays the secondary's. */
        memcpy(at, s->cmds, s->cmd_size);
    }
    vkm_forget(cmd);
}

/* ------------------------------------------------------------------ dynamic state. */

#define STATE(cmd) ((cmd)->state)

static VKAPI_ATTR void VKAPI_CALL CmdSetViewport(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                                                 const VkViewport *viewports)
{
    VkmGfxState *s = STATE(cmd);
    for (uint32_t i = 0; i < count && first + i < VKM_VIEWPORTS; i++)
        s->dyn.viewports[first + i] = viewports[i];
    s->dyn.viewport_count = MIME_MAX(s->dyn.viewport_count, MIME_MIN(first + count, (uint32_t)VKM_VIEWPORTS));
    s->viewport_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetViewportWithCount(VkCommandBuffer cmd, uint32_t count,
                                                          const VkViewport *viewports)
{
    STATE(cmd)->dyn.viewport_count = 0;
    CmdSetViewport(cmd, 0, count, viewports);
    STATE(cmd)->dyn.viewport_count = MIME_MIN(count, (uint32_t)VKM_VIEWPORTS);
}

static VKAPI_ATTR void VKAPI_CALL CmdSetScissor(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                                                const VkRect2D *rects)
{
    VkmGfxState *s = STATE(cmd);
    for (uint32_t i = 0; i < count && first + i < VKM_VIEWPORTS; i++)
        s->dyn.scissors[first + i] = rects[i];
    s->dyn.scissor_count = MIME_MAX(s->dyn.scissor_count, MIME_MIN(first + count, (uint32_t)VKM_VIEWPORTS));
    s->scissor_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetScissorWithCount(VkCommandBuffer cmd, uint32_t count, const VkRect2D *rects)
{
    STATE(cmd)->dyn.scissor_count = 0;
    CmdSetScissor(cmd, 0, count, rects);
    STATE(cmd)->dyn.scissor_count = MIME_MIN(count, (uint32_t)VKM_VIEWPORTS);
}

static VKAPI_ATTR void VKAPI_CALL CmdSetLineWidth(VkCommandBuffer cmd, float width)
{
}

static VKAPI_ATTR void VKAPI_CALL CmdSetDepthBias(VkCommandBuffer cmd, float constant, float clamp, float slope)
{
    VkmGfxState *s = STATE(cmd);
    s->dyn.bias_constant = constant;
    s->dyn.bias_clamp = clamp;
    s->dyn.bias_slope = slope;
    s->raster_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetBlendConstants(VkCommandBuffer cmd, const float constants[4])
{
    memcpy(STATE(cmd)->dyn.blend_constants, constants, sizeof(float) * 4);
    STATE(cmd)->constants_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetDepthBounds(VkCommandBuffer cmd, float min, float max)
{
    STATE(cmd)->dyn.bounds_min = min;
    STATE(cmd)->dyn.bounds_max = max;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetStencilCompareMask(VkCommandBuffer cmd, VkStencilFaceFlags faces,
                                                           uint32_t mask)
{
    if (faces & VK_STENCIL_FACE_FRONT_BIT)
        STATE(cmd)->dyn.front_stencil.read = mask;
    if (faces & VK_STENCIL_FACE_BACK_BIT)
        STATE(cmd)->dyn.back_stencil.read = mask;
    STATE(cmd)->depth_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetStencilWriteMask(VkCommandBuffer cmd, VkStencilFaceFlags faces,
                                                         uint32_t mask)
{
    if (faces & VK_STENCIL_FACE_FRONT_BIT)
        STATE(cmd)->dyn.front_stencil.write = mask;
    if (faces & VK_STENCIL_FACE_BACK_BIT)
        STATE(cmd)->dyn.back_stencil.write = mask;
    STATE(cmd)->depth_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetStencilReference(VkCommandBuffer cmd, VkStencilFaceFlags faces,
                                                         uint32_t reference)
{
    if (faces & VK_STENCIL_FACE_FRONT_BIT)
        STATE(cmd)->dyn.ref_front = reference;
    if (faces & VK_STENCIL_FACE_BACK_BIT)
        STATE(cmd)->dyn.ref_back = reference;
    STATE(cmd)->ref_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetCullMode(VkCommandBuffer cmd, VkCullModeFlags mode)
{
    STATE(cmd)->dyn.cull = (uint8_t)mode;
    STATE(cmd)->raster_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetFrontFace(VkCommandBuffer cmd, VkFrontFace face)
{
    STATE(cmd)->dyn.front = (uint8_t)face;
    STATE(cmd)->raster_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetPrimitiveTopology(VkCommandBuffer cmd, VkPrimitiveTopology topology)
{
    VkmGfxState *s = STATE(cmd);
    if (vkm_topology_class(topology) != vkm_topology_class(s->dyn.topology))
        s->pipeline_dirty = true;
    s->dyn.topology = (uint8_t)topology;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetDepthTestEnable(VkCommandBuffer cmd, VkBool32 enable)
{
    STATE(cmd)->dyn.depth_test = (uint8_t)enable;
    STATE(cmd)->depth_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetDepthWriteEnable(VkCommandBuffer cmd, VkBool32 enable)
{
    STATE(cmd)->dyn.depth_write = (uint8_t)enable;
    STATE(cmd)->depth_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetDepthCompareOp(VkCommandBuffer cmd, VkCompareOp op)
{
    STATE(cmd)->dyn.depth_compare = (uint8_t)op;
    STATE(cmd)->depth_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetDepthBoundsTestEnable(VkCommandBuffer cmd, VkBool32 enable)
{
    STATE(cmd)->dyn.bounds_test = (uint8_t)enable;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetStencilTestEnable(VkCommandBuffer cmd, VkBool32 enable)
{
    STATE(cmd)->dyn.stencil_test = (uint8_t)enable;
    STATE(cmd)->depth_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetStencilOp(VkCommandBuffer cmd, VkStencilFaceFlags faces, VkStencilOp fail,
                                                  VkStencilOp pass, VkStencilOp depth_fail, VkCompareOp compare)
{
    VkmGfxState *s = STATE(cmd);
    VkmStencilFace *f[2] = {(faces & VK_STENCIL_FACE_FRONT_BIT) ? &s->dyn.front_stencil : nullptr,
                            (faces & VK_STENCIL_FACE_BACK_BIT) ? &s->dyn.back_stencil : nullptr};
    for (int i = 0; i < 2; i++)
        if (f[i]) {
            f[i]->fail = (uint8_t)fail;
            f[i]->pass = (uint8_t)pass;
            f[i]->depth_fail = (uint8_t)depth_fail;
            f[i]->compare = (uint8_t)compare;
        }
    s->depth_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetRasterizerDiscardEnable(VkCommandBuffer cmd, VkBool32 enable)
{
    STATE(cmd)->dyn.discard = (uint8_t)enable;
    STATE(cmd)->pipeline_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetDepthBiasEnable(VkCommandBuffer cmd, VkBool32 enable)
{
    STATE(cmd)->dyn.bias_enable = (uint8_t)enable;
    STATE(cmd)->raster_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetPrimitiveRestartEnable(VkCommandBuffer cmd, VkBool32 enable)
{
    STATE(cmd)->dyn.restart = (uint8_t)enable;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetDepthClampEnableEXT(VkCommandBuffer cmd, VkBool32 enable)
{
    STATE(cmd)->dyn.depth_clamp = (uint8_t)enable;
    STATE(cmd)->raster_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetDepthClipEnableEXT(VkCommandBuffer cmd, VkBool32 enable)
{
    STATE(cmd)->dyn.depth_clip = (uint8_t)enable;
    STATE(cmd)->raster_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetPolygonModeEXT(VkCommandBuffer cmd, VkPolygonMode mode)
{
    STATE(cmd)->dyn.polygon = (uint8_t)mode;
    STATE(cmd)->raster_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetRasterizationSamplesEXT(VkCommandBuffer cmd, VkSampleCountFlagBits samples)
{
    STATE(cmd)->dyn.samples = (uint8_t)samples;
    STATE(cmd)->pipeline_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetSampleMaskEXT(VkCommandBuffer cmd, VkSampleCountFlagBits samples,
                                                      const VkSampleMask *mask)
{
    STATE(cmd)->dyn.sample_mask = mask ? mask[0] : ~0u;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetAlphaToCoverageEnableEXT(VkCommandBuffer cmd, VkBool32 enable)
{
    STATE(cmd)->dyn.alpha_to_coverage = (uint8_t)enable;
    STATE(cmd)->pipeline_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetAlphaToOneEnableEXT(VkCommandBuffer cmd, VkBool32 enable)
{
    STATE(cmd)->dyn.alpha_to_one = (uint8_t)enable;
    STATE(cmd)->pipeline_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetColorBlendEnableEXT(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                                                            const VkBool32 *enables)
{
    for (uint32_t i = 0; i < count && first + i < VKM_COLORS; i++)
        STATE(cmd)->dyn.blend[first + i].enable = (uint8_t)enables[i];
    STATE(cmd)->pipeline_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetColorBlendEquationEXT(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                                                              const VkColorBlendEquationEXT *eq)
{
    for (uint32_t i = 0; i < count && first + i < VKM_COLORS; i++) {
        VkmBlend *b = &STATE(cmd)->dyn.blend[first + i];
        b->src_color = (uint8_t)eq[i].srcColorBlendFactor;
        b->dst_color = (uint8_t)eq[i].dstColorBlendFactor;
        b->color_op = (uint8_t)eq[i].colorBlendOp;
        b->src_alpha = (uint8_t)eq[i].srcAlphaBlendFactor;
        b->dst_alpha = (uint8_t)eq[i].dstAlphaBlendFactor;
        b->alpha_op = (uint8_t)eq[i].alphaBlendOp;
    }
    STATE(cmd)->pipeline_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetColorWriteMaskEXT(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                                                          const VkColorComponentFlags *masks)
{
    for (uint32_t i = 0; i < count && first + i < VKM_COLORS; i++)
        STATE(cmd)->dyn.blend[first + i].write = (uint8_t)masks[i];
    STATE(cmd)->pipeline_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetVertexInputEXT(VkCommandBuffer cmd, uint32_t binding_count,
                                                       const VkVertexInputBindingDescription2EXT *bindings,
                                                       uint32_t attribute_count,
                                                       const VkVertexInputAttributeDescription2EXT *attributes)
{
    VkmGfxState *s = STATE(cmd);
    s->dyn.vertex.binding_count = MIME_MIN(binding_count, (uint32_t)VKM_VERTEX_BINDINGS);
    s->dyn.vertex.attribute_count = MIME_MIN(attribute_count, (uint32_t)VKM_VERTEX_ATTRIBUTES);
    memcpy(s->dyn.vertex.bindings, bindings, sizeof(*bindings) * s->dyn.vertex.binding_count);
    memcpy(s->dyn.vertex.attributes, attributes, sizeof(*attributes) * s->dyn.vertex.attribute_count);
    for (uint32_t i = 0; i < s->dyn.vertex.binding_count; i++)
        if (bindings[i].binding < VKM_VERTEX_BINDINGS)
            s->dyn.strides[bindings[i].binding] = bindings[i].stride;
    s->pipeline_dirty = true;
}

/* the extension commands shader objects come with that have nothing to do on Metal. */
static VKAPI_ATTR void VKAPI_CALL CmdSetTessellationDomainOriginEXT(VkCommandBuffer cmd,
                                                                    VkTessellationDomainOrigin origin)
{
}

static VKAPI_ATTR void VKAPI_CALL CmdSetLogicOpEnableEXT(VkCommandBuffer cmd, VkBool32 enable)
{
}

static VKAPI_ATTR void VKAPI_CALL CmdSetLogicOpEXT(VkCommandBuffer cmd, VkLogicOp op)
{
}

static VKAPI_ATTR void VKAPI_CALL CmdSetPatchControlPointsEXT(VkCommandBuffer cmd, uint32_t points)
{
}

static VKAPI_ATTR void VKAPI_CALL CmdSetRasterizationStreamEXT(VkCommandBuffer cmd, uint32_t stream)
{
}

static VKAPI_ATTR void VKAPI_CALL CmdSetConservativeRasterizationModeEXT(VkCommandBuffer cmd,
                                                                         VkConservativeRasterizationModeEXT mode)
{
}

static VKAPI_ATTR void VKAPI_CALL CmdSetLineRasterizationModeEXT(VkCommandBuffer cmd,
                                                                 VkLineRasterizationModeEXT mode)
{
}

static VKAPI_ATTR void VKAPI_CALL CmdSetLineStippleEnableEXT(VkCommandBuffer cmd, VkBool32 enable)
{
}

static VKAPI_ATTR void VKAPI_CALL CmdSetDeviceMask(VkCommandBuffer cmd, uint32_t mask)
{
}

/* ------------------------------------------------------------------ bindings. */

static VKAPI_ATTR void VKAPI_CALL CmdBindShadersEXT(VkCommandBuffer cmd, uint32_t count,
                                                    const VkShaderStageFlagBits *stages, const VkShaderEXT *shaders)
{
    VkmGfxState *s = STATE(cmd);
    for (uint32_t i = 0; i < count; i++) {
        const VkmCode *code = shaders && shaders[i] ? &shaders[i]->code : nullptr;
        switch (stages[i]) {
        case VK_SHADER_STAGE_VERTEX_BIT:
            s->vs = code;
            s->pipeline_dirty = true;
            break;
        case VK_SHADER_STAGE_FRAGMENT_BIT:
            s->fs = code;
            s->pipeline_dirty = true;
            break;
        case VK_SHADER_STAGE_GEOMETRY_BIT:
            s->gs = code;
            break;
        case VK_SHADER_STAGE_COMPUTE_BIT:
            s->cs = code;
            s->compute = code ? vkm_compute_pipeline(cmd->device, code, false) : nullptr;
            break;
        default:
            break;
        }
    }
}

#define FIXED(bit) (!(g->dynamic & (1ull << (bit))))

static void apply_static(VkmGfxState *s, const VkmGfxStatic *g)
{
    VkmDynamic *d = &s->dyn;
    const VkmDynamic *p = &g->state;
    if (FIXED(VKM_D_VERTEX_INPUT)) {
        d->vertex = p->vertex;
        if (FIXED(VKM_D_STRIDE))
            memcpy(d->strides, p->strides, sizeof(d->strides));
    }
    if (FIXED(VKM_D_TOPOLOGY))
        d->topology = p->topology;
    if (FIXED(VKM_D_RESTART))
        d->restart = p->restart;
    if (FIXED(VKM_D_VIEWPORT_COUNT))
        d->viewport_count = p->viewport_count;
    if (FIXED(VKM_D_VIEWPORT) && FIXED(VKM_D_VIEWPORT_COUNT))
        memcpy(d->viewports, p->viewports, sizeof(d->viewports));
    if (FIXED(VKM_D_SCISSOR_COUNT))
        d->scissor_count = p->scissor_count;
    if (FIXED(VKM_D_SCISSOR) && FIXED(VKM_D_SCISSOR_COUNT))
        memcpy(d->scissors, p->scissors, sizeof(d->scissors));
    if (FIXED(VKM_D_DISCARD))
        d->discard = p->discard;
    if (FIXED(VKM_D_CULL_MODE))
        d->cull = p->cull;
    if (FIXED(VKM_D_FRONT_FACE))
        d->front = p->front;
    if (FIXED(VKM_D_POLYGON_MODE))
        d->polygon = p->polygon;
    if (FIXED(VKM_D_DEPTH_CLAMP))
        d->depth_clamp = p->depth_clamp;
    if (FIXED(VKM_D_DEPTH_CLIP))
        d->depth_clip = p->depth_clip;
    if (FIXED(VKM_D_BIAS_ENABLE))
        d->bias_enable = p->bias_enable;
    if (FIXED(VKM_D_DEPTH_BIAS)) {
        d->bias_constant = p->bias_constant;
        d->bias_clamp = p->bias_clamp;
        d->bias_slope = p->bias_slope;
    }
    if (FIXED(VKM_D_SAMPLES))
        d->samples = p->samples;
    if (FIXED(VKM_D_SAMPLE_MASK))
        d->sample_mask = p->sample_mask;
    if (FIXED(VKM_D_ALPHA_TO_COVERAGE))
        d->alpha_to_coverage = p->alpha_to_coverage;
    if (FIXED(VKM_D_ALPHA_TO_ONE))
        d->alpha_to_one = p->alpha_to_one;
    if (FIXED(VKM_D_DEPTH_TEST))
        d->depth_test = p->depth_test;
    if (FIXED(VKM_D_DEPTH_WRITE))
        d->depth_write = p->depth_write;
    if (FIXED(VKM_D_DEPTH_COMPARE))
        d->depth_compare = p->depth_compare;
    if (FIXED(VKM_D_BOUNDS_TEST))
        d->bounds_test = p->bounds_test;
    if (FIXED(VKM_D_DEPTH_BOUNDS)) {
        d->bounds_min = p->bounds_min;
        d->bounds_max = p->bounds_max;
    }
    if (FIXED(VKM_D_STENCIL_TEST))
        d->stencil_test = p->stencil_test;
    for (int face = 0; face < 2; face++) {
        VkmStencilFace *to = face ? &d->back_stencil : &d->front_stencil;
        const VkmStencilFace *from = face ? &p->back_stencil : &p->front_stencil;
        if (FIXED(VKM_D_STENCIL_OP)) {
            to->fail = from->fail;
            to->pass = from->pass;
            to->depth_fail = from->depth_fail;
            to->compare = from->compare;
        }
        if (FIXED(VKM_D_STENCIL_COMPARE_MASK))
            to->read = from->read;
        if (FIXED(VKM_D_STENCIL_WRITE_MASK))
            to->write = from->write;
    }
    if (FIXED(VKM_D_STENCIL_REFERENCE)) {
        d->ref_front = p->ref_front;
        d->ref_back = p->ref_back;
    }
    for (uint32_t i = 0; i < VKM_COLORS; i++) {
        if (FIXED(VKM_D_BLEND_ENABLE))
            d->blend[i].enable = p->blend[i].enable;
        if (FIXED(VKM_D_WRITE_MASK))
            d->blend[i].write = p->blend[i].write;
        if (FIXED(VKM_D_BLEND_EQUATION)) {
            uint8_t enable = d->blend[i].enable, write = d->blend[i].write;
            d->blend[i] = p->blend[i];
            d->blend[i].enable = enable;
            d->blend[i].write = write;
        }
    }
    if (FIXED(VKM_D_BLEND_CONSTANTS))
        memcpy(d->blend_constants, p->blend_constants, sizeof(d->blend_constants));
    s->pipeline_dirty = s->depth_dirty = s->raster_dirty = s->viewport_dirty = s->scissor_dirty = true;
    s->ref_dirty = s->constants_dirty = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdBindPipeline(VkCommandBuffer cmd, VkPipelineBindPoint bind, VkPipeline p)
{
    VkmGfxState *s = STATE(cmd);
    if (bind == VK_PIPELINE_BIND_POINT_COMPUTE) {
        s->cs = &p->stages[0];
        s->compute = p->compute;
        return;
    }
    s->vs = p->has[0] ? &p->stages[0] : nullptr;
    s->fs = p->has[1] ? &p->stages[1] : nullptr;
    s->gs = p->has[2] ? &p->stages[2] : nullptr;
    apply_static(s, p->gfx);
}

static VKAPI_ATTR void VKAPI_CALL CmdBindDescriptorSets(VkCommandBuffer cmd, VkPipelineBindPoint bind,
                                                        VkPipelineLayout layout, uint32_t first, uint32_t count,
                                                        const VkDescriptorSet *sets, uint32_t dynamic_count,
                                                        const uint32_t *dynamic)
{
    int point = bind == VK_PIPELINE_BIND_POINT_COMPUTE ? VKM_COMPUTE : VKM_GFX;
    uint32_t used = 0;
    for (uint32_t i = 0; i < count && first + i < VKM_SETS; i++) {
        VkDescriptorSet set = sets[i];
        uint64_t gpu = set ? set->gpu : 0;
        /* dynamic buffers take their offsets in a copy of the set's arguments. */
        uint32_t dynamic_here = 0;
        for (uint32_t k = 0; set && k < set->layout->count; k++) {
            VkDescriptorType t = set->layout->bindings[k].type;
            if (t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
                dynamic_here += set->layout->bindings[k].count;
        }
        if (dynamic_here && used + dynamic_here <= dynamic_count) {
            uint8_t *copy = vkm_upload(cmd, set->size, 16, &gpu, nullptr, nullptr);
            if (!copy)
                return;
            memcpy(copy, set->map, set->size);
            for (uint32_t k = 0; k < set->layout->count; k++) {
                const VkmBinding *b = &set->layout->bindings[k];
                if (b->type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC &&
                    b->type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
                    continue;
                for (uint32_t e = 0; e < b->count; e++) {
                    uint64_t *slot = (uint64_t *)(copy + (uint64_t)(b->slot + e) * 8);
                    if (*slot)
                        *slot += dynamic[used];
                    used++;
                }
            }
        }
        STATE(cmd)->sets[point][first + i] = gpu;
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdBindDescriptorSets2(VkCommandBuffer cmd, const VkBindDescriptorSetsInfo *info)
{
    if (info->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS)
        CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, info->layout, info->firstSet,
                              info->descriptorSetCount, info->pDescriptorSets, 0, nullptr);
    if (info->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT)
        CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, info->layout, info->firstSet,
                              info->descriptorSetCount, info->pDescriptorSets, 0, nullptr);
}

/* descriptor buffers: a set is the argument buffer at the address of a bound buffer plus an offset. */
static VKAPI_ATTR void VKAPI_CALL CmdBindDescriptorBuffersEXT(VkCommandBuffer cmd, uint32_t count,
                                                              const VkDescriptorBufferBindingInfoEXT *infos)
{
    for (uint32_t i = 0; i < count && i < VKM_SETS; i++)
        STATE(cmd)->descriptor_buffers[i] = infos[i].address;
}

static VKAPI_ATTR void VKAPI_CALL CmdSetDescriptorBufferOffsetsEXT(VkCommandBuffer cmd, VkPipelineBindPoint bind,
                                                                   VkPipelineLayout layout, uint32_t first,
                                                                   uint32_t count, const uint32_t *indices,
                                                                   const VkDeviceSize *offsets)
{
    VkmGfxState *s = STATE(cmd);
    int point = bind == VK_PIPELINE_BIND_POINT_COMPUTE ? VKM_COMPUTE : VKM_GFX;
    for (uint32_t i = 0; i < count && first + i < VKM_SETS; i++)
        s->sets[point][first + i] = indices[i] < VKM_SETS ? s->descriptor_buffers[indices[i]] + offsets[i] : 0;
}

static VKAPI_ATTR void VKAPI_CALL CmdBindDescriptorBufferEmbeddedSamplersEXT(VkCommandBuffer cmd,
                                                                             VkPipelineBindPoint bind,
                                                                             VkPipelineLayout layout, uint32_t set)
{
    int point = bind == VK_PIPELINE_BIND_POINT_COMPUTE ? VKM_COMPUTE : VKM_GFX;
    if (set < VKM_SETS && set < layout->set_count && layout->sets[set])
        STATE(cmd)->sets[point][set] = layout->sets[set]->embedded_gpu;
}

static VKAPI_ATTR void VKAPI_CALL CmdPushConstants(VkCommandBuffer cmd, VkPipelineLayout layout,
                                                   VkShaderStageFlags stages, uint32_t offset, uint32_t size,
                                                   const void *data)
{
    VkmGfxState *s = STATE(cmd);
    if (offset >= VKM_PUSH_SIZE)
        return;
    memcpy(s->push + offset, data, MIME_MIN(size, VKM_PUSH_SIZE - offset));
    s->push_dirty[VKM_GFX] = s->push_dirty[VKM_COMPUTE] = true;
}

static VKAPI_ATTR void VKAPI_CALL CmdPushConstants2(VkCommandBuffer cmd, const VkPushConstantsInfo *info)
{
    CmdPushConstants(cmd, info->layout, info->stageFlags, info->offset, info->size, info->pValues);
}

static VKAPI_ATTR void VKAPI_CALL CmdBindVertexBuffers2(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                                                        const VkBuffer *buffers, const VkDeviceSize *offsets,
                                                        const VkDeviceSize *sizes, const VkDeviceSize *strides)
{
    VkmGfxState *s = STATE(cmd);
    for (uint32_t i = 0; i < count && first + i < VKM_VERTEX_BINDINGS; i++) {
        VkBuffer b = buffers[i];
        uint64_t size = b ? b->size - offsets[i] : 0;
        if (sizes && sizes[i] != VK_WHOLE_SIZE)
            size = sizes[i];
        s->vertex[first + i].address = b ? b->gpu + offsets[i] : 0;
        s->vertex[first + i].size = size;
        if (strides) {
            if ((s->dyn.strides[first + i] == 0) != (strides[i] == 0))
                s->pipeline_dirty = true;
            s->dyn.strides[first + i] = (uint32_t)strides[i];
        }
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdBindVertexBuffers(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                                                       const VkBuffer *buffers, const VkDeviceSize *offsets)
{
    CmdBindVertexBuffers2(cmd, first, count, buffers, offsets, nullptr, nullptr);
}

static VKAPI_ATTR void VKAPI_CALL CmdBindIndexBuffer2(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset,
                                                      VkDeviceSize size, VkIndexType type)
{
    VkmGfxState *s = STATE(cmd);
    s->index_address = buffer ? buffer->gpu + offset : 0;
    s->index_host = buffer && buffer->memory && buffer->memory->map ? buffer->memory->map + buffer->offset + offset : nullptr;
    s->index_size = buffer ? (size == VK_WHOLE_SIZE ? buffer->size - offset : size) : 0;
    s->index_type = type == VK_INDEX_TYPE_UINT32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
}

static VKAPI_ATTR void VKAPI_CALL CmdBindIndexBuffer(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset,
                                                     VkIndexType type)
{
    CmdBindIndexBuffer2(cmd, buffer, offset, VK_WHOLE_SIZE, type);
}

/* ------------------------------------------------------------------ rendering. */

static uint8_t load_action(VkAttachmentLoadOp op, bool whole)
{
    switch (op) {
    case VK_ATTACHMENT_LOAD_OP_CLEAR: return whole ? MTLLoadActionClear : MTLLoadActionLoad;
    case VK_ATTACHMENT_LOAD_OP_DONT_CARE: return whole ? MTLLoadActionDontCare : MTLLoadActionLoad;
    default: return MTLLoadActionLoad;
    }
}

static uint8_t store_action(VkAttachmentStoreOp op, bool whole, bool resolve)
{
    bool keep = op != VK_ATTACHMENT_STORE_OP_DONT_CARE || !whole;
    if (resolve)
        return keep ? MTLStoreActionStoreAndMultisampleResolve : MTLStoreActionMultisampleResolve;
    return keep ? MTLStoreActionStore : MTLStoreActionDontCare;
}

static bool integer_format(VkFormat f)
{
    switch (f) {
    case VK_FORMAT_R8_UINT: case VK_FORMAT_R8_SINT: case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R8G8B8A8_SINT: case VK_FORMAT_A8B8G8R8_UINT_PACK32:
    case VK_FORMAT_A8B8G8R8_SINT_PACK32: case VK_FORMAT_A2B10G10R10_UINT_PACK32: case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_SINT: case VK_FORMAT_R16G16_UINT: case VK_FORMAT_R16G16_SINT: case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SINT: case VK_FORMAT_R32_UINT: case VK_FORMAT_R32_SINT: case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32_SINT: case VK_FORMAT_R32G32B32A32_UINT: case VK_FORMAT_R32G32B32A32_SINT: return true;
    default: return false;
    }
}

static bool signed_format(VkFormat f)
{
    switch (f) {
    case VK_FORMAT_R8_SINT: case VK_FORMAT_R8G8_SINT: case VK_FORMAT_R8G8B8A8_SINT: case VK_FORMAT_A8B8G8R8_SINT_PACK32:
    case VK_FORMAT_R16_SINT: case VK_FORMAT_R16G16_SINT: case VK_FORMAT_R16G16B16A16_SINT: case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32G32_SINT: case VK_FORMAT_R32G32B32A32_SINT: return true;
    default: return false;
    }
}

MTLClearColor vkm_clear_color(VkFormat format, const VkClearColorValue *v)
{
    if (!integer_format(format))
        return (MTLClearColor){v->float32[0], v->float32[1], v->float32[2], v->float32[3]};
    if (signed_format(format))
        return (MTLClearColor){v->int32[0], v->int32[1], v->int32[2], v->int32[3]};
    return (MTLClearColor){v->uint32[0], v->uint32[1], v->uint32[2], v->uint32[3]};
}

static void attach(VkmAttach *a, VkImageView view, const VkRenderingAttachmentInfo *info, bool whole)
{
    a->texture = view->render;
    a->level = view->render_level;
    a->slice = view->render_slice;
    bool resolve = info->resolveMode != VK_RESOLVE_MODE_NONE && info->resolveImageView;
    a->load = load_action(info->loadOp, whole);
    a->store = store_action(info->storeOp, whole, resolve);
    if (resolve) {
        a->resolve = info->resolveImageView->render;
        a->resolve_level = info->resolveImageView->render_level;
        a->resolve_slice = info->resolveImageView->render_slice;
        a->resolve_filter = info->resolveMode == VK_RESOLVE_MODE_MIN_BIT   ? MTLMultisampleDepthResolveFilterMin
                            : info->resolveMode == VK_RESOLVE_MODE_MAX_BIT ? MTLMultisampleDepthResolveFilterMax
                                                                           : MTLMultisampleDepthResolveFilterSample0;
    }
}

static void begin_rendering(VkCommandBuffer cmd, const VkRenderingInfo *info)
{
    VkmGfxState *s = STATE(cmd);
    size_t at = cmd->cmd_size;
    VkmBeginRender *b = vkm_emit(cmd, VKM_OP_BEGIN_RENDER, sizeof(*b));
    if (!b)
        return;
    uint32_t width = UINT32_MAX, height = UINT32_MAX, samples = 0;
    VkImageView v[VKM_COLORS + 2] = {};
    for (uint32_t i = 0; i < info->colorAttachmentCount && i < VKM_COLORS; i++)
        v[i] = info->pColorAttachments[i].imageView;
    v[VKM_COLORS] = info->pDepthAttachment ? info->pDepthAttachment->imageView : VK_NULL_HANDLE;
    v[VKM_COLORS + 1] = info->pStencilAttachment ? info->pStencilAttachment->imageView : VK_NULL_HANDLE;
    for (uint32_t i = 0; i < VKM_COLORS + 2; i++)
        if (v[i] && v[i]->render) {
            width = MIME_MIN(width, v[i]->extent.width);
            height = MIME_MIN(height, v[i]->extent.height);
            samples = MIME_MAX(samples, v[i]->image->samples);
        }
    const VkRect2D area = info->renderArea;
    if (width == UINT32_MAX) {
        width = area.offset.x + area.extent.width;
        height = area.offset.y + area.extent.height;
    }
    bool whole = area.offset.x == 0 && area.offset.y == 0 && area.extent.width >= width && area.extent.height >= height;
    b->width = width;
    b->height = height;
    b->layers = info->viewMask ? 32 - __builtin_clz(info->viewMask) : info->layerCount;
    b->samples = samples ? samples : 1;
    b->color_count = MIME_MIN(info->colorAttachmentCount, (uint32_t)VKM_COLORS);
    memset(s->colors, 0, sizeof(s->colors));
    for (uint32_t i = 0; i < b->color_count; i++) {
        const VkRenderingAttachmentInfo *c = &info->pColorAttachments[i];
        if (!c->imageView || !c->imageView->render)
            continue;
        attach(&b->colors[i], c->imageView, c, whole);
        b->colors[i].clear = vkm_clear_color(c->imageView->format, &c->clearValue.color);
        s->colors[i] = c->imageView->fi->pixel;
    }
    s->has_depth = s->has_stencil = false;
    if (v[VKM_COLORS] && v[VKM_COLORS]->render) {
        attach(&b->depth, v[VKM_COLORS], info->pDepthAttachment, whole);
        b->depth.clear.red = info->pDepthAttachment->clearValue.depthStencil.depth;
        s->has_depth = true;
    }
    if (v[VKM_COLORS + 1] && v[VKM_COLORS + 1]->render) {
        attach(&b->stencil, v[VKM_COLORS + 1], info->pStencilAttachment, whole);
        b->stencil.clear.red = info->pStencilAttachment->clearValue.depthStencil.stencil;
        s->has_stencil = true;
    }
    if (s->occlusion)
        b->visibility = s->occlusion->buffer;
    s->rendering = true;
    s->drawn = false;
    s->begin = at;
    s->color_count = b->color_count;
    s->samples = b->samples;
    s->width = width;
    s->height = height;
    s->layers = b->layers;
    vkm_forget(cmd);
    if (s->occlusion) {
        VkmOpVisibility *o = vkm_emit(cmd, VKM_OP_VISIBILITY, sizeof(*o));
        if (o) {
            o->mode = s->occlusion_precise ? MTLVisibilityResultModeCounting : MTLVisibilityResultModeBoolean;
            o->offset = s->occlusion_query * 8;
        }
    }
    /* a clear of part of the attachments is a draw over the render area. */
    if (!whole) {
        bool any = false;
        VkClearAttachment clears[VKM_COLORS + 1];
        uint32_t n = 0;
        for (uint32_t i = 0; i < b->color_count; i++)
            if (info->pColorAttachments[i].imageView && info->pColorAttachments[i].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
                clears[n++] = (VkClearAttachment){VK_IMAGE_ASPECT_COLOR_BIT, i, info->pColorAttachments[i].clearValue};
        VkImageAspectFlags ds = 0;
        if (s->has_depth && info->pDepthAttachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
            ds |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (s->has_stencil && info->pStencilAttachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
            ds |= VK_IMAGE_ASPECT_STENCIL_BIT;
        if (ds) {
            VkClearValue value = {};
            if (ds & VK_IMAGE_ASPECT_DEPTH_BIT)
                value.depthStencil.depth = info->pDepthAttachment->clearValue.depthStencil.depth;
            if (ds & VK_IMAGE_ASPECT_STENCIL_BIT)
                value.depthStencil.stencil = info->pStencilAttachment->clearValue.depthStencil.stencil;
            clears[n++] = (VkClearAttachment){ds, 0, value};
        }
        any = n > 0;
        if (any) {
            VkClearRect rect = {area, 0, b->layers};
            vkm_clear_rects(cmd, n, clears, 1, &rect);
        }
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdBeginRendering(VkCommandBuffer cmd, const VkRenderingInfo *info)
{
    begin_rendering(cmd, info);
}

static VKAPI_ATTR void VKAPI_CALL CmdEndRendering(VkCommandBuffer cmd)
{
    VkmGfxState *s = STATE(cmd);
    if (!s->rendering)
        return;
    VkmBeginRender *b = (VkmBeginRender *)(cmd->cmds + s->begin);
    bool clears = false, resolves = false;
    for (uint32_t i = 0; i < b->color_count; i++) {
        clears |= b->colors[i].texture && b->colors[i].load == MTLLoadActionClear;
        resolves |= b->colors[i].resolve != nullptr;
    }
    clears |= (b->depth.texture && b->depth.load == MTLLoadActionClear) ||
              (b->stencil.texture && b->stencil.load == MTLLoadActionClear);
    resolves |= b->depth.resolve || b->stencil.resolve;
    /* a pass that neither draws, clears nor resolves does nothing. */
    if (!s->drawn && !clears && !resolves && cmd->cmd_size == s->begin + b->head.size && !s->occlusion)
        cmd->cmd_size = s->begin;
    else
        vkm_emit(cmd, VKM_OP_END_RENDER, sizeof(VkmOp));
    s->rendering = false;
}

/* a secondary command buffer that continues a render pass draws to its formats. */
static void begin_inherited(VkCommandBuffer cmd, const VkCommandBufferInheritanceInfo *inherit)
{
    VkmGfxState *s = STATE(cmd);
    const VkCommandBufferInheritanceRenderingInfo *r =
        vkm_find(inherit->pNext, VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO);
    s->rendering = true;
    s->drawn = true;
    s->samples = 1;
    s->width = s->height = 16384;
    if (r) {
        s->color_count = MIME_MIN(r->colorAttachmentCount, (uint32_t)VKM_COLORS);
        for (uint32_t i = 0; i < s->color_count; i++)
            s->colors[i] = vkm_format(r->pColorAttachmentFormats[i])->pixel;
        s->samples = r->rasterizationSamples ? r->rasterizationSamples : 1;
        s->has_depth = r->depthAttachmentFormat != VK_FORMAT_UNDEFINED;
        s->has_stencil = r->stencilAttachmentFormat != VK_FORMAT_UNDEFINED;
    } else if (inherit->renderPass) {
        const VkmSubpass *sp = &inherit->renderPass->subpasses[inherit->subpass];
        s->color_count = sp->color_count;
        for (uint32_t i = 0; i < sp->color_count; i++)
            if (sp->colors[i] != VK_ATTACHMENT_UNUSED) {
                const VkAttachmentDescription2 *a = &inherit->renderPass->attachments[sp->colors[i]];
                s->colors[i] = vkm_format(a->format)->pixel;
                s->samples = a->samples;
            }
        s->has_depth = s->has_stencil = sp->depth != VK_ATTACHMENT_UNUSED;
    }
    vkm_forget(cmd);
}

/* ------------------------------------------------------------------ render passes. */

static VKAPI_ATTR VkResult VKAPI_CALL CreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2 *info,
                                                        const VkAllocationCallbacks *a, VkRenderPass *out)
{
    VkRenderPass rp = vkm_zalloc(sizeof(*rp));
    if (!rp)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    rp->attachment_count = info->attachmentCount;
    rp->subpass_count = info->subpassCount;
    rp->attachments = vkm_zalloc(sizeof(*rp->attachments) * MIME_MAX(info->attachmentCount, 1u));
    rp->subpasses = vkm_zalloc(sizeof(*rp->subpasses) * MIME_MAX(info->subpassCount, 1u));
    if (!rp->attachments || !rp->subpasses) {
        free(rp->attachments);
        free(rp->subpasses);
        free(rp);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(rp->attachments, info->pAttachments, sizeof(*rp->attachments) * info->attachmentCount);
    for (uint32_t i = 0; i < info->subpassCount; i++) {
        const VkSubpassDescription2 *sd = &info->pSubpasses[i];
        VkmSubpass *sp = &rp->subpasses[i];
        sp->color_count = MIME_MIN(sd->colorAttachmentCount, (uint32_t)VKM_COLORS);
        for (uint32_t k = 0; k < sp->color_count; k++) {
            sp->colors[k] = sd->pColorAttachments[k].attachment;
            sp->resolves[k] = sd->pResolveAttachments ? sd->pResolveAttachments[k].attachment : VK_ATTACHMENT_UNUSED;
        }
        sp->depth = sd->pDepthStencilAttachment ? sd->pDepthStencilAttachment->attachment : VK_ATTACHMENT_UNUSED;
        sp->depth_resolve = VK_ATTACHMENT_UNUSED;
        sp->view_mask = sd->viewMask;
    }
    *out = rp;
    return VK_SUCCESS;
}

static VkAttachmentReference2 reference2(const VkAttachmentReference *r)
{
    return (VkAttachmentReference2){VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, r->attachment, r->layout, 0};
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *info,
                                                       const VkAllocationCallbacks *a, VkRenderPass *out)
{
    VkAttachmentDescription2 attachments[32];
    VkSubpassDescription2 subpasses[8];
    VkAttachmentReference2 refs[8][VKM_COLORS * 2 + 1];
    uint32_t na = MIME_MIN(info->attachmentCount, 32u), ns = MIME_MIN(info->subpassCount, 8u);
    for (uint32_t i = 0; i < na; i++) {
        const VkAttachmentDescription *d = &info->pAttachments[i];
        attachments[i] = (VkAttachmentDescription2){VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2, nullptr, d->flags,
                                                    d->format, d->samples, d->loadOp, d->storeOp, d->stencilLoadOp,
                                                    d->stencilStoreOp, d->initialLayout, d->finalLayout};
    }
    for (uint32_t i = 0; i < ns; i++) {
        const VkSubpassDescription *s = &info->pSubpasses[i];
        uint32_t nc = MIME_MIN(s->colorAttachmentCount, (uint32_t)VKM_COLORS);
        for (uint32_t k = 0; k < nc; k++) {
            refs[i][k] = reference2(&s->pColorAttachments[k]);
            refs[i][VKM_COLORS + k] = s->pResolveAttachments ? reference2(&s->pResolveAttachments[k])
                                                             : (VkAttachmentReference2){.attachment = VK_ATTACHMENT_UNUSED};
        }
        if (s->pDepthStencilAttachment)
            refs[i][VKM_COLORS * 2] = reference2(s->pDepthStencilAttachment);
        subpasses[i] = (VkSubpassDescription2){VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
                                               nullptr,
                                               s->flags,
                                               s->pipelineBindPoint,
                                               0,
                                               0,
                                               nullptr,
                                               nc,
                                               refs[i],
                                               s->pResolveAttachments ? &refs[i][VKM_COLORS] : nullptr,
                                               s->pDepthStencilAttachment ? &refs[i][VKM_COLORS * 2] : nullptr};
    }
    VkRenderPassCreateInfo2 two = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2, nullptr, info->flags, na, attachments,
                                   ns, subpasses};
    return CreateRenderPass2(device, &two, a, out);
}

static VKAPI_ATTR void VKAPI_CALL DestroyRenderPass(VkDevice device, VkRenderPass rp, const VkAllocationCallbacks *a)
{
    if (!rp)
        return;
    free(rp->attachments);
    free(rp->subpasses);
    free(rp);
}

static VKAPI_ATTR void VKAPI_CALL GetRenderAreaGranularity(VkDevice device, VkRenderPass rp, VkExtent2D *out)
{
    *out = (VkExtent2D){1, 1};
}

static VKAPI_ATTR void VKAPI_CALL GetRenderingAreaGranularity(VkDevice device, const VkRenderingAreaInfo *info,
                                                             VkExtent2D *out)
{
    *out = (VkExtent2D){1, 1};
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *info,
                                                        const VkAllocationCallbacks *a, VkFramebuffer *out)
{
    VkFramebuffer fb = vkm_zalloc(sizeof(*fb) + sizeof(VkImageView) * info->attachmentCount);
    if (!fb)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    fb->width = info->width;
    fb->height = info->height;
    fb->layers = info->layers;
    fb->count = info->attachmentCount;
    if (!(info->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT))
        memcpy(fb->views, info->pAttachments, sizeof(VkImageView) * info->attachmentCount);
    *out = fb;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyFramebuffer(VkDevice device, VkFramebuffer fb, const VkAllocationCallbacks *a)
{
    free(fb);
}

/* a legacy render pass's current subpass, as dynamic rendering. */
typedef struct LegacyPass {
    VkRenderPass pass;
    VkFramebuffer fb;
    uint32_t subpass;
    VkRect2D area;
    VkClearValue clears[32];
    VkImageView views[32];
} LegacyPass;

static LegacyPass *legacy_of(VkCommandBuffer cmd)
{
    if (!STATE(cmd)->legacy)
        STATE(cmd)->legacy = vkm_zalloc(sizeof(LegacyPass));
    return STATE(cmd)->legacy;
}

static void legacy_begin(VkCommandBuffer cmd, LegacyPass *lp)
{
    const VkmSubpass *sp = &lp->pass->subpasses[lp->subpass];
    VkRenderingAttachmentInfo colors[VKM_COLORS], depth = {}, stencil = {};
    for (uint32_t i = 0; i < sp->color_count; i++) {
        colors[i] = (VkRenderingAttachmentInfo){VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        uint32_t a = sp->colors[i];
        if (a == VK_ATTACHMENT_UNUSED)
            continue;
        const VkAttachmentDescription2 *d = &lp->pass->attachments[a];
        colors[i].imageView = lp->views[a];
        colors[i].loadOp = lp->subpass == 0 ? d->loadOp : VK_ATTACHMENT_LOAD_OP_LOAD;
        colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colors[i].clearValue = lp->clears[a];
        uint32_t r = sp->resolves[i];
        if (r != VK_ATTACHMENT_UNUSED) {
            colors[i].resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            colors[i].resolveImageView = lp->views[r];
        }
    }
    if (sp->depth != VK_ATTACHMENT_UNUSED) {
        const VkAttachmentDescription2 *d = &lp->pass->attachments[sp->depth];
        const VkmFormat *f = vkm_format(d->format);
        VkRenderingAttachmentInfo base = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        base.imageView = lp->views[sp->depth];
        base.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        base.clearValue = lp->clears[sp->depth];
        if (f->aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
            depth = base;
            depth.loadOp = lp->subpass == 0 ? d->loadOp : VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        if (f->aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
            stencil = base;
            stencil.loadOp = lp->subpass == 0 ? d->stencilLoadOp : VK_ATTACHMENT_LOAD_OP_LOAD;
        }
    }
    VkRenderingInfo info = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = lp->area;
    info.layerCount = MIME_MAX(lp->fb->layers, 1u);
    info.viewMask = sp->view_mask;
    info.colorAttachmentCount = sp->color_count;
    info.pColorAttachments = colors;
    info.pDepthAttachment = depth.imageView ? &depth : nullptr;
    info.pStencilAttachment = stencil.imageView ? &stencil : nullptr;
    begin_rendering(cmd, &info);
}

static VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass2(VkCommandBuffer cmd, const VkRenderPassBeginInfo *begin,
                                                      const VkSubpassBeginInfo *info)
{
    LegacyPass *lp = legacy_of(cmd);
    if (!lp)
        return;
    lp->pass = begin->renderPass;
    lp->fb = begin->framebuffer;
    lp->subpass = 0;
    lp->area = begin->renderArea;
    memset(lp->clears, 0, sizeof(lp->clears));
    memcpy(lp->clears, begin->pClearValues, sizeof(VkClearValue) * MIME_MIN(begin->clearValueCount, 32u));
    const VkRenderPassAttachmentBeginInfo *imageless =
        vkm_find(begin->pNext, VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO);
    uint32_t n = MIME_MIN(lp->pass->attachment_count, 32u);
    for (uint32_t i = 0; i < n; i++)
        lp->views[i] = imageless ? imageless->pAttachments[i] : lp->fb->views[i];
    legacy_begin(cmd, lp);
}

static VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass(VkCommandBuffer cmd, const VkRenderPassBeginInfo *begin,
                                                     VkSubpassContents contents)
{
    VkSubpassBeginInfo info = {VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO, nullptr, contents};
    CmdBeginRenderPass2(cmd, begin, &info);
}

static VKAPI_ATTR void VKAPI_CALL CmdNextSubpass2(VkCommandBuffer cmd, const VkSubpassBeginInfo *begin,
                                                  const VkSubpassEndInfo *end)
{
    LegacyPass *lp = legacy_of(cmd);
    CmdEndRendering(cmd);
    if (lp && lp->pass && lp->subpass + 1 < lp->pass->subpass_count) {
        lp->subpass++;
        legacy_begin(cmd, lp);
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdNextSubpass(VkCommandBuffer cmd, VkSubpassContents contents)
{
    CmdNextSubpass2(cmd, nullptr, nullptr);
}

static VKAPI_ATTR void VKAPI_CALL CmdEndRenderPass2(VkCommandBuffer cmd, const VkSubpassEndInfo *end)
{
    CmdEndRendering(cmd);
    LegacyPass *lp = legacy_of(cmd);
    if (lp)
        lp->pass = nullptr;
}

static VKAPI_ATTR void VKAPI_CALL CmdEndRenderPass(VkCommandBuffer cmd)
{
    CmdEndRenderPass2(cmd, nullptr);
}

/* ------------------------------------------------------------------ draws. */

uint32_t vkm_primitive(VkPrimitiveTopology topology)
{
    switch (topology) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return MTLPrimitiveTypePoint;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: return MTLPrimitiveTypeLine;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return MTLPrimitiveTypeLineStrip;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return MTLPrimitiveTypeTriangleStrip;
    default: return MTLPrimitiveTypeTriangle;
    }
}

uint32_t vkm_topology_class(VkPrimitiveTopology topology)
{
    switch (topology) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return MTLPrimitiveTopologyClassPoint;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY: return MTLPrimitiveTopologyClassLine;
    default: return MTLPrimitiveTopologyClassTriangle;
    }
}

static uint16_t input_format(const VkmInput *in)
{
    switch (in->kind) {
    case 1: return MTLVertexFormatInt4;
    case 2: return MTLVertexFormatUInt4;
    default: return MTLVertexFormatFloat4;
    }
}

static bool src1_factor(uint8_t f)
{
    return f >= VK_BLEND_FACTOR_SRC1_COLOR && f <= VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
}

static bool uses_src1(const VkmBlend *b)
{
    return src1_factor(b->src_color) || src1_factor(b->dst_color) || src1_factor(b->src_alpha) ||
           src1_factor(b->dst_alpha);
}

static uint8_t narrow_unsigned(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R8G8B8_UINT:
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_B8G8R8A8_UINT:
    case VK_FORMAT_A8B8G8R8_UINT_PACK32: return VKM_WIDEN_UINT8;
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16B16_UINT:
    case VK_FORMAT_R16G16B16A16_UINT: return VKM_WIDEN_UINT16;
    default: return VKM_WIDEN_NONE;
    }
}

/* the binding missing vertex attributes read zeros from. */
#define ZERO_BINDING (VKM_VERTEX_BINDINGS - 1)

/* the word fragment shaders count their invocations in, for the active pipeline statistics query. */
static uint64_t stats_counter(const VkmGfxState *s)
{
    VkQueryPool pool = s->stats;
    uint32_t bit = VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
    if (!pool || !(pool->statistics & bit) || s->stats_query >= pool->count)
        return 0;
    uint32_t n = __builtin_popcount(pool->statistics), k = __builtin_popcount(pool->statistics & (bit - 1));
    return pool->buffer.gpuAddress + ((uint64_t)s->stats_query * n + k) * 8;
}

/* what the host knows a draw or dispatch adds to the active statistics query. */
static void count_stats(VkCommandBuffer cmd, const uint64_t add[VKM_STATS])
{
    VkmGfxState *s = STATE(cmd);
    if (!s->stats)
        return;
    VkmOpStats *o = vkm_emit(cmd, VKM_OP_STATS, sizeof(*o));
    if (!o)
        return;
    o->pool = s->stats;
    o->query = s->stats_query;
    memcpy(o->add, add, sizeof(o->add));
}

static uint32_t topology_primitives(VkPrimitiveTopology topology, uint32_t n)
{
    switch (topology) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return n;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: return n / 2;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return n > 1 ? n - 1 : 0;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return n / 3;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: return n > 2 ? n - 2 : 0;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY: return n / 4;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY: return n > 3 ? n - 3 : 0;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: return n / 6;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY: return n > 5 ? (n - 4) / 2 : 0;
    default: return 0;
    }
}

static void count_draw(VkCommandBuffer cmd, uint32_t vertices, uint32_t instances)
{
    VkmGfxState *s = STATE(cmd);
    if (!s->stats)
        return;
    uint64_t prims = (uint64_t)topology_primitives(s->dyn.topology, vertices) * instances, all = (uint64_t)vertices * instances;
    uint64_t add[VKM_STATS] = {all, prims, all};
    if (s->gs) {
        add[3] = prims * MIME_MAX(s->gs->gs.invocations, 1u);
        add[4] = add[3];
        prims = add[3];
    }
    add[5] = add[6] = prims;
    count_stats(cmd, add);
}

/* the stages a draw runs; the geometry stage's emulation draws with vertex functions of its own. */
typedef struct Stages {
    const VkmCode *vs, *fs;
    uint8_t topology_class, discard;
} Stages;

static id<MTLRenderPipelineState> resolve_pipeline(VkCommandBuffer cmd, const Stages *st, bool *zero)
{
    VkmGfxState *s = STATE(cmd);
    const VkmDynamic *d = &s->dyn;
    const VkmCode *vs = st->vs, *fs = st->fs;
    if (fs && (fs->outputs & 2) && s->colors[0] && d->blend[0].enable && uses_src1(&d->blend[0])) {
        const VkmCode *dual = vkm_code_dual(cmd->device, fs);
        if (dual)
            fs = dual;
    }
    if (fs && stats_counter(s)) {
        const VkmCode *counted = vkm_code_counted(cmd->device, fs);
        if (counted)
            fs = counted;
    }
    /* a signed input fed an unsigned 8 or 16 bit format reads it through a variant declaring it unsigned. */
    uint8_t widen[VKM_VERTEX_ATTRIBUTES] = {};
    bool widened = false;
    for (uint32_t i = 0; i < d->vertex.attribute_count; i++) {
        const VkVertexInputAttributeDescription2EXT *a = &d->vertex.attributes[i];
        if (a->location >= VKM_VERTEX_ATTRIBUTES || !(vs->inputs & (1u << a->location)) ||
            vs->input[a->location].kind != 1)
            continue;
        uint8_t w = narrow_unsigned(a->format);
        widen[a->location] = w;
        widened |= w != VKM_WIDEN_NONE;
    }
    if (widened) {
        const VkmCode *variant = vkm_code_widened(cmd->device, vs, widen);
        if (variant)
            vs = variant;
    }
    VkmPipelineKey key;
    memset(&key, 0, sizeof(key));
    key.vs = vs->hash;
    key.fs = fs ? fs->hash : 0;
    for (uint32_t i = 0; i < s->color_count; i++) {
        key.colors[i] = s->colors[i];
        if (!s->colors[i])
            continue;
        key.blend[i] = d->blend[i];
        if (!key.blend[i].enable)
            key.blend[i] = (VkmBlend){0, d->blend[i].write};
    }
    key.samples = (uint8_t)s->samples;
    key.icb = s->icb;
    key.topology_class = st->topology_class;
    key.discard = st->discard;
    key.alpha_to_coverage = d->alpha_to_coverage;
    key.alpha_to_one = d->alpha_to_one;
    uint32_t covered = 0;
    for (uint32_t i = 0; i < d->vertex.attribute_count && key.attribute_count < VKM_VERTEX_ATTRIBUTES; i++) {
        const VkVertexInputAttributeDescription2EXT *a = &d->vertex.attributes[i];
        if (a->location >= VKM_VERTEX_ATTRIBUTES || !(vs->inputs & (1u << a->location)) ||
            a->binding >= ZERO_BINDING)
            continue;
        key.attributes[key.attribute_count++] = (typeof(key.attributes[0])){(uint8_t)a->location, (uint8_t)a->binding,
                                                                            vkm_format(a->format)->vertex, a->offset};
        covered |= 1u << a->location;
    }
    *zero = false;
    for (uint32_t loc = 0; loc < VKM_VERTEX_ATTRIBUTES; loc++)
        if ((vs->inputs & (1u << loc)) && !(covered & (1u << loc))) {
            key.attributes[key.attribute_count++] =
                (typeof(key.attributes[0])){(uint8_t)loc, ZERO_BINDING, input_format(&vs->input[loc]), 0};
            *zero = true;
        }
    for (uint32_t i = 0; i < d->vertex.binding_count; i++) {
        const VkVertexInputBindingDescription2EXT *b = &d->vertex.bindings[i];
        if (b->binding >= ZERO_BINDING)
            continue;
        uint32_t rate = b->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE
                            ? (b->divisor ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionConstant)
                            : MTLVertexStepFunctionPerVertex;
        if (!d->strides[b->binding])
            rate = MTLVertexStepFunctionConstant;
        key.bindings[b->binding].rate = rate;
        key.bindings[b->binding].divisor = b->divisor;
    }
    key.bindings[ZERO_BINDING].rate = MTLVertexStepFunctionConstant;
    return vkm_render_pipeline(cmd->device, &key, vs, fs);
}

static void emit_buffer(VkCommandBuffer cmd, int point, uint32_t index, uint64_t address, uint32_t stride)
{
    VkmGfxState *s = STATE(cmd);
    if (s->set_buffers[point][index] == address && (point == VKM_COMPUTE || s->set_strides[index] == stride))
        return;
    VkmOpBuffer *o = vkm_emit(cmd, point == VKM_COMPUTE ? VKM_OP_COMPUTE_BUFFER : VKM_OP_BUFFER, sizeof(*o));
    if (!o)
        return;
    o->index = index;
    o->address = address;
    o->stride = stride;
    s->set_buffers[point][index] = address;
    s->set_strides[index] = stride;
}

static void emit_bindings(VkCommandBuffer cmd, int point)
{
    VkmGfxState *s = STATE(cmd);
    for (uint32_t i = 0; i < VKM_SETS; i++)
        if (s->sets[point][i])
            emit_buffer(cmd, point, i, s->sets[point][i], ~0u);
    if (s->push_dirty[point]) {
        uint64_t gpu;
        void *p = vkm_upload(cmd, VKM_PUSH_SIZE, 16, &gpu, nullptr, nullptr);
        if (p) {
            memcpy(p, s->push, VKM_PUSH_SIZE);
            emit_buffer(cmd, point, VKM_BUFFER_PUSH, gpu, ~0u);
        }
        s->push_dirty[point] = false;
    }
}

static uint32_t cull_mode(uint8_t cull)
{
    switch (cull) {
    case VK_CULL_MODE_FRONT_BIT: return MTLCullModeFront;
    case VK_CULL_MODE_BACK_BIT: return MTLCullModeBack;
    default: return MTLCullModeNone;
    }
}

/* the state of a draw that differs from the last one; false when nothing can be drawn. */
static bool prepare_stages(VkCommandBuffer cmd, const Stages *st)
{
    VkmGfxState *s = STATE(cmd);
    const VkmDynamic *d = &s->dyn;
    if (!s->rendering || !st->vs || (d->cull == VK_CULL_MODE_FRONT_AND_BACK && !st->discard))
        return false;
    bool zero = false;
    if (s->pipeline_dirty || !s->set_pipeline) {
        id<MTLRenderPipelineState> pipeline = resolve_pipeline(cmd, st, &zero);
        if (!pipeline)
            return false;
        if (pipeline != s->set_pipeline) {
            VkmOpObject *o = vkm_emit(cmd, VKM_OP_PIPELINE, sizeof(*o));
            if (o)
                o->object = pipeline;
            s->set_pipeline = pipeline;
        }
        s->pipeline_dirty = false;
        if (zero)
            emit_buffer(cmd, VKM_GFX, VKM_BUFFER_VERTEX + ZERO_BINDING, cmd->device->zero_gpu, 0);
    }
    if (s->depth_dirty) {
        VkmDepthKey key;
        memset(&key, 0, sizeof(key));
        key.test = s->has_depth && d->depth_test;
        key.write = s->has_depth && d->depth_write;
        key.compare = d->depth_compare;
        key.stencil = s->has_stencil && d->stencil_test;
        if (key.stencil) {
            key.front = d->front_stencil;
            key.back = d->back_stencil;
        }
        id<MTLDepthStencilState> depth = vkm_depth_state(cmd->device, &key);
        if (depth && depth != s->set_depth) {
            VkmOpObject *o = vkm_emit(cmd, VKM_OP_DEPTH_STATE, sizeof(*o));
            if (o)
                o->object = depth;
            s->set_depth = depth;
        }
        s->depth_dirty = false;
    }
    if (s->raster_dirty) {
        VkmOpRaster *o = vkm_emit(cmd, VKM_OP_RASTER, sizeof(*o));
        if (o) {
            o->cull = cull_mode(d->cull);
            o->winding = d->front == VK_FRONT_FACE_CLOCKWISE ? MTLWindingClockwise : MTLWindingCounterClockwise;
            o->fill = d->polygon == VK_POLYGON_MODE_LINE ? MTLTriangleFillModeLines : MTLTriangleFillModeFill;
            o->clip = d->depth_clamp || !d->depth_clip ? MTLDepthClipModeClamp : MTLDepthClipModeClip;
            if (d->bias_enable) {
                o->bias_constant = d->bias_constant;
                o->bias_slope = d->bias_slope;
                o->bias_clamp = d->bias_clamp;
            }
        }
        s->raster_dirty = false;
    }
    if (s->viewport_dirty && d->viewport_count) {
        VkmOpViewports *o = vkm_emit(cmd, VKM_OP_VIEWPORTS, sizeof(*o));
        if (o) {
            o->count = d->viewport_count;
            for (uint32_t i = 0; i < d->viewport_count; i++) {
                const VkViewport *v = &d->viewports[i];
                o->viewports[i] = (MTLViewport){v->x, v->y, v->width, v->height, v->minDepth, v->maxDepth};
            }
        }
        s->viewport_dirty = false;
    }
    if (s->scissor_dirty && d->scissor_count) {
        VkmOpScissors *o = vkm_emit(cmd, VKM_OP_SCISSORS, sizeof(*o));
        if (o) {
            o->count = d->scissor_count;
            memcpy(o->rects, d->scissors, sizeof(VkRect2D) * d->scissor_count);
        }
        s->scissor_dirty = false;
    }
    if (s->ref_dirty) {
        VkmOpValues *o = vkm_emit(cmd, VKM_OP_STENCIL_REF, sizeof(*o));
        if (o) {
            o->u[0] = d->ref_front;
            o->u[1] = d->ref_back;
        }
        s->ref_dirty = false;
    }
    if (s->constants_dirty) {
        VkmOpValues *o = vkm_emit(cmd, VKM_OP_BLEND_CONSTANTS, sizeof(*o));
        if (o)
            memcpy(o->f, d->blend_constants, sizeof(o->f));
        s->constants_dirty = false;
    }
    emit_bindings(cmd, VKM_GFX);
    uint64_t counter = st->fs ? stats_counter(s) : 0;
    if (counter && st->fs->counted)
        emit_buffer(cmd, VKM_GFX, VKM_BUFFER_AUX, counter, ~0u);
    for (uint32_t i = 0; i < d->vertex.binding_count && st->vs->inputs; i++) {
        uint32_t b = d->vertex.bindings[i].binding;
        if (b < ZERO_BINDING && s->vertex[b].address)
            emit_buffer(cmd, VKM_GFX, VKM_BUFFER_VERTEX + b, s->vertex[b].address, d->strides[b]);
    }
    s->drawn = true;
    return true;
}

static bool prepare_draw(VkCommandBuffer cmd)
{
    VkmGfxState *s = STATE(cmd);
    Stages st = {s->vs, s->fs, (uint8_t)vkm_topology_class(s->dyn.topology), s->dyn.discard};
    return prepare_stages(cmd, &st);
}

/*
 * metal draws no triangle fans, so a fan is the list of its triangles, vertices i + 1, i + 2 and the first, in the
 * order vulkan gives them. the indices are made when the draw is recorded: an indexed fan reads the ones bound then,
 * cut by a restart index when restarts are on.
 */
static void draw_fan(VkCommandBuffer cmd, uint32_t count, uint32_t instances, uint32_t first, int32_t base_vertex,
                     uint32_t first_instance, bool indexed)
{
    VkmGfxState *s = STATE(cmd);
    bool wide = s->index_type == MTLIndexTypeUInt32;
    if (indexed && (!s->index_host || ((uint64_t)first + count) * (wide ? 4 : 2) > s->index_size))
        return;
    if (count < 3)
        return;
    uint64_t gpu;
    uint32_t *out = vkm_upload(cmd, (uint64_t)(count - 2) * 12, 4, &gpu, nullptr, nullptr);
    if (!out)
        return;
    uint32_t restart = wide ? 0xFFFFFFFFu : 0xFFFFu, n = 0, hub = 0, run = 0, previous = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t v = !indexed ? i : wide ? ((const uint32_t *)s->index_host)[first + i] :
                                           ((const uint16_t *)s->index_host)[first + i];
        if (indexed && s->dyn.restart && v == restart) {
            run = 0;
            continue;
        }
        if (!run)
            hub = v;
        else if (run >= 2) {
            out[n++] = previous;
            out[n++] = v;
            out[n++] = hub;
        }
        previous = v;
        run++;
    }
    if (!n || !prepare_draw(cmd))
        return;
    VkmOpDraw *o = vkm_emit(cmd, VKM_OP_DRAW_INDEXED, sizeof(*o));
    if (!o)
        return;
    o->primitive = MTLPrimitiveTypeTriangle;
    o->index_type = MTLIndexTypeUInt32;
    o->count = n;
    o->instances = instances;
    o->base_vertex = indexed ? base_vertex : (int32_t)first;
    o->base_instance = first_instance;
    o->index = gpu;
    o->index_length = (uint64_t)n * 4;
}

/* the primitives a topology makes of a count of vertices, and how far apart their first vertices are. */
static uint32_t primitives(VkPrimitiveTopology topology, uint32_t count, uint32_t vertices, uint32_t *step,
                           bool *swap)
{
    *swap = false;
    switch (topology) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: *step = 1; return vertices == 1 ? count : 0;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: *step = 2; return vertices == 2 ? count / 2 : 0;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: *step = 1; return vertices == 2 && count >= 2 ? count - 1 : 0;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: *step = 3; return vertices == 3 ? count / 3 : 0;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
        *step = 1;
        *swap = true;
        return vertices == 3 && count >= 3 ? count - 2 : 0;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY: *step = 4; return vertices == 4 ? count / 4 : 0;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY: *step = 1; return vertices == 4 && count >= 4 ? count - 3 : 0;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: *step = 6; return vertices == 6 ? count / 6 : 0;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
        *step = 2;
        return vertices == 6 && count >= 6 ? (count - 4) / 2 : 0;
    default: return 0;
    }
}

/* the records a vertex shader writes an instance: one a vertex index the draw reads. */
static uint32_t record_slots(VkmGfxState *s, bool indexed, uint32_t first, uint32_t count)
{
    if (!indexed)
        return count;
    uint32_t top = 0;
    bool wide = s->index_type == MTLIndexTypeUInt32;
    uint32_t restart = wide ? ~0u : 0xffffu;
    if (s->index_host && ((uint64_t)first + count) * (wide ? 4 : 2) <= s->index_size) {
        for (uint32_t i = 0; i < count; i++) {
            uint32_t v = wide ? ((const uint32_t *)s->index_host)[first + i] : ((const uint16_t *)s->index_host)[first + i];
            if (v != restart)
                top = MIME_MAX(top, v);
        }
        return top + 1;
    }
    /* indices only the gpu sees go as far as the vertex buffers do. */
    const VkmDynamic *d = &s->dyn;
    top = wide ? 1u << 20 : 1u << 16;
    for (uint32_t i = 0; i < d->vertex.binding_count; i++) {
        const VkVertexInputBindingDescription2EXT *b = &d->vertex.bindings[i];
        if (b->binding < ZERO_BINDING && b->inputRate == VK_VERTEX_INPUT_RATE_VERTEX && d->strides[b->binding])
            top = MIME_MIN(top, (uint32_t)MIME_MIN(s->vertex[b->binding].size / d->strides[b->binding], 1u << 24));
    }
    return MIME_MAX(top, 1u);
}

/* a stream output step: what stream output took of the records, or the counters at its end. */
static void xfb_pass(VkCommandBuffer cmd, VkmXfbParams *p, const VkmXfb *layout)
{
    VkmGfxState *s = STATE(cmd);
    id<MTLRenderPipelineState> pipeline = vkm_meta_xfb(cmd->device, s->colors, s->samples);
    uint64_t gpu;
    VkmXfbParams *x = vkm_upload(cmd, sizeof(*x), 16, &gpu, nullptr, nullptr);
    if (!pipeline || !x)
        return;
    for (uint32_t b = 0; b < 4; b++) {
        p->buffers[b] = s->xfb[b].address;
        p->sizes[b] = (uint32_t)MIME_MIN(s->xfb[b].size, (uint64_t)UINT32_MAX);
        if (!p->counters[b] && p->mode != 2)
            p->counters[b] = s->xfb_counters[b];
    }
    p->state = s->xfb_state;
    if (s->xfb_query && s->xfb_query_index < s->xfb_query->count)
        p->query = s->xfb_query->buffer.gpuAddress + (uint64_t)s->xfb_query_index * 16;
    if (layout) {
        memcpy(p->strides, layout->strides, sizeof(p->strides));
        p->entry_count = layout->count;
        memcpy(p->entries, layout->entries, sizeof(p->entries));
    }
    *x = *p;
    VkmOpObject *o = vkm_emit(cmd, VKM_OP_PIPELINE, sizeof(*o));
    if (o)
        o->object = pipeline;
    s->set_pipeline = pipeline;
    s->pipeline_dirty = true;
    emit_buffer(cmd, VKM_GFX, 0, gpu, ~0u);
    s->set_buffers[VKM_GFX][0] = 0;
    VkmOpDraw *d = vkm_emit(cmd, VKM_OP_DRAW, sizeof(*d));
    if (d) {
        d->primitive = MTLPrimitiveTypePoint;
        d->count = d->instances = 1;
    }
    vkm_emit(cmd, VKM_OP_VERTEX_BARRIER, sizeof(VkmOp));
}

/* the words of the buffer a vertex shader writes its records to, and its header. */
static uint32_t *records_begin(VkCommandBuffer cmd, bool indexed, uint32_t count, uint32_t instances, uint32_t first,
                               int32_t base_vertex, uint32_t first_instance, uint64_t extra, uint64_t *emu,
                               uint64_t *in_words)
{
    VkmGfxState *s = STATE(cmd);
    uint32_t cap = record_slots(s, indexed, first, count);
    *in_words = (uint64_t)instances * cap * s->vs->record_words;
    uint64_t words = VKM_GS_HEADER + *in_words + extra;
    if (words * 4 > (1ull << 30)) {
        VKM_ERROR("a draw of %llu words of records", (unsigned long long)words);
        return nullptr;
    }
    uint32_t *h = vkm_upload(cmd, words * 4, 16, emu, nullptr, nullptr);
    if (!h)
        return nullptr;
    memset(h, 0, VKM_GS_HEADER * 4);
    uint32_t index_size = s->index_type == MTLIndexTypeUInt32 ? 4 : 2;
    h[VKM_GS_VBASE] = indexed ? (uint32_t)base_vertex : first;
    h[VKM_GS_CAP] = cap;
    h[VKM_GS_IN_BASE] = VKM_GS_HEADER;
    h[VKM_GS_IN_STRIDE] = s->vs->record_words;
    h[VKM_GS_BASE_INSTANCE] = first_instance;
    h[VKM_GS_IN_BUILTINS] = s->vs->record_builtins;
    h[VKM_GS_INDEX_TYPE] = indexed ? (index_size == 4 ? 2 : 1) : 0;
    h[VKM_GS_INDEX_FIRST] = indexed ? first + (uint32_t)(s->index_address & 3) / index_size : 0;
    return h;
}

/* the vertex shader with rasterization off, writing its records. */
static bool records_pass(VkCommandBuffer cmd, const VkmCode *capture, bool indexed, uint32_t count,
                         uint32_t instances, uint32_t first, int32_t base_vertex, uint32_t first_instance, uint64_t emu)
{
    VkmGfxState *s = STATE(cmd);
    uint32_t index_size = s->index_type == MTLIndexTypeUInt32 ? 4 : 2;
    s->pipeline_dirty = true;
    Stages st = {capture, nullptr, (uint8_t)vkm_topology_class(s->dyn.topology), 1};
    if (!prepare_stages(cmd, &st))
        return false;
    emit_buffer(cmd, VKM_GFX, VKM_BUFFER_AUX, emu, ~0u);
    VkmOpDraw *o = vkm_emit(cmd, indexed ? VKM_OP_DRAW_INDEXED : VKM_OP_DRAW, sizeof(*o));
    if (!o)
        return false;
    o->primitive = vkm_primitive(s->dyn.topology);
    o->count = count;
    o->instances = instances;
    o->first = first;
    o->base_instance = first_instance;
    if (indexed) {
        o->index_type = s->index_type;
        o->base_vertex = base_vertex;
        o->index = s->index_address + (uint64_t)first * index_size;
        o->index_length = s->index_size > (uint64_t)first * index_size ? s->index_size - (uint64_t)first * index_size : 0;
    }
    vkm_emit(cmd, VKM_OP_VERTEX_BARRIER, sizeof(VkmOp));
    return true;
}

/* the vertices of a primitive of a list or strip topology, as stream output takes them. */
static uint32_t stream_vertices(VkPrimitiveTopology topology, uint32_t *step, bool *swap)
{
    *swap = topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    switch (topology) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: *step = 1; return 1;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: *step = 2; return 2;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: *step = 1; return 2;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: *step = 3; return 3;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: *step = 1; return 3;
    default: return 0;
    }
}

/* a draw whose vertex shader's outputs stream out: its records, then what stream output takes of them. */
static void draw_streamed(VkCommandBuffer cmd, bool indexed, uint32_t count, uint32_t instances, uint32_t first,
                          int32_t base_vertex, uint32_t first_instance)
{
    VkmGfxState *s = STATE(cmd);
    const VkmCode *capture = vkm_code_capture(cmd->device, s->vs);
    uint32_t step;
    bool swap;
    uint32_t verts = stream_vertices(s->dyn.topology, &step, &swap);
    if (!capture || !verts || (indexed && !s->index_address))
        return;
    uint64_t emu, in_words;
    uint32_t *h = records_begin(cmd, indexed, count, instances, first, base_vertex, first_instance, 0, &emu, &in_words);
    if (!h || !records_pass(cmd, capture, indexed, count, instances, first, base_vertex, first_instance, emu))
        goto done;
    VkmXfbParams p = {};
    p.emu = emu;
    p.mode = 0;
    p.verts = verts;
    p.prims = topology_primitives(s->dyn.topology, count);
    p.instances = instances;
    p.mul = step;
    p.swap = swap;
    p.index = s->index_address;
    p.index_type = h[VKM_GS_INDEX_TYPE];
    p.index_first = indexed ? first : 0;
    p.cap = h[VKM_GS_CAP];
    p.in_base = VKM_GS_HEADER;
    p.in_stride = s->vs->record_words;
    xfb_pass(cmd, &p, s->vs->xfb);
done:
    s->pipeline_dirty = true;
    s->set_buffers[VKM_GFX][VKM_BUFFER_AUX] = 0;
}

/*
 * a draw through a geometry shader: the vertex shader writes the records of its vertices with rasterization off,
 * the geometry shader runs a vertex function a primitive and invocation that writes its own and their strips, and
 * its replay draws them through the fragment shader. barriers between the three keep them in order inside the pass.
 */
static void draw_geometry(VkCommandBuffer cmd, bool indexed, uint32_t count, uint32_t instances, uint32_t first,
                          int32_t base_vertex, uint32_t first_instance)
{
    VkmGfxState *s = STATE(cmd);
    const VkmCode *gs = s->gs, *capture = vkm_code_capture(cmd->device, s->vs);
    if (!capture || !gs->expand || !gs->replay || (indexed && !s->index_address))
        return;
    const VkmGsInfo *g = &gs->gs;
    uint32_t step;
    bool swap;
    uint32_t prims = primitives(s->dyn.topology, count, g->in_vertices, &step, &swap);
    if (!prims)
        return;
    uint64_t invocations = (uint64_t)prims * g->invocations;
    uint64_t out_words = (uint64_t)instances * invocations * g->max_vertices * g->out_stride;
    uint64_t idx_words = g->out_kind ? (uint64_t)instances * invocations * g->max_vertices * 2 : 0;
    uint64_t count_words = (uint64_t)instances * invocations;
    if (invocations * instances > UINT32_MAX / (2 * g->max_vertices)) {
        VKM_ERROR("a geometry shader draw of %llu invocations", (unsigned long long)(invocations * instances));
        return;
    }
    uint64_t emu, in_words;
    uint32_t *h = records_begin(cmd, indexed, count, instances, first, base_vertex, first_instance,
                                out_words + idx_words + count_words, &emu, &in_words);
    if (!h)
        return;
    uint64_t index_at = s->index_address & ~3ull;
    h[VKM_GS_PRIMS] = prims;
    h[VKM_GS_MUL] = step;
    h[VKM_GS_SWAP] = swap;
    h[VKM_GS_OUT_BASE] = (uint32_t)(VKM_GS_HEADER + in_words);
    h[VKM_GS_IDX_BASE] = (uint32_t)(VKM_GS_HEADER + in_words + out_words);
    h[VKM_GS_COUNT_BASE] = (uint32_t)(VKM_GS_HEADER + in_words + out_words + idx_words);
    VkmOpDraw *o;
    Stages st;
    if (!records_pass(cmd, capture, indexed, count, instances, first, base_vertex, first_instance, emu))
        goto done;

    /* the geometry shader's. */
    s->pipeline_dirty = true;
    st = (Stages){gs->expand, nullptr, MTLPrimitiveTopologyClassPoint, 1};
    if (!prepare_stages(cmd, &st))
        goto done;
    emit_buffer(cmd, VKM_GFX, VKM_BUFFER_VERTEX, emu, ~0u);
    emit_buffer(cmd, VKM_GFX, VKM_BUFFER_VERTEX + 1, indexed ? index_at : emu, ~0u);
    if (!(o = vkm_emit(cmd, VKM_OP_DRAW, sizeof(*o))))
        goto done;
    o->primitive = MTLPrimitiveTypePoint;
    o->count = (uint32_t)invocations;
    o->instances = instances;
    vkm_emit(cmd, VKM_OP_VERTEX_BARRIER, sizeof(VkmOp));
    if (s->xfb_state && gs->xfb) {
        VkmXfbParams p = {};
        p.emu = emu;
        p.mode = 1;
        p.verts = g->out_kind + 1;
        p.instances = instances;
        p.invocations = (uint32_t)invocations;
        p.max_vertices = g->max_vertices;
        p.out_base = h[VKM_GS_OUT_BASE];
        p.out_stride = g->out_stride;
        p.idx_base = h[VKM_GS_IDX_BASE];
        p.counts_base = h[VKM_GS_COUNT_BASE];
        p.strip = g->out_kind != 0;
        xfb_pass(cmd, &p, gs->xfb);
    }

    /* and the draw of what it emitted. */
    static const uint8_t classes[] = {MTLPrimitiveTopologyClassPoint, MTLPrimitiveTopologyClassLine,
                                      MTLPrimitiveTopologyClassTriangle};
    static const uint8_t strips[] = {MTLPrimitiveTypePoint, MTLPrimitiveTypeLineStrip, MTLPrimitiveTypeTriangleStrip};
    s->pipeline_dirty = true;
    st = (Stages){gs->replay, s->fs, classes[g->out_kind], s->dyn.discard};
    if (!s->dyn.discard && prepare_stages(cmd, &st)) {
        emit_buffer(cmd, VKM_GFX, VKM_BUFFER_VERTEX, emu, ~0u);
        uint64_t total = (uint64_t)instances * invocations * g->max_vertices;
        if ((o = vkm_emit(cmd, g->out_kind ? VKM_OP_DRAW_INDEXED : VKM_OP_DRAW, sizeof(*o)))) {
            o->primitive = strips[g->out_kind];
            o->instances = 1;
            if (g->out_kind) {
                o->index_type = MTLIndexTypeUInt32;
                o->count = (uint32_t)(total * 2);
                o->index = emu + (uint64_t)h[VKM_GS_IDX_BASE] * 4;
                o->index_length = idx_words * 4;
            } else {
                o->count = (uint32_t)total;
            }
        }
    }
done:
    s->pipeline_dirty = true;
    for (uint32_t i = VKM_BUFFER_AUX; i <= VKM_BUFFER_VERTEX + 1; i++)
        s->set_buffers[VKM_GFX][i] = 0;
}

static VKAPI_ATTR void VKAPI_CALL CmdDraw(VkCommandBuffer cmd, uint32_t vertices, uint32_t instances, uint32_t first,
                                          uint32_t first_instance)
{
    VkmGfxState *s = STATE(cmd);
    count_draw(cmd, vertices, instances);
    if (s->gs) {
        if (vertices && instances)
            draw_geometry(cmd, false, vertices, instances, first, 0, first_instance);
        return;
    }
    if (vertices && instances && s->xfb_state && s->vs && s->vs->xfb) {
        draw_streamed(cmd, false, vertices, instances, first, 0, first_instance);
        if (s->dyn.discard)
            return;
    }
    if (vertices && instances && STATE(cmd)->dyn.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) {
        draw_fan(cmd, vertices, instances, first, 0, first_instance, false);
        return;
    }
    if (!vertices || !instances || !prepare_draw(cmd))
        return;
    VkmOpDraw *o = vkm_emit(cmd, VKM_OP_DRAW, sizeof(*o));
    if (!o)
        return;
    o->primitive = vkm_primitive(STATE(cmd)->dyn.topology);
    o->count = vertices;
    o->instances = instances;
    o->first = first;
    o->base_instance = first_instance;
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndexed(VkCommandBuffer cmd, uint32_t indices, uint32_t instances,
                                                 uint32_t first, int32_t base_vertex, uint32_t first_instance)
{
    VkmGfxState *s = STATE(cmd);
    count_draw(cmd, indices, instances);
    if (s->gs) {
        if (indices && instances)
            draw_geometry(cmd, true, indices, instances, first, base_vertex, first_instance);
        return;
    }
    if (indices && instances && s->xfb_state && s->vs && s->vs->xfb) {
        draw_streamed(cmd, true, indices, instances, first, base_vertex, first_instance);
        if (s->dyn.discard)
            return;
    }
    if (indices && instances && s->index_address && s->dyn.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) {
        draw_fan(cmd, indices, instances, first, base_vertex, first_instance, true);
        return;
    }
    if (!indices || !instances || !s->index_address || !prepare_draw(cmd))
        return;
    VkmOpDraw *o = vkm_emit(cmd, VKM_OP_DRAW_INDEXED, sizeof(*o));
    if (!o)
        return;
    uint32_t size = s->index_type == MTLIndexTypeUInt32 ? 4 : 2;
    o->primitive = vkm_primitive(s->dyn.topology);
    o->index_type = s->index_type;
    o->count = indices;
    o->instances = instances;
    o->base_vertex = base_vertex;
    o->base_instance = first_instance;
    o->index = s->index_address + (uint64_t)first * size;
    o->index_length = s->index_size > (uint64_t)first * size ? s->index_size - (uint64_t)first * size : 0;
}

static bool indirect_geometry(VkCommandBuffer cmd)
{
    if (!STATE(cmd)->gs)
        return false;
    static bool told;
    if (!told) {
        told = true;
        VKM_ERROR("no indirect draws through a geometry shader");
    }
    return true;
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndirect(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset,
                                                  uint32_t count, uint32_t stride)
{
    if (!count || indirect_geometry(cmd) || !prepare_draw(cmd))
        return;
    VkmOpDraw *o = vkm_emit(cmd, VKM_OP_DRAW_INDIRECT, sizeof(*o));
    if (!o)
        return;
    o->primitive = vkm_primitive(STATE(cmd)->dyn.topology);
    o->indirect = buffer->gpu + offset;
    o->draws = count;
    o->stride = stride ? stride : sizeof(VkDrawIndirectCommand);
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndexedIndirect(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset,
                                                         uint32_t count, uint32_t stride)
{
    VkmGfxState *s = STATE(cmd);
    if (!count || !s->index_address || indirect_geometry(cmd) || !prepare_draw(cmd))
        return;
    VkmOpDraw *o = vkm_emit(cmd, VKM_OP_DRAW_INDEXED_INDIRECT, sizeof(*o));
    if (!o)
        return;
    o->primitive = vkm_primitive(s->dyn.topology);
    o->index_type = s->index_type;
    o->index = s->index_address;
    o->index_length = s->index_size;
    o->indirect = buffer->gpu + offset;
    o->draws = count;
    o->stride = stride ? stride : sizeof(VkDrawIndexedIndirectCommand);
}

/* ------------------------------------------------------------------ dispatches. */

static bool prepare_dispatch(VkCommandBuffer cmd)
{
    VkmGfxState *s = STATE(cmd);
    if (s->rendering || !s->compute)
        return false;
    if (s->compute != s->set_compute) {
        VkmOpCompute *o = vkm_emit(cmd, VKM_OP_COMPUTE_PIPELINE, sizeof(*o));
        if (!o)
            return false;
        o->pipeline = s->compute;
        o->local = s->cs->local;
        s->set_compute = s->compute;
    }
    emit_bindings(cmd, VKM_COMPUTE);
    return true;
}

static VKAPI_ATTR void VKAPI_CALL CmdDispatch(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t z)
{
    if (!x || !y || !z || !prepare_dispatch(cmd))
        return;
    VkmGfxState *s = STATE(cmd);
    if (s->stats) {
        uint64_t add[VKM_STATS] = {};
        add[10] = (uint64_t)x * y * z * s->cs->local.width * s->cs->local.height * s->cs->local.depth;
        count_stats(cmd, add);
    }
    VkmOpDispatch *o = vkm_emit(cmd, VKM_OP_DISPATCH, sizeof(*o));
    if (o) {
        o->x = x;
        o->y = y;
        o->z = z;
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdDispatchBase(VkCommandBuffer cmd, uint32_t bx, uint32_t by, uint32_t bz,
                                                  uint32_t x, uint32_t y, uint32_t z)
{
    CmdDispatch(cmd, x, y, z);
}

static VKAPI_ATTR void VKAPI_CALL CmdDispatchIndirect(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset)
{
    if (!prepare_dispatch(cmd))
        return;
    VkmOpDispatch *o = vkm_emit(cmd, VKM_OP_DISPATCH_INDIRECT, sizeof(*o));
    if (o)
        o->indirect = buffer->gpu + offset;
}

/* ------------------------------------------------------------------ transform feedback. */

static void *emit_before_pass(VkCommandBuffer cmd, uint32_t op, size_t size);

static VKAPI_ATTR void VKAPI_CALL CmdBindTransformFeedbackBuffersEXT(VkCommandBuffer cmd, uint32_t first,
                                                                     uint32_t count, const VkBuffer *buffers,
                                                                     const VkDeviceSize *offsets,
                                                                     const VkDeviceSize *sizes)
{
    VkmGfxState *s = STATE(cmd);
    for (uint32_t i = 0; i < count && first + i < 4; i++) {
        VkBuffer b = buffers[i];
        uint64_t offset = offsets[i];
        s->xfb[first + i].address = b ? b->gpu + offset : 0;
        s->xfb[first + i].size = !b ? 0 : !sizes || sizes[i] == VK_WHOLE_SIZE ? b->size - offset : sizes[i];
    }
}

static void counters_of(uint64_t out[4], uint32_t first, uint32_t count, const VkBuffer *buffers,
                        const VkDeviceSize *offsets)
{
    memset(out, 0, sizeof(uint64_t) * 4);
    for (uint32_t i = 0; buffers && i < count && first + i < 4; i++)
        if (buffers[i])
            out[first + i] = buffers[i]->gpu + (offsets ? offsets[i] : 0);
}

/* the offsets start at the counters, or at zero, when the first draw writes. */
static VKAPI_ATTR void VKAPI_CALL CmdBeginTransformFeedbackEXT(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                                                               const VkBuffer *counters, const VkDeviceSize *offsets)
{
    VkmGfxState *s = STATE(cmd);
    uint64_t gpu;
    uint32_t *state = vkm_upload(cmd, 32, 16, &gpu, nullptr, nullptr);
    if (!state)
        return;
    memset(state, 0, 32);
    s->xfb_state = gpu;
    counters_of(s->xfb_counters, first, count, counters, offsets);
}

static VKAPI_ATTR void VKAPI_CALL CmdEndTransformFeedbackEXT(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                                                             const VkBuffer *counters, const VkDeviceSize *offsets)
{
    VkmGfxState *s = STATE(cmd);
    if (!s->xfb_state)
        return;
    VkmXfbParams p = {.mode = 2};
    counters_of(p.counters, first, count, counters, offsets);
    if (s->rendering)
        xfb_pass(cmd, &p, nullptr);
    s->xfb_state = 0;
}

/* a draw of the vertices stream output wrote: their count comes from the counter, before the render pass. */
static void *emit_before_pass(VkCommandBuffer cmd, uint32_t op, size_t size);

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndirectByteCountEXT(VkCommandBuffer cmd, uint32_t instances,
                                                              uint32_t first_instance, VkBuffer counter,
                                                              VkDeviceSize counter_offset, uint32_t vertex_offset,
                                                              uint32_t stride)
{
    VkmGfxState *s = STATE(cmd);
    if (!instances || !s->rendering || indirect_geometry(cmd))
        return;
    struct {
        uint64_t counter, out;
        uint32_t vertex_offset, stride, instances, first_instance;
    } *p;
    uint64_t params, args;
    id<MTLComputePipelineState> pipeline = vkm_meta_byte_count(cmd->device);
    p = vkm_upload(cmd, sizeof(*p), 16, &params, nullptr, nullptr);
    if (!pipeline || !p || !vkm_upload(cmd, 16, 16, &args, nullptr, nullptr))
        return;
    *p = (typeof(*p)){counter->gpu + counter_offset, args, vertex_offset, stride, instances, first_instance};
    VkmOpGenerate *g = emit_before_pass(cmd, VKM_OP_GENERATE, sizeof(*g));
    if (!g) {
        VKM_ERROR("a draw of stream output in a render pass another command buffer began");
        return;
    }
    g->pipeline = pipeline;
    g->threads = 1;
    g->params = g->icb = params;
    if (!prepare_draw(cmd))
        return;
    VkmOpDraw *o = vkm_emit(cmd, VKM_OP_DRAW_INDIRECT, sizeof(*o));
    if (!o)
        return;
    o->primitive = vkm_primitive(s->dyn.topology);
    o->indirect = args;
    o->draws = 1;
    o->stride = 16;
}

/* ------------------------------------------------------------------ generated commands. */

/* more sequences than this are cut, the indirect command buffer being one command a sequence. */
#define DGC_MAX (1u << 20)

static VKAPI_ATTR VkResult VKAPI_CALL CreateIndirectCommandsLayoutEXT(VkDevice device,
                                                                      const VkIndirectCommandsLayoutCreateInfoEXT *info,
                                                                      const VkAllocationCallbacks *a,
                                                                      VkIndirectCommandsLayoutEXT *out)
{
    if (info->tokenCount > VKM_DGC_TOKENS)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkIndirectCommandsLayoutEXT l = vkm_zalloc(sizeof(*l));
    if (!l)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    l->stride = info->indirectStride;
    l->token_count = info->tokenCount;
    for (uint32_t i = 0; i < info->tokenCount; i++) {
        const VkIndirectCommandsLayoutTokenEXT *t = &info->pTokens[i];
        VkmDgcToken *k = &l->tokens[i];
        k->offset = t->offset;
        switch (t->type) {
        case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT:
        case VK_INDIRECT_COMMANDS_TOKEN_TYPE_SEQUENCE_INDEX_EXT: {
            const VkPushConstantRange *r = &t->data.pPushConstant->updateRange;
            if (r->offset + r->size > VKM_PUSH_SIZE || ((r->offset | r->size | t->offset) & 3))
                goto unsupported;
            k->type = t->type == VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT ? VKM_DGC_PUSH : VKM_DGC_SEQUENCE;
            k->dst = r->offset;
            k->size = r->size;
            l->pushes = true;
            break;
        }
        case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_EXT:
            k->type = VKM_DGC_VERTEX;
            k->dst = t->data.pVertexBuffer->vertexBindingUnit;
            break;
        case VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_EXT:
            k->type = VKM_DGC_INDEX;
            k->dst = t->data.pIndexBuffer->mode == VK_INDIRECT_COMMANDS_INPUT_MODE_DXGI_INDEX_BUFFER_EXT;
            break;
        case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_EXT: k->type = VKM_DGC_DRAW; break;
        case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_EXT: k->type = VKM_DGC_DRAW_INDEXED; break;
        case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT:
            k->type = VKM_DGC_DISPATCH;
            l->dispatch = true;
            break;
        default:
            goto unsupported;
        }
        if (t->offset & 3)
            goto unsupported;
    }
    *out = l;
    return VK_SUCCESS;
unsupported:
    VKM_ERROR("no generated commands for this layout");
    free(l);
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

static VKAPI_ATTR void VKAPI_CALL DestroyIndirectCommandsLayoutEXT(VkDevice device, VkIndirectCommandsLayoutEXT l,
                                                                   const VkAllocationCallbacks *a)
{
    free(l);
}

/* the preprocess memory holds each sequence's push constants. */
static VKAPI_ATTR void VKAPI_CALL GetGeneratedCommandsMemoryRequirementsEXT(
    VkDevice device, const VkGeneratedCommandsMemoryRequirementsInfoEXT *info, VkMemoryRequirements2 *out)
{
    VkIndirectCommandsLayoutEXT l = info->indirectCommandsLayout;
    uint32_t max = MIME_MIN(info->maxSequenceCount, DGC_MAX);
    out->memoryRequirements.size = l && l->pushes ? (VkDeviceSize)max * VKM_PUSH_SIZE : 0;
    out->memoryRequirements.alignment = 256;
    out->memoryRequirements.memoryTypeBits = (1u << VKM_MEMORY_TYPES) - 1;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateIndirectExecutionSetEXT(VkDevice device,
                                                                    const VkIndirectExecutionSetCreateInfoEXT *info,
                                                                    const VkAllocationCallbacks *a,
                                                                    VkIndirectExecutionSetEXT *out)
{
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

static VKAPI_ATTR void VKAPI_CALL DestroyIndirectExecutionSetEXT(VkDevice device, VkIndirectExecutionSetEXT set,
                                                                 const VkAllocationCallbacks *a)
{
}

static VKAPI_ATTR void VKAPI_CALL UpdateIndirectExecutionSetPipelineEXT(
    VkDevice device, VkIndirectExecutionSetEXT set, uint32_t count, const VkWriteIndirectExecutionSetPipelineEXT *w)
{
}

static VKAPI_ATTR void VKAPI_CALL UpdateIndirectExecutionSetShaderEXT(VkDevice device, VkIndirectExecutionSetEXT set,
                                                                      uint32_t count,
                                                                      const VkWriteIndirectExecutionSetShaderEXT *w)
{
}

/* the commands are generated where they run. */
static VKAPI_ATTR void VKAPI_CALL CmdPreprocessGeneratedCommandsEXT(VkCommandBuffer cmd,
                                                                    const VkGeneratedCommandsInfoEXT *info,
                                                                    VkCommandBuffer state)
{
}

static VkmIcb *icb_get(VkCommandBuffer cmd, bool dispatch, uint32_t count)
{
    for (VkmIcb *b = cmd->icbs; b; b = b->next)
        if (!b->used && b->dispatch == dispatch && b->capacity >= count) {
            b->used = true;
            return b;
        }
    uint32_t capacity = 64;
    while (capacity < count)
        capacity *= 2;
    MTLIndirectCommandBufferDescriptor *desc = [[MTLIndirectCommandBufferDescriptor alloc] init];
    desc.inheritPipelineState = true;
    desc.inheritBuffers = false;
    if (dispatch) {
        desc.commandTypes = MTLIndirectCommandTypeConcurrentDispatch;
        desc.maxKernelBufferBindCount = VKM_BUFFER_VERTEX;
    } else {
        desc.commandTypes = MTLIndirectCommandTypeDraw | MTLIndirectCommandTypeDrawIndexed;
        desc.maxVertexBufferBindCount = VKM_BUFFER_VERTEX + VKM_VERTEX_BINDINGS;
        desc.maxFragmentBufferBindCount = VKM_BUFFER_VERTEX;
        desc.supportDynamicAttributeStride = true;
    }
    id<MTLIndirectCommandBuffer> icb =
        [cmd->device->mtl newIndirectCommandBufferWithDescriptor:desc
                                                 maxCommandCount:capacity
                                                         options:MTLResourceStorageModePrivate];
    [desc release];
    VkmIcb *b = icb ? vkm_zalloc(sizeof(*b)) : nullptr;
    if (!b) {
        VKM_ERROR("no indirect command buffer of %u commands", capacity);
        [icb release];
        return nullptr;
    }
    b->icb = icb;
    b->rid = icb.gpuResourceID;
    b->capacity = capacity;
    b->dispatch = dispatch;
    b->used = true;
    b->next = cmd->icbs;
    cmd->icbs = b;
    vkm_resident(cmd->device, icb);
    return b;
}

/* an op ahead of the render pass being recorded, where a compute encoder runs it. */
static void *emit_before_pass(VkCommandBuffer cmd, uint32_t op, size_t size)
{
    VkmGfxState *s = STATE(cmd);
    if (s->begin >= cmd->cmd_size || ((const VkmOp *)(cmd->cmds + s->begin))->op != VKM_OP_BEGIN_RENDER)
        return nullptr;
    size = MIME_ALIGN_UP(size, (size_t)8);
    size_t end = cmd->cmd_size;
    if (!vkm_emit(cmd, 0, size))
        return nullptr;
    memmove(cmd->cmds + s->begin + size, cmd->cmds + s->begin, end - s->begin);
    VkmOp *o = (VkmOp *)(cmd->cmds + s->begin);
    memset(o, 0, size);
    o->op = op;
    o->size = (uint32_t)size;
    s->begin += size;
    return o;
}

/*
 * each sequence is a command of an indirect command buffer, which a kernel encodes from the arguments with the
 * state bound now and what the sequence's tokens change. the commands inherit the pipeline, resolved for indirect
 * command buffers. a render pass cannot run the kernel, so it runs before the pass begins: what it reads was
 * written before the pass, as nothing inside one makes indirect arguments visible to it.
 */
static VKAPI_ATTR void VKAPI_CALL CmdExecuteGeneratedCommandsEXT(VkCommandBuffer cmd, VkBool32 preprocessed,
                                                                 const VkGeneratedCommandsInfoEXT *info)
{
    VkmGfxState *s = STATE(cmd);
    VkIndirectCommandsLayoutEXT l = info->indirectCommandsLayout;
    uint32_t max = MIME_MIN(info->maxSequenceCount, DGC_MAX);
    if (!l || !max)
        return;
    bool dispatch = l->dispatch;
    int point = dispatch ? VKM_COMPUTE : VKM_GFX;
    id<MTLComputePipelineState> normal = s->compute;
    if (dispatch) {
        if (!s->cs || !s->compute || !(s->compute = vkm_compute_pipeline(cmd->device, s->cs, true)) ||
            !prepare_dispatch(cmd))
            goto done;
    } else {
        s->icb = s->pipeline_dirty = true;
        bool ready = prepare_draw(cmd);
        s->icb = false;
        s->pipeline_dirty = true;
        if (!ready)
            return;
    }
    uint32_t chunks = (max + VKM_DGC_CHUNK - 1) / VKM_DGC_CHUNK;
    uint64_t params_gpu, ranges_gpu, rid_gpu;
    VkmDgcParams *p = vkm_upload(cmd, sizeof(*p), 16, &params_gpu, nullptr, nullptr);
    void *ranges = vkm_upload(cmd, (uint64_t)chunks * 8, 8, &ranges_gpu, nullptr, nullptr);
    MTLResourceID *rid = vkm_upload(cmd, sizeof(*rid), 8, &rid_gpu, nullptr, nullptr);
    VkmIcb *icb = icb_get(cmd, dispatch, max);
    id<MTLComputePipelineState> generator = vkm_meta_generate(cmd->device, dispatch);
    if (!p || !ranges || !rid || !icb || !generator)
        goto done;
    memset(p, 0, sizeof(*p));
    p->args = info->indirectAddress;
    p->count = info->sequenceCountAddress;
    p->ranges = ranges_gpu;
    p->stride = l->stride;
    p->max_count = max;
    p->token_count = l->token_count;
    memcpy(p->tokens, l->tokens, sizeof(p->tokens));
    memcpy(p->buffers, s->set_buffers[point], sizeof(p->buffers));
    memcpy(p->strides, s->set_strides, sizeof(p->strides));
    memcpy(p->push, s->push, sizeof(p->push));
    if (l->pushes) {
        p->pushes = info->preprocessAddress;
        if ((!p->pushes || info->preprocessSize < (uint64_t)max * VKM_PUSH_SIZE) &&
            !vkm_upload(cmd, (uint64_t)max * VKM_PUSH_SIZE, 256, &p->pushes, nullptr, nullptr))
            goto done;
    }
    if (dispatch) {
        p->local[0] = (uint32_t)s->cs->local.width;
        p->local[1] = (uint32_t)s->cs->local.height;
        p->local[2] = (uint32_t)s->cs->local.depth;
    } else {
        p->primitive = vkm_primitive(s->dyn.topology);
        p->index = s->index_address;
        p->index_wide = s->index_type == MTLIndexTypeUInt32;
    }
    *rid = icb->rid;
    VkmOpGenerate *g = dispatch ? vkm_emit(cmd, VKM_OP_GENERATE, sizeof(*g))
                                : emit_before_pass(cmd, VKM_OP_GENERATE, sizeof(*g));
    if (!g) {
        if (!dispatch)
            VKM_ERROR("generated draws in a render pass another command buffer began");
        goto done;
    }
    g->pipeline = generator;
    g->threads = max;
    g->params = params_gpu;
    g->icb = rid_gpu;
    if (dispatch) {
        /* the kernel took the compute encoder's pipeline and first two buffers. */
        s->set_compute = nullptr;
        s->set_buffers[VKM_COMPUTE][0] = s->set_buffers[VKM_COMPUTE][1] = 0;
        if (!prepare_dispatch(cmd))
            goto done;
    }
    VkmOpExecute *x = vkm_emit(cmd, VKM_OP_EXECUTE, sizeof(*x));
    if (x) {
        x->icb = icb->icb;
        x->dispatch = dispatch;
        x->chunks = chunks;
        x->ranges = ranges_gpu;
    }
done:
    s->compute = normal;
}

const VkmEntry vkm_cmd_entries[] = {
    VKM_ENTRY(CreateCommandPool),
    VKM_ENTRY(DestroyCommandPool),
    VKM_ENTRY(ResetCommandPool),
    VKM_ENTRY(TrimCommandPool),
    VKM_ALIAS("vkTrimCommandPoolKHR", TrimCommandPool),
    VKM_ENTRY(AllocateCommandBuffers),
    VKM_ENTRY(FreeCommandBuffers),
    VKM_ENTRY(BeginCommandBuffer),
    VKM_ENTRY(EndCommandBuffer),
    VKM_ENTRY(ResetCommandBuffer),
    VKM_ENTRY(CmdExecuteCommands),
    VKM_ENTRY(CmdSetViewport),
    VKM_ENTRY(CmdSetViewportWithCount),
    VKM_ALIAS("vkCmdSetViewportWithCountEXT", CmdSetViewportWithCount),
    VKM_ENTRY(CmdSetScissor),
    VKM_ENTRY(CmdSetScissorWithCount),
    VKM_ALIAS("vkCmdSetScissorWithCountEXT", CmdSetScissorWithCount),
    VKM_ENTRY(CmdSetLineWidth),
    VKM_ENTRY(CmdSetDepthBias),
    VKM_ENTRY(CmdSetBlendConstants),
    VKM_ENTRY(CmdSetDepthBounds),
    VKM_ENTRY(CmdSetStencilCompareMask),
    VKM_ENTRY(CmdSetStencilWriteMask),
    VKM_ENTRY(CmdSetStencilReference),
    VKM_ENTRY(CmdSetCullMode),
    VKM_ALIAS("vkCmdSetCullModeEXT", CmdSetCullMode),
    VKM_ENTRY(CmdSetFrontFace),
    VKM_ALIAS("vkCmdSetFrontFaceEXT", CmdSetFrontFace),
    VKM_ENTRY(CmdSetPrimitiveTopology),
    VKM_ALIAS("vkCmdSetPrimitiveTopologyEXT", CmdSetPrimitiveTopology),
    VKM_ENTRY(CmdSetDepthTestEnable),
    VKM_ALIAS("vkCmdSetDepthTestEnableEXT", CmdSetDepthTestEnable),
    VKM_ENTRY(CmdSetDepthWriteEnable),
    VKM_ALIAS("vkCmdSetDepthWriteEnableEXT", CmdSetDepthWriteEnable),
    VKM_ENTRY(CmdSetDepthCompareOp),
    VKM_ALIAS("vkCmdSetDepthCompareOpEXT", CmdSetDepthCompareOp),
    VKM_ENTRY(CmdSetDepthBoundsTestEnable),
    VKM_ALIAS("vkCmdSetDepthBoundsTestEnableEXT", CmdSetDepthBoundsTestEnable),
    VKM_ENTRY(CmdSetStencilTestEnable),
    VKM_ALIAS("vkCmdSetStencilTestEnableEXT", CmdSetStencilTestEnable),
    VKM_ENTRY(CmdSetStencilOp),
    VKM_ALIAS("vkCmdSetStencilOpEXT", CmdSetStencilOp),
    VKM_ENTRY(CmdSetRasterizerDiscardEnable),
    VKM_ALIAS("vkCmdSetRasterizerDiscardEnableEXT", CmdSetRasterizerDiscardEnable),
    VKM_ENTRY(CmdSetDepthBiasEnable),
    VKM_ALIAS("vkCmdSetDepthBiasEnableEXT", CmdSetDepthBiasEnable),
    VKM_ENTRY(CmdSetPrimitiveRestartEnable),
    VKM_ALIAS("vkCmdSetPrimitiveRestartEnableEXT", CmdSetPrimitiveRestartEnable),
    VKM_ENTRY(CmdSetDepthClampEnableEXT),
    VKM_ENTRY(CmdSetDepthClipEnableEXT),
    VKM_ENTRY(CmdSetPolygonModeEXT),
    VKM_ENTRY(CmdSetRasterizationSamplesEXT),
    VKM_ENTRY(CmdSetSampleMaskEXT),
    VKM_ENTRY(CmdSetAlphaToCoverageEnableEXT),
    VKM_ENTRY(CmdSetAlphaToOneEnableEXT),
    VKM_ENTRY(CmdSetColorBlendEnableEXT),
    VKM_ENTRY(CmdSetColorBlendEquationEXT),
    VKM_ENTRY(CmdSetColorWriteMaskEXT),
    VKM_ENTRY(CmdSetVertexInputEXT),
    VKM_ENTRY(CmdSetTessellationDomainOriginEXT),
    VKM_ENTRY(CmdSetLogicOpEnableEXT),
    VKM_ENTRY(CmdSetLogicOpEXT),
    VKM_ENTRY(CmdSetPatchControlPointsEXT),
    VKM_ENTRY(CmdSetRasterizationStreamEXT),
    VKM_ENTRY(CmdSetConservativeRasterizationModeEXT),
    VKM_ENTRY(CmdSetLineRasterizationModeEXT),
    VKM_ENTRY(CmdSetLineStippleEnableEXT),
    VKM_ENTRY(CmdSetDeviceMask),
    VKM_ENTRY(CmdBindShadersEXT),
    VKM_ENTRY(CmdBindPipeline),
    VKM_ENTRY(CmdBindDescriptorSets),
    VKM_ENTRY(CmdBindDescriptorSets2),
    VKM_ENTRY(CmdPushConstants),
    VKM_ENTRY(CmdPushConstants2),
    VKM_ENTRY(CmdBindVertexBuffers),
    VKM_ENTRY(CmdBindVertexBuffers2),
    VKM_ALIAS("vkCmdBindVertexBuffers2EXT", CmdBindVertexBuffers2),
    VKM_ENTRY(CmdBindIndexBuffer),
    VKM_ENTRY(CmdBindIndexBuffer2),
    VKM_ALIAS("vkCmdBindIndexBuffer2KHR", CmdBindIndexBuffer2),
    VKM_ENTRY(CmdBeginRendering),
    VKM_ALIAS("vkCmdBeginRenderingKHR", CmdBeginRendering),
    VKM_ENTRY(CmdEndRendering),
    VKM_ALIAS("vkCmdEndRenderingKHR", CmdEndRendering),
    VKM_ENTRY(CreateRenderPass),
    VKM_ENTRY(CreateRenderPass2),
    VKM_ALIAS("vkCreateRenderPass2KHR", CreateRenderPass2),
    VKM_ENTRY(DestroyRenderPass),
    VKM_ENTRY(GetRenderAreaGranularity),
    VKM_ALIAS("vkGetRenderingAreaGranularityKHR", GetRenderingAreaGranularity),
    VKM_ENTRY(CreateFramebuffer),
    VKM_ENTRY(DestroyFramebuffer),
    VKM_ENTRY(CmdBeginRenderPass),
    VKM_ENTRY(CmdBeginRenderPass2),
    VKM_ALIAS("vkCmdBeginRenderPass2KHR", CmdBeginRenderPass2),
    VKM_ENTRY(CmdNextSubpass),
    VKM_ENTRY(CmdNextSubpass2),
    VKM_ALIAS("vkCmdNextSubpass2KHR", CmdNextSubpass2),
    VKM_ENTRY(CmdEndRenderPass),
    VKM_ENTRY(CmdEndRenderPass2),
    VKM_ALIAS("vkCmdEndRenderPass2KHR", CmdEndRenderPass2),
    VKM_ENTRY(CmdBindDescriptorBuffersEXT),
    VKM_ENTRY(CmdSetDescriptorBufferOffsetsEXT),
    VKM_ENTRY(CmdBindDescriptorBufferEmbeddedSamplersEXT),
    VKM_ENTRY(CmdDraw),
    VKM_ENTRY(CmdDrawIndexed),
    VKM_ENTRY(CmdDrawIndirect),
    VKM_ENTRY(CmdDrawIndexedIndirect),
    VKM_ENTRY(CmdDispatch),
    VKM_ENTRY(CmdDispatchBase),
    VKM_ENTRY(CmdDispatchIndirect),
    VKM_ENTRY(CmdBindTransformFeedbackBuffersEXT),
    VKM_ENTRY(CmdBeginTransformFeedbackEXT),
    VKM_ENTRY(CmdEndTransformFeedbackEXT),
    VKM_ENTRY(CmdDrawIndirectByteCountEXT),
    VKM_ENTRY(CreateIndirectCommandsLayoutEXT),
    VKM_ENTRY(DestroyIndirectCommandsLayoutEXT),
    VKM_ENTRY(GetGeneratedCommandsMemoryRequirementsEXT),
    VKM_ENTRY(CreateIndirectExecutionSetEXT),
    VKM_ENTRY(DestroyIndirectExecutionSetEXT),
    VKM_ENTRY(UpdateIndirectExecutionSetPipelineEXT),
    VKM_ENTRY(UpdateIndirectExecutionSetShaderEXT),
    VKM_ENTRY(CmdPreprocessGeneratedCommandsEXT),
    VKM_ENTRY(CmdExecuteGeneratedCommandsEXT),
    {nullptr, nullptr},
};
