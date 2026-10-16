/*
 * d3d9.dll: direct3d 9 over mime's own direct3d 11. a device is a d3d11
 * device and its immediate context; surfaces, textures and buffers are
 * d3d11 resources, with a copy in memory where direct3d 9 lets programs
 * lock them, which unlocking uploads in the order of the gpu's work, so a
 * lock never waits. direct3d 9's shaders and fixed function state become
 * dxbc, which d3d11 compiles as it does its own.
 */
#ifndef D3D9_PRIVATE_H
#define D3D9_PRIVATE_H

#define MIME_BUILD_D3D9
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/winspool.h>
#include <mime/win32/d3d9.h>
#include <mime/win32/d3d11.h>
#include <mime/d3d11_mime.h>
#include "../lib/privdata/privdata.h"

#define D9_MAX(a, b) ((a) > (b) ? (a) : (b))
#define D9_MIN(a, b) ((a) < (b) ? (a) : (b))
#define D9_ALIGN(v, a) (((v) + (a)-1) & ~((a)-1))

void D9Log(const char *format, ...);

/* ------------------------------------------------------------------ formats */

#define D9_FORMAT_DEPTH 0x01
#define D9_FORMAT_STENCIL 0x02
#define D9_FORMAT_RENDER 0x04
#define D9_FORMAT_SRGB 0x08
#define D9_FORMAT_FLOAT 0x10
/* luminance with alpha in four bits each, stored as two bytes. */
#define D9_FORMAT_A4L4 0x20
#define D9_FORMAT_LOCKABLE 0x40
#define D9_FORMAT_NULL 0x80

typedef struct D9Format {
    D3DFORMAT d3d;
    /* the resource's format, typeless where views read it in two ways. */
    DXGI_FORMAT resource;
    DXGI_FORMAT view;
    DXGI_FORMAT srgb;
    DXGI_FORMAT depth;
    /* how shaders see the view's components, as MIME_SWIZZLE has them, 0 for as they are. */
    WORD swizzle;
    /* bytes of a block of texels, the block's size, and the bytes the gpu keeps a block in. */
    BYTE bytes;
    BYTE block;
    BYTE stored;
    BYTE flags;
} D9Format;

const D9Format *D9GetFormat(D3DFORMAT format);
UINT D9FormatPitch(const D9Format *f, UINT width);
UINT D9FormatRows(const D9Format *f, UINT height);
/* the bytes of a level's texels as programs lock them, converted to the gpu's and back. */
void D9ConvertUpload(const D9Format *f, const BYTE *in, UINT in_pitch, BYTE *out, UINT out_pitch, UINT width,
                     UINT rows);

/* ------------------------------------------------------------------ objects */

typedef struct D9Device D9Device;
typedef struct D9Surface D9Surface;
typedef struct D9Texture D9Texture;
typedef struct D9SwapChain D9SwapChain;

/*
 * what every object has: its device, references and private data. what
 * programs hold keeps the device alive; what the device binds keeps only
 * the object, so bound objects never keep their device.
 */
typedef struct D9Resource D9Resource;
struct D9Resource {
    LONG refs;
    LONG binds;
    /* back buffers and the automatic depth buffer, which live as long as their device. */
    BOOL implicit;
    void (*destroy)(D9Resource *r);
    D9Device *device;
    MimePrivateData data;
    D3DRESOURCETYPE type;
    D3DPOOL pool;
    DWORD usage;
    DWORD priority;
    const D9Format *format;
    D3DFORMAT d3d_format;
};

/*
 * a subresource programs may lock: its bytes in memory as direct3d 9 lays
 * them out, and what a lock covers.
 */
typedef struct D9Memory {
    BYTE *bytes;
    UINT pitch;
    UINT slice;
    UINT size;
    BOOL locked;
    DWORD lock_flags;
    RECT lock_rect;
    UINT lock_front, lock_back;
} D9Memory;

