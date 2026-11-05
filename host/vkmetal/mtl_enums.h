/* Metal enumeration values for C, printed from the macOS SDK headers. */
#ifndef MIME_VKMETAL_MTL_ENUMS_H
#define MIME_VKMETAL_MTL_ENUMS_H

/* MTLPixelFormat. */
enum {
    MTLPixelFormatInvalid = 0x0,
    MTLPixelFormatA8Unorm = 0x1,
    MTLPixelFormatR8Unorm = 0xa,
    MTLPixelFormatR8Unorm_sRGB = 0xb,
    MTLPixelFormatR8Snorm = 0xc,
    MTLPixelFormatR8Uint = 0xd,
    MTLPixelFormatR8Sint = 0xe,
    MTLPixelFormatR16Unorm = 0x14,
    MTLPixelFormatR16Snorm = 0x16,
    MTLPixelFormatR16Uint = 0x17,
    MTLPixelFormatR16Sint = 0x18,
    MTLPixelFormatR16Float = 0x19,
    MTLPixelFormatRG8Unorm = 0x1e,
    MTLPixelFormatRG8Unorm_sRGB = 0x1f,
    MTLPixelFormatRG8Snorm = 0x20,
    MTLPixelFormatRG8Uint = 0x21,
    MTLPixelFormatRG8Sint = 0x22,
    MTLPixelFormatB5G6R5Unorm = 0x28,
    MTLPixelFormatA1BGR5Unorm = 0x29,
    MTLPixelFormatABGR4Unorm = 0x2a,
    MTLPixelFormatBGR5A1Unorm = 0x2b,
    MTLPixelFormatRGB8Unorm = 0x2d,
    MTLPixelFormatRGB8Snorm = 0x2e,
    MTLPixelFormatRGB8Uint = 0x2f,
    MTLPixelFormatRGB8Sint = 0x30,
    MTLPixelFormatR32Uint = 0x35,
    MTLPixelFormatR32Sint = 0x36,
    MTLPixelFormatR32Float = 0x37,
    MTLPixelFormatRG16Unorm = 0x3c,
    MTLPixelFormatRG16Snorm = 0x3e,
    MTLPixelFormatRG16Uint = 0x3f,
    MTLPixelFormatRG16Sint = 0x40,
    MTLPixelFormatRG16Float = 0x41,
    MTLPixelFormatRGBA8Unorm = 0x46,
    MTLPixelFormatRGBA8Unorm_sRGB = 0x47,
    MTLPixelFormatRGBA8Snorm = 0x48,
    MTLPixelFormatRGBA8Uint = 0x49,
    MTLPixelFormatRGBA8Sint = 0x4a,
    MTLPixelFormatBGRA8Unorm = 0x50,
    MTLPixelFormatBGRA8Unorm_sRGB = 0x51,
    MTLPixelFormatRGB10A2Unorm = 0x5a,
    MTLPixelFormatRGB10A2Uint = 0x5b,
    MTLPixelFormatRG11B10Float = 0x5c,
    MTLPixelFormatRGB9E5Float = 0x5d,
    MTLPixelFormatBGR10A2Unorm = 0x5e,
    MTLPixelFormatBGR10_XR = 0x22a,
    MTLPixelFormatBGR10_XR_sRGB = 0x22b,
    MTLPixelFormatRGB16Unorm = 0x5f,
    MTLPixelFormatRGB16Snorm = 0x60,
    MTLPixelFormatRGB16Uint = 0x61,
    MTLPixelFormatRGB16Sint = 0x62,
    MTLPixelFormatRGB16Float = 0x63,
    MTLPixelFormatRG32Uint = 0x67,
    MTLPixelFormatRG32Sint = 0x68,
    MTLPixelFormatRG32Float = 0x69,
    MTLPixelFormatRGBA16Unorm = 0x6e,
    MTLPixelFormatRGBA16Snorm = 0x70,
    MTLPixelFormatRGBA16Uint = 0x71,
    MTLPixelFormatRGBA16Sint = 0x72,
    MTLPixelFormatRGBA16Float = 0x73,
    MTLPixelFormatBGRA10_XR = 0x228,
    MTLPixelFormatBGRA10_XR_sRGB = 0x229,
    MTLPixelFormatRGB32Uint = 0x78,
    MTLPixelFormatRGB32Sint = 0x79,
    MTLPixelFormatRGB32Float = 0x7a,
    MTLPixelFormatRGBA32Uint = 0x7b,
    MTLPixelFormatRGBA32Sint = 0x7c,
    MTLPixelFormatRGBA32Float = 0x7d,
    MTLPixelFormatBC1_RGBA = 0x82,
    MTLPixelFormatBC1_RGBA_sRGB = 0x83,
    MTLPixelFormatBC2_RGBA = 0x84,
    MTLPixelFormatBC2_RGBA_sRGB = 0x85,
    MTLPixelFormatBC3_RGBA = 0x86,
    MTLPixelFormatBC3_RGBA_sRGB = 0x87,
    MTLPixelFormatBC4_RUnorm = 0x8c,
    MTLPixelFormatBC4_RSnorm = 0x8d,
    MTLPixelFormatBC5_RGUnorm = 0x8e,
    MTLPixelFormatBC5_RGSnorm = 0x8f,
    MTLPixelFormatBC6H_RGBFloat = 0x96,
    MTLPixelFormatBC6H_RGBUfloat = 0x97,
    MTLPixelFormatBC7_RGBAUnorm = 0x98,
    MTLPixelFormatBC7_RGBAUnorm_sRGB = 0x99,
    MTLPixelFormatPVRTC_RGB_2BPP = 0xa0,
    MTLPixelFormatPVRTC_RGB_2BPP_sRGB = 0xa1,
    MTLPixelFormatPVRTC_RGB_4BPP = 0xa2,
    MTLPixelFormatPVRTC_RGB_4BPP_sRGB = 0xa3,
    MTLPixelFormatPVRTC_RGBA_2BPP = 0xa4,
    MTLPixelFormatPVRTC_RGBA_2BPP_sRGB = 0xa5,
    MTLPixelFormatPVRTC_RGBA_4BPP = 0xa6,
    MTLPixelFormatPVRTC_RGBA_4BPP_sRGB = 0xa7,
    MTLPixelFormatEAC_R11Unorm = 0xaa,
    MTLPixelFormatEAC_R11Snorm = 0xac,
    MTLPixelFormatEAC_RG11Unorm = 0xae,
    MTLPixelFormatEAC_RG11Snorm = 0xb0,
    MTLPixelFormatEAC_RGBA8 = 0xb2,
    MTLPixelFormatEAC_RGBA8_sRGB = 0xb3,
    MTLPixelFormatETC2_RGB8 = 0xb4,
    MTLPixelFormatETC2_RGB8_sRGB = 0xb5,
    MTLPixelFormatETC2_RGB8A1 = 0xb6,
    MTLPixelFormatETC2_RGB8A1_sRGB = 0xb7,
    MTLPixelFormatASTC_4x4_sRGB = 0xba,
    MTLPixelFormatASTC_5x4_sRGB = 0xbb,
    MTLPixelFormatASTC_5x5_sRGB = 0xbc,
    MTLPixelFormatASTC_6x5_sRGB = 0xbd,
    MTLPixelFormatASTC_6x6_sRGB = 0xbe,
    MTLPixelFormatASTC_8x5_sRGB = 0xc0,
    MTLPixelFormatASTC_8x6_sRGB = 0xc1,
    MTLPixelFormatASTC_8x8_sRGB = 0xc2,
    MTLPixelFormatASTC_10x5_sRGB = 0xc3,
    MTLPixelFormatASTC_10x6_sRGB = 0xc4,
    MTLPixelFormatASTC_10x8_sRGB = 0xc5,
    MTLPixelFormatASTC_10x10_sRGB = 0xc6,
    MTLPixelFormatASTC_12x10_sRGB = 0xc7,
    MTLPixelFormatASTC_12x12_sRGB = 0xc8,
    MTLPixelFormatASTC_4x4_LDR = 0xcc,
    MTLPixelFormatASTC_5x4_LDR = 0xcd,
    MTLPixelFormatASTC_5x5_LDR = 0xce,
    MTLPixelFormatASTC_6x5_LDR = 0xcf,
    MTLPixelFormatASTC_6x6_LDR = 0xd0,
    MTLPixelFormatASTC_8x5_LDR = 0xd2,
    MTLPixelFormatASTC_8x6_LDR = 0xd3,
    MTLPixelFormatASTC_8x8_LDR = 0xd4,
    MTLPixelFormatASTC_10x5_LDR = 0xd5,
    MTLPixelFormatASTC_10x6_LDR = 0xd6,
    MTLPixelFormatASTC_10x8_LDR = 0xd7,
    MTLPixelFormatASTC_10x10_LDR = 0xd8,
    MTLPixelFormatASTC_12x10_LDR = 0xd9,
    MTLPixelFormatASTC_12x12_LDR = 0xda,
    MTLPixelFormatASTC_4x4_HDR = 0xde,
    MTLPixelFormatASTC_5x4_HDR = 0xdf,
    MTLPixelFormatASTC_5x5_HDR = 0xe0,
    MTLPixelFormatASTC_6x5_HDR = 0xe1,
    MTLPixelFormatASTC_6x6_HDR = 0xe2,
    MTLPixelFormatASTC_8x5_HDR = 0xe4,
    MTLPixelFormatASTC_8x6_HDR = 0xe5,
    MTLPixelFormatASTC_8x8_HDR = 0xe6,
    MTLPixelFormatASTC_10x5_HDR = 0xe7,
    MTLPixelFormatASTC_10x6_HDR = 0xe8,
    MTLPixelFormatASTC_10x8_HDR = 0xe9,
    MTLPixelFormatASTC_10x10_HDR = 0xea,
    MTLPixelFormatASTC_12x10_HDR = 0xeb,
    MTLPixelFormatASTC_12x12_HDR = 0xec,
    MTLPixelFormatGBGR422 = 0xf0,
    MTLPixelFormatBGRG422 = 0xf1,
    MTLPixelFormatDepth16Unorm = 0xfa,
    MTLPixelFormatDepth32Float = 0xfc,
    MTLPixelFormatStencil8 = 0xfd,
    MTLPixelFormatDepth24Unorm_Stencil8 = 0xff,
    MTLPixelFormatDepth32Float_Stencil8 = 0x104,
    MTLPixelFormatX32_Stencil8 = 0x105,
    MTLPixelFormatX24_Stencil8 = 0x106,
    MTLPixelFormatUnspecialized = 0x107,
};

