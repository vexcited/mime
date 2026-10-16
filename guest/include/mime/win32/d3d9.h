/*
 * direct3d 9: the interfaces of objects and devices. their method order
 * is windows', as programs call them through the vtables.
 */
#ifndef MIME_WIN32_D3D9_H
#define MIME_WIN32_D3D9_H

#include <mime/win32/dxgi.h>
#include <mime/win32/d3d9types.h>
#include <mime/win32/d3d9caps.h>

#define D3D_SDK_VERSION 32
#define D3D9b_SDK_VERSION 31

#define D3DADAPTER_DEFAULT 0
#define D3DENUM_WHQL_LEVEL 0x00000002
#define D3DENUM_NO_DRIVERVERSION 0x00000004
#define D3DPRESENT_BACK_BUFFERS_MAX 3
#define D3DPRESENT_BACK_BUFFERS_MAX_EX 30
#define D3DSGR_NO_CALIBRATION 0x00000000
#define D3DPV_DONOTCOPYDATA 0x00000001
#define D3DSGR_CALIBRATE 0x00000001
#define D3DCURSOR_IMMEDIATE_UPDATE 0x00000001

#define D3DPRESENT_DONOTWAIT 0x00000001
#define D3DPRESENT_LINEAR_CONTENT 0x00000002
#define D3DPRESENT_DONOTFLIP 0x00000004
#define D3DPRESENT_FLIPRESTART 0x00000008
#define D3DPRESENT_VIDEO_RESTRICT_TO_MONITOR 0x00000010
#define D3DPRESENT_UPDATEOVERLAYONLY 0x00000020
#define D3DPRESENT_HIDEOVERLAY 0x00000040
#define D3DPRESENT_UPDATECOLORKEY 0x00000080
#define D3DPRESENT_FORCEIMMEDIATE 0x00000100

#define D3DCREATE_FPU_PRESERVE 0x00000002
#define D3DCREATE_MULTITHREADED 0x00000004
#define D3DCREATE_PUREDEVICE 0x00000010
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x00000020
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040
#define D3DCREATE_MIXED_VERTEXPROCESSING 0x00000080
#define D3DCREATE_DISABLE_DRIVER_MANAGEMENT 0x00000100
#define D3DCREATE_ADAPTERGROUP_DEVICE 0x00000200
#define D3DCREATE_DISABLE_DRIVER_MANAGEMENT_EX 0x00000400
#define D3DCREATE_NOWINDOWCHANGES 0x00000800
#define D3DCREATE_DISABLE_PSGP_THREADING 0x00002000
#define D3DCREATE_ENABLE_PRESENTSTATS 0x00004000
#define D3DCREATE_DISABLE_PRINTSCREEN 0x00008000
#define D3DCREATE_SCREENSAVER 0x10000000

#define _FACD3D 0x876
#define MAKE_D3DHRESULT(code) ((HRESULT)(0x88760000 | (code)))
#define MAKE_D3DSTATUS(code) ((HRESULT)(0x08760000 | (code)))

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
#define D3DERR_WASSTILLDRAWING MAKE_D3DHRESULT(540)
#define D3DOK_NOAUTOGEN MAKE_D3DSTATUS(2159)
#define D3DERR_DEVICEREMOVED MAKE_D3DHRESULT(2160)
#define S_NOT_RESIDENT MAKE_D3DSTATUS(2165)
#define S_RESIDENT_IN_SHARED_MEMORY MAKE_D3DSTATUS(2166)
#define S_PRESENT_MODE_CHANGED MAKE_D3DSTATUS(2167)
#define S_PRESENT_OCCLUDED MAKE_D3DSTATUS(2168)
#define D3DERR_DEVICEHUNG MAKE_D3DHRESULT(2164)
#define D3DERR_UNSUPPORTEDOVERLAY MAKE_D3DHRESULT(2171)
#define D3DERR_UNSUPPORTEDOVERLAYFORMAT MAKE_D3DHRESULT(2172)
#define D3DERR_CANNOTPROTECTCONTENT MAKE_D3DHRESULT(2173)
#define D3DERR_UNSUPPORTEDCRYPTO MAKE_D3DHRESULT(2174)
#define D3DERR_PRESENT_STATISTICS_DISJOINT MAKE_D3DHRESULT(2180)

typedef struct IDirect3D9 IDirect3D9;
typedef struct IDirect3D9Ex IDirect3D9Ex;
typedef struct IDirect3DDevice9 IDirect3DDevice9;
typedef struct IDirect3DDevice9Ex IDirect3DDevice9Ex;
typedef struct IDirect3DStateBlock9 IDirect3DStateBlock9;
typedef struct IDirect3DSwapChain9 IDirect3DSwapChain9;
typedef struct IDirect3DSwapChain9Ex IDirect3DSwapChain9Ex;
typedef struct IDirect3DResource9 IDirect3DResource9;
typedef struct IDirect3DVertexDeclaration9 IDirect3DVertexDeclaration9;
typedef struct IDirect3DVertexShader9 IDirect3DVertexShader9;
typedef struct IDirect3DPixelShader9 IDirect3DPixelShader9;
typedef struct IDirect3DBaseTexture9 IDirect3DBaseTexture9;
typedef struct IDirect3DTexture9 IDirect3DTexture9;
typedef struct IDirect3DVolumeTexture9 IDirect3DVolumeTexture9;
typedef struct IDirect3DCubeTexture9 IDirect3DCubeTexture9;
typedef struct IDirect3DVertexBuffer9 IDirect3DVertexBuffer9;
typedef struct IDirect3DIndexBuffer9 IDirect3DIndexBuffer9;
typedef struct IDirect3DSurface9 IDirect3DSurface9;
typedef struct IDirect3DVolume9 IDirect3DVolume9;
typedef struct IDirect3DQuery9 IDirect3DQuery9;

