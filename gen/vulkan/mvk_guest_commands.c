/* generated from vk.xml, do not edit. */
#include "mvk_guest.h"

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures * pFeatures)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceFeatures);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pFeatures)
            mvk_out_VkPhysicalDeviceFeatures(d, pFeatures);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties * pFormatProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceFormatProperties);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, (uint32_t)(format));
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pFormatProperties)
            mvk_out_VkFormatProperties(d, pFormatProperties);
    }
    mvk_call_done(d);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties * pImageFormatProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceImageFormatProperties);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, (uint32_t)(format));
    mvk_put_u32(e, (uint32_t)(type));
    mvk_put_u32(e, (uint32_t)(tiling));
    mvk_put_u32(e, (uint32_t)(usage));
    mvk_put_u32(e, (uint32_t)(flags));
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pImageFormatProperties)
            mvk_out_VkImageFormatProperties(d, pImageFormatProperties);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL mvk_raw_vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties * pProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceProperties);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pProperties)
            mvk_out_VkPhysicalDeviceProperties(d, pProperties);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t * pQueueFamilyPropertyCount, VkQueueFamilyProperties * pQueueFamilyProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceQueueFamilyProperties);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pQueueFamilyPropertyCount ? *pQueueFamilyPropertyCount : 0);
    mvk_put_u32(e, pQueueFamilyProperties ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pQueueFamilyPropertyCount)
            *pQueueFamilyPropertyCount = (uint32_t)mvk_get_u32(d);
        if (pQueueFamilyProperties && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkQueueFamilyProperties(d, &pQueueFamilyProperties[i]);
        }
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL mvk_raw_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties * pMemoryProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceMemoryProperties);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMemoryProperties)
            mvk_out_VkPhysicalDeviceMemoryProperties(d, pMemoryProperties);
    }
    mvk_call_done(d);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkBindBufferMemory);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_handle(e, 0, (uint64_t)(memory));
    mvk_put_u64(e, (uint64_t)(memoryOffset));
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkBindImageMemory);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(image));
    mvk_put_handle(e, 0, (uint64_t)(memory));
    mvk_put_u64(e, (uint64_t)(memoryOffset));
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements * pMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetBufferMemoryRequirements);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMemoryRequirements)
            mvk_out_VkMemoryRequirements(d, pMemoryRequirements);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements * pMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetImageMemoryRequirements);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(image));
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMemoryRequirements)
            mvk_out_VkMemoryRequirements(d, pMemoryRequirements);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t * pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements * pSparseMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetImageSparseMemoryRequirements);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(image));
    mvk_put_u32(e, pSparseMemoryRequirementCount ? *pSparseMemoryRequirementCount : 0);
    mvk_put_u32(e, pSparseMemoryRequirements ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pSparseMemoryRequirementCount)
            *pSparseMemoryRequirementCount = (uint32_t)mvk_get_u32(d);
        if (pSparseMemoryRequirements && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkSparseImageMemoryRequirements(d, &pSparseMemoryRequirements[i]);
        }
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t * pPropertyCount, VkSparseImageFormatProperties * pProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceSparseImageFormatProperties);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, (uint32_t)(format));
    mvk_put_u32(e, (uint32_t)(type));
    mvk_put_u32(e, (uint32_t)(samples));
    mvk_put_u32(e, (uint32_t)(usage));
    mvk_put_u32(e, (uint32_t)(tiling));
    mvk_put_u32(e, pPropertyCount ? *pPropertyCount : 0);
    mvk_put_u32(e, pProperties ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pPropertyCount)
            *pPropertyCount = (uint32_t)mvk_get_u32(d);
        if (pProperties && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkSparseImageFormatProperties(d, &pProperties[i]);
        }
    }
    mvk_call_done(d);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkQueryPool * pQueryPool)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateQueryPool);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkQueryPoolCreateInfo(e, &pCreateInfo[i]);
    }
    *pQueryPool = (VkQueryPool)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pQueryPool);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyQueryPool);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_call_end(e);
    mvk_free_id((uint64_t)queryPool);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkBuffer * pBuffer)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateBuffer);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBufferCreateInfo(e, &pCreateInfo[i]);
    }
    *pBuffer = (VkBuffer)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pBuffer);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyBuffer);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_call_end(e);
    mvk_free_id((uint64_t)buffer);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkImage * pImage)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateImage);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkImageCreateInfo(e, &pCreateInfo[i]);
    }
    *pImage = (VkImage)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pImage);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyImage);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(image));
    mvk_call_end(e);
    mvk_free_id((uint64_t)image);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource * pSubresource, VkSubresourceLayout * pLayout)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetImageSubresourceLayout);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(image));
    mvk_put_u32(e, pSubresource ? 1 : 0);
    if (pSubresource) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkImageSubresource(e, &pSubresource[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pLayout)
            mvk_out_VkSubresourceLayout(d, pLayout);
    }
    mvk_call_done(d);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device, const VkImageViewCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkImageView * pView)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateImageView);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkImageViewCreateInfo(e, &pCreateInfo[i]);
    }
    *pView = (VkImageView)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pView);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyImageView);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(imageView));
    mvk_call_end(e);
    mvk_free_id((uint64_t)imageView);
}

VKAPI_ATTR VkResult VKAPI_CALL mvk_raw_vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkCommandPool * pCommandPool)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateCommandPool);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCommandPoolCreateInfo(e, &pCreateInfo[i]);
    }
    *pCommandPool = (VkCommandPool)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pCommandPool);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy * pRegions)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyBuffer);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(srcBuffer));
    mvk_put_handle(e, 0, (uint64_t)(dstBuffer));
    mvk_put_u32(e, (uint32_t)(regionCount));
    mvk_put_u32(e, pRegions ? 1 : 0);
    if (pRegions) {
        for (uint32_t i = 0; i < (uint32_t)(regionCount); i++)
            mvk_put_VkBufferCopy(e, &pRegions[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy * pRegions)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyImage);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(srcImage));
    mvk_put_u32(e, (uint32_t)(srcImageLayout));
    mvk_put_handle(e, 0, (uint64_t)(dstImage));
    mvk_put_u32(e, (uint32_t)(dstImageLayout));
    mvk_put_u32(e, (uint32_t)(regionCount));
    mvk_put_u32(e, pRegions ? 1 : 0);
    if (pRegions) {
        for (uint32_t i = 0; i < (uint32_t)(regionCount); i++)
            mvk_put_VkImageCopy(e, &pRegions[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy * pRegions)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyBufferToImage);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(srcBuffer));
    mvk_put_handle(e, 0, (uint64_t)(dstImage));
    mvk_put_u32(e, (uint32_t)(dstImageLayout));
    mvk_put_u32(e, (uint32_t)(regionCount));
    mvk_put_u32(e, pRegions ? 1 : 0);
    if (pRegions) {
        for (uint32_t i = 0; i < (uint32_t)(regionCount); i++)
            mvk_put_VkBufferImageCopy(e, &pRegions[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy * pRegions)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyImageToBuffer);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(srcImage));
    mvk_put_u32(e, (uint32_t)(srcImageLayout));
    mvk_put_handle(e, 0, (uint64_t)(dstBuffer));
    mvk_put_u32(e, (uint32_t)(regionCount));
    mvk_put_u32(e, pRegions ? 1 : 0);
    if (pRegions) {
        for (uint32_t i = 0; i < (uint32_t)(regionCount); i++)
            mvk_put_VkBufferImageCopy(e, &pRegions[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void * pData)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdUpdateBuffer);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(dstBuffer));
    mvk_put_u64(e, (uint64_t)(dstOffset));
    mvk_put_u64(e, (uint64_t)(dataSize));
    mvk_put_blob(e, pData, dataSize);
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdFillBuffer);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(dstBuffer));
    mvk_put_u64(e, (uint64_t)(dstOffset));
    mvk_put_u64(e, (uint64_t)(size));
    mvk_put_u32(e, (uint32_t)(data));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier * pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier * pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier * pImageMemoryBarriers)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdPipelineBarrier);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(srcStageMask));
    mvk_put_u32(e, (uint32_t)(dstStageMask));
    mvk_put_u32(e, (uint32_t)(dependencyFlags));
    mvk_put_u32(e, (uint32_t)(memoryBarrierCount));
    mvk_put_u32(e, pMemoryBarriers ? 1 : 0);
    if (pMemoryBarriers) {
        for (uint32_t i = 0; i < (uint32_t)(memoryBarrierCount); i++)
            mvk_put_VkMemoryBarrier(e, &pMemoryBarriers[i]);
    }
    mvk_put_u32(e, (uint32_t)(bufferMemoryBarrierCount));
    mvk_put_u32(e, pBufferMemoryBarriers ? 1 : 0);
    if (pBufferMemoryBarriers) {
        for (uint32_t i = 0; i < (uint32_t)(bufferMemoryBarrierCount); i++)
            mvk_put_VkBufferMemoryBarrier(e, &pBufferMemoryBarriers[i]);
    }
    mvk_put_u32(e, (uint32_t)(imageMemoryBarrierCount));
    mvk_put_u32(e, pImageMemoryBarriers ? 1 : 0);
    if (pImageMemoryBarriers) {
        for (uint32_t i = 0; i < (uint32_t)(imageMemoryBarrierCount); i++)
            mvk_put_VkImageMemoryBarrier(e, &pImageMemoryBarriers[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBeginQuery);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(query));
    mvk_put_u32(e, (uint32_t)(flags));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdEndQuery);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(query));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdResetQueryPool);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(firstQuery));
    mvk_put_u32(e, (uint32_t)(queryCount));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdWriteTimestamp);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(pipelineStage));
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(query));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyQueryPoolResults);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(firstQuery));
    mvk_put_u32(e, (uint32_t)(queryCount));
    mvk_put_handle(e, 0, (uint64_t)(dstBuffer));
    mvk_put_u64(e, (uint64_t)(dstOffset));
    mvk_put_u64(e, (uint64_t)(stride));
    mvk_put_u32(e, (uint32_t)(flags));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer * pCommandBuffers)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdExecuteCommands);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(commandBufferCount));
    mvk_put_u32(e, pCommandBuffers ? 1 : 0);
    if (pCommandBuffers) {
        for (uint32_t i = 0; i < (uint32_t)(commandBufferCount); i++)
            mvk_put_handle(e, 1, mvk_dispatch_id(pCommandBuffers[i]));
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(VkDevice device, const VkBufferViewCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkBufferView * pView)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateBufferView);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBufferViewCreateInfo(e, &pCreateInfo[i]);
    }
    *pView = (VkBufferView)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pView);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyBufferView);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(bufferView));
    mvk_call_end(e);
    mvk_free_id((uint64_t)bufferView);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkShaderModule * pShaderModule)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateShaderModule);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkShaderModuleCreateInfo(e, &pCreateInfo[i]);
    }
    *pShaderModule = (VkShaderModule)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pShaderModule);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyShaderModule);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(shaderModule));
    mvk_call_end(e);
    mvk_free_id((uint64_t)shaderModule);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkPipelineCache * pPipelineCache)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreatePipelineCache);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPipelineCacheCreateInfo(e, &pCreateInfo[i]);
    }
    *pPipelineCache = (VkPipelineCache)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pPipelineCache);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyPipelineCache);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(pipelineCache));
    mvk_call_end(e);
    mvk_free_id((uint64_t)pipelineCache);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t * pDataSize, void * pData)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPipelineCacheData);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(pipelineCache));
    mvk_put_u32(e, pData ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pDataSize)
            *pDataSize = (size_t)mvk_get_u64(d);
        if (pData && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            mvk_get_bytes(d, pData, n);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache * pSrcCaches)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkMergePipelineCaches);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(dstCache));
    mvk_put_u32(e, (uint32_t)(srcCacheCount));
    mvk_put_u32(e, pSrcCaches ? 1 : 0);
    if (pSrcCaches) {
        for (uint32_t i = 0; i < (uint32_t)(srcCacheCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pSrcCaches[i]));
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo * pCreateInfos, const VkAllocationCallbacks * pAllocator, VkPipeline * pPipelines)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateComputePipelines);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(pipelineCache));
    mvk_put_u32(e, (uint32_t)(createInfoCount));
    mvk_put_u32(e, pCreateInfos ? 1 : 0);
    if (pCreateInfos) {
        for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
            mvk_put_VkComputePipelineCreateInfo(e, &pCreateInfos[i]);
    }
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++) {
        pPipelines[i] = (VkPipeline)mvk_new_id();
        mvk_put_u64(e, (uint64_t)pPipelines[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyPipeline);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(pipeline));
    mvk_call_end(e);
    mvk_free_id((uint64_t)pipeline);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkPipelineLayout * pPipelineLayout)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreatePipelineLayout);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPipelineLayoutCreateInfo(e, &pCreateInfo[i]);
    }
    *pPipelineLayout = (VkPipelineLayout)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pPipelineLayout);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyPipelineLayout);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(pipelineLayout));
    mvk_call_end(e);
    mvk_free_id((uint64_t)pipelineLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice device, const VkSamplerCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkSampler * pSampler)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateSampler);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkSamplerCreateInfo(e, &pCreateInfo[i]);
    }
    *pSampler = (VkSampler)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pSampler);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroySampler);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(sampler));
    mvk_call_end(e);
    mvk_free_id((uint64_t)sampler);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkDescriptorSetLayout * pSetLayout)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateDescriptorSetLayout);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDescriptorSetLayoutCreateInfo(e, &pCreateInfo[i]);
    }
    *pSetLayout = (VkDescriptorSetLayout)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pSetLayout);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyDescriptorSetLayout);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(descriptorSetLayout));
    mvk_call_end(e);
    mvk_free_id((uint64_t)descriptorSetLayout);
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet * pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet * pDescriptorCopies)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkUpdateDescriptorSets);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(descriptorWriteCount));
    mvk_put_u32(e, pDescriptorWrites ? 1 : 0);
    if (pDescriptorWrites) {
        for (uint32_t i = 0; i < (uint32_t)(descriptorWriteCount); i++)
            mvk_put_VkWriteDescriptorSet(e, &pDescriptorWrites[i]);
    }
    mvk_put_u32(e, (uint32_t)(descriptorCopyCount));
    mvk_put_u32(e, pDescriptorCopies ? 1 : 0);
    if (pDescriptorCopies) {
        for (uint32_t i = 0; i < (uint32_t)(descriptorCopyCount); i++)
            mvk_put_VkCopyDescriptorSet(e, &pDescriptorCopies[i]);
    }
    mvk_call_end(e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindPipeline);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(pipelineBindPoint));
    mvk_put_handle(e, 0, (uint64_t)(pipeline));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet * pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t * pDynamicOffsets)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindDescriptorSets);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(pipelineBindPoint));
    mvk_put_handle(e, 0, (uint64_t)(layout));
    mvk_put_u32(e, (uint32_t)(firstSet));
    mvk_put_u32(e, (uint32_t)(descriptorSetCount));
    mvk_put_u32(e, pDescriptorSets ? 1 : 0);
    if (pDescriptorSets) {
        for (uint32_t i = 0; i < (uint32_t)(descriptorSetCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pDescriptorSets[i]));
    }
    mvk_put_u32(e, (uint32_t)(dynamicOffsetCount));
    mvk_put_u32(e, pDynamicOffsets ? 1 : 0);
    if (pDynamicOffsets) {
        mvk_put_bytes(e, pDynamicOffsets, (size_t)(dynamicOffsetCount) * 4);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue * pColor, uint32_t rangeCount, const VkImageSubresourceRange * pRanges)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdClearColorImage);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(image));
    mvk_put_u32(e, (uint32_t)(imageLayout));
    mvk_put_u32(e, pColor ? 1 : 0);
    if (pColor) {
        mvk_put_bytes(e, pColor, (size_t)(1) * sizeof(*pColor));
    }
    mvk_put_u32(e, (uint32_t)(rangeCount));
    mvk_put_u32(e, pRanges ? 1 : 0);
    if (pRanges) {
        for (uint32_t i = 0; i < (uint32_t)(rangeCount); i++)
            mvk_put_VkImageSubresourceRange(e, &pRanges[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDispatch);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(groupCountX));
    mvk_put_u32(e, (uint32_t)(groupCountY));
    mvk_put_u32(e, (uint32_t)(groupCountZ));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDispatchIndirect);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_u64(e, (uint64_t)(offset));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetEvent);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(event));
    mvk_put_u32(e, (uint32_t)(stageMask));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdResetEvent);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(event));
    mvk_put_u32(e, (uint32_t)(stageMask));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent * pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier * pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier * pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier * pImageMemoryBarriers)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdWaitEvents);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(eventCount));
    mvk_put_u32(e, pEvents ? 1 : 0);
    if (pEvents) {
        for (uint32_t i = 0; i < (uint32_t)(eventCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pEvents[i]));
    }
    mvk_put_u32(e, (uint32_t)(srcStageMask));
    mvk_put_u32(e, (uint32_t)(dstStageMask));
    mvk_put_u32(e, (uint32_t)(memoryBarrierCount));
    mvk_put_u32(e, pMemoryBarriers ? 1 : 0);
    if (pMemoryBarriers) {
        for (uint32_t i = 0; i < (uint32_t)(memoryBarrierCount); i++)
            mvk_put_VkMemoryBarrier(e, &pMemoryBarriers[i]);
    }
    mvk_put_u32(e, (uint32_t)(bufferMemoryBarrierCount));
    mvk_put_u32(e, pBufferMemoryBarriers ? 1 : 0);
    if (pBufferMemoryBarriers) {
        for (uint32_t i = 0; i < (uint32_t)(bufferMemoryBarrierCount); i++)
            mvk_put_VkBufferMemoryBarrier(e, &pBufferMemoryBarriers[i]);
    }
    mvk_put_u32(e, (uint32_t)(imageMemoryBarrierCount));
    mvk_put_u32(e, pImageMemoryBarriers ? 1 : 0);
    if (pImageMemoryBarriers) {
        for (uint32_t i = 0; i < (uint32_t)(imageMemoryBarrierCount); i++)
            mvk_put_VkImageMemoryBarrier(e, &pImageMemoryBarriers[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void * pValues)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdPushConstants);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(layout));
    mvk_put_u32(e, (uint32_t)(stageFlags));
    mvk_put_u32(e, (uint32_t)(offset));
    mvk_put_u32(e, (uint32_t)(size));
    mvk_put_blob(e, pValues, size);
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo * pCreateInfos, const VkAllocationCallbacks * pAllocator, VkPipeline * pPipelines)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateGraphicsPipelines);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(pipelineCache));
    mvk_put_u32(e, (uint32_t)(createInfoCount));
    mvk_put_u32(e, pCreateInfos ? 1 : 0);
    if (pCreateInfos) {
        for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
            mvk_put_VkGraphicsPipelineCreateInfo(e, &pCreateInfos[i]);
    }
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++) {
        pPipelines[i] = (VkPipeline)mvk_new_id();
        mvk_put_u64(e, (uint64_t)pPipelines[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkFramebuffer * pFramebuffer)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateFramebuffer);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkFramebufferCreateInfo(e, &pCreateInfo[i]);
    }
    *pFramebuffer = (VkFramebuffer)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pFramebuffer);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyFramebuffer);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(framebuffer));
    mvk_call_end(e);
    mvk_free_id((uint64_t)framebuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkRenderPass * pRenderPass)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateRenderPass);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkRenderPassCreateInfo(e, &pCreateInfo[i]);
    }
    *pRenderPass = (VkRenderPass)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pRenderPass);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyRenderPass);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(renderPass));
    mvk_call_end(e);
    mvk_free_id((uint64_t)renderPass);
}

