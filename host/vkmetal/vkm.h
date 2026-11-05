/*
 * mime's Vulkan driver on Metal 4, for the guest's Vulkan wire on macOS. it implements what the guest's
 * direct3d, dxgi and vulkan-1 use, and the host's presenter, in the host process: every VkDevice is a Metal 4
 * queue on the one MTLDevice, so memory and semaphores shared across processes are the same Metal objects.
 *
 * memory is a placement heap with one buffer over it, so images are placed textures and buffers are a range
 * of the heap's buffer. mapped guest memory is a buffer over guest RAM without a copy. descriptor sets are
 * Metal 3 argument buffers: each descriptor element is one 8 byte resource id or gpu address. command
 * buffers record a command list; draws resolve their Metal pipelines while recording, and a submit encodes
 * the list into Metal 4 command buffers.
 */
#ifndef MIME_VKMETAL_VKM_H
#define MIME_VKMETAL_VKM_H

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../mime_host.h"
#include "mtl.h"
#include "vkm_msl.h"
#include "vkmetal.h"


/* ------------------------------------------------------------------ formats. */

enum {
    VKM_CAP_SAMPLED = 1 << 0,
    VKM_CAP_FILTER = 1 << 1,
    VKM_CAP_STORAGE = 1 << 2,
    VKM_CAP_ATOMIC = 1 << 3,
    VKM_CAP_COLOR = 1 << 4,
    VKM_CAP_BLEND = 1 << 5,
    VKM_CAP_MSAA = 1 << 6,
    VKM_CAP_RESOLVE = 1 << 7,
    VKM_CAP_DEPTH = 1 << 8,
    VKM_CAP_TEXEL = 1 << 9,
    VKM_CAP_STORAGE_TEXEL = 1 << 10,
    VKM_CAP_VERTEX = 1 << 11,
};

typedef struct VkmFormat {
    uint16_t pixel;  /* MTLPixelFormat, 0 when images cannot have the format. */
    uint16_t vertex; /* MTLVertexFormat, 0 when vertex attributes cannot. */
    uint8_t bytes;   /* of a texel, or of a block for compressed formats. */
    uint8_t block_w, block_h;
    uint8_t aspects; /* VkImageAspectFlags. */
    uint32_t caps;
    bool srgb, compressed;
} VkmFormat;

/* the format's description; a zeroed one when the driver has no use for it. */
const VkmFormat *vkm_format(VkFormat format);
VkFormatFeatureFlags2 vkm_format_features(const VkmFormat *f, bool linear, bool buffer);

/* ------------------------------------------------------------------ instance and devices. */

struct VkPhysicalDevice_T {
    id mtl;
    char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint64_t memory_size;
    uint32_t texel_alignment; /* minimum texture buffer offset alignment over the formats. */
    bool float_filter;        /* 32 bit float textures filter. */
    bool bc;
    uint8_t uuid[VK_UUID_SIZE];
};

struct VkInstance_T {
    uint32_t api;
    VkPhysicalDevice physical;
};

/* a submission's Metal objects, recycled once the queue's event passes its value. */
typedef struct VkmFlight {
    struct VkmFlight *next;
    uint64_t value;
    id allocator;
    id buffers[16];
    uint32_t buffer_count;
} VkmFlight;

struct VkQueue_T {
    VkDevice device;
    id queue;  /* MTL4CommandQueue. */
    id event;  /* MTLSharedEvent the queue signals after each submission. */
    uint64_t submitted;
    pthread_mutex_t lock;
    VkmFlight *flights, *spare;
    VkmFlight *open; /* the submission being encoded. */
    id tables[2];    /* argument tables for render and compute encoders. */
};

/* under the queue's lock: a Metal command buffer of the next submission, its commit, and the submission's end. */
id vkm_queue_begin(VkQueue q);
void vkm_queue_commit(VkQueue q, id cmd);
uint64_t vkm_queue_signal(VkQueue q);

typedef struct VkmCache VkmCache;