/* a 2d image: a level or face of a texture, or a surface of its own. */
struct D9Surface {
    IDirect3DSurface9 iface;
    D9Resource base;
    /* the texture it is a level of, which holds its references, null for surfaces of their own. */
    D9Texture *container;
    D9SwapChain *swapchain;
    ID3D11Texture2D *texture;
    UINT level, face;
    UINT width, height;
    D3DMULTISAMPLE_TYPE samples;
    DWORD quality;
    BOOL lockable;
    /* its bytes, a level of its texture's for levels, and whether they are what the gpu has. */
    D9Memory *memory;
    D9Memory own_memory;
    BOOL memory_valid;
    /* the views it is drawn into, made at the first use. */
    ID3D11RenderTargetView *rtv, *rtv_srgb;
    ID3D11DepthStencilView *dsv;
    HDC dc;
    HBITMAP bitmap;
};

struct D9Texture {
    union {
        IDirect3DBaseTexture9 base;
        IDirect3DTexture9 tex;
        IDirect3DCubeTexture9 cube;
        IDirect3DVolumeTexture9 volume;
    } iface;
    D9Resource base;
    ID3D11Resource *resource;
    UINT width, height, depth, levels, faces;
    DWORD lod;
    D3DTEXTUREFILTERTYPE autogen_filter;
    BOOL autogen_dirty;
    D9Surface **surfaces;
    D9Memory *memory;
    ID3D11ShaderResourceView *srv, *srv_srgb;
};

/* ------------------------------------------------------------------ the swap chain */

struct D9SwapChain {
    IDirect3DSwapChain9Ex iface;
    LONG refs;
    D9Device *device;
    D3DPRESENT_PARAMETERS params;
    HWND window;
    IDXGISwapChain *dxgi;
    ID3D11Texture2D *dxgi_buffer;
    /* the device's own, which lives as long as it does. */
    BOOL implicit;
    BOOL warned_rects;
    /* what is rendered to, copied to dxgi's buffer when presented. */
    D9Surface *back_buffers[D3DPRESENT_BACK_BUFFERS_MAX_EX];
    UINT back_buffer_count;
    UINT present_count;
    D3DGAMMARAMP gamma;
};

HRESULT D9CreateSwapChain(D9Device *device, D3DPRESENT_PARAMETERS *params, D9SwapChain **out);
HRESULT D9ResetSwapChain(D9SwapChain *swapchain, D3DPRESENT_PARAMETERS *params);
void D9DestroySwapChain(D9SwapChain *swapchain);
HRESULT D9Present(D9SwapChain *swapchain, const RECT *source, const RECT *dest, HWND window, DWORD flags);
BOOL D9SwapChainInUse(const D9SwapChain *swapchain);
HRESULT D9RasterStatus(D9SwapChain *swapchain, D3DRASTER_STATUS *status);
/* takes an additional swap chain out of its device's list. */
void D9ForgetSwapChain(D9Device *device, D9SwapChain *swapchain);

/* ------------------------------------------------------------------ the device */

#define D9_MAX_STAGES 8
#define D9_MAX_SAMPLERS 21
#define D9_MAX_STREAMS 16
#define D9_MAX_TARGETS 4
#define D9_VS_CONSTANTS 256
#define D9_PS_CONSTANTS 224

typedef struct D9Light {
    DWORD index;
    BOOL enabled;
    D3DLIGHT9 light;
} D9Light;

struct D9Device {
    IDirect3DDevice9Ex iface;
    LONG refs;
    IDirect3D9Ex *d3d;
    BOOL ex;
    D3DDEVICE_CREATION_PARAMETERS creation;
    CRITICAL_SECTION lock;
    BOOL multithreaded;

    ID3D11Device1 *d3d11;
    ID3D11DeviceContext1 *context;
    IMimeD3D11Device *mime;
    IDXGIFactory *factory;

    D9SwapChain **swapchains;
    UINT swapchain_count;
    BOOL in_scene;

    D9Surface *targets[D9_MAX_TARGETS];
    D9Surface *depth_stencil;
    D9Surface *auto_depth;
    D3DVIEWPORT9 viewport;
    RECT scissor;
    DWORD render_states[256];
    DWORD stage_states[D9_MAX_STAGES][33];
    DWORD sampler_states[D9_MAX_SAMPLERS][14];
    D3DMATRIX transforms[512];
    D3DMATERIAL9 material;
    D9Light *lights;
    UINT light_count;
    float clip_planes[6][4];
    D3DCLIPSTATUS9 clip_status;
    DWORD fvf;
    UINT stream_freq[D9_MAX_STREAMS];
    float vs_f[D9_VS_CONSTANTS][4];
    int vs_i[16][4];
    BOOL vs_b[16];
    float ps_f[D9_PS_CONSTANTS][4];
    int ps_i[16][4];
    BOOL ps_b[16];
    /* what changed since the last draw. */
    BOOL targets_dirty, viewport_dirty, states_dirty, ff_dirty, vs_consts_dirty, ps_consts_dirty;
    UINT samplers_dirty;
    HCURSOR cursor;
    BOOL cursor_shown;
    BOOL software_vertex_processing;
    float npatch_segments;
    UINT palette;
    UINT max_frame_latency;
    INT gpu_priority;
};

