/* generated from vk.xml, do not edit. */
#ifndef MVK_PROTOCOL_H
#define MVK_PROTOCOL_H

#include <mime/mvk_wire.h>

enum {
    MVK_OP_NONE = 0,
    MVK_OP_vkGetPhysicalDeviceFeatures = 1,
    MVK_OP_vkGetPhysicalDeviceFormatProperties = 2,
    MVK_OP_vkGetPhysicalDeviceImageFormatProperties = 3,
    MVK_OP_vkGetPhysicalDeviceProperties = 4,
    MVK_OP_vkGetPhysicalDeviceQueueFamilyProperties = 5,
    MVK_OP_vkGetPhysicalDeviceMemoryProperties = 6,
    MVK_OP_vkBindBufferMemory = 7,
    MVK_OP_vkBindImageMemory = 8,
    MVK_OP_vkGetBufferMemoryRequirements = 9,
    MVK_OP_vkGetImageMemoryRequirements = 10,
    MVK_OP_vkGetImageSparseMemoryRequirements = 11,
    MVK_OP_vkGetPhysicalDeviceSparseImageFormatProperties = 12,
    MVK_OP_vkCreateQueryPool = 13,
    MVK_OP_vkDestroyQueryPool = 14,
    MVK_OP_vkCreateBuffer = 15,
    MVK_OP_vkDestroyBuffer = 16,
    MVK_OP_vkCreateImage = 17,
    MVK_OP_vkDestroyImage = 18,
    MVK_OP_vkGetImageSubresourceLayout = 19,
    MVK_OP_vkCreateImageView = 20,
    MVK_OP_vkDestroyImageView = 21,
    MVK_OP_vkCreateCommandPool = 22,
    MVK_OP_vkCmdCopyBuffer = 23,
    MVK_OP_vkCmdCopyImage = 24,
    MVK_OP_vkCmdCopyBufferToImage = 25,
    MVK_OP_vkCmdCopyImageToBuffer = 26,
    MVK_OP_vkCmdUpdateBuffer = 27,
    MVK_OP_vkCmdFillBuffer = 28,
    MVK_OP_vkCmdPipelineBarrier = 29,
    MVK_OP_vkCmdBeginQuery = 30,
    MVK_OP_vkCmdEndQuery = 31,
    MVK_OP_vkCmdResetQueryPool = 32,
    MVK_OP_vkCmdWriteTimestamp = 33,
    MVK_OP_vkCmdCopyQueryPoolResults = 34,
    MVK_OP_vkCmdExecuteCommands = 35,
    MVK_OP_vkCreateBufferView = 36,
    MVK_OP_vkDestroyBufferView = 37,
    MVK_OP_vkCreateShaderModule = 38,
    MVK_OP_vkDestroyShaderModule = 39,
    MVK_OP_vkCreatePipelineCache = 40,
    MVK_OP_vkDestroyPipelineCache = 41,
    MVK_OP_vkGetPipelineCacheData = 42,
    MVK_OP_vkMergePipelineCaches = 43,
    MVK_OP_vkCreateComputePipelines = 44,
    MVK_OP_vkDestroyPipeline = 45,
    MVK_OP_vkCreatePipelineLayout = 46,
    MVK_OP_vkDestroyPipelineLayout = 47,
    MVK_OP_vkCreateSampler = 48,
    MVK_OP_vkDestroySampler = 49,
    MVK_OP_vkCreateDescriptorSetLayout = 50,
    MVK_OP_vkDestroyDescriptorSetLayout = 51,
    MVK_OP_vkUpdateDescriptorSets = 52,
    MVK_OP_vkCmdBindPipeline = 53,
    MVK_OP_vkCmdBindDescriptorSets = 54,
    MVK_OP_vkCmdClearColorImage = 55,
    MVK_OP_vkCmdDispatch = 56,
    MVK_OP_vkCmdDispatchIndirect = 57,
    MVK_OP_vkCmdSetEvent = 58,
    MVK_OP_vkCmdResetEvent = 59,
    MVK_OP_vkCmdWaitEvents = 60,
    MVK_OP_vkCmdPushConstants = 61,
    MVK_OP_vkCreateGraphicsPipelines = 62,
    MVK_OP_vkCreateFramebuffer = 63,
    MVK_OP_vkDestroyFramebuffer = 64,
    MVK_OP_vkCreateRenderPass = 65,
    MVK_OP_vkDestroyRenderPass = 66,
    MVK_OP_vkGetRenderAreaGranularity = 67,
    MVK_OP_vkCmdSetViewport = 68,
    MVK_OP_vkCmdSetScissor = 69,
    MVK_OP_vkCmdSetLineWidth = 70,
    MVK_OP_vkCmdSetDepthBias = 71,
    MVK_OP_vkCmdSetBlendConstants = 72,
    MVK_OP_vkCmdSetDepthBounds = 73,
    MVK_OP_vkCmdSetStencilCompareMask = 74,
    MVK_OP_vkCmdSetStencilWriteMask = 75,
    MVK_OP_vkCmdSetStencilReference = 76,
    MVK_OP_vkCmdBindIndexBuffer = 77,
    MVK_OP_vkCmdBindVertexBuffers = 78,
    MVK_OP_vkCmdDraw = 79,
    MVK_OP_vkCmdDrawIndexed = 80,
    MVK_OP_vkCmdDrawIndirect = 81,
    MVK_OP_vkCmdDrawIndexedIndirect = 82,
    MVK_OP_vkCmdBlitImage = 83,
    MVK_OP_vkCmdClearDepthStencilImage = 84,
    MVK_OP_vkCmdClearAttachments = 85,
    MVK_OP_vkCmdResolveImage = 86,
    MVK_OP_vkCmdBeginRenderPass = 87,
    MVK_OP_vkCmdNextSubpass = 88,
    MVK_OP_vkCmdEndRenderPass = 89,
    MVK_OP_vkBindBufferMemory2 = 90,
    MVK_OP_vkBindImageMemory2 = 91,
    MVK_OP_vkGetDeviceGroupPeerMemoryFeatures = 92,
    MVK_OP_vkCmdSetDeviceMask = 93,
    MVK_OP_vkGetImageMemoryRequirements2 = 94,
    MVK_OP_vkGetBufferMemoryRequirements2 = 95,
    MVK_OP_vkGetImageSparseMemoryRequirements2 = 96,
    MVK_OP_vkGetPhysicalDeviceFeatures2 = 97,
    MVK_OP_vkGetPhysicalDeviceProperties2 = 98,
    MVK_OP_vkGetPhysicalDeviceFormatProperties2 = 99,
    MVK_OP_vkGetPhysicalDeviceImageFormatProperties2 = 100,
    MVK_OP_vkGetPhysicalDeviceQueueFamilyProperties2 = 101,
    MVK_OP_vkGetPhysicalDeviceMemoryProperties2 = 102,
    MVK_OP_vkGetPhysicalDeviceSparseImageFormatProperties2 = 103,
    MVK_OP_vkTrimCommandPool = 104,
    MVK_OP_vkGetPhysicalDeviceExternalBufferProperties = 105,
    MVK_OP_vkGetPhysicalDeviceExternalFenceProperties = 106,
    MVK_OP_vkGetPhysicalDeviceExternalSemaphoreProperties = 107,
    MVK_OP_vkCmdDispatchBase = 108,
    MVK_OP_vkCreateDescriptorUpdateTemplate = 109,
    MVK_OP_vkDestroyDescriptorUpdateTemplate = 110,
    MVK_OP_vkGetDescriptorSetLayoutSupport = 111,
    MVK_OP_vkCreateSamplerYcbcrConversion = 112,
    MVK_OP_vkDestroySamplerYcbcrConversion = 113,
    MVK_OP_vkResetQueryPool = 114,
    MVK_OP_vkGetBufferDeviceAddress = 115,
    MVK_OP_vkGetBufferOpaqueCaptureAddress = 116,
    MVK_OP_vkGetDeviceMemoryOpaqueCaptureAddress = 117,
    MVK_OP_vkCmdDrawIndirectCount = 118,
    MVK_OP_vkCmdDrawIndexedIndirectCount = 119,
    MVK_OP_vkCreateRenderPass2 = 120,
    MVK_OP_vkCmdBeginRenderPass2 = 121,
    MVK_OP_vkCmdNextSubpass2 = 122,
    MVK_OP_vkCmdEndRenderPass2 = 123,
    MVK_OP_vkGetPhysicalDeviceToolProperties = 124,
    MVK_OP_vkCreatePrivateDataSlot = 125,
    MVK_OP_vkDestroyPrivateDataSlot = 126,
    MVK_OP_vkSetPrivateData = 127,
    MVK_OP_vkGetPrivateData = 128,
    MVK_OP_vkCmdPipelineBarrier2 = 129,
    MVK_OP_vkCmdWriteTimestamp2 = 130,
    MVK_OP_vkCmdCopyBuffer2 = 131,
    MVK_OP_vkCmdCopyImage2 = 132,
    MVK_OP_vkCmdCopyBufferToImage2 = 133,
    MVK_OP_vkCmdCopyImageToBuffer2 = 134,
    MVK_OP_vkGetDeviceBufferMemoryRequirements = 135,
    MVK_OP_vkGetDeviceImageMemoryRequirements = 136,
    MVK_OP_vkGetDeviceImageSparseMemoryRequirements = 137,
    MVK_OP_vkCmdSetEvent2 = 138,
    MVK_OP_vkCmdResetEvent2 = 139,
    MVK_OP_vkCmdWaitEvents2 = 140,
    MVK_OP_vkCmdBlitImage2 = 141,
    MVK_OP_vkCmdResolveImage2 = 142,
    MVK_OP_vkCmdBeginRendering = 143,
    MVK_OP_vkCmdEndRendering = 144,
    MVK_OP_vkCmdSetCullMode = 145,
    MVK_OP_vkCmdSetFrontFace = 146,
    MVK_OP_vkCmdSetPrimitiveTopology = 147,
    MVK_OP_vkCmdSetViewportWithCount = 148,
    MVK_OP_vkCmdSetScissorWithCount = 149,
    MVK_OP_vkCmdBindVertexBuffers2 = 150,
    MVK_OP_vkCmdSetDepthTestEnable = 151,
    MVK_OP_vkCmdSetDepthWriteEnable = 152,
    MVK_OP_vkCmdSetDepthCompareOp = 153,
    MVK_OP_vkCmdSetDepthBoundsTestEnable = 154,
    MVK_OP_vkCmdSetStencilTestEnable = 155,
    MVK_OP_vkCmdSetStencilOp = 156,
    MVK_OP_vkCmdSetRasterizerDiscardEnable = 157,
    MVK_OP_vkCmdSetDepthBiasEnable = 158,
    MVK_OP_vkCmdSetPrimitiveRestartEnable = 159,
    MVK_OP_vkGetDeviceImageSubresourceLayout = 160,
    MVK_OP_vkGetImageSubresourceLayout2 = 161,
    MVK_OP_vkCopyImageToImage = 162,
    MVK_OP_vkTransitionImageLayout = 163,
    MVK_OP_vkCmdPushDescriptorSet = 164,
    MVK_OP_vkCmdBindDescriptorSets2 = 165,
    MVK_OP_vkCmdPushConstants2 = 166,
    MVK_OP_vkCmdPushDescriptorSet2 = 167,
    MVK_OP_vkCmdSetLineStipple = 168,
    MVK_OP_vkCmdBindIndexBuffer2 = 169,
    MVK_OP_vkGetRenderingAreaGranularity = 170,
    MVK_OP_vkCmdSetRenderingAttachmentLocations = 171,
    MVK_OP_vkCmdSetRenderingInputAttachmentIndices = 172,
    MVK_OP_vkCmdBindTransformFeedbackBuffersEXT = 173,
    MVK_OP_vkCmdBeginTransformFeedbackEXT = 174,
    MVK_OP_vkCmdEndTransformFeedbackEXT = 175,
    MVK_OP_vkCmdBeginQueryIndexedEXT = 176,
    MVK_OP_vkCmdEndQueryIndexedEXT = 177,
    MVK_OP_vkCmdDrawIndirectByteCountEXT = 178,
    MVK_OP_vkGetImageViewHandleNVX = 179,
    MVK_OP_vkGetImageViewHandle64NVX = 180,
    MVK_OP_vkGetImageViewAddressNVX = 181,
    MVK_OP_vkGetDeviceCombinedImageSamplerIndexNVX = 182,
    MVK_OP_vkGetShaderInfoAMD = 183,
    MVK_OP_vkGetPhysicalDeviceExternalImageFormatPropertiesNV = 184,
    MVK_OP_vkCmdBeginConditionalRenderingEXT = 185,
    MVK_OP_vkCmdEndConditionalRenderingEXT = 186,
    MVK_OP_vkCmdSetViewportWScalingNV = 187,
    MVK_OP_vkCmdSetDiscardRectangleEXT = 188,
    MVK_OP_vkCmdSetDiscardRectangleEnableEXT = 189,
    MVK_OP_vkCmdSetDiscardRectangleModeEXT = 190,
    MVK_OP_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR = 191,
    MVK_OP_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR = 192,
    MVK_OP_vkAcquireProfilingLockKHR = 193,
    MVK_OP_vkReleaseProfilingLockKHR = 194,
    MVK_OP_vkCreateGpaSessionAMD = 195,
    MVK_OP_vkDestroyGpaSessionAMD = 196,
    MVK_OP_vkSetGpaDeviceClockModeAMD = 197,
    MVK_OP_vkGetGpaDeviceClockInfoAMD = 198,
    MVK_OP_vkCmdEndGpaSampleAMD = 199,
    MVK_OP_vkGetGpaSessionStatusAMD = 200,
    MVK_OP_vkGetGpaSessionResultsAMD = 201,
    MVK_OP_vkResetGpaSessionAMD = 202,
    MVK_OP_vkCmdCopyGpaSessionResultsAMD = 203,
    MVK_OP_vkWriteSamplerDescriptorsEXT = 204,
    MVK_OP_vkWriteResourceDescriptorsEXT = 205,
    MVK_OP_vkCmdBindSamplerHeapEXT = 206,
    MVK_OP_vkCmdBindResourceHeapEXT = 207,
    MVK_OP_vkCmdPushDataEXT = 208,
    MVK_OP_vkGetImageOpaqueCaptureDataEXT = 209,
    MVK_OP_vkGetPhysicalDeviceDescriptorSizeEXT = 210,
    MVK_OP_vkRegisterCustomBorderColorEXT = 211,
    MVK_OP_vkUnregisterCustomBorderColorEXT = 212,
    MVK_OP_vkGetTensorOpaqueCaptureDataARM = 213,
    MVK_OP_vkCmdSetSampleLocationsEXT = 214,
    MVK_OP_vkGetPhysicalDeviceMultisamplePropertiesEXT = 215,
    MVK_OP_vkCreateAccelerationStructureKHR = 216,
    MVK_OP_vkDestroyAccelerationStructureKHR = 217,
    MVK_OP_vkCmdBuildAccelerationStructuresKHR = 218,
    MVK_OP_vkCmdBuildAccelerationStructuresIndirectKHR = 219,
    MVK_OP_vkBuildAccelerationStructuresKHR = 220,
    MVK_OP_vkCopyAccelerationStructureKHR = 221,
    MVK_OP_vkCopyAccelerationStructureToMemoryKHR = 222,
    MVK_OP_vkCopyMemoryToAccelerationStructureKHR = 223,
    MVK_OP_vkWriteAccelerationStructuresPropertiesKHR = 224,
    MVK_OP_vkCmdCopyAccelerationStructureKHR = 225,
    MVK_OP_vkCmdCopyAccelerationStructureToMemoryKHR = 226,
    MVK_OP_vkCmdCopyMemoryToAccelerationStructureKHR = 227,
    MVK_OP_vkGetAccelerationStructureDeviceAddressKHR = 228,
    MVK_OP_vkCmdWriteAccelerationStructuresPropertiesKHR = 229,
    MVK_OP_vkGetDeviceAccelerationStructureCompatibilityKHR = 230,
    MVK_OP_vkGetAccelerationStructureBuildSizesKHR = 231,
    MVK_OP_vkCmdTraceRaysKHR = 232,
    MVK_OP_vkCreateRayTracingPipelinesKHR = 233,
    MVK_OP_vkGetRayTracingShaderGroupHandlesKHR = 234,
    MVK_OP_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR = 235,
    MVK_OP_vkCmdTraceRaysIndirectKHR = 236,
    MVK_OP_vkGetRayTracingShaderGroupStackSizeKHR = 237,
    MVK_OP_vkCmdSetRayTracingPipelineStackSizeKHR = 238,
    MVK_OP_vkGetImageDrmFormatModifierPropertiesEXT = 239,
    MVK_OP_vkCreateValidationCacheEXT = 240,
    MVK_OP_vkDestroyValidationCacheEXT = 241,
    MVK_OP_vkMergeValidationCachesEXT = 242,
    MVK_OP_vkGetValidationCacheDataEXT = 243,
    MVK_OP_vkCmdBindShadingRateImageNV = 244,
    MVK_OP_vkCmdSetViewportShadingRatePaletteNV = 245,
    MVK_OP_vkCmdSetCoarseSampleOrderNV = 246,
    MVK_OP_vkCreateAccelerationStructureNV = 247,
    MVK_OP_vkDestroyAccelerationStructureNV = 248,
    MVK_OP_vkGetAccelerationStructureMemoryRequirementsNV = 249,
    MVK_OP_vkBindAccelerationStructureMemoryNV = 250,
    MVK_OP_vkCmdBuildAccelerationStructureNV = 251,
    MVK_OP_vkCmdCopyAccelerationStructureNV = 252,
    MVK_OP_vkCmdTraceRaysNV = 253,
    MVK_OP_vkCreateRayTracingPipelinesNV = 254,
    MVK_OP_vkGetAccelerationStructureHandleNV = 255,
    MVK_OP_vkCmdWriteAccelerationStructuresPropertiesNV = 256,
    MVK_OP_vkCompileDeferredNV = 257,
    MVK_OP_vkCmdWriteBufferMarkerAMD = 258,
    MVK_OP_vkCmdWriteBufferMarker2AMD = 259,
    MVK_OP_vkCmdDrawMeshTasksNV = 260,
    MVK_OP_vkCmdDrawMeshTasksIndirectNV = 261,
    MVK_OP_vkCmdDrawMeshTasksIndirectCountNV = 262,
    MVK_OP_vkCmdSetExclusiveScissorEnableNV = 263,
    MVK_OP_vkCmdSetExclusiveScissorNV = 264,
    MVK_OP_vkUninitializePerformanceApiINTEL = 265,
    MVK_OP_vkAcquirePerformanceConfigurationINTEL = 266,
    MVK_OP_vkReleasePerformanceConfigurationINTEL = 267,
    MVK_OP_vkQueueSetPerformanceConfigurationINTEL = 268,
    MVK_OP_vkGetPerformanceParameterINTEL = 269,
    MVK_OP_vkGetPhysicalDeviceFragmentShadingRatesKHR = 270,
    MVK_OP_vkCmdSetFragmentShadingRateKHR = 271,
    MVK_OP_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV = 272,
    MVK_OP_vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV = 273,
    MVK_OP_vkCreateDeferredOperationKHR = 274,
    MVK_OP_vkDestroyDeferredOperationKHR = 275,
    MVK_OP_vkGetDeferredOperationMaxConcurrencyKHR = 276,
    MVK_OP_vkGetDeferredOperationResultKHR = 277,
    MVK_OP_vkDeferredOperationJoinKHR = 278,
    MVK_OP_vkGetPipelineExecutablePropertiesKHR = 279,
    MVK_OP_vkGetPipelineExecutableStatisticsKHR = 280,
    MVK_OP_vkGetPipelineExecutableInternalRepresentationsKHR = 281,
    MVK_OP_vkGetGeneratedCommandsMemoryRequirementsNV = 282,
    MVK_OP_vkCmdPreprocessGeneratedCommandsNV = 283,
    MVK_OP_vkCmdExecuteGeneratedCommandsNV = 284,
    MVK_OP_vkCmdBindPipelineShaderGroupNV = 285,
    MVK_OP_vkCreateIndirectCommandsLayoutNV = 286,
    MVK_OP_vkDestroyIndirectCommandsLayoutNV = 287,
    MVK_OP_vkCmdSetDepthBias2EXT = 288,
    MVK_OP_vkQueueSetPerfHintQCOM = 289,
    MVK_OP_vkSetLatencySleepModeLegacyNV = 290,
    MVK_OP_vkLatencySleepLegacyNV = 291,
    MVK_OP_vkSetLatencyMarkerLegacyNV = 292,
    MVK_OP_vkQueueNotifyOutOfBandLegacyNV = 293,
    MVK_OP_vkGetSleepStatusLegacyNV = 294,
    MVK_OP_vkShutdownLatencyDeviceLegacyNV = 295,
    MVK_OP_vkGetDescriptorSetLayoutSizeEXT = 296,
    MVK_OP_vkGetDescriptorSetLayoutBindingOffsetEXT = 297,
    MVK_OP_vkCmdBindDescriptorBuffersEXT = 298,
    MVK_OP_vkCmdSetDescriptorBufferOffsetsEXT = 299,
    MVK_OP_vkCmdBindDescriptorBufferEmbeddedSamplersEXT = 300,
    MVK_OP_vkCmdBindIndexBuffer3KHR = 301,
    MVK_OP_vkCmdBindVertexBuffers3KHR = 302,
    MVK_OP_vkCmdDrawIndirect2KHR = 303,
    MVK_OP_vkCmdDrawIndexedIndirect2KHR = 304,
    MVK_OP_vkCmdDispatchIndirect2KHR = 305,
    MVK_OP_vkCmdCopyMemoryKHR = 306,
    MVK_OP_vkCmdCopyMemoryToImageKHR = 307,
    MVK_OP_vkCmdCopyImageToMemoryKHR = 308,
    MVK_OP_vkCmdUpdateMemoryKHR = 309,
    MVK_OP_vkCmdFillMemoryKHR = 310,
    MVK_OP_vkCmdCopyQueryPoolResultsToMemoryKHR = 311,
    MVK_OP_vkCmdDrawIndirectCount2KHR = 312,
    MVK_OP_vkCmdDrawIndexedIndirectCount2KHR = 313,
    MVK_OP_vkCmdBeginConditionalRendering2EXT = 314,
    MVK_OP_vkCmdBindTransformFeedbackBuffers2EXT = 315,
    MVK_OP_vkCmdBeginTransformFeedback2EXT = 316,
    MVK_OP_vkCmdEndTransformFeedback2EXT = 317,
    MVK_OP_vkCmdDrawIndirectByteCount2EXT = 318,
    MVK_OP_vkCmdDrawMeshTasksIndirect2EXT = 319,
    MVK_OP_vkCmdDrawMeshTasksIndirectCount2EXT = 320,
    MVK_OP_vkCmdWriteMarkerToMemoryAMD = 321,
    MVK_OP_vkCreateAccelerationStructure2KHR = 322,
    MVK_OP_vkCmdSetFragmentShadingRateEnumNV = 323,
    MVK_OP_vkCmdDrawMeshTasksEXT = 324,
    MVK_OP_vkCmdDrawMeshTasksIndirectEXT = 325,
    MVK_OP_vkCmdDrawMeshTasksIndirectCountEXT = 326,
    MVK_OP_vkCmdSetVertexInputEXT = 327,
    MVK_OP_vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI = 328,
    MVK_OP_vkCmdSubpassShadingHUAWEI = 329,
    MVK_OP_vkCmdBindInvocationMaskHUAWEI = 330,
    MVK_OP_vkGetPipelinePropertiesEXT = 331,
    MVK_OP_vkCmdSetPatchControlPointsEXT = 332,
    MVK_OP_vkCmdSetLogicOpEXT = 333,
    MVK_OP_vkCmdSetColorWriteEnableEXT = 334,
    MVK_OP_vkCmdTraceRaysIndirect2KHR = 335,
    MVK_OP_vkCmdDrawMultiEXT = 336,
    MVK_OP_vkCmdDrawMultiIndexedEXT = 337,
    MVK_OP_vkCreateMicromapEXT = 338,
    MVK_OP_vkDestroyMicromapEXT = 339,
    MVK_OP_vkCmdBuildMicromapsEXT = 340,
    MVK_OP_vkBuildMicromapsEXT = 341,
    MVK_OP_vkCopyMicromapEXT = 342,
    MVK_OP_vkCopyMicromapToMemoryEXT = 343,
    MVK_OP_vkCopyMemoryToMicromapEXT = 344,
    MVK_OP_vkWriteMicromapsPropertiesEXT = 345,
    MVK_OP_vkCmdCopyMicromapEXT = 346,
    MVK_OP_vkCmdCopyMicromapToMemoryEXT = 347,
    MVK_OP_vkCmdCopyMemoryToMicromapEXT = 348,
    MVK_OP_vkCmdWriteMicromapsPropertiesEXT = 349,
    MVK_OP_vkGetDeviceMicromapCompatibilityEXT = 350,
    MVK_OP_vkGetMicromapBuildSizesEXT = 351,
    MVK_OP_vkCmdDrawClusterHUAWEI = 352,
    MVK_OP_vkCmdDrawClusterIndirectHUAWEI = 353,
    MVK_OP_vkSetDeviceMemoryPriorityEXT = 354,
    MVK_OP_vkCmdSetDispatchParametersARM = 355,
    MVK_OP_vkGetDescriptorSetLayoutHostMappingInfoVALVE = 356,
    MVK_OP_vkCmdCopyMemoryIndirectNV = 357,
    MVK_OP_vkCmdCopyMemoryToImageIndirectNV = 358,
    MVK_OP_vkCmdDecompressMemoryNV = 359,
    MVK_OP_vkCmdDecompressMemoryIndirectCountNV = 360,
    MVK_OP_vkGetPipelineIndirectMemoryRequirementsNV = 361,
    MVK_OP_vkCmdUpdatePipelineIndirectBufferNV = 362,
    MVK_OP_vkGetPipelineIndirectDeviceAddressNV = 363,
    MVK_OP_vkCmdSetDepthClampEnableEXT = 364,
    MVK_OP_vkCmdSetPolygonModeEXT = 365,
    MVK_OP_vkCmdSetRasterizationSamplesEXT = 366,
    MVK_OP_vkCmdSetSampleMaskEXT = 367,
    MVK_OP_vkCmdSetAlphaToCoverageEnableEXT = 368,
    MVK_OP_vkCmdSetAlphaToOneEnableEXT = 369,
    MVK_OP_vkCmdSetLogicOpEnableEXT = 370,
    MVK_OP_vkCmdSetColorBlendEnableEXT = 371,
    MVK_OP_vkCmdSetColorBlendEquationEXT = 372,
    MVK_OP_vkCmdSetColorWriteMaskEXT = 373,
    MVK_OP_vkCmdSetTessellationDomainOriginEXT = 374,
    MVK_OP_vkCmdSetRasterizationStreamEXT = 375,
    MVK_OP_vkCmdSetConservativeRasterizationModeEXT = 376,
    MVK_OP_vkCmdSetExtraPrimitiveOverestimationSizeEXT = 377,
    MVK_OP_vkCmdSetDepthClipEnableEXT = 378,
    MVK_OP_vkCmdSetSampleLocationsEnableEXT = 379,
    MVK_OP_vkCmdSetColorBlendAdvancedEXT = 380,
    MVK_OP_vkCmdSetProvokingVertexModeEXT = 381,
    MVK_OP_vkCmdSetLineRasterizationModeEXT = 382,
    MVK_OP_vkCmdSetLineStippleEnableEXT = 383,
    MVK_OP_vkCmdSetDepthClipNegativeOneToOneEXT = 384,
    MVK_OP_vkCmdSetViewportWScalingEnableNV = 385,
    MVK_OP_vkCmdSetViewportSwizzleNV = 386,
    MVK_OP_vkCmdSetCoverageToColorEnableNV = 387,
    MVK_OP_vkCmdSetCoverageToColorLocationNV = 388,
    MVK_OP_vkCmdSetCoverageModulationModeNV = 389,
    MVK_OP_vkCmdSetCoverageModulationTableEnableNV = 390,
    MVK_OP_vkCmdSetCoverageModulationTableNV = 391,
    MVK_OP_vkCmdSetShadingRateImageEnableNV = 392,
    MVK_OP_vkCmdSetRepresentativeFragmentTestEnableNV = 393,
    MVK_OP_vkCmdSetCoverageReductionModeNV = 394,
    MVK_OP_vkGetShaderModuleIdentifierEXT = 395,
    MVK_OP_vkGetShaderModuleCreateInfoIdentifierEXT = 396,
    MVK_OP_vkGetPhysicalDeviceOpticalFlowImageFormatsNV = 397,
    MVK_OP_vkCreateOpticalFlowSessionNV = 398,
    MVK_OP_vkDestroyOpticalFlowSessionNV = 399,
    MVK_OP_vkBindOpticalFlowSessionImageNV = 400,
    MVK_OP_vkCmdOpticalFlowExecuteNV = 401,
    MVK_OP_vkCreateShadersEXT = 402,
    MVK_OP_vkDestroyShaderEXT = 403,
    MVK_OP_vkGetShaderBinaryDataEXT = 404,
    MVK_OP_vkCmdBindShadersEXT = 405,
    MVK_OP_vkCmdSetDepthClampRangeEXT = 406,
    MVK_OP_vkGetPhysicalDeviceCooperativeVectorPropertiesNV = 407,
    MVK_OP_vkConvertCooperativeVectorMatrixNV = 408,
    MVK_OP_vkCmdConvertCooperativeVectorMatrixNV = 409,
    MVK_OP_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR = 410,
    MVK_OP_vkCmdSetAttachmentFeedbackLoopEnableEXT = 411,
    MVK_OP_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR = 412,
    MVK_OP_vkGetCalibratedTimestampsKHR = 413,
    MVK_OP_vkCmdSetDescriptorBufferOffsets2EXT = 414,
    MVK_OP_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT = 415,
    MVK_OP_vkCmdCopyMemoryIndirectKHR = 416,
    MVK_OP_vkCmdCopyMemoryToImageIndirectKHR = 417,
    MVK_OP_vkCmdDecompressMemoryEXT = 418,
    MVK_OP_vkCmdDecompressMemoryIndirectCountEXT = 419,
    MVK_OP_vkGetClusterAccelerationStructureBuildSizesNV = 420,
    MVK_OP_vkCmdBuildClusterAccelerationStructureIndirectNV = 421,
    MVK_OP_vkGetPartitionedAccelerationStructuresBuildSizesNV = 422,
    MVK_OP_vkCmdBuildPartitionedAccelerationStructuresNV = 423,
    MVK_OP_vkGetGeneratedCommandsMemoryRequirementsEXT = 424,
    MVK_OP_vkCmdPreprocessGeneratedCommandsEXT = 425,
    MVK_OP_vkCmdExecuteGeneratedCommandsEXT = 426,
    MVK_OP_vkCreateIndirectCommandsLayoutEXT = 427,
    MVK_OP_vkDestroyIndirectCommandsLayoutEXT = 428,
    MVK_OP_vkCreateIndirectExecutionSetEXT = 429,
    MVK_OP_vkDestroyIndirectExecutionSetEXT = 430,
    MVK_OP_vkUpdateIndirectExecutionSetPipelineEXT = 431,
    MVK_OP_vkUpdateIndirectExecutionSetShaderEXT = 432,
    MVK_OP_vkGetDeviceFaultReportsKHR = 433,
    MVK_OP_vkGetDeviceFaultDebugInfoKHR = 434,
    MVK_OP_vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV = 435,
    MVK_OP_vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM = 436,
    MVK_OP_vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM = 437,
    MVK_OP_vkCreateShaderInstrumentationARM = 438,
    MVK_OP_vkDestroyShaderInstrumentationARM = 439,
    MVK_OP_vkCmdBeginShaderInstrumentationARM = 440,
    MVK_OP_vkCmdEndShaderInstrumentationARM = 441,
    MVK_OP_vkClearShaderInstrumentationMetricsARM = 442,
    MVK_OP_vkCmdBeginCustomResolveEXT = 443,
    MVK_OP_vkCmdEndRendering2KHR = 444,
    MVK_OP_vkCmdSetComputeOccupancyPriorityNV = 445,
    MVK_OP_vkGetPhysicalDeviceCooperativeMatrixProperties2EXT = 446,
    MVK_OP_vkCmdSetPrimitiveRestartIndexEXT = 447,
    MVK_OP_GENERATED_COUNT = 448,
};