struct VkDevice_T {
    VkPhysicalDevice physical;
    id mtl;
    id compiler;  /* MTL4Compiler. */
    id residency; /* every allocation of the device. */
    pthread_mutex_t residency_lock;
    bool residency_dirty;
    struct VkQueue_T queue;
    /* upload chunks the command buffers reuse. */
    struct VkmChunk *chunks;
    pthread_mutex_t chunk_lock;
    id zero; /* zeros, for vertex attributes no binding gives. */
    uint64_t zero_gpu;
    /* textures placed since the last submission, which it clears first. */
    struct VkmInit *inits;
    uint32_t init_count, init_capacity;
    pthread_mutex_t init_lock;
    /* destroyed objects the queue's submitted work may still use. */
    struct VkmGrave *graves;
    uint32_t grave_count, grave_capacity;
    pthread_mutex_t grave_lock;
    /* the allocations by gpu address, for the descriptors of descriptor buffers that only give one. */
    struct VkDeviceMemory_T **memories;
    uint32_t memory_count, memory_capacity;
    pthread_mutex_t memory_lock;
};

/* instance.c: whether the device advertises the extension. */
bool vkm_device_extension(const char *name);

void vkm_resident(VkDevice device, id allocation);
/*
 * releases a destroyed object, and takes it out of the residency set when resident, once the queue finishes what
 * was submitted before: a freed texture's resource id goes to the next texture, which draws still running would read.
 */
void vkm_release_later(VkDevice device, id object, bool resident);
/*
 * a texture placed in a heap starts with what the memory held, compression state included, which decompresses
 * to garbage; windows zeroes new allocations and direct3d programs count on it. the next submission clears it.
 */
void vkm_image_init(VkDevice device, VkImage image);
void vkm_unresident(VkDevice device, id allocation);
void vkm_residency_commit(VkDevice device);

/* ------------------------------------------------------------------ memory and resources. */

enum {
    VKM_MEMORY_DEVICE = 0, /* a private heap. */
    VKM_MEMORY_HOST = 1,   /* shared, mapped, or guest memory imported without a copy. */
    VKM_MEMORY_TYPES = 2,
};

/* a texture over part of a buffer, which a texel buffer descriptor made from an address needs. */
typedef struct VkmTexel {
    struct VkmTexel *next;
    uint64_t address, range;
    VkFormat format;
    id texture;
    MTLResourceID rid;
} VkmTexel;

struct VkDeviceMemory_T {
    id buffer;  /* over the whole allocation. */
    id heap;    /* to place images in; none for host pointer imports. */
    id texture; /* of the image at its start, when other processes share the memory through it. */
    bool exported; /* the memory's first image is its texture for other processes. */
    uint8_t *map;
    uint64_t size, gpu;
    uint32_t type;
    VkmTexel *texels; /* under the device's memory lock. */
};

/* the allocation a gpu address lies in, or NULL. */
VkDeviceMemory vkm_memory_at(VkDevice device, uint64_t address);
MTLResourceID vkm_texel_rid(VkDevice device, uint64_t address, uint64_t range, VkFormat format);

struct VkBuffer_T {
    uint64_t size;
    VkBufferUsageFlags2 usage;
    VkDeviceMemory memory;
    uint64_t offset, gpu;
};

struct VkBufferView_T {
    id texture;
    MTLResourceID rid;
};

struct VkImage_T {
    VkImageType type;
    VkFormat format;
    const VkmFormat *fi;
    VkExtent3D extent;
    uint32_t levels, layers, samples;
    VkImageUsageFlags usage;
    VkImageCreateFlags flags;
    VkImageTiling tiling;
    id desc;    /* MTLTextureDescriptor the texture is made from at binding. */
    id texture;
    uint64_t size, align;
    uint64_t row_pitch; /* for linear images, which are textures over a buffer. */
    VkDeviceMemory memory;
    uint64_t offset;
    bool swapchain;
};

struct VkImageView_T {
    VkImage image;
    VkImageViewType type;
    VkFormat format;
    const VkmFormat *fi;
    VkImageAspectFlags aspects;
    uint32_t base_level, levels, base_layer, layers;
    VkImageUsageFlags usage;
    VkExtent3D extent; /* of the base level. */
    id texture;        /* for shaders. */
    MTLResourceID rid;
    id render;         /* for attachments: the image's texture or a view in another format. */
    uint32_t render_level, render_slice;
};

struct VkSampler_T {
    id state;
    MTLResourceID rid;
};

