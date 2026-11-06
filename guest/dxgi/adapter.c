/*
 * adapters, one per gpu and the basic render driver, and their outputs. the
 * first adapter drives the monitor, like the gpu a windows pc's display hangs
 * off. the others have no outputs.
 */
#include "dxgi_private.h"

static DxgiAdapter *impl(IDXGIAdapter4 *iface) { return CONTAINING_RECORD(iface, DxgiAdapter, iface); }

static const GUID *const ADAPTER_IIDS[] = {
    &IID_IUnknown,      &IID_IDXGIObject,   &IID_IDXGIAdapter,  &IID_IDXGIAdapter1,
    &IID_IDXGIAdapter2, &IID_IDXGIAdapter3, &IID_IDXGIAdapter4,
};

static HRESULT STDMETHODCALLTYPE adapter_query(IDXGIAdapter4 *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    DxgiAdapter *adapter = impl(iface);
    *out = nullptr;
    for (UINT i = 0; i < RTL_NUMBER_OF(ADAPTER_IIDS); i++)
        if (IsEqualGUID(riid, ADAPTER_IIDS[i]))
            *out = &adapter->iface;
    if (IsEqualGUID(riid, &IID_IMimeDXGIAdapter))
        *out = &adapter->mime;
    if (!*out)
        return E_NOINTERFACE;
    InterlockedIncrement(&adapter->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE adapter_add_ref(IDXGIAdapter4 *iface)
{
    return InterlockedIncrement(&impl(iface)->refs);
}

static ULONG STDMETHODCALLTYPE adapter_release(IDXGIAdapter4 *iface)
{
    DxgiAdapter *adapter = impl(iface);
    ULONG refs = InterlockedDecrement(&adapter->refs);
    if (!refs) {
        MimePrivateClear(&adapter->data);
        adapter->factory->iface.lpVtbl->Release(&adapter->factory->iface);
        HeapFree(GetProcessHeap(), 0, adapter);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE adapter_set_private_data(IDXGIAdapter4 *iface, REFGUID guid, UINT size,
                                                          const void *data)
{
    return MimePrivateSet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE adapter_set_private_data_interface(IDXGIAdapter4 *iface, REFGUID guid,
                                                                    const IUnknown *unknown)
{
    return MimePrivateSetInterface(&impl(iface)->data, guid, unknown);
}

static HRESULT STDMETHODCALLTYPE adapter_get_private_data(IDXGIAdapter4 *iface, REFGUID guid, UINT *size,
                                                          void *data)
{
    return MimePrivateGet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE adapter_get_parent(IDXGIAdapter4 *iface, REFIID riid, void **parent)
{
    if (!parent)
        return DXGI_ERROR_INVALID_CALL;
    IDXGIFactory7 *factory = &impl(iface)->factory->iface;
    return factory->lpVtbl->QueryInterface(factory, riid, parent);
}

/* index n - 1 finds the monitor whose device is \\.\DISPLAYn. */
typedef struct FIND_MONITOR {
    UINT index;
    HMONITOR monitor;
} FIND_MONITOR;

static BOOL CALLBACK find_monitor(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM param)
{
    FIND_MONITOR *find = (FIND_MONITOR *)param;
    UINT number;
    if (DxgiMonitorNumber(monitor, &number) && number == find->index + 1) {
        find->monitor = monitor;
        return FALSE;
    }
    return TRUE;
}

/* outputs follow the order of the monitors' devices. */
static HRESULT STDMETHODCALLTYPE adapter_enum_outputs(IDXGIAdapter4 *iface, UINT index, IDXGIOutput **output)
{
    if (!output)
        return DXGI_ERROR_INVALID_CALL;
    *output = nullptr;
    DxgiAdapter *adapter = impl(iface);
    if (adapter->gpu != &DxgiGetVulkan()->gpus[0])
        return DXGI_ERROR_NOT_FOUND;
    FIND_MONITOR find = {index, nullptr};
    EnumDisplayMonitors(nullptr, nullptr, find_monitor, (LPARAM)&find);
    if (!find.monitor)
        return DXGI_ERROR_NOT_FOUND;
    return DxgiCreateOutput(adapter, find.monitor, output);
}

static HRESULT STDMETHODCALLTYPE adapter_get_desc3(IDXGIAdapter4 *iface, DXGI_ADAPTER_DESC3 *desc)
{
    if (!desc)
        return E_INVALIDARG;
    *desc = impl(iface)->gpu->desc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE adapter_get_desc2(IDXGIAdapter4 *iface, DXGI_ADAPTER_DESC2 *desc)
{
    if (!desc)
        return E_INVALIDARG;
    DXGI_ADAPTER_DESC3 d3;
    adapter_get_desc3(iface, &d3);
    memcpy(desc, &d3, sizeof(*desc));
    desc->Flags &= DXGI_ADAPTER_FLAG_REMOTE | DXGI_ADAPTER_FLAG_SOFTWARE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE adapter_get_desc1(IDXGIAdapter4 *iface, DXGI_ADAPTER_DESC1 *desc)
{
    if (!desc)
        return E_INVALIDARG;
    DXGI_ADAPTER_DESC2 d2;
    adapter_get_desc2(iface, &d2);
    memcpy(desc, &d2, sizeof(*desc));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE adapter_get_desc(IDXGIAdapter4 *iface, DXGI_ADAPTER_DESC *desc)
{
    if (!desc)
        return E_INVALIDARG;
    DXGI_ADAPTER_DESC1 d1;
    adapter_get_desc1(iface, &d1);
    memcpy(desc, &d1, sizeof(*desc));
    return S_OK;
}

static const GUID IID_ID3D10Device = {0x9B7E4C0F, 0x342C, 0x4106, {0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0}};
static const GUID IID_ID3D10Device1 = {0x9B7E4C8F, 0x342C, 0x4106, {0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0}};

/*
 * like windows, the user mode driver version is only answered for direct3d
 * 10, 10.1 and dxgi devices. windows also creates a 10.1 device to answer
 * for 10.1, which is not needed here: every gpu reaches 11.
 */
static HRESULT STDMETHODCALLTYPE adapter_check_interface_support(IDXGIAdapter4 *iface, REFGUID guid,
                                                                 LARGE_INTEGER *version)
{
    if (!guid)
        return DXGI_ERROR_INVALID_CALL;
    if (!IsEqualGUID(guid, &IID_ID3D10Device) && !IsEqualGUID(guid, &IID_ID3D10Device1) &&
        !IsEqualGUID(guid, &IID_IDXGIDevice))
        return DXGI_ERROR_UNSUPPORTED;
    if (version)
        *version = impl(iface)->gpu->umd_version;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE adapter_register_event(IDXGIAdapter4 *iface, HANDLE event, DWORD *cookie)
{
    static LONG next;
    if (!event || !cookie)
        return DXGI_ERROR_INVALID_CALL;
    *cookie = (DWORD)InterlockedIncrement(&next);
    return S_OK;
}

static void STDMETHODCALLTYPE adapter_unregister(IDXGIAdapter4 *iface, DWORD cookie) {}

#define BUDGET_EVENTS 16
#define BUDGET_POLL_MS 500
/* a budget that moved this much is news for the programs that listen. */
#define BUDGET_STEP (64ull << 20)

static SRWLOCK g_budget_lock = SRWLOCK_INIT;
static struct {
    HANDLE event;
    DWORD cookie;
    const DxgiGpu *gpu;
    UINT64 budget;
} g_budget_events[BUDGET_EVENTS];
static BOOL g_budget_watching;
/* what the process reserved in each segment of each gpu, the basic render driver last. */
static UINT64 g_reservations[RTL_NUMBER_OF(((DxgiVulkan *)0)->gpus) + 1][2];

static UINT64 *reservation_of(const DxgiGpu *gpu, DXGI_MEMORY_SEGMENT_GROUP group)
{
    const DxgiVulkan *vk = DxgiGetVulkan();
    UINT index = gpu == &vk->warp ? RTL_NUMBER_OF(vk->gpus) : (UINT)(gpu - vk->gpus);
    return &g_reservations[index][group];
}

/* budgets of vulkan's memory heaps, local being the gpu's own. */
static void memory_info(const DxgiGpu *gpu, DXGI_MEMORY_SEGMENT_GROUP group, DXGI_QUERY_VIDEO_MEMORY_INFO *info)
{
    memset(info, 0, sizeof(*info));
    /* the software adapter's only segment is its share of system memory. */
    if (gpu == &DxgiGetVulkan()->warp) {
        if (group == DXGI_MEMORY_SEGMENT_GROUP_LOCAL)
            info->Budget = gpu->desc.SharedSystemMemory;
    } else {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
        VkPhysicalDeviceMemoryProperties2 memory = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, &budget};
        DxgiGetVulkan()->get_memory2(gpu->physical, &memory);
        BOOL integrated = gpu->properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        for (UINT i = 0; i < memory.memoryProperties.memoryHeapCount; i++) {
            BOOL local = (memory.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) || integrated;
            if (local != (group == DXGI_MEMORY_SEGMENT_GROUP_LOCAL))
                continue;
            UINT64 size = memory.memoryProperties.memoryHeaps[i].size;
            info->Budget += budget.heapBudget[i] ? budget.heapBudget[i] : size;
            info->CurrentUsage += budget.heapUsage[i];
        }
    }
    info->AvailableForReservation = info->Budget / 2;
    info->CurrentReservation = *reservation_of(gpu, group);
}

static HRESULT STDMETHODCALLTYPE adapter_query_video_memory_info(IDXGIAdapter4 *iface, UINT node,
                                                                 DXGI_MEMORY_SEGMENT_GROUP group,
                                                                 DXGI_QUERY_VIDEO_MEMORY_INFO *info)
{
    if (node || !info || group > DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL)
        return DXGI_ERROR_INVALID_CALL;
    memory_info(impl(iface)->gpu, group, info);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE adapter_set_video_memory_reservation(IDXGIAdapter4 *iface, UINT node,
                                                                      DXGI_MEMORY_SEGMENT_GROUP group,
                                                                      UINT64 reservation)
{
    if (node || group > DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL)
        return DXGI_ERROR_INVALID_CALL;
    DXGI_QUERY_VIDEO_MEMORY_INFO info;
    memory_info(impl(iface)->gpu, group, &info);
    if (reservation > info.AvailableForReservation)
        return E_INVALIDARG;
    *reservation_of(impl(iface)->gpu, group) = reservation;
    return S_OK;
}

/* polls the local budgets while a program listens, and sets its event when one moved notably. */
static DWORD WINAPI budget_watch(void *arg)
{
    for (;;) {
        Sleep(BUDGET_POLL_MS);
        AcquireSRWLockExclusive(&g_budget_lock);
        BOOL listening = FALSE;
        for (UINT i = 0; i < BUDGET_EVENTS; i++) {
            if (!g_budget_events[i].event)
                continue;
            listening = TRUE;
            DXGI_QUERY_VIDEO_MEMORY_INFO info;
            memory_info(g_budget_events[i].gpu, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
            UINT64 last = g_budget_events[i].budget;
            if ((info.Budget > last ? info.Budget - last : last - info.Budget) >= BUDGET_STEP) {
                g_budget_events[i].budget = info.Budget;
                SetEvent(g_budget_events[i].event);
            }
        }
        if (!listening)
            g_budget_watching = FALSE;
        ReleaseSRWLockExclusive(&g_budget_lock);
        if (!listening)
            return 0;
    }
}

static HRESULT STDMETHODCALLTYPE adapter_register_budget_event(IDXGIAdapter4 *iface, HANDLE event, DWORD *cookie)
{
    static LONG next;
    if (!event || !cookie)
        return DXGI_ERROR_INVALID_CALL;
    HANDLE copy;
    if (!DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return DXGI_ERROR_INVALID_CALL;
    DXGI_QUERY_VIDEO_MEMORY_INFO info;
    memory_info(impl(iface)->gpu, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
    HRESULT hr = E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&g_budget_lock);
    for (UINT i = 0; i < BUDGET_EVENTS; i++) {
        if (g_budget_events[i].event)
            continue;
        g_budget_events[i].event = copy;
        g_budget_events[i].cookie = *cookie = (DWORD)InterlockedIncrement(&next);
        g_budget_events[i].gpu = impl(iface)->gpu;
        g_budget_events[i].budget = info.Budget;
        hr = S_OK;
        break;
    }
    if (SUCCEEDED(hr) && !g_budget_watching) {
        /* the watcher may outlive every factory, so the module stays. */
        HMODULE self;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           (const WCHAR *)budget_watch, &self);
        HANDLE thread = CreateThread(nullptr, 0, budget_watch, nullptr, 0, nullptr);
        if (thread) {
            g_budget_watching = TRUE;
            CloseHandle(thread);
        }
    }
    ReleaseSRWLockExclusive(&g_budget_lock);
    if (FAILED(hr))
        CloseHandle(copy);
    return hr;
}

static void STDMETHODCALLTYPE adapter_unregister_budget(IDXGIAdapter4 *iface, DWORD cookie)
{
    HANDLE event = nullptr;
    AcquireSRWLockExclusive(&g_budget_lock);
    for (UINT i = 0; i < BUDGET_EVENTS; i++)
        if (cookie && g_budget_events[i].cookie == cookie) {
            event = g_budget_events[i].event;
            memset(&g_budget_events[i], 0, sizeof(g_budget_events[i]));
        }
    ReleaseSRWLockExclusive(&g_budget_lock);
    if (event)
        CloseHandle(event);
}

static const IDXGIAdapter4Vtbl adapter_vtbl = {
    adapter_query,
    adapter_add_ref,
    adapter_release,
    adapter_set_private_data,
    adapter_set_private_data_interface,
    adapter_get_private_data,
    adapter_get_parent,
    adapter_enum_outputs,
    adapter_get_desc,
    adapter_check_interface_support,
    adapter_get_desc1,
    adapter_get_desc2,
    adapter_register_event,
    adapter_unregister,
    adapter_query_video_memory_info,
    adapter_set_video_memory_reservation,
    adapter_register_budget_event,
    adapter_unregister_budget,
    adapter_get_desc3,
};

static DxgiAdapter *mime_impl(IMimeDXGIAdapter *iface) { return CONTAINING_RECORD(iface, DxgiAdapter, mime); }

static HRESULT STDMETHODCALLTYPE mime_query(IMimeDXGIAdapter *iface, REFIID riid, void **out)
{
    return adapter_query(&mime_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE mime_add_ref(IMimeDXGIAdapter *iface)
{
    return adapter_add_ref(&mime_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE mime_release(IMimeDXGIAdapter *iface)
{
    return adapter_release(&mime_impl(iface)->iface);
}

static HRESULT STDMETHODCALLTYPE mime_get_vulkan(IMimeDXGIAdapter *iface, MIME_DXGI_VULKAN *out)
{
    const DxgiVulkan *vk = DxgiGetVulkan();
    out->Instance = vk->instance;
    out->PhysicalDevice = mime_impl(iface)->gpu->physical;
    out->GetInstanceProcAddr = (void *)vk->get_instance_proc;
    out->ApiVersion = vk->api_version;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mime_acquire_shared(IMimeDXGIAdapter *iface, const void *create_info,
                                                     const void *profile, UINT profile_size,
                                                     MIME_DXGI_SHARED_VK **shared)
{
    return DxgiAcquireShared(mime_impl(iface)->gpu, create_info, profile, profile_size, shared);
}

static void STDMETHODCALLTYPE mime_release_shared(IMimeDXGIAdapter *iface, MIME_DXGI_SHARED_VK *shared)
{
    DxgiReleaseShared(shared);
}

static HRESULT STDMETHODCALLTYPE mime_publish_semaphore(IMimeDXGIAdapter *iface, MIME_DXGI_SHARED_VK *shared, UINT64 id,
                                                        void *semaphore, void **entry)
{
    return DxgiPublishSemaphore(shared, id, semaphore, entry);
}

static HRESULT STDMETHODCALLTYPE mime_open_semaphore(IMimeDXGIAdapter *iface, MIME_DXGI_SHARED_VK *shared, UINT64 id,
                                                     void **semaphore, void **entry)
{
    return DxgiOpenSemaphore(shared, id, semaphore, entry);
}

static void STDMETHODCALLTYPE mime_release_semaphore(IMimeDXGIAdapter *iface, MIME_DXGI_SHARED_VK *shared, void *entry)
{
    DxgiReleaseSemaphore(shared, entry);
}

static const IMimeDXGIAdapterVtbl mime_vtbl = {
    mime_query,           mime_add_ref,        mime_release,           mime_get_vulkan,
    mime_acquire_shared,  mime_release_shared, mime_publish_semaphore, mime_open_semaphore,
    mime_release_semaphore};

HRESULT DxgiCreateAdapter(DxgiFactory *factory, const DxgiGpu *gpu, REFIID riid, void **out)
{
    DxgiAdapter *adapter = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*adapter));
    if (!adapter)
        return E_OUTOFMEMORY;
    adapter->iface.lpVtbl = &adapter_vtbl;
    adapter->mime.lpVtbl = &mime_vtbl;
    adapter->refs = 1;
    adapter->factory = factory;
    factory->iface.lpVtbl->AddRef(&factory->iface);
    adapter->gpu = gpu;
    HRESULT hr = adapter_query(&adapter->iface, riid, out);
    adapter_release(&adapter->iface);
    return hr;
}

DxgiAdapter *DxgiAdapterFrom(IUnknown *unknown)
{
    IMimeDXGIAdapter *mime = nullptr;
    if (!unknown || FAILED(unknown->lpVtbl->QueryInterface(unknown, &IID_IMimeDXGIAdapter, (void **)&mime)))
        return nullptr;
    mime->lpVtbl->Release(mime);
    return mime->lpVtbl == &mime_vtbl ? mime_impl(mime) : nullptr;
}
