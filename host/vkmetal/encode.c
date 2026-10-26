/*
 * a submission's command buffers replayed into a Metal 4 command buffer. render passes are render encoders;
 * copies, fills and dispatches share a compute encoder until the next pass. every encoder begins after what
 * the queue ran before it, which is what Vulkan's barriers between passes ask for; barriers between copies
 * and dispatches are barriers inside the compute encoder.
 */
#include "vkm_cmd.h"

#include <time.h>

typedef struct Enc {
    VkQueue q;
    id cmd, render, compute;
    uint32_t width, height;
    MTLSize local;
    uint64_t value; /* the queue's value once this submission is done. */
} Enc;

/* the work of later encoders, a render pass's load actions included, waits for this encoder's. */
static void before_queue(id encoder)
{
    MTL_MSG(void (*)(id, SEL, uint64_t, uint64_t, NSUInteger), encoder,
            "barrierAfterStages:beforeQueueStages:visibilityOptions:", (uint64_t)MTLStageAll, (uint64_t)MTLStageAll,
            MTL4VisibilityOptionDevice);
}

static void end_encoder(Enc *e)
{
    if (e->render) {
        before_queue(e->render);
        M_V(e->render, "endEncoding");
        e->render = nullptr;
    }
    if (e->compute) {
        before_queue(e->compute);
        M_V(e->compute, "endEncoding");
        e->compute = nullptr;
    }
}

static void after_queue(id encoder)
{
    MTL_MSG(void (*)(id, SEL, uint64_t, uint64_t, NSUInteger), encoder,
            "barrierAfterQueueStages:beforeStages:visibilityOptions:", (uint64_t)MTLStageAll, (uint64_t)MTLStageAll,
            MTL4VisibilityOptionDevice);
}

static id compute(Enc *e)
{
    if (e->compute)
        return e->compute;
    end_encoder(e);
    e->compute = M_ID(e->cmd, "computeCommandEncoder");
    after_queue(e->compute);
    M_V_ID(e->compute, "setArgumentTable:", e->q->tables[1]);
    return e->compute;
}

/* ------------------------------------------------------------------ render passes. */

static void attachment(id a, const VkmAttach *at)
{
    bool is3d = M_U(at->texture, "textureType") == MTLTextureType3D;
    M_V_ID(a, "setTexture:", at->texture);
    M_V_U(a, "setLevel:", at->level);
    M_V_U(a, is3d ? "setDepthPlane:" : "setSlice:", at->slice);
    M_V_U(a, "setLoadAction:", at->load);
    M_V_U(a, "setStoreAction:", at->store);
    if (at->resolve) {
        bool resolve3d = M_U(at->resolve, "textureType") == MTLTextureType3D;
        M_V_ID(a, "setResolveTexture:", at->resolve);
        M_V_U(a, "setResolveLevel:", at->resolve_level);
        M_V_U(a, resolve3d ? "setResolveDepthPlane:" : "setResolveSlice:", at->resolve_slice);
    }
}

static void begin_render(Enc *e, const VkmBeginRender *b)
{
    end_encoder(e);
    id desc = mtl_new("MTL4RenderPassDescriptor");
    id colors = M_ID(desc, "colorAttachments");
    for (uint32_t i = 0; i < b->color_count; i++) {
        if (!b->colors[i].texture)
            continue;
        id a = M_AT(colors, i);
        attachment(a, &b->colors[i]);
        MTL_MSG(void (*)(id, SEL, MTLClearColor), a, "setClearColor:", b->colors[i].clear);
    }
    if (b->depth.texture) {
        id a = M_ID(desc, "depthAttachment");
        attachment(a, &b->depth);
        M_V_D(a, "setClearDepth:", b->depth.clear.r);
        if (b->depth.resolve)
            M_V_U(a, "setDepthResolveFilter:", b->depth.resolve_filter);
    }
    if (b->stencil.texture) {
        id a = M_ID(desc, "stencilAttachment");
        attachment(a, &b->stencil);
        MTL_MSG(void (*)(id, SEL, uint32_t), a, "setClearStencil:", (uint32_t)b->stencil.clear.r);
        if (b->stencil.resolve)
            M_V_U(a, "setStencilResolveFilter:", b->stencil.resolve_filter == MTLMultisampleDepthResolveFilterSample0
                                                     ? MTLMultisampleStencilResolveFilterSample0
                                                     : MTLMultisampleStencilResolveFilterDepthResolvedSample);
    }
    M_V_U(desc, "setRenderTargetWidth:", b->width);
    M_V_U(desc, "setRenderTargetHeight:", b->height);
    if (b->layers > 1)
        M_V_U(desc, "setRenderTargetArrayLength:", b->layers);
    M_V_U(desc, "setDefaultRasterSampleCount:", MIME_MAX(b->samples, 1u));
    if (b->visibility) {
        M_V_ID(desc, "setVisibilityResultBuffer:", b->visibility);
        M_V_I(desc, "setVisibilityResultType:", MTLVisibilityResultTypeAccumulate);
    }
    /* the encoder is autoreleased into the submission's pool. */
    e->render = M_ID_ID(e->cmd, "renderCommandEncoderWithDescriptor:", desc);
    mtl_release(desc);
    if (!e->render) {
        VKM_ERROR("no render encoder for a %ux%u pass", b->width, b->height);
        return;
    }
    e->width = b->width;
    e->height = b->height;
    after_queue(e->render);
    MTL_MSG(void (*)(id, SEL, id, NSUInteger), e->render, "setArgumentTable:atStages:", e->q->tables[0],
            MTLRenderStageVertex | MTLRenderStageFragment);
}