VKAPI_ATTR void VKAPI_CALL vkGetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D * pGranularity)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetRenderAreaGranularity);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(renderPass));
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pGranularity)
            mvk_out_VkExtent2D(d, pGranularity);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport * pViewports)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetViewport);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstViewport));
    mvk_put_u32(e, (uint32_t)(viewportCount));
    mvk_put_u32(e, pViewports ? 1 : 0);
    if (pViewports) {
        for (uint32_t i = 0; i < (uint32_t)(viewportCount); i++)
            mvk_put_VkViewport(e, &pViewports[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D * pScissors)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetScissor);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstScissor));
    mvk_put_u32(e, (uint32_t)(scissorCount));
    mvk_put_u32(e, pScissors ? 1 : 0);
    if (pScissors) {
        for (uint32_t i = 0; i < (uint32_t)(scissorCount); i++)
            mvk_put_VkRect2D(e, &pScissors[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetLineWidth);
    if (!e)
        return;
    mvk_put_u32(e, mvk_f32_bits(lineWidth));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDepthBias);
    if (!e)
        return;
    mvk_put_u32(e, mvk_f32_bits(depthBiasConstantFactor));
    mvk_put_u32(e, mvk_f32_bits(depthBiasClamp));
    mvk_put_u32(e, mvk_f32_bits(depthBiasSlopeFactor));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4])
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetBlendConstants);
    if (!e)
        return;
    mvk_put_bytes(e, blendConstants, (size_t)(4) * sizeof((blendConstants)[0]));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDepthBounds);
    if (!e)
        return;
    mvk_put_u32(e, mvk_f32_bits(minDepthBounds));
    mvk_put_u32(e, mvk_f32_bits(maxDepthBounds));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetStencilCompareMask);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(faceMask));
    mvk_put_u32(e, (uint32_t)(compareMask));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetStencilWriteMask);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(faceMask));
    mvk_put_u32(e, (uint32_t)(writeMask));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetStencilReference);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(faceMask));
    mvk_put_u32(e, (uint32_t)(reference));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindIndexBuffer);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_u64(e, (uint64_t)(offset));
    mvk_put_u32(e, (uint32_t)(indexType));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer * pBuffers, const VkDeviceSize * pOffsets)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindVertexBuffers);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstBinding));
    mvk_put_u32(e, (uint32_t)(bindingCount));
    mvk_put_u32(e, pBuffers ? 1 : 0);
    if (pBuffers) {
        for (uint32_t i = 0; i < (uint32_t)(bindingCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pBuffers[i]));
    }
    mvk_put_u32(e, pOffsets ? 1 : 0);
    if (pOffsets) {
        mvk_put_bytes(e, pOffsets, (size_t)(bindingCount) * 8);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDraw);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(vertexCount));
    mvk_put_u32(e, (uint32_t)(instanceCount));
    mvk_put_u32(e, (uint32_t)(firstVertex));
    mvk_put_u32(e, (uint32_t)(firstInstance));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawIndexed);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(indexCount));
    mvk_put_u32(e, (uint32_t)(instanceCount));
    mvk_put_u32(e, (uint32_t)(firstIndex));
    mvk_put_u32(e, (uint32_t)(vertexOffset));
    mvk_put_u32(e, (uint32_t)(firstInstance));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawIndirect);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_u64(e, (uint64_t)(offset));
    mvk_put_u32(e, (uint32_t)(drawCount));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawIndexedIndirect);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_u64(e, (uint64_t)(offset));
    mvk_put_u32(e, (uint32_t)(drawCount));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit * pRegions, VkFilter filter)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBlitImage);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(srcImage));
    mvk_put_u32(e, (uint32_t)(srcImageLayout));
    mvk_put_handle(e, 0, (uint64_t)(dstImage));
    mvk_put_u32(e, (uint32_t)(dstImageLayout));
    mvk_put_u32(e, (uint32_t)(regionCount));
    mvk_put_u32(e, pRegions ? 1 : 0);
    if (pRegions) {
        for (uint32_t i = 0; i < (uint32_t)(regionCount); i++)
            mvk_put_VkImageBlit(e, &pRegions[i]);
    }
    mvk_put_u32(e, (uint32_t)(filter));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue * pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange * pRanges)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdClearDepthStencilImage);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(image));
    mvk_put_u32(e, (uint32_t)(imageLayout));
    mvk_put_u32(e, pDepthStencil ? 1 : 0);
    if (pDepthStencil) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkClearDepthStencilValue(e, &pDepthStencil[i]);
    }
    mvk_put_u32(e, (uint32_t)(rangeCount));
    mvk_put_u32(e, pRanges ? 1 : 0);
    if (pRanges) {
        for (uint32_t i = 0; i < (uint32_t)(rangeCount); i++)
            mvk_put_VkImageSubresourceRange(e, &pRanges[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment * pAttachments, uint32_t rectCount, const VkClearRect * pRects)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdClearAttachments);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(attachmentCount));
    mvk_put_u32(e, pAttachments ? 1 : 0);
    if (pAttachments) {
        for (uint32_t i = 0; i < (uint32_t)(attachmentCount); i++)
            mvk_put_VkClearAttachment(e, &pAttachments[i]);
    }
    mvk_put_u32(e, (uint32_t)(rectCount));
    mvk_put_u32(e, pRects ? 1 : 0);
    if (pRects) {
        for (uint32_t i = 0; i < (uint32_t)(rectCount); i++)
            mvk_put_VkClearRect(e, &pRects[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve * pRegions)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdResolveImage);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(srcImage));
    mvk_put_u32(e, (uint32_t)(srcImageLayout));
    mvk_put_handle(e, 0, (uint64_t)(dstImage));
    mvk_put_u32(e, (uint32_t)(dstImageLayout));
    mvk_put_u32(e, (uint32_t)(regionCount));
    mvk_put_u32(e, pRegions ? 1 : 0);
    if (pRegions) {
        for (uint32_t i = 0; i < (uint32_t)(regionCount); i++)
            mvk_put_VkImageResolve(e, &pRegions[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo * pRenderPassBegin, VkSubpassContents contents)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBeginRenderPass);
    if (!e)
        return;
    mvk_put_u32(e, pRenderPassBegin ? 1 : 0);
    if (pRenderPassBegin) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkRenderPassBeginInfo(e, &pRenderPassBegin[i]);
    }
    mvk_put_u32(e, (uint32_t)(contents));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdNextSubpass);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(contents));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer commandBuffer)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdEndRenderPass);
    if (!e)
        return;
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo * pBindInfos)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkBindBufferMemory2);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(bindInfoCount));
    mvk_put_u32(e, pBindInfos ? 1 : 0);
    if (pBindInfos) {
        for (uint32_t i = 0; i < (uint32_t)(bindInfoCount); i++)
            mvk_put_VkBindBufferMemoryInfo(e, &pBindInfos[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo * pBindInfos)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkBindImageMemory2);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(bindInfoCount));
    mvk_put_u32(e, pBindInfos ? 1 : 0);
    if (pBindInfos) {
        for (uint32_t i = 0; i < (uint32_t)(bindInfoCount); i++)
            mvk_put_VkBindImageMemoryInfo(e, &pBindInfos[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags * pPeerMemoryFeatures)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeviceGroupPeerMemoryFeatures);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(heapIndex));
    mvk_put_u32(e, (uint32_t)(localDeviceIndex));
    mvk_put_u32(e, (uint32_t)(remoteDeviceIndex));
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pPeerMemoryFeatures)
            *pPeerMemoryFeatures = (VkPeerMemoryFeatureFlags)mvk_get_u32(d);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDeviceMask);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(deviceMask));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2 * pInfo, VkMemoryRequirements2 * pMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetImageMemoryRequirements2);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkImageMemoryRequirementsInfo2(e, &pInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pMemoryRequirements);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMemoryRequirements)
            mvk_out_VkMemoryRequirements2(d, pMemoryRequirements);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2 * pInfo, VkMemoryRequirements2 * pMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetBufferMemoryRequirements2);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBufferMemoryRequirementsInfo2(e, &pInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pMemoryRequirements);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMemoryRequirements)
            mvk_out_VkMemoryRequirements2(d, pMemoryRequirements);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2 * pInfo, uint32_t * pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2 * pSparseMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetImageSparseMemoryRequirements2);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkImageSparseMemoryRequirementsInfo2(e, &pInfo[i]);
    }
    mvk_put_u32(e, pSparseMemoryRequirementCount ? *pSparseMemoryRequirementCount : 0);
    mvk_put_u32(e, pSparseMemoryRequirements ? 1 : 0);
    if (pSparseMemoryRequirements)
        for (uint32_t i = 0; i < (uint32_t)(*pSparseMemoryRequirementCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pSparseMemoryRequirements[i]);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pSparseMemoryRequirementCount)
            *pSparseMemoryRequirementCount = (uint32_t)mvk_get_u32(d);
        if (pSparseMemoryRequirements && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkSparseImageMemoryRequirements2(d, &pSparseMemoryRequirements[i]);
        }
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 * pFeatures)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceFeatures2);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pFeatures);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pFeatures)
            mvk_out_VkPhysicalDeviceFeatures2(d, pFeatures);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL mvk_raw_vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2 * pProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceProperties2);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pProperties);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pProperties)
            mvk_out_VkPhysicalDeviceProperties2(d, pProperties);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2 * pFormatProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceFormatProperties2);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, (uint32_t)(format));
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pFormatProperties);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pFormatProperties)
            mvk_out_VkFormatProperties2(d, pFormatProperties);
    }
    mvk_call_done(d);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2 * pImageFormatInfo, VkImageFormatProperties2 * pImageFormatProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceImageFormatProperties2);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pImageFormatInfo ? 1 : 0);
    if (pImageFormatInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPhysicalDeviceImageFormatInfo2(e, &pImageFormatInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pImageFormatProperties);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pImageFormatProperties)
            mvk_out_VkImageFormatProperties2(d, pImageFormatProperties);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t * pQueueFamilyPropertyCount, VkQueueFamilyProperties2 * pQueueFamilyProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceQueueFamilyProperties2);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pQueueFamilyPropertyCount ? *pQueueFamilyPropertyCount : 0);
    mvk_put_u32(e, pQueueFamilyProperties ? 1 : 0);
    if (pQueueFamilyProperties)
        for (uint32_t i = 0; i < (uint32_t)(*pQueueFamilyPropertyCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pQueueFamilyProperties[i]);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pQueueFamilyPropertyCount)
            *pQueueFamilyPropertyCount = (uint32_t)mvk_get_u32(d);
        if (pQueueFamilyProperties && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkQueueFamilyProperties2(d, &pQueueFamilyProperties[i]);
        }
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL mvk_raw_vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2 * pMemoryProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceMemoryProperties2);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pMemoryProperties);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMemoryProperties)
            mvk_out_VkPhysicalDeviceMemoryProperties2(d, pMemoryProperties);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2 * pFormatInfo, uint32_t * pPropertyCount, VkSparseImageFormatProperties2 * pProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceSparseImageFormatProperties2);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pFormatInfo ? 1 : 0);
    if (pFormatInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPhysicalDeviceSparseImageFormatInfo2(e, &pFormatInfo[i]);
    }
    mvk_put_u32(e, pPropertyCount ? *pPropertyCount : 0);
    mvk_put_u32(e, pProperties ? 1 : 0);
    if (pProperties)
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pProperties[i]);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pPropertyCount)
            *pPropertyCount = (uint32_t)mvk_get_u32(d);
        if (pProperties && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkSparseImageFormatProperties2(d, &pProperties[i]);
        }
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkTrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkTrimCommandPool);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(commandPool));
    mvk_put_u32(e, (uint32_t)(flags));
    mvk_call_end(e);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo * pExternalBufferInfo, VkExternalBufferProperties * pExternalBufferProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceExternalBufferProperties);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pExternalBufferInfo ? 1 : 0);
    if (pExternalBufferInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPhysicalDeviceExternalBufferInfo(e, &pExternalBufferInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pExternalBufferProperties);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pExternalBufferProperties)
            mvk_out_VkExternalBufferProperties(d, pExternalBufferProperties);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo * pExternalFenceInfo, VkExternalFenceProperties * pExternalFenceProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceExternalFenceProperties);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pExternalFenceInfo ? 1 : 0);
    if (pExternalFenceInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPhysicalDeviceExternalFenceInfo(e, &pExternalFenceInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pExternalFenceProperties);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pExternalFenceProperties)
            mvk_out_VkExternalFenceProperties(d, pExternalFenceProperties);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo * pExternalSemaphoreInfo, VkExternalSemaphoreProperties * pExternalSemaphoreProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceExternalSemaphoreProperties);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pExternalSemaphoreInfo ? 1 : 0);
    if (pExternalSemaphoreInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPhysicalDeviceExternalSemaphoreInfo(e, &pExternalSemaphoreInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pExternalSemaphoreProperties);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pExternalSemaphoreProperties)
            mvk_out_VkExternalSemaphoreProperties(d, pExternalSemaphoreProperties);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDispatchBase);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(baseGroupX));
    mvk_put_u32(e, (uint32_t)(baseGroupY));
    mvk_put_u32(e, (uint32_t)(baseGroupZ));
    mvk_put_u32(e, (uint32_t)(groupCountX));
    mvk_put_u32(e, (uint32_t)(groupCountY));
    mvk_put_u32(e, (uint32_t)(groupCountZ));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL mvk_raw_vkCreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkDescriptorUpdateTemplate * pDescriptorUpdateTemplate)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateDescriptorUpdateTemplate);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDescriptorUpdateTemplateCreateInfo(e, &pCreateInfo[i]);
    }
    *pDescriptorUpdateTemplate = (VkDescriptorUpdateTemplate)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pDescriptorUpdateTemplate);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL mvk_raw_vkDestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyDescriptorUpdateTemplate);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(descriptorUpdateTemplate));
    mvk_call_end(e);
    mvk_free_id((uint64_t)descriptorUpdateTemplate);
}

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo * pCreateInfo, VkDescriptorSetLayoutSupport * pSupport)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDescriptorSetLayoutSupport);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDescriptorSetLayoutCreateInfo(e, &pCreateInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pSupport);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pSupport)
            mvk_out_VkDescriptorSetLayoutSupport(d, pSupport);
    }
    mvk_call_done(d);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkSamplerYcbcrConversion * pYcbcrConversion)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateSamplerYcbcrConversion);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkSamplerYcbcrConversionCreateInfo(e, &pCreateInfo[i]);
    }
    *pYcbcrConversion = (VkSamplerYcbcrConversion)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pYcbcrConversion);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroySamplerYcbcrConversion);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(ycbcrConversion));
    mvk_call_end(e);
    mvk_free_id((uint64_t)ycbcrConversion);
}

