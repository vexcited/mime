/*
 * guest swapchains render into host images. a present blits one into a host window swapchain and draws the
 * guest frame around it, or on Wayland and macOS into a surface over the client area alone, a Metal layer on
 * macOS. a presenter owns the window surface until its last swapchain is destroyed, then returns it to the
 * window system.
 *
 * acquire and present have no replies. each image has a binary semaphore signaled by its present blit; the
 * next acquire waits for it before signaling the guest.
 *
 * a presenter thread per connection handles host acquire, blit and present, so display waits do not block
 * later guest calls. the connection thread consumes guest semaphores and signals a timeline value for the
 * blit, then queues the present. acquiring an image waits for its pending present to be submitted.
 */
#include "mvk_host.h"
#include "display.h"
#include "gamma.h"

struct wl_display;
struct wl_surface;
typedef struct _XDisplay Display;
typedef unsigned long Window;
typedef unsigned long VisualID;
#include <vulkan/vulkan_wayland.h>
#include <vulkan/vulkan_xlib.h>
#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_IMAGES 8
#define MAX_HOST_IMAGES 16
#define FRAMES 3
#define MAX_PRESENTERS 128
#define ACQUIRE_TIMEOUT_NS 100000000ull
/* a swapchain that stopped presenting this long gives the host swapchain's settings to another one. */
#define OWNER_IDLE_NS 500000000ull
/* how long a presenter outlives its last swapchain, for the one a resize makes next. */
#define PARK_NS 500000000ull
#define MAX_PARKED 16
/* a capture keeps a copy of each frame this long, for the captures that follow it. */
#define LAST_KEEP_NS 5000000000ull

const char *const mvk_wsi_instance_extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
#ifdef __APPLE__
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
#else
    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
    nullptr,
};

bool mvk_wsi_device_extension(const char *name)
{
    return !strcmp(name, VK_KHR_SWAPCHAIN_EXTENSION_NAME) || !strcmp(name, VK_EXT_HDR_METADATA_EXTENSION_NAME);
}

/* what the values of an image mean: sRGB, scRGB where 1 is 80 nits, or BT.2020 through the curve of ST 2084. */
enum { SPACE_SDR, SPACE_SCRGB, SPACE_PQ };

typedef struct Presenter Presenter;

typedef struct Swapchain {
    struct Swapchain *next;
    uint64_t id;
    uint64_t serial; /* unique for the life of the process, unlike the address. */
    Presenter *presenter;
    VkFormat format;
    VkExtent2D extent;
    VkPresentModeKHR mode;
    uint32_t count;
    VkImage images[MAX_IMAGES];
    uint64_t image_ids[MAX_IMAGES];
    VkDeviceMemory memory[MAX_IMAGES];
    VkSemaphore released[MAX_IMAGES]; /* signaled by the last present of the image. */
    bool pending[MAX_IMAGES];
    uint32_t queued[MAX_IMAGES]; /* presents of the image the presenting thread did not submit yet. */
    bool flip; /* bottom-first rows, as OpenGL stores them. */
    bool premultiplied; /* the alpha of the images shows what is behind the window. */
    uint32_t space;
    /* what draws the images of one of high dynamic range as another space. */
    VkImageView views[MAX_IMAGES];
    VkDescriptorPool pool;
    VkDescriptorSet sets[MAX_IMAGES];
    /* the mastering display the program described, under the convert lock. */
    bool has_metadata;
    VkHdrMetadataEXT metadata;
} Swapchain;

typedef struct Frame {
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore acquired;
} Frame;

/* display gamma: compose the frame into an image, then draw it through the ramp. */
typedef struct GammaPass {
    bool broken; /* could not be made: frames show without the ramp. */
    VkFormat format; /* of the render pass and pipeline. */
    VkRenderPass pass;
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkSampler sampler;
    VkImage compose;
    VkDeviceMemory compose_memory;
    VkImageView compose_view;
    VkExtent2D compose_extent;
    VkFormat compose_format;
    VkImage ramp;
    VkDeviceMemory ramp_memory;
    VkImageView ramp_view;
    VkBuffer staging;
    VkDeviceMemory staging_memory;
    uint16_t *staging_map;
    uint32_t generation;
    bool ramp_stale; /* the staging buffer has a ramp update not yet uploaded. */
    bool ramp_valid;
    VkSwapchainKHR views_of;
    VkImageView views[MAX_HOST_IMAGES];
    VkFramebuffer framebuffers[MAX_HOST_IMAGES];
} GammaPass;

/* a guest image of high dynamic range drawn as another space, the size of the image. */
typedef struct ConvertTarget {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFramebuffer framebuffer;
    VkExtent2D extent;
    uint32_t space;
} ConvertTarget;

/* how a frame of high dynamic range shows in standard range: the nits of its white, cut or rolled off above. */
typedef struct Tone {
    bool on;
    float white;
    bool roll;
} Tone;

struct Presenter {
    uint32_t window;
    uint32_t source; /* the window of the guest it presents for, 0 when a window has one presenter. */
    uint32_t last_source; /* the window the last image was made for. */
    MvkTarget placed; /* the last the guest told of its layer, under g_presenters_lock. */
    MvkConnection *conn;
    int refs;
    pthread_mutex_t lock;
    uint32_t generation;
    bool attached;

    VkSurfaceKHR surface;
    uint32_t checked_family;
    bool supported;
    VkSwapchainKHR swapchain;
    VkFormat format;
    bool srgb;
    bool premultiplied; /* the swapchain composes with alpha. */
    bool premultiplied_wanted; /* what the last swapchain asked for, which the surface may not offer. */
    uint32_t space; /* of the swapchain. */
    bool deep_wanted; /* high dynamic range was asked for, which the surface may not offer. */
    bool metadata_set;
    VkHdrMetadataEXT metadata;
    /* the guest image as the swapchain's space, and as standard range for the copy when that is another. */
    ConvertTarget convert, standard;
    Tone tone;
    VkExtent2D extent;
    VkPresentModeKHR wanted_mode;
    bool stale;
    /* the swapchain whose settings the host swapchain follows, and when it last presented. */
    uint64_t owner;
    uint64_t owner_seen;
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

    /* the guest image last presented, which stays as it is until the guest draws into it again. */
    Swapchain *shown_from;
    uint32_t shown_index;
    VkExtent2D shown_extent;
    /* the last image the guest presented, kept for captures of the screen until want_last_until, and where it shows. */
    uint64_t want_last_until;
    VkImage last;
    VkDeviceMemory last_memory;
    VkExtent2D last_extent;
    VkFormat last_format;
    bool last_valid;
    MimeRect last_client;
    /* image serial and serial cached in the readback buffer. */
    uint64_t last_serial;
    uint64_t readback_serial;
    /* capture metadata has a separate lock because presentation holds the window lock for a frame. */
    pthread_mutex_t info_lock;
    bool info_valid;
    uint32_t info_width, info_height;
    MimeRect info_client;
    uint64_t info_serial;
    VkQueue last_queue;
    VkBuffer readback;
    VkDeviceMemory readback_memory;
    uint8_t *readback_map;
    VkDeviceSize readback_size;
    bool readback_coherent;

    GammaPass gamma;
};

typedef struct PresentJob {
    struct PresentJob *next;
    Swapchain *s;
    uint32_t index;
    MvkTarget target;
    VkQueue queue;
    uint32_t family;
    uint64_t ready; /* the value of the ready timeline the guest's semaphores signal, 0 for none. */
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
    /* presenters whose swapchain went, each with a reference until its time, and the thread that lets them go. */
    pthread_t park_thread;
    bool park_running, park_stop;
    pthread_mutex_t park_lock;
    pthread_cond_t park_cond;
    struct {
        Presenter *presenter;
        uint64_t until;
    } parked[MAX_PARKED];
    PFN_vkDestroySurfaceKHR DestroySurface;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR SurfaceSupport;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR SurfaceCapabilities;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR SurfaceFormats;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR SurfacePresentModes;
    PFN_vkCreateWaylandSurfaceKHR CreateWaylandSurface;
    PFN_vkCreateXlibSurfaceKHR CreateXlibSurface;
#ifdef __APPLE__
    PFN_vkCreateMetalSurfaceEXT CreateMetalSurface;
#endif
    PFN_vkCreateSwapchainKHR CreateSwapchain;
    PFN_vkDestroySwapchainKHR DestroySwapchain;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImages;
    PFN_vkAcquireNextImageKHR AcquireNextImage;
    PFN_vkQueuePresentKHR QueuePresent;
    PFN_vkSetHdrMetadataEXT SetHdrMetadata;
    /* what draws an image of high dynamic range as each space, made with the first swapchain of one. */
    pthread_mutex_t convert_lock;
    bool convert_tried, convert_ok;
    VkDescriptorSetLayout convert_set_layout;
    VkPipelineLayout convert_layout;
    VkSampler convert_sampler;
    struct {
        VkRenderPass pass;
        VkPipeline pipeline;
    } convert[3];
} MvkWsi;

static pthread_mutex_t g_presenters_lock = PTHREAD_MUTEX_INITIALIZER;
static Presenter *g_presenters[MAX_PRESENTERS];
static uint64_t g_swapchain_serial;

