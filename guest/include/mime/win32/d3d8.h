/*
 * direct3d 8: the types that differ from direct3d 9, the d3dvsd vertex declaration tokens and the interfaces, in
 * windows' method order. shared types come from d3d9types.h. when d3d9.h is included first, this header's types
 * get an 8 appended to their names.
 */
#ifndef MIME_WIN32_D3D8_H
#define MIME_WIN32_D3D8_H

#ifdef MIME_WIN32_D3D9TYPES_H
#define MIME_D3D8_NAME(n) n##8
#define MIME_D3D8_RENAMED 1
#else
#define MIME_D3D8_TYPES 1
#define MIME_D3D8_NAME(n) n
#endif

#include <mime/win32/objbase.h>
#include <mime/win32/d3d9types.h>
#include <mime/win32/d3d9caps.h>

#ifndef MIME_D3D8_RENAMED
#define D3D_SDK_VERSION 220
#define D3DSWAPEFFECT_DISCARD 1
#define D3DSWAPEFFECT_FLIP 2
#define D3DSWAPEFFECT_COPY 3
typedef DWORD D3DSWAPEFFECT;
#define D3DADAPTER_DEFAULT 0
#define D3DCREATE_FPU_PRESERVE 0x00000002
#define D3DCREATE_MULTITHREADED 0x00000004
#define D3DCREATE_PUREDEVICE 0x00000010
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x00000020
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040
#define D3DCREATE_MIXED_VERTEXPROCESSING 0x00000080
#define D3DCREATE_DISABLE_DRIVER_MANAGEMENT 0x00000100
#define D3DSGR_NO_CALIBRATION 0x00000000
#define D3DSGR_CALIBRATE 0x00000001
#define D3DCURSOR_IMMEDIATE_UPDATE 0x00000001
#define D3DPV_DONOTCOPYDATA 0x00000001
#define _FACD3D 0x876
#define MAKE_D3DHRESULT(code) ((HRESULT)(0x88760000 | (code)))
#define D3D_OK S_OK
#define D3DERR_WRONGTEXTUREFORMAT MAKE_D3DHRESULT(2072)
#define D3DERR_UNSUPPORTEDCOLOROPERATION MAKE_D3DHRESULT(2073)
#define D3DERR_UNSUPPORTEDCOLORARG MAKE_D3DHRESULT(2074)
#define D3DERR_UNSUPPORTEDALPHAOPERATION MAKE_D3DHRESULT(2075)
#define D3DERR_UNSUPPORTEDALPHAARG MAKE_D3DHRESULT(2076)
#define D3DERR_TOOMANYOPERATIONS MAKE_D3DHRESULT(2077)
#define D3DERR_CONFLICTINGTEXTUREFILTER MAKE_D3DHRESULT(2078)
#define D3DERR_UNSUPPORTEDFACTORVALUE MAKE_D3DHRESULT(2079)
#define D3DERR_CONFLICTINGRENDERSTATE MAKE_D3DHRESULT(2081)
#define D3DERR_UNSUPPORTEDTEXTUREFILTER MAKE_D3DHRESULT(2082)
#define D3DERR_CONFLICTINGTEXTUREPALETTE MAKE_D3DHRESULT(2086)
#define D3DERR_DRIVERINTERNALERROR MAKE_D3DHRESULT(2087)
#define D3DERR_NOTFOUND MAKE_D3DHRESULT(2150)
#define D3DERR_MOREDATA MAKE_D3DHRESULT(2151)
#define D3DERR_DEVICELOST MAKE_D3DHRESULT(2152)
#define D3DERR_DEVICENOTRESET MAKE_D3DHRESULT(2153)
#define D3DERR_NOTAVAILABLE MAKE_D3DHRESULT(2154)
#define D3DERR_OUTOFVIDEOMEMORY MAKE_D3DHRESULT(380)
#define D3DERR_INVALIDDEVICE MAKE_D3DHRESULT(2155)
#define D3DERR_INVALIDCALL MAKE_D3DHRESULT(2156)
#define D3DERR_DRIVERINVALIDCALL MAKE_D3DHRESULT(2157)
#endif

