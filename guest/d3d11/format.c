/*
 * dxgi formats as vulkan formats. typeless formats are their unsigned
 * integer or unorm member, made mutable so typed views reinterpret them;
 * typeless depth formats also name the depth format their textures take
 * when bound as depth stencils. x8 formats read their alpha as one.
 */
#include "d3d11_private.h"

#define F(dxgi, vkf, bytes, ...) [DXGI_FORMAT_##dxgi] = {VK_FORMAT_##vkf, VK_FORMAT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT, bytes, 1, __VA_ARGS__}
#define D(dxgi, vkf, depthf, aspect, bytes, ...) [DXGI_FORMAT_##dxgi] = {VK_FORMAT_##vkf, VK_FORMAT_##depthf, aspect, bytes, 1, __VA_ARGS__}
#define BC(dxgi, vkf, bytes, ...) [DXGI_FORMAT_##dxgi] = {VK_FORMAT_##vkf, VK_FORMAT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT, bytes, 4, __VA_ARGS__}

#define T D3D_FORMAT_TYPELESS
#define S D3D_FORMAT_SRGB
#define I D3D_FORMAT_INT
#define X D3D_FORMAT_BGRX
#define PL D3D_FORMAT_PLANAR
#define DEPTH (VK_IMAGE_ASPECT_DEPTH_BIT)
#define DS (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
#define DF D3D_FORMAT_DEPTH
#define SF D3D_FORMAT_STENCIL

