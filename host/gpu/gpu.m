/*
 * Metal 4 command replay over mime/gpu.h channels. each guest process has a worker reading its Metal-level command
 * ring; the guest owns API state and render-pass decisions. Metal buffers map guest RAM directly. shader.c compiles
 * programs concurrently, pipelines wait for them, and each present copies a surface's back buffer to its window's
 * CAMetalLayer, or upscales it with MetalFX when the window is stretched.
 */
#include "gpu.h"

#include "../ui/ui.h"
#include "archive.h"
#include "shader.h"

#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <QuartzCore/CAMetalLayer.h>
#include <dispatch/dispatch.h>
#include <ctype.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------------- objects */

/*
 * tracks attachment contents in encoding order: undefined data is never loaded, pending clears become load actions,
 * and untouched attachments avoid stores.
 */
enum { TRACK_DATA, TRACK_UNDEFINED, TRACK_CLEARED };

typedef struct Aspect {
    uint8_t kind;
    bool materialized; /* a cleared aspect whose memory holds the clear value. */
    float value[4];    /* the clear color, or the depth in value[0]. */
    uint32_t stencil;
} Aspect;

/* the color or depth aspect, then the stencil aspect. */
typedef struct Track {
    bool tracked;
    Aspect aspect[2];
} Track;

typedef struct Texture {
    id<MTLTexture> tex;
    id<MTLTexture> sampled;
    uint32_t format, width, height, samples;
    Track track;
} Texture;

typedef struct Program {
    dispatch_semaphore_t ready;
    MTL4LibraryFunctionDescriptor *fn[MIME_GPU_STAGES];
    bool compiled;
    char *fs_msl;    /* the fragment source, for the logic operation variants. */
    char fs_entry[64];
    MTL4LibraryFunctionDescriptor *logic_fn[16]; /* by operation counted from GL_CLEAR, made on first use. */
    bool logic_failed[16];
    os_unfair_lock logic_lock;
    dispatch_group_t builds; /* the pipelines being built from it. */
} Program;

typedef struct Pipeline {
    dispatch_semaphore_t ready; /* signaled once its build is over, and kept so. */
    id<MTLRenderPipelineState> state;
} Pipeline;

typedef struct Surface {
    id<MTLTexture> color, depth; /* depth holds the stencil too when both exist. */
    Track color_track, depth_track;
    void *layer;
    uint32_t width, height, samples;
    uint32_t window_id;
    uint16_t depth_format, stencil_format;
    bool sync;
    id<MTLResidencySet> layer_set; /* the drawables of the layer, resident on the queue. */
    id<MTL4FXSpatialScaler> scaler;
    id<MTLTexture> scaled; /* the scaler's output when a drawable cannot be it. */
    uint32_t unscalable_width, unscalable_height; /* the output size metalfx refused. */
} Surface;

typedef struct Binding {
    uint32_t id;
    uint32_t sampler;
    bool surface;
} Binding;

/*
 * metal 4 keeps nothing alive: an object the guest lets go of waits here for the submissions that may still use it.
 * resident ones leave the residency set at the same time.
 */
enum { RETIRE_RELEASE, RETIRE_RESIDENT, RETIRE_QUEUE_SET };

typedef struct Retired {
    id object;
    uint64_t serial;
    uint8_t kind;
} Retired;

typedef struct Allocator {
    id<MTL4CommandAllocator> allocator;
    uint64_t serial; /* the submission whose commands it holds. */
} Allocator;

typedef struct Upload {
    id<MTLBuffer> buffer;
    uint64_t serial;
} Upload;

static constexpr int MAX_ALLOCATORS = 8;
static constexpr NSUInteger UPLOAD_SIZE = 1 << 20;
static constexpr NSUInteger UPLOAD_ALIGN = 256;
static constexpr NSUInteger MAX_SAMPLERS = 16;

typedef struct Channel {
    uint32_t id;
    MimeVm *vm;
    MimeGpuHeader *hdr;
    uint8_t *ring;
    id<MTLBuffer> blocks[MIME_GPU_MAX_BLOCKS];
    uint8_t *block_base[MIME_GPU_MAX_BLOCKS];
    uint64_t block_size[MIME_GPU_MAX_BLOCKS];
    bool block_resident[MIME_GPU_MAX_BLOCKS];

    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool kicked, stop;

    id<MTL4CommandQueue> queue;
    id<MTL4CommandBuffer> cb;
    bool cb_open;
    id<MTL4RenderCommandEncoder> render;
    id<MTL4ComputeCommandEncoder> blit;
    bool blit_used;
    id<MTL4ArgumentTable> tables[MIME_GPU_STAGES];
    id<MTLSamplerState> blit_samplers[2]; /* nearest and linear. */
    id<MTLResidencySet> residency;
    bool residency_dirty;
    /* submissions count from 1, and their feedback moves completed up to them. */
    uint64_t submitted;
    uint64_t completed;
    Allocator allocators[MAX_ALLOCATORS];
    uint32_t allocator_count;
    Upload *uploads;
    uint32_t uploads_cap, upload_count, upload;
    NSUInteger upload_used;
    Retired *retired;
    uint32_t retired_cap, retired_count;

    Texture *textures;
    uint32_t textures_cap;
    Program **programs;
    uint32_t programs_cap;
    Pipeline **pipelines;
    uint32_t pipelines_cap;
    id<MTLDepthStencilState> *depth_states;
    uint32_t depth_states_cap;
    id<MTLSamplerState> *samplers;
    uint32_t samplers_cap;
    Surface *surfaces;
    uint32_t surfaces_cap;

    /* the render pass in progress. */
    uint32_t pass_width, pass_height;
    uint16_t pass_color[MIME_GPU_MAX_COLORS], pass_depth, pass_stencil;
    uint32_t pass_samples;
    /*
     * defers the guest pass's encoder until the first draw so clears become load actions. copies or commits may
     * split it into encoders that preserve contents. enc_load records each attachment's load action; written tracks
     * draws and clears.
     */
    MimeGpuPass pass;
    bool pass_live, pass_started;
    uint8_t enc_load[MIME_GPU_MAX_COLORS + 2];
    bool enc_unknown[MIME_GPU_MAX_COLORS + 2];
    uint32_t written;
    uint8_t depth_flags; /* of the bound depth stencil state: 1 writes depth, 2 writes stencil. */
    uint8_t *depth_state_flags;
    uint32_t depth_state_flags_cap;
    /* a fence or flush met while an encoder was open, committed once it ends or the guest waits for it. */
    uint64_t deferred_fence, committed_fence;
    bool deferred;

    /* state the guest set, applied to an encoder before a draw. */
    struct {
        id<MTLRenderPipelineState> pipeline;
        id<MTLDepthStencilState> depth_state;
        uint32_t front_ref, back_ref;
        MimeGpuSetRaster raster;
        MTLViewport viewport;
        MimeGpuRect scissor;
        bool has_scissor;
        float blend[4];
        MimeGpuMem vb[MIME_GPU_MAX_ATTRIBS];
        uint32_t vb_mask;
        uint8_t uniforms[4096];
        uint32_t uniform_bytes;
        MimeGpuMem uniform_buffers[MIME_GPU_STAGES][MIME_GPU_MAX_UNIFORM_BLOCKS];
        uint32_t uniform_buffer_mask[MIME_GPU_STAGES];
        Binding tex[MIME_GPU_STAGES][MIME_GPU_MAX_TEXTURES];
        uint32_t tex_mask[MIME_GPU_STAGES];
        uint32_t dirty;
    } st;

    id<MTLRenderPipelineState> clear_pipelines[16];
    uint64_t clear_keys[16];
    id<MTLRenderPipelineState> blit_pipelines[8];
    uint64_t blit_keys[8];
    id<MTLLibrary> internal_library;
} Channel;

enum {
    DIRTY_PIPELINE = 1u << 0,
    DIRTY_DEPTH = 1u << 1,
    DIRTY_RASTER = 1u << 2,
    DIRTY_VIEWPORT = 1u << 3,
    DIRTY_SCISSOR = 1u << 4,
    DIRTY_BLEND = 1u << 5,
    DIRTY_VB = 1u << 6,
    DIRTY_UNIFORMS = 1u << 7,
    DIRTY_TEXTURES = 1u << 8,
    DIRTY_ALL = 0x1FF,
};

static constexpr int MAX_CHANNELS = 256;

static os_unfair_lock g_lock = OS_UNFAIR_LOCK_INIT;
static Channel *g_channels[MAX_CHANNELS];
static id<MTLDevice> g_device;
static MimePipelineCache *g_cache;
static id<MTL4Compiler> g_metalfx_compiler; /* the scalers' own pipelines stay out of the cache. */
static bool g_metalfx;
static dispatch_queue_t g_compile_queue;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static Channel *find_channel(uint64_t id_)
{
    os_unfair_lock_lock(&g_lock);
    Channel *c = id_ < MAX_CHANNELS ? g_channels[id_] : nullptr;
    os_unfair_lock_unlock(&g_lock);
    return c;
}

static void *slot(void **table, uint32_t *cap, uint32_t id_, size_t size)
{
    if (id_ >= *cap) {
        uint32_t n = *cap ? *cap : 64;
        while (n <= id_)
            n *= 2;
        *table = realloc(*table, (size_t)n * size);
        memset((uint8_t *)*table + (size_t)*cap * size, 0, (size_t)(n - *cap) * size);
        *cap = n;
    }
    return (uint8_t *)*table + (size_t)id_ * size;
}

#define SLOT(c, table, id_) ((typeof((c)->table))slot((void **)&(c)->table, &(c)->table##_cap, id_, sizeof(*(c)->table)))

static const char *error_text(NSError *err)
{
    if (!err)
        return "unknown error";
    return err.localizedDescription.UTF8String;
}

static void lose(Channel *c, const char *why)
{
    mime_warn("gpu: channel %u stops: %s", c->id, why);
    c->hdr->lost = 1;
}

static bool heap_range(Channel *c, const MimeGpuMem *m, uint64_t size)
{
    return m->block < MIME_GPU_MAX_BLOCKS && c->blocks[m->block] && m->offset <= c->block_size[m->block] &&
           size <= c->block_size[m->block] - m->offset;
}

static void resident(Channel *c, id<MTLAllocation> allocation)
{
    [c->residency addAllocation:allocation];
    c->residency_dirty = true;
}

/* the Metal buffer and offset of guest heap memory, nil when out of range. it becomes resident on first use. */
static id<MTLBuffer> heap(Channel *c, const MimeGpuMem *m, uint64_t size, uint64_t *offset)
{
    if (!heap_range(c, m, size))
        return nil;
    if (!c->block_resident[m->block]) {
        resident(c, c->blocks[m->block]);
        c->block_resident[m->block] = true;
    }
    *offset = m->offset;
    return c->blocks[m->block];
}

/* any thread: the compile jobs write their results through it. */
static void *heap_ptr(Channel *c, const MimeGpuMem *m, uint64_t size)
{
    return heap_range(c, m, size) ? c->block_base[m->block] + m->offset : nullptr;
}

/* ---------------------------------------------------------------- encoders */

static uint64_t completed(Channel *c)
{
    return __atomic_load_n(&c->completed, __ATOMIC_ACQUIRE);
}

/* the last submission that can use what is encoded now. */
static uint64_t current_serial(Channel *c)
{
    return c->cb_open ? c->submitted + 1 : c->submitted;
}

static void retire(Channel *c, id object, uint8_t kind)
{
    if (!object)
        return;
    *SLOT(c, retired, c->retired_count) = (Retired){object, current_serial(c), kind};
    c->retired_count++;
}

static void sweep(Channel *c)
{
    uint64_t done = completed(c);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < c->retired_count; i++) {
        Retired r = c->retired[i];
        if (r.serial > done) {
            c->retired[kept++] = r;
            continue;
        }
        if (r.kind == RETIRE_RESIDENT) {
            [c->residency removeAllocation:r.object];
            c->residency_dirty = true;
        } else if (r.kind == RETIRE_QUEUE_SET) {
            [c->queue removeResidencySet:r.object];
        }
        [r.object release];
    }
    c->retired_count = kept;
}

/* an allocator is reset only once the gpu is done with the commands it holds. */
static id<MTL4CommandBuffer> command_buffer(Channel *c)
{
    if (c->cb_open)
        return c->cb;
    Allocator *a = nullptr;
    while (!a) {
        uint64_t done = completed(c);
        for (uint32_t i = 0; i < c->allocator_count && !a; i++)
            if (c->allocators[i].serial <= done)
                a = &c->allocators[i];
        if (a) {
            [a->allocator reset];
        } else if (c->allocator_count < MAX_ALLOCATORS) {
            a = &c->allocators[c->allocator_count++];
            a->allocator = [g_device newCommandAllocator];
        } else {
            usleep(100);
        }
    }
    a->serial = c->submitted + 1;
    [c->cb beginCommandBufferWithAllocator:a->allocator];
    c->cb_open = true;
    return c->cb;
}

/* the bytes a draw reads come from shared buffers, each reused once the gpu is past the last submission using it. */
static MTLGPUAddress upload(Channel *c, const void *data, NSUInteger size)
{
    NSUInteger aligned = (size + UPLOAD_ALIGN - 1) & ~(UPLOAD_ALIGN - 1);
    if (!c->upload_count || c->upload_used + aligned > UPLOAD_SIZE) {
        uint64_t done = completed(c);
        uint32_t next = c->upload_count;
        for (uint32_t i = 0; i < c->upload_count && next == c->upload_count; i++)
            if (i != c->upload && c->uploads[i].serial <= done)
                next = i;
        if (next == c->upload_count) {
            Upload *u = SLOT(c, uploads, next);
            MTLResourceOptions options = MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined;
            u->buffer = [g_device newBufferWithLength:UPLOAD_SIZE options:options];
            resident(c, u->buffer);
            c->upload_count++;
        }
        c->upload = next;
        c->upload_used = 0;
    }
    Upload *u = &c->uploads[c->upload];
    u->serial = c->submitted + 1;
    memcpy((uint8_t *)u->buffer.contents + c->upload_used, data, size);
    MTLGPUAddress address = u->buffer.gpuAddress + c->upload_used;
    c->upload_used += aligned;
    return address;
}

