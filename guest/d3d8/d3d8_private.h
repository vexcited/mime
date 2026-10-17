/*
 * d3d8.dll: direct3d 8 over mime's direct3d 9. every object wraps its
 * direct3d 9 one, and the device keeps the state as direct3d 8 reports
 * it: its render states with the values windows starts from, lights
 * enabled as 128, viewports adjusted, shaders by handle, state blocks by
 * token. objects the device binds count those bindings apart from their
 * references, as windows' runtime does.
 */
#ifndef D3D8_PRIVATE_H
#define D3D8_PRIVATE_H

#define MIME_BUILD_D3D8
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>
#include <mime/win32/winspool.h>
#include <mime/win32/d3d9.h>
#include <mime/win32/d3d8.h>
#include "../lib/privdata/privdata.h"

#define D8_MAX(a, b) ((a) > (b) ? (a) : (b))
#define D8_MIN(a, b) ((a) < (b) ? (a) : (b))

#define D8_STAGES 8
#define D8_STREAMS 16
#define D8_VS_CONSTANTS 96
#define D8_PS_CONSTANTS 8
#define D8_PLANES 32
#define D8_UNSET 0xBAADCAFE
#define D8_NO_PALETTE 0xFFFF
/* view, projection, the eight texture matrices and the 256 world ones. */
#define D8_TRANSFORMS 266

typedef struct D8Direct3D D8Direct3D;
typedef struct D8Device D8Device;
typedef struct D8Object D8Object;

void D8Log(const char *format, ...);

/* ------------------------------------------------------------------ IDirect3D8 */

typedef struct D8Mode {
    UINT width, height, refresh;
    D3DFORMAT format;
} D8Mode;

typedef struct D8Adapter {
    char device[32];
    D8Mode *modes;
    UINT mode_count;
} D8Adapter;

struct D8Direct3D {
    IDirect3D8 iface;
    LONG refs;
    IDirect3D9 *d9;
    UINT sdk;
    D8Adapter *adapters;
    UINT adapter_count;
    CRITICAL_SECTION cs;
};

D8Direct3D *D8Direct3DFrom(IDirect3D8 *iface);
HRESULT D8CreateDirect3D(UINT sdk, IDirect3D8 **out);
HRESULT D8GetCaps(D8Direct3D *d3d, UINT adapter, D3DDEVTYPE type, D3DCAPS8 *caps);
/* the format of the desktop as direct3d 8 names it. */
D3DFORMAT D8DisplayFormat(D8Direct3D *d3d, UINT adapter);
D3DFORMAT D8NoAlpha(D3DFORMAT format);
/* the type direct3d 9 makes the device with: it knows no reference rasterizer of its own. */
static inline D3DDEVTYPE D8DeviceType9(D3DDEVTYPE type) { return type == D3DDEVTYPE_REF ? D3DDEVTYPE_HAL : type; }
BOOL D8IsDepthFormat(D3DFORMAT format);
BOOL D8HasStencil(D3DFORMAT format);
/* bytes of a texel, or of a 4x4 block for the compressed formats, 0 for those it does not know. */
UINT D8FormatBytes(D3DFORMAT format);
BOOL D8IsDxt(D3DFORMAT format);
UINT D8LevelSize(D3DFORMAT format, UINT width, UINT height, UINT depth);

/* ------------------------------------------------------------------ objects */

enum {
    D8_SURFACE,
    D8_VOLUME,
    D8_TEXTURE,
    D8_CUBE_TEXTURE,
    D8_VOLUME_TEXTURE,
    D8_VERTEX_BUFFER,
    D8_INDEX_BUFFER,
    D8_SWAPCHAIN,
};

/*
 * any object a program gets from the device. its references are the
 * program's, its uses the device's bindings; while it has references it
 * holds one on the device. levels of textures count with their texture.
 */
struct D8Object {
    const void *vtbl;
    LONG refs;
    LONG uses;
    UINT kind;
    D8Device *device;
    D8Object *container;
    IUnknown *d9;
    MimePrivateData data;
    D3DRESOURCETYPE type;
    D3DFORMAT format;
    DWORD usage;
    D3DPOOL pool;
    DWORD priority;
    DWORD lod;
    BOOL lockable;
    /* levels of 2d textures in memory, which lock without checks and unlock when not locked. */
    BOOL lenient;
    LONG locks;
    UINT width, height, depth;
    D3DMULTISAMPLE_TYPE samples;
    /* textures: their levels, six times as many for cubes, face major. */
    UINT levels;
    D8Object **subs;
    UINT level, face;
    /* buffers. */
    UINT size;
    DWORD fvf;
    /* swap chains: their back buffers, and whether presenting them flips. */
    D8Object **buffers;
    UINT buffer_count;
    BOOL flip;
};

