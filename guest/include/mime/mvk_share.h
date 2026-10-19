/*
 * memory and semaphores shared between processes. Every process has a vulkan device of its own on the host, so
 * these private commands, which direct3d finds through vkGetDeviceProcAddr, export a device's memory or timeline
 * semaphore under the id of a shared object, see mime/gfxshare.h, and import it into the device of another process.
 */
#ifndef MIME_MVK_SHARE_H
#define MIME_MVK_SHARE_H

#include <vulkan/vulkan_core.h>

/* the memory, which was allocated with VkExportMemoryAllocateInfo for opaque descriptors, kept under the id. */
typedef VkResult(VKAPI_PTR *PFN_vkMimeShareExportMemory)(VkDevice device, VkDeviceMemory memory, uint64_t id);
/* a new memory of the device from the one kept under the id, of the size and type of the exporting memory. */
typedef VkResult(VKAPI_PTR *PFN_vkMimeShareImportMemory)(VkDevice device, uint64_t id, VkImage dedicated,
                                                         VkDeviceSize size, uint32_t type_index,
                                                         VkDeviceMemory *memory);
/* the timeline semaphore, which was made with VkExportSemaphoreCreateInfo for opaque descriptors. */
typedef VkResult(VKAPI_PTR *PFN_vkMimeShareExportSemaphore)(VkDevice device, VkSemaphore semaphore, uint64_t id);
/* the semaphore, a timeline of the device, shares the payload of the one kept under the id. */
typedef VkResult(VKAPI_PTR *PFN_vkMimeShareImportSemaphore)(VkDevice device, VkSemaphore semaphore, uint64_t id);

#endif
