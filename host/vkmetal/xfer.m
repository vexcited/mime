/*
 * transfers, clears, blits, barriers, events and query commands. clears and blits that Metal has no command
 * for are draws in render passes of their own; a clear of a whole render pass before its first draw becomes
 * the pass's load action.
 */
#include "vkm_cmd.h"

#define STATE(cmd) ((cmd)->state)

uint64_t vkm_stages(VkPipelineStageFlags2 stages)
{
    if (stages & (VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT |
                  VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT))
        return MTLStageAll;
    uint64_t out = 0;
    if (stages & (VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
                  VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                  VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
                  VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT))
        out |= MTLStageVertex;
    if (stages & (VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT))
        out |= MTLStageFragment;
    if (stages & VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
        out |= MTLStageDispatch;
    /* transfers are copies, or draws for clears and blits. */
    if (stages & (VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT |
                  VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_HOST_BIT))
        out |= MTLStageBlit | MTLStageFragment;
    return out ? out : MTLStageAll;
}

static bool outside(VkCommandBuffer cmd, const char *what)
{
    if (!STATE(cmd)->rendering)
        return true;
    VKM_ERROR("%s inside a render pass", what);
    return false;
}

/* ------------------------------------------------------------------ buffers. */

static void copy_buffer(VkCommandBuffer cmd, id<MTLBuffer> src, uint64_t src_offset, id<MTLBuffer> dst,
                        uint64_t dst_offset, uint64_t size)
{
    VkmOpCopyBuffer *o = vkm_emit(cmd, VKM_OP_COPY_BUFFER, sizeof(*o));
    if (!o)
        return;
    o->src = src;
    o->dst = dst;
    o->src_offset = src_offset;
    o->dst_offset = dst_offset;
    o->size = size;
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyBuffer(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, uint32_t count,
                                                const VkBufferCopy *regions)
{
    if (!outside(cmd, "a buffer copy"))
        return;
    for (uint32_t i = 0; i < count; i++)
        if (regions[i].size)
            copy_buffer(cmd, src->memory->buffer, src->offset + regions[i].srcOffset, dst->memory->buffer,
                        dst->offset + regions[i].dstOffset, regions[i].size);
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyBuffer2(VkCommandBuffer cmd, const VkCopyBufferInfo2 *info)
{
    for (uint32_t i = 0; i < info->regionCount; i++) {
        const VkBufferCopy2 *r = &info->pRegions[i];
        VkBufferCopy one = {r->srcOffset, r->dstOffset, r->size};
        CmdCopyBuffer(cmd, info->srcBuffer, info->dstBuffer, 1, &one);
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdUpdateBuffer(VkCommandBuffer cmd, VkBuffer dst, VkDeviceSize offset,
                                                  VkDeviceSize size, const void *data)
{
    if (!size || !outside(cmd, "a buffer update"))
        return;
    id<MTLBuffer> buffer;
    uint64_t at;
    void *p = vkm_upload(cmd, size, 16, nullptr, &buffer, &at);
    if (!p)
        return;
    memcpy(p, data, size);
    copy_buffer(cmd, buffer, at, dst->memory->buffer, dst->offset + offset, size);
}

static VKAPI_ATTR void VKAPI_CALL CmdFillBuffer(VkCommandBuffer cmd, VkBuffer dst, VkDeviceSize offset,
                                                VkDeviceSize size, uint32_t value)
{
    if (!outside(cmd, "a buffer fill"))
        return;
    if (size == VK_WHOLE_SIZE)
        size = (dst->size - offset) & ~3ull;
    if (!size)
        return;
    uint8_t b = value & 0xff;
    if (value == b * 0x01010101u) {
        VkmOpFill *o = vkm_emit(cmd, VKM_OP_FILL_BUFFER, sizeof(*o));
        if (o) {
            o->buffer = dst->memory->buffer;
            o->offset = dst->offset + offset;
            o->size = size;
            o->value = b;
        }
        return;
    }
    /* other patterns are copies of a repeating block. */
    uint64_t block = MIME_MIN(size, (uint64_t)65536);
    id<MTLBuffer> buffer;
    uint64_t at;
    uint32_t *p = vkm_upload(cmd, block, 16, nullptr, &buffer, &at);
    if (!p)
        return;
    for (uint64_t i = 0; i < block / 4; i++)
        p[i] = value;
    for (uint64_t done = 0; done < size; done += block)
        copy_buffer(cmd, buffer, at, dst->memory->buffer, dst->offset + offset + done, MIME_MIN(block, size - done));
}

/* ------------------------------------------------------------------ buffers and images. */

static uint32_t aspect_options(VkImage image, VkImageAspectFlags aspect)
{
    if (!(image->fi->aspects & VK_IMAGE_ASPECT_STENCIL_BIT) || !(image->fi->aspects & VK_IMAGE_ASPECT_DEPTH_BIT))
        return MTLBlitOptionNone;
    return aspect == VK_IMAGE_ASPECT_STENCIL_BIT ? MTLBlitOptionStencilFromDepthStencil
                                                 : MTLBlitOptionDepthFromDepthStencil;
}

static uint32_t aspect_bytes(VkImage image, VkImageAspectFlags aspect)
{
    if (image->format == VK_FORMAT_D32_SFLOAT_S8_UINT)
        return aspect == VK_IMAGE_ASPECT_STENCIL_BIT ? 1 : 4;
    return image->fi->bytes;
}

static void buffer_image(VkCommandBuffer cmd, bool to_image, VkBuffer buffer, VkImage image,
                         const VkBufferImageCopy2 *r)
{
    const VkmFormat *f = image->fi;
    uint32_t row_texels = r->bufferRowLength ? r->bufferRowLength : r->imageExtent.width;
    uint32_t rows = r->bufferImageHeight ? r->bufferImageHeight : r->imageExtent.height;
    uint64_t row = (uint64_t)((row_texels + f->block_w - 1) / f->block_w) * aspect_bytes(image, r->imageSubresource.aspectMask);
    uint64_t slice = row * ((rows + f->block_h - 1) / f->block_h);
    bool is3d = image->type == VK_IMAGE_TYPE_3D;
    uint32_t layers = r->imageSubresource.layerCount == VK_REMAINING_ARRAY_LAYERS
                          ? image->layers - r->imageSubresource.baseArrayLayer
                          : r->imageSubresource.layerCount;
    VkExtent3D level = vkm_level_extent(image, r->imageSubresource.mipLevel);
    for (uint32_t l = 0; l < (is3d ? 1 : layers); l++) {
        VkmOpCopyBufferImage *o = vkm_emit(cmd, VKM_OP_COPY_BUFFER_IMAGE, sizeof(*o));
        if (!o)
            return;
        o->to_image = to_image;
        o->buffer = buffer->memory->buffer;
        o->texture = image->texture;
        o->offset = buffer->offset + r->bufferOffset + l * slice;
        o->row = row;
        o->image = is3d ? slice : 0;
        o->slice = is3d ? 0 : r->imageSubresource.baseArrayLayer + l;
        o->level = r->imageSubresource.mipLevel;
        o->options = aspect_options(image, r->imageSubresource.aspectMask);
        o->origin = (MTLOrigin){(NSUInteger)r->imageOffset.x, (NSUInteger)r->imageOffset.y, (NSUInteger)r->imageOffset.z};
        /* blocks at the image's edge may extend past it, but the copy may not. */
        o->size = (MTLSize){MIME_MIN(r->imageExtent.width, level.width - (uint32_t)r->imageOffset.x),
                            MIME_MIN(r->imageExtent.height, level.height - (uint32_t)r->imageOffset.y),
                            is3d ? r->imageExtent.depth : 1};
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyBufferToImage2(VkCommandBuffer cmd, const VkCopyBufferToImageInfo2 *info)
{
    if (!outside(cmd, "a buffer to image copy"))
        return;
    for (uint32_t i = 0; i < info->regionCount; i++)
        buffer_image(cmd, true, info->srcBuffer, info->dstImage, &info->pRegions[i]);
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyImageToBuffer2(VkCommandBuffer cmd, const VkCopyImageToBufferInfo2 *info)
{
    if (!outside(cmd, "an image to buffer copy"))
        return;
    for (uint32_t i = 0; i < info->regionCount; i++)
        buffer_image(cmd, false, info->dstBuffer, info->srcImage, &info->pRegions[i]);
}

static VkBufferImageCopy2 region2(const VkBufferImageCopy *r)
{
    return (VkBufferImageCopy2){VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2, nullptr, r->bufferOffset, r->bufferRowLength,
                                r->bufferImageHeight, r->imageSubresource, r->imageOffset, r->imageExtent};
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyBufferToImage(VkCommandBuffer cmd, VkBuffer src, VkImage dst,
                                                       VkImageLayout layout, uint32_t count,
                                                       const VkBufferImageCopy *regions)
{
    if (!outside(cmd, "a buffer to image copy"))
        return;
    for (uint32_t i = 0; i < count; i++) {
        VkBufferImageCopy2 r = region2(&regions[i]);
        buffer_image(cmd, true, src, dst, &r);
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyImageToBuffer(VkCommandBuffer cmd, VkImage src, VkImageLayout layout,
                                                       VkBuffer dst, uint32_t count, const VkBufferImageCopy *regions)
{
    if (!outside(cmd, "an image to buffer copy"))
        return;
    for (uint32_t i = 0; i < count; i++) {
        VkBufferImageCopy2 r = region2(&regions[i]);
        buffer_image(cmd, false, dst, src, &r);
    }
}

/* ------------------------------------------------------------------ images. */

static void copy_image(VkCommandBuffer cmd, VkImage src, VkImage dst, const VkImageCopy2 *r)
{
    bool src3d = src->type == VK_IMAGE_TYPE_3D, dst3d = dst->type == VK_IMAGE_TYPE_3D;
    uint32_t layers = r->srcSubresource.layerCount == VK_REMAINING_ARRAY_LAYERS
                          ? src->layers - r->srcSubresource.baseArrayLayer
                          : r->srcSubresource.layerCount;
    uint32_t depth = r->extent.depth;
    uint32_t slices = src3d && dst3d ? 1 : MIME_MAX(layers, src3d || dst3d ? depth : 1);
    if (src->fi->pixel != dst->fi->pixel && src->fi->bytes != dst->fi->bytes)
        VKM_ERROR("a copy between images of formats %u and %u", src->format, dst->format);
    VkExtent3D level = vkm_level_extent(src, r->srcSubresource.mipLevel);
    for (uint32_t l = 0; l < slices; l++) {
        VkmOpCopyImage *o = vkm_emit(cmd, VKM_OP_COPY_IMAGE, sizeof(*o));
        if (!o)
            return;
        o->src = src->texture;
        o->dst = dst->texture;
        o->src_level = r->srcSubresource.mipLevel;
        o->dst_level = r->dstSubresource.mipLevel;
        o->src_slice = src3d ? 0 : r->srcSubresource.baseArrayLayer + l;
        o->dst_slice = dst3d ? 0 : r->dstSubresource.baseArrayLayer + l;
        o->src_origin = (MTLOrigin){(NSUInteger)r->srcOffset.x, (NSUInteger)r->srcOffset.y,
                                    (NSUInteger)r->srcOffset.z + (src3d && !dst3d ? l : 0)};
        o->dst_origin = (MTLOrigin){(NSUInteger)r->dstOffset.x, (NSUInteger)r->dstOffset.y,
                                    (NSUInteger)r->dstOffset.z + (dst3d && !src3d ? l : 0)};
        o->size = (MTLSize){MIME_MIN(r->extent.width, level.width - (uint32_t)r->srcOffset.x),
                            MIME_MIN(r->extent.height, level.height - (uint32_t)r->srcOffset.y),
                            src3d && dst3d ? depth : 1};
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyImage2(VkCommandBuffer cmd, const VkCopyImageInfo2 *info)
{
    if (!outside(cmd, "an image copy"))
        return;
    for (uint32_t i = 0; i < info->regionCount; i++)
        copy_image(cmd, info->srcImage, info->dstImage, &info->pRegions[i]);
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyImage(VkCommandBuffer cmd, VkImage src, VkImageLayout src_layout,
                                               VkImage dst, VkImageLayout dst_layout, uint32_t count,
                                               const VkImageCopy *regions)
{
    if (!outside(cmd, "an image copy"))
        return;
    for (uint32_t i = 0; i < count; i++) {
        const VkImageCopy *r = &regions[i];
        VkImageCopy2 two = {VK_STRUCTURE_TYPE_IMAGE_COPY_2, nullptr, r->srcSubresource, r->srcOffset,
                            r->dstSubresource,          r->dstOffset, r->extent};
        copy_image(cmd, src, dst, &two);
    }
}

/* a render pass of its own on one level and layer of an image. */
static VkmBeginRender *own_pass(VkCommandBuffer cmd, VkImage image, uint32_t level, uint32_t layer)
{
    VkmBeginRender *b = vkm_emit(cmd, VKM_OP_BEGIN_RENDER, sizeof(*b));
    if (!b)
        return nullptr;
    VkExtent3D e = vkm_level_extent(image, level);
    b->width = e.width;
    b->height = e.height;
    b->layers = 1;
    b->samples = image->samples;
    VkmAttach a = {image->texture, nullptr, level, layer};
    a.load = MTLLoadActionLoad;
    a.store = MTLStoreActionStore;
    if (image->fi->aspects & VK_IMAGE_ASPECT_COLOR_BIT) {
        b->color_count = 1;
        b->colors[0] = a;
    } else {
        if (image->fi->aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
            b->depth = a;
        if (image->fi->aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
            b->stencil = a;
    }
    return b;
}

static void clear_image(VkCommandBuffer cmd, VkImage image, const VkClearColorValue *color,
                        const VkClearDepthStencilValue *ds, uint32_t count, const VkImageSubresourceRange *ranges)
{
    if (!outside(cmd, "an image clear"))
        return;
    for (uint32_t i = 0; i < count; i++) {
        const VkImageSubresourceRange *r = &ranges[i];
        uint32_t levels = r->levelCount == VK_REMAINING_MIP_LEVELS ? image->levels - r->baseMipLevel : r->levelCount;
        uint32_t layers = r->layerCount == VK_REMAINING_ARRAY_LAYERS ? image->layers - r->baseArrayLayer : r->layerCount;
        for (uint32_t m = 0; m < levels; m++) {
            uint32_t n = image->type == VK_IMAGE_TYPE_3D ? vkm_level_extent(image, r->baseMipLevel + m).depth : layers;
            for (uint32_t l = 0; l < n; l++) {
                VkmBeginRender *b = own_pass(cmd, image, r->baseMipLevel + m,
                                             image->type == VK_IMAGE_TYPE_3D ? l : r->baseArrayLayer + l);
                if (!b)
                    return;
                if (color) {
                    b->colors[0].load = MTLLoadActionClear;
                    b->colors[0].clear = vkm_clear_color(image->format, color);
                } else {
                    if ((r->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) && b->depth.texture) {
                        b->depth.load = MTLLoadActionClear;
                        b->depth.clear.red = ds->depth;
                    }
                    if ((r->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) && b->stencil.texture) {
                        b->stencil.load = MTLLoadActionClear;
                        b->stencil.clear.red = ds->stencil;
                    }
                }
                vkm_emit(cmd, VKM_OP_END_RENDER, sizeof(VkmOp));
            }
        }
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdClearColorImage(VkCommandBuffer cmd, VkImage image, VkImageLayout layout,
                                                     const VkClearColorValue *color, uint32_t count,
                                                     const VkImageSubresourceRange *ranges)
{
    clear_image(cmd, image, color, nullptr, count, ranges);
}

static VKAPI_ATTR void VKAPI_CALL CmdClearDepthStencilImage(VkCommandBuffer cmd, VkImage image, VkImageLayout layout,
                                                            const VkClearDepthStencilValue *ds, uint32_t count,
                                                            const VkImageSubresourceRange *ranges)
{
    clear_image(cmd, image, nullptr, ds, count, ranges);
}

void vkm_clear_rects(VkCommandBuffer cmd, uint32_t count, const VkClearAttachment *clears, uint32_t rect_count,
                     const VkClearRect *rects)
{
    VkmGfxState *s = STATE(cmd);
    if (!s->rendering || !count || !rect_count)
        return;
    VkmBeginRender *b = (VkmBeginRender *)(cmd->cmds + s->begin);
    bool whole = rect_count == 1 && rects[0].rect.offset.x == 0 && rects[0].rect.offset.y == 0 &&
                 rects[0].rect.extent.width >= s->width && rects[0].rect.extent.height >= s->height &&
                 rects[0].baseArrayLayer == 0 && rects[0].layerCount >= s->layers;
    if (!s->drawn && whole && b->head.op == VKM_OP_BEGIN_RENDER) {
        for (uint32_t i = 0; i < count; i++) {
            const VkClearAttachment *c = &clears[i];
            if (c->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
                if (c->colorAttachment < b->color_count && b->colors[c->colorAttachment].texture) {
                    VkmAttach *a = &b->colors[c->colorAttachment];
                    a->load = MTLLoadActionClear;
                    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    for (VkFormat f = VK_FORMAT_R8_UNORM; f <= VK_FORMAT_E5B9G9R9_UFLOAT_PACK32; f++)
                        if (vkm_format(f)->pixel == s->colors[c->colorAttachment]) {
                            format = f;
                            break;
                        }
                    a->clear = vkm_clear_color(format, &c->clearValue.color);
                }
                continue;
            }
            if ((c->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) && b->depth.texture) {
                b->depth.load = MTLLoadActionClear;
                b->depth.clear.red = c->clearValue.depthStencil.depth;
            }
            if ((c->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) && b->stencil.texture) {
                b->stencil.load = MTLLoadActionClear;
                b->stencil.clear.red = c->clearValue.depthStencil.stencil;
            }
        }
        return;
    }
    /* a draw over the rectangles: the values go to the shaders, depth through the vertices. */
    uint32_t mask = 0;
    bool depth = false, stencil = false;
    struct {
        VkClearColorValue colors[VKM_COLORS];
        float depth;
    } values = {};
    uint32_t stencil_value = 0;
    for (uint32_t i = 0; i < count; i++) {
        const VkClearAttachment *c = &clears[i];
        if ((c->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) && c->colorAttachment < VKM_COLORS &&
            s->colors[c->colorAttachment]) {
            mask |= 1u << c->colorAttachment;
            values.colors[c->colorAttachment] = c->clearValue.color;
        }
        if ((c->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) && s->has_depth) {
            depth = true;
            values.depth = c->clearValue.depthStencil.depth;
        }
        if ((c->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) && s->has_stencil) {
            stencil = true;
            stencil_value = c->clearValue.depthStencil.stencil;
        }
    }
    if (!mask && !depth && !stencil)
        return;
    id<MTLRenderPipelineState> pipeline = vkm_meta_clear(cmd->device, s->colors, mask, depth, stencil, s->samples, s->layers > 1);
    if (!pipeline)
        return;
    uint64_t gpu;
    void *p = vkm_upload(cmd, sizeof(values), 16, &gpu, nullptr, nullptr);
    if (!p)
        return;
    memcpy(p, &values, sizeof(values));
    for (uint32_t at = 0; at < rect_count; at += 16) {
        VkmOpClearRects *o = vkm_emit(cmd, VKM_OP_CLEAR_RECTS, sizeof(*o));
        if (!o)
            return;
        o->pipeline = pipeline;
        o->depth_state = vkm_meta_depth_state(cmd->device, depth, stencil);
        o->values = gpu;
        o->stencil = stencil_value;
        o->rect_count = MIME_MIN(rect_count - at, 16u);
        for (uint32_t i = 0; i < o->rect_count; i++) {
            const VkClearRect *r = &rects[at + i];
            VkRect2D rect = r->rect;
            /* the clear stays inside the attachments, which a scissor must. */
            uint32_t x = MIME_MIN((uint32_t)MIME_MAX(rect.offset.x, 0), s->width);
            uint32_t y = MIME_MIN((uint32_t)MIME_MAX(rect.offset.y, 0), s->height);
            rect.offset = (VkOffset2D){(int32_t)x, (int32_t)y};
            rect.extent.width = MIME_MIN(rect.extent.width, s->width - x);
            rect.extent.height = MIME_MIN(rect.extent.height, s->height - y);
            o->rects[i] = rect;
            o->base_layer[i] = r->baseArrayLayer;
            o->layer_count[i] = r->layerCount == VK_REMAINING_ARRAY_LAYERS ? s->layers - r->baseArrayLayer : r->layerCount;
        }
    }
    vkm_forget(cmd);
}

static VKAPI_ATTR void VKAPI_CALL CmdClearAttachments(VkCommandBuffer cmd, uint32_t count,
                                                      const VkClearAttachment *clears, uint32_t rect_count,
                                                      const VkClearRect *rects)
{
    vkm_clear_rects(cmd, count, clears, rect_count, rects);
}

static bool same_size(VkOffset3D a0, VkOffset3D a1, VkOffset3D b0, VkOffset3D b1)
{
    return a1.x - a0.x == b1.x - b0.x && a1.y - a0.y == b1.y - b0.y && a1.z - a0.z == b1.z - b0.z && a1.x > a0.x &&
           a1.y > a0.y && a1.z > a0.z;
}

static void blit_image(VkCommandBuffer cmd, VkImage src, VkImage dst, const VkImageBlit2 *r, VkFilter filter)
{
    const VkOffset3D *s0 = &r->srcOffsets[0], *s1 = &r->srcOffsets[1], *d0 = &r->dstOffsets[0], *d1 = &r->dstOffsets[1];
    uint32_t layers = r->srcSubresource.layerCount == VK_REMAINING_ARRAY_LAYERS
                          ? src->layers - r->srcSubresource.baseArrayLayer
                          : r->srcSubresource.layerCount;
    /* an unscaled blit between images of one format is a copy. */
    if (src->fi->pixel == dst->fi->pixel && same_size(*s0, *s1, *d0, *d1) && src->samples == dst->samples) {
        VkImageCopy2 copy = {VK_STRUCTURE_TYPE_IMAGE_COPY_2, nullptr, r->srcSubresource, *s0, r->dstSubresource, *d0,
                             {(uint32_t)(s1->x - s0->x), (uint32_t)(s1->y - s0->y), (uint32_t)(s1->z - s0->z)}};
        copy_image(cmd, src, dst, &copy);
        return;
    }
    bool depth = (dst->fi->aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
    VkExtent3D se = vkm_level_extent(src, r->srcSubresource.mipLevel);
    VkExtent3D de = vkm_level_extent(dst, r->dstSubresource.mipLevel);
    uint32_t type = (uint32_t)src->texture.textureType;
    id<MTLRenderPipelineState> pipeline = vkm_meta_blit(cmd->device, dst->fi->pixel, type, depth, dst->samples);
    if (!pipeline)
        return;
    for (uint32_t l = 0; l < layers; l++) {
        VkmBeginRender *b = own_pass(cmd, dst, r->dstSubresource.mipLevel, r->dstSubresource.baseArrayLayer + l);
        if (!b)
            return;
        VkmOpBlit *o = vkm_emit(cmd, VKM_OP_BLIT, sizeof(*o));
        if (!o)
            return;
        o->pipeline = pipeline;
        o->depth_state = depth ? vkm_meta_depth_state(cmd->device, true, false) : nullptr;
        o->src = src->texture;
        o->src_level = r->srcSubresource.mipLevel;
        o->dst_width = de.width;
        o->dst_height = de.height;
        int32_t x0 = MIME_MIN(d0->x, d1->x), x1 = MIME_MAX(d0->x, d1->x);
        int32_t y0 = MIME_MIN(d0->y, d1->y), y1 = MIME_MAX(d0->y, d1->y);
        o->rect = (VkRect2D){{x0, y0}, {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0)}};
        /* the source corners at the destination's first and second corners, flips included. */
        VkmBlitParams p = {};
        float u0 = (float)s0->x / se.width, u1 = (float)s1->x / se.width;
        float v0 = (float)s0->y / se.height, v1 = (float)s1->y / se.height;
        if (d0->x > d1->x) {
            float t = u0;
            u0 = u1;
            u1 = t;
        }
        if (d0->y > d1->y) {
            float t = v0;
            v0 = v1;
            v1 = t;
        }
        p.src[0] = u0;
        p.src[1] = v0;
        p.src[2] = u1;
        p.src[3] = v1;
        p.layer = src->type == VK_IMAGE_TYPE_3D ? ((float)s0->z + 0.5f) / se.depth
                                                : (float)(r->srcSubresource.baseArrayLayer + l);
        p.lod = (float)r->srcSubresource.mipLevel;
        void *at = vkm_upload(cmd, sizeof(p), 16, &o->params, nullptr, nullptr);
        if (!at)
            return;
        memcpy(at, &p, sizeof(p));
        o->sampler = filter == VK_FILTER_LINEAR ? vkm_sampler_linear(cmd->device) : vkm_sampler_nearest(cmd->device);
        vkm_emit(cmd, VKM_OP_END_RENDER, sizeof(VkmOp));
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdBlitImage2(VkCommandBuffer cmd, const VkBlitImageInfo2 *info)
{
    if (!outside(cmd, "an image blit"))
        return;
    for (uint32_t i = 0; i < info->regionCount; i++)
        blit_image(cmd, info->srcImage, info->dstImage, &info->pRegions[i], info->filter);
}

static VKAPI_ATTR void VKAPI_CALL CmdBlitImage(VkCommandBuffer cmd, VkImage src, VkImageLayout src_layout,
                                               VkImage dst, VkImageLayout dst_layout, uint32_t count,
                                               const VkImageBlit *regions, VkFilter filter)
{
    if (!outside(cmd, "an image blit"))
        return;
    for (uint32_t i = 0; i < count; i++) {
        const VkImageBlit *r = &regions[i];
        VkImageBlit2 two = {VK_STRUCTURE_TYPE_IMAGE_BLIT_2, nullptr, r->srcSubresource,
                            {r->srcOffsets[0], r->srcOffsets[1]}, r->dstSubresource,
                            {r->dstOffsets[0], r->dstOffsets[1]}};
        blit_image(cmd, src, dst, &two, filter);
    }
}

/* a resolve is a render pass on the multisampled image that resolves at its end. */
static void resolve_image(VkCommandBuffer cmd, VkImage src, VkImage dst, const VkImageResolve2 *r)
{
    uint32_t layers = r->srcSubresource.layerCount == VK_REMAINING_ARRAY_LAYERS
                          ? src->layers - r->srcSubresource.baseArrayLayer
                          : r->srcSubresource.layerCount;
    for (uint32_t l = 0; l < layers; l++) {
        VkmBeginRender *b = own_pass(cmd, src, r->srcSubresource.mipLevel, r->srcSubresource.baseArrayLayer + l);
        if (!b)
            return;
        VkmAttach *a = b->color_count ? &b->colors[0] : &b->depth;
        a->store = MTLStoreActionMultisampleResolve;
        a->resolve = dst->texture;
        a->resolve_level = r->dstSubresource.mipLevel;
        a->resolve_slice = r->dstSubresource.baseArrayLayer + l;
        vkm_emit(cmd, VKM_OP_END_RENDER, sizeof(VkmOp));
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdResolveImage2(VkCommandBuffer cmd, const VkResolveImageInfo2 *info)
{
    if (!outside(cmd, "an image resolve"))
        return;
    for (uint32_t i = 0; i < info->regionCount; i++)
        resolve_image(cmd, info->srcImage, info->dstImage, &info->pRegions[i]);
}

static VKAPI_ATTR void VKAPI_CALL CmdResolveImage(VkCommandBuffer cmd, VkImage src, VkImageLayout src_layout,
                                                  VkImage dst, VkImageLayout dst_layout, uint32_t count,
                                                  const VkImageResolve *regions)
{
    if (!outside(cmd, "an image resolve"))
        return;
    for (uint32_t i = 0; i < count; i++) {
        const VkImageResolve *r = &regions[i];
        VkImageResolve2 two = {VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2, nullptr, r->srcSubresource, r->srcOffset,
                               r->dstSubresource,             r->dstOffset, r->extent};
        resolve_image(cmd, src, dst, &two);
    }
}

/* ------------------------------------------------------------------ synchronization. */

static void barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst)
{
    VkmOpBarrier *o = vkm_emit(cmd, VKM_OP_BARRIER, sizeof(*o));
    if (o) {
        o->after = vkm_stages(src);
        o->before = vkm_stages(dst);
    }
}

static void dependency(VkCommandBuffer cmd, const VkDependencyInfo *info)
{
    VkPipelineStageFlags2 src = 0, dst = 0;
    for (uint32_t i = 0; i < info->memoryBarrierCount; i++) {
        src |= info->pMemoryBarriers[i].srcStageMask;
        dst |= info->pMemoryBarriers[i].dstStageMask;
    }
    for (uint32_t i = 0; i < info->bufferMemoryBarrierCount; i++) {
        src |= info->pBufferMemoryBarriers[i].srcStageMask;
        dst |= info->pBufferMemoryBarriers[i].dstStageMask;
    }
    for (uint32_t i = 0; i < info->imageMemoryBarrierCount; i++) {
        src |= info->pImageMemoryBarriers[i].srcStageMask;
        dst |= info->pImageMemoryBarriers[i].dstStageMask;
    }
    if (src || dst)
        barrier(cmd, src, dst);
}

static VKAPI_ATTR void VKAPI_CALL CmdPipelineBarrier2(VkCommandBuffer cmd, const VkDependencyInfo *info)
{
    dependency(cmd, info);
}

static VKAPI_ATTR void VKAPI_CALL CmdPipelineBarrier(VkCommandBuffer cmd, VkPipelineStageFlags src,
                                                     VkPipelineStageFlags dst, VkDependencyFlags flags,
                                                     uint32_t memory_count, const VkMemoryBarrier *memory,
                                                     uint32_t buffer_count, const VkBufferMemoryBarrier *buffers,
                                                     uint32_t image_count, const VkImageMemoryBarrier *images)
{
    barrier(cmd, src, dst);
}

static VKAPI_ATTR void VKAPI_CALL CmdSetEvent2(VkCommandBuffer cmd, VkEvent event, const VkDependencyInfo *info)
{
    VkmOpQuery *o = vkm_emit(cmd, VKM_OP_EVENT, sizeof(*o));
    if (o) {
        o->pool = (VkQueryPool)(void *)event;
        o->first = 1;
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdSetEvent(VkCommandBuffer cmd, VkEvent event, VkPipelineStageFlags stages)
{
    CmdSetEvent2(cmd, event, nullptr);
}

static VKAPI_ATTR void VKAPI_CALL CmdResetEvent2(VkCommandBuffer cmd, VkEvent event, VkPipelineStageFlags2 stages)
{
    VkmOpQuery *o = vkm_emit(cmd, VKM_OP_EVENT, sizeof(*o));
    if (o)
        o->pool = (VkQueryPool)(void *)event;
}

static VKAPI_ATTR void VKAPI_CALL CmdResetEvent(VkCommandBuffer cmd, VkEvent event, VkPipelineStageFlags stages)
{
    CmdResetEvent2(cmd, event, stages);
}

static VKAPI_ATTR void VKAPI_CALL CmdWaitEvents2(VkCommandBuffer cmd, uint32_t count, const VkEvent *events,
                                                 const VkDependencyInfo *infos)
{
    for (uint32_t i = 0; i < count; i++)
        dependency(cmd, &infos[i]);
}

static VKAPI_ATTR void VKAPI_CALL CmdWaitEvents(VkCommandBuffer cmd, uint32_t count, const VkEvent *events,
                                                VkPipelineStageFlags src, VkPipelineStageFlags dst,
                                                uint32_t memory_count, const VkMemoryBarrier *memory,
                                                uint32_t buffer_count, const VkBufferMemoryBarrier *buffers,
                                                uint32_t image_count, const VkImageMemoryBarrier *images)
{
    barrier(cmd, src, dst);
}

/* ------------------------------------------------------------------ queries. */

static VKAPI_ATTR void VKAPI_CALL CmdResetQueryPool(VkCommandBuffer cmd, VkQueryPool pool, uint32_t first,
                                                    uint32_t count)
{
    if (!outside(cmd, "a query pool reset"))
        return;
    VkmOpQuery *o = vkm_emit(cmd, VKM_OP_QUERY_RESET, sizeof(*o));
    if (o) {
        o->pool = pool;
        o->first = first;
        o->count = count;
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdBeginQuery(VkCommandBuffer cmd, VkQueryPool pool, uint32_t query,
                                                VkQueryControlFlags flags)
{
    VkmGfxState *s = STATE(cmd);
    if (pool->type == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT) {
        s->xfb_query = pool;
        s->xfb_query_index = query;
        return;
    }
    if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
        s->stats = pool;
        s->stats_query = query;
        s->pipeline_dirty = true;
        return;
    }
    if (pool->type != VK_QUERY_TYPE_OCCLUSION)
        return;
    s->occlusion = pool;
    s->occlusion_query = query;
    s->occlusion_precise = flags & VK_QUERY_CONTROL_PRECISE_BIT;
    if (!s->rendering)
        return;
    VkmBeginRender *b = (VkmBeginRender *)(cmd->cmds + s->begin);
    if (b->visibility && b->visibility != pool->buffer)
        VKM_ERROR("occlusion queries of two pools in one render pass");
    b->visibility = pool->buffer;
    VkmOpVisibility *o = vkm_emit(cmd, VKM_OP_VISIBILITY, sizeof(*o));
    if (o) {
        o->mode = s->occlusion_precise ? MTLVisibilityResultModeCounting : MTLVisibilityResultModeBoolean;
        o->offset = query * 8;
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdEndQuery(VkCommandBuffer cmd, VkQueryPool pool, uint32_t query)
{
    VkmGfxState *s = STATE(cmd);
    if (pool->type == VK_QUERY_TYPE_OCCLUSION) {
        if (s->rendering) {
            VkmOpVisibility *o = vkm_emit(cmd, VKM_OP_VISIBILITY, sizeof(*o));
            if (o)
                o->mode = MTLVisibilityResultModeDisabled;
        }
        s->occlusion = nullptr;
    }
    if (pool->type == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT && s->xfb_query == pool)
        s->xfb_query = nullptr;
    if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS && s->stats == pool) {
        s->stats = nullptr;
        s->pipeline_dirty = true;
    }
    /* the query's result is there once this command buffer has run. */
    VkmOpQuery *o = vkm_emit(cmd, VKM_OP_TIMESTAMP, sizeof(*o));
    if (o) {
        o->pool = pool;
        o->first = query;
        o->count = 1;
        o->flags = 1;
    }
}

/* stream output queries count stream zero, the only one there is. */
static VKAPI_ATTR void VKAPI_CALL CmdBeginQueryIndexedEXT(VkCommandBuffer cmd, VkQueryPool pool, uint32_t query,
                                                          VkQueryControlFlags flags, uint32_t index)
{
    if (!index)
        CmdBeginQuery(cmd, pool, query, flags);
}

static VKAPI_ATTR void VKAPI_CALL CmdEndQueryIndexedEXT(VkCommandBuffer cmd, VkQueryPool pool, uint32_t query,
                                                        uint32_t index)
{
    CmdEndQuery(cmd, pool, query);
}

static VKAPI_ATTR void VKAPI_CALL CmdWriteTimestamp2(VkCommandBuffer cmd, VkPipelineStageFlags2 stage,
                                                     VkQueryPool pool, uint32_t query)
{
    VkmOpQuery *o = vkm_emit(cmd, VKM_OP_TIMESTAMP, sizeof(*o));
    if (o) {
        o->pool = pool;
        o->first = query;
        o->count = 1;
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdWriteTimestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage,
                                                    VkQueryPool pool, uint32_t query)
{
    CmdWriteTimestamp2(cmd, stage, pool, query);
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyQueryPoolResults(VkCommandBuffer cmd, VkQueryPool pool, uint32_t first,
                                                          uint32_t count, VkBuffer dst, VkDeviceSize offset,
                                                          VkDeviceSize stride, VkQueryResultFlags flags)
{
    if (!outside(cmd, "a query result copy"))
        return;
    VkmOpQuery *o = vkm_emit(cmd, VKM_OP_QUERY_COPY, sizeof(*o));
    if (o) {
        o->pool = pool;
        o->first = first;
        o->count = count;
        o->buffer = dst;
        o->offset = offset;
        o->stride = stride;
        o->flags = flags;
    }
}

const VkmEntry vkm_xfer_entries[] = {
    VKM_ENTRY(CmdCopyBuffer),
    VKM_ENTRY(CmdCopyBuffer2),
    VKM_ALIAS("vkCmdCopyBuffer2KHR", CmdCopyBuffer2),
    VKM_ENTRY(CmdUpdateBuffer),
    VKM_ENTRY(CmdFillBuffer),
    VKM_ENTRY(CmdCopyBufferToImage),
    VKM_ENTRY(CmdCopyBufferToImage2),
    VKM_ALIAS("vkCmdCopyBufferToImage2KHR", CmdCopyBufferToImage2),
    VKM_ENTRY(CmdCopyImageToBuffer),
    VKM_ENTRY(CmdCopyImageToBuffer2),
    VKM_ALIAS("vkCmdCopyImageToBuffer2KHR", CmdCopyImageToBuffer2),
    VKM_ENTRY(CmdCopyImage),
    VKM_ENTRY(CmdCopyImage2),
    VKM_ALIAS("vkCmdCopyImage2KHR", CmdCopyImage2),
    VKM_ENTRY(CmdClearColorImage),
    VKM_ENTRY(CmdClearDepthStencilImage),
    VKM_ENTRY(CmdClearAttachments),
    VKM_ENTRY(CmdBlitImage),
    VKM_ENTRY(CmdBlitImage2),
    VKM_ALIAS("vkCmdBlitImage2KHR", CmdBlitImage2),
    VKM_ENTRY(CmdResolveImage),
    VKM_ENTRY(CmdResolveImage2),
    VKM_ALIAS("vkCmdResolveImage2KHR", CmdResolveImage2),
    VKM_ENTRY(CmdPipelineBarrier),
    VKM_ENTRY(CmdPipelineBarrier2),
    VKM_ALIAS("vkCmdPipelineBarrier2KHR", CmdPipelineBarrier2),
    VKM_ENTRY(CmdSetEvent),
    VKM_ENTRY(CmdSetEvent2),
    VKM_ENTRY(CmdResetEvent),
    VKM_ENTRY(CmdResetEvent2),
    VKM_ENTRY(CmdWaitEvents),
    VKM_ENTRY(CmdWaitEvents2),
    VKM_ENTRY(CmdResetQueryPool),
    VKM_ENTRY(CmdBeginQuery),
    VKM_ENTRY(CmdBeginQueryIndexedEXT),
    VKM_ENTRY(CmdEndQueryIndexedEXT),
    VKM_ENTRY(CmdEndQuery),
    VKM_ENTRY(CmdWriteTimestamp),
    VKM_ENTRY(CmdWriteTimestamp2),
    VKM_ALIAS("vkCmdWriteTimestamp2KHR", CmdWriteTimestamp2),
    VKM_ENTRY(CmdCopyQueryPoolResults),
    {nullptr, nullptr},
};
