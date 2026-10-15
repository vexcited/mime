/*
 * the vulkan connections of guest processes, run on the host's vulkan
 * driver. each connection has a thread that runs its calls in ring order
 * and keeps the table from guest ids to host handles. memory the guest
 * maps is its own ram, imported into the driver with
 * VK_EXT_external_memory_host, so neither side copies what the other
 * writes. command buffers arrive whole and are recorded here when the
 * guest ends them.
 */
#include "mvk_host.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CONNECTIONS 64
#define MAX_PHYSICAL_DEVICES 16
#define MAX_IDS (64u << 20)


static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static MvkConnection *g_connections[MAX_CONNECTIONS];

/* ------------------------------------------------------------------ handles */

static uint64_t mix(uint64_t v)
{
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdull;
    v ^= v >> 33;
    return v;
}

static void reverse_put(MvkConnection *c, uint64_t handle, uint64_t id)
{
    if (!handle)
        return;
    if ((c->reverse_used + 1) * 2 > c->reverse_size) {
        size_t old = c->reverse_size;
        __typeof__(c->reverse) prev = c->reverse;
        c->reverse_size = old ? old * 2 : 1024;
        c->reverse = calloc(c->reverse_size, sizeof(*c->reverse));
        c->reverse_used = 0;
        for (size_t i = 0; i < old; i++)
            if (prev[i].handle)
                reverse_put(c, prev[i].handle, prev[i].id);
        free(prev);
    }
    size_t mask = c->reverse_size - 1;
    for (size_t i = mix(handle) & mask;; i = (i + 1) & mask) {
        if (!c->reverse[i].handle || c->reverse[i].handle == handle) {
            if (!c->reverse[i].handle)
                c->reverse_used++;
            c->reverse[i].handle = handle;
            c->reverse[i].id = id;
            return;
        }
    }
}

static uint64_t reverse_get(MvkConnection *c, uint64_t handle)
{
    if (!c->reverse_size || !handle)
        return 0;
    size_t mask = c->reverse_size - 1;
    for (size_t i = mix(handle) & mask; c->reverse[i].handle; i = (i + 1) & mask)
        if (c->reverse[i].handle == handle)
            return c->reverse[i].id;
    return 0;
}

static void bind(MvkConnection *c, uint64_t id, uint64_t handle)
{
    if (!id || id >= MAX_IDS)
        return;
    if (id >= c->handle_count) {
        size_t n = c->handle_count ? c->handle_count : 4096;
        while (n <= id)
            n *= 2;
        c->handles = realloc(c->handles, n * sizeof(uint64_t));
        memset(c->handles + c->handle_count, 0, (n - c->handle_count) * sizeof(uint64_t));
        c->handle_count = n;
    }
    c->handles[id] = handle;
    reverse_put(c, handle, id);
}

static uint64_t lookup(MvkConnection *c, uint64_t id)
{
    return id < c->handle_count ? c->handles[id] : 0;
}

uint64_t mvk_lookup(MvkConnection *c, uint64_t id)
{
    return lookup(c, id);
}

void mvk_bind(MvkConnection *c, uint64_t id, uint64_t handle)
{
    bind(c, id, handle);
}

uint64_t mvk_get_handle(MvkDecoder *d, int dispatchable)
{
    return lookup(d->owner, mvk_get_u64(d));
}

void mvk_bind_id(MvkCall *c, uint64_t id, uint64_t handle)
{
    bind(c->conn, id, handle);
}

uint64_t mvk_host_id(MvkEncoder *e, uint64_t handle)
{
    return reverse_get(e->owner, handle);
}

void mvk_check(MvkCall *c, VkResult result, const char *what)
{
    if (result < 0)
        mime_warn("vulkan: %s failed (%d)", what, (int)result);
}

void mvk_unsupported(MvkCall *c, const char *what)
{
    mime_warn("vulkan: the host driver has no %s", what);
    if (c->header.reply != MVK_NO_REPLY) {
        MvkEncoder *e = mvk_reply_begin(c);
        mvk_put_u32(e, (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT);
        mvk_reply_end(c);
    }
}

/* ------------------------------------------------------------------ replies and chains */

/* the reply space starts with a status word the guest checks: 1 when the reply did not fit. */
MvkEncoder *mvk_reply_begin(MvkCall *c)
{
    /* a call nobody waits for replies into nothing. */
    static uint8_t sink[16];
    if (c->header.reply == MVK_NO_REPLY) {
        c->out = (MvkEncoder){sink, sink + 8, sink + 8, 0, c->conn, NULL};
        c->replying = 1;
        return &c->out;
    }
    uint8_t *base = c->conn->reply + c->header.reply;
    c->out = (MvkEncoder){base, base + 8, base + c->header.reply_size, 0, c->conn, NULL};
    c->replying = 1;
    return &c->out;
}

void mvk_reply_end(MvkCall *c)
{
    if (c->header.reply == MVK_NO_REPLY)
        return;
    uint32_t status = c->out.overflow ? 1 : 0;
    memcpy(c->out.start, &status, 4);
}

void *mvk_get_chain(MvkDecoder *d)
{
    uint32_t type = mvk_get_u32(d);
    if (!type || d->error)
        return NULL;
    void *s = mvk_get_chain_struct(d, (VkStructureType)type);
    if (!s)
        d->error = 1;
    return s;
}

/* an output struct the guest described by the sTypes of its chain, zeroed and linked. */
void mvk_get_out_shape_into(MvkDecoder *d, void *dst, size_t size)
{
    VkBaseOutStructure *base = dst;
    memset(base, 0, size);
    base->sType = (VkStructureType)mvk_get_u32(d);
    VkBaseOutStructure *tail = base;
    for (;;) {
        uint32_t type = mvk_get_u32(d);
        if (!type || d->error)
            break;
        size_t n = mvk_struct_size((VkStructureType)type);
        if (!n) {
            d->error = 1;
            break;
        }
        VkBaseOutStructure *s = mvk_alloc(d, n);
        s->sType = (VkStructureType)type;
        tail->pNext = s;
        tail = s;
    }
}

void *mvk_get_out_shape(MvkDecoder *d, size_t size)
{
    void *s = mvk_alloc(d, size);
    mvk_get_out_shape_into(d, s, size);
    return s;
}

void mvk_reply_chain(MvkEncoder *e, const void *next)
{
    const VkBaseOutStructure *s = next;
    if (!s) {
        mvk_put_u32(e, 0);
        return;
    }
    mvk_put_u32(e, (uint32_t)s->sType);
    mvk_reply_chain_struct(e, s);
}

void mvk_put_chain(MvkEncoder *e, const void *next)
{
    mvk_put_u32(e, 0);
}

uint64_t mvk_dispatch_id(const void *object)
{
    return 0;
}

/* ------------------------------------------------------------------ instance and devices */

static bool load_library(MvkConnection *c)
{
    if (c->library)
        return true;
    c->library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!c->library) {
        mime_warn("vulkan: no vulkan loader on the host (%s)", dlerror());
        return false;
    }
    c->gipa = (PFN_vkGetInstanceProcAddr)dlsym(c->library, "vkGetInstanceProcAddr");
    return c->gipa != NULL;
}

