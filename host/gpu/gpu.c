/*
 * gpu channels on metal.
 *
 * each guest process that renders gets a channel (mime/gpu.h): a thread
 * here executes the commands it writes in its ring, in order, and encodes
 * them for metal. the commands are already metal level, so this is mostly
 * a thin replay: the guest keeps the graphics api state, decides render
 * passes and only sends what changes. memory the commands name is guest
 * memory that metal reads and writes in place through buffers made over the
 * guest ram mapping.
 *
 * guest programs are compiled on a concurrent queue (shader.c) while the
 * ring goes on; a pipeline waits for its program. surfaces keep a back
 * buffer the guest renders to, copied to a CAMetalLayer on the window at
 * each present.
 */
#include "gpu.h"

#include "../ui/ui.h"
#include "shader.h"

#include <dispatch/dispatch.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef unsigned long NSUInteger;
typedef long NSInteger;

typedef struct MTLViewport {
    double x, y, width, height, znear, zfar;
} MTLViewport;
typedef struct MTLScissorRect {
    NSUInteger x, y, width, height;
} MTLScissorRect;
typedef struct MTLClearColor {
    double r, g, b, a;
} MTLClearColor;
typedef struct MTLOrigin {
    NSUInteger x, y, z;
} MTLOrigin;
typedef struct MTLSize {
    NSUInteger width, height, depth;
} MTLSize;
typedef struct MTLSwizzle {
    uint8_t r, g, b, a;
} MTLSwizzle;
typedef struct MTLRange {
    NSUInteger location, length;
} MTLRange;

extern id MTLCreateSystemDefaultDevice(void);
extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *pool);

#define S(name)                                                                                                        \
    ({                                                                                                                 \
        static SEL s_;                                                                                                 \
        if (!s_)                                                                                                       \
            s_ = sel_registerName(name);                                                                               \
        s_;                                                                                                            \
    })
#define CLS(name) ((id)objc_getClass(name))
#define MSG(type, obj, name, ...) ((type)objc_msgSend)((id)(obj), S(name), ##__VA_ARGS__)

typedef id (*F_id)(id, SEL);
typedef id (*F_id_id)(id, SEL, id);
typedef id (*F_id_u)(id, SEL, NSUInteger);
typedef id (*F_id_id_err)(id, SEL, id, id *);
typedef id (*F_id_id_id_err)(id, SEL, id, id, id *);
typedef id (*F_id_p_u_u)(id, SEL, const void *, NSUInteger, NSUInteger);
typedef id (*F_id_p_u_u_blk)(id, SEL, void *, NSUInteger, NSUInteger, void *);
typedef id (*F_id_str)(id, SEL, const char *);
typedef const char *(*F_str)(id, SEL);
typedef void (*F_v)(id, SEL);
typedef void (*F_v_id)(id, SEL, id);
typedef void (*F_v_u)(id, SEL, NSUInteger);
typedef void (*F_v_b)(id, SEL, bool);
typedef void (*F_v_d)(id, SEL, double);
typedef void (*F_v_f)(id, SEL, float);
typedef void (*F_v_u32)(id, SEL, uint32_t);
typedef void (*F_v_id_u)(id, SEL, id, NSUInteger);
typedef void (*F_v_id_u_u)(id, SEL, id, NSUInteger, NSUInteger);
typedef void (*F_v_u_u)(id, SEL, NSUInteger, NSUInteger);
typedef void (*F_v_p_u_u)(id, SEL, const void *, NSUInteger, NSUInteger);
typedef void (*F_v_u32_u32)(id, SEL, uint32_t, uint32_t);
typedef void (*F_v_f4)(id, SEL, float, float, float, float);
typedef void (*F_v_f3)(id, SEL, float, float, float);

enum {
    USAGE_SHADER_READ = 1,
    USAGE_RENDER_TARGET = 4,
    USAGE_PIXEL_FORMAT_VIEW = 16,
    STORAGE_PRIVATE = 2,
    PIXEL_BGRA8 = 80,
    PIXEL_D32S8 = 260,
    PIXEL_D32 = 252,
    PIXEL_S8 = 253,
    TEXTURE_2D = 2,
    TEXTURE_2D_ARRAY = 3,
    TEXTURE_2D_MS = 4,
    TEXTURE_CUBE = 5,
    TEXTURE_CUBE_ARRAY = 6,
    TEXTURE_3D = 7,
    LOAD_DONT_CARE = 0,
    LOAD_LOAD = 1,
    LOAD_CLEAR = 2,
    STORE_DONT_CARE = 0,
    STORE_STORE = 1,
    STORE_UNKNOWN = 4,
};

/* ---------------------------------------------------------------- objects */

/*
 * what an attachment holds, followed in encoding order so passes skip the
 * memory traffic gl does not need: undefined contents are never loaded, a
 * clear only the gpu would read back becomes a clear load action, and an
 * attachment a pass leaves untouched is not stored again.
 */
enum { TRACK_DATA, TRACK_UNDEFINED, TRACK_CLEARED };

typedef struct Aspect {
    uint8_t kind;
    bool materialized; /* a cleared aspect whose memory holds the clear value */
    float value[4];    /* the clear color, or the depth in value[0] */
    uint32_t stencil;
} Aspect;

/* the color or depth aspect, then the stencil aspect. */
typedef struct Track {
    bool tracked;
    Aspect aspect[2];
} Track;

typedef struct Texture {
    id tex;
    id sampled;
    uint32_t format, width, height, samples;
    Track track;
} Texture;

typedef struct Program {
    dispatch_semaphore_t ready;
    id fn[MIME_GPU_STAGES];
    bool compiled;
} Program;

typedef struct Surface {
    id color, depth; /* depth holds the stencil too when both exist */
    Track color_track, depth_track;
    void *layer;
    uint32_t width, height, samples;
    uint32_t window_id;
    uint16_t depth_format, stencil_format;
    bool sync;
} Surface;

typedef struct Binding {
    uint32_t id;
    uint32_t sampler;
    bool surface;
} Binding;

