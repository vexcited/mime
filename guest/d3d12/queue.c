/*
 * command queues and fences. a queue submits to one of the device's vulkan
 * queues, a fence is a timeline semaphore. the host runs vulkan's waits on
 * the thread that runs every call of the process, so a wait on the cpu may
 * only block on what a submission already signals, and briefly: fences
 * waited for with events are polled by a thread of the device.
 */
#include "d3d12_private.h"

/* ------------------------------------------------------------------ fences */

static D12Fence *fence_impl(ID3D12Fence1 *iface) { return CONTAINING_RECORD(iface, D12Fence, iface); }

D12Fence *D12FenceFrom(ID3D12Fence *iface) { return iface ? CONTAINING_RECORD(iface, D12Fence, iface) : NULL; }

/* the value the gpu or cpu signalled last, asked of the host. */
UINT64 D12FenceValue(D12Fence *fence)
{
    D12Device *device = fence->child.device;
    UINT64 value = fence->completed;
    if (device->vk.vkGetSemaphoreCounterValue(device->vk.device, fence->semaphore, &value) == VK_SUCCESS)
        fence->completed = value;
    return fence->completed;
}

/* waits on the host until the fence reaches a value or a timeout in nanoseconds passes. */
static BOOL fence_wait_host(D12Fence *fence, UINT64 value, UINT64 timeout)
{
    D12Device *device = fence->child.device;
    VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &fence->semaphore;
    wait.pValues = &value;
    if (device->vk.vkWaitSemaphores(device->vk.device, &wait, timeout) == VK_SUCCESS) {
        if (value > fence->completed)
            fence->completed = value;
        return TRUE;
    }
    return FALSE;
}

/* blocks until the fence reaches a value: on the host once its signal is submitted, polling until then. */
static void fence_block(D12Fence *fence, UINT64 value)
{
    while (D12FenceValue(fence) < value) {
        if (fence->queued >= value)
            fence_wait_host(fence, value, 2000000);
        else
            Sleep(1);
    }
}

/* the waiter thread: sets the events of satisfied waits, polling while any wait is left. */
static DWORD WINAPI waiter_main(void *param)
{
    D12Device *device = param;
    while (!device->waiter_stop) {
        BOOL pending = FALSE, submitted = FALSE;
        D12Fence *submitted_fence = NULL;
        UINT64 submitted_value = 0;
        AcquireSRWLockExclusive(&device->fence_lock);
        for (D12Fence **link = &device->waiting_fences; *link;) {
            D12Fence *fence = *link;
            UINT64 value = D12FenceValue(fence);
            AcquireSRWLockExclusive(&fence->lock);
            for (UINT i = 0; i < fence->wait_count;) {
                if (fence->waits[i].value <= value) {
                    SetEvent(fence->waits[i].event);
                    fence->waits[i] = fence->waits[--fence->wait_count];
                } else {
                    if (!submitted && fence->queued >= fence->waits[i].value) {
                        submitted = TRUE;
                        submitted_fence = fence;
                        submitted_value = fence->waits[i].value;
                    }
                    i++;
                }
            }
            BOOL empty = !fence->wait_count;
            ReleaseSRWLockExclusive(&fence->lock);
            if (empty) {
                *link = fence->next_waiting;
                fence->next_waiting = NULL;
                fence->iface.lpVtbl->Release(&fence->iface);
            } else {
                pending = TRUE;
                link = &fence->next_waiting;
            }
        }
        if (submitted)
            submitted_fence->iface.lpVtbl->AddRef(&submitted_fence->iface);
        ReleaseSRWLockExclusive(&device->fence_lock);
        if (submitted) {
            fence_wait_host(submitted_fence, submitted_value, 1000000);
            submitted_fence->iface.lpVtbl->Release(&submitted_fence->iface);
        } else if (pending) {
            WaitForSingleObject(device->waiter_wake, 1);
        } else {
            WaitForSingleObject(device->waiter_wake, INFINITE);
        }
    }
    return 0;
}

void D12FenceWake(D12Device *device)
{
    if (device->waiter)
        SetEvent(device->waiter_wake);
}

