/*
 * the device: an adapter's Vulkan device with what Direct3D 12 needs enabled, its own commands, the features it
 * reports and ID3D12Device14, whose methods mostly hand over to the other files.
 */
#include "d3d12_private.h"

BOOL D12DeviceUnlist(D12Device *device);

D12Device *D12DeviceFrom(ID3D12Device *iface) { return CONTAINING_RECORD(iface, D12Device, iface); }
static D12Device *impl(ID3D12Device14 *iface) { return CONTAINING_RECORD(iface, D12Device, iface); }

void D12ChildInit(D12Child *child, D12Device *device)
{
    child->refs = 1;
    child->device = device;
    device->iface.lpVtbl->AddRef(&device->iface);
}

void D12ChildRelease(D12Child *child)
{
    MimePrivateClear(&child->data);
    child->device->iface.lpVtbl->Release(&child->device->iface);
}

HRESULT D12ChildGetDevice(D12Child *child, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    return child->device->iface.lpVtbl->QueryInterface(&child->device->iface, riid, out);
}

static void destroy_object(D12Device *device, VkObjectType type, UINT64 handle)
{
    D12Vk *vk = &device->vk;
    switch (type) {
    case VK_OBJECT_TYPE_IMAGE:
        vk->vkDestroyImage(vk->device, (VkImage)handle, nullptr);
        break;
    case VK_OBJECT_TYPE_IMAGE_VIEW:
        vk->vkDestroyImageView(vk->device, (VkImageView)handle, nullptr);
        break;
    case VK_OBJECT_TYPE_BUFFER:
        vk->vkDestroyBuffer(vk->device, (VkBuffer)handle, nullptr);
        break;
    case VK_OBJECT_TYPE_DEVICE_MEMORY:
        vk->vkFreeMemory(vk->device, (VkDeviceMemory)handle, nullptr);
        break;
    case VK_OBJECT_TYPE_SAMPLER:
        vk->vkDestroySampler(vk->device, (VkSampler)handle, nullptr);
        break;
    case VK_OBJECT_TYPE_PIPELINE:
        vk->vkDestroyPipeline(vk->device, (VkPipeline)handle, nullptr);
        break;
    case VK_OBJECT_TYPE_QUERY_POOL:
        vk->vkDestroyQueryPool(vk->device, (VkQueryPool)handle, nullptr);
        break;
    case VK_OBJECT_TYPE_PIPELINE_LAYOUT:
        vk->vkDestroyPipelineLayout(vk->device, (VkPipelineLayout)handle, nullptr);
        break;
    case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
        vk->vkDestroyDescriptorSetLayout(vk->device, (VkDescriptorSetLayout)handle, nullptr);
        break;
    case VK_OBJECT_TYPE_COMMAND_POOL:
        vk->vkDestroyCommandPool(vk->device, (VkCommandPool)handle, nullptr);
        break;
    case VK_OBJECT_TYPE_SEMAPHORE:
        vk->vkDestroySemaphore(vk->device, (VkSemaphore)handle, nullptr);
        break;
    default:
        break;
    }
}

/*
 * Direct3D 12 programs keep what their own work uses alive until it is done, so released objects can go at once,
 * but for the device's own commands: a texture made and released at once is named in those, so what is released
 * while they record waits until they ran.
 */
void D12Defer(D12Device *device, VkObjectType type, UINT64 handle)
{
    if (!handle)
        return;
    EnterCriticalSection(&device->init_lock);
    if (device->init_recording) {
        if (device->garbage_count == device->garbage_capacity) {
            UINT capacity = device->garbage_capacity ? device->garbage_capacity * 2 : 64;
            D12Garbage *grown = device->garbage
                                    ? HeapReAlloc(GetProcessHeap(), 0, device->garbage, capacity * sizeof(*grown))
                                    : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*grown));
            if (grown) {
                device->garbage = grown;
                device->garbage_capacity = capacity;
            }
        }
        if (device->garbage_count < device->garbage_capacity) {
            device->garbage[device->garbage_count++] = (D12Garbage){handle, type};
            LeaveCriticalSection(&device->init_lock);
            return;
        }
        /* with no room to keep it, the batch goes first. */
        D12Log("d3d12: no room to keep a released object until the device's commands run");
    }
    LeaveCriticalSection(&device->init_lock);
    destroy_object(device, type, handle);
}

static void destroy_garbage(D12Device *device, D12Garbage *garbage, UINT count)
{
    for (UINT i = 0; i < count; i++)
        destroy_object(device, garbage[i].type, garbage[i].handle);
    if (garbage)
        HeapFree(GetProcessHeap(), 0, garbage);
}

/* ------------------------------------------------------------------ the device's own commands */

/*
 * images start in the undefined layout and live in the general one. the device records their transitions and the
 * next queue to submit runs them first; the init timeline orders them before every other queue's later work.
 */
VkCommandBuffer D12InitCommands(D12Device *device)
{
    D12Vk *vk = &device->vk;
    EnterCriticalSection(&device->init_lock);
    if (!device->init_recording) {
        if (device->init_cmd) {
            /* the previous batch must be done before its command buffer is reused. */
            VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
            wait.semaphoreCount = 1;
            wait.pSemaphores = &device->init_timeline;
            wait.pValues = &device->init_serial;
            vk->vkWaitSemaphores(vk->device, &wait, UINT64_MAX);
            destroy_garbage(device, device->retired, device->retired_count);
            device->retired = nullptr;
            device->retired_count = 0;
        } else {
            VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            alloc.commandPool = device->init_pool;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;
            vk->vkAllocateCommandBuffers(vk->device, &alloc, &device->init_cmd);
        }
        vk->vkResetCommandBuffer(device->init_cmd, 0);
        VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk->vkBeginCommandBuffer(device->init_cmd, &begin);
        device->init_recording = TRUE;
    }
    return device->init_cmd;
}

void D12InitDone(D12Device *device) { LeaveCriticalSection(&device->init_lock); }

/* ends the recorded commands and returns them with the init timeline serial they signal, NULL when none. */
VkCommandBuffer D12TakeInitCommands(D12Device *device, UINT64 *serial)
{
    EnterCriticalSection(&device->init_lock);
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (device->init_recording) {
        /* a batch records only once the one before ran, which freed what that one kept. */
        device->retired = device->garbage;
        device->retired_count = device->garbage_count;
        device->garbage = nullptr;
        device->garbage_count = device->garbage_capacity = 0;
        D12Barrier(device, device->init_cmd);
        device->vk.vkEndCommandBuffer(device->init_cmd);
        device->init_recording = FALSE;
        cmd = device->init_cmd;
        device->init_serial++;
    }
    *serial = device->init_serial;
    LeaveCriticalSection(&device->init_lock);
    return cmd;
}

void D12Barrier(D12Device *device, VkCommandBuffer cmd)
{
    VkMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    device->vk.vkCmdPipelineBarrier2(cmd, &dep);
}

/* ------------------------------------------------------------------ creation */

/*
 * all Direct3D devices of the process share one Vulkan device, see vkprofile.h. it is made for both APIs with the
 * queues of its family, and the first of those queues is left to Direct3D 11.
 */
static HRESULT create_vulkan_device(D12Device *device, IDXGIAdapter *adapter, const MIME_DXGI_VULKAN *v)
{
    MimeVkShared shared;
    HRESULT hr = MimeVkAcquire(adapter, v, &shared);
    if (FAILED(hr))
        return hr;
    const MimeVkProfile *p = shared.profile;
    if (!p->d3d12) {
        MimeVkRelease(&shared);
        return DXGI_ERROR_UNSUPPORTED;
    }
    D12Vk *vk = &device->vk;
    device->shared = shared;
    vk->device = (VkDevice)shared.vk->Device;
    vk->vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)shared.vk->GetDeviceProcAddr;
#define X(name) vk->name = (PFN_##name)vk->vkGetDeviceProcAddr(vk->device, #name);
    D12_VK_DEVICE_FUNCS(X)
#undef X
    vk->vkMxShareExportMemory = (PFN_vkMxShareExportMemory)vk->vkGetDeviceProcAddr(vk->device, "vkMxShareExportMemory");
    vk->vkMxShareImportMemory = (PFN_vkMxShareImportMemory)vk->vkGetDeviceProcAddr(vk->device, "vkMxShareImportMemory");
    vk->vkMxShareExportSemaphore =
        (PFN_vkMxShareExportSemaphore)vk->vkGetDeviceProcAddr(vk->device, "vkMxShareExportSemaphore");
    vk->vkMxShareImportSemaphore =
        (PFN_vkMxShareImportSemaphore)vk->vkGetDeviceProcAddr(vk->device, "vkMxShareImportSemaphore");