/* MTLVertexFormat. */
enum {
    MTLVertexFormatInvalid = 0x0,
    MTLVertexFormatUChar2 = 0x1,
    MTLVertexFormatUChar3 = 0x2,
    MTLVertexFormatUChar4 = 0x3,
    MTLVertexFormatChar2 = 0x4,
    MTLVertexFormatChar3 = 0x5,
    MTLVertexFormatChar4 = 0x6,
    MTLVertexFormatUChar2Normalized = 0x7,
    MTLVertexFormatUChar3Normalized = 0x8,
    MTLVertexFormatUChar4Normalized = 0x9,
    MTLVertexFormatChar2Normalized = 0xa,
    MTLVertexFormatChar3Normalized = 0xb,
    MTLVertexFormatChar4Normalized = 0xc,
    MTLVertexFormatUShort2 = 0xd,
    MTLVertexFormatUShort3 = 0xe,
    MTLVertexFormatUShort4 = 0xf,
    MTLVertexFormatShort2 = 0x10,
    MTLVertexFormatShort3 = 0x11,
    MTLVertexFormatShort4 = 0x12,
    MTLVertexFormatUShort2Normalized = 0x13,
    MTLVertexFormatUShort3Normalized = 0x14,
    MTLVertexFormatUShort4Normalized = 0x15,
    MTLVertexFormatShort2Normalized = 0x16,
    MTLVertexFormatShort3Normalized = 0x17,
    MTLVertexFormatShort4Normalized = 0x18,
    MTLVertexFormatHalf2 = 0x19,
    MTLVertexFormatHalf3 = 0x1a,
    MTLVertexFormatHalf4 = 0x1b,
    MTLVertexFormatFloat = 0x1c,
    MTLVertexFormatFloat2 = 0x1d,
    MTLVertexFormatFloat3 = 0x1e,
    MTLVertexFormatFloat4 = 0x1f,
    MTLVertexFormatInt = 0x20,
    MTLVertexFormatInt2 = 0x21,
    MTLVertexFormatInt3 = 0x22,
    MTLVertexFormatInt4 = 0x23,
    MTLVertexFormatUInt = 0x24,
    MTLVertexFormatUInt2 = 0x25,
    MTLVertexFormatUInt3 = 0x26,
    MTLVertexFormatUInt4 = 0x27,
    MTLVertexFormatInt1010102Normalized = 0x28,
    MTLVertexFormatUInt1010102Normalized = 0x29,
    MTLVertexFormatUChar4Normalized_BGRA = 0x2a,
    MTLVertexFormatUChar = 0x2d,
    MTLVertexFormatChar = 0x2e,
    MTLVertexFormatUCharNormalized = 0x2f,
    MTLVertexFormatCharNormalized = 0x30,
    MTLVertexFormatUShort = 0x31,
    MTLVertexFormatShort = 0x32,
    MTLVertexFormatUShortNormalized = 0x33,
    MTLVertexFormatShortNormalized = 0x34,
    MTLVertexFormatHalf = 0x35,
    MTLVertexFormatFloatRG11B10 = 0x36,
    MTLVertexFormatFloatRGB9E5 = 0x37,
};