/* the swap effect, states and caps that direct3d 9 renamed or dropped. */
#define D3DSWAPEFFECT_COPY_VSYNC 4
#define D3DCAPS2_NO2DDURING3DSCENE 0x00000002
#define D3DCAPS2_CANRENDERWINDOWED 0x00080000
#define D3DPMISCCAPS_LINEPATTERNREP 0x00000004
#define D3DPRASTERCAPS_PAT 0x00000008
#define D3DPRASTERCAPS_ANTIALIASEDGES 0x00001000
#define D3DPRASTERCAPS_ZBIAS 0x00004000
#define D3DPRASTERCAPS_STRETCHBLTMULTISAMPLE 0x00800000
#define D3DVTXPCAPS_NO_VSDT_UBYTE4 0x00000080
#define D3DRS_LINEPATTERN ((D3DRENDERSTATETYPE)10)
#define D3DRS_ZVISIBLE ((D3DRENDERSTATETYPE)30)
#define D3DRS_EDGEANTIALIAS ((D3DRENDERSTATETYPE)40)
#define D3DRS_ZBIAS ((D3DRENDERSTATETYPE)47)
#define D3DRS_SOFTWAREVERTEXPROCESSING ((D3DRENDERSTATETYPE)153)
#define D3DRS_PATCHSEGMENTS ((D3DRENDERSTATETYPE)164)
#define D3DRS_POSITIONORDER ((D3DRENDERSTATETYPE)172)
#define D3DRS_NORMALORDER ((D3DRENDERSTATETYPE)173)
#define D3DTSS_ADDRESSU ((D3DTEXTURESTAGESTATETYPE)13)
#define D3DTSS_ADDRESSV ((D3DTEXTURESTAGESTATETYPE)14)
#define D3DTSS_BORDERCOLOR ((D3DTEXTURESTAGESTATETYPE)15)
#define D3DTSS_MAGFILTER ((D3DTEXTURESTAGESTATETYPE)16)
#define D3DTSS_MINFILTER ((D3DTEXTURESTAGESTATETYPE)17)
#define D3DTSS_MIPFILTER ((D3DTEXTURESTAGESTATETYPE)18)
#define D3DTSS_MIPMAPLODBIAS ((D3DTEXTURESTAGESTATETYPE)19)
#define D3DTSS_MAXMIPLEVEL ((D3DTEXTURESTAGESTATETYPE)20)
#define D3DTSS_MAXANISOTROPY ((D3DTEXTURESTAGESTATETYPE)21)
#define D3DTSS_ADDRESSW ((D3DTEXTURESTAGESTATETYPE)25)
#define D3DDEVINFOID_TEXTUREMANAGER 1
#define D3DDEVINFOID_D3DTEXTUREMANAGER 2
#define D3DDEVINFOID_TEXTURING 3
#define D3DDEVINFOID_VCACHE 4
#define D3DDEVINFOID_RESOURCEMANAGER 5
#define D3DDEVINFOID_VERTEXSTATS 6
#define D3DENUM_NO_WHQL_LEVEL 0x00000002
#ifndef D3DSPD_IUNKNOWN
#define D3DSPD_IUNKNOWN 0x00000001
#endif
#define D3DPRESENT_INTERVAL_DEFAULT_8 0

typedef D3DVIEWPORT9 D3DVIEWPORT8;
typedef D3DMATERIAL9 D3DMATERIAL8;
typedef D3DLIGHT9 D3DLIGHT8;
typedef D3DCLIPSTATUS9 D3DCLIPSTATUS8;

/* ------------------------------------------------------------------ vertex declarations */

#define D3DVSD_TOKENTYPESHIFT 29
#define D3DVSD_TOKENTYPEMASK (7u << D3DVSD_TOKENTYPESHIFT)
#define D3DVSD_TOKEN_NOP 0
#define D3DVSD_TOKEN_STREAM 1
#define D3DVSD_TOKEN_STREAMDATA 2
#define D3DVSD_TOKEN_TESSELLATOR 3
#define D3DVSD_TOKEN_CONSTMEM 4
#define D3DVSD_TOKEN_EXT 5
#define D3DVSD_TOKEN_END 7
#define D3DVSD_STREAMTESSSHIFT 28
#define D3DVSD_DATALOADTYPESHIFT 28
#define D3DVSD_DATATYPESHIFT 16
#define D3DVSD_SKIPCOUNTSHIFT 16
#define D3DVSD_VERTEXREGSHIFT 0
#define D3DVSD_VERTEXREGINSHIFT 20
#define D3DVSD_CONSTCOUNTSHIFT 25
#define D3DVSD_CONSTADDRESSSHIFT 0
#define D3DVSD_EXTCOUNTSHIFT 24
#define D3DVSD_MAKETOKENTYPE(t) ((DWORD)(t) << D3DVSD_TOKENTYPESHIFT)
#define D3DVSD_STREAM(n) (D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_STREAM) | (n))
#define D3DVSD_STREAM_TESS() (D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_STREAM) | (1u << D3DVSD_STREAMTESSSHIFT))
#define D3DVSD_REG(reg, type)                                                                                          \
    (D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_STREAMDATA) | ((DWORD)(type) << D3DVSD_DATATYPESHIFT) | (reg))
#define D3DVSD_SKIP(count)                                                                                             \
    (D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_STREAMDATA) | 0x10000000u | ((DWORD)(count) << D3DVSD_SKIPCOUNTSHIFT))
#define D3DVSD_CONST(address, count)                                                                                   \
    (D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_CONSTMEM) | ((DWORD)(count) << D3DVSD_CONSTCOUNTSHIFT) | (address))
#define D3DVSD_TESSNORMAL(in, out)                                                                                     \
    (D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_TESSELLATOR) | ((DWORD)(in) << D3DVSD_VERTEXREGINSHIFT) |                       \
     (0x02u << D3DVSD_DATATYPESHIFT) | (out))
#define D3DVSD_TESSUV(reg)                                                                                             \
    (D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_TESSELLATOR) | 0x10000000u | (0x01u << D3DVSD_DATATYPESHIFT) | (reg))
