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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
        typeof(c->reverse) prev = c->reverse;
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

/* ------------------------------------------------------------------ objects left alive */

static void live_put(MvkConnection *c, uint64_t handle, uint32_t kind)
{
    size_t mask = c->live_size - 1, i = mix(handle) & mask;
    while (c->live[i].handle && c->live[i].handle != handle)
        i = (i + 1) & mask;
    if (!c->live[i].handle)
        c->live_used++;
    c->live[i].handle = handle;
    c->live[i].kind = kind;
}

void mvk_track(MvkConnection *c, uint64_t handle, int kind)
{
    if (!handle || kind <= MVK_KIND_NONE)
        return;
    pthread_mutex_lock(&c->live_lock);
    if ((c->live_used + 1) * 2 > c->live_size) {
        size_t old = c->live_size;
        typeof(c->live) prev = c->live, next = calloc(old ? old * 2 : 1024, sizeof(*c->live));
        if (!next) {
            pthread_mutex_unlock(&c->live_lock);
            return;
        }
        c->live = next;
        c->live_size = old ? old * 2 : 1024;
        c->live_used = 0;
        for (size_t i = 0; i < old; i++)
            if (prev[i].handle)
                live_put(c, prev[i].handle, prev[i].kind);
        free(prev);
    }
    live_put(c, handle, (uint32_t)kind);
    pthread_mutex_unlock(&c->live_lock);
}

void mvk_forget(MvkConnection *c, uint64_t handle)
{
    if (!handle)
        return;
    pthread_mutex_lock(&c->live_lock);
    if (c->live_size) {
        size_t mask = c->live_size - 1, i = mix(handle) & mask;
        while (c->live[i].handle && c->live[i].handle != handle)
            i = (i + 1) & mask;
        if (c->live[i].handle) {
            /* the entries after it move back over the hole. */
            c->live[i].handle = 0;
            c->live_used--;
            for (size_t j = (i + 1) & mask; c->live[j].handle; j = (j + 1) & mask) {
                uint64_t h = c->live[j].handle;
                uint32_t k = c->live[j].kind;
                c->live[j].handle = 0;
                c->live_used--;
                live_put(c, h, k);
            }
        }
    }
    pthread_mutex_unlock(&c->live_lock);
}

static int by_kind(const void *a, const void *b)
{
    const typeof(*((MvkConnection *)0)->live) *x = a, *y = b;
    return (int)x->kind - (int)y->kind;
}

/*
 * what the program left alive on the device, destroyed before it as
 * windows' drivers do for a process that goes: the device is idle.
 */
static void destroy_leftovers(MvkConnection *c)
{
    pthread_mutex_lock(&c->live_lock);
    size_t n = 0;
    for (size_t i = 0; i < c->live_size; i++)
        if (c->live[i].handle)
            c->live[n++] = c->live[i];
    qsort(c->live, n, sizeof(*c->live), by_kind);
    for (size_t i = 0; i < n && c->device; i++)
        mvk_destroy_kind(&c->fn, c->device, (int)c->live[i].kind, c->live[i].handle);
    free(c->live);
    c->live = nullptr;
    c->live_size = c->live_used = 0;
    pthread_mutex_unlock(&c->live_lock);
}

/* the pool a command buffer came from, which its recordings must not share with another thread's. */
static void pool_of_put(MvkConnection *c, VkCommandBuffer buffer, VkCommandPool pool)
{
    if (!buffer)
        return;
    if ((c->buffer_pool_used + 1) * 2 > c->buffer_pool_size) {
        size_t old = c->buffer_pool_size;
        typeof(c->buffer_pools) prev = c->buffer_pools;
        c->buffer_pool_size = old ? old * 2 : 256;
        c->buffer_pools = calloc(c->buffer_pool_size, sizeof(*c->buffer_pools));
        c->buffer_pool_used = 0;
        for (size_t i = 0; i < old; i++)
            if (prev[i].buffer)
                pool_of_put(c, prev[i].buffer, prev[i].pool);
        free(prev);
    }
    size_t mask = c->buffer_pool_size - 1;
    for (size_t i = mix((uint64_t)(uintptr_t)buffer) & mask;; i = (i + 1) & mask)
        if (!c->buffer_pools[i].buffer || c->buffer_pools[i].buffer == buffer) {
            if (!c->buffer_pools[i].buffer)
                c->buffer_pool_used++;
            c->buffer_pools[i].buffer = buffer;
            c->buffer_pools[i].pool = pool;
            return;
        }
}

static VkCommandPool pool_of(MvkConnection *c, VkCommandBuffer buffer)
{
    if (!c->buffer_pool_size || !buffer)
        return VK_NULL_HANDLE;
    size_t mask = c->buffer_pool_size - 1;
    for (size_t i = mix((uint64_t)(uintptr_t)buffer) & mask; c->buffer_pools[i].buffer; i = (i + 1) & mask)
        if (c->buffer_pools[i].buffer == buffer)
            return c->buffer_pools[i].pool;
    return VK_NULL_HANDLE;
}

#define CHUNK_BITS 16u
#define CHUNK_IDS (1u << CHUNK_BITS)
#define CHUNKS (MAX_IDS >> CHUNK_BITS)

static uint64_t table_get(MvkConnection *c, uint64_t id)
{
    if (!id || id >= MAX_IDS || !c->handle_chunks)
        return 0;
    uint64_t *chunk = __atomic_load_n(&c->handle_chunks[id >> CHUNK_BITS], __ATOMIC_ACQUIRE);
    return chunk ? __atomic_load_n(&chunk[id & (CHUNK_IDS - 1)], __ATOMIC_ACQUIRE) : 0;
}