/* MTLVertexStepFunction. */
enum {
    MTLVertexStepFunctionConstant = 0x0,
    MTLVertexStepFunctionPerVertex = 0x1,
    MTLVertexStepFunctionPerInstance = 0x2,
    MTLVertexStepFunctionPerPatch = 0x3,
    MTLVertexStepFunctionPerPatchControlPoint = 0x4,
};

/* MTLBlendFactor. */
enum {
    MTLBlendFactorZero = 0x0,
    MTLBlendFactorOne = 0x1,
    MTLBlendFactorSourceColor = 0x2,
    MTLBlendFactorOneMinusSourceColor = 0x3,
    MTLBlendFactorSourceAlpha = 0x4,
    MTLBlendFactorOneMinusSourceAlpha = 0x5,
    MTLBlendFactorDestinationColor = 0x6,
    MTLBlendFactorOneMinusDestinationColor = 0x7,
    MTLBlendFactorDestinationAlpha = 0x8,
    MTLBlendFactorOneMinusDestinationAlpha = 0x9,
    MTLBlendFactorSourceAlphaSaturated = 0xa,
    MTLBlendFactorBlendColor = 0xb,
    MTLBlendFactorOneMinusBlendColor = 0xc,
    MTLBlendFactorBlendAlpha = 0xd,
    MTLBlendFactorOneMinusBlendAlpha = 0xe,
    MTLBlendFactorSource1Color = 0xf,
    MTLBlendFactorOneMinusSource1Color = 0x10,
    MTLBlendFactorSource1Alpha = 0x11,
    MTLBlendFactorOneMinusSource1Alpha = 0x12,
    MTLBlendFactorUnspecialized = 0x13,
};

