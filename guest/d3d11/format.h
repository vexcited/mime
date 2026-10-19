/*
 * dxgi formats as vulkan formats, shared by d3d11.dll and d3d12.dll, which
 * both build format.c.
 */
#ifndef D3D_FORMAT_H
#define D3D_FORMAT_H

/* what a dxgi format is to vulkan and to direct3d. */
typedef struct D3dFormat {
    VkFormat vk;
    /* the vulkan format of typeless depth formats seen as color, and of their depth views. */
    VkFormat depth;
    VkImageAspectFlags aspect;
    /* bytes per block and the block's size in texels: 1 for uncompressed formats, 4 for bc. */
    BYTE block_bytes;
    BYTE block;
    BYTE flags;
} D3dFormat;

#define D3D_FORMAT_TYPELESS 0x01
#define D3D_FORMAT_SRGB 0x02
#define D3D_FORMAT_DEPTH 0x04
#define D3D_FORMAT_STENCIL 0x08
#define D3D_FORMAT_INT 0x10
#define D3D_FORMAT_BGRX 0x20
/* video formats: a luma plane, then one of interleaved chroma at half the size each way. */
#define D3D_FORMAT_PLANAR 0x40

const D3dFormat *D3dGetFormat(DXGI_FORMAT format);
BOOL D3dPackClear(VkFormat format, const UINT *uints, const FLOAT *floats, BYTE out[16], UINT *size);
BOOL D3dClearValueOfBits(VkFormat format, const BYTE *element, VkClearColorValue *value);
void D3dFormatsUseD32(void);

#endif
