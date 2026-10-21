/*
 * the device: the vulkan device of an adapter with what direct3d 11 needs
 * enabled, its queue and timeline, and ID3D11Device5 with the dxgi device
 * and multithread interfaces it also answers to.
 */
#include "d3d11_private.h"

HRESULT D3dHeapInit(D3dDevice *device);
void D3dHeapDestroy(D3dDevice *device);
void D3dCollectGarbage(D3dDevice *device);

D3dDevice *D3dDeviceFrom(ID3D11Device *iface) { return CONTAINING_RECORD(iface, D3dDevice, iface); }
static D3dDevice *impl(ID3D11Device5 *iface) { return CONTAINING_RECORD(iface, D3dDevice, iface); }

void D3dLog(const char *format, ...)
{
    char text[512];
    va_list ap;
    va_start(ap, format);
    int n = _vsnprintf(text, sizeof(text) - 2, format, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof(text) - 2)
        n = sizeof(text) - 2;
    text[n++] = '\n';
    text[n] = 0;
    OutputDebugStringA(text);
}

void D3dChildInit(D3dChild *child, D3dDevice *device)
{
    child->refs = 1;
    child->device = device;
    device->iface.lpVtbl->AddRef(&device->iface);
}

void D3dChildRelease(D3dChild *child)
{
    MimePrivateClear(&child->data);
    child->device->iface.lpVtbl->Release(&child->device->iface);
}

/* ------------------------------------------------------------------ the queue and timeline */

UINT64 D3dCompleted(D3dDevice *device)
{
    UINT64 value = device->completed;
    if (value + 1 >= device->next_serial)
        return value;
    if (device->vk.vkGetSemaphoreCounterValue(device->vk.device, device->timeline, &value) == VK_SUCCESS &&
        value > device->completed) {
        device->completed = value;
        D3dCollectGarbage(device);
    }
    return device->completed;
}

void D3dWaitSerial(D3dDevice *device, UINT64 serial)
{
    if (serial >= device->next_serial)
        serial = device->next_serial - 1;
    if (serial <= device->completed)
        return;
    VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &device->timeline;
    wait.pValues = &serial;
    if (device->vk.vkWaitSemaphores(device->vk.device, &wait, UINT64_MAX) == VK_SUCCESS && serial > device->completed) {
        device->completed = serial;
        D3dCollectGarbage(device);
    }
}

/*
 * submits the init commands, then cmd if any, waiting and signalling
 * beyond the timeline as asked. returns the serial the submission
 * signals.
 */
