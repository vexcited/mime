/* mime's Vulkan driver on Metal 4, as the host's Vulkan code loads it. */
#ifndef MIME_VKMETAL_H
#define MIME_VKMETAL_H

#include <vulkan/vulkan.h>

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkm_GetInstanceProcAddr(VkInstance instance, const char *name);

#endif