static MvkWsi *wsi_of(MvkConnection *c)
{
    if (c->wsi)
        return c->wsi;
    MvkWsi *w = calloc(1, sizeof(*w));
    if (!w)
        return nullptr;
    pthread_mutex_init(&w->lock, nullptr);
    pthread_cond_init(&w->cond, nullptr);
    pthread_mutex_init(&w->convert_lock, nullptr);
    pthread_mutex_init(&w->park_lock, nullptr);
    pthread_cond_init(&w->park_cond, nullptr);
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
#ifdef __APPLE__
    INSTANCE(CreateMetalSurface, "vkCreateMetalSurfaceEXT");
#endif
    DEVICE(CreateSwapchain, "vkCreateSwapchainKHR");
    DEVICE(DestroySwapchain, "vkDestroySwapchainKHR");
    DEVICE(GetSwapchainImages, "vkGetSwapchainImagesKHR");
    DEVICE(AcquireNextImage, "vkAcquireNextImageKHR");
    DEVICE(QueuePresent, "vkQueuePresentKHR");
    if (c->hdr_metadata)
        DEVICE(SetHdrMetadata, "vkSetHdrMetadataEXT");
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

/* a type of the bits in system memory, which the budget of video memory leaves alone. */
static int32_t system_memory_type(MvkConnection *c, uint32_t bits)
{
    VkPhysicalDeviceMemoryProperties props;
    c->fn.vkGetPhysicalDeviceMemoryProperties(c->physical_device, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((bits & (1u << i)) &&
            !(props.memoryHeaps[props.memoryTypes[i].heapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT))
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

/* ------------------------------------------------------------------ swapchains. */

static void destroy_swapchain_images(MvkConnection *c, Swapchain *s)
{
    for (uint32_t i = 0; i < s->count; i++)
        if (s->views[i])
            c->fn.vkDestroyImageView(c->device, s->views[i], nullptr);
    if (s->pool)
        c->fn.vkDestroyDescriptorPool(c->device, s->pool, nullptr);
    for (uint32_t i = 0; i < s->count; i++) {
        if (s->images[i])
            c->fn.vkDestroyImage(c->device, s->images[i], nullptr);
        if (s->memory[i])
            c->fn.vkFreeMemory(c->device, s->memory[i], nullptr);
        if (s->released[i])
            c->fn.vkDestroySemaphore(c->device, s->released[i], nullptr);
    }
}

static bool convert_ready(MvkConnection *c, MvkWsi *w);
static bool convert_views(MvkConnection *c, MvkWsi *w, Swapchain *s);

static uint32_t space_of(VkFormat format, VkColorSpaceKHR space)
{
    if (space == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT && format == VK_FORMAT_R16G16B16A16_SFLOAT)
        return SPACE_SCRGB;
    if (space == VK_COLOR_SPACE_HDR10_ST2084_EXT &&
        (format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || format == VK_FORMAT_A2R10G10B10_UNORM_PACK32))
        return SPACE_PQ;
    return SPACE_SDR;
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
    uint32_t space = space_of(format, (VkColorSpaceKHR)mvk_get_u32(d));
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
    VkCompositeAlphaFlagBitsKHR composite = (VkCompositeAlphaFlagBitsKHR)mvk_get_u32(d);
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    Swapchain *s = calloc(1, sizeof(*s));
    if (!d->error && c->device && s && count && count <= MAX_IMAGES && family_count <= 16 && view_count <= 16) {
        *s = (Swapchain){.id = id, .serial = __atomic_add_fetch(&g_swapchain_serial, 1, __ATOMIC_RELAXED),
                         .format = format, .extent = extent, .mode = mode, .count = count,
                         .premultiplied = composite == VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR};
        /* a mutable swapchain's views may differ in format; otherwise only usage the format allows. */
        bool mutable = (flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) != 0;
        /* one of high dynamic range is sampled by the pass that draws it as what the window shows. */
        if (space && !convert_ready(c, wsi_of(c)))
            space = SPACE_SDR;
        if (space)
            usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
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
        if (result == VK_SUCCESS && space && convert_views(c, c->wsi, s))
            s->space = space;
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

/* ------------------------------------------------------------------ presenters. */

static void wait_frames(MvkConnection *c, Presenter *p)
{
    for (uint32_t i = 0; i < FRAMES; i++)
        if (p->frames[i].fence)
            c->fn.vkWaitForFences(c->device, 1, &p->frames[i].fence, VK_TRUE, UINT64_MAX);
}

static void gamma_drop_views(MvkConnection *c, Presenter *p)
{
    GammaPass *g = &p->gamma;
    for (uint32_t i = 0; i < MAX_HOST_IMAGES; i++) {
        if (g->framebuffers[i])
            c->fn.vkDestroyFramebuffer(c->device, g->framebuffers[i], nullptr);
        if (g->views[i])
            c->fn.vkDestroyImageView(c->device, g->views[i], nullptr);
        g->framebuffers[i] = VK_NULL_HANDLE;
        g->views[i] = VK_NULL_HANDLE;
    }
    g->views_of = VK_NULL_HANDLE;
}

/* the host swapchain goes once no present waits for its semaphores. */
static void drop_swapchain(MvkConnection *c, Presenter *p)
{
    MvkWsi *w = c->wsi;
    gamma_drop_views(c, p);
    for (uint32_t i = 0; i < p->count; i++) {
        if (p->done[i])
            c->fn.vkDestroySemaphore(c->device, p->done[i], nullptr);
        p->done[i] = VK_NULL_HANDLE;
    }
    if (p->swapchain)
        w->DestroySwapchain(c->device, p->swapchain, nullptr);
    p->swapchain = VK_NULL_HANDLE;
    p->count = 0;
    p->metadata_set = false;
}

static void drop_last(MvkConnection *c, Presenter *p)
{
    if (p->last)
        c->fn.vkDestroyImage(c->device, p->last, nullptr);
    if (p->last_memory)
        c->fn.vkFreeMemory(c->device, p->last_memory, nullptr);
    if (p->readback)
        c->fn.vkDestroyBuffer(c->device, p->readback, nullptr);
    if (p->readback_memory)
        c->fn.vkFreeMemory(c->device, p->readback_memory, nullptr);
    p->last = VK_NULL_HANDLE;
    p->last_memory = VK_NULL_HANDLE;
    p->last_valid = false;
    p->readback = VK_NULL_HANDLE;
    p->readback_memory = VK_NULL_HANDLE;
    p->readback_map = nullptr;
    p->readback_size = 0;
    p->readback_serial = 0;
}

static void drop_gdi(MvkConnection *c, Presenter *p)
{
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
}

static void gamma_drop_compose(MvkConnection *c, GammaPass *g);
static void convert_drop(MvkConnection *c, ConvertTarget *t);

/* the window shows nothing for now: what is the window's size goes, and comes back at the next present that shows. */
static void drop_window_images(MvkConnection *c, Presenter *p)
{
    wait_frames(c, p);
    drop_swapchain(c, p);
    drop_last(c, p);
    drop_gdi(c, p);
    gamma_drop_compose(c, &p->gamma);
    convert_drop(c, &p->convert);
    convert_drop(c, &p->standard);
}

static bool has_window_images(const Presenter *p)
{
    return p->swapchain || p->last || p->gdi || p->gamma.compose || p->convert.image || p->standard.image;
}

static void drop_surface(MvkConnection *c, Presenter *p)
{
    wait_frames(c, p);
    drop_window_images(c, p);
    if (p->surface)
        c->wsi->DestroySurface(c->instance, p->surface, nullptr);
    p->surface = VK_NULL_HANDLE;
    p->checked_family = UINT32_MAX;
}

static void gamma_destroy(MvkConnection *c, Presenter *p);

/* the device must be idle before destroying the presenter. */
static void destroy_presenter(MvkConnection *c, Presenter *p)
{
    pthread_mutex_lock(&p->lock);
    drop_surface(c, p);
    for (uint32_t i = 0; i < FRAMES; i++) {
        if (p->frames[i].fence)
            c->fn.vkDestroyFence(c->device, p->frames[i].fence, nullptr);
        if (p->frames[i].acquired)
            c->fn.vkDestroySemaphore(c->device, p->frames[i].acquired, nullptr);
    }
    if (p->pool)
        c->fn.vkDestroyCommandPool(c->device, p->pool, nullptr);
    gamma_destroy(c, p);
    bool attached = p->attached;
    pthread_mutex_unlock(&p->lock);
    pthread_mutex_destroy(&p->lock);
    pthread_mutex_destroy(&p->info_lock);
    if (attached)
        mime_ui_vk_detach(p->window, p->source);
    free(p->shadow);
    free(p);
}

/* creates a presenter unless another connection owns the window. */
static Presenter *presenter_get(MvkConnection *c, uint32_t window, uint32_t source)
{
    pthread_mutex_lock(&g_presenters_lock);
    Presenter *p = nullptr, **slot = nullptr;
    for (int i = 0; i < MAX_PRESENTERS; i++) {
        if (g_presenters[i] && g_presenters[i]->window == window && g_presenters[i]->source == source)
            p = g_presenters[i];
        else if (!g_presenters[i] && !slot)
            slot = &g_presenters[i];
    }
    if (p && p->conn != c)
        p = nullptr;
    else if (!p && slot && (p = calloc(1, sizeof(*p)))) {
        p->window = window;
        p->source = source;
        p->conn = c;
        p->checked_family = UINT32_MAX;
        p->pool_family = UINT32_MAX;
        pthread_mutex_init(&p->lock, nullptr);
        pthread_mutex_init(&p->info_lock, nullptr);
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

static void *parking_main(void *arg)
{
    MvkConnection *c = arg;
    MvkWsi *w = c->wsi;
    mime_set_thread_name("mime-vk-park");
    pthread_mutex_lock(&w->park_lock);
    while (!w->park_stop) {
        uint64_t now = mime_now_ns(), next = 0;
        Presenter *gone = nullptr;
        for (int i = 0; i < MAX_PARKED && !gone; i++) {
            if (!w->parked[i].presenter)
                continue;
            if (w->parked[i].until <= now) {
                gone = w->parked[i].presenter;
                w->parked[i].presenter = nullptr;
            } else if (!next || w->parked[i].until < next) {
                next = w->parked[i].until;
            }
        }
        if (gone) {
            pthread_mutex_unlock(&w->park_lock);
            presenter_put(c, gone);
            pthread_mutex_lock(&w->park_lock);
        } else if (next) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            uint64_t at = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec + (next - now);
            ts = (struct timespec){(time_t)(at / 1000000000ull), (long)(at % 1000000000ull)};
            pthread_cond_timedwait(&w->park_cond, &w->park_lock, &ts);
        } else {
            pthread_cond_wait(&w->park_cond, &w->park_lock);
        }
    }
    pthread_mutex_unlock(&w->park_lock);
    return nullptr;
}

/*
 * takes the reference of a swapchain that goes. a program that resizes its window destroys its swapchain and makes
 * another, and the presenter the next one finds keeps the window's surface and what it shows in between.
 */
static void presenter_park(MvkConnection *c, Presenter *p)
{
    MvkWsi *w = c->wsi;
    pthread_mutex_lock(&w->park_lock);
    if (!w->park_running && !w->park_stop)
        w->park_running = !pthread_create(&w->park_thread, nullptr, parking_main, c);
    int slot = -1;
    bool held = false;
    for (int i = 0; i < MAX_PARKED && w->park_running; i++) {
        if (w->parked[i].presenter == p) {
            slot = i;
            held = true;
            break;
        }
        if (!w->parked[i].presenter && slot < 0)
            slot = i;
    }
    if (slot >= 0) {
        w->parked[slot].presenter = p;
        w->parked[slot].until = mime_now_ns() + PARK_NS;
        pthread_cond_broadcast(&w->park_cond);
    }
    pthread_mutex_unlock(&w->park_lock);
    if (slot < 0 || held)
        presenter_put(c, p);
}

static void stop_parking(MvkConnection *c)
{
    MvkWsi *w = c->wsi;
    pthread_mutex_lock(&w->park_lock);
    w->park_stop = true;
    pthread_cond_broadcast(&w->park_cond);
    bool running = w->park_running;
    pthread_mutex_unlock(&w->park_lock);
    if (running)
        pthread_join(w->park_thread, nullptr);
    w->park_running = false;
    for (int i = 0; i < MAX_PARKED; i++) {
        if (w->parked[i].presenter)
            presenter_put(c, w->parked[i].presenter);
        w->parked[i].presenter = nullptr;
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
        if (p->surface || has_window_images(p)) {
            MvkConnection *c = p->conn;
            pthread_mutex_lock(&c->queue_lock);
            c->fn.vkDeviceWaitIdle(c->device);
            pthread_mutex_unlock(&c->queue_lock);
            drop_surface(c, p);
        }
        pthread_mutex_unlock(&p->lock);
    }
    pthread_mutex_unlock(&g_presenters_lock);
}

void mime_vk_window_hidden(uint32_t id)
{
    pthread_mutex_lock(&g_presenters_lock);
    for (int i = 0; i < MAX_PRESENTERS; i++) {
        Presenter *p = g_presenters[i];
        if (!p || p->window != id)
            continue;
        pthread_mutex_lock(&p->lock);
        if (has_window_images(p)) {
            MvkConnection *c = p->conn;
            pthread_mutex_lock(&c->queue_lock);
            c->fn.vkDeviceWaitIdle(c->device);
            pthread_mutex_unlock(&c->queue_lock);
            drop_window_images(c, p);
        }
        pthread_mutex_unlock(&p->lock);
    }
    pthread_mutex_unlock(&g_presenters_lock);
}

/* takes the host window from SDL and makes the surface, outside the presenter's lock. */
static void presenter_attach(MvkConnection *c, Presenter *p, MimeRect area, uint32_t order)
{
    pthread_mutex_lock(&p->lock);
    uint32_t generation = p->generation;
    bool need = !p->surface;
    pthread_mutex_unlock(&p->lock);
    if (!need)
        return;
    MimeUiNative native;
    if (!mime_ui_vk_attach(p->window, p->source, area, order, &native))
        return;
    pthread_mutex_lock(&p->lock);
    /* one detach answers the presenter's attaches. */
    if (p->attached)
        mime_ui_vk_detach(p->window, p->source);
    p->attached = true;
    if (p->generation == generation && !p->surface) {
        MvkWsi *w = c->wsi;
        VkResult r = VK_ERROR_EXTENSION_NOT_PRESENT;
#ifdef __APPLE__
        if (native.metal && w->CreateMetalSurface) {
            VkMetalSurfaceCreateInfoEXT info = {VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT, nullptr, 0,
                                                native.surface};
            r = w->CreateMetalSurface(c->instance, &info, nullptr, &p->surface);
        } else
#endif
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
        } else {
            mvk_instance_presented(c);
        }
    }
    pthread_mutex_unlock(&p->lock);
}

static VkSurfaceFormatKHR pick_format(MvkConnection *c, Presenter *p, bool srgb, bool deep)
{
    VkSurfaceFormatKHR formats[64];
    uint32_t n = 64;
    if (c->wsi->SurfaceFormats(c->physical_device, p->surface, &n, formats) < 0 || !n)
        return (VkSurfaceFormatKHR){VK_FORMAT_UNDEFINED};
    /* HDR10 first, which every compositor that shows high dynamic range takes. */
    for (uint32_t want = SPACE_PQ; deep && want >= SPACE_SCRGB; want--)
        for (uint32_t i = 0; i < n; i++)
            if (space_of(formats[i].format, formats[i].colorSpace) == want)
                return formats[i];
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
                           VkPresentModeKHR mode, bool premultiplied, bool deep)
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
    VkSurfaceFormatKHR format = pick_format(c, p, srgb, deep);
    if (format.format == VK_FORMAT_UNDEFINED)
        return false;
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount)
        count = caps.maxImageCount;
    if (count > MAX_HOST_IMAGES)
        count = MAX_HOST_IMAGES;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (premultiplied && (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR))
        alpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    else
        premultiplied = false;
    if (!(caps.supportedCompositeAlpha & alpha))
        alpha = (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
    /* the old swapchain goes first when both would not fit. */
    VkDeviceSize bytes = (VkDeviceSize)count * extent.width * extent.height *
                         (format.format == VK_FORMAT_R16G16B16A16_SFLOAT ? 8 : 4);
    if (p->swapchain && !mvk_budget_fits(c, bytes)) {
        pthread_mutex_lock(&c->queue_lock);
        c->fn.vkQueueWaitIdle(queue);
        pthread_mutex_unlock(&c->queue_lock);
        drop_swapchain(c, p);
    }
    if (!p->swapchain && !mvk_budget_fits(c, bytes))
        return false;
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
    /* the old swapchain, retired by the new one, goes once no work uses it, even if replacement failed. */
    if (p->swapchain) {
        pthread_mutex_lock(&c->queue_lock);
        c->fn.vkQueueWaitIdle(queue);
        pthread_mutex_unlock(&c->queue_lock);
        drop_swapchain(c, p);
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
    p->premultiplied = premultiplied;
    p->space = space_of(format.format, format.colorSpace);
    p->deep_wanted = deep;
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
    drop_gdi(c, p);
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

/* allocates a guest-sized image to retain the last present; false on failure. */
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
    p->readback_serial = 0;
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
    VkResult made = type < 0 ? VK_ERROR_OUT_OF_DEVICE_MEMORY
                             : c->fn.vkAllocateMemory(c->device, &alloc, nullptr, &p->last_memory);
    /* a game that fills its video memory budget leaves no room for the copy, which system memory holds as well. */
    if (made != VK_SUCCESS && (type = system_memory_type(c, req.memoryTypeBits)) >= 0) {
        alloc.memoryTypeIndex = (uint32_t)type;
        made = c->fn.vkAllocateMemory(c->device, &alloc, nullptr, &p->last_memory);
    }
    if (made != VK_SUCCESS || c->fn.vkBindImageMemory(c->device, p->last, p->last_memory, 0) != VK_SUCCESS) {
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

/* ------------------------------------------------------------------ the display's gamma. */

static void barrier(MvkConnection *c, VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                    VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                    VkPipelineStageFlags dst_stage);

/* the unorm twin of a format the pass reads raw, undefined for those it leaves alone. */
static VkFormat unorm_of(VkFormat f)
{
    switch (f) {
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_R8G8B8A8_UNORM:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return f;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

static void gamma_drop_pipeline(MvkConnection *c, GammaPass *g)
{
    if (g->pipeline)
        c->fn.vkDestroyPipeline(c->device, g->pipeline, nullptr);
    if (g->pass)
        c->fn.vkDestroyRenderPass(c->device, g->pass, nullptr);
    g->pipeline = VK_NULL_HANDLE;
    g->pass = VK_NULL_HANDLE;
    g->format = VK_FORMAT_UNDEFINED;
}

static void gamma_drop_compose(MvkConnection *c, GammaPass *g)
{
    if (g->compose_view)
        c->fn.vkDestroyImageView(c->device, g->compose_view, nullptr);
    if (g->compose)
        c->fn.vkDestroyImage(c->device, g->compose, nullptr);
    if (g->compose_memory)
        c->fn.vkFreeMemory(c->device, g->compose_memory, nullptr);
    g->compose_view = VK_NULL_HANDLE;
    g->compose = VK_NULL_HANDLE;
    g->compose_memory = VK_NULL_HANDLE;
    g->compose_extent = (VkExtent2D){0, 0};
}

static void gamma_destroy(MvkConnection *c, Presenter *p)
{
    GammaPass *g = &p->gamma;
    gamma_drop_views(c, p);
    gamma_drop_pipeline(c, g);
    gamma_drop_compose(c, g);
    if (g->ramp_view)
        c->fn.vkDestroyImageView(c->device, g->ramp_view, nullptr);
    if (g->ramp)
        c->fn.vkDestroyImage(c->device, g->ramp, nullptr);
    if (g->ramp_memory)
        c->fn.vkFreeMemory(c->device, g->ramp_memory, nullptr);
    if (g->staging)
        c->fn.vkDestroyBuffer(c->device, g->staging, nullptr);
    if (g->staging_memory)
        c->fn.vkFreeMemory(c->device, g->staging_memory, nullptr);
    if (g->sampler)
        c->fn.vkDestroySampler(c->device, g->sampler, nullptr);
    if (g->pool)
        c->fn.vkDestroyDescriptorPool(c->device, g->pool, nullptr);
    if (g->layout)
        c->fn.vkDestroyPipelineLayout(c->device, g->layout, nullptr);
    if (g->set_layout)
        c->fn.vkDestroyDescriptorSetLayout(c->device, g->set_layout, nullptr);
    memset(g, 0, sizeof(*g));
}

static bool gamma_image(MvkConnection *c, VkImageCreateInfo *info, VkImage *image, VkDeviceMemory *memory,
                        VkFormat view_format, VkImageView *view)
{
    if (c->fn.vkCreateImage(c->device, info, nullptr, image) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    c->fn.vkGetImageMemoryRequirements(c->device, *image, &req);
    int32_t type = memory_type(c, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, (uint32_t)type};
    if (type < 0 || c->fn.vkAllocateMemory(c->device, &alloc, nullptr, memory) != VK_SUCCESS ||
        c->fn.vkBindImageMemory(c->device, *image, *memory, 0) != VK_SUCCESS)
        return false;
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = view_format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    return c->fn.vkCreateImageView(c->device, &vi, nullptr, view) == VK_SUCCESS;
}

/* layouts, sampler, ramp image and staging buffer live with the presenter. */
static bool gamma_base(MvkConnection *c, GammaPass *g)
{
    if (g->set)
        return true;
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo sli = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 2, bindings};
    if (c->fn.vkCreateDescriptorSetLayout(c->device, &sli, nullptr, &g->set_layout) != VK_SUCCESS)
        return false;
    VkPushConstantRange push = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4};
    VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &g->set_layout, 1,
                                      &push};
    if (c->fn.vkCreatePipelineLayout(c->device, &pli, nullptr, &g->layout) != VK_SUCCESS)
        return false;
    VkSamplerCreateInfo si = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (c->fn.vkCreateSampler(c->device, &si, nullptr, &g->sampler) != VK_SUCCESS)
        return false;
    VkImageCreateInfo ri = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R16G16B16A16_UNORM,
        .extent = {256, 1, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!gamma_image(c, &ri, &g->ramp, &g->ramp_memory, VK_FORMAT_R16G16B16A16_UNORM, &g->ramp_view))
        return false;
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, 256 * 8, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_SHARING_MODE_EXCLUSIVE};
    if (c->fn.vkCreateBuffer(c->device, &bi, nullptr, &g->staging) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    c->fn.vkGetBufferMemoryRequirements(c->device, g->staging, &req);
    int32_t type = memory_type(c, req.memoryTypeBits,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, (uint32_t)type};
    if (type < 0 || c->fn.vkAllocateMemory(c->device, &alloc, nullptr, &g->staging_memory) != VK_SUCCESS ||
        c->fn.vkBindBufferMemory(c->device, g->staging, g->staging_memory, 0) != VK_SUCCESS ||
        c->fn.vkMapMemory(c->device, g->staging_memory, 0, VK_WHOLE_SIZE, 0, (void **)&g->staging_map) != VK_SUCCESS)
        return false;
    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &size};
    if (c->fn.vkCreateDescriptorPool(c->device, &dpi, nullptr, &g->pool) != VK_SUCCESS)
        return false;
    VkDescriptorSetAllocateInfo dsi = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, g->pool, 1,
                                       &g->set_layout};
    return c->fn.vkAllocateDescriptorSets(c->device, &dsi, &g->set) == VK_SUCCESS;
}

/* a render pass over one image of format f, left in the given layout. */
static bool make_pass(MvkConnection *c, VkFormat f, VkImageLayout final, VkRenderPass *pass)
{
    VkAttachmentDescription attachment = {0, f, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                          VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                          VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, final};
    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    .colorAttachmentCount = 1,
                                    .pColorAttachments = &ref};
    VkSubpassDependency dependency = {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0};
    VkRenderPassCreateInfo rpi = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0, 1, &attachment, 1, &subpass, 1,
                                  &dependency};
    return c->fn.vkCreateRenderPass(c->device, &rpi, nullptr, pass) == VK_SUCCESS;
}

/* a pipeline that draws one triangle over the whole pass. */
static bool make_pipeline(MvkConnection *c, VkRenderPass pass, VkPipelineLayout layout, const uint32_t *vs,
                          size_t vs_size, const uint32_t *fs, size_t fs_size, VkPipeline *pipeline)
{
    VkShaderModule modules[2] = {};
    VkShaderModuleCreateInfo vmi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, vs_size, vs};
    VkShaderModuleCreateInfo fmi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, fs_size, fs};
    bool ok = c->fn.vkCreateShaderModule(c->device, &vmi, nullptr, &modules[0]) == VK_SUCCESS &&
              c->fn.vkCreateShaderModule(c->device, &fmi, nullptr, &modules[1]) == VK_SUCCESS;
    if (ok) {
        VkPipelineShaderStageCreateInfo stages[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, modules[0],
             "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, modules[1],
             "main", nullptr},
        };
        VkPipelineVertexInputStateCreateInfo vertex = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                           nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
        VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0,
                                                      1, nullptr, 1, nullptr};
        VkPipelineRasterizationStateCreateInfo raster = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .lineWidth = 1.0f,
        };
        VkPipelineMultisampleStateCreateInfo multisample = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };
        VkPipelineColorBlendAttachmentState blend = {.colorWriteMask = 0xF};
        VkPipelineColorBlendStateCreateInfo blending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                        .attachmentCount = 1,
                                                        .pAttachments = &blend};
        VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2,
                                                    dynamic_states};
        VkGraphicsPipelineCreateInfo gpi = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2,
            .pStages = stages,
            .pVertexInputState = &vertex,
            .pInputAssemblyState = &assembly,
            .pViewportState = &viewport,
            .pRasterizationState = &raster,
            .pMultisampleState = &multisample,
            .pColorBlendState = &blending,
            .pDynamicState = &dynamic,
            .layout = layout,
            .renderPass = pass,
        };
        ok = c->fn.vkCreateGraphicsPipelines(c->device, VK_NULL_HANDLE, 1, &gpi, nullptr, pipeline) == VK_SUCCESS;
    }
    for (int i = 0; i < 2; i++)
        if (modules[i])
            c->fn.vkDestroyShaderModule(c->device, modules[i], nullptr);
    return ok;
}

