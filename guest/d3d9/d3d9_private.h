/*
 * d3d9.dll: direct3d 9 over mime's own direct3d 11. resources programs may
 * lock keep a copy in memory, uploaded on unlock in gpu order, so a lock
 * never waits. shaders and fixed function state are translated to dxbc.
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
    /* typeless when views read it two ways. */
    DXGI_FORMAT resource;
    DXGI_FORMAT view;
    DXGI_FORMAT srgb;
    DXGI_FORMAT depth;
    /* a MIME_SWIZZLE applied to shader reads, 0 for none. */
    WORD swizzle;
    /* bytes per block as locked, block size in texels, and bytes per block on the gpu. */
    BYTE bytes;
    BYTE block;
    BYTE stored;
    BYTE flags;
} D9Format;

const D9Format *D9GetFormat(D3DFORMAT format);
UINT D9FormatPitch(const D9Format *f, UINT width);
UINT D9FormatRows(const D9Format *f, UINT height);
UINT D9PackColor(const D9Format *f, D3DCOLOR color, BYTE out[16]);
/* converts texels between the locked layout and the gpu's. */
void D9ConvertUpload(const D9Format *f, const BYTE *in, UINT in_pitch, BYTE *out, UINT out_pitch, UINT width,
                     UINT rows);

/* ------------------------------------------------------------------ objects */

typedef struct D9Device D9Device;
typedef struct D9Surface D9Surface;
typedef struct D9Texture D9Texture;
typedef struct D9SwapChain D9SwapChain;

/*
 * program references keep the device alive. device binds keep only the
 * object, so bound objects never keep their device.
 */
