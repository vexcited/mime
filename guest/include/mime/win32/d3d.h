/* direct3d 1 to 7, served by ddraw. methods follow windows' vtable order. */
#ifndef MIME_WIN32_D3D_H
#define MIME_WIN32_D3D_H

#include <mime/win32/d3dtypes.h>
#include <mime/win32/d3dcaps.h>

typedef struct IDirect3D IDirect3D, *LPDIRECT3D;
typedef struct IDirect3D2 IDirect3D2, *LPDIRECT3D2;
typedef struct IDirect3D3 IDirect3D3, *LPDIRECT3D3;
typedef struct IDirect3D7 IDirect3D7, *LPDIRECT3D7;
typedef struct IDirect3DDevice IDirect3DDevice, *LPDIRECT3DDEVICE;
typedef struct IDirect3DDevice2 IDirect3DDevice2, *LPDIRECT3DDEVICE2;
typedef struct IDirect3DDevice3 IDirect3DDevice3, *LPDIRECT3DDEVICE3;
typedef struct IDirect3DDevice7 IDirect3DDevice7, *LPDIRECT3DDEVICE7;
typedef struct IDirect3DExecuteBuffer IDirect3DExecuteBuffer, *LPDIRECT3DEXECUTEBUFFER;
typedef struct IDirect3DLight IDirect3DLight, *LPDIRECT3DLIGHT;
typedef struct IDirect3DMaterial IDirect3DMaterial, *LPDIRECT3DMATERIAL;
typedef struct IDirect3DMaterial2 IDirect3DMaterial2, *LPDIRECT3DMATERIAL2;
typedef struct IDirect3DMaterial3 IDirect3DMaterial3, *LPDIRECT3DMATERIAL3;
typedef struct IDirect3DTexture IDirect3DTexture, *LPDIRECT3DTEXTURE;
typedef struct IDirect3DTexture2 IDirect3DTexture2, *LPDIRECT3DTEXTURE2;
typedef struct IDirect3DViewport IDirect3DViewport, *LPDIRECT3DVIEWPORT;
typedef struct IDirect3DViewport2 IDirect3DViewport2, *LPDIRECT3DVIEWPORT2;
typedef struct IDirect3DViewport3 IDirect3DViewport3, *LPDIRECT3DVIEWPORT3;
typedef struct IDirect3DVertexBuffer IDirect3DVertexBuffer, *LPDIRECT3DVERTEXBUFFER;
typedef struct IDirect3DVertexBuffer7 IDirect3DVertexBuffer7, *LPDIRECT3DVERTEXBUFFER7;

/* ------------------------------------------------------------------ results */

