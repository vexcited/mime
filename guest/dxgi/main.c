/*
 * dxgi.dll exports and the gpus behind its adapters. the vulkan instance is
 * made with the first factory and lives as long as the process.
 */
#include "dxgi_private.h"

int _fltused = 0x9875;

static DxgiVulkan g_vulkan;
static INIT_ONCE g_vulkan_once = INIT_ONCE_STATIC_INIT;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

/*
 * the user mode driver version as windows numbers it, read by programs through CheckInterfaceSupport. on x64 the
 * host gpu's package is told as windows tells one, nvidia's in the last digit of the major and the minor, so 580.95
 * is 32.0.15.8095. the arm64 machine's adreno package is 31.0.63.7.
 */
static LARGE_INTEGER umd_version(const VkPhysicalDeviceProperties *p)
{
    LARGE_INTEGER v;
#if defined(__aarch64__)
    v.HighPart = (31 << 16);
    v.LowPart = (63 << 16) | 7;
#else
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
#endif
    return v;
}

/*
 * on arm64 the adapter presents the reference machine, a qualcomm adreno x1-85, whatever the host gpu is. on x64 the
 * host gpu is real hardware the guest runs, so it is described as itself.
 */
static void identify(const VkPhysicalDeviceProperties *p, DXGI_ADAPTER_DESC3 *desc)
{
#if defined(__aarch64__)
    static const WCHAR name[] = L"Qualcomm(R) Adreno(TM) X1-85 GPU";
    memcpy(desc->Description, name, sizeof(name));
    desc->VendorId = 0x5143;
    desc->DeviceId = 0x0C36;
    desc->SubSysId = 0x10280C69;
    desc->Revision = 0x0086;
    (void)p;
#else
    MultiByteToWideChar(CP_UTF8, 0, p->deviceName, -1, desc->Description, 128);
    desc->Description[127] = 0;
    desc->VendorId = p->vendorID;
    desc->DeviceId = p->deviceID;
    desc->Revision = 0;
    desc->SubSysId = 0;
#endif
}

static void describe(DxgiGpu *gpu, UINT index)
{
    DXGI_ADAPTER_DESC3 *desc = &gpu->desc;
    memset(desc, 0, sizeof(*desc));
    identify(&gpu->properties, desc);
    for (UINT i = 0; i < gpu->memory.memoryHeapCount; i++) {
        const VkMemoryHeap *heap = &gpu->memory.memoryHeaps[i];
        if (heap->flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            desc->DedicatedVideoMemory += heap->size;
        else
            desc->SharedSystemMemory += heap->size;
    }
    /* like windows, an integrated gpu's memory counts as shared system memory. */
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

/* discrete gpus first, like windows lists the one driving the display, since games pick the first. */
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

static void find_gpus(DxgiVulkan *vk)
{
    /*
     * load mime's vulkan-1.dll from the system directory by full path. programs that ship their own loader, like
     * Chromium's embedded browsers with an empty driver list, would have it found first by name, leaving no adapters.
     */
    WCHAR path[MAX_PATH + 16];
    UINT length = GetSystemDirectoryW(path, MAX_PATH);
    HMODULE module = nullptr;
    if (length && length < MAX_PATH) {
        lstrcpyW(path + length, L"\\vulkan-1.dll");
        module = LoadLibraryW(path);
    }
    if (!module)
        return;
    vk->get_instance_proc = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(module, "vkGetInstanceProcAddr");
    if (!vk->get_instance_proc)
        return;
    PFN_vkEnumerateInstanceVersion enumerate_version =
        (PFN_vkEnumerateInstanceVersion)vk->get_instance_proc(nullptr, "vkEnumerateInstanceVersion");
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)vk->get_instance_proc(nullptr, "vkCreateInstance");
    uint32_t version = VK_API_VERSION_1_0;
    if (!enumerate_version || !create || enumerate_version(&version) != VK_SUCCESS ||
        version < VK_API_VERSION_1_3)
        return;
    vk->api_version = version;
    static const char *const extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "dxgi";
    app.pEngineName = "DirectX";
    app.apiVersion = version;
    VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = RTL_NUMBER_OF(extensions);
    info.ppEnabledExtensionNames = extensions;
    if (create(&info, nullptr, &vk->instance) != VK_SUCCESS)
        return;

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
        /* an insertion sort by rank, stable so vulkan's order holds within a kind. */
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
}

/*
 * the basic render driver, windows' warp adapter. with a gpu, its devices run on the first gpu's vulkan device: warp
 * is complete there and fast, and only its identity tells it apart. a cpu vulkan driver like lavapipe is never used
 * for it, being slower than the gpu. without a gpu it has no vulkan device, and d3d11 makes the software device of
 * warp.c on it.
 */
static void describe_warp(DxgiVulkan *vk)
{
    DxgiGpu *gpu = &vk->warp;
    DXGI_ADAPTER_DESC3 *desc = &gpu->desc;
    if (vk->count) {
        gpu->physical = vk->gpus[0].physical;
        gpu->properties = vk->gpus[0].properties;
        gpu->memory = vk->gpus[0].memory;
    }
    static const WCHAR name[] = L"Microsoft Basic Render Driver";
    memcpy(desc->Description, name, sizeof(name));
    desc->VendorId = 0x1414;
    desc->DeviceId = 0x8C;
    /* windows gives the software adapter half of the physical memory as its shared segment. */
    MEMORYSTATUSEX status = {sizeof(status)};
    if (GlobalMemoryStatusEx(&status))
        desc->SharedSystemMemory = (SIZE_T)(status.ullTotalPhys / 2);
    desc->AdapterLuid.LowPart = 0x0000D3D0 + RTL_NUMBER_OF(vk->gpus);
    desc->Flags = DXGI_ADAPTER_FLAG3_SOFTWARE;
    desc->GraphicsPreemptionGranularity = DXGI_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY;
    desc->ComputePreemptionGranularity = DXGI_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY;
    /* the version of d3d10warp.dll. */
    gpu->umd_version.HighPart = (10 << 16) | 0;
    gpu->umd_version.LowPart = (26100 << 16) | 9278;
}

static BOOL CALLBACK init_vulkan(INIT_ONCE *once, void *param, void **context)
{
    find_gpus(&g_vulkan);
    describe_warp(&g_vulkan);
    return TRUE;
}

const DxgiVulkan *DxgiGetVulkan(void)
{
    InitOnceExecuteOnce(&g_vulkan_once, init_vulkan, nullptr, nullptr);
    return &g_vulkan;
}

const DxgiGpu *DxgiGpuAt(UINT index)
{
    const DxgiVulkan *vk = DxgiGetVulkan();
    if (index < vk->count)
        return &vk->gpus[index];
    return index == vk->count ? &vk->warp : nullptr;
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

/* absent, as when the graphics tools are not installed. */
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

/* windows tells the kernel driver through a process wide setting, here the vblanks of an output are never virtualized. */
DXGIAPI HRESULT WINAPI DXGIDisableVBlankVirtualization(void)
{
    return S_OK;
}