void D12FenceWaiterStop(D12Device *device)
{
    if (!device->waiter)
        return;
    device->waiter_stop = TRUE;
    SetEvent(device->waiter_wake);
    WaitForSingleObject(device->waiter, INFINITE);
    CloseHandle(device->waiter);
    device->waiter = NULL;
}

static HRESULT add_wait(D12Fence *fence, UINT64 value, HANDLE event)
{
    D12Device *device = fence->child.device;
    AcquireSRWLockExclusive(&device->fence_lock);
    if (!device->waiter && !(device->waiter = CreateThread(NULL, 0, waiter_main, device, 0, NULL))) {
        ReleaseSRWLockExclusive(&device->fence_lock);
        return E_OUTOFMEMORY;
    }
    AcquireSRWLockExclusive(&fence->lock);
    if (fence->wait_count == fence->wait_capacity) {
        UINT capacity = fence->wait_capacity ? fence->wait_capacity * 2 : 8;
        D12FenceWait *waits = fence->waits ? HeapReAlloc(GetProcessHeap(), 0, fence->waits, capacity * sizeof(*waits))
                                           : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*waits));
        if (!waits) {
            ReleaseSRWLockExclusive(&fence->lock);
            ReleaseSRWLockExclusive(&device->fence_lock);
            return E_OUTOFMEMORY;
        }
        fence->waits = waits;
        fence->wait_capacity = capacity;
    }
    fence->waits[fence->wait_count++] = (D12FenceWait){value, event};
    BOOL listed = fence->wait_count > 1 || fence->next_waiting || device->waiting_fences == fence;
    ReleaseSRWLockExclusive(&fence->lock);
    if (!listed) {
        fence->iface.lpVtbl->AddRef(&fence->iface);
        fence->next_waiting = device->waiting_fences;
        device->waiting_fences = fence;
    }
    ReleaseSRWLockExclusive(&device->fence_lock);
    D12FenceWake(device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fence_query(ID3D12Fence1 *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceChild) || IsEqualGUID(riid, &IID_ID3D12Pageable) ||
        IsEqualGUID(riid, &IID_ID3D12Fence) || IsEqualGUID(riid, &IID_ID3D12Fence1)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE fence_add_ref(ID3D12Fence1 *iface) { return InterlockedIncrement(&fence_impl(iface)->child.refs); }

static ULONG STDMETHODCALLTYPE fence_release(ID3D12Fence1 *iface)
{
    D12Fence *fence = fence_impl(iface);
    ULONG refs = InterlockedDecrement(&fence->child.refs);
    if (!refs) {
        D12Device *device = fence->child.device;
        device->vk.vkDestroySemaphore(device->vk.device, fence->semaphore, NULL);
        HeapFree(GetProcessHeap(), 0, fence->waits);
        D12ChildRelease(&fence->child);
        HeapFree(GetProcessHeap(), 0, fence);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE fence_get_private_data(ID3D12Fence1 *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&fence_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE fence_set_private_data(ID3D12Fence1 *iface, REFGUID guid, UINT size, const void *data)
{
    return MimePrivateSet(&fence_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE fence_set_private_data_interface(ID3D12Fence1 *iface, REFGUID guid,
                                                                  const IUnknown *data)
{
    return MimePrivateSetInterface(&fence_impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE fence_set_name(ID3D12Fence1 *iface, LPCWSTR name) { return S_OK; }

static HRESULT STDMETHODCALLTYPE fence_get_device(ID3D12Fence1 *iface, REFIID riid, void **device)
{
    return D12ChildGetDevice(&fence_impl(iface)->child, riid, device);
}

static UINT64 STDMETHODCALLTYPE fence_get_completed_value(ID3D12Fence1 *iface)
{
    return D12FenceValue(fence_impl(iface));
}

static HRESULT STDMETHODCALLTYPE fence_set_event_on_completion(ID3D12Fence1 *iface, UINT64 value, HANDLE event)
{
    D12Fence *fence = fence_impl(iface);
    if (D12FenceValue(fence) >= value) {
        if (event)
            SetEvent(event);
        return S_OK;
    }
    if (!event) {
        fence_block(fence, value);
        return S_OK;
    }
    return add_wait(fence, value, event);
}

/* vulkan's timelines only go up: a value at or below the current one is not signalled again. */
static HRESULT STDMETHODCALLTYPE fence_signal(ID3D12Fence1 *iface, UINT64 value)
{
    D12Fence *fence = fence_impl(iface);
    D12Device *device = fence->child.device;
    if (value > fence->queued)
        fence->queued = value;
    if (value <= D12FenceValue(fence))
        return S_OK;
    VkSemaphoreSignalInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
    info.semaphore = fence->semaphore;
    info.value = value;
    device->vk.vkSignalSemaphore(device->vk.device, &info);
    fence->completed = value;
    D12FenceWake(device);
    return S_OK;
}

static D3D12_FENCE_FLAGS STDMETHODCALLTYPE fence_get_creation_flags(ID3D12Fence1 *iface)
{
    return fence_impl(iface)->flags;
}

static const struct ID3D12Fence1Vtbl g_fence_vtbl = {
    fence_query,
    fence_add_ref,
    fence_release,
    fence_get_private_data,
    fence_set_private_data,
    fence_set_private_data_interface,
    fence_set_name,
    fence_get_device,
    fence_get_completed_value,
    fence_set_event_on_completion,
    fence_signal,
    fence_get_creation_flags,
};

HRESULT D12CreateFence(D12Device *device, UINT64 value, D3D12_FENCE_FLAGS flags, D12Fence **out)
{
    D12Fence *fence = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*fence));
    if (!fence)
        return E_OUTOFMEMORY;
    fence->iface.lpVtbl = &g_fence_vtbl;
    fence->flags = flags;
    fence->completed = fence->queued = value;
    InitializeSRWLock(&fence->lock);
    VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = value;
    VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    if (device->vk.vkCreateSemaphore(device->vk.device, &info, NULL, &fence->semaphore) != VK_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, fence);
        return E_OUTOFMEMORY;
    }
    fence->child.refs = 1;
    D12ChildInit(&fence->child, device);
    *out = fence;
    return S_OK;
}

/* SetEventOnMultipleFenceCompletion: each fence waited for in turn, the event set once all or any are done. */
HRESULT D12SetEventOnFences(D12Device *device, ID3D12Fence *const *fences, const UINT64 *values, UINT count,
                            UINT flags, HANDLE event)
{
    if (!count || !fences || !values)
        return E_INVALIDARG;
    BOOL any = flags & 1;
    UINT done = 0;
    for (UINT i = 0; i < count; i++)
        if (D12FenceValue(D12FenceFrom(fences[i])) >= values[i])
            done++;
    if (done == count || (any && done)) {
        if (event)
            SetEvent(event);
        return S_OK;
    }
    if (!event) {
        for (;;) {
            done = 0;
            for (UINT i = 0; i < count; i++)
                if (D12FenceValue(D12FenceFrom(fences[i])) >= values[i])
                    done++;
            if (done == count || (any && done))
                return S_OK;
            Sleep(1);
        }
    }
    /* guessed: with an event, the fences' own waits stand in, the last one setting it for all of them. */
    if (any || count == 1)
        return add_wait(D12FenceFrom(fences[0]), values[0], event);
    for (UINT i = 0; i < count; i++)
        fence_block(D12FenceFrom(fences[i]), values[i]);
    SetEvent(event);
    return S_OK;
}

/* ------------------------------------------------------------------ queues */

static D12Queue *queue_impl(ID3D12CommandQueue *iface) { return CONTAINING_RECORD(iface, D12Queue, iface); }

D12Queue *D12QueueFrom(ID3D12CommandQueue *iface) { return iface ? queue_impl(iface) : NULL; }

/*
 * submits command buffers, the device's own commands first, waiting and
 * signalling as asked, then the queue's timeline. returns the serial the
 * submission signals on it.
 */
UINT64 D12QueueSubmit(D12Queue *queue, const VkCommandBuffer *buffers, UINT count, const VkSemaphoreSubmitInfo *waits,
                      UINT wait_count, const VkSemaphoreSubmitInfo *signals, UINT signal_count)
{
    D12Device *device = queue->child.device;
    UINT64 init_serial;
    VkCommandBuffer init = D12TakeInitCommands(device, &init_serial);
    VkCommandBufferSubmitInfo cmds[64];
    VkSemaphoreSubmitInfo wait_infos[9], signal_infos[9];
    UINT n = 0, w = 0, s = 0;
    if (init)
        cmds[n++] = (VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, NULL, init};
    for (UINT i = 0; i < count && n < RTL_NUMBER_OF(cmds); i++)
        cmds[n++] = (VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, NULL, buffers[i]};
    /* another queue's submission ran the device's commands: this one waits for them. */
    if (!init && init_serial) {
        wait_infos[w] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait_infos[w].semaphore = device->init_timeline;
        wait_infos[w].value = init_serial;
        wait_infos[w++].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
    for (UINT i = 0; i < wait_count && w < RTL_NUMBER_OF(wait_infos); i++)
        wait_infos[w++] = waits[i];
    if (init) {
        signal_infos[s] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal_infos[s].semaphore = device->init_timeline;
        signal_infos[s].value = init_serial;
        signal_infos[s++].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
    for (UINT i = 0; i < signal_count && s < RTL_NUMBER_OF(signal_infos) - 1; i++)
        signal_infos[s++] = signals[i];
    AcquireSRWLockExclusive(queue->queue_lock);
    UINT64 serial = queue->next_serial++;
    signal_infos[s] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal_infos[s].semaphore = queue->timeline;
    signal_infos[s].value = serial;
    signal_infos[s++].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = w;
    submit.pWaitSemaphoreInfos = wait_infos;
    submit.commandBufferInfoCount = n;
    submit.pCommandBufferInfos = cmds;
    submit.signalSemaphoreInfoCount = s;
    submit.pSignalSemaphoreInfos = signal_infos;
    if (device->vk.vkQueueSubmit2(queue->queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
        D12Log("d3d12: vkQueueSubmit2 failed");
    ReleaseSRWLockExclusive(queue->queue_lock);
    return serial;
}

void D12QueueWaitIdle(D12Queue *queue)
{
    D12Device *device = queue->child.device;
    UINT64 serial = queue->next_serial - 1;
    if (!serial)
        return;
    VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &queue->timeline;
    wait.pValues = &serial;
    device->vk.vkWaitSemaphores(device->vk.device, &wait, UINT64_MAX);
}

static HRESULT STDMETHODCALLTYPE queue_query(ID3D12CommandQueue *iface, REFIID riid, void **out)
{
    D12Queue *queue = queue_impl(iface);
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceChild) || IsEqualGUID(riid, &IID_ID3D12Pageable) ||
        IsEqualGUID(riid, &IID_ID3D12CommandQueue)) {
        *out = &queue->iface;
    } else if (IsEqualGUID(riid, &IID_IMimeDXGIPresenter)) {
        *out = &queue->presenter;
    } else {
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&queue->child.refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE queue_add_ref(ID3D12CommandQueue *iface)
{
    return InterlockedIncrement(&queue_impl(iface)->child.refs);
}

static ULONG STDMETHODCALLTYPE queue_release(ID3D12CommandQueue *iface)
{
    D12Queue *queue = queue_impl(iface);
    ULONG refs = InterlockedDecrement(&queue->child.refs);
    if (!refs) {
        D12Device *device = queue->child.device;
        D12QueueWaitIdle(queue);
        device->vk.vkDestroySemaphore(device->vk.device, queue->timeline, NULL);
        D12ChildRelease(&queue->child);
        HeapFree(GetProcessHeap(), 0, queue);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE queue_get_private_data(ID3D12CommandQueue *iface, REFGUID guid, UINT *size,
                                                        void *data)
{
    return MimePrivateGet(&queue_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE queue_set_private_data(ID3D12CommandQueue *iface, REFGUID guid, UINT size,
                                                        const void *data)
{
    return MimePrivateSet(&queue_impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE queue_set_private_data_interface(ID3D12CommandQueue *iface, REFGUID guid,
                                                                  const IUnknown *data)
{
    return MimePrivateSetInterface(&queue_impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE queue_set_name(ID3D12CommandQueue *iface, LPCWSTR name) { return S_OK; }

static HRESULT STDMETHODCALLTYPE queue_get_device(ID3D12CommandQueue *iface, REFIID riid, void **device)
{
    return D12ChildGetDevice(&queue_impl(iface)->child, riid, device);
}

/* tiled resources are not supported. */
static void STDMETHODCALLTYPE queue_update_tile_mappings(ID3D12CommandQueue *iface, ID3D12Resource *resource,
                                                         UINT region_count,
                                                         const D3D12_TILED_RESOURCE_COORDINATE *region_starts,
                                                         const D3D12_TILE_REGION_SIZE *region_sizes, ID3D12Heap *heap,
                                                         UINT range_count, const D3D12_TILE_RANGE_FLAGS *range_flags,
                                                         const UINT *heap_starts, const UINT *range_tile_counts,
                                                         D3D12_TILE_MAPPING_FLAGS flags)
{
}

static void STDMETHODCALLTYPE queue_copy_tile_mappings(ID3D12CommandQueue *iface, ID3D12Resource *dst,
                                                       const D3D12_TILED_RESOURCE_COORDINATE *dst_start,
                                                       ID3D12Resource *src,
                                                       const D3D12_TILED_RESOURCE_COORDINATE *src_start,
                                                       const D3D12_TILE_REGION_SIZE *size,
                                                       D3D12_TILE_MAPPING_FLAGS flags)
{
}

VkCommandBuffer D12CommandListBuffer(ID3D12CommandList *list);

static void STDMETHODCALLTYPE queue_execute_command_lists(ID3D12CommandQueue *iface, UINT count,
                                                          ID3D12CommandList *const *lists)
{
    D12Queue *queue = queue_impl(iface);
    VkCommandBuffer buffers[64];
    UINT n = 0;
    for (UINT i = 0; i < count; i++) {
        VkCommandBuffer cmd = lists[i] ? D12CommandListBuffer(lists[i]) : VK_NULL_HANDLE;
        if (!cmd)
            continue;
        if (n == RTL_NUMBER_OF(buffers)) {
            D12QueueSubmit(queue, buffers, n, NULL, 0, NULL, 0);
            n = 0;
        }
        buffers[n++] = cmd;
    }
    D12QueueSubmit(queue, buffers, n, NULL, 0, NULL, 0);
}

static void STDMETHODCALLTYPE queue_set_marker(ID3D12CommandQueue *iface, UINT metadata, const void *data, UINT size)
{
}

static void STDMETHODCALLTYPE queue_end_event(ID3D12CommandQueue *iface) {}

static HRESULT STDMETHODCALLTYPE queue_signal(ID3D12CommandQueue *iface, ID3D12Fence *fence_iface, UINT64 value)
{
    D12Queue *queue = queue_impl(iface);
    D12Fence *fence = D12FenceFrom(fence_iface);
    if (!fence)
        return E_INVALIDARG;
    /* vulkan's timelines only go up: a value a signal already promised is not signalled again. */
    if (value <= fence->queued)
        return S_OK;
    fence->queued = value;
    VkSemaphoreSubmitInfo signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = fence->semaphore;
    signal.value = value;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    D12QueueSubmit(queue, NULL, 0, NULL, 0, &signal, 1);
    D12FenceWake(queue->child.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE queue_wait(ID3D12CommandQueue *iface, ID3D12Fence *fence_iface, UINT64 value)
{
    D12Queue *queue = queue_impl(iface);
    D12Fence *fence = D12FenceFrom(fence_iface);
    if (!fence)
        return E_INVALIDARG;
    VkSemaphoreSubmitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = fence->semaphore;
    wait.value = value;
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    D12QueueSubmit(queue, NULL, 0, &wait, 1, NULL, 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE queue_get_timestamp_frequency(ID3D12CommandQueue *iface, UINT64 *frequency)
{
    if (!frequency)
        return E_INVALIDARG;
    FLOAT period = queue_impl(iface)->child.device->properties.limits.timestampPeriod;
    *frequency = (UINT64)(1e9 / (period > 0 ? period : 1.0f));
    return S_OK;
}

/* guessed: vulkan here cannot tie the gpu's clock to the cpu's, so the gpu timestamp is the cpu's in its units. */
static HRESULT STDMETHODCALLTYPE queue_get_clock_calibration(ID3D12CommandQueue *iface, UINT64 *gpu, UINT64 *cpu)
{
    if (!gpu || !cpu)
        return E_INVALIDARG;
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    UINT64 gpu_frequency;
    queue_get_timestamp_frequency(iface, &gpu_frequency);
    *cpu = counter.QuadPart;
    *gpu = (UINT64)((double)counter.QuadPart * gpu_frequency / frequency.QuadPart);
    return S_OK;
}

static D3D12_COMMAND_QUEUE_DESC *STDMETHODCALLTYPE queue_get_desc(ID3D12CommandQueue *iface,
                                                                  D3D12_COMMAND_QUEUE_DESC *ret)
{
    *ret = queue_impl(iface)->desc;
    return ret;
}

static const struct ID3D12CommandQueueVtbl g_queue_vtbl = {
    queue_query,
    queue_add_ref,
    queue_release,
    queue_get_private_data,
    queue_set_private_data,
    queue_set_private_data_interface,
    queue_set_name,
    queue_get_device,
    queue_update_tile_mappings,
    queue_copy_tile_mappings,
    queue_execute_command_lists,
    queue_set_marker,
    queue_set_marker,
    queue_end_event,
    queue_signal,
    queue_wait,
    queue_get_timestamp_frequency,
    queue_get_clock_calibration,
    queue_get_desc,
};

/* ------------------------------------------------------------------ IMimeDXGIPresenter */

static D12Queue *presenter_impl(IMimeDXGIPresenter *iface) { return CONTAINING_RECORD(iface, D12Queue, presenter); }

static HRESULT STDMETHODCALLTYPE presenter_query(IMimeDXGIPresenter *iface, REFIID riid, void **out)
{
    return queue_query(&presenter_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE presenter_add_ref(IMimeDXGIPresenter *iface)
{
    return queue_add_ref(&presenter_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE presenter_release(IMimeDXGIPresenter *iface)
{
    return queue_release(&presenter_impl(iface)->iface);
}

static HRESULT STDMETHODCALLTYPE presenter_create_swap_chain(IMimeDXGIPresenter *iface, IDXGIFactory *factory,
                                                             HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
                                                             const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,
                                                             IDXGIOutput *restrict_to, IDXGISwapChain1 **swapchain)
{
    D12Queue *queue = presenter_impl(iface);
    if (queue->desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        return DXGI_ERROR_INVALID_CALL;
    return D12CreateSwapChain(queue, factory, window, desc, fullscreen, swapchain);
}

static const struct IMimeDXGIPresenterVtbl g_presenter_vtbl = {
    presenter_query,
    presenter_add_ref,
    presenter_release,
    presenter_create_swap_chain,
};

HRESULT D12CreateQueue(D12Device *device, const D3D12_COMMAND_QUEUE_DESC *desc, D12Queue **out)
{
    if (desc->Type != D3D12_COMMAND_LIST_TYPE_DIRECT && desc->Type != D3D12_COMMAND_LIST_TYPE_COMPUTE &&
        desc->Type != D3D12_COMMAND_LIST_TYPE_COPY)
        return E_INVALIDARG;
    D12Queue *queue = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*queue));
    if (!queue)
        return E_OUTOFMEMORY;
    queue->iface.lpVtbl = &g_queue_vtbl;
    queue->presenter.lpVtbl = &g_presenter_vtbl;
    queue->desc = *desc;
    if (!queue->desc.NodeMask)
        queue->desc.NodeMask = 1;
    /* the queues of a family in turn, so queues of the program run beside each other as far as there are. */
    UINT index = (UINT)InterlockedIncrement(&device->next_queue) % device->queue_count;
    queue->queue = device->queues[index];
    queue->queue_lock = &device->queue_locks[index];
    queue->family = device->family;
    queue->next_serial = 1;
    VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    if (device->vk.vkCreateSemaphore(device->vk.device, &info, NULL, &queue->timeline) != VK_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, queue);
        return E_OUTOFMEMORY;
    }
    queue->child.refs = 1;
    D12ChildInit(&queue->child, device);
    *out = queue;
    return S_OK;
}
