/*
 * swap chains. the back buffer is an ordinary device texture. Present blits it, scaled to the client area, to an
 * image of a vulkan swap chain on the window and presents that. sync interval 0 presents immediately, others use
 * fifo, and switching between them recreates the vulkan swap chain. with a flip model the program still sees one
 * back buffer, as in direct3d 11.
 *
 * composition swap chains have no window. Present copies the back buffer to a texture of its own, which dcomp.dll
 * draws into the windows it composes, and notifies it. the sync interval paces the caller to display blanks like
 * the compositor would: a frame shows at the first blank after it was presented once the previous frame has had
 * its intervals, and Present waits when more frames than the latency allows are still to show.
 */
#include "d3d11_private.h"
#include <mime/vblank.h>

#define MAX_IMAGES 8
#define SEMAPHORES 16

#define MAX_LISTENERS 4
#define SHOWN_FRAMES 32

typedef struct D3dSwapChain {
    IDXGISwapChain4 iface;
    IMimeDXGICompositionSource source;
    IDXGISwapChainMedia media;
    LONG refs;
    D3dDevice *device;
    IDXGIFactory *factory;
    HWND window;
    DXGI_SWAP_CHAIN_DESC1 desc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen;
    BOOL fullscreen_state;
    /* the output a fullscreen swap chain owns and controls the gamma of. */
    IDXGIOutput *owned;
    /* window placement saved before going fullscreen. */
    BOOL windowed_saved;
    RECT windowed_rect;
    LONG windowed_style, windowed_ex_style;
    D3dResource *back;
    /* a multisampled back buffer resolves here before the blit. */
    D3dResource *resolved;
    MimePrivateData data;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;
    VkPresentModeKHR mode;
    UINT image_count;
    VkImage images[MAX_IMAGES];
    VkSemaphore present[MAX_IMAGES];
    VkSemaphore acquire[SEMAPHORES];
    UINT64 acquire_serial[SEMAPHORES];

    UINT present_count;
    UINT64 frame_serials[16];
    UINT max_latency;
    HANDLE latency;
    DXGI_COLOR_SPACE_TYPE color_space;
    DXGI_MODE_ROTATION rotation;
    DXGI_RGBA background;
    UINT source_width, source_height;

    /* composition: the last presented frame and the listeners for the next one. */
    BOOL composition;
    HANDLE surface_handle;
    CRITICAL_SECTION frame_lock;
    D3dResource *front;
    UINT64 frame_serial;
    DXGI_MATRIX_3X2_F matrix;
    struct {
        MIME_COMPOSITION_LISTENER function;
        void *context;
    } listeners[MAX_LISTENERS];
    /* id of each recent present and the blank it shows at. */
    struct {
        UINT id;
        UINT64 blank;
    } shown[SHOWN_FRAMES];
    UINT64 last_blank;
    UINT last_interval;
    UINT present_duration;
} D3dSwapChain;

static D3dSwapChain *impl(IDXGISwapChain4 *iface) { return CONTAINING_RECORD(iface, D3dSwapChain, iface); }
static D3dSwapChain *source_impl(IMimeDXGICompositionSource *iface)
{
    return CONTAINING_RECORD(iface, D3dSwapChain, source);
}
static D3dSwapChain *media_impl(IDXGISwapChainMedia *iface) { return CONTAINING_RECORD(iface, D3dSwapChain, media); }

/* ------------------------------------------------------------------ the vulkan swap chain */

static void destroy_swapchain(D3dSwapChain *sc)
{
    D3dVk *vk = &sc->device->vk;
    if (!sc->swapchain)
        return;
    D3dContextFlush(sc->device->immediate);
    vk->vkDeviceWaitIdle(vk->device);
    for (UINT i = 0; i < sc->image_count; i++)
        vk->vkDestroySemaphore(vk->device, sc->present[i], nullptr);
    vk->vkDestroySwapchainKHR(vk->device, sc->swapchain, nullptr);
    sc->swapchain = VK_NULL_HANDLE;
    sc->image_count = 0;
}

/* picks a surface format that keeps the back buffer's bytes unchanged, srgb or not. */
static VkFormat choose_format(D3dSwapChain *sc)
{
    D3dVk *vk = &sc->device->vk;
    VkSurfaceFormatKHR formats[32];
    UINT count = RTL_NUMBER_OF(formats);
    vk->vkGetPhysicalDeviceSurfaceFormatsKHR(vk->physical, sc->surface, &count, formats);
    VkFormat wanted;
    switch (sc->desc.Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        wanted = VK_FORMAT_B8G8R8A8_SRGB;
        break;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        wanted = VK_FORMAT_R8G8B8A8_SRGB;
        break;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        wanted = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        break;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        wanted = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    default:
        wanted = VK_FORMAT_B8G8R8A8_UNORM;
        break;
    }
    for (UINT i = 0; i < count; i++)
        if (formats[i].format == wanted)
            return wanted;
    BOOL srgb = wanted == VK_FORMAT_B8G8R8A8_SRGB || wanted == VK_FORMAT_R8G8B8A8_SRGB;
    for (UINT i = 0; i < count; i++)
        if (formats[i].format == (srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM))
            return formats[i].format;
    return count ? formats[0].format : VK_FORMAT_B8G8R8A8_UNORM;
}

/* VK_NOT_READY when the window has no area to show anything in. */
static VkResult create_swapchain(D3dSwapChain *sc, VkPresentModeKHR mode)
{
    D3dDevice *device = sc->device;
    D3dVk *vk = &device->vk;
    VkSurfaceCapabilitiesKHR caps;
    VkResult r = vk->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->physical, sc->surface, &caps);
    if (r != VK_SUCCESS)
        return r;
    if (!caps.currentExtent.width || !caps.currentExtent.height)
        return VK_NOT_READY;
    VkPresentModeKHR modes[8];
    UINT count = RTL_NUMBER_OF(modes);
    vk->vkGetPhysicalDeviceSurfacePresentModesKHR(vk->physical, sc->surface, &count, modes);
    VkPresentModeKHR chosen = VK_PRESENT_MODE_FIFO_KHR;
    for (UINT i = 0; i < count; i++)
        if (modes[i] == mode)
            chosen = mode;
    if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR && chosen != mode)
        for (UINT i = 0; i < count; i++)
            if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                chosen = VK_PRESENT_MODE_MAILBOX_KHR;
    sc->format = choose_format(sc);
    VkSwapchainCreateInfoKHR info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = sc->surface;
    /* the host copies a presented image out at once, so two are enough to draw one while the other shows. */
    info.minImageCount = D3D_MAX(caps.minImageCount, 2);
    if (caps.maxImageCount)
        info.minImageCount = D3D_MIN(info.minImageCount, caps.maxImageCount);
    info.imageFormat = sc->format;
    info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    info.imageExtent = caps.currentExtent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    info.compositeAlpha = sc->desc.AlphaMode == DXGI_ALPHA_MODE_PREMULTIPLIED ? VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
                                                                               : VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = chosen;
    info.clipped = TRUE;
    info.oldSwapchain = sc->swapchain;
    VkSwapchainKHR swapchain;
    r = vk->vkCreateSwapchainKHR(vk->device, &info, nullptr, &swapchain);
    /* the old one may be what keeps the new one from fitting. */
    if (r == VK_ERROR_OUT_OF_DEVICE_MEMORY && sc->swapchain) {
        destroy_swapchain(sc);
        info.oldSwapchain = VK_NULL_HANDLE;
        r = vk->vkCreateSwapchainKHR(vk->device, &info, nullptr, &swapchain);
    }
    if (r != VK_SUCCESS)
        return r;
    destroy_swapchain(sc);
    sc->swapchain = swapchain;
    sc->extent = caps.currentExtent;
    sc->mode = mode;
    UINT images = MAX_IMAGES;
    vk->vkGetSwapchainImagesKHR(vk->device, swapchain, &images, sc->images);
    sc->image_count = images;
    VkSemaphoreCreateInfo sem = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (UINT i = 0; i < images; i++)
        vk->vkCreateSemaphore(vk->device, &sem, nullptr, &sc->present[i]);
    return VK_SUCCESS;
}