/* the connection's thread alone sets handles. */
static void table_set(MvkConnection *c, uint64_t id, uint64_t handle)
{
    if (!id || id >= MAX_IDS || !c->handle_chunks)
        return;
    uint64_t *chunk = c->handle_chunks[id >> CHUNK_BITS];
    if (!chunk) {
        if (!handle || !(chunk = calloc(CHUNK_IDS, sizeof(uint64_t))))
            return;
        __atomic_store_n(&c->handle_chunks[id >> CHUNK_BITS], chunk, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&chunk[id & (CHUNK_IDS - 1)], handle, __ATOMIC_RELEASE);
}

static void bind(MvkConnection *c, uint64_t id, uint64_t handle)
{
    if (!id || id >= MAX_IDS)
        return;
    table_set(c, id, handle);
    reverse_put(c, handle, id);
}

static uint64_t await_id(MvkConnection *c, uint64_t id);
static void unwatch(MvkConnection *c, VkSemaphore semaphore);
static void stop_watcher(MvkConnection *c);

static uint64_t await_id_aside(MvkConnection *c, uint64_t id);

/* the thread recording a command buffer aside, not the connection's. */
static __thread bool t_recorder;

/*
 * an id not bound yet may be a pipeline being created aside: its creation
 * is waited for, by the connection's thread, which binds it, or by a
 * thread recording aside, which takes the handle from the creation.
 */
static uint64_t lookup(MvkConnection *c, uint64_t id)
{
    uint64_t handle = table_get(c, id);
    if (!handle && id && __atomic_load_n(&c->jobs, __ATOMIC_ACQUIRE))
        handle = t_recorder ? await_id_aside(c, id) : await_id(c, id);
    return handle;
}

uint64_t mvk_lookup(MvkConnection *c, uint64_t id)
{
    return lookup(c, id);
}

void mvk_bind(MvkConnection *c, uint64_t id, uint64_t handle)
{
    bind(c, id, handle);
}

/* an id that names nothing, as an object whose creation failed, is not passed to the driver as null. */
uint64_t mvk_get_handle(MvkDecoder *d, int dispatchable)
{
    uint64_t id = mvk_get_u64(d), handle = lookup(d->owner, id);
    if (id && !handle)
        d->error = 1;
    return handle;
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

void mvk_malformed(MvkCall *c, const char *what)
{
    mime_warn("vulkan: malformed %s", what);
    if (c->header.reply != MVK_NO_REPLY) {
        MvkEncoder *e = mvk_reply_begin(c);
        mvk_put_u32(e, (uint32_t)VK_ERROR_INITIALIZATION_FAILED);
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
        c->out = (MvkEncoder){sink, sink + 8, sink + 8, 0, c->conn, nullptr};
        c->replying = 1;
        return &c->out;
    }
    uint8_t *base = c->conn->reply + c->header.reply;
    c->out = (MvkEncoder){base, base + 8, base + c->header.reply_size, 0, c->conn, nullptr};
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
        return nullptr;
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
    return c->gipa != nullptr;
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
        (PFN_vkEnumerateInstanceExtensionProperties)c->gipa(nullptr, "vkEnumerateInstanceExtensionProperties");
    PFN_vkEnumerateInstanceVersion version = (PFN_vkEnumerateInstanceVersion)c->gipa(nullptr, "vkEnumerateInstanceVersion");
    uint32_t count = 0, api = VK_API_VERSION_1_0;
    enumerate(nullptr, &count, nullptr);
    VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
    enumerate(nullptr, &count, list);
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
            (PFN_vkEnumerateInstanceExtensionProperties)c->gipa(nullptr, "vkEnumerateInstanceExtensionProperties");
        uint32_t count = 0;
        enumerate(nullptr, &count, nullptr);
        VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
        enumerate(nullptr, &count, list);
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
        info.ppEnabledLayerNames = nullptr;
        info.enabledExtensionCount = n;
        info.ppEnabledExtensionNames = names;
        PFN_vkCreateInstance create = (PFN_vkCreateInstance)c->gipa(nullptr, "vkCreateInstance");
        result = create(&info, nullptr, &c->instance);
        if (result == VK_SUCCESS) {
            mvk_load_dispatch(&c->fn, c->gipa, c->instance, nullptr, nullptr);
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
static uint32_t probe_importable_types(MvkConnection *c, VkPhysicalDevice physical)
{
    uint32_t count = 0;
    c->fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
    c->fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, list);
    bool has = host_has(list, count, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    free(list);
    if (!has)
        return 0;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, 0, 1, &priority};
    const char *ext = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
    VkDeviceCreateInfo info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr, 0, 1, &queue, 0, nullptr, 1, &ext, nullptr};
    VkDevice device;
    if (c->fn.vkCreateDevice(physical, &info, nullptr, &device) != VK_SUCCESS)
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
    destroy(device, nullptr);
    return bits;
}

/*
 * the answer is the same for every connection to a device, and a device made only to ask takes 70 ms, so it is
 * kept for the life of the process, by what names the device and its driver.
 */
static struct {
    uint32_t vendor, device, driver;
    char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint32_t bits;
} g_importable[8];
static uint32_t g_importable_count;
static pthread_mutex_t g_importable_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t importable_types(MvkConnection *c, VkPhysicalDevice physical)
{
    VkPhysicalDeviceProperties props;
    c->fn.vkGetPhysicalDeviceProperties(physical, &props);
    pthread_mutex_lock(&g_importable_lock);
    for (uint32_t i = 0; i < g_importable_count; i++) {
        if (g_importable[i].vendor == props.vendorID && g_importable[i].device == props.deviceID &&
            g_importable[i].driver == props.driverVersion && !strcmp(g_importable[i].name, props.deviceName)) {
            uint32_t bits = g_importable[i].bits;
            pthread_mutex_unlock(&g_importable_lock);
            return bits;
        }
    }
    pthread_mutex_unlock(&g_importable_lock);
    uint32_t bits = probe_importable_types(c, physical);
    pthread_mutex_lock(&g_importable_lock);
    if (g_importable_count < sizeof(g_importable) / sizeof(g_importable[0])) {
        g_importable[g_importable_count].vendor = props.vendorID;
        g_importable[g_importable_count].device = props.deviceID;
        g_importable[g_importable_count].driver = props.driverVersion;
        snprintf(g_importable[g_importable_count].name, sizeof(g_importable[0].name), "%s", props.deviceName);
        g_importable[g_importable_count++].bits = bits;
    }
    pthread_mutex_unlock(&g_importable_lock);
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
    if (!physical || c->fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) < 0) {
        mvk_put_u32(e, (uint32_t)VK_ERROR_INITIALIZATION_FAILED);
        mvk_reply_end(call);
        return;
    }
    VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
    c->fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, list);
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
        c->fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
        VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
        c->fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, list);
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
        /* timeline semaphores hand presents to the presenting thread: on whenever the gpu has them. */
        VkPhysicalDeviceTimelineSemaphoreFeatures timeline = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
        VkPhysicalDeviceFeatures2 query = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &timeline};
        c->fn.vkGetPhysicalDeviceFeatures2(physical, &query);
        bool named = false;
        for (VkBaseOutStructure *s = (VkBaseOutStructure *)info.pNext; s && timeline.timelineSemaphore; s = s->pNext) {
            if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
                ((VkPhysicalDeviceVulkan12Features *)s)->timelineSemaphore = VK_TRUE;
                named = true;
            } else if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES) {
                ((VkPhysicalDeviceTimelineSemaphoreFeatures *)s)->timelineSemaphore = VK_TRUE;
                named = true;
            }
        }
        if (timeline.timelineSemaphore && !named) {
            timeline.pNext = (void *)info.pNext;
            info.pNext = &timeline;
        }
        result = c->fn.vkCreateDevice(physical, &info, nullptr, &c->device);
        if (result == VK_SUCCESS) {
            c->physical_device = physical;
            c->timelines = timeline.timelineSemaphore;
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
                    c->queues[c->queue_count++] = (typeof(c->queues[0])){q, family};
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
    stop_watcher(c);
    c->fn.vkDeviceWaitIdle(c->device);
    destroy_leftovers(c);
    c->fn.vkDestroyDevice(c->device, nullptr);
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
                result = c->fn.vkAllocateMemory(c->device, &info, nullptr, &memory);
            }
        } else {
            result = c->fn.vkAllocateMemory(c->device, &info, nullptr, &memory);
        }
        if (result == VK_SUCCESS) {
            bind(c, id, (uint64_t)memory);
            mvk_track(c, (uint64_t)memory, MVK_KIND_VkDeviceMemory);
        }
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