#define D3DVSD_END() 0xFFFFFFFF
#define D3DVSD_NOP() 0x00000000
#define D3DVSDT_FLOAT1 0x00
#define D3DVSDT_FLOAT2 0x01
#define D3DVSDT_FLOAT3 0x02
#define D3DVSDT_FLOAT4 0x03
#define D3DVSDT_D3DCOLOR 0x04
#define D3DVSDT_UBYTE4 0x05
#define D3DVSDT_SHORT2 0x06
#define D3DVSDT_SHORT4 0x07
#define D3DVSDE_POSITION 0
#define D3DVSDE_BLENDWEIGHT 1
#define D3DVSDE_BLENDINDICES 2
#define D3DVSDE_NORMAL 3
#define D3DVSDE_PSIZE 4
#define D3DVSDE_DIFFUSE 5
#define D3DVSDE_SPECULAR 6
#define D3DVSDE_TEXCOORD0 7
#define D3DVSDE_TEXCOORD1 8
#define D3DVSDE_TEXCOORD2 9
#define D3DVSDE_TEXCOORD3 10
#define D3DVSDE_TEXCOORD4 11
#define D3DVSDE_TEXCOORD5 12
#define D3DVSDE_TEXCOORD6 13
#define D3DVSDE_TEXCOORD7 14
#define D3DVSDE_POSITION2 15
#define D3DVSDE_NORMAL2 16

/* ------------------------------------------------------------------ types of its own */

typedef struct MIME_D3D8_NAME(_D3DPRESENT_PARAMETERS_) {
    UINT BackBufferWidth, BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    DWORD SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT FullScreen_PresentationInterval;
} MIME_D3D8_NAME(D3DPRESENT_PARAMETERS);

typedef struct MIME_D3D8_NAME(_D3DSURFACE_DESC) {
    D3DFORMAT Format;
    D3DRESOURCETYPE Type;
    DWORD Usage;
    D3DPOOL Pool;
    UINT Size;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    UINT Width, Height;
} MIME_D3D8_NAME(D3DSURFACE_DESC);

typedef struct MIME_D3D8_NAME(_D3DVOLUME_DESC) {
    D3DFORMAT Format;
    D3DRESOURCETYPE Type;
    DWORD Usage;
    D3DPOOL Pool;
    UINT Size;
    UINT Width, Height, Depth;
} MIME_D3D8_NAME(D3DVOLUME_DESC);

#define D3D8_PRESENT MIME_D3D8_NAME(D3DPRESENT_PARAMETERS)
#define D3D8_SURFACE_DESC MIME_D3D8_NAME(D3DSURFACE_DESC)
#define D3D8_VOLUME_DESC MIME_D3D8_NAME(D3DVOLUME_DESC)

/* windows packs the direct3d 8 types to 4 bytes, which matters for DriverVersion. */
#pragma pack(push, 4)
typedef struct _D3DADAPTER_IDENTIFIER8 {
    char Driver[512];
    char Description[512];
    LARGE_INTEGER DriverVersion;
    DWORD VendorId, DeviceId, SubSysId, Revision;
    GUID DeviceIdentifier;
    DWORD WHQLLevel;
} D3DADAPTER_IDENTIFIER8;
#pragma pack(pop)

/* the first 212 bytes of D3DCAPS9, whose last field direct3d 9 renamed. */
typedef struct _D3DCAPS8 {
    D3DDEVTYPE DeviceType;
    UINT AdapterOrdinal;
    DWORD Caps, Caps2, Caps3, PresentationIntervals, CursorCaps, DevCaps, PrimitiveMiscCaps, RasterCaps, ZCmpCaps;
    DWORD SrcBlendCaps, DestBlendCaps, AlphaCmpCaps, ShadeCaps, TextureCaps, TextureFilterCaps;
    DWORD CubeTextureFilterCaps, VolumeTextureFilterCaps, TextureAddressCaps, VolumeTextureAddressCaps, LineCaps;
    DWORD MaxTextureWidth, MaxTextureHeight, MaxVolumeExtent, MaxTextureRepeat, MaxTextureAspectRatio, MaxAnisotropy;
    float MaxVertexW, GuardBandLeft, GuardBandTop, GuardBandRight, GuardBandBottom, ExtentsAdjust;
    DWORD StencilCaps, FVFCaps, TextureOpCaps, MaxTextureBlendStages, MaxSimultaneousTextures, VertexProcessingCaps;
    DWORD MaxActiveLights, MaxUserClipPlanes, MaxVertexBlendMatrices, MaxVertexBlendMatrixIndex;
    float MaxPointSize;
    DWORD MaxPrimitiveCount, MaxVertexIndex, MaxStreams, MaxStreamStride, VertexShaderVersion, MaxVertexShaderConst;
    DWORD PixelShaderVersion;
    float MaxPixelShaderValue;
} D3DCAPS8;

/* ------------------------------------------------------------------ interfaces */

typedef struct IDirect3D8 IDirect3D8;
typedef struct IDirect3DDevice8 IDirect3DDevice8;
typedef struct IDirect3DSwapChain8 IDirect3DSwapChain8;
typedef struct IDirect3DResource8 IDirect3DResource8;
typedef struct IDirect3DBaseTexture8 IDirect3DBaseTexture8;
typedef struct IDirect3DTexture8 IDirect3DTexture8;
typedef struct IDirect3DVolumeTexture8 IDirect3DVolumeTexture8;
typedef struct IDirect3DCubeTexture8 IDirect3DCubeTexture8;
typedef struct IDirect3DVertexBuffer8 IDirect3DVertexBuffer8;
typedef struct IDirect3DIndexBuffer8 IDirect3DIndexBuffer8;
typedef struct IDirect3DSurface8 IDirect3DSurface8;
typedef struct IDirect3DVolume8 IDirect3DVolume8;

