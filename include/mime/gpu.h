/*
 * shared GPU channel in contiguous guest memory: a header, a guest-written command ring executed by the host
 * on Metal, and a heap for buffers, uploads and readbacks. Metal accesses heap memory directly. further heap
 * blocks can be added; commands address them by block and offset.
 *
 * the guest owns graphics API state and sends Metal-level commands using Metal enum values. guest-assigned
 * object ids are unique within the channel.
 */
#ifndef MIME_GPU_H
#define MIME_GPU_H

#include <stdint.h>

#define MIME_GPU_ALIGN 16384u              /* host page size, required by Metal for buffers over guest memory. */
#define MIME_GPU_HEADER_SIZE 16384u
#define MIME_GPU_RING_SIZE (4u << 20)
#define MIME_GPU_CHANNEL_SIZE (32u << 20)  /* heap block 0: the header, the ring, then heap. */
#define MIME_GPU_MAX_BLOCKS 64
#define MIME_GPU_MAX_BLOCK_SIZE (64u << 20)
#define MIME_GPU_MAX_RECORD (1u << 20)

#define MIME_GPU_MAX_ATTRIBS 16
#define MIME_GPU_MAX_COLORS 8
#define MIME_GPU_MAX_TEXTURES 32
#define MIME_GPU_VERTEX_BUFFER_BASE 14     /* Metal buffer index of vertex buffer slot 0, uniforms use index 0. */
#define MIME_GPU_UNIFORM_BUFFER 0
#define MIME_GPU_MAX_UNIFORM_BLOCKS 12

typedef struct MimeGpuHeader {
    /* written by the guest. */
    volatile uint64_t put;          /* bytes written to the ring since creation. */
    volatile uint64_t wait_fence;   /* a thread sleeps until this fence, 0 when none. */
    volatile uint64_t want_fence;   /* highest fence a thread waits for, so the host commits it early. */
    uint64_t guest_pad[5];
    /* written by the host. */
    volatile uint64_t get;          /* bytes executed. */
    volatile uint64_t done_fence;   /* last fence the GPU completed. */
    volatile uint32_t host_idle;    /* the host sleeps until kicked. */
    volatile uint32_t lost;         /* the host stopped executing this channel after an error. */
    volatile uint64_t features;     /* MIME_GPU_FEATURE_*, written before the channel is handed to the guest. */
    uint64_t host_pad[4];
} MimeGpuHeader;

/* a record is a MimeGpuCmd header followed by its body, padded to 8 bytes. */
typedef struct MimeGpuCmd {
    uint32_t op;
    uint32_t size; /* bytes, header included, multiple of 8. */
} MimeGpuCmd;