/* the texture of a view for an aspect of a depth stencil image, like the stencil plane. */
uint32_t vkm_aspect_format(const VkmFormat *f, VkImageAspectFlags aspect);
uint32_t vkm_texture_type(VkImageViewType type, uint32_t samples);
VkExtent3D vkm_level_extent(VkImage image, uint32_t level);

/* ------------------------------------------------------------------ descriptors. */

typedef struct VkmBinding {
    uint32_t binding;
    VkDescriptorType type;
    uint32_t count;
    uint32_t slot; /* the first argument buffer element, each 8 bytes. */
    VkShaderStageFlags stages;
    VkSampler *immutable;
    VkDescriptorBindingFlags flags;
} VkmBinding;

struct VkDescriptorSetLayout_T {
    uint32_t refs;
    uint32_t count;
    VkmBinding *bindings;
    uint32_t slots;
    VkDescriptorSetLayoutCreateFlags flags;
    /* the immutable samplers of a layout that embeds them, for a descriptor buffer to bind. */
    VkDevice device;
    id embedded;
    uint64_t embedded_gpu;
};

struct VkDescriptorPool_T {
    id buffer;
    uint8_t *map;
    uint64_t gpu, size, used;
    VkDescriptorPoolCreateFlags flags;
    struct VkDescriptorSet_T *sets;
    /* freed ranges, for pools that free sets. */
    struct VkmRange {
        uint64_t offset, size;
    } *holes;
    uint32_t hole_count, hole_capacity;
};

struct VkDescriptorSet_T {
    struct VkDescriptorSet_T *next;
    VkDescriptorPool pool;
    VkDescriptorSetLayout layout;
    uint64_t offset, size;
    uint8_t *map;
    uint64_t gpu;
    uint32_t variable;
};

struct VkPipelineLayout_T {
    uint32_t refs;
    uint32_t set_count;
    VkDescriptorSetLayout sets[VKM_SETS];
    uint32_t push_size;
};

void vkm_set_layout_unref(VkDescriptorSetLayout layout);
const VkmBinding *vkm_set_binding(VkDescriptorSetLayout layout, uint32_t binding);

/* ------------------------------------------------------------------ shaders. */

/* spirv.c: the geometry stage as vertex functions, and the words of the buffer they share. */
#define VKM_GS_BUILTINS 24
enum {
    VKM_GS_VBASE,         /* the vertex index of a vertex shader's first record. */
    VKM_GS_CAP,           /* records an instance. */
    VKM_GS_IN_BASE,       /* the word of the first. */
    VKM_GS_IN_STRIDE,     /* words a record. */
    VKM_GS_BASE_INSTANCE,
    VKM_GS_IN_BUILTINS,   /* the word of a record's built-ins. */
    VKM_GS_PRIMS,         /* primitives an instance. */
    VKM_GS_MUL,           /* vertices from one primitive to the next. */
    VKM_GS_SWAP,          /* odd triangles of strips swap their first two vertices. */
    VKM_GS_INDEX_TYPE,    /* 0 for none, 1 for 16 bits, 2 for 32. */
    VKM_GS_INDEX_FIRST,
    VKM_GS_OUT_BASE,      /* the word of the geometry shader's first record. */
    VKM_GS_IDX_BASE,      /* the word of its first index. */
    VKM_GS_COUNT_BASE,    /* the word of the vertices each invocation emitted, for points. */
    VKM_GS_HEADER = 16,
};

typedef struct VkmGsInfo {
    uint32_t in_vertices, out_kind; /* points, line or triangle strips. */
    uint32_t max_vertices, invocations;
    uint32_t out_stride, out_builtins;
    uint32_t *code;
    size_t words;
} VkmGsInfo;

bool vkm_spirv_capture(const uint32_t *spirv, size_t words, uint32_t **out, size_t *out_words, uint32_t *record_words,
                       uint32_t *builtins);
bool vkm_spirv_geometry(const uint32_t *spirv, size_t words, bool replay, VkmGsInfo *info);
bool vkm_spirv_count(const uint32_t *spirv, size_t words, uint32_t **out, size_t *out_words);

/* what stream output takes of a stage's records: each output's buffer, byte offset, first word and words. */
typedef struct VkmXfb {
    uint32_t count;
    uint32_t strides[4];
    struct {
        uint32_t buffer, offset, word, count;
    } entries[32];
} VkmXfb;