#define D3D_OK DD_OK
#define D3DERR_BADMAJORVERSION MAKE_DDHRESULT(700)
#define D3DERR_BADMINORVERSION MAKE_DDHRESULT(701)
#define D3DERR_INVALID_DEVICE MAKE_DDHRESULT(705)
#define D3DERR_INITFAILED MAKE_DDHRESULT(706)
#define D3DERR_DEVICEAGGREGATED MAKE_DDHRESULT(707)
#define D3DERR_EXECUTE_CREATE_FAILED MAKE_DDHRESULT(710)
#define D3DERR_EXECUTE_DESTROY_FAILED MAKE_DDHRESULT(711)
#define D3DERR_EXECUTE_LOCK_FAILED MAKE_DDHRESULT(712)
#define D3DERR_EXECUTE_UNLOCK_FAILED MAKE_DDHRESULT(713)
#define D3DERR_EXECUTE_LOCKED MAKE_DDHRESULT(714)
#define D3DERR_EXECUTE_NOT_LOCKED MAKE_DDHRESULT(715)
#define D3DERR_EXECUTE_FAILED MAKE_DDHRESULT(716)
#define D3DERR_EXECUTE_CLIPPED_FAILED MAKE_DDHRESULT(717)
#define D3DERR_TEXTURE_NO_SUPPORT MAKE_DDHRESULT(720)
#define D3DERR_TEXTURE_CREATE_FAILED MAKE_DDHRESULT(721)
#define D3DERR_TEXTURE_DESTROY_FAILED MAKE_DDHRESULT(722)
#define D3DERR_TEXTURE_LOCK_FAILED MAKE_DDHRESULT(723)
#define D3DERR_TEXTURE_UNLOCK_FAILED MAKE_DDHRESULT(724)
#define D3DERR_TEXTURE_LOAD_FAILED MAKE_DDHRESULT(725)
#define D3DERR_TEXTURE_SWAP_FAILED MAKE_DDHRESULT(726)
#define D3DERR_TEXTURE_LOCKED MAKE_DDHRESULT(727)
#define D3DERR_TEXTURE_NOT_LOCKED MAKE_DDHRESULT(728)
#define D3DERR_TEXTURE_GETSURF_FAILED MAKE_DDHRESULT(729)
#define D3DERR_MATRIX_CREATE_FAILED MAKE_DDHRESULT(730)
#define D3DERR_MATRIX_DESTROY_FAILED MAKE_DDHRESULT(731)
#define D3DERR_MATRIX_SETDATA_FAILED MAKE_DDHRESULT(732)
#define D3DERR_MATRIX_GETDATA_FAILED MAKE_DDHRESULT(733)
#define D3DERR_SETVIEWPORTDATA_FAILED MAKE_DDHRESULT(734)
#define D3DERR_INVALIDCURRENTVIEWPORT MAKE_DDHRESULT(735)
#define D3DERR_INVALIDPRIMITIVETYPE MAKE_DDHRESULT(736)
#define D3DERR_INVALIDVERTEXTYPE MAKE_DDHRESULT(737)
#define D3DERR_TEXTURE_BADSIZE MAKE_DDHRESULT(738)
#define D3DERR_INVALIDRAMPTEXTURE MAKE_DDHRESULT(739)
#define D3DERR_MATERIAL_CREATE_FAILED MAKE_DDHRESULT(740)
#define D3DERR_MATERIAL_DESTROY_FAILED MAKE_DDHRESULT(741)
#define D3DERR_MATERIAL_SETDATA_FAILED MAKE_DDHRESULT(742)
#define D3DERR_MATERIAL_GETDATA_FAILED MAKE_DDHRESULT(743)
#define D3DERR_INVALIDPALETTE MAKE_DDHRESULT(744)
#define D3DERR_ZBUFF_NEEDS_SYSTEMMEMORY MAKE_DDHRESULT(745)
#define D3DERR_ZBUFF_NEEDS_VIDEOMEMORY MAKE_DDHRESULT(746)
#define D3DERR_SURFACENOTINVIDMEM MAKE_DDHRESULT(747)
#define D3DERR_LIGHT_SET_FAILED MAKE_DDHRESULT(750)
#define D3DERR_LIGHTHASVIEWPORT MAKE_DDHRESULT(751)
#define D3DERR_LIGHTNOTINTHISVIEWPORT MAKE_DDHRESULT(752)
#define D3DERR_SCENE_IN_SCENE MAKE_DDHRESULT(760)
#define D3DERR_SCENE_NOT_IN_SCENE MAKE_DDHRESULT(761)
#define D3DERR_SCENE_BEGIN_FAILED MAKE_DDHRESULT(762)
#define D3DERR_SCENE_END_FAILED MAKE_DDHRESULT(763)
#define D3DERR_INBEGIN MAKE_DDHRESULT(770)
#define D3DERR_NOTINBEGIN MAKE_DDHRESULT(771)
#define D3DERR_NOVIEWPORTS MAKE_DDHRESULT(772)
#define D3DERR_VIEWPORTDATANOTSET MAKE_DDHRESULT(773)
#define D3DERR_VIEWPORTHASNODEVICE MAKE_DDHRESULT(774)
#define D3DERR_NOCURRENTVIEWPORT MAKE_DDHRESULT(775)
#define D3DERR_INVALIDVERTEXFORMAT MAKE_DDHRESULT(2048)
#define D3DERR_COLORKEYATTACHED MAKE_DDHRESULT(2050)
#define D3DERR_VERTEXBUFFEROPTIMIZED MAKE_DDHRESULT(2060)
#define D3DERR_VBUF_CREATE_FAILED MAKE_DDHRESULT(2061)
#define D3DERR_VERTEXBUFFERLOCKED MAKE_DDHRESULT(2062)
#define D3DERR_VERTEXBUFFERUNLOCKFAILED MAKE_DDHRESULT(2063)
#define D3DERR_ZBUFFER_NOTPRESENT MAKE_DDHRESULT(2070)
#define D3DERR_STENCILBUFFER_NOTPRESENT MAKE_DDHRESULT(2071)
#define D3DERR_WRONGTEXTUREFORMAT MAKE_DDHRESULT(2072)
#define D3DERR_UNSUPPORTEDCOLOROPERATION MAKE_DDHRESULT(2073)
#define D3DERR_UNSUPPORTEDCOLORARG MAKE_DDHRESULT(2074)
#define D3DERR_UNSUPPORTEDALPHAOPERATION MAKE_DDHRESULT(2075)
#define D3DERR_UNSUPPORTEDALPHAARG MAKE_DDHRESULT(2076)
#define D3DERR_TOOMANYOPERATIONS MAKE_DDHRESULT(2077)
#define D3DERR_CONFLICTINGTEXTUREFILTER MAKE_DDHRESULT(2078)
#define D3DERR_UNSUPPORTEDFACTORVALUE MAKE_DDHRESULT(2079)
#define D3DERR_CONFLICTINGRENDERSTATE MAKE_DDHRESULT(2081)
#define D3DERR_UNSUPPORTEDTEXTUREFILTER MAKE_DDHRESULT(2082)
#define D3DERR_TOOMANYPRIMITIVES MAKE_DDHRESULT(2083)
#define D3DERR_INVALIDMATRIX MAKE_DDHRESULT(2084)
#define D3DERR_TOOMANYVERTICES MAKE_DDHRESULT(2085)
#define D3DERR_CONFLICTINGTEXTUREPALETTE MAKE_DDHRESULT(2086)
#define D3DERR_INVALIDSTATEBLOCK MAKE_DDHRESULT(2100)
#define D3DERR_INBEGINSTATEBLOCK MAKE_DDHRESULT(2101)
#define D3DERR_NOTINBEGINSTATEBLOCK MAKE_DDHRESULT(2102)

#define D3DNEXT_NEXT 0x00000001
#define D3DNEXT_HEAD 0x00000002
#define D3DNEXT_TAIL 0x00000004

#define D3DDP_WAIT 0x00000001
#define D3DDP_OUTOFORDER 0x00000002
#define D3DDP_DONOTCLIP 0x00000004
#define D3DDP_DONOTUPDATEEXTENTS 0x00000008
#define D3DDP_DONOTLIGHT 0x00000010

/* ------------------------------------------------------------------ the direct3d object */

#define IDirect3D_METHODS(T)                                                                                           \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, REFCLSID clsid);                                                  \
    HRESULT(STDMETHODCALLTYPE *EnumDevices)(T * This, LPD3DENUMDEVICESCALLBACK callback, LPVOID context);              \
    HRESULT(STDMETHODCALLTYPE *CreateLight)(T * This, LPDIRECT3DLIGHT * light, IUnknown * outer);                      \
    HRESULT(STDMETHODCALLTYPE *CreateMaterial)(T * This, LPDIRECT3DMATERIAL * material, IUnknown * outer);             \
    HRESULT(STDMETHODCALLTYPE *CreateViewport)(T * This, LPDIRECT3DVIEWPORT * viewport, IUnknown * outer);             \
    HRESULT(STDMETHODCALLTYPE *FindDevice)(T * This, LPD3DFINDDEVICESEARCH search, LPD3DFINDDEVICERESULT result);

#define IDirect3D2_METHODS(T)                                                                                          \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *EnumDevices)(T * This, LPD3DENUMDEVICESCALLBACK callback, LPVOID context);              \
    HRESULT(STDMETHODCALLTYPE *CreateLight)(T * This, LPDIRECT3DLIGHT * light, IUnknown * outer);                      \
    HRESULT(STDMETHODCALLTYPE *CreateMaterial)(T * This, LPDIRECT3DMATERIAL2 * material, IUnknown * outer);            \
    HRESULT(STDMETHODCALLTYPE *CreateViewport)(T * This, LPDIRECT3DVIEWPORT2 * viewport, IUnknown * outer);            \
    HRESULT(STDMETHODCALLTYPE *FindDevice)(T * This, LPD3DFINDDEVICESEARCH search, LPD3DFINDDEVICERESULT result);      \
    HRESULT(STDMETHODCALLTYPE *CreateDevice)(T * This, REFCLSID clsid, LPDIRECTDRAWSURFACE surface,                    \
                                             LPDIRECT3DDEVICE2 * device);