static void free_memory(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t id = mvk_get_u64(&call->in);
    VkDeviceMemory memory = (VkDeviceMemory)lookup(c, id);
    if (memory && c->device) {
        c->fn.vkFreeMemory(c->device, memory, nullptr);
        mvk_forget(c, (uint64_t)memory);
    }
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
        for (uint32_t i = 0; i < n; i++) {
            bind(c, ids[i], (uint64_t)(uintptr_t)buffers[i]);
            pool_of_put(c, buffers[i], info.commandPool);
        }
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
/*
 * recordings aside: a command buffer's stream, copied here as it is sent,
 * decoded and recorded by a pool of threads, each with its own arena, so
 * the command buffers a program records on many threads are recorded on
 * many here too. two of one command pool are recorded one after the
 * other, as vulkan wants; submissions, and the calls that reset, free or
 * allocate command buffers, wait for every recording sent before them.
 * small streams, secondary command buffers and streams that execute
 * others are recorded here, in order.
 */
typedef struct MvkRecord {
    struct MvkRecord *queued; /* the pool's */
    struct MvkRecord *next;   /* the connection's, newest first */
    struct MvkRecord *after;  /* the last recording of the same command pool, waited for */
    MvkConnection *conn;
    VkCommandBuffer buffer;
    VkCommandPool pool;
    VkCommandBufferBeginInfo info;
    uint8_t *stream;
    size_t size;
    volatile int done;
} MvkRecord;

#define RECORD_ASIDE_MIN 16384u

static pthread_mutex_t g_record_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_record_work = PTHREAD_COND_INITIALIZER;
static MvkRecord *g_record_first, *g_record_last;
static uint32_t g_recorders;

static void run_stream(MvkCall *outer, VkCommandBuffer buffer, const uint8_t *stream, size_t size);

static void record_stream(MvkConnection *c, VkCommandBuffer buffer, const VkCommandBufferBeginInfo *info,
                          const uint8_t *stream, size_t size, uint8_t *arena, size_t arena_size)
{
    MvkCall outer = {.conn = c, .fn = &c->fn};
    outer.header.reply = MVK_NO_REPLY;
    outer.in = (MvkDecoder){nullptr, nullptr, nullptr, 0, c, arena, 0, arena_size};
    c->fn.vkBeginCommandBuffer(buffer, info);
    run_stream(&outer, buffer, stream, size);
    VkResult result = c->fn.vkEndCommandBuffer(buffer);
    if (result != VK_SUCCESS)
        mime_warn("vulkan: vkEndCommandBuffer failed: %d", result);
}

static void wait_record(MvkConnection *c, MvkRecord *r)
{
    pthread_mutex_lock(&c->record_lock);
    while (!__atomic_load_n(&r->done, __ATOMIC_ACQUIRE))
        pthread_cond_wait(&c->record_done, &c->record_lock);
    pthread_mutex_unlock(&c->record_lock);
}

static void *recorder_main(void *arg)
{
    (void)arg;
    mime_set_thread_name("mime-vk-record");
    t_recorder = true;
    uint8_t *arena = malloc(ARENA_SIZE);
    for (;;) {
        pthread_mutex_lock(&g_record_lock);
        while (!g_record_first)
            pthread_cond_wait(&g_record_work, &g_record_lock);
        MvkRecord *r = g_record_first;
        g_record_first = r->queued;
        if (!g_record_first)
            g_record_last = nullptr;
        pthread_mutex_unlock(&g_record_lock);
        MvkConnection *c = r->conn;
        if (r->after)
            wait_record(c, r->after);
        if (arena)
            record_stream(c, r->buffer, &r->info, r->stream, r->size, arena, ARENA_SIZE);
        free(r->stream);
        r->stream = nullptr;
        pthread_mutex_lock(&c->record_lock);
        __atomic_store_n(&r->done, 1, __ATOMIC_RELEASE);
        c->records_pending--;
        pthread_cond_broadcast(&c->record_done);
        pthread_mutex_unlock(&c->record_lock);
    }
    return nullptr;
}

static void record_push(MvkRecord *r)
{
    pthread_mutex_lock(&g_record_lock);
    if (!g_recorders) {
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        g_recorders = cpus > 4 ? (uint32_t)(cpus / 2 < 8 ? cpus / 2 : 8) : 2;
        for (uint32_t i = 0; i < g_recorders; i++) {
            pthread_t thread;
            pthread_create(&thread, nullptr, recorder_main, nullptr);
            pthread_detach(thread);
        }
    }
    r->queued = nullptr;
    if (g_record_last)
        g_record_last->queued = r;
    else
        g_record_first = r;
    g_record_last = r;
    pthread_cond_signal(&g_record_work);
    pthread_mutex_unlock(&g_record_lock);
}

/* every recording sent so far done, and forgotten. */
static void drain_records(MvkConnection *c)
{
    if (!c->records)
        return;
    pthread_mutex_lock(&c->record_lock);
    while (c->records_pending)
        pthread_cond_wait(&c->record_done, &c->record_lock);
    pthread_mutex_unlock(&c->record_lock);
    while (c->records) {
        MvkRecord *r = c->records;
        c->records = r->next;
        free(r);
    }
}

static bool records_waited_for(uint32_t op)
{
    switch (op) {
    case MVK_OP_QUEUE_SUBMIT:
    case MVK_OP_QUEUE_SUBMIT2:
    case MVK_OP_ALLOCATE_COMMAND_BUFFERS:
    case MVK_OP_FREE_COMMAND_BUFFERS:
    case MVK_OP_RESET_COMMAND_BUFFER:
    case MVK_OP_RESET_COMMAND_POOL:
    case MVK_OP_DESTROY_COMMAND_POOL:
    case MVK_OP_DESTROY_DEVICE:
    case MVK_OP_vkTrimCommandPool:
        return true;
    default:
        return false;
    }
}

/* whether a stream executes secondary command buffers, which must be recorded before. */
static bool executes_others(const uint8_t *stream, size_t size)
{
    for (size_t at = 0; at + sizeof(MvkStreamCommand) <= size;) {
        MvkStreamCommand h;
        memcpy(&h, stream + at, sizeof(h));
        if (h.size < sizeof(h) || (h.size & 7) || h.size > size - at)
            return false;
        if (h.op == MVK_OP_vkCmdExecuteCommands)
            return true;
        at += h.size;
    }
    return false;
}

static void record(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    VkCommandBuffer buffer = (VkCommandBuffer)(uintptr_t)mvk_get_handle(d, 1);
    VkCommandBufferBeginInfo info;
    mvk_get_VkCommandBufferBeginInfo(d, &info);
    uint64_t pa = mvk_get_u64(d);
    uint64_t total = mvk_get_u64(d);
    if (d->error || !buffer || !c->device || total > (1ull << 31))
        return;
    uint8_t *stream = malloc(total ? total : 1);
    if (!stream)
        return;
    size_t at = 0;
    for (int chunks = 0; pa && at < total && chunks < 1 << 20; chunks++) {
        MvkStreamChunk *chunk = mime_vm_phys(c->vm, pa, MVK_STREAM_CHUNK);
        if (!chunk)
            break;
        MvkStreamChunk head = *chunk;
        size_t n = MIME_MIN((size_t)head.used, (size_t)MVK_STREAM_CHUNK - sizeof(head));
        n = MIME_MIN(n, (size_t)(total - at));
        memcpy(stream + at, chunk + 1, n);
        at += n;
        pa = head.next_pa;
    }
    VkCommandPool pool = pool_of(c, buffer);
    MvkRecord *last = nullptr;
    for (MvkRecord *r = c->records; r && !last; r = r->next)
        if (r->pool == pool)
            last = r;
    bool aside = pool && at >= RECORD_ASIDE_MIN && !info.pInheritanceInfo && !executes_others(stream, at);
    if (!aside) {
        if (last)
            wait_record(c, last);
        if (executes_others(stream, at))
            drain_records(c);
        record_stream(c, buffer, &info, stream, at, d->arena + d->arena_used, d->arena_size - d->arena_used);
        free(stream);
        return;
    }
    MvkRecord *r = calloc(1, sizeof(*r));
    if (!r) {
        record_stream(c, buffer, &info, stream, at, d->arena + d->arena_used, d->arena_size - d->arena_used);
        free(stream);
        return;
    }
    *r = (MvkRecord){.after = last, .conn = c, .buffer = buffer, .pool = pool, .info = info, .stream = stream,
                     .size = at};
    r->info.pNext = nullptr;
    r->next = c->records;
    c->records = r;
    pthread_mutex_lock(&c->record_lock);
    c->records_pending++;
    pthread_mutex_unlock(&c->record_lock);
    record_push(r);
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
    if (call->conn->device && pool) {
        call->fn->vkDestroyCommandPool(call->conn->device, pool, nullptr);
        mvk_forget(call->conn, (uint64_t)pool);
    }
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
    if (!d->error && queue) {
        pthread_mutex_lock(&call->conn->queue_lock);
        VkResult result = call->fn->vkQueueSubmit(queue, count, submits, fence);
        pthread_mutex_unlock(&call->conn->queue_lock);
        mvk_check(call, result, "vkQueueSubmit");
    }
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
    if (!d->error && queue) {
        pthread_mutex_lock(&call->conn->queue_lock);
        VkResult result = call->fn->vkQueueSubmit2(queue, count, submits, fence);
        pthread_mutex_unlock(&call->conn->queue_lock);
        mvk_check(call, result, "vkQueueSubmit2");
    }
}

static void reply_result(MvkCall *call, VkResult result)
{
    MvkEncoder *e = mvk_reply_begin(call);
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

static void queue_bind_sparse(MvkCall *call)
{
    MvkDecoder *d = &call->in;
    VkQueue queue = (VkQueue)(uintptr_t)mvk_get_handle(d, 1);
    uint32_t count = mvk_get_u32(d);
    VkBindSparseInfo *infos = mvk_alloc(d, sizeof(VkBindSparseInfo) * (count ? count : 1));
    for (uint32_t i = 0; infos && i < count; i++)
        mvk_get_VkBindSparseInfo(d, &infos[i]);
    VkFence fence = (VkFence)mvk_get_handle(d, 0);
    if (!d->error && infos && queue) {
        pthread_mutex_lock(&call->conn->queue_lock);
        VkResult result = call->fn->vkQueueBindSparse(queue, count, infos, fence);
        pthread_mutex_unlock(&call->conn->queue_lock);
        mvk_check(call, result, "vkQueueBindSparse");
    }
}

/* idle waits take the presents sent before them in too. */
static void queue_wait_idle(MvkCall *call)
{
    VkQueue queue = (VkQueue)(uintptr_t)mvk_get_handle(&call->in, 1);
    mvk_wsi_drain(call->conn);
    pthread_mutex_lock(&call->conn->queue_lock);
    VkResult result = queue ? call->fn->vkQueueWaitIdle(queue) : VK_ERROR_DEVICE_LOST;
    pthread_mutex_unlock(&call->conn->queue_lock);
    reply_result(call, result);
}

static void device_wait_idle(MvkCall *call)
{
    MvkConnection *c = call->conn;
    mvk_wsi_drain(c);
    pthread_mutex_lock(&c->queue_lock);
    VkResult result = c->device ? call->fn->vkDeviceWaitIdle(c->device) : VK_ERROR_DEVICE_LOST;
    pthread_mutex_unlock(&c->queue_lock);
    reply_result(call, result);
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
            result = c->fn.vkCreateFence(c->device, &info, nullptr, (VkFence *)&handle);
    } else if (op == MVK_OP_CREATE_SEMAPHORE) {
        VkSemaphoreCreateInfo info;
        mvk_get_VkSemaphoreCreateInfo(d, &info);
        if (!d->error && c->device)
            result = c->fn.vkCreateSemaphore(c->device, &info, nullptr, (VkSemaphore *)&handle);
    } else if (op == MVK_OP_CREATE_EVENT) {
        VkEventCreateInfo info;
        mvk_get_VkEventCreateInfo(d, &info);
        if (!d->error && c->device)
            result = c->fn.vkCreateEvent(c->device, &info, nullptr, (VkEvent *)&handle);
    } else if (op == MVK_OP_CREATE_DESCRIPTOR_POOL) {
        VkDescriptorPoolCreateInfo info;
        mvk_get_VkDescriptorPoolCreateInfo(d, &info);
        if (!d->error && c->device)
            result = c->fn.vkCreateDescriptorPool(c->device, &info, nullptr, (VkDescriptorPool *)&handle);
    }
    if (result == VK_SUCCESS) {
        bind(c, id, handle);
        mvk_track(c, handle, op == MVK_OP_CREATE_FENCE       ? MVK_KIND_VkFence
                             : op == MVK_OP_CREATE_SEMAPHORE ? MVK_KIND_VkSemaphore
                             : op == MVK_OP_CREATE_EVENT     ? MVK_KIND_VkEvent
                                                             : MVK_KIND_VkDescriptorPool);
    }
    mvk_check(call, result, "creating a synchronization object");
}

static void destroy_object(MvkCall *call, int op)
{
    MvkConnection *c = call->conn;
    uint64_t id = mvk_get_u64(&call->in);
    uint64_t handle = lookup(c, id);
    if (handle && c->device) {
        if (op == MVK_OP_DESTROY_FENCE)
            c->fn.vkDestroyFence(c->device, (VkFence)handle, nullptr);
        else if (op == MVK_OP_DESTROY_SEMAPHORE) {
            unwatch(c, (VkSemaphore)handle);
            c->fn.vkDestroySemaphore(c->device, (VkSemaphore)handle, nullptr);
        }
        else if (op == MVK_OP_DESTROY_EVENT)
            c->fn.vkDestroyEvent(c->device, (VkEvent)handle, nullptr);
        else if (op == MVK_OP_DESTROY_DESCRIPTOR_POOL)
            c->fn.vkDestroyDescriptorPool(c->device, (VkDescriptorPool)handle, nullptr);
        mvk_forget(c, handle);
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
    VkDescriptorImageInfo image = {};
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
    uint8_t bytes[256] = {};
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
    void *data = size <= call->header.reply_size ? mvk_alloc(d, size) : nullptr;
    VkResult result = data && call->conn->device
                          ? call->fn->vkGetQueryPoolResults(call->conn->device, pool, first, count, size, data, stride, flags)
                          : VK_ERROR_OUT_OF_HOST_MEMORY;
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_blob(e, data, data ? size : 0);
    mvk_reply_end(call);
}

/* ------------------------------------------------------------------ pipelines created aside */

/*
 * a pipeline compiles for milliseconds, and the guest never waits for its
 * creation: run in ring order it would hold every later call, a frame's
 * submits included. the connection thread decodes it, so every handle is
 * looked up there, and a pool of threads runs the driver's call; the ids
 * are bound once it is done, and a lookup of one not bound yet waits for
 * its creation alone. destructions of what a creation may still use wait
 * for every creation before them.
 */
typedef struct MvkJob {
    struct MvkJob *next, *prev; /* the connection's, in call order */
    struct MvkJob *queued; /* the pool's */
    MvkConnection *conn;
    uint32_t op;
    VkDevice device;
    VkPipelineCache cache;
    uint32_t count;
    void *infos;
    uint64_t *ids;
    VkPipeline *pipelines;
    bool libraries; /* links pipeline libraries, which a destruction must not take away */
    VkResult result;
    volatile int done;
    uint8_t *memory; /* the call's copy, then the arena it decodes into */
} MvkJob;

static pthread_mutex_t g_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_pool_work = PTHREAD_COND_INITIALIZER;
static MvkJob *g_pool_first, *g_pool_last;
static uint32_t g_pool_threads;

static void *pool_main(void *arg)
{
    (void)arg;
    mime_set_thread_name("mime-pipelines");
    for (;;) {
        pthread_mutex_lock(&g_pool_lock);
        while (!g_pool_first)
            pthread_cond_wait(&g_pool_work, &g_pool_lock);
        MvkJob *job = g_pool_first;
        g_pool_first = job->queued;
        if (!g_pool_first)
            g_pool_last = nullptr;
        pthread_mutex_unlock(&g_pool_lock);
        const MvkDispatch *fn = &job->conn->fn;
        job->result = job->op == MVK_OP_vkCreateGraphicsPipelines
                          ? fn->vkCreateGraphicsPipelines(job->device, job->cache, job->count, job->infos, nullptr,
                                                          job->pipelines)
                          : fn->vkCreateComputePipelines(job->device, job->cache, job->count, job->infos, nullptr,
                                                         job->pipelines);
        MvkConnection *c = job->conn;
        pthread_mutex_lock(&c->job_lock);
        __atomic_store_n(&job->done, 1, __ATOMIC_RELEASE);
        __atomic_add_fetch(&c->jobs_finished, 1, __ATOMIC_RELEASE);
        pthread_cond_broadcast(&c->job_done);
        pthread_mutex_unlock(&c->job_lock);
    }
    return nullptr;
}

static void pool_push(MvkJob *job)
{
    pthread_mutex_lock(&g_pool_lock);
    if (!g_pool_threads) {
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        g_pool_threads = cpus > 4 ? (uint32_t)(cpus / 2 < 8 ? cpus / 2 : 8) : 2;
        for (uint32_t i = 0; i < g_pool_threads; i++) {
            pthread_t thread;
            pthread_create(&thread, nullptr, pool_main, nullptr);
            pthread_detach(thread);
        }
    }
    job->queued = nullptr;
    if (g_pool_last)
        g_pool_last->queued = job;
    else
        g_pool_first = job;
    g_pool_last = job;
    pthread_cond_signal(&g_pool_work);
    pthread_mutex_unlock(&g_pool_lock);
}

/* binds the ids of a finished creation and forgets it. */
static void finish_job(MvkConnection *c, MvkJob *job)
{
    /* bound before it leaves the list, so a recording thread finds one or the other. */
    for (uint32_t i = 0; i < job->count; i++) {
        bind(c, job->ids[i], (uint64_t)job->pipelines[i]);
        mvk_track(c, (uint64_t)job->pipelines[i], MVK_KIND_VkPipeline);
    }
    pthread_mutex_lock(&c->job_lock);
    if (job->prev)
        job->prev->next = job->next;
    else
        c->jobs = job->next;
    if (job->next)
        job->next->prev = job->prev;
    else
        c->last_job = job->prev;
    pthread_mutex_unlock(&c->job_lock);
    if (job->result != VK_SUCCESS)
        mime_warn("vulkan: %s failed: %d", mvk_generated_names[job->op], job->result);
    free(job->memory);
    free(job);
}

static void wait_job(MvkConnection *c, MvkJob *job)
{
    pthread_mutex_lock(&c->job_lock);
    while (!__atomic_load_n(&job->done, __ATOMIC_ACQUIRE))
        pthread_cond_wait(&c->job_done, &c->job_lock);
    pthread_mutex_unlock(&c->job_lock);
    finish_job(c, job);
}

/* the creations done so far, bound. */
static void reap_jobs(MvkConnection *c)
{
    for (MvkJob *job = c->jobs, *next; job; job = next) {
        next = job->next;
        if (__atomic_load_n(&job->done, __ATOMIC_ACQUIRE))
            finish_job(c, job);
    }
}

static void drain_jobs(MvkConnection *c)
{
    while (c->jobs)
        wait_job(c, c->jobs);
}

static uint64_t await_id(MvkConnection *c, uint64_t id)
{
    for (MvkJob *job = c->jobs; job; job = job->next)
        for (uint32_t i = 0; i < job->count; i++)
            if (job->ids[i] == id) {
                wait_job(c, job);
                return table_get(c, id);
            }
    return 0;
}

/* a recording thread's wait: the creation of the id, or its binding when it is done already. */
static uint64_t await_id_aside(MvkConnection *c, uint64_t id)
{
    uint64_t handle = 0;
    pthread_mutex_lock(&c->job_lock);
    for (MvkJob *job = c->jobs; job && !handle; job = job->next)
        for (uint32_t i = 0; i < job->count; i++)
            if (job->ids[i] == id) {
                while (!__atomic_load_n(&job->done, __ATOMIC_ACQUIRE))
                    pthread_cond_wait(&c->job_done, &c->job_lock);
                handle = (uint64_t)job->pipelines[i];
                break;
            }
    pthread_mutex_unlock(&c->job_lock);
    return handle ? handle : table_get(c, id);
}

static bool links_libraries(const void *next)
{
    for (const VkBaseInStructure *s = next; s; s = s->pNext)
        if (s->sType == VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR)
            return true;
    return false;
}

/* vkCreateGraphicsPipelines and vkCreateComputePipelines, decoded here and run by the pool. */
static void create_pipelines_aside(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint32_t op = call->header.op;
    bool graphics = op == MVK_OP_vkCreateGraphicsPipelines;
    size_t size = (size_t)(call->in.end - call->in.start), arena = size * 8 + 65536;
    MvkJob *job = calloc(1, sizeof(*job));
    uint8_t *memory = job ? malloc(size + arena) : nullptr;
    if (!memory) {
        free(job);
        mvk_check(call, VK_ERROR_OUT_OF_HOST_MEMORY, mvk_generated_names[op]);
        return;
    }
    memcpy(memory, call->in.start, size);
    MvkDecoder d = {memory, memory, memory + size, 0, c, memory + size, 0, arena};
    job->conn = c;
    job->op = op;
    job->memory = memory;
    job->device = (VkDevice)mvk_get_handle(&d, 1);
    job->cache = (VkPipelineCache)mvk_get_handle(&d, 0);
    job->count = mvk_get_u32(&d);
    size_t info_size = graphics ? sizeof(VkGraphicsPipelineCreateInfo) : sizeof(VkComputePipelineCreateInfo);
    if (mvk_get_u32(&d)) {
        job->infos = mvk_alloc(&d, (size_t)job->count * info_size);
        for (uint32_t i = 0; job->infos && i < job->count; i++) {
            if (graphics) {
                VkGraphicsPipelineCreateInfo *info = &((VkGraphicsPipelineCreateInfo *)job->infos)[i];
                mvk_get_VkGraphicsPipelineCreateInfo(&d, info);
                job->libraries |= links_libraries(info->pNext);
            } else {
                mvk_get_VkComputePipelineCreateInfo(&d, &((VkComputePipelineCreateInfo *)job->infos)[i]);
            }
        }
    }
    job->ids = mvk_alloc(&d, sizeof(uint64_t) * job->count);
    job->pipelines = mvk_alloc(&d, sizeof(VkPipeline) * job->count);
    for (uint32_t i = 0; job->ids && i < job->count; i++)
        job->ids[i] = mvk_get_u64(&d);
    if (d.error || !job->infos || !job->ids || !job->pipelines) {
        mvk_malformed(call, mvk_generated_names[op]);
        free(memory);
        free(job);
        return;
    }
    if (graphics ? !c->fn.vkCreateGraphicsPipelines : !c->fn.vkCreateComputePipelines) {
        mvk_unsupported(call, mvk_generated_names[op]);
        free(memory);
        free(job);
        return;
    }
    /* the ids may be those of pipelines destroyed before: their lookups now wait for this creation. */
    for (uint32_t i = 0; i < job->count; i++)
        table_set(c, job->ids[i], 0);
    pthread_mutex_lock(&c->job_lock);
    job->prev = c->last_job;
    if (c->last_job)
        c->last_job->next = job;
    else
        c->jobs = job;
    c->last_job = job;
    pthread_mutex_unlock(&c->job_lock);
    pool_push(job);
}

/* calls that destroy what a creation aside may still use wait for all of them. */
static bool waits_for_jobs(MvkConnection *c, uint32_t op)
{
    switch (op) {
    case MVK_OP_vkDestroyShaderModule:
    case MVK_OP_vkDestroyPipelineCache:
    case MVK_OP_vkDestroyPipelineLayout:
    case MVK_OP_vkDestroySampler:
    case MVK_OP_vkDestroyDescriptorSetLayout:
    case MVK_OP_vkDestroyRenderPass:
    case MVK_OP_vkDestroySamplerYcbcrConversion:
    case MVK_OP_DESTROY_DEVICE:
        return true;
    case MVK_OP_vkDestroyPipeline:
        for (MvkJob *job = c->jobs; job; job = job->next)
            if (job->libraries)
                return true;
        return false;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ watched semaphores */

/*
 * a guest thread waiting for timeline values would hold every call of its
 * process if the connection's thread waited for them. the guest asks
 * instead with timeouts of zero, and when that fails names the values it
 * waits for; a watcher thread waits for any of them, and for a wake
 * semaphore the connection signals when it adds one, then counts those
 * reached in the header's events and raises the interrupt, and the guest,
 * which slept until events changed, asks again.
 */
typedef struct MvkWatch {
    VkSemaphore semaphore;
    uint64_t value;
    /* a mirror's header slot, which stays watched for the value after the one it holds; -1 for a wait. */
    int32_t slot;
} MvkWatch;

static void wake_watcher(MvkConnection *c)
{
    VkSemaphoreSignalInfo signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, nullptr, c->wake, ++c->wake_value};
    c->fn.vkSignalSemaphore(c->device, &signal);
}

static void *watcher_main(void *arg)
{
    MvkConnection *c = arg;
    mime_set_thread_name("mime-vk-watch");
    VkSemaphore *semaphores = nullptr;
    uint64_t *values = nullptr;
    uint32_t capacity = 0;
    for (;;) {
        pthread_mutex_lock(&c->watch_lock);
        if (c->watch_stop) {
            pthread_mutex_unlock(&c->watch_lock);
            break;
        }
        if (c->watch_count + 1 > capacity) {
            capacity = (c->watch_count + 1) * 2;
            semaphores = realloc(semaphores, capacity * sizeof(*semaphores));
            values = realloc(values, capacity * sizeof(*values));
        }
        uint32_t n = 0;
        for (uint32_t i = 0; i < c->watch_count; i++, n++) {
            semaphores[n] = c->watches[i].semaphore;
            values[n] = c->watches[i].value;
        }
        semaphores[n] = c->wake;
        values[n++] = c->wake_value + 1;
        pthread_mutex_unlock(&c->watch_lock);

        VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, VK_SEMAPHORE_WAIT_ANY_BIT, n,
                                    semaphores, values};
        c->fn.vkWaitSemaphores(c->device, &wait, 1000000000ull);

        pthread_mutex_lock(&c->watch_lock);
        bool reached = false;
        for (uint32_t i = 0; i < c->watch_count;) {
            uint64_t value = 0;
            VkResult got = c->fn.vkGetSemaphoreCounterValue(c->device, c->watches[i].semaphore, &value);
            if (c->watches[i].slot >= 0 && got == VK_SUCCESS) {
                if (value >= c->watches[i].value) {
                    __atomic_store_n(&c->header->mirror[c->watches[i].slot], value, __ATOMIC_RELEASE);
                    c->watches[i].value = value + 1;
                }
                i++;
                continue;
            }
            if (got != VK_SUCCESS || value >= c->watches[i].value) {
                c->watches[i] = c->watches[--c->watch_count];
                reached = true;
            } else {
                i++;
            }
        }
        c->rounds++;
        pthread_cond_broadcast(&c->watch_round);
        pthread_mutex_unlock(&c->watch_lock);
        if (reached) {
            __atomic_add_fetch(&c->header->events, 1, __ATOMIC_RELEASE);
            mime_vm_raise_irq(c->vm, MIME_IRQ_GPU);
        }
    }
    free(semaphores);
    free(values);
    return nullptr;
}

static bool start_watcher(MvkConnection *c)
{
    if (c->watching)
        return true;
    VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr, VK_SEMAPHORE_TYPE_TIMELINE, 0};
    VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    if (c->fn.vkCreateSemaphore(c->device, &info, nullptr, &c->wake) != VK_SUCCESS)
        return false;
    c->wake_value = 0;
    c->watch_stop = false;
    c->watching = true;
    pthread_create(&c->watcher, nullptr, watcher_main, c);
    return true;
}

static void add_watch(MvkConnection *c, VkSemaphore semaphore, uint64_t value, int32_t slot)
{
    if (c->watch_count == c->watch_capacity) {
        c->watch_capacity = c->watch_capacity ? c->watch_capacity * 2 : 16;
        c->watches = realloc(c->watches, c->watch_capacity * sizeof(*c->watches));
    }
    c->watches[c->watch_count++] = (MvkWatch){semaphore, value, slot};
}

/*
 * a timeline semaphore the guest reads without asking: its value goes to a
 * header slot now and each time it advances, which the guest only reuses
 * once this connection ran the semaphore's destruction.
 */
static void mirror_semaphore(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    VkSemaphore semaphore = (VkSemaphore)mvk_get_handle(d, 0);
    uint32_t slot = mvk_get_u32(d);
    if (d->error || !c->device || !semaphore || slot >= MVK_MIRRORS || !c->fn.vkWaitSemaphores || !start_watcher(c))
        return;
    pthread_mutex_lock(&c->watch_lock);
    uint64_t value = 0;
    if (c->fn.vkGetSemaphoreCounterValue(c->device, semaphore, &value) == VK_SUCCESS) {
        __atomic_store_n(&c->header->mirror[slot], value, __ATOMIC_RELEASE);
        add_watch(c, semaphore, value + 1, (int32_t)slot);
    }
    pthread_mutex_unlock(&c->watch_lock);
    wake_watcher(c);
}

static void watch_semaphores(MvkCall *call)
{
    MvkConnection *c = call->conn;
    MvkDecoder *d = &call->in;
    uint32_t n = mvk_get_u32(d);
    if (d->error || !c->device || !c->fn.vkWaitSemaphores || !start_watcher(c))
        return;
    pthread_mutex_lock(&c->watch_lock);
    for (uint32_t i = 0; i < n && !d->error; i++) {
        VkSemaphore semaphore = (VkSemaphore)mvk_get_handle(d, 0);
        uint64_t value = mvk_get_u64(d);
        bool known = !semaphore;
        for (uint32_t k = 0; k < c->watch_count && !known; k++)
            known = c->watches[k].slot < 0 && c->watches[k].semaphore == semaphore && c->watches[k].value == value;
        if (known)
            continue;
        add_watch(c, semaphore, value, -1);
    }
    pthread_mutex_unlock(&c->watch_lock);
    wake_watcher(c);
}

/* a semaphore going away leaves the watches, and the watcher's wait, before it is destroyed. */
static void unwatch(MvkConnection *c, VkSemaphore semaphore)
{
    if (!c->watching)
        return;
    pthread_mutex_lock(&c->watch_lock);
    bool watched = false;
    for (uint32_t i = 0; i < c->watch_count;) {
        if (c->watches[i].semaphore == semaphore) {
            c->watches[i] = c->watches[--c->watch_count];
            watched = true;
        } else {
            i++;
        }
    }
    uint64_t round = c->rounds;
    if (watched) {
        wake_watcher(c);
        while (c->rounds <= round)
            pthread_cond_wait(&c->watch_round, &c->watch_lock);
    }
    pthread_mutex_unlock(&c->watch_lock);
}

static void stop_watcher(MvkConnection *c)
{
    if (!c->watching)
        return;
    pthread_mutex_lock(&c->watch_lock);
    c->watch_stop = true;
    pthread_mutex_unlock(&c->watch_lock);
    wake_watcher(c);
    pthread_join(c->watcher, nullptr);
    c->fn.vkDestroySemaphore(c->device, c->wake, nullptr);
    c->wake = VK_NULL_HANDLE;
    c->watch_count = 0;
    c->watching = false;
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
    case MVK_OP_QUEUE_BIND_SPARSE: queue_bind_sparse(call); break;
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
    case MVK_OP_WATCH_SEMAPHORES: watch_semaphores(call); break;
    case MVK_OP_MIRROR_SEMAPHORE: mirror_semaphore(call); break;
    default:
        mime_warn("vulkan: unknown call %#x", call->header.op);
        break;
    }
}