#define IDirect3D9_METHODS(T)                                                                                          \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *RegisterSoftwareDevice)(T * This, void *initialize);                                    \
    UINT(STDMETHODCALLTYPE *GetAdapterCount)(T * This);                                                                \
    HRESULT(STDMETHODCALLTYPE *GetAdapterIdentifier)(T * This, UINT adapter, DWORD flags,                             \
                                                     D3DADAPTER_IDENTIFIER9 *identifier);                              \
    UINT(STDMETHODCALLTYPE *GetAdapterModeCount)(T * This, UINT adapter, D3DFORMAT format);                            \
    HRESULT(STDMETHODCALLTYPE *EnumAdapterModes)(T * This, UINT adapter, D3DFORMAT format, UINT mode,                  \
                                                 D3DDISPLAYMODE *out);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetAdapterDisplayMode)(T * This, UINT adapter, D3DDISPLAYMODE *mode);                   \
    HRESULT(STDMETHODCALLTYPE *CheckDeviceType)(T * This, UINT adapter, D3DDEVTYPE type, D3DFORMAT display,            \
                                                D3DFORMAT back_buffer, BOOL windowed);                                 \
    HRESULT(STDMETHODCALLTYPE *CheckDeviceFormat)(T * This, UINT adapter, D3DDEVTYPE type, D3DFORMAT display,          \
                                                  DWORD usage, D3DRESOURCETYPE resource, D3DFORMAT format);            \
    HRESULT(STDMETHODCALLTYPE *CheckDeviceMultiSampleType)(T * This, UINT adapter, D3DDEVTYPE type,                   \
                                                           D3DFORMAT format, BOOL windowed,                            \
                                                           D3DMULTISAMPLE_TYPE samples, DWORD *qualities);             \
    HRESULT(STDMETHODCALLTYPE *CheckDepthStencilMatch)(T * This, UINT adapter, D3DDEVTYPE type, D3DFORMAT display,     \
                                                       D3DFORMAT target, D3DFORMAT depth);                             \
    HRESULT(STDMETHODCALLTYPE *CheckDeviceFormatConversion)(T * This, UINT adapter, D3DDEVTYPE type,                  \
                                                            D3DFORMAT source, D3DFORMAT target);                       \
    HRESULT(STDMETHODCALLTYPE *GetDeviceCaps)(T * This, UINT adapter, D3DDEVTYPE type, D3DCAPS9 *caps);               \
    HMONITOR(STDMETHODCALLTYPE *GetAdapterMonitor)(T * This, UINT adapter);                                            \
    HRESULT(STDMETHODCALLTYPE *CreateDevice)(T * This, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,         \
                                             D3DPRESENT_PARAMETERS *parameters, IDirect3DDevice9 **device);

#define IDirect3D9Ex_METHODS(T)                                                                                        \
    IDirect3D9_METHODS(T)                                                                                              \
    UINT(STDMETHODCALLTYPE *GetAdapterModeCountEx)(T * This, UINT adapter, const D3DDISPLAYMODEFILTER *filter);       \
    HRESULT(STDMETHODCALLTYPE *EnumAdapterModesEx)(T * This, UINT adapter, const D3DDISPLAYMODEFILTER *filter,         \
                                                   UINT mode, D3DDISPLAYMODEEX *out);                                  \
    HRESULT(STDMETHODCALLTYPE *GetAdapterDisplayModeEx)(T * This, UINT adapter, D3DDISPLAYMODEEX *mode,               \
                                                        D3DDISPLAYROTATION *rotation);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateDeviceEx)(T * This, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,       \
                                               D3DPRESENT_PARAMETERS *parameters, D3DDISPLAYMODEEX *mode,              \
                                               IDirect3DDevice9Ex **device);                                           \
    HRESULT(STDMETHODCALLTYPE *GetAdapterLUID)(T * This, UINT adapter, LUID *luid);

