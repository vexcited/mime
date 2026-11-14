#ifndef MVK_DISPATCH_H
#define MVK_DISPATCH_H

typedef struct MvkDispatch {
    PFN_vkCreateInstance vkCreateInstance;
    PFN_vkDestroyInstance vkDestroyInstance;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
    PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
    PFN_vkGetPhysicalDeviceImageFormatProperties vkGetPhysicalDeviceImageFormatProperties;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
    PFN_vkCreateDevice vkCreateDevice;
    PFN_vkDestroyDevice vkDestroyDevice;
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
    PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
    PFN_vkEnumerateDeviceLayerProperties vkEnumerateDeviceLayerProperties;
    PFN_vkGetDeviceQueue vkGetDeviceQueue;
    PFN_vkQueueSubmit vkQueueSubmit;
    PFN_vkQueueWaitIdle vkQueueWaitIdle;
    PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
    PFN_vkAllocateMemory vkAllocateMemory;
    PFN_vkFreeMemory vkFreeMemory;
    PFN_vkMapMemory vkMapMemory;
    PFN_vkUnmapMemory vkUnmapMemory;
    PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
    PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;
    PFN_vkGetDeviceMemoryCommitment vkGetDeviceMemoryCommitment;
    PFN_vkBindBufferMemory vkBindBufferMemory;
    PFN_vkBindImageMemory vkBindImageMemory;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
    PFN_vkGetImageSparseMemoryRequirements vkGetImageSparseMemoryRequirements;
    PFN_vkGetPhysicalDeviceSparseImageFormatProperties vkGetPhysicalDeviceSparseImageFormatProperties;
    PFN_vkQueueBindSparse vkQueueBindSparse;
    PFN_vkCreateFence vkCreateFence;
    PFN_vkDestroyFence vkDestroyFence;
    PFN_vkResetFences vkResetFences;
    PFN_vkGetFenceStatus vkGetFenceStatus;
    PFN_vkWaitForFences vkWaitForFences;
    PFN_vkCreateSemaphore vkCreateSemaphore;
    PFN_vkDestroySemaphore vkDestroySemaphore;
    PFN_vkCreateQueryPool vkCreateQueryPool;
    PFN_vkDestroyQueryPool vkDestroyQueryPool;
    PFN_vkGetQueryPoolResults vkGetQueryPoolResults;
    PFN_vkCreateBuffer vkCreateBuffer;
    PFN_vkDestroyBuffer vkDestroyBuffer;
    PFN_vkCreateImage vkCreateImage;
    PFN_vkDestroyImage vkDestroyImage;
    PFN_vkGetImageSubresourceLayout vkGetImageSubresourceLayout;
    PFN_vkCreateImageView vkCreateImageView;
    PFN_vkDestroyImageView vkDestroyImageView;
    PFN_vkCreateCommandPool vkCreateCommandPool;
    PFN_vkDestroyCommandPool vkDestroyCommandPool;
    PFN_vkResetCommandPool vkResetCommandPool;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    PFN_vkFreeCommandBuffers vkFreeCommandBuffers;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    PFN_vkEndCommandBuffer vkEndCommandBuffer;
    PFN_vkResetCommandBuffer vkResetCommandBuffer;
    PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
    PFN_vkCmdCopyImage vkCmdCopyImage;
    PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
    PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;
    PFN_vkCmdUpdateBuffer vkCmdUpdateBuffer;
    PFN_vkCmdFillBuffer vkCmdFillBuffer;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
    PFN_vkCmdBeginQuery vkCmdBeginQuery;
    PFN_vkCmdEndQuery vkCmdEndQuery;
    PFN_vkCmdResetQueryPool vkCmdResetQueryPool;
    PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp;
    PFN_vkCmdCopyQueryPoolResults vkCmdCopyQueryPoolResults;
    PFN_vkCmdExecuteCommands vkCmdExecuteCommands;
    PFN_vkCreateEvent vkCreateEvent;
    PFN_vkDestroyEvent vkDestroyEvent;
    PFN_vkGetEventStatus vkGetEventStatus;
    PFN_vkSetEvent vkSetEvent;
    PFN_vkResetEvent vkResetEvent;
    PFN_vkCreateBufferView vkCreateBufferView;
    PFN_vkDestroyBufferView vkDestroyBufferView;
    PFN_vkCreateShaderModule vkCreateShaderModule;
    PFN_vkDestroyShaderModule vkDestroyShaderModule;
    PFN_vkCreatePipelineCache vkCreatePipelineCache;
    PFN_vkDestroyPipelineCache vkDestroyPipelineCache;
    PFN_vkGetPipelineCacheData vkGetPipelineCacheData;
    PFN_vkMergePipelineCaches vkMergePipelineCaches;
    PFN_vkCreateComputePipelines vkCreateComputePipelines;
    PFN_vkDestroyPipeline vkDestroyPipeline;
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
    PFN_vkCreateSampler vkCreateSampler;
    PFN_vkDestroySampler vkDestroySampler;
    PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
    PFN_vkResetDescriptorPool vkResetDescriptorPool;
    PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
    PFN_vkFreeDescriptorSets vkFreeDescriptorSets;
    PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
    PFN_vkCmdBindPipeline vkCmdBindPipeline;
    PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
    PFN_vkCmdClearColorImage vkCmdClearColorImage;
    PFN_vkCmdDispatch vkCmdDispatch;
    PFN_vkCmdDispatchIndirect vkCmdDispatchIndirect;
    PFN_vkCmdSetEvent vkCmdSetEvent;
    PFN_vkCmdResetEvent vkCmdResetEvent;
    PFN_vkCmdWaitEvents vkCmdWaitEvents;
    PFN_vkCmdPushConstants vkCmdPushConstants;
    PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
    PFN_vkCreateFramebuffer vkCreateFramebuffer;
    PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
    PFN_vkCreateRenderPass vkCreateRenderPass;
    PFN_vkDestroyRenderPass vkDestroyRenderPass;
    PFN_vkGetRenderAreaGranularity vkGetRenderAreaGranularity;
    PFN_vkCmdSetViewport vkCmdSetViewport;
    PFN_vkCmdSetScissor vkCmdSetScissor;
    PFN_vkCmdSetLineWidth vkCmdSetLineWidth;
    PFN_vkCmdSetDepthBias vkCmdSetDepthBias;
    PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants;
    PFN_vkCmdSetDepthBounds vkCmdSetDepthBounds;
    PFN_vkCmdSetStencilCompareMask vkCmdSetStencilCompareMask;
    PFN_vkCmdSetStencilWriteMask vkCmdSetStencilWriteMask;
    PFN_vkCmdSetStencilReference vkCmdSetStencilReference;
    PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
    PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
    PFN_vkCmdDraw vkCmdDraw;
    PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
    PFN_vkCmdDrawIndirect vkCmdDrawIndirect;
    PFN_vkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect;
    PFN_vkCmdBlitImage vkCmdBlitImage;
    PFN_vkCmdClearDepthStencilImage vkCmdClearDepthStencilImage;
    PFN_vkCmdClearAttachments vkCmdClearAttachments;
    PFN_vkCmdResolveImage vkCmdResolveImage;
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
    PFN_vkCmdNextSubpass vkCmdNextSubpass;
    PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
    PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
    PFN_vkBindBufferMemory2 vkBindBufferMemory2;
    PFN_vkBindImageMemory2 vkBindImageMemory2;
    PFN_vkGetDeviceGroupPeerMemoryFeatures vkGetDeviceGroupPeerMemoryFeatures;
    PFN_vkCmdSetDeviceMask vkCmdSetDeviceMask;
    PFN_vkEnumeratePhysicalDeviceGroups vkEnumeratePhysicalDeviceGroups;
    PFN_vkGetImageMemoryRequirements2 vkGetImageMemoryRequirements2;
    PFN_vkGetBufferMemoryRequirements2 vkGetBufferMemoryRequirements2;
    PFN_vkGetImageSparseMemoryRequirements2 vkGetImageSparseMemoryRequirements2;
    PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2;
    PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2;
    PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2;
    PFN_vkGetPhysicalDeviceImageFormatProperties2 vkGetPhysicalDeviceImageFormatProperties2;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 vkGetPhysicalDeviceQueueFamilyProperties2;
    PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2;
    PFN_vkGetPhysicalDeviceSparseImageFormatProperties2 vkGetPhysicalDeviceSparseImageFormatProperties2;
    PFN_vkTrimCommandPool vkTrimCommandPool;
    PFN_vkGetDeviceQueue2 vkGetDeviceQueue2;
    PFN_vkGetPhysicalDeviceExternalBufferProperties vkGetPhysicalDeviceExternalBufferProperties;
    PFN_vkGetPhysicalDeviceExternalFenceProperties vkGetPhysicalDeviceExternalFenceProperties;
    PFN_vkGetPhysicalDeviceExternalSemaphoreProperties vkGetPhysicalDeviceExternalSemaphoreProperties;
    PFN_vkCmdDispatchBase vkCmdDispatchBase;
    PFN_vkCreateDescriptorUpdateTemplate vkCreateDescriptorUpdateTemplate;
    PFN_vkDestroyDescriptorUpdateTemplate vkDestroyDescriptorUpdateTemplate;
    PFN_vkUpdateDescriptorSetWithTemplate vkUpdateDescriptorSetWithTemplate;
    PFN_vkGetDescriptorSetLayoutSupport vkGetDescriptorSetLayoutSupport;
    PFN_vkCreateSamplerYcbcrConversion vkCreateSamplerYcbcrConversion;
    PFN_vkDestroySamplerYcbcrConversion vkDestroySamplerYcbcrConversion;
    PFN_vkResetQueryPool vkResetQueryPool;
    PFN_vkGetSemaphoreCounterValue vkGetSemaphoreCounterValue;
    PFN_vkWaitSemaphores vkWaitSemaphores;
    PFN_vkSignalSemaphore vkSignalSemaphore;
    PFN_vkGetBufferDeviceAddress vkGetBufferDeviceAddress;
    PFN_vkGetBufferOpaqueCaptureAddress vkGetBufferOpaqueCaptureAddress;
    PFN_vkGetDeviceMemoryOpaqueCaptureAddress vkGetDeviceMemoryOpaqueCaptureAddress;
    PFN_vkCmdDrawIndirectCount vkCmdDrawIndirectCount;
    PFN_vkCmdDrawIndexedIndirectCount vkCmdDrawIndexedIndirectCount;
    PFN_vkCreateRenderPass2 vkCreateRenderPass2;
    PFN_vkCmdBeginRenderPass2 vkCmdBeginRenderPass2;
    PFN_vkCmdNextSubpass2 vkCmdNextSubpass2;
    PFN_vkCmdEndRenderPass2 vkCmdEndRenderPass2;
    PFN_vkGetPhysicalDeviceToolProperties vkGetPhysicalDeviceToolProperties;
    PFN_vkCreatePrivateDataSlot vkCreatePrivateDataSlot;
    PFN_vkDestroyPrivateDataSlot vkDestroyPrivateDataSlot;
    PFN_vkSetPrivateData vkSetPrivateData;
    PFN_vkGetPrivateData vkGetPrivateData;
    PFN_vkCmdPipelineBarrier2 vkCmdPipelineBarrier2;
    PFN_vkCmdWriteTimestamp2 vkCmdWriteTimestamp2;
    PFN_vkQueueSubmit2 vkQueueSubmit2;
    PFN_vkCmdCopyBuffer2 vkCmdCopyBuffer2;
    PFN_vkCmdCopyImage2 vkCmdCopyImage2;
    PFN_vkCmdCopyBufferToImage2 vkCmdCopyBufferToImage2;
    PFN_vkCmdCopyImageToBuffer2 vkCmdCopyImageToBuffer2;
    PFN_vkGetDeviceBufferMemoryRequirements vkGetDeviceBufferMemoryRequirements;
    PFN_vkGetDeviceImageMemoryRequirements vkGetDeviceImageMemoryRequirements;
    PFN_vkGetDeviceImageSparseMemoryRequirements vkGetDeviceImageSparseMemoryRequirements;
    PFN_vkCmdSetEvent2 vkCmdSetEvent2;
    PFN_vkCmdResetEvent2 vkCmdResetEvent2;
    PFN_vkCmdWaitEvents2 vkCmdWaitEvents2;
    PFN_vkCmdBlitImage2 vkCmdBlitImage2;
    PFN_vkCmdResolveImage2 vkCmdResolveImage2;
    PFN_vkCmdBeginRendering vkCmdBeginRendering;
    PFN_vkCmdEndRendering vkCmdEndRendering;
    PFN_vkCmdSetCullMode vkCmdSetCullMode;
    PFN_vkCmdSetFrontFace vkCmdSetFrontFace;
    PFN_vkCmdSetPrimitiveTopology vkCmdSetPrimitiveTopology;
    PFN_vkCmdSetViewportWithCount vkCmdSetViewportWithCount;
    PFN_vkCmdSetScissorWithCount vkCmdSetScissorWithCount;
    PFN_vkCmdBindVertexBuffers2 vkCmdBindVertexBuffers2;
    PFN_vkCmdSetDepthTestEnable vkCmdSetDepthTestEnable;
    PFN_vkCmdSetDepthWriteEnable vkCmdSetDepthWriteEnable;
    PFN_vkCmdSetDepthCompareOp vkCmdSetDepthCompareOp;
    PFN_vkCmdSetDepthBoundsTestEnable vkCmdSetDepthBoundsTestEnable;
    PFN_vkCmdSetStencilTestEnable vkCmdSetStencilTestEnable;
    PFN_vkCmdSetStencilOp vkCmdSetStencilOp;
    PFN_vkCmdSetRasterizerDiscardEnable vkCmdSetRasterizerDiscardEnable;
    PFN_vkCmdSetDepthBiasEnable vkCmdSetDepthBiasEnable;
    PFN_vkCmdSetPrimitiveRestartEnable vkCmdSetPrimitiveRestartEnable;
    PFN_vkMapMemory2 vkMapMemory2;
    PFN_vkUnmapMemory2 vkUnmapMemory2;
    PFN_vkGetDeviceImageSubresourceLayout vkGetDeviceImageSubresourceLayout;
    PFN_vkGetImageSubresourceLayout2 vkGetImageSubresourceLayout2;
    PFN_vkCopyMemoryToImage vkCopyMemoryToImage;
    PFN_vkCopyImageToMemory vkCopyImageToMemory;
    PFN_vkCopyImageToImage vkCopyImageToImage;
    PFN_vkTransitionImageLayout vkTransitionImageLayout;
    PFN_vkCmdPushDescriptorSet vkCmdPushDescriptorSet;
    PFN_vkCmdPushDescriptorSetWithTemplate vkCmdPushDescriptorSetWithTemplate;
    PFN_vkCmdBindDescriptorSets2 vkCmdBindDescriptorSets2;
    PFN_vkCmdPushConstants2 vkCmdPushConstants2;
    PFN_vkCmdPushDescriptorSet2 vkCmdPushDescriptorSet2;
    PFN_vkCmdPushDescriptorSetWithTemplate2 vkCmdPushDescriptorSetWithTemplate2;
    PFN_vkCmdSetLineStipple vkCmdSetLineStipple;
    PFN_vkCmdBindIndexBuffer2 vkCmdBindIndexBuffer2;
    PFN_vkGetRenderingAreaGranularity vkGetRenderingAreaGranularity;
    PFN_vkCmdSetRenderingAttachmentLocations vkCmdSetRenderingAttachmentLocations;
    PFN_vkCmdSetRenderingInputAttachmentIndices vkCmdSetRenderingInputAttachmentIndices;
    PFN_vkCmdBindTransformFeedbackBuffersEXT vkCmdBindTransformFeedbackBuffersEXT;
    PFN_vkCmdBeginTransformFeedbackEXT vkCmdBeginTransformFeedbackEXT;
    PFN_vkCmdEndTransformFeedbackEXT vkCmdEndTransformFeedbackEXT;
    PFN_vkCmdBeginQueryIndexedEXT vkCmdBeginQueryIndexedEXT;
    PFN_vkCmdEndQueryIndexedEXT vkCmdEndQueryIndexedEXT;
    PFN_vkCmdDrawIndirectByteCountEXT vkCmdDrawIndirectByteCountEXT;
    PFN_vkGetImageViewHandleNVX vkGetImageViewHandleNVX;
    PFN_vkGetImageViewHandle64NVX vkGetImageViewHandle64NVX;
    PFN_vkGetImageViewAddressNVX vkGetImageViewAddressNVX;
    PFN_vkGetDeviceCombinedImageSamplerIndexNVX vkGetDeviceCombinedImageSamplerIndexNVX;
    PFN_vkCmdDrawIndirectCountAMD vkCmdDrawIndirectCountAMD;
    PFN_vkCmdDrawIndexedIndirectCountAMD vkCmdDrawIndexedIndirectCountAMD;
    PFN_vkGetShaderInfoAMD vkGetShaderInfoAMD;
    PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR;
    PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHR;
    PFN_vkGetPhysicalDeviceExternalImageFormatPropertiesNV vkGetPhysicalDeviceExternalImageFormatPropertiesNV;
    PFN_vkGetPhysicalDeviceFeatures2KHR vkGetPhysicalDeviceFeatures2KHR;
    PFN_vkGetPhysicalDeviceProperties2KHR vkGetPhysicalDeviceProperties2KHR;
    PFN_vkGetPhysicalDeviceFormatProperties2KHR vkGetPhysicalDeviceFormatProperties2KHR;
    PFN_vkGetPhysicalDeviceImageFormatProperties2KHR vkGetPhysicalDeviceImageFormatProperties2KHR;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2KHR vkGetPhysicalDeviceQueueFamilyProperties2KHR;
    PFN_vkGetPhysicalDeviceMemoryProperties2KHR vkGetPhysicalDeviceMemoryProperties2KHR;
    PFN_vkGetPhysicalDeviceSparseImageFormatProperties2KHR vkGetPhysicalDeviceSparseImageFormatProperties2KHR;
    PFN_vkGetDeviceGroupPeerMemoryFeaturesKHR vkGetDeviceGroupPeerMemoryFeaturesKHR;
    PFN_vkCmdSetDeviceMaskKHR vkCmdSetDeviceMaskKHR;
    PFN_vkCmdDispatchBaseKHR vkCmdDispatchBaseKHR;
    PFN_vkGetDeviceGroupPresentCapabilitiesKHR vkGetDeviceGroupPresentCapabilitiesKHR;
    PFN_vkGetDeviceGroupSurfacePresentModesKHR vkGetDeviceGroupSurfacePresentModesKHR;
    PFN_vkGetPhysicalDevicePresentRectanglesKHR vkGetPhysicalDevicePresentRectanglesKHR;
    PFN_vkAcquireNextImage2KHR vkAcquireNextImage2KHR;
    PFN_vkTrimCommandPoolKHR vkTrimCommandPoolKHR;
    PFN_vkEnumeratePhysicalDeviceGroupsKHR vkEnumeratePhysicalDeviceGroupsKHR;
    PFN_vkGetPhysicalDeviceExternalBufferPropertiesKHR vkGetPhysicalDeviceExternalBufferPropertiesKHR;
    PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR vkGetPhysicalDeviceExternalSemaphorePropertiesKHR;
    PFN_vkCmdPushDescriptorSetKHR vkCmdPushDescriptorSetKHR;
    PFN_vkCmdPushDescriptorSetWithTemplateKHR vkCmdPushDescriptorSetWithTemplateKHR;
    PFN_vkCmdBeginConditionalRenderingEXT vkCmdBeginConditionalRenderingEXT;
    PFN_vkCmdEndConditionalRenderingEXT vkCmdEndConditionalRenderingEXT;
    PFN_vkCreateDescriptorUpdateTemplateKHR vkCreateDescriptorUpdateTemplateKHR;
    PFN_vkDestroyDescriptorUpdateTemplateKHR vkDestroyDescriptorUpdateTemplateKHR;
    PFN_vkUpdateDescriptorSetWithTemplateKHR vkUpdateDescriptorSetWithTemplateKHR;
    PFN_vkCmdSetViewportWScalingNV vkCmdSetViewportWScalingNV;
    PFN_vkCmdSetDiscardRectangleEXT vkCmdSetDiscardRectangleEXT;
    PFN_vkCmdSetDiscardRectangleEnableEXT vkCmdSetDiscardRectangleEnableEXT;
    PFN_vkCmdSetDiscardRectangleModeEXT vkCmdSetDiscardRectangleModeEXT;
    PFN_vkCreateRenderPass2KHR vkCreateRenderPass2KHR;
    PFN_vkCmdBeginRenderPass2KHR vkCmdBeginRenderPass2KHR;
    PFN_vkCmdNextSubpass2KHR vkCmdNextSubpass2KHR;
    PFN_vkCmdEndRenderPass2KHR vkCmdEndRenderPass2KHR;
    PFN_vkGetPhysicalDeviceExternalFencePropertiesKHR vkGetPhysicalDeviceExternalFencePropertiesKHR;
    PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR;
    PFN_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR;
    PFN_vkAcquireProfilingLockKHR vkAcquireProfilingLockKHR;
    PFN_vkReleaseProfilingLockKHR vkReleaseProfilingLockKHR;
    PFN_vkCreateGpaSessionAMD vkCreateGpaSessionAMD;
    PFN_vkDestroyGpaSessionAMD vkDestroyGpaSessionAMD;
    PFN_vkSetGpaDeviceClockModeAMD vkSetGpaDeviceClockModeAMD;
    PFN_vkGetGpaDeviceClockInfoAMD vkGetGpaDeviceClockInfoAMD;
    PFN_vkCmdBeginGpaSessionAMD vkCmdBeginGpaSessionAMD;
    PFN_vkCmdEndGpaSessionAMD vkCmdEndGpaSessionAMD;
    PFN_vkCmdBeginGpaSampleAMD vkCmdBeginGpaSampleAMD;
    PFN_vkCmdEndGpaSampleAMD vkCmdEndGpaSampleAMD;
    PFN_vkGetGpaSessionStatusAMD vkGetGpaSessionStatusAMD;
    PFN_vkGetGpaSessionResultsAMD vkGetGpaSessionResultsAMD;
    PFN_vkResetGpaSessionAMD vkResetGpaSessionAMD;
    PFN_vkCmdCopyGpaSessionResultsAMD vkCmdCopyGpaSessionResultsAMD;
    PFN_vkWriteSamplerDescriptorsEXT vkWriteSamplerDescriptorsEXT;
    PFN_vkWriteResourceDescriptorsEXT vkWriteResourceDescriptorsEXT;
    PFN_vkCmdBindSamplerHeapEXT vkCmdBindSamplerHeapEXT;
    PFN_vkCmdBindResourceHeapEXT vkCmdBindResourceHeapEXT;
    PFN_vkCmdPushDataEXT vkCmdPushDataEXT;
    PFN_vkGetImageOpaqueCaptureDataEXT vkGetImageOpaqueCaptureDataEXT;
    PFN_vkGetPhysicalDeviceDescriptorSizeEXT vkGetPhysicalDeviceDescriptorSizeEXT;
    PFN_vkRegisterCustomBorderColorEXT vkRegisterCustomBorderColorEXT;
    PFN_vkUnregisterCustomBorderColorEXT vkUnregisterCustomBorderColorEXT;
    PFN_vkGetTensorOpaqueCaptureDataARM vkGetTensorOpaqueCaptureDataARM;
    PFN_vkCmdSetSampleLocationsEXT vkCmdSetSampleLocationsEXT;
    PFN_vkGetPhysicalDeviceMultisamplePropertiesEXT vkGetPhysicalDeviceMultisamplePropertiesEXT;
    PFN_vkGetImageMemoryRequirements2KHR vkGetImageMemoryRequirements2KHR;
    PFN_vkGetBufferMemoryRequirements2KHR vkGetBufferMemoryRequirements2KHR;
    PFN_vkGetImageSparseMemoryRequirements2KHR vkGetImageSparseMemoryRequirements2KHR;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
    PFN_vkCmdBuildAccelerationStructuresIndirectKHR vkCmdBuildAccelerationStructuresIndirectKHR;
    PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR;
    PFN_vkCopyAccelerationStructureKHR vkCopyAccelerationStructureKHR;
    PFN_vkCopyAccelerationStructureToMemoryKHR vkCopyAccelerationStructureToMemoryKHR;
    PFN_vkCopyMemoryToAccelerationStructureKHR vkCopyMemoryToAccelerationStructureKHR;
    PFN_vkWriteAccelerationStructuresPropertiesKHR vkWriteAccelerationStructuresPropertiesKHR;
    PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR;
    PFN_vkCmdCopyAccelerationStructureToMemoryKHR vkCmdCopyAccelerationStructureToMemoryKHR;
    PFN_vkCmdCopyMemoryToAccelerationStructureKHR vkCmdCopyMemoryToAccelerationStructureKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR;
    PFN_vkGetDeviceAccelerationStructureCompatibilityKHR vkGetDeviceAccelerationStructureCompatibilityKHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
    PFN_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR vkGetRayTracingCaptureReplayShaderGroupHandlesKHR;
    PFN_vkCmdTraceRaysIndirectKHR vkCmdTraceRaysIndirectKHR;
    PFN_vkGetRayTracingShaderGroupStackSizeKHR vkGetRayTracingShaderGroupStackSizeKHR;
    PFN_vkCmdSetRayTracingPipelineStackSizeKHR vkCmdSetRayTracingPipelineStackSizeKHR;
    PFN_vkCreateSamplerYcbcrConversionKHR vkCreateSamplerYcbcrConversionKHR;
    PFN_vkDestroySamplerYcbcrConversionKHR vkDestroySamplerYcbcrConversionKHR;
    PFN_vkBindBufferMemory2KHR vkBindBufferMemory2KHR;
    PFN_vkBindImageMemory2KHR vkBindImageMemory2KHR;
    PFN_vkGetImageDrmFormatModifierPropertiesEXT vkGetImageDrmFormatModifierPropertiesEXT;
    PFN_vkCreateValidationCacheEXT vkCreateValidationCacheEXT;
    PFN_vkDestroyValidationCacheEXT vkDestroyValidationCacheEXT;
    PFN_vkMergeValidationCachesEXT vkMergeValidationCachesEXT;
    PFN_vkGetValidationCacheDataEXT vkGetValidationCacheDataEXT;
    PFN_vkCmdBindShadingRateImageNV vkCmdBindShadingRateImageNV;
    PFN_vkCmdSetViewportShadingRatePaletteNV vkCmdSetViewportShadingRatePaletteNV;
    PFN_vkCmdSetCoarseSampleOrderNV vkCmdSetCoarseSampleOrderNV;
    PFN_vkCreateAccelerationStructureNV vkCreateAccelerationStructureNV;
    PFN_vkDestroyAccelerationStructureNV vkDestroyAccelerationStructureNV;
    PFN_vkGetAccelerationStructureMemoryRequirementsNV vkGetAccelerationStructureMemoryRequirementsNV;
    PFN_vkBindAccelerationStructureMemoryNV vkBindAccelerationStructureMemoryNV;
    PFN_vkCmdBuildAccelerationStructureNV vkCmdBuildAccelerationStructureNV;
    PFN_vkCmdCopyAccelerationStructureNV vkCmdCopyAccelerationStructureNV;
    PFN_vkCmdTraceRaysNV vkCmdTraceRaysNV;
    PFN_vkCreateRayTracingPipelinesNV vkCreateRayTracingPipelinesNV;
    PFN_vkGetRayTracingShaderGroupHandlesNV vkGetRayTracingShaderGroupHandlesNV;
    PFN_vkGetAccelerationStructureHandleNV vkGetAccelerationStructureHandleNV;
    PFN_vkCmdWriteAccelerationStructuresPropertiesNV vkCmdWriteAccelerationStructuresPropertiesNV;
    PFN_vkCompileDeferredNV vkCompileDeferredNV;
    PFN_vkGetDescriptorSetLayoutSupportKHR vkGetDescriptorSetLayoutSupportKHR;
    PFN_vkCmdDrawIndirectCountKHR vkCmdDrawIndirectCountKHR;
    PFN_vkCmdDrawIndexedIndirectCountKHR vkCmdDrawIndexedIndirectCountKHR;
    PFN_vkCmdWriteBufferMarkerAMD vkCmdWriteBufferMarkerAMD;
    PFN_vkCmdWriteBufferMarker2AMD vkCmdWriteBufferMarker2AMD;
    PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT vkGetPhysicalDeviceCalibrateableTimeDomainsEXT;
    PFN_vkGetCalibratedTimestampsEXT vkGetCalibratedTimestampsEXT;
    PFN_vkCmdDrawMeshTasksNV vkCmdDrawMeshTasksNV;
    PFN_vkCmdDrawMeshTasksIndirectNV vkCmdDrawMeshTasksIndirectNV;
    PFN_vkCmdDrawMeshTasksIndirectCountNV vkCmdDrawMeshTasksIndirectCountNV;
    PFN_vkCmdSetExclusiveScissorEnableNV vkCmdSetExclusiveScissorEnableNV;
    PFN_vkCmdSetExclusiveScissorNV vkCmdSetExclusiveScissorNV;
    PFN_vkCmdSetCheckpointNV vkCmdSetCheckpointNV;
    PFN_vkGetQueueCheckpointDataNV vkGetQueueCheckpointDataNV;
    PFN_vkGetQueueCheckpointData2NV vkGetQueueCheckpointData2NV;
    PFN_vkGetSemaphoreCounterValueKHR vkGetSemaphoreCounterValueKHR;
    PFN_vkWaitSemaphoresKHR vkWaitSemaphoresKHR;
    PFN_vkSignalSemaphoreKHR vkSignalSemaphoreKHR;
    PFN_vkInitializePerformanceApiINTEL vkInitializePerformanceApiINTEL;
    PFN_vkUninitializePerformanceApiINTEL vkUninitializePerformanceApiINTEL;
    PFN_vkCmdSetPerformanceMarkerINTEL vkCmdSetPerformanceMarkerINTEL;
    PFN_vkCmdSetPerformanceStreamMarkerINTEL vkCmdSetPerformanceStreamMarkerINTEL;
    PFN_vkCmdSetPerformanceOverrideINTEL vkCmdSetPerformanceOverrideINTEL;
    PFN_vkAcquirePerformanceConfigurationINTEL vkAcquirePerformanceConfigurationINTEL;
    PFN_vkReleasePerformanceConfigurationINTEL vkReleasePerformanceConfigurationINTEL;
    PFN_vkQueueSetPerformanceConfigurationINTEL vkQueueSetPerformanceConfigurationINTEL;
    PFN_vkGetPerformanceParameterINTEL vkGetPerformanceParameterINTEL;
    PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR vkGetPhysicalDeviceFragmentShadingRatesKHR;
    PFN_vkCmdSetFragmentShadingRateKHR vkCmdSetFragmentShadingRateKHR;
    PFN_vkCmdSetRenderingAttachmentLocationsKHR vkCmdSetRenderingAttachmentLocationsKHR;
    PFN_vkCmdSetRenderingInputAttachmentIndicesKHR vkCmdSetRenderingInputAttachmentIndicesKHR;
    PFN_vkGetBufferDeviceAddressEXT vkGetBufferDeviceAddressEXT;
    PFN_vkGetPhysicalDeviceToolPropertiesEXT vkGetPhysicalDeviceToolPropertiesEXT;
    PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV vkGetPhysicalDeviceCooperativeMatrixPropertiesNV;
    PFN_vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV;
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
    PFN_vkGetBufferOpaqueCaptureAddressKHR vkGetBufferOpaqueCaptureAddressKHR;
    PFN_vkGetDeviceMemoryOpaqueCaptureAddressKHR vkGetDeviceMemoryOpaqueCaptureAddressKHR;
    PFN_vkCmdSetLineStippleEXT vkCmdSetLineStippleEXT;
    PFN_vkResetQueryPoolEXT vkResetQueryPoolEXT;
    PFN_vkCmdSetCullModeEXT vkCmdSetCullModeEXT;
    PFN_vkCmdSetFrontFaceEXT vkCmdSetFrontFaceEXT;
    PFN_vkCmdSetPrimitiveTopologyEXT vkCmdSetPrimitiveTopologyEXT;
    PFN_vkCmdSetViewportWithCountEXT vkCmdSetViewportWithCountEXT;
    PFN_vkCmdSetScissorWithCountEXT vkCmdSetScissorWithCountEXT;
    PFN_vkCmdBindVertexBuffers2EXT vkCmdBindVertexBuffers2EXT;
    PFN_vkCmdSetDepthTestEnableEXT vkCmdSetDepthTestEnableEXT;
    PFN_vkCmdSetDepthWriteEnableEXT vkCmdSetDepthWriteEnableEXT;
    PFN_vkCmdSetDepthCompareOpEXT vkCmdSetDepthCompareOpEXT;
    PFN_vkCmdSetDepthBoundsTestEnableEXT vkCmdSetDepthBoundsTestEnableEXT;
    PFN_vkCmdSetStencilTestEnableEXT vkCmdSetStencilTestEnableEXT;
    PFN_vkCmdSetStencilOpEXT vkCmdSetStencilOpEXT;
    PFN_vkCreateDeferredOperationKHR vkCreateDeferredOperationKHR;
    PFN_vkDestroyDeferredOperationKHR vkDestroyDeferredOperationKHR;
    PFN_vkGetDeferredOperationMaxConcurrencyKHR vkGetDeferredOperationMaxConcurrencyKHR;
    PFN_vkGetDeferredOperationResultKHR vkGetDeferredOperationResultKHR;
    PFN_vkDeferredOperationJoinKHR vkDeferredOperationJoinKHR;
    PFN_vkGetPipelineExecutablePropertiesKHR vkGetPipelineExecutablePropertiesKHR;
    PFN_vkGetPipelineExecutableStatisticsKHR vkGetPipelineExecutableStatisticsKHR;
    PFN_vkGetPipelineExecutableInternalRepresentationsKHR vkGetPipelineExecutableInternalRepresentationsKHR;
    PFN_vkCopyMemoryToImageEXT vkCopyMemoryToImageEXT;
    PFN_vkCopyImageToMemoryEXT vkCopyImageToMemoryEXT;
    PFN_vkCopyImageToImageEXT vkCopyImageToImageEXT;
    PFN_vkTransitionImageLayoutEXT vkTransitionImageLayoutEXT;
    PFN_vkGetImageSubresourceLayout2EXT vkGetImageSubresourceLayout2EXT;
    PFN_vkMapMemory2KHR vkMapMemory2KHR;
    PFN_vkUnmapMemory2KHR vkUnmapMemory2KHR;
    PFN_vkGetGeneratedCommandsMemoryRequirementsNV vkGetGeneratedCommandsMemoryRequirementsNV;
    PFN_vkCmdPreprocessGeneratedCommandsNV vkCmdPreprocessGeneratedCommandsNV;
    PFN_vkCmdExecuteGeneratedCommandsNV vkCmdExecuteGeneratedCommandsNV;
    PFN_vkCmdBindPipelineShaderGroupNV vkCmdBindPipelineShaderGroupNV;
    PFN_vkCreateIndirectCommandsLayoutNV vkCreateIndirectCommandsLayoutNV;
    PFN_vkDestroyIndirectCommandsLayoutNV vkDestroyIndirectCommandsLayoutNV;
    PFN_vkCmdSetDepthBias2EXT vkCmdSetDepthBias2EXT;
    PFN_vkCreatePrivateDataSlotEXT vkCreatePrivateDataSlotEXT;
    PFN_vkDestroyPrivateDataSlotEXT vkDestroyPrivateDataSlotEXT;
    PFN_vkSetPrivateDataEXT vkSetPrivateDataEXT;
    PFN_vkGetPrivateDataEXT vkGetPrivateDataEXT;
    PFN_vkQueueSetPerfHintQCOM vkQueueSetPerfHintQCOM;
    PFN_vkSetLatencySleepModeLegacyNV vkSetLatencySleepModeLegacyNV;
    PFN_vkLatencySleepLegacyNV vkLatencySleepLegacyNV;
    PFN_vkSetLatencyMarkerLegacyNV vkSetLatencyMarkerLegacyNV;
    PFN_vkGetLatencyTimingsLegacyNV vkGetLatencyTimingsLegacyNV;
    PFN_vkQueueNotifyOutOfBandLegacyNV vkQueueNotifyOutOfBandLegacyNV;
    PFN_vkGetSleepStatusLegacyNV vkGetSleepStatusLegacyNV;
    PFN_vkShutdownLatencyDeviceLegacyNV vkShutdownLatencyDeviceLegacyNV;
    PFN_vkCmdSetEvent2KHR vkCmdSetEvent2KHR;
    PFN_vkCmdResetEvent2KHR vkCmdResetEvent2KHR;
    PFN_vkCmdWaitEvents2KHR vkCmdWaitEvents2KHR;
    PFN_vkCmdPipelineBarrier2KHR vkCmdPipelineBarrier2KHR;
    PFN_vkCmdWriteTimestamp2KHR vkCmdWriteTimestamp2KHR;
    PFN_vkQueueSubmit2KHR vkQueueSubmit2KHR;
    PFN_vkGetDescriptorSetLayoutSizeEXT vkGetDescriptorSetLayoutSizeEXT;
    PFN_vkGetDescriptorSetLayoutBindingOffsetEXT vkGetDescriptorSetLayoutBindingOffsetEXT;
    PFN_vkGetDescriptorEXT vkGetDescriptorEXT;
    PFN_vkCmdBindDescriptorBuffersEXT vkCmdBindDescriptorBuffersEXT;
    PFN_vkCmdSetDescriptorBufferOffsetsEXT vkCmdSetDescriptorBufferOffsetsEXT;
    PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT vkCmdBindDescriptorBufferEmbeddedSamplersEXT;
    PFN_vkGetBufferOpaqueCaptureDescriptorDataEXT vkGetBufferOpaqueCaptureDescriptorDataEXT;
    PFN_vkGetImageOpaqueCaptureDescriptorDataEXT vkGetImageOpaqueCaptureDescriptorDataEXT;
    PFN_vkGetImageViewOpaqueCaptureDescriptorDataEXT vkGetImageViewOpaqueCaptureDescriptorDataEXT;
    PFN_vkGetSamplerOpaqueCaptureDescriptorDataEXT vkGetSamplerOpaqueCaptureDescriptorDataEXT;
    PFN_vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT;
    PFN_vkCmdBindIndexBuffer3KHR vkCmdBindIndexBuffer3KHR;
    PFN_vkCmdBindVertexBuffers3KHR vkCmdBindVertexBuffers3KHR;
    PFN_vkCmdDrawIndirect2KHR vkCmdDrawIndirect2KHR;
    PFN_vkCmdDrawIndexedIndirect2KHR vkCmdDrawIndexedIndirect2KHR;
    PFN_vkCmdDispatchIndirect2KHR vkCmdDispatchIndirect2KHR;
    PFN_vkCmdCopyMemoryKHR vkCmdCopyMemoryKHR;
    PFN_vkCmdCopyMemoryToImageKHR vkCmdCopyMemoryToImageKHR;
    PFN_vkCmdCopyImageToMemoryKHR vkCmdCopyImageToMemoryKHR;
    PFN_vkCmdUpdateMemoryKHR vkCmdUpdateMemoryKHR;
    PFN_vkCmdFillMemoryKHR vkCmdFillMemoryKHR;
    PFN_vkCmdCopyQueryPoolResultsToMemoryKHR vkCmdCopyQueryPoolResultsToMemoryKHR;
    PFN_vkCmdDrawIndirectCount2KHR vkCmdDrawIndirectCount2KHR;
    PFN_vkCmdDrawIndexedIndirectCount2KHR vkCmdDrawIndexedIndirectCount2KHR;
    PFN_vkCmdBeginConditionalRendering2EXT vkCmdBeginConditionalRendering2EXT;
    PFN_vkCmdBindTransformFeedbackBuffers2EXT vkCmdBindTransformFeedbackBuffers2EXT;
    PFN_vkCmdBeginTransformFeedback2EXT vkCmdBeginTransformFeedback2EXT;
    PFN_vkCmdEndTransformFeedback2EXT vkCmdEndTransformFeedback2EXT;
    PFN_vkCmdDrawIndirectByteCount2EXT vkCmdDrawIndirectByteCount2EXT;
    PFN_vkCmdDrawMeshTasksIndirect2EXT vkCmdDrawMeshTasksIndirect2EXT;
    PFN_vkCmdDrawMeshTasksIndirectCount2EXT vkCmdDrawMeshTasksIndirectCount2EXT;
    PFN_vkCmdWriteMarkerToMemoryAMD vkCmdWriteMarkerToMemoryAMD;
    PFN_vkCreateAccelerationStructure2KHR vkCreateAccelerationStructure2KHR;
    PFN_vkCmdSetFragmentShadingRateEnumNV vkCmdSetFragmentShadingRateEnumNV;
    PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT;
    PFN_vkCmdDrawMeshTasksIndirectEXT vkCmdDrawMeshTasksIndirectEXT;
    PFN_vkCmdDrawMeshTasksIndirectCountEXT vkCmdDrawMeshTasksIndirectCountEXT;
    PFN_vkCmdCopyBuffer2KHR vkCmdCopyBuffer2KHR;
    PFN_vkCmdCopyImage2KHR vkCmdCopyImage2KHR;
    PFN_vkCmdCopyBufferToImage2KHR vkCmdCopyBufferToImage2KHR;
    PFN_vkCmdCopyImageToBuffer2KHR vkCmdCopyImageToBuffer2KHR;
    PFN_vkCmdBlitImage2KHR vkCmdBlitImage2KHR;
    PFN_vkCmdResolveImage2KHR vkCmdResolveImage2KHR;
    PFN_vkGetDeviceFaultInfoEXT vkGetDeviceFaultInfoEXT;
    PFN_vkCmdSetVertexInputEXT vkCmdSetVertexInputEXT;
    PFN_vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI;
    PFN_vkCmdSubpassShadingHUAWEI vkCmdSubpassShadingHUAWEI;
    PFN_vkCmdBindInvocationMaskHUAWEI vkCmdBindInvocationMaskHUAWEI;
    PFN_vkGetMemoryRemoteAddressNV vkGetMemoryRemoteAddressNV;
    PFN_vkGetPipelinePropertiesEXT vkGetPipelinePropertiesEXT;
    PFN_vkCmdSetPatchControlPointsEXT vkCmdSetPatchControlPointsEXT;
    PFN_vkCmdSetRasterizerDiscardEnableEXT vkCmdSetRasterizerDiscardEnableEXT;
    PFN_vkCmdSetDepthBiasEnableEXT vkCmdSetDepthBiasEnableEXT;
    PFN_vkCmdSetLogicOpEXT vkCmdSetLogicOpEXT;
    PFN_vkCmdSetPrimitiveRestartEnableEXT vkCmdSetPrimitiveRestartEnableEXT;
    PFN_vkCmdSetColorWriteEnableEXT vkCmdSetColorWriteEnableEXT;
    PFN_vkCmdTraceRaysIndirect2KHR vkCmdTraceRaysIndirect2KHR;
    PFN_vkCmdDrawMultiEXT vkCmdDrawMultiEXT;
    PFN_vkCmdDrawMultiIndexedEXT vkCmdDrawMultiIndexedEXT;
    PFN_vkCreateMicromapEXT vkCreateMicromapEXT;
    PFN_vkDestroyMicromapEXT vkDestroyMicromapEXT;
    PFN_vkCmdBuildMicromapsEXT vkCmdBuildMicromapsEXT;
    PFN_vkBuildMicromapsEXT vkBuildMicromapsEXT;
    PFN_vkCopyMicromapEXT vkCopyMicromapEXT;
    PFN_vkCopyMicromapToMemoryEXT vkCopyMicromapToMemoryEXT;
    PFN_vkCopyMemoryToMicromapEXT vkCopyMemoryToMicromapEXT;
    PFN_vkWriteMicromapsPropertiesEXT vkWriteMicromapsPropertiesEXT;
    PFN_vkCmdCopyMicromapEXT vkCmdCopyMicromapEXT;
    PFN_vkCmdCopyMicromapToMemoryEXT vkCmdCopyMicromapToMemoryEXT;
    PFN_vkCmdCopyMemoryToMicromapEXT vkCmdCopyMemoryToMicromapEXT;
    PFN_vkCmdWriteMicromapsPropertiesEXT vkCmdWriteMicromapsPropertiesEXT;
    PFN_vkGetDeviceMicromapCompatibilityEXT vkGetDeviceMicromapCompatibilityEXT;
    PFN_vkGetMicromapBuildSizesEXT vkGetMicromapBuildSizesEXT;
    PFN_vkCmdDrawClusterHUAWEI vkCmdDrawClusterHUAWEI;
    PFN_vkCmdDrawClusterIndirectHUAWEI vkCmdDrawClusterIndirectHUAWEI;
    PFN_vkSetDeviceMemoryPriorityEXT vkSetDeviceMemoryPriorityEXT;
    PFN_vkGetDeviceBufferMemoryRequirementsKHR vkGetDeviceBufferMemoryRequirementsKHR;
    PFN_vkGetDeviceImageMemoryRequirementsKHR vkGetDeviceImageMemoryRequirementsKHR;
    PFN_vkGetDeviceImageSparseMemoryRequirementsKHR vkGetDeviceImageSparseMemoryRequirementsKHR;
    PFN_vkCmdSetDispatchParametersARM vkCmdSetDispatchParametersARM;
    PFN_vkGetDescriptorSetLayoutHostMappingInfoVALVE vkGetDescriptorSetLayoutHostMappingInfoVALVE;
    PFN_vkGetDescriptorSetHostMappingVALVE vkGetDescriptorSetHostMappingVALVE;
    PFN_vkCmdCopyMemoryIndirectNV vkCmdCopyMemoryIndirectNV;
    PFN_vkCmdCopyMemoryToImageIndirectNV vkCmdCopyMemoryToImageIndirectNV;
    PFN_vkCmdDecompressMemoryNV vkCmdDecompressMemoryNV;
    PFN_vkCmdDecompressMemoryIndirectCountNV vkCmdDecompressMemoryIndirectCountNV;
    PFN_vkGetPipelineIndirectMemoryRequirementsNV vkGetPipelineIndirectMemoryRequirementsNV;
    PFN_vkCmdUpdatePipelineIndirectBufferNV vkCmdUpdatePipelineIndirectBufferNV;
    PFN_vkGetPipelineIndirectDeviceAddressNV vkGetPipelineIndirectDeviceAddressNV;
    PFN_vkCmdSetDepthClampEnableEXT vkCmdSetDepthClampEnableEXT;
    PFN_vkCmdSetPolygonModeEXT vkCmdSetPolygonModeEXT;
    PFN_vkCmdSetRasterizationSamplesEXT vkCmdSetRasterizationSamplesEXT;
    PFN_vkCmdSetSampleMaskEXT vkCmdSetSampleMaskEXT;
    PFN_vkCmdSetAlphaToCoverageEnableEXT vkCmdSetAlphaToCoverageEnableEXT;
    PFN_vkCmdSetAlphaToOneEnableEXT vkCmdSetAlphaToOneEnableEXT;
    PFN_vkCmdSetLogicOpEnableEXT vkCmdSetLogicOpEnableEXT;
    PFN_vkCmdSetColorBlendEnableEXT vkCmdSetColorBlendEnableEXT;
    PFN_vkCmdSetColorBlendEquationEXT vkCmdSetColorBlendEquationEXT;
    PFN_vkCmdSetColorWriteMaskEXT vkCmdSetColorWriteMaskEXT;
    PFN_vkCmdSetTessellationDomainOriginEXT vkCmdSetTessellationDomainOriginEXT;
    PFN_vkCmdSetRasterizationStreamEXT vkCmdSetRasterizationStreamEXT;
    PFN_vkCmdSetConservativeRasterizationModeEXT vkCmdSetConservativeRasterizationModeEXT;
    PFN_vkCmdSetExtraPrimitiveOverestimationSizeEXT vkCmdSetExtraPrimitiveOverestimationSizeEXT;
    PFN_vkCmdSetDepthClipEnableEXT vkCmdSetDepthClipEnableEXT;
    PFN_vkCmdSetSampleLocationsEnableEXT vkCmdSetSampleLocationsEnableEXT;
    PFN_vkCmdSetColorBlendAdvancedEXT vkCmdSetColorBlendAdvancedEXT;
    PFN_vkCmdSetProvokingVertexModeEXT vkCmdSetProvokingVertexModeEXT;
    PFN_vkCmdSetLineRasterizationModeEXT vkCmdSetLineRasterizationModeEXT;
    PFN_vkCmdSetLineStippleEnableEXT vkCmdSetLineStippleEnableEXT;
    PFN_vkCmdSetDepthClipNegativeOneToOneEXT vkCmdSetDepthClipNegativeOneToOneEXT;
    PFN_vkCmdSetViewportWScalingEnableNV vkCmdSetViewportWScalingEnableNV;
    PFN_vkCmdSetViewportSwizzleNV vkCmdSetViewportSwizzleNV;
    PFN_vkCmdSetCoverageToColorEnableNV vkCmdSetCoverageToColorEnableNV;
    PFN_vkCmdSetCoverageToColorLocationNV vkCmdSetCoverageToColorLocationNV;
    PFN_vkCmdSetCoverageModulationModeNV vkCmdSetCoverageModulationModeNV;
    PFN_vkCmdSetCoverageModulationTableEnableNV vkCmdSetCoverageModulationTableEnableNV;
    PFN_vkCmdSetCoverageModulationTableNV vkCmdSetCoverageModulationTableNV;
    PFN_vkCmdSetShadingRateImageEnableNV vkCmdSetShadingRateImageEnableNV;
    PFN_vkCmdSetRepresentativeFragmentTestEnableNV vkCmdSetRepresentativeFragmentTestEnableNV;
    PFN_vkCmdSetCoverageReductionModeNV vkCmdSetCoverageReductionModeNV;
    PFN_vkGetShaderModuleIdentifierEXT vkGetShaderModuleIdentifierEXT;
    PFN_vkGetShaderModuleCreateInfoIdentifierEXT vkGetShaderModuleCreateInfoIdentifierEXT;
    PFN_vkGetPhysicalDeviceOpticalFlowImageFormatsNV vkGetPhysicalDeviceOpticalFlowImageFormatsNV;
    PFN_vkCreateOpticalFlowSessionNV vkCreateOpticalFlowSessionNV;
    PFN_vkDestroyOpticalFlowSessionNV vkDestroyOpticalFlowSessionNV;
    PFN_vkBindOpticalFlowSessionImageNV vkBindOpticalFlowSessionImageNV;
    PFN_vkCmdOpticalFlowExecuteNV vkCmdOpticalFlowExecuteNV;
    PFN_vkCmdBindIndexBuffer2KHR vkCmdBindIndexBuffer2KHR;
    PFN_vkGetRenderingAreaGranularityKHR vkGetRenderingAreaGranularityKHR;
    PFN_vkGetDeviceImageSubresourceLayoutKHR vkGetDeviceImageSubresourceLayoutKHR;
    PFN_vkGetImageSubresourceLayout2KHR vkGetImageSubresourceLayout2KHR;
    PFN_vkCreateShadersEXT vkCreateShadersEXT;
    PFN_vkDestroyShaderEXT vkDestroyShaderEXT;
    PFN_vkGetShaderBinaryDataEXT vkGetShaderBinaryDataEXT;
    PFN_vkCmdBindShadersEXT vkCmdBindShadersEXT;
    PFN_vkCmdSetDepthClampRangeEXT vkCmdSetDepthClampRangeEXT;
    PFN_vkGetPhysicalDeviceCooperativeVectorPropertiesNV vkGetPhysicalDeviceCooperativeVectorPropertiesNV;
    PFN_vkConvertCooperativeVectorMatrixNV vkConvertCooperativeVectorMatrixNV;
    PFN_vkCmdConvertCooperativeVectorMatrixNV vkCmdConvertCooperativeVectorMatrixNV;
    PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR;
    PFN_vkCmdSetAttachmentFeedbackLoopEnableEXT vkCmdSetAttachmentFeedbackLoopEnableEXT;
    PFN_vkCmdSetLineStippleKHR vkCmdSetLineStippleKHR;
    PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR vkGetPhysicalDeviceCalibrateableTimeDomainsKHR;
    PFN_vkGetCalibratedTimestampsKHR vkGetCalibratedTimestampsKHR;
    PFN_vkCmdBindDescriptorSets2KHR vkCmdBindDescriptorSets2KHR;
    PFN_vkCmdPushConstants2KHR vkCmdPushConstants2KHR;
    PFN_vkCmdPushDescriptorSet2KHR vkCmdPushDescriptorSet2KHR;
    PFN_vkCmdPushDescriptorSetWithTemplate2KHR vkCmdPushDescriptorSetWithTemplate2KHR;
    PFN_vkCmdSetDescriptorBufferOffsets2EXT vkCmdSetDescriptorBufferOffsets2EXT;
    PFN_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT vkCmdBindDescriptorBufferEmbeddedSamplers2EXT;
    PFN_vkCmdCopyMemoryIndirectKHR vkCmdCopyMemoryIndirectKHR;
    PFN_vkCmdCopyMemoryToImageIndirectKHR vkCmdCopyMemoryToImageIndirectKHR;
    PFN_vkCmdDecompressMemoryEXT vkCmdDecompressMemoryEXT;
    PFN_vkCmdDecompressMemoryIndirectCountEXT vkCmdDecompressMemoryIndirectCountEXT;
    PFN_vkGetClusterAccelerationStructureBuildSizesNV vkGetClusterAccelerationStructureBuildSizesNV;
    PFN_vkCmdBuildClusterAccelerationStructureIndirectNV vkCmdBuildClusterAccelerationStructureIndirectNV;
    PFN_vkGetPartitionedAccelerationStructuresBuildSizesNV vkGetPartitionedAccelerationStructuresBuildSizesNV;
    PFN_vkCmdBuildPartitionedAccelerationStructuresNV vkCmdBuildPartitionedAccelerationStructuresNV;
    PFN_vkGetGeneratedCommandsMemoryRequirementsEXT vkGetGeneratedCommandsMemoryRequirementsEXT;
    PFN_vkCmdPreprocessGeneratedCommandsEXT vkCmdPreprocessGeneratedCommandsEXT;
    PFN_vkCmdExecuteGeneratedCommandsEXT vkCmdExecuteGeneratedCommandsEXT;
    PFN_vkCreateIndirectCommandsLayoutEXT vkCreateIndirectCommandsLayoutEXT;
    PFN_vkDestroyIndirectCommandsLayoutEXT vkDestroyIndirectCommandsLayoutEXT;
    PFN_vkCreateIndirectExecutionSetEXT vkCreateIndirectExecutionSetEXT;
    PFN_vkDestroyIndirectExecutionSetEXT vkDestroyIndirectExecutionSetEXT;
    PFN_vkUpdateIndirectExecutionSetPipelineEXT vkUpdateIndirectExecutionSetPipelineEXT;
    PFN_vkUpdateIndirectExecutionSetShaderEXT vkUpdateIndirectExecutionSetShaderEXT;
    PFN_vkGetDeviceFaultReportsKHR vkGetDeviceFaultReportsKHR;
    PFN_vkGetDeviceFaultDebugInfoKHR vkGetDeviceFaultDebugInfoKHR;
    PFN_vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV;
    PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM;
    PFN_vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM;
    PFN_vkCreateShaderInstrumentationARM vkCreateShaderInstrumentationARM;
    PFN_vkDestroyShaderInstrumentationARM vkDestroyShaderInstrumentationARM;
    PFN_vkCmdBeginShaderInstrumentationARM vkCmdBeginShaderInstrumentationARM;
    PFN_vkCmdEndShaderInstrumentationARM vkCmdEndShaderInstrumentationARM;
    PFN_vkGetShaderInstrumentationValuesARM vkGetShaderInstrumentationValuesARM;
    PFN_vkClearShaderInstrumentationMetricsARM vkClearShaderInstrumentationMetricsARM;
    PFN_vkCmdEndRendering2EXT vkCmdEndRendering2EXT;
    PFN_vkCmdBeginCustomResolveEXT vkCmdBeginCustomResolveEXT;
    PFN_vkCmdEndRendering2KHR vkCmdEndRendering2KHR;
    PFN_vkCmdSetComputeOccupancyPriorityNV vkCmdSetComputeOccupancyPriorityNV;
    PFN_vkGetPhysicalDeviceCooperativeMatrixProperties2EXT vkGetPhysicalDeviceCooperativeMatrixProperties2EXT;
    PFN_vkCmdSetPrimitiveRestartIndexEXT vkCmdSetPrimitiveRestartIndexEXT;
} MvkDispatch;

