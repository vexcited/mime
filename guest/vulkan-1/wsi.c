/*
 * the window system. a surface is just its window. swapchain images are host
 * images, and the host presents them through its own swapchain on the
 * window's host window, so acquire and present need no reply. images are
 * handed out in turn, and the host orders each acquire after the gpu is done
 * with the image's last present.
 *
 * as with windows drivers, a swapchain whose window changed size is out of
 * date, and the surface reports its window's client area.
 */
#include "mvk_guest.h"

#include <mime/ntuser.h>
#include <mime/win32/winuser.h>

#define MAX_IMAGES 8

typedef struct MvkSurface {
    HWND hwnd;
} MvkSurface;

typedef struct MvkSwapchain {
    uint64_t id;
    HWND hwnd;
    VkExtent2D extent;
    uint32_t count;
    uint32_t next;
    SRWLOCK lock;
    BOOL acquired[MAX_IMAGES];
    uint64_t images[MAX_IMAGES];
} MvkSwapchain;

static MvkSurface *surface_of(VkSurfaceKHR surface)
{
    return (MvkSurface *)(uintptr_t)surface;
}

static MvkSwapchain *swapchain_of(VkSwapchainKHR swapchain)
{
    return (MvkSwapchain *)(uintptr_t)swapchain;
}

/* zero while minimized. */
static VkExtent2D client_extent(HWND hwnd)
{
    RECT r;
    if (IsIconic(hwnd) || !GetClientRect(hwnd, &r))
        return (VkExtent2D){0, 0};
    return (VkExtent2D){(uint32_t)(r.right - r.left), (uint32_t)(r.bottom - r.top)};
}

static BOOL graphics_family(VkPhysicalDevice physical, uint32_t family)
{
    VkQueueFamilyProperties props[32];
    uint32_t count = 32;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, props);
    return family < count && (props[family].queueFlags & VK_QUEUE_GRAPHICS_BIT);
}

/* ------------------------------------------------------------------ surfaces */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR *info,
                                                       const VkAllocationCallbacks *allocator, VkSurfaceKHR *out)
{
    MvkSurface *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    s->hwnd = info->hwnd;
    *out = (VkSurfaceKHR)(uintptr_t)s;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                                               const VkAllocationCallbacks *allocator)
{
    if (surface)
        HeapFree(GetProcessHeap(), 0, surface_of(surface));
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physical,
                                                                              uint32_t family)
{
    return graphics_family(physical, family);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physical, uint32_t family,
                                                                    VkSurfaceKHR surface, VkBool32 *supported)
{
    *supported = graphics_family(physical, family);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physical,
                                                                         VkSurfaceKHR surface,
                                                                         VkSurfaceCapabilitiesKHR *caps)
{
    uint32_t max = physical->properties.limits.maxImageDimension2D;
    VkExtent2D extent = client_extent(surface_of(surface)->hwnd);
    *caps = (VkSurfaceCapabilitiesKHR){
        .minImageCount = 2,
        .maxImageCount = MAX_IMAGES,
        .currentExtent = extent,
        .minImageExtent = extent.width ? (VkExtent2D){1, 1} : extent,
        .maxImageExtent = extent.width ? (VkExtent2D){max ? max : 16384, max ? max : 16384} : extent,
        .maxImageArrayLayers = 1,
        .supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        .supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
    };
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physical,
                                                                          const VkPhysicalDeviceSurfaceInfo2KHR *info,
                                                                          VkSurfaceCapabilities2KHR *caps)
{
    for (VkBaseOutStructure *s = caps->pNext; s; s = s->pNext)
        if (s->sType == VK_STRUCTURE_TYPE_SURFACE_PROTECTED_CAPABILITIES_KHR)
            ((VkSurfaceProtectedCapabilitiesKHR *)s)->supportsProtected = VK_FALSE;
    return vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, info->surface, &caps->surfaceCapabilities);
}

