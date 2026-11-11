/*
 * runs guest Vulkan connections on the host driver. each connection executes calls in ring order and maps
 * guest ids to host handles. VK_EXT_external_memory_host imports mapped guest RAM without copies. command
 * buffers arrive as complete streams and are recorded when the guest ends them.
 */
#include "display.h"
#include "mvk_host.h"
#include "video.h"

#ifdef __APPLE__
#include "../vkmetal/vkmetal.h"
#endif

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static void dump_hook(MvkConnection *c);

#define MAX_CONNECTIONS 64
#define MAX_PHYSICAL_DEVICES 16
#define MAX_IDS (64u << 20)


static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static MvkConnection *g_connections[MAX_CONNECTIONS];

/* ------------------------------------------------------------------ handles. */

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

/* ------------------------------------------------------------------ objects left alive. */

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

/* with the device idle, destroys objects the process left alive before destroying the device. */
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

/* command pool ownership prevents concurrent recording through the same pool. */
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
static void stop_watcher(MvkConnection *c);

static uint64_t await_id_aside(MvkConnection *c, uint64_t id);

/* the recording worker, separate from the connection thread. */
static __thread bool t_recorder;

/*
 * an unbound id may be an asynchronous pipeline creation. the connection thread waits and binds it; recording
 * workers wait and take its handle directly.
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

/* ------------------------------------------------------------------ replies and chains. */