#define IDirect3D3_METHODS(T)                                                                                          \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *EnumDevices)(T * This, LPD3DENUMDEVICESCALLBACK callback, LPVOID context);              \
    HRESULT(STDMETHODCALLTYPE *CreateLight)(T * This, LPDIRECT3DLIGHT * light, IUnknown * outer);                      \
    HRESULT(STDMETHODCALLTYPE *CreateMaterial)(T * This, LPDIRECT3DMATERIAL3 * material, IUnknown * outer);            \
    HRESULT(STDMETHODCALLTYPE *CreateViewport)(T * This, LPDIRECT3DVIEWPORT3 * viewport, IUnknown * outer);            \
    HRESULT(STDMETHODCALLTYPE *FindDevice)(T * This, LPD3DFINDDEVICESEARCH search, LPD3DFINDDEVICERESULT result);      \
    HRESULT(STDMETHODCALLTYPE *CreateDevice)(T * This, REFCLSID clsid, LPDIRECTDRAWSURFACE4 surface,                   \
                                             LPDIRECT3DDEVICE3 * device, IUnknown * outer);                            \
    HRESULT(STDMETHODCALLTYPE *CreateVertexBuffer)(T * This, LPD3DVERTEXBUFFERDESC desc,                               \
                                                   LPDIRECT3DVERTEXBUFFER * buffer, DWORD flags, IUnknown * outer);    \
    HRESULT(STDMETHODCALLTYPE *EnumZBufferFormats)(T * This, REFCLSID device, LPD3DENUMPIXELFORMATSCALLBACK callback,  \
                                                   LPVOID context);                                                    \
    HRESULT(STDMETHODCALLTYPE *EvictManagedTextures)(T * This);

#define IDirect3D7_METHODS(T)                                                                                          \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *EnumDevices)(T * This, LPD3DENUMDEVICESCALLBACK7 callback, LPVOID context);             \
    HRESULT(STDMETHODCALLTYPE *CreateDevice)(T * This, REFCLSID clsid, LPDIRECTDRAWSURFACE7 surface,                   \
                                             LPDIRECT3DDEVICE7 * device);                                              \
    HRESULT(STDMETHODCALLTYPE *CreateVertexBuffer)(T * This, LPD3DVERTEXBUFFERDESC desc,                               \
                                                   LPDIRECT3DVERTEXBUFFER7 * buffer, DWORD flags);                     \
    HRESULT(STDMETHODCALLTYPE *EnumZBufferFormats)(T * This, REFCLSID device, LPD3DENUMPIXELFORMATSCALLBACK callback,  \
                                                   LPVOID context);                                                    \
    HRESULT(STDMETHODCALLTYPE *EvictManagedTextures)(T * This);

/* ------------------------------------------------------------------ devices */

#define IDirect3DDevice_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, LPDIRECT3D d3d, LPGUID guid, LPD3DDEVICEDESC desc);               \
    HRESULT(STDMETHODCALLTYPE *GetCaps)(T * This, LPD3DDEVICEDESC hal, LPD3DDEVICEDESC hel);                           \
    HRESULT(STDMETHODCALLTYPE *SwapTextureHandles)(T * This, LPDIRECT3DTEXTURE texture1, LPDIRECT3DTEXTURE texture2);  \
    HRESULT(STDMETHODCALLTYPE *CreateExecuteBuffer)(T * This, LPD3DEXECUTEBUFFERDESC desc,                             \
                                                    LPDIRECT3DEXECUTEBUFFER * buffer, IUnknown * outer);               \
    HRESULT(STDMETHODCALLTYPE *GetStats)(T * This, LPD3DSTATS stats);                                                  \
    HRESULT(STDMETHODCALLTYPE *Execute)(T * This, LPDIRECT3DEXECUTEBUFFER buffer, LPDIRECT3DVIEWPORT viewport,         \
                                        DWORD flags);                                                                  \
    HRESULT(STDMETHODCALLTYPE *AddViewport)(T * This, LPDIRECT3DVIEWPORT viewport);                                    \
    HRESULT(STDMETHODCALLTYPE *DeleteViewport)(T * This, LPDIRECT3DVIEWPORT viewport);                                 \
    HRESULT(STDMETHODCALLTYPE *NextViewport)(T * This, LPDIRECT3DVIEWPORT viewport, LPDIRECT3DVIEWPORT * next,         \
                                             DWORD flags);                                                             \
    HRESULT(STDMETHODCALLTYPE *Pick)(T * This, LPDIRECT3DEXECUTEBUFFER buffer, LPDIRECT3DVIEWPORT viewport,            \
                                     DWORD flags, LPD3DRECT rect);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetPickRecords)(T * This, LPDWORD count, LPD3DPICKRECORD records);                      \
    HRESULT(STDMETHODCALLTYPE *EnumTextureFormats)(T * This, LPD3DENUMTEXTUREFORMATSCALLBACK callback,                 \
                                                   LPVOID context);                                                    \
    HRESULT(STDMETHODCALLTYPE *CreateMatrix)(T * This, LPD3DMATRIXHANDLE matrix);                                      \
    HRESULT(STDMETHODCALLTYPE *SetMatrix)(T * This, D3DMATRIXHANDLE handle, const LPD3DMATRIX matrix);                 \
    HRESULT(STDMETHODCALLTYPE *GetMatrix)(T * This, D3DMATRIXHANDLE handle, LPD3DMATRIX matrix);                       \
    HRESULT(STDMETHODCALLTYPE *DeleteMatrix)(T * This, D3DMATRIXHANDLE handle);                                        \
    HRESULT(STDMETHODCALLTYPE *BeginScene)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *EndScene)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetDirect3D)(T * This, LPDIRECT3D * d3d);