/* MTLBlendOperation. */
enum {
    MTLBlendOperationAdd = 0x0,
    MTLBlendOperationSubtract = 0x1,
    MTLBlendOperationReverseSubtract = 0x2,
    MTLBlendOperationMin = 0x3,
    MTLBlendOperationMax = 0x4,
    MTLBlendOperationUnspecialized = 0x5,
};

/* MTLColorWriteMask. */
enum {
    MTLColorWriteMaskNone = 0x0,
    MTLColorWriteMaskRed = 0x8,
    MTLColorWriteMaskGreen = 0x4,
    MTLColorWriteMaskBlue = 0x2,
    MTLColorWriteMaskAlpha = 0x1,
    MTLColorWriteMaskAll = 0xf,
    MTLColorWriteMaskUnspecialized = 0x10,
};

/* MTLCompareFunction. */
enum {
    MTLCompareFunctionNever = 0x0,
    MTLCompareFunctionLess = 0x1,
    MTLCompareFunctionEqual = 0x2,
    MTLCompareFunctionLessEqual = 0x3,
    MTLCompareFunctionGreater = 0x4,
    MTLCompareFunctionNotEqual = 0x5,
    MTLCompareFunctionGreaterEqual = 0x6,
    MTLCompareFunctionAlways = 0x7,
};

/* MTLStencilOperation. */
enum {
    MTLStencilOperationKeep = 0x0,
    MTLStencilOperationZero = 0x1,
    MTLStencilOperationReplace = 0x2,
    MTLStencilOperationIncrementClamp = 0x3,
    MTLStencilOperationDecrementClamp = 0x4,
    MTLStencilOperationInvert = 0x5,
    MTLStencilOperationIncrementWrap = 0x6,
    MTLStencilOperationDecrementWrap = 0x7,
};

/* MTLStages. */
enum {
    MTLStageVertex = 0x1,
    MTLStageFragment = 0x2,
    MTLStageTile = 0x4,
    MTLStageObject = 0x8,
    MTLStageMesh = 0x10,
    MTLStageResourceState = 0x4000000,
    MTLStageDispatch = 0x8000000,
    MTLStageBlit = 0x10000000,
    MTLStageAccelerationStructure = 0x20000000,
    MTLStageMachineLearning = 0x40000000,
    MTLStageAll = 0x7fffffffffffffff,
};

/* MTLRenderStages. */
enum {
    MTLRenderStageVertex = 0x1,
    MTLRenderStageFragment = 0x2,
    MTLRenderStageTile = 0x4,
    MTLRenderStageObject = 0x8,
    MTLRenderStageMesh = 0x10,
};

/* MTLLoadAction. */
enum {
    MTLLoadActionDontCare = 0x0,
    MTLLoadActionLoad = 0x1,
    MTLLoadActionClear = 0x2,
};