enum {
    MIME_GPU_NOP = 0,             /* skipped, used to pad the end of the ring. */
    MIME_GPU_FENCE,               /* MimeGpuFence: commits and signals done_fence when the GPU reaches it. */
    MIME_GPU_FLUSH,               /* commits the work recorded so far. */
    MIME_GPU_TEXTURE_CREATE,      /* MimeGpuTextureCreate */
    MIME_GPU_TEXTURE_DESTROY,     /* MimeGpuObject */
    MIME_GPU_TEXTURE_UPLOAD,      /* MimeGpuTextureCopy: heap to texture. */
    MIME_GPU_TEXTURE_READ,        /* MimeGpuTextureCopy: texture to heap. */
    MIME_GPU_TEXTURE_MIPMAPS,     /* MimeGpuObject */
    MIME_GPU_BUFFER_COPY,         /* MimeGpuBufferCopy */
    MIME_GPU_PROGRAM_CREATE,      /* MimeGpuProgramCreate */
    MIME_GPU_PROGRAM_DESTROY,     /* MimeGpuObject */
    MIME_GPU_PIPELINE_CREATE,     /* MimeGpuPipelineCreate */
    MIME_GPU_DEPTH_STENCIL_CREATE,/* MimeGpuDepthStencilCreate */
    MIME_GPU_SAMPLER_CREATE,      /* MimeGpuSamplerCreate */
    MIME_GPU_SURFACE_CONFIG,      /* MimeGpuSurfaceConfig */
    MIME_GPU_SURFACE_DESTROY,     /* MimeGpuObject */
    MIME_GPU_PRESENT,             /* MimeGpuObject: the surface's back buffer goes to its window. */
    MIME_GPU_PASS_BEGIN,          /* MimeGpuPass */
    MIME_GPU_PASS_END,
    MIME_GPU_CLEAR,               /* MimeGpuClear: inside a pass, through the scissor. */
    MIME_GPU_BLIT,                /* MimeGpuBlit: outside passes. */
    MIME_GPU_SET_PIPELINE,        /* MimeGpuObject */
    MIME_GPU_SET_DEPTH_STENCIL,   /* MimeGpuSetDepthStencil */
    MIME_GPU_SET_RASTER,          /* MimeGpuSetRaster */
    MIME_GPU_SET_VIEWPORT,        /* MimeGpuSetViewport */
    MIME_GPU_SET_SCISSOR,         /* MimeGpuRect */
    MIME_GPU_SET_BLEND_COLOR,     /* MimeGpuSetBlendColor */
    MIME_GPU_SET_VERTEX_BUFFER,   /* MimeGpuSetVertexBuffer */
    MIME_GPU_SET_UNIFORMS,        /* MimeGpuSetUniforms followed by the data. */
    MIME_GPU_SET_TEXTURE,         /* MimeGpuSetTexture */
    MIME_GPU_DRAW,                /* MimeGpuDraw */
    MIME_GPU_DRAW_INDEXED,        /* MimeGpuDrawIndexed */
    MIME_GPU_SET_UNIFORM_BUFFER,
    MIME_GPU_TEXTURE_SWIZZLE,
    /* 64 to 127: MIME_GPU_FEATURE_COMPUTE. */
    MIME_GPU_COMPUTE_PROGRAM_CREATE = 64, /* MimeGpuProgramCreate: the compute source is in vertex_bytes. */
    MIME_GPU_DISPATCH,                    /* MimeGpuDispatch */
    MIME_GPU_SET_STORAGE_BUFFER,          /* MimeGpuSetStorageBuffer */
    MIME_GPU_SET_IMAGE,                   /* MimeGpuSetImage */
    MIME_GPU_MEMORY_BARRIER,              /* MimeGpuCmd: every write reaches every later access. */
    MIME_GPU_COPY_IMAGE,                  /* MimeGpuCopyImage */
    MIME_GPU_OP_COUNT
};

/*
 * what the host can run beyond the commands above. a host that sets none of them is a plain vertex and fragment
 * machine, and the guest must not send it a command of a range it did not announce.
 *
 * opcodes 64 to 127 belong to compute, storage buffers, images and indirect draws, 128 to 191 to geometry and
 * tessellation shaders and transform feedback.
 */
enum {
    MIME_GPU_FEATURE_COMPUTE = 1u << 0,   /* compute programs, storage buffers, atomic counters, image load and store. */
    MIME_GPU_FEATURE_INDIRECT = 1u << 1,  /* indirect draws and dispatches, multi draws. */
    MIME_GPU_FEATURE_GEOMETRY = 1u << 2,
    MIME_GPU_FEATURE_TESSELLATION = 1u << 3,
    MIME_GPU_FEATURE_FEEDBACK = 1u << 4,  /* transform feedback. */
    MIME_GPU_FEATURE_VIEW = 1u << 5,      /* texture views. */
};

#define MIME_GPU_FEATURE_QUERY (1ull << 32)     /* QUERY_BEGIN and QUERY_END: occlusion and feedback counters. */
#define MIME_GPU_FEATURE_CUBE_ARRAY (1ull << 36) /* cube map array textures. */
#define MIME_GPU_FEATURE_DUAL_BLEND (1ull << 35) /* the second color output of a fragment shader as a blend factor. */
#define MIME_GPU_FEATURE_FP64 (1ull << 34)      /* 64 bit floats in shaders and vertex attributes. */
#define MIME_GPU_FEATURE_VIEWPORTS (1ull << 33) /* more than one viewport and scissor, and the indexed commands. */

#define MIME_GPU_FEATURE_TEXEL_BUFFER (1ull << 48) /* texture buffers: TEXTURE_BUFFER. */
#define MIME_GPU_FEATURE_TEXTURE_VIEW (1ull << 49) /* TEXTURE_VIEW. */

/* 192 to 255: texture buffers and views. */
enum {
    MIME_GPU_TEXTURE_BUFFER = 192, /* MimeGpuTextureBuffer */
    MIME_GPU_TEXTURE_VIEW,         /* MimeGpuTextureView */
};

