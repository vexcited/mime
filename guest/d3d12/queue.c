/*
 * command queues and fences. a queue submits to one of the device's Vulkan queues and a fence is a timeline
 * semaphore. cpu waits are Vulkan's, which the host's watcher serves without blocking the process's other calls,
 * even for values no submission signals yet.
 */
#include "d3d12_private.h"

/* ------------------------------------------------------------------ fences */

static D12QueueOp *release_held(D12Device *device, D12QueueOp *done);
static void free_ops(D12QueueOp *op);

static D12Fence *fence_impl(ID3D12Fence1 *iface) { return CONTAINING_RECORD(iface, D12Fence, iface); }

D12Fence *D12FenceFrom(ID3D12Fence *iface) { return iface ? CONTAINING_RECORD(iface, D12Fence, iface) : nullptr; }

/* asks the host for the last value the gpu or cpu signalled. */
UINT64 D12FenceValue(D12Fence *fence)
{
    D12Device *device = fence->child.device;
    UINT64 value = fence->completed;
    if (device->vk.vkGetSemaphoreCounterValue(device->vk.device, fence->semaphore, &value) == VK_SUCCESS &&
        value > fence->completed)
        fence->completed = value;
    return fence->completed;
}

static void fences_block(D12Device *device, UINT count, ID3D12Fence *const *fences, const UINT64 *values, BOOL any)
{
    VkSemaphore semaphores[64];
    count = D12_MIN(count, (UINT)RTL_NUMBER_OF(semaphores));
    for (UINT i = 0; i < count; i++)
        semaphores[i] = D12FenceFrom(fences[i])->semaphore;
    VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.flags = any ? VK_SEMAPHORE_WAIT_ANY_BIT : 0;
    wait.semaphoreCount = count;
    wait.pSemaphores = semaphores;
    wait.pValues = values;
    device->vk.vkWaitSemaphores(device->vk.device, &wait, UINT64_MAX);
}

static void fence_block(D12Fence *fence, UINT64 value)
{
    ID3D12Fence *iface = (ID3D12Fence *)&fence->iface;
    fences_block(fence->child.device, 1, &iface, &value, FALSE);
    /* the host's value reached it, which its mirror may not show yet. */
    if (value > fence->completed)
        fence->completed = value;
}

static BOOL grow_array(void **array, UINT *capacity, UINT count, SIZE_T size)
{
    if (count < *capacity)
        return TRUE;
    UINT grown = *capacity ? *capacity * 2 : 8;
    void *larger = *array ? HeapReAlloc(GetProcessHeap(), 0, *array, grown * size) : HeapAlloc(GetProcessHeap(), 0, grown * size);
    if (!larger)
        return FALSE;
    *array = larger;
    *capacity = grown;
    return TRUE;
}

/*
 * sets the events of satisfied waits, then waits for any waiting fence's next value or for the wake semaphore,
 * which a new wait signals. A fence nobody waits on any more is let go before the events are set, so a program that
 * wakes on its event and releases its objects finds nothing of the device still held by this thread.
 */
