/*
 * gpu channel shared by a guest process and the host.
 *
 * a channel is one physically contiguous block of guest memory, mapped in
 * the process and seen by the host in place: a header, a command ring the
 * guest writes and the host executes on metal, and heap space for buffers,
 * texture uploads and readbacks that metal reads and writes without copies.
 * more heap blocks can be added later. commands name memory as a heap block
 * and an offset in it.
 *
 * the guest keeps the graphics api state and sends metal level commands,
 * so every value below is already a metal enum value. objects are numbered
 * by the guest, unique in the channel.
 */
#ifndef MIME_GPU_H
#define MIME_GPU_H

#include <stdint.h>

#define MIME_GPU_ALIGN 16384u              /* host page size, required by metal for buffers over guest memory */
#define MIME_GPU_HEADER_SIZE 16384u
#define MIME_GPU_RING_SIZE (4u << 20)
#define MIME_GPU_CHANNEL_SIZE (64u << 20)  /* header, ring, then heap block 0 */
#define MIME_GPU_MAX_BLOCKS 64
#define MIME_GPU_MAX_BLOCK_SIZE (64u << 20)
#define MIME_GPU_MAX_RECORD (1u << 20)

#define MIME_GPU_MAX_ATTRIBS 16
#define MIME_GPU_MAX_COLORS 8
#define MIME_GPU_MAX_TEXTURES 32
#define MIME_GPU_VERTEX_BUFFER_BASE 14     /* metal buffer index of vertex buffer slot 0, uniforms use index 0 */
#define MIME_GPU_UNIFORM_BUFFER 0

typedef struct MimeGpuHeader {
    /* written by the guest. */
    volatile uint64_t put;          /* bytes written to the ring since creation */
    volatile uint64_t wait_fence;   /* a thread sleeps until this fence, 0 when none */
    uint64_t guest_pad[6];
    /* written by the host. */
    volatile uint64_t get;          /* bytes executed */
    volatile uint64_t done_fence;   /* last fence the gpu completed */
    volatile uint32_t host_idle;    /* the host sleeps until kicked */
    volatile uint32_t lost;         /* the host stopped executing this channel after an error */
    uint64_t host_pad[5];
} MimeGpuHeader;

/* a record is a MimeGpuCmd header followed by its body, padded to 8 bytes. */
typedef struct MimeGpuCmd {
    uint32_t op;
    uint32_t size; /* bytes, header included, multiple of 8 */
} MimeGpuCmd;

enum {
    MIME_GPU_NOP = 0,             /* skipped, used to pad the end of the ring */
    MIME_GPU_FENCE,               /* MimeGpuFence: commits and signals done_fence when the gpu reaches it */
    MIME_GPU_FLUSH,               /* commits the work recorded so far */
    MIME_GPU_TEXTURE_CREATE,      /* MimeGpuTextureCreate */
    MIME_GPU_TEXTURE_DESTROY,     /* MimeGpuObject */
    MIME_GPU_TEXTURE_UPLOAD,      /* MimeGpuTextureCopy: heap to texture */
    MIME_GPU_TEXTURE_READ,        /* MimeGpuTextureCopy: texture to heap */
    MIME_GPU_TEXTURE_MIPMAPS,     /* MimeGpuObject */
    MIME_GPU_BUFFER_COPY,         /* MimeGpuBufferCopy */
    MIME_GPU_PROGRAM_CREATE,      /* MimeGpuProgramCreate */
    MIME_GPU_PROGRAM_DESTROY,     /* MimeGpuObject */
    MIME_GPU_PIPELINE_CREATE,     /* MimeGpuPipelineCreate */
    MIME_GPU_DEPTH_STENCIL_CREATE,/* MimeGpuDepthStencilCreate */
    MIME_GPU_SAMPLER_CREATE,      /* MimeGpuSamplerCreate */
    MIME_GPU_SURFACE_CONFIG,      /* MimeGpuSurfaceConfig */
    MIME_GPU_SURFACE_DESTROY,     /* MimeGpuObject */
    MIME_GPU_PRESENT,             /* MimeGpuObject: the surface's back buffer goes to its window */
    MIME_GPU_PASS_BEGIN,          /* MimeGpuPass */
    MIME_GPU_PASS_END,
    MIME_GPU_CLEAR,               /* MimeGpuClear: inside a pass, through the scissor */
    MIME_GPU_BLIT,                /* MimeGpuBlit: outside passes */
    MIME_GPU_SET_PIPELINE,        /* MimeGpuObject */
    MIME_GPU_SET_DEPTH_STENCIL,   /* MimeGpuSetDepthStencil */
    MIME_GPU_SET_RASTER,          /* MimeGpuSetRaster */
    MIME_GPU_SET_VIEWPORT,        /* MimeGpuSetViewport */
    MIME_GPU_SET_SCISSOR,         /* MimeGpuRect */
    MIME_GPU_SET_BLEND_COLOR,     /* MimeGpuSetBlendColor */
    MIME_GPU_SET_VERTEX_BUFFER,   /* MimeGpuSetVertexBuffer */
    MIME_GPU_SET_UNIFORMS,        /* MimeGpuSetUniforms followed by the data */
    MIME_GPU_SET_TEXTURE,         /* MimeGpuSetTexture */
    MIME_GPU_DRAW,                /* MimeGpuDraw */
    MIME_GPU_DRAW_INDEXED,        /* MimeGpuDrawIndexed */
    MIME_GPU_OP_COUNT
};