#define D8_IFACE(o) ((void *)(o))

ULONG D8ObjectAddRef(D8Object *o);
ULONG D8ObjectRelease(D8Object *o);
void D8ObjectUse(D8Object *o);
void D8ObjectUnuse(D8Object *o);
/* moves a binding from what a slot holds to another object. */
void D8Bind(D8Object **slot, D8Object *o);
D8Object *D8ObjectFrom(void *iface);
/* the object of the program's pointer, if it is of this device and of one of the kinds asked. */
D8Object *D8Check(D8Device *d, void *iface, UINT kinds);
#define D8_KIND(k) (1u << (k))
#define D8_TEXTURES (D8_KIND(D8_TEXTURE) | D8_KIND(D8_CUBE_TEXTURE) | D8_KIND(D8_VOLUME_TEXTURE))

/* wraps a direct3d 9 object, the reference passing to the wrapper. internal ones start used, not referenced. */
HRESULT D8WrapSurface(D8Device *d, IDirect3DSurface9 *s9, DWORD usage, BOOL internal, D8Object **out);
HRESULT D8WrapTexture(D8Device *d, UINT kind, IUnknown *t9, DWORD usage, D3DPOOL pool, D3DFORMAT format,
                      D8Object **out);
HRESULT D8WrapBuffer(D8Device *d, UINT kind, IUnknown *b9, UINT size, DWORD usage, DWORD fvf, D3DPOOL pool,
                     D3DFORMAT format, D8Object **out);
HRESULT D8WrapSwapChain(D8Device *d, IDirect3DSwapChain9 *sc9, const D3DPRESENT_PARAMETERS *params, BOOL internal,
                        D8Object **out);
/* lets go of the back buffers of a swap chain, before a reset makes new ones. */
void D8SwapChainDrop(D8Object *sc);
HRESULT D8SwapChainTake(D8Object *sc, const D3DPRESENT_PARAMETERS *params);
HRESULT D8SurfaceDesc(D8Object *s, D3D8_SURFACE_DESC *desc);
HRESULT D8SwapChainPresent(D8Object *sc, const RECT *source, const RECT *dest, HWND window, const RGNDATA *dirty);
HRESULT D8SwapChainBackBuffer(D8Object *sc, UINT index, IDirect3DSurface8 **out);

/* ------------------------------------------------------------------ shaders */

typedef struct D8Constants {
    UINT start, count;
    float *values;
} D8Constants;

typedef struct D8VertexShader {
    DWORD *declaration;
    UINT declaration_size;
    DWORD *function;
    UINT function_size;
    IDirect3DVertexDeclaration9 *decl9;
    IDirect3DVertexShader9 *vs9;
    D8Constants *constants;
    UINT constant_sets;
} D8VertexShader;

typedef struct D8PixelShader {
    DWORD *function;
    UINT function_size;
    IDirect3DPixelShader9 *ps9;
    /* the registers of its def instructions, which setting it loads. */
    D8Constants *defs;
    UINT def_count;
} D8PixelShader;

/* handles of shaders and state blocks: indices, 0 kept back, the last freed given out first. */
typedef struct D8Table {
    void **slots;
    UINT count, capacity;
    UINT *free;
    UINT free_count, free_capacity;
} D8Table;

UINT D8TableAdd(D8Table *t, void *item);
void *D8TableGet(const D8Table *t, UINT index);
void D8TableRemove(D8Table *t, UINT index);
void D8TableFree(D8Table *t);

/* checks shader code and declarations as windows' runtime does, the text of what fails kept when asked. */
HRESULT D8ValidateVertexShader(const DWORD *code, const DWORD *declaration, const D3DCAPS8 *caps, BOOL text,
                               char **errors);
HRESULT D8ValidatePixelShader(const DWORD *code, const D3DCAPS8 *caps, BOOL text, char **errors);

/* ------------------------------------------------------------------ state blocks */

enum {
    D8_CMD_RS,
    D8_CMD_TSS,
    D8_CMD_TEXTURE,
    D8_CMD_PALETTE,
    D8_CMD_STREAM,
    D8_CMD_INDICES,
    D8_CMD_VIEWPORT,
    D8_CMD_TRANSFORM,
    D8_CMD_PLANE,
    D8_CMD_MATERIAL,
    D8_CMD_LIGHT,
    D8_CMD_LIGHT_ENABLE,
    D8_CMD_VS,
    D8_CMD_PS,
    D8_CMD_VS_CONSTANTS,
    D8_CMD_PS_CONSTANTS,
};