static DWORD WINAPI waiter_main(void *param)
{
    D12Device *device = param;
    VkSemaphore *semaphores = nullptr;
    UINT64 *values = nullptr;
    UINT capacity = 0;
    HANDLE *fired = nullptr;
    D12Fence **done = nullptr;
    UINT fired_count = 0, fired_capacity = 0, done_count = 0, done_capacity = 0;
    BOOL gone = FALSE;
    device->waiter_id = GetCurrentThreadId();
    device->waiter_exit = &gone;
    while (!device->waiter_stop) {
        UINT64 wakes = __atomic_load_n((const volatile UINT64 *)&device->waiter_wakes, __ATOMIC_ACQUIRE);
        UINT n = 0;
        AcquireSRWLockExclusive(&device->fence_lock);
        for (D12Fence **link = &device->waiting_fences; *link;) {
            D12Fence *fence = *link;
            UINT64 value = D12FenceValue(fence), next = UINT64_MAX;
            AcquireSRWLockExclusive(&fence->lock);
            for (UINT i = 0; i < fence->wait_count;) {
                if (fence->waits[i].value <= value) {
                    if (grow_array((void **)&fired, &fired_capacity, fired_count, sizeof(*fired)))
                        fired[fired_count++] = fence->waits[i].event;
                    else
                        SetEvent(fence->waits[i].event);
                    fence->waits[i] = fence->waits[--fence->wait_count];
                } else {
                    next = D12_MIN(next, fence->waits[i].value);
                    i++;
                }
            }
            BOOL empty = !fence->wait_count;
            ReleaseSRWLockExclusive(&fence->lock);
            if (empty) {
                *link = fence->next_waiting;
                fence->next_waiting = nullptr;
                if (grow_array((void **)&done, &done_capacity, done_count, sizeof(*done)))
                    done[done_count++] = fence;
                else
                    fence->iface.lpVtbl->Release(&fence->iface);
                continue;
            }
            if (n + 2 > capacity) {
                UINT grown = capacity ? capacity * 2 : 16;
                VkSemaphore *s = HeapAlloc(GetProcessHeap(), 0, grown * sizeof(*s));
                UINT64 *v = HeapAlloc(GetProcessHeap(), 0, grown * sizeof(*v));
                if (s && v) {
                    if (n) {
                        memcpy(s, semaphores, n * sizeof(*s));
                        memcpy(v, values, n * sizeof(*v));
                    }
                    HeapFree(GetProcessHeap(), 0, semaphores);
                    HeapFree(GetProcessHeap(), 0, values);
                    semaphores = s, values = v, capacity = grown;
                } else {
                    HeapFree(GetProcessHeap(), 0, s);
                    HeapFree(GetProcessHeap(), 0, v);
                }
            }
            if (n + 1 < capacity) {
                semaphores[n] = fence->semaphore;
                values[n++] = next;
            }
            link = &fence->next_waiting;
        }
        ReleaseSRWLockExclusive(&device->fence_lock);
        /* the last reference of a program that has let go of everything is this one, and ends the device here. */
        for (UINT i = 0; i < done_count; i++)
            done[i]->iface.lpVtbl->Release(&done[i]->iface);
        for (UINT i = 0; i < fired_count; i++)
            SetEvent(fired[i]);
        done_count = fired_count = 0;
        if (gone)
            break;
        VkSemaphore wake = device->waiter_wake;
        UINT64 woken = wakes + 1;
        VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wait.flags = VK_SEMAPHORE_WAIT_ANY_BIT;
        wait.semaphoreCount = n + 1;
        wait.pSemaphores = n ? semaphores : &wake;
        wait.pValues = n ? values : &woken;
        if (n) {
            semaphores[n] = wake;
            values[n] = woken;
        }
        device->vk.vkWaitSemaphores(device->vk.device, &wait, UINT64_MAX);
    }
    HeapFree(GetProcessHeap(), 0, semaphores);
    HeapFree(GetProcessHeap(), 0, values);
    HeapFree(GetProcessHeap(), 0, fired);
    HeapFree(GetProcessHeap(), 0, done);
    return 0;
}

void D12FenceWake(D12Device *device)
{
    if (!device->waiter)
        return;
    VkSemaphoreSignalInfo signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
    signal.semaphore = device->waiter_wake;
    signal.value = (UINT64)InterlockedIncrement64(&device->waiter_wakes);
    device->vk.vkSignalSemaphore(device->vk.device, &signal);
}

void D12FenceWaiterStop(D12Device *device)
{
    if (!device->waiter)
        return;
    device->waiter_stop = TRUE;
    if (GetCurrentThreadId() == device->waiter_id) {
        /* the waiter let go of the last reference itself, and leaves its loop when this returns. */
        *device->waiter_exit = TRUE;
        CloseHandle(device->waiter);
        device->waiter = nullptr;
        return;
    }
    D12FenceWake(device);
    WaitForSingleObject(device->waiter, INFINITE);
    CloseHandle(device->waiter);
    device->waiter = nullptr;
}