typedef struct MimeGpuMem {
    uint32_t block;
    uint32_t pad;
    uint64_t offset;
} MimeGpuMem;

typedef struct MimeGpuObject {
    MimeGpuCmd cmd;
    uint32_t id;
    uint32_t pad;
} MimeGpuObject;

typedef struct MimeGpuFence {
    MimeGpuCmd cmd;
    uint64_t value;
} MimeGpuFence;

enum {
    MIME_GPU_USAGE_SAMPLED = 1u << 0,
    MIME_GPU_USAGE_TARGET = 1u << 1,
};

typedef struct MimeGpuTextureCreate {
    MimeGpuCmd cmd;
    uint32_t id;
    uint32_t type;   /* MTLTextureType */
    uint32_t format; /* MTLPixelFormat */
    uint32_t width, height, depth; /* depth is the slice count of arrays and cubes too */
    uint32_t levels;
    uint32_t samples;
    uint32_t usage;
    uint32_t swizzle; /* 4 MTLTextureSwizzle bytes, red first, 0 for identity */
} MimeGpuTextureCreate;

/* a region of a texture and its rows in the heap. */
typedef struct MimeGpuTextureCopy {
    MimeGpuCmd cmd;
    uint32_t id;      /* texture, or the surface when surface is set */
    uint32_t surface; /* 1: back buffer of surface id */
    uint32_t level, slice;
    uint32_t x, y, z, width, height, depth;
    MimeGpuMem mem;
    uint32_t row_bytes;
    uint32_t image_bytes;
} MimeGpuTextureCopy;

typedef struct MimeGpuBufferCopy {
    MimeGpuCmd cmd;
    MimeGpuMem src;
    MimeGpuMem dst;
    uint64_t size;
} MimeGpuBufferCopy;

/*
 * glsl sources of a program. the host compiles them and writes a
 * MimeGpuProgramInfo at result, then signals fence when the guest may read it.
 * the body holds the vertex source, the fragment source and the attribute
 * bindings as "name\0location\0" pairs, each part sized below.
 */
typedef struct MimeGpuProgramCreate {
    MimeGpuCmd cmd;
    uint32_t id;
    uint32_t vertex_bytes;
    uint32_t fragment_bytes;
    uint32_t bindings_bytes;
    MimeGpuMem result;
    uint32_t result_capacity;
    uint32_t pad;
    uint64_t fence;
} MimeGpuProgramCreate;

typedef struct MimeGpuProgramInfo {
    uint32_t status;        /* 0 linked, 1 failed: the log says why */
    uint32_t size;          /* bytes written, 0 while pending */
    uint32_t log_offset, log_bytes;
    uint32_t attrib_count, attrib_offset;   /* MimeGpuAttribInfo array */
    uint32_t uniform_count, uniform_offset; /* MimeGpuUniformInfo array */
    uint32_t uniform_bytes;                 /* size of the uniform block */
    uint32_t pad;
} MimeGpuProgramInfo;

