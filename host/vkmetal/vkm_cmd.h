/* the command list between recording and encoding, and the state draws are resolved from. */
#ifndef MIME_VKMETAL_VKM_CMD_H
#define MIME_VKMETAL_VKM_CMD_H

#include "vkm.h"

/* ------------------------------------------------------------------ dynamic state. */

enum {
    VKM_DYN_VERTEX = 1 << 0,     /* vertex input and strides. */
    VKM_DYN_TOPOLOGY = 1 << 1,
    VKM_DYN_RASTER = 1 << 2,     /* cull, front face, polygon mode, discard, depth clamp and clip. */
    VKM_DYN_BIAS = 1 << 3,
    VKM_DYN_DEPTH = 1 << 4,      /* depth test, write, compare and bounds. */
    VKM_DYN_STENCIL = 1 << 5,    /* stencil test and ops. */
    VKM_DYN_STENCIL_MASKS = 1 << 6,
    VKM_DYN_STENCIL_REF = 1 << 7,
    VKM_DYN_VIEWPORT = 1 << 8,
    VKM_DYN_SCISSOR = 1 << 9,
    VKM_DYN_BLEND = 1 << 10,     /* enables, equations and write masks. */
    VKM_DYN_BLEND_CONSTANTS = 1 << 11,
    VKM_DYN_MULTISAMPLE = 1 << 12,
    VKM_DYN_ALL = (1 << 13) - 1,
};

/* a depth clip enable no one set follows the depth clamp, as vulkan says. */
static constexpr uint8_t VKM_CLIP_UNSET = 2;

typedef struct VkmDynamic {
    VkmVertexInput vertex;
    uint32_t strides[VKM_VERTEX_BINDINGS];
    uint8_t topology, restart, discard, cull, front, polygon, depth_clamp, depth_clip;
    uint8_t bias_enable, depth_test, depth_write, depth_compare, bounds_test, stencil_test;
    uint8_t samples, alpha_to_coverage, alpha_to_one, pad;
    float bias_constant, bias_clamp, bias_slope;
    float bounds_min, bounds_max;
    VkmStencilFace front_stencil, back_stencil;
    uint32_t ref_front, ref_back;
    uint32_t viewport_count, scissor_count;
    VkViewport viewports[VKM_VIEWPORTS];
    VkRect2D scissors[VKM_VIEWPORTS];
    VkmBlend blend[VKM_COLORS];
    float blend_constants[4];
    uint32_t sample_mask;
} VkmDynamic;

/* the Vulkan dynamic states, as bits of a pipeline's mask. */
enum {
    VKM_D_VIEWPORT,
    VKM_D_SCISSOR,
    VKM_D_DEPTH_BIAS,
    VKM_D_BLEND_CONSTANTS,
    VKM_D_DEPTH_BOUNDS,
    VKM_D_STENCIL_COMPARE_MASK,
    VKM_D_STENCIL_WRITE_MASK,
    VKM_D_STENCIL_REFERENCE,
    VKM_D_CULL_MODE,
    VKM_D_FRONT_FACE,
    VKM_D_TOPOLOGY,
    VKM_D_VIEWPORT_COUNT,
    VKM_D_SCISSOR_COUNT,
    VKM_D_STRIDE,
    VKM_D_DEPTH_TEST,
    VKM_D_DEPTH_WRITE,
    VKM_D_DEPTH_COMPARE,
    VKM_D_BOUNDS_TEST,
    VKM_D_STENCIL_TEST,
    VKM_D_STENCIL_OP,
    VKM_D_DISCARD,
    VKM_D_BIAS_ENABLE,
    VKM_D_RESTART,
    VKM_D_VERTEX_INPUT,
    VKM_D_DEPTH_CLAMP,
    VKM_D_POLYGON_MODE,
    VKM_D_SAMPLES,
    VKM_D_SAMPLE_MASK,
    VKM_D_ALPHA_TO_COVERAGE,
    VKM_D_ALPHA_TO_ONE,
    VKM_D_BLEND_ENABLE,
    VKM_D_BLEND_EQUATION,
    VKM_D_WRITE_MASK,
    VKM_D_DEPTH_CLIP,
    VKM_D_LINE_WIDTH,
};