/* the render pass and pipeline that write a swapchain image of format f. */
static bool gamma_pipeline(MvkConnection *c, GammaPass *g, VkFormat f)
{
    if (g->pipeline && g->format == f)
        return true;
    gamma_drop_pipeline(c, g);
    const uint32_t *vs, *fs;
    size_t vs_size, fs_size;
    if (!mime_gamma_shaders(&vs, &vs_size, &fs, &fs_size) ||
        !make_pass(c, f, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &g->pass) ||
        !make_pipeline(c, g->pass, g->layout, vs, vs_size, fs, fs_size, &g->pipeline))
        return false;
    g->format = f;
    return true;
}

/* the image frames are composed in, the swapchain's size and format, read raw through a unorm view. */
static bool gamma_compose(MvkConnection *c, Presenter *p)
{
    GammaPass *g = &p->gamma;
    if (g->compose && g->compose_extent.width == p->extent.width && g->compose_extent.height == p->extent.height &&
        g->compose_format == p->format)
        return true;
    gamma_drop_compose(c, g);
    VkImageCreateInfo ii = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = p->format,
        .extent = {p->extent.width, p->extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!gamma_image(c, &ii, &g->compose, &g->compose_memory, unorm_of(p->format), &g->compose_view))
        return false;
    g->compose_extent = p->extent;
    g->compose_format = p->format;
    VkDescriptorImageInfo images[2] = {
        {g->sampler, g->compose_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {g->sampler, g->ramp_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };
    VkWriteDescriptorSet writes[2] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, g->set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         &images[0], nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, g->set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         &images[1], nullptr, nullptr},
    };
    c->fn.vkUpdateDescriptorSets(c->device, 2, writes, 0, nullptr);
    return true;
}

