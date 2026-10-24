/*
 * memory and semaphores shared between processes. each process has its own vulkan device on the host, so these
 * private commands, found by direct3d through vkGetDeviceProcAddr, export a device's memory or timeline semaphore
 * under a shared object's id, see mime/gfxshare.h, and import it into another process's device.
 */
#ifndef MIME_MVK_SHARE_H
#define MIME_MVK_SHARE_H

#include <vulkan/vulkan_core.h>

/* keeps memory allocated with VkExportMemoryAllocateInfo for opaque descriptors under the id. */
typedef VkResult(VKAPI_PTR *PFN_vkMxShareExportMemory)(VkDevice device, VkDeviceMemory memory, uint64_t id);
/* new device memory from the one kept under the id, with the exporter's size and type. */
typedef VkResult(VKAPI_PTR *PFN_vkMxShareImportMemory)(VkDevice device, uint64_t id, VkImage dedicated,
                                                         VkDeviceSize size, uint32_t type_index,
                                                         VkDeviceMemory *memory);
/* the timeline semaphore must be created with VkExportSemaphoreCreateInfo for opaque descriptors. */
typedef VkResult(VKAPI_PTR *PFN_vkMxShareExportSemaphore)(VkDevice device, VkSemaphore semaphore, uint64_t id);
/* makes the semaphore, a timeline of the device, share the payload kept under the id. */
typedef VkResult(VKAPI_PTR *PFN_vkMxShareImportSemaphore)(VkDevice device, VkSemaphore semaphore, uint64_t id);

#endif