static D3dFormat g_formats[DXGI_FORMAT_A4B4G4R4_UNORM + 1] = {
    F(R32G32B32A32_TYPELESS, R32G32B32A32_UINT, 16, T),
    F(R32G32B32A32_FLOAT, R32G32B32A32_SFLOAT, 16, 0),
    F(R32G32B32A32_UINT, R32G32B32A32_UINT, 16, I),
    F(R32G32B32A32_SINT, R32G32B32A32_SINT, 16, I),
    F(R32G32B32_TYPELESS, R32G32B32_UINT, 12, T),
    F(R32G32B32_FLOAT, R32G32B32_SFLOAT, 12, 0),
    F(R32G32B32_UINT, R32G32B32_UINT, 12, I),
    F(R32G32B32_SINT, R32G32B32_SINT, 12, I),
    F(R16G16B16A16_TYPELESS, R16G16B16A16_UINT, 8, T),
    F(R16G16B16A16_FLOAT, R16G16B16A16_SFLOAT, 8, 0),
    F(R16G16B16A16_UNORM, R16G16B16A16_UNORM, 8, 0),
    F(R16G16B16A16_UINT, R16G16B16A16_UINT, 8, I),
    F(R16G16B16A16_SNORM, R16G16B16A16_SNORM, 8, 0),
    F(R16G16B16A16_SINT, R16G16B16A16_SINT, 8, I),
    F(R32G32_TYPELESS, R32G32_UINT, 8, T),
    F(R32G32_FLOAT, R32G32_SFLOAT, 8, 0),
    F(R32G32_UINT, R32G32_UINT, 8, I),
    F(R32G32_SINT, R32G32_SINT, 8, I),
    D(R32G8X24_TYPELESS, D32_SFLOAT_S8_UINT, D32_SFLOAT_S8_UINT, DS, 8, T | DF | SF),
    D(D32_FLOAT_S8X24_UINT, D32_SFLOAT_S8_UINT, D32_SFLOAT_S8_UINT, DS, 8, DF | SF),
    D(R32_FLOAT_X8X24_TYPELESS, D32_SFLOAT_S8_UINT, D32_SFLOAT_S8_UINT, DEPTH, 8, DF),
    D(X32_TYPELESS_G8X24_UINT, D32_SFLOAT_S8_UINT, D32_SFLOAT_S8_UINT, VK_IMAGE_ASPECT_STENCIL_BIT, 8, SF | I),
    F(R10G10B10A2_TYPELESS, A2B10G10R10_UINT_PACK32, 4, T),
    F(R10G10B10A2_UNORM, A2B10G10R10_UNORM_PACK32, 4, 0),
    F(R10G10B10A2_UINT, A2B10G10R10_UINT_PACK32, 4, I),
    F(R11G11B10_FLOAT, B10G11R11_UFLOAT_PACK32, 4, 0),
    F(R8G8B8A8_TYPELESS, R8G8B8A8_UNORM, 4, T),
    F(R8G8B8A8_UNORM, R8G8B8A8_UNORM, 4, 0),
    F(R8G8B8A8_UNORM_SRGB, R8G8B8A8_SRGB, 4, S),
    F(R8G8B8A8_UINT, R8G8B8A8_UINT, 4, I),
    F(R8G8B8A8_SNORM, R8G8B8A8_SNORM, 4, 0),
    F(R8G8B8A8_SINT, R8G8B8A8_SINT, 4, I),
    F(R16G16_TYPELESS, R16G16_UINT, 4, T),
    F(R16G16_FLOAT, R16G16_SFLOAT, 4, 0),
    F(R16G16_UNORM, R16G16_UNORM, 4, 0),
    F(R16G16_UINT, R16G16_UINT, 4, I),
    F(R16G16_SNORM, R16G16_SNORM, 4, 0),
    F(R16G16_SINT, R16G16_SINT, 4, I),
    D(R32_TYPELESS, R32_UINT, D32_SFLOAT, DEPTH, 4, T),
    D(D32_FLOAT, D32_SFLOAT, D32_SFLOAT, DEPTH, 4, DF),
    D(R32_FLOAT, R32_SFLOAT, D32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0),
    F(R32_UINT, R32_UINT, 4, I),
    F(R32_SINT, R32_SINT, 4, I),
    D(R24G8_TYPELESS, D24_UNORM_S8_UINT, D24_UNORM_S8_UINT, DS, 4, T | DF | SF),
    D(D24_UNORM_S8_UINT, D24_UNORM_S8_UINT, D24_UNORM_S8_UINT, DS, 4, DF | SF),
    D(R24_UNORM_X8_TYPELESS, D24_UNORM_S8_UINT, D24_UNORM_S8_UINT, DEPTH, 4, DF),
    D(X24_TYPELESS_G8_UINT, D24_UNORM_S8_UINT, D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_STENCIL_BIT, 4, SF | I),
    F(R8G8_TYPELESS, R8G8_UNORM, 2, T),
    F(R8G8_UNORM, R8G8_UNORM, 2, 0),
    F(R8G8_UINT, R8G8_UINT, 2, I),
    F(R8G8_SNORM, R8G8_SNORM, 2, 0),
    F(R8G8_SINT, R8G8_SINT, 2, I),
    D(R16_TYPELESS, R16_UINT, D16_UNORM, DEPTH, 2, T),
    F(R16_FLOAT, R16_SFLOAT, 2, 0),
    D(D16_UNORM, D16_UNORM, D16_UNORM, DEPTH, 2, DF),
    D(R16_UNORM, R16_UNORM, D16_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 2, 0),
    F(R16_UINT, R16_UINT, 2, I),
    F(R16_SNORM, R16_SNORM, 2, 0),
    F(R16_SINT, R16_SINT, 2, I),
    F(R8_TYPELESS, R8_UNORM, 1, T),
    F(R8_UNORM, R8_UNORM, 1, 0),
    F(R8_UINT, R8_UINT, 1, I),
    F(R8_SNORM, R8_SNORM, 1, 0),
    F(R8_SINT, R8_SINT, 1, I),
    F(A8_UNORM, R8_UNORM, 1, 0),
    F(R9G9B9E5_SHAREDEXP, E5B9G9R9_UFLOAT_PACK32, 4, 0),
    BC(BC1_TYPELESS, BC1_RGBA_UNORM_BLOCK, 8, T),
    BC(BC1_UNORM, BC1_RGBA_UNORM_BLOCK, 8, 0),
    BC(BC1_UNORM_SRGB, BC1_RGBA_SRGB_BLOCK, 8, S),
    BC(BC2_TYPELESS, BC2_UNORM_BLOCK, 16, T),
    BC(BC2_UNORM, BC2_UNORM_BLOCK, 16, 0),
    BC(BC2_UNORM_SRGB, BC2_SRGB_BLOCK, 16, S),
    BC(BC3_TYPELESS, BC3_UNORM_BLOCK, 16, T),
    BC(BC3_UNORM, BC3_UNORM_BLOCK, 16, 0),
    BC(BC3_UNORM_SRGB, BC3_SRGB_BLOCK, 16, S),
    BC(BC4_TYPELESS, BC4_UNORM_BLOCK, 8, T),
    BC(BC4_UNORM, BC4_UNORM_BLOCK, 8, 0),
    BC(BC4_SNORM, BC4_SNORM_BLOCK, 8, 0),
    BC(BC5_TYPELESS, BC5_UNORM_BLOCK, 16, T),
    BC(BC5_UNORM, BC5_UNORM_BLOCK, 16, 0),
    BC(BC5_SNORM, BC5_SNORM_BLOCK, 16, 0),
    F(B5G6R5_UNORM, R5G6B5_UNORM_PACK16, 2, 0),
    F(B5G5R5A1_UNORM, A1R5G5B5_UNORM_PACK16, 2, 0),
    F(B8G8R8A8_UNORM, B8G8R8A8_UNORM, 4, 0),
    F(B8G8R8X8_UNORM, B8G8R8A8_UNORM, 4, X),
    F(B8G8R8A8_TYPELESS, B8G8R8A8_UNORM, 4, T),
    F(B8G8R8A8_UNORM_SRGB, B8G8R8A8_SRGB, 4, S),
    F(B8G8R8X8_TYPELESS, B8G8R8A8_UNORM, 4, T | X),
    F(B8G8R8X8_UNORM_SRGB, B8G8R8A8_SRGB, 4, S | X),
    BC(BC6H_TYPELESS, BC6H_UFLOAT_BLOCK, 16, T),
    BC(BC6H_UF16, BC6H_UFLOAT_BLOCK, 16, 0),
    BC(BC6H_SF16, BC6H_SFLOAT_BLOCK, 16, 0),
    BC(BC7_TYPELESS, BC7_UNORM_BLOCK, 16, T),
    BC(BC7_UNORM, BC7_UNORM_BLOCK, 16, 0),
    BC(BC7_UNORM_SRGB, BC7_SRGB_BLOCK, 16, S),
    F(B4G4R4A4_UNORM, A4R4G4B4_UNORM_PACK16, 2, 0),
    [DXGI_FORMAT_NV12] = {VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, VK_FORMAT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, PL},
    [DXGI_FORMAT_P010] = {VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16, VK_FORMAT_UNDEFINED,
                          VK_IMAGE_ASPECT_COLOR_BIT, 2, 1, PL},
    [DXGI_FORMAT_P016] = {VK_FORMAT_G16_B16R16_2PLANE_420_UNORM, VK_FORMAT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT, 2, 1, PL},
};

