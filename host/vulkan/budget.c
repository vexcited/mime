#include "mvk_host.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DEVICES 256
#define MARGIN_MIN (256ull << 20)
#define WARN_INTERVAL_NS 1000000000ull

typedef struct BudgetDevice {
    VkDevice device;
    VkPhysicalDevice physical;
    PFN_vkAllocateMemory allocate;
    VkPhysicalDeviceMemoryProperties memory;
    bool budgets; /* the device reports VK_EXT_memory_budget. */
} BudgetDevice;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static BudgetDevice g_devices[MAX_DEVICES];
static PFN_vkGetPhysicalDeviceMemoryProperties2 g_properties2;
static PFN_vkEnumerateDeviceExtensionProperties g_extensions;
/* bytes being allocated on each heap, which the driver does not count yet. */
static VkDeviceSize g_pending[VK_MAX_MEMORY_HEAPS];
static uint64_t g_warned;

/* the room the desktop keeps on a heap of size bytes: 5% or 256 MiB. */
static VkDeviceSize margin_of(VkDeviceSize size) { return MIME_MAX(size / 20, MARGIN_MIN); }

static bool has_budget_extension(VkPhysicalDevice physical)
{
    uint32_t count = 0;
    if (!g_extensions || g_extensions(physical, nullptr, &count, nullptr) < 0 || !count)
        return false;
    VkExtensionProperties *list = calloc(count, sizeof(*list));
    bool found = false;
    if (list && g_extensions(physical, nullptr, &count, list) >= 0)
        for (uint32_t i = 0; i < count && !found; i++)
            found = !strcmp(list[i].extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    free(list);
    return found;
}

/* under g_lock. */
static BudgetDevice *find_device(VkDevice device)
{
    for (int i = 0; i < MAX_DEVICES; i++)
        if (g_devices[i].device == device)
            return &g_devices[i];
    return nullptr;
}

/* what the driver says the process may still take from each heap, the margin left out. */
static bool read_room(VkPhysicalDevice physical, const VkPhysicalDeviceMemoryProperties *memory,
                      VkDeviceSize room[VK_MAX_MEMORY_HEAPS])
{
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, &budget};
    if (!g_properties2)
        return false;
    g_properties2(physical, &props);
    for (uint32_t i = 0; i < memory->memoryHeapCount; i++) {
        VkDeviceSize size = memory->memoryHeaps[i].size, limit = budget.heapBudget[i] ? budget.heapBudget[i] : size;
        limit = limit > margin_of(size) ? limit - margin_of(size) : 0;
        room[i] = limit > budget.heapUsage[i] ? limit - budget.heapUsage[i] : 0;
    }
    return true;
}

static bool imports(const VkMemoryAllocateInfo *info)
{
    for (const VkBaseInStructure *s = info->pNext; s; s = s->pNext)
        if (s->sType == VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT ||
            s->sType == VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR)
            return true;
    return false;
}

/* takes size bytes of heap if they fit, to be given back by release once the driver has them. */
static bool reserve(const BudgetDevice *d, uint32_t heap, VkDeviceSize size)
{
    VkDeviceSize room[VK_MAX_MEMORY_HEAPS];
    bool fits;
    if (d->budgets && read_room(d->physical, &d->memory, room))
        fits = g_pending[heap] + size <= room[heap];
    else
        fits = size <= d->memory.memoryHeaps[heap].size - MIME_MIN(margin_of(d->memory.memoryHeaps[heap].size),
                                                                    d->memory.memoryHeaps[heap].size);
    if (fits) {
        g_pending[heap] += size;
        return true;
    }
    uint64_t now = mime_now_ns();
    if (now - g_warned > WARN_INTERVAL_NS) {
        g_warned = now;
        mime_warn("vulkan: %llu KiB more on heap %u would leave the desktop less than %llu MiB of video memory, refused",
                  (unsigned long long)(size >> 10), heap,
                  (unsigned long long)(margin_of(d->memory.memoryHeaps[heap].size) >> 20));
    }
    return false;
}