bool vkm_spirv_xfb(const uint32_t *spirv, size_t words, VkmXfb *out);

typedef struct VkmSource VkmSource;

/* a stage compiled to Metal, shared by every shader made from the same MSL. */
typedef struct VkmWidened VkmWidened;

typedef struct VkmCode {
    VkShaderStageFlagBits stage;
    id library;
    id function;     /* MTL4LibraryFunctionDescriptor. */
    MTLSize local;   /* compute threadgroup size. */
    uint32_t inputs; /* vertex input locations. */
    VkmInput input[VKM_VERTEX_ATTRIBUTES];
    uint32_t outputs; /* fragment outputs written. */
    bool writes_depth, writes_stencil;
    uint64_t hash; /* of the MSL and its entry point. */
    /* a vertex shader keeps its SPIR-V for the variant writing records, made when a geometry shader needs it. */
    VkmSource *source;
    struct VkmCode *capture;
    uint32_t record_words, record_builtins;
    /* a vertex shader's variants reading narrow unsigned attributes into its signed inputs. */
    VkmWidened *widened;
    /* a fragment shader's variant giving its output 1 as the second source of dual source blending. */
    struct VkmCode *dual;
    bool dual_failed;
    /* a fragment shader's variant counting its invocations for pipeline statistics. */
    struct VkmCode *counted;
    bool count_failed;
    /* a geometry shader is the two vertex functions of its emulation. */
    struct VkmCode *expand, *replay;
    VkmGsInfo gs;
    VkmXfb *xfb; /* the stream output of a vertex or geometry shader, or NULL. */
} VkmCode;

struct VkmWidened {
    uint8_t widen[VKM_VERTEX_ATTRIBUTES]; /* VKM_WIDEN_*. */
    VkmCode *code;                        /* null when it did not compile. */
    VkmWidened *next;
};

typedef struct VkmLayoutView {
    uint32_t set_count;
    VkDescriptorSetLayout sets[VKM_SETS];
    uint32_t push_size;
} VkmLayoutView;

VkResult vkm_compile(VkDevice device, const uint32_t *spirv, size_t size, const char *entry,
                     VkShaderStageFlagBits stage, const VkSpecializationInfo *spec, const VkmLayoutView *layout,
                     VkmCode *out);
void vkm_code_free(VkmCode *code);
/* the vertex shader writing its outputs to records, or NULL. */
const VkmCode *vkm_code_capture(VkDevice device, const VkmCode *vs);
const VkmCode *vkm_code_counted(VkDevice device, const VkmCode *fs);
const VkmCode *vkm_code_widened(VkDevice device, const VkmCode *vs, const uint8_t *widen);
const VkmCode *vkm_code_dual(VkDevice device, const VkmCode *fs);
/* a library of MSL written for the driver itself. */
id vkm_library(VkDevice device, const char *source);
id vkm_function(id library, const char *name);

struct VkShaderModule_T {
    uint32_t *code;
    size_t size;
};

struct VkShaderEXT_T {
    VkmCode code;
};

/* ------------------------------------------------------------------ pipelines. */

typedef struct VkmVertexInput {
    uint32_t binding_count, attribute_count;
    VkVertexInputBindingDescription2EXT bindings[VKM_VERTEX_BINDINGS];
    VkVertexInputAttributeDescription2EXT attributes[VKM_VERTEX_ATTRIBUTES];
} VkmVertexInput;

/* a color attachment's blending, in Vulkan's enumerations. */
typedef struct VkmBlend {
    uint8_t enable, write;
    uint8_t src_color, dst_color, color_op, src_alpha, dst_alpha, alpha_op;
} VkmBlend;

/* what a render pipeline state is made from; zeroed before it is filled so it compares as bytes. */
typedef struct VkmPipelineKey {
    uint64_t vs, fs;
    uint32_t colors[VKM_COLORS]; /* pixel formats. */
    VkmBlend blend[VKM_COLORS];
    uint8_t samples, topology_class, discard, alpha_to_coverage, alpha_to_one;
    uint8_t attribute_count;
    uint8_t icb; /* the pipeline runs from indirect command buffers too. */
    uint8_t pad;
    struct {
        uint8_t location, binding;
        uint16_t format;
        uint32_t offset;
    } attributes[VKM_VERTEX_ATTRIBUTES];
    struct {
        uint32_t rate; /* MTLVertexStepFunction. */
        uint32_t divisor;
    } bindings[VKM_VERTEX_BINDINGS];
} VkmPipelineKey;

