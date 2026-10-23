/*
 * private d3d11.dll extensions for d3d9.dll: swizzled views for direct3d 9's luminance and depth formats, scaled
 * copies, and direct3d 9 draw topologies.
 */
#ifndef MIME_D3D11_MIME_H
#define MIME_D3D11_MIME_H

#include <mime/win32/d3d11.h>

/* each component is a VkComponentSwizzle: 1 zero, 2 one, 3 to 6 red to alpha. */
#define MIME_SWIZZLE(r, g, b, a) ((r) | ((g) << 4) | ((b) << 8) | ((a) << 12))
#define MIME_SWIZZLE_ZERO 1
#define MIME_SWIZZLE_ONE 2
#define MIME_SWIZZLE_R 3
#define MIME_SWIZZLE_G 4
#define MIME_SWIZZLE_B 5
#define MIME_SWIZZLE_A 6

/* direct3d 9 topologies: strips that never restart, and fans. */
#define MIME_TOPOLOGY_D3D9 0x10000
#define MIME_TOPOLOGY_TRIANGLEFAN (MIME_TOPOLOGY_D3D9 | 0xFF)

typedef struct IMimeD3D11Device IMimeD3D11Device;
#define IMimeD3D11Device_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateSwizzledView)(T * This, ID3D11Resource * resource,                                \
                                                   const D3D11_SHADER_RESOURCE_VIEW_DESC *desc, UINT swizzle,          \
                                                   ID3D11ShaderResourceView **view);                                   \
    void(STDMETHODCALLTYPE *Blit)(T * This, ID3D11Resource * dst, UINT dst_sub, const RECT *dst_rect,                  \
                                  ID3D11Resource *src, UINT src_sub, const RECT *src_rect, BOOL linear);       \
    void(STDMETHODCALLTYPE *ClearDepthStencil)(T * This, ID3D11DepthStencilView * view, UINT flags, FLOAT depth,       \
                                               UINT8 stencil, const D3D11_RECT *rects, UINT count);
MIME_COM_INTERFACE(IMimeD3D11Device)
MIME_DEFINE_GUID(IID_IMimeD3D11Device, 0x6B1E8F42, 0x3C7D, 0x4E19, 0x9A, 0x2F, 0x51, 0xD8, 0x0C, 0x7E, 0x93, 0xB4);

#endif