static MTLScissorRect scissor(const Enc *e, VkRect2D r)
{
    uint32_t x = MIME_MIN((uint32_t)MIME_MAX(r.offset.x, 0), e->width);
    uint32_t y = MIME_MIN((uint32_t)MIME_MAX(r.offset.y, 0), e->height);
    return (MTLScissorRect){x, y, MIME_MIN(r.extent.width, e->width - x), MIME_MIN(r.extent.height, e->height - y)};
}

static void set_address(id table, uint32_t index, uint64_t address)
{
    MTL_MSG(void (*)(id, SEL, uint64_t, NSUInteger), table, "setAddress:atIndex:", address, index);
}

static void plain_raster(id r)
{
    M_V_U(r, "setCullMode:", MTLCullModeNone);
    M_V_U(r, "setTriangleFillMode:", MTLTriangleFillModeFill);
    MTL_MSG(void (*)(id, SEL, float, float, float), r, "setDepthBias:slopeScale:clamp:", 0.0f, 0.0f, 0.0f);
}

static void clear_rects(Enc *e, const VkmOpClearRects *o)
{
    id r = e->render;
    M_V_ID(r, "setRenderPipelineState:", o->pipeline);
    M_V_ID(r, "setDepthStencilState:", o->depth_state);
    MTL_MSG(void (*)(id, SEL, uint32_t), r, "setStencilReferenceValue:", o->stencil);
    plain_raster(r);
    set_address(e->q->tables[0], 0, o->values);
    for (uint32_t i = 0; i < o->rect_count; i++) {
        MTLScissorRect s = scissor(e, o->rects[i]);
        if (!s.width || !s.height || !o->layer_count[i])
            continue;
        MTLViewport v = {(double)s.x, (double)s.y, (double)s.width, (double)s.height, 0.0, 1.0};
        MTL_MSG(void (*)(id, SEL, MTLViewport), r, "setViewport:", v);
        MTL_MSG(void (*)(id, SEL, MTLScissorRect), r, "setScissorRect:", s);
        MTL_MSG(void (*)(id, SEL, NSUInteger, NSUInteger, NSUInteger, NSUInteger, NSUInteger), r,
                "drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:", MTLPrimitiveTypeTriangle, 0, 3,
                o->layer_count[i], o->base_layer[i]);
    }
}

static void blit(Enc *e, const VkmOpBlit *o)
{
    id r = e->render, table = e->q->tables[0];
    M_V_ID(r, "setRenderPipelineState:", o->pipeline);
    M_V_ID(r, "setDepthStencilState:", o->depth_state);
    plain_raster(r);
    MTLScissorRect s = scissor(e, o->rect);
    if (!s.width || !s.height)
        return;
    MTLViewport v = {(double)o->rect.offset.x, (double)o->rect.offset.y, (double)o->rect.extent.width,
                     (double)o->rect.extent.height, 0.0, 1.0};
    MTL_MSG(void (*)(id, SEL, MTLViewport), r, "setViewport:", v);
    MTL_MSG(void (*)(id, SEL, MTLScissorRect), r, "setScissorRect:", s);
    set_address(table, 0, o->params);
    MTL_MSG(void (*)(id, SEL, MTLResourceID, NSUInteger), table, "setTexture:atIndex:", M_RID(o->src, "gpuResourceID"),
            0);
    MTL_MSG(void (*)(id, SEL, MTLResourceID, NSUInteger), table, "setSamplerState:atIndex:",
            M_RID(o->sampler, "gpuResourceID"), 0);
    MTL_MSG(void (*)(id, SEL, NSUInteger, NSUInteger, NSUInteger), r, "drawPrimitives:vertexStart:vertexCount:",
            MTLPrimitiveTypeTriangleStrip, 0, 4);
}

