/*
 * surfaces and swapchains on a CAMetalLayer. swapchain images are textures of the driver's own; a present
 * copies the image into the layer's next drawable on the queue, which waits for the drawable and signals it.
 */
#include "vkm.h"

#include <CoreGraphics/CoreGraphics.h>

/* the driver's own commands, for images the swapchain makes. */
#define CALL(name) ((PFN_##name)vkm_GetInstanceProcAddr(nullptr, #name))

static const VkSurfaceFormatKHR g_formats[] = {
    {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
};

static const VkPresentModeKHR g_modes[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR};

/* ------------------------------------------------------------------ surfaces. */

static VKAPI_ATTR VkResult VKAPI_CALL CreateMetalSurfaceEXT(VkInstance instance,
                                                            const VkMetalSurfaceCreateInfoEXT *info,
                                                            const VkAllocationCallbacks *a, VkSurfaceKHR *out)
{
    if (!info->pLayer)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkSurfaceKHR s = vkm_zalloc(sizeof(*s));
    if (!s)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    s->layer = [(CAMetalLayer *)info->pLayer retain];
    *out = s;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR s,
                                                   const VkAllocationCallbacks *a)
{
    if (!s)
        return;
    [s->layer release];
    free(s);
}

static VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice p, uint32_t family,
                                                                        VkSurfaceKHR s, VkBool32 *out)
{
    *out = family == 0;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice p, VkSurfaceKHR s,
                                                                             VkSurfaceCapabilitiesKHR *out)
{
    /* the swapchain sets the layer's drawable size, so the surface takes any extent. */
    *out = (VkSurfaceCapabilitiesKHR){
        .minImageCount = 2,
        .maxImageCount = MIME_ARRAY_COUNT(((struct VkSwapchainKHR_T *)0)->images),
        .currentExtent = {UINT32_MAX, UINT32_MAX},
        .minImageExtent = {1, 1},
        .maxImageExtent = {16384, 16384},
        .maxImageArrayLayers = 1,
        .supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        .supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    };
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceCapabilities2KHR(
    VkPhysicalDevice p, const VkPhysicalDeviceSurfaceInfo2KHR *info, VkSurfaceCapabilities2KHR *out)
{
    return GetPhysicalDeviceSurfaceCapabilitiesKHR(p, info->surface, &out->surfaceCapabilities);
}

static VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice p, VkSurfaceKHR s,
                                                                        uint32_t *count, VkSurfaceFormatKHR *out)
{
    uint32_t n = MIME_ARRAY_COUNT(g_formats);
    if (!out) {
        *count = n;
        return VK_SUCCESS;
    }
    uint32_t k = MIME_MIN(*count, n);
    memcpy(out, g_formats, sizeof(*out) * k);
    *count = k;
    return k < n ? VK_INCOMPLETE : VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice p,
                                                                         const VkPhysicalDeviceSurfaceInfo2KHR *info,
                                                                         uint32_t *count, VkSurfaceFormat2KHR *out)
{
    uint32_t n = MIME_ARRAY_COUNT(g_formats);
    if (!out) {
        *count = n;
        return VK_SUCCESS;
    }
    uint32_t k = MIME_MIN(*count, n);
    for (uint32_t i = 0; i < k; i++)
        out[i].surfaceFormat = g_formats[i];
    *count = k;
    return k < n ? VK_INCOMPLETE : VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice p, VkSurfaceKHR s,
                                                                             uint32_t *count, VkPresentModeKHR *out)
{
    uint32_t n = MIME_ARRAY_COUNT(g_modes);
    if (!out) {
        *count = n;
        return VK_SUCCESS;
    }
    uint32_t k = MIME_MIN(*count, n);
    memcpy(out, g_modes, sizeof(*out) * k);
    *count = k;
    return k < n ? VK_INCOMPLETE : VK_SUCCESS;
}

/* ------------------------------------------------------------------ swapchains. */