VKAPI_ATTR void VKAPI_CALL vkResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkResetQueryPool);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(firstQuery));
    mvk_put_u32(e, (uint32_t)(queryCount));
    mvk_call_end(e);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetBufferDeviceAddress);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBufferDeviceAddressInfo(e, &pInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    VkDeviceAddress result = d ? (VkDeviceAddress)mvk_get_u64(d) : (VkDeviceAddress)0;
    if (d) {
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetBufferOpaqueCaptureAddress);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBufferDeviceAddressInfo(e, &pInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    uint64_t result = d ? (uint64_t)mvk_get_u64(d) : (uint64_t)0;
    if (d) {
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddress(VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeviceMemoryOpaqueCaptureAddress);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDeviceMemoryOpaqueCaptureAddressInfo(e, &pInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    uint64_t result = d ? (uint64_t)mvk_get_u64(d) : (uint64_t)0;
    if (d) {
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawIndirectCount);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_u64(e, (uint64_t)(offset));
    mvk_put_handle(e, 0, (uint64_t)(countBuffer));
    mvk_put_u64(e, (uint64_t)(countBufferOffset));
    mvk_put_u32(e, (uint32_t)(maxDrawCount));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawIndexedIndirectCount);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_u64(e, (uint64_t)(offset));
    mvk_put_handle(e, 0, (uint64_t)(countBuffer));
    mvk_put_u64(e, (uint64_t)(countBufferOffset));
    mvk_put_u32(e, (uint32_t)(maxDrawCount));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2 * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkRenderPass * pRenderPass)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateRenderPass2);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkRenderPassCreateInfo2(e, &pCreateInfo[i]);
    }
    *pRenderPass = (VkRenderPass)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pRenderPass);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo * pRenderPassBegin, const VkSubpassBeginInfo * pSubpassBeginInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBeginRenderPass2);
    if (!e)
        return;
    mvk_put_u32(e, pRenderPassBegin ? 1 : 0);
    if (pRenderPassBegin) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkRenderPassBeginInfo(e, &pRenderPassBegin[i]);
    }
    mvk_put_u32(e, pSubpassBeginInfo ? 1 : 0);
    if (pSubpassBeginInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkSubpassBeginInfo(e, &pSubpassBeginInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo * pSubpassBeginInfo, const VkSubpassEndInfo * pSubpassEndInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdNextSubpass2);
    if (!e)
        return;
    mvk_put_u32(e, pSubpassBeginInfo ? 1 : 0);
    if (pSubpassBeginInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkSubpassBeginInfo(e, &pSubpassBeginInfo[i]);
    }
    mvk_put_u32(e, pSubpassEndInfo ? 1 : 0);
    if (pSubpassEndInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkSubpassEndInfo(e, &pSubpassEndInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo * pSubpassEndInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdEndRenderPass2);
    if (!e)
        return;
    mvk_put_u32(e, pSubpassEndInfo ? 1 : 0);
    if (pSubpassEndInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkSubpassEndInfo(e, &pSubpassEndInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceToolProperties(VkPhysicalDevice physicalDevice, uint32_t * pToolCount, VkPhysicalDeviceToolProperties * pToolProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceToolProperties);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pToolCount ? *pToolCount : 0);
    mvk_put_u32(e, pToolProperties ? 1 : 0);
    if (pToolProperties)
        for (uint32_t i = 0; i < (uint32_t)(*pToolCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pToolProperties[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pToolCount)
            *pToolCount = (uint32_t)mvk_get_u32(d);
        if (pToolProperties && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkPhysicalDeviceToolProperties(d, &pToolProperties[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePrivateDataSlot(VkDevice device, const VkPrivateDataSlotCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkPrivateDataSlot * pPrivateDataSlot)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreatePrivateDataSlot);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPrivateDataSlotCreateInfo(e, &pCreateInfo[i]);
    }
    *pPrivateDataSlot = (VkPrivateDataSlot)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pPrivateDataSlot);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPrivateDataSlot(VkDevice device, VkPrivateDataSlot privateDataSlot, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyPrivateDataSlot);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(privateDataSlot));
    mvk_call_end(e);
    mvk_free_id((uint64_t)privateDataSlot);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t data)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkSetPrivateData);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(objectType));
    mvk_put_u64(e, (uint64_t)(objectHandle));
    mvk_put_handle(e, 0, (uint64_t)(privateDataSlot));
    mvk_put_u64(e, (uint64_t)(data));
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t * pData)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPrivateData);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(objectType));
    mvk_put_u64(e, (uint64_t)(objectHandle));
    mvk_put_handle(e, 0, (uint64_t)(privateDataSlot));
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pData)
            *pData = (uint64_t)mvk_get_u64(d);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo * pDependencyInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdPipelineBarrier2);
    if (!e)
        return;
    mvk_put_u32(e, pDependencyInfo ? 1 : 0);
    if (pDependencyInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDependencyInfo(e, &pDependencyInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdWriteTimestamp2);
    if (!e)
        return;
    mvk_put_u64(e, (uint64_t)(stage));
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(query));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2 * pCopyBufferInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyBuffer2);
    if (!e)
        return;
    mvk_put_u32(e, pCopyBufferInfo ? 1 : 0);
    if (pCopyBufferInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyBufferInfo2(e, &pCopyBufferInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2 * pCopyImageInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyImage2);
    if (!e)
        return;
    mvk_put_u32(e, pCopyImageInfo ? 1 : 0);
    if (pCopyImageInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyImageInfo2(e, &pCopyImageInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2 * pCopyBufferToImageInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyBufferToImage2);
    if (!e)
        return;
    mvk_put_u32(e, pCopyBufferToImageInfo ? 1 : 0);
    if (pCopyBufferToImageInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyBufferToImageInfo2(e, &pCopyBufferToImageInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2 * pCopyImageToBufferInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyImageToBuffer2);
    if (!e)
        return;
    mvk_put_u32(e, pCopyImageToBufferInfo ? 1 : 0);
    if (pCopyImageToBufferInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyImageToBufferInfo2(e, &pCopyImageToBufferInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirements(VkDevice device, const VkDeviceBufferMemoryRequirements * pInfo, VkMemoryRequirements2 * pMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeviceBufferMemoryRequirements);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDeviceBufferMemoryRequirements(e, &pInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pMemoryRequirements);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMemoryRequirements)
            mvk_out_VkMemoryRequirements2(d, pMemoryRequirements);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirements(VkDevice device, const VkDeviceImageMemoryRequirements * pInfo, VkMemoryRequirements2 * pMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeviceImageMemoryRequirements);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDeviceImageMemoryRequirements(e, &pInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pMemoryRequirements);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMemoryRequirements)
            mvk_out_VkMemoryRequirements2(d, pMemoryRequirements);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSparseMemoryRequirements(VkDevice device, const VkDeviceImageMemoryRequirements * pInfo, uint32_t * pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2 * pSparseMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeviceImageSparseMemoryRequirements);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDeviceImageMemoryRequirements(e, &pInfo[i]);
    }
    mvk_put_u32(e, pSparseMemoryRequirementCount ? *pSparseMemoryRequirementCount : 0);
    mvk_put_u32(e, pSparseMemoryRequirements ? 1 : 0);
    if (pSparseMemoryRequirements)
        for (uint32_t i = 0; i < (uint32_t)(*pSparseMemoryRequirementCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pSparseMemoryRequirements[i]);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pSparseMemoryRequirementCount)
            *pSparseMemoryRequirementCount = (uint32_t)mvk_get_u32(d);
        if (pSparseMemoryRequirements && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkSparseImageMemoryRequirements2(d, &pSparseMemoryRequirements[i]);
        }
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo * pDependencyInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetEvent2);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(event));
    mvk_put_u32(e, pDependencyInfo ? 1 : 0);
    if (pDependencyInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDependencyInfo(e, &pDependencyInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdResetEvent2);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(event));
    mvk_put_u64(e, (uint64_t)(stageMask));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent * pEvents, const VkDependencyInfo * pDependencyInfos)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdWaitEvents2);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(eventCount));
    mvk_put_u32(e, pEvents ? 1 : 0);
    if (pEvents) {
        for (uint32_t i = 0; i < (uint32_t)(eventCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pEvents[i]));
    }
    mvk_put_u32(e, pDependencyInfos ? 1 : 0);
    if (pDependencyInfos) {
        for (uint32_t i = 0; i < (uint32_t)(eventCount); i++)
            mvk_put_VkDependencyInfo(e, &pDependencyInfos[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2 * pBlitImageInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBlitImage2);
    if (!e)
        return;
    mvk_put_u32(e, pBlitImageInfo ? 1 : 0);
    if (pBlitImageInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBlitImageInfo2(e, &pBlitImageInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2 * pResolveImageInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdResolveImage2);
    if (!e)
        return;
    mvk_put_u32(e, pResolveImageInfo ? 1 : 0);
    if (pResolveImageInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkResolveImageInfo2(e, &pResolveImageInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo * pRenderingInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBeginRendering);
    if (!e)
        return;
    mvk_put_u32(e, pRenderingInfo ? 1 : 0);
    if (pRenderingInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkRenderingInfo(e, &pRenderingInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering(VkCommandBuffer commandBuffer)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdEndRendering);
    if (!e)
        return;
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetCullMode(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetCullMode);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(cullMode));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetFrontFace(VkCommandBuffer commandBuffer, VkFrontFace frontFace)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetFrontFace);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(frontFace));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveTopology(VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetPrimitiveTopology);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(primitiveTopology));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWithCount(VkCommandBuffer commandBuffer, uint32_t viewportCount, const VkViewport * pViewports)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetViewportWithCount);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(viewportCount));
    mvk_put_u32(e, pViewports ? 1 : 0);
    if (pViewports) {
        for (uint32_t i = 0; i < (uint32_t)(viewportCount); i++)
            mvk_put_VkViewport(e, &pViewports[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissorWithCount(VkCommandBuffer commandBuffer, uint32_t scissorCount, const VkRect2D * pScissors)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetScissorWithCount);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(scissorCount));
    mvk_put_u32(e, pScissors ? 1 : 0);
    if (pScissors) {
        for (uint32_t i = 0; i < (uint32_t)(scissorCount); i++)
            mvk_put_VkRect2D(e, &pScissors[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer * pBuffers, const VkDeviceSize * pOffsets, const VkDeviceSize * pSizes, const VkDeviceSize * pStrides)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindVertexBuffers2);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstBinding));
    mvk_put_u32(e, (uint32_t)(bindingCount));
    mvk_put_u32(e, pBuffers ? 1 : 0);
    if (pBuffers) {
        for (uint32_t i = 0; i < (uint32_t)(bindingCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pBuffers[i]));
    }
    mvk_put_u32(e, pOffsets ? 1 : 0);
    if (pOffsets) {
        mvk_put_bytes(e, pOffsets, (size_t)(bindingCount) * 8);
    }
    mvk_put_u32(e, pSizes ? 1 : 0);
    if (pSizes) {
        mvk_put_bytes(e, pSizes, (size_t)(bindingCount) * 8);
    }
    mvk_put_u32(e, pStrides ? 1 : 0);
    if (pStrides) {
        mvk_put_bytes(e, pStrides, (size_t)(bindingCount) * 8);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDepthTestEnable);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(depthTestEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthWriteEnable(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDepthWriteEnable);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(depthWriteEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthCompareOp(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDepthCompareOp);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(depthCompareOp));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBoundsTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDepthBoundsTestEnable);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(depthBoundsTestEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilTestEnable(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetStencilTestEnable);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(stencilTestEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOp(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetStencilOp);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(faceMask));
    mvk_put_u32(e, (uint32_t)(failOp));
    mvk_put_u32(e, (uint32_t)(passOp));
    mvk_put_u32(e, (uint32_t)(depthFailOp));
    mvk_put_u32(e, (uint32_t)(compareOp));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizerDiscardEnable(VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetRasterizerDiscardEnable);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(rasterizerDiscardEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBiasEnable(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDepthBiasEnable);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(depthBiasEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetPrimitiveRestartEnable);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(primitiveRestartEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSubresourceLayout(VkDevice device, const VkDeviceImageSubresourceInfo * pInfo, VkSubresourceLayout2 * pLayout)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeviceImageSubresourceLayout);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDeviceImageSubresourceInfo(e, &pInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pLayout);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pLayout)
            mvk_out_VkSubresourceLayout2(d, pLayout);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2(VkDevice device, VkImage image, const VkImageSubresource2 * pSubresource, VkSubresourceLayout2 * pLayout)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetImageSubresourceLayout2);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(image));
    mvk_put_u32(e, pSubresource ? 1 : 0);
    if (pSubresource) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkImageSubresource2(e, &pSubresource[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pLayout);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pLayout)
            mvk_out_VkSubresourceLayout2(d, pLayout);
    }
    mvk_call_done(d);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToImage(VkDevice device, const VkCopyImageToImageInfo * pCopyImageToImageInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCopyImageToImage);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCopyImageToImageInfo ? 1 : 0);
    if (pCopyImageToImageInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyImageToImageInfo(e, &pCopyImageToImageInfo[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkTransitionImageLayout(VkDevice device, uint32_t transitionCount, const VkHostImageLayoutTransitionInfo * pTransitions)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkTransitionImageLayout);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(transitionCount));
    mvk_put_u32(e, pTransitions ? 1 : 0);
    if (pTransitions) {
        for (uint32_t i = 0; i < (uint32_t)(transitionCount); i++)
            mvk_put_VkHostImageLayoutTransitionInfo(e, &pTransitions[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet * pDescriptorWrites)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdPushDescriptorSet);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(pipelineBindPoint));
    mvk_put_handle(e, 0, (uint64_t)(layout));
    mvk_put_u32(e, (uint32_t)(set));
    mvk_put_u32(e, (uint32_t)(descriptorWriteCount));
    mvk_put_u32(e, pDescriptorWrites ? 1 : 0);
    if (pDescriptorWrites) {
        for (uint32_t i = 0; i < (uint32_t)(descriptorWriteCount); i++)
            mvk_put_VkWriteDescriptorSet(e, &pDescriptorWrites[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets2(VkCommandBuffer commandBuffer, const VkBindDescriptorSetsInfo * pBindDescriptorSetsInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindDescriptorSets2);
    if (!e)
        return;
    mvk_put_u32(e, pBindDescriptorSetsInfo ? 1 : 0);
    if (pBindDescriptorSetsInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBindDescriptorSetsInfo(e, &pBindDescriptorSetsInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants2(VkCommandBuffer commandBuffer, const VkPushConstantsInfo * pPushConstantsInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdPushConstants2);
    if (!e)
        return;
    mvk_put_u32(e, pPushConstantsInfo ? 1 : 0);
    if (pPushConstantsInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPushConstantsInfo(e, &pPushConstantsInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet2(VkCommandBuffer commandBuffer, const VkPushDescriptorSetInfo * pPushDescriptorSetInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdPushDescriptorSet2);
    if (!e)
        return;
    mvk_put_u32(e, pPushDescriptorSetInfo ? 1 : 0);
    if (pPushDescriptorSetInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPushDescriptorSetInfo(e, &pPushDescriptorSetInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStipple(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetLineStipple);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(lineStippleFactor));
    mvk_put_u16(e, (uint16_t)(lineStipplePattern));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer2(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkIndexType indexType)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindIndexBuffer2);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_u64(e, (uint64_t)(offset));
    mvk_put_u64(e, (uint64_t)(size));
    mvk_put_u32(e, (uint32_t)(indexType));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetRenderingAreaGranularity(VkDevice device, const VkRenderingAreaInfo * pRenderingAreaInfo, VkExtent2D * pGranularity)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetRenderingAreaGranularity);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pRenderingAreaInfo ? 1 : 0);
    if (pRenderingAreaInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkRenderingAreaInfo(e, &pRenderingAreaInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pGranularity)
            mvk_out_VkExtent2D(d, pGranularity);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingAttachmentLocations(VkCommandBuffer commandBuffer, const VkRenderingAttachmentLocationInfo * pLocationInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetRenderingAttachmentLocations);
    if (!e)
        return;
    mvk_put_u32(e, pLocationInfo ? 1 : 0);
    if (pLocationInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkRenderingAttachmentLocationInfo(e, &pLocationInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingInputAttachmentIndices(VkCommandBuffer commandBuffer, const VkRenderingInputAttachmentIndexInfo * pInputAttachmentIndexInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetRenderingInputAttachmentIndices);
    if (!e)
        return;
    mvk_put_u32(e, pInputAttachmentIndexInfo ? 1 : 0);
    if (pInputAttachmentIndexInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkRenderingInputAttachmentIndexInfo(e, &pInputAttachmentIndexInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer * pBuffers, const VkDeviceSize * pOffsets, const VkDeviceSize * pSizes)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindTransformFeedbackBuffersEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstBinding));
    mvk_put_u32(e, (uint32_t)(bindingCount));
    mvk_put_u32(e, pBuffers ? 1 : 0);
    if (pBuffers) {
        for (uint32_t i = 0; i < (uint32_t)(bindingCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pBuffers[i]));
    }
    mvk_put_u32(e, pOffsets ? 1 : 0);
    if (pOffsets) {
        mvk_put_bytes(e, pOffsets, (size_t)(bindingCount) * 8);
    }
    mvk_put_u32(e, pSizes ? 1 : 0);
    if (pSizes) {
        mvk_put_bytes(e, pSizes, (size_t)(bindingCount) * 8);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer, uint32_t counterBufferCount, const VkBuffer * pCounterBuffers, const VkDeviceSize * pCounterBufferOffsets)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBeginTransformFeedbackEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstCounterBuffer));
    mvk_put_u32(e, (uint32_t)(counterBufferCount));
    mvk_put_u32(e, pCounterBuffers ? 1 : 0);
    if (pCounterBuffers) {
        for (uint32_t i = 0; i < (uint32_t)(counterBufferCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pCounterBuffers[i]));
    }
    mvk_put_u32(e, pCounterBufferOffsets ? 1 : 0);
    if (pCounterBufferOffsets) {
        mvk_put_bytes(e, pCounterBufferOffsets, (size_t)(counterBufferCount) * 8);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer, uint32_t counterBufferCount, const VkBuffer * pCounterBuffers, const VkDeviceSize * pCounterBufferOffsets)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdEndTransformFeedbackEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstCounterBuffer));
    mvk_put_u32(e, (uint32_t)(counterBufferCount));
    mvk_put_u32(e, pCounterBuffers ? 1 : 0);
    if (pCounterBuffers) {
        for (uint32_t i = 0; i < (uint32_t)(counterBufferCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pCounterBuffers[i]));
    }
    mvk_put_u32(e, pCounterBufferOffsets ? 1 : 0);
    if (pCounterBufferOffsets) {
        mvk_put_bytes(e, pCounterBufferOffsets, (size_t)(counterBufferCount) * 8);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags, uint32_t index)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBeginQueryIndexedEXT);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(query));
    mvk_put_u32(e, (uint32_t)(flags));
    mvk_put_u32(e, (uint32_t)(index));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, uint32_t index)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdEndQueryIndexedEXT);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(query));
    mvk_put_u32(e, (uint32_t)(index));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer, uint32_t instanceCount, uint32_t firstInstance, VkBuffer counterBuffer, VkDeviceSize counterBufferOffset, uint32_t counterOffset, uint32_t vertexStride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawIndirectByteCountEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(instanceCount));
    mvk_put_u32(e, (uint32_t)(firstInstance));
    mvk_put_handle(e, 0, (uint64_t)(counterBuffer));
    mvk_put_u64(e, (uint64_t)(counterBufferOffset));
    mvk_put_u32(e, (uint32_t)(counterOffset));
    mvk_put_u32(e, (uint32_t)(vertexStride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR uint32_t VKAPI_CALL vkGetImageViewHandleNVX(VkDevice device, const VkImageViewHandleInfoNVX * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetImageViewHandleNVX);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkImageViewHandleInfoNVX(e, &pInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    uint32_t result = d ? (uint32_t)mvk_get_u32(d) : (uint32_t)0;
    if (d) {
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetImageViewHandle64NVX(VkDevice device, const VkImageViewHandleInfoNVX * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetImageViewHandle64NVX);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkImageViewHandleInfoNVX(e, &pInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    uint64_t result = d ? (uint64_t)mvk_get_u64(d) : (uint64_t)0;
    if (d) {
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageViewAddressNVX(VkDevice device, VkImageView imageView, VkImageViewAddressPropertiesNVX * pProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetImageViewAddressNVX);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(imageView));
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pProperties);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pProperties)
            mvk_out_VkImageViewAddressPropertiesNVX(d, pProperties);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceCombinedImageSamplerIndexNVX(VkDevice device, uint64_t imageViewIndex, uint64_t samplerIndex)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeviceCombinedImageSamplerIndexNVX);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u64(e, (uint64_t)(imageViewIndex));
    mvk_put_u64(e, (uint64_t)(samplerIndex));
    MvkDecoder *d = mvk_call_wait(e);
    uint64_t result = d ? (uint64_t)mvk_get_u64(d) : (uint64_t)0;
    if (d) {
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetShaderInfoAMD(VkDevice device, VkPipeline pipeline, VkShaderStageFlagBits shaderStage, VkShaderInfoTypeAMD infoType, size_t * pInfoSize, void * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetShaderInfoAMD);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(pipeline));
    mvk_put_u32(e, (uint32_t)(shaderStage));
    mvk_put_u32(e, (uint32_t)(infoType));
    mvk_put_u32(e, pInfo ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pInfoSize)
            *pInfoSize = (size_t)mvk_get_u64(d);
        if (pInfo && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            mvk_get_bytes(d, pInfo, n);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceExternalImageFormatPropertiesNV(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkExternalMemoryHandleTypeFlagsNV externalHandleType, VkExternalImageFormatPropertiesNV * pExternalImageFormatProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceExternalImageFormatPropertiesNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, (uint32_t)(format));
    mvk_put_u32(e, (uint32_t)(type));
    mvk_put_u32(e, (uint32_t)(tiling));
    mvk_put_u32(e, (uint32_t)(usage));
    mvk_put_u32(e, (uint32_t)(flags));
    mvk_put_u32(e, (uint32_t)(externalHandleType));
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pExternalImageFormatProperties)
            mvk_out_VkExternalImageFormatPropertiesNV(d, pExternalImageFormatProperties);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginConditionalRenderingEXT(VkCommandBuffer commandBuffer, const VkConditionalRenderingBeginInfoEXT * pConditionalRenderingBegin)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBeginConditionalRenderingEXT);
    if (!e)
        return;
    mvk_put_u32(e, pConditionalRenderingBegin ? 1 : 0);
    if (pConditionalRenderingBegin) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkConditionalRenderingBeginInfoEXT(e, &pConditionalRenderingBegin[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndConditionalRenderingEXT(VkCommandBuffer commandBuffer)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdEndConditionalRenderingEXT);
    if (!e)
        return;
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWScalingNV(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewportWScalingNV * pViewportWScalings)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetViewportWScalingNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstViewport));
    mvk_put_u32(e, (uint32_t)(viewportCount));
    mvk_put_u32(e, pViewportWScalings ? 1 : 0);
    if (pViewportWScalings) {
        for (uint32_t i = 0; i < (uint32_t)(viewportCount); i++)
            mvk_put_VkViewportWScalingNV(e, &pViewportWScalings[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleEXT(VkCommandBuffer commandBuffer, uint32_t firstDiscardRectangle, uint32_t discardRectangleCount, const VkRect2D * pDiscardRectangles)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDiscardRectangleEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstDiscardRectangle));
    mvk_put_u32(e, (uint32_t)(discardRectangleCount));
    mvk_put_u32(e, pDiscardRectangles ? 1 : 0);
    if (pDiscardRectangles) {
        for (uint32_t i = 0; i < (uint32_t)(discardRectangleCount); i++)
            mvk_put_VkRect2D(e, &pDiscardRectangles[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleEnableEXT(VkCommandBuffer commandBuffer, VkBool32 discardRectangleEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDiscardRectangleEnableEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(discardRectangleEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleModeEXT(VkCommandBuffer commandBuffer, VkDiscardRectangleModeEXT discardRectangleMode)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDiscardRectangleModeEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(discardRectangleMode));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, uint32_t * pCounterCount, VkPerformanceCounterKHR * pCounters, VkPerformanceCounterDescriptionKHR * pCounterDescriptions)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, (uint32_t)(queueFamilyIndex));
    mvk_put_u32(e, pCounterCount ? *pCounterCount : 0);
    mvk_put_u32(e, pCounters ? 1 : 0);
    if (pCounters)
        for (uint32_t i = 0; i < (uint32_t)(*pCounterCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pCounters[i]);
    mvk_put_u32(e, pCounterDescriptions ? 1 : 0);
    if (pCounterDescriptions)
        for (uint32_t i = 0; i < (uint32_t)(*pCounterCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pCounterDescriptions[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pCounterCount)
            *pCounterCount = (uint32_t)mvk_get_u32(d);
        if (pCounters && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkPerformanceCounterKHR(d, &pCounters[i]);
        }
        if (pCounterDescriptions && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkPerformanceCounterDescriptionKHR(d, &pCounterDescriptions[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR(VkPhysicalDevice physicalDevice, const VkQueryPoolPerformanceCreateInfoKHR * pPerformanceQueryCreateInfo, uint32_t * pNumPasses)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pPerformanceQueryCreateInfo ? 1 : 0);
    if (pPerformanceQueryCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkQueryPoolPerformanceCreateInfoKHR(e, &pPerformanceQueryCreateInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pNumPasses)
            *pNumPasses = (uint32_t)mvk_get_u32(d);
    }
    mvk_call_done(d);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireProfilingLockKHR(VkDevice device, const VkAcquireProfilingLockInfoKHR * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkAcquireProfilingLockKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkAcquireProfilingLockInfoKHR(e, &pInfo[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkReleaseProfilingLockKHR(VkDevice device)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkReleaseProfilingLockKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_call_end(e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateGpaSessionAMD(VkDevice device, const VkGpaSessionCreateInfoAMD * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkGpaSessionAMD * pGpaSession)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateGpaSessionAMD);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkGpaSessionCreateInfoAMD(e, &pCreateInfo[i]);
    }
    *pGpaSession = (VkGpaSessionAMD)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pGpaSession);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyGpaSessionAMD(VkDevice device, VkGpaSessionAMD gpaSession, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyGpaSessionAMD);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(gpaSession));
    mvk_call_end(e);
    mvk_free_id((uint64_t)gpaSession);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetGpaDeviceClockModeAMD(VkDevice device, VkGpaDeviceClockModeInfoAMD * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkSetGpaDeviceClockModeAMD);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pInfo);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pInfo)
            mvk_out_VkGpaDeviceClockModeInfoAMD(d, pInfo);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetGpaDeviceClockInfoAMD(VkDevice device, VkGpaDeviceGetClockInfoAMD * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetGpaDeviceClockInfoAMD);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pInfo);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pInfo)
            mvk_out_VkGpaDeviceGetClockInfoAMD(d, pInfo);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndGpaSampleAMD(VkCommandBuffer commandBuffer, VkGpaSessionAMD gpaSession, uint32_t sampleID)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdEndGpaSampleAMD);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(gpaSession));
    mvk_put_u32(e, (uint32_t)(sampleID));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetGpaSessionStatusAMD(VkDevice device, VkGpaSessionAMD gpaSession)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetGpaSessionStatusAMD);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(gpaSession));
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetGpaSessionResultsAMD(VkDevice device, VkGpaSessionAMD gpaSession, uint32_t sampleID, size_t * pSizeInBytes, void * pData)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetGpaSessionResultsAMD);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(gpaSession));
    mvk_put_u32(e, (uint32_t)(sampleID));
    mvk_put_u32(e, pData ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pSizeInBytes)
            *pSizeInBytes = (size_t)mvk_get_u64(d);
        if (pData && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            mvk_get_bytes(d, pData, n);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetGpaSessionAMD(VkDevice device, VkGpaSessionAMD gpaSession)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkResetGpaSessionAMD);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(gpaSession));
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyGpaSessionResultsAMD(VkCommandBuffer commandBuffer, VkGpaSessionAMD gpaSession)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyGpaSessionResultsAMD);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(gpaSession));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkWriteSamplerDescriptorsEXT(VkDevice device, uint32_t samplerCount, const VkSamplerCreateInfo * pSamplers, const VkHostAddressRangeEXT * pDescriptors)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkWriteSamplerDescriptorsEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(samplerCount));
    mvk_put_u32(e, pSamplers ? 1 : 0);
    if (pSamplers) {
        for (uint32_t i = 0; i < (uint32_t)(samplerCount); i++)
            mvk_put_VkSamplerCreateInfo(e, &pSamplers[i]);
    }
    mvk_put_u32(e, pDescriptors ? 1 : 0);
    if (pDescriptors) {
        for (uint32_t i = 0; i < (uint32_t)(samplerCount); i++)
            mvk_put_VkHostAddressRangeEXT(e, &pDescriptors[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWriteResourceDescriptorsEXT(VkDevice device, uint32_t resourceCount, const VkResourceDescriptorInfoEXT * pResources, const VkHostAddressRangeEXT * pDescriptors)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkWriteResourceDescriptorsEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(resourceCount));
    mvk_put_u32(e, pResources ? 1 : 0);
    if (pResources) {
        for (uint32_t i = 0; i < (uint32_t)(resourceCount); i++)
            mvk_put_VkResourceDescriptorInfoEXT(e, &pResources[i]);
    }
    mvk_put_u32(e, pDescriptors ? 1 : 0);
    if (pDescriptors) {
        for (uint32_t i = 0; i < (uint32_t)(resourceCount); i++)
            mvk_put_VkHostAddressRangeEXT(e, &pDescriptors[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindSamplerHeapEXT(VkCommandBuffer commandBuffer, const VkBindHeapInfoEXT * pBindInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindSamplerHeapEXT);
    if (!e)
        return;
    mvk_put_u32(e, pBindInfo ? 1 : 0);
    if (pBindInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBindHeapInfoEXT(e, &pBindInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindResourceHeapEXT(VkCommandBuffer commandBuffer, const VkBindHeapInfoEXT * pBindInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindResourceHeapEXT);
    if (!e)
        return;
    mvk_put_u32(e, pBindInfo ? 1 : 0);
    if (pBindInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBindHeapInfoEXT(e, &pBindInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDataEXT(VkCommandBuffer commandBuffer, const VkPushDataInfoEXT * pPushDataInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdPushDataEXT);
    if (!e)
        return;
    mvk_put_u32(e, pPushDataInfo ? 1 : 0);
    if (pPushDataInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPushDataInfoEXT(e, &pPushDataInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageOpaqueCaptureDataEXT(VkDevice device, uint32_t imageCount, const VkImage * pImages, VkHostAddressRangeEXT * pDatas)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetImageOpaqueCaptureDataEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(imageCount));
    mvk_put_u32(e, pImages ? 1 : 0);
    if (pImages) {
        for (uint32_t i = 0; i < (uint32_t)(imageCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pImages[i]));
    }
    mvk_put_u32(e, pDatas ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pDatas && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkHostAddressRangeEXT(d, &pDatas[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkDeviceSize VKAPI_CALL vkGetPhysicalDeviceDescriptorSizeEXT(VkPhysicalDevice physicalDevice, VkDescriptorType descriptorType)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceDescriptorSizeEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, (uint32_t)(descriptorType));
    MvkDecoder *d = mvk_call_wait(e);
    VkDeviceSize result = d ? (VkDeviceSize)mvk_get_u64(d) : (VkDeviceSize)0;
    if (d) {
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkRegisterCustomBorderColorEXT(VkDevice device, const VkSamplerCustomBorderColorCreateInfoEXT * pBorderColor, VkBool32 requestIndex, uint32_t * pIndex)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkRegisterCustomBorderColorEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pBorderColor ? 1 : 0);
    if (pBorderColor) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkSamplerCustomBorderColorCreateInfoEXT(e, &pBorderColor[i]);
    }
    mvk_put_u32(e, (uint32_t)(requestIndex));
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pIndex)
            *pIndex = (uint32_t)mvk_get_u32(d);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkUnregisterCustomBorderColorEXT(VkDevice device, uint32_t index)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkUnregisterCustomBorderColorEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(index));
    mvk_call_end(e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetTensorOpaqueCaptureDataARM(VkDevice device, uint32_t tensorCount, const VkTensorARM * pTensors, VkHostAddressRangeEXT * pDatas)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetTensorOpaqueCaptureDataARM);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(tensorCount));
    mvk_put_u32(e, pTensors ? 1 : 0);
    if (pTensors) {
        for (uint32_t i = 0; i < (uint32_t)(tensorCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pTensors[i]));
    }
    mvk_put_u32(e, pDatas ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pDatas && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkHostAddressRangeEXT(d, &pDatas[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleLocationsEXT(VkCommandBuffer commandBuffer, const VkSampleLocationsInfoEXT * pSampleLocationsInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetSampleLocationsEXT);
    if (!e)
        return;
    mvk_put_u32(e, pSampleLocationsInfo ? 1 : 0);
    if (pSampleLocationsInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkSampleLocationsInfoEXT(e, &pSampleLocationsInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMultisamplePropertiesEXT(VkPhysicalDevice physicalDevice, VkSampleCountFlagBits samples, VkMultisamplePropertiesEXT * pMultisampleProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceMultisamplePropertiesEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, (uint32_t)(samples));
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pMultisampleProperties);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMultisampleProperties)
            mvk_out_VkMultisamplePropertiesEXT(d, pMultisampleProperties);
    }
    mvk_call_done(d);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureKHR(VkDevice device, const VkAccelerationStructureCreateInfoKHR * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkAccelerationStructureKHR * pAccelerationStructure)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateAccelerationStructureKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkAccelerationStructureCreateInfoKHR(e, &pCreateInfo[i]);
    }
    *pAccelerationStructure = (VkAccelerationStructureKHR)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pAccelerationStructure);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureKHR(VkDevice device, VkAccelerationStructureKHR accelerationStructure, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyAccelerationStructureKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(accelerationStructure));
    mvk_call_end(e);
    mvk_free_id((uint64_t)accelerationStructure);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR * pInfos, const VkAccelerationStructureBuildRangeInfoKHR * const * ppBuildRangeInfos)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBuildAccelerationStructuresKHR);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(infoCount));
    mvk_put_u32(e, pInfos ? 1 : 0);
    if (pInfos) {
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_put_VkAccelerationStructureBuildGeometryInfoKHR(e, &pInfos[i]);
    }
    mvk_put_u32(e, ppBuildRangeInfos ? 1 : 0);
    if (ppBuildRangeInfos) {
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            for (uint32_t j = 0; j < (uint32_t)(pInfos[i].geometryCount); j++)
                mvk_put_VkAccelerationStructureBuildRangeInfoKHR(e, &ppBuildRangeInfos[i][j]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresIndirectKHR(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR * pInfos, const VkDeviceAddress * pIndirectDeviceAddresses, const uint32_t * pIndirectStrides, const uint32_t * const * ppMaxPrimitiveCounts)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBuildAccelerationStructuresIndirectKHR);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(infoCount));
    mvk_put_u32(e, pInfos ? 1 : 0);
    if (pInfos) {
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_put_VkAccelerationStructureBuildGeometryInfoKHR(e, &pInfos[i]);
    }
    mvk_put_u32(e, pIndirectDeviceAddresses ? 1 : 0);
    if (pIndirectDeviceAddresses) {
        mvk_put_bytes(e, pIndirectDeviceAddresses, (size_t)(infoCount) * 8);
    }
    mvk_put_u32(e, pIndirectStrides ? 1 : 0);
    if (pIndirectStrides) {
        mvk_put_bytes(e, pIndirectStrides, (size_t)(infoCount) * 4);
    }
    mvk_put_u32(e, ppMaxPrimitiveCounts ? 1 : 0);
    if (ppMaxPrimitiveCounts) {
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_put_bytes(e, ppMaxPrimitiveCounts[i], (size_t)(pInfos[i].geometryCount) * sizeof(uint32_t));
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBuildAccelerationStructuresKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR * pInfos, const VkAccelerationStructureBuildRangeInfoKHR * const * ppBuildRangeInfos)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkBuildAccelerationStructuresKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(deferredOperation));
    mvk_put_u32(e, (uint32_t)(infoCount));
    mvk_put_u32(e, pInfos ? 1 : 0);
    if (pInfos) {
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_put_VkAccelerationStructureBuildGeometryInfoKHR(e, &pInfos[i]);
    }
    mvk_put_u32(e, ppBuildRangeInfos ? 1 : 0);
    if (ppBuildRangeInfos) {
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            for (uint32_t j = 0; j < (uint32_t)(pInfos[i].geometryCount); j++)
                mvk_put_VkAccelerationStructureBuildRangeInfoKHR(e, &ppBuildRangeInfos[i][j]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyAccelerationStructureKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyAccelerationStructureInfoKHR * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCopyAccelerationStructureKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(deferredOperation));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyAccelerationStructureInfoKHR(e, &pInfo[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyAccelerationStructureToMemoryKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyAccelerationStructureToMemoryInfoKHR * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCopyAccelerationStructureToMemoryKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(deferredOperation));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyAccelerationStructureToMemoryInfoKHR(e, &pInfo[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToAccelerationStructureKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMemoryToAccelerationStructureInfoKHR * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCopyMemoryToAccelerationStructureKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(deferredOperation));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyMemoryToAccelerationStructureInfoKHR(e, &pInfo[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWriteAccelerationStructuresPropertiesKHR(VkDevice device, uint32_t accelerationStructureCount, const VkAccelerationStructureKHR * pAccelerationStructures, VkQueryType queryType, size_t dataSize, void * pData, size_t stride)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkWriteAccelerationStructuresPropertiesKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(accelerationStructureCount));
    mvk_put_u32(e, pAccelerationStructures ? 1 : 0);
    if (pAccelerationStructures) {
        for (uint32_t i = 0; i < (uint32_t)(accelerationStructureCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pAccelerationStructures[i]));
    }
    mvk_put_u32(e, (uint32_t)(queryType));
    mvk_put_u64(e, (uint64_t)(dataSize));
    mvk_put_u32(e, pData ? 1 : 0);
    mvk_put_u64(e, (uint64_t)(stride));
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pData && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            mvk_get_bytes(d, pData, n);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyAccelerationStructureKHR);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyAccelerationStructureInfoKHR(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureToMemoryInfoKHR * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyAccelerationStructureToMemoryKHR);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyAccelerationStructureToMemoryInfoKHR(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyMemoryToAccelerationStructureInfoKHR * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyMemoryToAccelerationStructureKHR);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyMemoryToAccelerationStructureInfoKHR(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetAccelerationStructureDeviceAddressKHR(VkDevice device, const VkAccelerationStructureDeviceAddressInfoKHR * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetAccelerationStructureDeviceAddressKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkAccelerationStructureDeviceAddressInfoKHR(e, &pInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    VkDeviceAddress result = d ? (VkDeviceAddress)mvk_get_u64(d) : (VkDeviceAddress)0;
    if (d) {
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteAccelerationStructuresPropertiesKHR(VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount, const VkAccelerationStructureKHR * pAccelerationStructures, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdWriteAccelerationStructuresPropertiesKHR);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(accelerationStructureCount));
    mvk_put_u32(e, pAccelerationStructures ? 1 : 0);
    if (pAccelerationStructures) {
        for (uint32_t i = 0; i < (uint32_t)(accelerationStructureCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pAccelerationStructures[i]));
    }
    mvk_put_u32(e, (uint32_t)(queryType));
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(firstQuery));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceAccelerationStructureCompatibilityKHR(VkDevice device, const VkAccelerationStructureVersionInfoKHR * pVersionInfo, VkAccelerationStructureCompatibilityKHR * pCompatibility)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeviceAccelerationStructureCompatibilityKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pVersionInfo ? 1 : 0);
    if (pVersionInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkAccelerationStructureVersionInfoKHR(e, &pVersionInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pCompatibility)
            *pCompatibility = (VkAccelerationStructureCompatibilityKHR)mvk_get_u32(d);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureBuildSizesKHR(VkDevice device, VkAccelerationStructureBuildTypeKHR buildType, const VkAccelerationStructureBuildGeometryInfoKHR * pBuildInfo, const uint32_t * pMaxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR * pSizeInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetAccelerationStructureBuildSizesKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(buildType));
    mvk_put_u32(e, pBuildInfo ? 1 : 0);
    if (pBuildInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkAccelerationStructureBuildGeometryInfoKHR(e, &pBuildInfo[i]);
    }
    mvk_put_u32(e, pMaxPrimitiveCounts ? 1 : 0);
    if (pMaxPrimitiveCounts) {
        mvk_put_bytes(e, pMaxPrimitiveCounts, (size_t)(pBuildInfo->geometryCount) * 4);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pSizeInfo);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pSizeInfo)
            mvk_out_VkAccelerationStructureBuildSizesInfoKHR(d, pSizeInfo);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR * pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR * pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR * pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR * pCallableShaderBindingTable, uint32_t width, uint32_t height, uint32_t depth)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdTraceRaysKHR);
    if (!e)
        return;
    mvk_put_u32(e, pRaygenShaderBindingTable ? 1 : 0);
    if (pRaygenShaderBindingTable) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkStridedDeviceAddressRegionKHR(e, &pRaygenShaderBindingTable[i]);
    }
    mvk_put_u32(e, pMissShaderBindingTable ? 1 : 0);
    if (pMissShaderBindingTable) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkStridedDeviceAddressRegionKHR(e, &pMissShaderBindingTable[i]);
    }
    mvk_put_u32(e, pHitShaderBindingTable ? 1 : 0);
    if (pHitShaderBindingTable) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkStridedDeviceAddressRegionKHR(e, &pHitShaderBindingTable[i]);
    }
    mvk_put_u32(e, pCallableShaderBindingTable ? 1 : 0);
    if (pCallableShaderBindingTable) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkStridedDeviceAddressRegionKHR(e, &pCallableShaderBindingTable[i]);
    }
    mvk_put_u32(e, (uint32_t)(width));
    mvk_put_u32(e, (uint32_t)(height));
    mvk_put_u32(e, (uint32_t)(depth));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoKHR * pCreateInfos, const VkAllocationCallbacks * pAllocator, VkPipeline * pPipelines)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateRayTracingPipelinesKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(deferredOperation));
    mvk_put_handle(e, 0, (uint64_t)(pipelineCache));
    mvk_put_u32(e, (uint32_t)(createInfoCount));
    mvk_put_u32(e, pCreateInfos ? 1 : 0);
    if (pCreateInfos) {
        for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
            mvk_put_VkRayTracingPipelineCreateInfoKHR(e, &pCreateInfos[i]);
    }
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++) {
        pPipelines[i] = (VkPipeline)mvk_new_id();
        mvk_put_u64(e, (uint64_t)pPipelines[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline, uint32_t firstGroup, uint32_t groupCount, size_t dataSize, void * pData)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetRayTracingShaderGroupHandlesKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(pipeline));
    mvk_put_u32(e, (uint32_t)(firstGroup));
    mvk_put_u32(e, (uint32_t)(groupCount));
    mvk_put_u64(e, (uint64_t)(dataSize));
    mvk_put_u32(e, pData ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pData && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            mvk_get_bytes(d, pData, n);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline, uint32_t firstGroup, uint32_t groupCount, size_t dataSize, void * pData)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(pipeline));
    mvk_put_u32(e, (uint32_t)(firstGroup));
    mvk_put_u32(e, (uint32_t)(groupCount));
    mvk_put_u64(e, (uint64_t)(dataSize));
    mvk_put_u32(e, pData ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pData && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            mvk_get_bytes(d, pData, n);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR * pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR * pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR * pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR * pCallableShaderBindingTable, VkDeviceAddress indirectDeviceAddress)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdTraceRaysIndirectKHR);
    if (!e)
        return;
    mvk_put_u32(e, pRaygenShaderBindingTable ? 1 : 0);
    if (pRaygenShaderBindingTable) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkStridedDeviceAddressRegionKHR(e, &pRaygenShaderBindingTable[i]);
    }
    mvk_put_u32(e, pMissShaderBindingTable ? 1 : 0);
    if (pMissShaderBindingTable) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkStridedDeviceAddressRegionKHR(e, &pMissShaderBindingTable[i]);
    }
    mvk_put_u32(e, pHitShaderBindingTable ? 1 : 0);
    if (pHitShaderBindingTable) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkStridedDeviceAddressRegionKHR(e, &pHitShaderBindingTable[i]);
    }
    mvk_put_u32(e, pCallableShaderBindingTable ? 1 : 0);
    if (pCallableShaderBindingTable) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkStridedDeviceAddressRegionKHR(e, &pCallableShaderBindingTable[i]);
    }
    mvk_put_u64(e, (uint64_t)(indirectDeviceAddress));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkDeviceSize VKAPI_CALL vkGetRayTracingShaderGroupStackSizeKHR(VkDevice device, VkPipeline pipeline, uint32_t group, VkShaderGroupShaderKHR groupShader)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetRayTracingShaderGroupStackSizeKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(pipeline));
    mvk_put_u32(e, (uint32_t)(group));
    mvk_put_u32(e, (uint32_t)(groupShader));
    MvkDecoder *d = mvk_call_wait(e);
    VkDeviceSize result = d ? (VkDeviceSize)mvk_get_u64(d) : (VkDeviceSize)0;
    if (d) {
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRayTracingPipelineStackSizeKHR(VkCommandBuffer commandBuffer, uint32_t pipelineStackSize)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetRayTracingPipelineStackSizeKHR);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(pipelineStackSize));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageDrmFormatModifierPropertiesEXT(VkDevice device, VkImage image, VkImageDrmFormatModifierPropertiesEXT * pProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetImageDrmFormatModifierPropertiesEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(image));
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pProperties);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pProperties)
            mvk_out_VkImageDrmFormatModifierPropertiesEXT(d, pProperties);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateValidationCacheEXT(VkDevice device, const VkValidationCacheCreateInfoEXT * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkValidationCacheEXT * pValidationCache)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateValidationCacheEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkValidationCacheCreateInfoEXT(e, &pCreateInfo[i]);
    }
    *pValidationCache = (VkValidationCacheEXT)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pValidationCache);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyValidationCacheEXT(VkDevice device, VkValidationCacheEXT validationCache, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyValidationCacheEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(validationCache));
    mvk_call_end(e);
    mvk_free_id((uint64_t)validationCache);
}