/* the methods devices 2 and 3 share after their first ones, differing only in the vertex type. */
#define IDirect3DDevice2_STATE_METHODS(T, VERTEXTYPE)                                                                  \
    HRESULT(STDMETHODCALLTYPE *Begin)(T * This, D3DPRIMITIVETYPE type, VERTEXTYPE vertex_type, DWORD flags);           \
    HRESULT(STDMETHODCALLTYPE *BeginIndexed)(T * This, D3DPRIMITIVETYPE type, VERTEXTYPE vertex_type,                  \
                                             LPVOID vertices, DWORD count, DWORD flags);                               \
    HRESULT(STDMETHODCALLTYPE *Vertex)(T * This, LPVOID vertex);                                                       \
    HRESULT(STDMETHODCALLTYPE *Index)(T * This, WORD index);                                                           \
    HRESULT(STDMETHODCALLTYPE *End)(T * This, DWORD flags);                                                            \
    HRESULT(STDMETHODCALLTYPE *GetRenderState)(T * This, D3DRENDERSTATETYPE state, LPDWORD value);                     \
    HRESULT(STDMETHODCALLTYPE *SetRenderState)(T * This, D3DRENDERSTATETYPE state, DWORD value);                       \
    HRESULT(STDMETHODCALLTYPE *GetLightState)(T * This, D3DLIGHTSTATETYPE state, LPDWORD value);                       \
    HRESULT(STDMETHODCALLTYPE *SetLightState)(T * This, D3DLIGHTSTATETYPE state, DWORD value);                         \
    HRESULT(STDMETHODCALLTYPE *SetTransform)(T * This, D3DTRANSFORMSTATETYPE state, LPD3DMATRIX matrix);               \
    HRESULT(STDMETHODCALLTYPE *GetTransform)(T * This, D3DTRANSFORMSTATETYPE state, LPD3DMATRIX matrix);               \
    HRESULT(STDMETHODCALLTYPE *MultiplyTransform)(T * This, D3DTRANSFORMSTATETYPE state, LPD3DMATRIX matrix);          \
    HRESULT(STDMETHODCALLTYPE *DrawPrimitive)(T * This, D3DPRIMITIVETYPE type, VERTEXTYPE vertex_type,                 \
                                              LPVOID vertices, DWORD count, DWORD flags);                              \
    HRESULT(STDMETHODCALLTYPE *DrawIndexedPrimitive)(T * This, D3DPRIMITIVETYPE type, VERTEXTYPE vertex_type,          \
                                                     LPVOID vertices, DWORD vertex_count, LPWORD indices,              \
                                                     DWORD index_count, DWORD flags);                                  \
    HRESULT(STDMETHODCALLTYPE *SetClipStatus)(T * This, LPD3DCLIPSTATUS status);                                       \
    HRESULT(STDMETHODCALLTYPE *GetClipStatus)(T * This, LPD3DCLIPSTATUS status);

#define IDirect3DDevice2_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetCaps)(T * This, LPD3DDEVICEDESC hal, LPD3DDEVICEDESC hel);                           \
    HRESULT(STDMETHODCALLTYPE *SwapTextureHandles)(T * This, LPDIRECT3DTEXTURE2 texture1,                              \
                                                   LPDIRECT3DTEXTURE2 texture2);                                       \
    HRESULT(STDMETHODCALLTYPE *GetStats)(T * This, LPD3DSTATS stats);                                                  \
    HRESULT(STDMETHODCALLTYPE *AddViewport)(T * This, LPDIRECT3DVIEWPORT2 viewport);                                   \
    HRESULT(STDMETHODCALLTYPE *DeleteViewport)(T * This, LPDIRECT3DVIEWPORT2 viewport);                                \
    HRESULT(STDMETHODCALLTYPE *NextViewport)(T * This, LPDIRECT3DVIEWPORT2 viewport, LPDIRECT3DVIEWPORT2 * next,       \
                                             DWORD flags);                                                             \
    HRESULT(STDMETHODCALLTYPE *EnumTextureFormats)(T * This, LPD3DENUMTEXTUREFORMATSCALLBACK callback,                 \
                                                   LPVOID context);                                                    \
    HRESULT(STDMETHODCALLTYPE *BeginScene)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *EndScene)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetDirect3D)(T * This, LPDIRECT3D2 * d3d);                                              \
    HRESULT(STDMETHODCALLTYPE *SetCurrentViewport)(T * This, LPDIRECT3DVIEWPORT2 viewport);                            \
    HRESULT(STDMETHODCALLTYPE *GetCurrentViewport)(T * This, LPDIRECT3DVIEWPORT2 * viewport);                          \
    HRESULT(STDMETHODCALLTYPE *SetRenderTarget)(T * This, LPDIRECTDRAWSURFACE surface, DWORD flags);                   \
    HRESULT(STDMETHODCALLTYPE *GetRenderTarget)(T * This, LPDIRECTDRAWSURFACE * surface);                              \
    IDirect3DDevice2_STATE_METHODS(T, D3DVERTEXTYPE)

#define IDirect3DDevice3_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetCaps)(T * This, LPD3DDEVICEDESC hal, LPD3DDEVICEDESC hel);                           \
    HRESULT(STDMETHODCALLTYPE *GetStats)(T * This, LPD3DSTATS stats);                                                  \
    HRESULT(STDMETHODCALLTYPE *AddViewport)(T * This, LPDIRECT3DVIEWPORT3 viewport);                                   \
    HRESULT(STDMETHODCALLTYPE *DeleteViewport)(T * This, LPDIRECT3DVIEWPORT3 viewport);                                \
    HRESULT(STDMETHODCALLTYPE *NextViewport)(T * This, LPDIRECT3DVIEWPORT3 viewport, LPDIRECT3DVIEWPORT3 * next,       \
                                             DWORD flags);                                                             \
    HRESULT(STDMETHODCALLTYPE *EnumTextureFormats)(T * This, LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context);  \
    HRESULT(STDMETHODCALLTYPE *BeginScene)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *EndScene)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetDirect3D)(T * This, LPDIRECT3D3 * d3d);                                              \
    HRESULT(STDMETHODCALLTYPE *SetCurrentViewport)(T * This, LPDIRECT3DVIEWPORT3 viewport);                            \
    HRESULT(STDMETHODCALLTYPE *GetCurrentViewport)(T * This, LPDIRECT3DVIEWPORT3 * viewport);                          \
    HRESULT(STDMETHODCALLTYPE *SetRenderTarget)(T * This, LPDIRECTDRAWSURFACE4 surface, DWORD flags);                  \
    HRESULT(STDMETHODCALLTYPE *GetRenderTarget)(T * This, LPDIRECTDRAWSURFACE4 * surface);                             \
    IDirect3DDevice2_STATE_METHODS(T, DWORD)                                                                           \
    HRESULT(STDMETHODCALLTYPE *DrawPrimitiveStrided)(T * This, D3DPRIMITIVETYPE type, DWORD fvf,                       \
                                                     LPD3DDRAWPRIMITIVESTRIDEDDATA data, DWORD count, DWORD flags);    \
    HRESULT(STDMETHODCALLTYPE *DrawIndexedPrimitiveStrided)(T * This, D3DPRIMITIVETYPE type, DWORD fvf,                \
                                                            LPD3DDRAWPRIMITIVESTRIDEDDATA data, DWORD vertex_count,    \
                                                            LPWORD indices, DWORD index_count, DWORD flags);           \
    HRESULT(STDMETHODCALLTYPE *DrawPrimitiveVB)(T * This, D3DPRIMITIVETYPE type, LPDIRECT3DVERTEXBUFFER buffer,        \
                                                DWORD start, DWORD count, DWORD flags);                                \
    HRESULT(STDMETHODCALLTYPE *DrawIndexedPrimitiveVB)(T * This, D3DPRIMITIVETYPE type,                                \
                                                       LPDIRECT3DVERTEXBUFFER buffer, LPWORD indices,                  \
                                                       DWORD index_count, DWORD flags);                                \
    HRESULT(STDMETHODCALLTYPE *ComputeSphereVisibility)(T * This, LPD3DVECTOR centers, LPD3DVALUE radii,               \
                                                        DWORD count, DWORD flags, LPDWORD results);                    \
    HRESULT(STDMETHODCALLTYPE *GetTexture)(T * This, DWORD stage, LPDIRECT3DTEXTURE2 * texture);                       \
    HRESULT(STDMETHODCALLTYPE *SetTexture)(T * This, DWORD stage, LPDIRECT3DTEXTURE2 texture);                         \
    HRESULT(STDMETHODCALLTYPE *GetTextureStageState)(T * This, DWORD stage, D3DTEXTURESTAGESTATETYPE state,            \
                                                     LPDWORD value);                                                   \
    HRESULT(STDMETHODCALLTYPE *SetTextureStageState)(T * This, DWORD stage, D3DTEXTURESTAGESTATETYPE state,            \
                                                     DWORD value);                                                     \
    HRESULT(STDMETHODCALLTYPE *ValidateDevice)(T * This, LPDWORD passes);