/* MTLStoreAction. */
enum {
    MTLStoreActionDontCare = 0x0,
    MTLStoreActionStore = 0x1,
    MTLStoreActionMultisampleResolve = 0x2,
    MTLStoreActionStoreAndMultisampleResolve = 0x3,
    MTLStoreActionUnknown = 0x4,
    MTLStoreActionCustomSampleDepthStore = 0x5,
};

/* MTLTextureType. */
enum {
    MTLTextureType1D = 0x0,
    MTLTextureType1DArray = 0x1,
    MTLTextureType2D = 0x2,
    MTLTextureType2DArray = 0x3,
    MTLTextureType2DMultisample = 0x4,
    MTLTextureTypeCube = 0x5,
    MTLTextureTypeCubeArray = 0x6,
    MTLTextureType3D = 0x7,
    MTLTextureType2DMultisampleArray = 0x8,
    MTLTextureTypeTextureBuffer = 0x9,
};

/* MTLTextureUsage. */
enum {
    MTLTextureUsageUnknown = 0x0,
    MTLTextureUsageShaderRead = 0x1,
    MTLTextureUsageShaderWrite = 0x2,
    MTLTextureUsageRenderTarget = 0x4,
    MTLTextureUsagePixelFormatView = 0x10,
    MTLTextureUsageShaderAtomic = 0x20,
};

/* MTLResourceOptions. */
enum {
    MTLResourceCPUCacheModeDefaultCache = 0x0,
    MTLResourceCPUCacheModeWriteCombined = 0x1,
    MTLResourceStorageModeShared = 0x0,
    MTLResourceStorageModeManaged = 0x10,
    MTLResourceStorageModePrivate = 0x20,
    MTLResourceStorageModeMemoryless = 0x30,
    MTLResourceHazardTrackingModeDefault = 0x0,
    MTLResourceHazardTrackingModeUntracked = 0x100,
    MTLResourceHazardTrackingModeTracked = 0x200,
    MTLResourceOptionCPUCacheModeDefault = 0x0,
    MTLResourceOptionCPUCacheModeWriteCombined = 0x1,
};

/* MTLStorageMode. */
enum {
    MTLStorageModeShared = 0x0,
    MTLStorageModeManaged = 0x1,
    MTLStorageModePrivate = 0x2,
    MTLStorageModeMemoryless = 0x3,
};

/* MTLCPUCacheMode. */
enum {
    MTLCPUCacheModeDefaultCache = 0x0,
    MTLCPUCacheModeWriteCombined = 0x1,
};

/* MTLHazardTrackingMode. */
enum {
    MTLHazardTrackingModeDefault = 0x0,
    MTLHazardTrackingModeUntracked = 0x1,
    MTLHazardTrackingModeTracked = 0x2,
};

/* MTLHeapType. */
enum {
    MTLHeapTypeAutomatic = 0x0,
    MTLHeapTypePlacement = 0x1,
    MTLHeapTypeSparse = 0x2,
};

/* MTLSamplerMinMagFilter. */
enum {
    MTLSamplerMinMagFilterNearest = 0x0,
    MTLSamplerMinMagFilterLinear = 0x1,
};

/* MTLSamplerMipFilter. */
enum {
    MTLSamplerMipFilterNotMipmapped = 0x0,
    MTLSamplerMipFilterNearest = 0x1,
    MTLSamplerMipFilterLinear = 0x2,
};

/* MTLSamplerAddressMode. */
enum {
    MTLSamplerAddressModeClampToEdge = 0x0,
    MTLSamplerAddressModeMirrorClampToEdge = 0x1,
    MTLSamplerAddressModeRepeat = 0x2,
    MTLSamplerAddressModeMirrorRepeat = 0x3,
    MTLSamplerAddressModeClampToZero = 0x4,
    MTLSamplerAddressModeClampToBorderColor = 0x5,
};

/* MTLSamplerBorderColor. */
enum {
    MTLSamplerBorderColorTransparentBlack = 0x0,
    MTLSamplerBorderColorOpaqueBlack = 0x1,
    MTLSamplerBorderColorOpaqueWhite = 0x2,
};

/* MTLSamplerReductionMode. */
enum {
    MTLSamplerReductionModeWeightedAverage = 0x0,
    MTLSamplerReductionModeMinimum = 0x1,
    MTLSamplerReductionModeMaximum = 0x2,
};

