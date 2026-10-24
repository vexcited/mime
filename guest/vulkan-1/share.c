/*
 * memory and semaphores shared with other processes' devices, see mime/mvk_share.h. the host keeps the
 * descriptors and these commands name them by shared object id.
 */
#include "mvk_guest.h"

#include <mime/mvk_share.h>

VkResult mvk_memory_imported(VkDeviceMemory memory, VkDeviceSize size);

VKAPI_ATTR VkResult VKAPI_CALL vkMxShareExportMemory(VkDevice device, VkDeviceMemory memory, uint64_t id)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_SHARE_EXPORT_MEMORY);
    mvk_put_u64(e, (uint64_t)memory);
    mvk_put_u64(e, id);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMxShareImportMemory(VkDevice device, uint64_t id, VkImage dedicated,
                                                       VkDeviceSize size, uint32_t type_index, VkDeviceMemory *memory)
{
    uint64_t memory_id = mvk_new_id();
    MvkEncoder *e = mvk_call_begin(MVK_OP_SHARE_IMPORT_MEMORY);
    mvk_put_u64(e, memory_id);
    mvk_put_u64(e, id);
    mvk_put_u64(e, (uint64_t)dedicated);
    mvk_put_u64(e, size);
    mvk_put_u32(e, type_index);
    mvk_put_u32(e, 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    mvk_call_done(d);
    if (result == VK_SUCCESS)
        result = mvk_memory_imported((VkDeviceMemory)memory_id, size);
    if (result != VK_SUCCESS) {
        mvk_free_id(memory_id);
        return result;
    }
    *memory = (VkDeviceMemory)memory_id;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMxShareExportSemaphore(VkDevice device, VkSemaphore semaphore, uint64_t id)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_SHARE_EXPORT_SEMAPHORE);
    mvk_put_u64(e, (uint64_t)semaphore);
    mvk_put_u64(e, id);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMxShareImportSemaphore(VkDevice device, VkSemaphore semaphore, uint64_t id)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_SHARE_IMPORT_SEMAPHORE);
    mvk_put_u64(e, (uint64_t)semaphore);
    mvk_put_u64(e, id);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    mvk_call_done(d);
    return result;
}

const MvkProc mvk_share_procs[] = {
    {"vkMxShareExportMemory", (PFN_vkVoidFunction)vkMxShareExportMemory},
    {"vkMxShareImportMemory", (PFN_vkVoidFunction)vkMxShareImportMemory},
    {"vkMxShareExportSemaphore", (PFN_vkVoidFunction)vkMxShareExportSemaphore},
    {"vkMxShareImportSemaphore", (PFN_vkVoidFunction)vkMxShareImportSemaphore},
    {nullptr, nullptr},
};