#define IDirect3DDevice9_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *TestCooperativeLevel)(T * This);                                                        \
    UINT(STDMETHODCALLTYPE *GetAvailableTextureMem)(T * This);                                                         \
    HRESULT(STDMETHODCALLTYPE *EvictManagedResources)(T * This);                                                       \
    HRESULT(STDMETHODCALLTYPE *GetDirect3D)(T * This, IDirect3D9 **d3d);                                               \
    HRESULT(STDMETHODCALLTYPE *GetDeviceCaps)(T * This, D3DCAPS9 *caps);                                               \
    HRESULT(STDMETHODCALLTYPE *GetDisplayMode)(T * This, UINT swapchain, D3DDISPLAYMODE *mode);                        \
    HRESULT(STDMETHODCALLTYPE *GetCreationParameters)(T * This, D3DDEVICE_CREATION_PARAMETERS *parameters);           \
    HRESULT(STDMETHODCALLTYPE *SetCursorProperties)(T * This, UINT x, UINT y, IDirect3DSurface9 *bitmap);              \
    void(STDMETHODCALLTYPE *SetCursorPosition)(T * This, int x, int y, DWORD flags);                                   \
    BOOL(STDMETHODCALLTYPE *ShowCursor)(T * This, BOOL show);                                                          \
    HRESULT(STDMETHODCALLTYPE *CreateAdditionalSwapChain)(T * This, D3DPRESENT_PARAMETERS *parameters,                 \
                                                          IDirect3DSwapChain9 **swapchain);                            \
    HRESULT(STDMETHODCALLTYPE *GetSwapChain)(T * This, UINT index, IDirect3DSwapChain9 **swapchain);                   \
    UINT(STDMETHODCALLTYPE *GetNumberOfSwapChains)(T * This);                                                          \
    HRESULT(STDMETHODCALLTYPE *Reset)(T * This, D3DPRESENT_PARAMETERS *parameters);                                    \
    HRESULT(STDMETHODCALLTYPE *Present)(T * This, const RECT *source, const RECT *dest, HWND window,                   \
                                        const RGNDATA *dirty);                                                         \
    HRESULT(STDMETHODCALLTYPE *GetBackBuffer)(T * This, UINT swapchain, UINT index, D3DBACKBUFFER_TYPE type,           \
                                              IDirect3DSurface9 **surface);                                            \
    HRESULT(STDMETHODCALLTYPE *GetRasterStatus)(T * This, UINT swapchain, D3DRASTER_STATUS *status);                   \
    HRESULT(STDMETHODCALLTYPE *SetDialogBoxMode)(T * This, BOOL enable);                                               \
    void(STDMETHODCALLTYPE *SetGammaRamp)(T * This, UINT swapchain, DWORD flags, const D3DGAMMARAMP *ramp);            \
    void(STDMETHODCALLTYPE *GetGammaRamp)(T * This, UINT swapchain, D3DGAMMARAMP *ramp);                               \
    HRESULT(STDMETHODCALLTYPE *CreateTexture)(T * This, UINT width, UINT height, UINT levels, DWORD usage,             \
                                              D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9 **texture,             \
                                              HANDLE *shared);                                                         \
    HRESULT(STDMETHODCALLTYPE *CreateVolumeTexture)(T * This, UINT width, UINT height, UINT depth, UINT levels,        \
                                                    DWORD usage, D3DFORMAT format, D3DPOOL pool,                       \
                                                    IDirect3DVolumeTexture9 **texture, HANDLE *shared);                \
    HRESULT(STDMETHODCALLTYPE *CreateCubeTexture)(T * This, UINT edge, UINT levels, DWORD usage, D3DFORMAT format,     \
                                                  D3DPOOL pool, IDirect3DCubeTexture9 **texture, HANDLE *shared);      \
    HRESULT(STDMETHODCALLTYPE *CreateVertexBuffer)(T * This, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,        \
                                                   IDirect3DVertexBuffer9 **buffer, HANDLE *shared);                   \
    HRESULT(STDMETHODCALLTYPE *CreateIndexBuffer)(T * This, UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,  \
                                                  IDirect3DIndexBuffer9 **buffer, HANDLE *shared);                     \
    HRESULT(STDMETHODCALLTYPE *CreateRenderTarget)(T * This, UINT width, UINT height, D3DFORMAT format,                \
                                                   D3DMULTISAMPLE_TYPE samples, DWORD quality, BOOL lockable,          \
                                                   IDirect3DSurface9 **surface, HANDLE *shared);                       \
    HRESULT(STDMETHODCALLTYPE *CreateDepthStencilSurface)(T * This, UINT width, UINT height, D3DFORMAT format,         \
                                                          D3DMULTISAMPLE_TYPE samples, DWORD quality, BOOL discard,    \
                                                          IDirect3DSurface9 **surface, HANDLE *shared);                \
    HRESULT(STDMETHODCALLTYPE *UpdateSurface)(T * This, IDirect3DSurface9 *source, const RECT *rect,                   \
                                              IDirect3DSurface9 *dest, const POINT *point);                            \
    HRESULT(STDMETHODCALLTYPE *UpdateTexture)(T * This, IDirect3DBaseTexture9 *source, IDirect3DBaseTexture9 *dest);   \
    HRESULT(STDMETHODCALLTYPE *GetRenderTargetData)(T * This, IDirect3DSurface9 *target, IDirect3DSurface9 *dest);     \
    HRESULT(STDMETHODCALLTYPE *GetFrontBufferData)(T * This, UINT swapchain, IDirect3DSurface9 *dest);                 \
    HRESULT(STDMETHODCALLTYPE *StretchRect)(T * This, IDirect3DSurface9 *source, const RECT *source_rect,              \
                                            IDirect3DSurface9 *dest, const RECT *dest_rect,                            \
                                            D3DTEXTUREFILTERTYPE filter);                                              \
    HRESULT(STDMETHODCALLTYPE *ColorFill)(T * This, IDirect3DSurface9 *surface, const RECT *rect, D3DCOLOR color);     \
    HRESULT(STDMETHODCALLTYPE *CreateOffscreenPlainSurface)(T * This, UINT width, UINT height, D3DFORMAT format,       \
                                                            D3DPOOL pool, IDirect3DSurface9 **surface,                 \
                                                            HANDLE *shared);                                           \
    HRESULT(STDMETHODCALLTYPE *SetRenderTarget)(T * This, DWORD index, IDirect3DSurface9 *surface);                    \
    HRESULT(STDMETHODCALLTYPE *GetRenderTarget)(T * This, DWORD index, IDirect3DSurface9 **surface);                   \
    HRESULT(STDMETHODCALLTYPE *SetDepthStencilSurface)(T * This, IDirect3DSurface9 *surface);                          \
    HRESULT(STDMETHODCALLTYPE *GetDepthStencilSurface)(T * This, IDirect3DSurface9 **surface);                         \
    HRESULT(STDMETHODCALLTYPE *BeginScene)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *EndScene)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *Clear)(T * This, DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color,        \
                                      float z, DWORD stencil);                                                         \
    HRESULT(STDMETHODCALLTYPE *SetTransform)(T * This, D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix);          \
    HRESULT(STDMETHODCALLTYPE *GetTransform)(T * This, D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix);                \
    HRESULT(STDMETHODCALLTYPE *MultiplyTransform)(T * This, D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix);     \
    HRESULT(STDMETHODCALLTYPE *SetViewport)(T * This, const D3DVIEWPORT9 *viewport);                                   \
    HRESULT(STDMETHODCALLTYPE *GetViewport)(T * This, D3DVIEWPORT9 *viewport);                                         \
    HRESULT(STDMETHODCALLTYPE *SetMaterial)(T * This, const D3DMATERIAL9 *material);                                   \
    HRESULT(STDMETHODCALLTYPE *GetMaterial)(T * This, D3DMATERIAL9 *material);                                         \
    HRESULT(STDMETHODCALLTYPE *SetLight)(T * This, DWORD index, const D3DLIGHT9 *light);                               \
    HRESULT(STDMETHODCALLTYPE *GetLight)(T * This, DWORD index, D3DLIGHT9 *light);                                     \
    HRESULT(STDMETHODCALLTYPE *LightEnable)(T * This, DWORD index, BOOL enable);                                       \
    HRESULT(STDMETHODCALLTYPE *GetLightEnable)(T * This, DWORD index, BOOL *enable);                                   \
    HRESULT(STDMETHODCALLTYPE *SetClipPlane)(T * This, DWORD index, const float *plane);                               \
    HRESULT(STDMETHODCALLTYPE *GetClipPlane)(T * This, DWORD index, float *plane);                                     \
    HRESULT(STDMETHODCALLTYPE *SetRenderState)(T * This, D3DRENDERSTATETYPE state, DWORD value);                       \
    HRESULT(STDMETHODCALLTYPE *GetRenderState)(T * This, D3DRENDERSTATETYPE state, DWORD *value);                      \
    HRESULT(STDMETHODCALLTYPE *CreateStateBlock)(T * This, D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9 **block);      \
    HRESULT(STDMETHODCALLTYPE *BeginStateBlock)(T * This);                                                             \
    HRESULT(STDMETHODCALLTYPE *EndStateBlock)(T * This, IDirect3DStateBlock9 **block);                                 \
    HRESULT(STDMETHODCALLTYPE *SetClipStatus)(T * This, const D3DCLIPSTATUS9 *status);                                 \
    HRESULT(STDMETHODCALLTYPE *GetClipStatus)(T * This, D3DCLIPSTATUS9 *status);                                       \
    HRESULT(STDMETHODCALLTYPE *GetTexture)(T * This, DWORD stage, IDirect3DBaseTexture9 **texture);                    \
    HRESULT(STDMETHODCALLTYPE *SetTexture)(T * This, DWORD stage, IDirect3DBaseTexture9 *texture);                     \
    HRESULT(STDMETHODCALLTYPE *GetTextureStageState)(T * This, DWORD stage, D3DTEXTURESTAGESTATETYPE type,             \
                                                     DWORD *value);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetTextureStageState)(T * This, DWORD stage, D3DTEXTURESTAGESTATETYPE type,             \
                                                     DWORD value);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetSamplerState)(T * This, DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD *value);      \
    HRESULT(STDMETHODCALLTYPE *SetSamplerState)(T * This, DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value);       \
    HRESULT(STDMETHODCALLTYPE *ValidateDevice)(T * This, DWORD *passes);                                               \
    HRESULT(STDMETHODCALLTYPE *SetPaletteEntries)(T * This, UINT palette, const PALETTEENTRY *entries);                \
    HRESULT(STDMETHODCALLTYPE *GetPaletteEntries)(T * This, UINT palette, PALETTEENTRY *entries);                      \
    HRESULT(STDMETHODCALLTYPE *SetCurrentTexturePalette)(T * This, UINT palette);                                      \
    HRESULT(STDMETHODCALLTYPE *GetCurrentTexturePalette)(T * This, UINT *palette);                                     \
    HRESULT(STDMETHODCALLTYPE *SetScissorRect)(T * This, const RECT *rect);                                            \
    HRESULT(STDMETHODCALLTYPE *GetScissorRect)(T * This, RECT *rect);                                                  \
    HRESULT(STDMETHODCALLTYPE *SetSoftwareVertexProcessing)(T * This, BOOL software);                                  \
    BOOL(STDMETHODCALLTYPE *GetSoftwareVertexProcessing)(T * This);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetNPatchMode)(T * This, float segments);                                               \
    float(STDMETHODCALLTYPE *GetNPatchMode)(T * This);                                                                 \
    HRESULT(STDMETHODCALLTYPE *DrawPrimitive)(T * This, D3DPRIMITIVETYPE type, UINT start, UINT count);                \
    HRESULT(STDMETHODCALLTYPE *DrawIndexedPrimitive)(T * This, D3DPRIMITIVETYPE type, INT base, UINT min_index,        \
                                                     UINT vertices, UINT start, UINT count);                           \
    HRESULT(STDMETHODCALLTYPE *DrawPrimitiveUP)(T * This, D3DPRIMITIVETYPE type, UINT count, const void *data,         \
                                                UINT stride);                                                          \
    HRESULT(STDMETHODCALLTYPE *DrawIndexedPrimitiveUP)(T * This, D3DPRIMITIVETYPE type, UINT min_index,                \
                                                       UINT vertices, UINT count, const void *indices,                 \
                                                       D3DFORMAT index_format, const void *data, UINT stride);         \
    HRESULT(STDMETHODCALLTYPE *ProcessVertices)(T * This, UINT source, UINT dest, UINT count,                          \
                                                IDirect3DVertexBuffer9 *buffer,                                        \
                                                IDirect3DVertexDeclaration9 *declaration, DWORD flags);                \
    HRESULT(STDMETHODCALLTYPE *CreateVertexDeclaration)(T * This, const D3DVERTEXELEMENT9 *elements,                   \
                                                        IDirect3DVertexDeclaration9 **declaration);                    \
    HRESULT(STDMETHODCALLTYPE *SetVertexDeclaration)(T * This, IDirect3DVertexDeclaration9 *declaration);              \
    HRESULT(STDMETHODCALLTYPE *GetVertexDeclaration)(T * This, IDirect3DVertexDeclaration9 **declaration);             \
    HRESULT(STDMETHODCALLTYPE *SetFVF)(T * This, DWORD fvf);                                                           \
    HRESULT(STDMETHODCALLTYPE *GetFVF)(T * This, DWORD *fvf);                                                          \
    HRESULT(STDMETHODCALLTYPE *CreateVertexShader)(T * This, const DWORD *code, IDirect3DVertexShader9 **shader);      \
    HRESULT(STDMETHODCALLTYPE *SetVertexShader)(T * This, IDirect3DVertexShader9 *shader);                             \
    HRESULT(STDMETHODCALLTYPE *GetVertexShader)(T * This, IDirect3DVertexShader9 **shader);                            \
    HRESULT(STDMETHODCALLTYPE *SetVertexShaderConstantF)(T * This, UINT start, const float *data, UINT count);         \
    HRESULT(STDMETHODCALLTYPE *GetVertexShaderConstantF)(T * This, UINT start, float *data, UINT count);               \
    HRESULT(STDMETHODCALLTYPE *SetVertexShaderConstantI)(T * This, UINT start, const int *data, UINT count);           \
    HRESULT(STDMETHODCALLTYPE *GetVertexShaderConstantI)(T * This, UINT start, int *data, UINT count);                 \
    HRESULT(STDMETHODCALLTYPE *SetVertexShaderConstantB)(T * This, UINT start, const BOOL *data, UINT count);          \
    HRESULT(STDMETHODCALLTYPE *GetVertexShaderConstantB)(T * This, UINT start, BOOL *data, UINT count);                \
    HRESULT(STDMETHODCALLTYPE *SetStreamSource)(T * This, UINT stream, IDirect3DVertexBuffer9 *buffer, UINT offset,    \
                                                UINT stride);                                                          \
    HRESULT(STDMETHODCALLTYPE *GetStreamSource)(T * This, UINT stream, IDirect3DVertexBuffer9 **buffer, UINT *offset,  \
                                                UINT *stride);                                                         \
    HRESULT(STDMETHODCALLTYPE *SetStreamSourceFreq)(T * This, UINT stream, UINT setting);                              \
    HRESULT(STDMETHODCALLTYPE *GetStreamSourceFreq)(T * This, UINT stream, UINT *setting);                             \
    HRESULT(STDMETHODCALLTYPE *SetIndices)(T * This, IDirect3DIndexBuffer9 *buffer);                                   \
    HRESULT(STDMETHODCALLTYPE *GetIndices)(T * This, IDirect3DIndexBuffer9 **buffer);                                  \
    HRESULT(STDMETHODCALLTYPE *CreatePixelShader)(T * This, const DWORD *code, IDirect3DPixelShader9 **shader);        \
    HRESULT(STDMETHODCALLTYPE *SetPixelShader)(T * This, IDirect3DPixelShader9 *shader);                               \
    HRESULT(STDMETHODCALLTYPE *GetPixelShader)(T * This, IDirect3DPixelShader9 **shader);                              \
    HRESULT(STDMETHODCALLTYPE *SetPixelShaderConstantF)(T * This, UINT start, const float *data, UINT count);          \
    HRESULT(STDMETHODCALLTYPE *GetPixelShaderConstantF)(T * This, UINT start, float *data, UINT count);                \
    HRESULT(STDMETHODCALLTYPE *SetPixelShaderConstantI)(T * This, UINT start, const int *data, UINT count);            \
    HRESULT(STDMETHODCALLTYPE *GetPixelShaderConstantI)(T * This, UINT start, int *data, UINT count);                  \
    HRESULT(STDMETHODCALLTYPE *SetPixelShaderConstantB)(T * This, UINT start, const BOOL *data, UINT count);           \
    HRESULT(STDMETHODCALLTYPE *GetPixelShaderConstantB)(T * This, UINT start, BOOL *data, UINT count);                 \
    HRESULT(STDMETHODCALLTYPE *DrawRectPatch)(T * This, UINT handle, const float *segments,                            \
                                              const D3DRECTPATCH_INFO *info);                                          \
    HRESULT(STDMETHODCALLTYPE *DrawTriPatch)(T * This, UINT handle, const float *segments,                             \
                                             const D3DTRIPATCH_INFO *info);                                            \
    HRESULT(STDMETHODCALLTYPE *DeletePatch)(T * This, UINT handle);                                                    \
    HRESULT(STDMETHODCALLTYPE *CreateQuery)(T * This, D3DQUERYTYPE type, IDirect3DQuery9 **query);

