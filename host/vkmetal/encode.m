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
    id<MTL4CommandBuffer> cmd;
    id<MTL4RenderCommandEncoder> render;
    id<MTL4ComputeCommandEncoder> compute;
    id<MTLComputePipelineState> pipeline; /* the compute encoder's. */
    uint32_t width, height;
    VkmOpRaster raster; /* what the render encoder has, zeroed as metal's defaults are. */
    MTLSize local;
    uint64_t value; /* the queue's value once this submission is done. */
} Enc;

/* the work of later encoders, a render pass's load actions included, waits for this encoder's. */
static void before_queue(id<MTL4CommandEncoder> encoder)
{
    [encoder barrierAfterStages:MTLStageAll beforeQueueStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
}

static void end_encoder(Enc *e)
{
    if (e->render) {
        before_queue(e->render);
        [e->render endEncoding];
        e->render = nullptr;
    }
    if (e->compute) {
        before_queue(e->compute);
        [e->compute endEncoding];
        e->compute = nullptr;
    }
}

static void after_queue(id<MTL4CommandEncoder> encoder)
{
    [encoder barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
}

static id<MTL4ComputeCommandEncoder> compute(Enc *e)
{
    if (e->compute)
        return e->compute;
    end_encoder(e);
    e->compute = [e->cmd computeCommandEncoder];
    e->pipeline = nil;
    after_queue(e->compute);
    [e->compute setArgumentTable:e->q->tables[1]];
    return e->compute;
}

static void compute_pipeline(Enc *e, id<MTLComputePipelineState> pipeline)
{
    id<MTL4ComputeCommandEncoder> c = compute(e);
    if (pipeline != e->pipeline)
        [c setComputePipelineState:pipeline];
    e->pipeline = pipeline;
}

/* ------------------------------------------------------------------ render passes. */

static void attachment(MTLRenderPassAttachmentDescriptor *a, const VkmAttach *at)
{
    bool is3d = at->texture.textureType == MTLTextureType3D;
    a.texture = at->texture;
    a.level = at->level;
    if (is3d)
        a.depthPlane = at->slice;
    else
        a.slice = at->slice;
    a.loadAction = (MTLLoadAction)at->load;
    a.storeAction = (MTLStoreAction)at->store;
    if (at->resolve) {
        bool resolve3d = at->resolve.textureType == MTLTextureType3D;
        a.resolveTexture = at->resolve;
        a.resolveLevel = at->resolve_level;
        if (resolve3d)
            a.resolveDepthPlane = at->resolve_slice;
        else
            a.resolveSlice = at->resolve_slice;
    }
}

static void begin_render(Enc *e, const VkmBeginRender *b)
{
    end_encoder(e);
    e->raster = (VkmOpRaster){};
    MTL4RenderPassDescriptor *desc = [[MTL4RenderPassDescriptor alloc] init];
    MTLRenderPassColorAttachmentDescriptorArray *colors = desc.colorAttachments;
    for (uint32_t i = 0; i < b->color_count; i++) {
        if (!b->colors[i].texture)
            continue;
        MTLRenderPassColorAttachmentDescriptor *a = colors[i];
        attachment(a, &b->colors[i]);
        a.clearColor = b->colors[i].clear;
    }
    if (b->depth.texture) {
        MTLRenderPassDepthAttachmentDescriptor *a = desc.depthAttachment;
        attachment(a, &b->depth);
        a.clearDepth = b->depth.clear.red;
        if (b->depth.resolve)
            a.depthResolveFilter = (MTLMultisampleDepthResolveFilter)b->depth.resolve_filter;
    }
    if (b->stencil.texture) {
        MTLRenderPassStencilAttachmentDescriptor *a = desc.stencilAttachment;
        attachment(a, &b->stencil);
        a.clearStencil = (uint32_t)b->stencil.clear.red;
        if (b->stencil.resolve)
            a.stencilResolveFilter = b->stencil.resolve_filter == MTLMultisampleDepthResolveFilterSample0
                                         ? MTLMultisampleStencilResolveFilterSample0
                                         : MTLMultisampleStencilResolveFilterDepthResolvedSample;
    }
    desc.renderTargetWidth = b->width;
    desc.renderTargetHeight = b->height;
    if (b->layers > 1)
        desc.renderTargetArrayLength = b->layers;
    desc.defaultRasterSampleCount = MIME_MAX(b->samples, 1u);
    if (b->visibility) {
        desc.visibilityResultBuffer = b->visibility;
        desc.visibilityResultType = MTLVisibilityResultTypeAccumulate;
    }
    /* the encoder is autoreleased into the submission's pool. */
    e->render = [e->cmd renderCommandEncoderWithDescriptor:desc];
    [desc release];
    if (!e->render) {
        VKM_ERROR("no render encoder for a %ux%u pass", b->width, b->height);
        return;
    }
    e->width = b->width;
    e->height = b->height;
    after_queue(e->render);
    [e->render setArgumentTable:e->q->tables[0] atStages:MTLRenderStageVertex | MTLRenderStageFragment];
}

static MTLScissorRect scissor(const Enc *e, VkRect2D r)
{
    uint32_t x = MIME_MIN((uint32_t)MIME_MAX(r.offset.x, 0), e->width);
    uint32_t y = MIME_MIN((uint32_t)MIME_MAX(r.offset.y, 0), e->height);
    return (MTLScissorRect){x, y, MIME_MIN(r.extent.width, e->width - x), MIME_MIN(r.extent.height, e->height - y)};
}

static void set_address(id<MTL4ArgumentTable> table, uint32_t index, uint64_t address)
{
    [table setAddress:address atIndex:index];
}

static void plain_raster(id<MTL4RenderCommandEncoder> r)
{
    [r setCullMode:MTLCullModeNone];
    [r setTriangleFillMode:MTLTriangleFillModeFill];
    [r setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
}

static void clear_rects(Enc *e, const VkmOpClearRects *o)
{
    id<MTL4RenderCommandEncoder> r = e->render;
    [r setRenderPipelineState:o->pipeline];
    [r setDepthStencilState:o->depth_state];
    [r setStencilReferenceValue:o->stencil];
    plain_raster(r);
    set_address(e->q->tables[0], 0, o->values);
    for (uint32_t i = 0; i < o->rect_count; i++) {
        MTLScissorRect s = scissor(e, o->rects[i]);
        if (!s.width || !s.height || !o->layer_count[i])
            continue;
        MTLViewport v = {(double)s.x, (double)s.y, (double)s.width, (double)s.height, 0.0, 1.0};
        [r setViewport:v];
        [r setScissorRect:s];
        [r drawPrimitives:MTLPrimitiveTypeTriangle
              vertexStart:0
              vertexCount:3
            instanceCount:o->layer_count[i]
             baseInstance:o->base_layer[i]];
    }
}

static void blit(Enc *e, const VkmOpBlit *o)
{
    id<MTL4RenderCommandEncoder> r = e->render;
    id<MTL4ArgumentTable> table = e->q->tables[0];
    [r setRenderPipelineState:o->pipeline];
    [r setDepthStencilState:o->depth_state];
    plain_raster(r);
    MTLScissorRect s = scissor(e, o->rect);
    if (!s.width || !s.height)
        return;
    MTLViewport v = {(double)o->rect.offset.x, (double)o->rect.offset.y, (double)o->rect.extent.width,
                     (double)o->rect.extent.height, 0.0, 1.0};
    [r setViewport:v];
    [r setScissorRect:s];
    set_address(table, 0, o->params);
    [table setTexture:o->src.gpuResourceID atIndex:0];
    [table setSamplerState:o->sampler.gpuResourceID atIndex:0];
    [r drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
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
    if (pool->type == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT)
        return 2;
    return pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS ? MIME_MAX(__builtin_popcount(pool->statistics), 1) : 1;
}

static void query_reset(Enc *e, const VkmOpQuery *o)
{
    VkQueryPool pool = o->pool;
    uint32_t n = query_values(pool);
    for (uint32_t i = 0; i < o->count && o->first + i < pool->count; i++)
        pool->written[o->first + i] = 0;
    /* statistics the host adds as it encodes start from zero then too. */
    if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
        uint32_t count = MIME_MIN(o->count, pool->count > o->first ? pool->count - o->first : 0);
        memset(&pool->values[(uint64_t)o->first * n], 0, (size_t)count * 8 * n);
        return;
    }
    [compute(e) fillBuffer:pool->buffer
                     range:(NSRange){(NSUInteger)o->first * 8 * n, (NSUInteger)o->count * 8 * n}
                     value:0];
}

static void dispatch_barrier(id<MTL4CommandEncoder> encoder)
{
    [encoder barrierAfterEncoderStages:MTLStageDispatch | MTLStageBlit | MTLStageAccelerationStructure
                   beforeEncoderStages:MTLStageDispatch | MTLStageBlit | MTLStageAccelerationStructure
                     visibilityOptions:MTL4VisibilityOptionDevice];
}

/* results go out as Vulkan lays them: each query's values, then its availability. */
static void query_copy(Enc *e, const VkmOpQuery *o)
{
    VkQueryPool pool = o->pool;
    id<MTL4ComputeCommandEncoder> c = compute(e);
    /* after what wrote the queries before it, acceleration structure sizes included. */
    dispatch_barrier(c);
    id<MTLBuffer> dst = o->buffer->memory->buffer;
    uint32_t n = query_values(pool);
    uint32_t size = o->flags & VK_QUERY_RESULT_64_BIT ? 8 : 4;
    for (uint32_t i = 0; i < o->count; i++) {
        uint64_t at = o->buffer->offset + o->offset + (uint64_t)i * o->stride;
        for (uint32_t k = 0; k < n; k++)
            [c copyFromBuffer:pool->buffer
                     sourceOffset:((NSUInteger)(o->first + i) * n + k) * 8
                         toBuffer:dst
                destinationOffset:at + k * size
                             size:size];
        if (o->flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
            uint64_t avail = at + n * size;
            [c fillBuffer:dst range:(NSRange){avail, 1} value:1];
            [c fillBuffer:dst range:(NSRange){avail + 1, size - 1} value:0];
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

/* ------------------------------------------------------------------ generated commands. */


/* the generation kernel sees what the encoder wrote before, and the commands it encodes see what it wrote. */
static void generate(Enc *e, const VkmOpGenerate *o)
{
    id<MTL4ComputeCommandEncoder> c = compute(e);
    dispatch_barrier(c);
    compute_pipeline(e, o->pipeline);
    set_address(e->q->tables[1], 0, o->params);
    set_address(e->q->tables[1], 1, o->icb);
    [c dispatchThreads:(MTLSize){o->threads, 1, 1} threadsPerThreadgroup:(MTLSize){64, 1, 1}];
    dispatch_barrier(c);
}

/* both encoders run indirect command buffers, but the method is not on the protocol they share. */
static void execute(Enc *e, const VkmOpExecute *o)
{
    if (o->dispatch) {
        id<MTL4ComputeCommandEncoder> c = compute(e);
        for (uint32_t i = 0; c && i < o->chunks; i++)
            [c executeCommandsInBuffer:o->icb indirectBuffer:o->ranges + (uint64_t)i * 8];
        return;
    }
    id<MTL4RenderCommandEncoder> r = e->render;
    for (uint32_t i = 0; r && i < o->chunks; i++)
        [r executeCommandsInBuffer:o->icb indirectBuffer:o->ranges + (uint64_t)i * 8];
}

/* ------------------------------------------------------------------ the replay. */

static void draw(Enc *e, uint32_t op, const VkmOpDraw *o)
{
    id<MTL4RenderCommandEncoder> r = e->render;
    MTLPrimitiveType primitive = (MTLPrimitiveType)o->primitive;
    MTLIndexType index_type = (MTLIndexType)o->index_type;
    switch (op) {
    case VKM_OP_DRAW:
        [r drawPrimitives:primitive
              vertexStart:o->first
              vertexCount:o->count
            instanceCount:o->instances
             baseInstance:o->base_instance];
        break;
    case VKM_OP_DRAW_INDEXED:
        [r drawIndexedPrimitives:primitive
                      indexCount:o->count
                       indexType:index_type
                     indexBuffer:o->index
               indexBufferLength:o->index_length
                   instanceCount:o->instances
                      baseVertex:o->base_vertex
                    baseInstance:o->base_instance];
        break;
    case VKM_OP_DRAW_INDIRECT:
        for (uint32_t i = 0; i < o->draws; i++)
            [r drawPrimitives:primitive indirectBuffer:o->indirect + (uint64_t)i * o->stride];
        break;
    case VKM_OP_DRAW_INDEXED_INDIRECT:
        for (uint32_t i = 0; i < o->draws; i++)
            [r drawIndexedPrimitives:primitive
                           indexType:index_type
                         indexBuffer:o->index
                   indexBufferLength:o->index_length
                      indirectBuffer:o->indirect + (uint64_t)i * o->stride];
        break;
    }
}

static void replay(Enc *e, const VkmOp *op)
{
    id<MTL4RenderCommandEncoder> r = e->render;
    /* render state outside a pass is from a pass whose encoder could not be made. */
    if (op->op > VKM_OP_END_RENDER && op->op <= VKM_OP_VISIBILITY && !r)
        return;
    switch (op->op) {
    case VKM_OP_BEGIN_RENDER: begin_render(e, (const VkmBeginRender *)op); break;
    case VKM_OP_END_RENDER: end_encoder(e); break;
    case VKM_OP_PIPELINE:
        [r setRenderPipelineState:(id<MTLRenderPipelineState>)((const VkmOpObject *)op)->object];
        break;
    case VKM_OP_DEPTH_STATE:
        [r setDepthStencilState:(id<MTLDepthStencilState>)((const VkmOpObject *)op)->object];
        break;
    case VKM_OP_RASTER: {
        /* validation calls the first depth clip mode of an encoder unused, even before a draw: only changes are set. */
        const VkmOpRaster *o = (const void *)op;
        VkmOpRaster *had = &e->raster;
        if (o->cull != had->cull)
            [r setCullMode:(MTLCullMode)o->cull];
        if (o->winding != had->winding)
            [r setFrontFacingWinding:(MTLWinding)o->winding];
        if (o->fill != had->fill)
            [r setTriangleFillMode:(MTLTriangleFillMode)o->fill];
        if (o->clip != had->clip)
            [r setDepthClipMode:(MTLDepthClipMode)o->clip];
        if (o->bias_constant != had->bias_constant || o->bias_slope != had->bias_slope ||
            o->bias_clamp != had->bias_clamp)
            [r setDepthBias:o->bias_constant slopeScale:o->bias_slope clamp:o->bias_clamp];
        *had = *o;
        break;
    }
    case VKM_OP_VIEWPORTS: {
        const VkmOpViewports *o = (const void *)op;
        [r setViewports:o->viewports count:o->count];
        break;
    }
    case VKM_OP_SCISSORS: {
        const VkmOpScissors *o = (const void *)op;
        MTLScissorRect rects[VKM_VIEWPORTS];
        for (uint32_t i = 0; i < o->count; i++)
            rects[i] = scissor(e, o->rects[i]);
        [r setScissorRects:rects count:o->count];
        break;
    }
    case VKM_OP_STENCIL_REF: {
        const VkmOpValues *o = (const void *)op;
        [r setStencilFrontReferenceValue:o->u[0] backReferenceValue:o->u[1]];
        break;
    }
    case VKM_OP_BLEND_CONSTANTS: {
        const VkmOpValues *o = (const void *)op;
        [r setBlendColorRed:o->f[0] green:o->f[1] blue:o->f[2] alpha:o->f[3]];
        break;
    }
    case VKM_OP_BUFFER: {
        const VkmOpBuffer *o = (const void *)op;
        if (o->stride == ~0u)
            set_address(e->q->tables[0], o->index, o->address);
        else
            [e->q->tables[0] setAddress:o->address attributeStride:o->stride atIndex:o->index];
        break;
    }
    case VKM_OP_DRAW:
    case VKM_OP_DRAW_INDEXED:
    case VKM_OP_DRAW_INDIRECT:
    case VKM_OP_DRAW_INDEXED_INDIRECT: draw(e, op->op, (const VkmOpDraw *)op); break;
    case VKM_OP_CLEAR_RECTS: clear_rects(e, (const VkmOpClearRects *)op); break;
    case VKM_OP_VISIBILITY: {
        const VkmOpVisibility *o = (const void *)op;
        [r setVisibilityResultMode:(MTLVisibilityResultMode)o->mode offset:o->offset];
        break;
    }
    case VKM_OP_COMPUTE_PIPELINE: {
        const VkmOpCompute *o = (const void *)op;
        compute_pipeline(e, o->pipeline);
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
        [compute(e) dispatchThreadgroups:(MTLSize){o->x, o->y, o->z} threadsPerThreadgroup:e->local];
        break;
    }
    case VKM_OP_DISPATCH_INDIRECT: {
        const VkmOpDispatch *o = (const void *)op;
        [compute(e) dispatchThreadgroupsWithIndirectBuffer:o->indirect threadsPerThreadgroup:e->local];
        break;
    }
    case VKM_OP_COPY_BUFFER: {
        const VkmOpCopyBuffer *o = (const void *)op;
        [compute(e) copyFromBuffer:o->src
                      sourceOffset:o->src_offset
                          toBuffer:o->dst
                 destinationOffset:o->dst_offset
                              size:o->size];
        break;
    }
    case VKM_OP_COPY_BUFFER_IMAGE: {
        const VkmOpCopyBufferImage *o = (const void *)op;
        if (o->to_image)
            [compute(e) copyFromBuffer:o->buffer
                          sourceOffset:o->offset
                     sourceBytesPerRow:o->row
                   sourceBytesPerImage:o->image
                            sourceSize:o->size
                             toTexture:o->texture
                      destinationSlice:o->slice
                      destinationLevel:o->level
                     destinationOrigin:o->origin
                               options:(MTLBlitOption)o->options];
        else
            [compute(e) copyFromTexture:o->texture
                             sourceSlice:o->slice
                             sourceLevel:o->level
                            sourceOrigin:o->origin
                              sourceSize:o->size
                                toBuffer:o->buffer
                       destinationOffset:o->offset
                  destinationBytesPerRow:o->row
                destinationBytesPerImage:o->image
                                 options:(MTLBlitOption)o->options];
        break;
    }
    case VKM_OP_COPY_IMAGE: {
        const VkmOpCopyImage *o = (const void *)op;
        [compute(e) copyFromTexture:o->src
                        sourceSlice:o->src_slice
                        sourceLevel:o->src_level
                       sourceOrigin:o->src_origin
                         sourceSize:o->size
                          toTexture:o->dst
                   destinationSlice:o->dst_slice
                   destinationLevel:o->dst_level
                  destinationOrigin:o->dst_origin];
        break;
    }
    case VKM_OP_FILL_BUFFER: {
        const VkmOpFill *o = (const void *)op;
        [compute(e) fillBuffer:o->buffer range:(NSRange){o->offset, o->size} value:(uint8_t)o->value];
        break;
    }
    case VKM_OP_BLIT:
        if (r)
            blit(e, (const VkmOpBlit *)op);
        break;
    case VKM_OP_BARRIER:
        if (e->compute)
            dispatch_barrier(e->compute);
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
    case VKM_OP_GENERATE: generate(e, (const VkmOpGenerate *)op); break;
    case VKM_OP_STATS: {
        const VkmOpStats *o = (const void *)op;
        VkQueryPool pool = o->pool;
        uint32_t n = query_values(pool), k = 0;
        if (o->query >= pool->count)
            break;
        for (uint32_t b = 0; b < VKM_STATS; b++)
            if (pool->statistics & (1u << b))
                pool->values[(uint64_t)o->query * n + k++] += o->add[b];
        break;
    }
    case VKM_OP_VERTEX_BARRIER:
        if (r)
            [r barrierAfterEncoderStages:MTLStageVertex
                     beforeEncoderStages:MTLStageVertex
                       visibilityOptions:MTL4VisibilityOptionDevice];
        break;
    case VKM_OP_EXECUTE: execute(e, (const VkmOpExecute *)op); break;
    case VKM_OP_AS_BUILD: vkm_encode_as_build(e->q->device, compute(e), (const VkmOpAsBuild *)op); break;
    case VKM_OP_AS_COPY: vkm_encode_as_copy(compute(e), (const VkmOpAsCopy *)op); break;
    case VKM_OP_AS_PROPERTY: vkm_encode_as_property(compute(e), (const VkmOpAsProperty *)op, e->q, e->value); break;
    default: break;
    }
}

void vkm_encode(VkQueue queue, VkCommandBuffer cmd, id<MTL4CommandBuffer> mtl_cmd)
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