/* MTLPrimitiveType. */
enum {
    MTLPrimitiveTypePoint = 0x0,
    MTLPrimitiveTypeLine = 0x1,
    MTLPrimitiveTypeLineStrip = 0x2,
    MTLPrimitiveTypeTriangle = 0x3,
    MTLPrimitiveTypeTriangleStrip = 0x4,
};

/* MTLPrimitiveTopologyClass. */
enum {
    MTLPrimitiveTopologyClassUnspecified = 0x0,
    MTLPrimitiveTopologyClassPoint = 0x1,
    MTLPrimitiveTopologyClassLine = 0x2,
    MTLPrimitiveTopologyClassTriangle = 0x3,
};

/* MTLIndexType. */
enum {
    MTLIndexTypeUInt16 = 0x0,
    MTLIndexTypeUInt32 = 0x1,
};

/* MTLCullMode. */
enum {
    MTLCullModeNone = 0x0,
    MTLCullModeFront = 0x1,
    MTLCullModeBack = 0x2,
};

/* MTLWinding. */
enum {
    MTLWindingClockwise = 0x0,
    MTLWindingCounterClockwise = 0x1,
};

/* MTLTriangleFillMode. */
enum {
    MTLTriangleFillModeFill = 0x0,
    MTLTriangleFillModeLines = 0x1,
};

/* MTLDepthClipMode. */
enum {
    MTLDepthClipModeClip = 0x0,
    MTLDepthClipModeClamp = 0x1,
};

/* MTLVisibilityResultMode. */
enum {
    MTLVisibilityResultModeDisabled = 0x0,
    MTLVisibilityResultModeBoolean = 0x1,
    MTLVisibilityResultModeCounting = 0x2,
};

/* MTLTextureSwizzle. */
enum {
    MTLTextureSwizzleZero = 0x0,
    MTLTextureSwizzleOne = 0x1,
    MTLTextureSwizzleRed = 0x2,
    MTLTextureSwizzleGreen = 0x3,
    MTLTextureSwizzleBlue = 0x4,
    MTLTextureSwizzleAlpha = 0x5,
};

/* MTLMultisampleDepthResolveFilter. */
enum {
    MTLMultisampleDepthResolveFilterSample0 = 0x0,
    MTLMultisampleDepthResolveFilterMin = 0x1,
    MTLMultisampleDepthResolveFilterMax = 0x2,
};

/* MTLMultisampleStencilResolveFilter. */
enum {
    MTLMultisampleStencilResolveFilterSample0 = 0x0,
    MTLMultisampleStencilResolveFilterDepthResolvedSample = 0x1,
};

/* MTLStoreActionOptions. */
enum {
    MTLStoreActionOptionNone = 0x0,
    MTLStoreActionOptionCustomSamplePositions = 0x1,
};

/* MTLBlitOption. */
enum {
    MTLBlitOptionNone = 0x0,
    MTLBlitOptionDepthFromDepthStencil = 0x1,
    MTLBlitOptionStencilFromDepthStencil = 0x2,
    MTLBlitOptionRowLinearPVRTC = 0x4,
};

/* MTLLanguageVersion. */
enum {
    MTLLanguageVersion1_1 = 0x10001,
    MTLLanguageVersion1_2 = 0x10002,
    MTLLanguageVersion2_0 = 0x20000,
    MTLLanguageVersion2_1 = 0x20001,
    MTLLanguageVersion2_2 = 0x20002,
    MTLLanguageVersion2_3 = 0x20003,
    MTLLanguageVersion2_4 = 0x20004,
    MTLLanguageVersion3_0 = 0x30000,
    MTLLanguageVersion3_1 = 0x30001,
    MTLLanguageVersion3_2 = 0x30002,
    MTLLanguageVersion4_0 = 0x40000,
    MTLLanguageVersion4_1 = 0x40001,
};

/* MTLLibraryOptimizationLevel. */
enum {
    MTLLibraryOptimizationLevelDefault = 0x0,
    MTLLibraryOptimizationLevelSize = 0x1,
};

/* MTLMathMode. */
enum {
    MTLMathModeSafe = 0x0,
    MTLMathModeRelaxed = 0x1,
    MTLMathModeFast = 0x2,
};

/* MTLMathFloatingPointFunctions. */
enum {
    MTLMathFloatingPointFunctionsFast = 0x0,
    MTLMathFloatingPointFunctionsPrecise = 0x1,
};

/* MTL4BlendState. */
enum {
    MTL4BlendStateDisabled = 0x0,
    MTL4BlendStateEnabled = 0x1,
    MTL4BlendStateUnspecialized = 0x2,
};