VKAPI_ATTR VkResult VKAPI_CALL vkMergeValidationCachesEXT(VkDevice device, VkValidationCacheEXT dstCache, uint32_t srcCacheCount, const VkValidationCacheEXT * pSrcCaches)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkMergeValidationCachesEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(dstCache));
    mvk_put_u32(e, (uint32_t)(srcCacheCount));
    mvk_put_u32(e, pSrcCaches ? 1 : 0);
    if (pSrcCaches) {
        for (uint32_t i = 0; i < (uint32_t)(srcCacheCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pSrcCaches[i]));
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetValidationCacheDataEXT(VkDevice device, VkValidationCacheEXT validationCache, size_t * pDataSize, void * pData)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetValidationCacheDataEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(validationCache));
    mvk_put_u32(e, pData ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pDataSize)
            *pDataSize = (size_t)mvk_get_u64(d);
        if (pData && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            mvk_get_bytes(d, pData, n);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindShadingRateImageNV(VkCommandBuffer commandBuffer, VkImageView imageView, VkImageLayout imageLayout)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindShadingRateImageNV);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(imageView));
    mvk_put_u32(e, (uint32_t)(imageLayout));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportShadingRatePaletteNV(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkShadingRatePaletteNV * pShadingRatePalettes)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetViewportShadingRatePaletteNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstViewport));
    mvk_put_u32(e, (uint32_t)(viewportCount));
    mvk_put_u32(e, pShadingRatePalettes ? 1 : 0);
    if (pShadingRatePalettes) {
        for (uint32_t i = 0; i < (uint32_t)(viewportCount); i++)
            mvk_put_VkShadingRatePaletteNV(e, &pShadingRatePalettes[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoarseSampleOrderNV(VkCommandBuffer commandBuffer, VkCoarseSampleOrderTypeNV sampleOrderType, uint32_t customSampleOrderCount, const VkCoarseSampleOrderCustomNV * pCustomSampleOrders)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetCoarseSampleOrderNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(sampleOrderType));
    mvk_put_u32(e, (uint32_t)(customSampleOrderCount));
    mvk_put_u32(e, pCustomSampleOrders ? 1 : 0);
    if (pCustomSampleOrders) {
        for (uint32_t i = 0; i < (uint32_t)(customSampleOrderCount); i++)
            mvk_put_VkCoarseSampleOrderCustomNV(e, &pCustomSampleOrders[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureNV(VkDevice device, const VkAccelerationStructureCreateInfoNV * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkAccelerationStructureNV * pAccelerationStructure)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateAccelerationStructureNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkAccelerationStructureCreateInfoNV(e, &pCreateInfo[i]);
    }
    *pAccelerationStructure = (VkAccelerationStructureNV)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pAccelerationStructure);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureNV(VkDevice device, VkAccelerationStructureNV accelerationStructure, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyAccelerationStructureNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(accelerationStructure));
    mvk_call_end(e);
    mvk_free_id((uint64_t)accelerationStructure);
}

VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureMemoryRequirementsNV(VkDevice device, const VkAccelerationStructureMemoryRequirementsInfoNV * pInfo, VkMemoryRequirements2 * pMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetAccelerationStructureMemoryRequirementsNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkAccelerationStructureMemoryRequirementsInfoNV(e, &pInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pMemoryRequirements);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMemoryRequirements)
            mvk_out_VkMemoryRequirements2(d, pMemoryRequirements);
    }
    mvk_call_done(d);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindAccelerationStructureMemoryNV(VkDevice device, uint32_t bindInfoCount, const VkBindAccelerationStructureMemoryInfoNV * pBindInfos)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkBindAccelerationStructureMemoryNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(bindInfoCount));
    mvk_put_u32(e, pBindInfos ? 1 : 0);
    if (pBindInfos) {
        for (uint32_t i = 0; i < (uint32_t)(bindInfoCount); i++)
            mvk_put_VkBindAccelerationStructureMemoryInfoNV(e, &pBindInfos[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructureNV(VkCommandBuffer commandBuffer, const VkAccelerationStructureInfoNV * pInfo, VkBuffer instanceData, VkDeviceSize instanceOffset, VkBool32 update, VkAccelerationStructureNV dst, VkAccelerationStructureNV src, VkBuffer scratch, VkDeviceSize scratchOffset)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBuildAccelerationStructureNV);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkAccelerationStructureInfoNV(e, &pInfo[i]);
    }
    mvk_put_handle(e, 0, (uint64_t)(instanceData));
    mvk_put_u64(e, (uint64_t)(instanceOffset));
    mvk_put_u32(e, (uint32_t)(update));
    mvk_put_handle(e, 0, (uint64_t)(dst));
    mvk_put_handle(e, 0, (uint64_t)(src));
    mvk_put_handle(e, 0, (uint64_t)(scratch));
    mvk_put_u64(e, (uint64_t)(scratchOffset));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureNV(VkCommandBuffer commandBuffer, VkAccelerationStructureNV dst, VkAccelerationStructureNV src, VkCopyAccelerationStructureModeKHR mode)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyAccelerationStructureNV);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(dst));
    mvk_put_handle(e, 0, (uint64_t)(src));
    mvk_put_u32(e, (uint32_t)(mode));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysNV(VkCommandBuffer commandBuffer, VkBuffer raygenShaderBindingTableBuffer, VkDeviceSize raygenShaderBindingOffset, VkBuffer missShaderBindingTableBuffer, VkDeviceSize missShaderBindingOffset, VkDeviceSize missShaderBindingStride, VkBuffer hitShaderBindingTableBuffer, VkDeviceSize hitShaderBindingOffset, VkDeviceSize hitShaderBindingStride, VkBuffer callableShaderBindingTableBuffer, VkDeviceSize callableShaderBindingOffset, VkDeviceSize callableShaderBindingStride, uint32_t width, uint32_t height, uint32_t depth)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdTraceRaysNV);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(raygenShaderBindingTableBuffer));
    mvk_put_u64(e, (uint64_t)(raygenShaderBindingOffset));
    mvk_put_handle(e, 0, (uint64_t)(missShaderBindingTableBuffer));
    mvk_put_u64(e, (uint64_t)(missShaderBindingOffset));
    mvk_put_u64(e, (uint64_t)(missShaderBindingStride));
    mvk_put_handle(e, 0, (uint64_t)(hitShaderBindingTableBuffer));
    mvk_put_u64(e, (uint64_t)(hitShaderBindingOffset));
    mvk_put_u64(e, (uint64_t)(hitShaderBindingStride));
    mvk_put_handle(e, 0, (uint64_t)(callableShaderBindingTableBuffer));
    mvk_put_u64(e, (uint64_t)(callableShaderBindingOffset));
    mvk_put_u64(e, (uint64_t)(callableShaderBindingStride));
    mvk_put_u32(e, (uint32_t)(width));
    mvk_put_u32(e, (uint32_t)(height));
    mvk_put_u32(e, (uint32_t)(depth));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesNV(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoNV * pCreateInfos, const VkAllocationCallbacks * pAllocator, VkPipeline * pPipelines)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateRayTracingPipelinesNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(pipelineCache));
    mvk_put_u32(e, (uint32_t)(createInfoCount));
    mvk_put_u32(e, pCreateInfos ? 1 : 0);
    if (pCreateInfos) {
        for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
            mvk_put_VkRayTracingPipelineCreateInfoNV(e, &pCreateInfos[i]);
    }
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++) {
        pPipelines[i] = (VkPipeline)mvk_new_id();
        mvk_put_u64(e, (uint64_t)pPipelines[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetAccelerationStructureHandleNV(VkDevice device, VkAccelerationStructureNV accelerationStructure, size_t dataSize, void * pData)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetAccelerationStructureHandleNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(accelerationStructure));
    mvk_put_u64(e, (uint64_t)(dataSize));
    mvk_put_u32(e, pData ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pData && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            mvk_get_bytes(d, pData, n);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteAccelerationStructuresPropertiesNV(VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount, const VkAccelerationStructureNV * pAccelerationStructures, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdWriteAccelerationStructuresPropertiesNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(accelerationStructureCount));
    mvk_put_u32(e, pAccelerationStructures ? 1 : 0);
    if (pAccelerationStructures) {
        for (uint32_t i = 0; i < (uint32_t)(accelerationStructureCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pAccelerationStructures[i]));
    }
    mvk_put_u32(e, (uint32_t)(queryType));
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(firstQuery));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCompileDeferredNV(VkDevice device, VkPipeline pipeline, uint32_t shader)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCompileDeferredNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(pipeline));
    mvk_put_u32(e, (uint32_t)(shader));
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteBufferMarkerAMD(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkBuffer dstBuffer, VkDeviceSize dstOffset, uint32_t marker)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdWriteBufferMarkerAMD);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(pipelineStage));
    mvk_put_handle(e, 0, (uint64_t)(dstBuffer));
    mvk_put_u64(e, (uint64_t)(dstOffset));
    mvk_put_u32(e, (uint32_t)(marker));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteBufferMarker2AMD(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkBuffer dstBuffer, VkDeviceSize dstOffset, uint32_t marker)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdWriteBufferMarker2AMD);
    if (!e)
        return;
    mvk_put_u64(e, (uint64_t)(stage));
    mvk_put_handle(e, 0, (uint64_t)(dstBuffer));
    mvk_put_u64(e, (uint64_t)(dstOffset));
    mvk_put_u32(e, (uint32_t)(marker));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksNV(VkCommandBuffer commandBuffer, uint32_t taskCount, uint32_t firstTask)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawMeshTasksNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(taskCount));
    mvk_put_u32(e, (uint32_t)(firstTask));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectNV(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawMeshTasksIndirectNV);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_u64(e, (uint64_t)(offset));
    mvk_put_u32(e, (uint32_t)(drawCount));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCountNV(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawMeshTasksIndirectCountNV);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_u64(e, (uint64_t)(offset));
    mvk_put_handle(e, 0, (uint64_t)(countBuffer));
    mvk_put_u64(e, (uint64_t)(countBufferOffset));
    mvk_put_u32(e, (uint32_t)(maxDrawCount));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetExclusiveScissorEnableNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor, uint32_t exclusiveScissorCount, const VkBool32 * pExclusiveScissorEnables)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetExclusiveScissorEnableNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstExclusiveScissor));
    mvk_put_u32(e, (uint32_t)(exclusiveScissorCount));
    mvk_put_u32(e, pExclusiveScissorEnables ? 1 : 0);
    if (pExclusiveScissorEnables) {
        mvk_put_bytes(e, pExclusiveScissorEnables, (size_t)(exclusiveScissorCount) * 4);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetExclusiveScissorNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor, uint32_t exclusiveScissorCount, const VkRect2D * pExclusiveScissors)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetExclusiveScissorNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstExclusiveScissor));
    mvk_put_u32(e, (uint32_t)(exclusiveScissorCount));
    mvk_put_u32(e, pExclusiveScissors ? 1 : 0);
    if (pExclusiveScissors) {
        for (uint32_t i = 0; i < (uint32_t)(exclusiveScissorCount); i++)
            mvk_put_VkRect2D(e, &pExclusiveScissors[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkUninitializePerformanceApiINTEL(VkDevice device)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkUninitializePerformanceApiINTEL);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_call_end(e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquirePerformanceConfigurationINTEL(VkDevice device, const VkPerformanceConfigurationAcquireInfoINTEL * pAcquireInfo, VkPerformanceConfigurationINTEL * pConfiguration)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkAcquirePerformanceConfigurationINTEL);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pAcquireInfo ? 1 : 0);
    if (pAcquireInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPerformanceConfigurationAcquireInfoINTEL(e, &pAcquireInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pConfiguration)
            *pConfiguration = (VkPerformanceConfigurationINTEL)mvk_get_u64(d);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkReleasePerformanceConfigurationINTEL(VkDevice device, VkPerformanceConfigurationINTEL configuration)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkReleasePerformanceConfigurationINTEL);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(configuration));
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSetPerformanceConfigurationINTEL(VkQueue queue, VkPerformanceConfigurationINTEL configuration)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkQueueSetPerformanceConfigurationINTEL);
    mvk_put_handle(e, 1, mvk_dispatch_id(queue));
    mvk_put_handle(e, 0, (uint64_t)(configuration));
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPerformanceParameterINTEL(VkDevice device, VkPerformanceParameterTypeINTEL parameter, VkPerformanceValueINTEL * pValue)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPerformanceParameterINTEL);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(parameter));
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pValue)
            mvk_out_VkPerformanceValueINTEL(d, pValue);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceFragmentShadingRatesKHR(VkPhysicalDevice physicalDevice, uint32_t * pFragmentShadingRateCount, VkPhysicalDeviceFragmentShadingRateKHR * pFragmentShadingRates)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceFragmentShadingRatesKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pFragmentShadingRateCount ? *pFragmentShadingRateCount : 0);
    mvk_put_u32(e, pFragmentShadingRates ? 1 : 0);
    if (pFragmentShadingRates)
        for (uint32_t i = 0; i < (uint32_t)(*pFragmentShadingRateCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pFragmentShadingRates[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pFragmentShadingRateCount)
            *pFragmentShadingRateCount = (uint32_t)mvk_get_u32(d);
        if (pFragmentShadingRates && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkPhysicalDeviceFragmentShadingRateKHR(d, &pFragmentShadingRates[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetFragmentShadingRateKHR(VkCommandBuffer commandBuffer, const VkExtent2D * pFragmentSize, const VkFragmentShadingRateCombinerOpKHR combinerOps[2])
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetFragmentShadingRateKHR);
    if (!e)
        return;
    mvk_put_u32(e, pFragmentSize ? 1 : 0);
    if (pFragmentSize) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkExtent2D(e, &pFragmentSize[i]);
    }
    mvk_put_bytes(e, combinerOps, (size_t)(2) * sizeof((combinerOps)[0]));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeMatrixPropertiesNV(VkPhysicalDevice physicalDevice, uint32_t * pPropertyCount, VkCooperativeMatrixPropertiesNV * pProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pPropertyCount ? *pPropertyCount : 0);
    mvk_put_u32(e, pProperties ? 1 : 0);
    if (pProperties)
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pProperties[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pPropertyCount)
            *pPropertyCount = (uint32_t)mvk_get_u32(d);
        if (pProperties && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkCooperativeMatrixPropertiesNV(d, &pProperties[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV(VkPhysicalDevice physicalDevice, uint32_t * pCombinationCount, VkFramebufferMixedSamplesCombinationNV * pCombinations)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pCombinationCount ? *pCombinationCount : 0);
    mvk_put_u32(e, pCombinations ? 1 : 0);
    if (pCombinations)
        for (uint32_t i = 0; i < (uint32_t)(*pCombinationCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pCombinations[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pCombinationCount)
            *pCombinationCount = (uint32_t)mvk_get_u32(d);
        if (pCombinations && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkFramebufferMixedSamplesCombinationNV(d, &pCombinations[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDeferredOperationKHR(VkDevice device, const VkAllocationCallbacks * pAllocator, VkDeferredOperationKHR * pDeferredOperation)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateDeferredOperationKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    *pDeferredOperation = (VkDeferredOperationKHR)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pDeferredOperation);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDeferredOperationKHR(VkDevice device, VkDeferredOperationKHR operation, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyDeferredOperationKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(operation));
    mvk_call_end(e);
    mvk_free_id((uint64_t)operation);
}

VKAPI_ATTR uint32_t VKAPI_CALL vkGetDeferredOperationMaxConcurrencyKHR(VkDevice device, VkDeferredOperationKHR operation)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeferredOperationMaxConcurrencyKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(operation));
    MvkDecoder *d = mvk_call_wait(e);
    uint32_t result = d ? (uint32_t)mvk_get_u32(d) : (uint32_t)0;
    if (d) {
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeferredOperationResultKHR(VkDevice device, VkDeferredOperationKHR operation)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeferredOperationResultKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(operation));
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeferredOperationJoinKHR(VkDevice device, VkDeferredOperationKHR operation)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDeferredOperationJoinKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(operation));
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutablePropertiesKHR(VkDevice device, const VkPipelineInfoKHR * pPipelineInfo, uint32_t * pExecutableCount, VkPipelineExecutablePropertiesKHR * pProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPipelineExecutablePropertiesKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pPipelineInfo ? 1 : 0);
    if (pPipelineInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPipelineInfoKHR(e, &pPipelineInfo[i]);
    }
    mvk_put_u32(e, pExecutableCount ? *pExecutableCount : 0);
    mvk_put_u32(e, pProperties ? 1 : 0);
    if (pProperties)
        for (uint32_t i = 0; i < (uint32_t)(*pExecutableCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pProperties[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pExecutableCount)
            *pExecutableCount = (uint32_t)mvk_get_u32(d);
        if (pProperties && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkPipelineExecutablePropertiesKHR(d, &pProperties[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutableStatisticsKHR(VkDevice device, const VkPipelineExecutableInfoKHR * pExecutableInfo, uint32_t * pStatisticCount, VkPipelineExecutableStatisticKHR * pStatistics)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPipelineExecutableStatisticsKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pExecutableInfo ? 1 : 0);
    if (pExecutableInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPipelineExecutableInfoKHR(e, &pExecutableInfo[i]);
    }
    mvk_put_u32(e, pStatisticCount ? *pStatisticCount : 0);
    mvk_put_u32(e, pStatistics ? 1 : 0);
    if (pStatistics)
        for (uint32_t i = 0; i < (uint32_t)(*pStatisticCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pStatistics[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pStatisticCount)
            *pStatisticCount = (uint32_t)mvk_get_u32(d);
        if (pStatistics && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkPipelineExecutableStatisticKHR(d, &pStatistics[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutableInternalRepresentationsKHR(VkDevice device, const VkPipelineExecutableInfoKHR * pExecutableInfo, uint32_t * pInternalRepresentationCount, VkPipelineExecutableInternalRepresentationKHR * pInternalRepresentations)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPipelineExecutableInternalRepresentationsKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pExecutableInfo ? 1 : 0);
    if (pExecutableInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPipelineExecutableInfoKHR(e, &pExecutableInfo[i]);
    }
    mvk_put_u32(e, pInternalRepresentationCount ? *pInternalRepresentationCount : 0);
    mvk_put_u32(e, pInternalRepresentations ? 1 : 0);
    if (pInternalRepresentations)
        for (uint32_t i = 0; i < (uint32_t)(*pInternalRepresentationCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pInternalRepresentations[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pInternalRepresentationCount)
            *pInternalRepresentationCount = (uint32_t)mvk_get_u32(d);
        if (pInternalRepresentations && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkPipelineExecutableInternalRepresentationKHR(d, &pInternalRepresentations[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkGetGeneratedCommandsMemoryRequirementsNV(VkDevice device, const VkGeneratedCommandsMemoryRequirementsInfoNV * pInfo, VkMemoryRequirements2 * pMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetGeneratedCommandsMemoryRequirementsNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkGeneratedCommandsMemoryRequirementsInfoNV(e, &pInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pMemoryRequirements);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMemoryRequirements)
            mvk_out_VkMemoryRequirements2(d, pMemoryRequirements);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPreprocessGeneratedCommandsNV(VkCommandBuffer commandBuffer, const VkGeneratedCommandsInfoNV * pGeneratedCommandsInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdPreprocessGeneratedCommandsNV);
    if (!e)
        return;
    mvk_put_u32(e, pGeneratedCommandsInfo ? 1 : 0);
    if (pGeneratedCommandsInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkGeneratedCommandsInfoNV(e, &pGeneratedCommandsInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdExecuteGeneratedCommandsNV(VkCommandBuffer commandBuffer, VkBool32 isPreprocessed, const VkGeneratedCommandsInfoNV * pGeneratedCommandsInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdExecuteGeneratedCommandsNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(isPreprocessed));
    mvk_put_u32(e, pGeneratedCommandsInfo ? 1 : 0);
    if (pGeneratedCommandsInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkGeneratedCommandsInfoNV(e, &pGeneratedCommandsInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipelineShaderGroupNV(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline, uint32_t groupIndex)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindPipelineShaderGroupNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(pipelineBindPoint));
    mvk_put_handle(e, 0, (uint64_t)(pipeline));
    mvk_put_u32(e, (uint32_t)(groupIndex));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectCommandsLayoutNV(VkDevice device, const VkIndirectCommandsLayoutCreateInfoNV * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkIndirectCommandsLayoutNV * pIndirectCommandsLayout)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateIndirectCommandsLayoutNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkIndirectCommandsLayoutCreateInfoNV(e, &pCreateInfo[i]);
    }
    *pIndirectCommandsLayout = (VkIndirectCommandsLayoutNV)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pIndirectCommandsLayout);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectCommandsLayoutNV(VkDevice device, VkIndirectCommandsLayoutNV indirectCommandsLayout, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyIndirectCommandsLayoutNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(indirectCommandsLayout));
    mvk_call_end(e);
    mvk_free_id((uint64_t)indirectCommandsLayout);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias2EXT(VkCommandBuffer commandBuffer, const VkDepthBiasInfoEXT * pDepthBiasInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDepthBias2EXT);
    if (!e)
        return;
    mvk_put_u32(e, pDepthBiasInfo ? 1 : 0);
    if (pDepthBiasInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDepthBiasInfoEXT(e, &pDepthBiasInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSetPerfHintQCOM(VkQueue queue, const VkPerfHintInfoQCOM * pPerfHintInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkQueueSetPerfHintQCOM);
    mvk_put_handle(e, 1, mvk_dispatch_id(queue));
    mvk_put_u32(e, pPerfHintInfo ? 1 : 0);
    if (pPerfHintInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPerfHintInfoQCOM(e, &pPerfHintInfo[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkSetLatencySleepModeLegacyNV(VkDevice device, VkBool32 lowLatencyMode, VkBool32 lowLatencyBoost, uint32_t minimumIntervalUs)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkSetLatencySleepModeLegacyNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(lowLatencyMode));
    mvk_put_u32(e, (uint32_t)(lowLatencyBoost));
    mvk_put_u32(e, (uint32_t)(minimumIntervalUs));
    mvk_call_end(e);
}

VKAPI_ATTR void VKAPI_CALL vkLatencySleepLegacyNV(VkDevice device, VkSemaphore signalSemaphore, uint64_t value)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkLatencySleepLegacyNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(signalSemaphore));
    mvk_put_u64(e, (uint64_t)(value));
    mvk_call_end(e);
}

VKAPI_ATTR void VKAPI_CALL vkSetLatencyMarkerLegacyNV(VkDevice device, uint64_t frameID, uint32_t marker)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkSetLatencyMarkerLegacyNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u64(e, (uint64_t)(frameID));
    mvk_put_u32(e, (uint32_t)(marker));
    mvk_call_end(e);
}

VKAPI_ATTR void VKAPI_CALL vkQueueNotifyOutOfBandLegacyNV(VkQueue queue, uint32_t queueType)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkQueueNotifyOutOfBandLegacyNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(queue));
    mvk_put_u32(e, (uint32_t)(queueType));
    mvk_call_end(e);
}

VKAPI_ATTR void VKAPI_CALL vkGetSleepStatusLegacyNV(VkDevice device, VkBool32 * pLowLatencyMode)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetSleepStatusLegacyNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pLowLatencyMode)
            *pLowLatencyMode = (VkBool32)mvk_get_u32(d);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkShutdownLatencyDeviceLegacyNV(VkDevice device)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkShutdownLatencyDeviceLegacyNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_call_end(e);
}

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSizeEXT(VkDevice device, VkDescriptorSetLayout layout, VkDeviceSize * pLayoutSizeInBytes)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDescriptorSetLayoutSizeEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(layout));
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pLayoutSizeInBytes)
            *pLayoutSizeInBytes = (VkDeviceSize)mvk_get_u64(d);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutBindingOffsetEXT(VkDevice device, VkDescriptorSetLayout layout, uint32_t binding, VkDeviceSize * pOffset)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDescriptorSetLayoutBindingOffsetEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(layout));
    mvk_put_u32(e, (uint32_t)(binding));
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pOffset)
            *pOffset = (VkDeviceSize)mvk_get_u64(d);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorBuffersEXT(VkCommandBuffer commandBuffer, uint32_t bufferCount, const VkDescriptorBufferBindingInfoEXT * pBindingInfos)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindDescriptorBuffersEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(bufferCount));
    mvk_put_u32(e, pBindingInfos ? 1 : 0);
    if (pBindingInfos) {
        for (uint32_t i = 0; i < (uint32_t)(bufferCount); i++)
            mvk_put_VkDescriptorBufferBindingInfoEXT(e, &pBindingInfos[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDescriptorBufferOffsetsEXT(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t setCount, const uint32_t * pBufferIndices, const VkDeviceSize * pOffsets)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDescriptorBufferOffsetsEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(pipelineBindPoint));
    mvk_put_handle(e, 0, (uint64_t)(layout));
    mvk_put_u32(e, (uint32_t)(firstSet));
    mvk_put_u32(e, (uint32_t)(setCount));
    mvk_put_u32(e, pBufferIndices ? 1 : 0);
    if (pBufferIndices) {
        mvk_put_bytes(e, pBufferIndices, (size_t)(setCount) * 4);
    }
    mvk_put_u32(e, pOffsets ? 1 : 0);
    if (pOffsets) {
        mvk_put_bytes(e, pOffsets, (size_t)(setCount) * 8);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorBufferEmbeddedSamplersEXT(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindDescriptorBufferEmbeddedSamplersEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(pipelineBindPoint));
    mvk_put_handle(e, 0, (uint64_t)(layout));
    mvk_put_u32(e, (uint32_t)(set));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer3KHR(VkCommandBuffer commandBuffer, const VkBindIndexBuffer3InfoKHR * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindIndexBuffer3KHR);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBindIndexBuffer3InfoKHR(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers3KHR(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBindVertexBuffer3InfoKHR * pBindingInfos)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindVertexBuffers3KHR);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstBinding));
    mvk_put_u32(e, (uint32_t)(bindingCount));
    mvk_put_u32(e, pBindingInfos ? 1 : 0);
    if (pBindingInfos) {
        for (uint32_t i = 0; i < (uint32_t)(bindingCount); i++)
            mvk_put_VkBindVertexBuffer3InfoKHR(e, &pBindingInfos[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect2KHR(VkCommandBuffer commandBuffer, const VkDrawIndirect2InfoKHR * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawIndirect2KHR);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDrawIndirect2InfoKHR(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect2KHR(VkCommandBuffer commandBuffer, const VkDrawIndirect2InfoKHR * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawIndexedIndirect2KHR);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDrawIndirect2InfoKHR(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect2KHR(VkCommandBuffer commandBuffer, const VkDispatchIndirect2InfoKHR * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDispatchIndirect2KHR);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDispatchIndirect2InfoKHR(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryKHR(VkCommandBuffer commandBuffer, const VkCopyDeviceMemoryInfoKHR * pCopyMemoryInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyMemoryKHR);
    if (!e)
        return;
    mvk_put_u32(e, pCopyMemoryInfo ? 1 : 0);
    if (pCopyMemoryInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyDeviceMemoryInfoKHR(e, &pCopyMemoryInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToImageKHR(VkCommandBuffer commandBuffer, const VkCopyDeviceMemoryImageInfoKHR * pCopyMemoryInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyMemoryToImageKHR);
    if (!e)
        return;
    mvk_put_u32(e, pCopyMemoryInfo ? 1 : 0);
    if (pCopyMemoryInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyDeviceMemoryImageInfoKHR(e, &pCopyMemoryInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToMemoryKHR(VkCommandBuffer commandBuffer, const VkCopyDeviceMemoryImageInfoKHR * pCopyMemoryInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyImageToMemoryKHR);
    if (!e)
        return;
    mvk_put_u32(e, pCopyMemoryInfo ? 1 : 0);
    if (pCopyMemoryInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyDeviceMemoryImageInfoKHR(e, &pCopyMemoryInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdUpdateMemoryKHR(VkCommandBuffer commandBuffer, const VkDeviceAddressRangeKHR * pDstRange, VkAddressCommandFlagsKHR dstFlags, VkDeviceSize dataSize, const void * pData)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdUpdateMemoryKHR);
    if (!e)
        return;
    mvk_put_u32(e, pDstRange ? 1 : 0);
    if (pDstRange) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDeviceAddressRangeKHR(e, &pDstRange[i]);
    }
    mvk_put_u32(e, (uint32_t)(dstFlags));
    mvk_put_u64(e, (uint64_t)(dataSize));
    mvk_put_blob(e, pData, dataSize);
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdFillMemoryKHR(VkCommandBuffer commandBuffer, const VkDeviceAddressRangeKHR * pDstRange, VkAddressCommandFlagsKHR dstFlags, uint32_t data)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdFillMemoryKHR);
    if (!e)
        return;
    mvk_put_u32(e, pDstRange ? 1 : 0);
    if (pDstRange) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDeviceAddressRangeKHR(e, &pDstRange[i]);
    }
    mvk_put_u32(e, (uint32_t)(dstFlags));
    mvk_put_u32(e, (uint32_t)(data));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResultsToMemoryKHR(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, const VkStridedDeviceAddressRangeKHR * pDstRange, VkAddressCommandFlagsKHR dstFlags, VkQueryResultFlags queryResultFlags)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyQueryPoolResultsToMemoryKHR);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(firstQuery));
    mvk_put_u32(e, (uint32_t)(queryCount));
    mvk_put_u32(e, pDstRange ? 1 : 0);
    if (pDstRange) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkStridedDeviceAddressRangeKHR(e, &pDstRange[i]);
    }
    mvk_put_u32(e, (uint32_t)(dstFlags));
    mvk_put_u32(e, (uint32_t)(queryResultFlags));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCount2KHR(VkCommandBuffer commandBuffer, const VkDrawIndirectCount2InfoKHR * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawIndirectCount2KHR);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDrawIndirectCount2InfoKHR(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCount2KHR(VkCommandBuffer commandBuffer, const VkDrawIndirectCount2InfoKHR * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawIndexedIndirectCount2KHR);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDrawIndirectCount2InfoKHR(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginConditionalRendering2EXT(VkCommandBuffer commandBuffer, const VkConditionalRenderingBeginInfo2EXT * pConditionalRenderingBegin)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBeginConditionalRendering2EXT);
    if (!e)
        return;
    mvk_put_u32(e, pConditionalRenderingBegin ? 1 : 0);
    if (pConditionalRenderingBegin) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkConditionalRenderingBeginInfo2EXT(e, &pConditionalRenderingBegin[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindTransformFeedbackBuffers2EXT(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBindTransformFeedbackBuffer2InfoEXT * pBindingInfos)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindTransformFeedbackBuffers2EXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstBinding));
    mvk_put_u32(e, (uint32_t)(bindingCount));
    mvk_put_u32(e, pBindingInfos ? 1 : 0);
    if (pBindingInfos) {
        for (uint32_t i = 0; i < (uint32_t)(bindingCount); i++)
            mvk_put_VkBindTransformFeedbackBuffer2InfoEXT(e, &pBindingInfos[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginTransformFeedback2EXT(VkCommandBuffer commandBuffer, uint32_t firstCounterRange, uint32_t counterRangeCount, const VkBindTransformFeedbackBuffer2InfoEXT * pCounterInfos)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBeginTransformFeedback2EXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstCounterRange));
    mvk_put_u32(e, (uint32_t)(counterRangeCount));
    mvk_put_u32(e, pCounterInfos ? 1 : 0);
    if (pCounterInfos) {
        for (uint32_t i = 0; i < (uint32_t)(counterRangeCount); i++)
            mvk_put_VkBindTransformFeedbackBuffer2InfoEXT(e, &pCounterInfos[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndTransformFeedback2EXT(VkCommandBuffer commandBuffer, uint32_t firstCounterRange, uint32_t counterRangeCount, const VkBindTransformFeedbackBuffer2InfoEXT * pCounterInfos)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdEndTransformFeedback2EXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstCounterRange));
    mvk_put_u32(e, (uint32_t)(counterRangeCount));
    mvk_put_u32(e, pCounterInfos ? 1 : 0);
    if (pCounterInfos) {
        for (uint32_t i = 0; i < (uint32_t)(counterRangeCount); i++)
            mvk_put_VkBindTransformFeedbackBuffer2InfoEXT(e, &pCounterInfos[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectByteCount2EXT(VkCommandBuffer commandBuffer, uint32_t instanceCount, uint32_t firstInstance, const VkBindTransformFeedbackBuffer2InfoEXT * pCounterInfo, uint32_t counterOffset, uint32_t vertexStride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawIndirectByteCount2EXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(instanceCount));
    mvk_put_u32(e, (uint32_t)(firstInstance));
    mvk_put_u32(e, pCounterInfo ? 1 : 0);
    if (pCounterInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBindTransformFeedbackBuffer2InfoEXT(e, &pCounterInfo[i]);
    }
    mvk_put_u32(e, (uint32_t)(counterOffset));
    mvk_put_u32(e, (uint32_t)(vertexStride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirect2EXT(VkCommandBuffer commandBuffer, const VkDrawIndirect2InfoKHR * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawMeshTasksIndirect2EXT);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDrawIndirect2InfoKHR(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCount2EXT(VkCommandBuffer commandBuffer, const VkDrawIndirectCount2InfoKHR * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawMeshTasksIndirectCount2EXT);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDrawIndirectCount2InfoKHR(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteMarkerToMemoryAMD(VkCommandBuffer commandBuffer, const VkMemoryMarkerInfoAMD * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdWriteMarkerToMemoryAMD);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkMemoryMarkerInfoAMD(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructure2KHR(VkDevice device, const VkAccelerationStructureCreateInfo2KHR * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkAccelerationStructureKHR * pAccelerationStructure)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateAccelerationStructure2KHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkAccelerationStructureCreateInfo2KHR(e, &pCreateInfo[i]);
    }
    *pAccelerationStructure = (VkAccelerationStructureKHR)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pAccelerationStructure);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetFragmentShadingRateEnumNV(VkCommandBuffer commandBuffer, VkFragmentShadingRateNV shadingRate, const VkFragmentShadingRateCombinerOpKHR combinerOps[2])
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetFragmentShadingRateEnumNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(shadingRate));
    mvk_put_bytes(e, combinerOps, (size_t)(2) * sizeof((combinerOps)[0]));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksEXT(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawMeshTasksEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(groupCountX));
    mvk_put_u32(e, (uint32_t)(groupCountY));
    mvk_put_u32(e, (uint32_t)(groupCountZ));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectEXT(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawMeshTasksIndirectEXT);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_u64(e, (uint64_t)(offset));
    mvk_put_u32(e, (uint32_t)(drawCount));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCountEXT(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawMeshTasksIndirectCountEXT);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_u64(e, (uint64_t)(offset));
    mvk_put_handle(e, 0, (uint64_t)(countBuffer));
    mvk_put_u64(e, (uint64_t)(countBufferOffset));
    mvk_put_u32(e, (uint32_t)(maxDrawCount));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetVertexInputEXT(VkCommandBuffer commandBuffer, uint32_t vertexBindingDescriptionCount, const VkVertexInputBindingDescription2EXT * pVertexBindingDescriptions, uint32_t vertexAttributeDescriptionCount, const VkVertexInputAttributeDescription2EXT * pVertexAttributeDescriptions)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetVertexInputEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(vertexBindingDescriptionCount));
    mvk_put_u32(e, pVertexBindingDescriptions ? 1 : 0);
    if (pVertexBindingDescriptions) {
        for (uint32_t i = 0; i < (uint32_t)(vertexBindingDescriptionCount); i++)
            mvk_put_VkVertexInputBindingDescription2EXT(e, &pVertexBindingDescriptions[i]);
    }
    mvk_put_u32(e, (uint32_t)(vertexAttributeDescriptionCount));
    mvk_put_u32(e, pVertexAttributeDescriptions ? 1 : 0);
    if (pVertexAttributeDescriptions) {
        for (uint32_t i = 0; i < (uint32_t)(vertexAttributeDescriptionCount); i++)
            mvk_put_VkVertexInputAttributeDescription2EXT(e, &pVertexAttributeDescriptions[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI(VkDevice device, VkRenderPass renderpass, VkExtent2D * pMaxWorkgroupSize)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(renderpass));
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pMaxWorkgroupSize)
            mvk_out_VkExtent2D(d, pMaxWorkgroupSize);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSubpassShadingHUAWEI(VkCommandBuffer commandBuffer)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSubpassShadingHUAWEI);
    if (!e)
        return;
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindInvocationMaskHUAWEI(VkCommandBuffer commandBuffer, VkImageView imageView, VkImageLayout imageLayout)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindInvocationMaskHUAWEI);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(imageView));
    mvk_put_u32(e, (uint32_t)(imageLayout));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelinePropertiesEXT(VkDevice device, const VkPipelineInfoKHR * pPipelineInfo, VkBaseOutStructure * pPipelineProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPipelinePropertiesEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pPipelineInfo ? 1 : 0);
    if (pPipelineInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPipelineInfoKHR(e, &pPipelineInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pPipelineProperties)
            mvk_out_VkBaseOutStructure(d, pPipelineProperties);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPatchControlPointsEXT(VkCommandBuffer commandBuffer, uint32_t patchControlPoints)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetPatchControlPointsEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(patchControlPoints));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLogicOpEXT(VkCommandBuffer commandBuffer, VkLogicOp logicOp)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetLogicOpEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(logicOp));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorWriteEnableEXT(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkBool32 * pColorWriteEnables)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetColorWriteEnableEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(attachmentCount));
    mvk_put_u32(e, pColorWriteEnables ? 1 : 0);
    if (pColorWriteEnables) {
        mvk_put_bytes(e, pColorWriteEnables, (size_t)(attachmentCount) * 4);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysIndirect2KHR(VkCommandBuffer commandBuffer, VkDeviceAddress indirectDeviceAddress)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdTraceRaysIndirect2KHR);
    if (!e)
        return;
    mvk_put_u64(e, (uint64_t)(indirectDeviceAddress));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMultiEXT(VkCommandBuffer commandBuffer, uint32_t drawCount, const VkMultiDrawInfoEXT * pVertexInfo, uint32_t instanceCount, uint32_t firstInstance, uint32_t stride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawMultiEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(drawCount));
    mvk_put_u32(e, pVertexInfo ? 1 : 0);
    if (pVertexInfo) {
        for (uint32_t i = 0; i < (uint32_t)(drawCount); i++)
            mvk_put_VkMultiDrawInfoEXT(e, &pVertexInfo[i]);
    }
    mvk_put_u32(e, (uint32_t)(instanceCount));
    mvk_put_u32(e, (uint32_t)(firstInstance));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMultiIndexedEXT(VkCommandBuffer commandBuffer, uint32_t drawCount, const VkMultiDrawIndexedInfoEXT * pIndexInfo, uint32_t instanceCount, uint32_t firstInstance, uint32_t stride, const int32_t * pVertexOffset)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawMultiIndexedEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(drawCount));
    mvk_put_u32(e, pIndexInfo ? 1 : 0);
    if (pIndexInfo) {
        for (uint32_t i = 0; i < (uint32_t)(drawCount); i++)
            mvk_put_VkMultiDrawIndexedInfoEXT(e, &pIndexInfo[i]);
    }
    mvk_put_u32(e, (uint32_t)(instanceCount));
    mvk_put_u32(e, (uint32_t)(firstInstance));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_put_u32(e, pVertexOffset ? 1 : 0);
    if (pVertexOffset) {
        mvk_put_bytes(e, pVertexOffset, (size_t)(1) * 4);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateMicromapEXT(VkDevice device, const VkMicromapCreateInfoEXT * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkMicromapEXT * pMicromap)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateMicromapEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkMicromapCreateInfoEXT(e, &pCreateInfo[i]);
    }
    *pMicromap = (VkMicromapEXT)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pMicromap);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyMicromapEXT(VkDevice device, VkMicromapEXT micromap, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyMicromapEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(micromap));
    mvk_call_end(e);
    mvk_free_id((uint64_t)micromap);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBuildMicromapsEXT(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkMicromapBuildInfoEXT * pInfos)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBuildMicromapsEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(infoCount));
    mvk_put_u32(e, pInfos ? 1 : 0);
    if (pInfos) {
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_put_VkMicromapBuildInfoEXT(e, &pInfos[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBuildMicromapsEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount, const VkMicromapBuildInfoEXT * pInfos)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkBuildMicromapsEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(deferredOperation));
    mvk_put_u32(e, (uint32_t)(infoCount));
    mvk_put_u32(e, pInfos ? 1 : 0);
    if (pInfos) {
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_put_VkMicromapBuildInfoEXT(e, &pInfos[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMicromapEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMicromapInfoEXT * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCopyMicromapEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(deferredOperation));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyMicromapInfoEXT(e, &pInfo[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMicromapToMemoryEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMicromapToMemoryInfoEXT * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCopyMicromapToMemoryEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(deferredOperation));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyMicromapToMemoryInfoEXT(e, &pInfo[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToMicromapEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMemoryToMicromapInfoEXT * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCopyMemoryToMicromapEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(deferredOperation));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyMemoryToMicromapInfoEXT(e, &pInfo[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWriteMicromapsPropertiesEXT(VkDevice device, uint32_t micromapCount, const VkMicromapEXT * pMicromaps, VkQueryType queryType, size_t dataSize, void * pData, size_t stride)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkWriteMicromapsPropertiesEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(micromapCount));
    mvk_put_u32(e, pMicromaps ? 1 : 0);
    if (pMicromaps) {
        for (uint32_t i = 0; i < (uint32_t)(micromapCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pMicromaps[i]));
    }
    mvk_put_u32(e, (uint32_t)(queryType));
    mvk_put_u64(e, (uint64_t)(dataSize));
    mvk_put_u32(e, pData ? 1 : 0);
    mvk_put_u64(e, (uint64_t)(stride));
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pData && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            mvk_get_bytes(d, pData, n);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMicromapEXT(VkCommandBuffer commandBuffer, const VkCopyMicromapInfoEXT * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyMicromapEXT);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyMicromapInfoEXT(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMicromapToMemoryEXT(VkCommandBuffer commandBuffer, const VkCopyMicromapToMemoryInfoEXT * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyMicromapToMemoryEXT);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyMicromapToMemoryInfoEXT(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToMicromapEXT(VkCommandBuffer commandBuffer, const VkCopyMemoryToMicromapInfoEXT * pInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyMemoryToMicromapEXT);
    if (!e)
        return;
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyMemoryToMicromapInfoEXT(e, &pInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteMicromapsPropertiesEXT(VkCommandBuffer commandBuffer, uint32_t micromapCount, const VkMicromapEXT * pMicromaps, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdWriteMicromapsPropertiesEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(micromapCount));
    mvk_put_u32(e, pMicromaps ? 1 : 0);
    if (pMicromaps) {
        for (uint32_t i = 0; i < (uint32_t)(micromapCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pMicromaps[i]));
    }
    mvk_put_u32(e, (uint32_t)(queryType));
    mvk_put_handle(e, 0, (uint64_t)(queryPool));
    mvk_put_u32(e, (uint32_t)(firstQuery));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMicromapCompatibilityEXT(VkDevice device, const VkMicromapVersionInfoEXT * pVersionInfo, VkAccelerationStructureCompatibilityKHR * pCompatibility)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeviceMicromapCompatibilityEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pVersionInfo ? 1 : 0);
    if (pVersionInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkMicromapVersionInfoEXT(e, &pVersionInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pCompatibility)
            *pCompatibility = (VkAccelerationStructureCompatibilityKHR)mvk_get_u32(d);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetMicromapBuildSizesEXT(VkDevice device, VkAccelerationStructureBuildTypeKHR buildType, const VkMicromapBuildInfoEXT * pBuildInfo, VkMicromapBuildSizesInfoEXT * pSizeInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetMicromapBuildSizesEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(buildType));
    mvk_put_u32(e, pBuildInfo ? 1 : 0);
    if (pBuildInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkMicromapBuildInfoEXT(e, &pBuildInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pSizeInfo);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pSizeInfo)
            mvk_out_VkMicromapBuildSizesInfoEXT(d, pSizeInfo);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawClusterHUAWEI(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawClusterHUAWEI);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(groupCountX));
    mvk_put_u32(e, (uint32_t)(groupCountY));
    mvk_put_u32(e, (uint32_t)(groupCountZ));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawClusterIndirectHUAWEI(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDrawClusterIndirectHUAWEI);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(buffer));
    mvk_put_u64(e, (uint64_t)(offset));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkSetDeviceMemoryPriorityEXT(VkDevice device, VkDeviceMemory memory, float priority)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkSetDeviceMemoryPriorityEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(memory));
    mvk_put_u32(e, mvk_f32_bits(priority));
    mvk_call_end(e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDispatchParametersARM(VkCommandBuffer commandBuffer, const VkDispatchParametersARM * pDispatchParameters)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDispatchParametersARM);
    if (!e)
        return;
    mvk_put_u32(e, pDispatchParameters ? 1 : 0);
    if (pDispatchParameters) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDispatchParametersARM(e, &pDispatchParameters[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutHostMappingInfoVALVE(VkDevice device, const VkDescriptorSetBindingReferenceVALVE * pBindingReference, VkDescriptorSetLayoutHostMappingInfoVALVE * pHostMapping)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDescriptorSetLayoutHostMappingInfoVALVE);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pBindingReference ? 1 : 0);
    if (pBindingReference) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDescriptorSetBindingReferenceVALVE(e, &pBindingReference[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pHostMapping);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pHostMapping)
            mvk_out_VkDescriptorSetLayoutHostMappingInfoVALVE(d, pHostMapping);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryIndirectNV(VkCommandBuffer commandBuffer, VkDeviceAddress copyBufferAddress, uint32_t copyCount, uint32_t stride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyMemoryIndirectNV);
    if (!e)
        return;
    mvk_put_u64(e, (uint64_t)(copyBufferAddress));
    mvk_put_u32(e, (uint32_t)(copyCount));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToImageIndirectNV(VkCommandBuffer commandBuffer, VkDeviceAddress copyBufferAddress, uint32_t copyCount, uint32_t stride, VkImage dstImage, VkImageLayout dstImageLayout, const VkImageSubresourceLayers * pImageSubresources)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyMemoryToImageIndirectNV);
    if (!e)
        return;
    mvk_put_u64(e, (uint64_t)(copyBufferAddress));
    mvk_put_u32(e, (uint32_t)(copyCount));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_put_handle(e, 0, (uint64_t)(dstImage));
    mvk_put_u32(e, (uint32_t)(dstImageLayout));
    mvk_put_u32(e, pImageSubresources ? 1 : 0);
    if (pImageSubresources) {
        for (uint32_t i = 0; i < (uint32_t)(copyCount); i++)
            mvk_put_VkImageSubresourceLayers(e, &pImageSubresources[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryNV(VkCommandBuffer commandBuffer, uint32_t decompressRegionCount, const VkDecompressMemoryRegionNV * pDecompressMemoryRegions)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDecompressMemoryNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(decompressRegionCount));
    mvk_put_u32(e, pDecompressMemoryRegions ? 1 : 0);
    if (pDecompressMemoryRegions) {
        for (uint32_t i = 0; i < (uint32_t)(decompressRegionCount); i++)
            mvk_put_VkDecompressMemoryRegionNV(e, &pDecompressMemoryRegions[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryIndirectCountNV(VkCommandBuffer commandBuffer, VkDeviceAddress indirectCommandsAddress, VkDeviceAddress indirectCommandsCountAddress, uint32_t stride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDecompressMemoryIndirectCountNV);
    if (!e)
        return;
    mvk_put_u64(e, (uint64_t)(indirectCommandsAddress));
    mvk_put_u64(e, (uint64_t)(indirectCommandsCountAddress));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetPipelineIndirectMemoryRequirementsNV(VkDevice device, const VkComputePipelineCreateInfo * pCreateInfo, VkMemoryRequirements2 * pMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPipelineIndirectMemoryRequirementsNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkComputePipelineCreateInfo(e, &pCreateInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pMemoryRequirements);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMemoryRequirements)
            mvk_out_VkMemoryRequirements2(d, pMemoryRequirements);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdUpdatePipelineIndirectBufferNV(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdUpdatePipelineIndirectBufferNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(pipelineBindPoint));
    mvk_put_handle(e, 0, (uint64_t)(pipeline));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetPipelineIndirectDeviceAddressNV(VkDevice device, const VkPipelineIndirectDeviceAddressInfoNV * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPipelineIndirectDeviceAddressNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPipelineIndirectDeviceAddressInfoNV(e, &pInfo[i]);
    }
    MvkDecoder *d = mvk_call_wait(e);
    VkDeviceAddress result = d ? (VkDeviceAddress)mvk_get_u64(d) : (VkDeviceAddress)0;
    if (d) {
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClampEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthClampEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDepthClampEnableEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(depthClampEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPolygonModeEXT(VkCommandBuffer commandBuffer, VkPolygonMode polygonMode)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetPolygonModeEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(polygonMode));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizationSamplesEXT(VkCommandBuffer commandBuffer, VkSampleCountFlagBits rasterizationSamples)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetRasterizationSamplesEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(rasterizationSamples));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleMaskEXT(VkCommandBuffer commandBuffer, VkSampleCountFlagBits samples, const VkSampleMask * pSampleMask)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetSampleMaskEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(samples));
    mvk_put_u32(e, pSampleMask ? 1 : 0);
    if (pSampleMask) {
        mvk_put_bytes(e, pSampleMask, (size_t)((samples + 31) / 32) * 4);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetAlphaToCoverageEnableEXT(VkCommandBuffer commandBuffer, VkBool32 alphaToCoverageEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetAlphaToCoverageEnableEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(alphaToCoverageEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetAlphaToOneEnableEXT(VkCommandBuffer commandBuffer, VkBool32 alphaToOneEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetAlphaToOneEnableEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(alphaToOneEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLogicOpEnableEXT(VkCommandBuffer commandBuffer, VkBool32 logicOpEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetLogicOpEnableEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(logicOpEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorBlendEnableEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment, uint32_t attachmentCount, const VkBool32 * pColorBlendEnables)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetColorBlendEnableEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstAttachment));
    mvk_put_u32(e, (uint32_t)(attachmentCount));
    mvk_put_u32(e, pColorBlendEnables ? 1 : 0);
    if (pColorBlendEnables) {
        mvk_put_bytes(e, pColorBlendEnables, (size_t)(attachmentCount) * 4);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorBlendEquationEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment, uint32_t attachmentCount, const VkColorBlendEquationEXT * pColorBlendEquations)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetColorBlendEquationEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstAttachment));
    mvk_put_u32(e, (uint32_t)(attachmentCount));
    mvk_put_u32(e, pColorBlendEquations ? 1 : 0);
    if (pColorBlendEquations) {
        for (uint32_t i = 0; i < (uint32_t)(attachmentCount); i++)
            mvk_put_VkColorBlendEquationEXT(e, &pColorBlendEquations[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorWriteMaskEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment, uint32_t attachmentCount, const VkColorComponentFlags * pColorWriteMasks)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetColorWriteMaskEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstAttachment));
    mvk_put_u32(e, (uint32_t)(attachmentCount));
    mvk_put_u32(e, pColorWriteMasks ? 1 : 0);
    if (pColorWriteMasks) {
        mvk_put_bytes(e, pColorWriteMasks, (size_t)(attachmentCount) * 4);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetTessellationDomainOriginEXT(VkCommandBuffer commandBuffer, VkTessellationDomainOrigin domainOrigin)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetTessellationDomainOriginEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(domainOrigin));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizationStreamEXT(VkCommandBuffer commandBuffer, uint32_t rasterizationStream)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetRasterizationStreamEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(rasterizationStream));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetConservativeRasterizationModeEXT(VkCommandBuffer commandBuffer, VkConservativeRasterizationModeEXT conservativeRasterizationMode)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetConservativeRasterizationModeEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(conservativeRasterizationMode));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetExtraPrimitiveOverestimationSizeEXT(VkCommandBuffer commandBuffer, float extraPrimitiveOverestimationSize)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetExtraPrimitiveOverestimationSizeEXT);
    if (!e)
        return;
    mvk_put_u32(e, mvk_f32_bits(extraPrimitiveOverestimationSize));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClipEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthClipEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDepthClipEnableEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(depthClipEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleLocationsEnableEXT(VkCommandBuffer commandBuffer, VkBool32 sampleLocationsEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetSampleLocationsEnableEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(sampleLocationsEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorBlendAdvancedEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment, uint32_t attachmentCount, const VkColorBlendAdvancedEXT * pColorBlendAdvanced)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetColorBlendAdvancedEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstAttachment));
    mvk_put_u32(e, (uint32_t)(attachmentCount));
    mvk_put_u32(e, pColorBlendAdvanced ? 1 : 0);
    if (pColorBlendAdvanced) {
        for (uint32_t i = 0; i < (uint32_t)(attachmentCount); i++)
            mvk_put_VkColorBlendAdvancedEXT(e, &pColorBlendAdvanced[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetProvokingVertexModeEXT(VkCommandBuffer commandBuffer, VkProvokingVertexModeEXT provokingVertexMode)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetProvokingVertexModeEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(provokingVertexMode));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineRasterizationModeEXT(VkCommandBuffer commandBuffer, VkLineRasterizationModeEXT lineRasterizationMode)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetLineRasterizationModeEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(lineRasterizationMode));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStippleEnableEXT(VkCommandBuffer commandBuffer, VkBool32 stippledLineEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetLineStippleEnableEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(stippledLineEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClipNegativeOneToOneEXT(VkCommandBuffer commandBuffer, VkBool32 negativeOneToOne)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDepthClipNegativeOneToOneEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(negativeOneToOne));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWScalingEnableNV(VkCommandBuffer commandBuffer, VkBool32 viewportWScalingEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetViewportWScalingEnableNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(viewportWScalingEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportSwizzleNV(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewportSwizzleNV * pViewportSwizzles)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetViewportSwizzleNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(firstViewport));
    mvk_put_u32(e, (uint32_t)(viewportCount));
    mvk_put_u32(e, pViewportSwizzles ? 1 : 0);
    if (pViewportSwizzles) {
        for (uint32_t i = 0; i < (uint32_t)(viewportCount); i++)
            mvk_put_VkViewportSwizzleNV(e, &pViewportSwizzles[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageToColorEnableNV(VkCommandBuffer commandBuffer, VkBool32 coverageToColorEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetCoverageToColorEnableNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(coverageToColorEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageToColorLocationNV(VkCommandBuffer commandBuffer, uint32_t coverageToColorLocation)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetCoverageToColorLocationNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(coverageToColorLocation));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageModulationModeNV(VkCommandBuffer commandBuffer, VkCoverageModulationModeNV coverageModulationMode)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetCoverageModulationModeNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(coverageModulationMode));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageModulationTableEnableNV(VkCommandBuffer commandBuffer, VkBool32 coverageModulationTableEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetCoverageModulationTableEnableNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(coverageModulationTableEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageModulationTableNV(VkCommandBuffer commandBuffer, uint32_t coverageModulationTableCount, const float * pCoverageModulationTable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetCoverageModulationTableNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(coverageModulationTableCount));
    mvk_put_u32(e, pCoverageModulationTable ? 1 : 0);
    if (pCoverageModulationTable) {
        mvk_put_bytes(e, pCoverageModulationTable, (size_t)(coverageModulationTableCount) * 4);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetShadingRateImageEnableNV(VkCommandBuffer commandBuffer, VkBool32 shadingRateImageEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetShadingRateImageEnableNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(shadingRateImageEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRepresentativeFragmentTestEnableNV(VkCommandBuffer commandBuffer, VkBool32 representativeFragmentTestEnable)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetRepresentativeFragmentTestEnableNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(representativeFragmentTestEnable));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageReductionModeNV(VkCommandBuffer commandBuffer, VkCoverageReductionModeNV coverageReductionMode)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetCoverageReductionModeNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(coverageReductionMode));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetShaderModuleIdentifierEXT(VkDevice device, VkShaderModule shaderModule, VkShaderModuleIdentifierEXT * pIdentifier)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetShaderModuleIdentifierEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(shaderModule));
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pIdentifier);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pIdentifier)
            mvk_out_VkShaderModuleIdentifierEXT(d, pIdentifier);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkGetShaderModuleCreateInfoIdentifierEXT(VkDevice device, const VkShaderModuleCreateInfo * pCreateInfo, VkShaderModuleIdentifierEXT * pIdentifier)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetShaderModuleCreateInfoIdentifierEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkShaderModuleCreateInfo(e, &pCreateInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pIdentifier);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pIdentifier)
            mvk_out_VkShaderModuleIdentifierEXT(d, pIdentifier);
    }
    mvk_call_done(d);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceOpticalFlowImageFormatsNV(VkPhysicalDevice physicalDevice, const VkOpticalFlowImageFormatInfoNV * pOpticalFlowImageFormatInfo, uint32_t * pFormatCount, VkOpticalFlowImageFormatPropertiesNV * pImageFormatProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceOpticalFlowImageFormatsNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pOpticalFlowImageFormatInfo ? 1 : 0);
    if (pOpticalFlowImageFormatInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkOpticalFlowImageFormatInfoNV(e, &pOpticalFlowImageFormatInfo[i]);
    }
    mvk_put_u32(e, pFormatCount ? *pFormatCount : 0);
    mvk_put_u32(e, pImageFormatProperties ? 1 : 0);
    if (pImageFormatProperties)
        for (uint32_t i = 0; i < (uint32_t)(*pFormatCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pImageFormatProperties[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pFormatCount)
            *pFormatCount = (uint32_t)mvk_get_u32(d);
        if (pImageFormatProperties && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkOpticalFlowImageFormatPropertiesNV(d, &pImageFormatProperties[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateOpticalFlowSessionNV(VkDevice device, const VkOpticalFlowSessionCreateInfoNV * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkOpticalFlowSessionNV * pSession)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateOpticalFlowSessionNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkOpticalFlowSessionCreateInfoNV(e, &pCreateInfo[i]);
    }
    *pSession = (VkOpticalFlowSessionNV)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pSession);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyOpticalFlowSessionNV(VkDevice device, VkOpticalFlowSessionNV session, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyOpticalFlowSessionNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(session));
    mvk_call_end(e);
    mvk_free_id((uint64_t)session);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindOpticalFlowSessionImageNV(VkDevice device, VkOpticalFlowSessionNV session, VkOpticalFlowSessionBindingPointNV bindingPoint, VkImageView view, VkImageLayout layout)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkBindOpticalFlowSessionImageNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(session));
    mvk_put_u32(e, (uint32_t)(bindingPoint));
    mvk_put_handle(e, 0, (uint64_t)(view));
    mvk_put_u32(e, (uint32_t)(layout));
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdOpticalFlowExecuteNV(VkCommandBuffer commandBuffer, VkOpticalFlowSessionNV session, const VkOpticalFlowExecuteInfoNV * pExecuteInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdOpticalFlowExecuteNV);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(session));
    mvk_put_u32(e, pExecuteInfo ? 1 : 0);
    if (pExecuteInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkOpticalFlowExecuteInfoNV(e, &pExecuteInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShadersEXT(VkDevice device, uint32_t createInfoCount, const VkShaderCreateInfoEXT * pCreateInfos, const VkAllocationCallbacks * pAllocator, VkShaderEXT * pShaders)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateShadersEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(createInfoCount));
    mvk_put_u32(e, pCreateInfos ? 1 : 0);
    if (pCreateInfos) {
        for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
            mvk_put_VkShaderCreateInfoEXT(e, &pCreateInfos[i]);
    }
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++) {
        pShaders[i] = (VkShaderEXT)mvk_new_id();
        mvk_put_u64(e, (uint64_t)pShaders[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderEXT(VkDevice device, VkShaderEXT shader, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyShaderEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(shader));
    mvk_call_end(e);
    mvk_free_id((uint64_t)shader);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetShaderBinaryDataEXT(VkDevice device, VkShaderEXT shader, size_t * pDataSize, void * pData)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetShaderBinaryDataEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(shader));
    mvk_put_u32(e, pData ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pDataSize)
            *pDataSize = (size_t)mvk_get_u64(d);
        if (pData && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            mvk_get_bytes(d, pData, n);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindShadersEXT(VkCommandBuffer commandBuffer, uint32_t stageCount, const VkShaderStageFlagBits * pStages, const VkShaderEXT * pShaders)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindShadersEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(stageCount));
    mvk_put_u32(e, pStages ? 1 : 0);
    if (pStages) {
        mvk_put_bytes(e, pStages, (size_t)(stageCount) * 4);
    }
    mvk_put_u32(e, pShaders ? 1 : 0);
    if (pShaders) {
        for (uint32_t i = 0; i < (uint32_t)(stageCount); i++)
            mvk_put_handle(e, 0, (uint64_t)(pShaders[i]));
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClampRangeEXT(VkCommandBuffer commandBuffer, VkDepthClampModeEXT depthClampMode, const VkDepthClampRangeEXT * pDepthClampRange)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDepthClampRangeEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(depthClampMode));
    mvk_put_u32(e, pDepthClampRange ? 1 : 0);
    if (pDepthClampRange) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDepthClampRangeEXT(e, &pDepthClampRange[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeVectorPropertiesNV(VkPhysicalDevice physicalDevice, uint32_t * pPropertyCount, VkCooperativeVectorPropertiesNV * pProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceCooperativeVectorPropertiesNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pPropertyCount ? *pPropertyCount : 0);
    mvk_put_u32(e, pProperties ? 1 : 0);
    if (pProperties)
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pProperties[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pPropertyCount)
            *pPropertyCount = (uint32_t)mvk_get_u32(d);
        if (pProperties && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkCooperativeVectorPropertiesNV(d, &pProperties[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkConvertCooperativeVectorMatrixNV(VkDevice device, const VkConvertCooperativeVectorMatrixInfoNV * pInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkConvertCooperativeVectorMatrixNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkConvertCooperativeVectorMatrixInfoNV(e, &pInfo[i]);
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdConvertCooperativeVectorMatrixNV(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkConvertCooperativeVectorMatrixInfoNV * pInfos)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdConvertCooperativeVectorMatrixNV);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(infoCount));
    mvk_put_u32(e, pInfos ? 1 : 0);
    if (pInfos) {
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_put_VkConvertCooperativeVectorMatrixInfoNV(e, &pInfos[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t * pPropertyCount, VkCooperativeMatrixPropertiesKHR * pProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pPropertyCount ? *pPropertyCount : 0);
    mvk_put_u32(e, pProperties ? 1 : 0);
    if (pProperties)
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pProperties[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pPropertyCount)
            *pPropertyCount = (uint32_t)mvk_get_u32(d);
        if (pProperties && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkCooperativeMatrixPropertiesKHR(d, &pProperties[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetAttachmentFeedbackLoopEnableEXT(VkCommandBuffer commandBuffer, VkImageAspectFlags aspectMask)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetAttachmentFeedbackLoopEnableEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(aspectMask));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(VkPhysicalDevice physicalDevice, uint32_t * pTimeDomainCount, VkTimeDomainKHR * pTimeDomains)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pTimeDomainCount ? *pTimeDomainCount : 0);
    mvk_put_u32(e, pTimeDomains ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pTimeDomainCount)
            *pTimeDomainCount = (uint32_t)mvk_get_u32(d);
        if (pTimeDomains && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            mvk_get_bytes(d, pTimeDomains, (size_t)n * sizeof(*pTimeDomains));
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetCalibratedTimestampsKHR(VkDevice device, uint32_t timestampCount, const VkCalibratedTimestampInfoKHR * pTimestampInfos, uint64_t * pTimestamps, uint64_t * pMaxDeviation)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetCalibratedTimestampsKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, (uint32_t)(timestampCount));
    mvk_put_u32(e, pTimestampInfos ? 1 : 0);
    if (pTimestampInfos) {
        for (uint32_t i = 0; i < (uint32_t)(timestampCount); i++)
            mvk_put_VkCalibratedTimestampInfoKHR(e, &pTimestampInfos[i]);
    }
    mvk_put_u32(e, pTimestamps ? 1 : 0);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pTimestamps && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            mvk_get_bytes(d, pTimestamps, (size_t)n * sizeof(*pTimestamps));
        }
        if (pMaxDeviation)
            *pMaxDeviation = (uint64_t)mvk_get_u64(d);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDescriptorBufferOffsets2EXT(VkCommandBuffer commandBuffer, const VkSetDescriptorBufferOffsetsInfoEXT * pSetDescriptorBufferOffsetsInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetDescriptorBufferOffsets2EXT);
    if (!e)
        return;
    mvk_put_u32(e, pSetDescriptorBufferOffsetsInfo ? 1 : 0);
    if (pSetDescriptorBufferOffsetsInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkSetDescriptorBufferOffsetsInfoEXT(e, &pSetDescriptorBufferOffsetsInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorBufferEmbeddedSamplers2EXT(VkCommandBuffer commandBuffer, const VkBindDescriptorBufferEmbeddedSamplersInfoEXT * pBindDescriptorBufferEmbeddedSamplersInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT);
    if (!e)
        return;
    mvk_put_u32(e, pBindDescriptorBufferEmbeddedSamplersInfo ? 1 : 0);
    if (pBindDescriptorBufferEmbeddedSamplersInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBindDescriptorBufferEmbeddedSamplersInfoEXT(e, &pBindDescriptorBufferEmbeddedSamplersInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryIndirectKHR(VkCommandBuffer commandBuffer, const VkCopyMemoryIndirectInfoKHR * pCopyMemoryIndirectInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyMemoryIndirectKHR);
    if (!e)
        return;
    mvk_put_u32(e, pCopyMemoryIndirectInfo ? 1 : 0);
    if (pCopyMemoryIndirectInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyMemoryIndirectInfoKHR(e, &pCopyMemoryIndirectInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToImageIndirectKHR(VkCommandBuffer commandBuffer, const VkCopyMemoryToImageIndirectInfoKHR * pCopyMemoryToImageIndirectInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdCopyMemoryToImageIndirectKHR);
    if (!e)
        return;
    mvk_put_u32(e, pCopyMemoryToImageIndirectInfo ? 1 : 0);
    if (pCopyMemoryToImageIndirectInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkCopyMemoryToImageIndirectInfoKHR(e, &pCopyMemoryToImageIndirectInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryEXT(VkCommandBuffer commandBuffer, const VkDecompressMemoryInfoEXT * pDecompressMemoryInfoEXT)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDecompressMemoryEXT);
    if (!e)
        return;
    mvk_put_u32(e, pDecompressMemoryInfoEXT ? 1 : 0);
    if (pDecompressMemoryInfoEXT) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkDecompressMemoryInfoEXT(e, &pDecompressMemoryInfoEXT[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryIndirectCountEXT(VkCommandBuffer commandBuffer, VkMemoryDecompressionMethodFlagsEXT decompressionMethod, VkDeviceAddress indirectCommandsAddress, VkDeviceAddress indirectCommandsCountAddress, uint32_t maxDecompressionCount, uint32_t stride)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdDecompressMemoryIndirectCountEXT);
    if (!e)
        return;
    mvk_put_u64(e, (uint64_t)(decompressionMethod));
    mvk_put_u64(e, (uint64_t)(indirectCommandsAddress));
    mvk_put_u64(e, (uint64_t)(indirectCommandsCountAddress));
    mvk_put_u32(e, (uint32_t)(maxDecompressionCount));
    mvk_put_u32(e, (uint32_t)(stride));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetClusterAccelerationStructureBuildSizesNV(VkDevice device, const VkClusterAccelerationStructureInputInfoNV * pInfo, VkAccelerationStructureBuildSizesInfoKHR * pSizeInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetClusterAccelerationStructureBuildSizesNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkClusterAccelerationStructureInputInfoNV(e, &pInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pSizeInfo);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pSizeInfo)
            mvk_out_VkAccelerationStructureBuildSizesInfoKHR(d, pSizeInfo);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBuildClusterAccelerationStructureIndirectNV(VkCommandBuffer commandBuffer, const VkClusterAccelerationStructureCommandsInfoNV * pCommandInfos)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBuildClusterAccelerationStructureIndirectNV);
    if (!e)
        return;
    mvk_put_u32(e, pCommandInfos ? 1 : 0);
    if (pCommandInfos) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkClusterAccelerationStructureCommandsInfoNV(e, &pCommandInfos[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetPartitionedAccelerationStructuresBuildSizesNV(VkDevice device, const VkPartitionedAccelerationStructureInstancesInputNV * pInfo, VkAccelerationStructureBuildSizesInfoKHR * pSizeInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPartitionedAccelerationStructuresBuildSizesNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPartitionedAccelerationStructureInstancesInputNV(e, &pInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pSizeInfo);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pSizeInfo)
            mvk_out_VkAccelerationStructureBuildSizesInfoKHR(d, pSizeInfo);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBuildPartitionedAccelerationStructuresNV(VkCommandBuffer commandBuffer, const VkBuildPartitionedAccelerationStructureInfoNV * pBuildInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBuildPartitionedAccelerationStructuresNV);
    if (!e)
        return;
    mvk_put_u32(e, pBuildInfo ? 1 : 0);
    if (pBuildInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBuildPartitionedAccelerationStructureInfoNV(e, &pBuildInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkGetGeneratedCommandsMemoryRequirementsEXT(VkDevice device, const VkGeneratedCommandsMemoryRequirementsInfoEXT * pInfo, VkMemoryRequirements2 * pMemoryRequirements)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetGeneratedCommandsMemoryRequirementsEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pInfo ? 1 : 0);
    if (pInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkGeneratedCommandsMemoryRequirementsInfoEXT(e, &pInfo[i]);
    }
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pMemoryRequirements);
    MvkDecoder *d = mvk_call_wait(e);
    if (d) {
        if (pMemoryRequirements)
            mvk_out_VkMemoryRequirements2(d, pMemoryRequirements);
    }
    mvk_call_done(d);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPreprocessGeneratedCommandsEXT(VkCommandBuffer commandBuffer, const VkGeneratedCommandsInfoEXT * pGeneratedCommandsInfo, VkCommandBuffer stateCommandBuffer)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdPreprocessGeneratedCommandsEXT);
    if (!e)
        return;
    mvk_put_u32(e, pGeneratedCommandsInfo ? 1 : 0);
    if (pGeneratedCommandsInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkGeneratedCommandsInfoEXT(e, &pGeneratedCommandsInfo[i]);
    }
    mvk_put_handle(e, 1, mvk_dispatch_id(stateCommandBuffer));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdExecuteGeneratedCommandsEXT(VkCommandBuffer commandBuffer, VkBool32 isPreprocessed, const VkGeneratedCommandsInfoEXT * pGeneratedCommandsInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdExecuteGeneratedCommandsEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(isPreprocessed));
    mvk_put_u32(e, pGeneratedCommandsInfo ? 1 : 0);
    if (pGeneratedCommandsInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkGeneratedCommandsInfoEXT(e, &pGeneratedCommandsInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectCommandsLayoutEXT(VkDevice device, const VkIndirectCommandsLayoutCreateInfoEXT * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkIndirectCommandsLayoutEXT * pIndirectCommandsLayout)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateIndirectCommandsLayoutEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkIndirectCommandsLayoutCreateInfoEXT(e, &pCreateInfo[i]);
    }
    *pIndirectCommandsLayout = (VkIndirectCommandsLayoutEXT)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pIndirectCommandsLayout);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectCommandsLayoutEXT(VkDevice device, VkIndirectCommandsLayoutEXT indirectCommandsLayout, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyIndirectCommandsLayoutEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(indirectCommandsLayout));
    mvk_call_end(e);
    mvk_free_id((uint64_t)indirectCommandsLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectExecutionSetEXT(VkDevice device, const VkIndirectExecutionSetCreateInfoEXT * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkIndirectExecutionSetEXT * pIndirectExecutionSet)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateIndirectExecutionSetEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkIndirectExecutionSetCreateInfoEXT(e, &pCreateInfo[i]);
    }
    *pIndirectExecutionSet = (VkIndirectExecutionSetEXT)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pIndirectExecutionSet);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectExecutionSetEXT(VkDevice device, VkIndirectExecutionSetEXT indirectExecutionSet, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyIndirectExecutionSetEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(indirectExecutionSet));
    mvk_call_end(e);
    mvk_free_id((uint64_t)indirectExecutionSet);
}

VKAPI_ATTR void VKAPI_CALL vkUpdateIndirectExecutionSetPipelineEXT(VkDevice device, VkIndirectExecutionSetEXT indirectExecutionSet, uint32_t executionSetWriteCount, const VkWriteIndirectExecutionSetPipelineEXT * pExecutionSetWrites)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkUpdateIndirectExecutionSetPipelineEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(indirectExecutionSet));
    mvk_put_u32(e, (uint32_t)(executionSetWriteCount));
    mvk_put_u32(e, pExecutionSetWrites ? 1 : 0);
    if (pExecutionSetWrites) {
        for (uint32_t i = 0; i < (uint32_t)(executionSetWriteCount); i++)
            mvk_put_VkWriteIndirectExecutionSetPipelineEXT(e, &pExecutionSetWrites[i]);
    }
    mvk_call_end(e);
}

VKAPI_ATTR void VKAPI_CALL vkUpdateIndirectExecutionSetShaderEXT(VkDevice device, VkIndirectExecutionSetEXT indirectExecutionSet, uint32_t executionSetWriteCount, const VkWriteIndirectExecutionSetShaderEXT * pExecutionSetWrites)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkUpdateIndirectExecutionSetShaderEXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(indirectExecutionSet));
    mvk_put_u32(e, (uint32_t)(executionSetWriteCount));
    mvk_put_u32(e, pExecutionSetWrites ? 1 : 0);
    if (pExecutionSetWrites) {
        for (uint32_t i = 0; i < (uint32_t)(executionSetWriteCount); i++)
            mvk_put_VkWriteIndirectExecutionSetShaderEXT(e, &pExecutionSetWrites[i]);
    }
    mvk_call_end(e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceFaultReportsKHR(VkDevice device, uint64_t timeout, uint32_t * pFaultCounts, VkDeviceFaultInfoKHR * pFaultInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeviceFaultReportsKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u64(e, (uint64_t)(timeout));
    mvk_put_u32(e, pFaultInfo ? 1 : 0);
    if (pFaultInfo)
        for (uint32_t i = 0; i < (uint32_t)(*pFaultCounts); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pFaultInfo[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pFaultCounts)
            *pFaultCounts = (uint32_t)mvk_get_u32(d);
        if (pFaultInfo && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkDeviceFaultInfoKHR(d, &pFaultInfo[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceFaultDebugInfoKHR(VkDevice device, VkDeviceFaultDebugInfoKHR * pDebugInfo)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetDeviceFaultDebugInfoKHR);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_out_shape(e, (const VkBaseOutStructure *)pDebugInfo);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pDebugInfo)
            mvk_out_VkDeviceFaultDebugInfoKHR(d, pDebugInfo);
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV(VkPhysicalDevice physicalDevice, uint32_t * pPropertyCount, VkCooperativeMatrixFlexibleDimensionsPropertiesNV * pProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pPropertyCount ? *pPropertyCount : 0);
    mvk_put_u32(e, pProperties ? 1 : 0);
    if (pProperties)
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pProperties[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pPropertyCount)
            *pPropertyCount = (uint32_t)mvk_get_u32(d);
        if (pProperties && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkCooperativeMatrixFlexibleDimensionsPropertiesNV(d, &pProperties[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, uint32_t * pCounterCount, VkPerformanceCounterARM * pCounters, VkPerformanceCounterDescriptionARM * pCounterDescriptions)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, (uint32_t)(queueFamilyIndex));
    mvk_put_u32(e, pCounterCount ? *pCounterCount : 0);
    mvk_put_u32(e, pCounters ? 1 : 0);
    if (pCounters)
        for (uint32_t i = 0; i < (uint32_t)(*pCounterCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pCounters[i]);
    mvk_put_u32(e, pCounterDescriptions ? 1 : 0);
    if (pCounterDescriptions)
        for (uint32_t i = 0; i < (uint32_t)(*pCounterCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pCounterDescriptions[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pCounterCount)
            *pCounterCount = (uint32_t)mvk_get_u32(d);
        if (pCounters && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkPerformanceCounterARM(d, &pCounters[i]);
        }
        if (pCounterDescriptions && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkPerformanceCounterDescriptionARM(d, &pCounterDescriptions[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM(VkPhysicalDevice physicalDevice, uint32_t * pDescriptionCount, VkShaderInstrumentationMetricDescriptionARM * pDescriptions)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pDescriptionCount ? *pDescriptionCount : 0);
    mvk_put_u32(e, pDescriptions ? 1 : 0);
    if (pDescriptions)
        for (uint32_t i = 0; i < (uint32_t)(*pDescriptionCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pDescriptions[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pDescriptionCount)
            *pDescriptionCount = (uint32_t)mvk_get_u32(d);
        if (pDescriptions && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkShaderInstrumentationMetricDescriptionARM(d, &pDescriptions[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderInstrumentationARM(VkDevice device, const VkShaderInstrumentationCreateInfoARM * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkShaderInstrumentationARM * pInstrumentation)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkCreateShaderInstrumentationARM);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_u32(e, pCreateInfo ? 1 : 0);
    if (pCreateInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkShaderInstrumentationCreateInfoARM(e, &pCreateInfo[i]);
    }
    *pInstrumentation = (VkShaderInstrumentationARM)mvk_new_id();
    mvk_put_u64(e, (uint64_t)*pInstrumentation);
    mvk_call_end(e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderInstrumentationARM(VkDevice device, VkShaderInstrumentationARM instrumentation, const VkAllocationCallbacks * pAllocator)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkDestroyShaderInstrumentationARM);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(instrumentation));
    mvk_call_end(e);
    mvk_free_id((uint64_t)instrumentation);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginShaderInstrumentationARM(VkCommandBuffer commandBuffer, VkShaderInstrumentationARM instrumentation)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBeginShaderInstrumentationARM);
    if (!e)
        return;
    mvk_put_handle(e, 0, (uint64_t)(instrumentation));
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndShaderInstrumentationARM(VkCommandBuffer commandBuffer)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdEndShaderInstrumentationARM);
    if (!e)
        return;
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkClearShaderInstrumentationMetricsARM(VkDevice device, VkShaderInstrumentationARM instrumentation)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkClearShaderInstrumentationMetricsARM);
    mvk_put_handle(e, 1, mvk_dispatch_id(device));
    mvk_put_handle(e, 0, (uint64_t)(instrumentation));
    mvk_call_end(e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginCustomResolveEXT(VkCommandBuffer commandBuffer, const VkBeginCustomResolveInfoEXT * pBeginCustomResolveInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdBeginCustomResolveEXT);
    if (!e)
        return;
    mvk_put_u32(e, pBeginCustomResolveInfo ? 1 : 0);
    if (pBeginCustomResolveInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkBeginCustomResolveInfoEXT(e, &pBeginCustomResolveInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering2KHR(VkCommandBuffer commandBuffer, const VkRenderingEndInfoKHR * pRenderingEndInfo)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdEndRendering2KHR);
    if (!e)
        return;
    mvk_put_u32(e, pRenderingEndInfo ? 1 : 0);
    if (pRenderingEndInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkRenderingEndInfoKHR(e, &pRenderingEndInfo[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetComputeOccupancyPriorityNV(VkCommandBuffer commandBuffer, const VkComputeOccupancyPriorityParametersNV * pParameters)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetComputeOccupancyPriorityNV);
    if (!e)
        return;
    mvk_put_u32(e, pParameters ? 1 : 0);
    if (pParameters) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkComputeOccupancyPriorityParametersNV(e, &pParameters[i]);
    }
    mvk_cmd_end(commandBuffer, e);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeMatrixProperties2EXT(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceCooperativeMatrixInfo2EXT * pCooperativeMatrixInfo, uint32_t * pPropertyCount, VkCooperativeMatrixProperties2EXT * pProperties)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_vkGetPhysicalDeviceCooperativeMatrixProperties2EXT);
    mvk_put_handle(e, 1, mvk_dispatch_id(physicalDevice));
    mvk_put_u32(e, pCooperativeMatrixInfo ? 1 : 0);
    if (pCooperativeMatrixInfo) {
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_put_VkPhysicalDeviceCooperativeMatrixInfo2EXT(e, &pCooperativeMatrixInfo[i]);
    }
    mvk_put_u32(e, pPropertyCount ? *pPropertyCount : 0);
    mvk_put_u32(e, pProperties ? 1 : 0);
    if (pProperties)
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            mvk_put_out_shape(e, (const VkBaseOutStructure *)&pProperties[i]);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_DEVICE_LOST;
    if (d) {
        if (pPropertyCount)
            *pPropertyCount = (uint32_t)mvk_get_u32(d);
        if (pProperties && mvk_get_u32(d)) {
            uint32_t n = mvk_get_u32(d);
            for (uint32_t i = 0; i < n; i++)
                mvk_out_VkCooperativeMatrixProperties2EXT(d, &pProperties[i]);
        }
    }
    mvk_call_done(d);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartIndexEXT(VkCommandBuffer commandBuffer, uint32_t primitiveRestartIndex)
{
    MvkEncoder *e = mvk_cmd_begin(commandBuffer, MVK_OP_vkCmdSetPrimitiveRestartIndexEXT);
    if (!e)
        return;
    mvk_put_u32(e, (uint32_t)(primitiveRestartIndex));
    mvk_cmd_end(commandBuffer, e);
}