#define IDirect3DDevice7_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetCaps)(T * This, LPD3DDEVICEDESC7 desc);                                              \
    HRESULT(STDMETHODCALLTYPE *EnumTextureFormats)(T * This, LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context);  \
    HRESULT(STDMETHODCALLTYPE *BeginScene)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *EndScene)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetDirect3D)(T * This, LPDIRECT3D7 * d3d);                                              \
    HRESULT(STDMETHODCALLTYPE *SetRenderTarget)(T * This, LPDIRECTDRAWSURFACE7 surface, DWORD flags);                  \
    HRESULT(STDMETHODCALLTYPE *GetRenderTarget)(T * This, LPDIRECTDRAWSURFACE7 * surface);                             \
    HRESULT(STDMETHODCALLTYPE *Clear)(T * This, DWORD count, LPD3DRECT rects, DWORD flags, D3DCOLOR color,             \
                                      D3DVALUE z, DWORD stencil);                                                      \
    HRESULT(STDMETHODCALLTYPE *SetTransform)(T * This, D3DTRANSFORMSTATETYPE state, LPD3DMATRIX matrix);               \
    HRESULT(STDMETHODCALLTYPE *GetTransform)(T * This, D3DTRANSFORMSTATETYPE state, LPD3DMATRIX matrix);               \
    HRESULT(STDMETHODCALLTYPE *SetViewport)(T * This, LPD3DVIEWPORT7 viewport);                                        \
    HRESULT(STDMETHODCALLTYPE *MultiplyTransform)(T * This, D3DTRANSFORMSTATETYPE state, LPD3DMATRIX matrix);          \
    HRESULT(STDMETHODCALLTYPE *GetViewport)(T * This, LPD3DVIEWPORT7 viewport);                                        \
    HRESULT(STDMETHODCALLTYPE *SetMaterial)(T * This, LPD3DMATERIAL7 material);                                        \
    HRESULT(STDMETHODCALLTYPE *GetMaterial)(T * This, LPD3DMATERIAL7 material);                                        \
    HRESULT(STDMETHODCALLTYPE *SetLight)(T * This, DWORD index, LPD3DLIGHT7 light);                                    \
    HRESULT(STDMETHODCALLTYPE *GetLight)(T * This, DWORD index, LPD3DLIGHT7 light);                                    \
    HRESULT(STDMETHODCALLTYPE *SetRenderState)(T * This, D3DRENDERSTATETYPE state, DWORD value);                       \
    HRESULT(STDMETHODCALLTYPE *GetRenderState)(T * This, D3DRENDERSTATETYPE state, LPDWORD value);                     \
    HRESULT(STDMETHODCALLTYPE *BeginStateBlock)(T * This);                                                             \
    HRESULT(STDMETHODCALLTYPE *EndStateBlock)(T * This, LPDWORD block);                                                \
    HRESULT(STDMETHODCALLTYPE *PreLoad)(T * This, LPDIRECTDRAWSURFACE7 texture);                                       \
    HRESULT(STDMETHODCALLTYPE *DrawPrimitive)(T * This, D3DPRIMITIVETYPE type, DWORD fvf, LPVOID vertices,             \
                                              DWORD count, DWORD flags);                                               \
    HRESULT(STDMETHODCALLTYPE *DrawIndexedPrimitive)(T * This, D3DPRIMITIVETYPE type, DWORD fvf, LPVOID vertices,      \
                                                     DWORD vertex_count, LPWORD indices, DWORD index_count,            \
                                                     DWORD flags);                                                     \
    HRESULT(STDMETHODCALLTYPE *SetClipStatus)(T * This, LPD3DCLIPSTATUS status);                                       \
    HRESULT(STDMETHODCALLTYPE *GetClipStatus)(T * This, LPD3DCLIPSTATUS status);                                       \
    HRESULT(STDMETHODCALLTYPE *DrawPrimitiveStrided)(T * This, D3DPRIMITIVETYPE type, DWORD fvf,                       \
                                                     LPD3DDRAWPRIMITIVESTRIDEDDATA data, DWORD count, DWORD flags);    \
    HRESULT(STDMETHODCALLTYPE *DrawIndexedPrimitiveStrided)(T * This, D3DPRIMITIVETYPE type, DWORD fvf,                \
                                                            LPD3DDRAWPRIMITIVESTRIDEDDATA data, DWORD vertex_count,    \
                                                            LPWORD indices, DWORD index_count, DWORD flags);           \
    HRESULT(STDMETHODCALLTYPE *DrawPrimitiveVB)(T * This, D3DPRIMITIVETYPE type, LPDIRECT3DVERTEXBUFFER7 buffer,       \
                                                DWORD start, DWORD count, DWORD flags);                                \
    HRESULT(STDMETHODCALLTYPE *DrawIndexedPrimitiveVB)(T * This, D3DPRIMITIVETYPE type,                                \
                                                       LPDIRECT3DVERTEXBUFFER7 buffer, DWORD start,                    \
                                                       DWORD vertex_count, LPWORD indices, DWORD index_count,          \
                                                       DWORD flags);                                                   \
    HRESULT(STDMETHODCALLTYPE *ComputeSphereVisibility)(T * This, LPD3DVECTOR centers, LPD3DVALUE radii,               \
                                                        DWORD count, DWORD flags, LPDWORD results);                    \
    HRESULT(STDMETHODCALLTYPE *GetTexture)(T * This, DWORD stage, LPDIRECTDRAWSURFACE7 * texture);                     \
    HRESULT(STDMETHODCALLTYPE *SetTexture)(T * This, DWORD stage, LPDIRECTDRAWSURFACE7 texture);                       \
    HRESULT(STDMETHODCALLTYPE *GetTextureStageState)(T * This, DWORD stage, D3DTEXTURESTAGESTATETYPE state,            \
                                                     LPDWORD value);                                                   \
    HRESULT(STDMETHODCALLTYPE *SetTextureStageState)(T * This, DWORD stage, D3DTEXTURESTAGESTATETYPE state,            \
                                                     DWORD value);                                                     \
    HRESULT(STDMETHODCALLTYPE *ValidateDevice)(T * This, LPDWORD passes);                                              \
    HRESULT(STDMETHODCALLTYPE *ApplyStateBlock)(T * This, DWORD block);                                                \
    HRESULT(STDMETHODCALLTYPE *CaptureStateBlock)(T * This, DWORD block);                                              \
    HRESULT(STDMETHODCALLTYPE *DeleteStateBlock)(T * This, DWORD block);                                               \
    HRESULT(STDMETHODCALLTYPE *CreateStateBlock)(T * This, D3DSTATEBLOCKTYPE type, LPDWORD block);                     \
    HRESULT(STDMETHODCALLTYPE *Load)(T * This, LPDIRECTDRAWSURFACE7 dst, LPPOINT dst_point, LPDIRECTDRAWSURFACE7 src,  \
                                     LPRECT src_rect, DWORD flags);                                                    \
    HRESULT(STDMETHODCALLTYPE *LightEnable)(T * This, DWORD index, BOOL enable);                                       \
    HRESULT(STDMETHODCALLTYPE *GetLightEnable)(T * This, DWORD index, BOOL * enable);                                  \
    HRESULT(STDMETHODCALLTYPE *SetClipPlane)(T * This, DWORD index, D3DVALUE * plane);                                 \
    HRESULT(STDMETHODCALLTYPE *GetClipPlane)(T * This, DWORD index, D3DVALUE * plane);                                 \
    HRESULT(STDMETHODCALLTYPE *GetInfo)(T * This, DWORD id, LPVOID info, DWORD size);

