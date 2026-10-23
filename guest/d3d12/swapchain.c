/*
 * swap chains of a direct queue. back buffers are textures of their own that the program renders to by index.
 * Present blits the current one, scaled to the client area, to an image of a Vulkan swap chain on the window, on
 * the queue after the program's work, then presents it. sync interval 0 presents immediately, others use fifo.
 */
#include "d3d12_private.h"

#define MAX_IMAGES 8
#define SLOTS 16

typedef struct D12SwapChain {
    IDXGISwapChain4 iface;
    LONG refs;
    D12Queue *queue;
    D12Device *device;
    IDXGIFactory *factory;
    HWND window;
    DXGI_SWAP_CHAIN_DESC1 desc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen;
    BOOL fullscreen_state;
    /* the output a fullscreen swap chain owns and controls the gamma of. */
    IDXGIOutput *owned;
    BOOL windowed_saved;
    RECT windowed_rect;
    LONG windowed_style, windowed_ex_style;
    D12Resource *buffers[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    MimePrivateData data;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;
    VkPresentModeKHR mode;
    UINT image_count;
    VkImage images[MAX_IMAGES];
    VkSemaphore present[MAX_IMAGES];
    /* per frame in flight: acquire semaphore, commands and the serial that used them. */
    VkCommandPool pool;
    VkSemaphore acquire[SLOTS];
    VkCommandBuffer commands[SLOTS];
    UINT64 slot_serial[SLOTS];

    UINT present_count;
    UINT current;
    UINT max_latency;
    HANDLE latency;
    DXGI_COLOR_SPACE_TYPE color_space;
    DXGI_RGBA background;
    UINT source_width, source_height;
} D12SwapChain;

static D12SwapChain *impl(IDXGISwapChain4 *iface) { return CONTAINING_RECORD(iface, D12SwapChain, iface); }

static void wait_serial(D12SwapChain *sc, UINT64 serial)
{
    if (!serial)
        return;
    D12Vk *vk = &sc->device->vk;
    VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &sc->queue->timeline;
    wait.pValues = &serial;
    vk->vkWaitSemaphores(vk->device, &wait, UINT64_MAX);
}

/* ------------------------------------------------------------------ the vulkan swap chain */

static void destroy_swapchain(D12SwapChain *sc)
{
    D12Vk *vk = &sc->device->vk;
    if (!sc->swapchain)
        return;
    D12QueueWaitIdle(sc->queue);
    for (UINT i = 0; i < sc->image_count; i++)
        vk->vkDestroySemaphore(vk->device, sc->present[i], nullptr);
    vk->vkDestroySwapchainKHR(vk->device, sc->swapchain, nullptr);
    sc->swapchain = VK_NULL_HANDLE;
    sc->image_count = 0;
}

static VkFormat choose_format(D12SwapChain *sc)
{
    D12Vk *vk = &sc->device->vk;
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

static BOOL create_swapchain(D12SwapChain *sc, VkPresentModeKHR mode)
{
    D12Vk *vk = &sc->device->vk;
    VkSurfaceCapabilitiesKHR caps;
    if (vk->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->physical, sc->surface, &caps) != VK_SUCCESS)
        return FALSE;
    if (!caps.currentExtent.width || !caps.currentExtent.height)
        return FALSE;
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
    info.minImageCount = D12_MAX(caps.minImageCount, 3u);
    if (caps.maxImageCount)
        info.minImageCount = D12_MIN(info.minImageCount, caps.maxImageCount);
    info.imageFormat = sc->format;
    info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    info.imageExtent = caps.currentExtent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = chosen;
    info.clipped = TRUE;
    info.oldSwapchain = sc->swapchain;
    VkSwapchainKHR swapchain;
    if (vk->vkCreateSwapchainKHR(vk->device, &info, nullptr, &swapchain) != VK_SUCCESS)
        return FALSE;
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
    return TRUE;
}

/* committed render targets per the description, which shaders may also read. */
static HRESULT create_buffers(D12SwapChain *sc)
{
    RECT client;
    GetClientRect(sc->window, &client);
    if (!sc->desc.Width)
        sc->desc.Width = D12_MAX(client.right - client.left, 1);
    if (!sc->desc.Height)
        sc->desc.Height = D12_MAX(client.bottom - client.top, 1);
    D3D12_RESOURCE_DESC1 desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = sc->desc.Width;
    desc.Height = sc->desc.Height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = sc->desc.Format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (sc->desc.BufferUsage & DXGI_USAGE_UNORDERED_ACCESS)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    for (UINT i = 0; i < sc->desc.BufferCount; i++) {
        HRESULT hr = D12CreateResource(sc->device, &heap, D3D12_HEAP_FLAG_NONE, &desc, nullptr, 0,
                                       D3D12_RESOURCE_STATE_PRESENT, nullptr, &sc->buffers[i]);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static void release_buffers(D12SwapChain *sc)
{
    for (UINT i = 0; i < DXGI_MAX_SWAP_CHAIN_BUFFERS; i++)
        if (sc->buffers[i]) {
            sc->buffers[i]->iface.lpVtbl->Release(&sc->buffers[i]->iface);
            sc->buffers[i] = nullptr;
        }
}

/* ------------------------------------------------------------------ presenting */

static HRESULT present(D12SwapChain *sc, UINT interval, UINT flags)
{
    D12Device *device = sc->device;
    D12Vk *vk = &device->vk;
    if (interval > 4)
        return DXGI_ERROR_INVALID_CALL;
    if (IsIconic(sc->window))
        return DXGI_STATUS_OCCLUDED;
    if (flags & DXGI_PRESENT_TEST)
        return S_OK;
    D12Resource *back = sc->buffers[sc->current];
    sc->current = (sc->current + 1) % sc->desc.BufferCount;
    VkPresentModeKHR mode = interval ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    if ((!sc->swapchain || sc->mode != mode) && !create_swapchain(sc, mode))
        return S_OK;

    UINT slot = sc->present_count % SLOTS;
    wait_serial(sc, sc->slot_serial[slot]);
    if (!sc->acquire[slot]) {
        VkSemaphoreCreateInfo sem = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vk->vkCreateSemaphore(vk->device, &sem, nullptr, &sc->acquire[slot]);
        VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = sc->pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        vk->vkAllocateCommandBuffers(vk->device, &alloc, &sc->commands[slot]);
    }
    UINT index = 0;
    VkResult r = vk->vkAcquireNextImageKHR(vk->device, sc->swapchain, UINT64_MAX, sc->acquire[slot], VK_NULL_HANDLE,
                                           &index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        if (!create_swapchain(sc, mode))
            return S_OK;
        r = vk->vkAcquireNextImageKHR(vk->device, sc->swapchain, UINT64_MAX, sc->acquire[slot], VK_NULL_HANDLE,
                                      &index);
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        return S_OK;

    VkCommandBuffer cmd = sc->commands[slot];
    vk->vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk->vkBeginCommandBuffer(cmd, &begin);
    VkImageMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = sc->images[index];
    barrier.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    /* makes the program's back buffer writes visible to the blit. */
    VkMemoryBarrier2 written = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    written.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    written.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    written.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    written.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &written;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vk->vkCmdPipelineBarrier2(cmd, &dep);
    dep.memoryBarrierCount = 0;
    VkImageBlit blit = {};
    blit.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    UINT width = sc->source_width ? sc->source_width : sc->desc.Width;
    UINT height = sc->source_height ? sc->source_height : sc->desc.Height;
    blit.srcOffsets[1] = (VkOffset3D){(INT)width, (INT)height, 1};
    blit.dstSubresource = blit.srcSubresource;
    blit.dstOffsets[1] = (VkOffset3D){(INT)sc->extent.width, (INT)sc->extent.height, 1};
    vk->vkCmdBlitImage(cmd, back->image, VK_IMAGE_LAYOUT_GENERAL, sc->images[index],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vk->vkCmdPipelineBarrier2(cmd, &dep);
    vk->vkEndCommandBuffer(cmd);

    VkSemaphoreSubmitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = sc->acquire[slot];
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    VkSemaphoreSubmitInfo signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = sc->present[index];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    UINT64 serial = D12QueueSubmit(sc->queue, &cmd, 1, &wait, 1, &signal, 1);
    sc->slot_serial[slot] = serial;

    VkPresentInfoKHR info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &sc->present[index];
    info.swapchainCount = 1;
    info.pSwapchains = &sc->swapchain;
    info.pImageIndices = &index;
    AcquireSRWLockExclusive(sc->queue->queue_lock);
    vk->vkQueuePresentKHR(sc->queue->queue, &info);
    ReleaseSRWLockExclusive(sc->queue->queue_lock);

    /* no more frames ahead of the gpu than the latency allows, 3 by default like dxgi. */
    sc->present_count++;
    UINT latency = sc->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT ? sc->max_latency : 3;
    latency = D12_MAX(1u, D12_MIN(latency, (UINT)SLOTS - 1));
    if (sc->present_count > latency)
        wait_serial(sc, sc->slot_serial[(sc->present_count - latency - 1) % SLOTS]);
    if (sc->latency)
        ReleaseSemaphore(sc->latency, 1, nullptr);
    return S_OK;
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
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE sc_add_ref(IDXGISwapChain4 *iface) { return InterlockedIncrement(&impl(iface)->refs); }

static void leave_fullscreen(D12SwapChain *sc);

static ULONG STDMETHODCALLTYPE sc_release(IDXGISwapChain4 *iface)
{
    D12SwapChain *sc = impl(iface);
    ULONG refs = InterlockedDecrement(&sc->refs);
    if (refs)
        return refs;
    D12Vk *vk = &sc->device->vk;
    if (sc->fullscreen_state)
        leave_fullscreen(sc);
    D12QueueWaitIdle(sc->queue);
    destroy_swapchain(sc);
    for (UINT i = 0; i < SLOTS; i++)
        if (sc->acquire[i])
            vk->vkDestroySemaphore(vk->device, sc->acquire[i], nullptr);
    vk->vkDestroyCommandPool(vk->device, sc->pool, nullptr);
    if (sc->surface)
        vk->vkDestroySurfaceKHR(vk->instance, sc->surface, nullptr);
    release_buffers(sc);
    if (sc->latency)
        CloseHandle(sc->latency);
    MimePrivateClear(&sc->data);
    sc->factory->lpVtbl->Release(sc->factory);
    sc->queue->iface.lpVtbl->Release(&sc->queue->iface);
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

/* the device of a Direct3D 12 swap chain is its queue. */
static HRESULT STDMETHODCALLTYPE sc_get_device(IDXGISwapChain4 *iface, REFIID riid, void **device)
{
    D12Queue *queue = impl(iface)->queue;
    return queue->iface.lpVtbl->QueryInterface(&queue->iface, riid, device);
}

static HRESULT STDMETHODCALLTYPE sc_present(IDXGISwapChain4 *iface, UINT interval, UINT flags)
{
    return present(impl(iface), interval, flags);
}

static HRESULT STDMETHODCALLTYPE sc_get_buffer(IDXGISwapChain4 *iface, UINT index, REFIID riid, void **surface)
{
    D12SwapChain *sc = impl(iface);
    if (!surface)
        return DXGI_ERROR_INVALID_CALL;
    *surface = nullptr;
    if (index >= sc->desc.BufferCount || !sc->buffers[index])
        return DXGI_ERROR_INVALID_CALL;
    return sc->buffers[index]->iface.lpVtbl->QueryInterface(&sc->buffers[index]->iface, riid, surface);
}

static BOOL fullscreen_style(LONG style, LONG ex_style)
{
    return !(style & 0x80CF0000) && (ex_style & 0x789) == WS_EX_TOPMOST;
}

static void enter_fullscreen(D12SwapChain *sc, IDXGIOutput *output)
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

/* an owned output knows when its swap chain lets go, and shows gdi's gamma again. */
static void own(IDXGIOutput *output, BOOL owned)
{
    IMimeDXGIOutput *mime;
    if (SUCCEEDED(output->lpVtbl->QueryInterface(output, &IID_IMimeDXGIOutput, (void **)&mime))) {
        mime->lpVtbl->SetFullscreenOwner(mime, owned);
        mime->lpVtbl->Release(mime);
    }
}

static void disown(D12SwapChain *sc)
{
    if (!sc->owned)
        return;
    own(sc->owned, FALSE);
    sc->owned->lpVtbl->Release(sc->owned);
    sc->owned = nullptr;
}

static void leave_fullscreen(D12SwapChain *sc)
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

static HRESULT STDMETHODCALLTYPE sc_get_containing_output(IDXGISwapChain4 *iface, IDXGIOutput **output)
{
    if (!output)
        return DXGI_ERROR_INVALID_CALL;
    IDXGIOutput *owned = impl(iface)->owned;
    if (owned) {
        owned->lpVtbl->AddRef(owned);
        *output = owned;
        return S_OK;
    }
    IDXGIAdapter *adapter = impl(iface)->device->adapter;
    return adapter->lpVtbl->EnumOutputs(adapter, 0, output);
}

static HRESULT STDMETHODCALLTYPE sc_set_fullscreen_state(IDXGISwapChain4 *iface, BOOL fullscreen, IDXGIOutput *target)
{
    D12SwapChain *sc = impl(iface);
    if (!fullscreen && target)
        return DXGI_ERROR_INVALID_CALL;
    if (fullscreen) {
        IDXGIOutput *output = target;
        if (output)
            output->lpVtbl->AddRef(output);
        else if (FAILED(sc_get_containing_output(iface, &output)))
            return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
        disown(sc);
        enter_fullscreen(sc, output);
        /* the output is the swap chain's until it leaves fullscreen. */
        own(output, TRUE);
        sc->owned = output;
    } else if (sc->fullscreen_state) {
        leave_fullscreen(sc);
    }
    sc->fullscreen_state = !!fullscreen;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_fullscreen_state(IDXGISwapChain4 *iface, BOOL *fullscreen,
                                                         IDXGIOutput **target)
{
    D12SwapChain *sc = impl(iface);
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
    D12SwapChain *sc = impl(iface);
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
    D12SwapChain *sc = impl(iface);
    /* the program must have let go of every back buffer. */
    for (UINT i = 0; i < DXGI_MAX_SWAP_CHAIN_BUFFERS; i++)
        if (sc->buffers[i] && sc->buffers[i]->child.refs > 1)
            return DXGI_ERROR_INVALID_CALL;
    if (count > DXGI_MAX_SWAP_CHAIN_BUFFERS)
        return DXGI_ERROR_INVALID_CALL;
    D12QueueWaitIdle(sc->queue);
    DXGI_SWAP_CHAIN_DESC1 old = sc->desc;
    if (count)
        sc->desc.BufferCount = count;
    if (format != DXGI_FORMAT_UNKNOWN)
        sc->desc.Format = format;
    sc->desc.Width = width;
    sc->desc.Height = height;
    sc->desc.Flags = flags;
    release_buffers(sc);
    HRESULT hr = create_buffers(sc);
    if (FAILED(hr)) {
        release_buffers(sc);
        sc->desc = old;
        create_buffers(sc);
        return hr;
    }
    sc->current = 0;
    if (sc->swapchain && choose_format(sc) != sc->format)
        create_swapchain(sc, sc->mode);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_resize_target(IDXGISwapChain4 *iface, const DXGI_MODE_DESC *mode)
{
    D12SwapChain *sc = impl(iface);
    if (!mode)
        return DXGI_ERROR_INVALID_CALL;
    if (sc->fullscreen_state || !mode->Width || !mode->Height)
        return S_OK;
    RECT rect = {0, 0, (LONG)mode->Width, (LONG)mode->Height};
    AdjustWindowRectEx(&rect, GetWindowLongW(sc->window, GWL_STYLE), FALSE, GetWindowLongW(sc->window, GWL_EXSTYLE));
    SetWindowPos(sc->window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_frame_statistics(IDXGISwapChain4 *iface, DXGI_FRAME_STATISTICS *stats)
{
    if (!stats)
        return E_INVALIDARG;
    memset(stats, 0, sizeof(*stats));
    return DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
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
    *desc = impl(iface)->fullscreen;
    desc->Windowed = !impl(iface)->fullscreen_state;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_hwnd(IDXGISwapChain4 *iface, HWND *hwnd)
{
    if (!hwnd)
        return DXGI_ERROR_INVALID_CALL;
    *hwnd = impl(iface)->window;
    return S_OK;
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
    return sc_present(iface, interval, flags);
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
    return rotation == DXGI_MODE_ROTATION_IDENTITY ? S_OK : DXGI_ERROR_UNSUPPORTED;
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
    D12SwapChain *sc = impl(iface);
    if (!width || !height || width > sc->desc.Width || height > sc->desc.Height)
        return E_INVALIDARG;
    sc->source_width = width;
    sc->source_height = height;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_source_size(IDXGISwapChain4 *iface, UINT *width, UINT *height)
{
    D12SwapChain *sc = impl(iface);
    if (!width || !height)
        return E_INVALIDARG;
    *width = sc->source_width ? sc->source_width : sc->desc.Width;
    *height = sc->source_height ? sc->source_height : sc->desc.Height;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_set_maximum_frame_latency(IDXGISwapChain4 *iface, UINT latency)
{
    D12SwapChain *sc = impl(iface);
    if (!(sc->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) || !latency || latency > 16)
        return DXGI_ERROR_INVALID_CALL;
    sc->max_latency = latency;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_get_maximum_frame_latency(IDXGISwapChain4 *iface, UINT *latency)
{
    D12SwapChain *sc = impl(iface);
    if (!latency || !(sc->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
        return DXGI_ERROR_INVALID_CALL;
    *latency = sc->max_latency;
    return S_OK;
}

static HANDLE STDMETHODCALLTYPE sc_get_frame_latency_waitable_object(IDXGISwapChain4 *iface)
{
    D12SwapChain *sc = impl(iface);
    HANDLE handle = nullptr;
    if (sc->latency)
        DuplicateHandle(GetCurrentProcess(), sc->latency, GetCurrentProcess(), &handle, 0, FALSE,
                        DUPLICATE_SAME_ACCESS);
    return handle;
}

static HRESULT STDMETHODCALLTYPE sc_set_matrix_transform(IDXGISwapChain4 *iface, const DXGI_MATRIX_3X2_F *matrix)
{
    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE sc_get_matrix_transform(IDXGISwapChain4 *iface, DXGI_MATRIX_3X2_F *matrix)
{
    return DXGI_ERROR_INVALID_CALL;
}

static UINT STDMETHODCALLTYPE sc_get_current_back_buffer_index(IDXGISwapChain4 *iface) { return impl(iface)->current; }

static HRESULT STDMETHODCALLTYPE sc_check_color_space_support(IDXGISwapChain4 *iface, DXGI_COLOR_SPACE_TYPE space,
                                                              UINT *support)
{
    if (!support)
        return E_INVALIDARG;
    *support = space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ? 1 : 0;
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

/* Direct3D 12 swap chains flip: at least two buffers, none multisampled. */
HRESULT D12CreateSwapChain(D12Queue *queue, IDXGIFactory *factory, HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
                           const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen, IDXGISwapChain1 **out)
{
    D12Device *device = queue->child.device;
    BOOL flip = desc->SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || desc->SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (!flip || desc->BufferCount < 2 || desc->BufferCount > DXGI_MAX_SWAP_CHAIN_BUFFERS ||
        desc->SampleDesc.Count != 1)
        return DXGI_ERROR_INVALID_CALL;
    D12SwapChain *sc = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sc));
    if (!sc)
        return E_OUTOFMEMORY;
    sc->iface.lpVtbl = &sc_vtbl;
    sc->refs = 1;
    sc->queue = queue;
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
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queue->family;
    HRESULT hr = device->vk.vkCreateCommandPool(device->vk.device, &pool, nullptr, &sc->pool) == VK_SUCCESS
                     ? create_buffers(sc)
                     : E_OUTOFMEMORY;
    VkWin32SurfaceCreateInfoKHR surface = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surface.hinstance = GetModuleHandleW(nullptr);
    surface.hwnd = window;
    if (SUCCEEDED(hr) &&
        device->vk.vkCreateWin32SurfaceKHR(device->vk.instance, &surface, nullptr, &sc->surface) != VK_SUCCESS)
        hr = DXGI_ERROR_UNSUPPORTED;
    if (FAILED(hr)) {
        release_buffers(sc);
        if (sc->pool)
            device->vk.vkDestroyCommandPool(device->vk.device, sc->pool, nullptr);
        HeapFree(GetProcessHeap(), 0, sc);
        return hr;
    }
    if (desc->Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
        sc->latency = CreateSemaphoreW(nullptr, sc->max_latency, 16, nullptr);
    factory->lpVtbl->AddRef(factory);
    queue->iface.lpVtbl->AddRef(&queue->iface);
    if (sc->fullscreen_state) {
        sc->fullscreen_state = FALSE;
        sc_set_fullscreen_state(&sc->iface, TRUE, nullptr);
    }
    *out = (IDXGISwapChain1 *)&sc->iface;
    return S_OK;
}