#define IDirect3DDevice9Ex_METHODS(T)                                                                                  \
    IDirect3DDevice9_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *SetConvolutionMonoKernel)(T * This, UINT width, UINT height, float *rows,               \
                                                         float *columns);                                              \
    HRESULT(STDMETHODCALLTYPE *ComposeRects)(T * This, IDirect3DSurface9 *source, IDirect3DSurface9 *dest,             \
                                             IDirect3DVertexBuffer9 *source_rects, UINT count,                         \
                                             IDirect3DVertexBuffer9 *dest_rects, D3DCOMPOSERECTSOP op, int x, int y);  \
    HRESULT(STDMETHODCALLTYPE *PresentEx)(T * This, const RECT *source, const RECT *dest, HWND window,                 \
                                          const RGNDATA *dirty, DWORD flags);                                          \
    HRESULT(STDMETHODCALLTYPE *GetGPUThreadPriority)(T * This, INT *priority);                                         \
    HRESULT(STDMETHODCALLTYPE *SetGPUThreadPriority)(T * This, INT priority);                                          \
    HRESULT(STDMETHODCALLTYPE *WaitForVBlank)(T * This, UINT swapchain);                                               \
    HRESULT(STDMETHODCALLTYPE *CheckResourceResidency)(T * This, IDirect3DResource9 **resources, UINT32 count);        \
    HRESULT(STDMETHODCALLTYPE *SetMaximumFrameLatency)(T * This, UINT latency);                                        \
    HRESULT(STDMETHODCALLTYPE *GetMaximumFrameLatency)(T * This, UINT *latency);                                       \
    HRESULT(STDMETHODCALLTYPE *CheckDeviceState)(T * This, HWND window);                                               \
    HRESULT(STDMETHODCALLTYPE *CreateRenderTargetEx)(T * This, UINT width, UINT height, D3DFORMAT format,              \
                                                     D3DMULTISAMPLE_TYPE samples, DWORD quality, BOOL lockable,        \
                                                     IDirect3DSurface9 **surface, HANDLE *shared, DWORD usage);        \
    HRESULT(STDMETHODCALLTYPE *CreateOffscreenPlainSurfaceEx)(T * This, UINT width, UINT height, D3DFORMAT format,     \
                                                              D3DPOOL pool, IDirect3DSurface9 **surface,               \
                                                              HANDLE *shared, DWORD usage);                            \
    HRESULT(STDMETHODCALLTYPE *CreateDepthStencilSurfaceEx)(T * This, UINT width, UINT height, D3DFORMAT format,       \
                                                            D3DMULTISAMPLE_TYPE samples, DWORD quality, BOOL discard,  \
                                                            IDirect3DSurface9 **surface, HANDLE *shared,               \
                                                            DWORD usage);                                              \
    HRESULT(STDMETHODCALLTYPE *ResetEx)(T * This, D3DPRESENT_PARAMETERS *parameters, D3DDISPLAYMODEEX *mode);          \
    HRESULT(STDMETHODCALLTYPE *GetDisplayModeEx)(T * This, UINT swapchain, D3DDISPLAYMODEEX *mode,                     \
                                                 D3DDISPLAYROTATION *rotation);