/* ------------------------------------------------------------------ queries. */

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint32_t query_values(VkQueryPool pool)
{
    return pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS ? MIME_MAX(__builtin_popcount(pool->statistics), 1) : 1;
}

static void query_reset(Enc *e, const VkmOpQuery *o)
{
    VkQueryPool pool = o->pool;
    uint32_t n = query_values(pool);
    for (uint32_t i = 0; i < o->count && o->first + i < pool->count; i++)
        pool->written[o->first + i] = 0;
    MTL_MSG(void (*)(id, SEL, id, NSRange, uint8_t), compute(e), "fillBuffer:range:value:", pool->buffer,
            (NSRange){(NSUInteger)o->first * 8 * n, (NSUInteger)o->count * 8 * n}, 0);
}

/* results go out as Vulkan lays them: each query's values, then its availability. */
static void query_copy(Enc *e, const VkmOpQuery *o)
{
    VkQueryPool pool = o->pool;
    id c = compute(e);
    id dst = o->buffer->memory->buffer;
    uint32_t n = query_values(pool);
    uint32_t size = o->flags & VK_QUERY_RESULT_64_BIT ? 8 : 4;
    for (uint32_t i = 0; i < o->count; i++) {
        uint64_t at = o->buffer->offset + o->offset + (uint64_t)i * o->stride;
        for (uint32_t k = 0; k < n; k++)
            MTL_MSG(void (*)(id, SEL, id, NSUInteger, id, NSUInteger, NSUInteger), c,
                    "copyFromBuffer:sourceOffset:toBuffer:destinationOffset:size:", pool->buffer,
                    ((NSUInteger)(o->first + i) * n + k) * 8, dst, at + k * size, size);
        if (o->flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
            uint64_t avail = at + n * size;
            MTL_MSG(void (*)(id, SEL, id, NSRange, uint8_t), c, "fillBuffer:range:value:", dst, (NSRange){avail, 1}, 1);
            MTL_MSG(void (*)(id, SEL, id, NSRange, uint8_t), c, "fillBuffer:range:value:", dst,
                    (NSRange){avail + 1, size - 1}, 0);
        }
    }
}

static void timestamp(Enc *e, const VkmOpQuery *o)
{
    VkQueryPool pool = o->pool;
    /* the host's clock at submission: Metal 4 timestamps are in counter heaps no copy can reach. */
    if (!o->flags && pool->type == VK_QUERY_TYPE_TIMESTAMP && o->first < pool->count)
        pool->values[o->first] = now_ns();
    vkm_query_written(pool, o->first, o->count, e->q, e->value);
}

/* ------------------------------------------------------------------ the replay. */

static void draw(Enc *e, uint32_t op, const VkmOpDraw *o)
{
    id r = e->render;
    switch (op) {
    case VKM_OP_DRAW:
        MTL_MSG(void (*)(id, SEL, NSUInteger, NSUInteger, NSUInteger, NSUInteger, NSUInteger), r,
                "drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:", o->primitive, o->first, o->count,
                o->instances, o->base_instance);
        break;
    case VKM_OP_DRAW_INDEXED:
        MTL_MSG(void (*)(id, SEL, NSUInteger, NSUInteger, NSUInteger, uint64_t, NSUInteger, NSUInteger, NSInteger,
                         NSUInteger),
                r,
                "drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferLength:instanceCount:baseVertex:"
                "baseInstance:",
                o->primitive, o->count, o->index_type, o->index, o->index_length, o->instances, o->base_vertex,
                o->base_instance);
        break;
    case VKM_OP_DRAW_INDIRECT:
        for (uint32_t i = 0; i < o->draws; i++)
            MTL_MSG(void (*)(id, SEL, NSUInteger, uint64_t), r, "drawPrimitives:indirectBuffer:", o->primitive,
                    o->indirect + (uint64_t)i * o->stride);
        break;
    case VKM_OP_DRAW_INDEXED_INDIRECT:
        for (uint32_t i = 0; i < o->draws; i++)
            MTL_MSG(void (*)(id, SEL, NSUInteger, NSUInteger, uint64_t, NSUInteger, uint64_t), r,
                    "drawIndexedPrimitives:indexType:indexBuffer:indexBufferLength:indirectBuffer:", o->primitive,
                    o->index_type, o->index, o->index_length, o->indirect + (uint64_t)i * o->stride);
        break;
    }
}