static bool gamma_views(MvkConnection *c, Presenter *p)
{
    GammaPass *g = &p->gamma;
    if (g->views_of == p->swapchain)
        return true;
    gamma_drop_views(c, p);
    for (uint32_t i = 0; i < p->count; i++) {
        VkImageViewCreateInfo vi = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = p->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = p->format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        if (c->fn.vkCreateImageView(c->device, &vi, nullptr, &g->views[i]) != VK_SUCCESS)
            return false;
        VkFramebufferCreateInfo fi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, g->pass, 1, &g->views[i],
                                      p->extent.width, p->extent.height, 1};
        if (c->fn.vkCreateFramebuffer(c->device, &fi, nullptr, &g->framebuffers[i]) != VK_SUCCESS)
            return false;
    }
    g->views_of = p->swapchain;
    return true;
}

/* prepares gamma resources and stages any new ramp for this frame. */
static bool gamma_prepare(MvkConnection *c, Presenter *p)
{
    GammaPass *g = &p->gamma;
    uint16_t ramp[3][256];
    uint32_t generation;
    if (p->space || !mime_gamma_ramp(ramp, &generation) || g->broken || unorm_of(p->format) == VK_FORMAT_UNDEFINED)
        return false;
    if (!gamma_base(c, g) || !gamma_pipeline(c, g, p->format) || !gamma_compose(c, p) || !gamma_views(c, p)) {
        mime_warn("vulkan: no gamma pass on window %#x", p->window);
        wait_frames(c, p);
        gamma_destroy(c, p);
        g->broken = true;
        return false;
    }
    if (generation != g->generation || !g->ramp_valid) {
        wait_frames(c, p);
        for (int i = 0; i < 256; i++) {
            g->staging_map[i * 4 + 0] = ramp[0][i];
            g->staging_map[i * 4 + 1] = ramp[1][i];
            g->staging_map[i * 4 + 2] = ramp[2][i];
            g->staging_map[i * 4 + 3] = 0xFFFF;
        }
        g->generation = generation;
        g->ramp_stale = true;
    }
    return true;
}