#define IDirect3D8_METHODS(T)                                                                                          \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *RegisterSoftwareDevice)(T * This, void *initialize);                                    \
    UINT(STDMETHODCALLTYPE *GetAdapterCount)(T * This);                                                                \
    HRESULT(STDMETHODCALLTYPE *GetAdapterIdentifier)(T * This, UINT adapter, DWORD flags,                             \
                                                     D3DADAPTER_IDENTIFIER8 *identifier);                              \
    UINT(STDMETHODCALLTYPE *GetAdapterModeCount)(T * This, UINT adapter);                                              \
    HRESULT(STDMETHODCALLTYPE *EnumAdapterModes)(T * This, UINT adapter, UINT mode, D3DDISPLAYMODE *out);              \
    HRESULT(STDMETHODCALLTYPE *GetAdapterDisplayMode)(T * This, UINT adapter, D3DDISPLAYMODE *mode);                   \
    HRESULT(STDMETHODCALLTYPE *CheckDeviceType)(T * This, UINT adapter, D3DDEVTYPE type, D3DFORMAT display,            \
                                                D3DFORMAT back_buffer, BOOL windowed);                                 \
    HRESULT(STDMETHODCALLTYPE *CheckDeviceFormat)(T * This, UINT adapter, D3DDEVTYPE type, D3DFORMAT display,          \
                                                  DWORD usage, D3DRESOURCETYPE resource, D3DFORMAT format);            \
    HRESULT(STDMETHODCALLTYPE *CheckDeviceMultiSampleType)(T * This, UINT adapter, D3DDEVTYPE type,                   \
                                                           D3DFORMAT format, BOOL windowed,                            \
                                                           D3DMULTISAMPLE_TYPE samples);                               \
    HRESULT(STDMETHODCALLTYPE *CheckDepthStencilMatch)(T * This, UINT adapter, D3DDEVTYPE type, D3DFORMAT display,     \
                                                       D3DFORMAT target, D3DFORMAT depth);                             \
    HRESULT(STDMETHODCALLTYPE *GetDeviceCaps)(T * This, UINT adapter, D3DDEVTYPE type, D3DCAPS8 *caps);               \
    HMONITOR(STDMETHODCALLTYPE *GetAdapterMonitor)(T * This, UINT adapter);                                            \
    HRESULT(STDMETHODCALLTYPE *CreateDevice)(T * This, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,         \
                                             D3D8_PRESENT *parameters, IDirect3DDevice8 **device);

