/*
 * the instance, its physical devices and entry point lookup. host gpu
 * properties pass through unchanged, except that the guest can only map
 * memory the host imports from guest ram, so other memory types lose their
 * host visibility.
 */
#include "mvk_guest.h"

#define MVK_API_VERSION VK_API_VERSION_1_4

/* host instance extensions the protocol carries, queried once. */
static VkExtensionProperties *g_instance_extensions;
static uint32_t g_instance_extension_count;
static uint32_t g_host_api = VK_API_VERSION_1_0;
static SRWLOCK g_lock = SRWLOCK_INIT;

static VkResult load_instance_extensions(void)
{
    AcquireSRWLockExclusive(&g_lock);
    VkResult result = VK_SUCCESS;
    if (!g_instance_extensions) {
        result = VK_ERROR_INITIALIZATION_FAILED;
        MvkEncoder *e = mvk_call_begin(MVK_OP_INSTANCE_EXTENSIONS);
        MvkDecoder *d = mvk_call_wait(e);
        if (d) {
            result = (VkResult)mvk_get_u32(d);
            if (result == VK_SUCCESS) {
                g_host_api = mvk_get_u32(d);
                uint32_t n = mvk_get_u32(d);
                VkExtensionProperties *list = HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(*list));
                if (list) {
                    for (uint32_t i = 0; i < n; i++)
                        mvk_get_bytes(d, &list[i], sizeof(list[i]));
                    g_instance_extension_count = n;
                    g_instance_extensions = list;
                } else {
                    result = VK_ERROR_OUT_OF_HOST_MEMORY;
                }
            }
        }
        mvk_call_done(d);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t *version)
{
    if (load_instance_extensions() != VK_SUCCESS)
        return VK_ERROR_INITIALIZATION_FAILED;
    *version = g_host_api < MVK_API_VERSION ? g_host_api : MVK_API_VERSION;
    return VK_SUCCESS;
}

static VkResult copy_properties(const VkExtensionProperties *list, uint32_t n, uint32_t *count,
                                VkExtensionProperties *out)
{
    if (!out) {
        *count = n;
        return VK_SUCCESS;
    }
    uint32_t copy = *count < n ? *count : n;
    memcpy(out, list, copy * sizeof(*list));
    *count = copy;
    return copy < n ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *layer, uint32_t *count,
                                                                      VkExtensionProperties *properties)
{
    if (layer)
        return VK_ERROR_LAYER_NOT_PRESENT;
    VkResult r = load_instance_extensions();
    if (r != VK_SUCCESS)
        return r;
    return copy_properties(g_instance_extensions, g_instance_extension_count, count, properties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t *count, VkLayerProperties *properties)
{
    *count = 0;
    return VK_SUCCESS;
}

static BOOL listed(const VkExtensionProperties *list, uint32_t n, const char *name)
{
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(list[i].extensionName, name))
            return TRUE;
    return FALSE;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *info, const VkAllocationCallbacks *allocator,
                                                VkInstance *out)
{
    VkResult r = load_instance_extensions();
    if (r != VK_SUCCESS)
        return r;
    if (info->enabledLayerCount)
        return VK_ERROR_LAYER_NOT_PRESENT;
    for (uint32_t i = 0; i < info->enabledExtensionCount; i++)
        if (!listed(g_instance_extensions, g_instance_extension_count, info->ppEnabledExtensionNames[i]))
            return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkInstance instance = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*instance));
    if (!instance)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    instance->obj.loader_magic = MVK_LOADER_MAGIC;
    instance->obj.id = mvk_new_id();
    instance->api_version = info->pApplicationInfo ? info->pApplicationInfo->apiVersion : VK_API_VERSION_1_0;
    MvkEncoder *e = mvk_call_begin(MVK_OP_CREATE_INSTANCE);
    mvk_put_u64(e, instance->obj.id);
    mvk_put_VkInstanceCreateInfo(e, info);
    MvkDecoder *d = mvk_call_wait(e);
    r = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_INITIALIZATION_FAILED;
    mvk_call_done(d);
    if (r != VK_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, instance);
        return r;
    }
    *out = instance;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *allocator)
{
    if (!instance)
        return;
    MvkEncoder *e = mvk_call_begin(MVK_OP_DESTROY_INSTANCE);
    mvk_put_u64(e, instance->obj.id);
    mvk_call_end(e);
    for (uint32_t i = 0; i < instance->physical_count; i++)
        HeapFree(GetProcessHeap(), 0, instance->physical[i]);
    HeapFree(GetProcessHeap(), 0, instance);
}