static void run_call(MvkCall *call)
{
    uint32_t op = call->header.op;
    MvkConnection *c = call->conn;
    uint64_t began = mime_now_ns();
    if (c->jobs) {
        uint32_t finished = __atomic_load_n(&c->jobs_finished, __ATOMIC_ACQUIRE);
        if (finished != c->jobs_seen) {
            c->jobs_seen = finished;
            reap_jobs(c);
        }
        if (c->jobs && waits_for_jobs(c, op))
            drain_jobs(c);
    }
    if (c->records && records_waited_for(op))
        drain_records(c);
    if (op == MVK_OP_vkCreateGraphicsPipelines || op == MVK_OP_vkCreateComputePipelines)
        create_pipelines_aside(call);
    else if (op > MVK_OP_NONE && op < MVK_OP_GENERATED_COUNT && mvk_generated_commands[op])
        mvk_generated_commands[op](call);
    else if (op >= MVK_OP_HAND && op < MVK_OP_HAND_END)
        run_hand(call);
    else
        mime_warn("vulkan: unknown call %#x", op);
    if (call->in.error)
        mime_warn("vulkan: malformed call %#x", op);
    /* what the driver was slow at, for MIME_DEBUG. */
    uint64_t took = mime_now_ns() - began;
    if (took > 5000000)
        mime_debug("vulkan: connection %u call %#x took %llu us", c->index, op, (unsigned long long)(took / 1000));
}