UINT64 D3dSubmit(D3dDevice *device, VkCommandBuffer cmd, const VkSemaphoreSubmitInfo *wait,
                 const VkSemaphoreSubmitInfo *signal)
{
    VkCommandBuffer init = D3dTakeInitCommands(device);
    AcquireSRWLockExclusive(device->queue_lock);
    UINT64 serial = device->next_serial;
    VkCommandBufferSubmitInfo buffers[2];
    UINT count = 0;
    if (init)
        buffers[count++] = (VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, init};
    if (cmd)
        buffers[count++] = (VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, cmd};
    VkSemaphoreSubmitInfo signals[2] = {{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}};
    signals[0].semaphore = device->timeline;
    signals[0].value = serial;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    if (signal)
        signals[1] = *signal;
    /* pictures decoded since the last submission are what this one may read. */
    VkSemaphoreSubmitInfo waits[2 + D3D_MAX_FENCE_WAITS];
    UINT wait_count = 0;
    if (wait)
        waits[wait_count++] = *wait;
    /* fences the program waited for since the last submission, which this one comes after. */
    for (UINT i = 0; i < device->fence_wait_count; i++)
        waits[wait_count++] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
                                                      device->fence_waits[i].semaphore, device->fence_waits[i].value,
                                                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    device->fence_wait_count = 0;
    if (device->video_value > device->video_waited) {
        waits[wait_count++] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
                                                      device->video_timeline, device->video_value,
                                                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
        device->video_waited = device->video_value;
    }
    VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = wait_count;
    submit.pWaitSemaphoreInfos = waits;
    submit.commandBufferInfoCount = count;
    submit.pCommandBufferInfos = buffers;
    submit.signalSemaphoreInfoCount = signal ? 2 : 1;
    submit.pSignalSemaphoreInfos = signals;
    if (device->vk.vkQueueSubmit2(device->queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
        D3dLog("d3d11: vkQueueSubmit2 failed");
    device->next_serial = serial + 1;
    device->renames = 0;
    ReleaseSRWLockExclusive(device->queue_lock);
    if (init)
        D3dInitDone(device);
    return serial;
}

/* the next submission waits for the fence to reach the value; a full list is submitted first. */
void D3dQueueWait(D3dDevice *device, VkSemaphore semaphore, UINT64 value)
{
    for (;;) {
        AcquireSRWLockExclusive(device->queue_lock);
        if (device->fence_wait_count < D3D_MAX_FENCE_WAITS) {
            device->fence_waits[device->fence_wait_count++] = (typeof(device->fence_waits[0])){semaphore, value};
            ReleaseSRWLockExclusive(device->queue_lock);
            return;
        }
        ReleaseSRWLockExclusive(device->queue_lock);
        D3dSubmit(device, VK_NULL_HANDLE, nullptr, nullptr);
    }
}

/* the commands of the device's own work, recorded under the init lock until D3dInitDone. */
VkCommandBuffer D3dInitCommands(D3dDevice *device)
{
    D3dVk *vk = &device->vk;
    EnterCriticalSection(&device->init_lock);
    if (!device->init_recording) {
        D3dCommandBuffer *c = &device->init_cmd;
        if (c->buffer)
            D3dWaitSerial(device, c->serial);
        else {
            VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            alloc.commandPool = device->init_pool;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;
            vk->vkAllocateCommandBuffers(vk->device, &alloc, &c->buffer);
        }
        vk->vkResetCommandBuffer(c->buffer, 0);
        VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk->vkBeginCommandBuffer(c->buffer, &begin);
        device->init_recording = TRUE;
    }
    return device->init_cmd.buffer;
}

/* leaves the init lock taken by D3dInitCommands. */
void D3dInitDone(D3dDevice *device) { LeaveCriticalSection(&device->init_lock); }

/* ends the init commands for submission, leaving the lock held until D3dInitDone; NULL with nothing recorded. */
VkCommandBuffer D3dTakeInitCommands(D3dDevice *device)
{
    EnterCriticalSection(&device->init_lock);
    if (!device->init_recording) {
        LeaveCriticalSection(&device->init_lock);
        return VK_NULL_HANDLE;
    }
    D3dBarrier(device->init_cmd.buffer, device);
    device->vk.vkEndCommandBuffer(device->init_cmd.buffer);
    device->init_recording = FALSE;
    device->init_cmd.serial = device->next_serial;
    return device->init_cmd.buffer;
}

/* a full memory barrier: what came before is written and visible to what comes after. */
void D3dBarrier(VkCommandBuffer cmd, D3dDevice *device)
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
 * the host serves one vulkan device to a process, so the direct3d 11 and 12 devices of a process share it with their
 * queues, which they take turns on: the first to ask makes it, see vkprofile.h. It goes with the last of them.
 */
static HRESULT acquire_vulkan_device(D3dDevice *device, IDXGIAdapter *adapter, const MIME_DXGI_VULKAN *v)
{
    MimeVkShared shared;
    HRESULT hr = MimeVkAcquire(adapter, v, &shared);
    if (FAILED(hr))
        return hr;
    const MimeVkProfile *p = shared.profile;
    if (!p->d3d11) {
        MimeVkRelease(&shared);
        return DXGI_ERROR_UNSUPPORTED;
    }
    D3dVk *vk = &device->vk;
    device->shared = shared;
    vk->device = (VkDevice)shared.vk->Device;
    vk->vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)shared.vk->GetDeviceProcAddr;
#define X(name) vk->name = (PFN_##name)vk->vkGetDeviceProcAddr(vk->device, #name);
    D3D_VK_DEVICE_FUNCS(X)
#undef X
    vk->vkMimeShareExportMemory = (PFN_vkMimeShareExportMemory)vk->vkGetDeviceProcAddr(vk->device, "vkMimeShareExportMemory");
    vk->vkMimeShareImportMemory = (PFN_vkMimeShareImportMemory)vk->vkGetDeviceProcAddr(vk->device, "vkMimeShareImportMemory");
    vk->vkMimeShareExportSemaphore =
        (PFN_vkMimeShareExportSemaphore)vk->vkGetDeviceProcAddr(vk->device, "vkMimeShareExportSemaphore");
    vk->vkMimeShareImportSemaphore =
        (PFN_vkMimeShareImportSemaphore)vk->vkGetDeviceProcAddr(vk->device, "vkMimeShareImportSemaphore");
    /* direct3d 11 submits on the first queue of the device, whose lock the other devices of the process take. */
    device->queue = (VkQueue)shared.vk->Queues[0];
    device->queue_lock = &shared.vk->QueueLocks[0];
    device->queue_family = shared.vk->QueueFamily;
#define COPY(field) device->field = p->field
    COPY(has_depth_clip);
    COPY(has_custom_border);
    COPY(has_transform_feedback);
    COPY(has_conditional);
    COPY(has_line_rasterization);
    COPY(has_divisor);
    COPY(has_zero_divisor);
    COPY(has_statistics);
    COPY(has_precise_occlusion);
    COPY(has_so_queries);
    COPY(has_logic_op);
    COPY(has_geometry);
    COPY(has_tessellation);
    COPY(has_video);
    COPY(has_vrs);
    COPY(has_conservative);
    COPY(has_mesh);
#undef COPY
    /* direct3d 11 has no depth bounds, and the device may enable them for direct3d 12. */
    device->has_depth_bounds = FALSE;
    if (device->has_video) {
        vk->vkMimeVideoGetCaps = (PFN_vkMimeVideoGetCaps)vk->vkGetDeviceProcAddr(vk->device, "vkMimeVideoGetCaps");
        vk->vkMimeVideoCreateSession =
            (PFN_vkMimeVideoCreateSession)vk->vkGetDeviceProcAddr(vk->device, "vkMimeVideoCreateSession");
        vk->vkMimeVideoDestroySession =
            (PFN_vkMimeVideoDestroySession)vk->vkGetDeviceProcAddr(vk->device, "vkMimeVideoDestroySession");
        vk->vkMimeVideoDecode = (PFN_vkMimeVideoDecode)vk->vkGetDeviceProcAddr(vk->device, "vkMimeVideoDecode");
    }
    return S_OK;
}

/* drops a device's hold, the vulkan device going with the last. */
static void release_vulkan_device(D3dDevice *device) { MimeVkRelease(&device->shared); }

static HRESULT init_device(D3dDevice *device, IDXGIAdapter *adapter)
{
    IMimeDXGIAdapter *mime = nullptr;
    if (FAILED(adapter->lpVtbl->QueryInterface(adapter, &IID_IMimeDXGIAdapter, (void **)&mime)))
        return E_INVALIDARG;
    MIME_DXGI_VULKAN v;
    mime->lpVtbl->GetVulkan(mime, &v);
    mime->lpVtbl->Release(mime);
    D3dVk *vk = &device->vk;
    vk->instance = v.Instance;
    vk->physical = v.PhysicalDevice;
    vk->vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)v.GetInstanceProcAddr;
    device->api_version = v.ApiVersion;
    if (!vk->instance || !vk->physical) {
        D3dLog("d3d11: the adapter has no vulkan device");
        return DXGI_ERROR_UNSUPPORTED;
    }
#define X(name) vk->name = (PFN_##name)vk->vkGetInstanceProcAddr(vk->instance, #name);
    D3D_VK_INSTANCE_FUNCS(X)
#undef X
    VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vk->vkGetPhysicalDeviceProperties2(vk->physical, &props);
    device->properties = props.properties;
    vk->vkGetPhysicalDeviceMemoryProperties(vk->physical, &device->memory);

    HRESULT hr = acquire_vulkan_device(device, adapter, &v);
    if (FAILED(hr))
        return hr;

    VkFormatProperties d24;
    vk->vkGetPhysicalDeviceFormatProperties(vk->physical, VK_FORMAT_D24_UNORM_S8_UINT, &d24);
    if (!(d24.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        D3dFormatsUseD32();

    VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo sem = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    if (vk->vkCreateSemaphore(vk->device, &sem, nullptr, &device->timeline) != VK_SUCCESS)
        return E_FAIL;
    device->next_serial = 1;

    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = device->queue_family;
    if (vk->vkCreateCommandPool(vk->device, &pool, nullptr, &device->init_pool) != VK_SUCCESS)
        return E_FAIL;

    if (FAILED(hr = D3dMemoryInit(device)) || FAILED(hr = D3dHeapInit(device)))
        return hr;
    if (device->has_transform_feedback &&
        FAILED(hr = D3dAllocBuffer(device, D3D_MEMORY_DEVICE, 16, 16, &device->so_dummy)))
        return hr;
    D3dVideoDeviceInit(device);
    return D3dCreateContext(device, D3D11_DEVICE_CONTEXT_IMMEDIATE, &device->immediate);
}

static void destroy_device(D3dDevice *device)
{
    D3dVk *vk = &device->vk;
    if (vk->device) {
        vk->vkDeviceWaitIdle(vk->device);
        device->completed = device->next_serial;
        if (device->immediate)
            D3dContextDestroy(device->immediate);
        D3dCollectGarbage(device);
        D3dVideoDeviceDestroy(device);
        D3dQueryPoolsDestroy(device);
        if (device->so_dummy.buffer)
            D3dFree(device, &device->so_dummy);
        D3dMemoryDestroy(device);
        D3dHeapDestroy(device);
        vk->vkDestroySampler(vk->device, device->null_sampler, nullptr);
        vk->vkDestroyDescriptorPool(vk->device, device->heap_pool, nullptr);
        vk->vkDestroyDescriptorPool(vk->device, device->table_pool, nullptr);
        vk->vkDestroyPipelineLayout(vk->device, device->pipeline_layout, nullptr);
        vk->vkDestroyDescriptorSetLayout(vk->device, device->heap_layout, nullptr);
        vk->vkDestroyDescriptorSetLayout(vk->device, device->table_layout, nullptr);
        vk->vkDestroyCommandPool(vk->device, device->init_pool, nullptr);
        vk->vkDestroySemaphore(vk->device, device->timeline, nullptr);
        release_vulkan_device(device);
    }
    if (device->adapter)
        device->adapter->lpVtbl->Release(device->adapter);
    MimePrivateClear(&device->data);
    DeleteCriticalSection(&device->init_lock);
    DeleteCriticalSection(&device->state_lock);
    HeapFree(GetProcessHeap(), 0, device);
}

/* ------------------------------------------------------------------ IUnknown */

static const GUID IID_ID3D11Debug = {0x79CF2233, 0x7536, 0x4948, {0x9D, 0x36, 0x1E, 0x46, 0x92, 0xDC, 0x57, 0x60}};

static HRESULT STDMETHODCALLTYPE device_query(ID3D11Device5 *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    D3dDevice *device = impl(iface);
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11Device) ||
        IsEqualGUID(riid, &IID_ID3D11Device1) || IsEqualGUID(riid, &IID_ID3D11Device2) ||
        IsEqualGUID(riid, &IID_ID3D11Device3) || IsEqualGUID(riid, &IID_ID3D11Device4) ||
        IsEqualGUID(riid, &IID_ID3D11Device5))
        *out = &device->iface;
    else if (IsEqualGUID(riid, &IID_IDXGIObject) || IsEqualGUID(riid, &IID_IDXGIDevice) ||
             IsEqualGUID(riid, &IID_IDXGIDevice1) || IsEqualGUID(riid, &IID_IDXGIDevice2) ||
             IsEqualGUID(riid, &IID_IDXGIDevice3) || IsEqualGUID(riid, &IID_IDXGIDevice4))
        *out = &device->dxgi;
    else if (IsEqualGUID(riid, &IID_IMimeDXGIPresenter))
        *out = &device->presenter;
    else if (IsEqualGUID(riid, &IID_IMimeD3D11Device))
        *out = &device->mime;
    else if (IsEqualGUID(riid, &IID_ID3D11Multithread))
        *out = (device->flags & D3D11_CREATE_DEVICE_SINGLETHREADED) ? nullptr : &device->multithread;
    else if ((*out = D3dVideoDeviceInterface(device, riid)) != nullptr)
        ;
    else
        *out = D3d10DeviceInterface(device, riid);
    if (!*out) {
        if (!IsEqualGUID(riid, &IID_ID3D11Debug))
            D3dLog("d3d11: no device interface {%08x-%04x-%04x}", riid->Data1, riid->Data2, riid->Data3);
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&device->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE device_add_ref(ID3D11Device5 *iface) { return InterlockedIncrement(&impl(iface)->refs); }

static ULONG STDMETHODCALLTYPE device_release(ID3D11Device5 *iface)
{
    D3dDevice *device = impl(iface);
    ULONG refs = InterlockedDecrement(&device->refs);
    if (!refs)
        destroy_device(device);
    return refs;
}

/* ------------------------------------------------------------------ creation methods */

static HRESULT STDMETHODCALLTYPE device_create_buffer(ID3D11Device5 *iface, const D3D11_BUFFER_DESC *desc,
                                                      const D3D11_SUBRESOURCE_DATA *data, ID3D11Buffer **buffer)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc)
        return E_INVALIDARG;
    D3dResource *resource;
    HRESULT hr = D3dCreateBuffer(impl(iface), desc, data, buffer ? &resource : nullptr);
    if (SUCCEEDED(hr) && buffer)
        *buffer = &resource->u.buffer;
    return buffer || FAILED(hr) ? hr : S_FALSE;
}