static bool host_has(const VkExtensionProperties *list, uint32_t count, const char *name)
{
    for (uint32_t i = 0; i < count; i++)
        if (!strcmp(list[i].extensionName, name))
            return true;
    return false;
}

static bool protocol_has(const char *name)
{
    for (const char *const *e = mvk_supported_extensions; *e; e++)
        if (!strcmp(*e, name))
            return true;
    return false;
}

/* the instance extensions of the host the protocol carries: (u32 count, then names). */
static void instance_extensions(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkEncoder *e = mvk_reply_begin(call);
    if (!load_library(c)) {
        mvk_put_u32(e, (uint32_t)VK_ERROR_INITIALIZATION_FAILED);
        mvk_reply_end(call);
        return;
    }
    PFN_vkEnumerateInstanceExtensionProperties enumerate =
        (PFN_vkEnumerateInstanceExtensionProperties)c->gipa(NULL, "vkEnumerateInstanceExtensionProperties");
    PFN_vkEnumerateInstanceVersion version = (PFN_vkEnumerateInstanceVersion)c->gipa(NULL, "vkEnumerateInstanceVersion");
    uint32_t count = 0, api = VK_API_VERSION_1_0;
    enumerate(NULL, &count, NULL);
    VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
    enumerate(NULL, &count, list);
    if (version)
        version(&api);
    mvk_put_u32(e, (uint32_t)VK_SUCCESS);
    mvk_put_u32(e, api);
    /* the guest's window system stands on the host's surfaces. */
    static const VkExtensionProperties wsi[] = {
        {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_SURFACE_SPEC_VERSION},
        {"VK_KHR_win32_surface", 6},
        {VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, VK_KHR_GET_SURFACE_CAPABILITIES_2_SPEC_VERSION},
    };
    uint32_t wsi_count = host_has(list, count, VK_KHR_SURFACE_EXTENSION_NAME) ? 3 : 0;
    uint32_t n = wsi_count;
    for (uint32_t i = 0; i < count; i++)
        if (protocol_has(list[i].extensionName))
            n++;
    mvk_put_u32(e, n);
    for (uint32_t i = 0; i < wsi_count; i++)
        mvk_put_bytes(e, &wsi[i], sizeof(wsi[i]));
    for (uint32_t i = 0; i < count; i++)
        if (protocol_has(list[i].extensionName))
            mvk_put_bytes(e, &list[i], sizeof(list[i]));
    free(list);
    mvk_reply_end(call);
}

static void create_instance(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    uint64_t id = mvk_get_u64(d);
    VkInstanceCreateInfo info;
    mvk_get_VkInstanceCreateInfo(d, &info);
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (!d->error && load_library(c) && !c->instance) {
        /* the guest's layers do not exist here, and only extensions the host has go through. */
        PFN_vkEnumerateInstanceExtensionProperties enumerate =
            (PFN_vkEnumerateInstanceExtensionProperties)c->gipa(NULL, "vkEnumerateInstanceExtensionProperties");
        uint32_t count = 0;
        enumerate(NULL, &count, NULL);
        VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
        enumerate(NULL, &count, list);
        const char **names = mvk_alloc(d, sizeof(char *) * (info.enabledExtensionCount + 8));
        uint32_t n = 0;
        for (uint32_t i = 0; i < info.enabledExtensionCount; i++)
            if (host_has(list, count, info.ppEnabledExtensionNames[i]) &&
                strcmp(info.ppEnabledExtensionNames[i], VK_KHR_SURFACE_EXTENSION_NAME))
                names[n++] = info.ppEnabledExtensionNames[i];
        /* the host surfaces the presenter makes on the guest's behalf. */
        for (const char *const *x = mvk_wsi_instance_extensions; *x; x++)
            if (host_has(list, count, *x))
                names[n++] = *x;
        free(list);
        info.enabledLayerCount = 0;
        info.ppEnabledLayerNames = NULL;
        info.enabledExtensionCount = n;
        info.ppEnabledExtensionNames = names;
        PFN_vkCreateInstance create = (PFN_vkCreateInstance)c->gipa(NULL, "vkCreateInstance");
        result = create(&info, NULL, &c->instance);
        if (result == VK_SUCCESS) {
            mvk_load_dispatch(&c->fn, c->gipa, c->instance, NULL, NULL);
            bind(c, id, (uint64_t)(uintptr_t)c->instance);
        }
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

/*
 * the memory types a device can import host memory into, which the guest
 * may map: a device of its own answers, created for the question.
 */
static uint32_t importable_types(MvkConnection *c, VkPhysicalDevice physical)
{
    uint32_t count = 0;
    c->fn.vkEnumerateDeviceExtensionProperties(physical, NULL, &count, NULL);
    VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
    c->fn.vkEnumerateDeviceExtensionProperties(physical, NULL, &count, list);
    bool has = host_has(list, count, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    free(list);
    if (!has)
        return 0;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &priority};
    const char *ext = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
    VkDeviceCreateInfo info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &queue, 0, NULL, 1, &ext, NULL};
    VkDevice device;
    if (c->fn.vkCreateDevice(physical, &info, NULL, &device) != VK_SUCCESS)
        return 0;
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)c->gipa(c->instance, "vkGetDeviceProcAddr");
    PFN_vkGetMemoryHostPointerPropertiesEXT props =
        (PFN_vkGetMemoryHostPointerPropertiesEXT)gdpa(device, "vkGetMemoryHostPointerPropertiesEXT");
    PFN_vkDestroyDevice destroy = (PFN_vkDestroyDevice)gdpa(device, "vkDestroyDevice");
    uint32_t bits = 0;
    void *probe = aligned_alloc(65536, 65536);
    VkMemoryHostPointerPropertiesEXT out = {VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    if (probe && props &&
        props(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, probe, &out) == VK_SUCCESS)
        bits = out.memoryTypeBits;
    free(probe);
    destroy(device, NULL);
    return bits;
}

/* binds the guest's ids to the physical devices; replies with their count and importable memory types. */
static void physical_devices(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    uint32_t want = mvk_get_u32(d);
    uint64_t ids[MAX_PHYSICAL_DEVICES];
    for (uint32_t i = 0; i < want && i < MAX_PHYSICAL_DEVICES; i++)
        ids[i] = mvk_get_u64(d);
    MvkEncoder *e = mvk_reply_begin(call);
    VkPhysicalDevice devices[MAX_PHYSICAL_DEVICES];
    uint32_t count = MAX_PHYSICAL_DEVICES;
    VkResult result = c->instance ? c->fn.vkEnumeratePhysicalDevices(c->instance, &count, devices)
                                  : VK_ERROR_INITIALIZATION_FAILED;
    if (result < 0)
        count = 0;
    if (count > want)
        count = want;
    for (uint32_t i = 0; i < count; i++)
        bind(c, ids[i], (uint64_t)(uintptr_t)devices[i]);
    mvk_put_u32(e, (uint32_t)(result < 0 ? result : VK_SUCCESS));
    mvk_put_u32(e, count);
    for (uint32_t i = 0; i < count; i++)
        mvk_put_u32(e, importable_types(c, devices[i]));
    mvk_reply_end(call);
}

static void device_extensions(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    VkPhysicalDevice physical = (VkPhysicalDevice)mvk_get_handle(d, 1);
    MvkEncoder *e = mvk_reply_begin(call);
    uint32_t count = 0;
    if (!physical || c->fn.vkEnumerateDeviceExtensionProperties(physical, NULL, &count, NULL) < 0) {
        mvk_put_u32(e, (uint32_t)VK_ERROR_INITIALIZATION_FAILED);
        mvk_reply_end(call);
        return;
    }
    VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
    c->fn.vkEnumerateDeviceExtensionProperties(physical, NULL, &count, list);
    mvk_put_u32(e, (uint32_t)VK_SUCCESS);
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++)
        if (protocol_has(list[i].extensionName) || mvk_wsi_device_extension(list[i].extensionName))
            n++;
    mvk_put_u32(e, n);
    for (uint32_t i = 0; i < count; i++)
        if (protocol_has(list[i].extensionName) || mvk_wsi_device_extension(list[i].extensionName))
            mvk_put_bytes(e, &list[i], sizeof(list[i]));
    free(list);
    mvk_reply_end(call);
}