/* ------------------------------------------------------------------ other objects */

#define IDirect3DExecuteBuffer_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, LPDIRECT3DDEVICE device, LPD3DEXECUTEBUFFERDESC desc);            \
    HRESULT(STDMETHODCALLTYPE *Lock)(T * This, LPD3DEXECUTEBUFFERDESC desc);                                           \
    HRESULT(STDMETHODCALLTYPE *Unlock)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *SetExecuteData)(T * This, LPD3DEXECUTEDATA data);                                       \
    HRESULT(STDMETHODCALLTYPE *GetExecuteData)(T * This, LPD3DEXECUTEDATA data);                                       \
    HRESULT(STDMETHODCALLTYPE *Validate)(T * This, LPDWORD offset, LPD3DVALIDATECALLBACK callback, LPVOID context,     \
                                         DWORD reserved);                                                              \
    HRESULT(STDMETHODCALLTYPE *Optimize)(T * This, DWORD reserved);

#define IDirect3DLight_METHODS(T)                                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, LPDIRECT3D d3d);                                                  \
    HRESULT(STDMETHODCALLTYPE *SetLight)(T * This, LPD3DLIGHT light);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetLight)(T * This, LPD3DLIGHT light);

#define IDirect3DMaterial_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, LPDIRECT3D d3d);                                                  \
    HRESULT(STDMETHODCALLTYPE *SetMaterial)(T * This, LPD3DMATERIAL material);                                         \
    HRESULT(STDMETHODCALLTYPE *GetMaterial)(T * This, LPD3DMATERIAL material);                                         \
    HRESULT(STDMETHODCALLTYPE *GetHandle)(T * This, LPDIRECT3DDEVICE device, LPD3DMATERIALHANDLE handle);              \
    HRESULT(STDMETHODCALLTYPE *Reserve)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *Unreserve)(T * This);

#define IDirect3DMaterial2_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetMaterial)(T * This, LPD3DMATERIAL material);                                         \
    HRESULT(STDMETHODCALLTYPE *GetMaterial)(T * This, LPD3DMATERIAL material);                                         \
    HRESULT(STDMETHODCALLTYPE *GetHandle)(T * This, LPDIRECT3DDEVICE2 device, LPD3DMATERIALHANDLE handle);

#define IDirect3DMaterial3_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetMaterial)(T * This, LPD3DMATERIAL material);                                         \
    HRESULT(STDMETHODCALLTYPE *GetMaterial)(T * This, LPD3DMATERIAL material);                                         \
    HRESULT(STDMETHODCALLTYPE *GetHandle)(T * This, LPDIRECT3DDEVICE3 device, LPD3DMATERIALHANDLE handle);

#define IDirect3DTexture_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, LPDIRECT3DDEVICE device, LPDIRECTDRAWSURFACE surface);            \
    HRESULT(STDMETHODCALLTYPE *GetHandle)(T * This, LPDIRECT3DDEVICE device, LPD3DTEXTUREHANDLE handle);               \
    HRESULT(STDMETHODCALLTYPE *PaletteChanged)(T * This, DWORD start, DWORD count);                                    \
    HRESULT(STDMETHODCALLTYPE *Load)(T * This, LPDIRECT3DTEXTURE source);                                              \
    HRESULT(STDMETHODCALLTYPE *Unload)(T * This);