typedef struct D9Resource D9Resource;
struct D9Resource {
    LONG refs;
    LONG binds;
    /* back buffers and the automatic depth buffer, which live as long as the device. */
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

/* a lockable subresource, laid out in memory as direct3d 9 does. */
typedef struct D9Memory {
    BYTE *bytes;
    UINT pitch;
    UINT slice;
    UINT size;
    BOOL locked;
    /* the bytes belong to the program: a system memory surface made over its memory. */
    BOOL external;
    DWORD lock_flags;
    RECT lock_rect;
    UINT lock_front, lock_back;
} D9Memory;

/* a texture level or face, or a standalone surface. */
struct D9Surface {
    IDirect3DSurface9 iface;
    D9Resource base;
    /* holds the references of its levels. null for standalone surfaces. */
    D9Texture *container;
    D9SwapChain *swapchain;
    ID3D11Texture2D *texture;
    UINT level, face;
    UINT width, height;
    D3DMULTISAMPLE_TYPE samples;
    DWORD quality;
    BOOL lockable;
    /* levels point into their texture's memory. memory_valid says whether it matches the gpu. */
    D9Memory *memory;
    D9Memory own_memory;
    BOOL memory_valid;
    /* made on first use. */
    ID3D11RenderTargetView *rtv, *rtv_srgb;
    ID3D11DepthStencilView *dsv;
    HDC dc;
    HBITMAP bitmap;
};

/* uses its texture's references. */
typedef struct D9Volume {
    IDirect3DVolume9 iface;
    D9Texture *container;
    MimePrivateData data;
    UINT level;
    UINT width, height, depth;
} D9Volume;

struct D9Texture {
    union {
        IDirect3DBaseTexture9 base;
        IDirect3DTexture9 tex;
        IDirect3DCubeTexture9 cube;
        IDirect3DVolumeTexture9 volume;
    } iface;
    D9Resource base;
    ID3D11Resource *resource;
    UINT width, height, depth, faces;
    /* d3d_levels has more than levels when the texture makes its own mips. */
    UINT levels, d3d_levels;
    DWORD lod;
    D3DTEXTUREFILTERTYPE autogen_filter;
    BOOL autogen_dirty;
    /* face by face, each with all its levels. */
    D9Surface **surfaces;
    D9Volume **volumes;
    D9Memory *memory;
    ID3D11ShaderResourceView *srv, *srv_srgb;
};

typedef struct D9Buffer {
    union {
        IDirect3DVertexBuffer9 vb;
        IDirect3DIndexBuffer9 ib;
    } iface;
    D9Resource base;
    ID3D11Buffer *buffer;
    UINT size;
    DWORD fvf;
    /* null for mapped buffers. the dirty range covers what locks changed. */
    BYTE *memory;
    UINT dirty_lo, dirty_hi;
    BYTE *mapped_bytes;
    UINT locks;
    BOOL mapped_once;
    /* drawn with since the last discard. */
    BOOL used;
} D9Buffer;

D9Texture *D9TextureFrom(IDirect3DBaseTexture9 *iface);
HRESULT D9CreateTexture(D9Device *device, D3DRESOURCETYPE type, UINT width, UINT height, UINT depth, UINT levels,
                        DWORD usage, D3DFORMAT format, D3DPOOL pool, HANDLE *shared, D9Texture **out);
ID3D11ShaderResourceView *D9TextureSrv(D9Texture *texture, BOOL srgb);
void D9TexturePrepare(D9Texture *texture);
HRESULT D9UpdateTexture(D9Device *device, D9Texture *source, D9Texture *dest);
void D9UploadLevel(D9Texture *texture, UINT sub, UINT index, const RECT *rect);
D9Buffer *D9BufferFrom(void *iface);
HRESULT D9CreateBuffer(D9Device *device, BOOL index, UINT size, DWORD usage, DWORD fvf, D3DFORMAT format,
                       D3DPOOL pool, D9Buffer **out);

/* ------------------------------------------------------------------ the swap chain */

/* a dxgi swap chain on a window, its buffer sized to the client area. */
typedef struct D9Output {
    HWND window;
    IDXGISwapChain *dxgi;
    ID3D11Texture2D *buffer;
    UINT width, height;
    /* what the window shows, kept once a present covered only part of it. */
    ID3D11Texture2D *front;
} D9Output;

struct D9SwapChain {
    IDirect3DSwapChain9Ex iface;
    LONG refs;
    D9Device *device;
    D3DPRESENT_PARAMETERS params;
    HWND window;
    D9Output output;
    /* for presents to a window other than the swap chain's. */
    D9Output *others;
    UINT other_count;
    /* the resolved multisampled back buffer, for presents that scale it. */
    ID3D11Texture2D *resolved;
    /* the device's own swap chain, which lives as long as the device. */
    BOOL implicit;
    /* copied to dxgi's buffer on present. */
    D9Surface *back_buffers[D3DPRESENT_BACK_BUFFERS_MAX_EX];
    UINT back_buffer_count;
    UINT present_count;
    /* zero until the program sets one, shown only while fullscreen. */
    D3DGAMMARAMP gamma;
    BOOL gamma_set, gamma_shown;
};

HRESULT D9CreateSwapChain(D9Device *device, D3DPRESENT_PARAMETERS *params, D9SwapChain **out);
HRESULT D9ResetSwapChain(D9SwapChain *swapchain, D3DPRESENT_PARAMETERS *params);
void D9DestroySwapChain(D9SwapChain *swapchain);
HRESULT D9Present(D9SwapChain *swapchain, const RECT *source, const RECT *dest, HWND window, const RGNDATA *dirty,
                  DWORD flags);
BOOL D9SwapChainInUse(const D9SwapChain *swapchain);
/* shows the ramp while fullscreen, takes it off otherwise. */
void D9ShowGamma(D9SwapChain *swapchain);
HRESULT D9RasterStatus(D9SwapChain *swapchain, D3DRASTER_STATUS *status);
void D9ForgetSwapChain(D9Device *device, D9SwapChain *swapchain);

/* ------------------------------------------------------------------ the device */

#define D9_MAX_STAGES 8
#define D9_MAX_SAMPLERS 21
#define D9_MAX_STREAMS 16
#define D9_MAX_TARGETS 4
#define D9_VS_CONSTANTS 256
#define D9_PS_CONSTANTS 224

typedef struct D9Ring {
    ID3D11Buffer *buffer;
    UINT size, used;
} D9Ring;

/* render state groups, so draws rebuild only the state objects that changed. */
#define D9_STATES_BLEND 1u
#define D9_STATES_DEPTH 2u
#define D9_STATES_RASTER 4u
#define D9_STATES_INTERNAL 8u
#define D9_STATES_ALL 15u
UINT D9RenderStateGroups(D3DRENDERSTATETYPE state);

typedef struct D9StateEntry {
    DWORD key[16];
    UINT size, hash;
    void *object;
} D9StateEntry;

typedef struct D9StateCache {
    D9StateEntry entries[64];
    UINT count, next, last;
} D9StateCache;

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