/* a window with no area shows nothing and is not an error; other failures are the program's to know. */
static HRESULT present_failed(D3dSwapChain *sc, VkResult r)
{
    D3dContextFlush(sc->device->immediate);
    if (r == VK_NOT_READY || r == VK_ERROR_SURFACE_LOST_KHR || r == VK_TIMEOUT)
        return S_OK;
    return D3dCheckResult(sc->device, r);
}

static HRESULT create_back_buffer(D3dSwapChain *sc)
{
    if (!sc->composition && (!sc->desc.Width || !sc->desc.Height)) {
        RECT client;
        GetClientRect(sc->window, &client);
        if (!sc->desc.Width)
            sc->desc.Width = D3D_MAX(client.right - client.left, 1);
        if (!sc->desc.Height)
            sc->desc.Height = D3D_MAX(client.bottom - client.top, 1);
    }
    D3D11_TEXTURE2D_DESC1 desc = {};
    desc.Width = sc->desc.Width;
    desc.Height = sc->desc.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = sc->desc.Format;
    desc.SampleDesc = sc->desc.SampleDesc;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (sc->desc.BufferUsage & DXGI_USAGE_UNORDERED_ACCESS)
        desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    HRESULT hr = D3dCreateTexture(sc->device, D3D11_RESOURCE_DIMENSION_TEXTURE2D, &desc, nullptr, &sc->back);
    if (SUCCEEDED(hr))
        sc->back->swapchain = TRUE;
    return hr;
}

/* ------------------------------------------------------------------ presenting */

static HRESULT present(D3dSwapChain *sc, UINT interval, UINT flags)
{
    D3dDevice *device = sc->device;
    D3dVk *vk = &device->vk;
    D3dContext *c = device->immediate;
    if (interval > 4)
        return DXGI_ERROR_INVALID_CALL;
    if (device->removed != S_OK)
        return DXGI_ERROR_DEVICE_REMOVED;
    if (IsIconic(sc->window))
        return DXGI_STATUS_OCCLUDED;
    if (flags & DXGI_PRESENT_TEST)
        return S_OK;
    VkPresentModeKHR mode = interval ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    VkResult made = VK_SUCCESS;
    if ((!sc->swapchain || sc->mode != mode) && (made = create_swapchain(sc, mode)) != VK_SUCCESS)
        return present_failed(sc, made);

    UINT slot = sc->present_count % SEMAPHORES;
    D3dWaitSerial(device, sc->acquire_serial[slot]);
    if (!sc->acquire[slot]) {
        VkSemaphoreCreateInfo sem = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vk->vkCreateSemaphore(vk->device, &sem, nullptr, &sc->acquire[slot]);
    }
    UINT index = 0;
    VkResult r = vk->vkAcquireNextImageKHR(vk->device, sc->swapchain, UINT64_MAX, sc->acquire[slot], VK_NULL_HANDLE,
                                           &index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        /* the window was resized. */
        if ((made = create_swapchain(sc, mode)) != VK_SUCCESS)
            return present_failed(sc, made);
        r = vk->vkAcquireNextImageKHR(vk->device, sc->swapchain, UINT64_MAX, sc->acquire[slot], VK_NULL_HANDLE,
                                      &index);
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        return present_failed(sc, r);

    VkCommandBuffer cmd = D3dContextCommands(c, TRUE);
    if (!cmd)
        return E_OUTOFMEMORY;
    VkImageMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = sc->images[index];
    barrier.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vk->vkCmdPipelineBarrier2(cmd, &dep);
    VkImage source = sc->back->tex.image;
    if (sc->back->tex.samples > 1) {
        if (!sc->resolved) {
            D3D11_TEXTURE2D_DESC1 desc = {};
            desc.Width = sc->back->tex.width;
            desc.Height = sc->back->tex.height;
            desc.MipLevels = desc.ArraySize = 1;
            desc.Format = sc->back->tex.format;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (FAILED(D3dCreateTexture(device, D3D11_RESOURCE_DIMENSION_TEXTURE2D, &desc, nullptr, &sc->resolved)))
                return E_OUTOFMEMORY;
        }
        VkImageResolve resolve = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                  {0, 0, 0}, {sc->back->tex.width, sc->back->tex.height, 1}};
        vk->vkCmdResolveImage(cmd, source, VK_IMAGE_LAYOUT_GENERAL, sc->resolved->tex.image, VK_IMAGE_LAYOUT_GENERAL,
                              1, &resolve);
        D3dBarrier(cmd, device);
        source = sc->resolved->tex.image;
        D3dContextUse(c, sc->resolved);
    }
    VkImageBlit blit = {};
    blit.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = (VkOffset3D){sc->back->tex.width, sc->back->tex.height, 1};
    blit.dstSubresource = blit.srcSubresource;
    blit.dstOffsets[1] = (VkOffset3D){sc->extent.width, sc->extent.height, 1};
    vk->vkCmdBlitImage(cmd, source, VK_IMAGE_LAYOUT_GENERAL, sc->images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vk->vkCmdPipelineBarrier2(cmd, &dep);
    D3dContextUse(c, sc->back);

    VkSemaphoreSubmitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = sc->acquire[slot];
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    VkSemaphoreSubmitInfo signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = sc->present[index];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    UINT64 serial = D3dContextSubmit(c, &wait, &signal);
    sc->acquire_serial[slot] = serial;

    VkPresentInfoKHR info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &sc->present[index];
    info.swapchainCount = 1;
    info.pSwapchains = &sc->swapchain;
    info.pImageIndices = &index;
    AcquireSRWLockExclusive(device->queue_lock);
    VkResult presented = vk->vkQueuePresentKHR(device->queue, &info);
    ReleaseSRWLockExclusive(device->queue_lock);
    if (presented == VK_ERROR_DEVICE_LOST)
        return D3dCheckResult(device, presented);

    /* keep the gpu at most the latency's worth of frames behind. */
    UINT latency = sc->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT ? sc->max_latency
                                                                                        : device->max_frame_latency;
    sc->frame_serials[sc->present_count % 16] = serial;
    sc->present_count++;
    latency = D3D_MAX(1, D3D_MIN(latency, 15));
    if (sc->present_count > latency)
        D3dWaitSerial(device, sc->frame_serials[(sc->present_count - latency - 1) % 16]);
    if (sc->latency)
        ReleaseSemaphore(sc->latency, 1, nullptr);
    return device->removed != S_OK ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
}

/* ------------------------------------------------------------------ composition */

/* composition reads frame bytes as they are: srgb formats are stored typeless and viewed as plain unorm. */
static DXGI_FORMAT view_format(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:
        return format;
    }
}

