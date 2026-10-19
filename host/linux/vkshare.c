/*
 * what the host keeps of shared direct3d resources and fences for processes other than the one that made them.
 *
 * Every process has a vulkan device of its own on the host, so memory and semaphores cross between processes as
 * opaque file descriptors. A process that shares a resource exports its memory under the id of the kernel object
 * the nt handle names; a process that opens the handle imports from that id. The descriptors stay here until the
 * kernel tells the object is gone with MIME_HC_GFX_SHARE_FREE, so a resource outlives the process that made it for as
 * long as a handle does. The same device in one process shares through its objects, not through here.
 */
#include "mvk_host.h"
#include "../gpu/gpu.h"

#include <pthread.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

typedef struct Share {
    struct Share *next;
    uint64_t id;
    int memory_fd;
    int semaphore_fd;
} Share;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Share *g_shares;
static bool g_limit_raised;

/* every shared resource keeps a descriptor: the soft limit of a process is far fewer than a browser's textures. */
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
            free(s);
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

/* keeps a descriptor under the id; one of a kind stays if there is one already. */
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

/* a copy of the descriptor kept, which an import takes. */
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

/* the payload of the semaphore kept under the id becomes the one of a semaphore of this device. */
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