/* the dynamic state bit of a Vulkan dynamic state, -1 for those the driver ignores. */
int vkm_dynamic_bit(VkDynamicState state);

/* what a graphics pipeline fixes: the state outside its dynamic states. */
struct VkmGfxStatic {
    uint64_t dynamic; /* 1 << VKM_D_* for each dynamic state. */
    VkmDynamic state;
};

/* ------------------------------------------------------------------ commands. */

enum {
    VKM_OP_BEGIN_RENDER = 1,
    VKM_OP_END_RENDER,
    VKM_OP_PIPELINE,
    VKM_OP_DEPTH_STATE,
    VKM_OP_RASTER,
    VKM_OP_VIEWPORTS,
    VKM_OP_SCISSORS,
    VKM_OP_STENCIL_REF,
    VKM_OP_BLEND_CONSTANTS,
    VKM_OP_BUFFER,         /* a render argument table address. */
    VKM_OP_DRAW,
    VKM_OP_DRAW_INDEXED,
    VKM_OP_DRAW_INDIRECT,
    VKM_OP_DRAW_INDEXED_INDIRECT,
    VKM_OP_CLEAR_RECTS,    /* a clear of attachments inside a render pass. */
    VKM_OP_VISIBILITY,
    VKM_OP_COMPUTE_PIPELINE,
    VKM_OP_COMPUTE_BUFFER, /* a compute argument table address. */
    VKM_OP_DISPATCH,
    VKM_OP_DISPATCH_INDIRECT,
    VKM_OP_COPY_BUFFER,
    VKM_OP_COPY_BUFFER_IMAGE,
    VKM_OP_COPY_IMAGE,
    VKM_OP_FILL_BUFFER,
    VKM_OP_BLIT,
    VKM_OP_BARRIER,
    VKM_OP_TIMESTAMP,
    VKM_OP_QUERY_RESET,
    VKM_OP_QUERY_COPY,
    VKM_OP_EVENT,
    VKM_OP_GENERATE,       /* generated commands encoded into an indirect command buffer. */
    VKM_OP_EXECUTE,        /* and run. */
    VKM_OP_VERTEX_BARRIER, /* what vertex functions wrote, seen by the next ones of the render pass. */
    VKM_OP_STATS,          /* pipeline statistics the host counts. */
    VKM_OP_AS_BUILD,
    VKM_OP_AS_COPY,
    VKM_OP_AS_PROPERTY,    /* a query of a structure's size. */
};

typedef struct VkmOp {
    uint32_t op, size; /* size includes this header. */
} VkmOp;

typedef struct VkmAttach {
    id<MTLTexture> texture, resolve;
    uint32_t level, slice, resolve_level, resolve_slice;
    uint8_t load, store; /* MTLLoadAction, MTLStoreAction. */
    uint8_t resolve_filter;
    uint8_t pad;
    MTLClearColor clear;
} VkmAttach;

typedef struct VkmBeginRender {
    VkmOp head;
    uint32_t width, height, layers, samples;
    uint32_t color_count;
    VkmAttach colors[VKM_COLORS];
    VkmAttach depth, stencil;
    id<MTLBuffer> visibility; /* the occlusion query pool's buffer. */
} VkmBeginRender;

/* a render pipeline state, or a depth stencil state. */
typedef struct VkmOpObject {
    VkmOp head;
    id object;
} VkmOpObject;

typedef struct VkmOpRaster {
    VkmOp head;
    uint32_t cull, winding, fill, clip;
    float bias_constant, bias_slope, bias_clamp;
} VkmOpRaster;

typedef struct VkmOpViewports {
    VkmOp head;
    uint32_t count;
    MTLViewport viewports[VKM_VIEWPORTS];
} VkmOpViewports;

typedef struct VkmOpScissors {
    VkmOp head;
    uint32_t count;
    VkRect2D rects[VKM_VIEWPORTS];
} VkmOpScissors;