/* metal 4 tracks no hazards: every encoder waits for all the work before it on the queue. */
static void wait_queue(id<MTL4CommandEncoder> e)
{
    [e barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
}

static void end_render(Channel *c);
static void track_reset(Track *t, bool tracked);
static bool in_pass(Channel *c, uint32_t id_, bool surface);
static void materialize(Channel *c, uint32_t id_, bool surface, bool depth);
static void write_texture(Channel *c, uint32_t id_, bool surface, bool depth, bool whole);
static bool covers(Channel *c, uint32_t id_, bool surface, uint32_t level, uint32_t slice, int64_t x, int64_t y,
                   int64_t w, int64_t h);

static void end_blit(Channel *c)
{
    if (c->blit) {
        [c->blit endEncoding];
        [c->blit release];
        c->blit = nil;
    }
}

static id<MTL4ComputeCommandEncoder> blit_encoder(Channel *c)
{
    end_render(c);
    if (!c->blit) {
        c->blit = [[command_buffer(c) computeCommandEncoder] retain];
        wait_queue(c->blit);
    } else {
        /* the copies of one encoder would otherwise run together. */
        [c->blit barrierAfterEncoderStages:MTLStageBlit beforeEncoderStages:MTLStageBlit
                         visibilityOptions:MTL4VisibilityOptionDevice];
    }
    return c->blit;
}

static void signal_fence(Channel *c, uint64_t value)
{
    uint64_t done = __atomic_load_n(&c->hdr->done_fence, __ATOMIC_ACQUIRE);
    while (done < value &&
           !__atomic_compare_exchange_n(&c->hdr->done_fence, &done, value, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        ;
    uint64_t wait = __atomic_load_n(&c->hdr->wait_fence, __ATOMIC_SEQ_CST);
    if (wait && value >= wait)
        mime_vm_raise_irq(c->vm, MIME_IRQ_GPU);
}

/* commits pending work and signals a nonzero fence after GPU completion. */
static void commit(Channel *c, uint64_t fence)
{
    end_render(c);
    end_blit(c);
    if (c->deferred) {
        if (c->deferred_fence > fence)
            fence = c->deferred_fence;
        c->deferred = false;
        c->deferred_fence = 0;
    }
    if (fence > c->committed_fence)
        c->committed_fence = fence;
    if (!c->cb_open && !fence)
        return;
    id<MTL4CommandBuffer> cb = command_buffer(c);
    [cb endCommandBuffer];
    if (c->residency_dirty) {
        [c->residency commit];
        c->residency_dirty = false;
    }
    uint64_t serial = ++c->submitted;
    MTL4CommitOptions *options = [[MTL4CommitOptions alloc] init];
    [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
        if (feedback.error)
            mime_warn("gpu: channel %u: %s", c->id, error_text(feedback.error));
        uint64_t done = __atomic_load_n(&c->completed, __ATOMIC_ACQUIRE);
        while (done < serial &&
               !__atomic_compare_exchange_n(&c->completed, &done, serial, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            ;
        if (fence)
            signal_fence(c, fence);
    }];
    [c->queue commit:&cb count:1 options:options];
    [options release];
    c->cb_open = false;
    sweep(c);
}

/* ---------------------------------------------------------------- textures */

static id<MTLTexture> texture_of(Channel *c, uint32_t id_, bool surface, bool depth)
{
    if (surface) {
        Surface *s = id_ < c->surfaces_cap ? &c->surfaces[id_] : nullptr;
        return s ? (depth ? s->depth : s->color) : nil;
    }
    return id_ < c->textures_cap ? c->textures[id_].tex : nil;
}

static MTLTextureSwizzleChannels swizzle_channels(uint32_t swizzle)
{
    return MTLTextureSwizzleChannelsMake((uint8_t)swizzle, (uint8_t)(swizzle >> 8), (uint8_t)(swizzle >> 16),
                                         (uint8_t)(swizzle >> 24));
}

static id<MTLTexture> new_texture(Channel *c, MTLTextureType type, MTLPixelFormat format, uint32_t w, uint32_t h,
                                  uint32_t depth, uint32_t levels, uint32_t samples, MTLTextureUsage usage,
                                  uint32_t swizzle)
{
    MTLTextureDescriptor *d = [[MTLTextureDescriptor alloc] init];
    d.textureType = type;
    d.pixelFormat = format;
    d.width = w ? w : 1;
    d.height = h ? h : 1;
    if (type == MTLTextureType3D)
        d.depth = depth ? depth : 1;
    else if (type == MTLTextureType2DArray || type == MTLTextureType1DArray)
        d.arrayLength = depth ? depth : 1;
    else if (type == MTLTextureTypeCubeArray)
        d.arrayLength = depth / 6 ? depth / 6 : 1;
    d.mipmapLevelCount = levels ? levels : 1;
    d.sampleCount = samples ? samples : 1;
    d.usage = usage;
    d.storageMode = MTLStorageModePrivate;
    if (swizzle)
        d.swizzle = swizzle_channels(swizzle);
    id<MTLTexture> t = [g_device newTextureWithDescriptor:d];
    [d release];
    if (t)
        resident(c, t);
    return t;
}

static void texture_swizzle(Channel *c, uint32_t id_, uint32_t swizzle)
{
    if (id_ >= c->textures_cap)
        return;
    Texture *t = &c->textures[id_];
    retire(c, t->sampled, RETIRE_RELEASE);
    t->sampled = nil;
    if (t->tex && swizzle != 0x05040302) {
        MTLTextureType type = t->tex.textureType;
        NSRange levels = NSMakeRange(0, t->tex.mipmapLevelCount);
        NSRange slices = NSMakeRange(0, t->tex.arrayLength);
        if (type == MTLTextureTypeCube || type == MTLTextureTypeCubeArray)
            slices.length *= 6;
        t->sampled = [t->tex newTextureViewWithPixelFormat:t->format textureType:type levels:levels slices:slices
                                                   swizzle:swizzle_channels(swizzle)];
        if (!t->sampled)
            mime_warn("gpu: texture %u swizzle view failed", id_);
    }
    c->st.dirty |= DIRTY_TEXTURES;
}

static void texture_create(Channel *c, const MimeGpuTextureCreate *t)
{
    if (in_pass(c, t->id, false))
        end_render(c);
    Texture *x = SLOT(c, textures, t->id);
    retire(c, x->tex, RETIRE_RESIDENT);
    track_reset(&x->track, (t->usage & MIME_GPU_USAGE_TARGET) &&
                               (t->type == MTLTextureType2D || t->type == MTLTextureType2DMultisample) &&
                               t->levels <= 1);
    MTLTextureUsage usage = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
    if (t->usage & MIME_GPU_USAGE_TARGET)
        usage |= MTLTextureUsageRenderTarget;
    x->tex = new_texture(c, t->type, t->format, t->width, t->height, t->depth, t->levels, t->samples, usage, 0);
    x->format = t->format;
    x->width = t->width;
    x->height = t->height;
    x->samples = t->samples;
    texture_swizzle(c, t->id, t->swizzle);
    if (!x->tex)
        mime_warn("gpu: texture %u (%ux%u format %u) failed", t->id, t->width, t->height, t->format);
}

static void texture_copy(Channel *c, const MimeGpuTextureCopy *t, bool upload)
{
    uint32_t kind = t->surface & 0xFF;
    id<MTLTexture> tex = texture_of(c, t->id, kind != MIME_GPU_COPY_TEXTURE, kind == MIME_GPU_COPY_SURFACE_DEPTH);
    uint32_t image = t->image_bytes ? t->image_bytes : t->row_bytes * t->height;
    uint64_t bytes = (uint64_t)image * (t->depth ? t->depth : 1);
    uint64_t off;
    id<MTLBuffer> buf = heap(c, &t->mem, bytes, &off);
    if (!tex || !buf || !t->width || !t->height)
        return;
    MTLSize size = {t->width, t->height, t->depth ? t->depth : 1};
    MTLOrigin origin = {t->x, t->y, t->z};
    end_render(c);
    if (upload)
        write_texture(c, t->id, kind != MIME_GPU_COPY_TEXTURE, kind == MIME_GPU_COPY_SURFACE_DEPTH,
                      t->depth <= 1 && covers(c, t->id, kind != MIME_GPU_COPY_TEXTURE, t->level, t->slice, t->x, t->y,
                                              t->width, t->height));
    else
        materialize(c, t->id, kind != MIME_GPU_COPY_TEXTURE, kind == MIME_GPU_COPY_SURFACE_DEPTH);
    MTLBlitOption options = MTLBlitOptionNone;
    if (tex.pixelFormat == MTLPixelFormatDepth32Float_Stencil8)
        options = (t->surface & MIME_GPU_COPY_STENCIL) ? MTLBlitOptionStencilFromDepthStencil
                                                       : MTLBlitOptionDepthFromDepthStencil;
    id<MTL4ComputeCommandEncoder> b = blit_encoder(c);
    if (upload)
        [b copyFromBuffer:buf sourceOffset:off sourceBytesPerRow:t->row_bytes sourceBytesPerImage:image
                   sourceSize:size toTexture:tex destinationSlice:t->slice destinationLevel:t->level
            destinationOrigin:origin options:options];
    else
        [b copyFromTexture:tex sourceSlice:t->slice sourceLevel:t->level sourceOrigin:origin sourceSize:size
                          toBuffer:buf destinationOffset:off destinationBytesPerRow:t->row_bytes
            destinationBytesPerImage:image options:options];
}

/* ---------------------------------------------------------------- programs */

typedef struct CompileJob {
    Channel *c;
    Program *p;
    char *vs, *fs, *bindings;
    size_t vs_len, fs_len, bindings_len;
    MimeGpuMem result;
    uint32_t capacity;
} CompileJob;

static MTL4LibraryFunctionDescriptor *make_function(const char *source, const char *entry, char **error)
{
    NSError *err = nil;
    id<MTLLibrary> lib = [g_device newLibraryWithSource:@(source) options:nil error:&err];
    if (!lib) {
        *error = strdup(error_text(err));
        return nil;
    }
    NSString *name = @(entry);
    MTL4LibraryFunctionDescriptor *fn = nil;
    if ([lib.functionNames containsObject:name]) {
        fn = [[MTL4LibraryFunctionDescriptor alloc] init];
        fn.library = lib;
        fn.name = name;
    } else {
        *error = strdup("translated entry point was not found");
    }
    [lib release];
    return fn;
}

static void append_program_error(MimeShaderResult *result, const char *message)
{
    MimeGpuProgramInfo *info = (MimeGpuProgramInfo *)result->info;
    size_t length = strlen(message), old_length = info->log_bytes;
    size_t size = (result->info_size + old_length + length + 2 + 7) & ~(size_t)7;
    if (size > UINT32_MAX)
        return;
    uint8_t *data = realloc(result->info, size);
    if (!data)
        return;
    result->info = data;
    info = (MimeGpuProgramInfo *)data;
    uint32_t offset = result->info_size;
    memcpy(data + offset, data + info->log_offset, old_length);
    memcpy(data + offset + old_length, message, length);
    data[offset + old_length + length] = '\n';
    memset(data + offset + old_length + length + 1, 0, size - offset - old_length - length - 1);
    info->log_offset = offset;
    info->log_bytes = (uint32_t)(old_length + length + 1);
    info->size = result->info_size = (uint32_t)size;
}

static void compile_job(void *arg)
{
    CompileJob *j = arg;
    @autoreleasepool {
        MimeShaderResult r;
        mime_shader_compile(j->vs, j->vs_len, j->fs, j->fs_len, j->bindings, j->bindings_len, &r);
        bool ok = r.ok;
        for (int s = 0; ok && s < MIME_GPU_STAGES; s++) {
            char *error = nullptr;
            j->p->fn[s] = make_function(r.msl[s], r.entry[s], &error);
            if (!j->p->fn[s]) {
                mime_warn("gpu: metal rejected a translated program: %s", error ? error : "");
                append_program_error(&r, error ? error : "metal rejected a translated program");
                free(error);
                ok = false;
            }
        }
        if (ok) {
            j->p->fs_msl = strdup(r.msl[1]);
            snprintf(j->p->fs_entry, sizeof(j->p->fs_entry), "%s", r.entry[1]);
        }
        j->p->compiled = ok;
        /* the info goes to the guest last: a nonzero size tells it the program is done. */
        uint8_t *dst = heap_ptr(j->c, &j->result, j->capacity);
        if (dst && j->capacity >= sizeof(MimeGpuProgramInfo)) {
            MimeGpuProgramInfo *info = (MimeGpuProgramInfo *)r.info;
            if (!ok)
                info->status = 1;
            uint32_t size = info->size;
            if (size > j->capacity) {
                /* the guest asks again with room for it. */
                MimeGpuProgramInfo small = *info;
                small.attrib_count = small.uniform_count = small.block_count = small.log_bytes = 0;
                memcpy(dst + sizeof(uint32_t) * 2, (uint8_t *)&small + sizeof(uint32_t) * 2,
                       sizeof(small) - sizeof(uint32_t) * 2);
                ((MimeGpuProgramInfo *)dst)->status = small.status;
            } else {
                memcpy(dst + sizeof(uint32_t) * 2, r.info + sizeof(uint32_t) * 2, size - sizeof(uint32_t) * 2);
                ((MimeGpuProgramInfo *)dst)->status = info->status;
            }
            __atomic_store_n(&((MimeGpuProgramInfo *)dst)->size, size, __ATOMIC_RELEASE);
        }
        mime_shader_free(&r);
        dispatch_semaphore_signal(j->p->ready);
    }
    free(j->vs);
    free(j->fs);
    free(j->bindings);
    free(j);
}

static void program_free(Program *p)
{
    dispatch_group_wait(p->builds, DISPATCH_TIME_FOREVER);
    dispatch_release(p->builds);
    dispatch_release(p->ready);
    [p->fn[0] release];
    [p->fn[1] release];
    for (int i = 0; i < 16; i++)
        [p->logic_fn[i] release];
    free(p->fs_msl);
    free(p);
}

static void program_create(Channel *c, const MimeGpuProgramCreate *pc)
{
    uint64_t body = pc->cmd.size - sizeof(*pc);
    if ((uint64_t)pc->vertex_bytes + pc->fragment_bytes + pc->bindings_bytes > body) {
        lose(c, "bad program record");
        return;
    }
    Program **slotp = SLOT(c, programs, pc->id);
    if (*slotp) {
        dispatch_semaphore_wait((*slotp)->ready, DISPATCH_TIME_FOREVER);
        program_free(*slotp);
    }
    Program *p = calloc(1, sizeof(Program));
    p->ready = dispatch_semaphore_create(0);
    p->builds = dispatch_group_create();
    p->logic_lock = OS_UNFAIR_LOCK_INIT;
    *slotp = p;
    const char *data = (const char *)(pc + 1);
    CompileJob *j = calloc(1, sizeof(CompileJob));
    j->c = c;
    j->p = p;
    j->vs = strndup(data, pc->vertex_bytes);
    j->vs_len = strlen(j->vs);
    j->fs = strndup(data + pc->vertex_bytes, pc->fragment_bytes);
    j->fs_len = strlen(j->fs);
    j->bindings = malloc(pc->bindings_bytes + 1);
    memcpy(j->bindings, data + pc->vertex_bytes + pc->fragment_bytes, pc->bindings_bytes);
    j->bindings_len = pc->bindings_bytes;
    j->result = pc->result;
    j->capacity = pc->result_capacity;
    dispatch_async_f(g_compile_queue, j, compile_job);
}

static void program_destroy(Channel *c, uint32_t id_)
{
    if (id_ >= c->programs_cap || !c->programs[id_])
        return;
    Program *p = c->programs[id_];
    dispatch_semaphore_wait(p->ready, DISPATCH_TIME_FOREVER);
    program_free(p);
    c->programs[id_] = nullptr;
}

/* ---------------------------------------------------------------- states */

static void set_color_target(MTL4RenderPipelineColorAttachmentDescriptor *att, const MimeGpuColorTarget *t)
{
    att.pixelFormat = t->format;
    att.writeMask = t->write_mask;
    att.blendingState = t->blend ? MTL4BlendStateEnabled : MTL4BlendStateDisabled;
    if (t->blend) {
        att.rgbBlendOperation = t->rgb_op;
        att.alphaBlendOperation = t->alpha_op;
        att.sourceRGBBlendFactor = t->src_rgb;
        att.destinationRGBBlendFactor = t->dst_rgb;
        att.sourceAlphaBlendFactor = t->src_alpha;
        att.destinationAlphaBlendFactor = t->dst_alpha;
    }
}

/*
 * metal has no logic operation. apple gpus read the attachment in the fragment shader, so a variant of the program's
 * fragment function reads the first color target and applies the operation to its 8 bit values, as gl does to an 8 bit
 * unorm buffer, which is what the guest's framebuffers are. the order is gl's, from GL_CLEAR.
 */
static const char *const g_logic_expr[16] = {
    "0u",     "s & d",     "s & ~d",    "s",      "~s & d",  "d",    "s ^ d",   "s | d",
    "~(s | d)", "~(s ^ d)", "~d",       "s | ~d", "~s",      "~s | d", "~(s & d)", "255u",
};

static bool logic_format(uint32_t format)
{
    return format == MTLPixelFormatR8Unorm || format == MTLPixelFormatR8Unorm_sRGB ||
           format == MTLPixelFormatRG8Unorm || format == MTLPixelFormatRG8Unorm_sRGB ||
           format == MTLPixelFormatRGBA8Unorm || format == MTLPixelFormatRGBA8Unorm_sRGB ||
           format == MTLPixelFormatBGRA8Unorm || format == MTLPixelFormatBGRA8Unorm_sRGB;
}

static char *logic_source(const char *msl, const char *entry, int op)
{
    const char *color = strstr(msl, " [[color(0)]]");
    const char *fragment = strstr(msl, "fragment ");
    if (!color || !fragment)
        return nullptr;
    const char *name_end = color, *name = color;
    while (name > msl && (isalnum((unsigned char)name[-1]) || name[-1] == '_'))
        name--;
    if (name == name_end)
        return nullptr;
    char member[128];
    snprintf(member, sizeof(member), "%.*s", (int)(name_end - name), name);
    char signature[96];
    snprintf(signature, sizeof(signature), " %s(", entry);
    const char *open = strstr(fragment, signature);
    if (!open)
        return nullptr;
    open += strlen(signature);
    const char *body = strchr(open, '{');
    if (!body)
        return nullptr;
    char helper[512];
    snprintf(helper, sizeof(helper),
             "static inline float4 mime_logic(float4 src, float4 dst)\n"
             "{\n"
             "    uint4 s = uint4(rint(saturate(src) * 255.0)), d = uint4(rint(saturate(dst) * 255.0));\n"
             "    return float4((%s) & 255u) / 255.0;\n"
             "}\n\n",
             g_logic_expr[op]);
    char ret[256];
    snprintf(ret, sizeof(ret), "{ out.%s = decltype(out.%s)(mime_logic(float4(out.%s), mime_dst)); return out; }",
             member, member, member);
    const char *param = *open == ')' ? "float4 mime_dst [[color(0)]]" : "float4 mime_dst [[color(0)]], ";
    size_t size = strlen(msl) + strlen(helper) + strlen(param) + 1;
    for (const char *r = strstr(body, "return out;"); r; r = strstr(r + 1, "return out;"))
        size += strlen(ret);
    char *out = malloc(size), *o = out;
    if (!out)
        return nullptr;
    size_t head = (size_t)(fragment - msl);
    memcpy(o, msl, head), o += head;
    o = stpcpy(o, helper);
    memcpy(o, fragment, (size_t)(open - fragment)), o += open - fragment;
    o = stpcpy(o, param);
    for (const char *from = open;;) {
        const char *r = strstr(from, "return out;");
        if (!r || r < body) {
            if (r && r < body) {
                memcpy(o, from, (size_t)(r + 11 - from)), o += r + 11 - from;
                from = r + 11;
                continue;
            }
            o = stpcpy(o, from);
            break;
        }
        memcpy(o, from, (size_t)(r - from)), o += r - from;
        o = stpcpy(o, ret);
        from = r + 11;
    }
    return out;
}

/* the program's fragment function with the logic operation, or nil when the target or the source does not allow it. */
static MTL4LibraryFunctionDescriptor *logic_function(Program *p, uint8_t logic, uint32_t format)
{
    int op = logic - 1;
    if (op < 0 || op >= 16 || !p->fs_msl || !logic_format(format) || p->logic_failed[op])
        return nil;
    if (p->logic_fn[op])
        return p->logic_fn[op];
    char *source = logic_source(p->fs_msl, p->fs_entry, op);
    char *error = nullptr;
    p->logic_fn[op] = source ? make_function(source, p->fs_entry, &error) : nil;
    if (!p->logic_fn[op]) {
        mime_warn("gpu: no logic operation %d for a program: %s", op, error ? error : "its source has no color output");
        p->logic_failed[op] = true;
    }
    free(error);
    free(source);
    return p->logic_fn[op];
}

typedef struct PipelineJob {
    Program *program;
    Pipeline *pipeline;
    MimeGpuPipelineDesc desc;
    uint32_t id;
} PipelineJob;

/* on the compile queue: pipelines made together build at once, and draws wait only for the one they use. */
static void pipeline_job(void *arg)
{
    PipelineJob *j = arg;
    Program *p = j->program;
    const MimeGpuPipelineDesc *d = &j->desc;
    dispatch_semaphore_wait(p->ready, DISPATCH_TIME_FOREVER);
    dispatch_semaphore_signal(p->ready);
    @autoreleasepool {
        if (p->compiled) {
            MTL4RenderPipelineDescriptor *desc = [[[MTL4RenderPipelineDescriptor alloc] init] autorelease];
            desc.vertexFunctionDescriptor = p->fn[0];
            MTL4LibraryFunctionDescriptor *fragment = p->fn[1];
            /* copy is what drawing does anyway. */
            if (d->color[0].logic_op && d->color[0].logic_op != 1 + 3) {
                os_unfair_lock_lock(&p->logic_lock);
                MTL4LibraryFunctionDescriptor *logic = logic_function(p, d->color[0].logic_op, d->color[0].format);
                if (logic)
                    fragment = logic;
                os_unfair_lock_unlock(&p->logic_lock);
            }
            desc.fragmentFunctionDescriptor = fragment;
            MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
            for (int i = 0; i < MIME_GPU_MAX_ATTRIBS; i++) {
                const MimeGpuVertexAttrib *a = &d->attribs[i];
                if (!a->format)
                    continue;
                MTLVertexAttributeDescriptor *att = vd.attributes[i];
                att.format = a->format;
                att.offset = a->offset;
                att.bufferIndex = MIME_GPU_VERTEX_BUFFER_BASE + a->slot;
            }
            for (int i = 0; i < MIME_GPU_MAX_ATTRIBS; i++) {
                const MimeGpuVertexLayout *l = &d->layouts[i];
                bool used = false;
                for (int a = 0; a < MIME_GPU_MAX_ATTRIBS; a++)
                    used |= d->attribs[a].format && d->attribs[a].slot == i;
                if (!used)
                    continue;
                MTLVertexBufferLayoutDescriptor *lay = vd.layouts[MIME_GPU_VERTEX_BUFFER_BASE + i];
                if (!l->stride) {
                    /* a constant still needs a stride, never used. */
                    lay.stepFunction = MTLVertexStepFunctionConstant;
                    lay.stepRate = 0;
                    lay.stride = 16;
                } else {
                    lay.stride = l->stride;
                    lay.stepFunction = l->instance_divisor ? MTLVertexStepFunctionPerInstance
                                                           : MTLVertexStepFunctionPerVertex;
                    lay.stepRate = l->instance_divisor ? l->instance_divisor : 1;
                }
            }
            desc.vertexDescriptor = vd;
            for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
                if (d->color[i].format)
                    set_color_target(desc.colorAttachments[i], &d->color[i]);
            desc.rasterSampleCount = d->samples ? d->samples : 1;
            desc.inputPrimitiveTopology = d->topology;
            id err = nil;
            j->pipeline->state = mime_pipeline_cache_render(g_cache, desc, &err);
            if (!j->pipeline->state)
                mime_warn("gpu: pipeline %u: %s", j->id, error_text(err));
        }
    }
    dispatch_semaphore_signal(j->pipeline->ready);
    dispatch_group_leave(p->builds);
    free(j);
}

static id<MTLRenderPipelineState> pipeline_state(Pipeline *pl)
{
    if (!pl)
        return nil;
    dispatch_semaphore_wait(pl->ready, DISPATCH_TIME_FOREVER);
    dispatch_semaphore_signal(pl->ready);
    return pl->state;
}

static void pipeline_free(Channel *c, Pipeline *pl)
{
    if (!pl)
        return;
    retire(c, pipeline_state(pl), RETIRE_RELEASE);
    dispatch_release(pl->ready);
    free(pl);
}

static void pipeline_create(Channel *c, const MimeGpuPipelineCreate *pc)
{
    Pipeline **slotp = SLOT(c, pipelines, pc->id);
    pipeline_free(c, *slotp);
    *slotp = nullptr;
    const MimeGpuPipelineDesc *d = &pc->desc;
    Program *p = d->program < c->programs_cap ? c->programs[d->program] : nullptr;
    if (!p)
        return;
    Pipeline *pl = calloc(1, sizeof(*pl));
    pl->ready = dispatch_semaphore_create(0);
    *slotp = pl;
    PipelineJob *j = mime_xmalloc(sizeof(*j));
    *j = (PipelineJob){.program = p, .pipeline = pl, .desc = *d, .id = pc->id};
    dispatch_group_enter(p->builds);
    dispatch_async_f(g_compile_queue, j, pipeline_job);
}

static void stencil_face(MTLStencilDescriptor *sd, const MimeGpuStencilFace *f)
{
    sd.stencilCompareFunction = f->func;
    sd.stencilFailureOperation = f->fail;
    sd.depthFailureOperation = f->depth_fail;
    sd.depthStencilPassOperation = f->pass;
    sd.readMask = f->read_mask;
    sd.writeMask = f->write_mask;
}

static id<MTLDepthStencilState> new_depth_state(const MimeGpuDepthStencilCreate *d)
{
    MTLDepthStencilDescriptor *desc = [[MTLDepthStencilDescriptor alloc] init];
    desc.depthCompareFunction = d->depth_func;
    desc.depthWriteEnabled = d->depth_write != 0;
    if (d->stencil) {
        MTLStencilDescriptor *front = [[MTLStencilDescriptor alloc] init];
        MTLStencilDescriptor *back = [[MTLStencilDescriptor alloc] init];
        stencil_face(front, &d->front);
        stencil_face(back, &d->back);
        desc.frontFaceStencil = front;
        desc.backFaceStencil = back;
        [front release];
        [back release];
    }
    id<MTLDepthStencilState> s = [g_device newDepthStencilStateWithDescriptor:desc];
    [desc release];
    return s;
}

static void sampler_create(Channel *c, const MimeGpuSamplerCreate *s)
{
    id<MTLSamplerState> *slotp = SLOT(c, samplers, s->id);
    retire(c, *slotp, RETIRE_RELEASE);
    MTLSamplerDescriptor *d = [[MTLSamplerDescriptor alloc] init];
    d.supportArgumentBuffers = YES;
    d.minFilter = s->min_filter;
    d.magFilter = s->mag_filter;
    d.mipFilter = s->mip_filter;
    d.sAddressMode = s->address_s;
    d.tAddressMode = s->address_t;
    d.rAddressMode = s->address_r;
    d.borderColor = s->border;
    d.maxAnisotropy = s->max_anisotropy ? s->max_anisotropy : 1;
    d.lodMinClamp = s->lod_min;
    d.lodMaxClamp = s->lod_max;
    if (s->compare)
        d.compareFunction = s->compare;
    *slotp = [g_device newSamplerStateWithDescriptor:d];
    [d release];
}

/* ---------------------------------------------------------------- surfaces */

static id<MTLRenderPipelineState> blit_pipeline(Channel *c, uint32_t format);
static id<MTL4RenderCommandEncoder> new_encoder(Channel *c, MTL4RenderPassDescriptor *rp);

static id<MTLSamplerState> blit_sampler(Channel *c, bool linear)
{
    if (!c->blit_samplers[linear]) {
        MTLSamplerDescriptor *d = [[MTLSamplerDescriptor alloc] init];
        d.supportArgumentBuffers = YES;
        d.minFilter = d.magFilter = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        c->blit_samplers[linear] = [g_device newSamplerStateWithDescriptor:d];
        [d release];
    }
    return c->blit_samplers[linear];
}

static void surface_config(Channel *c, const MimeGpuSurfaceConfig *sc)
{
    Surface *s = SLOT(c, surfaces, sc->id);
    uint32_t w = sc->width ? sc->width : 1, h = sc->height ? sc->height : 1;
    uint32_t samples = sc->samples ? sc->samples : 1;
    if (!s->color || s->width != w || s->height != h || s->samples != samples ||
        s->depth_format != sc->depth_format || s->stencil_format != sc->stencil_format) {
        end_render(c);
        retire(c, s->color, RETIRE_RESIDENT);
        retire(c, s->depth, RETIRE_RESIDENT);
        s->depth = nil;
        MTLTextureType type = samples > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
        s->color = new_texture(c, type, MTLPixelFormatBGRA8Unorm, w, h, 1, 1, samples,
                               MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget, 0);
        uint32_t ds = sc->depth_format && sc->stencil_format ? MTLPixelFormatDepth32Float_Stencil8
                      : sc->depth_format                     ? sc->depth_format
                                                             : sc->stencil_format;
        if (ds)
            s->depth = new_texture(c, type, ds, w, h, 1, 1, samples, MTLTextureUsageRenderTarget, 0);
        track_reset(&s->color_track, true);
        track_reset(&s->depth_track, true);
        s->width = w;
        s->height = h;
        s->samples = samples;
        s->depth_format = sc->depth_format;
        s->stencil_format = sc->stencil_format;
    }
    if (!s->layer) {
        /* with metalfx the drawables have the display's pixels, and a stretched window upscales into them. */
        s->layer = mime_ui_metal_layer_create(g_device, true, g_metalfx);
        s->layer_set = [((__bridge CAMetalLayer *)s->layer).residencySet retain];
        [c->queue addResidencySet:s->layer_set];
    }
    s->window_id = sc->window_id;
    s->sync = sc->sync != 0;
    mime_ui_metal_layer_place(s->layer, sc->window_id, sc->x, sc->y, w, h, s->sync, sc->order);
}

static void drop_scaler(Channel *c, Surface *s)
{
    retire(c, s->scaler, RETIRE_RELEASE);
    retire(c, s->scaled, RETIRE_RESIDENT);
    s->scaler = nil;
    s->scaled = nil;
}

static void surface_destroy(Channel *c, uint32_t id_)
{
    if (id_ >= c->surfaces_cap)
        return;
    Surface *s = &c->surfaces[id_];
    if (in_pass(c, id_, true))
        end_render(c);
    retire(c, s->color, RETIRE_RESIDENT);
    retire(c, s->depth, RETIRE_RESIDENT);
    drop_scaler(c, s);
    retire(c, s->layer_set, RETIRE_QUEUE_SET);
    if (s->layer)
        mime_ui_metal_layer_destroy(s->layer);
    memset(s, 0, sizeof(*s));
}

/* metalfx encodes encoders of its own, which do not wait for ours unless a barrier makes them. */
static void barrier_before_metalfx(Channel *c)
{
    end_render(c);
    end_blit(c);
    id<MTL4ComputeCommandEncoder> e = [command_buffer(c) computeCommandEncoder];
    [e barrierAfterStages:MTLStageAll beforeQueueStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    [e endEncoding];
}

/* a stretched window shows its back buffer upscaled by metalfx rather than filtered by the compositor. */
static bool upscale(Channel *c, Surface *s, id<MTLTexture> dst)
{
    if (!g_metalfx || (dst.width == s->unscalable_width && dst.height == s->unscalable_height))
        return false;
    id<MTL4FXSpatialScaler> scaler = s->scaler;
    if (!scaler || scaler.inputWidth != s->width || scaler.inputHeight != s->height ||
        scaler.outputWidth != dst.width || scaler.outputHeight != dst.height) {
        drop_scaler(c, s);
        MTLFXSpatialScalerDescriptor *d = [[MTLFXSpatialScalerDescriptor alloc] init];
        d.inputWidth = s->width;
        d.inputHeight = s->height;
        d.outputWidth = dst.width;
        d.outputHeight = dst.height;
        d.colorTextureFormat = s->color.pixelFormat;
        d.outputTextureFormat = dst.pixelFormat;
        d.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
        scaler = [d newSpatialScalerWithDevice:g_device compiler:g_metalfx_compiler];
        [d release];
        if (!scaler || (s->color.usage & scaler.colorTextureUsage) != scaler.colorTextureUsage) {
            [scaler release];
            s->unscalable_width = (uint32_t)dst.width;
            s->unscalable_height = (uint32_t)dst.height;
            return false;
        }
        s->scaler = scaler;
        /* the output has to be private, which a drawable may not be. */
        MTLTextureUsage usage = scaler.outputTextureUsage;
        if (dst.storageMode != MTLStorageModePrivate || (dst.usage & usage) != usage)
            s->scaled = new_texture(c, MTLTextureType2D, dst.pixelFormat, (uint32_t)dst.width, (uint32_t)dst.height,
                                    1, 1, 1, usage, 0);
    }
    barrier_before_metalfx(c);
    scaler.colorTexture = s->color;
    scaler.outputTexture = s->scaled ? s->scaled : dst;
    scaler.inputContentWidth = s->width;
    scaler.inputContentHeight = s->height;
    [scaler encodeToCommandBuffer:command_buffer(c)];
    if (s->scaled) {
        MTLOrigin zero = {0, 0, 0};
        MTLSize size = {dst.width, dst.height, 1};
        [blit_encoder(c) copyFromTexture:s->scaled sourceSlice:0 sourceLevel:0 sourceOrigin:zero sourceSize:size
                               toTexture:dst destinationSlice:0 destinationLevel:0 destinationOrigin:zero];
        end_blit(c);
    }
    return true;
}

/* without metalfx a stretched back buffer is filtered over the whole drawable. */
static void stretch(Channel *c, id<MTLTexture> src, id<MTLTexture> dst)
{
    id<MTLRenderPipelineState> ps = blit_pipeline(c, (uint32_t)dst.pixelFormat);
    if (!ps)
        return;
    end_render(c);
    end_blit(c);
    MTL4RenderPassDescriptor *rp = [[[MTL4RenderPassDescriptor alloc] init] autorelease];
    rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTL4RenderCommandEncoder> e = new_encoder(c, rp);
    float uv[4] = {0, 0, 1, 1};
    [e setViewport:(MTLViewport){0, 0, dst.width, dst.height, 0, 1}];
    [e setRenderPipelineState:ps];
    [c->tables[MIME_GPU_STAGE_VERTEX] setAddress:upload(c, uv, sizeof(uv)) atIndex:0];
    [c->tables[MIME_GPU_STAGE_FRAGMENT] setTexture:src.gpuResourceID atIndex:0];
    [c->tables[MIME_GPU_STAGE_FRAGMENT] setSamplerState:blit_sampler(c, true).gpuResourceID atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e endEncoding];
    [e release];
}

/*
 * copies the back buffer into the layer's next drawable, or upscales it into a larger one; the layer flips it to
 * OpenGL's orientation.
 */
static void present(Channel *c, uint32_t id_)
{
    if (id_ >= c->surfaces_cap || !c->surfaces[id_].layer || !c->surfaces[id_].window_id) {
        commit(c, 0);
        return;
    }
    Surface *s = &c->surfaces[id_];
    if (s->samples > 1) {
        /* drawables are single sampled: resolve into a temporary texture first. */
        commit(c, 0);
        return;
    }
    end_render(c);
    materialize(c, id_, true, false);
    id<CAMetalDrawable> drawable = [(__bridge CAMetalLayer *)s->layer nextDrawable];
    if (!drawable) {
        commit(c, 0);
        return;
    }
    id<MTLTexture> dst = drawable.texture;
    if (dst.width > s->width || dst.height > s->height) {
        if (!upscale(c, s, dst))
            stretch(c, s->color, dst);
    } else {
        MTLOrigin zero = {0, 0, 0};
        MTLSize size = {dst.width, dst.height, 1};
        [blit_encoder(c) copyFromTexture:s->color sourceSlice:0 sourceLevel:0 sourceOrigin:zero sourceSize:size
                               toTexture:dst destinationSlice:0 destinationLevel:0 destinationOrigin:zero];
        end_blit(c);
    }
    [c->queue waitForDrawable:drawable];
    commit(c, 0);
    [c->queue signalDrawable:drawable];
    [drawable present];
}

/* ---------------------------------------------------------------- passes */

#define ATTACHMENTS (MIME_GPU_MAX_COLORS + 2)

/* metal deprecated the packed 24 bit depth formats, 255 and 262, which the guest may still name. */
static bool has_depth(MTLPixelFormat f)
{
    return f == MTLPixelFormatDepth16Unorm || f == MTLPixelFormatDepth32Float || f == 255 ||
           f == MTLPixelFormatDepth32Float_Stencil8;
}

static bool has_stencil(MTLPixelFormat f)
{
    return f == MTLPixelFormatStencil8 || f == 255 || f == MTLPixelFormatDepth32Float_Stencil8 ||
           f == MTLPixelFormatX32_Stencil8 || f == 262;
}

static Track *track_of(Channel *c, uint32_t id_, bool surface, bool depth)
{
    Track *t = nullptr;
    if (surface) {
        if (id_ < c->surfaces_cap)
            t = depth ? &c->surfaces[id_].depth_track : &c->surfaces[id_].color_track;
    } else if (id_ < c->textures_cap) {
        t = &c->textures[id_].track;
    }
    return t && t->tracked ? t : nullptr;
}

static void track_reset(Track *t, bool tracked)
{
    memset(t, 0, sizeof(*t));
    t->tracked = tracked;
    t->aspect[0].kind = t->aspect[1].kind = tracked ? TRACK_UNDEFINED : TRACK_DATA;
}

/* the attachment of the guest pass at index i: colors, then depth and stencil. */
static MimeGpuAttachment *pass_att(Channel *c, int i)
{
    return i < MIME_GPU_MAX_COLORS ? &c->pass.color[i] : i == MIME_GPU_MAX_COLORS ? &c->pass.depth : &c->pass.stencil;
}

/* returns the tracked attachment aspect, or NULL when untracked. */
static Aspect *pass_aspect(Channel *c, int i)
{
    MimeGpuAttachment *a = pass_att(c, i);
    if (!a->id || a->level || a->slice || a->resolve)
        return nullptr;
    Track *t = track_of(c, a->id, a->surface, i >= MIME_GPU_MAX_COLORS);
    return t ? &t->aspect[i == MIME_GPU_MAX_COLORS + 1] : nullptr;
}

static void aspect_clear_value(Channel *c, int i, Aspect *as)
{
    as->kind = TRACK_CLEARED;
    as->materialized = false;
    if (i < MIME_GPU_MAX_COLORS)
        memcpy(as->value, c->pass.clear_color[i], sizeof(as->value));
    else if (i == MIME_GPU_MAX_COLORS)
        as->value[0] = c->pass.clear_depth;
    else
        as->stencil = c->pass.clear_stencil;
}

static bool pending(const Track *t)
{
    return t && ((t->aspect[0].kind == TRACK_CLEARED && !t->aspect[0].materialized) ||
                 (t->aspect[1].kind == TRACK_CLEARED && !t->aspect[1].materialized));
}

static void set_data(Track *t)
{
    if (t)
        t->aspect[0].kind = t->aspect[1].kind = TRACK_DATA;
}

/* the pass whose encoder ended continues by loading what it left. */
static void pass_continue(Channel *c)
{
    for (int i = 0; i < ATTACHMENTS; i++)
        pass_att(c, i)->load = MTLLoadActionLoad;
    c->written = 0;
}

static id<MTL4RenderCommandEncoder> new_encoder(Channel *c, MTL4RenderPassDescriptor *rp)
{
    end_blit(c);
    id<MTL4RenderCommandEncoder> e = [[command_buffer(c) renderCommandEncoderWithDescriptor:rp] retain];
    if (e) {
        wait_queue(e);
        [e setArgumentTable:c->tables[MIME_GPU_STAGE_VERTEX] atStages:MTLRenderStageVertex];
        [e setArgumentTable:c->tables[MIME_GPU_STAGE_FRAGMENT] atStages:MTLRenderStageFragment];
    }
    return e;
}

/* opens the pass encoder using tracked attachment contents for load actions. */
static bool ensure_encoder(Channel *c)
{
    if (c->render)
        return true;
    if (!c->pass_live)
        return false;
    MTL4RenderPassDescriptor *rp = [[[MTL4RenderPassDescriptor alloc] init] autorelease];
    for (int i = 0; i < ATTACHMENTS; i++) {
        MimeGpuAttachment *a = pass_att(c, i);
        c->enc_unknown[i] = false;
        if (!a->id)
            continue;
        id<MTLTexture> tex = texture_of(c, a->id, a->surface, i >= MIME_GPU_MAX_COLORS);
        if (!tex) {
            c->pass_live = false;
            return false;
        }
        Aspect *as = pass_aspect(c, i);
        uint8_t load = a->load, store = a->store;
        if (as && load == MTLLoadActionLoad && as->kind == TRACK_UNDEFINED) {
            load = MTLLoadActionDontCare;
        } else if (as && load == MTLLoadActionLoad && as->kind == TRACK_CLEARED) {
            load = MTLLoadActionClear;
            if (i < MIME_GPU_MAX_COLORS)
                memcpy(c->pass.clear_color[i], as->value, sizeof(as->value));
            else if (i == MIME_GPU_MAX_COLORS)
                c->pass.clear_depth = as->value[0];
            else
                c->pass.clear_stencil = as->stencil;
        }
        /* decides a tracked attachment's store action when the encoder ends. */
        if (as && store == MTLStoreActionStore) {
            store = MTLStoreActionUnknown;
            c->enc_unknown[i] = true;
        }
        c->enc_load[i] = load;
        MTLRenderPassAttachmentDescriptor *att;
        if (i < MIME_GPU_MAX_COLORS) {
            const float *v = c->pass.clear_color[i];
            rp.colorAttachments[i].clearColor = MTLClearColorMake(v[0], v[1], v[2], v[3]);
            att = rp.colorAttachments[i];
        } else if (i == MIME_GPU_MAX_COLORS) {
            rp.depthAttachment.clearDepth = c->pass.clear_depth;
            att = rp.depthAttachment;
        } else {
            rp.stencilAttachment.clearStencil = c->pass.clear_stencil;
            att = rp.stencilAttachment;
        }
        att.texture = tex;
        att.level = a->level;
        att.slice = a->slice;
        att.loadAction = load;
        att.storeAction = store;
        if (a->resolve)
            att.resolveTexture = texture_of(c, a->resolve, false, false);
    }
    rp.renderTargetWidth = c->pass.width;
    rp.renderTargetHeight = c->pass.height;
    c->pass_started = true;
    c->written = 0;
    c->render = new_encoder(c, rp);
    c->st.dirty = DIRTY_ALL;
    return c->render != nil;
}

/*
 * stores tracked attachments only if written or loaded. an untouched clear stays deferred for the next pass. a
 * segment without an encoder does no GPU work when all attachments are tracked.
 */
static void end_render(Channel *c)
{
    if (!c->render) {
        if (!c->pass_live)
            return;
        bool gpu = false;
        for (int i = 0; i < ATTACHMENTS; i++) {
            MimeGpuAttachment *a = pass_att(c, i);
            if (a->id && a->load == MTLLoadActionClear && !pass_aspect(c, i))
                gpu = true;
        }
        if (!gpu) {
            for (int i = 0; i < ATTACHMENTS; i++) {
                Aspect *as = pass_aspect(c, i);
                uint8_t load = pass_att(c, i)->load;
                if (as && load == MTLLoadActionClear)
                    aspect_clear_value(c, i, as);
                else if (as && load == MTLLoadActionDontCare)
                    as->kind = TRACK_UNDEFINED;
            }
            pass_continue(c);
            return;
        }
        if (!ensure_encoder(c))
            return;
    }
    for (int i = 0; i < ATTACHMENTS; i++) {
        MimeGpuAttachment *a = pass_att(c, i);
        if (!a->id)
            continue;
        Aspect *as = pass_aspect(c, i);
        bool written = (c->written >> i) & 1;
        uint8_t store = a->store;
        if (c->enc_unknown[i]) {
            store = written || c->enc_load[i] == MTLLoadActionLoad ? MTLStoreActionStore : MTLStoreActionDontCare;
            if (i < MIME_GPU_MAX_COLORS)
                [c->render setColorStoreAction:store atIndex:i];
            else if (i == MIME_GPU_MAX_COLORS)
                [c->render setDepthStoreAction:store];
            else
                [c->render setStencilStoreAction:store];
        }
        if (as) {
            if (store != MTLStoreActionDontCare)
                as->kind = TRACK_DATA;
            else if (c->enc_load[i] == MTLLoadActionClear && !written)
                aspect_clear_value(c, i, as);
            else
                as->kind = TRACK_UNDEFINED;
        }
        if (a->resolve)
            set_data(track_of(c, a->resolve, false, false));
    }
    [c->render endEncoding];
    [c->render release];
    c->render = nil;
    pass_continue(c);
}

/* materializes a deferred clear before reads outside passes. */
static void materialize(Channel *c, uint32_t id_, bool surface, bool depth)
{
    Track *t = track_of(c, id_, surface, depth);
    id<MTLTexture> tex = texture_of(c, id_, surface, depth);
    if (!pending(t) || !tex)
        return;
    end_render(c);
    end_blit(c);
    if (!pending(t))
        return;
    MTLPixelFormat format = tex.pixelFormat;
    MTL4RenderPassDescriptor *rp = [[[MTL4RenderPassDescriptor alloc] init] autorelease];
    for (int k = 0; k < 2; k++) {
        Aspect *as = &t->aspect[k];
        MTLRenderPassAttachmentDescriptor *att;
        if (has_depth(format) || has_stencil(format)) {
            if (k == 0 && !has_depth(format))
                continue;
            if (k == 1 && !has_stencil(format))
                continue;
            if (k) {
                rp.stencilAttachment.clearStencil = as->stencil;
                att = rp.stencilAttachment;
            } else {
                rp.depthAttachment.clearDepth = as->value[0];
                att = rp.depthAttachment;
            }
        } else {
            if (k)
                continue;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(as->value[0], as->value[1], as->value[2],
                                                                  as->value[3]);
            att = rp.colorAttachments[0];
        }
        att.texture = tex;
        att.loadAction = as->kind == TRACK_CLEARED ? MTLLoadActionClear
                         : as->kind == TRACK_DATA  ? MTLLoadActionLoad
                                                   : MTLLoadActionDontCare;
        att.storeAction = MTLStoreActionStore;
        if (as->kind == TRACK_CLEARED)
            as->materialized = true;
    }
    id<MTL4RenderCommandEncoder> e = new_encoder(c, rp);
    [e endEncoding];
    [e release];
}

/* a write outside passes: what it does not cover keeps the old contents. */
static void write_texture(Channel *c, uint32_t id_, bool surface, bool depth, bool whole)
{
    Track *t = track_of(c, id_, surface, depth);
    if (!t)
        return;
    if (!whole)
        materialize(c, id_, surface, depth);
    set_data(t);
}

static bool covers(Channel *c, uint32_t id_, bool surface, uint32_t level, uint32_t slice, int64_t x, int64_t y,
                   int64_t w, int64_t h)
{
    uint32_t tw = 0, th = 0;
    if (surface && id_ < c->surfaces_cap) {
        tw = c->surfaces[id_].width;
        th = c->surfaces[id_].height;
    } else if (!surface && id_ < c->textures_cap) {
        tw = c->textures[id_].width;
        th = c->textures[id_].height;
    }
    return !level && !slice && x <= 0 && y <= 0 && x + w >= tw && y + h >= th && tw && th;
}

/* draws read the textures they sample from memory. */
static void materialize_bound(Channel *c)
{
    for (int s = 0; s < MIME_GPU_STAGES; s++) {
        for (uint32_t m = c->st.tex_mask[s], i; m; m &= m - 1) {
            i = (uint32_t)__builtin_ctz(m);
            Binding *b = &c->st.tex[s][i];
            if (pending(track_of(c, b->id, b->surface, false)))
                materialize(c, b->id, b->surface, false);
        }
    }
}

static uint16_t format_of(Channel *c, const MimeGpuAttachment *a, bool depth)
{
    if (!a->id)
        return 0;
    if (a->surface) {
        Surface *s = a->id < c->surfaces_cap ? &c->surfaces[a->id] : nullptr;
        if (!s || !s->color)
            return 0;
        if (!depth)
            return MTLPixelFormatBGRA8Unorm;
        return s->depth_format && s->stencil_format ? MTLPixelFormatDepth32Float_Stencil8
               : s->depth_format                     ? s->depth_format
                                                     : s->stencil_format;
    }
    return a->id < c->textures_cap ? (uint16_t)c->textures[a->id].format : 0;
}

/* records the guest pass; its encoder waits for the first draw. */
static void pass_begin(Channel *c, const MimeGpuPass *p)
{
    end_render(c);
    c->pass = *p;
    c->pass_live = true;
    c->pass_started = false;
    c->written = 0;
    c->pass_samples = 0;
    for (int i = 0; i < ATTACHMENTS; i++) {
        MimeGpuAttachment *a = pass_att(c, i);
        uint16_t f = format_of(c, a, i >= MIME_GPU_MAX_COLORS);
        if (i < MIME_GPU_MAX_COLORS)
            c->pass_color[i] = f;
        else if (i == MIME_GPU_MAX_COLORS)
            c->pass_depth = f == MTLPixelFormatStencil8 ? 0 : f;
        else
            c->pass_stencil = f == MTLPixelFormatDepth32Float ? 0 : f;
        id<MTLTexture> t = a->id ? texture_of(c, a->id, a->surface, i >= MIME_GPU_MAX_COLORS) : nil;
        if (t && !c->pass_samples)
            c->pass_samples = (uint32_t)t.sampleCount;
    }
    if (!c->pass_samples)
        c->pass_samples = 1;
    c->pass_width = p->width;
    c->pass_height = p->height;
    c->st.dirty = DIRTY_ALL;
}

static bool in_pass(Channel *c, uint32_t id_, bool surface)
{
    if (!c->pass_live)
        return false;
    for (int i = 0; i < ATTACHMENTS; i++) {
        MimeGpuAttachment *a = pass_att(c, i);
        if (a->id == id_ && !a->surface == !surface)
            return true;
    }
    return false;
}

static bool flush_state(Channel *c)
{
    if (!c->st.pipeline || !ensure_encoder(c))
        return false;
    id<MTL4RenderCommandEncoder> e = c->render;
    uint32_t d = c->st.dirty;
    if (d & DIRTY_PIPELINE)
        [e setRenderPipelineState:c->st.pipeline];
    if (d & DIRTY_DEPTH) {
        if (c->st.depth_state)
            [e setDepthStencilState:c->st.depth_state];
        [e setStencilFrontReferenceValue:c->st.front_ref backReferenceValue:c->st.back_ref];
    }
    if (d & DIRTY_RASTER) {
        [e setCullMode:c->st.raster.cull];
        [e setFrontFacingWinding:c->st.raster.winding];
        [e setTriangleFillMode:c->st.raster.fill];
        [e setDepthBias:c->st.raster.depth_bias slopeScale:c->st.raster.slope_scale clamp:c->st.raster.bias_clamp];
        [e setDepthClipMode:c->st.raster.depth_clip ? MTLDepthClipModeClip : MTLDepthClipModeClamp];
    }
    if (d & DIRTY_VIEWPORT)
        [e setViewport:c->st.viewport];
    if (d & DIRTY_SCISSOR) {
        /* Metal wants the rectangle inside the attachments. */
        int64_t x0 = 0, y0 = 0, x1 = c->pass_width, y1 = c->pass_height;
        if (c->st.has_scissor) {
            x0 = c->st.scissor.x > 0 ? c->st.scissor.x : 0;
            y0 = c->st.scissor.y > 0 ? c->st.scissor.y : 0;
            int64_t r = (int64_t)c->st.scissor.x + c->st.scissor.width, b = (int64_t)c->st.scissor.y + c->st.scissor.height;
            x1 = r < x1 ? r : x1;
            y1 = b < y1 ? b : y1;
        }
        MTLScissorRect sr = {0, 0, 0, 0};
        if (x1 > x0 && y1 > y0)
            sr = (MTLScissorRect){(NSUInteger)x0, (NSUInteger)y0, (NSUInteger)(x1 - x0), (NSUInteger)(y1 - y0)};
        [e setScissorRect:sr];
    }
    if (d & DIRTY_BLEND)
        [e setBlendColorRed:c->st.blend[0] green:c->st.blend[1] blue:c->st.blend[2] alpha:c->st.blend[3]];
    id<MTL4ArgumentTable> vt = c->tables[MIME_GPU_STAGE_VERTEX], ft = c->tables[MIME_GPU_STAGE_FRAGMENT];
    if (d & DIRTY_VB) {
        for (uint32_t m = c->st.vb_mask, i; m; m &= m - 1) {
            i = (uint32_t)__builtin_ctz(m);
            uint64_t off;
            id<MTLBuffer> buf = heap(c, &c->st.vb[i], 0, &off);
            if (buf)
                [vt setAddress:buf.gpuAddress + off atIndex:MIME_GPU_VERTEX_BUFFER_BASE + i];
        }
    }
    if ((d & DIRTY_UNIFORMS) && c->st.uniform_bytes) {
        MTLGPUAddress uniforms = upload(c, c->st.uniforms, c->st.uniform_bytes);
        [vt setAddress:uniforms atIndex:MIME_GPU_UNIFORM_BUFFER];
        [ft setAddress:uniforms atIndex:MIME_GPU_UNIFORM_BUFFER];
    }
    if (d & DIRTY_UNIFORMS) {
        for (int stage = 0; stage < MIME_GPU_STAGES; stage++) {
            for (uint32_t mask = c->st.uniform_buffer_mask[stage]; mask; mask &= mask - 1) {
                uint32_t index = (uint32_t)__builtin_ctz(mask);
                uint64_t offset;
                id<MTLBuffer> buffer = heap(c, &c->st.uniform_buffers[stage][index], 0, &offset);
                if (buffer)
                    [c->tables[stage] setAddress:buffer.gpuAddress + offset atIndex:index + 1];
            }
        }
    }
    if (d & DIRTY_TEXTURES) {
        for (int s = 0; s < MIME_GPU_STAGES; s++) {
            for (uint32_t m = c->st.tex_mask[s], i; m; m &= m - 1) {
                i = (uint32_t)__builtin_ctz(m);
                Binding *b = &c->st.tex[s][i];
                id<MTLTexture> t = texture_of(c, b->id, b->surface, false);
                if (!b->surface && b->id < c->textures_cap && c->textures[b->id].sampled)
                    t = c->textures[b->id].sampled;
                id<MTLSamplerState> smp = b->sampler < c->samplers_cap ? c->samplers[b->sampler] : nil;
                [c->tables[s] setTexture:t.gpuResourceID atIndex:i];
                if (smp && i < MAX_SAMPLERS)
                    [c->tables[s] setSamplerState:smp.gpuResourceID atIndex:i];
            }
        }
    }
    c->st.dirty = 0;
    return true;
}

/* ---------------------------------------------------------------- internal shaders */

static const char g_internal_msl[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct ClearOut { float4 pos [[position]]; };\n"
    "vertex ClearOut mime_clear_vs(uint vid [[vertex_id]], constant float &depth [[buffer(0)]]) {\n"
    "  float2 p = float2((vid << 1) & 2, vid & 2);\n"
    "  ClearOut o; o.pos = float4(p * 2.0 - 1.0, depth, 1.0); return o;\n"
    "}\n"
    "struct BlitOut { float4 pos [[position]]; float2 uv; };\n"
    "vertex BlitOut mime_blit_vs(uint vid [[vertex_id]], constant float4 &src [[buffer(0)]]) {\n"
    "  float2 p = float2((vid << 1) & 2, vid & 2);\n"
    "  BlitOut o; o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0); o.pos.y = -o.pos.y;\n"
    "  o.uv = src.xy + (src.zw - src.xy) * p; return o;\n"
    "}\n"
    "fragment float4 mime_blit_fs(BlitOut in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) {\n"
    "  return t.sample(s, in.uv);\n"
    "}\n";

static MTL4LibraryFunctionDescriptor *internal_function(Channel *c, const char *name)
{
    if (!c->internal_library) {
        NSError *err = nil;
        c->internal_library = [g_device newLibraryWithSource:[NSString stringWithUTF8String:g_internal_msl]
                                                     options:nil
                                                       error:&err];
        if (!c->internal_library) {
            mime_warn("gpu: internal shaders: %s", error_text(err));
            return nil;
        }
    }
    MTL4LibraryFunctionDescriptor *fn = [[MTL4LibraryFunctionDescriptor alloc] init];
    fn.library = c->internal_library;
    fn.name = @(name);
    return fn;
}

/* a clear draws through the scissor with a fragment function writing the color to every target. */
static id<MTLRenderPipelineState> clear_pipeline(Channel *c, uint32_t color_mask)
{
    uint64_t key = (uint64_t)c->pass_depth | (uint64_t)c->pass_stencil << 16 | (uint64_t)c->pass_samples << 32 |
                   (uint64_t)color_mask << 40;
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
        key ^= (uint64_t)c->pass_color[i] * (0x9E3779B97F4A7C15ull >> i);
    for (int i = 0; i < 16; i++)
        if (c->clear_pipelines[i] && c->clear_keys[i] == key)
            return c->clear_pipelines[i];

    /* every member of a fragment result needs an attribute: without colors the function returns nothing. */
    char src[2048];
    bool colors_used = false;
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
        colors_used |= c->pass_color[i] != 0;
    int n = snprintf(src, sizeof(src), "#include <metal_stdlib>\nusing namespace metal;\n");
    if (colors_used) {
        n += snprintf(src + n, sizeof(src) - (size_t)n, "struct O {");
        for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
            if (c->pass_color[i])
                n += snprintf(src + n, sizeof(src) - (size_t)n, " float4 c%d [[color(%d)]];", i, i);
        n += snprintf(src + n, sizeof(src) - (size_t)n,
                      " };\nfragment O mime_clear_fs(constant float4 &color [[buffer(0)]]) { O o;");
        for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
            if (c->pass_color[i])
                n += snprintf(src + n, sizeof(src) - (size_t)n, " o.c%d = color;", i);
        snprintf(src + n, sizeof(src) - (size_t)n, " return o; }\n");
    } else {
        snprintf(src + n, sizeof(src) - (size_t)n, "fragment void mime_clear_fs() {}\n");
    }
    char *error = nullptr;
    MTL4LibraryFunctionDescriptor *fs = make_function(src, "mime_clear_fs", &error);
    free(error);
    MTL4LibraryFunctionDescriptor *vs = internal_function(c, "mime_clear_vs");
    if (!fs || !vs) {
        [fs release];
        [vs release];
        return nil;
    }
    MTL4RenderPipelineDescriptor *desc = [[MTL4RenderPipelineDescriptor alloc] init];
    desc.vertexFunctionDescriptor = vs;
    desc.fragmentFunctionDescriptor = fs;
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++) {
        if (!c->pass_color[i])
            continue;
        MimeGpuColorTarget t = {.format = c->pass_color[i], .write_mask = (uint8_t)color_mask};
        set_color_target(desc.colorAttachments[i], &t);
    }
    desc.rasterSampleCount = c->pass_samples;
    id err = nil;
    id<MTLRenderPipelineState> ps = mime_pipeline_cache_render(g_cache, desc, &err);
    if (!ps)
        mime_warn("gpu: clear pipeline: %s", error_text(err));
    [desc release];
    [vs release];
    [fs release];
    int victim = (int)(key % 16);
    retire(c, c->clear_pipelines[victim], RETIRE_RELEASE);
    c->clear_pipelines[victim] = ps;
    c->clear_keys[victim] = key;
    return ps;
}

static void clear(Channel *c, const MimeGpuClear *cl)
{
    if (!c->pass_live)
        return;
    uint32_t mask = cl->mask;
    bool any_color = false;
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
        any_color |= c->pass_color[i] != 0;
    if (!any_color || !cl->color_mask)
        mask &= ~MIME_GPU_CLEAR_COLOR;
    if (!c->pass_depth)
        mask &= ~MIME_GPU_CLEAR_DEPTH;
    if (!c->pass_stencil)
        mask &= ~MIME_GPU_CLEAR_STENCIL;
    if (!mask)
        return;
    /* before the first draw, a clear of whole attachments is their load action. */
    bool whole = !c->st.has_scissor || (c->st.scissor.x <= 0 && c->st.scissor.y <= 0 &&
                                        (int64_t)c->st.scissor.x + c->st.scissor.width >= c->pass_width &&
                                        (int64_t)c->st.scissor.y + c->st.scissor.height >= c->pass_height);
    if (!c->render && whole && (!(mask & MIME_GPU_CLEAR_COLOR) || (cl->color_mask & 0xF) == 0xF) &&
        (!(mask & MIME_GPU_CLEAR_STENCIL) || (cl->stencil_write_mask & 0xFF) == 0xFF)) {
        for (int i = 0; i < MIME_GPU_MAX_COLORS; i++) {
            if ((mask & MIME_GPU_CLEAR_COLOR) && c->pass.color[i].id) {
                c->pass.color[i].load = MTLLoadActionClear;
                memcpy(c->pass.clear_color[i], cl->color, sizeof(cl->color));
            }
        }
        if (mask & MIME_GPU_CLEAR_DEPTH) {
            c->pass.depth.load = MTLLoadActionClear;
            c->pass.clear_depth = cl->depth;
        }
        if (mask & MIME_GPU_CLEAR_STENCIL) {
            c->pass.stencil.load = MTLLoadActionClear;
            c->pass.clear_stencil = cl->stencil;
        }
        return;
    }
    if (!ensure_encoder(c))
        return;
    if (mask & MIME_GPU_CLEAR_COLOR)
        c->written |= (1u << MIME_GPU_MAX_COLORS) - 1;
    if (mask & MIME_GPU_CLEAR_DEPTH)
        c->written |= 1u << MIME_GPU_MAX_COLORS;
    if (mask & MIME_GPU_CLEAR_STENCIL)
        c->written |= 1u << (MIME_GPU_MAX_COLORS + 1);
    id<MTLRenderPipelineState> ps = clear_pipeline(c, (mask & MIME_GPU_CLEAR_COLOR) ? cl->color_mask : 0);
    if (!ps)
        return;
    MimeGpuDepthStencilCreate ds = {
        .depth_func = MTLCompareFunctionAlways,
        .depth_write = (mask & MIME_GPU_CLEAR_DEPTH) != 0,
        .stencil = (mask & MIME_GPU_CLEAR_STENCIL) != 0,
        .front = {.func = MTLCompareFunctionAlways, .fail = MTLStencilOperationReplace,
                  .depth_fail = MTLStencilOperationReplace, .pass = MTLStencilOperationReplace, .read_mask = 0xFF,
                  .write_mask = (uint8_t)cl->stencil_write_mask},
    };
    ds.back = ds.front;
    id<MTLDepthStencilState> dss = new_depth_state(&ds);
    id<MTL4RenderCommandEncoder> e = c->render;
    [e setRenderPipelineState:ps];
    [e setDepthStencilState:dss];
    [e setStencilFrontReferenceValue:cl->stencil backReferenceValue:cl->stencil];
    [e setCullMode:MTLCullModeNone];
    [e setTriangleFillMode:MTLTriangleFillModeFill];
    MTLViewport vp = {0, 0, c->pass_width, c->pass_height, 0, 1};
    [e setViewport:vp];
    uint32_t dirty = c->st.dirty;
    c->st.dirty = DIRTY_SCISSOR;
    id<MTLRenderPipelineState> saved = c->st.pipeline;
    c->st.pipeline = ps;
    flush_state(c);
    c->st.pipeline = saved;
    float depth = cl->depth;
    [c->tables[MIME_GPU_STAGE_VERTEX] setAddress:upload(c, &depth, sizeof(depth)) atIndex:0];
    [c->tables[MIME_GPU_STAGE_FRAGMENT] setAddress:upload(c, cl->color, sizeof(cl->color)) atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    retire(c, dss, RETIRE_RELEASE);
    c->st.dirty = dirty | DIRTY_PIPELINE | DIRTY_DEPTH | DIRTY_RASTER | DIRTY_VIEWPORT | DIRTY_UNIFORMS;
}

/* ---------------------------------------------------------------- blits */

static id<MTLRenderPipelineState> blit_pipeline(Channel *c, uint32_t format)
{
    for (int i = 0; i < 8; i++)
        if (c->blit_pipelines[i] && c->blit_keys[i] == format)
            return c->blit_pipelines[i];
    MTL4LibraryFunctionDescriptor *vs = internal_function(c, "mime_blit_vs");
    MTL4LibraryFunctionDescriptor *fs = internal_function(c, "mime_blit_fs");
    if (!vs || !fs) {
        [vs release];
        [fs release];
        return nil;
    }
    MTL4RenderPipelineDescriptor *desc = [[MTL4RenderPipelineDescriptor alloc] init];
    desc.vertexFunctionDescriptor = vs;
    desc.fragmentFunctionDescriptor = fs;
    MimeGpuColorTarget t = {.format = (uint16_t)format, .write_mask = 0xF};
    set_color_target(desc.colorAttachments[0], &t);
    id err = nil;
    id<MTLRenderPipelineState> ps = mime_pipeline_cache_render(g_cache, desc, &err);
    if (!ps)
        mime_warn("gpu: blit pipeline: %s", error_text(err));
    [desc release];
    [vs release];
    [fs release];
    int victim = (int)(format % 8);
    retire(c, c->blit_pipelines[victim], RETIRE_RELEASE);
    c->blit_pipelines[victim] = ps;
    c->blit_keys[victim] = format;
    return ps;
}

static void blit(Channel *c, const MimeGpuBlit *b)
{
    end_render(c);
    bool depth = (b->mask & (MIME_GPU_CLEAR_DEPTH | MIME_GPU_CLEAR_STENCIL)) != 0;
    id<MTLTexture> src = texture_of(c, b->src.id, b->src.surface, depth);
    id<MTLTexture> dst = texture_of(c, b->dst.id, b->dst.surface, depth);
    if (!src || !dst)
        return;
    int32_t sw = b->src_x1 - b->src_x0, sh = b->src_y1 - b->src_y0;
    int32_t dw = b->dst_x1 - b->dst_x0, dh = b->dst_y1 - b->dst_y0;
    NSUInteger src_samples = src.sampleCount, dst_samples = dst.sampleCount;
    MTLPixelFormat src_format = src.pixelFormat, dst_format = dst.pixelFormat;
    materialize(c, b->src.id, b->src.surface, depth);
    int32_t lx = b->dst_x0 < b->dst_x1 ? b->dst_x0 : b->dst_x1, ly = b->dst_y0 < b->dst_y1 ? b->dst_y0 : b->dst_y1;
    bool whole = !depth && covers(c, b->dst.id, b->dst.surface, b->dst.level, b->dst.slice, lx, ly, abs(dw), abs(dh));
    if (sw == dw && sh == dh && sw > 0 && sh > 0 && src_samples == dst_samples && src_format == dst_format) {
        write_texture(c, b->dst.id, b->dst.surface, depth, whole);
        id<MTL4ComputeCommandEncoder> e = blit_encoder(c);
        MTLOrigin so = {(NSUInteger)b->src_x0, (NSUInteger)b->src_y0, 0}, dor = {(NSUInteger)b->dst_x0, (NSUInteger)b->dst_y0, 0};
        MTLSize size = {(NSUInteger)sw, (NSUInteger)sh, 1};
        [e copyFromTexture:src sourceSlice:b->src.slice sourceLevel:b->src.level sourceOrigin:so sourceSize:size
                 toTexture:dst destinationSlice:b->dst.slice destinationLevel:b->dst.level destinationOrigin:dor];
        return;
    }
    end_blit(c);
    if (src_samples > 1 && dst_samples == 1 && !depth) {
        /* resolves the whole source into an equally sized destination. */
        MTL4RenderPassDescriptor *rp = [[[MTL4RenderPassDescriptor alloc] init] autorelease];
        MTLRenderPassColorAttachmentDescriptor *att = rp.colorAttachments[0];
        att.texture = src;
        att.loadAction = MTLLoadActionLoad;
        att.storeAction = MTLStoreActionStoreAndMultisampleResolve;
        att.resolveTexture = dst;
        att.resolveLevel = b->dst.level;
        att.resolveSlice = b->dst.slice;
        write_texture(c, b->dst.id, b->dst.surface, false, true);
        id<MTL4RenderCommandEncoder> e = new_encoder(c, rp);
        [e endEncoding];
        [e release];
        return;
    }
    if (depth || src_samples > 1)
        return;
    /* stretches the source rectangle over the destination rectangle. */
    id<MTLRenderPipelineState> ps = blit_pipeline(c, (uint32_t)dst.pixelFormat);
    if (!ps)
        return;
    NSUInteger tw = src.width, th = src.height;
    MTL4RenderPassDescriptor *rp = [[[MTL4RenderPassDescriptor alloc] init] autorelease];
    MTLRenderPassColorAttachmentDescriptor *att = rp.colorAttachments[0];
    att.texture = dst;
    att.level = b->dst.level;
    att.slice = b->dst.slice;
    /* the pass loads the destination it does not cover: a pending clear needs no memory traffic. */
    Track *dt = track_of(c, b->dst.id, b->dst.surface, false);
    uint8_t load = MTLLoadActionLoad;
    if (dt && whole) {
        load = MTLLoadActionDontCare;
    } else if (dt && dt->aspect[0].kind == TRACK_CLEARED && !b->dst.level && !b->dst.slice) {
        const float *v = dt->aspect[0].value;
        load = MTLLoadActionClear;
        att.clearColor = MTLClearColorMake(v[0], v[1], v[2], v[3]);
    }
    set_data(dt);
    att.loadAction = load;
    att.storeAction = MTLStoreActionStore;
    id<MTL4RenderCommandEncoder> e = new_encoder(c, rp);
    int32_t x0 = b->dst_x0 < b->dst_x1 ? b->dst_x0 : b->dst_x1, y0 = b->dst_y0 < b->dst_y1 ? b->dst_y0 : b->dst_y1;
    MTLViewport vp = {x0, y0, abs(dw), abs(dh), 0, 1};
    [e setViewport:vp];
    /* uv of the destination corners, mirrored when the two rectangles disagree. */
    float u0 = (float)b->src_x0 / tw, u1 = (float)b->src_x1 / tw, v0 = (float)b->src_y0 / th, v1 = (float)b->src_y1 / th;
    if (dw < 0) {
        float t = u0;
        u0 = u1;
        u1 = t;
    }
    if (dh < 0) {
        float t = v0;
        v0 = v1;
        v1 = t;
    }
    float uv[4] = {u0, v0, u1, v1};
    [e setRenderPipelineState:ps];
    [c->tables[MIME_GPU_STAGE_VERTEX] setAddress:upload(c, uv, sizeof(uv)) atIndex:0];
    [c->tables[MIME_GPU_STAGE_FRAGMENT] setTexture:src.gpuResourceID atIndex:0];
    [c->tables[MIME_GPU_STAGE_FRAGMENT] setSamplerState:blit_sampler(c, b->linear != 0).gpuResourceID atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e endEncoding];
    [e release];
}

/* ---------------------------------------------------------------- execution */

/* prepares the encoder for a draw and notes the attachments it writes. */
static bool draw_ready(Channel *c)
{
    if (!c->pass_live || !c->st.pipeline)
        return false;
    materialize_bound(c);
    if (!flush_state(c))
        return false;
    c->written |= (1u << MIME_GPU_MAX_COLORS) - 1;
    if (c->depth_flags & 1)
        c->written |= 1u << MIME_GPU_MAX_COLORS;
    if (c->depth_flags & 2)
        c->written |= 1u << (MIME_GPU_MAX_COLORS + 1);
    return true;
}

static void draw(Channel *c, const MimeGpuDraw *d)
{
    if (!d->count || !draw_ready(c))
        return;
    [c->render drawPrimitives:d->primitive vertexStart:d->first vertexCount:d->count
                instanceCount:d->instances ? d->instances : 1 baseInstance:d->base_instance];
}

static void draw_indexed(Channel *c, const MimeGpuDrawIndexed *d)
{
    if (!d->count || !draw_ready(c))
        return;
    uint64_t off;
    uint64_t size = (uint64_t)d->count * (d->index_type ? 4 : 2);
    id<MTLBuffer> buf = heap(c, &d->indices, size, &off);
    if (!buf)
        return;
    [c->render drawIndexedPrimitives:d->primitive indexCount:d->count indexType:d->index_type
                         indexBuffer:buf.gpuAddress + off indexBufferLength:size
                       instanceCount:d->instances ? d->instances : 1 baseVertex:d->base_vertex
                        baseInstance:d->base_instance];
}

#define BODY(type) ((const type *)cmd)
#define NEED(type)                                                                                                     \
    if (cmd->size < sizeof(type)) {                                                                                    \
        lose(c, "short record");                                                                                       \
        return;                                                                                                        \
    }

static void execute(Channel *c, const MimeGpuCmd *cmd)
{
    switch (cmd->op) {
    case MIME_GPU_NOP:
        break;
    case MIME_GPU_FENCE:
    case MIME_GPU_FLUSH: {
        uint64_t value = 0;
        if (cmd->op == MIME_GPU_FENCE) {
            NEED(MimeGpuFence);
            value = BODY(MimeGpuFence)->value;
        }
        /* inside an encoder the commit waits for its end, unless the guest waits for the fence first. */
        if (c->render) {
            c->deferred = true;
            if (value > c->deferred_fence)
                c->deferred_fence = value;
        } else {
            commit(c, value);
        }
        break;
    }
    case MIME_GPU_TEXTURE_CREATE:
        NEED(MimeGpuTextureCreate);
        texture_create(c, BODY(MimeGpuTextureCreate));
        break;
    case MIME_GPU_TEXTURE_DESTROY:
        NEED(MimeGpuObject);
        if (BODY(MimeGpuObject)->id < c->textures_cap) {
            if (in_pass(c, BODY(MimeGpuObject)->id, false))
                end_render(c);
            Texture *t = &c->textures[BODY(MimeGpuObject)->id];
            retire(c, t->tex, RETIRE_RESIDENT);
            retire(c, t->sampled, RETIRE_RELEASE);
            memset(t, 0, sizeof(*t));
        }
        break;
    case MIME_GPU_TEXTURE_SWIZZLE:
        NEED(MimeGpuTextureSwizzle);
        texture_swizzle(c, BODY(MimeGpuTextureSwizzle)->id, BODY(MimeGpuTextureSwizzle)->swizzle);
        break;
    case MIME_GPU_TEXTURE_UPLOAD:
    case MIME_GPU_TEXTURE_READ:
        NEED(MimeGpuTextureCopy);
        texture_copy(c, BODY(MimeGpuTextureCopy), cmd->op == MIME_GPU_TEXTURE_UPLOAD);
        break;
    case MIME_GPU_TEXTURE_MIPMAPS: {
        NEED(MimeGpuObject);
        id<MTLTexture> t = texture_of(c, BODY(MimeGpuObject)->id, false, false);
        if (t && t.mipmapLevelCount > 1)
            [blit_encoder(c) generateMipmapsForTexture:t];
        break;
    }
    case MIME_GPU_BUFFER_COPY: {
        NEED(MimeGpuBufferCopy);
        const MimeGpuBufferCopy *b = BODY(MimeGpuBufferCopy);
        uint64_t so, dof;
        id<MTLBuffer> src = heap(c, &b->src, b->size, &so), dst = heap(c, &b->dst, b->size, &dof);
        if (src && dst && b->size)
            [blit_encoder(c) copyFromBuffer:src sourceOffset:so toBuffer:dst destinationOffset:dof size:b->size];
        break;
    }
    case MIME_GPU_PROGRAM_CREATE:
        NEED(MimeGpuProgramCreate);
        program_create(c, BODY(MimeGpuProgramCreate));
        break;
    case MIME_GPU_PROGRAM_DESTROY:
        NEED(MimeGpuObject);
        program_destroy(c, BODY(MimeGpuObject)->id);
        break;
    case MIME_GPU_PIPELINE_CREATE:
        NEED(MimeGpuPipelineCreate);
        pipeline_create(c, BODY(MimeGpuPipelineCreate));
        break;
    case MIME_GPU_DEPTH_STENCIL_CREATE: {
        NEED(MimeGpuDepthStencilCreate);
        const MimeGpuDepthStencilCreate *d = BODY(MimeGpuDepthStencilCreate);
        id<MTLDepthStencilState> *slotp = SLOT(c, depth_states, d->id);
        retire(c, *slotp, RETIRE_RELEASE);
        *slotp = new_depth_state(d);
        *SLOT(c, depth_state_flags, d->id) = (d->depth_write ? 1 : 0) | (d->stencil ? 2 : 0);
        break;
    }
    case MIME_GPU_SAMPLER_CREATE:
        NEED(MimeGpuSamplerCreate);
        sampler_create(c, BODY(MimeGpuSamplerCreate));
        break;
    case MIME_GPU_SURFACE_CONFIG:
        NEED(MimeGpuSurfaceConfig);
        surface_config(c, BODY(MimeGpuSurfaceConfig));
        break;
    case MIME_GPU_SURFACE_DESTROY:
        NEED(MimeGpuObject);
        surface_destroy(c, BODY(MimeGpuObject)->id);
        break;
    case MIME_GPU_PRESENT:
        NEED(MimeGpuObject);
        present(c, BODY(MimeGpuObject)->id);
        break;
    case MIME_GPU_PASS_BEGIN:
        NEED(MimeGpuPass);
        pass_begin(c, BODY(MimeGpuPass));
        break;
    case MIME_GPU_PASS_END:
        end_render(c);
        c->pass_live = false;
        break;
    case MIME_GPU_CLEAR:
        NEED(MimeGpuClear);
        clear(c, BODY(MimeGpuClear));
        break;
    case MIME_GPU_BLIT:
        NEED(MimeGpuBlit);
        blit(c, BODY(MimeGpuBlit));
        break;
    case MIME_GPU_SET_PIPELINE: {
        NEED(MimeGpuObject);
        uint32_t i = BODY(MimeGpuObject)->id;
        c->st.pipeline = pipeline_state(i < c->pipelines_cap ? c->pipelines[i] : nullptr);
        c->st.dirty |= DIRTY_PIPELINE;
        break;
    }
    case MIME_GPU_SET_DEPTH_STENCIL: {
        NEED(MimeGpuSetDepthStencil);
        const MimeGpuSetDepthStencil *s = BODY(MimeGpuSetDepthStencil);
        c->st.depth_state = s->id < c->depth_states_cap ? c->depth_states[s->id] : nil;
        c->depth_flags = s->id < c->depth_state_flags_cap ? c->depth_state_flags[s->id] : 0;
        c->st.front_ref = s->front_ref;
        c->st.back_ref = s->back_ref;
        c->st.dirty |= DIRTY_DEPTH;
        break;
    }
    case MIME_GPU_SET_RASTER:
        NEED(MimeGpuSetRaster);
        c->st.raster = *BODY(MimeGpuSetRaster);
        c->st.dirty |= DIRTY_RASTER;
        break;
    case MIME_GPU_SET_VIEWPORT: {
        NEED(MimeGpuSetViewport);
        const MimeGpuSetViewport *v = BODY(MimeGpuSetViewport);
        c->st.viewport = (MTLViewport){v->x, v->y, v->width, v->height, v->znear, v->zfar};
        c->st.dirty |= DIRTY_VIEWPORT;
        break;
    }
    case MIME_GPU_SET_SCISSOR:
        NEED(MimeGpuRect);
        c->st.scissor = *BODY(MimeGpuRect);
        c->st.has_scissor = true;
        c->st.dirty |= DIRTY_SCISSOR;
        break;
    case MIME_GPU_SET_BLEND_COLOR:
        NEED(MimeGpuSetBlendColor);
        memcpy(c->st.blend, BODY(MimeGpuSetBlendColor)->rgba, sizeof(c->st.blend));
        c->st.dirty |= DIRTY_BLEND;
        break;
    case MIME_GPU_SET_VERTEX_BUFFER: {
        NEED(MimeGpuSetVertexBuffer);
        const MimeGpuSetVertexBuffer *v = BODY(MimeGpuSetVertexBuffer);
        if (v->slot < MIME_GPU_MAX_ATTRIBS) {
            c->st.vb[v->slot] = v->mem;
            c->st.vb_mask |= 1u << v->slot;
            c->st.dirty |= DIRTY_VB;
        }
        break;
    }
    case MIME_GPU_SET_UNIFORMS: {
        NEED(MimeGpuSetUniforms);
        const MimeGpuSetUniforms *u = BODY(MimeGpuSetUniforms);
        uint32_t n = u->bytes;
        if (n > sizeof(c->st.uniforms) || n > cmd->size - sizeof(*u)) {
            lose(c, "uniform record");
            return;
        }
        memcpy(c->st.uniforms, u + 1, n);
        c->st.uniform_bytes = n;
        c->st.dirty |= DIRTY_UNIFORMS;
        break;
    }
    case MIME_GPU_SET_UNIFORM_BUFFER: {
        NEED(MimeGpuSetUniformBuffer);
        const MimeGpuSetUniformBuffer *binding = BODY(MimeGpuSetUniformBuffer);
        if (binding->slot >= MIME_GPU_MAX_UNIFORM_BLOCKS) {
            lose(c, "uniform buffer slot");
            return;
        }
        for (int stage = 0; stage < MIME_GPU_STAGES; stage++) {
            if (!(binding->stages & (1u << stage))) continue;
            c->st.uniform_buffers[stage][binding->slot] = binding->mem;
            c->st.uniform_buffer_mask[stage] |= 1u << binding->slot;
        }
        c->st.dirty |= DIRTY_UNIFORMS;
        break;
    }
    case MIME_GPU_SET_TEXTURE: {
        NEED(MimeGpuSetTexture);
        const MimeGpuSetTexture *t = BODY(MimeGpuSetTexture);
        if (t->stage < MIME_GPU_STAGES && t->index < MIME_GPU_MAX_TEXTURES) {
            c->st.tex[t->stage][t->index] = (Binding){t->id, t->sampler, t->surface != 0};
            if (t->id)
                c->st.tex_mask[t->stage] |= 1u << t->index;
            else
                c->st.tex_mask[t->stage] &= ~(1u << t->index);
            c->st.dirty |= DIRTY_TEXTURES;
        }
        break;
    }
    case MIME_GPU_DRAW:
        NEED(MimeGpuDraw);
        draw(c, BODY(MimeGpuDraw));
        break;
    case MIME_GPU_DRAW_INDEXED:
        NEED(MimeGpuDrawIndexed);
        draw_indexed(c, BODY(MimeGpuDrawIndexed));
        break;
    default:
        lose(c, "unknown command");
        break;
    }
}

static bool held(Channel *c, uint64_t fence)
{
    return fence > c->committed_fence && fence <= c->deferred_fence;
}

/* checks for a guest waiting on a deferred fence, excluding fences the ring has not reached. */
static bool fence_wanted(Channel *c)
{
    return held(c, __atomic_load_n(&c->hdr->want_fence, __ATOMIC_ACQUIRE)) ||
           held(c, __atomic_load_n(&c->hdr->wait_fence, __ATOMIC_ACQUIRE));
}

/* spins a little before sleeping: a guest in the middle of a frame publishes again within microseconds. */
static bool wait_for_work(Channel *c, uint64_t get)
{
    if (c->deferred) {
        /* a deferred commit waits a millisecond for the pass to go on, less when the guest waits for it. */
        double end = now_s() + 0.001;
        while (__atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE) == get && !c->stop && !fence_wanted(c) && now_s() < end)
            __asm__ volatile("yield");
        if (__atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE) != get || c->stop)
            return !c->stop;
        @autoreleasepool {
            commit(c, 0);
        }
    }
    for (int i = 0; i < 2000; i++) {
        if (__atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE) != get || c->stop)
            return !c->stop;
        __asm__ volatile("yield");
    }
    pthread_mutex_lock(&c->lock);
    c->hdr->host_idle = 1;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    while (!c->stop && !c->kicked && __atomic_load_n(&c->hdr->put, __ATOMIC_SEQ_CST) == get) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100 * 1000 * 1000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&c->cond, &c->lock, &ts);
    }
    c->kicked = false;
    c->hdr->host_idle = 0;
    bool run = !c->stop;
    pthread_mutex_unlock(&c->lock);
    return run;
}

static void *channel_main(void *arg)
{
    Channel *c = arg;
    pthread_setname_np("mime-gpu");
    uint64_t get = c->hdr->get;
    for (;;) {
        @autoreleasepool {
            for (int n = 0; n < 4096; n++) {
                uint64_t put = __atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE);
                if (put == get || c->hdr->lost)
                    break;
                uint64_t pos = get % MIME_GPU_RING_SIZE;
                const MimeGpuCmd *cmd = (const MimeGpuCmd *)(c->ring + pos);
                if (put - get > MIME_GPU_RING_SIZE || cmd->size < sizeof(MimeGpuCmd) || (cmd->size & 7) ||
                    cmd->size > put - get || pos + cmd->size > MIME_GPU_RING_SIZE) {
                    lose(c, "bad ring record");
                    break;
                }
                execute(c, cmd);
                if (c->deferred && (!c->render || fence_wanted(c)))
                    commit(c, 0);
                get += cmd->size;
                __atomic_store_n(&c->hdr->get, get, __ATOMIC_RELEASE);
            }
            sweep(c);
        }
        if (c->hdr->lost) {
            /* nothing more runs: the guest sees lost and stops waiting. */
            __atomic_store_n(&c->hdr->get, __atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
            signal_fence(c, UINT64_MAX >> 1);
        }
        if (__atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE) == get && !wait_for_work(c, get))
            break;
    }
    return nullptr;
}