#define COPY(field) device->field = p->field
    COPY(has_depth_clip);
    COPY(has_custom_border);
    COPY(has_transform_feedback);
    COPY(has_conditional);
    COPY(has_divisor);
    COPY(has_zero_divisor);
    COPY(has_logic_op);
    COPY(has_depth_bounds);
    COPY(has_geometry);
    COPY(has_null_descriptor);
    COPY(has_tessellation);
    COPY(has_statistics);
    COPY(has_so_queries);
    COPY(has_conservative);
    COPY(has_line_rasterization);
    COPY(has_patch_points);
    COPY(has_unused_attachments);
    COPY(has_native16);
    COPY(has_waves);
    COPY(has_mesh);
    COPY(has_atomic64);
    COPY(has_shared_atomic64);
    COPY(has_barycentrics);
    COPY(has_maintenance5);
    COPY(has_maintenance8);
    COPY(inf_nan_widths);
    COPY(has_compute_derivatives);
    COPY(has_msaa_storage);
    COPY(vrs_tier);
    COPY(vrs_tile);
    COPY(conservative_tier);
    COPY(tiled_tier);
    COPY(wave_size);
    COPY(wave_min);
    COPY(wave_max);
    COPY(wave_stages);
    COPY(vrs_more_rates);
    COPY(vrs_viewports);
    COPY(has_dgc);
    COPY(dgc_stages);
    COPY(has_rt);
    COPY(has_rt_pipeline);
    COPY(db);
#undef COPY
    device->family = shared.vk->QueueFamily;
    device->queue_count = shared.vk->QueueCount;
    for (UINT i = 0; i < device->queue_count; i++)
        device->queues[i] = (VkQueue)shared.vk->Queues[i];
    return S_OK;
}

static HRESULT init_device(D12Device *device, IDXGIAdapter *adapter)
{
    IMimeDXGIAdapter *mime = nullptr;
    if (FAILED(adapter->lpVtbl->QueryInterface(adapter, &IID_IMimeDXGIAdapter, (void **)&mime)))
        return E_INVALIDARG;
    MIME_DXGI_VULKAN v;
    mime->lpVtbl->GetVulkan(mime, &v);
    mime->lpVtbl->Release(mime);
    D12Vk *vk = &device->vk;
    vk->instance = v.Instance;
    vk->physical = v.PhysicalDevice;
    vk->vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)v.GetInstanceProcAddr;
    device->api_version = v.ApiVersion;
    if (!vk->instance || !vk->physical)
        return DXGI_ERROR_UNSUPPORTED;
#define X(name) vk->name = (PFN_##name)vk->vkGetInstanceProcAddr(vk->instance, #name);
    D12_VK_INSTANCE_FUNCS(X)
#undef X
    VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vk->vkGetPhysicalDeviceProperties2(vk->physical, &props);
    device->properties = props.properties;
    vk->vkGetPhysicalDeviceMemoryProperties(vk->physical, &device->memory);

    HRESULT hr = create_vulkan_device(device, adapter, &v);
    if (FAILED(hr))
        return hr;
    for (UINT i = 0; i < D12_MEMORY_KINDS; i++)
        device->buffer_types[i] = ~0u;

    VkFormatProperties d24;
    vk->vkGetPhysicalDeviceFormatProperties(vk->physical, VK_FORMAT_D24_UNORM_S8_UINT, &d24);
    if (!(d24.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        D3dFormatsUseD32();

    VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo sem = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    if (vk->vkCreateSemaphore(vk->device, &sem, nullptr, &device->init_timeline) != VK_SUCCESS)
        return E_FAIL;
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = device->family;
    if (vk->vkCreateCommandPool(vk->device, &pool, nullptr, &device->init_pool) != VK_SUCCESS)
        return E_FAIL;
    VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = device->init_pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    if (vk->vkAllocateCommandBuffers(vk->device, &alloc, &device->boundary_cmd) != VK_SUCCESS)
        return E_FAIL;
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    vk->vkBeginCommandBuffer(device->boundary_cmd, &begin);
    D12Barrier(device, device->boundary_cmd);
    if (vk->vkEndCommandBuffer(device->boundary_cmd) != VK_SUCCESS)
        return E_FAIL;
    if (FAILED(hr = D12DescriptorsInit(device)))
        return hr;
    VkPipelineCacheCreateInfo cache = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    if (vk->vkCreatePipelineCache(vk->device, &cache, nullptr, &device->pipeline_cache) != VK_SUCCESS)
        return E_FAIL;
    if (device->has_rt_pipeline && FAILED(D12RaysInit(device)))
        device->has_rt_pipeline = FALSE;
    VkSemaphoreTypeCreateInfo timeline = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr,
                                          VK_SEMAPHORE_TYPE_TIMELINE, 0};
    VkSemaphoreCreateInfo wake = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timeline};
    return vk->vkCreateSemaphore(vk->device, &wake, nullptr, &device->waiter_wake) == VK_SUCCESS ? S_OK : E_FAIL;
}

void D12FenceWaiterStop(D12Device *device);

static void destroy_device(D12Device *device)
{
    D12Vk *vk = &device->vk;
    D12FenceWaiterStop(device);
    if (vk->device) {
        vk->vkDeviceWaitIdle(vk->device);
        destroy_garbage(device, device->retired, device->retired_count);
        destroy_garbage(device, device->garbage, device->garbage_count);
        D12AccelerationStructuresDestroy(device);
        D12RaysDestroy(device);
        D12DescriptorsDestroy(device);
        D12MemoryDestroy(device);
        vk->vkDestroyPipelineCache(vk->device, device->pipeline_cache, nullptr);
        vk->vkDestroyCommandPool(vk->device, device->init_pool, nullptr);
        vk->vkDestroySemaphore(vk->device, device->init_timeline, nullptr);
        vk->vkDestroySemaphore(vk->device, device->waiter_wake, nullptr);
        MimeVkRelease(&device->shared);
    }
    if (device->adapter)
        device->adapter->lpVtbl->Release(device->adapter);
    HeapFree(GetProcessHeap(), 0, device->address_heaps);
    MimePrivateClear(&device->data);
    DeleteCriticalSection(&device->init_lock);
    HeapFree(GetProcessHeap(), 0, device);
}

/* ------------------------------------------------------------------ format support */