typedef struct VkmOpValues {
    VkmOp head;
    uint32_t u[4];
    float f[4];
} VkmOpValues;

typedef struct VkmOpBuffer {
    VkmOp head;
    uint32_t index, stride; /* stride ~0 when the binding has none. */
    uint64_t address;
} VkmOpBuffer;

typedef struct VkmOpDraw {
    VkmOp head;
    uint32_t primitive, index_type;
    uint32_t count, instances, first, base_instance;
    int32_t base_vertex;
    uint32_t draws, stride;
    uint64_t index, index_length, indirect;
} VkmOpDraw;

typedef struct VkmOpClearRects {
    VkmOp head;
    id<MTLRenderPipelineState> pipeline;
    id<MTLDepthStencilState> depth_state;
    uint32_t rect_count;
    uint64_t values; /* gpu address of the colors, then the depth. */
    VkRect2D rects[16];
    uint32_t base_layer[16], layer_count[16];
    uint32_t stencil;
} VkmOpClearRects;

typedef struct VkmOpVisibility {
    VkmOp head;
    uint32_t mode, offset;
} VkmOpVisibility;

typedef struct VkmOpCompute {
    VkmOp head;
    id<MTLComputePipelineState> pipeline;
    MTLSize local;
} VkmOpCompute;

typedef struct VkmOpDispatch {
    VkmOp head;
    uint32_t x, y, z;
    uint64_t indirect;
} VkmOpDispatch;

typedef struct VkmOpCopyBuffer {
    VkmOp head;
    id<MTLBuffer> src, dst;
    uint64_t src_offset, dst_offset, size;
} VkmOpCopyBuffer;

typedef struct VkmOpCopyBufferImage {
    VkmOp head;
    bool to_image;
    id<MTLBuffer> buffer;
    id<MTLTexture> texture;
    uint64_t offset, row, image;
    uint32_t slice, level, options;
    MTLOrigin origin;
    MTLSize size;
} VkmOpCopyBufferImage;

typedef struct VkmOpCopyImage {
    VkmOp head;
    id<MTLTexture> src, dst;
    uint32_t src_slice, src_level, dst_slice, dst_level;
    MTLOrigin src_origin, dst_origin;
    MTLSize size;
} VkmOpCopyImage;

typedef struct VkmOpFill {
    VkmOp head;
    id<MTLBuffer> buffer;
    uint64_t offset, size;
    uint32_t value;
    uint64_t gpu;
} VkmOpFill;

/* a scaled copy drawn into a render pass of its own. */
typedef struct VkmOpBlit {
    VkmOp head;
    id<MTLRenderPipelineState> pipeline;
    id<MTLDepthStencilState> depth_state;
    id<MTLTexture> src, dst;
    uint32_t dst_level, dst_slice, src_level, src_slice;
    bool depth, stencil;
    uint32_t dst_width, dst_height;
    VkRect2D rect;
    uint64_t params; /* gpu address of the shaders' VkmBlitParams. */
    id<MTLSamplerState> sampler;
} VkmOpBlit;

/* what a blit's shaders read: the normalized source corners, the source layer or 3D depth, and level. */
typedef struct VkmBlitParams {
    float src[4];
    float layer, lod;
    float pad[2];
} VkmBlitParams;

typedef struct VkmOpBarrier {
    VkmOp head;
    uint64_t after, before; /* MTLStages. */
} VkmOpBarrier;

/* what a draw or dispatch adds to a pipeline statistics query, by VkQueryPipelineStatisticFlagBits bit. */
#define VKM_STATS 11

typedef struct VkmOpStats {
    VkmOp head;
    VkQueryPool pool;
    uint32_t query;
    uint64_t add[VKM_STATS];
} VkmOpStats;

typedef struct VkmOpGenerate {
    VkmOp head;
    id<MTLComputePipelineState> pipeline;
    uint32_t threads;
    uint64_t params, icb; /* gpu addresses of the VkmDgcParams and of the buffer's resource id. */
} VkmOpGenerate;

