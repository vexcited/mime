/* Vulkan formats on Metal: pixel and vertex formats, sizes and what Apple GPUs do with them. */
#include "vkm.h"

#define ALL (VKM_CAP_SAMPLED | VKM_CAP_FILTER | VKM_CAP_STORAGE | VKM_CAP_COLOR | VKM_CAP_BLEND | VKM_CAP_MSAA | \
             VKM_CAP_RESOLVE | VKM_CAP_TEXEL | VKM_CAP_STORAGE_TEXEL)
#define INT (VKM_CAP_SAMPLED | VKM_CAP_STORAGE | VKM_CAP_COLOR | VKM_CAP_MSAA | VKM_CAP_TEXEL | VKM_CAP_STORAGE_TEXEL)
#define SRGB (VKM_CAP_SAMPLED | VKM_CAP_FILTER | VKM_CAP_COLOR | VKM_CAP_BLEND | VKM_CAP_MSAA | VKM_CAP_RESOLVE)
#define PACKED (VKM_CAP_SAMPLED | VKM_CAP_FILTER | VKM_CAP_COLOR | VKM_CAP_BLEND | VKM_CAP_MSAA | VKM_CAP_RESOLVE)
#define FLOAT32 (VKM_CAP_SAMPLED | VKM_CAP_STORAGE | VKM_CAP_COLOR | VKM_CAP_BLEND | VKM_CAP_MSAA | VKM_CAP_TEXEL | \
                 VKM_CAP_STORAGE_TEXEL)
#define DEPTH (VKM_CAP_SAMPLED | VKM_CAP_FILTER | VKM_CAP_DEPTH | VKM_CAP_MSAA | VKM_CAP_RESOLVE)
#define BLOCK (VKM_CAP_SAMPLED | VKM_CAP_FILTER)
#define V VKM_CAP_VERTEX
#define C VK_IMAGE_ASPECT_COLOR_BIT

#define F(vk, px, vx, size, caps) [vk] = {px, vx, size, 1, 1, C, (caps) | ((vx) ? V : 0)}
#define S(vk, px, size) [vk] = {px, 0, size, 1, 1, C, SRGB, true}
#define Z(vk, px, size, aspects, caps) [vk] = {px, 0, size, 1, 1, aspects, caps}
#define B(vk, px, size, w, h, srgb) [vk] = {px, 0, size, w, h, C, BLOCK, srgb, true}
#define VO(vk, vx, size) [vk] = {0, vx, size, 1, 1, C, V}