#define IDirect3DDevice8_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *TestCooperativeLevel)(T * This);                                                        \
    UINT(STDMETHODCALLTYPE *GetAvailableTextureMem)(T * This);                                                         \
    HRESULT(STDMETHODCALLTYPE *ResourceManagerDiscardBytes)(T * This, DWORD bytes);                                    \
    HRESULT(STDMETHODCALLTYPE *GetDirect3D)(T * This, IDirect3D8 **d3d);                                               \
    HRESULT(STDMETHODCALLTYPE *GetDeviceCaps)(T * This, D3DCAPS8 *caps);                                               \
    HRESULT(STDMETHODCALLTYPE *GetDisplayMode)(T * This, D3DDISPLAYMODE *mode);                                        \
    HRESULT(STDMETHODCALLTYPE *GetCreationParameters)(T * This, D3DDEVICE_CREATION_PARAMETERS *parameters);            \
    HRESULT(STDMETHODCALLTYPE *SetCursorProperties)(T * This, UINT x, UINT y, IDirect3DSurface8 *bitmap);             \
    void(STDMETHODCALLTYPE *SetCursorPosition)(T * This, int x, int y, DWORD flags);                                   \
    BOOL(STDMETHODCALLTYPE *ShowCursor)(T * This, BOOL show);                                                          \
    HRESULT(STDMETHODCALLTYPE *CreateAdditionalSwapChain)(T * This, D3D8_PRESENT *parameters,                          \
                                                          IDirect3DSwapChain8 **swapchain);                            \
    HRESULT(STDMETHODCALLTYPE *Reset)(T * This, D3D8_PRESENT *parameters);                                             \
    HRESULT(STDMETHODCALLTYPE *Present)(T * This, const RECT *source, const RECT *dest, HWND window,                   \
                                        const RGNDATA *dirty);                                                         \
    HRESULT(STDMETHODCALLTYPE *GetBackBuffer)(T * This, UINT index, D3DBACKBUFFER_TYPE type,                           \
                                              IDirect3DSurface8 **buffer);                                             \
    HRESULT(STDMETHODCALLTYPE *GetRasterStatus)(T * This, D3DRASTER_STATUS *status);                                   \
    void(STDMETHODCALLTYPE *SetGammaRamp)(T * This, DWORD flags, const D3DGAMMARAMP *ramp);                            \
    void(STDMETHODCALLTYPE *GetGammaRamp)(T * This, D3DGAMMARAMP *ramp);                                               \
    HRESULT(STDMETHODCALLTYPE *CreateTexture)(T * This, UINT width, UINT height, UINT levels, DWORD usage,             \
                                              D3DFORMAT format, D3DPOOL pool, IDirect3DTexture8 **texture);            \
    HRESULT(STDMETHODCALLTYPE *CreateVolumeTexture)(T * This, UINT width, UINT height, UINT depth, UINT levels,        \
                                                    DWORD usage, D3DFORMAT format, D3DPOOL pool,                       \
                                                    IDirect3DVolumeTexture8 **texture);                                \
    HRESULT(STDMETHODCALLTYPE *CreateCubeTexture)(T * This, UINT edge, UINT levels, DWORD usage, D3DFORMAT format,     \
                                                  D3DPOOL pool, IDirect3DCubeTexture8 **texture);                      \
    HRESULT(STDMETHODCALLTYPE *CreateVertexBuffer)(T * This, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,        \
                                                   IDirect3DVertexBuffer8 **buffer);                                   \
    HRESULT(STDMETHODCALLTYPE *CreateIndexBuffer)(T * This, UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,  \
                                                  IDirect3DIndexBuffer8 **buffer);                                     \
    HRESULT(STDMETHODCALLTYPE *CreateRenderTarget)(T * This, UINT width, UINT height, D3DFORMAT format,               \
                                                   D3DMULTISAMPLE_TYPE samples, BOOL lockable,                         \
                                                   IDirect3DSurface8 **surface);                                       \
    HRESULT(STDMETHODCALLTYPE *CreateDepthStencilSurface)(T * This, UINT width, UINT height, D3DFORMAT format,        \
                                                          D3DMULTISAMPLE_TYPE samples, IDirect3DSurface8 **surface);   \
    HRESULT(STDMETHODCALLTYPE *CreateImageSurface)(T * This, UINT width, UINT height, D3DFORMAT format,               \
                                                   IDirect3DSurface8 **surface);                                       \
    HRESULT(STDMETHODCALLTYPE *CopyRects)(T * This, IDirect3DSurface8 *source, const RECT *rects, UINT count,         \
                                          IDirect3DSurface8 *dest, const POINT *points);                               \
    HRESULT(STDMETHODCALLTYPE *UpdateTexture)(T * This, IDirect3DBaseTexture8 *source, IDirect3DBaseTexture8 *dest);  \
    HRESULT(STDMETHODCALLTYPE *GetFrontBuffer)(T * This, IDirect3DSurface8 *dest);                                     \
    HRESULT(STDMETHODCALLTYPE *SetRenderTarget)(T * This, IDirect3DSurface8 *target, IDirect3DSurface8 *depth);       \
    HRESULT(STDMETHODCALLTYPE *GetRenderTarget)(T * This, IDirect3DSurface8 **target);                                 \
    HRESULT(STDMETHODCALLTYPE *GetDepthStencilSurface)(T * This, IDirect3DSurface8 **depth);                           \
    HRESULT(STDMETHODCALLTYPE *BeginScene)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *EndScene)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *Clear)(T * This, DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color,        \
                                      float z, DWORD stencil);                                                         \
    HRESULT(STDMETHODCALLTYPE *SetTransform)(T * This, D3DTRANSFORMSTATETYPE type, const D3DMATRIX *matrix);           \
    HRESULT(STDMETHODCALLTYPE *GetTransform)(T * This, D3DTRANSFORMSTATETYPE type, D3DMATRIX *matrix);                 \
    HRESULT(STDMETHODCALLTYPE *MultiplyTransform)(T * This, D3DTRANSFORMSTATETYPE type, const D3DMATRIX *matrix);      \
    HRESULT(STDMETHODCALLTYPE *SetViewport)(T * This, const D3DVIEWPORT8 *viewport);                                   \
    HRESULT(STDMETHODCALLTYPE *GetViewport)(T * This, D3DVIEWPORT8 *viewport);                                         \
    HRESULT(STDMETHODCALLTYPE *SetMaterial)(T * This, const D3DMATERIAL8 *material);                                   \
    HRESULT(STDMETHODCALLTYPE *GetMaterial)(T * This, D3DMATERIAL8 *material);                                         \
    HRESULT(STDMETHODCALLTYPE *SetLight)(T * This, DWORD index, const D3DLIGHT8 *light);                               \
    HRESULT(STDMETHODCALLTYPE *GetLight)(T * This, DWORD index, D3DLIGHT8 *light);                                     \
    HRESULT(STDMETHODCALLTYPE *LightEnable)(T * This, DWORD index, BOOL enable);                                       \
    HRESULT(STDMETHODCALLTYPE *GetLightEnable)(T * This, DWORD index, BOOL *enable);                                   \
    HRESULT(STDMETHODCALLTYPE *SetClipPlane)(T * This, DWORD index, const float *plane);                               \
    HRESULT(STDMETHODCALLTYPE *GetClipPlane)(T * This, DWORD index, float *plane);                                     \
    HRESULT(STDMETHODCALLTYPE *SetRenderState)(T * This, D3DRENDERSTATETYPE state, DWORD value);                       \
    HRESULT(STDMETHODCALLTYPE *GetRenderState)(T * This, D3DRENDERSTATETYPE state, DWORD *value);                      \
    HRESULT(STDMETHODCALLTYPE *BeginStateBlock)(T * This);                                                             \
    HRESULT(STDMETHODCALLTYPE *EndStateBlock)(T * This, DWORD *token);                                                 \
    HRESULT(STDMETHODCALLTYPE *ApplyStateBlock)(T * This, DWORD token);                                                \
    HRESULT(STDMETHODCALLTYPE *CaptureStateBlock)(T * This, DWORD token);                                              \
    HRESULT(STDMETHODCALLTYPE *DeleteStateBlock)(T * This, DWORD token);                                               \
    HRESULT(STDMETHODCALLTYPE *CreateStateBlock)(T * This, D3DSTATEBLOCKTYPE type, DWORD *token);                      \
    HRESULT(STDMETHODCALLTYPE *SetClipStatus)(T * This, const D3DCLIPSTATUS8 *status);                                 \
    HRESULT(STDMETHODCALLTYPE *GetClipStatus)(T * This, D3DCLIPSTATUS8 *status);                                       \
    HRESULT(STDMETHODCALLTYPE *GetTexture)(T * This, DWORD stage, IDirect3DBaseTexture8 **texture);                    \
    HRESULT(STDMETHODCALLTYPE *SetTexture)(T * This, DWORD stage, IDirect3DBaseTexture8 *texture);                     \
    HRESULT(STDMETHODCALLTYPE *GetTextureStageState)(T * This, DWORD stage, D3DTEXTURESTAGESTATETYPE type,             \
                                                     DWORD *value);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetTextureStageState)(T * This, DWORD stage, D3DTEXTURESTAGESTATETYPE type,             \
                                                     DWORD value);                                                     \
    HRESULT(STDMETHODCALLTYPE *ValidateDevice)(T * This, DWORD *passes);                                               \
    HRESULT(STDMETHODCALLTYPE *GetInfo)(T * This, DWORD id, void *info, DWORD size);                                   \
    HRESULT(STDMETHODCALLTYPE *SetPaletteEntries)(T * This, UINT palette, const PALETTEENTRY *entries);                \
    HRESULT(STDMETHODCALLTYPE *GetPaletteEntries)(T * This, UINT palette, PALETTEENTRY *entries);                      \
    HRESULT(STDMETHODCALLTYPE *SetCurrentTexturePalette)(T * This, UINT palette);                                      \
    HRESULT(STDMETHODCALLTYPE *GetCurrentTexturePalette)(T * This, UINT *palette);                                     \
    HRESULT(STDMETHODCALLTYPE *DrawPrimitive)(T * This, D3DPRIMITIVETYPE type, UINT start, UINT count);                \
    HRESULT(STDMETHODCALLTYPE *DrawIndexedPrimitive)(T * This, D3DPRIMITIVETYPE type, UINT min_index,                  \
                                                     UINT vertex_count, UINT start_index, UINT count);                 \
    HRESULT(STDMETHODCALLTYPE *DrawPrimitiveUP)(T * This, D3DPRIMITIVETYPE type, UINT count, const void *data,         \
                                                UINT stride);                                                          \
    HRESULT(STDMETHODCALLTYPE *DrawIndexedPrimitiveUP)(T * This, D3DPRIMITIVETYPE type, UINT min_index,                \
                                                       UINT vertex_count, UINT count, const void *indices,             \
                                                       D3DFORMAT index_format, const void *data, UINT stride);         \
    HRESULT(STDMETHODCALLTYPE *ProcessVertices)(T * This, UINT src_start, UINT dst_index, UINT count,                  \
                                                IDirect3DVertexBuffer8 *dest, DWORD flags);                            \
    HRESULT(STDMETHODCALLTYPE *CreateVertexShader)(T * This, const DWORD *declaration, const DWORD *function,          \
                                                   DWORD *handle, DWORD usage);                                        \
    HRESULT(STDMETHODCALLTYPE *SetVertexShader)(T * This, DWORD handle);                                               \
    HRESULT(STDMETHODCALLTYPE *GetVertexShader)(T * This, DWORD *handle);                                              \
    HRESULT(STDMETHODCALLTYPE *DeleteVertexShader)(T * This, DWORD handle);                                            \
    HRESULT(STDMETHODCALLTYPE *SetVertexShaderConstant)(T * This, DWORD reg, const void *data, DWORD count);           \
    HRESULT(STDMETHODCALLTYPE *GetVertexShaderConstant)(T * This, DWORD reg, void *data, DWORD count);                 \
    HRESULT(STDMETHODCALLTYPE *GetVertexShaderDeclaration)(T * This, DWORD handle, void *data, DWORD *size);           \
    HRESULT(STDMETHODCALLTYPE *GetVertexShaderFunction)(T * This, DWORD handle, void *data, DWORD *size);              \
    HRESULT(STDMETHODCALLTYPE *SetStreamSource)(T * This, UINT stream, IDirect3DVertexBuffer8 *buffer, UINT stride);   \
    HRESULT(STDMETHODCALLTYPE *GetStreamSource)(T * This, UINT stream, IDirect3DVertexBuffer8 **buffer,                \
                                                UINT *stride);                                                         \
    HRESULT(STDMETHODCALLTYPE *SetIndices)(T * This, IDirect3DIndexBuffer8 *indices, UINT base);                       \
    HRESULT(STDMETHODCALLTYPE *GetIndices)(T * This, IDirect3DIndexBuffer8 **indices, UINT *base);                     \
    HRESULT(STDMETHODCALLTYPE *CreatePixelShader)(T * This, const DWORD *function, DWORD *handle);                     \
    HRESULT(STDMETHODCALLTYPE *SetPixelShader)(T * This, DWORD handle);                                                \
    HRESULT(STDMETHODCALLTYPE *GetPixelShader)(T * This, DWORD *handle);                                               \
    HRESULT(STDMETHODCALLTYPE *DeletePixelShader)(T * This, DWORD handle);                                             \
    HRESULT(STDMETHODCALLTYPE *SetPixelShaderConstant)(T * This, DWORD reg, const void *data, DWORD count);            \
    HRESULT(STDMETHODCALLTYPE *GetPixelShaderConstant)(T * This, DWORD reg, void *data, DWORD count);                  \
    HRESULT(STDMETHODCALLTYPE *GetPixelShaderFunction)(T * This, DWORD handle, void *data, DWORD *size);               \
    HRESULT(STDMETHODCALLTYPE *DrawRectPatch)(T * This, UINT handle, const float *segments,                            \
                                              const D3DRECTPATCH_INFO *info);                                          \
    HRESULT(STDMETHODCALLTYPE *DrawTriPatch)(T * This, UINT handle, const float *segments,                             \
                                             const D3DTRIPATCH_INFO *info);                                            \
    HRESULT(STDMETHODCALLTYPE *DeletePatch)(T * This, UINT handle);