    D9Texture *textures[D9_MAX_SAMPLERS];
    struct {
        D9Buffer *buffer;
        UINT offset, stride;
    } streams[D9_MAX_STREAMS];
    D9Buffer *indices;
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
    BOOL targets_dirty, viewport_dirty, states_dirty, ff_dirty, vs_consts_dirty, ps_consts_dirty;
    /* D9_STATES_* groups changed since the last draw. */
    UINT state_groups;
    UINT samplers_dirty, textures_dirty, streams_dirty;
    BOOL indices_dirty;
    HCURSOR cursor;
    BOOL cursor_shown;

    struct D9Shader *vs, *ps;
    struct D9VertexDecl *decl;
    /* declarations made by SetFVF, kept for the device's life. */
    struct D9VertexDecl **fvf_decls;
    UINT fvf_decl_count;
    /* per stage: the float, integer and internal constant buffers. */
    ID3D11Buffer *cbs[2][3];
    /* float constant buffers of 16 to 256 registers. the one bound is just large enough for the shader. */
    ID3D11Buffer *float_cbs[2][5];
    UINT bound_bucket[2];
    ID3D11Buffer *zeros;
    D9Ring up_vertices, up_indices;
    /* ProcessVertices' stream output and its readback copy. */
    ID3D11Buffer *processed, *processed_read;
    UINT processed_size;
    D9StateCache blend_cache, depth_cache, raster_cache, sampler_cache;
    /* what the context has bound, so draws set only what changed. */
    struct D9Variant *bound_vs, *bound_ps;
    ID3D11InputLayout *bound_layout;
    D3D11_PRIMITIVE_TOPOLOGY bound_topology;
    ID3D11ShaderResourceView *bound_srvs[D9_MAX_SAMPLERS];
    ID3D11SamplerState *bound_samplers[D9_MAX_SAMPLERS];
    BOOL bound_srgb, bound_vs_ff;
    ID3D11BlendState *bound_blend;
    ID3D11DepthStencilState *bound_depth;
    ID3D11RasterizerState *bound_raster;
    DWORD bound_blend_factor, bound_sample_mask, bound_stencil_ref;
    BOOL internal_dirty;
    /* the internal constants last uploaded, to skip uploads that change nothing. */
    float internal[2][24][4];
    BOOL internal_valid[2];
    BOOL warned_fixed;
    /* fixed function programs and their constants. */
    struct D9FFProgram *ff_vs, *ff_ps;
    float ff_vs_consts[151][4];
    float ff_ps_consts[27][4];
    /* while BeginStateBlock records, setters change this block instead of the device. */
    struct D9StateBlock *recording;
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
HRESULT D9InitDraws(D9Device *device);
void D9FreeDraws(D9Device *device);
void D9ResetBindings(D9Device *device);
UINT D9VertexCount(D3DPRIMITIVETYPE type, UINT primitives);
HRESULT D9Draw(D9Device *device, D3DPRIMITIVETYPE type, UINT start, UINT primitives);
HRESULT D9DrawIndexed(D9Device *device, D3DPRIMITIVETYPE type, INT base, UINT start, UINT primitives);
HRESULT D9DrawUP(D9Device *device, D3DPRIMITIVETYPE type, UINT primitives, const void *data, UINT stride);
HRESULT D9DrawIndexedUP(D9Device *device, D3DPRIMITIVETYPE type, UINT min_index, UINT vertices, UINT primitives,
                        const void *indices, D3DFORMAT format, const void *data, UINT stride);
void D9DeviceSetStream(D9Device *device, UINT stream, struct D9Buffer *buffer, UINT offset, UINT stride);
void D9DeviceSetIndices(D9Device *device, struct D9Buffer *buffer);
void D9FillCaps(UINT adapter, D3DCAPS9 *caps);

/* ------------------------------------------------------------------ resources */

void D9ResourceInit(D9Resource *r, D9Device *device, D3DRESOURCETYPE type, D3DPOOL pool, DWORD usage,
                    D3DFORMAT format, void (*destroy)(D9Resource *r));
void D9ResourceClear(D9Resource *r);
ULONG D9ResourceAddRef(D9Resource *r);
ULONG D9ResourceRelease(D9Resource *r);
/* references the device holds on what it binds. */
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
                        D3DMULTISAMPLE_TYPE samples, DWORD quality, BOOL lockable, void *memory, D9Surface **out);