enum {
    MIME_GPU_PROGRAM_STAGES = 128, /* MimeGpuProgramStages: more stages for the PROGRAM_CREATE of the same id that follows. */
    MIME_GPU_XFB_BUFFERS,          /* MimeGpuXfbBuffers */
    MIME_GPU_XFB_CONTROL,          /* MimeGpuXfbControl */
    MIME_GPU_QUERY_BEGIN,          /* MimeGpuQuery */
    MIME_GPU_QUERY_END,            /* MimeGpuQuery */
    MIME_GPU_SET_VIEWPORT_INDEXED, /* MimeGpuSetViewportIndexed */
    MIME_GPU_SET_SCISSOR_INDEXED,  /* MimeGpuSetScissorIndexed */
    MIME_GPU_DRAW_XFB,             /* MimeGpuDrawXfb */
};

#define MIME_GPU_MAX_XFB_BUFFERS 4
#define MIME_GPU_MAX_VIEWPORTS 16

/* a viewport with a negative height flips the rows, and MimeGpuSetRaster says when depth runs 0 to 1: glClipControl. */
#define MIME_GPU_FEATURE_CLIP_CONTROL (1ull << 40)

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
    uint32_t width, height, depth; /* depth is the slice count of arrays and cubes too. */
    uint32_t levels;
    uint32_t samples;
    uint32_t usage;
    uint32_t swizzle; /* four Metal swizzle bytes, red first. */
} MimeGpuTextureCreate;

/* a texture id that reads texels from a range of the heap: the id is replaced when it had an image or another range. */
typedef struct MimeGpuTextureBuffer {
    MimeGpuCmd cmd;
    uint32_t id;
    uint32_t format; /* MTLPixelFormat */
    MimeGpuMem mem;
    uint64_t size;
} MimeGpuTextureBuffer;

/* a texture id that reads the levels and layers of another texture's storage as another type and format. */
typedef struct MimeGpuTextureView {
    MimeGpuCmd cmd;
    uint32_t id, source;
    uint32_t type;   /* MTLTextureType */
    uint32_t format; /* MTLPixelFormat */
    uint32_t min_level, levels;
    uint32_t min_layer, layers;
    uint32_t swizzle;
    uint32_t pad;
} MimeGpuTextureView;

typedef struct MimeGpuTextureSwizzle {
    MimeGpuCmd cmd;
    uint32_t id;
    uint32_t swizzle;
} MimeGpuTextureSwizzle;

/*
 * copy source or destination: a texture, surface color buffer or depth/stencil buffer, with a separate
 * stencil selection.
 */
enum {
    MIME_GPU_COPY_TEXTURE = 0,
    MIME_GPU_COPY_SURFACE = 1,
    MIME_GPU_COPY_SURFACE_DEPTH = 2,
    MIME_GPU_COPY_STENCIL = 0x100, /* the stencil of a depth and stencil format, not its depth. */
};

/* a region of a texture and its rows in the heap. */
typedef struct MimeGpuTextureCopy {
    MimeGpuCmd cmd;
    uint32_t id;      /* texture, or the surface when surface is set. */
    uint32_t surface; /* MIME_GPU_COPY_* */
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
 * the host compiles GLSL while the ring continues, then writes MimeGpuProgramInfo at result with size last.
 * the guest polls for nonzero size. the body contains vertex source, fragment source and "name\0location\0"
 * attribute pairs, sized below.
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
} MimeGpuProgramCreate;

/*
 * the stages of a program besides the vertex and fragment ones, and the varyings transform feedback captures. the body is
 * the tessellation control, tessellation evaluation and geometry sources, then the names of the varyings, each ended by a
 * NUL. a name may be gl_NextBuffer or gl_SkipComponents1 to 4. the PROGRAM_CREATE of the same id follows at once.
 */
typedef struct MimeGpuProgramStages {
    MimeGpuCmd cmd;
    uint32_t id;
    uint32_t control_bytes, evaluation_bytes, geometry_bytes;
    uint32_t feedback_bytes;
    uint32_t feedback_separate; /* the varyings go to buffers of their own. */
    uint32_t fragment_bytes;    /* "name\0location\0index\0" for each output the application placed, after the varyings. */
    uint32_t pad;
} MimeGpuProgramStages;