static void run_stream(MvkCall *outer, VkCommandBuffer buffer, const uint8_t *stream, size_t size)
{
    MvkConnection *c = outer->conn;
    size_t at = 0;
    while (at + sizeof(MvkStreamCommand) <= size) {
        MvkStreamCommand h;
        memcpy(&h, stream + at, sizeof(h));
        if (h.size < sizeof(h) || (h.size & 7) || h.size > size - at)
            break;
        MvkCall call = {.conn = c, .fn = &c->fn, .command_buffer = buffer};
        call.header.op = h.op;
        call.header.size = h.size;
        call.header.reply = MVK_NO_REPLY;
        call.in = (MvkDecoder){stream + at + sizeof(h), stream + at + sizeof(h), stream + at + h.size, 0, c,
                               outer->in.arena + outer->in.arena_used, 0, outer->in.arena_size - outer->in.arena_used};
        if (h.op > MVK_OP_NONE && h.op < MVK_OP_GENERATED_COUNT && mvk_generated_commands[h.op])
            mvk_generated_commands[h.op](&call);
        else
            mime_warn("vulkan: unknown recorded command %#x", h.op);
        /* what follows a command that did not decode may need it, a dispatch its pipeline: the driver
           would crash, so the command buffer keeps what came before only. */
        if (call.in.error) {
            mime_warn("vulkan: the rest of a command buffer is dropped");
            break;
        }
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
    return nullptr;
}

/* ------------------------------------------------------------------ hypercalls */

/* ------------------------------------------------------------------ the host's own device */

/*
 * opengl's channels render through a connection of the host's own: no
 * guest ring, an instance with the presenter's surfaces, and a device its
 * caller makes with what it needs.
 */
bool mvk_internal_instance(MvkConnection *c, uint32_t api)
{
    if (!load_library(c))
        return false;
    PFN_vkEnumerateInstanceExtensionProperties enumerate =
        (PFN_vkEnumerateInstanceExtensionProperties)c->gipa(nullptr, "vkEnumerateInstanceExtensionProperties");
    uint32_t count = 0;
    enumerate(nullptr, &count, nullptr);
    VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
    enumerate(nullptr, &count, list);
    const char *names[8];
    uint32_t n = 0;
    for (const char *const *x = mvk_wsi_instance_extensions; *x && n < 8; x++)
        if (host_has(list, count, *x))
            names[n++] = *x;
    free(list);
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "mime", 1, "mime", 1, api};
    VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app, 0, nullptr, n, names};
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)c->gipa(nullptr, "vkCreateInstance");
    if (create(&info, nullptr, &c->instance) != VK_SUCCESS)
        return false;
    mvk_load_dispatch(&c->fn, c->gipa, c->instance, nullptr, nullptr);
    pthread_mutex_init(&c->queue_lock, nullptr);
    pthread_mutex_init(&c->live_lock, nullptr);
    return true;
}