UINT D12FormatSupport(D12Device *device, DXGI_FORMAT format, UINT *support2)
{
    const D3dFormat *f = D3dGetFormat(format);
    if (support2)
        *support2 = 0;
    if (f->vk == VK_FORMAT_UNDEFINED)
        return format == DXGI_FORMAT_UNKNOWN ? D3D12_FORMAT_SUPPORT1_BUFFER : 0;
    VkFormatProperties p;
    VkFormat vkf = (f->flags & (D3D_FORMAT_DEPTH | D3D_FORMAT_STENCIL)) && f->depth ? f->depth : f->vk;
    device->vk.vkGetPhysicalDeviceFormatProperties(device->vk.physical, vkf, &p);
    VkFormatFeatureFlags image = p.optimalTilingFeatures, buffer = p.bufferFeatures;
    UINT s = 0;
    if (f->flags & D3D_FORMAT_TYPELESS) {
        s = D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_TEXTURE3D |
            D3D12_FORMAT_SUPPORT1_TEXTURECUBE | D3D12_FORMAT_SUPPORT1_MIP | D3D12_FORMAT_SUPPORT1_CAST_WITHIN_BIT_LAYOUT;
        if (f->block == 1)
            s |= D3D12_FORMAT_SUPPORT1_BUFFER;
        return image ? s : 0;
    }
    if (buffer & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)
        s |= D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER | D3D12_FORMAT_SUPPORT1_BUFFER;
    if (format == DXGI_FORMAT_R16_UINT || format == DXGI_FORMAT_R32_UINT)
        s |= D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER;
    if (buffer & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT)
        s |= D3D12_FORMAT_SUPPORT1_BUFFER | D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
    if (image & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
        s |= D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_TEXTURECUBE | D3D12_FORMAT_SUPPORT1_MIP |
             D3D12_FORMAT_SUPPORT1_SHADER_LOAD | D3D12_FORMAT_SUPPORT1_CAST_WITHIN_BIT_LAYOUT;
        if (!(f->flags & (D3D_FORMAT_DEPTH | D3D_FORMAT_STENCIL)))
            s |= D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE3D;
        if (!(f->flags & D3D_FORMAT_INT) && (image & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
            s |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_SHADER_GATHER;
        if (f->depth && !(f->flags & D3D_FORMAT_INT))
            s |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON | D3D12_FORMAT_SUPPORT1_SHADER_GATHER_COMPARISON;
    }
    if (image & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) {
        s |= D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET |
             D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD;
        if (!(f->flags & D3D_FORMAT_INT))
            s |= D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE;
        if (image & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
            s |= D3D12_FORMAT_SUPPORT1_BLENDABLE;
    }
    if (image & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        s |= D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL | D3D12_FORMAT_SUPPORT1_TEXTURE2D |
             D3D12_FORMAT_SUPPORT1_TEXTURECUBE | D3D12_FORMAT_SUPPORT1_MIP |
             D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET;
    if ((image & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) || (buffer & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT))
        s |= D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
    if (buffer & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT && f->block == 1 && !(f->flags & D3D_FORMAT_DEPTH))
        s |= D3D12_FORMAT_SUPPORT1_SO_BUFFER;
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        s |= D3D12_FORMAT_SUPPORT1_DISPLAY | D3D12_FORMAT_SUPPORT1_BACK_BUFFER_CAST;
        break;
    default:
        break;
    }
    if (support2) {
        UINT s2 = 0;
        if (s & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) {
            s2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
            if (image & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
                s2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
        }
        if (format == DXGI_FORMAT_R32_UINT || format == DXGI_FORMAT_R32_SINT)
            s2 |= D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS |
                  D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE |
                  D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX |
                  D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX;
        if ((s & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) && (f->flags & D3D_FORMAT_INT) && device->has_logic_op)
            s2 |= D3D12_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP;
        *support2 = s2;
    }
    return s;
}

/* ------------------------------------------------------------------ feature support */

/* the highest feature level reported: 12_1, without tiled resources and rasterizer ordered views. */
#define MAX_FEATURE_LEVEL D3D_FEATURE_LEVEL_12_1

static UINT plane_count(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        return 2;
    default:
        return 1;
    }
}

/* features whose data is a list of dwords: the size and the nonzero dwords. */
typedef struct FeatureWords {
    D3D12_FEATURE feature;
    UINT size;
    UINT words[10];
} FeatureWords;

static void fixed_features(D12Device *device, FeatureWords *list, UINT *count)
{
    UINT n = 0;
#define FEATURE(f, s, ...) list[n++] = (FeatureWords){f, s, {__VA_ARGS__}}
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS2, 8, device->has_depth_bounds, 0);
    /* copy queue timestamps, casting typed formats, WriteBufferImmediate on every list type, barycentrics. */
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS3, 20, TRUE, TRUE, 0xF, 0, device->has_barycentrics);
    FEATURE(D3D12_FEATURE_EXISTING_HEAPS, 4, FALSE);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS4, 12, FALSE, 0, device->has_native16);
    FEATURE(D3D12_FEATURE_CROSS_NODE, 8, 0, FALSE);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS5, 12, FALSE, 0,
            device->has_rt_pipeline ? D3D12_RAYTRACING_TIER_1_1 : D3D12_RAYTRACING_TIER_NOT_SUPPORTED);
    FEATURE(D3D12_FEATURE_DISPLAYABLE, 8, FALSE, 0);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS6, 20, device->vrs_more_rates, device->vrs_viewports, device->vrs_tier,
            device->vrs_tile, FALSE);
    /* sampler feedback at tier 0.9; its maps are storage images of dwords. */
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS7, 8, device->has_mesh ? D3D12_MESH_SHADER_TIER_1 : 0, 90);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS8, 4, TRUE);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS9, 24, FALSE, FALSE, device->has_atomic64, device->has_shared_atomic64, FALSE, 0);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS10, 8, 0);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS11, 4, device->has_atomic64);
    /* enhanced barriers are memory barriers of their scopes; relaxed format casting is what typeless images do. */
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS12, 12, 0, TRUE, TRUE);
    /* a negative viewport height flips y and min depth above max depth flips z, as in vulkan. */
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS13, 24, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE);
    /* advanced texture ops need non-constant offsets, writable msaa textures need msaa storage images. */
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS14, 12, device->has_maintenance8, device->has_msaa_storage, TRUE);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS15, 8, TRUE, TRUE);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS16, 8, TRUE, FALSE);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS17, 8, FALSE, FALSE);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS18, 4, FALSE);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS19, 40, FALSE, 1, FALSE, TRUE, FALSE, FALSE, 2048, 2048, 1000000, FALSE);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS20, 8, FALSE, 0);
    FEATURE(D3D12_FEATURE_D3D12_OPTIONS21, 16, 0, 0, FALSE, FALSE);
    FEATURE(D3D12_FEATURE_PREDICATION, 4, device->has_conditional);
    FEATURE(D3D12_FEATURE_SHADER_CACHE, 4, D3D12_SHADER_CACHE_SUPPORT_SINGLE_PSO);
#undef FEATURE
    *count = n;
}

