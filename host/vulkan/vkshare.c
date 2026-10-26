/*
 * shared Direct3D resources and fences. each guest process has its own host Vulkan device; memory and
 * semaphores cross devices through opaque file descriptors, or on macOS through the Metal textures and shared
 * events behind them, which the guest's file descriptor requests are turned into. exports are keyed by the kernel
 * object id behind the NT handle. handles remain until MIME_HC_GFX_SHARE_FREE, so resources outlive their
 * creating process while handles remain. sharing within one device uses its objects directly.
 */
#include "mvk_host.h"
#include "../gpu/gpu.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <vulkan/vulkan_metal.h>
#endif

#include <pthread.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

typedef struct Share {
    struct Share *next;
    uint64_t id;
    int memory_fd;
    int semaphore_fd;
    /* retained Metal objects on macOS: the texture of the memory, the shared event of the semaphore. */
    void *memory_object;
    void *semaphore_object;
} Share;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Share *g_shares;

static Share *find(uint64_t id, bool create)
{
    for (Share *s = g_shares; s; s = s->next)
        if (s->id == id)
            return s;
    if (!create)
        return nullptr;
    Share *s = calloc(1, sizeof(*s));
    if (!s)
        return nullptr;
    s->id = id;
    s->memory_fd = s->semaphore_fd = -1;
    s->next = g_shares;
    g_shares = s;
    return s;
}