D9Surface *D9SurfaceFrom(IDirect3DSurface9 *iface);
ULONG D9SurfaceAddRef(D9Surface *surface);
ULONG D9SurfaceRelease(D9Surface *surface);
ID3D11RenderTargetView *D9SurfaceRtv(D9Surface *surface, BOOL srgb);
ID3D11DepthStencilView *D9SurfaceDsv(D9Surface *surface);
/* copy texels between the gpu and the surface's memory. */
HRESULT D9SurfaceDownload(D9Surface *surface);
void D9SurfaceUpload(D9Surface *surface, const RECT *rect);
RECT D9SurfaceBlockRect(const D9Surface *surface, const RECT *rect);
void D9SurfaceUploadBytes(D9Surface *surface, const RECT *rect, const BYTE *bytes, UINT pitch);
void D9SurfaceWritten(D9Surface *surface);
UINT D9SurfaceSubresource(const D9Surface *surface);
HRESULT D9SurfaceLock(D9Surface *surface, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags);
HRESULT D9SurfaceUnlock(D9Surface *surface);
/* levels and back buffers start as a surface without a texture. */
HRESULT D9NewSurface(D9Device *device, D3DFORMAT format, DWORD usage, D3DPOOL pool, UINT width, UINT height,
                     D9Surface **out);
void D9DestroyImplicitSurface(D9Surface *surface);
HRESULT D9SurfaceMakeTexture(D9Surface *surface);
void D9FreeMemory(D9Memory *memory);
HRESULT D9AllocMemory(D9Memory *memory, const D9Format *f, UINT width, UINT height, UINT depth);

/* ------------------------------------------------------------------ dxbc */

typedef struct D9Code {
    DWORD *words;
    UINT count, capacity;
    BOOL failed;
} D9Code;

typedef struct D9Opnd {
    DWORD words[10];
    UINT count;
} D9Opnd;

typedef struct D9SigElement {
    const char *name;
    UINT index;
    UINT system_value;
    UINT component_type;
    UINT reg;
    BYTE mask, used;
} D9SigElement;

typedef struct D9Signature {
    UINT count;
    D9SigElement elements[40];
} D9Signature;

void D9CodePut(D9Code *c, DWORD word);
void D9CodeFree(D9Code *c);
D9Opnd D9Dst(UINT type, UINT reg, UINT mask);
D9Opnd D9Src(UINT type, UINT reg, UINT swizzle);
D9Opnd D9Temp(UINT reg, UINT mask);
D9Opnd D9TempSrc(UINT reg, UINT swizzle);
D9Opnd D9TempScalar(UINT reg, UINT component);
D9Opnd D9Imm4(DWORD x, DWORD y, DWORD z, DWORD w);
D9Opnd D9Imm(DWORD v);
D9Opnd D9ImmF(float f);
D9Opnd D9Cb(UINT slot, UINT element, UINT swizzle, int rel_reg, UINT rel_component);
D9Opnd D9ResourceOpnd(UINT reg);
D9Opnd D9SamplerOpnd(UINT reg);
D9Opnd D9Mod(D9Opnd o, UINT modifier);
D9Opnd D9Neg(D9Opnd o);
D9Opnd D9Abs(D9Opnd o);
void D9Emit(D9Code *c, DWORD opcode, const D9Opnd *ops, UINT count);
void D9Op1(D9Code *c, DWORD op, D9Opnd d, D9Opnd a);
void D9Op2(D9Code *c, DWORD op, D9Opnd d, D9Opnd a, D9Opnd b);
void D9Op3(D9Code *c, DWORD op, D9Opnd d, D9Opnd a, D9Opnd b, D9Opnd e);
HRESULT D9Container(const D9Signature *inputs, const D9Signature *outputs, const D9Code *code, void **out,
                    SIZE_T *size);