static const D3dFormat g_unknown = {VK_FORMAT_UNDEFINED};

const D3dFormat *D3dGetFormat(DXGI_FORMAT format)
{
    if ((UINT)format >= RTL_NUMBER_OF(g_formats) || g_formats[format].vk == VK_FORMAT_UNDEFINED)
        return &g_unknown;
    return &g_formats[format];
}

/* the device falls back to 32 bit depth where the gpu has no 24 bit depth buffers, as amd's. */
void D3dFormatsUseD32(void)
{
    static const DXGI_FORMAT d24[] = {DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT,
                                      DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_X24_TYPELESS_G8_UINT};
    for (UINT i = 0; i < RTL_NUMBER_OF(d24); i++)
        g_formats[d24[i]].vk = g_formats[d24[i]].depth = VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkImageUsageFlags D3dImageUsage(UINT bind, UINT misc, D3D11_USAGE usage)
{
    VkImageUsageFlags flags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (bind & D3D11_BIND_SHADER_RESOURCE)
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (bind & D3D11_BIND_RENDER_TARGET)
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (bind & D3D11_BIND_DEPTH_STENCIL)
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (bind & D3D11_BIND_UNORDERED_ACCESS)
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    /* the decoder writes a picture and reads it as a reference. */
    if (bind & D3D11_BIND_DECODER)
        flags |= VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
    return flags;
}

/* what CheckFormatSupport reports, from what vulkan supports with optimal tiling and in buffers. */
UINT D3dFormatSupport(D3dDevice *device, DXGI_FORMAT format, UINT *support2)
{
    const D3dFormat *f = D3dGetFormat(format);
    if (support2)
        *support2 = 0;
    if (f->vk == VK_FORMAT_UNDEFINED)
        return 0;
    if (f->flags & D3D_FORMAT_PLANAR) {
        /* planes are read through views of their own formats, copied and decoded into whole. */
        UINT depth = format == DXGI_FORMAT_NV12 ? D3D_VIDEO_8BIT : format == DXGI_FORMAT_P010 ? D3D_VIDEO_10BIT : 0;
        UINT planar = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_LOAD |
                      D3D11_FORMAT_SUPPORT_SHADER_SAMPLE | D3D11_FORMAT_SUPPORT_CPU_LOCKABLE;
        if (device->video_depths & depth)
            planar |= D3D11_FORMAT_SUPPORT_DECODER_OUTPUT;
        if (support2)
            *support2 = D3D11_FORMAT_SUPPORT2_SHAREABLE;
        return planar;
    }
    VkFormatProperties p;
    VkFormat vk = (f->flags & (D3D_FORMAT_DEPTH | D3D_FORMAT_STENCIL)) && f->depth ? f->depth : f->vk;
    device->vk.vkGetPhysicalDeviceFormatProperties(device->vk.physical, vk, &p);
    VkFormatFeatureFlags image = p.optimalTilingFeatures, buffer = p.bufferFeatures;
    UINT s = 0;
    if (f->flags & D3D_FORMAT_TYPELESS) {
        /* typeless formats only create resources. */
        s = D3D11_FORMAT_SUPPORT_TEXTURE1D | D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_TEXTURE3D |
            D3D11_FORMAT_SUPPORT_TEXTURECUBE | D3D11_FORMAT_SUPPORT_MIP | D3D11_FORMAT_SUPPORT_CAST_WITHIN_BIT_LAYOUT;
        if (f->block == 1)
            s |= D3D11_FORMAT_SUPPORT_BUFFER;
        return image ? s : 0;
    }
    if (buffer & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)
        s |= D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER | D3D11_FORMAT_SUPPORT_BUFFER;
    if (format == DXGI_FORMAT_R16_UINT || format == DXGI_FORMAT_R32_UINT)
        s |= D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER;
    if (buffer & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT)
        s |= D3D11_FORMAT_SUPPORT_BUFFER | D3D11_FORMAT_SUPPORT_SHADER_LOAD;
    if (image & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
        s |= D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_TEXTURECUBE | D3D11_FORMAT_SUPPORT_MIP |
             D3D11_FORMAT_SUPPORT_SHADER_LOAD | D3D11_FORMAT_SUPPORT_CPU_LOCKABLE |
             D3D11_FORMAT_SUPPORT_CAST_WITHIN_BIT_LAYOUT;
        if (!(f->flags & (D3D_FORMAT_DEPTH | D3D_FORMAT_STENCIL)))
            s |= D3D11_FORMAT_SUPPORT_TEXTURE1D | D3D11_FORMAT_SUPPORT_TEXTURE3D;
        if (!(f->flags & D3D_FORMAT_INT) && (image & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
            s |= D3D11_FORMAT_SUPPORT_SHADER_SAMPLE | D3D11_FORMAT_SUPPORT_SHADER_GATHER;
        if (f->depth && !(f->flags & D3D_FORMAT_INT))
            s |= D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_COMPARISON | D3D11_FORMAT_SUPPORT_SHADER_GATHER_COMPARISON;
    }
    if (image & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) {
        s |= D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET |
             D3D11_FORMAT_SUPPORT_MULTISAMPLE_LOAD;
        if (!(f->flags & D3D_FORMAT_INT))
            s |= D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE;
        if (image & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
            s |= D3D11_FORMAT_SUPPORT_BLENDABLE;
        if ((image & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) && !(f->flags & D3D_FORMAT_INT))
            s |= D3D11_FORMAT_SUPPORT_MIP_AUTOGEN;
    }
    if (image & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        s |= D3D11_FORMAT_SUPPORT_DEPTH_STENCIL | D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_TEXTURECUBE |
             D3D11_FORMAT_SUPPORT_MIP | D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET;
    if ((image & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) || (buffer & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT))
        s |= D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        s |= D3D11_FORMAT_SUPPORT_DISPLAY | D3D11_FORMAT_SUPPORT_BACK_BUFFER_CAST;
        break;
    default:
        break;
    }
    if (support2) {
        UINT s2 = 0;
        if (s & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) {
            s2 |= D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE;
            if (image & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
                s2 |= D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
        }
        if (format == DXGI_FORMAT_R32_UINT || format == DXGI_FORMAT_R32_SINT)
            s2 |= D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_ADD | D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS |
                  D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE |
                  D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE | D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX |
                  D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX;
        if (s & D3D11_FORMAT_SUPPORT_RENDER_TARGET && (f->flags & D3D_FORMAT_INT))
            s2 |= D3D11_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP;
        if (s & (D3D11_FORMAT_SUPPORT_TEXTURE2D))
            s2 |= D3D11_FORMAT_SUPPORT2_SHAREABLE;
        *support2 = s2;
    }
    return s;
}

/* ------------------------------------------------------------------ clear values */

typedef enum ChannelKind { CH_UNORM, CH_SNORM, CH_UINT, CH_SINT, CH_FLOAT, CH_UFLOAT } ChannelKind;

/* the channels of a color format from its lowest bit, in r, g, b, a order: their widths and kind. */
static BOOL channels(VkFormat f, BYTE bits[4], UINT *count, ChannelKind *kind)
{
    static const struct {
        VkFormat vk;
        BYTE count, bits[4];
        ChannelKind kind;
    } TABLE[] = {
        {VK_FORMAT_R32G32B32A32_SFLOAT, 4, {32, 32, 32, 32}, CH_FLOAT},
        {VK_FORMAT_R32G32B32A32_UINT, 4, {32, 32, 32, 32}, CH_UINT},
        {VK_FORMAT_R32G32B32A32_SINT, 4, {32, 32, 32, 32}, CH_SINT},
        {VK_FORMAT_R32G32B32_SFLOAT, 3, {32, 32, 32}, CH_FLOAT},
        {VK_FORMAT_R32G32B32_UINT, 3, {32, 32, 32}, CH_UINT},
        {VK_FORMAT_R32G32B32_SINT, 3, {32, 32, 32}, CH_SINT},
        {VK_FORMAT_R16G16B16A16_SFLOAT, 4, {16, 16, 16, 16}, CH_FLOAT},
        {VK_FORMAT_R16G16B16A16_UNORM, 4, {16, 16, 16, 16}, CH_UNORM},
        {VK_FORMAT_R16G16B16A16_UINT, 4, {16, 16, 16, 16}, CH_UINT},
        {VK_FORMAT_R16G16B16A16_SNORM, 4, {16, 16, 16, 16}, CH_SNORM},
        {VK_FORMAT_R16G16B16A16_SINT, 4, {16, 16, 16, 16}, CH_SINT},
        {VK_FORMAT_R32G32_SFLOAT, 2, {32, 32}, CH_FLOAT},
        {VK_FORMAT_R32G32_UINT, 2, {32, 32}, CH_UINT},
        {VK_FORMAT_R32G32_SINT, 2, {32, 32}, CH_SINT},
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4, {10, 10, 10, 2}, CH_UNORM},
        {VK_FORMAT_A2B10G10R10_UINT_PACK32, 4, {10, 10, 10, 2}, CH_UINT},
        {VK_FORMAT_B10G11R11_UFLOAT_PACK32, 3, {11, 11, 10}, CH_UFLOAT},
        {VK_FORMAT_R8G8B8A8_UNORM, 4, {8, 8, 8, 8}, CH_UNORM},
        {VK_FORMAT_R8G8B8A8_UINT, 4, {8, 8, 8, 8}, CH_UINT},
        {VK_FORMAT_R8G8B8A8_SNORM, 4, {8, 8, 8, 8}, CH_SNORM},
        {VK_FORMAT_R8G8B8A8_SINT, 4, {8, 8, 8, 8}, CH_SINT},
        {VK_FORMAT_R16G16_SFLOAT, 2, {16, 16}, CH_FLOAT},
        {VK_FORMAT_R16G16_UNORM, 2, {16, 16}, CH_UNORM},
        {VK_FORMAT_R16G16_UINT, 2, {16, 16}, CH_UINT},
        {VK_FORMAT_R16G16_SNORM, 2, {16, 16}, CH_SNORM},
        {VK_FORMAT_R16G16_SINT, 2, {16, 16}, CH_SINT},
        {VK_FORMAT_R32_SFLOAT, 1, {32}, CH_FLOAT},
        {VK_FORMAT_R32_UINT, 1, {32}, CH_UINT},
        {VK_FORMAT_R32_SINT, 1, {32}, CH_SINT},
        {VK_FORMAT_R8G8_UNORM, 2, {8, 8}, CH_UNORM},
        {VK_FORMAT_R8G8_UINT, 2, {8, 8}, CH_UINT},
        {VK_FORMAT_R8G8_SNORM, 2, {8, 8}, CH_SNORM},
        {VK_FORMAT_R8G8_SINT, 2, {8, 8}, CH_SINT},
        {VK_FORMAT_R16_SFLOAT, 1, {16}, CH_FLOAT},
        {VK_FORMAT_R16_UNORM, 1, {16}, CH_UNORM},
        {VK_FORMAT_R16_UINT, 1, {16}, CH_UINT},
        {VK_FORMAT_R16_SNORM, 1, {16}, CH_SNORM},
        {VK_FORMAT_R16_SINT, 1, {16}, CH_SINT},
        {VK_FORMAT_R8_UNORM, 1, {8}, CH_UNORM},
        {VK_FORMAT_R8_UINT, 1, {8}, CH_UINT},
        {VK_FORMAT_R8_SNORM, 1, {8}, CH_SNORM},
        {VK_FORMAT_R8_SINT, 1, {8}, CH_SINT},
    };
    for (UINT i = 0; i < RTL_NUMBER_OF(TABLE); i++)
        if (TABLE[i].vk == f) {
            *count = TABLE[i].count;
            memcpy(bits, TABLE[i].bits, 4);
            *kind = TABLE[i].kind;
            return TRUE;
        }
    return FALSE;
}

static UINT float_bits(FLOAT f)
{
    UINT u;
    memcpy(&u, &f, 4);
    return u;
}

static FLOAT bits_float(UINT u)
{
    FLOAT f;
    memcpy(&f, &u, 4);
    return f;
}

/* a float as a small float of the exponent and mantissa widths, rounded to nearest even; signed adds a sign bit. */
static UINT to_small_float(FLOAT f, UINT exponent_bits, UINT mantissa_bits, BOOL is_signed)
{
    UINT u = float_bits(f), sign = u >> 31, bias = (1u << (exponent_bits - 1)) - 1;
    UINT e = (u >> 23) & 0xFF, m = u & 0x7FFFFF, max_e = (1u << exponent_bits) - 1;
    UINT out;
    if (!is_signed && sign)
        return e == 0xFF && m ? max_e << mantissa_bits | 1 : 0;
    if (e == 0xFF)
        out = max_e << mantissa_bits | (m ? 1u << (mantissa_bits - 1) : 0);
    else if ((int)e - 127 + (int)bias >= (int)max_e)
        out = max_e << mantissa_bits;
    else {
        int ne = (int)e - 127 + (int)bias;
        UINT full = m | 0x800000, shift = 23 - mantissa_bits;
        if (ne <= 0) {
            /* denormal: one more bit shifted out for each step below the smallest exponent. */
            shift += (UINT)(1 - ne);
            ne = 0;
            if (shift > 24) {
                out = 0;
                goto done;
            }
        } else {
            full &= 0x7FFFFF;
        }
        UINT kept = full >> shift, rest = full & ((1u << shift) - 1), half = 1u << (shift - 1);
        if (rest > half || (rest == half && (kept & 1)))
            kept++;
        out = ((UINT)ne << mantissa_bits) + kept;
        if (out >= max_e << mantissa_bits)
            out = max_e << mantissa_bits;
    }
done:
    return is_signed ? out | sign << (exponent_bits + mantissa_bits) : out;
}

static FLOAT from_small_float(UINT v, UINT exponent_bits, UINT mantissa_bits, BOOL is_signed)
{
    UINT sign = is_signed ? (v >> (exponent_bits + mantissa_bits)) & 1 : 0;
    UINT e = (v >> mantissa_bits) & ((1u << exponent_bits) - 1), m = v & ((1u << mantissa_bits) - 1);
    UINT bias = (1u << (exponent_bits - 1)) - 1, out;
    if (e == (1u << exponent_bits) - 1)
        out = 0xFFu << 23 | m << (23 - mantissa_bits);
    else if (e)
        out = (e - bias + 127) << 23 | m << (23 - mantissa_bits);
    else if (!m)
        out = 0;
    else {
        /* denormal: normalized for a float. */
        int ne = 1 - (int)bias;
        while (!(m & (1u << mantissa_bits))) {
            m <<= 1;
            ne--;
        }
        out = (UINT)(ne + 127) << 23 | (m & ((1u << mantissa_bits) - 1)) << (23 - mantissa_bits);
    }
    return bits_float(out | sign << 31);
}

static UINT round_half(FLOAT f) { return (UINT)(f + 0.5f); }

/*
 * one element of a color format holding a clear: the low bits of each
 * value for clears of uints, as ClearUnorderedAccessViewUint writes them
 * whatever the format, or the floats converted to the format for clears
 * of floats. FALSE for formats without channels to clear, and for clears
 * of floats on integer formats, which direct3d drops.
 */
BOOL D3dPackClear(VkFormat format, const UINT *uints, const FLOAT *floats, BYTE out[16], UINT *size)
{
    BYTE bits[4];
    UINT count;
    ChannelKind kind;
    if (!channels(format, bits, &count, &kind) || (floats && (kind == CH_UINT || kind == CH_SINT)))
        return FALSE;
    memset(out, 0, 16);
    UINT at = 0;
    for (UINT i = 0; i < count; i++) {
        UINT n = bits[i], mask = n == 32 ? ~0u : (1u << n) - 1, v;
        if (uints)
            v = uints[i] & mask;
        else if (kind == CH_UNORM)
            v = round_half(D3D_MAX(0.0f, D3D_MIN(floats[i], 1.0f)) * (FLOAT)mask);
        else if (kind == CH_SNORM) {
            FLOAT f = D3D_MAX(-1.0f, D3D_MIN(floats[i], 1.0f)) * (FLOAT)(mask >> 1);
            v = (UINT)(INT)(f < 0 ? f - 0.5f : f + 0.5f) & mask;
        } else if (kind == CH_UFLOAT)
            v = to_small_float(floats[i], 5, n - 5, FALSE);
        else if (n == 16)
            v = to_small_float(floats[i], 5, 10, TRUE);
        else
            v = float_bits(floats[i]);
        /* channels are packed from the lowest bit, the bytes little endian. */
        for (UINT b = 0; b < n; b++, at++)
            if (v >> b & 1)
                out[at / 8] |= (BYTE)(1u << (at % 8));
    }
    *size = (at + 7) / 8;
    return TRUE;
}

/* the clear value of an image of the format that writes an element's bits exactly. */
BOOL D3dClearValueOfBits(VkFormat format, const BYTE *element, VkClearColorValue *value)
{
    BYTE bits[4];
    UINT count;
    ChannelKind kind;
    if (!channels(format, bits, &count, &kind))
        return FALSE;
    memset(value, 0, sizeof(*value));
    UINT at = 0;
    for (UINT i = 0; i < count; i++) {
        UINT n = bits[i], mask = n == 32 ? ~0u : (1u << n) - 1, v = 0;
        for (UINT b = 0; b < n; b++, at++)
            v |= (UINT)(element[at / 8] >> (at % 8) & 1) << b;
        UINT sign = n == 32 ? 0 : v >> (n - 1) & 1;
        INT s = sign ? (INT)(v | ~mask) : (INT)v;
        switch (kind) {
        case CH_UINT:
            value->uint32[i] = v;
            break;
        case CH_SINT:
            value->int32[i] = s;
            break;
        case CH_UNORM:
            value->float32[i] = (FLOAT)v / (FLOAT)mask;
            break;
        case CH_SNORM:
            value->float32[i] = D3D_MAX((FLOAT)s / (FLOAT)(mask >> 1), -1.0f);
            break;
        case CH_UFLOAT:
            value->float32[i] = from_small_float(v, 5, n - 5, FALSE);
            break;
        case CH_FLOAT:
            value->float32[i] = n == 16 ? from_small_float(v, 5, 10, TRUE) : bits_float(v);
            break;
        }
    }
    return TRUE;
}