typedef struct MimeGpuAttribInfo {
    uint32_t name_offset; /* from the info, nul terminated */
    uint32_t location;
    uint32_t gl_type;
    uint32_t array_size;
} MimeGpuAttribInfo;

enum { MIME_GPU_STAGE_VERTEX = 0, MIME_GPU_STAGE_FRAGMENT = 1, MIME_GPU_STAGES = 2 };

typedef struct MimeGpuUniformInfo {
    uint32_t name_offset; /* "name" or "name[0]" for arrays */
    uint32_t gl_type;
    uint32_t array_size;
    uint32_t offset;      /* in the uniform block, samplers have none */
    uint32_t array_stride;
    uint32_t matrix_stride;
    int32_t texture[MIME_GPU_STAGES]; /* samplers: texture and sampler index of element 0, -1 if unused */
} MimeGpuUniformInfo;

typedef struct MimeGpuVertexAttrib {
    uint8_t format; /* MTLVertexFormat, 0 when unused */
    uint8_t slot;   /* vertex buffer slot */
    uint16_t offset;
} MimeGpuVertexAttrib;

typedef struct MimeGpuVertexLayout {
    uint32_t stride; /* 0: the value is constant */
    uint32_t instance_divisor; /* 0 per vertex */
} MimeGpuVertexLayout;

typedef struct MimeGpuColorTarget {
    uint16_t format; /* MTLPixelFormat, 0 when unused */
    uint8_t write_mask; /* MTLColorWriteMask */
    uint8_t blend;
    uint8_t rgb_op, alpha_op;
    uint8_t src_rgb, dst_rgb, src_alpha, dst_alpha;
    uint8_t pad[6];
} MimeGpuColorTarget;

typedef struct MimeGpuPipelineDesc {
    uint32_t program;
    uint16_t depth_format, stencil_format;
    uint32_t samples;
    uint32_t topology; /* MTLPrimitiveTopologyClass */
    MimeGpuColorTarget color[MIME_GPU_MAX_COLORS];
    MimeGpuVertexAttrib attribs[MIME_GPU_MAX_ATTRIBS]; /* by location */
    MimeGpuVertexLayout layouts[MIME_GPU_MAX_ATTRIBS]; /* by slot */
} MimeGpuPipelineDesc;

typedef struct MimeGpuPipelineCreate {
    MimeGpuCmd cmd;
    uint32_t id;
    uint32_t pad;
    MimeGpuPipelineDesc desc;
} MimeGpuPipelineCreate;

typedef struct MimeGpuStencilFace {
    uint8_t func, fail, depth_fail, pass; /* MTLCompareFunction, MTLStencilOperation */
    uint8_t read_mask, write_mask;
    uint8_t pad[2];
} MimeGpuStencilFace;

typedef struct MimeGpuDepthStencilCreate {
    MimeGpuCmd cmd;
    uint32_t id;
    uint8_t depth_func, depth_write, stencil;
    uint8_t pad;
    MimeGpuStencilFace front, back;
} MimeGpuDepthStencilCreate;

typedef struct MimeGpuSamplerCreate {
    MimeGpuCmd cmd;
    uint32_t id;
    uint8_t min_filter, mag_filter, mip_filter; /* MTLSamplerMinMagFilter, MTLSamplerMipFilter */
    uint8_t address_s, address_t, address_r;  /* MTLSamplerAddressMode */
    uint8_t compare; /* MTLCompareFunction, 0 without comparison */
    uint8_t border;  /* MTLSamplerBorderColor */
    uint8_t max_anisotropy;
    uint8_t pad[3];
    float lod_min, lod_max;
} MimeGpuSamplerCreate;

/* the window region a surface shows on, in guest pixels relative to the window's surface. */
typedef struct MimeGpuSurfaceConfig {
    MimeGpuCmd cmd;
    uint32_t id;
    uint32_t window_id; /* host window, 0 to hide */
    int32_t x, y;
    uint32_t width, height;
    uint16_t color_format, depth_format, stencil_format;
    uint16_t sync;  /* wait for the display when presenting */
    uint32_t samples;
    uint32_t pad;
} MimeGpuSurfaceConfig;