static DXGI_FORMAT storage_format(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    default:
        return format;
    }
}

static HRESULT create_front(D3dSwapChain *sc)
{
    D3D11_TEXTURE2D_DESC1 desc = {};
    desc.Width = sc->back->tex.width;
    desc.Height = sc->back->tex.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = storage_format(sc->desc.Format);
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3dResource *front;
    HRESULT hr = D3dCreateTexture(sc->device, D3D11_RESOURCE_DIMENSION_TEXTURE2D, &desc, nullptr, &front);
    if (FAILED(hr))
        return hr;
    EnterCriticalSection(&sc->frame_lock);
    D3dResource *old = sc->front;
    sc->front = front;
    LeaveCriticalSection(&sc->frame_lock);
    if (old)
        D3dResourceRelease(old);
    return S_OK;
}

/* the blank for the next present: once the previous frame had its intervals, and never before the coming blank. */
static UINT64 schedule(D3dSwapChain *sc, const MIME_VBLANK *vblank, UINT interval, UINT64 now)
{
    UINT64 slot = MimeVblankIndex(vblank, now) + 1;
    if (sc->present_count) {
        UINT64 held = sc->last_blank + D3D_MAX(sc->last_interval, 1);
        if (held > slot)
            slot = held;
    }
    return slot;
}

/* publishes the back buffer as the frame, paced to the display. */
static HRESULT present_composition(D3dSwapChain *sc, UINT interval, UINT flags, const DXGI_PRESENT_PARAMETERS *params)
{
    D3dDevice *device = sc->device;
    D3dContext *c = device->immediate;
    const UINT known = DXGI_PRESENT_TEST | DXGI_PRESENT_DO_NOT_SEQUENCE | DXGI_PRESENT_RESTART |
                       DXGI_PRESENT_DO_NOT_WAIT | 0x10 | 0x20 | 0x40 | 0x100 | DXGI_PRESENT_ALLOW_TEARING;
    if (interval > 4 || (flags & ~known))
        return DXGI_ERROR_INVALID_CALL;
    if ((flags & DXGI_PRESENT_TEST) && flags != DXGI_PRESENT_TEST)
        return DXGI_ERROR_INVALID_CALL;
    if ((flags & DXGI_PRESENT_ALLOW_TEARING) && (interval || !(sc->desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)))
        return DXGI_ERROR_INVALID_CALL;
    if (device->removed != S_OK)
        return DXGI_ERROR_DEVICE_REMOVED;
    if (params) {
        /* dirty rects must be non-empty and inside the buffer. a scroll needs dirty rects and a scroll rect. */
        RECT buffer = {0, 0, (LONG)sc->desc.Width, (LONG)sc->desc.Height};
        if (params->DirtyRectsCount && !params->pDirtyRects)
            return DXGI_ERROR_INVALID_CALL;
        for (UINT i = 0; i < params->DirtyRectsCount; i++) {
            RECT r = params->pDirtyRects[i], clipped;
            if (r.left >= r.right || r.top >= r.bottom || !IntersectRect(&clipped, &r, &buffer) ||
                !EqualRect(&clipped, &r))
                return DXGI_ERROR_INVALID_CALL;
        }
        if (params->pScrollRect || params->pScrollOffset) {
            if (!params->DirtyRectsCount || !params->pScrollRect)
                return DXGI_ERROR_INVALID_CALL;
            RECT r = *params->pScrollRect, clipped, moved = r;
            if (r.left >= r.right || r.top >= r.bottom || !IntersectRect(&clipped, &r, &buffer) ||
                !EqualRect(&clipped, &r))
                return DXGI_ERROR_INVALID_CALL;
            if (params->pScrollOffset) {
                OffsetRect(&moved, params->pScrollOffset->x, params->pScrollOffset->y);
                if (!IntersectRect(&clipped, &moved, &buffer) || !EqualRect(&clipped, &moved))
                    return DXGI_ERROR_INVALID_CALL;
            }
        }
    }
    if (flags & DXGI_PRESENT_TEST)
        return S_OK;

    /* keep the display at most the latency's worth of frames behind. */
    MIME_VBLANK vblank;
    MimeVblankInfo(&vblank);
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    UINT64 now = (UINT64)counter.QuadPart;
    UINT latency = sc->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT ? sc->max_latency
                                                                                        : device->max_frame_latency;
    latency = D3D_MAX(1, D3D_MIN(latency, 15));
    for (;;) {
        UINT64 current = MimeVblankIndex(&vblank, now), oldest = 0;
        UINT ahead = 0;
        for (UINT i = 0; i < SHOWN_FRAMES; i++)
            if (sc->shown[i].id && sc->shown[i].blank > current) {
                if (!ahead++ || sc->shown[i].blank < oldest)
                    oldest = sc->shown[i].blank;
            }
        if (ahead < latency)
            break;
        if (flags & DXGI_PRESENT_DO_NOT_WAIT)
            return DXGI_ERROR_WAS_STILL_DRAWING;
        /* wait for the oldest pending frame to show to make room. */
        UINT64 until = MimeVblankTime(&vblank, oldest);
        if (until > now)
            Sleep((DWORD)((until - now) * 1000 / vblank.frequency) + 1);
        QueryPerformanceCounter(&counter);
        now = (UINT64)counter.QuadPart;
    }

    if (!sc->front || sc->front->tex.width != sc->back->tex.width || sc->front->tex.height != sc->back->tex.height) {
        HRESULT hr = create_front(sc);
        if (FAILED(hr))
            return hr;
    }
    D3dCtxCopyResource(c, sc->front->iface, sc->back->iface);
    D3dContextSubmit(c, nullptr, nullptr);

    UINT64 slot = schedule(sc, &vblank, interval, now);
    sc->present_count++;
    sc->shown[sc->present_count % SHOWN_FRAMES].id = sc->present_count;
    sc->shown[sc->present_count % SHOWN_FRAMES].blank = slot;
    sc->last_blank = slot;
    sc->last_interval = interval;
    EnterCriticalSection(&sc->frame_lock);
    sc->frame_serial++;
    LeaveCriticalSection(&sc->frame_lock);
    if (sc->latency)
        ReleaseSemaphore(sc->latency, 1, nullptr);
    return device->removed != S_OK ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
}