void mime_vk_share_free(uint64_t id)
{
    pthread_mutex_lock(&g_lock);
    for (Share **p = &g_shares; *p; p = &(*p)->next) {
        if ((*p)->id == id) {
            Share *s = *p;
            *p = s->next;
            if (s->memory_fd >= 0)
                close(s->memory_fd);
            if (s->semaphore_fd >= 0)
                close(s->semaphore_fd);
#ifdef __APPLE__
            if (s->memory_object)
                CFRelease(s->memory_object);
            if (s->semaphore_object)
                CFRelease(s->semaphore_object);
#endif
            free(s);
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

#ifdef __APPLE__
/* ------------------------------------------------------------------ Metal objects on macOS. */

/* keeps a retained Metal object under the id; the first one stays. */
static VkResult keep_object(uint64_t id, void *object, bool memory)
{
    pthread_mutex_lock(&g_lock);
    Share *s = find(id, true);
    VkResult result = VK_SUCCESS;
    void **slot = s ? (memory ? &s->memory_object : &s->semaphore_object) : nullptr;
    if (!slot)
        result = VK_ERROR_OUT_OF_HOST_MEMORY;
    else if (!*slot)
        *slot = (void *)CFRetain(object);
    else if (*slot != object)
        mime_warn("vulkan: share %llx already holds another %s", (unsigned long long)id, memory ? "texture" : "event");
    pthread_mutex_unlock(&g_lock);
    return result;
}

/* the stored object, retained for the caller. */
static void *take_object(uint64_t id, bool memory)
{
    void *object = nullptr;
    pthread_mutex_lock(&g_lock);
    Share *s = find(id, false);
    if (s && (memory ? s->memory_object : s->semaphore_object))
        object = (void *)CFRetain(memory ? s->memory_object : s->semaphore_object);
    pthread_mutex_unlock(&g_lock);
    return object;
}

static PFN_vkCreateImage g_create_image;
static PFN_vkAllocateMemory g_allocate_memory;
static PFN_vkCreateSemaphore g_create_semaphore;

/* the guest asks for file descriptors, which the Metal driver has not: its images share as Metal textures. */
static VkResult VKAPI_CALL create_image(VkDevice device, const VkImageCreateInfo *info, const VkAllocationCallbacks *a,
                                        VkImage *image)
{
    for (VkBaseOutStructure *s = (VkBaseOutStructure *)info->pNext; s; s = s->pNext)
        if (s->sType == VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO) {
            VkExternalMemoryImageCreateInfo *x = (VkExternalMemoryImageCreateInfo *)s;
            if (x->handleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT)
                x->handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLTEXTURE_BIT_EXT;
        }
    return g_create_image(device, info, a, image);
}

static VkResult VKAPI_CALL allocate_memory(VkDevice device, const VkMemoryAllocateInfo *info,
                                           const VkAllocationCallbacks *a, VkDeviceMemory *memory)
{
    for (VkBaseOutStructure *s = (VkBaseOutStructure *)info->pNext; s; s = s->pNext)
        if (s->sType == VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO) {
            VkExportMemoryAllocateInfo *x = (VkExportMemoryAllocateInfo *)s;
            if (x->handleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT)
                x->handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLTEXTURE_BIT_EXT;
        }
    return g_allocate_memory(device, info, a, memory);
}

/* the driver's semaphores stand on shared events already, which export without asking. */
static VkResult VKAPI_CALL create_semaphore(VkDevice device, const VkSemaphoreCreateInfo *info,
                                            const VkAllocationCallbacks *a, VkSemaphore *semaphore)
{
    for (VkBaseOutStructure *prev = (VkBaseOutStructure *)info, *s = prev->pNext; s; prev = s, s = s->pNext)
        if (s->sType == VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO) {
            prev->pNext = s->pNext;
            s = prev;
        }
    return g_create_semaphore(device, info, a, semaphore);
}

void mvk_share_device_created(MvkConnection *c)
{
    g_create_image = c->fn.vkCreateImage;
    g_allocate_memory = c->fn.vkAllocateMemory;
    g_create_semaphore = c->fn.vkCreateSemaphore;
    c->fn.vkCreateImage = create_image;
    c->fn.vkAllocateMemory = allocate_memory;
    c->fn.vkCreateSemaphore = create_semaphore;
}

static void *device_proc(MvkConnection *c, const char *name)
{
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)c->gipa(c->instance, "vkGetDeviceProcAddr");
    return c->device ? (void *)gdpa(c->device, name) : nullptr;
}

void mvk_share_export_memory(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t memory_id = mvk_get_u64(&call->in), id = mvk_get_u64(&call->in);
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_FEATURE_NOT_PRESENT;
    VkDeviceMemory memory = (VkDeviceMemory)mvk_lookup(c, memory_id);
    PFN_vkGetMemoryMetalHandleEXT get = (PFN_vkGetMemoryMetalHandleEXT)device_proc(c, "vkGetMemoryMetalHandleEXT");
    if (!call->in.error && memory && get) {
        VkMemoryGetMetalHandleInfoEXT info = {VK_STRUCTURE_TYPE_MEMORY_GET_METAL_HANDLE_INFO_EXT, nullptr, memory,
                                              VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLTEXTURE_BIT_EXT};
        void *texture = nullptr;
        result = get(c->device, &info, &texture);
        if (result == VK_SUCCESS)
            result = texture ? keep_object(id, texture, true) : VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

void mvk_share_import_memory(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t memory_id = mvk_get_u64(&call->in), id = mvk_get_u64(&call->in), image_id = mvk_get_u64(&call->in);
    uint64_t size = mvk_get_u64(&call->in);
    uint32_t type = mvk_get_u32(&call->in);
    mvk_get_u32(&call->in);
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_FEATURE_NOT_PRESENT;
    if (!call->in.error && c->device) {
        void *texture = take_object(id, true);
        if (!texture) {
            result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
        } else {
            VkMemoryDedicatedAllocateInfo dedicated = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, nullptr,
                                                       (VkImage)mvk_lookup(c, image_id), VK_NULL_HANDLE};
            VkImportMemoryMetalHandleInfoEXT import = {VK_STRUCTURE_TYPE_IMPORT_MEMORY_METAL_HANDLE_INFO_EXT,
                                                       image_id ? &dedicated : nullptr,
                                                       VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLTEXTURE_BIT_EXT, texture};
            VkMemoryAllocateInfo info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import, size, type};
            VkDeviceMemory memory = VK_NULL_HANDLE;
            result = c->fn.vkAllocateMemory(c->device, &info, nullptr, &memory);
            if (result == VK_SUCCESS) {
                mvk_bind(c, memory_id, (uint64_t)memory);
                mvk_track(c, (uint64_t)memory, MVK_KIND_VkDeviceMemory);
            }
            /* the driver retains what it imports. */
            CFRelease(texture);
        }
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

void mvk_share_export_semaphore(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t semaphore_id = mvk_get_u64(&call->in), id = mvk_get_u64(&call->in);
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_FEATURE_NOT_PRESENT;
    VkSemaphore semaphore = (VkSemaphore)mvk_lookup(c, semaphore_id);
    PFN_vkExportMetalObjectsEXT export_objects = (PFN_vkExportMetalObjectsEXT)device_proc(c, "vkExportMetalObjectsEXT");
    if (!call->in.error && semaphore && export_objects) {
        VkExportMetalSharedEventInfoEXT event = {VK_STRUCTURE_TYPE_EXPORT_METAL_SHARED_EVENT_INFO_EXT, nullptr,
                                                 semaphore};
        VkExportMetalObjectsInfoEXT info = {VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT, &event};
        export_objects(c->device, &info);
        result = event.mtlSharedEvent ? keep_object(id, event.mtlSharedEvent, false) : VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

/*
 * the driver takes a shared event only when a semaphore is made, so the guest's semaphore is made again on the
 * stored event and its id names the new one.
 */
void mvk_share_import_semaphore(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t semaphore_id = mvk_get_u64(&call->in), id = mvk_get_u64(&call->in);
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_FEATURE_NOT_PRESENT;
    VkSemaphore old = (VkSemaphore)mvk_lookup(c, semaphore_id);
    if (!call->in.error && old && c->device) {
        void *event = take_object(id, false);
        if (!event) {
            result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
        } else {
            VkImportMetalSharedEventInfoEXT import = {VK_STRUCTURE_TYPE_IMPORT_METAL_SHARED_EVENT_INFO_EXT, nullptr,
                                                      event};
            VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, &import,
                                              VK_SEMAPHORE_TYPE_TIMELINE, 0};
            VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
            VkSemaphore semaphore = VK_NULL_HANDLE;
            result = c->fn.vkCreateSemaphore(c->device, &info, nullptr, &semaphore);
            if (result == VK_SUCCESS) {
                mvk_forget(c, (uint64_t)old);
                c->fn.vkDestroySemaphore(c->device, old, nullptr);
                mvk_bind(c, semaphore_id, (uint64_t)semaphore);
                mvk_track(c, (uint64_t)semaphore, MVK_KIND_VkSemaphore);
                mvk_mirror_refresh(c, semaphore);
            }
            CFRelease(event);
        }
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

#else

void mvk_share_device_created(MvkConnection *c) {}

static bool g_limit_raised;

/* each shared resource needs a descriptor; a browser can have more textures than the process's soft limit. */
static void raise_limit(void)
{
    if (g_limit_raised)
        return;
    g_limit_raised = true;
    struct rlimit limit;
    if (!getrlimit(RLIMIT_NOFILE, &limit) && limit.rlim_cur < limit.rlim_max) {
        limit.rlim_cur = limit.rlim_max < (1 << 17) ? limit.rlim_max : (1 << 17);
        setrlimit(RLIMIT_NOFILE, &limit);
    }
}

/* keeps one descriptor of each kind under the id. */
static VkResult keep(uint64_t id, int fd, bool memory)
{
    raise_limit();
    pthread_mutex_lock(&g_lock);
    Share *s = find(id, true);
    VkResult result = VK_SUCCESS;
    int *slot = s ? (memory ? &s->memory_fd : &s->semaphore_fd) : nullptr;
    if (!slot)
        result = VK_ERROR_OUT_OF_HOST_MEMORY;
    else if (*slot >= 0)
        close(fd);
    else
        *slot = fd;
    pthread_mutex_unlock(&g_lock);
    return result;
}

/* duplicates the stored descriptor for an import. */
static int take(uint64_t id, bool memory)
{
    int fd = -1;
    pthread_mutex_lock(&g_lock);
    Share *s = find(id, false);
    if (s)
        fd = dup(memory ? s->memory_fd : s->semaphore_fd);
    pthread_mutex_unlock(&g_lock);
    return fd;
}


typedef struct ShareFn {
    PFN_vkGetMemoryFdKHR get_memory_fd;
    PFN_vkGetSemaphoreFdKHR get_semaphore_fd;
    PFN_vkImportSemaphoreFdKHR import_semaphore_fd;
    VkDevice device;
} ShareFn;

static bool functions(MvkConnection *c, ShareFn *fn)
{
    if (!c->device)
        return false;
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)c->gipa(c->instance, "vkGetDeviceProcAddr");
    fn->device = c->device;
    fn->get_memory_fd = (PFN_vkGetMemoryFdKHR)gdpa(c->device, "vkGetMemoryFdKHR");
    fn->get_semaphore_fd = (PFN_vkGetSemaphoreFdKHR)gdpa(c->device, "vkGetSemaphoreFdKHR");
    fn->import_semaphore_fd = (PFN_vkImportSemaphoreFdKHR)gdpa(c->device, "vkImportSemaphoreFdKHR");
    return fn->get_memory_fd && fn->get_semaphore_fd && fn->import_semaphore_fd;
}

void mvk_share_export_memory(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t memory_id = mvk_get_u64(&call->in), id = mvk_get_u64(&call->in);
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_FEATURE_NOT_PRESENT;
    ShareFn fn;
    VkDeviceMemory memory = (VkDeviceMemory)mvk_lookup(c, memory_id);
    if (!call->in.error && memory && functions(c, &fn)) {
        VkMemoryGetFdInfoKHR info = {VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, nullptr, memory,
                                     VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
        int fd = -1;
        result = fn.get_memory_fd(fn.device, &info, &fd);
        if (result == VK_SUCCESS)
            result = keep(id, fd, true);
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

void mvk_share_import_memory(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t memory_id = mvk_get_u64(&call->in), id = mvk_get_u64(&call->in), image_id = mvk_get_u64(&call->in);
    uint64_t size = mvk_get_u64(&call->in);
    uint32_t type = mvk_get_u32(&call->in);
    mvk_get_u32(&call->in);
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_FEATURE_NOT_PRESENT;
    ShareFn fn;
    if (!call->in.error && functions(c, &fn)) {
        int fd = take(id, true);
        if (fd < 0) {
            result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
        } else {
            VkMemoryDedicatedAllocateInfo dedicated = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, nullptr,
                                                       (VkImage)mvk_lookup(c, image_id), VK_NULL_HANDLE};
            VkImportMemoryFdInfoKHR import = {VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
                                              image_id ? &dedicated : nullptr,
                                              VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT, fd};
            VkMemoryAllocateInfo info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import, size, type};
            VkDeviceMemory memory = VK_NULL_HANDLE;
            result = c->fn.vkAllocateMemory(c->device, &info, nullptr, &memory);
            if (result == VK_SUCCESS) {
                mvk_bind(c, memory_id, (uint64_t)memory);
                mvk_track(c, (uint64_t)memory, MVK_KIND_VkDeviceMemory);
            } else {
                /* the descriptor belongs to the driver only after a successful import. */
                close(fd);
            }
        }
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

void mvk_share_export_semaphore(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t semaphore_id = mvk_get_u64(&call->in), id = mvk_get_u64(&call->in);
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_FEATURE_NOT_PRESENT;
    ShareFn fn;
    VkSemaphore semaphore = (VkSemaphore)mvk_lookup(c, semaphore_id);
    if (!call->in.error && semaphore && functions(c, &fn)) {
        VkSemaphoreGetFdInfoKHR info = {VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR, nullptr, semaphore,
                                        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
        int fd = -1;
        result = fn.get_semaphore_fd(fn.device, &info, &fd);
        if (result == VK_SUCCESS)
            result = keep(id, fd, false);
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

/* imports the stored semaphore payload into this device. */
void mvk_share_import_semaphore(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t semaphore_id = mvk_get_u64(&call->in), id = mvk_get_u64(&call->in);
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_FEATURE_NOT_PRESENT;
    ShareFn fn;
    VkSemaphore semaphore = (VkSemaphore)mvk_lookup(c, semaphore_id);
    if (!call->in.error && semaphore && functions(c, &fn)) {
        int fd = take(id, false);
        if (fd < 0) {
            result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
        } else {
            VkImportSemaphoreFdInfoKHR info = {VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR, nullptr, semaphore, 0,
                                               VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT, fd};
            result = fn.import_semaphore_fd(fn.device, &info);
            if (result != VK_SUCCESS)
                close(fd);
            else
                mvk_mirror_refresh(c, semaphore);
        }
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}
#endif