/* ------------------------------------------------------------------ shaders */

enum {
    D9_OP_NOP = 0,
    D9_OP_MOV = 1,
    D9_OP_ADD = 2,
    D9_OP_SUB = 3,
    D9_OP_MAD = 4,
    D9_OP_MUL = 5,
    D9_OP_RCP = 6,
    D9_OP_RSQ = 7,
    D9_OP_DP3 = 8,
    D9_OP_DP4 = 9,
    D9_OP_MIN = 10,
    D9_OP_MAX = 11,
    D9_OP_SLT = 12,
    D9_OP_SGE = 13,
    D9_OP_EXP = 14,
    D9_OP_LOG = 15,
    D9_OP_LIT = 16,
    D9_OP_DST = 17,
    D9_OP_LRP = 18,
    D9_OP_FRC = 19,
    D9_OP_M4x4 = 20,
    D9_OP_M4x3 = 21,
    D9_OP_M3x4 = 22,
    D9_OP_M3x3 = 23,
    D9_OP_M3x2 = 24,
    D9_OP_CALL = 25,
    D9_OP_CALLNZ = 26,
    D9_OP_LOOP = 27,
    D9_OP_RET = 28,
    D9_OP_ENDLOOP = 29,
    D9_OP_LABEL = 30,
    D9_OP_DCL = 31,
    D9_OP_POW = 32,
    D9_OP_CRS = 33,
    D9_OP_SGN = 34,
    D9_OP_ABS = 35,
    D9_OP_NRM = 36,
    D9_OP_SINCOS = 37,
    D9_OP_REP = 38,
    D9_OP_ENDREP = 39,
    D9_OP_IF = 40,
    D9_OP_IFC = 41,
    D9_OP_ELSE = 42,
    D9_OP_ENDIF = 43,
    D9_OP_BREAK = 44,
    D9_OP_BREAKC = 45,
    D9_OP_MOVA = 46,
    D9_OP_DEFB = 47,
    D9_OP_DEFI = 48,
    D9_OP_TEXCOORD = 64,
    D9_OP_TEXKILL = 65,
    D9_OP_TEX = 66,
    D9_OP_TEXBEM = 67,
    D9_OP_TEXBEML = 68,
    D9_OP_TEXREG2AR = 69,
    D9_OP_TEXREG2GB = 70,
    D9_OP_TEXM3x2PAD = 71,
    D9_OP_TEXM3x2TEX = 72,
    D9_OP_TEXM3x3PAD = 73,
    D9_OP_TEXM3x3TEX = 74,
    D9_OP_TEXM3x3SPEC = 76,
    D9_OP_TEXM3x3VSPEC = 77,
    D9_OP_EXPP = 78,
    D9_OP_LOGP = 79,
    D9_OP_CND = 80,
    D9_OP_DEF = 81,
    D9_OP_TEXREG2RGB = 82,
    D9_OP_TEXDP3TEX = 83,
    D9_OP_TEXM3x2DEPTH = 84,
    D9_OP_TEXDP3 = 85,
    D9_OP_TEXM3x3 = 86,
    D9_OP_TEXDEPTH = 87,
    D9_OP_CMP = 88,
    D9_OP_BEM = 89,
    D9_OP_DP2ADD = 90,
    D9_OP_DSX = 91,
    D9_OP_DSY = 92,
    D9_OP_TEXLDD = 93,
    D9_OP_SETP = 94,
    D9_OP_TEXLDL = 95,
    D9_OP_BREAKP = 96,
    D9_OP_PHASE = 0xFFFD,
    D9_OP_COMMENT = 0xFFFE,
    D9_OP_END = 0xFFFF,
};

typedef struct D9Param {
    DWORD token;
    /* the address register token of relative addressing, model 2 and up. */
    DWORD rel;
    BOOL has_rel;
} D9Param;

