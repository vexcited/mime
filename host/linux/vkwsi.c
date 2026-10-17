/*
 * the guest's swapchains. their images are host images the guest renders
 * to, and a present blits one into a swapchain of the host's own, made on
 * the host window of the guest window, with the window's frame from the
 * guest surface around it. a presenter owns a host window's surface while
 * swapchains present to it, and gives the window back to sdl's renderer
 * once the last one goes.
 *
 * acquire and present come without replies. a binary semaphore per image
 * orders them on the gpu: the blit of a present signals it, and the next
 * acquire of the image waits for it before it signals the guest.
 *
 * the host's acquire waits for the display, which would hold every later
 * call of the process, the next frame's included: a presenting thread per
 * connection does the acquire, the blit and the present. the connection's
 * thread only takes the guest's semaphores, in a submission that signals a
 * timeline value the blit waits for, and queues the present; an acquire of
 * an image whose present the presenting thread has not submitted yet waits
 * for it.
 */
#include "mvk_host.h"
#include "display.h"

struct wl_display;
struct wl_surface;
typedef struct _XDisplay Display;
typedef unsigned long Window;
typedef unsigned long VisualID;
#include <vulkan/vulkan_wayland.h>
#include <vulkan/vulkan_xlib.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_IMAGES 8
#define MAX_HOST_IMAGES 16
#define FRAMES 3
#define MAX_PRESENTERS 64
#define MAX_GRAVE 64
#define ACQUIRE_TIMEOUT_NS 100000000ull

const char *const mvk_wsi_instance_extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
    nullptr,
};