/*
 * the device, with the import of host memory added so the guest's mapped
 * memory can be its own ram; then the queues the guest named, as (family,
 * index, id) triples.
 */
/*
 * buffers may be bound to guest memory the host imported, which vulkan
 * allows only to buffers made for that kind of handle: every buffer is
 * made so. the driver's function, the same for every connection.
 */
static PFN_vkCreateBuffer g_create_buffer;

static VKAPI_ATTR VkResult VKAPI_CALL create_importable_buffer(VkDevice device, const VkBufferCreateInfo *info,
                                                               const VkAllocationCallbacks *allocator,
                                                               VkBuffer *buffer)
{
    for (const VkBaseInStructure *s = info->pNext; s; s = s->pNext)
        if (s->sType == VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO)
            return g_create_buffer(device, info, allocator, buffer);
    VkExternalMemoryBufferCreateInfo external = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO, info->pNext,
                                                 VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
    VkBufferCreateInfo copy = *info;
    copy.pNext = &external;
    return g_create_buffer(device, &copy, allocator, buffer);
}

static void create_device(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    VkPhysicalDevice physical = (VkPhysicalDevice)mvk_get_handle(d, 1);
    uint64_t id = mvk_get_u64(d);
    VkDeviceCreateInfo info;
    mvk_get_VkDeviceCreateInfo(d, &info);
    uint32_t queues = mvk_get_u32(d);
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (!d->error && physical && !c->device) {
        uint32_t count = 0;
        c->fn.vkEnumerateDeviceExtensionProperties(physical, NULL, &count, NULL);
        VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
        c->fn.vkEnumerateDeviceExtensionProperties(physical, NULL, &count, list);
        const char **names = mvk_alloc(d, sizeof(char *) * (info.enabledExtensionCount + 4));
        uint32_t n = 0;
        for (uint32_t i = 0; i < info.enabledExtensionCount; i++)
            if (host_has(list, count, info.ppEnabledExtensionNames[i]))
                names[n++] = info.ppEnabledExtensionNames[i];
        c->host_import = host_has(list, count, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
        if (c->host_import)
            names[n++] = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
        /* the presenter's swapchains, whether the guest presents or not. */
        bool swapchain = false;
        for (uint32_t i = 0; i < n; i++)
            swapchain |= !strcmp(names[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        if (!swapchain && host_has(list, count, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            names[n++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        free(list);
        info.enabledLayerCount = 0;
        info.enabledExtensionCount = n;
        info.ppEnabledExtensionNames = names;
        result = c->fn.vkCreateDevice(physical, &info, NULL, &c->device);
        if (result == VK_SUCCESS) {
            c->physical_device = physical;
            PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)c->gipa(c->instance, "vkGetDeviceProcAddr");
            mvk_load_dispatch(&c->fn, c->gipa, c->instance, gdpa, c->device);
            if (c->host_import && c->fn.vkCreateBuffer) {
                g_create_buffer = c->fn.vkCreateBuffer;
                c->fn.vkCreateBuffer = create_importable_buffer;
            }
            bind(c, id, (uint64_t)(uintptr_t)c->device);
            VkPhysicalDeviceExternalMemoryHostPropertiesEXT host = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
            VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &host};
            c->fn.vkGetPhysicalDeviceProperties2(physical, &props);
            c->import_alignment = host.minImportedHostPointerAlignment ? host.minImportedHostPointerAlignment : 4096;
            for (uint32_t i = 0; i < queues; i++) {
                uint32_t family = mvk_get_u32(d), index = mvk_get_u32(d);
                uint64_t qid = mvk_get_u64(d);
                VkQueue q = VK_NULL_HANDLE;
                c->fn.vkGetDeviceQueue(c->device, family, index, &q);
                bind(c, qid, (uint64_t)(uintptr_t)q);
                if (q && c->queue_count < sizeof(c->queues) / sizeof(c->queues[0]))
                    c->queues[c->queue_count++] = (__typeof__(c->queues[0])){q, family};
            }
        }
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

/*
 * the device goes with the swapchains the presenter made on it, whose
 * functions were the device's, and a later device may take its place.
 */
static void destroy_device(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t id = mvk_get_u64(&call->in);
    if (call->in.error || !c->device || lookup(c, id) != (uint64_t)(uintptr_t)c->device)
        return;
    mvk_wsi_close(c);
    c->fn.vkDeviceWaitIdle(c->device);
    c->fn.vkDestroyDevice(c->device, NULL);
    bind(c, id, 0);
    c->device = VK_NULL_HANDLE;
    c->physical_device = VK_NULL_HANDLE;
    c->queue_count = 0;
}

/* ------------------------------------------------------------------ memory */

/*
 * memory the guest maps lives in its ram, physically contiguous: the host
 * imports it. other memory is the driver's own.
 */
static void allocate_memory(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    uint64_t id = mvk_get_u64(d);
    VkMemoryAllocateInfo info;
    mvk_get_VkMemoryAllocateInfo(d, &info);
    uint64_t pa = mvk_get_u64(d);
    uint64_t span = mvk_get_u64(d);
    MvkEncoder *e = mvk_reply_begin(call);
    VkResult result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImportMemoryHostPointerInfoEXT import = {VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT};
    if (!d->error && c->device) {
        if (pa) {
            void *host = mime_vm_phys(c->vm, pa, span);
            if (host && c->host_import && span % c->import_alignment == 0 && span >= info.allocationSize) {
                import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
                import.pHostPointer = host;
                import.pNext = info.pNext;
                info.pNext = &import;
                info.allocationSize = span;
                result = c->fn.vkAllocateMemory(c->device, &info, NULL, &memory);
            }
        } else {
            result = c->fn.vkAllocateMemory(c->device, &info, NULL, &memory);
        }
        if (result == VK_SUCCESS)
            bind(c, id, (uint64_t)memory);
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

static void free_memory(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t id = mvk_get_u64(&call->in);
    VkDeviceMemory memory = (VkDeviceMemory)lookup(c, id);
    if (memory && c->device)
        c->fn.vkFreeMemory(c->device, memory, NULL);
    bind(c, id, 0);
}

/* ------------------------------------------------------------------ command buffers */

static void allocate_command_buffers(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    VkCommandBufferAllocateInfo info;
    mvk_get_VkCommandBufferAllocateInfo(d, &info);
    uint32_t n = info.commandBufferCount;
    uint64_t *ids = mvk_alloc(d, sizeof(uint64_t) * (n ? n : 1));
    for (uint32_t i = 0; i < n; i++)
        ids[i] = mvk_get_u64(d);
    MvkEncoder *e = mvk_reply_begin(call);
    VkCommandBuffer *buffers = mvk_alloc(d, sizeof(VkCommandBuffer) * (n ? n : 1));
    VkResult result = d->error || !c->device ? VK_ERROR_INITIALIZATION_FAILED
                                             : c->fn.vkAllocateCommandBuffers(c->device, &info, buffers);
    if (result == VK_SUCCESS)
        for (uint32_t i = 0; i < n; i++)
            bind(c, ids[i], (uint64_t)(uintptr_t)buffers[i]);
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

static void free_command_buffers(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    VkCommandPool pool = (VkCommandPool)mvk_get_handle(d, 0);
    uint32_t n = mvk_get_u32(d);
    VkCommandBuffer *buffers = mvk_alloc(d, sizeof(VkCommandBuffer) * (n ? n : 1));
    for (uint32_t i = 0; i < n; i++) {
        uint64_t id = mvk_get_u64(d);
        buffers[i] = (VkCommandBuffer)(uintptr_t)lookup(c, id);
        bind(c, id, 0);
    }
    if (!d->error && c->device)
        c->fn.vkFreeCommandBuffers(c->device, pool, n, buffers);
}

static void run_stream(MvkCall *outer, VkCommandBuffer buffer, const uint8_t *stream, size_t size);

/*
 * records a command buffer from its stream: the begin information, then
 * the chunks of commands the guest wrote, which are copied before they are
 * read since the guest may write them meanwhile.
 */
static void record(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    VkCommandBuffer buffer = (VkCommandBuffer)(uintptr_t)mvk_get_handle(d, 1);
    VkCommandBufferBeginInfo info;
    mvk_get_VkCommandBufferBeginInfo(d, &info);
    uint64_t pa = mvk_get_u64(d);
    uint64_t total = mvk_get_u64(d);
    if (d->error || !buffer || !c->device)
        return;
    if (total > c->stream_size) {
        free(c->stream);
        c->stream_size = MIME_ALIGN_UP(total, 1u << 20);
        c->stream = malloc(c->stream_size);
        if (!c->stream) {
            c->stream_size = 0;
            return;
        }
    }
    size_t at = 0;
    for (int chunks = 0; pa && at < total && chunks < 1 << 20; chunks++) {
        MvkStreamChunk *chunk = mime_vm_phys(c->vm, pa, MVK_STREAM_CHUNK);
        if (!chunk)
            break;
        MvkStreamChunk head = *chunk;
        size_t n = MIME_MIN((size_t)head.used, (size_t)MVK_STREAM_CHUNK - sizeof(head));
        n = MIME_MIN(n, (size_t)(total - at));
        memcpy(c->stream + at, chunk + 1, n);
        at += n;
        pa = head.next_pa;
    }
    c->fn.vkBeginCommandBuffer(buffer, &info);
    run_stream(call, buffer, c->stream, at);
    VkResult result = c->fn.vkEndCommandBuffer(buffer);
    mvk_check(call, result, "vkEndCommandBuffer");
}

static void reset_command_buffer(MvkCall *call)
{
    MvkDecoder *d = &call->in;
    VkCommandBuffer buffer = (VkCommandBuffer)(uintptr_t)mvk_get_handle(d, 1);
    uint32_t flags = mvk_get_u32(d);
    if (buffer)
        call->fn->vkResetCommandBuffer(buffer, flags);
}

static void reset_command_pool(MvkCall *call)
{
    MvkDecoder *d = &call->in;
    VkCommandPool pool = (VkCommandPool)mvk_get_handle(d, 0);
    uint32_t flags = mvk_get_u32(d);
    if (call->conn->device)
        call->fn->vkResetCommandPool(call->conn->device, pool, flags);
}

static void destroy_command_pool(MvkCall *call)
{
    MvkDecoder *d = &call->in;
    uint64_t id = mvk_get_u64(d);
    VkCommandPool pool = (VkCommandPool)lookup(call->conn, id);
    if (call->conn->device && pool)
        call->fn->vkDestroyCommandPool(call->conn->device, pool, NULL);
    bind(call->conn, id, 0);
}

/* ------------------------------------------------------------------ queues and synchronization */

static void queue_submit(MvkCall *call)
{
    MvkDecoder *d = &call->in;
    VkQueue queue = (VkQueue)(uintptr_t)mvk_get_handle(d, 1);
    uint32_t count = mvk_get_u32(d);
    VkSubmitInfo *submits = mvk_alloc(d, sizeof(VkSubmitInfo) * (count ? count : 1));
    for (uint32_t i = 0; i < count; i++)
        mvk_get_VkSubmitInfo(d, &submits[i]);
    VkFence fence = (VkFence)mvk_get_handle(d, 0);
    if (!d->error && queue)
        mvk_check(call, call->fn->vkQueueSubmit(queue, count, submits, fence), "vkQueueSubmit");
}

static void queue_submit2(MvkCall *call)
{
    MvkDecoder *d = &call->in;
    VkQueue queue = (VkQueue)(uintptr_t)mvk_get_handle(d, 1);
    uint32_t count = mvk_get_u32(d);
    VkSubmitInfo2 *submits = mvk_alloc(d, sizeof(VkSubmitInfo2) * (count ? count : 1));
    for (uint32_t i = 0; i < count; i++)
        mvk_get_VkSubmitInfo2(d, &submits[i]);
    VkFence fence = (VkFence)mvk_get_handle(d, 0);
    if (!d->error && queue)
        mvk_check(call, call->fn->vkQueueSubmit2(queue, count, submits, fence), "vkQueueSubmit2");
}

static void reply_result(MvkCall *call, VkResult result)
{
    MvkEncoder *e = mvk_reply_begin(call);
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

static void queue_wait_idle(MvkCall *call)
{
    VkQueue queue = (VkQueue)(uintptr_t)mvk_get_handle(&call->in, 1);
    reply_result(call, queue ? call->fn->vkQueueWaitIdle(queue) : VK_ERROR_DEVICE_LOST);
}

static void device_wait_idle(MvkCall *call)
{
    reply_result(call, call->conn->device ? call->fn->vkDeviceWaitIdle(call->conn->device) : VK_ERROR_DEVICE_LOST);
}

static void create_object(MvkCall *call, int op)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    uint64_t id = mvk_get_u64(d);
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    uint64_t handle = 0;
    if (op == MVK_OP_CREATE_FENCE) {
        VkFenceCreateInfo info;
        mvk_get_VkFenceCreateInfo(d, &info);
        if (!d->error && c->device)
            result = c->fn.vkCreateFence(c->device, &info, NULL, (VkFence *)&handle);
    } else if (op == MVK_OP_CREATE_SEMAPHORE) {
        VkSemaphoreCreateInfo info;
        mvk_get_VkSemaphoreCreateInfo(d, &info);
        if (!d->error && c->device)
            result = c->fn.vkCreateSemaphore(c->device, &info, NULL, (VkSemaphore *)&handle);
    } else if (op == MVK_OP_CREATE_EVENT) {
        VkEventCreateInfo info;
        mvk_get_VkEventCreateInfo(d, &info);
        if (!d->error && c->device)
            result = c->fn.vkCreateEvent(c->device, &info, NULL, (VkEvent *)&handle);
    } else if (op == MVK_OP_CREATE_DESCRIPTOR_POOL) {
        VkDescriptorPoolCreateInfo info;
        mvk_get_VkDescriptorPoolCreateInfo(d, &info);
        if (!d->error && c->device)
            result = c->fn.vkCreateDescriptorPool(c->device, &info, NULL, (VkDescriptorPool *)&handle);
    }
    if (result == VK_SUCCESS)
        bind(c, id, handle);
    mvk_check(call, result, "creating a synchronization object");
}

static void destroy_object(MvkCall *call, int op)
{
    MvkConnection *c = call->conn;
    uint64_t id = mvk_get_u64(&call->in);
    uint64_t handle = lookup(c, id);
    if (handle && c->device) {
        if (op == MVK_OP_DESTROY_FENCE)
            c->fn.vkDestroyFence(c->device, (VkFence)handle, NULL);
        else if (op == MVK_OP_DESTROY_SEMAPHORE)
            c->fn.vkDestroySemaphore(c->device, (VkSemaphore)handle, NULL);
        else if (op == MVK_OP_DESTROY_EVENT)
            c->fn.vkDestroyEvent(c->device, (VkEvent)handle, NULL);
        else if (op == MVK_OP_DESTROY_DESCRIPTOR_POOL)
            c->fn.vkDestroyDescriptorPool(c->device, (VkDescriptorPool)handle, NULL);
    }
    bind(c, id, 0);
}

static void wait_fences(MvkCall *call)
{
    MvkDecoder *d = &call->in;
    uint32_t n = mvk_get_u32(d);
    VkFence *fences = mvk_alloc(d, sizeof(VkFence) * (n ? n : 1));
    for (uint32_t i = 0; i < n; i++)
        fences[i] = (VkFence)mvk_get_handle(d, 0);
    uint32_t all = mvk_get_u32(d);
    uint64_t timeout = mvk_get_u64(d);
    reply_result(call, d->error || !call->conn->device
                           ? VK_ERROR_DEVICE_LOST
                           : call->fn->vkWaitForFences(call->conn->device, n, fences, all, timeout));
}

static void fence_status(MvkCall *call)
{
    VkFence fence = (VkFence)mvk_get_handle(&call->in, 0);
    reply_result(call, call->conn->device ? call->fn->vkGetFenceStatus(call->conn->device, fence) : VK_ERROR_DEVICE_LOST);
}

static void reset_fences(MvkCall *call)
{
    MvkDecoder *d = &call->in;
    uint32_t n = mvk_get_u32(d);
    VkFence *fences = mvk_alloc(d, sizeof(VkFence) * (n ? n : 1));
    for (uint32_t i = 0; i < n; i++)
        fences[i] = (VkFence)mvk_get_handle(d, 0);
    if (!d->error && call->conn->device)
        call->fn->vkResetFences(call->conn->device, n, fences);
}

static void wait_semaphores(MvkCall *call)
{
    MvkDecoder *d = &call->in;
    VkSemaphoreWaitInfo info;
    mvk_get_VkSemaphoreWaitInfo(d, &info);
    uint64_t timeout = mvk_get_u64(d);
    reply_result(call, d->error || !call->conn->device ? VK_ERROR_DEVICE_LOST
                                                       : call->fn->vkWaitSemaphores(call->conn->device, &info, timeout));
}

static void semaphore_value(MvkCall *call)
{
    VkSemaphore semaphore = (VkSemaphore)mvk_get_handle(&call->in, 0);
    uint64_t value = 0;
    VkResult result = call->conn->device ? call->fn->vkGetSemaphoreCounterValue(call->conn->device, semaphore, &value)
                                         : VK_ERROR_DEVICE_LOST;
    MvkEncoder *e = mvk_reply_begin(call);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u64(e, value);
    mvk_reply_end(call);
}

/* a descriptor's bytes, from what the guest's union pointed to. */
static void get_descriptor(MvkCall *call)
{
    MvkDecoder *d = &call->in;
    VkDescriptorGetInfoEXT info = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    info.type = (VkDescriptorType)mvk_get_u32(d);
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorImageInfo image = {0};
    VkDescriptorAddressInfoEXT address = {VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT};
    if (info.type == VK_DESCRIPTOR_TYPE_SAMPLER) {
        sampler = (VkSampler)mvk_get_handle(d, 0);
        info.data.pSampler = &sampler;
    } else if (info.type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR ||
               info.type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV) {
        info.data.accelerationStructure = mvk_get_u64(d);
    } else if (info.type <= VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT && mvk_get_u32(d)) {
        switch (info.type) {
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            image.sampler = (VkSampler)mvk_get_handle(d, 0);
            image.imageView = (VkImageView)mvk_get_handle(d, 0);
            image.imageLayout = (VkImageLayout)mvk_get_u32(d);
            info.data.pSampledImage = &image;
            break;
        default:
            address.address = mvk_get_u64(d);
            address.range = mvk_get_u64(d);
            address.format = (VkFormat)mvk_get_u32(d);
            info.data.pUniformBuffer = &address;
            break;
        }
    }
    uint64_t size = mvk_get_u64(d);
    uint8_t bytes[256] = {0};
    if (size > sizeof(bytes))
        size = sizeof(bytes);
    if (!d->error && call->conn->device && call->fn->vkGetDescriptorEXT)
        call->fn->vkGetDescriptorEXT(call->conn->device, &info, (size_t)size, bytes);
    MvkEncoder *e = mvk_reply_begin(call);
    mvk_put_bytes(e, bytes, (size_t)size);
    mvk_reply_end(call);
}

static void signal_semaphore(MvkCall *call)
{
    VkSemaphoreSignalInfo info;
    mvk_get_VkSemaphoreSignalInfo(&call->in, &info);
    if (!call->in.error && call->conn->device)
        call->fn->vkSignalSemaphore(call->conn->device, &info);
}

static void event_op(MvkCall *call, int op)
{
    VkEvent event = (VkEvent)mvk_get_handle(&call->in, 0);
    VkDevice device = call->conn->device;
    if (!device)
        return;
    if (op == MVK_OP_EVENT_STATUS)
        reply_result(call, call->fn->vkGetEventStatus(device, event));
    else if (op == MVK_OP_SET_EVENT)
        call->fn->vkSetEvent(device, event);
    else
        call->fn->vkResetEvent(device, event);
}

/* ------------------------------------------------------------------ descriptors and queries */

static void allocate_descriptor_sets(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    VkDescriptorSetAllocateInfo info;
    mvk_get_VkDescriptorSetAllocateInfo(d, &info);
    uint32_t n = info.descriptorSetCount;
    uint64_t *ids = mvk_alloc(d, sizeof(uint64_t) * (n ? n : 1));
    for (uint32_t i = 0; i < n; i++)
        ids[i] = mvk_get_u64(d);
    VkDescriptorSet *sets = mvk_alloc(d, sizeof(VkDescriptorSet) * (n ? n : 1));
    VkResult result = d->error || !c->device ? VK_ERROR_INITIALIZATION_FAILED
                                             : c->fn.vkAllocateDescriptorSets(c->device, &info, sets);
    if (result == VK_SUCCESS)
        for (uint32_t i = 0; i < n; i++)
            bind(c, ids[i], (uint64_t)sets[i]);
    reply_result(call, result);
}

static void free_descriptor_sets(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    VkDescriptorPool pool = (VkDescriptorPool)mvk_get_handle(d, 0);
    uint32_t n = mvk_get_u32(d);
    VkDescriptorSet *sets = mvk_alloc(d, sizeof(VkDescriptorSet) * (n ? n : 1));
    for (uint32_t i = 0; i < n; i++) {
        uint64_t id = mvk_get_u64(d);
        sets[i] = (VkDescriptorSet)lookup(c, id);
        bind(c, id, 0);
    }
    if (!d->error && c->device)
        c->fn.vkFreeDescriptorSets(c->device, pool, n, sets);
}

static void reset_descriptor_pool(MvkCall *call)
{
    MvkDecoder *d = &call->in;
    VkDescriptorPool pool = (VkDescriptorPool)mvk_get_handle(d, 0);
    if (call->conn->device)
        call->fn->vkResetDescriptorPool(call->conn->device, pool, 0);
}

static void query_pool_results(MvkCall *call)
{
    MvkDecoder *d = &call->in;
    VkQueryPool pool = (VkQueryPool)mvk_get_handle(d, 0);
    uint32_t first = mvk_get_u32(d), count = mvk_get_u32(d);
    uint64_t size = mvk_get_u64(d), stride = mvk_get_u64(d);
    uint32_t flags = mvk_get_u32(d);
    MvkEncoder *e = mvk_reply_begin(call);
    void *data = size <= call->header.reply_size ? mvk_alloc(d, size) : NULL;
    VkResult result = data && call->conn->device
                          ? call->fn->vkGetQueryPoolResults(call->conn->device, pool, first, count, size, data, stride, flags)
                          : VK_ERROR_OUT_OF_HOST_MEMORY;
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_blob(e, data, data ? size : 0);
    mvk_reply_end(call);
}

/* ------------------------------------------------------------------ running calls */

static void run_hand(MvkCall *call)
{
    switch (call->header.op) {
    case MVK_OP_INSTANCE_EXTENSIONS: instance_extensions(call); break;
    case MVK_OP_CREATE_INSTANCE: create_instance(call); break;
    case MVK_OP_DESTROY_INSTANCE: break; /* the connection outlives it, and goes with the process */
    case MVK_OP_PHYSICAL_DEVICES: physical_devices(call); break;
    case MVK_OP_DEVICE_EXTENSIONS: device_extensions(call); break;
    case MVK_OP_CREATE_DEVICE: create_device(call); break;
    case MVK_OP_DESTROY_DEVICE: destroy_device(call); break;
    case MVK_OP_ALLOCATE_MEMORY: allocate_memory(call); break;
    case MVK_OP_FREE_MEMORY: free_memory(call); break;
    case MVK_OP_ALLOCATE_COMMAND_BUFFERS: allocate_command_buffers(call); break;
    case MVK_OP_FREE_COMMAND_BUFFERS: free_command_buffers(call); break;
    case MVK_OP_RECORD: record(call); break;
    case MVK_OP_RESET_COMMAND_BUFFER: reset_command_buffer(call); break;
    case MVK_OP_RESET_COMMAND_POOL: reset_command_pool(call); break;
    case MVK_OP_DESTROY_COMMAND_POOL: destroy_command_pool(call); break;
    case MVK_OP_QUEUE_SUBMIT: queue_submit(call); break;
    case MVK_OP_QUEUE_SUBMIT2: queue_submit2(call); break;
    case MVK_OP_QUEUE_WAIT_IDLE: queue_wait_idle(call); break;
    case MVK_OP_DEVICE_WAIT_IDLE: device_wait_idle(call); break;
    case MVK_OP_CREATE_FENCE:
    case MVK_OP_CREATE_SEMAPHORE:
    case MVK_OP_CREATE_EVENT:
    case MVK_OP_CREATE_DESCRIPTOR_POOL: create_object(call, (int)call->header.op); break;
    case MVK_OP_DESTROY_FENCE:
    case MVK_OP_DESTROY_SEMAPHORE:
    case MVK_OP_DESTROY_EVENT:
    case MVK_OP_DESTROY_DESCRIPTOR_POOL: destroy_object(call, (int)call->header.op); break;
    case MVK_OP_WAIT_FENCES: wait_fences(call); break;
    case MVK_OP_FENCE_STATUS: fence_status(call); break;
    case MVK_OP_RESET_FENCES: reset_fences(call); break;
    case MVK_OP_WAIT_SEMAPHORES: wait_semaphores(call); break;
    case MVK_OP_SEMAPHORE_VALUE: semaphore_value(call); break;
    case MVK_OP_SIGNAL_SEMAPHORE: signal_semaphore(call); break;
    case MVK_OP_EVENT_STATUS:
    case MVK_OP_SET_EVENT:
    case MVK_OP_RESET_EVENT: event_op(call, (int)call->header.op); break;
    case MVK_OP_ALLOCATE_DESCRIPTOR_SETS: allocate_descriptor_sets(call); break;
    case MVK_OP_FREE_DESCRIPTOR_SETS: free_descriptor_sets(call); break;
    case MVK_OP_RESET_DESCRIPTOR_POOL: reset_descriptor_pool(call); break;
    case MVK_OP_QUERY_POOL_RESULTS: query_pool_results(call); break;
    case MVK_OP_CREATE_SWAPCHAIN: mvk_wsi_create_swapchain(call); break;
    case MVK_OP_DESTROY_SWAPCHAIN: mvk_wsi_destroy_swapchain(call); break;
    case MVK_OP_ACQUIRE_IMAGE: mvk_wsi_acquire(call); break;
    case MVK_OP_PRESENT: mvk_wsi_present(call); break;
    case MVK_OP_GET_DESCRIPTOR: get_descriptor(call); break;
    default:
        mime_warn("vulkan: unknown call %#x", call->header.op);
        break;
    }
}

static void run_call(MvkCall *call)
{
    uint32_t op = call->header.op;
    if (op > MVK_OP_NONE && op < MVK_OP_GENERATED_COUNT && mvk_generated_commands[op])
        mvk_generated_commands[op](call);
    else if (op >= MVK_OP_HAND && op < MVK_OP_HAND_END)
        run_hand(call);
    else
        mime_warn("vulkan: unknown call %#x", op);
    if (call->in.error)
        mime_warn("vulkan: malformed call %#x", op);
}

static void run_stream(MvkCall *outer, VkCommandBuffer buffer, const uint8_t *stream, size_t size)
{
    MvkConnection *c = outer->conn;
    size_t at = 0;
    while (at + sizeof(MvkCallHeader) <= size) {
        MvkCallHeader h;
        memcpy(&h, stream + at, sizeof(h));
        if (h.size < sizeof(h) || (h.size & 7) || h.size > size - at)
            break;
        MvkCall call = {.conn = c, .fn = &c->fn, .command_buffer = buffer, .header = h};
        call.header.reply = MVK_NO_REPLY;
        call.in = (MvkDecoder){stream + at + sizeof(h), stream + at + sizeof(h), stream + at + h.size, 0, c,
                               outer->in.arena + outer->in.arena_used, 0, outer->in.arena_size - outer->in.arena_used};
        if (h.op > MVK_OP_NONE && h.op < MVK_OP_GENERATED_COUNT && mvk_generated_commands[h.op])
            mvk_generated_commands[h.op](&call);
        else
            mime_warn("vulkan: unknown recorded command %#x", h.op);
        at += h.size;
    }
}

static void *connection_main(void *arg)
{
    MvkConnection *c = arg;
    mime_set_thread_name("mime-vulkan");
    MvkHeader *h = c->header;
    uint64_t get = h->get;
    for (;;) {
        uint64_t put = __atomic_load_n(&h->put, __ATOMIC_ACQUIRE);
        if (put == get) {
            /* a closing connection runs what the guest sent before going: its last destructions. */
            if (c->closing)
                break;
            /* a short spin catches the next call of a busy guest without a kick. */
            uint64_t until = mime_now_ns() + 50000;
            while (put == get && mime_now_ns() < until) {
                __builtin_ia32_pause();
                put = __atomic_load_n(&h->put, __ATOMIC_ACQUIRE);
            }
            if (put == get) {
                __atomic_store_n(&h->host_idle, 1, __ATOMIC_SEQ_CST);
                put = __atomic_load_n(&h->put, __ATOMIC_SEQ_CST);
                if (put == get) {
                    pthread_mutex_lock(&c->lock);
                    while (!c->kicked && !c->closing) {
                        struct timespec ts;
                        clock_gettime(CLOCK_REALTIME, &ts);
                        ts.tv_nsec += 100000000;
                        if (ts.tv_nsec >= 1000000000) {
                            ts.tv_sec++;
                            ts.tv_nsec -= 1000000000;
                        }
                        pthread_cond_timedwait(&c->cond, &c->lock, &ts);
                        if (__atomic_load_n(&h->put, __ATOMIC_ACQUIRE) != get)
                            break;
                    }
                    c->kicked = false;
                    pthread_mutex_unlock(&c->lock);
                }
                __atomic_store_n(&h->host_idle, 0, __ATOMIC_SEQ_CST);
                continue;
            }
        }
        uint32_t at = (uint32_t)(get % MVK_RING_SIZE);
        if (MVK_RING_SIZE - at < sizeof(MvkCallHeader)) {
            /* too little room for a call at the end of the ring: the guest went on at its start. */
            get += MVK_RING_SIZE - at;
            continue;
        }
        MvkCallHeader header;
        memcpy(&header, c->ring + at, sizeof(header));
        if (header.size < sizeof(header) || (header.size & 7) || header.size > MVK_RING_SIZE - at ||
            get + header.size > put) {
            mime_warn("vulkan: connection %u lost: a call of %u bytes at %u", c->index, header.size, at);
            __atomic_store_n(&h->lost, 1, __ATOMIC_RELEASE);
            mime_vm_raise_irq(c->vm, MIME_IRQ_GPU);
            break;
        }
        if (header.op != MVK_OP_NOP) {
            memcpy(c->call, c->ring + at, header.size);
            MvkCall call = {.conn = c, .fn = &c->fn, .header = header};
            if (header.reply != MVK_NO_REPLY &&
                (header.reply > MVK_REPLY_SIZE || header.reply_size > MVK_REPLY_SIZE - header.reply ||
                 header.reply_size < 16))
                call.header.reply = MVK_NO_REPLY;
            call.in = (MvkDecoder){c->call + sizeof(header), c->call + sizeof(header), c->call + header.size, 0, c,
                                   c->arena, 0, ARENA_SIZE};
            run_call(&call);
        }
        get += header.size;
        __atomic_store_n(&h->get, get, __ATOMIC_RELEASE);
        __atomic_store_n(&h->done, header.serial, __ATOMIC_RELEASE);
        uint64_t wait = __atomic_load_n(&h->wait_serial, __ATOMIC_ACQUIRE);
        if (wait && wait <= header.serial)
            mime_vm_raise_irq(c->vm, MIME_IRQ_GPU);
    }
    return NULL;
}

/* ------------------------------------------------------------------ hypercalls */

uint64_t mime_vk_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *index)
{
    if (size != MVK_CONNECTION_SIZE)
        return MIME_HC_STATUS_INVALID;
    uint8_t *base = mime_vm_phys(vm, pa, size);
    if (!base)
        return MIME_HC_STATUS_INVALID;
    MvkConnection *c = calloc(1, sizeof(*c));
    c->vm = vm;
    c->header = (MvkHeader *)base;
    c->ring = base + MVK_RING_OFFSET;
    c->reply = base + MVK_REPLY_OFFSET;
    c->call = malloc(MVK_MAX_CALL);
    c->arena = malloc(ARENA_SIZE);
    pthread_mutex_init(&c->lock, NULL);
    pthread_cond_init(&c->cond, NULL);
    if (!c->call || !c->arena) {
        free(c->call);
        free(c->arena);
        free(c);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    pthread_mutex_lock(&g_lock);
    uint32_t i = 1;
    while (i < MAX_CONNECTIONS && g_connections[i])
        i++;
    if (i == MAX_CONNECTIONS) {
        pthread_mutex_unlock(&g_lock);
        free(c->call);
        free(c->arena);
        free(c);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    c->index = i;
    g_connections[i] = c;
    pthread_mutex_unlock(&g_lock);
    pthread_create(&c->thread, NULL, connection_main, c);
    *index = i;
    return MIME_HC_STATUS_OK;
}

void mime_vk_kick(uint64_t index)
{
    pthread_mutex_lock(&g_lock);
    MvkConnection *c = index < MAX_CONNECTIONS ? g_connections[index] : NULL;
    if (c) {
        pthread_mutex_lock(&c->lock);
        c->kicked = true;
        pthread_cond_signal(&c->cond);
        pthread_mutex_unlock(&c->lock);
    }
    pthread_mutex_unlock(&g_lock);
}

/* returns once the host and the gpu no longer use the connection's memory, as the guest frees it next. */
void mime_vk_close(uint64_t index)
{
    pthread_mutex_lock(&g_lock);
    MvkConnection *c = index < MAX_CONNECTIONS ? g_connections[index] : NULL;
    if (c)
        g_connections[index] = NULL;
    pthread_mutex_unlock(&g_lock);
    if (!c)
        return;
    pthread_mutex_lock(&c->lock);
    c->closing = true;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->lock);
    pthread_join(c->thread, NULL);
    mvk_wsi_close(c);
    if (c->device) {
        c->fn.vkDeviceWaitIdle(c->device);
        c->fn.vkDestroyDevice(c->device, NULL);
    }
    if (c->instance)
        c->fn.vkDestroyInstance(c->instance, NULL);
    if (c->library)
        dlclose(c->library);
    free(c->handles);
    free(c->reverse);
    free(c->call);
    free(c->arena);
    free(c->stream);
    free(c);
}