#define IDirect3DTexture2_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetHandle)(T * This, LPDIRECT3DDEVICE2 device, LPD3DTEXTUREHANDLE handle);              \
    HRESULT(STDMETHODCALLTYPE *PaletteChanged)(T * This, DWORD start, DWORD count);                                    \
    HRESULT(STDMETHODCALLTYPE *Load)(T * This, LPDIRECT3DTEXTURE2 source);

#define IDirect3DViewport_FIRST_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, LPDIRECT3D d3d);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetViewport)(T * This, LPD3DVIEWPORT data);                                             \
    HRESULT(STDMETHODCALLTYPE *SetViewport)(T * This, LPD3DVIEWPORT data);                                             \
    HRESULT(STDMETHODCALLTYPE *TransformVertices)(T * This, DWORD count, LPD3DTRANSFORMDATA data, DWORD flags,         \
                                                  LPDWORD offscreen);                                                  \
    HRESULT(STDMETHODCALLTYPE *LightElements)(T * This, DWORD count, LPD3DLIGHTDATA data);                             \
    HRESULT(STDMETHODCALLTYPE *SetBackground)(T * This, D3DMATERIALHANDLE material);                                   \
    HRESULT(STDMETHODCALLTYPE *GetBackground)(T * This, LPD3DMATERIALHANDLE material, LPBOOL valid);                   \
    HRESULT(STDMETHODCALLTYPE *SetBackgroundDepth)(T * This, LPDIRECTDRAWSURFACE surface);                             \
    HRESULT(STDMETHODCALLTYPE *GetBackgroundDepth)(T * This, LPDIRECTDRAWSURFACE * surface, LPBOOL valid);             \
    HRESULT(STDMETHODCALLTYPE *Clear)(T * This, DWORD count, LPD3DRECT rects, DWORD flags);                            \
    HRESULT(STDMETHODCALLTYPE *AddLight)(T * This, LPDIRECT3DLIGHT light);                                             \
    HRESULT(STDMETHODCALLTYPE *DeleteLight)(T * This, LPDIRECT3DLIGHT light);                                          \
    HRESULT(STDMETHODCALLTYPE *NextLight)(T * This, LPDIRECT3DLIGHT light, LPDIRECT3DLIGHT * next, DWORD flags);

#define IDirect3DViewport_METHODS(T) IDirect3DViewport_FIRST_METHODS(T)

#define IDirect3DViewport2_METHODS(T)                                                                                  \
    IDirect3DViewport_FIRST_METHODS(T)                                                                                 \
    HRESULT(STDMETHODCALLTYPE *GetViewport2)(T * This, LPD3DVIEWPORT2 data);                                           \
    HRESULT(STDMETHODCALLTYPE *SetViewport2)(T * This, LPD3DVIEWPORT2 data);

#define IDirect3DViewport3_METHODS(T)                                                                                  \
    IDirect3DViewport2_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *SetBackgroundDepth2)(T * This, LPDIRECTDRAWSURFACE4 surface);                           \
    HRESULT(STDMETHODCALLTYPE *GetBackgroundDepth2)(T * This, LPDIRECTDRAWSURFACE4 * surface, LPBOOL valid);           \
    HRESULT(STDMETHODCALLTYPE *Clear2)(T * This, DWORD count, LPD3DRECT rects, DWORD flags, D3DCOLOR color,            \
                                       D3DVALUE z, DWORD stencil);

/* ------------------------------------------------------------------ vertex buffers */

#define IDirect3DVertexBuffer_METHODS(T)                                                                               \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Lock)(T * This, DWORD flags, LPVOID * data, LPDWORD size);                              \
    HRESULT(STDMETHODCALLTYPE *Unlock)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *ProcessVertices)(T * This, DWORD op, DWORD dst_index, DWORD count,                      \
                                                LPDIRECT3DVERTEXBUFFER src, DWORD src_index,                           \
                                                LPDIRECT3DDEVICE3 device, DWORD flags);                                \
    HRESULT(STDMETHODCALLTYPE *GetVertexBufferDesc)(T * This, LPD3DVERTEXBUFFERDESC desc);                             \
    HRESULT(STDMETHODCALLTYPE *Optimize)(T * This, LPDIRECT3DDEVICE3 device, DWORD flags);

#define IDirect3DVertexBuffer7_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Lock)(T * This, DWORD flags, LPVOID * data, LPDWORD size);                              \
    HRESULT(STDMETHODCALLTYPE *Unlock)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *ProcessVertices)(T * This, DWORD op, DWORD dst_index, DWORD count,                      \
                                                LPDIRECT3DVERTEXBUFFER7 src, DWORD src_index,                          \
                                                LPDIRECT3DDEVICE7 device, DWORD flags);                                \
    HRESULT(STDMETHODCALLTYPE *GetVertexBufferDesc)(T * This, LPD3DVERTEXBUFFERDESC desc);                             \
    HRESULT(STDMETHODCALLTYPE *Optimize)(T * This, LPDIRECT3DDEVICE7 device, DWORD flags);                             \
    HRESULT(STDMETHODCALLTYPE *ProcessVerticesStrided)(T * This, DWORD op, DWORD dst_index, DWORD count,               \
                                                       LPD3DDRAWPRIMITIVESTRIDEDDATA data, DWORD fvf,                  \
                                                       LPDIRECT3DDEVICE7 device, DWORD flags);

MIME_COM_INTERFACE(IDirect3D)
MIME_COM_INTERFACE(IDirect3D2)
MIME_COM_INTERFACE(IDirect3D3)
MIME_COM_INTERFACE(IDirect3D7)
MIME_COM_INTERFACE(IDirect3DDevice)
MIME_COM_INTERFACE(IDirect3DDevice2)
MIME_COM_INTERFACE(IDirect3DDevice3)
MIME_COM_INTERFACE(IDirect3DDevice7)
MIME_COM_INTERFACE(IDirect3DExecuteBuffer)
MIME_COM_INTERFACE(IDirect3DLight)
MIME_COM_INTERFACE(IDirect3DMaterial)
MIME_COM_INTERFACE(IDirect3DMaterial2)
MIME_COM_INTERFACE(IDirect3DMaterial3)
MIME_COM_INTERFACE(IDirect3DTexture)
MIME_COM_INTERFACE(IDirect3DTexture2)
MIME_COM_INTERFACE(IDirect3DViewport)
MIME_COM_INTERFACE(IDirect3DViewport2)
MIME_COM_INTERFACE(IDirect3DViewport3)
MIME_COM_INTERFACE(IDirect3DVertexBuffer)
MIME_COM_INTERFACE(IDirect3DVertexBuffer7)

