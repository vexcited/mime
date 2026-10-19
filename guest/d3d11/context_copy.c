/*
 * what the context does to resources outside draws: mapping them, which
 * waits for the gpu only when the program asks to read or to overwrite in
 * place, updates, copies, clears, mip generation and resolves.
 */
#include "d3d11_private.h"

/* ------------------------------------------------------------------ waiting for the gpu */

/* waits until the gpu is done with the resource, submitting first if it is still recorded. */
static HRESULT wait_idle(D3dContext *c, D3dResource *r, UINT flags)
{
    D3dDevice *device = c->child.device;
    if (r->last_use <= device->completed)
        return S_OK;
    if (r->last_use >= device->next_serial)
        D3dContextFlush(c);
    if (D3dCompleted(device) >= r->last_use)
        return S_OK;
    if (flags & D3D11_MAP_FLAG_DO_NOT_WAIT)
        return DXGI_ERROR_WAS_STILL_DRAWING;
    D3dWaitSerial(device, r->last_use);
    return S_OK;
}

/* ------------------------------------------------------------------ map */

/*
 * renamed ranges come back once the gpu ran past them: when many were
 * taken new, what was recorded is submitted so later renames find some.
 */
static void renaming(D3dContext *c)
{
    if (c->child.device->renames >= 256)
        D3dContextFlush(c);
}

HRESULT D3dCtxMap(D3dContext *c, ID3D11Resource *resource, UINT sub, D3D11_MAP type, UINT flags,
                  D3D11_MAPPED_SUBRESOURCE *mapped)
{
    D3dResource *r = D3dResourceFrom(resource);
    D3dDevice *device = c->child.device;
    if (mapped)
        memset(mapped, 0, sizeof(*mapped));
    if (!r || type < D3D11_MAP_READ || type > D3D11_MAP_WRITE_NO_OVERWRITE)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    /* deferred contexts write dynamic resources only, without waiting. */
    if (c->type == D3D11_DEVICE_CONTEXT_DEFERRED &&
        ((type != D3D11_MAP_WRITE_DISCARD && type != D3D11_MAP_WRITE_NO_OVERWRITE) || r->usage != D3D11_USAGE_DYNAMIC))
        return E_INVALIDARG;
    if (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
        if (!r->buf.mapped || sub || (r->usage != D3D11_USAGE_DYNAMIC && r->usage != D3D11_USAGE_STAGING))
            return E_INVALIDARG;
        if (c->type == D3D11_DEVICE_CONTEXT_DEFERRED) {
            const D3dRingSlice *slice =
                type == D3D11_MAP_WRITE_DISCARD ? D3dContextListDiscard(c, r) : D3dContextListRange(c, r);
            if (!slice)
                return type == D3D11_MAP_WRITE_DISCARD ? E_OUTOFMEMORY : E_INVALIDARG;
            if (mapped) {
                mapped->pData = slice->mapped;
                mapped->RowPitch = mapped->DepthPitch = r->buf.size;
            }
            return S_OK;
        }
        if (type == D3D11_MAP_WRITE_DISCARD) {
            /* a range the gpu does not use, so the program never waits. */
            BOOL retired;
            if (D3dBufferDiscardCb(device, r, &retired)) {
                if (retired)
                    D3dContextFlush(c);
            } else if (r->buf.cb_chunk) {
                hr = wait_idle(c, r, flags);
            } else {
                renaming(c);
                if (r->last_use > device->completed && !D3dBufferRename(device, r))
                    hr = wait_idle(c, r, flags);
            }
        } else if (type != D3D11_MAP_WRITE_NO_OVERWRITE) {
            hr = wait_idle(c, r, flags);
        }
        if (FAILED(hr))
            return hr;
        if (mapped) {
            mapped->pData = r->buf.mapped;
            mapped->RowPitch = mapped->DepthPitch = r->buf.size;
        }
        return S_OK;
    }
    if (!r->tex.staging.mapped || sub >= D3dSubresourceCount(r))
        return E_INVALIDARG;
    if (c->type == D3D11_DEVICE_CONTEXT_DEFERRED) {
        if (c->list_map_count == ARRAYSIZE(c->list_maps))
            return E_OUTOFMEMORY;
        UINT mip, layer;
        D3dSubresourceDecompose(r, sub, &mip, &layer);
        VkDeviceSize size = (VkDeviceSize)r->tex.sub_slice_pitch[sub] * D3dMipExtent(r, mip).depth;
        D3dRingSlice *slice = &c->list_maps[c->list_map_count].slice;
        if (!D3dContextRingAlloc(c, size, D3D_MAX(r->tex.fmt->block_bytes, 16), slice))
            return E_OUTOFMEMORY;
        c->list_maps[c->list_map_count].resource = r;
        c->list_maps[c->list_map_count++].sub = sub;
        if (mapped) {
            mapped->pData = slice->mapped;
            mapped->RowPitch = r->tex.sub_row_pitch[sub];
            mapped->DepthPitch = r->tex.sub_slice_pitch[sub];
        }
        return S_OK;
    }
    if (FAILED(hr = wait_idle(c, r, flags)))
        return hr;
    if (r->usage == D3D11_USAGE_DYNAMIC)
        r->tex.dirty = TRUE;
    if (mapped) {
        mapped->pData = r->tex.staging.mapped + r->tex.sub_offset[sub];
        mapped->RowPitch = r->tex.sub_row_pitch[sub];
        mapped->DepthPitch = r->tex.sub_slice_pitch[sub];
    }
    return S_OK;
}