typedef struct D9Inst {
    UINT opcode, control;
    BOOL predicated, coissue;
    BOOL has_dst;
    D9Param dst, pred, src[4];
    UINT src_count;
    DWORD usage;
    DWORD values[4];
    /* including the instruction token. */
    UINT words;
} D9Inst;

/* parsed once when the shader is created. */
typedef struct D9ShaderInfo {
    BOOL pixel;
    UINT major, minor;
    DWORD *code;
    UINT words;
    /* by register: inputs of vertex shaders and model 3 pixel shaders, outputs of model 3 vertex shaders. */
    BYTE in_usage[16], in_index[16], in_mask[16];
    BOOL in_centroid[16];
    UINT in_declared;
    BYTE out_usage[16], out_index[16], out_mask[16];
    UINT out_declared;
    /* as declared by dcl: 2 2d, 3 cube, 4 volume. */
    BYTE sampler_type[16];
    /* outputs written: by kind for vertex shaders before model 3, color targets for pixel shaders. */
    UINT written;
    BOOL writes_depth, vpos, vface;
    /* constants the shader defines itself, overriding the device's. */
    struct {
        UINT reg;
        DWORD v[4];
    } *defs;
    UINT def_count;
    UINT float_count;
    BOOL relative;
    /* the integer and boolean buffer is uploaded only when the code reads them. */
    BOOL uses_ints;
} D9ShaderInfo;

/* D9ShaderInfo.written bits of vertex shaders before model 3. */
#define D9_OUT_POS 0x1
#define D9_OUT_FOG 0x2
#define D9_OUT_PTS 0x4
#define D9_OUT_D0 0x8
#define D9_OUT_T0 0x20

/* the device state a translation depends on. */
typedef struct D9ShaderKey {
    /* pixel shaders: registers the vertex shader writes. the others read as zeros. */
    UINT ps_inputs;
    /* texture types of undeclared samplers, 2 bits each: 0 2d, 1 cube, 2 volume. */
    UINT sampler_types;
    /* samplers bound to depth textures, which compare. */
    WORD shadow;
    /* 0 none, 1 from the vertex, 2 exp, 3 exp2, 4 linear in depth. */
    BYTE fog;
    /* the alpha test comparison, 0 for none. */
    BYTE alpha_func;
    BYTE flat;
    /* vertex shaders: the render state's point size, written when the shader writes none. */
    BYTE point_size;
    /* vertex shaders: the enabled user clip planes. */
    BYTE clip_planes;
    /* pixel shaders: texture coordinates come from the point sprite. */
    BYTE point_sprite;
    /* vertex shaders: integer attributes converted to floats, 2 bits each: 1 unsigned, 2 signed. */
    UINT int_inputs;
    /* pixel shaders 1.0 to 1.3: stages whose coordinates are projected. */
    BYTE projected;
} D9ShaderKey;

typedef struct D9Variant {
    struct D9Variant *next;
    D9ShaderKey key;
    const D9ShaderInfo *info;
    /* null when translation failed. vertex shaders keep their dxbc to build input layouts. */
    ID3D11DeviceChild *shader;
    void *dxbc;
    SIZE_T size;
    /* vertex shaders: the interpolated value registers written. */
    UINT outputs;
    /* for ProcessVertices: streams out the position, then each output register. */
    ID3D11GeometryShader *stream_out;
} D9Variant;

typedef struct D9Shader {
    union {
        IDirect3DVertexShader9 vs;
        IDirect3DPixelShader9 ps;
    } iface;
    D9Resource base;
    D9ShaderInfo info;
    /* a fixed function program, its constants taken from the device state. */
    BOOL ff;
    D9Variant *variants;
    D9Variant *last;
} D9Shader;

typedef struct D9Layout {
    struct D9Layout *next;
    D9Variant *variant;
    UINT instanced;
    UINT rates[16];
    ID3D11InputLayout *layout;
} D9Layout;

typedef struct D9VertexDecl {
    IDirect3DVertexDeclaration9 iface;
    D9Resource base;
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH + 1];
    UINT count;
    DWORD fvf;
    UINT streams;
    BOOL pretransformed;
    UINT colors;
    D9Layout *layouts;
} D9VertexDecl;