static const VkmFormat g_core[] = {
    F(VK_FORMAT_R4G4B4A4_UNORM_PACK16, MTLPixelFormatABGR4Unorm, 0, 2, PACKED),
    F(VK_FORMAT_R5G6B5_UNORM_PACK16, MTLPixelFormatB5G6R5Unorm, 0, 2, PACKED),
    F(VK_FORMAT_R5G5B5A1_UNORM_PACK16, MTLPixelFormatA1BGR5Unorm, 0, 2, PACKED),
    F(VK_FORMAT_A1R5G5B5_UNORM_PACK16, MTLPixelFormatBGR5A1Unorm, 0, 2, PACKED),

    F(VK_FORMAT_R8_UNORM, MTLPixelFormatR8Unorm, MTLVertexFormatUCharNormalized, 1, ALL),
    F(VK_FORMAT_R8_SNORM, MTLPixelFormatR8Snorm, MTLVertexFormatCharNormalized, 1, ALL),
    VO(VK_FORMAT_R8_USCALED, MTLVertexFormatUChar, 1),
    VO(VK_FORMAT_R8_SSCALED, MTLVertexFormatChar, 1),
    F(VK_FORMAT_R8_UINT, MTLPixelFormatR8Uint, MTLVertexFormatUChar, 1, INT),
    F(VK_FORMAT_R8_SINT, MTLPixelFormatR8Sint, MTLVertexFormatChar, 1, INT),
    S(VK_FORMAT_R8_SRGB, MTLPixelFormatR8Unorm_sRGB, 1),
    F(VK_FORMAT_R8G8_UNORM, MTLPixelFormatRG8Unorm, MTLVertexFormatUChar2Normalized, 2, ALL),
    F(VK_FORMAT_R8G8_SNORM, MTLPixelFormatRG8Snorm, MTLVertexFormatChar2Normalized, 2, ALL),
    VO(VK_FORMAT_R8G8_USCALED, MTLVertexFormatUChar2, 2),
    VO(VK_FORMAT_R8G8_SSCALED, MTLVertexFormatChar2, 2),
    F(VK_FORMAT_R8G8_UINT, MTLPixelFormatRG8Uint, MTLVertexFormatUChar2, 2, INT),
    F(VK_FORMAT_R8G8_SINT, MTLPixelFormatRG8Sint, MTLVertexFormatChar2, 2, INT),
    S(VK_FORMAT_R8G8_SRGB, MTLPixelFormatRG8Unorm_sRGB, 2),
    VO(VK_FORMAT_R8G8B8_UNORM, MTLVertexFormatUChar3Normalized, 3),
    VO(VK_FORMAT_R8G8B8_SNORM, MTLVertexFormatChar3Normalized, 3),
    VO(VK_FORMAT_R8G8B8_USCALED, MTLVertexFormatUChar3, 3),
    VO(VK_FORMAT_R8G8B8_SSCALED, MTLVertexFormatChar3, 3),
    VO(VK_FORMAT_R8G8B8_UINT, MTLVertexFormatUChar3, 3),
    VO(VK_FORMAT_R8G8B8_SINT, MTLVertexFormatChar3, 3),
    F(VK_FORMAT_R8G8B8A8_UNORM, MTLPixelFormatRGBA8Unorm, MTLVertexFormatUChar4Normalized, 4, ALL),
    F(VK_FORMAT_R8G8B8A8_SNORM, MTLPixelFormatRGBA8Snorm, MTLVertexFormatChar4Normalized, 4, ALL),
    VO(VK_FORMAT_R8G8B8A8_USCALED, MTLVertexFormatUChar4, 4),
    VO(VK_FORMAT_R8G8B8A8_SSCALED, MTLVertexFormatChar4, 4),
    F(VK_FORMAT_R8G8B8A8_UINT, MTLPixelFormatRGBA8Uint, MTLVertexFormatUChar4, 4, INT),
    F(VK_FORMAT_R8G8B8A8_SINT, MTLPixelFormatRGBA8Sint, MTLVertexFormatChar4, 4, INT),
    S(VK_FORMAT_R8G8B8A8_SRGB, MTLPixelFormatRGBA8Unorm_sRGB, 4),
    F(VK_FORMAT_B8G8R8A8_UNORM, MTLPixelFormatBGRA8Unorm, MTLVertexFormatUChar4Normalized_BGRA, 4, ALL),
    S(VK_FORMAT_B8G8R8A8_SRGB, MTLPixelFormatBGRA8Unorm_sRGB, 4),
    F(VK_FORMAT_A8B8G8R8_UNORM_PACK32, MTLPixelFormatRGBA8Unorm, MTLVertexFormatUChar4Normalized, 4, ALL),
    F(VK_FORMAT_A8B8G8R8_SNORM_PACK32, MTLPixelFormatRGBA8Snorm, MTLVertexFormatChar4Normalized, 4, ALL),
    VO(VK_FORMAT_A8B8G8R8_USCALED_PACK32, MTLVertexFormatUChar4, 4),
    VO(VK_FORMAT_A8B8G8R8_SSCALED_PACK32, MTLVertexFormatChar4, 4),
    F(VK_FORMAT_A8B8G8R8_UINT_PACK32, MTLPixelFormatRGBA8Uint, MTLVertexFormatUChar4, 4, INT),
    F(VK_FORMAT_A8B8G8R8_SINT_PACK32, MTLPixelFormatRGBA8Sint, MTLVertexFormatChar4, 4, INT),
    S(VK_FORMAT_A8B8G8R8_SRGB_PACK32, MTLPixelFormatRGBA8Unorm_sRGB, 4),
    F(VK_FORMAT_A2R10G10B10_UNORM_PACK32, MTLPixelFormatBGR10A2Unorm, 0, 4, ALL),
    F(VK_FORMAT_A2B10G10R10_UNORM_PACK32, MTLPixelFormatRGB10A2Unorm, MTLVertexFormatUInt1010102Normalized, 4, ALL),
    VO(VK_FORMAT_A2B10G10R10_SNORM_PACK32, MTLVertexFormatInt1010102Normalized, 4),
    F(VK_FORMAT_A2B10G10R10_UINT_PACK32, MTLPixelFormatRGB10A2Uint, 0, 4, INT),

    F(VK_FORMAT_R16_UNORM, MTLPixelFormatR16Unorm, MTLVertexFormatUShortNormalized, 2, ALL),
    F(VK_FORMAT_R16_SNORM, MTLPixelFormatR16Snorm, MTLVertexFormatShortNormalized, 2, ALL),
    VO(VK_FORMAT_R16_USCALED, MTLVertexFormatUShort, 2),
    VO(VK_FORMAT_R16_SSCALED, MTLVertexFormatShort, 2),
    F(VK_FORMAT_R16_UINT, MTLPixelFormatR16Uint, MTLVertexFormatUShort, 2, INT),
    F(VK_FORMAT_R16_SINT, MTLPixelFormatR16Sint, MTLVertexFormatShort, 2, INT),
    F(VK_FORMAT_R16_SFLOAT, MTLPixelFormatR16Float, MTLVertexFormatHalf, 2, ALL),
    F(VK_FORMAT_R16G16_UNORM, MTLPixelFormatRG16Unorm, MTLVertexFormatUShort2Normalized, 4, ALL),
    F(VK_FORMAT_R16G16_SNORM, MTLPixelFormatRG16Snorm, MTLVertexFormatShort2Normalized, 4, ALL),
    VO(VK_FORMAT_R16G16_USCALED, MTLVertexFormatUShort2, 4),
    VO(VK_FORMAT_R16G16_SSCALED, MTLVertexFormatShort2, 4),
    F(VK_FORMAT_R16G16_UINT, MTLPixelFormatRG16Uint, MTLVertexFormatUShort2, 4, INT),
    F(VK_FORMAT_R16G16_SINT, MTLPixelFormatRG16Sint, MTLVertexFormatShort2, 4, INT),
    F(VK_FORMAT_R16G16_SFLOAT, MTLPixelFormatRG16Float, MTLVertexFormatHalf2, 4, ALL),
    VO(VK_FORMAT_R16G16B16_UNORM, MTLVertexFormatUShort3Normalized, 6),
    VO(VK_FORMAT_R16G16B16_SNORM, MTLVertexFormatShort3Normalized, 6),
    VO(VK_FORMAT_R16G16B16_USCALED, MTLVertexFormatUShort3, 6),
    VO(VK_FORMAT_R16G16B16_SSCALED, MTLVertexFormatShort3, 6),
    VO(VK_FORMAT_R16G16B16_UINT, MTLVertexFormatUShort3, 6),
    VO(VK_FORMAT_R16G16B16_SINT, MTLVertexFormatShort3, 6),
    VO(VK_FORMAT_R16G16B16_SFLOAT, MTLVertexFormatHalf3, 6),
    F(VK_FORMAT_R16G16B16A16_UNORM, MTLPixelFormatRGBA16Unorm, MTLVertexFormatUShort4Normalized, 8, ALL),
    F(VK_FORMAT_R16G16B16A16_SNORM, MTLPixelFormatRGBA16Snorm, MTLVertexFormatShort4Normalized, 8, ALL),
    VO(VK_FORMAT_R16G16B16A16_USCALED, MTLVertexFormatUShort4, 8),
    VO(VK_FORMAT_R16G16B16A16_SSCALED, MTLVertexFormatShort4, 8),
    F(VK_FORMAT_R16G16B16A16_UINT, MTLPixelFormatRGBA16Uint, MTLVertexFormatUShort4, 8, INT),
    F(VK_FORMAT_R16G16B16A16_SINT, MTLPixelFormatRGBA16Sint, MTLVertexFormatShort4, 8, INT),
    F(VK_FORMAT_R16G16B16A16_SFLOAT, MTLPixelFormatRGBA16Float, MTLVertexFormatHalf4, 8, ALL),

    F(VK_FORMAT_R32_UINT, MTLPixelFormatR32Uint, MTLVertexFormatUInt, 4, INT | VKM_CAP_ATOMIC),
    F(VK_FORMAT_R32_SINT, MTLPixelFormatR32Sint, MTLVertexFormatInt, 4, INT | VKM_CAP_ATOMIC),
    F(VK_FORMAT_R32_SFLOAT, MTLPixelFormatR32Float, MTLVertexFormatFloat, 4, FLOAT32),
    F(VK_FORMAT_R32G32_UINT, MTLPixelFormatRG32Uint, MTLVertexFormatUInt2, 8, INT),
    F(VK_FORMAT_R32G32_SINT, MTLPixelFormatRG32Sint, MTLVertexFormatInt2, 8, INT),
    F(VK_FORMAT_R32G32_SFLOAT, MTLPixelFormatRG32Float, MTLVertexFormatFloat2, 8, FLOAT32),
    VO(VK_FORMAT_R32G32B32_UINT, MTLVertexFormatUInt3, 12),
    VO(VK_FORMAT_R32G32B32_SINT, MTLVertexFormatInt3, 12),
    VO(VK_FORMAT_R32G32B32_SFLOAT, MTLVertexFormatFloat3, 12),
    F(VK_FORMAT_R32G32B32A32_UINT, MTLPixelFormatRGBA32Uint, MTLVertexFormatUInt4, 16, INT),
    F(VK_FORMAT_R32G32B32A32_SINT, MTLPixelFormatRGBA32Sint, MTLVertexFormatInt4, 16, INT),
    F(VK_FORMAT_R32G32B32A32_SFLOAT, MTLPixelFormatRGBA32Float, MTLVertexFormatFloat4, 16, FLOAT32),

    F(VK_FORMAT_B10G11R11_UFLOAT_PACK32, MTLPixelFormatRG11B10Float, MTLVertexFormatFloatRG11B10, 4, ALL),
    F(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, MTLPixelFormatRGB9E5Float, MTLVertexFormatFloatRGB9E5, 4, PACKED),

    Z(VK_FORMAT_D16_UNORM, MTLPixelFormatDepth16Unorm, 2, VK_IMAGE_ASPECT_DEPTH_BIT, DEPTH),
    Z(VK_FORMAT_D32_SFLOAT, MTLPixelFormatDepth32Float, 4, VK_IMAGE_ASPECT_DEPTH_BIT, DEPTH),
    Z(VK_FORMAT_S8_UINT, MTLPixelFormatStencil8, 1, VK_IMAGE_ASPECT_STENCIL_BIT,
      VKM_CAP_SAMPLED | VKM_CAP_DEPTH | VKM_CAP_MSAA),
    Z(VK_FORMAT_D32_SFLOAT_S8_UINT, MTLPixelFormatDepth32Float_Stencil8, 8,
      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, DEPTH),

    B(VK_FORMAT_BC1_RGB_UNORM_BLOCK, MTLPixelFormatBC1_RGBA, 8, 4, 4, false),
    B(VK_FORMAT_BC1_RGB_SRGB_BLOCK, MTLPixelFormatBC1_RGBA_sRGB, 8, 4, 4, true),
    B(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, MTLPixelFormatBC1_RGBA, 8, 4, 4, false),
    B(VK_FORMAT_BC1_RGBA_SRGB_BLOCK, MTLPixelFormatBC1_RGBA_sRGB, 8, 4, 4, true),
    B(VK_FORMAT_BC2_UNORM_BLOCK, MTLPixelFormatBC2_RGBA, 16, 4, 4, false),
    B(VK_FORMAT_BC2_SRGB_BLOCK, MTLPixelFormatBC2_RGBA_sRGB, 16, 4, 4, true),
    B(VK_FORMAT_BC3_UNORM_BLOCK, MTLPixelFormatBC3_RGBA, 16, 4, 4, false),
    B(VK_FORMAT_BC3_SRGB_BLOCK, MTLPixelFormatBC3_RGBA_sRGB, 16, 4, 4, true),
    B(VK_FORMAT_BC4_UNORM_BLOCK, MTLPixelFormatBC4_RUnorm, 8, 4, 4, false),
    B(VK_FORMAT_BC4_SNORM_BLOCK, MTLPixelFormatBC4_RSnorm, 8, 4, 4, false),
    B(VK_FORMAT_BC5_UNORM_BLOCK, MTLPixelFormatBC5_RGUnorm, 16, 4, 4, false),
    B(VK_FORMAT_BC5_SNORM_BLOCK, MTLPixelFormatBC5_RGSnorm, 16, 4, 4, false),
    B(VK_FORMAT_BC6H_UFLOAT_BLOCK, MTLPixelFormatBC6H_RGBUfloat, 16, 4, 4, false),
    B(VK_FORMAT_BC6H_SFLOAT_BLOCK, MTLPixelFormatBC6H_RGBFloat, 16, 4, 4, false),
    B(VK_FORMAT_BC7_UNORM_BLOCK, MTLPixelFormatBC7_RGBAUnorm, 16, 4, 4, false),
    B(VK_FORMAT_BC7_SRGB_BLOCK, MTLPixelFormatBC7_RGBAUnorm_sRGB, 16, 4, 4, true),
    B(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK, MTLPixelFormatETC2_RGB8, 8, 4, 4, false),
    B(VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK, MTLPixelFormatETC2_RGB8_sRGB, 8, 4, 4, true),
    B(VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK, MTLPixelFormatETC2_RGB8A1, 8, 4, 4, false),
    B(VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK, MTLPixelFormatETC2_RGB8A1_sRGB, 8, 4, 4, true),
    B(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, MTLPixelFormatEAC_RGBA8, 16, 4, 4, false),
    B(VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK, MTLPixelFormatEAC_RGBA8_sRGB, 16, 4, 4, true),
    B(VK_FORMAT_EAC_R11_UNORM_BLOCK, MTLPixelFormatEAC_R11Unorm, 8, 4, 4, false),
    B(VK_FORMAT_EAC_R11_SNORM_BLOCK, MTLPixelFormatEAC_R11Snorm, 8, 4, 4, false),
    B(VK_FORMAT_EAC_R11G11_UNORM_BLOCK, MTLPixelFormatEAC_RG11Unorm, 16, 4, 4, false),
    B(VK_FORMAT_EAC_R11G11_SNORM_BLOCK, MTLPixelFormatEAC_RG11Snorm, 16, 4, 4, false),
    B(VK_FORMAT_ASTC_4x4_UNORM_BLOCK, MTLPixelFormatASTC_4x4_LDR, 16, 4, 4, false),
    B(VK_FORMAT_ASTC_4x4_SRGB_BLOCK, MTLPixelFormatASTC_4x4_sRGB, 16, 4, 4, true),
    B(VK_FORMAT_ASTC_5x4_UNORM_BLOCK, MTLPixelFormatASTC_5x4_LDR, 16, 5, 4, false),
    B(VK_FORMAT_ASTC_5x4_SRGB_BLOCK, MTLPixelFormatASTC_5x4_sRGB, 16, 5, 4, true),
    B(VK_FORMAT_ASTC_5x5_UNORM_BLOCK, MTLPixelFormatASTC_5x5_LDR, 16, 5, 5, false),
    B(VK_FORMAT_ASTC_5x5_SRGB_BLOCK, MTLPixelFormatASTC_5x5_sRGB, 16, 5, 5, true),
    B(VK_FORMAT_ASTC_6x5_UNORM_BLOCK, MTLPixelFormatASTC_6x5_LDR, 16, 6, 5, false),
    B(VK_FORMAT_ASTC_6x5_SRGB_BLOCK, MTLPixelFormatASTC_6x5_sRGB, 16, 6, 5, true),
    B(VK_FORMAT_ASTC_6x6_UNORM_BLOCK, MTLPixelFormatASTC_6x6_LDR, 16, 6, 6, false),
    B(VK_FORMAT_ASTC_6x6_SRGB_BLOCK, MTLPixelFormatASTC_6x6_sRGB, 16, 6, 6, true),
    B(VK_FORMAT_ASTC_8x5_UNORM_BLOCK, MTLPixelFormatASTC_8x5_LDR, 16, 8, 5, false),
    B(VK_FORMAT_ASTC_8x5_SRGB_BLOCK, MTLPixelFormatASTC_8x5_sRGB, 16, 8, 5, true),
    B(VK_FORMAT_ASTC_8x6_UNORM_BLOCK, MTLPixelFormatASTC_8x6_LDR, 16, 8, 6, false),
    B(VK_FORMAT_ASTC_8x6_SRGB_BLOCK, MTLPixelFormatASTC_8x6_sRGB, 16, 8, 6, true),
    B(VK_FORMAT_ASTC_8x8_UNORM_BLOCK, MTLPixelFormatASTC_8x8_LDR, 16, 8, 8, false),
    B(VK_FORMAT_ASTC_8x8_SRGB_BLOCK, MTLPixelFormatASTC_8x8_sRGB, 16, 8, 8, true),
    B(VK_FORMAT_ASTC_10x5_UNORM_BLOCK, MTLPixelFormatASTC_10x5_LDR, 16, 10, 5, false),
    B(VK_FORMAT_ASTC_10x5_SRGB_BLOCK, MTLPixelFormatASTC_10x5_sRGB, 16, 10, 5, true),
    B(VK_FORMAT_ASTC_10x6_UNORM_BLOCK, MTLPixelFormatASTC_10x6_LDR, 16, 10, 6, false),
    B(VK_FORMAT_ASTC_10x6_SRGB_BLOCK, MTLPixelFormatASTC_10x6_sRGB, 16, 10, 6, true),
    B(VK_FORMAT_ASTC_10x8_UNORM_BLOCK, MTLPixelFormatASTC_10x8_LDR, 16, 10, 8, false),
    B(VK_FORMAT_ASTC_10x8_SRGB_BLOCK, MTLPixelFormatASTC_10x8_sRGB, 16, 10, 8, true),
    B(VK_FORMAT_ASTC_10x10_UNORM_BLOCK, MTLPixelFormatASTC_10x10_LDR, 16, 10, 10, false),
    B(VK_FORMAT_ASTC_10x10_SRGB_BLOCK, MTLPixelFormatASTC_10x10_sRGB, 16, 10, 10, true),
    B(VK_FORMAT_ASTC_12x10_UNORM_BLOCK, MTLPixelFormatASTC_12x10_LDR, 16, 12, 10, false),
    B(VK_FORMAT_ASTC_12x10_SRGB_BLOCK, MTLPixelFormatASTC_12x10_sRGB, 16, 12, 10, true),
    B(VK_FORMAT_ASTC_12x12_UNORM_BLOCK, MTLPixelFormatASTC_12x12_LDR, 16, 12, 12, false),
    B(VK_FORMAT_ASTC_12x12_SRGB_BLOCK, MTLPixelFormatASTC_12x12_sRGB, 16, 12, 12, true),
};