/* what the host found in the extra stages, appended to the program info. */
typedef struct MimeGpuStageInfo {
    uint32_t stages;            /* bit 0 vertex, 1 fragment, 2 tess control, 3 tess evaluation, 4 geometry. */
    uint32_t geometry_input;    /* GL_POINTS, GL_LINES, GL_LINES_ADJACENCY, GL_TRIANGLES or GL_TRIANGLES_ADJACENCY. */
    uint32_t geometry_output;   /* GL_POINTS, GL_LINE_STRIP or GL_TRIANGLE_STRIP. */
    uint32_t geometry_vertices; /* max_vertices. */
    uint32_t geometry_invocations;
    uint32_t control_vertices;  /* vertices of a patch the control shader writes. */
    uint32_t evaluation_mode;   /* GL_TRIANGLES, GL_QUADS or GL_ISOLINES. */
    uint32_t evaluation_spacing;
    uint32_t evaluation_ccw;
    uint32_t evaluation_point_mode;
    uint32_t feedback_count, feedback_offset; /* MimeGpuFeedbackInfo array. */
    uint32_t feedback_stride[MIME_GPU_MAX_XFB_BUFFERS]; /* bytes of a vertex in each buffer. */
    uint32_t feedback_buffers;
} MimeGpuStageInfo;

typedef struct MimeGpuFeedbackInfo {
    uint32_t name_offset;
    uint32_t gl_type;
    uint32_t size;      /* array elements. */
    uint32_t buffer;
} MimeGpuFeedbackInfo;

typedef struct MimeGpuProgramInfo {
    uint32_t status;        /* 0 linked, 1 failed: the log says why. */
    uint32_t size;          /* bytes written, 0 while pending. */
    uint32_t log_offset, log_bytes;
    uint32_t attrib_count, attrib_offset;   /* MimeGpuAttribInfo array. */
    uint32_t uniform_count, uniform_offset; /* MimeGpuUniformInfo array. */
    uint32_t uniform_bytes;                 /* size of the uniform block. */
    uint32_t pad;
    uint32_t block_count, block_offset;
    MimeGpuStageInfo stage;
    uint32_t storage_count, storage_offset; /* MimeGpuStorageBlockInfo array. */
    uint32_t local_size[3];                 /* compute: the work group size. */
    uint32_t counter_count, counter_offset; /* MimeGpuCounterInfo array. */
} MimeGpuProgramInfo;

/* a shader storage block. the declared size leaves out a trailing unsized array. */
typedef struct MimeGpuStorageBlockInfo {
    uint32_t name_offset;
    uint32_t bytes;
    uint32_t stages;
    int32_t binding; /* layout(binding), -1 without one. */
    uint32_t variables;
    uint32_t atomic; /* the block holds the atomic counters of the binding, see MimeGpuCounterInfo. */
} MimeGpuStorageBlockInfo;

/* an atomic counter buffer: the counters share a binding and sit at offsets of their own. */
typedef struct MimeGpuCounterInfo {
    uint32_t name_offset;
    uint32_t binding;
    uint32_t offset;
    uint32_t stages;
    uint32_t array_size;
    uint32_t pad;
} MimeGpuCounterInfo;

typedef struct MimeGpuUniformBlockInfo {
    uint32_t name_offset;
    uint32_t bytes;
    uint32_t stages;
    uint32_t pad;
} MimeGpuUniformBlockInfo;

typedef struct MimeGpuAttribInfo {
    uint32_t name_offset; /* from the info, NUL terminated. */
    uint32_t location;
    uint32_t gl_type;
    uint32_t array_size;
} MimeGpuAttribInfo;

enum { MIME_GPU_STAGE_VERTEX = 0, MIME_GPU_STAGE_FRAGMENT = 1, MIME_GPU_STAGES = 2 };

typedef struct MimeGpuUniformInfo {
    uint32_t name_offset; /* "name" or "name[0]" for arrays. */
    uint32_t gl_type;
    uint32_t array_size;
    uint32_t offset;      /* in the uniform block, samplers have none. */
    uint32_t array_stride;
    uint32_t matrix_stride;
    int32_t texture[MIME_GPU_STAGES]; /* samplers: texture and sampler index of element 0, -1 if unused. images: the
                                         image slot in texture[0]. */
    int32_t binding;                  /* samplers and images: layout(binding), -1 without one. */
    uint32_t block;                   /* a member of a named uniform block: the block's index plus one, else 0. */
} MimeGpuUniformInfo;

typedef struct MimeGpuVertexAttrib {
    uint8_t format; /* MTLVertexFormat, 0 when unused. */
    uint8_t slot;   /* vertex buffer slot. */
    uint16_t offset;
} MimeGpuVertexAttrib;

typedef struct MimeGpuVertexLayout {
    uint32_t stride; /* 0: the value is constant. */
    uint32_t instance_divisor; /* 0 per vertex. */
} MimeGpuVertexLayout;