/* ---------------------------------------------------------------- hypercalls */

static bool add_block(Channel *c, uint32_t index, uint64_t pa, uint64_t size)
{
    uint8_t *base = mime_vm_phys(c->vm, pa, size);
    if (!base || index >= MIME_GPU_MAX_BLOCKS || c->blocks[index] || ((uintptr_t)base & (MIME_GPU_ALIGN - 1)) ||
        (size & (MIME_GPU_ALIGN - 1)))
        return false;
    id<MTLBuffer> buf = [g_device newBufferWithBytesNoCopy:base length:size options:MTLResourceStorageModeShared
                                               deallocator:nil];
    if (!buf)
        return false;
    c->block_base[index] = base;
    c->block_size[index] = size;
    __atomic_store_n((void **)&c->blocks[index], (void *)buf, __ATOMIC_RELEASE);
    return true;
}

static void init_device(void)
{
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (![device supportsFamily:MTLGPUFamilyMetal4]) {
        mime_warn("gpu: %s has no Metal 4", device ? device.name.UTF8String : "this Mac");
        [device release];
        return;
    }
    g_cache = mime_pipeline_cache(device, "opengl");
    MTL4CompilerDescriptor *d = [[MTL4CompilerDescriptor alloc] init];
    g_metalfx_compiler = [device newCompilerWithDescriptor:d error:nil];
    [d release];
    g_metalfx = [MTLFXSpatialScalerDescriptor supportsMetal4FX:device];
    g_compile_queue = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
    if (g_cache && g_metalfx_compiler)
        g_device = device;
    else
        [device release];
}

