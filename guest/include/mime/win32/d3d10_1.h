/* direct3d 10.1: feature levels, cube arrays in views and blending per target. */
#ifndef MIME_WIN32_D3D10_1_H
#define MIME_WIN32_D3D10_1_H

#include <mime/win32/d3d10.h>

#define D3D10_1_SDK_VERSION 32

typedef enum D3D10_FEATURE_LEVEL1 {
    D3D10_FEATURE_LEVEL_10_0 = 0xA000,
    D3D10_FEATURE_LEVEL_10_1 = 0xA100,
    D3D10_FEATURE_LEVEL_9_1 = 0x9100,
    D3D10_FEATURE_LEVEL_9_2 = 0x9200,
    D3D10_FEATURE_LEVEL_9_3 = 0x9300
} D3D10_FEATURE_LEVEL1;

typedef D3D_SRV_DIMENSION D3D10_SRV_DIMENSION1;
#define D3D10_1_SRV_DIMENSION_UNKNOWN D3D_SRV_DIMENSION_UNKNOWN
#define D3D10_1_SRV_DIMENSION_BUFFER D3D_SRV_DIMENSION_BUFFER
#define D3D10_1_SRV_DIMENSION_TEXTURE1D D3D_SRV_DIMENSION_TEXTURE1D
#define D3D10_1_SRV_DIMENSION_TEXTURE1DARRAY D3D_SRV_DIMENSION_TEXTURE1DARRAY
#define D3D10_1_SRV_DIMENSION_TEXTURE2D D3D_SRV_DIMENSION_TEXTURE2D
#define D3D10_1_SRV_DIMENSION_TEXTURE2DARRAY D3D_SRV_DIMENSION_TEXTURE2DARRAY
#define D3D10_1_SRV_DIMENSION_TEXTURE2DMS D3D_SRV_DIMENSION_TEXTURE2DMS
#define D3D10_1_SRV_DIMENSION_TEXTURE2DMSARRAY D3D_SRV_DIMENSION_TEXTURE2DMSARRAY
#define D3D10_1_SRV_DIMENSION_TEXTURE3D D3D_SRV_DIMENSION_TEXTURE3D
#define D3D10_1_SRV_DIMENSION_TEXTURECUBE D3D_SRV_DIMENSION_TEXTURECUBE
#define D3D10_1_SRV_DIMENSION_TEXTURECUBEARRAY D3D_SRV_DIMENSION_TEXTURECUBEARRAY

typedef struct D3D10_TEXCUBE_ARRAY_SRV1 {
    UINT MostDetailedMip;
    UINT MipLevels;
    UINT First2DArrayFace;
    UINT NumCubes;
} D3D10_TEXCUBE_ARRAY_SRV1;

typedef struct D3D10_SHADER_RESOURCE_VIEW_DESC1 {
    DXGI_FORMAT Format;
    D3D10_SRV_DIMENSION1 ViewDimension;
    union {
        D3D10_BUFFER_SRV Buffer;
        D3D10_TEX1D_SRV Texture1D;
        D3D10_TEX1D_ARRAY_SRV Texture1DArray;
        D3D10_TEX2D_SRV Texture2D;
        D3D10_TEX2D_ARRAY_SRV Texture2DArray;
        D3D10_TEX2DMS_SRV Texture2DMS;
        D3D10_TEX2DMS_ARRAY_SRV Texture2DMSArray;
        D3D10_TEX3D_SRV Texture3D;
        D3D10_TEXCUBE_SRV TextureCube;
        D3D10_TEXCUBE_ARRAY_SRV1 TextureCubeArray;
    };
} D3D10_SHADER_RESOURCE_VIEW_DESC1;

typedef struct D3D10_RENDER_TARGET_BLEND_DESC1 {
    BOOL BlendEnable;
    D3D10_BLEND SrcBlend;
    D3D10_BLEND DestBlend;
    D3D10_BLEND_OP BlendOp;
    D3D10_BLEND SrcBlendAlpha;
    D3D10_BLEND DestBlendAlpha;
    D3D10_BLEND_OP BlendOpAlpha;
    UINT8 RenderTargetWriteMask;
} D3D10_RENDER_TARGET_BLEND_DESC1;

typedef struct D3D10_BLEND_DESC1 {
    BOOL AlphaToCoverageEnable;
    BOOL IndependentBlendEnable;
    D3D10_RENDER_TARGET_BLEND_DESC1 RenderTarget[8];
} D3D10_BLEND_DESC1;

typedef struct ID3D10BlendState1 ID3D10BlendState1;
typedef struct ID3D10ShaderResourceView1 ID3D10ShaderResourceView1;
typedef struct ID3D10Device1 ID3D10Device1;

#define ID3D10BlendState1_METHODS(T)                                                                                   \
    ID3D10BlendState_METHODS(T)                                                                                        \
    void(STDMETHODCALLTYPE *GetDesc1)(T *This, D3D10_BLEND_DESC1 *desc);

#define ID3D10ShaderResourceView1_METHODS(T)                                                                           \
    ID3D10ShaderResourceView_METHODS(T)                                                                                \
    void(STDMETHODCALLTYPE *GetDesc1)(T *This, D3D10_SHADER_RESOURCE_VIEW_DESC1 *desc);

#define ID3D10Device1_METHODS(T)                                                                                       \
    ID3D10Device_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *CreateShaderResourceView1)(T *This, ID3D10Resource *resource,                           \
                                                          const D3D10_SHADER_RESOURCE_VIEW_DESC1 *desc,                \
                                                          ID3D10ShaderResourceView1 **view);                           \
    HRESULT(STDMETHODCALLTYPE *CreateBlendState1)(T *This, const D3D10_BLEND_DESC1 *desc, ID3D10BlendState1 **state);  \
    D3D10_FEATURE_LEVEL1(STDMETHODCALLTYPE *GetFeatureLevel)(T *This);

MIME_COM_INTERFACE(ID3D10BlendState1)
MIME_COM_INTERFACE(ID3D10ShaderResourceView1)
MIME_COM_INTERFACE(ID3D10Device1)

MIME_DEFINE_GUID(IID_ID3D10BlendState1, 0xEDAD8D99, 0x8A35, 0x4D6D, 0x85, 0x66, 0x2E, 0xA2, 0x76, 0xCD, 0xE1, 0x61);
MIME_DEFINE_GUID(IID_ID3D10ShaderResourceView1, 0x9B7E4C87, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89,
                 0xF0);
MIME_DEFINE_GUID(IID_ID3D10Device1, 0x9B7E4C8F, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);

#ifndef MIME_BUILD_D3D10_1
#define D3D10_1API DECLSPEC_IMPORT
#else
#define D3D10_1API DECLSPEC_EXPORT
#endif

D3D10_1API HRESULT WINAPI D3D10CreateDevice1(IDXGIAdapter *adapter, D3D10_DRIVER_TYPE type, HMODULE software,
                                             UINT flags, D3D10_FEATURE_LEVEL1 level, UINT sdk,
                                             ID3D10Device1 **device);
D3D10_1API HRESULT WINAPI D3D10CreateDeviceAndSwapChain1(IDXGIAdapter *adapter, D3D10_DRIVER_TYPE type,
                                                         HMODULE software, UINT flags, D3D10_FEATURE_LEVEL1 level,
                                                         UINT sdk, DXGI_SWAP_CHAIN_DESC *desc,
                                                         IDXGISwapChain **swapchain, ID3D10Device1 **device);

#endif