static void swapchain_free(VkSwapchainKHR sc)
{
    VkDevice device = sc->device;
    for (uint32_t i = 0; i < sc->count; i++) {
        if (sc->images[i]) {
            sc->images[i]->swapchain = false;
            CALL(vkDestroyImage)(device, sc->images[i], nullptr);
        }
        CALL(vkFreeMemory)(device, sc->memory[i], nullptr);
    }
    /* a retired swapchain's layer belongs to its replacement. */
    if (sc->resident && sc->surface) {
        id<MTLResidencySet> residency = sc->surface->layer.residencySet;
        if (residency)
            [device->queue.queue removeResidencySet:residency];
    }
    free(sc);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *info,
                                                         const VkAllocationCallbacks *a, VkSwapchainKHR *out)
{
    const VkmFormat *f = vkm_format(info->imageFormat);
    if (!f->pixel)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkSwapchainKHR sc = vkm_zalloc(sizeof(*sc));
    if (!sc)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    sc->surface = info->surface;
    sc->device = device;
    sc->format = info->imageFormat;
    sc->extent = info->imageExtent;
    sc->mode = info->presentMode;
    sc->count = MIME_MIN(MIME_MAX(info->minImageCount, 2u), (uint32_t)MIME_ARRAY_COUNT(sc->images));
    VkResult result = VK_SUCCESS;
    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = info->imageFormat,
        .extent = {info->imageExtent.width, info->imageExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = info->imageUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    };
    for (uint32_t i = 0; i < sc->count && result == VK_SUCCESS; i++) {
        result = CALL(vkCreateImage)(device, &ici, nullptr, &sc->images[i]);
        if (result != VK_SUCCESS)
            break;
        VkMemoryRequirements req;
        CALL(vkGetImageMemoryRequirements)(device, sc->images[i], &req);
        VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, VKM_MEMORY_DEVICE};
        result = CALL(vkAllocateMemory)(device, &mai, nullptr, &sc->memory[i]);
        if (result == VK_SUCCESS)
            result = CALL(vkBindImageMemory)(device, sc->images[i], sc->memory[i], 0);
        sc->images[i]->swapchain = true;
    }
    if (result != VK_SUCCESS) {
        swapchain_free(sc);
        return result;
    }
    @autoreleasepool {
        CAMetalLayer *layer = sc->surface->layer;
        layer.device = device->mtl;
        layer.pixelFormat = (MTLPixelFormat)f->pixel;
        layer.framebufferOnly = false;
        layer.drawableSize = (CGSize){info->imageExtent.width, info->imageExtent.height};
        layer.maximumDrawableCount = MIME_MIN(MIME_MAX(sc->count, 2u), 3u);
        layer.displaySyncEnabled = info->presentMode != VK_PRESENT_MODE_IMMEDIATE_KHR;
        layer.opaque = info->compositeAlpha == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        layer.colorspace = space;
        CGColorSpaceRelease(space);
        id<MTLResidencySet> residency = layer.residencySet;
        if (residency) {
            [device->queue.queue addResidencySet:residency];
            sc->resident = true;
        }
    }
    if (info->oldSwapchain)
        info->oldSwapchain->surface = nullptr;
    *out = sc;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR sc,
                                                     const VkAllocationCallbacks *a)
{
    if (sc)
        swapchain_free(sc);
}

static VKAPI_ATTR VkResult VKAPI_CALL GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR sc, uint32_t *count,
                                                            VkImage *out)
{
    if (!out) {
        *count = sc->count;
        return VK_SUCCESS;
    }
    uint32_t k = MIME_MIN(*count, sc->count);
    memcpy(out, sc->images, sizeof(*out) * k);
    *count = k;
    return k < sc->count ? VK_INCOMPLETE : VK_SUCCESS;
}

