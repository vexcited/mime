/* generated from vk.xml, do not edit. */
#include "mvk_host.h"

static void reply_VkPhysicalDeviceFeatures(MvkEncoder *e, const VkPhysicalDeviceFeatures *s);
static void reply_VkFormatProperties(MvkEncoder *e, const VkFormatProperties *s);
static void reply_VkImageFormatProperties(MvkEncoder *e, const VkImageFormatProperties *s);
static void reply_VkExtent3D(MvkEncoder *e, const VkExtent3D *s);
static void reply_VkPhysicalDeviceProperties(MvkEncoder *e, const VkPhysicalDeviceProperties *s);
static void reply_VkPhysicalDeviceLimits(MvkEncoder *e, const VkPhysicalDeviceLimits *s);
static void reply_VkPhysicalDeviceSparseProperties(MvkEncoder *e, const VkPhysicalDeviceSparseProperties *s);
static void reply_VkQueueFamilyProperties(MvkEncoder *e, const VkQueueFamilyProperties *s);
static void reply_VkPhysicalDeviceMemoryProperties(MvkEncoder *e, const VkPhysicalDeviceMemoryProperties *s);
static void reply_VkMemoryType(MvkEncoder *e, const VkMemoryType *s);
static void reply_VkMemoryHeap(MvkEncoder *e, const VkMemoryHeap *s);
static void reply_VkMemoryRequirements(MvkEncoder *e, const VkMemoryRequirements *s);
static void reply_VkSparseImageMemoryRequirements(MvkEncoder *e, const VkSparseImageMemoryRequirements *s);
static void reply_VkSparseImageFormatProperties(MvkEncoder *e, const VkSparseImageFormatProperties *s);
static void reply_VkSubresourceLayout(MvkEncoder *e, const VkSubresourceLayout *s);
static void reply_VkExtent2D(MvkEncoder *e, const VkExtent2D *s);
static void reply_VkMemoryRequirements2(MvkEncoder *e, const VkMemoryRequirements2 *s);
static void reply_VkSparseImageMemoryRequirements2(MvkEncoder *e, const VkSparseImageMemoryRequirements2 *s);
static void reply_VkPhysicalDeviceFeatures2(MvkEncoder *e, const VkPhysicalDeviceFeatures2 *s);
static void reply_VkPhysicalDeviceProperties2(MvkEncoder *e, const VkPhysicalDeviceProperties2 *s);
static void reply_VkFormatProperties2(MvkEncoder *e, const VkFormatProperties2 *s);
static void reply_VkImageFormatProperties2(MvkEncoder *e, const VkImageFormatProperties2 *s);
static void reply_VkQueueFamilyProperties2(MvkEncoder *e, const VkQueueFamilyProperties2 *s);
static void reply_VkPhysicalDeviceMemoryProperties2(MvkEncoder *e, const VkPhysicalDeviceMemoryProperties2 *s);
static void reply_VkSparseImageFormatProperties2(MvkEncoder *e, const VkSparseImageFormatProperties2 *s);
static void reply_VkExternalBufferProperties(MvkEncoder *e, const VkExternalBufferProperties *s);
static void reply_VkExternalMemoryProperties(MvkEncoder *e, const VkExternalMemoryProperties *s);
static void reply_VkExternalFenceProperties(MvkEncoder *e, const VkExternalFenceProperties *s);
static void reply_VkExternalSemaphoreProperties(MvkEncoder *e, const VkExternalSemaphoreProperties *s);
static void reply_VkDescriptorSetLayoutSupport(MvkEncoder *e, const VkDescriptorSetLayoutSupport *s);
static void reply_VkPhysicalDeviceToolProperties(MvkEncoder *e, const VkPhysicalDeviceToolProperties *s);
static void reply_VkSubresourceLayout2(MvkEncoder *e, const VkSubresourceLayout2 *s);
static void reply_VkImageViewAddressPropertiesNVX(MvkEncoder *e, const VkImageViewAddressPropertiesNVX *s);
static void reply_VkExternalImageFormatPropertiesNV(MvkEncoder *e, const VkExternalImageFormatPropertiesNV *s);
static void reply_VkPerformanceCounterKHR(MvkEncoder *e, const VkPerformanceCounterKHR *s);
static void reply_VkPerformanceCounterDescriptionKHR(MvkEncoder *e, const VkPerformanceCounterDescriptionKHR *s);
static void reply_VkGpaDeviceClockModeInfoAMD(MvkEncoder *e, const VkGpaDeviceClockModeInfoAMD *s);
static void reply_VkGpaDeviceGetClockInfoAMD(MvkEncoder *e, const VkGpaDeviceGetClockInfoAMD *s);
static void reply_VkHostAddressRangeEXT(MvkEncoder *e, const VkHostAddressRangeEXT *s);
static void reply_VkMultisamplePropertiesEXT(MvkEncoder *e, const VkMultisamplePropertiesEXT *s);
static void reply_VkAccelerationStructureBuildSizesInfoKHR(MvkEncoder *e, const VkAccelerationStructureBuildSizesInfoKHR *s);
static void reply_VkImageDrmFormatModifierPropertiesEXT(MvkEncoder *e, const VkImageDrmFormatModifierPropertiesEXT *s);
static void reply_VkPerformanceValueINTEL(MvkEncoder *e, const VkPerformanceValueINTEL *s);
static void reply_VkPhysicalDeviceFragmentShadingRateKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRateKHR *s);
static void reply_VkCooperativeMatrixPropertiesNV(MvkEncoder *e, const VkCooperativeMatrixPropertiesNV *s);
static void reply_VkFramebufferMixedSamplesCombinationNV(MvkEncoder *e, const VkFramebufferMixedSamplesCombinationNV *s);
static void reply_VkPipelineExecutablePropertiesKHR(MvkEncoder *e, const VkPipelineExecutablePropertiesKHR *s);
static void reply_VkPipelineExecutableStatisticKHR(MvkEncoder *e, const VkPipelineExecutableStatisticKHR *s);
static void reply_VkPipelineExecutableInternalRepresentationKHR(MvkEncoder *e, const VkPipelineExecutableInternalRepresentationKHR *s);
static void reply_VkBaseOutStructure(MvkEncoder *e, const VkBaseOutStructure *s);
static void reply_VkMicromapBuildSizesInfoEXT(MvkEncoder *e, const VkMicromapBuildSizesInfoEXT *s);
static void reply_VkDescriptorSetLayoutHostMappingInfoVALVE(MvkEncoder *e, const VkDescriptorSetLayoutHostMappingInfoVALVE *s);
static void reply_VkShaderModuleIdentifierEXT(MvkEncoder *e, const VkShaderModuleIdentifierEXT *s);
static void reply_VkOpticalFlowImageFormatPropertiesNV(MvkEncoder *e, const VkOpticalFlowImageFormatPropertiesNV *s);
static void reply_VkCooperativeVectorPropertiesNV(MvkEncoder *e, const VkCooperativeVectorPropertiesNV *s);
static void reply_VkCooperativeMatrixPropertiesKHR(MvkEncoder *e, const VkCooperativeMatrixPropertiesKHR *s);
static void reply_VkDeviceFaultInfoKHR(MvkEncoder *e, const VkDeviceFaultInfoKHR *s);
static void reply_VkDeviceFaultAddressInfoKHR(MvkEncoder *e, const VkDeviceFaultAddressInfoKHR *s);
static void reply_VkDeviceFaultVendorInfoKHR(MvkEncoder *e, const VkDeviceFaultVendorInfoKHR *s);
static void reply_VkDeviceFaultDebugInfoKHR(MvkEncoder *e, const VkDeviceFaultDebugInfoKHR *s);
static void reply_VkCooperativeMatrixFlexibleDimensionsPropertiesNV(MvkEncoder *e, const VkCooperativeMatrixFlexibleDimensionsPropertiesNV *s);
static void reply_VkPerformanceCounterARM(MvkEncoder *e, const VkPerformanceCounterARM *s);
static void reply_VkPerformanceCounterDescriptionARM(MvkEncoder *e, const VkPerformanceCounterDescriptionARM *s);
static void reply_VkShaderInstrumentationMetricDescriptionARM(MvkEncoder *e, const VkShaderInstrumentationMetricDescriptionARM *s);
static void reply_VkCooperativeMatrixProperties2EXT(MvkEncoder *e, const VkCooperativeMatrixProperties2EXT *s);
static void reply_VkPhysicalDeviceGroupProperties(MvkEncoder *e, const VkPhysicalDeviceGroupProperties *s);
static void reply_VkLayerProperties(MvkEncoder *e, const VkLayerProperties *s);
static void reply_VkExtensionProperties(MvkEncoder *e, const VkExtensionProperties *s);
static void reply_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV *s);
static void reply_VkPhysicalDevicePushConstantBankFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePushConstantBankFeaturesNV *s);
static void reply_VkPhysicalDevicePushConstantBankPropertiesNV(MvkEncoder *e, const VkPhysicalDevicePushConstantBankPropertiesNV *s);
static void reply_VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV *s);
static void reply_VkPhysicalDevicePrivateDataFeatures(MvkEncoder *e, const VkPhysicalDevicePrivateDataFeatures *s);
static void reply_VkPhysicalDevicePrivateDataBaseHandleFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePrivateDataBaseHandleFeaturesNV *s);
static void reply_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *s);
static void reply_VkPhysicalDeviceClusterAccelerationStructureFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceClusterAccelerationStructureFeaturesNV *s);
static void reply_VkPhysicalDeviceClusterAccelerationStructurePropertiesNV(MvkEncoder *e, const VkPhysicalDeviceClusterAccelerationStructurePropertiesNV *s);
static void reply_VkPhysicalDeviceMultiDrawPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMultiDrawPropertiesEXT *s);
static void reply_VkPhysicalDevicePushDescriptorProperties(MvkEncoder *e, const VkPhysicalDevicePushDescriptorProperties *s);
static void reply_VkPhysicalDeviceDriverProperties(MvkEncoder *e, const VkPhysicalDeviceDriverProperties *s);
static void reply_VkConformanceVersion(MvkEncoder *e, const VkConformanceVersion *s);
static void reply_VkPhysicalDeviceVariablePointersFeatures(MvkEncoder *e, const VkPhysicalDeviceVariablePointersFeatures *s);
static void reply_VkExternalImageFormatProperties(MvkEncoder *e, const VkExternalImageFormatProperties *s);
static void reply_VkPhysicalDeviceIDProperties(MvkEncoder *e, const VkPhysicalDeviceIDProperties *s);
static void reply_VkPhysicalDeviceMultiviewFeatures(MvkEncoder *e, const VkPhysicalDeviceMultiviewFeatures *s);
static void reply_VkPhysicalDeviceMultiviewProperties(MvkEncoder *e, const VkPhysicalDeviceMultiviewProperties *s);
static void reply_VkPhysicalDeviceDiscardRectanglePropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDiscardRectanglePropertiesEXT *s);
static void reply_VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX(MvkEncoder *e, const VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX *s);
static void reply_VkPhysicalDevice16BitStorageFeatures(MvkEncoder *e, const VkPhysicalDevice16BitStorageFeatures *s);
static void reply_VkPhysicalDeviceSubgroupProperties(MvkEncoder *e, const VkPhysicalDeviceSubgroupProperties *s);
static void reply_VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *s);
static void reply_VkPhysicalDevicePointClippingProperties(MvkEncoder *e, const VkPhysicalDevicePointClippingProperties *s);
static void reply_VkMemoryDedicatedRequirements(MvkEncoder *e, const VkMemoryDedicatedRequirements *s);
static void reply_VkPhysicalDeviceSamplerYcbcrConversionFeatures(MvkEncoder *e, const VkPhysicalDeviceSamplerYcbcrConversionFeatures *s);
static void reply_VkSamplerYcbcrConversionImageFormatProperties(MvkEncoder *e, const VkSamplerYcbcrConversionImageFormatProperties *s);
static void reply_VkTextureLODGatherFormatPropertiesAMD(MvkEncoder *e, const VkTextureLODGatherFormatPropertiesAMD *s);
static void reply_VkPhysicalDeviceProtectedMemoryFeatures(MvkEncoder *e, const VkPhysicalDeviceProtectedMemoryFeatures *s);
static void reply_VkPhysicalDeviceProtectedMemoryProperties(MvkEncoder *e, const VkPhysicalDeviceProtectedMemoryProperties *s);
static void reply_VkPhysicalDeviceSamplerFilterMinmaxProperties(MvkEncoder *e, const VkPhysicalDeviceSamplerFilterMinmaxProperties *s);
static void reply_VkPhysicalDeviceSampleLocationsPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceSampleLocationsPropertiesEXT *s);
static void reply_VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT *s);
static void reply_VkPhysicalDeviceMultiDrawFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMultiDrawFeaturesEXT *s);
static void reply_VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT *s);
static void reply_VkPhysicalDeviceInlineUniformBlockFeatures(MvkEncoder *e, const VkPhysicalDeviceInlineUniformBlockFeatures *s);
static void reply_VkPhysicalDeviceInlineUniformBlockProperties(MvkEncoder *e, const VkPhysicalDeviceInlineUniformBlockProperties *s);
static void reply_VkPhysicalDeviceMaintenance3Properties(MvkEncoder *e, const VkPhysicalDeviceMaintenance3Properties *s);
static void reply_VkPhysicalDeviceMaintenance4Features(MvkEncoder *e, const VkPhysicalDeviceMaintenance4Features *s);
static void reply_VkPhysicalDeviceMaintenance4Properties(MvkEncoder *e, const VkPhysicalDeviceMaintenance4Properties *s);
static void reply_VkPhysicalDeviceMaintenance5Features(MvkEncoder *e, const VkPhysicalDeviceMaintenance5Features *s);
static void reply_VkPhysicalDeviceMaintenance5Properties(MvkEncoder *e, const VkPhysicalDeviceMaintenance5Properties *s);
static void reply_VkPhysicalDeviceMaintenance6Features(MvkEncoder *e, const VkPhysicalDeviceMaintenance6Features *s);
static void reply_VkPhysicalDeviceMaintenance6Properties(MvkEncoder *e, const VkPhysicalDeviceMaintenance6Properties *s);
static void reply_VkPhysicalDeviceMaintenance7FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance7FeaturesKHR *s);
static void reply_VkPhysicalDeviceMaintenance7PropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance7PropertiesKHR *s);
static void reply_VkPhysicalDeviceLayeredApiPropertiesListKHR(MvkEncoder *e, const VkPhysicalDeviceLayeredApiPropertiesListKHR *s);
static void reply_VkPhysicalDeviceLayeredApiPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceLayeredApiPropertiesKHR *s);
static void reply_VkPhysicalDeviceLayeredApiVulkanPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceLayeredApiVulkanPropertiesKHR *s);
static void reply_VkPhysicalDeviceMaintenance8FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance8FeaturesKHR *s);
static void reply_VkPhysicalDeviceMaintenance9FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance9FeaturesKHR *s);
static void reply_VkPhysicalDeviceMaintenance9PropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance9PropertiesKHR *s);
static void reply_VkPhysicalDeviceMaintenance11FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance11FeaturesKHR *s);
static void reply_VkPhysicalDeviceMaintenance10PropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance10PropertiesKHR *s);
static void reply_VkPhysicalDeviceMaintenance10FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance10FeaturesKHR *s);
static void reply_VkQueueFamilyOwnershipTransferPropertiesKHR(MvkEncoder *e, const VkQueueFamilyOwnershipTransferPropertiesKHR *s);
static void reply_VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR(MvkEncoder *e, const VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR *s);
static void reply_VkPhysicalDeviceShaderDrawParametersFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderDrawParametersFeatures *s);
static void reply_VkPhysicalDeviceShaderFloat16Int8Features(MvkEncoder *e, const VkPhysicalDeviceShaderFloat16Int8Features *s);
static void reply_VkPhysicalDeviceFloatControlsProperties(MvkEncoder *e, const VkPhysicalDeviceFloatControlsProperties *s);
static void reply_VkPhysicalDeviceHostQueryResetFeatures(MvkEncoder *e, const VkPhysicalDeviceHostQueryResetFeatures *s);
static void reply_VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM *s);
static void reply_VkPhysicalDeviceGlobalPriorityQueryFeatures(MvkEncoder *e, const VkPhysicalDeviceGlobalPriorityQueryFeatures *s);
static void reply_VkQueueFamilyGlobalPriorityProperties(MvkEncoder *e, const VkQueueFamilyGlobalPriorityProperties *s);
static void reply_VkPhysicalDeviceDeviceMemoryReportFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDeviceMemoryReportFeaturesEXT *s);
static void reply_VkPhysicalDeviceConservativeRasterizationPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceConservativeRasterizationPropertiesEXT *s);
static void reply_VkPhysicalDeviceShaderCorePropertiesAMD(MvkEncoder *e, const VkPhysicalDeviceShaderCorePropertiesAMD *s);
static void reply_VkPhysicalDeviceShaderCoreProperties2AMD(MvkEncoder *e, const VkPhysicalDeviceShaderCoreProperties2AMD *s);
static void reply_VkPhysicalDeviceDescriptorIndexingFeatures(MvkEncoder *e, const VkPhysicalDeviceDescriptorIndexingFeatures *s);
static void reply_VkPhysicalDeviceDescriptorIndexingProperties(MvkEncoder *e, const VkPhysicalDeviceDescriptorIndexingProperties *s);
static void reply_VkDescriptorSetVariableDescriptorCountLayoutSupport(MvkEncoder *e, const VkDescriptorSetVariableDescriptorCountLayoutSupport *s);
static void reply_VkPhysicalDeviceTimelineSemaphoreFeatures(MvkEncoder *e, const VkPhysicalDeviceTimelineSemaphoreFeatures *s);
static void reply_VkPhysicalDeviceTimelineSemaphoreProperties(MvkEncoder *e, const VkPhysicalDeviceTimelineSemaphoreProperties *s);
static void reply_VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *s);
static void reply_VkPhysicalDeviceVertexAttributeDivisorProperties(MvkEncoder *e, const VkPhysicalDeviceVertexAttributeDivisorProperties *s);
static void reply_VkPhysicalDevicePCIBusInfoPropertiesEXT(MvkEncoder *e, const VkPhysicalDevicePCIBusInfoPropertiesEXT *s);
static void reply_VkPhysicalDevice8BitStorageFeatures(MvkEncoder *e, const VkPhysicalDevice8BitStorageFeatures *s);
static void reply_VkPhysicalDeviceConditionalRenderingFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceConditionalRenderingFeaturesEXT *s);
static void reply_VkPhysicalDeviceVulkanMemoryModelFeatures(MvkEncoder *e, const VkPhysicalDeviceVulkanMemoryModelFeatures *s);
static void reply_VkPhysicalDeviceShaderAtomicInt64Features(MvkEncoder *e, const VkPhysicalDeviceShaderAtomicInt64Features *s);
static void reply_VkPhysicalDeviceShaderAtomicFloatFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *s);
static void reply_VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT *s);
static void reply_VkPhysicalDeviceVertexAttributeDivisorFeatures(MvkEncoder *e, const VkPhysicalDeviceVertexAttributeDivisorFeatures *s);
static void reply_VkQueueFamilyCheckpointPropertiesNV(MvkEncoder *e, const VkQueueFamilyCheckpointPropertiesNV *s);
static void reply_VkPhysicalDeviceDepthStencilResolveProperties(MvkEncoder *e, const VkPhysicalDeviceDepthStencilResolveProperties *s);
static void reply_VkPhysicalDeviceASTCDecodeFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceASTCDecodeFeaturesEXT *s);
static void reply_VkPhysicalDeviceTransformFeedbackFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceTransformFeedbackFeaturesEXT *s);
static void reply_VkPhysicalDeviceTransformFeedbackPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceTransformFeedbackPropertiesEXT *s);
static void reply_VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV *s);
static void reply_VkPhysicalDeviceExclusiveScissorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceExclusiveScissorFeaturesNV *s);
static void reply_VkPhysicalDeviceCornerSampledImageFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCornerSampledImageFeaturesNV *s);
static void reply_VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR *s);
static void reply_VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR *s);
static void reply_VkPhysicalDeviceShaderImageFootprintFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceShaderImageFootprintFeaturesNV *s);
static void reply_VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV *s);
static void reply_VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR *s);
static void reply_VkPhysicalDeviceCopyMemoryIndirectFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCopyMemoryIndirectFeaturesNV *s);
static void reply_VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR *s);
static void reply_VkPhysicalDeviceMemoryDecompressionFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMemoryDecompressionFeaturesEXT *s);
static void reply_VkPhysicalDeviceMemoryDecompressionPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMemoryDecompressionPropertiesEXT *s);
static void reply_VkPhysicalDeviceShadingRateImageFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceShadingRateImageFeaturesNV *s);
static void reply_VkPhysicalDeviceShadingRateImagePropertiesNV(MvkEncoder *e, const VkPhysicalDeviceShadingRateImagePropertiesNV *s);
static void reply_VkPhysicalDeviceInvocationMaskFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceInvocationMaskFeaturesHUAWEI *s);
static void reply_VkPhysicalDeviceMeshShaderFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceMeshShaderFeaturesNV *s);
static void reply_VkPhysicalDeviceMeshShaderPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceMeshShaderPropertiesNV *s);
static void reply_VkPhysicalDeviceMeshShaderFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMeshShaderFeaturesEXT *s);
static void reply_VkPhysicalDeviceMeshShaderPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMeshShaderPropertiesEXT *s);
static void reply_VkPhysicalDeviceAccelerationStructureFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceAccelerationStructureFeaturesKHR *s);
static void reply_VkPhysicalDeviceRayTracingPipelineFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRayTracingPipelineFeaturesKHR *s);
static void reply_VkPhysicalDeviceRayQueryFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRayQueryFeaturesKHR *s);
static void reply_VkPhysicalDeviceAccelerationStructurePropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceAccelerationStructurePropertiesKHR *s);
static void reply_VkPhysicalDeviceRayTracingPipelinePropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceRayTracingPipelinePropertiesKHR *s);
static void reply_VkPhysicalDeviceRayTracingPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingPropertiesNV *s);
static void reply_VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR *s);
static void reply_VkDrmFormatModifierPropertiesListEXT(MvkEncoder *e, const VkDrmFormatModifierPropertiesListEXT *s);
static void reply_VkDrmFormatModifierPropertiesEXT(MvkEncoder *e, const VkDrmFormatModifierPropertiesEXT *s);
static void reply_VkPhysicalDeviceFragmentDensityMapFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapFeaturesEXT *s);
static void reply_VkPhysicalDeviceFragmentDensityMap2FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMap2FeaturesEXT *s);
static void reply_VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT *s);
static void reply_VkPhysicalDeviceFragmentDensityMapPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapPropertiesEXT *s);
static void reply_VkPhysicalDeviceFragmentDensityMap2PropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMap2PropertiesEXT *s);
static void reply_VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT *s);
static void reply_VkPhysicalDeviceScalarBlockLayoutFeatures(MvkEncoder *e, const VkPhysicalDeviceScalarBlockLayoutFeatures *s);
static void reply_VkPhysicalDeviceUniformBufferStandardLayoutFeatures(MvkEncoder *e, const VkPhysicalDeviceUniformBufferStandardLayoutFeatures *s);
static void reply_VkPhysicalDeviceDepthClipEnableFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDepthClipEnableFeaturesEXT *s);
static void reply_VkPhysicalDeviceMemoryBudgetPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMemoryBudgetPropertiesEXT *s);
static void reply_VkPhysicalDeviceMemoryPriorityFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMemoryPriorityFeaturesEXT *s);
static void reply_VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT *s);
static void reply_VkPhysicalDeviceBufferDeviceAddressFeatures(MvkEncoder *e, const VkPhysicalDeviceBufferDeviceAddressFeatures *s);
static void reply_VkPhysicalDeviceBufferDeviceAddressFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *s);
static void reply_VkFilterCubicImageViewImageFormatPropertiesEXT(MvkEncoder *e, const VkFilterCubicImageViewImageFormatPropertiesEXT *s);
static void reply_VkPhysicalDeviceImagelessFramebufferFeatures(MvkEncoder *e, const VkPhysicalDeviceImagelessFramebufferFeatures *s);
static void reply_VkPhysicalDeviceTextureCompressionASTCHDRFeatures(MvkEncoder *e, const VkPhysicalDeviceTextureCompressionASTCHDRFeatures *s);
static void reply_VkPhysicalDeviceCooperativeMatrixFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixFeaturesNV *s);
static void reply_VkPhysicalDeviceCooperativeMatrixPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixPropertiesNV *s);
static void reply_VkPhysicalDeviceYcbcrImageArraysFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *s);
static void reply_VkPipelineCreationFeedback(MvkEncoder *e, const VkPipelineCreationFeedback *s);
static void reply_VkPhysicalDevicePerformanceQueryFeaturesKHR(MvkEncoder *e, const VkPhysicalDevicePerformanceQueryFeaturesKHR *s);
static void reply_VkPhysicalDevicePerformanceQueryPropertiesKHR(MvkEncoder *e, const VkPhysicalDevicePerformanceQueryPropertiesKHR *s);
static void reply_VkPhysicalDeviceCoverageReductionModeFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCoverageReductionModeFeaturesNV *s);
static void reply_VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL(MvkEncoder *e, const VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *s);
static void reply_VkPhysicalDeviceShaderClockFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderClockFeaturesKHR *s);
static void reply_VkPhysicalDeviceIndexTypeUint8Features(MvkEncoder *e, const VkPhysicalDeviceIndexTypeUint8Features *s);
static void reply_VkPhysicalDeviceShaderSMBuiltinsPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceShaderSMBuiltinsPropertiesNV *s);
static void reply_VkPhysicalDeviceShaderSMBuiltinsFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceShaderSMBuiltinsFeaturesNV *s);
static void reply_VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT *s);
static void reply_VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures(MvkEncoder *e, const VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures *s);
static void reply_VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *s);
static void reply_VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR(MvkEncoder *e, const VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *s);
static void reply_VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *s);
static void reply_VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *s);
static void reply_VkPhysicalDeviceTexelBufferAlignmentProperties(MvkEncoder *e, const VkPhysicalDeviceTexelBufferAlignmentProperties *s);
static void reply_VkPhysicalDeviceSubgroupSizeControlFeatures(MvkEncoder *e, const VkPhysicalDeviceSubgroupSizeControlFeatures *s);
static void reply_VkPhysicalDeviceSubgroupSizeControlProperties(MvkEncoder *e, const VkPhysicalDeviceSubgroupSizeControlProperties *s);
static void reply_VkPhysicalDeviceSubpassShadingPropertiesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceSubpassShadingPropertiesHUAWEI *s);
static void reply_VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI *s);
static void reply_VkPhysicalDeviceLineRasterizationFeatures(MvkEncoder *e, const VkPhysicalDeviceLineRasterizationFeatures *s);
static void reply_VkPhysicalDeviceLineRasterizationProperties(MvkEncoder *e, const VkPhysicalDeviceLineRasterizationProperties *s);
static void reply_VkPhysicalDevicePipelineCreationCacheControlFeatures(MvkEncoder *e, const VkPhysicalDevicePipelineCreationCacheControlFeatures *s);
static void reply_VkPhysicalDeviceVulkan11Features(MvkEncoder *e, const VkPhysicalDeviceVulkan11Features *s);
static void reply_VkPhysicalDeviceVulkan11Properties(MvkEncoder *e, const VkPhysicalDeviceVulkan11Properties *s);
static void reply_VkPhysicalDeviceVulkan12Features(MvkEncoder *e, const VkPhysicalDeviceVulkan12Features *s);
static void reply_VkPhysicalDeviceVulkan12Properties(MvkEncoder *e, const VkPhysicalDeviceVulkan12Properties *s);
static void reply_VkPhysicalDeviceVulkan13Features(MvkEncoder *e, const VkPhysicalDeviceVulkan13Features *s);
static void reply_VkPhysicalDeviceVulkan13Properties(MvkEncoder *e, const VkPhysicalDeviceVulkan13Properties *s);
static void reply_VkPhysicalDeviceVulkan14Features(MvkEncoder *e, const VkPhysicalDeviceVulkan14Features *s);
static void reply_VkPhysicalDeviceVulkan14Properties(MvkEncoder *e, const VkPhysicalDeviceVulkan14Properties *s);
static void reply_VkPhysicalDeviceCoherentMemoryFeaturesAMD(MvkEncoder *e, const VkPhysicalDeviceCoherentMemoryFeaturesAMD *s);
static void reply_VkPhysicalDeviceGpaFeaturesAMD(MvkEncoder *e, const VkPhysicalDeviceGpaFeaturesAMD *s);
static void reply_VkPhysicalDeviceGpaPropertiesAMD(MvkEncoder *e, const VkPhysicalDeviceGpaPropertiesAMD *s);
static void reply_VkPhysicalDeviceGpaProperties2AMD(MvkEncoder *e, const VkPhysicalDeviceGpaProperties2AMD *s);
static void reply_VkPhysicalDeviceCustomBorderColorPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceCustomBorderColorPropertiesEXT *s);
static void reply_VkPhysicalDeviceCustomBorderColorFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceCustomBorderColorFeaturesEXT *s);
static void reply_VkPhysicalDeviceBorderColorSwizzleFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *s);
static void reply_VkPhysicalDeviceExtendedDynamicStateFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *s);
static void reply_VkPhysicalDeviceExtendedDynamicState2FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *s);
static void reply_VkPhysicalDeviceExtendedDynamicState3FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *s);
static void reply_VkPhysicalDeviceExtendedDynamicState3PropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceExtendedDynamicState3PropertiesEXT *s);
static void reply_VkPhysicalDeviceExtendedFlagsFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceExtendedFlagsFeaturesKHR *s);
static void reply_VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV *s);
static void reply_VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV(MvkEncoder *e, const VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV *s);
static void reply_VkPhysicalDeviceDiagnosticsConfigFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDiagnosticsConfigFeaturesNV *s);
static void reply_VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures(MvkEncoder *e, const VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures *s);
static void reply_VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR *s);
static void reply_VkPhysicalDeviceRobustness2FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRobustness2FeaturesKHR *s);
static void reply_VkPhysicalDeviceRobustness2PropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceRobustness2PropertiesKHR *s);
static void reply_VkPhysicalDeviceImageRobustnessFeatures(MvkEncoder *e, const VkPhysicalDeviceImageRobustnessFeatures *s);
static void reply_VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *s);
static void reply_VkPhysicalDevice4444FormatsFeaturesEXT(MvkEncoder *e, const VkPhysicalDevice4444FormatsFeaturesEXT *s);
static void reply_VkPhysicalDeviceSubpassShadingFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceSubpassShadingFeaturesHUAWEI *s);
static void reply_VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI *s);
static void reply_VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI *s);
static void reply_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *s);
static void reply_VkPhysicalDeviceFragmentShadingRateFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRateFeaturesKHR *s);
static void reply_VkPhysicalDeviceFragmentShadingRatePropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRatePropertiesKHR *s);
static void reply_VkPhysicalDeviceShaderTerminateInvocationFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderTerminateInvocationFeatures *s);
static void reply_VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV *s);
static void reply_VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV *s);
static void reply_VkPhysicalDeviceImage2DViewOf3DFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImage2DViewOf3DFeaturesEXT *s);
static void reply_VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT *s);
static void reply_VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT *s);
static void reply_VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT *s);
static void reply_VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT *s);
static void reply_VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *s);
static void reply_VkPhysicalDeviceDepthClipControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDepthClipControlFeaturesEXT *s);
static void reply_VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT *s);
static void reply_VkPhysicalDeviceCustomResolveFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceCustomResolveFeaturesEXT *s);
static void reply_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT *s);
static void reply_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT *s);
static void reply_VkPhysicalDeviceDepthClampControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDepthClampControlFeaturesEXT *s);
static void reply_VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *s);
static void reply_VkPhysicalDeviceExternalMemoryRDMAFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceExternalMemoryRDMAFeaturesNV *s);
static void reply_VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR *s);
static void reply_VkPhysicalDeviceColorWriteEnableFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceColorWriteEnableFeaturesEXT *s);
static void reply_VkQueueFamilyCheckpointProperties2NV(MvkEncoder *e, const VkQueueFamilyCheckpointProperties2NV *s);
static void reply_VkPhysicalDeviceSynchronization2Features(MvkEncoder *e, const VkPhysicalDeviceSynchronization2Features *s);
static void reply_VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR *s);
static void reply_VkPhysicalDeviceHostImageCopyFeatures(MvkEncoder *e, const VkPhysicalDeviceHostImageCopyFeatures *s);
static void reply_VkPhysicalDeviceHostImageCopyProperties(MvkEncoder *e, const VkPhysicalDeviceHostImageCopyProperties *s);
static void reply_VkSubresourceHostMemcpySize(MvkEncoder *e, const VkSubresourceHostMemcpySize *s);
static void reply_VkHostImageCopyDevicePerformanceQuery(MvkEncoder *e, const VkHostImageCopyDevicePerformanceQuery *s);
static void reply_VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT *s);
static void reply_VkPhysicalDeviceLegacyDitheringFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceLegacyDitheringFeaturesEXT *s);
static void reply_VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT *s);
static void reply_VkSubpassResolvePerformanceQueryEXT(MvkEncoder *e, const VkSubpassResolvePerformanceQueryEXT *s);
static void reply_VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT *s);
static void reply_VkPhysicalDevicePipelineProtectedAccessFeatures(MvkEncoder *e, const VkPhysicalDevicePipelineProtectedAccessFeatures *s);
static void reply_VkPhysicalDeviceInheritedViewportScissorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceInheritedViewportScissorFeaturesNV *s);
static void reply_VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT *s);
static void reply_VkPhysicalDeviceProvokingVertexFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceProvokingVertexFeaturesEXT *s);
static void reply_VkPhysicalDeviceProvokingVertexPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceProvokingVertexPropertiesEXT *s);
static void reply_VkPhysicalDeviceDescriptorBufferFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorBufferFeaturesEXT *s);
static void reply_VkPhysicalDeviceDescriptorBufferPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorBufferPropertiesEXT *s);
static void reply_VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT *s);
static void reply_VkPhysicalDeviceShaderIntegerDotProductFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderIntegerDotProductFeatures *s);
static void reply_VkPhysicalDeviceShaderIntegerDotProductProperties(MvkEncoder *e, const VkPhysicalDeviceShaderIntegerDotProductProperties *s);
static void reply_VkPhysicalDeviceDrmPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDrmPropertiesEXT *s);
static void reply_VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR *s);
static void reply_VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR *s);
static void reply_VkPhysicalDeviceShaderFmaFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderFmaFeaturesKHR *s);
static void reply_VkPhysicalDeviceRayTracingMotionBlurFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingMotionBlurFeaturesNV *s);
static void reply_VkPhysicalDeviceRayTracingValidationFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingValidationFeaturesNV *s);
static void reply_VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV *s);
static void reply_VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT *s);
static void reply_VkFormatProperties3(MvkEncoder *e, const VkFormatProperties3 *s);
static void reply_VkFormatProperties4KHR(MvkEncoder *e, const VkFormatProperties4KHR *s);
static void reply_VkDrmFormatModifierPropertiesList2EXT(MvkEncoder *e, const VkDrmFormatModifierPropertiesList2EXT *s);
static void reply_VkDrmFormatModifierProperties2EXT(MvkEncoder *e, const VkDrmFormatModifierProperties2EXT *s);
static void reply_VkPhysicalDeviceDynamicRenderingFeatures(MvkEncoder *e, const VkPhysicalDeviceDynamicRenderingFeatures *s);
static void reply_VkPhysicalDeviceImageViewMinLodFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageViewMinLodFeaturesEXT *s);
static void reply_VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT *s);
static void reply_VkPhysicalDeviceLinearColorAttachmentFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceLinearColorAttachmentFeaturesNV *s);
static void reply_VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT *s);
static void reply_VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *s);
static void reply_VkTensorExplicitTilingFormatPropertiesARM(MvkEncoder *e, const VkTensorExplicitTilingFormatPropertiesARM *s);
static void reply_VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE *s);
static void reply_VkPhysicalDeviceNestedCommandBufferFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceNestedCommandBufferFeaturesEXT *s);
static void reply_VkPhysicalDeviceNestedCommandBufferPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceNestedCommandBufferPropertiesEXT *s);
static void reply_VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *s);
static void reply_VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *s);
static void reply_VkPhysicalDeviceImageCompressionControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageCompressionControlFeaturesEXT *s);
static void reply_VkImageCompressionPropertiesEXT(MvkEncoder *e, const VkImageCompressionPropertiesEXT *s);
static void reply_VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT *s);
static void reply_VkRenderPassCreationFeedbackInfoEXT(MvkEncoder *e, const VkRenderPassCreationFeedbackInfoEXT *s);
static void reply_VkRenderPassSubpassFeedbackInfoEXT(MvkEncoder *e, const VkRenderPassSubpassFeedbackInfoEXT *s);
static void reply_VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT *s);
static void reply_VkPhysicalDeviceOpacityMicromapFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceOpacityMicromapFeaturesKHR *s);
static void reply_VkPhysicalDeviceOpacityMicromapFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceOpacityMicromapFeaturesEXT *s);
static void reply_VkPhysicalDeviceOpacityMicromapPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceOpacityMicromapPropertiesKHR *s);
static void reply_VkPhysicalDeviceOpacityMicromapPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceOpacityMicromapPropertiesEXT *s);
static void reply_VkPhysicalDevicePipelinePropertiesFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePipelinePropertiesFeaturesEXT *s);
static void reply_VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD(MvkEncoder *e, const VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD *s);
static void reply_VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT *s);
static void reply_VkPhysicalDevicePipelineRobustnessFeatures(MvkEncoder *e, const VkPhysicalDevicePipelineRobustnessFeatures *s);
static void reply_VkPhysicalDevicePipelineRobustnessProperties(MvkEncoder *e, const VkPhysicalDevicePipelineRobustnessProperties *s);
static void reply_VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM *s);
static void reply_VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM(MvkEncoder *e, const VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM *s);
static void reply_VkPhysicalDeviceImageProcessingFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessingFeaturesQCOM *s);
static void reply_VkPhysicalDeviceImageProcessingPropertiesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessingPropertiesQCOM *s);
static void reply_VkPhysicalDeviceAmigoProfilingFeaturesSEC(MvkEncoder *e, const VkPhysicalDeviceAmigoProfilingFeaturesSEC *s);
static void reply_VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT *s);
static void reply_VkPhysicalDeviceAddressBindingReportFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceAddressBindingReportFeaturesEXT *s);
static void reply_VkPhysicalDeviceOpticalFlowFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceOpticalFlowFeaturesNV *s);
static void reply_VkPhysicalDeviceOpticalFlowPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceOpticalFlowPropertiesNV *s);
static void reply_VkPhysicalDeviceFaultFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFaultFeaturesEXT *s);
static void reply_VkPhysicalDeviceFaultFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceFaultFeaturesKHR *s);
static void reply_VkPhysicalDeviceFaultPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceFaultPropertiesKHR *s);
static void reply_VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR(MvkEncoder *e, const VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR *s);
static void reply_VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM *s);
static void reply_VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM *s);
static void reply_VkPhysicalDeviceFrameBoundaryFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFrameBoundaryFeaturesEXT *s);
static void reply_VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT *s);
static void reply_VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR *s);
static void reply_VkPhysicalDeviceDepthBiasControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDepthBiasControlFeaturesEXT *s);
static void reply_VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT *s);
static void reply_VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV *s);
static void reply_VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT *s);
static void reply_VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV *s);
static void reply_VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV *s);
static void reply_VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV(MvkEncoder *e, const VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV *s);
static void reply_VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM *s);
static void reply_VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR *s);
static void reply_VkPhysicalDeviceShaderCorePropertiesARM(MvkEncoder *e, const VkPhysicalDeviceShaderCorePropertiesARM *s);
static void reply_VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM *s);
static void reply_VkPhysicalDeviceShaderObjectFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderObjectFeaturesEXT *s);
static void reply_VkPhysicalDeviceShaderObjectPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderObjectPropertiesEXT *s);
static void reply_VkPhysicalDeviceShaderTileImageFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderTileImageFeaturesEXT *s);
static void reply_VkPhysicalDeviceShaderTileImagePropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderTileImagePropertiesEXT *s);
static void reply_VkPhysicalDeviceCooperativeMatrixFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixFeaturesKHR *s);
static void reply_VkPhysicalDeviceCooperativeMatrixPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixPropertiesKHR *s);
static void reply_VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM *s);
static void reply_VkPhysicalDeviceCubicClampFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceCubicClampFeaturesQCOM *s);
static void reply_VkPhysicalDeviceYcbcrDegammaFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceYcbcrDegammaFeaturesQCOM *s);
static void reply_VkPhysicalDeviceCubicWeightsFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceCubicWeightsFeaturesQCOM *s);
static void reply_VkPhysicalDeviceImageProcessing2FeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessing2FeaturesQCOM *s);
static void reply_VkPhysicalDeviceImageProcessing2PropertiesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessing2PropertiesQCOM *s);
static void reply_VkPhysicalDeviceImageProcessing3FeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessing3FeaturesQCOM *s);
static void reply_VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV *s);
static void reply_VkPhysicalDeviceLayeredDriverPropertiesMSFT(MvkEncoder *e, const VkPhysicalDeviceLayeredDriverPropertiesMSFT *s);
static void reply_VkPhysicalDevicePerStageDescriptorSetFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePerStageDescriptorSetFeaturesNV *s);
static void reply_VkPhysicalDeviceSchedulingControlsFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceSchedulingControlsFeaturesARM *s);
static void reply_VkPhysicalDeviceSchedulingControlsPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceSchedulingControlsPropertiesARM *s);
static void reply_VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM *s);
static void reply_VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG(MvkEncoder *e, const VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG *s);
static void reply_VkPhysicalDeviceRenderPassStripedFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceRenderPassStripedFeaturesARM *s);
static void reply_VkPhysicalDeviceRenderPassStripedPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceRenderPassStripedPropertiesARM *s);
static void reply_VkPhysicalDevicePipelineOpacityMicromapFeaturesARM(MvkEncoder *e, const VkPhysicalDevicePipelineOpacityMicromapFeaturesARM *s);
static void reply_VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR *s);
static void reply_VkPhysicalDeviceShaderSubgroupRotateFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderSubgroupRotateFeatures *s);
static void reply_VkPhysicalDeviceShaderExpectAssumeFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderExpectAssumeFeatures *s);
static void reply_VkPhysicalDeviceShaderFloatControls2Features(MvkEncoder *e, const VkPhysicalDeviceShaderFloatControls2Features *s);
static void reply_VkPhysicalDeviceDynamicRenderingLocalReadFeatures(MvkEncoder *e, const VkPhysicalDeviceDynamicRenderingLocalReadFeatures *s);
static void reply_VkPhysicalDeviceShaderQuadControlFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderQuadControlFeaturesKHR *s);
static void reply_VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV *s);
static void reply_VkPhysicalDeviceMapMemoryPlacedFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMapMemoryPlacedFeaturesEXT *s);
static void reply_VkPhysicalDeviceMapMemoryPlacedPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMapMemoryPlacedPropertiesEXT *s);
static void reply_VkPhysicalDeviceShaderBfloat16FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderBfloat16FeaturesKHR *s);
static void reply_VkPhysicalDeviceRawAccessChainsFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRawAccessChainsFeaturesNV *s);
static void reply_VkPhysicalDeviceCommandBufferInheritanceFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCommandBufferInheritanceFeaturesNV *s);
static void reply_VkPhysicalDeviceImageAlignmentControlFeaturesMESA(MvkEncoder *e, const VkPhysicalDeviceImageAlignmentControlFeaturesMESA *s);
static void reply_VkPhysicalDeviceImageAlignmentControlPropertiesMESA(MvkEncoder *e, const VkPhysicalDeviceImageAlignmentControlPropertiesMESA *s);
static void reply_VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT *s);
static void reply_VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR(MvkEncoder *e, const VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR *s);
static void reply_VkPhysicalDeviceCooperativeMatrix2FeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrix2FeaturesNV *s);
static void reply_VkPhysicalDeviceCooperativeMatrix2PropertiesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrix2PropertiesNV *s);
static void reply_VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV *s);
static void reply_VkPhysicalDeviceHdrVividFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceHdrVividFeaturesHUAWEI *s);
static void reply_VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT *s);
static void reply_VkPhysicalDeviceDepthClampZeroOneFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceDepthClampZeroOneFeaturesKHR *s);
static void reply_VkPhysicalDeviceCooperativeVectorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeVectorFeaturesNV *s);
static void reply_VkPhysicalDeviceCooperativeVectorPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeVectorPropertiesNV *s);
static void reply_VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE *s);
static void reply_VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE *s);
static void reply_VkPhysicalDevicePresentMeteringFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePresentMeteringFeaturesNV *s);
static void reply_VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT *s);
static void reply_VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE *s);
static void reply_VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT *s);
static void reply_VkPhysicalDeviceFormatPackFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceFormatPackFeaturesARM *s);
static void reply_VkPhysicalDeviceThrottleHintFeaturesSEC(MvkEncoder *e, const VkPhysicalDeviceThrottleHintFeaturesSEC *s);
static void reply_VkPhysicalDeviceShaderFloat8FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderFloat8FeaturesEXT *s);
static void reply_VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC(MvkEncoder *e, const VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC *s);
static void reply_VkPhysicalDeviceDataGraphModelFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceDataGraphModelFeaturesQCOM *s);
static void reply_VkPhysicalDeviceShaderUntypedPointersFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderUntypedPointersFeaturesKHR *s);
static void reply_VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE *s);
static void reply_VkPhysicalDeviceShader64BitIndexingFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShader64BitIndexingFeaturesEXT *s);
static void reply_VkPhysicalDeviceQueuePerfHintFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceQueuePerfHintFeaturesQCOM *s);
static void reply_VkPhysicalDeviceQueuePerfHintPropertiesQCOM(MvkEncoder *e, const VkPhysicalDeviceQueuePerfHintPropertiesQCOM *s);
static void reply_VkPhysicalDevicePerformanceCountersByRegionFeaturesARM(MvkEncoder *e, const VkPhysicalDevicePerformanceCountersByRegionFeaturesARM *s);
static void reply_VkPhysicalDevicePerformanceCountersByRegionPropertiesARM(MvkEncoder *e, const VkPhysicalDevicePerformanceCountersByRegionPropertiesARM *s);
static void reply_VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV *s);
static void reply_VkPhysicalDeviceShaderLongVectorFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderLongVectorFeaturesEXT *s);
static void reply_VkPhysicalDeviceShaderLongVectorPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderLongVectorPropertiesEXT *s);
static void reply_VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT *s);
static void reply_VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT *s);
static void reply_VkSubsampledImageFormatPropertiesEXT(MvkEncoder *e, const VkSubsampledImageFormatPropertiesEXT *s);
static void reply_VkPhysicalDeviceShaderSplitBarrierFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderSplitBarrierFeaturesEXT *s);
static void reply_VkPhysicalDeviceShaderSplitBarrierPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderSplitBarrierPropertiesEXT *s);
static void reply_VkPhysicalDeviceDescriptorHeapFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorHeapFeaturesEXT *s);
static void reply_VkPhysicalDeviceDescriptorHeapPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorHeapPropertiesEXT *s);
static void reply_VkPhysicalDeviceDescriptorHeapTensorPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceDescriptorHeapTensorPropertiesARM *s);
static void reply_VkPhysicalDeviceShaderInstrumentationFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceShaderInstrumentationFeaturesARM *s);
static void reply_VkPhysicalDeviceShaderInstrumentationPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceShaderInstrumentationPropertiesARM *s);
static void reply_VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR *s);
static void reply_VkPhysicalDeviceShaderConstantDataFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderConstantDataFeaturesKHR *s);
static void reply_VkPhysicalDeviceShaderAbortFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderAbortFeaturesKHR *s);
static void reply_VkPhysicalDeviceShaderAbortPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderAbortPropertiesKHR *s);
static void reply_VkDeviceFaultShaderAbortMessageInfoKHR(MvkEncoder *e, const VkDeviceFaultShaderAbortMessageInfoKHR *s);
static void reply_VkPhysicalDeviceImageTilingControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageTilingControlFeaturesEXT *s);
static void reply_VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT *s);
static void reply_VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT *s);
static void reply_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE *s);
static void reply_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE(MvkEncoder *e, const VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE *s);
static void reply_VkPhysicalDeviceInfoPropertiesINTEL(MvkEncoder *e, const VkPhysicalDeviceInfoPropertiesINTEL *s);

static void reply_VkPhysicalDeviceFeatures(MvkEncoder *e, const VkPhysicalDeviceFeatures *s)
{
    mvk_put_u32(e, (uint32_t)(s->robustBufferAccess));
    mvk_put_u32(e, (uint32_t)(s->fullDrawIndexUint32));
    mvk_put_u32(e, (uint32_t)(s->imageCubeArray));
    mvk_put_u32(e, (uint32_t)(s->independentBlend));
    mvk_put_u32(e, (uint32_t)(s->geometryShader));
    mvk_put_u32(e, (uint32_t)(s->tessellationShader));
    mvk_put_u32(e, (uint32_t)(s->sampleRateShading));
    mvk_put_u32(e, (uint32_t)(s->dualSrcBlend));
    mvk_put_u32(e, (uint32_t)(s->logicOp));
    mvk_put_u32(e, (uint32_t)(s->multiDrawIndirect));
    mvk_put_u32(e, (uint32_t)(s->drawIndirectFirstInstance));
    mvk_put_u32(e, (uint32_t)(s->depthClamp));
    mvk_put_u32(e, (uint32_t)(s->depthBiasClamp));
    mvk_put_u32(e, (uint32_t)(s->fillModeNonSolid));
    mvk_put_u32(e, (uint32_t)(s->depthBounds));
    mvk_put_u32(e, (uint32_t)(s->wideLines));
    mvk_put_u32(e, (uint32_t)(s->largePoints));
    mvk_put_u32(e, (uint32_t)(s->alphaToOne));
    mvk_put_u32(e, (uint32_t)(s->multiViewport));
    mvk_put_u32(e, (uint32_t)(s->samplerAnisotropy));
    mvk_put_u32(e, (uint32_t)(s->textureCompressionETC2));
    mvk_put_u32(e, (uint32_t)(s->textureCompressionASTC_LDR));
    mvk_put_u32(e, (uint32_t)(s->textureCompressionBC));
    mvk_put_u32(e, (uint32_t)(s->occlusionQueryPrecise));
    mvk_put_u32(e, (uint32_t)(s->pipelineStatisticsQuery));
    mvk_put_u32(e, (uint32_t)(s->vertexPipelineStoresAndAtomics));
    mvk_put_u32(e, (uint32_t)(s->fragmentStoresAndAtomics));
    mvk_put_u32(e, (uint32_t)(s->shaderTessellationAndGeometryPointSize));
    mvk_put_u32(e, (uint32_t)(s->shaderImageGatherExtended));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageImageExtendedFormats));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageImageMultisample));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageImageReadWithoutFormat));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageImageWriteWithoutFormat));
    mvk_put_u32(e, (uint32_t)(s->shaderUniformBufferArrayDynamicIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderSampledImageArrayDynamicIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageBufferArrayDynamicIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageImageArrayDynamicIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderClipDistance));
    mvk_put_u32(e, (uint32_t)(s->shaderCullDistance));
    mvk_put_u32(e, (uint32_t)(s->shaderFloat64));
    mvk_put_u32(e, (uint32_t)(s->shaderInt64));
    mvk_put_u32(e, (uint32_t)(s->shaderInt16));
    mvk_put_u32(e, (uint32_t)(s->shaderResourceResidency));
    mvk_put_u32(e, (uint32_t)(s->shaderResourceMinLod));
    mvk_put_u32(e, (uint32_t)(s->sparseBinding));
    mvk_put_u32(e, (uint32_t)(s->sparseResidencyBuffer));
    mvk_put_u32(e, (uint32_t)(s->sparseResidencyImage2D));
    mvk_put_u32(e, (uint32_t)(s->sparseResidencyImage3D));
    mvk_put_u32(e, (uint32_t)(s->sparseResidency2Samples));
    mvk_put_u32(e, (uint32_t)(s->sparseResidency4Samples));
    mvk_put_u32(e, (uint32_t)(s->sparseResidency8Samples));
    mvk_put_u32(e, (uint32_t)(s->sparseResidency16Samples));
    mvk_put_u32(e, (uint32_t)(s->sparseResidencyAliased));
    mvk_put_u32(e, (uint32_t)(s->variableMultisampleRate));
    mvk_put_u32(e, (uint32_t)(s->inheritedQueries));
}

static void reply_VkFormatProperties(MvkEncoder *e, const VkFormatProperties *s)
{
    mvk_put_u32(e, (uint32_t)(s->linearTilingFeatures));
    mvk_put_u32(e, (uint32_t)(s->optimalTilingFeatures));
    mvk_put_u32(e, (uint32_t)(s->bufferFeatures));
}

static void reply_VkImageFormatProperties(MvkEncoder *e, const VkImageFormatProperties *s)
{
    reply_VkExtent3D(e, &s->maxExtent);
    mvk_put_u32(e, (uint32_t)(s->maxMipLevels));
    mvk_put_u32(e, (uint32_t)(s->maxArrayLayers));
    mvk_put_u32(e, (uint32_t)(s->sampleCounts));
    mvk_put_u64(e, (uint64_t)(s->maxResourceSize));
}

static void reply_VkExtent3D(MvkEncoder *e, const VkExtent3D *s)
{
    mvk_put_u32(e, (uint32_t)(s->width));
    mvk_put_u32(e, (uint32_t)(s->height));
    mvk_put_u32(e, (uint32_t)(s->depth));
}

static void reply_VkPhysicalDeviceProperties(MvkEncoder *e, const VkPhysicalDeviceProperties *s)
{
    mvk_put_u32(e, (uint32_t)(s->apiVersion));
    mvk_put_u32(e, (uint32_t)(s->driverVersion));
    mvk_put_u32(e, (uint32_t)(s->vendorID));
    mvk_put_u32(e, (uint32_t)(s->deviceID));
    mvk_put_u32(e, (uint32_t)(s->deviceType));
    mvk_put_bytes(e, s->deviceName, sizeof(s->deviceName));
    mvk_put_bytes(e, s->pipelineCacheUUID, sizeof(s->pipelineCacheUUID));
    reply_VkPhysicalDeviceLimits(e, &s->limits);
    reply_VkPhysicalDeviceSparseProperties(e, &s->sparseProperties);
}

static void reply_VkPhysicalDeviceLimits(MvkEncoder *e, const VkPhysicalDeviceLimits *s)
{
    mvk_put_u32(e, (uint32_t)(s->maxImageDimension1D));
    mvk_put_u32(e, (uint32_t)(s->maxImageDimension2D));
    mvk_put_u32(e, (uint32_t)(s->maxImageDimension3D));
    mvk_put_u32(e, (uint32_t)(s->maxImageDimensionCube));
    mvk_put_u32(e, (uint32_t)(s->maxImageArrayLayers));
    mvk_put_u32(e, (uint32_t)(s->maxTexelBufferElements));
    mvk_put_u32(e, (uint32_t)(s->maxUniformBufferRange));
    mvk_put_u32(e, (uint32_t)(s->maxStorageBufferRange));
    mvk_put_u32(e, (uint32_t)(s->maxPushConstantsSize));
    mvk_put_u32(e, (uint32_t)(s->maxMemoryAllocationCount));
    mvk_put_u32(e, (uint32_t)(s->maxSamplerAllocationCount));
    mvk_put_u64(e, (uint64_t)(s->bufferImageGranularity));
    mvk_put_u64(e, (uint64_t)(s->sparseAddressSpaceSize));
    mvk_put_u32(e, (uint32_t)(s->maxBoundDescriptorSets));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorSamplers));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUniformBuffers));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorStorageBuffers));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorSampledImages));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorStorageImages));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorInputAttachments));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageResources));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetSamplers));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUniformBuffers));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUniformBuffersDynamic));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetStorageBuffers));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetStorageBuffersDynamic));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetSampledImages));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetStorageImages));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetInputAttachments));
    mvk_put_u32(e, (uint32_t)(s->maxVertexInputAttributes));
    mvk_put_u32(e, (uint32_t)(s->maxVertexInputBindings));
    mvk_put_u32(e, (uint32_t)(s->maxVertexInputAttributeOffset));
    mvk_put_u32(e, (uint32_t)(s->maxVertexInputBindingStride));
    mvk_put_u32(e, (uint32_t)(s->maxVertexOutputComponents));
    mvk_put_u32(e, (uint32_t)(s->maxTessellationGenerationLevel));
    mvk_put_u32(e, (uint32_t)(s->maxTessellationPatchSize));
    mvk_put_u32(e, (uint32_t)(s->maxTessellationControlPerVertexInputComponents));
    mvk_put_u32(e, (uint32_t)(s->maxTessellationControlPerVertexOutputComponents));
    mvk_put_u32(e, (uint32_t)(s->maxTessellationControlPerPatchOutputComponents));
    mvk_put_u32(e, (uint32_t)(s->maxTessellationControlTotalOutputComponents));
    mvk_put_u32(e, (uint32_t)(s->maxTessellationEvaluationInputComponents));
    mvk_put_u32(e, (uint32_t)(s->maxTessellationEvaluationOutputComponents));
    mvk_put_u32(e, (uint32_t)(s->maxGeometryShaderInvocations));
    mvk_put_u32(e, (uint32_t)(s->maxGeometryInputComponents));
    mvk_put_u32(e, (uint32_t)(s->maxGeometryOutputComponents));
    mvk_put_u32(e, (uint32_t)(s->maxGeometryOutputVertices));
    mvk_put_u32(e, (uint32_t)(s->maxGeometryTotalOutputComponents));
    mvk_put_u32(e, (uint32_t)(s->maxFragmentInputComponents));
    mvk_put_u32(e, (uint32_t)(s->maxFragmentOutputAttachments));
    mvk_put_u32(e, (uint32_t)(s->maxFragmentDualSrcAttachments));
    mvk_put_u32(e, (uint32_t)(s->maxFragmentCombinedOutputResources));
    mvk_put_u32(e, (uint32_t)(s->maxComputeSharedMemorySize));
    mvk_put_bytes(e, s->maxComputeWorkGroupCount, sizeof(s->maxComputeWorkGroupCount));
    mvk_put_u32(e, (uint32_t)(s->maxComputeWorkGroupInvocations));
    mvk_put_bytes(e, s->maxComputeWorkGroupSize, sizeof(s->maxComputeWorkGroupSize));
    mvk_put_u32(e, (uint32_t)(s->subPixelPrecisionBits));
    mvk_put_u32(e, (uint32_t)(s->subTexelPrecisionBits));
    mvk_put_u32(e, (uint32_t)(s->mipmapPrecisionBits));
    mvk_put_u32(e, (uint32_t)(s->maxDrawIndexedIndexValue));
    mvk_put_u32(e, (uint32_t)(s->maxDrawIndirectCount));
    mvk_put_u32(e, mvk_f32_bits(s->maxSamplerLodBias));
    mvk_put_u32(e, mvk_f32_bits(s->maxSamplerAnisotropy));
    mvk_put_u32(e, (uint32_t)(s->maxViewports));
    mvk_put_bytes(e, s->maxViewportDimensions, sizeof(s->maxViewportDimensions));
    mvk_put_bytes(e, s->viewportBoundsRange, sizeof(s->viewportBoundsRange));
    mvk_put_u32(e, (uint32_t)(s->viewportSubPixelBits));
    mvk_put_u64(e, (uint64_t)(s->minMemoryMapAlignment));
    mvk_put_u64(e, (uint64_t)(s->minTexelBufferOffsetAlignment));
    mvk_put_u64(e, (uint64_t)(s->minUniformBufferOffsetAlignment));
    mvk_put_u64(e, (uint64_t)(s->minStorageBufferOffsetAlignment));
    mvk_put_u32(e, (uint32_t)(s->minTexelOffset));
    mvk_put_u32(e, (uint32_t)(s->maxTexelOffset));
    mvk_put_u32(e, (uint32_t)(s->minTexelGatherOffset));
    mvk_put_u32(e, (uint32_t)(s->maxTexelGatherOffset));
    mvk_put_u32(e, mvk_f32_bits(s->minInterpolationOffset));
    mvk_put_u32(e, mvk_f32_bits(s->maxInterpolationOffset));
    mvk_put_u32(e, (uint32_t)(s->subPixelInterpolationOffsetBits));
    mvk_put_u32(e, (uint32_t)(s->maxFramebufferWidth));
    mvk_put_u32(e, (uint32_t)(s->maxFramebufferHeight));
    mvk_put_u32(e, (uint32_t)(s->maxFramebufferLayers));
    mvk_put_u32(e, (uint32_t)(s->framebufferColorSampleCounts));
    mvk_put_u32(e, (uint32_t)(s->framebufferDepthSampleCounts));
    mvk_put_u32(e, (uint32_t)(s->framebufferStencilSampleCounts));
    mvk_put_u32(e, (uint32_t)(s->framebufferNoAttachmentsSampleCounts));
    mvk_put_u32(e, (uint32_t)(s->maxColorAttachments));
    mvk_put_u32(e, (uint32_t)(s->sampledImageColorSampleCounts));
    mvk_put_u32(e, (uint32_t)(s->sampledImageIntegerSampleCounts));
    mvk_put_u32(e, (uint32_t)(s->sampledImageDepthSampleCounts));
    mvk_put_u32(e, (uint32_t)(s->sampledImageStencilSampleCounts));
    mvk_put_u32(e, (uint32_t)(s->storageImageSampleCounts));
    mvk_put_u32(e, (uint32_t)(s->maxSampleMaskWords));
    mvk_put_u32(e, (uint32_t)(s->timestampComputeAndGraphics));
    mvk_put_u32(e, mvk_f32_bits(s->timestampPeriod));
    mvk_put_u32(e, (uint32_t)(s->maxClipDistances));
    mvk_put_u32(e, (uint32_t)(s->maxCullDistances));
    mvk_put_u32(e, (uint32_t)(s->maxCombinedClipAndCullDistances));
    mvk_put_u32(e, (uint32_t)(s->discreteQueuePriorities));
    mvk_put_bytes(e, s->pointSizeRange, sizeof(s->pointSizeRange));
    mvk_put_bytes(e, s->lineWidthRange, sizeof(s->lineWidthRange));
    mvk_put_u32(e, mvk_f32_bits(s->pointSizeGranularity));
    mvk_put_u32(e, mvk_f32_bits(s->lineWidthGranularity));
    mvk_put_u32(e, (uint32_t)(s->strictLines));
    mvk_put_u32(e, (uint32_t)(s->standardSampleLocations));
    mvk_put_u64(e, (uint64_t)(s->optimalBufferCopyOffsetAlignment));
    mvk_put_u64(e, (uint64_t)(s->optimalBufferCopyRowPitchAlignment));
    mvk_put_u64(e, (uint64_t)(s->nonCoherentAtomSize));
}

static void reply_VkPhysicalDeviceSparseProperties(MvkEncoder *e, const VkPhysicalDeviceSparseProperties *s)
{
    mvk_put_u32(e, (uint32_t)(s->residencyStandard2DBlockShape));
    mvk_put_u32(e, (uint32_t)(s->residencyStandard2DMultisampleBlockShape));
    mvk_put_u32(e, (uint32_t)(s->residencyStandard3DBlockShape));
    mvk_put_u32(e, (uint32_t)(s->residencyAlignedMipSize));
    mvk_put_u32(e, (uint32_t)(s->residencyNonResidentStrict));
}

static void reply_VkQueueFamilyProperties(MvkEncoder *e, const VkQueueFamilyProperties *s)
{
    mvk_put_u32(e, (uint32_t)(s->queueFlags));
    mvk_put_u32(e, (uint32_t)(s->queueCount));
    mvk_put_u32(e, (uint32_t)(s->timestampValidBits));
    reply_VkExtent3D(e, &s->minImageTransferGranularity);
}

static void reply_VkPhysicalDeviceMemoryProperties(MvkEncoder *e, const VkPhysicalDeviceMemoryProperties *s)
{
    mvk_put_u32(e, (uint32_t)(s->memoryTypeCount));
    mvk_put_bytes(e, s->memoryTypes, sizeof(s->memoryTypes));
    mvk_put_u32(e, (uint32_t)(s->memoryHeapCount));
    mvk_put_bytes(e, s->memoryHeaps, sizeof(s->memoryHeaps));
}

static void reply_VkMemoryType(MvkEncoder *e, const VkMemoryType *s)
{
    mvk_put_u32(e, (uint32_t)(s->propertyFlags));
    mvk_put_u32(e, (uint32_t)(s->heapIndex));
}

static void reply_VkMemoryHeap(MvkEncoder *e, const VkMemoryHeap *s)
{
    mvk_put_u64(e, (uint64_t)(s->size));
    mvk_put_u32(e, (uint32_t)(s->flags));
}

static void reply_VkMemoryRequirements(MvkEncoder *e, const VkMemoryRequirements *s)
{
    mvk_put_u64(e, (uint64_t)(s->size));
    mvk_put_u64(e, (uint64_t)(s->alignment));
    mvk_put_u32(e, (uint32_t)(s->memoryTypeBits));
}

static void reply_VkSparseImageMemoryRequirements(MvkEncoder *e, const VkSparseImageMemoryRequirements *s)
{
    reply_VkSparseImageFormatProperties(e, &s->formatProperties);
    mvk_put_u32(e, (uint32_t)(s->imageMipTailFirstLod));
    mvk_put_u64(e, (uint64_t)(s->imageMipTailSize));
    mvk_put_u64(e, (uint64_t)(s->imageMipTailOffset));
    mvk_put_u64(e, (uint64_t)(s->imageMipTailStride));
}

static void reply_VkSparseImageFormatProperties(MvkEncoder *e, const VkSparseImageFormatProperties *s)
{
    mvk_put_u32(e, (uint32_t)(s->aspectMask));
    reply_VkExtent3D(e, &s->imageGranularity);
    mvk_put_u32(e, (uint32_t)(s->flags));
}

static void reply_VkSubresourceLayout(MvkEncoder *e, const VkSubresourceLayout *s)
{
    mvk_put_u64(e, (uint64_t)(s->offset));
    mvk_put_u64(e, (uint64_t)(s->size));
    mvk_put_u64(e, (uint64_t)(s->rowPitch));
    mvk_put_u64(e, (uint64_t)(s->arrayPitch));
    mvk_put_u64(e, (uint64_t)(s->depthPitch));
}

static void reply_VkExtent2D(MvkEncoder *e, const VkExtent2D *s)
{
    mvk_put_u32(e, (uint32_t)(s->width));
    mvk_put_u32(e, (uint32_t)(s->height));
}

static void reply_VkMemoryRequirements2(MvkEncoder *e, const VkMemoryRequirements2 *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkMemoryRequirements(e, &s->memoryRequirements);
}

static void reply_VkSparseImageMemoryRequirements2(MvkEncoder *e, const VkSparseImageMemoryRequirements2 *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkSparseImageMemoryRequirements(e, &s->memoryRequirements);
}

static void reply_VkPhysicalDeviceFeatures2(MvkEncoder *e, const VkPhysicalDeviceFeatures2 *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkPhysicalDeviceFeatures(e, &s->features);
}

static void reply_VkPhysicalDeviceProperties2(MvkEncoder *e, const VkPhysicalDeviceProperties2 *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkPhysicalDeviceProperties(e, &s->properties);
}

static void reply_VkFormatProperties2(MvkEncoder *e, const VkFormatProperties2 *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkFormatProperties(e, &s->formatProperties);
}

static void reply_VkImageFormatProperties2(MvkEncoder *e, const VkImageFormatProperties2 *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkImageFormatProperties(e, &s->imageFormatProperties);
}

static void reply_VkQueueFamilyProperties2(MvkEncoder *e, const VkQueueFamilyProperties2 *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkQueueFamilyProperties(e, &s->queueFamilyProperties);
}

static void reply_VkPhysicalDeviceMemoryProperties2(MvkEncoder *e, const VkPhysicalDeviceMemoryProperties2 *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkPhysicalDeviceMemoryProperties(e, &s->memoryProperties);
}

static void reply_VkSparseImageFormatProperties2(MvkEncoder *e, const VkSparseImageFormatProperties2 *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkSparseImageFormatProperties(e, &s->properties);
}

static void reply_VkExternalBufferProperties(MvkEncoder *e, const VkExternalBufferProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkExternalMemoryProperties(e, &s->externalMemoryProperties);
}

static void reply_VkExternalMemoryProperties(MvkEncoder *e, const VkExternalMemoryProperties *s)
{
    mvk_put_u32(e, (uint32_t)(s->externalMemoryFeatures));
    mvk_put_u32(e, (uint32_t)(s->exportFromImportedHandleTypes));
    mvk_put_u32(e, (uint32_t)(s->compatibleHandleTypes));
}

static void reply_VkExternalFenceProperties(MvkEncoder *e, const VkExternalFenceProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->exportFromImportedHandleTypes));
    mvk_put_u32(e, (uint32_t)(s->compatibleHandleTypes));
    mvk_put_u32(e, (uint32_t)(s->externalFenceFeatures));
}

static void reply_VkExternalSemaphoreProperties(MvkEncoder *e, const VkExternalSemaphoreProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->exportFromImportedHandleTypes));
    mvk_put_u32(e, (uint32_t)(s->compatibleHandleTypes));
    mvk_put_u32(e, (uint32_t)(s->externalSemaphoreFeatures));
}

static void reply_VkDescriptorSetLayoutSupport(MvkEncoder *e, const VkDescriptorSetLayoutSupport *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->supported));
}

static void reply_VkPhysicalDeviceToolProperties(MvkEncoder *e, const VkPhysicalDeviceToolProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_bytes(e, s->name, sizeof(s->name));
    mvk_put_bytes(e, s->version, sizeof(s->version));
    mvk_put_u32(e, (uint32_t)(s->purposes));
    mvk_put_bytes(e, s->description, sizeof(s->description));
    mvk_put_bytes(e, s->layer, sizeof(s->layer));
}

static void reply_VkSubresourceLayout2(MvkEncoder *e, const VkSubresourceLayout2 *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkSubresourceLayout(e, &s->subresourceLayout);
}

static void reply_VkImageViewAddressPropertiesNVX(MvkEncoder *e, const VkImageViewAddressPropertiesNVX *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->deviceAddress));
    mvk_put_u64(e, (uint64_t)(s->size));
}

static void reply_VkExternalImageFormatPropertiesNV(MvkEncoder *e, const VkExternalImageFormatPropertiesNV *s)
{
    reply_VkImageFormatProperties(e, &s->imageFormatProperties);
    mvk_put_u32(e, (uint32_t)(s->externalMemoryFeatures));
    mvk_put_u32(e, (uint32_t)(s->exportFromImportedHandleTypes));
    mvk_put_u32(e, (uint32_t)(s->compatibleHandleTypes));
}

static void reply_VkPerformanceCounterKHR(MvkEncoder *e, const VkPerformanceCounterKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->unit));
    mvk_put_u32(e, (uint32_t)(s->scope));
    mvk_put_u32(e, (uint32_t)(s->storage));
    mvk_put_bytes(e, s->uuid, sizeof(s->uuid));
}

static void reply_VkPerformanceCounterDescriptionKHR(MvkEncoder *e, const VkPerformanceCounterDescriptionKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->flags));
    mvk_put_bytes(e, s->name, sizeof(s->name));
    mvk_put_bytes(e, s->category, sizeof(s->category));
    mvk_put_bytes(e, s->description, sizeof(s->description));
}

static void reply_VkGpaDeviceClockModeInfoAMD(MvkEncoder *e, const VkGpaDeviceClockModeInfoAMD *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->clockMode));
    mvk_put_u32(e, mvk_f32_bits(s->memoryClockRatioToPeak));
    mvk_put_u32(e, mvk_f32_bits(s->engineClockRatioToPeak));
}

static void reply_VkGpaDeviceGetClockInfoAMD(MvkEncoder *e, const VkGpaDeviceGetClockInfoAMD *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, mvk_f32_bits(s->memoryClockRatioToPeak));
    mvk_put_u32(e, mvk_f32_bits(s->engineClockRatioToPeak));
    mvk_put_u32(e, (uint32_t)(s->memoryClockFrequency));
    mvk_put_u32(e, (uint32_t)(s->engineClockFrequency));
}

static void reply_VkHostAddressRangeEXT(MvkEncoder *e, const VkHostAddressRangeEXT *s)
{
    mvk_put_u32(e, 0);
    mvk_put_u64(e, (uint64_t)(s->size));
}

static void reply_VkMultisamplePropertiesEXT(MvkEncoder *e, const VkMultisamplePropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkExtent2D(e, &s->maxSampleLocationGridSize);
}

static void reply_VkAccelerationStructureBuildSizesInfoKHR(MvkEncoder *e, const VkAccelerationStructureBuildSizesInfoKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->accelerationStructureSize));
    mvk_put_u64(e, (uint64_t)(s->updateScratchSize));
    mvk_put_u64(e, (uint64_t)(s->buildScratchSize));
}

static void reply_VkImageDrmFormatModifierPropertiesEXT(MvkEncoder *e, const VkImageDrmFormatModifierPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->drmFormatModifier));
}

static void reply_VkPerformanceValueINTEL(MvkEncoder *e, const VkPerformanceValueINTEL *s)
{
    mvk_put_u32(e, (uint32_t)(s->type));
    mvk_put_bytes(e, &s->data, sizeof(s->data));
}

static void reply_VkPhysicalDeviceFragmentShadingRateKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRateKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->sampleCounts));
    reply_VkExtent2D(e, &s->fragmentSize);
}

static void reply_VkCooperativeMatrixPropertiesNV(MvkEncoder *e, const VkCooperativeMatrixPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->MSize));
    mvk_put_u32(e, (uint32_t)(s->NSize));
    mvk_put_u32(e, (uint32_t)(s->KSize));
    mvk_put_u32(e, (uint32_t)(s->AType));
    mvk_put_u32(e, (uint32_t)(s->BType));
    mvk_put_u32(e, (uint32_t)(s->CType));
    mvk_put_u32(e, (uint32_t)(s->DType));
    mvk_put_u32(e, (uint32_t)(s->scope));
}

static void reply_VkFramebufferMixedSamplesCombinationNV(MvkEncoder *e, const VkFramebufferMixedSamplesCombinationNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->coverageReductionMode));
    mvk_put_u32(e, (uint32_t)(s->rasterizationSamples));
    mvk_put_u32(e, (uint32_t)(s->depthStencilSamples));
    mvk_put_u32(e, (uint32_t)(s->colorSamples));
}

static void reply_VkPipelineExecutablePropertiesKHR(MvkEncoder *e, const VkPipelineExecutablePropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->stages));
    mvk_put_bytes(e, s->name, sizeof(s->name));
    mvk_put_bytes(e, s->description, sizeof(s->description));
    mvk_put_u32(e, (uint32_t)(s->subgroupSize));
}

static void reply_VkPipelineExecutableStatisticKHR(MvkEncoder *e, const VkPipelineExecutableStatisticKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_bytes(e, s->name, sizeof(s->name));
    mvk_put_bytes(e, s->description, sizeof(s->description));
    mvk_put_u32(e, (uint32_t)(s->format));
    mvk_put_bytes(e, &s->value, sizeof(s->value));
}

static void reply_VkPipelineExecutableInternalRepresentationKHR(MvkEncoder *e, const VkPipelineExecutableInternalRepresentationKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_bytes(e, s->name, sizeof(s->name));
    mvk_put_bytes(e, s->description, sizeof(s->description));
    mvk_put_u32(e, (uint32_t)(s->isText));
    mvk_put_u64(e, (uint64_t)(s->dataSize));
    mvk_put_u32(e, 0);
}

static void reply_VkBaseOutStructure(MvkEncoder *e, const VkBaseOutStructure *s)
{
    mvk_reply_chain(e, s->pNext);
}

static void reply_VkMicromapBuildSizesInfoEXT(MvkEncoder *e, const VkMicromapBuildSizesInfoEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->micromapSize));
    mvk_put_u64(e, (uint64_t)(s->buildScratchSize));
    mvk_put_u32(e, (uint32_t)(s->discardable));
}

static void reply_VkDescriptorSetLayoutHostMappingInfoVALVE(MvkEncoder *e, const VkDescriptorSetLayoutHostMappingInfoVALVE *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->descriptorOffset));
    mvk_put_u32(e, (uint32_t)(s->descriptorSize));
}

static void reply_VkShaderModuleIdentifierEXT(MvkEncoder *e, const VkShaderModuleIdentifierEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->identifierSize));
    mvk_put_bytes(e, s->identifier, sizeof(s->identifier));
}

static void reply_VkOpticalFlowImageFormatPropertiesNV(MvkEncoder *e, const VkOpticalFlowImageFormatPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->format));
}

static void reply_VkCooperativeVectorPropertiesNV(MvkEncoder *e, const VkCooperativeVectorPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->inputType));
    mvk_put_u32(e, (uint32_t)(s->inputInterpretation));
    mvk_put_u32(e, (uint32_t)(s->matrixInterpretation));
    mvk_put_u32(e, (uint32_t)(s->biasInterpretation));
    mvk_put_u32(e, (uint32_t)(s->resultType));
    mvk_put_u32(e, (uint32_t)(s->transpose));
}

static void reply_VkCooperativeMatrixPropertiesKHR(MvkEncoder *e, const VkCooperativeMatrixPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->MSize));
    mvk_put_u32(e, (uint32_t)(s->NSize));
    mvk_put_u32(e, (uint32_t)(s->KSize));
    mvk_put_u32(e, (uint32_t)(s->AType));
    mvk_put_u32(e, (uint32_t)(s->BType));
    mvk_put_u32(e, (uint32_t)(s->CType));
    mvk_put_u32(e, (uint32_t)(s->ResultType));
    mvk_put_u32(e, (uint32_t)(s->saturatingAccumulation));
    mvk_put_u32(e, (uint32_t)(s->scope));
}

static void reply_VkDeviceFaultInfoKHR(MvkEncoder *e, const VkDeviceFaultInfoKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->flags));
    mvk_put_u64(e, (uint64_t)(s->groupId));
    mvk_put_bytes(e, s->description, sizeof(s->description));
    reply_VkDeviceFaultAddressInfoKHR(e, &s->faultAddressInfo);
    reply_VkDeviceFaultAddressInfoKHR(e, &s->instructionAddressInfo);
    reply_VkDeviceFaultVendorInfoKHR(e, &s->vendorInfo);
}

static void reply_VkDeviceFaultAddressInfoKHR(MvkEncoder *e, const VkDeviceFaultAddressInfoKHR *s)
{
    mvk_put_u32(e, (uint32_t)(s->addressType));
    mvk_put_u64(e, (uint64_t)(s->reportedAddress));
    mvk_put_u64(e, (uint64_t)(s->addressPrecision));
}

static void reply_VkDeviceFaultVendorInfoKHR(MvkEncoder *e, const VkDeviceFaultVendorInfoKHR *s)
{
    mvk_put_bytes(e, s->description, sizeof(s->description));
    mvk_put_u64(e, (uint64_t)(s->vendorFaultCode));
    mvk_put_u64(e, (uint64_t)(s->vendorFaultData));
}

static void reply_VkDeviceFaultDebugInfoKHR(MvkEncoder *e, const VkDeviceFaultDebugInfoKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->vendorBinarySize));
    mvk_put_u32(e, 0);
}

static void reply_VkCooperativeMatrixFlexibleDimensionsPropertiesNV(MvkEncoder *e, const VkCooperativeMatrixFlexibleDimensionsPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->MGranularity));
    mvk_put_u32(e, (uint32_t)(s->NGranularity));
    mvk_put_u32(e, (uint32_t)(s->KGranularity));
    mvk_put_u32(e, (uint32_t)(s->AType));
    mvk_put_u32(e, (uint32_t)(s->BType));
    mvk_put_u32(e, (uint32_t)(s->CType));
    mvk_put_u32(e, (uint32_t)(s->ResultType));
    mvk_put_u32(e, (uint32_t)(s->saturatingAccumulation));
    mvk_put_u32(e, (uint32_t)(s->scope));
    mvk_put_u32(e, (uint32_t)(s->workgroupInvocations));
}

static void reply_VkPerformanceCounterARM(MvkEncoder *e, const VkPerformanceCounterARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->counterID));
}

static void reply_VkPerformanceCounterDescriptionARM(MvkEncoder *e, const VkPerformanceCounterDescriptionARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->flags));
    mvk_put_bytes(e, s->name, sizeof(s->name));
}

static void reply_VkShaderInstrumentationMetricDescriptionARM(MvkEncoder *e, const VkShaderInstrumentationMetricDescriptionARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_bytes(e, s->name, sizeof(s->name));
    mvk_put_bytes(e, s->description, sizeof(s->description));
}

static void reply_VkCooperativeMatrixProperties2EXT(MvkEncoder *e, const VkCooperativeMatrixProperties2EXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->MGranularity));
    mvk_put_u32(e, (uint32_t)(s->NGranularity));
    mvk_put_u32(e, (uint32_t)(s->KGranularity));
    mvk_put_u32(e, (uint32_t)(s->AType));
    mvk_put_u32(e, (uint32_t)(s->BType));
    mvk_put_u32(e, (uint32_t)(s->CType));
    mvk_put_u32(e, (uint32_t)(s->ResultType));
}

static void reply_VkPhysicalDeviceGroupProperties(MvkEncoder *e, const VkPhysicalDeviceGroupProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->physicalDeviceCount));
    mvk_put_bytes(e, s->physicalDevices, sizeof(s->physicalDevices));
    mvk_put_u32(e, (uint32_t)(s->subsetAllocation));
}

static void reply_VkLayerProperties(MvkEncoder *e, const VkLayerProperties *s)
{
    mvk_put_bytes(e, s->layerName, sizeof(s->layerName));
    mvk_put_u32(e, (uint32_t)(s->specVersion));
    mvk_put_u32(e, (uint32_t)(s->implementationVersion));
    mvk_put_bytes(e, s->description, sizeof(s->description));
}

static void reply_VkExtensionProperties(MvkEncoder *e, const VkExtensionProperties *s)
{
    mvk_put_bytes(e, s->extensionName, sizeof(s->extensionName));
    mvk_put_u32(e, (uint32_t)(s->specVersion));
}

static void reply_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->deviceGeneratedCommands));
}

static void reply_VkPhysicalDevicePushConstantBankFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePushConstantBankFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pushConstantBank));
}

static void reply_VkPhysicalDevicePushConstantBankPropertiesNV(MvkEncoder *e, const VkPhysicalDevicePushConstantBankPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxGraphicsPushConstantBanks));
    mvk_put_u32(e, (uint32_t)(s->maxComputePushConstantBanks));
    mvk_put_u32(e, (uint32_t)(s->maxGraphicsPushDataBanks));
    mvk_put_u32(e, (uint32_t)(s->maxComputePushDataBanks));
}

static void reply_VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->deviceGeneratedCompute));
    mvk_put_u32(e, (uint32_t)(s->deviceGeneratedComputePipelines));
    mvk_put_u32(e, (uint32_t)(s->deviceGeneratedComputeCaptureReplay));
}

static void reply_VkPhysicalDevicePrivateDataFeatures(MvkEncoder *e, const VkPhysicalDevicePrivateDataFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->privateData));
}

static void reply_VkPhysicalDevicePrivateDataBaseHandleFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePrivateDataBaseHandleFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->privateDataBaseHandle));
}

static void reply_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxGraphicsShaderGroupCount));
    mvk_put_u32(e, (uint32_t)(s->maxIndirectSequenceCount));
    mvk_put_u32(e, (uint32_t)(s->maxIndirectCommandsTokenCount));
    mvk_put_u32(e, (uint32_t)(s->maxIndirectCommandsStreamCount));
    mvk_put_u32(e, (uint32_t)(s->maxIndirectCommandsTokenOffset));
    mvk_put_u32(e, (uint32_t)(s->maxIndirectCommandsStreamStride));
    mvk_put_u32(e, (uint32_t)(s->minSequencesCountBufferOffsetAlignment));
    mvk_put_u32(e, (uint32_t)(s->minSequencesIndexBufferOffsetAlignment));
    mvk_put_u32(e, (uint32_t)(s->minIndirectCommandsBufferOffsetAlignment));
}

static void reply_VkPhysicalDeviceClusterAccelerationStructureFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceClusterAccelerationStructureFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->clusterAccelerationStructure));
}

static void reply_VkPhysicalDeviceClusterAccelerationStructurePropertiesNV(MvkEncoder *e, const VkPhysicalDeviceClusterAccelerationStructurePropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxVerticesPerCluster));
    mvk_put_u32(e, (uint32_t)(s->maxTrianglesPerCluster));
    mvk_put_u32(e, (uint32_t)(s->clusterScratchByteAlignment));
    mvk_put_u32(e, (uint32_t)(s->clusterByteAlignment));
    mvk_put_u32(e, (uint32_t)(s->clusterTemplateByteAlignment));
    mvk_put_u32(e, (uint32_t)(s->clusterBottomLevelByteAlignment));
    mvk_put_u32(e, (uint32_t)(s->clusterTemplateBoundsByteAlignment));
    mvk_put_u32(e, (uint32_t)(s->maxClusterGeometryIndex));
}

static void reply_VkPhysicalDeviceMultiDrawPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMultiDrawPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxMultiDrawCount));
}

static void reply_VkPhysicalDevicePushDescriptorProperties(MvkEncoder *e, const VkPhysicalDevicePushDescriptorProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxPushDescriptors));
}

static void reply_VkPhysicalDeviceDriverProperties(MvkEncoder *e, const VkPhysicalDeviceDriverProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->driverID));
    mvk_put_bytes(e, s->driverName, sizeof(s->driverName));
    mvk_put_bytes(e, s->driverInfo, sizeof(s->driverInfo));
    reply_VkConformanceVersion(e, &s->conformanceVersion);
}

static void reply_VkConformanceVersion(MvkEncoder *e, const VkConformanceVersion *s)
{
    mvk_put_u8(e, (uint8_t)(s->major));
    mvk_put_u8(e, (uint8_t)(s->minor));
    mvk_put_u8(e, (uint8_t)(s->subminor));
    mvk_put_u8(e, (uint8_t)(s->patch));
}

static void reply_VkPhysicalDeviceVariablePointersFeatures(MvkEncoder *e, const VkPhysicalDeviceVariablePointersFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->variablePointersStorageBuffer));
    mvk_put_u32(e, (uint32_t)(s->variablePointers));
}

static void reply_VkExternalImageFormatProperties(MvkEncoder *e, const VkExternalImageFormatProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkExternalMemoryProperties(e, &s->externalMemoryProperties);
}

static void reply_VkPhysicalDeviceIDProperties(MvkEncoder *e, const VkPhysicalDeviceIDProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_bytes(e, s->deviceUUID, sizeof(s->deviceUUID));
    mvk_put_bytes(e, s->driverUUID, sizeof(s->driverUUID));
    mvk_put_bytes(e, s->deviceLUID, sizeof(s->deviceLUID));
    mvk_put_u32(e, (uint32_t)(s->deviceNodeMask));
    mvk_put_u32(e, (uint32_t)(s->deviceLUIDValid));
}

static void reply_VkPhysicalDeviceMultiviewFeatures(MvkEncoder *e, const VkPhysicalDeviceMultiviewFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->multiview));
    mvk_put_u32(e, (uint32_t)(s->multiviewGeometryShader));
    mvk_put_u32(e, (uint32_t)(s->multiviewTessellationShader));
}

static void reply_VkPhysicalDeviceMultiviewProperties(MvkEncoder *e, const VkPhysicalDeviceMultiviewProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxMultiviewViewCount));
    mvk_put_u32(e, (uint32_t)(s->maxMultiviewInstanceIndex));
}

static void reply_VkPhysicalDeviceDiscardRectanglePropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDiscardRectanglePropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxDiscardRectangles));
}

static void reply_VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX(MvkEncoder *e, const VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->perViewPositionAllComponents));
}

static void reply_VkPhysicalDevice16BitStorageFeatures(MvkEncoder *e, const VkPhysicalDevice16BitStorageFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->storageBuffer16BitAccess));
    mvk_put_u32(e, (uint32_t)(s->uniformAndStorageBuffer16BitAccess));
    mvk_put_u32(e, (uint32_t)(s->storagePushConstant16));
    mvk_put_u32(e, (uint32_t)(s->storageInputOutput16));
}

static void reply_VkPhysicalDeviceSubgroupProperties(MvkEncoder *e, const VkPhysicalDeviceSubgroupProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->subgroupSize));
    mvk_put_u32(e, (uint32_t)(s->supportedStages));
    mvk_put_u32(e, (uint32_t)(s->supportedOperations));
    mvk_put_u32(e, (uint32_t)(s->quadOperationsInAllStages));
}

static void reply_VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderSubgroupExtendedTypes));
}

static void reply_VkPhysicalDevicePointClippingProperties(MvkEncoder *e, const VkPhysicalDevicePointClippingProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pointClippingBehavior));
}

static void reply_VkMemoryDedicatedRequirements(MvkEncoder *e, const VkMemoryDedicatedRequirements *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->prefersDedicatedAllocation));
    mvk_put_u32(e, (uint32_t)(s->requiresDedicatedAllocation));
}

static void reply_VkPhysicalDeviceSamplerYcbcrConversionFeatures(MvkEncoder *e, const VkPhysicalDeviceSamplerYcbcrConversionFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->samplerYcbcrConversion));
}

static void reply_VkSamplerYcbcrConversionImageFormatProperties(MvkEncoder *e, const VkSamplerYcbcrConversionImageFormatProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->combinedImageSamplerDescriptorCount));
}

static void reply_VkTextureLODGatherFormatPropertiesAMD(MvkEncoder *e, const VkTextureLODGatherFormatPropertiesAMD *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->supportsTextureGatherLODBiasAMD));
}

static void reply_VkPhysicalDeviceProtectedMemoryFeatures(MvkEncoder *e, const VkPhysicalDeviceProtectedMemoryFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->protectedMemory));
}

static void reply_VkPhysicalDeviceProtectedMemoryProperties(MvkEncoder *e, const VkPhysicalDeviceProtectedMemoryProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->protectedNoFault));
}

static void reply_VkPhysicalDeviceSamplerFilterMinmaxProperties(MvkEncoder *e, const VkPhysicalDeviceSamplerFilterMinmaxProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->filterMinmaxSingleComponentFormats));
    mvk_put_u32(e, (uint32_t)(s->filterMinmaxImageComponentMapping));
}

static void reply_VkPhysicalDeviceSampleLocationsPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceSampleLocationsPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->sampleLocationSampleCounts));
    reply_VkExtent2D(e, &s->maxSampleLocationGridSize);
    mvk_put_bytes(e, s->sampleLocationCoordinateRange, sizeof(s->sampleLocationCoordinateRange));
    mvk_put_u32(e, (uint32_t)(s->sampleLocationSubPixelBits));
    mvk_put_u32(e, (uint32_t)(s->variableSampleLocations));
}

static void reply_VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->advancedBlendCoherentOperations));
}

static void reply_VkPhysicalDeviceMultiDrawFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMultiDrawFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->multiDraw));
}

static void reply_VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->advancedBlendMaxColorAttachments));
    mvk_put_u32(e, (uint32_t)(s->advancedBlendIndependentBlend));
    mvk_put_u32(e, (uint32_t)(s->advancedBlendNonPremultipliedSrcColor));
    mvk_put_u32(e, (uint32_t)(s->advancedBlendNonPremultipliedDstColor));
    mvk_put_u32(e, (uint32_t)(s->advancedBlendCorrelatedOverlap));
    mvk_put_u32(e, (uint32_t)(s->advancedBlendAllOperations));
}

static void reply_VkPhysicalDeviceInlineUniformBlockFeatures(MvkEncoder *e, const VkPhysicalDeviceInlineUniformBlockFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->inlineUniformBlock));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingInlineUniformBlockUpdateAfterBind));
}

static void reply_VkPhysicalDeviceInlineUniformBlockProperties(MvkEncoder *e, const VkPhysicalDeviceInlineUniformBlockProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxInlineUniformBlockSize));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorInlineUniformBlocks));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetInlineUniformBlocks));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindInlineUniformBlocks));
}

static void reply_VkPhysicalDeviceMaintenance3Properties(MvkEncoder *e, const VkPhysicalDeviceMaintenance3Properties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxPerSetDescriptors));
    mvk_put_u64(e, (uint64_t)(s->maxMemoryAllocationSize));
}

static void reply_VkPhysicalDeviceMaintenance4Features(MvkEncoder *e, const VkPhysicalDeviceMaintenance4Features *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maintenance4));
}

static void reply_VkPhysicalDeviceMaintenance4Properties(MvkEncoder *e, const VkPhysicalDeviceMaintenance4Properties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->maxBufferSize));
}

static void reply_VkPhysicalDeviceMaintenance5Features(MvkEncoder *e, const VkPhysicalDeviceMaintenance5Features *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maintenance5));
}

static void reply_VkPhysicalDeviceMaintenance5Properties(MvkEncoder *e, const VkPhysicalDeviceMaintenance5Properties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->earlyFragmentMultisampleCoverageAfterSampleCounting));
    mvk_put_u32(e, (uint32_t)(s->earlyFragmentSampleMaskTestBeforeSampleCounting));
    mvk_put_u32(e, (uint32_t)(s->depthStencilSwizzleOneSupport));
    mvk_put_u32(e, (uint32_t)(s->polygonModePointSize));
    mvk_put_u32(e, (uint32_t)(s->nonStrictSinglePixelWideLinesUseParallelogram));
    mvk_put_u32(e, (uint32_t)(s->nonStrictWideLinesUseParallelogram));
}

static void reply_VkPhysicalDeviceMaintenance6Features(MvkEncoder *e, const VkPhysicalDeviceMaintenance6Features *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maintenance6));
}

static void reply_VkPhysicalDeviceMaintenance6Properties(MvkEncoder *e, const VkPhysicalDeviceMaintenance6Properties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->blockTexelViewCompatibleMultipleLayers));
    mvk_put_u32(e, (uint32_t)(s->maxCombinedImageSamplerDescriptorCount));
    mvk_put_u32(e, (uint32_t)(s->fragmentShadingRateClampCombinerInputs));
}

static void reply_VkPhysicalDeviceMaintenance7FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance7FeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maintenance7));
}

static void reply_VkPhysicalDeviceMaintenance7PropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance7PropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->robustFragmentShadingRateAttachmentAccess));
    mvk_put_u32(e, (uint32_t)(s->separateDepthStencilAttachmentAccess));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetTotalUniformBuffersDynamic));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetTotalStorageBuffersDynamic));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetTotalBuffersDynamic));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindTotalUniformBuffersDynamic));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindTotalStorageBuffersDynamic));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindTotalBuffersDynamic));
}

static void reply_VkPhysicalDeviceLayeredApiPropertiesListKHR(MvkEncoder *e, const VkPhysicalDeviceLayeredApiPropertiesListKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->layeredApiCount));
    mvk_put_u32(e, 0);
}

static void reply_VkPhysicalDeviceLayeredApiPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceLayeredApiPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->vendorID));
    mvk_put_u32(e, (uint32_t)(s->deviceID));
    mvk_put_u32(e, (uint32_t)(s->layeredAPI));
    mvk_put_bytes(e, s->deviceName, sizeof(s->deviceName));
}

static void reply_VkPhysicalDeviceLayeredApiVulkanPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceLayeredApiVulkanPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkPhysicalDeviceProperties2(e, &s->properties);
}

static void reply_VkPhysicalDeviceMaintenance8FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance8FeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maintenance8));
}

static void reply_VkPhysicalDeviceMaintenance9FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance9FeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maintenance9));
}

static void reply_VkPhysicalDeviceMaintenance9PropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance9PropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->image2DViewOf3DSparse));
    mvk_put_u32(e, (uint32_t)(s->defaultVertexAttributeValue));
}

static void reply_VkPhysicalDeviceMaintenance11FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance11FeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maintenance11));
}

static void reply_VkPhysicalDeviceMaintenance10PropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance10PropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rgba4OpaqueBlackSwizzled));
    mvk_put_u32(e, (uint32_t)(s->resolveSrgbFormatAppliesTransferFunction));
    mvk_put_u32(e, (uint32_t)(s->resolveSrgbFormatSupportsTransferFunctionControl));
}

static void reply_VkPhysicalDeviceMaintenance10FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance10FeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maintenance10));
}

static void reply_VkQueueFamilyOwnershipTransferPropertiesKHR(MvkEncoder *e, const VkQueueFamilyOwnershipTransferPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->optimalImageTransferToQueueFamilies));
}

static void reply_VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR(MvkEncoder *e, const VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkExtent3D(e, &s->optimalImageTransferGranularity);
}

static void reply_VkPhysicalDeviceShaderDrawParametersFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderDrawParametersFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderDrawParameters));
}

static void reply_VkPhysicalDeviceShaderFloat16Int8Features(MvkEncoder *e, const VkPhysicalDeviceShaderFloat16Int8Features *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderInt8));
}

static void reply_VkPhysicalDeviceFloatControlsProperties(MvkEncoder *e, const VkPhysicalDeviceFloatControlsProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->denormBehaviorIndependence));
    mvk_put_u32(e, (uint32_t)(s->roundingModeIndependence));
    mvk_put_u32(e, (uint32_t)(s->shaderSignedZeroInfNanPreserveFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderSignedZeroInfNanPreserveFloat32));
    mvk_put_u32(e, (uint32_t)(s->shaderSignedZeroInfNanPreserveFloat64));
    mvk_put_u32(e, (uint32_t)(s->shaderDenormPreserveFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderDenormPreserveFloat32));
    mvk_put_u32(e, (uint32_t)(s->shaderDenormPreserveFloat64));
    mvk_put_u32(e, (uint32_t)(s->shaderDenormFlushToZeroFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderDenormFlushToZeroFloat32));
    mvk_put_u32(e, (uint32_t)(s->shaderDenormFlushToZeroFloat64));
    mvk_put_u32(e, (uint32_t)(s->shaderRoundingModeRTEFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderRoundingModeRTEFloat32));
    mvk_put_u32(e, (uint32_t)(s->shaderRoundingModeRTEFloat64));
    mvk_put_u32(e, (uint32_t)(s->shaderRoundingModeRTZFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderRoundingModeRTZFloat32));
    mvk_put_u32(e, (uint32_t)(s->shaderRoundingModeRTZFloat64));
}

static void reply_VkPhysicalDeviceHostQueryResetFeatures(MvkEncoder *e, const VkPhysicalDeviceHostQueryResetFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->hostQueryReset));
}

static void reply_VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->elapsedTimerQuery));
}

static void reply_VkPhysicalDeviceGlobalPriorityQueryFeatures(MvkEncoder *e, const VkPhysicalDeviceGlobalPriorityQueryFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->globalPriorityQuery));
}

static void reply_VkQueueFamilyGlobalPriorityProperties(MvkEncoder *e, const VkQueueFamilyGlobalPriorityProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->priorityCount));
    mvk_put_bytes(e, s->priorities, sizeof(s->priorities));
}

static void reply_VkPhysicalDeviceDeviceMemoryReportFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDeviceMemoryReportFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->deviceMemoryReport));
}

static void reply_VkPhysicalDeviceConservativeRasterizationPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceConservativeRasterizationPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, mvk_f32_bits(s->primitiveOverestimationSize));
    mvk_put_u32(e, mvk_f32_bits(s->maxExtraPrimitiveOverestimationSize));
    mvk_put_u32(e, mvk_f32_bits(s->extraPrimitiveOverestimationSizeGranularity));
    mvk_put_u32(e, (uint32_t)(s->primitiveUnderestimation));
    mvk_put_u32(e, (uint32_t)(s->conservativePointAndLineRasterization));
    mvk_put_u32(e, (uint32_t)(s->degenerateTrianglesRasterized));
    mvk_put_u32(e, (uint32_t)(s->degenerateLinesRasterized));
    mvk_put_u32(e, (uint32_t)(s->fullyCoveredFragmentShaderInputVariable));
    mvk_put_u32(e, (uint32_t)(s->conservativeRasterizationPostDepthCoverage));
}

static void reply_VkPhysicalDeviceShaderCorePropertiesAMD(MvkEncoder *e, const VkPhysicalDeviceShaderCorePropertiesAMD *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderEngineCount));
    mvk_put_u32(e, (uint32_t)(s->shaderArraysPerEngineCount));
    mvk_put_u32(e, (uint32_t)(s->computeUnitsPerShaderArray));
    mvk_put_u32(e, (uint32_t)(s->simdPerComputeUnit));
    mvk_put_u32(e, (uint32_t)(s->wavefrontsPerSimd));
    mvk_put_u32(e, (uint32_t)(s->wavefrontSize));
    mvk_put_u32(e, (uint32_t)(s->sgprsPerSimd));
    mvk_put_u32(e, (uint32_t)(s->minSgprAllocation));
    mvk_put_u32(e, (uint32_t)(s->maxSgprAllocation));
    mvk_put_u32(e, (uint32_t)(s->sgprAllocationGranularity));
    mvk_put_u32(e, (uint32_t)(s->vgprsPerSimd));
    mvk_put_u32(e, (uint32_t)(s->minVgprAllocation));
    mvk_put_u32(e, (uint32_t)(s->maxVgprAllocation));
    mvk_put_u32(e, (uint32_t)(s->vgprAllocationGranularity));
}

static void reply_VkPhysicalDeviceShaderCoreProperties2AMD(MvkEncoder *e, const VkPhysicalDeviceShaderCoreProperties2AMD *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderCoreFeatures));
    mvk_put_u32(e, (uint32_t)(s->activeComputeUnitCount));
}

static void reply_VkPhysicalDeviceDescriptorIndexingFeatures(MvkEncoder *e, const VkPhysicalDeviceDescriptorIndexingFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderInputAttachmentArrayDynamicIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderUniformTexelBufferArrayDynamicIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageTexelBufferArrayDynamicIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderUniformBufferArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderSampledImageArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageBufferArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageImageArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderInputAttachmentArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderUniformTexelBufferArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageTexelBufferArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingUniformBufferUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingSampledImageUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingStorageImageUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingStorageBufferUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingUniformTexelBufferUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingStorageTexelBufferUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingUpdateUnusedWhilePending));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingPartiallyBound));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingVariableDescriptorCount));
    mvk_put_u32(e, (uint32_t)(s->runtimeDescriptorArray));
}

static void reply_VkPhysicalDeviceDescriptorIndexingProperties(MvkEncoder *e, const VkPhysicalDeviceDescriptorIndexingProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxUpdateAfterBindDescriptorsInAllPools));
    mvk_put_u32(e, (uint32_t)(s->shaderUniformBufferArrayNonUniformIndexingNative));
    mvk_put_u32(e, (uint32_t)(s->shaderSampledImageArrayNonUniformIndexingNative));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageBufferArrayNonUniformIndexingNative));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageImageArrayNonUniformIndexingNative));
    mvk_put_u32(e, (uint32_t)(s->shaderInputAttachmentArrayNonUniformIndexingNative));
    mvk_put_u32(e, (uint32_t)(s->robustBufferAccessUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->quadDivergentImplicitLod));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindSamplers));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindUniformBuffers));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindStorageBuffers));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindSampledImages));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindStorageImages));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindInputAttachments));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageUpdateAfterBindResources));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindSamplers));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindUniformBuffers));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindStorageBuffers));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindSampledImages));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindStorageImages));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindInputAttachments));
}

static void reply_VkDescriptorSetVariableDescriptorCountLayoutSupport(MvkEncoder *e, const VkDescriptorSetVariableDescriptorCountLayoutSupport *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxVariableDescriptorCount));
}

static void reply_VkPhysicalDeviceTimelineSemaphoreFeatures(MvkEncoder *e, const VkPhysicalDeviceTimelineSemaphoreFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->timelineSemaphore));
}

static void reply_VkPhysicalDeviceTimelineSemaphoreProperties(MvkEncoder *e, const VkPhysicalDeviceTimelineSemaphoreProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->maxTimelineSemaphoreValueDifference));
}

static void reply_VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxVertexAttribDivisor));
}

static void reply_VkPhysicalDeviceVertexAttributeDivisorProperties(MvkEncoder *e, const VkPhysicalDeviceVertexAttributeDivisorProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxVertexAttribDivisor));
    mvk_put_u32(e, (uint32_t)(s->supportsNonZeroFirstInstance));
}

static void reply_VkPhysicalDevicePCIBusInfoPropertiesEXT(MvkEncoder *e, const VkPhysicalDevicePCIBusInfoPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pciDomain));
    mvk_put_u32(e, (uint32_t)(s->pciBus));
    mvk_put_u32(e, (uint32_t)(s->pciDevice));
    mvk_put_u32(e, (uint32_t)(s->pciFunction));
}

static void reply_VkPhysicalDevice8BitStorageFeatures(MvkEncoder *e, const VkPhysicalDevice8BitStorageFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->storageBuffer8BitAccess));
    mvk_put_u32(e, (uint32_t)(s->uniformAndStorageBuffer8BitAccess));
    mvk_put_u32(e, (uint32_t)(s->storagePushConstant8));
}

static void reply_VkPhysicalDeviceConditionalRenderingFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceConditionalRenderingFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->conditionalRendering));
    mvk_put_u32(e, (uint32_t)(s->inheritedConditionalRendering));
}

static void reply_VkPhysicalDeviceVulkanMemoryModelFeatures(MvkEncoder *e, const VkPhysicalDeviceVulkanMemoryModelFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->vulkanMemoryModel));
    mvk_put_u32(e, (uint32_t)(s->vulkanMemoryModelDeviceScope));
    mvk_put_u32(e, (uint32_t)(s->vulkanMemoryModelAvailabilityVisibilityChains));
}

static void reply_VkPhysicalDeviceShaderAtomicInt64Features(MvkEncoder *e, const VkPhysicalDeviceShaderAtomicInt64Features *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderBufferInt64Atomics));
    mvk_put_u32(e, (uint32_t)(s->shaderSharedInt64Atomics));
}

static void reply_VkPhysicalDeviceShaderAtomicFloatFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderBufferFloat32Atomics));
    mvk_put_u32(e, (uint32_t)(s->shaderBufferFloat32AtomicAdd));
    mvk_put_u32(e, (uint32_t)(s->shaderBufferFloat64Atomics));
    mvk_put_u32(e, (uint32_t)(s->shaderBufferFloat64AtomicAdd));
    mvk_put_u32(e, (uint32_t)(s->shaderSharedFloat32Atomics));
    mvk_put_u32(e, (uint32_t)(s->shaderSharedFloat32AtomicAdd));
    mvk_put_u32(e, (uint32_t)(s->shaderSharedFloat64Atomics));
    mvk_put_u32(e, (uint32_t)(s->shaderSharedFloat64AtomicAdd));
    mvk_put_u32(e, (uint32_t)(s->shaderImageFloat32Atomics));
    mvk_put_u32(e, (uint32_t)(s->shaderImageFloat32AtomicAdd));
    mvk_put_u32(e, (uint32_t)(s->sparseImageFloat32Atomics));
    mvk_put_u32(e, (uint32_t)(s->sparseImageFloat32AtomicAdd));
}

static void reply_VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderBufferFloat16Atomics));
    mvk_put_u32(e, (uint32_t)(s->shaderBufferFloat16AtomicAdd));
    mvk_put_u32(e, (uint32_t)(s->shaderBufferFloat16AtomicMinMax));
    mvk_put_u32(e, (uint32_t)(s->shaderBufferFloat32AtomicMinMax));
    mvk_put_u32(e, (uint32_t)(s->shaderBufferFloat64AtomicMinMax));
    mvk_put_u32(e, (uint32_t)(s->shaderSharedFloat16Atomics));
    mvk_put_u32(e, (uint32_t)(s->shaderSharedFloat16AtomicAdd));
    mvk_put_u32(e, (uint32_t)(s->shaderSharedFloat16AtomicMinMax));
    mvk_put_u32(e, (uint32_t)(s->shaderSharedFloat32AtomicMinMax));
    mvk_put_u32(e, (uint32_t)(s->shaderSharedFloat64AtomicMinMax));
    mvk_put_u32(e, (uint32_t)(s->shaderImageFloat32AtomicMinMax));
    mvk_put_u32(e, (uint32_t)(s->sparseImageFloat32AtomicMinMax));
}

static void reply_VkPhysicalDeviceVertexAttributeDivisorFeatures(MvkEncoder *e, const VkPhysicalDeviceVertexAttributeDivisorFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->vertexAttributeInstanceRateDivisor));
    mvk_put_u32(e, (uint32_t)(s->vertexAttributeInstanceRateZeroDivisor));
}

static void reply_VkQueueFamilyCheckpointPropertiesNV(MvkEncoder *e, const VkQueueFamilyCheckpointPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->checkpointExecutionStageMask));
}

static void reply_VkPhysicalDeviceDepthStencilResolveProperties(MvkEncoder *e, const VkPhysicalDeviceDepthStencilResolveProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->supportedDepthResolveModes));
    mvk_put_u32(e, (uint32_t)(s->supportedStencilResolveModes));
    mvk_put_u32(e, (uint32_t)(s->independentResolveNone));
    mvk_put_u32(e, (uint32_t)(s->independentResolve));
}

static void reply_VkPhysicalDeviceASTCDecodeFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceASTCDecodeFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->decodeModeSharedExponent));
}

static void reply_VkPhysicalDeviceTransformFeedbackFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceTransformFeedbackFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->transformFeedback));
    mvk_put_u32(e, (uint32_t)(s->geometryStreams));
}

static void reply_VkPhysicalDeviceTransformFeedbackPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceTransformFeedbackPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxTransformFeedbackStreams));
    mvk_put_u32(e, (uint32_t)(s->maxTransformFeedbackBuffers));
    mvk_put_u64(e, (uint64_t)(s->maxTransformFeedbackBufferSize));
    mvk_put_u32(e, (uint32_t)(s->maxTransformFeedbackStreamDataSize));
    mvk_put_u32(e, (uint32_t)(s->maxTransformFeedbackBufferDataSize));
    mvk_put_u32(e, (uint32_t)(s->maxTransformFeedbackBufferDataStride));
    mvk_put_u32(e, (uint32_t)(s->transformFeedbackQueries));
    mvk_put_u32(e, (uint32_t)(s->transformFeedbackStreamsLinesTriangles));
    mvk_put_u32(e, (uint32_t)(s->transformFeedbackRasterizationStreamSelect));
    mvk_put_u32(e, (uint32_t)(s->transformFeedbackDraw));
}

static void reply_VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->representativeFragmentTest));
}

static void reply_VkPhysicalDeviceExclusiveScissorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceExclusiveScissorFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->exclusiveScissor));
}

static void reply_VkPhysicalDeviceCornerSampledImageFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCornerSampledImageFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cornerSampledImage));
}

static void reply_VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->computeDerivativeGroupQuads));
    mvk_put_u32(e, (uint32_t)(s->computeDerivativeGroupLinear));
}

static void reply_VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->meshAndTaskShaderDerivatives));
}

static void reply_VkPhysicalDeviceShaderImageFootprintFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceShaderImageFootprintFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->imageFootprint));
}

static void reply_VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->dedicatedAllocationImageAliasing));
}

static void reply_VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->indirectMemoryCopy));
    mvk_put_u32(e, (uint32_t)(s->indirectMemoryToImageCopy));
}

static void reply_VkPhysicalDeviceCopyMemoryIndirectFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCopyMemoryIndirectFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->indirectCopy));
}

static void reply_VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->supportedQueues));
}

static void reply_VkPhysicalDeviceMemoryDecompressionFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMemoryDecompressionFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->memoryDecompression));
}

static void reply_VkPhysicalDeviceMemoryDecompressionPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMemoryDecompressionPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->decompressionMethods));
    mvk_put_u64(e, (uint64_t)(s->maxDecompressionIndirectCount));
}

static void reply_VkPhysicalDeviceShadingRateImageFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceShadingRateImageFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shadingRateImage));
    mvk_put_u32(e, (uint32_t)(s->shadingRateCoarseSampleOrder));
}

static void reply_VkPhysicalDeviceShadingRateImagePropertiesNV(MvkEncoder *e, const VkPhysicalDeviceShadingRateImagePropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkExtent2D(e, &s->shadingRateTexelSize);
    mvk_put_u32(e, (uint32_t)(s->shadingRatePaletteSize));
    mvk_put_u32(e, (uint32_t)(s->shadingRateMaxCoarseSamples));
}

static void reply_VkPhysicalDeviceInvocationMaskFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceInvocationMaskFeaturesHUAWEI *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->invocationMask));
}

static void reply_VkPhysicalDeviceMeshShaderFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceMeshShaderFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->taskShader));
    mvk_put_u32(e, (uint32_t)(s->meshShader));
}

static void reply_VkPhysicalDeviceMeshShaderPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceMeshShaderPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxDrawMeshTasksCount));
    mvk_put_u32(e, (uint32_t)(s->maxTaskWorkGroupInvocations));
    mvk_put_bytes(e, s->maxTaskWorkGroupSize, sizeof(s->maxTaskWorkGroupSize));
    mvk_put_u32(e, (uint32_t)(s->maxTaskTotalMemorySize));
    mvk_put_u32(e, (uint32_t)(s->maxTaskOutputCount));
    mvk_put_u32(e, (uint32_t)(s->maxMeshWorkGroupInvocations));
    mvk_put_bytes(e, s->maxMeshWorkGroupSize, sizeof(s->maxMeshWorkGroupSize));
    mvk_put_u32(e, (uint32_t)(s->maxMeshTotalMemorySize));
    mvk_put_u32(e, (uint32_t)(s->maxMeshOutputVertices));
    mvk_put_u32(e, (uint32_t)(s->maxMeshOutputPrimitives));
    mvk_put_u32(e, (uint32_t)(s->maxMeshMultiviewViewCount));
    mvk_put_u32(e, (uint32_t)(s->meshOutputPerVertexGranularity));
    mvk_put_u32(e, (uint32_t)(s->meshOutputPerPrimitiveGranularity));
}

static void reply_VkPhysicalDeviceMeshShaderFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMeshShaderFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->taskShader));
    mvk_put_u32(e, (uint32_t)(s->meshShader));
    mvk_put_u32(e, (uint32_t)(s->multiviewMeshShader));
    mvk_put_u32(e, (uint32_t)(s->primitiveFragmentShadingRateMeshShader));
    mvk_put_u32(e, (uint32_t)(s->meshShaderQueries));
}

static void reply_VkPhysicalDeviceMeshShaderPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMeshShaderPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxTaskWorkGroupTotalCount));
    mvk_put_bytes(e, s->maxTaskWorkGroupCount, sizeof(s->maxTaskWorkGroupCount));
    mvk_put_u32(e, (uint32_t)(s->maxTaskWorkGroupInvocations));
    mvk_put_bytes(e, s->maxTaskWorkGroupSize, sizeof(s->maxTaskWorkGroupSize));
    mvk_put_u32(e, (uint32_t)(s->maxTaskPayloadSize));
    mvk_put_u32(e, (uint32_t)(s->maxTaskSharedMemorySize));
    mvk_put_u32(e, (uint32_t)(s->maxTaskPayloadAndSharedMemorySize));
    mvk_put_u32(e, (uint32_t)(s->maxMeshWorkGroupTotalCount));
    mvk_put_bytes(e, s->maxMeshWorkGroupCount, sizeof(s->maxMeshWorkGroupCount));
    mvk_put_u32(e, (uint32_t)(s->maxMeshWorkGroupInvocations));
    mvk_put_bytes(e, s->maxMeshWorkGroupSize, sizeof(s->maxMeshWorkGroupSize));
    mvk_put_u32(e, (uint32_t)(s->maxMeshSharedMemorySize));
    mvk_put_u32(e, (uint32_t)(s->maxMeshPayloadAndSharedMemorySize));
    mvk_put_u32(e, (uint32_t)(s->maxMeshOutputMemorySize));
    mvk_put_u32(e, (uint32_t)(s->maxMeshPayloadAndOutputMemorySize));
    mvk_put_u32(e, (uint32_t)(s->maxMeshOutputComponents));
    mvk_put_u32(e, (uint32_t)(s->maxMeshOutputVertices));
    mvk_put_u32(e, (uint32_t)(s->maxMeshOutputPrimitives));
    mvk_put_u32(e, (uint32_t)(s->maxMeshOutputLayers));
    mvk_put_u32(e, (uint32_t)(s->maxMeshMultiviewViewCount));
    mvk_put_u32(e, (uint32_t)(s->meshOutputPerVertexGranularity));
    mvk_put_u32(e, (uint32_t)(s->meshOutputPerPrimitiveGranularity));
    mvk_put_u32(e, (uint32_t)(s->maxPreferredTaskWorkGroupInvocations));
    mvk_put_u32(e, (uint32_t)(s->maxPreferredMeshWorkGroupInvocations));
    mvk_put_u32(e, (uint32_t)(s->prefersLocalInvocationVertexOutput));
    mvk_put_u32(e, (uint32_t)(s->prefersLocalInvocationPrimitiveOutput));
    mvk_put_u32(e, (uint32_t)(s->prefersCompactVertexOutput));
    mvk_put_u32(e, (uint32_t)(s->prefersCompactPrimitiveOutput));
}

static void reply_VkPhysicalDeviceAccelerationStructureFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceAccelerationStructureFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->accelerationStructure));
    mvk_put_u32(e, (uint32_t)(s->accelerationStructureCaptureReplay));
    mvk_put_u32(e, (uint32_t)(s->accelerationStructureIndirectBuild));
    mvk_put_u32(e, (uint32_t)(s->accelerationStructureHostCommands));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingAccelerationStructureUpdateAfterBind));
}

static void reply_VkPhysicalDeviceRayTracingPipelineFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRayTracingPipelineFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rayTracingPipeline));
    mvk_put_u32(e, (uint32_t)(s->rayTracingPipelineShaderGroupHandleCaptureReplay));
    mvk_put_u32(e, (uint32_t)(s->rayTracingPipelineShaderGroupHandleCaptureReplayMixed));
    mvk_put_u32(e, (uint32_t)(s->rayTracingPipelineTraceRaysIndirect));
    mvk_put_u32(e, (uint32_t)(s->rayTraversalPrimitiveCulling));
}

static void reply_VkPhysicalDeviceRayQueryFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRayQueryFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rayQuery));
}

static void reply_VkPhysicalDeviceAccelerationStructurePropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceAccelerationStructurePropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->maxGeometryCount));
    mvk_put_u64(e, (uint64_t)(s->maxInstanceCount));
    mvk_put_u64(e, (uint64_t)(s->maxPrimitiveCount));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorAccelerationStructures));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindAccelerationStructures));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetAccelerationStructures));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindAccelerationStructures));
    mvk_put_u32(e, (uint32_t)(s->minAccelerationStructureScratchOffsetAlignment));
}

static void reply_VkPhysicalDeviceRayTracingPipelinePropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceRayTracingPipelinePropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderGroupHandleSize));
    mvk_put_u32(e, (uint32_t)(s->maxRayRecursionDepth));
    mvk_put_u32(e, (uint32_t)(s->maxShaderGroupStride));
    mvk_put_u32(e, (uint32_t)(s->shaderGroupBaseAlignment));
    mvk_put_u32(e, (uint32_t)(s->shaderGroupHandleCaptureReplaySize));
    mvk_put_u32(e, (uint32_t)(s->maxRayDispatchInvocationCount));
    mvk_put_u32(e, (uint32_t)(s->shaderGroupHandleAlignment));
    mvk_put_u32(e, (uint32_t)(s->maxRayHitAttributeSize));
}

static void reply_VkPhysicalDeviceRayTracingPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderGroupHandleSize));
    mvk_put_u32(e, (uint32_t)(s->maxRecursionDepth));
    mvk_put_u32(e, (uint32_t)(s->maxShaderGroupStride));
    mvk_put_u32(e, (uint32_t)(s->shaderGroupBaseAlignment));
    mvk_put_u64(e, (uint64_t)(s->maxGeometryCount));
    mvk_put_u64(e, (uint64_t)(s->maxInstanceCount));
    mvk_put_u64(e, (uint64_t)(s->maxTriangleCount));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetAccelerationStructures));
}

static void reply_VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rayTracingMaintenance1));
    mvk_put_u32(e, (uint32_t)(s->rayTracingPipelineTraceRaysIndirect2));
}

static void reply_VkDrmFormatModifierPropertiesListEXT(MvkEncoder *e, const VkDrmFormatModifierPropertiesListEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->drmFormatModifierCount));
    mvk_put_u32(e, 0);
}

static void reply_VkDrmFormatModifierPropertiesEXT(MvkEncoder *e, const VkDrmFormatModifierPropertiesEXT *s)
{
    mvk_put_u64(e, (uint64_t)(s->drmFormatModifier));
    mvk_put_u32(e, (uint32_t)(s->drmFormatModifierPlaneCount));
    mvk_put_u32(e, (uint32_t)(s->drmFormatModifierTilingFeatures));
}

static void reply_VkPhysicalDeviceFragmentDensityMapFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->fragmentDensityMap));
    mvk_put_u32(e, (uint32_t)(s->fragmentDensityMapDynamic));
    mvk_put_u32(e, (uint32_t)(s->fragmentDensityMapNonSubsampledImages));
}

static void reply_VkPhysicalDeviceFragmentDensityMap2FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMap2FeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->fragmentDensityMapDeferred));
}

static void reply_VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->fragmentDensityMapOffset));
}

static void reply_VkPhysicalDeviceFragmentDensityMapPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkExtent2D(e, &s->minFragmentDensityTexelSize);
    reply_VkExtent2D(e, &s->maxFragmentDensityTexelSize);
    mvk_put_u32(e, (uint32_t)(s->fragmentDensityInvocations));
}

static void reply_VkPhysicalDeviceFragmentDensityMap2PropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMap2PropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->subsampledLoads));
    mvk_put_u32(e, (uint32_t)(s->subsampledCoarseReconstructionEarlyAccess));
    mvk_put_u32(e, (uint32_t)(s->maxSubsampledArrayLayers));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetSubsampledSamplers));
}

static void reply_VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkExtent2D(e, &s->fragmentDensityOffsetGranularity);
}

static void reply_VkPhysicalDeviceScalarBlockLayoutFeatures(MvkEncoder *e, const VkPhysicalDeviceScalarBlockLayoutFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->scalarBlockLayout));
}

static void reply_VkPhysicalDeviceUniformBufferStandardLayoutFeatures(MvkEncoder *e, const VkPhysicalDeviceUniformBufferStandardLayoutFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->uniformBufferStandardLayout));
}

static void reply_VkPhysicalDeviceDepthClipEnableFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDepthClipEnableFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->depthClipEnable));
}

static void reply_VkPhysicalDeviceMemoryBudgetPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMemoryBudgetPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_bytes(e, s->heapBudget, sizeof(s->heapBudget));
    mvk_put_bytes(e, s->heapUsage, sizeof(s->heapUsage));
}

static void reply_VkPhysicalDeviceMemoryPriorityFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMemoryPriorityFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->memoryPriority));
}

static void reply_VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pageableDeviceLocalMemory));
}

static void reply_VkPhysicalDeviceBufferDeviceAddressFeatures(MvkEncoder *e, const VkPhysicalDeviceBufferDeviceAddressFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->bufferDeviceAddress));
    mvk_put_u32(e, (uint32_t)(s->bufferDeviceAddressCaptureReplay));
    mvk_put_u32(e, (uint32_t)(s->bufferDeviceAddressMultiDevice));
}

static void reply_VkPhysicalDeviceBufferDeviceAddressFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->bufferDeviceAddress));
    mvk_put_u32(e, (uint32_t)(s->bufferDeviceAddressCaptureReplay));
    mvk_put_u32(e, (uint32_t)(s->bufferDeviceAddressMultiDevice));
}

static void reply_VkFilterCubicImageViewImageFormatPropertiesEXT(MvkEncoder *e, const VkFilterCubicImageViewImageFormatPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->filterCubic));
    mvk_put_u32(e, (uint32_t)(s->filterCubicMinmax));
}

static void reply_VkPhysicalDeviceImagelessFramebufferFeatures(MvkEncoder *e, const VkPhysicalDeviceImagelessFramebufferFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->imagelessFramebuffer));
}

static void reply_VkPhysicalDeviceTextureCompressionASTCHDRFeatures(MvkEncoder *e, const VkPhysicalDeviceTextureCompressionASTCHDRFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->textureCompressionASTC_HDR));
}

static void reply_VkPhysicalDeviceCooperativeMatrixFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrix));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixRobustBufferAccess));
}

static void reply_VkPhysicalDeviceCooperativeMatrixPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixSupportedStages));
}

static void reply_VkPhysicalDeviceYcbcrImageArraysFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->ycbcrImageArrays));
}

static void reply_VkPipelineCreationFeedback(MvkEncoder *e, const VkPipelineCreationFeedback *s)
{
    mvk_put_u32(e, (uint32_t)(s->flags));
    mvk_put_u64(e, (uint64_t)(s->duration));
}

static void reply_VkPhysicalDevicePerformanceQueryFeaturesKHR(MvkEncoder *e, const VkPhysicalDevicePerformanceQueryFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->performanceCounterQueryPools));
    mvk_put_u32(e, (uint32_t)(s->performanceCounterMultipleQueryPools));
}

static void reply_VkPhysicalDevicePerformanceQueryPropertiesKHR(MvkEncoder *e, const VkPhysicalDevicePerformanceQueryPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->allowCommandBufferQueryCopies));
}

static void reply_VkPhysicalDeviceCoverageReductionModeFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCoverageReductionModeFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->coverageReductionMode));
}

static void reply_VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL(MvkEncoder *e, const VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderIntegerFunctions2));
}

static void reply_VkPhysicalDeviceShaderClockFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderClockFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderSubgroupClock));
    mvk_put_u32(e, (uint32_t)(s->shaderDeviceClock));
}

static void reply_VkPhysicalDeviceIndexTypeUint8Features(MvkEncoder *e, const VkPhysicalDeviceIndexTypeUint8Features *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->indexTypeUint8));
}

static void reply_VkPhysicalDeviceShaderSMBuiltinsPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceShaderSMBuiltinsPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderSMCount));
    mvk_put_u32(e, (uint32_t)(s->shaderWarpsPerSM));
}

static void reply_VkPhysicalDeviceShaderSMBuiltinsFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceShaderSMBuiltinsFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderSMBuiltins));
}

static void reply_VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->fragmentShaderSampleInterlock));
    mvk_put_u32(e, (uint32_t)(s->fragmentShaderPixelInterlock));
    mvk_put_u32(e, (uint32_t)(s->fragmentShaderShadingRateInterlock));
}

static void reply_VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures(MvkEncoder *e, const VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->separateDepthStencilLayouts));
}

static void reply_VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->primitiveTopologyListRestart));
    mvk_put_u32(e, (uint32_t)(s->primitiveTopologyPatchListRestart));
}

static void reply_VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR(MvkEncoder *e, const VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pipelineExecutableInfo));
}

static void reply_VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderDemoteToHelperInvocation));
}

static void reply_VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->texelBufferAlignment));
}

static void reply_VkPhysicalDeviceTexelBufferAlignmentProperties(MvkEncoder *e, const VkPhysicalDeviceTexelBufferAlignmentProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->storageTexelBufferOffsetAlignmentBytes));
    mvk_put_u32(e, (uint32_t)(s->storageTexelBufferOffsetSingleTexelAlignment));
    mvk_put_u64(e, (uint64_t)(s->uniformTexelBufferOffsetAlignmentBytes));
    mvk_put_u32(e, (uint32_t)(s->uniformTexelBufferOffsetSingleTexelAlignment));
}

static void reply_VkPhysicalDeviceSubgroupSizeControlFeatures(MvkEncoder *e, const VkPhysicalDeviceSubgroupSizeControlFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->subgroupSizeControl));
    mvk_put_u32(e, (uint32_t)(s->computeFullSubgroups));
}

static void reply_VkPhysicalDeviceSubgroupSizeControlProperties(MvkEncoder *e, const VkPhysicalDeviceSubgroupSizeControlProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->minSubgroupSize));
    mvk_put_u32(e, (uint32_t)(s->maxSubgroupSize));
    mvk_put_u32(e, (uint32_t)(s->maxComputeWorkgroupSubgroups));
    mvk_put_u32(e, (uint32_t)(s->requiredSubgroupSizeStages));
}

static void reply_VkPhysicalDeviceSubpassShadingPropertiesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceSubpassShadingPropertiesHUAWEI *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxSubpassShadingWorkgroupSizeAspectRatio));
}

static void reply_VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_bytes(e, s->maxWorkGroupCount, sizeof(s->maxWorkGroupCount));
    mvk_put_bytes(e, s->maxWorkGroupSize, sizeof(s->maxWorkGroupSize));
    mvk_put_u32(e, (uint32_t)(s->maxOutputClusterCount));
    mvk_put_u64(e, (uint64_t)(s->indirectBufferOffsetAlignment));
}

static void reply_VkPhysicalDeviceLineRasterizationFeatures(MvkEncoder *e, const VkPhysicalDeviceLineRasterizationFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rectangularLines));
    mvk_put_u32(e, (uint32_t)(s->bresenhamLines));
    mvk_put_u32(e, (uint32_t)(s->smoothLines));
    mvk_put_u32(e, (uint32_t)(s->stippledRectangularLines));
    mvk_put_u32(e, (uint32_t)(s->stippledBresenhamLines));
    mvk_put_u32(e, (uint32_t)(s->stippledSmoothLines));
}

static void reply_VkPhysicalDeviceLineRasterizationProperties(MvkEncoder *e, const VkPhysicalDeviceLineRasterizationProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->lineSubPixelPrecisionBits));
}

static void reply_VkPhysicalDevicePipelineCreationCacheControlFeatures(MvkEncoder *e, const VkPhysicalDevicePipelineCreationCacheControlFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pipelineCreationCacheControl));
}

static void reply_VkPhysicalDeviceVulkan11Features(MvkEncoder *e, const VkPhysicalDeviceVulkan11Features *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->storageBuffer16BitAccess));
    mvk_put_u32(e, (uint32_t)(s->uniformAndStorageBuffer16BitAccess));
    mvk_put_u32(e, (uint32_t)(s->storagePushConstant16));
    mvk_put_u32(e, (uint32_t)(s->storageInputOutput16));
    mvk_put_u32(e, (uint32_t)(s->multiview));
    mvk_put_u32(e, (uint32_t)(s->multiviewGeometryShader));
    mvk_put_u32(e, (uint32_t)(s->multiviewTessellationShader));
    mvk_put_u32(e, (uint32_t)(s->variablePointersStorageBuffer));
    mvk_put_u32(e, (uint32_t)(s->variablePointers));
    mvk_put_u32(e, (uint32_t)(s->protectedMemory));
    mvk_put_u32(e, (uint32_t)(s->samplerYcbcrConversion));
    mvk_put_u32(e, (uint32_t)(s->shaderDrawParameters));
}

static void reply_VkPhysicalDeviceVulkan11Properties(MvkEncoder *e, const VkPhysicalDeviceVulkan11Properties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_bytes(e, s->deviceUUID, sizeof(s->deviceUUID));
    mvk_put_bytes(e, s->driverUUID, sizeof(s->driverUUID));
    mvk_put_bytes(e, s->deviceLUID, sizeof(s->deviceLUID));
    mvk_put_u32(e, (uint32_t)(s->deviceNodeMask));
    mvk_put_u32(e, (uint32_t)(s->deviceLUIDValid));
    mvk_put_u32(e, (uint32_t)(s->subgroupSize));
    mvk_put_u32(e, (uint32_t)(s->subgroupSupportedStages));
    mvk_put_u32(e, (uint32_t)(s->subgroupSupportedOperations));
    mvk_put_u32(e, (uint32_t)(s->subgroupQuadOperationsInAllStages));
    mvk_put_u32(e, (uint32_t)(s->pointClippingBehavior));
    mvk_put_u32(e, (uint32_t)(s->maxMultiviewViewCount));
    mvk_put_u32(e, (uint32_t)(s->maxMultiviewInstanceIndex));
    mvk_put_u32(e, (uint32_t)(s->protectedNoFault));
    mvk_put_u32(e, (uint32_t)(s->maxPerSetDescriptors));
    mvk_put_u64(e, (uint64_t)(s->maxMemoryAllocationSize));
}

static void reply_VkPhysicalDeviceVulkan12Features(MvkEncoder *e, const VkPhysicalDeviceVulkan12Features *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->samplerMirrorClampToEdge));
    mvk_put_u32(e, (uint32_t)(s->drawIndirectCount));
    mvk_put_u32(e, (uint32_t)(s->storageBuffer8BitAccess));
    mvk_put_u32(e, (uint32_t)(s->uniformAndStorageBuffer8BitAccess));
    mvk_put_u32(e, (uint32_t)(s->storagePushConstant8));
    mvk_put_u32(e, (uint32_t)(s->shaderBufferInt64Atomics));
    mvk_put_u32(e, (uint32_t)(s->shaderSharedInt64Atomics));
    mvk_put_u32(e, (uint32_t)(s->shaderFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderInt8));
    mvk_put_u32(e, (uint32_t)(s->descriptorIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderInputAttachmentArrayDynamicIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderUniformTexelBufferArrayDynamicIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageTexelBufferArrayDynamicIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderUniformBufferArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderSampledImageArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageBufferArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageImageArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderInputAttachmentArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderUniformTexelBufferArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageTexelBufferArrayNonUniformIndexing));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingUniformBufferUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingSampledImageUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingStorageImageUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingStorageBufferUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingUniformTexelBufferUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingStorageTexelBufferUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingUpdateUnusedWhilePending));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingPartiallyBound));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingVariableDescriptorCount));
    mvk_put_u32(e, (uint32_t)(s->runtimeDescriptorArray));
    mvk_put_u32(e, (uint32_t)(s->samplerFilterMinmax));
    mvk_put_u32(e, (uint32_t)(s->scalarBlockLayout));
    mvk_put_u32(e, (uint32_t)(s->imagelessFramebuffer));
    mvk_put_u32(e, (uint32_t)(s->uniformBufferStandardLayout));
    mvk_put_u32(e, (uint32_t)(s->shaderSubgroupExtendedTypes));
    mvk_put_u32(e, (uint32_t)(s->separateDepthStencilLayouts));
    mvk_put_u32(e, (uint32_t)(s->hostQueryReset));
    mvk_put_u32(e, (uint32_t)(s->timelineSemaphore));
    mvk_put_u32(e, (uint32_t)(s->bufferDeviceAddress));
    mvk_put_u32(e, (uint32_t)(s->bufferDeviceAddressCaptureReplay));
    mvk_put_u32(e, (uint32_t)(s->bufferDeviceAddressMultiDevice));
    mvk_put_u32(e, (uint32_t)(s->vulkanMemoryModel));
    mvk_put_u32(e, (uint32_t)(s->vulkanMemoryModelDeviceScope));
    mvk_put_u32(e, (uint32_t)(s->vulkanMemoryModelAvailabilityVisibilityChains));
    mvk_put_u32(e, (uint32_t)(s->shaderOutputViewportIndex));
    mvk_put_u32(e, (uint32_t)(s->shaderOutputLayer));
    mvk_put_u32(e, (uint32_t)(s->subgroupBroadcastDynamicId));
}

static void reply_VkPhysicalDeviceVulkan12Properties(MvkEncoder *e, const VkPhysicalDeviceVulkan12Properties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->driverID));
    mvk_put_bytes(e, s->driverName, sizeof(s->driverName));
    mvk_put_bytes(e, s->driverInfo, sizeof(s->driverInfo));
    reply_VkConformanceVersion(e, &s->conformanceVersion);
    mvk_put_u32(e, (uint32_t)(s->denormBehaviorIndependence));
    mvk_put_u32(e, (uint32_t)(s->roundingModeIndependence));
    mvk_put_u32(e, (uint32_t)(s->shaderSignedZeroInfNanPreserveFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderSignedZeroInfNanPreserveFloat32));
    mvk_put_u32(e, (uint32_t)(s->shaderSignedZeroInfNanPreserveFloat64));
    mvk_put_u32(e, (uint32_t)(s->shaderDenormPreserveFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderDenormPreserveFloat32));
    mvk_put_u32(e, (uint32_t)(s->shaderDenormPreserveFloat64));
    mvk_put_u32(e, (uint32_t)(s->shaderDenormFlushToZeroFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderDenormFlushToZeroFloat32));
    mvk_put_u32(e, (uint32_t)(s->shaderDenormFlushToZeroFloat64));
    mvk_put_u32(e, (uint32_t)(s->shaderRoundingModeRTEFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderRoundingModeRTEFloat32));
    mvk_put_u32(e, (uint32_t)(s->shaderRoundingModeRTEFloat64));
    mvk_put_u32(e, (uint32_t)(s->shaderRoundingModeRTZFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderRoundingModeRTZFloat32));
    mvk_put_u32(e, (uint32_t)(s->shaderRoundingModeRTZFloat64));
    mvk_put_u32(e, (uint32_t)(s->maxUpdateAfterBindDescriptorsInAllPools));
    mvk_put_u32(e, (uint32_t)(s->shaderUniformBufferArrayNonUniformIndexingNative));
    mvk_put_u32(e, (uint32_t)(s->shaderSampledImageArrayNonUniformIndexingNative));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageBufferArrayNonUniformIndexingNative));
    mvk_put_u32(e, (uint32_t)(s->shaderStorageImageArrayNonUniformIndexingNative));
    mvk_put_u32(e, (uint32_t)(s->shaderInputAttachmentArrayNonUniformIndexingNative));
    mvk_put_u32(e, (uint32_t)(s->robustBufferAccessUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->quadDivergentImplicitLod));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindSamplers));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindUniformBuffers));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindStorageBuffers));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindSampledImages));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindStorageImages));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindInputAttachments));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageUpdateAfterBindResources));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindSamplers));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindUniformBuffers));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindStorageBuffers));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindSampledImages));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindStorageImages));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindInputAttachments));
    mvk_put_u32(e, (uint32_t)(s->supportedDepthResolveModes));
    mvk_put_u32(e, (uint32_t)(s->supportedStencilResolveModes));
    mvk_put_u32(e, (uint32_t)(s->independentResolveNone));
    mvk_put_u32(e, (uint32_t)(s->independentResolve));
    mvk_put_u32(e, (uint32_t)(s->filterMinmaxSingleComponentFormats));
    mvk_put_u32(e, (uint32_t)(s->filterMinmaxImageComponentMapping));
    mvk_put_u64(e, (uint64_t)(s->maxTimelineSemaphoreValueDifference));
    mvk_put_u32(e, (uint32_t)(s->framebufferIntegerColorSampleCounts));
}

static void reply_VkPhysicalDeviceVulkan13Features(MvkEncoder *e, const VkPhysicalDeviceVulkan13Features *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->robustImageAccess));
    mvk_put_u32(e, (uint32_t)(s->inlineUniformBlock));
    mvk_put_u32(e, (uint32_t)(s->descriptorBindingInlineUniformBlockUpdateAfterBind));
    mvk_put_u32(e, (uint32_t)(s->pipelineCreationCacheControl));
    mvk_put_u32(e, (uint32_t)(s->privateData));
    mvk_put_u32(e, (uint32_t)(s->shaderDemoteToHelperInvocation));
    mvk_put_u32(e, (uint32_t)(s->shaderTerminateInvocation));
    mvk_put_u32(e, (uint32_t)(s->subgroupSizeControl));
    mvk_put_u32(e, (uint32_t)(s->computeFullSubgroups));
    mvk_put_u32(e, (uint32_t)(s->synchronization2));
    mvk_put_u32(e, (uint32_t)(s->textureCompressionASTC_HDR));
    mvk_put_u32(e, (uint32_t)(s->shaderZeroInitializeWorkgroupMemory));
    mvk_put_u32(e, (uint32_t)(s->dynamicRendering));
    mvk_put_u32(e, (uint32_t)(s->shaderIntegerDotProduct));
    mvk_put_u32(e, (uint32_t)(s->maintenance4));
}

static void reply_VkPhysicalDeviceVulkan13Properties(MvkEncoder *e, const VkPhysicalDeviceVulkan13Properties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->minSubgroupSize));
    mvk_put_u32(e, (uint32_t)(s->maxSubgroupSize));
    mvk_put_u32(e, (uint32_t)(s->maxComputeWorkgroupSubgroups));
    mvk_put_u32(e, (uint32_t)(s->requiredSubgroupSizeStages));
    mvk_put_u32(e, (uint32_t)(s->maxInlineUniformBlockSize));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorInlineUniformBlocks));
    mvk_put_u32(e, (uint32_t)(s->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetInlineUniformBlocks));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorSetUpdateAfterBindInlineUniformBlocks));
    mvk_put_u32(e, (uint32_t)(s->maxInlineUniformTotalSize));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct8BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct8BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct8BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct4x8BitPackedUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct4x8BitPackedSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct4x8BitPackedMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct16BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct16BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct16BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct32BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct32BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct32BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct64BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct64BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct64BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating8BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating8BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating16BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating16BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating32BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating32BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating64BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating64BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated));
    mvk_put_u64(e, (uint64_t)(s->storageTexelBufferOffsetAlignmentBytes));
    mvk_put_u32(e, (uint32_t)(s->storageTexelBufferOffsetSingleTexelAlignment));
    mvk_put_u64(e, (uint64_t)(s->uniformTexelBufferOffsetAlignmentBytes));
    mvk_put_u32(e, (uint32_t)(s->uniformTexelBufferOffsetSingleTexelAlignment));
    mvk_put_u64(e, (uint64_t)(s->maxBufferSize));
}

static void reply_VkPhysicalDeviceVulkan14Features(MvkEncoder *e, const VkPhysicalDeviceVulkan14Features *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->globalPriorityQuery));
    mvk_put_u32(e, (uint32_t)(s->shaderSubgroupRotate));
    mvk_put_u32(e, (uint32_t)(s->shaderSubgroupRotateClustered));
    mvk_put_u32(e, (uint32_t)(s->shaderFloatControls2));
    mvk_put_u32(e, (uint32_t)(s->shaderExpectAssume));
    mvk_put_u32(e, (uint32_t)(s->rectangularLines));
    mvk_put_u32(e, (uint32_t)(s->bresenhamLines));
    mvk_put_u32(e, (uint32_t)(s->smoothLines));
    mvk_put_u32(e, (uint32_t)(s->stippledRectangularLines));
    mvk_put_u32(e, (uint32_t)(s->stippledBresenhamLines));
    mvk_put_u32(e, (uint32_t)(s->stippledSmoothLines));
    mvk_put_u32(e, (uint32_t)(s->vertexAttributeInstanceRateDivisor));
    mvk_put_u32(e, (uint32_t)(s->vertexAttributeInstanceRateZeroDivisor));
    mvk_put_u32(e, (uint32_t)(s->indexTypeUint8));
    mvk_put_u32(e, (uint32_t)(s->dynamicRenderingLocalRead));
    mvk_put_u32(e, (uint32_t)(s->maintenance5));
    mvk_put_u32(e, (uint32_t)(s->maintenance6));
    mvk_put_u32(e, (uint32_t)(s->pipelineProtectedAccess));
    mvk_put_u32(e, (uint32_t)(s->pipelineRobustness));
    mvk_put_u32(e, (uint32_t)(s->hostImageCopy));
    mvk_put_u32(e, (uint32_t)(s->pushDescriptor));
}

static void reply_VkPhysicalDeviceVulkan14Properties(MvkEncoder *e, const VkPhysicalDeviceVulkan14Properties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->lineSubPixelPrecisionBits));
    mvk_put_u32(e, (uint32_t)(s->maxVertexAttribDivisor));
    mvk_put_u32(e, (uint32_t)(s->supportsNonZeroFirstInstance));
    mvk_put_u32(e, (uint32_t)(s->maxPushDescriptors));
    mvk_put_u32(e, (uint32_t)(s->dynamicRenderingLocalReadDepthStencilAttachments));
    mvk_put_u32(e, (uint32_t)(s->dynamicRenderingLocalReadMultisampledAttachments));
    mvk_put_u32(e, (uint32_t)(s->earlyFragmentMultisampleCoverageAfterSampleCounting));
    mvk_put_u32(e, (uint32_t)(s->earlyFragmentSampleMaskTestBeforeSampleCounting));
    mvk_put_u32(e, (uint32_t)(s->depthStencilSwizzleOneSupport));
    mvk_put_u32(e, (uint32_t)(s->polygonModePointSize));
    mvk_put_u32(e, (uint32_t)(s->nonStrictSinglePixelWideLinesUseParallelogram));
    mvk_put_u32(e, (uint32_t)(s->nonStrictWideLinesUseParallelogram));
    mvk_put_u32(e, (uint32_t)(s->blockTexelViewCompatibleMultipleLayers));
    mvk_put_u32(e, (uint32_t)(s->maxCombinedImageSamplerDescriptorCount));
    mvk_put_u32(e, (uint32_t)(s->fragmentShadingRateClampCombinerInputs));
    mvk_put_u32(e, (uint32_t)(s->defaultRobustnessStorageBuffers));
    mvk_put_u32(e, (uint32_t)(s->defaultRobustnessUniformBuffers));
    mvk_put_u32(e, (uint32_t)(s->defaultRobustnessVertexInputs));
    mvk_put_u32(e, (uint32_t)(s->defaultRobustnessImages));
    mvk_put_u32(e, (uint32_t)(s->copySrcLayoutCount));
    mvk_put_u32(e, 0);
    mvk_put_u32(e, (uint32_t)(s->copyDstLayoutCount));
    mvk_put_u32(e, 0);
    mvk_put_bytes(e, s->optimalTilingLayoutUUID, sizeof(s->optimalTilingLayoutUUID));
    mvk_put_u32(e, (uint32_t)(s->identicalMemoryTypeRequirements));
}

static void reply_VkPhysicalDeviceCoherentMemoryFeaturesAMD(MvkEncoder *e, const VkPhysicalDeviceCoherentMemoryFeaturesAMD *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->deviceCoherentMemory));
}

static void reply_VkPhysicalDeviceGpaFeaturesAMD(MvkEncoder *e, const VkPhysicalDeviceGpaFeaturesAMD *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->perfCounters));
    mvk_put_u32(e, (uint32_t)(s->streamingPerfCounters));
    mvk_put_u32(e, (uint32_t)(s->sqThreadTracing));
    mvk_put_u32(e, (uint32_t)(s->clockModes));
}

static void reply_VkPhysicalDeviceGpaPropertiesAMD(MvkEncoder *e, const VkPhysicalDeviceGpaPropertiesAMD *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->flags));
    mvk_put_u64(e, (uint64_t)(s->maxSqttSeBufferSize));
    mvk_put_u32(e, (uint32_t)(s->shaderEngineCount));
    mvk_put_u32(e, (uint32_t)(s->perfBlockCount));
    mvk_put_u32(e, 0);
}

static void reply_VkPhysicalDeviceGpaProperties2AMD(MvkEncoder *e, const VkPhysicalDeviceGpaProperties2AMD *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->revisionId));
}

static void reply_VkPhysicalDeviceCustomBorderColorPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceCustomBorderColorPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxCustomBorderColorSamplers));
}

static void reply_VkPhysicalDeviceCustomBorderColorFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceCustomBorderColorFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->customBorderColors));
    mvk_put_u32(e, (uint32_t)(s->customBorderColorWithoutFormat));
}

static void reply_VkPhysicalDeviceBorderColorSwizzleFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->borderColorSwizzle));
    mvk_put_u32(e, (uint32_t)(s->borderColorSwizzleFromImage));
}

static void reply_VkPhysicalDeviceExtendedDynamicStateFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState));
}

static void reply_VkPhysicalDeviceExtendedDynamicState2FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState2));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState2LogicOp));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState2PatchControlPoints));
}

static void reply_VkPhysicalDeviceExtendedDynamicState3FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3TessellationDomainOrigin));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3DepthClampEnable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3PolygonMode));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3RasterizationSamples));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3SampleMask));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3AlphaToCoverageEnable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3AlphaToOneEnable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3LogicOpEnable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3ColorBlendEnable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3ColorBlendEquation));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3ColorWriteMask));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3RasterizationStream));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3ConservativeRasterizationMode));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3ExtraPrimitiveOverestimationSize));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3DepthClipEnable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3SampleLocationsEnable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3ColorBlendAdvanced));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3ProvokingVertexMode));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3LineRasterizationMode));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3LineStippleEnable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3DepthClipNegativeOneToOne));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3ViewportWScalingEnable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3ViewportSwizzle));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3CoverageToColorEnable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3CoverageToColorLocation));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3CoverageModulationMode));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3CoverageModulationTableEnable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3CoverageModulationTable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3CoverageReductionMode));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3RepresentativeFragmentTestEnable));
    mvk_put_u32(e, (uint32_t)(s->extendedDynamicState3ShadingRateImageEnable));
}

static void reply_VkPhysicalDeviceExtendedDynamicState3PropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceExtendedDynamicState3PropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->dynamicPrimitiveTopologyUnrestricted));
}

static void reply_VkPhysicalDeviceExtendedFlagsFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceExtendedFlagsFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->extendedFlags));
}

static void reply_VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->partitionedAccelerationStructure));
}

static void reply_VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV(MvkEncoder *e, const VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxPartitionCount));
}

static void reply_VkPhysicalDeviceDiagnosticsConfigFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDiagnosticsConfigFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->diagnosticsConfig));
}

static void reply_VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures(MvkEncoder *e, const VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderZeroInitializeWorkgroupMemory));
}

static void reply_VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderSubgroupUniformControlFlow));
}

static void reply_VkPhysicalDeviceRobustness2FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRobustness2FeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->robustBufferAccess2));
    mvk_put_u32(e, (uint32_t)(s->robustImageAccess2));
    mvk_put_u32(e, (uint32_t)(s->nullDescriptor));
}

static void reply_VkPhysicalDeviceRobustness2PropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceRobustness2PropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->robustStorageBufferAccessSizeAlignment));
    mvk_put_u64(e, (uint64_t)(s->robustUniformBufferAccessSizeAlignment));
}

static void reply_VkPhysicalDeviceImageRobustnessFeatures(MvkEncoder *e, const VkPhysicalDeviceImageRobustnessFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->robustImageAccess));
}

static void reply_VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->workgroupMemoryExplicitLayout));
    mvk_put_u32(e, (uint32_t)(s->workgroupMemoryExplicitLayoutScalarBlockLayout));
    mvk_put_u32(e, (uint32_t)(s->workgroupMemoryExplicitLayout8BitAccess));
    mvk_put_u32(e, (uint32_t)(s->workgroupMemoryExplicitLayout16BitAccess));
}

static void reply_VkPhysicalDevice4444FormatsFeaturesEXT(MvkEncoder *e, const VkPhysicalDevice4444FormatsFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->formatA4R4G4B4));
    mvk_put_u32(e, (uint32_t)(s->formatA4B4G4R4));
}

static void reply_VkPhysicalDeviceSubpassShadingFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceSubpassShadingFeaturesHUAWEI *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->subpassShading));
}

static void reply_VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->clustercullingShader));
    mvk_put_u32(e, (uint32_t)(s->multiviewClusterCullingShader));
}

static void reply_VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->clusterShadingRate));
}

static void reply_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderImageInt64Atomics));
    mvk_put_u32(e, (uint32_t)(s->sparseImageInt64Atomics));
}

static void reply_VkPhysicalDeviceFragmentShadingRateFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRateFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pipelineFragmentShadingRate));
    mvk_put_u32(e, (uint32_t)(s->primitiveFragmentShadingRate));
    mvk_put_u32(e, (uint32_t)(s->attachmentFragmentShadingRate));
}

static void reply_VkPhysicalDeviceFragmentShadingRatePropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRatePropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkExtent2D(e, &s->minFragmentShadingRateAttachmentTexelSize);
    reply_VkExtent2D(e, &s->maxFragmentShadingRateAttachmentTexelSize);
    mvk_put_u32(e, (uint32_t)(s->maxFragmentShadingRateAttachmentTexelSizeAspectRatio));
    mvk_put_u32(e, (uint32_t)(s->primitiveFragmentShadingRateWithMultipleViewports));
    mvk_put_u32(e, (uint32_t)(s->layeredShadingRateAttachments));
    mvk_put_u32(e, (uint32_t)(s->fragmentShadingRateNonTrivialCombinerOps));
    reply_VkExtent2D(e, &s->maxFragmentSize);
    mvk_put_u32(e, (uint32_t)(s->maxFragmentSizeAspectRatio));
    mvk_put_u32(e, (uint32_t)(s->maxFragmentShadingRateCoverageSamples));
    mvk_put_u32(e, (uint32_t)(s->maxFragmentShadingRateRasterizationSamples));
    mvk_put_u32(e, (uint32_t)(s->fragmentShadingRateWithShaderDepthStencilWrites));
    mvk_put_u32(e, (uint32_t)(s->fragmentShadingRateWithSampleMask));
    mvk_put_u32(e, (uint32_t)(s->fragmentShadingRateWithShaderSampleMask));
    mvk_put_u32(e, (uint32_t)(s->fragmentShadingRateWithConservativeRasterization));
    mvk_put_u32(e, (uint32_t)(s->fragmentShadingRateWithFragmentShaderInterlock));
    mvk_put_u32(e, (uint32_t)(s->fragmentShadingRateWithCustomSampleLocations));
    mvk_put_u32(e, (uint32_t)(s->fragmentShadingRateStrictMultiplyCombiner));
}

static void reply_VkPhysicalDeviceShaderTerminateInvocationFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderTerminateInvocationFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderTerminateInvocation));
}

static void reply_VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->fragmentShadingRateEnums));
    mvk_put_u32(e, (uint32_t)(s->supersampleFragmentShadingRates));
    mvk_put_u32(e, (uint32_t)(s->noInvocationFragmentShadingRates));
}

static void reply_VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxFragmentShadingRateInvocationCount));
}

static void reply_VkPhysicalDeviceImage2DViewOf3DFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImage2DViewOf3DFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->image2DViewOf3D));
    mvk_put_u32(e, (uint32_t)(s->sampler2DViewOf3D));
}

static void reply_VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->imageSlicedViewOf3D));
}

static void reply_VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->attachmentFeedbackLoopDynamicState));
}

static void reply_VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->legacyVertexAttributes));
}

static void reply_VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->nativeUnalignedPerformance));
}

static void reply_VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->mutableDescriptorType));
}

static void reply_VkPhysicalDeviceDepthClipControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDepthClipControlFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->depthClipControl));
}

static void reply_VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->zeroInitializeDeviceMemory));
}

static void reply_VkPhysicalDeviceCustomResolveFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceCustomResolveFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->customResolve));
}

static void reply_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->deviceGeneratedCommands));
    mvk_put_u32(e, (uint32_t)(s->dynamicGeneratedPipelineLayout));
}

static void reply_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxIndirectPipelineCount));
    mvk_put_u32(e, (uint32_t)(s->maxIndirectShaderObjectCount));
    mvk_put_u32(e, (uint32_t)(s->maxIndirectSequenceCount));
    mvk_put_u32(e, (uint32_t)(s->maxIndirectCommandsTokenCount));
    mvk_put_u32(e, (uint32_t)(s->maxIndirectCommandsTokenOffset));
    mvk_put_u32(e, (uint32_t)(s->maxIndirectCommandsIndirectStride));
    mvk_put_u32(e, (uint32_t)(s->supportedIndirectCommandsInputModes));
    mvk_put_u32(e, (uint32_t)(s->supportedIndirectCommandsShaderStages));
    mvk_put_u32(e, (uint32_t)(s->supportedIndirectCommandsShaderStagesPipelineBinding));
    mvk_put_u32(e, (uint32_t)(s->supportedIndirectCommandsShaderStagesShaderBinding));
    mvk_put_u32(e, (uint32_t)(s->deviceGeneratedCommandsTransformFeedback));
    mvk_put_u32(e, (uint32_t)(s->deviceGeneratedCommandsMultiDrawIndirectCount));
}

static void reply_VkPhysicalDeviceDepthClampControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDepthClampControlFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->depthClampControl));
}

static void reply_VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->vertexInputDynamicState));
}

static void reply_VkPhysicalDeviceExternalMemoryRDMAFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceExternalMemoryRDMAFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->externalMemoryRDMA));
}

static void reply_VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderRelaxedExtendedInstruction));
}

static void reply_VkPhysicalDeviceColorWriteEnableFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceColorWriteEnableFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->colorWriteEnable));
}

static void reply_VkQueueFamilyCheckpointProperties2NV(MvkEncoder *e, const VkQueueFamilyCheckpointProperties2NV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->checkpointExecutionStageMask));
}

static void reply_VkPhysicalDeviceSynchronization2Features(MvkEncoder *e, const VkPhysicalDeviceSynchronization2Features *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->synchronization2));
}

static void reply_VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->unifiedImageLayouts));
    mvk_put_u32(e, (uint32_t)(s->unifiedImageLayoutsVideo));
}

static void reply_VkPhysicalDeviceHostImageCopyFeatures(MvkEncoder *e, const VkPhysicalDeviceHostImageCopyFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->hostImageCopy));
}

static void reply_VkPhysicalDeviceHostImageCopyProperties(MvkEncoder *e, const VkPhysicalDeviceHostImageCopyProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->copySrcLayoutCount));
    mvk_put_u32(e, 0);
    mvk_put_u32(e, (uint32_t)(s->copyDstLayoutCount));
    mvk_put_u32(e, 0);
    mvk_put_bytes(e, s->optimalTilingLayoutUUID, sizeof(s->optimalTilingLayoutUUID));
    mvk_put_u32(e, (uint32_t)(s->identicalMemoryTypeRequirements));
}

static void reply_VkSubresourceHostMemcpySize(MvkEncoder *e, const VkSubresourceHostMemcpySize *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->size));
}

static void reply_VkHostImageCopyDevicePerformanceQuery(MvkEncoder *e, const VkHostImageCopyDevicePerformanceQuery *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->optimalDeviceAccess));
    mvk_put_u32(e, (uint32_t)(s->identicalMemoryLayout));
}

static void reply_VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->primitivesGeneratedQuery));
    mvk_put_u32(e, (uint32_t)(s->primitivesGeneratedQueryWithRasterizerDiscard));
    mvk_put_u32(e, (uint32_t)(s->primitivesGeneratedQueryWithNonZeroStreams));
}

static void reply_VkPhysicalDeviceLegacyDitheringFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceLegacyDitheringFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->legacyDithering));
}

static void reply_VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->multisampledRenderToSingleSampled));
}

static void reply_VkSubpassResolvePerformanceQueryEXT(MvkEncoder *e, const VkSubpassResolvePerformanceQueryEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->optimal));
}

static void reply_VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->multisampledRenderToSwapchain));
}

static void reply_VkPhysicalDevicePipelineProtectedAccessFeatures(MvkEncoder *e, const VkPhysicalDevicePipelineProtectedAccessFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pipelineProtectedAccess));
}

static void reply_VkPhysicalDeviceInheritedViewportScissorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceInheritedViewportScissorFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->inheritedViewportScissor2D));
}

static void reply_VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->ycbcr2plane444Formats));
}

static void reply_VkPhysicalDeviceProvokingVertexFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceProvokingVertexFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->provokingVertexLast));
    mvk_put_u32(e, (uint32_t)(s->transformFeedbackPreservesProvokingVertex));
}

static void reply_VkPhysicalDeviceProvokingVertexPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceProvokingVertexPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->provokingVertexModePerPipeline));
    mvk_put_u32(e, (uint32_t)(s->transformFeedbackPreservesTriangleFanProvokingVertex));
}

static void reply_VkPhysicalDeviceDescriptorBufferFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorBufferFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->descriptorBuffer));
    mvk_put_u32(e, (uint32_t)(s->descriptorBufferCaptureReplay));
    mvk_put_u32(e, (uint32_t)(s->descriptorBufferImageLayoutIgnored));
    mvk_put_u32(e, (uint32_t)(s->descriptorBufferPushDescriptors));
}

static void reply_VkPhysicalDeviceDescriptorBufferPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorBufferPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->combinedImageSamplerDescriptorSingleArray));
    mvk_put_u32(e, (uint32_t)(s->bufferlessPushDescriptors));
    mvk_put_u32(e, (uint32_t)(s->allowSamplerImageViewPostSubmitCreation));
    mvk_put_u64(e, (uint64_t)(s->descriptorBufferOffsetAlignment));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorBufferBindings));
    mvk_put_u32(e, (uint32_t)(s->maxResourceDescriptorBufferBindings));
    mvk_put_u32(e, (uint32_t)(s->maxSamplerDescriptorBufferBindings));
    mvk_put_u32(e, (uint32_t)(s->maxEmbeddedImmutableSamplerBindings));
    mvk_put_u32(e, (uint32_t)(s->maxEmbeddedImmutableSamplers));
    mvk_put_u64(e, (uint64_t)(s->bufferCaptureReplayDescriptorDataSize));
    mvk_put_u64(e, (uint64_t)(s->imageCaptureReplayDescriptorDataSize));
    mvk_put_u64(e, (uint64_t)(s->imageViewCaptureReplayDescriptorDataSize));
    mvk_put_u64(e, (uint64_t)(s->samplerCaptureReplayDescriptorDataSize));
    mvk_put_u64(e, (uint64_t)(s->accelerationStructureCaptureReplayDescriptorDataSize));
    mvk_put_u64(e, (uint64_t)(s->samplerDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->combinedImageSamplerDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->sampledImageDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->storageImageDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->uniformTexelBufferDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->robustUniformTexelBufferDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->storageTexelBufferDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->robustStorageTexelBufferDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->uniformBufferDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->robustUniformBufferDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->storageBufferDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->robustStorageBufferDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->inputAttachmentDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->accelerationStructureDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->maxSamplerDescriptorBufferRange));
    mvk_put_u64(e, (uint64_t)(s->maxResourceDescriptorBufferRange));
    mvk_put_u64(e, (uint64_t)(s->samplerDescriptorBufferAddressSpaceSize));
    mvk_put_u64(e, (uint64_t)(s->resourceDescriptorBufferAddressSpaceSize));
    mvk_put_u64(e, (uint64_t)(s->descriptorBufferAddressSpaceSize));
}

static void reply_VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->combinedImageSamplerDensityMapDescriptorSize));
}

static void reply_VkPhysicalDeviceShaderIntegerDotProductFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderIntegerDotProductFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderIntegerDotProduct));
}

static void reply_VkPhysicalDeviceShaderIntegerDotProductProperties(MvkEncoder *e, const VkPhysicalDeviceShaderIntegerDotProductProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct8BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct8BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct8BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct4x8BitPackedUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct4x8BitPackedSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct4x8BitPackedMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct16BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct16BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct16BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct32BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct32BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct32BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct64BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct64BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProduct64BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating8BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating8BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating16BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating16BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating32BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating32BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating64BitUnsignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating64BitSignedAccelerated));
    mvk_put_u32(e, (uint32_t)(s->integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated));
}

static void reply_VkPhysicalDeviceDrmPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDrmPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->hasPrimary));
    mvk_put_u32(e, (uint32_t)(s->hasRender));
    mvk_put_u64(e, (uint64_t)(s->primaryMajor));
    mvk_put_u64(e, (uint64_t)(s->primaryMinor));
    mvk_put_u64(e, (uint64_t)(s->renderMajor));
    mvk_put_u64(e, (uint64_t)(s->renderMinor));
}

static void reply_VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->fragmentShaderBarycentric));
}

static void reply_VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->triStripVertexOrderIndependentOfProvokingVertex));
}

static void reply_VkPhysicalDeviceShaderFmaFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderFmaFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderFmaFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderFmaFloat32));
    mvk_put_u32(e, (uint32_t)(s->shaderFmaFloat64));
}

static void reply_VkPhysicalDeviceRayTracingMotionBlurFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingMotionBlurFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rayTracingMotionBlur));
    mvk_put_u32(e, (uint32_t)(s->rayTracingMotionBlurPipelineTraceRaysIndirect));
}

static void reply_VkPhysicalDeviceRayTracingValidationFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingValidationFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rayTracingValidation));
}

static void reply_VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->spheres));
    mvk_put_u32(e, (uint32_t)(s->linearSweptSpheres));
}

static void reply_VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->formatRgba10x6WithoutYCbCrSampler));
}

static void reply_VkFormatProperties3(MvkEncoder *e, const VkFormatProperties3 *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->linearTilingFeatures));
    mvk_put_u64(e, (uint64_t)(s->optimalTilingFeatures));
    mvk_put_u64(e, (uint64_t)(s->bufferFeatures));
}

static void reply_VkFormatProperties4KHR(MvkEncoder *e, const VkFormatProperties4KHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->linearTilingFeatures));
    mvk_put_u64(e, (uint64_t)(s->optimalTilingFeatures));
    mvk_put_u64(e, (uint64_t)(s->bufferFeatures));
}

static void reply_VkDrmFormatModifierPropertiesList2EXT(MvkEncoder *e, const VkDrmFormatModifierPropertiesList2EXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->drmFormatModifierCount));
    mvk_put_u32(e, 0);
}

static void reply_VkDrmFormatModifierProperties2EXT(MvkEncoder *e, const VkDrmFormatModifierProperties2EXT *s)
{
    mvk_put_u64(e, (uint64_t)(s->drmFormatModifier));
    mvk_put_u32(e, (uint32_t)(s->drmFormatModifierPlaneCount));
    mvk_put_u64(e, (uint64_t)(s->drmFormatModifierTilingFeatures));
}

static void reply_VkPhysicalDeviceDynamicRenderingFeatures(MvkEncoder *e, const VkPhysicalDeviceDynamicRenderingFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->dynamicRendering));
}

static void reply_VkPhysicalDeviceImageViewMinLodFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageViewMinLodFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->minLod));
}

static void reply_VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rasterizationOrderColorAttachmentAccess));
    mvk_put_u32(e, (uint32_t)(s->rasterizationOrderDepthAttachmentAccess));
    mvk_put_u32(e, (uint32_t)(s->rasterizationOrderStencilAttachmentAccess));
}

static void reply_VkPhysicalDeviceLinearColorAttachmentFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceLinearColorAttachmentFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->linearColorAttachment));
}

static void reply_VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->graphicsPipelineLibrary));
}

static void reply_VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->graphicsPipelineLibraryFastLinking));
    mvk_put_u32(e, (uint32_t)(s->graphicsPipelineLibraryIndependentInterpolationDecoration));
}

static void reply_VkTensorExplicitTilingFormatPropertiesARM(MvkEncoder *e, const VkTensorExplicitTilingFormatPropertiesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->brick16TilingTensorFeatures));
    mvk_put_u64(e, (uint64_t)(s->brick8TilingTensorFeatures));
    mvk_put_u64(e, (uint64_t)(s->brick4TilingTensorFeatures));
    mvk_put_u64(e, (uint64_t)(s->blockUTilingTensorFeatures));
    mvk_put_u64(e, (uint64_t)(s->blockU64kTilingTensorFeatures));
}

static void reply_VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->descriptorSetHostMapping));
}

static void reply_VkPhysicalDeviceNestedCommandBufferFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceNestedCommandBufferFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->nestedCommandBuffer));
    mvk_put_u32(e, (uint32_t)(s->nestedCommandBufferRendering));
    mvk_put_u32(e, (uint32_t)(s->nestedCommandBufferSimultaneousUse));
}

static void reply_VkPhysicalDeviceNestedCommandBufferPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceNestedCommandBufferPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxCommandBufferNestingLevel));
}

static void reply_VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderModuleIdentifier));
}

static void reply_VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_bytes(e, s->shaderModuleIdentifierAlgorithmUUID, sizeof(s->shaderModuleIdentifierAlgorithmUUID));
}

static void reply_VkPhysicalDeviceImageCompressionControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageCompressionControlFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->imageCompressionControl));
}

static void reply_VkImageCompressionPropertiesEXT(MvkEncoder *e, const VkImageCompressionPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->imageCompressionFlags));
    mvk_put_u32(e, (uint32_t)(s->imageCompressionFixedRateFlags));
}

static void reply_VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->imageCompressionControlSwapchain));
}

static void reply_VkRenderPassCreationFeedbackInfoEXT(MvkEncoder *e, const VkRenderPassCreationFeedbackInfoEXT *s)
{
    mvk_put_u32(e, (uint32_t)(s->postMergeSubpassCount));
}

static void reply_VkRenderPassSubpassFeedbackInfoEXT(MvkEncoder *e, const VkRenderPassSubpassFeedbackInfoEXT *s)
{
    mvk_put_u32(e, (uint32_t)(s->subpassMergeStatus));
    mvk_put_bytes(e, s->description, sizeof(s->description));
    mvk_put_u32(e, (uint32_t)(s->postMergeIndex));
}

static void reply_VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->subpassMergeFeedback));
}

static void reply_VkPhysicalDeviceOpacityMicromapFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceOpacityMicromapFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->micromap));
}

static void reply_VkPhysicalDeviceOpacityMicromapFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceOpacityMicromapFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->micromap));
    mvk_put_u32(e, (uint32_t)(s->micromapCaptureReplay));
    mvk_put_u32(e, (uint32_t)(s->micromapHostCommands));
}

static void reply_VkPhysicalDeviceOpacityMicromapPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceOpacityMicromapPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxOpacity2StateSubdivisionLevel));
    mvk_put_u32(e, (uint32_t)(s->maxOpacity4StateSubdivisionLevel));
    mvk_put_u32(e, (uint32_t)(s->maxOpacityLossy4StateSubdivisionLevel));
    mvk_put_u64(e, (uint64_t)(s->maxMicromapTriangles));
}

static void reply_VkPhysicalDeviceOpacityMicromapPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceOpacityMicromapPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxOpacity2StateSubdivisionLevel));
    mvk_put_u32(e, (uint32_t)(s->maxOpacity4StateSubdivisionLevel));
}

static void reply_VkPhysicalDevicePipelinePropertiesFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePipelinePropertiesFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pipelinePropertiesIdentifier));
}

static void reply_VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD(MvkEncoder *e, const VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderEarlyAndLateFragmentTests));
}

static void reply_VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->nonSeamlessCubeMap));
}

static void reply_VkPhysicalDevicePipelineRobustnessFeatures(MvkEncoder *e, const VkPhysicalDevicePipelineRobustnessFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pipelineRobustness));
}

static void reply_VkPhysicalDevicePipelineRobustnessProperties(MvkEncoder *e, const VkPhysicalDevicePipelineRobustnessProperties *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->defaultRobustnessStorageBuffers));
    mvk_put_u32(e, (uint32_t)(s->defaultRobustnessUniformBuffers));
    mvk_put_u32(e, (uint32_t)(s->defaultRobustnessVertexInputs));
    mvk_put_u32(e, (uint32_t)(s->defaultRobustnessImages));
}

static void reply_VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderMultipleWaitQueues));
}

static void reply_VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM(MvkEncoder *e, const VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxShaderWaitQueues));
}

static void reply_VkPhysicalDeviceImageProcessingFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessingFeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->textureSampleWeighted));
    mvk_put_u32(e, (uint32_t)(s->textureBoxFilter));
    mvk_put_u32(e, (uint32_t)(s->textureBlockMatch));
}

static void reply_VkPhysicalDeviceImageProcessingPropertiesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessingPropertiesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxWeightFilterPhases));
    reply_VkExtent2D(e, &s->maxWeightFilterDimension);
    reply_VkExtent2D(e, &s->maxBlockMatchRegion);
    reply_VkExtent2D(e, &s->maxBoxFilterBlockSize);
}

static void reply_VkPhysicalDeviceAmigoProfilingFeaturesSEC(MvkEncoder *e, const VkPhysicalDeviceAmigoProfilingFeaturesSEC *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->amigoProfiling));
}

static void reply_VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->attachmentFeedbackLoopLayout));
}

static void reply_VkPhysicalDeviceAddressBindingReportFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceAddressBindingReportFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->reportAddressBinding));
}

static void reply_VkPhysicalDeviceOpticalFlowFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceOpticalFlowFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->opticalFlow));
}

static void reply_VkPhysicalDeviceOpticalFlowPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceOpticalFlowPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->supportedOutputGridSizes));
    mvk_put_u32(e, (uint32_t)(s->supportedHintGridSizes));
    mvk_put_u32(e, (uint32_t)(s->hintSupported));
    mvk_put_u32(e, (uint32_t)(s->costSupported));
    mvk_put_u32(e, (uint32_t)(s->bidirectionalFlowSupported));
    mvk_put_u32(e, (uint32_t)(s->globalFlowSupported));
    mvk_put_u32(e, (uint32_t)(s->minWidth));
    mvk_put_u32(e, (uint32_t)(s->minHeight));
    mvk_put_u32(e, (uint32_t)(s->maxWidth));
    mvk_put_u32(e, (uint32_t)(s->maxHeight));
    mvk_put_u32(e, (uint32_t)(s->maxNumRegionsOfInterest));
}

static void reply_VkPhysicalDeviceFaultFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFaultFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->deviceFault));
    mvk_put_u32(e, (uint32_t)(s->deviceFaultVendorBinary));
}

static void reply_VkPhysicalDeviceFaultFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceFaultFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->deviceFault));
    mvk_put_u32(e, (uint32_t)(s->deviceFaultVendorBinary));
    mvk_put_u32(e, (uint32_t)(s->deviceFaultReportMasked));
    mvk_put_u32(e, (uint32_t)(s->deviceFaultDeviceLostOnMasked));
}

static void reply_VkPhysicalDeviceFaultPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceFaultPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxDeviceFaultCount));
}

static void reply_VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR(MvkEncoder *e, const VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pipelineLibraryGroupHandles));
}

static void reply_VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->shaderCoreMask));
    mvk_put_u32(e, (uint32_t)(s->shaderCoreCount));
    mvk_put_u32(e, (uint32_t)(s->shaderWarpsPerCore));
}

static void reply_VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderCoreBuiltins));
}

static void reply_VkPhysicalDeviceFrameBoundaryFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFrameBoundaryFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->frameBoundary));
}

static void reply_VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->dynamicRenderingUnusedAttachments));
}

static void reply_VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->internallySynchronizedQueues));
}

static void reply_VkPhysicalDeviceDepthBiasControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDepthBiasControlFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->depthBiasControl));
    mvk_put_u32(e, (uint32_t)(s->leastRepresentableValueForceUnormRepresentation));
    mvk_put_u32(e, (uint32_t)(s->floatRepresentation));
    mvk_put_u32(e, (uint32_t)(s->depthBiasExact));
}

static void reply_VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rayTracingInvocationReorder));
}

static void reply_VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rayTracingInvocationReorder));
}

static void reply_VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rayTracingInvocationReorderReorderingHint));
    mvk_put_u32(e, (uint32_t)(s->maxShaderBindingTableRecordIndex));
}

static void reply_VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rayTracingInvocationReorderReorderingHint));
}

static void reply_VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->extendedSparseAddressSpace));
}

static void reply_VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV(MvkEncoder *e, const VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->extendedSparseAddressSpaceSize));
    mvk_put_u32(e, (uint32_t)(s->extendedSparseImageUsageFlags));
    mvk_put_u32(e, (uint32_t)(s->extendedSparseBufferUsageFlags));
}

static void reply_VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->multiviewPerViewViewports));
}

static void reply_VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->rayTracingPositionFetch));
}

static void reply_VkPhysicalDeviceShaderCorePropertiesARM(MvkEncoder *e, const VkPhysicalDeviceShaderCorePropertiesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pixelRate));
    mvk_put_u32(e, (uint32_t)(s->texelRate));
    mvk_put_u32(e, (uint32_t)(s->fmaRate));
}

static void reply_VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->multiviewPerViewRenderAreas));
}

static void reply_VkPhysicalDeviceShaderObjectFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderObjectFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderObject));
}

static void reply_VkPhysicalDeviceShaderObjectPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderObjectPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_bytes(e, s->shaderBinaryUUID, sizeof(s->shaderBinaryUUID));
    mvk_put_u32(e, (uint32_t)(s->shaderBinaryVersion));
}

static void reply_VkPhysicalDeviceShaderTileImageFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderTileImageFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderTileImageColorReadAccess));
    mvk_put_u32(e, (uint32_t)(s->shaderTileImageDepthReadAccess));
    mvk_put_u32(e, (uint32_t)(s->shaderTileImageStencilReadAccess));
}

static void reply_VkPhysicalDeviceShaderTileImagePropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderTileImagePropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderTileImageCoherentReadAccelerated));
    mvk_put_u32(e, (uint32_t)(s->shaderTileImageReadSampleFromPixelRateInvocation));
    mvk_put_u32(e, (uint32_t)(s->shaderTileImageReadFromHelperInvocation));
}

static void reply_VkPhysicalDeviceCooperativeMatrixFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrix));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixRobustBufferAccess));
}

static void reply_VkPhysicalDeviceCooperativeMatrixPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixSupportedStages));
}

static void reply_VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixConversion));
}

static void reply_VkPhysicalDeviceCubicClampFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceCubicClampFeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cubicRangeClamp));
}

static void reply_VkPhysicalDeviceYcbcrDegammaFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceYcbcrDegammaFeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->ycbcrDegamma));
}

static void reply_VkPhysicalDeviceCubicWeightsFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceCubicWeightsFeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->selectableCubicWeights));
}

static void reply_VkPhysicalDeviceImageProcessing2FeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessing2FeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->textureBlockMatch2));
}

static void reply_VkPhysicalDeviceImageProcessing2PropertiesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessing2PropertiesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkExtent2D(e, &s->maxBlockMatchWindow);
}

static void reply_VkPhysicalDeviceImageProcessing3FeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessing3FeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->imageGatherLinear));
    mvk_put_u32(e, (uint32_t)(s->imageGatherExtendedModes));
    mvk_put_u32(e, (uint32_t)(s->blockMatchExtendedClampToEdge));
}

static void reply_VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->descriptorPoolOverallocation));
}

static void reply_VkPhysicalDeviceLayeredDriverPropertiesMSFT(MvkEncoder *e, const VkPhysicalDeviceLayeredDriverPropertiesMSFT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->underlyingAPI));
}

static void reply_VkPhysicalDevicePerStageDescriptorSetFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePerStageDescriptorSetFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->perStageDescriptorSet));
    mvk_put_u32(e, (uint32_t)(s->dynamicPipelineLayout));
}

static void reply_VkPhysicalDeviceSchedulingControlsFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceSchedulingControlsFeaturesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->schedulingControls));
}

static void reply_VkPhysicalDeviceSchedulingControlsPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceSchedulingControlsPropertiesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->schedulingControlsFlags));
}

static void reply_VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->schedulingControlsMaxWarpsCount));
    mvk_put_u32(e, (uint32_t)(s->schedulingControlsMaxQueuedBatchesCount));
    mvk_put_u32(e, (uint32_t)(s->schedulingControlsMaxWorkGroupBatchSize));
}

static void reply_VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG(MvkEncoder *e, const VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->relaxedLineRasterization));
}

static void reply_VkPhysicalDeviceRenderPassStripedFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceRenderPassStripedFeaturesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->renderPassStriped));
}

static void reply_VkPhysicalDeviceRenderPassStripedPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceRenderPassStripedPropertiesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    reply_VkExtent2D(e, &s->renderPassStripeGranularity);
    mvk_put_u32(e, (uint32_t)(s->maxRenderPassStripes));
}

static void reply_VkPhysicalDevicePipelineOpacityMicromapFeaturesARM(MvkEncoder *e, const VkPhysicalDevicePipelineOpacityMicromapFeaturesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pipelineOpacityMicromap));
}

static void reply_VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderMaximalReconvergence));
}

static void reply_VkPhysicalDeviceShaderSubgroupRotateFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderSubgroupRotateFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderSubgroupRotate));
    mvk_put_u32(e, (uint32_t)(s->shaderSubgroupRotateClustered));
}

static void reply_VkPhysicalDeviceShaderExpectAssumeFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderExpectAssumeFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderExpectAssume));
}

static void reply_VkPhysicalDeviceShaderFloatControls2Features(MvkEncoder *e, const VkPhysicalDeviceShaderFloatControls2Features *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderFloatControls2));
}

static void reply_VkPhysicalDeviceDynamicRenderingLocalReadFeatures(MvkEncoder *e, const VkPhysicalDeviceDynamicRenderingLocalReadFeatures *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->dynamicRenderingLocalRead));
}

static void reply_VkPhysicalDeviceShaderQuadControlFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderQuadControlFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderQuadControl));
}

static void reply_VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderFloat16VectorAtomics));
}

static void reply_VkPhysicalDeviceMapMemoryPlacedFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMapMemoryPlacedFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->memoryMapPlaced));
    mvk_put_u32(e, (uint32_t)(s->memoryMapRangePlaced));
    mvk_put_u32(e, (uint32_t)(s->memoryUnmapReserve));
}

static void reply_VkPhysicalDeviceMapMemoryPlacedPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMapMemoryPlacedPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->minPlacedMemoryMapAlignment));
}

static void reply_VkPhysicalDeviceShaderBfloat16FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderBfloat16FeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderBFloat16Type));
    mvk_put_u32(e, (uint32_t)(s->shaderBFloat16DotProduct));
    mvk_put_u32(e, (uint32_t)(s->shaderBFloat16CooperativeMatrix));
}

static void reply_VkPhysicalDeviceRawAccessChainsFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRawAccessChainsFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderRawAccessChains));
}

static void reply_VkPhysicalDeviceCommandBufferInheritanceFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCommandBufferInheritanceFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->commandBufferInheritance));
}

static void reply_VkPhysicalDeviceImageAlignmentControlFeaturesMESA(MvkEncoder *e, const VkPhysicalDeviceImageAlignmentControlFeaturesMESA *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->imageAlignmentControl));
}

static void reply_VkPhysicalDeviceImageAlignmentControlPropertiesMESA(MvkEncoder *e, const VkPhysicalDeviceImageAlignmentControlPropertiesMESA *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->supportedImageAlignmentMask));
}

static void reply_VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderReplicatedComposites));
}

static void reply_VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR(MvkEncoder *e, const VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->presentModeFifoLatestReady));
}

static void reply_VkPhysicalDeviceCooperativeMatrix2FeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrix2FeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixWorkgroupScope));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixFlexibleDimensions));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixReductions));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixConversions));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixPerElementOperations));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixTensorAddressing));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixBlockLoads));
}

static void reply_VkPhysicalDeviceCooperativeMatrix2PropertiesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrix2PropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixWorkgroupScopeMaxWorkgroupSize));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixFlexibleDimensionsMaxDimension));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixWorkgroupScopeReservedSharedMemory));
}

static void reply_VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixDecodeVector));
}

static void reply_VkPhysicalDeviceHdrVividFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceHdrVividFeaturesHUAWEI *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->hdrVivid));
}

static void reply_VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->vertexAttributeRobustness));
}

static void reply_VkPhysicalDeviceDepthClampZeroOneFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceDepthClampZeroOneFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->depthClampZeroOne));
}

static void reply_VkPhysicalDeviceCooperativeVectorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeVectorFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cooperativeVector));
    mvk_put_u32(e, (uint32_t)(s->cooperativeVectorTraining));
}

static void reply_VkPhysicalDeviceCooperativeVectorPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeVectorPropertiesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cooperativeVectorSupportedStages));
    mvk_put_u32(e, (uint32_t)(s->cooperativeVectorTrainingFloat16Accumulation));
    mvk_put_u32(e, (uint32_t)(s->cooperativeVectorTrainingFloat32Accumulation));
    mvk_put_u32(e, (uint32_t)(s->maxCooperativeVectorComponents));
}

static void reply_VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxFragmentDensityMapLayers));
}

static void reply_VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->fragmentDensityMapLayered));
}

static void reply_VkPhysicalDevicePresentMeteringFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePresentMeteringFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->presentMetering));
}

static void reply_VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderUniformBufferUnsizedArray));
}

static void reply_VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderMixedFloatDotProductFloat16AccFloat32));
    mvk_put_u32(e, (uint32_t)(s->shaderMixedFloatDotProductFloat16AccFloat16));
    mvk_put_u32(e, (uint32_t)(s->shaderMixedFloatDotProductBFloat16Acc));
    mvk_put_u32(e, (uint32_t)(s->shaderMixedFloatDotProductFloat8AccFloat32));
}

static void reply_VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->primitiveRestartIndex));
}

static void reply_VkPhysicalDeviceFormatPackFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceFormatPackFeaturesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->formatPack));
}

static void reply_VkPhysicalDeviceThrottleHintFeaturesSEC(MvkEncoder *e, const VkPhysicalDeviceThrottleHintFeaturesSEC *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->throttleHint));
}

static void reply_VkPhysicalDeviceShaderFloat8FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderFloat8FeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderFloat8));
    mvk_put_u32(e, (uint32_t)(s->shaderFloat8CooperativeMatrix));
}

static void reply_VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC(MvkEncoder *e, const VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->pipelineCacheIncrementalMode));
}

static void reply_VkPhysicalDeviceDataGraphModelFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceDataGraphModelFeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->dataGraphModel));
}

static void reply_VkPhysicalDeviceShaderUntypedPointersFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderUntypedPointersFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderUntypedPointers));
}

static void reply_VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->videoEncodeRgbConversion));
}

static void reply_VkPhysicalDeviceShader64BitIndexingFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShader64BitIndexingFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shader64BitIndexing));
}

static void reply_VkPhysicalDeviceQueuePerfHintFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceQueuePerfHintFeaturesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->queuePerfHint));
}

static void reply_VkPhysicalDeviceQueuePerfHintPropertiesQCOM(MvkEncoder *e, const VkPhysicalDeviceQueuePerfHintPropertiesQCOM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->supportedQueues));
}

static void reply_VkPhysicalDevicePerformanceCountersByRegionFeaturesARM(MvkEncoder *e, const VkPhysicalDevicePerformanceCountersByRegionFeaturesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->performanceCountersByRegion));
}

static void reply_VkPhysicalDevicePerformanceCountersByRegionPropertiesARM(MvkEncoder *e, const VkPhysicalDevicePerformanceCountersByRegionPropertiesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxPerRegionPerformanceCounters));
    reply_VkExtent2D(e, &s->performanceCounterRegionSize);
    mvk_put_u32(e, (uint32_t)(s->rowStrideAlignment));
    mvk_put_u32(e, (uint32_t)(s->regionAlignment));
    mvk_put_u32(e, (uint32_t)(s->identityTransformOrder));
}

static void reply_VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->computeOccupancyPriority));
}

static void reply_VkPhysicalDeviceShaderLongVectorFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderLongVectorFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->longVector));
}

static void reply_VkPhysicalDeviceShaderLongVectorPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderLongVectorPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxVectorComponents));
}

static void reply_VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->textureCompressionASTC_3D));
}

static void reply_VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderSubgroupPartitioned));
}

static void reply_VkSubsampledImageFormatPropertiesEXT(MvkEncoder *e, const VkSubsampledImageFormatPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->subsampledImageDescriptorCount));
}

static void reply_VkPhysicalDeviceShaderSplitBarrierFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderSplitBarrierFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderSplitBarrier));
}

static void reply_VkPhysicalDeviceShaderSplitBarrierPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderSplitBarrierPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->splitBarrierReservedSharedMemory));
}

static void reply_VkPhysicalDeviceDescriptorHeapFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorHeapFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->descriptorHeap));
    mvk_put_u32(e, (uint32_t)(s->descriptorHeapCaptureReplay));
}

static void reply_VkPhysicalDeviceDescriptorHeapPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorHeapPropertiesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->samplerHeapAlignment));
    mvk_put_u64(e, (uint64_t)(s->resourceHeapAlignment));
    mvk_put_u64(e, (uint64_t)(s->maxSamplerHeapSize));
    mvk_put_u64(e, (uint64_t)(s->maxResourceHeapSize));
    mvk_put_u64(e, (uint64_t)(s->minSamplerHeapReservedRange));
    mvk_put_u64(e, (uint64_t)(s->minSamplerHeapReservedRangeWithEmbedded));
    mvk_put_u64(e, (uint64_t)(s->minResourceHeapReservedRange));
    mvk_put_u64(e, (uint64_t)(s->samplerDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->imageDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->bufferDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->samplerDescriptorAlignment));
    mvk_put_u64(e, (uint64_t)(s->imageDescriptorAlignment));
    mvk_put_u64(e, (uint64_t)(s->bufferDescriptorAlignment));
    mvk_put_u64(e, (uint64_t)(s->maxPushDataSize));
    mvk_put_u64(e, (uint64_t)(s->imageCaptureReplayOpaqueDataSize));
    mvk_put_u32(e, (uint32_t)(s->maxDescriptorHeapEmbeddedSamplers));
    mvk_put_u32(e, (uint32_t)(s->samplerYcbcrConversionCount));
    mvk_put_u32(e, (uint32_t)(s->sparseDescriptorHeaps));
    mvk_put_u32(e, (uint32_t)(s->protectedDescriptorHeaps));
}

static void reply_VkPhysicalDeviceDescriptorHeapTensorPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceDescriptorHeapTensorPropertiesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->tensorDescriptorSize));
    mvk_put_u64(e, (uint64_t)(s->tensorDescriptorAlignment));
    mvk_put_u64(e, (uint64_t)(s->tensorCaptureReplayOpaqueDataSize));
}

static void reply_VkPhysicalDeviceShaderInstrumentationFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceShaderInstrumentationFeaturesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderInstrumentation));
}

static void reply_VkPhysicalDeviceShaderInstrumentationPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceShaderInstrumentationPropertiesARM *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->numMetrics));
    mvk_put_u32(e, (uint32_t)(s->perBasicBlockGranularity));
}

static void reply_VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->deviceAddressCommands));
}

static void reply_VkPhysicalDeviceShaderConstantDataFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderConstantDataFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderConstantData));
}

static void reply_VkPhysicalDeviceShaderAbortFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderAbortFeaturesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderAbort));
}

static void reply_VkPhysicalDeviceShaderAbortPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderAbortPropertiesKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->maxShaderAbortMessageSize));
}

static void reply_VkDeviceFaultShaderAbortMessageInfoKHR(MvkEncoder *e, const VkDeviceFaultShaderAbortMessageInfoKHR *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u64(e, (uint64_t)(s->messageDataSize));
    mvk_put_u32(e, 0);
}

static void reply_VkPhysicalDeviceImageTilingControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageTilingControlFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->imageTilingControl));
}

static void reply_VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->shaderFloat4));
    mvk_put_u32(e, (uint32_t)(s->shaderFloat6));
    mvk_put_u32(e, (uint32_t)(s->shaderFloat8UnsignedE8M0));
    mvk_put_u32(e, (uint32_t)(s->shaderMXInt8));
}

static void reply_VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixProperties2));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixReductions));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixConversions));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixPerElementOperations));
    mvk_put_u32(e, (uint32_t)(s->cooperativeMatrixGetCoordinate));
}

static void reply_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->bufferDeviceAddressAllocationAlignment));
}

static void reply_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE(MvkEncoder *e, const VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->maxBufferDeviceAddressAllocationAlignment));
}

static void reply_VkPhysicalDeviceInfoPropertiesINTEL(MvkEncoder *e, const VkPhysicalDeviceInfoPropertiesINTEL *s)
{
    mvk_reply_chain(e, s->pNext);
    mvk_put_u32(e, (uint32_t)(s->deviceIpVersionArch));
    mvk_put_u32(e, (uint32_t)(s->deviceIpVersionRelease));
    mvk_put_u32(e, (uint32_t)(s->deviceIpVersionRevision));
}

void mvk_reply_chain_struct(MvkEncoder *e, const VkBaseOutStructure *s)
{
    switch ((int)s->sType) {
    case VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2: reply_VkMemoryRequirements2(e, (const VkMemoryRequirements2 *)s); break;
    case VK_STRUCTURE_TYPE_SPARSE_IMAGE_MEMORY_REQUIREMENTS_2: reply_VkSparseImageMemoryRequirements2(e, (const VkSparseImageMemoryRequirements2 *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: reply_VkPhysicalDeviceFeatures2(e, (const VkPhysicalDeviceFeatures2 *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2: reply_VkPhysicalDeviceProperties2(e, (const VkPhysicalDeviceProperties2 *)s); break;
    case VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2: reply_VkFormatProperties2(e, (const VkFormatProperties2 *)s); break;
    case VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2: reply_VkImageFormatProperties2(e, (const VkImageFormatProperties2 *)s); break;
    case VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2: reply_VkQueueFamilyProperties2(e, (const VkQueueFamilyProperties2 *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2: reply_VkPhysicalDeviceMemoryProperties2(e, (const VkPhysicalDeviceMemoryProperties2 *)s); break;
    case VK_STRUCTURE_TYPE_SPARSE_IMAGE_FORMAT_PROPERTIES_2: reply_VkSparseImageFormatProperties2(e, (const VkSparseImageFormatProperties2 *)s); break;
    case VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES: reply_VkExternalBufferProperties(e, (const VkExternalBufferProperties *)s); break;
    case VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES: reply_VkExternalFenceProperties(e, (const VkExternalFenceProperties *)s); break;
    case VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES: reply_VkExternalSemaphoreProperties(e, (const VkExternalSemaphoreProperties *)s); break;
    case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT: reply_VkDescriptorSetLayoutSupport(e, (const VkDescriptorSetLayoutSupport *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES: reply_VkPhysicalDeviceToolProperties(e, (const VkPhysicalDeviceToolProperties *)s); break;
    case VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2: reply_VkSubresourceLayout2(e, (const VkSubresourceLayout2 *)s); break;
    case VK_STRUCTURE_TYPE_IMAGE_VIEW_ADDRESS_PROPERTIES_NVX: reply_VkImageViewAddressPropertiesNVX(e, (const VkImageViewAddressPropertiesNVX *)s); break;
    case VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_KHR: reply_VkPerformanceCounterKHR(e, (const VkPerformanceCounterKHR *)s); break;
    case VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_DESCRIPTION_KHR: reply_VkPerformanceCounterDescriptionKHR(e, (const VkPerformanceCounterDescriptionKHR *)s); break;
    case VK_STRUCTURE_TYPE_GPA_DEVICE_CLOCK_MODE_INFO_AMD: reply_VkGpaDeviceClockModeInfoAMD(e, (const VkGpaDeviceClockModeInfoAMD *)s); break;
    case VK_STRUCTURE_TYPE_GPA_DEVICE_GET_CLOCK_INFO_AMD: reply_VkGpaDeviceGetClockInfoAMD(e, (const VkGpaDeviceGetClockInfoAMD *)s); break;
    case VK_STRUCTURE_TYPE_MULTISAMPLE_PROPERTIES_EXT: reply_VkMultisamplePropertiesEXT(e, (const VkMultisamplePropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR: reply_VkAccelerationStructureBuildSizesInfoKHR(e, (const VkAccelerationStructureBuildSizesInfoKHR *)s); break;
    case VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT: reply_VkImageDrmFormatModifierPropertiesEXT(e, (const VkImageDrmFormatModifierPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR: reply_VkPhysicalDeviceFragmentShadingRateKHR(e, (const VkPhysicalDeviceFragmentShadingRateKHR *)s); break;
    case VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_NV: reply_VkCooperativeMatrixPropertiesNV(e, (const VkCooperativeMatrixPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_FRAMEBUFFER_MIXED_SAMPLES_COMBINATION_NV: reply_VkFramebufferMixedSamplesCombinationNV(e, (const VkFramebufferMixedSamplesCombinationNV *)s); break;
    case VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR: reply_VkPipelineExecutablePropertiesKHR(e, (const VkPipelineExecutablePropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR: reply_VkPipelineExecutableStatisticKHR(e, (const VkPipelineExecutableStatisticKHR *)s); break;
    case VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR: reply_VkPipelineExecutableInternalRepresentationKHR(e, (const VkPipelineExecutableInternalRepresentationKHR *)s); break;
    case VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT: reply_VkMicromapBuildSizesInfoEXT(e, (const VkMicromapBuildSizesInfoEXT *)s); break;
    case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_HOST_MAPPING_INFO_VALVE: reply_VkDescriptorSetLayoutHostMappingInfoVALVE(e, (const VkDescriptorSetLayoutHostMappingInfoVALVE *)s); break;
    case VK_STRUCTURE_TYPE_SHADER_MODULE_IDENTIFIER_EXT: reply_VkShaderModuleIdentifierEXT(e, (const VkShaderModuleIdentifierEXT *)s); break;
    case VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_PROPERTIES_NV: reply_VkOpticalFlowImageFormatPropertiesNV(e, (const VkOpticalFlowImageFormatPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_COOPERATIVE_VECTOR_PROPERTIES_NV: reply_VkCooperativeVectorPropertiesNV(e, (const VkCooperativeVectorPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR: reply_VkCooperativeMatrixPropertiesKHR(e, (const VkCooperativeMatrixPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR: reply_VkDeviceFaultInfoKHR(e, (const VkDeviceFaultInfoKHR *)s); break;
    case VK_STRUCTURE_TYPE_DEVICE_FAULT_DEBUG_INFO_KHR: reply_VkDeviceFaultDebugInfoKHR(e, (const VkDeviceFaultDebugInfoKHR *)s); break;
    case VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_FLEXIBLE_DIMENSIONS_PROPERTIES_NV: reply_VkCooperativeMatrixFlexibleDimensionsPropertiesNV(e, (const VkCooperativeMatrixFlexibleDimensionsPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_ARM: reply_VkPerformanceCounterARM(e, (const VkPerformanceCounterARM *)s); break;
    case VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_DESCRIPTION_ARM: reply_VkPerformanceCounterDescriptionARM(e, (const VkPerformanceCounterDescriptionARM *)s); break;
    case VK_STRUCTURE_TYPE_SHADER_INSTRUMENTATION_METRIC_DESCRIPTION_ARM: reply_VkShaderInstrumentationMetricDescriptionARM(e, (const VkShaderInstrumentationMetricDescriptionARM *)s); break;
    case VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_2_EXT: reply_VkCooperativeMatrixProperties2EXT(e, (const VkCooperativeMatrixProperties2EXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES: reply_VkPhysicalDeviceGroupProperties(e, (const VkPhysicalDeviceGroupProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_NV: reply_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV(e, (const VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_CONSTANT_BANK_FEATURES_NV: reply_VkPhysicalDevicePushConstantBankFeaturesNV(e, (const VkPhysicalDevicePushConstantBankFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_CONSTANT_BANK_PROPERTIES_NV: reply_VkPhysicalDevicePushConstantBankPropertiesNV(e, (const VkPhysicalDevicePushConstantBankPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_COMPUTE_FEATURES_NV: reply_VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV(e, (const VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES: reply_VkPhysicalDevicePrivateDataFeatures(e, (const VkPhysicalDevicePrivateDataFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_BASE_HANDLE_FEATURES_NV: reply_VkPhysicalDevicePrivateDataBaseHandleFeaturesNV(e, (const VkPhysicalDevicePrivateDataBaseHandleFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_NV: reply_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV(e, (const VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV: reply_VkPhysicalDeviceClusterAccelerationStructureFeaturesNV(e, (const VkPhysicalDeviceClusterAccelerationStructureFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_PROPERTIES_NV: reply_VkPhysicalDeviceClusterAccelerationStructurePropertiesNV(e, (const VkPhysicalDeviceClusterAccelerationStructurePropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_PROPERTIES_EXT: reply_VkPhysicalDeviceMultiDrawPropertiesEXT(e, (const VkPhysicalDeviceMultiDrawPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES: reply_VkPhysicalDevicePushDescriptorProperties(e, (const VkPhysicalDevicePushDescriptorProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES: reply_VkPhysicalDeviceDriverProperties(e, (const VkPhysicalDeviceDriverProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: reply_VkPhysicalDeviceVariablePointersFeatures(e, (const VkPhysicalDeviceVariablePointersFeatures *)s); break;
    case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES: reply_VkExternalImageFormatProperties(e, (const VkExternalImageFormatProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: reply_VkPhysicalDeviceIDProperties(e, (const VkPhysicalDeviceIDProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: reply_VkPhysicalDeviceMultiviewFeatures(e, (const VkPhysicalDeviceMultiviewFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES: reply_VkPhysicalDeviceMultiviewProperties(e, (const VkPhysicalDeviceMultiviewProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DISCARD_RECTANGLE_PROPERTIES_EXT: reply_VkPhysicalDeviceDiscardRectanglePropertiesEXT(e, (const VkPhysicalDeviceDiscardRectanglePropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PER_VIEW_ATTRIBUTES_PROPERTIES_NVX: reply_VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX(e, (const VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: reply_VkPhysicalDevice16BitStorageFeatures(e, (const VkPhysicalDevice16BitStorageFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: reply_VkPhysicalDeviceSubgroupProperties(e, (const VkPhysicalDeviceSubgroupProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES: reply_VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures(e, (const VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES: reply_VkPhysicalDevicePointClippingProperties(e, (const VkPhysicalDevicePointClippingProperties *)s); break;
    case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: reply_VkMemoryDedicatedRequirements(e, (const VkMemoryDedicatedRequirements *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES: reply_VkPhysicalDeviceSamplerYcbcrConversionFeatures(e, (const VkPhysicalDeviceSamplerYcbcrConversionFeatures *)s); break;
    case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES: reply_VkSamplerYcbcrConversionImageFormatProperties(e, (const VkSamplerYcbcrConversionImageFormatProperties *)s); break;
    case VK_STRUCTURE_TYPE_TEXTURE_LOD_GATHER_FORMAT_PROPERTIES_AMD: reply_VkTextureLODGatherFormatPropertiesAMD(e, (const VkTextureLODGatherFormatPropertiesAMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES: reply_VkPhysicalDeviceProtectedMemoryFeatures(e, (const VkPhysicalDeviceProtectedMemoryFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES: reply_VkPhysicalDeviceProtectedMemoryProperties(e, (const VkPhysicalDeviceProtectedMemoryProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES: reply_VkPhysicalDeviceSamplerFilterMinmaxProperties(e, (const VkPhysicalDeviceSamplerFilterMinmaxProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLE_LOCATIONS_PROPERTIES_EXT: reply_VkPhysicalDeviceSampleLocationsPropertiesEXT(e, (const VkPhysicalDeviceSampleLocationsPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_FEATURES_EXT: reply_VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT(e, (const VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT: reply_VkPhysicalDeviceMultiDrawFeaturesEXT(e, (const VkPhysicalDeviceMultiDrawFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_PROPERTIES_EXT: reply_VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT(e, (const VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES: reply_VkPhysicalDeviceInlineUniformBlockFeatures(e, (const VkPhysicalDeviceInlineUniformBlockFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES: reply_VkPhysicalDeviceInlineUniformBlockProperties(e, (const VkPhysicalDeviceInlineUniformBlockProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES: reply_VkPhysicalDeviceMaintenance3Properties(e, (const VkPhysicalDeviceMaintenance3Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES: reply_VkPhysicalDeviceMaintenance4Features(e, (const VkPhysicalDeviceMaintenance4Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES: reply_VkPhysicalDeviceMaintenance4Properties(e, (const VkPhysicalDeviceMaintenance4Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES: reply_VkPhysicalDeviceMaintenance5Features(e, (const VkPhysicalDeviceMaintenance5Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_PROPERTIES: reply_VkPhysicalDeviceMaintenance5Properties(e, (const VkPhysicalDeviceMaintenance5Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES: reply_VkPhysicalDeviceMaintenance6Features(e, (const VkPhysicalDeviceMaintenance6Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_PROPERTIES: reply_VkPhysicalDeviceMaintenance6Properties(e, (const VkPhysicalDeviceMaintenance6Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_FEATURES_KHR: reply_VkPhysicalDeviceMaintenance7FeaturesKHR(e, (const VkPhysicalDeviceMaintenance7FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_PROPERTIES_KHR: reply_VkPhysicalDeviceMaintenance7PropertiesKHR(e, (const VkPhysicalDeviceMaintenance7PropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_API_PROPERTIES_LIST_KHR: reply_VkPhysicalDeviceLayeredApiPropertiesListKHR(e, (const VkPhysicalDeviceLayeredApiPropertiesListKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_API_PROPERTIES_KHR: reply_VkPhysicalDeviceLayeredApiPropertiesKHR(e, (const VkPhysicalDeviceLayeredApiPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_API_VULKAN_PROPERTIES_KHR: reply_VkPhysicalDeviceLayeredApiVulkanPropertiesKHR(e, (const VkPhysicalDeviceLayeredApiVulkanPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR: reply_VkPhysicalDeviceMaintenance8FeaturesKHR(e, (const VkPhysicalDeviceMaintenance8FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR: reply_VkPhysicalDeviceMaintenance9FeaturesKHR(e, (const VkPhysicalDeviceMaintenance9FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_PROPERTIES_KHR: reply_VkPhysicalDeviceMaintenance9PropertiesKHR(e, (const VkPhysicalDeviceMaintenance9PropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_11_FEATURES_KHR: reply_VkPhysicalDeviceMaintenance11FeaturesKHR(e, (const VkPhysicalDeviceMaintenance11FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_10_PROPERTIES_KHR: reply_VkPhysicalDeviceMaintenance10PropertiesKHR(e, (const VkPhysicalDeviceMaintenance10PropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_10_FEATURES_KHR: reply_VkPhysicalDeviceMaintenance10FeaturesKHR(e, (const VkPhysicalDeviceMaintenance10FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_QUEUE_FAMILY_OWNERSHIP_TRANSFER_PROPERTIES_KHR: reply_VkQueueFamilyOwnershipTransferPropertiesKHR(e, (const VkQueueFamilyOwnershipTransferPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_QUEUE_FAMILY_OPTIMAL_IMAGE_TRANSFER_GRANULARITY_PROPERTIES_KHR: reply_VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR(e, (const VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES: reply_VkPhysicalDeviceShaderDrawParametersFeatures(e, (const VkPhysicalDeviceShaderDrawParametersFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES: reply_VkPhysicalDeviceShaderFloat16Int8Features(e, (const VkPhysicalDeviceShaderFloat16Int8Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES: reply_VkPhysicalDeviceFloatControlsProperties(e, (const VkPhysicalDeviceFloatControlsProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES: reply_VkPhysicalDeviceHostQueryResetFeatures(e, (const VkPhysicalDeviceHostQueryResetFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ELAPSED_TIMER_QUERY_FEATURES_QCOM: reply_VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM(e, (const VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES: reply_VkPhysicalDeviceGlobalPriorityQueryFeatures(e, (const VkPhysicalDeviceGlobalPriorityQueryFeatures *)s); break;
    case VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES: reply_VkQueueFamilyGlobalPriorityProperties(e, (const VkQueueFamilyGlobalPriorityProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_MEMORY_REPORT_FEATURES_EXT: reply_VkPhysicalDeviceDeviceMemoryReportFeaturesEXT(e, (const VkPhysicalDeviceDeviceMemoryReportFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT: reply_VkPhysicalDeviceConservativeRasterizationPropertiesEXT(e, (const VkPhysicalDeviceConservativeRasterizationPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD: reply_VkPhysicalDeviceShaderCorePropertiesAMD(e, (const VkPhysicalDeviceShaderCorePropertiesAMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_2_AMD: reply_VkPhysicalDeviceShaderCoreProperties2AMD(e, (const VkPhysicalDeviceShaderCoreProperties2AMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES: reply_VkPhysicalDeviceDescriptorIndexingFeatures(e, (const VkPhysicalDeviceDescriptorIndexingFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES: reply_VkPhysicalDeviceDescriptorIndexingProperties(e, (const VkPhysicalDeviceDescriptorIndexingProperties *)s); break;
    case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT: reply_VkDescriptorSetVariableDescriptorCountLayoutSupport(e, (const VkDescriptorSetVariableDescriptorCountLayoutSupport *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES: reply_VkPhysicalDeviceTimelineSemaphoreFeatures(e, (const VkPhysicalDeviceTimelineSemaphoreFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES: reply_VkPhysicalDeviceTimelineSemaphoreProperties(e, (const VkPhysicalDeviceTimelineSemaphoreProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT: reply_VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT(e, (const VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES: reply_VkPhysicalDeviceVertexAttributeDivisorProperties(e, (const VkPhysicalDeviceVertexAttributeDivisorProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT: reply_VkPhysicalDevicePCIBusInfoPropertiesEXT(e, (const VkPhysicalDevicePCIBusInfoPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES: reply_VkPhysicalDevice8BitStorageFeatures(e, (const VkPhysicalDevice8BitStorageFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT: reply_VkPhysicalDeviceConditionalRenderingFeaturesEXT(e, (const VkPhysicalDeviceConditionalRenderingFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES: reply_VkPhysicalDeviceVulkanMemoryModelFeatures(e, (const VkPhysicalDeviceVulkanMemoryModelFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES: reply_VkPhysicalDeviceShaderAtomicInt64Features(e, (const VkPhysicalDeviceShaderAtomicInt64Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT: reply_VkPhysicalDeviceShaderAtomicFloatFeaturesEXT(e, (const VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT: reply_VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT(e, (const VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES: reply_VkPhysicalDeviceVertexAttributeDivisorFeatures(e, (const VkPhysicalDeviceVertexAttributeDivisorFeatures *)s); break;
    case VK_STRUCTURE_TYPE_QUEUE_FAMILY_CHECKPOINT_PROPERTIES_NV: reply_VkQueueFamilyCheckpointPropertiesNV(e, (const VkQueueFamilyCheckpointPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES: reply_VkPhysicalDeviceDepthStencilResolveProperties(e, (const VkPhysicalDeviceDepthStencilResolveProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ASTC_DECODE_FEATURES_EXT: reply_VkPhysicalDeviceASTCDecodeFeaturesEXT(e, (const VkPhysicalDeviceASTCDecodeFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT: reply_VkPhysicalDeviceTransformFeedbackFeaturesEXT(e, (const VkPhysicalDeviceTransformFeedbackFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT: reply_VkPhysicalDeviceTransformFeedbackPropertiesEXT(e, (const VkPhysicalDeviceTransformFeedbackPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_REPRESENTATIVE_FRAGMENT_TEST_FEATURES_NV: reply_VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV(e, (const VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXCLUSIVE_SCISSOR_FEATURES_NV: reply_VkPhysicalDeviceExclusiveScissorFeaturesNV(e, (const VkPhysicalDeviceExclusiveScissorFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CORNER_SAMPLED_IMAGE_FEATURES_NV: reply_VkPhysicalDeviceCornerSampledImageFeaturesNV(e, (const VkPhysicalDeviceCornerSampledImageFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR: reply_VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR(e, (const VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_PROPERTIES_KHR: reply_VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR(e, (const VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_FOOTPRINT_FEATURES_NV: reply_VkPhysicalDeviceShaderImageFootprintFeaturesNV(e, (const VkPhysicalDeviceShaderImageFootprintFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEDICATED_ALLOCATION_IMAGE_ALIASING_FEATURES_NV: reply_VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV(e, (const VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COPY_MEMORY_INDIRECT_FEATURES_KHR: reply_VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR(e, (const VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COPY_MEMORY_INDIRECT_FEATURES_NV: reply_VkPhysicalDeviceCopyMemoryIndirectFeaturesNV(e, (const VkPhysicalDeviceCopyMemoryIndirectFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COPY_MEMORY_INDIRECT_PROPERTIES_KHR: reply_VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR(e, (const VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_FEATURES_EXT: reply_VkPhysicalDeviceMemoryDecompressionFeaturesEXT(e, (const VkPhysicalDeviceMemoryDecompressionFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_PROPERTIES_EXT: reply_VkPhysicalDeviceMemoryDecompressionPropertiesEXT(e, (const VkPhysicalDeviceMemoryDecompressionPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_FEATURES_NV: reply_VkPhysicalDeviceShadingRateImageFeaturesNV(e, (const VkPhysicalDeviceShadingRateImageFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_PROPERTIES_NV: reply_VkPhysicalDeviceShadingRateImagePropertiesNV(e, (const VkPhysicalDeviceShadingRateImagePropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INVOCATION_MASK_FEATURES_HUAWEI: reply_VkPhysicalDeviceInvocationMaskFeaturesHUAWEI(e, (const VkPhysicalDeviceInvocationMaskFeaturesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV: reply_VkPhysicalDeviceMeshShaderFeaturesNV(e, (const VkPhysicalDeviceMeshShaderFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_NV: reply_VkPhysicalDeviceMeshShaderPropertiesNV(e, (const VkPhysicalDeviceMeshShaderPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT: reply_VkPhysicalDeviceMeshShaderFeaturesEXT(e, (const VkPhysicalDeviceMeshShaderFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT: reply_VkPhysicalDeviceMeshShaderPropertiesEXT(e, (const VkPhysicalDeviceMeshShaderPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR: reply_VkPhysicalDeviceAccelerationStructureFeaturesKHR(e, (const VkPhysicalDeviceAccelerationStructureFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR: reply_VkPhysicalDeviceRayTracingPipelineFeaturesKHR(e, (const VkPhysicalDeviceRayTracingPipelineFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR: reply_VkPhysicalDeviceRayQueryFeaturesKHR(e, (const VkPhysicalDeviceRayQueryFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR: reply_VkPhysicalDeviceAccelerationStructurePropertiesKHR(e, (const VkPhysicalDeviceAccelerationStructurePropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR: reply_VkPhysicalDeviceRayTracingPipelinePropertiesKHR(e, (const VkPhysicalDeviceRayTracingPipelinePropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV: reply_VkPhysicalDeviceRayTracingPropertiesNV(e, (const VkPhysicalDeviceRayTracingPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR: reply_VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR(e, (const VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT: reply_VkDrmFormatModifierPropertiesListEXT(e, (const VkDrmFormatModifierPropertiesListEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT: reply_VkPhysicalDeviceFragmentDensityMapFeaturesEXT(e, (const VkPhysicalDeviceFragmentDensityMapFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_FEATURES_EXT: reply_VkPhysicalDeviceFragmentDensityMap2FeaturesEXT(e, (const VkPhysicalDeviceFragmentDensityMap2FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_FEATURES_EXT: reply_VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT(e, (const VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT: reply_VkPhysicalDeviceFragmentDensityMapPropertiesEXT(e, (const VkPhysicalDeviceFragmentDensityMapPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_PROPERTIES_EXT: reply_VkPhysicalDeviceFragmentDensityMap2PropertiesEXT(e, (const VkPhysicalDeviceFragmentDensityMap2PropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_PROPERTIES_EXT: reply_VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT(e, (const VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES: reply_VkPhysicalDeviceScalarBlockLayoutFeatures(e, (const VkPhysicalDeviceScalarBlockLayoutFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES: reply_VkPhysicalDeviceUniformBufferStandardLayoutFeatures(e, (const VkPhysicalDeviceUniformBufferStandardLayoutFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT: reply_VkPhysicalDeviceDepthClipEnableFeaturesEXT(e, (const VkPhysicalDeviceDepthClipEnableFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT: reply_VkPhysicalDeviceMemoryBudgetPropertiesEXT(e, (const VkPhysicalDeviceMemoryBudgetPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT: reply_VkPhysicalDeviceMemoryPriorityFeaturesEXT(e, (const VkPhysicalDeviceMemoryPriorityFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT: reply_VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT(e, (const VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES: reply_VkPhysicalDeviceBufferDeviceAddressFeatures(e, (const VkPhysicalDeviceBufferDeviceAddressFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT: reply_VkPhysicalDeviceBufferDeviceAddressFeaturesEXT(e, (const VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_FILTER_CUBIC_IMAGE_VIEW_IMAGE_FORMAT_PROPERTIES_EXT: reply_VkFilterCubicImageViewImageFormatPropertiesEXT(e, (const VkFilterCubicImageViewImageFormatPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES: reply_VkPhysicalDeviceImagelessFramebufferFeatures(e, (const VkPhysicalDeviceImagelessFramebufferFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES: reply_VkPhysicalDeviceTextureCompressionASTCHDRFeatures(e, (const VkPhysicalDeviceTextureCompressionASTCHDRFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_NV: reply_VkPhysicalDeviceCooperativeMatrixFeaturesNV(e, (const VkPhysicalDeviceCooperativeMatrixFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_NV: reply_VkPhysicalDeviceCooperativeMatrixPropertiesNV(e, (const VkPhysicalDeviceCooperativeMatrixPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_IMAGE_ARRAYS_FEATURES_EXT: reply_VkPhysicalDeviceYcbcrImageArraysFeaturesEXT(e, (const VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR: reply_VkPhysicalDevicePerformanceQueryFeaturesKHR(e, (const VkPhysicalDevicePerformanceQueryFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_PROPERTIES_KHR: reply_VkPhysicalDevicePerformanceQueryPropertiesKHR(e, (const VkPhysicalDevicePerformanceQueryPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COVERAGE_REDUCTION_MODE_FEATURES_NV: reply_VkPhysicalDeviceCoverageReductionModeFeaturesNV(e, (const VkPhysicalDeviceCoverageReductionModeFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_FUNCTIONS_2_FEATURES_INTEL: reply_VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL(e, (const VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR: reply_VkPhysicalDeviceShaderClockFeaturesKHR(e, (const VkPhysicalDeviceShaderClockFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES: reply_VkPhysicalDeviceIndexTypeUint8Features(e, (const VkPhysicalDeviceIndexTypeUint8Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SM_BUILTINS_PROPERTIES_NV: reply_VkPhysicalDeviceShaderSMBuiltinsPropertiesNV(e, (const VkPhysicalDeviceShaderSMBuiltinsPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SM_BUILTINS_FEATURES_NV: reply_VkPhysicalDeviceShaderSMBuiltinsFeaturesNV(e, (const VkPhysicalDeviceShaderSMBuiltinsFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT: reply_VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT(e, (const VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES: reply_VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures(e, (const VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT: reply_VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT(e, (const VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR: reply_VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR(e, (const VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES: reply_VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures(e, (const VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT: reply_VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT(e, (const VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES: reply_VkPhysicalDeviceTexelBufferAlignmentProperties(e, (const VkPhysicalDeviceTexelBufferAlignmentProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES: reply_VkPhysicalDeviceSubgroupSizeControlFeatures(e, (const VkPhysicalDeviceSubgroupSizeControlFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES: reply_VkPhysicalDeviceSubgroupSizeControlProperties(e, (const VkPhysicalDeviceSubgroupSizeControlProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBPASS_SHADING_PROPERTIES_HUAWEI: reply_VkPhysicalDeviceSubpassShadingPropertiesHUAWEI(e, (const VkPhysicalDeviceSubpassShadingPropertiesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_PROPERTIES_HUAWEI: reply_VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI(e, (const VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES: reply_VkPhysicalDeviceLineRasterizationFeatures(e, (const VkPhysicalDeviceLineRasterizationFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES: reply_VkPhysicalDeviceLineRasterizationProperties(e, (const VkPhysicalDeviceLineRasterizationProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES: reply_VkPhysicalDevicePipelineCreationCacheControlFeatures(e, (const VkPhysicalDevicePipelineCreationCacheControlFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: reply_VkPhysicalDeviceVulkan11Features(e, (const VkPhysicalDeviceVulkan11Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES: reply_VkPhysicalDeviceVulkan11Properties(e, (const VkPhysicalDeviceVulkan11Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: reply_VkPhysicalDeviceVulkan12Features(e, (const VkPhysicalDeviceVulkan12Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES: reply_VkPhysicalDeviceVulkan12Properties(e, (const VkPhysicalDeviceVulkan12Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: reply_VkPhysicalDeviceVulkan13Features(e, (const VkPhysicalDeviceVulkan13Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES: reply_VkPhysicalDeviceVulkan13Properties(e, (const VkPhysicalDeviceVulkan13Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES: reply_VkPhysicalDeviceVulkan14Features(e, (const VkPhysicalDeviceVulkan14Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES: reply_VkPhysicalDeviceVulkan14Properties(e, (const VkPhysicalDeviceVulkan14Properties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD: reply_VkPhysicalDeviceCoherentMemoryFeaturesAMD(e, (const VkPhysicalDeviceCoherentMemoryFeaturesAMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GPA_FEATURES_AMD: reply_VkPhysicalDeviceGpaFeaturesAMD(e, (const VkPhysicalDeviceGpaFeaturesAMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GPA_PROPERTIES_AMD: reply_VkPhysicalDeviceGpaPropertiesAMD(e, (const VkPhysicalDeviceGpaPropertiesAMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GPA_PROPERTIES_2_AMD: reply_VkPhysicalDeviceGpaProperties2AMD(e, (const VkPhysicalDeviceGpaProperties2AMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT: reply_VkPhysicalDeviceCustomBorderColorPropertiesEXT(e, (const VkPhysicalDeviceCustomBorderColorPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT: reply_VkPhysicalDeviceCustomBorderColorFeaturesEXT(e, (const VkPhysicalDeviceCustomBorderColorFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT: reply_VkPhysicalDeviceBorderColorSwizzleFeaturesEXT(e, (const VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT: reply_VkPhysicalDeviceExtendedDynamicStateFeaturesEXT(e, (const VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT: reply_VkPhysicalDeviceExtendedDynamicState2FeaturesEXT(e, (const VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT: reply_VkPhysicalDeviceExtendedDynamicState3FeaturesEXT(e, (const VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_PROPERTIES_EXT: reply_VkPhysicalDeviceExtendedDynamicState3PropertiesEXT(e, (const VkPhysicalDeviceExtendedDynamicState3PropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_FLAGS_FEATURES_KHR: reply_VkPhysicalDeviceExtendedFlagsFeaturesKHR(e, (const VkPhysicalDeviceExtendedFlagsFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_FEATURES_NV: reply_VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV(e, (const VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_PROPERTIES_NV: reply_VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV(e, (const VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV: reply_VkPhysicalDeviceDiagnosticsConfigFeaturesNV(e, (const VkPhysicalDeviceDiagnosticsConfigFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES: reply_VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures(e, (const VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_FEATURES_KHR: reply_VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR(e, (const VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_KHR: reply_VkPhysicalDeviceRobustness2FeaturesKHR(e, (const VkPhysicalDeviceRobustness2FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_KHR: reply_VkPhysicalDeviceRobustness2PropertiesKHR(e, (const VkPhysicalDeviceRobustness2PropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES: reply_VkPhysicalDeviceImageRobustnessFeatures(e, (const VkPhysicalDeviceImageRobustnessFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR: reply_VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR(e, (const VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT: reply_VkPhysicalDevice4444FormatsFeaturesEXT(e, (const VkPhysicalDevice4444FormatsFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBPASS_SHADING_FEATURES_HUAWEI: reply_VkPhysicalDeviceSubpassShadingFeaturesHUAWEI(e, (const VkPhysicalDeviceSubpassShadingFeaturesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_FEATURES_HUAWEI: reply_VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI(e, (const VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_VRS_FEATURES_HUAWEI: reply_VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI(e, (const VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT: reply_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT(e, (const VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR: reply_VkPhysicalDeviceFragmentShadingRateFeaturesKHR(e, (const VkPhysicalDeviceFragmentShadingRateFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR: reply_VkPhysicalDeviceFragmentShadingRatePropertiesKHR(e, (const VkPhysicalDeviceFragmentShadingRatePropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES: reply_VkPhysicalDeviceShaderTerminateInvocationFeatures(e, (const VkPhysicalDeviceShaderTerminateInvocationFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_ENUMS_FEATURES_NV: reply_VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV(e, (const VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_ENUMS_PROPERTIES_NV: reply_VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV(e, (const VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_2D_VIEW_OF_3D_FEATURES_EXT: reply_VkPhysicalDeviceImage2DViewOf3DFeaturesEXT(e, (const VkPhysicalDeviceImage2DViewOf3DFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_SLICED_VIEW_OF_3D_FEATURES_EXT: reply_VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT(e, (const VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_FEATURES_EXT: reply_VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT(e, (const VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LEGACY_VERTEX_ATTRIBUTES_FEATURES_EXT: reply_VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT(e, (const VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LEGACY_VERTEX_ATTRIBUTES_PROPERTIES_EXT: reply_VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT(e, (const VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT: reply_VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT(e, (const VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT: reply_VkPhysicalDeviceDepthClipControlFeaturesEXT(e, (const VkPhysicalDeviceDepthClipControlFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_DEVICE_MEMORY_FEATURES_EXT: reply_VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT(e, (const VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_RESOLVE_FEATURES_EXT: reply_VkPhysicalDeviceCustomResolveFeaturesEXT(e, (const VkPhysicalDeviceCustomResolveFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_EXT: reply_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT(e, (const VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_EXT: reply_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT(e, (const VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLAMP_CONTROL_FEATURES_EXT: reply_VkPhysicalDeviceDepthClampControlFeaturesEXT(e, (const VkPhysicalDeviceDepthClampControlFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT: reply_VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT(e, (const VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_RDMA_FEATURES_NV: reply_VkPhysicalDeviceExternalMemoryRDMAFeaturesNV(e, (const VkPhysicalDeviceExternalMemoryRDMAFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_RELAXED_EXTENDED_INSTRUCTION_FEATURES_KHR: reply_VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR(e, (const VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: reply_VkPhysicalDeviceColorWriteEnableFeaturesEXT(e, (const VkPhysicalDeviceColorWriteEnableFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_QUEUE_FAMILY_CHECKPOINT_PROPERTIES_2_NV: reply_VkQueueFamilyCheckpointProperties2NV(e, (const VkQueueFamilyCheckpointProperties2NV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES: reply_VkPhysicalDeviceSynchronization2Features(e, (const VkPhysicalDeviceSynchronization2Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR: reply_VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR(e, (const VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES: reply_VkPhysicalDeviceHostImageCopyFeatures(e, (const VkPhysicalDeviceHostImageCopyFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES: reply_VkPhysicalDeviceHostImageCopyProperties(e, (const VkPhysicalDeviceHostImageCopyProperties *)s); break;
    case VK_STRUCTURE_TYPE_SUBRESOURCE_HOST_MEMCPY_SIZE: reply_VkSubresourceHostMemcpySize(e, (const VkSubresourceHostMemcpySize *)s); break;
    case VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY: reply_VkHostImageCopyDevicePerformanceQuery(e, (const VkHostImageCopyDevicePerformanceQuery *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVES_GENERATED_QUERY_FEATURES_EXT: reply_VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT(e, (const VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LEGACY_DITHERING_FEATURES_EXT: reply_VkPhysicalDeviceLegacyDitheringFeaturesEXT(e, (const VkPhysicalDeviceLegacyDitheringFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT: reply_VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT(e, (const VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_SUBPASS_RESOLVE_PERFORMANCE_QUERY_EXT: reply_VkSubpassResolvePerformanceQueryEXT(e, (const VkSubpassResolvePerformanceQueryEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SWAPCHAIN_FEATURES_EXT: reply_VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT(e, (const VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_PROTECTED_ACCESS_FEATURES: reply_VkPhysicalDevicePipelineProtectedAccessFeatures(e, (const VkPhysicalDevicePipelineProtectedAccessFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INHERITED_VIEWPORT_SCISSOR_FEATURES_NV: reply_VkPhysicalDeviceInheritedViewportScissorFeaturesNV(e, (const VkPhysicalDeviceInheritedViewportScissorFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_2_PLANE_444_FORMATS_FEATURES_EXT: reply_VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT(e, (const VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT: reply_VkPhysicalDeviceProvokingVertexFeaturesEXT(e, (const VkPhysicalDeviceProvokingVertexFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_PROPERTIES_EXT: reply_VkPhysicalDeviceProvokingVertexPropertiesEXT(e, (const VkPhysicalDeviceProvokingVertexPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT: reply_VkPhysicalDeviceDescriptorBufferFeaturesEXT(e, (const VkPhysicalDeviceDescriptorBufferFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT: reply_VkPhysicalDeviceDescriptorBufferPropertiesEXT(e, (const VkPhysicalDeviceDescriptorBufferPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_DENSITY_MAP_PROPERTIES_EXT: reply_VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT(e, (const VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES: reply_VkPhysicalDeviceShaderIntegerDotProductFeatures(e, (const VkPhysicalDeviceShaderIntegerDotProductFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES: reply_VkPhysicalDeviceShaderIntegerDotProductProperties(e, (const VkPhysicalDeviceShaderIntegerDotProductProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT: reply_VkPhysicalDeviceDrmPropertiesEXT(e, (const VkPhysicalDeviceDrmPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR: reply_VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR(e, (const VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_PROPERTIES_KHR: reply_VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR(e, (const VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FMA_FEATURES_KHR: reply_VkPhysicalDeviceShaderFmaFeaturesKHR(e, (const VkPhysicalDeviceShaderFmaFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MOTION_BLUR_FEATURES_NV: reply_VkPhysicalDeviceRayTracingMotionBlurFeaturesNV(e, (const VkPhysicalDeviceRayTracingMotionBlurFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV: reply_VkPhysicalDeviceRayTracingValidationFeaturesNV(e, (const VkPhysicalDeviceRayTracingValidationFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV: reply_VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV(e, (const VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RGBA10X6_FORMATS_FEATURES_EXT: reply_VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT(e, (const VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3: reply_VkFormatProperties3(e, (const VkFormatProperties3 *)s); break;
    case VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_4_KHR: reply_VkFormatProperties4KHR(e, (const VkFormatProperties4KHR *)s); break;
    case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT: reply_VkDrmFormatModifierPropertiesList2EXT(e, (const VkDrmFormatModifierPropertiesList2EXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES: reply_VkPhysicalDeviceDynamicRenderingFeatures(e, (const VkPhysicalDeviceDynamicRenderingFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT: reply_VkPhysicalDeviceImageViewMinLodFeaturesEXT(e, (const VkPhysicalDeviceImageViewMinLodFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT: reply_VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT(e, (const VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINEAR_COLOR_ATTACHMENT_FEATURES_NV: reply_VkPhysicalDeviceLinearColorAttachmentFeaturesNV(e, (const VkPhysicalDeviceLinearColorAttachmentFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT: reply_VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT(e, (const VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_PROPERTIES_EXT: reply_VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT(e, (const VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_TENSOR_EXPLICIT_TILING_FORMAT_PROPERTIES_ARM: reply_VkTensorExplicitTilingFormatPropertiesARM(e, (const VkTensorExplicitTilingFormatPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_SET_HOST_MAPPING_FEATURES_VALVE: reply_VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE(e, (const VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NESTED_COMMAND_BUFFER_FEATURES_EXT: reply_VkPhysicalDeviceNestedCommandBufferFeaturesEXT(e, (const VkPhysicalDeviceNestedCommandBufferFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NESTED_COMMAND_BUFFER_PROPERTIES_EXT: reply_VkPhysicalDeviceNestedCommandBufferPropertiesEXT(e, (const VkPhysicalDeviceNestedCommandBufferPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_FEATURES_EXT: reply_VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT(e, (const VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_PROPERTIES_EXT: reply_VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT(e, (const VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_FEATURES_EXT: reply_VkPhysicalDeviceImageCompressionControlFeaturesEXT(e, (const VkPhysicalDeviceImageCompressionControlFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT: reply_VkImageCompressionPropertiesEXT(e, (const VkImageCompressionPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN_FEATURES_EXT: reply_VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT(e, (const VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBPASS_MERGE_FEEDBACK_FEATURES_EXT: reply_VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT(e, (const VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_KHR: reply_VkPhysicalDeviceOpacityMicromapFeaturesKHR(e, (const VkPhysicalDeviceOpacityMicromapFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT: reply_VkPhysicalDeviceOpacityMicromapFeaturesEXT(e, (const VkPhysicalDeviceOpacityMicromapFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_KHR: reply_VkPhysicalDeviceOpacityMicromapPropertiesKHR(e, (const VkPhysicalDeviceOpacityMicromapPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT: reply_VkPhysicalDeviceOpacityMicromapPropertiesEXT(e, (const VkPhysicalDeviceOpacityMicromapPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_PROPERTIES_FEATURES_EXT: reply_VkPhysicalDevicePipelinePropertiesFeaturesEXT(e, (const VkPhysicalDevicePipelinePropertiesFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EARLY_AND_LATE_FRAGMENT_TESTS_FEATURES_AMD: reply_VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD(e, (const VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT: reply_VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT(e, (const VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES: reply_VkPhysicalDevicePipelineRobustnessFeatures(e, (const VkPhysicalDevicePipelineRobustnessFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_PROPERTIES: reply_VkPhysicalDevicePipelineRobustnessProperties(e, (const VkPhysicalDevicePipelineRobustnessProperties *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MULTIPLE_WAIT_QUEUES_FEATURES_QCOM: reply_VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM(e, (const VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MULTIPLE_WAIT_QUEUES_PROPERTIES_QCOM: reply_VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM(e, (const VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_FEATURES_QCOM: reply_VkPhysicalDeviceImageProcessingFeaturesQCOM(e, (const VkPhysicalDeviceImageProcessingFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_PROPERTIES_QCOM: reply_VkPhysicalDeviceImageProcessingPropertiesQCOM(e, (const VkPhysicalDeviceImageProcessingPropertiesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_AMIGO_PROFILING_FEATURES_SEC: reply_VkPhysicalDeviceAmigoProfilingFeaturesSEC(e, (const VkPhysicalDeviceAmigoProfilingFeaturesSEC *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT: reply_VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT(e, (const VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT: reply_VkPhysicalDeviceAddressBindingReportFeaturesEXT(e, (const VkPhysicalDeviceAddressBindingReportFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV: reply_VkPhysicalDeviceOpticalFlowFeaturesNV(e, (const VkPhysicalDeviceOpticalFlowFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_PROPERTIES_NV: reply_VkPhysicalDeviceOpticalFlowPropertiesNV(e, (const VkPhysicalDeviceOpticalFlowPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT: reply_VkPhysicalDeviceFaultFeaturesEXT(e, (const VkPhysicalDeviceFaultFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_KHR: reply_VkPhysicalDeviceFaultFeaturesKHR(e, (const VkPhysicalDeviceFaultFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_PROPERTIES_KHR: reply_VkPhysicalDeviceFaultPropertiesKHR(e, (const VkPhysicalDeviceFaultPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_LIBRARY_GROUP_HANDLES_FEATURES_KHR: reply_VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR(e, (const VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_BUILTINS_PROPERTIES_ARM: reply_VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM(e, (const VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_BUILTINS_FEATURES_ARM: reply_VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM(e, (const VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAME_BOUNDARY_FEATURES_EXT: reply_VkPhysicalDeviceFrameBoundaryFeaturesEXT(e, (const VkPhysicalDeviceFrameBoundaryFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT: reply_VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT(e, (const VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INTERNALLY_SYNCHRONIZED_QUEUES_FEATURES_KHR: reply_VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR(e, (const VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT: reply_VkPhysicalDeviceDepthBiasControlFeaturesEXT(e, (const VkPhysicalDeviceDepthBiasControlFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_EXT: reply_VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT(e, (const VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV: reply_VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV(e, (const VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_EXT: reply_VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT(e, (const VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_NV: reply_VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV(e, (const VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_SPARSE_ADDRESS_SPACE_FEATURES_NV: reply_VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV(e, (const VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_SPARSE_ADDRESS_SPACE_PROPERTIES_NV: reply_VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV(e, (const VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PER_VIEW_VIEWPORTS_FEATURES_QCOM: reply_VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM(e, (const VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR: reply_VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR(e, (const VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_ARM: reply_VkPhysicalDeviceShaderCorePropertiesARM(e, (const VkPhysicalDeviceShaderCorePropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PER_VIEW_RENDER_AREAS_FEATURES_QCOM: reply_VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM(e, (const VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT: reply_VkPhysicalDeviceShaderObjectFeaturesEXT(e, (const VkPhysicalDeviceShaderObjectFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_PROPERTIES_EXT: reply_VkPhysicalDeviceShaderObjectPropertiesEXT(e, (const VkPhysicalDeviceShaderObjectPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TILE_IMAGE_FEATURES_EXT: reply_VkPhysicalDeviceShaderTileImageFeaturesEXT(e, (const VkPhysicalDeviceShaderTileImageFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TILE_IMAGE_PROPERTIES_EXT: reply_VkPhysicalDeviceShaderTileImagePropertiesEXT(e, (const VkPhysicalDeviceShaderTileImagePropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR: reply_VkPhysicalDeviceCooperativeMatrixFeaturesKHR(e, (const VkPhysicalDeviceCooperativeMatrixFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_KHR: reply_VkPhysicalDeviceCooperativeMatrixPropertiesKHR(e, (const VkPhysicalDeviceCooperativeMatrixPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_CONVERSION_FEATURES_QCOM: reply_VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM(e, (const VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUBIC_CLAMP_FEATURES_QCOM: reply_VkPhysicalDeviceCubicClampFeaturesQCOM(e, (const VkPhysicalDeviceCubicClampFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_DEGAMMA_FEATURES_QCOM: reply_VkPhysicalDeviceYcbcrDegammaFeaturesQCOM(e, (const VkPhysicalDeviceYcbcrDegammaFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUBIC_WEIGHTS_FEATURES_QCOM: reply_VkPhysicalDeviceCubicWeightsFeaturesQCOM(e, (const VkPhysicalDeviceCubicWeightsFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_2_FEATURES_QCOM: reply_VkPhysicalDeviceImageProcessing2FeaturesQCOM(e, (const VkPhysicalDeviceImageProcessing2FeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_2_PROPERTIES_QCOM: reply_VkPhysicalDeviceImageProcessing2PropertiesQCOM(e, (const VkPhysicalDeviceImageProcessing2PropertiesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_3_FEATURES_QCOM: reply_VkPhysicalDeviceImageProcessing3FeaturesQCOM(e, (const VkPhysicalDeviceImageProcessing3FeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_POOL_OVERALLOCATION_FEATURES_NV: reply_VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV(e, (const VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_DRIVER_PROPERTIES_MSFT: reply_VkPhysicalDeviceLayeredDriverPropertiesMSFT(e, (const VkPhysicalDeviceLayeredDriverPropertiesMSFT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PER_STAGE_DESCRIPTOR_SET_FEATURES_NV: reply_VkPhysicalDevicePerStageDescriptorSetFeaturesNV(e, (const VkPhysicalDevicePerStageDescriptorSetFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCHEDULING_CONTROLS_FEATURES_ARM: reply_VkPhysicalDeviceSchedulingControlsFeaturesARM(e, (const VkPhysicalDeviceSchedulingControlsFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCHEDULING_CONTROLS_PROPERTIES_ARM: reply_VkPhysicalDeviceSchedulingControlsPropertiesARM(e, (const VkPhysicalDeviceSchedulingControlsPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCHEDULING_CONTROLS_DISPATCH_PARAMETERS_PROPERTIES_ARM: reply_VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM(e, (const VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RELAXED_LINE_RASTERIZATION_FEATURES_IMG: reply_VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG(e, (const VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RENDER_PASS_STRIPED_FEATURES_ARM: reply_VkPhysicalDeviceRenderPassStripedFeaturesARM(e, (const VkPhysicalDeviceRenderPassStripedFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RENDER_PASS_STRIPED_PROPERTIES_ARM: reply_VkPhysicalDeviceRenderPassStripedPropertiesARM(e, (const VkPhysicalDeviceRenderPassStripedPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_OPACITY_MICROMAP_FEATURES_ARM: reply_VkPhysicalDevicePipelineOpacityMicromapFeaturesARM(e, (const VkPhysicalDevicePipelineOpacityMicromapFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MAXIMAL_RECONVERGENCE_FEATURES_KHR: reply_VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR(e, (const VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES: reply_VkPhysicalDeviceShaderSubgroupRotateFeatures(e, (const VkPhysicalDeviceShaderSubgroupRotateFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EXPECT_ASSUME_FEATURES: reply_VkPhysicalDeviceShaderExpectAssumeFeatures(e, (const VkPhysicalDeviceShaderExpectAssumeFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES: reply_VkPhysicalDeviceShaderFloatControls2Features(e, (const VkPhysicalDeviceShaderFloatControls2Features *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES: reply_VkPhysicalDeviceDynamicRenderingLocalReadFeatures(e, (const VkPhysicalDeviceDynamicRenderingLocalReadFeatures *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_QUAD_CONTROL_FEATURES_KHR: reply_VkPhysicalDeviceShaderQuadControlFeaturesKHR(e, (const VkPhysicalDeviceShaderQuadControlFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT16_VECTOR_FEATURES_NV: reply_VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV(e, (const VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_FEATURES_EXT: reply_VkPhysicalDeviceMapMemoryPlacedFeaturesEXT(e, (const VkPhysicalDeviceMapMemoryPlacedFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_PROPERTIES_EXT: reply_VkPhysicalDeviceMapMemoryPlacedPropertiesEXT(e, (const VkPhysicalDeviceMapMemoryPlacedPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_BFLOAT16_FEATURES_KHR: reply_VkPhysicalDeviceShaderBfloat16FeaturesKHR(e, (const VkPhysicalDeviceShaderBfloat16FeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAW_ACCESS_CHAINS_FEATURES_NV: reply_VkPhysicalDeviceRawAccessChainsFeaturesNV(e, (const VkPhysicalDeviceRawAccessChainsFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMMAND_BUFFER_INHERITANCE_FEATURES_NV: reply_VkPhysicalDeviceCommandBufferInheritanceFeaturesNV(e, (const VkPhysicalDeviceCommandBufferInheritanceFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ALIGNMENT_CONTROL_FEATURES_MESA: reply_VkPhysicalDeviceImageAlignmentControlFeaturesMESA(e, (const VkPhysicalDeviceImageAlignmentControlFeaturesMESA *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ALIGNMENT_CONTROL_PROPERTIES_MESA: reply_VkPhysicalDeviceImageAlignmentControlPropertiesMESA(e, (const VkPhysicalDeviceImageAlignmentControlPropertiesMESA *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_REPLICATED_COMPOSITES_FEATURES_EXT: reply_VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT(e, (const VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR: reply_VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR(e, (const VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_FEATURES_NV: reply_VkPhysicalDeviceCooperativeMatrix2FeaturesNV(e, (const VkPhysicalDeviceCooperativeMatrix2FeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_PROPERTIES_NV: reply_VkPhysicalDeviceCooperativeMatrix2PropertiesNV(e, (const VkPhysicalDeviceCooperativeMatrix2PropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_DECODE_VECTOR_FEATURES_NV: reply_VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV(e, (const VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HDR_VIVID_FEATURES_HUAWEI: reply_VkPhysicalDeviceHdrVividFeaturesHUAWEI(e, (const VkPhysicalDeviceHdrVividFeaturesHUAWEI *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_ROBUSTNESS_FEATURES_EXT: reply_VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT(e, (const VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLAMP_ZERO_ONE_FEATURES_KHR: reply_VkPhysicalDeviceDepthClampZeroOneFeaturesKHR(e, (const VkPhysicalDeviceDepthClampZeroOneFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV: reply_VkPhysicalDeviceCooperativeVectorFeaturesNV(e, (const VkPhysicalDeviceCooperativeVectorFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_PROPERTIES_NV: reply_VkPhysicalDeviceCooperativeVectorPropertiesNV(e, (const VkPhysicalDeviceCooperativeVectorPropertiesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_LAYERED_PROPERTIES_VALVE: reply_VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE(e, (const VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_LAYERED_FEATURES_VALVE: reply_VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE(e, (const VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_METERING_FEATURES_NV: reply_VkPhysicalDevicePresentMeteringFeaturesNV(e, (const VkPhysicalDevicePresentMeteringFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNIFORM_BUFFER_UNSIZED_ARRAY_FEATURES_EXT: reply_VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT(e, (const VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE: reply_VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE(e, (const VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_RESTART_INDEX_FEATURES_EXT: reply_VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT(e, (const VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FORMAT_PACK_FEATURES_ARM: reply_VkPhysicalDeviceFormatPackFeaturesARM(e, (const VkPhysicalDeviceFormatPackFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_THROTTLE_HINT_FEATURES_SEC: reply_VkPhysicalDeviceThrottleHintFeaturesSEC(e, (const VkPhysicalDeviceThrottleHintFeaturesSEC *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT: reply_VkPhysicalDeviceShaderFloat8FeaturesEXT(e, (const VkPhysicalDeviceShaderFloat8FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CACHE_INCREMENTAL_MODE_FEATURES_SEC: reply_VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC(e, (const VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DATA_GRAPH_MODEL_FEATURES_QCOM: reply_VkPhysicalDeviceDataGraphModelFeaturesQCOM(e, (const VkPhysicalDeviceDataGraphModelFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR: reply_VkPhysicalDeviceShaderUntypedPointersFeaturesKHR(e, (const VkPhysicalDeviceShaderUntypedPointersFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_ENCODE_RGB_CONVERSION_FEATURES_VALVE: reply_VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE(e, (const VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_64_BIT_INDEXING_FEATURES_EXT: reply_VkPhysicalDeviceShader64BitIndexingFeaturesEXT(e, (const VkPhysicalDeviceShader64BitIndexingFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_QUEUE_PERF_HINT_FEATURES_QCOM: reply_VkPhysicalDeviceQueuePerfHintFeaturesQCOM(e, (const VkPhysicalDeviceQueuePerfHintFeaturesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_QUEUE_PERF_HINT_PROPERTIES_QCOM: reply_VkPhysicalDeviceQueuePerfHintPropertiesQCOM(e, (const VkPhysicalDeviceQueuePerfHintPropertiesQCOM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_COUNTERS_BY_REGION_FEATURES_ARM: reply_VkPhysicalDevicePerformanceCountersByRegionFeaturesARM(e, (const VkPhysicalDevicePerformanceCountersByRegionFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_COUNTERS_BY_REGION_PROPERTIES_ARM: reply_VkPhysicalDevicePerformanceCountersByRegionPropertiesARM(e, (const VkPhysicalDevicePerformanceCountersByRegionPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_OCCUPANCY_PRIORITY_FEATURES_NV: reply_VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV(e, (const VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_LONG_VECTOR_FEATURES_EXT: reply_VkPhysicalDeviceShaderLongVectorFeaturesEXT(e, (const VkPhysicalDeviceShaderLongVectorFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_LONG_VECTOR_PROPERTIES_EXT: reply_VkPhysicalDeviceShaderLongVectorPropertiesEXT(e, (const VkPhysicalDeviceShaderLongVectorPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_3D_FEATURES_EXT: reply_VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT(e, (const VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_PARTITIONED_FEATURES_EXT: reply_VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT(e, (const VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_SUBSAMPLED_IMAGE_FORMAT_PROPERTIES_EXT: reply_VkSubsampledImageFormatPropertiesEXT(e, (const VkSubsampledImageFormatPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SPLIT_BARRIER_FEATURES_EXT: reply_VkPhysicalDeviceShaderSplitBarrierFeaturesEXT(e, (const VkPhysicalDeviceShaderSplitBarrierFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SPLIT_BARRIER_PROPERTIES_EXT: reply_VkPhysicalDeviceShaderSplitBarrierPropertiesEXT(e, (const VkPhysicalDeviceShaderSplitBarrierPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT: reply_VkPhysicalDeviceDescriptorHeapFeaturesEXT(e, (const VkPhysicalDeviceDescriptorHeapFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT: reply_VkPhysicalDeviceDescriptorHeapPropertiesEXT(e, (const VkPhysicalDeviceDescriptorHeapPropertiesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_TENSOR_PROPERTIES_ARM: reply_VkPhysicalDeviceDescriptorHeapTensorPropertiesARM(e, (const VkPhysicalDeviceDescriptorHeapTensorPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INSTRUMENTATION_FEATURES_ARM: reply_VkPhysicalDeviceShaderInstrumentationFeaturesARM(e, (const VkPhysicalDeviceShaderInstrumentationFeaturesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INSTRUMENTATION_PROPERTIES_ARM: reply_VkPhysicalDeviceShaderInstrumentationPropertiesARM(e, (const VkPhysicalDeviceShaderInstrumentationPropertiesARM *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_ADDRESS_COMMANDS_FEATURES_KHR: reply_VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR(e, (const VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CONSTANT_DATA_FEATURES_KHR: reply_VkPhysicalDeviceShaderConstantDataFeaturesKHR(e, (const VkPhysicalDeviceShaderConstantDataFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ABORT_FEATURES_KHR: reply_VkPhysicalDeviceShaderAbortFeaturesKHR(e, (const VkPhysicalDeviceShaderAbortFeaturesKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ABORT_PROPERTIES_KHR: reply_VkPhysicalDeviceShaderAbortPropertiesKHR(e, (const VkPhysicalDeviceShaderAbortPropertiesKHR *)s); break;
    case VK_STRUCTURE_TYPE_DEVICE_FAULT_SHADER_ABORT_MESSAGE_INFO_KHR: reply_VkDeviceFaultShaderAbortMessageInfoKHR(e, (const VkDeviceFaultShaderAbortMessageInfoKHR *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_TILING_CONTROL_FEATURES_EXT: reply_VkPhysicalDeviceImageTilingControlFeaturesEXT(e, (const VkPhysicalDeviceImageTilingControlFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OCP_MICROSCALING_TYPES_FEATURES_EXT: reply_VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT(e, (const VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_MAINTENANCE_1_FEATURES_EXT: reply_VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT(e, (const VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_ALLOCATION_ALIGNMENT_FEATURES_VALVE: reply_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE(e, (const VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_ALLOCATION_ALIGNMENT_PROPERTIES_VALVE: reply_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE(e, (const VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE *)s); break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INFO_PROPERTIES_INTEL: reply_VkPhysicalDeviceInfoPropertiesINTEL(e, (const VkPhysicalDeviceInfoPropertiesINTEL *)s); break;
    default: break;
    }
}

static void run_vkGetPhysicalDeviceFeatures(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    VkPhysicalDeviceFeatures pFeatures_value;
    memset(&pFeatures_value, 0, sizeof(pFeatures_value));
    VkPhysicalDeviceFeatures *pFeatures = &pFeatures_value;
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceFeatures");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceFeatures) {
        mvk_unsupported(c, "vkGetPhysicalDeviceFeatures");
        return;
    }
    c->fn->vkGetPhysicalDeviceFeatures(physicalDevice, pFeatures);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkPhysicalDeviceFeatures(e, pFeatures);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceFormatProperties(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkFormat format;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    format = (VkFormat)mvk_get_u32(d);
    VkFormatProperties pFormatProperties_value;
    memset(&pFormatProperties_value, 0, sizeof(pFormatProperties_value));
    VkFormatProperties *pFormatProperties = &pFormatProperties_value;
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceFormatProperties");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceFormatProperties) {
        mvk_unsupported(c, "vkGetPhysicalDeviceFormatProperties");
        return;
    }
    c->fn->vkGetPhysicalDeviceFormatProperties(physicalDevice, format, pFormatProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkFormatProperties(e, pFormatProperties);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceImageFormatProperties(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkFormat format;
    VkImageType type;
    VkImageTiling tiling;
    VkImageUsageFlags usage;
    VkImageCreateFlags flags;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    format = (VkFormat)mvk_get_u32(d);
    type = (VkImageType)mvk_get_u32(d);
    tiling = (VkImageTiling)mvk_get_u32(d);
    usage = (VkImageUsageFlags)mvk_get_u32(d);
    flags = (VkImageCreateFlags)mvk_get_u32(d);
    VkImageFormatProperties pImageFormatProperties_value;
    memset(&pImageFormatProperties_value, 0, sizeof(pImageFormatProperties_value));
    VkImageFormatProperties *pImageFormatProperties = &pImageFormatProperties_value;
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceImageFormatProperties");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceImageFormatProperties) {
        mvk_unsupported(c, "vkGetPhysicalDeviceImageFormatProperties");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceImageFormatProperties(physicalDevice, format, type, tiling, usage, flags, pImageFormatProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    reply_VkImageFormatProperties(e, pImageFormatProperties);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceProperties(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    VkPhysicalDeviceProperties pProperties_value;
    memset(&pProperties_value, 0, sizeof(pProperties_value));
    VkPhysicalDeviceProperties *pProperties = &pProperties_value;
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceProperties");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceProperties) {
        mvk_unsupported(c, "vkGetPhysicalDeviceProperties");
        return;
    }
    c->fn->vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkPhysicalDeviceProperties(e, pProperties);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceQueueFamilyProperties(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    uint32_t pQueueFamilyPropertyCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pQueueFamilyPropertyCount = &pQueueFamilyPropertyCount_value;
    int want_pQueueFamilyProperties = mvk_get_u32(d);
    VkQueueFamilyProperties *pQueueFamilyProperties = NULL;
    if (want_pQueueFamilyProperties) {
        pQueueFamilyProperties = mvk_alloc(d, sizeof(VkQueueFamilyProperties) * (size_t)(*pQueueFamilyPropertyCount) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceQueueFamilyProperties");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceQueueFamilyProperties) {
        mvk_unsupported(c, "vkGetPhysicalDeviceQueueFamilyProperties");
        return;
    }
    c->fn->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(*pQueueFamilyPropertyCount));
    mvk_put_u32(e, pQueueFamilyProperties != NULL);
    if (pQueueFamilyProperties) {
        mvk_put_u32(e, (uint32_t)(*pQueueFamilyPropertyCount));
        for (uint32_t i = 0; i < (uint32_t)(*pQueueFamilyPropertyCount); i++)
            reply_VkQueueFamilyProperties(e, &pQueueFamilyProperties[i]);
    }
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceMemoryProperties(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    VkPhysicalDeviceMemoryProperties pMemoryProperties_value;
    memset(&pMemoryProperties_value, 0, sizeof(pMemoryProperties_value));
    VkPhysicalDeviceMemoryProperties *pMemoryProperties = &pMemoryProperties_value;
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceMemoryProperties");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceMemoryProperties) {
        mvk_unsupported(c, "vkGetPhysicalDeviceMemoryProperties");
        return;
    }
    c->fn->vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkPhysicalDeviceMemoryProperties(e, pMemoryProperties);
    mvk_reply_end(c);
}

static void run_vkBindBufferMemory(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize memoryOffset;
    device = (VkDevice)mvk_get_handle(d, 1);
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    memory = (VkDeviceMemory)mvk_get_handle(d, 0);
    memoryOffset = (VkDeviceSize)mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkBindBufferMemory");
        return;
    }
    if (!c->fn->vkBindBufferMemory) {
        mvk_unsupported(c, "vkBindBufferMemory");
        return;
    }
    VkResult result = c->fn->vkBindBufferMemory(device, buffer, memory, memoryOffset);
    mvk_check(c, result, "vkBindBufferMemory");
}

static void run_vkBindImageMemory(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImage image;
    VkDeviceMemory memory;
    VkDeviceSize memoryOffset;
    device = (VkDevice)mvk_get_handle(d, 1);
    image = (VkImage)mvk_get_handle(d, 0);
    memory = (VkDeviceMemory)mvk_get_handle(d, 0);
    memoryOffset = (VkDeviceSize)mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkBindImageMemory");
        return;
    }
    if (!c->fn->vkBindImageMemory) {
        mvk_unsupported(c, "vkBindImageMemory");
        return;
    }
    VkResult result = c->fn->vkBindImageMemory(device, image, memory, memoryOffset);
    mvk_check(c, result, "vkBindImageMemory");
}

static void run_vkGetBufferMemoryRequirements(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkBuffer buffer;
    device = (VkDevice)mvk_get_handle(d, 1);
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    VkMemoryRequirements pMemoryRequirements_value;
    memset(&pMemoryRequirements_value, 0, sizeof(pMemoryRequirements_value));
    VkMemoryRequirements *pMemoryRequirements = &pMemoryRequirements_value;
    if (d->error) {
        mvk_malformed(c, "vkGetBufferMemoryRequirements");
        return;
    }
    if (!c->fn->vkGetBufferMemoryRequirements) {
        mvk_unsupported(c, "vkGetBufferMemoryRequirements");
        return;
    }
    c->fn->vkGetBufferMemoryRequirements(device, buffer, pMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkMemoryRequirements(e, pMemoryRequirements);
    mvk_reply_end(c);
}

static void run_vkGetImageMemoryRequirements(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImage image;
    device = (VkDevice)mvk_get_handle(d, 1);
    image = (VkImage)mvk_get_handle(d, 0);
    VkMemoryRequirements pMemoryRequirements_value;
    memset(&pMemoryRequirements_value, 0, sizeof(pMemoryRequirements_value));
    VkMemoryRequirements *pMemoryRequirements = &pMemoryRequirements_value;
    if (d->error) {
        mvk_malformed(c, "vkGetImageMemoryRequirements");
        return;
    }
    if (!c->fn->vkGetImageMemoryRequirements) {
        mvk_unsupported(c, "vkGetImageMemoryRequirements");
        return;
    }
    c->fn->vkGetImageMemoryRequirements(device, image, pMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkMemoryRequirements(e, pMemoryRequirements);
    mvk_reply_end(c);
}

static void run_vkGetImageSparseMemoryRequirements(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImage image;
    device = (VkDevice)mvk_get_handle(d, 1);
    image = (VkImage)mvk_get_handle(d, 0);
    uint32_t pSparseMemoryRequirementCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pSparseMemoryRequirementCount = &pSparseMemoryRequirementCount_value;
    int want_pSparseMemoryRequirements = mvk_get_u32(d);
    VkSparseImageMemoryRequirements *pSparseMemoryRequirements = NULL;
    if (want_pSparseMemoryRequirements) {
        pSparseMemoryRequirements = mvk_alloc(d, sizeof(VkSparseImageMemoryRequirements) * (size_t)(*pSparseMemoryRequirementCount) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetImageSparseMemoryRequirements");
        return;
    }
    if (!c->fn->vkGetImageSparseMemoryRequirements) {
        mvk_unsupported(c, "vkGetImageSparseMemoryRequirements");
        return;
    }
    c->fn->vkGetImageSparseMemoryRequirements(device, image, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(*pSparseMemoryRequirementCount));
    mvk_put_u32(e, pSparseMemoryRequirements != NULL);
    if (pSparseMemoryRequirements) {
        mvk_put_u32(e, (uint32_t)(*pSparseMemoryRequirementCount));
        for (uint32_t i = 0; i < (uint32_t)(*pSparseMemoryRequirementCount); i++)
            reply_VkSparseImageMemoryRequirements(e, &pSparseMemoryRequirements[i]);
    }
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceSparseImageFormatProperties(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkFormat format;
    VkImageType type;
    VkSampleCountFlagBits samples;
    VkImageUsageFlags usage;
    VkImageTiling tiling;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    format = (VkFormat)mvk_get_u32(d);
    type = (VkImageType)mvk_get_u32(d);
    samples = (VkSampleCountFlagBits)mvk_get_u32(d);
    usage = (VkImageUsageFlags)mvk_get_u32(d);
    tiling = (VkImageTiling)mvk_get_u32(d);
    uint32_t pPropertyCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pPropertyCount = &pPropertyCount_value;
    int want_pProperties = mvk_get_u32(d);
    VkSparseImageFormatProperties *pProperties = NULL;
    if (want_pProperties) {
        pProperties = mvk_alloc(d, sizeof(VkSparseImageFormatProperties) * (size_t)(*pPropertyCount) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceSparseImageFormatProperties");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceSparseImageFormatProperties) {
        mvk_unsupported(c, "vkGetPhysicalDeviceSparseImageFormatProperties");
        return;
    }
    c->fn->vkGetPhysicalDeviceSparseImageFormatProperties(physicalDevice, format, type, samples, usage, tiling, pPropertyCount, pProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(*pPropertyCount));
    mvk_put_u32(e, pProperties != NULL);
    if (pProperties) {
        mvk_put_u32(e, (uint32_t)(*pPropertyCount));
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            reply_VkSparseImageFormatProperties(e, &pProperties[i]);
    }
    mvk_reply_end(c);
}

static void run_vkCreateQueryPool(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkQueryPoolCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkQueryPoolCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkQueryPoolCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkQueryPoolCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pQueryPool = mvk_get_u64(d);
    VkQueryPool pQueryPool_value = VK_NULL_HANDLE;
    VkQueryPool *pQueryPool = &pQueryPool_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateQueryPool");
        return;
    }
    if (!c->fn->vkCreateQueryPool) {
        mvk_unsupported(c, "vkCreateQueryPool");
        return;
    }
    VkResult result = c->fn->vkCreateQueryPool(device, pCreateInfo, NULL, pQueryPool);
    mvk_bind_id(c, id_pQueryPool, (uint64_t)pQueryPool_value);
    mvk_track(c->conn, (uint64_t)pQueryPool_value, MVK_KIND_VkQueryPool);
    mvk_check(c, result, "vkCreateQueryPool");
}

static void run_vkDestroyQueryPool(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkQueryPool queryPool;
    device = (VkDevice)mvk_get_handle(d, 1);
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyQueryPool");
        return;
    }
    if (!c->fn->vkDestroyQueryPool) {
        mvk_unsupported(c, "vkDestroyQueryPool");
        return;
    }
    c->fn->vkDestroyQueryPool(device, queryPool, NULL);
    mvk_forget(c->conn, (uint64_t)queryPool);
}

static void run_vkCreateBuffer(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkBufferCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkBufferCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBufferCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBufferCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pBuffer = mvk_get_u64(d);
    VkBuffer pBuffer_value = VK_NULL_HANDLE;
    VkBuffer *pBuffer = &pBuffer_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateBuffer");
        return;
    }
    if (!c->fn->vkCreateBuffer) {
        mvk_unsupported(c, "vkCreateBuffer");
        return;
    }
    VkResult result = c->fn->vkCreateBuffer(device, pCreateInfo, NULL, pBuffer);
    mvk_bind_id(c, id_pBuffer, (uint64_t)pBuffer_value);
    mvk_track(c->conn, (uint64_t)pBuffer_value, MVK_KIND_VkBuffer);
    mvk_check(c, result, "vkCreateBuffer");
}

static void run_vkDestroyBuffer(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkBuffer buffer;
    device = (VkDevice)mvk_get_handle(d, 1);
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyBuffer");
        return;
    }
    if (!c->fn->vkDestroyBuffer) {
        mvk_unsupported(c, "vkDestroyBuffer");
        return;
    }
    c->fn->vkDestroyBuffer(device, buffer, NULL);
    mvk_forget(c->conn, (uint64_t)buffer);
}

static void run_vkCreateImage(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImageCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkImageCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkImageCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkImageCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pImage = mvk_get_u64(d);
    VkImage pImage_value = VK_NULL_HANDLE;
    VkImage *pImage = &pImage_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateImage");
        return;
    }
    if (!c->fn->vkCreateImage) {
        mvk_unsupported(c, "vkCreateImage");
        return;
    }
    VkResult result = c->fn->vkCreateImage(device, pCreateInfo, NULL, pImage);
    mvk_bind_id(c, id_pImage, (uint64_t)pImage_value);
    mvk_track(c->conn, (uint64_t)pImage_value, MVK_KIND_VkImage);
    mvk_check(c, result, "vkCreateImage");
}

static void run_vkDestroyImage(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImage image;
    device = (VkDevice)mvk_get_handle(d, 1);
    image = (VkImage)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyImage");
        return;
    }
    if (!c->fn->vkDestroyImage) {
        mvk_unsupported(c, "vkDestroyImage");
        return;
    }
    c->fn->vkDestroyImage(device, image, NULL);
    mvk_forget(c->conn, (uint64_t)image);
}

static void run_vkGetImageSubresourceLayout(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImage image;
    VkImageSubresource * pSubresource;
    device = (VkDevice)mvk_get_handle(d, 1);
    image = (VkImage)mvk_get_handle(d, 0);
    if (mvk_get_u32(d)) {
        VkImageSubresource *items = mvk_alloc(d, (size_t)(1) * sizeof(VkImageSubresource));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkImageSubresource(d, &items[i]);
        pSubresource = items;
    } else {
        pSubresource = NULL;
    }
    VkSubresourceLayout pLayout_value;
    memset(&pLayout_value, 0, sizeof(pLayout_value));
    VkSubresourceLayout *pLayout = &pLayout_value;
    if (d->error) {
        mvk_malformed(c, "vkGetImageSubresourceLayout");
        return;
    }
    if (!c->fn->vkGetImageSubresourceLayout) {
        mvk_unsupported(c, "vkGetImageSubresourceLayout");
        return;
    }
    c->fn->vkGetImageSubresourceLayout(device, image, pSubresource, pLayout);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkSubresourceLayout(e, pLayout);
    mvk_reply_end(c);
}

static void run_vkCreateImageView(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImageViewCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkImageViewCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkImageViewCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkImageViewCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pView = mvk_get_u64(d);
    VkImageView pView_value = VK_NULL_HANDLE;
    VkImageView *pView = &pView_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateImageView");
        return;
    }
    if (!c->fn->vkCreateImageView) {
        mvk_unsupported(c, "vkCreateImageView");
        return;
    }
    VkResult result = c->fn->vkCreateImageView(device, pCreateInfo, NULL, pView);
    mvk_bind_id(c, id_pView, (uint64_t)pView_value);
    mvk_track(c->conn, (uint64_t)pView_value, MVK_KIND_VkImageView);
    mvk_check(c, result, "vkCreateImageView");
}

static void run_vkDestroyImageView(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImageView imageView;
    device = (VkDevice)mvk_get_handle(d, 1);
    imageView = (VkImageView)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyImageView");
        return;
    }
    if (!c->fn->vkDestroyImageView) {
        mvk_unsupported(c, "vkDestroyImageView");
        return;
    }
    c->fn->vkDestroyImageView(device, imageView, NULL);
    mvk_forget(c->conn, (uint64_t)imageView);
}

static void run_vkCreateCommandPool(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkCommandPoolCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkCommandPoolCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCommandPoolCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCommandPoolCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pCommandPool = mvk_get_u64(d);
    VkCommandPool pCommandPool_value = VK_NULL_HANDLE;
    VkCommandPool *pCommandPool = &pCommandPool_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateCommandPool");
        return;
    }
    if (!c->fn->vkCreateCommandPool) {
        mvk_unsupported(c, "vkCreateCommandPool");
        return;
    }
    VkResult result = c->fn->vkCreateCommandPool(device, pCreateInfo, NULL, pCommandPool);
    mvk_bind_id(c, id_pCommandPool, (uint64_t)pCommandPool_value);
    mvk_track(c->conn, (uint64_t)pCommandPool_value, MVK_KIND_VkCommandPool);
    mvk_check(c, result, "vkCreateCommandPool");
}

static void run_vkCmdCopyBuffer(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer srcBuffer;
    VkBuffer dstBuffer;
    uint32_t regionCount;
    VkBufferCopy * pRegions;
    srcBuffer = (VkBuffer)mvk_get_handle(d, 0);
    dstBuffer = (VkBuffer)mvk_get_handle(d, 0);
    regionCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBufferCopy *items = mvk_alloc(d, (size_t)(regionCount) * sizeof(VkBufferCopy));
        for (uint32_t i = 0; i < (uint32_t)(regionCount); i++)
            mvk_get_VkBufferCopy(d, &items[i]);
        pRegions = items;
    } else {
        pRegions = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyBuffer");
        return;
    }
    if (!c->fn->vkCmdCopyBuffer) {
        mvk_unsupported(c, "vkCmdCopyBuffer");
        return;
    }
    c->fn->vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}

static void run_vkCmdCopyImage(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkImage srcImage;
    VkImageLayout srcImageLayout;
    VkImage dstImage;
    VkImageLayout dstImageLayout;
    uint32_t regionCount;
    VkImageCopy * pRegions;
    srcImage = (VkImage)mvk_get_handle(d, 0);
    srcImageLayout = (VkImageLayout)mvk_get_u32(d);
    dstImage = (VkImage)mvk_get_handle(d, 0);
    dstImageLayout = (VkImageLayout)mvk_get_u32(d);
    regionCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkImageCopy *items = mvk_alloc(d, (size_t)(regionCount) * sizeof(VkImageCopy));
        for (uint32_t i = 0; i < (uint32_t)(regionCount); i++)
            mvk_get_VkImageCopy(d, &items[i]);
        pRegions = items;
    } else {
        pRegions = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyImage");
        return;
    }
    if (!c->fn->vkCmdCopyImage) {
        mvk_unsupported(c, "vkCmdCopyImage");
        return;
    }
    c->fn->vkCmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}

static void run_vkCmdCopyBufferToImage(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer srcBuffer;
    VkImage dstImage;
    VkImageLayout dstImageLayout;
    uint32_t regionCount;
    VkBufferImageCopy * pRegions;
    srcBuffer = (VkBuffer)mvk_get_handle(d, 0);
    dstImage = (VkImage)mvk_get_handle(d, 0);
    dstImageLayout = (VkImageLayout)mvk_get_u32(d);
    regionCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBufferImageCopy *items = mvk_alloc(d, (size_t)(regionCount) * sizeof(VkBufferImageCopy));
        for (uint32_t i = 0; i < (uint32_t)(regionCount); i++)
            mvk_get_VkBufferImageCopy(d, &items[i]);
        pRegions = items;
    } else {
        pRegions = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyBufferToImage");
        return;
    }
    if (!c->fn->vkCmdCopyBufferToImage) {
        mvk_unsupported(c, "vkCmdCopyBufferToImage");
        return;
    }
    c->fn->vkCmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}

static void run_vkCmdCopyImageToBuffer(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkImage srcImage;
    VkImageLayout srcImageLayout;
    VkBuffer dstBuffer;
    uint32_t regionCount;
    VkBufferImageCopy * pRegions;
    srcImage = (VkImage)mvk_get_handle(d, 0);
    srcImageLayout = (VkImageLayout)mvk_get_u32(d);
    dstBuffer = (VkBuffer)mvk_get_handle(d, 0);
    regionCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBufferImageCopy *items = mvk_alloc(d, (size_t)(regionCount) * sizeof(VkBufferImageCopy));
        for (uint32_t i = 0; i < (uint32_t)(regionCount); i++)
            mvk_get_VkBufferImageCopy(d, &items[i]);
        pRegions = items;
    } else {
        pRegions = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyImageToBuffer");
        return;
    }
    if (!c->fn->vkCmdCopyImageToBuffer) {
        mvk_unsupported(c, "vkCmdCopyImageToBuffer");
        return;
    }
    c->fn->vkCmdCopyImageToBuffer(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
}

static void run_vkCmdUpdateBuffer(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer dstBuffer;
    VkDeviceSize dstOffset;
    VkDeviceSize dataSize;
    void * pData;
    dstBuffer = (VkBuffer)mvk_get_handle(d, 0);
    dstOffset = (VkDeviceSize)mvk_get_u64(d);
    dataSize = (VkDeviceSize)mvk_get_u64(d);
    pData = (void *)mvk_get_blob(d, NULL);
    if (d->error) {
        mvk_malformed(c, "vkCmdUpdateBuffer");
        return;
    }
    if (!c->fn->vkCmdUpdateBuffer) {
        mvk_unsupported(c, "vkCmdUpdateBuffer");
        return;
    }
    c->fn->vkCmdUpdateBuffer(commandBuffer, dstBuffer, dstOffset, dataSize, pData);
}

static void run_vkCmdFillBuffer(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer dstBuffer;
    VkDeviceSize dstOffset;
    VkDeviceSize size;
    uint32_t data;
    dstBuffer = (VkBuffer)mvk_get_handle(d, 0);
    dstOffset = (VkDeviceSize)mvk_get_u64(d);
    size = (VkDeviceSize)mvk_get_u64(d);
    data = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdFillBuffer");
        return;
    }
    if (!c->fn->vkCmdFillBuffer) {
        mvk_unsupported(c, "vkCmdFillBuffer");
        return;
    }
    c->fn->vkCmdFillBuffer(commandBuffer, dstBuffer, dstOffset, size, data);
}

static void run_vkCmdPipelineBarrier(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineStageFlags srcStageMask;
    VkPipelineStageFlags dstStageMask;
    VkDependencyFlags dependencyFlags;
    uint32_t memoryBarrierCount;
    VkMemoryBarrier * pMemoryBarriers;
    uint32_t bufferMemoryBarrierCount;
    VkBufferMemoryBarrier * pBufferMemoryBarriers;
    uint32_t imageMemoryBarrierCount;
    VkImageMemoryBarrier * pImageMemoryBarriers;
    srcStageMask = (VkPipelineStageFlags)mvk_get_u32(d);
    dstStageMask = (VkPipelineStageFlags)mvk_get_u32(d);
    dependencyFlags = (VkDependencyFlags)mvk_get_u32(d);
    memoryBarrierCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkMemoryBarrier *items = mvk_alloc(d, (size_t)(memoryBarrierCount) * sizeof(VkMemoryBarrier));
        for (uint32_t i = 0; i < (uint32_t)(memoryBarrierCount); i++)
            mvk_get_VkMemoryBarrier(d, &items[i]);
        pMemoryBarriers = items;
    } else {
        pMemoryBarriers = NULL;
    }
    bufferMemoryBarrierCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBufferMemoryBarrier *items = mvk_alloc(d, (size_t)(bufferMemoryBarrierCount) * sizeof(VkBufferMemoryBarrier));
        for (uint32_t i = 0; i < (uint32_t)(bufferMemoryBarrierCount); i++)
            mvk_get_VkBufferMemoryBarrier(d, &items[i]);
        pBufferMemoryBarriers = items;
    } else {
        pBufferMemoryBarriers = NULL;
    }
    imageMemoryBarrierCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkImageMemoryBarrier *items = mvk_alloc(d, (size_t)(imageMemoryBarrierCount) * sizeof(VkImageMemoryBarrier));
        for (uint32_t i = 0; i < (uint32_t)(imageMemoryBarrierCount); i++)
            mvk_get_VkImageMemoryBarrier(d, &items[i]);
        pImageMemoryBarriers = items;
    } else {
        pImageMemoryBarriers = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdPipelineBarrier");
        return;
    }
    if (!c->fn->vkCmdPipelineBarrier) {
        mvk_unsupported(c, "vkCmdPipelineBarrier");
        return;
    }
    c->fn->vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}

static void run_vkCmdBeginQuery(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkQueryPool queryPool;
    uint32_t query;
    VkQueryControlFlags flags;
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    query = (uint32_t)mvk_get_u32(d);
    flags = (VkQueryControlFlags)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdBeginQuery");
        return;
    }
    if (!c->fn->vkCmdBeginQuery) {
        mvk_unsupported(c, "vkCmdBeginQuery");
        return;
    }
    c->fn->vkCmdBeginQuery(commandBuffer, queryPool, query, flags);
}

static void run_vkCmdEndQuery(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkQueryPool queryPool;
    uint32_t query;
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    query = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdEndQuery");
        return;
    }
    if (!c->fn->vkCmdEndQuery) {
        mvk_unsupported(c, "vkCmdEndQuery");
        return;
    }
    c->fn->vkCmdEndQuery(commandBuffer, queryPool, query);
}

static void run_vkCmdResetQueryPool(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkQueryPool queryPool;
    uint32_t firstQuery;
    uint32_t queryCount;
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    firstQuery = (uint32_t)mvk_get_u32(d);
    queryCount = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdResetQueryPool");
        return;
    }
    if (!c->fn->vkCmdResetQueryPool) {
        mvk_unsupported(c, "vkCmdResetQueryPool");
        return;
    }
    c->fn->vkCmdResetQueryPool(commandBuffer, queryPool, firstQuery, queryCount);
}

static void run_vkCmdWriteTimestamp(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineStageFlagBits pipelineStage;
    VkQueryPool queryPool;
    uint32_t query;
    pipelineStage = (VkPipelineStageFlagBits)mvk_get_u32(d);
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    query = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdWriteTimestamp");
        return;
    }
    if (!c->fn->vkCmdWriteTimestamp) {
        mvk_unsupported(c, "vkCmdWriteTimestamp");
        return;
    }
    c->fn->vkCmdWriteTimestamp(commandBuffer, pipelineStage, queryPool, query);
}

static void run_vkCmdCopyQueryPoolResults(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkQueryPool queryPool;
    uint32_t firstQuery;
    uint32_t queryCount;
    VkBuffer dstBuffer;
    VkDeviceSize dstOffset;
    VkDeviceSize stride;
    VkQueryResultFlags flags;
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    firstQuery = (uint32_t)mvk_get_u32(d);
    queryCount = (uint32_t)mvk_get_u32(d);
    dstBuffer = (VkBuffer)mvk_get_handle(d, 0);
    dstOffset = (VkDeviceSize)mvk_get_u64(d);
    stride = (VkDeviceSize)mvk_get_u64(d);
    flags = (VkQueryResultFlags)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyQueryPoolResults");
        return;
    }
    if (!c->fn->vkCmdCopyQueryPoolResults) {
        mvk_unsupported(c, "vkCmdCopyQueryPoolResults");
        return;
    }
    c->fn->vkCmdCopyQueryPoolResults(commandBuffer, queryPool, firstQuery, queryCount, dstBuffer, dstOffset, stride, flags);
}

static void run_vkCmdExecuteCommands(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t commandBufferCount;
    VkCommandBuffer * pCommandBuffers;
    commandBufferCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkCommandBuffer *items = mvk_alloc(d, (size_t)(commandBufferCount) * sizeof(VkCommandBuffer));
        for (uint32_t i = 0; i < (uint32_t)(commandBufferCount); i++)
            items[i] = (VkCommandBuffer)mvk_get_handle(d, 1);
        pCommandBuffers = items;
    } else {
        pCommandBuffers = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdExecuteCommands");
        return;
    }
    if (!c->fn->vkCmdExecuteCommands) {
        mvk_unsupported(c, "vkCmdExecuteCommands");
        return;
    }
    c->fn->vkCmdExecuteCommands(commandBuffer, commandBufferCount, pCommandBuffers);
}

static void run_vkCreateBufferView(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkBufferViewCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkBufferViewCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBufferViewCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBufferViewCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pView = mvk_get_u64(d);
    VkBufferView pView_value = VK_NULL_HANDLE;
    VkBufferView *pView = &pView_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateBufferView");
        return;
    }
    if (!c->fn->vkCreateBufferView) {
        mvk_unsupported(c, "vkCreateBufferView");
        return;
    }
    VkResult result = c->fn->vkCreateBufferView(device, pCreateInfo, NULL, pView);
    mvk_bind_id(c, id_pView, (uint64_t)pView_value);
    mvk_track(c->conn, (uint64_t)pView_value, MVK_KIND_VkBufferView);
    mvk_check(c, result, "vkCreateBufferView");
}

static void run_vkDestroyBufferView(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkBufferView bufferView;
    device = (VkDevice)mvk_get_handle(d, 1);
    bufferView = (VkBufferView)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyBufferView");
        return;
    }
    if (!c->fn->vkDestroyBufferView) {
        mvk_unsupported(c, "vkDestroyBufferView");
        return;
    }
    c->fn->vkDestroyBufferView(device, bufferView, NULL);
    mvk_forget(c->conn, (uint64_t)bufferView);
}

static void run_vkCreateShaderModule(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkShaderModuleCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkShaderModuleCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkShaderModuleCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkShaderModuleCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pShaderModule = mvk_get_u64(d);
    VkShaderModule pShaderModule_value = VK_NULL_HANDLE;
    VkShaderModule *pShaderModule = &pShaderModule_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateShaderModule");
        return;
    }
    if (!c->fn->vkCreateShaderModule) {
        mvk_unsupported(c, "vkCreateShaderModule");
        return;
    }
    VkResult result = c->fn->vkCreateShaderModule(device, pCreateInfo, NULL, pShaderModule);
    mvk_bind_id(c, id_pShaderModule, (uint64_t)pShaderModule_value);
    mvk_track(c->conn, (uint64_t)pShaderModule_value, MVK_KIND_VkShaderModule);
    mvk_check(c, result, "vkCreateShaderModule");
}

static void run_vkDestroyShaderModule(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkShaderModule shaderModule;
    device = (VkDevice)mvk_get_handle(d, 1);
    shaderModule = (VkShaderModule)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyShaderModule");
        return;
    }
    if (!c->fn->vkDestroyShaderModule) {
        mvk_unsupported(c, "vkDestroyShaderModule");
        return;
    }
    c->fn->vkDestroyShaderModule(device, shaderModule, NULL);
    mvk_forget(c->conn, (uint64_t)shaderModule);
}

static void run_vkCreatePipelineCache(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineCacheCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPipelineCacheCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPipelineCacheCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPipelineCacheCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pPipelineCache = mvk_get_u64(d);
    VkPipelineCache pPipelineCache_value = VK_NULL_HANDLE;
    VkPipelineCache *pPipelineCache = &pPipelineCache_value;
    if (d->error) {
        mvk_malformed(c, "vkCreatePipelineCache");
        return;
    }
    if (!c->fn->vkCreatePipelineCache) {
        mvk_unsupported(c, "vkCreatePipelineCache");
        return;
    }
    VkResult result = c->fn->vkCreatePipelineCache(device, pCreateInfo, NULL, pPipelineCache);
    mvk_bind_id(c, id_pPipelineCache, (uint64_t)pPipelineCache_value);
    mvk_track(c->conn, (uint64_t)pPipelineCache_value, MVK_KIND_VkPipelineCache);
    mvk_check(c, result, "vkCreatePipelineCache");
}

static void run_vkDestroyPipelineCache(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineCache pipelineCache;
    device = (VkDevice)mvk_get_handle(d, 1);
    pipelineCache = (VkPipelineCache)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyPipelineCache");
        return;
    }
    if (!c->fn->vkDestroyPipelineCache) {
        mvk_unsupported(c, "vkDestroyPipelineCache");
        return;
    }
    c->fn->vkDestroyPipelineCache(device, pipelineCache, NULL);
    mvk_forget(c->conn, (uint64_t)pipelineCache);
}

static void run_vkGetPipelineCacheData(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineCache pipelineCache;
    device = (VkDevice)mvk_get_handle(d, 1);
    pipelineCache = (VkPipelineCache)mvk_get_handle(d, 0);
    size_t pDataSize_value;
    memset(&pDataSize_value, 0, sizeof(pDataSize_value));
    size_t *pDataSize = &pDataSize_value;
    int want_pData = mvk_get_u32(d);
    uint8_t *pData = NULL;
    if (want_pData) {
        pData = mvk_alloc(d, sizeof(uint8_t) * (size_t)(*pDataSize) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPipelineCacheData");
        return;
    }
    if (!c->fn->vkGetPipelineCacheData) {
        mvk_unsupported(c, "vkGetPipelineCacheData");
        return;
    }
    VkResult result = c->fn->vkGetPipelineCacheData(device, pipelineCache, pDataSize, pData);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u64(e, (uint64_t)(*pDataSize));
    mvk_put_u32(e, pData != NULL);
    if (pData) {
        mvk_put_u32(e, (uint32_t)(*pDataSize));
        mvk_put_bytes(e, pData, (size_t)(*pDataSize));
    }
    mvk_reply_end(c);
}

static void run_vkMergePipelineCaches(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineCache dstCache;
    uint32_t srcCacheCount;
    VkPipelineCache * pSrcCaches;
    device = (VkDevice)mvk_get_handle(d, 1);
    dstCache = (VkPipelineCache)mvk_get_handle(d, 0);
    srcCacheCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkPipelineCache *items = mvk_alloc(d, (size_t)(srcCacheCount) * sizeof(VkPipelineCache));
        for (uint32_t i = 0; i < (uint32_t)(srcCacheCount); i++)
            items[i] = (VkPipelineCache)mvk_get_handle(d, 0);
        pSrcCaches = items;
    } else {
        pSrcCaches = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkMergePipelineCaches");
        return;
    }
    if (!c->fn->vkMergePipelineCaches) {
        mvk_unsupported(c, "vkMergePipelineCaches");
        return;
    }
    VkResult result = c->fn->vkMergePipelineCaches(device, dstCache, srcCacheCount, pSrcCaches);
    mvk_check(c, result, "vkMergePipelineCaches");
}

static void run_vkCreateComputePipelines(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineCache pipelineCache;
    uint32_t createInfoCount;
    VkComputePipelineCreateInfo * pCreateInfos;
    device = (VkDevice)mvk_get_handle(d, 1);
    pipelineCache = (VkPipelineCache)mvk_get_handle(d, 0);
    createInfoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkComputePipelineCreateInfo *items = mvk_alloc(d, (size_t)(createInfoCount) * sizeof(VkComputePipelineCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
            mvk_get_VkComputePipelineCreateInfo(d, &items[i]);
        pCreateInfos = items;
    } else {
        pCreateInfos = NULL;
    }
    uint64_t *ids_pPipelines = mvk_alloc(d, sizeof(uint64_t) * (size_t)(createInfoCount));
    VkPipeline *pPipelines = mvk_alloc(d, sizeof(VkPipeline) * (size_t)(createInfoCount));
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
        ids_pPipelines[i] = mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkCreateComputePipelines");
        return;
    }
    if (!c->fn->vkCreateComputePipelines) {
        mvk_unsupported(c, "vkCreateComputePipelines");
        return;
    }
    VkResult result = c->fn->vkCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, NULL, pPipelines);
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++) {
        mvk_bind_id(c, ids_pPipelines[i], (uint64_t)pPipelines[i]);
        mvk_track(c->conn, (uint64_t)pPipelines[i], MVK_KIND_VkPipeline);
    }
    mvk_check(c, result, "vkCreateComputePipelines");
}

static void run_vkDestroyPipeline(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipeline pipeline;
    device = (VkDevice)mvk_get_handle(d, 1);
    pipeline = (VkPipeline)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyPipeline");
        return;
    }
    if (!c->fn->vkDestroyPipeline) {
        mvk_unsupported(c, "vkDestroyPipeline");
        return;
    }
    c->fn->vkDestroyPipeline(device, pipeline, NULL);
    mvk_forget(c->conn, (uint64_t)pipeline);
}

static void run_vkCreatePipelineLayout(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineLayoutCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPipelineLayoutCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPipelineLayoutCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPipelineLayoutCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pPipelineLayout = mvk_get_u64(d);
    VkPipelineLayout pPipelineLayout_value = VK_NULL_HANDLE;
    VkPipelineLayout *pPipelineLayout = &pPipelineLayout_value;
    if (d->error) {
        mvk_malformed(c, "vkCreatePipelineLayout");
        return;
    }
    if (!c->fn->vkCreatePipelineLayout) {
        mvk_unsupported(c, "vkCreatePipelineLayout");
        return;
    }
    VkResult result = c->fn->vkCreatePipelineLayout(device, pCreateInfo, NULL, pPipelineLayout);
    mvk_bind_id(c, id_pPipelineLayout, (uint64_t)pPipelineLayout_value);
    mvk_track(c->conn, (uint64_t)pPipelineLayout_value, MVK_KIND_VkPipelineLayout);
    mvk_check(c, result, "vkCreatePipelineLayout");
}

static void run_vkDestroyPipelineLayout(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineLayout pipelineLayout;
    device = (VkDevice)mvk_get_handle(d, 1);
    pipelineLayout = (VkPipelineLayout)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyPipelineLayout");
        return;
    }
    if (!c->fn->vkDestroyPipelineLayout) {
        mvk_unsupported(c, "vkDestroyPipelineLayout");
        return;
    }
    c->fn->vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    mvk_forget(c->conn, (uint64_t)pipelineLayout);
}

static void run_vkCreateSampler(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkSamplerCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkSamplerCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkSamplerCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkSamplerCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pSampler = mvk_get_u64(d);
    VkSampler pSampler_value = VK_NULL_HANDLE;
    VkSampler *pSampler = &pSampler_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateSampler");
        return;
    }
    if (!c->fn->vkCreateSampler) {
        mvk_unsupported(c, "vkCreateSampler");
        return;
    }
    VkResult result = c->fn->vkCreateSampler(device, pCreateInfo, NULL, pSampler);
    mvk_bind_id(c, id_pSampler, (uint64_t)pSampler_value);
    mvk_track(c->conn, (uint64_t)pSampler_value, MVK_KIND_VkSampler);
    mvk_check(c, result, "vkCreateSampler");
}

static void run_vkDestroySampler(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkSampler sampler;
    device = (VkDevice)mvk_get_handle(d, 1);
    sampler = (VkSampler)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroySampler");
        return;
    }
    if (!c->fn->vkDestroySampler) {
        mvk_unsupported(c, "vkDestroySampler");
        return;
    }
    c->fn->vkDestroySampler(device, sampler, NULL);
    mvk_forget(c->conn, (uint64_t)sampler);
}

static void run_vkCreateDescriptorSetLayout(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDescriptorSetLayoutCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkDescriptorSetLayoutCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDescriptorSetLayoutCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDescriptorSetLayoutCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pSetLayout = mvk_get_u64(d);
    VkDescriptorSetLayout pSetLayout_value = VK_NULL_HANDLE;
    VkDescriptorSetLayout *pSetLayout = &pSetLayout_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateDescriptorSetLayout");
        return;
    }
    if (!c->fn->vkCreateDescriptorSetLayout) {
        mvk_unsupported(c, "vkCreateDescriptorSetLayout");
        return;
    }
    VkResult result = c->fn->vkCreateDescriptorSetLayout(device, pCreateInfo, NULL, pSetLayout);
    mvk_bind_id(c, id_pSetLayout, (uint64_t)pSetLayout_value);
    mvk_track(c->conn, (uint64_t)pSetLayout_value, MVK_KIND_VkDescriptorSetLayout);
    mvk_check(c, result, "vkCreateDescriptorSetLayout");
}

static void run_vkDestroyDescriptorSetLayout(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDescriptorSetLayout descriptorSetLayout;
    device = (VkDevice)mvk_get_handle(d, 1);
    descriptorSetLayout = (VkDescriptorSetLayout)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyDescriptorSetLayout");
        return;
    }
    if (!c->fn->vkDestroyDescriptorSetLayout) {
        mvk_unsupported(c, "vkDestroyDescriptorSetLayout");
        return;
    }
    c->fn->vkDestroyDescriptorSetLayout(device, descriptorSetLayout, NULL);
    mvk_forget(c->conn, (uint64_t)descriptorSetLayout);
}

static void run_vkUpdateDescriptorSets(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t descriptorWriteCount;
    VkWriteDescriptorSet * pDescriptorWrites;
    uint32_t descriptorCopyCount;
    VkCopyDescriptorSet * pDescriptorCopies;
    device = (VkDevice)mvk_get_handle(d, 1);
    descriptorWriteCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkWriteDescriptorSet *items = mvk_alloc(d, (size_t)(descriptorWriteCount) * sizeof(VkWriteDescriptorSet));
        for (uint32_t i = 0; i < (uint32_t)(descriptorWriteCount); i++)
            mvk_get_VkWriteDescriptorSet(d, &items[i]);
        pDescriptorWrites = items;
    } else {
        pDescriptorWrites = NULL;
    }
    descriptorCopyCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkCopyDescriptorSet *items = mvk_alloc(d, (size_t)(descriptorCopyCount) * sizeof(VkCopyDescriptorSet));
        for (uint32_t i = 0; i < (uint32_t)(descriptorCopyCount); i++)
            mvk_get_VkCopyDescriptorSet(d, &items[i]);
        pDescriptorCopies = items;
    } else {
        pDescriptorCopies = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkUpdateDescriptorSets");
        return;
    }
    if (!c->fn->vkUpdateDescriptorSets) {
        mvk_unsupported(c, "vkUpdateDescriptorSets");
        return;
    }
    c->fn->vkUpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
}

static void run_vkCmdBindPipeline(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineBindPoint pipelineBindPoint;
    VkPipeline pipeline;
    pipelineBindPoint = (VkPipelineBindPoint)mvk_get_u32(d);
    pipeline = (VkPipeline)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkCmdBindPipeline");
        return;
    }
    if (!c->fn->vkCmdBindPipeline) {
        mvk_unsupported(c, "vkCmdBindPipeline");
        return;
    }
    c->fn->vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
}

static void run_vkCmdBindDescriptorSets(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineBindPoint pipelineBindPoint;
    VkPipelineLayout layout;
    uint32_t firstSet;
    uint32_t descriptorSetCount;
    VkDescriptorSet * pDescriptorSets;
    uint32_t dynamicOffsetCount;
    uint32_t * pDynamicOffsets;
    pipelineBindPoint = (VkPipelineBindPoint)mvk_get_u32(d);
    layout = (VkPipelineLayout)mvk_get_handle(d, 0);
    firstSet = (uint32_t)mvk_get_u32(d);
    descriptorSetCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkDescriptorSet *items = mvk_alloc(d, (size_t)(descriptorSetCount) * sizeof(VkDescriptorSet));
        for (uint32_t i = 0; i < (uint32_t)(descriptorSetCount); i++)
            items[i] = (VkDescriptorSet)mvk_get_handle(d, 0);
        pDescriptorSets = items;
    } else {
        pDescriptorSets = NULL;
    }
    dynamicOffsetCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(dynamicOffsetCount) * sizeof(uint32_t));
        mvk_get_bytes(d, items, (size_t)(dynamicOffsetCount) * sizeof(uint32_t));
        pDynamicOffsets = items;
    } else {
        pDynamicOffsets = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindDescriptorSets");
        return;
    }
    if (!c->fn->vkCmdBindDescriptorSets) {
        mvk_unsupported(c, "vkCmdBindDescriptorSets");
        return;
    }
    c->fn->vkCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}

static void run_vkCmdClearColorImage(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkImage image;
    VkImageLayout imageLayout;
    VkClearColorValue * pColor;
    uint32_t rangeCount;
    VkImageSubresourceRange * pRanges;
    image = (VkImage)mvk_get_handle(d, 0);
    imageLayout = (VkImageLayout)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(1) * sizeof(VkClearColorValue));
        mvk_get_bytes(d, items, (size_t)(1) * sizeof(VkClearColorValue));
        pColor = items;
    } else {
        pColor = NULL;
    }
    rangeCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkImageSubresourceRange *items = mvk_alloc(d, (size_t)(rangeCount) * sizeof(VkImageSubresourceRange));
        for (uint32_t i = 0; i < (uint32_t)(rangeCount); i++)
            mvk_get_VkImageSubresourceRange(d, &items[i]);
        pRanges = items;
    } else {
        pRanges = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdClearColorImage");
        return;
    }
    if (!c->fn->vkCmdClearColorImage) {
        mvk_unsupported(c, "vkCmdClearColorImage");
        return;
    }
    c->fn->vkCmdClearColorImage(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
}

static void run_vkCmdDispatch(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t groupCountX;
    uint32_t groupCountY;
    uint32_t groupCountZ;
    groupCountX = (uint32_t)mvk_get_u32(d);
    groupCountY = (uint32_t)mvk_get_u32(d);
    groupCountZ = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDispatch");
        return;
    }
    if (!c->fn->vkCmdDispatch) {
        mvk_unsupported(c, "vkCmdDispatch");
        return;
    }
    c->fn->vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

static void run_vkCmdDispatchIndirect(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer buffer;
    VkDeviceSize offset;
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    offset = (VkDeviceSize)mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDispatchIndirect");
        return;
    }
    if (!c->fn->vkCmdDispatchIndirect) {
        mvk_unsupported(c, "vkCmdDispatchIndirect");
        return;
    }
    c->fn->vkCmdDispatchIndirect(commandBuffer, buffer, offset);
}

static void run_vkCmdSetEvent(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkEvent event;
    VkPipelineStageFlags stageMask;
    event = (VkEvent)mvk_get_handle(d, 0);
    stageMask = (VkPipelineStageFlags)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetEvent");
        return;
    }
    if (!c->fn->vkCmdSetEvent) {
        mvk_unsupported(c, "vkCmdSetEvent");
        return;
    }
    c->fn->vkCmdSetEvent(commandBuffer, event, stageMask);
}

static void run_vkCmdResetEvent(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkEvent event;
    VkPipelineStageFlags stageMask;
    event = (VkEvent)mvk_get_handle(d, 0);
    stageMask = (VkPipelineStageFlags)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdResetEvent");
        return;
    }
    if (!c->fn->vkCmdResetEvent) {
        mvk_unsupported(c, "vkCmdResetEvent");
        return;
    }
    c->fn->vkCmdResetEvent(commandBuffer, event, stageMask);
}

static void run_vkCmdWaitEvents(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t eventCount;
    VkEvent * pEvents;
    VkPipelineStageFlags srcStageMask;
    VkPipelineStageFlags dstStageMask;
    uint32_t memoryBarrierCount;
    VkMemoryBarrier * pMemoryBarriers;
    uint32_t bufferMemoryBarrierCount;
    VkBufferMemoryBarrier * pBufferMemoryBarriers;
    uint32_t imageMemoryBarrierCount;
    VkImageMemoryBarrier * pImageMemoryBarriers;
    eventCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkEvent *items = mvk_alloc(d, (size_t)(eventCount) * sizeof(VkEvent));
        for (uint32_t i = 0; i < (uint32_t)(eventCount); i++)
            items[i] = (VkEvent)mvk_get_handle(d, 0);
        pEvents = items;
    } else {
        pEvents = NULL;
    }
    srcStageMask = (VkPipelineStageFlags)mvk_get_u32(d);
    dstStageMask = (VkPipelineStageFlags)mvk_get_u32(d);
    memoryBarrierCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkMemoryBarrier *items = mvk_alloc(d, (size_t)(memoryBarrierCount) * sizeof(VkMemoryBarrier));
        for (uint32_t i = 0; i < (uint32_t)(memoryBarrierCount); i++)
            mvk_get_VkMemoryBarrier(d, &items[i]);
        pMemoryBarriers = items;
    } else {
        pMemoryBarriers = NULL;
    }
    bufferMemoryBarrierCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBufferMemoryBarrier *items = mvk_alloc(d, (size_t)(bufferMemoryBarrierCount) * sizeof(VkBufferMemoryBarrier));
        for (uint32_t i = 0; i < (uint32_t)(bufferMemoryBarrierCount); i++)
            mvk_get_VkBufferMemoryBarrier(d, &items[i]);
        pBufferMemoryBarriers = items;
    } else {
        pBufferMemoryBarriers = NULL;
    }
    imageMemoryBarrierCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkImageMemoryBarrier *items = mvk_alloc(d, (size_t)(imageMemoryBarrierCount) * sizeof(VkImageMemoryBarrier));
        for (uint32_t i = 0; i < (uint32_t)(imageMemoryBarrierCount); i++)
            mvk_get_VkImageMemoryBarrier(d, &items[i]);
        pImageMemoryBarriers = items;
    } else {
        pImageMemoryBarriers = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdWaitEvents");
        return;
    }
    if (!c->fn->vkCmdWaitEvents) {
        mvk_unsupported(c, "vkCmdWaitEvents");
        return;
    }
    c->fn->vkCmdWaitEvents(commandBuffer, eventCount, pEvents, srcStageMask, dstStageMask, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}

static void run_vkCmdPushConstants(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineLayout layout;
    VkShaderStageFlags stageFlags;
    uint32_t offset;
    uint32_t size;
    void * pValues;
    layout = (VkPipelineLayout)mvk_get_handle(d, 0);
    stageFlags = (VkShaderStageFlags)mvk_get_u32(d);
    offset = (uint32_t)mvk_get_u32(d);
    size = (uint32_t)mvk_get_u32(d);
    pValues = (void *)mvk_get_blob(d, NULL);
    if (d->error) {
        mvk_malformed(c, "vkCmdPushConstants");
        return;
    }
    if (!c->fn->vkCmdPushConstants) {
        mvk_unsupported(c, "vkCmdPushConstants");
        return;
    }
    c->fn->vkCmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
}

static void run_vkCreateGraphicsPipelines(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineCache pipelineCache;
    uint32_t createInfoCount;
    VkGraphicsPipelineCreateInfo * pCreateInfos;
    device = (VkDevice)mvk_get_handle(d, 1);
    pipelineCache = (VkPipelineCache)mvk_get_handle(d, 0);
    createInfoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkGraphicsPipelineCreateInfo *items = mvk_alloc(d, (size_t)(createInfoCount) * sizeof(VkGraphicsPipelineCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
            mvk_get_VkGraphicsPipelineCreateInfo(d, &items[i]);
        pCreateInfos = items;
    } else {
        pCreateInfos = NULL;
    }
    uint64_t *ids_pPipelines = mvk_alloc(d, sizeof(uint64_t) * (size_t)(createInfoCount));
    VkPipeline *pPipelines = mvk_alloc(d, sizeof(VkPipeline) * (size_t)(createInfoCount));
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
        ids_pPipelines[i] = mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkCreateGraphicsPipelines");
        return;
    }
    if (!c->fn->vkCreateGraphicsPipelines) {
        mvk_unsupported(c, "vkCreateGraphicsPipelines");
        return;
    }
    VkResult result = c->fn->vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, NULL, pPipelines);
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++) {
        mvk_bind_id(c, ids_pPipelines[i], (uint64_t)pPipelines[i]);
        mvk_track(c->conn, (uint64_t)pPipelines[i], MVK_KIND_VkPipeline);
    }
    mvk_check(c, result, "vkCreateGraphicsPipelines");
}

static void run_vkCreateFramebuffer(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkFramebufferCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkFramebufferCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkFramebufferCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkFramebufferCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pFramebuffer = mvk_get_u64(d);
    VkFramebuffer pFramebuffer_value = VK_NULL_HANDLE;
    VkFramebuffer *pFramebuffer = &pFramebuffer_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateFramebuffer");
        return;
    }
    if (!c->fn->vkCreateFramebuffer) {
        mvk_unsupported(c, "vkCreateFramebuffer");
        return;
    }
    VkResult result = c->fn->vkCreateFramebuffer(device, pCreateInfo, NULL, pFramebuffer);
    mvk_bind_id(c, id_pFramebuffer, (uint64_t)pFramebuffer_value);
    mvk_track(c->conn, (uint64_t)pFramebuffer_value, MVK_KIND_VkFramebuffer);
    mvk_check(c, result, "vkCreateFramebuffer");
}

static void run_vkDestroyFramebuffer(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkFramebuffer framebuffer;
    device = (VkDevice)mvk_get_handle(d, 1);
    framebuffer = (VkFramebuffer)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyFramebuffer");
        return;
    }
    if (!c->fn->vkDestroyFramebuffer) {
        mvk_unsupported(c, "vkDestroyFramebuffer");
        return;
    }
    c->fn->vkDestroyFramebuffer(device, framebuffer, NULL);
    mvk_forget(c->conn, (uint64_t)framebuffer);
}

static void run_vkCreateRenderPass(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkRenderPassCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkRenderPassCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkRenderPassCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkRenderPassCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pRenderPass = mvk_get_u64(d);
    VkRenderPass pRenderPass_value = VK_NULL_HANDLE;
    VkRenderPass *pRenderPass = &pRenderPass_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateRenderPass");
        return;
    }
    if (!c->fn->vkCreateRenderPass) {
        mvk_unsupported(c, "vkCreateRenderPass");
        return;
    }
    VkResult result = c->fn->vkCreateRenderPass(device, pCreateInfo, NULL, pRenderPass);
    mvk_bind_id(c, id_pRenderPass, (uint64_t)pRenderPass_value);
    mvk_track(c->conn, (uint64_t)pRenderPass_value, MVK_KIND_VkRenderPass);
    mvk_check(c, result, "vkCreateRenderPass");
}

static void run_vkDestroyRenderPass(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkRenderPass renderPass;
    device = (VkDevice)mvk_get_handle(d, 1);
    renderPass = (VkRenderPass)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyRenderPass");
        return;
    }
    if (!c->fn->vkDestroyRenderPass) {
        mvk_unsupported(c, "vkDestroyRenderPass");
        return;
    }
    c->fn->vkDestroyRenderPass(device, renderPass, NULL);
    mvk_forget(c->conn, (uint64_t)renderPass);
}

static void run_vkGetRenderAreaGranularity(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkRenderPass renderPass;
    device = (VkDevice)mvk_get_handle(d, 1);
    renderPass = (VkRenderPass)mvk_get_handle(d, 0);
    VkExtent2D pGranularity_value;
    memset(&pGranularity_value, 0, sizeof(pGranularity_value));
    VkExtent2D *pGranularity = &pGranularity_value;
    if (d->error) {
        mvk_malformed(c, "vkGetRenderAreaGranularity");
        return;
    }
    if (!c->fn->vkGetRenderAreaGranularity) {
        mvk_unsupported(c, "vkGetRenderAreaGranularity");
        return;
    }
    c->fn->vkGetRenderAreaGranularity(device, renderPass, pGranularity);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkExtent2D(e, pGranularity);
    mvk_reply_end(c);
}

static void run_vkCmdSetViewport(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstViewport;
    uint32_t viewportCount;
    VkViewport * pViewports;
    firstViewport = (uint32_t)mvk_get_u32(d);
    viewportCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkViewport *items = mvk_alloc(d, (size_t)(viewportCount) * sizeof(VkViewport));
        for (uint32_t i = 0; i < (uint32_t)(viewportCount); i++)
            mvk_get_VkViewport(d, &items[i]);
        pViewports = items;
    } else {
        pViewports = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetViewport");
        return;
    }
    if (!c->fn->vkCmdSetViewport) {
        mvk_unsupported(c, "vkCmdSetViewport");
        return;
    }
    c->fn->vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
}

static void run_vkCmdSetScissor(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstScissor;
    uint32_t scissorCount;
    VkRect2D * pScissors;
    firstScissor = (uint32_t)mvk_get_u32(d);
    scissorCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkRect2D *items = mvk_alloc(d, (size_t)(scissorCount) * sizeof(VkRect2D));
        for (uint32_t i = 0; i < (uint32_t)(scissorCount); i++)
            mvk_get_VkRect2D(d, &items[i]);
        pScissors = items;
    } else {
        pScissors = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetScissor");
        return;
    }
    if (!c->fn->vkCmdSetScissor) {
        mvk_unsupported(c, "vkCmdSetScissor");
        return;
    }
    c->fn->vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
}

static void run_vkCmdSetLineWidth(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    float lineWidth;
    lineWidth = mvk_bits_f32(mvk_get_u32(d));
    if (d->error) {
        mvk_malformed(c, "vkCmdSetLineWidth");
        return;
    }
    if (!c->fn->vkCmdSetLineWidth) {
        mvk_unsupported(c, "vkCmdSetLineWidth");
        return;
    }
    c->fn->vkCmdSetLineWidth(commandBuffer, lineWidth);
}

static void run_vkCmdSetDepthBias(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    float depthBiasConstantFactor;
    float depthBiasClamp;
    float depthBiasSlopeFactor;
    depthBiasConstantFactor = mvk_bits_f32(mvk_get_u32(d));
    depthBiasClamp = mvk_bits_f32(mvk_get_u32(d));
    depthBiasSlopeFactor = mvk_bits_f32(mvk_get_u32(d));
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDepthBias");
        return;
    }
    if (!c->fn->vkCmdSetDepthBias) {
        mvk_unsupported(c, "vkCmdSetDepthBias");
        return;
    }
    c->fn->vkCmdSetDepthBias(commandBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
}

static void run_vkCmdSetBlendConstants(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    float blendConstants[4];
    mvk_get_bytes(d, blendConstants, (size_t)(4) * sizeof((blendConstants)[0]));
    if (d->error) {
        mvk_malformed(c, "vkCmdSetBlendConstants");
        return;
    }
    if (!c->fn->vkCmdSetBlendConstants) {
        mvk_unsupported(c, "vkCmdSetBlendConstants");
        return;
    }
    c->fn->vkCmdSetBlendConstants(commandBuffer, blendConstants);
}

static void run_vkCmdSetDepthBounds(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    float minDepthBounds;
    float maxDepthBounds;
    minDepthBounds = mvk_bits_f32(mvk_get_u32(d));
    maxDepthBounds = mvk_bits_f32(mvk_get_u32(d));
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDepthBounds");
        return;
    }
    if (!c->fn->vkCmdSetDepthBounds) {
        mvk_unsupported(c, "vkCmdSetDepthBounds");
        return;
    }
    c->fn->vkCmdSetDepthBounds(commandBuffer, minDepthBounds, maxDepthBounds);
}

static void run_vkCmdSetStencilCompareMask(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkStencilFaceFlags faceMask;
    uint32_t compareMask;
    faceMask = (VkStencilFaceFlags)mvk_get_u32(d);
    compareMask = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetStencilCompareMask");
        return;
    }
    if (!c->fn->vkCmdSetStencilCompareMask) {
        mvk_unsupported(c, "vkCmdSetStencilCompareMask");
        return;
    }
    c->fn->vkCmdSetStencilCompareMask(commandBuffer, faceMask, compareMask);
}

static void run_vkCmdSetStencilWriteMask(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkStencilFaceFlags faceMask;
    uint32_t writeMask;
    faceMask = (VkStencilFaceFlags)mvk_get_u32(d);
    writeMask = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetStencilWriteMask");
        return;
    }
    if (!c->fn->vkCmdSetStencilWriteMask) {
        mvk_unsupported(c, "vkCmdSetStencilWriteMask");
        return;
    }
    c->fn->vkCmdSetStencilWriteMask(commandBuffer, faceMask, writeMask);
}

static void run_vkCmdSetStencilReference(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkStencilFaceFlags faceMask;
    uint32_t reference;
    faceMask = (VkStencilFaceFlags)mvk_get_u32(d);
    reference = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetStencilReference");
        return;
    }
    if (!c->fn->vkCmdSetStencilReference) {
        mvk_unsupported(c, "vkCmdSetStencilReference");
        return;
    }
    c->fn->vkCmdSetStencilReference(commandBuffer, faceMask, reference);
}

static void run_vkCmdBindIndexBuffer(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer buffer;
    VkDeviceSize offset;
    VkIndexType indexType;
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    offset = (VkDeviceSize)mvk_get_u64(d);
    indexType = (VkIndexType)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdBindIndexBuffer");
        return;
    }
    if (!c->fn->vkCmdBindIndexBuffer) {
        mvk_unsupported(c, "vkCmdBindIndexBuffer");
        return;
    }
    c->fn->vkCmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
}

static void run_vkCmdBindVertexBuffers(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstBinding;
    uint32_t bindingCount;
    VkBuffer * pBuffers;
    VkDeviceSize * pOffsets;
    firstBinding = (uint32_t)mvk_get_u32(d);
    bindingCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBuffer *items = mvk_alloc(d, (size_t)(bindingCount) * sizeof(VkBuffer));
        for (uint32_t i = 0; i < (uint32_t)(bindingCount); i++)
            items[i] = (VkBuffer)mvk_get_handle(d, 0);
        pBuffers = items;
    } else {
        pBuffers = NULL;
    }
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(bindingCount) * sizeof(VkDeviceSize));
        mvk_get_bytes(d, items, (size_t)(bindingCount) * sizeof(VkDeviceSize));
        pOffsets = items;
    } else {
        pOffsets = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindVertexBuffers");
        return;
    }
    if (!c->fn->vkCmdBindVertexBuffers) {
        mvk_unsupported(c, "vkCmdBindVertexBuffers");
        return;
    }
    c->fn->vkCmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
}

static void run_vkCmdDraw(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
    vertexCount = (uint32_t)mvk_get_u32(d);
    instanceCount = (uint32_t)mvk_get_u32(d);
    firstVertex = (uint32_t)mvk_get_u32(d);
    firstInstance = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDraw");
        return;
    }
    if (!c->fn->vkCmdDraw) {
        mvk_unsupported(c, "vkCmdDraw");
        return;
    }
    c->fn->vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

static void run_vkCmdDrawIndexed(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t vertexOffset;
    uint32_t firstInstance;
    indexCount = (uint32_t)mvk_get_u32(d);
    instanceCount = (uint32_t)mvk_get_u32(d);
    firstIndex = (uint32_t)mvk_get_u32(d);
    vertexOffset = (int32_t)mvk_get_u32(d);
    firstInstance = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawIndexed");
        return;
    }
    if (!c->fn->vkCmdDrawIndexed) {
        mvk_unsupported(c, "vkCmdDrawIndexed");
        return;
    }
    c->fn->vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

static void run_vkCmdDrawIndirect(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer buffer;
    VkDeviceSize offset;
    uint32_t drawCount;
    uint32_t stride;
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    offset = (VkDeviceSize)mvk_get_u64(d);
    drawCount = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawIndirect");
        return;
    }
    if (!c->fn->vkCmdDrawIndirect) {
        mvk_unsupported(c, "vkCmdDrawIndirect");
        return;
    }
    c->fn->vkCmdDrawIndirect(commandBuffer, buffer, offset, drawCount, stride);
}

static void run_vkCmdDrawIndexedIndirect(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer buffer;
    VkDeviceSize offset;
    uint32_t drawCount;
    uint32_t stride;
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    offset = (VkDeviceSize)mvk_get_u64(d);
    drawCount = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawIndexedIndirect");
        return;
    }
    if (!c->fn->vkCmdDrawIndexedIndirect) {
        mvk_unsupported(c, "vkCmdDrawIndexedIndirect");
        return;
    }
    c->fn->vkCmdDrawIndexedIndirect(commandBuffer, buffer, offset, drawCount, stride);
}

static void run_vkCmdBlitImage(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkImage srcImage;
    VkImageLayout srcImageLayout;
    VkImage dstImage;
    VkImageLayout dstImageLayout;
    uint32_t regionCount;
    VkImageBlit * pRegions;
    VkFilter filter;
    srcImage = (VkImage)mvk_get_handle(d, 0);
    srcImageLayout = (VkImageLayout)mvk_get_u32(d);
    dstImage = (VkImage)mvk_get_handle(d, 0);
    dstImageLayout = (VkImageLayout)mvk_get_u32(d);
    regionCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkImageBlit *items = mvk_alloc(d, (size_t)(regionCount) * sizeof(VkImageBlit));
        for (uint32_t i = 0; i < (uint32_t)(regionCount); i++)
            mvk_get_VkImageBlit(d, &items[i]);
        pRegions = items;
    } else {
        pRegions = NULL;
    }
    filter = (VkFilter)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdBlitImage");
        return;
    }
    if (!c->fn->vkCmdBlitImage) {
        mvk_unsupported(c, "vkCmdBlitImage");
        return;
    }
    c->fn->vkCmdBlitImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
}

static void run_vkCmdClearDepthStencilImage(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkImage image;
    VkImageLayout imageLayout;
    VkClearDepthStencilValue * pDepthStencil;
    uint32_t rangeCount;
    VkImageSubresourceRange * pRanges;
    image = (VkImage)mvk_get_handle(d, 0);
    imageLayout = (VkImageLayout)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkClearDepthStencilValue *items = mvk_alloc(d, (size_t)(1) * sizeof(VkClearDepthStencilValue));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkClearDepthStencilValue(d, &items[i]);
        pDepthStencil = items;
    } else {
        pDepthStencil = NULL;
    }
    rangeCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkImageSubresourceRange *items = mvk_alloc(d, (size_t)(rangeCount) * sizeof(VkImageSubresourceRange));
        for (uint32_t i = 0; i < (uint32_t)(rangeCount); i++)
            mvk_get_VkImageSubresourceRange(d, &items[i]);
        pRanges = items;
    } else {
        pRanges = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdClearDepthStencilImage");
        return;
    }
    if (!c->fn->vkCmdClearDepthStencilImage) {
        mvk_unsupported(c, "vkCmdClearDepthStencilImage");
        return;
    }
    c->fn->vkCmdClearDepthStencilImage(commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
}

static void run_vkCmdClearAttachments(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t attachmentCount;
    VkClearAttachment * pAttachments;
    uint32_t rectCount;
    VkClearRect * pRects;
    attachmentCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkClearAttachment *items = mvk_alloc(d, (size_t)(attachmentCount) * sizeof(VkClearAttachment));
        for (uint32_t i = 0; i < (uint32_t)(attachmentCount); i++)
            mvk_get_VkClearAttachment(d, &items[i]);
        pAttachments = items;
    } else {
        pAttachments = NULL;
    }
    rectCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkClearRect *items = mvk_alloc(d, (size_t)(rectCount) * sizeof(VkClearRect));
        for (uint32_t i = 0; i < (uint32_t)(rectCount); i++)
            mvk_get_VkClearRect(d, &items[i]);
        pRects = items;
    } else {
        pRects = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdClearAttachments");
        return;
    }
    if (!c->fn->vkCmdClearAttachments) {
        mvk_unsupported(c, "vkCmdClearAttachments");
        return;
    }
    c->fn->vkCmdClearAttachments(commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
}

static void run_vkCmdResolveImage(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkImage srcImage;
    VkImageLayout srcImageLayout;
    VkImage dstImage;
    VkImageLayout dstImageLayout;
    uint32_t regionCount;
    VkImageResolve * pRegions;
    srcImage = (VkImage)mvk_get_handle(d, 0);
    srcImageLayout = (VkImageLayout)mvk_get_u32(d);
    dstImage = (VkImage)mvk_get_handle(d, 0);
    dstImageLayout = (VkImageLayout)mvk_get_u32(d);
    regionCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkImageResolve *items = mvk_alloc(d, (size_t)(regionCount) * sizeof(VkImageResolve));
        for (uint32_t i = 0; i < (uint32_t)(regionCount); i++)
            mvk_get_VkImageResolve(d, &items[i]);
        pRegions = items;
    } else {
        pRegions = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdResolveImage");
        return;
    }
    if (!c->fn->vkCmdResolveImage) {
        mvk_unsupported(c, "vkCmdResolveImage");
        return;
    }
    c->fn->vkCmdResolveImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}

static void run_vkCmdBeginRenderPass(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkRenderPassBeginInfo * pRenderPassBegin;
    VkSubpassContents contents;
    if (mvk_get_u32(d)) {
        VkRenderPassBeginInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkRenderPassBeginInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkRenderPassBeginInfo(d, &items[i]);
        pRenderPassBegin = items;
    } else {
        pRenderPassBegin = NULL;
    }
    contents = (VkSubpassContents)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdBeginRenderPass");
        return;
    }
    if (!c->fn->vkCmdBeginRenderPass) {
        mvk_unsupported(c, "vkCmdBeginRenderPass");
        return;
    }
    c->fn->vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

static void run_vkCmdNextSubpass(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkSubpassContents contents;
    contents = (VkSubpassContents)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdNextSubpass");
        return;
    }
    if (!c->fn->vkCmdNextSubpass) {
        mvk_unsupported(c, "vkCmdNextSubpass");
        return;
    }
    c->fn->vkCmdNextSubpass(commandBuffer, contents);
}

static void run_vkCmdEndRenderPass(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    if (d->error) {
        mvk_malformed(c, "vkCmdEndRenderPass");
        return;
    }
    if (!c->fn->vkCmdEndRenderPass) {
        mvk_unsupported(c, "vkCmdEndRenderPass");
        return;
    }
    c->fn->vkCmdEndRenderPass(commandBuffer);
}

static void run_vkBindBufferMemory2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t bindInfoCount;
    VkBindBufferMemoryInfo * pBindInfos;
    device = (VkDevice)mvk_get_handle(d, 1);
    bindInfoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBindBufferMemoryInfo *items = mvk_alloc(d, (size_t)(bindInfoCount) * sizeof(VkBindBufferMemoryInfo));
        for (uint32_t i = 0; i < (uint32_t)(bindInfoCount); i++)
            mvk_get_VkBindBufferMemoryInfo(d, &items[i]);
        pBindInfos = items;
    } else {
        pBindInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkBindBufferMemory2");
        return;
    }
    if (!c->fn->vkBindBufferMemory2) {
        mvk_unsupported(c, "vkBindBufferMemory2");
        return;
    }
    VkResult result = c->fn->vkBindBufferMemory2(device, bindInfoCount, pBindInfos);
    mvk_check(c, result, "vkBindBufferMemory2");
}

static void run_vkBindImageMemory2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t bindInfoCount;
    VkBindImageMemoryInfo * pBindInfos;
    device = (VkDevice)mvk_get_handle(d, 1);
    bindInfoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBindImageMemoryInfo *items = mvk_alloc(d, (size_t)(bindInfoCount) * sizeof(VkBindImageMemoryInfo));
        for (uint32_t i = 0; i < (uint32_t)(bindInfoCount); i++)
            mvk_get_VkBindImageMemoryInfo(d, &items[i]);
        pBindInfos = items;
    } else {
        pBindInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkBindImageMemory2");
        return;
    }
    if (!c->fn->vkBindImageMemory2) {
        mvk_unsupported(c, "vkBindImageMemory2");
        return;
    }
    VkResult result = c->fn->vkBindImageMemory2(device, bindInfoCount, pBindInfos);
    mvk_check(c, result, "vkBindImageMemory2");
}

static void run_vkGetDeviceGroupPeerMemoryFeatures(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t heapIndex;
    uint32_t localDeviceIndex;
    uint32_t remoteDeviceIndex;
    device = (VkDevice)mvk_get_handle(d, 1);
    heapIndex = (uint32_t)mvk_get_u32(d);
    localDeviceIndex = (uint32_t)mvk_get_u32(d);
    remoteDeviceIndex = (uint32_t)mvk_get_u32(d);
    VkPeerMemoryFeatureFlags pPeerMemoryFeatures_value;
    memset(&pPeerMemoryFeatures_value, 0, sizeof(pPeerMemoryFeatures_value));
    VkPeerMemoryFeatureFlags *pPeerMemoryFeatures = &pPeerMemoryFeatures_value;
    if (d->error) {
        mvk_malformed(c, "vkGetDeviceGroupPeerMemoryFeatures");
        return;
    }
    if (!c->fn->vkGetDeviceGroupPeerMemoryFeatures) {
        mvk_unsupported(c, "vkGetDeviceGroupPeerMemoryFeatures");
        return;
    }
    c->fn->vkGetDeviceGroupPeerMemoryFeatures(device, heapIndex, localDeviceIndex, remoteDeviceIndex, pPeerMemoryFeatures);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(*pPeerMemoryFeatures));
    mvk_reply_end(c);
}

static void run_vkCmdSetDeviceMask(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t deviceMask;
    deviceMask = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDeviceMask");
        return;
    }
    if (!c->fn->vkCmdSetDeviceMask) {
        mvk_unsupported(c, "vkCmdSetDeviceMask");
        return;
    }
    c->fn->vkCmdSetDeviceMask(commandBuffer, deviceMask);
}

static void run_vkGetImageMemoryRequirements2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImageMemoryRequirementsInfo2 * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkImageMemoryRequirementsInfo2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkImageMemoryRequirementsInfo2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkImageMemoryRequirementsInfo2(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    VkMemoryRequirements2 *pMemoryRequirements = mvk_get_out_shape(d, sizeof(VkMemoryRequirements2));
    if (d->error) {
        mvk_malformed(c, "vkGetImageMemoryRequirements2");
        return;
    }
    if (!c->fn->vkGetImageMemoryRequirements2) {
        mvk_unsupported(c, "vkGetImageMemoryRequirements2");
        return;
    }
    c->fn->vkGetImageMemoryRequirements2(device, pInfo, pMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkMemoryRequirements2(e, pMemoryRequirements);
    mvk_reply_end(c);
}

static void run_vkGetBufferMemoryRequirements2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkBufferMemoryRequirementsInfo2 * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkBufferMemoryRequirementsInfo2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBufferMemoryRequirementsInfo2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBufferMemoryRequirementsInfo2(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    VkMemoryRequirements2 *pMemoryRequirements = mvk_get_out_shape(d, sizeof(VkMemoryRequirements2));
    if (d->error) {
        mvk_malformed(c, "vkGetBufferMemoryRequirements2");
        return;
    }
    if (!c->fn->vkGetBufferMemoryRequirements2) {
        mvk_unsupported(c, "vkGetBufferMemoryRequirements2");
        return;
    }
    c->fn->vkGetBufferMemoryRequirements2(device, pInfo, pMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkMemoryRequirements2(e, pMemoryRequirements);
    mvk_reply_end(c);
}

static void run_vkGetImageSparseMemoryRequirements2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImageSparseMemoryRequirementsInfo2 * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkImageSparseMemoryRequirementsInfo2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkImageSparseMemoryRequirementsInfo2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkImageSparseMemoryRequirementsInfo2(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    uint32_t pSparseMemoryRequirementCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pSparseMemoryRequirementCount = &pSparseMemoryRequirementCount_value;
    int want_pSparseMemoryRequirements = mvk_get_u32(d);
    VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements = NULL;
    uint32_t shape_count_pSparseMemoryRequirements = 0;
    if (want_pSparseMemoryRequirements) {
        pSparseMemoryRequirements = mvk_alloc(d, sizeof(VkSparseImageMemoryRequirements2) * (size_t)(*pSparseMemoryRequirementCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pSparseMemoryRequirementCount); i++)
            mvk_get_out_shape_into(d, &pSparseMemoryRequirements[i], sizeof(VkSparseImageMemoryRequirements2));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetImageSparseMemoryRequirements2");
        return;
    }
    if (!c->fn->vkGetImageSparseMemoryRequirements2) {
        mvk_unsupported(c, "vkGetImageSparseMemoryRequirements2");
        return;
    }
    c->fn->vkGetImageSparseMemoryRequirements2(device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(*pSparseMemoryRequirementCount));
    mvk_put_u32(e, pSparseMemoryRequirements != NULL);
    if (pSparseMemoryRequirements) {
        mvk_put_u32(e, (uint32_t)(*pSparseMemoryRequirementCount));
        for (uint32_t i = 0; i < (uint32_t)(*pSparseMemoryRequirementCount); i++)
            reply_VkSparseImageMemoryRequirements2(e, &pSparseMemoryRequirements[i]);
    }
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceFeatures2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    VkPhysicalDeviceFeatures2 *pFeatures = mvk_get_out_shape(d, sizeof(VkPhysicalDeviceFeatures2));
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceFeatures2");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceFeatures2) {
        mvk_unsupported(c, "vkGetPhysicalDeviceFeatures2");
        return;
    }
    c->fn->vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkPhysicalDeviceFeatures2(e, pFeatures);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceProperties2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    VkPhysicalDeviceProperties2 *pProperties = mvk_get_out_shape(d, sizeof(VkPhysicalDeviceProperties2));
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceProperties2");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceProperties2) {
        mvk_unsupported(c, "vkGetPhysicalDeviceProperties2");
        return;
    }
    c->fn->vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkPhysicalDeviceProperties2(e, pProperties);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceFormatProperties2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkFormat format;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    format = (VkFormat)mvk_get_u32(d);
    VkFormatProperties2 *pFormatProperties = mvk_get_out_shape(d, sizeof(VkFormatProperties2));
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceFormatProperties2");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceFormatProperties2) {
        mvk_unsupported(c, "vkGetPhysicalDeviceFormatProperties2");
        return;
    }
    c->fn->vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, pFormatProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkFormatProperties2(e, pFormatProperties);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceImageFormatProperties2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceImageFormatInfo2 * pImageFormatInfo;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPhysicalDeviceImageFormatInfo2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPhysicalDeviceImageFormatInfo2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPhysicalDeviceImageFormatInfo2(d, &items[i]);
        pImageFormatInfo = items;
    } else {
        pImageFormatInfo = NULL;
    }
    VkImageFormatProperties2 *pImageFormatProperties = mvk_get_out_shape(d, sizeof(VkImageFormatProperties2));
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceImageFormatProperties2");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceImageFormatProperties2) {
        mvk_unsupported(c, "vkGetPhysicalDeviceImageFormatProperties2");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, pImageFormatInfo, pImageFormatProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    reply_VkImageFormatProperties2(e, pImageFormatProperties);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceQueueFamilyProperties2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    uint32_t pQueueFamilyPropertyCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pQueueFamilyPropertyCount = &pQueueFamilyPropertyCount_value;
    int want_pQueueFamilyProperties = mvk_get_u32(d);
    VkQueueFamilyProperties2 *pQueueFamilyProperties = NULL;
    uint32_t shape_count_pQueueFamilyProperties = 0;
    if (want_pQueueFamilyProperties) {
        pQueueFamilyProperties = mvk_alloc(d, sizeof(VkQueueFamilyProperties2) * (size_t)(*pQueueFamilyPropertyCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pQueueFamilyPropertyCount); i++)
            mvk_get_out_shape_into(d, &pQueueFamilyProperties[i], sizeof(VkQueueFamilyProperties2));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceQueueFamilyProperties2");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceQueueFamilyProperties2) {
        mvk_unsupported(c, "vkGetPhysicalDeviceQueueFamilyProperties2");
        return;
    }
    c->fn->vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(*pQueueFamilyPropertyCount));
    mvk_put_u32(e, pQueueFamilyProperties != NULL);
    if (pQueueFamilyProperties) {
        mvk_put_u32(e, (uint32_t)(*pQueueFamilyPropertyCount));
        for (uint32_t i = 0; i < (uint32_t)(*pQueueFamilyPropertyCount); i++)
            reply_VkQueueFamilyProperties2(e, &pQueueFamilyProperties[i]);
    }
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceMemoryProperties2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    VkPhysicalDeviceMemoryProperties2 *pMemoryProperties = mvk_get_out_shape(d, sizeof(VkPhysicalDeviceMemoryProperties2));
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceMemoryProperties2");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceMemoryProperties2) {
        mvk_unsupported(c, "vkGetPhysicalDeviceMemoryProperties2");
        return;
    }
    c->fn->vkGetPhysicalDeviceMemoryProperties2(physicalDevice, pMemoryProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkPhysicalDeviceMemoryProperties2(e, pMemoryProperties);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceSparseImageFormatProperties2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceSparseImageFormatInfo2 * pFormatInfo;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPhysicalDeviceSparseImageFormatInfo2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPhysicalDeviceSparseImageFormatInfo2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPhysicalDeviceSparseImageFormatInfo2(d, &items[i]);
        pFormatInfo = items;
    } else {
        pFormatInfo = NULL;
    }
    uint32_t pPropertyCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pPropertyCount = &pPropertyCount_value;
    int want_pProperties = mvk_get_u32(d);
    VkSparseImageFormatProperties2 *pProperties = NULL;
    uint32_t shape_count_pProperties = 0;
    if (want_pProperties) {
        pProperties = mvk_alloc(d, sizeof(VkSparseImageFormatProperties2) * (size_t)(*pPropertyCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            mvk_get_out_shape_into(d, &pProperties[i], sizeof(VkSparseImageFormatProperties2));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceSparseImageFormatProperties2");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceSparseImageFormatProperties2) {
        mvk_unsupported(c, "vkGetPhysicalDeviceSparseImageFormatProperties2");
        return;
    }
    c->fn->vkGetPhysicalDeviceSparseImageFormatProperties2(physicalDevice, pFormatInfo, pPropertyCount, pProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(*pPropertyCount));
    mvk_put_u32(e, pProperties != NULL);
    if (pProperties) {
        mvk_put_u32(e, (uint32_t)(*pPropertyCount));
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            reply_VkSparseImageFormatProperties2(e, &pProperties[i]);
    }
    mvk_reply_end(c);
}

static void run_vkTrimCommandPool(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkCommandPool commandPool;
    VkCommandPoolTrimFlags flags;
    device = (VkDevice)mvk_get_handle(d, 1);
    commandPool = (VkCommandPool)mvk_get_handle(d, 0);
    flags = (VkCommandPoolTrimFlags)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkTrimCommandPool");
        return;
    }
    if (!c->fn->vkTrimCommandPool) {
        mvk_unsupported(c, "vkTrimCommandPool");
        return;
    }
    c->fn->vkTrimCommandPool(device, commandPool, flags);
}

static void run_vkGetPhysicalDeviceExternalBufferProperties(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceExternalBufferInfo * pExternalBufferInfo;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPhysicalDeviceExternalBufferInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPhysicalDeviceExternalBufferInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPhysicalDeviceExternalBufferInfo(d, &items[i]);
        pExternalBufferInfo = items;
    } else {
        pExternalBufferInfo = NULL;
    }
    VkExternalBufferProperties *pExternalBufferProperties = mvk_get_out_shape(d, sizeof(VkExternalBufferProperties));
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceExternalBufferProperties");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceExternalBufferProperties) {
        mvk_unsupported(c, "vkGetPhysicalDeviceExternalBufferProperties");
        return;
    }
    c->fn->vkGetPhysicalDeviceExternalBufferProperties(physicalDevice, pExternalBufferInfo, pExternalBufferProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkExternalBufferProperties(e, pExternalBufferProperties);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceExternalFenceProperties(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceExternalFenceInfo * pExternalFenceInfo;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPhysicalDeviceExternalFenceInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPhysicalDeviceExternalFenceInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPhysicalDeviceExternalFenceInfo(d, &items[i]);
        pExternalFenceInfo = items;
    } else {
        pExternalFenceInfo = NULL;
    }
    VkExternalFenceProperties *pExternalFenceProperties = mvk_get_out_shape(d, sizeof(VkExternalFenceProperties));
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceExternalFenceProperties");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceExternalFenceProperties) {
        mvk_unsupported(c, "vkGetPhysicalDeviceExternalFenceProperties");
        return;
    }
    c->fn->vkGetPhysicalDeviceExternalFenceProperties(physicalDevice, pExternalFenceInfo, pExternalFenceProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkExternalFenceProperties(e, pExternalFenceProperties);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceExternalSemaphoreProperties(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceExternalSemaphoreInfo * pExternalSemaphoreInfo;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPhysicalDeviceExternalSemaphoreInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPhysicalDeviceExternalSemaphoreInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPhysicalDeviceExternalSemaphoreInfo(d, &items[i]);
        pExternalSemaphoreInfo = items;
    } else {
        pExternalSemaphoreInfo = NULL;
    }
    VkExternalSemaphoreProperties *pExternalSemaphoreProperties = mvk_get_out_shape(d, sizeof(VkExternalSemaphoreProperties));
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceExternalSemaphoreProperties");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceExternalSemaphoreProperties) {
        mvk_unsupported(c, "vkGetPhysicalDeviceExternalSemaphoreProperties");
        return;
    }
    c->fn->vkGetPhysicalDeviceExternalSemaphoreProperties(physicalDevice, pExternalSemaphoreInfo, pExternalSemaphoreProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkExternalSemaphoreProperties(e, pExternalSemaphoreProperties);
    mvk_reply_end(c);
}

static void run_vkCmdDispatchBase(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t baseGroupX;
    uint32_t baseGroupY;
    uint32_t baseGroupZ;
    uint32_t groupCountX;
    uint32_t groupCountY;
    uint32_t groupCountZ;
    baseGroupX = (uint32_t)mvk_get_u32(d);
    baseGroupY = (uint32_t)mvk_get_u32(d);
    baseGroupZ = (uint32_t)mvk_get_u32(d);
    groupCountX = (uint32_t)mvk_get_u32(d);
    groupCountY = (uint32_t)mvk_get_u32(d);
    groupCountZ = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDispatchBase");
        return;
    }
    if (!c->fn->vkCmdDispatchBase) {
        mvk_unsupported(c, "vkCmdDispatchBase");
        return;
    }
    c->fn->vkCmdDispatchBase(commandBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ);
}

static void run_vkCreateDescriptorUpdateTemplate(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDescriptorUpdateTemplateCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkDescriptorUpdateTemplateCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDescriptorUpdateTemplateCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDescriptorUpdateTemplateCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pDescriptorUpdateTemplate = mvk_get_u64(d);
    VkDescriptorUpdateTemplate pDescriptorUpdateTemplate_value = VK_NULL_HANDLE;
    VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate = &pDescriptorUpdateTemplate_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateDescriptorUpdateTemplate");
        return;
    }
    if (!c->fn->vkCreateDescriptorUpdateTemplate) {
        mvk_unsupported(c, "vkCreateDescriptorUpdateTemplate");
        return;
    }
    VkResult result = c->fn->vkCreateDescriptorUpdateTemplate(device, pCreateInfo, NULL, pDescriptorUpdateTemplate);
    mvk_bind_id(c, id_pDescriptorUpdateTemplate, (uint64_t)pDescriptorUpdateTemplate_value);
    mvk_track(c->conn, (uint64_t)pDescriptorUpdateTemplate_value, MVK_KIND_VkDescriptorUpdateTemplate);
    mvk_check(c, result, "vkCreateDescriptorUpdateTemplate");
}

static void run_vkDestroyDescriptorUpdateTemplate(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDescriptorUpdateTemplate descriptorUpdateTemplate;
    device = (VkDevice)mvk_get_handle(d, 1);
    descriptorUpdateTemplate = (VkDescriptorUpdateTemplate)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyDescriptorUpdateTemplate");
        return;
    }
    if (!c->fn->vkDestroyDescriptorUpdateTemplate) {
        mvk_unsupported(c, "vkDestroyDescriptorUpdateTemplate");
        return;
    }
    c->fn->vkDestroyDescriptorUpdateTemplate(device, descriptorUpdateTemplate, NULL);
    mvk_forget(c->conn, (uint64_t)descriptorUpdateTemplate);
}

static void run_vkGetDescriptorSetLayoutSupport(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDescriptorSetLayoutCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkDescriptorSetLayoutCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDescriptorSetLayoutCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDescriptorSetLayoutCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    VkDescriptorSetLayoutSupport *pSupport = mvk_get_out_shape(d, sizeof(VkDescriptorSetLayoutSupport));
    if (d->error) {
        mvk_malformed(c, "vkGetDescriptorSetLayoutSupport");
        return;
    }
    if (!c->fn->vkGetDescriptorSetLayoutSupport) {
        mvk_unsupported(c, "vkGetDescriptorSetLayoutSupport");
        return;
    }
    c->fn->vkGetDescriptorSetLayoutSupport(device, pCreateInfo, pSupport);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkDescriptorSetLayoutSupport(e, pSupport);
    mvk_reply_end(c);
}

static void run_vkCreateSamplerYcbcrConversion(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkSamplerYcbcrConversionCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkSamplerYcbcrConversionCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkSamplerYcbcrConversionCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkSamplerYcbcrConversionCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pYcbcrConversion = mvk_get_u64(d);
    VkSamplerYcbcrConversion pYcbcrConversion_value = VK_NULL_HANDLE;
    VkSamplerYcbcrConversion *pYcbcrConversion = &pYcbcrConversion_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateSamplerYcbcrConversion");
        return;
    }
    if (!c->fn->vkCreateSamplerYcbcrConversion) {
        mvk_unsupported(c, "vkCreateSamplerYcbcrConversion");
        return;
    }
    VkResult result = c->fn->vkCreateSamplerYcbcrConversion(device, pCreateInfo, NULL, pYcbcrConversion);
    mvk_bind_id(c, id_pYcbcrConversion, (uint64_t)pYcbcrConversion_value);
    mvk_track(c->conn, (uint64_t)pYcbcrConversion_value, MVK_KIND_VkSamplerYcbcrConversion);
    mvk_check(c, result, "vkCreateSamplerYcbcrConversion");
}

static void run_vkDestroySamplerYcbcrConversion(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkSamplerYcbcrConversion ycbcrConversion;
    device = (VkDevice)mvk_get_handle(d, 1);
    ycbcrConversion = (VkSamplerYcbcrConversion)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroySamplerYcbcrConversion");
        return;
    }
    if (!c->fn->vkDestroySamplerYcbcrConversion) {
        mvk_unsupported(c, "vkDestroySamplerYcbcrConversion");
        return;
    }
    c->fn->vkDestroySamplerYcbcrConversion(device, ycbcrConversion, NULL);
    mvk_forget(c->conn, (uint64_t)ycbcrConversion);
}

static void run_vkResetQueryPool(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkQueryPool queryPool;
    uint32_t firstQuery;
    uint32_t queryCount;
    device = (VkDevice)mvk_get_handle(d, 1);
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    firstQuery = (uint32_t)mvk_get_u32(d);
    queryCount = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkResetQueryPool");
        return;
    }
    if (!c->fn->vkResetQueryPool) {
        mvk_unsupported(c, "vkResetQueryPool");
        return;
    }
    c->fn->vkResetQueryPool(device, queryPool, firstQuery, queryCount);
}

static void run_vkGetBufferDeviceAddress(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkBufferDeviceAddressInfo * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkBufferDeviceAddressInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBufferDeviceAddressInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBufferDeviceAddressInfo(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkGetBufferDeviceAddress");
        return;
    }
    if (!c->fn->vkGetBufferDeviceAddress) {
        mvk_unsupported(c, "vkGetBufferDeviceAddress");
        return;
    }
    VkDeviceAddress result = c->fn->vkGetBufferDeviceAddress(device, pInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u64(e, (uint64_t)(result));
    mvk_reply_end(c);
}

static void run_vkGetBufferOpaqueCaptureAddress(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkBufferDeviceAddressInfo * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkBufferDeviceAddressInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBufferDeviceAddressInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBufferDeviceAddressInfo(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkGetBufferOpaqueCaptureAddress");
        return;
    }
    if (!c->fn->vkGetBufferOpaqueCaptureAddress) {
        mvk_unsupported(c, "vkGetBufferOpaqueCaptureAddress");
        return;
    }
    uint64_t result = c->fn->vkGetBufferOpaqueCaptureAddress(device, pInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u64(e, (uint64_t)(result));
    mvk_reply_end(c);
}

static void run_vkGetDeviceMemoryOpaqueCaptureAddress(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeviceMemoryOpaqueCaptureAddressInfo * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkDeviceMemoryOpaqueCaptureAddressInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDeviceMemoryOpaqueCaptureAddressInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDeviceMemoryOpaqueCaptureAddressInfo(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkGetDeviceMemoryOpaqueCaptureAddress");
        return;
    }
    if (!c->fn->vkGetDeviceMemoryOpaqueCaptureAddress) {
        mvk_unsupported(c, "vkGetDeviceMemoryOpaqueCaptureAddress");
        return;
    }
    uint64_t result = c->fn->vkGetDeviceMemoryOpaqueCaptureAddress(device, pInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u64(e, (uint64_t)(result));
    mvk_reply_end(c);
}

static void run_vkCmdDrawIndirectCount(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer buffer;
    VkDeviceSize offset;
    VkBuffer countBuffer;
    VkDeviceSize countBufferOffset;
    uint32_t maxDrawCount;
    uint32_t stride;
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    offset = (VkDeviceSize)mvk_get_u64(d);
    countBuffer = (VkBuffer)mvk_get_handle(d, 0);
    countBufferOffset = (VkDeviceSize)mvk_get_u64(d);
    maxDrawCount = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawIndirectCount");
        return;
    }
    if (!c->fn->vkCmdDrawIndirectCount) {
        mvk_unsupported(c, "vkCmdDrawIndirectCount");
        return;
    }
    c->fn->vkCmdDrawIndirectCount(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

static void run_vkCmdDrawIndexedIndirectCount(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer buffer;
    VkDeviceSize offset;
    VkBuffer countBuffer;
    VkDeviceSize countBufferOffset;
    uint32_t maxDrawCount;
    uint32_t stride;
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    offset = (VkDeviceSize)mvk_get_u64(d);
    countBuffer = (VkBuffer)mvk_get_handle(d, 0);
    countBufferOffset = (VkDeviceSize)mvk_get_u64(d);
    maxDrawCount = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawIndexedIndirectCount");
        return;
    }
    if (!c->fn->vkCmdDrawIndexedIndirectCount) {
        mvk_unsupported(c, "vkCmdDrawIndexedIndirectCount");
        return;
    }
    c->fn->vkCmdDrawIndexedIndirectCount(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

static void run_vkCreateRenderPass2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkRenderPassCreateInfo2 * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkRenderPassCreateInfo2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkRenderPassCreateInfo2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkRenderPassCreateInfo2(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pRenderPass = mvk_get_u64(d);
    VkRenderPass pRenderPass_value = VK_NULL_HANDLE;
    VkRenderPass *pRenderPass = &pRenderPass_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateRenderPass2");
        return;
    }
    if (!c->fn->vkCreateRenderPass2) {
        mvk_unsupported(c, "vkCreateRenderPass2");
        return;
    }
    VkResult result = c->fn->vkCreateRenderPass2(device, pCreateInfo, NULL, pRenderPass);
    mvk_bind_id(c, id_pRenderPass, (uint64_t)pRenderPass_value);
    mvk_track(c->conn, (uint64_t)pRenderPass_value, MVK_KIND_VkRenderPass);
    mvk_check(c, result, "vkCreateRenderPass2");
}

static void run_vkCmdBeginRenderPass2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkRenderPassBeginInfo * pRenderPassBegin;
    VkSubpassBeginInfo * pSubpassBeginInfo;
    if (mvk_get_u32(d)) {
        VkRenderPassBeginInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkRenderPassBeginInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkRenderPassBeginInfo(d, &items[i]);
        pRenderPassBegin = items;
    } else {
        pRenderPassBegin = NULL;
    }
    if (mvk_get_u32(d)) {
        VkSubpassBeginInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkSubpassBeginInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkSubpassBeginInfo(d, &items[i]);
        pSubpassBeginInfo = items;
    } else {
        pSubpassBeginInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBeginRenderPass2");
        return;
    }
    if (!c->fn->vkCmdBeginRenderPass2) {
        mvk_unsupported(c, "vkCmdBeginRenderPass2");
        return;
    }
    c->fn->vkCmdBeginRenderPass2(commandBuffer, pRenderPassBegin, pSubpassBeginInfo);
}

static void run_vkCmdNextSubpass2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkSubpassBeginInfo * pSubpassBeginInfo;
    VkSubpassEndInfo * pSubpassEndInfo;
    if (mvk_get_u32(d)) {
        VkSubpassBeginInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkSubpassBeginInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkSubpassBeginInfo(d, &items[i]);
        pSubpassBeginInfo = items;
    } else {
        pSubpassBeginInfo = NULL;
    }
    if (mvk_get_u32(d)) {
        VkSubpassEndInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkSubpassEndInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkSubpassEndInfo(d, &items[i]);
        pSubpassEndInfo = items;
    } else {
        pSubpassEndInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdNextSubpass2");
        return;
    }
    if (!c->fn->vkCmdNextSubpass2) {
        mvk_unsupported(c, "vkCmdNextSubpass2");
        return;
    }
    c->fn->vkCmdNextSubpass2(commandBuffer, pSubpassBeginInfo, pSubpassEndInfo);
}

static void run_vkCmdEndRenderPass2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkSubpassEndInfo * pSubpassEndInfo;
    if (mvk_get_u32(d)) {
        VkSubpassEndInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkSubpassEndInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkSubpassEndInfo(d, &items[i]);
        pSubpassEndInfo = items;
    } else {
        pSubpassEndInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdEndRenderPass2");
        return;
    }
    if (!c->fn->vkCmdEndRenderPass2) {
        mvk_unsupported(c, "vkCmdEndRenderPass2");
        return;
    }
    c->fn->vkCmdEndRenderPass2(commandBuffer, pSubpassEndInfo);
}

static void run_vkGetPhysicalDeviceToolProperties(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    uint32_t pToolCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pToolCount = &pToolCount_value;
    int want_pToolProperties = mvk_get_u32(d);
    VkPhysicalDeviceToolProperties *pToolProperties = NULL;
    uint32_t shape_count_pToolProperties = 0;
    if (want_pToolProperties) {
        pToolProperties = mvk_alloc(d, sizeof(VkPhysicalDeviceToolProperties) * (size_t)(*pToolCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pToolCount); i++)
            mvk_get_out_shape_into(d, &pToolProperties[i], sizeof(VkPhysicalDeviceToolProperties));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceToolProperties");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceToolProperties) {
        mvk_unsupported(c, "vkGetPhysicalDeviceToolProperties");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceToolProperties(physicalDevice, pToolCount, pToolProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pToolCount));
    mvk_put_u32(e, pToolProperties != NULL);
    if (pToolProperties) {
        mvk_put_u32(e, (uint32_t)(*pToolCount));
        for (uint32_t i = 0; i < (uint32_t)(*pToolCount); i++)
            reply_VkPhysicalDeviceToolProperties(e, &pToolProperties[i]);
    }
    mvk_reply_end(c);
}

static void run_vkCreatePrivateDataSlot(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPrivateDataSlotCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPrivateDataSlotCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPrivateDataSlotCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPrivateDataSlotCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pPrivateDataSlot = mvk_get_u64(d);
    VkPrivateDataSlot pPrivateDataSlot_value = VK_NULL_HANDLE;
    VkPrivateDataSlot *pPrivateDataSlot = &pPrivateDataSlot_value;
    if (d->error) {
        mvk_malformed(c, "vkCreatePrivateDataSlot");
        return;
    }
    if (!c->fn->vkCreatePrivateDataSlot) {
        mvk_unsupported(c, "vkCreatePrivateDataSlot");
        return;
    }
    VkResult result = c->fn->vkCreatePrivateDataSlot(device, pCreateInfo, NULL, pPrivateDataSlot);
    mvk_bind_id(c, id_pPrivateDataSlot, (uint64_t)pPrivateDataSlot_value);
    mvk_track(c->conn, (uint64_t)pPrivateDataSlot_value, MVK_KIND_VkPrivateDataSlot);
    mvk_check(c, result, "vkCreatePrivateDataSlot");
}

static void run_vkDestroyPrivateDataSlot(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPrivateDataSlot privateDataSlot;
    device = (VkDevice)mvk_get_handle(d, 1);
    privateDataSlot = (VkPrivateDataSlot)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyPrivateDataSlot");
        return;
    }
    if (!c->fn->vkDestroyPrivateDataSlot) {
        mvk_unsupported(c, "vkDestroyPrivateDataSlot");
        return;
    }
    c->fn->vkDestroyPrivateDataSlot(device, privateDataSlot, NULL);
    mvk_forget(c->conn, (uint64_t)privateDataSlot);
}

static void run_vkSetPrivateData(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkObjectType objectType;
    uint64_t objectHandle;
    VkPrivateDataSlot privateDataSlot;
    uint64_t data;
    device = (VkDevice)mvk_get_handle(d, 1);
    objectType = (VkObjectType)mvk_get_u32(d);
    objectHandle = (uint64_t)mvk_get_u64(d);
    privateDataSlot = (VkPrivateDataSlot)mvk_get_handle(d, 0);
    data = (uint64_t)mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkSetPrivateData");
        return;
    }
    if (!c->fn->vkSetPrivateData) {
        mvk_unsupported(c, "vkSetPrivateData");
        return;
    }
    VkResult result = c->fn->vkSetPrivateData(device, objectType, objectHandle, privateDataSlot, data);
    mvk_check(c, result, "vkSetPrivateData");
}

static void run_vkGetPrivateData(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkObjectType objectType;
    uint64_t objectHandle;
    VkPrivateDataSlot privateDataSlot;
    device = (VkDevice)mvk_get_handle(d, 1);
    objectType = (VkObjectType)mvk_get_u32(d);
    objectHandle = (uint64_t)mvk_get_u64(d);
    privateDataSlot = (VkPrivateDataSlot)mvk_get_handle(d, 0);
    uint64_t pData_value;
    memset(&pData_value, 0, sizeof(pData_value));
    uint64_t *pData = &pData_value;
    if (d->error) {
        mvk_malformed(c, "vkGetPrivateData");
        return;
    }
    if (!c->fn->vkGetPrivateData) {
        mvk_unsupported(c, "vkGetPrivateData");
        return;
    }
    c->fn->vkGetPrivateData(device, objectType, objectHandle, privateDataSlot, pData);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u64(e, (uint64_t)(*pData));
    mvk_reply_end(c);
}

static void run_vkCmdPipelineBarrier2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDependencyInfo * pDependencyInfo;
    if (mvk_get_u32(d)) {
        VkDependencyInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDependencyInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDependencyInfo(d, &items[i]);
        pDependencyInfo = items;
    } else {
        pDependencyInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdPipelineBarrier2");
        return;
    }
    if (!c->fn->vkCmdPipelineBarrier2) {
        mvk_unsupported(c, "vkCmdPipelineBarrier2");
        return;
    }
    c->fn->vkCmdPipelineBarrier2(commandBuffer, pDependencyInfo);
}

static void run_vkCmdWriteTimestamp2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineStageFlags2 stage;
    VkQueryPool queryPool;
    uint32_t query;
    stage = (VkPipelineStageFlags2)mvk_get_u64(d);
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    query = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdWriteTimestamp2");
        return;
    }
    if (!c->fn->vkCmdWriteTimestamp2) {
        mvk_unsupported(c, "vkCmdWriteTimestamp2");
        return;
    }
    c->fn->vkCmdWriteTimestamp2(commandBuffer, stage, queryPool, query);
}

static void run_vkCmdCopyBuffer2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyBufferInfo2 * pCopyBufferInfo;
    if (mvk_get_u32(d)) {
        VkCopyBufferInfo2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyBufferInfo2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyBufferInfo2(d, &items[i]);
        pCopyBufferInfo = items;
    } else {
        pCopyBufferInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyBuffer2");
        return;
    }
    if (!c->fn->vkCmdCopyBuffer2) {
        mvk_unsupported(c, "vkCmdCopyBuffer2");
        return;
    }
    c->fn->vkCmdCopyBuffer2(commandBuffer, pCopyBufferInfo);
}

static void run_vkCmdCopyImage2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyImageInfo2 * pCopyImageInfo;
    if (mvk_get_u32(d)) {
        VkCopyImageInfo2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyImageInfo2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyImageInfo2(d, &items[i]);
        pCopyImageInfo = items;
    } else {
        pCopyImageInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyImage2");
        return;
    }
    if (!c->fn->vkCmdCopyImage2) {
        mvk_unsupported(c, "vkCmdCopyImage2");
        return;
    }
    c->fn->vkCmdCopyImage2(commandBuffer, pCopyImageInfo);
}

static void run_vkCmdCopyBufferToImage2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyBufferToImageInfo2 * pCopyBufferToImageInfo;
    if (mvk_get_u32(d)) {
        VkCopyBufferToImageInfo2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyBufferToImageInfo2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyBufferToImageInfo2(d, &items[i]);
        pCopyBufferToImageInfo = items;
    } else {
        pCopyBufferToImageInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyBufferToImage2");
        return;
    }
    if (!c->fn->vkCmdCopyBufferToImage2) {
        mvk_unsupported(c, "vkCmdCopyBufferToImage2");
        return;
    }
    c->fn->vkCmdCopyBufferToImage2(commandBuffer, pCopyBufferToImageInfo);
}

static void run_vkCmdCopyImageToBuffer2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyImageToBufferInfo2 * pCopyImageToBufferInfo;
    if (mvk_get_u32(d)) {
        VkCopyImageToBufferInfo2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyImageToBufferInfo2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyImageToBufferInfo2(d, &items[i]);
        pCopyImageToBufferInfo = items;
    } else {
        pCopyImageToBufferInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyImageToBuffer2");
        return;
    }
    if (!c->fn->vkCmdCopyImageToBuffer2) {
        mvk_unsupported(c, "vkCmdCopyImageToBuffer2");
        return;
    }
    c->fn->vkCmdCopyImageToBuffer2(commandBuffer, pCopyImageToBufferInfo);
}

static void run_vkGetDeviceBufferMemoryRequirements(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeviceBufferMemoryRequirements * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkDeviceBufferMemoryRequirements *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDeviceBufferMemoryRequirements));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDeviceBufferMemoryRequirements(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    VkMemoryRequirements2 *pMemoryRequirements = mvk_get_out_shape(d, sizeof(VkMemoryRequirements2));
    if (d->error) {
        mvk_malformed(c, "vkGetDeviceBufferMemoryRequirements");
        return;
    }
    if (!c->fn->vkGetDeviceBufferMemoryRequirements) {
        mvk_unsupported(c, "vkGetDeviceBufferMemoryRequirements");
        return;
    }
    c->fn->vkGetDeviceBufferMemoryRequirements(device, pInfo, pMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkMemoryRequirements2(e, pMemoryRequirements);
    mvk_reply_end(c);
}

static void run_vkGetDeviceImageMemoryRequirements(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeviceImageMemoryRequirements * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkDeviceImageMemoryRequirements *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDeviceImageMemoryRequirements));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDeviceImageMemoryRequirements(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    VkMemoryRequirements2 *pMemoryRequirements = mvk_get_out_shape(d, sizeof(VkMemoryRequirements2));
    if (d->error) {
        mvk_malformed(c, "vkGetDeviceImageMemoryRequirements");
        return;
    }
    if (!c->fn->vkGetDeviceImageMemoryRequirements) {
        mvk_unsupported(c, "vkGetDeviceImageMemoryRequirements");
        return;
    }
    c->fn->vkGetDeviceImageMemoryRequirements(device, pInfo, pMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkMemoryRequirements2(e, pMemoryRequirements);
    mvk_reply_end(c);
}

static void run_vkGetDeviceImageSparseMemoryRequirements(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeviceImageMemoryRequirements * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkDeviceImageMemoryRequirements *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDeviceImageMemoryRequirements));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDeviceImageMemoryRequirements(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    uint32_t pSparseMemoryRequirementCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pSparseMemoryRequirementCount = &pSparseMemoryRequirementCount_value;
    int want_pSparseMemoryRequirements = mvk_get_u32(d);
    VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements = NULL;
    uint32_t shape_count_pSparseMemoryRequirements = 0;
    if (want_pSparseMemoryRequirements) {
        pSparseMemoryRequirements = mvk_alloc(d, sizeof(VkSparseImageMemoryRequirements2) * (size_t)(*pSparseMemoryRequirementCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pSparseMemoryRequirementCount); i++)
            mvk_get_out_shape_into(d, &pSparseMemoryRequirements[i], sizeof(VkSparseImageMemoryRequirements2));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetDeviceImageSparseMemoryRequirements");
        return;
    }
    if (!c->fn->vkGetDeviceImageSparseMemoryRequirements) {
        mvk_unsupported(c, "vkGetDeviceImageSparseMemoryRequirements");
        return;
    }
    c->fn->vkGetDeviceImageSparseMemoryRequirements(device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(*pSparseMemoryRequirementCount));
    mvk_put_u32(e, pSparseMemoryRequirements != NULL);
    if (pSparseMemoryRequirements) {
        mvk_put_u32(e, (uint32_t)(*pSparseMemoryRequirementCount));
        for (uint32_t i = 0; i < (uint32_t)(*pSparseMemoryRequirementCount); i++)
            reply_VkSparseImageMemoryRequirements2(e, &pSparseMemoryRequirements[i]);
    }
    mvk_reply_end(c);
}

static void run_vkCmdSetEvent2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkEvent event;
    VkDependencyInfo * pDependencyInfo;
    event = (VkEvent)mvk_get_handle(d, 0);
    if (mvk_get_u32(d)) {
        VkDependencyInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDependencyInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDependencyInfo(d, &items[i]);
        pDependencyInfo = items;
    } else {
        pDependencyInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetEvent2");
        return;
    }
    if (!c->fn->vkCmdSetEvent2) {
        mvk_unsupported(c, "vkCmdSetEvent2");
        return;
    }
    c->fn->vkCmdSetEvent2(commandBuffer, event, pDependencyInfo);
}

static void run_vkCmdResetEvent2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkEvent event;
    VkPipelineStageFlags2 stageMask;
    event = (VkEvent)mvk_get_handle(d, 0);
    stageMask = (VkPipelineStageFlags2)mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdResetEvent2");
        return;
    }
    if (!c->fn->vkCmdResetEvent2) {
        mvk_unsupported(c, "vkCmdResetEvent2");
        return;
    }
    c->fn->vkCmdResetEvent2(commandBuffer, event, stageMask);
}

static void run_vkCmdWaitEvents2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t eventCount;
    VkEvent * pEvents;
    VkDependencyInfo * pDependencyInfos;
    eventCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkEvent *items = mvk_alloc(d, (size_t)(eventCount) * sizeof(VkEvent));
        for (uint32_t i = 0; i < (uint32_t)(eventCount); i++)
            items[i] = (VkEvent)mvk_get_handle(d, 0);
        pEvents = items;
    } else {
        pEvents = NULL;
    }
    if (mvk_get_u32(d)) {
        VkDependencyInfo *items = mvk_alloc(d, (size_t)(eventCount) * sizeof(VkDependencyInfo));
        for (uint32_t i = 0; i < (uint32_t)(eventCount); i++)
            mvk_get_VkDependencyInfo(d, &items[i]);
        pDependencyInfos = items;
    } else {
        pDependencyInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdWaitEvents2");
        return;
    }
    if (!c->fn->vkCmdWaitEvents2) {
        mvk_unsupported(c, "vkCmdWaitEvents2");
        return;
    }
    c->fn->vkCmdWaitEvents2(commandBuffer, eventCount, pEvents, pDependencyInfos);
}

static void run_vkCmdBlitImage2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBlitImageInfo2 * pBlitImageInfo;
    if (mvk_get_u32(d)) {
        VkBlitImageInfo2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBlitImageInfo2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBlitImageInfo2(d, &items[i]);
        pBlitImageInfo = items;
    } else {
        pBlitImageInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBlitImage2");
        return;
    }
    if (!c->fn->vkCmdBlitImage2) {
        mvk_unsupported(c, "vkCmdBlitImage2");
        return;
    }
    c->fn->vkCmdBlitImage2(commandBuffer, pBlitImageInfo);
}

static void run_vkCmdResolveImage2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkResolveImageInfo2 * pResolveImageInfo;
    if (mvk_get_u32(d)) {
        VkResolveImageInfo2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkResolveImageInfo2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkResolveImageInfo2(d, &items[i]);
        pResolveImageInfo = items;
    } else {
        pResolveImageInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdResolveImage2");
        return;
    }
    if (!c->fn->vkCmdResolveImage2) {
        mvk_unsupported(c, "vkCmdResolveImage2");
        return;
    }
    c->fn->vkCmdResolveImage2(commandBuffer, pResolveImageInfo);
}

static void run_vkCmdBeginRendering(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkRenderingInfo * pRenderingInfo;
    if (mvk_get_u32(d)) {
        VkRenderingInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkRenderingInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkRenderingInfo(d, &items[i]);
        pRenderingInfo = items;
    } else {
        pRenderingInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBeginRendering");
        return;
    }
    if (!c->fn->vkCmdBeginRendering) {
        mvk_unsupported(c, "vkCmdBeginRendering");
        return;
    }
    c->fn->vkCmdBeginRendering(commandBuffer, pRenderingInfo);
}

static void run_vkCmdEndRendering(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    if (d->error) {
        mvk_malformed(c, "vkCmdEndRendering");
        return;
    }
    if (!c->fn->vkCmdEndRendering) {
        mvk_unsupported(c, "vkCmdEndRendering");
        return;
    }
    c->fn->vkCmdEndRendering(commandBuffer);
}

static void run_vkCmdSetCullMode(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCullModeFlags cullMode;
    cullMode = (VkCullModeFlags)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetCullMode");
        return;
    }
    if (!c->fn->vkCmdSetCullMode) {
        mvk_unsupported(c, "vkCmdSetCullMode");
        return;
    }
    c->fn->vkCmdSetCullMode(commandBuffer, cullMode);
}

static void run_vkCmdSetFrontFace(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkFrontFace frontFace;
    frontFace = (VkFrontFace)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetFrontFace");
        return;
    }
    if (!c->fn->vkCmdSetFrontFace) {
        mvk_unsupported(c, "vkCmdSetFrontFace");
        return;
    }
    c->fn->vkCmdSetFrontFace(commandBuffer, frontFace);
}

static void run_vkCmdSetPrimitiveTopology(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPrimitiveTopology primitiveTopology;
    primitiveTopology = (VkPrimitiveTopology)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetPrimitiveTopology");
        return;
    }
    if (!c->fn->vkCmdSetPrimitiveTopology) {
        mvk_unsupported(c, "vkCmdSetPrimitiveTopology");
        return;
    }
    c->fn->vkCmdSetPrimitiveTopology(commandBuffer, primitiveTopology);
}

static void run_vkCmdSetViewportWithCount(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t viewportCount;
    VkViewport * pViewports;
    viewportCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkViewport *items = mvk_alloc(d, (size_t)(viewportCount) * sizeof(VkViewport));
        for (uint32_t i = 0; i < (uint32_t)(viewportCount); i++)
            mvk_get_VkViewport(d, &items[i]);
        pViewports = items;
    } else {
        pViewports = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetViewportWithCount");
        return;
    }
    if (!c->fn->vkCmdSetViewportWithCount) {
        mvk_unsupported(c, "vkCmdSetViewportWithCount");
        return;
    }
    c->fn->vkCmdSetViewportWithCount(commandBuffer, viewportCount, pViewports);
}

static void run_vkCmdSetScissorWithCount(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t scissorCount;
    VkRect2D * pScissors;
    scissorCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkRect2D *items = mvk_alloc(d, (size_t)(scissorCount) * sizeof(VkRect2D));
        for (uint32_t i = 0; i < (uint32_t)(scissorCount); i++)
            mvk_get_VkRect2D(d, &items[i]);
        pScissors = items;
    } else {
        pScissors = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetScissorWithCount");
        return;
    }
    if (!c->fn->vkCmdSetScissorWithCount) {
        mvk_unsupported(c, "vkCmdSetScissorWithCount");
        return;
    }
    c->fn->vkCmdSetScissorWithCount(commandBuffer, scissorCount, pScissors);
}

static void run_vkCmdBindVertexBuffers2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstBinding;
    uint32_t bindingCount;
    VkBuffer * pBuffers;
    VkDeviceSize * pOffsets;
    VkDeviceSize * pSizes;
    VkDeviceSize * pStrides;
    firstBinding = (uint32_t)mvk_get_u32(d);
    bindingCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBuffer *items = mvk_alloc(d, (size_t)(bindingCount) * sizeof(VkBuffer));
        for (uint32_t i = 0; i < (uint32_t)(bindingCount); i++)
            items[i] = (VkBuffer)mvk_get_handle(d, 0);
        pBuffers = items;
    } else {
        pBuffers = NULL;
    }
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(bindingCount) * sizeof(VkDeviceSize));
        mvk_get_bytes(d, items, (size_t)(bindingCount) * sizeof(VkDeviceSize));
        pOffsets = items;
    } else {
        pOffsets = NULL;
    }
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(bindingCount) * sizeof(VkDeviceSize));
        mvk_get_bytes(d, items, (size_t)(bindingCount) * sizeof(VkDeviceSize));
        pSizes = items;
    } else {
        pSizes = NULL;
    }
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(bindingCount) * sizeof(VkDeviceSize));
        mvk_get_bytes(d, items, (size_t)(bindingCount) * sizeof(VkDeviceSize));
        pStrides = items;
    } else {
        pStrides = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindVertexBuffers2");
        return;
    }
    if (!c->fn->vkCmdBindVertexBuffers2) {
        mvk_unsupported(c, "vkCmdBindVertexBuffers2");
        return;
    }
    c->fn->vkCmdBindVertexBuffers2(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes, pStrides);
}

static void run_vkCmdSetDepthTestEnable(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 depthTestEnable;
    depthTestEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDepthTestEnable");
        return;
    }
    if (!c->fn->vkCmdSetDepthTestEnable) {
        mvk_unsupported(c, "vkCmdSetDepthTestEnable");
        return;
    }
    c->fn->vkCmdSetDepthTestEnable(commandBuffer, depthTestEnable);
}

static void run_vkCmdSetDepthWriteEnable(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 depthWriteEnable;
    depthWriteEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDepthWriteEnable");
        return;
    }
    if (!c->fn->vkCmdSetDepthWriteEnable) {
        mvk_unsupported(c, "vkCmdSetDepthWriteEnable");
        return;
    }
    c->fn->vkCmdSetDepthWriteEnable(commandBuffer, depthWriteEnable);
}

static void run_vkCmdSetDepthCompareOp(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCompareOp depthCompareOp;
    depthCompareOp = (VkCompareOp)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDepthCompareOp");
        return;
    }
    if (!c->fn->vkCmdSetDepthCompareOp) {
        mvk_unsupported(c, "vkCmdSetDepthCompareOp");
        return;
    }
    c->fn->vkCmdSetDepthCompareOp(commandBuffer, depthCompareOp);
}

static void run_vkCmdSetDepthBoundsTestEnable(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 depthBoundsTestEnable;
    depthBoundsTestEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDepthBoundsTestEnable");
        return;
    }
    if (!c->fn->vkCmdSetDepthBoundsTestEnable) {
        mvk_unsupported(c, "vkCmdSetDepthBoundsTestEnable");
        return;
    }
    c->fn->vkCmdSetDepthBoundsTestEnable(commandBuffer, depthBoundsTestEnable);
}

static void run_vkCmdSetStencilTestEnable(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 stencilTestEnable;
    stencilTestEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetStencilTestEnable");
        return;
    }
    if (!c->fn->vkCmdSetStencilTestEnable) {
        mvk_unsupported(c, "vkCmdSetStencilTestEnable");
        return;
    }
    c->fn->vkCmdSetStencilTestEnable(commandBuffer, stencilTestEnable);
}

static void run_vkCmdSetStencilOp(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkStencilFaceFlags faceMask;
    VkStencilOp failOp;
    VkStencilOp passOp;
    VkStencilOp depthFailOp;
    VkCompareOp compareOp;
    faceMask = (VkStencilFaceFlags)mvk_get_u32(d);
    failOp = (VkStencilOp)mvk_get_u32(d);
    passOp = (VkStencilOp)mvk_get_u32(d);
    depthFailOp = (VkStencilOp)mvk_get_u32(d);
    compareOp = (VkCompareOp)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetStencilOp");
        return;
    }
    if (!c->fn->vkCmdSetStencilOp) {
        mvk_unsupported(c, "vkCmdSetStencilOp");
        return;
    }
    c->fn->vkCmdSetStencilOp(commandBuffer, faceMask, failOp, passOp, depthFailOp, compareOp);
}

static void run_vkCmdSetRasterizerDiscardEnable(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 rasterizerDiscardEnable;
    rasterizerDiscardEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetRasterizerDiscardEnable");
        return;
    }
    if (!c->fn->vkCmdSetRasterizerDiscardEnable) {
        mvk_unsupported(c, "vkCmdSetRasterizerDiscardEnable");
        return;
    }
    c->fn->vkCmdSetRasterizerDiscardEnable(commandBuffer, rasterizerDiscardEnable);
}

static void run_vkCmdSetDepthBiasEnable(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 depthBiasEnable;
    depthBiasEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDepthBiasEnable");
        return;
    }
    if (!c->fn->vkCmdSetDepthBiasEnable) {
        mvk_unsupported(c, "vkCmdSetDepthBiasEnable");
        return;
    }
    c->fn->vkCmdSetDepthBiasEnable(commandBuffer, depthBiasEnable);
}

static void run_vkCmdSetPrimitiveRestartEnable(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 primitiveRestartEnable;
    primitiveRestartEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetPrimitiveRestartEnable");
        return;
    }
    if (!c->fn->vkCmdSetPrimitiveRestartEnable) {
        mvk_unsupported(c, "vkCmdSetPrimitiveRestartEnable");
        return;
    }
    c->fn->vkCmdSetPrimitiveRestartEnable(commandBuffer, primitiveRestartEnable);
}

static void run_vkGetDeviceImageSubresourceLayout(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeviceImageSubresourceInfo * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkDeviceImageSubresourceInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDeviceImageSubresourceInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDeviceImageSubresourceInfo(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    VkSubresourceLayout2 *pLayout = mvk_get_out_shape(d, sizeof(VkSubresourceLayout2));
    if (d->error) {
        mvk_malformed(c, "vkGetDeviceImageSubresourceLayout");
        return;
    }
    if (!c->fn->vkGetDeviceImageSubresourceLayout) {
        mvk_unsupported(c, "vkGetDeviceImageSubresourceLayout");
        return;
    }
    c->fn->vkGetDeviceImageSubresourceLayout(device, pInfo, pLayout);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkSubresourceLayout2(e, pLayout);
    mvk_reply_end(c);
}

static void run_vkGetImageSubresourceLayout2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImage image;
    VkImageSubresource2 * pSubresource;
    device = (VkDevice)mvk_get_handle(d, 1);
    image = (VkImage)mvk_get_handle(d, 0);
    if (mvk_get_u32(d)) {
        VkImageSubresource2 *items = mvk_alloc(d, (size_t)(1) * sizeof(VkImageSubresource2));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkImageSubresource2(d, &items[i]);
        pSubresource = items;
    } else {
        pSubresource = NULL;
    }
    VkSubresourceLayout2 *pLayout = mvk_get_out_shape(d, sizeof(VkSubresourceLayout2));
    if (d->error) {
        mvk_malformed(c, "vkGetImageSubresourceLayout2");
        return;
    }
    if (!c->fn->vkGetImageSubresourceLayout2) {
        mvk_unsupported(c, "vkGetImageSubresourceLayout2");
        return;
    }
    c->fn->vkGetImageSubresourceLayout2(device, image, pSubresource, pLayout);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkSubresourceLayout2(e, pLayout);
    mvk_reply_end(c);
}

static void run_vkCopyImageToImage(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkCopyImageToImageInfo * pCopyImageToImageInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkCopyImageToImageInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyImageToImageInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyImageToImageInfo(d, &items[i]);
        pCopyImageToImageInfo = items;
    } else {
        pCopyImageToImageInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCopyImageToImage");
        return;
    }
    if (!c->fn->vkCopyImageToImage) {
        mvk_unsupported(c, "vkCopyImageToImage");
        return;
    }
    VkResult result = c->fn->vkCopyImageToImage(device, pCopyImageToImageInfo);
    mvk_check(c, result, "vkCopyImageToImage");
}

static void run_vkTransitionImageLayout(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t transitionCount;
    VkHostImageLayoutTransitionInfo * pTransitions;
    device = (VkDevice)mvk_get_handle(d, 1);
    transitionCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkHostImageLayoutTransitionInfo *items = mvk_alloc(d, (size_t)(transitionCount) * sizeof(VkHostImageLayoutTransitionInfo));
        for (uint32_t i = 0; i < (uint32_t)(transitionCount); i++)
            mvk_get_VkHostImageLayoutTransitionInfo(d, &items[i]);
        pTransitions = items;
    } else {
        pTransitions = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkTransitionImageLayout");
        return;
    }
    if (!c->fn->vkTransitionImageLayout) {
        mvk_unsupported(c, "vkTransitionImageLayout");
        return;
    }
    VkResult result = c->fn->vkTransitionImageLayout(device, transitionCount, pTransitions);
    mvk_check(c, result, "vkTransitionImageLayout");
}

static void run_vkCmdPushDescriptorSet(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineBindPoint pipelineBindPoint;
    VkPipelineLayout layout;
    uint32_t set;
    uint32_t descriptorWriteCount;
    VkWriteDescriptorSet * pDescriptorWrites;
    pipelineBindPoint = (VkPipelineBindPoint)mvk_get_u32(d);
    layout = (VkPipelineLayout)mvk_get_handle(d, 0);
    set = (uint32_t)mvk_get_u32(d);
    descriptorWriteCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkWriteDescriptorSet *items = mvk_alloc(d, (size_t)(descriptorWriteCount) * sizeof(VkWriteDescriptorSet));
        for (uint32_t i = 0; i < (uint32_t)(descriptorWriteCount); i++)
            mvk_get_VkWriteDescriptorSet(d, &items[i]);
        pDescriptorWrites = items;
    } else {
        pDescriptorWrites = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdPushDescriptorSet");
        return;
    }
    if (!c->fn->vkCmdPushDescriptorSet) {
        mvk_unsupported(c, "vkCmdPushDescriptorSet");
        return;
    }
    c->fn->vkCmdPushDescriptorSet(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
}

static void run_vkCmdBindDescriptorSets2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBindDescriptorSetsInfo * pBindDescriptorSetsInfo;
    if (mvk_get_u32(d)) {
        VkBindDescriptorSetsInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBindDescriptorSetsInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBindDescriptorSetsInfo(d, &items[i]);
        pBindDescriptorSetsInfo = items;
    } else {
        pBindDescriptorSetsInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindDescriptorSets2");
        return;
    }
    if (!c->fn->vkCmdBindDescriptorSets2) {
        mvk_unsupported(c, "vkCmdBindDescriptorSets2");
        return;
    }
    c->fn->vkCmdBindDescriptorSets2(commandBuffer, pBindDescriptorSetsInfo);
}

static void run_vkCmdPushConstants2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPushConstantsInfo * pPushConstantsInfo;
    if (mvk_get_u32(d)) {
        VkPushConstantsInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPushConstantsInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPushConstantsInfo(d, &items[i]);
        pPushConstantsInfo = items;
    } else {
        pPushConstantsInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdPushConstants2");
        return;
    }
    if (!c->fn->vkCmdPushConstants2) {
        mvk_unsupported(c, "vkCmdPushConstants2");
        return;
    }
    c->fn->vkCmdPushConstants2(commandBuffer, pPushConstantsInfo);
}

static void run_vkCmdPushDescriptorSet2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPushDescriptorSetInfo * pPushDescriptorSetInfo;
    if (mvk_get_u32(d)) {
        VkPushDescriptorSetInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPushDescriptorSetInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPushDescriptorSetInfo(d, &items[i]);
        pPushDescriptorSetInfo = items;
    } else {
        pPushDescriptorSetInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdPushDescriptorSet2");
        return;
    }
    if (!c->fn->vkCmdPushDescriptorSet2) {
        mvk_unsupported(c, "vkCmdPushDescriptorSet2");
        return;
    }
    c->fn->vkCmdPushDescriptorSet2(commandBuffer, pPushDescriptorSetInfo);
}

static void run_vkCmdSetLineStipple(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t lineStippleFactor;
    uint16_t lineStipplePattern;
    lineStippleFactor = (uint32_t)mvk_get_u32(d);
    lineStipplePattern = (uint16_t)mvk_get_u16(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetLineStipple");
        return;
    }
    if (!c->fn->vkCmdSetLineStipple) {
        mvk_unsupported(c, "vkCmdSetLineStipple");
        return;
    }
    c->fn->vkCmdSetLineStipple(commandBuffer, lineStippleFactor, lineStipplePattern);
}

static void run_vkCmdBindIndexBuffer2(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize size;
    VkIndexType indexType;
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    offset = (VkDeviceSize)mvk_get_u64(d);
    size = (VkDeviceSize)mvk_get_u64(d);
    indexType = (VkIndexType)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdBindIndexBuffer2");
        return;
    }
    if (!c->fn->vkCmdBindIndexBuffer2) {
        mvk_unsupported(c, "vkCmdBindIndexBuffer2");
        return;
    }
    c->fn->vkCmdBindIndexBuffer2(commandBuffer, buffer, offset, size, indexType);
}

static void run_vkGetRenderingAreaGranularity(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkRenderingAreaInfo * pRenderingAreaInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkRenderingAreaInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkRenderingAreaInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkRenderingAreaInfo(d, &items[i]);
        pRenderingAreaInfo = items;
    } else {
        pRenderingAreaInfo = NULL;
    }
    VkExtent2D pGranularity_value;
    memset(&pGranularity_value, 0, sizeof(pGranularity_value));
    VkExtent2D *pGranularity = &pGranularity_value;
    if (d->error) {
        mvk_malformed(c, "vkGetRenderingAreaGranularity");
        return;
    }
    if (!c->fn->vkGetRenderingAreaGranularity) {
        mvk_unsupported(c, "vkGetRenderingAreaGranularity");
        return;
    }
    c->fn->vkGetRenderingAreaGranularity(device, pRenderingAreaInfo, pGranularity);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkExtent2D(e, pGranularity);
    mvk_reply_end(c);
}

static void run_vkCmdSetRenderingAttachmentLocations(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkRenderingAttachmentLocationInfo * pLocationInfo;
    if (mvk_get_u32(d)) {
        VkRenderingAttachmentLocationInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkRenderingAttachmentLocationInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkRenderingAttachmentLocationInfo(d, &items[i]);
        pLocationInfo = items;
    } else {
        pLocationInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetRenderingAttachmentLocations");
        return;
    }
    if (!c->fn->vkCmdSetRenderingAttachmentLocations) {
        mvk_unsupported(c, "vkCmdSetRenderingAttachmentLocations");
        return;
    }
    c->fn->vkCmdSetRenderingAttachmentLocations(commandBuffer, pLocationInfo);
}

static void run_vkCmdSetRenderingInputAttachmentIndices(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkRenderingInputAttachmentIndexInfo * pInputAttachmentIndexInfo;
    if (mvk_get_u32(d)) {
        VkRenderingInputAttachmentIndexInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkRenderingInputAttachmentIndexInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkRenderingInputAttachmentIndexInfo(d, &items[i]);
        pInputAttachmentIndexInfo = items;
    } else {
        pInputAttachmentIndexInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetRenderingInputAttachmentIndices");
        return;
    }
    if (!c->fn->vkCmdSetRenderingInputAttachmentIndices) {
        mvk_unsupported(c, "vkCmdSetRenderingInputAttachmentIndices");
        return;
    }
    c->fn->vkCmdSetRenderingInputAttachmentIndices(commandBuffer, pInputAttachmentIndexInfo);
}

static void run_vkCmdBindTransformFeedbackBuffersEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstBinding;
    uint32_t bindingCount;
    VkBuffer * pBuffers;
    VkDeviceSize * pOffsets;
    VkDeviceSize * pSizes;
    firstBinding = (uint32_t)mvk_get_u32(d);
    bindingCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBuffer *items = mvk_alloc(d, (size_t)(bindingCount) * sizeof(VkBuffer));
        for (uint32_t i = 0; i < (uint32_t)(bindingCount); i++)
            items[i] = (VkBuffer)mvk_get_handle(d, 0);
        pBuffers = items;
    } else {
        pBuffers = NULL;
    }
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(bindingCount) * sizeof(VkDeviceSize));
        mvk_get_bytes(d, items, (size_t)(bindingCount) * sizeof(VkDeviceSize));
        pOffsets = items;
    } else {
        pOffsets = NULL;
    }
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(bindingCount) * sizeof(VkDeviceSize));
        mvk_get_bytes(d, items, (size_t)(bindingCount) * sizeof(VkDeviceSize));
        pSizes = items;
    } else {
        pSizes = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindTransformFeedbackBuffersEXT");
        return;
    }
    if (!c->fn->vkCmdBindTransformFeedbackBuffersEXT) {
        mvk_unsupported(c, "vkCmdBindTransformFeedbackBuffersEXT");
        return;
    }
    c->fn->vkCmdBindTransformFeedbackBuffersEXT(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes);
}

static void run_vkCmdBeginTransformFeedbackEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstCounterBuffer;
    uint32_t counterBufferCount;
    VkBuffer * pCounterBuffers;
    VkDeviceSize * pCounterBufferOffsets;
    firstCounterBuffer = (uint32_t)mvk_get_u32(d);
    counterBufferCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBuffer *items = mvk_alloc(d, (size_t)(counterBufferCount) * sizeof(VkBuffer));
        for (uint32_t i = 0; i < (uint32_t)(counterBufferCount); i++)
            items[i] = (VkBuffer)mvk_get_handle(d, 0);
        pCounterBuffers = items;
    } else {
        pCounterBuffers = NULL;
    }
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(counterBufferCount) * sizeof(VkDeviceSize));
        mvk_get_bytes(d, items, (size_t)(counterBufferCount) * sizeof(VkDeviceSize));
        pCounterBufferOffsets = items;
    } else {
        pCounterBufferOffsets = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBeginTransformFeedbackEXT");
        return;
    }
    if (!c->fn->vkCmdBeginTransformFeedbackEXT) {
        mvk_unsupported(c, "vkCmdBeginTransformFeedbackEXT");
        return;
    }
    c->fn->vkCmdBeginTransformFeedbackEXT(commandBuffer, firstCounterBuffer, counterBufferCount, pCounterBuffers, pCounterBufferOffsets);
}

static void run_vkCmdEndTransformFeedbackEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstCounterBuffer;
    uint32_t counterBufferCount;
    VkBuffer * pCounterBuffers;
    VkDeviceSize * pCounterBufferOffsets;
    firstCounterBuffer = (uint32_t)mvk_get_u32(d);
    counterBufferCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBuffer *items = mvk_alloc(d, (size_t)(counterBufferCount) * sizeof(VkBuffer));
        for (uint32_t i = 0; i < (uint32_t)(counterBufferCount); i++)
            items[i] = (VkBuffer)mvk_get_handle(d, 0);
        pCounterBuffers = items;
    } else {
        pCounterBuffers = NULL;
    }
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(counterBufferCount) * sizeof(VkDeviceSize));
        mvk_get_bytes(d, items, (size_t)(counterBufferCount) * sizeof(VkDeviceSize));
        pCounterBufferOffsets = items;
    } else {
        pCounterBufferOffsets = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdEndTransformFeedbackEXT");
        return;
    }
    if (!c->fn->vkCmdEndTransformFeedbackEXT) {
        mvk_unsupported(c, "vkCmdEndTransformFeedbackEXT");
        return;
    }
    c->fn->vkCmdEndTransformFeedbackEXT(commandBuffer, firstCounterBuffer, counterBufferCount, pCounterBuffers, pCounterBufferOffsets);
}

static void run_vkCmdBeginQueryIndexedEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkQueryPool queryPool;
    uint32_t query;
    VkQueryControlFlags flags;
    uint32_t index;
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    query = (uint32_t)mvk_get_u32(d);
    flags = (VkQueryControlFlags)mvk_get_u32(d);
    index = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdBeginQueryIndexedEXT");
        return;
    }
    if (!c->fn->vkCmdBeginQueryIndexedEXT) {
        mvk_unsupported(c, "vkCmdBeginQueryIndexedEXT");
        return;
    }
    c->fn->vkCmdBeginQueryIndexedEXT(commandBuffer, queryPool, query, flags, index);
}

static void run_vkCmdEndQueryIndexedEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkQueryPool queryPool;
    uint32_t query;
    uint32_t index;
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    query = (uint32_t)mvk_get_u32(d);
    index = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdEndQueryIndexedEXT");
        return;
    }
    if (!c->fn->vkCmdEndQueryIndexedEXT) {
        mvk_unsupported(c, "vkCmdEndQueryIndexedEXT");
        return;
    }
    c->fn->vkCmdEndQueryIndexedEXT(commandBuffer, queryPool, query, index);
}

static void run_vkCmdDrawIndirectByteCountEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t instanceCount;
    uint32_t firstInstance;
    VkBuffer counterBuffer;
    VkDeviceSize counterBufferOffset;
    uint32_t counterOffset;
    uint32_t vertexStride;
    instanceCount = (uint32_t)mvk_get_u32(d);
    firstInstance = (uint32_t)mvk_get_u32(d);
    counterBuffer = (VkBuffer)mvk_get_handle(d, 0);
    counterBufferOffset = (VkDeviceSize)mvk_get_u64(d);
    counterOffset = (uint32_t)mvk_get_u32(d);
    vertexStride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawIndirectByteCountEXT");
        return;
    }
    if (!c->fn->vkCmdDrawIndirectByteCountEXT) {
        mvk_unsupported(c, "vkCmdDrawIndirectByteCountEXT");
        return;
    }
    c->fn->vkCmdDrawIndirectByteCountEXT(commandBuffer, instanceCount, firstInstance, counterBuffer, counterBufferOffset, counterOffset, vertexStride);
}

static void run_vkGetImageViewHandleNVX(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImageViewHandleInfoNVX * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkImageViewHandleInfoNVX *items = mvk_alloc(d, (size_t)(1) * sizeof(VkImageViewHandleInfoNVX));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkImageViewHandleInfoNVX(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkGetImageViewHandleNVX");
        return;
    }
    if (!c->fn->vkGetImageViewHandleNVX) {
        mvk_unsupported(c, "vkGetImageViewHandleNVX");
        return;
    }
    uint32_t result = c->fn->vkGetImageViewHandleNVX(device, pInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(result));
    mvk_reply_end(c);
}

static void run_vkGetImageViewHandle64NVX(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImageViewHandleInfoNVX * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkImageViewHandleInfoNVX *items = mvk_alloc(d, (size_t)(1) * sizeof(VkImageViewHandleInfoNVX));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkImageViewHandleInfoNVX(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkGetImageViewHandle64NVX");
        return;
    }
    if (!c->fn->vkGetImageViewHandle64NVX) {
        mvk_unsupported(c, "vkGetImageViewHandle64NVX");
        return;
    }
    uint64_t result = c->fn->vkGetImageViewHandle64NVX(device, pInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u64(e, (uint64_t)(result));
    mvk_reply_end(c);
}

static void run_vkGetImageViewAddressNVX(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImageView imageView;
    device = (VkDevice)mvk_get_handle(d, 1);
    imageView = (VkImageView)mvk_get_handle(d, 0);
    VkImageViewAddressPropertiesNVX *pProperties = mvk_get_out_shape(d, sizeof(VkImageViewAddressPropertiesNVX));
    if (d->error) {
        mvk_malformed(c, "vkGetImageViewAddressNVX");
        return;
    }
    if (!c->fn->vkGetImageViewAddressNVX) {
        mvk_unsupported(c, "vkGetImageViewAddressNVX");
        return;
    }
    VkResult result = c->fn->vkGetImageViewAddressNVX(device, imageView, pProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    reply_VkImageViewAddressPropertiesNVX(e, pProperties);
    mvk_reply_end(c);
}

static void run_vkGetDeviceCombinedImageSamplerIndexNVX(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint64_t imageViewIndex;
    uint64_t samplerIndex;
    device = (VkDevice)mvk_get_handle(d, 1);
    imageViewIndex = (uint64_t)mvk_get_u64(d);
    samplerIndex = (uint64_t)mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkGetDeviceCombinedImageSamplerIndexNVX");
        return;
    }
    if (!c->fn->vkGetDeviceCombinedImageSamplerIndexNVX) {
        mvk_unsupported(c, "vkGetDeviceCombinedImageSamplerIndexNVX");
        return;
    }
    uint64_t result = c->fn->vkGetDeviceCombinedImageSamplerIndexNVX(device, imageViewIndex, samplerIndex);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u64(e, (uint64_t)(result));
    mvk_reply_end(c);
}

static void run_vkGetShaderInfoAMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipeline pipeline;
    VkShaderStageFlagBits shaderStage;
    VkShaderInfoTypeAMD infoType;
    device = (VkDevice)mvk_get_handle(d, 1);
    pipeline = (VkPipeline)mvk_get_handle(d, 0);
    shaderStage = (VkShaderStageFlagBits)mvk_get_u32(d);
    infoType = (VkShaderInfoTypeAMD)mvk_get_u32(d);
    size_t pInfoSize_value;
    memset(&pInfoSize_value, 0, sizeof(pInfoSize_value));
    size_t *pInfoSize = &pInfoSize_value;
    int want_pInfo = mvk_get_u32(d);
    uint8_t *pInfo = NULL;
    if (want_pInfo) {
        pInfo = mvk_alloc(d, sizeof(uint8_t) * (size_t)(*pInfoSize) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetShaderInfoAMD");
        return;
    }
    if (!c->fn->vkGetShaderInfoAMD) {
        mvk_unsupported(c, "vkGetShaderInfoAMD");
        return;
    }
    VkResult result = c->fn->vkGetShaderInfoAMD(device, pipeline, shaderStage, infoType, pInfoSize, pInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u64(e, (uint64_t)(*pInfoSize));
    mvk_put_u32(e, pInfo != NULL);
    if (pInfo) {
        mvk_put_u32(e, (uint32_t)(*pInfoSize));
        mvk_put_bytes(e, pInfo, (size_t)(*pInfoSize));
    }
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceExternalImageFormatPropertiesNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkFormat format;
    VkImageType type;
    VkImageTiling tiling;
    VkImageUsageFlags usage;
    VkImageCreateFlags flags;
    VkExternalMemoryHandleTypeFlagsNV externalHandleType;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    format = (VkFormat)mvk_get_u32(d);
    type = (VkImageType)mvk_get_u32(d);
    tiling = (VkImageTiling)mvk_get_u32(d);
    usage = (VkImageUsageFlags)mvk_get_u32(d);
    flags = (VkImageCreateFlags)mvk_get_u32(d);
    externalHandleType = (VkExternalMemoryHandleTypeFlagsNV)mvk_get_u32(d);
    VkExternalImageFormatPropertiesNV pExternalImageFormatProperties_value;
    memset(&pExternalImageFormatProperties_value, 0, sizeof(pExternalImageFormatProperties_value));
    VkExternalImageFormatPropertiesNV *pExternalImageFormatProperties = &pExternalImageFormatProperties_value;
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceExternalImageFormatPropertiesNV");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceExternalImageFormatPropertiesNV) {
        mvk_unsupported(c, "vkGetPhysicalDeviceExternalImageFormatPropertiesNV");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceExternalImageFormatPropertiesNV(physicalDevice, format, type, tiling, usage, flags, externalHandleType, pExternalImageFormatProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    reply_VkExternalImageFormatPropertiesNV(e, pExternalImageFormatProperties);
    mvk_reply_end(c);
}

static void run_vkCmdBeginConditionalRenderingEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkConditionalRenderingBeginInfoEXT * pConditionalRenderingBegin;
    if (mvk_get_u32(d)) {
        VkConditionalRenderingBeginInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkConditionalRenderingBeginInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkConditionalRenderingBeginInfoEXT(d, &items[i]);
        pConditionalRenderingBegin = items;
    } else {
        pConditionalRenderingBegin = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBeginConditionalRenderingEXT");
        return;
    }
    if (!c->fn->vkCmdBeginConditionalRenderingEXT) {
        mvk_unsupported(c, "vkCmdBeginConditionalRenderingEXT");
        return;
    }
    c->fn->vkCmdBeginConditionalRenderingEXT(commandBuffer, pConditionalRenderingBegin);
}

static void run_vkCmdEndConditionalRenderingEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    if (d->error) {
        mvk_malformed(c, "vkCmdEndConditionalRenderingEXT");
        return;
    }
    if (!c->fn->vkCmdEndConditionalRenderingEXT) {
        mvk_unsupported(c, "vkCmdEndConditionalRenderingEXT");
        return;
    }
    c->fn->vkCmdEndConditionalRenderingEXT(commandBuffer);
}

static void run_vkCmdSetViewportWScalingNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstViewport;
    uint32_t viewportCount;
    VkViewportWScalingNV * pViewportWScalings;
    firstViewport = (uint32_t)mvk_get_u32(d);
    viewportCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkViewportWScalingNV *items = mvk_alloc(d, (size_t)(viewportCount) * sizeof(VkViewportWScalingNV));
        for (uint32_t i = 0; i < (uint32_t)(viewportCount); i++)
            mvk_get_VkViewportWScalingNV(d, &items[i]);
        pViewportWScalings = items;
    } else {
        pViewportWScalings = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetViewportWScalingNV");
        return;
    }
    if (!c->fn->vkCmdSetViewportWScalingNV) {
        mvk_unsupported(c, "vkCmdSetViewportWScalingNV");
        return;
    }
    c->fn->vkCmdSetViewportWScalingNV(commandBuffer, firstViewport, viewportCount, pViewportWScalings);
}

static void run_vkCmdSetDiscardRectangleEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstDiscardRectangle;
    uint32_t discardRectangleCount;
    VkRect2D * pDiscardRectangles;
    firstDiscardRectangle = (uint32_t)mvk_get_u32(d);
    discardRectangleCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkRect2D *items = mvk_alloc(d, (size_t)(discardRectangleCount) * sizeof(VkRect2D));
        for (uint32_t i = 0; i < (uint32_t)(discardRectangleCount); i++)
            mvk_get_VkRect2D(d, &items[i]);
        pDiscardRectangles = items;
    } else {
        pDiscardRectangles = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDiscardRectangleEXT");
        return;
    }
    if (!c->fn->vkCmdSetDiscardRectangleEXT) {
        mvk_unsupported(c, "vkCmdSetDiscardRectangleEXT");
        return;
    }
    c->fn->vkCmdSetDiscardRectangleEXT(commandBuffer, firstDiscardRectangle, discardRectangleCount, pDiscardRectangles);
}

static void run_vkCmdSetDiscardRectangleEnableEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 discardRectangleEnable;
    discardRectangleEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDiscardRectangleEnableEXT");
        return;
    }
    if (!c->fn->vkCmdSetDiscardRectangleEnableEXT) {
        mvk_unsupported(c, "vkCmdSetDiscardRectangleEnableEXT");
        return;
    }
    c->fn->vkCmdSetDiscardRectangleEnableEXT(commandBuffer, discardRectangleEnable);
}

static void run_vkCmdSetDiscardRectangleModeEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDiscardRectangleModeEXT discardRectangleMode;
    discardRectangleMode = (VkDiscardRectangleModeEXT)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDiscardRectangleModeEXT");
        return;
    }
    if (!c->fn->vkCmdSetDiscardRectangleModeEXT) {
        mvk_unsupported(c, "vkCmdSetDiscardRectangleModeEXT");
        return;
    }
    c->fn->vkCmdSetDiscardRectangleModeEXT(commandBuffer, discardRectangleMode);
}

static void run_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    uint32_t queueFamilyIndex;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    queueFamilyIndex = (uint32_t)mvk_get_u32(d);
    uint32_t pCounterCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pCounterCount = &pCounterCount_value;
    int want_pCounters = mvk_get_u32(d);
    VkPerformanceCounterKHR *pCounters = NULL;
    uint32_t shape_count_pCounters = 0;
    int want_pCounterDescriptions = mvk_get_u32(d);
    VkPerformanceCounterDescriptionKHR *pCounterDescriptions = NULL;
    uint32_t shape_count_pCounterDescriptions = 0;
    if (want_pCounters) {
        pCounters = mvk_alloc(d, sizeof(VkPerformanceCounterKHR) * (size_t)(*pCounterCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pCounterCount); i++)
            mvk_get_out_shape_into(d, &pCounters[i], sizeof(VkPerformanceCounterKHR));
    }
    if (want_pCounterDescriptions) {
        pCounterDescriptions = mvk_alloc(d, sizeof(VkPerformanceCounterDescriptionKHR) * (size_t)(*pCounterCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pCounterCount); i++)
            mvk_get_out_shape_into(d, &pCounterDescriptions[i], sizeof(VkPerformanceCounterDescriptionKHR));
    }
    if (d->error) {
        mvk_malformed(c, "vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR");
        return;
    }
    if (!c->fn->vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR) {
        mvk_unsupported(c, "vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR");
        return;
    }
    VkResult result = c->fn->vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(physicalDevice, queueFamilyIndex, pCounterCount, pCounters, pCounterDescriptions);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pCounterCount));
    mvk_put_u32(e, pCounters != NULL);
    if (pCounters) {
        mvk_put_u32(e, (uint32_t)(*pCounterCount));
        for (uint32_t i = 0; i < (uint32_t)(*pCounterCount); i++)
            reply_VkPerformanceCounterKHR(e, &pCounters[i]);
    }
    mvk_put_u32(e, pCounterDescriptions != NULL);
    if (pCounterDescriptions) {
        mvk_put_u32(e, (uint32_t)(*pCounterCount));
        for (uint32_t i = 0; i < (uint32_t)(*pCounterCount); i++)
            reply_VkPerformanceCounterDescriptionKHR(e, &pCounterDescriptions[i]);
    }
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkQueryPoolPerformanceCreateInfoKHR * pPerformanceQueryCreateInfo;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkQueryPoolPerformanceCreateInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkQueryPoolPerformanceCreateInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkQueryPoolPerformanceCreateInfoKHR(d, &items[i]);
        pPerformanceQueryCreateInfo = items;
    } else {
        pPerformanceQueryCreateInfo = NULL;
    }
    uint32_t pNumPasses_value;
    memset(&pNumPasses_value, 0, sizeof(pNumPasses_value));
    uint32_t *pNumPasses = &pNumPasses_value;
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR) {
        mvk_unsupported(c, "vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR");
        return;
    }
    c->fn->vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR(physicalDevice, pPerformanceQueryCreateInfo, pNumPasses);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(*pNumPasses));
    mvk_reply_end(c);
}

static void run_vkAcquireProfilingLockKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkAcquireProfilingLockInfoKHR * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkAcquireProfilingLockInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkAcquireProfilingLockInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkAcquireProfilingLockInfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkAcquireProfilingLockKHR");
        return;
    }
    if (!c->fn->vkAcquireProfilingLockKHR) {
        mvk_unsupported(c, "vkAcquireProfilingLockKHR");
        return;
    }
    VkResult result = c->fn->vkAcquireProfilingLockKHR(device, pInfo);
    mvk_check(c, result, "vkAcquireProfilingLockKHR");
}

static void run_vkReleaseProfilingLockKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (d->error) {
        mvk_malformed(c, "vkReleaseProfilingLockKHR");
        return;
    }
    if (!c->fn->vkReleaseProfilingLockKHR) {
        mvk_unsupported(c, "vkReleaseProfilingLockKHR");
        return;
    }
    c->fn->vkReleaseProfilingLockKHR(device);
}

static void run_vkCreateGpaSessionAMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkGpaSessionCreateInfoAMD * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkGpaSessionCreateInfoAMD *items = mvk_alloc(d, (size_t)(1) * sizeof(VkGpaSessionCreateInfoAMD));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkGpaSessionCreateInfoAMD(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pGpaSession = mvk_get_u64(d);
    VkGpaSessionAMD pGpaSession_value = VK_NULL_HANDLE;
    VkGpaSessionAMD *pGpaSession = &pGpaSession_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateGpaSessionAMD");
        return;
    }
    if (!c->fn->vkCreateGpaSessionAMD) {
        mvk_unsupported(c, "vkCreateGpaSessionAMD");
        return;
    }
    VkResult result = c->fn->vkCreateGpaSessionAMD(device, pCreateInfo, NULL, pGpaSession);
    mvk_bind_id(c, id_pGpaSession, (uint64_t)pGpaSession_value);
    mvk_track(c->conn, (uint64_t)pGpaSession_value, MVK_KIND_VkGpaSessionAMD);
    mvk_check(c, result, "vkCreateGpaSessionAMD");
}

static void run_vkDestroyGpaSessionAMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkGpaSessionAMD gpaSession;
    device = (VkDevice)mvk_get_handle(d, 1);
    gpaSession = (VkGpaSessionAMD)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyGpaSessionAMD");
        return;
    }
    if (!c->fn->vkDestroyGpaSessionAMD) {
        mvk_unsupported(c, "vkDestroyGpaSessionAMD");
        return;
    }
    c->fn->vkDestroyGpaSessionAMD(device, gpaSession, NULL);
    mvk_forget(c->conn, (uint64_t)gpaSession);
}

static void run_vkSetGpaDeviceClockModeAMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    device = (VkDevice)mvk_get_handle(d, 1);
    VkGpaDeviceClockModeInfoAMD *pInfo = mvk_get_out_shape(d, sizeof(VkGpaDeviceClockModeInfoAMD));
    if (d->error) {
        mvk_malformed(c, "vkSetGpaDeviceClockModeAMD");
        return;
    }
    if (!c->fn->vkSetGpaDeviceClockModeAMD) {
        mvk_unsupported(c, "vkSetGpaDeviceClockModeAMD");
        return;
    }
    VkResult result = c->fn->vkSetGpaDeviceClockModeAMD(device, pInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    reply_VkGpaDeviceClockModeInfoAMD(e, pInfo);
    mvk_reply_end(c);
}

static void run_vkGetGpaDeviceClockInfoAMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    device = (VkDevice)mvk_get_handle(d, 1);
    VkGpaDeviceGetClockInfoAMD *pInfo = mvk_get_out_shape(d, sizeof(VkGpaDeviceGetClockInfoAMD));
    if (d->error) {
        mvk_malformed(c, "vkGetGpaDeviceClockInfoAMD");
        return;
    }
    if (!c->fn->vkGetGpaDeviceClockInfoAMD) {
        mvk_unsupported(c, "vkGetGpaDeviceClockInfoAMD");
        return;
    }
    VkResult result = c->fn->vkGetGpaDeviceClockInfoAMD(device, pInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    reply_VkGpaDeviceGetClockInfoAMD(e, pInfo);
    mvk_reply_end(c);
}

static void run_vkCmdEndGpaSampleAMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkGpaSessionAMD gpaSession;
    uint32_t sampleID;
    gpaSession = (VkGpaSessionAMD)mvk_get_handle(d, 0);
    sampleID = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdEndGpaSampleAMD");
        return;
    }
    if (!c->fn->vkCmdEndGpaSampleAMD) {
        mvk_unsupported(c, "vkCmdEndGpaSampleAMD");
        return;
    }
    c->fn->vkCmdEndGpaSampleAMD(commandBuffer, gpaSession, sampleID);
}

static void run_vkGetGpaSessionStatusAMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkGpaSessionAMD gpaSession;
    device = (VkDevice)mvk_get_handle(d, 1);
    gpaSession = (VkGpaSessionAMD)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkGetGpaSessionStatusAMD");
        return;
    }
    if (!c->fn->vkGetGpaSessionStatusAMD) {
        mvk_unsupported(c, "vkGetGpaSessionStatusAMD");
        return;
    }
    VkResult result = c->fn->vkGetGpaSessionStatusAMD(device, gpaSession);
    mvk_check(c, result, "vkGetGpaSessionStatusAMD");
}

static void run_vkGetGpaSessionResultsAMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkGpaSessionAMD gpaSession;
    uint32_t sampleID;
    device = (VkDevice)mvk_get_handle(d, 1);
    gpaSession = (VkGpaSessionAMD)mvk_get_handle(d, 0);
    sampleID = (uint32_t)mvk_get_u32(d);
    size_t pSizeInBytes_value;
    memset(&pSizeInBytes_value, 0, sizeof(pSizeInBytes_value));
    size_t *pSizeInBytes = &pSizeInBytes_value;
    int want_pData = mvk_get_u32(d);
    uint8_t *pData = NULL;
    if (want_pData) {
        pData = mvk_alloc(d, sizeof(uint8_t) * (size_t)(*pSizeInBytes) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetGpaSessionResultsAMD");
        return;
    }
    if (!c->fn->vkGetGpaSessionResultsAMD) {
        mvk_unsupported(c, "vkGetGpaSessionResultsAMD");
        return;
    }
    VkResult result = c->fn->vkGetGpaSessionResultsAMD(device, gpaSession, sampleID, pSizeInBytes, pData);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u64(e, (uint64_t)(*pSizeInBytes));
    mvk_put_u32(e, pData != NULL);
    if (pData) {
        mvk_put_u32(e, (uint32_t)(*pSizeInBytes));
        mvk_put_bytes(e, pData, (size_t)(*pSizeInBytes));
    }
    mvk_reply_end(c);
}

static void run_vkResetGpaSessionAMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkGpaSessionAMD gpaSession;
    device = (VkDevice)mvk_get_handle(d, 1);
    gpaSession = (VkGpaSessionAMD)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkResetGpaSessionAMD");
        return;
    }
    if (!c->fn->vkResetGpaSessionAMD) {
        mvk_unsupported(c, "vkResetGpaSessionAMD");
        return;
    }
    VkResult result = c->fn->vkResetGpaSessionAMD(device, gpaSession);
    mvk_check(c, result, "vkResetGpaSessionAMD");
}

static void run_vkCmdCopyGpaSessionResultsAMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkGpaSessionAMD gpaSession;
    gpaSession = (VkGpaSessionAMD)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyGpaSessionResultsAMD");
        return;
    }
    if (!c->fn->vkCmdCopyGpaSessionResultsAMD) {
        mvk_unsupported(c, "vkCmdCopyGpaSessionResultsAMD");
        return;
    }
    c->fn->vkCmdCopyGpaSessionResultsAMD(commandBuffer, gpaSession);
}

static void run_vkWriteSamplerDescriptorsEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t samplerCount;
    VkSamplerCreateInfo * pSamplers;
    VkHostAddressRangeEXT * pDescriptors;
    device = (VkDevice)mvk_get_handle(d, 1);
    samplerCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkSamplerCreateInfo *items = mvk_alloc(d, (size_t)(samplerCount) * sizeof(VkSamplerCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(samplerCount); i++)
            mvk_get_VkSamplerCreateInfo(d, &items[i]);
        pSamplers = items;
    } else {
        pSamplers = NULL;
    }
    if (mvk_get_u32(d)) {
        VkHostAddressRangeEXT *items = mvk_alloc(d, (size_t)(samplerCount) * sizeof(VkHostAddressRangeEXT));
        for (uint32_t i = 0; i < (uint32_t)(samplerCount); i++)
            mvk_get_VkHostAddressRangeEXT(d, &items[i]);
        pDescriptors = items;
    } else {
        pDescriptors = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkWriteSamplerDescriptorsEXT");
        return;
    }
    if (!c->fn->vkWriteSamplerDescriptorsEXT) {
        mvk_unsupported(c, "vkWriteSamplerDescriptorsEXT");
        return;
    }
    VkResult result = c->fn->vkWriteSamplerDescriptorsEXT(device, samplerCount, pSamplers, pDescriptors);
    mvk_check(c, result, "vkWriteSamplerDescriptorsEXT");
}

static void run_vkWriteResourceDescriptorsEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t resourceCount;
    VkResourceDescriptorInfoEXT * pResources;
    VkHostAddressRangeEXT * pDescriptors;
    device = (VkDevice)mvk_get_handle(d, 1);
    resourceCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkResourceDescriptorInfoEXT *items = mvk_alloc(d, (size_t)(resourceCount) * sizeof(VkResourceDescriptorInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(resourceCount); i++)
            mvk_get_VkResourceDescriptorInfoEXT(d, &items[i]);
        pResources = items;
    } else {
        pResources = NULL;
    }
    if (mvk_get_u32(d)) {
        VkHostAddressRangeEXT *items = mvk_alloc(d, (size_t)(resourceCount) * sizeof(VkHostAddressRangeEXT));
        for (uint32_t i = 0; i < (uint32_t)(resourceCount); i++)
            mvk_get_VkHostAddressRangeEXT(d, &items[i]);
        pDescriptors = items;
    } else {
        pDescriptors = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkWriteResourceDescriptorsEXT");
        return;
    }
    if (!c->fn->vkWriteResourceDescriptorsEXT) {
        mvk_unsupported(c, "vkWriteResourceDescriptorsEXT");
        return;
    }
    VkResult result = c->fn->vkWriteResourceDescriptorsEXT(device, resourceCount, pResources, pDescriptors);
    mvk_check(c, result, "vkWriteResourceDescriptorsEXT");
}

static void run_vkCmdBindSamplerHeapEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBindHeapInfoEXT * pBindInfo;
    if (mvk_get_u32(d)) {
        VkBindHeapInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBindHeapInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBindHeapInfoEXT(d, &items[i]);
        pBindInfo = items;
    } else {
        pBindInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindSamplerHeapEXT");
        return;
    }
    if (!c->fn->vkCmdBindSamplerHeapEXT) {
        mvk_unsupported(c, "vkCmdBindSamplerHeapEXT");
        return;
    }
    c->fn->vkCmdBindSamplerHeapEXT(commandBuffer, pBindInfo);
}

static void run_vkCmdBindResourceHeapEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBindHeapInfoEXT * pBindInfo;
    if (mvk_get_u32(d)) {
        VkBindHeapInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBindHeapInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBindHeapInfoEXT(d, &items[i]);
        pBindInfo = items;
    } else {
        pBindInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindResourceHeapEXT");
        return;
    }
    if (!c->fn->vkCmdBindResourceHeapEXT) {
        mvk_unsupported(c, "vkCmdBindResourceHeapEXT");
        return;
    }
    c->fn->vkCmdBindResourceHeapEXT(commandBuffer, pBindInfo);
}

static void run_vkCmdPushDataEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPushDataInfoEXT * pPushDataInfo;
    if (mvk_get_u32(d)) {
        VkPushDataInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPushDataInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPushDataInfoEXT(d, &items[i]);
        pPushDataInfo = items;
    } else {
        pPushDataInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdPushDataEXT");
        return;
    }
    if (!c->fn->vkCmdPushDataEXT) {
        mvk_unsupported(c, "vkCmdPushDataEXT");
        return;
    }
    c->fn->vkCmdPushDataEXT(commandBuffer, pPushDataInfo);
}

static void run_vkGetImageOpaqueCaptureDataEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t imageCount;
    VkImage * pImages;
    device = (VkDevice)mvk_get_handle(d, 1);
    imageCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkImage *items = mvk_alloc(d, (size_t)(imageCount) * sizeof(VkImage));
        for (uint32_t i = 0; i < (uint32_t)(imageCount); i++)
            items[i] = (VkImage)mvk_get_handle(d, 0);
        pImages = items;
    } else {
        pImages = NULL;
    }
    int want_pDatas = mvk_get_u32(d);
    VkHostAddressRangeEXT *pDatas = NULL;
    if (want_pDatas) {
        pDatas = mvk_alloc(d, sizeof(VkHostAddressRangeEXT) * (size_t)(imageCount) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetImageOpaqueCaptureDataEXT");
        return;
    }
    if (!c->fn->vkGetImageOpaqueCaptureDataEXT) {
        mvk_unsupported(c, "vkGetImageOpaqueCaptureDataEXT");
        return;
    }
    VkResult result = c->fn->vkGetImageOpaqueCaptureDataEXT(device, imageCount, pImages, pDatas);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, pDatas != NULL);
    if (pDatas) {
        mvk_put_u32(e, (uint32_t)(imageCount));
        for (uint32_t i = 0; i < (uint32_t)(imageCount); i++)
            reply_VkHostAddressRangeEXT(e, &pDatas[i]);
    }
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceDescriptorSizeEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkDescriptorType descriptorType;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    descriptorType = (VkDescriptorType)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceDescriptorSizeEXT");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceDescriptorSizeEXT) {
        mvk_unsupported(c, "vkGetPhysicalDeviceDescriptorSizeEXT");
        return;
    }
    VkDeviceSize result = c->fn->vkGetPhysicalDeviceDescriptorSizeEXT(physicalDevice, descriptorType);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u64(e, (uint64_t)(result));
    mvk_reply_end(c);
}

static void run_vkRegisterCustomBorderColorEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkSamplerCustomBorderColorCreateInfoEXT * pBorderColor;
    VkBool32 requestIndex;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkSamplerCustomBorderColorCreateInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkSamplerCustomBorderColorCreateInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkSamplerCustomBorderColorCreateInfoEXT(d, &items[i]);
        pBorderColor = items;
    } else {
        pBorderColor = NULL;
    }
    requestIndex = (VkBool32)mvk_get_u32(d);
    uint32_t pIndex_value;
    memset(&pIndex_value, 0, sizeof(pIndex_value));
    uint32_t *pIndex = &pIndex_value;
    if (d->error) {
        mvk_malformed(c, "vkRegisterCustomBorderColorEXT");
        return;
    }
    if (!c->fn->vkRegisterCustomBorderColorEXT) {
        mvk_unsupported(c, "vkRegisterCustomBorderColorEXT");
        return;
    }
    VkResult result = c->fn->vkRegisterCustomBorderColorEXT(device, pBorderColor, requestIndex, pIndex);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pIndex));
    mvk_reply_end(c);
}

static void run_vkUnregisterCustomBorderColorEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t index;
    device = (VkDevice)mvk_get_handle(d, 1);
    index = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkUnregisterCustomBorderColorEXT");
        return;
    }
    if (!c->fn->vkUnregisterCustomBorderColorEXT) {
        mvk_unsupported(c, "vkUnregisterCustomBorderColorEXT");
        return;
    }
    c->fn->vkUnregisterCustomBorderColorEXT(device, index);
}

static void run_vkGetTensorOpaqueCaptureDataARM(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t tensorCount;
    VkTensorARM * pTensors;
    device = (VkDevice)mvk_get_handle(d, 1);
    tensorCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkTensorARM *items = mvk_alloc(d, (size_t)(tensorCount) * sizeof(VkTensorARM));
        for (uint32_t i = 0; i < (uint32_t)(tensorCount); i++)
            items[i] = (VkTensorARM)mvk_get_handle(d, 0);
        pTensors = items;
    } else {
        pTensors = NULL;
    }
    int want_pDatas = mvk_get_u32(d);
    VkHostAddressRangeEXT *pDatas = NULL;
    if (want_pDatas) {
        pDatas = mvk_alloc(d, sizeof(VkHostAddressRangeEXT) * (size_t)(tensorCount) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetTensorOpaqueCaptureDataARM");
        return;
    }
    if (!c->fn->vkGetTensorOpaqueCaptureDataARM) {
        mvk_unsupported(c, "vkGetTensorOpaqueCaptureDataARM");
        return;
    }
    VkResult result = c->fn->vkGetTensorOpaqueCaptureDataARM(device, tensorCount, pTensors, pDatas);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, pDatas != NULL);
    if (pDatas) {
        mvk_put_u32(e, (uint32_t)(tensorCount));
        for (uint32_t i = 0; i < (uint32_t)(tensorCount); i++)
            reply_VkHostAddressRangeEXT(e, &pDatas[i]);
    }
    mvk_reply_end(c);
}

static void run_vkCmdSetSampleLocationsEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkSampleLocationsInfoEXT * pSampleLocationsInfo;
    if (mvk_get_u32(d)) {
        VkSampleLocationsInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkSampleLocationsInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkSampleLocationsInfoEXT(d, &items[i]);
        pSampleLocationsInfo = items;
    } else {
        pSampleLocationsInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetSampleLocationsEXT");
        return;
    }
    if (!c->fn->vkCmdSetSampleLocationsEXT) {
        mvk_unsupported(c, "vkCmdSetSampleLocationsEXT");
        return;
    }
    c->fn->vkCmdSetSampleLocationsEXT(commandBuffer, pSampleLocationsInfo);
}

static void run_vkGetPhysicalDeviceMultisamplePropertiesEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkSampleCountFlagBits samples;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    samples = (VkSampleCountFlagBits)mvk_get_u32(d);
    VkMultisamplePropertiesEXT *pMultisampleProperties = mvk_get_out_shape(d, sizeof(VkMultisamplePropertiesEXT));
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceMultisamplePropertiesEXT");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceMultisamplePropertiesEXT) {
        mvk_unsupported(c, "vkGetPhysicalDeviceMultisamplePropertiesEXT");
        return;
    }
    c->fn->vkGetPhysicalDeviceMultisamplePropertiesEXT(physicalDevice, samples, pMultisampleProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkMultisamplePropertiesEXT(e, pMultisampleProperties);
    mvk_reply_end(c);
}

static void run_vkCreateAccelerationStructureKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkAccelerationStructureCreateInfoKHR * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureCreateInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkAccelerationStructureCreateInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkAccelerationStructureCreateInfoKHR(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pAccelerationStructure = mvk_get_u64(d);
    VkAccelerationStructureKHR pAccelerationStructure_value = VK_NULL_HANDLE;
    VkAccelerationStructureKHR *pAccelerationStructure = &pAccelerationStructure_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateAccelerationStructureKHR");
        return;
    }
    if (!c->fn->vkCreateAccelerationStructureKHR) {
        mvk_unsupported(c, "vkCreateAccelerationStructureKHR");
        return;
    }
    VkResult result = c->fn->vkCreateAccelerationStructureKHR(device, pCreateInfo, NULL, pAccelerationStructure);
    mvk_bind_id(c, id_pAccelerationStructure, (uint64_t)pAccelerationStructure_value);
    mvk_track(c->conn, (uint64_t)pAccelerationStructure_value, MVK_KIND_VkAccelerationStructureKHR);
    mvk_check(c, result, "vkCreateAccelerationStructureKHR");
}

static void run_vkDestroyAccelerationStructureKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkAccelerationStructureKHR accelerationStructure;
    device = (VkDevice)mvk_get_handle(d, 1);
    accelerationStructure = (VkAccelerationStructureKHR)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyAccelerationStructureKHR");
        return;
    }
    if (!c->fn->vkDestroyAccelerationStructureKHR) {
        mvk_unsupported(c, "vkDestroyAccelerationStructureKHR");
        return;
    }
    c->fn->vkDestroyAccelerationStructureKHR(device, accelerationStructure, NULL);
    mvk_forget(c->conn, (uint64_t)accelerationStructure);
}

static void run_vkCmdBuildAccelerationStructuresKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t infoCount;
    VkAccelerationStructureBuildGeometryInfoKHR * pInfos;
    const VkAccelerationStructureBuildRangeInfoKHR * const * ppBuildRangeInfos;
    infoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureBuildGeometryInfoKHR *items = mvk_alloc(d, (size_t)(infoCount) * sizeof(VkAccelerationStructureBuildGeometryInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_get_VkAccelerationStructureBuildGeometryInfoKHR(d, &items[i]);
        pInfos = items;
    } else {
        pInfos = NULL;
    }
    if (mvk_get_u32(d)) {
        VkAccelerationStructureBuildRangeInfoKHR **items = mvk_alloc(d, sizeof(VkAccelerationStructureBuildRangeInfoKHR *) * (size_t)(infoCount));
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++) {
            uint32_t inner = (uint32_t)(pInfos[i].geometryCount);
            items[i] = mvk_alloc(d, sizeof(VkAccelerationStructureBuildRangeInfoKHR) * (size_t)(inner ? inner : 1));
            for (uint32_t j = 0; j < inner; j++)
                mvk_get_VkAccelerationStructureBuildRangeInfoKHR(d, &items[i][j]);
        }
        ppBuildRangeInfos = (void *)items;
    } else {
        ppBuildRangeInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBuildAccelerationStructuresKHR");
        return;
    }
    if (!c->fn->vkCmdBuildAccelerationStructuresKHR) {
        mvk_unsupported(c, "vkCmdBuildAccelerationStructuresKHR");
        return;
    }
    c->fn->vkCmdBuildAccelerationStructuresKHR(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);
}

static void run_vkCmdBuildAccelerationStructuresIndirectKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t infoCount;
    VkAccelerationStructureBuildGeometryInfoKHR * pInfos;
    VkDeviceAddress * pIndirectDeviceAddresses;
    uint32_t * pIndirectStrides;
    const uint32_t * const * ppMaxPrimitiveCounts;
    infoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureBuildGeometryInfoKHR *items = mvk_alloc(d, (size_t)(infoCount) * sizeof(VkAccelerationStructureBuildGeometryInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_get_VkAccelerationStructureBuildGeometryInfoKHR(d, &items[i]);
        pInfos = items;
    } else {
        pInfos = NULL;
    }
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(infoCount) * sizeof(VkDeviceAddress));
        mvk_get_bytes(d, items, (size_t)(infoCount) * sizeof(VkDeviceAddress));
        pIndirectDeviceAddresses = items;
    } else {
        pIndirectDeviceAddresses = NULL;
    }
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(infoCount) * sizeof(uint32_t));
        mvk_get_bytes(d, items, (size_t)(infoCount) * sizeof(uint32_t));
        pIndirectStrides = items;
    } else {
        pIndirectStrides = NULL;
    }
    if (mvk_get_u32(d)) {
        uint32_t **items = mvk_alloc(d, sizeof(uint32_t *) * (size_t)(infoCount));
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++) {
            uint32_t inner = (uint32_t)(pInfos[i].geometryCount);
            items[i] = mvk_alloc(d, sizeof(uint32_t) * (size_t)(inner ? inner : 1));
            mvk_get_bytes(d, items[i], (size_t)inner * sizeof(uint32_t));
        }
        ppMaxPrimitiveCounts = (void *)items;
    } else {
        ppMaxPrimitiveCounts = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBuildAccelerationStructuresIndirectKHR");
        return;
    }
    if (!c->fn->vkCmdBuildAccelerationStructuresIndirectKHR) {
        mvk_unsupported(c, "vkCmdBuildAccelerationStructuresIndirectKHR");
        return;
    }
    c->fn->vkCmdBuildAccelerationStructuresIndirectKHR(commandBuffer, infoCount, pInfos, pIndirectDeviceAddresses, pIndirectStrides, ppMaxPrimitiveCounts);
}

static void run_vkBuildAccelerationStructuresKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR deferredOperation;
    uint32_t infoCount;
    VkAccelerationStructureBuildGeometryInfoKHR * pInfos;
    const VkAccelerationStructureBuildRangeInfoKHR * const * ppBuildRangeInfos;
    device = (VkDevice)mvk_get_handle(d, 1);
    deferredOperation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    infoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureBuildGeometryInfoKHR *items = mvk_alloc(d, (size_t)(infoCount) * sizeof(VkAccelerationStructureBuildGeometryInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_get_VkAccelerationStructureBuildGeometryInfoKHR(d, &items[i]);
        pInfos = items;
    } else {
        pInfos = NULL;
    }
    if (mvk_get_u32(d)) {
        VkAccelerationStructureBuildRangeInfoKHR **items = mvk_alloc(d, sizeof(VkAccelerationStructureBuildRangeInfoKHR *) * (size_t)(infoCount));
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++) {
            uint32_t inner = (uint32_t)(pInfos[i].geometryCount);
            items[i] = mvk_alloc(d, sizeof(VkAccelerationStructureBuildRangeInfoKHR) * (size_t)(inner ? inner : 1));
            for (uint32_t j = 0; j < inner; j++)
                mvk_get_VkAccelerationStructureBuildRangeInfoKHR(d, &items[i][j]);
        }
        ppBuildRangeInfos = (void *)items;
    } else {
        ppBuildRangeInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkBuildAccelerationStructuresKHR");
        return;
    }
    if (!c->fn->vkBuildAccelerationStructuresKHR) {
        mvk_unsupported(c, "vkBuildAccelerationStructuresKHR");
        return;
    }
    VkResult result = c->fn->vkBuildAccelerationStructuresKHR(device, deferredOperation, infoCount, pInfos, ppBuildRangeInfos);
    mvk_check(c, result, "vkBuildAccelerationStructuresKHR");
}

static void run_vkCopyAccelerationStructureKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR deferredOperation;
    VkCopyAccelerationStructureInfoKHR * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    deferredOperation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    if (mvk_get_u32(d)) {
        VkCopyAccelerationStructureInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyAccelerationStructureInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyAccelerationStructureInfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCopyAccelerationStructureKHR");
        return;
    }
    if (!c->fn->vkCopyAccelerationStructureKHR) {
        mvk_unsupported(c, "vkCopyAccelerationStructureKHR");
        return;
    }
    VkResult result = c->fn->vkCopyAccelerationStructureKHR(device, deferredOperation, pInfo);
    mvk_check(c, result, "vkCopyAccelerationStructureKHR");
}

static void run_vkCopyAccelerationStructureToMemoryKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR deferredOperation;
    VkCopyAccelerationStructureToMemoryInfoKHR * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    deferredOperation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    if (mvk_get_u32(d)) {
        VkCopyAccelerationStructureToMemoryInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyAccelerationStructureToMemoryInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyAccelerationStructureToMemoryInfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCopyAccelerationStructureToMemoryKHR");
        return;
    }
    if (!c->fn->vkCopyAccelerationStructureToMemoryKHR) {
        mvk_unsupported(c, "vkCopyAccelerationStructureToMemoryKHR");
        return;
    }
    VkResult result = c->fn->vkCopyAccelerationStructureToMemoryKHR(device, deferredOperation, pInfo);
    mvk_check(c, result, "vkCopyAccelerationStructureToMemoryKHR");
}

static void run_vkCopyMemoryToAccelerationStructureKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR deferredOperation;
    VkCopyMemoryToAccelerationStructureInfoKHR * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    deferredOperation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    if (mvk_get_u32(d)) {
        VkCopyMemoryToAccelerationStructureInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyMemoryToAccelerationStructureInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyMemoryToAccelerationStructureInfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCopyMemoryToAccelerationStructureKHR");
        return;
    }
    if (!c->fn->vkCopyMemoryToAccelerationStructureKHR) {
        mvk_unsupported(c, "vkCopyMemoryToAccelerationStructureKHR");
        return;
    }
    VkResult result = c->fn->vkCopyMemoryToAccelerationStructureKHR(device, deferredOperation, pInfo);
    mvk_check(c, result, "vkCopyMemoryToAccelerationStructureKHR");
}

static void run_vkWriteAccelerationStructuresPropertiesKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t accelerationStructureCount;
    VkAccelerationStructureKHR * pAccelerationStructures;
    VkQueryType queryType;
    size_t dataSize;
    size_t stride;
    device = (VkDevice)mvk_get_handle(d, 1);
    accelerationStructureCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureKHR *items = mvk_alloc(d, (size_t)(accelerationStructureCount) * sizeof(VkAccelerationStructureKHR));
        for (uint32_t i = 0; i < (uint32_t)(accelerationStructureCount); i++)
            items[i] = (VkAccelerationStructureKHR)mvk_get_handle(d, 0);
        pAccelerationStructures = items;
    } else {
        pAccelerationStructures = NULL;
    }
    queryType = (VkQueryType)mvk_get_u32(d);
    dataSize = (size_t)mvk_get_u64(d);
    int want_pData = mvk_get_u32(d);
    uint8_t *pData = NULL;
    stride = (size_t)mvk_get_u64(d);
    if (want_pData) {
        pData = mvk_alloc(d, sizeof(uint8_t) * (size_t)(dataSize) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkWriteAccelerationStructuresPropertiesKHR");
        return;
    }
    if (!c->fn->vkWriteAccelerationStructuresPropertiesKHR) {
        mvk_unsupported(c, "vkWriteAccelerationStructuresPropertiesKHR");
        return;
    }
    VkResult result = c->fn->vkWriteAccelerationStructuresPropertiesKHR(device, accelerationStructureCount, pAccelerationStructures, queryType, dataSize, pData, stride);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, pData != NULL);
    if (pData) {
        mvk_put_u32(e, (uint32_t)(dataSize));
        mvk_put_bytes(e, pData, (size_t)(dataSize));
    }
    mvk_reply_end(c);
}

static void run_vkCmdCopyAccelerationStructureKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyAccelerationStructureInfoKHR * pInfo;
    if (mvk_get_u32(d)) {
        VkCopyAccelerationStructureInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyAccelerationStructureInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyAccelerationStructureInfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyAccelerationStructureKHR");
        return;
    }
    if (!c->fn->vkCmdCopyAccelerationStructureKHR) {
        mvk_unsupported(c, "vkCmdCopyAccelerationStructureKHR");
        return;
    }
    c->fn->vkCmdCopyAccelerationStructureKHR(commandBuffer, pInfo);
}

static void run_vkCmdCopyAccelerationStructureToMemoryKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyAccelerationStructureToMemoryInfoKHR * pInfo;
    if (mvk_get_u32(d)) {
        VkCopyAccelerationStructureToMemoryInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyAccelerationStructureToMemoryInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyAccelerationStructureToMemoryInfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyAccelerationStructureToMemoryKHR");
        return;
    }
    if (!c->fn->vkCmdCopyAccelerationStructureToMemoryKHR) {
        mvk_unsupported(c, "vkCmdCopyAccelerationStructureToMemoryKHR");
        return;
    }
    c->fn->vkCmdCopyAccelerationStructureToMemoryKHR(commandBuffer, pInfo);
}

static void run_vkCmdCopyMemoryToAccelerationStructureKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyMemoryToAccelerationStructureInfoKHR * pInfo;
    if (mvk_get_u32(d)) {
        VkCopyMemoryToAccelerationStructureInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyMemoryToAccelerationStructureInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyMemoryToAccelerationStructureInfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyMemoryToAccelerationStructureKHR");
        return;
    }
    if (!c->fn->vkCmdCopyMemoryToAccelerationStructureKHR) {
        mvk_unsupported(c, "vkCmdCopyMemoryToAccelerationStructureKHR");
        return;
    }
    c->fn->vkCmdCopyMemoryToAccelerationStructureKHR(commandBuffer, pInfo);
}

static void run_vkGetAccelerationStructureDeviceAddressKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkAccelerationStructureDeviceAddressInfoKHR * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureDeviceAddressInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkAccelerationStructureDeviceAddressInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkAccelerationStructureDeviceAddressInfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkGetAccelerationStructureDeviceAddressKHR");
        return;
    }
    if (!c->fn->vkGetAccelerationStructureDeviceAddressKHR) {
        mvk_unsupported(c, "vkGetAccelerationStructureDeviceAddressKHR");
        return;
    }
    VkDeviceAddress result = c->fn->vkGetAccelerationStructureDeviceAddressKHR(device, pInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u64(e, (uint64_t)(result));
    mvk_reply_end(c);
}

static void run_vkCmdWriteAccelerationStructuresPropertiesKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t accelerationStructureCount;
    VkAccelerationStructureKHR * pAccelerationStructures;
    VkQueryType queryType;
    VkQueryPool queryPool;
    uint32_t firstQuery;
    accelerationStructureCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureKHR *items = mvk_alloc(d, (size_t)(accelerationStructureCount) * sizeof(VkAccelerationStructureKHR));
        for (uint32_t i = 0; i < (uint32_t)(accelerationStructureCount); i++)
            items[i] = (VkAccelerationStructureKHR)mvk_get_handle(d, 0);
        pAccelerationStructures = items;
    } else {
        pAccelerationStructures = NULL;
    }
    queryType = (VkQueryType)mvk_get_u32(d);
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    firstQuery = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdWriteAccelerationStructuresPropertiesKHR");
        return;
    }
    if (!c->fn->vkCmdWriteAccelerationStructuresPropertiesKHR) {
        mvk_unsupported(c, "vkCmdWriteAccelerationStructuresPropertiesKHR");
        return;
    }
    c->fn->vkCmdWriteAccelerationStructuresPropertiesKHR(commandBuffer, accelerationStructureCount, pAccelerationStructures, queryType, queryPool, firstQuery);
}

static void run_vkGetDeviceAccelerationStructureCompatibilityKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkAccelerationStructureVersionInfoKHR * pVersionInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureVersionInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkAccelerationStructureVersionInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkAccelerationStructureVersionInfoKHR(d, &items[i]);
        pVersionInfo = items;
    } else {
        pVersionInfo = NULL;
    }
    VkAccelerationStructureCompatibilityKHR pCompatibility_value;
    memset(&pCompatibility_value, 0, sizeof(pCompatibility_value));
    VkAccelerationStructureCompatibilityKHR *pCompatibility = &pCompatibility_value;
    if (d->error) {
        mvk_malformed(c, "vkGetDeviceAccelerationStructureCompatibilityKHR");
        return;
    }
    if (!c->fn->vkGetDeviceAccelerationStructureCompatibilityKHR) {
        mvk_unsupported(c, "vkGetDeviceAccelerationStructureCompatibilityKHR");
        return;
    }
    c->fn->vkGetDeviceAccelerationStructureCompatibilityKHR(device, pVersionInfo, pCompatibility);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(*pCompatibility));
    mvk_reply_end(c);
}

static void run_vkGetAccelerationStructureBuildSizesKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkAccelerationStructureBuildTypeKHR buildType;
    VkAccelerationStructureBuildGeometryInfoKHR * pBuildInfo;
    uint32_t * pMaxPrimitiveCounts;
    device = (VkDevice)mvk_get_handle(d, 1);
    buildType = (VkAccelerationStructureBuildTypeKHR)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureBuildGeometryInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkAccelerationStructureBuildGeometryInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkAccelerationStructureBuildGeometryInfoKHR(d, &items[i]);
        pBuildInfo = items;
    } else {
        pBuildInfo = NULL;
    }
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(pBuildInfo->geometryCount) * sizeof(uint32_t));
        mvk_get_bytes(d, items, (size_t)(pBuildInfo->geometryCount) * sizeof(uint32_t));
        pMaxPrimitiveCounts = items;
    } else {
        pMaxPrimitiveCounts = NULL;
    }
    VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo = mvk_get_out_shape(d, sizeof(VkAccelerationStructureBuildSizesInfoKHR));
    if (d->error) {
        mvk_malformed(c, "vkGetAccelerationStructureBuildSizesKHR");
        return;
    }
    if (!c->fn->vkGetAccelerationStructureBuildSizesKHR) {
        mvk_unsupported(c, "vkGetAccelerationStructureBuildSizesKHR");
        return;
    }
    c->fn->vkGetAccelerationStructureBuildSizesKHR(device, buildType, pBuildInfo, pMaxPrimitiveCounts, pSizeInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkAccelerationStructureBuildSizesInfoKHR(e, pSizeInfo);
    mvk_reply_end(c);
}

static void run_vkCmdTraceRaysKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkStridedDeviceAddressRegionKHR * pRaygenShaderBindingTable;
    VkStridedDeviceAddressRegionKHR * pMissShaderBindingTable;
    VkStridedDeviceAddressRegionKHR * pHitShaderBindingTable;
    VkStridedDeviceAddressRegionKHR * pCallableShaderBindingTable;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    if (mvk_get_u32(d)) {
        VkStridedDeviceAddressRegionKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkStridedDeviceAddressRegionKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkStridedDeviceAddressRegionKHR(d, &items[i]);
        pRaygenShaderBindingTable = items;
    } else {
        pRaygenShaderBindingTable = NULL;
    }
    if (mvk_get_u32(d)) {
        VkStridedDeviceAddressRegionKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkStridedDeviceAddressRegionKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkStridedDeviceAddressRegionKHR(d, &items[i]);
        pMissShaderBindingTable = items;
    } else {
        pMissShaderBindingTable = NULL;
    }
    if (mvk_get_u32(d)) {
        VkStridedDeviceAddressRegionKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkStridedDeviceAddressRegionKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkStridedDeviceAddressRegionKHR(d, &items[i]);
        pHitShaderBindingTable = items;
    } else {
        pHitShaderBindingTable = NULL;
    }
    if (mvk_get_u32(d)) {
        VkStridedDeviceAddressRegionKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkStridedDeviceAddressRegionKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkStridedDeviceAddressRegionKHR(d, &items[i]);
        pCallableShaderBindingTable = items;
    } else {
        pCallableShaderBindingTable = NULL;
    }
    width = (uint32_t)mvk_get_u32(d);
    height = (uint32_t)mvk_get_u32(d);
    depth = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdTraceRaysKHR");
        return;
    }
    if (!c->fn->vkCmdTraceRaysKHR) {
        mvk_unsupported(c, "vkCmdTraceRaysKHR");
        return;
    }
    c->fn->vkCmdTraceRaysKHR(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, width, height, depth);
}

static void run_vkCreateRayTracingPipelinesKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR deferredOperation;
    VkPipelineCache pipelineCache;
    uint32_t createInfoCount;
    VkRayTracingPipelineCreateInfoKHR * pCreateInfos;
    device = (VkDevice)mvk_get_handle(d, 1);
    deferredOperation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    pipelineCache = (VkPipelineCache)mvk_get_handle(d, 0);
    createInfoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkRayTracingPipelineCreateInfoKHR *items = mvk_alloc(d, (size_t)(createInfoCount) * sizeof(VkRayTracingPipelineCreateInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
            mvk_get_VkRayTracingPipelineCreateInfoKHR(d, &items[i]);
        pCreateInfos = items;
    } else {
        pCreateInfos = NULL;
    }
    uint64_t *ids_pPipelines = mvk_alloc(d, sizeof(uint64_t) * (size_t)(createInfoCount));
    VkPipeline *pPipelines = mvk_alloc(d, sizeof(VkPipeline) * (size_t)(createInfoCount));
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
        ids_pPipelines[i] = mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkCreateRayTracingPipelinesKHR");
        return;
    }
    if (!c->fn->vkCreateRayTracingPipelinesKHR) {
        mvk_unsupported(c, "vkCreateRayTracingPipelinesKHR");
        return;
    }
    VkResult result = c->fn->vkCreateRayTracingPipelinesKHR(device, deferredOperation, pipelineCache, createInfoCount, pCreateInfos, NULL, pPipelines);
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++) {
        mvk_bind_id(c, ids_pPipelines[i], (uint64_t)pPipelines[i]);
        mvk_track(c->conn, (uint64_t)pPipelines[i], MVK_KIND_VkPipeline);
    }
    mvk_check(c, result, "vkCreateRayTracingPipelinesKHR");
}

static void run_vkGetRayTracingShaderGroupHandlesKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipeline pipeline;
    uint32_t firstGroup;
    uint32_t groupCount;
    size_t dataSize;
    device = (VkDevice)mvk_get_handle(d, 1);
    pipeline = (VkPipeline)mvk_get_handle(d, 0);
    firstGroup = (uint32_t)mvk_get_u32(d);
    groupCount = (uint32_t)mvk_get_u32(d);
    dataSize = (size_t)mvk_get_u64(d);
    int want_pData = mvk_get_u32(d);
    uint8_t *pData = NULL;
    if (want_pData) {
        pData = mvk_alloc(d, sizeof(uint8_t) * (size_t)(dataSize) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetRayTracingShaderGroupHandlesKHR");
        return;
    }
    if (!c->fn->vkGetRayTracingShaderGroupHandlesKHR) {
        mvk_unsupported(c, "vkGetRayTracingShaderGroupHandlesKHR");
        return;
    }
    VkResult result = c->fn->vkGetRayTracingShaderGroupHandlesKHR(device, pipeline, firstGroup, groupCount, dataSize, pData);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, pData != NULL);
    if (pData) {
        mvk_put_u32(e, (uint32_t)(dataSize));
        mvk_put_bytes(e, pData, (size_t)(dataSize));
    }
    mvk_reply_end(c);
}

static void run_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipeline pipeline;
    uint32_t firstGroup;
    uint32_t groupCount;
    size_t dataSize;
    device = (VkDevice)mvk_get_handle(d, 1);
    pipeline = (VkPipeline)mvk_get_handle(d, 0);
    firstGroup = (uint32_t)mvk_get_u32(d);
    groupCount = (uint32_t)mvk_get_u32(d);
    dataSize = (size_t)mvk_get_u64(d);
    int want_pData = mvk_get_u32(d);
    uint8_t *pData = NULL;
    if (want_pData) {
        pData = mvk_alloc(d, sizeof(uint8_t) * (size_t)(dataSize) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR");
        return;
    }
    if (!c->fn->vkGetRayTracingCaptureReplayShaderGroupHandlesKHR) {
        mvk_unsupported(c, "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR");
        return;
    }
    VkResult result = c->fn->vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(device, pipeline, firstGroup, groupCount, dataSize, pData);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, pData != NULL);
    if (pData) {
        mvk_put_u32(e, (uint32_t)(dataSize));
        mvk_put_bytes(e, pData, (size_t)(dataSize));
    }
    mvk_reply_end(c);
}

static void run_vkCmdTraceRaysIndirectKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkStridedDeviceAddressRegionKHR * pRaygenShaderBindingTable;
    VkStridedDeviceAddressRegionKHR * pMissShaderBindingTable;
    VkStridedDeviceAddressRegionKHR * pHitShaderBindingTable;
    VkStridedDeviceAddressRegionKHR * pCallableShaderBindingTable;
    VkDeviceAddress indirectDeviceAddress;
    if (mvk_get_u32(d)) {
        VkStridedDeviceAddressRegionKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkStridedDeviceAddressRegionKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkStridedDeviceAddressRegionKHR(d, &items[i]);
        pRaygenShaderBindingTable = items;
    } else {
        pRaygenShaderBindingTable = NULL;
    }
    if (mvk_get_u32(d)) {
        VkStridedDeviceAddressRegionKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkStridedDeviceAddressRegionKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkStridedDeviceAddressRegionKHR(d, &items[i]);
        pMissShaderBindingTable = items;
    } else {
        pMissShaderBindingTable = NULL;
    }
    if (mvk_get_u32(d)) {
        VkStridedDeviceAddressRegionKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkStridedDeviceAddressRegionKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkStridedDeviceAddressRegionKHR(d, &items[i]);
        pHitShaderBindingTable = items;
    } else {
        pHitShaderBindingTable = NULL;
    }
    if (mvk_get_u32(d)) {
        VkStridedDeviceAddressRegionKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkStridedDeviceAddressRegionKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkStridedDeviceAddressRegionKHR(d, &items[i]);
        pCallableShaderBindingTable = items;
    } else {
        pCallableShaderBindingTable = NULL;
    }
    indirectDeviceAddress = (VkDeviceAddress)mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdTraceRaysIndirectKHR");
        return;
    }
    if (!c->fn->vkCmdTraceRaysIndirectKHR) {
        mvk_unsupported(c, "vkCmdTraceRaysIndirectKHR");
        return;
    }
    c->fn->vkCmdTraceRaysIndirectKHR(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, indirectDeviceAddress);
}

static void run_vkGetRayTracingShaderGroupStackSizeKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipeline pipeline;
    uint32_t group;
    VkShaderGroupShaderKHR groupShader;
    device = (VkDevice)mvk_get_handle(d, 1);
    pipeline = (VkPipeline)mvk_get_handle(d, 0);
    group = (uint32_t)mvk_get_u32(d);
    groupShader = (VkShaderGroupShaderKHR)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkGetRayTracingShaderGroupStackSizeKHR");
        return;
    }
    if (!c->fn->vkGetRayTracingShaderGroupStackSizeKHR) {
        mvk_unsupported(c, "vkGetRayTracingShaderGroupStackSizeKHR");
        return;
    }
    VkDeviceSize result = c->fn->vkGetRayTracingShaderGroupStackSizeKHR(device, pipeline, group, groupShader);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u64(e, (uint64_t)(result));
    mvk_reply_end(c);
}

static void run_vkCmdSetRayTracingPipelineStackSizeKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t pipelineStackSize;
    pipelineStackSize = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetRayTracingPipelineStackSizeKHR");
        return;
    }
    if (!c->fn->vkCmdSetRayTracingPipelineStackSizeKHR) {
        mvk_unsupported(c, "vkCmdSetRayTracingPipelineStackSizeKHR");
        return;
    }
    c->fn->vkCmdSetRayTracingPipelineStackSizeKHR(commandBuffer, pipelineStackSize);
}

static void run_vkGetImageDrmFormatModifierPropertiesEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkImage image;
    device = (VkDevice)mvk_get_handle(d, 1);
    image = (VkImage)mvk_get_handle(d, 0);
    VkImageDrmFormatModifierPropertiesEXT *pProperties = mvk_get_out_shape(d, sizeof(VkImageDrmFormatModifierPropertiesEXT));
    if (d->error) {
        mvk_malformed(c, "vkGetImageDrmFormatModifierPropertiesEXT");
        return;
    }
    if (!c->fn->vkGetImageDrmFormatModifierPropertiesEXT) {
        mvk_unsupported(c, "vkGetImageDrmFormatModifierPropertiesEXT");
        return;
    }
    VkResult result = c->fn->vkGetImageDrmFormatModifierPropertiesEXT(device, image, pProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    reply_VkImageDrmFormatModifierPropertiesEXT(e, pProperties);
    mvk_reply_end(c);
}

static void run_vkCreateValidationCacheEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkValidationCacheCreateInfoEXT * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkValidationCacheCreateInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkValidationCacheCreateInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkValidationCacheCreateInfoEXT(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pValidationCache = mvk_get_u64(d);
    VkValidationCacheEXT pValidationCache_value = VK_NULL_HANDLE;
    VkValidationCacheEXT *pValidationCache = &pValidationCache_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateValidationCacheEXT");
        return;
    }
    if (!c->fn->vkCreateValidationCacheEXT) {
        mvk_unsupported(c, "vkCreateValidationCacheEXT");
        return;
    }
    VkResult result = c->fn->vkCreateValidationCacheEXT(device, pCreateInfo, NULL, pValidationCache);
    mvk_bind_id(c, id_pValidationCache, (uint64_t)pValidationCache_value);
    mvk_track(c->conn, (uint64_t)pValidationCache_value, MVK_KIND_VkValidationCacheEXT);
    mvk_check(c, result, "vkCreateValidationCacheEXT");
}

static void run_vkDestroyValidationCacheEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkValidationCacheEXT validationCache;
    device = (VkDevice)mvk_get_handle(d, 1);
    validationCache = (VkValidationCacheEXT)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyValidationCacheEXT");
        return;
    }
    if (!c->fn->vkDestroyValidationCacheEXT) {
        mvk_unsupported(c, "vkDestroyValidationCacheEXT");
        return;
    }
    c->fn->vkDestroyValidationCacheEXT(device, validationCache, NULL);
    mvk_forget(c->conn, (uint64_t)validationCache);
}

static void run_vkMergeValidationCachesEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkValidationCacheEXT dstCache;
    uint32_t srcCacheCount;
    VkValidationCacheEXT * pSrcCaches;
    device = (VkDevice)mvk_get_handle(d, 1);
    dstCache = (VkValidationCacheEXT)mvk_get_handle(d, 0);
    srcCacheCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkValidationCacheEXT *items = mvk_alloc(d, (size_t)(srcCacheCount) * sizeof(VkValidationCacheEXT));
        for (uint32_t i = 0; i < (uint32_t)(srcCacheCount); i++)
            items[i] = (VkValidationCacheEXT)mvk_get_handle(d, 0);
        pSrcCaches = items;
    } else {
        pSrcCaches = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkMergeValidationCachesEXT");
        return;
    }
    if (!c->fn->vkMergeValidationCachesEXT) {
        mvk_unsupported(c, "vkMergeValidationCachesEXT");
        return;
    }
    VkResult result = c->fn->vkMergeValidationCachesEXT(device, dstCache, srcCacheCount, pSrcCaches);
    mvk_check(c, result, "vkMergeValidationCachesEXT");
}

static void run_vkGetValidationCacheDataEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkValidationCacheEXT validationCache;
    device = (VkDevice)mvk_get_handle(d, 1);
    validationCache = (VkValidationCacheEXT)mvk_get_handle(d, 0);
    size_t pDataSize_value;
    memset(&pDataSize_value, 0, sizeof(pDataSize_value));
    size_t *pDataSize = &pDataSize_value;
    int want_pData = mvk_get_u32(d);
    uint8_t *pData = NULL;
    if (want_pData) {
        pData = mvk_alloc(d, sizeof(uint8_t) * (size_t)(*pDataSize) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetValidationCacheDataEXT");
        return;
    }
    if (!c->fn->vkGetValidationCacheDataEXT) {
        mvk_unsupported(c, "vkGetValidationCacheDataEXT");
        return;
    }
    VkResult result = c->fn->vkGetValidationCacheDataEXT(device, validationCache, pDataSize, pData);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u64(e, (uint64_t)(*pDataSize));
    mvk_put_u32(e, pData != NULL);
    if (pData) {
        mvk_put_u32(e, (uint32_t)(*pDataSize));
        mvk_put_bytes(e, pData, (size_t)(*pDataSize));
    }
    mvk_reply_end(c);
}

static void run_vkCmdBindShadingRateImageNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkImageView imageView;
    VkImageLayout imageLayout;
    imageView = (VkImageView)mvk_get_handle(d, 0);
    imageLayout = (VkImageLayout)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdBindShadingRateImageNV");
        return;
    }
    if (!c->fn->vkCmdBindShadingRateImageNV) {
        mvk_unsupported(c, "vkCmdBindShadingRateImageNV");
        return;
    }
    c->fn->vkCmdBindShadingRateImageNV(commandBuffer, imageView, imageLayout);
}

static void run_vkCmdSetViewportShadingRatePaletteNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstViewport;
    uint32_t viewportCount;
    VkShadingRatePaletteNV * pShadingRatePalettes;
    firstViewport = (uint32_t)mvk_get_u32(d);
    viewportCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkShadingRatePaletteNV *items = mvk_alloc(d, (size_t)(viewportCount) * sizeof(VkShadingRatePaletteNV));
        for (uint32_t i = 0; i < (uint32_t)(viewportCount); i++)
            mvk_get_VkShadingRatePaletteNV(d, &items[i]);
        pShadingRatePalettes = items;
    } else {
        pShadingRatePalettes = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetViewportShadingRatePaletteNV");
        return;
    }
    if (!c->fn->vkCmdSetViewportShadingRatePaletteNV) {
        mvk_unsupported(c, "vkCmdSetViewportShadingRatePaletteNV");
        return;
    }
    c->fn->vkCmdSetViewportShadingRatePaletteNV(commandBuffer, firstViewport, viewportCount, pShadingRatePalettes);
}

static void run_vkCmdSetCoarseSampleOrderNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCoarseSampleOrderTypeNV sampleOrderType;
    uint32_t customSampleOrderCount;
    VkCoarseSampleOrderCustomNV * pCustomSampleOrders;
    sampleOrderType = (VkCoarseSampleOrderTypeNV)mvk_get_u32(d);
    customSampleOrderCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkCoarseSampleOrderCustomNV *items = mvk_alloc(d, (size_t)(customSampleOrderCount) * sizeof(VkCoarseSampleOrderCustomNV));
        for (uint32_t i = 0; i < (uint32_t)(customSampleOrderCount); i++)
            mvk_get_VkCoarseSampleOrderCustomNV(d, &items[i]);
        pCustomSampleOrders = items;
    } else {
        pCustomSampleOrders = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetCoarseSampleOrderNV");
        return;
    }
    if (!c->fn->vkCmdSetCoarseSampleOrderNV) {
        mvk_unsupported(c, "vkCmdSetCoarseSampleOrderNV");
        return;
    }
    c->fn->vkCmdSetCoarseSampleOrderNV(commandBuffer, sampleOrderType, customSampleOrderCount, pCustomSampleOrders);
}

static void run_vkCreateAccelerationStructureNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkAccelerationStructureCreateInfoNV * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureCreateInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkAccelerationStructureCreateInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkAccelerationStructureCreateInfoNV(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pAccelerationStructure = mvk_get_u64(d);
    VkAccelerationStructureNV pAccelerationStructure_value = VK_NULL_HANDLE;
    VkAccelerationStructureNV *pAccelerationStructure = &pAccelerationStructure_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateAccelerationStructureNV");
        return;
    }
    if (!c->fn->vkCreateAccelerationStructureNV) {
        mvk_unsupported(c, "vkCreateAccelerationStructureNV");
        return;
    }
    VkResult result = c->fn->vkCreateAccelerationStructureNV(device, pCreateInfo, NULL, pAccelerationStructure);
    mvk_bind_id(c, id_pAccelerationStructure, (uint64_t)pAccelerationStructure_value);
    mvk_track(c->conn, (uint64_t)pAccelerationStructure_value, MVK_KIND_VkAccelerationStructureNV);
    mvk_check(c, result, "vkCreateAccelerationStructureNV");
}

static void run_vkDestroyAccelerationStructureNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkAccelerationStructureNV accelerationStructure;
    device = (VkDevice)mvk_get_handle(d, 1);
    accelerationStructure = (VkAccelerationStructureNV)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyAccelerationStructureNV");
        return;
    }
    if (!c->fn->vkDestroyAccelerationStructureNV) {
        mvk_unsupported(c, "vkDestroyAccelerationStructureNV");
        return;
    }
    c->fn->vkDestroyAccelerationStructureNV(device, accelerationStructure, NULL);
    mvk_forget(c->conn, (uint64_t)accelerationStructure);
}

static void run_vkGetAccelerationStructureMemoryRequirementsNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkAccelerationStructureMemoryRequirementsInfoNV * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureMemoryRequirementsInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkAccelerationStructureMemoryRequirementsInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkAccelerationStructureMemoryRequirementsInfoNV(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    VkMemoryRequirements2 *pMemoryRequirements = mvk_get_out_shape(d, sizeof(VkMemoryRequirements2));
    if (d->error) {
        mvk_malformed(c, "vkGetAccelerationStructureMemoryRequirementsNV");
        return;
    }
    if (!c->fn->vkGetAccelerationStructureMemoryRequirementsNV) {
        mvk_unsupported(c, "vkGetAccelerationStructureMemoryRequirementsNV");
        return;
    }
    c->fn->vkGetAccelerationStructureMemoryRequirementsNV(device, pInfo, pMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkMemoryRequirements2(e, pMemoryRequirements);
    mvk_reply_end(c);
}

static void run_vkBindAccelerationStructureMemoryNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t bindInfoCount;
    VkBindAccelerationStructureMemoryInfoNV * pBindInfos;
    device = (VkDevice)mvk_get_handle(d, 1);
    bindInfoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBindAccelerationStructureMemoryInfoNV *items = mvk_alloc(d, (size_t)(bindInfoCount) * sizeof(VkBindAccelerationStructureMemoryInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(bindInfoCount); i++)
            mvk_get_VkBindAccelerationStructureMemoryInfoNV(d, &items[i]);
        pBindInfos = items;
    } else {
        pBindInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkBindAccelerationStructureMemoryNV");
        return;
    }
    if (!c->fn->vkBindAccelerationStructureMemoryNV) {
        mvk_unsupported(c, "vkBindAccelerationStructureMemoryNV");
        return;
    }
    VkResult result = c->fn->vkBindAccelerationStructureMemoryNV(device, bindInfoCount, pBindInfos);
    mvk_check(c, result, "vkBindAccelerationStructureMemoryNV");
}

static void run_vkCmdBuildAccelerationStructureNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkAccelerationStructureInfoNV * pInfo;
    VkBuffer instanceData;
    VkDeviceSize instanceOffset;
    VkBool32 update;
    VkAccelerationStructureNV dst;
    VkAccelerationStructureNV src;
    VkBuffer scratch;
    VkDeviceSize scratchOffset;
    if (mvk_get_u32(d)) {
        VkAccelerationStructureInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkAccelerationStructureInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkAccelerationStructureInfoNV(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    instanceData = (VkBuffer)mvk_get_handle(d, 0);
    instanceOffset = (VkDeviceSize)mvk_get_u64(d);
    update = (VkBool32)mvk_get_u32(d);
    dst = (VkAccelerationStructureNV)mvk_get_handle(d, 0);
    src = (VkAccelerationStructureNV)mvk_get_handle(d, 0);
    scratch = (VkBuffer)mvk_get_handle(d, 0);
    scratchOffset = (VkDeviceSize)mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdBuildAccelerationStructureNV");
        return;
    }
    if (!c->fn->vkCmdBuildAccelerationStructureNV) {
        mvk_unsupported(c, "vkCmdBuildAccelerationStructureNV");
        return;
    }
    c->fn->vkCmdBuildAccelerationStructureNV(commandBuffer, pInfo, instanceData, instanceOffset, update, dst, src, scratch, scratchOffset);
}

static void run_vkCmdCopyAccelerationStructureNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkAccelerationStructureNV dst;
    VkAccelerationStructureNV src;
    VkCopyAccelerationStructureModeKHR mode;
    dst = (VkAccelerationStructureNV)mvk_get_handle(d, 0);
    src = (VkAccelerationStructureNV)mvk_get_handle(d, 0);
    mode = (VkCopyAccelerationStructureModeKHR)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyAccelerationStructureNV");
        return;
    }
    if (!c->fn->vkCmdCopyAccelerationStructureNV) {
        mvk_unsupported(c, "vkCmdCopyAccelerationStructureNV");
        return;
    }
    c->fn->vkCmdCopyAccelerationStructureNV(commandBuffer, dst, src, mode);
}

static void run_vkCmdTraceRaysNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer raygenShaderBindingTableBuffer;
    VkDeviceSize raygenShaderBindingOffset;
    VkBuffer missShaderBindingTableBuffer;
    VkDeviceSize missShaderBindingOffset;
    VkDeviceSize missShaderBindingStride;
    VkBuffer hitShaderBindingTableBuffer;
    VkDeviceSize hitShaderBindingOffset;
    VkDeviceSize hitShaderBindingStride;
    VkBuffer callableShaderBindingTableBuffer;
    VkDeviceSize callableShaderBindingOffset;
    VkDeviceSize callableShaderBindingStride;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    raygenShaderBindingTableBuffer = (VkBuffer)mvk_get_handle(d, 0);
    raygenShaderBindingOffset = (VkDeviceSize)mvk_get_u64(d);
    missShaderBindingTableBuffer = (VkBuffer)mvk_get_handle(d, 0);
    missShaderBindingOffset = (VkDeviceSize)mvk_get_u64(d);
    missShaderBindingStride = (VkDeviceSize)mvk_get_u64(d);
    hitShaderBindingTableBuffer = (VkBuffer)mvk_get_handle(d, 0);
    hitShaderBindingOffset = (VkDeviceSize)mvk_get_u64(d);
    hitShaderBindingStride = (VkDeviceSize)mvk_get_u64(d);
    callableShaderBindingTableBuffer = (VkBuffer)mvk_get_handle(d, 0);
    callableShaderBindingOffset = (VkDeviceSize)mvk_get_u64(d);
    callableShaderBindingStride = (VkDeviceSize)mvk_get_u64(d);
    width = (uint32_t)mvk_get_u32(d);
    height = (uint32_t)mvk_get_u32(d);
    depth = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdTraceRaysNV");
        return;
    }
    if (!c->fn->vkCmdTraceRaysNV) {
        mvk_unsupported(c, "vkCmdTraceRaysNV");
        return;
    }
    c->fn->vkCmdTraceRaysNV(commandBuffer, raygenShaderBindingTableBuffer, raygenShaderBindingOffset, missShaderBindingTableBuffer, missShaderBindingOffset, missShaderBindingStride, hitShaderBindingTableBuffer, hitShaderBindingOffset, hitShaderBindingStride, callableShaderBindingTableBuffer, callableShaderBindingOffset, callableShaderBindingStride, width, height, depth);
}

static void run_vkCreateRayTracingPipelinesNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineCache pipelineCache;
    uint32_t createInfoCount;
    VkRayTracingPipelineCreateInfoNV * pCreateInfos;
    device = (VkDevice)mvk_get_handle(d, 1);
    pipelineCache = (VkPipelineCache)mvk_get_handle(d, 0);
    createInfoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkRayTracingPipelineCreateInfoNV *items = mvk_alloc(d, (size_t)(createInfoCount) * sizeof(VkRayTracingPipelineCreateInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
            mvk_get_VkRayTracingPipelineCreateInfoNV(d, &items[i]);
        pCreateInfos = items;
    } else {
        pCreateInfos = NULL;
    }
    uint64_t *ids_pPipelines = mvk_alloc(d, sizeof(uint64_t) * (size_t)(createInfoCount));
    VkPipeline *pPipelines = mvk_alloc(d, sizeof(VkPipeline) * (size_t)(createInfoCount));
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
        ids_pPipelines[i] = mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkCreateRayTracingPipelinesNV");
        return;
    }
    if (!c->fn->vkCreateRayTracingPipelinesNV) {
        mvk_unsupported(c, "vkCreateRayTracingPipelinesNV");
        return;
    }
    VkResult result = c->fn->vkCreateRayTracingPipelinesNV(device, pipelineCache, createInfoCount, pCreateInfos, NULL, pPipelines);
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++) {
        mvk_bind_id(c, ids_pPipelines[i], (uint64_t)pPipelines[i]);
        mvk_track(c->conn, (uint64_t)pPipelines[i], MVK_KIND_VkPipeline);
    }
    mvk_check(c, result, "vkCreateRayTracingPipelinesNV");
}

static void run_vkGetAccelerationStructureHandleNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkAccelerationStructureNV accelerationStructure;
    size_t dataSize;
    device = (VkDevice)mvk_get_handle(d, 1);
    accelerationStructure = (VkAccelerationStructureNV)mvk_get_handle(d, 0);
    dataSize = (size_t)mvk_get_u64(d);
    int want_pData = mvk_get_u32(d);
    uint8_t *pData = NULL;
    if (want_pData) {
        pData = mvk_alloc(d, sizeof(uint8_t) * (size_t)(dataSize) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetAccelerationStructureHandleNV");
        return;
    }
    if (!c->fn->vkGetAccelerationStructureHandleNV) {
        mvk_unsupported(c, "vkGetAccelerationStructureHandleNV");
        return;
    }
    VkResult result = c->fn->vkGetAccelerationStructureHandleNV(device, accelerationStructure, dataSize, pData);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, pData != NULL);
    if (pData) {
        mvk_put_u32(e, (uint32_t)(dataSize));
        mvk_put_bytes(e, pData, (size_t)(dataSize));
    }
    mvk_reply_end(c);
}

static void run_vkCmdWriteAccelerationStructuresPropertiesNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t accelerationStructureCount;
    VkAccelerationStructureNV * pAccelerationStructures;
    VkQueryType queryType;
    VkQueryPool queryPool;
    uint32_t firstQuery;
    accelerationStructureCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureNV *items = mvk_alloc(d, (size_t)(accelerationStructureCount) * sizeof(VkAccelerationStructureNV));
        for (uint32_t i = 0; i < (uint32_t)(accelerationStructureCount); i++)
            items[i] = (VkAccelerationStructureNV)mvk_get_handle(d, 0);
        pAccelerationStructures = items;
    } else {
        pAccelerationStructures = NULL;
    }
    queryType = (VkQueryType)mvk_get_u32(d);
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    firstQuery = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdWriteAccelerationStructuresPropertiesNV");
        return;
    }
    if (!c->fn->vkCmdWriteAccelerationStructuresPropertiesNV) {
        mvk_unsupported(c, "vkCmdWriteAccelerationStructuresPropertiesNV");
        return;
    }
    c->fn->vkCmdWriteAccelerationStructuresPropertiesNV(commandBuffer, accelerationStructureCount, pAccelerationStructures, queryType, queryPool, firstQuery);
}

static void run_vkCompileDeferredNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipeline pipeline;
    uint32_t shader;
    device = (VkDevice)mvk_get_handle(d, 1);
    pipeline = (VkPipeline)mvk_get_handle(d, 0);
    shader = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCompileDeferredNV");
        return;
    }
    if (!c->fn->vkCompileDeferredNV) {
        mvk_unsupported(c, "vkCompileDeferredNV");
        return;
    }
    VkResult result = c->fn->vkCompileDeferredNV(device, pipeline, shader);
    mvk_check(c, result, "vkCompileDeferredNV");
}

static void run_vkCmdWriteBufferMarkerAMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineStageFlagBits pipelineStage;
    VkBuffer dstBuffer;
    VkDeviceSize dstOffset;
    uint32_t marker;
    pipelineStage = (VkPipelineStageFlagBits)mvk_get_u32(d);
    dstBuffer = (VkBuffer)mvk_get_handle(d, 0);
    dstOffset = (VkDeviceSize)mvk_get_u64(d);
    marker = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdWriteBufferMarkerAMD");
        return;
    }
    if (!c->fn->vkCmdWriteBufferMarkerAMD) {
        mvk_unsupported(c, "vkCmdWriteBufferMarkerAMD");
        return;
    }
    c->fn->vkCmdWriteBufferMarkerAMD(commandBuffer, pipelineStage, dstBuffer, dstOffset, marker);
}

static void run_vkCmdWriteBufferMarker2AMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineStageFlags2 stage;
    VkBuffer dstBuffer;
    VkDeviceSize dstOffset;
    uint32_t marker;
    stage = (VkPipelineStageFlags2)mvk_get_u64(d);
    dstBuffer = (VkBuffer)mvk_get_handle(d, 0);
    dstOffset = (VkDeviceSize)mvk_get_u64(d);
    marker = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdWriteBufferMarker2AMD");
        return;
    }
    if (!c->fn->vkCmdWriteBufferMarker2AMD) {
        mvk_unsupported(c, "vkCmdWriteBufferMarker2AMD");
        return;
    }
    c->fn->vkCmdWriteBufferMarker2AMD(commandBuffer, stage, dstBuffer, dstOffset, marker);
}

static void run_vkCmdDrawMeshTasksNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t taskCount;
    uint32_t firstTask;
    taskCount = (uint32_t)mvk_get_u32(d);
    firstTask = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawMeshTasksNV");
        return;
    }
    if (!c->fn->vkCmdDrawMeshTasksNV) {
        mvk_unsupported(c, "vkCmdDrawMeshTasksNV");
        return;
    }
    c->fn->vkCmdDrawMeshTasksNV(commandBuffer, taskCount, firstTask);
}

static void run_vkCmdDrawMeshTasksIndirectNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer buffer;
    VkDeviceSize offset;
    uint32_t drawCount;
    uint32_t stride;
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    offset = (VkDeviceSize)mvk_get_u64(d);
    drawCount = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawMeshTasksIndirectNV");
        return;
    }
    if (!c->fn->vkCmdDrawMeshTasksIndirectNV) {
        mvk_unsupported(c, "vkCmdDrawMeshTasksIndirectNV");
        return;
    }
    c->fn->vkCmdDrawMeshTasksIndirectNV(commandBuffer, buffer, offset, drawCount, stride);
}

static void run_vkCmdDrawMeshTasksIndirectCountNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer buffer;
    VkDeviceSize offset;
    VkBuffer countBuffer;
    VkDeviceSize countBufferOffset;
    uint32_t maxDrawCount;
    uint32_t stride;
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    offset = (VkDeviceSize)mvk_get_u64(d);
    countBuffer = (VkBuffer)mvk_get_handle(d, 0);
    countBufferOffset = (VkDeviceSize)mvk_get_u64(d);
    maxDrawCount = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawMeshTasksIndirectCountNV");
        return;
    }
    if (!c->fn->vkCmdDrawMeshTasksIndirectCountNV) {
        mvk_unsupported(c, "vkCmdDrawMeshTasksIndirectCountNV");
        return;
    }
    c->fn->vkCmdDrawMeshTasksIndirectCountNV(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

static void run_vkCmdSetExclusiveScissorEnableNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstExclusiveScissor;
    uint32_t exclusiveScissorCount;
    VkBool32 * pExclusiveScissorEnables;
    firstExclusiveScissor = (uint32_t)mvk_get_u32(d);
    exclusiveScissorCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(exclusiveScissorCount) * sizeof(VkBool32));
        mvk_get_bytes(d, items, (size_t)(exclusiveScissorCount) * sizeof(VkBool32));
        pExclusiveScissorEnables = items;
    } else {
        pExclusiveScissorEnables = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetExclusiveScissorEnableNV");
        return;
    }
    if (!c->fn->vkCmdSetExclusiveScissorEnableNV) {
        mvk_unsupported(c, "vkCmdSetExclusiveScissorEnableNV");
        return;
    }
    c->fn->vkCmdSetExclusiveScissorEnableNV(commandBuffer, firstExclusiveScissor, exclusiveScissorCount, pExclusiveScissorEnables);
}

static void run_vkCmdSetExclusiveScissorNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstExclusiveScissor;
    uint32_t exclusiveScissorCount;
    VkRect2D * pExclusiveScissors;
    firstExclusiveScissor = (uint32_t)mvk_get_u32(d);
    exclusiveScissorCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkRect2D *items = mvk_alloc(d, (size_t)(exclusiveScissorCount) * sizeof(VkRect2D));
        for (uint32_t i = 0; i < (uint32_t)(exclusiveScissorCount); i++)
            mvk_get_VkRect2D(d, &items[i]);
        pExclusiveScissors = items;
    } else {
        pExclusiveScissors = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetExclusiveScissorNV");
        return;
    }
    if (!c->fn->vkCmdSetExclusiveScissorNV) {
        mvk_unsupported(c, "vkCmdSetExclusiveScissorNV");
        return;
    }
    c->fn->vkCmdSetExclusiveScissorNV(commandBuffer, firstExclusiveScissor, exclusiveScissorCount, pExclusiveScissors);
}

static void run_vkUninitializePerformanceApiINTEL(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (d->error) {
        mvk_malformed(c, "vkUninitializePerformanceApiINTEL");
        return;
    }
    if (!c->fn->vkUninitializePerformanceApiINTEL) {
        mvk_unsupported(c, "vkUninitializePerformanceApiINTEL");
        return;
    }
    c->fn->vkUninitializePerformanceApiINTEL(device);
}

static void run_vkAcquirePerformanceConfigurationINTEL(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPerformanceConfigurationAcquireInfoINTEL * pAcquireInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPerformanceConfigurationAcquireInfoINTEL *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPerformanceConfigurationAcquireInfoINTEL));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPerformanceConfigurationAcquireInfoINTEL(d, &items[i]);
        pAcquireInfo = items;
    } else {
        pAcquireInfo = NULL;
    }
    VkPerformanceConfigurationINTEL pConfiguration_value;
    memset(&pConfiguration_value, 0, sizeof(pConfiguration_value));
    VkPerformanceConfigurationINTEL *pConfiguration = &pConfiguration_value;
    if (d->error) {
        mvk_malformed(c, "vkAcquirePerformanceConfigurationINTEL");
        return;
    }
    if (!c->fn->vkAcquirePerformanceConfigurationINTEL) {
        mvk_unsupported(c, "vkAcquirePerformanceConfigurationINTEL");
        return;
    }
    VkResult result = c->fn->vkAcquirePerformanceConfigurationINTEL(device, pAcquireInfo, pConfiguration);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u64(e, mvk_host_id(e, (uint64_t)*pConfiguration));
    mvk_reply_end(c);
}

static void run_vkReleasePerformanceConfigurationINTEL(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPerformanceConfigurationINTEL configuration;
    device = (VkDevice)mvk_get_handle(d, 1);
    configuration = (VkPerformanceConfigurationINTEL)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkReleasePerformanceConfigurationINTEL");
        return;
    }
    if (!c->fn->vkReleasePerformanceConfigurationINTEL) {
        mvk_unsupported(c, "vkReleasePerformanceConfigurationINTEL");
        return;
    }
    VkResult result = c->fn->vkReleasePerformanceConfigurationINTEL(device, configuration);
    mvk_check(c, result, "vkReleasePerformanceConfigurationINTEL");
}

static void run_vkQueueSetPerformanceConfigurationINTEL(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkQueue queue;
    VkPerformanceConfigurationINTEL configuration;
    queue = (VkQueue)mvk_get_handle(d, 1);
    configuration = (VkPerformanceConfigurationINTEL)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkQueueSetPerformanceConfigurationINTEL");
        return;
    }
    if (!c->fn->vkQueueSetPerformanceConfigurationINTEL) {
        mvk_unsupported(c, "vkQueueSetPerformanceConfigurationINTEL");
        return;
    }
    VkResult result = c->fn->vkQueueSetPerformanceConfigurationINTEL(queue, configuration);
    mvk_check(c, result, "vkQueueSetPerformanceConfigurationINTEL");
}

static void run_vkGetPerformanceParameterINTEL(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPerformanceParameterTypeINTEL parameter;
    device = (VkDevice)mvk_get_handle(d, 1);
    parameter = (VkPerformanceParameterTypeINTEL)mvk_get_u32(d);
    VkPerformanceValueINTEL pValue_value;
    memset(&pValue_value, 0, sizeof(pValue_value));
    VkPerformanceValueINTEL *pValue = &pValue_value;
    if (d->error) {
        mvk_malformed(c, "vkGetPerformanceParameterINTEL");
        return;
    }
    if (!c->fn->vkGetPerformanceParameterINTEL) {
        mvk_unsupported(c, "vkGetPerformanceParameterINTEL");
        return;
    }
    VkResult result = c->fn->vkGetPerformanceParameterINTEL(device, parameter, pValue);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    reply_VkPerformanceValueINTEL(e, pValue);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceFragmentShadingRatesKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    uint32_t pFragmentShadingRateCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pFragmentShadingRateCount = &pFragmentShadingRateCount_value;
    int want_pFragmentShadingRates = mvk_get_u32(d);
    VkPhysicalDeviceFragmentShadingRateKHR *pFragmentShadingRates = NULL;
    uint32_t shape_count_pFragmentShadingRates = 0;
    if (want_pFragmentShadingRates) {
        pFragmentShadingRates = mvk_alloc(d, sizeof(VkPhysicalDeviceFragmentShadingRateKHR) * (size_t)(*pFragmentShadingRateCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pFragmentShadingRateCount); i++)
            mvk_get_out_shape_into(d, &pFragmentShadingRates[i], sizeof(VkPhysicalDeviceFragmentShadingRateKHR));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceFragmentShadingRatesKHR");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceFragmentShadingRatesKHR) {
        mvk_unsupported(c, "vkGetPhysicalDeviceFragmentShadingRatesKHR");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceFragmentShadingRatesKHR(physicalDevice, pFragmentShadingRateCount, pFragmentShadingRates);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pFragmentShadingRateCount));
    mvk_put_u32(e, pFragmentShadingRates != NULL);
    if (pFragmentShadingRates) {
        mvk_put_u32(e, (uint32_t)(*pFragmentShadingRateCount));
        for (uint32_t i = 0; i < (uint32_t)(*pFragmentShadingRateCount); i++)
            reply_VkPhysicalDeviceFragmentShadingRateKHR(e, &pFragmentShadingRates[i]);
    }
    mvk_reply_end(c);
}

static void run_vkCmdSetFragmentShadingRateKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkExtent2D * pFragmentSize;
    VkFragmentShadingRateCombinerOpKHR combinerOps[2];
    if (mvk_get_u32(d)) {
        VkExtent2D *items = mvk_alloc(d, (size_t)(1) * sizeof(VkExtent2D));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkExtent2D(d, &items[i]);
        pFragmentSize = items;
    } else {
        pFragmentSize = NULL;
    }
    mvk_get_bytes(d, combinerOps, (size_t)(2) * sizeof((combinerOps)[0]));
    if (d->error) {
        mvk_malformed(c, "vkCmdSetFragmentShadingRateKHR");
        return;
    }
    if (!c->fn->vkCmdSetFragmentShadingRateKHR) {
        mvk_unsupported(c, "vkCmdSetFragmentShadingRateKHR");
        return;
    }
    c->fn->vkCmdSetFragmentShadingRateKHR(commandBuffer, pFragmentSize, combinerOps);
}

static void run_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    uint32_t pPropertyCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pPropertyCount = &pPropertyCount_value;
    int want_pProperties = mvk_get_u32(d);
    VkCooperativeMatrixPropertiesNV *pProperties = NULL;
    uint32_t shape_count_pProperties = 0;
    if (want_pProperties) {
        pProperties = mvk_alloc(d, sizeof(VkCooperativeMatrixPropertiesNV) * (size_t)(*pPropertyCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            mvk_get_out_shape_into(d, &pProperties[i], sizeof(VkCooperativeMatrixPropertiesNV));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceCooperativeMatrixPropertiesNV");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceCooperativeMatrixPropertiesNV) {
        mvk_unsupported(c, "vkGetPhysicalDeviceCooperativeMatrixPropertiesNV");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceCooperativeMatrixPropertiesNV(physicalDevice, pPropertyCount, pProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pPropertyCount));
    mvk_put_u32(e, pProperties != NULL);
    if (pProperties) {
        mvk_put_u32(e, (uint32_t)(*pPropertyCount));
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            reply_VkCooperativeMatrixPropertiesNV(e, &pProperties[i]);
    }
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    uint32_t pCombinationCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pCombinationCount = &pCombinationCount_value;
    int want_pCombinations = mvk_get_u32(d);
    VkFramebufferMixedSamplesCombinationNV *pCombinations = NULL;
    uint32_t shape_count_pCombinations = 0;
    if (want_pCombinations) {
        pCombinations = mvk_alloc(d, sizeof(VkFramebufferMixedSamplesCombinationNV) * (size_t)(*pCombinationCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pCombinationCount); i++)
            mvk_get_out_shape_into(d, &pCombinations[i], sizeof(VkFramebufferMixedSamplesCombinationNV));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV) {
        mvk_unsupported(c, "vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV(physicalDevice, pCombinationCount, pCombinations);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pCombinationCount));
    mvk_put_u32(e, pCombinations != NULL);
    if (pCombinations) {
        mvk_put_u32(e, (uint32_t)(*pCombinationCount));
        for (uint32_t i = 0; i < (uint32_t)(*pCombinationCount); i++)
            reply_VkFramebufferMixedSamplesCombinationNV(e, &pCombinations[i]);
    }
    mvk_reply_end(c);
}

static void run_vkCreateDeferredOperationKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    device = (VkDevice)mvk_get_handle(d, 1);
    uint64_t id_pDeferredOperation = mvk_get_u64(d);
    VkDeferredOperationKHR pDeferredOperation_value = VK_NULL_HANDLE;
    VkDeferredOperationKHR *pDeferredOperation = &pDeferredOperation_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateDeferredOperationKHR");
        return;
    }
    if (!c->fn->vkCreateDeferredOperationKHR) {
        mvk_unsupported(c, "vkCreateDeferredOperationKHR");
        return;
    }
    VkResult result = c->fn->vkCreateDeferredOperationKHR(device, NULL, pDeferredOperation);
    mvk_bind_id(c, id_pDeferredOperation, (uint64_t)pDeferredOperation_value);
    mvk_track(c->conn, (uint64_t)pDeferredOperation_value, MVK_KIND_VkDeferredOperationKHR);
    mvk_check(c, result, "vkCreateDeferredOperationKHR");
}

static void run_vkDestroyDeferredOperationKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR operation;
    device = (VkDevice)mvk_get_handle(d, 1);
    operation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyDeferredOperationKHR");
        return;
    }
    if (!c->fn->vkDestroyDeferredOperationKHR) {
        mvk_unsupported(c, "vkDestroyDeferredOperationKHR");
        return;
    }
    c->fn->vkDestroyDeferredOperationKHR(device, operation, NULL);
    mvk_forget(c->conn, (uint64_t)operation);
}

static void run_vkGetDeferredOperationMaxConcurrencyKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR operation;
    device = (VkDevice)mvk_get_handle(d, 1);
    operation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkGetDeferredOperationMaxConcurrencyKHR");
        return;
    }
    if (!c->fn->vkGetDeferredOperationMaxConcurrencyKHR) {
        mvk_unsupported(c, "vkGetDeferredOperationMaxConcurrencyKHR");
        return;
    }
    uint32_t result = c->fn->vkGetDeferredOperationMaxConcurrencyKHR(device, operation);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(result));
    mvk_reply_end(c);
}

static void run_vkGetDeferredOperationResultKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR operation;
    device = (VkDevice)mvk_get_handle(d, 1);
    operation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkGetDeferredOperationResultKHR");
        return;
    }
    if (!c->fn->vkGetDeferredOperationResultKHR) {
        mvk_unsupported(c, "vkGetDeferredOperationResultKHR");
        return;
    }
    VkResult result = c->fn->vkGetDeferredOperationResultKHR(device, operation);
    mvk_check(c, result, "vkGetDeferredOperationResultKHR");
}

static void run_vkDeferredOperationJoinKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR operation;
    device = (VkDevice)mvk_get_handle(d, 1);
    operation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDeferredOperationJoinKHR");
        return;
    }
    if (!c->fn->vkDeferredOperationJoinKHR) {
        mvk_unsupported(c, "vkDeferredOperationJoinKHR");
        return;
    }
    VkResult result = c->fn->vkDeferredOperationJoinKHR(device, operation);
    mvk_check(c, result, "vkDeferredOperationJoinKHR");
}

static void run_vkGetPipelineExecutablePropertiesKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineInfoKHR * pPipelineInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPipelineInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPipelineInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPipelineInfoKHR(d, &items[i]);
        pPipelineInfo = items;
    } else {
        pPipelineInfo = NULL;
    }
    uint32_t pExecutableCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pExecutableCount = &pExecutableCount_value;
    int want_pProperties = mvk_get_u32(d);
    VkPipelineExecutablePropertiesKHR *pProperties = NULL;
    uint32_t shape_count_pProperties = 0;
    if (want_pProperties) {
        pProperties = mvk_alloc(d, sizeof(VkPipelineExecutablePropertiesKHR) * (size_t)(*pExecutableCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pExecutableCount); i++)
            mvk_get_out_shape_into(d, &pProperties[i], sizeof(VkPipelineExecutablePropertiesKHR));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPipelineExecutablePropertiesKHR");
        return;
    }
    if (!c->fn->vkGetPipelineExecutablePropertiesKHR) {
        mvk_unsupported(c, "vkGetPipelineExecutablePropertiesKHR");
        return;
    }
    VkResult result = c->fn->vkGetPipelineExecutablePropertiesKHR(device, pPipelineInfo, pExecutableCount, pProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pExecutableCount));
    mvk_put_u32(e, pProperties != NULL);
    if (pProperties) {
        mvk_put_u32(e, (uint32_t)(*pExecutableCount));
        for (uint32_t i = 0; i < (uint32_t)(*pExecutableCount); i++)
            reply_VkPipelineExecutablePropertiesKHR(e, &pProperties[i]);
    }
    mvk_reply_end(c);
}

static void run_vkGetPipelineExecutableStatisticsKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineExecutableInfoKHR * pExecutableInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPipelineExecutableInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPipelineExecutableInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPipelineExecutableInfoKHR(d, &items[i]);
        pExecutableInfo = items;
    } else {
        pExecutableInfo = NULL;
    }
    uint32_t pStatisticCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pStatisticCount = &pStatisticCount_value;
    int want_pStatistics = mvk_get_u32(d);
    VkPipelineExecutableStatisticKHR *pStatistics = NULL;
    uint32_t shape_count_pStatistics = 0;
    if (want_pStatistics) {
        pStatistics = mvk_alloc(d, sizeof(VkPipelineExecutableStatisticKHR) * (size_t)(*pStatisticCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pStatisticCount); i++)
            mvk_get_out_shape_into(d, &pStatistics[i], sizeof(VkPipelineExecutableStatisticKHR));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPipelineExecutableStatisticsKHR");
        return;
    }
    if (!c->fn->vkGetPipelineExecutableStatisticsKHR) {
        mvk_unsupported(c, "vkGetPipelineExecutableStatisticsKHR");
        return;
    }
    VkResult result = c->fn->vkGetPipelineExecutableStatisticsKHR(device, pExecutableInfo, pStatisticCount, pStatistics);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pStatisticCount));
    mvk_put_u32(e, pStatistics != NULL);
    if (pStatistics) {
        mvk_put_u32(e, (uint32_t)(*pStatisticCount));
        for (uint32_t i = 0; i < (uint32_t)(*pStatisticCount); i++)
            reply_VkPipelineExecutableStatisticKHR(e, &pStatistics[i]);
    }
    mvk_reply_end(c);
}

static void run_vkGetPipelineExecutableInternalRepresentationsKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineExecutableInfoKHR * pExecutableInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPipelineExecutableInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPipelineExecutableInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPipelineExecutableInfoKHR(d, &items[i]);
        pExecutableInfo = items;
    } else {
        pExecutableInfo = NULL;
    }
    uint32_t pInternalRepresentationCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pInternalRepresentationCount = &pInternalRepresentationCount_value;
    int want_pInternalRepresentations = mvk_get_u32(d);
    VkPipelineExecutableInternalRepresentationKHR *pInternalRepresentations = NULL;
    uint32_t shape_count_pInternalRepresentations = 0;
    if (want_pInternalRepresentations) {
        pInternalRepresentations = mvk_alloc(d, sizeof(VkPipelineExecutableInternalRepresentationKHR) * (size_t)(*pInternalRepresentationCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pInternalRepresentationCount); i++)
            mvk_get_out_shape_into(d, &pInternalRepresentations[i], sizeof(VkPipelineExecutableInternalRepresentationKHR));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPipelineExecutableInternalRepresentationsKHR");
        return;
    }
    if (!c->fn->vkGetPipelineExecutableInternalRepresentationsKHR) {
        mvk_unsupported(c, "vkGetPipelineExecutableInternalRepresentationsKHR");
        return;
    }
    VkResult result = c->fn->vkGetPipelineExecutableInternalRepresentationsKHR(device, pExecutableInfo, pInternalRepresentationCount, pInternalRepresentations);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pInternalRepresentationCount));
    mvk_put_u32(e, pInternalRepresentations != NULL);
    if (pInternalRepresentations) {
        mvk_put_u32(e, (uint32_t)(*pInternalRepresentationCount));
        for (uint32_t i = 0; i < (uint32_t)(*pInternalRepresentationCount); i++)
            reply_VkPipelineExecutableInternalRepresentationKHR(e, &pInternalRepresentations[i]);
    }
    mvk_reply_end(c);
}

static void run_vkGetGeneratedCommandsMemoryRequirementsNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkGeneratedCommandsMemoryRequirementsInfoNV * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkGeneratedCommandsMemoryRequirementsInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkGeneratedCommandsMemoryRequirementsInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkGeneratedCommandsMemoryRequirementsInfoNV(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    VkMemoryRequirements2 *pMemoryRequirements = mvk_get_out_shape(d, sizeof(VkMemoryRequirements2));
    if (d->error) {
        mvk_malformed(c, "vkGetGeneratedCommandsMemoryRequirementsNV");
        return;
    }
    if (!c->fn->vkGetGeneratedCommandsMemoryRequirementsNV) {
        mvk_unsupported(c, "vkGetGeneratedCommandsMemoryRequirementsNV");
        return;
    }
    c->fn->vkGetGeneratedCommandsMemoryRequirementsNV(device, pInfo, pMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkMemoryRequirements2(e, pMemoryRequirements);
    mvk_reply_end(c);
}

static void run_vkCmdPreprocessGeneratedCommandsNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkGeneratedCommandsInfoNV * pGeneratedCommandsInfo;
    if (mvk_get_u32(d)) {
        VkGeneratedCommandsInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkGeneratedCommandsInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkGeneratedCommandsInfoNV(d, &items[i]);
        pGeneratedCommandsInfo = items;
    } else {
        pGeneratedCommandsInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdPreprocessGeneratedCommandsNV");
        return;
    }
    if (!c->fn->vkCmdPreprocessGeneratedCommandsNV) {
        mvk_unsupported(c, "vkCmdPreprocessGeneratedCommandsNV");
        return;
    }
    c->fn->vkCmdPreprocessGeneratedCommandsNV(commandBuffer, pGeneratedCommandsInfo);
}

static void run_vkCmdExecuteGeneratedCommandsNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 isPreprocessed;
    VkGeneratedCommandsInfoNV * pGeneratedCommandsInfo;
    isPreprocessed = (VkBool32)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkGeneratedCommandsInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkGeneratedCommandsInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkGeneratedCommandsInfoNV(d, &items[i]);
        pGeneratedCommandsInfo = items;
    } else {
        pGeneratedCommandsInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdExecuteGeneratedCommandsNV");
        return;
    }
    if (!c->fn->vkCmdExecuteGeneratedCommandsNV) {
        mvk_unsupported(c, "vkCmdExecuteGeneratedCommandsNV");
        return;
    }
    c->fn->vkCmdExecuteGeneratedCommandsNV(commandBuffer, isPreprocessed, pGeneratedCommandsInfo);
}

static void run_vkCmdBindPipelineShaderGroupNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineBindPoint pipelineBindPoint;
    VkPipeline pipeline;
    uint32_t groupIndex;
    pipelineBindPoint = (VkPipelineBindPoint)mvk_get_u32(d);
    pipeline = (VkPipeline)mvk_get_handle(d, 0);
    groupIndex = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdBindPipelineShaderGroupNV");
        return;
    }
    if (!c->fn->vkCmdBindPipelineShaderGroupNV) {
        mvk_unsupported(c, "vkCmdBindPipelineShaderGroupNV");
        return;
    }
    c->fn->vkCmdBindPipelineShaderGroupNV(commandBuffer, pipelineBindPoint, pipeline, groupIndex);
}

static void run_vkCreateIndirectCommandsLayoutNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkIndirectCommandsLayoutCreateInfoNV * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkIndirectCommandsLayoutCreateInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkIndirectCommandsLayoutCreateInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkIndirectCommandsLayoutCreateInfoNV(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pIndirectCommandsLayout = mvk_get_u64(d);
    VkIndirectCommandsLayoutNV pIndirectCommandsLayout_value = VK_NULL_HANDLE;
    VkIndirectCommandsLayoutNV *pIndirectCommandsLayout = &pIndirectCommandsLayout_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateIndirectCommandsLayoutNV");
        return;
    }
    if (!c->fn->vkCreateIndirectCommandsLayoutNV) {
        mvk_unsupported(c, "vkCreateIndirectCommandsLayoutNV");
        return;
    }
    VkResult result = c->fn->vkCreateIndirectCommandsLayoutNV(device, pCreateInfo, NULL, pIndirectCommandsLayout);
    mvk_bind_id(c, id_pIndirectCommandsLayout, (uint64_t)pIndirectCommandsLayout_value);
    mvk_track(c->conn, (uint64_t)pIndirectCommandsLayout_value, MVK_KIND_VkIndirectCommandsLayoutNV);
    mvk_check(c, result, "vkCreateIndirectCommandsLayoutNV");
}

static void run_vkDestroyIndirectCommandsLayoutNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkIndirectCommandsLayoutNV indirectCommandsLayout;
    device = (VkDevice)mvk_get_handle(d, 1);
    indirectCommandsLayout = (VkIndirectCommandsLayoutNV)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyIndirectCommandsLayoutNV");
        return;
    }
    if (!c->fn->vkDestroyIndirectCommandsLayoutNV) {
        mvk_unsupported(c, "vkDestroyIndirectCommandsLayoutNV");
        return;
    }
    c->fn->vkDestroyIndirectCommandsLayoutNV(device, indirectCommandsLayout, NULL);
    mvk_forget(c->conn, (uint64_t)indirectCommandsLayout);
}

static void run_vkCmdSetDepthBias2EXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDepthBiasInfoEXT * pDepthBiasInfo;
    if (mvk_get_u32(d)) {
        VkDepthBiasInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDepthBiasInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDepthBiasInfoEXT(d, &items[i]);
        pDepthBiasInfo = items;
    } else {
        pDepthBiasInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDepthBias2EXT");
        return;
    }
    if (!c->fn->vkCmdSetDepthBias2EXT) {
        mvk_unsupported(c, "vkCmdSetDepthBias2EXT");
        return;
    }
    c->fn->vkCmdSetDepthBias2EXT(commandBuffer, pDepthBiasInfo);
}

static void run_vkQueueSetPerfHintQCOM(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkQueue queue;
    VkPerfHintInfoQCOM * pPerfHintInfo;
    queue = (VkQueue)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPerfHintInfoQCOM *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPerfHintInfoQCOM));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPerfHintInfoQCOM(d, &items[i]);
        pPerfHintInfo = items;
    } else {
        pPerfHintInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkQueueSetPerfHintQCOM");
        return;
    }
    if (!c->fn->vkQueueSetPerfHintQCOM) {
        mvk_unsupported(c, "vkQueueSetPerfHintQCOM");
        return;
    }
    VkResult result = c->fn->vkQueueSetPerfHintQCOM(queue, pPerfHintInfo);
    mvk_check(c, result, "vkQueueSetPerfHintQCOM");
}

static void run_vkSetLatencySleepModeLegacyNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkBool32 lowLatencyMode;
    VkBool32 lowLatencyBoost;
    uint32_t minimumIntervalUs;
    device = (VkDevice)mvk_get_handle(d, 1);
    lowLatencyMode = (VkBool32)mvk_get_u32(d);
    lowLatencyBoost = (VkBool32)mvk_get_u32(d);
    minimumIntervalUs = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkSetLatencySleepModeLegacyNV");
        return;
    }
    if (!c->fn->vkSetLatencySleepModeLegacyNV) {
        mvk_unsupported(c, "vkSetLatencySleepModeLegacyNV");
        return;
    }
    c->fn->vkSetLatencySleepModeLegacyNV(device, lowLatencyMode, lowLatencyBoost, minimumIntervalUs);
}

static void run_vkLatencySleepLegacyNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkSemaphore signalSemaphore;
    uint64_t value;
    device = (VkDevice)mvk_get_handle(d, 1);
    signalSemaphore = (VkSemaphore)mvk_get_handle(d, 0);
    value = (uint64_t)mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkLatencySleepLegacyNV");
        return;
    }
    if (!c->fn->vkLatencySleepLegacyNV) {
        mvk_unsupported(c, "vkLatencySleepLegacyNV");
        return;
    }
    c->fn->vkLatencySleepLegacyNV(device, signalSemaphore, value);
}

static void run_vkSetLatencyMarkerLegacyNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint64_t frameID;
    uint32_t marker;
    device = (VkDevice)mvk_get_handle(d, 1);
    frameID = (uint64_t)mvk_get_u64(d);
    marker = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkSetLatencyMarkerLegacyNV");
        return;
    }
    if (!c->fn->vkSetLatencyMarkerLegacyNV) {
        mvk_unsupported(c, "vkSetLatencyMarkerLegacyNV");
        return;
    }
    c->fn->vkSetLatencyMarkerLegacyNV(device, frameID, marker);
}

static void run_vkQueueNotifyOutOfBandLegacyNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkQueue queue;
    uint32_t queueType;
    queue = (VkQueue)mvk_get_handle(d, 1);
    queueType = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkQueueNotifyOutOfBandLegacyNV");
        return;
    }
    if (!c->fn->vkQueueNotifyOutOfBandLegacyNV) {
        mvk_unsupported(c, "vkQueueNotifyOutOfBandLegacyNV");
        return;
    }
    c->fn->vkQueueNotifyOutOfBandLegacyNV(queue, queueType);
}

static void run_vkGetSleepStatusLegacyNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    device = (VkDevice)mvk_get_handle(d, 1);
    VkBool32 pLowLatencyMode_value;
    memset(&pLowLatencyMode_value, 0, sizeof(pLowLatencyMode_value));
    VkBool32 *pLowLatencyMode = &pLowLatencyMode_value;
    if (d->error) {
        mvk_malformed(c, "vkGetSleepStatusLegacyNV");
        return;
    }
    if (!c->fn->vkGetSleepStatusLegacyNV) {
        mvk_unsupported(c, "vkGetSleepStatusLegacyNV");
        return;
    }
    c->fn->vkGetSleepStatusLegacyNV(device, pLowLatencyMode);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(*pLowLatencyMode));
    mvk_reply_end(c);
}

static void run_vkShutdownLatencyDeviceLegacyNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (d->error) {
        mvk_malformed(c, "vkShutdownLatencyDeviceLegacyNV");
        return;
    }
    if (!c->fn->vkShutdownLatencyDeviceLegacyNV) {
        mvk_unsupported(c, "vkShutdownLatencyDeviceLegacyNV");
        return;
    }
    c->fn->vkShutdownLatencyDeviceLegacyNV(device);
}

static void run_vkGetDescriptorSetLayoutSizeEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDescriptorSetLayout layout;
    device = (VkDevice)mvk_get_handle(d, 1);
    layout = (VkDescriptorSetLayout)mvk_get_handle(d, 0);
    VkDeviceSize pLayoutSizeInBytes_value;
    memset(&pLayoutSizeInBytes_value, 0, sizeof(pLayoutSizeInBytes_value));
    VkDeviceSize *pLayoutSizeInBytes = &pLayoutSizeInBytes_value;
    if (d->error) {
        mvk_malformed(c, "vkGetDescriptorSetLayoutSizeEXT");
        return;
    }
    if (!c->fn->vkGetDescriptorSetLayoutSizeEXT) {
        mvk_unsupported(c, "vkGetDescriptorSetLayoutSizeEXT");
        return;
    }
    c->fn->vkGetDescriptorSetLayoutSizeEXT(device, layout, pLayoutSizeInBytes);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u64(e, (uint64_t)(*pLayoutSizeInBytes));
    mvk_reply_end(c);
}

static void run_vkGetDescriptorSetLayoutBindingOffsetEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDescriptorSetLayout layout;
    uint32_t binding;
    device = (VkDevice)mvk_get_handle(d, 1);
    layout = (VkDescriptorSetLayout)mvk_get_handle(d, 0);
    binding = (uint32_t)mvk_get_u32(d);
    VkDeviceSize pOffset_value;
    memset(&pOffset_value, 0, sizeof(pOffset_value));
    VkDeviceSize *pOffset = &pOffset_value;
    if (d->error) {
        mvk_malformed(c, "vkGetDescriptorSetLayoutBindingOffsetEXT");
        return;
    }
    if (!c->fn->vkGetDescriptorSetLayoutBindingOffsetEXT) {
        mvk_unsupported(c, "vkGetDescriptorSetLayoutBindingOffsetEXT");
        return;
    }
    c->fn->vkGetDescriptorSetLayoutBindingOffsetEXT(device, layout, binding, pOffset);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u64(e, (uint64_t)(*pOffset));
    mvk_reply_end(c);
}

static void run_vkCmdBindDescriptorBuffersEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t bufferCount;
    VkDescriptorBufferBindingInfoEXT * pBindingInfos;
    bufferCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkDescriptorBufferBindingInfoEXT *items = mvk_alloc(d, (size_t)(bufferCount) * sizeof(VkDescriptorBufferBindingInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(bufferCount); i++)
            mvk_get_VkDescriptorBufferBindingInfoEXT(d, &items[i]);
        pBindingInfos = items;
    } else {
        pBindingInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindDescriptorBuffersEXT");
        return;
    }
    if (!c->fn->vkCmdBindDescriptorBuffersEXT) {
        mvk_unsupported(c, "vkCmdBindDescriptorBuffersEXT");
        return;
    }
    c->fn->vkCmdBindDescriptorBuffersEXT(commandBuffer, bufferCount, pBindingInfos);
}

static void run_vkCmdSetDescriptorBufferOffsetsEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineBindPoint pipelineBindPoint;
    VkPipelineLayout layout;
    uint32_t firstSet;
    uint32_t setCount;
    uint32_t * pBufferIndices;
    VkDeviceSize * pOffsets;
    pipelineBindPoint = (VkPipelineBindPoint)mvk_get_u32(d);
    layout = (VkPipelineLayout)mvk_get_handle(d, 0);
    firstSet = (uint32_t)mvk_get_u32(d);
    setCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(setCount) * sizeof(uint32_t));
        mvk_get_bytes(d, items, (size_t)(setCount) * sizeof(uint32_t));
        pBufferIndices = items;
    } else {
        pBufferIndices = NULL;
    }
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(setCount) * sizeof(VkDeviceSize));
        mvk_get_bytes(d, items, (size_t)(setCount) * sizeof(VkDeviceSize));
        pOffsets = items;
    } else {
        pOffsets = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDescriptorBufferOffsetsEXT");
        return;
    }
    if (!c->fn->vkCmdSetDescriptorBufferOffsetsEXT) {
        mvk_unsupported(c, "vkCmdSetDescriptorBufferOffsetsEXT");
        return;
    }
    c->fn->vkCmdSetDescriptorBufferOffsetsEXT(commandBuffer, pipelineBindPoint, layout, firstSet, setCount, pBufferIndices, pOffsets);
}

static void run_vkCmdBindDescriptorBufferEmbeddedSamplersEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineBindPoint pipelineBindPoint;
    VkPipelineLayout layout;
    uint32_t set;
    pipelineBindPoint = (VkPipelineBindPoint)mvk_get_u32(d);
    layout = (VkPipelineLayout)mvk_get_handle(d, 0);
    set = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdBindDescriptorBufferEmbeddedSamplersEXT");
        return;
    }
    if (!c->fn->vkCmdBindDescriptorBufferEmbeddedSamplersEXT) {
        mvk_unsupported(c, "vkCmdBindDescriptorBufferEmbeddedSamplersEXT");
        return;
    }
    c->fn->vkCmdBindDescriptorBufferEmbeddedSamplersEXT(commandBuffer, pipelineBindPoint, layout, set);
}

static void run_vkCmdBindIndexBuffer3KHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBindIndexBuffer3InfoKHR * pInfo;
    if (mvk_get_u32(d)) {
        VkBindIndexBuffer3InfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBindIndexBuffer3InfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBindIndexBuffer3InfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindIndexBuffer3KHR");
        return;
    }
    if (!c->fn->vkCmdBindIndexBuffer3KHR) {
        mvk_unsupported(c, "vkCmdBindIndexBuffer3KHR");
        return;
    }
    c->fn->vkCmdBindIndexBuffer3KHR(commandBuffer, pInfo);
}

static void run_vkCmdBindVertexBuffers3KHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstBinding;
    uint32_t bindingCount;
    VkBindVertexBuffer3InfoKHR * pBindingInfos;
    firstBinding = (uint32_t)mvk_get_u32(d);
    bindingCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBindVertexBuffer3InfoKHR *items = mvk_alloc(d, (size_t)(bindingCount) * sizeof(VkBindVertexBuffer3InfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(bindingCount); i++)
            mvk_get_VkBindVertexBuffer3InfoKHR(d, &items[i]);
        pBindingInfos = items;
    } else {
        pBindingInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindVertexBuffers3KHR");
        return;
    }
    if (!c->fn->vkCmdBindVertexBuffers3KHR) {
        mvk_unsupported(c, "vkCmdBindVertexBuffers3KHR");
        return;
    }
    c->fn->vkCmdBindVertexBuffers3KHR(commandBuffer, firstBinding, bindingCount, pBindingInfos);
}

static void run_vkCmdDrawIndirect2KHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDrawIndirect2InfoKHR * pInfo;
    if (mvk_get_u32(d)) {
        VkDrawIndirect2InfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDrawIndirect2InfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDrawIndirect2InfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawIndirect2KHR");
        return;
    }
    if (!c->fn->vkCmdDrawIndirect2KHR) {
        mvk_unsupported(c, "vkCmdDrawIndirect2KHR");
        return;
    }
    c->fn->vkCmdDrawIndirect2KHR(commandBuffer, pInfo);
}

static void run_vkCmdDrawIndexedIndirect2KHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDrawIndirect2InfoKHR * pInfo;
    if (mvk_get_u32(d)) {
        VkDrawIndirect2InfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDrawIndirect2InfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDrawIndirect2InfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawIndexedIndirect2KHR");
        return;
    }
    if (!c->fn->vkCmdDrawIndexedIndirect2KHR) {
        mvk_unsupported(c, "vkCmdDrawIndexedIndirect2KHR");
        return;
    }
    c->fn->vkCmdDrawIndexedIndirect2KHR(commandBuffer, pInfo);
}

static void run_vkCmdDispatchIndirect2KHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDispatchIndirect2InfoKHR * pInfo;
    if (mvk_get_u32(d)) {
        VkDispatchIndirect2InfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDispatchIndirect2InfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDispatchIndirect2InfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdDispatchIndirect2KHR");
        return;
    }
    if (!c->fn->vkCmdDispatchIndirect2KHR) {
        mvk_unsupported(c, "vkCmdDispatchIndirect2KHR");
        return;
    }
    c->fn->vkCmdDispatchIndirect2KHR(commandBuffer, pInfo);
}

static void run_vkCmdCopyMemoryKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyDeviceMemoryInfoKHR * pCopyMemoryInfo;
    if (mvk_get_u32(d)) {
        VkCopyDeviceMemoryInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyDeviceMemoryInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyDeviceMemoryInfoKHR(d, &items[i]);
        pCopyMemoryInfo = items;
    } else {
        pCopyMemoryInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyMemoryKHR");
        return;
    }
    if (!c->fn->vkCmdCopyMemoryKHR) {
        mvk_unsupported(c, "vkCmdCopyMemoryKHR");
        return;
    }
    c->fn->vkCmdCopyMemoryKHR(commandBuffer, pCopyMemoryInfo);
}

static void run_vkCmdCopyMemoryToImageKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyDeviceMemoryImageInfoKHR * pCopyMemoryInfo;
    if (mvk_get_u32(d)) {
        VkCopyDeviceMemoryImageInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyDeviceMemoryImageInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyDeviceMemoryImageInfoKHR(d, &items[i]);
        pCopyMemoryInfo = items;
    } else {
        pCopyMemoryInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyMemoryToImageKHR");
        return;
    }
    if (!c->fn->vkCmdCopyMemoryToImageKHR) {
        mvk_unsupported(c, "vkCmdCopyMemoryToImageKHR");
        return;
    }
    c->fn->vkCmdCopyMemoryToImageKHR(commandBuffer, pCopyMemoryInfo);
}

static void run_vkCmdCopyImageToMemoryKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyDeviceMemoryImageInfoKHR * pCopyMemoryInfo;
    if (mvk_get_u32(d)) {
        VkCopyDeviceMemoryImageInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyDeviceMemoryImageInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyDeviceMemoryImageInfoKHR(d, &items[i]);
        pCopyMemoryInfo = items;
    } else {
        pCopyMemoryInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyImageToMemoryKHR");
        return;
    }
    if (!c->fn->vkCmdCopyImageToMemoryKHR) {
        mvk_unsupported(c, "vkCmdCopyImageToMemoryKHR");
        return;
    }
    c->fn->vkCmdCopyImageToMemoryKHR(commandBuffer, pCopyMemoryInfo);
}

static void run_vkCmdUpdateMemoryKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDeviceAddressRangeKHR * pDstRange;
    VkAddressCommandFlagsKHR dstFlags;
    VkDeviceSize dataSize;
    void * pData;
    if (mvk_get_u32(d)) {
        VkDeviceAddressRangeKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDeviceAddressRangeKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDeviceAddressRangeKHR(d, &items[i]);
        pDstRange = items;
    } else {
        pDstRange = NULL;
    }
    dstFlags = (VkAddressCommandFlagsKHR)mvk_get_u32(d);
    dataSize = (VkDeviceSize)mvk_get_u64(d);
    pData = (void *)mvk_get_blob(d, NULL);
    if (d->error) {
        mvk_malformed(c, "vkCmdUpdateMemoryKHR");
        return;
    }
    if (!c->fn->vkCmdUpdateMemoryKHR) {
        mvk_unsupported(c, "vkCmdUpdateMemoryKHR");
        return;
    }
    c->fn->vkCmdUpdateMemoryKHR(commandBuffer, pDstRange, dstFlags, dataSize, pData);
}

static void run_vkCmdFillMemoryKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDeviceAddressRangeKHR * pDstRange;
    VkAddressCommandFlagsKHR dstFlags;
    uint32_t data;
    if (mvk_get_u32(d)) {
        VkDeviceAddressRangeKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDeviceAddressRangeKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDeviceAddressRangeKHR(d, &items[i]);
        pDstRange = items;
    } else {
        pDstRange = NULL;
    }
    dstFlags = (VkAddressCommandFlagsKHR)mvk_get_u32(d);
    data = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdFillMemoryKHR");
        return;
    }
    if (!c->fn->vkCmdFillMemoryKHR) {
        mvk_unsupported(c, "vkCmdFillMemoryKHR");
        return;
    }
    c->fn->vkCmdFillMemoryKHR(commandBuffer, pDstRange, dstFlags, data);
}

static void run_vkCmdCopyQueryPoolResultsToMemoryKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkQueryPool queryPool;
    uint32_t firstQuery;
    uint32_t queryCount;
    VkStridedDeviceAddressRangeKHR * pDstRange;
    VkAddressCommandFlagsKHR dstFlags;
    VkQueryResultFlags queryResultFlags;
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    firstQuery = (uint32_t)mvk_get_u32(d);
    queryCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkStridedDeviceAddressRangeKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkStridedDeviceAddressRangeKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkStridedDeviceAddressRangeKHR(d, &items[i]);
        pDstRange = items;
    } else {
        pDstRange = NULL;
    }
    dstFlags = (VkAddressCommandFlagsKHR)mvk_get_u32(d);
    queryResultFlags = (VkQueryResultFlags)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyQueryPoolResultsToMemoryKHR");
        return;
    }
    if (!c->fn->vkCmdCopyQueryPoolResultsToMemoryKHR) {
        mvk_unsupported(c, "vkCmdCopyQueryPoolResultsToMemoryKHR");
        return;
    }
    c->fn->vkCmdCopyQueryPoolResultsToMemoryKHR(commandBuffer, queryPool, firstQuery, queryCount, pDstRange, dstFlags, queryResultFlags);
}

static void run_vkCmdDrawIndirectCount2KHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDrawIndirectCount2InfoKHR * pInfo;
    if (mvk_get_u32(d)) {
        VkDrawIndirectCount2InfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDrawIndirectCount2InfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDrawIndirectCount2InfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawIndirectCount2KHR");
        return;
    }
    if (!c->fn->vkCmdDrawIndirectCount2KHR) {
        mvk_unsupported(c, "vkCmdDrawIndirectCount2KHR");
        return;
    }
    c->fn->vkCmdDrawIndirectCount2KHR(commandBuffer, pInfo);
}

static void run_vkCmdDrawIndexedIndirectCount2KHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDrawIndirectCount2InfoKHR * pInfo;
    if (mvk_get_u32(d)) {
        VkDrawIndirectCount2InfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDrawIndirectCount2InfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDrawIndirectCount2InfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawIndexedIndirectCount2KHR");
        return;
    }
    if (!c->fn->vkCmdDrawIndexedIndirectCount2KHR) {
        mvk_unsupported(c, "vkCmdDrawIndexedIndirectCount2KHR");
        return;
    }
    c->fn->vkCmdDrawIndexedIndirectCount2KHR(commandBuffer, pInfo);
}

static void run_vkCmdBeginConditionalRendering2EXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkConditionalRenderingBeginInfo2EXT * pConditionalRenderingBegin;
    if (mvk_get_u32(d)) {
        VkConditionalRenderingBeginInfo2EXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkConditionalRenderingBeginInfo2EXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkConditionalRenderingBeginInfo2EXT(d, &items[i]);
        pConditionalRenderingBegin = items;
    } else {
        pConditionalRenderingBegin = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBeginConditionalRendering2EXT");
        return;
    }
    if (!c->fn->vkCmdBeginConditionalRendering2EXT) {
        mvk_unsupported(c, "vkCmdBeginConditionalRendering2EXT");
        return;
    }
    c->fn->vkCmdBeginConditionalRendering2EXT(commandBuffer, pConditionalRenderingBegin);
}

static void run_vkCmdBindTransformFeedbackBuffers2EXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstBinding;
    uint32_t bindingCount;
    VkBindTransformFeedbackBuffer2InfoEXT * pBindingInfos;
    firstBinding = (uint32_t)mvk_get_u32(d);
    bindingCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBindTransformFeedbackBuffer2InfoEXT *items = mvk_alloc(d, (size_t)(bindingCount) * sizeof(VkBindTransformFeedbackBuffer2InfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(bindingCount); i++)
            mvk_get_VkBindTransformFeedbackBuffer2InfoEXT(d, &items[i]);
        pBindingInfos = items;
    } else {
        pBindingInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindTransformFeedbackBuffers2EXT");
        return;
    }
    if (!c->fn->vkCmdBindTransformFeedbackBuffers2EXT) {
        mvk_unsupported(c, "vkCmdBindTransformFeedbackBuffers2EXT");
        return;
    }
    c->fn->vkCmdBindTransformFeedbackBuffers2EXT(commandBuffer, firstBinding, bindingCount, pBindingInfos);
}

static void run_vkCmdBeginTransformFeedback2EXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstCounterRange;
    uint32_t counterRangeCount;
    VkBindTransformFeedbackBuffer2InfoEXT * pCounterInfos;
    firstCounterRange = (uint32_t)mvk_get_u32(d);
    counterRangeCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBindTransformFeedbackBuffer2InfoEXT *items = mvk_alloc(d, (size_t)(counterRangeCount) * sizeof(VkBindTransformFeedbackBuffer2InfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(counterRangeCount); i++)
            mvk_get_VkBindTransformFeedbackBuffer2InfoEXT(d, &items[i]);
        pCounterInfos = items;
    } else {
        pCounterInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBeginTransformFeedback2EXT");
        return;
    }
    if (!c->fn->vkCmdBeginTransformFeedback2EXT) {
        mvk_unsupported(c, "vkCmdBeginTransformFeedback2EXT");
        return;
    }
    c->fn->vkCmdBeginTransformFeedback2EXT(commandBuffer, firstCounterRange, counterRangeCount, pCounterInfos);
}

static void run_vkCmdEndTransformFeedback2EXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstCounterRange;
    uint32_t counterRangeCount;
    VkBindTransformFeedbackBuffer2InfoEXT * pCounterInfos;
    firstCounterRange = (uint32_t)mvk_get_u32(d);
    counterRangeCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBindTransformFeedbackBuffer2InfoEXT *items = mvk_alloc(d, (size_t)(counterRangeCount) * sizeof(VkBindTransformFeedbackBuffer2InfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(counterRangeCount); i++)
            mvk_get_VkBindTransformFeedbackBuffer2InfoEXT(d, &items[i]);
        pCounterInfos = items;
    } else {
        pCounterInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdEndTransformFeedback2EXT");
        return;
    }
    if (!c->fn->vkCmdEndTransformFeedback2EXT) {
        mvk_unsupported(c, "vkCmdEndTransformFeedback2EXT");
        return;
    }
    c->fn->vkCmdEndTransformFeedback2EXT(commandBuffer, firstCounterRange, counterRangeCount, pCounterInfos);
}

static void run_vkCmdDrawIndirectByteCount2EXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t instanceCount;
    uint32_t firstInstance;
    VkBindTransformFeedbackBuffer2InfoEXT * pCounterInfo;
    uint32_t counterOffset;
    uint32_t vertexStride;
    instanceCount = (uint32_t)mvk_get_u32(d);
    firstInstance = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkBindTransformFeedbackBuffer2InfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBindTransformFeedbackBuffer2InfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBindTransformFeedbackBuffer2InfoEXT(d, &items[i]);
        pCounterInfo = items;
    } else {
        pCounterInfo = NULL;
    }
    counterOffset = (uint32_t)mvk_get_u32(d);
    vertexStride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawIndirectByteCount2EXT");
        return;
    }
    if (!c->fn->vkCmdDrawIndirectByteCount2EXT) {
        mvk_unsupported(c, "vkCmdDrawIndirectByteCount2EXT");
        return;
    }
    c->fn->vkCmdDrawIndirectByteCount2EXT(commandBuffer, instanceCount, firstInstance, pCounterInfo, counterOffset, vertexStride);
}

static void run_vkCmdDrawMeshTasksIndirect2EXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDrawIndirect2InfoKHR * pInfo;
    if (mvk_get_u32(d)) {
        VkDrawIndirect2InfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDrawIndirect2InfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDrawIndirect2InfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawMeshTasksIndirect2EXT");
        return;
    }
    if (!c->fn->vkCmdDrawMeshTasksIndirect2EXT) {
        mvk_unsupported(c, "vkCmdDrawMeshTasksIndirect2EXT");
        return;
    }
    c->fn->vkCmdDrawMeshTasksIndirect2EXT(commandBuffer, pInfo);
}

static void run_vkCmdDrawMeshTasksIndirectCount2EXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDrawIndirectCount2InfoKHR * pInfo;
    if (mvk_get_u32(d)) {
        VkDrawIndirectCount2InfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDrawIndirectCount2InfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDrawIndirectCount2InfoKHR(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawMeshTasksIndirectCount2EXT");
        return;
    }
    if (!c->fn->vkCmdDrawMeshTasksIndirectCount2EXT) {
        mvk_unsupported(c, "vkCmdDrawMeshTasksIndirectCount2EXT");
        return;
    }
    c->fn->vkCmdDrawMeshTasksIndirectCount2EXT(commandBuffer, pInfo);
}

static void run_vkCmdWriteMarkerToMemoryAMD(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkMemoryMarkerInfoAMD * pInfo;
    if (mvk_get_u32(d)) {
        VkMemoryMarkerInfoAMD *items = mvk_alloc(d, (size_t)(1) * sizeof(VkMemoryMarkerInfoAMD));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkMemoryMarkerInfoAMD(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdWriteMarkerToMemoryAMD");
        return;
    }
    if (!c->fn->vkCmdWriteMarkerToMemoryAMD) {
        mvk_unsupported(c, "vkCmdWriteMarkerToMemoryAMD");
        return;
    }
    c->fn->vkCmdWriteMarkerToMemoryAMD(commandBuffer, pInfo);
}

static void run_vkCreateAccelerationStructure2KHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkAccelerationStructureCreateInfo2KHR * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkAccelerationStructureCreateInfo2KHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkAccelerationStructureCreateInfo2KHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkAccelerationStructureCreateInfo2KHR(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pAccelerationStructure = mvk_get_u64(d);
    VkAccelerationStructureKHR pAccelerationStructure_value = VK_NULL_HANDLE;
    VkAccelerationStructureKHR *pAccelerationStructure = &pAccelerationStructure_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateAccelerationStructure2KHR");
        return;
    }
    if (!c->fn->vkCreateAccelerationStructure2KHR) {
        mvk_unsupported(c, "vkCreateAccelerationStructure2KHR");
        return;
    }
    VkResult result = c->fn->vkCreateAccelerationStructure2KHR(device, pCreateInfo, NULL, pAccelerationStructure);
    mvk_bind_id(c, id_pAccelerationStructure, (uint64_t)pAccelerationStructure_value);
    mvk_track(c->conn, (uint64_t)pAccelerationStructure_value, MVK_KIND_VkAccelerationStructureKHR);
    mvk_check(c, result, "vkCreateAccelerationStructure2KHR");
}

static void run_vkCmdSetFragmentShadingRateEnumNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkFragmentShadingRateNV shadingRate;
    VkFragmentShadingRateCombinerOpKHR combinerOps[2];
    shadingRate = (VkFragmentShadingRateNV)mvk_get_u32(d);
    mvk_get_bytes(d, combinerOps, (size_t)(2) * sizeof((combinerOps)[0]));
    if (d->error) {
        mvk_malformed(c, "vkCmdSetFragmentShadingRateEnumNV");
        return;
    }
    if (!c->fn->vkCmdSetFragmentShadingRateEnumNV) {
        mvk_unsupported(c, "vkCmdSetFragmentShadingRateEnumNV");
        return;
    }
    c->fn->vkCmdSetFragmentShadingRateEnumNV(commandBuffer, shadingRate, combinerOps);
}

static void run_vkCmdDrawMeshTasksEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t groupCountX;
    uint32_t groupCountY;
    uint32_t groupCountZ;
    groupCountX = (uint32_t)mvk_get_u32(d);
    groupCountY = (uint32_t)mvk_get_u32(d);
    groupCountZ = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawMeshTasksEXT");
        return;
    }
    if (!c->fn->vkCmdDrawMeshTasksEXT) {
        mvk_unsupported(c, "vkCmdDrawMeshTasksEXT");
        return;
    }
    c->fn->vkCmdDrawMeshTasksEXT(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

static void run_vkCmdDrawMeshTasksIndirectEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer buffer;
    VkDeviceSize offset;
    uint32_t drawCount;
    uint32_t stride;
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    offset = (VkDeviceSize)mvk_get_u64(d);
    drawCount = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawMeshTasksIndirectEXT");
        return;
    }
    if (!c->fn->vkCmdDrawMeshTasksIndirectEXT) {
        mvk_unsupported(c, "vkCmdDrawMeshTasksIndirectEXT");
        return;
    }
    c->fn->vkCmdDrawMeshTasksIndirectEXT(commandBuffer, buffer, offset, drawCount, stride);
}

static void run_vkCmdDrawMeshTasksIndirectCountEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer buffer;
    VkDeviceSize offset;
    VkBuffer countBuffer;
    VkDeviceSize countBufferOffset;
    uint32_t maxDrawCount;
    uint32_t stride;
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    offset = (VkDeviceSize)mvk_get_u64(d);
    countBuffer = (VkBuffer)mvk_get_handle(d, 0);
    countBufferOffset = (VkDeviceSize)mvk_get_u64(d);
    maxDrawCount = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawMeshTasksIndirectCountEXT");
        return;
    }
    if (!c->fn->vkCmdDrawMeshTasksIndirectCountEXT) {
        mvk_unsupported(c, "vkCmdDrawMeshTasksIndirectCountEXT");
        return;
    }
    c->fn->vkCmdDrawMeshTasksIndirectCountEXT(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

static void run_vkCmdSetVertexInputEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t vertexBindingDescriptionCount;
    VkVertexInputBindingDescription2EXT * pVertexBindingDescriptions;
    uint32_t vertexAttributeDescriptionCount;
    VkVertexInputAttributeDescription2EXT * pVertexAttributeDescriptions;
    vertexBindingDescriptionCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkVertexInputBindingDescription2EXT *items = mvk_alloc(d, (size_t)(vertexBindingDescriptionCount) * sizeof(VkVertexInputBindingDescription2EXT));
        for (uint32_t i = 0; i < (uint32_t)(vertexBindingDescriptionCount); i++)
            mvk_get_VkVertexInputBindingDescription2EXT(d, &items[i]);
        pVertexBindingDescriptions = items;
    } else {
        pVertexBindingDescriptions = NULL;
    }
    vertexAttributeDescriptionCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkVertexInputAttributeDescription2EXT *items = mvk_alloc(d, (size_t)(vertexAttributeDescriptionCount) * sizeof(VkVertexInputAttributeDescription2EXT));
        for (uint32_t i = 0; i < (uint32_t)(vertexAttributeDescriptionCount); i++)
            mvk_get_VkVertexInputAttributeDescription2EXT(d, &items[i]);
        pVertexAttributeDescriptions = items;
    } else {
        pVertexAttributeDescriptions = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetVertexInputEXT");
        return;
    }
    if (!c->fn->vkCmdSetVertexInputEXT) {
        mvk_unsupported(c, "vkCmdSetVertexInputEXT");
        return;
    }
    c->fn->vkCmdSetVertexInputEXT(commandBuffer, vertexBindingDescriptionCount, pVertexBindingDescriptions, vertexAttributeDescriptionCount, pVertexAttributeDescriptions);
}

static void run_vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkRenderPass renderpass;
    device = (VkDevice)mvk_get_handle(d, 1);
    renderpass = (VkRenderPass)mvk_get_handle(d, 0);
    VkExtent2D pMaxWorkgroupSize_value;
    memset(&pMaxWorkgroupSize_value, 0, sizeof(pMaxWorkgroupSize_value));
    VkExtent2D *pMaxWorkgroupSize = &pMaxWorkgroupSize_value;
    if (d->error) {
        mvk_malformed(c, "vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI");
        return;
    }
    if (!c->fn->vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI) {
        mvk_unsupported(c, "vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI");
        return;
    }
    VkResult result = c->fn->vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI(device, renderpass, pMaxWorkgroupSize);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    reply_VkExtent2D(e, pMaxWorkgroupSize);
    mvk_reply_end(c);
}

static void run_vkCmdSubpassShadingHUAWEI(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    if (d->error) {
        mvk_malformed(c, "vkCmdSubpassShadingHUAWEI");
        return;
    }
    if (!c->fn->vkCmdSubpassShadingHUAWEI) {
        mvk_unsupported(c, "vkCmdSubpassShadingHUAWEI");
        return;
    }
    c->fn->vkCmdSubpassShadingHUAWEI(commandBuffer);
}

static void run_vkCmdBindInvocationMaskHUAWEI(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkImageView imageView;
    VkImageLayout imageLayout;
    imageView = (VkImageView)mvk_get_handle(d, 0);
    imageLayout = (VkImageLayout)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdBindInvocationMaskHUAWEI");
        return;
    }
    if (!c->fn->vkCmdBindInvocationMaskHUAWEI) {
        mvk_unsupported(c, "vkCmdBindInvocationMaskHUAWEI");
        return;
    }
    c->fn->vkCmdBindInvocationMaskHUAWEI(commandBuffer, imageView, imageLayout);
}

static void run_vkGetPipelinePropertiesEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineInfoKHR * pPipelineInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPipelineInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPipelineInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPipelineInfoKHR(d, &items[i]);
        pPipelineInfo = items;
    } else {
        pPipelineInfo = NULL;
    }
    VkBaseOutStructure pPipelineProperties_value;
    memset(&pPipelineProperties_value, 0, sizeof(pPipelineProperties_value));
    VkBaseOutStructure *pPipelineProperties = &pPipelineProperties_value;
    if (d->error) {
        mvk_malformed(c, "vkGetPipelinePropertiesEXT");
        return;
    }
    if (!c->fn->vkGetPipelinePropertiesEXT) {
        mvk_unsupported(c, "vkGetPipelinePropertiesEXT");
        return;
    }
    VkResult result = c->fn->vkGetPipelinePropertiesEXT(device, pPipelineInfo, pPipelineProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    reply_VkBaseOutStructure(e, pPipelineProperties);
    mvk_reply_end(c);
}

static void run_vkCmdSetPatchControlPointsEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t patchControlPoints;
    patchControlPoints = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetPatchControlPointsEXT");
        return;
    }
    if (!c->fn->vkCmdSetPatchControlPointsEXT) {
        mvk_unsupported(c, "vkCmdSetPatchControlPointsEXT");
        return;
    }
    c->fn->vkCmdSetPatchControlPointsEXT(commandBuffer, patchControlPoints);
}

static void run_vkCmdSetLogicOpEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkLogicOp logicOp;
    logicOp = (VkLogicOp)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetLogicOpEXT");
        return;
    }
    if (!c->fn->vkCmdSetLogicOpEXT) {
        mvk_unsupported(c, "vkCmdSetLogicOpEXT");
        return;
    }
    c->fn->vkCmdSetLogicOpEXT(commandBuffer, logicOp);
}

static void run_vkCmdSetColorWriteEnableEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t attachmentCount;
    VkBool32 * pColorWriteEnables;
    attachmentCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(attachmentCount) * sizeof(VkBool32));
        mvk_get_bytes(d, items, (size_t)(attachmentCount) * sizeof(VkBool32));
        pColorWriteEnables = items;
    } else {
        pColorWriteEnables = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetColorWriteEnableEXT");
        return;
    }
    if (!c->fn->vkCmdSetColorWriteEnableEXT) {
        mvk_unsupported(c, "vkCmdSetColorWriteEnableEXT");
        return;
    }
    c->fn->vkCmdSetColorWriteEnableEXT(commandBuffer, attachmentCount, pColorWriteEnables);
}

static void run_vkCmdTraceRaysIndirect2KHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDeviceAddress indirectDeviceAddress;
    indirectDeviceAddress = (VkDeviceAddress)mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdTraceRaysIndirect2KHR");
        return;
    }
    if (!c->fn->vkCmdTraceRaysIndirect2KHR) {
        mvk_unsupported(c, "vkCmdTraceRaysIndirect2KHR");
        return;
    }
    c->fn->vkCmdTraceRaysIndirect2KHR(commandBuffer, indirectDeviceAddress);
}

static void run_vkCmdDrawMultiEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t drawCount;
    VkMultiDrawInfoEXT * pVertexInfo;
    uint32_t instanceCount;
    uint32_t firstInstance;
    uint32_t stride;
    drawCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkMultiDrawInfoEXT *items = mvk_alloc(d, (size_t)(drawCount) * sizeof(VkMultiDrawInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(drawCount); i++)
            mvk_get_VkMultiDrawInfoEXT(d, &items[i]);
        pVertexInfo = items;
    } else {
        pVertexInfo = NULL;
    }
    instanceCount = (uint32_t)mvk_get_u32(d);
    firstInstance = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawMultiEXT");
        return;
    }
    if (!c->fn->vkCmdDrawMultiEXT) {
        mvk_unsupported(c, "vkCmdDrawMultiEXT");
        return;
    }
    c->fn->vkCmdDrawMultiEXT(commandBuffer, drawCount, pVertexInfo, instanceCount, firstInstance, stride);
}

static void run_vkCmdDrawMultiIndexedEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t drawCount;
    VkMultiDrawIndexedInfoEXT * pIndexInfo;
    uint32_t instanceCount;
    uint32_t firstInstance;
    uint32_t stride;
    int32_t * pVertexOffset;
    drawCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkMultiDrawIndexedInfoEXT *items = mvk_alloc(d, (size_t)(drawCount) * sizeof(VkMultiDrawIndexedInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(drawCount); i++)
            mvk_get_VkMultiDrawIndexedInfoEXT(d, &items[i]);
        pIndexInfo = items;
    } else {
        pIndexInfo = NULL;
    }
    instanceCount = (uint32_t)mvk_get_u32(d);
    firstInstance = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(1) * sizeof(int32_t));
        mvk_get_bytes(d, items, (size_t)(1) * sizeof(int32_t));
        pVertexOffset = items;
    } else {
        pVertexOffset = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawMultiIndexedEXT");
        return;
    }
    if (!c->fn->vkCmdDrawMultiIndexedEXT) {
        mvk_unsupported(c, "vkCmdDrawMultiIndexedEXT");
        return;
    }
    c->fn->vkCmdDrawMultiIndexedEXT(commandBuffer, drawCount, pIndexInfo, instanceCount, firstInstance, stride, pVertexOffset);
}

static void run_vkCreateMicromapEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkMicromapCreateInfoEXT * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkMicromapCreateInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkMicromapCreateInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkMicromapCreateInfoEXT(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pMicromap = mvk_get_u64(d);
    VkMicromapEXT pMicromap_value = VK_NULL_HANDLE;
    VkMicromapEXT *pMicromap = &pMicromap_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateMicromapEXT");
        return;
    }
    if (!c->fn->vkCreateMicromapEXT) {
        mvk_unsupported(c, "vkCreateMicromapEXT");
        return;
    }
    VkResult result = c->fn->vkCreateMicromapEXT(device, pCreateInfo, NULL, pMicromap);
    mvk_bind_id(c, id_pMicromap, (uint64_t)pMicromap_value);
    mvk_track(c->conn, (uint64_t)pMicromap_value, MVK_KIND_VkMicromapEXT);
    mvk_check(c, result, "vkCreateMicromapEXT");
}

static void run_vkDestroyMicromapEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkMicromapEXT micromap;
    device = (VkDevice)mvk_get_handle(d, 1);
    micromap = (VkMicromapEXT)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyMicromapEXT");
        return;
    }
    if (!c->fn->vkDestroyMicromapEXT) {
        mvk_unsupported(c, "vkDestroyMicromapEXT");
        return;
    }
    c->fn->vkDestroyMicromapEXT(device, micromap, NULL);
    mvk_forget(c->conn, (uint64_t)micromap);
}

static void run_vkCmdBuildMicromapsEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t infoCount;
    VkMicromapBuildInfoEXT * pInfos;
    infoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkMicromapBuildInfoEXT *items = mvk_alloc(d, (size_t)(infoCount) * sizeof(VkMicromapBuildInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_get_VkMicromapBuildInfoEXT(d, &items[i]);
        pInfos = items;
    } else {
        pInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBuildMicromapsEXT");
        return;
    }
    if (!c->fn->vkCmdBuildMicromapsEXT) {
        mvk_unsupported(c, "vkCmdBuildMicromapsEXT");
        return;
    }
    c->fn->vkCmdBuildMicromapsEXT(commandBuffer, infoCount, pInfos);
}

static void run_vkBuildMicromapsEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR deferredOperation;
    uint32_t infoCount;
    VkMicromapBuildInfoEXT * pInfos;
    device = (VkDevice)mvk_get_handle(d, 1);
    deferredOperation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    infoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkMicromapBuildInfoEXT *items = mvk_alloc(d, (size_t)(infoCount) * sizeof(VkMicromapBuildInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_get_VkMicromapBuildInfoEXT(d, &items[i]);
        pInfos = items;
    } else {
        pInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkBuildMicromapsEXT");
        return;
    }
    if (!c->fn->vkBuildMicromapsEXT) {
        mvk_unsupported(c, "vkBuildMicromapsEXT");
        return;
    }
    VkResult result = c->fn->vkBuildMicromapsEXT(device, deferredOperation, infoCount, pInfos);
    mvk_check(c, result, "vkBuildMicromapsEXT");
}

static void run_vkCopyMicromapEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR deferredOperation;
    VkCopyMicromapInfoEXT * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    deferredOperation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    if (mvk_get_u32(d)) {
        VkCopyMicromapInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyMicromapInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyMicromapInfoEXT(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCopyMicromapEXT");
        return;
    }
    if (!c->fn->vkCopyMicromapEXT) {
        mvk_unsupported(c, "vkCopyMicromapEXT");
        return;
    }
    VkResult result = c->fn->vkCopyMicromapEXT(device, deferredOperation, pInfo);
    mvk_check(c, result, "vkCopyMicromapEXT");
}

static void run_vkCopyMicromapToMemoryEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR deferredOperation;
    VkCopyMicromapToMemoryInfoEXT * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    deferredOperation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    if (mvk_get_u32(d)) {
        VkCopyMicromapToMemoryInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyMicromapToMemoryInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyMicromapToMemoryInfoEXT(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCopyMicromapToMemoryEXT");
        return;
    }
    if (!c->fn->vkCopyMicromapToMemoryEXT) {
        mvk_unsupported(c, "vkCopyMicromapToMemoryEXT");
        return;
    }
    VkResult result = c->fn->vkCopyMicromapToMemoryEXT(device, deferredOperation, pInfo);
    mvk_check(c, result, "vkCopyMicromapToMemoryEXT");
}

static void run_vkCopyMemoryToMicromapEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeferredOperationKHR deferredOperation;
    VkCopyMemoryToMicromapInfoEXT * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    deferredOperation = (VkDeferredOperationKHR)mvk_get_handle(d, 0);
    if (mvk_get_u32(d)) {
        VkCopyMemoryToMicromapInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyMemoryToMicromapInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyMemoryToMicromapInfoEXT(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCopyMemoryToMicromapEXT");
        return;
    }
    if (!c->fn->vkCopyMemoryToMicromapEXT) {
        mvk_unsupported(c, "vkCopyMemoryToMicromapEXT");
        return;
    }
    VkResult result = c->fn->vkCopyMemoryToMicromapEXT(device, deferredOperation, pInfo);
    mvk_check(c, result, "vkCopyMemoryToMicromapEXT");
}

static void run_vkWriteMicromapsPropertiesEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t micromapCount;
    VkMicromapEXT * pMicromaps;
    VkQueryType queryType;
    size_t dataSize;
    size_t stride;
    device = (VkDevice)mvk_get_handle(d, 1);
    micromapCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkMicromapEXT *items = mvk_alloc(d, (size_t)(micromapCount) * sizeof(VkMicromapEXT));
        for (uint32_t i = 0; i < (uint32_t)(micromapCount); i++)
            items[i] = (VkMicromapEXT)mvk_get_handle(d, 0);
        pMicromaps = items;
    } else {
        pMicromaps = NULL;
    }
    queryType = (VkQueryType)mvk_get_u32(d);
    dataSize = (size_t)mvk_get_u64(d);
    int want_pData = mvk_get_u32(d);
    uint8_t *pData = NULL;
    stride = (size_t)mvk_get_u64(d);
    if (want_pData) {
        pData = mvk_alloc(d, sizeof(uint8_t) * (size_t)(dataSize) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkWriteMicromapsPropertiesEXT");
        return;
    }
    if (!c->fn->vkWriteMicromapsPropertiesEXT) {
        mvk_unsupported(c, "vkWriteMicromapsPropertiesEXT");
        return;
    }
    VkResult result = c->fn->vkWriteMicromapsPropertiesEXT(device, micromapCount, pMicromaps, queryType, dataSize, pData, stride);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, pData != NULL);
    if (pData) {
        mvk_put_u32(e, (uint32_t)(dataSize));
        mvk_put_bytes(e, pData, (size_t)(dataSize));
    }
    mvk_reply_end(c);
}

static void run_vkCmdCopyMicromapEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyMicromapInfoEXT * pInfo;
    if (mvk_get_u32(d)) {
        VkCopyMicromapInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyMicromapInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyMicromapInfoEXT(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyMicromapEXT");
        return;
    }
    if (!c->fn->vkCmdCopyMicromapEXT) {
        mvk_unsupported(c, "vkCmdCopyMicromapEXT");
        return;
    }
    c->fn->vkCmdCopyMicromapEXT(commandBuffer, pInfo);
}

static void run_vkCmdCopyMicromapToMemoryEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyMicromapToMemoryInfoEXT * pInfo;
    if (mvk_get_u32(d)) {
        VkCopyMicromapToMemoryInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyMicromapToMemoryInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyMicromapToMemoryInfoEXT(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyMicromapToMemoryEXT");
        return;
    }
    if (!c->fn->vkCmdCopyMicromapToMemoryEXT) {
        mvk_unsupported(c, "vkCmdCopyMicromapToMemoryEXT");
        return;
    }
    c->fn->vkCmdCopyMicromapToMemoryEXT(commandBuffer, pInfo);
}

static void run_vkCmdCopyMemoryToMicromapEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyMemoryToMicromapInfoEXT * pInfo;
    if (mvk_get_u32(d)) {
        VkCopyMemoryToMicromapInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyMemoryToMicromapInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyMemoryToMicromapInfoEXT(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyMemoryToMicromapEXT");
        return;
    }
    if (!c->fn->vkCmdCopyMemoryToMicromapEXT) {
        mvk_unsupported(c, "vkCmdCopyMemoryToMicromapEXT");
        return;
    }
    c->fn->vkCmdCopyMemoryToMicromapEXT(commandBuffer, pInfo);
}

static void run_vkCmdWriteMicromapsPropertiesEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t micromapCount;
    VkMicromapEXT * pMicromaps;
    VkQueryType queryType;
    VkQueryPool queryPool;
    uint32_t firstQuery;
    micromapCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkMicromapEXT *items = mvk_alloc(d, (size_t)(micromapCount) * sizeof(VkMicromapEXT));
        for (uint32_t i = 0; i < (uint32_t)(micromapCount); i++)
            items[i] = (VkMicromapEXT)mvk_get_handle(d, 0);
        pMicromaps = items;
    } else {
        pMicromaps = NULL;
    }
    queryType = (VkQueryType)mvk_get_u32(d);
    queryPool = (VkQueryPool)mvk_get_handle(d, 0);
    firstQuery = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdWriteMicromapsPropertiesEXT");
        return;
    }
    if (!c->fn->vkCmdWriteMicromapsPropertiesEXT) {
        mvk_unsupported(c, "vkCmdWriteMicromapsPropertiesEXT");
        return;
    }
    c->fn->vkCmdWriteMicromapsPropertiesEXT(commandBuffer, micromapCount, pMicromaps, queryType, queryPool, firstQuery);
}

static void run_vkGetDeviceMicromapCompatibilityEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkMicromapVersionInfoEXT * pVersionInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkMicromapVersionInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkMicromapVersionInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkMicromapVersionInfoEXT(d, &items[i]);
        pVersionInfo = items;
    } else {
        pVersionInfo = NULL;
    }
    VkAccelerationStructureCompatibilityKHR pCompatibility_value;
    memset(&pCompatibility_value, 0, sizeof(pCompatibility_value));
    VkAccelerationStructureCompatibilityKHR *pCompatibility = &pCompatibility_value;
    if (d->error) {
        mvk_malformed(c, "vkGetDeviceMicromapCompatibilityEXT");
        return;
    }
    if (!c->fn->vkGetDeviceMicromapCompatibilityEXT) {
        mvk_unsupported(c, "vkGetDeviceMicromapCompatibilityEXT");
        return;
    }
    c->fn->vkGetDeviceMicromapCompatibilityEXT(device, pVersionInfo, pCompatibility);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)(*pCompatibility));
    mvk_reply_end(c);
}

static void run_vkGetMicromapBuildSizesEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkAccelerationStructureBuildTypeKHR buildType;
    VkMicromapBuildInfoEXT * pBuildInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    buildType = (VkAccelerationStructureBuildTypeKHR)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkMicromapBuildInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkMicromapBuildInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkMicromapBuildInfoEXT(d, &items[i]);
        pBuildInfo = items;
    } else {
        pBuildInfo = NULL;
    }
    VkMicromapBuildSizesInfoEXT *pSizeInfo = mvk_get_out_shape(d, sizeof(VkMicromapBuildSizesInfoEXT));
    if (d->error) {
        mvk_malformed(c, "vkGetMicromapBuildSizesEXT");
        return;
    }
    if (!c->fn->vkGetMicromapBuildSizesEXT) {
        mvk_unsupported(c, "vkGetMicromapBuildSizesEXT");
        return;
    }
    c->fn->vkGetMicromapBuildSizesEXT(device, buildType, pBuildInfo, pSizeInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkMicromapBuildSizesInfoEXT(e, pSizeInfo);
    mvk_reply_end(c);
}

static void run_vkCmdDrawClusterHUAWEI(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t groupCountX;
    uint32_t groupCountY;
    uint32_t groupCountZ;
    groupCountX = (uint32_t)mvk_get_u32(d);
    groupCountY = (uint32_t)mvk_get_u32(d);
    groupCountZ = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawClusterHUAWEI");
        return;
    }
    if (!c->fn->vkCmdDrawClusterHUAWEI) {
        mvk_unsupported(c, "vkCmdDrawClusterHUAWEI");
        return;
    }
    c->fn->vkCmdDrawClusterHUAWEI(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

static void run_vkCmdDrawClusterIndirectHUAWEI(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuffer buffer;
    VkDeviceSize offset;
    buffer = (VkBuffer)mvk_get_handle(d, 0);
    offset = (VkDeviceSize)mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDrawClusterIndirectHUAWEI");
        return;
    }
    if (!c->fn->vkCmdDrawClusterIndirectHUAWEI) {
        mvk_unsupported(c, "vkCmdDrawClusterIndirectHUAWEI");
        return;
    }
    c->fn->vkCmdDrawClusterIndirectHUAWEI(commandBuffer, buffer, offset);
}

static void run_vkSetDeviceMemoryPriorityEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDeviceMemory memory;
    float priority;
    device = (VkDevice)mvk_get_handle(d, 1);
    memory = (VkDeviceMemory)mvk_get_handle(d, 0);
    priority = mvk_bits_f32(mvk_get_u32(d));
    if (d->error) {
        mvk_malformed(c, "vkSetDeviceMemoryPriorityEXT");
        return;
    }
    if (!c->fn->vkSetDeviceMemoryPriorityEXT) {
        mvk_unsupported(c, "vkSetDeviceMemoryPriorityEXT");
        return;
    }
    c->fn->vkSetDeviceMemoryPriorityEXT(device, memory, priority);
}

static void run_vkCmdSetDispatchParametersARM(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDispatchParametersARM * pDispatchParameters;
    if (mvk_get_u32(d)) {
        VkDispatchParametersARM *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDispatchParametersARM));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDispatchParametersARM(d, &items[i]);
        pDispatchParameters = items;
    } else {
        pDispatchParameters = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDispatchParametersARM");
        return;
    }
    if (!c->fn->vkCmdSetDispatchParametersARM) {
        mvk_unsupported(c, "vkCmdSetDispatchParametersARM");
        return;
    }
    c->fn->vkCmdSetDispatchParametersARM(commandBuffer, pDispatchParameters);
}

static void run_vkGetDescriptorSetLayoutHostMappingInfoVALVE(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkDescriptorSetBindingReferenceVALVE * pBindingReference;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkDescriptorSetBindingReferenceVALVE *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDescriptorSetBindingReferenceVALVE));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDescriptorSetBindingReferenceVALVE(d, &items[i]);
        pBindingReference = items;
    } else {
        pBindingReference = NULL;
    }
    VkDescriptorSetLayoutHostMappingInfoVALVE *pHostMapping = mvk_get_out_shape(d, sizeof(VkDescriptorSetLayoutHostMappingInfoVALVE));
    if (d->error) {
        mvk_malformed(c, "vkGetDescriptorSetLayoutHostMappingInfoVALVE");
        return;
    }
    if (!c->fn->vkGetDescriptorSetLayoutHostMappingInfoVALVE) {
        mvk_unsupported(c, "vkGetDescriptorSetLayoutHostMappingInfoVALVE");
        return;
    }
    c->fn->vkGetDescriptorSetLayoutHostMappingInfoVALVE(device, pBindingReference, pHostMapping);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkDescriptorSetLayoutHostMappingInfoVALVE(e, pHostMapping);
    mvk_reply_end(c);
}

static void run_vkCmdCopyMemoryIndirectNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDeviceAddress copyBufferAddress;
    uint32_t copyCount;
    uint32_t stride;
    copyBufferAddress = (VkDeviceAddress)mvk_get_u64(d);
    copyCount = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyMemoryIndirectNV");
        return;
    }
    if (!c->fn->vkCmdCopyMemoryIndirectNV) {
        mvk_unsupported(c, "vkCmdCopyMemoryIndirectNV");
        return;
    }
    c->fn->vkCmdCopyMemoryIndirectNV(commandBuffer, copyBufferAddress, copyCount, stride);
}

static void run_vkCmdCopyMemoryToImageIndirectNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDeviceAddress copyBufferAddress;
    uint32_t copyCount;
    uint32_t stride;
    VkImage dstImage;
    VkImageLayout dstImageLayout;
    VkImageSubresourceLayers * pImageSubresources;
    copyBufferAddress = (VkDeviceAddress)mvk_get_u64(d);
    copyCount = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    dstImage = (VkImage)mvk_get_handle(d, 0);
    dstImageLayout = (VkImageLayout)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkImageSubresourceLayers *items = mvk_alloc(d, (size_t)(copyCount) * sizeof(VkImageSubresourceLayers));
        for (uint32_t i = 0; i < (uint32_t)(copyCount); i++)
            mvk_get_VkImageSubresourceLayers(d, &items[i]);
        pImageSubresources = items;
    } else {
        pImageSubresources = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyMemoryToImageIndirectNV");
        return;
    }
    if (!c->fn->vkCmdCopyMemoryToImageIndirectNV) {
        mvk_unsupported(c, "vkCmdCopyMemoryToImageIndirectNV");
        return;
    }
    c->fn->vkCmdCopyMemoryToImageIndirectNV(commandBuffer, copyBufferAddress, copyCount, stride, dstImage, dstImageLayout, pImageSubresources);
}

static void run_vkCmdDecompressMemoryNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t decompressRegionCount;
    VkDecompressMemoryRegionNV * pDecompressMemoryRegions;
    decompressRegionCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkDecompressMemoryRegionNV *items = mvk_alloc(d, (size_t)(decompressRegionCount) * sizeof(VkDecompressMemoryRegionNV));
        for (uint32_t i = 0; i < (uint32_t)(decompressRegionCount); i++)
            mvk_get_VkDecompressMemoryRegionNV(d, &items[i]);
        pDecompressMemoryRegions = items;
    } else {
        pDecompressMemoryRegions = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdDecompressMemoryNV");
        return;
    }
    if (!c->fn->vkCmdDecompressMemoryNV) {
        mvk_unsupported(c, "vkCmdDecompressMemoryNV");
        return;
    }
    c->fn->vkCmdDecompressMemoryNV(commandBuffer, decompressRegionCount, pDecompressMemoryRegions);
}

static void run_vkCmdDecompressMemoryIndirectCountNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDeviceAddress indirectCommandsAddress;
    VkDeviceAddress indirectCommandsCountAddress;
    uint32_t stride;
    indirectCommandsAddress = (VkDeviceAddress)mvk_get_u64(d);
    indirectCommandsCountAddress = (VkDeviceAddress)mvk_get_u64(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDecompressMemoryIndirectCountNV");
        return;
    }
    if (!c->fn->vkCmdDecompressMemoryIndirectCountNV) {
        mvk_unsupported(c, "vkCmdDecompressMemoryIndirectCountNV");
        return;
    }
    c->fn->vkCmdDecompressMemoryIndirectCountNV(commandBuffer, indirectCommandsAddress, indirectCommandsCountAddress, stride);
}

static void run_vkGetPipelineIndirectMemoryRequirementsNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkComputePipelineCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkComputePipelineCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkComputePipelineCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkComputePipelineCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    VkMemoryRequirements2 *pMemoryRequirements = mvk_get_out_shape(d, sizeof(VkMemoryRequirements2));
    if (d->error) {
        mvk_malformed(c, "vkGetPipelineIndirectMemoryRequirementsNV");
        return;
    }
    if (!c->fn->vkGetPipelineIndirectMemoryRequirementsNV) {
        mvk_unsupported(c, "vkGetPipelineIndirectMemoryRequirementsNV");
        return;
    }
    c->fn->vkGetPipelineIndirectMemoryRequirementsNV(device, pCreateInfo, pMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkMemoryRequirements2(e, pMemoryRequirements);
    mvk_reply_end(c);
}

static void run_vkCmdUpdatePipelineIndirectBufferNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPipelineBindPoint pipelineBindPoint;
    VkPipeline pipeline;
    pipelineBindPoint = (VkPipelineBindPoint)mvk_get_u32(d);
    pipeline = (VkPipeline)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkCmdUpdatePipelineIndirectBufferNV");
        return;
    }
    if (!c->fn->vkCmdUpdatePipelineIndirectBufferNV) {
        mvk_unsupported(c, "vkCmdUpdatePipelineIndirectBufferNV");
        return;
    }
    c->fn->vkCmdUpdatePipelineIndirectBufferNV(commandBuffer, pipelineBindPoint, pipeline);
}

static void run_vkGetPipelineIndirectDeviceAddressNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPipelineIndirectDeviceAddressInfoNV * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPipelineIndirectDeviceAddressInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPipelineIndirectDeviceAddressInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPipelineIndirectDeviceAddressInfoNV(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPipelineIndirectDeviceAddressNV");
        return;
    }
    if (!c->fn->vkGetPipelineIndirectDeviceAddressNV) {
        mvk_unsupported(c, "vkGetPipelineIndirectDeviceAddressNV");
        return;
    }
    VkDeviceAddress result = c->fn->vkGetPipelineIndirectDeviceAddressNV(device, pInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u64(e, (uint64_t)(result));
    mvk_reply_end(c);
}

static void run_vkCmdSetDepthClampEnableEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 depthClampEnable;
    depthClampEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDepthClampEnableEXT");
        return;
    }
    if (!c->fn->vkCmdSetDepthClampEnableEXT) {
        mvk_unsupported(c, "vkCmdSetDepthClampEnableEXT");
        return;
    }
    c->fn->vkCmdSetDepthClampEnableEXT(commandBuffer, depthClampEnable);
}

static void run_vkCmdSetPolygonModeEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkPolygonMode polygonMode;
    polygonMode = (VkPolygonMode)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetPolygonModeEXT");
        return;
    }
    if (!c->fn->vkCmdSetPolygonModeEXT) {
        mvk_unsupported(c, "vkCmdSetPolygonModeEXT");
        return;
    }
    c->fn->vkCmdSetPolygonModeEXT(commandBuffer, polygonMode);
}

static void run_vkCmdSetRasterizationSamplesEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkSampleCountFlagBits rasterizationSamples;
    rasterizationSamples = (VkSampleCountFlagBits)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetRasterizationSamplesEXT");
        return;
    }
    if (!c->fn->vkCmdSetRasterizationSamplesEXT) {
        mvk_unsupported(c, "vkCmdSetRasterizationSamplesEXT");
        return;
    }
    c->fn->vkCmdSetRasterizationSamplesEXT(commandBuffer, rasterizationSamples);
}

static void run_vkCmdSetSampleMaskEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkSampleCountFlagBits samples;
    VkSampleMask * pSampleMask;
    samples = (VkSampleCountFlagBits)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)((samples + 31) / 32) * sizeof(VkSampleMask));
        mvk_get_bytes(d, items, (size_t)((samples + 31) / 32) * sizeof(VkSampleMask));
        pSampleMask = items;
    } else {
        pSampleMask = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetSampleMaskEXT");
        return;
    }
    if (!c->fn->vkCmdSetSampleMaskEXT) {
        mvk_unsupported(c, "vkCmdSetSampleMaskEXT");
        return;
    }
    c->fn->vkCmdSetSampleMaskEXT(commandBuffer, samples, pSampleMask);
}

static void run_vkCmdSetAlphaToCoverageEnableEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 alphaToCoverageEnable;
    alphaToCoverageEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetAlphaToCoverageEnableEXT");
        return;
    }
    if (!c->fn->vkCmdSetAlphaToCoverageEnableEXT) {
        mvk_unsupported(c, "vkCmdSetAlphaToCoverageEnableEXT");
        return;
    }
    c->fn->vkCmdSetAlphaToCoverageEnableEXT(commandBuffer, alphaToCoverageEnable);
}

static void run_vkCmdSetAlphaToOneEnableEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 alphaToOneEnable;
    alphaToOneEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetAlphaToOneEnableEXT");
        return;
    }
    if (!c->fn->vkCmdSetAlphaToOneEnableEXT) {
        mvk_unsupported(c, "vkCmdSetAlphaToOneEnableEXT");
        return;
    }
    c->fn->vkCmdSetAlphaToOneEnableEXT(commandBuffer, alphaToOneEnable);
}

static void run_vkCmdSetLogicOpEnableEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 logicOpEnable;
    logicOpEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetLogicOpEnableEXT");
        return;
    }
    if (!c->fn->vkCmdSetLogicOpEnableEXT) {
        mvk_unsupported(c, "vkCmdSetLogicOpEnableEXT");
        return;
    }
    c->fn->vkCmdSetLogicOpEnableEXT(commandBuffer, logicOpEnable);
}

static void run_vkCmdSetColorBlendEnableEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstAttachment;
    uint32_t attachmentCount;
    VkBool32 * pColorBlendEnables;
    firstAttachment = (uint32_t)mvk_get_u32(d);
    attachmentCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(attachmentCount) * sizeof(VkBool32));
        mvk_get_bytes(d, items, (size_t)(attachmentCount) * sizeof(VkBool32));
        pColorBlendEnables = items;
    } else {
        pColorBlendEnables = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetColorBlendEnableEXT");
        return;
    }
    if (!c->fn->vkCmdSetColorBlendEnableEXT) {
        mvk_unsupported(c, "vkCmdSetColorBlendEnableEXT");
        return;
    }
    c->fn->vkCmdSetColorBlendEnableEXT(commandBuffer, firstAttachment, attachmentCount, pColorBlendEnables);
}

static void run_vkCmdSetColorBlendEquationEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstAttachment;
    uint32_t attachmentCount;
    VkColorBlendEquationEXT * pColorBlendEquations;
    firstAttachment = (uint32_t)mvk_get_u32(d);
    attachmentCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkColorBlendEquationEXT *items = mvk_alloc(d, (size_t)(attachmentCount) * sizeof(VkColorBlendEquationEXT));
        for (uint32_t i = 0; i < (uint32_t)(attachmentCount); i++)
            mvk_get_VkColorBlendEquationEXT(d, &items[i]);
        pColorBlendEquations = items;
    } else {
        pColorBlendEquations = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetColorBlendEquationEXT");
        return;
    }
    if (!c->fn->vkCmdSetColorBlendEquationEXT) {
        mvk_unsupported(c, "vkCmdSetColorBlendEquationEXT");
        return;
    }
    c->fn->vkCmdSetColorBlendEquationEXT(commandBuffer, firstAttachment, attachmentCount, pColorBlendEquations);
}

static void run_vkCmdSetColorWriteMaskEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstAttachment;
    uint32_t attachmentCount;
    VkColorComponentFlags * pColorWriteMasks;
    firstAttachment = (uint32_t)mvk_get_u32(d);
    attachmentCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(attachmentCount) * sizeof(VkColorComponentFlags));
        mvk_get_bytes(d, items, (size_t)(attachmentCount) * sizeof(VkColorComponentFlags));
        pColorWriteMasks = items;
    } else {
        pColorWriteMasks = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetColorWriteMaskEXT");
        return;
    }
    if (!c->fn->vkCmdSetColorWriteMaskEXT) {
        mvk_unsupported(c, "vkCmdSetColorWriteMaskEXT");
        return;
    }
    c->fn->vkCmdSetColorWriteMaskEXT(commandBuffer, firstAttachment, attachmentCount, pColorWriteMasks);
}

static void run_vkCmdSetTessellationDomainOriginEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkTessellationDomainOrigin domainOrigin;
    domainOrigin = (VkTessellationDomainOrigin)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetTessellationDomainOriginEXT");
        return;
    }
    if (!c->fn->vkCmdSetTessellationDomainOriginEXT) {
        mvk_unsupported(c, "vkCmdSetTessellationDomainOriginEXT");
        return;
    }
    c->fn->vkCmdSetTessellationDomainOriginEXT(commandBuffer, domainOrigin);
}

static void run_vkCmdSetRasterizationStreamEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t rasterizationStream;
    rasterizationStream = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetRasterizationStreamEXT");
        return;
    }
    if (!c->fn->vkCmdSetRasterizationStreamEXT) {
        mvk_unsupported(c, "vkCmdSetRasterizationStreamEXT");
        return;
    }
    c->fn->vkCmdSetRasterizationStreamEXT(commandBuffer, rasterizationStream);
}

static void run_vkCmdSetConservativeRasterizationModeEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkConservativeRasterizationModeEXT conservativeRasterizationMode;
    conservativeRasterizationMode = (VkConservativeRasterizationModeEXT)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetConservativeRasterizationModeEXT");
        return;
    }
    if (!c->fn->vkCmdSetConservativeRasterizationModeEXT) {
        mvk_unsupported(c, "vkCmdSetConservativeRasterizationModeEXT");
        return;
    }
    c->fn->vkCmdSetConservativeRasterizationModeEXT(commandBuffer, conservativeRasterizationMode);
}

static void run_vkCmdSetExtraPrimitiveOverestimationSizeEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    float extraPrimitiveOverestimationSize;
    extraPrimitiveOverestimationSize = mvk_bits_f32(mvk_get_u32(d));
    if (d->error) {
        mvk_malformed(c, "vkCmdSetExtraPrimitiveOverestimationSizeEXT");
        return;
    }
    if (!c->fn->vkCmdSetExtraPrimitiveOverestimationSizeEXT) {
        mvk_unsupported(c, "vkCmdSetExtraPrimitiveOverestimationSizeEXT");
        return;
    }
    c->fn->vkCmdSetExtraPrimitiveOverestimationSizeEXT(commandBuffer, extraPrimitiveOverestimationSize);
}

static void run_vkCmdSetDepthClipEnableEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 depthClipEnable;
    depthClipEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDepthClipEnableEXT");
        return;
    }
    if (!c->fn->vkCmdSetDepthClipEnableEXT) {
        mvk_unsupported(c, "vkCmdSetDepthClipEnableEXT");
        return;
    }
    c->fn->vkCmdSetDepthClipEnableEXT(commandBuffer, depthClipEnable);
}

static void run_vkCmdSetSampleLocationsEnableEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 sampleLocationsEnable;
    sampleLocationsEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetSampleLocationsEnableEXT");
        return;
    }
    if (!c->fn->vkCmdSetSampleLocationsEnableEXT) {
        mvk_unsupported(c, "vkCmdSetSampleLocationsEnableEXT");
        return;
    }
    c->fn->vkCmdSetSampleLocationsEnableEXT(commandBuffer, sampleLocationsEnable);
}

static void run_vkCmdSetColorBlendAdvancedEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstAttachment;
    uint32_t attachmentCount;
    VkColorBlendAdvancedEXT * pColorBlendAdvanced;
    firstAttachment = (uint32_t)mvk_get_u32(d);
    attachmentCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkColorBlendAdvancedEXT *items = mvk_alloc(d, (size_t)(attachmentCount) * sizeof(VkColorBlendAdvancedEXT));
        for (uint32_t i = 0; i < (uint32_t)(attachmentCount); i++)
            mvk_get_VkColorBlendAdvancedEXT(d, &items[i]);
        pColorBlendAdvanced = items;
    } else {
        pColorBlendAdvanced = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetColorBlendAdvancedEXT");
        return;
    }
    if (!c->fn->vkCmdSetColorBlendAdvancedEXT) {
        mvk_unsupported(c, "vkCmdSetColorBlendAdvancedEXT");
        return;
    }
    c->fn->vkCmdSetColorBlendAdvancedEXT(commandBuffer, firstAttachment, attachmentCount, pColorBlendAdvanced);
}

static void run_vkCmdSetProvokingVertexModeEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkProvokingVertexModeEXT provokingVertexMode;
    provokingVertexMode = (VkProvokingVertexModeEXT)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetProvokingVertexModeEXT");
        return;
    }
    if (!c->fn->vkCmdSetProvokingVertexModeEXT) {
        mvk_unsupported(c, "vkCmdSetProvokingVertexModeEXT");
        return;
    }
    c->fn->vkCmdSetProvokingVertexModeEXT(commandBuffer, provokingVertexMode);
}

static void run_vkCmdSetLineRasterizationModeEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkLineRasterizationModeEXT lineRasterizationMode;
    lineRasterizationMode = (VkLineRasterizationModeEXT)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetLineRasterizationModeEXT");
        return;
    }
    if (!c->fn->vkCmdSetLineRasterizationModeEXT) {
        mvk_unsupported(c, "vkCmdSetLineRasterizationModeEXT");
        return;
    }
    c->fn->vkCmdSetLineRasterizationModeEXT(commandBuffer, lineRasterizationMode);
}

static void run_vkCmdSetLineStippleEnableEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 stippledLineEnable;
    stippledLineEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetLineStippleEnableEXT");
        return;
    }
    if (!c->fn->vkCmdSetLineStippleEnableEXT) {
        mvk_unsupported(c, "vkCmdSetLineStippleEnableEXT");
        return;
    }
    c->fn->vkCmdSetLineStippleEnableEXT(commandBuffer, stippledLineEnable);
}

static void run_vkCmdSetDepthClipNegativeOneToOneEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 negativeOneToOne;
    negativeOneToOne = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDepthClipNegativeOneToOneEXT");
        return;
    }
    if (!c->fn->vkCmdSetDepthClipNegativeOneToOneEXT) {
        mvk_unsupported(c, "vkCmdSetDepthClipNegativeOneToOneEXT");
        return;
    }
    c->fn->vkCmdSetDepthClipNegativeOneToOneEXT(commandBuffer, negativeOneToOne);
}

static void run_vkCmdSetViewportWScalingEnableNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 viewportWScalingEnable;
    viewportWScalingEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetViewportWScalingEnableNV");
        return;
    }
    if (!c->fn->vkCmdSetViewportWScalingEnableNV) {
        mvk_unsupported(c, "vkCmdSetViewportWScalingEnableNV");
        return;
    }
    c->fn->vkCmdSetViewportWScalingEnableNV(commandBuffer, viewportWScalingEnable);
}

static void run_vkCmdSetViewportSwizzleNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t firstViewport;
    uint32_t viewportCount;
    VkViewportSwizzleNV * pViewportSwizzles;
    firstViewport = (uint32_t)mvk_get_u32(d);
    viewportCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkViewportSwizzleNV *items = mvk_alloc(d, (size_t)(viewportCount) * sizeof(VkViewportSwizzleNV));
        for (uint32_t i = 0; i < (uint32_t)(viewportCount); i++)
            mvk_get_VkViewportSwizzleNV(d, &items[i]);
        pViewportSwizzles = items;
    } else {
        pViewportSwizzles = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetViewportSwizzleNV");
        return;
    }
    if (!c->fn->vkCmdSetViewportSwizzleNV) {
        mvk_unsupported(c, "vkCmdSetViewportSwizzleNV");
        return;
    }
    c->fn->vkCmdSetViewportSwizzleNV(commandBuffer, firstViewport, viewportCount, pViewportSwizzles);
}

static void run_vkCmdSetCoverageToColorEnableNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 coverageToColorEnable;
    coverageToColorEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetCoverageToColorEnableNV");
        return;
    }
    if (!c->fn->vkCmdSetCoverageToColorEnableNV) {
        mvk_unsupported(c, "vkCmdSetCoverageToColorEnableNV");
        return;
    }
    c->fn->vkCmdSetCoverageToColorEnableNV(commandBuffer, coverageToColorEnable);
}

static void run_vkCmdSetCoverageToColorLocationNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t coverageToColorLocation;
    coverageToColorLocation = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetCoverageToColorLocationNV");
        return;
    }
    if (!c->fn->vkCmdSetCoverageToColorLocationNV) {
        mvk_unsupported(c, "vkCmdSetCoverageToColorLocationNV");
        return;
    }
    c->fn->vkCmdSetCoverageToColorLocationNV(commandBuffer, coverageToColorLocation);
}

static void run_vkCmdSetCoverageModulationModeNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCoverageModulationModeNV coverageModulationMode;
    coverageModulationMode = (VkCoverageModulationModeNV)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetCoverageModulationModeNV");
        return;
    }
    if (!c->fn->vkCmdSetCoverageModulationModeNV) {
        mvk_unsupported(c, "vkCmdSetCoverageModulationModeNV");
        return;
    }
    c->fn->vkCmdSetCoverageModulationModeNV(commandBuffer, coverageModulationMode);
}

static void run_vkCmdSetCoverageModulationTableEnableNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 coverageModulationTableEnable;
    coverageModulationTableEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetCoverageModulationTableEnableNV");
        return;
    }
    if (!c->fn->vkCmdSetCoverageModulationTableEnableNV) {
        mvk_unsupported(c, "vkCmdSetCoverageModulationTableEnableNV");
        return;
    }
    c->fn->vkCmdSetCoverageModulationTableEnableNV(commandBuffer, coverageModulationTableEnable);
}

static void run_vkCmdSetCoverageModulationTableNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t coverageModulationTableCount;
    float * pCoverageModulationTable;
    coverageModulationTableCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(coverageModulationTableCount) * sizeof(float));
        mvk_get_bytes(d, items, (size_t)(coverageModulationTableCount) * sizeof(float));
        pCoverageModulationTable = items;
    } else {
        pCoverageModulationTable = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetCoverageModulationTableNV");
        return;
    }
    if (!c->fn->vkCmdSetCoverageModulationTableNV) {
        mvk_unsupported(c, "vkCmdSetCoverageModulationTableNV");
        return;
    }
    c->fn->vkCmdSetCoverageModulationTableNV(commandBuffer, coverageModulationTableCount, pCoverageModulationTable);
}

static void run_vkCmdSetShadingRateImageEnableNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 shadingRateImageEnable;
    shadingRateImageEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetShadingRateImageEnableNV");
        return;
    }
    if (!c->fn->vkCmdSetShadingRateImageEnableNV) {
        mvk_unsupported(c, "vkCmdSetShadingRateImageEnableNV");
        return;
    }
    c->fn->vkCmdSetShadingRateImageEnableNV(commandBuffer, shadingRateImageEnable);
}

static void run_vkCmdSetRepresentativeFragmentTestEnableNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 representativeFragmentTestEnable;
    representativeFragmentTestEnable = (VkBool32)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetRepresentativeFragmentTestEnableNV");
        return;
    }
    if (!c->fn->vkCmdSetRepresentativeFragmentTestEnableNV) {
        mvk_unsupported(c, "vkCmdSetRepresentativeFragmentTestEnableNV");
        return;
    }
    c->fn->vkCmdSetRepresentativeFragmentTestEnableNV(commandBuffer, representativeFragmentTestEnable);
}

static void run_vkCmdSetCoverageReductionModeNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCoverageReductionModeNV coverageReductionMode;
    coverageReductionMode = (VkCoverageReductionModeNV)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetCoverageReductionModeNV");
        return;
    }
    if (!c->fn->vkCmdSetCoverageReductionModeNV) {
        mvk_unsupported(c, "vkCmdSetCoverageReductionModeNV");
        return;
    }
    c->fn->vkCmdSetCoverageReductionModeNV(commandBuffer, coverageReductionMode);
}

static void run_vkGetShaderModuleIdentifierEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkShaderModule shaderModule;
    device = (VkDevice)mvk_get_handle(d, 1);
    shaderModule = (VkShaderModule)mvk_get_handle(d, 0);
    VkShaderModuleIdentifierEXT *pIdentifier = mvk_get_out_shape(d, sizeof(VkShaderModuleIdentifierEXT));
    if (d->error) {
        mvk_malformed(c, "vkGetShaderModuleIdentifierEXT");
        return;
    }
    if (!c->fn->vkGetShaderModuleIdentifierEXT) {
        mvk_unsupported(c, "vkGetShaderModuleIdentifierEXT");
        return;
    }
    c->fn->vkGetShaderModuleIdentifierEXT(device, shaderModule, pIdentifier);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkShaderModuleIdentifierEXT(e, pIdentifier);
    mvk_reply_end(c);
}

static void run_vkGetShaderModuleCreateInfoIdentifierEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkShaderModuleCreateInfo * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkShaderModuleCreateInfo *items = mvk_alloc(d, (size_t)(1) * sizeof(VkShaderModuleCreateInfo));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkShaderModuleCreateInfo(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    VkShaderModuleIdentifierEXT *pIdentifier = mvk_get_out_shape(d, sizeof(VkShaderModuleIdentifierEXT));
    if (d->error) {
        mvk_malformed(c, "vkGetShaderModuleCreateInfoIdentifierEXT");
        return;
    }
    if (!c->fn->vkGetShaderModuleCreateInfoIdentifierEXT) {
        mvk_unsupported(c, "vkGetShaderModuleCreateInfoIdentifierEXT");
        return;
    }
    c->fn->vkGetShaderModuleCreateInfoIdentifierEXT(device, pCreateInfo, pIdentifier);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkShaderModuleIdentifierEXT(e, pIdentifier);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceOpticalFlowImageFormatsNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkOpticalFlowImageFormatInfoNV * pOpticalFlowImageFormatInfo;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkOpticalFlowImageFormatInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkOpticalFlowImageFormatInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkOpticalFlowImageFormatInfoNV(d, &items[i]);
        pOpticalFlowImageFormatInfo = items;
    } else {
        pOpticalFlowImageFormatInfo = NULL;
    }
    uint32_t pFormatCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pFormatCount = &pFormatCount_value;
    int want_pImageFormatProperties = mvk_get_u32(d);
    VkOpticalFlowImageFormatPropertiesNV *pImageFormatProperties = NULL;
    uint32_t shape_count_pImageFormatProperties = 0;
    if (want_pImageFormatProperties) {
        pImageFormatProperties = mvk_alloc(d, sizeof(VkOpticalFlowImageFormatPropertiesNV) * (size_t)(*pFormatCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pFormatCount); i++)
            mvk_get_out_shape_into(d, &pImageFormatProperties[i], sizeof(VkOpticalFlowImageFormatPropertiesNV));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceOpticalFlowImageFormatsNV");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceOpticalFlowImageFormatsNV) {
        mvk_unsupported(c, "vkGetPhysicalDeviceOpticalFlowImageFormatsNV");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceOpticalFlowImageFormatsNV(physicalDevice, pOpticalFlowImageFormatInfo, pFormatCount, pImageFormatProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pFormatCount));
    mvk_put_u32(e, pImageFormatProperties != NULL);
    if (pImageFormatProperties) {
        mvk_put_u32(e, (uint32_t)(*pFormatCount));
        for (uint32_t i = 0; i < (uint32_t)(*pFormatCount); i++)
            reply_VkOpticalFlowImageFormatPropertiesNV(e, &pImageFormatProperties[i]);
    }
    mvk_reply_end(c);
}

static void run_vkCreateOpticalFlowSessionNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkOpticalFlowSessionCreateInfoNV * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkOpticalFlowSessionCreateInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkOpticalFlowSessionCreateInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkOpticalFlowSessionCreateInfoNV(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pSession = mvk_get_u64(d);
    VkOpticalFlowSessionNV pSession_value = VK_NULL_HANDLE;
    VkOpticalFlowSessionNV *pSession = &pSession_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateOpticalFlowSessionNV");
        return;
    }
    if (!c->fn->vkCreateOpticalFlowSessionNV) {
        mvk_unsupported(c, "vkCreateOpticalFlowSessionNV");
        return;
    }
    VkResult result = c->fn->vkCreateOpticalFlowSessionNV(device, pCreateInfo, NULL, pSession);
    mvk_bind_id(c, id_pSession, (uint64_t)pSession_value);
    mvk_track(c->conn, (uint64_t)pSession_value, MVK_KIND_VkOpticalFlowSessionNV);
    mvk_check(c, result, "vkCreateOpticalFlowSessionNV");
}

static void run_vkDestroyOpticalFlowSessionNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkOpticalFlowSessionNV session;
    device = (VkDevice)mvk_get_handle(d, 1);
    session = (VkOpticalFlowSessionNV)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyOpticalFlowSessionNV");
        return;
    }
    if (!c->fn->vkDestroyOpticalFlowSessionNV) {
        mvk_unsupported(c, "vkDestroyOpticalFlowSessionNV");
        return;
    }
    c->fn->vkDestroyOpticalFlowSessionNV(device, session, NULL);
    mvk_forget(c->conn, (uint64_t)session);
}

static void run_vkBindOpticalFlowSessionImageNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkOpticalFlowSessionNV session;
    VkOpticalFlowSessionBindingPointNV bindingPoint;
    VkImageView view;
    VkImageLayout layout;
    device = (VkDevice)mvk_get_handle(d, 1);
    session = (VkOpticalFlowSessionNV)mvk_get_handle(d, 0);
    bindingPoint = (VkOpticalFlowSessionBindingPointNV)mvk_get_u32(d);
    view = (VkImageView)mvk_get_handle(d, 0);
    layout = (VkImageLayout)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkBindOpticalFlowSessionImageNV");
        return;
    }
    if (!c->fn->vkBindOpticalFlowSessionImageNV) {
        mvk_unsupported(c, "vkBindOpticalFlowSessionImageNV");
        return;
    }
    VkResult result = c->fn->vkBindOpticalFlowSessionImageNV(device, session, bindingPoint, view, layout);
    mvk_check(c, result, "vkBindOpticalFlowSessionImageNV");
}

static void run_vkCmdOpticalFlowExecuteNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkOpticalFlowSessionNV session;
    VkOpticalFlowExecuteInfoNV * pExecuteInfo;
    session = (VkOpticalFlowSessionNV)mvk_get_handle(d, 0);
    if (mvk_get_u32(d)) {
        VkOpticalFlowExecuteInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkOpticalFlowExecuteInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkOpticalFlowExecuteInfoNV(d, &items[i]);
        pExecuteInfo = items;
    } else {
        pExecuteInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdOpticalFlowExecuteNV");
        return;
    }
    if (!c->fn->vkCmdOpticalFlowExecuteNV) {
        mvk_unsupported(c, "vkCmdOpticalFlowExecuteNV");
        return;
    }
    c->fn->vkCmdOpticalFlowExecuteNV(commandBuffer, session, pExecuteInfo);
}

static void run_vkCreateShadersEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t createInfoCount;
    VkShaderCreateInfoEXT * pCreateInfos;
    device = (VkDevice)mvk_get_handle(d, 1);
    createInfoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkShaderCreateInfoEXT *items = mvk_alloc(d, (size_t)(createInfoCount) * sizeof(VkShaderCreateInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
            mvk_get_VkShaderCreateInfoEXT(d, &items[i]);
        pCreateInfos = items;
    } else {
        pCreateInfos = NULL;
    }
    uint64_t *ids_pShaders = mvk_alloc(d, sizeof(uint64_t) * (size_t)(createInfoCount));
    VkShaderEXT *pShaders = mvk_alloc(d, sizeof(VkShaderEXT) * (size_t)(createInfoCount));
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++)
        ids_pShaders[i] = mvk_get_u64(d);
    if (d->error) {
        mvk_malformed(c, "vkCreateShadersEXT");
        return;
    }
    if (!c->fn->vkCreateShadersEXT) {
        mvk_unsupported(c, "vkCreateShadersEXT");
        return;
    }
    VkResult result = c->fn->vkCreateShadersEXT(device, createInfoCount, pCreateInfos, NULL, pShaders);
    for (uint32_t i = 0; i < (uint32_t)(createInfoCount); i++) {
        mvk_bind_id(c, ids_pShaders[i], (uint64_t)pShaders[i]);
        mvk_track(c->conn, (uint64_t)pShaders[i], MVK_KIND_VkShaderEXT);
    }
    mvk_check(c, result, "vkCreateShadersEXT");
}

static void run_vkDestroyShaderEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkShaderEXT shader;
    device = (VkDevice)mvk_get_handle(d, 1);
    shader = (VkShaderEXT)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyShaderEXT");
        return;
    }
    if (!c->fn->vkDestroyShaderEXT) {
        mvk_unsupported(c, "vkDestroyShaderEXT");
        return;
    }
    c->fn->vkDestroyShaderEXT(device, shader, NULL);
    mvk_forget(c->conn, (uint64_t)shader);
}

static void run_vkGetShaderBinaryDataEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkShaderEXT shader;
    device = (VkDevice)mvk_get_handle(d, 1);
    shader = (VkShaderEXT)mvk_get_handle(d, 0);
    size_t pDataSize_value;
    memset(&pDataSize_value, 0, sizeof(pDataSize_value));
    size_t *pDataSize = &pDataSize_value;
    int want_pData = mvk_get_u32(d);
    uint8_t *pData = NULL;
    if (want_pData) {
        pData = mvk_alloc(d, sizeof(uint8_t) * (size_t)(*pDataSize) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetShaderBinaryDataEXT");
        return;
    }
    if (!c->fn->vkGetShaderBinaryDataEXT) {
        mvk_unsupported(c, "vkGetShaderBinaryDataEXT");
        return;
    }
    VkResult result = c->fn->vkGetShaderBinaryDataEXT(device, shader, pDataSize, pData);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u64(e, (uint64_t)(*pDataSize));
    mvk_put_u32(e, pData != NULL);
    if (pData) {
        mvk_put_u32(e, (uint32_t)(*pDataSize));
        mvk_put_bytes(e, pData, (size_t)(*pDataSize));
    }
    mvk_reply_end(c);
}

static void run_vkCmdBindShadersEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t stageCount;
    VkShaderStageFlagBits * pStages;
    VkShaderEXT * pShaders;
    stageCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        void *items = mvk_alloc(d, (size_t)(stageCount) * sizeof(VkShaderStageFlagBits));
        mvk_get_bytes(d, items, (size_t)(stageCount) * sizeof(VkShaderStageFlagBits));
        pStages = items;
    } else {
        pStages = NULL;
    }
    if (mvk_get_u32(d)) {
        VkShaderEXT *items = mvk_alloc(d, (size_t)(stageCount) * sizeof(VkShaderEXT));
        for (uint32_t i = 0; i < (uint32_t)(stageCount); i++)
            items[i] = (VkShaderEXT)mvk_get_handle(d, 0);
        pShaders = items;
    } else {
        pShaders = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindShadersEXT");
        return;
    }
    if (!c->fn->vkCmdBindShadersEXT) {
        mvk_unsupported(c, "vkCmdBindShadersEXT");
        return;
    }
    c->fn->vkCmdBindShadersEXT(commandBuffer, stageCount, pStages, pShaders);
}

static void run_vkCmdSetDepthClampRangeEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDepthClampModeEXT depthClampMode;
    VkDepthClampRangeEXT * pDepthClampRange;
    depthClampMode = (VkDepthClampModeEXT)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkDepthClampRangeEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDepthClampRangeEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDepthClampRangeEXT(d, &items[i]);
        pDepthClampRange = items;
    } else {
        pDepthClampRange = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDepthClampRangeEXT");
        return;
    }
    if (!c->fn->vkCmdSetDepthClampRangeEXT) {
        mvk_unsupported(c, "vkCmdSetDepthClampRangeEXT");
        return;
    }
    c->fn->vkCmdSetDepthClampRangeEXT(commandBuffer, depthClampMode, pDepthClampRange);
}

static void run_vkGetPhysicalDeviceCooperativeVectorPropertiesNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    uint32_t pPropertyCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pPropertyCount = &pPropertyCount_value;
    int want_pProperties = mvk_get_u32(d);
    VkCooperativeVectorPropertiesNV *pProperties = NULL;
    uint32_t shape_count_pProperties = 0;
    if (want_pProperties) {
        pProperties = mvk_alloc(d, sizeof(VkCooperativeVectorPropertiesNV) * (size_t)(*pPropertyCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            mvk_get_out_shape_into(d, &pProperties[i], sizeof(VkCooperativeVectorPropertiesNV));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceCooperativeVectorPropertiesNV");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceCooperativeVectorPropertiesNV) {
        mvk_unsupported(c, "vkGetPhysicalDeviceCooperativeVectorPropertiesNV");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceCooperativeVectorPropertiesNV(physicalDevice, pPropertyCount, pProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pPropertyCount));
    mvk_put_u32(e, pProperties != NULL);
    if (pProperties) {
        mvk_put_u32(e, (uint32_t)(*pPropertyCount));
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            reply_VkCooperativeVectorPropertiesNV(e, &pProperties[i]);
    }
    mvk_reply_end(c);
}

static void run_vkConvertCooperativeVectorMatrixNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkConvertCooperativeVectorMatrixInfoNV * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkConvertCooperativeVectorMatrixInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkConvertCooperativeVectorMatrixInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkConvertCooperativeVectorMatrixInfoNV(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkConvertCooperativeVectorMatrixNV");
        return;
    }
    if (!c->fn->vkConvertCooperativeVectorMatrixNV) {
        mvk_unsupported(c, "vkConvertCooperativeVectorMatrixNV");
        return;
    }
    VkResult result = c->fn->vkConvertCooperativeVectorMatrixNV(device, pInfo);
    mvk_check(c, result, "vkConvertCooperativeVectorMatrixNV");
}

static void run_vkCmdConvertCooperativeVectorMatrixNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t infoCount;
    VkConvertCooperativeVectorMatrixInfoNV * pInfos;
    infoCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkConvertCooperativeVectorMatrixInfoNV *items = mvk_alloc(d, (size_t)(infoCount) * sizeof(VkConvertCooperativeVectorMatrixInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(infoCount); i++)
            mvk_get_VkConvertCooperativeVectorMatrixInfoNV(d, &items[i]);
        pInfos = items;
    } else {
        pInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdConvertCooperativeVectorMatrixNV");
        return;
    }
    if (!c->fn->vkCmdConvertCooperativeVectorMatrixNV) {
        mvk_unsupported(c, "vkCmdConvertCooperativeVectorMatrixNV");
        return;
    }
    c->fn->vkCmdConvertCooperativeVectorMatrixNV(commandBuffer, infoCount, pInfos);
}

static void run_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    uint32_t pPropertyCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pPropertyCount = &pPropertyCount_value;
    int want_pProperties = mvk_get_u32(d);
    VkCooperativeMatrixPropertiesKHR *pProperties = NULL;
    uint32_t shape_count_pProperties = 0;
    if (want_pProperties) {
        pProperties = mvk_alloc(d, sizeof(VkCooperativeMatrixPropertiesKHR) * (size_t)(*pPropertyCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            mvk_get_out_shape_into(d, &pProperties[i], sizeof(VkCooperativeMatrixPropertiesKHR));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR) {
        mvk_unsupported(c, "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(physicalDevice, pPropertyCount, pProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pPropertyCount));
    mvk_put_u32(e, pProperties != NULL);
    if (pProperties) {
        mvk_put_u32(e, (uint32_t)(*pPropertyCount));
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            reply_VkCooperativeMatrixPropertiesKHR(e, &pProperties[i]);
    }
    mvk_reply_end(c);
}

static void run_vkCmdSetAttachmentFeedbackLoopEnableEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkImageAspectFlags aspectMask;
    aspectMask = (VkImageAspectFlags)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetAttachmentFeedbackLoopEnableEXT");
        return;
    }
    if (!c->fn->vkCmdSetAttachmentFeedbackLoopEnableEXT) {
        mvk_unsupported(c, "vkCmdSetAttachmentFeedbackLoopEnableEXT");
        return;
    }
    c->fn->vkCmdSetAttachmentFeedbackLoopEnableEXT(commandBuffer, aspectMask);
}

static void run_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    uint32_t pTimeDomainCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pTimeDomainCount = &pTimeDomainCount_value;
    int want_pTimeDomains = mvk_get_u32(d);
    VkTimeDomainKHR *pTimeDomains = NULL;
    if (want_pTimeDomains) {
        pTimeDomains = mvk_alloc(d, sizeof(VkTimeDomainKHR) * (size_t)(*pTimeDomainCount) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceCalibrateableTimeDomainsKHR");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceCalibrateableTimeDomainsKHR) {
        mvk_unsupported(c, "vkGetPhysicalDeviceCalibrateableTimeDomainsKHR");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(physicalDevice, pTimeDomainCount, pTimeDomains);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pTimeDomainCount));
    mvk_put_u32(e, pTimeDomains != NULL);
    if (pTimeDomains) {
        mvk_put_u32(e, (uint32_t)(*pTimeDomainCount));
        mvk_put_bytes(e, pTimeDomains, (size_t)(*pTimeDomainCount) * sizeof(*pTimeDomains));
    }
    mvk_reply_end(c);
}

static void run_vkGetCalibratedTimestampsKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint32_t timestampCount;
    VkCalibratedTimestampInfoKHR * pTimestampInfos;
    device = (VkDevice)mvk_get_handle(d, 1);
    timestampCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkCalibratedTimestampInfoKHR *items = mvk_alloc(d, (size_t)(timestampCount) * sizeof(VkCalibratedTimestampInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(timestampCount); i++)
            mvk_get_VkCalibratedTimestampInfoKHR(d, &items[i]);
        pTimestampInfos = items;
    } else {
        pTimestampInfos = NULL;
    }
    int want_pTimestamps = mvk_get_u32(d);
    uint64_t *pTimestamps = NULL;
    uint64_t pMaxDeviation_value;
    memset(&pMaxDeviation_value, 0, sizeof(pMaxDeviation_value));
    uint64_t *pMaxDeviation = &pMaxDeviation_value;
    if (want_pTimestamps) {
        pTimestamps = mvk_alloc(d, sizeof(uint64_t) * (size_t)(timestampCount) + 1);
    }
    if (d->error) {
        mvk_malformed(c, "vkGetCalibratedTimestampsKHR");
        return;
    }
    if (!c->fn->vkGetCalibratedTimestampsKHR) {
        mvk_unsupported(c, "vkGetCalibratedTimestampsKHR");
        return;
    }
    VkResult result = c->fn->vkGetCalibratedTimestampsKHR(device, timestampCount, pTimestampInfos, pTimestamps, pMaxDeviation);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, pTimestamps != NULL);
    if (pTimestamps) {
        mvk_put_u32(e, (uint32_t)(timestampCount));
        mvk_put_bytes(e, pTimestamps, (size_t)(timestampCount) * sizeof(*pTimestamps));
    }
    mvk_put_u64(e, (uint64_t)(*pMaxDeviation));
    mvk_reply_end(c);
}

static void run_vkCmdSetDescriptorBufferOffsets2EXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkSetDescriptorBufferOffsetsInfoEXT * pSetDescriptorBufferOffsetsInfo;
    if (mvk_get_u32(d)) {
        VkSetDescriptorBufferOffsetsInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkSetDescriptorBufferOffsetsInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkSetDescriptorBufferOffsetsInfoEXT(d, &items[i]);
        pSetDescriptorBufferOffsetsInfo = items;
    } else {
        pSetDescriptorBufferOffsetsInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetDescriptorBufferOffsets2EXT");
        return;
    }
    if (!c->fn->vkCmdSetDescriptorBufferOffsets2EXT) {
        mvk_unsupported(c, "vkCmdSetDescriptorBufferOffsets2EXT");
        return;
    }
    c->fn->vkCmdSetDescriptorBufferOffsets2EXT(commandBuffer, pSetDescriptorBufferOffsetsInfo);
}

static void run_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBindDescriptorBufferEmbeddedSamplersInfoEXT * pBindDescriptorBufferEmbeddedSamplersInfo;
    if (mvk_get_u32(d)) {
        VkBindDescriptorBufferEmbeddedSamplersInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBindDescriptorBufferEmbeddedSamplersInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBindDescriptorBufferEmbeddedSamplersInfoEXT(d, &items[i]);
        pBindDescriptorBufferEmbeddedSamplersInfo = items;
    } else {
        pBindDescriptorBufferEmbeddedSamplersInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBindDescriptorBufferEmbeddedSamplers2EXT");
        return;
    }
    if (!c->fn->vkCmdBindDescriptorBufferEmbeddedSamplers2EXT) {
        mvk_unsupported(c, "vkCmdBindDescriptorBufferEmbeddedSamplers2EXT");
        return;
    }
    c->fn->vkCmdBindDescriptorBufferEmbeddedSamplers2EXT(commandBuffer, pBindDescriptorBufferEmbeddedSamplersInfo);
}

static void run_vkCmdCopyMemoryIndirectKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyMemoryIndirectInfoKHR * pCopyMemoryIndirectInfo;
    if (mvk_get_u32(d)) {
        VkCopyMemoryIndirectInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyMemoryIndirectInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyMemoryIndirectInfoKHR(d, &items[i]);
        pCopyMemoryIndirectInfo = items;
    } else {
        pCopyMemoryIndirectInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyMemoryIndirectKHR");
        return;
    }
    if (!c->fn->vkCmdCopyMemoryIndirectKHR) {
        mvk_unsupported(c, "vkCmdCopyMemoryIndirectKHR");
        return;
    }
    c->fn->vkCmdCopyMemoryIndirectKHR(commandBuffer, pCopyMemoryIndirectInfo);
}

static void run_vkCmdCopyMemoryToImageIndirectKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkCopyMemoryToImageIndirectInfoKHR * pCopyMemoryToImageIndirectInfo;
    if (mvk_get_u32(d)) {
        VkCopyMemoryToImageIndirectInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkCopyMemoryToImageIndirectInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkCopyMemoryToImageIndirectInfoKHR(d, &items[i]);
        pCopyMemoryToImageIndirectInfo = items;
    } else {
        pCopyMemoryToImageIndirectInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdCopyMemoryToImageIndirectKHR");
        return;
    }
    if (!c->fn->vkCmdCopyMemoryToImageIndirectKHR) {
        mvk_unsupported(c, "vkCmdCopyMemoryToImageIndirectKHR");
        return;
    }
    c->fn->vkCmdCopyMemoryToImageIndirectKHR(commandBuffer, pCopyMemoryToImageIndirectInfo);
}

static void run_vkCmdDecompressMemoryEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkDecompressMemoryInfoEXT * pDecompressMemoryInfoEXT;
    if (mvk_get_u32(d)) {
        VkDecompressMemoryInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkDecompressMemoryInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkDecompressMemoryInfoEXT(d, &items[i]);
        pDecompressMemoryInfoEXT = items;
    } else {
        pDecompressMemoryInfoEXT = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdDecompressMemoryEXT");
        return;
    }
    if (!c->fn->vkCmdDecompressMemoryEXT) {
        mvk_unsupported(c, "vkCmdDecompressMemoryEXT");
        return;
    }
    c->fn->vkCmdDecompressMemoryEXT(commandBuffer, pDecompressMemoryInfoEXT);
}

static void run_vkCmdDecompressMemoryIndirectCountEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkMemoryDecompressionMethodFlagsEXT decompressionMethod;
    VkDeviceAddress indirectCommandsAddress;
    VkDeviceAddress indirectCommandsCountAddress;
    uint32_t maxDecompressionCount;
    uint32_t stride;
    decompressionMethod = (VkMemoryDecompressionMethodFlagsEXT)mvk_get_u64(d);
    indirectCommandsAddress = (VkDeviceAddress)mvk_get_u64(d);
    indirectCommandsCountAddress = (VkDeviceAddress)mvk_get_u64(d);
    maxDecompressionCount = (uint32_t)mvk_get_u32(d);
    stride = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdDecompressMemoryIndirectCountEXT");
        return;
    }
    if (!c->fn->vkCmdDecompressMemoryIndirectCountEXT) {
        mvk_unsupported(c, "vkCmdDecompressMemoryIndirectCountEXT");
        return;
    }
    c->fn->vkCmdDecompressMemoryIndirectCountEXT(commandBuffer, decompressionMethod, indirectCommandsAddress, indirectCommandsCountAddress, maxDecompressionCount, stride);
}

static void run_vkGetClusterAccelerationStructureBuildSizesNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkClusterAccelerationStructureInputInfoNV * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkClusterAccelerationStructureInputInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkClusterAccelerationStructureInputInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkClusterAccelerationStructureInputInfoNV(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo = mvk_get_out_shape(d, sizeof(VkAccelerationStructureBuildSizesInfoKHR));
    if (d->error) {
        mvk_malformed(c, "vkGetClusterAccelerationStructureBuildSizesNV");
        return;
    }
    if (!c->fn->vkGetClusterAccelerationStructureBuildSizesNV) {
        mvk_unsupported(c, "vkGetClusterAccelerationStructureBuildSizesNV");
        return;
    }
    c->fn->vkGetClusterAccelerationStructureBuildSizesNV(device, pInfo, pSizeInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkAccelerationStructureBuildSizesInfoKHR(e, pSizeInfo);
    mvk_reply_end(c);
}

static void run_vkCmdBuildClusterAccelerationStructureIndirectNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkClusterAccelerationStructureCommandsInfoNV * pCommandInfos;
    if (mvk_get_u32(d)) {
        VkClusterAccelerationStructureCommandsInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkClusterAccelerationStructureCommandsInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkClusterAccelerationStructureCommandsInfoNV(d, &items[i]);
        pCommandInfos = items;
    } else {
        pCommandInfos = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBuildClusterAccelerationStructureIndirectNV");
        return;
    }
    if (!c->fn->vkCmdBuildClusterAccelerationStructureIndirectNV) {
        mvk_unsupported(c, "vkCmdBuildClusterAccelerationStructureIndirectNV");
        return;
    }
    c->fn->vkCmdBuildClusterAccelerationStructureIndirectNV(commandBuffer, pCommandInfos);
}

static void run_vkGetPartitionedAccelerationStructuresBuildSizesNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkPartitionedAccelerationStructureInstancesInputNV * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPartitionedAccelerationStructureInstancesInputNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPartitionedAccelerationStructureInstancesInputNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPartitionedAccelerationStructureInstancesInputNV(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo = mvk_get_out_shape(d, sizeof(VkAccelerationStructureBuildSizesInfoKHR));
    if (d->error) {
        mvk_malformed(c, "vkGetPartitionedAccelerationStructuresBuildSizesNV");
        return;
    }
    if (!c->fn->vkGetPartitionedAccelerationStructuresBuildSizesNV) {
        mvk_unsupported(c, "vkGetPartitionedAccelerationStructuresBuildSizesNV");
        return;
    }
    c->fn->vkGetPartitionedAccelerationStructuresBuildSizesNV(device, pInfo, pSizeInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkAccelerationStructureBuildSizesInfoKHR(e, pSizeInfo);
    mvk_reply_end(c);
}

static void run_vkCmdBuildPartitionedAccelerationStructuresNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBuildPartitionedAccelerationStructureInfoNV * pBuildInfo;
    if (mvk_get_u32(d)) {
        VkBuildPartitionedAccelerationStructureInfoNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBuildPartitionedAccelerationStructureInfoNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBuildPartitionedAccelerationStructureInfoNV(d, &items[i]);
        pBuildInfo = items;
    } else {
        pBuildInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBuildPartitionedAccelerationStructuresNV");
        return;
    }
    if (!c->fn->vkCmdBuildPartitionedAccelerationStructuresNV) {
        mvk_unsupported(c, "vkCmdBuildPartitionedAccelerationStructuresNV");
        return;
    }
    c->fn->vkCmdBuildPartitionedAccelerationStructuresNV(commandBuffer, pBuildInfo);
}

static void run_vkGetGeneratedCommandsMemoryRequirementsEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkGeneratedCommandsMemoryRequirementsInfoEXT * pInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkGeneratedCommandsMemoryRequirementsInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkGeneratedCommandsMemoryRequirementsInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkGeneratedCommandsMemoryRequirementsInfoEXT(d, &items[i]);
        pInfo = items;
    } else {
        pInfo = NULL;
    }
    VkMemoryRequirements2 *pMemoryRequirements = mvk_get_out_shape(d, sizeof(VkMemoryRequirements2));
    if (d->error) {
        mvk_malformed(c, "vkGetGeneratedCommandsMemoryRequirementsEXT");
        return;
    }
    if (!c->fn->vkGetGeneratedCommandsMemoryRequirementsEXT) {
        mvk_unsupported(c, "vkGetGeneratedCommandsMemoryRequirementsEXT");
        return;
    }
    c->fn->vkGetGeneratedCommandsMemoryRequirementsEXT(device, pInfo, pMemoryRequirements);
    MvkEncoder *e = mvk_reply_begin(c);
    reply_VkMemoryRequirements2(e, pMemoryRequirements);
    mvk_reply_end(c);
}

static void run_vkCmdPreprocessGeneratedCommandsEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkGeneratedCommandsInfoEXT * pGeneratedCommandsInfo;
    VkCommandBuffer stateCommandBuffer;
    if (mvk_get_u32(d)) {
        VkGeneratedCommandsInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkGeneratedCommandsInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkGeneratedCommandsInfoEXT(d, &items[i]);
        pGeneratedCommandsInfo = items;
    } else {
        pGeneratedCommandsInfo = NULL;
    }
    stateCommandBuffer = (VkCommandBuffer)mvk_get_handle(d, 1);
    if (d->error) {
        mvk_malformed(c, "vkCmdPreprocessGeneratedCommandsEXT");
        return;
    }
    if (!c->fn->vkCmdPreprocessGeneratedCommandsEXT) {
        mvk_unsupported(c, "vkCmdPreprocessGeneratedCommandsEXT");
        return;
    }
    c->fn->vkCmdPreprocessGeneratedCommandsEXT(commandBuffer, pGeneratedCommandsInfo, stateCommandBuffer);
}

static void run_vkCmdExecuteGeneratedCommandsEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBool32 isPreprocessed;
    VkGeneratedCommandsInfoEXT * pGeneratedCommandsInfo;
    isPreprocessed = (VkBool32)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkGeneratedCommandsInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkGeneratedCommandsInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkGeneratedCommandsInfoEXT(d, &items[i]);
        pGeneratedCommandsInfo = items;
    } else {
        pGeneratedCommandsInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdExecuteGeneratedCommandsEXT");
        return;
    }
    if (!c->fn->vkCmdExecuteGeneratedCommandsEXT) {
        mvk_unsupported(c, "vkCmdExecuteGeneratedCommandsEXT");
        return;
    }
    c->fn->vkCmdExecuteGeneratedCommandsEXT(commandBuffer, isPreprocessed, pGeneratedCommandsInfo);
}

static void run_vkCreateIndirectCommandsLayoutEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkIndirectCommandsLayoutCreateInfoEXT * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkIndirectCommandsLayoutCreateInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkIndirectCommandsLayoutCreateInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkIndirectCommandsLayoutCreateInfoEXT(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pIndirectCommandsLayout = mvk_get_u64(d);
    VkIndirectCommandsLayoutEXT pIndirectCommandsLayout_value = VK_NULL_HANDLE;
    VkIndirectCommandsLayoutEXT *pIndirectCommandsLayout = &pIndirectCommandsLayout_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateIndirectCommandsLayoutEXT");
        return;
    }
    if (!c->fn->vkCreateIndirectCommandsLayoutEXT) {
        mvk_unsupported(c, "vkCreateIndirectCommandsLayoutEXT");
        return;
    }
    VkResult result = c->fn->vkCreateIndirectCommandsLayoutEXT(device, pCreateInfo, NULL, pIndirectCommandsLayout);
    mvk_bind_id(c, id_pIndirectCommandsLayout, (uint64_t)pIndirectCommandsLayout_value);
    mvk_track(c->conn, (uint64_t)pIndirectCommandsLayout_value, MVK_KIND_VkIndirectCommandsLayoutEXT);
    mvk_check(c, result, "vkCreateIndirectCommandsLayoutEXT");
}

static void run_vkDestroyIndirectCommandsLayoutEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkIndirectCommandsLayoutEXT indirectCommandsLayout;
    device = (VkDevice)mvk_get_handle(d, 1);
    indirectCommandsLayout = (VkIndirectCommandsLayoutEXT)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyIndirectCommandsLayoutEXT");
        return;
    }
    if (!c->fn->vkDestroyIndirectCommandsLayoutEXT) {
        mvk_unsupported(c, "vkDestroyIndirectCommandsLayoutEXT");
        return;
    }
    c->fn->vkDestroyIndirectCommandsLayoutEXT(device, indirectCommandsLayout, NULL);
    mvk_forget(c->conn, (uint64_t)indirectCommandsLayout);
}

static void run_vkCreateIndirectExecutionSetEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkIndirectExecutionSetCreateInfoEXT * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkIndirectExecutionSetCreateInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkIndirectExecutionSetCreateInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkIndirectExecutionSetCreateInfoEXT(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pIndirectExecutionSet = mvk_get_u64(d);
    VkIndirectExecutionSetEXT pIndirectExecutionSet_value = VK_NULL_HANDLE;
    VkIndirectExecutionSetEXT *pIndirectExecutionSet = &pIndirectExecutionSet_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateIndirectExecutionSetEXT");
        return;
    }
    if (!c->fn->vkCreateIndirectExecutionSetEXT) {
        mvk_unsupported(c, "vkCreateIndirectExecutionSetEXT");
        return;
    }
    VkResult result = c->fn->vkCreateIndirectExecutionSetEXT(device, pCreateInfo, NULL, pIndirectExecutionSet);
    mvk_bind_id(c, id_pIndirectExecutionSet, (uint64_t)pIndirectExecutionSet_value);
    mvk_track(c->conn, (uint64_t)pIndirectExecutionSet_value, MVK_KIND_VkIndirectExecutionSetEXT);
    mvk_check(c, result, "vkCreateIndirectExecutionSetEXT");
}

static void run_vkDestroyIndirectExecutionSetEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkIndirectExecutionSetEXT indirectExecutionSet;
    device = (VkDevice)mvk_get_handle(d, 1);
    indirectExecutionSet = (VkIndirectExecutionSetEXT)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyIndirectExecutionSetEXT");
        return;
    }
    if (!c->fn->vkDestroyIndirectExecutionSetEXT) {
        mvk_unsupported(c, "vkDestroyIndirectExecutionSetEXT");
        return;
    }
    c->fn->vkDestroyIndirectExecutionSetEXT(device, indirectExecutionSet, NULL);
    mvk_forget(c->conn, (uint64_t)indirectExecutionSet);
}

static void run_vkUpdateIndirectExecutionSetPipelineEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkIndirectExecutionSetEXT indirectExecutionSet;
    uint32_t executionSetWriteCount;
    VkWriteIndirectExecutionSetPipelineEXT * pExecutionSetWrites;
    device = (VkDevice)mvk_get_handle(d, 1);
    indirectExecutionSet = (VkIndirectExecutionSetEXT)mvk_get_handle(d, 0);
    executionSetWriteCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkWriteIndirectExecutionSetPipelineEXT *items = mvk_alloc(d, (size_t)(executionSetWriteCount) * sizeof(VkWriteIndirectExecutionSetPipelineEXT));
        for (uint32_t i = 0; i < (uint32_t)(executionSetWriteCount); i++)
            mvk_get_VkWriteIndirectExecutionSetPipelineEXT(d, &items[i]);
        pExecutionSetWrites = items;
    } else {
        pExecutionSetWrites = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkUpdateIndirectExecutionSetPipelineEXT");
        return;
    }
    if (!c->fn->vkUpdateIndirectExecutionSetPipelineEXT) {
        mvk_unsupported(c, "vkUpdateIndirectExecutionSetPipelineEXT");
        return;
    }
    c->fn->vkUpdateIndirectExecutionSetPipelineEXT(device, indirectExecutionSet, executionSetWriteCount, pExecutionSetWrites);
}

static void run_vkUpdateIndirectExecutionSetShaderEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkIndirectExecutionSetEXT indirectExecutionSet;
    uint32_t executionSetWriteCount;
    VkWriteIndirectExecutionSetShaderEXT * pExecutionSetWrites;
    device = (VkDevice)mvk_get_handle(d, 1);
    indirectExecutionSet = (VkIndirectExecutionSetEXT)mvk_get_handle(d, 0);
    executionSetWriteCount = (uint32_t)mvk_get_u32(d);
    if (mvk_get_u32(d)) {
        VkWriteIndirectExecutionSetShaderEXT *items = mvk_alloc(d, (size_t)(executionSetWriteCount) * sizeof(VkWriteIndirectExecutionSetShaderEXT));
        for (uint32_t i = 0; i < (uint32_t)(executionSetWriteCount); i++)
            mvk_get_VkWriteIndirectExecutionSetShaderEXT(d, &items[i]);
        pExecutionSetWrites = items;
    } else {
        pExecutionSetWrites = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkUpdateIndirectExecutionSetShaderEXT");
        return;
    }
    if (!c->fn->vkUpdateIndirectExecutionSetShaderEXT) {
        mvk_unsupported(c, "vkUpdateIndirectExecutionSetShaderEXT");
        return;
    }
    c->fn->vkUpdateIndirectExecutionSetShaderEXT(device, indirectExecutionSet, executionSetWriteCount, pExecutionSetWrites);
}

static void run_vkGetDeviceFaultReportsKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    uint64_t timeout;
    device = (VkDevice)mvk_get_handle(d, 1);
    timeout = (uint64_t)mvk_get_u64(d);
    uint32_t pFaultCounts_value;
    memset(&pFaultCounts_value, 0, sizeof(pFaultCounts_value));
    uint32_t *pFaultCounts = &pFaultCounts_value;
    int want_pFaultInfo = mvk_get_u32(d);
    VkDeviceFaultInfoKHR *pFaultInfo = NULL;
    uint32_t shape_count_pFaultInfo = 0;
    if (want_pFaultInfo) {
        pFaultInfo = mvk_alloc(d, sizeof(VkDeviceFaultInfoKHR) * (size_t)(*pFaultCounts) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pFaultCounts); i++)
            mvk_get_out_shape_into(d, &pFaultInfo[i], sizeof(VkDeviceFaultInfoKHR));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetDeviceFaultReportsKHR");
        return;
    }
    if (!c->fn->vkGetDeviceFaultReportsKHR) {
        mvk_unsupported(c, "vkGetDeviceFaultReportsKHR");
        return;
    }
    VkResult result = c->fn->vkGetDeviceFaultReportsKHR(device, timeout, pFaultCounts, pFaultInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pFaultCounts));
    mvk_put_u32(e, pFaultInfo != NULL);
    if (pFaultInfo) {
        mvk_put_u32(e, (uint32_t)(*pFaultCounts));
        for (uint32_t i = 0; i < (uint32_t)(*pFaultCounts); i++)
            reply_VkDeviceFaultInfoKHR(e, &pFaultInfo[i]);
    }
    mvk_reply_end(c);
}

static void run_vkGetDeviceFaultDebugInfoKHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    device = (VkDevice)mvk_get_handle(d, 1);
    VkDeviceFaultDebugInfoKHR *pDebugInfo = mvk_get_out_shape(d, sizeof(VkDeviceFaultDebugInfoKHR));
    if (d->error) {
        mvk_malformed(c, "vkGetDeviceFaultDebugInfoKHR");
        return;
    }
    if (!c->fn->vkGetDeviceFaultDebugInfoKHR) {
        mvk_unsupported(c, "vkGetDeviceFaultDebugInfoKHR");
        return;
    }
    VkResult result = c->fn->vkGetDeviceFaultDebugInfoKHR(device, pDebugInfo);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    reply_VkDeviceFaultDebugInfoKHR(e, pDebugInfo);
    mvk_reply_end(c);
}

static void run_vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    uint32_t pPropertyCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pPropertyCount = &pPropertyCount_value;
    int want_pProperties = mvk_get_u32(d);
    VkCooperativeMatrixFlexibleDimensionsPropertiesNV *pProperties = NULL;
    uint32_t shape_count_pProperties = 0;
    if (want_pProperties) {
        pProperties = mvk_alloc(d, sizeof(VkCooperativeMatrixFlexibleDimensionsPropertiesNV) * (size_t)(*pPropertyCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            mvk_get_out_shape_into(d, &pProperties[i], sizeof(VkCooperativeMatrixFlexibleDimensionsPropertiesNV));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV) {
        mvk_unsupported(c, "vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV(physicalDevice, pPropertyCount, pProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pPropertyCount));
    mvk_put_u32(e, pProperties != NULL);
    if (pProperties) {
        mvk_put_u32(e, (uint32_t)(*pPropertyCount));
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            reply_VkCooperativeMatrixFlexibleDimensionsPropertiesNV(e, &pProperties[i]);
    }
    mvk_reply_end(c);
}

static void run_vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    uint32_t queueFamilyIndex;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    queueFamilyIndex = (uint32_t)mvk_get_u32(d);
    uint32_t pCounterCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pCounterCount = &pCounterCount_value;
    int want_pCounters = mvk_get_u32(d);
    VkPerformanceCounterARM *pCounters = NULL;
    uint32_t shape_count_pCounters = 0;
    int want_pCounterDescriptions = mvk_get_u32(d);
    VkPerformanceCounterDescriptionARM *pCounterDescriptions = NULL;
    uint32_t shape_count_pCounterDescriptions = 0;
    if (want_pCounters) {
        pCounters = mvk_alloc(d, sizeof(VkPerformanceCounterARM) * (size_t)(*pCounterCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pCounterCount); i++)
            mvk_get_out_shape_into(d, &pCounters[i], sizeof(VkPerformanceCounterARM));
    }
    if (want_pCounterDescriptions) {
        pCounterDescriptions = mvk_alloc(d, sizeof(VkPerformanceCounterDescriptionARM) * (size_t)(*pCounterCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pCounterCount); i++)
            mvk_get_out_shape_into(d, &pCounterDescriptions[i], sizeof(VkPerformanceCounterDescriptionARM));
    }
    if (d->error) {
        mvk_malformed(c, "vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM");
        return;
    }
    if (!c->fn->vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM) {
        mvk_unsupported(c, "vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM");
        return;
    }
    VkResult result = c->fn->vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM(physicalDevice, queueFamilyIndex, pCounterCount, pCounters, pCounterDescriptions);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pCounterCount));
    mvk_put_u32(e, pCounters != NULL);
    if (pCounters) {
        mvk_put_u32(e, (uint32_t)(*pCounterCount));
        for (uint32_t i = 0; i < (uint32_t)(*pCounterCount); i++)
            reply_VkPerformanceCounterARM(e, &pCounters[i]);
    }
    mvk_put_u32(e, pCounterDescriptions != NULL);
    if (pCounterDescriptions) {
        mvk_put_u32(e, (uint32_t)(*pCounterCount));
        for (uint32_t i = 0; i < (uint32_t)(*pCounterCount); i++)
            reply_VkPerformanceCounterDescriptionARM(e, &pCounterDescriptions[i]);
    }
    mvk_reply_end(c);
}

static void run_vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    uint32_t pDescriptionCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pDescriptionCount = &pDescriptionCount_value;
    int want_pDescriptions = mvk_get_u32(d);
    VkShaderInstrumentationMetricDescriptionARM *pDescriptions = NULL;
    uint32_t shape_count_pDescriptions = 0;
    if (want_pDescriptions) {
        pDescriptions = mvk_alloc(d, sizeof(VkShaderInstrumentationMetricDescriptionARM) * (size_t)(*pDescriptionCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pDescriptionCount); i++)
            mvk_get_out_shape_into(d, &pDescriptions[i], sizeof(VkShaderInstrumentationMetricDescriptionARM));
    }
    if (d->error) {
        mvk_malformed(c, "vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM");
        return;
    }
    if (!c->fn->vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM) {
        mvk_unsupported(c, "vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM");
        return;
    }
    VkResult result = c->fn->vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM(physicalDevice, pDescriptionCount, pDescriptions);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pDescriptionCount));
    mvk_put_u32(e, pDescriptions != NULL);
    if (pDescriptions) {
        mvk_put_u32(e, (uint32_t)(*pDescriptionCount));
        for (uint32_t i = 0; i < (uint32_t)(*pDescriptionCount); i++)
            reply_VkShaderInstrumentationMetricDescriptionARM(e, &pDescriptions[i]);
    }
    mvk_reply_end(c);
}

static void run_vkCreateShaderInstrumentationARM(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkShaderInstrumentationCreateInfoARM * pCreateInfo;
    device = (VkDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkShaderInstrumentationCreateInfoARM *items = mvk_alloc(d, (size_t)(1) * sizeof(VkShaderInstrumentationCreateInfoARM));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkShaderInstrumentationCreateInfoARM(d, &items[i]);
        pCreateInfo = items;
    } else {
        pCreateInfo = NULL;
    }
    uint64_t id_pInstrumentation = mvk_get_u64(d);
    VkShaderInstrumentationARM pInstrumentation_value = VK_NULL_HANDLE;
    VkShaderInstrumentationARM *pInstrumentation = &pInstrumentation_value;
    if (d->error) {
        mvk_malformed(c, "vkCreateShaderInstrumentationARM");
        return;
    }
    if (!c->fn->vkCreateShaderInstrumentationARM) {
        mvk_unsupported(c, "vkCreateShaderInstrumentationARM");
        return;
    }
    VkResult result = c->fn->vkCreateShaderInstrumentationARM(device, pCreateInfo, NULL, pInstrumentation);
    mvk_bind_id(c, id_pInstrumentation, (uint64_t)pInstrumentation_value);
    mvk_track(c->conn, (uint64_t)pInstrumentation_value, MVK_KIND_VkShaderInstrumentationARM);
    mvk_check(c, result, "vkCreateShaderInstrumentationARM");
}

static void run_vkDestroyShaderInstrumentationARM(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkShaderInstrumentationARM instrumentation;
    device = (VkDevice)mvk_get_handle(d, 1);
    instrumentation = (VkShaderInstrumentationARM)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkDestroyShaderInstrumentationARM");
        return;
    }
    if (!c->fn->vkDestroyShaderInstrumentationARM) {
        mvk_unsupported(c, "vkDestroyShaderInstrumentationARM");
        return;
    }
    c->fn->vkDestroyShaderInstrumentationARM(device, instrumentation, NULL);
    mvk_forget(c->conn, (uint64_t)instrumentation);
}

static void run_vkCmdBeginShaderInstrumentationARM(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkShaderInstrumentationARM instrumentation;
    instrumentation = (VkShaderInstrumentationARM)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkCmdBeginShaderInstrumentationARM");
        return;
    }
    if (!c->fn->vkCmdBeginShaderInstrumentationARM) {
        mvk_unsupported(c, "vkCmdBeginShaderInstrumentationARM");
        return;
    }
    c->fn->vkCmdBeginShaderInstrumentationARM(commandBuffer, instrumentation);
}

static void run_vkCmdEndShaderInstrumentationARM(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    if (d->error) {
        mvk_malformed(c, "vkCmdEndShaderInstrumentationARM");
        return;
    }
    if (!c->fn->vkCmdEndShaderInstrumentationARM) {
        mvk_unsupported(c, "vkCmdEndShaderInstrumentationARM");
        return;
    }
    c->fn->vkCmdEndShaderInstrumentationARM(commandBuffer);
}

static void run_vkClearShaderInstrumentationMetricsARM(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkDevice device;
    VkShaderInstrumentationARM instrumentation;
    device = (VkDevice)mvk_get_handle(d, 1);
    instrumentation = (VkShaderInstrumentationARM)mvk_get_handle(d, 0);
    if (d->error) {
        mvk_malformed(c, "vkClearShaderInstrumentationMetricsARM");
        return;
    }
    if (!c->fn->vkClearShaderInstrumentationMetricsARM) {
        mvk_unsupported(c, "vkClearShaderInstrumentationMetricsARM");
        return;
    }
    c->fn->vkClearShaderInstrumentationMetricsARM(device, instrumentation);
}

static void run_vkCmdBeginCustomResolveEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkBeginCustomResolveInfoEXT * pBeginCustomResolveInfo;
    if (mvk_get_u32(d)) {
        VkBeginCustomResolveInfoEXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkBeginCustomResolveInfoEXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkBeginCustomResolveInfoEXT(d, &items[i]);
        pBeginCustomResolveInfo = items;
    } else {
        pBeginCustomResolveInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdBeginCustomResolveEXT");
        return;
    }
    if (!c->fn->vkCmdBeginCustomResolveEXT) {
        mvk_unsupported(c, "vkCmdBeginCustomResolveEXT");
        return;
    }
    c->fn->vkCmdBeginCustomResolveEXT(commandBuffer, pBeginCustomResolveInfo);
}

static void run_vkCmdEndRendering2KHR(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkRenderingEndInfoKHR * pRenderingEndInfo;
    if (mvk_get_u32(d)) {
        VkRenderingEndInfoKHR *items = mvk_alloc(d, (size_t)(1) * sizeof(VkRenderingEndInfoKHR));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkRenderingEndInfoKHR(d, &items[i]);
        pRenderingEndInfo = items;
    } else {
        pRenderingEndInfo = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdEndRendering2KHR");
        return;
    }
    if (!c->fn->vkCmdEndRendering2KHR) {
        mvk_unsupported(c, "vkCmdEndRendering2KHR");
        return;
    }
    c->fn->vkCmdEndRendering2KHR(commandBuffer, pRenderingEndInfo);
}

static void run_vkCmdSetComputeOccupancyPriorityNV(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    VkComputeOccupancyPriorityParametersNV * pParameters;
    if (mvk_get_u32(d)) {
        VkComputeOccupancyPriorityParametersNV *items = mvk_alloc(d, (size_t)(1) * sizeof(VkComputeOccupancyPriorityParametersNV));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkComputeOccupancyPriorityParametersNV(d, &items[i]);
        pParameters = items;
    } else {
        pParameters = NULL;
    }
    if (d->error) {
        mvk_malformed(c, "vkCmdSetComputeOccupancyPriorityNV");
        return;
    }
    if (!c->fn->vkCmdSetComputeOccupancyPriorityNV) {
        mvk_unsupported(c, "vkCmdSetComputeOccupancyPriorityNV");
        return;
    }
    c->fn->vkCmdSetComputeOccupancyPriorityNV(commandBuffer, pParameters);
}

static void run_vkGetPhysicalDeviceCooperativeMatrixProperties2EXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceCooperativeMatrixInfo2EXT * pCooperativeMatrixInfo;
    physicalDevice = (VkPhysicalDevice)mvk_get_handle(d, 1);
    if (mvk_get_u32(d)) {
        VkPhysicalDeviceCooperativeMatrixInfo2EXT *items = mvk_alloc(d, (size_t)(1) * sizeof(VkPhysicalDeviceCooperativeMatrixInfo2EXT));
        for (uint32_t i = 0; i < (uint32_t)(1); i++)
            mvk_get_VkPhysicalDeviceCooperativeMatrixInfo2EXT(d, &items[i]);
        pCooperativeMatrixInfo = items;
    } else {
        pCooperativeMatrixInfo = NULL;
    }
    uint32_t pPropertyCount_value = (uint32_t)mvk_get_u32(d);
    uint32_t *pPropertyCount = &pPropertyCount_value;
    int want_pProperties = mvk_get_u32(d);
    VkCooperativeMatrixProperties2EXT *pProperties = NULL;
    uint32_t shape_count_pProperties = 0;
    if (want_pProperties) {
        pProperties = mvk_alloc(d, sizeof(VkCooperativeMatrixProperties2EXT) * (size_t)(*pPropertyCount) + 1);
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            mvk_get_out_shape_into(d, &pProperties[i], sizeof(VkCooperativeMatrixProperties2EXT));
    }
    if (d->error) {
        mvk_malformed(c, "vkGetPhysicalDeviceCooperativeMatrixProperties2EXT");
        return;
    }
    if (!c->fn->vkGetPhysicalDeviceCooperativeMatrixProperties2EXT) {
        mvk_unsupported(c, "vkGetPhysicalDeviceCooperativeMatrixProperties2EXT");
        return;
    }
    VkResult result = c->fn->vkGetPhysicalDeviceCooperativeMatrixProperties2EXT(physicalDevice, pCooperativeMatrixInfo, pPropertyCount, pProperties);
    MvkEncoder *e = mvk_reply_begin(c);
    mvk_put_u32(e, (uint32_t)result);
    mvk_put_u32(e, (uint32_t)(*pPropertyCount));
    mvk_put_u32(e, pProperties != NULL);
    if (pProperties) {
        mvk_put_u32(e, (uint32_t)(*pPropertyCount));
        for (uint32_t i = 0; i < (uint32_t)(*pPropertyCount); i++)
            reply_VkCooperativeMatrixProperties2EXT(e, &pProperties[i]);
    }
    mvk_reply_end(c);
}

static void run_vkCmdSetPrimitiveRestartIndexEXT(MvkCall *c)
{
    MvkDecoder *d = &c->in;
    VkCommandBuffer commandBuffer = c->command_buffer;
    uint32_t primitiveRestartIndex;
    primitiveRestartIndex = (uint32_t)mvk_get_u32(d);
    if (d->error) {
        mvk_malformed(c, "vkCmdSetPrimitiveRestartIndexEXT");
        return;
    }
    if (!c->fn->vkCmdSetPrimitiveRestartIndexEXT) {
        mvk_unsupported(c, "vkCmdSetPrimitiveRestartIndexEXT");
        return;
    }
    c->fn->vkCmdSetPrimitiveRestartIndexEXT(commandBuffer, primitiveRestartIndex);
}

const MvkRun mvk_generated_commands[MVK_OP_GENERATED_COUNT] = {
    [MVK_OP_vkGetPhysicalDeviceFeatures] = run_vkGetPhysicalDeviceFeatures,
    [MVK_OP_vkGetPhysicalDeviceFormatProperties] = run_vkGetPhysicalDeviceFormatProperties,
    [MVK_OP_vkGetPhysicalDeviceImageFormatProperties] = run_vkGetPhysicalDeviceImageFormatProperties,
    [MVK_OP_vkGetPhysicalDeviceProperties] = run_vkGetPhysicalDeviceProperties,
    [MVK_OP_vkGetPhysicalDeviceQueueFamilyProperties] = run_vkGetPhysicalDeviceQueueFamilyProperties,
    [MVK_OP_vkGetPhysicalDeviceMemoryProperties] = run_vkGetPhysicalDeviceMemoryProperties,
    [MVK_OP_vkBindBufferMemory] = run_vkBindBufferMemory,
    [MVK_OP_vkBindImageMemory] = run_vkBindImageMemory,
    [MVK_OP_vkGetBufferMemoryRequirements] = run_vkGetBufferMemoryRequirements,
    [MVK_OP_vkGetImageMemoryRequirements] = run_vkGetImageMemoryRequirements,
    [MVK_OP_vkGetImageSparseMemoryRequirements] = run_vkGetImageSparseMemoryRequirements,
    [MVK_OP_vkGetPhysicalDeviceSparseImageFormatProperties] = run_vkGetPhysicalDeviceSparseImageFormatProperties,
    [MVK_OP_vkCreateQueryPool] = run_vkCreateQueryPool,
    [MVK_OP_vkDestroyQueryPool] = run_vkDestroyQueryPool,
    [MVK_OP_vkCreateBuffer] = run_vkCreateBuffer,
    [MVK_OP_vkDestroyBuffer] = run_vkDestroyBuffer,
    [MVK_OP_vkCreateImage] = run_vkCreateImage,
    [MVK_OP_vkDestroyImage] = run_vkDestroyImage,
    [MVK_OP_vkGetImageSubresourceLayout] = run_vkGetImageSubresourceLayout,
    [MVK_OP_vkCreateImageView] = run_vkCreateImageView,
    [MVK_OP_vkDestroyImageView] = run_vkDestroyImageView,
    [MVK_OP_vkCreateCommandPool] = run_vkCreateCommandPool,
    [MVK_OP_vkCmdCopyBuffer] = run_vkCmdCopyBuffer,
    [MVK_OP_vkCmdCopyImage] = run_vkCmdCopyImage,
    [MVK_OP_vkCmdCopyBufferToImage] = run_vkCmdCopyBufferToImage,
    [MVK_OP_vkCmdCopyImageToBuffer] = run_vkCmdCopyImageToBuffer,
    [MVK_OP_vkCmdUpdateBuffer] = run_vkCmdUpdateBuffer,
    [MVK_OP_vkCmdFillBuffer] = run_vkCmdFillBuffer,
    [MVK_OP_vkCmdPipelineBarrier] = run_vkCmdPipelineBarrier,
    [MVK_OP_vkCmdBeginQuery] = run_vkCmdBeginQuery,
    [MVK_OP_vkCmdEndQuery] = run_vkCmdEndQuery,
    [MVK_OP_vkCmdResetQueryPool] = run_vkCmdResetQueryPool,
    [MVK_OP_vkCmdWriteTimestamp] = run_vkCmdWriteTimestamp,
    [MVK_OP_vkCmdCopyQueryPoolResults] = run_vkCmdCopyQueryPoolResults,
    [MVK_OP_vkCmdExecuteCommands] = run_vkCmdExecuteCommands,
    [MVK_OP_vkCreateBufferView] = run_vkCreateBufferView,
    [MVK_OP_vkDestroyBufferView] = run_vkDestroyBufferView,
    [MVK_OP_vkCreateShaderModule] = run_vkCreateShaderModule,
    [MVK_OP_vkDestroyShaderModule] = run_vkDestroyShaderModule,
    [MVK_OP_vkCreatePipelineCache] = run_vkCreatePipelineCache,
    [MVK_OP_vkDestroyPipelineCache] = run_vkDestroyPipelineCache,
    [MVK_OP_vkGetPipelineCacheData] = run_vkGetPipelineCacheData,
    [MVK_OP_vkMergePipelineCaches] = run_vkMergePipelineCaches,
    [MVK_OP_vkCreateComputePipelines] = run_vkCreateComputePipelines,
    [MVK_OP_vkDestroyPipeline] = run_vkDestroyPipeline,
    [MVK_OP_vkCreatePipelineLayout] = run_vkCreatePipelineLayout,
    [MVK_OP_vkDestroyPipelineLayout] = run_vkDestroyPipelineLayout,
    [MVK_OP_vkCreateSampler] = run_vkCreateSampler,
    [MVK_OP_vkDestroySampler] = run_vkDestroySampler,
    [MVK_OP_vkCreateDescriptorSetLayout] = run_vkCreateDescriptorSetLayout,
    [MVK_OP_vkDestroyDescriptorSetLayout] = run_vkDestroyDescriptorSetLayout,
    [MVK_OP_vkUpdateDescriptorSets] = run_vkUpdateDescriptorSets,
    [MVK_OP_vkCmdBindPipeline] = run_vkCmdBindPipeline,
    [MVK_OP_vkCmdBindDescriptorSets] = run_vkCmdBindDescriptorSets,
    [MVK_OP_vkCmdClearColorImage] = run_vkCmdClearColorImage,
    [MVK_OP_vkCmdDispatch] = run_vkCmdDispatch,
    [MVK_OP_vkCmdDispatchIndirect] = run_vkCmdDispatchIndirect,
    [MVK_OP_vkCmdSetEvent] = run_vkCmdSetEvent,
    [MVK_OP_vkCmdResetEvent] = run_vkCmdResetEvent,
    [MVK_OP_vkCmdWaitEvents] = run_vkCmdWaitEvents,
    [MVK_OP_vkCmdPushConstants] = run_vkCmdPushConstants,
    [MVK_OP_vkCreateGraphicsPipelines] = run_vkCreateGraphicsPipelines,
    [MVK_OP_vkCreateFramebuffer] = run_vkCreateFramebuffer,
    [MVK_OP_vkDestroyFramebuffer] = run_vkDestroyFramebuffer,
    [MVK_OP_vkCreateRenderPass] = run_vkCreateRenderPass,
    [MVK_OP_vkDestroyRenderPass] = run_vkDestroyRenderPass,
    [MVK_OP_vkGetRenderAreaGranularity] = run_vkGetRenderAreaGranularity,
    [MVK_OP_vkCmdSetViewport] = run_vkCmdSetViewport,
    [MVK_OP_vkCmdSetScissor] = run_vkCmdSetScissor,
    [MVK_OP_vkCmdSetLineWidth] = run_vkCmdSetLineWidth,
    [MVK_OP_vkCmdSetDepthBias] = run_vkCmdSetDepthBias,
    [MVK_OP_vkCmdSetBlendConstants] = run_vkCmdSetBlendConstants,
    [MVK_OP_vkCmdSetDepthBounds] = run_vkCmdSetDepthBounds,
    [MVK_OP_vkCmdSetStencilCompareMask] = run_vkCmdSetStencilCompareMask,
    [MVK_OP_vkCmdSetStencilWriteMask] = run_vkCmdSetStencilWriteMask,
    [MVK_OP_vkCmdSetStencilReference] = run_vkCmdSetStencilReference,
    [MVK_OP_vkCmdBindIndexBuffer] = run_vkCmdBindIndexBuffer,
    [MVK_OP_vkCmdBindVertexBuffers] = run_vkCmdBindVertexBuffers,
    [MVK_OP_vkCmdDraw] = run_vkCmdDraw,
    [MVK_OP_vkCmdDrawIndexed] = run_vkCmdDrawIndexed,
    [MVK_OP_vkCmdDrawIndirect] = run_vkCmdDrawIndirect,
    [MVK_OP_vkCmdDrawIndexedIndirect] = run_vkCmdDrawIndexedIndirect,
    [MVK_OP_vkCmdBlitImage] = run_vkCmdBlitImage,
    [MVK_OP_vkCmdClearDepthStencilImage] = run_vkCmdClearDepthStencilImage,
    [MVK_OP_vkCmdClearAttachments] = run_vkCmdClearAttachments,
    [MVK_OP_vkCmdResolveImage] = run_vkCmdResolveImage,
    [MVK_OP_vkCmdBeginRenderPass] = run_vkCmdBeginRenderPass,
    [MVK_OP_vkCmdNextSubpass] = run_vkCmdNextSubpass,
    [MVK_OP_vkCmdEndRenderPass] = run_vkCmdEndRenderPass,
    [MVK_OP_vkBindBufferMemory2] = run_vkBindBufferMemory2,
    [MVK_OP_vkBindImageMemory2] = run_vkBindImageMemory2,
    [MVK_OP_vkGetDeviceGroupPeerMemoryFeatures] = run_vkGetDeviceGroupPeerMemoryFeatures,
    [MVK_OP_vkCmdSetDeviceMask] = run_vkCmdSetDeviceMask,
    [MVK_OP_vkGetImageMemoryRequirements2] = run_vkGetImageMemoryRequirements2,
    [MVK_OP_vkGetBufferMemoryRequirements2] = run_vkGetBufferMemoryRequirements2,
    [MVK_OP_vkGetImageSparseMemoryRequirements2] = run_vkGetImageSparseMemoryRequirements2,
    [MVK_OP_vkGetPhysicalDeviceFeatures2] = run_vkGetPhysicalDeviceFeatures2,
    [MVK_OP_vkGetPhysicalDeviceProperties2] = run_vkGetPhysicalDeviceProperties2,
    [MVK_OP_vkGetPhysicalDeviceFormatProperties2] = run_vkGetPhysicalDeviceFormatProperties2,
    [MVK_OP_vkGetPhysicalDeviceImageFormatProperties2] = run_vkGetPhysicalDeviceImageFormatProperties2,
    [MVK_OP_vkGetPhysicalDeviceQueueFamilyProperties2] = run_vkGetPhysicalDeviceQueueFamilyProperties2,
    [MVK_OP_vkGetPhysicalDeviceMemoryProperties2] = run_vkGetPhysicalDeviceMemoryProperties2,
    [MVK_OP_vkGetPhysicalDeviceSparseImageFormatProperties2] = run_vkGetPhysicalDeviceSparseImageFormatProperties2,
    [MVK_OP_vkTrimCommandPool] = run_vkTrimCommandPool,
    [MVK_OP_vkGetPhysicalDeviceExternalBufferProperties] = run_vkGetPhysicalDeviceExternalBufferProperties,
    [MVK_OP_vkGetPhysicalDeviceExternalFenceProperties] = run_vkGetPhysicalDeviceExternalFenceProperties,
    [MVK_OP_vkGetPhysicalDeviceExternalSemaphoreProperties] = run_vkGetPhysicalDeviceExternalSemaphoreProperties,
    [MVK_OP_vkCmdDispatchBase] = run_vkCmdDispatchBase,
    [MVK_OP_vkCreateDescriptorUpdateTemplate] = run_vkCreateDescriptorUpdateTemplate,
    [MVK_OP_vkDestroyDescriptorUpdateTemplate] = run_vkDestroyDescriptorUpdateTemplate,
    [MVK_OP_vkGetDescriptorSetLayoutSupport] = run_vkGetDescriptorSetLayoutSupport,
    [MVK_OP_vkCreateSamplerYcbcrConversion] = run_vkCreateSamplerYcbcrConversion,
    [MVK_OP_vkDestroySamplerYcbcrConversion] = run_vkDestroySamplerYcbcrConversion,
    [MVK_OP_vkResetQueryPool] = run_vkResetQueryPool,
    [MVK_OP_vkGetBufferDeviceAddress] = run_vkGetBufferDeviceAddress,
    [MVK_OP_vkGetBufferOpaqueCaptureAddress] = run_vkGetBufferOpaqueCaptureAddress,
    [MVK_OP_vkGetDeviceMemoryOpaqueCaptureAddress] = run_vkGetDeviceMemoryOpaqueCaptureAddress,
    [MVK_OP_vkCmdDrawIndirectCount] = run_vkCmdDrawIndirectCount,
    [MVK_OP_vkCmdDrawIndexedIndirectCount] = run_vkCmdDrawIndexedIndirectCount,
    [MVK_OP_vkCreateRenderPass2] = run_vkCreateRenderPass2,
    [MVK_OP_vkCmdBeginRenderPass2] = run_vkCmdBeginRenderPass2,
    [MVK_OP_vkCmdNextSubpass2] = run_vkCmdNextSubpass2,
    [MVK_OP_vkCmdEndRenderPass2] = run_vkCmdEndRenderPass2,
    [MVK_OP_vkGetPhysicalDeviceToolProperties] = run_vkGetPhysicalDeviceToolProperties,
    [MVK_OP_vkCreatePrivateDataSlot] = run_vkCreatePrivateDataSlot,
    [MVK_OP_vkDestroyPrivateDataSlot] = run_vkDestroyPrivateDataSlot,
    [MVK_OP_vkSetPrivateData] = run_vkSetPrivateData,
    [MVK_OP_vkGetPrivateData] = run_vkGetPrivateData,
    [MVK_OP_vkCmdPipelineBarrier2] = run_vkCmdPipelineBarrier2,
    [MVK_OP_vkCmdWriteTimestamp2] = run_vkCmdWriteTimestamp2,
    [MVK_OP_vkCmdCopyBuffer2] = run_vkCmdCopyBuffer2,
    [MVK_OP_vkCmdCopyImage2] = run_vkCmdCopyImage2,
    [MVK_OP_vkCmdCopyBufferToImage2] = run_vkCmdCopyBufferToImage2,
    [MVK_OP_vkCmdCopyImageToBuffer2] = run_vkCmdCopyImageToBuffer2,
    [MVK_OP_vkGetDeviceBufferMemoryRequirements] = run_vkGetDeviceBufferMemoryRequirements,
    [MVK_OP_vkGetDeviceImageMemoryRequirements] = run_vkGetDeviceImageMemoryRequirements,
    [MVK_OP_vkGetDeviceImageSparseMemoryRequirements] = run_vkGetDeviceImageSparseMemoryRequirements,
    [MVK_OP_vkCmdSetEvent2] = run_vkCmdSetEvent2,
    [MVK_OP_vkCmdResetEvent2] = run_vkCmdResetEvent2,
    [MVK_OP_vkCmdWaitEvents2] = run_vkCmdWaitEvents2,
    [MVK_OP_vkCmdBlitImage2] = run_vkCmdBlitImage2,
    [MVK_OP_vkCmdResolveImage2] = run_vkCmdResolveImage2,
    [MVK_OP_vkCmdBeginRendering] = run_vkCmdBeginRendering,
    [MVK_OP_vkCmdEndRendering] = run_vkCmdEndRendering,
    [MVK_OP_vkCmdSetCullMode] = run_vkCmdSetCullMode,
    [MVK_OP_vkCmdSetFrontFace] = run_vkCmdSetFrontFace,
    [MVK_OP_vkCmdSetPrimitiveTopology] = run_vkCmdSetPrimitiveTopology,
    [MVK_OP_vkCmdSetViewportWithCount] = run_vkCmdSetViewportWithCount,
    [MVK_OP_vkCmdSetScissorWithCount] = run_vkCmdSetScissorWithCount,
    [MVK_OP_vkCmdBindVertexBuffers2] = run_vkCmdBindVertexBuffers2,
    [MVK_OP_vkCmdSetDepthTestEnable] = run_vkCmdSetDepthTestEnable,
    [MVK_OP_vkCmdSetDepthWriteEnable] = run_vkCmdSetDepthWriteEnable,
    [MVK_OP_vkCmdSetDepthCompareOp] = run_vkCmdSetDepthCompareOp,
    [MVK_OP_vkCmdSetDepthBoundsTestEnable] = run_vkCmdSetDepthBoundsTestEnable,
    [MVK_OP_vkCmdSetStencilTestEnable] = run_vkCmdSetStencilTestEnable,
    [MVK_OP_vkCmdSetStencilOp] = run_vkCmdSetStencilOp,
    [MVK_OP_vkCmdSetRasterizerDiscardEnable] = run_vkCmdSetRasterizerDiscardEnable,
    [MVK_OP_vkCmdSetDepthBiasEnable] = run_vkCmdSetDepthBiasEnable,
    [MVK_OP_vkCmdSetPrimitiveRestartEnable] = run_vkCmdSetPrimitiveRestartEnable,
    [MVK_OP_vkGetDeviceImageSubresourceLayout] = run_vkGetDeviceImageSubresourceLayout,
    [MVK_OP_vkGetImageSubresourceLayout2] = run_vkGetImageSubresourceLayout2,
    [MVK_OP_vkCopyImageToImage] = run_vkCopyImageToImage,
    [MVK_OP_vkTransitionImageLayout] = run_vkTransitionImageLayout,
    [MVK_OP_vkCmdPushDescriptorSet] = run_vkCmdPushDescriptorSet,
    [MVK_OP_vkCmdBindDescriptorSets2] = run_vkCmdBindDescriptorSets2,
    [MVK_OP_vkCmdPushConstants2] = run_vkCmdPushConstants2,
    [MVK_OP_vkCmdPushDescriptorSet2] = run_vkCmdPushDescriptorSet2,
    [MVK_OP_vkCmdSetLineStipple] = run_vkCmdSetLineStipple,
    [MVK_OP_vkCmdBindIndexBuffer2] = run_vkCmdBindIndexBuffer2,
    [MVK_OP_vkGetRenderingAreaGranularity] = run_vkGetRenderingAreaGranularity,
    [MVK_OP_vkCmdSetRenderingAttachmentLocations] = run_vkCmdSetRenderingAttachmentLocations,
    [MVK_OP_vkCmdSetRenderingInputAttachmentIndices] = run_vkCmdSetRenderingInputAttachmentIndices,
    [MVK_OP_vkCmdBindTransformFeedbackBuffersEXT] = run_vkCmdBindTransformFeedbackBuffersEXT,
    [MVK_OP_vkCmdBeginTransformFeedbackEXT] = run_vkCmdBeginTransformFeedbackEXT,
    [MVK_OP_vkCmdEndTransformFeedbackEXT] = run_vkCmdEndTransformFeedbackEXT,
    [MVK_OP_vkCmdBeginQueryIndexedEXT] = run_vkCmdBeginQueryIndexedEXT,
    [MVK_OP_vkCmdEndQueryIndexedEXT] = run_vkCmdEndQueryIndexedEXT,
    [MVK_OP_vkCmdDrawIndirectByteCountEXT] = run_vkCmdDrawIndirectByteCountEXT,
    [MVK_OP_vkGetImageViewHandleNVX] = run_vkGetImageViewHandleNVX,
    [MVK_OP_vkGetImageViewHandle64NVX] = run_vkGetImageViewHandle64NVX,
    [MVK_OP_vkGetImageViewAddressNVX] = run_vkGetImageViewAddressNVX,
    [MVK_OP_vkGetDeviceCombinedImageSamplerIndexNVX] = run_vkGetDeviceCombinedImageSamplerIndexNVX,
    [MVK_OP_vkGetShaderInfoAMD] = run_vkGetShaderInfoAMD,
    [MVK_OP_vkGetPhysicalDeviceExternalImageFormatPropertiesNV] = run_vkGetPhysicalDeviceExternalImageFormatPropertiesNV,
    [MVK_OP_vkCmdBeginConditionalRenderingEXT] = run_vkCmdBeginConditionalRenderingEXT,
    [MVK_OP_vkCmdEndConditionalRenderingEXT] = run_vkCmdEndConditionalRenderingEXT,
    [MVK_OP_vkCmdSetViewportWScalingNV] = run_vkCmdSetViewportWScalingNV,
    [MVK_OP_vkCmdSetDiscardRectangleEXT] = run_vkCmdSetDiscardRectangleEXT,
    [MVK_OP_vkCmdSetDiscardRectangleEnableEXT] = run_vkCmdSetDiscardRectangleEnableEXT,
    [MVK_OP_vkCmdSetDiscardRectangleModeEXT] = run_vkCmdSetDiscardRectangleModeEXT,
    [MVK_OP_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR] = run_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR,
    [MVK_OP_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR] = run_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR,
    [MVK_OP_vkAcquireProfilingLockKHR] = run_vkAcquireProfilingLockKHR,
    [MVK_OP_vkReleaseProfilingLockKHR] = run_vkReleaseProfilingLockKHR,
    [MVK_OP_vkCreateGpaSessionAMD] = run_vkCreateGpaSessionAMD,
    [MVK_OP_vkDestroyGpaSessionAMD] = run_vkDestroyGpaSessionAMD,
    [MVK_OP_vkSetGpaDeviceClockModeAMD] = run_vkSetGpaDeviceClockModeAMD,
    [MVK_OP_vkGetGpaDeviceClockInfoAMD] = run_vkGetGpaDeviceClockInfoAMD,
    [MVK_OP_vkCmdEndGpaSampleAMD] = run_vkCmdEndGpaSampleAMD,
    [MVK_OP_vkGetGpaSessionStatusAMD] = run_vkGetGpaSessionStatusAMD,
    [MVK_OP_vkGetGpaSessionResultsAMD] = run_vkGetGpaSessionResultsAMD,
    [MVK_OP_vkResetGpaSessionAMD] = run_vkResetGpaSessionAMD,
    [MVK_OP_vkCmdCopyGpaSessionResultsAMD] = run_vkCmdCopyGpaSessionResultsAMD,
    [MVK_OP_vkWriteSamplerDescriptorsEXT] = run_vkWriteSamplerDescriptorsEXT,
    [MVK_OP_vkWriteResourceDescriptorsEXT] = run_vkWriteResourceDescriptorsEXT,
    [MVK_OP_vkCmdBindSamplerHeapEXT] = run_vkCmdBindSamplerHeapEXT,
    [MVK_OP_vkCmdBindResourceHeapEXT] = run_vkCmdBindResourceHeapEXT,
    [MVK_OP_vkCmdPushDataEXT] = run_vkCmdPushDataEXT,
    [MVK_OP_vkGetImageOpaqueCaptureDataEXT] = run_vkGetImageOpaqueCaptureDataEXT,
    [MVK_OP_vkGetPhysicalDeviceDescriptorSizeEXT] = run_vkGetPhysicalDeviceDescriptorSizeEXT,
    [MVK_OP_vkRegisterCustomBorderColorEXT] = run_vkRegisterCustomBorderColorEXT,
    [MVK_OP_vkUnregisterCustomBorderColorEXT] = run_vkUnregisterCustomBorderColorEXT,
    [MVK_OP_vkGetTensorOpaqueCaptureDataARM] = run_vkGetTensorOpaqueCaptureDataARM,
    [MVK_OP_vkCmdSetSampleLocationsEXT] = run_vkCmdSetSampleLocationsEXT,
    [MVK_OP_vkGetPhysicalDeviceMultisamplePropertiesEXT] = run_vkGetPhysicalDeviceMultisamplePropertiesEXT,
    [MVK_OP_vkCreateAccelerationStructureKHR] = run_vkCreateAccelerationStructureKHR,
    [MVK_OP_vkDestroyAccelerationStructureKHR] = run_vkDestroyAccelerationStructureKHR,
    [MVK_OP_vkCmdBuildAccelerationStructuresKHR] = run_vkCmdBuildAccelerationStructuresKHR,
    [MVK_OP_vkCmdBuildAccelerationStructuresIndirectKHR] = run_vkCmdBuildAccelerationStructuresIndirectKHR,
    [MVK_OP_vkBuildAccelerationStructuresKHR] = run_vkBuildAccelerationStructuresKHR,
    [MVK_OP_vkCopyAccelerationStructureKHR] = run_vkCopyAccelerationStructureKHR,
    [MVK_OP_vkCopyAccelerationStructureToMemoryKHR] = run_vkCopyAccelerationStructureToMemoryKHR,
    [MVK_OP_vkCopyMemoryToAccelerationStructureKHR] = run_vkCopyMemoryToAccelerationStructureKHR,
    [MVK_OP_vkWriteAccelerationStructuresPropertiesKHR] = run_vkWriteAccelerationStructuresPropertiesKHR,
    [MVK_OP_vkCmdCopyAccelerationStructureKHR] = run_vkCmdCopyAccelerationStructureKHR,
    [MVK_OP_vkCmdCopyAccelerationStructureToMemoryKHR] = run_vkCmdCopyAccelerationStructureToMemoryKHR,
    [MVK_OP_vkCmdCopyMemoryToAccelerationStructureKHR] = run_vkCmdCopyMemoryToAccelerationStructureKHR,
    [MVK_OP_vkGetAccelerationStructureDeviceAddressKHR] = run_vkGetAccelerationStructureDeviceAddressKHR,
    [MVK_OP_vkCmdWriteAccelerationStructuresPropertiesKHR] = run_vkCmdWriteAccelerationStructuresPropertiesKHR,
    [MVK_OP_vkGetDeviceAccelerationStructureCompatibilityKHR] = run_vkGetDeviceAccelerationStructureCompatibilityKHR,
    [MVK_OP_vkGetAccelerationStructureBuildSizesKHR] = run_vkGetAccelerationStructureBuildSizesKHR,
    [MVK_OP_vkCmdTraceRaysKHR] = run_vkCmdTraceRaysKHR,
    [MVK_OP_vkCreateRayTracingPipelinesKHR] = run_vkCreateRayTracingPipelinesKHR,
    [MVK_OP_vkGetRayTracingShaderGroupHandlesKHR] = run_vkGetRayTracingShaderGroupHandlesKHR,
    [MVK_OP_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR] = run_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR,
    [MVK_OP_vkCmdTraceRaysIndirectKHR] = run_vkCmdTraceRaysIndirectKHR,
    [MVK_OP_vkGetRayTracingShaderGroupStackSizeKHR] = run_vkGetRayTracingShaderGroupStackSizeKHR,
    [MVK_OP_vkCmdSetRayTracingPipelineStackSizeKHR] = run_vkCmdSetRayTracingPipelineStackSizeKHR,
    [MVK_OP_vkGetImageDrmFormatModifierPropertiesEXT] = run_vkGetImageDrmFormatModifierPropertiesEXT,
    [MVK_OP_vkCreateValidationCacheEXT] = run_vkCreateValidationCacheEXT,
    [MVK_OP_vkDestroyValidationCacheEXT] = run_vkDestroyValidationCacheEXT,
    [MVK_OP_vkMergeValidationCachesEXT] = run_vkMergeValidationCachesEXT,
    [MVK_OP_vkGetValidationCacheDataEXT] = run_vkGetValidationCacheDataEXT,
    [MVK_OP_vkCmdBindShadingRateImageNV] = run_vkCmdBindShadingRateImageNV,
    [MVK_OP_vkCmdSetViewportShadingRatePaletteNV] = run_vkCmdSetViewportShadingRatePaletteNV,
    [MVK_OP_vkCmdSetCoarseSampleOrderNV] = run_vkCmdSetCoarseSampleOrderNV,
    [MVK_OP_vkCreateAccelerationStructureNV] = run_vkCreateAccelerationStructureNV,
    [MVK_OP_vkDestroyAccelerationStructureNV] = run_vkDestroyAccelerationStructureNV,
    [MVK_OP_vkGetAccelerationStructureMemoryRequirementsNV] = run_vkGetAccelerationStructureMemoryRequirementsNV,
    [MVK_OP_vkBindAccelerationStructureMemoryNV] = run_vkBindAccelerationStructureMemoryNV,
    [MVK_OP_vkCmdBuildAccelerationStructureNV] = run_vkCmdBuildAccelerationStructureNV,
    [MVK_OP_vkCmdCopyAccelerationStructureNV] = run_vkCmdCopyAccelerationStructureNV,
    [MVK_OP_vkCmdTraceRaysNV] = run_vkCmdTraceRaysNV,
    [MVK_OP_vkCreateRayTracingPipelinesNV] = run_vkCreateRayTracingPipelinesNV,
    [MVK_OP_vkGetAccelerationStructureHandleNV] = run_vkGetAccelerationStructureHandleNV,
    [MVK_OP_vkCmdWriteAccelerationStructuresPropertiesNV] = run_vkCmdWriteAccelerationStructuresPropertiesNV,
    [MVK_OP_vkCompileDeferredNV] = run_vkCompileDeferredNV,
    [MVK_OP_vkCmdWriteBufferMarkerAMD] = run_vkCmdWriteBufferMarkerAMD,
    [MVK_OP_vkCmdWriteBufferMarker2AMD] = run_vkCmdWriteBufferMarker2AMD,
    [MVK_OP_vkCmdDrawMeshTasksNV] = run_vkCmdDrawMeshTasksNV,
    [MVK_OP_vkCmdDrawMeshTasksIndirectNV] = run_vkCmdDrawMeshTasksIndirectNV,
    [MVK_OP_vkCmdDrawMeshTasksIndirectCountNV] = run_vkCmdDrawMeshTasksIndirectCountNV,
    [MVK_OP_vkCmdSetExclusiveScissorEnableNV] = run_vkCmdSetExclusiveScissorEnableNV,
    [MVK_OP_vkCmdSetExclusiveScissorNV] = run_vkCmdSetExclusiveScissorNV,
    [MVK_OP_vkUninitializePerformanceApiINTEL] = run_vkUninitializePerformanceApiINTEL,
    [MVK_OP_vkAcquirePerformanceConfigurationINTEL] = run_vkAcquirePerformanceConfigurationINTEL,
    [MVK_OP_vkReleasePerformanceConfigurationINTEL] = run_vkReleasePerformanceConfigurationINTEL,
    [MVK_OP_vkQueueSetPerformanceConfigurationINTEL] = run_vkQueueSetPerformanceConfigurationINTEL,
    [MVK_OP_vkGetPerformanceParameterINTEL] = run_vkGetPerformanceParameterINTEL,
    [MVK_OP_vkGetPhysicalDeviceFragmentShadingRatesKHR] = run_vkGetPhysicalDeviceFragmentShadingRatesKHR,
    [MVK_OP_vkCmdSetFragmentShadingRateKHR] = run_vkCmdSetFragmentShadingRateKHR,
    [MVK_OP_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV] = run_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV,
    [MVK_OP_vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV] = run_vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV,
    [MVK_OP_vkCreateDeferredOperationKHR] = run_vkCreateDeferredOperationKHR,
    [MVK_OP_vkDestroyDeferredOperationKHR] = run_vkDestroyDeferredOperationKHR,
    [MVK_OP_vkGetDeferredOperationMaxConcurrencyKHR] = run_vkGetDeferredOperationMaxConcurrencyKHR,
    [MVK_OP_vkGetDeferredOperationResultKHR] = run_vkGetDeferredOperationResultKHR,
    [MVK_OP_vkDeferredOperationJoinKHR] = run_vkDeferredOperationJoinKHR,
    [MVK_OP_vkGetPipelineExecutablePropertiesKHR] = run_vkGetPipelineExecutablePropertiesKHR,
    [MVK_OP_vkGetPipelineExecutableStatisticsKHR] = run_vkGetPipelineExecutableStatisticsKHR,
    [MVK_OP_vkGetPipelineExecutableInternalRepresentationsKHR] = run_vkGetPipelineExecutableInternalRepresentationsKHR,
    [MVK_OP_vkGetGeneratedCommandsMemoryRequirementsNV] = run_vkGetGeneratedCommandsMemoryRequirementsNV,
    [MVK_OP_vkCmdPreprocessGeneratedCommandsNV] = run_vkCmdPreprocessGeneratedCommandsNV,
    [MVK_OP_vkCmdExecuteGeneratedCommandsNV] = run_vkCmdExecuteGeneratedCommandsNV,
    [MVK_OP_vkCmdBindPipelineShaderGroupNV] = run_vkCmdBindPipelineShaderGroupNV,
    [MVK_OP_vkCreateIndirectCommandsLayoutNV] = run_vkCreateIndirectCommandsLayoutNV,
    [MVK_OP_vkDestroyIndirectCommandsLayoutNV] = run_vkDestroyIndirectCommandsLayoutNV,
    [MVK_OP_vkCmdSetDepthBias2EXT] = run_vkCmdSetDepthBias2EXT,
    [MVK_OP_vkQueueSetPerfHintQCOM] = run_vkQueueSetPerfHintQCOM,
    [MVK_OP_vkSetLatencySleepModeLegacyNV] = run_vkSetLatencySleepModeLegacyNV,
    [MVK_OP_vkLatencySleepLegacyNV] = run_vkLatencySleepLegacyNV,
    [MVK_OP_vkSetLatencyMarkerLegacyNV] = run_vkSetLatencyMarkerLegacyNV,
    [MVK_OP_vkQueueNotifyOutOfBandLegacyNV] = run_vkQueueNotifyOutOfBandLegacyNV,
    [MVK_OP_vkGetSleepStatusLegacyNV] = run_vkGetSleepStatusLegacyNV,
    [MVK_OP_vkShutdownLatencyDeviceLegacyNV] = run_vkShutdownLatencyDeviceLegacyNV,
    [MVK_OP_vkGetDescriptorSetLayoutSizeEXT] = run_vkGetDescriptorSetLayoutSizeEXT,
    [MVK_OP_vkGetDescriptorSetLayoutBindingOffsetEXT] = run_vkGetDescriptorSetLayoutBindingOffsetEXT,
    [MVK_OP_vkCmdBindDescriptorBuffersEXT] = run_vkCmdBindDescriptorBuffersEXT,
    [MVK_OP_vkCmdSetDescriptorBufferOffsetsEXT] = run_vkCmdSetDescriptorBufferOffsetsEXT,
    [MVK_OP_vkCmdBindDescriptorBufferEmbeddedSamplersEXT] = run_vkCmdBindDescriptorBufferEmbeddedSamplersEXT,
    [MVK_OP_vkCmdBindIndexBuffer3KHR] = run_vkCmdBindIndexBuffer3KHR,
    [MVK_OP_vkCmdBindVertexBuffers3KHR] = run_vkCmdBindVertexBuffers3KHR,
    [MVK_OP_vkCmdDrawIndirect2KHR] = run_vkCmdDrawIndirect2KHR,
    [MVK_OP_vkCmdDrawIndexedIndirect2KHR] = run_vkCmdDrawIndexedIndirect2KHR,
    [MVK_OP_vkCmdDispatchIndirect2KHR] = run_vkCmdDispatchIndirect2KHR,
    [MVK_OP_vkCmdCopyMemoryKHR] = run_vkCmdCopyMemoryKHR,
    [MVK_OP_vkCmdCopyMemoryToImageKHR] = run_vkCmdCopyMemoryToImageKHR,
    [MVK_OP_vkCmdCopyImageToMemoryKHR] = run_vkCmdCopyImageToMemoryKHR,
    [MVK_OP_vkCmdUpdateMemoryKHR] = run_vkCmdUpdateMemoryKHR,
    [MVK_OP_vkCmdFillMemoryKHR] = run_vkCmdFillMemoryKHR,
    [MVK_OP_vkCmdCopyQueryPoolResultsToMemoryKHR] = run_vkCmdCopyQueryPoolResultsToMemoryKHR,
    [MVK_OP_vkCmdDrawIndirectCount2KHR] = run_vkCmdDrawIndirectCount2KHR,
    [MVK_OP_vkCmdDrawIndexedIndirectCount2KHR] = run_vkCmdDrawIndexedIndirectCount2KHR,
    [MVK_OP_vkCmdBeginConditionalRendering2EXT] = run_vkCmdBeginConditionalRendering2EXT,
    [MVK_OP_vkCmdBindTransformFeedbackBuffers2EXT] = run_vkCmdBindTransformFeedbackBuffers2EXT,
    [MVK_OP_vkCmdBeginTransformFeedback2EXT] = run_vkCmdBeginTransformFeedback2EXT,
    [MVK_OP_vkCmdEndTransformFeedback2EXT] = run_vkCmdEndTransformFeedback2EXT,
    [MVK_OP_vkCmdDrawIndirectByteCount2EXT] = run_vkCmdDrawIndirectByteCount2EXT,
    [MVK_OP_vkCmdDrawMeshTasksIndirect2EXT] = run_vkCmdDrawMeshTasksIndirect2EXT,
    [MVK_OP_vkCmdDrawMeshTasksIndirectCount2EXT] = run_vkCmdDrawMeshTasksIndirectCount2EXT,
    [MVK_OP_vkCmdWriteMarkerToMemoryAMD] = run_vkCmdWriteMarkerToMemoryAMD,
    [MVK_OP_vkCreateAccelerationStructure2KHR] = run_vkCreateAccelerationStructure2KHR,
    [MVK_OP_vkCmdSetFragmentShadingRateEnumNV] = run_vkCmdSetFragmentShadingRateEnumNV,
    [MVK_OP_vkCmdDrawMeshTasksEXT] = run_vkCmdDrawMeshTasksEXT,
    [MVK_OP_vkCmdDrawMeshTasksIndirectEXT] = run_vkCmdDrawMeshTasksIndirectEXT,
    [MVK_OP_vkCmdDrawMeshTasksIndirectCountEXT] = run_vkCmdDrawMeshTasksIndirectCountEXT,
    [MVK_OP_vkCmdSetVertexInputEXT] = run_vkCmdSetVertexInputEXT,
    [MVK_OP_vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI] = run_vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI,
    [MVK_OP_vkCmdSubpassShadingHUAWEI] = run_vkCmdSubpassShadingHUAWEI,
    [MVK_OP_vkCmdBindInvocationMaskHUAWEI] = run_vkCmdBindInvocationMaskHUAWEI,
    [MVK_OP_vkGetPipelinePropertiesEXT] = run_vkGetPipelinePropertiesEXT,
    [MVK_OP_vkCmdSetPatchControlPointsEXT] = run_vkCmdSetPatchControlPointsEXT,
    [MVK_OP_vkCmdSetLogicOpEXT] = run_vkCmdSetLogicOpEXT,
    [MVK_OP_vkCmdSetColorWriteEnableEXT] = run_vkCmdSetColorWriteEnableEXT,
    [MVK_OP_vkCmdTraceRaysIndirect2KHR] = run_vkCmdTraceRaysIndirect2KHR,
    [MVK_OP_vkCmdDrawMultiEXT] = run_vkCmdDrawMultiEXT,
    [MVK_OP_vkCmdDrawMultiIndexedEXT] = run_vkCmdDrawMultiIndexedEXT,
    [MVK_OP_vkCreateMicromapEXT] = run_vkCreateMicromapEXT,
    [MVK_OP_vkDestroyMicromapEXT] = run_vkDestroyMicromapEXT,
    [MVK_OP_vkCmdBuildMicromapsEXT] = run_vkCmdBuildMicromapsEXT,
    [MVK_OP_vkBuildMicromapsEXT] = run_vkBuildMicromapsEXT,
    [MVK_OP_vkCopyMicromapEXT] = run_vkCopyMicromapEXT,
    [MVK_OP_vkCopyMicromapToMemoryEXT] = run_vkCopyMicromapToMemoryEXT,
    [MVK_OP_vkCopyMemoryToMicromapEXT] = run_vkCopyMemoryToMicromapEXT,
    [MVK_OP_vkWriteMicromapsPropertiesEXT] = run_vkWriteMicromapsPropertiesEXT,
    [MVK_OP_vkCmdCopyMicromapEXT] = run_vkCmdCopyMicromapEXT,
    [MVK_OP_vkCmdCopyMicromapToMemoryEXT] = run_vkCmdCopyMicromapToMemoryEXT,
    [MVK_OP_vkCmdCopyMemoryToMicromapEXT] = run_vkCmdCopyMemoryToMicromapEXT,
    [MVK_OP_vkCmdWriteMicromapsPropertiesEXT] = run_vkCmdWriteMicromapsPropertiesEXT,
    [MVK_OP_vkGetDeviceMicromapCompatibilityEXT] = run_vkGetDeviceMicromapCompatibilityEXT,
    [MVK_OP_vkGetMicromapBuildSizesEXT] = run_vkGetMicromapBuildSizesEXT,
    [MVK_OP_vkCmdDrawClusterHUAWEI] = run_vkCmdDrawClusterHUAWEI,
    [MVK_OP_vkCmdDrawClusterIndirectHUAWEI] = run_vkCmdDrawClusterIndirectHUAWEI,
    [MVK_OP_vkSetDeviceMemoryPriorityEXT] = run_vkSetDeviceMemoryPriorityEXT,
    [MVK_OP_vkCmdSetDispatchParametersARM] = run_vkCmdSetDispatchParametersARM,
    [MVK_OP_vkGetDescriptorSetLayoutHostMappingInfoVALVE] = run_vkGetDescriptorSetLayoutHostMappingInfoVALVE,
    [MVK_OP_vkCmdCopyMemoryIndirectNV] = run_vkCmdCopyMemoryIndirectNV,
    [MVK_OP_vkCmdCopyMemoryToImageIndirectNV] = run_vkCmdCopyMemoryToImageIndirectNV,
    [MVK_OP_vkCmdDecompressMemoryNV] = run_vkCmdDecompressMemoryNV,
    [MVK_OP_vkCmdDecompressMemoryIndirectCountNV] = run_vkCmdDecompressMemoryIndirectCountNV,
    [MVK_OP_vkGetPipelineIndirectMemoryRequirementsNV] = run_vkGetPipelineIndirectMemoryRequirementsNV,
    [MVK_OP_vkCmdUpdatePipelineIndirectBufferNV] = run_vkCmdUpdatePipelineIndirectBufferNV,
    [MVK_OP_vkGetPipelineIndirectDeviceAddressNV] = run_vkGetPipelineIndirectDeviceAddressNV,
    [MVK_OP_vkCmdSetDepthClampEnableEXT] = run_vkCmdSetDepthClampEnableEXT,
    [MVK_OP_vkCmdSetPolygonModeEXT] = run_vkCmdSetPolygonModeEXT,
    [MVK_OP_vkCmdSetRasterizationSamplesEXT] = run_vkCmdSetRasterizationSamplesEXT,
    [MVK_OP_vkCmdSetSampleMaskEXT] = run_vkCmdSetSampleMaskEXT,
    [MVK_OP_vkCmdSetAlphaToCoverageEnableEXT] = run_vkCmdSetAlphaToCoverageEnableEXT,
    [MVK_OP_vkCmdSetAlphaToOneEnableEXT] = run_vkCmdSetAlphaToOneEnableEXT,
    [MVK_OP_vkCmdSetLogicOpEnableEXT] = run_vkCmdSetLogicOpEnableEXT,
    [MVK_OP_vkCmdSetColorBlendEnableEXT] = run_vkCmdSetColorBlendEnableEXT,
    [MVK_OP_vkCmdSetColorBlendEquationEXT] = run_vkCmdSetColorBlendEquationEXT,
    [MVK_OP_vkCmdSetColorWriteMaskEXT] = run_vkCmdSetColorWriteMaskEXT,
    [MVK_OP_vkCmdSetTessellationDomainOriginEXT] = run_vkCmdSetTessellationDomainOriginEXT,
    [MVK_OP_vkCmdSetRasterizationStreamEXT] = run_vkCmdSetRasterizationStreamEXT,
    [MVK_OP_vkCmdSetConservativeRasterizationModeEXT] = run_vkCmdSetConservativeRasterizationModeEXT,
    [MVK_OP_vkCmdSetExtraPrimitiveOverestimationSizeEXT] = run_vkCmdSetExtraPrimitiveOverestimationSizeEXT,
    [MVK_OP_vkCmdSetDepthClipEnableEXT] = run_vkCmdSetDepthClipEnableEXT,
    [MVK_OP_vkCmdSetSampleLocationsEnableEXT] = run_vkCmdSetSampleLocationsEnableEXT,
    [MVK_OP_vkCmdSetColorBlendAdvancedEXT] = run_vkCmdSetColorBlendAdvancedEXT,
    [MVK_OP_vkCmdSetProvokingVertexModeEXT] = run_vkCmdSetProvokingVertexModeEXT,
    [MVK_OP_vkCmdSetLineRasterizationModeEXT] = run_vkCmdSetLineRasterizationModeEXT,
    [MVK_OP_vkCmdSetLineStippleEnableEXT] = run_vkCmdSetLineStippleEnableEXT,
    [MVK_OP_vkCmdSetDepthClipNegativeOneToOneEXT] = run_vkCmdSetDepthClipNegativeOneToOneEXT,
    [MVK_OP_vkCmdSetViewportWScalingEnableNV] = run_vkCmdSetViewportWScalingEnableNV,
    [MVK_OP_vkCmdSetViewportSwizzleNV] = run_vkCmdSetViewportSwizzleNV,
    [MVK_OP_vkCmdSetCoverageToColorEnableNV] = run_vkCmdSetCoverageToColorEnableNV,
    [MVK_OP_vkCmdSetCoverageToColorLocationNV] = run_vkCmdSetCoverageToColorLocationNV,
    [MVK_OP_vkCmdSetCoverageModulationModeNV] = run_vkCmdSetCoverageModulationModeNV,
    [MVK_OP_vkCmdSetCoverageModulationTableEnableNV] = run_vkCmdSetCoverageModulationTableEnableNV,
    [MVK_OP_vkCmdSetCoverageModulationTableNV] = run_vkCmdSetCoverageModulationTableNV,
    [MVK_OP_vkCmdSetShadingRateImageEnableNV] = run_vkCmdSetShadingRateImageEnableNV,
    [MVK_OP_vkCmdSetRepresentativeFragmentTestEnableNV] = run_vkCmdSetRepresentativeFragmentTestEnableNV,
    [MVK_OP_vkCmdSetCoverageReductionModeNV] = run_vkCmdSetCoverageReductionModeNV,
    [MVK_OP_vkGetShaderModuleIdentifierEXT] = run_vkGetShaderModuleIdentifierEXT,
    [MVK_OP_vkGetShaderModuleCreateInfoIdentifierEXT] = run_vkGetShaderModuleCreateInfoIdentifierEXT,
    [MVK_OP_vkGetPhysicalDeviceOpticalFlowImageFormatsNV] = run_vkGetPhysicalDeviceOpticalFlowImageFormatsNV,
    [MVK_OP_vkCreateOpticalFlowSessionNV] = run_vkCreateOpticalFlowSessionNV,
    [MVK_OP_vkDestroyOpticalFlowSessionNV] = run_vkDestroyOpticalFlowSessionNV,
    [MVK_OP_vkBindOpticalFlowSessionImageNV] = run_vkBindOpticalFlowSessionImageNV,
    [MVK_OP_vkCmdOpticalFlowExecuteNV] = run_vkCmdOpticalFlowExecuteNV,
    [MVK_OP_vkCreateShadersEXT] = run_vkCreateShadersEXT,
    [MVK_OP_vkDestroyShaderEXT] = run_vkDestroyShaderEXT,
    [MVK_OP_vkGetShaderBinaryDataEXT] = run_vkGetShaderBinaryDataEXT,
    [MVK_OP_vkCmdBindShadersEXT] = run_vkCmdBindShadersEXT,
    [MVK_OP_vkCmdSetDepthClampRangeEXT] = run_vkCmdSetDepthClampRangeEXT,
    [MVK_OP_vkGetPhysicalDeviceCooperativeVectorPropertiesNV] = run_vkGetPhysicalDeviceCooperativeVectorPropertiesNV,
    [MVK_OP_vkConvertCooperativeVectorMatrixNV] = run_vkConvertCooperativeVectorMatrixNV,
    [MVK_OP_vkCmdConvertCooperativeVectorMatrixNV] = run_vkCmdConvertCooperativeVectorMatrixNV,
    [MVK_OP_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR] = run_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR,
    [MVK_OP_vkCmdSetAttachmentFeedbackLoopEnableEXT] = run_vkCmdSetAttachmentFeedbackLoopEnableEXT,
    [MVK_OP_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR] = run_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR,
    [MVK_OP_vkGetCalibratedTimestampsKHR] = run_vkGetCalibratedTimestampsKHR,
    [MVK_OP_vkCmdSetDescriptorBufferOffsets2EXT] = run_vkCmdSetDescriptorBufferOffsets2EXT,
    [MVK_OP_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT] = run_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT,
    [MVK_OP_vkCmdCopyMemoryIndirectKHR] = run_vkCmdCopyMemoryIndirectKHR,
    [MVK_OP_vkCmdCopyMemoryToImageIndirectKHR] = run_vkCmdCopyMemoryToImageIndirectKHR,
    [MVK_OP_vkCmdDecompressMemoryEXT] = run_vkCmdDecompressMemoryEXT,
    [MVK_OP_vkCmdDecompressMemoryIndirectCountEXT] = run_vkCmdDecompressMemoryIndirectCountEXT,
    [MVK_OP_vkGetClusterAccelerationStructureBuildSizesNV] = run_vkGetClusterAccelerationStructureBuildSizesNV,
    [MVK_OP_vkCmdBuildClusterAccelerationStructureIndirectNV] = run_vkCmdBuildClusterAccelerationStructureIndirectNV,
    [MVK_OP_vkGetPartitionedAccelerationStructuresBuildSizesNV] = run_vkGetPartitionedAccelerationStructuresBuildSizesNV,
    [MVK_OP_vkCmdBuildPartitionedAccelerationStructuresNV] = run_vkCmdBuildPartitionedAccelerationStructuresNV,
    [MVK_OP_vkGetGeneratedCommandsMemoryRequirementsEXT] = run_vkGetGeneratedCommandsMemoryRequirementsEXT,
    [MVK_OP_vkCmdPreprocessGeneratedCommandsEXT] = run_vkCmdPreprocessGeneratedCommandsEXT,
    [MVK_OP_vkCmdExecuteGeneratedCommandsEXT] = run_vkCmdExecuteGeneratedCommandsEXT,
    [MVK_OP_vkCreateIndirectCommandsLayoutEXT] = run_vkCreateIndirectCommandsLayoutEXT,
    [MVK_OP_vkDestroyIndirectCommandsLayoutEXT] = run_vkDestroyIndirectCommandsLayoutEXT,
    [MVK_OP_vkCreateIndirectExecutionSetEXT] = run_vkCreateIndirectExecutionSetEXT,
    [MVK_OP_vkDestroyIndirectExecutionSetEXT] = run_vkDestroyIndirectExecutionSetEXT,
    [MVK_OP_vkUpdateIndirectExecutionSetPipelineEXT] = run_vkUpdateIndirectExecutionSetPipelineEXT,
    [MVK_OP_vkUpdateIndirectExecutionSetShaderEXT] = run_vkUpdateIndirectExecutionSetShaderEXT,
    [MVK_OP_vkGetDeviceFaultReportsKHR] = run_vkGetDeviceFaultReportsKHR,
    [MVK_OP_vkGetDeviceFaultDebugInfoKHR] = run_vkGetDeviceFaultDebugInfoKHR,
    [MVK_OP_vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV] = run_vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV,
    [MVK_OP_vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM] = run_vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM,
    [MVK_OP_vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM] = run_vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM,
    [MVK_OP_vkCreateShaderInstrumentationARM] = run_vkCreateShaderInstrumentationARM,
    [MVK_OP_vkDestroyShaderInstrumentationARM] = run_vkDestroyShaderInstrumentationARM,
    [MVK_OP_vkCmdBeginShaderInstrumentationARM] = run_vkCmdBeginShaderInstrumentationARM,
    [MVK_OP_vkCmdEndShaderInstrumentationARM] = run_vkCmdEndShaderInstrumentationARM,
    [MVK_OP_vkClearShaderInstrumentationMetricsARM] = run_vkClearShaderInstrumentationMetricsARM,
    [MVK_OP_vkCmdBeginCustomResolveEXT] = run_vkCmdBeginCustomResolveEXT,
    [MVK_OP_vkCmdEndRendering2KHR] = run_vkCmdEndRendering2KHR,
    [MVK_OP_vkCmdSetComputeOccupancyPriorityNV] = run_vkCmdSetComputeOccupancyPriorityNV,
    [MVK_OP_vkGetPhysicalDeviceCooperativeMatrixProperties2EXT] = run_vkGetPhysicalDeviceCooperativeMatrixProperties2EXT,
    [MVK_OP_vkCmdSetPrimitiveRestartIndexEXT] = run_vkCmdSetPrimitiveRestartIndexEXT,
};

const char *const mvk_generated_names[MVK_OP_GENERATED_COUNT] = {
    [MVK_OP_vkGetPhysicalDeviceFeatures] = "vkGetPhysicalDeviceFeatures",
    [MVK_OP_vkGetPhysicalDeviceFormatProperties] = "vkGetPhysicalDeviceFormatProperties",
    [MVK_OP_vkGetPhysicalDeviceImageFormatProperties] = "vkGetPhysicalDeviceImageFormatProperties",
    [MVK_OP_vkGetPhysicalDeviceProperties] = "vkGetPhysicalDeviceProperties",
    [MVK_OP_vkGetPhysicalDeviceQueueFamilyProperties] = "vkGetPhysicalDeviceQueueFamilyProperties",
    [MVK_OP_vkGetPhysicalDeviceMemoryProperties] = "vkGetPhysicalDeviceMemoryProperties",
    [MVK_OP_vkBindBufferMemory] = "vkBindBufferMemory",
    [MVK_OP_vkBindImageMemory] = "vkBindImageMemory",
    [MVK_OP_vkGetBufferMemoryRequirements] = "vkGetBufferMemoryRequirements",
    [MVK_OP_vkGetImageMemoryRequirements] = "vkGetImageMemoryRequirements",
    [MVK_OP_vkGetImageSparseMemoryRequirements] = "vkGetImageSparseMemoryRequirements",
    [MVK_OP_vkGetPhysicalDeviceSparseImageFormatProperties] = "vkGetPhysicalDeviceSparseImageFormatProperties",
    [MVK_OP_vkCreateQueryPool] = "vkCreateQueryPool",
    [MVK_OP_vkDestroyQueryPool] = "vkDestroyQueryPool",
    [MVK_OP_vkCreateBuffer] = "vkCreateBuffer",
    [MVK_OP_vkDestroyBuffer] = "vkDestroyBuffer",
    [MVK_OP_vkCreateImage] = "vkCreateImage",
    [MVK_OP_vkDestroyImage] = "vkDestroyImage",
    [MVK_OP_vkGetImageSubresourceLayout] = "vkGetImageSubresourceLayout",
    [MVK_OP_vkCreateImageView] = "vkCreateImageView",
    [MVK_OP_vkDestroyImageView] = "vkDestroyImageView",
    [MVK_OP_vkCreateCommandPool] = "vkCreateCommandPool",
    [MVK_OP_vkCmdCopyBuffer] = "vkCmdCopyBuffer",
    [MVK_OP_vkCmdCopyImage] = "vkCmdCopyImage",
    [MVK_OP_vkCmdCopyBufferToImage] = "vkCmdCopyBufferToImage",
    [MVK_OP_vkCmdCopyImageToBuffer] = "vkCmdCopyImageToBuffer",
    [MVK_OP_vkCmdUpdateBuffer] = "vkCmdUpdateBuffer",
    [MVK_OP_vkCmdFillBuffer] = "vkCmdFillBuffer",
    [MVK_OP_vkCmdPipelineBarrier] = "vkCmdPipelineBarrier",
    [MVK_OP_vkCmdBeginQuery] = "vkCmdBeginQuery",
    [MVK_OP_vkCmdEndQuery] = "vkCmdEndQuery",
    [MVK_OP_vkCmdResetQueryPool] = "vkCmdResetQueryPool",
    [MVK_OP_vkCmdWriteTimestamp] = "vkCmdWriteTimestamp",
    [MVK_OP_vkCmdCopyQueryPoolResults] = "vkCmdCopyQueryPoolResults",
    [MVK_OP_vkCmdExecuteCommands] = "vkCmdExecuteCommands",
    [MVK_OP_vkCreateBufferView] = "vkCreateBufferView",
    [MVK_OP_vkDestroyBufferView] = "vkDestroyBufferView",
    [MVK_OP_vkCreateShaderModule] = "vkCreateShaderModule",
    [MVK_OP_vkDestroyShaderModule] = "vkDestroyShaderModule",
    [MVK_OP_vkCreatePipelineCache] = "vkCreatePipelineCache",
    [MVK_OP_vkDestroyPipelineCache] = "vkDestroyPipelineCache",
    [MVK_OP_vkGetPipelineCacheData] = "vkGetPipelineCacheData",
    [MVK_OP_vkMergePipelineCaches] = "vkMergePipelineCaches",
    [MVK_OP_vkCreateComputePipelines] = "vkCreateComputePipelines",
    [MVK_OP_vkDestroyPipeline] = "vkDestroyPipeline",
    [MVK_OP_vkCreatePipelineLayout] = "vkCreatePipelineLayout",
    [MVK_OP_vkDestroyPipelineLayout] = "vkDestroyPipelineLayout",
    [MVK_OP_vkCreateSampler] = "vkCreateSampler",
    [MVK_OP_vkDestroySampler] = "vkDestroySampler",
    [MVK_OP_vkCreateDescriptorSetLayout] = "vkCreateDescriptorSetLayout",
    [MVK_OP_vkDestroyDescriptorSetLayout] = "vkDestroyDescriptorSetLayout",
    [MVK_OP_vkUpdateDescriptorSets] = "vkUpdateDescriptorSets",
    [MVK_OP_vkCmdBindPipeline] = "vkCmdBindPipeline",
    [MVK_OP_vkCmdBindDescriptorSets] = "vkCmdBindDescriptorSets",
    [MVK_OP_vkCmdClearColorImage] = "vkCmdClearColorImage",
    [MVK_OP_vkCmdDispatch] = "vkCmdDispatch",
    [MVK_OP_vkCmdDispatchIndirect] = "vkCmdDispatchIndirect",
    [MVK_OP_vkCmdSetEvent] = "vkCmdSetEvent",
    [MVK_OP_vkCmdResetEvent] = "vkCmdResetEvent",
    [MVK_OP_vkCmdWaitEvents] = "vkCmdWaitEvents",
    [MVK_OP_vkCmdPushConstants] = "vkCmdPushConstants",
    [MVK_OP_vkCreateGraphicsPipelines] = "vkCreateGraphicsPipelines",
    [MVK_OP_vkCreateFramebuffer] = "vkCreateFramebuffer",
    [MVK_OP_vkDestroyFramebuffer] = "vkDestroyFramebuffer",
    [MVK_OP_vkCreateRenderPass] = "vkCreateRenderPass",
    [MVK_OP_vkDestroyRenderPass] = "vkDestroyRenderPass",
    [MVK_OP_vkGetRenderAreaGranularity] = "vkGetRenderAreaGranularity",
    [MVK_OP_vkCmdSetViewport] = "vkCmdSetViewport",
    [MVK_OP_vkCmdSetScissor] = "vkCmdSetScissor",
    [MVK_OP_vkCmdSetLineWidth] = "vkCmdSetLineWidth",
    [MVK_OP_vkCmdSetDepthBias] = "vkCmdSetDepthBias",
    [MVK_OP_vkCmdSetBlendConstants] = "vkCmdSetBlendConstants",
    [MVK_OP_vkCmdSetDepthBounds] = "vkCmdSetDepthBounds",
    [MVK_OP_vkCmdSetStencilCompareMask] = "vkCmdSetStencilCompareMask",
    [MVK_OP_vkCmdSetStencilWriteMask] = "vkCmdSetStencilWriteMask",
    [MVK_OP_vkCmdSetStencilReference] = "vkCmdSetStencilReference",
    [MVK_OP_vkCmdBindIndexBuffer] = "vkCmdBindIndexBuffer",
    [MVK_OP_vkCmdBindVertexBuffers] = "vkCmdBindVertexBuffers",
    [MVK_OP_vkCmdDraw] = "vkCmdDraw",
    [MVK_OP_vkCmdDrawIndexed] = "vkCmdDrawIndexed",
    [MVK_OP_vkCmdDrawIndirect] = "vkCmdDrawIndirect",
    [MVK_OP_vkCmdDrawIndexedIndirect] = "vkCmdDrawIndexedIndirect",
    [MVK_OP_vkCmdBlitImage] = "vkCmdBlitImage",
    [MVK_OP_vkCmdClearDepthStencilImage] = "vkCmdClearDepthStencilImage",
    [MVK_OP_vkCmdClearAttachments] = "vkCmdClearAttachments",
    [MVK_OP_vkCmdResolveImage] = "vkCmdResolveImage",
    [MVK_OP_vkCmdBeginRenderPass] = "vkCmdBeginRenderPass",
    [MVK_OP_vkCmdNextSubpass] = "vkCmdNextSubpass",
    [MVK_OP_vkCmdEndRenderPass] = "vkCmdEndRenderPass",
    [MVK_OP_vkBindBufferMemory2] = "vkBindBufferMemory2",
    [MVK_OP_vkBindImageMemory2] = "vkBindImageMemory2",
    [MVK_OP_vkGetDeviceGroupPeerMemoryFeatures] = "vkGetDeviceGroupPeerMemoryFeatures",
    [MVK_OP_vkCmdSetDeviceMask] = "vkCmdSetDeviceMask",
    [MVK_OP_vkGetImageMemoryRequirements2] = "vkGetImageMemoryRequirements2",
    [MVK_OP_vkGetBufferMemoryRequirements2] = "vkGetBufferMemoryRequirements2",
    [MVK_OP_vkGetImageSparseMemoryRequirements2] = "vkGetImageSparseMemoryRequirements2",
    [MVK_OP_vkGetPhysicalDeviceFeatures2] = "vkGetPhysicalDeviceFeatures2",
    [MVK_OP_vkGetPhysicalDeviceProperties2] = "vkGetPhysicalDeviceProperties2",
    [MVK_OP_vkGetPhysicalDeviceFormatProperties2] = "vkGetPhysicalDeviceFormatProperties2",
    [MVK_OP_vkGetPhysicalDeviceImageFormatProperties2] = "vkGetPhysicalDeviceImageFormatProperties2",
    [MVK_OP_vkGetPhysicalDeviceQueueFamilyProperties2] = "vkGetPhysicalDeviceQueueFamilyProperties2",
    [MVK_OP_vkGetPhysicalDeviceMemoryProperties2] = "vkGetPhysicalDeviceMemoryProperties2",
    [MVK_OP_vkGetPhysicalDeviceSparseImageFormatProperties2] = "vkGetPhysicalDeviceSparseImageFormatProperties2",
    [MVK_OP_vkTrimCommandPool] = "vkTrimCommandPool",
    [MVK_OP_vkGetPhysicalDeviceExternalBufferProperties] = "vkGetPhysicalDeviceExternalBufferProperties",
    [MVK_OP_vkGetPhysicalDeviceExternalFenceProperties] = "vkGetPhysicalDeviceExternalFenceProperties",
    [MVK_OP_vkGetPhysicalDeviceExternalSemaphoreProperties] = "vkGetPhysicalDeviceExternalSemaphoreProperties",
    [MVK_OP_vkCmdDispatchBase] = "vkCmdDispatchBase",
    [MVK_OP_vkCreateDescriptorUpdateTemplate] = "vkCreateDescriptorUpdateTemplate",
    [MVK_OP_vkDestroyDescriptorUpdateTemplate] = "vkDestroyDescriptorUpdateTemplate",
    [MVK_OP_vkGetDescriptorSetLayoutSupport] = "vkGetDescriptorSetLayoutSupport",
    [MVK_OP_vkCreateSamplerYcbcrConversion] = "vkCreateSamplerYcbcrConversion",
    [MVK_OP_vkDestroySamplerYcbcrConversion] = "vkDestroySamplerYcbcrConversion",
    [MVK_OP_vkResetQueryPool] = "vkResetQueryPool",
    [MVK_OP_vkGetBufferDeviceAddress] = "vkGetBufferDeviceAddress",
    [MVK_OP_vkGetBufferOpaqueCaptureAddress] = "vkGetBufferOpaqueCaptureAddress",
    [MVK_OP_vkGetDeviceMemoryOpaqueCaptureAddress] = "vkGetDeviceMemoryOpaqueCaptureAddress",
    [MVK_OP_vkCmdDrawIndirectCount] = "vkCmdDrawIndirectCount",
    [MVK_OP_vkCmdDrawIndexedIndirectCount] = "vkCmdDrawIndexedIndirectCount",
    [MVK_OP_vkCreateRenderPass2] = "vkCreateRenderPass2",
    [MVK_OP_vkCmdBeginRenderPass2] = "vkCmdBeginRenderPass2",
    [MVK_OP_vkCmdNextSubpass2] = "vkCmdNextSubpass2",
    [MVK_OP_vkCmdEndRenderPass2] = "vkCmdEndRenderPass2",
    [MVK_OP_vkGetPhysicalDeviceToolProperties] = "vkGetPhysicalDeviceToolProperties",
    [MVK_OP_vkCreatePrivateDataSlot] = "vkCreatePrivateDataSlot",
    [MVK_OP_vkDestroyPrivateDataSlot] = "vkDestroyPrivateDataSlot",
    [MVK_OP_vkSetPrivateData] = "vkSetPrivateData",
    [MVK_OP_vkGetPrivateData] = "vkGetPrivateData",
    [MVK_OP_vkCmdPipelineBarrier2] = "vkCmdPipelineBarrier2",
    [MVK_OP_vkCmdWriteTimestamp2] = "vkCmdWriteTimestamp2",
    [MVK_OP_vkCmdCopyBuffer2] = "vkCmdCopyBuffer2",
    [MVK_OP_vkCmdCopyImage2] = "vkCmdCopyImage2",
    [MVK_OP_vkCmdCopyBufferToImage2] = "vkCmdCopyBufferToImage2",
    [MVK_OP_vkCmdCopyImageToBuffer2] = "vkCmdCopyImageToBuffer2",
    [MVK_OP_vkGetDeviceBufferMemoryRequirements] = "vkGetDeviceBufferMemoryRequirements",
    [MVK_OP_vkGetDeviceImageMemoryRequirements] = "vkGetDeviceImageMemoryRequirements",
    [MVK_OP_vkGetDeviceImageSparseMemoryRequirements] = "vkGetDeviceImageSparseMemoryRequirements",
    [MVK_OP_vkCmdSetEvent2] = "vkCmdSetEvent2",
    [MVK_OP_vkCmdResetEvent2] = "vkCmdResetEvent2",
    [MVK_OP_vkCmdWaitEvents2] = "vkCmdWaitEvents2",
    [MVK_OP_vkCmdBlitImage2] = "vkCmdBlitImage2",
    [MVK_OP_vkCmdResolveImage2] = "vkCmdResolveImage2",
    [MVK_OP_vkCmdBeginRendering] = "vkCmdBeginRendering",
    [MVK_OP_vkCmdEndRendering] = "vkCmdEndRendering",
    [MVK_OP_vkCmdSetCullMode] = "vkCmdSetCullMode",
    [MVK_OP_vkCmdSetFrontFace] = "vkCmdSetFrontFace",
    [MVK_OP_vkCmdSetPrimitiveTopology] = "vkCmdSetPrimitiveTopology",
    [MVK_OP_vkCmdSetViewportWithCount] = "vkCmdSetViewportWithCount",
    [MVK_OP_vkCmdSetScissorWithCount] = "vkCmdSetScissorWithCount",
    [MVK_OP_vkCmdBindVertexBuffers2] = "vkCmdBindVertexBuffers2",
    [MVK_OP_vkCmdSetDepthTestEnable] = "vkCmdSetDepthTestEnable",
    [MVK_OP_vkCmdSetDepthWriteEnable] = "vkCmdSetDepthWriteEnable",
    [MVK_OP_vkCmdSetDepthCompareOp] = "vkCmdSetDepthCompareOp",
    [MVK_OP_vkCmdSetDepthBoundsTestEnable] = "vkCmdSetDepthBoundsTestEnable",
    [MVK_OP_vkCmdSetStencilTestEnable] = "vkCmdSetStencilTestEnable",
    [MVK_OP_vkCmdSetStencilOp] = "vkCmdSetStencilOp",
    [MVK_OP_vkCmdSetRasterizerDiscardEnable] = "vkCmdSetRasterizerDiscardEnable",
    [MVK_OP_vkCmdSetDepthBiasEnable] = "vkCmdSetDepthBiasEnable",
    [MVK_OP_vkCmdSetPrimitiveRestartEnable] = "vkCmdSetPrimitiveRestartEnable",
    [MVK_OP_vkGetDeviceImageSubresourceLayout] = "vkGetDeviceImageSubresourceLayout",
    [MVK_OP_vkGetImageSubresourceLayout2] = "vkGetImageSubresourceLayout2",
    [MVK_OP_vkCopyImageToImage] = "vkCopyImageToImage",
    [MVK_OP_vkTransitionImageLayout] = "vkTransitionImageLayout",
    [MVK_OP_vkCmdPushDescriptorSet] = "vkCmdPushDescriptorSet",
    [MVK_OP_vkCmdBindDescriptorSets2] = "vkCmdBindDescriptorSets2",
    [MVK_OP_vkCmdPushConstants2] = "vkCmdPushConstants2",
    [MVK_OP_vkCmdPushDescriptorSet2] = "vkCmdPushDescriptorSet2",
    [MVK_OP_vkCmdSetLineStipple] = "vkCmdSetLineStipple",
    [MVK_OP_vkCmdBindIndexBuffer2] = "vkCmdBindIndexBuffer2",
    [MVK_OP_vkGetRenderingAreaGranularity] = "vkGetRenderingAreaGranularity",
    [MVK_OP_vkCmdSetRenderingAttachmentLocations] = "vkCmdSetRenderingAttachmentLocations",
    [MVK_OP_vkCmdSetRenderingInputAttachmentIndices] = "vkCmdSetRenderingInputAttachmentIndices",
    [MVK_OP_vkCmdBindTransformFeedbackBuffersEXT] = "vkCmdBindTransformFeedbackBuffersEXT",
    [MVK_OP_vkCmdBeginTransformFeedbackEXT] = "vkCmdBeginTransformFeedbackEXT",
    [MVK_OP_vkCmdEndTransformFeedbackEXT] = "vkCmdEndTransformFeedbackEXT",
    [MVK_OP_vkCmdBeginQueryIndexedEXT] = "vkCmdBeginQueryIndexedEXT",
    [MVK_OP_vkCmdEndQueryIndexedEXT] = "vkCmdEndQueryIndexedEXT",
    [MVK_OP_vkCmdDrawIndirectByteCountEXT] = "vkCmdDrawIndirectByteCountEXT",
    [MVK_OP_vkGetImageViewHandleNVX] = "vkGetImageViewHandleNVX",
    [MVK_OP_vkGetImageViewHandle64NVX] = "vkGetImageViewHandle64NVX",
    [MVK_OP_vkGetImageViewAddressNVX] = "vkGetImageViewAddressNVX",
    [MVK_OP_vkGetDeviceCombinedImageSamplerIndexNVX] = "vkGetDeviceCombinedImageSamplerIndexNVX",
    [MVK_OP_vkGetShaderInfoAMD] = "vkGetShaderInfoAMD",
    [MVK_OP_vkGetPhysicalDeviceExternalImageFormatPropertiesNV] = "vkGetPhysicalDeviceExternalImageFormatPropertiesNV",
    [MVK_OP_vkCmdBeginConditionalRenderingEXT] = "vkCmdBeginConditionalRenderingEXT",
    [MVK_OP_vkCmdEndConditionalRenderingEXT] = "vkCmdEndConditionalRenderingEXT",
    [MVK_OP_vkCmdSetViewportWScalingNV] = "vkCmdSetViewportWScalingNV",
    [MVK_OP_vkCmdSetDiscardRectangleEXT] = "vkCmdSetDiscardRectangleEXT",
    [MVK_OP_vkCmdSetDiscardRectangleEnableEXT] = "vkCmdSetDiscardRectangleEnableEXT",
    [MVK_OP_vkCmdSetDiscardRectangleModeEXT] = "vkCmdSetDiscardRectangleModeEXT",
    [MVK_OP_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR] = "vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR",
    [MVK_OP_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR] = "vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR",
    [MVK_OP_vkAcquireProfilingLockKHR] = "vkAcquireProfilingLockKHR",
    [MVK_OP_vkReleaseProfilingLockKHR] = "vkReleaseProfilingLockKHR",
    [MVK_OP_vkCreateGpaSessionAMD] = "vkCreateGpaSessionAMD",
    [MVK_OP_vkDestroyGpaSessionAMD] = "vkDestroyGpaSessionAMD",
    [MVK_OP_vkSetGpaDeviceClockModeAMD] = "vkSetGpaDeviceClockModeAMD",
    [MVK_OP_vkGetGpaDeviceClockInfoAMD] = "vkGetGpaDeviceClockInfoAMD",
    [MVK_OP_vkCmdEndGpaSampleAMD] = "vkCmdEndGpaSampleAMD",
    [MVK_OP_vkGetGpaSessionStatusAMD] = "vkGetGpaSessionStatusAMD",
    [MVK_OP_vkGetGpaSessionResultsAMD] = "vkGetGpaSessionResultsAMD",
    [MVK_OP_vkResetGpaSessionAMD] = "vkResetGpaSessionAMD",
    [MVK_OP_vkCmdCopyGpaSessionResultsAMD] = "vkCmdCopyGpaSessionResultsAMD",
    [MVK_OP_vkWriteSamplerDescriptorsEXT] = "vkWriteSamplerDescriptorsEXT",
    [MVK_OP_vkWriteResourceDescriptorsEXT] = "vkWriteResourceDescriptorsEXT",
    [MVK_OP_vkCmdBindSamplerHeapEXT] = "vkCmdBindSamplerHeapEXT",
    [MVK_OP_vkCmdBindResourceHeapEXT] = "vkCmdBindResourceHeapEXT",
    [MVK_OP_vkCmdPushDataEXT] = "vkCmdPushDataEXT",
    [MVK_OP_vkGetImageOpaqueCaptureDataEXT] = "vkGetImageOpaqueCaptureDataEXT",
    [MVK_OP_vkGetPhysicalDeviceDescriptorSizeEXT] = "vkGetPhysicalDeviceDescriptorSizeEXT",
    [MVK_OP_vkRegisterCustomBorderColorEXT] = "vkRegisterCustomBorderColorEXT",
    [MVK_OP_vkUnregisterCustomBorderColorEXT] = "vkUnregisterCustomBorderColorEXT",
    [MVK_OP_vkGetTensorOpaqueCaptureDataARM] = "vkGetTensorOpaqueCaptureDataARM",
    [MVK_OP_vkCmdSetSampleLocationsEXT] = "vkCmdSetSampleLocationsEXT",
    [MVK_OP_vkGetPhysicalDeviceMultisamplePropertiesEXT] = "vkGetPhysicalDeviceMultisamplePropertiesEXT",
    [MVK_OP_vkCreateAccelerationStructureKHR] = "vkCreateAccelerationStructureKHR",
    [MVK_OP_vkDestroyAccelerationStructureKHR] = "vkDestroyAccelerationStructureKHR",
    [MVK_OP_vkCmdBuildAccelerationStructuresKHR] = "vkCmdBuildAccelerationStructuresKHR",
    [MVK_OP_vkCmdBuildAccelerationStructuresIndirectKHR] = "vkCmdBuildAccelerationStructuresIndirectKHR",
    [MVK_OP_vkBuildAccelerationStructuresKHR] = "vkBuildAccelerationStructuresKHR",
    [MVK_OP_vkCopyAccelerationStructureKHR] = "vkCopyAccelerationStructureKHR",
    [MVK_OP_vkCopyAccelerationStructureToMemoryKHR] = "vkCopyAccelerationStructureToMemoryKHR",
    [MVK_OP_vkCopyMemoryToAccelerationStructureKHR] = "vkCopyMemoryToAccelerationStructureKHR",
    [MVK_OP_vkWriteAccelerationStructuresPropertiesKHR] = "vkWriteAccelerationStructuresPropertiesKHR",
    [MVK_OP_vkCmdCopyAccelerationStructureKHR] = "vkCmdCopyAccelerationStructureKHR",
    [MVK_OP_vkCmdCopyAccelerationStructureToMemoryKHR] = "vkCmdCopyAccelerationStructureToMemoryKHR",
    [MVK_OP_vkCmdCopyMemoryToAccelerationStructureKHR] = "vkCmdCopyMemoryToAccelerationStructureKHR",
    [MVK_OP_vkGetAccelerationStructureDeviceAddressKHR] = "vkGetAccelerationStructureDeviceAddressKHR",
    [MVK_OP_vkCmdWriteAccelerationStructuresPropertiesKHR] = "vkCmdWriteAccelerationStructuresPropertiesKHR",
    [MVK_OP_vkGetDeviceAccelerationStructureCompatibilityKHR] = "vkGetDeviceAccelerationStructureCompatibilityKHR",
    [MVK_OP_vkGetAccelerationStructureBuildSizesKHR] = "vkGetAccelerationStructureBuildSizesKHR",
    [MVK_OP_vkCmdTraceRaysKHR] = "vkCmdTraceRaysKHR",
    [MVK_OP_vkCreateRayTracingPipelinesKHR] = "vkCreateRayTracingPipelinesKHR",
    [MVK_OP_vkGetRayTracingShaderGroupHandlesKHR] = "vkGetRayTracingShaderGroupHandlesKHR",
    [MVK_OP_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR] = "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR",
    [MVK_OP_vkCmdTraceRaysIndirectKHR] = "vkCmdTraceRaysIndirectKHR",
    [MVK_OP_vkGetRayTracingShaderGroupStackSizeKHR] = "vkGetRayTracingShaderGroupStackSizeKHR",
    [MVK_OP_vkCmdSetRayTracingPipelineStackSizeKHR] = "vkCmdSetRayTracingPipelineStackSizeKHR",
    [MVK_OP_vkGetImageDrmFormatModifierPropertiesEXT] = "vkGetImageDrmFormatModifierPropertiesEXT",
    [MVK_OP_vkCreateValidationCacheEXT] = "vkCreateValidationCacheEXT",
    [MVK_OP_vkDestroyValidationCacheEXT] = "vkDestroyValidationCacheEXT",
    [MVK_OP_vkMergeValidationCachesEXT] = "vkMergeValidationCachesEXT",
    [MVK_OP_vkGetValidationCacheDataEXT] = "vkGetValidationCacheDataEXT",
    [MVK_OP_vkCmdBindShadingRateImageNV] = "vkCmdBindShadingRateImageNV",
    [MVK_OP_vkCmdSetViewportShadingRatePaletteNV] = "vkCmdSetViewportShadingRatePaletteNV",
    [MVK_OP_vkCmdSetCoarseSampleOrderNV] = "vkCmdSetCoarseSampleOrderNV",
    [MVK_OP_vkCreateAccelerationStructureNV] = "vkCreateAccelerationStructureNV",
    [MVK_OP_vkDestroyAccelerationStructureNV] = "vkDestroyAccelerationStructureNV",
    [MVK_OP_vkGetAccelerationStructureMemoryRequirementsNV] = "vkGetAccelerationStructureMemoryRequirementsNV",
    [MVK_OP_vkBindAccelerationStructureMemoryNV] = "vkBindAccelerationStructureMemoryNV",
    [MVK_OP_vkCmdBuildAccelerationStructureNV] = "vkCmdBuildAccelerationStructureNV",
    [MVK_OP_vkCmdCopyAccelerationStructureNV] = "vkCmdCopyAccelerationStructureNV",
    [MVK_OP_vkCmdTraceRaysNV] = "vkCmdTraceRaysNV",
    [MVK_OP_vkCreateRayTracingPipelinesNV] = "vkCreateRayTracingPipelinesNV",
    [MVK_OP_vkGetAccelerationStructureHandleNV] = "vkGetAccelerationStructureHandleNV",
    [MVK_OP_vkCmdWriteAccelerationStructuresPropertiesNV] = "vkCmdWriteAccelerationStructuresPropertiesNV",
    [MVK_OP_vkCompileDeferredNV] = "vkCompileDeferredNV",
    [MVK_OP_vkCmdWriteBufferMarkerAMD] = "vkCmdWriteBufferMarkerAMD",
    [MVK_OP_vkCmdWriteBufferMarker2AMD] = "vkCmdWriteBufferMarker2AMD",
    [MVK_OP_vkCmdDrawMeshTasksNV] = "vkCmdDrawMeshTasksNV",
    [MVK_OP_vkCmdDrawMeshTasksIndirectNV] = "vkCmdDrawMeshTasksIndirectNV",
    [MVK_OP_vkCmdDrawMeshTasksIndirectCountNV] = "vkCmdDrawMeshTasksIndirectCountNV",
    [MVK_OP_vkCmdSetExclusiveScissorEnableNV] = "vkCmdSetExclusiveScissorEnableNV",
    [MVK_OP_vkCmdSetExclusiveScissorNV] = "vkCmdSetExclusiveScissorNV",
    [MVK_OP_vkUninitializePerformanceApiINTEL] = "vkUninitializePerformanceApiINTEL",
    [MVK_OP_vkAcquirePerformanceConfigurationINTEL] = "vkAcquirePerformanceConfigurationINTEL",
    [MVK_OP_vkReleasePerformanceConfigurationINTEL] = "vkReleasePerformanceConfigurationINTEL",
    [MVK_OP_vkQueueSetPerformanceConfigurationINTEL] = "vkQueueSetPerformanceConfigurationINTEL",
    [MVK_OP_vkGetPerformanceParameterINTEL] = "vkGetPerformanceParameterINTEL",
    [MVK_OP_vkGetPhysicalDeviceFragmentShadingRatesKHR] = "vkGetPhysicalDeviceFragmentShadingRatesKHR",
    [MVK_OP_vkCmdSetFragmentShadingRateKHR] = "vkCmdSetFragmentShadingRateKHR",
    [MVK_OP_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV] = "vkGetPhysicalDeviceCooperativeMatrixPropertiesNV",
    [MVK_OP_vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV] = "vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV",
    [MVK_OP_vkCreateDeferredOperationKHR] = "vkCreateDeferredOperationKHR",
    [MVK_OP_vkDestroyDeferredOperationKHR] = "vkDestroyDeferredOperationKHR",
    [MVK_OP_vkGetDeferredOperationMaxConcurrencyKHR] = "vkGetDeferredOperationMaxConcurrencyKHR",
    [MVK_OP_vkGetDeferredOperationResultKHR] = "vkGetDeferredOperationResultKHR",
    [MVK_OP_vkDeferredOperationJoinKHR] = "vkDeferredOperationJoinKHR",
    [MVK_OP_vkGetPipelineExecutablePropertiesKHR] = "vkGetPipelineExecutablePropertiesKHR",
    [MVK_OP_vkGetPipelineExecutableStatisticsKHR] = "vkGetPipelineExecutableStatisticsKHR",
    [MVK_OP_vkGetPipelineExecutableInternalRepresentationsKHR] = "vkGetPipelineExecutableInternalRepresentationsKHR",
    [MVK_OP_vkGetGeneratedCommandsMemoryRequirementsNV] = "vkGetGeneratedCommandsMemoryRequirementsNV",
    [MVK_OP_vkCmdPreprocessGeneratedCommandsNV] = "vkCmdPreprocessGeneratedCommandsNV",
    [MVK_OP_vkCmdExecuteGeneratedCommandsNV] = "vkCmdExecuteGeneratedCommandsNV",
    [MVK_OP_vkCmdBindPipelineShaderGroupNV] = "vkCmdBindPipelineShaderGroupNV",
    [MVK_OP_vkCreateIndirectCommandsLayoutNV] = "vkCreateIndirectCommandsLayoutNV",
    [MVK_OP_vkDestroyIndirectCommandsLayoutNV] = "vkDestroyIndirectCommandsLayoutNV",
    [MVK_OP_vkCmdSetDepthBias2EXT] = "vkCmdSetDepthBias2EXT",
    [MVK_OP_vkQueueSetPerfHintQCOM] = "vkQueueSetPerfHintQCOM",
    [MVK_OP_vkSetLatencySleepModeLegacyNV] = "vkSetLatencySleepModeLegacyNV",
    [MVK_OP_vkLatencySleepLegacyNV] = "vkLatencySleepLegacyNV",
    [MVK_OP_vkSetLatencyMarkerLegacyNV] = "vkSetLatencyMarkerLegacyNV",
    [MVK_OP_vkQueueNotifyOutOfBandLegacyNV] = "vkQueueNotifyOutOfBandLegacyNV",
    [MVK_OP_vkGetSleepStatusLegacyNV] = "vkGetSleepStatusLegacyNV",
    [MVK_OP_vkShutdownLatencyDeviceLegacyNV] = "vkShutdownLatencyDeviceLegacyNV",
    [MVK_OP_vkGetDescriptorSetLayoutSizeEXT] = "vkGetDescriptorSetLayoutSizeEXT",
    [MVK_OP_vkGetDescriptorSetLayoutBindingOffsetEXT] = "vkGetDescriptorSetLayoutBindingOffsetEXT",
    [MVK_OP_vkCmdBindDescriptorBuffersEXT] = "vkCmdBindDescriptorBuffersEXT",
    [MVK_OP_vkCmdSetDescriptorBufferOffsetsEXT] = "vkCmdSetDescriptorBufferOffsetsEXT",
    [MVK_OP_vkCmdBindDescriptorBufferEmbeddedSamplersEXT] = "vkCmdBindDescriptorBufferEmbeddedSamplersEXT",
    [MVK_OP_vkCmdBindIndexBuffer3KHR] = "vkCmdBindIndexBuffer3KHR",
    [MVK_OP_vkCmdBindVertexBuffers3KHR] = "vkCmdBindVertexBuffers3KHR",
    [MVK_OP_vkCmdDrawIndirect2KHR] = "vkCmdDrawIndirect2KHR",
    [MVK_OP_vkCmdDrawIndexedIndirect2KHR] = "vkCmdDrawIndexedIndirect2KHR",
    [MVK_OP_vkCmdDispatchIndirect2KHR] = "vkCmdDispatchIndirect2KHR",
    [MVK_OP_vkCmdCopyMemoryKHR] = "vkCmdCopyMemoryKHR",
    [MVK_OP_vkCmdCopyMemoryToImageKHR] = "vkCmdCopyMemoryToImageKHR",
    [MVK_OP_vkCmdCopyImageToMemoryKHR] = "vkCmdCopyImageToMemoryKHR",
    [MVK_OP_vkCmdUpdateMemoryKHR] = "vkCmdUpdateMemoryKHR",
    [MVK_OP_vkCmdFillMemoryKHR] = "vkCmdFillMemoryKHR",
    [MVK_OP_vkCmdCopyQueryPoolResultsToMemoryKHR] = "vkCmdCopyQueryPoolResultsToMemoryKHR",
    [MVK_OP_vkCmdDrawIndirectCount2KHR] = "vkCmdDrawIndirectCount2KHR",
    [MVK_OP_vkCmdDrawIndexedIndirectCount2KHR] = "vkCmdDrawIndexedIndirectCount2KHR",
    [MVK_OP_vkCmdBeginConditionalRendering2EXT] = "vkCmdBeginConditionalRendering2EXT",
    [MVK_OP_vkCmdBindTransformFeedbackBuffers2EXT] = "vkCmdBindTransformFeedbackBuffers2EXT",
    [MVK_OP_vkCmdBeginTransformFeedback2EXT] = "vkCmdBeginTransformFeedback2EXT",
    [MVK_OP_vkCmdEndTransformFeedback2EXT] = "vkCmdEndTransformFeedback2EXT",
    [MVK_OP_vkCmdDrawIndirectByteCount2EXT] = "vkCmdDrawIndirectByteCount2EXT",
    [MVK_OP_vkCmdDrawMeshTasksIndirect2EXT] = "vkCmdDrawMeshTasksIndirect2EXT",
    [MVK_OP_vkCmdDrawMeshTasksIndirectCount2EXT] = "vkCmdDrawMeshTasksIndirectCount2EXT",
    [MVK_OP_vkCmdWriteMarkerToMemoryAMD] = "vkCmdWriteMarkerToMemoryAMD",
    [MVK_OP_vkCreateAccelerationStructure2KHR] = "vkCreateAccelerationStructure2KHR",
    [MVK_OP_vkCmdSetFragmentShadingRateEnumNV] = "vkCmdSetFragmentShadingRateEnumNV",
    [MVK_OP_vkCmdDrawMeshTasksEXT] = "vkCmdDrawMeshTasksEXT",
    [MVK_OP_vkCmdDrawMeshTasksIndirectEXT] = "vkCmdDrawMeshTasksIndirectEXT",
    [MVK_OP_vkCmdDrawMeshTasksIndirectCountEXT] = "vkCmdDrawMeshTasksIndirectCountEXT",
    [MVK_OP_vkCmdSetVertexInputEXT] = "vkCmdSetVertexInputEXT",
    [MVK_OP_vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI] = "vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI",
    [MVK_OP_vkCmdSubpassShadingHUAWEI] = "vkCmdSubpassShadingHUAWEI",
    [MVK_OP_vkCmdBindInvocationMaskHUAWEI] = "vkCmdBindInvocationMaskHUAWEI",
    [MVK_OP_vkGetPipelinePropertiesEXT] = "vkGetPipelinePropertiesEXT",
    [MVK_OP_vkCmdSetPatchControlPointsEXT] = "vkCmdSetPatchControlPointsEXT",
    [MVK_OP_vkCmdSetLogicOpEXT] = "vkCmdSetLogicOpEXT",
    [MVK_OP_vkCmdSetColorWriteEnableEXT] = "vkCmdSetColorWriteEnableEXT",
    [MVK_OP_vkCmdTraceRaysIndirect2KHR] = "vkCmdTraceRaysIndirect2KHR",
    [MVK_OP_vkCmdDrawMultiEXT] = "vkCmdDrawMultiEXT",
    [MVK_OP_vkCmdDrawMultiIndexedEXT] = "vkCmdDrawMultiIndexedEXT",
    [MVK_OP_vkCreateMicromapEXT] = "vkCreateMicromapEXT",
    [MVK_OP_vkDestroyMicromapEXT] = "vkDestroyMicromapEXT",
    [MVK_OP_vkCmdBuildMicromapsEXT] = "vkCmdBuildMicromapsEXT",
    [MVK_OP_vkBuildMicromapsEXT] = "vkBuildMicromapsEXT",
    [MVK_OP_vkCopyMicromapEXT] = "vkCopyMicromapEXT",
    [MVK_OP_vkCopyMicromapToMemoryEXT] = "vkCopyMicromapToMemoryEXT",
    [MVK_OP_vkCopyMemoryToMicromapEXT] = "vkCopyMemoryToMicromapEXT",
    [MVK_OP_vkWriteMicromapsPropertiesEXT] = "vkWriteMicromapsPropertiesEXT",
    [MVK_OP_vkCmdCopyMicromapEXT] = "vkCmdCopyMicromapEXT",
    [MVK_OP_vkCmdCopyMicromapToMemoryEXT] = "vkCmdCopyMicromapToMemoryEXT",
    [MVK_OP_vkCmdCopyMemoryToMicromapEXT] = "vkCmdCopyMemoryToMicromapEXT",
    [MVK_OP_vkCmdWriteMicromapsPropertiesEXT] = "vkCmdWriteMicromapsPropertiesEXT",
    [MVK_OP_vkGetDeviceMicromapCompatibilityEXT] = "vkGetDeviceMicromapCompatibilityEXT",
    [MVK_OP_vkGetMicromapBuildSizesEXT] = "vkGetMicromapBuildSizesEXT",
    [MVK_OP_vkCmdDrawClusterHUAWEI] = "vkCmdDrawClusterHUAWEI",
    [MVK_OP_vkCmdDrawClusterIndirectHUAWEI] = "vkCmdDrawClusterIndirectHUAWEI",
    [MVK_OP_vkSetDeviceMemoryPriorityEXT] = "vkSetDeviceMemoryPriorityEXT",
    [MVK_OP_vkCmdSetDispatchParametersARM] = "vkCmdSetDispatchParametersARM",
    [MVK_OP_vkGetDescriptorSetLayoutHostMappingInfoVALVE] = "vkGetDescriptorSetLayoutHostMappingInfoVALVE",
    [MVK_OP_vkCmdCopyMemoryIndirectNV] = "vkCmdCopyMemoryIndirectNV",
    [MVK_OP_vkCmdCopyMemoryToImageIndirectNV] = "vkCmdCopyMemoryToImageIndirectNV",
    [MVK_OP_vkCmdDecompressMemoryNV] = "vkCmdDecompressMemoryNV",
    [MVK_OP_vkCmdDecompressMemoryIndirectCountNV] = "vkCmdDecompressMemoryIndirectCountNV",
    [MVK_OP_vkGetPipelineIndirectMemoryRequirementsNV] = "vkGetPipelineIndirectMemoryRequirementsNV",
    [MVK_OP_vkCmdUpdatePipelineIndirectBufferNV] = "vkCmdUpdatePipelineIndirectBufferNV",
    [MVK_OP_vkGetPipelineIndirectDeviceAddressNV] = "vkGetPipelineIndirectDeviceAddressNV",
    [MVK_OP_vkCmdSetDepthClampEnableEXT] = "vkCmdSetDepthClampEnableEXT",
    [MVK_OP_vkCmdSetPolygonModeEXT] = "vkCmdSetPolygonModeEXT",
    [MVK_OP_vkCmdSetRasterizationSamplesEXT] = "vkCmdSetRasterizationSamplesEXT",
    [MVK_OP_vkCmdSetSampleMaskEXT] = "vkCmdSetSampleMaskEXT",
    [MVK_OP_vkCmdSetAlphaToCoverageEnableEXT] = "vkCmdSetAlphaToCoverageEnableEXT",
    [MVK_OP_vkCmdSetAlphaToOneEnableEXT] = "vkCmdSetAlphaToOneEnableEXT",
    [MVK_OP_vkCmdSetLogicOpEnableEXT] = "vkCmdSetLogicOpEnableEXT",
    [MVK_OP_vkCmdSetColorBlendEnableEXT] = "vkCmdSetColorBlendEnableEXT",
    [MVK_OP_vkCmdSetColorBlendEquationEXT] = "vkCmdSetColorBlendEquationEXT",
    [MVK_OP_vkCmdSetColorWriteMaskEXT] = "vkCmdSetColorWriteMaskEXT",
    [MVK_OP_vkCmdSetTessellationDomainOriginEXT] = "vkCmdSetTessellationDomainOriginEXT",
    [MVK_OP_vkCmdSetRasterizationStreamEXT] = "vkCmdSetRasterizationStreamEXT",
    [MVK_OP_vkCmdSetConservativeRasterizationModeEXT] = "vkCmdSetConservativeRasterizationModeEXT",
    [MVK_OP_vkCmdSetExtraPrimitiveOverestimationSizeEXT] = "vkCmdSetExtraPrimitiveOverestimationSizeEXT",
    [MVK_OP_vkCmdSetDepthClipEnableEXT] = "vkCmdSetDepthClipEnableEXT",
    [MVK_OP_vkCmdSetSampleLocationsEnableEXT] = "vkCmdSetSampleLocationsEnableEXT",
    [MVK_OP_vkCmdSetColorBlendAdvancedEXT] = "vkCmdSetColorBlendAdvancedEXT",
    [MVK_OP_vkCmdSetProvokingVertexModeEXT] = "vkCmdSetProvokingVertexModeEXT",
    [MVK_OP_vkCmdSetLineRasterizationModeEXT] = "vkCmdSetLineRasterizationModeEXT",
    [MVK_OP_vkCmdSetLineStippleEnableEXT] = "vkCmdSetLineStippleEnableEXT",
    [MVK_OP_vkCmdSetDepthClipNegativeOneToOneEXT] = "vkCmdSetDepthClipNegativeOneToOneEXT",
    [MVK_OP_vkCmdSetViewportWScalingEnableNV] = "vkCmdSetViewportWScalingEnableNV",
    [MVK_OP_vkCmdSetViewportSwizzleNV] = "vkCmdSetViewportSwizzleNV",
    [MVK_OP_vkCmdSetCoverageToColorEnableNV] = "vkCmdSetCoverageToColorEnableNV",
    [MVK_OP_vkCmdSetCoverageToColorLocationNV] = "vkCmdSetCoverageToColorLocationNV",
    [MVK_OP_vkCmdSetCoverageModulationModeNV] = "vkCmdSetCoverageModulationModeNV",
    [MVK_OP_vkCmdSetCoverageModulationTableEnableNV] = "vkCmdSetCoverageModulationTableEnableNV",
    [MVK_OP_vkCmdSetCoverageModulationTableNV] = "vkCmdSetCoverageModulationTableNV",
    [MVK_OP_vkCmdSetShadingRateImageEnableNV] = "vkCmdSetShadingRateImageEnableNV",
    [MVK_OP_vkCmdSetRepresentativeFragmentTestEnableNV] = "vkCmdSetRepresentativeFragmentTestEnableNV",
    [MVK_OP_vkCmdSetCoverageReductionModeNV] = "vkCmdSetCoverageReductionModeNV",
    [MVK_OP_vkGetShaderModuleIdentifierEXT] = "vkGetShaderModuleIdentifierEXT",
    [MVK_OP_vkGetShaderModuleCreateInfoIdentifierEXT] = "vkGetShaderModuleCreateInfoIdentifierEXT",
    [MVK_OP_vkGetPhysicalDeviceOpticalFlowImageFormatsNV] = "vkGetPhysicalDeviceOpticalFlowImageFormatsNV",
    [MVK_OP_vkCreateOpticalFlowSessionNV] = "vkCreateOpticalFlowSessionNV",
    [MVK_OP_vkDestroyOpticalFlowSessionNV] = "vkDestroyOpticalFlowSessionNV",
    [MVK_OP_vkBindOpticalFlowSessionImageNV] = "vkBindOpticalFlowSessionImageNV",
    [MVK_OP_vkCmdOpticalFlowExecuteNV] = "vkCmdOpticalFlowExecuteNV",
    [MVK_OP_vkCreateShadersEXT] = "vkCreateShadersEXT",
    [MVK_OP_vkDestroyShaderEXT] = "vkDestroyShaderEXT",
    [MVK_OP_vkGetShaderBinaryDataEXT] = "vkGetShaderBinaryDataEXT",
    [MVK_OP_vkCmdBindShadersEXT] = "vkCmdBindShadersEXT",
    [MVK_OP_vkCmdSetDepthClampRangeEXT] = "vkCmdSetDepthClampRangeEXT",
    [MVK_OP_vkGetPhysicalDeviceCooperativeVectorPropertiesNV] = "vkGetPhysicalDeviceCooperativeVectorPropertiesNV",
    [MVK_OP_vkConvertCooperativeVectorMatrixNV] = "vkConvertCooperativeVectorMatrixNV",
    [MVK_OP_vkCmdConvertCooperativeVectorMatrixNV] = "vkCmdConvertCooperativeVectorMatrixNV",
    [MVK_OP_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR] = "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR",
    [MVK_OP_vkCmdSetAttachmentFeedbackLoopEnableEXT] = "vkCmdSetAttachmentFeedbackLoopEnableEXT",
    [MVK_OP_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR] = "vkGetPhysicalDeviceCalibrateableTimeDomainsKHR",
    [MVK_OP_vkGetCalibratedTimestampsKHR] = "vkGetCalibratedTimestampsKHR",
    [MVK_OP_vkCmdSetDescriptorBufferOffsets2EXT] = "vkCmdSetDescriptorBufferOffsets2EXT",
    [MVK_OP_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT] = "vkCmdBindDescriptorBufferEmbeddedSamplers2EXT",
    [MVK_OP_vkCmdCopyMemoryIndirectKHR] = "vkCmdCopyMemoryIndirectKHR",
    [MVK_OP_vkCmdCopyMemoryToImageIndirectKHR] = "vkCmdCopyMemoryToImageIndirectKHR",
    [MVK_OP_vkCmdDecompressMemoryEXT] = "vkCmdDecompressMemoryEXT",
    [MVK_OP_vkCmdDecompressMemoryIndirectCountEXT] = "vkCmdDecompressMemoryIndirectCountEXT",
    [MVK_OP_vkGetClusterAccelerationStructureBuildSizesNV] = "vkGetClusterAccelerationStructureBuildSizesNV",
    [MVK_OP_vkCmdBuildClusterAccelerationStructureIndirectNV] = "vkCmdBuildClusterAccelerationStructureIndirectNV",
    [MVK_OP_vkGetPartitionedAccelerationStructuresBuildSizesNV] = "vkGetPartitionedAccelerationStructuresBuildSizesNV",
    [MVK_OP_vkCmdBuildPartitionedAccelerationStructuresNV] = "vkCmdBuildPartitionedAccelerationStructuresNV",
    [MVK_OP_vkGetGeneratedCommandsMemoryRequirementsEXT] = "vkGetGeneratedCommandsMemoryRequirementsEXT",
    [MVK_OP_vkCmdPreprocessGeneratedCommandsEXT] = "vkCmdPreprocessGeneratedCommandsEXT",
    [MVK_OP_vkCmdExecuteGeneratedCommandsEXT] = "vkCmdExecuteGeneratedCommandsEXT",
    [MVK_OP_vkCreateIndirectCommandsLayoutEXT] = "vkCreateIndirectCommandsLayoutEXT",
    [MVK_OP_vkDestroyIndirectCommandsLayoutEXT] = "vkDestroyIndirectCommandsLayoutEXT",
    [MVK_OP_vkCreateIndirectExecutionSetEXT] = "vkCreateIndirectExecutionSetEXT",
    [MVK_OP_vkDestroyIndirectExecutionSetEXT] = "vkDestroyIndirectExecutionSetEXT",
    [MVK_OP_vkUpdateIndirectExecutionSetPipelineEXT] = "vkUpdateIndirectExecutionSetPipelineEXT",
    [MVK_OP_vkUpdateIndirectExecutionSetShaderEXT] = "vkUpdateIndirectExecutionSetShaderEXT",
    [MVK_OP_vkGetDeviceFaultReportsKHR] = "vkGetDeviceFaultReportsKHR",
    [MVK_OP_vkGetDeviceFaultDebugInfoKHR] = "vkGetDeviceFaultDebugInfoKHR",
    [MVK_OP_vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV] = "vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV",
    [MVK_OP_vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM] = "vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM",
    [MVK_OP_vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM] = "vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM",
    [MVK_OP_vkCreateShaderInstrumentationARM] = "vkCreateShaderInstrumentationARM",
    [MVK_OP_vkDestroyShaderInstrumentationARM] = "vkDestroyShaderInstrumentationARM",
    [MVK_OP_vkCmdBeginShaderInstrumentationARM] = "vkCmdBeginShaderInstrumentationARM",
    [MVK_OP_vkCmdEndShaderInstrumentationARM] = "vkCmdEndShaderInstrumentationARM",
    [MVK_OP_vkClearShaderInstrumentationMetricsARM] = "vkClearShaderInstrumentationMetricsARM",
    [MVK_OP_vkCmdBeginCustomResolveEXT] = "vkCmdBeginCustomResolveEXT",
    [MVK_OP_vkCmdEndRendering2KHR] = "vkCmdEndRendering2KHR",
    [MVK_OP_vkCmdSetComputeOccupancyPriorityNV] = "vkCmdSetComputeOccupancyPriorityNV",
    [MVK_OP_vkGetPhysicalDeviceCooperativeMatrixProperties2EXT] = "vkGetPhysicalDeviceCooperativeMatrixProperties2EXT",
    [MVK_OP_vkCmdSetPrimitiveRestartIndexEXT] = "vkCmdSetPrimitiveRestartIndexEXT",
};
