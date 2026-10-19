/*
 * the vulkan device the direct3d devices of a process share. The host serves a process one vulkan device, so the
 * direct3d 11 and 12 devices made on an adapter use the same, its queues and the locks around them. The first to
 * ask describes it, the last to release it takes it away.
 */
#include "dxgi_private.h"

typedef struct DxgiShared {
    MIME_DXGI_SHARED_VK pub;
    LONG refs;
    UINT gpu_index;
    PFN_vkDestroyDevice destroy;
    PFN_vkDeviceWaitIdle wait_idle;
} DxgiShared;

static DxgiShared *g_shared[8];
static SRWLOCK g_shared_lock = SRWLOCK_INIT;

HRESULT DxgiAcquireShared(const DxgiGpu *gpu, const void *create_info, const void *profile, UINT profile_size,
                          MIME_DXGI_SHARED_VK **out)
{
    const DxgiVulkan *vk = DxgiGetVulkan();
    UINT index = (UINT)(gpu - vk->gpus);
    if (index >= vk->count)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&g_shared_lock);
    DxgiShared *shared = g_shared[index];
    if (shared) {
        shared->refs++;
        *out = &shared->pub;
        ReleaseSRWLockExclusive(&g_shared_lock);
        return S_OK;
    }
    HRESULT hr = S_FALSE;
    if (create_info && profile_size <= MIME_VK_PROFILE_BYTES) {
        const VkDeviceCreateInfo *info = create_info;
        PFN_vkCreateDevice create = (PFN_vkCreateDevice)vk->get_instance_proc(vk->instance, "vkCreateDevice");
        PFN_vkGetDeviceProcAddr device_proc =
            (PFN_vkGetDeviceProcAddr)vk->get_instance_proc(vk->instance, "vkGetDeviceProcAddr");
        shared = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*shared));
        VkDevice device = VK_NULL_HANDLE;
        if (!shared) {
            hr = E_OUTOFMEMORY;
        } else if (!create || !device_proc || info->queueCreateInfoCount != 1 ||
                   info->pQueueCreateInfos[0].queueCount > MIME_VK_MAX_QUEUES ||
                   create(gpu->physical, info, nullptr, &device) != VK_SUCCESS) {
            hr = DXGI_ERROR_UNSUPPORTED;
            HeapFree(GetProcessHeap(), 0, shared);
            shared = nullptr;
        } else {
            PFN_vkGetDeviceQueue get_queue = (PFN_vkGetDeviceQueue)device_proc(device, "vkGetDeviceQueue");
            shared->destroy = (PFN_vkDestroyDevice)device_proc(device, "vkDestroyDevice");
            shared->wait_idle = (PFN_vkDeviceWaitIdle)device_proc(device, "vkDeviceWaitIdle");
            shared->gpu_index = index;
            shared->refs = 1;
            shared->pub.Device = device;
            shared->pub.GetDeviceProcAddr = (void *)device_proc;
            shared->pub.QueueFamily = info->pQueueCreateInfos[0].queueFamilyIndex;
            shared->pub.QueueCount = info->pQueueCreateInfos[0].queueCount;
            for (UINT i = 0; i < shared->pub.QueueCount; i++) {
                VkQueue queue = VK_NULL_HANDLE;
                get_queue(device, shared->pub.QueueFamily, i, &queue);
                shared->pub.Queues[i] = queue;
                InitializeSRWLock(&shared->pub.QueueLocks[i]);
            }
            shared->pub.ProfileSize = profile_size;
            memcpy(shared->pub.Profile, profile, profile_size);
            g_shared[index] = shared;
            *out = &shared->pub;
            hr = S_OK;
        }
    }
    ReleaseSRWLockExclusive(&g_shared_lock);
    return hr;
}

void DxgiReleaseShared(MIME_DXGI_SHARED_VK *pub)
{
    DxgiShared *shared = CONTAINING_RECORD(pub, DxgiShared, pub);
    AcquireSRWLockExclusive(&g_shared_lock);
    BOOL last = --shared->refs == 0;
    if (last)
        g_shared[shared->gpu_index] = nullptr;
    ReleaseSRWLockExclusive(&g_shared_lock);
    if (last) {
        shared->wait_idle(shared->pub.Device);
        shared->destroy(shared->pub.Device, nullptr);
        HeapFree(GetProcessHeap(), 0, shared);
    }
}