/* called after the device lock is released, so listeners may draw with the device. */
static void notify_listeners(D3dSwapChain *sc)
{
    struct {
        MIME_COMPOSITION_LISTENER function;
        void *context;
    } copy[MAX_LISTENERS];
    EnterCriticalSection(&sc->frame_lock);
    memcpy(copy, sc->listeners, sizeof(copy));
    LeaveCriticalSection(&sc->frame_lock);
    for (UINT i = 0; i < MAX_LISTENERS; i++)
        if (copy[i].function)
            copy[i].function(copy[i].context, (IUnknown *)&sc->iface);
}

/* ------------------------------------------------------------------ IDXGISwapChain4 */

static HRESULT STDMETHODCALLTYPE sc_query(IDXGISwapChain4 *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDXGIObject) ||
        IsEqualGUID(riid, &IID_IDXGIDeviceSubObject) || IsEqualGUID(riid, &IID_IDXGISwapChain) ||
        IsEqualGUID(riid, &IID_IDXGISwapChain1) || IsEqualGUID(riid, &IID_IDXGISwapChain2) ||
        IsEqualGUID(riid, &IID_IDXGISwapChain3) || IsEqualGUID(riid, &IID_IDXGISwapChain4)) {
        InterlockedIncrement(&impl(iface)->refs);
        *out = iface;
        return S_OK;
    }
    D3dSwapChain *sc = impl(iface);
    if (IsEqualGUID(riid, &IID_IDXGISwapChainMedia)) {
        InterlockedIncrement(&sc->refs);
        *out = &sc->media;
        return S_OK;
    }
    if (sc->composition && IsEqualGUID(riid, &IID_IMimeDXGICompositionSource)) {
        InterlockedIncrement(&sc->refs);
        *out = &sc->source;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE sc_add_ref(IDXGISwapChain4 *iface) { return InterlockedIncrement(&impl(iface)->refs); }

static void leave_fullscreen(D3dSwapChain *sc);

static ULONG STDMETHODCALLTYPE sc_release(IDXGISwapChain4 *iface)
{
    D3dSwapChain *sc = impl(iface);
    ULONG refs = InterlockedDecrement(&sc->refs);
    if (refs)
        return refs;
    D3dDevice *device = sc->device;
    if (sc->fullscreen_state)
        leave_fullscreen(sc);
    destroy_swapchain(sc);
    D3dVk *vk = &device->vk;
    for (UINT i = 0; i < SEMAPHORES; i++)
        if (sc->acquire[i])
            vk->vkDestroySemaphore(vk->device, sc->acquire[i], nullptr);
    if (sc->surface)
        vk->vkDestroySurfaceKHR(vk->instance, sc->surface, nullptr);
    if (sc->back)
        D3dResourceRelease(sc->back);
    if (sc->resolved)
        D3dResourceRelease(sc->resolved);
    if (sc->front)
        D3dResourceRelease(sc->front);
    if (sc->composition)
        DeleteCriticalSection(&sc->frame_lock);
    if (sc->surface_handle)
        CloseHandle(sc->surface_handle);
    if (sc->latency)
        CloseHandle(sc->latency);
    MimePrivateClear(&sc->data);
    sc->factory->lpVtbl->Release(sc->factory);
    device->iface.lpVtbl->Release(&device->iface);
    HeapFree(GetProcessHeap(), 0, sc);
    return 0;
}

static HRESULT STDMETHODCALLTYPE sc_set_private_data(IDXGISwapChain4 *iface, REFGUID guid, UINT size,
                                                     const void *data)
{
    return MimePrivateSet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE sc_set_private_data_interface(IDXGISwapChain4 *iface, REFGUID guid,
                                                               const IUnknown *unknown)
{
    return MimePrivateSetInterface(&impl(iface)->data, guid, unknown);
}

static HRESULT STDMETHODCALLTYPE sc_get_private_data(IDXGISwapChain4 *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE sc_get_parent(IDXGISwapChain4 *iface, REFIID riid, void **parent)
{
    IDXGIFactory *factory = impl(iface)->factory;
    return factory->lpVtbl->QueryInterface(factory, riid, parent);
}

static HRESULT STDMETHODCALLTYPE sc_get_device(IDXGISwapChain4 *iface, REFIID riid, void **device)
{
    D3dDevice *d = impl(iface)->device;
    return d->iface.lpVtbl->QueryInterface(&d->iface, riid, device);
}

static HRESULT present_locked(D3dSwapChain *sc, UINT interval, UINT flags, const DXGI_PRESENT_PARAMETERS *params)
{
    EnterCriticalSection(&sc->device->immediate->lock);
    HRESULT hr = sc->composition ? present_composition(sc, interval, flags, params) : present(sc, interval, flags);
    LeaveCriticalSection(&sc->device->immediate->lock);
    /* notify only once the device is free again. */
    if (SUCCEEDED(hr) && sc->composition && !(flags & DXGI_PRESENT_TEST))
        notify_listeners(sc);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sc_present(IDXGISwapChain4 *iface, UINT interval, UINT flags)
{
    return present_locked(impl(iface), interval, flags, nullptr);
}

static HRESULT STDMETHODCALLTYPE sc_get_buffer(IDXGISwapChain4 *iface, UINT index, REFIID riid, void **surface)
{
    D3dSwapChain *sc = impl(iface);
    if (!surface)
        return DXGI_ERROR_INVALID_CALL;
    *surface = nullptr;
    BOOL flip = sc->desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL ||
                sc->desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (index && (!flip || index >= sc->desc.BufferCount))
        return DXGI_ERROR_INVALID_CALL;
    return sc->back->iface->lpVtbl->QueryInterface(sc->back->iface, riid, surface);
}

/*
 * fullscreen like dxgi does it, without a mode change: the window loses its frame and edges and covers the output,
 * topmost. leaving restores its styles, position and size.
 */
static BOOL fullscreen_style(LONG style, LONG ex_style)
{
    return !(style & 0x80CF0000) && (ex_style & 0x789) == WS_EX_TOPMOST;
}

static void enter_fullscreen(D3dSwapChain *sc, IDXGIOutput *output)
{
    LONG style = GetWindowLongW(sc->window, GWL_STYLE), ex_style = GetWindowLongW(sc->window, GWL_EXSTYLE);
    if (!fullscreen_style(style, ex_style)) {
        GetWindowRect(sc->window, &sc->windowed_rect);
        sc->windowed_style = style;
        sc->windowed_ex_style = ex_style;
        sc->windowed_saved = TRUE;
        SetWindowLongW(sc->window, GWL_STYLE, style & 0x7F30FFFF);
        SetWindowLongW(sc->window, GWL_EXSTYLE, ex_style & 0xFFFFF87E);
    }
    DXGI_OUTPUT_DESC desc;
    if (FAILED(output->lpVtbl->GetDesc(output, &desc)))
        return;
    RECT r = desc.DesktopCoordinates;
    SetWindowPos(sc->window, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
    SetWindowPos(sc->window, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
}

/* an output released by its swap chain shows gdi's gamma again. */
static void own(IDXGIOutput *output, BOOL owned)
{
    IMimeDXGIOutput *mime;
    if (SUCCEEDED(output->lpVtbl->QueryInterface(output, &IID_IMimeDXGIOutput, (void **)&mime))) {
        mime->lpVtbl->SetFullscreenOwner(mime, owned);
        mime->lpVtbl->Release(mime);
    }
}

static void disown(D3dSwapChain *sc)
{
    if (!sc->owned)
        return;
    own(sc->owned, FALSE);
    sc->owned->lpVtbl->Release(sc->owned);
    sc->owned = nullptr;
}

static void leave_fullscreen(D3dSwapChain *sc)
{
    disown(sc);
    if (!sc->windowed_saved)
        return;
    SetWindowLongW(sc->window, GWL_STYLE, sc->windowed_style);
    SetWindowLongW(sc->window, GWL_EXSTYLE, sc->windowed_ex_style);
    RECT r = sc->windowed_rect;
    SetWindowPos(sc->window, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
    sc->windowed_saved = FALSE;
}

static HRESULT STDMETHODCALLTYPE sc_set_fullscreen_state(IDXGISwapChain4 *iface, BOOL fullscreen, IDXGIOutput *target)
{
    D3dSwapChain *sc = impl(iface);
    if (sc->composition)
        return DXGI_ERROR_INVALID_CALL;
    if (!fullscreen && target)
        return DXGI_ERROR_INVALID_CALL;
    if (fullscreen) {
        IDXGIOutput *output = target;
        if (output)
            output->lpVtbl->AddRef(output);
        else if (FAILED(iface->lpVtbl->GetContainingOutput(iface, &output)))
            return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
        disown(sc);
        enter_fullscreen(sc, output);
        own(output, TRUE);
        sc->owned = output;
    } else if (sc->fullscreen_state) {
        leave_fullscreen(sc);
    }
    sc->fullscreen_state = !!fullscreen;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_containing_output(IDXGISwapChain4 *iface, IDXGIOutput **output)
{
    if (!output)
        return DXGI_ERROR_INVALID_CALL;
    if (impl(iface)->composition) {
        *output = nullptr;
        return DXGI_ERROR_INVALID_CALL;
    }
    IDXGIOutput *owned = impl(iface)->owned;
    if (owned) {
        owned->lpVtbl->AddRef(owned);
        *output = owned;
        return S_OK;
    }
    IDXGIAdapter *adapter = impl(iface)->device->adapter;
    return adapter->lpVtbl->EnumOutputs(adapter, 0, output);
}

static HRESULT STDMETHODCALLTYPE sc_get_fullscreen_state(IDXGISwapChain4 *iface, BOOL *fullscreen,
                                                         IDXGIOutput **target)
{
    D3dSwapChain *sc = impl(iface);
    if (fullscreen)
        *fullscreen = sc->fullscreen_state;
    if (target) {
        *target = nullptr;
        if (sc->fullscreen_state)
            sc_get_containing_output(iface, target);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_desc(IDXGISwapChain4 *iface, DXGI_SWAP_CHAIN_DESC *desc)
{
    D3dSwapChain *sc = impl(iface);
    if (!desc)
        return E_INVALIDARG;
    memset(desc, 0, sizeof(*desc));
    desc->BufferDesc.Width = sc->desc.Width;
    desc->BufferDesc.Height = sc->desc.Height;
    desc->BufferDesc.RefreshRate = sc->fullscreen.RefreshRate;
    desc->BufferDesc.Format = sc->desc.Format;
    desc->BufferDesc.ScanlineOrdering = sc->fullscreen.ScanlineOrdering;
    desc->BufferDesc.Scaling = sc->fullscreen.Scaling;
    desc->SampleDesc = sc->desc.SampleDesc;
    desc->BufferUsage = sc->desc.BufferUsage;
    desc->BufferCount = sc->desc.BufferCount;
    desc->OutputWindow = sc->window;
    desc->Windowed = !sc->fullscreen_state;
    desc->SwapEffect = sc->desc.SwapEffect;
    desc->Flags = sc->desc.Flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_resize_buffers(IDXGISwapChain4 *iface, UINT count, UINT width, UINT height,
                                                   DXGI_FORMAT format, UINT flags)
{
    D3dSwapChain *sc = impl(iface);
    /* no window to take the size from. */
    if (sc->composition && (!width || !height))
        return DXGI_ERROR_INVALID_CALL;
    if (sc->back && sc->back->child.refs > 1)
        return DXGI_ERROR_INVALID_CALL;
    D3dContextFlush(sc->device->immediate);
    if (count)
        sc->desc.BufferCount = count;
    if (format != DXGI_FORMAT_UNKNOWN)
        sc->desc.Format = format;
    sc->desc.Width = width;
    sc->desc.Height = height;
    sc->desc.Flags = flags;
    D3dResource *old = sc->back;
    sc->back = nullptr;
    HRESULT hr = create_back_buffer(sc);
    if (FAILED(hr)) {
        sc->back = old;
        return hr;
    }
    if (old)
        D3dResourceRelease(old);
    if (sc->resolved) {
        D3dResourceRelease(sc->resolved);
        sc->resolved = nullptr;
    }
    sc->source_width = sc->source_height = 0;
    if (sc->swapchain && choose_format(sc) != sc->format) {
        VkResult r = create_swapchain(sc, sc->mode);
        if (r != VK_SUCCESS && r != VK_NOT_READY)
            return D3dCheckResult(sc->device, r);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_resize_target(IDXGISwapChain4 *iface, const DXGI_MODE_DESC *mode)
{
    D3dSwapChain *sc = impl(iface);
    if (!mode || sc->composition)
        return DXGI_ERROR_INVALID_CALL;
    if (sc->fullscreen_state || !mode->Width || !mode->Height)
        return S_OK;
    RECT rect = {0, 0, mode->Width, mode->Height};
    AdjustWindowRectEx(&rect, GetWindowLongW(sc->window, GWL_STYLE), FALSE, GetWindowLongW(sc->window, GWL_EXSTYLE));
    SetWindowPos(sc->window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    return S_OK;
}

/*
 * the last present that reached the display, the blank it showed at and the latest blank. until a frame has shown
 * the statistics are disjoint, like dxgi's.
 */
static HRESULT frame_statistics(D3dSwapChain *sc, UINT *present_count, UINT *present_refresh, UINT *sync_refresh,
                                LARGE_INTEGER *sync_time)
{
    MIME_VBLANK vblank;
    MimeVblankInfo(&vblank);
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    UINT64 current = MimeVblankIndex(&vblank, (UINT64)counter.QuadPart);
    UINT found = 0;
    UINT64 blank = 0;
    for (UINT i = 0; i < SHOWN_FRAMES; i++)
        if (sc->shown[i].id && sc->shown[i].blank <= current && sc->shown[i].id > found) {
            found = sc->shown[i].id;
            blank = sc->shown[i].blank;
        }
    *present_count = *present_refresh = *sync_refresh = 0;
    sync_time->QuadPart = 0;
    if (!sc->composition || !found)
        return DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
    *present_count = found;
    *present_refresh = (UINT)blank;
    *sync_refresh = (UINT)current;
    sync_time->QuadPart = (LONGLONG)MimeVblankTime(&vblank, current);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_frame_statistics(IDXGISwapChain4 *iface, DXGI_FRAME_STATISTICS *stats)
{
    if (!stats)
        return E_INVALIDARG;
    memset(stats, 0, sizeof(*stats));
    return frame_statistics(impl(iface), &stats->PresentCount, &stats->PresentRefreshCount, &stats->SyncRefreshCount,
                            &stats->SyncQPCTime);
}

static HRESULT STDMETHODCALLTYPE sc_get_last_present_count(IDXGISwapChain4 *iface, UINT *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = impl(iface)->present_count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_desc1(IDXGISwapChain4 *iface, DXGI_SWAP_CHAIN_DESC1 *desc)
{
    if (!desc)
        return E_INVALIDARG;
    *desc = impl(iface)->desc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_fullscreen_desc(IDXGISwapChain4 *iface, DXGI_SWAP_CHAIN_FULLSCREEN_DESC *desc)
{
    if (!desc)
        return E_INVALIDARG;
    if (impl(iface)->composition) {
        memset(desc, 0, sizeof(*desc));
        return DXGI_ERROR_INVALID_CALL;
    }
    *desc = impl(iface)->fullscreen;
    desc->Windowed = !impl(iface)->fullscreen_state;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_hwnd(IDXGISwapChain4 *iface, HWND *hwnd)
{
    if (!hwnd)
        return DXGI_ERROR_INVALID_CALL;
    *hwnd = impl(iface)->window;
    return impl(iface)->composition ? DXGI_ERROR_INVALID_CALL : S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_core_window(IDXGISwapChain4 *iface, REFIID riid, void **window)
{
    if (window)
        *window = nullptr;
    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE sc_present1(IDXGISwapChain4 *iface, UINT interval, UINT flags,
                                             const DXGI_PRESENT_PARAMETERS *parameters)
{
    return present_locked(impl(iface), interval, flags, parameters);
}

static BOOL STDMETHODCALLTYPE sc_is_temporary_mono_supported(IDXGISwapChain4 *iface) { return FALSE; }

static HRESULT STDMETHODCALLTYPE sc_get_restrict_to_output(IDXGISwapChain4 *iface, IDXGIOutput **output)
{
    if (!output)
        return E_INVALIDARG;
    *output = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_set_background_color(IDXGISwapChain4 *iface, const DXGI_RGBA *color)
{
    if (!color)
        return E_INVALIDARG;
    impl(iface)->background = *color;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_background_color(IDXGISwapChain4 *iface, DXGI_RGBA *color)
{
    if (!color)
        return E_INVALIDARG;
    *color = impl(iface)->background;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_set_rotation(IDXGISwapChain4 *iface, DXGI_MODE_ROTATION rotation)
{
    if (rotation != DXGI_MODE_ROTATION_IDENTITY)
        return DXGI_ERROR_UNSUPPORTED;
    impl(iface)->rotation = rotation;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_rotation(IDXGISwapChain4 *iface, DXGI_MODE_ROTATION *rotation)
{
    if (!rotation)
        return E_INVALIDARG;
    *rotation = DXGI_MODE_ROTATION_IDENTITY;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_set_source_size(IDXGISwapChain4 *iface, UINT width, UINT height)
{
    D3dSwapChain *sc = impl(iface);
    if (!width || !height || width > sc->desc.Width || height > sc->desc.Height)
        return E_INVALIDARG;
    sc->source_width = width;
    sc->source_height = height;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_source_size(IDXGISwapChain4 *iface, UINT *width, UINT *height)
{
    D3dSwapChain *sc = impl(iface);
    if (!width || !height)
        return E_INVALIDARG;
    *width = sc->source_width ? sc->source_width : sc->desc.Width;
    *height = sc->source_height ? sc->source_height : sc->desc.Height;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_set_maximum_frame_latency(IDXGISwapChain4 *iface, UINT latency)
{
    D3dSwapChain *sc = impl(iface);
    if (!(sc->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) || !latency || latency > 16)
        return DXGI_ERROR_INVALID_CALL;
    sc->max_latency = latency;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_maximum_frame_latency(IDXGISwapChain4 *iface, UINT *latency)
{
    D3dSwapChain *sc = impl(iface);
    if (!latency || !(sc->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
        return DXGI_ERROR_INVALID_CALL;
    *latency = sc->max_latency;
    return S_OK;
}

/* each caller gets its own handle to close. */
static HANDLE STDMETHODCALLTYPE sc_get_frame_latency_waitable_object(IDXGISwapChain4 *iface)
{
    D3dSwapChain *sc = impl(iface);
    HANDLE handle = nullptr;
    if (sc->latency)
        DuplicateHandle(GetCurrentProcess(), sc->latency, GetCurrentProcess(), &handle, 0, FALSE,
                        DUPLICATE_SAME_ACCESS);
    return handle;
}

/* only scale and translation, no skew or rotation. */
static HRESULT STDMETHODCALLTYPE sc_set_matrix_transform(IDXGISwapChain4 *iface, const DXGI_MATRIX_3X2_F *matrix)
{
    D3dSwapChain *sc = impl(iface);
    if (!matrix || !sc->composition)
        return DXGI_ERROR_INVALID_CALL;
    const float *f = &matrix->_11;
    for (UINT i = 0; i < 6; i++)
        if (!(f[i] - f[i] == 0.0f))
            return DXGI_ERROR_INVALID_CALL;
    if (matrix->_12 != 0.0f || matrix->_21 != 0.0f)
        return DXGI_ERROR_INVALID_CALL;
    EnterCriticalSection(&sc->frame_lock);
    sc->matrix = *matrix;
    LeaveCriticalSection(&sc->frame_lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_matrix_transform(IDXGISwapChain4 *iface, DXGI_MATRIX_3X2_F *matrix)
{
    D3dSwapChain *sc = impl(iface);
    if (!matrix || !sc->composition)
        return DXGI_ERROR_INVALID_CALL;
    EnterCriticalSection(&sc->frame_lock);
    *matrix = sc->matrix;
    LeaveCriticalSection(&sc->frame_lock);
    return S_OK;
}

static UINT STDMETHODCALLTYPE sc_get_current_back_buffer_index(IDXGISwapChain4 *iface)
{
    D3dSwapChain *sc = impl(iface);
    return sc->desc.BufferCount ? sc->present_count % sc->desc.BufferCount : 0;
}

static HRESULT STDMETHODCALLTYPE sc_check_color_space_support(IDXGISwapChain4 *iface, DXGI_COLOR_SPACE_TYPE space,
                                                              UINT *support)
{
    if (!support)
        return E_INVALIDARG;
    *support = space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ? 1 /* DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT */
                                                                : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_set_color_space1(IDXGISwapChain4 *iface, DXGI_COLOR_SPACE_TYPE space)
{
    if (space != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
        return E_INVALIDARG;
    impl(iface)->color_space = space;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_resize_buffers1(IDXGISwapChain4 *iface, UINT count, UINT width, UINT height,
                                                    DXGI_FORMAT format, UINT flags, const UINT *node_masks,
                                                    IUnknown *const *queues)
{
    return sc_resize_buffers(iface, count, width, height, format, flags);
}

static HRESULT STDMETHODCALLTYPE sc_set_hdr_meta_data(IDXGISwapChain4 *iface, DXGI_HDR_METADATA_TYPE type, UINT size,
                                                      void *metadata)
{
    return S_OK;
}

static const IDXGISwapChain4Vtbl sc_vtbl = {
    sc_query,
    sc_add_ref,
    sc_release,
    sc_set_private_data,
    sc_set_private_data_interface,
    sc_get_private_data,
    sc_get_parent,
    sc_get_device,
    sc_present,
    sc_get_buffer,
    sc_set_fullscreen_state,
    sc_get_fullscreen_state,
    sc_get_desc,
    sc_resize_buffers,
    sc_resize_target,
    sc_get_containing_output,
    sc_get_frame_statistics,
    sc_get_last_present_count,
    sc_get_desc1,
    sc_get_fullscreen_desc,
    sc_get_hwnd,
    sc_get_core_window,
    sc_present1,
    sc_is_temporary_mono_supported,
    sc_get_restrict_to_output,
    sc_set_background_color,
    sc_get_background_color,
    sc_set_rotation,
    sc_get_rotation,
    sc_set_source_size,
    sc_get_source_size,
    sc_set_maximum_frame_latency,
    sc_get_maximum_frame_latency,
    sc_get_frame_latency_waitable_object,
    sc_set_matrix_transform,
    sc_get_matrix_transform,
    sc_get_current_back_buffer_index,
    sc_check_color_space_support,
    sc_set_color_space1,
    sc_resize_buffers1,
    sc_set_hdr_meta_data,
};

/* ------------------------------------------------------------------ composition source */

static HRESULT STDMETHODCALLTYPE src_query(IMimeDXGICompositionSource *iface, REFIID riid, void **out)
{
    return sc_query(&source_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE src_add_ref(IMimeDXGICompositionSource *iface)
{
    return sc_add_ref(&source_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE src_release(IMimeDXGICompositionSource *iface)
{
    return sc_release(&source_impl(iface)->iface);
}

/* S_FALSE until the first Present. */
static HRESULT STDMETHODCALLTYPE src_get_frame(IMimeDXGICompositionSource *iface, MIME_COMPOSITION_FRAME *frame)
{
    D3dSwapChain *sc = source_impl(iface);
    if (!frame)
        return E_INVALIDARG;
    memset(frame, 0, sizeof(*frame));
    EnterCriticalSection(&sc->frame_lock);
    D3dResource *front = sc->front;
    if (front) {
        front->iface->lpVtbl->AddRef(front->iface);
        frame->Texture = (IUnknown *)front->iface;
    }
    frame->Serial = sc->frame_serial;
    frame->Matrix = sc->matrix;
    LeaveCriticalSection(&sc->frame_lock);
    if (!front)
        return S_FALSE;
    frame->Width = front->tex.width;
    frame->Height = front->tex.height;
    frame->Source.right = sc->source_width && sc->source_width <= frame->Width ? (LONG)sc->source_width : (LONG)frame->Width;
    frame->Source.bottom =
        sc->source_height && sc->source_height <= frame->Height ? (LONG)sc->source_height : (LONG)frame->Height;
    frame->ViewFormat = view_format(sc->desc.Format);
    frame->AlphaMode = sc->desc.AlphaMode;
    frame->ColorSpace = sc->color_space;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE src_add_listener(IMimeDXGICompositionSource *iface, MIME_COMPOSITION_LISTENER function,
                                                  void *context)
{
    D3dSwapChain *sc = source_impl(iface);
    HRESULT hr = E_OUTOFMEMORY;
    EnterCriticalSection(&sc->frame_lock);
    for (UINT i = 0; i < MAX_LISTENERS && FAILED(hr); i++)
        if (!sc->listeners[i].function) {
            sc->listeners[i].function = function;
            sc->listeners[i].context = context;
            hr = S_OK;
        }
    LeaveCriticalSection(&sc->frame_lock);
    return hr;
}

static void STDMETHODCALLTYPE src_remove_listener(IMimeDXGICompositionSource *iface, void *context)
{
    D3dSwapChain *sc = source_impl(iface);
    EnterCriticalSection(&sc->frame_lock);
    for (UINT i = 0; i < MAX_LISTENERS; i++)
        if (sc->listeners[i].function && sc->listeners[i].context == context)
            memset(&sc->listeners[i], 0, sizeof(sc->listeners[i]));
    LeaveCriticalSection(&sc->frame_lock);
}

static const IMimeDXGICompositionSourceVtbl src_vtbl = {
    src_query, src_add_ref, src_release, src_get_frame, src_add_listener, src_remove_listener,
};

/* ------------------------------------------------------------------ IDXGISwapChainMedia */

static HRESULT STDMETHODCALLTYPE media_query(IDXGISwapChainMedia *iface, REFIID riid, void **out)
{
    return sc_query(&media_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE media_add_ref(IDXGISwapChainMedia *iface)
{
    return sc_add_ref(&media_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE media_release(IDXGISwapChainMedia *iface)
{
    return sc_release(&media_impl(iface)->iface);
}

static HRESULT STDMETHODCALLTYPE media_get_frame_statistics(IDXGISwapChainMedia *iface,
                                                            DXGI_FRAME_STATISTICS_MEDIA *stats)
{
    D3dSwapChain *sc = media_impl(iface);
    if (!stats)
        return DXGI_ERROR_INVALID_CALL;
    memset(stats, 0, sizeof(*stats));
    HRESULT hr = frame_statistics(sc, &stats->PresentCount, &stats->PresentRefreshCount, &stats->SyncRefreshCount,
                                  &stats->SyncQPCTime);
    stats->CompositionMode = DXGI_FRAME_PRESENTATION_MODE_COMPOSED;
    stats->ApprovedPresentDuration = sc->present_duration;
    return hr;
}

static HRESULT STDMETHODCALLTYPE media_set_present_duration(IDXGISwapChainMedia *iface, UINT duration)
{
    media_impl(iface)->present_duration = duration;
    return S_OK;
}

/* the display has one rate, so the only supported duration is its period, in 100 ns units. */
static HRESULT STDMETHODCALLTYPE media_check_present_duration_support(IDXGISwapChainMedia *iface, UINT desired,
                                                                      UINT *smaller, UINT *larger)
{
    if (!smaller || !larger)
        return E_INVALIDARG;
    UINT period = 10000000 / MimeRefreshRate();
    if (desired > period) {
        *smaller = period;
        *larger = 0;
    } else if (desired >= period) {
        *smaller = *larger = desired;
    } else {
        *smaller = 0;
        *larger = period;
    }
    return S_OK;
}

static const IDXGISwapChainMediaVtbl media_vtbl = {
    media_query,
    media_add_ref,
    media_release,
    media_get_frame_statistics,
    media_set_present_duration,
    media_check_present_duration_support,
};

HRESULT D3dCreateSwapChain(D3dDevice *device, IDXGIFactory *factory, HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
                           const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen, IDXGISwapChain1 **out)
{
    if (!desc->BufferCount || desc->BufferCount > DXGI_MAX_SWAP_CHAIN_BUFFERS || !desc->SampleDesc.Count)
        return DXGI_ERROR_INVALID_CALL;
    BOOL flip = desc->SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || desc->SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (flip && (desc->BufferCount < 2 || desc->SampleDesc.Count > 1))
        return DXGI_ERROR_INVALID_CALL;
    D3dSwapChain *sc = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sc));
    if (!sc)
        return E_OUTOFMEMORY;
    sc->iface.lpVtbl = &sc_vtbl;
    sc->source.lpVtbl = &src_vtbl;
    sc->media.lpVtbl = &media_vtbl;
    sc->refs = 1;
    sc->device = device;
    sc->factory = factory;
    sc->window = window;
    sc->desc = *desc;
    if (fullscreen)
        sc->fullscreen = *fullscreen;
    else
        sc->fullscreen.Windowed = TRUE;
    sc->fullscreen_state = !sc->fullscreen.Windowed;
    sc->max_latency = 1;
    sc->color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    sc->rotation = DXGI_MODE_ROTATION_IDENTITY;
    HRESULT hr = create_back_buffer(sc);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, sc);
        return hr;
    }
    VkWin32SurfaceCreateInfoKHR surface = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surface.hinstance = GetModuleHandleW(nullptr);
    surface.hwnd = window;
    if (device->vk.vkCreateWin32SurfaceKHR(device->vk.instance, &surface, nullptr, &sc->surface) != VK_SUCCESS) {
        D3dResourceRelease(sc->back);
        HeapFree(GetProcessHeap(), 0, sc);
        return DXGI_ERROR_UNSUPPORTED;
    }
    if (desc->Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
        sc->latency = CreateSemaphoreW(nullptr, sc->max_latency, 16, nullptr);
    factory->lpVtbl->AddRef(factory);
    device->iface.lpVtbl->AddRef(&device->iface);
    if (sc->fullscreen_state) {
        sc->fullscreen_state = FALSE;
        sc_set_fullscreen_state(&sc->iface, TRUE, nullptr);
    }
    *out = (IDXGISwapChain1 *)&sc->iface;
    return S_OK;
}

/* the checks are dxgi's for a windowless flip model chain. */
HRESULT D3dCreateCompositionSwapChain(D3dDevice *device, IDXGIFactory *factory, HANDLE surface,
                                      const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGISwapChain1 **out)
{
    if (!desc->Width || !desc->Height)
        return DXGI_ERROR_INVALID_CALL;
    if (desc->AlphaMode > DXGI_ALPHA_MODE_IGNORE || desc->AlphaMode == DXGI_ALPHA_MODE_STRAIGHT)
        return DXGI_ERROR_INVALID_CALL;
    if (desc->SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL && desc->SwapEffect != DXGI_SWAP_EFFECT_FLIP_DISCARD)
        return DXGI_ERROR_INVALID_CALL;
    if (desc->BufferCount < 2 || desc->BufferCount > DXGI_MAX_SWAP_CHAIN_BUFFERS)
        return DXGI_ERROR_INVALID_CALL;
    switch (desc->Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        break;
    default:
        return DXGI_ERROR_INVALID_CALL;
    }
    if (desc->SampleDesc.Count != 1 || desc->SampleDesc.Quality)
        return DXGI_ERROR_INVALID_CALL;
    if (desc->BufferUsage & ~(DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER |
                              DXGI_USAGE_UNORDERED_ACCESS))
        return DXGI_ERROR_INVALID_CALL;
    if (desc->Scaling != DXGI_SCALING_STRETCH || desc->Stereo)
        return DXGI_ERROR_INVALID_CALL;
    UINT flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT |
                 DXGI_SWAP_CHAIN_FLAG_RESTRICT_SHARED_RESOURCE_DRIVER |
                 DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |
                 DXGI_SWAP_CHAIN_FLAG_RESTRICTED_TO_ALL_HOLOGRAPHIC_DISPLAYS | 0x2000;
    /* a surface handle makes a video chain, which also accepts the video flags. */
    if (surface)
        flags |= DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY | DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO |
                 DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO | DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED;
    if (desc->Flags & ~flags)
        return DXGI_ERROR_INVALID_CALL;
    D3dSwapChain *sc = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sc));
    if (!sc)
        return E_OUTOFMEMORY;
    sc->iface.lpVtbl = &sc_vtbl;
    sc->source.lpVtbl = &src_vtbl;
    sc->media.lpVtbl = &media_vtbl;
    sc->refs = 1;
    sc->device = device;
    sc->factory = factory;
    sc->composition = TRUE;
    sc->desc = *desc;
    sc->fullscreen.Windowed = TRUE;
    sc->max_latency = 1;
    sc->color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    sc->rotation = DXGI_MODE_ROTATION_IDENTITY;
    sc->matrix._11 = sc->matrix._22 = 1.0f;
    InitializeCriticalSection(&sc->frame_lock);
    HRESULT hr = create_back_buffer(sc);
    if (FAILED(hr)) {
        DeleteCriticalSection(&sc->frame_lock);
        HeapFree(GetProcessHeap(), 0, sc);
        return hr;
    }
    if (desc->Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
        sc->latency = CreateSemaphoreW(nullptr, sc->max_latency, 16, nullptr);
    if (surface && !DuplicateHandle(GetCurrentProcess(), surface, GetCurrentProcess(), &sc->surface_handle, 0, FALSE,
                                    DUPLICATE_SAME_ACCESS)) {
        D3dResourceRelease(sc->back);
        if (sc->latency)
            CloseHandle(sc->latency);
        DeleteCriticalSection(&sc->frame_lock);
        HeapFree(GetProcessHeap(), 0, sc);
        return DXGI_ERROR_INVALID_CALL;
    }
    factory->lpVtbl->AddRef(factory);
    device->iface.lpVtbl->AddRef(&device->iface);
    *out = (IDXGISwapChain1 *)&sc->iface;
    return S_OK;
}