#define IDirect3DStateBlock9_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDevice)(T * This, IDirect3DDevice9 **device);                                        \
    HRESULT(STDMETHODCALLTYPE *Capture)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *Apply)(T * This);

#define IDirect3DSwapChain9_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Present)(T * This, const RECT *source, const RECT *dest, HWND window,                   \
                                        const RGNDATA *dirty, DWORD flags);                                            \
    HRESULT(STDMETHODCALLTYPE *GetFrontBufferData)(T * This, IDirect3DSurface9 *dest);                                 \
    HRESULT(STDMETHODCALLTYPE *GetBackBuffer)(T * This, UINT index, D3DBACKBUFFER_TYPE type,                           \
                                              IDirect3DSurface9 **surface);                                            \
    HRESULT(STDMETHODCALLTYPE *GetRasterStatus)(T * This, D3DRASTER_STATUS *status);                                   \
    HRESULT(STDMETHODCALLTYPE *GetDisplayMode)(T * This, D3DDISPLAYMODE *mode);                                        \
    HRESULT(STDMETHODCALLTYPE *GetDevice)(T * This, IDirect3DDevice9 **device);                                        \
    HRESULT(STDMETHODCALLTYPE *GetPresentParameters)(T * This, D3DPRESENT_PARAMETERS *parameters);