/* the kinds of device objects the host destroys for a program, in the order it does. */
enum {
    MVK_KIND_NONE,
    MVK_KIND_VkAccelerationStructureKHR,
    MVK_KIND_VkAccelerationStructureNV,
    MVK_KIND_VkBuffer,
    MVK_KIND_VkBufferView,
    MVK_KIND_VkDeferredOperationKHR,
    MVK_KIND_VkDescriptorSetLayout,
    MVK_KIND_VkDescriptorUpdateTemplate,
    MVK_KIND_VkEvent,
    MVK_KIND_VkFence,
    MVK_KIND_VkFramebuffer,
    MVK_KIND_VkGpaSessionAMD,
    MVK_KIND_VkImage,
    MVK_KIND_VkImageView,
    MVK_KIND_VkIndirectCommandsLayoutEXT,
    MVK_KIND_VkIndirectCommandsLayoutNV,
    MVK_KIND_VkIndirectExecutionSetEXT,
    MVK_KIND_VkMicromapEXT,
    MVK_KIND_VkOpticalFlowSessionNV,
    MVK_KIND_VkPipeline,
    MVK_KIND_VkPipelineCache,
    MVK_KIND_VkPipelineLayout,
    MVK_KIND_VkPrivateDataSlot,
    MVK_KIND_VkRenderPass,
    MVK_KIND_VkSampler,
    MVK_KIND_VkSamplerYcbcrConversion,
    MVK_KIND_VkSemaphore,
    MVK_KIND_VkShaderEXT,
    MVK_KIND_VkShaderInstrumentationARM,
    MVK_KIND_VkShaderModule,
    MVK_KIND_VkValidationCacheEXT,
    MVK_KIND_VkCommandPool,
    MVK_KIND_VkDescriptorPool,
    MVK_KIND_VkQueryPool,
    MVK_KIND_VkDeviceMemory,
    MVK_KIND_COUNT
};

static inline void mvk_destroy_kind(const MvkDispatch *t, VkDevice device, int kind, uint64_t handle)
{
    switch (kind) {
    case MVK_KIND_VkAccelerationStructureKHR:
        if (t->vkDestroyAccelerationStructureKHR)
            t->vkDestroyAccelerationStructureKHR(device, (VkAccelerationStructureKHR)handle, NULL);
        break;
    case MVK_KIND_VkAccelerationStructureNV:
        if (t->vkDestroyAccelerationStructureNV)
            t->vkDestroyAccelerationStructureNV(device, (VkAccelerationStructureNV)handle, NULL);
        break;
    case MVK_KIND_VkBuffer:
        if (t->vkDestroyBuffer)
            t->vkDestroyBuffer(device, (VkBuffer)handle, NULL);
        break;
    case MVK_KIND_VkBufferView:
        if (t->vkDestroyBufferView)
            t->vkDestroyBufferView(device, (VkBufferView)handle, NULL);
        break;
    case MVK_KIND_VkDeferredOperationKHR:
        if (t->vkDestroyDeferredOperationKHR)
            t->vkDestroyDeferredOperationKHR(device, (VkDeferredOperationKHR)handle, NULL);
        break;
    case MVK_KIND_VkDescriptorSetLayout:
        if (t->vkDestroyDescriptorSetLayout)
            t->vkDestroyDescriptorSetLayout(device, (VkDescriptorSetLayout)handle, NULL);
        break;
    case MVK_KIND_VkDescriptorUpdateTemplate:
        if (t->vkDestroyDescriptorUpdateTemplate)
            t->vkDestroyDescriptorUpdateTemplate(device, (VkDescriptorUpdateTemplate)handle, NULL);
        break;
    case MVK_KIND_VkEvent:
        if (t->vkDestroyEvent)
            t->vkDestroyEvent(device, (VkEvent)handle, NULL);
        break;
    case MVK_KIND_VkFence:
        if (t->vkDestroyFence)
            t->vkDestroyFence(device, (VkFence)handle, NULL);
        break;
    case MVK_KIND_VkFramebuffer:
        if (t->vkDestroyFramebuffer)
            t->vkDestroyFramebuffer(device, (VkFramebuffer)handle, NULL);
        break;
    case MVK_KIND_VkGpaSessionAMD:
        if (t->vkDestroyGpaSessionAMD)
            t->vkDestroyGpaSessionAMD(device, (VkGpaSessionAMD)handle, NULL);
        break;
    case MVK_KIND_VkImage:
        if (t->vkDestroyImage)
            t->vkDestroyImage(device, (VkImage)handle, NULL);
        break;
    case MVK_KIND_VkImageView:
        if (t->vkDestroyImageView)
            t->vkDestroyImageView(device, (VkImageView)handle, NULL);
        break;
    case MVK_KIND_VkIndirectCommandsLayoutEXT:
        if (t->vkDestroyIndirectCommandsLayoutEXT)
            t->vkDestroyIndirectCommandsLayoutEXT(device, (VkIndirectCommandsLayoutEXT)handle, NULL);
        break;
    case MVK_KIND_VkIndirectCommandsLayoutNV:
        if (t->vkDestroyIndirectCommandsLayoutNV)
            t->vkDestroyIndirectCommandsLayoutNV(device, (VkIndirectCommandsLayoutNV)handle, NULL);
        break;
    case MVK_KIND_VkIndirectExecutionSetEXT:
        if (t->vkDestroyIndirectExecutionSetEXT)
            t->vkDestroyIndirectExecutionSetEXT(device, (VkIndirectExecutionSetEXT)handle, NULL);
        break;
    case MVK_KIND_VkMicromapEXT:
        if (t->vkDestroyMicromapEXT)
            t->vkDestroyMicromapEXT(device, (VkMicromapEXT)handle, NULL);
        break;
    case MVK_KIND_VkOpticalFlowSessionNV:
        if (t->vkDestroyOpticalFlowSessionNV)
            t->vkDestroyOpticalFlowSessionNV(device, (VkOpticalFlowSessionNV)handle, NULL);
        break;
    case MVK_KIND_VkPipeline:
        if (t->vkDestroyPipeline)
            t->vkDestroyPipeline(device, (VkPipeline)handle, NULL);
        break;
    case MVK_KIND_VkPipelineCache:
        if (t->vkDestroyPipelineCache)
            t->vkDestroyPipelineCache(device, (VkPipelineCache)handle, NULL);
        break;
    case MVK_KIND_VkPipelineLayout:
        if (t->vkDestroyPipelineLayout)
            t->vkDestroyPipelineLayout(device, (VkPipelineLayout)handle, NULL);
        break;
    case MVK_KIND_VkPrivateDataSlot:
        if (t->vkDestroyPrivateDataSlot)
            t->vkDestroyPrivateDataSlot(device, (VkPrivateDataSlot)handle, NULL);
        break;
    case MVK_KIND_VkRenderPass:
        if (t->vkDestroyRenderPass)
            t->vkDestroyRenderPass(device, (VkRenderPass)handle, NULL);
        break;
    case MVK_KIND_VkSampler:
        if (t->vkDestroySampler)
            t->vkDestroySampler(device, (VkSampler)handle, NULL);
        break;
    case MVK_KIND_VkSamplerYcbcrConversion:
        if (t->vkDestroySamplerYcbcrConversion)
            t->vkDestroySamplerYcbcrConversion(device, (VkSamplerYcbcrConversion)handle, NULL);
        break;
    case MVK_KIND_VkSemaphore:
        if (t->vkDestroySemaphore)
            t->vkDestroySemaphore(device, (VkSemaphore)handle, NULL);
        break;
    case MVK_KIND_VkShaderEXT:
        if (t->vkDestroyShaderEXT)
            t->vkDestroyShaderEXT(device, (VkShaderEXT)handle, NULL);
        break;
    case MVK_KIND_VkShaderInstrumentationARM:
        if (t->vkDestroyShaderInstrumentationARM)
            t->vkDestroyShaderInstrumentationARM(device, (VkShaderInstrumentationARM)handle, NULL);
        break;
    case MVK_KIND_VkShaderModule:
        if (t->vkDestroyShaderModule)
            t->vkDestroyShaderModule(device, (VkShaderModule)handle, NULL);
        break;
    case MVK_KIND_VkValidationCacheEXT:
        if (t->vkDestroyValidationCacheEXT)
            t->vkDestroyValidationCacheEXT(device, (VkValidationCacheEXT)handle, NULL);
        break;
    case MVK_KIND_VkCommandPool:
        if (t->vkDestroyCommandPool)
            t->vkDestroyCommandPool(device, (VkCommandPool)handle, NULL);
        break;
    case MVK_KIND_VkDescriptorPool:
        if (t->vkDestroyDescriptorPool)
            t->vkDestroyDescriptorPool(device, (VkDescriptorPool)handle, NULL);
        break;
    case MVK_KIND_VkQueryPool:
        if (t->vkDestroyQueryPool)
            t->vkDestroyQueryPool(device, (VkQueryPool)handle, NULL);
        break;
    case MVK_KIND_VkDeviceMemory:
        if (t->vkFreeMemory)
            t->vkFreeMemory(device, (VkDeviceMemory)handle, NULL);
        break;
    default:
        break;
    }
}