/* a dynamic texture's mapped bytes, copied to its image where the gpu reads them. */
void D3dCtxUnmap(D3dContext *c, ID3D11Resource *resource, UINT sub)
{
    D3dResource *r = D3dResourceFrom(resource);
    if (!r || r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER || r->usage != D3D11_USAGE_DYNAMIC)
        return;
    D3dDevice *device = c->child.device;
    VkBuffer source = r->tex.staging.buffer;
    VkDeviceSize base = r->tex.staging.offset;
    if (c->type == D3D11_DEVICE_CONTEXT_DEFERRED) {
        UINT i = 0;
        while (i < c->list_map_count && (c->list_maps[i].resource != r || c->list_maps[i].sub != sub))
            i++;
        if (i == c->list_map_count)
            return;
        source = c->list_maps[i].slice.buffer;
        /* the regions add the subresource's offset in the staging layout back. */
        base = c->list_maps[i].slice.offset - r->tex.sub_offset[sub];
        c->list_maps[i] = c->list_maps[--c->list_map_count];
    } else if (!r->tex.dirty) {
        return;
    }
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    VkBufferImageCopy regions[2];
    UINT count;
    D3dImageCopyRegions(r, sub, base, regions, &count);
    device->vk.vkCmdCopyBufferToImage(cmd, source, r->tex.image, VK_IMAGE_LAYOUT_GENERAL, count, regions);
    c->pending_barrier = TRUE;
    r->tex.dirty = FALSE;
    D3dContextUse(c, r);
}

HRESULT D3dContextMapSurface(D3dContext *c, D3dResource *r, DXGI_MAPPED_RECT *rect, UINT flags)
{
    if (!rect)
        return E_INVALIDARG;
    D3D11_MAP type = flags & DXGI_MAP_READ ? (flags & DXGI_MAP_WRITE ? D3D11_MAP_READ_WRITE : D3D11_MAP_READ)
                                           : (flags & DXGI_MAP_DISCARD ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE);
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = D3dCtxMap(c, r->iface, 0, type, 0, &mapped);
    if (SUCCEEDED(hr)) {
        rect->pBits = mapped.pData;
        rect->Pitch = mapped.RowPitch;
    }
    return hr;
}

void D3dContextUnmapSurface(D3dContext *c, D3dResource *r) { D3dCtxUnmap(c, r->iface, 0); }

/* ------------------------------------------------------------------ update */

static void update_buffer(D3dContext *c, D3dResource *r, UINT offset, UINT size, const void *data)
{
    D3dDevice *device = c->child.device;
    if (offset >= r->buf.size || !size)
        return;
    size = D3D_MIN(size, r->buf.size - offset);
    if (r->buf.mapped) {
        /* mapped memory the gpu may be reading: a new range, the old bytes carried over. */
        if (r->last_use > device->completed) {
            renaming(c);
            BYTE *old = r->buf.mapped;
            if (D3dBufferRename(device, r)) {
                if (offset || size < r->buf.size)
                    memcpy(r->buf.mapped, old, r->buf.size);
            } else {
                wait_idle(c, r, 0);
            }
        }
        memcpy(r->buf.mapped + offset, data, size);
        return;
    }
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    if (!(offset % 4) && !(size % 4) && size <= 65536) {
        /* small updates travel in the command stream itself. */
        device->vk.vkCmdUpdateBuffer(cmd, r->buf.buffer, r->buf.offset + offset, size, data);
    } else {
        D3dRingSlice slice;
        if (!D3dContextRingAlloc(c, size, 16, &slice))
            return;
        memcpy(slice.mapped, data, size);
        VkBufferCopy copy = {slice.offset, r->buf.offset + offset, size};
        device->vk.vkCmdCopyBuffer(cmd, slice.buffer, r->buf.buffer, 1, &copy);
    }
    c->pending_barrier = TRUE;
    D3dContextUse(c, r);
}