typedef struct MimeGpuAttachment {
    uint32_t id;       /* texture, 0 when unused */
    uint8_t surface;   /* id is a surface: its color, depth or stencil buffer */
    uint8_t load;      /* MTLLoadAction */
    uint8_t store;     /* MTLStoreAction */
    uint8_t pad;
    uint32_t level, slice;
    uint32_t resolve;  /* multisample resolve texture, 0 if none */
} MimeGpuAttachment;

typedef struct MimeGpuPass {
    MimeGpuCmd cmd;
    uint32_t width, height;
    MimeGpuAttachment color[MIME_GPU_MAX_COLORS];
    MimeGpuAttachment depth, stencil;
    float clear_color[MIME_GPU_MAX_COLORS][4];
    float clear_depth;
    uint32_t clear_stencil;
} MimeGpuPass;

enum { MIME_GPU_CLEAR_COLOR = 1, MIME_GPU_CLEAR_DEPTH = 2, MIME_GPU_CLEAR_STENCIL = 4 };

typedef struct MimeGpuClear {
    MimeGpuCmd cmd;
    uint32_t mask;
    uint32_t color_mask; /* MTLColorWriteMask for every target */
    float color[4];
    float depth;
    uint32_t stencil;
    uint32_t stencil_write_mask;
    uint32_t pad;
} MimeGpuClear;

typedef struct MimeGpuRect {
    MimeGpuCmd cmd;
    int32_t x, y;
    uint32_t width, height;
} MimeGpuRect;

/* copies or stretches between textures or surface buffers, rows counted from the bottom. */
typedef struct MimeGpuBlit {
    MimeGpuCmd cmd;
    MimeGpuAttachment src, dst;
    int32_t src_x0, src_y0, src_x1, src_y1;
    int32_t dst_x0, dst_y0, dst_x1, dst_y1;
    uint32_t mask;   /* MIME_GPU_CLEAR_* buffers */
    uint32_t linear;
} MimeGpuBlit;

typedef struct MimeGpuSetDepthStencil {
    MimeGpuCmd cmd;
    uint32_t id;
    uint32_t front_ref, back_ref;
    uint32_t pad;
} MimeGpuSetDepthStencil;

typedef struct MimeGpuSetRaster {
    MimeGpuCmd cmd;
    uint8_t cull;      /* MTLCullMode */
    uint8_t winding;   /* MTLWinding of front faces as rendered */
    uint8_t fill;      /* MTLTriangleFillMode */
    uint8_t depth_clip;
    float depth_bias, slope_scale, bias_clamp;
} MimeGpuSetRaster;

typedef struct MimeGpuSetViewport {
    MimeGpuCmd cmd;
    float x, y, width, height, znear, zfar;
} MimeGpuSetViewport;

typedef struct MimeGpuSetBlendColor {
    MimeGpuCmd cmd;
    float rgba[4];
} MimeGpuSetBlendColor;

typedef struct MimeGpuSetVertexBuffer {
    MimeGpuCmd cmd;
    uint32_t slot;
    uint32_t pad;
    MimeGpuMem mem;
} MimeGpuSetVertexBuffer;

typedef struct MimeGpuSetUniforms {
    MimeGpuCmd cmd;
    uint32_t bytes; /* followed by the data */
    uint32_t pad;
} MimeGpuSetUniforms;

typedef struct MimeGpuSetTexture {
    MimeGpuCmd cmd;
    uint8_t stage;
    uint8_t index;
    uint8_t surface;  /* id is a surface: its color buffer */
    uint8_t pad;
    uint32_t id;      /* texture, 0 unbinds */
    uint32_t sampler;
    uint32_t pad2;
} MimeGpuSetTexture;

typedef struct MimeGpuDraw {
    MimeGpuCmd cmd;
    uint32_t primitive; /* MTLPrimitiveType */
    uint32_t first, count;
    uint32_t instances, base_instance;
    uint32_t pad;
} MimeGpuDraw;

typedef struct MimeGpuDrawIndexed {
    MimeGpuCmd cmd;
    uint32_t primitive;
    uint32_t count;
    uint32_t index_type; /* MTLIndexType */
    uint32_t instances;
    int32_t base_vertex;
    uint32_t base_instance;
    MimeGpuMem indices;
} MimeGpuDrawIndexed;

#endif