/* the composed frame drawn through the ramp into the swapchain image, which the render pass leaves ready to present. */
static void gamma_record(MvkConnection *c, Presenter *p, VkCommandBuffer cmd, uint32_t target)
{
    GammaPass *g = &p->gamma;
    if (g->ramp_stale) {
        barrier(c, cmd, g->ramp, g->ramp_valid ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {256, 1, 1}};
        c->fn.vkCmdCopyBufferToImage(cmd, g->staging, g->ramp, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier(c, cmd, g->ramp, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        g->ramp_stale = false;
        g->ramp_valid = true;
    }
    barrier(c, cmd, g->compose, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    VkRenderPassBeginInfo begin = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr, g->pass, g->framebuffers[target],
                                   {{0, 0}, p->extent}, 0, nullptr};
    c->fn.vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    c->fn.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pipeline);
    c->fn.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g->layout, 0, 1, &g->set, 0, nullptr);
    uint32_t srgb = is_srgb(p->format);
    c->fn.vkCmdPushConstants(cmd, g->layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(srgb), &srgb);
    VkViewport viewport = {0, 0, (float)p->extent.width, (float)p->extent.height, 0, 1};
    VkRect2D scissor = {{0, 0}, p->extent};
    c->fn.vkCmdSetViewport(cmd, 0, 1, &viewport);
    c->fn.vkCmdSetScissor(cmd, 0, 1, &scissor);
    c->fn.vkCmdDraw(cmd, 3, 1, 0, 0);
    c->fn.vkCmdEndRenderPass(cmd);
}

/* ------------------------------------------------------------------ high dynamic range. */

static const VkFormat CONVERT_FORMATS[3] = {
    [SPACE_SDR] = VK_FORMAT_B8G8R8A8_UNORM,
    [SPACE_SCRGB] = VK_FORMAT_R16G16B16A16_SFLOAT,
    [SPACE_PQ] = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
};

typedef struct ConvertPush {
    uint32_t source, target;
    float white;
    uint32_t roll;
    int32_t flip; /* the height of an image that has its rows bottom first, else 0. */
} ConvertPush;

static bool convert_make(MvkConnection *c, MvkWsi *w)
{
    const uint32_t *vs, *fs;
    size_t vs_size, fs_size;
    if (!mime_convert_shaders(&vs, &vs_size, &fs, &fs_size))
        return false;
    VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                                            nullptr};
    VkDescriptorSetLayoutCreateInfo sli = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &binding};
    if (c->fn.vkCreateDescriptorSetLayout(c->device, &sli, nullptr, &w->convert_set_layout) != VK_SUCCESS)
        return false;
    VkPushConstantRange push = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ConvertPush)};
    VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1,
                                      &w->convert_set_layout, 1, &push};
    if (c->fn.vkCreatePipelineLayout(c->device, &pli, nullptr, &w->convert_layout) != VK_SUCCESS)
        return false;
    VkSamplerCreateInfo si = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (c->fn.vkCreateSampler(c->device, &si, nullptr, &w->convert_sampler) != VK_SUCCESS)
        return false;
    for (int i = 0; i < 3; i++)
        if (!make_pass(c, CONVERT_FORMATS[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &w->convert[i].pass) ||
            !make_pipeline(c, w->convert[i].pass, w->convert_layout, vs, vs_size, fs, fs_size, &w->convert[i].pipeline))
            return false;
    return true;
}

static bool convert_ready(MvkConnection *c, MvkWsi *w)
{
    if (!w)
        return false;
    pthread_mutex_lock(&w->convert_lock);
    if (!w->convert_tried) {
        w->convert_tried = true;
        w->convert_ok = convert_make(c, w);
        if (!w->convert_ok)
            mime_warn("vulkan: no pass for high dynamic range, its swapchains show as they are");
    }
    bool ok = w->convert_ok;
    pthread_mutex_unlock(&w->convert_lock);
    return ok;
}

static void convert_destroy(MvkConnection *c, MvkWsi *w)
{
    for (int i = 0; i < 3; i++) {
        if (w->convert[i].pipeline)
            c->fn.vkDestroyPipeline(c->device, w->convert[i].pipeline, nullptr);
        if (w->convert[i].pass)
            c->fn.vkDestroyRenderPass(c->device, w->convert[i].pass, nullptr);
    }
    if (w->convert_sampler)
        c->fn.vkDestroySampler(c->device, w->convert_sampler, nullptr);
    if (w->convert_layout)
        c->fn.vkDestroyPipelineLayout(c->device, w->convert_layout, nullptr);
    if (w->convert_set_layout)
        c->fn.vkDestroyDescriptorSetLayout(c->device, w->convert_set_layout, nullptr);
}

/* a view and a descriptor set for each image of the swapchain. */
static bool convert_views(MvkConnection *c, MvkWsi *w, Swapchain *s)
{
    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, s->count};
    VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, s->count, 1, &size};
    if (c->fn.vkCreateDescriptorPool(c->device, &dpi, nullptr, &s->pool) != VK_SUCCESS)
        return false;
    for (uint32_t i = 0; i < s->count; i++) {
        VkImageViewCreateInfo vi = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = s->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = s->format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        VkDescriptorSetAllocateInfo dsi = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, s->pool, 1,
                                           &w->convert_set_layout};
        if (c->fn.vkCreateImageView(c->device, &vi, nullptr, &s->views[i]) != VK_SUCCESS ||
            c->fn.vkAllocateDescriptorSets(c->device, &dsi, &s->sets[i]) != VK_SUCCESS)
            return false;
        VkDescriptorImageInfo image = {w->convert_sampler, s->views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s->sets[i], 0, 0, 1,
                                      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &image, nullptr, nullptr};
        c->fn.vkUpdateDescriptorSets(c->device, 1, &write, 0, nullptr);
    }
    return true;
}

static void convert_drop(MvkConnection *c, ConvertTarget *t)
{
    if (t->framebuffer)
        c->fn.vkDestroyFramebuffer(c->device, t->framebuffer, nullptr);
    if (t->view)
        c->fn.vkDestroyImageView(c->device, t->view, nullptr);
    if (t->image)
        c->fn.vkDestroyImage(c->device, t->image, nullptr);
    if (t->memory)
        c->fn.vkFreeMemory(c->device, t->memory, nullptr);
    *t = (ConvertTarget){};
}

static bool convert_target(MvkConnection *c, Presenter *p, ConvertTarget *t, VkExtent2D extent, uint32_t space)
{
    if (t->framebuffer && t->extent.width == extent.width && t->extent.height == extent.height && t->space == space)
        return true;
    wait_frames(c, p);
    convert_drop(c, t);
    VkImageCreateInfo ii = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = CONVERT_FORMATS[space],
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkFramebufferCreateInfo fi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, c->wsi->convert[space].pass, 1,
                                  &t->view, extent.width, extent.height, 1};
    if (!gamma_image(c, &ii, &t->image, &t->memory, ii.format, &t->view) ||
        c->fn.vkCreateFramebuffer(c->device, &fi, nullptr, &t->framebuffer) != VK_SUCCESS) {
        convert_drop(c, t);
        return false;
    }
    t->extent = extent;
    t->space = space;
    return true;
}