#define IDirect3DSwapChain8_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Present)(T * This, const RECT *source, const RECT *dest, HWND window,                   \
                                        const RGNDATA *dirty);                                                         \
    HRESULT(STDMETHODCALLTYPE *GetBackBuffer)(T * This, UINT index, D3DBACKBUFFER_TYPE type,                           \
                                              IDirect3DSurface8 **buffer);

#define IDirect3DResource8_COMMON(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDevice)(T * This, IDirect3DDevice8 **device);                                        \
    HRESULT(STDMETHODCALLTYPE *SetPrivateData)(T * This, REFGUID guid, const void *data, DWORD size, DWORD flags);     \
    HRESULT(STDMETHODCALLTYPE *GetPrivateData)(T * This, REFGUID guid, void *data, DWORD *size);                       \
    HRESULT(STDMETHODCALLTYPE *FreePrivateData)(T * This, REFGUID guid);

#define IDirect3DResource8_METHODS(T)                                                                                  \
    IDirect3DResource8_COMMON(T)                                                                                       \
    DWORD(STDMETHODCALLTYPE *SetPriority)(T * This, DWORD priority);                                                   \
    DWORD(STDMETHODCALLTYPE *GetPriority)(T * This);                                                                   \
    void(STDMETHODCALLTYPE *PreLoad)(T * This);                                                                        \
    D3DRESOURCETYPE(STDMETHODCALLTYPE *GetType)(T * This);