static HRESULT STDMETHODCALLTYPE device_check_feature_support(ID3D12Device14 *iface, D3D12_FEATURE feature,
                                                              void *data, UINT size)
{
    D12Device *device = impl(iface);
    if (!data)
        return E_INVALIDARG;
#define CHECK(type)                                                                                                    \
    if (size != sizeof(type))                                                                                          \
        return E_INVALIDARG;                                                                                           \
    type *d = data;
    switch (feature) {
    case D3D12_FEATURE_D3D12_OPTIONS: {
        CHECK(D3D12_FEATURE_DATA_D3D12_OPTIONS)
        VkPhysicalDeviceFeatures f;
        VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        device->vk.vkGetPhysicalDeviceFeatures2(device->vk.physical, &f2);
        f = f2.features;
        memset(d, 0, sizeof(*d));
        d->DoublePrecisionFloatShaderOps = f.shaderFloat64;
        d->OutputMergerLogicOp = device->has_logic_op;
        d->MinPrecisionSupport = D3D12_SHADER_MIN_PRECISION_SUPPORT_NONE;
        d->TiledResourcesTier = device->tiled_tier;
        d->ResourceBindingTier = D3D12_RESOURCE_BINDING_TIER_3;
        d->TypedUAVLoadAdditionalFormats = f.shaderStorageImageReadWithoutFormat;
        d->ConservativeRasterizationTier = device->conservative_tier;
        d->MaxGPUVirtualAddressBitsPerResource = 40;
        d->CrossNodeSharingTier = D3D12_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED;
        d->VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation = TRUE;
        d->ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_2;
        return S_OK;
    }
    case D3D12_FEATURE_ARCHITECTURE: {
        CHECK(D3D12_FEATURE_DATA_ARCHITECTURE)
        if (d->NodeIndex)
            return E_INVALIDARG;
        d->TileBasedRenderer = FALSE;
        d->UMA = device->properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        d->CacheCoherentUMA = d->UMA;
        return S_OK;
    }
    case D3D12_FEATURE_ARCHITECTURE1: {
        CHECK(D3D12_FEATURE_DATA_ARCHITECTURE1)
        if (d->NodeIndex)
            return E_INVALIDARG;
        d->TileBasedRenderer = FALSE;
        d->UMA = device->properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        d->CacheCoherentUMA = d->UMA;
        d->IsolatedMMU = TRUE;
        return S_OK;
    }
    case D3D12_FEATURE_FEATURE_LEVELS: {
        CHECK(D3D12_FEATURE_DATA_FEATURE_LEVELS)
        if (!d->NumFeatureLevels || !d->pFeatureLevelsRequested)
            return E_INVALIDARG;
        d->MaxSupportedFeatureLevel = 0;
        for (UINT i = 0; i < d->NumFeatureLevels; i++) {
            D3D_FEATURE_LEVEL level = d->pFeatureLevelsRequested[i];
            if (level <= MAX_FEATURE_LEVEL && level > d->MaxSupportedFeatureLevel)
                d->MaxSupportedFeatureLevel = level;
        }
        return S_OK;
    }
    case D3D12_FEATURE_FORMAT_SUPPORT: {
        CHECK(D3D12_FEATURE_DATA_FORMAT_SUPPORT)
        UINT s2;
        d->Support1 = D12FormatSupport(device, d->Format, &s2);
        d->Support2 = s2;
        if (!d->Support1 && d->Format != DXGI_FORMAT_UNKNOWN)
            return E_FAIL;
        return S_OK;
    }
    case D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS: {
        CHECK(D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS)
        d->NumQualityLevels = 0;
        const D3dFormat *f = D3dGetFormat(d->Format);
        if (d->SampleCount == 1) {
            d->NumQualityLevels = 1;
            return S_OK;
        }
        if (f->vk == VK_FORMAT_UNDEFINED || !d->SampleCount || d->SampleCount > 64 ||
            (d->SampleCount & (d->SampleCount - 1)))
            return S_OK;
        BOOL depth = (f->flags & (D3D_FORMAT_DEPTH | D3D_FORMAT_STENCIL)) != 0;
        VkImageFormatProperties props;
        if (device->vk.vkGetPhysicalDeviceImageFormatProperties(
                device->vk.physical, depth && f->depth ? f->depth : f->vk, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0,
                &props) == VK_SUCCESS &&
            (props.sampleCounts & d->SampleCount))
            d->NumQualityLevels = 1;
        return S_OK;
    }
    case D3D12_FEATURE_FORMAT_INFO: {
        CHECK(D3D12_FEATURE_DATA_FORMAT_INFO)
        if (d->Format != DXGI_FORMAT_UNKNOWN && D3dGetFormat(d->Format)->vk == VK_FORMAT_UNDEFINED)
            return E_INVALIDARG;
        d->PlaneCount = (UINT8)plane_count(d->Format);
        return S_OK;
    }
    case D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT: {
        CHECK(D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT)
        d->MaxGPUVirtualAddressBitsPerResource = 40;
        d->MaxGPUVirtualAddressBitsPerProcess = 40;
        return S_OK;
    }
    case D3D12_FEATURE_SHADER_MODEL: {
        CHECK(D3D12_FEATURE_DATA_SHADER_MODEL)
        if (d->HighestShaderModel < D3D_SHADER_MODEL_5_1 || d->HighestShaderModel > D3D_SHADER_MODEL_6_9)
            return E_INVALIDARG;
        /* dxil up to shader model 6.8 compiles; optional features like mesh and ray tracing are reported elsewhere. */
        if (d->HighestShaderModel > D3D_SHADER_MODEL_6_8)
            d->HighestShaderModel = D3D_SHADER_MODEL_6_8;
        return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS1: {
        CHECK(D3D12_FEATURE_DATA_D3D12_OPTIONS1)
        VkPhysicalDeviceSubgroupProperties sub = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
        VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &sub};
        device->vk.vkGetPhysicalDeviceProperties2(device->vk.physical, &props);
        VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        device->vk.vkGetPhysicalDeviceFeatures2(device->vk.physical, &f2);
        d->WaveOps = device->has_waves;
        d->WaveLaneCountMin = device->wave_min;
        d->WaveLaneCountMax = device->wave_max;
        /* guessed: Vulkan does not say how many lanes run at once. */
        d->TotalLaneCount = sub.subgroupSize * 32;
        d->ExpandedComputeResourceStates = TRUE;
        d->Int64ShaderOps = f2.features.shaderInt64;
        return S_OK;
    }
    case D3D12_FEATURE_ROOT_SIGNATURE: {
        CHECK(D3D12_FEATURE_DATA_ROOT_SIGNATURE)
        if (d->HighestVersion < D3D_ROOT_SIGNATURE_VERSION_1_0 || d->HighestVersion > D3D_ROOT_SIGNATURE_VERSION_1_2)
            return E_INVALIDARG;
        return S_OK;
    }
    case D3D12_FEATURE_COMMAND_QUEUE_PRIORITY: {
        CHECK(D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY)
        d->PriorityForTypeIsSupported =
            d->Priority == D3D12_COMMAND_QUEUE_PRIORITY_NORMAL || d->Priority == D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
        return S_OK;
    }
    case D3D12_FEATURE_SERIALIZATION:
    case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT:
    case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT: {
        /* a node index that must be 0, then a support value that is always 0. */
        if (size != 8)
            return E_INVALIDARG;
        UINT *words = data;
        if (words[0])
            return E_INVALIDARG;
        words[1] = 0;
        return S_OK;
    }
    default:
        break;
    }
#undef CHECK
    FeatureWords list[32];
    UINT count;
    fixed_features(device, list, &count);
    for (UINT i = 0; i < count; i++)
        if (list[i].feature == feature) {
            if (size != list[i].size)
                return E_INVALIDARG;
            memcpy(data, list[i].words, size);
            return S_OK;
        }
    return E_INVALIDARG;
}

/* ------------------------------------------------------------------ IUnknown and ID3D12Object */