static void update_texture(D3dContext *c, D3dResource *r, UINT sub, const D3D11_BOX *box, const void *data,
                           UINT row_pitch, UINT depth_pitch)
{
    D3dDevice *device = c->child.device;
    if (sub >= D3dSubresourceCount(r))
        return;
    UINT mip, layer;
    D3dSubresourceDecompose(r, sub, &mip, &layer);
    VkExtent3D e = D3dMipExtent(r, mip);
    D3D11_BOX b = box ? *box : (D3D11_BOX){0, 0, 0, e.width, e.height, e.depth};
    if (b.right <= b.left || b.bottom <= b.top || b.back <= b.front || b.right > e.width || b.bottom > e.height ||
        b.back > e.depth)
        return;
    const D3dFormat *f = r->tex.fmt;
    UINT width = b.right - b.left, height = b.bottom - b.top, depth = b.back - b.front;
    UINT row = (width + f->block - 1) / f->block * f->block_bytes;
    UINT rows = (height + f->block - 1) / f->block;
    if (r->tex.staging.mapped && !r->tex.image) {
        /* a staging texture: its packed bytes, once the gpu is done with them. */
        wait_idle(c, r, 0);
        BYTE *dst = r->tex.staging.mapped + r->tex.sub_offset[sub] + (SIZE_T)b.front * r->tex.sub_slice_pitch[sub] +
                    (SIZE_T)(b.top / f->block) * r->tex.sub_row_pitch[sub] + (SIZE_T)(b.left / f->block) * f->block_bytes;
        UINT stored = f->flags & D3D_FORMAT_PLANAR ? rows * 3 / 2 : rows;
        for (UINT z = 0; z < depth; z++)
            for (UINT y = 0; y < stored; y++)
                memcpy(dst + (SIZE_T)z * r->tex.sub_slice_pitch[sub] + (SIZE_T)y * r->tex.sub_row_pitch[sub],
                       (const BYTE *)data + (SIZE_T)z * depth_pitch + (SIZE_T)y * row_pitch, row);
        return;
    }
    if (f->flags & D3D_FORMAT_PLANAR) {
        /* a video picture is updated whole: its planes follow each other in the data, the chroma half as tall. */
        if (b.left || b.top || width != e.width || height != e.height)
            return;
        UINT total_rows = rows + (rows + 1) / 2;
        D3dRingSlice packed;
        if (!D3dContextRingAlloc(c, (VkDeviceSize)row * total_rows, 16, &packed))
            return;
        for (UINT y = 0; y < total_rows; y++)
            memcpy(packed.mapped + (SIZE_T)y * row, (const BYTE *)data + (SIZE_T)y * row_pitch, row);
        VkBufferImageCopy copies[2] = {
            {packed.offset, row / f->block_bytes, 0, {VK_IMAGE_ASPECT_PLANE_0_BIT, mip, layer, 1}, {0, 0, 0}, {width, height, 1}},
            {packed.offset + (VkDeviceSize)row * rows, row / (2 * f->block_bytes), 0,
             {VK_IMAGE_ASPECT_PLANE_1_BIT, mip, layer, 1}, {0, 0, 0}, {(width + 1) / 2, (height + 1) / 2, 1}}};
        VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
        if (!cmd)
            return;
        device->vk.vkCmdCopyBufferToImage(cmd, packed.buffer, r->tex.image, VK_IMAGE_LAYOUT_GENERAL, 2, copies);
        c->pending_barrier = TRUE;
        D3dContextUse(c, r);
        return;
    }
    D3dRingSlice slice;
    if (!D3dContextRingAlloc(c, (VkDeviceSize)row * rows * depth, D3D_MAX(f->block_bytes, 16), &slice))
        return;
    for (UINT z = 0; z < depth; z++)
        for (UINT y = 0; y < rows; y++)
            memcpy(slice.mapped + (SIZE_T)z * row * rows + (SIZE_T)y * row,
                   (const BYTE *)data + (SIZE_T)z * depth_pitch + (SIZE_T)y * row_pitch, row);
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    VkBufferImageCopy copy = {};
    copy.bufferOffset = slice.offset;
    copy.imageSubresource = (VkImageSubresourceLayers){r->tex.aspect & ~VK_IMAGE_ASPECT_STENCIL_BIT, mip, layer, 1};
    if (r->tex.aspect == VK_IMAGE_ASPECT_STENCIL_BIT)
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    copy.imageOffset = (VkOffset3D){b.left, b.top, b.front};
    copy.imageExtent = (VkExtent3D){width, height, depth};
    device->vk.vkCmdCopyBufferToImage(cmd, slice.buffer, r->tex.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    c->pending_barrier = TRUE;
    D3dContextUse(c, r);
}

void D3dCtxUpdateSubresource(D3dContext *c, ID3D11Resource *dst, UINT dst_sub, const D3D11_BOX *box,
                             const void *data, UINT row_pitch, UINT depth_pitch, UINT flags)
{
    D3dResource *r = D3dResourceFrom(dst);
    if (!r || !data)
        return;
    if (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
        if (dst_sub)
            return;
        UINT offset = box ? box->left : 0;
        UINT size = box ? (box->right > box->left ? box->right - box->left : 0) : r->buf.size;
        update_buffer(c, r, offset, size, data);
        return;
    }
    update_texture(c, r, dst_sub, box, data, row_pitch, depth_pitch);
}

/* ------------------------------------------------------------------ copies */

/* a box of a staging texture's packed bytes as a buffer region seen from an image copy. */
static VkBufferImageCopy staging_copy(const D3dResource *staging, UINT sub, VkOffset3D at)
{
    const D3dFormat *f = staging->tex.fmt;
    VkBufferImageCopy c = {};
    c.bufferOffset = staging->tex.staging.offset + staging->tex.sub_offset[sub] +
                     (VkDeviceSize)at.z * staging->tex.sub_slice_pitch[sub] +
                     (VkDeviceSize)(at.y / f->block) * staging->tex.sub_row_pitch[sub] +
                     (VkDeviceSize)(at.x / f->block) * f->block_bytes;
    c.bufferRowLength = staging->tex.sub_row_pitch[sub] / f->block_bytes * f->block;
    c.bufferImageHeight = staging->tex.sub_slice_pitch[sub] / staging->tex.sub_row_pitch[sub] * f->block;
    return c;
}

/* a video picture, whole, plane by plane: images copy their planes, staging copies are the planes packed. */
static void copy_planar(D3dContext *c, D3dResource *dst, UINT dst_sub, D3dResource *src, UINT src_sub)
{
    D3dVk *vk = &c->child.device->vk;
    if (dst->tex.format != src->tex.format || dst_sub >= D3dSubresourceCount(dst) ||
        src_sub >= D3dSubresourceCount(src))
        return;
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    UINT dst_mip, dst_layer, src_mip, src_layer;
    D3dSubresourceDecompose(dst, dst_sub, &dst_mip, &dst_layer);
    D3dSubresourceDecompose(src, src_sub, &src_mip, &src_layer);
    VkBufferImageCopy regions[2];
    UINT count;
    if (src->tex.image && dst->tex.image) {
        VkImageCopy copies[2];
        for (UINT p = 0; p < 2; p++) {
            VkImageAspectFlags plane = p ? VK_IMAGE_ASPECT_PLANE_1_BIT : VK_IMAGE_ASPECT_PLANE_0_BIT;
            UINT w = p ? (src->tex.width + 1) / 2 : src->tex.width, h = p ? (src->tex.height + 1) / 2 : src->tex.height;
            copies[p] = (VkImageCopy){{plane, src_mip, src_layer, 1}, {0, 0, 0}, {plane, dst_mip, dst_layer, 1}, {0, 0, 0},
                                      {D3D_MIN(w, dst->tex.width), D3D_MIN(h, dst->tex.height), 1}};
        }
        vk->vkCmdCopyImage(cmd, src->tex.image, VK_IMAGE_LAYOUT_GENERAL, dst->tex.image, VK_IMAGE_LAYOUT_GENERAL, 2,
                           copies);
    } else if (src->tex.image) {
        D3dImageCopyRegions(dst, dst_sub, dst->tex.staging.offset, regions, &count);
        for (UINT p = 0; p < count; p++)
            regions[p].imageSubresource.baseArrayLayer = src_layer;
        vk->vkCmdCopyImageToBuffer(cmd, src->tex.image, VK_IMAGE_LAYOUT_GENERAL, dst->tex.staging.buffer, count, regions);
    } else if (dst->tex.image) {
        D3dImageCopyRegions(src, src_sub, src->tex.staging.offset, regions, &count);
        for (UINT p = 0; p < count; p++)
            regions[p].imageSubresource.baseArrayLayer = dst_layer;
        vk->vkCmdCopyBufferToImage(cmd, src->tex.staging.buffer, dst->tex.image, VK_IMAGE_LAYOUT_GENERAL, count, regions);
    } else {
        VkBufferCopy copy = {src->tex.staging.offset + src->tex.sub_offset[src_sub],
                             dst->tex.staging.offset + dst->tex.sub_offset[dst_sub],
                             D3D_MIN(src->tex.sub_slice_pitch[src_sub], dst->tex.sub_slice_pitch[dst_sub])};
        vk->vkCmdCopyBuffer(cmd, src->tex.staging.buffer, dst->tex.staging.buffer, 1, &copy);
    }
    c->pending_barrier = TRUE;
    D3dContextUse(c, src);
    D3dContextUse(c, dst);
}

static void copy_texture(D3dContext *c, D3dResource *dst, UINT dst_sub, UINT x, UINT y, UINT z, D3dResource *src,
                         UINT src_sub, const D3D11_BOX *box)
{
    if ((src->tex.fmt->flags | dst->tex.fmt->flags) & D3D_FORMAT_PLANAR) {
        if (!x && !y && !z && !box)
            copy_planar(c, dst, dst_sub, src, src_sub);
        return;
    }
    D3dDevice *device = c->child.device;
    D3dVk *vk = &device->vk;
    if (dst_sub >= D3dSubresourceCount(dst) || src_sub >= D3dSubresourceCount(src))
        return;
    UINT src_mip, src_layer, dst_mip, dst_layer;
    D3dSubresourceDecompose(src, src_sub, &src_mip, &src_layer);
    D3dSubresourceDecompose(dst, dst_sub, &dst_mip, &dst_layer);
    VkExtent3D se = D3dMipExtent(src, src_mip), de = D3dMipExtent(dst, dst_mip);
    D3D11_BOX b = box ? *box : (D3D11_BOX){0, 0, 0, se.width, se.height, se.depth};
    b.right = D3D_MIN(b.right, se.width), b.bottom = D3D_MIN(b.bottom, se.height), b.back = D3D_MIN(b.back, se.depth);
    if (b.right <= b.left || b.bottom <= b.top || b.back <= b.front)
        return;
    VkExtent3D extent = {b.right - b.left, b.bottom - b.top, b.back - b.front};
    /* clipped to the destination, keeping whole compressed blocks. */
    extent.width = D3D_MIN(extent.width, de.width - D3D_MIN(x, de.width));
    extent.height = D3D_MIN(extent.height, de.height - D3D_MIN(y, de.height));
    extent.depth = D3D_MIN(extent.depth, de.depth - D3D_MIN(z, de.depth));
    if (!extent.width || !extent.height || !extent.depth)
        return;
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    VkImageAspectFlags aspect = src->tex.aspect & dst->tex.aspect;
    if (!aspect)
        aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkOffset3D src_at = {b.left, b.top, b.front}, dst_at = {x, y, z};
    if (src->tex.image && dst->tex.image) {
        VkImageCopy copy = {{aspect, src_mip, src_layer, 1}, src_at, {aspect, dst_mip, dst_layer, 1}, dst_at, extent};
        vk->vkCmdCopyImage(cmd, src->tex.image, VK_IMAGE_LAYOUT_GENERAL, dst->tex.image, VK_IMAGE_LAYOUT_GENERAL, 1,
                           &copy);
    } else if (src->tex.image) {
        VkBufferImageCopy copy = staging_copy(dst, dst_sub, dst_at);
        copy.imageSubresource = (VkImageSubresourceLayers){aspect & ~VK_IMAGE_ASPECT_STENCIL_BIT, src_mip, src_layer, 1};
        if (!copy.imageSubresource.aspectMask)
            copy.imageSubresource.aspectMask = aspect;
        copy.imageOffset = src_at;
        copy.imageExtent = extent;
        vk->vkCmdCopyImageToBuffer(cmd, src->tex.image, VK_IMAGE_LAYOUT_GENERAL, dst->tex.staging.buffer, 1, &copy);
    } else if (dst->tex.image) {
        VkBufferImageCopy copy = staging_copy(src, src_sub, src_at);
        copy.imageSubresource = (VkImageSubresourceLayers){aspect & ~VK_IMAGE_ASPECT_STENCIL_BIT, dst_mip, dst_layer, 1};
        if (!copy.imageSubresource.aspectMask)
            copy.imageSubresource.aspectMask = aspect;
        copy.imageOffset = dst_at;
        copy.imageExtent = extent;
        vk->vkCmdCopyBufferToImage(cmd, src->tex.staging.buffer, dst->tex.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    } else {
        /* staging to staging: row by row. */
        const D3dFormat *f = src->tex.fmt;
        UINT rows = (extent.height + f->block - 1) / f->block;
        UINT row = (extent.width + f->block - 1) / f->block * f->block_bytes;
        for (UINT zz = 0; zz < extent.depth; zz++)
            for (UINT yy = 0; yy < rows; yy++) {
                VkBufferImageCopy s = staging_copy(src, src_sub, (VkOffset3D){src_at.x, src_at.y + yy * f->block,
                                                                               src_at.z + zz});
                VkBufferImageCopy d = staging_copy(dst, dst_sub, (VkOffset3D){dst_at.x, dst_at.y + yy * f->block,
                                                                               dst_at.z + zz});
                VkBufferCopy copy = {s.bufferOffset, d.bufferOffset, row};
                vk->vkCmdCopyBuffer(cmd, src->tex.staging.buffer, dst->tex.staging.buffer, 1, &copy);
            }
    }
    c->pending_barrier = TRUE;
    D3dContextUse(c, src);
    D3dContextUse(c, dst);
}

static void copy_buffer(D3dContext *c, D3dResource *dst, UINT x, D3dResource *src, const D3D11_BOX *box)
{
    D3dDevice *device = c->child.device;
    UINT left = box ? box->left : 0, right = box ? box->right : src->buf.size;
    right = D3D_MIN(right, src->buf.size);
    if (right <= left || x >= dst->buf.size)
        return;
    UINT size = D3D_MIN(right - left, dst->buf.size - x);
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    VkDeviceSize from = src->buf.offset + left, to = dst->buf.offset + x;
    if (src->buf.buffer == dst->buf.buffer && from < to + size && to < from + size) {
        /* overlapping ranges go through the ring. */
        D3dRingSlice slice;
        if (!D3dContextRingAlloc(c, size, 16, &slice))
            return;
        VkBufferCopy first = {from, slice.offset, size}, second = {slice.offset, to, size};
        device->vk.vkCmdCopyBuffer(cmd, src->buf.buffer, slice.buffer, 1, &first);
        D3dBarrier(cmd, device);
        device->vk.vkCmdCopyBuffer(cmd, slice.buffer, dst->buf.buffer, 1, &second);
    } else {
        VkBufferCopy copy = {from, to, size};
        device->vk.vkCmdCopyBuffer(cmd, src->buf.buffer, dst->buf.buffer, 1, &copy);
    }
    c->pending_barrier = TRUE;
    D3dContextUse(c, src);
    D3dContextUse(c, dst);
}

void D3dCtxCopySubresourceRegion(D3dContext *c, ID3D11Resource *dst, UINT dst_sub, UINT x, UINT y, UINT z,
                                 ID3D11Resource *src, UINT src_sub, const D3D11_BOX *box, UINT flags)
{
    D3dResource *d = D3dResourceFrom(dst), *s = D3dResourceFrom(src);
    if (!d || !s || (d->dimension == D3D11_RESOURCE_DIMENSION_BUFFER) != (s->dimension == D3D11_RESOURCE_DIMENSION_BUFFER))
        return;
    if (d->dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
        copy_buffer(c, d, x, s, box);
    else
        copy_texture(c, d, dst_sub, x, y, z, s, src_sub, box);
}

void D3dCtxCopyResource(D3dContext *c, ID3D11Resource *dst, ID3D11Resource *src)
{
    D3dResource *d = D3dResourceFrom(dst), *s = D3dResourceFrom(src);
    if (!d || !s || d == s || d->dimension != s->dimension)
        return;
    if (d->dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
        copy_buffer(c, d, 0, s, nullptr);
        return;
    }
    UINT count = D3D_MIN(D3dSubresourceCount(d), D3dSubresourceCount(s));
    for (UINT i = 0; i < count; i++)
        copy_texture(c, d, i, 0, 0, 0, s, i, nullptr);
}

void D3dCtxCopyStructureCount(D3dContext *c, ID3D11Buffer *dst, UINT offset, ID3D11UnorderedAccessView *src)
{
    D3dResource *d = D3dResourceFrom((ID3D11Resource *)dst);
    D3dView *v = D3dViewFrom(src, D3D_VIEW_UAV);
    if (!d || !v || !v->counter.block || offset + 4 > d->buf.size)
        return;
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    VkBufferCopy copy = {v->counter.offset, d->buf.offset + offset, 4};
    c->child.device->vk.vkCmdCopyBuffer(cmd, v->counter.buffer, d->buf.buffer, 1, &copy);
    c->pending_barrier = TRUE;
    D3dContextUse(c, d);
}

/* ------------------------------------------------------------------ clears */

/* a clear color as the view's format takes it: integer formats get the floats cast. */
static VkClearColorValue clear_color(const D3dView *view, const FLOAT color[4])
{
    VkClearColorValue value;
    DXGI_FORMAT format = view->kind == D3D_VIEW_RTV ? view->desc.rtv.Format : view->desc.uav.Format;
    const D3dFormat *f = D3dGetFormat(format);
    if (format == DXGI_FORMAT_A8_UNORM) {
        value.float32[0] = color[3];
        value.float32[1] = value.float32[2] = value.float32[3] = 0;
    } else if (f->flags & D3D_FORMAT_INT) {
        for (UINT i = 0; i < 4; i++)
            value.int32[i] = (INT)color[i];
    } else {
        memcpy(value.float32, color, sizeof(value.float32));
    }
    return value;
}

/* runs a rendering of the single view that only clears it, rects or all of it. */
static void clear_attachment(D3dContext *c, D3dView *view, const VkClearValue *value, VkImageAspectFlags aspects,
                             const D3D11_RECT *rects, UINT rect_count)
{
    D3dDevice *device = c->child.device;
    D3dVk *vk = &device->vk;
    /* a target bound to the open rendering clears inside it. */
    BOOL bound = FALSE;
    UINT index = 0;
    if (c->rendering && !c->targets_dirty) {
        for (UINT i = 0; i < c->rtv_count; i++)
            if (c->rtv[i] == view)
                bound = TRUE, index = i;
        if (c->dsv == view)
            bound = TRUE;
    }
    VkCommandBuffer cmd = bound ? c->cmd->buffer : D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    VkClearRect clear_rects[16];
    UINT count = rects ? D3D_MIN(rect_count, 16) : 1;
    for (UINT i = 0; i < count; i++) {
        VkRect2D r = {{0, 0}, {view->extent.width, view->extent.height}};
        if (rects) {
            LONG left = D3D_MAX(rects[i].left, 0), top = D3D_MAX(rects[i].top, 0);
            LONG right = D3D_MIN(rects[i].right, (LONG)view->extent.width);
            LONG bottom = D3D_MIN(rects[i].bottom, (LONG)view->extent.height);
            if (right <= left || bottom <= top) {
                r.extent.width = r.extent.height = 0;
            } else {
                r.offset = (VkOffset2D){left, top};
                r.extent = (VkExtent2D){right - left, bottom - top};
            }
        }
        clear_rects[i] = (VkClearRect){r, 0, view->range.layerCount};
    }
    VkClearAttachment attachment = {aspects, index, *value};
    if (bound) {
        vk->vkCmdClearAttachments(cmd, 1, &attachment, count, clear_rects);
        return;
    }
    VkRenderingAttachmentInfo info = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    info.imageView = view->image_view;
    info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = (VkExtent2D){view->extent.width, view->extent.height};
    rendering.layerCount = view->range.layerCount;
    if (view->kind == D3D_VIEW_RTV) {
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &info;
    } else {
        if (view->range.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
            rendering.pDepthAttachment = &info;
        if (view->range.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
            rendering.pStencilAttachment = &info;
    }
    vk->vkCmdBeginRendering(cmd, &rendering);
    vk->vkCmdClearAttachments(cmd, 1, &attachment, count, clear_rects);
    vk->vkCmdEndRendering(cmd);
    c->pending_barrier = TRUE;
    D3dContextUse(c, view->resource);
}

void D3dCtxClearRenderTargetView(D3dContext *c, ID3D11RenderTargetView *iface, const FLOAT color[4])
{
    D3dView *view = D3dViewFrom(iface, D3D_VIEW_RTV);
    if (!view || !color)
        return;
    VkClearValue value;
    value.color = clear_color(view, color);
    clear_attachment(c, view, &value, VK_IMAGE_ASPECT_COLOR_BIT, nullptr, 0);
}

void D3dCtxClearDepthStencilView(D3dContext *c, ID3D11DepthStencilView *iface, UINT flags, FLOAT depth,
                                 UINT8 stencil)
{
    D3dCtxClearDepthStencilRects(c, iface, flags, depth, stencil, nullptr, 0);
}

/* d3d9.dll's clears of depth buffers, which its viewport and rectangles bound. */
void D3dCtxClearDepthStencilRects(D3dContext *c, ID3D11DepthStencilView *iface, UINT flags, FLOAT depth,
                                  UINT8 stencil, const D3D11_RECT *rects, UINT count)
{
    D3dView *view = D3dViewFrom(iface, D3D_VIEW_DSV);
    if (!view)
        return;
    VkImageAspectFlags aspects = 0;
    if (flags & D3D11_CLEAR_DEPTH)
        aspects |= view->range.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT;
    if (flags & D3D11_CLEAR_STENCIL)
        aspects |= view->range.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT;
    if (!aspects)
        return;
    VkClearValue value;
    value.depthStencil.depth = D3D_MAX(0.0f, D3D_MIN(depth, 1.0f));
    value.depthStencil.stencil = stencil;
    clear_attachment(c, view, &value, aspects, rects, count);
}

/* a range of a buffer filled with an element repeated: a copy of it from the ring, past 4 bytes. */
static void fill_pattern(D3dContext *c, VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
                         const BYTE *element, UINT element_size)
{
    D3dDevice *device = c->child.device;
    if (element_size == 4) {
        UINT word;
        memcpy(&word, element, 4);
        device->vk.vkCmdFillBuffer(cmd, buffer, offset, size & ~3ull, word);
        return;
    }
    VkDeviceSize chunk = D3D_MIN(size, 65536 / element_size * element_size);
    D3dRingSlice slice;
    if (!chunk || !D3dContextRingAlloc(c, chunk, 16, &slice))
        return;
    for (VkDeviceSize at = 0; at < chunk; at += element_size)
        memcpy(slice.mapped + at, element, element_size);
    VkBufferCopy regions[64];
    UINT n = 0;
    for (VkDeviceSize at = 0; at < size; at += chunk) {
        regions[n++] = (VkBufferCopy){slice.offset, offset + at, D3D_MIN(chunk, size - at)};
        if (n == RTL_NUMBER_OF(regions) || at + chunk >= size) {
            device->vk.vkCmdCopyBuffer(cmd, slice.buffer, buffer, n, regions);
            n = 0;
        }
    }
}

/*
 * clears of unordered access views. uints go to each channel as their low
 * bits, whatever the format; floats are converted, on float and normalized
 * formats only. raw and structured buffers take the first uint. a texture
 * whose view has another format than the image clears to the value that
 * writes the view's bits in the image's format.
 */
static void clear_uav(D3dContext *c, D3dView *view, const UINT *uints, const FLOAT *floats)
{
    D3dDevice *device = c->child.device;
    D3dResource *r = view->resource;
    BOOL raw = view->desc.uav.ViewDimension == D3D11_UAV_DIMENSION_BUFFER &&
               ((view->desc.uav.Buffer.Flags & D3D11_BUFFER_UAV_FLAG_RAW) || (r->misc & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED));
    BYTE element[16];
    UINT size = 4;
    if (raw) {
        if (!uints)
            return;
        memcpy(element, uints, 4);
    } else if (!D3dPackClear(view->format, uints, floats, element, &size)) {
        return;
    }
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    if (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
        D3dViewRefresh(view);
        fill_pattern(c, cmd, r->buf.buffer, r->buf.offset + view->buffer_offset, view->buffer_size, element, size);
    } else {
        VkClearColorValue value;
        if (!D3dClearValueOfBits(D3dGetFormat(r->tex.format)->vk, element, &value))
            return;
        device->vk.vkCmdClearColorImage(cmd, r->tex.image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &view->range);
    }
    c->pending_barrier = TRUE;
    D3dContextUse(c, r);
}

void D3dCtxClearUnorderedAccessViewUint(D3dContext *c, ID3D11UnorderedAccessView *iface, const UINT values[4])
{
    D3dView *view = D3dViewFrom(iface, D3D_VIEW_UAV);
    if (view && values)
        clear_uav(c, view, values, nullptr);
}

void D3dCtxClearUnorderedAccessViewFloat(D3dContext *c, ID3D11UnorderedAccessView *iface, const FLOAT values[4])
{
    D3dView *view = D3dViewFrom(iface, D3D_VIEW_UAV);
    if (view && values)
        clear_uav(c, view, nullptr, values);
}

/*
 * ClearView of unordered access views: the color converted to the view's
 * format, integer formats taking it as integers. rectangles are element
 * ranges of buffers; on textures each row of one is a copy of a row of
 * the element repeated, which writes the view's bits whatever the image's
 * format.
 */
static void clear_uav_view(D3dContext *c, D3dView *view, const FLOAT color[4], const D3D11_RECT *rects, UINT count)
{
    D3dDevice *device = c->child.device;
    D3dResource *r = view->resource;
    const D3dFormat *f = D3dGetFormat(view->desc.uav.Format);
    UINT uints[4];
    for (UINT i = 0; i < 4; i++)
        uints[i] = f->flags & D3D_FORMAT_INT ? (UINT)(INT)color[i] : 0;
    if (!count) {
        clear_uav(c, view, f->flags & D3D_FORMAT_INT ? uints : nullptr, f->flags & D3D_FORMAT_INT ? nullptr : color);
        return;
    }
    BYTE element[16];
    UINT size;
    if (!D3dPackClear(view->format, f->flags & D3D_FORMAT_INT ? uints : nullptr, f->flags & D3D_FORMAT_INT ? nullptr : color,
                      element, &size))
        return;
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    if (r->dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
        D3dViewRefresh(view);
        UINT elements = view->buffer_size / size;
        for (UINT i = 0; i < count; i++) {
            UINT left = (UINT)D3D_MAX(rects[i].left, 0), right = D3D_MIN((UINT)D3D_MAX(rects[i].right, 0), elements);
            if (left < right)
                fill_pattern(c, cmd, r->buf.buffer, r->buf.offset + view->buffer_offset + (VkDeviceSize)left * size,
                             (VkDeviceSize)(right - left) * size, element, size);
        }
    } else {
        VkExtent3D extent = D3dMipExtent(r, view->range.baseMipLevel);
        for (UINT i = 0; i < count; i++) {
            UINT left = (UINT)D3D_MAX(rects[i].left, 0), top = (UINT)D3D_MAX(rects[i].top, 0);
            UINT right = D3D_MIN((UINT)D3D_MAX(rects[i].right, 0), extent.width);
            UINT bottom = D3D_MIN((UINT)D3D_MAX(rects[i].bottom, 0), extent.height);
            if (left >= right || top >= bottom)
                continue;
            D3dRingSlice row;
            if (!D3dContextRingAlloc(c, (VkDeviceSize)(right - left) * size, 16, &row))
                return;
            for (UINT x = 0; x < right - left; x++)
                memcpy(row.mapped + x * size, element, size);
            VkBufferImageCopy regions[64];
            UINT n = 0;
            for (UINT y = top; y < bottom; y++) {
                VkBufferImageCopy *copy = &regions[n++];
                *copy = (VkBufferImageCopy){row.offset, 0, 0};
                copy->imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, view->range.baseMipLevel,
                                                                    view->range.baseArrayLayer, view->range.layerCount};
                copy->imageOffset = (VkOffset3D){(INT)left, (INT)y, 0};
                copy->imageExtent = (VkExtent3D){right - left, 1, 1};
                if (n == RTL_NUMBER_OF(regions) || y + 1 == bottom) {
                    device->vk.vkCmdCopyBufferToImage(cmd, row.buffer, r->tex.image, VK_IMAGE_LAYOUT_GENERAL, n,
                                                      regions);
                    n = 0;
                }
            }
        }
    }
    c->pending_barrier = TRUE;
    D3dContextUse(c, r);
}

/* ClearView of render targets and unordered access views, whole or in rectangles. */
void D3dCtxClearView(D3dContext *c, ID3D11View *iface, const FLOAT color[4], const D3D11_RECT *rects, UINT count)
{
    if (!color)
        return;
    D3dView *view = D3dViewFrom(iface, D3D_VIEW_RTV);
    if (view) {
        VkClearValue value;
        value.color = clear_color(view, color);
        clear_attachment(c, view, &value, VK_IMAGE_ASPECT_COLOR_BIT, count ? rects : nullptr, count);
        return;
    }
    if ((view = D3dViewFrom(iface, D3D_VIEW_UAV)))
        clear_uav_view(c, view, color, count ? rects : nullptr, count);
}


/* ------------------------------------------------------------------ mips and resolves */

/* each mip of the view from the one above with a linear blit. */
void D3dCtxGenerateMips(D3dContext *c, ID3D11ShaderResourceView *iface)
{
    D3dView *view = D3dViewFrom(iface, D3D_VIEW_SRV);
    if (!view || view->resource->dimension == D3D11_RESOURCE_DIMENSION_BUFFER ||
        !(view->resource->misc & D3D11_RESOURCE_MISC_GENERATE_MIPS))
        return;
    D3dDevice *device = c->child.device;
    D3dResource *r = view->resource;
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    UINT first = view->range.baseMipLevel, count = view->range.levelCount;
    for (UINT mip = first + 1; mip < first + count; mip++) {
        VkExtent3D s = D3dMipExtent(r, mip - 1), d = D3dMipExtent(r, mip);
        VkImageBlit blit = {};
        blit.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, mip - 1,
                                                         view->range.baseArrayLayer, view->range.layerCount};
        blit.srcOffsets[1] = (VkOffset3D){s.width, s.height, s.depth};
        blit.dstSubresource = blit.srcSubresource;
        blit.dstSubresource.mipLevel = mip;
        blit.dstOffsets[1] = (VkOffset3D){d.width, d.height, d.depth};
        device->vk.vkCmdBlitImage(cmd, r->tex.image, VK_IMAGE_LAYOUT_GENERAL, r->tex.image, VK_IMAGE_LAYOUT_GENERAL, 1,
                                  &blit, VK_FILTER_LINEAR);
        D3dBarrier(cmd, device);
    }
    c->pending_barrier = TRUE;
    D3dContextUse(c, r);
}

void D3dCtxResolveSubresource(D3dContext *c, ID3D11Resource *dst, UINT dst_sub, ID3D11Resource *src, UINT src_sub,
                              DXGI_FORMAT format)
{
    D3dResource *d = D3dResourceFrom(dst), *s = D3dResourceFrom(src);
    if (!d || !s || !d->tex.image || !s->tex.image || dst_sub >= D3dSubresourceCount(d) ||
        src_sub >= D3dSubresourceCount(s))
        return;
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    UINT dm, dl, sm, sl;
    D3dSubresourceDecompose(d, dst_sub, &dm, &dl);
    D3dSubresourceDecompose(s, src_sub, &sm, &sl);
    VkImageResolve resolve = {{VK_IMAGE_ASPECT_COLOR_BIT, sm, sl, 1}, {0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, dm, dl, 1},
                              {0, 0, 0}, D3dMipExtent(d, dm)};
    if (s->tex.samples > 1)
        c->child.device->vk.vkCmdResolveImage(cmd, s->tex.image, VK_IMAGE_LAYOUT_GENERAL, d->tex.image,
                                              VK_IMAGE_LAYOUT_GENERAL, 1, &resolve);
    else
        copy_texture(c, d, dst_sub, 0, 0, 0, s, src_sub, nullptr);
    c->pending_barrier = TRUE;
    D3dContextUse(c, s);
    D3dContextUse(c, d);
}

/*
 * d3d9.dll's StretchRect and presents: a rectangle of a subresource to a
 * rectangle of another, scaled and converted by a blit, multisampled
 * sources of the same size resolved.
 */
void D3dCtxBlit(D3dContext *c, ID3D11Resource *dst, UINT dst_sub, const RECT *dst_rect, ID3D11Resource *src,
                UINT src_sub, const RECT *src_rect, BOOL linear)
{
    D3dResource *d = D3dResourceFrom(dst), *s = D3dResourceFrom(src);
    if (!d || !s || !d->tex.image || !s->tex.image || dst_sub >= D3dSubresourceCount(d) ||
        src_sub >= D3dSubresourceCount(s))
        return;
    UINT dm, dl, sm, sl;
    D3dSubresourceDecompose(d, dst_sub, &dm, &dl);
    D3dSubresourceDecompose(s, src_sub, &sm, &sl);
    VkExtent3D se = D3dMipExtent(s, sm), de = D3dMipExtent(d, dm);
    RECT sr = src_rect ? *src_rect : (RECT){0, 0, (LONG)se.width, (LONG)se.height};
    RECT dr = dst_rect ? *dst_rect : (RECT){0, 0, (LONG)de.width, (LONG)de.height};
    if (sr.right <= sr.left || sr.bottom <= sr.top || dr.right <= dr.left || dr.bottom <= dr.top)
        return;
    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return;
    D3dVk *vk = &c->child.device->vk;
    VkImageAspectFlags aspect = s->tex.aspect & d->tex.aspect;
    if (!aspect)
        aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageSubresourceLayers sl_ = {aspect, sm, sl, 1}, dl_ = {aspect, dm, dl, 1};
    BOOL same = sr.right - sr.left == dr.right - dr.left && sr.bottom - sr.top == dr.bottom - dr.top;
    if (s->tex.samples > 1 && same && aspect == VK_IMAGE_ASPECT_COLOR_BIT) {
        VkImageResolve resolve = {sl_, {sr.left, sr.top, 0}, dl_, {dr.left, dr.top, 0},
                                  {(UINT)(sr.right - sr.left), (UINT)(sr.bottom - sr.top), 1}};
        vk->vkCmdResolveImage(cmd, s->tex.image, VK_IMAGE_LAYOUT_GENERAL, d->tex.image, VK_IMAGE_LAYOUT_GENERAL, 1,
                              &resolve);
    } else if (same && s->tex.fmt->vk == d->tex.fmt->vk && s->tex.samples == d->tex.samples) {
        VkImageCopy copy = {sl_, {sr.left, sr.top, 0}, dl_, {dr.left, dr.top, 0},
                            {(UINT)(sr.right - sr.left), (UINT)(sr.bottom - sr.top), 1}};
        vk->vkCmdCopyImage(cmd, s->tex.image, VK_IMAGE_LAYOUT_GENERAL, d->tex.image, VK_IMAGE_LAYOUT_GENERAL, 1,
                           &copy);
    } else if (s->tex.samples <= 1 && d->tex.samples <= 1) {
        VkImageBlit blit = {sl_, {{sr.left, sr.top, 0}, {sr.right, sr.bottom, 1}}, dl_,
                            {{dr.left, dr.top, 0}, {dr.right, dr.bottom, 1}}};
        vk->vkCmdBlitImage(cmd, s->tex.image, VK_IMAGE_LAYOUT_GENERAL, d->tex.image, VK_IMAGE_LAYOUT_GENERAL, 1, &blit,
                           linear && aspect == VK_IMAGE_ASPECT_COLOR_BIT ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    } else {
        D3dLog("d3d11: no blit between multisampled images of other sizes");
    }
    c->pending_barrier = TRUE;
    D3dContextUse(c, s);
    D3dContextUse(c, d);
}