id vkm_render_pipeline(VkDevice device, const VkmPipelineKey *key, const VkmCode *vs, const VkmCode *fs);

typedef struct VkmStencilFace {
    uint8_t fail, pass, depth_fail, compare;
    uint32_t read, write;
} VkmStencilFace;

typedef struct VkmDepthKey {
    uint8_t test, write, compare, stencil;
    VkmStencilFace front, back;
} VkmDepthKey;

id vkm_depth_state(VkDevice device, const VkmDepthKey *key);

/* state a graphics pipeline sets, or leaves to the command buffer for its dynamic states. */
typedef struct VkmGfxStatic VkmGfxStatic;

struct VkPipeline_T {
    VkPipelineBindPoint bind;
    VkmCode stages[3]; /* vertex, fragment and geometry, or compute in the first. */
    bool has[3];
    id compute; /* MTLComputePipelineState. */
    VkmGfxStatic *gfx;
};

id vkm_compute_pipeline(VkDevice device, const VkmCode *code, bool icb);

/* a step of each sequence of device generated commands, what the generation kernel reads. */
enum {
    VKM_DGC_PUSH = 1,
    VKM_DGC_VERTEX,
    VKM_DGC_INDEX,
    VKM_DGC_SEQUENCE,
    VKM_DGC_DRAW,
    VKM_DGC_DRAW_INDEXED,
    VKM_DGC_DISPATCH,
};

#define VKM_DGC_TOKENS 16

typedef struct VkmDgcToken {
    uint32_t type, offset;
    uint32_t dst, size; /* the push constant bytes, the vertex binding, or the index buffer's input mode. */
} VkmDgcToken;

struct VkIndirectCommandsLayoutEXT_T {
    uint32_t stride, token_count;
    bool pushes, dispatch;
    VkmDgcToken tokens[VKM_DGC_TOKENS];
};

/* indirect command buffers a command buffer's generated commands are encoded into, kept until it is reset. */
typedef struct VkmIcb {
    struct VkmIcb *next;
    id icb;
    MTLResourceID rid;
    uint32_t capacity;
    bool dispatch, used;
} VkmIcb;

/* render passes, as the dynamic rendering their subpasses begin. */
typedef struct VkmSubpass {
    uint32_t color_count;
    uint32_t colors[VKM_COLORS];  /* attachment indices, VK_ATTACHMENT_UNUSED for none. */
    uint32_t resolves[VKM_COLORS];
    uint32_t depth, depth_resolve;
    uint32_t view_mask;
} VkmSubpass;

struct VkRenderPass_T {
    uint32_t attachment_count, subpass_count;
    VkAttachmentDescription2 *attachments;
    VkmSubpass *subpasses;
};

struct VkFramebuffer_T {
    uint32_t width, height, layers;
    uint32_t count;
    VkImageView views[];
};

struct VkPipelineCache_T {
    int unused;
};

/* hash tables of Metal objects keyed by plain structs. */
id vkm_cache_find(VkmCache *cache, const void *key, size_t size, uint64_t hash);
void vkm_cache_add(VkmCache **cache, const void *key, size_t size, uint64_t hash, id object);
void vkm_cache_free(VkmCache *cache);
uint64_t vkm_hash(const void *data, size_t size, uint64_t seed);

/* ------------------------------------------------------------------ synchronization. */

struct VkSemaphore_T {
    id event; /* MTLSharedEvent. binary semaphores count signals in it too. */
    VkSemaphoreType type;
    uint64_t binary; /* the last value a binary signal set. */
};

struct VkFence_T {
    VkQueue queue;
    uint64_t value; /* signaled once the queue's event reaches it, 0 when signaled by the host. */
    bool signaled;
};

struct VkEvent_T {
    volatile uint32_t set;
};

/* a queue's wait for a semaphore or its signal, under the queue's lock. */
void vkm_semaphore_wait(VkQueue q, VkSemaphore s, uint64_t value);
void vkm_semaphore_signal(VkQueue q, VkSemaphore s, uint64_t value);