/* the device the caller made on the instance, with one queue of family. */
void mvk_internal_device(MvkConnection *c, VkPhysicalDevice physical, VkDevice device, uint32_t family)
{
    c->physical_device = physical;
    c->device = device;
    c->timelines = true;
    c->host_import = true;
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)c->gipa(c->instance, "vkGetDeviceProcAddr");
    mvk_load_dispatch(&c->fn, c->gipa, c->instance, gdpa, device);
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT host = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &host};
    c->fn.vkGetPhysicalDeviceProperties2(physical, &props);
    c->import_alignment = host.minImportedHostPointerAlignment ? host.minImportedHostPointerAlignment : 4096;
    VkQueue q = VK_NULL_HANDLE;
    c->fn.vkGetDeviceQueue(device, family, 0, &q);
    c->queues[0].queue = q;
    c->queues[0].family = family;
    c->queue_count = 1;
}

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
    c->handle_chunks = calloc(CHUNKS, sizeof(uint64_t *));
    pthread_mutex_init(&c->lock, nullptr);
    pthread_cond_init(&c->cond, nullptr);
    pthread_mutex_init(&c->job_lock, nullptr);
    pthread_cond_init(&c->job_done, nullptr);
    pthread_mutex_init(&c->watch_lock, nullptr);
    pthread_cond_init(&c->watch_round, nullptr);
    pthread_mutex_init(&c->queue_lock, nullptr);
    pthread_mutex_init(&c->record_lock, nullptr);
    pthread_cond_init(&c->record_done, nullptr);
    pthread_mutex_init(&c->live_lock, nullptr);
    if (!c->call || !c->arena || !c->handle_chunks) {
        free(c->call);
        free(c->arena);
        free(c->handle_chunks);
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
    pthread_create(&c->thread, nullptr, connection_main, c);
    *index = i;
    return MIME_HC_STATUS_OK;
}