/* images come back in turn: the queue orders a present's copy before what the next frame draws into it. */
static VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImageKHR(VkDevice device, VkSwapchainKHR sc, uint64_t timeout,
                                                          VkSemaphore semaphore, VkFence fence, uint32_t *index)
{
    if (!sc->surface)
        return VK_ERROR_OUT_OF_DATE_KHR;
    *index = sc->next;
    sc->next = (sc->next + 1) % sc->count;
    if (semaphore) {
        uint64_t value = semaphore->type == VK_SEMAPHORE_TYPE_BINARY ? ++semaphore->binary : 0;
        if (value)
            semaphore->event.signaledValue = value;
    }
    if (fence) {
        fence->signaled = true;
        fence->queue = nullptr;
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR *info,
                                                           uint32_t *index)
{
    return AcquireNextImageKHR(device, info->swapchain, info->timeout, info->semaphore, info->fence, index);
}

static VkResult present_one(VkQueue q, VkSwapchainKHR sc, uint32_t index, uint32_t wait_count,
                            const VkSemaphore *waits)
{
    if (!sc->surface)
        return VK_ERROR_OUT_OF_DATE_KHR;
    id<CAMetalDrawable> drawable = [sc->surface->layer nextDrawable];
    pthread_mutex_lock(&q->lock);
    for (uint32_t i = 0; i < wait_count; i++)
        vkm_semaphore_wait(q, waits[i], 0);
    VkResult result = VK_SUCCESS;
    if (drawable) {
        id<MTLTexture> target = drawable.texture;
        VkImage image = sc->images[index];
        [q->queue waitForDrawable:drawable];
        id<MTL4CommandBuffer> cmd = vkm_queue_begin(q);
        if (cmd) {
            id<MTL4ComputeCommandEncoder> e = [cmd computeCommandEncoder];
            [e barrierAfterQueueStages:MTLStageAll
                          beforeStages:MTLStageBlit
                     visibilityOptions:MTL4VisibilityOptionDevice];
            MTLSize size = {MIME_MIN(sc->extent.width, (uint32_t)target.width),
                            MIME_MIN(sc->extent.height, (uint32_t)target.height), 1};
            [e copyFromTexture:image->texture
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:(MTLOrigin){}
                       sourceSize:size
                        toTexture:target
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:(MTLOrigin){}];
            [e endEncoding];
            vkm_queue_commit(q, cmd);
        } else {
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        [q->queue signalDrawable:drawable];
        [drawable present];
    }
    vkm_queue_signal(q);
    pthread_mutex_unlock(&q->lock);
    return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue q, const VkPresentInfoKHR *info)
{
    VkResult result = VK_SUCCESS;
    @autoreleasepool {
        vkm_residency_commit(q->device);
        for (uint32_t i = 0; i < info->swapchainCount; i++) {
            /* the waits go before the first present only, which orders the others after them too. */
            VkResult r = present_one(q, info->pSwapchains[i], info->pImageIndices[i],
                                     i ? 0 : info->waitSemaphoreCount, info->pWaitSemaphores);
            if (info->pResults)
                info->pResults[i] = r;
            if (r != VK_SUCCESS)
                result = r;
        }
    }
    return result;
}

const VkmEntry vkm_wsi_entries[] = {
    VKM_ENTRY(CreateMetalSurfaceEXT),
    VKM_ENTRY(DestroySurfaceKHR),
    VKM_ENTRY(GetPhysicalDeviceSurfaceSupportKHR),
    VKM_ENTRY(GetPhysicalDeviceSurfaceCapabilitiesKHR),
    VKM_ENTRY(GetPhysicalDeviceSurfaceCapabilities2KHR),
    VKM_ENTRY(GetPhysicalDeviceSurfaceFormatsKHR),
    VKM_ENTRY(GetPhysicalDeviceSurfaceFormats2KHR),
    VKM_ENTRY(GetPhysicalDeviceSurfacePresentModesKHR),
    VKM_ENTRY(CreateSwapchainKHR),
    VKM_ENTRY(DestroySwapchainKHR),
    VKM_ENTRY(GetSwapchainImagesKHR),
    VKM_ENTRY(AcquireNextImageKHR),
    VKM_ENTRY(AcquireNextImage2KHR),
    VKM_ENTRY(QueuePresentKHR),
    {nullptr, nullptr},
};