/* fixed function program keys: the state the programs depend on. */
typedef struct D9FFVSKey {
    UINT inputs;
    UINT tex_sizes;
    UINT lights;
    UINT tex_index, tex_gen, tex_flags;
    BYTE material_sources;
    BYTE pretransformed, lighting, specular, local_viewer, normalize, fog, range_fog, point_scale, blend, indexed;
    BYTE stages;
} D9FFVSKey;

typedef struct D9FFPSStage {
    BYTE color_op, alpha_op;
    BYTE color_args[3], alpha_args[3];
    BYTE result_temp, texture, projected;
} D9FFPSStage;

typedef struct D9FFPSKey {
    D9FFPSStage stage[8];
    BYTE stages, specular;
} D9FFPSKey;

typedef struct D9FFProgram {
    struct D9FFProgram *next;
    D9Shader *shader;
    UINT size;
    BYTE key[];
} D9FFProgram;

#define D9_FF_VS_CONSTANTS 151
#define D9_FF_PS_CONSTANTS 27

void D9FFVSKeyOf(const D9Device *device, D9FFVSKey *key);
void D9FFPSKeyOf(const D9Device *device, D9FFPSKey *key);
D9Shader *D9FFShader(D9Device *device, BOOL pixel, const void *key, UINT size);
void D9FreeFFShaders(D9Device *device);
void D9UpdateFFConstants(D9Device *device);
void D9FFClipPlanes(const D9Device *device, float out[6][4]);

/* which states a block holds. */
typedef struct D9StateMask {
    DWORD render_states[8];
    UINT64 stage_states[D9_MAX_STAGES];
    WORD sampler_states[D9_MAX_SAMPLERS];
    DWORD transforms[16];
    DWORD vs_f[8], ps_f[7];
    DWORD vs_i, ps_i, vs_b, ps_b;
    UINT textures, streams, stream_freq;
    BYTE clip_planes;
    BOOL material, viewport, scissor, vs, ps, decl, indices, palette, npatch, all_lights;
} D9StateMask;

typedef struct D9BlockLight {
    DWORD index;
    BOOL has_light, has_enable;
    D3DLIGHT9 light;
    BOOL enabled;
} D9BlockLight;

typedef struct D9StateValues {
    DWORD render_states[256];
    DWORD stage_states[D9_MAX_STAGES][33];
    DWORD sampler_states[D9_MAX_SAMPLERS][14];
    D3DMATRIX *transforms;
    D3DMATERIAL9 material;
    D9BlockLight *lights;
    UINT light_count;
    float clip_planes[6][4];
    D3DVIEWPORT9 viewport;
    RECT scissor;
    D9Shader *vs, *ps;
    struct D9VertexDecl *decl;
    D9Texture *textures[D9_MAX_SAMPLERS];
    struct {
        D9Buffer *buffer;
        UINT offset, stride;
    } streams[D9_MAX_STREAMS];
    UINT stream_freq[D9_MAX_STREAMS];
    D9Buffer *indices;
    float vs_f[D9_VS_CONSTANTS][4];
    int vs_i[16][4];
    BOOL vs_b[16];
    float ps_f[D9_PS_CONSTANTS][4];
    int ps_i[16][4];
    BOOL ps_b[16];
    UINT palette;
    float npatch;
} D9StateValues;

typedef struct D9StateBlock {
    IDirect3DStateBlock9 iface;
    D9Resource base;
    D9StateMask mask;
    D9StateValues v;
} D9StateBlock;