#define IDirect3DBaseTexture8_METHODS(T)                                                                               \
    IDirect3DResource8_METHODS(T)                                                                                      \
    DWORD(STDMETHODCALLTYPE *SetLOD)(T * This, DWORD lod);                                                             \
    DWORD(STDMETHODCALLTYPE *GetLOD)(T * This);                                                                        \
    DWORD(STDMETHODCALLTYPE *GetLevelCount)(T * This);

#define IDirect3DTexture8_METHODS(T)                                                                                   \
    IDirect3DBaseTexture8_METHODS(T)                                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetLevelDesc)(T * This, UINT level, D3D8_SURFACE_DESC *desc);                           \
    HRESULT(STDMETHODCALLTYPE *GetSurfaceLevel)(T * This, UINT level, IDirect3DSurface8 **surface);                    \
    HRESULT(STDMETHODCALLTYPE *LockRect)(T * This, UINT level, D3DLOCKED_RECT *locked, const RECT *rect,               \
                                         DWORD flags);                                                                 \
    HRESULT(STDMETHODCALLTYPE *UnlockRect)(T * This, UINT level);                                                      \
    HRESULT(STDMETHODCALLTYPE *AddDirtyRect)(T * This, const RECT *rect);

#define IDirect3DCubeTexture8_METHODS(T)                                                                               \
    IDirect3DBaseTexture8_METHODS(T)                                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetLevelDesc)(T * This, UINT level, D3D8_SURFACE_DESC *desc);                           \
    HRESULT(STDMETHODCALLTYPE *GetCubeMapSurface)(T * This, D3DCUBEMAP_FACES face, UINT level,                         \
                                                  IDirect3DSurface8 **surface);                                        \
    HRESULT(STDMETHODCALLTYPE *LockRect)(T * This, D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *locked,          \
                                         const RECT *rect, DWORD flags);                                               \
    HRESULT(STDMETHODCALLTYPE *UnlockRect)(T * This, D3DCUBEMAP_FACES face, UINT level);                               \
    HRESULT(STDMETHODCALLTYPE *AddDirtyRect)(T * This, D3DCUBEMAP_FACES face, const RECT *rect);

#define IDirect3DVolumeTexture8_METHODS(T)                                                                             \
    IDirect3DBaseTexture8_METHODS(T)                                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetLevelDesc)(T * This, UINT level, D3D8_VOLUME_DESC *desc);                            \
    HRESULT(STDMETHODCALLTYPE *GetVolumeLevel)(T * This, UINT level, IDirect3DVolume8 **volume);                       \
    HRESULT(STDMETHODCALLTYPE *LockBox)(T * This, UINT level, D3DLOCKED_BOX *locked, const D3DBOX *box,                \
                                        DWORD flags);                                                                  \
    HRESULT(STDMETHODCALLTYPE *UnlockBox)(T * This, UINT level);                                                       \
    HRESULT(STDMETHODCALLTYPE *AddDirtyBox)(T * This, const D3DBOX *box);

#define IDirect3DVertexBuffer8_METHODS(T)                                                                              \
    IDirect3DResource8_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *Lock)(T * This, UINT offset, UINT size, BYTE **data, DWORD flags);                      \
    HRESULT(STDMETHODCALLTYPE *Unlock)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3DVERTEXBUFFER_DESC *desc);

