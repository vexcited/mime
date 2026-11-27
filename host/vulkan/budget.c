#include "mvk_host.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DEVICES 256
#define MARGIN_MIN (768ull << 20)
#define WARN_INTERVAL_NS 1000000000ull

typedef struct BudgetDevice {
    VkDevice device;
    VkPhysicalDevice physical;
    PFN_vkAllocateMemory allocate;
    VkPhysicalDeviceMemoryProperties memory;
    bool budgets; /* the device reports VK_EXT_memory_budget. */
    int32_t spill; /* the type in system memory every image and buffer takes, or -1, spill_type. */
} BudgetDevice;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static BudgetDevice g_devices[MAX_DEVICES];
/* bytes of video memory given system memory since the last warning, and in all. */
static VkDeviceSize g_spilled_recent, g_spilled;
static uint64_t g_spill_warned;
static PFN_vkGetPhysicalDeviceMemoryProperties2 g_properties2;
static PFN_vkEnumerateDeviceExtensionProperties g_extensions;
/* bytes being allocated on each heap, which the driver does not count yet. */
static VkDeviceSize g_pending[VK_MAX_MEMORY_HEAPS];
static uint64_t g_warned;

/*
 * the room the desktop and the driver keep on a heap of size bytes: 10% or 768 MiB. the driver takes what it needs
 * for a process's command streams and shaders from there, and with less a game that fills its budget sees the gpu
 * fault on command memory the driver could not get.
 */
static VkDeviceSize margin_of(VkDeviceSize size) { return MIME_MAX(size / 10, MARGIN_MIN); }

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
    if (fits)
        g_pending[heap] += size;
    return fits;
}

/* under g_lock. */
static void warn_refused(const BudgetDevice *d, uint32_t heap, VkDeviceSize size)
{
    uint64_t now = mime_now_ns();
    if (now - g_warned > WARN_INTERVAL_NS) {
        g_warned = now;
        mime_warn("vulkan: %llu KiB more on heap %u would leave the desktop less than %llu MiB of video memory, and "
                  "system memory has no room for it, refused",
                  (unsigned long long)(size >> 10), heap,
                  (unsigned long long)(margin_of(d->memory.memoryHeaps[heap].size) >> 20));
    }
}

/*
 * the memory type of system memory that every resource may live in, the one video memory spills to. WDDM pages video
 * memory out to system memory when a program asks for more than the gpu has, so its allocations succeed and the
 * program runs slower. the Linux drivers refuse instead, NVIDIA at the size of the heap with
 * VK_ERROR_OUT_OF_DEVICE_MEMORY, and a game that only retries freezes. NVIDIA lets images of every kind and buffers
 * live in its type 0, system memory without any property, which is where they go. found from the memory types the
 * resources the gpu has take, -1 when none fits them all.
 */
static int32_t spill_type(MvkConnection *c, const VkPhysicalDeviceMemoryProperties *memory)
{
    if (!c->fn.vkCreateImage || !c->fn.vkGetImageMemoryRequirements || !c->fn.vkDestroyImage || !c->fn.vkCreateBuffer ||
        !c->fn.vkGetBufferMemoryRequirements || !c->fn.vkDestroyBuffer)
        return -1;
    static const struct {
        VkFormat format;
        VkImageUsageFlags usage;
        VkSampleCountFlagBits samples;
    } images[] = {
        {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SAMPLE_COUNT_1_BIT},
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
         VK_SAMPLE_COUNT_1_BIT},
        {VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
         VK_SAMPLE_COUNT_1_BIT},
        {VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_SAMPLE_COUNT_1_BIT},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_SAMPLE_COUNT_4_BIT},
        {VK_FORMAT_BC7_UNORM_BLOCK, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SAMPLE_COUNT_1_BIT},
    };
    uint32_t bits = ~0u;
    for (size_t i = 0; i < MIME_ARRAY_COUNT(images); i++) {
        VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0, VK_IMAGE_TYPE_2D, images[i].format,
                                  {256, 256, 1}, 1, 1, images[i].samples, VK_IMAGE_TILING_OPTIMAL, images[i].usage,
                                  VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED};
        VkImage image;
        /* a format the gpu does not have is left out. */
        if (c->fn.vkCreateImage(c->device, &info, nullptr, &image) != VK_SUCCESS)
            continue;
        VkMemoryRequirements req;
        c->fn.vkGetImageMemoryRequirements(c->device, image, &req);
        c->fn.vkDestroyImage(c->device, image, nullptr);
        bits &= req.memoryTypeBits;
    }
    VkBufferCreateInfo binfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, 65536,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer;
    if (c->fn.vkCreateBuffer(c->device, &binfo, nullptr, &buffer) != VK_SUCCESS)
        return -1;
    VkMemoryRequirements req;
    c->fn.vkGetBufferMemoryRequirements(c->device, buffer, &req);
    c->fn.vkDestroyBuffer(c->device, buffer, nullptr);
    bits &= req.memoryTypeBits;
    int32_t found = -1;
    for (uint32_t i = 0; i < memory->memoryTypeCount; i++) {
        const VkMemoryType *t = &memory->memoryTypes[i];
        if (!(bits & (1u << i)) || (memory->memoryHeaps[t->heapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ||
            (t->propertyFlags & (VK_MEMORY_PROPERTY_PROTECTED_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)))
            continue;
        /* without host access the driver may place it as it likes, so that one comes first. */
        if (found < 0 || !t->propertyFlags)
            found = (int32_t)i;
        if (!t->propertyFlags)
            break;
    }
    return found;
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
        /*
         * past the budget the allocation goes to system memory, as WDDM would page it out, and the program goes on
         * slower. only with room in system memory, else the program hears what windows tells when both are full.
         */
        int32_t spill = d->spill;
        bool room = spill >= 0 && info->allocationSize <= mime_memory_room();
        if (!room)
            warn_refused(d, (uint32_t)heap, info->allocationSize);
        pthread_mutex_unlock(&g_lock);
        if (!room)
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        VkMemoryAllocateInfo moved = *info;
        moved.memoryTypeIndex = (uint32_t)spill;
        VkResult r = allocate(device, &moved, a, memory);
        pthread_mutex_lock(&g_lock);
        if (r != VK_SUCCESS) {
            warn_refused(d, (uint32_t)heap, info->allocationSize);
            pthread_mutex_unlock(&g_lock);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        g_spilled += info->allocationSize;
        g_spilled_recent += info->allocationSize;
        uint64_t now = mime_now_ns();
        if (now - g_spill_warned > WARN_INTERVAL_NS) {
            g_spill_warned = now;
            mime_info("vulkan: %llu MiB of video memory went to system memory, %llu MiB since the gpu was full",
                      (unsigned long long)(g_spilled_recent >> 20), (unsigned long long)(g_spilled >> 20));
            g_spilled_recent = 0;
        }
        pthread_mutex_unlock(&g_lock);
        return VK_SUCCESS;
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
    d.spill = spill_type(c, &d.memory);
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