HRESULT D9NewStateBlock(D9Device *device, D9StateBlock **out);
HRESULT D9CreateQuery(D9Device *device, D3DQUERYTYPE type, IDirect3DQuery9 **out);
HRESULT D9CreateStateBlock(D9Device *device, D3DSTATEBLOCKTYPE type, D9StateBlock **out);
void D9RecordRenderState(D9StateBlock *b, DWORD state, DWORD value);
void D9RecordStageState(D9StateBlock *b, DWORD stage, DWORD type, DWORD value);
void D9RecordSamplerState(D9StateBlock *b, UINT slot, DWORD type, DWORD value);
void D9RecordTransform(D9StateBlock *b, UINT index, const D3DMATRIX *m);
void D9RecordMaterial(D9StateBlock *b, const D3DMATERIAL9 *m);
void D9RecordLight(D9StateBlock *b, DWORD index, const D3DLIGHT9 *light);
void D9RecordLightEnable(D9StateBlock *b, DWORD index, BOOL enable);
void D9RecordClipPlane(D9StateBlock *b, DWORD index, const float *plane);
void D9RecordViewport(D9StateBlock *b, const D3DVIEWPORT9 *viewport);
void D9RecordScissor(D9StateBlock *b, const RECT *rect);
void D9RecordShader(D9StateBlock *b, BOOL pixel, D9Shader *shader);
void D9RecordDecl(D9StateBlock *b, struct D9VertexDecl *decl);
void D9RecordTexture(D9StateBlock *b, UINT slot, D9Texture *texture);
void D9RecordStream(D9StateBlock *b, UINT stream, D9Buffer *buffer, UINT offset, UINT stride);
void D9RecordStreamFreq(D9StateBlock *b, UINT stream, UINT setting);
void D9RecordIndices(D9StateBlock *b, D9Buffer *buffer);
void D9RecordConstants(D9StateBlock *b, BOOL pixel, UINT kind, UINT start, const void *data, UINT count);
void D9RecordPalette(D9StateBlock *b, UINT palette);
void D9RecordNPatch(D9StateBlock *b, float segments);

/* the input slot of the zero buffer that inputs missing from a declaration read. */
#define D9_ZERO_STREAM 16

D9Shader *D9ShaderFrom(void *iface);
HRESULT D9CreateShader(D9Device *device, const DWORD *code, BOOL pixel, D9Shader **out);
D9Variant *D9ShaderVariant(D9Shader *shader, const D9ShaderKey *key);
D9VertexDecl *D9VertexDeclFrom(IDirect3DVertexDeclaration9 *iface);
HRESULT D9CreateVertexDecl(D9Device *device, const D3DVERTEXELEMENT9 *elements, DWORD fvf, D9VertexDecl **out);
UINT D9FvfElements(DWORD fvf, D3DVERTEXELEMENT9 *elements);
UINT D9DeclIntegerInputs(const D9VertexDecl *decl, const D9ShaderInfo *vs);
ID3D11InputLayout *D9DeclLayout(D9VertexDecl *decl, D9Device *device, D9Variant *vs, UINT instanced,
                                const UINT *rates);

UINT D9RegType(DWORD token);
UINT D9RegNum(DWORD token);
BOOL D9Decode(const D9ShaderInfo *info, const DWORD *code, UINT left, D9Inst *inst);
HRESULT D9ParseShader(const DWORD *code, BOOL pixel, D9ShaderInfo *info);
void D9FreeShaderInfo(D9ShaderInfo *info);
/* the returned dxbc is freed with HeapFree. */
HRESULT D9TranslateShader(const D9ShaderInfo *info, const D9ShaderKey *key, void **bytecode, SIZE_T *size,
                          UINT *outputs);
/* the float constant registers bound for a shader, which sets the buffer size. */
UINT D9FloatBucketSize(const D9ShaderInfo *info);

/* interpolated values use the same registers in every shader, so any two link. */
#define D9_SLOT_POSITION 28
#define D9_SLOT_POINT_SIZE 29
#define D9_SLOT_CLIP 30
#define D9_SLOT_FACE 29
#define D9_SLOT_POINT_COORD 30
int D9VaryingSlot(UINT usage, UINT index);
void D9SlotSemantic(UINT slot, const char **name, UINT *index);
BOOL D9PrepareDraw(D9Device *device, D3DPRIMITIVETYPE type);
HRESULT D9ProcessVertices(D9Device *device, UINT source, UINT dest, UINT count, D9Buffer *buffer,
                          D9VertexDecl *declaration, DWORD flags);
const char *D9UsageName(UINT usage);

/* ------------------------------------------------------------------ adapters */

UINT D9AdapterCount(void);
/* each direct3d 9 adapter is one monitor, a dxgi output. */
HRESULT D9GetOutput(UINT adapter, IDXGIAdapter1 **out_adapter, IDXGIOutput **out_output);
IDXGIFactory1 *D9Factory(void);

#endif