/* MTL4AlphaToCoverageState. */
enum {
    MTL4AlphaToCoverageStateDisabled = 0x0,
    MTL4AlphaToCoverageStateEnabled = 0x1,
};

/* MTL4AlphaToOneState. */
enum {
    MTL4AlphaToOneStateDisabled = 0x0,
    MTL4AlphaToOneStateEnabled = 0x1,
};

/* MTL4IndirectCommandBufferSupportState. */
enum {
    MTL4IndirectCommandBufferSupportStateDisabled = 0x0,
    MTL4IndirectCommandBufferSupportStateEnabled = 0x1,
};

/* MTLIndirectCommandType. */
enum {
    MTLIndirectCommandTypeDraw = 0x1,
    MTLIndirectCommandTypeDrawIndexed = 0x2,
    MTLIndirectCommandTypeConcurrentDispatch = 0x20,
};

/* MTL4VisibilityOptions. */
enum {
    MTL4VisibilityOptionNone = 0x0,
    MTL4VisibilityOptionDevice = 0x1,
    MTL4VisibilityOptionResourceAlias = 0x2,
};

/* MTL4RenderEncoderOptions. */
enum {
    MTL4RenderEncoderOptionNone = 0x0,
    MTL4RenderEncoderOptionSuspending = 0x1,
    MTL4RenderEncoderOptionResuming = 0x2,
};

/* MTLTextureCompressionType. */
enum {
    MTLTextureCompressionTypeLossless = 0x0,
    MTLTextureCompressionTypeLossy = 0x1,
};

/* MTL4LogicalToPhysicalColorAttachmentMappingState. */
enum {
    MTL4LogicalToPhysicalColorAttachmentMappingStateIdentity = 0x0,
    MTL4LogicalToPhysicalColorAttachmentMappingStateInherited = 0x1,
};

/* MTLVisibilityResultType. */
enum {
    MTLVisibilityResultTypeReset = 0x0,
    MTLVisibilityResultTypeAccumulate = 0x1,
};

/* MTL4TimestampGranularity. */
enum {
    MTL4TimestampGranularityRelaxed = 0x0,
    MTL4TimestampGranularityPrecise = 0x1,
};

/* MTL4CounterHeapType. */
enum {
    MTL4CounterHeapTypeInvalid = 0x0,
    MTL4CounterHeapTypeTimestamp = 0x1,
};

/* MTLShaderValidation. */
enum {
    MTLShaderValidationDefault = 0x0,
    MTLShaderValidationEnabled = 0x1,
    MTLShaderValidationDisabled = 0x2,
};

/* MTLGPUFamily. */
enum {
    MTLGPUFamilyApple1 = 0x3e9,
    MTLGPUFamilyApple2 = 0x3ea,
    MTLGPUFamilyApple3 = 0x3eb,
    MTLGPUFamilyApple4 = 0x3ec,
    MTLGPUFamilyApple5 = 0x3ed,
    MTLGPUFamilyApple6 = 0x3ee,
    MTLGPUFamilyApple7 = 0x3ef,
    MTLGPUFamilyApple8 = 0x3f0,
    MTLGPUFamilyApple9 = 0x3f1,
    MTLGPUFamilyApple10 = 0x3f2,
    MTLGPUFamilyApple11 = 0x3f3,
    MTLGPUFamilyMac1 = 0x7d1,
    MTLGPUFamilyMac2 = 0x7d2,
    MTLGPUFamilyCommon1 = 0xbb9,
    MTLGPUFamilyCommon2 = 0xbba,
    MTLGPUFamilyCommon3 = 0xbbb,
    MTLGPUFamilyMacCatalyst1 = 0xfa1,
    MTLGPUFamilyMacCatalyst2 = 0xfa2,
    MTLGPUFamilyMetal3 = 0x1389,
    MTLGPUFamilyMetal4 = 0x138a,
};