#define IDirect3DSwapChain9Ex_METHODS(T)                                                                               \
    IDirect3DSwapChain9_METHODS(T)                                                                                     \
    HRESULT(STDMETHODCALLTYPE *GetLastPresentCount)(T * This, UINT *count);                                            \
    HRESULT(STDMETHODCALLTYPE *GetPresentStats)(T * This, D3DPRESENTSTATS *stats);                                     \
    HRESULT(STDMETHODCALLTYPE *GetDisplayModeEx)(T * This, D3DDISPLAYMODEEX *mode, D3DDISPLAYROTATION *rotation);

#define IDirect3DResource9_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDevice)(T * This, IDirect3DDevice9 **device);                                        \
    HRESULT(STDMETHODCALLTYPE *SetPrivateData)(T * This, REFGUID guid, const void *data, DWORD size, DWORD flags);     \
    HRESULT(STDMETHODCALLTYPE *GetPrivateData)(T * This, REFGUID guid, void *data, DWORD *size);                       \
    HRESULT(STDMETHODCALLTYPE *FreePrivateData)(T * This, REFGUID guid);                                               \
    DWORD(STDMETHODCALLTYPE *SetPriority)(T * This, DWORD priority);                                                   \
    DWORD(STDMETHODCALLTYPE *GetPriority)(T * This);                                                                   \
    void(STDMETHODCALLTYPE *PreLoad)(T * This);                                                                        \
    D3DRESOURCETYPE(STDMETHODCALLTYPE *GetType)(T * This);

#define IDirect3DVertexDeclaration9_METHODS(T)                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDevice)(T * This, IDirect3DDevice9 **device);                                        \
    HRESULT(STDMETHODCALLTYPE *GetDeclaration)(T * This, D3DVERTEXELEMENT9 *elements, UINT *count);

#define IDirect3DVertexShader9_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDevice)(T * This, IDirect3DDevice9 **device);                                        \
    HRESULT(STDMETHODCALLTYPE *GetFunction)(T * This, void *data, UINT *size);

#define IDirect3DPixelShader9_METHODS(T) IDirect3DVertexShader9_METHODS(T)

#define IDirect3DBaseTexture9_METHODS(T)                                                                               \
    IDirect3DResource9_METHODS(T)                                                                                      \
    DWORD(STDMETHODCALLTYPE *SetLOD)(T * This, DWORD lod);                                                             \
    DWORD(STDMETHODCALLTYPE *GetLOD)(T * This);                                                                        \
    DWORD(STDMETHODCALLTYPE *GetLevelCount)(T * This);                                                                 \
    HRESULT(STDMETHODCALLTYPE *SetAutoGenFilterType)(T * This, D3DTEXTUREFILTERTYPE filter);                           \
    D3DTEXTUREFILTERTYPE(STDMETHODCALLTYPE *GetAutoGenFilterType)(T * This);                                           \
    void(STDMETHODCALLTYPE *GenerateMipSubLevels)(T * This);

#define IDirect3DTexture9_METHODS(T)                                                                                   \
    IDirect3DBaseTexture9_METHODS(T)                                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetLevelDesc)(T * This, UINT level, D3DSURFACE_DESC *desc);                             \
    HRESULT(STDMETHODCALLTYPE *GetSurfaceLevel)(T * This, UINT level, IDirect3DSurface9 **surface);                    \
    HRESULT(STDMETHODCALLTYPE *LockRect)(T * This, UINT level, D3DLOCKED_RECT *locked, const RECT *rect,               \
                                         DWORD flags);                                                                 \
    HRESULT(STDMETHODCALLTYPE *UnlockRect)(T * This, UINT level);                                                      \
    HRESULT(STDMETHODCALLTYPE *AddDirtyRect)(T * This, const RECT *rect);