static HRESULT STDMETHODCALLTYPE device_query(ID3D12Device14 *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    static const GUID *const ids[] = {
        &IID_IUnknown,       &IID_ID3D12Object,   &IID_ID3D12Device,   &IID_ID3D12Device1,  &IID_ID3D12Device2,
        &IID_ID3D12Device3,  &IID_ID3D12Device4,  &IID_ID3D12Device5,  &IID_ID3D12Device6,  &IID_ID3D12Device7,
        &IID_ID3D12Device8,  &IID_ID3D12Device9,  &IID_ID3D12Device10, &IID_ID3D12Device11, &IID_ID3D12Device12,
        &IID_ID3D12Device13, &IID_ID3D12Device14,
    };
    for (UINT i = 0; i < RTL_NUMBER_OF(ids); i++)
        if (IsEqualGUID(riid, ids[i])) {
            iface->lpVtbl->AddRef(iface);
            *out = iface;
            return S_OK;
        }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE device_add_ref(ID3D12Device14 *iface) { return InterlockedIncrement(&impl(iface)->refs); }

static ULONG STDMETHODCALLTYPE device_release(ID3D12Device14 *iface)
{
    D12Device *device = impl(iface);
    ULONG refs = InterlockedDecrement(&device->refs);
    if (!refs && D12DeviceUnlist(device))
        destroy_device(device);
    return refs;
}

static HRESULT STDMETHODCALLTYPE device_get_private_data(ID3D12Device14 *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE device_set_private_data(ID3D12Device14 *iface, REFGUID guid, UINT size,
                                                         const void *data)
{
    return MimePrivateSet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE device_set_private_data_interface(ID3D12Device14 *iface, REFGUID guid,
                                                                   const IUnknown *data)
{
    return MimePrivateSetInterface(&impl(iface)->data, guid, data);
}

static HRESULT STDMETHODCALLTYPE device_set_name(ID3D12Device14 *iface, LPCWSTR name) { return S_OK; }

/* ------------------------------------------------------------------ objects */

static UINT STDMETHODCALLTYPE device_get_node_count(ID3D12Device14 *iface) { return 1; }

/* returns a new object's interface as asked and drops the creation reference. */
static HRESULT hand_out(HRESULT hr, IUnknown *object, REFIID riid, void **out)
{
    if (FAILED(hr))
        return hr;
    if (!out) {
        object->lpVtbl->Release(object);
        return S_FALSE;
    }
    hr = object->lpVtbl->QueryInterface(object, riid, out);
    object->lpVtbl->Release(object);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_create_command_queue(ID3D12Device14 *iface, const D3D12_COMMAND_QUEUE_DESC *desc,
                                                             REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    if (!desc)
        return E_INVALIDARG;
    D12Queue *queue = nullptr;
    HRESULT hr = D12CreateQueue(impl(iface), desc, &queue);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&queue->iface : nullptr, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_command_queue1(ID3D12Device14 *iface,
                                                              const D3D12_COMMAND_QUEUE_DESC *desc, REFIID creator,
                                                              REFIID riid, void **out)
{
    return device_create_command_queue(iface, desc, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_command_allocator(ID3D12Device14 *iface, D3D12_COMMAND_LIST_TYPE type,
                                                                 REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    D12CommandAllocator *allocator = nullptr;
    HRESULT hr = D12CreateCommandAllocator(impl(iface), type, &allocator);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&allocator->iface : nullptr, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_graphics_pipeline_state(ID3D12Device14 *iface,
                                                                       const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
                                                                       REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    if (!desc)
        return E_INVALIDARG;
    D12PipelineState *state = nullptr;
    HRESULT hr = D12CreateGraphicsPipeline(impl(iface), desc, &state);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&state->iface : nullptr, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_compute_pipeline_state(ID3D12Device14 *iface,
                                                                      const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
                                                                      REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    if (!desc)
        return E_INVALIDARG;
    D12PipelineState *state = nullptr;
    HRESULT hr = D12CreateComputePipeline(impl(iface), desc, &state);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&state->iface : nullptr, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_pipeline_state(ID3D12Device14 *iface,
                                                              const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                                                              REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    if (!desc)
        return E_INVALIDARG;
    D12PipelineState *state = nullptr;
    HRESULT hr = D12CreatePipelineFromStream(impl(iface), desc, &state);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&state->iface : nullptr, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_command_list(ID3D12Device14 *iface, UINT node_mask,
                                                            D3D12_COMMAND_LIST_TYPE type,
                                                            ID3D12CommandAllocator *allocator,
                                                            ID3D12PipelineState *initial, REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    if (!allocator)
        return E_INVALIDARG;
    D12CommandList *list = nullptr;
    HRESULT hr = D12CreateCommandList(impl(iface), type, D12AllocatorFrom(allocator), D12PipelineFrom(initial), &list);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&list->iface : nullptr, riid, out);
}

/* the list starts closed, with no allocator until its first Reset. */
static HRESULT STDMETHODCALLTYPE device_create_command_list1(ID3D12Device14 *iface, UINT node_mask,
                                                             D3D12_COMMAND_LIST_TYPE type,
                                                             D3D12_COMMAND_LIST_FLAGS flags, REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    D12CommandList *list = nullptr;
    HRESULT hr = D12CreateCommandList(impl(iface), type, nullptr, nullptr, &list);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&list->iface : nullptr, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_descriptor_heap(ID3D12Device14 *iface,
                                                               const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid,
                                                               void **out)
{
    if (out)
        *out = nullptr;
    if (!desc)
        return E_INVALIDARG;
    D12DescriptorHeap *heap = nullptr;
    HRESULT hr = D12CreateDescriptorHeap(impl(iface), desc, &heap);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&heap->iface : nullptr, riid, out);
}

static UINT STDMETHODCALLTYPE device_get_descriptor_handle_increment_size(ID3D12Device14 *iface,
                                                                          D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    return type <= D3D12_DESCRIPTOR_HEAP_TYPE_DSV ? D12_INCREMENT : 0;
}

static HRESULT STDMETHODCALLTYPE device_create_root_signature(ID3D12Device14 *iface, UINT node_mask, const void *blob,
                                                              SIZE_T size, REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    D12RootSignature *root = nullptr;
    HRESULT hr = D12CreateRootSignature(impl(iface), blob, size, &root);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&root->iface : nullptr, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_root_signature_from_library(ID3D12Device14 *iface, UINT node_mask,
                                                                           const void *library, SIZE_T size,
                                                                           LPCWSTR subobject, REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE device_create_cbv(ID3D12Device14 *iface, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
                                                D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    D12CreateCbv(impl(iface), desc, handle);
}

static void STDMETHODCALLTYPE device_create_srv(ID3D12Device14 *iface, ID3D12Resource *resource,
                                                const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
                                                D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    D12CreateSrv(impl(iface), D12ResourceFrom(resource), desc, handle);
}

static void STDMETHODCALLTYPE device_create_uav(ID3D12Device14 *iface, ID3D12Resource *resource,
                                                ID3D12Resource *counter, const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
                                                D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    D12CreateUav(impl(iface), D12ResourceFrom(resource), D12ResourceFrom(counter), desc, handle);
}

static void STDMETHODCALLTYPE device_create_rtv(ID3D12Device14 *iface, ID3D12Resource *resource,
                                                const D3D12_RENDER_TARGET_VIEW_DESC *desc,
                                                D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    D12CreateRtv(impl(iface), D12ResourceFrom(resource), desc, handle);
}

static void STDMETHODCALLTYPE device_create_dsv(ID3D12Device14 *iface, ID3D12Resource *resource,
                                                const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
                                                D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    D12CreateDsv(impl(iface), D12ResourceFrom(resource), desc, handle);
}

static void STDMETHODCALLTYPE device_create_sampler(ID3D12Device14 *iface, const D3D12_SAMPLER_DESC *desc,
                                                    D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    if (!desc)
        return;
    D3D12_SAMPLER_DESC2 desc2;
    memcpy(&desc2, desc, sizeof(*desc));
    desc2.Flags = 0;
    D12CreateSampler(impl(iface), &desc2, handle);
}

static void STDMETHODCALLTYPE device_create_sampler2(ID3D12Device14 *iface, const D3D12_SAMPLER_DESC2 *desc,
                                                     D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    if (desc)
        D12CreateSampler(impl(iface), desc, handle);
}

/* a view of the feedback map's 32-bit words, an array when the texture has layers. */
static void STDMETHODCALLTYPE device_create_sampler_feedback_uav(ID3D12Device14 *iface, ID3D12Resource *target,
                                                                 ID3D12Resource *feedback,
                                                                 D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    D12Resource *r = D12ResourceFrom(feedback);
    if (!r || !D12IsFeedback(r->desc.Format))
        return;
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {DXGI_FORMAT_R32_UINT, D3D12_UAV_DIMENSION_TEXTURE2D};
    if (r->layers > 1) {
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.ArraySize = r->layers;
    }
    D12CreateUav(impl(iface), r, nullptr, &desc, handle);
}

/* source and destination ranges are cut wherever either one ends. */
static void STDMETHODCALLTYPE device_copy_descriptors(ID3D12Device14 *iface, UINT dst_count,
                                                      const D3D12_CPU_DESCRIPTOR_HANDLE *dst_starts,
                                                      const UINT *dst_sizes, UINT src_count,
                                                      const D3D12_CPU_DESCRIPTOR_HANDLE *src_starts,
                                                      const UINT *src_sizes, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    D12Device *device = impl(iface);
    UINT di = 0, si = 0, d_done = 0, s_done = 0;
    while (di < dst_count && si < src_count) {
        UINT d_size = dst_sizes ? dst_sizes[di] : 1, s_size = src_sizes ? src_sizes[si] : 1;
        UINT n = D12_MIN(d_size - d_done, s_size - s_done);
        if (n) {
            D3D12_CPU_DESCRIPTOR_HANDLE d = {dst_starts[di].ptr + (SIZE_T)d_done * D12_INCREMENT};
            D3D12_CPU_DESCRIPTOR_HANDLE s = {src_starts[si].ptr + (SIZE_T)s_done * D12_INCREMENT};
            D12CopyDescriptors(device, d, s, n, type);
        }
        d_done += n;
        s_done += n;
        if (d_done >= d_size) {
            di++;
            d_done = 0;
        }
        if (s_done >= s_size) {
            si++;
            s_done = 0;
        }
    }
}

static void STDMETHODCALLTYPE device_copy_descriptors_simple(ID3D12Device14 *iface, UINT count,
                                                             D3D12_CPU_DESCRIPTOR_HANDLE dst,
                                                             D3D12_CPU_DESCRIPTOR_HANDLE src,
                                                             D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    D12CopyDescriptors(impl(iface), dst, src, count, type);
}

/* ------------------------------------------------------------------ resources */

static D3D12_RESOURCE_DESC1 desc1_of(const D3D12_RESOURCE_DESC *desc)
{
    D3D12_RESOURCE_DESC1 d1 = {};
    memcpy(&d1, desc, sizeof(*desc));
    return d1;
}

/* several resources placed one after another, each at its alignment. */
static D3D12_RESOURCE_ALLOCATION_INFO allocation_info(D12Device *device, UINT count, const D3D12_RESOURCE_DESC *descs,
                                                      const D3D12_RESOURCE_DESC1 *descs1,
                                                      D3D12_RESOURCE_ALLOCATION_INFO1 *infos)
{
    D3D12_RESOURCE_ALLOCATION_INFO total = {0, 1};
    for (UINT i = 0; i < count; i++) {
        D3D12_RESOURCE_DESC1 d = descs1 ? descs1[i] : desc1_of(&descs[i]);
        D3D12_RESOURCE_ALLOCATION_INFO info = D12AllocationInfo(device, &d);
        if (info.SizeInBytes == ~0ull) {
            total.SizeInBytes = ~0ull;
            if (infos)
                infos[i] = (D3D12_RESOURCE_ALLOCATION_INFO1){~0ull, info.Alignment, 0};
            continue;
        }
        UINT64 offset = D12_ALIGN(total.SizeInBytes, info.Alignment);
        if (infos)
            infos[i] = (D3D12_RESOURCE_ALLOCATION_INFO1){offset, info.Alignment, info.SizeInBytes};
        if (total.SizeInBytes != ~0ull)
            total.SizeInBytes = offset + info.SizeInBytes;
        total.Alignment = D12_MAX(total.Alignment, info.Alignment);
    }
    return total;
}

static D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE device_get_resource_allocation_info(
    ID3D12Device14 *iface, D3D12_RESOURCE_ALLOCATION_INFO *ret, UINT visible_mask, UINT count,
    const D3D12_RESOURCE_DESC *descs)
{
    *ret = allocation_info(impl(iface), count, descs, nullptr, nullptr);
    return ret;
}

static D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE device_get_resource_allocation_info1(
    ID3D12Device14 *iface, D3D12_RESOURCE_ALLOCATION_INFO *ret, UINT visible_mask, UINT count,
    const D3D12_RESOURCE_DESC *descs, D3D12_RESOURCE_ALLOCATION_INFO1 *infos)
{
    *ret = allocation_info(impl(iface), count, descs, nullptr, infos);
    return ret;
}

static D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE device_get_resource_allocation_info2(
    ID3D12Device14 *iface, D3D12_RESOURCE_ALLOCATION_INFO *ret, UINT visible_mask, UINT count,
    const D3D12_RESOURCE_DESC1 *descs, D3D12_RESOURCE_ALLOCATION_INFO1 *infos)
{
    *ret = allocation_info(impl(iface), count, nullptr, descs, infos);
    return ret;
}

static D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE device_get_resource_allocation_info3(
    ID3D12Device14 *iface, D3D12_RESOURCE_ALLOCATION_INFO *ret, UINT visible_mask, UINT count,
    const D3D12_RESOURCE_DESC1 *descs, const UINT32 *castable_counts, const DXGI_FORMAT *const *castable,
    D3D12_RESOURCE_ALLOCATION_INFO1 *infos)
{
    *ret = allocation_info(impl(iface), count, nullptr, descs, infos);
    return ret;
}

/* on a discrete gpu, upload is write combined and readback is write back. */
static D3D12_HEAP_PROPERTIES *STDMETHODCALLTYPE device_get_custom_heap_properties(ID3D12Device14 *iface,
                                                                                  D3D12_HEAP_PROPERTIES *ret,
                                                                                  UINT node_mask,
                                                                                  D3D12_HEAP_TYPE type)
{
    memset(ret, 0, sizeof(*ret));
    ret->Type = D3D12_HEAP_TYPE_CUSTOM;
    ret->CreationNodeMask = ret->VisibleNodeMask = 1;
    switch (type) {
    case D3D12_HEAP_TYPE_DEFAULT:
        ret->CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
        ret->MemoryPoolPreference = D3D12_MEMORY_POOL_L1;
        break;
    case D3D12_HEAP_TYPE_UPLOAD:
        ret->CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
        ret->MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
        break;
    case D3D12_HEAP_TYPE_READBACK:
        ret->CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
        ret->MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
        break;
    default:
        ret->Type = 0;
        break;
    }
    return ret;
}

static HRESULT create_resource(D12Device *device, const D3D12_HEAP_PROPERTIES *props, D3D12_HEAP_FLAGS flags,
                               const D3D12_RESOURCE_DESC1 *desc, D12Heap *heap, UINT64 offset,
                               D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear, REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    if (!desc)
        return E_INVALIDARG;
    D12Resource *resource = nullptr;
    HRESULT hr = D12CreateResource(device, props, flags, desc, heap, offset, state, clear, &resource);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&resource->iface : nullptr, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_committed_resource(ID3D12Device14 *iface,
                                                                  const D3D12_HEAP_PROPERTIES *props,
                                                                  D3D12_HEAP_FLAGS flags,
                                                                  const D3D12_RESOURCE_DESC *desc,
                                                                  D3D12_RESOURCE_STATES state,
                                                                  const D3D12_CLEAR_VALUE *clear, REFIID riid,
                                                                  void **out)
{
    if (!desc || !props) {
        if (out)
            *out = nullptr;
        return E_INVALIDARG;
    }
    D3D12_RESOURCE_DESC1 d1 = desc1_of(desc);
    return create_resource(impl(iface), props, flags, &d1, nullptr, 0, state, clear, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_committed_resource1(ID3D12Device14 *iface,
                                                                   const D3D12_HEAP_PROPERTIES *props,
                                                                   D3D12_HEAP_FLAGS flags,
                                                                   const D3D12_RESOURCE_DESC *desc,
                                                                   D3D12_RESOURCE_STATES state,
                                                                   const D3D12_CLEAR_VALUE *clear, void *session,
                                                                   REFIID riid, void **out)
{
    return device_create_committed_resource(iface, props, flags, desc, state, clear, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_committed_resource2(ID3D12Device14 *iface,
                                                                   const D3D12_HEAP_PROPERTIES *props,
                                                                   D3D12_HEAP_FLAGS flags,
                                                                   const D3D12_RESOURCE_DESC1 *desc,
                                                                   D3D12_RESOURCE_STATES state,
                                                                   const D3D12_CLEAR_VALUE *clear, void *session,
                                                                   REFIID riid, void **out)
{
    if (!props) {
        if (out)
            *out = nullptr;
        return E_INVALIDARG;
    }
    return create_resource(impl(iface), props, flags, desc, nullptr, 0, state, clear, riid, out);
}

/* every image lives in the general layout, so the initial layout of enhanced barriers is ignored. */
static HRESULT STDMETHODCALLTYPE device_create_committed_resource3(
    ID3D12Device14 *iface, const D3D12_HEAP_PROPERTIES *props, D3D12_HEAP_FLAGS flags,
    const D3D12_RESOURCE_DESC1 *desc, UINT layout, const D3D12_CLEAR_VALUE *clear, void *session, UINT32 castable_count,
    const DXGI_FORMAT *castable, REFIID riid, void **out)
{
    if (!props) {
        if (out)
            *out = nullptr;
        return E_INVALIDARG;
    }
    return create_resource(impl(iface), props, flags, desc, nullptr, 0, D3D12_RESOURCE_STATE_COMMON, clear, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_heap(ID3D12Device14 *iface, const D3D12_HEAP_DESC *desc, REFIID riid,
                                                    void **out)
{
    if (out)
        *out = nullptr;
    if (!desc)
        return E_INVALIDARG;
    D12Heap *heap = nullptr;
    HRESULT hr = D12CreateHeap(impl(iface), desc, &heap);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&heap->iface : nullptr, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_heap1(ID3D12Device14 *iface, const D3D12_HEAP_DESC *desc, void *session,
                                                     REFIID riid, void **out)
{
    return device_create_heap(iface, desc, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_placed_resource(ID3D12Device14 *iface, ID3D12Heap *heap, UINT64 offset,
                                                               const D3D12_RESOURCE_DESC *desc,
                                                               D3D12_RESOURCE_STATES state,
                                                               const D3D12_CLEAR_VALUE *clear, REFIID riid, void **out)
{
    if (!desc || !heap) {
        if (out)
            *out = nullptr;
        return E_INVALIDARG;
    }
    D3D12_RESOURCE_DESC1 d1 = desc1_of(desc);
    D12Heap *h = D12HeapFrom(heap);
    return create_resource(impl(iface), &h->desc.Properties, h->desc.Flags, &d1, h, offset, state, clear, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_placed_resource1(ID3D12Device14 *iface, ID3D12Heap *heap,
                                                                UINT64 offset, const D3D12_RESOURCE_DESC1 *desc,
                                                                D3D12_RESOURCE_STATES state,
                                                                const D3D12_CLEAR_VALUE *clear, REFIID riid,
                                                                void **out)
{
    if (!desc || !heap) {
        if (out)
            *out = nullptr;
        return E_INVALIDARG;
    }
    D12Heap *h = D12HeapFrom(heap);
    return create_resource(impl(iface), &h->desc.Properties, h->desc.Flags, desc, h, offset, state, clear, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_placed_resource2(ID3D12Device14 *iface, ID3D12Heap *heap,
                                                                UINT64 offset, const D3D12_RESOURCE_DESC1 *desc,
                                                                UINT layout, const D3D12_CLEAR_VALUE *clear,
                                                                UINT32 castable_count, const DXGI_FORMAT *castable,
                                                                REFIID riid, void **out)
{
    return device_create_placed_resource1(iface, heap, offset, desc, D3D12_RESOURCE_STATE_COMMON, clear, riid, out);
}

/* supported at the tiled resources tier the options report. */
static HRESULT STDMETHODCALLTYPE device_create_reserved_resource(ID3D12Device14 *iface, const D3D12_RESOURCE_DESC *desc,
                                                                 D3D12_RESOURCE_STATES state,
                                                                 const D3D12_CLEAR_VALUE *clear, REFIID riid,
                                                                 void **out)
{
    if (out)
        *out = nullptr;
    if (!desc)
        return E_INVALIDARG;
    D3D12_RESOURCE_DESC1 d1 = desc1_of(desc);
    D12Resource *resource = nullptr;
    HRESULT hr = D12CreateReservedResource(impl(iface), &d1, state, &resource);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&resource->iface : nullptr, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_reserved_resource1(ID3D12Device14 *iface,
                                                                  const D3D12_RESOURCE_DESC *desc,
                                                                  D3D12_RESOURCE_STATES state,
                                                                  const D3D12_CLEAR_VALUE *clear, void *session,
                                                                  REFIID riid, void **out)
{
    return device_create_reserved_resource(iface, desc, state, clear, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_create_reserved_resource2(ID3D12Device14 *iface,
                                                                  const D3D12_RESOURCE_DESC *desc, UINT layout,
                                                                  const D3D12_CLEAR_VALUE *clear, void *session,
                                                                  UINT32 castable_count, const DXGI_FORMAT *castable,
                                                                  REFIID riid, void **out)
{
    return device_create_reserved_resource(iface, desc, 0, clear, riid, out);
}

static void validation_error(D12Device *device, const char *message)
{
    D12Log("d3d12: %s", message);
}

/*
 * the checks run in the same order as on windows: object and handle are required, the object must be shareable,
 * and fences and heaps need GENERIC_ALL access.
 */
static HRESULT STDMETHODCALLTYPE device_create_shared_handle(ID3D12Device14 *iface, ID3D12DeviceChild *object,
                                                             const SECURITY_ATTRIBUTES *attributes, DWORD access,
                                                             LPCWSTR name, HANDLE *handle)
{
    D12Device *device = impl(iface);
    if (!object || !handle) {
        validation_error(device, object ? "pHandle cannot be NULL." : "pObject cannot be NULL.");
        return E_INVALIDARG;
    }
    ID3D12Fence *fence = nullptr;
    if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_ID3D12Fence, (void **)&fence))) {
        D12Fence *f = D12FenceFrom(fence);
        fence->lpVtbl->Release(fence);
        if (!(f->flags & D3D12_FENCE_FLAG_SHARED)) {
            validation_error(device, "pObject points to a fence that was not created with the D3D12_FENCE_FLAG_SHARED flag.");
            return E_INVALIDARG;
        }
        if (access != GENERIC_ALL) {
            validation_error(device, "Access parameter must be GENERIC_ALL.");
            return E_INVALIDARG;
        }
        return D12FenceShare(f, attributes, name, handle);
    }
    ID3D12Resource *resource = nullptr;
    if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_ID3D12Resource, (void **)&resource))) {
        D12Resource *r = D12ResourceFrom(resource);
        resource->lpVtbl->Release(resource);
        if (!r->committed || r->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ||
            !(r->heap_flags & D3D12_HEAP_FLAG_SHARED)) {
            validation_error(device, !r->committed ? "pObject points to a resource that is not a committed resource. Only committed resources can have a shared handle."
                                                   : "pObject points to a committed resource or a heap that was not created with the D3D12_HEAP_FLAG_SHARED flag.");
            return E_INVALIDARG;
        }
        if (access != GENERIC_ALL) {
            validation_error(device, "Access parameter must be GENERIC_ALL.");
            return E_INVALIDARG;
        }
        return D12ResourceShare(r, attributes, name, handle);
    }
    validation_error(device,
                     "Only Committed Resources, Heaps, Fences, or Protected Sessions can have a shared handle. pObject "
                     "points to none of these DeviceChild types.");
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE device_open_shared_handle(ID3D12Device14 *iface, HANDLE handle, REFIID riid,
                                                           void **out)
{
    D12Device *device = impl(iface);
    UINT kind = 0;
    /* legacy shared handles have bit 1 set, which no NT handle has, and name Direct3D 11 resources. */
    if ((ULONG_PTR)handle & MIME_LEGACY_HANDLE_BIT) {
        HANDLE nt;
        HRESULT opened = MimeShareOpenLegacy(MimeLegacyNumber(handle), &nt);
        if (FAILED(opened))
            return opened;
        HRESULT hr = D12OpenSharedResource(device, nt, riid, out);
        CloseHandle(nt);
        return hr;
    }
    HRESULT hr = MimeShareKind(handle, &kind);
    if (FAILED(hr))
        return hr;
    if (kind == MIME_GFXSHARE_SYNC) {
        D12Fence *fence;
        hr = D12FenceOpen(device, handle, &fence);
        if (FAILED(hr))
            return hr;
        hr = fence->iface.lpVtbl->QueryInterface(&fence->iface, riid, out);
        fence->iface.lpVtbl->Release(&fence->iface);
        return hr;
    }
    return D12OpenSharedResource(device, handle, riid, out);
}

/*
 * like windows, any access but GENERIC_ALL fails before the lookup, and a name is tried as a resource, then as a
 * fence.
 */
static HRESULT STDMETHODCALLTYPE device_open_shared_handle_by_name(ID3D12Device14 *iface, LPCWSTR name, DWORD access,
                                                                   HANDLE *handle)
{
    if (!handle)
        return E_INVALIDARG;
    *handle = nullptr;
    if (!name || access != GENERIC_ALL)
        return E_INVALIDARG;
    HRESULT hr = MimeShareOpenName(name, MIME_GFXSHARE_RESOURCE, handle);
    if (FAILED(hr))
        hr = MimeShareOpenName(name, MIME_GFXSHARE_SYNC, handle);
    if (FAILED(hr))
        *handle = nullptr;
    return hr;
}

/* residency is the host driver's: everything stays resident. */
static HRESULT STDMETHODCALLTYPE device_make_resident(ID3D12Device14 *iface, UINT count,
                                                      ID3D12Pageable *const *objects)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_enqueue_make_resident(ID3D12Device14 *iface, UINT flags, UINT count,
                                                              ID3D12Pageable *const *objects, ID3D12Fence *fence,
                                                              UINT64 value)
{
    return fence ? fence->lpVtbl->Signal(fence, value) : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE device_set_residency_priority(ID3D12Device14 *iface, UINT count,
                                                               ID3D12Pageable *const *objects, const UINT *priorities)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_create_fence(ID3D12Device14 *iface, UINT64 initial, D3D12_FENCE_FLAGS flags,
                                                     REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    D12Fence *fence = nullptr;
    HRESULT hr = D12CreateFence(impl(iface), initial, flags, &fence);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&fence->iface : nullptr, riid, out);
}

HRESULT D12SetEventOnFences(D12Device *device, ID3D12Fence *const *fences, const UINT64 *values, UINT count,
                            UINT flags, HANDLE event);

static HRESULT STDMETHODCALLTYPE device_set_event_on_multiple_fence_completion(ID3D12Device14 *iface,
                                                                               ID3D12Fence *const *fences,
                                                                               const UINT64 *values, UINT count,
                                                                               UINT flags, HANDLE event)
{
    return D12SetEventOnFences(impl(iface), fences, values, count, flags, event);
}

static HRESULT STDMETHODCALLTYPE device_get_device_removed_reason(ID3D12Device14 *iface)
{
    return impl(iface)->removed;
}

static void STDMETHODCALLTYPE device_remove_device(ID3D12Device14 *iface)
{
    impl(iface)->removed = DXGI_ERROR_DEVICE_REMOVED;
}

static void STDMETHODCALLTYPE device_get_copyable_footprints(ID3D12Device14 *iface, const D3D12_RESOURCE_DESC *desc,
                                                             UINT first, UINT count, UINT64 base,
                                                             D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts, UINT *rows,
                                                             UINT64 *row_size, UINT64 *total)
{
    D3D12_RESOURCE_DESC1 d1 = desc1_of(desc);
    D12Footprints(impl(iface), &d1, first, count, base, layouts, rows, row_size, total);
}

static void STDMETHODCALLTYPE device_get_copyable_footprints1(ID3D12Device14 *iface, const D3D12_RESOURCE_DESC1 *desc,
                                                              UINT first, UINT count, UINT64 base,
                                                              D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts,
                                                              UINT *rows, UINT64 *row_size, UINT64 *total)
{
    D12Footprints(impl(iface), desc, first, count, base, layouts, rows, row_size, total);
}

static HRESULT STDMETHODCALLTYPE device_create_query_heap(ID3D12Device14 *iface, const D3D12_QUERY_HEAP_DESC *desc,
                                                          REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    if (!desc)
        return E_INVALIDARG;
    D12QueryHeap *heap = nullptr;
    HRESULT hr = D12CreateQueryHeap(impl(iface), desc, &heap);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&heap->iface : nullptr, riid, out);
}

static HRESULT STDMETHODCALLTYPE device_set_stable_power_state(ID3D12Device14 *iface, BOOL enable) { return S_OK; }

static HRESULT STDMETHODCALLTYPE device_create_command_signature(ID3D12Device14 *iface,
                                                                 const D3D12_COMMAND_SIGNATURE_DESC *desc,
                                                                 ID3D12RootSignature *root, REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    if (!desc)
        return E_INVALIDARG;
    D12CommandSignature *signature = nullptr;
    HRESULT hr = D12CreateCommandSignature(impl(iface), desc, D12RootSignatureFrom(root), &signature);
    return hand_out(hr, SUCCEEDED(hr) ? (IUnknown *)&signature->iface : nullptr, riid, out);
}

static void STDMETHODCALLTYPE device_get_resource_tiling(ID3D12Device14 *iface, ID3D12Resource *resource,
                                                         UINT *tile_count, D3D12_PACKED_MIP_INFO *packed,
                                                         D3D12_TILE_SHAPE *shape, UINT *subresource_count, UINT first,
                                                         D3D12_SUBRESOURCE_TILING *tilings)
{
    D12Resource *r = D12ResourceFrom(resource);
    if (r) {
        D12ResourceTiling(r, tile_count, packed, shape, subresource_count, first, tilings);
        return;
    }
    if (tile_count)
        *tile_count = 0;
    if (packed)
        memset(packed, 0, sizeof(*packed));
    if (shape)
        memset(shape, 0, sizeof(*shape));
    if (subresource_count)
        *subresource_count = 0;
}

static LUID *STDMETHODCALLTYPE device_get_adapter_luid(ID3D12Device14 *iface, LUID *ret)
{
    *ret = impl(iface)->adapter_luid;
    return ret;
}

/* pipeline libraries need a driver that serializes pipelines, so this reports them unsupported like windows does. */
static HRESULT STDMETHODCALLTYPE device_create_pipeline_library(ID3D12Device14 *iface, const void *blob, SIZE_T size,
                                                                REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE device_open_existing_heap_from_address(ID3D12Device14 *iface, const void *address,
                                                                        REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE device_open_existing_heap_from_address1(ID3D12Device14 *iface, const void *address,
                                                                         SIZE_T size, REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE device_open_existing_heap_from_file_mapping(ID3D12Device14 *iface, HANDLE mapping,
                                                                             REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE device_create_protected_session(ID3D12Device14 *iface, const void *desc, REFIID riid,
                                                                 void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE device_create_lifetime_tracker(ID3D12Device14 *iface, void *owner, REFIID riid,
                                                                void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE device_enumerate_meta_commands(ID3D12Device14 *iface, UINT *count, void *descs)
{
    if (!count)
        return E_INVALIDARG;
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_enumerate_meta_command_parameters(ID3D12Device14 *iface, REFGUID command,
                                                                          UINT stage, UINT *total_size, UINT *count,
                                                                          void *descs)
{
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE device_create_meta_command(ID3D12Device14 *iface, REFGUID command, UINT node_mask,
                                                            const void *data, SIZE_T size, REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE device_create_state_object(ID3D12Device14 *iface, const D3D12_STATE_OBJECT_DESC *desc,
                                                            REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    D12StateObject *so;
    HRESULT hr = D12CreateStateObject(impl(iface), desc, nullptr, &so);
    if (FAILED(hr))
        return hr;
    ID3D12StateObject *object = (ID3D12StateObject *)so;
    hr = object->lpVtbl->QueryInterface(object, riid, out);
    object->lpVtbl->Release(object);
    return hr;
}

static void STDMETHODCALLTYPE device_get_raytracing_prebuild_info(
    ID3D12Device14 *iface, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *desc,
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO *info)
{
    if (desc && info)
        D12PrebuildInfo(impl(iface), desc, info);
}

static UINT STDMETHODCALLTYPE device_check_driver_matching_identifier(ID3D12Device14 *iface, UINT type,
                                                                      const void *identifier)
{
    return identifier ? D12CheckDriverMatchingIdentifier(impl(iface), type, identifier) : 2;
}

static HRESULT STDMETHODCALLTYPE device_set_background_processing_mode(ID3D12Device14 *iface, UINT mode, UINT action,
                                                                       HANDLE event, BOOL *further)
{
    if (further)
        *further = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_add_to_state_object(ID3D12Device14 *iface,
                                                            const D3D12_STATE_OBJECT_DESC *addition,
                                                            ID3D12StateObject *state, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    D12StateObject *parent = D12StateObjectFrom(state), *so;
    if (!parent)
        return E_INVALIDARG;
    HRESULT hr = D12CreateStateObject(impl(iface), addition, parent, &so);
    if (FAILED(hr))
        return hr;
    ID3D12StateObject *object = (ID3D12StateObject *)so;
    hr = object->lpVtbl->QueryInterface(object, riid, out);
    object->lpVtbl->Release(object);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_create_shader_cache_session(ID3D12Device14 *iface, const void *desc,
                                                                    REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
}

static HRESULT STDMETHODCALLTYPE device_shader_cache_control(ID3D12Device14 *iface, UINT kinds, UINT control)
{
    return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
}

static const struct ID3D12Device14Vtbl g_device_vtbl = {
    device_query,
    device_add_ref,
    device_release,
    device_get_private_data,
    device_set_private_data,
    device_set_private_data_interface,
    device_set_name,
    device_get_node_count,
    device_create_command_queue,
    device_create_command_allocator,
    device_create_graphics_pipeline_state,
    device_create_compute_pipeline_state,
    device_create_command_list,
    device_check_feature_support,
    device_create_descriptor_heap,
    device_get_descriptor_handle_increment_size,
    device_create_root_signature,
    device_create_cbv,
    device_create_srv,
    device_create_uav,
    device_create_rtv,
    device_create_dsv,
    device_create_sampler,
    device_copy_descriptors,
    device_copy_descriptors_simple,
    device_get_resource_allocation_info,
    device_get_custom_heap_properties,
    device_create_committed_resource,
    device_create_heap,
    device_create_placed_resource,
    device_create_reserved_resource,
    device_create_shared_handle,
    device_open_shared_handle,
    device_open_shared_handle_by_name,
    device_make_resident,
    device_make_resident,
    device_create_fence,
    device_get_device_removed_reason,
    device_get_copyable_footprints,
    device_create_query_heap,
    device_set_stable_power_state,
    device_create_command_signature,
    device_get_resource_tiling,
    device_get_adapter_luid,
    device_create_pipeline_library,
    device_set_event_on_multiple_fence_completion,
    device_set_residency_priority,
    device_create_pipeline_state,
    device_open_existing_heap_from_address,
    device_open_existing_heap_from_file_mapping,
    device_enqueue_make_resident,
    device_create_command_list1,
    device_create_protected_session,
    device_create_committed_resource1,
    device_create_heap1,
    device_create_reserved_resource1,
    device_get_resource_allocation_info1,
    device_create_lifetime_tracker,
    device_remove_device,
    device_enumerate_meta_commands,
    device_enumerate_meta_command_parameters,
    device_create_meta_command,
    device_create_state_object,
    device_get_raytracing_prebuild_info,
    device_check_driver_matching_identifier,
    device_set_background_processing_mode,
    device_add_to_state_object,
    device_create_protected_session,
    device_get_resource_allocation_info2,
    device_create_committed_resource2,
    device_create_placed_resource1,
    device_create_sampler_feedback_uav,
    device_get_copyable_footprints1,
    device_create_shader_cache_session,
    device_shader_cache_control,
    device_create_command_queue1,
    device_create_committed_resource3,
    device_create_placed_resource2,
    device_create_reserved_resource2,
    device_create_sampler2,
    device_get_resource_allocation_info3,
    device_open_existing_heap_from_address1,
    device_create_root_signature_from_library,
};

HRESULT D12CreateDevice(IDXGIAdapter *adapter, D3D_FEATURE_LEVEL level, D12Device **out)
{
    if (level > MAX_FEATURE_LEVEL)
        return DXGI_ERROR_UNSUPPORTED;
    D12Device *device = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*device));
    if (!device)
        return E_OUTOFMEMORY;
    device->iface.lpVtbl = &g_device_vtbl;
    device->refs = 1;
    device->feature_level = MAX_FEATURE_LEVEL;
    device->adapter = adapter;
    adapter->lpVtbl->AddRef(adapter);
    InitializeCriticalSection(&device->init_lock);
    HRESULT hr = init_device(device, adapter);
    if (FAILED(hr)) {
        destroy_device(device);
        return hr;
    }
    *out = device;
    return S_OK;
}