D9Device *D9DeviceFrom(IDirect3DDevice9 *iface);
HRESULT D9CreateDevice(IDirect3D9Ex *d3d, BOOL ex, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                       D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *mode, D9Device **out);
void D9Lock(D9Device *device);
void D9Unlock(D9Device *device);
/* the caps of the adapter, filled as for the device. */
void D9FillCaps(UINT adapter, D3DCAPS9 *caps);

/* ------------------------------------------------------------------ resources */

void D9ResourceInit(D9Resource *r, D9Device *device, D3DRESOURCETYPE type, D3DPOOL pool, DWORD usage,
                    D3DFORMAT format, void (*destroy)(D9Resource *r));
void D9ResourceClear(D9Resource *r);
ULONG D9ResourceAddRef(D9Resource *r);
ULONG D9ResourceRelease(D9Resource *r);
/* the device's own references, of what it binds. */
void D9Bind(D9Resource *r);
void D9Unbind(D9Resource *r);
HRESULT D9ResourceGetDevice(D9Resource *r, IDirect3DDevice9 **device);
HRESULT D9ResourceSetPrivateData(D9Resource *r, REFGUID guid, const void *data, DWORD size, DWORD flags);
HRESULT D9ResourceGetPrivateData(D9Resource *r, REFGUID guid, void *data, DWORD *size);
HRESULT D9ResourceFreePrivateData(D9Resource *r, REFGUID guid);
DWORD D9ResourceSetPriority(D9Resource *r, DWORD priority);
BOOL D9LockRect(const D9Format *f, UINT width, UINT height, const RECT *rect, RECT *out);
BYTE *D9MemoryAt(const D9Memory *memory, const D9Format *f, UINT x, UINT y, UINT z);

HRESULT D9CreateSurface(D9Device *device, UINT width, UINT height, D3DFORMAT format, DWORD usage, D3DPOOL pool,
                        D3DMULTISAMPLE_TYPE samples, DWORD quality, BOOL lockable, D9Surface **out);
D9Surface *D9SurfaceFrom(IDirect3DSurface9 *iface);
ULONG D9SurfaceAddRef(D9Surface *surface);
ULONG D9SurfaceRelease(D9Surface *surface);
ID3D11RenderTargetView *D9SurfaceRtv(D9Surface *surface, BOOL srgb);
ID3D11DepthStencilView *D9SurfaceDsv(D9Surface *surface);
/* copies a surface's gpu texels to its memory, and its memory to the gpu. */
HRESULT D9SurfaceDownload(D9Surface *surface);
void D9SurfaceUpload(D9Surface *surface, const RECT *rect);
void D9SurfaceWritten(D9Surface *surface);
UINT D9SurfaceSubresource(const D9Surface *surface);
HRESULT D9SurfaceLock(D9Surface *surface, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags);
HRESULT D9SurfaceUnlock(D9Surface *surface);
/* a surface object without a texture, as levels and back buffers start. */
HRESULT D9NewSurface(D9Device *device, D3DFORMAT format, DWORD usage, D3DPOOL pool, UINT width, UINT height,
                     D9Surface **out);
void D9DestroyImplicitSurface(D9Surface *surface);
HRESULT D9SurfaceMakeTexture(D9Surface *surface);
void D9FreeMemory(D9Memory *memory);
HRESULT D9AllocMemory(D9Memory *memory, const D9Format *f, UINT width, UINT height, UINT depth);

/* ------------------------------------------------------------------ adapters */

UINT D9AdapterCount(void);
/* the dxgi adapter and output of a direct3d 9 adapter, one for each monitor. */
HRESULT D9GetOutput(UINT adapter, IDXGIAdapter1 **out_adapter, IDXGIOutput **out_output);
IDXGIFactory1 *D9Factory(void);

#endif