/* draws image index of s as the target's space, which is left to be blitted from. */
static void convert_draw(MvkConnection *c, VkCommandBuffer cmd, Swapchain *s, uint32_t index, ConvertTarget *t,
                         Tone tone)
{
    MvkWsi *w = c->wsi;
    VkRenderPassBeginInfo begin = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr, w->convert[t->space].pass,
                                   t->framebuffer, {{0, 0}, t->extent}, 0, nullptr};
    c->fn.vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    c->fn.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, w->convert[t->space].pipeline);
    c->fn.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, w->convert_layout, 0, 1, &s->sets[index], 0,
                                  nullptr);
    ConvertPush push = {s->space, t->space, tone.white, tone.roll, s->flip ? (int32_t)s->extent.height : 0};
    c->fn.vkCmdPushConstants(cmd, w->convert_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    VkViewport viewport = {0, 0, (float)t->extent.width, (float)t->extent.height, 0, 1};
    VkRect2D scissor = {{0, 0}, t->extent};
    c->fn.vkCmdSetViewport(cmd, 0, 1, &viewport);
    c->fn.vkCmdSetScissor(cmd, 0, 1, &scissor);
    c->fn.vkCmdDraw(cmd, 3, 1, 0, 0);
    c->fn.vkCmdEndRenderPass(cmd);
    barrier(c, cmd, t->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
}

/* the guest's image goes back to the layout it presents in, from the one it was read in. */
static void release_source(MvkConnection *c, VkCommandBuffer cmd, VkImage src, bool sampled)
{
    if (sampled)
        barrier(c, cmd, src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_SHADER_READ_BIT, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    else
        barrier(c, cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_TRANSFER_READ_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

/* whether a monitor shows high dynamic range, which is what the guest was told, and what that one can show. */
static bool display_hdr(MimeMonitorColor *color)
{
    MimeMonitor monitors[MIME_MAX_MONITORS];
    uint32_t n = mime_ui_get_monitors(monitors, MIME_MAX_MONITORS);
    for (uint32_t i = 0; i < n && i < MIME_MAX_MONITORS; i++)
        if (monitors[i].flags & MIME_MONITOR_HDR) {
            *color = monitors[i].color;
            return true;
        }
    return false;
}

/*
 * scRGB on a monitor of standard range has its white at 1, and Windows cuts what is over. a program told of high
 * dynamic range draws white where the monitor's description put it, and its highlights roll off.
 */
static Tone tone_of(const Swapchain *s, bool told)
{
    bool plain = s->space == SPACE_SCRGB && !told;
    return (Tone){true, plain ? 80.0f : 203.0f, !plain};
}

/* tells the compositor what the frames were made for: the program's word, else the monitor itself. */
static void show_metadata(MvkConnection *c, Presenter *p, Swapchain *s, const MimeMonitorColor *m)
{
    MvkWsi *w = c->wsi;
    if (!p->space || !w->SetHdrMetadata)
        return;
    VkHdrMetadataEXT data;
    memset(&data, 0, sizeof(data));
    pthread_mutex_lock(&w->convert_lock);
    bool given = s->has_metadata;
    if (given)
        data = s->metadata;
    pthread_mutex_unlock(&w->convert_lock);
    if (!given) {
        if (!m->max_nits)
            return;
        data.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
        data.displayPrimaryRed = (VkXYColorEXT){0.708f, 0.292f};
        data.displayPrimaryGreen = (VkXYColorEXT){0.170f, 0.797f};
        data.displayPrimaryBlue = (VkXYColorEXT){0.131f, 0.046f};
        data.whitePoint = (VkXYColorEXT){0.3127f, 0.3290f};
        if (m->red_x && m->green_x && m->blue_x && m->white_x) {
            data.displayPrimaryRed = (VkXYColorEXT){m->red_x / 1e6f, m->red_y / 1e6f};
            data.displayPrimaryGreen = (VkXYColorEXT){m->green_x / 1e6f, m->green_y / 1e6f};
            data.displayPrimaryBlue = (VkXYColorEXT){m->blue_x / 1e6f, m->blue_y / 1e6f};
            data.whitePoint = (VkXYColorEXT){m->white_x / 1e6f, m->white_y / 1e6f};
        }
        data.maxLuminance = m->max_nits / 1e4f;
        data.minLuminance = m->min_nits / 1e4f;
        data.maxContentLightLevel = data.maxLuminance;
        data.maxFrameAverageLightLevel = m->max_frame_nits ? m->max_frame_nits / 1e4f : data.maxLuminance;
    }
    if (p->metadata_set && !memcmp(&p->metadata, &data, sizeof(data)))
        return;
    w->SetHdrMetadata(c->device, 1, &p->swapchain, &data);
    p->metadata = data;
    p->metadata_set = true;
}

/* (swapchain id, twelve floats: red, green, blue and white xy, then max, min, content and frame average nits) */
void mvk_wsi_hdr_metadata(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    Swapchain *s = (Swapchain *)(uintptr_t)mvk_get_handle(d, 0);
    float v[12];
    mvk_get_bytes(d, v, sizeof(v));
    MvkWsi *w = c->wsi;
    if (d->error || !s || !w)
        return;
    pthread_mutex_lock(&w->convert_lock);
    memset(&s->metadata, 0, sizeof(s->metadata));
    s->metadata.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
    s->metadata.displayPrimaryRed = (VkXYColorEXT){v[0], v[1]};
    s->metadata.displayPrimaryGreen = (VkXYColorEXT){v[2], v[3]};
    s->metadata.displayPrimaryBlue = (VkXYColorEXT){v[4], v[5]};
    s->metadata.whitePoint = (VkXYColorEXT){v[6], v[7]};
    s->metadata.maxLuminance = v[8];
    s->metadata.minLuminance = v[9];
    s->metadata.maxContentLightLevel = v[10];
    s->metadata.maxFrameAverageLightLevel = v[11];
    s->has_metadata = v[8] > 0;
    pthread_mutex_unlock(&w->convert_lock);
}

/* ------------------------------------------------------------------ recording. */

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
    double s = f->scale;
    return (Box){f->origin_x + (r.left - f->host.left) * s, f->origin_y + (r.top - f->host.top) * s,
                 f->origin_x + (r.right - f->host.left) * s, f->origin_y + (r.bottom - f->host.top) * s};
}

static void record(MvkConnection *c, Presenter *p, Frame *f, uint32_t target, Swapchain *s, uint32_t index,
                   const MimeUiFrame *frame, MimeRect client, bool upload, bool gamma)
{
    VkCommandBuffer cmd = f->cmd;
    /* through the ramp, the frame is composed first, after the last frame's pass read it. */
    VkImage dst = gamma ? p->gamma.compose : p->images[target];
    c->fn.vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    c->fn.vkBeginCommandBuffer(cmd, &begin);
    barrier(c, cmd, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            gamma ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT
                  : VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
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
    VkImage src = s->images[index], from = src, kept = src;
    bool flip = s->flip;
    if (p->tone.on) {
        /* one of high dynamic range is drawn as what the window shows, and as standard range for the copy. */
        barrier(c, cmd, src, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
                VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        convert_draw(c, cmd, s, index, &p->convert, p->tone);
        from = kept = p->convert.image;
        if (p->space && p->last) {
            convert_draw(c, cmd, s, index, &p->standard, p->tone);
            kept = p->standard.image;
        }
        flip = false;
    } else {
        barrier(c, cmd, src, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0,
                VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
    Box crop = {0, 0, s->extent.width, s->extent.height}, area;
    if (frame->subsurface) {
        /* a layer is an area of the window, and the image lies over the part of it the client area takes. */
        Box whole = {0, 0, p->extent.width, p->extent.height};
        area = whole;
        double lw = frame->area.right - frame->area.left, lh = frame->area.bottom - frame->area.top;
        if (lw > 0 && lh > 0) {
            double kx = p->extent.width / lw, ky = p->extent.height / lh;
            area = (Box){(frame->client.left - frame->area.left) * kx, (frame->client.top - frame->area.top) * ky,
                         (frame->client.right - frame->area.left) * kx, (frame->client.bottom - frame->area.top) * ky};
            /*
             * past a client area that shrank the layer still shows until the window is painted there. windows
             * leaves the last image in it, which the edge of this one stands for: its rows, columns and corners at
             * the ends, drawn out to the ends of the layer.
             */
            if (area.left > 0.5 || area.top > 0.5 || area.right < whole.right - 0.5 || area.bottom < whole.bottom - 0.5) {
                double sw = s->extent.width, sh = s->extent.height;
                double sx[4] = {0, 1, sw - 1, sw}, sy[4] = {0, 1, sh - 1, sh};
                double dx[4] = {0, area.left, area.right, whole.right}, dy[4] = {0, area.top, area.bottom, whole.bottom};
                for (int j = 0; j < 3; j++)
                    for (int i = 0; i < 3; i++)
                        if (i != 1 || j != 1)
                            blit(c, cmd, from, (Box){i == 1 ? 0 : sx[i], j == 1 ? 0 : sy[j], i == 1 ? sw : sx[i + 1],
                                                     j == 1 ? sh : sy[j + 1]},
                                 s->extent.width, s->extent.height, dst, (Box){dx[i], dy[j], dx[i + 1], dy[j + 1]},
                                 p->extent.width, p->extent.height, flip);
            }
        }
    } else {
        area = to_pixels(frame, frame->client);
    }
    blit(c, cmd, from, crop, s->extent.width, s->extent.height, dst, area, p->extent.width, p->extent.height, flip);
    /* and kept, for captures of the screen. */
    if (p->last) {
        barrier(c, cmd, p->last, p->last_valid ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        blit(c, cmd, kept, (Box){0, 0, s->extent.width, s->extent.height}, s->extent.width, s->extent.height, p->last,
             (Box){0, 0, p->last_extent.width, p->last_extent.height}, p->last_extent.width, p->last_extent.height,
             flip);
        barrier(c, cmd, p->last, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
    p->last_valid = p->last != VK_NULL_HANDLE;
    p->shown_from = s;
    p->shown_index = index;
    p->shown_extent = s->extent;
    p->last_client = client;
    p->last_serial++;
    pthread_mutex_lock(&p->info_lock);
    p->info_valid = true;
    p->info_width = s->extent.width;
    p->info_height = s->extent.height;
    p->info_client = client;
    p->info_serial = p->last_serial;
    pthread_mutex_unlock(&p->info_lock);
    release_source(c, cmd, src, p->tone.on);
    if (gamma)
        gamma_record(c, p, cmd, target);
    else
        barrier(c, cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    c->fn.vkEndCommandBuffer(cmd);
}

/* ------------------------------------------------------------------ present. */

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

/* under the presenter lock: returns false if the window will show nothing. */
static bool show(MvkConnection *c, Presenter *p, VkQueue queue, uint32_t family, const Waits *waits, Swapchain *s,
                 uint32_t index, const MimeUiFrame *frame, MimeRect client)
{
    MvkWsi *w = c->wsi;
    if (!p->surface)
        return false;
    if (!frame->shown || !frame->pixel_width || !frame->pixel_height) {
        if (has_window_images(p)) {
            pthread_mutex_lock(&c->queue_lock);
            c->fn.vkQueueWaitIdle(queue);
            pthread_mutex_unlock(&c->queue_lock);
            drop_window_images(c, p);
        }
        return false;
    }
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
    /* one of high dynamic range shows as that where the monitor does, over the client area alone. */
    MimeMonitorColor color = {};
    bool told = s->space && display_hdr(&color);
    bool deep = told && frame->subsurface;
    /* swapchains that share the window take turns at its settings only once the one that has them stops. */
    uint64_t now = mime_now_ns();
    bool owner = !p->owner || p->owner == s->serial || now - p->owner_seen > OWNER_IDLE_NS;
    if (owner) {
        p->owner = s->serial;
        p->owner_seen = now;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        bool settings = !p->swapchain || (owner && (p->srgb != srgb || p->wanted_mode != s->mode ||
                                                    p->premultiplied_wanted != s->premultiplied ||
                                                    p->deep_wanted != deep));
        if (settings || p->stale || p->extent.width != want.width || p->extent.height != want.height) {
            wait_frames(c, p);
            bool mine = owner || !p->swapchain;
            bool encoded = mine ? srgb : p->srgb, premultiplied = mine ? s->premultiplied : p->premultiplied_wanted;
            VkPresentModeKHR mode = mine ? s->mode : p->wanted_mode;
            p->premultiplied_wanted = premultiplied;
            if (!make_swapchain(c, p, queue, want, encoded, mode, premultiplied, mine ? deep : p->deep_wanted))
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
        VkFormat gdi_format = p->srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
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
        /* the copy for captures lives only while captures ask for it. */
        if (now < p->want_last_until)
            make_last(c, p, s->extent, srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM);
        else if (p->last || p->readback) {
            wait_frames(c, p);
            drop_last(c, p);
        }
        p->last_queue = queue;
        p->tone = (Tone){};
        if (s->space && convert_target(c, p, &p->convert, s->extent, p->space)) {
            p->tone = tone_of(s, told);
        } else if (p->convert.image) {
            wait_frames(c, p);
            convert_drop(c, &p->convert);
        }
        if (p->tone.on && p->space && p->last) {
            if (!convert_target(c, p, &p->standard, s->extent, SPACE_SDR)) {
                wait_frames(c, p);
                drop_last(c, p);
            }
        } else if (p->standard.image) {
            wait_frames(c, p);
            convert_drop(c, &p->standard);
        }
        if (p->tone.on)
            show_metadata(c, p, s, &color);
        bool gamma = gamma_prepare(c, p);
        record(c, p, f, target, s, index, frame, client, upload, gamma);

        Waits all = *waits;
        all.semaphores[all.count] = f->acquired;
        all.values[all.count] = 0;
        all.stages[all.count++] = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
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

/* the part of the client area that shows, empty when none does. */
static MimeRect shown_area(const MvkTarget *t)
{
    MimeRect r = {MIME_MAX(t->visible.left, t->client.left), MIME_MAX(t->visible.top, t->client.top),
                  MIME_MIN(t->visible.right, t->client.right), MIME_MIN(t->visible.bottom, t->client.bottom)};
    if (r.right <= r.left || r.bottom <= r.top)
        return (MimeRect){};
    return r;
}

/* presents one swapchain, or just releases its image when the window is hidden. */
static void present_one(MvkConnection *c, VkQueue queue, uint32_t family, const Waits *waits, Swapchain *s,
                        uint32_t index, const MvkTarget *t)
{
    MvkWsi *w = c->wsi;
    /* a window of the guest keeps a presenter for each window that presents where the display has layers, else one. */
    uint32_t source = mime_ui_vk_layers() ? t->source : 0;
    if (s->presenter && (s->presenter->window != t->window || s->presenter->source != source)) {
        presenter_put(c, s->presenter);
        s->presenter = nullptr;
    }
    if (!s->presenter && t->window && can_present(w))
        s->presenter = presenter_get(c, t->window, source);
    Presenter *p = s->presenter;
    bool shown = false;
    /*
     * a frame comes with the place its window had when the guest made it, and the window may have moved since: the
     * guest told that in a layer notice, and the old place would put the layer back over what was painted there.
     */
    MvkTarget now = *t;
    int32_t width = t->client.right - t->client.left, height = t->client.bottom - t->client.top;
    if (p) {
        pthread_mutex_lock(&g_presenters_lock);
        if (now.stamp && p->placed.stamp && (int32_t)(p->placed.stamp - now.stamp) > 0) {
            now.client = p->placed.client;
            now.visible = p->placed.visible;
            now.order = p->placed.order;
            now.stamp = p->placed.stamp;
        }
        pthread_mutex_unlock(&g_presenters_lock);
        t = &now;
    }
    MimeRect area = shown_area(t);
    if (p) {
        if (area.right > area.left || p->surface)
            presenter_attach(c, p, area, t->order);
        MimeUiFrame frame = {};
        /* the image keeps the size its frame was made for, where the client area is now. */
        MimeUiPlace place = {t->client, t->visible, t->order, t->stamp, false};
        MimeRect image = {t->client.left, t->client.top, t->client.left + width, t->client.top + height};
        if (mime_ui_vk_frame(t->window, source, &place, image, &frame)) {
            if (!frame.subsurface)
                refresh_shadow(p, &frame);
            pthread_mutex_lock(&p->lock);
            p->last_source = t->source;
            shown = show(c, p, queue, family, waits, s, index, &frame, t->client);
            pthread_mutex_unlock(&p->lock);
            if (frame.subsurface)
                mime_ui_vk_shown(t->window, source, frame.serial, shown);
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
        present_one(c, job->queue, job->family, &waits, job->s, job->index, &job->target);
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
     * consume guest semaphores once in ring order, before the guest may signal them again. process multiple
     * swapchains sequentially, or signal the next ready timeline value for the presenter thread.
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
        MvkTarget target = {.window = mvk_get_u32(d)};
        target.client.left = (int32_t)mvk_get_u32(d);
        target.client.top = (int32_t)mvk_get_u32(d);
        target.client.right = (int32_t)mvk_get_u32(d);
        target.client.bottom = (int32_t)mvk_get_u32(d);
        target.source = mvk_get_u32(d);
        target.order = mvk_get_u32(d);
        target.visible.left = (int32_t)mvk_get_u32(d);
        target.visible.top = (int32_t)mvk_get_u32(d);
        target.visible.right = (int32_t)mvk_get_u32(d);
        target.visible.bottom = (int32_t)mvk_get_u32(d);
        target.stamp = mvk_get_u32(d);
        if (d->error || !s || index >= s->count)
            break;
        /* a released image is acquired before its next present: anything else is the guest's mistake. */
        if (s->pending[index]) {
            mime_warn("vulkan: image %u of a swapchain presented twice", index);
            continue;
        }
        if (!aside) {
            present_one(c, queue, family, &waits, s, index, &target);
            waits.count = 0;
            continue;
        }
        PresentJob *job = calloc(1, sizeof(*job));
        if (!job)
            break;
        *job = (PresentJob){nullptr, s, index, target, queue, family, ready};
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

/* no presenter keeps the images of a swapchain that goes. */
static void forget_shown(Swapchain *s)
{
    pthread_mutex_lock(&g_presenters_lock);
    for (int i = 0; i < MAX_PRESENTERS; i++) {
        Presenter *p = g_presenters[i];
        if (!p || p->shown_from != s)
            continue;
        pthread_mutex_lock(&p->lock);
        if (p->shown_from == s)
            p->shown_from = nullptr;
        pthread_mutex_unlock(&p->lock);
    }
    pthread_mutex_unlock(&g_presenters_lock);
}

static void free_swapchain(MvkConnection *c, Swapchain *s)
{
    mvk_wsi_drain(c);
    pthread_mutex_lock(&c->queue_lock);
    c->fn.vkDeviceWaitIdle(c->device);
    pthread_mutex_unlock(&c->queue_lock);
    forget_shown(s);
    if (s->presenter) {
        pthread_mutex_lock(&s->presenter->lock);
        if (s->presenter->owner == s->serial)
            s->presenter->owner = 0;
        pthread_mutex_unlock(&s->presenter->lock);
        presenter_park(c, s->presenter);
    }
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
    stop_parking(c);
    convert_destroy(c, w);
    pthread_mutex_destroy(&w->park_lock);
    pthread_cond_destroy(&w->park_cond);
    pthread_mutex_destroy(&w->convert_lock);
    pthread_mutex_destroy(&w->lock);
    pthread_cond_destroy(&w->cond);
    free(w);
    c->wsi = nullptr;
}

/* ------------------------------------------------------------------ images of the host's own. */

MvkImageChain *mvk_wsi_chain_create(MvkConnection *c, VkImage image, VkFormat format, VkExtent2D extent)
{
    if (!wsi_of(c))
        return nullptr;
    Swapchain *s = calloc(1, sizeof(*s));
    if (!s)
        return nullptr;
    s->serial = __atomic_add_fetch(&g_swapchain_serial, 1, __ATOMIC_RELAXED);
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

/* presents a host-owned image after waiting for rendered. */
void mvk_wsi_chain_present(MvkConnection *c, MvkImageChain *s, VkQueue queue, uint32_t family, VkSemaphore rendered,
                           const MvkTarget *target, bool sync)
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
        present_one(c, queue, family, &waits, s, 0, target);
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
    *job = (PresentJob){nullptr, s, 0, *target, queue, family, ready};
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

/* returns the semaphore for the next render after its present is submitted, or null. */
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

/* ------------------------------------------------------------------ readback. */

/* rows of 32 bit pixels with the alpha forced to opaque, which is what the guest composes them as. */
static void copy_opaque(void *pixels, uint32_t stride, const uint8_t *from, uint32_t width, uint32_t height)
{
    for (uint32_t y = 0; y < height; y++) {
        uint32_t *to = (uint32_t *)((uint8_t *)pixels + (size_t)y * stride);
        const uint32_t *in = (const uint32_t *)(from + (size_t)y * width * 4);
        for (uint32_t x = 0; x < width; x++)
            to[x] = in[x] | 0xFF000000u;
    }
}

/* runs commands on the queue of the last present and waits for them, under the presenter's lock. */
static bool run_once(MvkConnection *c, Presenter *p, void (*fill)(MvkConnection *c, Presenter *p, VkCommandBuffer cmd))
{
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, p->pool,
                                      VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    if (c->fn.vkAllocateCommandBuffers(c->device, &ai, &cmd) != VK_SUCCESS)
        return false;
    VkFence fence;
    VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (c->fn.vkCreateFence(c->device, &fi, nullptr, &fence) != VK_SUCCESS) {
        c->fn.vkFreeCommandBuffers(c->device, p->pool, 1, &cmd);
        return false;
    }
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    c->fn.vkBeginCommandBuffer(cmd, &begin);
    fill(c, p, cmd);
    c->fn.vkEndCommandBuffer(cmd);
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &cmd, 0, nullptr};
    bool ok = submit_locked(c, p->last_queue, &submit, fence) == VK_SUCCESS &&
              c->fn.vkWaitForFences(c->device, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    c->fn.vkDestroyFence(c->device, fence, nullptr);
    c->fn.vkFreeCommandBuffers(c->device, p->pool, 1, &cmd);
    return ok;
}

static void copy_shown(MvkConnection *c, Presenter *p, VkCommandBuffer cmd)
{
    Swapchain *s = p->shown_from;
    VkImage src = s->images[p->shown_index], from = src;
    VkExtent2D e = p->shown_extent;
    bool flip = s->flip;
    if (p->tone.on) {
        ConvertTarget *t = p->space ? &p->standard : &p->convert;
        barrier(c, cmd, src, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
                VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        convert_draw(c, cmd, s, p->shown_index, t, p->tone);
        from = t->image;
        flip = false;
    } else {
        barrier(c, cmd, src, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0,
                VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
    barrier(c, cmd, p->last, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    blit(c, cmd, from, (Box){0, 0, e.width, e.height}, e.width, e.height, p->last, (Box){0, 0, e.width, e.height},
         e.width, e.height, flip);
    barrier(c, cmd, p->last, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    release_source(c, cmd, src, p->tone.on);
}

/* the first capture of a window that keeps no copy takes the guest image it last presented, still as it was shown. */
static bool keep_shown(MvkConnection *c, Presenter *p)
{
    Swapchain *s = p->shown_from;
    if (!s || !s->images[p->shown_index] || !p->pool || !p->last_queue)
        return false;
    VkFormat format = is_srgb(s->format) ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
    if (!make_last(c, p, p->shown_extent, format))
        return false;
    p->tone = (Tone){};
    MimeMonitorColor ignored;
    if (s->space && convert_target(c, p, p->space ? &p->standard : &p->convert, p->shown_extent, SPACE_SDR))
        p->tone = tone_of(s, display_hdr(&ignored));
    if (!run_once(c, p, copy_shown))
        return false;
    p->last_valid = true;
    return true;
}

static void copy_last(MvkConnection *c, Presenter *p, VkCommandBuffer cmd)
{
    /* the present that wrote the image went first on this queue. */
    barrier(c, cmd, p->last, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {p->last_extent.width, p->last_extent.height, 1},
    };
    c->fn.vkCmdCopyImageToBuffer(cmd, p->last, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, p->readback, 1, &copy);
}

/*
 * copies the last presented image into BGRA rows if capacity permits. returns its size and window-relative
 * client rectangle, or false if the window has no image.
 */
bool mvk_wsi_readback(uint32_t window, uint32_t source, void *pixels, uint32_t capacity_width, uint32_t capacity_height,
                      uint32_t stride, uint32_t *width, uint32_t *height, MimeRect *client, uint64_t *serial)
{
    /* where a window has one presenter, the image is the last one presented and the window it was made for is the one asked. */
    bool layers = mime_ui_vk_layers();
    /* the pixels are the last frame the program presented, which may still be on its way. */
    if (pixels)
        mime_vk_sync();
    pthread_mutex_lock(&g_presenters_lock);
    Presenter *p = nullptr;
    for (int i = 0; i < MAX_PRESENTERS; i++) {
        Presenter *q = g_presenters[i];
        if (q && q->window == window && (layers ? q->source == source : (!source || q->last_source == source))) {
            p = q;
            p->refs++;
            break;
        }
    }
    pthread_mutex_unlock(&g_presenters_lock);
    if (!p)
        return false;
    MvkConnection *c = p->conn;
    bool ok = false;
    if (!pixels) {
        /* reads size and serial without waiting for presentation. */
        pthread_mutex_lock(&p->info_lock);
        ok = p->info_valid;
        if (ok) {
            *width = p->info_width;
            *height = p->info_height;
            *client = p->info_client;
            *serial = p->info_serial;
        }
        pthread_mutex_unlock(&p->info_lock);
        presenter_put(c, p);
        return ok;
    }
    pthread_mutex_lock(&p->lock);
    p->want_last_until = mime_now_ns() + LAST_KEEP_NS;
    if ((!p->last_valid && !keep_shown(c, p)) || !p->pool || !p->last_queue)
        goto out;
    *width = p->last_extent.width;
    *height = p->last_extent.height;
    *client = p->last_client;
    *serial = p->last_serial;
    if (!pixels || p->last_extent.width > capacity_width || p->last_extent.height > capacity_height) {
        ok = true;
        goto out;
    }
    VkDeviceSize size = (VkDeviceSize)p->last_extent.width * p->last_extent.height * 4;
    /* reuse cached readback for an unchanged image, avoiding GPU work. */
    if (p->readback_serial == p->last_serial && p->readback_size >= size) {
        copy_opaque(pixels, stride, p->readback_map, p->last_extent.width, p->last_extent.height);
        ok = true;
        goto out;
    }
    if (p->readback_size < size) {
        p->readback_serial = 0;
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
        /* cached memory, which reads at the speed of a copy: write combined memory reads at a few hundred megabytes a second. */
        const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        int32_t type = memory_type(c, req.memoryTypeBits, host | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        p->readback_coherent = type >= 0;
        if (type < 0)
            type = memory_type(c, req.memoryTypeBits, host);
        if (type < 0) {
            type = memory_type(c, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            p->readback_coherent = true;
        }
        VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, (uint32_t)type};
        if (type < 0 || c->fn.vkAllocateMemory(c->device, &alloc, nullptr, &p->readback_memory) != VK_SUCCESS ||
            c->fn.vkBindBufferMemory(c->device, p->readback, p->readback_memory, 0) != VK_SUCCESS ||
            c->fn.vkMapMemory(c->device, p->readback_memory, 0, VK_WHOLE_SIZE, 0, (void **)&p->readback_map) !=
                VK_SUCCESS)
            goto out;
        p->readback_size = size;
    }
    if (run_once(c, p, copy_last)) {
        if (!p->readback_coherent) {
            VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, p->readback_memory, 0,
                                         VK_WHOLE_SIZE};
            c->fn.vkInvalidateMappedMemoryRanges(c->device, 1, &range);
        }
        copy_opaque(pixels, stride, p->readback_map, p->last_extent.width, p->last_extent.height);
        p->readback_serial = p->last_serial;
        ok = true;
    }
out:
    pthread_mutex_unlock(&p->lock);
    presenter_put(c, p);
    return ok;
}

/* a window that presents moved, was hidden, or was covered: its layer follows without waiting for the next present. */
void mime_vk_layer(const MimeWinLayer *layer)
{
    if (!mime_ui_vk_layers())
        return;
    pthread_mutex_lock(&g_presenters_lock);
    Presenter *p = nullptr;
    for (int i = 0; i < MAX_PRESENTERS && !p; i++)
        if (g_presenters[i] && g_presenters[i]->window == layer->window_id && g_presenters[i]->source == layer->source)
            p = g_presenters[i];
    MvkTarget t = {layer->window_id, layer->source, layer->order, layer->client, layer->visible, layer->stamp};
    if (p) {
        p->refs++;
        p->placed = t;
    }
    pthread_mutex_unlock(&g_presenters_lock);
    if (!p)
        return;
    MimeUiPlace place = {t.client, t.visible, t.order, t.stamp, layer->follows != 0};
    mime_ui_vk_place(t.window, t.source, &place);
    presenter_put(p->conn, p);
}