typedef struct MimeGpuColorTarget {
    uint16_t format; /* MTLPixelFormat, 0 when unused. */
    uint8_t write_mask; /* MTLColorWriteMask */
    uint8_t blend;
    uint8_t rgb_op, alpha_op;
    uint8_t src_rgb, dst_rgb, src_alpha, dst_alpha;
    uint8_t logic_op; /* 0 for none, else 1 and the number of the operation counted from GL_CLEAR; blending is off then. */
    uint8_t pad[5];
} MimeGpuColorTarget;

typedef struct MimeGpuPipelineDesc {
    uint32_t program;
    uint32_t program_serial;
    uint16_t depth_format, stencil_format;
    uint32_t samples;
    uint32_t topology; /* MTLPrimitiveTopologyClass; 4 is patches, whose size is in bits 8 to 15. */
    MimeGpuColorTarget color[MIME_GPU_MAX_COLORS];
    MimeGpuVertexAttrib attribs[MIME_GPU_MAX_ATTRIBS]; /* by location. */
    MimeGpuVertexLayout layouts[MIME_GPU_MAX_ATTRIBS]; /* by slot. */
} MimeGpuPipelineDesc;

typedef struct MimeGpuPipelineCreate {
    MimeGpuCmd cmd;
    uint32_t id;
    uint32_t pad;
    MimeGpuPipelineDesc desc;
} MimeGpuPipelineCreate;

typedef struct MimeGpuStencilFace {
    uint8_t func, fail, depth_fail, pass; /* MTLCompareFunction, MTLStencilOperation. */
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
    uint8_t min_filter, mag_filter, mip_filter; /* MTLSamplerMinMagFilter, MTLSamplerMipFilter. */
    uint8_t address_s, address_t, address_r;  /* MTLSamplerAddressMode */
    uint8_t compare; /* MTLCompareFunction, 0 without comparison. */
    uint8_t border;  /* MTLSamplerBorderColor */
    uint8_t max_anisotropy;
    uint8_t pad[3];
    float lod_min, lod_max;
} MimeGpuSamplerCreate;

/* surface rectangle in window-relative guest pixels. */
typedef struct MimeGpuSurfaceConfig {
    MimeGpuCmd cmd;
    uint32_t id;
    uint32_t window_id; /* host window, 0 to hide. */
    int32_t x, y;
    uint32_t width, height;
    uint16_t color_format, depth_format, stencil_format;
    uint16_t sync;  /* wait for the display when presenting. */
    uint32_t samples;
    uint32_t source; /* the window the surface is for, a child of the host window or the host window itself. */
    uint32_t order;  /* its place in the paint order of the host window's tree, the front ones last. */
    int32_t visible[4]; /* left, top, right and bottom of the part of the client area that shows, window-relative. */
    /*
     * counts what the guest tells of the places of windows, here and in MimeWinLayer, never 0. this one is read
     * after all that was drawn before it, so a later notice of the window's layer may have come first.
     */
    uint32_t stamp;
} MimeGpuSurfaceConfig;

typedef struct MimeGpuAttachment {
    uint32_t id;       /* texture, 0 when unused. */
    uint8_t surface;   /* id is a surface: its color, depth or stencil buffer. */
    uint8_t load;      /* MTLLoadAction */
    uint8_t store;     /* MTLStoreAction */
    uint8_t pad;
    uint32_t level, slice;
    uint32_t resolve;  /* multisample resolve texture, 0 if none. */
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
    uint32_t color_mask; /* MTLColorWriteMask for every target. */
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
    uint32_t mask;   /* MIME_GPU_CLEAR_* buffers. */
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
    uint8_t winding;   /* MTLWinding of front faces as rendered. */
    uint8_t fill;      /* MTLTriangleFillMode */
    uint8_t depth_clip; /* bit 0 clips depth, bit 1 discards primitives before rasterization. */
    uint8_t zero_to_one; /* clip space depth runs 0 to 1 and not -1 to 1, with MIME_GPU_FEATURE_CLIP_CONTROL. */
    uint8_t pad[3];
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
    uint32_t bytes; /* followed by the data. */
    uint32_t pad;
} MimeGpuSetUniforms;

typedef struct MimeGpuSetUniformBuffer {
    MimeGpuCmd cmd;
    uint32_t slot, stages;
    MimeGpuMem mem;
} MimeGpuSetUniformBuffer;