typedef struct Channel {
    uint32_t id;
    MimeVm *vm;
    MimeGpuHeader *hdr;
    uint8_t *ring;
    id blocks[MIME_GPU_MAX_BLOCKS];
    uint8_t *block_base[MIME_GPU_MAX_BLOCKS];
    uint64_t block_size[MIME_GPU_MAX_BLOCKS];

    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool kicked, stop;
    volatile long outstanding; /* command buffers whose completion handler has not run */

    id queue;
    id cb, render, blit;

    Texture *textures;
    uint32_t textures_cap;
    Program **programs;
    uint32_t programs_cap;
    id *pipelines;
    uint32_t pipelines_cap;
    id *depth_states;
    uint32_t depth_states_cap;
    id *samplers;
    uint32_t samplers_cap;
    Surface *surfaces;
    uint32_t surfaces_cap;

    /* the render pass in progress. */
    uint32_t pass_width, pass_height;
    uint16_t pass_color[MIME_GPU_MAX_COLORS], pass_depth, pass_stencil;
    uint32_t pass_samples;
    /*
     * the guest's open pass. its encoder starts at the first draw, so clears
     * before it become load actions; an encoder ended by a copy or a commit
     * before the guest ends the pass continues in a new one that keeps the
     * contents. enc_load is the load action the encoder used for each
     * attachment, colors then depth and stencil, and written the attachments
     * its draws and clears wrote.
     */
    MimeGpuPass pass;
    bool pass_live, pass_started;
    uint8_t enc_load[MIME_GPU_MAX_COLORS + 2];
    bool enc_unknown[MIME_GPU_MAX_COLORS + 2];
    uint32_t written;
    uint8_t depth_flags; /* of the bound depth stencil state: 1 writes depth, 2 writes stencil */
    uint8_t *depth_state_flags;
    uint32_t depth_state_flags_cap;
    /* a fence or flush met while an encoder was open, committed once it ends or the guest waits for it. */
    uint64_t deferred_fence, committed_fence;
    bool deferred;

    /* state the guest set, applied to an encoder before a draw. */
    struct {
        id pipeline, depth_state;
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

    id clear_pipelines[16];
    uint64_t clear_keys[16];
    id blit_pipelines[8];
    uint64_t blit_keys[8];
    id internal_library;
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

#define MAX_CHANNELS 256

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Channel *g_channels[MAX_CHANNELS];
static id g_device;
static dispatch_queue_t g_compile_queue;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static Channel *find_channel(uint64_t id_)
{
    pthread_mutex_lock(&g_lock);
    Channel *c = id_ < MAX_CHANNELS ? g_channels[id_] : NULL;
    pthread_mutex_unlock(&g_lock);
    return c;
}

/* grows a table indexed by guest object id. */
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

#define SLOT(c, table, id_) ((__typeof__((c)->table))slot((void **)&(c)->table, &(c)->table##_cap, id_, sizeof(*(c)->table)))

static void release(id obj)
{
    if (obj)
        MSG(F_v, obj, "release");
}

static id nsstring(const char *s)
{
    return MSG(F_id_str, CLS("NSString"), "stringWithUTF8String:", s);
}

static const char *error_text(id err)
{
    if (!err)
        return "unknown error";
    return MSG(F_str, MSG(F_id, err, "localizedDescription"), "UTF8String");
}

static void lose(Channel *c, const char *why)
{
    mime_warn("gpu: channel %u stops: %s", c->id, why);
    c->hdr->lost = 1;
}

/* the metal buffer and offset of guest heap memory, NULL when out of range. */
static id heap(Channel *c, const MimeGpuMem *m, uint64_t size, uint64_t *offset)
{
    if (m->block >= MIME_GPU_MAX_BLOCKS || !c->blocks[m->block] || m->offset > c->block_size[m->block] ||
        size > c->block_size[m->block] - m->offset)
        return nil;
    *offset = m->offset;
    return c->blocks[m->block];
}

static void *heap_ptr(Channel *c, const MimeGpuMem *m, uint64_t size)
{
    uint64_t off;
    if (!heap(c, m, size, &off))
        return NULL;
    return c->block_base[m->block] + off;
}

/* ---------------------------------------------------------------- encoders */

static id command_buffer(Channel *c)
{
    if (!c->cb) {
        c->cb = MSG(F_id, c->queue, "commandBuffer");
        MSG(F_v, c->cb, "retain");
    }
    return c->cb;
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
        MSG(F_v, c->blit, "endEncoding");
        release(c->blit);
        c->blit = nil;
    }
}

static id blit_encoder(Channel *c)
{
    end_render(c);
    if (!c->blit) {
        c->blit = MSG(F_id, command_buffer(c), "blitCommandEncoder");
        MSG(F_v, c->blit, "retain");
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

/* commits the work so far; fence, when not 0, is signaled once the gpu finished it. */
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
    if (!c->cb && !fence)
        return;
    id cb = command_buffer(c);
    __atomic_fetch_add(&c->outstanding, 1, __ATOMIC_ACQ_REL);
    void (^done)(id) = ^(id buffer) {
        if (fence)
            signal_fence(c, fence);
        __atomic_fetch_sub(&c->outstanding, 1, __ATOMIC_ACQ_REL);
    };
    MSG(F_v_id, cb, "addCompletedHandler:", (id)done);
    MSG(F_v, cb, "commit");
    release(cb);
    c->cb = nil;
}

/* ---------------------------------------------------------------- textures */

static id texture_of(Channel *c, uint32_t id_, bool surface, bool depth)
{
    if (surface) {
        Surface *s = id_ < c->surfaces_cap ? &c->surfaces[id_] : NULL;
        return s ? (depth ? s->depth : s->color) : nil;
    }
    return id_ < c->textures_cap ? c->textures[id_].tex : nil;
}

static id new_texture(uint32_t type, uint32_t format, uint32_t w, uint32_t h, uint32_t depth, uint32_t levels,
                      uint32_t samples, NSUInteger usage, uint32_t swizzle)
{
    id d = MSG(F_id, MSG(F_id, CLS("MTLTextureDescriptor"), "alloc"), "init");
    MSG(F_v_u, d, "setTextureType:", type);
    MSG(F_v_u, d, "setPixelFormat:", format);
    MSG(F_v_u, d, "setWidth:", w ? w : 1);
    MSG(F_v_u, d, "setHeight:", h ? h : 1);
    if (type == TEXTURE_3D)
        MSG(F_v_u, d, "setDepth:", depth ? depth : 1);
    else if (type == TEXTURE_2D_ARRAY || type == 1)
        MSG(F_v_u, d, "setArrayLength:", depth ? depth : 1);
    else if (type == TEXTURE_CUBE_ARRAY)
        MSG(F_v_u, d, "setArrayLength:", depth / 6 ? depth / 6 : 1);
    MSG(F_v_u, d, "setMipmapLevelCount:", levels ? levels : 1);
    MSG(F_v_u, d, "setSampleCount:", samples ? samples : 1);
    MSG(F_v_u, d, "setUsage:", usage);
    MSG(F_v_u, d, "setStorageMode:", STORAGE_PRIVATE);
    if (swizzle) {
        MTLSwizzle sw = {(uint8_t)swizzle, (uint8_t)(swizzle >> 8), (uint8_t)(swizzle >> 16), (uint8_t)(swizzle >> 24)};
        ((void (*)(id, SEL, MTLSwizzle))objc_msgSend)(d, S("setSwizzle:"), sw);
    }
    id t = MSG(F_id_id, g_device, "newTextureWithDescriptor:", d);
    release(d);
    return t;
}

static void texture_swizzle(Channel *c, uint32_t id_, uint32_t swizzle)
{
    if (id_ >= c->textures_cap)
        return;
    Texture *t = &c->textures[id_];
    release(t->sampled);
    t->sampled = nil;
    if (t->tex && swizzle != 0x05040302) {
        NSUInteger type = MSG(NSUInteger (*)(id, SEL), t->tex, "textureType");
        MTLRange levels = {0, MSG(NSUInteger (*)(id, SEL), t->tex, "mipmapLevelCount")};
        MTLRange slices = {0, MSG(NSUInteger (*)(id, SEL), t->tex, "arrayLength")};
        if (type == TEXTURE_CUBE || type == TEXTURE_CUBE_ARRAY)
            slices.length *= 6;
        MTLSwizzle sw = {(uint8_t)swizzle, (uint8_t)(swizzle >> 8), (uint8_t)(swizzle >> 16), (uint8_t)(swizzle >> 24)};
        t->sampled = ((id (*)(id, SEL, NSUInteger, NSUInteger, MTLRange, MTLRange, MTLSwizzle))objc_msgSend)(
            t->tex, S("newTextureViewWithPixelFormat:textureType:levels:slices:swizzle:"), t->format, type, levels, slices, sw);
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
    release(x->tex);
    track_reset(&x->track, (t->usage & MIME_GPU_USAGE_TARGET) && (t->type == TEXTURE_2D || t->type == TEXTURE_2D_MS) &&
                               t->levels <= 1);
    NSUInteger usage = USAGE_SHADER_READ | USAGE_PIXEL_FORMAT_VIEW;
    if (t->usage & MIME_GPU_USAGE_TARGET)
        usage |= USAGE_RENDER_TARGET;
    x->tex = new_texture(t->type, t->format, t->width, t->height, t->depth, t->levels, t->samples, usage, 0);
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
    id tex = texture_of(c, t->id, t->surface, false);
    uint32_t image = t->image_bytes ? t->image_bytes : t->row_bytes * t->height;
    uint64_t bytes = (uint64_t)image * (t->depth ? t->depth : 1);
    uint64_t off;
    id buf = heap(c, &t->mem, bytes, &off);
    if (!tex || !buf || !t->width || !t->height)
        return;
    MTLSize size = {t->width, t->height, t->depth ? t->depth : 1};
    MTLOrigin origin = {t->x, t->y, t->z};
    end_render(c);
    if (upload)
        write_texture(c, t->id, t->surface, false,
                      t->depth <= 1 && covers(c, t->id, t->surface, t->level, t->slice, t->x, t->y, t->width, t->height));
    else
        materialize(c, t->id, t->surface, false);
    id b = blit_encoder(c);
    if (upload)
        ((void (*)(id, SEL, id, NSUInteger, NSUInteger, NSUInteger, MTLSize, id, NSUInteger, NSUInteger, MTLOrigin))
             objc_msgSend)(b, S("copyFromBuffer:sourceOffset:sourceBytesPerRow:sourceBytesPerImage:sourceSize:toTexture:"
                                "destinationSlice:destinationLevel:destinationOrigin:"),
                           buf, off, t->row_bytes, image, size, tex, t->slice, t->level, origin);
    else
        ((void (*)(id, SEL, id, NSUInteger, NSUInteger, MTLOrigin, MTLSize, id, NSUInteger, NSUInteger, NSUInteger))
             objc_msgSend)(b, S("copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toBuffer:"
                                "destinationOffset:destinationBytesPerRow:destinationBytesPerImage:"),
                           tex, t->slice, t->level, origin, size, buf, off, t->row_bytes, image);
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

static id make_function(const char *source, const char *entry, char **error)
{
    id err = nil;
    id lib = MSG(F_id_id_id_err, g_device, "newLibraryWithSource:options:error:", nsstring(source), nil, &err);
    if (!lib) {
        *error = strdup(error_text(err));
        return nil;
    }
    id fn = MSG(F_id_id, lib, "newFunctionWithName:", nsstring(entry));
    release(lib);
    if (!fn)
        *error = strdup("translated entry point was not found");
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
    void *pool = objc_autoreleasePoolPush();
    MimeShaderResult r;
    mime_shader_compile(j->vs, j->vs_len, j->fs, j->fs_len, j->bindings, j->bindings_len, &r);
    bool ok = r.ok;
    for (int s = 0; ok && s < MIME_GPU_STAGES; s++) {
        char *error = NULL;
        j->p->fn[s] = make_function(r.msl[s], r.entry[s], &error);
        if (!j->p->fn[s]) {
            mime_warn("gpu: metal rejected a translated program: %s", error ? error : "");
            append_program_error(&r, error ? error : "metal rejected a translated program");
            free(error);
            ok = false;
        }
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
    objc_autoreleasePoolPop(pool);
    free(j->vs);
    free(j->fs);
    free(j->bindings);
    free(j);
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
        release((*slotp)->fn[0]);
        release((*slotp)->fn[1]);
        free(*slotp);
    }
    Program *p = calloc(1, sizeof(Program));
    p->ready = dispatch_semaphore_create(0);
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

static Program *program_ready(Channel *c, uint32_t id_)
{
    if (id_ >= c->programs_cap || !c->programs[id_])
        return NULL;
    Program *p = c->programs[id_];
    /* a program stays signaled once compiled. */
    dispatch_semaphore_wait(p->ready, DISPATCH_TIME_FOREVER);
    dispatch_semaphore_signal(p->ready);
    return p->compiled ? p : NULL;
}

static void program_destroy(Channel *c, uint32_t id_)
{
    if (id_ >= c->programs_cap || !c->programs[id_])
        return;
    Program *p = c->programs[id_];
    dispatch_semaphore_wait(p->ready, DISPATCH_TIME_FOREVER);
    release(p->fn[0]);
    release(p->fn[1]);
    free(p);
    c->programs[id_] = NULL;
}

/* ---------------------------------------------------------------- states */

static void set_color_target(id att, const MimeGpuColorTarget *t)
{
    MSG(F_v_u, att, "setPixelFormat:", t->format);
    MSG(F_v_u, att, "setWriteMask:", t->write_mask);
    MSG(F_v_b, att, "setBlendingEnabled:", t->blend != 0);
    if (t->blend) {
        MSG(F_v_u, att, "setRgbBlendOperation:", t->rgb_op);
        MSG(F_v_u, att, "setAlphaBlendOperation:", t->alpha_op);
        MSG(F_v_u, att, "setSourceRGBBlendFactor:", t->src_rgb);
        MSG(F_v_u, att, "setDestinationRGBBlendFactor:", t->dst_rgb);
        MSG(F_v_u, att, "setSourceAlphaBlendFactor:", t->src_alpha);
        MSG(F_v_u, att, "setDestinationAlphaBlendFactor:", t->dst_alpha);
    }
}

static void pipeline_create(Channel *c, const MimeGpuPipelineCreate *pc)
{
    id *slotp = SLOT(c, pipelines, pc->id);
    release(*slotp);
    *slotp = nil;
    const MimeGpuPipelineDesc *d = &pc->desc;
    Program *p = program_ready(c, d->program);
    if (!p)
        return;
    id desc = MSG(F_id, MSG(F_id, CLS("MTLRenderPipelineDescriptor"), "alloc"), "init");
    MSG(F_v_id, desc, "setVertexFunction:", p->fn[0]);
    MSG(F_v_id, desc, "setFragmentFunction:", p->fn[1]);
    id vd = MSG(F_id, CLS("MTLVertexDescriptor"), "vertexDescriptor");
    id attrs = MSG(F_id, vd, "attributes"), layouts = MSG(F_id, vd, "layouts");
    for (int i = 0; i < MIME_GPU_MAX_ATTRIBS; i++) {
        const MimeGpuVertexAttrib *a = &d->attribs[i];
        if (!a->format)
            continue;
        id att = MSG(F_id_u, attrs, "objectAtIndexedSubscript:", (NSUInteger)i);
        MSG(F_v_u, att, "setFormat:", a->format);
        MSG(F_v_u, att, "setOffset:", a->offset);
        MSG(F_v_u, att, "setBufferIndex:", (NSUInteger)(MIME_GPU_VERTEX_BUFFER_BASE + a->slot));
    }
    for (int i = 0; i < MIME_GPU_MAX_ATTRIBS; i++) {
        const MimeGpuVertexLayout *l = &d->layouts[i];
        bool used = false;
        for (int a = 0; a < MIME_GPU_MAX_ATTRIBS; a++)
            used |= d->attribs[a].format && d->attribs[a].slot == i;
        if (!used)
            continue;
        id lay = MSG(F_id_u, layouts, "objectAtIndexedSubscript:", (NSUInteger)(MIME_GPU_VERTEX_BUFFER_BASE + i));
        if (!l->stride) {
            /* a constant still needs a stride, never used. */
            MSG(F_v_u, lay, "setStepFunction:", 0);
            MSG(F_v_u, lay, "setStepRate:", 0);
            MSG(F_v_u, lay, "setStride:", 16);
        } else {
            MSG(F_v_u, lay, "setStride:", l->stride);
            MSG(F_v_u, lay, "setStepFunction:", l->instance_divisor ? 2 : 1);
            MSG(F_v_u, lay, "setStepRate:", l->instance_divisor ? l->instance_divisor : 1);
        }
    }
    MSG(F_v_id, desc, "setVertexDescriptor:", vd);
    id colors = MSG(F_id, desc, "colorAttachments");
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
        if (d->color[i].format)
            set_color_target(MSG(F_id_u, colors, "objectAtIndexedSubscript:", (NSUInteger)i), &d->color[i]);
    MSG(F_v_u, desc, "setDepthAttachmentPixelFormat:", d->depth_format);
    MSG(F_v_u, desc, "setStencilAttachmentPixelFormat:", d->stencil_format);
    MSG(F_v_u, desc, "setRasterSampleCount:", d->samples ? d->samples : 1);
    MSG(F_v_u, desc, "setInputPrimitiveTopology:", d->topology);
    id err = nil;
    *slotp = MSG(F_id_id_err, g_device, "newRenderPipelineStateWithDescriptor:error:", desc, &err);
    if (!*slotp)
        mime_warn("gpu: pipeline %u: %s", pc->id, error_text(err));
    release(desc);
}

static void stencil_face(id sd, const MimeGpuStencilFace *f)
{
    MSG(F_v_u, sd, "setStencilCompareFunction:", f->func);
    MSG(F_v_u, sd, "setStencilFailureOperation:", f->fail);
    MSG(F_v_u, sd, "setDepthFailureOperation:", f->depth_fail);
    MSG(F_v_u, sd, "setDepthStencilPassOperation:", f->pass);
    MSG(F_v_u32, sd, "setReadMask:", f->read_mask);
    MSG(F_v_u32, sd, "setWriteMask:", f->write_mask);
}

static id new_depth_state(const MimeGpuDepthStencilCreate *d)
{
    id desc = MSG(F_id, MSG(F_id, CLS("MTLDepthStencilDescriptor"), "alloc"), "init");
    MSG(F_v_u, desc, "setDepthCompareFunction:", d->depth_func);
    MSG(F_v_b, desc, "setDepthWriteEnabled:", d->depth_write != 0);
    if (d->stencil) {
        id front = MSG(F_id, MSG(F_id, CLS("MTLStencilDescriptor"), "alloc"), "init");
        id back = MSG(F_id, MSG(F_id, CLS("MTLStencilDescriptor"), "alloc"), "init");
        stencil_face(front, &d->front);
        stencil_face(back, &d->back);
        MSG(F_v_id, desc, "setFrontFaceStencil:", front);
        MSG(F_v_id, desc, "setBackFaceStencil:", back);
        release(front);
        release(back);
    }
    id s = MSG(F_id_id, g_device, "newDepthStencilStateWithDescriptor:", desc);
    release(desc);
    return s;
}

static void sampler_create(Channel *c, const MimeGpuSamplerCreate *s)
{
    id *slotp = SLOT(c, samplers, s->id);
    release(*slotp);
    id d = MSG(F_id, MSG(F_id, CLS("MTLSamplerDescriptor"), "alloc"), "init");
    MSG(F_v_u, d, "setMinFilter:", s->min_filter);
    MSG(F_v_u, d, "setMagFilter:", s->mag_filter);
    MSG(F_v_u, d, "setMipFilter:", s->mip_filter);
    MSG(F_v_u, d, "setSAddressMode:", s->address_s);
    MSG(F_v_u, d, "setTAddressMode:", s->address_t);
    MSG(F_v_u, d, "setRAddressMode:", s->address_r);
    MSG(F_v_u, d, "setBorderColor:", s->border);
    MSG(F_v_u, d, "setMaxAnisotropy:", s->max_anisotropy ? s->max_anisotropy : 1);
    MSG(F_v_f, d, "setLodMinClamp:", s->lod_min);
    MSG(F_v_f, d, "setLodMaxClamp:", s->lod_max);
    if (s->compare)
        MSG(F_v_u, d, "setCompareFunction:", s->compare);
    *slotp = MSG(F_id_id, g_device, "newSamplerStateWithDescriptor:", d);
    release(d);
}

/* ---------------------------------------------------------------- surfaces */

static void surface_config(Channel *c, const MimeGpuSurfaceConfig *sc)
{
    Surface *s = SLOT(c, surfaces, sc->id);
    uint32_t w = sc->width ? sc->width : 1, h = sc->height ? sc->height : 1;
    uint32_t samples = sc->samples ? sc->samples : 1;
    if (!s->color || s->width != w || s->height != h || s->samples != samples ||
        s->depth_format != sc->depth_format || s->stencil_format != sc->stencil_format) {
        end_render(c);
        release(s->color);
        release(s->depth);
        s->depth = nil;
        s->color = new_texture(samples > 1 ? TEXTURE_2D_MS : TEXTURE_2D, PIXEL_BGRA8, w, h, 1, 1, samples,
                               USAGE_SHADER_READ | USAGE_RENDER_TARGET, 0);
        uint32_t ds = sc->depth_format && sc->stencil_format ? PIXEL_D32S8 : sc->depth_format ? sc->depth_format
                                                                                              : sc->stencil_format;
        if (ds)
            s->depth = new_texture(samples > 1 ? TEXTURE_2D_MS : TEXTURE_2D, ds, w, h, 1, 1, samples, USAGE_RENDER_TARGET,
                                   0);
        track_reset(&s->color_track, true);
        track_reset(&s->depth_track, true);
        s->width = w;
        s->height = h;
        s->samples = samples;
        s->depth_format = sc->depth_format;
        s->stencil_format = sc->stencil_format;
    }
    if (!s->layer)
        s->layer = mime_ui_metal_layer_create(g_device);
    s->window_id = sc->window_id;
    s->sync = sc->sync != 0;
    mime_ui_metal_layer_place(s->layer, sc->window_id, sc->x, sc->y, w, h, s->sync);
}

static void surface_destroy(Channel *c, uint32_t id_)
{
    if (id_ >= c->surfaces_cap)
        return;
    Surface *s = &c->surfaces[id_];
    if (in_pass(c, id_, true))
        end_render(c);
    release(s->color);
    release(s->depth);
    if (s->layer)
        mime_ui_metal_layer_destroy(s->layer);
    memset(s, 0, sizeof(*s));
}

/* copies the back buffer into the layer's next drawable; the layer flips it to gl's orientation. */
static void present(Channel *c, uint32_t id_)
{
    if (id_ >= c->surfaces_cap || !c->surfaces[id_].layer || !c->surfaces[id_].window_id) {
        commit(c, 0);
        return;
    }
    Surface *s = &c->surfaces[id_];
    id src = s->color;
    if (s->samples > 1) {
        /* drawables are single sampled: resolve into a temporary texture first. */
        commit(c, 0);
        return;
    }
    end_render(c);
    materialize(c, id_, true, false);
    id drawable = MSG(F_id, (id)s->layer, "nextDrawable");
    if (drawable) {
        id dst = MSG(F_id, drawable, "texture");
        NSUInteger w = MSG(NSUInteger (*)(id, SEL), dst, "width"), h = MSG(NSUInteger (*)(id, SEL), dst, "height");
        if (w > s->width)
            w = s->width;
        if (h > s->height)
            h = s->height;
        id b = blit_encoder(c);
        MTLOrigin zero = {0, 0, 0};
        MTLSize size = {w, h, 1};
        ((void (*)(id, SEL, id, NSUInteger, NSUInteger, MTLOrigin, MTLSize, id, NSUInteger, NSUInteger, MTLOrigin))
             objc_msgSend)(b, S("copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toTexture:"
                                "destinationSlice:destinationLevel:destinationOrigin:"),
                           src, 0, 0, zero, size, dst, 0, 0, zero);
        end_blit(c);
        MSG(F_v_id, command_buffer(c), "presentDrawable:", drawable);
    }
    commit(c, 0);
}

/* ---------------------------------------------------------------- passes */

#define ATTACHMENTS (MIME_GPU_MAX_COLORS + 2)

static bool has_depth(NSUInteger f)
{
    return f == 250 || f == 252 || f == 255 || f == 260;
}

static bool has_stencil(NSUInteger f)
{
    return f == 253 || f == 255 || f == 260 || f == 261 || f == 262;
}

static Track *track_of(Channel *c, uint32_t id_, bool surface, bool depth)
{
    Track *t = NULL;
    if (surface) {
        if (id_ < c->surfaces_cap)
            t = depth ? &c->surfaces[id_].depth_track : &c->surfaces[id_].color_track;
    } else if (id_ < c->textures_cap) {
        t = &c->textures[id_].track;
    }
    return t && t->tracked ? t : NULL;
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

/* the followed aspect attachment i renders to, NULL when its contents are not followed. */
static Aspect *pass_aspect(Channel *c, int i)
{
    MimeGpuAttachment *a = pass_att(c, i);
    if (!a->id || a->level || a->slice || a->resolve)
        return NULL;
    Track *t = track_of(c, a->id, a->surface, i >= MIME_GPU_MAX_COLORS);
    return t ? &t->aspect[i == MIME_GPU_MAX_COLORS + 1] : NULL;
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
        pass_att(c, i)->load = LOAD_LOAD;
    c->written = 0;
}

static id new_encoder(Channel *c, id rp)
{
    end_blit(c);
    id e = MSG(F_id_id, command_buffer(c), "renderCommandEncoderWithDescriptor:", rp);
    if (e)
        MSG(F_v, e, "retain");
    return e;
}

/* opens the encoder of the guest pass, its load actions taken from what each attachment holds. */
static bool ensure_encoder(Channel *c)
{
    if (c->render)
        return true;
    if (!c->pass_live)
        return false;
    id rp = MSG(F_id, CLS("MTLRenderPassDescriptor"), "renderPassDescriptor");
    id colors = MSG(F_id, rp, "colorAttachments");
    for (int i = 0; i < ATTACHMENTS; i++) {
        MimeGpuAttachment *a = pass_att(c, i);
        c->enc_unknown[i] = false;
        if (!a->id)
            continue;
        id tex = texture_of(c, a->id, a->surface, i >= MIME_GPU_MAX_COLORS);
        if (!tex) {
            c->pass_live = false;
            return false;
        }
        Aspect *as = pass_aspect(c, i);
        uint8_t load = a->load, store = a->store;
        if (as && load == LOAD_LOAD && as->kind == TRACK_UNDEFINED) {
            load = LOAD_DONT_CARE;
        } else if (as && load == LOAD_LOAD && as->kind == TRACK_CLEARED) {
            load = LOAD_CLEAR;
            if (i < MIME_GPU_MAX_COLORS)
                memcpy(c->pass.clear_color[i], as->value, sizeof(as->value));
            else if (i == MIME_GPU_MAX_COLORS)
                c->pass.clear_depth = as->value[0];
            else
                c->pass.clear_stencil = as->stencil;
        }
        /* a followed attachment's store is decided when the encoder ends, by what it wrote. */
        if (as && store == STORE_STORE) {
            store = STORE_UNKNOWN;
            c->enc_unknown[i] = true;
        }
        c->enc_load[i] = load;
        id att = i < MIME_GPU_MAX_COLORS        ? MSG(F_id_u, colors, "objectAtIndexedSubscript:", (NSUInteger)i)
                 : i == MIME_GPU_MAX_COLORS ? MSG(F_id, rp, "depthAttachment")
                                             : MSG(F_id, rp, "stencilAttachment");
        MSG(F_v_id, att, "setTexture:", tex);
        MSG(F_v_u, att, "setLevel:", a->level);
        MSG(F_v_u, att, "setSlice:", a->slice);
        MSG(F_v_u, att, "setLoadAction:", load);
        MSG(F_v_u, att, "setStoreAction:", store);
        if (a->resolve)
            MSG(F_v_id, att, "setResolveTexture:", texture_of(c, a->resolve, false, false));
        if (i < MIME_GPU_MAX_COLORS) {
            const float *v = c->pass.clear_color[i];
            ((void (*)(id, SEL, MTLClearColor))objc_msgSend)(att, S("setClearColor:"), (MTLClearColor){v[0], v[1], v[2], v[3]});
        } else if (i == MIME_GPU_MAX_COLORS) {
            MSG(F_v_d, att, "setClearDepth:", c->pass.clear_depth);
        } else {
            MSG(F_v_u32, att, "setClearStencil:", c->pass.clear_stencil);
        }
    }
    MSG(F_v_u, rp, "setRenderTargetWidth:", c->pass.width);
    MSG(F_v_u, rp, "setRenderTargetHeight:", c->pass.height);
    c->pass_started = true;
    c->written = 0;
    c->render = new_encoder(c, rp);
    c->st.dirty = DIRTY_ALL;
    return c->render != nil;
}

/*
 * ends the open encoder of the guest pass. a followed attachment is stored
 * only when the encoder wrote it or loaded what it holds: a cleared one
 * nothing drew to keeps its clear as state, the next pass loading it clears
 * again. a pass segment without an encoder does no gpu work when every
 * attachment is followed.
 */
static void end_render(Channel *c)
{
    if (!c->render) {
        if (!c->pass_live)
            return;
        bool gpu = false;
        for (int i = 0; i < ATTACHMENTS; i++) {
            MimeGpuAttachment *a = pass_att(c, i);
            if (a->id && a->load == LOAD_CLEAR && !pass_aspect(c, i))
                gpu = true;
        }
        if (!gpu) {
            for (int i = 0; i < ATTACHMENTS; i++) {
                Aspect *as = pass_aspect(c, i);
                uint8_t load = pass_att(c, i)->load;
                if (as && load == LOAD_CLEAR)
                    aspect_clear_value(c, i, as);
                else if (as && load == LOAD_DONT_CARE)
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
            store = written || c->enc_load[i] == LOAD_LOAD ? STORE_STORE : STORE_DONT_CARE;
            if (i < MIME_GPU_MAX_COLORS)
                MSG(F_v_u_u, c->render, "setColorStoreAction:atIndex:", store, (NSUInteger)i);
            else if (i == MIME_GPU_MAX_COLORS)
                MSG(F_v_u, c->render, "setDepthStoreAction:", store);
            else
                MSG(F_v_u, c->render, "setStencilStoreAction:", store);
        }
        if (as) {
            if (store != STORE_DONT_CARE)
                as->kind = TRACK_DATA;
            else if (c->enc_load[i] == LOAD_CLEAR && !written)
                aspect_clear_value(c, i, as);
            else
                as->kind = TRACK_UNDEFINED;
        }
        if (a->resolve)
            set_data(track_of(c, a->resolve, false, false));
    }
    MSG(F_v, c->render, "endEncoding");
    release(c->render);
    c->render = nil;
    pass_continue(c);
}

/* gives a cleared aspect nothing stored yet its clear value in memory, for reads outside passes. */
static void materialize(Channel *c, uint32_t id_, bool surface, bool depth)
{
    Track *t = track_of(c, id_, surface, depth);
    id tex = texture_of(c, id_, surface, depth);
    if (!pending(t) || !tex)
        return;
    end_render(c);
    end_blit(c);
    if (!pending(t))
        return;
    NSUInteger format = MSG(NSUInteger (*)(id, SEL), tex, "pixelFormat");
    id rp = MSG(F_id, CLS("MTLRenderPassDescriptor"), "renderPassDescriptor");
    for (int k = 0; k < 2; k++) {
        Aspect *as = &t->aspect[k];
        id att;
        if (has_depth(format) || has_stencil(format)) {
            if (k == 0 && !has_depth(format))
                continue;
            if (k == 1 && !has_stencil(format))
                continue;
            att = MSG(F_id, rp, k ? "stencilAttachment" : "depthAttachment");
            if (k)
                MSG(F_v_u32, att, "setClearStencil:", as->stencil);
            else
                MSG(F_v_d, att, "setClearDepth:", as->value[0]);
        } else {
            if (k)
                continue;
            att = MSG(F_id_u, MSG(F_id, rp, "colorAttachments"), "objectAtIndexedSubscript:", (NSUInteger)0);
            ((void (*)(id, SEL, MTLClearColor))objc_msgSend)(
                att, S("setClearColor:"), (MTLClearColor){as->value[0], as->value[1], as->value[2], as->value[3]});
        }
        MSG(F_v_id, att, "setTexture:", tex);
        MSG(F_v_u, att, "setLoadAction:",
            as->kind == TRACK_CLEARED ? LOAD_CLEAR : as->kind == TRACK_DATA ? LOAD_LOAD : LOAD_DONT_CARE);
        MSG(F_v_u, att, "setStoreAction:", STORE_STORE);
        if (as->kind == TRACK_CLEARED)
            as->materialized = true;
    }
    id e = MSG(F_id_id, command_buffer(c), "renderCommandEncoderWithDescriptor:", rp);
    MSG(F_v, e, "endEncoding");
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
        Surface *s = a->id < c->surfaces_cap ? &c->surfaces[a->id] : NULL;
        if (!s || !s->color)
            return 0;
        if (!depth)
            return PIXEL_BGRA8;
        return s->depth_format && s->stencil_format ? PIXEL_D32S8 : s->depth_format ? s->depth_format : s->stencil_format;
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
            c->pass_depth = f == PIXEL_S8 ? 0 : f;
        else
            c->pass_stencil = f == PIXEL_D32 ? 0 : f;
        id t = a->id ? texture_of(c, a->id, a->surface, i >= MIME_GPU_MAX_COLORS) : nil;
        if (t && !c->pass_samples)
            c->pass_samples = (uint32_t)MSG(NSUInteger (*)(id, SEL), t, "sampleCount");
    }
    if (!c->pass_samples)
        c->pass_samples = 1;
    c->pass_width = p->width;
    c->pass_height = p->height;
    c->st.dirty = DIRTY_ALL;
}

/* whether texture id_ is an attachment of the guest pass. */
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

/* applies the state the guest changed since the last draw. */
static bool flush_state(Channel *c)
{
    if (!c->st.pipeline || !ensure_encoder(c))
        return false;
    id e = c->render;
    uint32_t d = c->st.dirty;
    if (d & DIRTY_PIPELINE)
        MSG(F_v_id, e, "setRenderPipelineState:", c->st.pipeline);
    if (d & DIRTY_DEPTH) {
        if (c->st.depth_state)
            MSG(F_v_id, e, "setDepthStencilState:", c->st.depth_state);
        MSG(F_v_u32_u32, e, "setStencilFrontReferenceValue:backReferenceValue:", c->st.front_ref, c->st.back_ref);
    }
    if (d & DIRTY_RASTER) {
        MSG(F_v_u, e, "setCullMode:", c->st.raster.cull);
        MSG(F_v_u, e, "setFrontFacingWinding:", c->st.raster.winding);
        MSG(F_v_u, e, "setTriangleFillMode:", c->st.raster.fill);
        MSG(F_v_f3, e, "setDepthBias:slopeScale:clamp:", c->st.raster.depth_bias, c->st.raster.slope_scale,
            c->st.raster.bias_clamp);
        MSG(F_v_u, e, "setDepthClipMode:", c->st.raster.depth_clip ? 0 : 1);
    }
    if (d & DIRTY_VIEWPORT)
        ((void (*)(id, SEL, MTLViewport))objc_msgSend)(e, S("setViewport:"), c->st.viewport);
    if (d & DIRTY_SCISSOR) {
        /* metal wants the rectangle inside the attachments. */
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
        ((void (*)(id, SEL, MTLScissorRect))objc_msgSend)(e, S("setScissorRect:"), sr);
    }
    if (d & DIRTY_BLEND)
        MSG(F_v_f4, e, "setBlendColorRed:green:blue:alpha:", c->st.blend[0], c->st.blend[1], c->st.blend[2],
            c->st.blend[3]);
    if (d & DIRTY_VB) {
        for (uint32_t m = c->st.vb_mask, i; m; m &= m - 1) {
            i = (uint32_t)__builtin_ctz(m);
            uint64_t off;
            id buf = heap(c, &c->st.vb[i], 0, &off);
            if (buf)
                MSG(F_v_id_u_u, e, "setVertexBuffer:offset:atIndex:", buf, off, MIME_GPU_VERTEX_BUFFER_BASE + i);
        }
    }
    if ((d & DIRTY_UNIFORMS) && c->st.uniform_bytes) {
        MSG(F_v_p_u_u, e, "setVertexBytes:length:atIndex:", c->st.uniforms, c->st.uniform_bytes, MIME_GPU_UNIFORM_BUFFER);
        MSG(F_v_p_u_u, e, "setFragmentBytes:length:atIndex:", c->st.uniforms, c->st.uniform_bytes,
            MIME_GPU_UNIFORM_BUFFER);
    }
    if (d & DIRTY_UNIFORMS) {
        for (int stage = 0; stage < MIME_GPU_STAGES; stage++) {
            for (uint32_t mask = c->st.uniform_buffer_mask[stage]; mask; mask &= mask - 1) {
                uint32_t index = (uint32_t)__builtin_ctz(mask);
                uint64_t offset;
                id buffer = heap(c, &c->st.uniform_buffers[stage][index], 0, &offset);
                if (buffer) {
                    if (stage)
                        MSG(F_v_id_u_u, e, "setFragmentBuffer:offset:atIndex:", buffer, offset, index + 1);
                    else
                        MSG(F_v_id_u_u, e, "setVertexBuffer:offset:atIndex:", buffer, offset, index + 1);
                }
            }
        }
    }
    if (d & DIRTY_TEXTURES) {
        for (int s = 0; s < MIME_GPU_STAGES; s++) {
            for (uint32_t m = c->st.tex_mask[s], i; m; m &= m - 1) {
                i = (uint32_t)__builtin_ctz(m);
                Binding *b = &c->st.tex[s][i];
                id t = texture_of(c, b->id, b->surface, false);
                if (!b->surface && b->id < c->textures_cap && c->textures[b->id].sampled)
                    t = c->textures[b->id].sampled;
                id smp = b->sampler < c->samplers_cap ? c->samplers[b->sampler] : nil;
                if (s) {
                    MSG(F_v_id_u, e, "setFragmentTexture:atIndex:", t, i);
                    if (smp)
                        MSG(F_v_id_u, e, "setFragmentSamplerState:atIndex:", smp, i);
                } else {
                    MSG(F_v_id_u, e, "setVertexTexture:atIndex:", t, i);
                    if (smp)
                        MSG(F_v_id_u, e, "setVertexSamplerState:atIndex:", smp, i);
                }
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

static id internal_function(Channel *c, const char *name)
{
    if (!c->internal_library) {
        id err = nil;
        c->internal_library =
            MSG(F_id_id_id_err, g_device, "newLibraryWithSource:options:error:", nsstring(g_internal_msl), nil, &err);
        if (!c->internal_library) {
            mime_warn("gpu: internal shaders: %s", error_text(err));
            return nil;
        }
    }
    return MSG(F_id_id, c->internal_library, "newFunctionWithName:", nsstring(name));
}

/* a clear draws through the scissor with a fragment function writing the color to every target. */
static id clear_pipeline(Channel *c, uint32_t color_mask)
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
    char *error = NULL;
    id fs = make_function(src, "mime_clear_fs", &error);
    free(error);
    id vs = internal_function(c, "mime_clear_vs");
    if (!fs || !vs) {
        release(fs);
        release(vs);
        return nil;
    }
    id desc = MSG(F_id, MSG(F_id, CLS("MTLRenderPipelineDescriptor"), "alloc"), "init");
    MSG(F_v_id, desc, "setVertexFunction:", vs);
    MSG(F_v_id, desc, "setFragmentFunction:", fs);
    id colors = MSG(F_id, desc, "colorAttachments");
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++) {
        if (!c->pass_color[i])
            continue;
        MimeGpuColorTarget t = {.format = c->pass_color[i], .write_mask = (uint8_t)color_mask};
        set_color_target(MSG(F_id_u, colors, "objectAtIndexedSubscript:", (NSUInteger)i), &t);
    }
    MSG(F_v_u, desc, "setDepthAttachmentPixelFormat:", c->pass_depth);
    MSG(F_v_u, desc, "setStencilAttachmentPixelFormat:", c->pass_stencil);
    MSG(F_v_u, desc, "setRasterSampleCount:", c->pass_samples);
    id err = nil;
    id ps = MSG(F_id_id_err, g_device, "newRenderPipelineStateWithDescriptor:error:", desc, &err);
    if (!ps)
        mime_warn("gpu: clear pipeline: %s", error_text(err));
    release(desc);
    release(vs);
    release(fs);
    int victim = (int)(key % 16);
    release(c->clear_pipelines[victim]);
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
                c->pass.color[i].load = LOAD_CLEAR;
                memcpy(c->pass.clear_color[i], cl->color, sizeof(cl->color));
            }
        }
        if (mask & MIME_GPU_CLEAR_DEPTH) {
            c->pass.depth.load = LOAD_CLEAR;
            c->pass.clear_depth = cl->depth;
        }
        if (mask & MIME_GPU_CLEAR_STENCIL) {
            c->pass.stencil.load = LOAD_CLEAR;
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
    id ps = clear_pipeline(c, (mask & MIME_GPU_CLEAR_COLOR) ? cl->color_mask : 0);
    if (!ps)
        return;
    MimeGpuDepthStencilCreate ds = {
        .depth_func = 7,
        .depth_write = (mask & MIME_GPU_CLEAR_DEPTH) != 0,
        .stencil = (mask & MIME_GPU_CLEAR_STENCIL) != 0,
        .front = {.func = 7, .fail = 2, .depth_fail = 2, .pass = 2, .read_mask = 0xFF,
                  .write_mask = (uint8_t)cl->stencil_write_mask},
    };
    ds.back = ds.front;
    id dss = new_depth_state(&ds);
    id e = c->render;
    MSG(F_v_id, e, "setRenderPipelineState:", ps);
    MSG(F_v_id, e, "setDepthStencilState:", dss);
    MSG(F_v_u32_u32, e, "setStencilFrontReferenceValue:backReferenceValue:", cl->stencil, cl->stencil);
    MSG(F_v_u, e, "setCullMode:", 0);
    MSG(F_v_u, e, "setTriangleFillMode:", 0);
    MTLViewport vp = {0, 0, c->pass_width, c->pass_height, 0, 1};
    ((void (*)(id, SEL, MTLViewport))objc_msgSend)(e, S("setViewport:"), vp);
    uint32_t dirty = c->st.dirty;
    c->st.dirty = DIRTY_SCISSOR;
    id saved = c->st.pipeline;
    c->st.pipeline = ps;
    flush_state(c);
    c->st.pipeline = saved;
    float depth = cl->depth;
    MSG(F_v_p_u_u, e, "setVertexBytes:length:atIndex:", &depth, sizeof(depth), 0);
    MSG(F_v_p_u_u, e, "setFragmentBytes:length:atIndex:", cl->color, sizeof(cl->color), 0);
    ((void (*)(id, SEL, NSUInteger, NSUInteger, NSUInteger))objc_msgSend)(e, S("drawPrimitives:vertexStart:vertexCount:"),
                                                                         3, 0, 3);
    release(dss);
    c->st.dirty = dirty | DIRTY_PIPELINE | DIRTY_DEPTH | DIRTY_RASTER | DIRTY_VIEWPORT | DIRTY_UNIFORMS;
}

/* ---------------------------------------------------------------- blits */

static id blit_pipeline(Channel *c, uint32_t format)
{
    for (int i = 0; i < 8; i++)
        if (c->blit_pipelines[i] && c->blit_keys[i] == format)
            return c->blit_pipelines[i];
    id vs = internal_function(c, "mime_blit_vs"), fs = internal_function(c, "mime_blit_fs");
    if (!vs || !fs) {
        release(vs);
        release(fs);
        return nil;
    }
    id desc = MSG(F_id, MSG(F_id, CLS("MTLRenderPipelineDescriptor"), "alloc"), "init");
    MSG(F_v_id, desc, "setVertexFunction:", vs);
    MSG(F_v_id, desc, "setFragmentFunction:", fs);
    MimeGpuColorTarget t = {.format = (uint16_t)format, .write_mask = 0xF};
    set_color_target(MSG(F_id_u, MSG(F_id, desc, "colorAttachments"), "objectAtIndexedSubscript:", (NSUInteger)0), &t);
    id err = nil;
    id ps = MSG(F_id_id_err, g_device, "newRenderPipelineStateWithDescriptor:error:", desc, &err);
    if (!ps)
        mime_warn("gpu: blit pipeline: %s", error_text(err));
    release(desc);
    release(vs);
    release(fs);
    int victim = (int)(format % 8);
    release(c->blit_pipelines[victim]);
    c->blit_pipelines[victim] = ps;
    c->blit_keys[victim] = format;
    return ps;
}

static void blit(Channel *c, const MimeGpuBlit *b)
{
    end_render(c);
    bool depth = (b->mask & (MIME_GPU_CLEAR_DEPTH | MIME_GPU_CLEAR_STENCIL)) != 0;
    id src = texture_of(c, b->src.id, b->src.surface, depth), dst = texture_of(c, b->dst.id, b->dst.surface, depth);
    if (!src || !dst)
        return;
    int32_t sw = b->src_x1 - b->src_x0, sh = b->src_y1 - b->src_y0;
    int32_t dw = b->dst_x1 - b->dst_x0, dh = b->dst_y1 - b->dst_y0;
    NSUInteger src_samples = MSG(NSUInteger (*)(id, SEL), src, "sampleCount");
    NSUInteger dst_samples = MSG(NSUInteger (*)(id, SEL), dst, "sampleCount");
    NSUInteger src_format = MSG(NSUInteger (*)(id, SEL), src, "pixelFormat");
    NSUInteger dst_format = MSG(NSUInteger (*)(id, SEL), dst, "pixelFormat");
    materialize(c, b->src.id, b->src.surface, depth);
    int32_t lx = b->dst_x0 < b->dst_x1 ? b->dst_x0 : b->dst_x1, ly = b->dst_y0 < b->dst_y1 ? b->dst_y0 : b->dst_y1;
    bool whole = !depth && covers(c, b->dst.id, b->dst.surface, b->dst.level, b->dst.slice, lx, ly, abs(dw), abs(dh));
    if (sw == dw && sh == dh && sw > 0 && sh > 0 && src_samples == dst_samples && src_format == dst_format) {
        write_texture(c, b->dst.id, b->dst.surface, depth, whole);
        id e = blit_encoder(c);
        MTLOrigin so = {(NSUInteger)b->src_x0, (NSUInteger)b->src_y0, 0}, dor = {(NSUInteger)b->dst_x0, (NSUInteger)b->dst_y0, 0};
        MTLSize size = {(NSUInteger)sw, (NSUInteger)sh, 1};
        ((void (*)(id, SEL, id, NSUInteger, NSUInteger, MTLOrigin, MTLSize, id, NSUInteger, NSUInteger, MTLOrigin))
             objc_msgSend)(e, S("copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toTexture:"
                                "destinationSlice:destinationLevel:destinationOrigin:"),
                           src, b->src.slice, b->src.level, so, size, dst, b->dst.slice, b->dst.level, dor);
        return;
    }
    end_blit(c);
    if (src_samples > 1 && dst_samples == 1 && !depth) {
        /* a resolve: the whole source into the destination, which has its size. */
        id rp = MSG(F_id, CLS("MTLRenderPassDescriptor"), "renderPassDescriptor");
        id att = MSG(F_id_u, MSG(F_id, rp, "colorAttachments"), "objectAtIndexedSubscript:", (NSUInteger)0);
        MSG(F_v_id, att, "setTexture:", src);
        MSG(F_v_u, att, "setLoadAction:", LOAD_LOAD);
        MSG(F_v_u, att, "setStoreAction:", 3);
        MSG(F_v_id, att, "setResolveTexture:", dst);
        MSG(F_v_u, att, "setResolveLevel:", b->dst.level);
        MSG(F_v_u, att, "setResolveSlice:", b->dst.slice);
        write_texture(c, b->dst.id, b->dst.surface, false, true);
        id e = MSG(F_id_id, command_buffer(c), "renderCommandEncoderWithDescriptor:", rp);
        MSG(F_v, e, "endEncoding");
        return;
    }
    if (depth || src_samples > 1)
        return;
    /* a stretch: draws the source rectangle over the destination one. */
    uint32_t format = (uint32_t)MSG(NSUInteger (*)(id, SEL), dst, "pixelFormat");
    id ps = blit_pipeline(c, format);
    if (!ps)
        return;
    NSUInteger tw = MSG(NSUInteger (*)(id, SEL), src, "width"), th = MSG(NSUInteger (*)(id, SEL), src, "height");
    id rp = MSG(F_id, CLS("MTLRenderPassDescriptor"), "renderPassDescriptor");
    id att = MSG(F_id_u, MSG(F_id, rp, "colorAttachments"), "objectAtIndexedSubscript:", (NSUInteger)0);
    MSG(F_v_id, att, "setTexture:", dst);
    MSG(F_v_u, att, "setLevel:", b->dst.level);
    MSG(F_v_u, att, "setSlice:", b->dst.slice);
    /* the pass loads the destination it does not cover: a pending clear needs no memory traffic. */
    Track *dt = track_of(c, b->dst.id, b->dst.surface, false);
    uint8_t load = LOAD_LOAD;
    if (dt && whole) {
        load = LOAD_DONT_CARE;
    } else if (dt && dt->aspect[0].kind == TRACK_CLEARED && !b->dst.level && !b->dst.slice) {
        const float *v = dt->aspect[0].value;
        load = LOAD_CLEAR;
        ((void (*)(id, SEL, MTLClearColor))objc_msgSend)(att, S("setClearColor:"), (MTLClearColor){v[0], v[1], v[2], v[3]});
    }
    set_data(dt);
    MSG(F_v_u, att, "setLoadAction:", load);
    MSG(F_v_u, att, "setStoreAction:", STORE_STORE);
    id e = MSG(F_id_id, command_buffer(c), "renderCommandEncoderWithDescriptor:", rp);
    int32_t x0 = b->dst_x0 < b->dst_x1 ? b->dst_x0 : b->dst_x1, y0 = b->dst_y0 < b->dst_y1 ? b->dst_y0 : b->dst_y1;
    MTLViewport vp = {x0, y0, abs(dw), abs(dh), 0, 1};
    ((void (*)(id, SEL, MTLViewport))objc_msgSend)(e, S("setViewport:"), vp);
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
    MimeGpuSamplerCreate sc = {.min_filter = (uint8_t)b->linear, .mag_filter = (uint8_t)b->linear};
    id d = MSG(F_id, MSG(F_id, CLS("MTLSamplerDescriptor"), "alloc"), "init");
    MSG(F_v_u, d, "setMinFilter:", sc.min_filter);
    MSG(F_v_u, d, "setMagFilter:", sc.mag_filter);
    id smp = MSG(F_id_id, g_device, "newSamplerStateWithDescriptor:", d);
    release(d);
    MSG(F_v_id, e, "setRenderPipelineState:", ps);
    MSG(F_v_p_u_u, e, "setVertexBytes:length:atIndex:", uv, sizeof(uv), 0);
    MSG(F_v_id_u, e, "setFragmentTexture:atIndex:", src, 0);
    MSG(F_v_id_u, e, "setFragmentSamplerState:atIndex:", smp, 0);
    ((void (*)(id, SEL, NSUInteger, NSUInteger, NSUInteger))objc_msgSend)(e, S("drawPrimitives:vertexStart:vertexCount:"),
                                                                         3, 0, 3);
    MSG(F_v, e, "endEncoding");
    release(smp);
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
    ((void (*)(id, SEL, NSUInteger, NSUInteger, NSUInteger, NSUInteger, NSUInteger))objc_msgSend)(
        c->render, S("drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:"), d->primitive, d->first,
        d->count, d->instances ? d->instances : 1, d->base_instance);
}

static void draw_indexed(Channel *c, const MimeGpuDrawIndexed *d)
{
    if (!d->count || !draw_ready(c))
        return;
    uint64_t off;
    id buf = heap(c, &d->indices, (uint64_t)d->count * (d->index_type ? 4 : 2), &off);
    if (!buf)
        return;
    ((void (*)(id, SEL, NSUInteger, NSUInteger, NSUInteger, id, NSUInteger, NSUInteger, NSInteger, NSUInteger))
         objc_msgSend)(c->render,
                       S("drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:"
                         "baseVertex:baseInstance:"),
                       d->primitive, d->count, d->index_type, buf, off, d->instances ? d->instances : 1, d->base_vertex,
                       d->base_instance);
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
            release(t->tex);
            release(t->sampled);
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
        id t = texture_of(c, BODY(MimeGpuObject)->id, false, false);
        if (t && MSG(NSUInteger (*)(id, SEL), t, "mipmapLevelCount") > 1)
            MSG(F_v_id, blit_encoder(c), "generateMipmapsForTexture:", t);
        break;
    }
    case MIME_GPU_BUFFER_COPY: {
        NEED(MimeGpuBufferCopy);
        const MimeGpuBufferCopy *b = BODY(MimeGpuBufferCopy);
        uint64_t so, dof;
        id src = heap(c, &b->src, b->size, &so), dst = heap(c, &b->dst, b->size, &dof);
        if (src && dst && b->size)
            ((void (*)(id, SEL, id, NSUInteger, id, NSUInteger, NSUInteger))objc_msgSend)(
                blit_encoder(c), S("copyFromBuffer:sourceOffset:toBuffer:destinationOffset:size:"), src, so, dst, dof,
                b->size);
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
        id *slotp = SLOT(c, depth_states, d->id);
        release(*slotp);
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
        c->st.pipeline = i < c->pipelines_cap ? c->pipelines[i] : nil;
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

/* whether the guest waits for a fence held back, not one the ring has not reached yet. */
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
        void *pool = objc_autoreleasePoolPush();
        commit(c, 0);
        objc_autoreleasePoolPop(pool);
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
        void *pool = objc_autoreleasePoolPush();
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
        objc_autoreleasePoolPop(pool);
        if (c->hdr->lost) {
            /* nothing more runs: the guest sees lost and stops waiting. */
            __atomic_store_n(&c->hdr->get, __atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
            signal_fence(c, UINT64_MAX >> 1);
        }
        if (__atomic_load_n(&c->hdr->put, __ATOMIC_ACQUIRE) == get && !wait_for_work(c, get))
            break;
    }
    return NULL;
}

/* ---------------------------------------------------------------- hypercalls */

static bool add_block(Channel *c, uint32_t index, uint64_t pa, uint64_t size)
{
    uint8_t *base = mime_vm_phys(c->vm, pa, size);
    if (!base || index >= MIME_GPU_MAX_BLOCKS || c->blocks[index] || ((uintptr_t)base & (MIME_GPU_ALIGN - 1)) ||
        (size & (MIME_GPU_ALIGN - 1)))
        return false;
    id buf = MSG(F_id_p_u_u_blk, g_device, "newBufferWithBytesNoCopy:length:options:deallocator:", base, size, 0, NULL);
    if (!buf)
        return false;
    c->block_base[index] = base;
    c->block_size[index] = size;
    __atomic_store_n(&c->blocks[index], buf, __ATOMIC_RELEASE);
    return true;
}

static void init_device(void)
{
    g_device = MTLCreateSystemDefaultDevice();
    g_compile_queue = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
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
    pthread_mutex_init(&c->lock, NULL);
    pthread_cond_init(&c->cond, NULL);
    void *pool = objc_autoreleasePoolPush();
    bool ok = add_block(c, 0, pa, size);
    c->queue = MSG(F_id, g_device, "newCommandQueue");
    objc_autoreleasePoolPop(pool);
    pthread_mutex_lock(&g_lock);
    for (uint32_t i = 1; ok && i < MAX_CHANNELS; i++) {
        if (!g_channels[i]) {
            c->id = i;
            g_channels[i] = c;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    if (!ok || !c->id || !c->queue) {
        release(c->queue);
        release(c->blocks[0]);
        free(c);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    pthread_create(&c->thread, NULL, channel_main, c);
    *channel = c->id;
    return MIME_HC_STATUS_OK;
}

uint64_t mime_gpu_add_block(MimeVm *vm, uint64_t channel, uint64_t index, uint64_t pa, uint64_t size)
{
    Channel *c = find_channel(channel);
    if (!c || c->vm != vm)
        return MIME_HC_STATUS_NOT_FOUND;
    void *pool = objc_autoreleasePoolPush();
    bool ok = add_block(c, (uint32_t)index, pa, size);
    objc_autoreleasePoolPop(pool);
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
    pthread_mutex_lock(&g_lock);
    Channel *c = channel < MAX_CHANNELS ? g_channels[channel] : NULL;
    if (c)
        g_channels[channel] = NULL;
    pthread_mutex_unlock(&g_lock);
    if (!c)
        return;
    pthread_mutex_lock(&c->lock);
    c->stop = true;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->lock);
    pthread_join(c->thread, NULL);

    void *pool = objc_autoreleasePoolPush();
    end_render(c);
    end_blit(c);
    id last = MSG(F_id, c->queue, "commandBuffer");
    if (c->cb) {
        MSG(F_v, c->cb, "commit");
        release(c->cb);
        c->cb = nil;
    }
    MSG(F_v, last, "commit");
    MSG(F_v, last, "waitUntilCompleted");
    while (__atomic_load_n(&c->outstanding, __ATOMIC_ACQUIRE))
        usleep(1000);
    for (uint32_t i = 0; i < c->textures_cap; i++) {
        release(c->textures[i].tex);
        release(c->textures[i].sampled);
    }
    for (uint32_t i = 0; i < c->programs_cap; i++)
        if (c->programs[i])
            program_destroy(c, i);
    for (uint32_t i = 0; i < c->pipelines_cap; i++)
        release(c->pipelines[i]);
    for (uint32_t i = 0; i < c->depth_states_cap; i++)
        release(c->depth_states[i]);
    for (uint32_t i = 0; i < c->samplers_cap; i++)
        release(c->samplers[i]);
    for (uint32_t i = 0; i < c->surfaces_cap; i++)
        surface_destroy(c, i);
    for (int i = 0; i < 16; i++)
        release(c->clear_pipelines[i]);
    for (int i = 0; i < 8; i++)
        release(c->blit_pipelines[i]);
    release(c->internal_library);
    for (int i = 0; i < MIME_GPU_MAX_BLOCKS; i++)
        release(c->blocks[i]);
    release(c->queue);
    objc_autoreleasePoolPop(pool);
    free(c->textures);
    free(c->programs);
    free(c->pipelines);
    free(c->depth_states);
    free(c->depth_state_flags);
    free(c->samplers);
    free(c->surfaces);
    pthread_mutex_destroy(&c->lock);
    pthread_cond_destroy(&c->cond);
    free(c);
}