bool mvk_wsi_device_extension(const char *name)
{
    return !strcmp(name, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
}

typedef struct Presenter Presenter;

typedef struct Swapchain {
    struct Swapchain *next;
    uint64_t id;
    Presenter *presenter;
    VkFormat format;
    VkExtent2D extent;
    VkPresentModeKHR mode;
    uint32_t count;
    VkImage images[MAX_IMAGES];
    uint64_t image_ids[MAX_IMAGES];
    VkDeviceMemory memory[MAX_IMAGES];
    VkSemaphore released[MAX_IMAGES]; /* signaled by the last present of the image */
    bool pending[MAX_IMAGES];
    uint32_t queued[MAX_IMAGES]; /* presents of the image the presenting thread did not submit yet */
    bool flip; /* rows bottom first, as gl keeps them */
} Swapchain;

typedef struct Frame {
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore acquired;
} Frame;

struct Presenter {
    uint32_t window;
    MvkConnection *conn;
    int refs;
    pthread_mutex_t lock;
    uint32_t generation; /* counts the host windows lost */
    bool attached;

    VkSurfaceKHR surface;
    uint32_t checked_family;
    bool supported;
    VkSwapchainKHR swapchain;
    VkFormat format;
    bool srgb;
    VkExtent2D extent;
    VkPresentModeKHR wanted_mode;
    bool stale;
    uint32_t count;
    VkImage images[MAX_HOST_IMAGES];
    VkSemaphore done[MAX_HOST_IMAGES];

    VkCommandPool pool;
    uint32_t pool_family;
    Frame frames[FRAMES];
    uint32_t frame;

    /* the guest surface: a copy the presenting thread keeps, and its image. */
    uint8_t *shadow;
    uint32_t shadow_width, shadow_height;
    MimeRect shadow_dirty;
    bool shadow_has_dirty;
    VkImage gdi;
    VkDeviceMemory gdi_memory;
    VkFormat gdi_format;
    uint32_t gdi_width, gdi_height;
    bool gdi_valid;
    VkBuffer staging;
    VkDeviceMemory staging_memory;
    uint8_t *staging_map;

    /* the last image the guest presented, kept for captures of the screen, and where it shows. */
    VkImage last;
    VkDeviceMemory last_memory;
    VkExtent2D last_extent;
    VkFormat last_format;
    bool last_valid;
    MimeRect last_client;
    VkQueue last_queue;
    VkBuffer readback;
    VkDeviceMemory readback_memory;
    uint8_t *readback_map;
    VkDeviceSize readback_size;

    /* semaphores a present may still wait for when the window went: freed once the device is idle. */
    VkSemaphore grave[MAX_GRAVE];
    uint32_t grave_count;
};

/* a present handed to the presenting thread. */
typedef struct PresentJob {
    struct PresentJob *next;
    Swapchain *s;
    uint32_t index, window;
    MimeRect client;
    VkQueue queue;
    uint32_t family;
    uint64_t ready; /* the value of the ready timeline the guest's semaphores signal, 0 for none */
} PresentJob;

typedef struct MvkWsi {
    Swapchain *swapchains;
    /* the presenting thread and its presents, under lock. */
    pthread_t thread;
    bool running, busy, stop;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    PresentJob *first, *last;
    VkSemaphore ready;
    uint64_t ready_value;
    PFN_vkDestroySurfaceKHR DestroySurface;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR SurfaceSupport;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR SurfaceCapabilities;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR SurfaceFormats;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR SurfacePresentModes;
    PFN_vkCreateWaylandSurfaceKHR CreateWaylandSurface;
    PFN_vkCreateXlibSurfaceKHR CreateXlibSurface;
    PFN_vkCreateSwapchainKHR CreateSwapchain;
    PFN_vkDestroySwapchainKHR DestroySwapchain;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImages;
    PFN_vkAcquireNextImageKHR AcquireNextImage;
    PFN_vkQueuePresentKHR QueuePresent;
} MvkWsi;

static pthread_mutex_t g_presenters_lock = PTHREAD_MUTEX_INITIALIZER;
static Presenter *g_presenters[MAX_PRESENTERS];

static MvkWsi *wsi_of(MvkConnection *c)
{
    if (c->wsi)
        return c->wsi;
    MvkWsi *w = calloc(1, sizeof(*w));
    if (!w)
        return nullptr;
    pthread_mutex_init(&w->lock, nullptr);
    pthread_cond_init(&w->cond, nullptr);
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)c->gipa(c->instance, "vkGetDeviceProcAddr");
#define INSTANCE(field, name) w->field = (typeof(w->field))c->gipa(c->instance, name)
#define DEVICE(field, name) w->field = (typeof(w->field))(c->device ? gdpa(c->device, name) : NULL)
    INSTANCE(DestroySurface, "vkDestroySurfaceKHR");
    INSTANCE(SurfaceSupport, "vkGetPhysicalDeviceSurfaceSupportKHR");
    INSTANCE(SurfaceCapabilities, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    INSTANCE(SurfaceFormats, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    INSTANCE(SurfacePresentModes, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    INSTANCE(CreateWaylandSurface, "vkCreateWaylandSurfaceKHR");
    INSTANCE(CreateXlibSurface, "vkCreateXlibSurfaceKHR");
    DEVICE(CreateSwapchain, "vkCreateSwapchainKHR");
    DEVICE(DestroySwapchain, "vkDestroySwapchainKHR");
    DEVICE(GetSwapchainImages, "vkGetSwapchainImagesKHR");
    DEVICE(AcquireNextImage, "vkAcquireNextImageKHR");
    DEVICE(QueuePresent, "vkQueuePresentKHR");
#undef INSTANCE
#undef DEVICE
    c->wsi = w;
    return w;
}

static bool can_present(const MvkWsi *w)
{
    return w && w->DestroySurface && w->SurfaceSupport && w->SurfaceCapabilities && w->SurfaceFormats &&
           w->SurfacePresentModes && w->CreateSwapchain && w->DestroySwapchain && w->GetSwapchainImages &&
           w->AcquireNextImage && w->QueuePresent;
}

static bool is_srgb(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return true;
    default:
        return false;
    }
}

static int32_t memory_type(MvkConnection *c, uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties props;
    c->fn.vkGetPhysicalDeviceMemoryProperties(c->physical_device, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want)
            return (int32_t)i;
    return -1;
}

static VkQueue queue_family(MvkConnection *c, VkQueue queue, uint32_t *family)
{
    for (uint32_t i = 0; i < c->queue_count; i++) {
        if (c->queues[i].queue == queue) {
            *family = c->queues[i].family;
            return queue;
        }
    }
    return VK_NULL_HANDLE;
}

static bool family_graphics(MvkConnection *c, uint32_t family)
{
    uint32_t count = 0;
    c->fn.vkGetPhysicalDeviceQueueFamilyProperties(c->physical_device, &count, nullptr);
    VkQueueFamilyProperties props[32];
    count = count < 32 ? count : 32;
    c->fn.vkGetPhysicalDeviceQueueFamilyProperties(c->physical_device, &count, props);
    return family < count && (props[family].queueFlags & VK_QUEUE_GRAPHICS_BIT);
}

static VkSemaphore new_semaphore(MvkConnection *c)
{
    VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore s = VK_NULL_HANDLE;
    c->fn.vkCreateSemaphore(c->device, &info, nullptr, &s);
    return s;
}

/* ------------------------------------------------------------------ swapchains */

static void destroy_swapchain_images(MvkConnection *c, Swapchain *s)
{
    for (uint32_t i = 0; i < s->count; i++) {
        if (s->images[i])
            c->fn.vkDestroyImage(c->device, s->images[i], nullptr);
        if (s->memory[i])
            c->fn.vkFreeMemory(c->device, s->memory[i], nullptr);
        if (s->released[i])
            c->fn.vkDestroySemaphore(c->device, s->released[i], nullptr);
    }
}

/*
 * (device id, swapchain id, flags, format, color space, width, height,
 * layers, usage, sharing, family count, families, present mode, view
 * format count, view formats, image count, image ids) -> result
 */
void mvk_wsi_create_swapchain(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    mvk_get_u64(d);
    uint64_t id = mvk_get_u64(d);
    uint32_t flags = mvk_get_u32(d);
    VkFormat format = (VkFormat)mvk_get_u32(d);
    mvk_get_u32(d);
    VkExtent2D extent = {mvk_get_u32(d), mvk_get_u32(d)};
    uint32_t layers = mvk_get_u32(d);
    VkImageUsageFlags usage = mvk_get_u32(d);
    VkSharingMode sharing = (VkSharingMode)mvk_get_u32(d);
    uint32_t family_count = mvk_get_u32(d);
    uint32_t families[16];
    for (uint32_t i = 0; i < family_count; i++) {
        uint32_t f = mvk_get_u32(d);
        if (i < 16)
            families[i] = f;
    }
    VkPresentModeKHR mode = (VkPresentModeKHR)mvk_get_u32(d);
    uint32_t view_count = mvk_get_u32(d);
    VkFormat views[16];
    for (uint32_t i = 0; i < view_count; i++) {
        VkFormat f = (VkFormat)mvk_get_u32(d);
        if (i < 16)
            views[i] = f;
    }
    uint32_t count = mvk_get_u32(d);
    uint64_t ids[MAX_IMAGES];
    for (uint32_t i = 0; i < count; i++) {
        uint64_t image = mvk_get_u64(d);
        if (i < MAX_IMAGES)
            ids[i] = image;
    }
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    Swapchain *s = calloc(1, sizeof(*s));
    if (!d->error && c->device && s && count && count <= MAX_IMAGES && family_count <= 16 && view_count <= 16) {
        *s = (Swapchain){.id = id, .format = format, .extent = extent, .mode = mode, .count = count};
        /* a mutable swapchain's views may differ in format; otherwise only usage the format allows. */
        bool mutable = (flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) != 0;
        VkFormatProperties fp;
        c->fn.vkGetPhysicalDeviceFormatProperties(c->physical_device, format, &fp);
        if (!mutable && !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
            usage &= ~VK_IMAGE_USAGE_STORAGE_BIT;
        VkImageFormatListCreateInfo list = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO, nullptr, view_count, views};
        VkImageCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = mutable && view_count ? &list : nullptr,
            .flags = mutable ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT : 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = {extent.width, extent.height, 1},
            .mipLevels = 1,
            .arrayLayers = layers ? layers : 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = sharing,
            .queueFamilyIndexCount = sharing == VK_SHARING_MODE_CONCURRENT ? family_count : 0,
            .pQueueFamilyIndices = families,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        result = VK_SUCCESS;
        for (uint32_t i = 0; i < count && result == VK_SUCCESS; i++) {
            result = c->fn.vkCreateImage(c->device, &info, nullptr, &s->images[i]);
            if (result != VK_SUCCESS)
                break;
            VkMemoryRequirements req;
            c->fn.vkGetImageMemoryRequirements(c->device, s->images[i], &req);
            int32_t type = memory_type(c, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (type < 0)
                type = memory_type(c, req.memoryTypeBits, 0);
            VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, (uint32_t)type};
            result = type < 0 ? VK_ERROR_OUT_OF_DEVICE_MEMORY
                              : c->fn.vkAllocateMemory(c->device, &alloc, nullptr, &s->memory[i]);
            if (result == VK_SUCCESS)
                result = c->fn.vkBindImageMemory(c->device, s->images[i], s->memory[i], 0);
            s->released[i] = new_semaphore(c);
            if (!s->released[i] && result == VK_SUCCESS)
                result = VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        if (result == VK_SUCCESS) {
            for (uint32_t i = 0; i < count; i++) {
                s->image_ids[i] = ids[i];
                mvk_bind(c, ids[i], (uint64_t)s->images[i]);
            }
            mvk_bind(c, id, (uint64_t)(uintptr_t)s);
            MvkWsi *w = wsi_of(c);
            if (w) {
                s->next = w->swapchains;
                w->swapchains = s;
            }
        } else {
            destroy_swapchain_images(c, s);
            mime_warn("vulkan: cannot make the images of a %ux%u swapchain (%d)", extent.width, extent.height,
                      (int)result);
        }
    }
    if (result != VK_SUCCESS)
        free(s);
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

/* ------------------------------------------------------------------ presenters */

static void wait_frames(MvkConnection *c, Presenter *p)
{
    for (uint32_t i = 0; i < FRAMES; i++)
        if (p->frames[i].fence)
            c->fn.vkWaitForFences(c->device, 1, &p->frames[i].fence, VK_TRUE, UINT64_MAX);
}

/* the host swapchain goes; its semaphores wait in the grave unless the device is idle. */
static void drop_swapchain(MvkConnection *c, Presenter *p, bool idle)
{
    MvkWsi *w = c->wsi;
    for (uint32_t i = 0; i < p->count; i++) {
        if (!p->done[i])
            continue;
        if (idle)
            c->fn.vkDestroySemaphore(c->device, p->done[i], nullptr);
        else if (p->grave_count < MAX_GRAVE)
            p->grave[p->grave_count++] = p->done[i];
        p->done[i] = VK_NULL_HANDLE;
    }
    if (p->swapchain)
        w->DestroySwapchain(c->device, p->swapchain, nullptr);
    p->swapchain = VK_NULL_HANDLE;
    p->count = 0;
}

static void drop_surface(MvkConnection *c, Presenter *p, bool idle)
{
    wait_frames(c, p);
    drop_swapchain(c, p, idle);
    if (p->surface)
        c->wsi->DestroySurface(c->instance, p->surface, nullptr);
    p->surface = VK_NULL_HANDLE;
    p->checked_family = UINT32_MAX;
}

/* with the device idle: everything of the presenter. */
static void destroy_presenter(MvkConnection *c, Presenter *p)
{
    pthread_mutex_lock(&p->lock);
    drop_surface(c, p, true);
    for (uint32_t i = 0; i < p->grave_count; i++)
        c->fn.vkDestroySemaphore(c->device, p->grave[i], nullptr);
    for (uint32_t i = 0; i < FRAMES; i++) {
        if (p->frames[i].fence)
            c->fn.vkDestroyFence(c->device, p->frames[i].fence, nullptr);
        if (p->frames[i].acquired)
            c->fn.vkDestroySemaphore(c->device, p->frames[i].acquired, nullptr);
    }
    if (p->pool)
        c->fn.vkDestroyCommandPool(c->device, p->pool, nullptr);
    if (p->gdi)
        c->fn.vkDestroyImage(c->device, p->gdi, nullptr);
    if (p->gdi_memory)
        c->fn.vkFreeMemory(c->device, p->gdi_memory, nullptr);
    if (p->staging)
        c->fn.vkDestroyBuffer(c->device, p->staging, nullptr);
    if (p->staging_memory)
        c->fn.vkFreeMemory(c->device, p->staging_memory, nullptr);
    if (p->last)
        c->fn.vkDestroyImage(c->device, p->last, nullptr);
    if (p->last_memory)
        c->fn.vkFreeMemory(c->device, p->last_memory, nullptr);
    if (p->readback)
        c->fn.vkDestroyBuffer(c->device, p->readback, nullptr);
    if (p->readback_memory)
        c->fn.vkFreeMemory(c->device, p->readback_memory, nullptr);
    bool attached = p->attached;
    pthread_mutex_unlock(&p->lock);
    pthread_mutex_destroy(&p->lock);
    if (attached)
        mime_ui_vk_detach(p->window);
    free(p->shadow);
    free(p);
}

/* the presenter of a window, made for the connection unless another one has it. */
static Presenter *presenter_get(MvkConnection *c, uint32_t window)
{
    pthread_mutex_lock(&g_presenters_lock);
    Presenter *p = nullptr, **slot = nullptr;
    for (int i = 0; i < MAX_PRESENTERS; i++) {
        if (g_presenters[i] && g_presenters[i]->window == window)
            p = g_presenters[i];
        else if (!g_presenters[i] && !slot)
            slot = &g_presenters[i];
    }
    if (p && p->conn != c)
        p = nullptr;
    else if (!p && slot && (p = calloc(1, sizeof(*p)))) {
        p->window = window;
        p->conn = c;
        p->checked_family = UINT32_MAX;
        p->pool_family = UINT32_MAX;
        pthread_mutex_init(&p->lock, nullptr);
        *slot = p;
    }
    if (p)
        p->refs++;
    pthread_mutex_unlock(&g_presenters_lock);
    return p;
}

static void presenter_put(MvkConnection *c, Presenter *p)
{
    if (!p)
        return;
    pthread_mutex_lock(&g_presenters_lock);
    bool last = --p->refs == 0;
    if (last)
        for (int i = 0; i < MAX_PRESENTERS; i++)
            if (g_presenters[i] == p)
                g_presenters[i] = nullptr;
    pthread_mutex_unlock(&g_presenters_lock);
    if (last) {
        pthread_mutex_lock(&c->queue_lock);
        c->fn.vkDeviceWaitIdle(c->device);
        pthread_mutex_unlock(&c->queue_lock);
        destroy_presenter(c, p);
    }
}

void mime_vk_window_lost(uint32_t id)
{
    pthread_mutex_lock(&g_presenters_lock);
    for (int i = 0; i < MAX_PRESENTERS; i++) {
        Presenter *p = g_presenters[i];
        if (!p || p->window != id)
            continue;
        pthread_mutex_lock(&p->lock);
        p->generation++;
        p->attached = false;
        /* the presenting thread's last present may still use the host swapchain: the queues go idle first. */
        if (p->surface) {
            MvkConnection *c = p->conn;
            pthread_mutex_lock(&c->queue_lock);
            c->fn.vkDeviceWaitIdle(c->device);
            pthread_mutex_unlock(&c->queue_lock);
            drop_surface(c, p, true);
        }
        pthread_mutex_unlock(&p->lock);
    }
    pthread_mutex_unlock(&g_presenters_lock);
}

/* takes the host window from sdl and makes the surface, outside the presenter's lock. */
static void presenter_attach(MvkConnection *c, Presenter *p, MimeRect client)
{
    pthread_mutex_lock(&p->lock);
    uint32_t generation = p->generation;
    bool need = !p->surface;
    pthread_mutex_unlock(&p->lock);
    if (!need)
        return;
    MimeUiNative native;
    if (!mime_ui_vk_attach(p->window, client, &native))
        return;
    pthread_mutex_lock(&p->lock);
    p->attached = true;
    if (p->generation == generation && !p->surface) {
        MvkWsi *w = c->wsi;
        VkResult r = VK_ERROR_EXTENSION_NOT_PRESENT;
        if (native.x11 && w->CreateXlibSurface) {
            VkXlibSurfaceCreateInfoKHR info = {VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR, nullptr, 0, native.display,
                                               (Window)native.window};
            r = w->CreateXlibSurface(c->instance, &info, nullptr, &p->surface);
        } else if (!native.x11 && w->CreateWaylandSurface) {
            VkWaylandSurfaceCreateInfoKHR info = {VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR, nullptr, 0,
                                                  native.display, native.surface};
            r = w->CreateWaylandSurface(c->instance, &info, nullptr, &p->surface);
        }
        if (r != VK_SUCCESS) {
            mime_warn("vulkan: cannot make a surface on window %#x (%d)", p->window, (int)r);
            p->surface = VK_NULL_HANDLE;
        }
    }
    pthread_mutex_unlock(&p->lock);
}

static VkSurfaceFormatKHR pick_format(MvkConnection *c, Presenter *p, bool srgb)
{
    VkSurfaceFormatKHR formats[64];
    uint32_t n = 64;
    if (c->wsi->SurfaceFormats(c->physical_device, p->surface, &n, formats) < 0 || !n)
        return (VkSurfaceFormatKHR){VK_FORMAT_UNDEFINED};
    static const VkFormat linear[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    static const VkFormat encoded[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
    const VkFormat *want = srgb ? encoded : linear;
    for (int k = 0; k < 2; k++)
        for (uint32_t i = 0; i < n; i++)
            if (formats[i].format == want[k] && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return formats[i];
    return formats[0];
}

static VkPresentModeKHR pick_mode(MvkConnection *c, Presenter *p, VkPresentModeKHR want)
{
    VkPresentModeKHR modes[16];
    uint32_t n = 16;
    if (c->wsi->SurfacePresentModes(c->physical_device, p->surface, &n, modes) < 0)
        n = 0;
    VkPresentModeKHR order[3] = {want, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR};
    /* without tearing, mailbox comes closest to immediate; others fall back to fifo. */
    if (want != VK_PRESENT_MODE_IMMEDIATE_KHR)
        order[1] = VK_PRESENT_MODE_FIFO_KHR;
    for (int k = 0; k < 3; k++)
        for (uint32_t i = 0; i < n; i++)
            if (modes[i] == order[k])
                return modes[i];
    return VK_PRESENT_MODE_FIFO_KHR;
}

static bool make_swapchain(MvkConnection *c, Presenter *p, VkQueue queue, VkExtent2D want, bool srgb,
                           VkPresentModeKHR mode)
{
    MvkWsi *w = c->wsi;
    VkSurfaceCapabilitiesKHR caps;
    if (w->SurfaceCapabilities(c->physical_device, p->surface, &caps) != VK_SUCCESS)
        return false;
    VkExtent2D extent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : want;
    extent.width = MIME_MIN(MIME_MAX(extent.width, caps.minImageExtent.width), caps.maxImageExtent.width);
    extent.height = MIME_MIN(MIME_MAX(extent.height, caps.minImageExtent.height), caps.maxImageExtent.height);
    if (!extent.width || !extent.height || !(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return false;
    VkSurfaceFormatKHR format = pick_format(c, p, srgb);
    if (format.format == VK_FORMAT_UNDEFINED)
        return false;
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount)
        count = caps.maxImageCount;
    if (count > MAX_HOST_IMAGES)
        count = MAX_HOST_IMAGES;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha))
        alpha = (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
    VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = p->surface,
        .minImageCount = count,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = alpha,
        .presentMode = pick_mode(c, p, mode),
        .clipped = VK_TRUE,
        .oldSwapchain = p->swapchain,
    };
    VkSwapchainKHR swapchain;
    VkResult r = w->CreateSwapchain(c->device, &info, nullptr, &swapchain);
    /* the old one goes either way, once nothing of it is in flight. */
    if (p->swapchain) {
        pthread_mutex_lock(&c->queue_lock);
        c->fn.vkQueueWaitIdle(queue);
        pthread_mutex_unlock(&c->queue_lock);
        drop_swapchain(c, p, true);
    }
    if (r != VK_SUCCESS) {
        mime_warn("vulkan: cannot make a %ux%u swapchain on window %#x (%d)", extent.width, extent.height, p->window,
                  (int)r);
        return false;
    }
    p->swapchain = swapchain;
    p->count = MAX_HOST_IMAGES;
    w->GetSwapchainImages(c->device, swapchain, &p->count, p->images);
    for (uint32_t i = 0; i < p->count; i++)
        p->done[i] = new_semaphore(c);
    p->format = format.format;
    p->srgb = srgb;
    p->extent = extent;
    p->wanted_mode = mode;
    p->stale = false;
    return true;
}

static bool make_frames(MvkConnection *c, Presenter *p, uint32_t family)
{
    if (p->pool && p->pool_family == family)
        return true;
    wait_frames(c, p);
    if (p->pool)
        c->fn.vkDestroyCommandPool(c->device, p->pool, nullptr);
    p->pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
                                    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, family};
    if (c->fn.vkCreateCommandPool(c->device, &info, nullptr, &p->pool) != VK_SUCCESS)
        return false;
    p->pool_family = family;
    for (uint32_t i = 0; i < FRAMES; i++) {
        Frame *f = &p->frames[i];
        VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, p->pool,
                                             VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        if (c->fn.vkAllocateCommandBuffers(c->device, &alloc, &f->cmd) != VK_SUCCESS)
            return false;
        if (!f->fence) {
            VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};
            if (c->fn.vkCreateFence(c->device, &fi, nullptr, &f->fence) != VK_SUCCESS)
                return false;
        }
        if (!f->acquired && !(f->acquired = new_semaphore(c)))
            return false;
    }
    return true;
}

/* the image of the guest surface, and the buffer it is uploaded through. */
static bool make_gdi(MvkConnection *c, Presenter *p, uint32_t width, uint32_t height, VkFormat format)
{
    if (p->gdi && p->gdi_width == width && p->gdi_height == height && p->gdi_format == format)
        return true;
    wait_frames(c, p);
    if (p->gdi)
        c->fn.vkDestroyImage(c->device, p->gdi, nullptr);
    if (p->gdi_memory)
        c->fn.vkFreeMemory(c->device, p->gdi_memory, nullptr);
    if (p->staging)
        c->fn.vkDestroyBuffer(c->device, p->staging, nullptr);
    if (p->staging_memory)
        c->fn.vkFreeMemory(c->device, p->staging_memory, nullptr);
    p->gdi = VK_NULL_HANDLE;
    p->gdi_memory = p->staging_memory = VK_NULL_HANDLE;
    p->staging = VK_NULL_HANDLE;
    p->staging_map = nullptr;
    p->gdi_valid = false;
    VkImageCreateInfo ii = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (c->fn.vkCreateImage(c->device, &ii, nullptr, &p->gdi) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    c->fn.vkGetImageMemoryRequirements(c->device, p->gdi, &req);
    int32_t type = memory_type(c, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, (uint32_t)type};
    if (type < 0 || c->fn.vkAllocateMemory(c->device, &alloc, nullptr, &p->gdi_memory) != VK_SUCCESS ||
        c->fn.vkBindImageMemory(c->device, p->gdi, p->gdi_memory, 0) != VK_SUCCESS)
        return false;
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, (VkDeviceSize)width * height * 4,
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE};
    if (c->fn.vkCreateBuffer(c->device, &bi, nullptr, &p->staging) != VK_SUCCESS)
        return false;
    c->fn.vkGetBufferMemoryRequirements(c->device, p->staging, &req);
    type = memory_type(c, req.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    alloc = (VkMemoryAllocateInfo){VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, (uint32_t)type};
    if (type < 0 || c->fn.vkAllocateMemory(c->device, &alloc, nullptr, &p->staging_memory) != VK_SUCCESS ||
        c->fn.vkBindBufferMemory(c->device, p->staging, p->staging_memory, 0) != VK_SUCCESS ||
        c->fn.vkMapMemory(c->device, p->staging_memory, 0, VK_WHOLE_SIZE, 0, (void **)&p->staging_map) != VK_SUCCESS)
        return false;
    p->gdi_width = width;
    p->gdi_height = height;
    p->gdi_format = format;
    return true;
}

/* the image the last present is kept in, the guest's size; false without one. */
static bool make_last(MvkConnection *c, Presenter *p, VkExtent2D extent, VkFormat format)
{
    if (p->last && p->last_extent.width == extent.width && p->last_extent.height == extent.height &&
        p->last_format == format)
        return true;
    wait_frames(c, p);
    if (p->last)
        c->fn.vkDestroyImage(c->device, p->last, nullptr);
    if (p->last_memory)
        c->fn.vkFreeMemory(c->device, p->last_memory, nullptr);
    p->last = VK_NULL_HANDLE;
    p->last_memory = VK_NULL_HANDLE;
    p->last_valid = false;
    VkImageCreateInfo ii = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (c->fn.vkCreateImage(c->device, &ii, nullptr, &p->last) != VK_SUCCESS) {
        p->last = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryRequirements req;
    c->fn.vkGetImageMemoryRequirements(c->device, p->last, &req);
    int32_t type = memory_type(c, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, (uint32_t)type};
    if (type < 0 || c->fn.vkAllocateMemory(c->device, &alloc, nullptr, &p->last_memory) != VK_SUCCESS ||
        c->fn.vkBindImageMemory(c->device, p->last, p->last_memory, 0) != VK_SUCCESS) {
        c->fn.vkDestroyImage(c->device, p->last, nullptr);
        if (p->last_memory)
            c->fn.vkFreeMemory(c->device, p->last_memory, nullptr);
        p->last = VK_NULL_HANDLE;
        p->last_memory = VK_NULL_HANDLE;
        return false;
    }
    p->last_extent = extent;
    p->last_format = format;
    return true;
}

/* ------------------------------------------------------------------ recording */

static void barrier(MvkConnection *c, VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                    VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                    VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS},
    };
    c->fn.vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

typedef struct Box {
    double left, top, right, bottom;
} Box;

/* blits src scaled onto dst, both cut to their images, src upside down when flip. */
static void blit(MvkConnection *c, VkCommandBuffer cmd, VkImage src, Box s, uint32_t sw, uint32_t sh, VkImage dst,
                 Box d, uint32_t dw, uint32_t dh, bool flip)
{
    if (s.right <= s.left || s.bottom <= s.top || d.right <= d.left || d.bottom <= d.top)
        return;
    double fx = (s.right - s.left) / (d.right - d.left), fy = (s.bottom - s.top) / (d.bottom - d.top);
    /* the source first, moving the destination with it, then the destination. */
    if (s.left < 0) d.left -= s.left / fx, s.left = 0;
    if (s.top < 0) d.top -= s.top / fy, s.top = 0;
    if (s.right > sw) d.right -= (s.right - sw) / fx, s.right = sw;
    if (s.bottom > sh) d.bottom -= (s.bottom - sh) / fy, s.bottom = sh;
    if (d.left < 0) s.left -= d.left * fx, d.left = 0;
    if (d.top < 0) s.top -= d.top * fy, d.top = 0;
    if (d.right > dw) s.right -= (d.right - dw) * fx, d.right = dw;
    if (d.bottom > dh) s.bottom -= (d.bottom - dh) * fy, d.bottom = dh;
    VkImageBlit region = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .srcOffsets = {{(int32_t)lround(s.left), (int32_t)lround(s.top), 0},
                       {(int32_t)lround(s.right), (int32_t)lround(s.bottom), 1}},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .dstOffsets = {{(int32_t)lround(d.left), (int32_t)lround(d.top), 0},
                       {(int32_t)lround(d.right), (int32_t)lround(d.bottom), 1}},
    };
    if (region.srcOffsets[1].x <= region.srcOffsets[0].x || region.srcOffsets[1].y <= region.srcOffsets[0].y ||
        region.dstOffsets[1].x <= region.dstOffsets[0].x || region.dstOffsets[1].y <= region.dstOffsets[0].y)
        return;
    bool same = region.srcOffsets[1].x - region.srcOffsets[0].x == region.dstOffsets[1].x - region.dstOffsets[0].x &&
                region.srcOffsets[1].y - region.srcOffsets[0].y == region.dstOffsets[1].y - region.dstOffsets[0].y;
    if (flip) {
        int32_t top = region.srcOffsets[0].y;
        region.srcOffsets[0].y = (int32_t)sh - region.srcOffsets[1].y;
        region.srcOffsets[1].y = (int32_t)sh - top;
        int32_t t = region.srcOffsets[0].y;
        region.srcOffsets[0].y = region.srcOffsets[1].y;
        region.srcOffsets[1].y = t;
    }
    c->fn.vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &region, same ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
}

/* the window relative guest rect r in host window pixels. */
static Box to_pixels(const MimeUiFrame *f, MimeRect r)
{
    double sx = (double)f->pixel_width / MIME_MAX(f->host.right - f->host.left, 1);
    double sy = (double)f->pixel_height / MIME_MAX(f->host.bottom - f->host.top, 1);
    return (Box){(r.left - f->host.left) * sx, (r.top - f->host.top) * sy, (r.right - f->host.left) * sx,
                 (r.bottom - f->host.top) * sy};
}

static void record(MvkConnection *c, Presenter *p, Frame *f, uint32_t target, Swapchain *s, uint32_t index,
                   const MimeUiFrame *frame, MimeRect client, bool upload)
{
    VkCommandBuffer cmd = f->cmd;
    VkImage dst = p->images[target];
    c->fn.vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    c->fn.vkBeginCommandBuffer(cmd, &begin);
    barrier(c, cmd, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    /* a subsurface is the client area alone; the whole window has its border and frame around it. */
    if (!frame->subsurface) {
        VkClearColorValue black = {{0, 0, 0, 1}};
        VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        c->fn.vkCmdClearColorImage(cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &all);
        barrier(c, cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
    }

    /* the window's frame, as the guest drew it. */
    if (upload) {
        barrier(c, cmd, p->gdi, p->gdi_valid ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        MimeRect d = p->gdi_valid ? p->shadow_dirty : (MimeRect){0, 0, (int32_t)p->gdi_width, (int32_t)p->gdi_height};
        VkBufferImageCopy copy = {
            .bufferOffset = ((VkDeviceSize)d.top * p->gdi_width + (VkDeviceSize)d.left) * 4,
            .bufferRowLength = p->gdi_width,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageOffset = {d.left, d.top, 0},
            .imageExtent = {(uint32_t)(d.right - d.left), (uint32_t)(d.bottom - d.top), 1},
        };
        c->fn.vkCmdCopyBufferToImage(cmd, p->staging, p->gdi, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier(c, cmd, p->gdi, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
        p->gdi_valid = true;
    }
    if (p->gdi_valid && !frame->subsurface) {
        MimeRect v = frame->visible;
        blit(c, cmd, p->gdi, (Box){v.left, v.top, v.right, v.bottom}, p->gdi_width, p->gdi_height, dst,
             to_pixels(frame, v), p->extent.width, p->extent.height, false);
        barrier(c, cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
    }

    /* the guest's image over the client area. */
    VkImage src = s->images[index];
    barrier(c, cmd, src, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    Box area = frame->subsurface ? (Box){0, 0, p->extent.width, p->extent.height} : to_pixels(frame, client);
    blit(c, cmd, src, (Box){0, 0, s->extent.width, s->extent.height}, s->extent.width, s->extent.height, dst, area,
         p->extent.width, p->extent.height, s->flip);
    /* and kept, for captures of the screen. */
    if (p->last) {
        barrier(c, cmd, p->last, p->last_valid ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        blit(c, cmd, src, (Box){0, 0, s->extent.width, s->extent.height}, s->extent.width, s->extent.height, p->last,
             (Box){0, 0, p->last_extent.width, p->last_extent.height}, p->last_extent.width, p->last_extent.height,
             s->flip);
        barrier(c, cmd, p->last, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
        p->last_valid = true;
        p->last_client = client;
    }
    barrier(c, cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_READ_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    barrier(c, cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    c->fn.vkEndCommandBuffer(cmd);
}

/* ------------------------------------------------------------------ present */

/* semaphores a submission waits for, the values of timelines among them. */
typedef struct Waits {
    uint32_t count;
    VkSemaphore semaphores[32];
    VkPipelineStageFlags stages[32];
    uint64_t values[32];
} Waits;

static VkResult submit_locked(MvkConnection *c, VkQueue queue, const VkSubmitInfo *submit, VkFence fence)
{
    pthread_mutex_lock(&c->queue_lock);
    VkResult r = c->fn.vkQueueSubmit(queue, 1, submit, fence);
    pthread_mutex_unlock(&c->queue_lock);
    return r;
}

/* a present that shows nothing: the guest's waits, and the image released. */
static void release_only(MvkConnection *c, VkQueue queue, const Waits *waits, Swapchain *s, uint32_t index)
{
    uint64_t none = 0;
    VkTimelineSemaphoreSubmitInfo values = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr, waits->count,
                                            waits->values, 1, &none};
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO,
                           c->timelines ? &values : nullptr,
                           waits->count,
                           waits->semaphores,
                           waits->stages,
                           0,
                           nullptr,
                           1,
                           &s->released[index]};
    if (submit_locked(c, queue, &submit, VK_NULL_HANDLE) == VK_SUCCESS)
        s->pending[index] = true;
}

/* refreshes the presenter's copy of the guest surface, outside its lock. */
static void refresh_shadow(Presenter *p, const MimeUiFrame *frame)
{
    if (!frame->surface_width || !frame->surface_height)
        return;
    if (p->shadow_width != frame->surface_width || p->shadow_height != frame->surface_height) {
        free(p->shadow);
        p->shadow = malloc((size_t)frame->surface_width * frame->surface_height * 4);
        p->shadow_width = p->shadow ? frame->surface_width : 0;
        p->shadow_height = p->shadow ? frame->surface_height : 0;
        p->shadow_has_dirty = false;
    }
    MimeRect d;
    if (p->shadow && mime_ui_vk_copy(p->window, p->shadow, p->shadow_width, p->shadow_height, &d)) {
        if (p->shadow_has_dirty)
            d = (MimeRect){MIME_MIN(d.left, p->shadow_dirty.left), MIME_MIN(d.top, p->shadow_dirty.top),
                           MIME_MAX(d.right, p->shadow_dirty.right), MIME_MAX(d.bottom, p->shadow_dirty.bottom)};
        p->shadow_dirty = d;
        p->shadow_has_dirty = true;
    }
}

/* the presenter's part of a present, under its lock: false when the window shows nothing of it. */
static bool show(MvkConnection *c, Presenter *p, VkQueue queue, uint32_t family, const Waits *waits, Swapchain *s,
                 uint32_t index, const MimeUiFrame *frame, MimeRect client)
{
    MvkWsi *w = c->wsi;
    if (!p->surface || !frame->shown || !frame->pixel_width || !frame->pixel_height)
        return false;
    if (p->checked_family != family) {
        VkBool32 ok = VK_FALSE;
        w->SurfaceSupport(c->physical_device, family, p->surface, &ok);
        p->supported = ok && family_graphics(c, family);
        p->checked_family = family;
    }
    if (!p->supported || !make_frames(c, p, family))
        return false;
    VkExtent2D want = {frame->pixel_width, frame->pixel_height};
    bool srgb = is_srgb(s->format);
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!p->swapchain || p->stale || p->srgb != srgb || p->wanted_mode != s->mode ||
            p->extent.width != want.width || p->extent.height != want.height) {
            wait_frames(c, p);
            if (!make_swapchain(c, p, queue, want, srgb, s->mode))
                return false;
        }
        Frame *f = &p->frames[p->frame];
        c->fn.vkWaitForFences(c->device, 1, &f->fence, VK_TRUE, UINT64_MAX);
        uint32_t target;
        VkResult r = w->AcquireNextImage(c->device, p->swapchain, ACQUIRE_TIMEOUT_NS, f->acquired, VK_NULL_HANDLE,
                                         &target);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            p->stale = true;
            continue;
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
            return false;
        if (r == VK_SUBOPTIMAL_KHR)
            p->stale = true;
        p->frame = (p->frame + 1) % FRAMES;

        /* the guest surface, through the staging buffer once no frame reads it. */
        VkFormat gdi_format = srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
        bool upload = false;
        if (!frame->subsurface && p->shadow &&
            (p->shadow_has_dirty || !p->gdi_valid || p->gdi_format != gdi_format)) {
            if (make_gdi(c, p, p->shadow_width, p->shadow_height, gdi_format)) {
                wait_frames(c, p);
                MimeRect d = p->gdi_valid && p->shadow_has_dirty
                                 ? p->shadow_dirty
                                 : (MimeRect){0, 0, (int32_t)p->shadow_width, (int32_t)p->shadow_height};
                for (int32_t y = d.top; y < d.bottom; y++)
                    memcpy(p->staging_map + ((size_t)y * p->shadow_width + (size_t)d.left) * 4,
                           p->shadow + ((size_t)y * p->shadow_width + (size_t)d.left) * 4,
                           (size_t)(d.right - d.left) * 4);
                p->shadow_dirty = d;
                p->shadow_has_dirty = false;
                upload = true;
            }
        }
        make_last(c, p, s->extent, srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM);
        p->last_queue = queue;
        record(c, p, f, target, s, index, frame, client, upload);

        Waits all = *waits;
        all.semaphores[all.count] = f->acquired;
        all.values[all.count] = 0;
        all.stages[all.count++] = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSemaphore signals[2] = {p->done[target], s->released[index]};
        uint64_t signal_values[2] = {0, 0};
        VkTimelineSemaphoreSubmitInfo values = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr, all.count,
                                                all.values, 2, signal_values};
        VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, c->timelines ? &values : nullptr, all.count, all.semaphores,
                               all.stages, 1, &f->cmd, 2, signals};
        c->fn.vkResetFences(c->device, 1, &f->fence);
        pthread_mutex_lock(&c->queue_lock);
        if (c->fn.vkQueueSubmit(queue, 1, &submit, f->fence) != VK_SUCCESS) {
            /* the fence stays unsignaled: a submit that only signals it keeps the next wait from hanging. */
            VkSubmitInfo none = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
            c->fn.vkQueueSubmit(queue, 1, &none, f->fence);
            pthread_mutex_unlock(&c->queue_lock);
            return false;
        }
        s->pending[index] = true;
        VkPresentInfoKHR info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr, 1, &p->done[target], 1, &p->swapchain,
                                 &target, nullptr};
        r = w->QueuePresent(queue, &info);
        pthread_mutex_unlock(&c->queue_lock);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            p->stale = true;
        return true;
    }
    return false;
}

/* a present of one swapchain: on its window when it shows, otherwise only the image released. */
static void present_one(MvkConnection *c, VkQueue queue, uint32_t family, const Waits *waits, Swapchain *s,
                        uint32_t index, uint32_t window, MimeRect client)
{
    MvkWsi *w = c->wsi;
    if (s->presenter && s->presenter->window != window) {
        presenter_put(c, s->presenter);
        s->presenter = nullptr;
    }
    if (!s->presenter && window && can_present(w))
        s->presenter = presenter_get(c, window);
    Presenter *p = s->presenter;
    bool shown = false;
    if (p) {
        presenter_attach(c, p, client);
        MimeUiFrame frame;
        if (mime_ui_vk_frame(window, client, &frame)) {
            if (!frame.subsurface)
                refresh_shadow(p, &frame);
            pthread_mutex_lock(&p->lock);
            shown = show(c, p, queue, family, waits, s, index, &frame, client);
            pthread_mutex_unlock(&p->lock);
        }
    }
    if (!shown)
        release_only(c, queue, waits, s, index);
}

static void *presenting_main(void *arg)
{
    MvkConnection *c = arg;
    MvkWsi *w = c->wsi;
    mime_set_thread_name("mime-vk-present");
    pthread_mutex_lock(&w->lock);
    for (;;) {
        while (!w->first && !w->stop)
            pthread_cond_wait(&w->cond, &w->lock);
        if (!w->first)
            break;
        PresentJob *job = w->first;
        w->first = job->next;
        if (!w->first)
            w->last = nullptr;
        w->busy = true;
        pthread_mutex_unlock(&w->lock);
        Waits waits = {};
        if (job->ready) {
            waits.semaphores[0] = w->ready;
            waits.stages[0] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            waits.values[0] = job->ready;
            waits.count = 1;
        }
        present_one(c, job->queue, job->family, &waits, job->s, job->index, job->window, job->client);
        pthread_mutex_lock(&w->lock);
        job->s->queued[job->index]--;
        w->busy = false;
        pthread_cond_broadcast(&w->cond);
        free(job);
    }
    pthread_mutex_unlock(&w->lock);
    return nullptr;
}

void mvk_wsi_drain(MvkConnection *c)
{
    MvkWsi *w = c->wsi;
    if (!w || !w->running)
        return;
    pthread_mutex_lock(&w->lock);
    while (w->first || w->busy)
        pthread_cond_wait(&w->cond, &w->lock);
    pthread_mutex_unlock(&w->lock);
}

static void stop_presenting(MvkConnection *c)
{
    MvkWsi *w = c->wsi;
    if (!w || !w->running)
        return;
    pthread_mutex_lock(&w->lock);
    w->stop = true;
    pthread_cond_broadcast(&w->cond);
    pthread_mutex_unlock(&w->lock);
    pthread_join(w->thread, nullptr);
    w->running = false;
    if (w->ready) {
        /* submissions still in flight signal it. */
        pthread_mutex_lock(&c->queue_lock);
        c->fn.vkDeviceWaitIdle(c->device);
        pthread_mutex_unlock(&c->queue_lock);
        c->fn.vkDestroySemaphore(c->device, w->ready, nullptr);
    }
    w->ready = VK_NULL_HANDLE;
}

/* the presenting thread and its ready timeline, made at the first present. */
static bool start_presenting(MvkConnection *c, MvkWsi *w)
{
    if (w->running)
        return true;
    if (!c->timelines)
        return false;
    VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr, VK_SEMAPHORE_TYPE_TIMELINE, 0};
    VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    if (c->fn.vkCreateSemaphore(c->device, &info, nullptr, &w->ready) != VK_SUCCESS)
        return false;
    w->ready_value = 0;
    w->stop = false;
    if (pthread_create(&w->thread, nullptr, presenting_main, c)) {
        c->fn.vkDestroySemaphore(c->device, w->ready, nullptr);
        w->ready = VK_NULL_HANDLE;
        return false;
    }
    w->running = true;
    return true;
}

/* (queue id, wait count, semaphore ids, count, then (swapchain id, index, window, client rect) each) */
void mvk_wsi_present(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    VkQueue queue = (VkQueue)(uintptr_t)mvk_get_handle(d, 1);
    Waits waits = {};
    uint32_t wait_count = mvk_get_u32(d);
    for (uint32_t i = 0; i < wait_count; i++) {
        VkSemaphore sem = (VkSemaphore)mvk_get_handle(d, 0);
        if (sem && waits.count < 31) {
            waits.semaphores[waits.count] = sem;
            waits.stages[waits.count++] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }
    }
    uint32_t count = mvk_get_u32(d);
    uint32_t family = 0;
    if (d->error || !queue || !queue_family(c, queue, &family))
        return;
    MvkWsi *w = wsi_of(c);
    bool aside = w && start_presenting(c, w);
    /*
     * the guest's semaphores are taken here, in ring order, before the
     * guest may signal them again: once for several swapchains, then one
     * after the other, or for the presenting thread in a submission that
     * signals the next ready value.
     */
    uint64_t ready = 0;
    if (waits.count && (aside || count > 1)) {
        uint64_t value = aside ? ++w->ready_value : 0;
        VkTimelineSemaphoreSubmitInfo values = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr, waits.count,
                                                waits.values, 1, &value};
        VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, aside ? &values : nullptr, waits.count, waits.semaphores,
                               waits.stages, 0, nullptr, aside ? 1 : 0, aside ? &w->ready : nullptr};
        submit_locked(c, queue, &submit, VK_NULL_HANDLE);
        if (!aside) {
            pthread_mutex_lock(&c->queue_lock);
            c->fn.vkQueueWaitIdle(queue);
            pthread_mutex_unlock(&c->queue_lock);
        }
        ready = value;
        waits.count = 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        Swapchain *s = (Swapchain *)(uintptr_t)mvk_get_handle(d, 0);
        uint32_t index = mvk_get_u32(d);
        uint32_t window = mvk_get_u32(d);
        MimeRect client;
        client.left = (int32_t)mvk_get_u32(d);
        client.top = (int32_t)mvk_get_u32(d);
        client.right = (int32_t)mvk_get_u32(d);
        client.bottom = (int32_t)mvk_get_u32(d);
        if (d->error || !s || index >= s->count)
            break;
        /* a released image is acquired before its next present: anything else is the guest's mistake. */
        if (s->pending[index]) {
            mime_warn("vulkan: image %u of a swapchain presented twice", index);
            continue;
        }
        if (!aside) {
            present_one(c, queue, family, &waits, s, index, window, client);
            waits.count = 0;
            continue;
        }
        PresentJob *job = calloc(1, sizeof(*job));
        if (!job)
            break;
        *job = (PresentJob){nullptr, s, index, window, client, queue, family, ready};
        s->pending[index] = true;
        pthread_mutex_lock(&w->lock);
        s->queued[index]++;
        if (w->last)
            w->last->next = job;
        else
            w->first = job;
        w->last = job;
        pthread_cond_broadcast(&w->cond);
        pthread_mutex_unlock(&w->lock);
    }
}

/* (swapchain id, index, semaphore id, fence id): the guest's acquire, signaled once the image is free. */
void mvk_wsi_acquire(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    Swapchain *s = (Swapchain *)(uintptr_t)mvk_get_handle(d, 0);
    uint32_t index = mvk_get_u32(d);
    VkSemaphore semaphore = (VkSemaphore)mvk_get_handle(d, 0);
    VkFence fence = (VkFence)mvk_get_handle(d, 0);
    if (d->error || !s || index >= s->count || !c->queue_count)
        return;
    /* the present that releases the image must be submitted before its semaphore is waited for. */
    MvkWsi *w = c->wsi;
    if (w && w->running) {
        pthread_mutex_lock(&w->lock);
        while (s->queued[index])
            pthread_cond_wait(&w->cond, &w->lock);
        pthread_mutex_unlock(&w->lock);
    }
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    bool wait = s->pending[index];
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO,
                           nullptr,
                           wait,
                           &s->released[index],
                           &stage,
                           0,
                           nullptr,
                           semaphore != VK_NULL_HANDLE,
                           &semaphore};
    if (!wait && !semaphore && !fence)
        return;
    if (submit_locked(c, c->queues[0].queue, &submit, fence) == VK_SUCCESS)
        s->pending[index] = false;
}

static void free_swapchain(MvkConnection *c, Swapchain *s)
{
    mvk_wsi_drain(c);
    pthread_mutex_lock(&c->queue_lock);
    c->fn.vkDeviceWaitIdle(c->device);
    pthread_mutex_unlock(&c->queue_lock);
    if (s->presenter)
        presenter_put(c, s->presenter);
    destroy_swapchain_images(c, s);
    free(s);
}

/* (swapchain id) */
void mvk_wsi_destroy_swapchain(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t id = mvk_get_u64(&call->in);
    Swapchain *s = (Swapchain *)(uintptr_t)mvk_lookup(c, id);
    if (!s || call->in.error)
        return;
    MvkWsi *w = c->wsi;
    for (Swapchain **it = w ? &w->swapchains : nullptr; it && *it; it = &(*it)->next) {
        if (*it == s) {
            *it = s->next;
            break;
        }
    }
    for (uint32_t i = 0; i < s->count; i++)
        mvk_bind(c, s->image_ids[i], 0);
    mvk_bind(c, id, 0);
    free_swapchain(c, s);
}

void mvk_wsi_close(MvkConnection *c)
{
    MvkWsi *w = c->wsi;
    if (!w)
        return;
    stop_presenting(c);
    while (w->swapchains) {
        Swapchain *s = w->swapchains;
        w->swapchains = s->next;
        free_swapchain(c, s);
    }
    pthread_mutex_destroy(&w->lock);
    pthread_cond_destroy(&w->cond);
    free(w);
    c->wsi = nullptr;
}

/* ------------------------------------------------------------------ images of the host's own */

MvkImageChain *mvk_wsi_chain_create(MvkConnection *c, VkImage image, VkFormat format, VkExtent2D extent)
{
    if (!wsi_of(c))
        return nullptr;
    Swapchain *s = calloc(1, sizeof(*s));
    if (!s)
        return nullptr;
    s->format = format;
    s->extent = extent;
    s->mode = VK_PRESENT_MODE_FIFO_KHR;
    s->count = 1;
    s->images[0] = image;
    s->flip = true;
    s->released[0] = new_semaphore(c);
    if (!s->released[0]) {
        free(s);
        return nullptr;
    }
    return s;
}

/* as a guest present of the chain's image, which waits for rendered. */
void mvk_wsi_chain_present(MvkConnection *c, MvkImageChain *s, VkQueue queue, uint32_t family, VkSemaphore rendered,
                           uint32_t window, MimeRect client, bool sync)
{
    MvkWsi *w = wsi_of(c);
    if (!w || s->pending[0])
        return;
    s->mode = sync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    Waits waits = {};
    if (rendered) {
        waits.semaphores[0] = rendered;
        waits.stages[0] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        waits.count = 1;
    }
    if (!start_presenting(c, w)) {
        present_one(c, queue, family, &waits, s, 0, window, client);
        return;
    }
    uint64_t ready = 0;
    if (waits.count) {
        ready = ++w->ready_value;
        VkTimelineSemaphoreSubmitInfo values = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr, waits.count,
                                                waits.values, 1, &ready};
        VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, &values, waits.count, waits.semaphores, waits.stages,
                               0, nullptr, 1, &w->ready};
        submit_locked(c, queue, &submit, VK_NULL_HANDLE);
    }
    PresentJob *job = calloc(1, sizeof(*job));
    if (!job)
        return;
    *job = (PresentJob){nullptr, s, 0, window, client, queue, family, ready};
    s->pending[0] = true;
    pthread_mutex_lock(&w->lock);
    s->queued[0]++;
    if (w->last)
        w->last->next = job;
    else
        w->first = job;
    w->last = job;
    pthread_cond_broadcast(&w->cond);
    pthread_mutex_unlock(&w->lock);
}

/* the semaphore the next rendering to the image waits for, once its present was submitted; null when none. */
VkSemaphore mvk_wsi_chain_acquire(MvkConnection *c, MvkImageChain *s)
{
    MvkWsi *w = c->wsi;
    if (w && w->running) {
        pthread_mutex_lock(&w->lock);
        while (s->queued[0])
            pthread_cond_wait(&w->cond, &w->lock);
        pthread_mutex_unlock(&w->lock);
    }
    if (!s->pending[0])
        return VK_NULL_HANDLE;
    s->pending[0] = false;
    return s->released[0];
}

/* the image stays the caller's. */
void mvk_wsi_chain_destroy(MvkConnection *c, MvkImageChain *s)
{
    if (!s)
        return;
    s->images[0] = VK_NULL_HANDLE;
    free_swapchain(c, s);
}

/* ------------------------------------------------------------------ readback */

/*
 * the last image presented to a window, copied as bgra rows into pixels
 * when it fits, with its size and the client rectangle it showed in,
 * relative to the window. false when the window presents nothing.
 */
bool mvk_wsi_readback(uint32_t window, void *pixels, uint32_t capacity_width, uint32_t capacity_height,
                      uint32_t stride, uint32_t *width, uint32_t *height, MimeRect *client)
{
    pthread_mutex_lock(&g_presenters_lock);
    Presenter *p = nullptr;
    for (int i = 0; i < MAX_PRESENTERS; i++)
        if (g_presenters[i] && g_presenters[i]->window == window) {
            p = g_presenters[i];
            p->refs++;
            break;
        }
    pthread_mutex_unlock(&g_presenters_lock);
    if (!p)
        return false;
    MvkConnection *c = p->conn;
    bool ok = false;
    pthread_mutex_lock(&p->lock);
    if (!p->last_valid || !p->pool || !p->last_queue)
        goto out;
    *width = p->last_extent.width;
    *height = p->last_extent.height;
    *client = p->last_client;
    if (!pixels || p->last_extent.width > capacity_width || p->last_extent.height > capacity_height) {
        ok = true;
        goto out;
    }
    VkDeviceSize size = (VkDeviceSize)p->last_extent.width * p->last_extent.height * 4;
    if (p->readback_size < size) {
        if (p->readback)
            c->fn.vkDestroyBuffer(c->device, p->readback, nullptr);
        if (p->readback_memory)
            c->fn.vkFreeMemory(c->device, p->readback_memory, nullptr);
        p->readback = VK_NULL_HANDLE;
        p->readback_memory = VK_NULL_HANDLE;
        p->readback_size = 0;
        VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_SHARING_MODE_EXCLUSIVE};
        if (c->fn.vkCreateBuffer(c->device, &bi, nullptr, &p->readback) != VK_SUCCESS)
            goto out;
        VkMemoryRequirements req;
        c->fn.vkGetBufferMemoryRequirements(c->device, p->readback, &req);
        int32_t type = memory_type(c, req.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, (uint32_t)type};
        if (type < 0 || c->fn.vkAllocateMemory(c->device, &alloc, nullptr, &p->readback_memory) != VK_SUCCESS ||
            c->fn.vkBindBufferMemory(c->device, p->readback, p->readback_memory, 0) != VK_SUCCESS ||
            c->fn.vkMapMemory(c->device, p->readback_memory, 0, VK_WHOLE_SIZE, 0, (void **)&p->readback_map) !=
                VK_SUCCESS)
            goto out;
        p->readback_size = size;
    }
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, p->pool,
                                      VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    if (c->fn.vkAllocateCommandBuffers(c->device, &ai, &cmd) != VK_SUCCESS)
        goto out;
    VkFence fence;
    VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (c->fn.vkCreateFence(c->device, &fi, nullptr, &fence) != VK_SUCCESS) {
        c->fn.vkFreeCommandBuffers(c->device, p->pool, 1, &cmd);
        goto out;
    }
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    c->fn.vkBeginCommandBuffer(cmd, &begin);
    /* the present that wrote the image went first on this queue. */
    barrier(c, cmd, p->last, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {p->last_extent.width, p->last_extent.height, 1},
    };
    c->fn.vkCmdCopyImageToBuffer(cmd, p->last, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, p->readback, 1, &copy);
    c->fn.vkEndCommandBuffer(cmd);
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &cmd, 0, nullptr};
    if (submit_locked(c, p->last_queue, &submit, fence) == VK_SUCCESS &&
        c->fn.vkWaitForFences(c->device, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS) {
        for (uint32_t y = 0; y < p->last_extent.height; y++)
            memcpy((uint8_t *)pixels + (size_t)y * stride, p->readback_map + (size_t)y * p->last_extent.width * 4,
                   (size_t)p->last_extent.width * 4);
        ok = true;
    }
    c->fn.vkDestroyFence(c->device, fence, nullptr);
    c->fn.vkFreeCommandBuffers(c->device, p->pool, 1, &cmd);
out:
    pthread_mutex_unlock(&p->lock);
    presenter_put(c, p);
    return ok;
}