static VKAPI_ATTR VkResult VKAPI_CALL allocate_memory(VkDevice device, const VkMemoryAllocateInfo *info,
                                                      const VkAllocationCallbacks *a, VkDeviceMemory *memory)
{
    pthread_mutex_lock(&g_lock);
    BudgetDevice *d = find_device(device);
    if (!d) {
        pthread_mutex_unlock(&g_lock);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    PFN_vkAllocateMemory allocate = d->allocate;
    int32_t heap = -1;
    if (info->memoryTypeIndex < d->memory.memoryTypeCount && !imports(info)) {
        uint32_t h = d->memory.memoryTypes[info->memoryTypeIndex].heapIndex;
        if (d->memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            heap = (int32_t)h;
    }
    if (heap >= 0 && !reserve(d, (uint32_t)heap, info->allocationSize)) {
        pthread_mutex_unlock(&g_lock);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    /* system memory the driver allocates for the guest is host RAM the guest's own grants do not cover. */
    if (heap < 0 && info->memoryTypeIndex < d->memory.memoryTypeCount && !imports(info) &&
        info->allocationSize > mime_memory_room()) {
        pthread_mutex_unlock(&g_lock);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    pthread_mutex_unlock(&g_lock);
    VkResult r = allocate(device, info, a, memory);
    if (heap >= 0) {
        pthread_mutex_lock(&g_lock);
        g_pending[heap] -= info->allocationSize;
        pthread_mutex_unlock(&g_lock);
    }
    return r;
}

bool mvk_budget_fits(MvkConnection *c, VkDeviceSize size)
{
    pthread_mutex_lock(&g_lock);
    BudgetDevice *d = c->device ? find_device(c->device) : nullptr;
    bool fits = true;
    if (d) {
        for (uint32_t i = 0; i < d->memory.memoryHeapCount; i++)
            if (d->memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                fits = reserve(d, i, size);
                if (fits)
                    g_pending[i] -= size;
                break;
            }
    }
    pthread_mutex_unlock(&g_lock);
    return fits;
}

/* the guest sees budgets with the desktop's margin taken out, which is what allocations are held to. */
static void lower_budgets(VkPhysicalDeviceMemoryProperties2 *props)
{
    for (VkBaseOutStructure *s = (VkBaseOutStructure *)props->pNext; s; s = s->pNext) {
        if (s->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT)
            continue;
        VkPhysicalDeviceMemoryBudgetPropertiesEXT *b = (VkPhysicalDeviceMemoryBudgetPropertiesEXT *)s;
        const VkPhysicalDeviceMemoryProperties *m = &props->memoryProperties;
        for (uint32_t i = 0; i < m->memoryHeapCount; i++) {
            if (!(m->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) || !b->heapBudget[i])
                continue;
            VkDeviceSize margin = margin_of(m->memoryHeaps[i].size);
            b->heapBudget[i] = b->heapBudget[i] > margin ? b->heapBudget[i] - margin : 0;
        }
    }
}

static VKAPI_ATTR void VKAPI_CALL memory_properties2(VkPhysicalDevice physical, VkPhysicalDeviceMemoryProperties2 *props)
{
    g_properties2(physical, props);
    lower_budgets(props);
}

void mvk_budget_hook(MvkConnection *c)
{
    pthread_mutex_lock(&g_lock);
    if (c->fn.vkGetPhysicalDeviceMemoryProperties2 && c->fn.vkGetPhysicalDeviceMemoryProperties2 != memory_properties2) {
        g_properties2 = c->fn.vkGetPhysicalDeviceMemoryProperties2;
        c->fn.vkGetPhysicalDeviceMemoryProperties2 = memory_properties2;
    }
    if (c->fn.vkGetPhysicalDeviceMemoryProperties2KHR && g_properties2)
        c->fn.vkGetPhysicalDeviceMemoryProperties2KHR = memory_properties2;
    if (c->fn.vkEnumerateDeviceExtensionProperties)
        g_extensions = c->fn.vkEnumerateDeviceExtensionProperties;
    pthread_mutex_unlock(&g_lock);
    if (!c->device || !c->physical_device || !c->fn.vkAllocateMemory || c->fn.vkAllocateMemory == allocate_memory)
        return;
    BudgetDevice d = {c->device, c->physical_device, c->fn.vkAllocateMemory};
    c->fn.vkGetPhysicalDeviceMemoryProperties(c->physical_device, &d.memory);
    d.budgets = has_budget_extension(c->physical_device);
    pthread_mutex_lock(&g_lock);
    BudgetDevice *slot = find_device(c->device);
    if (!slot)
        slot = find_device(VK_NULL_HANDLE);
    if (slot) {
        *slot = d;
        c->fn.vkAllocateMemory = allocate_memory;
    }
    pthread_mutex_unlock(&g_lock);
}

void mvk_budget_forget(VkDevice device)
{
    pthread_mutex_lock(&g_lock);
    BudgetDevice *d = device ? find_device(device) : nullptr;
    if (d)
        memset(d, 0, sizeof(*d));
    pthread_mutex_unlock(&g_lock);
}