static HRESULT create_texture(ID3D11Device5 *iface, D3D11_RESOURCE_DIMENSION dimension, const void *desc,
                              const D3D11_SUBRESOURCE_DATA *data, void **out)
{
    if (!desc)
        return E_INVALIDARG;
    D3dResource *resource;
    HRESULT hr = D3dCreateTexture(impl(iface), dimension, desc, data, out ? &resource : nullptr);
    if (SUCCEEDED(hr) && out)
        *out = resource->iface;
    return out || FAILED(hr) ? hr : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE device_create_texture1d(ID3D11Device5 *iface, const D3D11_TEXTURE1D_DESC *desc,
                                                         const D3D11_SUBRESOURCE_DATA *data,
                                                         ID3D11Texture1D **texture)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_texture(iface, D3D11_RESOURCE_DIMENSION_TEXTURE1D, desc, data, (void **)texture);
}

static HRESULT STDMETHODCALLTYPE device_create_texture2d1(ID3D11Device5 *iface, const D3D11_TEXTURE2D_DESC1 *desc,
                                                          const D3D11_SUBRESOURCE_DATA *data,
                                                          ID3D11Texture2D1 **texture)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_texture(iface, D3D11_RESOURCE_DIMENSION_TEXTURE2D, desc, data, (void **)texture);
}

static HRESULT STDMETHODCALLTYPE device_create_texture2d(ID3D11Device5 *iface, const D3D11_TEXTURE2D_DESC *desc,
                                                         const D3D11_SUBRESOURCE_DATA *data,
                                                         ID3D11Texture2D **texture)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc)
        return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC1 desc1;
    memcpy(&desc1, desc, sizeof(*desc));
    desc1.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
    return device_create_texture2d1(iface, &desc1, data, (ID3D11Texture2D1 **)texture);
}

static HRESULT STDMETHODCALLTYPE device_create_texture3d1(ID3D11Device5 *iface, const D3D11_TEXTURE3D_DESC1 *desc,
                                                          const D3D11_SUBRESOURCE_DATA *data,
                                                          ID3D11Texture3D1 **texture)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_texture(iface, D3D11_RESOURCE_DIMENSION_TEXTURE3D, desc, data, (void **)texture);
}

static HRESULT STDMETHODCALLTYPE device_create_texture3d(ID3D11Device5 *iface, const D3D11_TEXTURE3D_DESC *desc,
                                                         const D3D11_SUBRESOURCE_DATA *data,
                                                         ID3D11Texture3D **texture)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc)
        return E_INVALIDARG;
    D3D11_TEXTURE3D_DESC1 desc1;
    memcpy(&desc1, desc, sizeof(*desc));
    desc1.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
    return device_create_texture3d1(iface, &desc1, data, (ID3D11Texture3D1 **)texture);
}

static HRESULT create_view(ID3D11Device5 *iface, D3dViewKind kind, ID3D11Resource *resource, const void *desc,
                           void **out)
{
    if (!resource)
        return E_INVALIDARG;
    D3dView *view;
    HRESULT hr = D3dCreateView(impl(iface), kind, resource, desc, 0, out ? &view : nullptr);
    if (SUCCEEDED(hr) && out)
        *out = &view->iface;
    return out || FAILED(hr) ? hr : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE device_create_shader_resource_view1(ID3D11Device5 *iface, ID3D11Resource *resource,
                                                                     const D3D11_SHADER_RESOURCE_VIEW_DESC1 *desc,
                                                                     ID3D11ShaderResourceView1 **view)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_view(iface, D3D_VIEW_SRV, resource, desc, (void **)view);
}

/* a view description as its second version has it. */
static void srv_desc1(const D3D11_SHADER_RESOURCE_VIEW_DESC *desc, D3D11_SHADER_RESOURCE_VIEW_DESC1 *desc1)
{
    memset(desc1, 0, sizeof(*desc1));
    desc1->Format = desc->Format;
    desc1->ViewDimension = desc->ViewDimension;
    switch (desc->ViewDimension) {
    case D3D11_SRV_DIMENSION_TEXTURE2D:
        desc1->Texture2D.MostDetailedMip = desc->Texture2D.MostDetailedMip;
        desc1->Texture2D.MipLevels = desc->Texture2D.MipLevels;
        break;
    case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
        memcpy(&desc1->Texture2DArray, &desc->Texture2DArray, sizeof(desc->Texture2DArray));
        break;
    default:
        memcpy(&desc1->Buffer, &desc->Buffer, sizeof(desc->BufferEx));
        break;
    }
}

static HRESULT STDMETHODCALLTYPE device_create_shader_resource_view(ID3D11Device5 *iface, ID3D11Resource *resource,
                                                                    const D3D11_SHADER_RESOURCE_VIEW_DESC *desc,
                                                                    ID3D11ShaderResourceView **view)
{
    D3D_DEVICE_SYNC(impl(iface));
    D3D11_SHADER_RESOURCE_VIEW_DESC1 desc1;
    if (desc)
        srv_desc1(desc, &desc1);
    return device_create_shader_resource_view1(iface, resource, desc ? &desc1 : nullptr,
                                               (ID3D11ShaderResourceView1 **)view);
}

static HRESULT STDMETHODCALLTYPE device_create_unordered_access_view1(ID3D11Device5 *iface, ID3D11Resource *resource,
                                                                      const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *desc,
                                                                      ID3D11UnorderedAccessView1 **view)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_view(iface, D3D_VIEW_UAV, resource, desc, (void **)view);
}

static HRESULT STDMETHODCALLTYPE device_create_unordered_access_view(ID3D11Device5 *iface, ID3D11Resource *resource,
                                                                     const D3D11_UNORDERED_ACCESS_VIEW_DESC *desc,
                                                                     ID3D11UnorderedAccessView **view)
{
    D3D_DEVICE_SYNC(impl(iface));
    D3D11_UNORDERED_ACCESS_VIEW_DESC1 desc1, *p = nullptr;
    if (desc) {
        memset(&desc1, 0, sizeof(desc1));
        desc1.Format = desc->Format;
        desc1.ViewDimension = desc->ViewDimension;
        switch (desc->ViewDimension) {
        case D3D11_UAV_DIMENSION_TEXTURE2D:
            desc1.Texture2D.MipSlice = desc->Texture2D.MipSlice;
            break;
        case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
            memcpy(&desc1.Texture2DArray, &desc->Texture2DArray, sizeof(desc->Texture2DArray));
            break;
        default:
            memcpy(&desc1.Buffer, &desc->Buffer, sizeof(desc->Buffer));
            break;
        }
        p = &desc1;
    }
    return device_create_unordered_access_view1(iface, resource, p, (ID3D11UnorderedAccessView1 **)view);
}

static HRESULT STDMETHODCALLTYPE device_create_render_target_view1(ID3D11Device5 *iface, ID3D11Resource *resource,
                                                                   const D3D11_RENDER_TARGET_VIEW_DESC1 *desc,
                                                                   ID3D11RenderTargetView1 **view)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_view(iface, D3D_VIEW_RTV, resource, desc, (void **)view);
}

static HRESULT STDMETHODCALLTYPE device_create_render_target_view(ID3D11Device5 *iface, ID3D11Resource *resource,
                                                                  const D3D11_RENDER_TARGET_VIEW_DESC *desc,
                                                                  ID3D11RenderTargetView **view)
{
    D3D_DEVICE_SYNC(impl(iface));
    D3D11_RENDER_TARGET_VIEW_DESC1 desc1, *p = nullptr;
    if (desc) {
        memset(&desc1, 0, sizeof(desc1));
        desc1.Format = desc->Format;
        desc1.ViewDimension = desc->ViewDimension;
        switch (desc->ViewDimension) {
        case D3D11_RTV_DIMENSION_TEXTURE2D:
            desc1.Texture2D.MipSlice = desc->Texture2D.MipSlice;
            break;
        case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
            memcpy(&desc1.Texture2DArray, &desc->Texture2DArray, sizeof(desc->Texture2DArray));
            break;
        default:
            memcpy(&desc1.Buffer, &desc->Buffer, sizeof(desc->Texture3D));
            break;
        }
        p = &desc1;
    }
    return device_create_render_target_view1(iface, resource, p, (ID3D11RenderTargetView1 **)view);
}

static HRESULT STDMETHODCALLTYPE device_create_depth_stencil_view(ID3D11Device5 *iface, ID3D11Resource *resource,
                                                                  const D3D11_DEPTH_STENCIL_VIEW_DESC *desc,
                                                                  ID3D11DepthStencilView **view)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_view(iface, D3D_VIEW_DSV, resource, desc, (void **)view);
}