/* MTLDataType. */
enum {
    MTLDataTypeNone = 0x0,
    MTLDataTypeStruct = 0x1,
    MTLDataTypeArray = 0x2,
    MTLDataTypeFloat = 0x3,
    MTLDataTypeFloat2 = 0x4,
    MTLDataTypeFloat3 = 0x5,
    MTLDataTypeFloat4 = 0x6,
    MTLDataTypeFloat2x2 = 0x7,
    MTLDataTypeFloat2x3 = 0x8,
    MTLDataTypeFloat2x4 = 0x9,
    MTLDataTypeFloat3x2 = 0xa,
    MTLDataTypeFloat3x3 = 0xb,
    MTLDataTypeFloat3x4 = 0xc,
    MTLDataTypeFloat4x2 = 0xd,
    MTLDataTypeFloat4x3 = 0xe,
    MTLDataTypeFloat4x4 = 0xf,
    MTLDataTypeHalf = 0x10,
    MTLDataTypeHalf2 = 0x11,
    MTLDataTypeHalf3 = 0x12,
    MTLDataTypeHalf4 = 0x13,
    MTLDataTypeHalf2x2 = 0x14,
    MTLDataTypeHalf2x3 = 0x15,
    MTLDataTypeHalf2x4 = 0x16,
    MTLDataTypeHalf3x2 = 0x17,
    MTLDataTypeHalf3x3 = 0x18,
    MTLDataTypeHalf3x4 = 0x19,
    MTLDataTypeHalf4x2 = 0x1a,
    MTLDataTypeHalf4x3 = 0x1b,
    MTLDataTypeHalf4x4 = 0x1c,
    MTLDataTypeInt = 0x1d,
    MTLDataTypeInt2 = 0x1e,
    MTLDataTypeInt3 = 0x1f,
    MTLDataTypeInt4 = 0x20,
    MTLDataTypeUInt = 0x21,
    MTLDataTypeUInt2 = 0x22,
    MTLDataTypeUInt3 = 0x23,
    MTLDataTypeUInt4 = 0x24,
    MTLDataTypeShort = 0x25,
    MTLDataTypeShort2 = 0x26,
    MTLDataTypeShort3 = 0x27,
    MTLDataTypeShort4 = 0x28,
    MTLDataTypeUShort = 0x29,
    MTLDataTypeUShort2 = 0x2a,
    MTLDataTypeUShort3 = 0x2b,
    MTLDataTypeUShort4 = 0x2c,
    MTLDataTypeChar = 0x2d,
    MTLDataTypeChar2 = 0x2e,
    MTLDataTypeChar3 = 0x2f,
    MTLDataTypeChar4 = 0x30,
    MTLDataTypeUChar = 0x31,
    MTLDataTypeUChar2 = 0x32,
    MTLDataTypeUChar3 = 0x33,
    MTLDataTypeUChar4 = 0x34,
    MTLDataTypeBool = 0x35,
    MTLDataTypeBool2 = 0x36,
    MTLDataTypeBool3 = 0x37,
    MTLDataTypeBool4 = 0x38,
    MTLDataTypeTexture = 0x3a,
    MTLDataTypeSampler = 0x3b,
    MTLDataTypePointer = 0x3c,
    MTLDataTypeR8Unorm = 0x3e,
    MTLDataTypeR8Snorm = 0x3f,
    MTLDataTypeR16Unorm = 0x40,
    MTLDataTypeR16Snorm = 0x41,
    MTLDataTypeRG8Unorm = 0x42,
    MTLDataTypeRG8Snorm = 0x43,
    MTLDataTypeRG16Unorm = 0x44,
    MTLDataTypeRG16Snorm = 0x45,
    MTLDataTypeRGBA8Unorm = 0x46,
    MTLDataTypeRGBA8Unorm_sRGB = 0x47,
    MTLDataTypeRGBA8Snorm = 0x48,
    MTLDataTypeRGBA16Unorm = 0x49,
    MTLDataTypeRGBA16Snorm = 0x4a,
    MTLDataTypeRGB10A2Unorm = 0x4b,
    MTLDataTypeRG11B10Float = 0x4c,
    MTLDataTypeRGB9E5Float = 0x4d,
    MTLDataTypeRenderPipeline = 0x4e,
    MTLDataTypeComputePipeline = 0x4f,
    MTLDataTypeIndirectCommandBuffer = 0x50,
    MTLDataTypeLong = 0x51,
    MTLDataTypeLong2 = 0x52,
    MTLDataTypeLong3 = 0x53,
    MTLDataTypeLong4 = 0x54,
    MTLDataTypeULong = 0x55,
    MTLDataTypeULong2 = 0x56,
    MTLDataTypeULong3 = 0x57,
    MTLDataTypeULong4 = 0x58,
    MTLDataTypeVisibleFunctionTable = 0x73,
    MTLDataTypeIntersectionFunctionTable = 0x74,
    MTLDataTypePrimitiveAccelerationStructure = 0x75,
    MTLDataTypeInstanceAccelerationStructure = 0x76,
    MTLDataTypeBFloat = 0x79,
    MTLDataTypeBFloat2 = 0x7a,
    MTLDataTypeBFloat3 = 0x7b,
    MTLDataTypeBFloat4 = 0x7c,
    MTLDataTypeDepthStencilState = 0x8b,
    MTLDataTypeTensor = 0x8c,
};

#endif