void mvk_put_VkPhysicalDeviceFeatures(MvkEncoder *e, const VkPhysicalDeviceFeatures *s);
void mvk_get_VkPhysicalDeviceFeatures(MvkDecoder *d, VkPhysicalDeviceFeatures *s);
void mvk_put_VkFormatProperties(MvkEncoder *e, const VkFormatProperties *s);
void mvk_get_VkFormatProperties(MvkDecoder *d, VkFormatProperties *s);
void mvk_put_VkImageFormatProperties(MvkEncoder *e, const VkImageFormatProperties *s);
void mvk_get_VkImageFormatProperties(MvkDecoder *d, VkImageFormatProperties *s);
void mvk_put_VkExtent3D(MvkEncoder *e, const VkExtent3D *s);
void mvk_get_VkExtent3D(MvkDecoder *d, VkExtent3D *s);
void mvk_put_VkPhysicalDeviceProperties(MvkEncoder *e, const VkPhysicalDeviceProperties *s);
void mvk_get_VkPhysicalDeviceProperties(MvkDecoder *d, VkPhysicalDeviceProperties *s);
void mvk_put_VkPhysicalDeviceLimits(MvkEncoder *e, const VkPhysicalDeviceLimits *s);
void mvk_get_VkPhysicalDeviceLimits(MvkDecoder *d, VkPhysicalDeviceLimits *s);
void mvk_put_VkPhysicalDeviceSparseProperties(MvkEncoder *e, const VkPhysicalDeviceSparseProperties *s);
void mvk_get_VkPhysicalDeviceSparseProperties(MvkDecoder *d, VkPhysicalDeviceSparseProperties *s);
void mvk_put_VkQueueFamilyProperties(MvkEncoder *e, const VkQueueFamilyProperties *s);
void mvk_get_VkQueueFamilyProperties(MvkDecoder *d, VkQueueFamilyProperties *s);
void mvk_put_VkPhysicalDeviceMemoryProperties(MvkEncoder *e, const VkPhysicalDeviceMemoryProperties *s);
void mvk_get_VkPhysicalDeviceMemoryProperties(MvkDecoder *d, VkPhysicalDeviceMemoryProperties *s);
void mvk_put_VkMemoryType(MvkEncoder *e, const VkMemoryType *s);
void mvk_get_VkMemoryType(MvkDecoder *d, VkMemoryType *s);
void mvk_put_VkMemoryHeap(MvkEncoder *e, const VkMemoryHeap *s);
void mvk_get_VkMemoryHeap(MvkDecoder *d, VkMemoryHeap *s);
void mvk_put_VkMemoryRequirements(MvkEncoder *e, const VkMemoryRequirements *s);
void mvk_get_VkMemoryRequirements(MvkDecoder *d, VkMemoryRequirements *s);
void mvk_put_VkSparseImageMemoryRequirements(MvkEncoder *e, const VkSparseImageMemoryRequirements *s);
void mvk_get_VkSparseImageMemoryRequirements(MvkDecoder *d, VkSparseImageMemoryRequirements *s);
void mvk_put_VkSparseImageFormatProperties(MvkEncoder *e, const VkSparseImageFormatProperties *s);
void mvk_get_VkSparseImageFormatProperties(MvkDecoder *d, VkSparseImageFormatProperties *s);
void mvk_put_VkQueryPoolCreateInfo(MvkEncoder *e, const VkQueryPoolCreateInfo *s);
void mvk_get_VkQueryPoolCreateInfo(MvkDecoder *d, VkQueryPoolCreateInfo *s);
void mvk_put_VkBufferCreateInfo(MvkEncoder *e, const VkBufferCreateInfo *s);
void mvk_get_VkBufferCreateInfo(MvkDecoder *d, VkBufferCreateInfo *s);
void mvk_put_VkImageCreateInfo(MvkEncoder *e, const VkImageCreateInfo *s);
void mvk_get_VkImageCreateInfo(MvkDecoder *d, VkImageCreateInfo *s);
void mvk_put_VkImageSubresource(MvkEncoder *e, const VkImageSubresource *s);
void mvk_get_VkImageSubresource(MvkDecoder *d, VkImageSubresource *s);
void mvk_put_VkSubresourceLayout(MvkEncoder *e, const VkSubresourceLayout *s);
void mvk_get_VkSubresourceLayout(MvkDecoder *d, VkSubresourceLayout *s);
void mvk_put_VkImageViewCreateInfo(MvkEncoder *e, const VkImageViewCreateInfo *s);
void mvk_get_VkImageViewCreateInfo(MvkDecoder *d, VkImageViewCreateInfo *s);
void mvk_put_VkComponentMapping(MvkEncoder *e, const VkComponentMapping *s);
void mvk_get_VkComponentMapping(MvkDecoder *d, VkComponentMapping *s);
void mvk_put_VkImageSubresourceRange(MvkEncoder *e, const VkImageSubresourceRange *s);
void mvk_get_VkImageSubresourceRange(MvkDecoder *d, VkImageSubresourceRange *s);
void mvk_put_VkCommandPoolCreateInfo(MvkEncoder *e, const VkCommandPoolCreateInfo *s);
void mvk_get_VkCommandPoolCreateInfo(MvkDecoder *d, VkCommandPoolCreateInfo *s);
void mvk_put_VkBufferCopy(MvkEncoder *e, const VkBufferCopy *s);
void mvk_get_VkBufferCopy(MvkDecoder *d, VkBufferCopy *s);
void mvk_put_VkImageCopy(MvkEncoder *e, const VkImageCopy *s);
void mvk_get_VkImageCopy(MvkDecoder *d, VkImageCopy *s);
void mvk_put_VkImageSubresourceLayers(MvkEncoder *e, const VkImageSubresourceLayers *s);
void mvk_get_VkImageSubresourceLayers(MvkDecoder *d, VkImageSubresourceLayers *s);
void mvk_put_VkOffset3D(MvkEncoder *e, const VkOffset3D *s);
void mvk_get_VkOffset3D(MvkDecoder *d, VkOffset3D *s);
void mvk_put_VkBufferImageCopy(MvkEncoder *e, const VkBufferImageCopy *s);
void mvk_get_VkBufferImageCopy(MvkDecoder *d, VkBufferImageCopy *s);
void mvk_put_VkMemoryBarrier(MvkEncoder *e, const VkMemoryBarrier *s);
void mvk_get_VkMemoryBarrier(MvkDecoder *d, VkMemoryBarrier *s);
void mvk_put_VkBufferMemoryBarrier(MvkEncoder *e, const VkBufferMemoryBarrier *s);
void mvk_get_VkBufferMemoryBarrier(MvkDecoder *d, VkBufferMemoryBarrier *s);
void mvk_put_VkImageMemoryBarrier(MvkEncoder *e, const VkImageMemoryBarrier *s);
void mvk_get_VkImageMemoryBarrier(MvkDecoder *d, VkImageMemoryBarrier *s);
void mvk_put_VkBufferViewCreateInfo(MvkEncoder *e, const VkBufferViewCreateInfo *s);
void mvk_get_VkBufferViewCreateInfo(MvkDecoder *d, VkBufferViewCreateInfo *s);
void mvk_put_VkShaderModuleCreateInfo(MvkEncoder *e, const VkShaderModuleCreateInfo *s);
void mvk_get_VkShaderModuleCreateInfo(MvkDecoder *d, VkShaderModuleCreateInfo *s);
void mvk_put_VkPipelineCacheCreateInfo(MvkEncoder *e, const VkPipelineCacheCreateInfo *s);
void mvk_get_VkPipelineCacheCreateInfo(MvkDecoder *d, VkPipelineCacheCreateInfo *s);
void mvk_put_VkComputePipelineCreateInfo(MvkEncoder *e, const VkComputePipelineCreateInfo *s);
void mvk_get_VkComputePipelineCreateInfo(MvkDecoder *d, VkComputePipelineCreateInfo *s);
void mvk_put_VkPipelineShaderStageCreateInfo(MvkEncoder *e, const VkPipelineShaderStageCreateInfo *s);
void mvk_get_VkPipelineShaderStageCreateInfo(MvkDecoder *d, VkPipelineShaderStageCreateInfo *s);
void mvk_put_VkSpecializationInfo(MvkEncoder *e, const VkSpecializationInfo *s);
void mvk_get_VkSpecializationInfo(MvkDecoder *d, VkSpecializationInfo *s);
void mvk_put_VkSpecializationMapEntry(MvkEncoder *e, const VkSpecializationMapEntry *s);
void mvk_get_VkSpecializationMapEntry(MvkDecoder *d, VkSpecializationMapEntry *s);
void mvk_put_VkPipelineLayoutCreateInfo(MvkEncoder *e, const VkPipelineLayoutCreateInfo *s);
void mvk_get_VkPipelineLayoutCreateInfo(MvkDecoder *d, VkPipelineLayoutCreateInfo *s);
void mvk_put_VkPushConstantRange(MvkEncoder *e, const VkPushConstantRange *s);
void mvk_get_VkPushConstantRange(MvkDecoder *d, VkPushConstantRange *s);
void mvk_put_VkSamplerCreateInfo(MvkEncoder *e, const VkSamplerCreateInfo *s);
void mvk_get_VkSamplerCreateInfo(MvkDecoder *d, VkSamplerCreateInfo *s);
void mvk_put_VkDescriptorSetLayoutCreateInfo(MvkEncoder *e, const VkDescriptorSetLayoutCreateInfo *s);
void mvk_get_VkDescriptorSetLayoutCreateInfo(MvkDecoder *d, VkDescriptorSetLayoutCreateInfo *s);
void mvk_put_VkDescriptorSetLayoutBinding(MvkEncoder *e, const VkDescriptorSetLayoutBinding *s);
void mvk_get_VkDescriptorSetLayoutBinding(MvkDecoder *d, VkDescriptorSetLayoutBinding *s);
void mvk_put_VkWriteDescriptorSet(MvkEncoder *e, const VkWriteDescriptorSet *s);
void mvk_get_VkWriteDescriptorSet(MvkDecoder *d, VkWriteDescriptorSet *s);
void mvk_put_VkDescriptorImageInfo(MvkEncoder *e, const VkDescriptorImageInfo *s);
void mvk_get_VkDescriptorImageInfo(MvkDecoder *d, VkDescriptorImageInfo *s);
void mvk_put_VkDescriptorBufferInfo(MvkEncoder *e, const VkDescriptorBufferInfo *s);
void mvk_get_VkDescriptorBufferInfo(MvkDecoder *d, VkDescriptorBufferInfo *s);
void mvk_put_VkCopyDescriptorSet(MvkEncoder *e, const VkCopyDescriptorSet *s);
void mvk_get_VkCopyDescriptorSet(MvkDecoder *d, VkCopyDescriptorSet *s);
void mvk_put_VkClearColorValue(MvkEncoder *e, const VkClearColorValue *s);
void mvk_get_VkClearColorValue(MvkDecoder *d, VkClearColorValue *s);
void mvk_put_VkGraphicsPipelineCreateInfo(MvkEncoder *e, const VkGraphicsPipelineCreateInfo *s);
void mvk_get_VkGraphicsPipelineCreateInfo(MvkDecoder *d, VkGraphicsPipelineCreateInfo *s);
void mvk_put_VkPipelineVertexInputStateCreateInfo(MvkEncoder *e, const VkPipelineVertexInputStateCreateInfo *s);
void mvk_get_VkPipelineVertexInputStateCreateInfo(MvkDecoder *d, VkPipelineVertexInputStateCreateInfo *s);
void mvk_put_VkVertexInputBindingDescription(MvkEncoder *e, const VkVertexInputBindingDescription *s);
void mvk_get_VkVertexInputBindingDescription(MvkDecoder *d, VkVertexInputBindingDescription *s);
void mvk_put_VkVertexInputAttributeDescription(MvkEncoder *e, const VkVertexInputAttributeDescription *s);
void mvk_get_VkVertexInputAttributeDescription(MvkDecoder *d, VkVertexInputAttributeDescription *s);
void mvk_put_VkPipelineInputAssemblyStateCreateInfo(MvkEncoder *e, const VkPipelineInputAssemblyStateCreateInfo *s);
void mvk_get_VkPipelineInputAssemblyStateCreateInfo(MvkDecoder *d, VkPipelineInputAssemblyStateCreateInfo *s);
void mvk_put_VkPipelineTessellationStateCreateInfo(MvkEncoder *e, const VkPipelineTessellationStateCreateInfo *s);
void mvk_get_VkPipelineTessellationStateCreateInfo(MvkDecoder *d, VkPipelineTessellationStateCreateInfo *s);
void mvk_put_VkPipelineViewportStateCreateInfo(MvkEncoder *e, const VkPipelineViewportStateCreateInfo *s);
void mvk_get_VkPipelineViewportStateCreateInfo(MvkDecoder *d, VkPipelineViewportStateCreateInfo *s);
void mvk_put_VkViewport(MvkEncoder *e, const VkViewport *s);
void mvk_get_VkViewport(MvkDecoder *d, VkViewport *s);
void mvk_put_VkRect2D(MvkEncoder *e, const VkRect2D *s);
void mvk_get_VkRect2D(MvkDecoder *d, VkRect2D *s);
void mvk_put_VkOffset2D(MvkEncoder *e, const VkOffset2D *s);
void mvk_get_VkOffset2D(MvkDecoder *d, VkOffset2D *s);
void mvk_put_VkExtent2D(MvkEncoder *e, const VkExtent2D *s);
void mvk_get_VkExtent2D(MvkDecoder *d, VkExtent2D *s);
void mvk_put_VkPipelineRasterizationStateCreateInfo(MvkEncoder *e, const VkPipelineRasterizationStateCreateInfo *s);
void mvk_get_VkPipelineRasterizationStateCreateInfo(MvkDecoder *d, VkPipelineRasterizationStateCreateInfo *s);
void mvk_put_VkPipelineMultisampleStateCreateInfo(MvkEncoder *e, const VkPipelineMultisampleStateCreateInfo *s);
void mvk_get_VkPipelineMultisampleStateCreateInfo(MvkDecoder *d, VkPipelineMultisampleStateCreateInfo *s);
void mvk_put_VkPipelineDepthStencilStateCreateInfo(MvkEncoder *e, const VkPipelineDepthStencilStateCreateInfo *s);
void mvk_get_VkPipelineDepthStencilStateCreateInfo(MvkDecoder *d, VkPipelineDepthStencilStateCreateInfo *s);
void mvk_put_VkStencilOpState(MvkEncoder *e, const VkStencilOpState *s);
void mvk_get_VkStencilOpState(MvkDecoder *d, VkStencilOpState *s);
void mvk_put_VkPipelineColorBlendStateCreateInfo(MvkEncoder *e, const VkPipelineColorBlendStateCreateInfo *s);
void mvk_get_VkPipelineColorBlendStateCreateInfo(MvkDecoder *d, VkPipelineColorBlendStateCreateInfo *s);
void mvk_put_VkPipelineColorBlendAttachmentState(MvkEncoder *e, const VkPipelineColorBlendAttachmentState *s);
void mvk_get_VkPipelineColorBlendAttachmentState(MvkDecoder *d, VkPipelineColorBlendAttachmentState *s);
void mvk_put_VkPipelineDynamicStateCreateInfo(MvkEncoder *e, const VkPipelineDynamicStateCreateInfo *s);
void mvk_get_VkPipelineDynamicStateCreateInfo(MvkDecoder *d, VkPipelineDynamicStateCreateInfo *s);
void mvk_put_VkFramebufferCreateInfo(MvkEncoder *e, const VkFramebufferCreateInfo *s);
void mvk_get_VkFramebufferCreateInfo(MvkDecoder *d, VkFramebufferCreateInfo *s);
void mvk_put_VkRenderPassCreateInfo(MvkEncoder *e, const VkRenderPassCreateInfo *s);
void mvk_get_VkRenderPassCreateInfo(MvkDecoder *d, VkRenderPassCreateInfo *s);
void mvk_put_VkAttachmentDescription(MvkEncoder *e, const VkAttachmentDescription *s);
void mvk_get_VkAttachmentDescription(MvkDecoder *d, VkAttachmentDescription *s);
void mvk_put_VkSubpassDescription(MvkEncoder *e, const VkSubpassDescription *s);
void mvk_get_VkSubpassDescription(MvkDecoder *d, VkSubpassDescription *s);
void mvk_put_VkAttachmentReference(MvkEncoder *e, const VkAttachmentReference *s);
void mvk_get_VkAttachmentReference(MvkDecoder *d, VkAttachmentReference *s);
void mvk_put_VkSubpassDependency(MvkEncoder *e, const VkSubpassDependency *s);
void mvk_get_VkSubpassDependency(MvkDecoder *d, VkSubpassDependency *s);
void mvk_put_VkImageBlit(MvkEncoder *e, const VkImageBlit *s);
void mvk_get_VkImageBlit(MvkDecoder *d, VkImageBlit *s);
void mvk_put_VkClearDepthStencilValue(MvkEncoder *e, const VkClearDepthStencilValue *s);
void mvk_get_VkClearDepthStencilValue(MvkDecoder *d, VkClearDepthStencilValue *s);
void mvk_put_VkClearAttachment(MvkEncoder *e, const VkClearAttachment *s);
void mvk_get_VkClearAttachment(MvkDecoder *d, VkClearAttachment *s);
void mvk_put_VkClearValue(MvkEncoder *e, const VkClearValue *s);
void mvk_get_VkClearValue(MvkDecoder *d, VkClearValue *s);
void mvk_put_VkClearRect(MvkEncoder *e, const VkClearRect *s);
void mvk_get_VkClearRect(MvkDecoder *d, VkClearRect *s);
void mvk_put_VkImageResolve(MvkEncoder *e, const VkImageResolve *s);
void mvk_get_VkImageResolve(MvkDecoder *d, VkImageResolve *s);
void mvk_put_VkRenderPassBeginInfo(MvkEncoder *e, const VkRenderPassBeginInfo *s);
void mvk_get_VkRenderPassBeginInfo(MvkDecoder *d, VkRenderPassBeginInfo *s);
void mvk_put_VkBindBufferMemoryInfo(MvkEncoder *e, const VkBindBufferMemoryInfo *s);
void mvk_get_VkBindBufferMemoryInfo(MvkDecoder *d, VkBindBufferMemoryInfo *s);
void mvk_put_VkBindImageMemoryInfo(MvkEncoder *e, const VkBindImageMemoryInfo *s);
void mvk_get_VkBindImageMemoryInfo(MvkDecoder *d, VkBindImageMemoryInfo *s);
void mvk_put_VkImageMemoryRequirementsInfo2(MvkEncoder *e, const VkImageMemoryRequirementsInfo2 *s);
void mvk_get_VkImageMemoryRequirementsInfo2(MvkDecoder *d, VkImageMemoryRequirementsInfo2 *s);
void mvk_put_VkMemoryRequirements2(MvkEncoder *e, const VkMemoryRequirements2 *s);
void mvk_get_VkMemoryRequirements2(MvkDecoder *d, VkMemoryRequirements2 *s);
void mvk_put_VkBufferMemoryRequirementsInfo2(MvkEncoder *e, const VkBufferMemoryRequirementsInfo2 *s);
void mvk_get_VkBufferMemoryRequirementsInfo2(MvkDecoder *d, VkBufferMemoryRequirementsInfo2 *s);
void mvk_put_VkImageSparseMemoryRequirementsInfo2(MvkEncoder *e, const VkImageSparseMemoryRequirementsInfo2 *s);
void mvk_get_VkImageSparseMemoryRequirementsInfo2(MvkDecoder *d, VkImageSparseMemoryRequirementsInfo2 *s);
void mvk_put_VkSparseImageMemoryRequirements2(MvkEncoder *e, const VkSparseImageMemoryRequirements2 *s);
void mvk_get_VkSparseImageMemoryRequirements2(MvkDecoder *d, VkSparseImageMemoryRequirements2 *s);
void mvk_put_VkPhysicalDeviceFeatures2(MvkEncoder *e, const VkPhysicalDeviceFeatures2 *s);
void mvk_get_VkPhysicalDeviceFeatures2(MvkDecoder *d, VkPhysicalDeviceFeatures2 *s);
void mvk_put_VkPhysicalDeviceProperties2(MvkEncoder *e, const VkPhysicalDeviceProperties2 *s);
void mvk_get_VkPhysicalDeviceProperties2(MvkDecoder *d, VkPhysicalDeviceProperties2 *s);
void mvk_put_VkFormatProperties2(MvkEncoder *e, const VkFormatProperties2 *s);
void mvk_get_VkFormatProperties2(MvkDecoder *d, VkFormatProperties2 *s);
void mvk_put_VkPhysicalDeviceImageFormatInfo2(MvkEncoder *e, const VkPhysicalDeviceImageFormatInfo2 *s);
void mvk_get_VkPhysicalDeviceImageFormatInfo2(MvkDecoder *d, VkPhysicalDeviceImageFormatInfo2 *s);
void mvk_put_VkImageFormatProperties2(MvkEncoder *e, const VkImageFormatProperties2 *s);
void mvk_get_VkImageFormatProperties2(MvkDecoder *d, VkImageFormatProperties2 *s);
void mvk_put_VkQueueFamilyProperties2(MvkEncoder *e, const VkQueueFamilyProperties2 *s);
void mvk_get_VkQueueFamilyProperties2(MvkDecoder *d, VkQueueFamilyProperties2 *s);
void mvk_put_VkPhysicalDeviceMemoryProperties2(MvkEncoder *e, const VkPhysicalDeviceMemoryProperties2 *s);
void mvk_get_VkPhysicalDeviceMemoryProperties2(MvkDecoder *d, VkPhysicalDeviceMemoryProperties2 *s);
void mvk_put_VkPhysicalDeviceSparseImageFormatInfo2(MvkEncoder *e, const VkPhysicalDeviceSparseImageFormatInfo2 *s);
void mvk_get_VkPhysicalDeviceSparseImageFormatInfo2(MvkDecoder *d, VkPhysicalDeviceSparseImageFormatInfo2 *s);
void mvk_put_VkSparseImageFormatProperties2(MvkEncoder *e, const VkSparseImageFormatProperties2 *s);
void mvk_get_VkSparseImageFormatProperties2(MvkDecoder *d, VkSparseImageFormatProperties2 *s);
void mvk_put_VkPhysicalDeviceExternalBufferInfo(MvkEncoder *e, const VkPhysicalDeviceExternalBufferInfo *s);
void mvk_get_VkPhysicalDeviceExternalBufferInfo(MvkDecoder *d, VkPhysicalDeviceExternalBufferInfo *s);
void mvk_put_VkExternalBufferProperties(MvkEncoder *e, const VkExternalBufferProperties *s);
void mvk_get_VkExternalBufferProperties(MvkDecoder *d, VkExternalBufferProperties *s);
void mvk_put_VkExternalMemoryProperties(MvkEncoder *e, const VkExternalMemoryProperties *s);
void mvk_get_VkExternalMemoryProperties(MvkDecoder *d, VkExternalMemoryProperties *s);
void mvk_put_VkPhysicalDeviceExternalFenceInfo(MvkEncoder *e, const VkPhysicalDeviceExternalFenceInfo *s);
void mvk_get_VkPhysicalDeviceExternalFenceInfo(MvkDecoder *d, VkPhysicalDeviceExternalFenceInfo *s);
void mvk_put_VkExternalFenceProperties(MvkEncoder *e, const VkExternalFenceProperties *s);
void mvk_get_VkExternalFenceProperties(MvkDecoder *d, VkExternalFenceProperties *s);
void mvk_put_VkPhysicalDeviceExternalSemaphoreInfo(MvkEncoder *e, const VkPhysicalDeviceExternalSemaphoreInfo *s);
void mvk_get_VkPhysicalDeviceExternalSemaphoreInfo(MvkDecoder *d, VkPhysicalDeviceExternalSemaphoreInfo *s);
void mvk_put_VkExternalSemaphoreProperties(MvkEncoder *e, const VkExternalSemaphoreProperties *s);
void mvk_get_VkExternalSemaphoreProperties(MvkDecoder *d, VkExternalSemaphoreProperties *s);
void mvk_put_VkDescriptorUpdateTemplateCreateInfo(MvkEncoder *e, const VkDescriptorUpdateTemplateCreateInfo *s);
void mvk_get_VkDescriptorUpdateTemplateCreateInfo(MvkDecoder *d, VkDescriptorUpdateTemplateCreateInfo *s);
void mvk_put_VkDescriptorUpdateTemplateEntry(MvkEncoder *e, const VkDescriptorUpdateTemplateEntry *s);
void mvk_get_VkDescriptorUpdateTemplateEntry(MvkDecoder *d, VkDescriptorUpdateTemplateEntry *s);
void mvk_put_VkDescriptorSetLayoutSupport(MvkEncoder *e, const VkDescriptorSetLayoutSupport *s);
void mvk_get_VkDescriptorSetLayoutSupport(MvkDecoder *d, VkDescriptorSetLayoutSupport *s);
void mvk_put_VkSamplerYcbcrConversionCreateInfo(MvkEncoder *e, const VkSamplerYcbcrConversionCreateInfo *s);
void mvk_get_VkSamplerYcbcrConversionCreateInfo(MvkDecoder *d, VkSamplerYcbcrConversionCreateInfo *s);
void mvk_put_VkBufferDeviceAddressInfo(MvkEncoder *e, const VkBufferDeviceAddressInfo *s);
void mvk_get_VkBufferDeviceAddressInfo(MvkDecoder *d, VkBufferDeviceAddressInfo *s);
void mvk_put_VkDeviceMemoryOpaqueCaptureAddressInfo(MvkEncoder *e, const VkDeviceMemoryOpaqueCaptureAddressInfo *s);
void mvk_get_VkDeviceMemoryOpaqueCaptureAddressInfo(MvkDecoder *d, VkDeviceMemoryOpaqueCaptureAddressInfo *s);
void mvk_put_VkRenderPassCreateInfo2(MvkEncoder *e, const VkRenderPassCreateInfo2 *s);
void mvk_get_VkRenderPassCreateInfo2(MvkDecoder *d, VkRenderPassCreateInfo2 *s);
void mvk_put_VkAttachmentDescription2(MvkEncoder *e, const VkAttachmentDescription2 *s);
void mvk_get_VkAttachmentDescription2(MvkDecoder *d, VkAttachmentDescription2 *s);
void mvk_put_VkSubpassDescription2(MvkEncoder *e, const VkSubpassDescription2 *s);
void mvk_get_VkSubpassDescription2(MvkDecoder *d, VkSubpassDescription2 *s);
void mvk_put_VkAttachmentReference2(MvkEncoder *e, const VkAttachmentReference2 *s);
void mvk_get_VkAttachmentReference2(MvkDecoder *d, VkAttachmentReference2 *s);
void mvk_put_VkSubpassDependency2(MvkEncoder *e, const VkSubpassDependency2 *s);
void mvk_get_VkSubpassDependency2(MvkDecoder *d, VkSubpassDependency2 *s);
void mvk_put_VkSubpassBeginInfo(MvkEncoder *e, const VkSubpassBeginInfo *s);
void mvk_get_VkSubpassBeginInfo(MvkDecoder *d, VkSubpassBeginInfo *s);
void mvk_put_VkSubpassEndInfo(MvkEncoder *e, const VkSubpassEndInfo *s);
void mvk_get_VkSubpassEndInfo(MvkDecoder *d, VkSubpassEndInfo *s);
void mvk_put_VkPhysicalDeviceToolProperties(MvkEncoder *e, const VkPhysicalDeviceToolProperties *s);
void mvk_get_VkPhysicalDeviceToolProperties(MvkDecoder *d, VkPhysicalDeviceToolProperties *s);
void mvk_put_VkPrivateDataSlotCreateInfo(MvkEncoder *e, const VkPrivateDataSlotCreateInfo *s);
void mvk_get_VkPrivateDataSlotCreateInfo(MvkDecoder *d, VkPrivateDataSlotCreateInfo *s);
void mvk_put_VkDependencyInfo(MvkEncoder *e, const VkDependencyInfo *s);
void mvk_get_VkDependencyInfo(MvkDecoder *d, VkDependencyInfo *s);
void mvk_put_VkMemoryBarrier2(MvkEncoder *e, const VkMemoryBarrier2 *s);
void mvk_get_VkMemoryBarrier2(MvkDecoder *d, VkMemoryBarrier2 *s);
void mvk_put_VkBufferMemoryBarrier2(MvkEncoder *e, const VkBufferMemoryBarrier2 *s);
void mvk_get_VkBufferMemoryBarrier2(MvkDecoder *d, VkBufferMemoryBarrier2 *s);
void mvk_put_VkImageMemoryBarrier2(MvkEncoder *e, const VkImageMemoryBarrier2 *s);
void mvk_get_VkImageMemoryBarrier2(MvkDecoder *d, VkImageMemoryBarrier2 *s);
void mvk_put_VkCopyBufferInfo2(MvkEncoder *e, const VkCopyBufferInfo2 *s);
void mvk_get_VkCopyBufferInfo2(MvkDecoder *d, VkCopyBufferInfo2 *s);
void mvk_put_VkBufferCopy2(MvkEncoder *e, const VkBufferCopy2 *s);
void mvk_get_VkBufferCopy2(MvkDecoder *d, VkBufferCopy2 *s);
void mvk_put_VkCopyImageInfo2(MvkEncoder *e, const VkCopyImageInfo2 *s);
void mvk_get_VkCopyImageInfo2(MvkDecoder *d, VkCopyImageInfo2 *s);
void mvk_put_VkImageCopy2(MvkEncoder *e, const VkImageCopy2 *s);
void mvk_get_VkImageCopy2(MvkDecoder *d, VkImageCopy2 *s);
void mvk_put_VkCopyBufferToImageInfo2(MvkEncoder *e, const VkCopyBufferToImageInfo2 *s);
void mvk_get_VkCopyBufferToImageInfo2(MvkDecoder *d, VkCopyBufferToImageInfo2 *s);
void mvk_put_VkBufferImageCopy2(MvkEncoder *e, const VkBufferImageCopy2 *s);
void mvk_get_VkBufferImageCopy2(MvkDecoder *d, VkBufferImageCopy2 *s);
void mvk_put_VkCopyImageToBufferInfo2(MvkEncoder *e, const VkCopyImageToBufferInfo2 *s);
void mvk_get_VkCopyImageToBufferInfo2(MvkDecoder *d, VkCopyImageToBufferInfo2 *s);
void mvk_put_VkDeviceBufferMemoryRequirements(MvkEncoder *e, const VkDeviceBufferMemoryRequirements *s);
void mvk_get_VkDeviceBufferMemoryRequirements(MvkDecoder *d, VkDeviceBufferMemoryRequirements *s);
void mvk_put_VkDeviceImageMemoryRequirements(MvkEncoder *e, const VkDeviceImageMemoryRequirements *s);
void mvk_get_VkDeviceImageMemoryRequirements(MvkDecoder *d, VkDeviceImageMemoryRequirements *s);
void mvk_put_VkBlitImageInfo2(MvkEncoder *e, const VkBlitImageInfo2 *s);
void mvk_get_VkBlitImageInfo2(MvkDecoder *d, VkBlitImageInfo2 *s);
void mvk_put_VkImageBlit2(MvkEncoder *e, const VkImageBlit2 *s);
void mvk_get_VkImageBlit2(MvkDecoder *d, VkImageBlit2 *s);
void mvk_put_VkResolveImageInfo2(MvkEncoder *e, const VkResolveImageInfo2 *s);
void mvk_get_VkResolveImageInfo2(MvkDecoder *d, VkResolveImageInfo2 *s);
void mvk_put_VkImageResolve2(MvkEncoder *e, const VkImageResolve2 *s);
void mvk_get_VkImageResolve2(MvkDecoder *d, VkImageResolve2 *s);
void mvk_put_VkRenderingInfo(MvkEncoder *e, const VkRenderingInfo *s);
void mvk_get_VkRenderingInfo(MvkDecoder *d, VkRenderingInfo *s);
void mvk_put_VkRenderingAttachmentInfo(MvkEncoder *e, const VkRenderingAttachmentInfo *s);
void mvk_get_VkRenderingAttachmentInfo(MvkDecoder *d, VkRenderingAttachmentInfo *s);
void mvk_put_VkDeviceImageSubresourceInfo(MvkEncoder *e, const VkDeviceImageSubresourceInfo *s);
void mvk_get_VkDeviceImageSubresourceInfo(MvkDecoder *d, VkDeviceImageSubresourceInfo *s);
void mvk_put_VkImageSubresource2(MvkEncoder *e, const VkImageSubresource2 *s);
void mvk_get_VkImageSubresource2(MvkDecoder *d, VkImageSubresource2 *s);
void mvk_put_VkSubresourceLayout2(MvkEncoder *e, const VkSubresourceLayout2 *s);
void mvk_get_VkSubresourceLayout2(MvkDecoder *d, VkSubresourceLayout2 *s);
void mvk_put_VkCopyImageToImageInfo(MvkEncoder *e, const VkCopyImageToImageInfo *s);
void mvk_get_VkCopyImageToImageInfo(MvkDecoder *d, VkCopyImageToImageInfo *s);
void mvk_put_VkHostImageLayoutTransitionInfo(MvkEncoder *e, const VkHostImageLayoutTransitionInfo *s);
void mvk_get_VkHostImageLayoutTransitionInfo(MvkDecoder *d, VkHostImageLayoutTransitionInfo *s);
void mvk_put_VkBindDescriptorSetsInfo(MvkEncoder *e, const VkBindDescriptorSetsInfo *s);
void mvk_get_VkBindDescriptorSetsInfo(MvkDecoder *d, VkBindDescriptorSetsInfo *s);
void mvk_put_VkPushConstantsInfo(MvkEncoder *e, const VkPushConstantsInfo *s);
void mvk_get_VkPushConstantsInfo(MvkDecoder *d, VkPushConstantsInfo *s);
void mvk_put_VkPushDescriptorSetInfo(MvkEncoder *e, const VkPushDescriptorSetInfo *s);
void mvk_get_VkPushDescriptorSetInfo(MvkDecoder *d, VkPushDescriptorSetInfo *s);
void mvk_put_VkRenderingAreaInfo(MvkEncoder *e, const VkRenderingAreaInfo *s);
void mvk_get_VkRenderingAreaInfo(MvkDecoder *d, VkRenderingAreaInfo *s);
void mvk_put_VkRenderingAttachmentLocationInfo(MvkEncoder *e, const VkRenderingAttachmentLocationInfo *s);
void mvk_get_VkRenderingAttachmentLocationInfo(MvkDecoder *d, VkRenderingAttachmentLocationInfo *s);
void mvk_put_VkRenderingInputAttachmentIndexInfo(MvkEncoder *e, const VkRenderingInputAttachmentIndexInfo *s);
void mvk_get_VkRenderingInputAttachmentIndexInfo(MvkDecoder *d, VkRenderingInputAttachmentIndexInfo *s);
void mvk_put_VkImageViewHandleInfoNVX(MvkEncoder *e, const VkImageViewHandleInfoNVX *s);
void mvk_get_VkImageViewHandleInfoNVX(MvkDecoder *d, VkImageViewHandleInfoNVX *s);
void mvk_put_VkImageViewAddressPropertiesNVX(MvkEncoder *e, const VkImageViewAddressPropertiesNVX *s);
void mvk_get_VkImageViewAddressPropertiesNVX(MvkDecoder *d, VkImageViewAddressPropertiesNVX *s);
void mvk_put_VkExternalImageFormatPropertiesNV(MvkEncoder *e, const VkExternalImageFormatPropertiesNV *s);
void mvk_get_VkExternalImageFormatPropertiesNV(MvkDecoder *d, VkExternalImageFormatPropertiesNV *s);
void mvk_put_VkConditionalRenderingBeginInfoEXT(MvkEncoder *e, const VkConditionalRenderingBeginInfoEXT *s);
void mvk_get_VkConditionalRenderingBeginInfoEXT(MvkDecoder *d, VkConditionalRenderingBeginInfoEXT *s);
void mvk_put_VkViewportWScalingNV(MvkEncoder *e, const VkViewportWScalingNV *s);
void mvk_get_VkViewportWScalingNV(MvkDecoder *d, VkViewportWScalingNV *s);
void mvk_put_VkPerformanceCounterKHR(MvkEncoder *e, const VkPerformanceCounterKHR *s);
void mvk_get_VkPerformanceCounterKHR(MvkDecoder *d, VkPerformanceCounterKHR *s);
void mvk_put_VkPerformanceCounterDescriptionKHR(MvkEncoder *e, const VkPerformanceCounterDescriptionKHR *s);
void mvk_get_VkPerformanceCounterDescriptionKHR(MvkDecoder *d, VkPerformanceCounterDescriptionKHR *s);
void mvk_put_VkQueryPoolPerformanceCreateInfoKHR(MvkEncoder *e, const VkQueryPoolPerformanceCreateInfoKHR *s);
void mvk_get_VkQueryPoolPerformanceCreateInfoKHR(MvkDecoder *d, VkQueryPoolPerformanceCreateInfoKHR *s);
void mvk_put_VkAcquireProfilingLockInfoKHR(MvkEncoder *e, const VkAcquireProfilingLockInfoKHR *s);
void mvk_get_VkAcquireProfilingLockInfoKHR(MvkDecoder *d, VkAcquireProfilingLockInfoKHR *s);
void mvk_put_VkGpaSessionCreateInfoAMD(MvkEncoder *e, const VkGpaSessionCreateInfoAMD *s);
void mvk_get_VkGpaSessionCreateInfoAMD(MvkDecoder *d, VkGpaSessionCreateInfoAMD *s);
void mvk_put_VkGpaDeviceClockModeInfoAMD(MvkEncoder *e, const VkGpaDeviceClockModeInfoAMD *s);
void mvk_get_VkGpaDeviceClockModeInfoAMD(MvkDecoder *d, VkGpaDeviceClockModeInfoAMD *s);
void mvk_put_VkGpaDeviceGetClockInfoAMD(MvkEncoder *e, const VkGpaDeviceGetClockInfoAMD *s);
void mvk_get_VkGpaDeviceGetClockInfoAMD(MvkDecoder *d, VkGpaDeviceGetClockInfoAMD *s);
void mvk_put_VkHostAddressRangeEXT(MvkEncoder *e, const VkHostAddressRangeEXT *s);
void mvk_get_VkHostAddressRangeEXT(MvkDecoder *d, VkHostAddressRangeEXT *s);
void mvk_put_VkResourceDescriptorInfoEXT(MvkEncoder *e, const VkResourceDescriptorInfoEXT *s);
void mvk_get_VkResourceDescriptorInfoEXT(MvkDecoder *d, VkResourceDescriptorInfoEXT *s);
void mvk_put_VkResourceDescriptorDataEXT(MvkEncoder *e, const VkResourceDescriptorDataEXT *s);
void mvk_get_VkResourceDescriptorDataEXT(MvkDecoder *d, VkResourceDescriptorDataEXT *s);
void mvk_put_VkImageDescriptorInfoEXT(MvkEncoder *e, const VkImageDescriptorInfoEXT *s);
void mvk_get_VkImageDescriptorInfoEXT(MvkDecoder *d, VkImageDescriptorInfoEXT *s);
void mvk_put_VkTexelBufferDescriptorInfoEXT(MvkEncoder *e, const VkTexelBufferDescriptorInfoEXT *s);
void mvk_get_VkTexelBufferDescriptorInfoEXT(MvkDecoder *d, VkTexelBufferDescriptorInfoEXT *s);
void mvk_put_VkDeviceAddressRangeKHR(MvkEncoder *e, const VkDeviceAddressRangeKHR *s);
void mvk_get_VkDeviceAddressRangeKHR(MvkDecoder *d, VkDeviceAddressRangeKHR *s);
void mvk_put_VkTensorViewCreateInfoARM(MvkEncoder *e, const VkTensorViewCreateInfoARM *s);
void mvk_get_VkTensorViewCreateInfoARM(MvkDecoder *d, VkTensorViewCreateInfoARM *s);
void mvk_put_VkBindHeapInfoEXT(MvkEncoder *e, const VkBindHeapInfoEXT *s);
void mvk_get_VkBindHeapInfoEXT(MvkDecoder *d, VkBindHeapInfoEXT *s);
void mvk_put_VkPushDataInfoEXT(MvkEncoder *e, const VkPushDataInfoEXT *s);
void mvk_get_VkPushDataInfoEXT(MvkDecoder *d, VkPushDataInfoEXT *s);
void mvk_put_VkHostAddressRangeConstEXT(MvkEncoder *e, const VkHostAddressRangeConstEXT *s);
void mvk_get_VkHostAddressRangeConstEXT(MvkDecoder *d, VkHostAddressRangeConstEXT *s);
void mvk_put_VkSamplerCustomBorderColorCreateInfoEXT(MvkEncoder *e, const VkSamplerCustomBorderColorCreateInfoEXT *s);
void mvk_get_VkSamplerCustomBorderColorCreateInfoEXT(MvkDecoder *d, VkSamplerCustomBorderColorCreateInfoEXT *s);
void mvk_put_VkSampleLocationsInfoEXT(MvkEncoder *e, const VkSampleLocationsInfoEXT *s);
void mvk_get_VkSampleLocationsInfoEXT(MvkDecoder *d, VkSampleLocationsInfoEXT *s);
void mvk_put_VkSampleLocationEXT(MvkEncoder *e, const VkSampleLocationEXT *s);
void mvk_get_VkSampleLocationEXT(MvkDecoder *d, VkSampleLocationEXT *s);
void mvk_put_VkMultisamplePropertiesEXT(MvkEncoder *e, const VkMultisamplePropertiesEXT *s);
void mvk_get_VkMultisamplePropertiesEXT(MvkDecoder *d, VkMultisamplePropertiesEXT *s);
void mvk_put_VkAccelerationStructureCreateInfoKHR(MvkEncoder *e, const VkAccelerationStructureCreateInfoKHR *s);
void mvk_get_VkAccelerationStructureCreateInfoKHR(MvkDecoder *d, VkAccelerationStructureCreateInfoKHR *s);
void mvk_put_VkAccelerationStructureBuildGeometryInfoKHR(MvkEncoder *e, const VkAccelerationStructureBuildGeometryInfoKHR *s);
void mvk_get_VkAccelerationStructureBuildGeometryInfoKHR(MvkDecoder *d, VkAccelerationStructureBuildGeometryInfoKHR *s);
void mvk_put_VkAccelerationStructureGeometryKHR(MvkEncoder *e, const VkAccelerationStructureGeometryKHR *s);
void mvk_get_VkAccelerationStructureGeometryKHR(MvkDecoder *d, VkAccelerationStructureGeometryKHR *s);
void mvk_put_VkAccelerationStructureGeometryDataKHR(MvkEncoder *e, const VkAccelerationStructureGeometryDataKHR *s);
void mvk_get_VkAccelerationStructureGeometryDataKHR(MvkDecoder *d, VkAccelerationStructureGeometryDataKHR *s);
void mvk_put_VkAccelerationStructureGeometryTrianglesDataKHR(MvkEncoder *e, const VkAccelerationStructureGeometryTrianglesDataKHR *s);
void mvk_get_VkAccelerationStructureGeometryTrianglesDataKHR(MvkDecoder *d, VkAccelerationStructureGeometryTrianglesDataKHR *s);
void mvk_put_VkDeviceOrHostAddressConstKHR(MvkEncoder *e, const VkDeviceOrHostAddressConstKHR *s);
void mvk_get_VkDeviceOrHostAddressConstKHR(MvkDecoder *d, VkDeviceOrHostAddressConstKHR *s);
void mvk_put_VkAccelerationStructureGeometryAabbsDataKHR(MvkEncoder *e, const VkAccelerationStructureGeometryAabbsDataKHR *s);
void mvk_get_VkAccelerationStructureGeometryAabbsDataKHR(MvkDecoder *d, VkAccelerationStructureGeometryAabbsDataKHR *s);
void mvk_put_VkAccelerationStructureGeometryInstancesDataKHR(MvkEncoder *e, const VkAccelerationStructureGeometryInstancesDataKHR *s);
void mvk_get_VkAccelerationStructureGeometryInstancesDataKHR(MvkDecoder *d, VkAccelerationStructureGeometryInstancesDataKHR *s);
void mvk_put_VkDeviceOrHostAddressKHR(MvkEncoder *e, const VkDeviceOrHostAddressKHR *s);
void mvk_get_VkDeviceOrHostAddressKHR(MvkDecoder *d, VkDeviceOrHostAddressKHR *s);
void mvk_put_VkAccelerationStructureBuildRangeInfoKHR(MvkEncoder *e, const VkAccelerationStructureBuildRangeInfoKHR *s);
void mvk_get_VkAccelerationStructureBuildRangeInfoKHR(MvkDecoder *d, VkAccelerationStructureBuildRangeInfoKHR *s);
void mvk_put_VkCopyAccelerationStructureInfoKHR(MvkEncoder *e, const VkCopyAccelerationStructureInfoKHR *s);
void mvk_get_VkCopyAccelerationStructureInfoKHR(MvkDecoder *d, VkCopyAccelerationStructureInfoKHR *s);
void mvk_put_VkCopyAccelerationStructureToMemoryInfoKHR(MvkEncoder *e, const VkCopyAccelerationStructureToMemoryInfoKHR *s);
void mvk_get_VkCopyAccelerationStructureToMemoryInfoKHR(MvkDecoder *d, VkCopyAccelerationStructureToMemoryInfoKHR *s);
void mvk_put_VkCopyMemoryToAccelerationStructureInfoKHR(MvkEncoder *e, const VkCopyMemoryToAccelerationStructureInfoKHR *s);
void mvk_get_VkCopyMemoryToAccelerationStructureInfoKHR(MvkDecoder *d, VkCopyMemoryToAccelerationStructureInfoKHR *s);
void mvk_put_VkAccelerationStructureDeviceAddressInfoKHR(MvkEncoder *e, const VkAccelerationStructureDeviceAddressInfoKHR *s);
void mvk_get_VkAccelerationStructureDeviceAddressInfoKHR(MvkDecoder *d, VkAccelerationStructureDeviceAddressInfoKHR *s);
void mvk_put_VkAccelerationStructureVersionInfoKHR(MvkEncoder *e, const VkAccelerationStructureVersionInfoKHR *s);
void mvk_get_VkAccelerationStructureVersionInfoKHR(MvkDecoder *d, VkAccelerationStructureVersionInfoKHR *s);
void mvk_put_VkAccelerationStructureBuildSizesInfoKHR(MvkEncoder *e, const VkAccelerationStructureBuildSizesInfoKHR *s);
void mvk_get_VkAccelerationStructureBuildSizesInfoKHR(MvkDecoder *d, VkAccelerationStructureBuildSizesInfoKHR *s);
void mvk_put_VkStridedDeviceAddressRegionKHR(MvkEncoder *e, const VkStridedDeviceAddressRegionKHR *s);
void mvk_get_VkStridedDeviceAddressRegionKHR(MvkDecoder *d, VkStridedDeviceAddressRegionKHR *s);
void mvk_put_VkRayTracingPipelineCreateInfoKHR(MvkEncoder *e, const VkRayTracingPipelineCreateInfoKHR *s);
void mvk_get_VkRayTracingPipelineCreateInfoKHR(MvkDecoder *d, VkRayTracingPipelineCreateInfoKHR *s);
void mvk_put_VkRayTracingShaderGroupCreateInfoKHR(MvkEncoder *e, const VkRayTracingShaderGroupCreateInfoKHR *s);
void mvk_get_VkRayTracingShaderGroupCreateInfoKHR(MvkDecoder *d, VkRayTracingShaderGroupCreateInfoKHR *s);
void mvk_put_VkPipelineLibraryCreateInfoKHR(MvkEncoder *e, const VkPipelineLibraryCreateInfoKHR *s);
void mvk_get_VkPipelineLibraryCreateInfoKHR(MvkDecoder *d, VkPipelineLibraryCreateInfoKHR *s);
void mvk_put_VkRayTracingPipelineInterfaceCreateInfoKHR(MvkEncoder *e, const VkRayTracingPipelineInterfaceCreateInfoKHR *s);
void mvk_get_VkRayTracingPipelineInterfaceCreateInfoKHR(MvkDecoder *d, VkRayTracingPipelineInterfaceCreateInfoKHR *s);
void mvk_put_VkImageDrmFormatModifierPropertiesEXT(MvkEncoder *e, const VkImageDrmFormatModifierPropertiesEXT *s);
void mvk_get_VkImageDrmFormatModifierPropertiesEXT(MvkDecoder *d, VkImageDrmFormatModifierPropertiesEXT *s);
void mvk_put_VkValidationCacheCreateInfoEXT(MvkEncoder *e, const VkValidationCacheCreateInfoEXT *s);
void mvk_get_VkValidationCacheCreateInfoEXT(MvkDecoder *d, VkValidationCacheCreateInfoEXT *s);
void mvk_put_VkShadingRatePaletteNV(MvkEncoder *e, const VkShadingRatePaletteNV *s);
void mvk_get_VkShadingRatePaletteNV(MvkDecoder *d, VkShadingRatePaletteNV *s);
void mvk_put_VkCoarseSampleOrderCustomNV(MvkEncoder *e, const VkCoarseSampleOrderCustomNV *s);
void mvk_get_VkCoarseSampleOrderCustomNV(MvkDecoder *d, VkCoarseSampleOrderCustomNV *s);
void mvk_put_VkCoarseSampleLocationNV(MvkEncoder *e, const VkCoarseSampleLocationNV *s);
void mvk_get_VkCoarseSampleLocationNV(MvkDecoder *d, VkCoarseSampleLocationNV *s);
void mvk_put_VkAccelerationStructureCreateInfoNV(MvkEncoder *e, const VkAccelerationStructureCreateInfoNV *s);
void mvk_get_VkAccelerationStructureCreateInfoNV(MvkDecoder *d, VkAccelerationStructureCreateInfoNV *s);
void mvk_put_VkAccelerationStructureInfoNV(MvkEncoder *e, const VkAccelerationStructureInfoNV *s);
void mvk_get_VkAccelerationStructureInfoNV(MvkDecoder *d, VkAccelerationStructureInfoNV *s);
void mvk_put_VkGeometryNV(MvkEncoder *e, const VkGeometryNV *s);
void mvk_get_VkGeometryNV(MvkDecoder *d, VkGeometryNV *s);
void mvk_put_VkGeometryDataNV(MvkEncoder *e, const VkGeometryDataNV *s);
void mvk_get_VkGeometryDataNV(MvkDecoder *d, VkGeometryDataNV *s);
void mvk_put_VkGeometryTrianglesNV(MvkEncoder *e, const VkGeometryTrianglesNV *s);
void mvk_get_VkGeometryTrianglesNV(MvkDecoder *d, VkGeometryTrianglesNV *s);
void mvk_put_VkGeometryAABBNV(MvkEncoder *e, const VkGeometryAABBNV *s);
void mvk_get_VkGeometryAABBNV(MvkDecoder *d, VkGeometryAABBNV *s);
void mvk_put_VkAccelerationStructureMemoryRequirementsInfoNV(MvkEncoder *e, const VkAccelerationStructureMemoryRequirementsInfoNV *s);
void mvk_get_VkAccelerationStructureMemoryRequirementsInfoNV(MvkDecoder *d, VkAccelerationStructureMemoryRequirementsInfoNV *s);
void mvk_put_VkBindAccelerationStructureMemoryInfoNV(MvkEncoder *e, const VkBindAccelerationStructureMemoryInfoNV *s);
void mvk_get_VkBindAccelerationStructureMemoryInfoNV(MvkDecoder *d, VkBindAccelerationStructureMemoryInfoNV *s);
void mvk_put_VkRayTracingPipelineCreateInfoNV(MvkEncoder *e, const VkRayTracingPipelineCreateInfoNV *s);
void mvk_get_VkRayTracingPipelineCreateInfoNV(MvkDecoder *d, VkRayTracingPipelineCreateInfoNV *s);
void mvk_put_VkRayTracingShaderGroupCreateInfoNV(MvkEncoder *e, const VkRayTracingShaderGroupCreateInfoNV *s);
void mvk_get_VkRayTracingShaderGroupCreateInfoNV(MvkDecoder *d, VkRayTracingShaderGroupCreateInfoNV *s);
void mvk_put_VkPerformanceConfigurationAcquireInfoINTEL(MvkEncoder *e, const VkPerformanceConfigurationAcquireInfoINTEL *s);
void mvk_get_VkPerformanceConfigurationAcquireInfoINTEL(MvkDecoder *d, VkPerformanceConfigurationAcquireInfoINTEL *s);
void mvk_put_VkPerformanceValueINTEL(MvkEncoder *e, const VkPerformanceValueINTEL *s);
void mvk_get_VkPerformanceValueINTEL(MvkDecoder *d, VkPerformanceValueINTEL *s);
void mvk_put_VkPerformanceValueDataINTEL(MvkEncoder *e, const VkPerformanceValueDataINTEL *s);
void mvk_get_VkPerformanceValueDataINTEL(MvkDecoder *d, VkPerformanceValueDataINTEL *s);
void mvk_put_VkPhysicalDeviceFragmentShadingRateKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRateKHR *s);
void mvk_get_VkPhysicalDeviceFragmentShadingRateKHR(MvkDecoder *d, VkPhysicalDeviceFragmentShadingRateKHR *s);
void mvk_put_VkCooperativeMatrixPropertiesNV(MvkEncoder *e, const VkCooperativeMatrixPropertiesNV *s);
void mvk_get_VkCooperativeMatrixPropertiesNV(MvkDecoder *d, VkCooperativeMatrixPropertiesNV *s);
void mvk_put_VkFramebufferMixedSamplesCombinationNV(MvkEncoder *e, const VkFramebufferMixedSamplesCombinationNV *s);
void mvk_get_VkFramebufferMixedSamplesCombinationNV(MvkDecoder *d, VkFramebufferMixedSamplesCombinationNV *s);
void mvk_put_VkPipelineInfoKHR(MvkEncoder *e, const VkPipelineInfoKHR *s);
void mvk_get_VkPipelineInfoKHR(MvkDecoder *d, VkPipelineInfoKHR *s);
void mvk_put_VkPipelineExecutablePropertiesKHR(MvkEncoder *e, const VkPipelineExecutablePropertiesKHR *s);
void mvk_get_VkPipelineExecutablePropertiesKHR(MvkDecoder *d, VkPipelineExecutablePropertiesKHR *s);
void mvk_put_VkPipelineExecutableInfoKHR(MvkEncoder *e, const VkPipelineExecutableInfoKHR *s);
void mvk_get_VkPipelineExecutableInfoKHR(MvkDecoder *d, VkPipelineExecutableInfoKHR *s);
void mvk_put_VkPipelineExecutableStatisticKHR(MvkEncoder *e, const VkPipelineExecutableStatisticKHR *s);
void mvk_get_VkPipelineExecutableStatisticKHR(MvkDecoder *d, VkPipelineExecutableStatisticKHR *s);
void mvk_put_VkPipelineExecutableStatisticValueKHR(MvkEncoder *e, const VkPipelineExecutableStatisticValueKHR *s);
void mvk_get_VkPipelineExecutableStatisticValueKHR(MvkDecoder *d, VkPipelineExecutableStatisticValueKHR *s);
void mvk_put_VkPipelineExecutableInternalRepresentationKHR(MvkEncoder *e, const VkPipelineExecutableInternalRepresentationKHR *s);
void mvk_get_VkPipelineExecutableInternalRepresentationKHR(MvkDecoder *d, VkPipelineExecutableInternalRepresentationKHR *s);
void mvk_put_VkGeneratedCommandsMemoryRequirementsInfoNV(MvkEncoder *e, const VkGeneratedCommandsMemoryRequirementsInfoNV *s);
void mvk_get_VkGeneratedCommandsMemoryRequirementsInfoNV(MvkDecoder *d, VkGeneratedCommandsMemoryRequirementsInfoNV *s);
void mvk_put_VkGeneratedCommandsInfoNV(MvkEncoder *e, const VkGeneratedCommandsInfoNV *s);
void mvk_get_VkGeneratedCommandsInfoNV(MvkDecoder *d, VkGeneratedCommandsInfoNV *s);
void mvk_put_VkIndirectCommandsStreamNV(MvkEncoder *e, const VkIndirectCommandsStreamNV *s);
void mvk_get_VkIndirectCommandsStreamNV(MvkDecoder *d, VkIndirectCommandsStreamNV *s);
void mvk_put_VkIndirectCommandsLayoutCreateInfoNV(MvkEncoder *e, const VkIndirectCommandsLayoutCreateInfoNV *s);
void mvk_get_VkIndirectCommandsLayoutCreateInfoNV(MvkDecoder *d, VkIndirectCommandsLayoutCreateInfoNV *s);
void mvk_put_VkIndirectCommandsLayoutTokenNV(MvkEncoder *e, const VkIndirectCommandsLayoutTokenNV *s);
void mvk_get_VkIndirectCommandsLayoutTokenNV(MvkDecoder *d, VkIndirectCommandsLayoutTokenNV *s);
void mvk_put_VkDepthBiasInfoEXT(MvkEncoder *e, const VkDepthBiasInfoEXT *s);
void mvk_get_VkDepthBiasInfoEXT(MvkDecoder *d, VkDepthBiasInfoEXT *s);
void mvk_put_VkPerfHintInfoQCOM(MvkEncoder *e, const VkPerfHintInfoQCOM *s);
void mvk_get_VkPerfHintInfoQCOM(MvkDecoder *d, VkPerfHintInfoQCOM *s);
void mvk_put_VkDescriptorBufferBindingInfoEXT(MvkEncoder *e, const VkDescriptorBufferBindingInfoEXT *s);
void mvk_get_VkDescriptorBufferBindingInfoEXT(MvkDecoder *d, VkDescriptorBufferBindingInfoEXT *s);
void mvk_put_VkBindIndexBuffer3InfoKHR(MvkEncoder *e, const VkBindIndexBuffer3InfoKHR *s);
void mvk_get_VkBindIndexBuffer3InfoKHR(MvkDecoder *d, VkBindIndexBuffer3InfoKHR *s);
void mvk_put_VkBindVertexBuffer3InfoKHR(MvkEncoder *e, const VkBindVertexBuffer3InfoKHR *s);
void mvk_get_VkBindVertexBuffer3InfoKHR(MvkDecoder *d, VkBindVertexBuffer3InfoKHR *s);
void mvk_put_VkStridedDeviceAddressRangeKHR(MvkEncoder *e, const VkStridedDeviceAddressRangeKHR *s);
void mvk_get_VkStridedDeviceAddressRangeKHR(MvkDecoder *d, VkStridedDeviceAddressRangeKHR *s);
void mvk_put_VkDrawIndirect2InfoKHR(MvkEncoder *e, const VkDrawIndirect2InfoKHR *s);
void mvk_get_VkDrawIndirect2InfoKHR(MvkDecoder *d, VkDrawIndirect2InfoKHR *s);
void mvk_put_VkDispatchIndirect2InfoKHR(MvkEncoder *e, const VkDispatchIndirect2InfoKHR *s);
void mvk_get_VkDispatchIndirect2InfoKHR(MvkDecoder *d, VkDispatchIndirect2InfoKHR *s);
void mvk_put_VkCopyDeviceMemoryInfoKHR(MvkEncoder *e, const VkCopyDeviceMemoryInfoKHR *s);
void mvk_get_VkCopyDeviceMemoryInfoKHR(MvkDecoder *d, VkCopyDeviceMemoryInfoKHR *s);
void mvk_put_VkDeviceMemoryCopyKHR(MvkEncoder *e, const VkDeviceMemoryCopyKHR *s);
void mvk_get_VkDeviceMemoryCopyKHR(MvkDecoder *d, VkDeviceMemoryCopyKHR *s);
void mvk_put_VkCopyDeviceMemoryImageInfoKHR(MvkEncoder *e, const VkCopyDeviceMemoryImageInfoKHR *s);
void mvk_get_VkCopyDeviceMemoryImageInfoKHR(MvkDecoder *d, VkCopyDeviceMemoryImageInfoKHR *s);
void mvk_put_VkDeviceMemoryImageCopyKHR(MvkEncoder *e, const VkDeviceMemoryImageCopyKHR *s);
void mvk_get_VkDeviceMemoryImageCopyKHR(MvkDecoder *d, VkDeviceMemoryImageCopyKHR *s);
void mvk_put_VkDrawIndirectCount2InfoKHR(MvkEncoder *e, const VkDrawIndirectCount2InfoKHR *s);
void mvk_get_VkDrawIndirectCount2InfoKHR(MvkDecoder *d, VkDrawIndirectCount2InfoKHR *s);
void mvk_put_VkConditionalRenderingBeginInfo2EXT(MvkEncoder *e, const VkConditionalRenderingBeginInfo2EXT *s);
void mvk_get_VkConditionalRenderingBeginInfo2EXT(MvkDecoder *d, VkConditionalRenderingBeginInfo2EXT *s);
void mvk_put_VkBindTransformFeedbackBuffer2InfoEXT(MvkEncoder *e, const VkBindTransformFeedbackBuffer2InfoEXT *s);
void mvk_get_VkBindTransformFeedbackBuffer2InfoEXT(MvkDecoder *d, VkBindTransformFeedbackBuffer2InfoEXT *s);
void mvk_put_VkMemoryMarkerInfoAMD(MvkEncoder *e, const VkMemoryMarkerInfoAMD *s);
void mvk_get_VkMemoryMarkerInfoAMD(MvkDecoder *d, VkMemoryMarkerInfoAMD *s);
void mvk_put_VkAccelerationStructureCreateInfo2KHR(MvkEncoder *e, const VkAccelerationStructureCreateInfo2KHR *s);
void mvk_get_VkAccelerationStructureCreateInfo2KHR(MvkDecoder *d, VkAccelerationStructureCreateInfo2KHR *s);
void mvk_put_VkVertexInputBindingDescription2EXT(MvkEncoder *e, const VkVertexInputBindingDescription2EXT *s);
void mvk_get_VkVertexInputBindingDescription2EXT(MvkDecoder *d, VkVertexInputBindingDescription2EXT *s);
void mvk_put_VkVertexInputAttributeDescription2EXT(MvkEncoder *e, const VkVertexInputAttributeDescription2EXT *s);
void mvk_get_VkVertexInputAttributeDescription2EXT(MvkDecoder *d, VkVertexInputAttributeDescription2EXT *s);
void mvk_put_VkBaseOutStructure(MvkEncoder *e, const VkBaseOutStructure *s);
void mvk_get_VkBaseOutStructure(MvkDecoder *d, VkBaseOutStructure *s);
void mvk_put_VkMultiDrawInfoEXT(MvkEncoder *e, const VkMultiDrawInfoEXT *s);
void mvk_get_VkMultiDrawInfoEXT(MvkDecoder *d, VkMultiDrawInfoEXT *s);
void mvk_put_VkMultiDrawIndexedInfoEXT(MvkEncoder *e, const VkMultiDrawIndexedInfoEXT *s);
void mvk_get_VkMultiDrawIndexedInfoEXT(MvkDecoder *d, VkMultiDrawIndexedInfoEXT *s);
void mvk_put_VkMicromapCreateInfoEXT(MvkEncoder *e, const VkMicromapCreateInfoEXT *s);
void mvk_get_VkMicromapCreateInfoEXT(MvkDecoder *d, VkMicromapCreateInfoEXT *s);
void mvk_put_VkMicromapBuildInfoEXT(MvkEncoder *e, const VkMicromapBuildInfoEXT *s);
void mvk_get_VkMicromapBuildInfoEXT(MvkDecoder *d, VkMicromapBuildInfoEXT *s);
void mvk_put_VkMicromapUsageEXT(MvkEncoder *e, const VkMicromapUsageEXT *s);
void mvk_get_VkMicromapUsageEXT(MvkDecoder *d, VkMicromapUsageEXT *s);
void mvk_put_VkCopyMicromapInfoEXT(MvkEncoder *e, const VkCopyMicromapInfoEXT *s);
void mvk_get_VkCopyMicromapInfoEXT(MvkDecoder *d, VkCopyMicromapInfoEXT *s);
void mvk_put_VkCopyMicromapToMemoryInfoEXT(MvkEncoder *e, const VkCopyMicromapToMemoryInfoEXT *s);
void mvk_get_VkCopyMicromapToMemoryInfoEXT(MvkDecoder *d, VkCopyMicromapToMemoryInfoEXT *s);
void mvk_put_VkCopyMemoryToMicromapInfoEXT(MvkEncoder *e, const VkCopyMemoryToMicromapInfoEXT *s);
void mvk_get_VkCopyMemoryToMicromapInfoEXT(MvkDecoder *d, VkCopyMemoryToMicromapInfoEXT *s);
void mvk_put_VkMicromapVersionInfoEXT(MvkEncoder *e, const VkMicromapVersionInfoEXT *s);
void mvk_get_VkMicromapVersionInfoEXT(MvkDecoder *d, VkMicromapVersionInfoEXT *s);
void mvk_put_VkMicromapBuildSizesInfoEXT(MvkEncoder *e, const VkMicromapBuildSizesInfoEXT *s);
void mvk_get_VkMicromapBuildSizesInfoEXT(MvkDecoder *d, VkMicromapBuildSizesInfoEXT *s);
void mvk_put_VkDispatchParametersARM(MvkEncoder *e, const VkDispatchParametersARM *s);
void mvk_get_VkDispatchParametersARM(MvkDecoder *d, VkDispatchParametersARM *s);
void mvk_put_VkDescriptorSetBindingReferenceVALVE(MvkEncoder *e, const VkDescriptorSetBindingReferenceVALVE *s);
void mvk_get_VkDescriptorSetBindingReferenceVALVE(MvkDecoder *d, VkDescriptorSetBindingReferenceVALVE *s);
void mvk_put_VkDescriptorSetLayoutHostMappingInfoVALVE(MvkEncoder *e, const VkDescriptorSetLayoutHostMappingInfoVALVE *s);
void mvk_get_VkDescriptorSetLayoutHostMappingInfoVALVE(MvkDecoder *d, VkDescriptorSetLayoutHostMappingInfoVALVE *s);
void mvk_put_VkDecompressMemoryRegionNV(MvkEncoder *e, const VkDecompressMemoryRegionNV *s);
void mvk_get_VkDecompressMemoryRegionNV(MvkDecoder *d, VkDecompressMemoryRegionNV *s);
void mvk_put_VkPipelineIndirectDeviceAddressInfoNV(MvkEncoder *e, const VkPipelineIndirectDeviceAddressInfoNV *s);
void mvk_get_VkPipelineIndirectDeviceAddressInfoNV(MvkDecoder *d, VkPipelineIndirectDeviceAddressInfoNV *s);
void mvk_put_VkColorBlendEquationEXT(MvkEncoder *e, const VkColorBlendEquationEXT *s);
void mvk_get_VkColorBlendEquationEXT(MvkDecoder *d, VkColorBlendEquationEXT *s);
void mvk_put_VkColorBlendAdvancedEXT(MvkEncoder *e, const VkColorBlendAdvancedEXT *s);
void mvk_get_VkColorBlendAdvancedEXT(MvkDecoder *d, VkColorBlendAdvancedEXT *s);
void mvk_put_VkViewportSwizzleNV(MvkEncoder *e, const VkViewportSwizzleNV *s);
void mvk_get_VkViewportSwizzleNV(MvkDecoder *d, VkViewportSwizzleNV *s);
void mvk_put_VkShaderModuleIdentifierEXT(MvkEncoder *e, const VkShaderModuleIdentifierEXT *s);
void mvk_get_VkShaderModuleIdentifierEXT(MvkDecoder *d, VkShaderModuleIdentifierEXT *s);
void mvk_put_VkOpticalFlowImageFormatInfoNV(MvkEncoder *e, const VkOpticalFlowImageFormatInfoNV *s);
void mvk_get_VkOpticalFlowImageFormatInfoNV(MvkDecoder *d, VkOpticalFlowImageFormatInfoNV *s);
void mvk_put_VkOpticalFlowImageFormatPropertiesNV(MvkEncoder *e, const VkOpticalFlowImageFormatPropertiesNV *s);
void mvk_get_VkOpticalFlowImageFormatPropertiesNV(MvkDecoder *d, VkOpticalFlowImageFormatPropertiesNV *s);
void mvk_put_VkOpticalFlowSessionCreateInfoNV(MvkEncoder *e, const VkOpticalFlowSessionCreateInfoNV *s);
void mvk_get_VkOpticalFlowSessionCreateInfoNV(MvkDecoder *d, VkOpticalFlowSessionCreateInfoNV *s);
void mvk_put_VkOpticalFlowExecuteInfoNV(MvkEncoder *e, const VkOpticalFlowExecuteInfoNV *s);
void mvk_get_VkOpticalFlowExecuteInfoNV(MvkDecoder *d, VkOpticalFlowExecuteInfoNV *s);
void mvk_put_VkShaderCreateInfoEXT(MvkEncoder *e, const VkShaderCreateInfoEXT *s);
void mvk_get_VkShaderCreateInfoEXT(MvkDecoder *d, VkShaderCreateInfoEXT *s);
void mvk_put_VkDepthClampRangeEXT(MvkEncoder *e, const VkDepthClampRangeEXT *s);
void mvk_get_VkDepthClampRangeEXT(MvkDecoder *d, VkDepthClampRangeEXT *s);
void mvk_put_VkCooperativeVectorPropertiesNV(MvkEncoder *e, const VkCooperativeVectorPropertiesNV *s);
void mvk_get_VkCooperativeVectorPropertiesNV(MvkDecoder *d, VkCooperativeVectorPropertiesNV *s);
void mvk_put_VkConvertCooperativeVectorMatrixInfoNV(MvkEncoder *e, const VkConvertCooperativeVectorMatrixInfoNV *s);
void mvk_get_VkConvertCooperativeVectorMatrixInfoNV(MvkDecoder *d, VkConvertCooperativeVectorMatrixInfoNV *s);
void mvk_put_VkCooperativeMatrixPropertiesKHR(MvkEncoder *e, const VkCooperativeMatrixPropertiesKHR *s);
void mvk_get_VkCooperativeMatrixPropertiesKHR(MvkDecoder *d, VkCooperativeMatrixPropertiesKHR *s);
void mvk_put_VkCalibratedTimestampInfoKHR(MvkEncoder *e, const VkCalibratedTimestampInfoKHR *s);
void mvk_get_VkCalibratedTimestampInfoKHR(MvkDecoder *d, VkCalibratedTimestampInfoKHR *s);
void mvk_put_VkSetDescriptorBufferOffsetsInfoEXT(MvkEncoder *e, const VkSetDescriptorBufferOffsetsInfoEXT *s);
void mvk_get_VkSetDescriptorBufferOffsetsInfoEXT(MvkDecoder *d, VkSetDescriptorBufferOffsetsInfoEXT *s);
void mvk_put_VkBindDescriptorBufferEmbeddedSamplersInfoEXT(MvkEncoder *e, const VkBindDescriptorBufferEmbeddedSamplersInfoEXT *s);
void mvk_get_VkBindDescriptorBufferEmbeddedSamplersInfoEXT(MvkDecoder *d, VkBindDescriptorBufferEmbeddedSamplersInfoEXT *s);
void mvk_put_VkCopyMemoryIndirectInfoKHR(MvkEncoder *e, const VkCopyMemoryIndirectInfoKHR *s);
void mvk_get_VkCopyMemoryIndirectInfoKHR(MvkDecoder *d, VkCopyMemoryIndirectInfoKHR *s);
void mvk_put_VkCopyMemoryToImageIndirectInfoKHR(MvkEncoder *e, const VkCopyMemoryToImageIndirectInfoKHR *s);
void mvk_get_VkCopyMemoryToImageIndirectInfoKHR(MvkDecoder *d, VkCopyMemoryToImageIndirectInfoKHR *s);
void mvk_put_VkDecompressMemoryInfoEXT(MvkEncoder *e, const VkDecompressMemoryInfoEXT *s);
void mvk_get_VkDecompressMemoryInfoEXT(MvkDecoder *d, VkDecompressMemoryInfoEXT *s);
void mvk_put_VkDecompressMemoryRegionEXT(MvkEncoder *e, const VkDecompressMemoryRegionEXT *s);
void mvk_get_VkDecompressMemoryRegionEXT(MvkDecoder *d, VkDecompressMemoryRegionEXT *s);
void mvk_put_VkClusterAccelerationStructureInputInfoNV(MvkEncoder *e, const VkClusterAccelerationStructureInputInfoNV *s);
void mvk_get_VkClusterAccelerationStructureInputInfoNV(MvkDecoder *d, VkClusterAccelerationStructureInputInfoNV *s);
void mvk_put_VkClusterAccelerationStructureOpInputNV(MvkEncoder *e, const VkClusterAccelerationStructureOpInputNV *s);
void mvk_get_VkClusterAccelerationStructureOpInputNV(MvkDecoder *d, VkClusterAccelerationStructureOpInputNV *s);
void mvk_put_VkClusterAccelerationStructureClustersBottomLevelInputNV(MvkEncoder *e, const VkClusterAccelerationStructureClustersBottomLevelInputNV *s);
void mvk_get_VkClusterAccelerationStructureClustersBottomLevelInputNV(MvkDecoder *d, VkClusterAccelerationStructureClustersBottomLevelInputNV *s);
void mvk_put_VkClusterAccelerationStructureTriangleClusterInputNV(MvkEncoder *e, const VkClusterAccelerationStructureTriangleClusterInputNV *s);
void mvk_get_VkClusterAccelerationStructureTriangleClusterInputNV(MvkDecoder *d, VkClusterAccelerationStructureTriangleClusterInputNV *s);
void mvk_put_VkClusterAccelerationStructureMoveObjectsInputNV(MvkEncoder *e, const VkClusterAccelerationStructureMoveObjectsInputNV *s);
void mvk_get_VkClusterAccelerationStructureMoveObjectsInputNV(MvkDecoder *d, VkClusterAccelerationStructureMoveObjectsInputNV *s);
void mvk_put_VkClusterAccelerationStructureCommandsInfoNV(MvkEncoder *e, const VkClusterAccelerationStructureCommandsInfoNV *s);
void mvk_get_VkClusterAccelerationStructureCommandsInfoNV(MvkDecoder *d, VkClusterAccelerationStructureCommandsInfoNV *s);
void mvk_put_VkPartitionedAccelerationStructureInstancesInputNV(MvkEncoder *e, const VkPartitionedAccelerationStructureInstancesInputNV *s);
void mvk_get_VkPartitionedAccelerationStructureInstancesInputNV(MvkDecoder *d, VkPartitionedAccelerationStructureInstancesInputNV *s);
void mvk_put_VkBuildPartitionedAccelerationStructureInfoNV(MvkEncoder *e, const VkBuildPartitionedAccelerationStructureInfoNV *s);
void mvk_get_VkBuildPartitionedAccelerationStructureInfoNV(MvkDecoder *d, VkBuildPartitionedAccelerationStructureInfoNV *s);
void mvk_put_VkGeneratedCommandsMemoryRequirementsInfoEXT(MvkEncoder *e, const VkGeneratedCommandsMemoryRequirementsInfoEXT *s);
void mvk_get_VkGeneratedCommandsMemoryRequirementsInfoEXT(MvkDecoder *d, VkGeneratedCommandsMemoryRequirementsInfoEXT *s);
void mvk_put_VkGeneratedCommandsInfoEXT(MvkEncoder *e, const VkGeneratedCommandsInfoEXT *s);
void mvk_get_VkGeneratedCommandsInfoEXT(MvkDecoder *d, VkGeneratedCommandsInfoEXT *s);
void mvk_put_VkIndirectCommandsLayoutCreateInfoEXT(MvkEncoder *e, const VkIndirectCommandsLayoutCreateInfoEXT *s);
void mvk_get_VkIndirectCommandsLayoutCreateInfoEXT(MvkDecoder *d, VkIndirectCommandsLayoutCreateInfoEXT *s);
void mvk_put_VkIndirectCommandsLayoutTokenEXT(MvkEncoder *e, const VkIndirectCommandsLayoutTokenEXT *s);
void mvk_get_VkIndirectCommandsLayoutTokenEXT(MvkDecoder *d, VkIndirectCommandsLayoutTokenEXT *s);
void mvk_put_VkIndirectCommandsTokenDataEXT(MvkEncoder *e, const VkIndirectCommandsTokenDataEXT *s);
void mvk_get_VkIndirectCommandsTokenDataEXT(MvkDecoder *d, VkIndirectCommandsTokenDataEXT *s);
void mvk_put_VkIndirectCommandsPushConstantTokenEXT(MvkEncoder *e, const VkIndirectCommandsPushConstantTokenEXT *s);
void mvk_get_VkIndirectCommandsPushConstantTokenEXT(MvkDecoder *d, VkIndirectCommandsPushConstantTokenEXT *s);
void mvk_put_VkIndirectCommandsVertexBufferTokenEXT(MvkEncoder *e, const VkIndirectCommandsVertexBufferTokenEXT *s);
void mvk_get_VkIndirectCommandsVertexBufferTokenEXT(MvkDecoder *d, VkIndirectCommandsVertexBufferTokenEXT *s);
void mvk_put_VkIndirectCommandsIndexBufferTokenEXT(MvkEncoder *e, const VkIndirectCommandsIndexBufferTokenEXT *s);
void mvk_get_VkIndirectCommandsIndexBufferTokenEXT(MvkDecoder *d, VkIndirectCommandsIndexBufferTokenEXT *s);
void mvk_put_VkIndirectCommandsExecutionSetTokenEXT(MvkEncoder *e, const VkIndirectCommandsExecutionSetTokenEXT *s);
void mvk_get_VkIndirectCommandsExecutionSetTokenEXT(MvkDecoder *d, VkIndirectCommandsExecutionSetTokenEXT *s);
void mvk_put_VkIndirectExecutionSetCreateInfoEXT(MvkEncoder *e, const VkIndirectExecutionSetCreateInfoEXT *s);
void mvk_get_VkIndirectExecutionSetCreateInfoEXT(MvkDecoder *d, VkIndirectExecutionSetCreateInfoEXT *s);
void mvk_put_VkIndirectExecutionSetInfoEXT(MvkEncoder *e, const VkIndirectExecutionSetInfoEXT *s);
void mvk_get_VkIndirectExecutionSetInfoEXT(MvkDecoder *d, VkIndirectExecutionSetInfoEXT *s);
void mvk_put_VkIndirectExecutionSetPipelineInfoEXT(MvkEncoder *e, const VkIndirectExecutionSetPipelineInfoEXT *s);
void mvk_get_VkIndirectExecutionSetPipelineInfoEXT(MvkDecoder *d, VkIndirectExecutionSetPipelineInfoEXT *s);
void mvk_put_VkIndirectExecutionSetShaderInfoEXT(MvkEncoder *e, const VkIndirectExecutionSetShaderInfoEXT *s);
void mvk_get_VkIndirectExecutionSetShaderInfoEXT(MvkDecoder *d, VkIndirectExecutionSetShaderInfoEXT *s);
void mvk_put_VkIndirectExecutionSetShaderLayoutInfoEXT(MvkEncoder *e, const VkIndirectExecutionSetShaderLayoutInfoEXT *s);
void mvk_get_VkIndirectExecutionSetShaderLayoutInfoEXT(MvkDecoder *d, VkIndirectExecutionSetShaderLayoutInfoEXT *s);
void mvk_put_VkWriteIndirectExecutionSetPipelineEXT(MvkEncoder *e, const VkWriteIndirectExecutionSetPipelineEXT *s);
void mvk_get_VkWriteIndirectExecutionSetPipelineEXT(MvkDecoder *d, VkWriteIndirectExecutionSetPipelineEXT *s);
void mvk_put_VkWriteIndirectExecutionSetShaderEXT(MvkEncoder *e, const VkWriteIndirectExecutionSetShaderEXT *s);
void mvk_get_VkWriteIndirectExecutionSetShaderEXT(MvkDecoder *d, VkWriteIndirectExecutionSetShaderEXT *s);
void mvk_put_VkDeviceFaultInfoKHR(MvkEncoder *e, const VkDeviceFaultInfoKHR *s);
void mvk_get_VkDeviceFaultInfoKHR(MvkDecoder *d, VkDeviceFaultInfoKHR *s);
void mvk_put_VkDeviceFaultAddressInfoKHR(MvkEncoder *e, const VkDeviceFaultAddressInfoKHR *s);
void mvk_get_VkDeviceFaultAddressInfoKHR(MvkDecoder *d, VkDeviceFaultAddressInfoKHR *s);
void mvk_put_VkDeviceFaultVendorInfoKHR(MvkEncoder *e, const VkDeviceFaultVendorInfoKHR *s);
void mvk_get_VkDeviceFaultVendorInfoKHR(MvkDecoder *d, VkDeviceFaultVendorInfoKHR *s);
void mvk_put_VkDeviceFaultDebugInfoKHR(MvkEncoder *e, const VkDeviceFaultDebugInfoKHR *s);
void mvk_get_VkDeviceFaultDebugInfoKHR(MvkDecoder *d, VkDeviceFaultDebugInfoKHR *s);
void mvk_put_VkCooperativeMatrixFlexibleDimensionsPropertiesNV(MvkEncoder *e, const VkCooperativeMatrixFlexibleDimensionsPropertiesNV *s);
void mvk_get_VkCooperativeMatrixFlexibleDimensionsPropertiesNV(MvkDecoder *d, VkCooperativeMatrixFlexibleDimensionsPropertiesNV *s);
void mvk_put_VkPerformanceCounterARM(MvkEncoder *e, const VkPerformanceCounterARM *s);
void mvk_get_VkPerformanceCounterARM(MvkDecoder *d, VkPerformanceCounterARM *s);
void mvk_put_VkPerformanceCounterDescriptionARM(MvkEncoder *e, const VkPerformanceCounterDescriptionARM *s);
void mvk_get_VkPerformanceCounterDescriptionARM(MvkDecoder *d, VkPerformanceCounterDescriptionARM *s);
void mvk_put_VkShaderInstrumentationMetricDescriptionARM(MvkEncoder *e, const VkShaderInstrumentationMetricDescriptionARM *s);
void mvk_get_VkShaderInstrumentationMetricDescriptionARM(MvkDecoder *d, VkShaderInstrumentationMetricDescriptionARM *s);
void mvk_put_VkShaderInstrumentationCreateInfoARM(MvkEncoder *e, const VkShaderInstrumentationCreateInfoARM *s);
void mvk_get_VkShaderInstrumentationCreateInfoARM(MvkDecoder *d, VkShaderInstrumentationCreateInfoARM *s);
void mvk_put_VkBeginCustomResolveInfoEXT(MvkEncoder *e, const VkBeginCustomResolveInfoEXT *s);
void mvk_get_VkBeginCustomResolveInfoEXT(MvkDecoder *d, VkBeginCustomResolveInfoEXT *s);
void mvk_put_VkRenderingEndInfoKHR(MvkEncoder *e, const VkRenderingEndInfoKHR *s);
void mvk_get_VkRenderingEndInfoKHR(MvkDecoder *d, VkRenderingEndInfoKHR *s);
void mvk_put_VkComputeOccupancyPriorityParametersNV(MvkEncoder *e, const VkComputeOccupancyPriorityParametersNV *s);
void mvk_get_VkComputeOccupancyPriorityParametersNV(MvkDecoder *d, VkComputeOccupancyPriorityParametersNV *s);
void mvk_put_VkPhysicalDeviceCooperativeMatrixInfo2EXT(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixInfo2EXT *s);
void mvk_get_VkPhysicalDeviceCooperativeMatrixInfo2EXT(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixInfo2EXT *s);
void mvk_put_VkCooperativeMatrixProperties2EXT(MvkEncoder *e, const VkCooperativeMatrixProperties2EXT *s);
void mvk_get_VkCooperativeMatrixProperties2EXT(MvkDecoder *d, VkCooperativeMatrixProperties2EXT *s);
void mvk_put_VkImageViewCaptureDescriptorDataInfoEXT(MvkEncoder *e, const VkImageViewCaptureDescriptorDataInfoEXT *s);
void mvk_get_VkImageViewCaptureDescriptorDataInfoEXT(MvkDecoder *d, VkImageViewCaptureDescriptorDataInfoEXT *s);
void mvk_put_VkDescriptorGetInfoEXT(MvkEncoder *e, const VkDescriptorGetInfoEXT *s);
void mvk_get_VkDescriptorGetInfoEXT(MvkDecoder *d, VkDescriptorGetInfoEXT *s);
void mvk_put_VkDescriptorDataEXT(MvkEncoder *e, const VkDescriptorDataEXT *s);
void mvk_get_VkDescriptorDataEXT(MvkDecoder *d, VkDescriptorDataEXT *s);
void mvk_put_VkDescriptorAddressInfoEXT(MvkEncoder *e, const VkDescriptorAddressInfoEXT *s);
void mvk_get_VkDescriptorAddressInfoEXT(MvkDecoder *d, VkDescriptorAddressInfoEXT *s);
void mvk_put_VkBindSparseInfo(MvkEncoder *e, const VkBindSparseInfo *s);
void mvk_get_VkBindSparseInfo(MvkDecoder *d, VkBindSparseInfo *s);
void mvk_put_VkSparseBufferMemoryBindInfo(MvkEncoder *e, const VkSparseBufferMemoryBindInfo *s);
void mvk_get_VkSparseBufferMemoryBindInfo(MvkDecoder *d, VkSparseBufferMemoryBindInfo *s);
void mvk_put_VkSparseMemoryBind(MvkEncoder *e, const VkSparseMemoryBind *s);
void mvk_get_VkSparseMemoryBind(MvkDecoder *d, VkSparseMemoryBind *s);
void mvk_put_VkSparseImageOpaqueMemoryBindInfo(MvkEncoder *e, const VkSparseImageOpaqueMemoryBindInfo *s);
void mvk_get_VkSparseImageOpaqueMemoryBindInfo(MvkDecoder *d, VkSparseImageOpaqueMemoryBindInfo *s);
void mvk_put_VkSparseImageMemoryBindInfo(MvkEncoder *e, const VkSparseImageMemoryBindInfo *s);
void mvk_get_VkSparseImageMemoryBindInfo(MvkDecoder *d, VkSparseImageMemoryBindInfo *s);
void mvk_put_VkSparseImageMemoryBind(MvkEncoder *e, const VkSparseImageMemoryBind *s);
void mvk_get_VkSparseImageMemoryBind(MvkDecoder *d, VkSparseImageMemoryBind *s);
void mvk_put_VkPhysicalDeviceGroupProperties(MvkEncoder *e, const VkPhysicalDeviceGroupProperties *s);
void mvk_get_VkPhysicalDeviceGroupProperties(MvkDecoder *d, VkPhysicalDeviceGroupProperties *s);
void mvk_put_VkCommandBufferBeginInfo(MvkEncoder *e, const VkCommandBufferBeginInfo *s);
void mvk_get_VkCommandBufferBeginInfo(MvkDecoder *d, VkCommandBufferBeginInfo *s);
void mvk_put_VkCommandBufferInheritanceInfo(MvkEncoder *e, const VkCommandBufferInheritanceInfo *s);
void mvk_get_VkCommandBufferInheritanceInfo(MvkDecoder *d, VkCommandBufferInheritanceInfo *s);
void mvk_put_VkCommandBufferAllocateInfo(MvkEncoder *e, const VkCommandBufferAllocateInfo *s);
void mvk_get_VkCommandBufferAllocateInfo(MvkDecoder *d, VkCommandBufferAllocateInfo *s);
void mvk_put_VkMappedMemoryRange(MvkEncoder *e, const VkMappedMemoryRange *s);
void mvk_get_VkMappedMemoryRange(MvkDecoder *d, VkMappedMemoryRange *s);
void mvk_put_VkSamplerCaptureDescriptorDataInfoEXT(MvkEncoder *e, const VkSamplerCaptureDescriptorDataInfoEXT *s);
void mvk_get_VkSamplerCaptureDescriptorDataInfoEXT(MvkDecoder *d, VkSamplerCaptureDescriptorDataInfoEXT *s);
void mvk_put_VkLayerProperties(MvkEncoder *e, const VkLayerProperties *s);
void mvk_get_VkLayerProperties(MvkDecoder *d, VkLayerProperties *s);
void mvk_put_VkImageCaptureDescriptorDataInfoEXT(MvkEncoder *e, const VkImageCaptureDescriptorDataInfoEXT *s);
void mvk_get_VkImageCaptureDescriptorDataInfoEXT(MvkDecoder *d, VkImageCaptureDescriptorDataInfoEXT *s);
void mvk_put_VkExtensionProperties(MvkEncoder *e, const VkExtensionProperties *s);
void mvk_get_VkExtensionProperties(MvkDecoder *d, VkExtensionProperties *s);
void mvk_put_VkBufferCaptureDescriptorDataInfoEXT(MvkEncoder *e, const VkBufferCaptureDescriptorDataInfoEXT *s);
void mvk_get_VkBufferCaptureDescriptorDataInfoEXT(MvkDecoder *d, VkBufferCaptureDescriptorDataInfoEXT *s);
void mvk_put_VkSubmitInfo(MvkEncoder *e, const VkSubmitInfo *s);
void mvk_get_VkSubmitInfo(MvkDecoder *d, VkSubmitInfo *s);
void mvk_put_VkMemoryAllocateInfo(MvkEncoder *e, const VkMemoryAllocateInfo *s);
void mvk_get_VkMemoryAllocateInfo(MvkDecoder *d, VkMemoryAllocateInfo *s);
void mvk_put_VkDeviceCreateInfo(MvkEncoder *e, const VkDeviceCreateInfo *s);
void mvk_get_VkDeviceCreateInfo(MvkDecoder *d, VkDeviceCreateInfo *s);
void mvk_put_VkDeviceQueueCreateInfo(MvkEncoder *e, const VkDeviceQueueCreateInfo *s);
void mvk_get_VkDeviceQueueCreateInfo(MvkDecoder *d, VkDeviceQueueCreateInfo *s);
void mvk_put_VkInstanceCreateInfo(MvkEncoder *e, const VkInstanceCreateInfo *s);
void mvk_get_VkInstanceCreateInfo(MvkDecoder *d, VkInstanceCreateInfo *s);
void mvk_put_VkApplicationInfo(MvkEncoder *e, const VkApplicationInfo *s);
void mvk_get_VkApplicationInfo(MvkDecoder *d, VkApplicationInfo *s);
void mvk_put_VkEventCreateInfo(MvkEncoder *e, const VkEventCreateInfo *s);
void mvk_get_VkEventCreateInfo(MvkDecoder *d, VkEventCreateInfo *s);
void mvk_put_VkAccelerationStructureCaptureDescriptorDataInfoEXT(MvkEncoder *e, const VkAccelerationStructureCaptureDescriptorDataInfoEXT *s);
void mvk_get_VkAccelerationStructureCaptureDescriptorDataInfoEXT(MvkDecoder *d, VkAccelerationStructureCaptureDescriptorDataInfoEXT *s);
void mvk_put_VkMemoryUnmapInfo(MvkEncoder *e, const VkMemoryUnmapInfo *s);
void mvk_get_VkMemoryUnmapInfo(MvkDecoder *d, VkMemoryUnmapInfo *s);
void mvk_put_VkSemaphoreSignalInfo(MvkEncoder *e, const VkSemaphoreSignalInfo *s);
void mvk_get_VkSemaphoreSignalInfo(MvkDecoder *d, VkSemaphoreSignalInfo *s);
void mvk_put_VkDeviceQueueInfo2(MvkEncoder *e, const VkDeviceQueueInfo2 *s);
void mvk_get_VkDeviceQueueInfo2(MvkDecoder *d, VkDeviceQueueInfo2 *s);
void mvk_put_VkSemaphoreWaitInfo(MvkEncoder *e, const VkSemaphoreWaitInfo *s);
void mvk_get_VkSemaphoreWaitInfo(MvkDecoder *d, VkSemaphoreWaitInfo *s);
void mvk_put_VkSemaphoreCreateInfo(MvkEncoder *e, const VkSemaphoreCreateInfo *s);
void mvk_get_VkSemaphoreCreateInfo(MvkDecoder *d, VkSemaphoreCreateInfo *s);
void mvk_put_VkFenceCreateInfo(MvkEncoder *e, const VkFenceCreateInfo *s);
void mvk_get_VkFenceCreateInfo(MvkDecoder *d, VkFenceCreateInfo *s);
void mvk_put_VkSubmitInfo2(MvkEncoder *e, const VkSubmitInfo2 *s);
void mvk_get_VkSubmitInfo2(MvkDecoder *d, VkSubmitInfo2 *s);
void mvk_put_VkSemaphoreSubmitInfo(MvkEncoder *e, const VkSemaphoreSubmitInfo *s);
void mvk_get_VkSemaphoreSubmitInfo(MvkDecoder *d, VkSemaphoreSubmitInfo *s);
void mvk_put_VkCommandBufferSubmitInfo(MvkEncoder *e, const VkCommandBufferSubmitInfo *s);
void mvk_get_VkCommandBufferSubmitInfo(MvkDecoder *d, VkCommandBufferSubmitInfo *s);
void mvk_put_VkDescriptorPoolCreateInfo(MvkEncoder *e, const VkDescriptorPoolCreateInfo *s);
void mvk_get_VkDescriptorPoolCreateInfo(MvkDecoder *d, VkDescriptorPoolCreateInfo *s);
void mvk_put_VkDescriptorPoolSize(MvkEncoder *e, const VkDescriptorPoolSize *s);
void mvk_get_VkDescriptorPoolSize(MvkDecoder *d, VkDescriptorPoolSize *s);
void mvk_put_VkDescriptorSetAllocateInfo(MvkEncoder *e, const VkDescriptorSetAllocateInfo *s);
void mvk_get_VkDescriptorSetAllocateInfo(MvkDecoder *d, VkDescriptorSetAllocateInfo *s);
void mvk_put_VkMemoryMapInfo(MvkEncoder *e, const VkMemoryMapInfo *s);
void mvk_get_VkMemoryMapInfo(MvkDecoder *d, VkMemoryMapInfo *s);
void mvk_put_VkBufferUsageFlags2CreateInfo(MvkEncoder *e, const VkBufferUsageFlags2CreateInfo *s);
void mvk_get_VkBufferUsageFlags2CreateInfo(MvkDecoder *d, VkBufferUsageFlags2CreateInfo *s);
void mvk_put_VkImageCreateFlags2CreateInfoKHR(MvkEncoder *e, const VkImageCreateFlags2CreateInfoKHR *s);
void mvk_get_VkImageCreateFlags2CreateInfoKHR(MvkDecoder *d, VkImageCreateFlags2CreateInfoKHR *s);
void mvk_put_VkImageUsageFlags2CreateInfoKHR(MvkEncoder *e, const VkImageUsageFlags2CreateInfoKHR *s);
void mvk_get_VkImageUsageFlags2CreateInfoKHR(MvkDecoder *d, VkImageUsageFlags2CreateInfoKHR *s);
void mvk_put_VkComputePipelineIndirectBufferInfoNV(MvkEncoder *e, const VkComputePipelineIndirectBufferInfoNV *s);
void mvk_get_VkComputePipelineIndirectBufferInfoNV(MvkDecoder *d, VkComputePipelineIndirectBufferInfoNV *s);
void mvk_put_VkPipelineCreateFlags2CreateInfo(MvkEncoder *e, const VkPipelineCreateFlags2CreateInfo *s);
void mvk_get_VkPipelineCreateFlags2CreateInfo(MvkDecoder *d, VkPipelineCreateFlags2CreateInfo *s);
void mvk_put_VkValidationFlagsEXT(MvkEncoder *e, const VkValidationFlagsEXT *s);
void mvk_get_VkValidationFlagsEXT(MvkDecoder *d, VkValidationFlagsEXT *s);
void mvk_put_VkValidationFeaturesEXT(MvkEncoder *e, const VkValidationFeaturesEXT *s);
void mvk_get_VkValidationFeaturesEXT(MvkDecoder *d, VkValidationFeaturesEXT *s);
void mvk_put_VkPipelineRasterizationStateRasterizationOrderAMD(MvkEncoder *e, const VkPipelineRasterizationStateRasterizationOrderAMD *s);
void mvk_get_VkPipelineRasterizationStateRasterizationOrderAMD(MvkDecoder *d, VkPipelineRasterizationStateRasterizationOrderAMD *s);
void mvk_put_VkDedicatedAllocationImageCreateInfoNV(MvkEncoder *e, const VkDedicatedAllocationImageCreateInfoNV *s);
void mvk_get_VkDedicatedAllocationImageCreateInfoNV(MvkDecoder *d, VkDedicatedAllocationImageCreateInfoNV *s);
void mvk_put_VkDedicatedAllocationBufferCreateInfoNV(MvkEncoder *e, const VkDedicatedAllocationBufferCreateInfoNV *s);
void mvk_get_VkDedicatedAllocationBufferCreateInfoNV(MvkDecoder *d, VkDedicatedAllocationBufferCreateInfoNV *s);
void mvk_put_VkDedicatedAllocationMemoryAllocateInfoNV(MvkEncoder *e, const VkDedicatedAllocationMemoryAllocateInfoNV *s);
void mvk_get_VkDedicatedAllocationMemoryAllocateInfoNV(MvkDecoder *d, VkDedicatedAllocationMemoryAllocateInfoNV *s);
void mvk_put_VkExternalMemoryImageCreateInfoNV(MvkEncoder *e, const VkExternalMemoryImageCreateInfoNV *s);
void mvk_get_VkExternalMemoryImageCreateInfoNV(MvkDecoder *d, VkExternalMemoryImageCreateInfoNV *s);
void mvk_put_VkExportMemoryAllocateInfoNV(MvkEncoder *e, const VkExportMemoryAllocateInfoNV *s);
void mvk_get_VkExportMemoryAllocateInfoNV(MvkDecoder *d, VkExportMemoryAllocateInfoNV *s);
void mvk_put_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV *s);
void mvk_get_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV(MvkDecoder *d, VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV *s);
void mvk_put_VkPushConstantBankInfoNV(MvkEncoder *e, const VkPushConstantBankInfoNV *s);
void mvk_get_VkPushConstantBankInfoNV(MvkDecoder *d, VkPushConstantBankInfoNV *s);
void mvk_put_VkPhysicalDevicePushConstantBankFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePushConstantBankFeaturesNV *s);
void mvk_get_VkPhysicalDevicePushConstantBankFeaturesNV(MvkDecoder *d, VkPhysicalDevicePushConstantBankFeaturesNV *s);
void mvk_put_VkPhysicalDevicePushConstantBankPropertiesNV(MvkEncoder *e, const VkPhysicalDevicePushConstantBankPropertiesNV *s);
void mvk_get_VkPhysicalDevicePushConstantBankPropertiesNV(MvkDecoder *d, VkPhysicalDevicePushConstantBankPropertiesNV *s);
void mvk_put_VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV *s);
void mvk_get_VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV(MvkDecoder *d, VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV *s);
void mvk_put_VkDevicePrivateDataCreateInfo(MvkEncoder *e, const VkDevicePrivateDataCreateInfo *s);
void mvk_get_VkDevicePrivateDataCreateInfo(MvkDecoder *d, VkDevicePrivateDataCreateInfo *s);
void mvk_put_VkPhysicalDevicePrivateDataFeatures(MvkEncoder *e, const VkPhysicalDevicePrivateDataFeatures *s);
void mvk_get_VkPhysicalDevicePrivateDataFeatures(MvkDecoder *d, VkPhysicalDevicePrivateDataFeatures *s);
void mvk_put_VkPhysicalDevicePrivateDataBaseHandleFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePrivateDataBaseHandleFeaturesNV *s);
void mvk_get_VkPhysicalDevicePrivateDataBaseHandleFeaturesNV(MvkDecoder *d, VkPhysicalDevicePrivateDataBaseHandleFeaturesNV *s);
void mvk_put_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *s);
void mvk_get_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV(MvkDecoder *d, VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *s);
void mvk_put_VkPhysicalDeviceClusterAccelerationStructureFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceClusterAccelerationStructureFeaturesNV *s);
void mvk_get_VkPhysicalDeviceClusterAccelerationStructureFeaturesNV(MvkDecoder *d, VkPhysicalDeviceClusterAccelerationStructureFeaturesNV *s);
void mvk_put_VkPhysicalDeviceClusterAccelerationStructurePropertiesNV(MvkEncoder *e, const VkPhysicalDeviceClusterAccelerationStructurePropertiesNV *s);
void mvk_get_VkPhysicalDeviceClusterAccelerationStructurePropertiesNV(MvkDecoder *d, VkPhysicalDeviceClusterAccelerationStructurePropertiesNV *s);
void mvk_put_VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV(MvkEncoder *e, const VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV *s);
void mvk_get_VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV(MvkDecoder *d, VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV *s);
void mvk_put_VkPhysicalDeviceMultiDrawPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMultiDrawPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceMultiDrawPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceMultiDrawPropertiesEXT *s);
void mvk_put_VkGraphicsPipelineShaderGroupsCreateInfoNV(MvkEncoder *e, const VkGraphicsPipelineShaderGroupsCreateInfoNV *s);
void mvk_get_VkGraphicsPipelineShaderGroupsCreateInfoNV(MvkDecoder *d, VkGraphicsPipelineShaderGroupsCreateInfoNV *s);
void mvk_put_VkGraphicsShaderGroupCreateInfoNV(MvkEncoder *e, const VkGraphicsShaderGroupCreateInfoNV *s);
void mvk_get_VkGraphicsShaderGroupCreateInfoNV(MvkDecoder *d, VkGraphicsShaderGroupCreateInfoNV *s);
void mvk_put_VkPhysicalDevicePushDescriptorProperties(MvkEncoder *e, const VkPhysicalDevicePushDescriptorProperties *s);
void mvk_get_VkPhysicalDevicePushDescriptorProperties(MvkDecoder *d, VkPhysicalDevicePushDescriptorProperties *s);
void mvk_put_VkPhysicalDeviceDriverProperties(MvkEncoder *e, const VkPhysicalDeviceDriverProperties *s);
void mvk_get_VkPhysicalDeviceDriverProperties(MvkDecoder *d, VkPhysicalDeviceDriverProperties *s);
void mvk_put_VkConformanceVersion(MvkEncoder *e, const VkConformanceVersion *s);
void mvk_get_VkConformanceVersion(MvkDecoder *d, VkConformanceVersion *s);
void mvk_put_VkPhysicalDeviceVariablePointersFeatures(MvkEncoder *e, const VkPhysicalDeviceVariablePointersFeatures *s);
void mvk_get_VkPhysicalDeviceVariablePointersFeatures(MvkDecoder *d, VkPhysicalDeviceVariablePointersFeatures *s);
void mvk_put_VkPhysicalDeviceExternalImageFormatInfo(MvkEncoder *e, const VkPhysicalDeviceExternalImageFormatInfo *s);
void mvk_get_VkPhysicalDeviceExternalImageFormatInfo(MvkDecoder *d, VkPhysicalDeviceExternalImageFormatInfo *s);
void mvk_put_VkExternalImageFormatProperties(MvkEncoder *e, const VkExternalImageFormatProperties *s);
void mvk_get_VkExternalImageFormatProperties(MvkDecoder *d, VkExternalImageFormatProperties *s);
void mvk_put_VkPhysicalDeviceIDProperties(MvkEncoder *e, const VkPhysicalDeviceIDProperties *s);
void mvk_get_VkPhysicalDeviceIDProperties(MvkDecoder *d, VkPhysicalDeviceIDProperties *s);
void mvk_put_VkExternalMemoryImageCreateInfo(MvkEncoder *e, const VkExternalMemoryImageCreateInfo *s);
void mvk_get_VkExternalMemoryImageCreateInfo(MvkDecoder *d, VkExternalMemoryImageCreateInfo *s);
void mvk_put_VkExternalMemoryBufferCreateInfo(MvkEncoder *e, const VkExternalMemoryBufferCreateInfo *s);
void mvk_get_VkExternalMemoryBufferCreateInfo(MvkDecoder *d, VkExternalMemoryBufferCreateInfo *s);
void mvk_put_VkExportMemoryAllocateInfo(MvkEncoder *e, const VkExportMemoryAllocateInfo *s);
void mvk_get_VkExportMemoryAllocateInfo(MvkDecoder *d, VkExportMemoryAllocateInfo *s);
void mvk_put_VkExportSemaphoreCreateInfo(MvkEncoder *e, const VkExportSemaphoreCreateInfo *s);
void mvk_get_VkExportSemaphoreCreateInfo(MvkDecoder *d, VkExportSemaphoreCreateInfo *s);
void mvk_put_VkExportFenceCreateInfo(MvkEncoder *e, const VkExportFenceCreateInfo *s);
void mvk_get_VkExportFenceCreateInfo(MvkDecoder *d, VkExportFenceCreateInfo *s);
void mvk_put_VkPhysicalDeviceMultiviewFeatures(MvkEncoder *e, const VkPhysicalDeviceMultiviewFeatures *s);
void mvk_get_VkPhysicalDeviceMultiviewFeatures(MvkDecoder *d, VkPhysicalDeviceMultiviewFeatures *s);
void mvk_put_VkPhysicalDeviceMultiviewProperties(MvkEncoder *e, const VkPhysicalDeviceMultiviewProperties *s);
void mvk_get_VkPhysicalDeviceMultiviewProperties(MvkDecoder *d, VkPhysicalDeviceMultiviewProperties *s);
void mvk_put_VkRenderPassMultiviewCreateInfo(MvkEncoder *e, const VkRenderPassMultiviewCreateInfo *s);
void mvk_get_VkRenderPassMultiviewCreateInfo(MvkDecoder *d, VkRenderPassMultiviewCreateInfo *s);
void mvk_put_VkMemoryAllocateFlagsInfo(MvkEncoder *e, const VkMemoryAllocateFlagsInfo *s);
void mvk_get_VkMemoryAllocateFlagsInfo(MvkDecoder *d, VkMemoryAllocateFlagsInfo *s);
void mvk_put_VkBindBufferMemoryDeviceGroupInfo(MvkEncoder *e, const VkBindBufferMemoryDeviceGroupInfo *s);
void mvk_get_VkBindBufferMemoryDeviceGroupInfo(MvkDecoder *d, VkBindBufferMemoryDeviceGroupInfo *s);
void mvk_put_VkBindImageMemoryDeviceGroupInfo(MvkEncoder *e, const VkBindImageMemoryDeviceGroupInfo *s);
void mvk_get_VkBindImageMemoryDeviceGroupInfo(MvkDecoder *d, VkBindImageMemoryDeviceGroupInfo *s);
void mvk_put_VkDeviceGroupRenderPassBeginInfo(MvkEncoder *e, const VkDeviceGroupRenderPassBeginInfo *s);
void mvk_get_VkDeviceGroupRenderPassBeginInfo(MvkDecoder *d, VkDeviceGroupRenderPassBeginInfo *s);
void mvk_put_VkDeviceGroupCommandBufferBeginInfo(MvkEncoder *e, const VkDeviceGroupCommandBufferBeginInfo *s);
void mvk_get_VkDeviceGroupCommandBufferBeginInfo(MvkDecoder *d, VkDeviceGroupCommandBufferBeginInfo *s);
void mvk_put_VkDeviceGroupSubmitInfo(MvkEncoder *e, const VkDeviceGroupSubmitInfo *s);
void mvk_get_VkDeviceGroupSubmitInfo(MvkDecoder *d, VkDeviceGroupSubmitInfo *s);
void mvk_put_VkDeviceGroupBindSparseInfo(MvkEncoder *e, const VkDeviceGroupBindSparseInfo *s);
void mvk_get_VkDeviceGroupBindSparseInfo(MvkDecoder *d, VkDeviceGroupBindSparseInfo *s);
void mvk_put_VkImageSwapchainCreateInfoKHR(MvkEncoder *e, const VkImageSwapchainCreateInfoKHR *s);
void mvk_get_VkImageSwapchainCreateInfoKHR(MvkDecoder *d, VkImageSwapchainCreateInfoKHR *s);
void mvk_put_VkBindImageMemorySwapchainInfoKHR(MvkEncoder *e, const VkBindImageMemorySwapchainInfoKHR *s);
void mvk_get_VkBindImageMemorySwapchainInfoKHR(MvkDecoder *d, VkBindImageMemorySwapchainInfoKHR *s);
void mvk_put_VkDeviceGroupDeviceCreateInfo(MvkEncoder *e, const VkDeviceGroupDeviceCreateInfo *s);
void mvk_get_VkDeviceGroupDeviceCreateInfo(MvkDecoder *d, VkDeviceGroupDeviceCreateInfo *s);
void mvk_put_VkPipelineViewportWScalingStateCreateInfoNV(MvkEncoder *e, const VkPipelineViewportWScalingStateCreateInfoNV *s);
void mvk_get_VkPipelineViewportWScalingStateCreateInfoNV(MvkDecoder *d, VkPipelineViewportWScalingStateCreateInfoNV *s);
void mvk_put_VkPipelineViewportSwizzleStateCreateInfoNV(MvkEncoder *e, const VkPipelineViewportSwizzleStateCreateInfoNV *s);
void mvk_get_VkPipelineViewportSwizzleStateCreateInfoNV(MvkDecoder *d, VkPipelineViewportSwizzleStateCreateInfoNV *s);
void mvk_put_VkPhysicalDeviceDiscardRectanglePropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDiscardRectanglePropertiesEXT *s);
void mvk_get_VkPhysicalDeviceDiscardRectanglePropertiesEXT(MvkDecoder *d, VkPhysicalDeviceDiscardRectanglePropertiesEXT *s);
void mvk_put_VkPipelineDiscardRectangleStateCreateInfoEXT(MvkEncoder *e, const VkPipelineDiscardRectangleStateCreateInfoEXT *s);
void mvk_get_VkPipelineDiscardRectangleStateCreateInfoEXT(MvkDecoder *d, VkPipelineDiscardRectangleStateCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX(MvkEncoder *e, const VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX *s);
void mvk_get_VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX(MvkDecoder *d, VkPhysicalDeviceMultiviewPerViewAttributesPropertiesNVX *s);
void mvk_put_VkRenderPassInputAttachmentAspectCreateInfo(MvkEncoder *e, const VkRenderPassInputAttachmentAspectCreateInfo *s);
void mvk_get_VkRenderPassInputAttachmentAspectCreateInfo(MvkDecoder *d, VkRenderPassInputAttachmentAspectCreateInfo *s);
void mvk_put_VkInputAttachmentAspectReference(MvkEncoder *e, const VkInputAttachmentAspectReference *s);
void mvk_get_VkInputAttachmentAspectReference(MvkDecoder *d, VkInputAttachmentAspectReference *s);
void mvk_put_VkPhysicalDevice16BitStorageFeatures(MvkEncoder *e, const VkPhysicalDevice16BitStorageFeatures *s);
void mvk_get_VkPhysicalDevice16BitStorageFeatures(MvkDecoder *d, VkPhysicalDevice16BitStorageFeatures *s);
void mvk_put_VkPhysicalDeviceSubgroupProperties(MvkEncoder *e, const VkPhysicalDeviceSubgroupProperties *s);
void mvk_get_VkPhysicalDeviceSubgroupProperties(MvkDecoder *d, VkPhysicalDeviceSubgroupProperties *s);
void mvk_put_VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *s);
void mvk_get_VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures(MvkDecoder *d, VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *s);
void mvk_put_VkPhysicalDevicePointClippingProperties(MvkEncoder *e, const VkPhysicalDevicePointClippingProperties *s);
void mvk_get_VkPhysicalDevicePointClippingProperties(MvkDecoder *d, VkPhysicalDevicePointClippingProperties *s);
void mvk_put_VkMemoryDedicatedRequirements(MvkEncoder *e, const VkMemoryDedicatedRequirements *s);
void mvk_get_VkMemoryDedicatedRequirements(MvkDecoder *d, VkMemoryDedicatedRequirements *s);
void mvk_put_VkMemoryDedicatedAllocateInfo(MvkEncoder *e, const VkMemoryDedicatedAllocateInfo *s);
void mvk_get_VkMemoryDedicatedAllocateInfo(MvkDecoder *d, VkMemoryDedicatedAllocateInfo *s);
void mvk_put_VkImageViewUsageCreateInfo(MvkEncoder *e, const VkImageViewUsageCreateInfo *s);
void mvk_get_VkImageViewUsageCreateInfo(MvkDecoder *d, VkImageViewUsageCreateInfo *s);
void mvk_put_VkImageViewUsage2CreateInfoKHR(MvkEncoder *e, const VkImageViewUsage2CreateInfoKHR *s);
void mvk_get_VkImageViewUsage2CreateInfoKHR(MvkDecoder *d, VkImageViewUsage2CreateInfoKHR *s);
void mvk_put_VkImageViewSlicedCreateInfoEXT(MvkEncoder *e, const VkImageViewSlicedCreateInfoEXT *s);
void mvk_get_VkImageViewSlicedCreateInfoEXT(MvkDecoder *d, VkImageViewSlicedCreateInfoEXT *s);
void mvk_put_VkPipelineTessellationDomainOriginStateCreateInfo(MvkEncoder *e, const VkPipelineTessellationDomainOriginStateCreateInfo *s);
void mvk_get_VkPipelineTessellationDomainOriginStateCreateInfo(MvkDecoder *d, VkPipelineTessellationDomainOriginStateCreateInfo *s);
void mvk_put_VkSamplerYcbcrConversionInfo(MvkEncoder *e, const VkSamplerYcbcrConversionInfo *s);
void mvk_get_VkSamplerYcbcrConversionInfo(MvkDecoder *d, VkSamplerYcbcrConversionInfo *s);
void mvk_put_VkBindImagePlaneMemoryInfo(MvkEncoder *e, const VkBindImagePlaneMemoryInfo *s);
void mvk_get_VkBindImagePlaneMemoryInfo(MvkDecoder *d, VkBindImagePlaneMemoryInfo *s);
void mvk_put_VkImagePlaneMemoryRequirementsInfo(MvkEncoder *e, const VkImagePlaneMemoryRequirementsInfo *s);
void mvk_get_VkImagePlaneMemoryRequirementsInfo(MvkDecoder *d, VkImagePlaneMemoryRequirementsInfo *s);
void mvk_put_VkPhysicalDeviceSamplerYcbcrConversionFeatures(MvkEncoder *e, const VkPhysicalDeviceSamplerYcbcrConversionFeatures *s);
void mvk_get_VkPhysicalDeviceSamplerYcbcrConversionFeatures(MvkDecoder *d, VkPhysicalDeviceSamplerYcbcrConversionFeatures *s);
void mvk_put_VkSamplerYcbcrConversionImageFormatProperties(MvkEncoder *e, const VkSamplerYcbcrConversionImageFormatProperties *s);
void mvk_get_VkSamplerYcbcrConversionImageFormatProperties(MvkDecoder *d, VkSamplerYcbcrConversionImageFormatProperties *s);
void mvk_put_VkTextureLODGatherFormatPropertiesAMD(MvkEncoder *e, const VkTextureLODGatherFormatPropertiesAMD *s);
void mvk_get_VkTextureLODGatherFormatPropertiesAMD(MvkDecoder *d, VkTextureLODGatherFormatPropertiesAMD *s);
void mvk_put_VkProtectedSubmitInfo(MvkEncoder *e, const VkProtectedSubmitInfo *s);
void mvk_get_VkProtectedSubmitInfo(MvkDecoder *d, VkProtectedSubmitInfo *s);
void mvk_put_VkPhysicalDeviceProtectedMemoryFeatures(MvkEncoder *e, const VkPhysicalDeviceProtectedMemoryFeatures *s);
void mvk_get_VkPhysicalDeviceProtectedMemoryFeatures(MvkDecoder *d, VkPhysicalDeviceProtectedMemoryFeatures *s);
void mvk_put_VkPhysicalDeviceProtectedMemoryProperties(MvkEncoder *e, const VkPhysicalDeviceProtectedMemoryProperties *s);
void mvk_get_VkPhysicalDeviceProtectedMemoryProperties(MvkDecoder *d, VkPhysicalDeviceProtectedMemoryProperties *s);
void mvk_put_VkPipelineCoverageToColorStateCreateInfoNV(MvkEncoder *e, const VkPipelineCoverageToColorStateCreateInfoNV *s);
void mvk_get_VkPipelineCoverageToColorStateCreateInfoNV(MvkDecoder *d, VkPipelineCoverageToColorStateCreateInfoNV *s);
void mvk_put_VkPhysicalDeviceSamplerFilterMinmaxProperties(MvkEncoder *e, const VkPhysicalDeviceSamplerFilterMinmaxProperties *s);
void mvk_get_VkPhysicalDeviceSamplerFilterMinmaxProperties(MvkDecoder *d, VkPhysicalDeviceSamplerFilterMinmaxProperties *s);
void mvk_put_VkRenderPassSampleLocationsBeginInfoEXT(MvkEncoder *e, const VkRenderPassSampleLocationsBeginInfoEXT *s);
void mvk_get_VkRenderPassSampleLocationsBeginInfoEXT(MvkDecoder *d, VkRenderPassSampleLocationsBeginInfoEXT *s);
void mvk_put_VkAttachmentSampleLocationsEXT(MvkEncoder *e, const VkAttachmentSampleLocationsEXT *s);
void mvk_get_VkAttachmentSampleLocationsEXT(MvkDecoder *d, VkAttachmentSampleLocationsEXT *s);
void mvk_put_VkSubpassSampleLocationsEXT(MvkEncoder *e, const VkSubpassSampleLocationsEXT *s);
void mvk_get_VkSubpassSampleLocationsEXT(MvkDecoder *d, VkSubpassSampleLocationsEXT *s);
void mvk_put_VkPipelineSampleLocationsStateCreateInfoEXT(MvkEncoder *e, const VkPipelineSampleLocationsStateCreateInfoEXT *s);
void mvk_get_VkPipelineSampleLocationsStateCreateInfoEXT(MvkDecoder *d, VkPipelineSampleLocationsStateCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceSampleLocationsPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceSampleLocationsPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceSampleLocationsPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceSampleLocationsPropertiesEXT *s);
void mvk_put_VkSamplerReductionModeCreateInfo(MvkEncoder *e, const VkSamplerReductionModeCreateInfo *s);
void mvk_get_VkSamplerReductionModeCreateInfo(MvkDecoder *d, VkSamplerReductionModeCreateInfo *s);
void mvk_put_VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceMultiDrawFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMultiDrawFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceMultiDrawFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMultiDrawFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT *s);
void mvk_put_VkPipelineColorBlendAdvancedStateCreateInfoEXT(MvkEncoder *e, const VkPipelineColorBlendAdvancedStateCreateInfoEXT *s);
void mvk_get_VkPipelineColorBlendAdvancedStateCreateInfoEXT(MvkDecoder *d, VkPipelineColorBlendAdvancedStateCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceInlineUniformBlockFeatures(MvkEncoder *e, const VkPhysicalDeviceInlineUniformBlockFeatures *s);
void mvk_get_VkPhysicalDeviceInlineUniformBlockFeatures(MvkDecoder *d, VkPhysicalDeviceInlineUniformBlockFeatures *s);
void mvk_put_VkPhysicalDeviceInlineUniformBlockProperties(MvkEncoder *e, const VkPhysicalDeviceInlineUniformBlockProperties *s);
void mvk_get_VkPhysicalDeviceInlineUniformBlockProperties(MvkDecoder *d, VkPhysicalDeviceInlineUniformBlockProperties *s);
void mvk_put_VkWriteDescriptorSetInlineUniformBlock(MvkEncoder *e, const VkWriteDescriptorSetInlineUniformBlock *s);
void mvk_get_VkWriteDescriptorSetInlineUniformBlock(MvkDecoder *d, VkWriteDescriptorSetInlineUniformBlock *s);
void mvk_put_VkDescriptorPoolInlineUniformBlockCreateInfo(MvkEncoder *e, const VkDescriptorPoolInlineUniformBlockCreateInfo *s);
void mvk_get_VkDescriptorPoolInlineUniformBlockCreateInfo(MvkDecoder *d, VkDescriptorPoolInlineUniformBlockCreateInfo *s);
void mvk_put_VkPipelineCoverageModulationStateCreateInfoNV(MvkEncoder *e, const VkPipelineCoverageModulationStateCreateInfoNV *s);
void mvk_get_VkPipelineCoverageModulationStateCreateInfoNV(MvkDecoder *d, VkPipelineCoverageModulationStateCreateInfoNV *s);
void mvk_put_VkImageFormatListCreateInfo(MvkEncoder *e, const VkImageFormatListCreateInfo *s);
void mvk_get_VkImageFormatListCreateInfo(MvkDecoder *d, VkImageFormatListCreateInfo *s);
void mvk_put_VkShaderModuleValidationCacheCreateInfoEXT(MvkEncoder *e, const VkShaderModuleValidationCacheCreateInfoEXT *s);
void mvk_get_VkShaderModuleValidationCacheCreateInfoEXT(MvkDecoder *d, VkShaderModuleValidationCacheCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceMaintenance3Properties(MvkEncoder *e, const VkPhysicalDeviceMaintenance3Properties *s);
void mvk_get_VkPhysicalDeviceMaintenance3Properties(MvkDecoder *d, VkPhysicalDeviceMaintenance3Properties *s);
void mvk_put_VkPhysicalDeviceMaintenance4Features(MvkEncoder *e, const VkPhysicalDeviceMaintenance4Features *s);
void mvk_get_VkPhysicalDeviceMaintenance4Features(MvkDecoder *d, VkPhysicalDeviceMaintenance4Features *s);
void mvk_put_VkPhysicalDeviceMaintenance4Properties(MvkEncoder *e, const VkPhysicalDeviceMaintenance4Properties *s);
void mvk_get_VkPhysicalDeviceMaintenance4Properties(MvkDecoder *d, VkPhysicalDeviceMaintenance4Properties *s);
void mvk_put_VkPhysicalDeviceMaintenance5Features(MvkEncoder *e, const VkPhysicalDeviceMaintenance5Features *s);
void mvk_get_VkPhysicalDeviceMaintenance5Features(MvkDecoder *d, VkPhysicalDeviceMaintenance5Features *s);
void mvk_put_VkPhysicalDeviceMaintenance5Properties(MvkEncoder *e, const VkPhysicalDeviceMaintenance5Properties *s);
void mvk_get_VkPhysicalDeviceMaintenance5Properties(MvkDecoder *d, VkPhysicalDeviceMaintenance5Properties *s);
void mvk_put_VkPhysicalDeviceMaintenance6Features(MvkEncoder *e, const VkPhysicalDeviceMaintenance6Features *s);
void mvk_get_VkPhysicalDeviceMaintenance6Features(MvkDecoder *d, VkPhysicalDeviceMaintenance6Features *s);
void mvk_put_VkPhysicalDeviceMaintenance6Properties(MvkEncoder *e, const VkPhysicalDeviceMaintenance6Properties *s);
void mvk_get_VkPhysicalDeviceMaintenance6Properties(MvkDecoder *d, VkPhysicalDeviceMaintenance6Properties *s);
void mvk_put_VkPhysicalDeviceMaintenance7FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance7FeaturesKHR *s);
void mvk_get_VkPhysicalDeviceMaintenance7FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance7FeaturesKHR *s);
void mvk_put_VkPhysicalDeviceMaintenance7PropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance7PropertiesKHR *s);
void mvk_get_VkPhysicalDeviceMaintenance7PropertiesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance7PropertiesKHR *s);
void mvk_put_VkPhysicalDeviceLayeredApiPropertiesListKHR(MvkEncoder *e, const VkPhysicalDeviceLayeredApiPropertiesListKHR *s);
void mvk_get_VkPhysicalDeviceLayeredApiPropertiesListKHR(MvkDecoder *d, VkPhysicalDeviceLayeredApiPropertiesListKHR *s);
void mvk_put_VkPhysicalDeviceLayeredApiPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceLayeredApiPropertiesKHR *s);
void mvk_get_VkPhysicalDeviceLayeredApiPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceLayeredApiPropertiesKHR *s);
void mvk_put_VkPhysicalDeviceLayeredApiVulkanPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceLayeredApiVulkanPropertiesKHR *s);
void mvk_get_VkPhysicalDeviceLayeredApiVulkanPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceLayeredApiVulkanPropertiesKHR *s);
void mvk_put_VkPhysicalDeviceMaintenance8FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance8FeaturesKHR *s);
void mvk_get_VkPhysicalDeviceMaintenance8FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance8FeaturesKHR *s);
void mvk_put_VkPhysicalDeviceMaintenance9FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance9FeaturesKHR *s);
void mvk_get_VkPhysicalDeviceMaintenance9FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance9FeaturesKHR *s);
void mvk_put_VkPhysicalDeviceMaintenance9PropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance9PropertiesKHR *s);
void mvk_get_VkPhysicalDeviceMaintenance9PropertiesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance9PropertiesKHR *s);
void mvk_put_VkPhysicalDeviceMaintenance11FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance11FeaturesKHR *s);
void mvk_get_VkPhysicalDeviceMaintenance11FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance11FeaturesKHR *s);
void mvk_put_VkPhysicalDeviceMaintenance10PropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance10PropertiesKHR *s);
void mvk_get_VkPhysicalDeviceMaintenance10PropertiesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance10PropertiesKHR *s);
void mvk_put_VkPhysicalDeviceMaintenance10FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceMaintenance10FeaturesKHR *s);
void mvk_get_VkPhysicalDeviceMaintenance10FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceMaintenance10FeaturesKHR *s);
void mvk_put_VkQueueFamilyOwnershipTransferPropertiesKHR(MvkEncoder *e, const VkQueueFamilyOwnershipTransferPropertiesKHR *s);
void mvk_get_VkQueueFamilyOwnershipTransferPropertiesKHR(MvkDecoder *d, VkQueueFamilyOwnershipTransferPropertiesKHR *s);
void mvk_put_VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR(MvkEncoder *e, const VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR *s);
void mvk_get_VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR(MvkDecoder *d, VkQueueFamilyOptimalImageTransferGranularityPropertiesKHR *s);
void mvk_put_VkPhysicalDeviceShaderDrawParametersFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderDrawParametersFeatures *s);
void mvk_get_VkPhysicalDeviceShaderDrawParametersFeatures(MvkDecoder *d, VkPhysicalDeviceShaderDrawParametersFeatures *s);
void mvk_put_VkPhysicalDeviceShaderFloat16Int8Features(MvkEncoder *e, const VkPhysicalDeviceShaderFloat16Int8Features *s);
void mvk_get_VkPhysicalDeviceShaderFloat16Int8Features(MvkDecoder *d, VkPhysicalDeviceShaderFloat16Int8Features *s);
void mvk_put_VkPhysicalDeviceFloatControlsProperties(MvkEncoder *e, const VkPhysicalDeviceFloatControlsProperties *s);
void mvk_get_VkPhysicalDeviceFloatControlsProperties(MvkDecoder *d, VkPhysicalDeviceFloatControlsProperties *s);
void mvk_put_VkPhysicalDeviceHostQueryResetFeatures(MvkEncoder *e, const VkPhysicalDeviceHostQueryResetFeatures *s);
void mvk_get_VkPhysicalDeviceHostQueryResetFeatures(MvkDecoder *d, VkPhysicalDeviceHostQueryResetFeatures *s);
void mvk_put_VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM *s);
void mvk_put_VkDeviceQueueGlobalPriorityCreateInfo(MvkEncoder *e, const VkDeviceQueueGlobalPriorityCreateInfo *s);
void mvk_get_VkDeviceQueueGlobalPriorityCreateInfo(MvkDecoder *d, VkDeviceQueueGlobalPriorityCreateInfo *s);
void mvk_put_VkPhysicalDeviceGlobalPriorityQueryFeatures(MvkEncoder *e, const VkPhysicalDeviceGlobalPriorityQueryFeatures *s);
void mvk_get_VkPhysicalDeviceGlobalPriorityQueryFeatures(MvkDecoder *d, VkPhysicalDeviceGlobalPriorityQueryFeatures *s);
void mvk_put_VkQueueFamilyGlobalPriorityProperties(MvkEncoder *e, const VkQueueFamilyGlobalPriorityProperties *s);
void mvk_get_VkQueueFamilyGlobalPriorityProperties(MvkDecoder *d, VkQueueFamilyGlobalPriorityProperties *s);
void mvk_put_VkPhysicalDeviceDeviceMemoryReportFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDeviceMemoryReportFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceDeviceMemoryReportFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDeviceMemoryReportFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceConservativeRasterizationPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceConservativeRasterizationPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceConservativeRasterizationPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceConservativeRasterizationPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceShaderCorePropertiesAMD(MvkEncoder *e, const VkPhysicalDeviceShaderCorePropertiesAMD *s);
void mvk_get_VkPhysicalDeviceShaderCorePropertiesAMD(MvkDecoder *d, VkPhysicalDeviceShaderCorePropertiesAMD *s);
void mvk_put_VkPhysicalDeviceShaderCoreProperties2AMD(MvkEncoder *e, const VkPhysicalDeviceShaderCoreProperties2AMD *s);
void mvk_get_VkPhysicalDeviceShaderCoreProperties2AMD(MvkDecoder *d, VkPhysicalDeviceShaderCoreProperties2AMD *s);
void mvk_put_VkPipelineRasterizationConservativeStateCreateInfoEXT(MvkEncoder *e, const VkPipelineRasterizationConservativeStateCreateInfoEXT *s);
void mvk_get_VkPipelineRasterizationConservativeStateCreateInfoEXT(MvkDecoder *d, VkPipelineRasterizationConservativeStateCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceDescriptorIndexingFeatures(MvkEncoder *e, const VkPhysicalDeviceDescriptorIndexingFeatures *s);
void mvk_get_VkPhysicalDeviceDescriptorIndexingFeatures(MvkDecoder *d, VkPhysicalDeviceDescriptorIndexingFeatures *s);
void mvk_put_VkPhysicalDeviceDescriptorIndexingProperties(MvkEncoder *e, const VkPhysicalDeviceDescriptorIndexingProperties *s);
void mvk_get_VkPhysicalDeviceDescriptorIndexingProperties(MvkDecoder *d, VkPhysicalDeviceDescriptorIndexingProperties *s);
void mvk_put_VkDescriptorSetLayoutBindingFlagsCreateInfo(MvkEncoder *e, const VkDescriptorSetLayoutBindingFlagsCreateInfo *s);
void mvk_get_VkDescriptorSetLayoutBindingFlagsCreateInfo(MvkDecoder *d, VkDescriptorSetLayoutBindingFlagsCreateInfo *s);
void mvk_put_VkDescriptorSetVariableDescriptorCountAllocateInfo(MvkEncoder *e, const VkDescriptorSetVariableDescriptorCountAllocateInfo *s);
void mvk_get_VkDescriptorSetVariableDescriptorCountAllocateInfo(MvkDecoder *d, VkDescriptorSetVariableDescriptorCountAllocateInfo *s);
void mvk_put_VkDescriptorSetVariableDescriptorCountLayoutSupport(MvkEncoder *e, const VkDescriptorSetVariableDescriptorCountLayoutSupport *s);
void mvk_get_VkDescriptorSetVariableDescriptorCountLayoutSupport(MvkDecoder *d, VkDescriptorSetVariableDescriptorCountLayoutSupport *s);
void mvk_put_VkPhysicalDeviceTimelineSemaphoreFeatures(MvkEncoder *e, const VkPhysicalDeviceTimelineSemaphoreFeatures *s);
void mvk_get_VkPhysicalDeviceTimelineSemaphoreFeatures(MvkDecoder *d, VkPhysicalDeviceTimelineSemaphoreFeatures *s);
void mvk_put_VkPhysicalDeviceTimelineSemaphoreProperties(MvkEncoder *e, const VkPhysicalDeviceTimelineSemaphoreProperties *s);
void mvk_get_VkPhysicalDeviceTimelineSemaphoreProperties(MvkDecoder *d, VkPhysicalDeviceTimelineSemaphoreProperties *s);
void mvk_put_VkSemaphoreTypeCreateInfo(MvkEncoder *e, const VkSemaphoreTypeCreateInfo *s);
void mvk_get_VkSemaphoreTypeCreateInfo(MvkDecoder *d, VkSemaphoreTypeCreateInfo *s);
void mvk_put_VkTimelineSemaphoreSubmitInfo(MvkEncoder *e, const VkTimelineSemaphoreSubmitInfo *s);
void mvk_get_VkTimelineSemaphoreSubmitInfo(MvkDecoder *d, VkTimelineSemaphoreSubmitInfo *s);
void mvk_put_VkPipelineVertexInputDivisorStateCreateInfo(MvkEncoder *e, const VkPipelineVertexInputDivisorStateCreateInfo *s);
void mvk_get_VkPipelineVertexInputDivisorStateCreateInfo(MvkDecoder *d, VkPipelineVertexInputDivisorStateCreateInfo *s);
void mvk_put_VkVertexInputBindingDivisorDescription(MvkEncoder *e, const VkVertexInputBindingDivisorDescription *s);
void mvk_get_VkVertexInputBindingDivisorDescription(MvkDecoder *d, VkVertexInputBindingDivisorDescription *s);
void mvk_put_VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceVertexAttributeDivisorProperties(MvkEncoder *e, const VkPhysicalDeviceVertexAttributeDivisorProperties *s);
void mvk_get_VkPhysicalDeviceVertexAttributeDivisorProperties(MvkDecoder *d, VkPhysicalDeviceVertexAttributeDivisorProperties *s);
void mvk_put_VkPhysicalDevicePCIBusInfoPropertiesEXT(MvkEncoder *e, const VkPhysicalDevicePCIBusInfoPropertiesEXT *s);
void mvk_get_VkPhysicalDevicePCIBusInfoPropertiesEXT(MvkDecoder *d, VkPhysicalDevicePCIBusInfoPropertiesEXT *s);
void mvk_put_VkCommandBufferInheritanceConditionalRenderingInfoEXT(MvkEncoder *e, const VkCommandBufferInheritanceConditionalRenderingInfoEXT *s);
void mvk_get_VkCommandBufferInheritanceConditionalRenderingInfoEXT(MvkDecoder *d, VkCommandBufferInheritanceConditionalRenderingInfoEXT *s);
void mvk_put_VkPhysicalDevice8BitStorageFeatures(MvkEncoder *e, const VkPhysicalDevice8BitStorageFeatures *s);
void mvk_get_VkPhysicalDevice8BitStorageFeatures(MvkDecoder *d, VkPhysicalDevice8BitStorageFeatures *s);
void mvk_put_VkPhysicalDeviceConditionalRenderingFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceConditionalRenderingFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceConditionalRenderingFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceConditionalRenderingFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceVulkanMemoryModelFeatures(MvkEncoder *e, const VkPhysicalDeviceVulkanMemoryModelFeatures *s);
void mvk_get_VkPhysicalDeviceVulkanMemoryModelFeatures(MvkDecoder *d, VkPhysicalDeviceVulkanMemoryModelFeatures *s);
void mvk_put_VkPhysicalDeviceShaderAtomicInt64Features(MvkEncoder *e, const VkPhysicalDeviceShaderAtomicInt64Features *s);
void mvk_get_VkPhysicalDeviceShaderAtomicInt64Features(MvkDecoder *d, VkPhysicalDeviceShaderAtomicInt64Features *s);
void mvk_put_VkPhysicalDeviceShaderAtomicFloatFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderAtomicFloatFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT *s);
void mvk_put_VkPhysicalDeviceVertexAttributeDivisorFeatures(MvkEncoder *e, const VkPhysicalDeviceVertexAttributeDivisorFeatures *s);
void mvk_get_VkPhysicalDeviceVertexAttributeDivisorFeatures(MvkDecoder *d, VkPhysicalDeviceVertexAttributeDivisorFeatures *s);
void mvk_put_VkQueueFamilyCheckpointPropertiesNV(MvkEncoder *e, const VkQueueFamilyCheckpointPropertiesNV *s);
void mvk_get_VkQueueFamilyCheckpointPropertiesNV(MvkDecoder *d, VkQueueFamilyCheckpointPropertiesNV *s);
void mvk_put_VkPhysicalDeviceDepthStencilResolveProperties(MvkEncoder *e, const VkPhysicalDeviceDepthStencilResolveProperties *s);
void mvk_get_VkPhysicalDeviceDepthStencilResolveProperties(MvkDecoder *d, VkPhysicalDeviceDepthStencilResolveProperties *s);
void mvk_put_VkSubpassDescriptionDepthStencilResolve(MvkEncoder *e, const VkSubpassDescriptionDepthStencilResolve *s);
void mvk_get_VkSubpassDescriptionDepthStencilResolve(MvkDecoder *d, VkSubpassDescriptionDepthStencilResolve *s);
void mvk_put_VkImageViewASTCDecodeModeEXT(MvkEncoder *e, const VkImageViewASTCDecodeModeEXT *s);
void mvk_get_VkImageViewASTCDecodeModeEXT(MvkDecoder *d, VkImageViewASTCDecodeModeEXT *s);
void mvk_put_VkPhysicalDeviceASTCDecodeFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceASTCDecodeFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceASTCDecodeFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceASTCDecodeFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceTransformFeedbackFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceTransformFeedbackFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceTransformFeedbackFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceTransformFeedbackFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceTransformFeedbackPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceTransformFeedbackPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceTransformFeedbackPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceTransformFeedbackPropertiesEXT *s);
void mvk_put_VkPipelineRasterizationStateStreamCreateInfoEXT(MvkEncoder *e, const VkPipelineRasterizationStateStreamCreateInfoEXT *s);
void mvk_get_VkPipelineRasterizationStateStreamCreateInfoEXT(MvkDecoder *d, VkPipelineRasterizationStateStreamCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV *s);
void mvk_get_VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV(MvkDecoder *d, VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV *s);
void mvk_put_VkPipelineRepresentativeFragmentTestStateCreateInfoNV(MvkEncoder *e, const VkPipelineRepresentativeFragmentTestStateCreateInfoNV *s);
void mvk_get_VkPipelineRepresentativeFragmentTestStateCreateInfoNV(MvkDecoder *d, VkPipelineRepresentativeFragmentTestStateCreateInfoNV *s);
void mvk_put_VkPhysicalDeviceExclusiveScissorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceExclusiveScissorFeaturesNV *s);
void mvk_get_VkPhysicalDeviceExclusiveScissorFeaturesNV(MvkDecoder *d, VkPhysicalDeviceExclusiveScissorFeaturesNV *s);
void mvk_put_VkPipelineViewportExclusiveScissorStateCreateInfoNV(MvkEncoder *e, const VkPipelineViewportExclusiveScissorStateCreateInfoNV *s);
void mvk_get_VkPipelineViewportExclusiveScissorStateCreateInfoNV(MvkDecoder *d, VkPipelineViewportExclusiveScissorStateCreateInfoNV *s);
void mvk_put_VkPhysicalDeviceCornerSampledImageFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCornerSampledImageFeaturesNV *s);
void mvk_get_VkPhysicalDeviceCornerSampledImageFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCornerSampledImageFeaturesNV *s);
void mvk_put_VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR *s);
void mvk_get_VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR *s);
void mvk_put_VkPhysicalDeviceShaderImageFootprintFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceShaderImageFootprintFeaturesNV *s);
void mvk_get_VkPhysicalDeviceShaderImageFootprintFeaturesNV(MvkDecoder *d, VkPhysicalDeviceShaderImageFootprintFeaturesNV *s);
void mvk_put_VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV *s);
void mvk_get_VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV(MvkDecoder *d, VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV *s);
void mvk_put_VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceCopyMemoryIndirectFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCopyMemoryIndirectFeaturesNV *s);
void mvk_get_VkPhysicalDeviceCopyMemoryIndirectFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCopyMemoryIndirectFeaturesNV *s);
void mvk_put_VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR *s);
void mvk_get_VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceCopyMemoryIndirectPropertiesKHR *s);
void mvk_put_VkPhysicalDeviceMemoryDecompressionFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMemoryDecompressionFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceMemoryDecompressionFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMemoryDecompressionFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceMemoryDecompressionPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMemoryDecompressionPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceMemoryDecompressionPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceMemoryDecompressionPropertiesEXT *s);
void mvk_put_VkPipelineViewportShadingRateImageStateCreateInfoNV(MvkEncoder *e, const VkPipelineViewportShadingRateImageStateCreateInfoNV *s);
void mvk_get_VkPipelineViewportShadingRateImageStateCreateInfoNV(MvkDecoder *d, VkPipelineViewportShadingRateImageStateCreateInfoNV *s);
void mvk_put_VkPhysicalDeviceShadingRateImageFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceShadingRateImageFeaturesNV *s);
void mvk_get_VkPhysicalDeviceShadingRateImageFeaturesNV(MvkDecoder *d, VkPhysicalDeviceShadingRateImageFeaturesNV *s);
void mvk_put_VkPhysicalDeviceShadingRateImagePropertiesNV(MvkEncoder *e, const VkPhysicalDeviceShadingRateImagePropertiesNV *s);
void mvk_get_VkPhysicalDeviceShadingRateImagePropertiesNV(MvkDecoder *d, VkPhysicalDeviceShadingRateImagePropertiesNV *s);
void mvk_put_VkPhysicalDeviceInvocationMaskFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceInvocationMaskFeaturesHUAWEI *s);
void mvk_get_VkPhysicalDeviceInvocationMaskFeaturesHUAWEI(MvkDecoder *d, VkPhysicalDeviceInvocationMaskFeaturesHUAWEI *s);
void mvk_put_VkPipelineViewportCoarseSampleOrderStateCreateInfoNV(MvkEncoder *e, const VkPipelineViewportCoarseSampleOrderStateCreateInfoNV *s);
void mvk_get_VkPipelineViewportCoarseSampleOrderStateCreateInfoNV(MvkDecoder *d, VkPipelineViewportCoarseSampleOrderStateCreateInfoNV *s);
void mvk_put_VkPhysicalDeviceMeshShaderFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceMeshShaderFeaturesNV *s);
void mvk_get_VkPhysicalDeviceMeshShaderFeaturesNV(MvkDecoder *d, VkPhysicalDeviceMeshShaderFeaturesNV *s);
void mvk_put_VkPhysicalDeviceMeshShaderPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceMeshShaderPropertiesNV *s);
void mvk_get_VkPhysicalDeviceMeshShaderPropertiesNV(MvkDecoder *d, VkPhysicalDeviceMeshShaderPropertiesNV *s);
void mvk_put_VkPhysicalDeviceMeshShaderFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMeshShaderFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceMeshShaderFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMeshShaderFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceMeshShaderPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMeshShaderPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceMeshShaderPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceMeshShaderPropertiesEXT *s);
void mvk_put_VkWriteDescriptorSetAccelerationStructureKHR(MvkEncoder *e, const VkWriteDescriptorSetAccelerationStructureKHR *s);
void mvk_get_VkWriteDescriptorSetAccelerationStructureKHR(MvkDecoder *d, VkWriteDescriptorSetAccelerationStructureKHR *s);
void mvk_put_VkWriteDescriptorSetAccelerationStructureNV(MvkEncoder *e, const VkWriteDescriptorSetAccelerationStructureNV *s);
void mvk_get_VkWriteDescriptorSetAccelerationStructureNV(MvkDecoder *d, VkWriteDescriptorSetAccelerationStructureNV *s);
void mvk_put_VkPhysicalDeviceAccelerationStructureFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceAccelerationStructureFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceAccelerationStructureFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceAccelerationStructureFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceRayTracingPipelineFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRayTracingPipelineFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceRayTracingPipelineFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceRayTracingPipelineFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceRayQueryFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRayQueryFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceRayQueryFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceRayQueryFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceAccelerationStructurePropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceAccelerationStructurePropertiesKHR *s);
void mvk_get_VkPhysicalDeviceAccelerationStructurePropertiesKHR(MvkDecoder *d, VkPhysicalDeviceAccelerationStructurePropertiesKHR *s);
void mvk_put_VkPhysicalDeviceRayTracingPipelinePropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceRayTracingPipelinePropertiesKHR *s);
void mvk_get_VkPhysicalDeviceRayTracingPipelinePropertiesKHR(MvkDecoder *d, VkPhysicalDeviceRayTracingPipelinePropertiesKHR *s);
void mvk_put_VkPhysicalDeviceRayTracingPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingPropertiesNV *s);
void mvk_get_VkPhysicalDeviceRayTracingPropertiesNV(MvkDecoder *d, VkPhysicalDeviceRayTracingPropertiesNV *s);
void mvk_put_VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR *s);
void mvk_get_VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR *s);
void mvk_put_VkDrmFormatModifierPropertiesListEXT(MvkEncoder *e, const VkDrmFormatModifierPropertiesListEXT *s);
void mvk_get_VkDrmFormatModifierPropertiesListEXT(MvkDecoder *d, VkDrmFormatModifierPropertiesListEXT *s);
void mvk_put_VkDrmFormatModifierPropertiesEXT(MvkEncoder *e, const VkDrmFormatModifierPropertiesEXT *s);
void mvk_get_VkDrmFormatModifierPropertiesEXT(MvkDecoder *d, VkDrmFormatModifierPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceImageDrmFormatModifierInfoEXT(MvkEncoder *e, const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *s);
void mvk_get_VkPhysicalDeviceImageDrmFormatModifierInfoEXT(MvkDecoder *d, VkPhysicalDeviceImageDrmFormatModifierInfoEXT *s);
void mvk_put_VkImageDrmFormatModifierListCreateInfoEXT(MvkEncoder *e, const VkImageDrmFormatModifierListCreateInfoEXT *s);
void mvk_get_VkImageDrmFormatModifierListCreateInfoEXT(MvkDecoder *d, VkImageDrmFormatModifierListCreateInfoEXT *s);
void mvk_put_VkImageDrmFormatModifierExplicitCreateInfoEXT(MvkEncoder *e, const VkImageDrmFormatModifierExplicitCreateInfoEXT *s);
void mvk_get_VkImageDrmFormatModifierExplicitCreateInfoEXT(MvkDecoder *d, VkImageDrmFormatModifierExplicitCreateInfoEXT *s);
void mvk_put_VkImageStencilUsageCreateInfo(MvkEncoder *e, const VkImageStencilUsageCreateInfo *s);
void mvk_get_VkImageStencilUsageCreateInfo(MvkDecoder *d, VkImageStencilUsageCreateInfo *s);
void mvk_put_VkImageStencilUsage2CreateInfoKHR(MvkEncoder *e, const VkImageStencilUsage2CreateInfoKHR *s);
void mvk_get_VkImageStencilUsage2CreateInfoKHR(MvkDecoder *d, VkImageStencilUsage2CreateInfoKHR *s);
void mvk_put_VkDeviceMemoryOverallocationCreateInfoAMD(MvkEncoder *e, const VkDeviceMemoryOverallocationCreateInfoAMD *s);
void mvk_get_VkDeviceMemoryOverallocationCreateInfoAMD(MvkDecoder *d, VkDeviceMemoryOverallocationCreateInfoAMD *s);
void mvk_put_VkPhysicalDeviceFragmentDensityMapFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceFragmentDensityMapFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMapFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceFragmentDensityMap2FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMap2FeaturesEXT *s);
void mvk_get_VkPhysicalDeviceFragmentDensityMap2FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMap2FeaturesEXT *s);
void mvk_put_VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceFragmentDensityMapPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceFragmentDensityMapPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMapPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceFragmentDensityMap2PropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMap2PropertiesEXT *s);
void mvk_get_VkPhysicalDeviceFragmentDensityMap2PropertiesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMap2PropertiesEXT *s);
void mvk_put_VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMapOffsetPropertiesEXT *s);
void mvk_put_VkRenderPassFragmentDensityMapCreateInfoEXT(MvkEncoder *e, const VkRenderPassFragmentDensityMapCreateInfoEXT *s);
void mvk_get_VkRenderPassFragmentDensityMapCreateInfoEXT(MvkDecoder *d, VkRenderPassFragmentDensityMapCreateInfoEXT *s);
void mvk_put_VkRenderPassFragmentDensityMapOffsetEndInfoEXT(MvkEncoder *e, const VkRenderPassFragmentDensityMapOffsetEndInfoEXT *s);
void mvk_get_VkRenderPassFragmentDensityMapOffsetEndInfoEXT(MvkDecoder *d, VkRenderPassFragmentDensityMapOffsetEndInfoEXT *s);
void mvk_put_VkPhysicalDeviceScalarBlockLayoutFeatures(MvkEncoder *e, const VkPhysicalDeviceScalarBlockLayoutFeatures *s);
void mvk_get_VkPhysicalDeviceScalarBlockLayoutFeatures(MvkDecoder *d, VkPhysicalDeviceScalarBlockLayoutFeatures *s);
void mvk_put_VkPhysicalDeviceUniformBufferStandardLayoutFeatures(MvkEncoder *e, const VkPhysicalDeviceUniformBufferStandardLayoutFeatures *s);
void mvk_get_VkPhysicalDeviceUniformBufferStandardLayoutFeatures(MvkDecoder *d, VkPhysicalDeviceUniformBufferStandardLayoutFeatures *s);
void mvk_put_VkPhysicalDeviceDepthClipEnableFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDepthClipEnableFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceDepthClipEnableFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDepthClipEnableFeaturesEXT *s);
void mvk_put_VkPipelineRasterizationDepthClipStateCreateInfoEXT(MvkEncoder *e, const VkPipelineRasterizationDepthClipStateCreateInfoEXT *s);
void mvk_get_VkPipelineRasterizationDepthClipStateCreateInfoEXT(MvkDecoder *d, VkPipelineRasterizationDepthClipStateCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceMemoryBudgetPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMemoryBudgetPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceMemoryBudgetPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceMemoryBudgetPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceMemoryPriorityFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMemoryPriorityFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceMemoryPriorityFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMemoryPriorityFeaturesEXT *s);
void mvk_put_VkMemoryPriorityAllocateInfoEXT(MvkEncoder *e, const VkMemoryPriorityAllocateInfoEXT *s);
void mvk_get_VkMemoryPriorityAllocateInfoEXT(MvkDecoder *d, VkMemoryPriorityAllocateInfoEXT *s);
void mvk_put_VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT *s);
void mvk_get_VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT(MvkDecoder *d, VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceBufferDeviceAddressFeatures(MvkEncoder *e, const VkPhysicalDeviceBufferDeviceAddressFeatures *s);
void mvk_get_VkPhysicalDeviceBufferDeviceAddressFeatures(MvkDecoder *d, VkPhysicalDeviceBufferDeviceAddressFeatures *s);
void mvk_put_VkPhysicalDeviceBufferDeviceAddressFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceBufferDeviceAddressFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *s);
void mvk_put_VkBufferOpaqueCaptureAddressCreateInfo(MvkEncoder *e, const VkBufferOpaqueCaptureAddressCreateInfo *s);
void mvk_get_VkBufferOpaqueCaptureAddressCreateInfo(MvkDecoder *d, VkBufferOpaqueCaptureAddressCreateInfo *s);
void mvk_put_VkBufferDeviceAddressCreateInfoEXT(MvkEncoder *e, const VkBufferDeviceAddressCreateInfoEXT *s);
void mvk_get_VkBufferDeviceAddressCreateInfoEXT(MvkDecoder *d, VkBufferDeviceAddressCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceImageViewImageFormatInfoEXT(MvkEncoder *e, const VkPhysicalDeviceImageViewImageFormatInfoEXT *s);
void mvk_get_VkPhysicalDeviceImageViewImageFormatInfoEXT(MvkDecoder *d, VkPhysicalDeviceImageViewImageFormatInfoEXT *s);
void mvk_put_VkFilterCubicImageViewImageFormatPropertiesEXT(MvkEncoder *e, const VkFilterCubicImageViewImageFormatPropertiesEXT *s);
void mvk_get_VkFilterCubicImageViewImageFormatPropertiesEXT(MvkDecoder *d, VkFilterCubicImageViewImageFormatPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceImagelessFramebufferFeatures(MvkEncoder *e, const VkPhysicalDeviceImagelessFramebufferFeatures *s);
void mvk_get_VkPhysicalDeviceImagelessFramebufferFeatures(MvkDecoder *d, VkPhysicalDeviceImagelessFramebufferFeatures *s);
void mvk_put_VkFramebufferAttachmentsCreateInfo(MvkEncoder *e, const VkFramebufferAttachmentsCreateInfo *s);
void mvk_get_VkFramebufferAttachmentsCreateInfo(MvkDecoder *d, VkFramebufferAttachmentsCreateInfo *s);
void mvk_put_VkFramebufferAttachmentImageInfo(MvkEncoder *e, const VkFramebufferAttachmentImageInfo *s);
void mvk_get_VkFramebufferAttachmentImageInfo(MvkDecoder *d, VkFramebufferAttachmentImageInfo *s);
void mvk_put_VkRenderPassAttachmentBeginInfo(MvkEncoder *e, const VkRenderPassAttachmentBeginInfo *s);
void mvk_get_VkRenderPassAttachmentBeginInfo(MvkDecoder *d, VkRenderPassAttachmentBeginInfo *s);
void mvk_put_VkPhysicalDeviceTextureCompressionASTCHDRFeatures(MvkEncoder *e, const VkPhysicalDeviceTextureCompressionASTCHDRFeatures *s);
void mvk_get_VkPhysicalDeviceTextureCompressionASTCHDRFeatures(MvkDecoder *d, VkPhysicalDeviceTextureCompressionASTCHDRFeatures *s);
void mvk_put_VkPhysicalDeviceCooperativeMatrixFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixFeaturesNV *s);
void mvk_get_VkPhysicalDeviceCooperativeMatrixFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixFeaturesNV *s);
void mvk_put_VkPhysicalDeviceCooperativeMatrixPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixPropertiesNV *s);
void mvk_get_VkPhysicalDeviceCooperativeMatrixPropertiesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixPropertiesNV *s);
void mvk_put_VkPhysicalDeviceYcbcrImageArraysFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceYcbcrImageArraysFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *s);
void mvk_put_VkPipelineCreationFeedbackCreateInfo(MvkEncoder *e, const VkPipelineCreationFeedbackCreateInfo *s);
void mvk_get_VkPipelineCreationFeedbackCreateInfo(MvkDecoder *d, VkPipelineCreationFeedbackCreateInfo *s);
void mvk_put_VkPipelineCreationFeedback(MvkEncoder *e, const VkPipelineCreationFeedback *s);
void mvk_get_VkPipelineCreationFeedback(MvkDecoder *d, VkPipelineCreationFeedback *s);
void mvk_put_VkPhysicalDevicePerformanceQueryFeaturesKHR(MvkEncoder *e, const VkPhysicalDevicePerformanceQueryFeaturesKHR *s);
void mvk_get_VkPhysicalDevicePerformanceQueryFeaturesKHR(MvkDecoder *d, VkPhysicalDevicePerformanceQueryFeaturesKHR *s);
void mvk_put_VkPhysicalDevicePerformanceQueryPropertiesKHR(MvkEncoder *e, const VkPhysicalDevicePerformanceQueryPropertiesKHR *s);
void mvk_get_VkPhysicalDevicePerformanceQueryPropertiesKHR(MvkDecoder *d, VkPhysicalDevicePerformanceQueryPropertiesKHR *s);
void mvk_put_VkPerformanceQuerySubmitInfoKHR(MvkEncoder *e, const VkPerformanceQuerySubmitInfoKHR *s);
void mvk_get_VkPerformanceQuerySubmitInfoKHR(MvkDecoder *d, VkPerformanceQuerySubmitInfoKHR *s);
void mvk_put_VkPhysicalDeviceCoverageReductionModeFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCoverageReductionModeFeaturesNV *s);
void mvk_get_VkPhysicalDeviceCoverageReductionModeFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCoverageReductionModeFeaturesNV *s);
void mvk_put_VkPipelineCoverageReductionStateCreateInfoNV(MvkEncoder *e, const VkPipelineCoverageReductionStateCreateInfoNV *s);
void mvk_get_VkPipelineCoverageReductionStateCreateInfoNV(MvkDecoder *d, VkPipelineCoverageReductionStateCreateInfoNV *s);
void mvk_put_VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL(MvkEncoder *e, const VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *s);
void mvk_get_VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL(MvkDecoder *d, VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *s);
void mvk_put_VkQueryPoolPerformanceQueryCreateInfoINTEL(MvkEncoder *e, const VkQueryPoolPerformanceQueryCreateInfoINTEL *s);
void mvk_get_VkQueryPoolPerformanceQueryCreateInfoINTEL(MvkDecoder *d, VkQueryPoolPerformanceQueryCreateInfoINTEL *s);
void mvk_put_VkPhysicalDeviceShaderClockFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderClockFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceShaderClockFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderClockFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceIndexTypeUint8Features(MvkEncoder *e, const VkPhysicalDeviceIndexTypeUint8Features *s);
void mvk_get_VkPhysicalDeviceIndexTypeUint8Features(MvkDecoder *d, VkPhysicalDeviceIndexTypeUint8Features *s);
void mvk_put_VkPhysicalDeviceShaderSMBuiltinsPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceShaderSMBuiltinsPropertiesNV *s);
void mvk_get_VkPhysicalDeviceShaderSMBuiltinsPropertiesNV(MvkDecoder *d, VkPhysicalDeviceShaderSMBuiltinsPropertiesNV *s);
void mvk_put_VkPhysicalDeviceShaderSMBuiltinsFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceShaderSMBuiltinsFeaturesNV *s);
void mvk_get_VkPhysicalDeviceShaderSMBuiltinsFeaturesNV(MvkDecoder *d, VkPhysicalDeviceShaderSMBuiltinsFeaturesNV *s);
void mvk_put_VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures(MvkEncoder *e, const VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures *s);
void mvk_get_VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures(MvkDecoder *d, VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures *s);
void mvk_put_VkAttachmentReferenceStencilLayout(MvkEncoder *e, const VkAttachmentReferenceStencilLayout *s);
void mvk_get_VkAttachmentReferenceStencilLayout(MvkDecoder *d, VkAttachmentReferenceStencilLayout *s);
void mvk_put_VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *s);
void mvk_get_VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT(MvkDecoder *d, VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *s);
void mvk_put_VkAttachmentDescriptionStencilLayout(MvkEncoder *e, const VkAttachmentDescriptionStencilLayout *s);
void mvk_get_VkAttachmentDescriptionStencilLayout(MvkDecoder *d, VkAttachmentDescriptionStencilLayout *s);
void mvk_put_VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR(MvkEncoder *e, const VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *s);
void mvk_get_VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR(MvkDecoder *d, VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *s);
void mvk_get_VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures(MvkDecoder *d, VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *s);
void mvk_put_VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceTexelBufferAlignmentProperties(MvkEncoder *e, const VkPhysicalDeviceTexelBufferAlignmentProperties *s);
void mvk_get_VkPhysicalDeviceTexelBufferAlignmentProperties(MvkDecoder *d, VkPhysicalDeviceTexelBufferAlignmentProperties *s);
void mvk_put_VkPhysicalDeviceSubgroupSizeControlFeatures(MvkEncoder *e, const VkPhysicalDeviceSubgroupSizeControlFeatures *s);
void mvk_get_VkPhysicalDeviceSubgroupSizeControlFeatures(MvkDecoder *d, VkPhysicalDeviceSubgroupSizeControlFeatures *s);
void mvk_put_VkPhysicalDeviceSubgroupSizeControlProperties(MvkEncoder *e, const VkPhysicalDeviceSubgroupSizeControlProperties *s);
void mvk_get_VkPhysicalDeviceSubgroupSizeControlProperties(MvkDecoder *d, VkPhysicalDeviceSubgroupSizeControlProperties *s);
void mvk_put_VkPipelineShaderStageRequiredSubgroupSizeCreateInfo(MvkEncoder *e, const VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *s);
void mvk_get_VkPipelineShaderStageRequiredSubgroupSizeCreateInfo(MvkDecoder *d, VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *s);
void mvk_put_VkSubpassShadingPipelineCreateInfoHUAWEI(MvkEncoder *e, const VkSubpassShadingPipelineCreateInfoHUAWEI *s);
void mvk_get_VkSubpassShadingPipelineCreateInfoHUAWEI(MvkDecoder *d, VkSubpassShadingPipelineCreateInfoHUAWEI *s);
void mvk_put_VkPhysicalDeviceSubpassShadingPropertiesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceSubpassShadingPropertiesHUAWEI *s);
void mvk_get_VkPhysicalDeviceSubpassShadingPropertiesHUAWEI(MvkDecoder *d, VkPhysicalDeviceSubpassShadingPropertiesHUAWEI *s);
void mvk_put_VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI *s);
void mvk_get_VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI(MvkDecoder *d, VkPhysicalDeviceClusterCullingShaderPropertiesHUAWEI *s);
void mvk_put_VkMemoryOpaqueCaptureAddressAllocateInfo(MvkEncoder *e, const VkMemoryOpaqueCaptureAddressAllocateInfo *s);
void mvk_get_VkMemoryOpaqueCaptureAddressAllocateInfo(MvkDecoder *d, VkMemoryOpaqueCaptureAddressAllocateInfo *s);
void mvk_put_VkPhysicalDeviceLineRasterizationFeatures(MvkEncoder *e, const VkPhysicalDeviceLineRasterizationFeatures *s);
void mvk_get_VkPhysicalDeviceLineRasterizationFeatures(MvkDecoder *d, VkPhysicalDeviceLineRasterizationFeatures *s);
void mvk_put_VkPhysicalDeviceLineRasterizationProperties(MvkEncoder *e, const VkPhysicalDeviceLineRasterizationProperties *s);
void mvk_get_VkPhysicalDeviceLineRasterizationProperties(MvkDecoder *d, VkPhysicalDeviceLineRasterizationProperties *s);
void mvk_put_VkPipelineRasterizationLineStateCreateInfo(MvkEncoder *e, const VkPipelineRasterizationLineStateCreateInfo *s);
void mvk_get_VkPipelineRasterizationLineStateCreateInfo(MvkDecoder *d, VkPipelineRasterizationLineStateCreateInfo *s);
void mvk_put_VkPhysicalDevicePipelineCreationCacheControlFeatures(MvkEncoder *e, const VkPhysicalDevicePipelineCreationCacheControlFeatures *s);
void mvk_get_VkPhysicalDevicePipelineCreationCacheControlFeatures(MvkDecoder *d, VkPhysicalDevicePipelineCreationCacheControlFeatures *s);
void mvk_put_VkPhysicalDeviceVulkan11Features(MvkEncoder *e, const VkPhysicalDeviceVulkan11Features *s);
void mvk_get_VkPhysicalDeviceVulkan11Features(MvkDecoder *d, VkPhysicalDeviceVulkan11Features *s);
void mvk_put_VkPhysicalDeviceVulkan11Properties(MvkEncoder *e, const VkPhysicalDeviceVulkan11Properties *s);
void mvk_get_VkPhysicalDeviceVulkan11Properties(MvkDecoder *d, VkPhysicalDeviceVulkan11Properties *s);
void mvk_put_VkPhysicalDeviceVulkan12Features(MvkEncoder *e, const VkPhysicalDeviceVulkan12Features *s);
void mvk_get_VkPhysicalDeviceVulkan12Features(MvkDecoder *d, VkPhysicalDeviceVulkan12Features *s);
void mvk_put_VkPhysicalDeviceVulkan12Properties(MvkEncoder *e, const VkPhysicalDeviceVulkan12Properties *s);
void mvk_get_VkPhysicalDeviceVulkan12Properties(MvkDecoder *d, VkPhysicalDeviceVulkan12Properties *s);
void mvk_put_VkPhysicalDeviceVulkan13Features(MvkEncoder *e, const VkPhysicalDeviceVulkan13Features *s);
void mvk_get_VkPhysicalDeviceVulkan13Features(MvkDecoder *d, VkPhysicalDeviceVulkan13Features *s);
void mvk_put_VkPhysicalDeviceVulkan13Properties(MvkEncoder *e, const VkPhysicalDeviceVulkan13Properties *s);
void mvk_get_VkPhysicalDeviceVulkan13Properties(MvkDecoder *d, VkPhysicalDeviceVulkan13Properties *s);
void mvk_put_VkPhysicalDeviceVulkan14Features(MvkEncoder *e, const VkPhysicalDeviceVulkan14Features *s);
void mvk_get_VkPhysicalDeviceVulkan14Features(MvkDecoder *d, VkPhysicalDeviceVulkan14Features *s);
void mvk_put_VkPhysicalDeviceVulkan14Properties(MvkEncoder *e, const VkPhysicalDeviceVulkan14Properties *s);
void mvk_get_VkPhysicalDeviceVulkan14Properties(MvkDecoder *d, VkPhysicalDeviceVulkan14Properties *s);
void mvk_put_VkPipelineCompilerControlCreateInfoAMD(MvkEncoder *e, const VkPipelineCompilerControlCreateInfoAMD *s);
void mvk_get_VkPipelineCompilerControlCreateInfoAMD(MvkDecoder *d, VkPipelineCompilerControlCreateInfoAMD *s);
void mvk_put_VkPhysicalDeviceCoherentMemoryFeaturesAMD(MvkEncoder *e, const VkPhysicalDeviceCoherentMemoryFeaturesAMD *s);
void mvk_get_VkPhysicalDeviceCoherentMemoryFeaturesAMD(MvkDecoder *d, VkPhysicalDeviceCoherentMemoryFeaturesAMD *s);
void mvk_put_VkPhysicalDeviceGpaFeaturesAMD(MvkEncoder *e, const VkPhysicalDeviceGpaFeaturesAMD *s);
void mvk_get_VkPhysicalDeviceGpaFeaturesAMD(MvkDecoder *d, VkPhysicalDeviceGpaFeaturesAMD *s);
void mvk_put_VkPhysicalDeviceGpaPropertiesAMD(MvkEncoder *e, const VkPhysicalDeviceGpaPropertiesAMD *s);
void mvk_get_VkPhysicalDeviceGpaPropertiesAMD(MvkDecoder *d, VkPhysicalDeviceGpaPropertiesAMD *s);
void mvk_put_VkGpaPerfBlockPropertiesAMD(MvkEncoder *e, const VkGpaPerfBlockPropertiesAMD *s);
void mvk_get_VkGpaPerfBlockPropertiesAMD(MvkDecoder *d, VkGpaPerfBlockPropertiesAMD *s);
void mvk_put_VkPhysicalDeviceGpaProperties2AMD(MvkEncoder *e, const VkPhysicalDeviceGpaProperties2AMD *s);
void mvk_get_VkPhysicalDeviceGpaProperties2AMD(MvkDecoder *d, VkPhysicalDeviceGpaProperties2AMD *s);
void mvk_put_VkPhysicalDeviceCustomBorderColorPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceCustomBorderColorPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceCustomBorderColorPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceCustomBorderColorPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceCustomBorderColorFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceCustomBorderColorFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceCustomBorderColorFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceCustomBorderColorFeaturesEXT *s);
void mvk_put_VkSamplerBorderColorComponentMappingCreateInfoEXT(MvkEncoder *e, const VkSamplerBorderColorComponentMappingCreateInfoEXT *s);
void mvk_get_VkSamplerBorderColorComponentMappingCreateInfoEXT(MvkDecoder *d, VkSamplerBorderColorComponentMappingCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceBorderColorSwizzleFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceBorderColorSwizzleFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *s);
void mvk_put_VkAccelerationStructureGeometryLinearSweptSpheresDataNV(MvkEncoder *e, const VkAccelerationStructureGeometryLinearSweptSpheresDataNV *s);
void mvk_get_VkAccelerationStructureGeometryLinearSweptSpheresDataNV(MvkDecoder *d, VkAccelerationStructureGeometryLinearSweptSpheresDataNV *s);
void mvk_put_VkAccelerationStructureGeometrySpheresDataNV(MvkEncoder *e, const VkAccelerationStructureGeometrySpheresDataNV *s);
void mvk_get_VkAccelerationStructureGeometrySpheresDataNV(MvkDecoder *d, VkAccelerationStructureGeometrySpheresDataNV *s);
void mvk_put_VkPhysicalDeviceExtendedDynamicStateFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceExtendedDynamicStateFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceExtendedDynamicState2FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *s);
void mvk_get_VkPhysicalDeviceExtendedDynamicState2FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *s);
void mvk_put_VkPhysicalDeviceExtendedDynamicState3FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *s);
void mvk_get_VkPhysicalDeviceExtendedDynamicState3FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *s);
void mvk_put_VkPhysicalDeviceExtendedDynamicState3PropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceExtendedDynamicState3PropertiesEXT *s);
void mvk_get_VkPhysicalDeviceExtendedDynamicState3PropertiesEXT(MvkDecoder *d, VkPhysicalDeviceExtendedDynamicState3PropertiesEXT *s);
void mvk_put_VkPhysicalDeviceExtendedFlagsFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceExtendedFlagsFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceExtendedFlagsFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceExtendedFlagsFeaturesKHR *s);
void mvk_put_VkRenderPassTransformBeginInfoQCOM(MvkEncoder *e, const VkRenderPassTransformBeginInfoQCOM *s);
void mvk_get_VkRenderPassTransformBeginInfoQCOM(MvkDecoder *d, VkRenderPassTransformBeginInfoQCOM *s);
void mvk_put_VkCopyCommandTransformInfoQCOM(MvkEncoder *e, const VkCopyCommandTransformInfoQCOM *s);
void mvk_get_VkCopyCommandTransformInfoQCOM(MvkDecoder *d, VkCopyCommandTransformInfoQCOM *s);
void mvk_put_VkCommandBufferInheritanceRenderPassTransformInfoQCOM(MvkEncoder *e, const VkCommandBufferInheritanceRenderPassTransformInfoQCOM *s);
void mvk_get_VkCommandBufferInheritanceRenderPassTransformInfoQCOM(MvkDecoder *d, VkCommandBufferInheritanceRenderPassTransformInfoQCOM *s);
void mvk_put_VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV *s);
void mvk_get_VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV(MvkDecoder *d, VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV *s);
void mvk_put_VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV(MvkEncoder *e, const VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV *s);
void mvk_get_VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV(MvkDecoder *d, VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV *s);
void mvk_put_VkPartitionedAccelerationStructureFlagsNV(MvkEncoder *e, const VkPartitionedAccelerationStructureFlagsNV *s);
void mvk_get_VkPartitionedAccelerationStructureFlagsNV(MvkDecoder *d, VkPartitionedAccelerationStructureFlagsNV *s);
void mvk_put_VkWriteDescriptorSetPartitionedAccelerationStructureNV(MvkEncoder *e, const VkWriteDescriptorSetPartitionedAccelerationStructureNV *s);
void mvk_get_VkWriteDescriptorSetPartitionedAccelerationStructureNV(MvkDecoder *d, VkWriteDescriptorSetPartitionedAccelerationStructureNV *s);
void mvk_put_VkPhysicalDeviceDiagnosticsConfigFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDiagnosticsConfigFeaturesNV *s);
void mvk_get_VkPhysicalDeviceDiagnosticsConfigFeaturesNV(MvkDecoder *d, VkPhysicalDeviceDiagnosticsConfigFeaturesNV *s);
void mvk_put_VkDeviceDiagnosticsConfigCreateInfoNV(MvkEncoder *e, const VkDeviceDiagnosticsConfigCreateInfoNV *s);
void mvk_get_VkDeviceDiagnosticsConfigCreateInfoNV(MvkDecoder *d, VkDeviceDiagnosticsConfigCreateInfoNV *s);
void mvk_put_VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures(MvkEncoder *e, const VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures *s);
void mvk_get_VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures(MvkDecoder *d, VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures *s);
void mvk_put_VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceRobustness2FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRobustness2FeaturesKHR *s);
void mvk_get_VkPhysicalDeviceRobustness2FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceRobustness2FeaturesKHR *s);
void mvk_put_VkPhysicalDeviceRobustness2PropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceRobustness2PropertiesKHR *s);
void mvk_get_VkPhysicalDeviceRobustness2PropertiesKHR(MvkDecoder *d, VkPhysicalDeviceRobustness2PropertiesKHR *s);
void mvk_put_VkPhysicalDeviceImageRobustnessFeatures(MvkEncoder *e, const VkPhysicalDeviceImageRobustnessFeatures *s);
void mvk_get_VkPhysicalDeviceImageRobustnessFeatures(MvkDecoder *d, VkPhysicalDeviceImageRobustnessFeatures *s);
void mvk_put_VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *s);
void mvk_put_VkPhysicalDevice4444FormatsFeaturesEXT(MvkEncoder *e, const VkPhysicalDevice4444FormatsFeaturesEXT *s);
void mvk_get_VkPhysicalDevice4444FormatsFeaturesEXT(MvkDecoder *d, VkPhysicalDevice4444FormatsFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceSubpassShadingFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceSubpassShadingFeaturesHUAWEI *s);
void mvk_get_VkPhysicalDeviceSubpassShadingFeaturesHUAWEI(MvkDecoder *d, VkPhysicalDeviceSubpassShadingFeaturesHUAWEI *s);
void mvk_put_VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI *s);
void mvk_get_VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI(MvkDecoder *d, VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI *s);
void mvk_put_VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI *s);
void mvk_get_VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI(MvkDecoder *d, VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI *s);
void mvk_put_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *s);
void mvk_put_VkFragmentShadingRateAttachmentInfoKHR(MvkEncoder *e, const VkFragmentShadingRateAttachmentInfoKHR *s);
void mvk_get_VkFragmentShadingRateAttachmentInfoKHR(MvkDecoder *d, VkFragmentShadingRateAttachmentInfoKHR *s);
void mvk_put_VkPipelineFragmentShadingRateStateCreateInfoKHR(MvkEncoder *e, const VkPipelineFragmentShadingRateStateCreateInfoKHR *s);
void mvk_get_VkPipelineFragmentShadingRateStateCreateInfoKHR(MvkDecoder *d, VkPipelineFragmentShadingRateStateCreateInfoKHR *s);
void mvk_put_VkPhysicalDeviceFragmentShadingRateFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRateFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceFragmentShadingRateFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceFragmentShadingRateFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceFragmentShadingRatePropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRatePropertiesKHR *s);
void mvk_get_VkPhysicalDeviceFragmentShadingRatePropertiesKHR(MvkDecoder *d, VkPhysicalDeviceFragmentShadingRatePropertiesKHR *s);
void mvk_put_VkPhysicalDeviceShaderTerminateInvocationFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderTerminateInvocationFeatures *s);
void mvk_get_VkPhysicalDeviceShaderTerminateInvocationFeatures(MvkDecoder *d, VkPhysicalDeviceShaderTerminateInvocationFeatures *s);
void mvk_put_VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV *s);
void mvk_get_VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV(MvkDecoder *d, VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV *s);
void mvk_put_VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV *s);
void mvk_get_VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV(MvkDecoder *d, VkPhysicalDeviceFragmentShadingRateEnumsPropertiesNV *s);
void mvk_put_VkPipelineFragmentShadingRateEnumStateCreateInfoNV(MvkEncoder *e, const VkPipelineFragmentShadingRateEnumStateCreateInfoNV *s);
void mvk_get_VkPipelineFragmentShadingRateEnumStateCreateInfoNV(MvkDecoder *d, VkPipelineFragmentShadingRateEnumStateCreateInfoNV *s);
void mvk_put_VkPhysicalDeviceImage2DViewOf3DFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImage2DViewOf3DFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceImage2DViewOf3DFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceImage2DViewOf3DFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *s);
void mvk_put_VkMutableDescriptorTypeCreateInfoEXT(MvkEncoder *e, const VkMutableDescriptorTypeCreateInfoEXT *s);
void mvk_get_VkMutableDescriptorTypeCreateInfoEXT(MvkDecoder *d, VkMutableDescriptorTypeCreateInfoEXT *s);
void mvk_put_VkMutableDescriptorTypeListEXT(MvkEncoder *e, const VkMutableDescriptorTypeListEXT *s);
void mvk_get_VkMutableDescriptorTypeListEXT(MvkDecoder *d, VkMutableDescriptorTypeListEXT *s);
void mvk_put_VkPhysicalDeviceDepthClipControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDepthClipControlFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceDepthClipControlFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDepthClipControlFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceCustomResolveFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceCustomResolveFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceCustomResolveFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceCustomResolveFeaturesEXT *s);
void mvk_put_VkCustomResolveCreateInfoEXT(MvkEncoder *e, const VkCustomResolveCreateInfoEXT *s);
void mvk_get_VkCustomResolveCreateInfoEXT(MvkDecoder *d, VkCustomResolveCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT *s);
void mvk_put_VkGeneratedCommandsPipelineInfoEXT(MvkEncoder *e, const VkGeneratedCommandsPipelineInfoEXT *s);
void mvk_get_VkGeneratedCommandsPipelineInfoEXT(MvkDecoder *d, VkGeneratedCommandsPipelineInfoEXT *s);
void mvk_put_VkGeneratedCommandsShaderInfoEXT(MvkEncoder *e, const VkGeneratedCommandsShaderInfoEXT *s);
void mvk_get_VkGeneratedCommandsShaderInfoEXT(MvkDecoder *d, VkGeneratedCommandsShaderInfoEXT *s);
void mvk_put_VkPipelineViewportDepthClipControlCreateInfoEXT(MvkEncoder *e, const VkPipelineViewportDepthClipControlCreateInfoEXT *s);
void mvk_get_VkPipelineViewportDepthClipControlCreateInfoEXT(MvkDecoder *d, VkPipelineViewportDepthClipControlCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceDepthClampControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDepthClampControlFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceDepthClampControlFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDepthClampControlFeaturesEXT *s);
void mvk_put_VkPipelineViewportDepthClampControlCreateInfoEXT(MvkEncoder *e, const VkPipelineViewportDepthClampControlCreateInfoEXT *s);
void mvk_get_VkPipelineViewportDepthClampControlCreateInfoEXT(MvkDecoder *d, VkPipelineViewportDepthClampControlCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceExternalMemoryRDMAFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceExternalMemoryRDMAFeaturesNV *s);
void mvk_get_VkPhysicalDeviceExternalMemoryRDMAFeaturesNV(MvkDecoder *d, VkPhysicalDeviceExternalMemoryRDMAFeaturesNV *s);
void mvk_put_VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceColorWriteEnableFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceColorWriteEnableFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceColorWriteEnableFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceColorWriteEnableFeaturesEXT *s);
void mvk_put_VkPipelineColorWriteCreateInfoEXT(MvkEncoder *e, const VkPipelineColorWriteCreateInfoEXT *s);
void mvk_get_VkPipelineColorWriteCreateInfoEXT(MvkDecoder *d, VkPipelineColorWriteCreateInfoEXT *s);
void mvk_put_VkMemoryBarrierAccessFlags3KHR(MvkEncoder *e, const VkMemoryBarrierAccessFlags3KHR *s);
void mvk_get_VkMemoryBarrierAccessFlags3KHR(MvkDecoder *d, VkMemoryBarrierAccessFlags3KHR *s);
void mvk_put_VkQueueFamilyCheckpointProperties2NV(MvkEncoder *e, const VkQueueFamilyCheckpointProperties2NV *s);
void mvk_get_VkQueueFamilyCheckpointProperties2NV(MvkDecoder *d, VkQueueFamilyCheckpointProperties2NV *s);
void mvk_put_VkPhysicalDeviceSynchronization2Features(MvkEncoder *e, const VkPhysicalDeviceSynchronization2Features *s);
void mvk_get_VkPhysicalDeviceSynchronization2Features(MvkDecoder *d, VkPhysicalDeviceSynchronization2Features *s);
void mvk_put_VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceHostImageCopyFeatures(MvkEncoder *e, const VkPhysicalDeviceHostImageCopyFeatures *s);
void mvk_get_VkPhysicalDeviceHostImageCopyFeatures(MvkDecoder *d, VkPhysicalDeviceHostImageCopyFeatures *s);
void mvk_put_VkPhysicalDeviceHostImageCopyProperties(MvkEncoder *e, const VkPhysicalDeviceHostImageCopyProperties *s);
void mvk_get_VkPhysicalDeviceHostImageCopyProperties(MvkDecoder *d, VkPhysicalDeviceHostImageCopyProperties *s);
void mvk_put_VkSubresourceHostMemcpySize(MvkEncoder *e, const VkSubresourceHostMemcpySize *s);
void mvk_get_VkSubresourceHostMemcpySize(MvkDecoder *d, VkSubresourceHostMemcpySize *s);
void mvk_put_VkHostImageCopyDevicePerformanceQuery(MvkEncoder *e, const VkHostImageCopyDevicePerformanceQuery *s);
void mvk_get_VkHostImageCopyDevicePerformanceQuery(MvkDecoder *d, VkHostImageCopyDevicePerformanceQuery *s);
void mvk_put_VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT *s);
void mvk_get_VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT(MvkDecoder *d, VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceLegacyDitheringFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceLegacyDitheringFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceLegacyDitheringFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceLegacyDitheringFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT *s);
void mvk_put_VkSubpassResolvePerformanceQueryEXT(MvkEncoder *e, const VkSubpassResolvePerformanceQueryEXT *s);
void mvk_get_VkSubpassResolvePerformanceQueryEXT(MvkDecoder *d, VkSubpassResolvePerformanceQueryEXT *s);
void mvk_put_VkMultisampledRenderToSingleSampledInfoEXT(MvkEncoder *e, const VkMultisampledRenderToSingleSampledInfoEXT *s);
void mvk_get_VkMultisampledRenderToSingleSampledInfoEXT(MvkDecoder *d, VkMultisampledRenderToSingleSampledInfoEXT *s);
void mvk_put_VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT *s);
void mvk_put_VkPhysicalDevicePipelineProtectedAccessFeatures(MvkEncoder *e, const VkPhysicalDevicePipelineProtectedAccessFeatures *s);
void mvk_get_VkPhysicalDevicePipelineProtectedAccessFeatures(MvkDecoder *d, VkPhysicalDevicePipelineProtectedAccessFeatures *s);
void mvk_put_VkPhysicalDeviceInheritedViewportScissorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceInheritedViewportScissorFeaturesNV *s);
void mvk_get_VkPhysicalDeviceInheritedViewportScissorFeaturesNV(MvkDecoder *d, VkPhysicalDeviceInheritedViewportScissorFeaturesNV *s);
void mvk_put_VkCommandBufferInheritanceViewportScissorInfoNV(MvkEncoder *e, const VkCommandBufferInheritanceViewportScissorInfoNV *s);
void mvk_get_VkCommandBufferInheritanceViewportScissorInfoNV(MvkDecoder *d, VkCommandBufferInheritanceViewportScissorInfoNV *s);
void mvk_put_VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceProvokingVertexFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceProvokingVertexFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceProvokingVertexFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceProvokingVertexFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceProvokingVertexPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceProvokingVertexPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceProvokingVertexPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceProvokingVertexPropertiesEXT *s);
void mvk_put_VkPipelineRasterizationProvokingVertexStateCreateInfoEXT(MvkEncoder *e, const VkPipelineRasterizationProvokingVertexStateCreateInfoEXT *s);
void mvk_get_VkPipelineRasterizationProvokingVertexStateCreateInfoEXT(MvkDecoder *d, VkPipelineRasterizationProvokingVertexStateCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceDescriptorBufferFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorBufferFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceDescriptorBufferFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDescriptorBufferFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceDescriptorBufferPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorBufferPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceDescriptorBufferPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceDescriptorBufferPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT *s);
void mvk_put_VkDescriptorBufferBindingPushDescriptorBufferHandleEXT(MvkEncoder *e, const VkDescriptorBufferBindingPushDescriptorBufferHandleEXT *s);
void mvk_get_VkDescriptorBufferBindingPushDescriptorBufferHandleEXT(MvkDecoder *d, VkDescriptorBufferBindingPushDescriptorBufferHandleEXT *s);
void mvk_put_VkPhysicalDeviceShaderIntegerDotProductFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderIntegerDotProductFeatures *s);
void mvk_get_VkPhysicalDeviceShaderIntegerDotProductFeatures(MvkDecoder *d, VkPhysicalDeviceShaderIntegerDotProductFeatures *s);
void mvk_put_VkPhysicalDeviceShaderIntegerDotProductProperties(MvkEncoder *e, const VkPhysicalDeviceShaderIntegerDotProductProperties *s);
void mvk_get_VkPhysicalDeviceShaderIntegerDotProductProperties(MvkDecoder *d, VkPhysicalDeviceShaderIntegerDotProductProperties *s);
void mvk_put_VkPhysicalDeviceDrmPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDrmPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceDrmPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceDrmPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR *s);
void mvk_get_VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR *s);
void mvk_put_VkPhysicalDeviceShaderFmaFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderFmaFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceShaderFmaFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderFmaFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceRayTracingMotionBlurFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingMotionBlurFeaturesNV *s);
void mvk_get_VkPhysicalDeviceRayTracingMotionBlurFeaturesNV(MvkDecoder *d, VkPhysicalDeviceRayTracingMotionBlurFeaturesNV *s);
void mvk_put_VkPhysicalDeviceRayTracingValidationFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingValidationFeaturesNV *s);
void mvk_get_VkPhysicalDeviceRayTracingValidationFeaturesNV(MvkDecoder *d, VkPhysicalDeviceRayTracingValidationFeaturesNV *s);
void mvk_put_VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV *s);
void mvk_get_VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV(MvkDecoder *d, VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV *s);
void mvk_put_VkAccelerationStructureGeometryMotionTrianglesDataNV(MvkEncoder *e, const VkAccelerationStructureGeometryMotionTrianglesDataNV *s);
void mvk_get_VkAccelerationStructureGeometryMotionTrianglesDataNV(MvkDecoder *d, VkAccelerationStructureGeometryMotionTrianglesDataNV *s);
void mvk_put_VkAccelerationStructureMotionInfoNV(MvkEncoder *e, const VkAccelerationStructureMotionInfoNV *s);
void mvk_get_VkAccelerationStructureMotionInfoNV(MvkDecoder *d, VkAccelerationStructureMotionInfoNV *s);
void mvk_put_VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT *s);
void mvk_put_VkFormatProperties3(MvkEncoder *e, const VkFormatProperties3 *s);
void mvk_get_VkFormatProperties3(MvkDecoder *d, VkFormatProperties3 *s);
void mvk_put_VkFormatProperties4KHR(MvkEncoder *e, const VkFormatProperties4KHR *s);
void mvk_get_VkFormatProperties4KHR(MvkDecoder *d, VkFormatProperties4KHR *s);
void mvk_put_VkDrmFormatModifierPropertiesList2EXT(MvkEncoder *e, const VkDrmFormatModifierPropertiesList2EXT *s);
void mvk_get_VkDrmFormatModifierPropertiesList2EXT(MvkDecoder *d, VkDrmFormatModifierPropertiesList2EXT *s);
void mvk_put_VkDrmFormatModifierProperties2EXT(MvkEncoder *e, const VkDrmFormatModifierProperties2EXT *s);
void mvk_get_VkDrmFormatModifierProperties2EXT(MvkDecoder *d, VkDrmFormatModifierProperties2EXT *s);
void mvk_put_VkPipelineRenderingCreateInfo(MvkEncoder *e, const VkPipelineRenderingCreateInfo *s);
void mvk_get_VkPipelineRenderingCreateInfo(MvkDecoder *d, VkPipelineRenderingCreateInfo *s);
void mvk_put_VkRenderingFragmentShadingRateAttachmentInfoKHR(MvkEncoder *e, const VkRenderingFragmentShadingRateAttachmentInfoKHR *s);
void mvk_get_VkRenderingFragmentShadingRateAttachmentInfoKHR(MvkDecoder *d, VkRenderingFragmentShadingRateAttachmentInfoKHR *s);
void mvk_put_VkRenderingFragmentDensityMapAttachmentInfoEXT(MvkEncoder *e, const VkRenderingFragmentDensityMapAttachmentInfoEXT *s);
void mvk_get_VkRenderingFragmentDensityMapAttachmentInfoEXT(MvkDecoder *d, VkRenderingFragmentDensityMapAttachmentInfoEXT *s);
void mvk_put_VkPhysicalDeviceDynamicRenderingFeatures(MvkEncoder *e, const VkPhysicalDeviceDynamicRenderingFeatures *s);
void mvk_get_VkPhysicalDeviceDynamicRenderingFeatures(MvkDecoder *d, VkPhysicalDeviceDynamicRenderingFeatures *s);
void mvk_put_VkCommandBufferInheritanceRenderingInfo(MvkEncoder *e, const VkCommandBufferInheritanceRenderingInfo *s);
void mvk_get_VkCommandBufferInheritanceRenderingInfo(MvkDecoder *d, VkCommandBufferInheritanceRenderingInfo *s);
void mvk_put_VkAttachmentSampleCountInfoAMD(MvkEncoder *e, const VkAttachmentSampleCountInfoAMD *s);
void mvk_get_VkAttachmentSampleCountInfoAMD(MvkDecoder *d, VkAttachmentSampleCountInfoAMD *s);
void mvk_put_VkMultiviewPerViewAttributesInfoNVX(MvkEncoder *e, const VkMultiviewPerViewAttributesInfoNVX *s);
void mvk_get_VkMultiviewPerViewAttributesInfoNVX(MvkDecoder *d, VkMultiviewPerViewAttributesInfoNVX *s);
void mvk_put_VkPhysicalDeviceImageViewMinLodFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageViewMinLodFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceImageViewMinLodFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceImageViewMinLodFeaturesEXT *s);
void mvk_put_VkImageViewMinLodCreateInfoEXT(MvkEncoder *e, const VkImageViewMinLodCreateInfoEXT *s);
void mvk_get_VkImageViewMinLodCreateInfoEXT(MvkDecoder *d, VkImageViewMinLodCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceLinearColorAttachmentFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceLinearColorAttachmentFeaturesNV *s);
void mvk_get_VkPhysicalDeviceLinearColorAttachmentFeaturesNV(MvkDecoder *d, VkPhysicalDeviceLinearColorAttachmentFeaturesNV *s);
void mvk_put_VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *s);
void mvk_put_VkGraphicsPipelineLibraryCreateInfoEXT(MvkEncoder *e, const VkGraphicsPipelineLibraryCreateInfoEXT *s);
void mvk_get_VkGraphicsPipelineLibraryCreateInfoEXT(MvkDecoder *d, VkGraphicsPipelineLibraryCreateInfoEXT *s);
void mvk_put_VkTensorExplicitTilingFormatPropertiesARM(MvkEncoder *e, const VkTensorExplicitTilingFormatPropertiesARM *s);
void mvk_get_VkTensorExplicitTilingFormatPropertiesARM(MvkDecoder *d, VkTensorExplicitTilingFormatPropertiesARM *s);
void mvk_put_VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE *s);
void mvk_get_VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE(MvkDecoder *d, VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE *s);
void mvk_put_VkPhysicalDeviceNestedCommandBufferFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceNestedCommandBufferFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceNestedCommandBufferFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceNestedCommandBufferFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceNestedCommandBufferPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceNestedCommandBufferPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceNestedCommandBufferPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceNestedCommandBufferPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *s);
void mvk_put_VkPipelineShaderStageModuleIdentifierCreateInfoEXT(MvkEncoder *e, const VkPipelineShaderStageModuleIdentifierCreateInfoEXT *s);
void mvk_get_VkPipelineShaderStageModuleIdentifierCreateInfoEXT(MvkDecoder *d, VkPipelineShaderStageModuleIdentifierCreateInfoEXT *s);
void mvk_put_VkImageCompressionControlEXT(MvkEncoder *e, const VkImageCompressionControlEXT *s);
void mvk_get_VkImageCompressionControlEXT(MvkDecoder *d, VkImageCompressionControlEXT *s);
void mvk_put_VkPhysicalDeviceImageCompressionControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageCompressionControlFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceImageCompressionControlFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceImageCompressionControlFeaturesEXT *s);
void mvk_put_VkImageCompressionPropertiesEXT(MvkEncoder *e, const VkImageCompressionPropertiesEXT *s);
void mvk_get_VkImageCompressionPropertiesEXT(MvkDecoder *d, VkImageCompressionPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT *s);
void mvk_put_VkRenderPassCreationControlEXT(MvkEncoder *e, const VkRenderPassCreationControlEXT *s);
void mvk_get_VkRenderPassCreationControlEXT(MvkDecoder *d, VkRenderPassCreationControlEXT *s);
void mvk_put_VkRenderPassCreationFeedbackCreateInfoEXT(MvkEncoder *e, const VkRenderPassCreationFeedbackCreateInfoEXT *s);
void mvk_get_VkRenderPassCreationFeedbackCreateInfoEXT(MvkDecoder *d, VkRenderPassCreationFeedbackCreateInfoEXT *s);
void mvk_put_VkRenderPassCreationFeedbackInfoEXT(MvkEncoder *e, const VkRenderPassCreationFeedbackInfoEXT *s);
void mvk_get_VkRenderPassCreationFeedbackInfoEXT(MvkDecoder *d, VkRenderPassCreationFeedbackInfoEXT *s);
void mvk_put_VkRenderPassSubpassFeedbackCreateInfoEXT(MvkEncoder *e, const VkRenderPassSubpassFeedbackCreateInfoEXT *s);
void mvk_get_VkRenderPassSubpassFeedbackCreateInfoEXT(MvkDecoder *d, VkRenderPassSubpassFeedbackCreateInfoEXT *s);
void mvk_put_VkRenderPassSubpassFeedbackInfoEXT(MvkEncoder *e, const VkRenderPassSubpassFeedbackInfoEXT *s);
void mvk_get_VkRenderPassSubpassFeedbackInfoEXT(MvkDecoder *d, VkRenderPassSubpassFeedbackInfoEXT *s);
void mvk_put_VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT *s);
void mvk_put_VkAccelerationStructureGeometryMicromapDataKHR(MvkEncoder *e, const VkAccelerationStructureGeometryMicromapDataKHR *s);
void mvk_get_VkAccelerationStructureGeometryMicromapDataKHR(MvkDecoder *d, VkAccelerationStructureGeometryMicromapDataKHR *s);
void mvk_put_VkMicromapUsageKHR(MvkEncoder *e, const VkMicromapUsageKHR *s);
void mvk_get_VkMicromapUsageKHR(MvkDecoder *d, VkMicromapUsageKHR *s);
void mvk_put_VkPhysicalDeviceOpacityMicromapFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceOpacityMicromapFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceOpacityMicromapFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceOpacityMicromapFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceOpacityMicromapFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceOpacityMicromapFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceOpacityMicromapFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceOpacityMicromapFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceOpacityMicromapPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceOpacityMicromapPropertiesKHR *s);
void mvk_get_VkPhysicalDeviceOpacityMicromapPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceOpacityMicromapPropertiesKHR *s);
void mvk_put_VkPhysicalDeviceOpacityMicromapPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceOpacityMicromapPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceOpacityMicromapPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceOpacityMicromapPropertiesEXT *s);
void mvk_put_VkAccelerationStructureTrianglesOpacityMicromapKHR(MvkEncoder *e, const VkAccelerationStructureTrianglesOpacityMicromapKHR *s);
void mvk_get_VkAccelerationStructureTrianglesOpacityMicromapKHR(MvkDecoder *d, VkAccelerationStructureTrianglesOpacityMicromapKHR *s);
void mvk_put_VkAccelerationStructureTrianglesOpacityMicromapEXT(MvkEncoder *e, const VkAccelerationStructureTrianglesOpacityMicromapEXT *s);
void mvk_get_VkAccelerationStructureTrianglesOpacityMicromapEXT(MvkDecoder *d, VkAccelerationStructureTrianglesOpacityMicromapEXT *s);
void mvk_put_VkPhysicalDevicePipelinePropertiesFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePipelinePropertiesFeaturesEXT *s);
void mvk_get_VkPhysicalDevicePipelinePropertiesFeaturesEXT(MvkDecoder *d, VkPhysicalDevicePipelinePropertiesFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD(MvkEncoder *e, const VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD *s);
void mvk_get_VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD(MvkDecoder *d, VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD *s);
void mvk_put_VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT *s);
void mvk_put_VkPhysicalDevicePipelineRobustnessFeatures(MvkEncoder *e, const VkPhysicalDevicePipelineRobustnessFeatures *s);
void mvk_get_VkPhysicalDevicePipelineRobustnessFeatures(MvkDecoder *d, VkPhysicalDevicePipelineRobustnessFeatures *s);
void mvk_put_VkPipelineRobustnessCreateInfo(MvkEncoder *e, const VkPipelineRobustnessCreateInfo *s);
void mvk_get_VkPipelineRobustnessCreateInfo(MvkDecoder *d, VkPipelineRobustnessCreateInfo *s);
void mvk_put_VkPhysicalDevicePipelineRobustnessProperties(MvkEncoder *e, const VkPhysicalDevicePipelineRobustnessProperties *s);
void mvk_get_VkPhysicalDevicePipelineRobustnessProperties(MvkDecoder *d, VkPhysicalDevicePipelineRobustnessProperties *s);
void mvk_put_VkImageViewSampleWeightCreateInfoQCOM(MvkEncoder *e, const VkImageViewSampleWeightCreateInfoQCOM *s);
void mvk_get_VkImageViewSampleWeightCreateInfoQCOM(MvkDecoder *d, VkImageViewSampleWeightCreateInfoQCOM *s);
void mvk_put_VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM *s);
void mvk_put_VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM(MvkEncoder *e, const VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM *s);
void mvk_get_VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM(MvkDecoder *d, VkPhysicalDeviceShaderMultipleWaitQueuesPropertiesQCOM *s);
void mvk_put_VkPhysicalDeviceImageProcessingFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessingFeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceImageProcessingFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceImageProcessingFeaturesQCOM *s);
void mvk_put_VkPhysicalDeviceImageProcessingPropertiesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessingPropertiesQCOM *s);
void mvk_get_VkPhysicalDeviceImageProcessingPropertiesQCOM(MvkDecoder *d, VkPhysicalDeviceImageProcessingPropertiesQCOM *s);
void mvk_put_VkPhysicalDeviceAmigoProfilingFeaturesSEC(MvkEncoder *e, const VkPhysicalDeviceAmigoProfilingFeaturesSEC *s);
void mvk_get_VkPhysicalDeviceAmigoProfilingFeaturesSEC(MvkDecoder *d, VkPhysicalDeviceAmigoProfilingFeaturesSEC *s);
void mvk_put_VkAmigoProfilingSubmitInfoSEC(MvkEncoder *e, const VkAmigoProfilingSubmitInfoSEC *s);
void mvk_get_VkAmigoProfilingSubmitInfoSEC(MvkDecoder *d, VkAmigoProfilingSubmitInfoSEC *s);
void mvk_put_VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT *s);
void mvk_put_VkAttachmentFeedbackLoopInfoEXT(MvkEncoder *e, const VkAttachmentFeedbackLoopInfoEXT *s);
void mvk_get_VkAttachmentFeedbackLoopInfoEXT(MvkDecoder *d, VkAttachmentFeedbackLoopInfoEXT *s);
void mvk_put_VkPhysicalDeviceAddressBindingReportFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceAddressBindingReportFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceAddressBindingReportFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceAddressBindingReportFeaturesEXT *s);
void mvk_put_VkRenderingAttachmentFlagsInfoKHR(MvkEncoder *e, const VkRenderingAttachmentFlagsInfoKHR *s);
void mvk_get_VkRenderingAttachmentFlagsInfoKHR(MvkDecoder *d, VkRenderingAttachmentFlagsInfoKHR *s);
void mvk_put_VkResolveImageModeInfoKHR(MvkEncoder *e, const VkResolveImageModeInfoKHR *s);
void mvk_get_VkResolveImageModeInfoKHR(MvkDecoder *d, VkResolveImageModeInfoKHR *s);
void mvk_put_VkPhysicalDeviceOpticalFlowFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceOpticalFlowFeaturesNV *s);
void mvk_get_VkPhysicalDeviceOpticalFlowFeaturesNV(MvkDecoder *d, VkPhysicalDeviceOpticalFlowFeaturesNV *s);
void mvk_put_VkPhysicalDeviceOpticalFlowPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceOpticalFlowPropertiesNV *s);
void mvk_get_VkPhysicalDeviceOpticalFlowPropertiesNV(MvkDecoder *d, VkPhysicalDeviceOpticalFlowPropertiesNV *s);
void mvk_put_VkPhysicalDeviceFaultFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFaultFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceFaultFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceFaultFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceFaultFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceFaultFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceFaultFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceFaultFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceFaultPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceFaultPropertiesKHR *s);
void mvk_get_VkPhysicalDeviceFaultPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceFaultPropertiesKHR *s);
void mvk_put_VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR(MvkEncoder *e, const VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR *s);
void mvk_get_VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR(MvkDecoder *d, VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesKHR *s);
void mvk_put_VkDepthBiasRepresentationInfoEXT(MvkEncoder *e, const VkDepthBiasRepresentationInfoEXT *s);
void mvk_get_VkDepthBiasRepresentationInfoEXT(MvkDecoder *d, VkDepthBiasRepresentationInfoEXT *s);
void mvk_put_VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM *s);
void mvk_get_VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM(MvkDecoder *d, VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM *s);
void mvk_put_VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM *s);
void mvk_get_VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM(MvkDecoder *d, VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM *s);
void mvk_put_VkFrameBoundaryEXT(MvkEncoder *e, const VkFrameBoundaryEXT *s);
void mvk_get_VkFrameBoundaryEXT(MvkDecoder *d, VkFrameBoundaryEXT *s);
void mvk_put_VkPhysicalDeviceFrameBoundaryFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceFrameBoundaryFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceFrameBoundaryFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceFrameBoundaryFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceDepthBiasControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDepthBiasControlFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceDepthBiasControlFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDepthBiasControlFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV *s);
void mvk_get_VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV(MvkDecoder *d, VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV *s);
void mvk_put_VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV *s);
void mvk_get_VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV(MvkDecoder *d, VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV *s);
void mvk_put_VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV *s);
void mvk_get_VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV(MvkDecoder *d, VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV *s);
void mvk_put_VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV(MvkEncoder *e, const VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV *s);
void mvk_get_VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV(MvkDecoder *d, VkPhysicalDeviceExtendedSparseAddressSpacePropertiesNV *s);
void mvk_put_VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM *s);
void mvk_put_VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceShaderCorePropertiesARM(MvkEncoder *e, const VkPhysicalDeviceShaderCorePropertiesARM *s);
void mvk_get_VkPhysicalDeviceShaderCorePropertiesARM(MvkDecoder *d, VkPhysicalDeviceShaderCorePropertiesARM *s);
void mvk_put_VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM *s);
void mvk_put_VkMultiviewPerViewRenderAreasRenderPassBeginInfoQCOM(MvkEncoder *e, const VkMultiviewPerViewRenderAreasRenderPassBeginInfoQCOM *s);
void mvk_get_VkMultiviewPerViewRenderAreasRenderPassBeginInfoQCOM(MvkDecoder *d, VkMultiviewPerViewRenderAreasRenderPassBeginInfoQCOM *s);
void mvk_put_VkPhysicalDeviceShaderObjectFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderObjectFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderObjectFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderObjectFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceShaderObjectPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderObjectPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceShaderObjectPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceShaderObjectPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceShaderTileImageFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderTileImageFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderTileImageFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderTileImageFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceShaderTileImagePropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderTileImagePropertiesEXT *s);
void mvk_get_VkPhysicalDeviceShaderTileImagePropertiesEXT(MvkDecoder *d, VkPhysicalDeviceShaderTileImagePropertiesEXT *s);
void mvk_put_VkPhysicalDeviceCooperativeMatrixFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceCooperativeMatrixFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceCooperativeMatrixPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixPropertiesKHR *s);
void mvk_get_VkPhysicalDeviceCooperativeMatrixPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixPropertiesKHR *s);
void mvk_put_VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM *s);
void mvk_put_VkBindMemoryStatus(MvkEncoder *e, const VkBindMemoryStatus *s);
void mvk_get_VkBindMemoryStatus(MvkDecoder *d, VkBindMemoryStatus *s);
void mvk_put_VkPhysicalDeviceCubicClampFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceCubicClampFeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceCubicClampFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceCubicClampFeaturesQCOM *s);
void mvk_put_VkPhysicalDeviceYcbcrDegammaFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceYcbcrDegammaFeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceYcbcrDegammaFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceYcbcrDegammaFeaturesQCOM *s);
void mvk_put_VkSamplerYcbcrConversionYcbcrDegammaCreateInfoQCOM(MvkEncoder *e, const VkSamplerYcbcrConversionYcbcrDegammaCreateInfoQCOM *s);
void mvk_get_VkSamplerYcbcrConversionYcbcrDegammaCreateInfoQCOM(MvkDecoder *d, VkSamplerYcbcrConversionYcbcrDegammaCreateInfoQCOM *s);
void mvk_put_VkPhysicalDeviceCubicWeightsFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceCubicWeightsFeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceCubicWeightsFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceCubicWeightsFeaturesQCOM *s);
void mvk_put_VkSamplerCubicWeightsCreateInfoQCOM(MvkEncoder *e, const VkSamplerCubicWeightsCreateInfoQCOM *s);
void mvk_get_VkSamplerCubicWeightsCreateInfoQCOM(MvkDecoder *d, VkSamplerCubicWeightsCreateInfoQCOM *s);
void mvk_put_VkBlitImageCubicWeightsInfoQCOM(MvkEncoder *e, const VkBlitImageCubicWeightsInfoQCOM *s);
void mvk_get_VkBlitImageCubicWeightsInfoQCOM(MvkDecoder *d, VkBlitImageCubicWeightsInfoQCOM *s);
void mvk_put_VkPhysicalDeviceImageProcessing2FeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessing2FeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceImageProcessing2FeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceImageProcessing2FeaturesQCOM *s);
void mvk_put_VkPhysicalDeviceImageProcessing2PropertiesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessing2PropertiesQCOM *s);
void mvk_get_VkPhysicalDeviceImageProcessing2PropertiesQCOM(MvkDecoder *d, VkPhysicalDeviceImageProcessing2PropertiesQCOM *s);
void mvk_put_VkSamplerBlockMatchWindowCreateInfoQCOM(MvkEncoder *e, const VkSamplerBlockMatchWindowCreateInfoQCOM *s);
void mvk_get_VkSamplerBlockMatchWindowCreateInfoQCOM(MvkDecoder *d, VkSamplerBlockMatchWindowCreateInfoQCOM *s);
void mvk_put_VkPhysicalDeviceImageProcessing3FeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceImageProcessing3FeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceImageProcessing3FeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceImageProcessing3FeaturesQCOM *s);
void mvk_put_VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV *s);
void mvk_get_VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV(MvkDecoder *d, VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV *s);
void mvk_put_VkPhysicalDeviceLayeredDriverPropertiesMSFT(MvkEncoder *e, const VkPhysicalDeviceLayeredDriverPropertiesMSFT *s);
void mvk_get_VkPhysicalDeviceLayeredDriverPropertiesMSFT(MvkDecoder *d, VkPhysicalDeviceLayeredDriverPropertiesMSFT *s);
void mvk_put_VkPhysicalDevicePerStageDescriptorSetFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePerStageDescriptorSetFeaturesNV *s);
void mvk_get_VkPhysicalDevicePerStageDescriptorSetFeaturesNV(MvkDecoder *d, VkPhysicalDevicePerStageDescriptorSetFeaturesNV *s);
void mvk_put_VkDeviceQueueShaderCoreControlCreateInfoARM(MvkEncoder *e, const VkDeviceQueueShaderCoreControlCreateInfoARM *s);
void mvk_get_VkDeviceQueueShaderCoreControlCreateInfoARM(MvkDecoder *d, VkDeviceQueueShaderCoreControlCreateInfoARM *s);
void mvk_put_VkPhysicalDeviceSchedulingControlsFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceSchedulingControlsFeaturesARM *s);
void mvk_get_VkPhysicalDeviceSchedulingControlsFeaturesARM(MvkDecoder *d, VkPhysicalDeviceSchedulingControlsFeaturesARM *s);
void mvk_put_VkPhysicalDeviceSchedulingControlsPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceSchedulingControlsPropertiesARM *s);
void mvk_get_VkPhysicalDeviceSchedulingControlsPropertiesARM(MvkDecoder *d, VkPhysicalDeviceSchedulingControlsPropertiesARM *s);
void mvk_put_VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM *s);
void mvk_get_VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM(MvkDecoder *d, VkPhysicalDeviceSchedulingControlsDispatchParametersPropertiesARM *s);
void mvk_put_VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG(MvkEncoder *e, const VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG *s);
void mvk_get_VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG(MvkDecoder *d, VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG *s);
void mvk_put_VkPhysicalDeviceRenderPassStripedFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceRenderPassStripedFeaturesARM *s);
void mvk_get_VkPhysicalDeviceRenderPassStripedFeaturesARM(MvkDecoder *d, VkPhysicalDeviceRenderPassStripedFeaturesARM *s);
void mvk_put_VkPhysicalDeviceRenderPassStripedPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceRenderPassStripedPropertiesARM *s);
void mvk_get_VkPhysicalDeviceRenderPassStripedPropertiesARM(MvkDecoder *d, VkPhysicalDeviceRenderPassStripedPropertiesARM *s);
void mvk_put_VkRenderPassStripeBeginInfoARM(MvkEncoder *e, const VkRenderPassStripeBeginInfoARM *s);
void mvk_get_VkRenderPassStripeBeginInfoARM(MvkDecoder *d, VkRenderPassStripeBeginInfoARM *s);
void mvk_put_VkRenderPassStripeInfoARM(MvkEncoder *e, const VkRenderPassStripeInfoARM *s);
void mvk_get_VkRenderPassStripeInfoARM(MvkDecoder *d, VkRenderPassStripeInfoARM *s);
void mvk_put_VkRenderPassStripeSubmitInfoARM(MvkEncoder *e, const VkRenderPassStripeSubmitInfoARM *s);
void mvk_get_VkRenderPassStripeSubmitInfoARM(MvkDecoder *d, VkRenderPassStripeSubmitInfoARM *s);
void mvk_put_VkPhysicalDevicePipelineOpacityMicromapFeaturesARM(MvkEncoder *e, const VkPhysicalDevicePipelineOpacityMicromapFeaturesARM *s);
void mvk_get_VkPhysicalDevicePipelineOpacityMicromapFeaturesARM(MvkDecoder *d, VkPhysicalDevicePipelineOpacityMicromapFeaturesARM *s);
void mvk_put_VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceShaderSubgroupRotateFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderSubgroupRotateFeatures *s);
void mvk_get_VkPhysicalDeviceShaderSubgroupRotateFeatures(MvkDecoder *d, VkPhysicalDeviceShaderSubgroupRotateFeatures *s);
void mvk_put_VkPhysicalDeviceShaderExpectAssumeFeatures(MvkEncoder *e, const VkPhysicalDeviceShaderExpectAssumeFeatures *s);
void mvk_get_VkPhysicalDeviceShaderExpectAssumeFeatures(MvkDecoder *d, VkPhysicalDeviceShaderExpectAssumeFeatures *s);
void mvk_put_VkPhysicalDeviceShaderFloatControls2Features(MvkEncoder *e, const VkPhysicalDeviceShaderFloatControls2Features *s);
void mvk_get_VkPhysicalDeviceShaderFloatControls2Features(MvkDecoder *d, VkPhysicalDeviceShaderFloatControls2Features *s);
void mvk_put_VkPhysicalDeviceDynamicRenderingLocalReadFeatures(MvkEncoder *e, const VkPhysicalDeviceDynamicRenderingLocalReadFeatures *s);
void mvk_get_VkPhysicalDeviceDynamicRenderingLocalReadFeatures(MvkDecoder *d, VkPhysicalDeviceDynamicRenderingLocalReadFeatures *s);
void mvk_put_VkPhysicalDeviceShaderQuadControlFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderQuadControlFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceShaderQuadControlFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderQuadControlFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV *s);
void mvk_get_VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV(MvkDecoder *d, VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV *s);
void mvk_put_VkPhysicalDeviceMapMemoryPlacedFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceMapMemoryPlacedFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceMapMemoryPlacedFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceMapMemoryPlacedFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceMapMemoryPlacedPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceMapMemoryPlacedPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceMapMemoryPlacedPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceMapMemoryPlacedPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceShaderBfloat16FeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderBfloat16FeaturesKHR *s);
void mvk_get_VkPhysicalDeviceShaderBfloat16FeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderBfloat16FeaturesKHR *s);
void mvk_put_VkPhysicalDeviceRawAccessChainsFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceRawAccessChainsFeaturesNV *s);
void mvk_get_VkPhysicalDeviceRawAccessChainsFeaturesNV(MvkDecoder *d, VkPhysicalDeviceRawAccessChainsFeaturesNV *s);
void mvk_put_VkPhysicalDeviceCommandBufferInheritanceFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCommandBufferInheritanceFeaturesNV *s);
void mvk_get_VkPhysicalDeviceCommandBufferInheritanceFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCommandBufferInheritanceFeaturesNV *s);
void mvk_put_VkPhysicalDeviceImageAlignmentControlFeaturesMESA(MvkEncoder *e, const VkPhysicalDeviceImageAlignmentControlFeaturesMESA *s);
void mvk_get_VkPhysicalDeviceImageAlignmentControlFeaturesMESA(MvkDecoder *d, VkPhysicalDeviceImageAlignmentControlFeaturesMESA *s);
void mvk_put_VkPhysicalDeviceImageAlignmentControlPropertiesMESA(MvkEncoder *e, const VkPhysicalDeviceImageAlignmentControlPropertiesMESA *s);
void mvk_get_VkPhysicalDeviceImageAlignmentControlPropertiesMESA(MvkDecoder *d, VkPhysicalDeviceImageAlignmentControlPropertiesMESA *s);
void mvk_put_VkImageAlignmentControlCreateInfoMESA(MvkEncoder *e, const VkImageAlignmentControlCreateInfoMESA *s);
void mvk_get_VkImageAlignmentControlCreateInfoMESA(MvkDecoder *d, VkImageAlignmentControlCreateInfoMESA *s);
void mvk_put_VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT *s);
void mvk_put_VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR(MvkEncoder *e, const VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR *s);
void mvk_get_VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR(MvkDecoder *d, VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceCooperativeMatrix2FeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrix2FeaturesNV *s);
void mvk_get_VkPhysicalDeviceCooperativeMatrix2FeaturesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrix2FeaturesNV *s);
void mvk_put_VkPhysicalDeviceCooperativeMatrix2PropertiesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrix2PropertiesNV *s);
void mvk_get_VkPhysicalDeviceCooperativeMatrix2PropertiesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrix2PropertiesNV *s);
void mvk_put_VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV *s);
void mvk_get_VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV *s);
void mvk_put_VkPhysicalDeviceHdrVividFeaturesHUAWEI(MvkEncoder *e, const VkPhysicalDeviceHdrVividFeaturesHUAWEI *s);
void mvk_get_VkPhysicalDeviceHdrVividFeaturesHUAWEI(MvkDecoder *d, VkPhysicalDeviceHdrVividFeaturesHUAWEI *s);
void mvk_put_VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceDepthClampZeroOneFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceDepthClampZeroOneFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceDepthClampZeroOneFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceDepthClampZeroOneFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceCooperativeVectorFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeVectorFeaturesNV *s);
void mvk_get_VkPhysicalDeviceCooperativeVectorFeaturesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeVectorFeaturesNV *s);
void mvk_put_VkPhysicalDeviceCooperativeVectorPropertiesNV(MvkEncoder *e, const VkPhysicalDeviceCooperativeVectorPropertiesNV *s);
void mvk_get_VkPhysicalDeviceCooperativeVectorPropertiesNV(MvkDecoder *d, VkPhysicalDeviceCooperativeVectorPropertiesNV *s);
void mvk_put_VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE *s);
void mvk_get_VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMapLayeredPropertiesVALVE *s);
void mvk_put_VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE *s);
void mvk_get_VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE(MvkDecoder *d, VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE *s);
void mvk_put_VkPipelineFragmentDensityMapLayeredCreateInfoVALVE(MvkEncoder *e, const VkPipelineFragmentDensityMapLayeredCreateInfoVALVE *s);
void mvk_get_VkPipelineFragmentDensityMapLayeredCreateInfoVALVE(MvkDecoder *d, VkPipelineFragmentDensityMapLayeredCreateInfoVALVE *s);
void mvk_put_VkPhysicalDevicePresentMeteringFeaturesNV(MvkEncoder *e, const VkPhysicalDevicePresentMeteringFeaturesNV *s);
void mvk_get_VkPhysicalDevicePresentMeteringFeaturesNV(MvkDecoder *d, VkPhysicalDevicePresentMeteringFeaturesNV *s);
void mvk_put_VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE *s);
void mvk_get_VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE(MvkDecoder *d, VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE *s);
void mvk_put_VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT(MvkEncoder *e, const VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT *s);
void mvk_get_VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT(MvkDecoder *d, VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceFormatPackFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceFormatPackFeaturesARM *s);
void mvk_get_VkPhysicalDeviceFormatPackFeaturesARM(MvkDecoder *d, VkPhysicalDeviceFormatPackFeaturesARM *s);
void mvk_put_VkPhysicalDeviceThrottleHintFeaturesSEC(MvkEncoder *e, const VkPhysicalDeviceThrottleHintFeaturesSEC *s);
void mvk_get_VkPhysicalDeviceThrottleHintFeaturesSEC(MvkDecoder *d, VkPhysicalDeviceThrottleHintFeaturesSEC *s);
void mvk_put_VkThrottleHintSubmitInfoSEC(MvkEncoder *e, const VkThrottleHintSubmitInfoSEC *s);
void mvk_get_VkThrottleHintSubmitInfoSEC(MvkDecoder *d, VkThrottleHintSubmitInfoSEC *s);
void mvk_put_VkPhysicalDeviceShaderFloat8FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderFloat8FeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderFloat8FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderFloat8FeaturesEXT *s);
void mvk_put_VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC(MvkEncoder *e, const VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC *s);
void mvk_get_VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC(MvkDecoder *d, VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC *s);
void mvk_put_VkPhysicalDeviceDataGraphModelFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceDataGraphModelFeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceDataGraphModelFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceDataGraphModelFeaturesQCOM *s);
void mvk_put_VkPhysicalDeviceShaderUntypedPointersFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderUntypedPointersFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceShaderUntypedPointersFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderUntypedPointersFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE *s);
void mvk_get_VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE(MvkDecoder *d, VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE *s);
void mvk_put_VkPhysicalDeviceShader64BitIndexingFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShader64BitIndexingFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShader64BitIndexingFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShader64BitIndexingFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceQueuePerfHintFeaturesQCOM(MvkEncoder *e, const VkPhysicalDeviceQueuePerfHintFeaturesQCOM *s);
void mvk_get_VkPhysicalDeviceQueuePerfHintFeaturesQCOM(MvkDecoder *d, VkPhysicalDeviceQueuePerfHintFeaturesQCOM *s);
void mvk_put_VkPhysicalDeviceQueuePerfHintPropertiesQCOM(MvkEncoder *e, const VkPhysicalDeviceQueuePerfHintPropertiesQCOM *s);
void mvk_get_VkPhysicalDeviceQueuePerfHintPropertiesQCOM(MvkDecoder *d, VkPhysicalDeviceQueuePerfHintPropertiesQCOM *s);
void mvk_put_VkPhysicalDevicePerformanceCountersByRegionFeaturesARM(MvkEncoder *e, const VkPhysicalDevicePerformanceCountersByRegionFeaturesARM *s);
void mvk_get_VkPhysicalDevicePerformanceCountersByRegionFeaturesARM(MvkDecoder *d, VkPhysicalDevicePerformanceCountersByRegionFeaturesARM *s);
void mvk_put_VkPhysicalDevicePerformanceCountersByRegionPropertiesARM(MvkEncoder *e, const VkPhysicalDevicePerformanceCountersByRegionPropertiesARM *s);
void mvk_get_VkPhysicalDevicePerformanceCountersByRegionPropertiesARM(MvkDecoder *d, VkPhysicalDevicePerformanceCountersByRegionPropertiesARM *s);
void mvk_put_VkRenderPassPerformanceCountersByRegionBeginInfoARM(MvkEncoder *e, const VkRenderPassPerformanceCountersByRegionBeginInfoARM *s);
void mvk_get_VkRenderPassPerformanceCountersByRegionBeginInfoARM(MvkDecoder *d, VkRenderPassPerformanceCountersByRegionBeginInfoARM *s);
void mvk_put_VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV(MvkEncoder *e, const VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV *s);
void mvk_get_VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV(MvkDecoder *d, VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV *s);
void mvk_put_VkPhysicalDeviceShaderLongVectorFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderLongVectorFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderLongVectorFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderLongVectorFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceShaderLongVectorPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderLongVectorPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceShaderLongVectorPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceShaderLongVectorPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT *s);
void mvk_put_VkShaderDescriptorSetAndBindingMappingInfoEXT(MvkEncoder *e, const VkShaderDescriptorSetAndBindingMappingInfoEXT *s);
void mvk_get_VkShaderDescriptorSetAndBindingMappingInfoEXT(MvkDecoder *d, VkShaderDescriptorSetAndBindingMappingInfoEXT *s);
void mvk_put_VkDescriptorSetAndBindingMappingEXT(MvkEncoder *e, const VkDescriptorSetAndBindingMappingEXT *s);
void mvk_get_VkDescriptorSetAndBindingMappingEXT(MvkDecoder *d, VkDescriptorSetAndBindingMappingEXT *s);
void mvk_put_VkDescriptorMappingSourceDataEXT(MvkEncoder *e, const VkDescriptorMappingSourceDataEXT *s);
void mvk_get_VkDescriptorMappingSourceDataEXT(MvkDecoder *d, VkDescriptorMappingSourceDataEXT *s);
void mvk_put_VkDescriptorMappingSourceConstantOffsetEXT(MvkEncoder *e, const VkDescriptorMappingSourceConstantOffsetEXT *s);
void mvk_get_VkDescriptorMappingSourceConstantOffsetEXT(MvkDecoder *d, VkDescriptorMappingSourceConstantOffsetEXT *s);
void mvk_put_VkDescriptorMappingSourcePushIndexEXT(MvkEncoder *e, const VkDescriptorMappingSourcePushIndexEXT *s);
void mvk_get_VkDescriptorMappingSourcePushIndexEXT(MvkDecoder *d, VkDescriptorMappingSourcePushIndexEXT *s);
void mvk_put_VkDescriptorMappingSourceIndirectIndexEXT(MvkEncoder *e, const VkDescriptorMappingSourceIndirectIndexEXT *s);
void mvk_get_VkDescriptorMappingSourceIndirectIndexEXT(MvkDecoder *d, VkDescriptorMappingSourceIndirectIndexEXT *s);
void mvk_put_VkDescriptorMappingSourceIndirectIndexArrayEXT(MvkEncoder *e, const VkDescriptorMappingSourceIndirectIndexArrayEXT *s);
void mvk_get_VkDescriptorMappingSourceIndirectIndexArrayEXT(MvkDecoder *d, VkDescriptorMappingSourceIndirectIndexArrayEXT *s);
void mvk_put_VkDescriptorMappingSourceHeapDataEXT(MvkEncoder *e, const VkDescriptorMappingSourceHeapDataEXT *s);
void mvk_get_VkDescriptorMappingSourceHeapDataEXT(MvkDecoder *d, VkDescriptorMappingSourceHeapDataEXT *s);
void mvk_put_VkDescriptorMappingSourceIndirectAddressEXT(MvkEncoder *e, const VkDescriptorMappingSourceIndirectAddressEXT *s);
void mvk_get_VkDescriptorMappingSourceIndirectAddressEXT(MvkDecoder *d, VkDescriptorMappingSourceIndirectAddressEXT *s);
void mvk_put_VkDescriptorMappingSourceShaderRecordIndexEXT(MvkEncoder *e, const VkDescriptorMappingSourceShaderRecordIndexEXT *s);
void mvk_get_VkDescriptorMappingSourceShaderRecordIndexEXT(MvkDecoder *d, VkDescriptorMappingSourceShaderRecordIndexEXT *s);
void mvk_put_VkSamplerCustomBorderColorIndexCreateInfoEXT(MvkEncoder *e, const VkSamplerCustomBorderColorIndexCreateInfoEXT *s);
void mvk_get_VkSamplerCustomBorderColorIndexCreateInfoEXT(MvkDecoder *d, VkSamplerCustomBorderColorIndexCreateInfoEXT *s);
void mvk_put_VkOpaqueCaptureDataCreateInfoEXT(MvkEncoder *e, const VkOpaqueCaptureDataCreateInfoEXT *s);
void mvk_get_VkOpaqueCaptureDataCreateInfoEXT(MvkDecoder *d, VkOpaqueCaptureDataCreateInfoEXT *s);
void mvk_put_VkIndirectCommandsLayoutPushDataTokenNV(MvkEncoder *e, const VkIndirectCommandsLayoutPushDataTokenNV *s);
void mvk_get_VkIndirectCommandsLayoutPushDataTokenNV(MvkDecoder *d, VkIndirectCommandsLayoutPushDataTokenNV *s);
void mvk_put_VkSubsampledImageFormatPropertiesEXT(MvkEncoder *e, const VkSubsampledImageFormatPropertiesEXT *s);
void mvk_get_VkSubsampledImageFormatPropertiesEXT(MvkDecoder *d, VkSubsampledImageFormatPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceShaderSplitBarrierFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderSplitBarrierFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderSplitBarrierFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderSplitBarrierFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceShaderSplitBarrierPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderSplitBarrierPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceShaderSplitBarrierPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceShaderSplitBarrierPropertiesEXT *s);
void mvk_put_VkPhysicalDeviceDescriptorHeapFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorHeapFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceDescriptorHeapFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceDescriptorHeapFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceDescriptorHeapPropertiesEXT(MvkEncoder *e, const VkPhysicalDeviceDescriptorHeapPropertiesEXT *s);
void mvk_get_VkPhysicalDeviceDescriptorHeapPropertiesEXT(MvkDecoder *d, VkPhysicalDeviceDescriptorHeapPropertiesEXT *s);
void mvk_put_VkCommandBufferInheritanceDescriptorHeapInfoEXT(MvkEncoder *e, const VkCommandBufferInheritanceDescriptorHeapInfoEXT *s);
void mvk_get_VkCommandBufferInheritanceDescriptorHeapInfoEXT(MvkDecoder *d, VkCommandBufferInheritanceDescriptorHeapInfoEXT *s);
void mvk_put_VkPhysicalDeviceDescriptorHeapTensorPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceDescriptorHeapTensorPropertiesARM *s);
void mvk_get_VkPhysicalDeviceDescriptorHeapTensorPropertiesARM(MvkDecoder *d, VkPhysicalDeviceDescriptorHeapTensorPropertiesARM *s);
void mvk_put_VkPhysicalDeviceShaderInstrumentationFeaturesARM(MvkEncoder *e, const VkPhysicalDeviceShaderInstrumentationFeaturesARM *s);
void mvk_get_VkPhysicalDeviceShaderInstrumentationFeaturesARM(MvkDecoder *d, VkPhysicalDeviceShaderInstrumentationFeaturesARM *s);
void mvk_put_VkPhysicalDeviceShaderInstrumentationPropertiesARM(MvkEncoder *e, const VkPhysicalDeviceShaderInstrumentationPropertiesARM *s);
void mvk_get_VkPhysicalDeviceShaderInstrumentationPropertiesARM(MvkDecoder *d, VkPhysicalDeviceShaderInstrumentationPropertiesARM *s);
void mvk_put_VkMemoryRangeBarriersInfoKHR(MvkEncoder *e, const VkMemoryRangeBarriersInfoKHR *s);
void mvk_get_VkMemoryRangeBarriersInfoKHR(MvkDecoder *d, VkMemoryRangeBarriersInfoKHR *s);
void mvk_put_VkMemoryRangeBarrierKHR(MvkEncoder *e, const VkMemoryRangeBarrierKHR *s);
void mvk_get_VkMemoryRangeBarrierKHR(MvkDecoder *d, VkMemoryRangeBarrierKHR *s);
void mvk_put_VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceShaderConstantDataFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderConstantDataFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceShaderConstantDataFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderConstantDataFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceShaderAbortFeaturesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderAbortFeaturesKHR *s);
void mvk_get_VkPhysicalDeviceShaderAbortFeaturesKHR(MvkDecoder *d, VkPhysicalDeviceShaderAbortFeaturesKHR *s);
void mvk_put_VkPhysicalDeviceShaderAbortPropertiesKHR(MvkEncoder *e, const VkPhysicalDeviceShaderAbortPropertiesKHR *s);
void mvk_get_VkPhysicalDeviceShaderAbortPropertiesKHR(MvkDecoder *d, VkPhysicalDeviceShaderAbortPropertiesKHR *s);
void mvk_put_VkDeviceFaultShaderAbortMessageInfoKHR(MvkEncoder *e, const VkDeviceFaultShaderAbortMessageInfoKHR *s);
void mvk_get_VkDeviceFaultShaderAbortMessageInfoKHR(MvkDecoder *d, VkDeviceFaultShaderAbortMessageInfoKHR *s);
void mvk_put_VkPhysicalDeviceImageTilingControlFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceImageTilingControlFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceImageTilingControlFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceImageTilingControlFeaturesEXT *s);
void mvk_put_VkImageTilingControlCreateInfoEXT(MvkEncoder *e, const VkImageTilingControlCreateInfoEXT *s);
void mvk_get_VkImageTilingControlCreateInfoEXT(MvkDecoder *d, VkImageTilingControlCreateInfoEXT *s);
void mvk_put_VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT *s);
void mvk_get_VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT(MvkDecoder *d, VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT *s);
void mvk_put_VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT(MvkEncoder *e, const VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT *s);
void mvk_get_VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT(MvkDecoder *d, VkPhysicalDeviceCooperativeMatrixMaintenance1FeaturesEXT *s);
void mvk_put_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE(MvkEncoder *e, const VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE *s);
void mvk_get_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE(MvkDecoder *d, VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentFeaturesVALVE *s);
void mvk_put_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE(MvkEncoder *e, const VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE *s);
void mvk_get_VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE(MvkDecoder *d, VkPhysicalDeviceBufferDeviceAddressAllocationAlignmentPropertiesVALVE *s);
void mvk_put_VkBufferDeviceAddressAlignmentAllocateInfoVALVE(MvkEncoder *e, const VkBufferDeviceAddressAlignmentAllocateInfoVALVE *s);
void mvk_get_VkBufferDeviceAddressAlignmentAllocateInfoVALVE(MvkDecoder *d, VkBufferDeviceAddressAlignmentAllocateInfoVALVE *s);
void mvk_put_VkPhysicalDeviceInfoPropertiesINTEL(MvkEncoder *e, const VkPhysicalDeviceInfoPropertiesINTEL *s);
void mvk_get_VkPhysicalDeviceInfoPropertiesINTEL(MvkDecoder *d, VkPhysicalDeviceInfoPropertiesINTEL *s);

/* the size of a struct from its sType, 0 when the protocol does not know it. */
size_t mvk_struct_size(VkStructureType type);
void mvk_put_chain_struct(MvkEncoder *e, const VkBaseInStructure *s);
void *mvk_get_chain_struct(MvkDecoder *d, VkStructureType type);

/* the side specific parts the codecs call. */
void mvk_put_chain(MvkEncoder *e, const void *next);
void *mvk_get_chain(MvkDecoder *d);
uint64_t mvk_get_handle(MvkDecoder *d, int dispatchable);
uint64_t mvk_dispatch_id(const void *object);

/* the extensions the protocol carries whole, NULL terminated. */
extern const char *const mvk_supported_extensions[];

#endif