void mime_vk_kick(uint64_t index)
{
    pthread_mutex_lock(&g_lock);
    MvkConnection *c = index < MAX_CONNECTIONS ? g_connections[index] : nullptr;
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
    MvkConnection *c = index < MAX_CONNECTIONS ? g_connections[index] : nullptr;
    if (c)
        g_connections[index] = nullptr;
    pthread_mutex_unlock(&g_lock);
    if (!c)
        return;
    pthread_mutex_lock(&c->lock);
    c->closing = true;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->lock);
    pthread_join(c->thread, nullptr);
    drain_records(c);
    drain_jobs(c);
    mvk_wsi_close(c);
    if (c->device)
        stop_watcher(c);
    if (c->device) {
        c->fn.vkDeviceWaitIdle(c->device);
        destroy_leftovers(c);
        c->fn.vkDestroyDevice(c->device, nullptr);
    }
    free(c->live);
    if (c->instance)
        c->fn.vkDestroyInstance(c->instance, nullptr);
    if (c->library)
        dlclose(c->library);
    free(c->watches);
    free(c->buffer_pools);
    for (uint32_t i = 0; i < CHUNKS && c->handle_chunks; i++)
        free(c->handle_chunks[i]);
    free(c->handle_chunks);
    free(c->reverse);
    free(c->call);
    free(c->arena);
    free(c);
}