static HRESULT STDMETHODCALLTYPE device_create_input_layout(ID3D11Device5 *iface,
                                                            const D3D11_INPUT_ELEMENT_DESC *elements, UINT count,
                                                            const void *bytecode, SIZE_T length,
                                                            ID3D11InputLayout **layout)
{
    D3D_DEVICE_SYNC(impl(iface));
    D3dInputLayout *object;
    HRESULT hr = D3dCreateInputLayout(impl(iface), elements, count, bytecode, length, layout ? &object : nullptr);
    if (SUCCEEDED(hr) && layout)
        *layout = &object->iface;
    return layout || FAILED(hr) ? hr : S_FALSE;
}

static HRESULT create_shader(ID3D11Device5 *iface, D3dStage stage, const void *bytecode, SIZE_T length,
                             const D3D11_SO_DECLARATION_ENTRY *so, UINT so_count, const UINT *strides,
                             UINT stride_count, UINT rasterized, void **out)
{
    if (!bytecode)
        return E_INVALIDARG;
    D3dShader *shader;
    HRESULT hr = D3dCreateShader(impl(iface), stage, bytecode, length, so, so_count, strides, stride_count,
                                 rasterized, out ? &shader : nullptr);
    if (SUCCEEDED(hr) && out)
        *out = &shader->iface;
    return out || FAILED(hr) ? hr : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE device_create_vertex_shader(ID3D11Device5 *iface, const void *bytecode,
                                                             SIZE_T length, ID3D11ClassLinkage *linkage,
                                                             ID3D11VertexShader **shader)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_shader(iface, D3D_VS, bytecode, length, nullptr, 0, nullptr, 0, 0, (void **)shader);
}

static HRESULT STDMETHODCALLTYPE device_create_geometry_shader(ID3D11Device5 *iface, const void *bytecode,
                                                               SIZE_T length, ID3D11ClassLinkage *linkage,
                                                               ID3D11GeometryShader **shader)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_shader(iface, D3D_GS, bytecode, length, nullptr, 0, nullptr, 0, 0, (void **)shader);
}

static HRESULT STDMETHODCALLTYPE device_create_geometry_shader_with_stream_output(
    ID3D11Device5 *iface, const void *bytecode, SIZE_T length, const D3D11_SO_DECLARATION_ENTRY *entries,
    UINT count, const UINT *strides, UINT stride_count, UINT rasterized_stream, ID3D11ClassLinkage *linkage,
    ID3D11GeometryShader **shader)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (count > 64 || stride_count > D3D11_SO_BUFFER_SLOT_COUNT || (count && !entries))
        return E_INVALIDARG;
    return create_shader(iface, D3D_GS, bytecode, length, entries, count, strides, stride_count, rasterized_stream,
                         (void **)shader);
}

static HRESULT STDMETHODCALLTYPE device_create_pixel_shader(ID3D11Device5 *iface, const void *bytecode,
                                                            SIZE_T length, ID3D11ClassLinkage *linkage,
                                                            ID3D11PixelShader **shader)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_shader(iface, D3D_PS, bytecode, length, nullptr, 0, nullptr, 0, 0, (void **)shader);
}

static HRESULT STDMETHODCALLTYPE device_create_hull_shader(ID3D11Device5 *iface, const void *bytecode,
                                                           SIZE_T length, ID3D11ClassLinkage *linkage,
                                                           ID3D11HullShader **shader)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_shader(iface, D3D_HS, bytecode, length, nullptr, 0, nullptr, 0, 0, (void **)shader);
}

static HRESULT STDMETHODCALLTYPE device_create_domain_shader(ID3D11Device5 *iface, const void *bytecode,
                                                             SIZE_T length, ID3D11ClassLinkage *linkage,
                                                             ID3D11DomainShader **shader)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_shader(iface, D3D_DS, bytecode, length, nullptr, 0, nullptr, 0, 0, (void **)shader);
}

static HRESULT STDMETHODCALLTYPE device_create_compute_shader(ID3D11Device5 *iface, const void *bytecode,
                                                              SIZE_T length, ID3D11ClassLinkage *linkage,
                                                              ID3D11ComputeShader **shader)
{
    D3D_DEVICE_SYNC(impl(iface));
    return create_shader(iface, D3D_CS, bytecode, length, nullptr, 0, nullptr, 0, 0, (void **)shader);
}

HRESULT D3dCreateClassLinkage(D3dDevice *device, ID3D11ClassLinkage **out);

static HRESULT STDMETHODCALLTYPE device_create_class_linkage(ID3D11Device5 *iface, ID3D11ClassLinkage **linkage)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!linkage)
        return E_INVALIDARG;
    return D3dCreateClassLinkage(impl(iface), linkage);
}

static HRESULT STDMETHODCALLTYPE device_create_blend_state1(ID3D11Device5 *iface, const D3D11_BLEND_DESC1 *desc,
                                                            ID3D11BlendState1 **state)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc)
        return E_INVALIDARG;
    D3dBlendState *object;
    HRESULT hr = D3dCreateBlendState(impl(iface), desc, state ? &object : nullptr);
    if (SUCCEEDED(hr) && state)
        *state = &object->iface;
    return state || FAILED(hr) ? hr : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE device_create_blend_state(ID3D11Device5 *iface, const D3D11_BLEND_DESC *desc,
                                                           ID3D11BlendState **state)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc)
        return E_INVALIDARG;
    D3D11_BLEND_DESC1 desc1 = {desc->AlphaToCoverageEnable, desc->IndependentBlendEnable};
    for (UINT i = 0; i < 8; i++) {
        const D3D11_RENDER_TARGET_BLEND_DESC *s = &desc->RenderTarget[i];
        D3D11_RENDER_TARGET_BLEND_DESC1 *d = &desc1.RenderTarget[i];
        d->BlendEnable = s->BlendEnable;
        d->LogicOpEnable = FALSE;
        d->SrcBlend = s->SrcBlend;
        d->DestBlend = s->DestBlend;
        d->BlendOp = s->BlendOp;
        d->SrcBlendAlpha = s->SrcBlendAlpha;
        d->DestBlendAlpha = s->DestBlendAlpha;
        d->BlendOpAlpha = s->BlendOpAlpha;
        d->LogicOp = D3D11_LOGIC_OP_NOOP;
        d->RenderTargetWriteMask = s->RenderTargetWriteMask;
    }
    return device_create_blend_state1(iface, &desc1, (ID3D11BlendState1 **)state);
}

static HRESULT STDMETHODCALLTYPE device_create_depth_stencil_state(ID3D11Device5 *iface,
                                                                   const D3D11_DEPTH_STENCIL_DESC *desc,
                                                                   ID3D11DepthStencilState **state)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc)
        return E_INVALIDARG;
    D3dDepthStencilState *object;
    HRESULT hr = D3dCreateDepthStencilState(impl(iface), desc, state ? &object : nullptr);
    if (SUCCEEDED(hr) && state)
        *state = &object->iface;
    return state || FAILED(hr) ? hr : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE device_create_rasterizer_state2(ID3D11Device5 *iface,
                                                                 const D3D11_RASTERIZER_DESC2 *desc,
                                                                 ID3D11RasterizerState2 **state)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc)
        return E_INVALIDARG;
    D3dRasterizerState *object;
    HRESULT hr = D3dCreateRasterizerState(impl(iface), desc, state ? &object : nullptr);
    if (SUCCEEDED(hr) && state)
        *state = &object->iface;
    return state || FAILED(hr) ? hr : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE device_create_rasterizer_state1(ID3D11Device5 *iface,
                                                                 const D3D11_RASTERIZER_DESC1 *desc,
                                                                 ID3D11RasterizerState1 **state)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc)
        return E_INVALIDARG;
    D3D11_RASTERIZER_DESC2 desc2;
    memcpy(&desc2, desc, sizeof(*desc));
    desc2.ConservativeRaster = D3D11_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return device_create_rasterizer_state2(iface, &desc2, (ID3D11RasterizerState2 **)state);
}

static HRESULT STDMETHODCALLTYPE device_create_rasterizer_state(ID3D11Device5 *iface,
                                                                const D3D11_RASTERIZER_DESC *desc,
                                                                ID3D11RasterizerState **state)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc)
        return E_INVALIDARG;
    D3D11_RASTERIZER_DESC2 desc2;
    memset(&desc2, 0, sizeof(desc2));
    memcpy(&desc2, desc, sizeof(*desc));
    return device_create_rasterizer_state2(iface, &desc2, (ID3D11RasterizerState2 **)state);
}