/* the reply space starts with a status word the guest checks: 1 when the reply did not fit. */
MvkEncoder *mvk_reply_begin(MvkCall *c)
{
    /* calls with no waiting guest discard replies. */
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

/* allocates and links a zeroed output chain using the requested sTypes. */
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

/* ------------------------------------------------------------------ instance and devices. */

#ifdef __APPLE__
/* mime's own Vulkan driver on Metal 4 runs in this process, see host/vkmetal. */
static bool load_library(MvkConnection *c)
{
    if (!c->library)
        c->library = dlopen(nullptr, RTLD_NOW);
    c->gipa = vkm_GetInstanceProcAddr;
    return c->library != nullptr;
}
#else
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
#endif

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
        {VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME, VK_EXT_SWAPCHAIN_COLOR_SPACE_SPEC_VERSION},
    };
    uint32_t wsi_count = host_has(list, count, VK_KHR_SURFACE_EXTENSION_NAME) ? 3 : 0;
    if (wsi_count && host_has(list, count, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
        wsi_count++;
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

/* ------------------------------------------------------------------ shared instances. */

/* zero request ids so equivalent requests from different processes compare equal with memcmp. */
typedef struct Request {
    uint8_t *bytes;
    size_t size;
} Request;

static void request_free(Request *r)
{
    free(r->bytes);
    *r = (Request){};
}

static bool request_same(const Request *a, const Request *b)
{
    return a->size && a->size == b->size && !memcmp(a->bytes, b->bytes, a->size);
}

/* a copy of the call's payload with the 64 bit ids at the offsets zeroed. */
static bool request_make(Request *r, const MvkDecoder *d, const size_t *ids, size_t count)
{
    size_t size = (size_t)(d->end - d->start);
    uint8_t *bytes = size ? malloc(size) : nullptr;
    if (!bytes)
        return false;
    memcpy(bytes, d->start, size);
    for (size_t i = 0; i < count; i++) {
        if (ids[i] > size || size - ids[i] < 8) {
            free(bytes);
            return false;
        }
        memset(bytes + ids[i], 0, 8);
    }
    r->bytes = bytes;
    r->size = size;
    return true;
}

static bool request_copy(Request *to, const Request *from)
{
    to->bytes = malloc(from->size ? from->size : 1);
    if (!to->bytes)
        return false;
    memcpy(to->bytes, from->bytes, from->size);
    to->size = from->size;
    return true;
}

/*
 * connections with identical requests share an instance, allowing them to reuse spare devices created on it.
 * the last connection destroys the instance unless a spare keeps it alive until machine shutdown.
 */
#define SPARES_MAX 2

/* a device made ahead, with what the host keeps of it and the request it was made for. */
typedef struct Spare {
    MvkConnection *shell;
    Request key;
    uint64_t made; /* when, in the host's nanoseconds. */
} Spare;

typedef struct HostInstance {
    struct HostInstance *next;
    Request key;
    uint32_t refs;
    MvkConnection base; /* the library, the instance and its commands, never a device. */
    Spare spares[SPARES_MAX];
} HostInstance;

static HostInstance *g_instances;
static pthread_mutex_t g_instance_lock = PTHREAD_MUTEX_INITIALIZER;

void mime_gpu_teardown_enter(void);
void mime_gpu_teardown_leave(void);
static void spare_destroy(HostInstance *hi);

/* creates an instance with only available host layers and extensions. */
static HostInstance *instance_make(VkInstanceCreateInfo info, MvkDecoder *d, VkResult *result)
{
    *result = VK_ERROR_INITIALIZATION_FAILED;
    HostInstance *hi = calloc(1, sizeof(*hi));
    if (!hi) {
        *result = VK_ERROR_OUT_OF_HOST_MEMORY;
        return nullptr;
    }
    MvkConnection *b = &hi->base;
    if (!load_library(b)) {
        free(hi);
        return nullptr;
    }
    PFN_vkEnumerateInstanceExtensionProperties enumerate =
        (PFN_vkEnumerateInstanceExtensionProperties)b->gipa(nullptr, "vkEnumerateInstanceExtensionProperties");
    uint32_t count = 0;
    enumerate(nullptr, &count, nullptr);
    VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
    enumerate(nullptr, &count, list);
    const char **names = mvk_alloc(d, sizeof(char *) * (info.enabledExtensionCount + 12));
    uint32_t n = 0;
    for (uint32_t i = 0; i < info.enabledExtensionCount; i++)
        if (host_has(list, count, info.ppEnabledExtensionNames[i]) &&
            strcmp(info.ppEnabledExtensionNames[i], VK_KHR_SURFACE_EXTENSION_NAME))
            names[n++] = info.ppEnabledExtensionNames[i];
    /* the host surfaces the presenter makes on the guest's behalf. */
    for (const char *const *x = mvk_wsi_instance_extensions; *x; x++) {
        bool named = false;
        for (uint32_t i = 0; i < n; i++)
            named |= !strcmp(names[i], *x);
        if (!named && host_has(list, count, *x))
            names[n++] = *x;
    }
    info.enabledLayerCount = 0;
    info.ppEnabledLayerNames = nullptr;
    free(list);
    info.enabledExtensionCount = n;
    info.ppEnabledExtensionNames = names;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)b->gipa(nullptr, "vkCreateInstance");
    *result = create(&info, nullptr, &b->instance);
    if (*result != VK_SUCCESS) {
        dlclose(b->library);
        free(hi);
        return nullptr;
    }
    mvk_load_dispatch(&b->fn, b->gipa, b->instance, nullptr, nullptr);
    mvk_budget_hook(b);
    hi->refs = 1;
    return hi;
}

/* acquires a reference to an existing matching instance or creates one. */
static HostInstance *instance_hold(Request *key, VkInstanceCreateInfo info, MvkDecoder *d, VkResult *result)
{
    pthread_mutex_lock(&g_instance_lock);
    for (HostInstance *hi = g_instances; hi; hi = hi->next) {
        if (request_same(&hi->key, key)) {
            hi->refs++;
            pthread_mutex_unlock(&g_instance_lock);
            *result = VK_SUCCESS;
            return hi;
        }
    }
    HostInstance *hi = instance_make(info, d, result);
    if (hi) {
        hi->key = *key;
        *key = (Request){};
        hi->next = g_instances;
        g_instances = hi;
    }
    pthread_mutex_unlock(&g_instance_lock);
    return hi;
}

static void instance_release(HostInstance *hi)
{
    pthread_mutex_lock(&g_instance_lock);
    bool last = --hi->refs == 0;
    if (last)
        for (HostInstance **link = &g_instances; *link; link = &(*link)->next)
            if (*link == hi) {
                *link = hi->next;
                break;
            }
    pthread_mutex_unlock(&g_instance_lock);
    if (!last)
        return;
    spare_destroy(hi);
    mime_gpu_teardown_enter();
    hi->base.fn.vkDestroyInstance(hi->base.instance, nullptr);
    mime_gpu_teardown_leave();
    dlclose(hi->base.library);
    request_free(&hi->key);
    free(hi);
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
        Request key = {};
        static const size_t id_at = 0;
        request_make(&key, d, &id_at, 1);
        HostInstance *hi = instance_hold(&key, info, d, &result);
        request_free(&key);
        if (hi) {
            c->host_instance = hi;
            c->instance = hi->base.instance;
            c->fn = hi->base.fn;
            bind(c, id, (uint64_t)(uintptr_t)c->instance);
        }
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

/* creates a temporary device to query which memory types can import host memory for guest mappings. */
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
 * cache importable memory types by device and driver for the process lifetime; creating a query device takes
 * 70 ms.
 */
static struct {
    uint32_t vendor, device, driver;
    char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    char uuids[2 * VK_UUID_SIZE * 2 + 1]; /* the device's and the driver's, in hex. */
    uint32_t bits;
} g_importable[8];
static uint32_t g_importable_count;
static pthread_mutex_t g_importable_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * persist the result to avoid 70 ms of driver work ahead of the first program at each start. the cache key
 * includes device and driver names, versions and UUIDs, so updates cannot reuse old results. ignore malformed
 * lines and only append to the file.
 */
#define IMPORT_CACHE_NAME "vulkan-import-cache"
#define IMPORT_CACHE_HEADER "mime vulkan import cache 1"

static void import_cache_path(char *out, size_t size)
{
    const MimePaths *paths = mime_daemon_paths();
    snprintf(out, size, "%s/%s", paths ? paths->cache : "", IMPORT_CACHE_NAME);
}

static void import_uuids(MvkConnection *c, VkPhysicalDevice physical, char out[2 * VK_UUID_SIZE * 2 + 1])
{
    out[0] = 0;
    if (!c->fn.vkGetPhysicalDeviceProperties2)
        return;
    VkPhysicalDeviceIDProperties id = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id};
    c->fn.vkGetPhysicalDeviceProperties2(physical, &props);
    for (uint32_t i = 0; i < VK_UUID_SIZE; i++)
        snprintf(out + i * 2, 3, "%02x", id.deviceUUID[i]);
    for (uint32_t i = 0; i < VK_UUID_SIZE; i++)
        snprintf(out + (VK_UUID_SIZE + i) * 2, 3, "%02x", id.driverUUID[i]);
}

/* the lines of the file that name this device, with the lock held. */
static bool import_cache_find(const VkPhysicalDeviceProperties *props, const char *uuids, uint32_t *bits)
{
    char path[MIME_PATH_MAX];
    import_cache_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[512];
    bool found = false, valid = fgets(line, sizeof(line), f) && !strncmp(line, IMPORT_CACHE_HEADER, strlen(IMPORT_CACHE_HEADER));
    while (valid && fgets(line, sizeof(line), f)) {
        unsigned vendor, device, driver, mask;
        char uuid_text[2 * VK_UUID_SIZE * 2 + 8];
        int name_at = 0;
        if (sscanf(line, "%x %x %x %64s %x %n", &vendor, &device, &driver, uuid_text, &mask, &name_at) < 5 || !name_at)
            continue;
        char *name = line + name_at;
        name[strcspn(name, "\n")] = 0;
        if (vendor == props->vendorID && device == props->deviceID && driver == props->driverVersion &&
            !strcmp(uuid_text, uuids[0] ? uuids : "-") && !strcmp(name, props->deviceName)) {
            *bits = mask;
            found = true;
        }
    }
    fclose(f);
    return found;
}

static void import_cache_add(const VkPhysicalDeviceProperties *props, const char *uuids, uint32_t bits)
{
    char path[MIME_PATH_MAX];
    import_cache_path(path, sizeof(path));
    bool fresh = access(path, F_OK) != 0;
    FILE *f = fopen(path, "a");
    if (!f)
        return;
    if (fresh)
        fprintf(f, "%s\n", IMPORT_CACHE_HEADER);
    fprintf(f, "%x %x %x %s %x %s\n", props->vendorID, props->deviceID, props->driverVersion, uuids[0] ? uuids : "-", bits,
            props->deviceName);
    fclose(f);
}

static uint32_t importable_types(MvkConnection *c, VkPhysicalDevice physical)
{
    VkPhysicalDeviceProperties props;
    c->fn.vkGetPhysicalDeviceProperties(physical, &props);
    char uuids[2 * VK_UUID_SIZE * 2 + 1];
    import_uuids(c, physical, uuids);
    pthread_mutex_lock(&g_importable_lock);
    for (uint32_t i = 0; i < g_importable_count; i++) {
        if (g_importable[i].vendor == props.vendorID && g_importable[i].device == props.deviceID &&
            g_importable[i].driver == props.driverVersion && !strcmp(g_importable[i].name, props.deviceName) &&
            !strcmp(g_importable[i].uuids, uuids)) {
            uint32_t bits = g_importable[i].bits;
            pthread_mutex_unlock(&g_importable_lock);
            return bits;
        }
    }
    uint32_t bits = 0;
    bool known = import_cache_find(&props, uuids, &bits);
    pthread_mutex_unlock(&g_importable_lock);
    if (!known) {
        bits = probe_importable_types(c, physical);
        pthread_mutex_lock(&g_importable_lock);
        import_cache_add(&props, uuids, bits);
    } else {
        pthread_mutex_lock(&g_importable_lock);
    }
    if (g_importable_count < sizeof(g_importable) / sizeof(g_importable[0])) {
        g_importable[g_importable_count].vendor = props.vendorID;
        g_importable[g_importable_count].device = props.deviceID;
        g_importable[g_importable_count].driver = props.driverVersion;
        snprintf(g_importable[g_importable_count].name, sizeof(g_importable[0].name), "%s", props.deviceName);
        snprintf(g_importable[g_importable_count].uuids, sizeof(g_importable[0].uuids), "%s", uuids);
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
    /* the guest sees one video extension; the host manages device requirements and codec details. */
    bool video = mvk_video_offered(c, physical);
    mvk_put_u32(e, n + (video ? 1 : 0));
    for (uint32_t i = 0; i < count; i++)
        if (protocol_has(list[i].extensionName) || mvk_wsi_device_extension(list[i].extensionName))
            mvk_put_bytes(e, &list[i], sizeof(list[i]));
    if (video) {
        VkExtensionProperties decode = {MVK_VIDEO_DECODE_EXTENSION, 1};
        mvk_put_bytes(e, &decode, sizeof(decode));
    }
    free(list);
    mvk_reply_end(call);
}

/* imports guest RAM into the device, then binds requested queues from family, index and id triples. */
/*
 * all buffers allow imported host memory handles so they can bind guest RAM. the driver function is shared by
 * connections.
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

/*
 * creates the requested device and binds queue ids from d. spare device requests have zero ids, so they bind
 * nothing.
 */
static VkResult device_create(MvkConnection *c, MvkDecoder *d, VkPhysicalDevice physical, uint64_t id,
                              VkDeviceCreateInfo info, uint32_t queues)
{
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (!d->error && physical && !c->device) {
        uint32_t count = 0;
        c->fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
        VkExtensionProperties *list = calloc(count ? count : 1, sizeof(*list));
        c->fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, list);
        const char **names = mvk_alloc(d, sizeof(char *) * (info.enabledExtensionCount + 20));
        uint32_t n = 0;
        bool video = false;
        for (uint32_t i = 0; i < info.enabledExtensionCount; i++) {
            if (!strcmp(info.ppEnabledExtensionNames[i], MVK_VIDEO_DECODE_EXTENSION))
                video = true;
            else if (host_has(list, count, info.ppEnabledExtensionNames[i]))
                names[n++] = info.ppEnabledExtensionNames[i];
        }
        /* the extensions decoding needs, and the layouts of images decoding keeps. */
        VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR unified_features = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR};
        VkPhysicalDeviceVideoMaintenance1FeaturesKHR maintenance_features = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_1_FEATURES_KHR, &unified_features};
        VkPhysicalDeviceVideoDecodeVP9FeaturesKHR vp9_features = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_DECODE_VP9_FEATURES_KHR, &maintenance_features};
        bool independent = false;
        video = video && mvk_video_offered(c, physical);
        bool unified = false;
        /* the decode queue, which the guest did not ask for. */
        float queue_priority = 1.0f;
        if (video) {
            uint32_t family = mvk_video_queue_family(c, physical);
            bool has_family = family == ~0u;
            for (uint32_t i = 0; i < info.queueCreateInfoCount; i++)
                has_family |= info.pQueueCreateInfos[i].queueFamilyIndex == family;
            if (family == ~0u) {
                video = false;
            } else if (!has_family) {
                VkDeviceQueueCreateInfo *queue_list = mvk_alloc(d, sizeof(*queue_list) * (info.queueCreateInfoCount + 1));
                memcpy(queue_list, info.pQueueCreateInfos, sizeof(*queue_list) * info.queueCreateInfoCount);
                queue_list[info.queueCreateInfoCount] = (VkDeviceQueueCreateInfo){
                    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, family, 1, &queue_priority};
                info.pQueueCreateInfos = queue_list;
                info.queueCreateInfoCount++;
            }
        }
        uint32_t video_codecs = 0;
        bool has_vp9 = false;
        if (video) {
            const char *video_names[8];
            size_t video_count = mvk_video_extensions(c, physical, video_names, &video_codecs);
            for (size_t i = 0; i < video_count; i++) {
                names[n++] = video_names[i];
                has_vp9 |= !strcmp(video_names[i], VK_KHR_VIDEO_DECODE_VP9_EXTENSION_NAME);
            }
            if (host_has(list, count, VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME)) {
                names[n++] = VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME;
                unified = true;
            }
            if (host_has(list, count, VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME)) {
                names[n++] = VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME;
                independent = true;
            }
        }
        /* a driver that is not fully conformant says so through an extension that must be on. */
        if (host_has(list, count, "VK_KHR_portability_subset")) {
            bool named = false;
            for (uint32_t i = 0; i < n; i++)
                named |= !strcmp(names[i], "VK_KHR_portability_subset");
            if (!named)
                names[n++] = "VK_KHR_portability_subset";
        }
        c->host_import = host_has(list, count, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
        if (c->host_import)
            names[n++] = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
        /* memory and semaphores that cross to other processes' devices, whether the guest asked or not. */
        if (host_has(list, count, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) &&
            host_has(list, count, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME)) {
            names[n++] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
            names[n++] = VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
        }
#ifdef __APPLE__
        /* on macOS they cross as Metal textures and shared events, see vkshare.c. */
        if (host_has(list, count, "VK_EXT_external_memory_metal") && host_has(list, count, "VK_EXT_metal_objects")) {
            names[n++] = "VK_EXT_external_memory_metal";
            names[n++] = "VK_EXT_metal_objects";
        }
#endif
        /* the presenter's swapchains, whether the guest presents or not. */
        bool swapchain = false;
        for (uint32_t i = 0; i < n; i++)
            swapchain |= !strcmp(names[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        if (!swapchain && host_has(list, count, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            names[n++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        /* and what describes their frames to a compositor that shows high dynamic range. */
        c->hdr_metadata = host_has(list, count, VK_EXT_HDR_METADATA_EXTENSION_NAME);
        bool metadata = false;
        for (uint32_t i = 0; i < n; i++)
            metadata |= !strcmp(names[i], VK_EXT_HDR_METADATA_EXTENSION_NAME);
        if (c->hdr_metadata && !metadata)
            names[n++] = VK_EXT_HDR_METADATA_EXTENSION_NAME;
        free(list);
        info.enabledLayerCount = 0;
        info.enabledExtensionCount = n;
        info.ppEnabledExtensionNames = names;
        /* timeline semaphores hand presents to the presenting thread: on whenever the GPU has them. */
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
        if (video) {
            vp9_features.pNext = &maintenance_features;
            VkPhysicalDeviceFeatures2 supported = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &vp9_features};
            c->fn.vkGetPhysicalDeviceFeatures2(physical, &supported);
            unified = unified && unified_features.unifiedImageLayouts && unified_features.unifiedImageLayoutsVideo;
            independent = independent && maintenance_features.videoMaintenance1;
            for (uint32_t i = 0; i < n; i++)
                if ((!unified && !strcmp(names[i], VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME)) ||
                    (!independent && !strcmp(names[i], VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME)))
                    names[i--] = names[--n];
            /* retains only features supported by the GPU. */
            unified_features.pNext = (void *)info.pNext;
            maintenance_features.pNext = unified ? (void *)&unified_features : (void *)info.pNext;
            vp9_features.pNext = independent ? (void *)&maintenance_features : maintenance_features.pNext;
            info.pNext = has_vp9 ? (void *)&vp9_features : vp9_features.pNext;
            info.enabledExtensionCount = n;
        }
        result = c->fn.vkCreateDevice(physical, &info, nullptr, &c->device);
        if (result == VK_SUCCESS) {
            c->physical_device = physical;
            c->timelines = timeline.timelineSemaphore;
            PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)c->gipa(c->instance, "vkGetDeviceProcAddr");
            mvk_load_dispatch(&c->fn, c->gipa, c->instance, gdpa, c->device);
            mvk_budget_hook(c);
            dump_hook(c);
            if (c->host_import && c->fn.vkCreateBuffer) {
                g_create_buffer = c->fn.vkCreateBuffer;
                c->fn.vkCreateBuffer = create_importable_buffer;
            }
            mvk_share_device_created(c);
            if (video)
                mvk_video_device_created(c, gdpa, unified, independent, video_codecs);
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
    return result;
}

/* ------------------------------------------------------------------ the spare device. */

/*
 * device creation costs a browser GPU process 78 ms with one queue. save requests on disk and pre-create
 * devices for the last two requests when the driver loads, using their shared instances. reuse requires
 * identical extensions, features and queues. replenish a spare after use and destroy unused spares at
 * shutdown.
 */
#define LEARNED_MAX 4
#define LEARNED_NAME "vulkan-device-requests"
#define LEARNED_HEADER "mime vulkan device requests 1"
#define UUID_TEXT (2 * VK_UUID_SIZE * 2 + 1)
#define REFILL_DELAY_NS 1500000000ull
#define SPARE_IDLE_NS 600000000000ull /* release idle spares after this interval so they do not keep the GPU awake. */
#define FILL_ARENA (1u << 20)

typedef struct Learned {
    char uuids[UUID_TEXT];
    Request instance, device;
} Learned;

static Learned g_learned[LEARNED_MAX]; /* the most recent first. */
static unsigned g_learned_count;
static bool g_learned_dirty;
static pthread_mutex_t g_learned_lock = PTHREAD_MUTEX_INITIALIZER;

static void learned_path(char *out, size_t size)
{
    const MimePaths *paths = mime_daemon_paths();
    snprintf(out, size, "%s/%s", paths ? paths->cache : "", LEARNED_NAME);
}

static bool hex_get(const char *text, Request *r)
{
    size_t digits = strlen(text);
    if (!digits || digits % 2)
        return false;
    r->bytes = malloc(digits / 2);
    if (!r->bytes)
        return false;
    for (size_t i = 0; i < digits / 2; i++) {
        unsigned byte;
        if (sscanf(text + i * 2, "%2x", &byte) != 1) {
            request_free(r);
            return false;
        }
        r->bytes[i] = (uint8_t)byte;
    }
    r->size = digits / 2;
    return true;
}

/* ignore malformed lines and files with a different format. */
static void learned_load(void)
{
    char path[MIME_PATH_MAX];
    learned_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char *line = nullptr;
    size_t capacity = 0;
    bool valid = getline(&line, &capacity, f) > 0 && !strncmp(line, LEARNED_HEADER, strlen(LEARNED_HEADER));
    pthread_mutex_lock(&g_learned_lock);
    while (valid && g_learned_count < LEARNED_MAX && getline(&line, &capacity, f) > 0) {
        char *save = nullptr;
        char *uuids = strtok_r(line, " \n", &save), *instance = strtok_r(nullptr, " \n", &save),
             *device = strtok_r(nullptr, " \n", &save);
        Learned l = {};
        if (!uuids || !instance || !device || strlen(uuids) >= sizeof(l.uuids))
            continue;
        strcpy(l.uuids, uuids);
        if (!hex_get(instance, &l.instance))
            continue;
        if (!hex_get(device, &l.device)) {
            request_free(&l.instance);
            continue;
        }
        g_learned[g_learned_count++] = l;
    }
    pthread_mutex_unlock(&g_learned_lock);
    free(line);
    fclose(f);
}

static void hex_put(FILE *f, const Request *r)
{
    for (size_t i = 0; i < r->size; i++)
        fprintf(f, "%02x", r->bytes[i]);
}

/* write a replacement file and rename it atomically to preserve complete data on interruption. */
static void learned_save(void)
{
    pthread_mutex_lock(&g_learned_lock);
    if (g_learned_dirty) {
        g_learned_dirty = false;
        char path[MIME_PATH_MAX], aside[MIME_PATH_MAX + 8];
        learned_path(path, sizeof(path));
        snprintf(aside, sizeof(aside), "%s.new", path);
        FILE *f = fopen(aside, "w");
        if (f) {
            fprintf(f, "%s\n", LEARNED_HEADER);
            for (unsigned i = 0; i < g_learned_count; i++) {
                fprintf(f, "%s ", g_learned[i].uuids);
                hex_put(f, &g_learned[i].instance);
                fputc(' ', f);
                hex_put(f, &g_learned[i].device);
                fputc('\n', f);
            }
            if (fclose(f) == 0)
                rename(aside, path);
            else
                unlink(aside);
        }
    }
    pthread_mutex_unlock(&g_learned_lock);
}

/* moves the latest successful device request to the front. */
static void learned_note(const char *uuids, const Request *instance, const Request *device)
{
    pthread_mutex_lock(&g_learned_lock);
    unsigned at = 0;
    bool found = false;
    for (unsigned i = 0; i < g_learned_count && !found; i++) {
        found = !strcmp(g_learned[i].uuids, uuids) && request_same(&g_learned[i].instance, instance) &&
                request_same(&g_learned[i].device, device);
        at = i;
    }
    if (found && at == 0) {
        pthread_mutex_unlock(&g_learned_lock);
        return;
    }
    Learned l = {};
    if (found) {
        l = g_learned[at];
    } else {
        snprintf(l.uuids, sizeof(l.uuids), "%s", uuids);
        if (!request_copy(&l.instance, instance) || !request_copy(&l.device, device)) {
            request_free(&l.instance);
            request_free(&l.device);
            pthread_mutex_unlock(&g_learned_lock);
            return;
        }
        if (g_learned_count == LEARNED_MAX) {
            request_free(&g_learned[LEARNED_MAX - 1].instance);
            request_free(&g_learned[LEARNED_MAX - 1].device);
            at = LEARNED_MAX - 1;
        } else {
            at = g_learned_count++;
        }
    }
    memmove(&g_learned[1], &g_learned[0], at * sizeof(g_learned[0]));
    g_learned[0] = l;
    g_learned_dirty = true;
    pthread_mutex_unlock(&g_learned_lock);
}

static uint32_t g_shells_destroyed;

static void shell_destroy(MvkConnection *s)
{
    __atomic_fetch_add(&g_shells_destroyed, 1, __ATOMIC_RELAXED);
    s->fn.vkDeviceWaitIdle(s->device);
    mvk_video_close(s);
    mime_gpu_teardown_enter();
    mvk_budget_forget(s->device);
    s->fn.vkDestroyDevice(s->device, nullptr);
    mime_gpu_teardown_leave();
    free(s);
}

static void spare_destroy(HostInstance *hi)
{
    MvkConnection *doomed[SPARES_MAX];
    pthread_mutex_lock(&g_instance_lock);
    for (unsigned i = 0; i < SPARES_MAX; i++) {
        doomed[i] = hi->spares[i].shell;
        hi->spares[i].shell = nullptr;
        request_free(&hi->spares[i].key);
    }
    pthread_mutex_unlock(&g_instance_lock);
    for (unsigned i = 0; i < SPARES_MAX; i++)
        if (doomed[i])
            shell_destroy(doomed[i]);
}

/* shared instances live until shutdown; tracks the device currently being prepared. */
static HostInstance *g_pool[SPARES_MAX];
static bool g_building;
static const Request *g_building_key;
static pthread_cond_t g_spare_made = PTHREAD_COND_INITIALIZER;

static void pool_release(void)
{
    uint32_t before = __atomic_load_n(&g_shells_destroyed, __ATOMIC_RELAXED);
    HostInstance *held[SPARES_MAX];
    pthread_mutex_lock(&g_instance_lock);
    for (unsigned i = 0; i < SPARES_MAX; i++) {
        held[i] = g_pool[i];
        g_pool[i] = nullptr;
    }
    pthread_mutex_unlock(&g_instance_lock);
    for (unsigned i = 0; i < SPARES_MAX; i++)
        if (held[i])
            instance_release(held[i]);
    uint32_t left = __atomic_load_n(&g_shells_destroyed, __ATOMIC_RELAXED) - before;
    if (left)
        mime_info("vulkan: %u devices made ahead were not taken and are destroyed with the machine", left);
}

/* finds a spare matching the request and instance, with the lock held. */
static Spare *spare_find(HostInstance *hi, const Request *key)
{
    for (unsigned i = 0; i < SPARES_MAX; i++)
        if (hi->spares[i].shell && request_same(&hi->spares[i].key, key))
            return &hi->spares[i];
    return nullptr;
}

/* takes a matching spare, waiting if its creation is in progress. */
static MvkConnection *spare_take(HostInstance *hi, const Request *key, VkPhysicalDevice physical, uint32_t queues)
{
    struct timespec limit;
    clock_gettime(CLOCK_REALTIME, &limit);
    limit.tv_nsec += 400000000;
    if (limit.tv_nsec >= 1000000000) {
        limit.tv_sec++;
        limit.tv_nsec -= 1000000000;
    }
    pthread_mutex_lock(&g_instance_lock);
    while (g_building && request_same(g_building_key, key) &&
           pthread_cond_timedwait(&g_spare_made, &g_instance_lock, &limit) == 0)
        ;
    MvkConnection *s = nullptr;
    Spare *spare = spare_find(hi, key);
    if (spare && spare->shell->physical_device == physical && spare->shell->queue_count == queues) {
        s = spare->shell;
        spare->shell = nullptr;
        request_free(&spare->key);
    }
    pthread_mutex_unlock(&g_instance_lock);
    return s;
}

/* binds a spare device and the queue ids from the request. */
static void device_adopt(MvkConnection *c, MvkDecoder *d, uint64_t id, MvkConnection *s, uint32_t queues)
{
    c->device = s->device;
    c->physical_device = s->physical_device;
    c->timelines = s->timelines;
    c->host_import = s->host_import;
    c->import_alignment = s->import_alignment;
    c->video = s->video;
    c->fn = s->fn;
    bind(c, id, (uint64_t)(uintptr_t)c->device);
    for (uint32_t i = 0; i < queues; i++) {
        mvk_get_u32(d);
        mvk_get_u32(d);
        bind(c, mvk_get_u64(d), (uint64_t)(uintptr_t)s->queues[i].queue);
        c->queues[c->queue_count++] = s->queues[i];
    }
    free(s);
}

/* normalizes a device request by clearing physical device, device and queue ids. */
static bool device_request(Request *key, const MvkDecoder *d, uint32_t queues)
{
    size_t ids[2 + sizeof(((MvkConnection *)0)->queues) / sizeof(((MvkConnection *)0)->queues[0])];
    size_t n = 0;
    ids[n++] = 0;
    ids[n++] = 8;
    if (queues > sizeof(ids) / sizeof(ids[0]) - 2)
        return false;
    MvkDecoder scan = *d;
    for (uint32_t i = 0; i < queues; i++) {
        mvk_get_u32(&scan);
        mvk_get_u32(&scan);
        mvk_get_u64(&scan);
        ids[n++] = (size_t)(scan.p - 8 - scan.start);
    }
    return !scan.error && request_make(key, d, ids, n);
}

static uint64_t realtime_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static bool g_no_spare;
static pthread_mutex_t g_maker_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_maker_wake = PTHREAD_COND_INITIALIZER;
static uint64_t g_maker_at; /* when the next device is due, in realtime nanoseconds, 0 for none. */
static bool g_maker_stop;
static bool g_maker_quiet; /* defer spare creation until connections are quiet. */
static uint64_t g_vk_busy; /* when a connection last ran a call of the guest, in the host's nanoseconds. */

/*
 * creating a spare can serialize driver work with active devices. a WebGPU page opened one second after the
 * browser waited 70 ms instead of 6 for its device.
 */
#define QUIET_NS 10000000000ull
static uint64_t g_quiet_ns = QUIET_NS; /* overridden by MIME_VK_SPARE_QUIET_MS. */

static void maker_request(uint64_t delay, bool quiet)
{
    pthread_mutex_lock(&g_maker_lock);
    uint64_t at = realtime_ns() + delay;
    g_maker_quiet |= quiet;
    if (!g_maker_stop && (!g_maker_at || at < g_maker_at)) {
        g_maker_at = at;
        pthread_cond_signal(&g_maker_wake);
    }
    pthread_mutex_unlock(&g_maker_lock);
}

/* creates a spare for this request and instance unless one already exists. */
static void spare_make_on(const Learned *want, uint8_t *arena)
{
    MvkConnection owner = {};
    MvkDecoder d = {want->instance.bytes, want->instance.bytes, want->instance.bytes + want->instance.size, 0, &owner,
                    arena, 0, FILL_ARENA};
    mvk_get_u64(&d);
    VkInstanceCreateInfo iinfo;
    mvk_get_VkInstanceCreateInfo(&d, &iinfo);
    Request key = {};
    VkResult r = VK_ERROR_INITIALIZATION_FAILED;
    HostInstance *hi = nullptr;
    if (!d.error && request_copy(&key, &want->instance))
        hi = instance_hold(&key, iinfo, &d, &r);
    request_free(&key);
    if (!hi)
        return;
    /* the machine retains this instance reference for later connections and their spare devices. */
    bool pooled = false, placed = false;
    pthread_mutex_lock(&g_instance_lock);
    for (unsigned i = 0; i < SPARES_MAX; i++)
        pooled |= g_pool[i] == hi;
    for (unsigned i = 0; i < SPARES_MAX && !pooled && !placed; i++) {
        if (!g_pool[i]) {
            g_pool[i] = hi;
            placed = true;
        }
    }
    pthread_mutex_unlock(&g_instance_lock);
    if (!placed)
        instance_release(hi);
    if (!placed && !pooled)
        return;

    VkPhysicalDevice devices[MAX_PHYSICAL_DEVICES], physical = VK_NULL_HANDLE;
    uint32_t count = MAX_PHYSICAL_DEVICES;
    if (hi->base.fn.vkEnumeratePhysicalDevices(hi->base.instance, &count, devices) >= 0) {
        for (uint32_t i = 0; i < count && !physical; i++) {
            char uuids[UUID_TEXT];
            import_uuids(&hi->base, devices[i], uuids);
            if (!strcmp(uuids[0] ? uuids : "-", want->uuids))
                physical = devices[i];
        }
    }
    pthread_mutex_lock(&g_instance_lock);
    bool have = spare_find(hi, &want->device) != nullptr;
    bool room = false;
    for (unsigned i = 0; i < SPARES_MAX; i++)
        room |= !hi->spares[i].shell;
    pthread_mutex_unlock(&g_instance_lock);
    if (have || !room || !physical)
        return;

    MvkConnection *s = calloc(1, sizeof(*s));
    if (!s)
        return;
    s->library = hi->base.library;
    s->gipa = hi->base.gipa;
    s->instance = hi->base.instance;
    s->fn = hi->base.fn;
    d = (MvkDecoder){want->device.bytes, want->device.bytes, want->device.bytes + want->device.size, 0, s, arena, 0,
                     FILL_ARENA};
    mvk_get_u64(&d);
    mvk_get_u64(&d);
    VkDeviceCreateInfo dinfo;
    mvk_get_VkDeviceCreateInfo(&d, &dinfo);
    uint32_t queues = mvk_get_u32(&d);
    uint64_t began = mime_now_ns();
    r = d.error ? VK_ERROR_INITIALIZATION_FAILED : device_create(s, &d, physical, 0, dinfo, queues);
    uint64_t took = (mime_now_ns() - began) / 1000000;
    if (r == VK_SUCCESS && s->queue_count == queues) {
        pthread_mutex_lock(&g_instance_lock);
        for (unsigned i = 0; i < SPARES_MAX && s; i++) {
            if (!hi->spares[i].shell && request_copy(&hi->spares[i].key, &want->device)) {
                hi->spares[i].shell = s;
                hi->spares[i].made = mime_now_ns();
                s = nullptr;
            }
        }
        pthread_mutex_unlock(&g_instance_lock);
    }
    mime_info("vulkan: a device is made ahead, %llu ms, %s", (unsigned long long)took, s ? "not kept" : "kept");
    if (s) {
        if (s->device)
            shell_destroy(s);
        else
            free(s);
    }
}

/* a process that asks for the same device while it is being made waits for it. */
static void spare_make(const Learned *want, uint8_t *arena)
{
    pthread_mutex_lock(&g_instance_lock);
    g_building = true;
    g_building_key = &want->device;
    pthread_mutex_unlock(&g_instance_lock);
    spare_make_on(want, arena);
    pthread_mutex_lock(&g_instance_lock);
    g_building = false;
    pthread_cond_broadcast(&g_spare_made);
    pthread_mutex_unlock(&g_instance_lock);
}

/* discard spares outside the recent request list. */
static void spare_prune(const Learned *want, unsigned n)
{
    MvkConnection *doomed[SPARES_MAX * SPARES_MAX];
    unsigned count = 0;
    pthread_mutex_lock(&g_instance_lock);
    for (unsigned h = 0; h < SPARES_MAX; h++) {
        for (unsigned i = 0; g_pool[h] && i < SPARES_MAX; i++) {
            Spare *spare = &g_pool[h]->spares[i];
            bool keep = false;
            for (unsigned j = 0; j < n && spare->shell; j++)
                keep |= request_same(&spare->key, &want[j].device);
            if (spare->shell && !keep) {
                doomed[count++] = spare->shell;
                spare->shell = nullptr;
                request_free(&spare->key);
            }
        }
    }
    pthread_mutex_unlock(&g_instance_lock);
    for (unsigned i = 0; i < count; i++)
        shell_destroy(doomed[i]);
}

/* release idle spares; recreate them when a process makes a matching device. */
static void spare_expire(void)
{
    MvkConnection *doomed[SPARES_MAX * SPARES_MAX];
    unsigned count = 0;
    uint64_t now = mime_now_ns();
    pthread_mutex_lock(&g_instance_lock);
    for (unsigned h = 0; h < SPARES_MAX; h++) {
        for (unsigned i = 0; g_pool[h] && i < SPARES_MAX; i++) {
            Spare *spare = &g_pool[h]->spares[i];
            if (spare->shell && now - spare->made > SPARE_IDLE_NS) {
                doomed[count++] = spare->shell;
                spare->shell = nullptr;
                request_free(&spare->key);
            }
        }
    }
    pthread_mutex_unlock(&g_instance_lock);
    for (unsigned i = 0; i < count; i++)
        shell_destroy(doomed[i]);
}

/* prepare the first request immediately; wait for quiet guest connections before the others. */
static void spare_fill(bool quiet)
{
    Learned want[SPARES_MAX] = {};
    unsigned n = 0;
    pthread_mutex_lock(&g_learned_lock);
    for (; n < g_learned_count && n < SPARES_MAX; n++) {
        if (!request_copy(&want[n].instance, &g_learned[n].instance) ||
            !request_copy(&want[n].device, &g_learned[n].device))
            break;
        memcpy(want[n].uuids, g_learned[n].uuids, sizeof(want[n].uuids));
    }
    pthread_mutex_unlock(&g_learned_lock);
    uint8_t *arena = malloc(FILL_ARENA);
    if (arena) {
        spare_prune(want, n);
        for (unsigned i = 0; i < n && (quiet || !i); i++)
            spare_make(&want[i], arena);
    }
    free(arena);
    for (unsigned i = 0; i < SPARES_MAX; i++) {
        request_free(&want[i].instance);
        request_free(&want[i].device);
    }
}

/* remember the successful request and schedule a replacement spare if none remains. */
static void device_learn(MvkConnection *c, VkPhysicalDevice physical, const Request *key)
{
    HostInstance *hi = c->host_instance;
    if (!hi || !hi->key.size || !key->size)
        return;
    char uuids[UUID_TEXT];
    import_uuids(c, physical, uuids);
    learned_note(uuids[0] ? uuids : "-", &hi->key, key);
    pthread_mutex_lock(&g_instance_lock);
    bool missing = !spare_find(hi, key);
    pthread_mutex_unlock(&g_instance_lock);
    if (missing)
        maker_request(REFILL_DELAY_NS, true);
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
        Request key = {};
        HostInstance *hi = c->host_instance;
        bool known = !g_no_spare && device_request(&key, d, queues);
        uint64_t began = mime_now_ns();
        MvkConnection *spare = known && hi ? spare_take(hi, &key, physical, queues) : nullptr;
        if (spare) {
            device_adopt(c, d, id, spare, queues);
            result = VK_SUCCESS;
        } else {
            result = device_create(c, d, physical, id, info, queues);
        }
        mime_info("vulkan: connection %u has its device after %llu us, %s", c->index,
                  (unsigned long long)((mime_now_ns() - began) / 1000), spare ? "made ahead" : "made now");
        if (result == VK_SUCCESS && known)
            device_learn(c, physical, &key);
        request_free(&key);
    }
    mvk_put_u32(e, (uint32_t)result);
    mvk_reply_end(call);
}

/* destroy presenter swapchains before their device and its function pointers become invalid. */
static void destroy_device(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint64_t id = mvk_get_u64(&call->in);
    if (call->in.error || !c->device || lookup(c, id) != (uint64_t)(uintptr_t)c->device)
        return;
    mvk_wsi_close(c);
    stop_watcher(c);
    c->fn.vkDeviceWaitIdle(c->device);
    mvk_video_close(c);
    destroy_leftovers(c);
    mvk_budget_forget(c->device);
    c->fn.vkDestroyDevice(c->device, nullptr);
    bind(c, id, 0);
    c->device = VK_NULL_HANDLE;
    c->physical_device = VK_NULL_HANDLE;
    c->queue_count = 0;
}

/* ------------------------------------------------------------------ memory. */

#ifndef MADV_POPULATE_WRITE
#define MADV_POPULATE_WRITE 23
#endif
#define PREFAULT_MIN (8ull << 20)
#define PREFAULT_THREADS 4
#define PREFAULT_STEP (4ull << 20)

typedef struct Prefault {
    uint8_t *start;
    size_t length;
} Prefault;

/* populate a few MiB per call to avoid blocking guest page faults on the memory map lock. */
static void *prefault_main(void *arg)
{
    const Prefault *p = arg;
    for (size_t at = 0; at < p->length; at += PREFAULT_STEP)
        if (madvise(p->start + at, MIME_MIN((size_t)PREFAULT_STEP, p->length - at), MADV_POPULATE_WRITE) != 0)
            break;
    return nullptr;
}

/*
 * the driver pins imported pages, faulting untouched ones serially: 25 ms for 64 MiB, up to 100 ms in the
 * machine while other guest faults wait. parallel prefaulting takes 5 ms. these pages would be pinned anyway,
 * so it uses no extra memory.
 */
static void prefault(void *host, uint64_t span)
{
    if (span < PREFAULT_MIN)
        return;
    uint64_t share = MIME_ALIGN_UP(span / PREFAULT_THREADS, 2ull << 20);
    Prefault parts[PREFAULT_THREADS];
    pthread_t threads[PREFAULT_THREADS];
    bool started[PREFAULT_THREADS] = {};
    uint32_t count = 0;
    for (uint64_t at = 0; at < span; at += share)
        parts[count++] = (Prefault){(uint8_t *)host + at, (size_t)MIME_MIN(share, span - at)};
    for (uint32_t i = 1; i < count; i++)
        started[i] = pthread_create(&threads[i], nullptr, prefault_main, &parts[i]) == 0;
    prefault_main(&parts[0]);
    for (uint32_t i = 1; i < count; i++) {
        if (started[i])
            pthread_join(threads[i], nullptr);
        else
            prefault_main(&parts[i]);
    }
}

/* mapped guest memory is contiguous guest RAM imported by the host; other allocations belong to the driver. */
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
                prefault(host, span);
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

/* ------------------------------------------------------------------ command buffers. */

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
 * records begin information and command chunks from the guest stream. copy chunks before decoding because the
 * guest may modify them.
 */
/*
 * copy command streams into a recording worker pool with one arena per worker. recordings from the same
 * command pool stay serial as Vulkan requires. submissions, resets, frees and allocations wait for all
 * earlier recordings. small streams, secondary command buffers and streams executing secondary buffers stay
 * on the connection thread.
 */
typedef struct MvkRecord {
    struct MvkRecord *queued; /* worker pool list. */
    struct MvkRecord *next;   /* connection list, newest first. */
    struct MvkRecord *after;  /* wait for the previous recording from this command pool. */
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

/* waits for and releases all queued recordings. */
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

/* detects secondary command buffer execution, which requires prior recording. */
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

/* ------------------------------------------------------------------ queues and synchronization. */

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
            mvk_unwatch(c, (VkSemaphore)handle);
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

/* descriptor bytes pointed to by the guest union. */
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

/* ------------------------------------------------------------------ descriptors and queries. */

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

/* ------------------------------------------------------------------ pipelines and shaders created aside. */

/*
 * pipeline compilation takes milliseconds; shader creation takes 1 to 2 ms each. executing them in ring order
 * delayed a browser first frame by 70 ms across dozens of shaders. decode on the connection thread so handle
 * lookups stay ordered, then run driver calls in a worker pool. bind ids on completion; lookups wait only for
 * the needed creation. destruction waits for all earlier creations that may use the object.
 */
typedef struct MvkJob {
    struct MvkJob *next, *prev; /* connection list in call order. */
    struct MvkJob *queued; /* worker pool list. */
    MvkConnection *conn;
    uint32_t op;
    VkDevice device;
    VkPipelineCache cache;
    uint32_t count;
    void *infos;
    uint64_t *ids;
    VkPipeline *pipelines; /* shader handles from one creation, all the same size. */
    bool libraries; /* linked pipeline libraries must survive until creation finishes. */
    VkResult result;
    volatile int done;
    int users;    /* recording waiters, updated under job_lock; the last waiter frees it. */
    bool retired; /* finished and unlinked; the last user frees it. */
    uint8_t *memory; /* copied call followed by its decoding arena. */
} MvkJob;

/* worker driver time since the last dump: shaders, graphics pipelines and compute pipelines. */
static struct {
    uint64_t creations, ns;
} g_job_stats[3];

static pthread_mutex_t g_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_pool_work = PTHREAD_COND_INITIALIZER;
static MvkJob *g_pool_first, *g_pool_last;
static uint32_t g_pool_threads;

/*
 * MIME_VK_DUMP_SPIRV=<dir> keeps every shader module a guest makes, named by its handle, and writes the modules of each
 * pipeline to jobs.log before the driver compiles it, so a driver crash names its shaders.
 */
static const char *g_spirv_dump;
static PFN_vkCreateShaderModule g_create_module;
static uint32_t g_dump_serial;

static void dump_words(const char *name, const void *code, size_t size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.spv", g_spirv_dump, name);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(code, 1, size, f);
        fclose(f);
    }
}

static VkResult VKAPI_CALL dump_create_module(VkDevice device, const VkShaderModuleCreateInfo *info,
                                              const VkAllocationCallbacks *allocator, VkShaderModule *module)
{
    VkResult result = g_create_module(device, info, allocator, module);
    if (result == VK_SUCCESS) {
        char name[32];
        snprintf(name, sizeof(name), "m%016llx", (unsigned long long)*module);
        dump_words(name, info->pCode, info->codeSize);
    }
    return result;
}

static void dump_hook(MvkConnection *c)
{
    if (!g_spirv_dump)
        g_spirv_dump = getenv("MIME_VK_DUMP_SPIRV");
    if (!g_spirv_dump || !c->fn.vkCreateShaderModule || c->fn.vkCreateShaderModule == dump_create_module)
        return;
    g_create_module = c->fn.vkCreateShaderModule;
    c->fn.vkCreateShaderModule = dump_create_module;
}

static void dump_stage(FILE *log, uint32_t job, uint32_t index, const VkPipelineShaderStageCreateInfo *stage)
{
    if (stage->module) {
        fprintf(log, " stage %#x m%016llx", stage->stage, (unsigned long long)stage->module);
        return;
    }
    for (const VkBaseInStructure *next = stage->pNext; next; next = next->pNext)
        if (next->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO) {
            const VkShaderModuleCreateInfo *inline_module = (const VkShaderModuleCreateInfo *)next;
            char name[32];
            snprintf(name, sizeof(name), "j%u-%u", job, index);
            dump_words(name, inline_module->pCode, inline_module->codeSize);
            fprintf(log, " stage %#x %s", stage->stage, name);
        }
}

static void dump_job(const MvkJob *job, bool finished)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/jobs.log", g_spirv_dump);
    FILE *log = fopen(path, "a");
    if (!log)
        return;
    uint32_t serial = __atomic_fetch_add(&g_dump_serial, 1, __ATOMIC_RELAXED);
    if (finished) {
        fprintf(log, "done %s of %u: %d\n", mvk_generated_names[job->op], job->count, job->result);
        fclose(log);
        return;
    }
    fprintf(log, "start %s of %u:", mvk_generated_names[job->op], job->count);
    uint32_t index = 0;
    for (uint32_t i = 0; i < job->count; i++) {
        if (job->op == MVK_OP_vkCreateGraphicsPipelines) {
            const VkGraphicsPipelineCreateInfo *info = (const VkGraphicsPipelineCreateInfo *)job->infos + i;
            for (uint32_t k = 0; k < info->stageCount; k++)
                dump_stage(log, serial, index++, &info->pStages[k]);
        } else if (job->op == MVK_OP_vkCreateComputePipelines) {
            dump_stage(log, serial, index++, &((const VkComputePipelineCreateInfo *)job->infos + i)->stage);
        } else {
            const VkShaderCreateInfoEXT *info = (const VkShaderCreateInfoEXT *)job->infos + i;
            char name[32];
            snprintf(name, sizeof(name), "j%u-%u", serial, index++);
            dump_words(name, info->pCode, info->codeSize);
            fprintf(log, " shader %#x %s", info->stage, name);
        }
        fprintf(log, " |");
    }
    fprintf(log, "\n");
    fclose(log);
}

/* runs creation on a worker, or on a caller that needs the result before a worker starts. */
static void run_job(MvkJob *job)
{
    {
        const MvkDispatch *fn = &job->conn->fn;
        if (g_spirv_dump)
            dump_job(job, false);
        uint64_t began = mime_now_ns();
        if (job->op == MVK_OP_vkCreateShadersEXT)
            job->result = fn->vkCreateShadersEXT(job->device, job->count, job->infos, nullptr, (VkShaderEXT *)job->pipelines);
        else if (job->op == MVK_OP_vkCreateGraphicsPipelines)
            job->result = fn->vkCreateGraphicsPipelines(job->device, job->cache, job->count, job->infos, nullptr,
                                                        job->pipelines);
        else
            job->result = fn->vkCreateComputePipelines(job->device, job->cache, job->count, job->infos, nullptr,
                                                       job->pipelines);
        if (g_spirv_dump)
            dump_job(job, true);
        MvkConnection *c = job->conn;
        uint64_t took = mime_now_ns() - began;
        int kind = job->op == MVK_OP_vkCreateShadersEXT ? 0 : job->op == MVK_OP_vkCreateGraphicsPipelines ? 1 : 2;
        __atomic_fetch_add(&g_job_stats[kind].creations, job->count, __ATOMIC_RELAXED);
        __atomic_fetch_add(&g_job_stats[kind].ns, took, __ATOMIC_RELAXED);
        if (took > 2000000)
            mime_debug("vulkan: connection %u: %s of %u took %llu us", c->index, mvk_generated_names[job->op], job->count,
                       (unsigned long long)(took / 1000));
        pthread_mutex_lock(&c->job_lock);
        __atomic_store_n(&job->done, 1, __ATOMIC_RELEASE);
        __atomic_add_fetch(&c->jobs_finished, 1, __ATOMIC_RELEASE);
        pthread_cond_broadcast(&c->job_done);
        pthread_mutex_unlock(&c->job_lock);
    }
}

/* claims an unstarted creation for the waiting caller. */
static bool take_job(MvkJob *job)
{
    bool found = false;
    pthread_mutex_lock(&g_pool_lock);
    MvkJob *previous = nullptr;
    for (MvkJob *j = g_pool_first; j; previous = j, j = j->queued) {
        if (j != job)
            continue;
        if (previous)
            previous->queued = j->queued;
        else
            g_pool_first = j->queued;
        if (g_pool_last == j)
            g_pool_last = previous;
        found = true;
        break;
    }
    pthread_mutex_unlock(&g_pool_lock);
    return found;
}

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
        run_job(job);
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

/* binds completed ids and releases the creation. */
static void finish_job(MvkConnection *c, MvkJob *job)
{
    /* bind before unlinking so recording workers always find the handle or its pending creation. */
    for (uint32_t i = 0; i < job->count; i++) {
        bind(c, job->ids[i], (uint64_t)job->pipelines[i]);
        mvk_track(c, (uint64_t)job->pipelines[i], job->op == MVK_OP_vkCreateShadersEXT ? MVK_KIND_VkShaderEXT : MVK_KIND_VkPipeline);
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
    bool in_use = job->users > 0;
    job->retired = in_use;
    pthread_mutex_unlock(&c->job_lock);
    if (job->result != VK_SUCCESS)
        mime_warn("vulkan: %s failed: %d", mvk_generated_names[job->op], job->result);
    if (!in_use) {
        free(job->memory);
        free(job);
    }
}

static void wait_job(MvkConnection *c, MvkJob *job)
{
    if (!__atomic_load_n(&job->done, __ATOMIC_ACQUIRE) && take_job(job))
        run_job(job);
    pthread_mutex_lock(&c->job_lock);
    while (!__atomic_load_n(&job->done, __ATOMIC_ACQUIRE))
        pthread_cond_wait(&c->job_done, &c->job_lock);
    pthread_mutex_unlock(&c->job_lock);
    finish_job(c, job);
}

/* binds all completed creations. */
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

/* recording workers wait for the requested creation or its completed handle binding. */
static uint64_t await_id_aside(MvkConnection *c, uint64_t id)
{
    uint64_t handle = 0;
    pthread_mutex_lock(&c->job_lock);
    for (MvkJob *job = c->jobs; job && !handle; job = job->next)
        for (uint32_t i = 0; i < job->count; i++)
            if (job->ids[i] == id) {
                job->users++;
                if (!__atomic_load_n(&job->done, __ATOMIC_ACQUIRE)) {
                    /* run an unstarted job here, releasing the lock workers need to report completion. */
                    pthread_mutex_unlock(&c->job_lock);
                    if (take_job(job))
                        run_job(job);
                    pthread_mutex_lock(&c->job_lock);
                }
                while (!__atomic_load_n(&job->done, __ATOMIC_ACQUIRE))
                    pthread_cond_wait(&c->job_done, &c->job_lock);
                handle = (uint64_t)job->pipelines[i];
                if (--job->users == 0 && job->retired) {
                    /* the connection finished this job during use; the last user frees it. */
                    pthread_mutex_unlock(&c->job_lock);
                    free(job->memory);
                    free(job);
                    return handle ? handle : table_get(c, id);
                }
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

/* vkCreateGraphicsPipelines, vkCreateComputePipelines and vkCreateShadersEXT, decoded here and run by the pool. */
static void create_pipelines_aside(MvkCall *call)
{
    MvkConnection *c = call->conn;
    uint32_t op = call->header.op;
    bool graphics = op == MVK_OP_vkCreateGraphicsPipelines, shaders = op == MVK_OP_vkCreateShadersEXT;
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
    if (!shaders)
        job->cache = (VkPipelineCache)mvk_get_handle(&d, 0);
    job->count = mvk_get_u32(&d);
    size_t info_size = shaders ? sizeof(VkShaderCreateInfoEXT)
                       : graphics ? sizeof(VkGraphicsPipelineCreateInfo) : sizeof(VkComputePipelineCreateInfo);
    if (mvk_get_u32(&d)) {
        job->infos = mvk_alloc(&d, (size_t)job->count * info_size);
        for (uint32_t i = 0; job->infos && i < job->count; i++) {
            if (shaders) {
                mvk_get_VkShaderCreateInfoEXT(&d, &((VkShaderCreateInfoEXT *)job->infos)[i]);
            } else if (graphics) {
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
    if (shaders ? !c->fn.vkCreateShadersEXT : graphics ? !c->fn.vkCreateGraphicsPipelines : !c->fn.vkCreateComputePipelines) {
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

/* wait for pending creations before destroying resources they may use. */
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

/* ------------------------------------------------------------------ watched semaphores. */

/*
 * waiting on the connection thread would block all process calls. the guest first polls timeline values, then
 * registers waits after a timeout. a watcher waits on those values and a wake semaphore for new
 * registrations. it increments header events and raises the interrupt for completed waits; the guest wakes
 * and polls again.
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
 * mirror timeline values in a header slot so the guest can read them without calls. update on advancement;
 * the guest reuses a slot only after the connection processes semaphore destruction.
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

/* importing another device semaphore payload invalidates the cached value; refresh both mirror and watch. */
void mvk_mirror_refresh(MvkConnection *c, VkSemaphore semaphore)
{
    if (!c->watching)
        return;
    pthread_mutex_lock(&c->watch_lock);
    uint64_t value = 0;
    if (c->fn.vkGetSemaphoreCounterValue(c->device, semaphore, &value) == VK_SUCCESS) {
        for (uint32_t i = 0; i < c->watch_count; i++) {
            if (c->watches[i].semaphore == semaphore && c->watches[i].slot >= 0) {
                __atomic_store_n(&c->header->mirror[c->watches[i].slot], value, __ATOMIC_RELEASE);
                c->watches[i].value = value + 1;
            }
        }
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
void mvk_unwatch(MvkConnection *c, VkSemaphore semaphore)
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

/* ------------------------------------------------------------------ running calls. */

static void run_hand(MvkCall *call)
{
    switch (call->header.op) {
    case MVK_OP_INSTANCE_EXTENSIONS: instance_extensions(call); break;
    case MVK_OP_CREATE_INSTANCE: create_instance(call); break;
    case MVK_OP_DESTROY_INSTANCE: break; /* the connection outlives it, and goes with the process. */
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
    case MVK_OP_HDR_METADATA: mvk_wsi_hdr_metadata(call); break;
    case MVK_OP_GET_DESCRIPTOR: get_descriptor(call); break;
    case MVK_OP_WATCH_SEMAPHORES: watch_semaphores(call); break;
    case MVK_OP_MIRROR_SEMAPHORE: mirror_semaphore(call); break;
    case MVK_OP_VIDEO_CAPS: mvk_video_caps(call); break;
    case MVK_OP_VIDEO_CREATE: mvk_video_create(call); break;
    case MVK_OP_VIDEO_DESTROY: mvk_video_destroy(call); break;
    case MVK_OP_VIDEO_DECODE: mvk_video_decode(call); break;
    case MVK_OP_SHARE_EXPORT_MEMORY: mvk_share_export_memory(call); break;
    case MVK_OP_SHARE_IMPORT_MEMORY: mvk_share_import_memory(call); break;
    case MVK_OP_SHARE_EXPORT_SEMAPHORE: mvk_share_export_semaphore(call); break;
    case MVK_OP_SHARE_IMPORT_SEMAPHORE: mvk_share_import_semaphore(call); break;
    default:
        mime_warn("vulkan: unknown call %#x", call->header.op);
        break;
    }
}

static struct {
    uint64_t calls, ns, replies;
} g_op_stats[MVK_OP_GENERATED_COUNT + 256];

static void run_call(MvkCall *call)
{
    uint32_t op = call->header.op;
    MvkConnection *c = call->conn;
    uint64_t began = mime_now_ns();
    mvk_video_enter(c);
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
    /* MIME_VK_SYNC_SHADERS=1 serializes shader creation in call order for comparison with worker execution. */
    static int sync_shaders = -1;
    if (sync_shaders < 0)
        sync_shaders = getenv("MIME_VK_SYNC_SHADERS") != nullptr;
    if (op == MVK_OP_vkCreateGraphicsPipelines || op == MVK_OP_vkCreateComputePipelines ||
        (op == MVK_OP_vkCreateShadersEXT && !sync_shaders))
        create_pipelines_aside(call);
    else if (op > MVK_OP_NONE && op < MVK_OP_GENERATED_COUNT && mvk_generated_commands[op])
        mvk_generated_commands[op](call);
    else if (op >= MVK_OP_HAND && op < MVK_OP_HAND_END)
        run_hand(call);
    else
        mime_warn("vulkan: unknown call %#x", op);
    if (call->in.error)
        mime_warn("vulkan: malformed call %#x", op);
    uint64_t took = mime_now_ns() - began;
    /* per-call host time for the machine dump; replies also cost a guest round trip. */
    uint32_t slot = op >= MVK_OP_HAND ? MVK_OP_GENERATED_COUNT + (op - MVK_OP_HAND) : op;
    if (slot < sizeof(g_op_stats) / sizeof(g_op_stats[0])) {
        __atomic_fetch_add(&g_op_stats[slot].calls, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&g_op_stats[slot].ns, took, __ATOMIC_RELAXED);
        if (call->header.reply != MVK_NO_REPLY)
            __atomic_fetch_add(&g_op_stats[slot].replies, 1, __ATOMIC_RELAXED);
    }
    /* slow driver calls for MIME_DEBUG. */
    if (took > 2000000)
        mime_debug("vulkan: connection %u call %#x took %llu us", c->index, op, (unsigned long long)(took / 1000));
}

/* dumps call counts and durations, then resets them. */
void mime_vk_dump_stats(void)
{
    static const char *const job_names[3] = {"shader objects", "graphics pipelines", "compute pipelines"};
    for (int i = 0; i < 3; i++) {
        if (g_job_stats[i].creations)
            mime_info("vulkan: %llu %s made aside, %llu ms in the driver", (unsigned long long)g_job_stats[i].creations,
                      job_names[i], (unsigned long long)(g_job_stats[i].ns / 1000000));
        g_job_stats[i].creations = g_job_stats[i].ns = 0;
    }
    uint64_t total_calls = 0, total_ns = 0, total_replies = 0;
    for (size_t i = 0; i < sizeof(g_op_stats) / sizeof(g_op_stats[0]); i++) {
        total_calls += g_op_stats[i].calls;
        total_ns += g_op_stats[i].ns;
        total_replies += g_op_stats[i].replies;
    }
    if (!total_calls)
        return;
    mime_info("vulkan: %llu calls, %llu of them waited for by the guest, %llu ms in the host", (unsigned long long)total_calls,
              (unsigned long long)total_replies, (unsigned long long)(total_ns / 1000000));
    for (int round = 0; round < 40; round++) {
        size_t best = 0;
        uint64_t most = 0;
        for (size_t i = 0; i < sizeof(g_op_stats) / sizeof(g_op_stats[0]); i++)
            if (g_op_stats[i].ns > most)
                most = g_op_stats[i].ns, best = i;
        if (!most)
            break;
        const char *name = best < MVK_OP_GENERATED_COUNT ? mvk_generated_names[best] : "hand-written";
        mime_info("vulkan: %s (%zu): %llu calls, %llu with a reply, %llu ms", name ? name : "?", best,
                  (unsigned long long)g_op_stats[best].calls, (unsigned long long)g_op_stats[best].replies,
                  (unsigned long long)(g_op_stats[best].ns / 1000000));
        g_op_stats[best].ns = 0;
    }
    memset(g_op_stats, 0, sizeof(g_op_stats));
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
        /* stop recording after a decode failure: later commands may depend on it and crash the driver. */
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
            /* drain queued calls, including final destructions, before closing. */
            if (c->closing)
                break;
            /* a short spin catches the next call of a busy guest without a kick. */
            uint64_t until = mime_now_ns() + 50000;
            while (put == get && mime_now_ns() < until) {
#if defined(__x86_64__)
                __builtin_ia32_pause();
#else
                __asm__ volatile("yield");
#endif
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
            __atomic_store_n(&g_vk_busy, mime_now_ns(), __ATOMIC_RELAXED);
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

/* ------------------------------------------------------------------ hypercalls. */

/* ------------------------------------------------------------------ the host's own device. */

/*
 * OpenGL channels use a host-owned connection without a guest ring. its instance supports presenter surfaces;
 * the caller creates the required device.
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
    const char *names[12];
    uint32_t n = 0;
    for (const char *const *x = mvk_wsi_instance_extensions; *x && n < 8; x++)
        if (host_has(list, count, *x))
            names[n++] = *x;
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "mime", 1, "mime", 1, api};
    VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app, 0, nullptr, 0, names};
    info.enabledLayerCount = 0;
    info.ppEnabledLayerNames = nullptr;
    info.enabledExtensionCount = n;
    free(list);
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)c->gipa(nullptr, "vkCreateInstance");
    if (create(&info, nullptr, &c->instance) != VK_SUCCESS)
        return false;
    mvk_load_dispatch(&c->fn, c->gipa, c->instance, nullptr, nullptr);
    mvk_budget_hook(c);
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
    mvk_budget_hook(c);
    dump_hook(c);
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

/* when the warm up of the driver began and ended, in the host's nanoseconds. */
static uint64_t g_warm_began, g_warm_done;

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
    mime_info("vulkan: a connection opens %llu ms after the warm up began, which is %s",
              (unsigned long long)((mime_now_ns() - __atomic_load_n(&g_warm_began, __ATOMIC_ACQUIRE)) / 1000000), __atomic_load_n(&g_warm_done, __ATOMIC_ACQUIRE) ? "done" : "not done");
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

static pthread_mutex_t g_teardown_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * returns once the calls every guest process posted before it have run and the presents among them are shown or kept.
 * the host runs a connection's calls and its presents after the guest went on, so a read of what a window presented
 * would give the frame before the last one. the window manager of Windows syncs the same way for a capture of the pixels
 * of a window, with DwmSyncCaptureSurfaceBits. a connection that does not catch up in a short while is left alone.
 */
void mime_vk_sync(void)
{
    MvkConnection *list[MAX_CONNECTIONS];
    uint64_t put[MAX_CONNECTIONS];
    uint32_t count = 0;
    pthread_mutex_lock(&g_lock);
    for (uint32_t i = 1; i < MAX_CONNECTIONS; i++) {
        MvkConnection *c = g_connections[i];
        if (!c || !c->header)
            continue;
        __atomic_add_fetch(&c->syncing, 1, __ATOMIC_ACQ_REL);
        list[count] = c;
        put[count++] = __atomic_load_n(&c->header->put, __ATOMIC_ACQUIRE);
    }
    pthread_mutex_unlock(&g_lock);
    uint64_t until = mime_now_ns() + 500000000;
    for (uint32_t i = 0; i < count; i++) {
        MvkConnection *c = list[i];
        MvkHeader *h = c->header;
        if (__atomic_load_n(&h->get, __ATOMIC_ACQUIRE) < put[i]) {
            pthread_mutex_lock(&c->lock);
            c->kicked = true;
            pthread_cond_signal(&c->cond);
            pthread_mutex_unlock(&c->lock);
        }
        while (__atomic_load_n(&h->get, __ATOMIC_ACQUIRE) < put[i] && !c->closing &&
               !__atomic_load_n(&h->lost, __ATOMIC_ACQUIRE) && mime_now_ns() < until) {
            struct timespec pause = {0, 100000};
            nanosleep(&pause, nullptr);
        }
        mvk_wsi_drain(c);
        __atomic_sub_fetch(&c->syncing, 1, __ATOMIC_ACQ_REL);
    }
}

void mime_gpu_teardown_enter(void)
{
    pthread_mutex_lock(&g_teardown_lock);
}

void mime_gpu_teardown_leave(void)
{
    pthread_mutex_unlock(&g_teardown_lock);
}

/* returns once the host and the GPU no longer use the connection's memory, as the guest frees it next. */
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
    /* a read of a window that sync waits for the connection is done with it first. */
    while (__atomic_load_n(&c->syncing, __ATOMIC_ACQUIRE)) {
        struct timespec pause = {0, 100000};
        nanosleep(&pause, nullptr);
    }
    pthread_join(c->thread, nullptr);
    drain_records(c);
    drain_jobs(c);
    mvk_wsi_close(c);
    if (c->device)
        stop_watcher(c);
    if (c->device) {
        c->fn.vkDeviceWaitIdle(c->device);
        mvk_video_close(c);
        destroy_leftovers(c);
        mime_gpu_teardown_enter();
        mvk_budget_forget(c->device);
        c->fn.vkDestroyDevice(c->device, nullptr);
        mime_gpu_teardown_leave();
    }
    free(c->live);
    if (c->host_instance)
        instance_release(c->host_instance);
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

static void warm_wait(void);

/* ------------------------------------------------------------------ the gpus the machine reports */

#define GPU_LIST_NAME "gpus"
#define GPU_LIST_HEADER "mime gpus 1"

static int gpu_rank(VkPhysicalDeviceType type)
{
    return type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 0 : type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : 2;
}

/* dxgi's adapters: no cpu drivers nor ones below vulkan 1.3, discrete ones first, vulkan's order within a kind. */
static uint32_t gpu_list(PFN_vkGetInstanceProcAddr gipa, VkInstance instance, MimeBootGpu *gpus, uint32_t max)
{
    PFN_vkEnumeratePhysicalDevices enumerate =
        (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties properties =
        (PFN_vkGetPhysicalDeviceProperties)gipa(instance, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceMemoryProperties memory =
        (PFN_vkGetPhysicalDeviceMemoryProperties)gipa(instance, "vkGetPhysicalDeviceMemoryProperties");
    VkPhysicalDevice devices[MAX_PHYSICAL_DEVICES];
    uint32_t count = MAX_PHYSICAL_DEVICES, n = 0;
    int ranks[MIME_BOOT_GPUS];
    if (!enumerate || !properties || !memory || enumerate(instance, &count, devices) < 0)
        return 0;
    for (uint32_t i = 0; i < count && n < max; i++) {
        VkPhysicalDeviceProperties p;
        properties(devices[i], &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU || p.apiVersion < VK_API_VERSION_1_3)
            continue;
        MimeBootGpu gpu = {};
        mime_strlcpy(gpu.name, p.deviceName, sizeof(gpu.name));
        gpu.vendor = p.vendorID;
        gpu.device = p.deviceID;
        gpu.driver_version = p.driverVersion;
        gpu.integrated = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        /* like dxgi's adapters, where an integrated gpu's memory is shared system memory. */
        VkPhysicalDeviceMemoryProperties m;
        memory(devices[i], &m);
        for (uint32_t h = 0; h < m.memoryHeapCount; h++)
            if (m.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                gpu.memory += m.memoryHeaps[h].size;
        if (gpu.integrated)
            gpu.memory = 128ull << 20;
        int rank = gpu_rank(p.deviceType);
        uint32_t at = n++;
        while (at && ranks[at - 1] > rank) {
            gpus[at] = gpus[at - 1];
            ranks[at] = ranks[at - 1];
            at--;
        }
        gpus[at] = gpu;
        ranks[at] = rank;
    }
    return n;
}

static void gpu_list_path(const MimePaths *paths, char *out, size_t size)
{
    snprintf(out, size, "%s/%s", paths ? paths->cache : "", GPU_LIST_NAME);
}

static void gpu_list_save(const MimePaths *paths, const MimeBootGpu *gpus, uint32_t count)
{
    char path[MIME_PATH_MAX], part[MIME_PATH_MAX + 16];
    gpu_list_path(paths, path, sizeof(path));
    snprintf(part, sizeof(part), "%s.%d", path, (int)getpid());
    FILE *f = fopen(part, "w");
    if (!f)
        return;
    fprintf(f, "%s\n", GPU_LIST_HEADER);
    for (uint32_t i = 0; i < count; i++)
        fprintf(f, "%x %x %x %u %llu %s\n", gpus[i].vendor, gpus[i].device, gpus[i].driver_version,
                gpus[i].integrated, (unsigned long long)gpus[i].memory, gpus[i].name);
    if (fclose(f) != 0 || rename(part, path) != 0)
        unlink(part);
}

static bool gpu_list_load(const MimePaths *paths, MimeBootGpu *gpus, uint32_t max, uint32_t *count)
{
    char path[MIME_PATH_MAX], line[512];
    gpu_list_path(paths, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    bool valid = fgets(line, sizeof(line), f) && !strncmp(line, GPU_LIST_HEADER, strlen(GPU_LIST_HEADER));
    *count = 0;
    while (valid && *count < max && fgets(line, sizeof(line), f)) {
        MimeBootGpu gpu = {};
        unsigned long long memory;
        int name_at = 0;
        if (sscanf(line, "%x %x %x %u %llu %n", &gpu.vendor, &gpu.device, &gpu.driver_version, &gpu.integrated,
                   &memory, &name_at) < 5 || !name_at)
            continue;
        line[strcspn(line, "\n")] = 0;
        mime_strlcpy(gpu.name, line + name_at, sizeof(gpu.name));
        gpu.memory = memory;
        gpus[(*count)++] = gpu;
    }
    fclose(f);
    return valid;
}

/*
 * the gpus as the last start found them, kept so a boot does not wait for the driver; the warm up refreshes the list
 * for the next one. only the first start asks vulkan before the guest boots.
 */
uint32_t mime_vk_gpus(const MimePaths *paths, MimeBootGpu *gpus, uint32_t max)
{
    uint32_t count = 0;
    if (gpu_list_load(paths, gpus, max, &count))
        return count;
    MvkConnection probe = {};
    if (!load_library(&probe))
        return 0;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)probe.gipa(nullptr, "vkCreateInstance");
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "mime", 1, "mime", 1, VK_API_VERSION_1_1};
    VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app};
    VkInstance instance;
    if (!create || create(&info, nullptr, &instance) != VK_SUCCESS)
        return 0;
    count = gpu_list(probe.gipa, instance, gpus, max);
    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)probe.gipa(instance, "vkDestroyInstance");
    if (destroy)
        destroy(instance, nullptr);
    gpu_list_save(paths, gpus, count);
    return count;
}

void mime_vk_close_all(void)
{
    warm_wait();
    /* save requests even if shutdown precedes the background write. */
    learned_save();
    for (uint64_t i = 0; i < MAX_CONNECTIONS; i++)
        mime_vk_close(i);
    pool_release();
}

/*
 * load the Vulkan loader and driver during boot, taking 120 ms, then query importable memory through a
 * temporary device, taking 70 ms. retain an instance so closing the last guest connection does not unload the
 * driver.
 */
static void *warm_run(void)
{
    uint64_t began = mime_now_ns();
    __atomic_store_n(&g_warm_began, began, __ATOMIC_RELEASE);
    MvkConnection warm = {};
    if (!mvk_internal_instance(&warm, VK_API_VERSION_1_1))
        return nullptr;
    VkPhysicalDevice devices[MAX_PHYSICAL_DEVICES];
    uint32_t count = MAX_PHYSICAL_DEVICES;
    if (warm.fn.vkEnumeratePhysicalDevices(warm.instance, &count, devices) >= 0)
        for (uint32_t i = 0; i < count; i++)
            importable_types(&warm, devices[i]);
    MimeBootGpu gpus[MIME_BOOT_GPUS];
    gpu_list_save(mime_daemon_paths(), gpus, gpu_list(warm.gipa, warm.instance, gpus, MIME_BOOT_GPUS));
    /* retain the instance to keep the driver loaded. */
    uint64_t done = mime_now_ns();
    __atomic_store_n(&g_warm_done, done, __ATOMIC_RELEASE);
    mime_info("vulkan: the driver is warm after %llu ms", (unsigned long long)((done - began) / 1000000));
    /* prepare the last run's device after driver loading and replenish it after use. */
    if (getenv("MIME_VK_SPARE_QUIET_MS"))
        g_quiet_ns = strtoull(getenv("MIME_VK_SPARE_QUIET_MS"), nullptr, 10) * 1000000ull;
    if (getenv("MIME_VK_NO_SPARE")) {
        /* disable device reuse to isolate device-specific problems. */
        g_no_spare = true;
        g_maker_stop = true;
    } else {
        learned_load();
        maker_request(0, false);
    }
    for (;;) {
        pthread_mutex_lock(&g_maker_lock);
        /* a minute at most without a request, to give back the devices nobody took. */
        uint64_t idle_until = realtime_ns() + 60000000000ull;
        while (!g_maker_stop && !g_maker_at && realtime_ns() < idle_until) {
            struct timespec until = {(time_t)(idle_until / 1000000000ull), (long)(idle_until % 1000000000ull)};
            pthread_cond_timedwait(&g_maker_wake, &g_maker_lock, &until);
        }
        while (!g_maker_stop && g_maker_at > realtime_ns()) {
            struct timespec until = {(time_t)(g_maker_at / 1000000000ull), (long)(g_maker_at % 1000000000ull)};
            pthread_cond_timedwait(&g_maker_wake, &g_maker_lock, &until);
        }
        bool stop = g_maker_stop, fill = g_maker_at != 0, quiet = g_maker_quiet;
        g_maker_at = 0;
        pthread_mutex_unlock(&g_maker_lock);
        if (stop)
            break;
        if (fill) {
            learned_save();
            if (quiet && mime_now_ns() - __atomic_load_n(&g_vk_busy, __ATOMIC_RELAXED) < g_quiet_ns) {
                /* the guest is calling the driver: asked again in a second. */
                maker_request(1000000000ull, true);
            } else {
                pthread_mutex_lock(&g_maker_lock);
                g_maker_quiet = false;
                pthread_mutex_unlock(&g_maker_lock);
                spare_fill(quiet);
                /* a second device is for the next program, and made when no program is starting. */
                if (!quiet)
                    maker_request(REFILL_DELAY_NS, true);
            }
        }
        spare_expire();
    }
    return nullptr;
}

static pthread_t g_warm_thread;
static bool g_warm_started;
/* the warm up thread ended, for a join with a time limit. */
static pthread_mutex_t g_warm_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_warm_ended_cond = PTHREAD_COND_INITIALIZER;
static bool g_warm_ended;

static void *warm_main(void *arg)
{
    mime_set_thread_name("mime-vk-warm");
    warm_run();
    pthread_mutex_lock(&g_warm_lock);
    g_warm_ended = true;
    pthread_cond_broadcast(&g_warm_ended_cond);
    pthread_mutex_unlock(&g_warm_lock);
    return nullptr;
}

void mime_vk_warm_up(void)
{
    g_warm_started = pthread_create(&g_warm_thread, nullptr, warm_main, nullptr) == 0;
}

/*
 * shutdown may begin while the warmup thread is inside the driver. join it before closing anything, since
 * exiting during driver work can crash its destructors.
 */
static void warm_wait(void)
{
    if (!g_warm_started)
        return;
    g_warm_started = false;
    pthread_mutex_lock(&g_maker_lock);
    g_maker_stop = true;
    pthread_cond_signal(&g_maker_wake);
    pthread_mutex_unlock(&g_maker_lock);
    /* a driver that hangs must not hang the machine's end. */
    struct timespec limit;
    clock_gettime(CLOCK_REALTIME, &limit);
    limit.tv_sec += 3;
    pthread_mutex_lock(&g_warm_lock);
    while (!g_warm_ended && pthread_cond_timedwait(&g_warm_ended_cond, &g_warm_lock, &limit) == 0) {
    }
    bool ended = g_warm_ended;
    pthread_mutex_unlock(&g_warm_lock);
    if (ended) {
        pthread_join(g_warm_thread, nullptr);
    } else {
        pthread_detach(g_warm_thread);
        mime_warn("vulkan: the warm up of the driver did not end in time");
    }
}
