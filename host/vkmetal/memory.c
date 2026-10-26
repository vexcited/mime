/*
 * memory and the resources bound to it. device memory is a private placement heap with one buffer over all of
 * it: buffers are ranges of that buffer, images are textures placed in the heap. host memory is a shared
 * buffer, or guest memory imported without a copy; linear images are textures over it.
 */
#include "vkm_cmd.h"

#define BUFFER_ALIGN 256

static const NSUInteger g_untracked = MTLResourceHazardTrackingModeUntracked;

/* ------------------------------------------------------------------ memory. */

static id heap_buffer(id heap, uint64_t size, NSUInteger options)
{
    return MTL_MSG(id (*)(id, SEL, NSUInteger, NSUInteger, NSUInteger), heap, "newBufferWithLength:options:offset:",
                   size, options, 0);
}

static VKAPI_ATTR VkResult VKAPI_CALL AllocateMemory(VkDevice device, const VkMemoryAllocateInfo *info,
                                                     const VkAllocationCallbacks *a, VkDeviceMemory *out)
{
    if (info->memoryTypeIndex >= VKM_MEMORY_TYPES)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    VkDeviceMemory m = vkm_zalloc(sizeof(*m));
    if (!m)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    void *pool = objc_autoreleasePoolPush();
    const VkImportMemoryHostPointerInfoEXT *host =
        vkm_find(info->pNext, VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT);
    const VkImportMemoryMetalHandleInfoEXT *metal =
        vkm_find(info->pNext, VK_STRUCTURE_TYPE_IMPORT_MEMORY_METAL_HANDLE_INFO_EXT);
    const VkExportMemoryAllocateInfo *export = vkm_find(info->pNext, VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO);
    uint64_t size = MIME_MAX(info->allocationSize, (VkDeviceSize)16);
    m->type = info->memoryTypeIndex;
    m->exported = export && export->handleTypes;
    m->size = size;
    NSUInteger shared = MTLResourceStorageModeShared | g_untracked;
    NSUInteger private = MTLResourceStorageModePrivate | g_untracked;
    if (host && host->handleType) {
        m->buffer = MTL_MSG(id (*)(id, SEL, void *, NSUInteger, NSUInteger, id), device->mtl,
                            "newBufferWithBytesNoCopy:length:options:deallocator:", host->pHostPointer, size, shared,
                            nullptr);
        m->map = host->pHostPointer;
    } else if (metal && metal->handle) {
        /* another process's image comes as its texture, which the image bound here takes. */
        if (metal->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLTEXTURE_BIT_EXT) {
            m->texture = mtl_retain((id)metal->handle);
            m->heap = mtl_retain(M_ID(m->texture, "heap"));
        } else {
            m->heap = mtl_retain((id)metal->handle);
        }
        if (m->heap) {
            m->size = size = M_U64(m->heap, "size");
            bool cpu = M_U(m->heap, "storageMode") == MTLStorageModeShared;
            m->buffer = heap_buffer(m->heap, size, cpu ? shared : private);
            m->map = cpu && m->buffer ? M_PTR(m->buffer, "contents") : nullptr;
        }
    } else if (m->type == VKM_MEMORY_HOST) {
        m->buffer = MTL_MSG(id (*)(id, SEL, NSUInteger, NSUInteger), device->mtl, "newBufferWithLength:options:", size,
                            shared);
        m->map = m->buffer ? M_PTR(m->buffer, "contents") : nullptr;
    } else {
        MTLSizeAndAlign sa = MTL_MSG(MTLSizeAndAlign (*)(id, SEL, NSUInteger, NSUInteger), device->mtl,
                                     "heapBufferSizeAndAlignWithLength:options:", size, private);
        id desc = mtl_new("MTLHeapDescriptor");
        M_V_I(desc, "setType:", MTLHeapTypePlacement);
        M_V_U(desc, "setStorageMode:", MTLStorageModePrivate);
        M_V_U(desc, "setHazardTrackingMode:", MTLHazardTrackingModeUntracked);
        M_V_U(desc, "setSize:", MIME_ALIGN_UP(MIME_MAX(sa.size, size), (uint64_t)MIME_MAX(sa.align, 16384)));
        m->heap = M_ID_ID(device->mtl, "newHeapWithDescriptor:", desc);
        mtl_release(desc);
        if (m->heap) {
            m->size = size = M_U64(m->heap, "size");
            m->buffer = heap_buffer(m->heap, size, private);
        }
    }
    objc_autoreleasePoolPop(pool);
    if (!m->buffer && !m->texture) {
        mtl_release(m->heap);
        free(m);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    m->gpu = m->buffer ? M_U64(m->buffer, "gpuAddress") : 0;
    vkm_resident(device, m->heap ? m->heap : m->buffer ? m->buffer : m->texture);
    *out = m;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL FreeMemory(VkDevice device, VkDeviceMemory m, const VkAllocationCallbacks *a)
{
    if (!m)
        return;
    id resident = m->heap ? m->heap : m->buffer ? m->buffer : m->texture;
    vkm_release_later(device, m->texture, m->texture == resident);
    vkm_release_later(device, m->buffer, m->buffer == resident);
    vkm_release_later(device, m->heap, m->heap == resident);
    free(m);
}

static VKAPI_ATTR VkResult VKAPI_CALL MapMemory(VkDevice device, VkDeviceMemory m, VkDeviceSize offset,
                                                VkDeviceSize size, VkMemoryMapFlags flags, void **out)
{
    if (!m->map)
        return VK_ERROR_MEMORY_MAP_FAILED;
    *out = m->map + offset;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL UnmapMemory(VkDevice device, VkDeviceMemory m)
{
}

static VKAPI_ATTR VkResult VKAPI_CALL FlushMappedMemoryRanges(VkDevice device, uint32_t count,
                                                              const VkMappedMemoryRange *ranges)
{
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory m, VkDeviceSize *out)
{
    *out = m->size;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetMemoryHostPointerPropertiesEXT(VkDevice device,
                                                                        VkExternalMemoryHandleTypeFlagBits type,
                                                                        const void *pointer,
                                                                        VkMemoryHostPointerPropertiesEXT *out)
{
    if (type != VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT)
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    out->memoryTypeBits = 1u << VKM_MEMORY_HOST;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetMemoryMetalHandleEXT(VkDevice device,
                                                              const VkMemoryGetMetalHandleInfoEXT *info, void **out)
{
    VkDeviceMemory m = info->memory;
    id handle = info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLTEXTURE_BIT_EXT ? m->texture
                : info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT  ? m->heap
                                                                                       : nullptr;
    if (!handle)
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    *out = handle;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetMemoryMetalHandlePropertiesEXT(VkDevice device,
                                                                        VkExternalMemoryHandleTypeFlagBits type,
                                                                        const void *handle,
                                                                        VkMemoryMetalHandlePropertiesEXT *out)
{
    if (type != VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT && type != VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLTEXTURE_BIT_EXT)
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    out->memoryTypeBits = 1u << VKM_MEMORY_DEVICE;
    return VK_SUCCESS;
}

static VKAPI_ATTR uint64_t VKAPI_CALL GetDeviceMemoryOpaqueCaptureAddress(
    VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo *info)
{
    return 0;
}

/* ------------------------------------------------------------------ buffers. */

static VKAPI_ATTR VkResult VKAPI_CALL CreateBuffer(VkDevice device, const VkBufferCreateInfo *info,
                                                   const VkAllocationCallbacks *a, VkBuffer *out)
{
    VkBuffer b = vkm_zalloc(sizeof(*b));
    if (!b)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    const VkBufferUsageFlags2CreateInfo *usage2 = vkm_find(info->pNext, VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO);
    b->size = info->size;
    b->usage = usage2 ? usage2->usage : info->usage;
    *out = b;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyBuffer(VkDevice device, VkBuffer b, const VkAllocationCallbacks *a)
{
    free(b);
}

static void buffer_requirements(uint64_t size, VkMemoryRequirements *out)
{
    out->size = MIME_ALIGN_UP(MIME_MAX(size, (uint64_t)1), (uint64_t)16);
    out->alignment = BUFFER_ALIGN;
    out->memoryTypeBits = (1u << VKM_MEMORY_TYPES) - 1;
}

static void dedicated(void *chain, bool prefer)
{
    VkMemoryDedicatedRequirements *d = (void *)vkm_find(chain, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS);
    if (d)
        d->prefersDedicatedAllocation = d->requiresDedicatedAllocation = prefer && false;
}

static VKAPI_ATTR void VKAPI_CALL GetBufferMemoryRequirements(VkDevice device, VkBuffer b, VkMemoryRequirements *out)
{
    buffer_requirements(b->size, out);
}

static VKAPI_ATTR void VKAPI_CALL GetBufferMemoryRequirements2(VkDevice device,
                                                              const VkBufferMemoryRequirementsInfo2 *info,
                                                              VkMemoryRequirements2 *out)
{
    buffer_requirements(info->buffer->size, &out->memoryRequirements);
    dedicated(out->pNext, false);
}

static VKAPI_ATTR void VKAPI_CALL GetDeviceBufferMemoryRequirements(VkDevice device,
                                                                   const VkDeviceBufferMemoryRequirements *info,
                                                                   VkMemoryRequirements2 *out)
{
    buffer_requirements(info->pCreateInfo->size, &out->memoryRequirements);
    dedicated(out->pNext, false);
}

static VKAPI_ATTR VkResult VKAPI_CALL BindBufferMemory(VkDevice device, VkBuffer b, VkDeviceMemory m,
                                                       VkDeviceSize offset)
{
    b->memory = m;
    b->offset = offset;
    b->gpu = m->gpu + offset;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL BindBufferMemory2(VkDevice device, uint32_t count,
                                                        const VkBindBufferMemoryInfo *infos)
{
    for (uint32_t i = 0; i < count; i++)
        BindBufferMemory(device, infos[i].buffer, infos[i].memory, infos[i].memoryOffset);
    return VK_SUCCESS;
}

static VKAPI_ATTR VkDeviceAddress VKAPI_CALL GetBufferDeviceAddress(VkDevice device,
                                                                   const VkBufferDeviceAddressInfo *info)
{
    return info->buffer->gpu;
}

static VKAPI_ATTR uint64_t VKAPI_CALL GetBufferOpaqueCaptureAddress(VkDevice device,
                                                                   const VkBufferDeviceAddressInfo *info)
{
    return 0;
}

/* ------------------------------------------------------------------ buffer views. */

static VKAPI_ATTR VkResult VKAPI_CALL CreateBufferView(VkDevice device, const VkBufferViewCreateInfo *info,
                                                       const VkAllocationCallbacks *a, VkBufferView *out)
{
    const VkmFormat *f = vkm_format(info->format);
    VkBuffer b = info->buffer;
    if (!f->pixel || !f->bytes || !b->memory)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkBufferView v = vkm_zalloc(sizeof(*v));
    if (!v)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    uint64_t range = info->range == VK_WHOLE_SIZE ? b->size - info->offset : info->range;
    uint64_t texels = range / f->bytes;
    void *pool = objc_autoreleasePoolPush();
    VkDeviceMemory m = b->memory;
    bool cpu = m->type == VKM_MEMORY_HOST;
    id desc = MTL_MSG(id (*)(id, SEL, NSUInteger, NSUInteger, NSUInteger, NSUInteger), MTL_CLASS("MTLTextureDescriptor"),
                      "textureBufferDescriptorWithPixelFormat:width:resourceOptions:usage:", f->pixel,
                      MIME_MAX(texels, (uint64_t)1),
                      (cpu ? MTLResourceStorageModeShared : MTLResourceStorageModePrivate) | g_untracked,
                      MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite |
                          ((f->caps & VKM_CAP_ATOMIC) ? MTLTextureUsageShaderAtomic : 0));
    v->texture = MTL_MSG(id (*)(id, SEL, id, NSUInteger, NSUInteger), m->buffer,
                         "newTextureWithDescriptor:offset:bytesPerRow:", desc, b->offset + info->offset,
                         MIME_ALIGN_UP(MIME_MAX(texels, (uint64_t)1) * f->bytes, (uint64_t)16));
    objc_autoreleasePoolPop(pool);
    if (!v->texture) {
        free(v);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    v->rid = M_RID(v->texture, "gpuResourceID");
    *out = v;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyBufferView(VkDevice device, VkBufferView v, const VkAllocationCallbacks *a)
{
    if (!v)
        return;
    vkm_release_later(device, v->texture, false);
    free(v);
}

/* ------------------------------------------------------------------ images. */

VkExtent3D vkm_level_extent(VkImage image, uint32_t level)
{
    return (VkExtent3D){MIME_MAX(image->extent.width >> level, 1u), MIME_MAX(image->extent.height >> level, 1u),
                        MIME_MAX(image->extent.depth >> level, 1u)};
}

static uint32_t image_texture_type(const VkImageCreateInfo *info)
{
    switch (info->imageType) {
    case VK_IMAGE_TYPE_1D: return info->arrayLayers > 1 ? MTLTextureType1DArray : MTLTextureType1D;
    case VK_IMAGE_TYPE_3D: return MTLTextureType3D;
    default:
        if (info->samples > 1)
            return info->arrayLayers > 1 ? MTLTextureType2DMultisampleArray : MTLTextureType2DMultisample;
        return info->arrayLayers > 1 || (info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) ? MTLTextureType2DArray
                                                                                           : MTLTextureType2D;
    }
}

static NSUInteger image_usage(const VkImageCreateInfo *info, const VkmFormat *f)
{
    NSUInteger u = 0;
    VkImageUsageFlags usage = info->usage;
    if (usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        u |= MTLTextureUsageShaderRead;
    if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
        u |= MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    /* clears and blits into an image render to it. */
    if ((usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) ||
        ((usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) && (f->caps & (VKM_CAP_COLOR | VKM_CAP_DEPTH))))
        u |= MTLTextureUsageRenderTarget;
    if ((info->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) || (f->aspects & VK_IMAGE_ASPECT_STENCIL_BIT))
        u |= MTLTextureUsagePixelFormatView;
    if ((usage & VK_IMAGE_USAGE_STORAGE_BIT) && (f->caps & VKM_CAP_ATOMIC))
        u |= MTLTextureUsageShaderAtomic;
    if (info->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) {
        /* a view in another format may need what this format cannot do. */
        u |= MTLTextureUsageShaderRead;
        if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
            u |= MTLTextureUsageShaderWrite;
    }
    return u ? u : MTLTextureUsageShaderRead;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateImage(VkDevice device, const VkImageCreateInfo *info,
                                                  const VkAllocationCallbacks *a, VkImage *out)
{
    const VkmFormat *f = vkm_format(info->format);
    if (!f->pixel)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkImage image = vkm_zalloc(sizeof(*image));
    if (!image)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    image->type = info->imageType;
    image->format = info->format;
    image->fi = f;
    image->extent = info->extent;
    image->levels = info->mipLevels;
    image->layers = info->arrayLayers;
    image->samples = info->samples;
    image->usage = info->usage;
    image->flags = info->flags;
    image->tiling = info->tiling;
    void *pool = objc_autoreleasePoolPush();
    bool linear = info->tiling == VK_IMAGE_TILING_LINEAR;
    id desc = mtl_new("MTLTextureDescriptor");
    M_V_U(desc, "setTextureType:", image_texture_type(info));
    M_V_U(desc, "setPixelFormat:", f->pixel);
    M_V_U(desc, "setWidth:", info->extent.width);
    M_V_U(desc, "setHeight:", info->extent.height);
    M_V_U(desc, "setDepth:", info->extent.depth);
    M_V_U(desc, "setMipmapLevelCount:", info->mipLevels);
    M_V_U(desc, "setArrayLength:", info->imageType == VK_IMAGE_TYPE_3D ? 1 : info->arrayLayers);
    M_V_U(desc, "setSampleCount:", info->samples);
    M_V_U(desc, "setUsage:", image_usage(info, f));
    M_V_U(desc, "setStorageMode:", linear ? MTLStorageModeShared : MTLStorageModePrivate);
    M_V_U(desc, "setHazardTrackingMode:", MTLHazardTrackingModeUntracked);
    image->desc = desc;
    if (linear) {
        NSUInteger align = MTL_MSG(NSUInteger (*)(id, SEL, NSUInteger), device->mtl,
                                   "minimumLinearTextureAlignmentForPixelFormat:", f->pixel);
        uint64_t row = (uint64_t)((info->extent.width + f->block_w - 1) / f->block_w) * f->bytes;
        image->row_pitch = MIME_ALIGN_UP(row, (uint64_t)MIME_MAX(align, 16));
        image->size = image->row_pitch * ((info->extent.height + f->block_h - 1) / f->block_h);
        image->align = MIME_MAX(align, 16);
    } else {
        MTLSizeAndAlign sa = MTL_MSG(MTLSizeAndAlign (*)(id, SEL, id), device->mtl,
                                     "heapTextureSizeAndAlignWithDescriptor:", desc);
        image->size = sa.size;
        image->align = sa.align;
    }
    objc_autoreleasePoolPop(pool);
    if (!image->size) {
        mtl_release(image->desc);
        free(image);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    *out = image;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *a)
{
    if (!image || image->swapchain)
        return;
    vkm_release_later(device, image->texture, false);
    mtl_release(image->desc);
    free(image);
}

static void image_requirements(VkImage image, VkMemoryRequirements *out)
{
    out->size = image->size;
    out->alignment = image->align;
    out->memoryTypeBits = 1u << (image->tiling == VK_IMAGE_TILING_LINEAR ? VKM_MEMORY_HOST : VKM_MEMORY_DEVICE);
}

static VKAPI_ATTR void VKAPI_CALL GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *out)
{
    image_requirements(image, out);
}

static VKAPI_ATTR void VKAPI_CALL GetImageMemoryRequirements2(VkDevice device,
                                                             const VkImageMemoryRequirementsInfo2 *info,
                                                             VkMemoryRequirements2 *out)
{
    image_requirements(info->image, &out->memoryRequirements);
    dedicated(out->pNext, false);
}

static VKAPI_ATTR void VKAPI_CALL GetDeviceImageMemoryRequirements(VkDevice device,
                                                                  const VkDeviceImageMemoryRequirements *info,
                                                                  VkMemoryRequirements2 *out)
{
    VkImage image;
    if (CreateImage(device, info->pCreateInfo, nullptr, &image) != VK_SUCCESS) {
        out->memoryRequirements = (VkMemoryRequirements){};
        return;
    }
    image_requirements(image, &out->memoryRequirements);
    dedicated(out->pNext, false);
    DestroyImage(device, image, nullptr);
}

static VKAPI_ATTR void VKAPI_CALL GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t *count,
                                                                  VkSparseImageMemoryRequirements *out)
{
    *count = 0;
}

static VKAPI_ATTR void VKAPI_CALL GetImageSparseMemoryRequirements2(VkDevice device,
                                                                   const VkImageSparseMemoryRequirementsInfo2 *info,
                                                                   uint32_t *count,
                                                                   VkSparseImageMemoryRequirements2 *out)
{
    *count = 0;
}

static VKAPI_ATTR void VKAPI_CALL GetDeviceImageSparseMemoryRequirements(VkDevice device,
                                                                        const VkDeviceImageMemoryRequirements *info,
                                                                        uint32_t *count,
                                                                        VkSparseImageMemoryRequirements2 *out)
{
    *count = 0;
}

static VkResult bind_image(VkDevice device, VkImage image, VkDeviceMemory m, VkDeviceSize offset)
{
    if (image->swapchain)
        return VK_SUCCESS;
    void *pool = objc_autoreleasePoolPush();
    mtl_release(image->texture);
    /* an imported image is the exporter's texture; any other image gets its own, even where one was before. */
    bool imported = m->texture && !m->exported;
    if (imported && !offset) {
        image->texture = mtl_retain(m->texture);
        if (M_U(m->texture, "width") != image->extent.width || M_U(m->texture, "height") != image->extent.height ||
            M_U(m->texture, "pixelFormat") != image->fi->pixel)
            VKM_ERROR("an image of %ux%u format %u is bound to an imported texture of %lux%lu format %lu",
                      image->extent.width, image->extent.height, image->format, M_U(m->texture, "width"),
                      M_U(m->texture, "height"), M_U(m->texture, "pixelFormat"));
    } else if (image->tiling == VK_IMAGE_TILING_LINEAR)
        image->texture = MTL_MSG(id (*)(id, SEL, id, NSUInteger, NSUInteger), m->buffer,
                                 "newTextureWithDescriptor:offset:bytesPerRow:", image->desc, offset, image->row_pitch);
    else if (m->heap)
        image->texture = MTL_MSG(id (*)(id, SEL, id, NSUInteger), m->heap, "newTextureWithDescriptor:offset:",
                                 image->desc, offset);
    else
        image->texture = nullptr;
    objc_autoreleasePoolPop(pool);
    image->memory = m;
    image->offset = offset;
    if (!image->texture) {
        VKM_ERROR("no texture for an image of format %u in memory of type %u", image->format, m->type);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    if (m->exported && !offset && !m->texture)
        m->texture = mtl_retain(image->texture);
    if (m->heap && !imported && image->tiling != VK_IMAGE_TILING_LINEAR)
        vkm_image_init(device, image);
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory m,
                                                      VkDeviceSize offset)
{
    return bind_image(device, image, m, offset);
}

static VKAPI_ATTR VkResult VKAPI_CALL BindImageMemory2(VkDevice device, uint32_t count,
                                                       const VkBindImageMemoryInfo *infos)
{
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < count; i++) {
        VkResult r = bind_image(device, infos[i].image, infos[i].memory, infos[i].memoryOffset);
        VkBindMemoryStatus *status = (void *)vkm_find(infos[i].pNext, VK_STRUCTURE_TYPE_BIND_MEMORY_STATUS);
        if (status && status->pResult)
            *status->pResult = r;
        if (r != VK_SUCCESS)
            result = r;
    }
    return result;
}

static VKAPI_ATTR void VKAPI_CALL GetImageSubresourceLayout(VkDevice device, VkImage image,
                                                           const VkImageSubresource *sub, VkSubresourceLayout *out)
{
    *out = (VkSubresourceLayout){0, image->size, image->row_pitch, image->size, image->size};
}

/* ------------------------------------------------------------------ image views. */

static uint8_t swizzle(VkComponentSwizzle s, uint8_t identity)
{
    switch (s) {
    case VK_COMPONENT_SWIZZLE_ZERO: return MTLTextureSwizzleZero;
    case VK_COMPONENT_SWIZZLE_ONE: return MTLTextureSwizzleOne;
    case VK_COMPONENT_SWIZZLE_R: return MTLTextureSwizzleRed;
    case VK_COMPONENT_SWIZZLE_G: return MTLTextureSwizzleGreen;
    case VK_COMPONENT_SWIZZLE_B: return MTLTextureSwizzleBlue;
    case VK_COMPONENT_SWIZZLE_A: return MTLTextureSwizzleAlpha;
    default: return identity;
    }
}

static id texture_view(id texture, uint32_t format, uint32_t type, uint32_t level, uint32_t levels, uint32_t layer,
                       uint32_t layers, MTLTextureSwizzleChannels sw)
{
    return MTL_MSG(id (*)(id, SEL, NSUInteger, NSUInteger, NSRange, NSRange, MTLTextureSwizzleChannels), texture,
                   "newTextureViewWithPixelFormat:textureType:levels:slices:swizzle:", format, type,
                   (NSRange){level, levels}, (NSRange){layer, layers}, sw);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateImageView(VkDevice device, const VkImageViewCreateInfo *info,
                                                      const VkAllocationCallbacks *a, VkImageView *out)
{
    VkImage image = info->image;
    if (!image->texture)
        return VK_ERROR_INITIALIZATION_FAILED;
    const VkmFormat *f = vkm_format(info->format);
    if (!f->pixel)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkImageView v = vkm_zalloc(sizeof(*v));
    if (!v)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    const VkImageSubresourceRange *r = &info->subresourceRange;
    v->image = image;
    v->type = info->viewType;
    v->format = info->format;
    v->fi = f;
    v->aspects = r->aspectMask;
    v->base_level = r->baseMipLevel;
    v->levels = r->levelCount == VK_REMAINING_MIP_LEVELS ? image->levels - r->baseMipLevel : r->levelCount;
    v->base_layer = r->baseArrayLayer;
    v->layers = r->layerCount == VK_REMAINING_ARRAY_LAYERS ? image->layers - r->baseArrayLayer : r->layerCount;
    const VkImageViewUsageCreateInfo *usage = vkm_find(info->pNext, VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO);
    v->usage = usage ? usage->usage : image->usage;
    v->extent = vkm_level_extent(image, v->base_level);
    void *pool = objc_autoreleasePoolPush();
    uint32_t format = f->pixel;
    if (image->fi->aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
        format = vkm_aspect_format(image->fi, r->aspectMask);
    bool is3d = image->type == VK_IMAGE_TYPE_3D;
    uint32_t type = vkm_texture_type(info->viewType, image->samples);
    if (v->usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
        MTLTextureSwizzleChannels sw = {
            swizzle(info->components.r, MTLTextureSwizzleRed), swizzle(info->components.g, MTLTextureSwizzleGreen),
            swizzle(info->components.b, MTLTextureSwizzleBlue), swizzle(info->components.a, MTLTextureSwizzleAlpha)};
        /* storage textures take no swizzle. */
        if (v->usage & VK_IMAGE_USAGE_STORAGE_BIT)
            sw = (MTLTextureSwizzleChannels){MTLTextureSwizzleRed, MTLTextureSwizzleGreen, MTLTextureSwizzleBlue,
                                             MTLTextureSwizzleAlpha};
        v->texture = texture_view(image->texture, format, type, v->base_level, v->levels, is3d ? 0 : v->base_layer,
                                  is3d ? 1 : v->layers, sw);
        if (v->texture)
            v->rid = M_RID(v->texture, "gpuResourceID");
    }
    if (v->usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        v->render_level = v->base_level;
        v->render_slice = v->base_layer;
        if (format == image->fi->pixel || (image->fi->aspects & VK_IMAGE_ASPECT_STENCIL_BIT)) {
            v->render = mtl_retain(image->texture);
        } else {
            uint32_t base_type = M_U(image->texture, "textureType");
            v->render = texture_view(image->texture, format, base_type, 0, image->levels, 0,
                                     is3d ? 1 : image->layers,
                                     (MTLTextureSwizzleChannels){MTLTextureSwizzleRed, MTLTextureSwizzleGreen,
                                                                 MTLTextureSwizzleBlue, MTLTextureSwizzleAlpha});
        }
    }
    objc_autoreleasePoolPop(pool);
    if ((v->usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) &&
        !v->texture) {
        VKM_ERROR("no view of format %u type %u on an image of format %u", info->format, info->viewType,
                  image->format);
        mtl_release(v->render);
        free(v);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    *out = v;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyImageView(VkDevice device, VkImageView v, const VkAllocationCallbacks *a)
{
    if (!v)
        return;
    vkm_release_later(device, v->texture, false);
    vkm_release_later(device, v->render, false);
    free(v);
}

/* ------------------------------------------------------------------ samplers. */

static uint32_t address_mode(VkSamplerAddressMode m)
{
    switch (m) {
    case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return MTLSamplerAddressModeMirrorRepeat;
    case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return MTLSamplerAddressModeClampToEdge;
    case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER: return MTLSamplerAddressModeClampToBorderColor;
    case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE: return MTLSamplerAddressModeMirrorClampToEdge;
    default: return MTLSamplerAddressModeRepeat;
    }
}

uint32_t vkm_compare(VkCompareOp op)
{
    switch (op) {
    case VK_COMPARE_OP_NEVER: return MTLCompareFunctionNever;
    case VK_COMPARE_OP_LESS: return MTLCompareFunctionLess;
    case VK_COMPARE_OP_EQUAL: return MTLCompareFunctionEqual;
    case VK_COMPARE_OP_LESS_OR_EQUAL: return MTLCompareFunctionLessEqual;
    case VK_COMPARE_OP_GREATER: return MTLCompareFunctionGreater;
    case VK_COMPARE_OP_NOT_EQUAL: return MTLCompareFunctionNotEqual;
    case VK_COMPARE_OP_GREATER_OR_EQUAL: return MTLCompareFunctionGreaterEqual;
    default: return MTLCompareFunctionAlways;
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateSampler(VkDevice device, const VkSamplerCreateInfo *info,
                                                    const VkAllocationCallbacks *a, VkSampler *out)
{
    VkSampler s = vkm_zalloc(sizeof(*s));
    if (!s)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    void *pool = objc_autoreleasePoolPush();
    id desc = mtl_new("MTLSamplerDescriptor");
    M_V_U(desc, "setMinFilter:", info->minFilter == VK_FILTER_LINEAR ? MTLSamplerMinMagFilterLinear
                                                                     : MTLSamplerMinMagFilterNearest);
    M_V_U(desc, "setMagFilter:", info->magFilter == VK_FILTER_LINEAR ? MTLSamplerMinMagFilterLinear
                                                                     : MTLSamplerMinMagFilterNearest);
    M_V_U(desc, "setMipFilter:", info->mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR ? MTLSamplerMipFilterLinear
                                                                                  : MTLSamplerMipFilterNearest);
    M_V_U(desc, "setSAddressMode:", address_mode(info->addressModeU));
    M_V_U(desc, "setTAddressMode:", address_mode(info->addressModeV));
    M_V_U(desc, "setRAddressMode:", address_mode(info->addressModeW));
    M_V_U(desc, "setMaxAnisotropy:", info->anisotropyEnable ? MIME_MAX(1, MIME_MIN((int)info->maxAnisotropy, 16)) : 1);
    M_V_F(desc, "setLodMinClamp:", info->minLod);
    M_V_F(desc, "setLodMaxClamp:", info->maxLod);
    M_V_F(desc, "setLodBias:", info->mipLodBias);
    M_V_B(desc, "setNormalizedCoordinates:", !info->unnormalizedCoordinates);
    if (info->compareEnable)
        M_V_U(desc, "setCompareFunction:", vkm_compare(info->compareOp));
    uint32_t border = MTLSamplerBorderColorTransparentBlack;
    if (info->borderColor == VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK || info->borderColor == VK_BORDER_COLOR_INT_OPAQUE_BLACK)
        border = MTLSamplerBorderColorOpaqueBlack;
    else if (info->borderColor == VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE ||
             info->borderColor == VK_BORDER_COLOR_INT_OPAQUE_WHITE)
        border = MTLSamplerBorderColorOpaqueWhite;
    M_V_U(desc, "setBorderColor:", border);
    const VkSamplerReductionModeCreateInfo *reduction =
        vkm_find(info->pNext, VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO);
    if (reduction && reduction->reductionMode == VK_SAMPLER_REDUCTION_MODE_MIN)
        M_V_U(desc, "setReductionMode:", MTLSamplerReductionModeMinimum);
    else if (reduction && reduction->reductionMode == VK_SAMPLER_REDUCTION_MODE_MAX)
        M_V_U(desc, "setReductionMode:", MTLSamplerReductionModeMaximum);
    M_V_B(desc, "setSupportArgumentBuffers:", true);
    s->state = M_ID_ID(device->mtl, "newSamplerStateWithDescriptor:", desc);
    mtl_release(desc);
    objc_autoreleasePoolPop(pool);
    if (!s->state) {
        free(s);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    s->rid = M_RID(s->state, "gpuResourceID");
    *out = s;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroySampler(VkDevice device, VkSampler s, const VkAllocationCallbacks *a)
{
    if (!s)
        return;
    vkm_release_later(device, s->state, false);
    free(s);
}

/* ------------------------------------------------------------------ private data. */

struct VkPrivateDataSlot_T {
    pthread_mutex_t lock;
    struct {
        uint64_t object, value;
    } *values;
    uint32_t count, capacity;
};

static VKAPI_ATTR VkResult VKAPI_CALL CreatePrivateDataSlot(VkDevice device, const VkPrivateDataSlotCreateInfo *info,
                                                            const VkAllocationCallbacks *a, VkPrivateDataSlot *out)
{
    VkPrivateDataSlot slot = vkm_zalloc(sizeof(*slot));
    if (!slot)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    pthread_mutex_init(&slot->lock, nullptr);
    *out = slot;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyPrivateDataSlot(VkDevice device, VkPrivateDataSlot slot,
                                                         const VkAllocationCallbacks *a)
{
    if (!slot)
        return;
    free(slot->values);
    free(slot);
}

static VKAPI_ATTR VkResult VKAPI_CALL SetPrivateData(VkDevice device, VkObjectType type, uint64_t object,
                                                     VkPrivateDataSlot slot, uint64_t value)
{
    pthread_mutex_lock(&slot->lock);
    for (uint32_t i = 0; i < slot->count; i++)
        if (slot->values[i].object == object) {
            slot->values[i].value = value;
            pthread_mutex_unlock(&slot->lock);
            return VK_SUCCESS;
        }
    if (slot->count == slot->capacity) {
        uint32_t capacity = slot->capacity ? slot->capacity * 2 : 16;
        void *grown = realloc(slot->values, capacity * sizeof(*slot->values));
        if (!grown) {
            pthread_mutex_unlock(&slot->lock);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        slot->values = grown;
        slot->capacity = capacity;
    }
    slot->values[slot->count].object = object;
    slot->values[slot->count++].value = value;
    pthread_mutex_unlock(&slot->lock);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL GetPrivateData(VkDevice device, VkObjectType type, uint64_t object,
                                                 VkPrivateDataSlot slot, uint64_t *value)
{
    *value = 0;
    pthread_mutex_lock(&slot->lock);
    for (uint32_t i = 0; i < slot->count; i++)
        if (slot->values[i].object == object)
            *value = slot->values[i].value;
    pthread_mutex_unlock(&slot->lock);
}

const VkmEntry vkm_memory_entries[] = {
    VKM_ENTRY(AllocateMemory),
    VKM_ENTRY(FreeMemory),
    VKM_ENTRY(MapMemory),
    VKM_ENTRY(UnmapMemory),
    VKM_ENTRY(FlushMappedMemoryRanges),
    VKM_ALIAS("vkInvalidateMappedMemoryRanges", FlushMappedMemoryRanges),
    VKM_ENTRY(GetDeviceMemoryCommitment),
    VKM_ENTRY(GetMemoryHostPointerPropertiesEXT),
    VKM_ENTRY(GetMemoryMetalHandleEXT),
    VKM_ENTRY(GetMemoryMetalHandlePropertiesEXT),
    VKM_ENTRY(GetDeviceMemoryOpaqueCaptureAddress),
    VKM_ENTRY(CreateBuffer),
    VKM_ENTRY(DestroyBuffer),
    VKM_ENTRY(GetBufferMemoryRequirements),
    VKM_ENTRY(GetBufferMemoryRequirements2),
    VKM_ALIAS("vkGetBufferMemoryRequirements2KHR", GetBufferMemoryRequirements2),
    VKM_ENTRY(GetDeviceBufferMemoryRequirements),
    VKM_ALIAS("vkGetDeviceBufferMemoryRequirementsKHR", GetDeviceBufferMemoryRequirements),
    VKM_ENTRY(BindBufferMemory),
    VKM_ENTRY(BindBufferMemory2),
    VKM_ALIAS("vkBindBufferMemory2KHR", BindBufferMemory2),
    VKM_ENTRY(GetBufferDeviceAddress),
    VKM_ALIAS("vkGetBufferDeviceAddressKHR", GetBufferDeviceAddress),
    VKM_ALIAS("vkGetBufferDeviceAddressEXT", GetBufferDeviceAddress),
    VKM_ENTRY(GetBufferOpaqueCaptureAddress),
    VKM_ENTRY(CreateBufferView),
    VKM_ENTRY(DestroyBufferView),
    VKM_ENTRY(CreateImage),
    VKM_ENTRY(DestroyImage),
    VKM_ENTRY(GetImageMemoryRequirements),
    VKM_ENTRY(GetImageMemoryRequirements2),
    VKM_ALIAS("vkGetImageMemoryRequirements2KHR", GetImageMemoryRequirements2),
    VKM_ENTRY(GetDeviceImageMemoryRequirements),
    VKM_ALIAS("vkGetDeviceImageMemoryRequirementsKHR", GetDeviceImageMemoryRequirements),
    VKM_ENTRY(GetImageSparseMemoryRequirements),
    VKM_ENTRY(GetImageSparseMemoryRequirements2),
    VKM_ENTRY(GetDeviceImageSparseMemoryRequirements),
    VKM_ENTRY(BindImageMemory),
    VKM_ENTRY(BindImageMemory2),
    VKM_ALIAS("vkBindImageMemory2KHR", BindImageMemory2),
    VKM_ENTRY(GetImageSubresourceLayout),
    VKM_ENTRY(CreateImageView),
    VKM_ENTRY(DestroyImageView),
    VKM_ENTRY(CreateSampler),
    VKM_ENTRY(DestroySampler),
    VKM_ENTRY(CreatePrivateDataSlot),
    VKM_ENTRY(DestroyPrivateDataSlot),
    VKM_ENTRY(SetPrivateData),
    VKM_ENTRY(GetPrivateData),
    {nullptr, nullptr},
};