static HRESULT STDMETHODCALLTYPE device_create_sampler_state(ID3D11Device5 *iface, const D3D11_SAMPLER_DESC *desc,
                                                             ID3D11SamplerState **state)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc)
        return E_INVALIDARG;
    D3dSamplerState *object;
    HRESULT hr = D3dCreateSamplerState(impl(iface), desc, state ? &object : nullptr);
    if (SUCCEEDED(hr) && state)
        *state = &object->iface;
    return state || FAILED(hr) ? hr : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE device_create_query1(ID3D11Device5 *iface, const D3D11_QUERY_DESC1 *desc,
                                                      ID3D11Query1 **query)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc)
        return E_INVALIDARG;
    D3dQuery *object;
    HRESULT hr = D3dCreateQuery(impl(iface), desc, query ? &object : nullptr);
    if (SUCCEEDED(hr) && query)
        *query = &object->iface;
    return query || FAILED(hr) ? hr : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE device_create_query(ID3D11Device5 *iface, const D3D11_QUERY_DESC *desc,
                                                     ID3D11Query **query)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc)
        return E_INVALIDARG;
    D3D11_QUERY_DESC1 desc1 = {desc->Query, desc->MiscFlags, D3D11_CONTEXT_TYPE_ALL};
    return device_create_query1(iface, &desc1, (ID3D11Query1 **)query);
}

static HRESULT STDMETHODCALLTYPE device_create_predicate(ID3D11Device5 *iface, const D3D11_QUERY_DESC *desc,
                                                         ID3D11Predicate **predicate)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!desc || (desc->Query != D3D11_QUERY_OCCLUSION_PREDICATE &&
                  desc->Query != D3D11_QUERY_SO_OVERFLOW_PREDICATE &&
                  !(desc->Query >= D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0 && (desc->Query & 1))))
        return E_INVALIDARG;
    return device_create_query(iface, desc, (ID3D11Query **)predicate);
}

static HRESULT STDMETHODCALLTYPE device_create_counter(ID3D11Device5 *iface, const D3D11_COUNTER_DESC *desc,
                                                       ID3D11Counter **counter)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (counter)
        *counter = nullptr;
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE device_create_deferred_context(ID3D11Device5 *iface, UINT flags,
                                                                ID3D11DeviceContext **context)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (context)
        *context = nullptr;
    if (flags)
        return E_INVALIDARG;
    if (!context)
        return S_FALSE;
    D3dContext *c;
    HRESULT hr = D3dCreateContext(impl(iface), D3D11_DEVICE_CONTEXT_DEFERRED, &c);
    if (SUCCEEDED(hr))
        *context = (ID3D11DeviceContext *)&c->iface;
    return hr;
}

/* OpenSharedResource takes the legacy handle GetSharedHandle gives, OpenSharedResource1 the nt handle. */
static HRESULT STDMETHODCALLTYPE device_open_shared_resource(ID3D11Device5 *iface, HANDLE handle, REFIID riid,
                                                             void **resource)
{
    D3D_DEVICE_SYNC(impl(iface));
    return D3dShareOpen(impl(iface), handle, FALSE, riid, resource);
}

static HRESULT STDMETHODCALLTYPE device_open_shared_resource1(ID3D11Device5 *iface, HANDLE handle, REFIID riid,
                                                              void **resource)
{
    D3D_DEVICE_SYNC(impl(iface));
    return D3dShareOpen(impl(iface), handle, TRUE, riid, resource);
}

/* the formats of video depend on what the host's gpu decodes, which is asked once. */
static UINT D3dFormatSupportProbed(D3dDevice *device, DXGI_FORMAT format, UINT *support2)
{
    if (D3dGetFormat(format)->flags & D3D_FORMAT_PLANAR)
        D3dVideoProbe(device);
    return D3dFormatSupport(device, format, support2);
}

static HRESULT STDMETHODCALLTYPE device_check_format_support(ID3D11Device5 *iface, DXGI_FORMAT format,
                                                             UINT *support)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!support)
        return E_INVALIDARG;
    *support = D3dFormatSupportProbed(impl(iface), format, nullptr);
    return *support ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE device_check_multisample_quality_levels1(ID3D11Device5 *iface, DXGI_FORMAT format,
                                                                          UINT samples, UINT flags, UINT *levels)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!levels)
        return E_INVALIDARG;
    *levels = 0;
    D3dDevice *device = impl(iface);
    const D3dFormat *f = D3dGetFormat(format);
    if (format == DXGI_FORMAT_UNKNOWN)
        return samples == 1 ? (*levels = 1, S_OK) : E_INVALIDARG;
    if (f->vk == VK_FORMAT_UNDEFINED || samples > 32 || !samples || (samples & (samples - 1)))
        return E_INVALIDARG;
    if (samples == 1) {
        *levels = 1;
        return S_OK;
    }
    BOOL depth = (f->flags & (D3D_FORMAT_DEPTH | D3D_FORMAT_STENCIL)) != 0;
    VkImageFormatProperties props;
    VkImageUsageFlags usage = depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (device->vk.vkGetPhysicalDeviceImageFormatProperties(device->vk.physical, depth ? f->depth : f->vk,
                                                            VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0,
                                                            &props) == VK_SUCCESS &&
        (props.sampleCounts & samples))
        *levels = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_check_multisample_quality_levels(ID3D11Device5 *iface, DXGI_FORMAT format,
                                                                         UINT samples, UINT *levels)
{
    D3D_DEVICE_SYNC(impl(iface));
    return device_check_multisample_quality_levels1(iface, format, samples, 0, levels);
}

static void STDMETHODCALLTYPE device_check_counter_info(ID3D11Device5 *iface, D3D11_COUNTER_INFO *info)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (info)
        memset(info, 0, sizeof(*info));
}

static HRESULT STDMETHODCALLTYPE device_check_counter(ID3D11Device5 *iface, const D3D11_COUNTER_DESC *desc,
                                                      D3D11_COUNTER_TYPE *type, UINT *active, LPSTR name,
                                                      UINT *name_length, LPSTR units, UINT *units_length,
                                                      LPSTR description, UINT *description_length)
{
    D3D_DEVICE_SYNC(impl(iface));
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE device_check_feature_support(ID3D11Device5 *iface, D3D11_FEATURE feature,
                                                              void *data, UINT size)
{
    D3D_DEVICE_SYNC(impl(iface));
    D3dDevice *device = impl(iface);
    if (!data)
        return E_INVALIDARG;
#define EXPECT(type)                                                                                                   \
    if (size != sizeof(type))                                                                                          \
        return E_INVALIDARG;                                                                                           \
    type *out = data;                                                                                                  \
    memset(out, 0, sizeof(*out))
    switch (feature) {
    case D3D11_FEATURE_THREADING: {
        EXPECT(D3D11_FEATURE_DATA_THREADING);
        out->DriverConcurrentCreates = TRUE;
        return S_OK;
    }
    case D3D11_FEATURE_DOUBLES: {
        EXPECT(D3D11_FEATURE_DATA_DOUBLES);
        VkPhysicalDeviceFeatures2 f = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        device->vk.vkGetPhysicalDeviceFeatures2(device->vk.physical, &f);
        out->DoublePrecisionFloatShaderOps = f.features.shaderFloat64;
        return S_OK;
    }
    case D3D11_FEATURE_FORMAT_SUPPORT: {
        EXPECT(D3D11_FEATURE_DATA_FORMAT_SUPPORT);
        out->InFormat = ((D3D11_FEATURE_DATA_FORMAT_SUPPORT *)data)->InFormat;
        return E_INVALIDARG;
    }
    case D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS: {
        EXPECT(D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS);
        out->ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x = TRUE;
        return S_OK;
    }
    case D3D11_FEATURE_D3D11_OPTIONS: {
        EXPECT(D3D11_FEATURE_DATA_D3D11_OPTIONS);
        out->OutputMergerLogicOp = device->has_logic_op;
        out->UAVOnlyRenderingForcedSampleCount = TRUE;
        out->DiscardAPIsSeenByDriver = TRUE;
        out->FlagsForUpdateAndCopySeenByDriver = TRUE;
        out->ClearView = TRUE;
        out->CopyWithOverlap = TRUE;
        out->ConstantBufferPartialUpdate = TRUE;
        out->ConstantBufferOffsetting = TRUE;
        out->MapNoOverwriteOnDynamicConstantBuffer = TRUE;
        out->MapNoOverwriteOnDynamicBufferSRV = TRUE;
        out->MultisampleRTVWithForcedSampleCountOne = TRUE;
        out->SAD4ShaderInstructions = TRUE;
        out->ExtendedResourceSharing = TRUE;
        return S_OK;
    }
    case D3D11_FEATURE_ARCHITECTURE_INFO: {
        EXPECT(D3D11_FEATURE_DATA_ARCHITECTURE_INFO);
        return S_OK;
    }
    case D3D11_FEATURE_D3D9_OPTIONS: {
        EXPECT(D3D11_FEATURE_DATA_D3D9_OPTIONS);
        out->FullNonPow2TextureSupport = TRUE;
        return S_OK;
    }
    case D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT: {
        EXPECT(D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT);
        return S_OK;
    }
    case D3D11_FEATURE_D3D9_SHADOW_SUPPORT: {
        EXPECT(D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT);
        out->SupportsDepthAsTextureWithLessEqualComparisonFilter = TRUE;
        return S_OK;
    }
    case D3D11_FEATURE_D3D11_OPTIONS1: {
        EXPECT(D3D11_FEATURE_DATA_D3D11_OPTIONS1);
        out->ClearViewAlsoSupportsDepthOnlyFormats = TRUE;
        return S_OK;
    }
    case D3D11_FEATURE_D3D9_SIMPLE_INSTANCING_SUPPORT: {
        EXPECT(D3D11_FEATURE_DATA_D3D9_SIMPLE_INSTANCING_SUPPORT);
        out->SimpleInstancingSupported = TRUE;
        return S_OK;
    }
    case D3D11_FEATURE_MARKER_SUPPORT: {
        EXPECT(D3D11_FEATURE_DATA_MARKER_SUPPORT);
        return S_OK;
    }
    case D3D11_FEATURE_D3D9_OPTIONS1: {
        EXPECT(D3D11_FEATURE_DATA_D3D9_OPTIONS1);
        out->FullNonPow2TextureSupported = TRUE;
        out->DepthAsTextureWithLessEqualComparisonFilterSupported = TRUE;
        out->SimpleInstancingSupported = TRUE;
        out->TextureCubeFaceRenderTargetWithNonCubeDepthStencilSupported = TRUE;
        return S_OK;
    }
    case D3D11_FEATURE_D3D11_OPTIONS2: {
        EXPECT(D3D11_FEATURE_DATA_D3D11_OPTIONS2);
        out->TypedUAVLoadAdditionalFormats = TRUE;
        return S_OK;
    }
    case D3D11_FEATURE_D3D11_OPTIONS3: {
        EXPECT(D3D11_FEATURE_DATA_D3D11_OPTIONS3);
        out->VPAndRTArrayIndexFromAnyShaderFeedingRasterizer = TRUE;
        return S_OK;
    }
    case D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT: {
        EXPECT(D3D11_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT);
        out->MaxGPUVirtualAddressBitsPerResource = 40;
        out->MaxGPUVirtualAddressBitsPerProcess = 40;
        return S_OK;
    }
    case D3D11_FEATURE_D3D11_OPTIONS4: {
        EXPECT(D3D11_FEATURE_DATA_D3D11_OPTIONS4);
        return S_OK;
    }
    case D3D11_FEATURE_SHADER_CACHE: {
        EXPECT(D3D11_FEATURE_DATA_SHADER_CACHE);
        return S_OK;
    }
    case D3D11_FEATURE_D3D11_OPTIONS5: {
        EXPECT(D3D11_FEATURE_DATA_D3D11_OPTIONS5);
        return S_OK;
    }
    default:
        break;
    }
#undef EXPECT
    if (feature == D3D11_FEATURE_FORMAT_SUPPORT2 && size == sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT2)) {
        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 *out = data;
        D3dFormatSupportProbed(device, out->InFormat, &out->OutFormatSupport2);
        return S_OK;
    }
    D3dLog("d3d11: CheckFeatureSupport %u is unknown", feature);
    return E_INVALIDARG;
}