#define IDirect3DVolumeTexture9_METHODS(T)                                                                             \
    IDirect3DBaseTexture9_METHODS(T)                                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetLevelDesc)(T * This, UINT level, D3DVOLUME_DESC *desc);                              \
    HRESULT(STDMETHODCALLTYPE *GetVolumeLevel)(T * This, UINT level, IDirect3DVolume9 **volume);                       \
    HRESULT(STDMETHODCALLTYPE *LockBox)(T * This, UINT level, D3DLOCKED_BOX *locked, const D3DBOX *box, DWORD flags);  \
    HRESULT(STDMETHODCALLTYPE *UnlockBox)(T * This, UINT level);                                                       \
    HRESULT(STDMETHODCALLTYPE *AddDirtyBox)(T * This, const D3DBOX *box);

#define IDirect3DCubeTexture9_METHODS(T)                                                                               \
    IDirect3DBaseTexture9_METHODS(T)                                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetLevelDesc)(T * This, UINT level, D3DSURFACE_DESC *desc);                             \
    HRESULT(STDMETHODCALLTYPE *GetCubeMapSurface)(T * This, D3DCUBEMAP_FACES face, UINT level,                         \
                                                  IDirect3DSurface9 **surface);                                        \
    HRESULT(STDMETHODCALLTYPE *LockRect)(T * This, D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *locked,          \
                                         const RECT *rect, DWORD flags);                                               \
    HRESULT(STDMETHODCALLTYPE *UnlockRect)(T * This, D3DCUBEMAP_FACES face, UINT level);                               \
    HRESULT(STDMETHODCALLTYPE *AddDirtyRect)(T * This, D3DCUBEMAP_FACES face, const RECT *rect);

#define IDirect3DVertexBuffer9_METHODS(T)                                                                              \
    IDirect3DResource9_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *Lock)(T * This, UINT offset, UINT size, void **data, DWORD flags);                      \
    HRESULT(STDMETHODCALLTYPE *Unlock)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3DVERTEXBUFFER_DESC *desc);

#define IDirect3DIndexBuffer9_METHODS(T)                                                                               \
    IDirect3DResource9_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *Lock)(T * This, UINT offset, UINT size, void **data, DWORD flags);                      \
    HRESULT(STDMETHODCALLTYPE *Unlock)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3DINDEXBUFFER_DESC *desc);

#define IDirect3DSurface9_METHODS(T)                                                                                   \
    IDirect3DResource9_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *GetContainer)(T * This, REFIID riid, void **container);                                 \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3DSURFACE_DESC *desc);                                              \
    HRESULT(STDMETHODCALLTYPE *LockRect)(T * This, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags);             \
    HRESULT(STDMETHODCALLTYPE *UnlockRect)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *GetDC)(T * This, HDC * dc);                                                             \
    HRESULT(STDMETHODCALLTYPE *ReleaseDC)(T * This, HDC dc);

#define IDirect3DVolume9_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDevice)(T * This, IDirect3DDevice9 **device);                                        \
    HRESULT(STDMETHODCALLTYPE *SetPrivateData)(T * This, REFGUID guid, const void *data, DWORD size, DWORD flags);     \
    HRESULT(STDMETHODCALLTYPE *GetPrivateData)(T * This, REFGUID guid, void *data, DWORD *size);                       \
    HRESULT(STDMETHODCALLTYPE *FreePrivateData)(T * This, REFGUID guid);                                               \
    HRESULT(STDMETHODCALLTYPE *GetContainer)(T * This, REFIID riid, void **container);                                 \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3DVOLUME_DESC *desc);                                               \
    HRESULT(STDMETHODCALLTYPE *LockBox)(T * This, D3DLOCKED_BOX *locked, const D3DBOX *box, DWORD flags);              \
    HRESULT(STDMETHODCALLTYPE *UnlockBox)(T * This);

#define IDirect3DQuery9_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDevice)(T * This, IDirect3DDevice9 **device);                                        \
    D3DQUERYTYPE(STDMETHODCALLTYPE *GetType)(T * This);                                                                \
    DWORD(STDMETHODCALLTYPE *GetDataSize)(T * This);                                                                   \
    HRESULT(STDMETHODCALLTYPE *Issue)(T * This, DWORD flags);                                                          \
    HRESULT(STDMETHODCALLTYPE *GetData)(T * This, void *data, DWORD size, DWORD flags);

MIME_COM_INTERFACE(IDirect3D9)
MIME_COM_INTERFACE(IDirect3D9Ex)
MIME_COM_INTERFACE(IDirect3DDevice9)
MIME_COM_INTERFACE(IDirect3DDevice9Ex)
MIME_COM_INTERFACE(IDirect3DStateBlock9)
MIME_COM_INTERFACE(IDirect3DSwapChain9)
MIME_COM_INTERFACE(IDirect3DSwapChain9Ex)
MIME_COM_INTERFACE(IDirect3DResource9)
MIME_COM_INTERFACE(IDirect3DVertexDeclaration9)
MIME_COM_INTERFACE(IDirect3DVertexShader9)
MIME_COM_INTERFACE(IDirect3DPixelShader9)
MIME_COM_INTERFACE(IDirect3DBaseTexture9)
MIME_COM_INTERFACE(IDirect3DTexture9)
MIME_COM_INTERFACE(IDirect3DVolumeTexture9)
MIME_COM_INTERFACE(IDirect3DCubeTexture9)
MIME_COM_INTERFACE(IDirect3DVertexBuffer9)
MIME_COM_INTERFACE(IDirect3DIndexBuffer9)
MIME_COM_INTERFACE(IDirect3DSurface9)
MIME_COM_INTERFACE(IDirect3DVolume9)
MIME_COM_INTERFACE(IDirect3DQuery9)

