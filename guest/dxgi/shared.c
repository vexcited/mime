/*
 * the vulkan device the direct3d devices of a process share. The host serves a process one vulkan device, so the
 * direct3d 11 and 12 devices made on an adapter use the same, its queues and the locks around them. The first to
 * ask describes it, the last to release it takes it away.
 */
#include "dxgi_private.h"

typedef struct DxgiShared DxgiShared;
typedef struct DxgiSemaphore DxgiSemaphore;

struct DxgiShared {
    MIME_DXGI_SHARED_VK pub;
    LONG refs;
    UINT gpu_index;
    PFN_vkDestroyDevice destroy;
    PFN_vkDeviceWaitIdle wait_idle;
    PFN_vkDestroySemaphore destroy_semaphore;
    DxgiSemaphore *semaphores;
};

/* a semaphore the process's fences share, held by as many of them as opened it. */
struct DxgiSemaphore {
    DxgiSemaphore *next;
    UINT64 id;
    VkSemaphore semaphore;
    LONG refs;
};

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
            shared->destroy_semaphore = (PFN_vkDestroySemaphore)device_proc(device, "vkDestroySemaphore");
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

HRESULT DxgiPublishSemaphore(MIME_DXGI_SHARED_VK *pub, UINT64 id, void *semaphore, void **entry)
{
    DxgiShared *shared = CONTAINING_RECORD(pub, DxgiShared, pub);
    DxgiSemaphore *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->id = id;
    s->semaphore = (VkSemaphore)semaphore;
    s->refs = 1;
    AcquireSRWLockExclusive(&g_shared_lock);
    s->next = shared->semaphores;
    shared->semaphores = s;
    ReleaseSRWLockExclusive(&g_shared_lock);
    *entry = s;
    return S_OK;
}

HRESULT DxgiOpenSemaphore(MIME_DXGI_SHARED_VK *pub, UINT64 id, void **semaphore, void **entry)
{
    DxgiShared *shared = CONTAINING_RECORD(pub, DxgiShared, pub);
    HRESULT hr = S_FALSE;
    AcquireSRWLockExclusive(&g_shared_lock);
    for (DxgiSemaphore *s = shared->semaphores; s; s = s->next) {
        if (s->id == id) {
            s->refs++;
            *semaphore = (void *)s->semaphore;
            *entry = s;
            hr = S_OK;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_shared_lock);
    return hr;
}

void DxgiReleaseSemaphore(MIME_DXGI_SHARED_VK *pub, void *entry)
{
    DxgiShared *shared = CONTAINING_RECORD(pub, DxgiShared, pub);
    DxgiSemaphore *s = entry;
    BOOL last = FALSE;
    AcquireSRWLockExclusive(&g_shared_lock);
    if (--s->refs == 0) {
        last = TRUE;
        for (DxgiSemaphore **p = &shared->semaphores; *p; p = &(*p)->next) {
            if (*p == s) {
                *p = s->next;
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_shared_lock);
    if (last) {
        shared->destroy_semaphore(shared->pub.Device, s->semaphore, nullptr);
        HeapFree(GetProcessHeap(), 0, s);
    }
}