/* a geometry of a build, its addresses those of the range it builds. */
typedef struct VkmAsGeometry {
    uint8_t kind; /* VkGeometryTypeKHR. */
    uint8_t opaque, no_duplicates;
    uint8_t index_type; /* 0 without indices, else MTLIndexType plus 1. */
    uint32_t vertex_format; /* MTLAttributeFormat. */
    uint32_t count;
    uint64_t vertex, vertex_stride, vertex_length;
    uint64_t index, index_length;
    uint64_t transform;
} VkmAsGeometry;

typedef struct VkmOpAsBuild {
    VkmOp head;
    VkAccelerationStructureKHR dst, src; /* src for an update. */
    uint64_t scratch, scratch_size;
    uint32_t usage; /* MTLAccelerationStructureUsage. */
    bool update, top;
    uint64_t instances; /* top level: the metal instances the conversion writes, in the scratch past metal's. */
    uint64_t instance_counts; /* an indirect build's count, of which instance_count is the most. */
    uint32_t instance_count;
    uint32_t geometry_count;
    VkmAsGeometry geometries[];
} VkmOpAsBuild;

typedef struct VkmOpAsCopy {
    VkmOp head;
    VkAccelerationStructureKHR src, dst;
    bool compact;
} VkmOpAsCopy;

typedef struct VkmOpAsProperty {
    VkmOp head;
    VkAccelerationStructureKHR as;
    VkQueryPool pool;
    uint32_t query;
    VkQueryType type;
} VkmOpAsProperty;

/* accel.m: what the encoder runs of the ops above, in a compute encoder. */
void vkm_encode_as_build(VkDevice device, id<MTL4ComputeCommandEncoder> c, const VkmOpAsBuild *o);
void vkm_encode_as_copy(id<MTL4ComputeCommandEncoder> c, const VkmOpAsCopy *o);
void vkm_encode_as_property(id<MTL4ComputeCommandEncoder> c, const VkmOpAsProperty *o, VkQueue queue,
                            uint64_t value);
id<MTLComputePipelineState> vkm_meta_instances(VkDevice device);

typedef struct VkmOpExecute {
    VkmOp head;
    id<MTLIndirectCommandBuffer> icb;
    bool dispatch;
    uint32_t chunks;
    uint64_t ranges; /* gpu address of the chunks' MTLIndirectCommandBufferExecutionRange. */
} VkmOpExecute;

/* the most commands one execution range runs. */
#define VKM_DGC_CHUNK 16384u

/* what the stream output vertex function reads, laid out as meta.c's Xfb. */
typedef struct VkmXfbParams {
    uint64_t emu, index, buffers[4], counters[4], query, state;
    uint32_t sizes[4], starts[4], strides[4];
    uint32_t mode, verts, prims, instances, mul, swap, index_type, index_first, cap, in_base, in_stride;
    uint32_t invocations, max_vertices, out_base, out_stride, idx_base, counts_base, strip, entry_count;
    struct {
        uint32_t buffer, offset, word, count;
    } entries[32];
} VkmXfbParams;

/* what the generation kernel reads, laid out as meta.c's Params. */
typedef struct VkmDgcParams {
    uint64_t args, count, pushes, ranges, index;
    uint64_t buffers[31];
    uint32_t strides[31];
    uint32_t stride, max_count, token_count, primitive, index_wide, local[3];
    VkmDgcToken tokens[VKM_DGC_TOKENS];
    uint32_t push[VKM_PUSH_SIZE / 4];
} VkmDgcParams;

typedef struct VkmOpQuery {
    VkmOp head;
    VkQueryPool pool;
    uint32_t first, count;
    VkBuffer buffer;
    uint64_t offset, stride;
    VkQueryResultFlags flags;
} VkmOpQuery;

/* ------------------------------------------------------------------ recording state. */

#define VKM_RT_STAGES                                                                                     \
    (VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | \
     VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR | VK_SHADER_STAGE_CALLABLE_BIT_KHR)