MIME_DEFINE_GUID(IID_IDirect3D9, 0x81BDCBCA, 0x64D4, 0x426D, 0xAE, 0x8D, 0xAD, 0x01, 0x47, 0xF4, 0x27, 0x5C);
MIME_DEFINE_GUID(IID_IDirect3D9Ex, 0x02177241, 0x69FC, 0x400C, 0x8F, 0xF1, 0x93, 0xA4, 0x4D, 0xF6, 0x86, 0x1D);
MIME_DEFINE_GUID(IID_IDirect3DDevice9, 0xD0223B96, 0xBF7A, 0x43FD, 0x92, 0xBD, 0xA4, 0x3B, 0x0D, 0x82, 0xB9, 0xEB);
MIME_DEFINE_GUID(IID_IDirect3DDevice9Ex, 0xB18B10CE, 0x2649, 0x405A, 0x87, 0x0F, 0x95, 0xF7, 0x77, 0xD4, 0x31, 0x3A);
MIME_DEFINE_GUID(IID_IDirect3DResource9, 0x05EEC05D, 0x8F7D, 0x4362, 0xB9, 0x99, 0xD1, 0xBA, 0xF3, 0x57, 0xC7, 0x04);
MIME_DEFINE_GUID(IID_IDirect3DBaseTexture9, 0x580CA87E, 0x1D3C, 0x4D54, 0x99, 0x1D, 0xB7, 0xD3, 0xE3, 0xC2, 0x98, 0xCE);
MIME_DEFINE_GUID(IID_IDirect3DTexture9, 0x85C31227, 0x3DE5, 0x4F00, 0x9B, 0x3A, 0xF1, 0x1A, 0xC3, 0x8C, 0x18, 0xB5);
MIME_DEFINE_GUID(IID_IDirect3DCubeTexture9, 0xFFF32F81, 0xD953, 0x473A, 0x92, 0x23, 0x93, 0xD6, 0x52, 0xAB, 0xA9, 0x3F);
MIME_DEFINE_GUID(IID_IDirect3DVolumeTexture9, 0x2518526C, 0xE789, 0x4111, 0xA7, 0xB9, 0x47, 0xEF, 0x32, 0x8D, 0x13,
                 0xE6);
MIME_DEFINE_GUID(IID_IDirect3DVertexBuffer9, 0xB64BB1B5, 0xFD70, 0x4DF6, 0xBF, 0x91, 0x19, 0xD0, 0xA1, 0x24, 0x55,
                 0xE3);
MIME_DEFINE_GUID(IID_IDirect3DIndexBuffer9, 0x7C9DD65E, 0xD3F7, 0x4529, 0xAC, 0xEE, 0x78, 0x58, 0x30, 0xAC, 0xDE, 0x35);
MIME_DEFINE_GUID(IID_IDirect3DSurface9, 0x0CFBAF3A, 0x9FF6, 0x429A, 0x99, 0xB3, 0xA2, 0x79, 0x6A, 0xF8, 0xB8, 0x9B);
MIME_DEFINE_GUID(IID_IDirect3DVolume9, 0x24F416E6, 0x1F67, 0x4AA7, 0xB8, 0x8E, 0xD3, 0x3F, 0x6F, 0x31, 0x28, 0xA1);
MIME_DEFINE_GUID(IID_IDirect3DSwapChain9, 0x794950F2, 0xADFC, 0x458A, 0x90, 0x5E, 0x10, 0xA1, 0x0B, 0x0B, 0x50, 0x3B);
MIME_DEFINE_GUID(IID_IDirect3DSwapChain9Ex, 0x91886CAF, 0x1C3D, 0x4D2E, 0xA0, 0xAB, 0x3E, 0x4C, 0x7D, 0x8D, 0x33, 0x03);
MIME_DEFINE_GUID(IID_IDirect3DVertexDeclaration9, 0xDD13C59C, 0x36FA, 0x4098, 0xA8, 0xFB, 0xC7, 0xED, 0x39, 0xDC, 0x85,
                 0x46);
MIME_DEFINE_GUID(IID_IDirect3DVertexShader9, 0xEFC5557E, 0x6265, 0x4613, 0x8A, 0x94, 0x43, 0x85, 0x78, 0x89, 0xEB,
                 0x36);
MIME_DEFINE_GUID(IID_IDirect3DPixelShader9, 0x6D3BDBDC, 0x5B02, 0x4415, 0xB8, 0x52, 0xCE, 0x5E, 0x8B, 0xCC, 0xB2, 0x89);
MIME_DEFINE_GUID(IID_IDirect3DStateBlock9, 0xB07C4FE5, 0x310D, 0x4BA8, 0xA2, 0x3C, 0x4F, 0x0F, 0x20, 0x6F, 0x21, 0x8B);
MIME_DEFINE_GUID(IID_IDirect3DQuery9, 0xD9771460, 0xA695, 0x4F26, 0xBB, 0xD3, 0x27, 0xB8, 0x40, 0xB5, 0x41, 0xCC);

#ifndef MIME_BUILD_D3D9
__declspec(dllimport) IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk);
__declspec(dllimport) HRESULT WINAPI Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex **d3d);
__declspec(dllimport) int WINAPI D3DPERF_BeginEvent(D3DCOLOR color, LPCWSTR name);
__declspec(dllimport) int WINAPI D3DPERF_EndEvent(void);
__declspec(dllimport) void WINAPI D3DPERF_SetMarker(D3DCOLOR color, LPCWSTR name);
__declspec(dllimport) void WINAPI D3DPERF_SetRegion(D3DCOLOR color, LPCWSTR name);
__declspec(dllimport) BOOL WINAPI D3DPERF_QueryRepeatFrame(void);
__declspec(dllimport) void WINAPI D3DPERF_SetOptions(DWORD options);
__declspec(dllimport) DWORD WINAPI D3DPERF_GetStatus(void);
#endif

#endif