void mime_vk_close_all(void)
{
    for (uint64_t i = 0; i < MAX_CONNECTIONS; i++)
        mime_vk_close(i);
}

/*
 * the first connection of a guest loads the loader and the driver, 120 ms, and makes a device to learn which
 * memory it can import, 70 ms more. A thread does both while the guest boots, so they are done when its first
 * program asks, and keeps an instance for good, as the loader would unload the driver with the last one that closed.
 */
static void *warm_main(void *arg)
{
    mime_set_thread_name("mime-vk-warm");
    uint64_t began = mime_now_ns();
    MvkConnection warm = {};
    if (!mvk_internal_instance(&warm, VK_API_VERSION_1_1))
        return nullptr;
    VkPhysicalDevice devices[MAX_PHYSICAL_DEVICES];
    uint32_t count = MAX_PHYSICAL_DEVICES;
    if (warm.fn.vkEnumeratePhysicalDevices(warm.instance, &count, devices) >= 0)
        for (uint32_t i = 0; i < count; i++)
            importable_types(&warm, devices[i]);
    /* the instance stays: the loader unloads the driver when the last one goes, and loads it again. */
    mime_debug("vulkan: the driver is warm after %llu ms", (unsigned long long)((mime_now_ns() - began) / 1000000));
    return nullptr;
}

void mime_vk_warm_up(void)
{
    pthread_t thread;
    if (pthread_create(&thread, nullptr, warm_main, nullptr) == 0)
        pthread_detach(thread);
}