/* ------------------------------------------------------------------ guids */

MIME_DEFINE_GUID(IID_IDirect3D, 0x3BBA0080, 0x2421, 0x11CF, 0xA3, 0x1A, 0x00, 0xAA, 0x00, 0xB9, 0x33, 0x56);
MIME_DEFINE_GUID(IID_IDirect3D2, 0x6AAE1EC1, 0x662A, 0x11D0, 0x88, 0x9D, 0x00, 0xAA, 0x00, 0xBB, 0xB7, 0x6A);
MIME_DEFINE_GUID(IID_IDirect3D3, 0xBB223240, 0xE72B, 0x11D0, 0xA9, 0xB4, 0x00, 0xAA, 0x00, 0xC0, 0x99, 0x3E);
MIME_DEFINE_GUID(IID_IDirect3D7, 0xF5049E77, 0x4861, 0x11D2, 0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8);
MIME_DEFINE_GUID(IID_IDirect3DRampDevice, 0xF2086B20, 0x259F, 0x11CF, 0xA3, 0x1A, 0x00, 0xAA, 0x00, 0xB9, 0x33, 0x56);
MIME_DEFINE_GUID(IID_IDirect3DRGBDevice, 0xA4665C60, 0x2673, 0x11CF, 0xA3, 0x1A, 0x00, 0xAA, 0x00, 0xB9, 0x33, 0x56);
MIME_DEFINE_GUID(IID_IDirect3DHALDevice, 0x84E63DE0, 0x46AA, 0x11CF, 0x81, 0x6F, 0x00, 0x00, 0xC0, 0x20, 0x15, 0x6E);
MIME_DEFINE_GUID(IID_IDirect3DMMXDevice, 0x881949A1, 0xD6F3, 0x11D0, 0x89, 0xAB, 0x00, 0xA0, 0xC9, 0x05, 0x41, 0x29);
MIME_DEFINE_GUID(IID_IDirect3DRefDevice, 0x50936643, 0x13E9, 0x11D1, 0x89, 0xAA, 0x00, 0xA0, 0xC9, 0x05, 0x41, 0x29);
MIME_DEFINE_GUID(IID_IDirect3DNullDevice, 0x8767DF22, 0xBACC, 0x11D1, 0x89, 0x69, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8);
MIME_DEFINE_GUID(IID_IDirect3DTnLHalDevice, 0xF5049E78, 0x4861, 0x11D2, 0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8);
MIME_DEFINE_GUID(IID_IDirect3DDevice, 0x64108800, 0x957D, 0x11D0, 0x89, 0xAB, 0x00, 0xA0, 0xC9, 0x05, 0x41, 0x29);
MIME_DEFINE_GUID(IID_IDirect3DDevice2, 0x93281501, 0x8CF8, 0x11D0, 0x89, 0xAB, 0x00, 0xA0, 0xC9, 0x05, 0x41, 0x29);
MIME_DEFINE_GUID(IID_IDirect3DDevice3, 0xB0AB3B60, 0x33D7, 0x11D1, 0xA9, 0x81, 0x00, 0xC0, 0x4F, 0xD7, 0xB1, 0x74);
MIME_DEFINE_GUID(IID_IDirect3DDevice7, 0xF5049E79, 0x4861, 0x11D2, 0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8);
MIME_DEFINE_GUID(IID_IDirect3DTexture, 0x2CDCD9E0, 0x25A0, 0x11CF, 0xA3, 0x1A, 0x00, 0xAA, 0x00, 0xB9, 0x33, 0x56);
MIME_DEFINE_GUID(IID_IDirect3DTexture2, 0x93281502, 0x8CF8, 0x11D0, 0x89, 0xAB, 0x00, 0xA0, 0xC9, 0x05, 0x41, 0x29);
MIME_DEFINE_GUID(IID_IDirect3DLight, 0x4417C142, 0x33AD, 0x11CF, 0x81, 0x6F, 0x00, 0x00, 0xC0, 0x20, 0x15, 0x6E);
MIME_DEFINE_GUID(IID_IDirect3DMaterial, 0x4417C144, 0x33AD, 0x11CF, 0x81, 0x6F, 0x00, 0x00, 0xC0, 0x20, 0x15, 0x6E);
MIME_DEFINE_GUID(IID_IDirect3DMaterial2, 0x93281503, 0x8CF8, 0x11D0, 0x89, 0xAB, 0x00, 0xA0, 0xC9, 0x05, 0x41, 0x29);
MIME_DEFINE_GUID(IID_IDirect3DMaterial3, 0xCA9C46F4, 0xD3C5, 0x11D1, 0xB7, 0x5A, 0x00, 0x60, 0x08, 0x52, 0xB3, 0x12);
MIME_DEFINE_GUID(IID_IDirect3DExecuteBuffer, 0x4417C145, 0x33AD, 0x11CF, 0x81, 0x6F, 0x00, 0x00, 0xC0, 0x20, 0x15,
                 0x6E);
MIME_DEFINE_GUID(IID_IDirect3DViewport, 0x4417C146, 0x33AD, 0x11CF, 0x81, 0x6F, 0x00, 0x00, 0xC0, 0x20, 0x15, 0x6E);
MIME_DEFINE_GUID(IID_IDirect3DViewport2, 0x93281500, 0x8CF8, 0x11D0, 0x89, 0xAB, 0x00, 0xA0, 0xC9, 0x05, 0x41, 0x29);
MIME_DEFINE_GUID(IID_IDirect3DViewport3, 0xB0AB3B61, 0x33D7, 0x11D1, 0xA9, 0x81, 0x00, 0xC0, 0x4F, 0xD7, 0xB1, 0x74);
MIME_DEFINE_GUID(IID_IDirect3DVertexBuffer, 0x7A503555, 0x4A83, 0x11D1, 0xA5, 0xDB, 0x00, 0xA0, 0xC9, 0x03, 0x67,
                 0xF8);
MIME_DEFINE_GUID(IID_IDirect3DVertexBuffer7, 0xF5049E7D, 0x4861, 0x11D2, 0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29,
                 0xA8);

#endif