/* waits for an event to reach value, false on timeout. */
bool vkm_event_wait(id event, uint64_t value, uint64_t timeout_ns);

/* ------------------------------------------------------------------ queries. */

struct VkQueryPool_T {
    VkQueryType type;
    uint32_t count;
    id buffer; /* 8 bytes for each value of a query, shared. */
    uint64_t *values;
    /* each query's availability: the value of the queue's event once it is written, 0 when reset. */
    uint64_t *written;
    VkQueue queue;
    VkQueryPipelineStatisticFlags statistics;
};

/* ------------------------------------------------------------------ command buffers. */

typedef struct VkmChunk {
    struct VkmChunk *next;
    id buffer;
    uint8_t *map;
    uint64_t gpu, size, used;
} VkmChunk;

struct VkCommandPool_T {
    VkDevice device;
    struct VkCommandBuffer_T *buffers;
};

typedef struct VkmGfxState VkmGfxState;

struct VkCommandBuffer_T {
    struct VkCommandBuffer_T *next;
    VkDevice device;
    VkCommandPool pool;
    VkCommandBufferLevel level;
    uint8_t *cmds;
    size_t cmd_size, cmd_capacity;
    VkmChunk *chunks; /* upload memory the commands use. */
    VkmIcb *icbs;
    VkmGfxState *state;
    VkResult status;
};

/* upload memory valid as long as the command buffer's recording. */
void *vkm_upload(VkCommandBuffer cmd, uint64_t size, uint64_t align, uint64_t *gpu, id *buffer, uint64_t *offset);
void vkm_chunks_release(VkDevice device, VkmChunk *chunks);

/* encode.c: a command buffer into a Metal 4 command buffer. */
void vkm_encode(VkQueue queue, VkCommandBuffer cmd, id mtl_cmd);

/* ------------------------------------------------------------------ swapchains. */

struct VkSurfaceKHR_T {
    id layer; /* CAMetalLayer. */
};

struct VkSwapchainKHR_T {
    VkSurfaceKHR surface;
    VkDevice device;
    VkFormat format;
    VkExtent2D extent;
    uint32_t count, next;
    VkImage images[8];
    VkDeviceMemory memory[8];
    VkPresentModeKHR mode;
    bool resident; /* the queue has the layer's residency set. */
};

/* ------------------------------------------------------------------ internal shaders, shared by every device. */

/* a draw of colors or depth over rectangles of the current render pass. */
id vkm_meta_clear(VkDevice device, const uint32_t colors[VKM_COLORS], uint32_t mask, bool depth, bool stencil,
                  uint32_t samples, bool layered);
id vkm_meta_blit(VkDevice device, uint32_t format, uint32_t source_type, bool depth, uint32_t samples);
id vkm_meta_depth_state(VkDevice device, bool depth, bool stencil);
/* the kernel encoding generated draws or dispatches into an indirect command buffer, one thread a sequence. */
id vkm_meta_generate(VkDevice device, bool dispatch);
/* the vertex function writing a draw's stream output, and the kernel making a draw of what it wrote. */
id vkm_meta_xfb(VkDevice device, const uint32_t colors[VKM_COLORS], uint32_t samples);
id vkm_meta_byte_count(VkDevice device);

/* ------------------------------------------------------------------ entry points. */

/* each file's commands, ending with a null name. */
typedef struct VkmEntry {
    const char *name;
    PFN_vkVoidFunction fn;
} VkmEntry;

#define VKM_ENTRY(name) {"vk" #name, (PFN_vkVoidFunction)name}
#define VKM_ALIAS(alias, name) {alias, (PFN_vkVoidFunction)name}

extern const VkmEntry vkm_instance_entries[], vkm_device_entries[], vkm_memory_entries[], vkm_descriptor_entries[],
    vkm_shader_entries[], vkm_cmd_entries[], vkm_xfer_entries[], vkm_query_entries[], vkm_wsi_entries[];

#define VKM_ERROR(...) mime_warn("vkmetal: " __VA_ARGS__)

static inline void *vkm_zalloc(size_t size)
{
    return calloc(1, size);
}

static inline const void *vkm_find(const void *chain, VkStructureType type)
{
    for (const VkBaseInStructure *s = chain; s; s = s->pNext)
        if (s->sType == type)
            return s;
    return nullptr;
}

#endif