typedef struct D8Command {
    DWORD kind, a, b;
    D8Object *object;
    float *constants;
    union {
        DWORD value;
        D3DMATRIX matrix;
        D3DVIEWPORT8 viewport;
        D3DMATERIAL8 material;
        D3DLIGHT8 light;
        float plane[4];
    } u;
} D8Command;

typedef struct D8StateBlock {
    D8Command *commands;
    UINT count, capacity;
} D8StateBlock;

/* ------------------------------------------------------------------ device */

typedef struct D8Light {
    BOOL enabled;
    D3DLIGHT8 light;
} D8Light;

typedef struct D8Palette {
    PALETTEENTRY entries[256];
} D8Palette;

struct D8Device {
    IDirect3DDevice8 iface;
    LONG refs;
    D8Direct3D *d3d;
    IDirect3DDevice9 *d9;
    CRITICAL_SECTION cs;
    BOOL multithreaded;
    UINT adapter;
    D3DDEVTYPE type;
    HWND focus;
    DWORD flags, creation_flags;
    DWORD thread;
    D3DCAPS8 caps;
    D8Object *swapchain;
    D8Object *auto_depth;
    D8Object *target, *depth;
    BOOL in_scene;
    BOOL software;
    D3DGAMMARAMP gamma;

    DWORD rs[256];
    DWORD tss[D8_STAGES][64];
    D8Object *textures[D8_STAGES];
    D3DMATRIX transforms[D8_TRANSFORMS];
    D3DVIEWPORT8 viewport;
    D3DMATERIAL8 material;
    D8Light **lights;
    UINT light_count;
    float planes[D8_PLANES][4];
    D3DCLIPSTATUS8 clip_status;
    D8Palette **palettes;
    UINT palette_count;
    UINT palette;

    struct {
        D8Object *buffer;
        UINT stride;
    } streams[D8_STREAMS];
    D8Object *indices;
    UINT base_vertex;
    DWORD vs, ps;
    float vs_constants[D8_VS_CONSTANTS][4];
    float ps_constants[D8_PS_CONSTANTS][4];
    D8Table vertex_shaders, pixel_shaders, blocks;
    D8StateBlock *recording;
    UINT recording_token;
};

D8Device *D8DeviceFrom(IDirect3DDevice8 *iface);
HRESULT D8CreateDevice(D8Direct3D *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                       D3DPRESENT_PARAMETERS8 *params, IDirect3DDevice8 **out);
void D8Enter(D8Device *d);
void D8Leave(D8Device *d);
/* checks presentation parameters as direct3d 8 does and turns them into direct3d 9's. */
HRESULT D8ConvertPresent(D8Device *d, D8Direct3D *d3d, UINT adapter, D3DDEVTYPE type, HWND focus,
                         D3DPRESENT_PARAMETERS8 *in, D3DPRESENT_PARAMETERS *out);

/* each part of the device's vtable, filled by the file that has it. */
void D8DeviceStateMethods(IDirect3DDevice8Vtbl *vtbl);
void D8DeviceShaderMethods(IDirect3DDevice8Vtbl *vtbl);

/* the state as a new or reset device has it, already the direct3d 9 device's. */
void D8DefaultState(D8Device *d);
/* lets go of every binding, state block, light and palette. */
void D8ClearState(D8Device *d);
void D8DeleteShaders(D8Device *d);
UINT D8TransformIndex(D3DTRANSFORMSTATETYPE type);
HRESULT D8SetRenderTarget(D8Device *d, D8Object *target, D8Object *depth);
void D8ResetViewport(D8Device *d);

/* the setters state blocks replay through. */
HRESULT D8SetVertexShader(D8Device *d, DWORD handle);
HRESULT D8SetPixelShader(D8Device *d, DWORD handle);
HRESULT D8SetStreamSource(D8Device *d, UINT stream, D8Object *buffer, UINT stride);
HRESULT D8SetIndices(D8Device *d, D8Object *buffer, UINT base);
HRESULT D8SetVertexConstants(D8Device *d, UINT start, const void *data, UINT count);
HRESULT D8SetPixelConstants(D8Device *d, UINT start, const void *data, UINT count);
/* a mixed device switching how it processes vertices, which unbinds its streams and shader. */
void D8SwitchProcessing(D8Device *d, BOOL software);

void D8BlockFree(D8Device *d, D8StateBlock *b);
D8Command *D8Record(D8Device *d, DWORD kind);

#endif