uint64_t mime_gpu_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *channel)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, init_device);
    if (!g_device || size != MIME_GPU_CHANNEL_SIZE)
        return MIME_HC_STATUS_UNSUPPORTED;
    uint8_t *base = mime_vm_phys(vm, pa, size);
    if (!base)
        return MIME_HC_STATUS_INVALID;
    Channel *c = calloc(1, sizeof(Channel));
    c->vm = vm;
    c->hdr = (MimeGpuHeader *)base;
    c->ring = base + MIME_GPU_HEADER_SIZE;
    pthread_mutex_init(&c->lock, nullptr);
    pthread_cond_init(&c->cond, nullptr);
    bool ok;
    @autoreleasepool {
        ok = add_block(c, 0, pa, size);
        c->queue = [g_device newMTL4CommandQueue];
        c->cb = [g_device newCommandBuffer];
        MTLResidencySetDescriptor *rd = [[MTLResidencySetDescriptor alloc] init];
        rd.initialCapacity = 64;
        c->residency = [g_device newResidencySetWithDescriptor:rd error:nil];
        [rd release];
        MTL4ArgumentTableDescriptor *td = [[MTL4ArgumentTableDescriptor alloc] init];
        td.maxBufferBindCount = MIME_GPU_VERTEX_BUFFER_BASE + MIME_GPU_MAX_ATTRIBS;
        td.maxTextureBindCount = MIME_GPU_MAX_TEXTURES;
        td.maxSamplerStateBindCount = MAX_SAMPLERS;
        td.initializeBindings = YES;
        for (int i = 0; i < MIME_GPU_STAGES; i++)
            c->tables[i] = [g_device newArgumentTableWithDescriptor:td error:nil];
        [td release];
        ok = ok && c->queue && c->cb && c->residency && c->tables[0] && c->tables[1];
        if (ok)
            [c->queue addResidencySet:c->residency];
    }
    os_unfair_lock_lock(&g_lock);
    for (uint32_t i = 1; ok && i < MAX_CHANNELS; i++) {
        if (!g_channels[i]) {
            c->id = i;
            g_channels[i] = c;
            break;
        }
    }
    os_unfair_lock_unlock(&g_lock);
    if (!ok || !c->id) {
        [c->queue release];
        [c->cb release];
        [c->residency release];
        [c->tables[0] release];
        [c->tables[1] release];
        [c->blocks[0] release];
        free(c);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    pthread_create(&c->thread, nullptr, channel_main, c);
    *channel = c->id;
    return MIME_HC_STATUS_OK;
}