/* fills every entry the instance or device has; only device level commands come from the device. */
static inline void mvk_load_dispatch(MvkDispatch *t, PFN_vkGetInstanceProcAddr gipa, VkInstance instance,
                                     PFN_vkGetDeviceProcAddr gdpa, VkDevice device)
{
    t->vkCreateInstance = (PFN_vkCreateInstance)gipa(instance, "vkCreateInstance");
    t->vkDestroyInstance = (PFN_vkDestroyInstance)gipa(instance, "vkDestroyInstance");
    t->vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
    t->vkGetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)gipa(instance, "vkGetPhysicalDeviceFeatures");
    t->vkGetPhysicalDeviceFormatProperties = (PFN_vkGetPhysicalDeviceFormatProperties)gipa(instance, "vkGetPhysicalDeviceFormatProperties");
    t->vkGetPhysicalDeviceImageFormatProperties = (PFN_vkGetPhysicalDeviceImageFormatProperties)gipa(instance, "vkGetPhysicalDeviceImageFormatProperties");
    t->vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)gipa(instance, "vkGetPhysicalDeviceProperties");
    t->vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    t->vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)gipa(instance, "vkGetPhysicalDeviceMemoryProperties");
    t->vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)gipa(instance, "vkGetInstanceProcAddr");
    t->vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)gipa(instance, "vkGetDeviceProcAddr");
    t->vkCreateDevice = (PFN_vkCreateDevice)gipa(instance, "vkCreateDevice");
    t->vkDestroyDevice = (PFN_vkDestroyDevice)(device ? gdpa(device, "vkDestroyDevice") : NULL);
    if (!t->vkDestroyDevice)
        t->vkDestroyDevice = (PFN_vkDestroyDevice)gipa(instance, "vkDestroyDevice");
    t->vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)gipa(instance, "vkEnumerateInstanceExtensionProperties");
    t->vkEnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)gipa(instance, "vkEnumerateDeviceExtensionProperties");
    t->vkEnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties)gipa(instance, "vkEnumerateInstanceLayerProperties");
    t->vkEnumerateDeviceLayerProperties = (PFN_vkEnumerateDeviceLayerProperties)gipa(instance, "vkEnumerateDeviceLayerProperties");
    t->vkGetDeviceQueue = (PFN_vkGetDeviceQueue)(device ? gdpa(device, "vkGetDeviceQueue") : NULL);
    if (!t->vkGetDeviceQueue)
        t->vkGetDeviceQueue = (PFN_vkGetDeviceQueue)gipa(instance, "vkGetDeviceQueue");
    t->vkQueueSubmit = (PFN_vkQueueSubmit)(device ? gdpa(device, "vkQueueSubmit") : NULL);
    if (!t->vkQueueSubmit)
        t->vkQueueSubmit = (PFN_vkQueueSubmit)gipa(instance, "vkQueueSubmit");
    t->vkQueueWaitIdle = (PFN_vkQueueWaitIdle)(device ? gdpa(device, "vkQueueWaitIdle") : NULL);
    if (!t->vkQueueWaitIdle)
        t->vkQueueWaitIdle = (PFN_vkQueueWaitIdle)gipa(instance, "vkQueueWaitIdle");
    t->vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)(device ? gdpa(device, "vkDeviceWaitIdle") : NULL);
    if (!t->vkDeviceWaitIdle)
        t->vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)gipa(instance, "vkDeviceWaitIdle");
    t->vkAllocateMemory = (PFN_vkAllocateMemory)(device ? gdpa(device, "vkAllocateMemory") : NULL);
    if (!t->vkAllocateMemory)
        t->vkAllocateMemory = (PFN_vkAllocateMemory)gipa(instance, "vkAllocateMemory");
    t->vkFreeMemory = (PFN_vkFreeMemory)(device ? gdpa(device, "vkFreeMemory") : NULL);
    if (!t->vkFreeMemory)
        t->vkFreeMemory = (PFN_vkFreeMemory)gipa(instance, "vkFreeMemory");
    t->vkMapMemory = (PFN_vkMapMemory)(device ? gdpa(device, "vkMapMemory") : NULL);
    if (!t->vkMapMemory)
        t->vkMapMemory = (PFN_vkMapMemory)gipa(instance, "vkMapMemory");
    t->vkUnmapMemory = (PFN_vkUnmapMemory)(device ? gdpa(device, "vkUnmapMemory") : NULL);
    if (!t->vkUnmapMemory)
        t->vkUnmapMemory = (PFN_vkUnmapMemory)gipa(instance, "vkUnmapMemory");
    t->vkFlushMappedMemoryRanges = (PFN_vkFlushMappedMemoryRanges)(device ? gdpa(device, "vkFlushMappedMemoryRanges") : NULL);
    if (!t->vkFlushMappedMemoryRanges)
        t->vkFlushMappedMemoryRanges = (PFN_vkFlushMappedMemoryRanges)gipa(instance, "vkFlushMappedMemoryRanges");
    t->vkInvalidateMappedMemoryRanges = (PFN_vkInvalidateMappedMemoryRanges)(device ? gdpa(device, "vkInvalidateMappedMemoryRanges") : NULL);
    if (!t->vkInvalidateMappedMemoryRanges)
        t->vkInvalidateMappedMemoryRanges = (PFN_vkInvalidateMappedMemoryRanges)gipa(instance, "vkInvalidateMappedMemoryRanges");
    t->vkGetDeviceMemoryCommitment = (PFN_vkGetDeviceMemoryCommitment)(device ? gdpa(device, "vkGetDeviceMemoryCommitment") : NULL);
    if (!t->vkGetDeviceMemoryCommitment)
        t->vkGetDeviceMemoryCommitment = (PFN_vkGetDeviceMemoryCommitment)gipa(instance, "vkGetDeviceMemoryCommitment");
    t->vkBindBufferMemory = (PFN_vkBindBufferMemory)(device ? gdpa(device, "vkBindBufferMemory") : NULL);
    if (!t->vkBindBufferMemory)
        t->vkBindBufferMemory = (PFN_vkBindBufferMemory)gipa(instance, "vkBindBufferMemory");
    t->vkBindImageMemory = (PFN_vkBindImageMemory)(device ? gdpa(device, "vkBindImageMemory") : NULL);
    if (!t->vkBindImageMemory)
        t->vkBindImageMemory = (PFN_vkBindImageMemory)gipa(instance, "vkBindImageMemory");
    t->vkGetBufferMemoryRequirements = (PFN_vkGetBufferMemoryRequirements)(device ? gdpa(device, "vkGetBufferMemoryRequirements") : NULL);
    if (!t->vkGetBufferMemoryRequirements)
        t->vkGetBufferMemoryRequirements = (PFN_vkGetBufferMemoryRequirements)gipa(instance, "vkGetBufferMemoryRequirements");
    t->vkGetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)(device ? gdpa(device, "vkGetImageMemoryRequirements") : NULL);
    if (!t->vkGetImageMemoryRequirements)
        t->vkGetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)gipa(instance, "vkGetImageMemoryRequirements");
    t->vkGetImageSparseMemoryRequirements = (PFN_vkGetImageSparseMemoryRequirements)(device ? gdpa(device, "vkGetImageSparseMemoryRequirements") : NULL);
    if (!t->vkGetImageSparseMemoryRequirements)
        t->vkGetImageSparseMemoryRequirements = (PFN_vkGetImageSparseMemoryRequirements)gipa(instance, "vkGetImageSparseMemoryRequirements");
    t->vkGetPhysicalDeviceSparseImageFormatProperties = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties)gipa(instance, "vkGetPhysicalDeviceSparseImageFormatProperties");
    t->vkQueueBindSparse = (PFN_vkQueueBindSparse)(device ? gdpa(device, "vkQueueBindSparse") : NULL);
    if (!t->vkQueueBindSparse)
        t->vkQueueBindSparse = (PFN_vkQueueBindSparse)gipa(instance, "vkQueueBindSparse");
    t->vkCreateFence = (PFN_vkCreateFence)(device ? gdpa(device, "vkCreateFence") : NULL);
    if (!t->vkCreateFence)
        t->vkCreateFence = (PFN_vkCreateFence)gipa(instance, "vkCreateFence");
    t->vkDestroyFence = (PFN_vkDestroyFence)(device ? gdpa(device, "vkDestroyFence") : NULL);
    if (!t->vkDestroyFence)
        t->vkDestroyFence = (PFN_vkDestroyFence)gipa(instance, "vkDestroyFence");
    t->vkResetFences = (PFN_vkResetFences)(device ? gdpa(device, "vkResetFences") : NULL);
    if (!t->vkResetFences)
        t->vkResetFences = (PFN_vkResetFences)gipa(instance, "vkResetFences");
    t->vkGetFenceStatus = (PFN_vkGetFenceStatus)(device ? gdpa(device, "vkGetFenceStatus") : NULL);
    if (!t->vkGetFenceStatus)
        t->vkGetFenceStatus = (PFN_vkGetFenceStatus)gipa(instance, "vkGetFenceStatus");
    t->vkWaitForFences = (PFN_vkWaitForFences)(device ? gdpa(device, "vkWaitForFences") : NULL);
    if (!t->vkWaitForFences)
        t->vkWaitForFences = (PFN_vkWaitForFences)gipa(instance, "vkWaitForFences");
    t->vkCreateSemaphore = (PFN_vkCreateSemaphore)(device ? gdpa(device, "vkCreateSemaphore") : NULL);
    if (!t->vkCreateSemaphore)
        t->vkCreateSemaphore = (PFN_vkCreateSemaphore)gipa(instance, "vkCreateSemaphore");
    t->vkDestroySemaphore = (PFN_vkDestroySemaphore)(device ? gdpa(device, "vkDestroySemaphore") : NULL);
    if (!t->vkDestroySemaphore)
        t->vkDestroySemaphore = (PFN_vkDestroySemaphore)gipa(instance, "vkDestroySemaphore");
    t->vkCreateQueryPool = (PFN_vkCreateQueryPool)(device ? gdpa(device, "vkCreateQueryPool") : NULL);
    if (!t->vkCreateQueryPool)
        t->vkCreateQueryPool = (PFN_vkCreateQueryPool)gipa(instance, "vkCreateQueryPool");
    t->vkDestroyQueryPool = (PFN_vkDestroyQueryPool)(device ? gdpa(device, "vkDestroyQueryPool") : NULL);
    if (!t->vkDestroyQueryPool)
        t->vkDestroyQueryPool = (PFN_vkDestroyQueryPool)gipa(instance, "vkDestroyQueryPool");
    t->vkGetQueryPoolResults = (PFN_vkGetQueryPoolResults)(device ? gdpa(device, "vkGetQueryPoolResults") : NULL);
    if (!t->vkGetQueryPoolResults)
        t->vkGetQueryPoolResults = (PFN_vkGetQueryPoolResults)gipa(instance, "vkGetQueryPoolResults");
    t->vkCreateBuffer = (PFN_vkCreateBuffer)(device ? gdpa(device, "vkCreateBuffer") : NULL);
    if (!t->vkCreateBuffer)
        t->vkCreateBuffer = (PFN_vkCreateBuffer)gipa(instance, "vkCreateBuffer");
    t->vkDestroyBuffer = (PFN_vkDestroyBuffer)(device ? gdpa(device, "vkDestroyBuffer") : NULL);
    if (!t->vkDestroyBuffer)
        t->vkDestroyBuffer = (PFN_vkDestroyBuffer)gipa(instance, "vkDestroyBuffer");
    t->vkCreateImage = (PFN_vkCreateImage)(device ? gdpa(device, "vkCreateImage") : NULL);
    if (!t->vkCreateImage)
        t->vkCreateImage = (PFN_vkCreateImage)gipa(instance, "vkCreateImage");
    t->vkDestroyImage = (PFN_vkDestroyImage)(device ? gdpa(device, "vkDestroyImage") : NULL);
    if (!t->vkDestroyImage)
        t->vkDestroyImage = (PFN_vkDestroyImage)gipa(instance, "vkDestroyImage");
    t->vkGetImageSubresourceLayout = (PFN_vkGetImageSubresourceLayout)(device ? gdpa(device, "vkGetImageSubresourceLayout") : NULL);
    if (!t->vkGetImageSubresourceLayout)
        t->vkGetImageSubresourceLayout = (PFN_vkGetImageSubresourceLayout)gipa(instance, "vkGetImageSubresourceLayout");
    t->vkCreateImageView = (PFN_vkCreateImageView)(device ? gdpa(device, "vkCreateImageView") : NULL);
    if (!t->vkCreateImageView)
        t->vkCreateImageView = (PFN_vkCreateImageView)gipa(instance, "vkCreateImageView");
    t->vkDestroyImageView = (PFN_vkDestroyImageView)(device ? gdpa(device, "vkDestroyImageView") : NULL);
    if (!t->vkDestroyImageView)
        t->vkDestroyImageView = (PFN_vkDestroyImageView)gipa(instance, "vkDestroyImageView");
    t->vkCreateCommandPool = (PFN_vkCreateCommandPool)(device ? gdpa(device, "vkCreateCommandPool") : NULL);
    if (!t->vkCreateCommandPool)
        t->vkCreateCommandPool = (PFN_vkCreateCommandPool)gipa(instance, "vkCreateCommandPool");
    t->vkDestroyCommandPool = (PFN_vkDestroyCommandPool)(device ? gdpa(device, "vkDestroyCommandPool") : NULL);
    if (!t->vkDestroyCommandPool)
        t->vkDestroyCommandPool = (PFN_vkDestroyCommandPool)gipa(instance, "vkDestroyCommandPool");
    t->vkResetCommandPool = (PFN_vkResetCommandPool)(device ? gdpa(device, "vkResetCommandPool") : NULL);
    if (!t->vkResetCommandPool)
        t->vkResetCommandPool = (PFN_vkResetCommandPool)gipa(instance, "vkResetCommandPool");
    t->vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)(device ? gdpa(device, "vkAllocateCommandBuffers") : NULL);
    if (!t->vkAllocateCommandBuffers)
        t->vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)gipa(instance, "vkAllocateCommandBuffers");
    t->vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers)(device ? gdpa(device, "vkFreeCommandBuffers") : NULL);
    if (!t->vkFreeCommandBuffers)
        t->vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers)gipa(instance, "vkFreeCommandBuffers");
    t->vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)(device ? gdpa(device, "vkBeginCommandBuffer") : NULL);
    if (!t->vkBeginCommandBuffer)
        t->vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)gipa(instance, "vkBeginCommandBuffer");
    t->vkEndCommandBuffer = (PFN_vkEndCommandBuffer)(device ? gdpa(device, "vkEndCommandBuffer") : NULL);
    if (!t->vkEndCommandBuffer)
        t->vkEndCommandBuffer = (PFN_vkEndCommandBuffer)gipa(instance, "vkEndCommandBuffer");
    t->vkResetCommandBuffer = (PFN_vkResetCommandBuffer)(device ? gdpa(device, "vkResetCommandBuffer") : NULL);
    if (!t->vkResetCommandBuffer)
        t->vkResetCommandBuffer = (PFN_vkResetCommandBuffer)gipa(instance, "vkResetCommandBuffer");
    t->vkCmdCopyBuffer = (PFN_vkCmdCopyBuffer)(device ? gdpa(device, "vkCmdCopyBuffer") : NULL);
    if (!t->vkCmdCopyBuffer)
        t->vkCmdCopyBuffer = (PFN_vkCmdCopyBuffer)gipa(instance, "vkCmdCopyBuffer");
    t->vkCmdCopyImage = (PFN_vkCmdCopyImage)(device ? gdpa(device, "vkCmdCopyImage") : NULL);
    if (!t->vkCmdCopyImage)
        t->vkCmdCopyImage = (PFN_vkCmdCopyImage)gipa(instance, "vkCmdCopyImage");
    t->vkCmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage)(device ? gdpa(device, "vkCmdCopyBufferToImage") : NULL);
    if (!t->vkCmdCopyBufferToImage)
        t->vkCmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage)gipa(instance, "vkCmdCopyBufferToImage");
    t->vkCmdCopyImageToBuffer = (PFN_vkCmdCopyImageToBuffer)(device ? gdpa(device, "vkCmdCopyImageToBuffer") : NULL);
    if (!t->vkCmdCopyImageToBuffer)
        t->vkCmdCopyImageToBuffer = (PFN_vkCmdCopyImageToBuffer)gipa(instance, "vkCmdCopyImageToBuffer");
    t->vkCmdUpdateBuffer = (PFN_vkCmdUpdateBuffer)(device ? gdpa(device, "vkCmdUpdateBuffer") : NULL);
    if (!t->vkCmdUpdateBuffer)
        t->vkCmdUpdateBuffer = (PFN_vkCmdUpdateBuffer)gipa(instance, "vkCmdUpdateBuffer");
    t->vkCmdFillBuffer = (PFN_vkCmdFillBuffer)(device ? gdpa(device, "vkCmdFillBuffer") : NULL);
    if (!t->vkCmdFillBuffer)
        t->vkCmdFillBuffer = (PFN_vkCmdFillBuffer)gipa(instance, "vkCmdFillBuffer");
    t->vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)(device ? gdpa(device, "vkCmdPipelineBarrier") : NULL);
    if (!t->vkCmdPipelineBarrier)
        t->vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)gipa(instance, "vkCmdPipelineBarrier");
    t->vkCmdBeginQuery = (PFN_vkCmdBeginQuery)(device ? gdpa(device, "vkCmdBeginQuery") : NULL);
    if (!t->vkCmdBeginQuery)
        t->vkCmdBeginQuery = (PFN_vkCmdBeginQuery)gipa(instance, "vkCmdBeginQuery");
    t->vkCmdEndQuery = (PFN_vkCmdEndQuery)(device ? gdpa(device, "vkCmdEndQuery") : NULL);
    if (!t->vkCmdEndQuery)
        t->vkCmdEndQuery = (PFN_vkCmdEndQuery)gipa(instance, "vkCmdEndQuery");
    t->vkCmdResetQueryPool = (PFN_vkCmdResetQueryPool)(device ? gdpa(device, "vkCmdResetQueryPool") : NULL);
    if (!t->vkCmdResetQueryPool)
        t->vkCmdResetQueryPool = (PFN_vkCmdResetQueryPool)gipa(instance, "vkCmdResetQueryPool");
    t->vkCmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)(device ? gdpa(device, "vkCmdWriteTimestamp") : NULL);
    if (!t->vkCmdWriteTimestamp)
        t->vkCmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)gipa(instance, "vkCmdWriteTimestamp");
    t->vkCmdCopyQueryPoolResults = (PFN_vkCmdCopyQueryPoolResults)(device ? gdpa(device, "vkCmdCopyQueryPoolResults") : NULL);
    if (!t->vkCmdCopyQueryPoolResults)
        t->vkCmdCopyQueryPoolResults = (PFN_vkCmdCopyQueryPoolResults)gipa(instance, "vkCmdCopyQueryPoolResults");
    t->vkCmdExecuteCommands = (PFN_vkCmdExecuteCommands)(device ? gdpa(device, "vkCmdExecuteCommands") : NULL);
    if (!t->vkCmdExecuteCommands)
        t->vkCmdExecuteCommands = (PFN_vkCmdExecuteCommands)gipa(instance, "vkCmdExecuteCommands");
    t->vkCreateEvent = (PFN_vkCreateEvent)(device ? gdpa(device, "vkCreateEvent") : NULL);
    if (!t->vkCreateEvent)
        t->vkCreateEvent = (PFN_vkCreateEvent)gipa(instance, "vkCreateEvent");
    t->vkDestroyEvent = (PFN_vkDestroyEvent)(device ? gdpa(device, "vkDestroyEvent") : NULL);
    if (!t->vkDestroyEvent)
        t->vkDestroyEvent = (PFN_vkDestroyEvent)gipa(instance, "vkDestroyEvent");
    t->vkGetEventStatus = (PFN_vkGetEventStatus)(device ? gdpa(device, "vkGetEventStatus") : NULL);
    if (!t->vkGetEventStatus)
        t->vkGetEventStatus = (PFN_vkGetEventStatus)gipa(instance, "vkGetEventStatus");
    t->vkSetEvent = (PFN_vkSetEvent)(device ? gdpa(device, "vkSetEvent") : NULL);
    if (!t->vkSetEvent)
        t->vkSetEvent = (PFN_vkSetEvent)gipa(instance, "vkSetEvent");
    t->vkResetEvent = (PFN_vkResetEvent)(device ? gdpa(device, "vkResetEvent") : NULL);
    if (!t->vkResetEvent)
        t->vkResetEvent = (PFN_vkResetEvent)gipa(instance, "vkResetEvent");
    t->vkCreateBufferView = (PFN_vkCreateBufferView)(device ? gdpa(device, "vkCreateBufferView") : NULL);
    if (!t->vkCreateBufferView)
        t->vkCreateBufferView = (PFN_vkCreateBufferView)gipa(instance, "vkCreateBufferView");
    t->vkDestroyBufferView = (PFN_vkDestroyBufferView)(device ? gdpa(device, "vkDestroyBufferView") : NULL);
    if (!t->vkDestroyBufferView)
        t->vkDestroyBufferView = (PFN_vkDestroyBufferView)gipa(instance, "vkDestroyBufferView");
    t->vkCreateShaderModule = (PFN_vkCreateShaderModule)(device ? gdpa(device, "vkCreateShaderModule") : NULL);
    if (!t->vkCreateShaderModule)
        t->vkCreateShaderModule = (PFN_vkCreateShaderModule)gipa(instance, "vkCreateShaderModule");
    t->vkDestroyShaderModule = (PFN_vkDestroyShaderModule)(device ? gdpa(device, "vkDestroyShaderModule") : NULL);
    if (!t->vkDestroyShaderModule)
        t->vkDestroyShaderModule = (PFN_vkDestroyShaderModule)gipa(instance, "vkDestroyShaderModule");
    t->vkCreatePipelineCache = (PFN_vkCreatePipelineCache)(device ? gdpa(device, "vkCreatePipelineCache") : NULL);
    if (!t->vkCreatePipelineCache)
        t->vkCreatePipelineCache = (PFN_vkCreatePipelineCache)gipa(instance, "vkCreatePipelineCache");
    t->vkDestroyPipelineCache = (PFN_vkDestroyPipelineCache)(device ? gdpa(device, "vkDestroyPipelineCache") : NULL);
    if (!t->vkDestroyPipelineCache)
        t->vkDestroyPipelineCache = (PFN_vkDestroyPipelineCache)gipa(instance, "vkDestroyPipelineCache");
    t->vkGetPipelineCacheData = (PFN_vkGetPipelineCacheData)(device ? gdpa(device, "vkGetPipelineCacheData") : NULL);
    if (!t->vkGetPipelineCacheData)
        t->vkGetPipelineCacheData = (PFN_vkGetPipelineCacheData)gipa(instance, "vkGetPipelineCacheData");
    t->vkMergePipelineCaches = (PFN_vkMergePipelineCaches)(device ? gdpa(device, "vkMergePipelineCaches") : NULL);
    if (!t->vkMergePipelineCaches)
        t->vkMergePipelineCaches = (PFN_vkMergePipelineCaches)gipa(instance, "vkMergePipelineCaches");
    t->vkCreateComputePipelines = (PFN_vkCreateComputePipelines)(device ? gdpa(device, "vkCreateComputePipelines") : NULL);
    if (!t->vkCreateComputePipelines)
        t->vkCreateComputePipelines = (PFN_vkCreateComputePipelines)gipa(instance, "vkCreateComputePipelines");
    t->vkDestroyPipeline = (PFN_vkDestroyPipeline)(device ? gdpa(device, "vkDestroyPipeline") : NULL);
    if (!t->vkDestroyPipeline)
        t->vkDestroyPipeline = (PFN_vkDestroyPipeline)gipa(instance, "vkDestroyPipeline");
    t->vkCreatePipelineLayout = (PFN_vkCreatePipelineLayout)(device ? gdpa(device, "vkCreatePipelineLayout") : NULL);
    if (!t->vkCreatePipelineLayout)
        t->vkCreatePipelineLayout = (PFN_vkCreatePipelineLayout)gipa(instance, "vkCreatePipelineLayout");
    t->vkDestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)(device ? gdpa(device, "vkDestroyPipelineLayout") : NULL);
    if (!t->vkDestroyPipelineLayout)
        t->vkDestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)gipa(instance, "vkDestroyPipelineLayout");
    t->vkCreateSampler = (PFN_vkCreateSampler)(device ? gdpa(device, "vkCreateSampler") : NULL);
    if (!t->vkCreateSampler)
        t->vkCreateSampler = (PFN_vkCreateSampler)gipa(instance, "vkCreateSampler");
    t->vkDestroySampler = (PFN_vkDestroySampler)(device ? gdpa(device, "vkDestroySampler") : NULL);
    if (!t->vkDestroySampler)
        t->vkDestroySampler = (PFN_vkDestroySampler)gipa(instance, "vkDestroySampler");
    t->vkCreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)(device ? gdpa(device, "vkCreateDescriptorSetLayout") : NULL);
    if (!t->vkCreateDescriptorSetLayout)
        t->vkCreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)gipa(instance, "vkCreateDescriptorSetLayout");
    t->vkDestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)(device ? gdpa(device, "vkDestroyDescriptorSetLayout") : NULL);
    if (!t->vkDestroyDescriptorSetLayout)
        t->vkDestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)gipa(instance, "vkDestroyDescriptorSetLayout");
    t->vkCreateDescriptorPool = (PFN_vkCreateDescriptorPool)(device ? gdpa(device, "vkCreateDescriptorPool") : NULL);
    if (!t->vkCreateDescriptorPool)
        t->vkCreateDescriptorPool = (PFN_vkCreateDescriptorPool)gipa(instance, "vkCreateDescriptorPool");
    t->vkDestroyDescriptorPool = (PFN_vkDestroyDescriptorPool)(device ? gdpa(device, "vkDestroyDescriptorPool") : NULL);
    if (!t->vkDestroyDescriptorPool)
        t->vkDestroyDescriptorPool = (PFN_vkDestroyDescriptorPool)gipa(instance, "vkDestroyDescriptorPool");
    t->vkResetDescriptorPool = (PFN_vkResetDescriptorPool)(device ? gdpa(device, "vkResetDescriptorPool") : NULL);
    if (!t->vkResetDescriptorPool)
        t->vkResetDescriptorPool = (PFN_vkResetDescriptorPool)gipa(instance, "vkResetDescriptorPool");
    t->vkAllocateDescriptorSets = (PFN_vkAllocateDescriptorSets)(device ? gdpa(device, "vkAllocateDescriptorSets") : NULL);
    if (!t->vkAllocateDescriptorSets)
        t->vkAllocateDescriptorSets = (PFN_vkAllocateDescriptorSets)gipa(instance, "vkAllocateDescriptorSets");
    t->vkFreeDescriptorSets = (PFN_vkFreeDescriptorSets)(device ? gdpa(device, "vkFreeDescriptorSets") : NULL);
    if (!t->vkFreeDescriptorSets)
        t->vkFreeDescriptorSets = (PFN_vkFreeDescriptorSets)gipa(instance, "vkFreeDescriptorSets");
    t->vkUpdateDescriptorSets = (PFN_vkUpdateDescriptorSets)(device ? gdpa(device, "vkUpdateDescriptorSets") : NULL);
    if (!t->vkUpdateDescriptorSets)
        t->vkUpdateDescriptorSets = (PFN_vkUpdateDescriptorSets)gipa(instance, "vkUpdateDescriptorSets");
    t->vkCmdBindPipeline = (PFN_vkCmdBindPipeline)(device ? gdpa(device, "vkCmdBindPipeline") : NULL);
    if (!t->vkCmdBindPipeline)
        t->vkCmdBindPipeline = (PFN_vkCmdBindPipeline)gipa(instance, "vkCmdBindPipeline");
    t->vkCmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)(device ? gdpa(device, "vkCmdBindDescriptorSets") : NULL);
    if (!t->vkCmdBindDescriptorSets)
        t->vkCmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)gipa(instance, "vkCmdBindDescriptorSets");
    t->vkCmdClearColorImage = (PFN_vkCmdClearColorImage)(device ? gdpa(device, "vkCmdClearColorImage") : NULL);
    if (!t->vkCmdClearColorImage)
        t->vkCmdClearColorImage = (PFN_vkCmdClearColorImage)gipa(instance, "vkCmdClearColorImage");
    t->vkCmdDispatch = (PFN_vkCmdDispatch)(device ? gdpa(device, "vkCmdDispatch") : NULL);
    if (!t->vkCmdDispatch)
        t->vkCmdDispatch = (PFN_vkCmdDispatch)gipa(instance, "vkCmdDispatch");
    t->vkCmdDispatchIndirect = (PFN_vkCmdDispatchIndirect)(device ? gdpa(device, "vkCmdDispatchIndirect") : NULL);
    if (!t->vkCmdDispatchIndirect)
        t->vkCmdDispatchIndirect = (PFN_vkCmdDispatchIndirect)gipa(instance, "vkCmdDispatchIndirect");
    t->vkCmdSetEvent = (PFN_vkCmdSetEvent)(device ? gdpa(device, "vkCmdSetEvent") : NULL);
    if (!t->vkCmdSetEvent)
        t->vkCmdSetEvent = (PFN_vkCmdSetEvent)gipa(instance, "vkCmdSetEvent");
    t->vkCmdResetEvent = (PFN_vkCmdResetEvent)(device ? gdpa(device, "vkCmdResetEvent") : NULL);
    if (!t->vkCmdResetEvent)
        t->vkCmdResetEvent = (PFN_vkCmdResetEvent)gipa(instance, "vkCmdResetEvent");
    t->vkCmdWaitEvents = (PFN_vkCmdWaitEvents)(device ? gdpa(device, "vkCmdWaitEvents") : NULL);
    if (!t->vkCmdWaitEvents)
        t->vkCmdWaitEvents = (PFN_vkCmdWaitEvents)gipa(instance, "vkCmdWaitEvents");
    t->vkCmdPushConstants = (PFN_vkCmdPushConstants)(device ? gdpa(device, "vkCmdPushConstants") : NULL);
    if (!t->vkCmdPushConstants)
        t->vkCmdPushConstants = (PFN_vkCmdPushConstants)gipa(instance, "vkCmdPushConstants");
    t->vkCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines)(device ? gdpa(device, "vkCreateGraphicsPipelines") : NULL);
    if (!t->vkCreateGraphicsPipelines)
        t->vkCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines)gipa(instance, "vkCreateGraphicsPipelines");
    t->vkCreateFramebuffer = (PFN_vkCreateFramebuffer)(device ? gdpa(device, "vkCreateFramebuffer") : NULL);
    if (!t->vkCreateFramebuffer)
        t->vkCreateFramebuffer = (PFN_vkCreateFramebuffer)gipa(instance, "vkCreateFramebuffer");
    t->vkDestroyFramebuffer = (PFN_vkDestroyFramebuffer)(device ? gdpa(device, "vkDestroyFramebuffer") : NULL);
    if (!t->vkDestroyFramebuffer)
        t->vkDestroyFramebuffer = (PFN_vkDestroyFramebuffer)gipa(instance, "vkDestroyFramebuffer");
    t->vkCreateRenderPass = (PFN_vkCreateRenderPass)(device ? gdpa(device, "vkCreateRenderPass") : NULL);
    if (!t->vkCreateRenderPass)
        t->vkCreateRenderPass = (PFN_vkCreateRenderPass)gipa(instance, "vkCreateRenderPass");
    t->vkDestroyRenderPass = (PFN_vkDestroyRenderPass)(device ? gdpa(device, "vkDestroyRenderPass") : NULL);
    if (!t->vkDestroyRenderPass)
        t->vkDestroyRenderPass = (PFN_vkDestroyRenderPass)gipa(instance, "vkDestroyRenderPass");
    t->vkGetRenderAreaGranularity = (PFN_vkGetRenderAreaGranularity)(device ? gdpa(device, "vkGetRenderAreaGranularity") : NULL);
    if (!t->vkGetRenderAreaGranularity)
        t->vkGetRenderAreaGranularity = (PFN_vkGetRenderAreaGranularity)gipa(instance, "vkGetRenderAreaGranularity");
    t->vkCmdSetViewport = (PFN_vkCmdSetViewport)(device ? gdpa(device, "vkCmdSetViewport") : NULL);
    if (!t->vkCmdSetViewport)
        t->vkCmdSetViewport = (PFN_vkCmdSetViewport)gipa(instance, "vkCmdSetViewport");
    t->vkCmdSetScissor = (PFN_vkCmdSetScissor)(device ? gdpa(device, "vkCmdSetScissor") : NULL);
    if (!t->vkCmdSetScissor)
        t->vkCmdSetScissor = (PFN_vkCmdSetScissor)gipa(instance, "vkCmdSetScissor");
    t->vkCmdSetLineWidth = (PFN_vkCmdSetLineWidth)(device ? gdpa(device, "vkCmdSetLineWidth") : NULL);
    if (!t->vkCmdSetLineWidth)
        t->vkCmdSetLineWidth = (PFN_vkCmdSetLineWidth)gipa(instance, "vkCmdSetLineWidth");
    t->vkCmdSetDepthBias = (PFN_vkCmdSetDepthBias)(device ? gdpa(device, "vkCmdSetDepthBias") : NULL);
    if (!t->vkCmdSetDepthBias)
        t->vkCmdSetDepthBias = (PFN_vkCmdSetDepthBias)gipa(instance, "vkCmdSetDepthBias");
    t->vkCmdSetBlendConstants = (PFN_vkCmdSetBlendConstants)(device ? gdpa(device, "vkCmdSetBlendConstants") : NULL);
    if (!t->vkCmdSetBlendConstants)
        t->vkCmdSetBlendConstants = (PFN_vkCmdSetBlendConstants)gipa(instance, "vkCmdSetBlendConstants");
    t->vkCmdSetDepthBounds = (PFN_vkCmdSetDepthBounds)(device ? gdpa(device, "vkCmdSetDepthBounds") : NULL);
    if (!t->vkCmdSetDepthBounds)
        t->vkCmdSetDepthBounds = (PFN_vkCmdSetDepthBounds)gipa(instance, "vkCmdSetDepthBounds");
    t->vkCmdSetStencilCompareMask = (PFN_vkCmdSetStencilCompareMask)(device ? gdpa(device, "vkCmdSetStencilCompareMask") : NULL);
    if (!t->vkCmdSetStencilCompareMask)
        t->vkCmdSetStencilCompareMask = (PFN_vkCmdSetStencilCompareMask)gipa(instance, "vkCmdSetStencilCompareMask");
    t->vkCmdSetStencilWriteMask = (PFN_vkCmdSetStencilWriteMask)(device ? gdpa(device, "vkCmdSetStencilWriteMask") : NULL);
    if (!t->vkCmdSetStencilWriteMask)
        t->vkCmdSetStencilWriteMask = (PFN_vkCmdSetStencilWriteMask)gipa(instance, "vkCmdSetStencilWriteMask");
    t->vkCmdSetStencilReference = (PFN_vkCmdSetStencilReference)(device ? gdpa(device, "vkCmdSetStencilReference") : NULL);
    if (!t->vkCmdSetStencilReference)
        t->vkCmdSetStencilReference = (PFN_vkCmdSetStencilReference)gipa(instance, "vkCmdSetStencilReference");
    t->vkCmdBindIndexBuffer = (PFN_vkCmdBindIndexBuffer)(device ? gdpa(device, "vkCmdBindIndexBuffer") : NULL);
    if (!t->vkCmdBindIndexBuffer)
        t->vkCmdBindIndexBuffer = (PFN_vkCmdBindIndexBuffer)gipa(instance, "vkCmdBindIndexBuffer");
    t->vkCmdBindVertexBuffers = (PFN_vkCmdBindVertexBuffers)(device ? gdpa(device, "vkCmdBindVertexBuffers") : NULL);
    if (!t->vkCmdBindVertexBuffers)
        t->vkCmdBindVertexBuffers = (PFN_vkCmdBindVertexBuffers)gipa(instance, "vkCmdBindVertexBuffers");
    t->vkCmdDraw = (PFN_vkCmdDraw)(device ? gdpa(device, "vkCmdDraw") : NULL);
    if (!t->vkCmdDraw)
        t->vkCmdDraw = (PFN_vkCmdDraw)gipa(instance, "vkCmdDraw");
    t->vkCmdDrawIndexed = (PFN_vkCmdDrawIndexed)(device ? gdpa(device, "vkCmdDrawIndexed") : NULL);
    if (!t->vkCmdDrawIndexed)
        t->vkCmdDrawIndexed = (PFN_vkCmdDrawIndexed)gipa(instance, "vkCmdDrawIndexed");
    t->vkCmdDrawIndirect = (PFN_vkCmdDrawIndirect)(device ? gdpa(device, "vkCmdDrawIndirect") : NULL);
    if (!t->vkCmdDrawIndirect)
        t->vkCmdDrawIndirect = (PFN_vkCmdDrawIndirect)gipa(instance, "vkCmdDrawIndirect");
    t->vkCmdDrawIndexedIndirect = (PFN_vkCmdDrawIndexedIndirect)(device ? gdpa(device, "vkCmdDrawIndexedIndirect") : NULL);
    if (!t->vkCmdDrawIndexedIndirect)
        t->vkCmdDrawIndexedIndirect = (PFN_vkCmdDrawIndexedIndirect)gipa(instance, "vkCmdDrawIndexedIndirect");
    t->vkCmdBlitImage = (PFN_vkCmdBlitImage)(device ? gdpa(device, "vkCmdBlitImage") : NULL);
    if (!t->vkCmdBlitImage)
        t->vkCmdBlitImage = (PFN_vkCmdBlitImage)gipa(instance, "vkCmdBlitImage");
    t->vkCmdClearDepthStencilImage = (PFN_vkCmdClearDepthStencilImage)(device ? gdpa(device, "vkCmdClearDepthStencilImage") : NULL);
    if (!t->vkCmdClearDepthStencilImage)
        t->vkCmdClearDepthStencilImage = (PFN_vkCmdClearDepthStencilImage)gipa(instance, "vkCmdClearDepthStencilImage");
    t->vkCmdClearAttachments = (PFN_vkCmdClearAttachments)(device ? gdpa(device, "vkCmdClearAttachments") : NULL);
    if (!t->vkCmdClearAttachments)
        t->vkCmdClearAttachments = (PFN_vkCmdClearAttachments)gipa(instance, "vkCmdClearAttachments");
    t->vkCmdResolveImage = (PFN_vkCmdResolveImage)(device ? gdpa(device, "vkCmdResolveImage") : NULL);
    if (!t->vkCmdResolveImage)
        t->vkCmdResolveImage = (PFN_vkCmdResolveImage)gipa(instance, "vkCmdResolveImage");
    t->vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)(device ? gdpa(device, "vkCmdBeginRenderPass") : NULL);
    if (!t->vkCmdBeginRenderPass)
        t->vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)gipa(instance, "vkCmdBeginRenderPass");
    t->vkCmdNextSubpass = (PFN_vkCmdNextSubpass)(device ? gdpa(device, "vkCmdNextSubpass") : NULL);
    if (!t->vkCmdNextSubpass)
        t->vkCmdNextSubpass = (PFN_vkCmdNextSubpass)gipa(instance, "vkCmdNextSubpass");
    t->vkCmdEndRenderPass = (PFN_vkCmdEndRenderPass)(device ? gdpa(device, "vkCmdEndRenderPass") : NULL);
    if (!t->vkCmdEndRenderPass)
        t->vkCmdEndRenderPass = (PFN_vkCmdEndRenderPass)gipa(instance, "vkCmdEndRenderPass");
    t->vkEnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)gipa(instance, "vkEnumerateInstanceVersion");
    t->vkBindBufferMemory2 = (PFN_vkBindBufferMemory2)(device ? gdpa(device, "vkBindBufferMemory2") : NULL);
    if (!t->vkBindBufferMemory2)
        t->vkBindBufferMemory2 = (PFN_vkBindBufferMemory2)gipa(instance, "vkBindBufferMemory2");
    t->vkBindImageMemory2 = (PFN_vkBindImageMemory2)(device ? gdpa(device, "vkBindImageMemory2") : NULL);
    if (!t->vkBindImageMemory2)
        t->vkBindImageMemory2 = (PFN_vkBindImageMemory2)gipa(instance, "vkBindImageMemory2");
    t->vkGetDeviceGroupPeerMemoryFeatures = (PFN_vkGetDeviceGroupPeerMemoryFeatures)(device ? gdpa(device, "vkGetDeviceGroupPeerMemoryFeatures") : NULL);
    if (!t->vkGetDeviceGroupPeerMemoryFeatures)
        t->vkGetDeviceGroupPeerMemoryFeatures = (PFN_vkGetDeviceGroupPeerMemoryFeatures)gipa(instance, "vkGetDeviceGroupPeerMemoryFeatures");
    t->vkCmdSetDeviceMask = (PFN_vkCmdSetDeviceMask)(device ? gdpa(device, "vkCmdSetDeviceMask") : NULL);
    if (!t->vkCmdSetDeviceMask)
        t->vkCmdSetDeviceMask = (PFN_vkCmdSetDeviceMask)gipa(instance, "vkCmdSetDeviceMask");
    t->vkEnumeratePhysicalDeviceGroups = (PFN_vkEnumeratePhysicalDeviceGroups)gipa(instance, "vkEnumeratePhysicalDeviceGroups");
    t->vkGetImageMemoryRequirements2 = (PFN_vkGetImageMemoryRequirements2)(device ? gdpa(device, "vkGetImageMemoryRequirements2") : NULL);
    if (!t->vkGetImageMemoryRequirements2)
        t->vkGetImageMemoryRequirements2 = (PFN_vkGetImageMemoryRequirements2)gipa(instance, "vkGetImageMemoryRequirements2");
    t->vkGetBufferMemoryRequirements2 = (PFN_vkGetBufferMemoryRequirements2)(device ? gdpa(device, "vkGetBufferMemoryRequirements2") : NULL);
    if (!t->vkGetBufferMemoryRequirements2)
        t->vkGetBufferMemoryRequirements2 = (PFN_vkGetBufferMemoryRequirements2)gipa(instance, "vkGetBufferMemoryRequirements2");
    t->vkGetImageSparseMemoryRequirements2 = (PFN_vkGetImageSparseMemoryRequirements2)(device ? gdpa(device, "vkGetImageSparseMemoryRequirements2") : NULL);
    if (!t->vkGetImageSparseMemoryRequirements2)
        t->vkGetImageSparseMemoryRequirements2 = (PFN_vkGetImageSparseMemoryRequirements2)gipa(instance, "vkGetImageSparseMemoryRequirements2");
    t->vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)gipa(instance, "vkGetPhysicalDeviceFeatures2");
    t->vkGetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)gipa(instance, "vkGetPhysicalDeviceProperties2");
    t->vkGetPhysicalDeviceFormatProperties2 = (PFN_vkGetPhysicalDeviceFormatProperties2)gipa(instance, "vkGetPhysicalDeviceFormatProperties2");
    t->vkGetPhysicalDeviceImageFormatProperties2 = (PFN_vkGetPhysicalDeviceImageFormatProperties2)gipa(instance, "vkGetPhysicalDeviceImageFormatProperties2");
    t->vkGetPhysicalDeviceQueueFamilyProperties2 = (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties2");
    t->vkGetPhysicalDeviceMemoryProperties2 = (PFN_vkGetPhysicalDeviceMemoryProperties2)gipa(instance, "vkGetPhysicalDeviceMemoryProperties2");
    t->vkGetPhysicalDeviceSparseImageFormatProperties2 = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties2)gipa(instance, "vkGetPhysicalDeviceSparseImageFormatProperties2");
    t->vkTrimCommandPool = (PFN_vkTrimCommandPool)(device ? gdpa(device, "vkTrimCommandPool") : NULL);
    if (!t->vkTrimCommandPool)
        t->vkTrimCommandPool = (PFN_vkTrimCommandPool)gipa(instance, "vkTrimCommandPool");
    t->vkGetDeviceQueue2 = (PFN_vkGetDeviceQueue2)(device ? gdpa(device, "vkGetDeviceQueue2") : NULL);
    if (!t->vkGetDeviceQueue2)
        t->vkGetDeviceQueue2 = (PFN_vkGetDeviceQueue2)gipa(instance, "vkGetDeviceQueue2");
    t->vkGetPhysicalDeviceExternalBufferProperties = (PFN_vkGetPhysicalDeviceExternalBufferProperties)gipa(instance, "vkGetPhysicalDeviceExternalBufferProperties");
    t->vkGetPhysicalDeviceExternalFenceProperties = (PFN_vkGetPhysicalDeviceExternalFenceProperties)gipa(instance, "vkGetPhysicalDeviceExternalFenceProperties");
    t->vkGetPhysicalDeviceExternalSemaphoreProperties = (PFN_vkGetPhysicalDeviceExternalSemaphoreProperties)gipa(instance, "vkGetPhysicalDeviceExternalSemaphoreProperties");
    t->vkCmdDispatchBase = (PFN_vkCmdDispatchBase)(device ? gdpa(device, "vkCmdDispatchBase") : NULL);
    if (!t->vkCmdDispatchBase)
        t->vkCmdDispatchBase = (PFN_vkCmdDispatchBase)gipa(instance, "vkCmdDispatchBase");
    t->vkCreateDescriptorUpdateTemplate = (PFN_vkCreateDescriptorUpdateTemplate)(device ? gdpa(device, "vkCreateDescriptorUpdateTemplate") : NULL);
    if (!t->vkCreateDescriptorUpdateTemplate)
        t->vkCreateDescriptorUpdateTemplate = (PFN_vkCreateDescriptorUpdateTemplate)gipa(instance, "vkCreateDescriptorUpdateTemplate");
    t->vkDestroyDescriptorUpdateTemplate = (PFN_vkDestroyDescriptorUpdateTemplate)(device ? gdpa(device, "vkDestroyDescriptorUpdateTemplate") : NULL);
    if (!t->vkDestroyDescriptorUpdateTemplate)
        t->vkDestroyDescriptorUpdateTemplate = (PFN_vkDestroyDescriptorUpdateTemplate)gipa(instance, "vkDestroyDescriptorUpdateTemplate");
    t->vkUpdateDescriptorSetWithTemplate = (PFN_vkUpdateDescriptorSetWithTemplate)(device ? gdpa(device, "vkUpdateDescriptorSetWithTemplate") : NULL);
    if (!t->vkUpdateDescriptorSetWithTemplate)
        t->vkUpdateDescriptorSetWithTemplate = (PFN_vkUpdateDescriptorSetWithTemplate)gipa(instance, "vkUpdateDescriptorSetWithTemplate");
    t->vkGetDescriptorSetLayoutSupport = (PFN_vkGetDescriptorSetLayoutSupport)(device ? gdpa(device, "vkGetDescriptorSetLayoutSupport") : NULL);
    if (!t->vkGetDescriptorSetLayoutSupport)
        t->vkGetDescriptorSetLayoutSupport = (PFN_vkGetDescriptorSetLayoutSupport)gipa(instance, "vkGetDescriptorSetLayoutSupport");
    t->vkCreateSamplerYcbcrConversion = (PFN_vkCreateSamplerYcbcrConversion)(device ? gdpa(device, "vkCreateSamplerYcbcrConversion") : NULL);
    if (!t->vkCreateSamplerYcbcrConversion)
        t->vkCreateSamplerYcbcrConversion = (PFN_vkCreateSamplerYcbcrConversion)gipa(instance, "vkCreateSamplerYcbcrConversion");
    t->vkDestroySamplerYcbcrConversion = (PFN_vkDestroySamplerYcbcrConversion)(device ? gdpa(device, "vkDestroySamplerYcbcrConversion") : NULL);
    if (!t->vkDestroySamplerYcbcrConversion)
        t->vkDestroySamplerYcbcrConversion = (PFN_vkDestroySamplerYcbcrConversion)gipa(instance, "vkDestroySamplerYcbcrConversion");
    t->vkResetQueryPool = (PFN_vkResetQueryPool)(device ? gdpa(device, "vkResetQueryPool") : NULL);
    if (!t->vkResetQueryPool)
        t->vkResetQueryPool = (PFN_vkResetQueryPool)gipa(instance, "vkResetQueryPool");
    t->vkGetSemaphoreCounterValue = (PFN_vkGetSemaphoreCounterValue)(device ? gdpa(device, "vkGetSemaphoreCounterValue") : NULL);
    if (!t->vkGetSemaphoreCounterValue)
        t->vkGetSemaphoreCounterValue = (PFN_vkGetSemaphoreCounterValue)gipa(instance, "vkGetSemaphoreCounterValue");
    t->vkWaitSemaphores = (PFN_vkWaitSemaphores)(device ? gdpa(device, "vkWaitSemaphores") : NULL);
    if (!t->vkWaitSemaphores)
        t->vkWaitSemaphores = (PFN_vkWaitSemaphores)gipa(instance, "vkWaitSemaphores");
    t->vkSignalSemaphore = (PFN_vkSignalSemaphore)(device ? gdpa(device, "vkSignalSemaphore") : NULL);
    if (!t->vkSignalSemaphore)
        t->vkSignalSemaphore = (PFN_vkSignalSemaphore)gipa(instance, "vkSignalSemaphore");
    t->vkGetBufferDeviceAddress = (PFN_vkGetBufferDeviceAddress)(device ? gdpa(device, "vkGetBufferDeviceAddress") : NULL);
    if (!t->vkGetBufferDeviceAddress)
        t->vkGetBufferDeviceAddress = (PFN_vkGetBufferDeviceAddress)gipa(instance, "vkGetBufferDeviceAddress");
    t->vkGetBufferOpaqueCaptureAddress = (PFN_vkGetBufferOpaqueCaptureAddress)(device ? gdpa(device, "vkGetBufferOpaqueCaptureAddress") : NULL);
    if (!t->vkGetBufferOpaqueCaptureAddress)
        t->vkGetBufferOpaqueCaptureAddress = (PFN_vkGetBufferOpaqueCaptureAddress)gipa(instance, "vkGetBufferOpaqueCaptureAddress");
    t->vkGetDeviceMemoryOpaqueCaptureAddress = (PFN_vkGetDeviceMemoryOpaqueCaptureAddress)(device ? gdpa(device, "vkGetDeviceMemoryOpaqueCaptureAddress") : NULL);
    if (!t->vkGetDeviceMemoryOpaqueCaptureAddress)
        t->vkGetDeviceMemoryOpaqueCaptureAddress = (PFN_vkGetDeviceMemoryOpaqueCaptureAddress)gipa(instance, "vkGetDeviceMemoryOpaqueCaptureAddress");
    t->vkCmdDrawIndirectCount = (PFN_vkCmdDrawIndirectCount)(device ? gdpa(device, "vkCmdDrawIndirectCount") : NULL);
    if (!t->vkCmdDrawIndirectCount)
        t->vkCmdDrawIndirectCount = (PFN_vkCmdDrawIndirectCount)gipa(instance, "vkCmdDrawIndirectCount");
    t->vkCmdDrawIndexedIndirectCount = (PFN_vkCmdDrawIndexedIndirectCount)(device ? gdpa(device, "vkCmdDrawIndexedIndirectCount") : NULL);
    if (!t->vkCmdDrawIndexedIndirectCount)
        t->vkCmdDrawIndexedIndirectCount = (PFN_vkCmdDrawIndexedIndirectCount)gipa(instance, "vkCmdDrawIndexedIndirectCount");
    t->vkCreateRenderPass2 = (PFN_vkCreateRenderPass2)(device ? gdpa(device, "vkCreateRenderPass2") : NULL);
    if (!t->vkCreateRenderPass2)
        t->vkCreateRenderPass2 = (PFN_vkCreateRenderPass2)gipa(instance, "vkCreateRenderPass2");
    t->vkCmdBeginRenderPass2 = (PFN_vkCmdBeginRenderPass2)(device ? gdpa(device, "vkCmdBeginRenderPass2") : NULL);
    if (!t->vkCmdBeginRenderPass2)
        t->vkCmdBeginRenderPass2 = (PFN_vkCmdBeginRenderPass2)gipa(instance, "vkCmdBeginRenderPass2");
    t->vkCmdNextSubpass2 = (PFN_vkCmdNextSubpass2)(device ? gdpa(device, "vkCmdNextSubpass2") : NULL);
    if (!t->vkCmdNextSubpass2)
        t->vkCmdNextSubpass2 = (PFN_vkCmdNextSubpass2)gipa(instance, "vkCmdNextSubpass2");
    t->vkCmdEndRenderPass2 = (PFN_vkCmdEndRenderPass2)(device ? gdpa(device, "vkCmdEndRenderPass2") : NULL);
    if (!t->vkCmdEndRenderPass2)
        t->vkCmdEndRenderPass2 = (PFN_vkCmdEndRenderPass2)gipa(instance, "vkCmdEndRenderPass2");
    t->vkGetPhysicalDeviceToolProperties = (PFN_vkGetPhysicalDeviceToolProperties)gipa(instance, "vkGetPhysicalDeviceToolProperties");
    t->vkCreatePrivateDataSlot = (PFN_vkCreatePrivateDataSlot)(device ? gdpa(device, "vkCreatePrivateDataSlot") : NULL);
    if (!t->vkCreatePrivateDataSlot)
        t->vkCreatePrivateDataSlot = (PFN_vkCreatePrivateDataSlot)gipa(instance, "vkCreatePrivateDataSlot");
    t->vkDestroyPrivateDataSlot = (PFN_vkDestroyPrivateDataSlot)(device ? gdpa(device, "vkDestroyPrivateDataSlot") : NULL);
    if (!t->vkDestroyPrivateDataSlot)
        t->vkDestroyPrivateDataSlot = (PFN_vkDestroyPrivateDataSlot)gipa(instance, "vkDestroyPrivateDataSlot");
    t->vkSetPrivateData = (PFN_vkSetPrivateData)(device ? gdpa(device, "vkSetPrivateData") : NULL);
    if (!t->vkSetPrivateData)
        t->vkSetPrivateData = (PFN_vkSetPrivateData)gipa(instance, "vkSetPrivateData");
    t->vkGetPrivateData = (PFN_vkGetPrivateData)(device ? gdpa(device, "vkGetPrivateData") : NULL);
    if (!t->vkGetPrivateData)
        t->vkGetPrivateData = (PFN_vkGetPrivateData)gipa(instance, "vkGetPrivateData");
    t->vkCmdPipelineBarrier2 = (PFN_vkCmdPipelineBarrier2)(device ? gdpa(device, "vkCmdPipelineBarrier2") : NULL);
    if (!t->vkCmdPipelineBarrier2)
        t->vkCmdPipelineBarrier2 = (PFN_vkCmdPipelineBarrier2)gipa(instance, "vkCmdPipelineBarrier2");
    t->vkCmdWriteTimestamp2 = (PFN_vkCmdWriteTimestamp2)(device ? gdpa(device, "vkCmdWriteTimestamp2") : NULL);
    if (!t->vkCmdWriteTimestamp2)
        t->vkCmdWriteTimestamp2 = (PFN_vkCmdWriteTimestamp2)gipa(instance, "vkCmdWriteTimestamp2");
    t->vkQueueSubmit2 = (PFN_vkQueueSubmit2)(device ? gdpa(device, "vkQueueSubmit2") : NULL);
    if (!t->vkQueueSubmit2)
        t->vkQueueSubmit2 = (PFN_vkQueueSubmit2)gipa(instance, "vkQueueSubmit2");
    t->vkCmdCopyBuffer2 = (PFN_vkCmdCopyBuffer2)(device ? gdpa(device, "vkCmdCopyBuffer2") : NULL);
    if (!t->vkCmdCopyBuffer2)
        t->vkCmdCopyBuffer2 = (PFN_vkCmdCopyBuffer2)gipa(instance, "vkCmdCopyBuffer2");
    t->vkCmdCopyImage2 = (PFN_vkCmdCopyImage2)(device ? gdpa(device, "vkCmdCopyImage2") : NULL);
    if (!t->vkCmdCopyImage2)
        t->vkCmdCopyImage2 = (PFN_vkCmdCopyImage2)gipa(instance, "vkCmdCopyImage2");
    t->vkCmdCopyBufferToImage2 = (PFN_vkCmdCopyBufferToImage2)(device ? gdpa(device, "vkCmdCopyBufferToImage2") : NULL);
    if (!t->vkCmdCopyBufferToImage2)
        t->vkCmdCopyBufferToImage2 = (PFN_vkCmdCopyBufferToImage2)gipa(instance, "vkCmdCopyBufferToImage2");
    t->vkCmdCopyImageToBuffer2 = (PFN_vkCmdCopyImageToBuffer2)(device ? gdpa(device, "vkCmdCopyImageToBuffer2") : NULL);
    if (!t->vkCmdCopyImageToBuffer2)
        t->vkCmdCopyImageToBuffer2 = (PFN_vkCmdCopyImageToBuffer2)gipa(instance, "vkCmdCopyImageToBuffer2");
    t->vkGetDeviceBufferMemoryRequirements = (PFN_vkGetDeviceBufferMemoryRequirements)(device ? gdpa(device, "vkGetDeviceBufferMemoryRequirements") : NULL);
    if (!t->vkGetDeviceBufferMemoryRequirements)
        t->vkGetDeviceBufferMemoryRequirements = (PFN_vkGetDeviceBufferMemoryRequirements)gipa(instance, "vkGetDeviceBufferMemoryRequirements");
    t->vkGetDeviceImageMemoryRequirements = (PFN_vkGetDeviceImageMemoryRequirements)(device ? gdpa(device, "vkGetDeviceImageMemoryRequirements") : NULL);
    if (!t->vkGetDeviceImageMemoryRequirements)
        t->vkGetDeviceImageMemoryRequirements = (PFN_vkGetDeviceImageMemoryRequirements)gipa(instance, "vkGetDeviceImageMemoryRequirements");
    t->vkGetDeviceImageSparseMemoryRequirements = (PFN_vkGetDeviceImageSparseMemoryRequirements)(device ? gdpa(device, "vkGetDeviceImageSparseMemoryRequirements") : NULL);
    if (!t->vkGetDeviceImageSparseMemoryRequirements)
        t->vkGetDeviceImageSparseMemoryRequirements = (PFN_vkGetDeviceImageSparseMemoryRequirements)gipa(instance, "vkGetDeviceImageSparseMemoryRequirements");
    t->vkCmdSetEvent2 = (PFN_vkCmdSetEvent2)(device ? gdpa(device, "vkCmdSetEvent2") : NULL);
    if (!t->vkCmdSetEvent2)
        t->vkCmdSetEvent2 = (PFN_vkCmdSetEvent2)gipa(instance, "vkCmdSetEvent2");
    t->vkCmdResetEvent2 = (PFN_vkCmdResetEvent2)(device ? gdpa(device, "vkCmdResetEvent2") : NULL);
    if (!t->vkCmdResetEvent2)
        t->vkCmdResetEvent2 = (PFN_vkCmdResetEvent2)gipa(instance, "vkCmdResetEvent2");
    t->vkCmdWaitEvents2 = (PFN_vkCmdWaitEvents2)(device ? gdpa(device, "vkCmdWaitEvents2") : NULL);
    if (!t->vkCmdWaitEvents2)
        t->vkCmdWaitEvents2 = (PFN_vkCmdWaitEvents2)gipa(instance, "vkCmdWaitEvents2");
    t->vkCmdBlitImage2 = (PFN_vkCmdBlitImage2)(device ? gdpa(device, "vkCmdBlitImage2") : NULL);
    if (!t->vkCmdBlitImage2)
        t->vkCmdBlitImage2 = (PFN_vkCmdBlitImage2)gipa(instance, "vkCmdBlitImage2");
    t->vkCmdResolveImage2 = (PFN_vkCmdResolveImage2)(device ? gdpa(device, "vkCmdResolveImage2") : NULL);
    if (!t->vkCmdResolveImage2)
        t->vkCmdResolveImage2 = (PFN_vkCmdResolveImage2)gipa(instance, "vkCmdResolveImage2");
    t->vkCmdBeginRendering = (PFN_vkCmdBeginRendering)(device ? gdpa(device, "vkCmdBeginRendering") : NULL);
    if (!t->vkCmdBeginRendering)
        t->vkCmdBeginRendering = (PFN_vkCmdBeginRendering)gipa(instance, "vkCmdBeginRendering");
    t->vkCmdEndRendering = (PFN_vkCmdEndRendering)(device ? gdpa(device, "vkCmdEndRendering") : NULL);
    if (!t->vkCmdEndRendering)
        t->vkCmdEndRendering = (PFN_vkCmdEndRendering)gipa(instance, "vkCmdEndRendering");
    t->vkCmdSetCullMode = (PFN_vkCmdSetCullMode)(device ? gdpa(device, "vkCmdSetCullMode") : NULL);
    if (!t->vkCmdSetCullMode)
        t->vkCmdSetCullMode = (PFN_vkCmdSetCullMode)gipa(instance, "vkCmdSetCullMode");
    t->vkCmdSetFrontFace = (PFN_vkCmdSetFrontFace)(device ? gdpa(device, "vkCmdSetFrontFace") : NULL);
    if (!t->vkCmdSetFrontFace)
        t->vkCmdSetFrontFace = (PFN_vkCmdSetFrontFace)gipa(instance, "vkCmdSetFrontFace");
    t->vkCmdSetPrimitiveTopology = (PFN_vkCmdSetPrimitiveTopology)(device ? gdpa(device, "vkCmdSetPrimitiveTopology") : NULL);
    if (!t->vkCmdSetPrimitiveTopology)
        t->vkCmdSetPrimitiveTopology = (PFN_vkCmdSetPrimitiveTopology)gipa(instance, "vkCmdSetPrimitiveTopology");
    t->vkCmdSetViewportWithCount = (PFN_vkCmdSetViewportWithCount)(device ? gdpa(device, "vkCmdSetViewportWithCount") : NULL);
    if (!t->vkCmdSetViewportWithCount)
        t->vkCmdSetViewportWithCount = (PFN_vkCmdSetViewportWithCount)gipa(instance, "vkCmdSetViewportWithCount");
    t->vkCmdSetScissorWithCount = (PFN_vkCmdSetScissorWithCount)(device ? gdpa(device, "vkCmdSetScissorWithCount") : NULL);
    if (!t->vkCmdSetScissorWithCount)
        t->vkCmdSetScissorWithCount = (PFN_vkCmdSetScissorWithCount)gipa(instance, "vkCmdSetScissorWithCount");
    t->vkCmdBindVertexBuffers2 = (PFN_vkCmdBindVertexBuffers2)(device ? gdpa(device, "vkCmdBindVertexBuffers2") : NULL);
    if (!t->vkCmdBindVertexBuffers2)
        t->vkCmdBindVertexBuffers2 = (PFN_vkCmdBindVertexBuffers2)gipa(instance, "vkCmdBindVertexBuffers2");
    t->vkCmdSetDepthTestEnable = (PFN_vkCmdSetDepthTestEnable)(device ? gdpa(device, "vkCmdSetDepthTestEnable") : NULL);
    if (!t->vkCmdSetDepthTestEnable)
        t->vkCmdSetDepthTestEnable = (PFN_vkCmdSetDepthTestEnable)gipa(instance, "vkCmdSetDepthTestEnable");
    t->vkCmdSetDepthWriteEnable = (PFN_vkCmdSetDepthWriteEnable)(device ? gdpa(device, "vkCmdSetDepthWriteEnable") : NULL);
    if (!t->vkCmdSetDepthWriteEnable)
        t->vkCmdSetDepthWriteEnable = (PFN_vkCmdSetDepthWriteEnable)gipa(instance, "vkCmdSetDepthWriteEnable");
    t->vkCmdSetDepthCompareOp = (PFN_vkCmdSetDepthCompareOp)(device ? gdpa(device, "vkCmdSetDepthCompareOp") : NULL);
    if (!t->vkCmdSetDepthCompareOp)
        t->vkCmdSetDepthCompareOp = (PFN_vkCmdSetDepthCompareOp)gipa(instance, "vkCmdSetDepthCompareOp");
    t->vkCmdSetDepthBoundsTestEnable = (PFN_vkCmdSetDepthBoundsTestEnable)(device ? gdpa(device, "vkCmdSetDepthBoundsTestEnable") : NULL);
    if (!t->vkCmdSetDepthBoundsTestEnable)
        t->vkCmdSetDepthBoundsTestEnable = (PFN_vkCmdSetDepthBoundsTestEnable)gipa(instance, "vkCmdSetDepthBoundsTestEnable");
    t->vkCmdSetStencilTestEnable = (PFN_vkCmdSetStencilTestEnable)(device ? gdpa(device, "vkCmdSetStencilTestEnable") : NULL);
    if (!t->vkCmdSetStencilTestEnable)
        t->vkCmdSetStencilTestEnable = (PFN_vkCmdSetStencilTestEnable)gipa(instance, "vkCmdSetStencilTestEnable");
    t->vkCmdSetStencilOp = (PFN_vkCmdSetStencilOp)(device ? gdpa(device, "vkCmdSetStencilOp") : NULL);
    if (!t->vkCmdSetStencilOp)
        t->vkCmdSetStencilOp = (PFN_vkCmdSetStencilOp)gipa(instance, "vkCmdSetStencilOp");
    t->vkCmdSetRasterizerDiscardEnable = (PFN_vkCmdSetRasterizerDiscardEnable)(device ? gdpa(device, "vkCmdSetRasterizerDiscardEnable") : NULL);
    if (!t->vkCmdSetRasterizerDiscardEnable)
        t->vkCmdSetRasterizerDiscardEnable = (PFN_vkCmdSetRasterizerDiscardEnable)gipa(instance, "vkCmdSetRasterizerDiscardEnable");
    t->vkCmdSetDepthBiasEnable = (PFN_vkCmdSetDepthBiasEnable)(device ? gdpa(device, "vkCmdSetDepthBiasEnable") : NULL);
    if (!t->vkCmdSetDepthBiasEnable)
        t->vkCmdSetDepthBiasEnable = (PFN_vkCmdSetDepthBiasEnable)gipa(instance, "vkCmdSetDepthBiasEnable");
    t->vkCmdSetPrimitiveRestartEnable = (PFN_vkCmdSetPrimitiveRestartEnable)(device ? gdpa(device, "vkCmdSetPrimitiveRestartEnable") : NULL);
    if (!t->vkCmdSetPrimitiveRestartEnable)
        t->vkCmdSetPrimitiveRestartEnable = (PFN_vkCmdSetPrimitiveRestartEnable)gipa(instance, "vkCmdSetPrimitiveRestartEnable");
    t->vkMapMemory2 = (PFN_vkMapMemory2)(device ? gdpa(device, "vkMapMemory2") : NULL);
    if (!t->vkMapMemory2)
        t->vkMapMemory2 = (PFN_vkMapMemory2)gipa(instance, "vkMapMemory2");
    t->vkUnmapMemory2 = (PFN_vkUnmapMemory2)(device ? gdpa(device, "vkUnmapMemory2") : NULL);
    if (!t->vkUnmapMemory2)
        t->vkUnmapMemory2 = (PFN_vkUnmapMemory2)gipa(instance, "vkUnmapMemory2");
    t->vkGetDeviceImageSubresourceLayout = (PFN_vkGetDeviceImageSubresourceLayout)(device ? gdpa(device, "vkGetDeviceImageSubresourceLayout") : NULL);
    if (!t->vkGetDeviceImageSubresourceLayout)
        t->vkGetDeviceImageSubresourceLayout = (PFN_vkGetDeviceImageSubresourceLayout)gipa(instance, "vkGetDeviceImageSubresourceLayout");
    t->vkGetImageSubresourceLayout2 = (PFN_vkGetImageSubresourceLayout2)(device ? gdpa(device, "vkGetImageSubresourceLayout2") : NULL);
    if (!t->vkGetImageSubresourceLayout2)
        t->vkGetImageSubresourceLayout2 = (PFN_vkGetImageSubresourceLayout2)gipa(instance, "vkGetImageSubresourceLayout2");
    t->vkCopyMemoryToImage = (PFN_vkCopyMemoryToImage)(device ? gdpa(device, "vkCopyMemoryToImage") : NULL);
    if (!t->vkCopyMemoryToImage)
        t->vkCopyMemoryToImage = (PFN_vkCopyMemoryToImage)gipa(instance, "vkCopyMemoryToImage");
    t->vkCopyImageToMemory = (PFN_vkCopyImageToMemory)(device ? gdpa(device, "vkCopyImageToMemory") : NULL);
    if (!t->vkCopyImageToMemory)
        t->vkCopyImageToMemory = (PFN_vkCopyImageToMemory)gipa(instance, "vkCopyImageToMemory");
    t->vkCopyImageToImage = (PFN_vkCopyImageToImage)(device ? gdpa(device, "vkCopyImageToImage") : NULL);
    if (!t->vkCopyImageToImage)
        t->vkCopyImageToImage = (PFN_vkCopyImageToImage)gipa(instance, "vkCopyImageToImage");
    t->vkTransitionImageLayout = (PFN_vkTransitionImageLayout)(device ? gdpa(device, "vkTransitionImageLayout") : NULL);
    if (!t->vkTransitionImageLayout)
        t->vkTransitionImageLayout = (PFN_vkTransitionImageLayout)gipa(instance, "vkTransitionImageLayout");
    t->vkCmdPushDescriptorSet = (PFN_vkCmdPushDescriptorSet)(device ? gdpa(device, "vkCmdPushDescriptorSet") : NULL);
    if (!t->vkCmdPushDescriptorSet)
        t->vkCmdPushDescriptorSet = (PFN_vkCmdPushDescriptorSet)gipa(instance, "vkCmdPushDescriptorSet");
    t->vkCmdPushDescriptorSetWithTemplate = (PFN_vkCmdPushDescriptorSetWithTemplate)(device ? gdpa(device, "vkCmdPushDescriptorSetWithTemplate") : NULL);
    if (!t->vkCmdPushDescriptorSetWithTemplate)
        t->vkCmdPushDescriptorSetWithTemplate = (PFN_vkCmdPushDescriptorSetWithTemplate)gipa(instance, "vkCmdPushDescriptorSetWithTemplate");
    t->vkCmdBindDescriptorSets2 = (PFN_vkCmdBindDescriptorSets2)(device ? gdpa(device, "vkCmdBindDescriptorSets2") : NULL);
    if (!t->vkCmdBindDescriptorSets2)
        t->vkCmdBindDescriptorSets2 = (PFN_vkCmdBindDescriptorSets2)gipa(instance, "vkCmdBindDescriptorSets2");
    t->vkCmdPushConstants2 = (PFN_vkCmdPushConstants2)(device ? gdpa(device, "vkCmdPushConstants2") : NULL);
    if (!t->vkCmdPushConstants2)
        t->vkCmdPushConstants2 = (PFN_vkCmdPushConstants2)gipa(instance, "vkCmdPushConstants2");
    t->vkCmdPushDescriptorSet2 = (PFN_vkCmdPushDescriptorSet2)(device ? gdpa(device, "vkCmdPushDescriptorSet2") : NULL);
    if (!t->vkCmdPushDescriptorSet2)
        t->vkCmdPushDescriptorSet2 = (PFN_vkCmdPushDescriptorSet2)gipa(instance, "vkCmdPushDescriptorSet2");
    t->vkCmdPushDescriptorSetWithTemplate2 = (PFN_vkCmdPushDescriptorSetWithTemplate2)(device ? gdpa(device, "vkCmdPushDescriptorSetWithTemplate2") : NULL);
    if (!t->vkCmdPushDescriptorSetWithTemplate2)
        t->vkCmdPushDescriptorSetWithTemplate2 = (PFN_vkCmdPushDescriptorSetWithTemplate2)gipa(instance, "vkCmdPushDescriptorSetWithTemplate2");
    t->vkCmdSetLineStipple = (PFN_vkCmdSetLineStipple)(device ? gdpa(device, "vkCmdSetLineStipple") : NULL);
    if (!t->vkCmdSetLineStipple)
        t->vkCmdSetLineStipple = (PFN_vkCmdSetLineStipple)gipa(instance, "vkCmdSetLineStipple");
    t->vkCmdBindIndexBuffer2 = (PFN_vkCmdBindIndexBuffer2)(device ? gdpa(device, "vkCmdBindIndexBuffer2") : NULL);
    if (!t->vkCmdBindIndexBuffer2)
        t->vkCmdBindIndexBuffer2 = (PFN_vkCmdBindIndexBuffer2)gipa(instance, "vkCmdBindIndexBuffer2");
    t->vkGetRenderingAreaGranularity = (PFN_vkGetRenderingAreaGranularity)(device ? gdpa(device, "vkGetRenderingAreaGranularity") : NULL);
    if (!t->vkGetRenderingAreaGranularity)
        t->vkGetRenderingAreaGranularity = (PFN_vkGetRenderingAreaGranularity)gipa(instance, "vkGetRenderingAreaGranularity");
    t->vkCmdSetRenderingAttachmentLocations = (PFN_vkCmdSetRenderingAttachmentLocations)(device ? gdpa(device, "vkCmdSetRenderingAttachmentLocations") : NULL);
    if (!t->vkCmdSetRenderingAttachmentLocations)
        t->vkCmdSetRenderingAttachmentLocations = (PFN_vkCmdSetRenderingAttachmentLocations)gipa(instance, "vkCmdSetRenderingAttachmentLocations");
    t->vkCmdSetRenderingInputAttachmentIndices = (PFN_vkCmdSetRenderingInputAttachmentIndices)(device ? gdpa(device, "vkCmdSetRenderingInputAttachmentIndices") : NULL);
    if (!t->vkCmdSetRenderingInputAttachmentIndices)
        t->vkCmdSetRenderingInputAttachmentIndices = (PFN_vkCmdSetRenderingInputAttachmentIndices)gipa(instance, "vkCmdSetRenderingInputAttachmentIndices");
    t->vkCmdBindTransformFeedbackBuffersEXT = (PFN_vkCmdBindTransformFeedbackBuffersEXT)(device ? gdpa(device, "vkCmdBindTransformFeedbackBuffersEXT") : NULL);
    if (!t->vkCmdBindTransformFeedbackBuffersEXT)
        t->vkCmdBindTransformFeedbackBuffersEXT = (PFN_vkCmdBindTransformFeedbackBuffersEXT)gipa(instance, "vkCmdBindTransformFeedbackBuffersEXT");
    t->vkCmdBeginTransformFeedbackEXT = (PFN_vkCmdBeginTransformFeedbackEXT)(device ? gdpa(device, "vkCmdBeginTransformFeedbackEXT") : NULL);
    if (!t->vkCmdBeginTransformFeedbackEXT)
        t->vkCmdBeginTransformFeedbackEXT = (PFN_vkCmdBeginTransformFeedbackEXT)gipa(instance, "vkCmdBeginTransformFeedbackEXT");
    t->vkCmdEndTransformFeedbackEXT = (PFN_vkCmdEndTransformFeedbackEXT)(device ? gdpa(device, "vkCmdEndTransformFeedbackEXT") : NULL);
    if (!t->vkCmdEndTransformFeedbackEXT)
        t->vkCmdEndTransformFeedbackEXT = (PFN_vkCmdEndTransformFeedbackEXT)gipa(instance, "vkCmdEndTransformFeedbackEXT");
    t->vkCmdBeginQueryIndexedEXT = (PFN_vkCmdBeginQueryIndexedEXT)(device ? gdpa(device, "vkCmdBeginQueryIndexedEXT") : NULL);
    if (!t->vkCmdBeginQueryIndexedEXT)
        t->vkCmdBeginQueryIndexedEXT = (PFN_vkCmdBeginQueryIndexedEXT)gipa(instance, "vkCmdBeginQueryIndexedEXT");
    t->vkCmdEndQueryIndexedEXT = (PFN_vkCmdEndQueryIndexedEXT)(device ? gdpa(device, "vkCmdEndQueryIndexedEXT") : NULL);
    if (!t->vkCmdEndQueryIndexedEXT)
        t->vkCmdEndQueryIndexedEXT = (PFN_vkCmdEndQueryIndexedEXT)gipa(instance, "vkCmdEndQueryIndexedEXT");
    t->vkCmdDrawIndirectByteCountEXT = (PFN_vkCmdDrawIndirectByteCountEXT)(device ? gdpa(device, "vkCmdDrawIndirectByteCountEXT") : NULL);
    if (!t->vkCmdDrawIndirectByteCountEXT)
        t->vkCmdDrawIndirectByteCountEXT = (PFN_vkCmdDrawIndirectByteCountEXT)gipa(instance, "vkCmdDrawIndirectByteCountEXT");
    t->vkGetImageViewHandleNVX = (PFN_vkGetImageViewHandleNVX)(device ? gdpa(device, "vkGetImageViewHandleNVX") : NULL);
    if (!t->vkGetImageViewHandleNVX)
        t->vkGetImageViewHandleNVX = (PFN_vkGetImageViewHandleNVX)gipa(instance, "vkGetImageViewHandleNVX");
    t->vkGetImageViewHandle64NVX = (PFN_vkGetImageViewHandle64NVX)(device ? gdpa(device, "vkGetImageViewHandle64NVX") : NULL);
    if (!t->vkGetImageViewHandle64NVX)
        t->vkGetImageViewHandle64NVX = (PFN_vkGetImageViewHandle64NVX)gipa(instance, "vkGetImageViewHandle64NVX");
    t->vkGetImageViewAddressNVX = (PFN_vkGetImageViewAddressNVX)(device ? gdpa(device, "vkGetImageViewAddressNVX") : NULL);
    if (!t->vkGetImageViewAddressNVX)
        t->vkGetImageViewAddressNVX = (PFN_vkGetImageViewAddressNVX)gipa(instance, "vkGetImageViewAddressNVX");
    t->vkGetDeviceCombinedImageSamplerIndexNVX = (PFN_vkGetDeviceCombinedImageSamplerIndexNVX)(device ? gdpa(device, "vkGetDeviceCombinedImageSamplerIndexNVX") : NULL);
    if (!t->vkGetDeviceCombinedImageSamplerIndexNVX)
        t->vkGetDeviceCombinedImageSamplerIndexNVX = (PFN_vkGetDeviceCombinedImageSamplerIndexNVX)gipa(instance, "vkGetDeviceCombinedImageSamplerIndexNVX");
    t->vkCmdDrawIndirectCountAMD = (PFN_vkCmdDrawIndirectCountAMD)(device ? gdpa(device, "vkCmdDrawIndirectCountAMD") : NULL);
    if (!t->vkCmdDrawIndirectCountAMD)
        t->vkCmdDrawIndirectCountAMD = (PFN_vkCmdDrawIndirectCountAMD)gipa(instance, "vkCmdDrawIndirectCountAMD");
    t->vkCmdDrawIndexedIndirectCountAMD = (PFN_vkCmdDrawIndexedIndirectCountAMD)(device ? gdpa(device, "vkCmdDrawIndexedIndirectCountAMD") : NULL);
    if (!t->vkCmdDrawIndexedIndirectCountAMD)
        t->vkCmdDrawIndexedIndirectCountAMD = (PFN_vkCmdDrawIndexedIndirectCountAMD)gipa(instance, "vkCmdDrawIndexedIndirectCountAMD");
    t->vkGetShaderInfoAMD = (PFN_vkGetShaderInfoAMD)(device ? gdpa(device, "vkGetShaderInfoAMD") : NULL);
    if (!t->vkGetShaderInfoAMD)
        t->vkGetShaderInfoAMD = (PFN_vkGetShaderInfoAMD)gipa(instance, "vkGetShaderInfoAMD");
    t->vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR)(device ? gdpa(device, "vkCmdBeginRenderingKHR") : NULL);
    if (!t->vkCmdBeginRenderingKHR)
        t->vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR)gipa(instance, "vkCmdBeginRenderingKHR");
    t->vkCmdEndRenderingKHR = (PFN_vkCmdEndRenderingKHR)(device ? gdpa(device, "vkCmdEndRenderingKHR") : NULL);
    if (!t->vkCmdEndRenderingKHR)
        t->vkCmdEndRenderingKHR = (PFN_vkCmdEndRenderingKHR)gipa(instance, "vkCmdEndRenderingKHR");
    t->vkGetPhysicalDeviceExternalImageFormatPropertiesNV = (PFN_vkGetPhysicalDeviceExternalImageFormatPropertiesNV)gipa(instance, "vkGetPhysicalDeviceExternalImageFormatPropertiesNV");
    t->vkGetPhysicalDeviceFeatures2KHR = (PFN_vkGetPhysicalDeviceFeatures2KHR)gipa(instance, "vkGetPhysicalDeviceFeatures2KHR");
    t->vkGetPhysicalDeviceProperties2KHR = (PFN_vkGetPhysicalDeviceProperties2KHR)gipa(instance, "vkGetPhysicalDeviceProperties2KHR");
    t->vkGetPhysicalDeviceFormatProperties2KHR = (PFN_vkGetPhysicalDeviceFormatProperties2KHR)gipa(instance, "vkGetPhysicalDeviceFormatProperties2KHR");
    t->vkGetPhysicalDeviceImageFormatProperties2KHR = (PFN_vkGetPhysicalDeviceImageFormatProperties2KHR)gipa(instance, "vkGetPhysicalDeviceImageFormatProperties2KHR");
    t->vkGetPhysicalDeviceQueueFamilyProperties2KHR = (PFN_vkGetPhysicalDeviceQueueFamilyProperties2KHR)gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties2KHR");
    t->vkGetPhysicalDeviceMemoryProperties2KHR = (PFN_vkGetPhysicalDeviceMemoryProperties2KHR)gipa(instance, "vkGetPhysicalDeviceMemoryProperties2KHR");
    t->vkGetPhysicalDeviceSparseImageFormatProperties2KHR = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties2KHR)gipa(instance, "vkGetPhysicalDeviceSparseImageFormatProperties2KHR");
    t->vkGetDeviceGroupPeerMemoryFeaturesKHR = (PFN_vkGetDeviceGroupPeerMemoryFeaturesKHR)(device ? gdpa(device, "vkGetDeviceGroupPeerMemoryFeaturesKHR") : NULL);
    if (!t->vkGetDeviceGroupPeerMemoryFeaturesKHR)
        t->vkGetDeviceGroupPeerMemoryFeaturesKHR = (PFN_vkGetDeviceGroupPeerMemoryFeaturesKHR)gipa(instance, "vkGetDeviceGroupPeerMemoryFeaturesKHR");
    t->vkCmdSetDeviceMaskKHR = (PFN_vkCmdSetDeviceMaskKHR)(device ? gdpa(device, "vkCmdSetDeviceMaskKHR") : NULL);
    if (!t->vkCmdSetDeviceMaskKHR)
        t->vkCmdSetDeviceMaskKHR = (PFN_vkCmdSetDeviceMaskKHR)gipa(instance, "vkCmdSetDeviceMaskKHR");
    t->vkCmdDispatchBaseKHR = (PFN_vkCmdDispatchBaseKHR)(device ? gdpa(device, "vkCmdDispatchBaseKHR") : NULL);
    if (!t->vkCmdDispatchBaseKHR)
        t->vkCmdDispatchBaseKHR = (PFN_vkCmdDispatchBaseKHR)gipa(instance, "vkCmdDispatchBaseKHR");
    t->vkGetDeviceGroupPresentCapabilitiesKHR = (PFN_vkGetDeviceGroupPresentCapabilitiesKHR)(device ? gdpa(device, "vkGetDeviceGroupPresentCapabilitiesKHR") : NULL);
    if (!t->vkGetDeviceGroupPresentCapabilitiesKHR)
        t->vkGetDeviceGroupPresentCapabilitiesKHR = (PFN_vkGetDeviceGroupPresentCapabilitiesKHR)gipa(instance, "vkGetDeviceGroupPresentCapabilitiesKHR");
    t->vkGetDeviceGroupSurfacePresentModesKHR = (PFN_vkGetDeviceGroupSurfacePresentModesKHR)(device ? gdpa(device, "vkGetDeviceGroupSurfacePresentModesKHR") : NULL);
    if (!t->vkGetDeviceGroupSurfacePresentModesKHR)
        t->vkGetDeviceGroupSurfacePresentModesKHR = (PFN_vkGetDeviceGroupSurfacePresentModesKHR)gipa(instance, "vkGetDeviceGroupSurfacePresentModesKHR");
    t->vkGetPhysicalDevicePresentRectanglesKHR = (PFN_vkGetPhysicalDevicePresentRectanglesKHR)gipa(instance, "vkGetPhysicalDevicePresentRectanglesKHR");
    t->vkAcquireNextImage2KHR = (PFN_vkAcquireNextImage2KHR)(device ? gdpa(device, "vkAcquireNextImage2KHR") : NULL);
    if (!t->vkAcquireNextImage2KHR)
        t->vkAcquireNextImage2KHR = (PFN_vkAcquireNextImage2KHR)gipa(instance, "vkAcquireNextImage2KHR");
    t->vkTrimCommandPoolKHR = (PFN_vkTrimCommandPoolKHR)(device ? gdpa(device, "vkTrimCommandPoolKHR") : NULL);
    if (!t->vkTrimCommandPoolKHR)
        t->vkTrimCommandPoolKHR = (PFN_vkTrimCommandPoolKHR)gipa(instance, "vkTrimCommandPoolKHR");
    t->vkEnumeratePhysicalDeviceGroupsKHR = (PFN_vkEnumeratePhysicalDeviceGroupsKHR)gipa(instance, "vkEnumeratePhysicalDeviceGroupsKHR");
    t->vkGetPhysicalDeviceExternalBufferPropertiesKHR = (PFN_vkGetPhysicalDeviceExternalBufferPropertiesKHR)gipa(instance, "vkGetPhysicalDeviceExternalBufferPropertiesKHR");
    t->vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = (PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR)gipa(instance, "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR");
    t->vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)(device ? gdpa(device, "vkCmdPushDescriptorSetKHR") : NULL);
    if (!t->vkCmdPushDescriptorSetKHR)
        t->vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)gipa(instance, "vkCmdPushDescriptorSetKHR");
    t->vkCmdPushDescriptorSetWithTemplateKHR = (PFN_vkCmdPushDescriptorSetWithTemplateKHR)(device ? gdpa(device, "vkCmdPushDescriptorSetWithTemplateKHR") : NULL);
    if (!t->vkCmdPushDescriptorSetWithTemplateKHR)
        t->vkCmdPushDescriptorSetWithTemplateKHR = (PFN_vkCmdPushDescriptorSetWithTemplateKHR)gipa(instance, "vkCmdPushDescriptorSetWithTemplateKHR");
    t->vkCmdBeginConditionalRenderingEXT = (PFN_vkCmdBeginConditionalRenderingEXT)(device ? gdpa(device, "vkCmdBeginConditionalRenderingEXT") : NULL);
    if (!t->vkCmdBeginConditionalRenderingEXT)
        t->vkCmdBeginConditionalRenderingEXT = (PFN_vkCmdBeginConditionalRenderingEXT)gipa(instance, "vkCmdBeginConditionalRenderingEXT");
    t->vkCmdEndConditionalRenderingEXT = (PFN_vkCmdEndConditionalRenderingEXT)(device ? gdpa(device, "vkCmdEndConditionalRenderingEXT") : NULL);
    if (!t->vkCmdEndConditionalRenderingEXT)
        t->vkCmdEndConditionalRenderingEXT = (PFN_vkCmdEndConditionalRenderingEXT)gipa(instance, "vkCmdEndConditionalRenderingEXT");
    t->vkCreateDescriptorUpdateTemplateKHR = (PFN_vkCreateDescriptorUpdateTemplateKHR)(device ? gdpa(device, "vkCreateDescriptorUpdateTemplateKHR") : NULL);
    if (!t->vkCreateDescriptorUpdateTemplateKHR)
        t->vkCreateDescriptorUpdateTemplateKHR = (PFN_vkCreateDescriptorUpdateTemplateKHR)gipa(instance, "vkCreateDescriptorUpdateTemplateKHR");
    t->vkDestroyDescriptorUpdateTemplateKHR = (PFN_vkDestroyDescriptorUpdateTemplateKHR)(device ? gdpa(device, "vkDestroyDescriptorUpdateTemplateKHR") : NULL);
    if (!t->vkDestroyDescriptorUpdateTemplateKHR)
        t->vkDestroyDescriptorUpdateTemplateKHR = (PFN_vkDestroyDescriptorUpdateTemplateKHR)gipa(instance, "vkDestroyDescriptorUpdateTemplateKHR");
    t->vkUpdateDescriptorSetWithTemplateKHR = (PFN_vkUpdateDescriptorSetWithTemplateKHR)(device ? gdpa(device, "vkUpdateDescriptorSetWithTemplateKHR") : NULL);
    if (!t->vkUpdateDescriptorSetWithTemplateKHR)
        t->vkUpdateDescriptorSetWithTemplateKHR = (PFN_vkUpdateDescriptorSetWithTemplateKHR)gipa(instance, "vkUpdateDescriptorSetWithTemplateKHR");
    t->vkCmdSetViewportWScalingNV = (PFN_vkCmdSetViewportWScalingNV)(device ? gdpa(device, "vkCmdSetViewportWScalingNV") : NULL);
    if (!t->vkCmdSetViewportWScalingNV)
        t->vkCmdSetViewportWScalingNV = (PFN_vkCmdSetViewportWScalingNV)gipa(instance, "vkCmdSetViewportWScalingNV");
    t->vkCmdSetDiscardRectangleEXT = (PFN_vkCmdSetDiscardRectangleEXT)(device ? gdpa(device, "vkCmdSetDiscardRectangleEXT") : NULL);
    if (!t->vkCmdSetDiscardRectangleEXT)
        t->vkCmdSetDiscardRectangleEXT = (PFN_vkCmdSetDiscardRectangleEXT)gipa(instance, "vkCmdSetDiscardRectangleEXT");
    t->vkCmdSetDiscardRectangleEnableEXT = (PFN_vkCmdSetDiscardRectangleEnableEXT)(device ? gdpa(device, "vkCmdSetDiscardRectangleEnableEXT") : NULL);
    if (!t->vkCmdSetDiscardRectangleEnableEXT)
        t->vkCmdSetDiscardRectangleEnableEXT = (PFN_vkCmdSetDiscardRectangleEnableEXT)gipa(instance, "vkCmdSetDiscardRectangleEnableEXT");
    t->vkCmdSetDiscardRectangleModeEXT = (PFN_vkCmdSetDiscardRectangleModeEXT)(device ? gdpa(device, "vkCmdSetDiscardRectangleModeEXT") : NULL);
    if (!t->vkCmdSetDiscardRectangleModeEXT)
        t->vkCmdSetDiscardRectangleModeEXT = (PFN_vkCmdSetDiscardRectangleModeEXT)gipa(instance, "vkCmdSetDiscardRectangleModeEXT");
    t->vkCreateRenderPass2KHR = (PFN_vkCreateRenderPass2KHR)(device ? gdpa(device, "vkCreateRenderPass2KHR") : NULL);
    if (!t->vkCreateRenderPass2KHR)
        t->vkCreateRenderPass2KHR = (PFN_vkCreateRenderPass2KHR)gipa(instance, "vkCreateRenderPass2KHR");
    t->vkCmdBeginRenderPass2KHR = (PFN_vkCmdBeginRenderPass2KHR)(device ? gdpa(device, "vkCmdBeginRenderPass2KHR") : NULL);
    if (!t->vkCmdBeginRenderPass2KHR)
        t->vkCmdBeginRenderPass2KHR = (PFN_vkCmdBeginRenderPass2KHR)gipa(instance, "vkCmdBeginRenderPass2KHR");
    t->vkCmdNextSubpass2KHR = (PFN_vkCmdNextSubpass2KHR)(device ? gdpa(device, "vkCmdNextSubpass2KHR") : NULL);
    if (!t->vkCmdNextSubpass2KHR)
        t->vkCmdNextSubpass2KHR = (PFN_vkCmdNextSubpass2KHR)gipa(instance, "vkCmdNextSubpass2KHR");
    t->vkCmdEndRenderPass2KHR = (PFN_vkCmdEndRenderPass2KHR)(device ? gdpa(device, "vkCmdEndRenderPass2KHR") : NULL);
    if (!t->vkCmdEndRenderPass2KHR)
        t->vkCmdEndRenderPass2KHR = (PFN_vkCmdEndRenderPass2KHR)gipa(instance, "vkCmdEndRenderPass2KHR");
    t->vkGetPhysicalDeviceExternalFencePropertiesKHR = (PFN_vkGetPhysicalDeviceExternalFencePropertiesKHR)gipa(instance, "vkGetPhysicalDeviceExternalFencePropertiesKHR");
    t->vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR = (PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR)gipa(instance, "vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR");
    t->vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR = (PFN_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR)gipa(instance, "vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR");
    t->vkAcquireProfilingLockKHR = (PFN_vkAcquireProfilingLockKHR)(device ? gdpa(device, "vkAcquireProfilingLockKHR") : NULL);
    if (!t->vkAcquireProfilingLockKHR)
        t->vkAcquireProfilingLockKHR = (PFN_vkAcquireProfilingLockKHR)gipa(instance, "vkAcquireProfilingLockKHR");
    t->vkReleaseProfilingLockKHR = (PFN_vkReleaseProfilingLockKHR)(device ? gdpa(device, "vkReleaseProfilingLockKHR") : NULL);
    if (!t->vkReleaseProfilingLockKHR)
        t->vkReleaseProfilingLockKHR = (PFN_vkReleaseProfilingLockKHR)gipa(instance, "vkReleaseProfilingLockKHR");
    t->vkCreateGpaSessionAMD = (PFN_vkCreateGpaSessionAMD)(device ? gdpa(device, "vkCreateGpaSessionAMD") : NULL);
    if (!t->vkCreateGpaSessionAMD)
        t->vkCreateGpaSessionAMD = (PFN_vkCreateGpaSessionAMD)gipa(instance, "vkCreateGpaSessionAMD");
    t->vkDestroyGpaSessionAMD = (PFN_vkDestroyGpaSessionAMD)(device ? gdpa(device, "vkDestroyGpaSessionAMD") : NULL);
    if (!t->vkDestroyGpaSessionAMD)
        t->vkDestroyGpaSessionAMD = (PFN_vkDestroyGpaSessionAMD)gipa(instance, "vkDestroyGpaSessionAMD");
    t->vkSetGpaDeviceClockModeAMD = (PFN_vkSetGpaDeviceClockModeAMD)(device ? gdpa(device, "vkSetGpaDeviceClockModeAMD") : NULL);
    if (!t->vkSetGpaDeviceClockModeAMD)
        t->vkSetGpaDeviceClockModeAMD = (PFN_vkSetGpaDeviceClockModeAMD)gipa(instance, "vkSetGpaDeviceClockModeAMD");
    t->vkGetGpaDeviceClockInfoAMD = (PFN_vkGetGpaDeviceClockInfoAMD)(device ? gdpa(device, "vkGetGpaDeviceClockInfoAMD") : NULL);
    if (!t->vkGetGpaDeviceClockInfoAMD)
        t->vkGetGpaDeviceClockInfoAMD = (PFN_vkGetGpaDeviceClockInfoAMD)gipa(instance, "vkGetGpaDeviceClockInfoAMD");
    t->vkCmdBeginGpaSessionAMD = (PFN_vkCmdBeginGpaSessionAMD)(device ? gdpa(device, "vkCmdBeginGpaSessionAMD") : NULL);
    if (!t->vkCmdBeginGpaSessionAMD)
        t->vkCmdBeginGpaSessionAMD = (PFN_vkCmdBeginGpaSessionAMD)gipa(instance, "vkCmdBeginGpaSessionAMD");
    t->vkCmdEndGpaSessionAMD = (PFN_vkCmdEndGpaSessionAMD)(device ? gdpa(device, "vkCmdEndGpaSessionAMD") : NULL);
    if (!t->vkCmdEndGpaSessionAMD)
        t->vkCmdEndGpaSessionAMD = (PFN_vkCmdEndGpaSessionAMD)gipa(instance, "vkCmdEndGpaSessionAMD");
    t->vkCmdBeginGpaSampleAMD = (PFN_vkCmdBeginGpaSampleAMD)(device ? gdpa(device, "vkCmdBeginGpaSampleAMD") : NULL);
    if (!t->vkCmdBeginGpaSampleAMD)
        t->vkCmdBeginGpaSampleAMD = (PFN_vkCmdBeginGpaSampleAMD)gipa(instance, "vkCmdBeginGpaSampleAMD");
    t->vkCmdEndGpaSampleAMD = (PFN_vkCmdEndGpaSampleAMD)(device ? gdpa(device, "vkCmdEndGpaSampleAMD") : NULL);
    if (!t->vkCmdEndGpaSampleAMD)
        t->vkCmdEndGpaSampleAMD = (PFN_vkCmdEndGpaSampleAMD)gipa(instance, "vkCmdEndGpaSampleAMD");
    t->vkGetGpaSessionStatusAMD = (PFN_vkGetGpaSessionStatusAMD)(device ? gdpa(device, "vkGetGpaSessionStatusAMD") : NULL);
    if (!t->vkGetGpaSessionStatusAMD)
        t->vkGetGpaSessionStatusAMD = (PFN_vkGetGpaSessionStatusAMD)gipa(instance, "vkGetGpaSessionStatusAMD");
    t->vkGetGpaSessionResultsAMD = (PFN_vkGetGpaSessionResultsAMD)(device ? gdpa(device, "vkGetGpaSessionResultsAMD") : NULL);
    if (!t->vkGetGpaSessionResultsAMD)
        t->vkGetGpaSessionResultsAMD = (PFN_vkGetGpaSessionResultsAMD)gipa(instance, "vkGetGpaSessionResultsAMD");
    t->vkResetGpaSessionAMD = (PFN_vkResetGpaSessionAMD)(device ? gdpa(device, "vkResetGpaSessionAMD") : NULL);
    if (!t->vkResetGpaSessionAMD)
        t->vkResetGpaSessionAMD = (PFN_vkResetGpaSessionAMD)gipa(instance, "vkResetGpaSessionAMD");
    t->vkCmdCopyGpaSessionResultsAMD = (PFN_vkCmdCopyGpaSessionResultsAMD)(device ? gdpa(device, "vkCmdCopyGpaSessionResultsAMD") : NULL);
    if (!t->vkCmdCopyGpaSessionResultsAMD)
        t->vkCmdCopyGpaSessionResultsAMD = (PFN_vkCmdCopyGpaSessionResultsAMD)gipa(instance, "vkCmdCopyGpaSessionResultsAMD");
    t->vkWriteSamplerDescriptorsEXT = (PFN_vkWriteSamplerDescriptorsEXT)(device ? gdpa(device, "vkWriteSamplerDescriptorsEXT") : NULL);
    if (!t->vkWriteSamplerDescriptorsEXT)
        t->vkWriteSamplerDescriptorsEXT = (PFN_vkWriteSamplerDescriptorsEXT)gipa(instance, "vkWriteSamplerDescriptorsEXT");
    t->vkWriteResourceDescriptorsEXT = (PFN_vkWriteResourceDescriptorsEXT)(device ? gdpa(device, "vkWriteResourceDescriptorsEXT") : NULL);
    if (!t->vkWriteResourceDescriptorsEXT)
        t->vkWriteResourceDescriptorsEXT = (PFN_vkWriteResourceDescriptorsEXT)gipa(instance, "vkWriteResourceDescriptorsEXT");
    t->vkCmdBindSamplerHeapEXT = (PFN_vkCmdBindSamplerHeapEXT)(device ? gdpa(device, "vkCmdBindSamplerHeapEXT") : NULL);
    if (!t->vkCmdBindSamplerHeapEXT)
        t->vkCmdBindSamplerHeapEXT = (PFN_vkCmdBindSamplerHeapEXT)gipa(instance, "vkCmdBindSamplerHeapEXT");
    t->vkCmdBindResourceHeapEXT = (PFN_vkCmdBindResourceHeapEXT)(device ? gdpa(device, "vkCmdBindResourceHeapEXT") : NULL);
    if (!t->vkCmdBindResourceHeapEXT)
        t->vkCmdBindResourceHeapEXT = (PFN_vkCmdBindResourceHeapEXT)gipa(instance, "vkCmdBindResourceHeapEXT");
    t->vkCmdPushDataEXT = (PFN_vkCmdPushDataEXT)(device ? gdpa(device, "vkCmdPushDataEXT") : NULL);
    if (!t->vkCmdPushDataEXT)
        t->vkCmdPushDataEXT = (PFN_vkCmdPushDataEXT)gipa(instance, "vkCmdPushDataEXT");
    t->vkGetImageOpaqueCaptureDataEXT = (PFN_vkGetImageOpaqueCaptureDataEXT)(device ? gdpa(device, "vkGetImageOpaqueCaptureDataEXT") : NULL);
    if (!t->vkGetImageOpaqueCaptureDataEXT)
        t->vkGetImageOpaqueCaptureDataEXT = (PFN_vkGetImageOpaqueCaptureDataEXT)gipa(instance, "vkGetImageOpaqueCaptureDataEXT");
    t->vkGetPhysicalDeviceDescriptorSizeEXT = (PFN_vkGetPhysicalDeviceDescriptorSizeEXT)gipa(instance, "vkGetPhysicalDeviceDescriptorSizeEXT");
    t->vkRegisterCustomBorderColorEXT = (PFN_vkRegisterCustomBorderColorEXT)(device ? gdpa(device, "vkRegisterCustomBorderColorEXT") : NULL);
    if (!t->vkRegisterCustomBorderColorEXT)
        t->vkRegisterCustomBorderColorEXT = (PFN_vkRegisterCustomBorderColorEXT)gipa(instance, "vkRegisterCustomBorderColorEXT");
    t->vkUnregisterCustomBorderColorEXT = (PFN_vkUnregisterCustomBorderColorEXT)(device ? gdpa(device, "vkUnregisterCustomBorderColorEXT") : NULL);
    if (!t->vkUnregisterCustomBorderColorEXT)
        t->vkUnregisterCustomBorderColorEXT = (PFN_vkUnregisterCustomBorderColorEXT)gipa(instance, "vkUnregisterCustomBorderColorEXT");
    t->vkGetTensorOpaqueCaptureDataARM = (PFN_vkGetTensorOpaqueCaptureDataARM)(device ? gdpa(device, "vkGetTensorOpaqueCaptureDataARM") : NULL);
    if (!t->vkGetTensorOpaqueCaptureDataARM)
        t->vkGetTensorOpaqueCaptureDataARM = (PFN_vkGetTensorOpaqueCaptureDataARM)gipa(instance, "vkGetTensorOpaqueCaptureDataARM");
    t->vkCmdSetSampleLocationsEXT = (PFN_vkCmdSetSampleLocationsEXT)(device ? gdpa(device, "vkCmdSetSampleLocationsEXT") : NULL);
    if (!t->vkCmdSetSampleLocationsEXT)
        t->vkCmdSetSampleLocationsEXT = (PFN_vkCmdSetSampleLocationsEXT)gipa(instance, "vkCmdSetSampleLocationsEXT");
    t->vkGetPhysicalDeviceMultisamplePropertiesEXT = (PFN_vkGetPhysicalDeviceMultisamplePropertiesEXT)gipa(instance, "vkGetPhysicalDeviceMultisamplePropertiesEXT");
    t->vkGetImageMemoryRequirements2KHR = (PFN_vkGetImageMemoryRequirements2KHR)(device ? gdpa(device, "vkGetImageMemoryRequirements2KHR") : NULL);
    if (!t->vkGetImageMemoryRequirements2KHR)
        t->vkGetImageMemoryRequirements2KHR = (PFN_vkGetImageMemoryRequirements2KHR)gipa(instance, "vkGetImageMemoryRequirements2KHR");
    t->vkGetBufferMemoryRequirements2KHR = (PFN_vkGetBufferMemoryRequirements2KHR)(device ? gdpa(device, "vkGetBufferMemoryRequirements2KHR") : NULL);
    if (!t->vkGetBufferMemoryRequirements2KHR)
        t->vkGetBufferMemoryRequirements2KHR = (PFN_vkGetBufferMemoryRequirements2KHR)gipa(instance, "vkGetBufferMemoryRequirements2KHR");
    t->vkGetImageSparseMemoryRequirements2KHR = (PFN_vkGetImageSparseMemoryRequirements2KHR)(device ? gdpa(device, "vkGetImageSparseMemoryRequirements2KHR") : NULL);
    if (!t->vkGetImageSparseMemoryRequirements2KHR)
        t->vkGetImageSparseMemoryRequirements2KHR = (PFN_vkGetImageSparseMemoryRequirements2KHR)gipa(instance, "vkGetImageSparseMemoryRequirements2KHR");
    t->vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)(device ? gdpa(device, "vkCreateAccelerationStructureKHR") : NULL);
    if (!t->vkCreateAccelerationStructureKHR)
        t->vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)gipa(instance, "vkCreateAccelerationStructureKHR");
    t->vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)(device ? gdpa(device, "vkDestroyAccelerationStructureKHR") : NULL);
    if (!t->vkDestroyAccelerationStructureKHR)
        t->vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)gipa(instance, "vkDestroyAccelerationStructureKHR");
    t->vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)(device ? gdpa(device, "vkCmdBuildAccelerationStructuresKHR") : NULL);
    if (!t->vkCmdBuildAccelerationStructuresKHR)
        t->vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)gipa(instance, "vkCmdBuildAccelerationStructuresKHR");
    t->vkCmdBuildAccelerationStructuresIndirectKHR = (PFN_vkCmdBuildAccelerationStructuresIndirectKHR)(device ? gdpa(device, "vkCmdBuildAccelerationStructuresIndirectKHR") : NULL);
    if (!t->vkCmdBuildAccelerationStructuresIndirectKHR)
        t->vkCmdBuildAccelerationStructuresIndirectKHR = (PFN_vkCmdBuildAccelerationStructuresIndirectKHR)gipa(instance, "vkCmdBuildAccelerationStructuresIndirectKHR");
    t->vkBuildAccelerationStructuresKHR = (PFN_vkBuildAccelerationStructuresKHR)(device ? gdpa(device, "vkBuildAccelerationStructuresKHR") : NULL);
    if (!t->vkBuildAccelerationStructuresKHR)
        t->vkBuildAccelerationStructuresKHR = (PFN_vkBuildAccelerationStructuresKHR)gipa(instance, "vkBuildAccelerationStructuresKHR");
    t->vkCopyAccelerationStructureKHR = (PFN_vkCopyAccelerationStructureKHR)(device ? gdpa(device, "vkCopyAccelerationStructureKHR") : NULL);
    if (!t->vkCopyAccelerationStructureKHR)
        t->vkCopyAccelerationStructureKHR = (PFN_vkCopyAccelerationStructureKHR)gipa(instance, "vkCopyAccelerationStructureKHR");
    t->vkCopyAccelerationStructureToMemoryKHR = (PFN_vkCopyAccelerationStructureToMemoryKHR)(device ? gdpa(device, "vkCopyAccelerationStructureToMemoryKHR") : NULL);
    if (!t->vkCopyAccelerationStructureToMemoryKHR)
        t->vkCopyAccelerationStructureToMemoryKHR = (PFN_vkCopyAccelerationStructureToMemoryKHR)gipa(instance, "vkCopyAccelerationStructureToMemoryKHR");
    t->vkCopyMemoryToAccelerationStructureKHR = (PFN_vkCopyMemoryToAccelerationStructureKHR)(device ? gdpa(device, "vkCopyMemoryToAccelerationStructureKHR") : NULL);
    if (!t->vkCopyMemoryToAccelerationStructureKHR)
        t->vkCopyMemoryToAccelerationStructureKHR = (PFN_vkCopyMemoryToAccelerationStructureKHR)gipa(instance, "vkCopyMemoryToAccelerationStructureKHR");
    t->vkWriteAccelerationStructuresPropertiesKHR = (PFN_vkWriteAccelerationStructuresPropertiesKHR)(device ? gdpa(device, "vkWriteAccelerationStructuresPropertiesKHR") : NULL);
    if (!t->vkWriteAccelerationStructuresPropertiesKHR)
        t->vkWriteAccelerationStructuresPropertiesKHR = (PFN_vkWriteAccelerationStructuresPropertiesKHR)gipa(instance, "vkWriteAccelerationStructuresPropertiesKHR");
    t->vkCmdCopyAccelerationStructureKHR = (PFN_vkCmdCopyAccelerationStructureKHR)(device ? gdpa(device, "vkCmdCopyAccelerationStructureKHR") : NULL);
    if (!t->vkCmdCopyAccelerationStructureKHR)
        t->vkCmdCopyAccelerationStructureKHR = (PFN_vkCmdCopyAccelerationStructureKHR)gipa(instance, "vkCmdCopyAccelerationStructureKHR");
    t->vkCmdCopyAccelerationStructureToMemoryKHR = (PFN_vkCmdCopyAccelerationStructureToMemoryKHR)(device ? gdpa(device, "vkCmdCopyAccelerationStructureToMemoryKHR") : NULL);
    if (!t->vkCmdCopyAccelerationStructureToMemoryKHR)
        t->vkCmdCopyAccelerationStructureToMemoryKHR = (PFN_vkCmdCopyAccelerationStructureToMemoryKHR)gipa(instance, "vkCmdCopyAccelerationStructureToMemoryKHR");
    t->vkCmdCopyMemoryToAccelerationStructureKHR = (PFN_vkCmdCopyMemoryToAccelerationStructureKHR)(device ? gdpa(device, "vkCmdCopyMemoryToAccelerationStructureKHR") : NULL);
    if (!t->vkCmdCopyMemoryToAccelerationStructureKHR)
        t->vkCmdCopyMemoryToAccelerationStructureKHR = (PFN_vkCmdCopyMemoryToAccelerationStructureKHR)gipa(instance, "vkCmdCopyMemoryToAccelerationStructureKHR");
    t->vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)(device ? gdpa(device, "vkGetAccelerationStructureDeviceAddressKHR") : NULL);
    if (!t->vkGetAccelerationStructureDeviceAddressKHR)
        t->vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)gipa(instance, "vkGetAccelerationStructureDeviceAddressKHR");
    t->vkCmdWriteAccelerationStructuresPropertiesKHR = (PFN_vkCmdWriteAccelerationStructuresPropertiesKHR)(device ? gdpa(device, "vkCmdWriteAccelerationStructuresPropertiesKHR") : NULL);
    if (!t->vkCmdWriteAccelerationStructuresPropertiesKHR)
        t->vkCmdWriteAccelerationStructuresPropertiesKHR = (PFN_vkCmdWriteAccelerationStructuresPropertiesKHR)gipa(instance, "vkCmdWriteAccelerationStructuresPropertiesKHR");
    t->vkGetDeviceAccelerationStructureCompatibilityKHR = (PFN_vkGetDeviceAccelerationStructureCompatibilityKHR)(device ? gdpa(device, "vkGetDeviceAccelerationStructureCompatibilityKHR") : NULL);
    if (!t->vkGetDeviceAccelerationStructureCompatibilityKHR)
        t->vkGetDeviceAccelerationStructureCompatibilityKHR = (PFN_vkGetDeviceAccelerationStructureCompatibilityKHR)gipa(instance, "vkGetDeviceAccelerationStructureCompatibilityKHR");
    t->vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)(device ? gdpa(device, "vkGetAccelerationStructureBuildSizesKHR") : NULL);
    if (!t->vkGetAccelerationStructureBuildSizesKHR)
        t->vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)gipa(instance, "vkGetAccelerationStructureBuildSizesKHR");
    t->vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)(device ? gdpa(device, "vkCmdTraceRaysKHR") : NULL);
    if (!t->vkCmdTraceRaysKHR)
        t->vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)gipa(instance, "vkCmdTraceRaysKHR");
    t->vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)(device ? gdpa(device, "vkCreateRayTracingPipelinesKHR") : NULL);
    if (!t->vkCreateRayTracingPipelinesKHR)
        t->vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)gipa(instance, "vkCreateRayTracingPipelinesKHR");
    t->vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)(device ? gdpa(device, "vkGetRayTracingShaderGroupHandlesKHR") : NULL);
    if (!t->vkGetRayTracingShaderGroupHandlesKHR)
        t->vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)gipa(instance, "vkGetRayTracingShaderGroupHandlesKHR");
    t->vkGetRayTracingCaptureReplayShaderGroupHandlesKHR = (PFN_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR)(device ? gdpa(device, "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR") : NULL);
    if (!t->vkGetRayTracingCaptureReplayShaderGroupHandlesKHR)
        t->vkGetRayTracingCaptureReplayShaderGroupHandlesKHR = (PFN_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR)gipa(instance, "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR");
    t->vkCmdTraceRaysIndirectKHR = (PFN_vkCmdTraceRaysIndirectKHR)(device ? gdpa(device, "vkCmdTraceRaysIndirectKHR") : NULL);
    if (!t->vkCmdTraceRaysIndirectKHR)
        t->vkCmdTraceRaysIndirectKHR = (PFN_vkCmdTraceRaysIndirectKHR)gipa(instance, "vkCmdTraceRaysIndirectKHR");
    t->vkGetRayTracingShaderGroupStackSizeKHR = (PFN_vkGetRayTracingShaderGroupStackSizeKHR)(device ? gdpa(device, "vkGetRayTracingShaderGroupStackSizeKHR") : NULL);
    if (!t->vkGetRayTracingShaderGroupStackSizeKHR)
        t->vkGetRayTracingShaderGroupStackSizeKHR = (PFN_vkGetRayTracingShaderGroupStackSizeKHR)gipa(instance, "vkGetRayTracingShaderGroupStackSizeKHR");
    t->vkCmdSetRayTracingPipelineStackSizeKHR = (PFN_vkCmdSetRayTracingPipelineStackSizeKHR)(device ? gdpa(device, "vkCmdSetRayTracingPipelineStackSizeKHR") : NULL);
    if (!t->vkCmdSetRayTracingPipelineStackSizeKHR)
        t->vkCmdSetRayTracingPipelineStackSizeKHR = (PFN_vkCmdSetRayTracingPipelineStackSizeKHR)gipa(instance, "vkCmdSetRayTracingPipelineStackSizeKHR");
    t->vkCreateSamplerYcbcrConversionKHR = (PFN_vkCreateSamplerYcbcrConversionKHR)(device ? gdpa(device, "vkCreateSamplerYcbcrConversionKHR") : NULL);
    if (!t->vkCreateSamplerYcbcrConversionKHR)
        t->vkCreateSamplerYcbcrConversionKHR = (PFN_vkCreateSamplerYcbcrConversionKHR)gipa(instance, "vkCreateSamplerYcbcrConversionKHR");
    t->vkDestroySamplerYcbcrConversionKHR = (PFN_vkDestroySamplerYcbcrConversionKHR)(device ? gdpa(device, "vkDestroySamplerYcbcrConversionKHR") : NULL);
    if (!t->vkDestroySamplerYcbcrConversionKHR)
        t->vkDestroySamplerYcbcrConversionKHR = (PFN_vkDestroySamplerYcbcrConversionKHR)gipa(instance, "vkDestroySamplerYcbcrConversionKHR");
    t->vkBindBufferMemory2KHR = (PFN_vkBindBufferMemory2KHR)(device ? gdpa(device, "vkBindBufferMemory2KHR") : NULL);
    if (!t->vkBindBufferMemory2KHR)
        t->vkBindBufferMemory2KHR = (PFN_vkBindBufferMemory2KHR)gipa(instance, "vkBindBufferMemory2KHR");
    t->vkBindImageMemory2KHR = (PFN_vkBindImageMemory2KHR)(device ? gdpa(device, "vkBindImageMemory2KHR") : NULL);
    if (!t->vkBindImageMemory2KHR)
        t->vkBindImageMemory2KHR = (PFN_vkBindImageMemory2KHR)gipa(instance, "vkBindImageMemory2KHR");
    t->vkGetImageDrmFormatModifierPropertiesEXT = (PFN_vkGetImageDrmFormatModifierPropertiesEXT)(device ? gdpa(device, "vkGetImageDrmFormatModifierPropertiesEXT") : NULL);
    if (!t->vkGetImageDrmFormatModifierPropertiesEXT)
        t->vkGetImageDrmFormatModifierPropertiesEXT = (PFN_vkGetImageDrmFormatModifierPropertiesEXT)gipa(instance, "vkGetImageDrmFormatModifierPropertiesEXT");
    t->vkCreateValidationCacheEXT = (PFN_vkCreateValidationCacheEXT)(device ? gdpa(device, "vkCreateValidationCacheEXT") : NULL);
    if (!t->vkCreateValidationCacheEXT)
        t->vkCreateValidationCacheEXT = (PFN_vkCreateValidationCacheEXT)gipa(instance, "vkCreateValidationCacheEXT");
    t->vkDestroyValidationCacheEXT = (PFN_vkDestroyValidationCacheEXT)(device ? gdpa(device, "vkDestroyValidationCacheEXT") : NULL);
    if (!t->vkDestroyValidationCacheEXT)
        t->vkDestroyValidationCacheEXT = (PFN_vkDestroyValidationCacheEXT)gipa(instance, "vkDestroyValidationCacheEXT");
    t->vkMergeValidationCachesEXT = (PFN_vkMergeValidationCachesEXT)(device ? gdpa(device, "vkMergeValidationCachesEXT") : NULL);
    if (!t->vkMergeValidationCachesEXT)
        t->vkMergeValidationCachesEXT = (PFN_vkMergeValidationCachesEXT)gipa(instance, "vkMergeValidationCachesEXT");
    t->vkGetValidationCacheDataEXT = (PFN_vkGetValidationCacheDataEXT)(device ? gdpa(device, "vkGetValidationCacheDataEXT") : NULL);
    if (!t->vkGetValidationCacheDataEXT)
        t->vkGetValidationCacheDataEXT = (PFN_vkGetValidationCacheDataEXT)gipa(instance, "vkGetValidationCacheDataEXT");
    t->vkCmdBindShadingRateImageNV = (PFN_vkCmdBindShadingRateImageNV)(device ? gdpa(device, "vkCmdBindShadingRateImageNV") : NULL);
    if (!t->vkCmdBindShadingRateImageNV)
        t->vkCmdBindShadingRateImageNV = (PFN_vkCmdBindShadingRateImageNV)gipa(instance, "vkCmdBindShadingRateImageNV");
    t->vkCmdSetViewportShadingRatePaletteNV = (PFN_vkCmdSetViewportShadingRatePaletteNV)(device ? gdpa(device, "vkCmdSetViewportShadingRatePaletteNV") : NULL);
    if (!t->vkCmdSetViewportShadingRatePaletteNV)
        t->vkCmdSetViewportShadingRatePaletteNV = (PFN_vkCmdSetViewportShadingRatePaletteNV)gipa(instance, "vkCmdSetViewportShadingRatePaletteNV");
    t->vkCmdSetCoarseSampleOrderNV = (PFN_vkCmdSetCoarseSampleOrderNV)(device ? gdpa(device, "vkCmdSetCoarseSampleOrderNV") : NULL);
    if (!t->vkCmdSetCoarseSampleOrderNV)
        t->vkCmdSetCoarseSampleOrderNV = (PFN_vkCmdSetCoarseSampleOrderNV)gipa(instance, "vkCmdSetCoarseSampleOrderNV");
    t->vkCreateAccelerationStructureNV = (PFN_vkCreateAccelerationStructureNV)(device ? gdpa(device, "vkCreateAccelerationStructureNV") : NULL);
    if (!t->vkCreateAccelerationStructureNV)
        t->vkCreateAccelerationStructureNV = (PFN_vkCreateAccelerationStructureNV)gipa(instance, "vkCreateAccelerationStructureNV");
    t->vkDestroyAccelerationStructureNV = (PFN_vkDestroyAccelerationStructureNV)(device ? gdpa(device, "vkDestroyAccelerationStructureNV") : NULL);
    if (!t->vkDestroyAccelerationStructureNV)
        t->vkDestroyAccelerationStructureNV = (PFN_vkDestroyAccelerationStructureNV)gipa(instance, "vkDestroyAccelerationStructureNV");
    t->vkGetAccelerationStructureMemoryRequirementsNV = (PFN_vkGetAccelerationStructureMemoryRequirementsNV)(device ? gdpa(device, "vkGetAccelerationStructureMemoryRequirementsNV") : NULL);
    if (!t->vkGetAccelerationStructureMemoryRequirementsNV)
        t->vkGetAccelerationStructureMemoryRequirementsNV = (PFN_vkGetAccelerationStructureMemoryRequirementsNV)gipa(instance, "vkGetAccelerationStructureMemoryRequirementsNV");
    t->vkBindAccelerationStructureMemoryNV = (PFN_vkBindAccelerationStructureMemoryNV)(device ? gdpa(device, "vkBindAccelerationStructureMemoryNV") : NULL);
    if (!t->vkBindAccelerationStructureMemoryNV)
        t->vkBindAccelerationStructureMemoryNV = (PFN_vkBindAccelerationStructureMemoryNV)gipa(instance, "vkBindAccelerationStructureMemoryNV");
    t->vkCmdBuildAccelerationStructureNV = (PFN_vkCmdBuildAccelerationStructureNV)(device ? gdpa(device, "vkCmdBuildAccelerationStructureNV") : NULL);
    if (!t->vkCmdBuildAccelerationStructureNV)
        t->vkCmdBuildAccelerationStructureNV = (PFN_vkCmdBuildAccelerationStructureNV)gipa(instance, "vkCmdBuildAccelerationStructureNV");
    t->vkCmdCopyAccelerationStructureNV = (PFN_vkCmdCopyAccelerationStructureNV)(device ? gdpa(device, "vkCmdCopyAccelerationStructureNV") : NULL);
    if (!t->vkCmdCopyAccelerationStructureNV)
        t->vkCmdCopyAccelerationStructureNV = (PFN_vkCmdCopyAccelerationStructureNV)gipa(instance, "vkCmdCopyAccelerationStructureNV");
    t->vkCmdTraceRaysNV = (PFN_vkCmdTraceRaysNV)(device ? gdpa(device, "vkCmdTraceRaysNV") : NULL);
    if (!t->vkCmdTraceRaysNV)
        t->vkCmdTraceRaysNV = (PFN_vkCmdTraceRaysNV)gipa(instance, "vkCmdTraceRaysNV");
    t->vkCreateRayTracingPipelinesNV = (PFN_vkCreateRayTracingPipelinesNV)(device ? gdpa(device, "vkCreateRayTracingPipelinesNV") : NULL);
    if (!t->vkCreateRayTracingPipelinesNV)
        t->vkCreateRayTracingPipelinesNV = (PFN_vkCreateRayTracingPipelinesNV)gipa(instance, "vkCreateRayTracingPipelinesNV");
    t->vkGetRayTracingShaderGroupHandlesNV = (PFN_vkGetRayTracingShaderGroupHandlesNV)(device ? gdpa(device, "vkGetRayTracingShaderGroupHandlesNV") : NULL);
    if (!t->vkGetRayTracingShaderGroupHandlesNV)
        t->vkGetRayTracingShaderGroupHandlesNV = (PFN_vkGetRayTracingShaderGroupHandlesNV)gipa(instance, "vkGetRayTracingShaderGroupHandlesNV");
    t->vkGetAccelerationStructureHandleNV = (PFN_vkGetAccelerationStructureHandleNV)(device ? gdpa(device, "vkGetAccelerationStructureHandleNV") : NULL);
    if (!t->vkGetAccelerationStructureHandleNV)
        t->vkGetAccelerationStructureHandleNV = (PFN_vkGetAccelerationStructureHandleNV)gipa(instance, "vkGetAccelerationStructureHandleNV");
    t->vkCmdWriteAccelerationStructuresPropertiesNV = (PFN_vkCmdWriteAccelerationStructuresPropertiesNV)(device ? gdpa(device, "vkCmdWriteAccelerationStructuresPropertiesNV") : NULL);
    if (!t->vkCmdWriteAccelerationStructuresPropertiesNV)
        t->vkCmdWriteAccelerationStructuresPropertiesNV = (PFN_vkCmdWriteAccelerationStructuresPropertiesNV)gipa(instance, "vkCmdWriteAccelerationStructuresPropertiesNV");
    t->vkCompileDeferredNV = (PFN_vkCompileDeferredNV)(device ? gdpa(device, "vkCompileDeferredNV") : NULL);
    if (!t->vkCompileDeferredNV)
        t->vkCompileDeferredNV = (PFN_vkCompileDeferredNV)gipa(instance, "vkCompileDeferredNV");
    t->vkGetDescriptorSetLayoutSupportKHR = (PFN_vkGetDescriptorSetLayoutSupportKHR)(device ? gdpa(device, "vkGetDescriptorSetLayoutSupportKHR") : NULL);
    if (!t->vkGetDescriptorSetLayoutSupportKHR)
        t->vkGetDescriptorSetLayoutSupportKHR = (PFN_vkGetDescriptorSetLayoutSupportKHR)gipa(instance, "vkGetDescriptorSetLayoutSupportKHR");
    t->vkCmdDrawIndirectCountKHR = (PFN_vkCmdDrawIndirectCountKHR)(device ? gdpa(device, "vkCmdDrawIndirectCountKHR") : NULL);
    if (!t->vkCmdDrawIndirectCountKHR)
        t->vkCmdDrawIndirectCountKHR = (PFN_vkCmdDrawIndirectCountKHR)gipa(instance, "vkCmdDrawIndirectCountKHR");
    t->vkCmdDrawIndexedIndirectCountKHR = (PFN_vkCmdDrawIndexedIndirectCountKHR)(device ? gdpa(device, "vkCmdDrawIndexedIndirectCountKHR") : NULL);
    if (!t->vkCmdDrawIndexedIndirectCountKHR)
        t->vkCmdDrawIndexedIndirectCountKHR = (PFN_vkCmdDrawIndexedIndirectCountKHR)gipa(instance, "vkCmdDrawIndexedIndirectCountKHR");
    t->vkCmdWriteBufferMarkerAMD = (PFN_vkCmdWriteBufferMarkerAMD)(device ? gdpa(device, "vkCmdWriteBufferMarkerAMD") : NULL);
    if (!t->vkCmdWriteBufferMarkerAMD)
        t->vkCmdWriteBufferMarkerAMD = (PFN_vkCmdWriteBufferMarkerAMD)gipa(instance, "vkCmdWriteBufferMarkerAMD");
    t->vkCmdWriteBufferMarker2AMD = (PFN_vkCmdWriteBufferMarker2AMD)(device ? gdpa(device, "vkCmdWriteBufferMarker2AMD") : NULL);
    if (!t->vkCmdWriteBufferMarker2AMD)
        t->vkCmdWriteBufferMarker2AMD = (PFN_vkCmdWriteBufferMarker2AMD)gipa(instance, "vkCmdWriteBufferMarker2AMD");
    t->vkGetPhysicalDeviceCalibrateableTimeDomainsEXT = (PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT)gipa(instance, "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT");
    t->vkGetCalibratedTimestampsEXT = (PFN_vkGetCalibratedTimestampsEXT)(device ? gdpa(device, "vkGetCalibratedTimestampsEXT") : NULL);
    if (!t->vkGetCalibratedTimestampsEXT)
        t->vkGetCalibratedTimestampsEXT = (PFN_vkGetCalibratedTimestampsEXT)gipa(instance, "vkGetCalibratedTimestampsEXT");
    t->vkCmdDrawMeshTasksNV = (PFN_vkCmdDrawMeshTasksNV)(device ? gdpa(device, "vkCmdDrawMeshTasksNV") : NULL);
    if (!t->vkCmdDrawMeshTasksNV)
        t->vkCmdDrawMeshTasksNV = (PFN_vkCmdDrawMeshTasksNV)gipa(instance, "vkCmdDrawMeshTasksNV");
    t->vkCmdDrawMeshTasksIndirectNV = (PFN_vkCmdDrawMeshTasksIndirectNV)(device ? gdpa(device, "vkCmdDrawMeshTasksIndirectNV") : NULL);
    if (!t->vkCmdDrawMeshTasksIndirectNV)
        t->vkCmdDrawMeshTasksIndirectNV = (PFN_vkCmdDrawMeshTasksIndirectNV)gipa(instance, "vkCmdDrawMeshTasksIndirectNV");
    t->vkCmdDrawMeshTasksIndirectCountNV = (PFN_vkCmdDrawMeshTasksIndirectCountNV)(device ? gdpa(device, "vkCmdDrawMeshTasksIndirectCountNV") : NULL);
    if (!t->vkCmdDrawMeshTasksIndirectCountNV)
        t->vkCmdDrawMeshTasksIndirectCountNV = (PFN_vkCmdDrawMeshTasksIndirectCountNV)gipa(instance, "vkCmdDrawMeshTasksIndirectCountNV");
    t->vkCmdSetExclusiveScissorEnableNV = (PFN_vkCmdSetExclusiveScissorEnableNV)(device ? gdpa(device, "vkCmdSetExclusiveScissorEnableNV") : NULL);
    if (!t->vkCmdSetExclusiveScissorEnableNV)
        t->vkCmdSetExclusiveScissorEnableNV = (PFN_vkCmdSetExclusiveScissorEnableNV)gipa(instance, "vkCmdSetExclusiveScissorEnableNV");
    t->vkCmdSetExclusiveScissorNV = (PFN_vkCmdSetExclusiveScissorNV)(device ? gdpa(device, "vkCmdSetExclusiveScissorNV") : NULL);
    if (!t->vkCmdSetExclusiveScissorNV)
        t->vkCmdSetExclusiveScissorNV = (PFN_vkCmdSetExclusiveScissorNV)gipa(instance, "vkCmdSetExclusiveScissorNV");
    t->vkCmdSetCheckpointNV = (PFN_vkCmdSetCheckpointNV)(device ? gdpa(device, "vkCmdSetCheckpointNV") : NULL);
    if (!t->vkCmdSetCheckpointNV)
        t->vkCmdSetCheckpointNV = (PFN_vkCmdSetCheckpointNV)gipa(instance, "vkCmdSetCheckpointNV");
    t->vkGetQueueCheckpointDataNV = (PFN_vkGetQueueCheckpointDataNV)(device ? gdpa(device, "vkGetQueueCheckpointDataNV") : NULL);
    if (!t->vkGetQueueCheckpointDataNV)
        t->vkGetQueueCheckpointDataNV = (PFN_vkGetQueueCheckpointDataNV)gipa(instance, "vkGetQueueCheckpointDataNV");
    t->vkGetQueueCheckpointData2NV = (PFN_vkGetQueueCheckpointData2NV)(device ? gdpa(device, "vkGetQueueCheckpointData2NV") : NULL);
    if (!t->vkGetQueueCheckpointData2NV)
        t->vkGetQueueCheckpointData2NV = (PFN_vkGetQueueCheckpointData2NV)gipa(instance, "vkGetQueueCheckpointData2NV");
    t->vkGetSemaphoreCounterValueKHR = (PFN_vkGetSemaphoreCounterValueKHR)(device ? gdpa(device, "vkGetSemaphoreCounterValueKHR") : NULL);
    if (!t->vkGetSemaphoreCounterValueKHR)
        t->vkGetSemaphoreCounterValueKHR = (PFN_vkGetSemaphoreCounterValueKHR)gipa(instance, "vkGetSemaphoreCounterValueKHR");
    t->vkWaitSemaphoresKHR = (PFN_vkWaitSemaphoresKHR)(device ? gdpa(device, "vkWaitSemaphoresKHR") : NULL);
    if (!t->vkWaitSemaphoresKHR)
        t->vkWaitSemaphoresKHR = (PFN_vkWaitSemaphoresKHR)gipa(instance, "vkWaitSemaphoresKHR");
    t->vkSignalSemaphoreKHR = (PFN_vkSignalSemaphoreKHR)(device ? gdpa(device, "vkSignalSemaphoreKHR") : NULL);
    if (!t->vkSignalSemaphoreKHR)
        t->vkSignalSemaphoreKHR = (PFN_vkSignalSemaphoreKHR)gipa(instance, "vkSignalSemaphoreKHR");
    t->vkInitializePerformanceApiINTEL = (PFN_vkInitializePerformanceApiINTEL)(device ? gdpa(device, "vkInitializePerformanceApiINTEL") : NULL);
    if (!t->vkInitializePerformanceApiINTEL)
        t->vkInitializePerformanceApiINTEL = (PFN_vkInitializePerformanceApiINTEL)gipa(instance, "vkInitializePerformanceApiINTEL");
    t->vkUninitializePerformanceApiINTEL = (PFN_vkUninitializePerformanceApiINTEL)(device ? gdpa(device, "vkUninitializePerformanceApiINTEL") : NULL);
    if (!t->vkUninitializePerformanceApiINTEL)
        t->vkUninitializePerformanceApiINTEL = (PFN_vkUninitializePerformanceApiINTEL)gipa(instance, "vkUninitializePerformanceApiINTEL");
    t->vkCmdSetPerformanceMarkerINTEL = (PFN_vkCmdSetPerformanceMarkerINTEL)(device ? gdpa(device, "vkCmdSetPerformanceMarkerINTEL") : NULL);
    if (!t->vkCmdSetPerformanceMarkerINTEL)
        t->vkCmdSetPerformanceMarkerINTEL = (PFN_vkCmdSetPerformanceMarkerINTEL)gipa(instance, "vkCmdSetPerformanceMarkerINTEL");
    t->vkCmdSetPerformanceStreamMarkerINTEL = (PFN_vkCmdSetPerformanceStreamMarkerINTEL)(device ? gdpa(device, "vkCmdSetPerformanceStreamMarkerINTEL") : NULL);
    if (!t->vkCmdSetPerformanceStreamMarkerINTEL)
        t->vkCmdSetPerformanceStreamMarkerINTEL = (PFN_vkCmdSetPerformanceStreamMarkerINTEL)gipa(instance, "vkCmdSetPerformanceStreamMarkerINTEL");
    t->vkCmdSetPerformanceOverrideINTEL = (PFN_vkCmdSetPerformanceOverrideINTEL)(device ? gdpa(device, "vkCmdSetPerformanceOverrideINTEL") : NULL);
    if (!t->vkCmdSetPerformanceOverrideINTEL)
        t->vkCmdSetPerformanceOverrideINTEL = (PFN_vkCmdSetPerformanceOverrideINTEL)gipa(instance, "vkCmdSetPerformanceOverrideINTEL");
    t->vkAcquirePerformanceConfigurationINTEL = (PFN_vkAcquirePerformanceConfigurationINTEL)(device ? gdpa(device, "vkAcquirePerformanceConfigurationINTEL") : NULL);
    if (!t->vkAcquirePerformanceConfigurationINTEL)
        t->vkAcquirePerformanceConfigurationINTEL = (PFN_vkAcquirePerformanceConfigurationINTEL)gipa(instance, "vkAcquirePerformanceConfigurationINTEL");
    t->vkReleasePerformanceConfigurationINTEL = (PFN_vkReleasePerformanceConfigurationINTEL)(device ? gdpa(device, "vkReleasePerformanceConfigurationINTEL") : NULL);
    if (!t->vkReleasePerformanceConfigurationINTEL)
        t->vkReleasePerformanceConfigurationINTEL = (PFN_vkReleasePerformanceConfigurationINTEL)gipa(instance, "vkReleasePerformanceConfigurationINTEL");
    t->vkQueueSetPerformanceConfigurationINTEL = (PFN_vkQueueSetPerformanceConfigurationINTEL)(device ? gdpa(device, "vkQueueSetPerformanceConfigurationINTEL") : NULL);
    if (!t->vkQueueSetPerformanceConfigurationINTEL)
        t->vkQueueSetPerformanceConfigurationINTEL = (PFN_vkQueueSetPerformanceConfigurationINTEL)gipa(instance, "vkQueueSetPerformanceConfigurationINTEL");
    t->vkGetPerformanceParameterINTEL = (PFN_vkGetPerformanceParameterINTEL)(device ? gdpa(device, "vkGetPerformanceParameterINTEL") : NULL);
    if (!t->vkGetPerformanceParameterINTEL)
        t->vkGetPerformanceParameterINTEL = (PFN_vkGetPerformanceParameterINTEL)gipa(instance, "vkGetPerformanceParameterINTEL");
    t->vkGetPhysicalDeviceFragmentShadingRatesKHR = (PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR)gipa(instance, "vkGetPhysicalDeviceFragmentShadingRatesKHR");
    t->vkCmdSetFragmentShadingRateKHR = (PFN_vkCmdSetFragmentShadingRateKHR)(device ? gdpa(device, "vkCmdSetFragmentShadingRateKHR") : NULL);
    if (!t->vkCmdSetFragmentShadingRateKHR)
        t->vkCmdSetFragmentShadingRateKHR = (PFN_vkCmdSetFragmentShadingRateKHR)gipa(instance, "vkCmdSetFragmentShadingRateKHR");
    t->vkCmdSetRenderingAttachmentLocationsKHR = (PFN_vkCmdSetRenderingAttachmentLocationsKHR)(device ? gdpa(device, "vkCmdSetRenderingAttachmentLocationsKHR") : NULL);
    if (!t->vkCmdSetRenderingAttachmentLocationsKHR)
        t->vkCmdSetRenderingAttachmentLocationsKHR = (PFN_vkCmdSetRenderingAttachmentLocationsKHR)gipa(instance, "vkCmdSetRenderingAttachmentLocationsKHR");
    t->vkCmdSetRenderingInputAttachmentIndicesKHR = (PFN_vkCmdSetRenderingInputAttachmentIndicesKHR)(device ? gdpa(device, "vkCmdSetRenderingInputAttachmentIndicesKHR") : NULL);
    if (!t->vkCmdSetRenderingInputAttachmentIndicesKHR)
        t->vkCmdSetRenderingInputAttachmentIndicesKHR = (PFN_vkCmdSetRenderingInputAttachmentIndicesKHR)gipa(instance, "vkCmdSetRenderingInputAttachmentIndicesKHR");
    t->vkGetBufferDeviceAddressEXT = (PFN_vkGetBufferDeviceAddressEXT)(device ? gdpa(device, "vkGetBufferDeviceAddressEXT") : NULL);
    if (!t->vkGetBufferDeviceAddressEXT)
        t->vkGetBufferDeviceAddressEXT = (PFN_vkGetBufferDeviceAddressEXT)gipa(instance, "vkGetBufferDeviceAddressEXT");
    t->vkGetPhysicalDeviceToolPropertiesEXT = (PFN_vkGetPhysicalDeviceToolPropertiesEXT)gipa(instance, "vkGetPhysicalDeviceToolPropertiesEXT");
    t->vkGetPhysicalDeviceCooperativeMatrixPropertiesNV = (PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV)gipa(instance, "vkGetPhysicalDeviceCooperativeMatrixPropertiesNV");
    t->vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV = (PFN_vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV)gipa(instance, "vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV");
    t->vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)(device ? gdpa(device, "vkGetBufferDeviceAddressKHR") : NULL);
    if (!t->vkGetBufferDeviceAddressKHR)
        t->vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)gipa(instance, "vkGetBufferDeviceAddressKHR");
    t->vkGetBufferOpaqueCaptureAddressKHR = (PFN_vkGetBufferOpaqueCaptureAddressKHR)(device ? gdpa(device, "vkGetBufferOpaqueCaptureAddressKHR") : NULL);
    if (!t->vkGetBufferOpaqueCaptureAddressKHR)
        t->vkGetBufferOpaqueCaptureAddressKHR = (PFN_vkGetBufferOpaqueCaptureAddressKHR)gipa(instance, "vkGetBufferOpaqueCaptureAddressKHR");
    t->vkGetDeviceMemoryOpaqueCaptureAddressKHR = (PFN_vkGetDeviceMemoryOpaqueCaptureAddressKHR)(device ? gdpa(device, "vkGetDeviceMemoryOpaqueCaptureAddressKHR") : NULL);
    if (!t->vkGetDeviceMemoryOpaqueCaptureAddressKHR)
        t->vkGetDeviceMemoryOpaqueCaptureAddressKHR = (PFN_vkGetDeviceMemoryOpaqueCaptureAddressKHR)gipa(instance, "vkGetDeviceMemoryOpaqueCaptureAddressKHR");
    t->vkCmdSetLineStippleEXT = (PFN_vkCmdSetLineStippleEXT)(device ? gdpa(device, "vkCmdSetLineStippleEXT") : NULL);
    if (!t->vkCmdSetLineStippleEXT)
        t->vkCmdSetLineStippleEXT = (PFN_vkCmdSetLineStippleEXT)gipa(instance, "vkCmdSetLineStippleEXT");
    t->vkResetQueryPoolEXT = (PFN_vkResetQueryPoolEXT)(device ? gdpa(device, "vkResetQueryPoolEXT") : NULL);
    if (!t->vkResetQueryPoolEXT)
        t->vkResetQueryPoolEXT = (PFN_vkResetQueryPoolEXT)gipa(instance, "vkResetQueryPoolEXT");
    t->vkCmdSetCullModeEXT = (PFN_vkCmdSetCullModeEXT)(device ? gdpa(device, "vkCmdSetCullModeEXT") : NULL);
    if (!t->vkCmdSetCullModeEXT)
        t->vkCmdSetCullModeEXT = (PFN_vkCmdSetCullModeEXT)gipa(instance, "vkCmdSetCullModeEXT");
    t->vkCmdSetFrontFaceEXT = (PFN_vkCmdSetFrontFaceEXT)(device ? gdpa(device, "vkCmdSetFrontFaceEXT") : NULL);
    if (!t->vkCmdSetFrontFaceEXT)
        t->vkCmdSetFrontFaceEXT = (PFN_vkCmdSetFrontFaceEXT)gipa(instance, "vkCmdSetFrontFaceEXT");
    t->vkCmdSetPrimitiveTopologyEXT = (PFN_vkCmdSetPrimitiveTopologyEXT)(device ? gdpa(device, "vkCmdSetPrimitiveTopologyEXT") : NULL);
    if (!t->vkCmdSetPrimitiveTopologyEXT)
        t->vkCmdSetPrimitiveTopologyEXT = (PFN_vkCmdSetPrimitiveTopologyEXT)gipa(instance, "vkCmdSetPrimitiveTopologyEXT");
    t->vkCmdSetViewportWithCountEXT = (PFN_vkCmdSetViewportWithCountEXT)(device ? gdpa(device, "vkCmdSetViewportWithCountEXT") : NULL);
    if (!t->vkCmdSetViewportWithCountEXT)
        t->vkCmdSetViewportWithCountEXT = (PFN_vkCmdSetViewportWithCountEXT)gipa(instance, "vkCmdSetViewportWithCountEXT");
    t->vkCmdSetScissorWithCountEXT = (PFN_vkCmdSetScissorWithCountEXT)(device ? gdpa(device, "vkCmdSetScissorWithCountEXT") : NULL);
    if (!t->vkCmdSetScissorWithCountEXT)
        t->vkCmdSetScissorWithCountEXT = (PFN_vkCmdSetScissorWithCountEXT)gipa(instance, "vkCmdSetScissorWithCountEXT");
    t->vkCmdBindVertexBuffers2EXT = (PFN_vkCmdBindVertexBuffers2EXT)(device ? gdpa(device, "vkCmdBindVertexBuffers2EXT") : NULL);
    if (!t->vkCmdBindVertexBuffers2EXT)
        t->vkCmdBindVertexBuffers2EXT = (PFN_vkCmdBindVertexBuffers2EXT)gipa(instance, "vkCmdBindVertexBuffers2EXT");
    t->vkCmdSetDepthTestEnableEXT = (PFN_vkCmdSetDepthTestEnableEXT)(device ? gdpa(device, "vkCmdSetDepthTestEnableEXT") : NULL);
    if (!t->vkCmdSetDepthTestEnableEXT)
        t->vkCmdSetDepthTestEnableEXT = (PFN_vkCmdSetDepthTestEnableEXT)gipa(instance, "vkCmdSetDepthTestEnableEXT");
    t->vkCmdSetDepthWriteEnableEXT = (PFN_vkCmdSetDepthWriteEnableEXT)(device ? gdpa(device, "vkCmdSetDepthWriteEnableEXT") : NULL);
    if (!t->vkCmdSetDepthWriteEnableEXT)
        t->vkCmdSetDepthWriteEnableEXT = (PFN_vkCmdSetDepthWriteEnableEXT)gipa(instance, "vkCmdSetDepthWriteEnableEXT");
    t->vkCmdSetDepthCompareOpEXT = (PFN_vkCmdSetDepthCompareOpEXT)(device ? gdpa(device, "vkCmdSetDepthCompareOpEXT") : NULL);
    if (!t->vkCmdSetDepthCompareOpEXT)
        t->vkCmdSetDepthCompareOpEXT = (PFN_vkCmdSetDepthCompareOpEXT)gipa(instance, "vkCmdSetDepthCompareOpEXT");
    t->vkCmdSetDepthBoundsTestEnableEXT = (PFN_vkCmdSetDepthBoundsTestEnableEXT)(device ? gdpa(device, "vkCmdSetDepthBoundsTestEnableEXT") : NULL);
    if (!t->vkCmdSetDepthBoundsTestEnableEXT)
        t->vkCmdSetDepthBoundsTestEnableEXT = (PFN_vkCmdSetDepthBoundsTestEnableEXT)gipa(instance, "vkCmdSetDepthBoundsTestEnableEXT");
    t->vkCmdSetStencilTestEnableEXT = (PFN_vkCmdSetStencilTestEnableEXT)(device ? gdpa(device, "vkCmdSetStencilTestEnableEXT") : NULL);
    if (!t->vkCmdSetStencilTestEnableEXT)
        t->vkCmdSetStencilTestEnableEXT = (PFN_vkCmdSetStencilTestEnableEXT)gipa(instance, "vkCmdSetStencilTestEnableEXT");
    t->vkCmdSetStencilOpEXT = (PFN_vkCmdSetStencilOpEXT)(device ? gdpa(device, "vkCmdSetStencilOpEXT") : NULL);
    if (!t->vkCmdSetStencilOpEXT)
        t->vkCmdSetStencilOpEXT = (PFN_vkCmdSetStencilOpEXT)gipa(instance, "vkCmdSetStencilOpEXT");
    t->vkCreateDeferredOperationKHR = (PFN_vkCreateDeferredOperationKHR)(device ? gdpa(device, "vkCreateDeferredOperationKHR") : NULL);
    if (!t->vkCreateDeferredOperationKHR)
        t->vkCreateDeferredOperationKHR = (PFN_vkCreateDeferredOperationKHR)gipa(instance, "vkCreateDeferredOperationKHR");
    t->vkDestroyDeferredOperationKHR = (PFN_vkDestroyDeferredOperationKHR)(device ? gdpa(device, "vkDestroyDeferredOperationKHR") : NULL);
    if (!t->vkDestroyDeferredOperationKHR)
        t->vkDestroyDeferredOperationKHR = (PFN_vkDestroyDeferredOperationKHR)gipa(instance, "vkDestroyDeferredOperationKHR");
    t->vkGetDeferredOperationMaxConcurrencyKHR = (PFN_vkGetDeferredOperationMaxConcurrencyKHR)(device ? gdpa(device, "vkGetDeferredOperationMaxConcurrencyKHR") : NULL);
    if (!t->vkGetDeferredOperationMaxConcurrencyKHR)
        t->vkGetDeferredOperationMaxConcurrencyKHR = (PFN_vkGetDeferredOperationMaxConcurrencyKHR)gipa(instance, "vkGetDeferredOperationMaxConcurrencyKHR");
    t->vkGetDeferredOperationResultKHR = (PFN_vkGetDeferredOperationResultKHR)(device ? gdpa(device, "vkGetDeferredOperationResultKHR") : NULL);
    if (!t->vkGetDeferredOperationResultKHR)
        t->vkGetDeferredOperationResultKHR = (PFN_vkGetDeferredOperationResultKHR)gipa(instance, "vkGetDeferredOperationResultKHR");
    t->vkDeferredOperationJoinKHR = (PFN_vkDeferredOperationJoinKHR)(device ? gdpa(device, "vkDeferredOperationJoinKHR") : NULL);
    if (!t->vkDeferredOperationJoinKHR)
        t->vkDeferredOperationJoinKHR = (PFN_vkDeferredOperationJoinKHR)gipa(instance, "vkDeferredOperationJoinKHR");
    t->vkGetPipelineExecutablePropertiesKHR = (PFN_vkGetPipelineExecutablePropertiesKHR)(device ? gdpa(device, "vkGetPipelineExecutablePropertiesKHR") : NULL);
    if (!t->vkGetPipelineExecutablePropertiesKHR)
        t->vkGetPipelineExecutablePropertiesKHR = (PFN_vkGetPipelineExecutablePropertiesKHR)gipa(instance, "vkGetPipelineExecutablePropertiesKHR");
    t->vkGetPipelineExecutableStatisticsKHR = (PFN_vkGetPipelineExecutableStatisticsKHR)(device ? gdpa(device, "vkGetPipelineExecutableStatisticsKHR") : NULL);
    if (!t->vkGetPipelineExecutableStatisticsKHR)
        t->vkGetPipelineExecutableStatisticsKHR = (PFN_vkGetPipelineExecutableStatisticsKHR)gipa(instance, "vkGetPipelineExecutableStatisticsKHR");
    t->vkGetPipelineExecutableInternalRepresentationsKHR = (PFN_vkGetPipelineExecutableInternalRepresentationsKHR)(device ? gdpa(device, "vkGetPipelineExecutableInternalRepresentationsKHR") : NULL);
    if (!t->vkGetPipelineExecutableInternalRepresentationsKHR)
        t->vkGetPipelineExecutableInternalRepresentationsKHR = (PFN_vkGetPipelineExecutableInternalRepresentationsKHR)gipa(instance, "vkGetPipelineExecutableInternalRepresentationsKHR");
    t->vkCopyMemoryToImageEXT = (PFN_vkCopyMemoryToImageEXT)(device ? gdpa(device, "vkCopyMemoryToImageEXT") : NULL);
    if (!t->vkCopyMemoryToImageEXT)
        t->vkCopyMemoryToImageEXT = (PFN_vkCopyMemoryToImageEXT)gipa(instance, "vkCopyMemoryToImageEXT");
    t->vkCopyImageToMemoryEXT = (PFN_vkCopyImageToMemoryEXT)(device ? gdpa(device, "vkCopyImageToMemoryEXT") : NULL);
    if (!t->vkCopyImageToMemoryEXT)
        t->vkCopyImageToMemoryEXT = (PFN_vkCopyImageToMemoryEXT)gipa(instance, "vkCopyImageToMemoryEXT");
    t->vkCopyImageToImageEXT = (PFN_vkCopyImageToImageEXT)(device ? gdpa(device, "vkCopyImageToImageEXT") : NULL);
    if (!t->vkCopyImageToImageEXT)
        t->vkCopyImageToImageEXT = (PFN_vkCopyImageToImageEXT)gipa(instance, "vkCopyImageToImageEXT");
    t->vkTransitionImageLayoutEXT = (PFN_vkTransitionImageLayoutEXT)(device ? gdpa(device, "vkTransitionImageLayoutEXT") : NULL);
    if (!t->vkTransitionImageLayoutEXT)
        t->vkTransitionImageLayoutEXT = (PFN_vkTransitionImageLayoutEXT)gipa(instance, "vkTransitionImageLayoutEXT");
    t->vkGetImageSubresourceLayout2EXT = (PFN_vkGetImageSubresourceLayout2EXT)(device ? gdpa(device, "vkGetImageSubresourceLayout2EXT") : NULL);
    if (!t->vkGetImageSubresourceLayout2EXT)
        t->vkGetImageSubresourceLayout2EXT = (PFN_vkGetImageSubresourceLayout2EXT)gipa(instance, "vkGetImageSubresourceLayout2EXT");
    t->vkMapMemory2KHR = (PFN_vkMapMemory2KHR)(device ? gdpa(device, "vkMapMemory2KHR") : NULL);
    if (!t->vkMapMemory2KHR)
        t->vkMapMemory2KHR = (PFN_vkMapMemory2KHR)gipa(instance, "vkMapMemory2KHR");
    t->vkUnmapMemory2KHR = (PFN_vkUnmapMemory2KHR)(device ? gdpa(device, "vkUnmapMemory2KHR") : NULL);
    if (!t->vkUnmapMemory2KHR)
        t->vkUnmapMemory2KHR = (PFN_vkUnmapMemory2KHR)gipa(instance, "vkUnmapMemory2KHR");
    t->vkGetGeneratedCommandsMemoryRequirementsNV = (PFN_vkGetGeneratedCommandsMemoryRequirementsNV)(device ? gdpa(device, "vkGetGeneratedCommandsMemoryRequirementsNV") : NULL);
    if (!t->vkGetGeneratedCommandsMemoryRequirementsNV)
        t->vkGetGeneratedCommandsMemoryRequirementsNV = (PFN_vkGetGeneratedCommandsMemoryRequirementsNV)gipa(instance, "vkGetGeneratedCommandsMemoryRequirementsNV");
    t->vkCmdPreprocessGeneratedCommandsNV = (PFN_vkCmdPreprocessGeneratedCommandsNV)(device ? gdpa(device, "vkCmdPreprocessGeneratedCommandsNV") : NULL);
    if (!t->vkCmdPreprocessGeneratedCommandsNV)
        t->vkCmdPreprocessGeneratedCommandsNV = (PFN_vkCmdPreprocessGeneratedCommandsNV)gipa(instance, "vkCmdPreprocessGeneratedCommandsNV");
    t->vkCmdExecuteGeneratedCommandsNV = (PFN_vkCmdExecuteGeneratedCommandsNV)(device ? gdpa(device, "vkCmdExecuteGeneratedCommandsNV") : NULL);
    if (!t->vkCmdExecuteGeneratedCommandsNV)
        t->vkCmdExecuteGeneratedCommandsNV = (PFN_vkCmdExecuteGeneratedCommandsNV)gipa(instance, "vkCmdExecuteGeneratedCommandsNV");
    t->vkCmdBindPipelineShaderGroupNV = (PFN_vkCmdBindPipelineShaderGroupNV)(device ? gdpa(device, "vkCmdBindPipelineShaderGroupNV") : NULL);
    if (!t->vkCmdBindPipelineShaderGroupNV)
        t->vkCmdBindPipelineShaderGroupNV = (PFN_vkCmdBindPipelineShaderGroupNV)gipa(instance, "vkCmdBindPipelineShaderGroupNV");
    t->vkCreateIndirectCommandsLayoutNV = (PFN_vkCreateIndirectCommandsLayoutNV)(device ? gdpa(device, "vkCreateIndirectCommandsLayoutNV") : NULL);
    if (!t->vkCreateIndirectCommandsLayoutNV)
        t->vkCreateIndirectCommandsLayoutNV = (PFN_vkCreateIndirectCommandsLayoutNV)gipa(instance, "vkCreateIndirectCommandsLayoutNV");
    t->vkDestroyIndirectCommandsLayoutNV = (PFN_vkDestroyIndirectCommandsLayoutNV)(device ? gdpa(device, "vkDestroyIndirectCommandsLayoutNV") : NULL);
    if (!t->vkDestroyIndirectCommandsLayoutNV)
        t->vkDestroyIndirectCommandsLayoutNV = (PFN_vkDestroyIndirectCommandsLayoutNV)gipa(instance, "vkDestroyIndirectCommandsLayoutNV");
    t->vkCmdSetDepthBias2EXT = (PFN_vkCmdSetDepthBias2EXT)(device ? gdpa(device, "vkCmdSetDepthBias2EXT") : NULL);
    if (!t->vkCmdSetDepthBias2EXT)
        t->vkCmdSetDepthBias2EXT = (PFN_vkCmdSetDepthBias2EXT)gipa(instance, "vkCmdSetDepthBias2EXT");
    t->vkCreatePrivateDataSlotEXT = (PFN_vkCreatePrivateDataSlotEXT)(device ? gdpa(device, "vkCreatePrivateDataSlotEXT") : NULL);
    if (!t->vkCreatePrivateDataSlotEXT)
        t->vkCreatePrivateDataSlotEXT = (PFN_vkCreatePrivateDataSlotEXT)gipa(instance, "vkCreatePrivateDataSlotEXT");
    t->vkDestroyPrivateDataSlotEXT = (PFN_vkDestroyPrivateDataSlotEXT)(device ? gdpa(device, "vkDestroyPrivateDataSlotEXT") : NULL);
    if (!t->vkDestroyPrivateDataSlotEXT)
        t->vkDestroyPrivateDataSlotEXT = (PFN_vkDestroyPrivateDataSlotEXT)gipa(instance, "vkDestroyPrivateDataSlotEXT");
    t->vkSetPrivateDataEXT = (PFN_vkSetPrivateDataEXT)(device ? gdpa(device, "vkSetPrivateDataEXT") : NULL);
    if (!t->vkSetPrivateDataEXT)
        t->vkSetPrivateDataEXT = (PFN_vkSetPrivateDataEXT)gipa(instance, "vkSetPrivateDataEXT");
    t->vkGetPrivateDataEXT = (PFN_vkGetPrivateDataEXT)(device ? gdpa(device, "vkGetPrivateDataEXT") : NULL);
    if (!t->vkGetPrivateDataEXT)
        t->vkGetPrivateDataEXT = (PFN_vkGetPrivateDataEXT)gipa(instance, "vkGetPrivateDataEXT");
    t->vkQueueSetPerfHintQCOM = (PFN_vkQueueSetPerfHintQCOM)(device ? gdpa(device, "vkQueueSetPerfHintQCOM") : NULL);
    if (!t->vkQueueSetPerfHintQCOM)
        t->vkQueueSetPerfHintQCOM = (PFN_vkQueueSetPerfHintQCOM)gipa(instance, "vkQueueSetPerfHintQCOM");
    t->vkSetLatencySleepModeLegacyNV = (PFN_vkSetLatencySleepModeLegacyNV)(device ? gdpa(device, "vkSetLatencySleepModeLegacyNV") : NULL);
    if (!t->vkSetLatencySleepModeLegacyNV)
        t->vkSetLatencySleepModeLegacyNV = (PFN_vkSetLatencySleepModeLegacyNV)gipa(instance, "vkSetLatencySleepModeLegacyNV");
    t->vkLatencySleepLegacyNV = (PFN_vkLatencySleepLegacyNV)(device ? gdpa(device, "vkLatencySleepLegacyNV") : NULL);
    if (!t->vkLatencySleepLegacyNV)
        t->vkLatencySleepLegacyNV = (PFN_vkLatencySleepLegacyNV)gipa(instance, "vkLatencySleepLegacyNV");
    t->vkSetLatencyMarkerLegacyNV = (PFN_vkSetLatencyMarkerLegacyNV)(device ? gdpa(device, "vkSetLatencyMarkerLegacyNV") : NULL);
    if (!t->vkSetLatencyMarkerLegacyNV)
        t->vkSetLatencyMarkerLegacyNV = (PFN_vkSetLatencyMarkerLegacyNV)gipa(instance, "vkSetLatencyMarkerLegacyNV");
    t->vkGetLatencyTimingsLegacyNV = (PFN_vkGetLatencyTimingsLegacyNV)(device ? gdpa(device, "vkGetLatencyTimingsLegacyNV") : NULL);
    if (!t->vkGetLatencyTimingsLegacyNV)
        t->vkGetLatencyTimingsLegacyNV = (PFN_vkGetLatencyTimingsLegacyNV)gipa(instance, "vkGetLatencyTimingsLegacyNV");
    t->vkQueueNotifyOutOfBandLegacyNV = (PFN_vkQueueNotifyOutOfBandLegacyNV)(device ? gdpa(device, "vkQueueNotifyOutOfBandLegacyNV") : NULL);
    if (!t->vkQueueNotifyOutOfBandLegacyNV)
        t->vkQueueNotifyOutOfBandLegacyNV = (PFN_vkQueueNotifyOutOfBandLegacyNV)gipa(instance, "vkQueueNotifyOutOfBandLegacyNV");
    t->vkGetSleepStatusLegacyNV = (PFN_vkGetSleepStatusLegacyNV)(device ? gdpa(device, "vkGetSleepStatusLegacyNV") : NULL);
    if (!t->vkGetSleepStatusLegacyNV)
        t->vkGetSleepStatusLegacyNV = (PFN_vkGetSleepStatusLegacyNV)gipa(instance, "vkGetSleepStatusLegacyNV");
    t->vkShutdownLatencyDeviceLegacyNV = (PFN_vkShutdownLatencyDeviceLegacyNV)(device ? gdpa(device, "vkShutdownLatencyDeviceLegacyNV") : NULL);
    if (!t->vkShutdownLatencyDeviceLegacyNV)
        t->vkShutdownLatencyDeviceLegacyNV = (PFN_vkShutdownLatencyDeviceLegacyNV)gipa(instance, "vkShutdownLatencyDeviceLegacyNV");
    t->vkCmdSetEvent2KHR = (PFN_vkCmdSetEvent2KHR)(device ? gdpa(device, "vkCmdSetEvent2KHR") : NULL);
    if (!t->vkCmdSetEvent2KHR)
        t->vkCmdSetEvent2KHR = (PFN_vkCmdSetEvent2KHR)gipa(instance, "vkCmdSetEvent2KHR");
    t->vkCmdResetEvent2KHR = (PFN_vkCmdResetEvent2KHR)(device ? gdpa(device, "vkCmdResetEvent2KHR") : NULL);
    if (!t->vkCmdResetEvent2KHR)
        t->vkCmdResetEvent2KHR = (PFN_vkCmdResetEvent2KHR)gipa(instance, "vkCmdResetEvent2KHR");
    t->vkCmdWaitEvents2KHR = (PFN_vkCmdWaitEvents2KHR)(device ? gdpa(device, "vkCmdWaitEvents2KHR") : NULL);
    if (!t->vkCmdWaitEvents2KHR)
        t->vkCmdWaitEvents2KHR = (PFN_vkCmdWaitEvents2KHR)gipa(instance, "vkCmdWaitEvents2KHR");
    t->vkCmdPipelineBarrier2KHR = (PFN_vkCmdPipelineBarrier2KHR)(device ? gdpa(device, "vkCmdPipelineBarrier2KHR") : NULL);
    if (!t->vkCmdPipelineBarrier2KHR)
        t->vkCmdPipelineBarrier2KHR = (PFN_vkCmdPipelineBarrier2KHR)gipa(instance, "vkCmdPipelineBarrier2KHR");
    t->vkCmdWriteTimestamp2KHR = (PFN_vkCmdWriteTimestamp2KHR)(device ? gdpa(device, "vkCmdWriteTimestamp2KHR") : NULL);
    if (!t->vkCmdWriteTimestamp2KHR)
        t->vkCmdWriteTimestamp2KHR = (PFN_vkCmdWriteTimestamp2KHR)gipa(instance, "vkCmdWriteTimestamp2KHR");
    t->vkQueueSubmit2KHR = (PFN_vkQueueSubmit2KHR)(device ? gdpa(device, "vkQueueSubmit2KHR") : NULL);
    if (!t->vkQueueSubmit2KHR)
        t->vkQueueSubmit2KHR = (PFN_vkQueueSubmit2KHR)gipa(instance, "vkQueueSubmit2KHR");
    t->vkGetDescriptorSetLayoutSizeEXT = (PFN_vkGetDescriptorSetLayoutSizeEXT)(device ? gdpa(device, "vkGetDescriptorSetLayoutSizeEXT") : NULL);
    if (!t->vkGetDescriptorSetLayoutSizeEXT)
        t->vkGetDescriptorSetLayoutSizeEXT = (PFN_vkGetDescriptorSetLayoutSizeEXT)gipa(instance, "vkGetDescriptorSetLayoutSizeEXT");
    t->vkGetDescriptorSetLayoutBindingOffsetEXT = (PFN_vkGetDescriptorSetLayoutBindingOffsetEXT)(device ? gdpa(device, "vkGetDescriptorSetLayoutBindingOffsetEXT") : NULL);
    if (!t->vkGetDescriptorSetLayoutBindingOffsetEXT)
        t->vkGetDescriptorSetLayoutBindingOffsetEXT = (PFN_vkGetDescriptorSetLayoutBindingOffsetEXT)gipa(instance, "vkGetDescriptorSetLayoutBindingOffsetEXT");
    t->vkGetDescriptorEXT = (PFN_vkGetDescriptorEXT)(device ? gdpa(device, "vkGetDescriptorEXT") : NULL);
    if (!t->vkGetDescriptorEXT)
        t->vkGetDescriptorEXT = (PFN_vkGetDescriptorEXT)gipa(instance, "vkGetDescriptorEXT");
    t->vkCmdBindDescriptorBuffersEXT = (PFN_vkCmdBindDescriptorBuffersEXT)(device ? gdpa(device, "vkCmdBindDescriptorBuffersEXT") : NULL);
    if (!t->vkCmdBindDescriptorBuffersEXT)
        t->vkCmdBindDescriptorBuffersEXT = (PFN_vkCmdBindDescriptorBuffersEXT)gipa(instance, "vkCmdBindDescriptorBuffersEXT");
    t->vkCmdSetDescriptorBufferOffsetsEXT = (PFN_vkCmdSetDescriptorBufferOffsetsEXT)(device ? gdpa(device, "vkCmdSetDescriptorBufferOffsetsEXT") : NULL);
    if (!t->vkCmdSetDescriptorBufferOffsetsEXT)
        t->vkCmdSetDescriptorBufferOffsetsEXT = (PFN_vkCmdSetDescriptorBufferOffsetsEXT)gipa(instance, "vkCmdSetDescriptorBufferOffsetsEXT");
    t->vkCmdBindDescriptorBufferEmbeddedSamplersEXT = (PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT)(device ? gdpa(device, "vkCmdBindDescriptorBufferEmbeddedSamplersEXT") : NULL);
    if (!t->vkCmdBindDescriptorBufferEmbeddedSamplersEXT)
        t->vkCmdBindDescriptorBufferEmbeddedSamplersEXT = (PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT)gipa(instance, "vkCmdBindDescriptorBufferEmbeddedSamplersEXT");
    t->vkGetBufferOpaqueCaptureDescriptorDataEXT = (PFN_vkGetBufferOpaqueCaptureDescriptorDataEXT)(device ? gdpa(device, "vkGetBufferOpaqueCaptureDescriptorDataEXT") : NULL);
    if (!t->vkGetBufferOpaqueCaptureDescriptorDataEXT)
        t->vkGetBufferOpaqueCaptureDescriptorDataEXT = (PFN_vkGetBufferOpaqueCaptureDescriptorDataEXT)gipa(instance, "vkGetBufferOpaqueCaptureDescriptorDataEXT");
    t->vkGetImageOpaqueCaptureDescriptorDataEXT = (PFN_vkGetImageOpaqueCaptureDescriptorDataEXT)(device ? gdpa(device, "vkGetImageOpaqueCaptureDescriptorDataEXT") : NULL);
    if (!t->vkGetImageOpaqueCaptureDescriptorDataEXT)
        t->vkGetImageOpaqueCaptureDescriptorDataEXT = (PFN_vkGetImageOpaqueCaptureDescriptorDataEXT)gipa(instance, "vkGetImageOpaqueCaptureDescriptorDataEXT");
    t->vkGetImageViewOpaqueCaptureDescriptorDataEXT = (PFN_vkGetImageViewOpaqueCaptureDescriptorDataEXT)(device ? gdpa(device, "vkGetImageViewOpaqueCaptureDescriptorDataEXT") : NULL);
    if (!t->vkGetImageViewOpaqueCaptureDescriptorDataEXT)
        t->vkGetImageViewOpaqueCaptureDescriptorDataEXT = (PFN_vkGetImageViewOpaqueCaptureDescriptorDataEXT)gipa(instance, "vkGetImageViewOpaqueCaptureDescriptorDataEXT");
    t->vkGetSamplerOpaqueCaptureDescriptorDataEXT = (PFN_vkGetSamplerOpaqueCaptureDescriptorDataEXT)(device ? gdpa(device, "vkGetSamplerOpaqueCaptureDescriptorDataEXT") : NULL);
    if (!t->vkGetSamplerOpaqueCaptureDescriptorDataEXT)
        t->vkGetSamplerOpaqueCaptureDescriptorDataEXT = (PFN_vkGetSamplerOpaqueCaptureDescriptorDataEXT)gipa(instance, "vkGetSamplerOpaqueCaptureDescriptorDataEXT");
    t->vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT = (PFN_vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT)(device ? gdpa(device, "vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT") : NULL);
    if (!t->vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT)
        t->vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT = (PFN_vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT)gipa(instance, "vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT");
    t->vkCmdBindIndexBuffer3KHR = (PFN_vkCmdBindIndexBuffer3KHR)(device ? gdpa(device, "vkCmdBindIndexBuffer3KHR") : NULL);
    if (!t->vkCmdBindIndexBuffer3KHR)
        t->vkCmdBindIndexBuffer3KHR = (PFN_vkCmdBindIndexBuffer3KHR)gipa(instance, "vkCmdBindIndexBuffer3KHR");
    t->vkCmdBindVertexBuffers3KHR = (PFN_vkCmdBindVertexBuffers3KHR)(device ? gdpa(device, "vkCmdBindVertexBuffers3KHR") : NULL);
    if (!t->vkCmdBindVertexBuffers3KHR)
        t->vkCmdBindVertexBuffers3KHR = (PFN_vkCmdBindVertexBuffers3KHR)gipa(instance, "vkCmdBindVertexBuffers3KHR");
    t->vkCmdDrawIndirect2KHR = (PFN_vkCmdDrawIndirect2KHR)(device ? gdpa(device, "vkCmdDrawIndirect2KHR") : NULL);
    if (!t->vkCmdDrawIndirect2KHR)
        t->vkCmdDrawIndirect2KHR = (PFN_vkCmdDrawIndirect2KHR)gipa(instance, "vkCmdDrawIndirect2KHR");
    t->vkCmdDrawIndexedIndirect2KHR = (PFN_vkCmdDrawIndexedIndirect2KHR)(device ? gdpa(device, "vkCmdDrawIndexedIndirect2KHR") : NULL);
    if (!t->vkCmdDrawIndexedIndirect2KHR)
        t->vkCmdDrawIndexedIndirect2KHR = (PFN_vkCmdDrawIndexedIndirect2KHR)gipa(instance, "vkCmdDrawIndexedIndirect2KHR");
    t->vkCmdDispatchIndirect2KHR = (PFN_vkCmdDispatchIndirect2KHR)(device ? gdpa(device, "vkCmdDispatchIndirect2KHR") : NULL);
    if (!t->vkCmdDispatchIndirect2KHR)
        t->vkCmdDispatchIndirect2KHR = (PFN_vkCmdDispatchIndirect2KHR)gipa(instance, "vkCmdDispatchIndirect2KHR");
    t->vkCmdCopyMemoryKHR = (PFN_vkCmdCopyMemoryKHR)(device ? gdpa(device, "vkCmdCopyMemoryKHR") : NULL);
    if (!t->vkCmdCopyMemoryKHR)
        t->vkCmdCopyMemoryKHR = (PFN_vkCmdCopyMemoryKHR)gipa(instance, "vkCmdCopyMemoryKHR");
    t->vkCmdCopyMemoryToImageKHR = (PFN_vkCmdCopyMemoryToImageKHR)(device ? gdpa(device, "vkCmdCopyMemoryToImageKHR") : NULL);
    if (!t->vkCmdCopyMemoryToImageKHR)
        t->vkCmdCopyMemoryToImageKHR = (PFN_vkCmdCopyMemoryToImageKHR)gipa(instance, "vkCmdCopyMemoryToImageKHR");
    t->vkCmdCopyImageToMemoryKHR = (PFN_vkCmdCopyImageToMemoryKHR)(device ? gdpa(device, "vkCmdCopyImageToMemoryKHR") : NULL);
    if (!t->vkCmdCopyImageToMemoryKHR)
        t->vkCmdCopyImageToMemoryKHR = (PFN_vkCmdCopyImageToMemoryKHR)gipa(instance, "vkCmdCopyImageToMemoryKHR");
    t->vkCmdUpdateMemoryKHR = (PFN_vkCmdUpdateMemoryKHR)(device ? gdpa(device, "vkCmdUpdateMemoryKHR") : NULL);
    if (!t->vkCmdUpdateMemoryKHR)
        t->vkCmdUpdateMemoryKHR = (PFN_vkCmdUpdateMemoryKHR)gipa(instance, "vkCmdUpdateMemoryKHR");
    t->vkCmdFillMemoryKHR = (PFN_vkCmdFillMemoryKHR)(device ? gdpa(device, "vkCmdFillMemoryKHR") : NULL);
    if (!t->vkCmdFillMemoryKHR)
        t->vkCmdFillMemoryKHR = (PFN_vkCmdFillMemoryKHR)gipa(instance, "vkCmdFillMemoryKHR");
    t->vkCmdCopyQueryPoolResultsToMemoryKHR = (PFN_vkCmdCopyQueryPoolResultsToMemoryKHR)(device ? gdpa(device, "vkCmdCopyQueryPoolResultsToMemoryKHR") : NULL);
    if (!t->vkCmdCopyQueryPoolResultsToMemoryKHR)
        t->vkCmdCopyQueryPoolResultsToMemoryKHR = (PFN_vkCmdCopyQueryPoolResultsToMemoryKHR)gipa(instance, "vkCmdCopyQueryPoolResultsToMemoryKHR");
    t->vkCmdDrawIndirectCount2KHR = (PFN_vkCmdDrawIndirectCount2KHR)(device ? gdpa(device, "vkCmdDrawIndirectCount2KHR") : NULL);
    if (!t->vkCmdDrawIndirectCount2KHR)
        t->vkCmdDrawIndirectCount2KHR = (PFN_vkCmdDrawIndirectCount2KHR)gipa(instance, "vkCmdDrawIndirectCount2KHR");
    t->vkCmdDrawIndexedIndirectCount2KHR = (PFN_vkCmdDrawIndexedIndirectCount2KHR)(device ? gdpa(device, "vkCmdDrawIndexedIndirectCount2KHR") : NULL);
    if (!t->vkCmdDrawIndexedIndirectCount2KHR)
        t->vkCmdDrawIndexedIndirectCount2KHR = (PFN_vkCmdDrawIndexedIndirectCount2KHR)gipa(instance, "vkCmdDrawIndexedIndirectCount2KHR");
    t->vkCmdBeginConditionalRendering2EXT = (PFN_vkCmdBeginConditionalRendering2EXT)(device ? gdpa(device, "vkCmdBeginConditionalRendering2EXT") : NULL);
    if (!t->vkCmdBeginConditionalRendering2EXT)
        t->vkCmdBeginConditionalRendering2EXT = (PFN_vkCmdBeginConditionalRendering2EXT)gipa(instance, "vkCmdBeginConditionalRendering2EXT");
    t->vkCmdBindTransformFeedbackBuffers2EXT = (PFN_vkCmdBindTransformFeedbackBuffers2EXT)(device ? gdpa(device, "vkCmdBindTransformFeedbackBuffers2EXT") : NULL);
    if (!t->vkCmdBindTransformFeedbackBuffers2EXT)
        t->vkCmdBindTransformFeedbackBuffers2EXT = (PFN_vkCmdBindTransformFeedbackBuffers2EXT)gipa(instance, "vkCmdBindTransformFeedbackBuffers2EXT");
    t->vkCmdBeginTransformFeedback2EXT = (PFN_vkCmdBeginTransformFeedback2EXT)(device ? gdpa(device, "vkCmdBeginTransformFeedback2EXT") : NULL);
    if (!t->vkCmdBeginTransformFeedback2EXT)
        t->vkCmdBeginTransformFeedback2EXT = (PFN_vkCmdBeginTransformFeedback2EXT)gipa(instance, "vkCmdBeginTransformFeedback2EXT");
    t->vkCmdEndTransformFeedback2EXT = (PFN_vkCmdEndTransformFeedback2EXT)(device ? gdpa(device, "vkCmdEndTransformFeedback2EXT") : NULL);
    if (!t->vkCmdEndTransformFeedback2EXT)
        t->vkCmdEndTransformFeedback2EXT = (PFN_vkCmdEndTransformFeedback2EXT)gipa(instance, "vkCmdEndTransformFeedback2EXT");
    t->vkCmdDrawIndirectByteCount2EXT = (PFN_vkCmdDrawIndirectByteCount2EXT)(device ? gdpa(device, "vkCmdDrawIndirectByteCount2EXT") : NULL);
    if (!t->vkCmdDrawIndirectByteCount2EXT)
        t->vkCmdDrawIndirectByteCount2EXT = (PFN_vkCmdDrawIndirectByteCount2EXT)gipa(instance, "vkCmdDrawIndirectByteCount2EXT");
    t->vkCmdDrawMeshTasksIndirect2EXT = (PFN_vkCmdDrawMeshTasksIndirect2EXT)(device ? gdpa(device, "vkCmdDrawMeshTasksIndirect2EXT") : NULL);
    if (!t->vkCmdDrawMeshTasksIndirect2EXT)
        t->vkCmdDrawMeshTasksIndirect2EXT = (PFN_vkCmdDrawMeshTasksIndirect2EXT)gipa(instance, "vkCmdDrawMeshTasksIndirect2EXT");
    t->vkCmdDrawMeshTasksIndirectCount2EXT = (PFN_vkCmdDrawMeshTasksIndirectCount2EXT)(device ? gdpa(device, "vkCmdDrawMeshTasksIndirectCount2EXT") : NULL);
    if (!t->vkCmdDrawMeshTasksIndirectCount2EXT)
        t->vkCmdDrawMeshTasksIndirectCount2EXT = (PFN_vkCmdDrawMeshTasksIndirectCount2EXT)gipa(instance, "vkCmdDrawMeshTasksIndirectCount2EXT");
    t->vkCmdWriteMarkerToMemoryAMD = (PFN_vkCmdWriteMarkerToMemoryAMD)(device ? gdpa(device, "vkCmdWriteMarkerToMemoryAMD") : NULL);
    if (!t->vkCmdWriteMarkerToMemoryAMD)
        t->vkCmdWriteMarkerToMemoryAMD = (PFN_vkCmdWriteMarkerToMemoryAMD)gipa(instance, "vkCmdWriteMarkerToMemoryAMD");
    t->vkCreateAccelerationStructure2KHR = (PFN_vkCreateAccelerationStructure2KHR)(device ? gdpa(device, "vkCreateAccelerationStructure2KHR") : NULL);
    if (!t->vkCreateAccelerationStructure2KHR)
        t->vkCreateAccelerationStructure2KHR = (PFN_vkCreateAccelerationStructure2KHR)gipa(instance, "vkCreateAccelerationStructure2KHR");
    t->vkCmdSetFragmentShadingRateEnumNV = (PFN_vkCmdSetFragmentShadingRateEnumNV)(device ? gdpa(device, "vkCmdSetFragmentShadingRateEnumNV") : NULL);
    if (!t->vkCmdSetFragmentShadingRateEnumNV)
        t->vkCmdSetFragmentShadingRateEnumNV = (PFN_vkCmdSetFragmentShadingRateEnumNV)gipa(instance, "vkCmdSetFragmentShadingRateEnumNV");
    t->vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)(device ? gdpa(device, "vkCmdDrawMeshTasksEXT") : NULL);
    if (!t->vkCmdDrawMeshTasksEXT)
        t->vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)gipa(instance, "vkCmdDrawMeshTasksEXT");
    t->vkCmdDrawMeshTasksIndirectEXT = (PFN_vkCmdDrawMeshTasksIndirectEXT)(device ? gdpa(device, "vkCmdDrawMeshTasksIndirectEXT") : NULL);
    if (!t->vkCmdDrawMeshTasksIndirectEXT)
        t->vkCmdDrawMeshTasksIndirectEXT = (PFN_vkCmdDrawMeshTasksIndirectEXT)gipa(instance, "vkCmdDrawMeshTasksIndirectEXT");
    t->vkCmdDrawMeshTasksIndirectCountEXT = (PFN_vkCmdDrawMeshTasksIndirectCountEXT)(device ? gdpa(device, "vkCmdDrawMeshTasksIndirectCountEXT") : NULL);
    if (!t->vkCmdDrawMeshTasksIndirectCountEXT)
        t->vkCmdDrawMeshTasksIndirectCountEXT = (PFN_vkCmdDrawMeshTasksIndirectCountEXT)gipa(instance, "vkCmdDrawMeshTasksIndirectCountEXT");
    t->vkCmdCopyBuffer2KHR = (PFN_vkCmdCopyBuffer2KHR)(device ? gdpa(device, "vkCmdCopyBuffer2KHR") : NULL);
    if (!t->vkCmdCopyBuffer2KHR)
        t->vkCmdCopyBuffer2KHR = (PFN_vkCmdCopyBuffer2KHR)gipa(instance, "vkCmdCopyBuffer2KHR");
    t->vkCmdCopyImage2KHR = (PFN_vkCmdCopyImage2KHR)(device ? gdpa(device, "vkCmdCopyImage2KHR") : NULL);
    if (!t->vkCmdCopyImage2KHR)
        t->vkCmdCopyImage2KHR = (PFN_vkCmdCopyImage2KHR)gipa(instance, "vkCmdCopyImage2KHR");
    t->vkCmdCopyBufferToImage2KHR = (PFN_vkCmdCopyBufferToImage2KHR)(device ? gdpa(device, "vkCmdCopyBufferToImage2KHR") : NULL);
    if (!t->vkCmdCopyBufferToImage2KHR)
        t->vkCmdCopyBufferToImage2KHR = (PFN_vkCmdCopyBufferToImage2KHR)gipa(instance, "vkCmdCopyBufferToImage2KHR");
    t->vkCmdCopyImageToBuffer2KHR = (PFN_vkCmdCopyImageToBuffer2KHR)(device ? gdpa(device, "vkCmdCopyImageToBuffer2KHR") : NULL);
    if (!t->vkCmdCopyImageToBuffer2KHR)
        t->vkCmdCopyImageToBuffer2KHR = (PFN_vkCmdCopyImageToBuffer2KHR)gipa(instance, "vkCmdCopyImageToBuffer2KHR");
    t->vkCmdBlitImage2KHR = (PFN_vkCmdBlitImage2KHR)(device ? gdpa(device, "vkCmdBlitImage2KHR") : NULL);
    if (!t->vkCmdBlitImage2KHR)
        t->vkCmdBlitImage2KHR = (PFN_vkCmdBlitImage2KHR)gipa(instance, "vkCmdBlitImage2KHR");
    t->vkCmdResolveImage2KHR = (PFN_vkCmdResolveImage2KHR)(device ? gdpa(device, "vkCmdResolveImage2KHR") : NULL);
    if (!t->vkCmdResolveImage2KHR)
        t->vkCmdResolveImage2KHR = (PFN_vkCmdResolveImage2KHR)gipa(instance, "vkCmdResolveImage2KHR");
    t->vkGetDeviceFaultInfoEXT = (PFN_vkGetDeviceFaultInfoEXT)(device ? gdpa(device, "vkGetDeviceFaultInfoEXT") : NULL);
    if (!t->vkGetDeviceFaultInfoEXT)
        t->vkGetDeviceFaultInfoEXT = (PFN_vkGetDeviceFaultInfoEXT)gipa(instance, "vkGetDeviceFaultInfoEXT");
    t->vkCmdSetVertexInputEXT = (PFN_vkCmdSetVertexInputEXT)(device ? gdpa(device, "vkCmdSetVertexInputEXT") : NULL);
    if (!t->vkCmdSetVertexInputEXT)
        t->vkCmdSetVertexInputEXT = (PFN_vkCmdSetVertexInputEXT)gipa(instance, "vkCmdSetVertexInputEXT");
    t->vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI = (PFN_vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI)(device ? gdpa(device, "vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI") : NULL);
    if (!t->vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI)
        t->vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI = (PFN_vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI)gipa(instance, "vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI");
    t->vkCmdSubpassShadingHUAWEI = (PFN_vkCmdSubpassShadingHUAWEI)(device ? gdpa(device, "vkCmdSubpassShadingHUAWEI") : NULL);
    if (!t->vkCmdSubpassShadingHUAWEI)
        t->vkCmdSubpassShadingHUAWEI = (PFN_vkCmdSubpassShadingHUAWEI)gipa(instance, "vkCmdSubpassShadingHUAWEI");
    t->vkCmdBindInvocationMaskHUAWEI = (PFN_vkCmdBindInvocationMaskHUAWEI)(device ? gdpa(device, "vkCmdBindInvocationMaskHUAWEI") : NULL);
    if (!t->vkCmdBindInvocationMaskHUAWEI)
        t->vkCmdBindInvocationMaskHUAWEI = (PFN_vkCmdBindInvocationMaskHUAWEI)gipa(instance, "vkCmdBindInvocationMaskHUAWEI");
    t->vkGetMemoryRemoteAddressNV = (PFN_vkGetMemoryRemoteAddressNV)(device ? gdpa(device, "vkGetMemoryRemoteAddressNV") : NULL);
    if (!t->vkGetMemoryRemoteAddressNV)
        t->vkGetMemoryRemoteAddressNV = (PFN_vkGetMemoryRemoteAddressNV)gipa(instance, "vkGetMemoryRemoteAddressNV");
    t->vkGetPipelinePropertiesEXT = (PFN_vkGetPipelinePropertiesEXT)(device ? gdpa(device, "vkGetPipelinePropertiesEXT") : NULL);
    if (!t->vkGetPipelinePropertiesEXT)
        t->vkGetPipelinePropertiesEXT = (PFN_vkGetPipelinePropertiesEXT)gipa(instance, "vkGetPipelinePropertiesEXT");
    t->vkCmdSetPatchControlPointsEXT = (PFN_vkCmdSetPatchControlPointsEXT)(device ? gdpa(device, "vkCmdSetPatchControlPointsEXT") : NULL);
    if (!t->vkCmdSetPatchControlPointsEXT)
        t->vkCmdSetPatchControlPointsEXT = (PFN_vkCmdSetPatchControlPointsEXT)gipa(instance, "vkCmdSetPatchControlPointsEXT");
    t->vkCmdSetRasterizerDiscardEnableEXT = (PFN_vkCmdSetRasterizerDiscardEnableEXT)(device ? gdpa(device, "vkCmdSetRasterizerDiscardEnableEXT") : NULL);
    if (!t->vkCmdSetRasterizerDiscardEnableEXT)
        t->vkCmdSetRasterizerDiscardEnableEXT = (PFN_vkCmdSetRasterizerDiscardEnableEXT)gipa(instance, "vkCmdSetRasterizerDiscardEnableEXT");
    t->vkCmdSetDepthBiasEnableEXT = (PFN_vkCmdSetDepthBiasEnableEXT)(device ? gdpa(device, "vkCmdSetDepthBiasEnableEXT") : NULL);
    if (!t->vkCmdSetDepthBiasEnableEXT)
        t->vkCmdSetDepthBiasEnableEXT = (PFN_vkCmdSetDepthBiasEnableEXT)gipa(instance, "vkCmdSetDepthBiasEnableEXT");
    t->vkCmdSetLogicOpEXT = (PFN_vkCmdSetLogicOpEXT)(device ? gdpa(device, "vkCmdSetLogicOpEXT") : NULL);
    if (!t->vkCmdSetLogicOpEXT)
        t->vkCmdSetLogicOpEXT = (PFN_vkCmdSetLogicOpEXT)gipa(instance, "vkCmdSetLogicOpEXT");
    t->vkCmdSetPrimitiveRestartEnableEXT = (PFN_vkCmdSetPrimitiveRestartEnableEXT)(device ? gdpa(device, "vkCmdSetPrimitiveRestartEnableEXT") : NULL);
    if (!t->vkCmdSetPrimitiveRestartEnableEXT)
        t->vkCmdSetPrimitiveRestartEnableEXT = (PFN_vkCmdSetPrimitiveRestartEnableEXT)gipa(instance, "vkCmdSetPrimitiveRestartEnableEXT");
    t->vkCmdSetColorWriteEnableEXT = (PFN_vkCmdSetColorWriteEnableEXT)(device ? gdpa(device, "vkCmdSetColorWriteEnableEXT") : NULL);
    if (!t->vkCmdSetColorWriteEnableEXT)
        t->vkCmdSetColorWriteEnableEXT = (PFN_vkCmdSetColorWriteEnableEXT)gipa(instance, "vkCmdSetColorWriteEnableEXT");
    t->vkCmdTraceRaysIndirect2KHR = (PFN_vkCmdTraceRaysIndirect2KHR)(device ? gdpa(device, "vkCmdTraceRaysIndirect2KHR") : NULL);
    if (!t->vkCmdTraceRaysIndirect2KHR)
        t->vkCmdTraceRaysIndirect2KHR = (PFN_vkCmdTraceRaysIndirect2KHR)gipa(instance, "vkCmdTraceRaysIndirect2KHR");
    t->vkCmdDrawMultiEXT = (PFN_vkCmdDrawMultiEXT)(device ? gdpa(device, "vkCmdDrawMultiEXT") : NULL);
    if (!t->vkCmdDrawMultiEXT)
        t->vkCmdDrawMultiEXT = (PFN_vkCmdDrawMultiEXT)gipa(instance, "vkCmdDrawMultiEXT");
    t->vkCmdDrawMultiIndexedEXT = (PFN_vkCmdDrawMultiIndexedEXT)(device ? gdpa(device, "vkCmdDrawMultiIndexedEXT") : NULL);
    if (!t->vkCmdDrawMultiIndexedEXT)
        t->vkCmdDrawMultiIndexedEXT = (PFN_vkCmdDrawMultiIndexedEXT)gipa(instance, "vkCmdDrawMultiIndexedEXT");
    t->vkCreateMicromapEXT = (PFN_vkCreateMicromapEXT)(device ? gdpa(device, "vkCreateMicromapEXT") : NULL);
    if (!t->vkCreateMicromapEXT)
        t->vkCreateMicromapEXT = (PFN_vkCreateMicromapEXT)gipa(instance, "vkCreateMicromapEXT");
    t->vkDestroyMicromapEXT = (PFN_vkDestroyMicromapEXT)(device ? gdpa(device, "vkDestroyMicromapEXT") : NULL);
    if (!t->vkDestroyMicromapEXT)
        t->vkDestroyMicromapEXT = (PFN_vkDestroyMicromapEXT)gipa(instance, "vkDestroyMicromapEXT");
    t->vkCmdBuildMicromapsEXT = (PFN_vkCmdBuildMicromapsEXT)(device ? gdpa(device, "vkCmdBuildMicromapsEXT") : NULL);
    if (!t->vkCmdBuildMicromapsEXT)
        t->vkCmdBuildMicromapsEXT = (PFN_vkCmdBuildMicromapsEXT)gipa(instance, "vkCmdBuildMicromapsEXT");
    t->vkBuildMicromapsEXT = (PFN_vkBuildMicromapsEXT)(device ? gdpa(device, "vkBuildMicromapsEXT") : NULL);
    if (!t->vkBuildMicromapsEXT)
        t->vkBuildMicromapsEXT = (PFN_vkBuildMicromapsEXT)gipa(instance, "vkBuildMicromapsEXT");
    t->vkCopyMicromapEXT = (PFN_vkCopyMicromapEXT)(device ? gdpa(device, "vkCopyMicromapEXT") : NULL);
    if (!t->vkCopyMicromapEXT)
        t->vkCopyMicromapEXT = (PFN_vkCopyMicromapEXT)gipa(instance, "vkCopyMicromapEXT");
    t->vkCopyMicromapToMemoryEXT = (PFN_vkCopyMicromapToMemoryEXT)(device ? gdpa(device, "vkCopyMicromapToMemoryEXT") : NULL);
    if (!t->vkCopyMicromapToMemoryEXT)
        t->vkCopyMicromapToMemoryEXT = (PFN_vkCopyMicromapToMemoryEXT)gipa(instance, "vkCopyMicromapToMemoryEXT");
    t->vkCopyMemoryToMicromapEXT = (PFN_vkCopyMemoryToMicromapEXT)(device ? gdpa(device, "vkCopyMemoryToMicromapEXT") : NULL);
    if (!t->vkCopyMemoryToMicromapEXT)
        t->vkCopyMemoryToMicromapEXT = (PFN_vkCopyMemoryToMicromapEXT)gipa(instance, "vkCopyMemoryToMicromapEXT");
    t->vkWriteMicromapsPropertiesEXT = (PFN_vkWriteMicromapsPropertiesEXT)(device ? gdpa(device, "vkWriteMicromapsPropertiesEXT") : NULL);
    if (!t->vkWriteMicromapsPropertiesEXT)
        t->vkWriteMicromapsPropertiesEXT = (PFN_vkWriteMicromapsPropertiesEXT)gipa(instance, "vkWriteMicromapsPropertiesEXT");
    t->vkCmdCopyMicromapEXT = (PFN_vkCmdCopyMicromapEXT)(device ? gdpa(device, "vkCmdCopyMicromapEXT") : NULL);
    if (!t->vkCmdCopyMicromapEXT)
        t->vkCmdCopyMicromapEXT = (PFN_vkCmdCopyMicromapEXT)gipa(instance, "vkCmdCopyMicromapEXT");
    t->vkCmdCopyMicromapToMemoryEXT = (PFN_vkCmdCopyMicromapToMemoryEXT)(device ? gdpa(device, "vkCmdCopyMicromapToMemoryEXT") : NULL);
    if (!t->vkCmdCopyMicromapToMemoryEXT)
        t->vkCmdCopyMicromapToMemoryEXT = (PFN_vkCmdCopyMicromapToMemoryEXT)gipa(instance, "vkCmdCopyMicromapToMemoryEXT");
    t->vkCmdCopyMemoryToMicromapEXT = (PFN_vkCmdCopyMemoryToMicromapEXT)(device ? gdpa(device, "vkCmdCopyMemoryToMicromapEXT") : NULL);
    if (!t->vkCmdCopyMemoryToMicromapEXT)
        t->vkCmdCopyMemoryToMicromapEXT = (PFN_vkCmdCopyMemoryToMicromapEXT)gipa(instance, "vkCmdCopyMemoryToMicromapEXT");
    t->vkCmdWriteMicromapsPropertiesEXT = (PFN_vkCmdWriteMicromapsPropertiesEXT)(device ? gdpa(device, "vkCmdWriteMicromapsPropertiesEXT") : NULL);
    if (!t->vkCmdWriteMicromapsPropertiesEXT)
        t->vkCmdWriteMicromapsPropertiesEXT = (PFN_vkCmdWriteMicromapsPropertiesEXT)gipa(instance, "vkCmdWriteMicromapsPropertiesEXT");
    t->vkGetDeviceMicromapCompatibilityEXT = (PFN_vkGetDeviceMicromapCompatibilityEXT)(device ? gdpa(device, "vkGetDeviceMicromapCompatibilityEXT") : NULL);
    if (!t->vkGetDeviceMicromapCompatibilityEXT)
        t->vkGetDeviceMicromapCompatibilityEXT = (PFN_vkGetDeviceMicromapCompatibilityEXT)gipa(instance, "vkGetDeviceMicromapCompatibilityEXT");
    t->vkGetMicromapBuildSizesEXT = (PFN_vkGetMicromapBuildSizesEXT)(device ? gdpa(device, "vkGetMicromapBuildSizesEXT") : NULL);
    if (!t->vkGetMicromapBuildSizesEXT)
        t->vkGetMicromapBuildSizesEXT = (PFN_vkGetMicromapBuildSizesEXT)gipa(instance, "vkGetMicromapBuildSizesEXT");
    t->vkCmdDrawClusterHUAWEI = (PFN_vkCmdDrawClusterHUAWEI)(device ? gdpa(device, "vkCmdDrawClusterHUAWEI") : NULL);
    if (!t->vkCmdDrawClusterHUAWEI)
        t->vkCmdDrawClusterHUAWEI = (PFN_vkCmdDrawClusterHUAWEI)gipa(instance, "vkCmdDrawClusterHUAWEI");
    t->vkCmdDrawClusterIndirectHUAWEI = (PFN_vkCmdDrawClusterIndirectHUAWEI)(device ? gdpa(device, "vkCmdDrawClusterIndirectHUAWEI") : NULL);
    if (!t->vkCmdDrawClusterIndirectHUAWEI)
        t->vkCmdDrawClusterIndirectHUAWEI = (PFN_vkCmdDrawClusterIndirectHUAWEI)gipa(instance, "vkCmdDrawClusterIndirectHUAWEI");
    t->vkSetDeviceMemoryPriorityEXT = (PFN_vkSetDeviceMemoryPriorityEXT)(device ? gdpa(device, "vkSetDeviceMemoryPriorityEXT") : NULL);
    if (!t->vkSetDeviceMemoryPriorityEXT)
        t->vkSetDeviceMemoryPriorityEXT = (PFN_vkSetDeviceMemoryPriorityEXT)gipa(instance, "vkSetDeviceMemoryPriorityEXT");
    t->vkGetDeviceBufferMemoryRequirementsKHR = (PFN_vkGetDeviceBufferMemoryRequirementsKHR)(device ? gdpa(device, "vkGetDeviceBufferMemoryRequirementsKHR") : NULL);
    if (!t->vkGetDeviceBufferMemoryRequirementsKHR)
        t->vkGetDeviceBufferMemoryRequirementsKHR = (PFN_vkGetDeviceBufferMemoryRequirementsKHR)gipa(instance, "vkGetDeviceBufferMemoryRequirementsKHR");
    t->vkGetDeviceImageMemoryRequirementsKHR = (PFN_vkGetDeviceImageMemoryRequirementsKHR)(device ? gdpa(device, "vkGetDeviceImageMemoryRequirementsKHR") : NULL);
    if (!t->vkGetDeviceImageMemoryRequirementsKHR)
        t->vkGetDeviceImageMemoryRequirementsKHR = (PFN_vkGetDeviceImageMemoryRequirementsKHR)gipa(instance, "vkGetDeviceImageMemoryRequirementsKHR");
    t->vkGetDeviceImageSparseMemoryRequirementsKHR = (PFN_vkGetDeviceImageSparseMemoryRequirementsKHR)(device ? gdpa(device, "vkGetDeviceImageSparseMemoryRequirementsKHR") : NULL);
    if (!t->vkGetDeviceImageSparseMemoryRequirementsKHR)
        t->vkGetDeviceImageSparseMemoryRequirementsKHR = (PFN_vkGetDeviceImageSparseMemoryRequirementsKHR)gipa(instance, "vkGetDeviceImageSparseMemoryRequirementsKHR");
    t->vkCmdSetDispatchParametersARM = (PFN_vkCmdSetDispatchParametersARM)(device ? gdpa(device, "vkCmdSetDispatchParametersARM") : NULL);
    if (!t->vkCmdSetDispatchParametersARM)
        t->vkCmdSetDispatchParametersARM = (PFN_vkCmdSetDispatchParametersARM)gipa(instance, "vkCmdSetDispatchParametersARM");
    t->vkGetDescriptorSetLayoutHostMappingInfoVALVE = (PFN_vkGetDescriptorSetLayoutHostMappingInfoVALVE)(device ? gdpa(device, "vkGetDescriptorSetLayoutHostMappingInfoVALVE") : NULL);
    if (!t->vkGetDescriptorSetLayoutHostMappingInfoVALVE)
        t->vkGetDescriptorSetLayoutHostMappingInfoVALVE = (PFN_vkGetDescriptorSetLayoutHostMappingInfoVALVE)gipa(instance, "vkGetDescriptorSetLayoutHostMappingInfoVALVE");
    t->vkGetDescriptorSetHostMappingVALVE = (PFN_vkGetDescriptorSetHostMappingVALVE)(device ? gdpa(device, "vkGetDescriptorSetHostMappingVALVE") : NULL);
    if (!t->vkGetDescriptorSetHostMappingVALVE)
        t->vkGetDescriptorSetHostMappingVALVE = (PFN_vkGetDescriptorSetHostMappingVALVE)gipa(instance, "vkGetDescriptorSetHostMappingVALVE");
    t->vkCmdCopyMemoryIndirectNV = (PFN_vkCmdCopyMemoryIndirectNV)(device ? gdpa(device, "vkCmdCopyMemoryIndirectNV") : NULL);
    if (!t->vkCmdCopyMemoryIndirectNV)
        t->vkCmdCopyMemoryIndirectNV = (PFN_vkCmdCopyMemoryIndirectNV)gipa(instance, "vkCmdCopyMemoryIndirectNV");
    t->vkCmdCopyMemoryToImageIndirectNV = (PFN_vkCmdCopyMemoryToImageIndirectNV)(device ? gdpa(device, "vkCmdCopyMemoryToImageIndirectNV") : NULL);
    if (!t->vkCmdCopyMemoryToImageIndirectNV)
        t->vkCmdCopyMemoryToImageIndirectNV = (PFN_vkCmdCopyMemoryToImageIndirectNV)gipa(instance, "vkCmdCopyMemoryToImageIndirectNV");
    t->vkCmdDecompressMemoryNV = (PFN_vkCmdDecompressMemoryNV)(device ? gdpa(device, "vkCmdDecompressMemoryNV") : NULL);
    if (!t->vkCmdDecompressMemoryNV)
        t->vkCmdDecompressMemoryNV = (PFN_vkCmdDecompressMemoryNV)gipa(instance, "vkCmdDecompressMemoryNV");
    t->vkCmdDecompressMemoryIndirectCountNV = (PFN_vkCmdDecompressMemoryIndirectCountNV)(device ? gdpa(device, "vkCmdDecompressMemoryIndirectCountNV") : NULL);
    if (!t->vkCmdDecompressMemoryIndirectCountNV)
        t->vkCmdDecompressMemoryIndirectCountNV = (PFN_vkCmdDecompressMemoryIndirectCountNV)gipa(instance, "vkCmdDecompressMemoryIndirectCountNV");
    t->vkGetPipelineIndirectMemoryRequirementsNV = (PFN_vkGetPipelineIndirectMemoryRequirementsNV)(device ? gdpa(device, "vkGetPipelineIndirectMemoryRequirementsNV") : NULL);
    if (!t->vkGetPipelineIndirectMemoryRequirementsNV)
        t->vkGetPipelineIndirectMemoryRequirementsNV = (PFN_vkGetPipelineIndirectMemoryRequirementsNV)gipa(instance, "vkGetPipelineIndirectMemoryRequirementsNV");
    t->vkCmdUpdatePipelineIndirectBufferNV = (PFN_vkCmdUpdatePipelineIndirectBufferNV)(device ? gdpa(device, "vkCmdUpdatePipelineIndirectBufferNV") : NULL);
    if (!t->vkCmdUpdatePipelineIndirectBufferNV)
        t->vkCmdUpdatePipelineIndirectBufferNV = (PFN_vkCmdUpdatePipelineIndirectBufferNV)gipa(instance, "vkCmdUpdatePipelineIndirectBufferNV");
    t->vkGetPipelineIndirectDeviceAddressNV = (PFN_vkGetPipelineIndirectDeviceAddressNV)(device ? gdpa(device, "vkGetPipelineIndirectDeviceAddressNV") : NULL);
    if (!t->vkGetPipelineIndirectDeviceAddressNV)
        t->vkGetPipelineIndirectDeviceAddressNV = (PFN_vkGetPipelineIndirectDeviceAddressNV)gipa(instance, "vkGetPipelineIndirectDeviceAddressNV");
    t->vkCmdSetDepthClampEnableEXT = (PFN_vkCmdSetDepthClampEnableEXT)(device ? gdpa(device, "vkCmdSetDepthClampEnableEXT") : NULL);
    if (!t->vkCmdSetDepthClampEnableEXT)
        t->vkCmdSetDepthClampEnableEXT = (PFN_vkCmdSetDepthClampEnableEXT)gipa(instance, "vkCmdSetDepthClampEnableEXT");
    t->vkCmdSetPolygonModeEXT = (PFN_vkCmdSetPolygonModeEXT)(device ? gdpa(device, "vkCmdSetPolygonModeEXT") : NULL);
    if (!t->vkCmdSetPolygonModeEXT)
        t->vkCmdSetPolygonModeEXT = (PFN_vkCmdSetPolygonModeEXT)gipa(instance, "vkCmdSetPolygonModeEXT");
    t->vkCmdSetRasterizationSamplesEXT = (PFN_vkCmdSetRasterizationSamplesEXT)(device ? gdpa(device, "vkCmdSetRasterizationSamplesEXT") : NULL);
    if (!t->vkCmdSetRasterizationSamplesEXT)
        t->vkCmdSetRasterizationSamplesEXT = (PFN_vkCmdSetRasterizationSamplesEXT)gipa(instance, "vkCmdSetRasterizationSamplesEXT");
    t->vkCmdSetSampleMaskEXT = (PFN_vkCmdSetSampleMaskEXT)(device ? gdpa(device, "vkCmdSetSampleMaskEXT") : NULL);
    if (!t->vkCmdSetSampleMaskEXT)
        t->vkCmdSetSampleMaskEXT = (PFN_vkCmdSetSampleMaskEXT)gipa(instance, "vkCmdSetSampleMaskEXT");
    t->vkCmdSetAlphaToCoverageEnableEXT = (PFN_vkCmdSetAlphaToCoverageEnableEXT)(device ? gdpa(device, "vkCmdSetAlphaToCoverageEnableEXT") : NULL);
    if (!t->vkCmdSetAlphaToCoverageEnableEXT)
        t->vkCmdSetAlphaToCoverageEnableEXT = (PFN_vkCmdSetAlphaToCoverageEnableEXT)gipa(instance, "vkCmdSetAlphaToCoverageEnableEXT");
    t->vkCmdSetAlphaToOneEnableEXT = (PFN_vkCmdSetAlphaToOneEnableEXT)(device ? gdpa(device, "vkCmdSetAlphaToOneEnableEXT") : NULL);
    if (!t->vkCmdSetAlphaToOneEnableEXT)
        t->vkCmdSetAlphaToOneEnableEXT = (PFN_vkCmdSetAlphaToOneEnableEXT)gipa(instance, "vkCmdSetAlphaToOneEnableEXT");
    t->vkCmdSetLogicOpEnableEXT = (PFN_vkCmdSetLogicOpEnableEXT)(device ? gdpa(device, "vkCmdSetLogicOpEnableEXT") : NULL);
    if (!t->vkCmdSetLogicOpEnableEXT)
        t->vkCmdSetLogicOpEnableEXT = (PFN_vkCmdSetLogicOpEnableEXT)gipa(instance, "vkCmdSetLogicOpEnableEXT");
    t->vkCmdSetColorBlendEnableEXT = (PFN_vkCmdSetColorBlendEnableEXT)(device ? gdpa(device, "vkCmdSetColorBlendEnableEXT") : NULL);
    if (!t->vkCmdSetColorBlendEnableEXT)
        t->vkCmdSetColorBlendEnableEXT = (PFN_vkCmdSetColorBlendEnableEXT)gipa(instance, "vkCmdSetColorBlendEnableEXT");
    t->vkCmdSetColorBlendEquationEXT = (PFN_vkCmdSetColorBlendEquationEXT)(device ? gdpa(device, "vkCmdSetColorBlendEquationEXT") : NULL);
    if (!t->vkCmdSetColorBlendEquationEXT)
        t->vkCmdSetColorBlendEquationEXT = (PFN_vkCmdSetColorBlendEquationEXT)gipa(instance, "vkCmdSetColorBlendEquationEXT");
    t->vkCmdSetColorWriteMaskEXT = (PFN_vkCmdSetColorWriteMaskEXT)(device ? gdpa(device, "vkCmdSetColorWriteMaskEXT") : NULL);
    if (!t->vkCmdSetColorWriteMaskEXT)
        t->vkCmdSetColorWriteMaskEXT = (PFN_vkCmdSetColorWriteMaskEXT)gipa(instance, "vkCmdSetColorWriteMaskEXT");
    t->vkCmdSetTessellationDomainOriginEXT = (PFN_vkCmdSetTessellationDomainOriginEXT)(device ? gdpa(device, "vkCmdSetTessellationDomainOriginEXT") : NULL);
    if (!t->vkCmdSetTessellationDomainOriginEXT)
        t->vkCmdSetTessellationDomainOriginEXT = (PFN_vkCmdSetTessellationDomainOriginEXT)gipa(instance, "vkCmdSetTessellationDomainOriginEXT");
    t->vkCmdSetRasterizationStreamEXT = (PFN_vkCmdSetRasterizationStreamEXT)(device ? gdpa(device, "vkCmdSetRasterizationStreamEXT") : NULL);
    if (!t->vkCmdSetRasterizationStreamEXT)
        t->vkCmdSetRasterizationStreamEXT = (PFN_vkCmdSetRasterizationStreamEXT)gipa(instance, "vkCmdSetRasterizationStreamEXT");
    t->vkCmdSetConservativeRasterizationModeEXT = (PFN_vkCmdSetConservativeRasterizationModeEXT)(device ? gdpa(device, "vkCmdSetConservativeRasterizationModeEXT") : NULL);
    if (!t->vkCmdSetConservativeRasterizationModeEXT)
        t->vkCmdSetConservativeRasterizationModeEXT = (PFN_vkCmdSetConservativeRasterizationModeEXT)gipa(instance, "vkCmdSetConservativeRasterizationModeEXT");
    t->vkCmdSetExtraPrimitiveOverestimationSizeEXT = (PFN_vkCmdSetExtraPrimitiveOverestimationSizeEXT)(device ? gdpa(device, "vkCmdSetExtraPrimitiveOverestimationSizeEXT") : NULL);
    if (!t->vkCmdSetExtraPrimitiveOverestimationSizeEXT)
        t->vkCmdSetExtraPrimitiveOverestimationSizeEXT = (PFN_vkCmdSetExtraPrimitiveOverestimationSizeEXT)gipa(instance, "vkCmdSetExtraPrimitiveOverestimationSizeEXT");
    t->vkCmdSetDepthClipEnableEXT = (PFN_vkCmdSetDepthClipEnableEXT)(device ? gdpa(device, "vkCmdSetDepthClipEnableEXT") : NULL);
    if (!t->vkCmdSetDepthClipEnableEXT)
        t->vkCmdSetDepthClipEnableEXT = (PFN_vkCmdSetDepthClipEnableEXT)gipa(instance, "vkCmdSetDepthClipEnableEXT");
    t->vkCmdSetSampleLocationsEnableEXT = (PFN_vkCmdSetSampleLocationsEnableEXT)(device ? gdpa(device, "vkCmdSetSampleLocationsEnableEXT") : NULL);
    if (!t->vkCmdSetSampleLocationsEnableEXT)
        t->vkCmdSetSampleLocationsEnableEXT = (PFN_vkCmdSetSampleLocationsEnableEXT)gipa(instance, "vkCmdSetSampleLocationsEnableEXT");
    t->vkCmdSetColorBlendAdvancedEXT = (PFN_vkCmdSetColorBlendAdvancedEXT)(device ? gdpa(device, "vkCmdSetColorBlendAdvancedEXT") : NULL);
    if (!t->vkCmdSetColorBlendAdvancedEXT)
        t->vkCmdSetColorBlendAdvancedEXT = (PFN_vkCmdSetColorBlendAdvancedEXT)gipa(instance, "vkCmdSetColorBlendAdvancedEXT");
    t->vkCmdSetProvokingVertexModeEXT = (PFN_vkCmdSetProvokingVertexModeEXT)(device ? gdpa(device, "vkCmdSetProvokingVertexModeEXT") : NULL);
    if (!t->vkCmdSetProvokingVertexModeEXT)
        t->vkCmdSetProvokingVertexModeEXT = (PFN_vkCmdSetProvokingVertexModeEXT)gipa(instance, "vkCmdSetProvokingVertexModeEXT");
    t->vkCmdSetLineRasterizationModeEXT = (PFN_vkCmdSetLineRasterizationModeEXT)(device ? gdpa(device, "vkCmdSetLineRasterizationModeEXT") : NULL);
    if (!t->vkCmdSetLineRasterizationModeEXT)
        t->vkCmdSetLineRasterizationModeEXT = (PFN_vkCmdSetLineRasterizationModeEXT)gipa(instance, "vkCmdSetLineRasterizationModeEXT");
    t->vkCmdSetLineStippleEnableEXT = (PFN_vkCmdSetLineStippleEnableEXT)(device ? gdpa(device, "vkCmdSetLineStippleEnableEXT") : NULL);
    if (!t->vkCmdSetLineStippleEnableEXT)
        t->vkCmdSetLineStippleEnableEXT = (PFN_vkCmdSetLineStippleEnableEXT)gipa(instance, "vkCmdSetLineStippleEnableEXT");
    t->vkCmdSetDepthClipNegativeOneToOneEXT = (PFN_vkCmdSetDepthClipNegativeOneToOneEXT)(device ? gdpa(device, "vkCmdSetDepthClipNegativeOneToOneEXT") : NULL);
    if (!t->vkCmdSetDepthClipNegativeOneToOneEXT)
        t->vkCmdSetDepthClipNegativeOneToOneEXT = (PFN_vkCmdSetDepthClipNegativeOneToOneEXT)gipa(instance, "vkCmdSetDepthClipNegativeOneToOneEXT");
    t->vkCmdSetViewportWScalingEnableNV = (PFN_vkCmdSetViewportWScalingEnableNV)(device ? gdpa(device, "vkCmdSetViewportWScalingEnableNV") : NULL);
    if (!t->vkCmdSetViewportWScalingEnableNV)
        t->vkCmdSetViewportWScalingEnableNV = (PFN_vkCmdSetViewportWScalingEnableNV)gipa(instance, "vkCmdSetViewportWScalingEnableNV");
    t->vkCmdSetViewportSwizzleNV = (PFN_vkCmdSetViewportSwizzleNV)(device ? gdpa(device, "vkCmdSetViewportSwizzleNV") : NULL);
    if (!t->vkCmdSetViewportSwizzleNV)
        t->vkCmdSetViewportSwizzleNV = (PFN_vkCmdSetViewportSwizzleNV)gipa(instance, "vkCmdSetViewportSwizzleNV");
    t->vkCmdSetCoverageToColorEnableNV = (PFN_vkCmdSetCoverageToColorEnableNV)(device ? gdpa(device, "vkCmdSetCoverageToColorEnableNV") : NULL);
    if (!t->vkCmdSetCoverageToColorEnableNV)
        t->vkCmdSetCoverageToColorEnableNV = (PFN_vkCmdSetCoverageToColorEnableNV)gipa(instance, "vkCmdSetCoverageToColorEnableNV");
    t->vkCmdSetCoverageToColorLocationNV = (PFN_vkCmdSetCoverageToColorLocationNV)(device ? gdpa(device, "vkCmdSetCoverageToColorLocationNV") : NULL);
    if (!t->vkCmdSetCoverageToColorLocationNV)
        t->vkCmdSetCoverageToColorLocationNV = (PFN_vkCmdSetCoverageToColorLocationNV)gipa(instance, "vkCmdSetCoverageToColorLocationNV");
    t->vkCmdSetCoverageModulationModeNV = (PFN_vkCmdSetCoverageModulationModeNV)(device ? gdpa(device, "vkCmdSetCoverageModulationModeNV") : NULL);
    if (!t->vkCmdSetCoverageModulationModeNV)
        t->vkCmdSetCoverageModulationModeNV = (PFN_vkCmdSetCoverageModulationModeNV)gipa(instance, "vkCmdSetCoverageModulationModeNV");
    t->vkCmdSetCoverageModulationTableEnableNV = (PFN_vkCmdSetCoverageModulationTableEnableNV)(device ? gdpa(device, "vkCmdSetCoverageModulationTableEnableNV") : NULL);
    if (!t->vkCmdSetCoverageModulationTableEnableNV)
        t->vkCmdSetCoverageModulationTableEnableNV = (PFN_vkCmdSetCoverageModulationTableEnableNV)gipa(instance, "vkCmdSetCoverageModulationTableEnableNV");
    t->vkCmdSetCoverageModulationTableNV = (PFN_vkCmdSetCoverageModulationTableNV)(device ? gdpa(device, "vkCmdSetCoverageModulationTableNV") : NULL);
    if (!t->vkCmdSetCoverageModulationTableNV)
        t->vkCmdSetCoverageModulationTableNV = (PFN_vkCmdSetCoverageModulationTableNV)gipa(instance, "vkCmdSetCoverageModulationTableNV");
    t->vkCmdSetShadingRateImageEnableNV = (PFN_vkCmdSetShadingRateImageEnableNV)(device ? gdpa(device, "vkCmdSetShadingRateImageEnableNV") : NULL);
    if (!t->vkCmdSetShadingRateImageEnableNV)
        t->vkCmdSetShadingRateImageEnableNV = (PFN_vkCmdSetShadingRateImageEnableNV)gipa(instance, "vkCmdSetShadingRateImageEnableNV");
    t->vkCmdSetRepresentativeFragmentTestEnableNV = (PFN_vkCmdSetRepresentativeFragmentTestEnableNV)(device ? gdpa(device, "vkCmdSetRepresentativeFragmentTestEnableNV") : NULL);
    if (!t->vkCmdSetRepresentativeFragmentTestEnableNV)
        t->vkCmdSetRepresentativeFragmentTestEnableNV = (PFN_vkCmdSetRepresentativeFragmentTestEnableNV)gipa(instance, "vkCmdSetRepresentativeFragmentTestEnableNV");
    t->vkCmdSetCoverageReductionModeNV = (PFN_vkCmdSetCoverageReductionModeNV)(device ? gdpa(device, "vkCmdSetCoverageReductionModeNV") : NULL);
    if (!t->vkCmdSetCoverageReductionModeNV)
        t->vkCmdSetCoverageReductionModeNV = (PFN_vkCmdSetCoverageReductionModeNV)gipa(instance, "vkCmdSetCoverageReductionModeNV");
    t->vkGetShaderModuleIdentifierEXT = (PFN_vkGetShaderModuleIdentifierEXT)(device ? gdpa(device, "vkGetShaderModuleIdentifierEXT") : NULL);
    if (!t->vkGetShaderModuleIdentifierEXT)
        t->vkGetShaderModuleIdentifierEXT = (PFN_vkGetShaderModuleIdentifierEXT)gipa(instance, "vkGetShaderModuleIdentifierEXT");
    t->vkGetShaderModuleCreateInfoIdentifierEXT = (PFN_vkGetShaderModuleCreateInfoIdentifierEXT)(device ? gdpa(device, "vkGetShaderModuleCreateInfoIdentifierEXT") : NULL);
    if (!t->vkGetShaderModuleCreateInfoIdentifierEXT)
        t->vkGetShaderModuleCreateInfoIdentifierEXT = (PFN_vkGetShaderModuleCreateInfoIdentifierEXT)gipa(instance, "vkGetShaderModuleCreateInfoIdentifierEXT");
    t->vkGetPhysicalDeviceOpticalFlowImageFormatsNV = (PFN_vkGetPhysicalDeviceOpticalFlowImageFormatsNV)gipa(instance, "vkGetPhysicalDeviceOpticalFlowImageFormatsNV");
    t->vkCreateOpticalFlowSessionNV = (PFN_vkCreateOpticalFlowSessionNV)(device ? gdpa(device, "vkCreateOpticalFlowSessionNV") : NULL);
    if (!t->vkCreateOpticalFlowSessionNV)
        t->vkCreateOpticalFlowSessionNV = (PFN_vkCreateOpticalFlowSessionNV)gipa(instance, "vkCreateOpticalFlowSessionNV");
    t->vkDestroyOpticalFlowSessionNV = (PFN_vkDestroyOpticalFlowSessionNV)(device ? gdpa(device, "vkDestroyOpticalFlowSessionNV") : NULL);
    if (!t->vkDestroyOpticalFlowSessionNV)
        t->vkDestroyOpticalFlowSessionNV = (PFN_vkDestroyOpticalFlowSessionNV)gipa(instance, "vkDestroyOpticalFlowSessionNV");
    t->vkBindOpticalFlowSessionImageNV = (PFN_vkBindOpticalFlowSessionImageNV)(device ? gdpa(device, "vkBindOpticalFlowSessionImageNV") : NULL);
    if (!t->vkBindOpticalFlowSessionImageNV)
        t->vkBindOpticalFlowSessionImageNV = (PFN_vkBindOpticalFlowSessionImageNV)gipa(instance, "vkBindOpticalFlowSessionImageNV");
    t->vkCmdOpticalFlowExecuteNV = (PFN_vkCmdOpticalFlowExecuteNV)(device ? gdpa(device, "vkCmdOpticalFlowExecuteNV") : NULL);
    if (!t->vkCmdOpticalFlowExecuteNV)
        t->vkCmdOpticalFlowExecuteNV = (PFN_vkCmdOpticalFlowExecuteNV)gipa(instance, "vkCmdOpticalFlowExecuteNV");
    t->vkCmdBindIndexBuffer2KHR = (PFN_vkCmdBindIndexBuffer2KHR)(device ? gdpa(device, "vkCmdBindIndexBuffer2KHR") : NULL);
    if (!t->vkCmdBindIndexBuffer2KHR)
        t->vkCmdBindIndexBuffer2KHR = (PFN_vkCmdBindIndexBuffer2KHR)gipa(instance, "vkCmdBindIndexBuffer2KHR");
    t->vkGetRenderingAreaGranularityKHR = (PFN_vkGetRenderingAreaGranularityKHR)(device ? gdpa(device, "vkGetRenderingAreaGranularityKHR") : NULL);
    if (!t->vkGetRenderingAreaGranularityKHR)
        t->vkGetRenderingAreaGranularityKHR = (PFN_vkGetRenderingAreaGranularityKHR)gipa(instance, "vkGetRenderingAreaGranularityKHR");
    t->vkGetDeviceImageSubresourceLayoutKHR = (PFN_vkGetDeviceImageSubresourceLayoutKHR)(device ? gdpa(device, "vkGetDeviceImageSubresourceLayoutKHR") : NULL);
    if (!t->vkGetDeviceImageSubresourceLayoutKHR)
        t->vkGetDeviceImageSubresourceLayoutKHR = (PFN_vkGetDeviceImageSubresourceLayoutKHR)gipa(instance, "vkGetDeviceImageSubresourceLayoutKHR");
    t->vkGetImageSubresourceLayout2KHR = (PFN_vkGetImageSubresourceLayout2KHR)(device ? gdpa(device, "vkGetImageSubresourceLayout2KHR") : NULL);
    if (!t->vkGetImageSubresourceLayout2KHR)
        t->vkGetImageSubresourceLayout2KHR = (PFN_vkGetImageSubresourceLayout2KHR)gipa(instance, "vkGetImageSubresourceLayout2KHR");
    t->vkCreateShadersEXT = (PFN_vkCreateShadersEXT)(device ? gdpa(device, "vkCreateShadersEXT") : NULL);
    if (!t->vkCreateShadersEXT)
        t->vkCreateShadersEXT = (PFN_vkCreateShadersEXT)gipa(instance, "vkCreateShadersEXT");
    t->vkDestroyShaderEXT = (PFN_vkDestroyShaderEXT)(device ? gdpa(device, "vkDestroyShaderEXT") : NULL);
    if (!t->vkDestroyShaderEXT)
        t->vkDestroyShaderEXT = (PFN_vkDestroyShaderEXT)gipa(instance, "vkDestroyShaderEXT");
    t->vkGetShaderBinaryDataEXT = (PFN_vkGetShaderBinaryDataEXT)(device ? gdpa(device, "vkGetShaderBinaryDataEXT") : NULL);
    if (!t->vkGetShaderBinaryDataEXT)
        t->vkGetShaderBinaryDataEXT = (PFN_vkGetShaderBinaryDataEXT)gipa(instance, "vkGetShaderBinaryDataEXT");
    t->vkCmdBindShadersEXT = (PFN_vkCmdBindShadersEXT)(device ? gdpa(device, "vkCmdBindShadersEXT") : NULL);
    if (!t->vkCmdBindShadersEXT)
        t->vkCmdBindShadersEXT = (PFN_vkCmdBindShadersEXT)gipa(instance, "vkCmdBindShadersEXT");
    t->vkCmdSetDepthClampRangeEXT = (PFN_vkCmdSetDepthClampRangeEXT)(device ? gdpa(device, "vkCmdSetDepthClampRangeEXT") : NULL);
    if (!t->vkCmdSetDepthClampRangeEXT)
        t->vkCmdSetDepthClampRangeEXT = (PFN_vkCmdSetDepthClampRangeEXT)gipa(instance, "vkCmdSetDepthClampRangeEXT");
    t->vkGetPhysicalDeviceCooperativeVectorPropertiesNV = (PFN_vkGetPhysicalDeviceCooperativeVectorPropertiesNV)gipa(instance, "vkGetPhysicalDeviceCooperativeVectorPropertiesNV");
    t->vkConvertCooperativeVectorMatrixNV = (PFN_vkConvertCooperativeVectorMatrixNV)(device ? gdpa(device, "vkConvertCooperativeVectorMatrixNV") : NULL);
    if (!t->vkConvertCooperativeVectorMatrixNV)
        t->vkConvertCooperativeVectorMatrixNV = (PFN_vkConvertCooperativeVectorMatrixNV)gipa(instance, "vkConvertCooperativeVectorMatrixNV");
    t->vkCmdConvertCooperativeVectorMatrixNV = (PFN_vkCmdConvertCooperativeVectorMatrixNV)(device ? gdpa(device, "vkCmdConvertCooperativeVectorMatrixNV") : NULL);
    if (!t->vkCmdConvertCooperativeVectorMatrixNV)
        t->vkCmdConvertCooperativeVectorMatrixNV = (PFN_vkCmdConvertCooperativeVectorMatrixNV)gipa(instance, "vkCmdConvertCooperativeVectorMatrixNV");
    t->vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR = (PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR)gipa(instance, "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR");
    t->vkCmdSetAttachmentFeedbackLoopEnableEXT = (PFN_vkCmdSetAttachmentFeedbackLoopEnableEXT)(device ? gdpa(device, "vkCmdSetAttachmentFeedbackLoopEnableEXT") : NULL);
    if (!t->vkCmdSetAttachmentFeedbackLoopEnableEXT)
        t->vkCmdSetAttachmentFeedbackLoopEnableEXT = (PFN_vkCmdSetAttachmentFeedbackLoopEnableEXT)gipa(instance, "vkCmdSetAttachmentFeedbackLoopEnableEXT");
    t->vkCmdSetLineStippleKHR = (PFN_vkCmdSetLineStippleKHR)(device ? gdpa(device, "vkCmdSetLineStippleKHR") : NULL);
    if (!t->vkCmdSetLineStippleKHR)
        t->vkCmdSetLineStippleKHR = (PFN_vkCmdSetLineStippleKHR)gipa(instance, "vkCmdSetLineStippleKHR");
    t->vkGetPhysicalDeviceCalibrateableTimeDomainsKHR = (PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR)gipa(instance, "vkGetPhysicalDeviceCalibrateableTimeDomainsKHR");
    t->vkGetCalibratedTimestampsKHR = (PFN_vkGetCalibratedTimestampsKHR)(device ? gdpa(device, "vkGetCalibratedTimestampsKHR") : NULL);
    if (!t->vkGetCalibratedTimestampsKHR)
        t->vkGetCalibratedTimestampsKHR = (PFN_vkGetCalibratedTimestampsKHR)gipa(instance, "vkGetCalibratedTimestampsKHR");
    t->vkCmdBindDescriptorSets2KHR = (PFN_vkCmdBindDescriptorSets2KHR)(device ? gdpa(device, "vkCmdBindDescriptorSets2KHR") : NULL);
    if (!t->vkCmdBindDescriptorSets2KHR)
        t->vkCmdBindDescriptorSets2KHR = (PFN_vkCmdBindDescriptorSets2KHR)gipa(instance, "vkCmdBindDescriptorSets2KHR");
    t->vkCmdPushConstants2KHR = (PFN_vkCmdPushConstants2KHR)(device ? gdpa(device, "vkCmdPushConstants2KHR") : NULL);
    if (!t->vkCmdPushConstants2KHR)
        t->vkCmdPushConstants2KHR = (PFN_vkCmdPushConstants2KHR)gipa(instance, "vkCmdPushConstants2KHR");
    t->vkCmdPushDescriptorSet2KHR = (PFN_vkCmdPushDescriptorSet2KHR)(device ? gdpa(device, "vkCmdPushDescriptorSet2KHR") : NULL);
    if (!t->vkCmdPushDescriptorSet2KHR)
        t->vkCmdPushDescriptorSet2KHR = (PFN_vkCmdPushDescriptorSet2KHR)gipa(instance, "vkCmdPushDescriptorSet2KHR");
    t->vkCmdPushDescriptorSetWithTemplate2KHR = (PFN_vkCmdPushDescriptorSetWithTemplate2KHR)(device ? gdpa(device, "vkCmdPushDescriptorSetWithTemplate2KHR") : NULL);
    if (!t->vkCmdPushDescriptorSetWithTemplate2KHR)
        t->vkCmdPushDescriptorSetWithTemplate2KHR = (PFN_vkCmdPushDescriptorSetWithTemplate2KHR)gipa(instance, "vkCmdPushDescriptorSetWithTemplate2KHR");
    t->vkCmdSetDescriptorBufferOffsets2EXT = (PFN_vkCmdSetDescriptorBufferOffsets2EXT)(device ? gdpa(device, "vkCmdSetDescriptorBufferOffsets2EXT") : NULL);
    if (!t->vkCmdSetDescriptorBufferOffsets2EXT)
        t->vkCmdSetDescriptorBufferOffsets2EXT = (PFN_vkCmdSetDescriptorBufferOffsets2EXT)gipa(instance, "vkCmdSetDescriptorBufferOffsets2EXT");
    t->vkCmdBindDescriptorBufferEmbeddedSamplers2EXT = (PFN_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT)(device ? gdpa(device, "vkCmdBindDescriptorBufferEmbeddedSamplers2EXT") : NULL);
    if (!t->vkCmdBindDescriptorBufferEmbeddedSamplers2EXT)
        t->vkCmdBindDescriptorBufferEmbeddedSamplers2EXT = (PFN_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT)gipa(instance, "vkCmdBindDescriptorBufferEmbeddedSamplers2EXT");
    t->vkCmdCopyMemoryIndirectKHR = (PFN_vkCmdCopyMemoryIndirectKHR)(device ? gdpa(device, "vkCmdCopyMemoryIndirectKHR") : NULL);
    if (!t->vkCmdCopyMemoryIndirectKHR)
        t->vkCmdCopyMemoryIndirectKHR = (PFN_vkCmdCopyMemoryIndirectKHR)gipa(instance, "vkCmdCopyMemoryIndirectKHR");
    t->vkCmdCopyMemoryToImageIndirectKHR = (PFN_vkCmdCopyMemoryToImageIndirectKHR)(device ? gdpa(device, "vkCmdCopyMemoryToImageIndirectKHR") : NULL);
    if (!t->vkCmdCopyMemoryToImageIndirectKHR)
        t->vkCmdCopyMemoryToImageIndirectKHR = (PFN_vkCmdCopyMemoryToImageIndirectKHR)gipa(instance, "vkCmdCopyMemoryToImageIndirectKHR");
    t->vkCmdDecompressMemoryEXT = (PFN_vkCmdDecompressMemoryEXT)(device ? gdpa(device, "vkCmdDecompressMemoryEXT") : NULL);
    if (!t->vkCmdDecompressMemoryEXT)
        t->vkCmdDecompressMemoryEXT = (PFN_vkCmdDecompressMemoryEXT)gipa(instance, "vkCmdDecompressMemoryEXT");
    t->vkCmdDecompressMemoryIndirectCountEXT = (PFN_vkCmdDecompressMemoryIndirectCountEXT)(device ? gdpa(device, "vkCmdDecompressMemoryIndirectCountEXT") : NULL);
    if (!t->vkCmdDecompressMemoryIndirectCountEXT)
        t->vkCmdDecompressMemoryIndirectCountEXT = (PFN_vkCmdDecompressMemoryIndirectCountEXT)gipa(instance, "vkCmdDecompressMemoryIndirectCountEXT");
    t->vkGetClusterAccelerationStructureBuildSizesNV = (PFN_vkGetClusterAccelerationStructureBuildSizesNV)(device ? gdpa(device, "vkGetClusterAccelerationStructureBuildSizesNV") : NULL);
    if (!t->vkGetClusterAccelerationStructureBuildSizesNV)
        t->vkGetClusterAccelerationStructureBuildSizesNV = (PFN_vkGetClusterAccelerationStructureBuildSizesNV)gipa(instance, "vkGetClusterAccelerationStructureBuildSizesNV");
    t->vkCmdBuildClusterAccelerationStructureIndirectNV = (PFN_vkCmdBuildClusterAccelerationStructureIndirectNV)(device ? gdpa(device, "vkCmdBuildClusterAccelerationStructureIndirectNV") : NULL);
    if (!t->vkCmdBuildClusterAccelerationStructureIndirectNV)
        t->vkCmdBuildClusterAccelerationStructureIndirectNV = (PFN_vkCmdBuildClusterAccelerationStructureIndirectNV)gipa(instance, "vkCmdBuildClusterAccelerationStructureIndirectNV");
    t->vkGetPartitionedAccelerationStructuresBuildSizesNV = (PFN_vkGetPartitionedAccelerationStructuresBuildSizesNV)(device ? gdpa(device, "vkGetPartitionedAccelerationStructuresBuildSizesNV") : NULL);
    if (!t->vkGetPartitionedAccelerationStructuresBuildSizesNV)
        t->vkGetPartitionedAccelerationStructuresBuildSizesNV = (PFN_vkGetPartitionedAccelerationStructuresBuildSizesNV)gipa(instance, "vkGetPartitionedAccelerationStructuresBuildSizesNV");
    t->vkCmdBuildPartitionedAccelerationStructuresNV = (PFN_vkCmdBuildPartitionedAccelerationStructuresNV)(device ? gdpa(device, "vkCmdBuildPartitionedAccelerationStructuresNV") : NULL);
    if (!t->vkCmdBuildPartitionedAccelerationStructuresNV)
        t->vkCmdBuildPartitionedAccelerationStructuresNV = (PFN_vkCmdBuildPartitionedAccelerationStructuresNV)gipa(instance, "vkCmdBuildPartitionedAccelerationStructuresNV");
    t->vkGetGeneratedCommandsMemoryRequirementsEXT = (PFN_vkGetGeneratedCommandsMemoryRequirementsEXT)(device ? gdpa(device, "vkGetGeneratedCommandsMemoryRequirementsEXT") : NULL);
    if (!t->vkGetGeneratedCommandsMemoryRequirementsEXT)
        t->vkGetGeneratedCommandsMemoryRequirementsEXT = (PFN_vkGetGeneratedCommandsMemoryRequirementsEXT)gipa(instance, "vkGetGeneratedCommandsMemoryRequirementsEXT");
    t->vkCmdPreprocessGeneratedCommandsEXT = (PFN_vkCmdPreprocessGeneratedCommandsEXT)(device ? gdpa(device, "vkCmdPreprocessGeneratedCommandsEXT") : NULL);
    if (!t->vkCmdPreprocessGeneratedCommandsEXT)
        t->vkCmdPreprocessGeneratedCommandsEXT = (PFN_vkCmdPreprocessGeneratedCommandsEXT)gipa(instance, "vkCmdPreprocessGeneratedCommandsEXT");
    t->vkCmdExecuteGeneratedCommandsEXT = (PFN_vkCmdExecuteGeneratedCommandsEXT)(device ? gdpa(device, "vkCmdExecuteGeneratedCommandsEXT") : NULL);
    if (!t->vkCmdExecuteGeneratedCommandsEXT)
        t->vkCmdExecuteGeneratedCommandsEXT = (PFN_vkCmdExecuteGeneratedCommandsEXT)gipa(instance, "vkCmdExecuteGeneratedCommandsEXT");
    t->vkCreateIndirectCommandsLayoutEXT = (PFN_vkCreateIndirectCommandsLayoutEXT)(device ? gdpa(device, "vkCreateIndirectCommandsLayoutEXT") : NULL);
    if (!t->vkCreateIndirectCommandsLayoutEXT)
        t->vkCreateIndirectCommandsLayoutEXT = (PFN_vkCreateIndirectCommandsLayoutEXT)gipa(instance, "vkCreateIndirectCommandsLayoutEXT");
    t->vkDestroyIndirectCommandsLayoutEXT = (PFN_vkDestroyIndirectCommandsLayoutEXT)(device ? gdpa(device, "vkDestroyIndirectCommandsLayoutEXT") : NULL);
    if (!t->vkDestroyIndirectCommandsLayoutEXT)
        t->vkDestroyIndirectCommandsLayoutEXT = (PFN_vkDestroyIndirectCommandsLayoutEXT)gipa(instance, "vkDestroyIndirectCommandsLayoutEXT");
    t->vkCreateIndirectExecutionSetEXT = (PFN_vkCreateIndirectExecutionSetEXT)(device ? gdpa(device, "vkCreateIndirectExecutionSetEXT") : NULL);
    if (!t->vkCreateIndirectExecutionSetEXT)
        t->vkCreateIndirectExecutionSetEXT = (PFN_vkCreateIndirectExecutionSetEXT)gipa(instance, "vkCreateIndirectExecutionSetEXT");
    t->vkDestroyIndirectExecutionSetEXT = (PFN_vkDestroyIndirectExecutionSetEXT)(device ? gdpa(device, "vkDestroyIndirectExecutionSetEXT") : NULL);
    if (!t->vkDestroyIndirectExecutionSetEXT)
        t->vkDestroyIndirectExecutionSetEXT = (PFN_vkDestroyIndirectExecutionSetEXT)gipa(instance, "vkDestroyIndirectExecutionSetEXT");
    t->vkUpdateIndirectExecutionSetPipelineEXT = (PFN_vkUpdateIndirectExecutionSetPipelineEXT)(device ? gdpa(device, "vkUpdateIndirectExecutionSetPipelineEXT") : NULL);
    if (!t->vkUpdateIndirectExecutionSetPipelineEXT)
        t->vkUpdateIndirectExecutionSetPipelineEXT = (PFN_vkUpdateIndirectExecutionSetPipelineEXT)gipa(instance, "vkUpdateIndirectExecutionSetPipelineEXT");
    t->vkUpdateIndirectExecutionSetShaderEXT = (PFN_vkUpdateIndirectExecutionSetShaderEXT)(device ? gdpa(device, "vkUpdateIndirectExecutionSetShaderEXT") : NULL);
    if (!t->vkUpdateIndirectExecutionSetShaderEXT)
        t->vkUpdateIndirectExecutionSetShaderEXT = (PFN_vkUpdateIndirectExecutionSetShaderEXT)gipa(instance, "vkUpdateIndirectExecutionSetShaderEXT");
    t->vkGetDeviceFaultReportsKHR = (PFN_vkGetDeviceFaultReportsKHR)(device ? gdpa(device, "vkGetDeviceFaultReportsKHR") : NULL);
    if (!t->vkGetDeviceFaultReportsKHR)
        t->vkGetDeviceFaultReportsKHR = (PFN_vkGetDeviceFaultReportsKHR)gipa(instance, "vkGetDeviceFaultReportsKHR");
    t->vkGetDeviceFaultDebugInfoKHR = (PFN_vkGetDeviceFaultDebugInfoKHR)(device ? gdpa(device, "vkGetDeviceFaultDebugInfoKHR") : NULL);
    if (!t->vkGetDeviceFaultDebugInfoKHR)
        t->vkGetDeviceFaultDebugInfoKHR = (PFN_vkGetDeviceFaultDebugInfoKHR)gipa(instance, "vkGetDeviceFaultDebugInfoKHR");
    t->vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV = (PFN_vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV)gipa(instance, "vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV");
    t->vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM = (PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM)gipa(instance, "vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM");
    t->vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM = (PFN_vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM)gipa(instance, "vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM");
    t->vkCreateShaderInstrumentationARM = (PFN_vkCreateShaderInstrumentationARM)(device ? gdpa(device, "vkCreateShaderInstrumentationARM") : NULL);
    if (!t->vkCreateShaderInstrumentationARM)
        t->vkCreateShaderInstrumentationARM = (PFN_vkCreateShaderInstrumentationARM)gipa(instance, "vkCreateShaderInstrumentationARM");
    t->vkDestroyShaderInstrumentationARM = (PFN_vkDestroyShaderInstrumentationARM)(device ? gdpa(device, "vkDestroyShaderInstrumentationARM") : NULL);
    if (!t->vkDestroyShaderInstrumentationARM)
        t->vkDestroyShaderInstrumentationARM = (PFN_vkDestroyShaderInstrumentationARM)gipa(instance, "vkDestroyShaderInstrumentationARM");
    t->vkCmdBeginShaderInstrumentationARM = (PFN_vkCmdBeginShaderInstrumentationARM)(device ? gdpa(device, "vkCmdBeginShaderInstrumentationARM") : NULL);
    if (!t->vkCmdBeginShaderInstrumentationARM)
        t->vkCmdBeginShaderInstrumentationARM = (PFN_vkCmdBeginShaderInstrumentationARM)gipa(instance, "vkCmdBeginShaderInstrumentationARM");
    t->vkCmdEndShaderInstrumentationARM = (PFN_vkCmdEndShaderInstrumentationARM)(device ? gdpa(device, "vkCmdEndShaderInstrumentationARM") : NULL);
    if (!t->vkCmdEndShaderInstrumentationARM)
        t->vkCmdEndShaderInstrumentationARM = (PFN_vkCmdEndShaderInstrumentationARM)gipa(instance, "vkCmdEndShaderInstrumentationARM");
    t->vkGetShaderInstrumentationValuesARM = (PFN_vkGetShaderInstrumentationValuesARM)(device ? gdpa(device, "vkGetShaderInstrumentationValuesARM") : NULL);
    if (!t->vkGetShaderInstrumentationValuesARM)
        t->vkGetShaderInstrumentationValuesARM = (PFN_vkGetShaderInstrumentationValuesARM)gipa(instance, "vkGetShaderInstrumentationValuesARM");
    t->vkClearShaderInstrumentationMetricsARM = (PFN_vkClearShaderInstrumentationMetricsARM)(device ? gdpa(device, "vkClearShaderInstrumentationMetricsARM") : NULL);
    if (!t->vkClearShaderInstrumentationMetricsARM)
        t->vkClearShaderInstrumentationMetricsARM = (PFN_vkClearShaderInstrumentationMetricsARM)gipa(instance, "vkClearShaderInstrumentationMetricsARM");
    t->vkCmdEndRendering2EXT = (PFN_vkCmdEndRendering2EXT)(device ? gdpa(device, "vkCmdEndRendering2EXT") : NULL);
    if (!t->vkCmdEndRendering2EXT)
        t->vkCmdEndRendering2EXT = (PFN_vkCmdEndRendering2EXT)gipa(instance, "vkCmdEndRendering2EXT");
    t->vkCmdBeginCustomResolveEXT = (PFN_vkCmdBeginCustomResolveEXT)(device ? gdpa(device, "vkCmdBeginCustomResolveEXT") : NULL);
    if (!t->vkCmdBeginCustomResolveEXT)
        t->vkCmdBeginCustomResolveEXT = (PFN_vkCmdBeginCustomResolveEXT)gipa(instance, "vkCmdBeginCustomResolveEXT");
    t->vkCmdEndRendering2KHR = (PFN_vkCmdEndRendering2KHR)(device ? gdpa(device, "vkCmdEndRendering2KHR") : NULL);
    if (!t->vkCmdEndRendering2KHR)
        t->vkCmdEndRendering2KHR = (PFN_vkCmdEndRendering2KHR)gipa(instance, "vkCmdEndRendering2KHR");
    t->vkCmdSetComputeOccupancyPriorityNV = (PFN_vkCmdSetComputeOccupancyPriorityNV)(device ? gdpa(device, "vkCmdSetComputeOccupancyPriorityNV") : NULL);
    if (!t->vkCmdSetComputeOccupancyPriorityNV)
        t->vkCmdSetComputeOccupancyPriorityNV = (PFN_vkCmdSetComputeOccupancyPriorityNV)gipa(instance, "vkCmdSetComputeOccupancyPriorityNV");
    t->vkGetPhysicalDeviceCooperativeMatrixProperties2EXT = (PFN_vkGetPhysicalDeviceCooperativeMatrixProperties2EXT)gipa(instance, "vkGetPhysicalDeviceCooperativeMatrixProperties2EXT");
    t->vkCmdSetPrimitiveRestartIndexEXT = (PFN_vkCmdSetPrimitiveRestartIndexEXT)(device ? gdpa(device, "vkCmdSetPrimitiveRestartIndexEXT") : NULL);
    if (!t->vkCmdSetPrimitiveRestartIndexEXT)
        t->vkCmdSetPrimitiveRestartIndexEXT = (PFN_vkCmdSetPrimitiveRestartIndexEXT)gipa(instance, "vkCmdSetPrimitiveRestartIndexEXT");
}

#endif