typedef struct MimeGpuSetTexture {
    MimeGpuCmd cmd;
    uint8_t stage;
    uint8_t index;
    uint8_t surface;  /* id is a surface: its color buffer. */
    uint8_t pad;
    uint32_t id;      /* texture, 0 unbinds. */
    uint32_t sampler;
    uint32_t pad2;
} MimeGpuSetTexture;

typedef struct MimeGpuDraw {
    MimeGpuCmd cmd;
    uint32_t primitive; /* MTLPrimitiveType; 6 to 10 are Vulkan's adjacency and patch topologies when the host has geometry. */
    uint32_t first, count;
    uint32_t instances, base_instance;
    uint32_t pad;
} MimeGpuDraw;

typedef struct MimeGpuSetViewportIndexed {
    MimeGpuCmd cmd;
    uint32_t index;
    float x, y, width, height, znear, zfar;
    uint32_t pad;
} MimeGpuSetViewportIndexed;

typedef struct MimeGpuSetScissorIndexed {
    MimeGpuCmd cmd;
    uint32_t index;
    int32_t x, y;
    uint32_t width, height;
    uint32_t pad;
} MimeGpuSetScissorIndexed;

/* the buffers transform feedback writes, by binding index; a size of 0 leaves the binding empty. */
typedef struct MimeGpuXfbBuffers {
    MimeGpuCmd cmd;
    uint32_t count;
    uint32_t pad;
    struct {
        MimeGpuMem mem;
        uint64_t size;
    } buffer[MIME_GPU_MAX_XFB_BUFFERS];
} MimeGpuXfbBuffers;

enum { MIME_GPU_XFB_BEGIN = 0, MIME_GPU_XFB_PAUSE, MIME_GPU_XFB_RESUME, MIME_GPU_XFB_END };

/* the object holds the byte counters of its buffers, which a later draw of the captured vertices reads. */
typedef struct MimeGpuXfbControl {
    MimeGpuCmd cmd;
    uint32_t action;
    uint32_t object;
} MimeGpuXfbControl;

enum {
    MIME_GPU_QUERY_SAMPLES = 0,   /* samples passed, exact. */
    MIME_GPU_QUERY_ANY_SAMPLES,   /* any samples passed: 0 or 1. */
    MIME_GPU_QUERY_PRIMITIVES,    /* primitives generated by a stream. */
    MIME_GPU_QUERY_WRITTEN,       /* primitives a stream wrote to the feedback buffers. */
    MIME_GPU_QUERY_OVERFLOW,      /* 1 when a stream had more primitives than the buffers took. */
};

/* the result is two quadwords at result: the value, then 1 once it is there. begin sets the type and the stream. */
typedef struct MimeGpuQuery {
    MimeGpuCmd cmd;
    uint32_t id;
    uint32_t type;
    uint32_t stream;
    uint32_t pad;
    MimeGpuMem result;
} MimeGpuQuery;

typedef struct MimeGpuDrawXfb {
    MimeGpuCmd cmd;
    uint32_t primitive;
    uint32_t object;
    uint32_t buffer;
    uint32_t stride;
    uint32_t instances;
    uint32_t pad;
} MimeGpuDrawXfb;
#define MIME_GPU_MAX_STORAGE_BLOCKS 16
#define MIME_GPU_MAX_IMAGES 16
#define MIME_GPU_MAX_ATOMIC_BINDINGS 8 /* atomic counter buffer binding points. */

typedef struct MimeGpuDispatch {
    MimeGpuCmd cmd;
    uint32_t program;
    uint32_t x, y, z;
} MimeGpuDispatch;

/* the block slot is the index of the block in the program, as for uniform blocks. */
typedef struct MimeGpuSetStorageBuffer {
    MimeGpuCmd cmd;
    uint32_t slot, pad;
    MimeGpuMem mem;
    uint64_t size; /* 0 unbinds. */
} MimeGpuSetStorageBuffer;

typedef struct MimeGpuSetImage {
    MimeGpuCmd cmd;
    uint32_t slot;
    uint32_t id;    /* texture, 0 unbinds. */
    uint32_t level;
    uint32_t layer;
    uint32_t layered;
    uint32_t pad;
} MimeGpuSetImage;

/* a region between two images of one format. z is a slice of a 3d image and a layer of an array or a cube. */
typedef struct MimeGpuCopyImage {
    MimeGpuCmd cmd;
    uint32_t src, dst;
    uint32_t src_level, dst_level;
    uint32_t src_x, src_y, src_z;
    uint32_t dst_x, dst_y, dst_z;
    uint32_t width, height, depth;
    uint32_t src_3d, dst_3d;
} MimeGpuCopyImage;

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