static const VkSurfaceFormatKHR g_formats[] = {
    {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    {VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    /* the last two only while a monitor shows high dynamic range, as Windows drivers list them. */
    {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT},
    {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT},
};
#define FORMAT_COUNT (sizeof(g_formats) / sizeof(g_formats[0]))
#define DEEP_FORMAT_COUNT 2

static uint32_t format_count(void)
{
    MIME_MONITOR_INFO list[MIME_USER_MAX_MONITORS];
    ULONG count = (ULONG)NtUserCallTwoParam((ULONG_PTR)list, MIME_USER_MAX_MONITORS, MIME_TWOPARAM_GETMONITORS);
    for (ULONG i = 0; i < count && i < MIME_USER_MAX_MONITORS; i++)
        if (list[i].Flags & MIME_MONITORINFOF_HDR)
            return FORMAT_COUNT;
    return FORMAT_COUNT - DEEP_FORMAT_COUNT;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physical, VkSurfaceKHR surface,
                                                                    uint32_t *count, VkSurfaceFormatKHR *formats)
{
    uint32_t all = format_count();
    if (!formats) {
        *count = all;
        return VK_SUCCESS;
    }
    uint32_t n = *count < all ? *count : all;
    memcpy(formats, g_formats, n * sizeof(*formats));
    *count = n;
    return n < all ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physical,
                                                                     const VkPhysicalDeviceSurfaceInfo2KHR *info,
                                                                     uint32_t *count, VkSurfaceFormat2KHR *formats)
{
    uint32_t all = format_count();
    if (!formats) {
        *count = all;
        return VK_SUCCESS;
    }
    uint32_t n = *count < all ? *count : all;
    for (uint32_t i = 0; i < n; i++)
        formats[i].surfaceFormat = g_formats[i];
    *count = n;
    return n < all ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physical,
                                                                         VkSurfaceKHR surface, uint32_t *count,
                                                                         VkPresentModeKHR *modes)
{
    static const VkPresentModeKHR all[] = {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
                                           VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR};
    if (!modes) {
        *count = 4;
        return VK_SUCCESS;
    }
    uint32_t n = *count < 4 ? *count : 4;
    memcpy(modes, all, n * sizeof(*modes));
    *count = n;
    return n < 4 ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physical,
                                                                       VkSurfaceKHR surface, uint32_t *count,
                                                                       VkRect2D *rects)
{
    if (!rects) {
        *count = 1;
        return VK_SUCCESS;
    }
    if (!*count)
        return VK_INCOMPLETE;
    rects[0] = (VkRect2D){{0, 0}, client_extent(surface_of(surface)->hwnd)};
    *count = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupPresentCapabilitiesKHR(VkDevice device,
                                                                      VkDeviceGroupPresentCapabilitiesKHR *caps)
{
    memset(caps->presentMask, 0, sizeof(caps->presentMask));
    caps->presentMask[0] = 1;
    caps->modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface,
                                                                      VkDeviceGroupPresentModeFlagsKHR *modes)
{
    *modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ swapchains */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *info,
                                                    const VkAllocationCallbacks *allocator, VkSwapchainKHR *out)
{
    MvkSwapchain *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    InitializeSRWLock(&s->lock);
    s->hwnd = surface_of(info->surface)->hwnd;
    s->extent = info->imageExtent;
    s->count = info->minImageCount < 2 ? 2 : info->minImageCount > MAX_IMAGES ? MAX_IMAGES : info->minImageCount;
    s->id = mvk_new_id();
    for (uint32_t i = 0; i < s->count; i++)
        s->images[i] = mvk_new_id();
    const VkImageFormatListCreateInfo *list = nullptr;
    for (const VkBaseInStructure *x = info->pNext; x; x = x->pNext)
        if (x->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO)
            list = (const VkImageFormatListCreateInfo *)x;

    MvkEncoder *e = mvk_call_begin(MVK_OP_CREATE_SWAPCHAIN);
    mvk_put_u64(e, device->obj.id);
    mvk_put_u64(e, s->id);
    mvk_put_u32(e, info->flags);
    mvk_put_u32(e, info->imageFormat);
    mvk_put_u32(e, info->imageColorSpace);
    mvk_put_u32(e, info->imageExtent.width);
    mvk_put_u32(e, info->imageExtent.height);
    mvk_put_u32(e, info->imageArrayLayers);
    mvk_put_u32(e, info->imageUsage);
    mvk_put_u32(e, info->imageSharingMode);
    uint32_t families = info->imageSharingMode == VK_SHARING_MODE_CONCURRENT ? info->queueFamilyIndexCount : 0;
    mvk_put_u32(e, families);
    for (uint32_t i = 0; i < families; i++)
        mvk_put_u32(e, info->pQueueFamilyIndices[i]);
    mvk_put_u32(e, info->presentMode);
    uint32_t views = list ? list->viewFormatCount : 0;
    mvk_put_u32(e, views);
    for (uint32_t i = 0; i < views; i++)
        mvk_put_u32(e, list->pViewFormats[i]);
    mvk_put_u32(e, s->count);
    for (uint32_t i = 0; i < s->count; i++)
        mvk_put_u64(e, s->images[i]);
    mvk_put_u32(e, info->compositeAlpha);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult r = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    mvk_call_done(d);
    if (r != VK_SUCCESS) {
        for (uint32_t i = 0; i < s->count; i++)
            mvk_free_id(s->images[i]);
        mvk_free_id(s->id);
        HeapFree(GetProcessHeap(), 0, s);
        return r;
    }
    *out = (VkSwapchainKHR)(uintptr_t)s;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                 const VkAllocationCallbacks *allocator)
{
    MvkSwapchain *s = swapchain_of(swapchain);
    if (!s)
        return;
    MvkEncoder *e = mvk_call_begin(MVK_OP_DESTROY_SWAPCHAIN);
    mvk_put_u64(e, s->id);
    mvk_call_end(e);
    for (uint32_t i = 0; i < s->count; i++)
        mvk_free_id(s->images[i]);
    mvk_free_id(s->id);
    HeapFree(GetProcessHeap(), 0, s);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *count,
                                                       VkImage *images)
{
    MvkSwapchain *s = swapchain_of(swapchain);
    if (!images) {
        *count = s->count;
        return VK_SUCCESS;
    }
    uint32_t n = *count < s->count ? *count : s->count;
    for (uint32_t i = 0; i < n; i++)
        images[i] = (VkImage)s->images[i];
    *count = n;
    return n < s->count ? VK_INCOMPLETE : VK_SUCCESS;
}

static BOOL out_of_date(MvkSwapchain *s)
{
    VkExtent2D now = client_extent(s->hwnd);
    return now.width && (now.width != s->extent.width || now.height != s->extent.height);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                                     VkSemaphore semaphore, VkFence fence, uint32_t *index)
{
    MvkSwapchain *s = swapchain_of(swapchain);
    if (out_of_date(s))
        return VK_ERROR_OUT_OF_DATE_KHR;
    AcquireSRWLockExclusive(&s->lock);
    uint32_t i = s->next, n = 0;
    while (n < s->count && s->acquired[i]) {
        i = (i + 1) % s->count;
        n++;
    }
    if (n == s->count) {
        ReleaseSRWLockExclusive(&s->lock);
        return timeout ? VK_TIMEOUT : VK_NOT_READY;
    }
    s->acquired[i] = TRUE;
    s->next = (i + 1) % s->count;
    MvkEncoder *e = mvk_call_begin(MVK_OP_ACQUIRE_IMAGE);
    mvk_put_u64(e, s->id);
    mvk_put_u32(e, i);
    mvk_put_u64(e, (uint64_t)semaphore);
    mvk_put_u64(e, (uint64_t)fence);
    mvk_call_end(e);
    ReleaseSRWLockExclusive(&s->lock);
    *index = i;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR *info,
                                                      uint32_t *index)
{
    return vkAcquireNextImageKHR(device, info->swapchain, info->timeout, info->semaphore, info->fence, index);
}

/* the swapchain shows in the top level window, at the client area's place in it and in front of what is behind it. */
static void target(HWND hwnd, MIME_PRESENT_LAYER *layer)
{
    *layer = (MIME_PRESENT_LAYER){};
    NtUserCallHwndParam(hwnd, (ULONG_PTR)layer, MIME_HWNDPARAM_PRESENTLAYER);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *info)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_PRESENT);
    mvk_put_u64(e, queue->obj.id);
    mvk_put_u32(e, info->waitSemaphoreCount);
    for (uint32_t i = 0; i < info->waitSemaphoreCount; i++)
        mvk_put_u64(e, (uint64_t)info->pWaitSemaphores[i]);
    mvk_put_u32(e, info->swapchainCount);
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < info->swapchainCount; i++) {
        MvkSwapchain *s = swapchain_of(info->pSwapchains[i]);
        uint32_t index = info->pImageIndices[i];
        MIME_PRESENT_LAYER layer;
        target(s->hwnd, &layer);
        mvk_put_u64(e, s->id);
        mvk_put_u32(e, index);
        mvk_put_u32(e, layer.Window);
        mvk_put_u32(e, (uint32_t)layer.Client.left);
        mvk_put_u32(e, (uint32_t)layer.Client.top);
        mvk_put_u32(e, (uint32_t)layer.Client.right);
        mvk_put_u32(e, (uint32_t)layer.Client.bottom);
        mvk_put_u32(e, (uint32_t)(ULONG_PTR)s->hwnd);
        mvk_put_u32(e, layer.Order);
        mvk_put_u32(e, (uint32_t)layer.Visible.left);
        mvk_put_u32(e, (uint32_t)layer.Visible.top);
        mvk_put_u32(e, (uint32_t)layer.Visible.right);
        mvk_put_u32(e, (uint32_t)layer.Visible.bottom);
        mvk_put_u32(e, layer.Stamp);
        AcquireSRWLockExclusive(&s->lock);
        if (index < s->count)
            s->acquired[index] = FALSE;
        ReleaseSRWLockExclusive(&s->lock);
        VkResult r = out_of_date(s) ? VK_SUBOPTIMAL_KHR : VK_SUCCESS;
        if (info->pResults)
            info->pResults[i] = r;
        if (r != VK_SUCCESS && result == VK_SUCCESS)
            result = r;
    }
    mvk_call_end(e);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkSetHdrMetadataEXT(VkDevice device, uint32_t count, const VkSwapchainKHR *swapchains,
                                               const VkHdrMetadataEXT *metadata)
{
    for (uint32_t i = 0; i < count; i++) {
        const VkHdrMetadataEXT *m = &metadata[i];
        float v[12] = {m->displayPrimaryRed.x,   m->displayPrimaryRed.y,  m->displayPrimaryGreen.x,
                       m->displayPrimaryGreen.y, m->displayPrimaryBlue.x, m->displayPrimaryBlue.y,
                       m->whitePoint.x,          m->whitePoint.y,         m->maxLuminance,
                       m->minLuminance,          m->maxContentLightLevel, m->maxFrameAverageLightLevel};
        MvkEncoder *e = mvk_call_begin(MVK_OP_HDR_METADATA);
        mvk_put_u64(e, swapchain_of(swapchains[i])->id);
        mvk_put_bytes(e, v, sizeof(v));
        mvk_call_end(e);
    }
}

const MvkProc mvk_wsi_procs[] = {
    {"vkSetHdrMetadataEXT", (PFN_vkVoidFunction)vkSetHdrMetadataEXT},
    {nullptr, nullptr},
};