/* bind points: ray tracing binds what its kernel, a compute pipeline, reads at each trace. */
enum { VKM_GFX, VKM_COMPUTE, VKM_RT };

/* what the next draw or dispatch is made from, and what the command list already set. */
struct VkmGfxState {
    VkmDynamic dyn;
    const VkmCode *vs, *fs, *cs, *gs;
    id<MTLComputePipelineState> compute; /* the bound one. */
    VkPipeline rays;                     /* the bound ray tracing pipeline. */
    bool pipeline_dirty, depth_dirty, raster_dirty, viewport_dirty, scissor_dirty, ref_dirty, constants_dirty;

    /* the render pass being recorded. */
    bool rendering, drawn;
    size_t begin; /* offset of its VKM_OP_BEGIN_RENDER. */
    uint32_t color_count, colors[VKM_COLORS], samples;
    uint32_t width, height, layers;
    bool has_depth, has_stencil;

    uint64_t sets[3][VKM_SETS];
    uint64_t descriptor_buffers[VKM_SETS]; /* the addresses vkCmdBindDescriptorBuffersEXT gave. */
    uint8_t push[VKM_PUSH_SIZE];
    bool push_dirty[3];
    struct {
        uint64_t address, size;
    } vertex[VKM_VERTEX_BINDINGS];
    uint64_t index_address, index_size;
    uint32_t index_type; /* MTLIndexType. */
    const uint8_t *index_host; /* the indices as the host sees them, or NULL when only the gpu does. */
    bool icb; /* the next pipeline is resolved for indirect command buffers. */

    /* the state the list has set since the encoder began. */
    id<MTLRenderPipelineState> set_pipeline;
    id<MTLDepthStencilState> set_depth;
    id<MTLComputePipelineState> set_compute;
    uint64_t set_buffers[2][31];
    uint32_t set_strides[31];

    void *legacy; /* the render pass and subpass of vkCmdBeginRenderPass. */

    /* transform feedback: the bound buffers, and the gpu block of the running offsets once it begins. */
    struct {
        uint64_t address, size;
    } xfb[4];
    uint64_t xfb_counters[4], xfb_state;
    VkQueryPool xfb_query;
    uint32_t xfb_query_index;

    /* a pipeline statistics query: the host counts what it knows, fragment shaders count themselves. */
    VkQueryPool stats;
    uint32_t stats_query;

    /* an occlusion query that spans render passes. */
    VkQueryPool occlusion;
    uint32_t occlusion_query;
    bool occlusion_precise;
};

/* appends a command; zeroed, with its header filled. */
void *vkm_emit(VkCommandBuffer cmd, uint32_t op, size_t size);
/* forgets what the list set, after commands that change encoder state behind the state tracking. */
void vkm_forget(VkCommandBuffer cmd);
/* ends the render pass and begins it again where it left off, without a visibility buffer, or nullptr. */
VkmBeginRender *vkm_render_split(VkCommandBuffer cmd);
uint64_t vkm_stages(VkPipelineStageFlags2 stages);
/* a clear of rectangles of the render pass's attachments, folded into its load actions when it can be. */
void vkm_clear_rects(VkCommandBuffer cmd, uint32_t count, const VkClearAttachment *clears, uint32_t rect_count,
                     const VkClearRect *rects);
MTLClearColor vkm_clear_color(VkFormat format, const VkClearColorValue *v);

/* the encoder side, encode.c. */
id<MTLSamplerState> vkm_sampler_linear(VkDevice device);
id<MTLSamplerState> vkm_sampler_nearest(VkDevice device);
uint32_t vkm_primitive(VkPrimitiveTopology topology);
uint32_t vkm_topology_class(VkPrimitiveTopology topology);
uint32_t vkm_compare(VkCompareOp op);
uint32_t vkm_stencil_op(VkStencilOp op);
uint32_t vkm_blend_factor(VkBlendFactor f);
uint32_t vkm_blend_op(VkBlendOp op);
void vkm_query_written(VkQueryPool pool, uint32_t first, uint32_t count, VkQueue queue, uint64_t value);

#endif