uint64_t mime_gpu_add_block(MimeVm *vm, uint64_t channel, uint64_t index, uint64_t pa, uint64_t size)
{
    Channel *c = find_channel(channel);
    if (!c || c->vm != vm)
        return MIME_HC_STATUS_NOT_FOUND;
    bool ok;
    @autoreleasepool {
        ok = add_block(c, (uint32_t)index, pa, size);
    }
    return ok ? MIME_HC_STATUS_OK : MIME_HC_STATUS_INVALID;
}

void mime_gpu_kick(uint64_t channel)
{
    Channel *c = find_channel(channel);
    if (!c)
        return;
    pthread_mutex_lock(&c->lock);
    c->kicked = true;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->lock);
}

void mime_gpu_close(uint64_t channel)
{
    os_unfair_lock_lock(&g_lock);
    Channel *c = channel < MAX_CHANNELS ? g_channels[channel] : nullptr;
    if (c)
        g_channels[channel] = nullptr;
    os_unfair_lock_unlock(&g_lock);
    if (!c)
        return;
    pthread_mutex_lock(&c->lock);
    c->stop = true;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->lock);
    pthread_join(c->thread, nullptr);

    @autoreleasepool {
        commit(c, 0);
        while (completed(c) < c->submitted)
            usleep(1000);
        for (uint32_t i = 0; i < c->textures_cap; i++) {
            retire(c, c->textures[i].tex, RETIRE_RESIDENT);
            retire(c, c->textures[i].sampled, RETIRE_RELEASE);
        }
        for (uint32_t i = 0; i < c->programs_cap; i++)
            if (c->programs[i])
                program_destroy(c, i);
        for (uint32_t i = 0; i < c->pipelines_cap; i++)
            pipeline_free(c, c->pipelines[i]);
        for (uint32_t i = 0; i < c->depth_states_cap; i++)
            retire(c, c->depth_states[i], RETIRE_RELEASE);
        for (uint32_t i = 0; i < c->samplers_cap; i++)
            retire(c, c->samplers[i], RETIRE_RELEASE);
        for (uint32_t i = 0; i < c->surfaces_cap; i++)
            surface_destroy(c, i);
        for (int i = 0; i < 16; i++)
            retire(c, c->clear_pipelines[i], RETIRE_RELEASE);
        for (int i = 0; i < 8; i++)
            retire(c, c->blit_pipelines[i], RETIRE_RELEASE);
        sweep(c);
        [c->internal_library release];
        for (int i = 0; i < 2; i++)
            [c->blit_samplers[i] release];
        for (uint32_t i = 0; i < c->upload_count; i++)
            [c->uploads[i].buffer release];
        for (uint32_t i = 0; i < c->allocator_count; i++)
            [c->allocators[i].allocator release];
        for (int i = 0; i < MIME_GPU_STAGES; i++)
            [c->tables[i] release];
        for (int i = 0; i < MIME_GPU_MAX_BLOCKS; i++)
            [c->blocks[i] release];
        [c->queue removeResidencySet:c->residency];
        [c->residency release];
        [c->cb release];
        [c->queue release];
    }
    free(c->textures);
    free(c->programs);
    free(c->pipelines);
    free(c->depth_states);
    free(c->depth_state_flags);
    free(c->samplers);
    free(c->surfaces);
    free(c->uploads);
    free(c->retired);
    pthread_mutex_destroy(&c->lock);
    pthread_cond_destroy(&c->cond);
    free(c);
}