#define IDirect3DIndexBuffer8_METHODS(T)                                                                               \
    IDirect3DResource8_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *Lock)(T * This, UINT offset, UINT size, BYTE **data, DWORD flags);                      \
    HRESULT(STDMETHODCALLTYPE *Unlock)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3DINDEXBUFFER_DESC *desc);

#define IDirect3DSurface8_METHODS(T)                                                                                   \
    IDirect3DResource8_COMMON(T)                                                                                       \
    HRESULT(STDMETHODCALLTYPE *GetContainer)(T * This, REFIID riid, void **container);                                 \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3D8_SURFACE_DESC *desc);                                            \
    HRESULT(STDMETHODCALLTYPE *LockRect)(T * This, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags);             \
    HRESULT(STDMETHODCALLTYPE *UnlockRect)(T * This);

#define IDirect3DVolume8_METHODS(T)                                                                                    \
    IDirect3DResource8_COMMON(T)                                                                                       \
    HRESULT(STDMETHODCALLTYPE *GetContainer)(T * This, REFIID riid, void **container);                                 \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3D8_VOLUME_DESC *desc);                                             \
    HRESULT(STDMETHODCALLTYPE *LockBox)(T * This, D3DLOCKED_BOX *locked, const D3DBOX *box, DWORD flags);              \
    HRESULT(STDMETHODCALLTYPE *UnlockBox)(T * This);

MIME_COM_INTERFACE(IDirect3D8)
MIME_COM_INTERFACE(IDirect3DDevice8)
MIME_COM_INTERFACE(IDirect3DSwapChain8)
MIME_COM_INTERFACE(IDirect3DResource8)
MIME_COM_INTERFACE(IDirect3DBaseTexture8)
MIME_COM_INTERFACE(IDirect3DTexture8)
MIME_COM_INTERFACE(IDirect3DCubeTexture8)
MIME_COM_INTERFACE(IDirect3DVolumeTexture8)
MIME_COM_INTERFACE(IDirect3DVertexBuffer8)
MIME_COM_INTERFACE(IDirect3DIndexBuffer8)
MIME_COM_INTERFACE(IDirect3DSurface8)
MIME_COM_INTERFACE(IDirect3DVolume8)

MIME_DEFINE_GUID(IID_IDirect3D8, 0x1DD9E8DA, 0x1C77, 0x4D40, 0xB0, 0xCF, 0x98, 0xFE, 0xFD, 0xFF, 0x95, 0x12);
MIME_DEFINE_GUID(IID_IDirect3DDevice8, 0x7385E5DF, 0x8FE8, 0x41D5, 0x86, 0xB6, 0xD7, 0xB4, 0x85, 0x47, 0xB6, 0xCF);
MIME_DEFINE_GUID(IID_IDirect3DResource8, 0x1B36BB7B, 0x09B7, 0x410A, 0xB4, 0x45, 0x7D, 0x14, 0x30, 0xD7, 0xB3, 0x3F);
MIME_DEFINE_GUID(IID_IDirect3DBaseTexture8, 0xB4211CFA, 0x51B9, 0x4A9F, 0xAB, 0x78, 0xDB, 0x99, 0xB2, 0xBB, 0x67, 0x8E);
MIME_DEFINE_GUID(IID_IDirect3DTexture8, 0xE4CDD575, 0x2866, 0x4F01, 0xB1, 0x2E, 0x7E, 0xEC, 0xE1, 0xEC, 0x93, 0x58);
MIME_DEFINE_GUID(IID_IDirect3DCubeTexture8, 0x3EE5B968, 0x2ACA, 0x4C34, 0x8B, 0xB5, 0x7E, 0x0C, 0x3D, 0x19, 0xB7, 0x50);
MIME_DEFINE_GUID(IID_IDirect3DVolumeTexture8, 0x4B8AAAFA, 0x140F, 0x42BA, 0x91, 0x31, 0x59, 0x7E, 0xAF, 0xAA, 0x2E,
                 0xAD);
MIME_DEFINE_GUID(IID_IDirect3DVertexBuffer8, 0x8AEEEAC7, 0x05F9, 0x44D4, 0xB5, 0x91, 0x00, 0x0B, 0x0D, 0xF1, 0xCB,
                 0x95);
MIME_DEFINE_GUID(IID_IDirect3DIndexBuffer8, 0x0E689C9A, 0x053D, 0x44A0, 0x9D, 0x92, 0xDB, 0x0E, 0x3D, 0x75, 0x0F, 0x86);
MIME_DEFINE_GUID(IID_IDirect3DSurface8, 0xB96EEBCA, 0xB326, 0x4EA5, 0x88, 0x2F, 0x2F, 0xF5, 0xBA, 0xE0, 0x21, 0xDD);
MIME_DEFINE_GUID(IID_IDirect3DVolume8, 0xBD7349F5, 0x14F1, 0x42E4, 0x9C, 0x79, 0x97, 0x23, 0x80, 0xDB, 0x40, 0xC0);
MIME_DEFINE_GUID(IID_IDirect3DSwapChain8, 0x928C088B, 0x76B9, 0x4C6B, 0xA5, 0x36, 0xA5, 0x90, 0x85, 0x38, 0x76, 0xCD);

#ifndef MIME_BUILD_D3D8
__declspec(dllimport) IDirect3D8 *WINAPI Direct3DCreate8(UINT sdk);
#endif

#endif