/* the format support query reads its format before clearing the answer. */
static HRESULT STDMETHODCALLTYPE device_check_feature_support_entry(ID3D11Device5 *iface, D3D11_FEATURE feature,
                                                                    void *data, UINT size)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (feature == D3D11_FEATURE_FORMAT_SUPPORT) {
        if (!data || size != sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT))
            return E_INVALIDARG;
        D3D11_FEATURE_DATA_FORMAT_SUPPORT *out = data;
        out->OutFormatSupport = D3dFormatSupportProbed(impl(iface), out->InFormat, nullptr);
        return S_OK;
    }
    return device_check_feature_support(iface, feature, data, size);
}

static HRESULT STDMETHODCALLTYPE device_get_private_data(ID3D11Device5 *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE device_set_private_data(ID3D11Device5 *iface, REFGUID guid, UINT size,
                                                         const void *data)
{
    return MimePrivateSet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE device_set_private_data_interface(ID3D11Device5 *iface, REFGUID guid,
                                                                   const IUnknown *data)
{
    return MimePrivateSetInterface(&impl(iface)->data, guid, data);
}

static D3D_FEATURE_LEVEL STDMETHODCALLTYPE device_get_feature_level(ID3D11Device5 *iface)
{
    return impl(iface)->feature_level;
}

static UINT STDMETHODCALLTYPE device_get_creation_flags(ID3D11Device5 *iface) { return impl(iface)->flags; }

static HRESULT STDMETHODCALLTYPE device_get_device_removed_reason(ID3D11Device5 *iface) { return S_OK; }

static void STDMETHODCALLTYPE device_get_immediate_context(ID3D11Device5 *iface, ID3D11DeviceContext **context)
{
    if (!context)
        return;
    D3dContext *immediate = impl(iface)->immediate;
    immediate->iface.lpVtbl->AddRef(&immediate->iface);
    *context = (ID3D11DeviceContext *)&immediate->iface;
}

static void STDMETHODCALLTYPE device_get_immediate_context1(ID3D11Device5 *iface, ID3D11DeviceContext1 **context)
{
    device_get_immediate_context(iface, (ID3D11DeviceContext **)context);
}

static void STDMETHODCALLTYPE device_get_immediate_context2(ID3D11Device5 *iface, ID3D11DeviceContext2 **context)
{
    device_get_immediate_context(iface, (ID3D11DeviceContext **)context);
}

static void STDMETHODCALLTYPE device_get_immediate_context3(ID3D11Device5 *iface, ID3D11DeviceContext3 **context)
{
    device_get_immediate_context(iface, (ID3D11DeviceContext **)context);
}

static HRESULT STDMETHODCALLTYPE device_set_exception_mode(ID3D11Device5 *iface, UINT flags)
{
    impl(iface)->exception_mode = flags;
    return S_OK;
}

static UINT STDMETHODCALLTYPE device_get_exception_mode(ID3D11Device5 *iface) { return impl(iface)->exception_mode; }

static HRESULT STDMETHODCALLTYPE device_create_deferred_context1(ID3D11Device5 *iface, UINT flags,
                                                                 ID3D11DeviceContext1 **context)
{
    D3D_DEVICE_SYNC(impl(iface));
    return device_create_deferred_context(iface, flags, (ID3D11DeviceContext **)context);
}

static HRESULT STDMETHODCALLTYPE device_create_deferred_context2(ID3D11Device5 *iface, UINT flags,
                                                                 ID3D11DeviceContext2 **context)
{
    D3D_DEVICE_SYNC(impl(iface));
    return device_create_deferred_context(iface, flags, (ID3D11DeviceContext **)context);
}

static HRESULT STDMETHODCALLTYPE device_create_deferred_context3(ID3D11Device5 *iface, UINT flags,
                                                                 ID3D11DeviceContext3 **context)
{
    D3D_DEVICE_SYNC(impl(iface));
    return device_create_deferred_context(iface, flags, (ID3D11DeviceContext **)context);
}

HRESULT D3dCreateContextState(D3dDevice *device, ID3DDeviceContextState **out);

static HRESULT STDMETHODCALLTYPE device_create_device_context_state(ID3D11Device5 *iface, UINT flags,
                                                                    const D3D_FEATURE_LEVEL *levels, UINT count,
                                                                    UINT sdk, REFIID emulated,
                                                                    D3D_FEATURE_LEVEL *chosen,
                                                                    ID3DDeviceContextState **state)
{
    D3D_DEVICE_SYNC(impl(iface));
    D3dDevice *device = impl(iface);
    D3D_FEATURE_LEVEL level = 0;
    for (UINT i = 0; i < count && !level; i++)
        if (levels[i] <= device->feature_level)
            level = levels[i];
    if (!level)
        return E_INVALIDARG;
    if (chosen)
        *chosen = level;
    if (!state)
        return S_FALSE;
    return D3dCreateContextState(device, state);
}

static HRESULT STDMETHODCALLTYPE device_open_shared_resource_by_name(ID3D11Device5 *iface, LPCWSTR name,
                                                                     DWORD access, REFIID riid, void **resource)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (!name || !resource)
        return E_INVALIDARG;
    *resource = nullptr;
    HANDLE handle;
    HRESULT hr = MimeShareOpenName(name, MIME_GFXSHARE_RESOURCE, &handle);
    if (FAILED(hr))
        return hr;
    hr = D3dShareOpen(impl(iface), handle, TRUE, riid, resource);
    CloseHandle(handle);
    return hr;
}

static void STDMETHODCALLTYPE device_get_resource_tiling(ID3D11Device5 *iface, ID3D11Resource *resource,
                                                         UINT *tiles, D3D11_PACKED_MIP_DESC *mips,
                                                         D3D11_TILE_SHAPE *shape, UINT *count, UINT first,
                                                         D3D11_SUBRESOURCE_TILING *tilings)
{
    D3D_DEVICE_SYNC(impl(iface));
    if (tiles)
        *tiles = 0;
    if (mips)
        memset(mips, 0, sizeof(*mips));
    if (shape)
        memset(shape, 0, sizeof(*shape));
    if (count)
        *count = 0;
}

static void STDMETHODCALLTYPE device_write_to_subresource(ID3D11Device5 *iface, ID3D11Resource *dst, UINT dst_sub,
                                                          const D3D11_BOX *box, const void *data, UINT row_pitch,
                                                          UINT depth_pitch)
{
    D3dLog("d3d11: WriteToSubresource needs mappable default resources, which the device does not report");
}

static void STDMETHODCALLTYPE device_read_from_subresource(ID3D11Device5 *iface, void *data, UINT row_pitch,
                                                           UINT depth_pitch, ID3D11Resource *src, UINT src_sub,
                                                           const D3D11_BOX *box)
{
    D3dLog("d3d11: ReadFromSubresource needs mappable default resources, which the device does not report");
}

static HRESULT STDMETHODCALLTYPE device_register_device_removed_event(ID3D11Device5 *iface, HANDLE event,
                                                                      DWORD *cookie)
{
    static LONG next;
    if (!event || !cookie)
        return E_INVALIDARG;
    *cookie = (DWORD)InterlockedIncrement(&next);
    return S_OK;
}

static void STDMETHODCALLTYPE device_unregister_device_removed(ID3D11Device5 *iface, DWORD cookie) {}

static HRESULT STDMETHODCALLTYPE device_open_shared_fence(ID3D11Device5 *iface, HANDLE handle, REFIID riid,
                                                          void **fence)
{
    D3D_DEVICE_SYNC(impl(iface));
    return D3dOpenSharedFence(impl(iface), handle, riid, fence);
}

static HRESULT STDMETHODCALLTYPE device_create_fence(ID3D11Device5 *iface, UINT64 value, D3D11_FENCE_FLAG flags,
                                                     REFIID riid, void **fence)
{
    D3D_DEVICE_SYNC(impl(iface));
    return D3dCreateFence(impl(iface), value, flags, riid, fence);
}

static const ID3D11Device5Vtbl device_vtbl = {
    device_query,
    device_add_ref,
    device_release,
    device_create_buffer,
    device_create_texture1d,
    device_create_texture2d,
    device_create_texture3d,
    device_create_shader_resource_view,
    device_create_unordered_access_view,
    device_create_render_target_view,
    device_create_depth_stencil_view,
    device_create_input_layout,
    device_create_vertex_shader,
    device_create_geometry_shader,
    device_create_geometry_shader_with_stream_output,
    device_create_pixel_shader,
    device_create_hull_shader,
    device_create_domain_shader,
    device_create_compute_shader,
    device_create_class_linkage,
    device_create_blend_state,
    device_create_depth_stencil_state,
    device_create_rasterizer_state,
    device_create_sampler_state,
    device_create_query,
    device_create_predicate,
    device_create_counter,
    device_create_deferred_context,
    device_open_shared_resource,
    device_check_format_support,
    device_check_multisample_quality_levels,
    device_check_counter_info,
    device_check_counter,
    device_check_feature_support_entry,
    device_get_private_data,
    device_set_private_data,
    device_set_private_data_interface,
    device_get_feature_level,
    device_get_creation_flags,
    device_get_device_removed_reason,
    device_get_immediate_context,
    device_set_exception_mode,
    device_get_exception_mode,
    device_get_immediate_context1,
    device_create_deferred_context1,
    device_create_blend_state1,
    device_create_rasterizer_state1,
    device_create_device_context_state,
    device_open_shared_resource1,
    device_open_shared_resource_by_name,
    device_get_immediate_context2,
    device_create_deferred_context2,
    device_get_resource_tiling,
    device_check_multisample_quality_levels1,
    device_create_texture2d1,
    device_create_texture3d1,
    device_create_rasterizer_state2,
    device_create_shader_resource_view1,
    device_create_unordered_access_view1,
    device_create_render_target_view1,
    device_create_query1,
    device_get_immediate_context3,
    device_create_deferred_context3,
    device_write_to_subresource,
    device_read_from_subresource,
    device_register_device_removed_event,
    device_unregister_device_removed,
    device_open_shared_fence,
    device_create_fence,
};

/* ------------------------------------------------------------------ IDXGIDevice4 */

static D3dDevice *dxgi_impl(IDXGIDevice4 *iface) { return CONTAINING_RECORD(iface, D3dDevice, dxgi); }

static HRESULT STDMETHODCALLTYPE dxgi_query(IDXGIDevice4 *iface, REFIID riid, void **out)
{
    return device_query(&dxgi_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE dxgi_add_ref(IDXGIDevice4 *iface) { return device_add_ref(&dxgi_impl(iface)->iface); }

static ULONG STDMETHODCALLTYPE dxgi_release(IDXGIDevice4 *iface) { return device_release(&dxgi_impl(iface)->iface); }

static HRESULT STDMETHODCALLTYPE dxgi_set_private_data(IDXGIDevice4 *iface, REFGUID guid, UINT size,
                                                       const void *data)
{
    return MimePrivateSet(&dxgi_impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_set_private_data_interface(IDXGIDevice4 *iface, REFGUID guid,
                                                                 const IUnknown *unknown)
{
    return MimePrivateSetInterface(&dxgi_impl(iface)->data, guid, unknown);
}

static HRESULT STDMETHODCALLTYPE dxgi_get_private_data(IDXGIDevice4 *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&dxgi_impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_get_parent(IDXGIDevice4 *iface, REFIID riid, void **parent)
{
    IDXGIAdapter *adapter = dxgi_impl(iface)->adapter;
    return adapter->lpVtbl->QueryInterface(adapter, riid, parent);
}

static HRESULT STDMETHODCALLTYPE dxgi_get_adapter(IDXGIDevice4 *iface, IDXGIAdapter **adapter)
{
    if (!adapter)
        return E_INVALIDARG;
    *adapter = dxgi_impl(iface)->adapter;
    (*adapter)->lpVtbl->AddRef(*adapter);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_create_surface(IDXGIDevice4 *iface, const DXGI_SURFACE_DESC *desc, UINT count,
                                                     DXGI_USAGE usage, const DXGI_SHARED_RESOURCE *shared,
                                                     IDXGISurface **surfaces)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_query_resource_residency(IDXGIDevice4 *iface, IUnknown *const *resources,
                                                               DXGI_RESIDENCY *status, UINT count)
{
    if (!resources || !status)
        return E_INVALIDARG;
    for (UINT i = 0; i < count; i++)
        status[i] = DXGI_RESIDENCY_FULLY_RESIDENT;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_set_gpu_thread_priority(IDXGIDevice4 *iface, INT priority)
{
    return priority < -7 || priority > 7 ? E_INVALIDARG : S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_get_gpu_thread_priority(IDXGIDevice4 *iface, INT *priority)
{
    if (!priority)
        return E_POINTER;
    *priority = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_set_maximum_frame_latency(IDXGIDevice4 *iface, UINT latency)
{
    if (latency > 16)
        return DXGI_ERROR_INVALID_CALL;
    dxgi_impl(iface)->max_frame_latency = latency ? latency : 3;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_get_maximum_frame_latency(IDXGIDevice4 *iface, UINT *latency)
{
    if (!latency)
        return DXGI_ERROR_INVALID_CALL;
    *latency = dxgi_impl(iface)->max_frame_latency;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_offer_resources(IDXGIDevice4 *iface, UINT count,
                                                      IDXGIResource *const *resources,
                                                      DXGI_OFFER_RESOURCE_PRIORITY priority)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_reclaim_resources(IDXGIDevice4 *iface, UINT count,
                                                        IDXGIResource *const *resources, BOOL *discarded)
{
    if (discarded)
        for (UINT i = 0; i < count; i++)
            discarded[i] = FALSE;
    return S_OK;
}

/* signals the event once the gpu ran everything submitted so far. */
static HRESULT STDMETHODCALLTYPE dxgi_enqueue_set_event(IDXGIDevice4 *iface, HANDLE event)
{
    D3dDevice *device = dxgi_impl(iface);
    D3dContextFlush(device->immediate);
    D3dWaitSerial(device, device->next_serial - 1);
    return SetEvent(event) ? S_OK : E_INVALIDARG;
}

static void STDMETHODCALLTYPE dxgi_trim(IDXGIDevice4 *iface) {}

static HRESULT STDMETHODCALLTYPE dxgi_offer_resources1(IDXGIDevice4 *iface, UINT count,
                                                       IDXGIResource *const *resources,
                                                       DXGI_OFFER_RESOURCE_PRIORITY priority, UINT flags)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_reclaim_resources1(IDXGIDevice4 *iface, UINT count,
                                                         IDXGIResource *const *resources, UINT *results)
{
    if (results)
        for (UINT i = 0; i < count; i++)
            results[i] = 0;
    return S_OK;
}

static const IDXGIDevice4Vtbl dxgi_vtbl = {
    dxgi_query,
    dxgi_add_ref,
    dxgi_release,
    dxgi_set_private_data,
    dxgi_set_private_data_interface,
    dxgi_get_private_data,
    dxgi_get_parent,
    dxgi_get_adapter,
    dxgi_create_surface,
    dxgi_query_resource_residency,
    dxgi_set_gpu_thread_priority,
    dxgi_get_gpu_thread_priority,
    dxgi_set_maximum_frame_latency,
    dxgi_get_maximum_frame_latency,
    dxgi_offer_resources,
    dxgi_reclaim_resources,
    dxgi_enqueue_set_event,
    dxgi_trim,
    dxgi_offer_resources1,
    dxgi_reclaim_resources1,
};

/* ------------------------------------------------------------------ IMimeDXGIPresenter */

static D3dDevice *presenter_impl(IMimeDXGIPresenter *iface) { return CONTAINING_RECORD(iface, D3dDevice, presenter); }

static HRESULT STDMETHODCALLTYPE presenter_query(IMimeDXGIPresenter *iface, REFIID riid, void **out)
{
    return device_query(&presenter_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE presenter_add_ref(IMimeDXGIPresenter *iface)
{
    return device_add_ref(&presenter_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE presenter_release(IMimeDXGIPresenter *iface)
{
    return device_release(&presenter_impl(iface)->iface);
}

static HRESULT STDMETHODCALLTYPE presenter_create_swap_chain(IMimeDXGIPresenter *iface, IDXGIFactory *factory,
                                                             HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
                                                             const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,
                                                             IDXGIOutput *restrict_to, IDXGISwapChain1 **swapchain)
{
    return D3dCreateSwapChain(presenter_impl(iface), factory, window, desc, fullscreen, swapchain);
}

static HRESULT STDMETHODCALLTYPE presenter_create_composition_swap_chain(IMimeDXGIPresenter *iface,
                                                                         IDXGIFactory *factory, HANDLE surface,
                                                                         const DXGI_SWAP_CHAIN_DESC1 *desc,
                                                                         IDXGIOutput *restrict_to,
                                                                         IDXGISwapChain1 **swapchain)
{
    return D3dCreateCompositionSwapChain(presenter_impl(iface), factory, surface, desc, swapchain);
}

static const IMimeDXGIPresenterVtbl presenter_vtbl = {
    presenter_query,
    presenter_add_ref,
    presenter_release,
    presenter_create_swap_chain,
    presenter_create_composition_swap_chain,
};

/* ------------------------------------------------------------------ IMimeD3D11Device */

static D3dDevice *mime_impl(IMimeD3D11Device *iface) { return CONTAINING_RECORD(iface, D3dDevice, mime); }

static HRESULT STDMETHODCALLTYPE mime_query(IMimeD3D11Device *iface, REFIID riid, void **out)
{
    return device_query(&mime_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE mime_add_ref(IMimeD3D11Device *iface) { return device_add_ref(&mime_impl(iface)->iface); }

static ULONG STDMETHODCALLTYPE mime_release(IMimeD3D11Device *iface) { return device_release(&mime_impl(iface)->iface); }

static HRESULT STDMETHODCALLTYPE mime_create_swizzled_view(IMimeD3D11Device *iface, ID3D11Resource *resource,
                                                           const D3D11_SHADER_RESOURCE_VIEW_DESC *desc, UINT swizzle,
                                                           ID3D11ShaderResourceView **out)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC1 desc1;
    if (!resource || !desc || !out)
        return E_INVALIDARG;
    srv_desc1(desc, &desc1);
    D3dView *view;
    HRESULT hr = D3dCreateView(mime_impl(iface), D3D_VIEW_SRV, resource, &desc1, swizzle, &view);
    if (SUCCEEDED(hr))
        *out = (ID3D11ShaderResourceView *)&view->iface;
    return hr;
}

static void STDMETHODCALLTYPE mime_blit(IMimeD3D11Device *iface, ID3D11Resource *dst, UINT dst_sub,
                                        const RECT *dst_rect, ID3D11Resource *src, UINT src_sub, const RECT *src_rect,
                                        BOOL linear)
{
    D3dCtxBlit(mime_impl(iface)->immediate, dst, dst_sub, dst_rect, src, src_sub, src_rect, linear);
}

static void STDMETHODCALLTYPE mime_clear_depth_stencil(IMimeD3D11Device *iface, ID3D11DepthStencilView *view,
                                                       UINT flags, FLOAT depth, UINT8 stencil,
                                                       const D3D11_RECT *rects, UINT count)
{
    D3dCtxClearDepthStencilRects(mime_impl(iface)->immediate, view, flags, depth, stencil, rects, count);
}

static const IMimeD3D11DeviceVtbl mime_vtbl = {
    mime_query,
    mime_add_ref,
    mime_release,
    mime_create_swizzled_view,
    mime_blit,
    mime_clear_depth_stencil,
};

/* ------------------------------------------------------------------ ID3D11Multithread */

static D3dDevice *mt_impl(ID3D11Multithread *iface) { return CONTAINING_RECORD(iface, D3dDevice, multithread); }

static HRESULT STDMETHODCALLTYPE mt_query(ID3D11Multithread *iface, REFIID riid, void **out)
{
    return device_query(&mt_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE mt_add_ref(ID3D11Multithread *iface) { return device_add_ref(&mt_impl(iface)->iface); }

static ULONG STDMETHODCALLTYPE mt_release(ID3D11Multithread *iface) { return device_release(&mt_impl(iface)->iface); }

static void STDMETHODCALLTYPE mt_enter(ID3D11Multithread *iface) { EnterCriticalSection(&mt_impl(iface)->immediate->lock); }

static void STDMETHODCALLTYPE mt_leave(ID3D11Multithread *iface) { LeaveCriticalSection(&mt_impl(iface)->immediate->lock); }

static BOOL STDMETHODCALLTYPE mt_set_protected(ID3D11Multithread *iface, BOOL protect)
{
    return D3dSetProtected(mt_impl(iface)->immediate, protect);
}

static BOOL STDMETHODCALLTYPE mt_get_protected(ID3D11Multithread *iface)
{
    return __atomic_load_n(&mt_impl(iface)->immediate->protected_, __ATOMIC_RELAXED) != 0;
}

static const ID3D11MultithreadVtbl mt_vtbl = {
    mt_query, mt_add_ref, mt_release, mt_enter, mt_leave, mt_set_protected, mt_get_protected,
};

/* ------------------------------------------------------------------ creation */

/* the first level asked that the device reaches, 11_1 at most. */
static D3D_FEATURE_LEVEL choose_level(const D3D_FEATURE_LEVEL *levels, UINT count)
{
    static const D3D_FEATURE_LEVEL defaults[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                                 D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,
                                                 D3D_FEATURE_LEVEL_9_2,  D3D_FEATURE_LEVEL_9_1};
    if (!levels || !count) {
        levels = defaults;
        count = RTL_NUMBER_OF(defaults);
    }
    for (UINT i = 0; i < count; i++)
        if (levels[i] >= D3D_FEATURE_LEVEL_9_1 && levels[i] <= D3D_FEATURE_LEVEL_11_1)
            return levels[i];
    return 0;
}

HRESULT D3dCreateDevice(IDXGIAdapter *adapter, UINT flags, const D3D_FEATURE_LEVEL *levels, UINT count,
                        D3dDevice **out)
{
    D3D_FEATURE_LEVEL level = choose_level(levels, count);
    if (!level)
        return E_INVALIDARG;
    D3dDevice *device = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*device));
    if (!device)
        return E_OUTOFMEMORY;
    device->iface.lpVtbl = &device_vtbl;
    device->dxgi.lpVtbl = &dxgi_vtbl;
    device->presenter.lpVtbl = &presenter_vtbl;
    device->mime.lpVtbl = &mime_vtbl;
    device->multithread.lpVtbl = &mt_vtbl;
    device->refs = 1;
    device->feature_level = level;
    device->flags = flags;
    device->max_frame_latency = 3;
    device->adapter = adapter;
    adapter->lpVtbl->AddRef(adapter);
    InitializeCriticalSection(&device->init_lock);
    InitializeCriticalSection(&device->state_lock);
    HRESULT hr = init_device(device, adapter);
    if (FAILED(hr)) {
        D3dLog("d3d11: device creation failed with %08x", hr);
        destroy_device(device);
        return hr;
    }
    /* the immediate context holds no reference on the device, which owns it. */
    device->refs = 1;
    *out = device;
    return S_OK;
}