static HRESULT add_wait(D12Fence *fence, UINT64 value, HANDLE event)
{
    D12Device *device = fence->child.device;
    AcquireSRWLockExclusive(&device->fence_lock);
    if (!device->waiter && !(device->waiter = CreateThread(nullptr, 0, waiter_main, device, 0, nullptr))) {
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
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE fence_add_ref(ID3D12Fence1 *iface) { return InterlockedIncrement(&fence_impl(iface)->child.refs); }

static ULONG STDMETHODCALLTYPE fence_release(ID3D12Fence1 *iface)
{
    D12Fence *fence = fence_impl(iface);
    ULONG refs = InterlockedDecrement(&fence->child.refs);
    if (!refs) {
        D12Device *device = fence->child.device;
        if (fence->entry)
            device->shared.adapter->lpVtbl->ReleaseSemaphore(device->shared.adapter, device->shared.vk, fence->entry);
        else
            device->vk.vkDestroySemaphore(device->vk.device, fence->semaphore, nullptr);
        MimeShareClose(&fence->share);
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

/*
 * Vulkan timelines only go up: a value at or below the current one is not signalled again. queues holding back a
 * wait for the value go on, after the signal so a submission that waits for it under the lock is not held up.
 */
static HRESULT STDMETHODCALLTYPE fence_signal(ID3D12Fence1 *iface, UINT64 value)
{
    D12Fence *fence = fence_impl(iface);
    D12Device *device = fence->child.device;
    BOOL higher = value > D12FenceValue(fence);
    if (higher) {
        VkSemaphoreSignalInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
        info.semaphore = fence->semaphore;
        info.value = value;
        device->vk.vkSignalSemaphore(device->vk.device, &info);
        fence->completed = value;
    }
    AcquireSRWLockExclusive(&device->submit_lock);
    if (value > fence->queued)
        fence->queued = value;
    D12QueueOp *done = release_held(device, nullptr);
    ReleaseSRWLockExclusive(&device->submit_lock);
    free_ops(done);
    if (higher)
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

static D12Fence *alloc_fence(D12Device *device, UINT64 value, D3D12_FENCE_FLAGS flags)
{
    D12Fence *fence = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*fence));
    if (!fence)
        return nullptr;
    fence->iface.lpVtbl = &g_fence_vtbl;
    fence->flags = flags;
    fence->completed = fence->queued = value;
    InitializeSRWLock(&fence->lock);
    fence->child.refs = 1;
    D12ChildInit(&fence->child, device);
    return fence;
}

HRESULT D12CreateFence(D12Device *device, UINT64 value, D3D12_FENCE_FLAGS flags, D12Fence **out)
{
    D12Fence *fence = alloc_fence(device, value, flags);
    if (!fence)
        return E_OUTOFMEMORY;
    /* a shared fence's payload is exported to other devices as a descriptor. */
    VkExportSemaphoreCreateInfo export = {VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, nullptr,
                                          VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
    VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = value;
    if (flags & D3D12_FENCE_FLAG_SHARED)
        type.pNext = &export;
    VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    if (device->vk.vkCreateSemaphore(device->vk.device, &info, nullptr, &fence->semaphore) != VK_SUCCESS) {
        D12ChildRelease(&fence->child);
        HeapFree(GetProcessHeap(), 0, fence);
        return E_OUTOFMEMORY;
    }
    *out = fence;
    return S_OK;
}

/* SetEventOnMultipleFenceCompletion: each fence is waited on in turn, and the event set once all or any are done. */
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
        fences_block(device, count, fences, values, any);
        return S_OK;
    }
    /* guessed: the fences' own waits stand in, and the last one sets the event for all of them. */
    if (any || count == 1)
        return add_wait(D12FenceFrom(fences[0]), values[0], event);
    for (UINT i = 0; i < count; i++)
        fence_block(D12FenceFrom(fences[i]), values[i]);
    SetEvent(event);
    return S_OK;
}

/* ------------------------------------------------------------------ queues */

static D12Queue *queue_impl(ID3D12CommandQueue *iface) { return CONTAINING_RECORD(iface, D12Queue, iface); }

D12Queue *D12QueueFrom(ID3D12CommandQueue *iface) { return iface ? queue_impl(iface) : nullptr; }

/*
 * submits the device's own commands, if any, in a batch of their own, so they wait for none of the program's fences:
 * every queue's next submission waits for them. then the buffers with the given waits and signals, and the queue's
 * timeline. returns the timeline serial the submission signals.
 */
static UINT64 submit(D12Queue *queue, const VkCommandBuffer *buffers, UINT count, const VkSemaphoreSubmitInfo *waits,
                     UINT wait_count, const VkSemaphoreSubmitInfo *signals, UINT signal_count)
{
    D12Device *device = queue->child.device;
    UINT64 init_serial;
    VkCommandBuffer init = D12TakeInitCommands(device, &init_serial);
    VkCommandBufferSubmitInfo cmds[64], init_cmd = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, init};
    VkSemaphoreSubmitInfo wait_infos[9], signal_infos[9];
    VkSemaphoreSubmitInfo init_signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, device->init_timeline,
                                         init_serial, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    UINT n = 0, w = 0, s = 0;
    for (UINT i = 0; i < count && n < RTL_NUMBER_OF(cmds); i++)
        cmds[n++] = (VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, buffers[i]};
    /* another queue's submission ran the device's commands, so wait for them. */
    if (!init && init_serial) {
        wait_infos[w] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait_infos[w].semaphore = device->init_timeline;
        wait_infos[w].value = init_serial;
        wait_infos[w++].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
    for (UINT i = 0; i < wait_count && w < RTL_NUMBER_OF(wait_infos); i++)
        wait_infos[w++] = waits[i];
    for (UINT i = 0; i < signal_count && s < RTL_NUMBER_OF(signal_infos) - 1; i++)
        signal_infos[s++] = signals[i];
    AcquireSRWLockExclusive(queue->queue_lock);
    if (queue->bind_serial && w < RTL_NUMBER_OF(wait_infos)) {
        wait_infos[w] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait_infos[w].semaphore = queue->timeline;
        wait_infos[w].value = queue->bind_serial;
        wait_infos[w++].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        queue->bind_serial = 0;
    }
    UINT64 serial = queue->next_serial++;
    signal_infos[s] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal_infos[s].semaphore = queue->timeline;
    signal_infos[s].value = serial;
    signal_infos[s++].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    /* the init batch's closing barrier orders its commands before the next batch's, which comes later on the queue. */
    VkSubmitInfo2 batches[2] = {{VK_STRUCTURE_TYPE_SUBMIT_INFO_2}, {VK_STRUCTURE_TYPE_SUBMIT_INFO_2}};
    UINT b = 0;
    if (init) {
        batches[b].commandBufferInfoCount = 1;
        batches[b].pCommandBufferInfos = &init_cmd;
        batches[b].signalSemaphoreInfoCount = 1;
        batches[b++].pSignalSemaphoreInfos = &init_signal;
    }
    batches[b].waitSemaphoreInfoCount = w;
    batches[b].pWaitSemaphoreInfos = wait_infos;
    batches[b].commandBufferInfoCount = n;
    batches[b].pCommandBufferInfos = cmds;
    batches[b].signalSemaphoreInfoCount = s;
    batches[b++].pSignalSemaphoreInfos = signal_infos;
    if (device->vk.vkQueueSubmit2(queue->queue, b, batches, VK_NULL_HANDLE) != VK_SUCCESS)
        D12Log("d3d12: vkQueueSubmit2 failed");
    ReleaseSRWLockExclusive(queue->queue_lock);
    return serial;
}

/* ------------------------------------------------------------------ held back work */

/*
 * Direct3D 12 lets a queue wait for a value nothing signals yet while the other queues run on. a vulkan wait like
 * that holds up what follows it on its vulkan queue, which other queues of the program may share, and the device's
 * own commands when the next submission is that queue's, which every queue then waits for: when the value comes
 * from one of those, nothing moves again. so such a wait and what the queue gets after it are held back here, and
 * go to vulkan in order once a submission or the cpu signals the value. a vulkan wait then only ever waits for work
 * submitted before it, which cannot wait for it in turn.
 */
enum { OP_EXECUTE, OP_SIGNAL, OP_WAIT };

struct D12QueueOp {
    struct D12QueueOp *next;
    UINT kind;
    D12Fence *fence;
    UINT64 value;
    UINT count;
    VkCommandBuffer buffers[];
};

/*
 * a wait for the value ends without more of the program's work: a submission or the cpu signals it already, or the
 * fence is shared and others signal it.
 */
static BOOL reachable(D12Fence *fence, UINT64 value)
{
    return value <= fence->queued || (fence->flags & D3D12_FENCE_FLAG_SHARED);
}

static void run_execute(D12Queue *queue, const VkCommandBuffer *buffers, UINT count)
{
    for (UINT at = 0; at < count; at += 64)
        submit(queue, buffers + at, D12_MIN(count - at, 64u), nullptr, 0, nullptr, 0);
}

/* Vulkan timelines only go up: a value at or below one already signalled is not signalled again. */
static void run_signal(D12Queue *queue, D12Fence *fence, UINT64 value)
{
    if (value <= fence->queued)
        return;
    fence->queued = value;
    VkSemaphoreSubmitInfo signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, fence->semaphore, value,
                                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    submit(queue, nullptr, 0, nullptr, 0, &signal, 1);
}

static void run_op(D12Queue *queue, UINT kind, D12Fence *fence, UINT64 value, const VkCommandBuffer *buffers,
                   UINT count)
{
    if (kind == OP_EXECUTE) {
        run_execute(queue, buffers, count);
    } else if (kind == OP_SIGNAL) {
        run_signal(queue, fence, value);
    } else {
        VkSemaphoreSubmitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, fence->semaphore, value,
                                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
        submit(queue, nullptr, 0, &wait, 1, nullptr, 0);
    }
}

/* runs the queue's first held back operation and puts it on the list of those to free outside the lock. */
static D12QueueOp *run_first(D12Queue *queue, D12QueueOp *done)
{
    D12QueueOp *op = queue->held;
    if (!(queue->held = op->next))
        queue->held_tail = &queue->held;
    run_op(queue, op->kind, op->fence, op->value, op->buffers, op->count);
    op->next = done;
    return op;
}

/* runs what the queues held back that no wait holds up any more, until nothing more is let go. */
static D12QueueOp *release_held(D12Device *device, D12QueueOp *done)
{
    BOOL any = FALSE;
    for (BOOL progress = TRUE; progress;) {
        progress = FALSE;
        for (D12Queue *queue = device->queue_list; queue; queue = queue->next) {
            D12QueueOp *op;
            while ((op = queue->held) && (op->kind != OP_WAIT || reachable(op->fence, op->value))) {
                done = run_first(queue, done);
                progress = any = TRUE;
            }
        }
    }
    if (any)
        WakeAllConditionVariable(&device->released);
    return done;
}

/* runs all the queue holds back, waits for values nothing signals yet included. */
static D12QueueOp *run_held(D12Queue *queue, D12QueueOp *done)
{
    while (queue->held)
        done = run_first(queue, done);
    return release_held(queue->child.device, done);
}

static void free_ops(D12QueueOp *op)
{
    while (op) {
        D12QueueOp *next = op->next;
        if (op->fence)
            op->fence->iface.lpVtbl->Release(&op->fence->iface);
        HeapFree(GetProcessHeap(), 0, op);
        op = next;
    }
}

/* runs an operation of the program's, unless work before it is held back or it waits for what nothing signals yet. */
static void queue_op(D12Queue *queue, UINT kind, D12Fence *fence, UINT64 value, const VkCommandBuffer *buffers,
                     UINT count)
{
    D12Device *device = queue->child.device;
    D12QueueOp *done = nullptr;
    AcquireSRWLockExclusive(&device->submit_lock);
    if (queue->held || (kind == OP_WAIT && !reachable(fence, value))) {
        D12QueueOp *op = HeapAlloc(GetProcessHeap(), 0, sizeof(*op) + count * sizeof(*buffers));
        if (op) {
            *op = (D12QueueOp){nullptr, kind, fence, value, count};
            if (count)
                memcpy(op->buffers, buffers, count * sizeof(*buffers));
            if (fence)
                fence->iface.lpVtbl->AddRef(&fence->iface);
            *queue->held_tail = op;
            queue->held_tail = &op->next;
            ReleaseSRWLockExclusive(&device->submit_lock);
            return;
        }
        /* with no memory to hold it, the order of the queue matters more than the wait. */
        done = run_held(queue, done);
    }
    run_op(queue, kind, fence, value, buffers, count);
    if (kind == OP_SIGNAL)
        done = release_held(device, done);
    ReleaseSRWLockExclusive(&device->submit_lock);
    free_ops(done);
}

/*
 * presents, tile mappings and idle waits go to vulkan at once, so what the queue holds back must go first: they wait
 * a while for another thread to signal what it waits for, then run it as it is.
 */
static void settle(D12Queue *queue)
{
    D12Device *device = queue->child.device;
    D12QueueOp *done = nullptr;
    AcquireSRWLockExclusive(&device->submit_lock);
    ULONGLONG start = GetTickCount64();
    while (queue->held) {
        ULONGLONG waited = GetTickCount64() - start;
        if (waited >= 1000 ||
            !SleepConditionVariableSRW(&device->released, &device->submit_lock, (DWORD)(1000 - waited), 0))
            break;
    }
    if (queue->held)
        done = run_held(queue, done);
    ReleaseSRWLockExclusive(&device->submit_lock);
    free_ops(done);
}

void D12QueueSettle(D12Queue *queue) { settle(queue); }

UINT64 D12QueueSubmit(D12Queue *queue, const VkCommandBuffer *buffers, UINT count, const VkSemaphoreSubmitInfo *waits,
                      UINT wait_count, const VkSemaphoreSubmitInfo *signals, UINT signal_count)
{
    D12Device *device = queue->child.device;
    settle(queue);
    AcquireSRWLockExclusive(&device->submit_lock);
    D12QueueOp *done = queue->held ? run_held(queue, nullptr) : nullptr;
    UINT64 serial = submit(queue, buffers, count, waits, wait_count, signals, signal_count);
    ReleaseSRWLockExclusive(&device->submit_lock);
    free_ops(done);
    return serial;
}

void D12QueueWaitIdle(D12Queue *queue)
{
    D12Device *device = queue->child.device;
    settle(queue);
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
        *out = nullptr;
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
        AcquireSRWLockExclusive(&device->submit_lock);
        for (D12Queue **link = &device->queue_list; *link; link = &(*link)->next)
            if (*link == queue) {
                *link = queue->next;
                break;
            }
        ReleaseSRWLockExclusive(&device->submit_lock);
        device->vk.vkDestroySemaphore(device->vk.device, queue->timeline, nullptr);
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
    D12UpdateTileMappings(queue_impl(iface), D12ResourceFrom(resource), region_count, region_starts, region_sizes,
                          D12HeapFrom(heap), range_count, range_flags, heap_starts, range_tile_counts);
}

static void STDMETHODCALLTYPE queue_copy_tile_mappings(ID3D12CommandQueue *iface, ID3D12Resource *dst,
                                                       const D3D12_TILED_RESOURCE_COORDINATE *dst_start,
                                                       ID3D12Resource *src,
                                                       const D3D12_TILED_RESOURCE_COORDINATE *src_start,
                                                       const D3D12_TILE_REGION_SIZE *size,
                                                       D3D12_TILE_MAPPING_FLAGS flags)
{
    D12CopyTileMappings(queue_impl(iface), D12ResourceFrom(dst), dst_start, D12ResourceFrom(src), src_start, size);
}

VkCommandBuffer D12CommandListBuffer(ID3D12CommandList *list);

static void STDMETHODCALLTYPE queue_execute_command_lists(ID3D12CommandQueue *iface, UINT count,
                                                          ID3D12CommandList *const *lists)
{
    D12Queue *queue = queue_impl(iface);
    VkCommandBuffer local[64];
    VkCommandBuffer *buffers =
        count < RTL_NUMBER_OF(local) ? local : HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(*buffers));
    if (!buffers)
        return;
    UINT n = 0;
    buffers[n++] = queue->child.device->boundary_cmd;
    for (UINT i = 0; i < count; i++) {
        VkCommandBuffer cmd = lists[i] ? D12CommandListBuffer(lists[i]) : VK_NULL_HANDLE;
        if (cmd)
            buffers[n++] = cmd;
    }
    queue_op(queue, OP_EXECUTE, nullptr, 0, buffers, n);
    if (buffers != local)
        HeapFree(GetProcessHeap(), 0, buffers);
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
    queue_op(queue, OP_SIGNAL, fence, value, nullptr, 0);
    D12FenceWake(queue->child.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE queue_wait(ID3D12CommandQueue *iface, ID3D12Fence *fence_iface, UINT64 value)
{
    D12Queue *queue = queue_impl(iface);
    D12Fence *fence = D12FenceFrom(fence_iface);
    if (!fence)
        return E_INVALIDARG;
    queue_op(queue, OP_WAIT, fence, value, nullptr, 0);
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

/* guessed: Vulkan cannot tie the gpu clock to the cpu's here, so the gpu timestamp is the cpu's in its units. */
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

/* Direct3D 12 swap chains present to windows only. */
static HRESULT STDMETHODCALLTYPE presenter_create_composition_swap_chain(IMimeDXGIPresenter *iface,
                                                                         IDXGIFactory *factory, HANDLE surface,
                                                                         const DXGI_SWAP_CHAIN_DESC1 *desc,
                                                                         IDXGIOutput *restrict_to,
                                                                         IDXGISwapChain1 **swapchain)
{
    return DXGI_ERROR_UNSUPPORTED;
}

static const struct IMimeDXGIPresenterVtbl g_presenter_vtbl = {
    presenter_query,
    presenter_add_ref,
    presenter_release,
    presenter_create_swap_chain,
    presenter_create_composition_swap_chain,
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
    /* round robin over the family's queues so the program's queues run side by side where possible. */
    /* the first queue is left to Direct3D 11, which shares the device, unless it is the only one. */
    UINT spare = device->queue_count > 1 ? device->queue_count - 1 : 1;
    UINT index = (UINT)InterlockedIncrement(&device->next_queue) % spare + (device->queue_count > 1 ? 1 : 0);
    queue->queue = device->queues[index];
    queue->queue_lock = &device->shared.vk->QueueLocks[index];
    queue->family = device->family;
    queue->next_serial = 1;
    VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    if (device->vk.vkCreateSemaphore(device->vk.device, &info, nullptr, &queue->timeline) != VK_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, queue);
        return E_OUTOFMEMORY;
    }
    queue->child.refs = 1;
    D12ChildInit(&queue->child, device);
    queue->held_tail = &queue->held;
    AcquireSRWLockExclusive(&device->submit_lock);
    queue->next = device->queue_list;
    device->queue_list = queue;
    ReleaseSRWLockExclusive(&device->submit_lock);
    *out = queue;
    return S_OK;
}

/* ------------------------------------------------------------------ shared fences */

/*
 * the first handle makes the kernel object and keeps the semaphore payload for other devices. the fence's
 * description is its flags and the adapter.
 */
HRESULT D12FenceShare(D12Fence *fence, const SECURITY_ATTRIBUTES *attributes, LPCWSTR name, HANDLE *out)
{
    D12Device *device = fence->child.device;
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&fence->lock);
    if (!fence->share.handle) {
        MimeShareBlob blob = {};
        blob.magic = MIME_SHARE_MAGIC;
        blob.kind = MIME_SHARE_FENCE;
        blob.fence_flags = fence->flags;
        blob.adapter = device->adapter_luid;
        hr = MimeShareCreate(&fence->share, MIME_GFXSHARE_SYNC, &blob, sizeof(blob), 0, name, attributes);
        if (SUCCEEDED(hr) &&
            device->vk.vkMxShareExportSemaphore(device->vk.device, fence->semaphore, fence->share.id) != VK_SUCCESS) {
            MimeShareClose(&fence->share);
            hr = E_FAIL;
        } else if (SUCCEEDED(hr)) {
            /* other devices of this process open the fence without importing it. */
            device->shared.adapter->lpVtbl->PublishSemaphore(device->shared.adapter, device->shared.vk, fence->share.id,
                                                             (void *)fence->semaphore, &fence->entry);
        }
    } else if (name) {
        /* a second name does not make another object. */
        hr = DXGI_ERROR_NAME_ALREADY_EXISTS;
    }
    ReleaseSRWLockExclusive(&fence->lock);
    if (SUCCEEDED(hr))
        hr = MimeShareHandle(&fence->share, 0, attributes, out);
    return hr;
}

HRESULT D12FenceOpen(D12Device *device, HANDLE handle, D12Fence **out)
{
    MimeShareBlob blob = {};
    MimeShare own;
    UINT size = 0;
    HRESULT hr = MimeShareOpen(handle, MIME_GFXSHARE_SYNC, &own, &blob, sizeof(blob), &size);
    if (FAILED(hr))
        return hr;
    if (size != sizeof(blob) || blob.magic != MIME_SHARE_MAGIC || blob.kind != MIME_SHARE_FENCE) {
        MimeShareClose(&own);
        return E_INVALIDARG;
    }
    D12Fence *fence = alloc_fence(device, 0, (D3D12_FENCE_FLAGS)(blob.fence_flags | D3D12_FENCE_FLAG_SHARED));
    if (!fence) {
        MimeShareClose(&own);
        return E_OUTOFMEMORY;
    }
    void *semaphore = nullptr;
    if (device->shared.adapter->lpVtbl->OpenSemaphore(device->shared.adapter, device->shared.vk, own.id, &semaphore,
                                                      &fence->entry) == S_OK) {
        /* made by a fence of this process: the devices share its semaphore. */
        fence->semaphore = (VkSemaphore)semaphore;
    } else {
        /* another process's fence: a local semaphore imports the payload, which never needs exporting again. */
        VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
        if (device->vk.vkCreateSemaphore(device->vk.device, &info, nullptr, &fence->semaphore) != VK_SUCCESS ||
            device->vk.vkMxShareImportSemaphore(device->vk.device, fence->semaphore, own.id) != VK_SUCCESS) {
            if (fence->semaphore)
                device->vk.vkDestroySemaphore(device->vk.device, fence->semaphore, nullptr);
            fence->semaphore = VK_NULL_HANDLE;
            MimeShareClose(&own);
            D12ChildRelease(&fence->child);
            HeapFree(GetProcessHeap(), 0, fence);
            return E_FAIL;
        }
    }
    fence->share = own;
    /* the payload already has a value, which the fence reports and its waits start from. */
    UINT64 value = 0;
    device->vk.vkGetSemaphoreCounterValue(device->vk.device, fence->semaphore, &value);
    fence->completed = fence->queued = value;
    *out = fence;
    return S_OK;
}
