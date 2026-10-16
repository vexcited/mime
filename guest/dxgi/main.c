/*
 * the exports of dxgi.dll and the gpus behind its adapters. the vulkan
 * instance is made once, the first time a factory is, and lives as long
 * as the process.
 */
#include "dxgi_private.h"

int _fltused = 0x9875;

static DxgiVulkan g_vulkan;
static INIT_ONCE g_vulkan_once = INIT_ONCE_STATIC_INIT;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

/*
 * the version of the user mode driver as windows numbers it, which
 * programs read through CheckInterfaceSupport. nvidia's drivers end in the
 * last digit of their major version and their minor, 580.95 being
 * 32.0.15.8095.
 */
static LARGE_INTEGER umd_version(const VkPhysicalDeviceProperties *p)
{
    LARGE_INTEGER v;
    UINT a = 31, b = 0, c, d;
    if (p->vendorID == 0x10DE) {
        UINT n = (p->driverVersion >> 22) * 100 + ((p->driverVersion >> 14) & 0xFF);
        a = 32;
        c = 10 + n / 10000;
        d = n % 10000;
    } else {
        c = VK_API_VERSION_MAJOR(p->driverVersion) * 100 + VK_API_VERSION_MINOR(p->driverVersion);
        d = VK_API_VERSION_PATCH(p->driverVersion);
    }
    v.HighPart = (a << 16) | b;
    v.LowPart = (c << 16) | (d & 0xFFFF);
    return v;
}

static void describe(DxgiGpu *gpu, UINT index)
{
    DXGI_ADAPTER_DESC3 *desc = &gpu->desc;
    memset(desc, 0, sizeof(*desc));
    MultiByteToWideChar(CP_UTF8, 0, gpu->properties.deviceName, -1, desc->Description, 128);
    desc->Description[127] = 0;
    desc->VendorId = gpu->properties.vendorID;
    desc->DeviceId = gpu->properties.deviceID;
    for (UINT i = 0; i < gpu->memory.memoryHeapCount; i++) {
        const VkMemoryHeap *heap = &gpu->memory.memoryHeaps[i];
        if (heap->flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            desc->DedicatedVideoMemory += heap->size;
        else
            desc->SharedSystemMemory += heap->size;
    }
    /* an integrated gpu's memory is the system's, as windows reports it. */
    if (gpu->properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        desc->SharedSystemMemory += desc->DedicatedVideoMemory;
        desc->DedicatedVideoMemory = 128 << 20;
    }
    /* adapters keep their luid across processes: shared resources name them by it. */
    desc->AdapterLuid.LowPart = 0x0000D3D0 + index;
    desc->Flags = DXGI_ADAPTER_FLAG3_SUPPORT_MONITORED_FENCES | DXGI_ADAPTER_FLAG3_SUPPORT_NON_MONITORED_FENCES |
                  DXGI_ADAPTER_FLAG3_KEYED_MUTEX_CONFORMANCE;
    desc->GraphicsPreemptionGranularity = DXGI_GRAPHICS_PREEMPTION_PIXEL_BOUNDARY;
    desc->ComputePreemptionGranularity = DXGI_COMPUTE_PREEMPTION_DISPATCH_BOUNDARY;
    gpu->umd_version = umd_version(&gpu->properties);
}

/* discrete gpus first, as windows puts the one driving the display and games pick the first. */
static int rank(const DxgiGpu *gpu)
{
    switch (gpu->properties.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 0;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 1;
    default:
        return 2;
    }
}

static BOOL CALLBACK init_vulkan(INIT_ONCE *once, void *param, void **context)
{
    DxgiVulkan *vk = &g_vulkan;
    HMODULE module = LoadLibraryW(L"vulkan-1.dll");
    if (!module)
        return TRUE;
    vk->get_instance_proc = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(module, "vkGetInstanceProcAddr");
    if (!vk->get_instance_proc)
        return TRUE;
    PFN_vkEnumerateInstanceVersion enumerate_version =
        (PFN_vkEnumerateInstanceVersion)vk->get_instance_proc(nullptr, "vkEnumerateInstanceVersion");
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)vk->get_instance_proc(nullptr, "vkCreateInstance");
    uint32_t version = VK_API_VERSION_1_0;
    if (!enumerate_version || !create || enumerate_version(&version) != VK_SUCCESS ||
        version < VK_API_VERSION_1_3)
        return TRUE;
    vk->api_version = version;
    static const char *const extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "dxgi";
    app.pEngineName = "mime";
    app.apiVersion = version;
    VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = RTL_NUMBER_OF(extensions);
    info.ppEnabledExtensionNames = extensions;
    if (create(&info, nullptr, &vk->instance) != VK_SUCCESS)
        return TRUE;

#define PROC(name) ((PFN_##name)vk->get_instance_proc(vk->instance, #name))
    PFN_vkEnumeratePhysicalDevices enumerate = PROC(vkEnumeratePhysicalDevices);
    PFN_vkGetPhysicalDeviceProperties properties = PROC(vkGetPhysicalDeviceProperties);
    PFN_vkGetPhysicalDeviceMemoryProperties memory = PROC(vkGetPhysicalDeviceMemoryProperties);
    vk->get_memory2 = PROC(vkGetPhysicalDeviceMemoryProperties2);
#undef PROC
    VkPhysicalDevice physical[RTL_NUMBER_OF(vk->gpus)];
    uint32_t count = RTL_NUMBER_OF(physical);
    VkResult r = enumerate(vk->instance, &count, physical);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE)
        count = 0;
    for (uint32_t i = 0; i < count; i++) {
        DxgiGpu gpu = {physical[i]};
        properties(gpu.physical, &gpu.properties);
        if (gpu.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ||
            gpu.properties.apiVersion < VK_API_VERSION_1_3)
            continue;
        memory(gpu.physical, &gpu.memory);
        /* a stable sort by rank keeps vulkan's order within a kind. */
        UINT at = vk->count;
        while (at && rank(&vk->gpus[at - 1]) > rank(&gpu)) {
            vk->gpus[at] = vk->gpus[at - 1];
            at--;
        }
        vk->gpus[at] = gpu;
        vk->count++;
    }
    for (UINT i = 0; i < vk->count; i++)
        describe(&vk->gpus[i], i);
    return TRUE;
}

const DxgiVulkan *DxgiGetVulkan(void)
{
    InitOnceExecuteOnce(&g_vulkan_once, init_vulkan, nullptr, nullptr);
    return &g_vulkan;
}

DXGIAPI HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void **factory)
{
    if (!factory)
        return E_INVALIDARG;
    *factory = nullptr;
    if (flags & ~DXGI_CREATE_FACTORY_DEBUG)
        return DXGI_ERROR_INVALID_CALL;
    return DxgiCreateFactory(flags, riid, factory);
}

DXGIAPI HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void **factory)
{
    return CreateDXGIFactory2(0, riid, factory);
}

DXGIAPI HRESULT WINAPI CreateDXGIFactory(REFIID riid, void **factory)
{
    return CreateDXGIFactory2(0, riid, factory);
}

/* the debug layer's interfaces, absent as when the graphics tools are not installed. */
DXGIAPI HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID riid, void **debug)
{
    if (debug)
        *debug = nullptr;
    return E_NOINTERFACE;
}

DXGIAPI HRESULT WINAPI DXGIDeclareAdapterRemovalSupport(void)
{
    static LONG declared;
    return InterlockedExchange(&declared, 1) ? DXGI_ERROR_INVALID_CALL : S_OK;
}