/* ------------------------------------------------------------------ physical devices */

static VkResult load_physical_devices(VkInstance instance)
{
    if (instance->physical_count)
        return VK_SUCCESS;
    uint64_t ids[16];
    for (int i = 0; i < 16; i++)
        ids[i] = mvk_new_id();
    MvkEncoder *e = mvk_call_begin(MVK_OP_PHYSICAL_DEVICES);
    mvk_put_u32(e, 16);
    for (int i = 0; i < 16; i++)
        mvk_put_u64(e, ids[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult r = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_INITIALIZATION_FAILED;
    uint32_t n = d ? mvk_get_u32(d) : 0;
    uint32_t importable[16] = {};
    for (uint32_t i = 0; i < n && i < 16; i++)
        importable[i] = d ? mvk_get_u32(d) : 0;
    mvk_call_done(d);
    if (r != VK_SUCCESS)
        return r;
    for (uint32_t i = 0; i < n && i < 16; i++) {
        VkPhysicalDevice p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
        if (!p)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        p->obj.loader_magic = MVK_LOADER_MAGIC;
        p->obj.id = ids[i];
        p->instance = instance;
        instance->physical[instance->physical_count++] = p;
        mvk_raw_vkGetPhysicalDeviceProperties(p, &p->properties);
        if (p->properties.apiVersion > MVK_API_VERSION)
            p->properties.apiVersion = MVK_API_VERSION;
        mvk_raw_vkGetPhysicalDeviceMemoryProperties(p, &p->memory);
        /* the guest cannot map memory the host cannot import from guest ram. */
        for (uint32_t t = 0; t < p->memory.memoryTypeCount; t++)
            if (!(importable[i] & (1u << t)))
                p->memory.memoryTypes[t].propertyFlags &=
                    ~(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        p->memory_known = 1;
    }
    for (uint32_t i = n; i < 16; i++)
        mvk_free_id(ids[i]);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *count, VkPhysicalDevice *out)
{
    VkResult r = load_physical_devices(instance);
    if (r != VK_SUCCESS)
        return r;
    if (!out) {
        *count = instance->physical_count;
        return VK_SUCCESS;
    }
    uint32_t n = *count < instance->physical_count ? *count : instance->physical_count;
    for (uint32_t i = 0; i < n; i++)
        out[i] = instance->physical[i];
    *count = n;
    return n < instance->physical_count ? VK_INCOMPLETE : VK_SUCCESS;
}

/* every physical device is a group of its own. */
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t *count,
                                                               VkPhysicalDeviceGroupProperties *out)
{
    VkResult r = load_physical_devices(instance);
    if (r != VK_SUCCESS)
        return r;
    if (!out) {
        *count = instance->physical_count;
        return VK_SUCCESS;
    }
    uint32_t n = *count < instance->physical_count ? *count : instance->physical_count;
    for (uint32_t i = 0; i < n; i++) {
        out[i].physicalDeviceCount = 1;
        memset(out[i].physicalDevices, 0, sizeof(out[i].physicalDevices));
        out[i].physicalDevices[0] = instance->physical[i];
        out[i].subsetAllocation = VK_FALSE;
    }
    *count = n;
    return n < instance->physical_count ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physical, VkPhysicalDeviceProperties *out)
{
    *out = physical->properties;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice physical, VkPhysicalDeviceProperties2 *out)
{
    mvk_raw_vkGetPhysicalDeviceProperties2(physical, out);
    out->properties.apiVersion = physical->properties.apiVersion;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physical,
                                                               VkPhysicalDeviceMemoryProperties *out)
{
    *out = physical->memory;
}

/*
 * programs ask for the memory budget every frame, and a reply waits behind everything the ring holds. so the last
 * one stands for a moment, unless the process allocated or freed memory since.
 */
#define BUDGET_AGE_MS 30

static SRWLOCK g_budget_lock = SRWLOCK_INIT;
static LONG g_memory_generation;
static struct {
    VkPhysicalDevice physical;
    ULONGLONG tick;
    LONG generation;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget;
} g_budget;

void mvk_memory_changed(void)
{
    InterlockedIncrement(&g_memory_generation);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physical,
                                                                VkPhysicalDeviceMemoryProperties2 *out)
{
    VkPhysicalDeviceMemoryBudgetPropertiesEXT *budget = out->pNext;
    if (budget && (budget->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT || budget->pNext))
        budget = nullptr;
    ULONGLONG now = GetTickCount64();
    LONG generation = InterlockedCompareExchange(&g_memory_generation, 0, 0);
    if (budget) {
        AcquireSRWLockShared(&g_budget_lock);
        BOOL fresh = g_budget.physical == physical && g_budget.generation == generation &&
                     now - g_budget.tick < BUDGET_AGE_MS;
        if (fresh) {
            memcpy(budget->heapBudget, g_budget.budget.heapBudget, sizeof(budget->heapBudget));
            memcpy(budget->heapUsage, g_budget.budget.heapUsage, sizeof(budget->heapUsage));
        }
        ReleaseSRWLockShared(&g_budget_lock);
        if (fresh) {
            out->memoryProperties = physical->memory;
            return;
        }
    }
    /* the chain, such as the memory budget, comes from the host; the types are the guest's view. */
    mvk_raw_vkGetPhysicalDeviceMemoryProperties2(physical, out);
    out->memoryProperties = physical->memory;
    if (budget) {
        AcquireSRWLockExclusive(&g_budget_lock);
        g_budget.physical = physical;
        g_budget.tick = now;
        g_budget.generation = generation;
        g_budget.budget = *budget;
        ReleaseSRWLockExclusive(&g_budget_lock);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice physical, uint32_t *count,
                                                                VkLayerProperties *properties)
{
    *count = 0;
    return VK_SUCCESS;
}

VkResult mvk_device_extensions(VkPhysicalDevice physical, VkExtensionProperties **list, uint32_t *count);

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physical, const char *layer,
                                                                    uint32_t *count, VkExtensionProperties *out)
{
    if (layer)
        return VK_ERROR_LAYER_NOT_PRESENT;
    VkExtensionProperties *list;
    uint32_t n;
    VkResult r = mvk_device_extensions(physical, &list, &n);
    if (r != VK_SUCCESS)
        return r;
    return copy_properties(list, n, count, out);
}

/* host device extensions the protocol carries, queried once per physical device. */
VkResult mvk_device_extensions(VkPhysicalDevice physical, VkExtensionProperties **out, uint32_t *count)
{
    static struct {
        VkPhysicalDevice physical;
        VkExtensionProperties *list;
        uint32_t count;
    } cache[16];
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < 16; i++) {
        if (cache[i].physical == physical) {
            *out = cache[i].list;
            *count = cache[i].count;
            ReleaseSRWLockExclusive(&g_lock);
            return VK_SUCCESS;
        }
    }
    MvkEncoder *e = mvk_call_begin(MVK_OP_DEVICE_EXTENSIONS);
    mvk_put_u64(e, physical->obj.id);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult r = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_INITIALIZATION_FAILED;
    VkExtensionProperties *list = nullptr;
    uint32_t n = 0;
    if (r == VK_SUCCESS) {
        n = mvk_get_u32(d);
        list = HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(*list));
        for (uint32_t i = 0; list && i < n; i++)
            mvk_get_bytes(d, &list[i], sizeof(list[i]));
    }
    mvk_call_done(d);
    if (r == VK_SUCCESS && list) {
        for (int i = 0; i < 16; i++) {
            if (!cache[i].physical) {
                cache[i].physical = physical;
                cache[i].list = list;
                cache[i].count = n;
                break;
            }
        }
        *out = list;
        *count = n;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return r == VK_SUCCESS && !list ? VK_ERROR_OUT_OF_HOST_MEMORY : r;
}

/* ------------------------------------------------------------------ entry points */

static PFN_vkVoidFunction find_proc(const char *name)
{
    if (!name)
        return nullptr;
    for (const MvkProc *p = mvk_procs; p->name; p++)
        if (!strcmp(p->name, name))
            return p->fn;
    for (const MvkProc *p = mvk_video_procs; p->name; p++)
        if (!strcmp(p->name, name))
            return p->fn;
    for (const MvkProc *p = mvk_share_procs; p->name; p++)
        if (!strcmp(p->name, name))
            return p->fn;
    for (const MvkProc *p = mvk_wsi_procs; p->name; p++)
        if (!strcmp(p->name, name))
            return p->fn;
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
    if (!name)
        return nullptr;
    if (!strcmp(name, "vkGetInstanceProcAddr"))
        return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    /* without an instance, only the global commands resolve. */
    if (!instance && strcmp(name, "vkCreateInstance") && strcmp(name, "vkEnumerateInstanceVersion") &&
        strcmp(name, "vkEnumerateInstanceExtensionProperties") && strcmp(name, "vkEnumerateInstanceLayerProperties"))
        return nullptr;
    return find_proc(name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *name)
{
    return find_proc(name);
}