static const VkmFormat g_a8 = {MTLPixelFormatA8Unorm, 0, 1, 1, 1, C, VKM_CAP_SAMPLED | VKM_CAP_FILTER};
static const VkmFormat g_none;

const VkmFormat *vkm_format(VkFormat format)
{
    if ((uint32_t)format < MIME_ARRAY_COUNT(g_core))
        return &g_core[format];
    if (format == VK_FORMAT_A8_UNORM)
        return &g_a8;
    return &g_none;
}

VkFormatFeatureFlags2 vkm_format_features(const VkmFormat *f, bool linear, bool buffer)
{
    VkFormatFeatureFlags2 out = 0;
    uint32_t caps = f->caps;
    if (buffer) {
        if (caps & VKM_CAP_VERTEX)
            out |= VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT;
        if (caps & VKM_CAP_TEXEL)
            out |= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT;
        if (caps & VKM_CAP_STORAGE_TEXEL)
            out |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT |
                   VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
                   VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
        if (caps & VKM_CAP_ATOMIC)
            out |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
        return out;
    }
    if (!f->pixel)
        return 0;
    /* linear images are textures over buffers: no depth, compression or multisampling. */
    if (linear && (f->compressed || (caps & VKM_CAP_DEPTH)))
        return 0;
    out |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT |
           VK_FORMAT_FEATURE_2_BLIT_SRC_BIT;
    if (caps & VKM_CAP_SAMPLED)
        out |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
    if (caps & VKM_CAP_FILTER)
        out |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_MINMAX_BIT;
    if (caps & VKM_CAP_STORAGE)
        out |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
               VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
    if (caps & VKM_CAP_ATOMIC)
        out |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT;
    if (caps & VKM_CAP_COLOR)
        out |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_2_BLIT_DST_BIT;
    if (caps & VKM_CAP_BLEND)
        out |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT;
    if (caps & VKM_CAP_DEPTH) {
        out |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (f->aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
            out |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
    }
    return out;
}

uint32_t vkm_aspect_format(const VkmFormat *f, VkImageAspectFlags aspect)
{
    if (f->pixel == MTLPixelFormatDepth32Float_Stencil8 && aspect == VK_IMAGE_ASPECT_STENCIL_BIT)
        return MTLPixelFormatX32_Stencil8;
    return f->pixel;
}

uint32_t vkm_texture_type(VkImageViewType type, uint32_t samples)
{
    switch (type) {
    case VK_IMAGE_VIEW_TYPE_1D: return MTLTextureType1D;
    case VK_IMAGE_VIEW_TYPE_1D_ARRAY: return MTLTextureType1DArray;
    case VK_IMAGE_VIEW_TYPE_2D: return samples > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
    case VK_IMAGE_VIEW_TYPE_2D_ARRAY: return samples > 1 ? MTLTextureType2DMultisampleArray : MTLTextureType2DArray;
    case VK_IMAGE_VIEW_TYPE_3D: return MTLTextureType3D;
    case VK_IMAGE_VIEW_TYPE_CUBE: return MTLTextureTypeCube;
    case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY: return MTLTextureTypeCubeArray;
    default: return MTLTextureType2D;
    }
}