static void replay(Enc *e, const VkmOp *op)
{
    id r = e->render;
    /* render state outside a pass is from a pass whose encoder could not be made. */
    if (op->op > VKM_OP_END_RENDER && op->op <= VKM_OP_VISIBILITY && !r)
        return;
    switch (op->op) {
    case VKM_OP_BEGIN_RENDER: begin_render(e, (const VkmBeginRender *)op); break;
    case VKM_OP_END_RENDER: end_encoder(e); break;
    case VKM_OP_PIPELINE: M_V_ID(r, "setRenderPipelineState:", ((const VkmOpObject *)op)->object); break;
    case VKM_OP_DEPTH_STATE: M_V_ID(r, "setDepthStencilState:", ((const VkmOpObject *)op)->object); break;
    case VKM_OP_RASTER: {
        const VkmOpRaster *o = (const void *)op;
        M_V_U(r, "setCullMode:", o->cull);
        M_V_U(r, "setFrontFacingWinding:", o->winding);
        M_V_U(r, "setTriangleFillMode:", o->fill);
        M_V_U(r, "setDepthClipMode:", o->clip);
        MTL_MSG(void (*)(id, SEL, float, float, float), r, "setDepthBias:slopeScale:clamp:", o->bias_constant,
                o->bias_slope, o->bias_clamp);
        break;
    }
    case VKM_OP_VIEWPORTS: {
        const VkmOpViewports *o = (const void *)op;
        MTL_MSG(void (*)(id, SEL, const MTLViewport *, NSUInteger), r, "setViewports:count:", o->viewports, o->count);
        break;
    }
    case VKM_OP_SCISSORS: {
        const VkmOpScissors *o = (const void *)op;
        MTLScissorRect rects[VKM_VIEWPORTS];
        for (uint32_t i = 0; i < o->count; i++)
            rects[i] = scissor(e, o->rects[i]);
        MTL_MSG(void (*)(id, SEL, const MTLScissorRect *, NSUInteger), r, "setScissorRects:count:", rects, o->count);
        break;
    }
    case VKM_OP_STENCIL_REF: {
        const VkmOpValues *o = (const void *)op;
        MTL_MSG(void (*)(id, SEL, uint32_t, uint32_t), r, "setStencilFrontReferenceValue:backReferenceValue:", o->u[0],
                o->u[1]);
        break;
    }
    case VKM_OP_BLEND_CONSTANTS: {
        const VkmOpValues *o = (const void *)op;
        MTL_MSG(void (*)(id, SEL, float, float, float, float), r, "setBlendColorRed:green:blue:alpha:", o->f[0],
                o->f[1], o->f[2], o->f[3]);
        break;
    }
    case VKM_OP_BUFFER: {
        const VkmOpBuffer *o = (const void *)op;
        if (o->stride == ~0u)
            set_address(e->q->tables[0], o->index, o->address);
        else
            MTL_MSG(void (*)(id, SEL, uint64_t, NSUInteger, NSUInteger), e->q->tables[0],
                    "setAddress:attributeStride:atIndex:", o->address, o->stride, o->index);
        break;
    }
    case VKM_OP_DRAW:
    case VKM_OP_DRAW_INDEXED:
    case VKM_OP_DRAW_INDIRECT:
    case VKM_OP_DRAW_INDEXED_INDIRECT: draw(e, op->op, (const VkmOpDraw *)op); break;
    case VKM_OP_CLEAR_RECTS: clear_rects(e, (const VkmOpClearRects *)op); break;
    case VKM_OP_VISIBILITY: {
        const VkmOpVisibility *o = (const void *)op;
        MTL_MSG(void (*)(id, SEL, NSUInteger, NSUInteger), r, "setVisibilityResultMode:offset:", o->mode, o->offset);
        break;
    }
    case VKM_OP_COMPUTE_PIPELINE: {
        const VkmOpCompute *o = (const void *)op;
        M_V_ID(compute(e), "setComputePipelineState:", o->pipeline);
        e->local = o->local;
        break;
    }
    case VKM_OP_COMPUTE_BUFFER: {
        const VkmOpBuffer *o = (const void *)op;
        set_address(e->q->tables[1], o->index, o->address);
        break;
    }
    case VKM_OP_DISPATCH: {
        const VkmOpDispatch *o = (const void *)op;
        MTL_MSG(void (*)(id, SEL, MTLSize, MTLSize), compute(e), "dispatchThreadgroups:threadsPerThreadgroup:",
                (MTLSize){o->x, o->y, o->z}, e->local);
        break;
    }
    case VKM_OP_DISPATCH_INDIRECT: {
        const VkmOpDispatch *o = (const void *)op;
        MTL_MSG(void (*)(id, SEL, uint64_t, MTLSize), compute(e), "dispatchThreadgroupsWithIndirectBuffer:threadsPerThreadgroup:",
                o->indirect, e->local);
        break;
    }
    case VKM_OP_COPY_BUFFER: {
        const VkmOpCopyBuffer *o = (const void *)op;
        MTL_MSG(void (*)(id, SEL, id, NSUInteger, id, NSUInteger, NSUInteger), compute(e),
                "copyFromBuffer:sourceOffset:toBuffer:destinationOffset:size:", o->src, o->src_offset, o->dst,
                o->dst_offset, o->size);
        break;
    }
    case VKM_OP_COPY_BUFFER_IMAGE: {
        const VkmOpCopyBufferImage *o = (const void *)op;
        if (o->to_image)
            MTL_MSG(void (*)(id, SEL, id, NSUInteger, NSUInteger, NSUInteger, MTLSize, id, NSUInteger, NSUInteger,
                             MTLOrigin, NSUInteger),
                    compute(e),
                    "copyFromBuffer:sourceOffset:sourceBytesPerRow:sourceBytesPerImage:sourceSize:toTexture:"
                    "destinationSlice:destinationLevel:destinationOrigin:options:",
                    o->buffer, o->offset, o->row, o->image, o->size, o->texture, o->slice, o->level, o->origin,
                    o->options);
        else
            MTL_MSG(void (*)(id, SEL, id, NSUInteger, NSUInteger, MTLOrigin, MTLSize, id, NSUInteger, NSUInteger,
                             NSUInteger, NSUInteger),
                    compute(e),
                    "copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toBuffer:destinationOffset:"
                    "destinationBytesPerRow:destinationBytesPerImage:options:",
                    o->texture, o->slice, o->level, o->origin, o->size, o->buffer, o->offset, o->row, o->image,
                    o->options);
        break;
    }
    case VKM_OP_COPY_IMAGE: {
        const VkmOpCopyImage *o = (const void *)op;
        MTL_MSG(void (*)(id, SEL, id, NSUInteger, NSUInteger, MTLOrigin, MTLSize, id, NSUInteger, NSUInteger, MTLOrigin),
                compute(e),
                "copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toTexture:destinationSlice:"
                "destinationLevel:destinationOrigin:",
                o->src, o->src_slice, o->src_level, o->src_origin, o->size, o->dst, o->dst_slice, o->dst_level,
                o->dst_origin);
        break;
    }
    case VKM_OP_FILL_BUFFER: {
        const VkmOpFill *o = (const void *)op;
        MTL_MSG(void (*)(id, SEL, id, NSRange, uint8_t), compute(e), "fillBuffer:range:value:", o->buffer,
                (NSRange){o->offset, o->size}, (uint8_t)o->value);
        break;
    }
    case VKM_OP_BLIT:
        if (r)
            blit(e, (const VkmOpBlit *)op);
        break;
    case VKM_OP_BARRIER:
        if (e->compute)
            MTL_MSG(void (*)(id, SEL, uint64_t, uint64_t, NSUInteger), e->compute,
                    "barrierAfterEncoderStages:beforeEncoderStages:visibilityOptions:",
                    (uint64_t)(MTLStageDispatch | MTLStageBlit), (uint64_t)(MTLStageDispatch | MTLStageBlit),
                    MTL4VisibilityOptionDevice);
        break;
    case VKM_OP_TIMESTAMP: timestamp(e, (const VkmOpQuery *)op); break;
    case VKM_OP_QUERY_RESET: query_reset(e, (const VkmOpQuery *)op); break;
    case VKM_OP_QUERY_COPY: query_copy(e, (const VkmOpQuery *)op); break;
    case VKM_OP_EVENT: {
        /* events are set when their submission is encoded; nothing in the guest waits on them from the GPU. */
        const VkmOpQuery *o = (const void *)op;
        __atomic_store_n(&((VkEvent)(void *)o->pool)->set, o->first, __ATOMIC_RELEASE);
        break;
    }
    default: break;
    }
}

void vkm_encode(VkQueue queue, VkCommandBuffer cmd, id mtl_cmd)
{
    Enc e = {queue, mtl_cmd};
    e.value = queue->submitted + 1;
    for (size_t at = 0; at < cmd->cmd_size;) {
        const VkmOp *op = (const VkmOp *)(cmd->cmds + at);
        if (!op->size)
            break;
        replay(&e, op);
        at += op->size;
    }
    end_encoder(&e);
}
