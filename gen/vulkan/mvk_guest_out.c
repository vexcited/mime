#include "mvk_guest.h"


void mvk_out_VkPhysicalDeviceFeatures(MvkDecoder *d, VkPhysicalDeviceFeatures *s)
{
    s->robustBufferAccess = (VkBool32)mvk_get_u32(d);
    s->fullDrawIndexUint32 = (VkBool32)mvk_get_u32(d);
    s->imageCubeArray = (VkBool32)mvk_get_u32(d);
    s->independentBlend = (VkBool32)mvk_get_u32(d);
    s->geometryShader = (VkBool32)mvk_get_u32(d);
    s->tessellationShader = (VkBool32)mvk_get_u32(d);
    s->sampleRateShading = (VkBool32)mvk_get_u32(d);
    s->dualSrcBlend = (VkBool32)mvk_get_u32(d);
    s->logicOp = (VkBool32)mvk_get_u32(d);
    s->multiDrawIndirect = (VkBool32)mvk_get_u32(d);
    s->drawIndirectFirstInstance = (VkBool32)mvk_get_u32(d);
    s->depthClamp = (VkBool32)mvk_get_u32(d);
    s->depthBiasClamp = (VkBool32)mvk_get_u32(d);
    s->fillModeNonSolid = (VkBool32)mvk_get_u32(d);
    s->depthBounds = (VkBool32)mvk_get_u32(d);
    s->wideLines = (VkBool32)mvk_get_u32(d);
    s->largePoints = (VkBool32)mvk_get_u32(d);
    s->alphaToOne = (VkBool32)mvk_get_u32(d);
    s->multiViewport = (VkBool32)mvk_get_u32(d);
    s->samplerAnisotropy = (VkBool32)mvk_get_u32(d);
    s->textureCompressionETC2 = (VkBool32)mvk_get_u32(d);
    s->textureCompressionASTC_LDR = (VkBool32)mvk_get_u32(d);
    s->textureCompressionBC = (VkBool32)mvk_get_u32(d);
    s->occlusionQueryPrecise = (VkBool32)mvk_get_u32(d);
    s->pipelineStatisticsQuery = (VkBool32)mvk_get_u32(d);
    s->vertexPipelineStoresAndAtomics = (VkBool32)mvk_get_u32(d);
    s->fragmentStoresAndAtomics = (VkBool32)mvk_get_u32(d);
    s->shaderTessellationAndGeometryPointSize = (VkBool32)mvk_get_u32(d);
    s->shaderImageGatherExtended = (VkBool32)mvk_get_u32(d);
    s->shaderStorageImageExtendedFormats = (VkBool32)mvk_get_u32(d);
    s->shaderStorageImageMultisample = (VkBool32)mvk_get_u32(d);
    s->shaderStorageImageReadWithoutFormat = (VkBool32)mvk_get_u32(d);
    s->shaderStorageImageWriteWithoutFormat = (VkBool32)mvk_get_u32(d);
    s->shaderUniformBufferArrayDynamicIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderSampledImageArrayDynamicIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderStorageBufferArrayDynamicIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderStorageImageArrayDynamicIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderClipDistance = (VkBool32)mvk_get_u32(d);
    s->shaderCullDistance = (VkBool32)mvk_get_u32(d);
    s->shaderFloat64 = (VkBool32)mvk_get_u32(d);
    s->shaderInt64 = (VkBool32)mvk_get_u32(d);
    s->shaderInt16 = (VkBool32)mvk_get_u32(d);
    s->shaderResourceResidency = (VkBool32)mvk_get_u32(d);
    s->shaderResourceMinLod = (VkBool32)mvk_get_u32(d);
    s->sparseBinding = (VkBool32)mvk_get_u32(d);
    s->sparseResidencyBuffer = (VkBool32)mvk_get_u32(d);
    s->sparseResidencyImage2D = (VkBool32)mvk_get_u32(d);
    s->sparseResidencyImage3D = (VkBool32)mvk_get_u32(d);
    s->sparseResidency2Samples = (VkBool32)mvk_get_u32(d);
    s->sparseResidency4Samples = (VkBool32)mvk_get_u32(d);
    s->sparseResidency8Samples = (VkBool32)mvk_get_u32(d);
    s->sparseResidency16Samples = (VkBool32)mvk_get_u32(d);
    s->sparseResidencyAliased = (VkBool32)mvk_get_u32(d);
    s->variableMultisampleRate = (VkBool32)mvk_get_u32(d);
    s->inheritedQueries = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkFormatProperties(MvkDecoder *d, VkFormatProperties *s)
{
    s->linearTilingFeatures = (VkFormatFeatureFlags)mvk_get_u32(d);
    s->optimalTilingFeatures = (VkFormatFeatureFlags)mvk_get_u32(d);
    s->bufferFeatures = (VkFormatFeatureFlags)mvk_get_u32(d);
}

void mvk_out_VkImageFormatProperties(MvkDecoder *d, VkImageFormatProperties *s)
{
    mvk_out_VkExtent3D(d, &s->maxExtent);
    s->maxMipLevels = (uint32_t)mvk_get_u32(d);
    s->maxArrayLayers = (uint32_t)mvk_get_u32(d);
    s->sampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
    s->maxResourceSize = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkExtent3D(MvkDecoder *d, VkExtent3D *s)
{
    s->width = (uint32_t)mvk_get_u32(d);
    s->height = (uint32_t)mvk_get_u32(d);
    s->depth = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceProperties(MvkDecoder *d, VkPhysicalDeviceProperties *s)
{
    s->apiVersion = (uint32_t)mvk_get_u32(d);
    s->driverVersion = (uint32_t)mvk_get_u32(d);
    s->vendorID = (uint32_t)mvk_get_u32(d);
    s->deviceID = (uint32_t)mvk_get_u32(d);
    s->deviceType = (VkPhysicalDeviceType)mvk_get_u32(d);
    mvk_get_bytes(d, s->deviceName, sizeof(s->deviceName));
    mvk_get_bytes(d, s->pipelineCacheUUID, sizeof(s->pipelineCacheUUID));
    mvk_out_VkPhysicalDeviceLimits(d, &s->limits);
    mvk_out_VkPhysicalDeviceSparseProperties(d, &s->sparseProperties);
}

void mvk_out_VkPhysicalDeviceLimits(MvkDecoder *d, VkPhysicalDeviceLimits *s)
{
    s->maxImageDimension1D = (uint32_t)mvk_get_u32(d);
    s->maxImageDimension2D = (uint32_t)mvk_get_u32(d);
    s->maxImageDimension3D = (uint32_t)mvk_get_u32(d);
    s->maxImageDimensionCube = (uint32_t)mvk_get_u32(d);
    s->maxImageArrayLayers = (uint32_t)mvk_get_u32(d);
    s->maxTexelBufferElements = (uint32_t)mvk_get_u32(d);
    s->maxUniformBufferRange = (uint32_t)mvk_get_u32(d);
    s->maxStorageBufferRange = (uint32_t)mvk_get_u32(d);
    s->maxPushConstantsSize = (uint32_t)mvk_get_u32(d);
    s->maxMemoryAllocationCount = (uint32_t)mvk_get_u32(d);
    s->maxSamplerAllocationCount = (uint32_t)mvk_get_u32(d);
    s->bufferImageGranularity = (VkDeviceSize)mvk_get_u64(d);
    s->sparseAddressSpaceSize = (VkDeviceSize)mvk_get_u64(d);
    s->maxBoundDescriptorSets = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorSamplers = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUniformBuffers = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorStorageBuffers = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorSampledImages = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorStorageImages = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorInputAttachments = (uint32_t)mvk_get_u32(d);
    s->maxPerStageResources = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetSamplers = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUniformBuffers = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUniformBuffersDynamic = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetStorageBuffers = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetStorageBuffersDynamic = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetSampledImages = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetStorageImages = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetInputAttachments = (uint32_t)mvk_get_u32(d);
    s->maxVertexInputAttributes = (uint32_t)mvk_get_u32(d);
    s->maxVertexInputBindings = (uint32_t)mvk_get_u32(d);
    s->maxVertexInputAttributeOffset = (uint32_t)mvk_get_u32(d);
    s->maxVertexInputBindingStride = (uint32_t)mvk_get_u32(d);
    s->maxVertexOutputComponents = (uint32_t)mvk_get_u32(d);
    s->maxTessellationGenerationLevel = (uint32_t)mvk_get_u32(d);
    s->maxTessellationPatchSize = (uint32_t)mvk_get_u32(d);
    s->maxTessellationControlPerVertexInputComponents = (uint32_t)mvk_get_u32(d);
    s->maxTessellationControlPerVertexOutputComponents = (uint32_t)mvk_get_u32(d);
    s->maxTessellationControlPerPatchOutputComponents = (uint32_t)mvk_get_u32(d);
    s->maxTessellationControlTotalOutputComponents = (uint32_t)mvk_get_u32(d);
    s->maxTessellationEvaluationInputComponents = (uint32_t)mvk_get_u32(d);
    s->maxTessellationEvaluationOutputComponents = (uint32_t)mvk_get_u32(d);
    s->maxGeometryShaderInvocations = (uint32_t)mvk_get_u32(d);
    s->maxGeometryInputComponents = (uint32_t)mvk_get_u32(d);
    s->maxGeometryOutputComponents = (uint32_t)mvk_get_u32(d);
    s->maxGeometryOutputVertices = (uint32_t)mvk_get_u32(d);
    s->maxGeometryTotalOutputComponents = (uint32_t)mvk_get_u32(d);
    s->maxFragmentInputComponents = (uint32_t)mvk_get_u32(d);
    s->maxFragmentOutputAttachments = (uint32_t)mvk_get_u32(d);
    s->maxFragmentDualSrcAttachments = (uint32_t)mvk_get_u32(d);
    s->maxFragmentCombinedOutputResources = (uint32_t)mvk_get_u32(d);
    s->maxComputeSharedMemorySize = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->maxComputeWorkGroupCount, sizeof(s->maxComputeWorkGroupCount));
    s->maxComputeWorkGroupInvocations = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->maxComputeWorkGroupSize, sizeof(s->maxComputeWorkGroupSize));
    s->subPixelPrecisionBits = (uint32_t)mvk_get_u32(d);
    s->subTexelPrecisionBits = (uint32_t)mvk_get_u32(d);
    s->mipmapPrecisionBits = (uint32_t)mvk_get_u32(d);
    s->maxDrawIndexedIndexValue = (uint32_t)mvk_get_u32(d);
    s->maxDrawIndirectCount = (uint32_t)mvk_get_u32(d);
    s->maxSamplerLodBias = mvk_bits_f32(mvk_get_u32(d));
    s->maxSamplerAnisotropy = mvk_bits_f32(mvk_get_u32(d));
    s->maxViewports = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->maxViewportDimensions, sizeof(s->maxViewportDimensions));
    mvk_get_bytes(d, s->viewportBoundsRange, sizeof(s->viewportBoundsRange));
    s->viewportSubPixelBits = (uint32_t)mvk_get_u32(d);
    s->minMemoryMapAlignment = (size_t)mvk_get_u64(d);
    s->minTexelBufferOffsetAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->minUniformBufferOffsetAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->minStorageBufferOffsetAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->minTexelOffset = (int32_t)mvk_get_u32(d);
    s->maxTexelOffset = (uint32_t)mvk_get_u32(d);
    s->minTexelGatherOffset = (int32_t)mvk_get_u32(d);
    s->maxTexelGatherOffset = (uint32_t)mvk_get_u32(d);
    s->minInterpolationOffset = mvk_bits_f32(mvk_get_u32(d));
    s->maxInterpolationOffset = mvk_bits_f32(mvk_get_u32(d));
    s->subPixelInterpolationOffsetBits = (uint32_t)mvk_get_u32(d);
    s->maxFramebufferWidth = (uint32_t)mvk_get_u32(d);
    s->maxFramebufferHeight = (uint32_t)mvk_get_u32(d);
    s->maxFramebufferLayers = (uint32_t)mvk_get_u32(d);
    s->framebufferColorSampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
    s->framebufferDepthSampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
    s->framebufferStencilSampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
    s->framebufferNoAttachmentsSampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
    s->maxColorAttachments = (uint32_t)mvk_get_u32(d);
    s->sampledImageColorSampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
    s->sampledImageIntegerSampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
    s->sampledImageDepthSampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
    s->sampledImageStencilSampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
    s->storageImageSampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
    s->maxSampleMaskWords = (uint32_t)mvk_get_u32(d);
    s->timestampComputeAndGraphics = (VkBool32)mvk_get_u32(d);
    s->timestampPeriod = mvk_bits_f32(mvk_get_u32(d));
    s->maxClipDistances = (uint32_t)mvk_get_u32(d);
    s->maxCullDistances = (uint32_t)mvk_get_u32(d);
    s->maxCombinedClipAndCullDistances = (uint32_t)mvk_get_u32(d);
    s->discreteQueuePriorities = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->pointSizeRange, sizeof(s->pointSizeRange));
    mvk_get_bytes(d, s->lineWidthRange, sizeof(s->lineWidthRange));
    s->pointSizeGranularity = mvk_bits_f32(mvk_get_u32(d));
    s->lineWidthGranularity = mvk_bits_f32(mvk_get_u32(d));
    s->strictLines = (VkBool32)mvk_get_u32(d);
    s->standardSampleLocations = (VkBool32)mvk_get_u32(d);
    s->optimalBufferCopyOffsetAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->optimalBufferCopyRowPitchAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->nonCoherentAtomSize = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceSparseProperties(MvkDecoder *d, VkPhysicalDeviceSparseProperties *s)
{
    s->residencyStandard2DBlockShape = (VkBool32)mvk_get_u32(d);
    s->residencyStandard2DMultisampleBlockShape = (VkBool32)mvk_get_u32(d);
    s->residencyStandard3DBlockShape = (VkBool32)mvk_get_u32(d);
    s->residencyAlignedMipSize = (VkBool32)mvk_get_u32(d);
    s->residencyNonResidentStrict = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkQueueFamilyProperties(MvkDecoder *d, VkQueueFamilyProperties *s)
{
    s->queueFlags = (VkQueueFlags)mvk_get_u32(d);
    s->queueCount = (uint32_t)mvk_get_u32(d);
    s->timestampValidBits = (uint32_t)mvk_get_u32(d);
    mvk_out_VkExtent3D(d, &s->minImageTransferGranularity);
}

void mvk_out_VkPhysicalDeviceMemoryProperties(MvkDecoder *d, VkPhysicalDeviceMemoryProperties *s)
{
    s->memoryTypeCount = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->memoryTypes, sizeof(s->memoryTypes));
    s->memoryHeapCount = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->memoryHeaps, sizeof(s->memoryHeaps));
}

void mvk_out_VkMemoryType(MvkDecoder *d, VkMemoryType *s)
{
    s->propertyFlags = (VkMemoryPropertyFlags)mvk_get_u32(d);
    s->heapIndex = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkMemoryHeap(MvkDecoder *d, VkMemoryHeap *s)
{
    s->size = (VkDeviceSize)mvk_get_u64(d);
    s->flags = (VkMemoryHeapFlags)mvk_get_u32(d);
}

void mvk_out_VkMemoryRequirements(MvkDecoder *d, VkMemoryRequirements *s)
{
    s->size = (VkDeviceSize)mvk_get_u64(d);
    s->alignment = (VkDeviceSize)mvk_get_u64(d);
    s->memoryTypeBits = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkSparseImageMemoryRequirements(MvkDecoder *d, VkSparseImageMemoryRequirements *s)
{
    mvk_out_VkSparseImageFormatProperties(d, &s->formatProperties);
    s->imageMipTailFirstLod = (uint32_t)mvk_get_u32(d);
    s->imageMipTailSize = (VkDeviceSize)mvk_get_u64(d);
    s->imageMipTailOffset = (VkDeviceSize)mvk_get_u64(d);
    s->imageMipTailStride = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkSparseImageFormatProperties(MvkDecoder *d, VkSparseImageFormatProperties *s)
{
    s->aspectMask = (VkImageAspectFlags)mvk_get_u32(d);
    mvk_out_VkExtent3D(d, &s->imageGranularity);
    s->flags = (VkSparseImageFormatFlags)mvk_get_u32(d);
}

void mvk_out_VkSubresourceLayout(MvkDecoder *d, VkSubresourceLayout *s)
{
    s->offset = (VkDeviceSize)mvk_get_u64(d);
    s->size = (VkDeviceSize)mvk_get_u64(d);
    s->rowPitch = (VkDeviceSize)mvk_get_u64(d);
    s->arrayPitch = (VkDeviceSize)mvk_get_u64(d);
    s->depthPitch = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkExtent2D(MvkDecoder *d, VkExtent2D *s)
{
    s->width = (uint32_t)mvk_get_u32(d);
    s->height = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkMemoryRequirements2(MvkDecoder *d, VkMemoryRequirements2 *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkMemoryRequirements(d, &s->memoryRequirements);
}

void mvk_out_VkSparseImageMemoryRequirements2(MvkDecoder *d, VkSparseImageMemoryRequirements2 *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkSparseImageMemoryRequirements(d, &s->memoryRequirements);
}

void mvk_out_VkPhysicalDeviceFeatures2(MvkDecoder *d, VkPhysicalDeviceFeatures2 *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkPhysicalDeviceFeatures(d, &s->features);
}

void mvk_out_VkPhysicalDeviceProperties2(MvkDecoder *d, VkPhysicalDeviceProperties2 *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkPhysicalDeviceProperties(d, &s->properties);
}

void mvk_out_VkFormatProperties2(MvkDecoder *d, VkFormatProperties2 *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkFormatProperties(d, &s->formatProperties);
}

void mvk_out_VkImageFormatProperties2(MvkDecoder *d, VkImageFormatProperties2 *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkImageFormatProperties(d, &s->imageFormatProperties);
}

void mvk_out_VkQueueFamilyProperties2(MvkDecoder *d, VkQueueFamilyProperties2 *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkQueueFamilyProperties(d, &s->queueFamilyProperties);
}

void mvk_out_VkPhysicalDeviceMemoryProperties2(MvkDecoder *d, VkPhysicalDeviceMemoryProperties2 *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkPhysicalDeviceMemoryProperties(d, &s->memoryProperties);
}

void mvk_out_VkSparseImageFormatProperties2(MvkDecoder *d, VkSparseImageFormatProperties2 *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkSparseImageFormatProperties(d, &s->properties);
}

void mvk_out_VkExternalBufferProperties(MvkDecoder *d, VkExternalBufferProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkExternalMemoryProperties(d, &s->externalMemoryProperties);
}

void mvk_out_VkExternalMemoryProperties(MvkDecoder *d, VkExternalMemoryProperties *s)
{
    s->externalMemoryFeatures = (VkExternalMemoryFeatureFlags)mvk_get_u32(d);
    s->exportFromImportedHandleTypes = (VkExternalMemoryHandleTypeFlags)mvk_get_u32(d);
    s->compatibleHandleTypes = (VkExternalMemoryHandleTypeFlags)mvk_get_u32(d);
}

void mvk_out_VkExternalFenceProperties(MvkDecoder *d, VkExternalFenceProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->exportFromImportedHandleTypes = (VkExternalFenceHandleTypeFlags)mvk_get_u32(d);
    s->compatibleHandleTypes = (VkExternalFenceHandleTypeFlags)mvk_get_u32(d);
    s->externalFenceFeatures = (VkExternalFenceFeatureFlags)mvk_get_u32(d);
}

void mvk_out_VkExternalSemaphoreProperties(MvkDecoder *d, VkExternalSemaphoreProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->exportFromImportedHandleTypes = (VkExternalSemaphoreHandleTypeFlags)mvk_get_u32(d);
    s->compatibleHandleTypes = (VkExternalSemaphoreHandleTypeFlags)mvk_get_u32(d);
    s->externalSemaphoreFeatures = (VkExternalSemaphoreFeatureFlags)mvk_get_u32(d);
}

void mvk_out_VkDescriptorSetLayoutSupport(MvkDecoder *d, VkDescriptorSetLayoutSupport *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->supported = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceToolProperties(MvkDecoder *d, VkPhysicalDeviceToolProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_get_bytes(d, s->name, sizeof(s->name));
    mvk_get_bytes(d, s->version, sizeof(s->version));
    s->purposes = (VkToolPurposeFlags)mvk_get_u32(d);
    mvk_get_bytes(d, s->description, sizeof(s->description));
    mvk_get_bytes(d, s->layer, sizeof(s->layer));
}

void mvk_out_VkSubresourceLayout2(MvkDecoder *d, VkSubresourceLayout2 *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkSubresourceLayout(d, &s->subresourceLayout);
}

void mvk_out_VkImageViewAddressPropertiesNVX(MvkDecoder *d, VkImageViewAddressPropertiesNVX *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->deviceAddress = (VkDeviceAddress)mvk_get_u64(d);
    s->size = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkExternalImageFormatPropertiesNV(MvkDecoder *d, VkExternalImageFormatPropertiesNV *s)
{
    mvk_out_VkImageFormatProperties(d, &s->imageFormatProperties);
    s->externalMemoryFeatures = (VkExternalMemoryFeatureFlagsNV)mvk_get_u32(d);
    s->exportFromImportedHandleTypes = (VkExternalMemoryHandleTypeFlagsNV)mvk_get_u32(d);
    s->compatibleHandleTypes = (VkExternalMemoryHandleTypeFlagsNV)mvk_get_u32(d);
}

void mvk_out_VkPerformanceCounterKHR(MvkDecoder *d, VkPerformanceCounterKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->unit = (VkPerformanceCounterUnitKHR)mvk_get_u32(d);
    s->scope = (VkPerformanceCounterScopeKHR)mvk_get_u32(d);
    s->storage = (VkPerformanceCounterStorageKHR)mvk_get_u32(d);
    mvk_get_bytes(d, s->uuid, sizeof(s->uuid));
}

void mvk_out_VkPerformanceCounterDescriptionKHR(MvkDecoder *d, VkPerformanceCounterDescriptionKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->flags = (VkPerformanceCounterDescriptionFlagsKHR)mvk_get_u32(d);
    mvk_get_bytes(d, s->name, sizeof(s->name));
    mvk_get_bytes(d, s->category, sizeof(s->category));
    mvk_get_bytes(d, s->description, sizeof(s->description));
}

void mvk_out_VkGpaDeviceClockModeInfoAMD(MvkDecoder *d, VkGpaDeviceClockModeInfoAMD *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->clockMode = (VkGpaDeviceClockModeAMD)mvk_get_u32(d);
    s->memoryClockRatioToPeak = mvk_bits_f32(mvk_get_u32(d));
    s->engineClockRatioToPeak = mvk_bits_f32(mvk_get_u32(d));
}

void mvk_out_VkGpaDeviceGetClockInfoAMD(MvkDecoder *d, VkGpaDeviceGetClockInfoAMD *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->memoryClockRatioToPeak = mvk_bits_f32(mvk_get_u32(d));
    s->engineClockRatioToPeak = mvk_bits_f32(mvk_get_u32(d));
    s->memoryClockFrequency = (uint32_t)mvk_get_u32(d);
    s->engineClockFrequency = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkHostAddressRangeEXT(MvkDecoder *d, VkHostAddressRangeEXT *s)
{
    mvk_skip_pointer(d);
    s->size = (size_t)mvk_get_u64(d);
}

void mvk_out_VkMultisamplePropertiesEXT(MvkDecoder *d, VkMultisamplePropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkExtent2D(d, &s->maxSampleLocationGridSize);
}

void mvk_out_VkAccelerationStructureBuildSizesInfoKHR(MvkDecoder *d, VkAccelerationStructureBuildSizesInfoKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->accelerationStructureSize = (VkDeviceSize)mvk_get_u64(d);
    s->updateScratchSize = (VkDeviceSize)mvk_get_u64(d);
    s->buildScratchSize = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkImageDrmFormatModifierPropertiesEXT(MvkDecoder *d, VkImageDrmFormatModifierPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->drmFormatModifier = (uint64_t)mvk_get_u64(d);
}

void mvk_out_VkPerformanceValueINTEL(MvkDecoder *d, VkPerformanceValueINTEL *s)
{
    s->type = (VkPerformanceValueTypeINTEL)mvk_get_u32(d);
    mvk_get_bytes(d, &s->data, sizeof(s->data));
}

void mvk_out_VkPhysicalDeviceFragmentShadingRateKHR(MvkDecoder *d, VkPhysicalDeviceFragmentShadingRateKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->sampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
    mvk_out_VkExtent2D(d, &s->fragmentSize);
}

void mvk_out_VkCooperativeMatrixPropertiesNV(MvkDecoder *d, VkCooperativeMatrixPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->MSize = (uint32_t)mvk_get_u32(d);
    s->NSize = (uint32_t)mvk_get_u32(d);
    s->KSize = (uint32_t)mvk_get_u32(d);
    s->AType = (VkComponentTypeNV)mvk_get_u32(d);
    s->BType = (VkComponentTypeNV)mvk_get_u32(d);
    s->CType = (VkComponentTypeNV)mvk_get_u32(d);
    s->DType = (VkComponentTypeNV)mvk_get_u32(d);
    s->scope = (VkScopeNV)mvk_get_u32(d);
}

void mvk_out_VkFramebufferMixedSamplesCombinationNV(MvkDecoder *d, VkFramebufferMixedSamplesCombinationNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->coverageReductionMode = (VkCoverageReductionModeNV)mvk_get_u32(d);
    s->rasterizationSamples = (VkSampleCountFlagBits)mvk_get_u32(d);
    s->depthStencilSamples = (VkSampleCountFlags)mvk_get_u32(d);
    s->colorSamples = (VkSampleCountFlags)mvk_get_u32(d);
}

void mvk_out_VkPipelineExecutablePropertiesKHR(MvkDecoder *d, VkPipelineExecutablePropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->stages = (VkShaderStageFlags)mvk_get_u32(d);
    mvk_get_bytes(d, s->name, sizeof(s->name));
    mvk_get_bytes(d, s->description, sizeof(s->description));
    s->subgroupSize = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPipelineExecutableStatisticKHR(MvkDecoder *d, VkPipelineExecutableStatisticKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_get_bytes(d, s->name, sizeof(s->name));
    mvk_get_bytes(d, s->description, sizeof(s->description));
    s->format = (VkPipelineExecutableStatisticFormatKHR)mvk_get_u32(d);
    mvk_get_bytes(d, &s->value, sizeof(s->value));
}

void mvk_out_VkPipelineExecutableInternalRepresentationKHR(MvkDecoder *d, VkPipelineExecutableInternalRepresentationKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_get_bytes(d, s->name, sizeof(s->name));
    mvk_get_bytes(d, s->description, sizeof(s->description));
    s->isText = (VkBool32)mvk_get_u32(d);
    s->dataSize = (size_t)mvk_get_u64(d);
    mvk_skip_pointer(d);
}

void mvk_out_VkBaseOutStructure(MvkDecoder *d, VkBaseOutStructure *s)
{
    mvk_out_chain(d, (void *)s->pNext);
}

void mvk_out_VkMicromapBuildSizesInfoEXT(MvkDecoder *d, VkMicromapBuildSizesInfoEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->micromapSize = (VkDeviceSize)mvk_get_u64(d);
    s->buildScratchSize = (VkDeviceSize)mvk_get_u64(d);
    s->discardable = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkDescriptorSetLayoutHostMappingInfoVALVE(MvkDecoder *d, VkDescriptorSetLayoutHostMappingInfoVALVE *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->descriptorOffset = (size_t)mvk_get_u64(d);
    s->descriptorSize = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkShaderModuleIdentifierEXT(MvkDecoder *d, VkShaderModuleIdentifierEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->identifierSize = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->identifier, sizeof(s->identifier));
}

void mvk_out_VkOpticalFlowImageFormatPropertiesNV(MvkDecoder *d, VkOpticalFlowImageFormatPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->format = (VkFormat)mvk_get_u32(d);
}

void mvk_out_VkCooperativeVectorPropertiesNV(MvkDecoder *d, VkCooperativeVectorPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->inputType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->inputInterpretation = (VkComponentTypeKHR)mvk_get_u32(d);
    s->matrixInterpretation = (VkComponentTypeKHR)mvk_get_u32(d);
    s->biasInterpretation = (VkComponentTypeKHR)mvk_get_u32(d);
    s->resultType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->transpose = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkCooperativeMatrixPropertiesKHR(MvkDecoder *d, VkCooperativeMatrixPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->MSize = (uint32_t)mvk_get_u32(d);
    s->NSize = (uint32_t)mvk_get_u32(d);
    s->KSize = (uint32_t)mvk_get_u32(d);
    s->AType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->BType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->CType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->ResultType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->saturatingAccumulation = (VkBool32)mvk_get_u32(d);
    s->scope = (VkScopeKHR)mvk_get_u32(d);
}

void mvk_out_VkDeviceFaultInfoKHR(MvkDecoder *d, VkDeviceFaultInfoKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->flags = (VkDeviceFaultFlagsKHR)mvk_get_u32(d);
    s->groupId = (uint64_t)mvk_get_u64(d);
    mvk_get_bytes(d, s->description, sizeof(s->description));
    mvk_out_VkDeviceFaultAddressInfoKHR(d, &s->faultAddressInfo);
    mvk_out_VkDeviceFaultAddressInfoKHR(d, &s->instructionAddressInfo);
    mvk_out_VkDeviceFaultVendorInfoKHR(d, &s->vendorInfo);
}

void mvk_out_VkDeviceFaultAddressInfoKHR(MvkDecoder *d, VkDeviceFaultAddressInfoKHR *s)
{
    s->addressType = (VkDeviceFaultAddressTypeKHR)mvk_get_u32(d);
    s->reportedAddress = (VkDeviceAddress)mvk_get_u64(d);
    s->addressPrecision = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkDeviceFaultVendorInfoKHR(MvkDecoder *d, VkDeviceFaultVendorInfoKHR *s)
{
    mvk_get_bytes(d, s->description, sizeof(s->description));
    s->vendorFaultCode = (uint64_t)mvk_get_u64(d);
    s->vendorFaultData = (uint64_t)mvk_get_u64(d);
}

void mvk_out_VkDeviceFaultDebugInfoKHR(MvkDecoder *d, VkDeviceFaultDebugInfoKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->vendorBinarySize = (uint32_t)mvk_get_u32(d);
    mvk_skip_pointer(d);
}

void mvk_out_VkCooperativeMatrixFlexibleDimensionsPropertiesNV(MvkDecoder *d, VkCooperativeMatrixFlexibleDimensionsPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->MGranularity = (uint32_t)mvk_get_u32(d);
    s->NGranularity = (uint32_t)mvk_get_u32(d);
    s->KGranularity = (uint32_t)mvk_get_u32(d);
    s->AType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->BType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->CType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->ResultType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->saturatingAccumulation = (VkBool32)mvk_get_u32(d);
    s->scope = (VkScopeKHR)mvk_get_u32(d);
    s->workgroupInvocations = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPerformanceCounterARM(MvkDecoder *d, VkPerformanceCounterARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->counterID = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPerformanceCounterDescriptionARM(MvkDecoder *d, VkPerformanceCounterDescriptionARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->flags = (VkPerformanceCounterDescriptionFlagsARM)mvk_get_u32(d);
    mvk_get_bytes(d, s->name, sizeof(s->name));
}

void mvk_out_VkShaderInstrumentationMetricDescriptionARM(MvkDecoder *d, VkShaderInstrumentationMetricDescriptionARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_get_bytes(d, s->name, sizeof(s->name));
    mvk_get_bytes(d, s->description, sizeof(s->description));
}

void mvk_out_VkCooperativeMatrixProperties2EXT(MvkDecoder *d, VkCooperativeMatrixProperties2EXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->MGranularity = (uint32_t)mvk_get_u32(d);
    s->NGranularity = (uint32_t)mvk_get_u32(d);
    s->KGranularity = (uint32_t)mvk_get_u32(d);
    s->AType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->BType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->CType = (VkComponentTypeKHR)mvk_get_u32(d);
    s->ResultType = (VkComponentTypeKHR)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceGroupProperties(MvkDecoder *d, VkPhysicalDeviceGroupProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->physicalDeviceCount = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->physicalDevices, sizeof(s->physicalDevices));
    s->subsetAllocation = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkLayerProperties(MvkDecoder *d, VkLayerProperties *s)
{
    mvk_get_bytes(d, s->layerName, sizeof(s->layerName));
    s->specVersion = (uint32_t)mvk_get_u32(d);
    s->implementationVersion = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->description, sizeof(s->description));
}

void mvk_out_VkExtensionProperties(MvkDecoder *d, VkExtensionProperties *s)
{
    mvk_get_bytes(d, s->extensionName, sizeof(s->extensionName));
    s->specVersion = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV(MvkDecoder *d, VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->deviceGeneratedCommands = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePushConstantBankFeaturesNV(MvkDecoder *d, VkPhysicalDevicePushConstantBankFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pushConstantBank = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePushConstantBankPropertiesNV(MvkDecoder *d, VkPhysicalDevicePushConstantBankPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxGraphicsPushConstantBanks = (uint32_t)mvk_get_u32(d);
    s->maxComputePushConstantBanks = (uint32_t)mvk_get_u32(d);
    s->maxGraphicsPushDataBanks = (uint32_t)mvk_get_u32(d);
    s->maxComputePushDataBanks = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV(MvkDecoder *d, VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->deviceGeneratedCompute = (VkBool32)mvk_get_u32(d);
    s->deviceGeneratedComputePipelines = (VkBool32)mvk_get_u32(d);
    s->deviceGeneratedComputeCaptureReplay = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePrivateDataFeatures(MvkDecoder *d, VkPhysicalDevicePrivateDataFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->privateData = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePrivateDataBaseHandleFeaturesNV(MvkDecoder *d, VkPhysicalDevicePrivateDataBaseHandleFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->privateDataBaseHandle = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV(MvkDecoder *d, VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxGraphicsShaderGroupCount = (uint32_t)mvk_get_u32(d);
    s->maxIndirectSequenceCount = (uint32_t)mvk_get_u32(d);
    s->maxIndirectCommandsTokenCount = (uint32_t)mvk_get_u32(d);
    s->maxIndirectCommandsStreamCount = (uint32_t)mvk_get_u32(d);
    s->maxIndirectCommandsTokenOffset = (uint32_t)mvk_get_u32(d);
    s->maxIndirectCommandsStreamStride = (uint32_t)mvk_get_u32(d);
    s->minSequencesCountBufferOffsetAlignment = (uint32_t)mvk_get_u32(d);
    s->minSequencesIndexBufferOffsetAlignment = (uint32_t)mvk_get_u32(d);
    s->minIndirectCommandsBufferOffsetAlignment = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceClusterAccelerationStructureFeaturesNV(MvkDecoder *d, VkPhysicalDeviceClusterAccelerationStructureFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->clusterAccelerationStructure = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceClusterAccelerationStructurePropertiesNV(MvkDecoder *d, VkPhysicalDeviceClusterAccelerationStructurePropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxVerticesPerCluster = (uint32_t)mvk_get_u32(d);
    s->maxTrianglesPerCluster = (uint32_t)mvk_get_u32(d);
    s->clusterScratchByteAlignment = (uint32_t)mvk_get_u32(d);
    s->clusterByteAlignment = (uint32_t)mvk_get_u32(d);
    s->clusterTemplateByteAlignment = (uint32_t)mvk_get_u32(d);
    s->clusterBottomLevelByteAlignment = (uint32_t)mvk_get_u32(d);
    s->clusterTemplateBoundsByteAlignment = (uint32_t)mvk_get_u32(d);
    s->maxClusterGeometryIndex = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMultiDrawPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceMultiDrawPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxMultiDrawCount = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePushDescriptorProperties(MvkDecoder *d, VkPhysicalDevicePushDescriptorProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxPushDescriptors = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDriverProperties(MvkDecoder *d, VkPhysicalDeviceDriverProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->driverID = (VkDriverId)mvk_get_u32(d);
    mvk_get_bytes(d, s->driverName, sizeof(s->driverName));
    mvk_get_bytes(d, s->driverInfo, sizeof(s->driverInfo));
    mvk_out_VkConformanceVersion(d, &s->conformanceVersion);
}

void mvk_out_VkConformanceVersion(MvkDecoder *d, VkConformanceVersion *s)
{
    s->major = (uint8_t)mvk_get_u8(d);
    s->minor = (uint8_t)mvk_get_u8(d);
    s->subminor = (uint8_t)mvk_get_u8(d);
    s->patch = (uint8_t)mvk_get_u8(d);
}

void mvk_out_VkPhysicalDeviceVariablePointersFeatures(MvkDecoder *d, VkPhysicalDeviceVariablePointersFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->variablePointersStorageBuffer = (VkBool32)mvk_get_u32(d);
    s->variablePointers = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkExternalImageFormatProperties(MvkDecoder *d, VkExternalImageFormatProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkExternalMemoryProperties(d, &s->externalMemoryProperties);
}

void mvk_out_VkPhysicalDeviceIDProperties(MvkDecoder *d, VkPhysicalDeviceIDProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_get_bytes(d, s->deviceUUID, sizeof(s->deviceUUID));
    mvk_get_bytes(d, s->driverUUID, sizeof(s->driverUUID));
    mvk_get_bytes(d, s->deviceLUID, sizeof(s->deviceLUID));
    s->deviceNodeMask = (uint32_t)mvk_get_u32(d);
    s->deviceLUIDValid = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMultiviewFeatures(MvkDecoder *d, VkPhysicalDeviceMultiviewFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->multiview = (VkBool32)mvk_get_u32(d);
    s->multiviewGeometryShader = (VkBool32)mvk_get_u32(d);
    s->multiviewTessellationShader = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMultiviewProperties(MvkDecoder *d, VkPhysicalDeviceMultiviewProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxMultiviewViewCount = (uint32_t)mvk_get_u32(d);
    s->maxMultiviewInstanceIndex = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDiscardRectanglePropertiesEXT(MvkDecoder *d, VkPhysicalDeviceDiscardRectanglePropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxDiscardRectangles = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX(MvkDecoder *d, VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->perViewPositionAllComponents = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevice16BitStorageFeatures(MvkDecoder *d, VkPhysicalDevice16BitStorageFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->storageBuffer16BitAccess = (VkBool32)mvk_get_u32(d);
    s->uniformAndStorageBuffer16BitAccess = (VkBool32)mvk_get_u32(d);
    s->storagePushConstant16 = (VkBool32)mvk_get_u32(d);
    s->storageInputOutput16 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceSubgroupProperties(MvkDecoder *d, VkPhysicalDeviceSubgroupProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->subgroupSize = (uint32_t)mvk_get_u32(d);
    s->supportedStages = (VkShaderStageFlags)mvk_get_u32(d);
    s->supportedOperations = (VkSubgroupFeatureFlags)mvk_get_u32(d);
    s->quadOperationsInAllStages = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures(MvkDecoder *d, VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderSubgroupExtendedTypes = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePointClippingProperties(MvkDecoder *d, VkPhysicalDevicePointClippingProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pointClippingBehavior = (VkPointClippingBehavior)mvk_get_u32(d);
}

void mvk_out_VkMemoryDedicatedRequirements(MvkDecoder *d, VkMemoryDedicatedRequirements *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->prefersDedicatedAllocation = (VkBool32)mvk_get_u32(d);
    s->requiresDedicatedAllocation = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceSamplerYcbcrConversionFeatures(MvkDecoder *d, VkPhysicalDeviceSamplerYcbcrConversionFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->samplerYcbcrConversion = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkSamplerYcbcrConversionImageFormatProperties(MvkDecoder *d, VkSamplerYcbcrConversionImageFormatProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->combinedImageSamplerDescriptorCount = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkTextureLODGatherFormatPropertiesAMD(MvkDecoder *d, VkTextureLODGatherFormatPropertiesAMD *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->supportsTextureGatherLODBiasAMD = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceProtectedMemoryFeatures(MvkDecoder *d, VkPhysicalDeviceProtectedMemoryFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->protectedMemory = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceProtectedMemoryProperties(MvkDecoder *d, VkPhysicalDeviceProtectedMemoryProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->protectedNoFault = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceSamplerFilterMinmaxProperties(MvkDecoder *d, VkPhysicalDeviceSamplerFilterMinmaxProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->filterMinmaxSingleComponentFormats = (VkBool32)mvk_get_u32(d);
    s->filterMinmaxImageComponentMapping = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceSampleLocationsPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceSampleLocationsPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->sampleLocationSampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
    mvk_out_VkExtent2D(d, &s->maxSampleLocationGridSize);
    mvk_get_bytes(d, s->sampleLocationCoordinateRange, sizeof(s->sampleLocationCoordinateRange));
    s->sampleLocationSubPixelBits = (uint32_t)mvk_get_u32(d);
    s->variableSampleLocations = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->advancedBlendCoherentOperations = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMultiDrawFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMultiDrawFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->multiDraw = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->advancedBlendMaxColorAttachments = (uint32_t)mvk_get_u32(d);
    s->advancedBlendIndependentBlend = (VkBool32)mvk_get_u32(d);
    s->advancedBlendNonPremultipliedSrcColor = (VkBool32)mvk_get_u32(d);
    s->advancedBlendNonPremultipliedDstColor = (VkBool32)mvk_get_u32(d);
    s->advancedBlendCorrelatedOverlap = (VkBool32)mvk_get_u32(d);
    s->advancedBlendAllOperations = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceInlineUniformBlockFeatures(MvkDecoder *d, VkPhysicalDeviceInlineUniformBlockFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->inlineUniformBlock = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingInlineUniformBlockUpdateAfterBind = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceInlineUniformBlockProperties(MvkDecoder *d, VkPhysicalDeviceInlineUniformBlockProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxInlineUniformBlockSize = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorInlineUniformBlocks = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetInlineUniformBlocks = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindInlineUniformBlocks = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMaintenance3Properties(MvkDecoder *d, VkPhysicalDeviceMaintenance3Properties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxPerSetDescriptors = (uint32_t)mvk_get_u32(d);
    s->maxMemoryAllocationSize = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceMaintenance4Features(MvkDecoder *d, VkPhysicalDeviceMaintenance4Features *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maintenance4 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMaintenance4Properties(MvkDecoder *d, VkPhysicalDeviceMaintenance4Properties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxBufferSize = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceMaintenance5Features(MvkDecoder *d, VkPhysicalDeviceMaintenance5Features *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maintenance5 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMaintenance5Properties(MvkDecoder *d, VkPhysicalDeviceMaintenance5Properties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->earlyFragmentMultisampleCoverageAfterSampleCounting = (VkBool32)mvk_get_u32(d);
    s->earlyFragmentSampleMaskTestBeforeSampleCounting = (VkBool32)mvk_get_u32(d);
    s->depthStencilSwizzleOneSupport = (VkBool32)mvk_get_u32(d);
    s->polygonModePointSize = (VkBool32)mvk_get_u32(d);
    s->nonStrictSinglePixelWideLinesUseParallelogram = (VkBool32)mvk_get_u32(d);
    s->nonStrictWideLinesUseParallelogram = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMaintenance6Features(MvkDecoder *d, VkPhysicalDeviceMaintenance6Features *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maintenance6 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMaintenance6Properties(MvkDecoder *d, VkPhysicalDeviceMaintenance6Properties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->blockTexelViewCompatibleMultipleLayers = (VkBool32)mvk_get_u32(d);
    s->maxCombinedImageSamplerDescriptorCount = (uint32_t)mvk_get_u32(d);
    s->fragmentShadingRateClampCombinerInputs = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMaintenance7FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance7FeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maintenance7 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMaintenance7PropertiesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance7PropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->robustFragmentShadingRateAttachmentAccess = (VkBool32)mvk_get_u32(d);
    s->separateDepthStencilAttachmentAccess = (VkBool32)mvk_get_u32(d);
    s->maxDescriptorSetTotalUniformBuffersDynamic = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetTotalStorageBuffersDynamic = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetTotalBuffersDynamic = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindTotalUniformBuffersDynamic = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindTotalStorageBuffersDynamic = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindTotalBuffersDynamic = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceLayeredApiPropertiesListKHR(MvkDecoder *d, VkPhysicalDeviceLayeredApiPropertiesListKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->layeredApiCount = (uint32_t)mvk_get_u32(d);
    mvk_skip_pointer(d);
}

void mvk_out_VkPhysicalDeviceLayeredApiPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceLayeredApiPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->vendorID = (uint32_t)mvk_get_u32(d);
    s->deviceID = (uint32_t)mvk_get_u32(d);
    s->layeredAPI = (VkPhysicalDeviceLayeredApiKHR)mvk_get_u32(d);
    mvk_get_bytes(d, s->deviceName, sizeof(s->deviceName));
}

void mvk_out_VkPhysicalDeviceLayeredApiVulkanPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceLayeredApiVulkanPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkPhysicalDeviceProperties2(d, &s->properties);
}

void mvk_out_VkPhysicalDeviceMaintenance8FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance8FeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maintenance8 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMaintenance9FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance9FeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maintenance9 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMaintenance9PropertiesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance9PropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->image2DViewOf3DSparse = (VkBool32)mvk_get_u32(d);
    s->defaultVertexAttributeValue = (VkDefaultVertexAttributeValueKHR)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMaintenance11FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance11FeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maintenance11 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMaintenance10PropertiesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance10PropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rgba4OpaqueBlackSwizzled = (VkBool32)mvk_get_u32(d);
    s->resolveSrgbFormatAppliesTransferFunction = (VkBool32)mvk_get_u32(d);
    s->resolveSrgbFormatSupportsTransferFunctionControl = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMaintenance10FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance10FeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maintenance10 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkQueueFamilyOwnershipTransferPropertiesKHR(MvkDecoder *d, VkQueueFamilyOwnershipTransferPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->optimalImageTransferToQueueFamilies = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR(MvkDecoder *d, VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkExtent3D(d, &s->optimalImageTransferGranularity);
}

void mvk_out_VkPhysicalDeviceShaderDrawParametersFeatures(MvkDecoder *d, VkPhysicalDeviceShaderDrawParametersFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderDrawParameters = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderFloat16Int8Features(MvkDecoder *d, VkPhysicalDeviceShaderFloat16Int8Features *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderInt8 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFloatControlsProperties(MvkDecoder *d, VkPhysicalDeviceFloatControlsProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->denormBehaviorIndependence = (VkShaderFloatControlsIndependence)mvk_get_u32(d);
    s->roundingModeIndependence = (VkShaderFloatControlsIndependence)mvk_get_u32(d);
    s->shaderSignedZeroInfNanPreserveFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderSignedZeroInfNanPreserveFloat32 = (VkBool32)mvk_get_u32(d);
    s->shaderSignedZeroInfNanPreserveFloat64 = (VkBool32)mvk_get_u32(d);
    s->shaderDenormPreserveFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderDenormPreserveFloat32 = (VkBool32)mvk_get_u32(d);
    s->shaderDenormPreserveFloat64 = (VkBool32)mvk_get_u32(d);
    s->shaderDenormFlushToZeroFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderDenormFlushToZeroFloat32 = (VkBool32)mvk_get_u32(d);
    s->shaderDenormFlushToZeroFloat64 = (VkBool32)mvk_get_u32(d);
    s->shaderRoundingModeRTEFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderRoundingModeRTEFloat32 = (VkBool32)mvk_get_u32(d);
    s->shaderRoundingModeRTEFloat64 = (VkBool32)mvk_get_u32(d);
    s->shaderRoundingModeRTZFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderRoundingModeRTZFloat32 = (VkBool32)mvk_get_u32(d);
    s->shaderRoundingModeRTZFloat64 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceHostQueryResetFeatures(MvkDecoder *d, VkPhysicalDeviceHostQueryResetFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->hostQueryReset = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->elapsedTimerQuery = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceGlobalPriorityQueryFeatures(MvkDecoder *d, VkPhysicalDeviceGlobalPriorityQueryFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->globalPriorityQuery = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkQueueFamilyGlobalPriorityProperties(MvkDecoder *d, VkQueueFamilyGlobalPriorityProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->priorityCount = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->priorities, sizeof(s->priorities));
}

void mvk_out_VkPhysicalDeviceDeviceMemoryReportFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDeviceMemoryReportFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->deviceMemoryReport = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceConservativeRasterizationPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceConservativeRasterizationPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->primitiveOverestimationSize = mvk_bits_f32(mvk_get_u32(d));
    s->maxExtraPrimitiveOverestimationSize = mvk_bits_f32(mvk_get_u32(d));
    s->extraPrimitiveOverestimationSizeGranularity = mvk_bits_f32(mvk_get_u32(d));
    s->primitiveUnderestimation = (VkBool32)mvk_get_u32(d);
    s->conservativePointAndLineRasterization = (VkBool32)mvk_get_u32(d);
    s->degenerateTrianglesRasterized = (VkBool32)mvk_get_u32(d);
    s->degenerateLinesRasterized = (VkBool32)mvk_get_u32(d);
    s->fullyCoveredFragmentShaderInputVariable = (VkBool32)mvk_get_u32(d);
    s->conservativeRasterizationPostDepthCoverage = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderCorePropertiesAMD(MvkDecoder *d, VkPhysicalDeviceShaderCorePropertiesAMD *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderEngineCount = (uint32_t)mvk_get_u32(d);
    s->shaderArraysPerEngineCount = (uint32_t)mvk_get_u32(d);
    s->computeUnitsPerShaderArray = (uint32_t)mvk_get_u32(d);
    s->simdPerComputeUnit = (uint32_t)mvk_get_u32(d);
    s->wavefrontsPerSimd = (uint32_t)mvk_get_u32(d);
    s->wavefrontSize = (uint32_t)mvk_get_u32(d);
    s->sgprsPerSimd = (uint32_t)mvk_get_u32(d);
    s->minSgprAllocation = (uint32_t)mvk_get_u32(d);
    s->maxSgprAllocation = (uint32_t)mvk_get_u32(d);
    s->sgprAllocationGranularity = (uint32_t)mvk_get_u32(d);
    s->vgprsPerSimd = (uint32_t)mvk_get_u32(d);
    s->minVgprAllocation = (uint32_t)mvk_get_u32(d);
    s->maxVgprAllocation = (uint32_t)mvk_get_u32(d);
    s->vgprAllocationGranularity = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderCoreProperties2AMD(MvkDecoder *d, VkPhysicalDeviceShaderCoreProperties2AMD *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderCoreFeatures = (VkShaderCorePropertiesFlagsAMD)mvk_get_u32(d);
    s->activeComputeUnitCount = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDescriptorIndexingFeatures(MvkDecoder *d, VkPhysicalDeviceDescriptorIndexingFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderInputAttachmentArrayDynamicIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderUniformTexelBufferArrayDynamicIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderStorageTexelBufferArrayDynamicIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderUniformBufferArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderSampledImageArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderStorageBufferArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderStorageImageArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderInputAttachmentArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderUniformTexelBufferArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderStorageTexelBufferArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingUniformBufferUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingSampledImageUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingStorageImageUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingStorageBufferUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingUniformTexelBufferUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingStorageTexelBufferUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingUpdateUnusedWhilePending = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingPartiallyBound = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingVariableDescriptorCount = (VkBool32)mvk_get_u32(d);
    s->runtimeDescriptorArray = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDescriptorIndexingProperties(MvkDecoder *d, VkPhysicalDeviceDescriptorIndexingProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxUpdateAfterBindDescriptorsInAllPools = (uint32_t)mvk_get_u32(d);
    s->shaderUniformBufferArrayNonUniformIndexingNative = (VkBool32)mvk_get_u32(d);
    s->shaderSampledImageArrayNonUniformIndexingNative = (VkBool32)mvk_get_u32(d);
    s->shaderStorageBufferArrayNonUniformIndexingNative = (VkBool32)mvk_get_u32(d);
    s->shaderStorageImageArrayNonUniformIndexingNative = (VkBool32)mvk_get_u32(d);
    s->shaderInputAttachmentArrayNonUniformIndexingNative = (VkBool32)mvk_get_u32(d);
    s->robustBufferAccessUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->quadDivergentImplicitLod = (VkBool32)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindSamplers = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindUniformBuffers = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindStorageBuffers = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindSampledImages = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindStorageImages = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindInputAttachments = (uint32_t)mvk_get_u32(d);
    s->maxPerStageUpdateAfterBindResources = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindSamplers = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindUniformBuffers = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindStorageBuffers = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindSampledImages = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindStorageImages = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindInputAttachments = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkDescriptorSetVariableDescriptorCountLayoutSupport(MvkDecoder *d, VkDescriptorSetVariableDescriptorCountLayoutSupport *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxVariableDescriptorCount = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceTimelineSemaphoreFeatures(MvkDecoder *d, VkPhysicalDeviceTimelineSemaphoreFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->timelineSemaphore = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceTimelineSemaphoreProperties(MvkDecoder *d, VkPhysicalDeviceTimelineSemaphoreProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxTimelineSemaphoreValueDifference = (uint64_t)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxVertexAttribDivisor = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceVertexAttributeDivisorProperties(MvkDecoder *d, VkPhysicalDeviceVertexAttributeDivisorProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxVertexAttribDivisor = (uint32_t)mvk_get_u32(d);
    s->supportsNonZeroFirstInstance = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePCIBusInfoPropertiesEXT(MvkDecoder *d, VkPhysicalDevicePCIBusInfoPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pciDomain = (uint32_t)mvk_get_u32(d);
    s->pciBus = (uint32_t)mvk_get_u32(d);
    s->pciDevice = (uint32_t)mvk_get_u32(d);
    s->pciFunction = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevice8BitStorageFeatures(MvkDecoder *d, VkPhysicalDevice8BitStorageFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->storageBuffer8BitAccess = (VkBool32)mvk_get_u32(d);
    s->uniformAndStorageBuffer8BitAccess = (VkBool32)mvk_get_u32(d);
    s->storagePushConstant8 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceConditionalRenderingFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceConditionalRenderingFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->conditionalRendering = (VkBool32)mvk_get_u32(d);
    s->inheritedConditionalRendering = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceVulkanMemoryModelFeatures(MvkDecoder *d, VkPhysicalDeviceVulkanMemoryModelFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->vulkanMemoryModel = (VkBool32)mvk_get_u32(d);
    s->vulkanMemoryModelDeviceScope = (VkBool32)mvk_get_u32(d);
    s->vulkanMemoryModelAvailabilityVisibilityChains = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderAtomicInt64Features(MvkDecoder *d, VkPhysicalDeviceShaderAtomicInt64Features *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderBufferInt64Atomics = (VkBool32)mvk_get_u32(d);
    s->shaderSharedInt64Atomics = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderAtomicFloatFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderBufferFloat32Atomics = (VkBool32)mvk_get_u32(d);
    s->shaderBufferFloat32AtomicAdd = (VkBool32)mvk_get_u32(d);
    s->shaderBufferFloat64Atomics = (VkBool32)mvk_get_u32(d);
    s->shaderBufferFloat64AtomicAdd = (VkBool32)mvk_get_u32(d);
    s->shaderSharedFloat32Atomics = (VkBool32)mvk_get_u32(d);
    s->shaderSharedFloat32AtomicAdd = (VkBool32)mvk_get_u32(d);
    s->shaderSharedFloat64Atomics = (VkBool32)mvk_get_u32(d);
    s->shaderSharedFloat64AtomicAdd = (VkBool32)mvk_get_u32(d);
    s->shaderImageFloat32Atomics = (VkBool32)mvk_get_u32(d);
    s->shaderImageFloat32AtomicAdd = (VkBool32)mvk_get_u32(d);
    s->sparseImageFloat32Atomics = (VkBool32)mvk_get_u32(d);
    s->sparseImageFloat32AtomicAdd = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderBufferFloat16Atomics = (VkBool32)mvk_get_u32(d);
    s->shaderBufferFloat16AtomicAdd = (VkBool32)mvk_get_u32(d);
    s->shaderBufferFloat16AtomicMinMax = (VkBool32)mvk_get_u32(d);
    s->shaderBufferFloat32AtomicMinMax = (VkBool32)mvk_get_u32(d);
    s->shaderBufferFloat64AtomicMinMax = (VkBool32)mvk_get_u32(d);
    s->shaderSharedFloat16Atomics = (VkBool32)mvk_get_u32(d);
    s->shaderSharedFloat16AtomicAdd = (VkBool32)mvk_get_u32(d);
    s->shaderSharedFloat16AtomicMinMax = (VkBool32)mvk_get_u32(d);
    s->shaderSharedFloat32AtomicMinMax = (VkBool32)mvk_get_u32(d);
    s->shaderSharedFloat64AtomicMinMax = (VkBool32)mvk_get_u32(d);
    s->shaderImageFloat32AtomicMinMax = (VkBool32)mvk_get_u32(d);
    s->sparseImageFloat32AtomicMinMax = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceVertexAttributeDivisorFeatures(MvkDecoder *d, VkPhysicalDeviceVertexAttributeDivisorFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->vertexAttributeInstanceRateDivisor = (VkBool32)mvk_get_u32(d);
    s->vertexAttributeInstanceRateZeroDivisor = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkQueueFamilyCheckpointPropertiesNV(MvkDecoder *d, VkQueueFamilyCheckpointPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->checkpointExecutionStageMask = (VkPipelineStageFlags)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDepthStencilResolveProperties(MvkDecoder *d, VkPhysicalDeviceDepthStencilResolveProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->supportedDepthResolveModes = (VkResolveModeFlags)mvk_get_u32(d);
    s->supportedStencilResolveModes = (VkResolveModeFlags)mvk_get_u32(d);
    s->independentResolveNone = (VkBool32)mvk_get_u32(d);
    s->independentResolve = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceASTCDecodeFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceASTCDecodeFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->decodeModeSharedExponent = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceTransformFeedbackFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceTransformFeedbackFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->transformFeedback = (VkBool32)mvk_get_u32(d);
    s->geometryStreams = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceTransformFeedbackPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceTransformFeedbackPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxTransformFeedbackStreams = (uint32_t)mvk_get_u32(d);
    s->maxTransformFeedbackBuffers = (uint32_t)mvk_get_u32(d);
    s->maxTransformFeedbackBufferSize = (VkDeviceSize)mvk_get_u64(d);
    s->maxTransformFeedbackStreamDataSize = (uint32_t)mvk_get_u32(d);
    s->maxTransformFeedbackBufferDataSize = (uint32_t)mvk_get_u32(d);
    s->maxTransformFeedbackBufferDataStride = (uint32_t)mvk_get_u32(d);
    s->transformFeedbackQueries = (VkBool32)mvk_get_u32(d);
    s->transformFeedbackStreamsLinesTriangles = (VkBool32)mvk_get_u32(d);
    s->transformFeedbackRasterizationStreamSelect = (VkBool32)mvk_get_u32(d);
    s->transformFeedbackDraw = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV(MvkDecoder *d, VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->representativeFragmentTest = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceExclusiveScissorFeaturesNV(MvkDecoder *d, VkPhysicalDeviceExclusiveScissorFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->exclusiveScissor = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCornerSampledImageFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCornerSampledImageFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cornerSampledImage = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->computeDerivativeGroupQuads = (VkBool32)mvk_get_u32(d);
    s->computeDerivativeGroupLinear = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->meshAndTaskShaderDerivatives = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderImageFootprintFeaturesNV(MvkDecoder *d, VkPhysicalDeviceShaderImageFootprintFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->imageFootprint = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV(MvkDecoder *d, VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->dedicatedAllocationImageAliasing = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->indirectMemoryCopy = (VkBool32)mvk_get_u32(d);
    s->indirectMemoryToImageCopy = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCopyMemoryIndirectFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCopyMemoryIndirectFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->indirectCopy = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->supportedQueues = (VkQueueFlags)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMemoryDecompressionFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMemoryDecompressionFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->memoryDecompression = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMemoryDecompressionPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceMemoryDecompressionPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->decompressionMethods = (VkMemoryDecompressionMethodFlagsEXT)mvk_get_u64(d);
    s->maxDecompressionIndirectCount = (uint64_t)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceShadingRateImageFeaturesNV(MvkDecoder *d, VkPhysicalDeviceShadingRateImageFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shadingRateImage = (VkBool32)mvk_get_u32(d);
    s->shadingRateCoarseSampleOrder = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShadingRateImagePropertiesNV(MvkDecoder *d, VkPhysicalDeviceShadingRateImagePropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkExtent2D(d, &s->shadingRateTexelSize);
    s->shadingRatePaletteSize = (uint32_t)mvk_get_u32(d);
    s->shadingRateMaxCoarseSamples = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceInvocationMaskFeaturesHUAWEI(MvkDecoder *d, VkPhysicalDeviceInvocationMaskFeaturesHUAWEI *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->invocationMask = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMeshShaderFeaturesNV(MvkDecoder *d, VkPhysicalDeviceMeshShaderFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->taskShader = (VkBool32)mvk_get_u32(d);
    s->meshShader = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMeshShaderPropertiesNV(MvkDecoder *d, VkPhysicalDeviceMeshShaderPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxDrawMeshTasksCount = (uint32_t)mvk_get_u32(d);
    s->maxTaskWorkGroupInvocations = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->maxTaskWorkGroupSize, sizeof(s->maxTaskWorkGroupSize));
    s->maxTaskTotalMemorySize = (uint32_t)mvk_get_u32(d);
    s->maxTaskOutputCount = (uint32_t)mvk_get_u32(d);
    s->maxMeshWorkGroupInvocations = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->maxMeshWorkGroupSize, sizeof(s->maxMeshWorkGroupSize));
    s->maxMeshTotalMemorySize = (uint32_t)mvk_get_u32(d);
    s->maxMeshOutputVertices = (uint32_t)mvk_get_u32(d);
    s->maxMeshOutputPrimitives = (uint32_t)mvk_get_u32(d);
    s->maxMeshMultiviewViewCount = (uint32_t)mvk_get_u32(d);
    s->meshOutputPerVertexGranularity = (uint32_t)mvk_get_u32(d);
    s->meshOutputPerPrimitiveGranularity = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMeshShaderFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMeshShaderFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->taskShader = (VkBool32)mvk_get_u32(d);
    s->meshShader = (VkBool32)mvk_get_u32(d);
    s->multiviewMeshShader = (VkBool32)mvk_get_u32(d);
    s->primitiveFragmentShadingRateMeshShader = (VkBool32)mvk_get_u32(d);
    s->meshShaderQueries = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMeshShaderPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceMeshShaderPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxTaskWorkGroupTotalCount = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->maxTaskWorkGroupCount, sizeof(s->maxTaskWorkGroupCount));
    s->maxTaskWorkGroupInvocations = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->maxTaskWorkGroupSize, sizeof(s->maxTaskWorkGroupSize));
    s->maxTaskPayloadSize = (uint32_t)mvk_get_u32(d);
    s->maxTaskSharedMemorySize = (uint32_t)mvk_get_u32(d);
    s->maxTaskPayloadAndSharedMemorySize = (uint32_t)mvk_get_u32(d);
    s->maxMeshWorkGroupTotalCount = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->maxMeshWorkGroupCount, sizeof(s->maxMeshWorkGroupCount));
    s->maxMeshWorkGroupInvocations = (uint32_t)mvk_get_u32(d);
    mvk_get_bytes(d, s->maxMeshWorkGroupSize, sizeof(s->maxMeshWorkGroupSize));
    s->maxMeshSharedMemorySize = (uint32_t)mvk_get_u32(d);
    s->maxMeshPayloadAndSharedMemorySize = (uint32_t)mvk_get_u32(d);
    s->maxMeshOutputMemorySize = (uint32_t)mvk_get_u32(d);
    s->maxMeshPayloadAndOutputMemorySize = (uint32_t)mvk_get_u32(d);
    s->maxMeshOutputComponents = (uint32_t)mvk_get_u32(d);
    s->maxMeshOutputVertices = (uint32_t)mvk_get_u32(d);
    s->maxMeshOutputPrimitives = (uint32_t)mvk_get_u32(d);
    s->maxMeshOutputLayers = (uint32_t)mvk_get_u32(d);
    s->maxMeshMultiviewViewCount = (uint32_t)mvk_get_u32(d);
    s->meshOutputPerVertexGranularity = (uint32_t)mvk_get_u32(d);
    s->meshOutputPerPrimitiveGranularity = (uint32_t)mvk_get_u32(d);
    s->maxPreferredTaskWorkGroupInvocations = (uint32_t)mvk_get_u32(d);
    s->maxPreferredMeshWorkGroupInvocations = (uint32_t)mvk_get_u32(d);
    s->prefersLocalInvocationVertexOutput = (VkBool32)mvk_get_u32(d);
    s->prefersLocalInvocationPrimitiveOutput = (VkBool32)mvk_get_u32(d);
    s->prefersCompactVertexOutput = (VkBool32)mvk_get_u32(d);
    s->prefersCompactPrimitiveOutput = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceAccelerationStructureFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceAccelerationStructureFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->accelerationStructure = (VkBool32)mvk_get_u32(d);
    s->accelerationStructureCaptureReplay = (VkBool32)mvk_get_u32(d);
    s->accelerationStructureIndirectBuild = (VkBool32)mvk_get_u32(d);
    s->accelerationStructureHostCommands = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingAccelerationStructureUpdateAfterBind = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayTracingPipelineFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceRayTracingPipelineFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rayTracingPipeline = (VkBool32)mvk_get_u32(d);
    s->rayTracingPipelineShaderGroupHandleCaptureReplay = (VkBool32)mvk_get_u32(d);
    s->rayTracingPipelineShaderGroupHandleCaptureReplayMixed = (VkBool32)mvk_get_u32(d);
    s->rayTracingPipelineTraceRaysIndirect = (VkBool32)mvk_get_u32(d);
    s->rayTraversalPrimitiveCulling = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayQueryFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceRayQueryFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rayQuery = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceAccelerationStructurePropertiesKHR(MvkDecoder *d, VkPhysicalDeviceAccelerationStructurePropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxGeometryCount = (uint64_t)mvk_get_u64(d);
    s->maxInstanceCount = (uint64_t)mvk_get_u64(d);
    s->maxPrimitiveCount = (uint64_t)mvk_get_u64(d);
    s->maxPerStageDescriptorAccelerationStructures = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindAccelerationStructures = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetAccelerationStructures = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindAccelerationStructures = (uint32_t)mvk_get_u32(d);
    s->minAccelerationStructureScratchOffsetAlignment = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayTracingPipelinePropertiesKHR(MvkDecoder *d, VkPhysicalDeviceRayTracingPipelinePropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderGroupHandleSize = (uint32_t)mvk_get_u32(d);
    s->maxRayRecursionDepth = (uint32_t)mvk_get_u32(d);
    s->maxShaderGroupStride = (uint32_t)mvk_get_u32(d);
    s->shaderGroupBaseAlignment = (uint32_t)mvk_get_u32(d);
    s->shaderGroupHandleCaptureReplaySize = (uint32_t)mvk_get_u32(d);
    s->maxRayDispatchInvocationCount = (uint32_t)mvk_get_u32(d);
    s->shaderGroupHandleAlignment = (uint32_t)mvk_get_u32(d);
    s->maxRayHitAttributeSize = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayTracingPropertiesNV(MvkDecoder *d, VkPhysicalDeviceRayTracingPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderGroupHandleSize = (uint32_t)mvk_get_u32(d);
    s->maxRecursionDepth = (uint32_t)mvk_get_u32(d);
    s->maxShaderGroupStride = (uint32_t)mvk_get_u32(d);
    s->shaderGroupBaseAlignment = (uint32_t)mvk_get_u32(d);
    s->maxGeometryCount = (uint64_t)mvk_get_u64(d);
    s->maxInstanceCount = (uint64_t)mvk_get_u64(d);
    s->maxTriangleCount = (uint64_t)mvk_get_u64(d);
    s->maxDescriptorSetAccelerationStructures = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rayTracingMaintenance1 = (VkBool32)mvk_get_u32(d);
    s->rayTracingPipelineTraceRaysIndirect2 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkDrmFormatModifierPropertiesListEXT(MvkDecoder *d, VkDrmFormatModifierPropertiesListEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->drmFormatModifierCount = (uint32_t)mvk_get_u32(d);
    mvk_skip_pointer(d);
}

void mvk_out_VkDrmFormatModifierPropertiesEXT(MvkDecoder *d, VkDrmFormatModifierPropertiesEXT *s)
{
    s->drmFormatModifier = (uint64_t)mvk_get_u64(d);
    s->drmFormatModifierPlaneCount = (uint32_t)mvk_get_u32(d);
    s->drmFormatModifierTilingFeatures = (VkFormatFeatureFlags)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentDensityMapFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMapFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->fragmentDensityMap = (VkBool32)mvk_get_u32(d);
    s->fragmentDensityMapDynamic = (VkBool32)mvk_get_u32(d);
    s->fragmentDensityMapNonSubsampledImages = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentDensityMap2FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMap2FeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->fragmentDensityMapDeferred = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->fragmentDensityMapOffset = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentDensityMapPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMapPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkExtent2D(d, &s->minFragmentDensityTexelSize);
    mvk_out_VkExtent2D(d, &s->maxFragmentDensityTexelSize);
    s->fragmentDensityInvocations = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentDensityMap2PropertiesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMap2PropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->subsampledLoads = (VkBool32)mvk_get_u32(d);
    s->subsampledCoarseReconstructionEarlyAccess = (VkBool32)mvk_get_u32(d);
    s->maxSubsampledArrayLayers = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetSubsampledSamplers = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkExtent2D(d, &s->fragmentDensityOffsetGranularity);
}

void mvk_out_VkPhysicalDeviceScalarBlockLayoutFeatures(MvkDecoder *d, VkPhysicalDeviceScalarBlockLayoutFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->scalarBlockLayout = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceUniformBufferStandardLayoutFeatures(MvkDecoder *d, VkPhysicalDeviceUniformBufferStandardLayoutFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->uniformBufferStandardLayout = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDepthClipEnableFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDepthClipEnableFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->depthClipEnable = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMemoryBudgetPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceMemoryBudgetPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_get_bytes(d, s->heapBudget, sizeof(s->heapBudget));
    mvk_get_bytes(d, s->heapUsage, sizeof(s->heapUsage));
}

void mvk_out_VkPhysicalDeviceMemoryPriorityFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMemoryPriorityFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->memoryPriority = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT(MvkDecoder *d, VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pageableDeviceLocalMemory = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceBufferDeviceAddressFeatures(MvkDecoder *d, VkPhysicalDeviceBufferDeviceAddressFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->bufferDeviceAddress = (VkBool32)mvk_get_u32(d);
    s->bufferDeviceAddressCaptureReplay = (VkBool32)mvk_get_u32(d);
    s->bufferDeviceAddressMultiDevice = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceBufferDeviceAddressFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->bufferDeviceAddress = (VkBool32)mvk_get_u32(d);
    s->bufferDeviceAddressCaptureReplay = (VkBool32)mvk_get_u32(d);
    s->bufferDeviceAddressMultiDevice = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkFilterCubicImageViewImageFormatPropertiesEXT(MvkDecoder *d, VkFilterCubicImageViewImageFormatPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->filterCubic = (VkBool32)mvk_get_u32(d);
    s->filterCubicMinmax = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceImagelessFramebufferFeatures(MvkDecoder *d, VkPhysicalDeviceImagelessFramebufferFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->imagelessFramebuffer = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceTextureCompressionASTCHDRFeatures(MvkDecoder *d, VkPhysicalDeviceTextureCompressionASTCHDRFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->textureCompressionASTC_HDR = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCooperativeMatrixFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cooperativeMatrix = (VkBool32)mvk_get_u32(d);
    s->cooperativeMatrixRobustBufferAccess = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCooperativeMatrixPropertiesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cooperativeMatrixSupportedStages = (VkShaderStageFlags)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceYcbcrImageArraysFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->ycbcrImageArrays = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPipelineCreationFeedback(MvkDecoder *d, VkPipelineCreationFeedback *s)
{
    s->flags = (VkPipelineCreationFeedbackFlags)mvk_get_u32(d);
    s->duration = (uint64_t)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDevicePerformanceQueryFeaturesKHR(MvkDecoder *d, VkPhysicalDevicePerformanceQueryFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->performanceCounterQueryPools = (VkBool32)mvk_get_u32(d);
    s->performanceCounterMultipleQueryPools = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePerformanceQueryPropertiesKHR(MvkDecoder *d, VkPhysicalDevicePerformanceQueryPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->allowCommandBufferQueryCopies = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCoverageReductionModeFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCoverageReductionModeFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->coverageReductionMode = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL(MvkDecoder *d, VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderIntegerFunctions2 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderClockFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderClockFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderSubgroupClock = (VkBool32)mvk_get_u32(d);
    s->shaderDeviceClock = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceIndexTypeUint8Features(MvkDecoder *d, VkPhysicalDeviceIndexTypeUint8Features *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->indexTypeUint8 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderSMBuiltinsPropertiesNV(MvkDecoder *d, VkPhysicalDeviceShaderSMBuiltinsPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderSMCount = (uint32_t)mvk_get_u32(d);
    s->shaderWarpsPerSM = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderSMBuiltinsFeaturesNV(MvkDecoder *d, VkPhysicalDeviceShaderSMBuiltinsFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderSMBuiltins = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->fragmentShaderSampleInterlock = (VkBool32)mvk_get_u32(d);
    s->fragmentShaderPixelInterlock = (VkBool32)mvk_get_u32(d);
    s->fragmentShaderShadingRateInterlock = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures(MvkDecoder *d, VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->separateDepthStencilLayouts = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT(MvkDecoder *d, VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->primitiveTopologyListRestart = (VkBool32)mvk_get_u32(d);
    s->primitiveTopologyPatchListRestart = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR(MvkDecoder *d, VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pipelineExecutableInfo = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures(MvkDecoder *d, VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderDemoteToHelperInvocation = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->texelBufferAlignment = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceTexelBufferAlignmentProperties(MvkDecoder *d, VkPhysicalDeviceTexelBufferAlignmentProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->storageTexelBufferOffsetAlignmentBytes = (VkDeviceSize)mvk_get_u64(d);
    s->storageTexelBufferOffsetSingleTexelAlignment = (VkBool32)mvk_get_u32(d);
    s->uniformTexelBufferOffsetAlignmentBytes = (VkDeviceSize)mvk_get_u64(d);
    s->uniformTexelBufferOffsetSingleTexelAlignment = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceSubgroupSizeControlFeatures(MvkDecoder *d, VkPhysicalDeviceSubgroupSizeControlFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->subgroupSizeControl = (VkBool32)mvk_get_u32(d);
    s->computeFullSubgroups = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceSubgroupSizeControlProperties(MvkDecoder *d, VkPhysicalDeviceSubgroupSizeControlProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->minSubgroupSize = (uint32_t)mvk_get_u32(d);
    s->maxSubgroupSize = (uint32_t)mvk_get_u32(d);
    s->maxComputeWorkgroupSubgroups = (uint32_t)mvk_get_u32(d);
    s->requiredSubgroupSizeStages = (VkShaderStageFlags)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceSubpassShadingPropertiesHUAWEI(MvkDecoder *d, VkPhysicalDeviceSubpassShadingPropertiesHUAWEI *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxSubpassShadingWorkgroupSizeAspectRatio = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI(MvkDecoder *d, VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_get_bytes(d, s->maxWorkGroupCount, sizeof(s->maxWorkGroupCount));
    mvk_get_bytes(d, s->maxWorkGroupSize, sizeof(s->maxWorkGroupSize));
    s->maxOutputClusterCount = (uint32_t)mvk_get_u32(d);
    s->indirectBufferOffsetAlignment = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceLineRasterizationFeatures(MvkDecoder *d, VkPhysicalDeviceLineRasterizationFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rectangularLines = (VkBool32)mvk_get_u32(d);
    s->bresenhamLines = (VkBool32)mvk_get_u32(d);
    s->smoothLines = (VkBool32)mvk_get_u32(d);
    s->stippledRectangularLines = (VkBool32)mvk_get_u32(d);
    s->stippledBresenhamLines = (VkBool32)mvk_get_u32(d);
    s->stippledSmoothLines = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceLineRasterizationProperties(MvkDecoder *d, VkPhysicalDeviceLineRasterizationProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->lineSubPixelPrecisionBits = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePipelineCreationCacheControlFeatures(MvkDecoder *d, VkPhysicalDevicePipelineCreationCacheControlFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pipelineCreationCacheControl = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceVulkan11Features(MvkDecoder *d, VkPhysicalDeviceVulkan11Features *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->storageBuffer16BitAccess = (VkBool32)mvk_get_u32(d);
    s->uniformAndStorageBuffer16BitAccess = (VkBool32)mvk_get_u32(d);
    s->storagePushConstant16 = (VkBool32)mvk_get_u32(d);
    s->storageInputOutput16 = (VkBool32)mvk_get_u32(d);
    s->multiview = (VkBool32)mvk_get_u32(d);
    s->multiviewGeometryShader = (VkBool32)mvk_get_u32(d);
    s->multiviewTessellationShader = (VkBool32)mvk_get_u32(d);
    s->variablePointersStorageBuffer = (VkBool32)mvk_get_u32(d);
    s->variablePointers = (VkBool32)mvk_get_u32(d);
    s->protectedMemory = (VkBool32)mvk_get_u32(d);
    s->samplerYcbcrConversion = (VkBool32)mvk_get_u32(d);
    s->shaderDrawParameters = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceVulkan11Properties(MvkDecoder *d, VkPhysicalDeviceVulkan11Properties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_get_bytes(d, s->deviceUUID, sizeof(s->deviceUUID));
    mvk_get_bytes(d, s->driverUUID, sizeof(s->driverUUID));
    mvk_get_bytes(d, s->deviceLUID, sizeof(s->deviceLUID));
    s->deviceNodeMask = (uint32_t)mvk_get_u32(d);
    s->deviceLUIDValid = (VkBool32)mvk_get_u32(d);
    s->subgroupSize = (uint32_t)mvk_get_u32(d);
    s->subgroupSupportedStages = (VkShaderStageFlags)mvk_get_u32(d);
    s->subgroupSupportedOperations = (VkSubgroupFeatureFlags)mvk_get_u32(d);
    s->subgroupQuadOperationsInAllStages = (VkBool32)mvk_get_u32(d);
    s->pointClippingBehavior = (VkPointClippingBehavior)mvk_get_u32(d);
    s->maxMultiviewViewCount = (uint32_t)mvk_get_u32(d);
    s->maxMultiviewInstanceIndex = (uint32_t)mvk_get_u32(d);
    s->protectedNoFault = (VkBool32)mvk_get_u32(d);
    s->maxPerSetDescriptors = (uint32_t)mvk_get_u32(d);
    s->maxMemoryAllocationSize = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceVulkan12Features(MvkDecoder *d, VkPhysicalDeviceVulkan12Features *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->samplerMirrorClampToEdge = (VkBool32)mvk_get_u32(d);
    s->drawIndirectCount = (VkBool32)mvk_get_u32(d);
    s->storageBuffer8BitAccess = (VkBool32)mvk_get_u32(d);
    s->uniformAndStorageBuffer8BitAccess = (VkBool32)mvk_get_u32(d);
    s->storagePushConstant8 = (VkBool32)mvk_get_u32(d);
    s->shaderBufferInt64Atomics = (VkBool32)mvk_get_u32(d);
    s->shaderSharedInt64Atomics = (VkBool32)mvk_get_u32(d);
    s->shaderFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderInt8 = (VkBool32)mvk_get_u32(d);
    s->descriptorIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderInputAttachmentArrayDynamicIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderUniformTexelBufferArrayDynamicIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderStorageTexelBufferArrayDynamicIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderUniformBufferArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderSampledImageArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderStorageBufferArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderStorageImageArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderInputAttachmentArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderUniformTexelBufferArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->shaderStorageTexelBufferArrayNonUniformIndexing = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingUniformBufferUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingSampledImageUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingStorageImageUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingStorageBufferUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingUniformTexelBufferUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingStorageTexelBufferUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingUpdateUnusedWhilePending = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingPartiallyBound = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingVariableDescriptorCount = (VkBool32)mvk_get_u32(d);
    s->runtimeDescriptorArray = (VkBool32)mvk_get_u32(d);
    s->samplerFilterMinmax = (VkBool32)mvk_get_u32(d);
    s->scalarBlockLayout = (VkBool32)mvk_get_u32(d);
    s->imagelessFramebuffer = (VkBool32)mvk_get_u32(d);
    s->uniformBufferStandardLayout = (VkBool32)mvk_get_u32(d);
    s->shaderSubgroupExtendedTypes = (VkBool32)mvk_get_u32(d);
    s->separateDepthStencilLayouts = (VkBool32)mvk_get_u32(d);
    s->hostQueryReset = (VkBool32)mvk_get_u32(d);
    s->timelineSemaphore = (VkBool32)mvk_get_u32(d);
    s->bufferDeviceAddress = (VkBool32)mvk_get_u32(d);
    s->bufferDeviceAddressCaptureReplay = (VkBool32)mvk_get_u32(d);
    s->bufferDeviceAddressMultiDevice = (VkBool32)mvk_get_u32(d);
    s->vulkanMemoryModel = (VkBool32)mvk_get_u32(d);
    s->vulkanMemoryModelDeviceScope = (VkBool32)mvk_get_u32(d);
    s->vulkanMemoryModelAvailabilityVisibilityChains = (VkBool32)mvk_get_u32(d);
    s->shaderOutputViewportIndex = (VkBool32)mvk_get_u32(d);
    s->shaderOutputLayer = (VkBool32)mvk_get_u32(d);
    s->subgroupBroadcastDynamicId = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceVulkan12Properties(MvkDecoder *d, VkPhysicalDeviceVulkan12Properties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->driverID = (VkDriverId)mvk_get_u32(d);
    mvk_get_bytes(d, s->driverName, sizeof(s->driverName));
    mvk_get_bytes(d, s->driverInfo, sizeof(s->driverInfo));
    mvk_out_VkConformanceVersion(d, &s->conformanceVersion);
    s->denormBehaviorIndependence = (VkShaderFloatControlsIndependence)mvk_get_u32(d);
    s->roundingModeIndependence = (VkShaderFloatControlsIndependence)mvk_get_u32(d);
    s->shaderSignedZeroInfNanPreserveFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderSignedZeroInfNanPreserveFloat32 = (VkBool32)mvk_get_u32(d);
    s->shaderSignedZeroInfNanPreserveFloat64 = (VkBool32)mvk_get_u32(d);
    s->shaderDenormPreserveFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderDenormPreserveFloat32 = (VkBool32)mvk_get_u32(d);
    s->shaderDenormPreserveFloat64 = (VkBool32)mvk_get_u32(d);
    s->shaderDenormFlushToZeroFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderDenormFlushToZeroFloat32 = (VkBool32)mvk_get_u32(d);
    s->shaderDenormFlushToZeroFloat64 = (VkBool32)mvk_get_u32(d);
    s->shaderRoundingModeRTEFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderRoundingModeRTEFloat32 = (VkBool32)mvk_get_u32(d);
    s->shaderRoundingModeRTEFloat64 = (VkBool32)mvk_get_u32(d);
    s->shaderRoundingModeRTZFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderRoundingModeRTZFloat32 = (VkBool32)mvk_get_u32(d);
    s->shaderRoundingModeRTZFloat64 = (VkBool32)mvk_get_u32(d);
    s->maxUpdateAfterBindDescriptorsInAllPools = (uint32_t)mvk_get_u32(d);
    s->shaderUniformBufferArrayNonUniformIndexingNative = (VkBool32)mvk_get_u32(d);
    s->shaderSampledImageArrayNonUniformIndexingNative = (VkBool32)mvk_get_u32(d);
    s->shaderStorageBufferArrayNonUniformIndexingNative = (VkBool32)mvk_get_u32(d);
    s->shaderStorageImageArrayNonUniformIndexingNative = (VkBool32)mvk_get_u32(d);
    s->shaderInputAttachmentArrayNonUniformIndexingNative = (VkBool32)mvk_get_u32(d);
    s->robustBufferAccessUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->quadDivergentImplicitLod = (VkBool32)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindSamplers = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindUniformBuffers = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindStorageBuffers = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindSampledImages = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindStorageImages = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindInputAttachments = (uint32_t)mvk_get_u32(d);
    s->maxPerStageUpdateAfterBindResources = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindSamplers = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindUniformBuffers = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindStorageBuffers = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindSampledImages = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindStorageImages = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindInputAttachments = (uint32_t)mvk_get_u32(d);
    s->supportedDepthResolveModes = (VkResolveModeFlags)mvk_get_u32(d);
    s->supportedStencilResolveModes = (VkResolveModeFlags)mvk_get_u32(d);
    s->independentResolveNone = (VkBool32)mvk_get_u32(d);
    s->independentResolve = (VkBool32)mvk_get_u32(d);
    s->filterMinmaxSingleComponentFormats = (VkBool32)mvk_get_u32(d);
    s->filterMinmaxImageComponentMapping = (VkBool32)mvk_get_u32(d);
    s->maxTimelineSemaphoreValueDifference = (uint64_t)mvk_get_u64(d);
    s->framebufferIntegerColorSampleCounts = (VkSampleCountFlags)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceVulkan13Features(MvkDecoder *d, VkPhysicalDeviceVulkan13Features *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->robustImageAccess = (VkBool32)mvk_get_u32(d);
    s->inlineUniformBlock = (VkBool32)mvk_get_u32(d);
    s->descriptorBindingInlineUniformBlockUpdateAfterBind = (VkBool32)mvk_get_u32(d);
    s->pipelineCreationCacheControl = (VkBool32)mvk_get_u32(d);
    s->privateData = (VkBool32)mvk_get_u32(d);
    s->shaderDemoteToHelperInvocation = (VkBool32)mvk_get_u32(d);
    s->shaderTerminateInvocation = (VkBool32)mvk_get_u32(d);
    s->subgroupSizeControl = (VkBool32)mvk_get_u32(d);
    s->computeFullSubgroups = (VkBool32)mvk_get_u32(d);
    s->synchronization2 = (VkBool32)mvk_get_u32(d);
    s->textureCompressionASTC_HDR = (VkBool32)mvk_get_u32(d);
    s->shaderZeroInitializeWorkgroupMemory = (VkBool32)mvk_get_u32(d);
    s->dynamicRendering = (VkBool32)mvk_get_u32(d);
    s->shaderIntegerDotProduct = (VkBool32)mvk_get_u32(d);
    s->maintenance4 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceVulkan13Properties(MvkDecoder *d, VkPhysicalDeviceVulkan13Properties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->minSubgroupSize = (uint32_t)mvk_get_u32(d);
    s->maxSubgroupSize = (uint32_t)mvk_get_u32(d);
    s->maxComputeWorkgroupSubgroups = (uint32_t)mvk_get_u32(d);
    s->requiredSubgroupSizeStages = (VkShaderStageFlags)mvk_get_u32(d);
    s->maxInlineUniformBlockSize = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorInlineUniformBlocks = (uint32_t)mvk_get_u32(d);
    s->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetInlineUniformBlocks = (uint32_t)mvk_get_u32(d);
    s->maxDescriptorSetUpdateAfterBindInlineUniformBlocks = (uint32_t)mvk_get_u32(d);
    s->maxInlineUniformTotalSize = (uint32_t)mvk_get_u32(d);
    s->integerDotProduct8BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct8BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct8BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct4x8BitPackedUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct4x8BitPackedSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct4x8BitPackedMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct16BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct16BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct16BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct32BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct32BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct32BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct64BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct64BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct64BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating8BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating8BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating16BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating16BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating32BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating32BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating64BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating64BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->storageTexelBufferOffsetAlignmentBytes = (VkDeviceSize)mvk_get_u64(d);
    s->storageTexelBufferOffsetSingleTexelAlignment = (VkBool32)mvk_get_u32(d);
    s->uniformTexelBufferOffsetAlignmentBytes = (VkDeviceSize)mvk_get_u64(d);
    s->uniformTexelBufferOffsetSingleTexelAlignment = (VkBool32)mvk_get_u32(d);
    s->maxBufferSize = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceVulkan14Features(MvkDecoder *d, VkPhysicalDeviceVulkan14Features *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->globalPriorityQuery = (VkBool32)mvk_get_u32(d);
    s->shaderSubgroupRotate = (VkBool32)mvk_get_u32(d);
    s->shaderSubgroupRotateClustered = (VkBool32)mvk_get_u32(d);
    s->shaderFloatControls2 = (VkBool32)mvk_get_u32(d);
    s->shaderExpectAssume = (VkBool32)mvk_get_u32(d);
    s->rectangularLines = (VkBool32)mvk_get_u32(d);
    s->bresenhamLines = (VkBool32)mvk_get_u32(d);
    s->smoothLines = (VkBool32)mvk_get_u32(d);
    s->stippledRectangularLines = (VkBool32)mvk_get_u32(d);
    s->stippledBresenhamLines = (VkBool32)mvk_get_u32(d);
    s->stippledSmoothLines = (VkBool32)mvk_get_u32(d);
    s->vertexAttributeInstanceRateDivisor = (VkBool32)mvk_get_u32(d);
    s->vertexAttributeInstanceRateZeroDivisor = (VkBool32)mvk_get_u32(d);
    s->indexTypeUint8 = (VkBool32)mvk_get_u32(d);
    s->dynamicRenderingLocalRead = (VkBool32)mvk_get_u32(d);
    s->maintenance5 = (VkBool32)mvk_get_u32(d);
    s->maintenance6 = (VkBool32)mvk_get_u32(d);
    s->pipelineProtectedAccess = (VkBool32)mvk_get_u32(d);
    s->pipelineRobustness = (VkBool32)mvk_get_u32(d);
    s->hostImageCopy = (VkBool32)mvk_get_u32(d);
    s->pushDescriptor = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceVulkan14Properties(MvkDecoder *d, VkPhysicalDeviceVulkan14Properties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->lineSubPixelPrecisionBits = (uint32_t)mvk_get_u32(d);
    s->maxVertexAttribDivisor = (uint32_t)mvk_get_u32(d);
    s->supportsNonZeroFirstInstance = (VkBool32)mvk_get_u32(d);
    s->maxPushDescriptors = (uint32_t)mvk_get_u32(d);
    s->dynamicRenderingLocalReadDepthStencilAttachments = (VkBool32)mvk_get_u32(d);
    s->dynamicRenderingLocalReadMultisampledAttachments = (VkBool32)mvk_get_u32(d);
    s->earlyFragmentMultisampleCoverageAfterSampleCounting = (VkBool32)mvk_get_u32(d);
    s->earlyFragmentSampleMaskTestBeforeSampleCounting = (VkBool32)mvk_get_u32(d);
    s->depthStencilSwizzleOneSupport = (VkBool32)mvk_get_u32(d);
    s->polygonModePointSize = (VkBool32)mvk_get_u32(d);
    s->nonStrictSinglePixelWideLinesUseParallelogram = (VkBool32)mvk_get_u32(d);
    s->nonStrictWideLinesUseParallelogram = (VkBool32)mvk_get_u32(d);
    s->blockTexelViewCompatibleMultipleLayers = (VkBool32)mvk_get_u32(d);
    s->maxCombinedImageSamplerDescriptorCount = (uint32_t)mvk_get_u32(d);
    s->fragmentShadingRateClampCombinerInputs = (VkBool32)mvk_get_u32(d);
    s->defaultRobustnessStorageBuffers = (VkPipelineRobustnessBufferBehavior)mvk_get_u32(d);
    s->defaultRobustnessUniformBuffers = (VkPipelineRobustnessBufferBehavior)mvk_get_u32(d);
    s->defaultRobustnessVertexInputs = (VkPipelineRobustnessBufferBehavior)mvk_get_u32(d);
    s->defaultRobustnessImages = (VkPipelineRobustnessImageBehavior)mvk_get_u32(d);
    s->copySrcLayoutCount = (uint32_t)mvk_get_u32(d);
    mvk_skip_pointer(d);
    s->copyDstLayoutCount = (uint32_t)mvk_get_u32(d);
    mvk_skip_pointer(d);
    mvk_get_bytes(d, s->optimalTilingLayoutUUID, sizeof(s->optimalTilingLayoutUUID));
    s->identicalMemoryTypeRequirements = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCoherentMemoryFeaturesAMD(MvkDecoder *d, VkPhysicalDeviceCoherentMemoryFeaturesAMD *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->deviceCoherentMemory = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceGpaFeaturesAMD(MvkDecoder *d, VkPhysicalDeviceGpaFeaturesAMD *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->perfCounters = (VkBool32)mvk_get_u32(d);
    s->streamingPerfCounters = (VkBool32)mvk_get_u32(d);
    s->sqThreadTracing = (VkBool32)mvk_get_u32(d);
    s->clockModes = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceGpaPropertiesAMD(MvkDecoder *d, VkPhysicalDeviceGpaPropertiesAMD *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->flags = (VkPhysicalDeviceGpaPropertiesFlagsAMD)mvk_get_u32(d);
    s->maxSqttSeBufferSize = (VkDeviceSize)mvk_get_u64(d);
    s->shaderEngineCount = (uint32_t)mvk_get_u32(d);
    s->perfBlockCount = (uint32_t)mvk_get_u32(d);
    mvk_skip_pointer(d);
}

void mvk_out_VkPhysicalDeviceGpaProperties2AMD(MvkDecoder *d, VkPhysicalDeviceGpaProperties2AMD *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->revisionId = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCustomBorderColorPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceCustomBorderColorPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxCustomBorderColorSamplers = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCustomBorderColorFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceCustomBorderColorFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->customBorderColors = (VkBool32)mvk_get_u32(d);
    s->customBorderColorWithoutFormat = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceBorderColorSwizzleFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->borderColorSwizzle = (VkBool32)mvk_get_u32(d);
    s->borderColorSwizzleFromImage = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceExtendedDynamicStateFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->extendedDynamicState = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceExtendedDynamicState2FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->extendedDynamicState2 = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState2LogicOp = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState2PatchControlPoints = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceExtendedDynamicState3FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->extendedDynamicState3TessellationDomainOrigin = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3DepthClampEnable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3PolygonMode = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3RasterizationSamples = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3SampleMask = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3AlphaToCoverageEnable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3AlphaToOneEnable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3LogicOpEnable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3ColorBlendEnable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3ColorBlendEquation = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3ColorWriteMask = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3RasterizationStream = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3ConservativeRasterizationMode = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3ExtraPrimitiveOverestimationSize = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3DepthClipEnable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3SampleLocationsEnable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3ColorBlendAdvanced = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3ProvokingVertexMode = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3LineRasterizationMode = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3LineStippleEnable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3DepthClipNegativeOneToOne = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3ViewportWScalingEnable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3ViewportSwizzle = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3CoverageToColorEnable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3CoverageToColorLocation = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3CoverageModulationMode = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3CoverageModulationTableEnable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3CoverageModulationTable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3CoverageReductionMode = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3RepresentativeFragmentTestEnable = (VkBool32)mvk_get_u32(d);
    s->extendedDynamicState3ShadingRateImageEnable = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceExtendedDynamicState3PropertiesEXT(MvkDecoder *d, VkPhysicalDeviceExtendedDynamicState3PropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->dynamicPrimitiveTopologyUnrestricted = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceExtendedFlagsFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceExtendedFlagsFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->extendedFlags = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV(MvkDecoder *d, VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->partitionedAccelerationStructure = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV(MvkDecoder *d, VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxPartitionCount = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDiagnosticsConfigFeaturesNV(MvkDecoder *d, VkPhysicalDeviceDiagnosticsConfigFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->diagnosticsConfig = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures(MvkDecoder *d, VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderZeroInitializeWorkgroupMemory = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderSubgroupUniformControlFlow = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRobustness2FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceRobustness2FeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->robustBufferAccess2 = (VkBool32)mvk_get_u32(d);
    s->robustImageAccess2 = (VkBool32)mvk_get_u32(d);
    s->nullDescriptor = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRobustness2PropertiesKHR(MvkDecoder *d, VkPhysicalDeviceRobustness2PropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->robustStorageBufferAccessSizeAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->robustUniformBufferAccessSizeAlignment = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceImageRobustnessFeatures(MvkDecoder *d, VkPhysicalDeviceImageRobustnessFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->robustImageAccess = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->workgroupMemoryExplicitLayout = (VkBool32)mvk_get_u32(d);
    s->workgroupMemoryExplicitLayoutScalarBlockLayout = (VkBool32)mvk_get_u32(d);
    s->workgroupMemoryExplicitLayout8BitAccess = (VkBool32)mvk_get_u32(d);
    s->workgroupMemoryExplicitLayout16BitAccess = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevice4444FormatsFeaturesEXT(MvkDecoder *d, VkPhysicalDevice4444FormatsFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->formatA4R4G4B4 = (VkBool32)mvk_get_u32(d);
    s->formatA4B4G4R4 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceSubpassShadingFeaturesHUAWEI(MvkDecoder *d, VkPhysicalDeviceSubpassShadingFeaturesHUAWEI *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->subpassShading = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI(MvkDecoder *d, VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->clustercullingShader = (VkBool32)mvk_get_u32(d);
    s->multiviewClusterCullingShader = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI(MvkDecoder *d, VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->clusterShadingRate = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderImageInt64Atomics = (VkBool32)mvk_get_u32(d);
    s->sparseImageInt64Atomics = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentShadingRateFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceFragmentShadingRateFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pipelineFragmentShadingRate = (VkBool32)mvk_get_u32(d);
    s->primitiveFragmentShadingRate = (VkBool32)mvk_get_u32(d);
    s->attachmentFragmentShadingRate = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentShadingRatePropertiesKHR(MvkDecoder *d, VkPhysicalDeviceFragmentShadingRatePropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkExtent2D(d, &s->minFragmentShadingRateAttachmentTexelSize);
    mvk_out_VkExtent2D(d, &s->maxFragmentShadingRateAttachmentTexelSize);
    s->maxFragmentShadingRateAttachmentTexelSizeAspectRatio = (uint32_t)mvk_get_u32(d);
    s->primitiveFragmentShadingRateWithMultipleViewports = (VkBool32)mvk_get_u32(d);
    s->layeredShadingRateAttachments = (VkBool32)mvk_get_u32(d);
    s->fragmentShadingRateNonTrivialCombinerOps = (VkBool32)mvk_get_u32(d);
    mvk_out_VkExtent2D(d, &s->maxFragmentSize);
    s->maxFragmentSizeAspectRatio = (uint32_t)mvk_get_u32(d);
    s->maxFragmentShadingRateCoverageSamples = (uint32_t)mvk_get_u32(d);
    s->maxFragmentShadingRateRasterizationSamples = (VkSampleCountFlagBits)mvk_get_u32(d);
    s->fragmentShadingRateWithShaderDepthStencilWrites = (VkBool32)mvk_get_u32(d);
    s->fragmentShadingRateWithSampleMask = (VkBool32)mvk_get_u32(d);
    s->fragmentShadingRateWithShaderSampleMask = (VkBool32)mvk_get_u32(d);
    s->fragmentShadingRateWithConservativeRasterization = (VkBool32)mvk_get_u32(d);
    s->fragmentShadingRateWithFragmentShaderInterlock = (VkBool32)mvk_get_u32(d);
    s->fragmentShadingRateWithCustomSampleLocations = (VkBool32)mvk_get_u32(d);
    s->fragmentShadingRateStrictMultiplyCombiner = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderTerminateInvocationFeatures(MvkDecoder *d, VkPhysicalDeviceShaderTerminateInvocationFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderTerminateInvocation = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV(MvkDecoder *d, VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->fragmentShadingRateEnums = (VkBool32)mvk_get_u32(d);
    s->supersampleFragmentShadingRates = (VkBool32)mvk_get_u32(d);
    s->noInvocationFragmentShadingRates = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV(MvkDecoder *d, VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxFragmentShadingRateInvocationCount = (VkSampleCountFlagBits)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceImage2DViewOf3DFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceImage2DViewOf3DFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->image2DViewOf3D = (VkBool32)mvk_get_u32(d);
    s->sampler2DViewOf3D = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->imageSlicedViewOf3D = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->attachmentFeedbackLoopDynamicState = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->legacyVertexAttributes = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->nativeUnalignedPerformance = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->mutableDescriptorType = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDepthClipControlFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDepthClipControlFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->depthClipControl = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->zeroInitializeDeviceMemory = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCustomResolveFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceCustomResolveFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->customResolve = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->deviceGeneratedCommands = (VkBool32)mvk_get_u32(d);
    s->dynamicGeneratedPipelineLayout = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxIndirectPipelineCount = (uint32_t)mvk_get_u32(d);
    s->maxIndirectShaderObjectCount = (uint32_t)mvk_get_u32(d);
    s->maxIndirectSequenceCount = (uint32_t)mvk_get_u32(d);
    s->maxIndirectCommandsTokenCount = (uint32_t)mvk_get_u32(d);
    s->maxIndirectCommandsTokenOffset = (uint32_t)mvk_get_u32(d);
    s->maxIndirectCommandsIndirectStride = (uint32_t)mvk_get_u32(d);
    s->supportedIndirectCommandsInputModes = (VkIndirectCommandsInputModeFlagsEXT)mvk_get_u32(d);
    s->supportedIndirectCommandsShaderStages = (VkShaderStageFlags)mvk_get_u32(d);
    s->supportedIndirectCommandsShaderStagesPipelineBinding = (VkShaderStageFlags)mvk_get_u32(d);
    s->supportedIndirectCommandsShaderStagesShaderBinding = (VkShaderStageFlags)mvk_get_u32(d);
    s->deviceGeneratedCommandsTransformFeedback = (VkBool32)mvk_get_u32(d);
    s->deviceGeneratedCommandsMultiDrawIndirectCount = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDepthClampControlFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDepthClampControlFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->depthClampControl = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->vertexInputDynamicState = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceExternalMemoryRDMAFeaturesNV(MvkDecoder *d, VkPhysicalDeviceExternalMemoryRDMAFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->externalMemoryRDMA = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderRelaxedExtendedInstruction = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceColorWriteEnableFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceColorWriteEnableFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->colorWriteEnable = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkQueueFamilyCheckpointProperties2NV(MvkDecoder *d, VkQueueFamilyCheckpointProperties2NV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->checkpointExecutionStageMask = (VkPipelineStageFlags2)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceSynchronization2Features(MvkDecoder *d, VkPhysicalDeviceSynchronization2Features *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->synchronization2 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->unifiedImageLayouts = (VkBool32)mvk_get_u32(d);
    s->unifiedImageLayoutsVideo = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceHostImageCopyFeatures(MvkDecoder *d, VkPhysicalDeviceHostImageCopyFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->hostImageCopy = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceHostImageCopyProperties(MvkDecoder *d, VkPhysicalDeviceHostImageCopyProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->copySrcLayoutCount = (uint32_t)mvk_get_u32(d);
    mvk_skip_pointer(d);
    s->copyDstLayoutCount = (uint32_t)mvk_get_u32(d);
    mvk_skip_pointer(d);
    mvk_get_bytes(d, s->optimalTilingLayoutUUID, sizeof(s->optimalTilingLayoutUUID));
    s->identicalMemoryTypeRequirements = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkSubresourceHostMemcpySize(MvkDecoder *d, VkSubresourceHostMemcpySize *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->size = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkHostImageCopyDevicePerformanceQuery(MvkDecoder *d, VkHostImageCopyDevicePerformanceQuery *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->optimalDeviceAccess = (VkBool32)mvk_get_u32(d);
    s->identicalMemoryLayout = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT(MvkDecoder *d, VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->primitivesGeneratedQuery = (VkBool32)mvk_get_u32(d);
    s->primitivesGeneratedQueryWithRasterizerDiscard = (VkBool32)mvk_get_u32(d);
    s->primitivesGeneratedQueryWithNonZeroStreams = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceLegacyDitheringFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceLegacyDitheringFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->legacyDithering = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->multisampledRenderToSingleSampled = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkSubpassResolvePerformanceQueryEXT(MvkDecoder *d, VkSubpassResolvePerformanceQueryEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->optimal = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->multisampledRenderToSwapchain = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePipelineProtectedAccessFeatures(MvkDecoder *d, VkPhysicalDevicePipelineProtectedAccessFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pipelineProtectedAccess = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceInheritedViewportScissorFeaturesNV(MvkDecoder *d, VkPhysicalDeviceInheritedViewportScissorFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->inheritedViewportScissor2D = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->ycbcr2plane444Formats = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceProvokingVertexFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceProvokingVertexFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->provokingVertexLast = (VkBool32)mvk_get_u32(d);
    s->transformFeedbackPreservesProvokingVertex = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceProvokingVertexPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceProvokingVertexPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->provokingVertexModePerPipeline = (VkBool32)mvk_get_u32(d);
    s->transformFeedbackPreservesTriangleFanProvokingVertex = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDescriptorBufferFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDescriptorBufferFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->descriptorBuffer = (VkBool32)mvk_get_u32(d);
    s->descriptorBufferCaptureReplay = (VkBool32)mvk_get_u32(d);
    s->descriptorBufferImageLayoutIgnored = (VkBool32)mvk_get_u32(d);
    s->descriptorBufferPushDescriptors = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDescriptorBufferPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceDescriptorBufferPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->combinedImageSamplerDescriptorSingleArray = (VkBool32)mvk_get_u32(d);
    s->bufferlessPushDescriptors = (VkBool32)mvk_get_u32(d);
    s->allowSamplerImageViewPostSubmitCreation = (VkBool32)mvk_get_u32(d);
    s->descriptorBufferOffsetAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->maxDescriptorBufferBindings = (uint32_t)mvk_get_u32(d);
    s->maxResourceDescriptorBufferBindings = (uint32_t)mvk_get_u32(d);
    s->maxSamplerDescriptorBufferBindings = (uint32_t)mvk_get_u32(d);
    s->maxEmbeddedImmutableSamplerBindings = (uint32_t)mvk_get_u32(d);
    s->maxEmbeddedImmutableSamplers = (uint32_t)mvk_get_u32(d);
    s->bufferCaptureReplayDescriptorDataSize = (size_t)mvk_get_u64(d);
    s->imageCaptureReplayDescriptorDataSize = (size_t)mvk_get_u64(d);
    s->imageViewCaptureReplayDescriptorDataSize = (size_t)mvk_get_u64(d);
    s->samplerCaptureReplayDescriptorDataSize = (size_t)mvk_get_u64(d);
    s->accelerationStructureCaptureReplayDescriptorDataSize = (size_t)mvk_get_u64(d);
    s->samplerDescriptorSize = (size_t)mvk_get_u64(d);
    s->combinedImageSamplerDescriptorSize = (size_t)mvk_get_u64(d);
    s->sampledImageDescriptorSize = (size_t)mvk_get_u64(d);
    s->storageImageDescriptorSize = (size_t)mvk_get_u64(d);
    s->uniformTexelBufferDescriptorSize = (size_t)mvk_get_u64(d);
    s->robustUniformTexelBufferDescriptorSize = (size_t)mvk_get_u64(d);
    s->storageTexelBufferDescriptorSize = (size_t)mvk_get_u64(d);
    s->robustStorageTexelBufferDescriptorSize = (size_t)mvk_get_u64(d);
    s->uniformBufferDescriptorSize = (size_t)mvk_get_u64(d);
    s->robustUniformBufferDescriptorSize = (size_t)mvk_get_u64(d);
    s->storageBufferDescriptorSize = (size_t)mvk_get_u64(d);
    s->robustStorageBufferDescriptorSize = (size_t)mvk_get_u64(d);
    s->inputAttachmentDescriptorSize = (size_t)mvk_get_u64(d);
    s->accelerationStructureDescriptorSize = (size_t)mvk_get_u64(d);
    s->maxSamplerDescriptorBufferRange = (VkDeviceSize)mvk_get_u64(d);
    s->maxResourceDescriptorBufferRange = (VkDeviceSize)mvk_get_u64(d);
    s->samplerDescriptorBufferAddressSpaceSize = (VkDeviceSize)mvk_get_u64(d);
    s->resourceDescriptorBufferAddressSpaceSize = (VkDeviceSize)mvk_get_u64(d);
    s->descriptorBufferAddressSpaceSize = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->combinedImageSamplerDensityMapDescriptorSize = (size_t)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceShaderIntegerDotProductFeatures(MvkDecoder *d, VkPhysicalDeviceShaderIntegerDotProductFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderIntegerDotProduct = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderIntegerDotProductProperties(MvkDecoder *d, VkPhysicalDeviceShaderIntegerDotProductProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->integerDotProduct8BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct8BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct8BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct4x8BitPackedUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct4x8BitPackedSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct4x8BitPackedMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct16BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct16BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct16BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct32BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct32BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct32BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct64BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct64BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProduct64BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating8BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating8BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating16BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating16BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating32BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating32BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating64BitUnsignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating64BitSignedAccelerated = (VkBool32)mvk_get_u32(d);
    s->integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDrmPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceDrmPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->hasPrimary = (VkBool32)mvk_get_u32(d);
    s->hasRender = (VkBool32)mvk_get_u32(d);
    s->primaryMajor = (int64_t)mvk_get_u64(d);
    s->primaryMinor = (int64_t)mvk_get_u64(d);
    s->renderMajor = (int64_t)mvk_get_u64(d);
    s->renderMinor = (int64_t)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->fragmentShaderBarycentric = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->triStripVertexOrderIndependentOfProvokingVertex = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderFmaFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderFmaFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderFmaFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderFmaFloat32 = (VkBool32)mvk_get_u32(d);
    s->shaderFmaFloat64 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayTracingMotionBlurFeaturesNV(MvkDecoder *d, VkPhysicalDeviceRayTracingMotionBlurFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rayTracingMotionBlur = (VkBool32)mvk_get_u32(d);
    s->rayTracingMotionBlurPipelineTraceRaysIndirect = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayTracingValidationFeaturesNV(MvkDecoder *d, VkPhysicalDeviceRayTracingValidationFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rayTracingValidation = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV(MvkDecoder *d, VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->spheres = (VkBool32)mvk_get_u32(d);
    s->linearSweptSpheres = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->formatRgba10x6WithoutYCbCrSampler = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkFormatProperties3(MvkDecoder *d, VkFormatProperties3 *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->linearTilingFeatures = (VkFormatFeatureFlags2)mvk_get_u64(d);
    s->optimalTilingFeatures = (VkFormatFeatureFlags2)mvk_get_u64(d);
    s->bufferFeatures = (VkFormatFeatureFlags2)mvk_get_u64(d);
}

void mvk_out_VkFormatProperties4KHR(MvkDecoder *d, VkFormatProperties4KHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->linearTilingFeatures = (VkFormatFeatureFlags4KHR)mvk_get_u64(d);
    s->optimalTilingFeatures = (VkFormatFeatureFlags4KHR)mvk_get_u64(d);
    s->bufferFeatures = (VkFormatFeatureFlags4KHR)mvk_get_u64(d);
}

void mvk_out_VkDrmFormatModifierPropertiesList2EXT(MvkDecoder *d, VkDrmFormatModifierPropertiesList2EXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->drmFormatModifierCount = (uint32_t)mvk_get_u32(d);
    mvk_skip_pointer(d);
}

void mvk_out_VkDrmFormatModifierProperties2EXT(MvkDecoder *d, VkDrmFormatModifierProperties2EXT *s)
{
    s->drmFormatModifier = (uint64_t)mvk_get_u64(d);
    s->drmFormatModifierPlaneCount = (uint32_t)mvk_get_u32(d);
    s->drmFormatModifierTilingFeatures = (VkFormatFeatureFlags2)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceDynamicRenderingFeatures(MvkDecoder *d, VkPhysicalDeviceDynamicRenderingFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->dynamicRendering = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceImageViewMinLodFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceImageViewMinLodFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->minLod = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rasterizationOrderColorAttachmentAccess = (VkBool32)mvk_get_u32(d);
    s->rasterizationOrderDepthAttachmentAccess = (VkBool32)mvk_get_u32(d);
    s->rasterizationOrderStencilAttachmentAccess = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceLinearColorAttachmentFeaturesNV(MvkDecoder *d, VkPhysicalDeviceLinearColorAttachmentFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->linearColorAttachment = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->graphicsPipelineLibrary = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->graphicsPipelineLibraryFastLinking = (VkBool32)mvk_get_u32(d);
    s->graphicsPipelineLibraryIndependentInterpolationDecoration = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkTensorExplicitTilingFormatPropertiesARM(MvkDecoder *d, VkTensorExplicitTilingFormatPropertiesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->brick16TilingTensorFeatures = (VkFormatFeatureFlags2)mvk_get_u64(d);
    s->brick8TilingTensorFeatures = (VkFormatFeatureFlags2)mvk_get_u64(d);
    s->brick4TilingTensorFeatures = (VkFormatFeatureFlags2)mvk_get_u64(d);
    s->blockUTilingTensorFeatures = (VkFormatFeatureFlags2)mvk_get_u64(d);
    s->blockU64kTilingTensorFeatures = (VkFormatFeatureFlags2)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE(MvkDecoder *d, VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->descriptorSetHostMapping = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceNestedCommandBufferFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceNestedCommandBufferFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->nestedCommandBuffer = (VkBool32)mvk_get_u32(d);
    s->nestedCommandBufferRendering = (VkBool32)mvk_get_u32(d);
    s->nestedCommandBufferSimultaneousUse = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceNestedCommandBufferPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceNestedCommandBufferPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxCommandBufferNestingLevel = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderModuleIdentifier = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_get_bytes(d, s->shaderModuleIdentifierAlgorithmUUID, sizeof(s->shaderModuleIdentifierAlgorithmUUID));
}

void mvk_out_VkPhysicalDeviceImageCompressionControlFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceImageCompressionControlFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->imageCompressionControl = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkImageCompressionPropertiesEXT(MvkDecoder *d, VkImageCompressionPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->imageCompressionFlags = (VkImageCompressionFlagsEXT)mvk_get_u32(d);
    s->imageCompressionFixedRateFlags = (VkImageCompressionFixedRateFlagsEXT)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->imageCompressionControlSwapchain = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkRenderPassCreationFeedbackInfoEXT(MvkDecoder *d, VkRenderPassCreationFeedbackInfoEXT *s)
{
    s->postMergeSubpassCount = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkRenderPassSubpassFeedbackInfoEXT(MvkDecoder *d, VkRenderPassSubpassFeedbackInfoEXT *s)
{
    s->subpassMergeStatus = (VkSubpassMergeStatusEXT)mvk_get_u32(d);
    mvk_get_bytes(d, s->description, sizeof(s->description));
    s->postMergeIndex = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->subpassMergeFeedback = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceOpacityMicromapFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceOpacityMicromapFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->micromap = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceOpacityMicromapFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceOpacityMicromapFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->micromap = (VkBool32)mvk_get_u32(d);
    s->micromapCaptureReplay = (VkBool32)mvk_get_u32(d);
    s->micromapHostCommands = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceOpacityMicromapPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceOpacityMicromapPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxOpacity2StateSubdivisionLevel = (uint32_t)mvk_get_u32(d);
    s->maxOpacity4StateSubdivisionLevel = (uint32_t)mvk_get_u32(d);
    s->maxOpacityLossy4StateSubdivisionLevel = (uint32_t)mvk_get_u32(d);
    s->maxMicromapTriangles = (uint64_t)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceOpacityMicromapPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceOpacityMicromapPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxOpacity2StateSubdivisionLevel = (uint32_t)mvk_get_u32(d);
    s->maxOpacity4StateSubdivisionLevel = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePipelinePropertiesFeaturesEXT(MvkDecoder *d, VkPhysicalDevicePipelinePropertiesFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pipelinePropertiesIdentifier = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD(MvkDecoder *d, VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderEarlyAndLateFragmentTests = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->nonSeamlessCubeMap = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePipelineRobustnessFeatures(MvkDecoder *d, VkPhysicalDevicePipelineRobustnessFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pipelineRobustness = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePipelineRobustnessProperties(MvkDecoder *d, VkPhysicalDevicePipelineRobustnessProperties *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->defaultRobustnessStorageBuffers = (VkPipelineRobustnessBufferBehavior)mvk_get_u32(d);
    s->defaultRobustnessUniformBuffers = (VkPipelineRobustnessBufferBehavior)mvk_get_u32(d);
    s->defaultRobustnessVertexInputs = (VkPipelineRobustnessBufferBehavior)mvk_get_u32(d);
    s->defaultRobustnessImages = (VkPipelineRobustnessImageBehavior)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderMultipleWaitQueues = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM(MvkDecoder *d, VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxShaderWaitQueues = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceImageProcessingFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceImageProcessingFeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->textureSampleWeighted = (VkBool32)mvk_get_u32(d);
    s->textureBoxFilter = (VkBool32)mvk_get_u32(d);
    s->textureBlockMatch = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceImageProcessingPropertiesQCOM(MvkDecoder *d, VkPhysicalDeviceImageProcessingPropertiesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxWeightFilterPhases = (uint32_t)mvk_get_u32(d);
    mvk_out_VkExtent2D(d, &s->maxWeightFilterDimension);
    mvk_out_VkExtent2D(d, &s->maxBlockMatchRegion);
    mvk_out_VkExtent2D(d, &s->maxBoxFilterBlockSize);
}

void mvk_out_VkPhysicalDeviceAmigoProfilingFeaturesSEC(MvkDecoder *d, VkPhysicalDeviceAmigoProfilingFeaturesSEC *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->amigoProfiling = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->attachmentFeedbackLoopLayout = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceAddressBindingReportFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceAddressBindingReportFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->reportAddressBinding = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceOpticalFlowFeaturesNV(MvkDecoder *d, VkPhysicalDeviceOpticalFlowFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->opticalFlow = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceOpticalFlowPropertiesNV(MvkDecoder *d, VkPhysicalDeviceOpticalFlowPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->supportedOutputGridSizes = (VkOpticalFlowGridSizeFlagsNV)mvk_get_u32(d);
    s->supportedHintGridSizes = (VkOpticalFlowGridSizeFlagsNV)mvk_get_u32(d);
    s->hintSupported = (VkBool32)mvk_get_u32(d);
    s->costSupported = (VkBool32)mvk_get_u32(d);
    s->bidirectionalFlowSupported = (VkBool32)mvk_get_u32(d);
    s->globalFlowSupported = (VkBool32)mvk_get_u32(d);
    s->minWidth = (uint32_t)mvk_get_u32(d);
    s->minHeight = (uint32_t)mvk_get_u32(d);
    s->maxWidth = (uint32_t)mvk_get_u32(d);
    s->maxHeight = (uint32_t)mvk_get_u32(d);
    s->maxNumRegionsOfInterest = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFaultFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceFaultFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->deviceFault = (VkBool32)mvk_get_u32(d);
    s->deviceFaultVendorBinary = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFaultFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceFaultFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->deviceFault = (VkBool32)mvk_get_u32(d);
    s->deviceFaultVendorBinary = (VkBool32)mvk_get_u32(d);
    s->deviceFaultReportMasked = (VkBool32)mvk_get_u32(d);
    s->deviceFaultDeviceLostOnMasked = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFaultPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceFaultPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxDeviceFaultCount = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR(MvkDecoder *d, VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pipelineLibraryGroupHandles = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM(MvkDecoder *d, VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderCoreMask = (uint64_t)mvk_get_u64(d);
    s->shaderCoreCount = (uint32_t)mvk_get_u32(d);
    s->shaderWarpsPerCore = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM(MvkDecoder *d, VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderCoreBuiltins = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFrameBoundaryFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceFrameBoundaryFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->frameBoundary = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->dynamicRenderingUnusedAttachments = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->internallySynchronizedQueues = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDepthBiasControlFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDepthBiasControlFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->depthBiasControl = (VkBool32)mvk_get_u32(d);
    s->leastRepresentableValueForceUnormRepresentation = (VkBool32)mvk_get_u32(d);
    s->floatRepresentation = (VkBool32)mvk_get_u32(d);
    s->depthBiasExact = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rayTracingInvocationReorder = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV(MvkDecoder *d, VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rayTracingInvocationReorder = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rayTracingInvocationReorderReorderingHint = (VkRayTracingInvocationReorderModeEXT)mvk_get_u32(d);
    s->maxShaderBindingTableRecordIndex = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV(MvkDecoder *d, VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rayTracingInvocationReorderReorderingHint = (VkRayTracingInvocationReorderModeEXT)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV(MvkDecoder *d, VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->extendedSparseAddressSpace = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV(MvkDecoder *d, VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->extendedSparseAddressSpaceSize = (VkDeviceSize)mvk_get_u64(d);
    s->extendedSparseImageUsageFlags = (VkImageUsageFlags)mvk_get_u32(d);
    s->extendedSparseBufferUsageFlags = (VkBufferUsageFlags)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->multiviewPerViewViewports = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->rayTracingPositionFetch = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderCorePropertiesARM(MvkDecoder *d, VkPhysicalDeviceShaderCorePropertiesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pixelRate = (uint32_t)mvk_get_u32(d);
    s->texelRate = (uint32_t)mvk_get_u32(d);
    s->fmaRate = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->multiviewPerViewRenderAreas = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderObjectFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderObjectFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderObject = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderObjectPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceShaderObjectPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_get_bytes(d, s->shaderBinaryUUID, sizeof(s->shaderBinaryUUID));
    s->shaderBinaryVersion = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderTileImageFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderTileImageFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderTileImageColorReadAccess = (VkBool32)mvk_get_u32(d);
    s->shaderTileImageDepthReadAccess = (VkBool32)mvk_get_u32(d);
    s->shaderTileImageStencilReadAccess = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderTileImagePropertiesEXT(MvkDecoder *d, VkPhysicalDeviceShaderTileImagePropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderTileImageCoherentReadAccelerated = (VkBool32)mvk_get_u32(d);
    s->shaderTileImageReadSampleFromPixelRateInvocation = (VkBool32)mvk_get_u32(d);
    s->shaderTileImageReadFromHelperInvocation = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCooperativeMatrixFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cooperativeMatrix = (VkBool32)mvk_get_u32(d);
    s->cooperativeMatrixRobustBufferAccess = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCooperativeMatrixPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cooperativeMatrixSupportedStages = (VkShaderStageFlags)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cooperativeMatrixConversion = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCubicClampFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceCubicClampFeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cubicRangeClamp = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceYcbcrDegammaFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceYcbcrDegammaFeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->ycbcrDegamma = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCubicWeightsFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceCubicWeightsFeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->selectableCubicWeights = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceImageProcessing2FeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceImageProcessing2FeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->textureBlockMatch2 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceImageProcessing2PropertiesQCOM(MvkDecoder *d, VkPhysicalDeviceImageProcessing2PropertiesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkExtent2D(d, &s->maxBlockMatchWindow);
}

void mvk_out_VkPhysicalDeviceImageProcessing3FeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceImageProcessing3FeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->imageGatherLinear = (VkBool32)mvk_get_u32(d);
    s->imageGatherExtendedModes = (VkBool32)mvk_get_u32(d);
    s->blockMatchExtendedClampToEdge = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV(MvkDecoder *d, VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->descriptorPoolOverallocation = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceLayeredDriverPropertiesMSFT(MvkDecoder *d, VkPhysicalDeviceLayeredDriverPropertiesMSFT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->underlyingAPI = (VkLayeredDriverUnderlyingApiMSFT)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePerStageDescriptorSetFeaturesNV(MvkDecoder *d, VkPhysicalDevicePerStageDescriptorSetFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->perStageDescriptorSet = (VkBool32)mvk_get_u32(d);
    s->dynamicPipelineLayout = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceSchedulingControlsFeaturesARM(MvkDecoder *d, VkPhysicalDeviceSchedulingControlsFeaturesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->schedulingControls = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceSchedulingControlsPropertiesARM(MvkDecoder *d, VkPhysicalDeviceSchedulingControlsPropertiesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->schedulingControlsFlags = (VkPhysicalDeviceSchedulingControlsFlagsARM)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM(MvkDecoder *d, VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->schedulingControlsMaxWarpsCount = (uint32_t)mvk_get_u32(d);
    s->schedulingControlsMaxQueuedBatchesCount = (uint32_t)mvk_get_u32(d);
    s->schedulingControlsMaxWorkGroupBatchSize = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG(MvkDecoder *d, VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->relaxedLineRasterization = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRenderPassStripedFeaturesARM(MvkDecoder *d, VkPhysicalDeviceRenderPassStripedFeaturesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->renderPassStriped = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRenderPassStripedPropertiesARM(MvkDecoder *d, VkPhysicalDeviceRenderPassStripedPropertiesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    mvk_out_VkExtent2D(d, &s->renderPassStripeGranularity);
    s->maxRenderPassStripes = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePipelineOpacityMicromapFeaturesARM(MvkDecoder *d, VkPhysicalDevicePipelineOpacityMicromapFeaturesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pipelineOpacityMicromap = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderMaximalReconvergence = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderSubgroupRotateFeatures(MvkDecoder *d, VkPhysicalDeviceShaderSubgroupRotateFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderSubgroupRotate = (VkBool32)mvk_get_u32(d);
    s->shaderSubgroupRotateClustered = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderExpectAssumeFeatures(MvkDecoder *d, VkPhysicalDeviceShaderExpectAssumeFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderExpectAssume = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderFloatControls2Features(MvkDecoder *d, VkPhysicalDeviceShaderFloatControls2Features *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderFloatControls2 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDynamicRenderingLocalReadFeatures(MvkDecoder *d, VkPhysicalDeviceDynamicRenderingLocalReadFeatures *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->dynamicRenderingLocalRead = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderQuadControlFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderQuadControlFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderQuadControl = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV(MvkDecoder *d, VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderFloat16VectorAtomics = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMapMemoryPlacedFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMapMemoryPlacedFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->memoryMapPlaced = (VkBool32)mvk_get_u32(d);
    s->memoryMapRangePlaced = (VkBool32)mvk_get_u32(d);
    s->memoryUnmapReserve = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceMapMemoryPlacedPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceMapMemoryPlacedPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->minPlacedMemoryMapAlignment = (VkDeviceSize)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceShaderBfloat16FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderBfloat16FeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderBFloat16Type = (VkBool32)mvk_get_u32(d);
    s->shaderBFloat16DotProduct = (VkBool32)mvk_get_u32(d);
    s->shaderBFloat16CooperativeMatrix = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceRawAccessChainsFeaturesNV(MvkDecoder *d, VkPhysicalDeviceRawAccessChainsFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderRawAccessChains = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCommandBufferInheritanceFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCommandBufferInheritanceFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->commandBufferInheritance = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceImageAlignmentControlFeaturesMESA(MvkDecoder *d, VkPhysicalDeviceImageAlignmentControlFeaturesMESA *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->imageAlignmentControl = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceImageAlignmentControlPropertiesMESA(MvkDecoder *d, VkPhysicalDeviceImageAlignmentControlPropertiesMESA *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->supportedImageAlignmentMask = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderReplicatedComposites = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR(MvkDecoder *d, VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->presentModeFifoLatestReady = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCooperativeMatrix2FeaturesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrix2FeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cooperativeMatrixWorkgroupScope = (VkBool32)mvk_get_u32(d);
    s->cooperativeMatrixFlexibleDimensions = (VkBool32)mvk_get_u32(d);
    s->cooperativeMatrixReductions = (VkBool32)mvk_get_u32(d);
    s->cooperativeMatrixConversions = (VkBool32)mvk_get_u32(d);
    s->cooperativeMatrixPerElementOperations = (VkBool32)mvk_get_u32(d);
    s->cooperativeMatrixTensorAddressing = (VkBool32)mvk_get_u32(d);
    s->cooperativeMatrixBlockLoads = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCooperativeMatrix2PropertiesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrix2PropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cooperativeMatrixWorkgroupScopeMaxWorkgroupSize = (uint32_t)mvk_get_u32(d);
    s->cooperativeMatrixFlexibleDimensionsMaxDimension = (uint32_t)mvk_get_u32(d);
    s->cooperativeMatrixWorkgroupScopeReservedSharedMemory = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cooperativeMatrixDecodeVector = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceHdrVividFeaturesHUAWEI(MvkDecoder *d, VkPhysicalDeviceHdrVividFeaturesHUAWEI *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->hdrVivid = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->vertexAttributeRobustness = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDepthClampZeroOneFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceDepthClampZeroOneFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->depthClampZeroOne = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCooperativeVectorFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeVectorFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cooperativeVector = (VkBool32)mvk_get_u32(d);
    s->cooperativeVectorTraining = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCooperativeVectorPropertiesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeVectorPropertiesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cooperativeVectorSupportedStages = (VkShaderStageFlags)mvk_get_u32(d);
    s->cooperativeVectorTrainingFloat16Accumulation = (VkBool32)mvk_get_u32(d);
    s->cooperativeVectorTrainingFloat32Accumulation = (VkBool32)mvk_get_u32(d);
    s->maxCooperativeVectorComponents = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxFragmentDensityMapLayers = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->fragmentDensityMapLayered = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePresentMeteringFeaturesNV(MvkDecoder *d, VkPhysicalDevicePresentMeteringFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->presentMetering = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderUniformBufferUnsizedArray = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE(MvkDecoder *d, VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderMixedFloatDotProductFloat16AccFloat32 = (VkBool32)mvk_get_u32(d);
    s->shaderMixedFloatDotProductFloat16AccFloat16 = (VkBool32)mvk_get_u32(d);
    s->shaderMixedFloatDotProductBFloat16Acc = (VkBool32)mvk_get_u32(d);
    s->shaderMixedFloatDotProductFloat8AccFloat32 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT(MvkDecoder *d, VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->primitiveRestartIndex = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceFormatPackFeaturesARM(MvkDecoder *d, VkPhysicalDeviceFormatPackFeaturesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->formatPack = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceThrottleHintFeaturesSEC(MvkDecoder *d, VkPhysicalDeviceThrottleHintFeaturesSEC *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->throttleHint = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderFloat8FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderFloat8FeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderFloat8 = (VkBool32)mvk_get_u32(d);
    s->shaderFloat8CooperativeMatrix = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC(MvkDecoder *d, VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->pipelineCacheIncrementalMode = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDataGraphModelFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceDataGraphModelFeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->dataGraphModel = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderUntypedPointersFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderUntypedPointersFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderUntypedPointers = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE(MvkDecoder *d, VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->videoEncodeRgbConversion = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShader64BitIndexingFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShader64BitIndexingFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shader64BitIndexing = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceQueuePerfHintFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceQueuePerfHintFeaturesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->queuePerfHint = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceQueuePerfHintPropertiesQCOM(MvkDecoder *d, VkPhysicalDeviceQueuePerfHintPropertiesQCOM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->supportedQueues = (VkQueueFlags)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePerformanceCountersByRegionFeaturesARM(MvkDecoder *d, VkPhysicalDevicePerformanceCountersByRegionFeaturesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->performanceCountersByRegion = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDevicePerformanceCountersByRegionPropertiesARM(MvkDecoder *d, VkPhysicalDevicePerformanceCountersByRegionPropertiesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxPerRegionPerformanceCounters = (uint32_t)mvk_get_u32(d);
    mvk_out_VkExtent2D(d, &s->performanceCounterRegionSize);
    s->rowStrideAlignment = (uint32_t)mvk_get_u32(d);
    s->regionAlignment = (uint32_t)mvk_get_u32(d);
    s->identityTransformOrder = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV(MvkDecoder *d, VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->computeOccupancyPriority = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderLongVectorFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderLongVectorFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->longVector = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderLongVectorPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceShaderLongVectorPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxVectorComponents = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->textureCompressionASTC_3D = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderSubgroupPartitioned = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkSubsampledImageFormatPropertiesEXT(MvkDecoder *d, VkSubsampledImageFormatPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->subsampledImageDescriptorCount = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderSplitBarrierFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderSplitBarrierFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderSplitBarrier = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderSplitBarrierPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceShaderSplitBarrierPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->splitBarrierReservedSharedMemory = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDescriptorHeapFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDescriptorHeapFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->descriptorHeap = (VkBool32)mvk_get_u32(d);
    s->descriptorHeapCaptureReplay = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDescriptorHeapPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceDescriptorHeapPropertiesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->samplerHeapAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->resourceHeapAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->maxSamplerHeapSize = (VkDeviceSize)mvk_get_u64(d);
    s->maxResourceHeapSize = (VkDeviceSize)mvk_get_u64(d);
    s->minSamplerHeapReservedRange = (VkDeviceSize)mvk_get_u64(d);
    s->minSamplerHeapReservedRangeWithEmbedded = (VkDeviceSize)mvk_get_u64(d);
    s->minResourceHeapReservedRange = (VkDeviceSize)mvk_get_u64(d);
    s->samplerDescriptorSize = (VkDeviceSize)mvk_get_u64(d);
    s->imageDescriptorSize = (VkDeviceSize)mvk_get_u64(d);
    s->bufferDescriptorSize = (VkDeviceSize)mvk_get_u64(d);
    s->samplerDescriptorAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->imageDescriptorAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->bufferDescriptorAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->maxPushDataSize = (VkDeviceSize)mvk_get_u64(d);
    s->imageCaptureReplayOpaqueDataSize = (size_t)mvk_get_u64(d);
    s->maxDescriptorHeapEmbeddedSamplers = (uint32_t)mvk_get_u32(d);
    s->samplerYcbcrConversionCount = (uint32_t)mvk_get_u32(d);
    s->sparseDescriptorHeaps = (VkBool32)mvk_get_u32(d);
    s->protectedDescriptorHeaps = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDescriptorHeapTensorPropertiesARM(MvkDecoder *d, VkPhysicalDeviceDescriptorHeapTensorPropertiesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->tensorDescriptorSize = (VkDeviceSize)mvk_get_u64(d);
    s->tensorDescriptorAlignment = (VkDeviceSize)mvk_get_u64(d);
    s->tensorCaptureReplayOpaqueDataSize = (size_t)mvk_get_u64(d);
}

void mvk_out_VkPhysicalDeviceShaderInstrumentationFeaturesARM(MvkDecoder *d, VkPhysicalDeviceShaderInstrumentationFeaturesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderInstrumentation = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderInstrumentationPropertiesARM(MvkDecoder *d, VkPhysicalDeviceShaderInstrumentationPropertiesARM *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->numMetrics = (uint32_t)mvk_get_u32(d);
    s->perBasicBlockGranularity = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->deviceAddressCommands = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderConstantDataFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderConstantDataFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderConstantData = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderAbortFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderAbortFeaturesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderAbort = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderAbortPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceShaderAbortPropertiesKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxShaderAbortMessageSize = (uint64_t)mvk_get_u64(d);
}

void mvk_out_VkDeviceFaultShaderAbortMessageInfoKHR(MvkDecoder *d, VkDeviceFaultShaderAbortMessageInfoKHR *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->messageDataSize = (uint64_t)mvk_get_u64(d);
    mvk_skip_pointer(d);
}

void mvk_out_VkPhysicalDeviceImageTilingControlFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceImageTilingControlFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->imageTilingControl = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->shaderFloat4 = (VkBool32)mvk_get_u32(d);
    s->shaderFloat6 = (VkBool32)mvk_get_u32(d);
    s->shaderFloat8UnsignedE8M0 = (VkBool32)mvk_get_u32(d);
    s->shaderMXInt8 = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->cooperativeMatrixProperties2 = (VkBool32)mvk_get_u32(d);
    s->cooperativeMatrixReductions = (VkBool32)mvk_get_u32(d);
    s->cooperativeMatrixConversions = (VkBool32)mvk_get_u32(d);
    s->cooperativeMatrixPerElementOperations = (VkBool32)mvk_get_u32(d);
    s->cooperativeMatrixGetCoordinate = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE(MvkDecoder *d, VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->bufferDeviceAddressAllocationAlignment = (VkBool32)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE(MvkDecoder *d, VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->maxBufferDeviceAddressAllocationAlignment = (uint32_t)mvk_get_u32(d);
}

void mvk_out_VkPhysicalDeviceInfoPropertiesINTEL(MvkDecoder *d, VkPhysicalDeviceInfoPropertiesINTEL *s)
{
    mvk_out_chain(d, (void *)s->pNext);
    s->deviceIpVersionArch = (uint32_t)mvk_get_u32(d);
    s->deviceIpVersionRelease = (uint32_t)mvk_get_u32(d);
    s->deviceIpVersionRevision = (uint32_t)mvk_get_u32(d);
}

void mvk_out_chain_struct(MvkDecoder *d, VkBaseOutStructure *s)
{
    switch ((int)s->sType) {
    case VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2: mvk_out_VkMemoryRequirements2(d, (VkMemoryRequirements2 *)s); break;
    case VK_STRUCTURE_TYPE_SPARSE_IMAGE_MEMORY_REQUIREMENTS_2: mvk_out_VkSparseImageMemoryRequirements2(d, (VkSparseImageMemoryRequirements2 *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: mvk_out_VkPhysicalDeviceFeatures2(d, (VkPhysicalDeviceFeatures2 *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2: mvk_out_VkPhysicalDeviceProperties2(d, (VkPhysicalDeviceProperties2 *)s); break;
    case VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2: mvk_out_VkFormatProperties2(d, (VkFormatProperties2 *)s); break;
    case VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2: mvk_out_VkImageFormatProperties2(d, (VkImageFormatProperties2 *)s); break;
    case VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2: mvk_out_VkQueueFamilyProperties2(d, (VkQueueFamilyProperties2 *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2: mvk_out_VkPhysicalDeviceMemoryProperties2(d, (VkPhysicalDeviceMemoryProperties2 *)s); break;
    case VK_STRUCTURE_TYPE_SPARSE_IMAGE_FORMAT_PROPERTIES_2: mvk_out_VkSparseImageFormatProperties2(d, (VkSparseImageFormatProperties2 *)s); break;
    case VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES: mvk_out_VkExternalBufferProperties(d, (VkExternalBufferProperties *)s); break;
    case VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES: mvk_out_VkExternalFenceProperties(d, (VkExternalFenceProperties *)s); break;
    case VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES: mvk_out_VkExternalSemaphoreProperties(d, (VkExternalSemaphoreProperties *)s); break;
    case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT: mvk_out_VkDescriptorSetLayoutSupport(d, (VkDescriptorSetLayoutSupport *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES: mvk_out_VkPhysicalDeviceToolProperties(d, (VkPhysicalDeviceToolProperties *)s); break;
    case VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2: mvk_out_VkSubresourceLayout2(d, (VkSubresourceLayout2 *)s); break;
    case VK_STRUCTURE_TYPE_IMAGE_VIEW_ADDRESS_PROPERTIES_NVX: mvk_out_VkImageViewAddressPropertiesNVX(d, (VkImageViewAddressPropertiesNVX *)s); break;
    case VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_KHR: mvk_out_VkPerformanceCounterKHR(d, (VkPerformanceCounterKHR *)s); break;
    case VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_DESCRIPTION_KHR: mvk_out_VkPerformanceCounterDescriptionKHR(d, (VkPerformanceCounterDescriptionKHR *)s); break;
    case VK_STRUCTURE_TYPE_GPA_DEVICE_CLOCK_MODE_INFO_AMD: mvk_out_VkGpaDeviceClockModeInfoAMD(d, (VkGpaDeviceClockModeInfoAMD *)s); break;
    case VK_STRUCTURE_TYPE_GPA_DEVICE_GET_CLOCK_INFO_AMD: mvk_out_VkGpaDeviceGetClockInfoAMD(d, (VkGpaDeviceGetClockInfoAMD *)s); break;
    case VK_STRUCTURE_TYPE_MULTISAMPLE_PROPERTIES_EXT: mvk_out_VkMultisamplePropertiesEXT(d, (VkMultisamplePropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR: mvk_out_VkAccelerationStructureBuildSizesInfoKHR(d, (VkAccelerationStructureBuildSizesInfoKHR *)s); break;
    case VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT: mvk_out_VkImageDrmFormatModifierPropertiesEXT(d, (VkImageDrmFormatModifierPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR: mvk_out_VkPhysicalDeviceFragmentShadingRateKHR(d, (VkPhysicalDeviceFragmentShadingRateKHR *)s); break;
    case VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_NV: mvk_out_VkCooperativeMatrixPropertiesNV(d, (VkCooperativeMatrixPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_FRAMEBUFFER_MIXED_SAMPLES_COMBINATION_NV: mvk_out_VkFramebufferMixedSamplesCombinationNV(d, (VkFramebufferMixedSamplesCombinationNV *)s); break;
    case VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR: mvk_out_VkPipelineExecutablePropertiesKHR(d, (VkPipelineExecutablePropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR: mvk_out_VkPipelineExecutableStatisticKHR(d, (VkPipelineExecutableStatisticKHR *)s); break;
    case VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR: mvk_out_VkPipelineExecutableInternalRepresentationKHR(d, (VkPipelineExecutableInternalRepresentationKHR *)s); break;
    case VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT: mvk_out_VkMicromapBuildSizesInfoEXT(d, (VkMicromapBuildSizesInfoEXT *)s); break;
    case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_HOST_MAPPING_INFO_VALVE: mvk_out_VkDescriptorSetLayoutHostMappingInfoVALVE(d, (VkDescriptorSetLayoutHostMappingInfoVALVE *)s); break;
    case VK_STRUCTURE_TYPE_SHADER_MODULE_IDENTIFIER_EXT: mvk_out_VkShaderModuleIdentifierEXT(d, (VkShaderModuleIdentifierEXT *)s); break;
    case VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_PROPERTIES_NV: mvk_out_VkOpticalFlowImageFormatPropertiesNV(d, (VkOpticalFlowImageFormatPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_COOPERATIVE_VECTOR_PROPERTIES_NV: mvk_out_VkCooperativeVectorPropertiesNV(d, (VkCooperativeVectorPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR: mvk_out_VkCooperativeMatrixPropertiesKHR(d, (VkCooperativeMatrixPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR: mvk_out_VkDeviceFaultInfoKHR(d, (VkDeviceFaultInfoKHR *)s); break;
    case VK_STRUCTURE_TYPE_DEVICE_FAULT_DEBUG_INFO_KHR: mvk_out_VkDeviceFaultDebugInfoKHR(d, (VkDeviceFaultDebugInfoKHR *)s); break;
    case VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_FLEXIBLE_DIMENSIONS_PROPERTIES_NV: mvk_out_VkCooperativeMatrixFlexibleDimensionsPropertiesNV(d, (VkCooperativeMatrixFlexibleDimensionsPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_ARM: mvk_out_VkPerformanceCounterARM(d, (VkPerformanceCounterARM *)s); break;
    case VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_DESCRIPTION_ARM: mvk_out_VkPerformanceCounterDescriptionARM(d, (VkPerformanceCounterDescriptionARM *)s); break;
    case VK_STRUCTURE_TYPE_SHADER_INSTRUMENTATION_METRIC_DESCRIPTION_ARM: mvk_out_VkShaderInstrumentationMetricDescriptionARM(d, (VkShaderInstrumentationMetricDescriptionARM *)s); break;
    case VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_2_EXT: mvk_out_VkCooperativeMatrixProperties2EXT(d, (VkCooperativeMatrixProperties2EXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES: mvk_out_VkPhysicalDeviceGroupProperties(d, (VkPhysicalDeviceGroupProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_NV: mvk_out_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV(d, (VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_CONSTANT_BANK_FEATURES_NV: mvk_out_VkPhysicalDevicePushConstantBankFeaturesNV(d, (VkPhysicalDevicePushConstantBankFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_CONSTANT_BANK_PROPERTIES_NV: mvk_out_VkPhysicalDevicePushConstantBankPropertiesNV(d, (VkPhysicalDevicePushConstantBankPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_COMPUTE_FEATURES_NV: mvk_out_VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV(d, (VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES: mvk_out_VkPhysicalDevicePrivateDataFeatures(d, (VkPhysicalDevicePrivateDataFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_BASE_HANDLE_FEATURES_NV: mvk_out_VkPhysicalDevicePrivateDataBaseHandleFeaturesNV(d, (VkPhysicalDevicePrivateDataBaseHandleFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_NV: mvk_out_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV(d, (VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV: mvk_out_VkPhysicalDeviceClusterAccelerationStructureFeaturesNV(d, (VkPhysicalDeviceClusterAccelerationStructureFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_PROPERTIES_NV: mvk_out_VkPhysicalDeviceClusterAccelerationStructurePropertiesNV(d, (VkPhysicalDeviceClusterAccelerationStructurePropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceMultiDrawPropertiesEXT(d, (VkPhysicalDeviceMultiDrawPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES: mvk_out_VkPhysicalDevicePushDescriptorProperties(d, (VkPhysicalDevicePushDescriptorProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES: mvk_out_VkPhysicalDeviceDriverProperties(d, (VkPhysicalDeviceDriverProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: mvk_out_VkPhysicalDeviceVariablePointersFeatures(d, (VkPhysicalDeviceVariablePointersFeatures *)s); break;
    case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES: mvk_out_VkExternalImageFormatProperties(d, (VkExternalImageFormatProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: mvk_out_VkPhysicalDeviceIDProperties(d, (VkPhysicalDeviceIDProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: mvk_out_VkPhysicalDeviceMultiviewFeatures(d, (VkPhysicalDeviceMultiviewFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES: mvk_out_VkPhysicalDeviceMultiviewProperties(d, (VkPhysicalDeviceMultiviewProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DISCARD_RECTANGLE_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceDiscardRectanglePropertiesEXT(d, (VkPhysicalDeviceDiscardRectanglePropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PER_VIEW_ATTRIBUTES_PROPERTIES_NVX: mvk_out_VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX(d, (VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: mvk_out_VkPhysicalDevice16BitStorageFeatures(d, (VkPhysicalDevice16BitStorageFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: mvk_out_VkPhysicalDeviceSubgroupProperties(d, (VkPhysicalDeviceSubgroupProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES: mvk_out_VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures(d, (VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES: mvk_out_VkPhysicalDevicePointClippingProperties(d, (VkPhysicalDevicePointClippingProperties *)s); break;
    case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: mvk_out_VkMemoryDedicatedRequirements(d, (VkMemoryDedicatedRequirements *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES: mvk_out_VkPhysicalDeviceSamplerYcbcrConversionFeatures(d, (VkPhysicalDeviceSamplerYcbcrConversionFeatures *)s); break;
    case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES: mvk_out_VkSamplerYcbcrConversionImageFormatProperties(d, (VkSamplerYcbcrConversionImageFormatProperties *)s); break;
    case VK_STRUCTURE_TYPE_TEXTURE_LOD_GATHER_FORMAT_PROPERTIES_AMD: mvk_out_VkTextureLODGatherFormatPropertiesAMD(d, (VkTextureLODGatherFormatPropertiesAMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES: mvk_out_VkPhysicalDeviceProtectedMemoryFeatures(d, (VkPhysicalDeviceProtectedMemoryFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES: mvk_out_VkPhysicalDeviceProtectedMemoryProperties(d, (VkPhysicalDeviceProtectedMemoryProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES: mvk_out_VkPhysicalDeviceSamplerFilterMinmaxProperties(d, (VkPhysicalDeviceSamplerFilterMinmaxProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLE_LOCATIONS_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceSampleLocationsPropertiesEXT(d, (VkPhysicalDeviceSampleLocationsPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_FEATURES_EXT: mvk_out_VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT(d, (VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT: mvk_out_VkPhysicalDeviceMultiDrawFeaturesEXT(d, (VkPhysicalDeviceMultiDrawFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT(d, (VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES: mvk_out_VkPhysicalDeviceInlineUniformBlockFeatures(d, (VkPhysicalDeviceInlineUniformBlockFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES: mvk_out_VkPhysicalDeviceInlineUniformBlockProperties(d, (VkPhysicalDeviceInlineUniformBlockProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES: mvk_out_VkPhysicalDeviceMaintenance3Properties(d, (VkPhysicalDeviceMaintenance3Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES: mvk_out_VkPhysicalDeviceMaintenance4Features(d, (VkPhysicalDeviceMaintenance4Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES: mvk_out_VkPhysicalDeviceMaintenance4Properties(d, (VkPhysicalDeviceMaintenance4Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES: mvk_out_VkPhysicalDeviceMaintenance5Features(d, (VkPhysicalDeviceMaintenance5Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_PROPERTIES: mvk_out_VkPhysicalDeviceMaintenance5Properties(d, (VkPhysicalDeviceMaintenance5Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES: mvk_out_VkPhysicalDeviceMaintenance6Features(d, (VkPhysicalDeviceMaintenance6Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_PROPERTIES: mvk_out_VkPhysicalDeviceMaintenance6Properties(d, (VkPhysicalDeviceMaintenance6Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_FEATURES_KHR: mvk_out_VkPhysicalDeviceMaintenance7FeaturesKHR(d, (VkPhysicalDeviceMaintenance7FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceMaintenance7PropertiesKHR(d, (VkPhysicalDeviceMaintenance7PropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_API_PROPERTIES_LIST_KHR: mvk_out_VkPhysicalDeviceLayeredApiPropertiesListKHR(d, (VkPhysicalDeviceLayeredApiPropertiesListKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_API_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceLayeredApiPropertiesKHR(d, (VkPhysicalDeviceLayeredApiPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_API_VULKAN_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceLayeredApiVulkanPropertiesKHR(d, (VkPhysicalDeviceLayeredApiVulkanPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR: mvk_out_VkPhysicalDeviceMaintenance8FeaturesKHR(d, (VkPhysicalDeviceMaintenance8FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR: mvk_out_VkPhysicalDeviceMaintenance9FeaturesKHR(d, (VkPhysicalDeviceMaintenance9FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceMaintenance9PropertiesKHR(d, (VkPhysicalDeviceMaintenance9PropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_11_FEATURES_KHR: mvk_out_VkPhysicalDeviceMaintenance11FeaturesKHR(d, (VkPhysicalDeviceMaintenance11FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_10_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceMaintenance10PropertiesKHR(d, (VkPhysicalDeviceMaintenance10PropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_10_FEATURES_KHR: mvk_out_VkPhysicalDeviceMaintenance10FeaturesKHR(d, (VkPhysicalDeviceMaintenance10FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_QUEUE_FAMILY_OWNERSHIP_TRANSFER_PROPERTIES_KHR: mvk_out_VkQueueFamilyOwnershipTransferPropertiesKHR(d, (VkQueueFamilyOwnershipTransferPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_QUEUE_FAMILY_OPTIMAL_IMAGE_TRANSFER_GRANULARITY_PROPERTIES_KHR: mvk_out_VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR(d, (VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES: mvk_out_VkPhysicalDeviceShaderDrawParametersFeatures(d, (VkPhysicalDeviceShaderDrawParametersFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES: mvk_out_VkPhysicalDeviceShaderFloat16Int8Features(d, (VkPhysicalDeviceShaderFloat16Int8Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES: mvk_out_VkPhysicalDeviceFloatControlsProperties(d, (VkPhysicalDeviceFloatControlsProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES: mvk_out_VkPhysicalDeviceHostQueryResetFeatures(d, (VkPhysicalDeviceHostQueryResetFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ELAPSED_TIMER_QUERY_FEATURES_QCOM: mvk_out_VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM(d, (VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES: mvk_out_VkPhysicalDeviceGlobalPriorityQueryFeatures(d, (VkPhysicalDeviceGlobalPriorityQueryFeatures *)s); break;
    case VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES: mvk_out_VkQueueFamilyGlobalPriorityProperties(d, (VkQueueFamilyGlobalPriorityProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_MEMORY_REPORT_FEATURES_EXT: mvk_out_VkPhysicalDeviceDeviceMemoryReportFeaturesEXT(d, (VkPhysicalDeviceDeviceMemoryReportFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceConservativeRasterizationPropertiesEXT(d, (VkPhysicalDeviceConservativeRasterizationPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD: mvk_out_VkPhysicalDeviceShaderCorePropertiesAMD(d, (VkPhysicalDeviceShaderCorePropertiesAMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_2_AMD: mvk_out_VkPhysicalDeviceShaderCoreProperties2AMD(d, (VkPhysicalDeviceShaderCoreProperties2AMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES: mvk_out_VkPhysicalDeviceDescriptorIndexingFeatures(d, (VkPhysicalDeviceDescriptorIndexingFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES: mvk_out_VkPhysicalDeviceDescriptorIndexingProperties(d, (VkPhysicalDeviceDescriptorIndexingProperties *)s); break;
    case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT: mvk_out_VkDescriptorSetVariableDescriptorCountLayoutSupport(d, (VkDescriptorSetVariableDescriptorCountLayoutSupport *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES: mvk_out_VkPhysicalDeviceTimelineSemaphoreFeatures(d, (VkPhysicalDeviceTimelineSemaphoreFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES: mvk_out_VkPhysicalDeviceTimelineSemaphoreProperties(d, (VkPhysicalDeviceTimelineSemaphoreProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT(d, (VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES: mvk_out_VkPhysicalDeviceVertexAttributeDivisorProperties(d, (VkPhysicalDeviceVertexAttributeDivisorProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT: mvk_out_VkPhysicalDevicePCIBusInfoPropertiesEXT(d, (VkPhysicalDevicePCIBusInfoPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES: mvk_out_VkPhysicalDevice8BitStorageFeatures(d, (VkPhysicalDevice8BitStorageFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT: mvk_out_VkPhysicalDeviceConditionalRenderingFeaturesEXT(d, (VkPhysicalDeviceConditionalRenderingFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES: mvk_out_VkPhysicalDeviceVulkanMemoryModelFeatures(d, (VkPhysicalDeviceVulkanMemoryModelFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES: mvk_out_VkPhysicalDeviceShaderAtomicInt64Features(d, (VkPhysicalDeviceShaderAtomicInt64Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderAtomicFloatFeaturesEXT(d, (VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT(d, (VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES: mvk_out_VkPhysicalDeviceVertexAttributeDivisorFeatures(d, (VkPhysicalDeviceVertexAttributeDivisorFeatures *)s); break;
    case VK_STRUCTURE_TYPE_QUEUE_FAMILY_CHECKPOINT_PROPERTIES_NV: mvk_out_VkQueueFamilyCheckpointPropertiesNV(d, (VkQueueFamilyCheckpointPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES: mvk_out_VkPhysicalDeviceDepthStencilResolveProperties(d, (VkPhysicalDeviceDepthStencilResolveProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ASTC_DECODE_FEATURES_EXT: mvk_out_VkPhysicalDeviceASTCDecodeFeaturesEXT(d, (VkPhysicalDeviceASTCDecodeFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT: mvk_out_VkPhysicalDeviceTransformFeedbackFeaturesEXT(d, (VkPhysicalDeviceTransformFeedbackFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceTransformFeedbackPropertiesEXT(d, (VkPhysicalDeviceTransformFeedbackPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_REPRESENTATIVE_FRAGMENT_TEST_FEATURES_NV: mvk_out_VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV(d, (VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXCLUSIVE_SCISSOR_FEATURES_NV: mvk_out_VkPhysicalDeviceExclusiveScissorFeaturesNV(d, (VkPhysicalDeviceExclusiveScissorFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CORNER_SAMPLED_IMAGE_FEATURES_NV: mvk_out_VkPhysicalDeviceCornerSampledImageFeaturesNV(d, (VkPhysicalDeviceCornerSampledImageFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR: mvk_out_VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR(d, (VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR(d, (VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_FOOTPRINT_FEATURES_NV: mvk_out_VkPhysicalDeviceShaderImageFootprintFeaturesNV(d, (VkPhysicalDeviceShaderImageFootprintFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEDICATED_ALLOCATION_IMAGE_ALIASING_FEATURES_NV: mvk_out_VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV(d, (VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COPY_MEMORY_INDIRECT_FEATURES_KHR: mvk_out_VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR(d, (VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COPY_MEMORY_INDIRECT_FEATURES_NV: mvk_out_VkPhysicalDeviceCopyMemoryIndirectFeaturesNV(d, (VkPhysicalDeviceCopyMemoryIndirectFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COPY_MEMORY_INDIRECT_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR(d, (VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_FEATURES_EXT: mvk_out_VkPhysicalDeviceMemoryDecompressionFeaturesEXT(d, (VkPhysicalDeviceMemoryDecompressionFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceMemoryDecompressionPropertiesEXT(d, (VkPhysicalDeviceMemoryDecompressionPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_FEATURES_NV: mvk_out_VkPhysicalDeviceShadingRateImageFeaturesNV(d, (VkPhysicalDeviceShadingRateImageFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_PROPERTIES_NV: mvk_out_VkPhysicalDeviceShadingRateImagePropertiesNV(d, (VkPhysicalDeviceShadingRateImagePropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INVOCATION_MASK_FEATURES_HUAWEI: mvk_out_VkPhysicalDeviceInvocationMaskFeaturesHUAWEI(d, (VkPhysicalDeviceInvocationMaskFeaturesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV: mvk_out_VkPhysicalDeviceMeshShaderFeaturesNV(d, (VkPhysicalDeviceMeshShaderFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_NV: mvk_out_VkPhysicalDeviceMeshShaderPropertiesNV(d, (VkPhysicalDeviceMeshShaderPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT: mvk_out_VkPhysicalDeviceMeshShaderFeaturesEXT(d, (VkPhysicalDeviceMeshShaderFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceMeshShaderPropertiesEXT(d, (VkPhysicalDeviceMeshShaderPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR: mvk_out_VkPhysicalDeviceAccelerationStructureFeaturesKHR(d, (VkPhysicalDeviceAccelerationStructureFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR: mvk_out_VkPhysicalDeviceRayTracingPipelineFeaturesKHR(d, (VkPhysicalDeviceRayTracingPipelineFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR: mvk_out_VkPhysicalDeviceRayQueryFeaturesKHR(d, (VkPhysicalDeviceRayQueryFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceAccelerationStructurePropertiesKHR(d, (VkPhysicalDeviceAccelerationStructurePropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceRayTracingPipelinePropertiesKHR(d, (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV: mvk_out_VkPhysicalDeviceRayTracingPropertiesNV(d, (VkPhysicalDeviceRayTracingPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR: mvk_out_VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR(d, (VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT: mvk_out_VkDrmFormatModifierPropertiesListEXT(d, (VkDrmFormatModifierPropertiesListEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT: mvk_out_VkPhysicalDeviceFragmentDensityMapFeaturesEXT(d, (VkPhysicalDeviceFragmentDensityMapFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_FEATURES_EXT: mvk_out_VkPhysicalDeviceFragmentDensityMap2FeaturesEXT(d, (VkPhysicalDeviceFragmentDensityMap2FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_FEATURES_EXT: mvk_out_VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT(d, (VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceFragmentDensityMapPropertiesEXT(d, (VkPhysicalDeviceFragmentDensityMapPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceFragmentDensityMap2PropertiesEXT(d, (VkPhysicalDeviceFragmentDensityMap2PropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT(d, (VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES: mvk_out_VkPhysicalDeviceScalarBlockLayoutFeatures(d, (VkPhysicalDeviceScalarBlockLayoutFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES: mvk_out_VkPhysicalDeviceUniformBufferStandardLayoutFeatures(d, (VkPhysicalDeviceUniformBufferStandardLayoutFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT: mvk_out_VkPhysicalDeviceDepthClipEnableFeaturesEXT(d, (VkPhysicalDeviceDepthClipEnableFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceMemoryBudgetPropertiesEXT(d, (VkPhysicalDeviceMemoryBudgetPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT: mvk_out_VkPhysicalDeviceMemoryPriorityFeaturesEXT(d, (VkPhysicalDeviceMemoryPriorityFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT: mvk_out_VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT(d, (VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES: mvk_out_VkPhysicalDeviceBufferDeviceAddressFeatures(d, (VkPhysicalDeviceBufferDeviceAddressFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT: mvk_out_VkPhysicalDeviceBufferDeviceAddressFeaturesEXT(d, (VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_FILTER_CUBIC_IMAGE_VIEW_IMAGE_FORMAT_PROPERTIES_EXT: mvk_out_VkFilterCubicImageViewImageFormatPropertiesEXT(d, (VkFilterCubicImageViewImageFormatPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES: mvk_out_VkPhysicalDeviceImagelessFramebufferFeatures(d, (VkPhysicalDeviceImagelessFramebufferFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES: mvk_out_VkPhysicalDeviceTextureCompressionASTCHDRFeatures(d, (VkPhysicalDeviceTextureCompressionASTCHDRFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_NV: mvk_out_VkPhysicalDeviceCooperativeMatrixFeaturesNV(d, (VkPhysicalDeviceCooperativeMatrixFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_NV: mvk_out_VkPhysicalDeviceCooperativeMatrixPropertiesNV(d, (VkPhysicalDeviceCooperativeMatrixPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_IMAGE_ARRAYS_FEATURES_EXT: mvk_out_VkPhysicalDeviceYcbcrImageArraysFeaturesEXT(d, (VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR: mvk_out_VkPhysicalDevicePerformanceQueryFeaturesKHR(d, (VkPhysicalDevicePerformanceQueryFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_PROPERTIES_KHR: mvk_out_VkPhysicalDevicePerformanceQueryPropertiesKHR(d, (VkPhysicalDevicePerformanceQueryPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COVERAGE_REDUCTION_MODE_FEATURES_NV: mvk_out_VkPhysicalDeviceCoverageReductionModeFeaturesNV(d, (VkPhysicalDeviceCoverageReductionModeFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_FUNCTIONS_2_FEATURES_INTEL: mvk_out_VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL(d, (VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR: mvk_out_VkPhysicalDeviceShaderClockFeaturesKHR(d, (VkPhysicalDeviceShaderClockFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES: mvk_out_VkPhysicalDeviceIndexTypeUint8Features(d, (VkPhysicalDeviceIndexTypeUint8Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SM_BUILTINS_PROPERTIES_NV: mvk_out_VkPhysicalDeviceShaderSMBuiltinsPropertiesNV(d, (VkPhysicalDeviceShaderSMBuiltinsPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SM_BUILTINS_FEATURES_NV: mvk_out_VkPhysicalDeviceShaderSMBuiltinsFeaturesNV(d, (VkPhysicalDeviceShaderSMBuiltinsFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT: mvk_out_VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT(d, (VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES: mvk_out_VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures(d, (VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT: mvk_out_VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT(d, (VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR: mvk_out_VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR(d, (VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES: mvk_out_VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures(d, (VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT: mvk_out_VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT(d, (VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES: mvk_out_VkPhysicalDeviceTexelBufferAlignmentProperties(d, (VkPhysicalDeviceTexelBufferAlignmentProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES: mvk_out_VkPhysicalDeviceSubgroupSizeControlFeatures(d, (VkPhysicalDeviceSubgroupSizeControlFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES: mvk_out_VkPhysicalDeviceSubgroupSizeControlProperties(d, (VkPhysicalDeviceSubgroupSizeControlProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBPASS_SHADING_PROPERTIES_HUAWEI: mvk_out_VkPhysicalDeviceSubpassShadingPropertiesHUAWEI(d, (VkPhysicalDeviceSubpassShadingPropertiesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_PROPERTIES_HUAWEI: mvk_out_VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI(d, (VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES: mvk_out_VkPhysicalDeviceLineRasterizationFeatures(d, (VkPhysicalDeviceLineRasterizationFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES: mvk_out_VkPhysicalDeviceLineRasterizationProperties(d, (VkPhysicalDeviceLineRasterizationProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES: mvk_out_VkPhysicalDevicePipelineCreationCacheControlFeatures(d, (VkPhysicalDevicePipelineCreationCacheControlFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: mvk_out_VkPhysicalDeviceVulkan11Features(d, (VkPhysicalDeviceVulkan11Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES: mvk_out_VkPhysicalDeviceVulkan11Properties(d, (VkPhysicalDeviceVulkan11Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: mvk_out_VkPhysicalDeviceVulkan12Features(d, (VkPhysicalDeviceVulkan12Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES: mvk_out_VkPhysicalDeviceVulkan12Properties(d, (VkPhysicalDeviceVulkan12Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: mvk_out_VkPhysicalDeviceVulkan13Features(d, (VkPhysicalDeviceVulkan13Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES: mvk_out_VkPhysicalDeviceVulkan13Properties(d, (VkPhysicalDeviceVulkan13Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES: mvk_out_VkPhysicalDeviceVulkan14Features(d, (VkPhysicalDeviceVulkan14Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES: mvk_out_VkPhysicalDeviceVulkan14Properties(d, (VkPhysicalDeviceVulkan14Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD: mvk_out_VkPhysicalDeviceCoherentMemoryFeaturesAMD(d, (VkPhysicalDeviceCoherentMemoryFeaturesAMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GPA_FEATURES_AMD: mvk_out_VkPhysicalDeviceGpaFeaturesAMD(d, (VkPhysicalDeviceGpaFeaturesAMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GPA_PROPERTIES_AMD: mvk_out_VkPhysicalDeviceGpaPropertiesAMD(d, (VkPhysicalDeviceGpaPropertiesAMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GPA_PROPERTIES_2_AMD: mvk_out_VkPhysicalDeviceGpaProperties2AMD(d, (VkPhysicalDeviceGpaProperties2AMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceCustomBorderColorPropertiesEXT(d, (VkPhysicalDeviceCustomBorderColorPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT: mvk_out_VkPhysicalDeviceCustomBorderColorFeaturesEXT(d, (VkPhysicalDeviceCustomBorderColorFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT: mvk_out_VkPhysicalDeviceBorderColorSwizzleFeaturesEXT(d, (VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT: mvk_out_VkPhysicalDeviceExtendedDynamicStateFeaturesEXT(d, (VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT: mvk_out_VkPhysicalDeviceExtendedDynamicState2FeaturesEXT(d, (VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT: mvk_out_VkPhysicalDeviceExtendedDynamicState3FeaturesEXT(d, (VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceExtendedDynamicState3PropertiesEXT(d, (VkPhysicalDeviceExtendedDynamicState3PropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_FLAGS_FEATURES_KHR: mvk_out_VkPhysicalDeviceExtendedFlagsFeaturesKHR(d, (VkPhysicalDeviceExtendedFlagsFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_FEATURES_NV: mvk_out_VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV(d, (VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_PROPERTIES_NV: mvk_out_VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV(d, (VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV: mvk_out_VkPhysicalDeviceDiagnosticsConfigFeaturesNV(d, (VkPhysicalDeviceDiagnosticsConfigFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES: mvk_out_VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures(d, (VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_FEATURES_KHR: mvk_out_VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR(d, (VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_KHR: mvk_out_VkPhysicalDeviceRobustness2FeaturesKHR(d, (VkPhysicalDeviceRobustness2FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceRobustness2PropertiesKHR(d, (VkPhysicalDeviceRobustness2PropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES: mvk_out_VkPhysicalDeviceImageRobustnessFeatures(d, (VkPhysicalDeviceImageRobustnessFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR: mvk_out_VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR(d, (VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT: mvk_out_VkPhysicalDevice4444FormatsFeaturesEXT(d, (VkPhysicalDevice4444FormatsFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBPASS_SHADING_FEATURES_HUAWEI: mvk_out_VkPhysicalDeviceSubpassShadingFeaturesHUAWEI(d, (VkPhysicalDeviceSubpassShadingFeaturesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_FEATURES_HUAWEI: mvk_out_VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI(d, (VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_VRS_FEATURES_HUAWEI: mvk_out_VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI(d, (VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT(d, (VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR: mvk_out_VkPhysicalDeviceFragmentShadingRateFeaturesKHR(d, (VkPhysicalDeviceFragmentShadingRateFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceFragmentShadingRatePropertiesKHR(d, (VkPhysicalDeviceFragmentShadingRatePropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES: mvk_out_VkPhysicalDeviceShaderTerminateInvocationFeatures(d, (VkPhysicalDeviceShaderTerminateInvocationFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_ENUMS_FEATURES_NV: mvk_out_VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV(d, (VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_ENUMS_PROPERTIES_NV: mvk_out_VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV(d, (VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_2D_VIEW_OF_3D_FEATURES_EXT: mvk_out_VkPhysicalDeviceImage2DViewOf3DFeaturesEXT(d, (VkPhysicalDeviceImage2DViewOf3DFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_SLICED_VIEW_OF_3D_FEATURES_EXT: mvk_out_VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT(d, (VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_FEATURES_EXT: mvk_out_VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT(d, (VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LEGACY_VERTEX_ATTRIBUTES_FEATURES_EXT: mvk_out_VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT(d, (VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LEGACY_VERTEX_ATTRIBUTES_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT(d, (VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT: mvk_out_VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT(d, (VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT: mvk_out_VkPhysicalDeviceDepthClipControlFeaturesEXT(d, (VkPhysicalDeviceDepthClipControlFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_DEVICE_MEMORY_FEATURES_EXT: mvk_out_VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT(d, (VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_RESOLVE_FEATURES_EXT: mvk_out_VkPhysicalDeviceCustomResolveFeaturesEXT(d, (VkPhysicalDeviceCustomResolveFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_EXT: mvk_out_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT(d, (VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT(d, (VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLAMP_CONTROL_FEATURES_EXT: mvk_out_VkPhysicalDeviceDepthClampControlFeaturesEXT(d, (VkPhysicalDeviceDepthClampControlFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT: mvk_out_VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT(d, (VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_RDMA_FEATURES_NV: mvk_out_VkPhysicalDeviceExternalMemoryRDMAFeaturesNV(d, (VkPhysicalDeviceExternalMemoryRDMAFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_RELAXED_EXTENDED_INSTRUCTION_FEATURES_KHR: mvk_out_VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR(d, (VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: mvk_out_VkPhysicalDeviceColorWriteEnableFeaturesEXT(d, (VkPhysicalDeviceColorWriteEnableFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_QUEUE_FAMILY_CHECKPOINT_PROPERTIES_2_NV: mvk_out_VkQueueFamilyCheckpointProperties2NV(d, (VkQueueFamilyCheckpointProperties2NV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES: mvk_out_VkPhysicalDeviceSynchronization2Features(d, (VkPhysicalDeviceSynchronization2Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR: mvk_out_VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR(d, (VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES: mvk_out_VkPhysicalDeviceHostImageCopyFeatures(d, (VkPhysicalDeviceHostImageCopyFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES: mvk_out_VkPhysicalDeviceHostImageCopyProperties(d, (VkPhysicalDeviceHostImageCopyProperties *)s); break;
    case VK_STRUCTURE_TYPE_SUBRESOURCE_HOST_MEMCPY_SIZE: mvk_out_VkSubresourceHostMemcpySize(d, (VkSubresourceHostMemcpySize *)s); break;
    case VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY: mvk_out_VkHostImageCopyDevicePerformanceQuery(d, (VkHostImageCopyDevicePerformanceQuery *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVES_GENERATED_QUERY_FEATURES_EXT: mvk_out_VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT(d, (VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LEGACY_DITHERING_FEATURES_EXT: mvk_out_VkPhysicalDeviceLegacyDitheringFeaturesEXT(d, (VkPhysicalDeviceLegacyDitheringFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT: mvk_out_VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT(d, (VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_SUBPASS_RESOLVE_PERFORMANCE_QUERY_EXT: mvk_out_VkSubpassResolvePerformanceQueryEXT(d, (VkSubpassResolvePerformanceQueryEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SWAPCHAIN_FEATURES_EXT: mvk_out_VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT(d, (VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_PROTECTED_ACCESS_FEATURES: mvk_out_VkPhysicalDevicePipelineProtectedAccessFeatures(d, (VkPhysicalDevicePipelineProtectedAccessFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INHERITED_VIEWPORT_SCISSOR_FEATURES_NV: mvk_out_VkPhysicalDeviceInheritedViewportScissorFeaturesNV(d, (VkPhysicalDeviceInheritedViewportScissorFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_2_PLANE_444_FORMATS_FEATURES_EXT: mvk_out_VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT(d, (VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT: mvk_out_VkPhysicalDeviceProvokingVertexFeaturesEXT(d, (VkPhysicalDeviceProvokingVertexFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceProvokingVertexPropertiesEXT(d, (VkPhysicalDeviceProvokingVertexPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT: mvk_out_VkPhysicalDeviceDescriptorBufferFeaturesEXT(d, (VkPhysicalDeviceDescriptorBufferFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceDescriptorBufferPropertiesEXT(d, (VkPhysicalDeviceDescriptorBufferPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_DENSITY_MAP_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT(d, (VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES: mvk_out_VkPhysicalDeviceShaderIntegerDotProductFeatures(d, (VkPhysicalDeviceShaderIntegerDotProductFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES: mvk_out_VkPhysicalDeviceShaderIntegerDotProductProperties(d, (VkPhysicalDeviceShaderIntegerDotProductProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceDrmPropertiesEXT(d, (VkPhysicalDeviceDrmPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR: mvk_out_VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR(d, (VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR(d, (VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FMA_FEATURES_KHR: mvk_out_VkPhysicalDeviceShaderFmaFeaturesKHR(d, (VkPhysicalDeviceShaderFmaFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MOTION_BLUR_FEATURES_NV: mvk_out_VkPhysicalDeviceRayTracingMotionBlurFeaturesNV(d, (VkPhysicalDeviceRayTracingMotionBlurFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV: mvk_out_VkPhysicalDeviceRayTracingValidationFeaturesNV(d, (VkPhysicalDeviceRayTracingValidationFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV: mvk_out_VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV(d, (VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RGBA10X6_FORMATS_FEATURES_EXT: mvk_out_VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT(d, (VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3: mvk_out_VkFormatProperties3(d, (VkFormatProperties3 *)s); break;
    case VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_4_KHR: mvk_out_VkFormatProperties4KHR(d, (VkFormatProperties4KHR *)s); break;
    case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT: mvk_out_VkDrmFormatModifierPropertiesList2EXT(d, (VkDrmFormatModifierPropertiesList2EXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES: mvk_out_VkPhysicalDeviceDynamicRenderingFeatures(d, (VkPhysicalDeviceDynamicRenderingFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT: mvk_out_VkPhysicalDeviceImageViewMinLodFeaturesEXT(d, (VkPhysicalDeviceImageViewMinLodFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT: mvk_out_VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT(d, (VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINEAR_COLOR_ATTACHMENT_FEATURES_NV: mvk_out_VkPhysicalDeviceLinearColorAttachmentFeaturesNV(d, (VkPhysicalDeviceLinearColorAttachmentFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT: mvk_out_VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT(d, (VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT(d, (VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_TENSOR_EXPLICIT_TILING_FORMAT_PROPERTIES_ARM: mvk_out_VkTensorExplicitTilingFormatPropertiesARM(d, (VkTensorExplicitTilingFormatPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_SET_HOST_MAPPING_FEATURES_VALVE: mvk_out_VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE(d, (VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NESTED_COMMAND_BUFFER_FEATURES_EXT: mvk_out_VkPhysicalDeviceNestedCommandBufferFeaturesEXT(d, (VkPhysicalDeviceNestedCommandBufferFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NESTED_COMMAND_BUFFER_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceNestedCommandBufferPropertiesEXT(d, (VkPhysicalDeviceNestedCommandBufferPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT(d, (VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT(d, (VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_FEATURES_EXT: mvk_out_VkPhysicalDeviceImageCompressionControlFeaturesEXT(d, (VkPhysicalDeviceImageCompressionControlFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT: mvk_out_VkImageCompressionPropertiesEXT(d, (VkImageCompressionPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN_FEATURES_EXT: mvk_out_VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT(d, (VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBPASS_MERGE_FEEDBACK_FEATURES_EXT: mvk_out_VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT(d, (VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_KHR: mvk_out_VkPhysicalDeviceOpacityMicromapFeaturesKHR(d, (VkPhysicalDeviceOpacityMicromapFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT: mvk_out_VkPhysicalDeviceOpacityMicromapFeaturesEXT(d, (VkPhysicalDeviceOpacityMicromapFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceOpacityMicromapPropertiesKHR(d, (VkPhysicalDeviceOpacityMicromapPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceOpacityMicromapPropertiesEXT(d, (VkPhysicalDeviceOpacityMicromapPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_PROPERTIES_FEATURES_EXT: mvk_out_VkPhysicalDevicePipelinePropertiesFeaturesEXT(d, (VkPhysicalDevicePipelinePropertiesFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EARLY_AND_LATE_FRAGMENT_TESTS_FEATURES_AMD: mvk_out_VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD(d, (VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT: mvk_out_VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT(d, (VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES: mvk_out_VkPhysicalDevicePipelineRobustnessFeatures(d, (VkPhysicalDevicePipelineRobustnessFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_PROPERTIES: mvk_out_VkPhysicalDevicePipelineRobustnessProperties(d, (VkPhysicalDevicePipelineRobustnessProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MULTIPLE_WAIT_QUEUES_FEATURES_QCOM: mvk_out_VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM(d, (VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MULTIPLE_WAIT_QUEUES_PROPERTIES_QCOM: mvk_out_VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM(d, (VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_FEATURES_QCOM: mvk_out_VkPhysicalDeviceImageProcessingFeaturesQCOM(d, (VkPhysicalDeviceImageProcessingFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_PROPERTIES_QCOM: mvk_out_VkPhysicalDeviceImageProcessingPropertiesQCOM(d, (VkPhysicalDeviceImageProcessingPropertiesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_AMIGO_PROFILING_FEATURES_SEC: mvk_out_VkPhysicalDeviceAmigoProfilingFeaturesSEC(d, (VkPhysicalDeviceAmigoProfilingFeaturesSEC *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT: mvk_out_VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT(d, (VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT: mvk_out_VkPhysicalDeviceAddressBindingReportFeaturesEXT(d, (VkPhysicalDeviceAddressBindingReportFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV: mvk_out_VkPhysicalDeviceOpticalFlowFeaturesNV(d, (VkPhysicalDeviceOpticalFlowFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_PROPERTIES_NV: mvk_out_VkPhysicalDeviceOpticalFlowPropertiesNV(d, (VkPhysicalDeviceOpticalFlowPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT: mvk_out_VkPhysicalDeviceFaultFeaturesEXT(d, (VkPhysicalDeviceFaultFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_KHR: mvk_out_VkPhysicalDeviceFaultFeaturesKHR(d, (VkPhysicalDeviceFaultFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceFaultPropertiesKHR(d, (VkPhysicalDeviceFaultPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_LIBRARY_GROUP_HANDLES_FEATURES_KHR: mvk_out_VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR(d, (VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_BUILTINS_PROPERTIES_ARM: mvk_out_VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM(d, (VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_BUILTINS_FEATURES_ARM: mvk_out_VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM(d, (VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAME_BOUNDARY_FEATURES_EXT: mvk_out_VkPhysicalDeviceFrameBoundaryFeaturesEXT(d, (VkPhysicalDeviceFrameBoundaryFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT: mvk_out_VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT(d, (VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INTERNALLY_SYNCHRONIZED_QUEUES_FEATURES_KHR: mvk_out_VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR(d, (VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT: mvk_out_VkPhysicalDeviceDepthBiasControlFeaturesEXT(d, (VkPhysicalDeviceDepthBiasControlFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_EXT: mvk_out_VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT(d, (VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV: mvk_out_VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV(d, (VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT(d, (VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_NV: mvk_out_VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV(d, (VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_SPARSE_ADDRESS_SPACE_FEATURES_NV: mvk_out_VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV(d, (VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_SPARSE_ADDRESS_SPACE_PROPERTIES_NV: mvk_out_VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV(d, (VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PER_VIEW_VIEWPORTS_FEATURES_QCOM: mvk_out_VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM(d, (VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR: mvk_out_VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR(d, (VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_ARM: mvk_out_VkPhysicalDeviceShaderCorePropertiesARM(d, (VkPhysicalDeviceShaderCorePropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PER_VIEW_RENDER_AREAS_FEATURES_QCOM: mvk_out_VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM(d, (VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderObjectFeaturesEXT(d, (VkPhysicalDeviceShaderObjectFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceShaderObjectPropertiesEXT(d, (VkPhysicalDeviceShaderObjectPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TILE_IMAGE_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderTileImageFeaturesEXT(d, (VkPhysicalDeviceShaderTileImageFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TILE_IMAGE_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceShaderTileImagePropertiesEXT(d, (VkPhysicalDeviceShaderTileImagePropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR: mvk_out_VkPhysicalDeviceCooperativeMatrixFeaturesKHR(d, (VkPhysicalDeviceCooperativeMatrixFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceCooperativeMatrixPropertiesKHR(d, (VkPhysicalDeviceCooperativeMatrixPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_CONVERSION_FEATURES_QCOM: mvk_out_VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM(d, (VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUBIC_CLAMP_FEATURES_QCOM: mvk_out_VkPhysicalDeviceCubicClampFeaturesQCOM(d, (VkPhysicalDeviceCubicClampFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_DEGAMMA_FEATURES_QCOM: mvk_out_VkPhysicalDeviceYcbcrDegammaFeaturesQCOM(d, (VkPhysicalDeviceYcbcrDegammaFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUBIC_WEIGHTS_FEATURES_QCOM: mvk_out_VkPhysicalDeviceCubicWeightsFeaturesQCOM(d, (VkPhysicalDeviceCubicWeightsFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_2_FEATURES_QCOM: mvk_out_VkPhysicalDeviceImageProcessing2FeaturesQCOM(d, (VkPhysicalDeviceImageProcessing2FeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_2_PROPERTIES_QCOM: mvk_out_VkPhysicalDeviceImageProcessing2PropertiesQCOM(d, (VkPhysicalDeviceImageProcessing2PropertiesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_3_FEATURES_QCOM: mvk_out_VkPhysicalDeviceImageProcessing3FeaturesQCOM(d, (VkPhysicalDeviceImageProcessing3FeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_POOL_OVERALLOCATION_FEATURES_NV: mvk_out_VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV(d, (VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_DRIVER_PROPERTIES_MSFT: mvk_out_VkPhysicalDeviceLayeredDriverPropertiesMSFT(d, (VkPhysicalDeviceLayeredDriverPropertiesMSFT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PER_STAGE_DESCRIPTOR_SET_FEATURES_NV: mvk_out_VkPhysicalDevicePerStageDescriptorSetFeaturesNV(d, (VkPhysicalDevicePerStageDescriptorSetFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCHEDULING_CONTROLS_FEATURES_ARM: mvk_out_VkPhysicalDeviceSchedulingControlsFeaturesARM(d, (VkPhysicalDeviceSchedulingControlsFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCHEDULING_CONTROLS_PROPERTIES_ARM: mvk_out_VkPhysicalDeviceSchedulingControlsPropertiesARM(d, (VkPhysicalDeviceSchedulingControlsPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCHEDULING_CONTROLS_DISPATCH_PARAMETERS_PROPERTIES_ARM: mvk_out_VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM(d, (VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RELAXED_LINE_RASTERIZATION_FEATURES_IMG: mvk_out_VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG(d, (VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RENDER_PASS_STRIPED_FEATURES_ARM: mvk_out_VkPhysicalDeviceRenderPassStripedFeaturesARM(d, (VkPhysicalDeviceRenderPassStripedFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RENDER_PASS_STRIPED_PROPERTIES_ARM: mvk_out_VkPhysicalDeviceRenderPassStripedPropertiesARM(d, (VkPhysicalDeviceRenderPassStripedPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_OPACITY_MICROMAP_FEATURES_ARM: mvk_out_VkPhysicalDevicePipelineOpacityMicromapFeaturesARM(d, (VkPhysicalDevicePipelineOpacityMicromapFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MAXIMAL_RECONVERGENCE_FEATURES_KHR: mvk_out_VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR(d, (VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES: mvk_out_VkPhysicalDeviceShaderSubgroupRotateFeatures(d, (VkPhysicalDeviceShaderSubgroupRotateFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EXPECT_ASSUME_FEATURES: mvk_out_VkPhysicalDeviceShaderExpectAssumeFeatures(d, (VkPhysicalDeviceShaderExpectAssumeFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES: mvk_out_VkPhysicalDeviceShaderFloatControls2Features(d, (VkPhysicalDeviceShaderFloatControls2Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES: mvk_out_VkPhysicalDeviceDynamicRenderingLocalReadFeatures(d, (VkPhysicalDeviceDynamicRenderingLocalReadFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_QUAD_CONTROL_FEATURES_KHR: mvk_out_VkPhysicalDeviceShaderQuadControlFeaturesKHR(d, (VkPhysicalDeviceShaderQuadControlFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT16_VECTOR_FEATURES_NV: mvk_out_VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV(d, (VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_FEATURES_EXT: mvk_out_VkPhysicalDeviceMapMemoryPlacedFeaturesEXT(d, (VkPhysicalDeviceMapMemoryPlacedFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceMapMemoryPlacedPropertiesEXT(d, (VkPhysicalDeviceMapMemoryPlacedPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_BFLOAT16_FEATURES_KHR: mvk_out_VkPhysicalDeviceShaderBfloat16FeaturesKHR(d, (VkPhysicalDeviceShaderBfloat16FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAW_ACCESS_CHAINS_FEATURES_NV: mvk_out_VkPhysicalDeviceRawAccessChainsFeaturesNV(d, (VkPhysicalDeviceRawAccessChainsFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMMAND_BUFFER_INHERITANCE_FEATURES_NV: mvk_out_VkPhysicalDeviceCommandBufferInheritanceFeaturesNV(d, (VkPhysicalDeviceCommandBufferInheritanceFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ALIGNMENT_CONTROL_FEATURES_MESA: mvk_out_VkPhysicalDeviceImageAlignmentControlFeaturesMESA(d, (VkPhysicalDeviceImageAlignmentControlFeaturesMESA *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ALIGNMENT_CONTROL_PROPERTIES_MESA: mvk_out_VkPhysicalDeviceImageAlignmentControlPropertiesMESA(d, (VkPhysicalDeviceImageAlignmentControlPropertiesMESA *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_REPLICATED_COMPOSITES_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT(d, (VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR: mvk_out_VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR(d, (VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_FEATURES_NV: mvk_out_VkPhysicalDeviceCooperativeMatrix2FeaturesNV(d, (VkPhysicalDeviceCooperativeMatrix2FeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_PROPERTIES_NV: mvk_out_VkPhysicalDeviceCooperativeMatrix2PropertiesNV(d, (VkPhysicalDeviceCooperativeMatrix2PropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_DECODE_VECTOR_FEATURES_NV: mvk_out_VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV(d, (VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HDR_VIVID_FEATURES_HUAWEI: mvk_out_VkPhysicalDeviceHdrVividFeaturesHUAWEI(d, (VkPhysicalDeviceHdrVividFeaturesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_ROBUSTNESS_FEATURES_EXT: mvk_out_VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT(d, (VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLAMP_ZERO_ONE_FEATURES_KHR: mvk_out_VkPhysicalDeviceDepthClampZeroOneFeaturesKHR(d, (VkPhysicalDeviceDepthClampZeroOneFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV: mvk_out_VkPhysicalDeviceCooperativeVectorFeaturesNV(d, (VkPhysicalDeviceCooperativeVectorFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_PROPERTIES_NV: mvk_out_VkPhysicalDeviceCooperativeVectorPropertiesNV(d, (VkPhysicalDeviceCooperativeVectorPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_LAYERED_PROPERTIES_VALVE: mvk_out_VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE(d, (VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_LAYERED_FEATURES_VALVE: mvk_out_VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE(d, (VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_METERING_FEATURES_NV: mvk_out_VkPhysicalDevicePresentMeteringFeaturesNV(d, (VkPhysicalDevicePresentMeteringFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNIFORM_BUFFER_UNSIZED_ARRAY_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT(d, (VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE: mvk_out_VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE(d, (VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_RESTART_INDEX_FEATURES_EXT: mvk_out_VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT(d, (VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FORMAT_PACK_FEATURES_ARM: mvk_out_VkPhysicalDeviceFormatPackFeaturesARM(d, (VkPhysicalDeviceFormatPackFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_THROTTLE_HINT_FEATURES_SEC: mvk_out_VkPhysicalDeviceThrottleHintFeaturesSEC(d, (VkPhysicalDeviceThrottleHintFeaturesSEC *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderFloat8FeaturesEXT(d, (VkPhysicalDeviceShaderFloat8FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CACHE_INCREMENTAL_MODE_FEATURES_SEC: mvk_out_VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC(d, (VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DATA_GRAPH_MODEL_FEATURES_QCOM: mvk_out_VkPhysicalDeviceDataGraphModelFeaturesQCOM(d, (VkPhysicalDeviceDataGraphModelFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR: mvk_out_VkPhysicalDeviceShaderUntypedPointersFeaturesKHR(d, (VkPhysicalDeviceShaderUntypedPointersFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_ENCODE_RGB_CONVERSION_FEATURES_VALVE: mvk_out_VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE(d, (VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_64_BIT_INDEXING_FEATURES_EXT: mvk_out_VkPhysicalDeviceShader64BitIndexingFeaturesEXT(d, (VkPhysicalDeviceShader64BitIndexingFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_QUEUE_PERF_HINT_FEATURES_QCOM: mvk_out_VkPhysicalDeviceQueuePerfHintFeaturesQCOM(d, (VkPhysicalDeviceQueuePerfHintFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_QUEUE_PERF_HINT_PROPERTIES_QCOM: mvk_out_VkPhysicalDeviceQueuePerfHintPropertiesQCOM(d, (VkPhysicalDeviceQueuePerfHintPropertiesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_COUNTERS_BY_REGION_FEATURES_ARM: mvk_out_VkPhysicalDevicePerformanceCountersByRegionFeaturesARM(d, (VkPhysicalDevicePerformanceCountersByRegionFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_COUNTERS_BY_REGION_PROPERTIES_ARM: mvk_out_VkPhysicalDevicePerformanceCountersByRegionPropertiesARM(d, (VkPhysicalDevicePerformanceCountersByRegionPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_OCCUPANCY_PRIORITY_FEATURES_NV: mvk_out_VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV(d, (VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_LONG_VECTOR_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderLongVectorFeaturesEXT(d, (VkPhysicalDeviceShaderLongVectorFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_LONG_VECTOR_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceShaderLongVectorPropertiesEXT(d, (VkPhysicalDeviceShaderLongVectorPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_3D_FEATURES_EXT: mvk_out_VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT(d, (VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_PARTITIONED_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT(d, (VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_SUBSAMPLED_IMAGE_FORMAT_PROPERTIES_EXT: mvk_out_VkSubsampledImageFormatPropertiesEXT(d, (VkSubsampledImageFormatPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SPLIT_BARRIER_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderSplitBarrierFeaturesEXT(d, (VkPhysicalDeviceShaderSplitBarrierFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SPLIT_BARRIER_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceShaderSplitBarrierPropertiesEXT(d, (VkPhysicalDeviceShaderSplitBarrierPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT: mvk_out_VkPhysicalDeviceDescriptorHeapFeaturesEXT(d, (VkPhysicalDeviceDescriptorHeapFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT: mvk_out_VkPhysicalDeviceDescriptorHeapPropertiesEXT(d, (VkPhysicalDeviceDescriptorHeapPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_TENSOR_PROPERTIES_ARM: mvk_out_VkPhysicalDeviceDescriptorHeapTensorPropertiesARM(d, (VkPhysicalDeviceDescriptorHeapTensorPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INSTRUMENTATION_FEATURES_ARM: mvk_out_VkPhysicalDeviceShaderInstrumentationFeaturesARM(d, (VkPhysicalDeviceShaderInstrumentationFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INSTRUMENTATION_PROPERTIES_ARM: mvk_out_VkPhysicalDeviceShaderInstrumentationPropertiesARM(d, (VkPhysicalDeviceShaderInstrumentationPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_ADDRESS_COMMANDS_FEATURES_KHR: mvk_out_VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR(d, (VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CONSTANT_DATA_FEATURES_KHR: mvk_out_VkPhysicalDeviceShaderConstantDataFeaturesKHR(d, (VkPhysicalDeviceShaderConstantDataFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ABORT_FEATURES_KHR: mvk_out_VkPhysicalDeviceShaderAbortFeaturesKHR(d, (VkPhysicalDeviceShaderAbortFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ABORT_PROPERTIES_KHR: mvk_out_VkPhysicalDeviceShaderAbortPropertiesKHR(d, (VkPhysicalDeviceShaderAbortPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_DEVICE_FAULT_SHADER_ABORT_MESSAGE_INFO_KHR: mvk_out_VkDeviceFaultShaderAbortMessageInfoKHR(d, (VkDeviceFaultShaderAbortMessageInfoKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_TILING_CONTROL_FEATURES_EXT: mvk_out_VkPhysicalDeviceImageTilingControlFeaturesEXT(d, (VkPhysicalDeviceImageTilingControlFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OCP_MICROSCALING_TYPES_FEATURES_EXT: mvk_out_VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT(d, (VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_MAINTENANCE_1_FEATURES_EXT: mvk_out_VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT(d, (VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_ALLOCATION_ALIGNMENT_FEATURES_VALVE: mvk_out_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE(d, (VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_ALLOCATION_ALIGNMENT_PROPERTIES_VALVE: mvk_out_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE(d, (VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INFO_PROPERTIES_INTEL: mvk_out_VkPhysicalDeviceInfoPropertiesINTEL(d, (VkPhysicalDeviceInfoPropertiesINTEL *)s); break;
    default: break;
    }
}
