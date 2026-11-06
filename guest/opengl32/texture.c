/*
 * textures. a texture lives on the host in Metal's private storage once it has an image; uploads go through the stream
 * and a gpu copy. a texture gets more mip levels, when the application defines or asks for them, by copying into a new
 * host texture. sampling parameters become Metal sampler states shared by content.
 */
#include "opengl32.h"

/* unit slots by target. */
enum { SLOT_2D, SLOT_CUBE, SLOT_3D, SLOT_2D_ARRAY, SLOT_RECT, SLOT_1D, SLOT_2D_MS, SLOT_1D_ARRAY, SLOT_CUBE_ARRAY, SLOT_COUNT };

enum {
    MTL_TEX_1D = 0,
    MTL_TEX_1D_ARRAY = 1,
    MTL_TEX_2D = 2,
    MTL_TEX_2D_ARRAY = 3,
    MTL_TEX_2D_MS = 4,
    MTL_TEX_CUBE = 5,
    MTL_TEX_CUBE_ARRAY = 6,
    MTL_TEX_3D = 7,
};

static int slot_of(GLenum target)
{
    switch (target) {
    case GL_TEXTURE_2D:
        return SLOT_2D;
    case GL_TEXTURE_CUBE_MAP:
        return SLOT_CUBE;
    case GL_TEXTURE_3D:
        return SLOT_3D;
    case GL_TEXTURE_2D_ARRAY:
        return SLOT_2D_ARRAY;
    case GL_TEXTURE_RECTANGLE:
        return SLOT_RECT;
    case GL_TEXTURE_1D:
        return SLOT_1D;
    case GL_TEXTURE_2D_MULTISAMPLE:
        return SLOT_2D_MS;
    case GL_TEXTURE_1D_ARRAY:
        return SLOT_1D_ARRAY;
    case GL_TEXTURE_CUBE_MAP_ARRAY:
        return gl_host_has(MIME_GPU_FEATURE_CUBE_ARRAY) ? SLOT_CUBE_ARRAY : -1;
    default:
        return -1;
    }
}

static uint32_t mtl_type_of(GLenum target)
{
    switch (target) {
    case GL_TEXTURE_CUBE_MAP:
        return MTL_TEX_CUBE;
    case GL_TEXTURE_CUBE_MAP_ARRAY:
        return MTL_TEX_CUBE_ARRAY;
    case GL_TEXTURE_3D:
        return MTL_TEX_3D;
    case GL_TEXTURE_2D_ARRAY:
        return MTL_TEX_2D_ARRAY;
    case GL_TEXTURE_1D:
        return MTL_TEX_1D;
    case GL_TEXTURE_1D_ARRAY:
        return MTL_TEX_1D_ARRAY;
    case GL_TEXTURE_2D_MULTISAMPLE:
        return MTL_TEX_2D_MS;
    default:
        return MTL_TEX_2D;
    }
}

/* the unit slot a sampler type reads. */
static int slot_of_sampler(GLenum type)
{
    switch (type) {
    case GL_SAMPLER_CUBE:
    case GL_SAMPLER_CUBE_SHADOW:
    case GL_INT_SAMPLER_CUBE:
    case GL_UNSIGNED_INT_SAMPLER_CUBE:
        return SLOT_CUBE;
    case GL_SAMPLER_CUBE_MAP_ARRAY:
    case GL_SAMPLER_CUBE_MAP_ARRAY_SHADOW:
    case GL_INT_SAMPLER_CUBE_MAP_ARRAY:
    case GL_UNSIGNED_INT_SAMPLER_CUBE_MAP_ARRAY:
        return SLOT_CUBE_ARRAY;
    case GL_SAMPLER_3D:
    case GL_INT_SAMPLER_3D:
    case GL_UNSIGNED_INT_SAMPLER_3D:
        return SLOT_3D;
    case GL_SAMPLER_2D_ARRAY:
    case GL_SAMPLER_2D_ARRAY_SHADOW:
    case GL_INT_SAMPLER_2D_ARRAY:
    case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
        return SLOT_2D_ARRAY;
    case GL_SAMPLER_2D_RECT:
    case GL_SAMPLER_2D_RECT_SHADOW:
        return SLOT_RECT;
    case GL_SAMPLER_1D:
    case GL_SAMPLER_1D_SHADOW:
        return SLOT_1D;
    case GL_SAMPLER_2D_MULTISAMPLE:
        return SLOT_2D_MS;
    case GL_SAMPLER_1D_ARRAY:
        return SLOT_1D_ARRAY;
    default:
        return SLOT_2D;
    }
}

GlTexture *gl_texture(GlContext *ctx, GLuint name)
{
    void *t = gl_name_get(&ctx->shared->textures, name);
    return t == GL_RESERVED ? nullptr : t;
}

GlTexture *gl_unit_texture(GlContext *ctx, GLuint unit, GLenum sampler_type)
{
    if (unit >= 32)
        return nullptr;
    return gl_texture(ctx, ctx->units[unit][slot_of_sampler(sampler_type)]);
}

static GlTexture *bound(GlContext *ctx, GLenum target)
{
    if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        target = GL_TEXTURE_CUBE_MAP;
    int s = slot_of(target);
    return s < 0 ? nullptr : gl_texture(ctx, ctx->units[ctx->active_texture][s]);
}

/* ---------------------------------------------------------------- formats */

#define SWIZZLE(r, g, b, a) ((uint32_t)(r) | (uint32_t)(g) << 8 | (uint32_t)(b) << 16 | (uint32_t)(a) << 24)
enum { SW_ZERO, SW_ONE, SW_R, SW_G, SW_B, SW_A };

static int swizzle_component(GLenum value)
{
    switch (value) {
    case GL_ZERO: return SW_ZERO;
    case GL_ONE: return SW_ONE;
    case GL_RED: return SW_R;
    case GL_GREEN: return SW_G;
    case GL_BLUE: return SW_B;
    case GL_ALPHA: return SW_A;
    default: return -1;
    }
}

static uint32_t sampling_swizzle(const GlTexture *t)
{
    uint32_t base = t->swizzle ? t->swizzle : SWIZZLE(SW_R, SW_G, SW_B, SW_A);
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned component = (unsigned)swizzle_component(t->component_swizzle[i]);
        if (component >= SW_R)
            component = (base >> ((component - SW_R) * 8)) & 255;
        result |= component << (i * 8);
    }
    return result;
}

uint32_t gl_mtl_format(GLenum internal, GLenum format, GLenum type, uint32_t *swizzle, BOOL *compressed)
{
    *swizzle = 0;
    *compressed = FALSE;
    BOOL f32 = type == GL_FLOAT, f16 = type == GL_HALF_FLOAT;
    switch (internal) {
    case 4:
    case GL_RGBA:
    case GL_BGRA:
    case GL_RGB:
    case 3:
        if (f32)
            return 125;
        if (f16)
            return 115;
        return (format == GL_BGRA && internal != GL_RGB && internal != 3) ? 80 : 70;
    case GL_RGBA8:
    case GL_RGB8:
    case GL_RGBA4:
    case GL_RGB5_A1:
    case GL_RGB565:
    case GL_RGB5:
    case GL_RGB4:
    case GL_RGBA2:
    case GL_R3_G3_B2:
        return format == GL_BGRA ? 80 : 70;
    case GL_BGRA8_EXT:
        return 80;
    case GL_SRGB8_ALPHA8:
    case GL_SRGB8:
    case GL_SRGB:
    case GL_SRGB_ALPHA:
        return 71;
    case GL_RGBA16F:
    case GL_RGB16F:
        return 115;
    case GL_RGBA32F:
    case GL_RGB32F:
        return 125;
    case GL_RGBA16:
    case GL_RGB16:
        return 110;
    case GL_RGB10_A2:
    case GL_RGB10:
        return 90;
    case GL_R11F_G11F_B10F:
        return 115;
    case GL_RED:
    case GL_R8:
        return f32 ? 55 : f16 ? 25 : 10;
    case GL_R16F:
        return 25;
    case GL_R32F:
        return 55;
    case GL_R16:
        return 20;
    case GL_RG:
    case GL_RG8:
        return f32 ? 105 : f16 ? 65 : 30;
    case GL_RG16F:
        return 65;
    case GL_RG32F:
        return 105;
    case GL_RG16:
        return 60;
    case 1:
    case GL_LUMINANCE:
    case GL_LUMINANCE8:
        *swizzle = SWIZZLE(SW_R, SW_R, SW_R, SW_ONE);
        return 10;
    case GL_ALPHA:
    case GL_ALPHA8:
        *swizzle = SWIZZLE(SW_ZERO, SW_ZERO, SW_ZERO, SW_R);
        return 10;
    case 2:
    case GL_LUMINANCE_ALPHA:
    case GL_LUMINANCE8_ALPHA8:
        *swizzle = SWIZZLE(SW_R, SW_R, SW_R, SW_G);
        return 30;
    case GL_INTENSITY:
    case GL_INTENSITY8:
        *swizzle = SWIZZLE(SW_R, SW_R, SW_R, SW_R);
        return 10;
    case GL_R32UI:
        return 53;
    case GL_RGBA8UI:
        return 73;
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32:
    case GL_DEPTH_COMPONENT32F:
        return 252;
    case GL_DEPTH_STENCIL:
    case GL_DEPTH24_STENCIL8:
    case GL_DEPTH32F_STENCIL8:
        return 260;
    case GL_STENCIL_INDEX8:
        return 253;
    case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        *compressed = TRUE;
        return 130;
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        *compressed = TRUE;
        return 132;
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        *compressed = TRUE;
        return 134;
    case GL_COMPRESSED_RGB:
    case GL_COMPRESSED_RGBA:
        return 70;
    default:
        return 0;
    }
}

static uint32_t block_bytes(uint32_t mtl)
{
    return mtl == 130 ? 8 : 16;
}

static uint32_t full_levels(uint32_t w, uint32_t h, uint32_t d)
{
    uint32_t m = w > h ? w : h;
    if (d > m)
        m = d;
    uint32_t n = 1;
    while (m > 1) {
        m >>= 1;
        n++;
    }
    return n;
}

/* ---------------------------------------------------------------- objects */

GLAPI void APIENTRY glGenTextures(GLsizei n, GLuint *textures)
{
    GL_CONTEXT_OR_RETURN();
    for (GLsizei i = 0; i < n; i++)
        textures[i] = gl_name_new(&ctx->shared->textures);
}

static GlTexture *make(GlContext *ctx, GLuint name, GLenum target)
{
    GlTexture *t = gl_texture(ctx, name);
    if (t)
        return t;
    t = gl_alloc(sizeof(GlTexture));
    if (!t)
        return nullptr;
    t->name = name;
    t->target = target;
    t->sampler.min_filter = target == GL_TEXTURE_RECTANGLE ? GL_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    t->sampler.mag_filter = GL_LINEAR;
    t->sampler.wrap_s = t->sampler.wrap_t = t->sampler.wrap_r =
        target == GL_TEXTURE_RECTANGLE ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    t->sampler.min_lod = -1000;
    t->sampler.max_lod = 1000;
    t->sampler.compare_func = GL_LEQUAL;
    t->sampler.max_anisotropy = 1;
    t->max_level = 1000;
    t->component_swizzle[0] = GL_RED;
    t->component_swizzle[1] = GL_GREEN;
    t->component_swizzle[2] = GL_BLUE;
    t->component_swizzle[3] = GL_ALPHA;
    gl_name_set(&ctx->shared->textures, name, t);
    return t;
}

GLAPI void APIENTRY glBindTexture(GLenum target, GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    int s = slot_of(target);
    if (s < 0) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (name) {
        GlTexture *t = make(ctx, name, target);
        if (!t) {
            gl_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
        if (t->target != target) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return;
        }
    }
    if (ctx->units[ctx->active_texture][s] != name) {
        ctx->units[ctx->active_texture][s] = name;
        ctx->dirty |= DIRTY_TEXTURES;
    }
}

GLAPI void APIENTRY glActiveTexture(GLenum texture)
{
    GL_CONTEXT_OR_RETURN();
    if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + 32) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->active_texture = texture - GL_TEXTURE0;
}

GLAPI GLboolean APIENTRY glIsTexture(GLuint name)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    return gl_texture(ctx, name) ? GL_TRUE : GL_FALSE;
}

static void destroy_host(GlTexture *t)
{
    if (!t->id)
        return;
    gl_device_lock();
    MimeGpuObject *o = gl_emit(MIME_GPU_TEXTURE_DESTROY, sizeof(MimeGpuObject));
    o->id = t->id;
    gl_device_unlock();
    gl_free_id(ID_TEXTURE, t->id);
    t->id = 0;
}

GLAPI void APIENTRY glDeleteTextures(GLsizei n, const GLuint *textures)
{
    GL_CONTEXT_OR_RETURN();
    for (GLsizei i = 0; i < n; i++) {
        GLuint name = textures[i];
        void *o = gl_name_get(&ctx->shared->textures, name);
        if (!o)
            continue;
        if (o != GL_RESERVED) {
            destroy_host(o);
            gl_free(o);
        }
        gl_name_set(&ctx->shared->textures, name, nullptr);
        for (int u = 0; u < 32; u++)
            for (int s = 0; s < SLOT_COUNT; s++)
                if (ctx->units[u][s] == name)
                    ctx->units[u][s] = 0;
    }
    ctx->dirty |= DIRTY_TEXTURES;
}

static void create_host(GlTexture *t, uint32_t levels)
{
    BOOL old = t->id != 0;
    uint32_t id = gl_new_id(ID_TEXTURE);
    gl_device_lock();
    MimeGpuTextureCreate *c = gl_emit(MIME_GPU_TEXTURE_CREATE, sizeof(MimeGpuTextureCreate));
    c->id = id;
    c->type = t->mtl_type;
    c->format = t->mtl_format;
    c->width = t->width;
    c->height = t->height;
    c->depth = t->depth;
    c->levels = levels;
    c->samples = t->samples ? t->samples : 1;
    c->usage = MIME_GPU_USAGE_SAMPLED | (t->compressed ? 0 : MIME_GPU_USAGE_TARGET);
    c->swizzle = sampling_swizzle(t);
    if (old) {
        /* the levels it had move to the new texture. */
        uint32_t slices = t->mtl_type == MTL_TEX_CUBE ? 6
                          : t->mtl_type == MTL_TEX_2D_ARRAY || t->mtl_type == MTL_TEX_CUBE_ARRAY ? t->depth : 1;
        for (uint32_t l = 0; l < t->levels && l < levels; l++) {
            if (!(t->defined_levels & (1u << l)))
                continue;
            for (uint32_t s = 0; s < slices; s++) {
                MimeGpuBlit *b = gl_emit(MIME_GPU_BLIT, sizeof(MimeGpuBlit));
                b->src.id = t->id;
                b->src.level = l;
                b->src.slice = s;
                b->dst.id = id;
                b->dst.level = l;
                b->dst.slice = s;
                b->src_x1 = b->dst_x1 = (int32_t)(t->width >> l ? t->width >> l : 1);
                b->src_y1 = b->dst_y1 = (int32_t)(t->height >> l ? t->height >> l : 1);
                b->mask = MIME_GPU_CLEAR_COLOR;
            }
        }
    }
    gl_device_unlock();
    destroy_host(t);
    t->id = id;
    t->levels = levels;
}

/* makes the host texture fit an image of level; returns FALSE on a bad size. */
static BOOL define(GlContext *ctx, GlTexture *t, GLint level, GLenum internal, GLenum format, GLenum type, uint32_t w,
                   uint32_t h, uint32_t d)
{
    if (level < 0 || level > 15)
        return FALSE;
    uint32_t swizzle;
    BOOL compressed;
    uint32_t mtl = gl_mtl_format(internal, format, type, &swizzle, &compressed);
    if (format && gl_pixel_transfer_error(mtl, format, type)) {
        gl_error(ctx, gl_pixel_transfer_error(mtl, format, type));
        return FALSE;
    }
    if (!mtl) {
        gl_log("unsupported internal format %#x", internal);
        mtl = 70;
    }
    uint32_t bw = level ? w << level : w, bh = level ? h << level : h, bd = level && t->target == GL_TEXTURE_3D ? d << level : d;
    if (t->target == GL_TEXTURE_1D)
        bh = 1;
    BOOL same = t->id && t->mtl_format == mtl && t->internal_format == internal;
    if (level == 0 && (!same || t->width != w || t->height != h || t->depth != d)) {
        t->mtl_format = mtl;
        t->mtl_type = mtl_type_of(t->target);
        t->internal_format = internal;
        t->swizzle = swizzle;
        t->compressed = compressed;
        t->width = w ? w : 1;
        t->height = h ? h : 1;
        t->depth = d ? d : 1;
        t->defined_levels = 0;
        uint32_t levels = t->levels > 1 && t->id ? full_levels(t->width, t->height, t->mtl_type == MTL_TEX_3D ? t->depth : 1) : 1;
        destroy_host(t);
        create_host(t, levels);
    } else if (level > 0 && (!t->id || level >= (GLint)t->levels)) {
        if (!t->id) {
            t->mtl_format = mtl;
            t->mtl_type = mtl_type_of(t->target);
            t->internal_format = internal;
            t->swizzle = swizzle;
            t->compressed = compressed;
            t->width = bw;
            t->height = bh;
            t->depth = bd;
        }
        create_host(t, full_levels(t->width, t->height, t->mtl_type == MTL_TEX_3D ? t->depth : 1));
    }
    t->defined_levels |= (uint16_t)(1u << level);
    t->sampler_id = 0;
    ctx->dirty |= DIRTY_TEXTURES;
    return TRUE;
}

/* ---------------------------------------------------------------- uploads */

static const uint8_t *unpack_source(GlContext *ctx, const void *pixels)
{
    if (ctx->pixel_unpack) {
        GlBuffer *b = gl_buffer(ctx, ctx->pixel_unpack);
        return b && b->mem.ptr ? b->mem.ptr + (ULONG_PTR)pixels : nullptr;
    }
    return pixels;
}

/* everything recorded so far happens before the copies that follow. */
static void outside_pass(GlContext *ctx)
{
    gl_device_lock();
    gl_flush_clears(ctx);
    gl_end_pass(ctx);
    gl_device_unlock();
}

static void upload(GlContext *ctx, GlTexture *t, GLint level, uint32_t slice, int32_t x, int32_t y, int32_t z,
                   uint32_t w, uint32_t h, uint32_t d, GLenum format, GLenum type, const void *pixels)
{
    GLenum error = gl_pixel_transfer_error(t->mtl_format, format, type);
    if (error) {
        gl_error(ctx, error);
        return;
    }
    const uint8_t *src = unpack_source(ctx, pixels);
    if (!src || !w || !h || !d || !t->id)
        return;
    uint32_t bpp = gl_format_bytes(t->mtl_format);
    uint32_t row = w * bpp, image = row * h;
    uint64_t total = (uint64_t)image * d;
    GlMem mem;
    BOOL temp = !gl_stream_alloc(total, 256, &mem);
    if (temp && !gl_heap_alloc(total, &mem)) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    uint32_t sbpp = gl_client_pixel_bytes(format, type);
    uint32_t row_len = ctx->unpack_row_length ? (uint32_t)ctx->unpack_row_length : w;
    uint32_t align = ctx->unpack_alignment ? (uint32_t)ctx->unpack_alignment : 1;
    uint64_t stride = ((uint64_t)row_len * sbpp + align - 1) / align * align;
    uint32_t image_rows = ctx->unpack_image_height ? (uint32_t)ctx->unpack_image_height : h;
    src += (uint64_t)ctx->unpack_skip_images * image_rows * stride + (uint64_t)ctx->unpack_skip_rows * stride +
           (uint64_t)ctx->unpack_skip_pixels * sbpp;
    for (uint32_t k = 0; k < d; k++)
        for (uint32_t r = 0; r < h; r++)
            gl_unpack_row(t->mtl_format, mem.ptr + (uint64_t)k * image + (uint64_t)r * row, format, type,
                          src + (uint64_t)k * image_rows * stride + (uint64_t)r * stride, w);
    outside_pass(ctx);
    gl_device_lock();
    MimeGpuTextureCopy *c = gl_emit(MIME_GPU_TEXTURE_UPLOAD, sizeof(MimeGpuTextureCopy));
    c->id = t->id;
    c->level = (uint32_t)level;
    c->slice = slice;
    c->x = (uint32_t)x;
    c->y = (uint32_t)y;
    c->z = (uint32_t)z;
    c->width = w;
    c->height = h;
    c->depth = d;
    c->mem = gl_mem(&mem);
    c->row_bytes = row;
    c->image_bytes = image;
    if (temp)
        gl_heap_free(&mem, gl_batch_fence());
    gl_device_unlock();
}

static void upload_compressed(GlContext *ctx, GlTexture *t, GLint level, uint32_t slice, int32_t x, int32_t y,
                              uint32_t w, uint32_t h, GLsizei size, const void *data)
{
    const uint8_t *src = unpack_source(ctx, data);
    if (!src || !t->id || size <= 0)
        return;
    uint32_t bx = (w + 3) / 4, by = (h + 3) / 4, row = bx * block_bytes(t->mtl_format);
    uint64_t total = (uint64_t)row * by;
    if ((uint64_t)size < total)
        return;
    GlMem mem;
    BOOL temp = !gl_stream_alloc(total, 256, &mem);
    if (temp && !gl_heap_alloc(total, &mem))
        return;
    memcpy(mem.ptr, src, (SIZE_T)total);
    outside_pass(ctx);
    gl_device_lock();
    MimeGpuTextureCopy *c = gl_emit(MIME_GPU_TEXTURE_UPLOAD, sizeof(MimeGpuTextureCopy));
    c->id = t->id;
    c->level = (uint32_t)level;
    c->slice = slice;
    c->x = (uint32_t)x;
    c->y = (uint32_t)y;
    c->width = bx * 4 > (t->width >> level) ? w : bx * 4;
    c->height = by * 4 > (t->height >> level) ? h : by * 4;
    c->depth = 1;
    c->mem = gl_mem(&mem);
    c->row_bytes = row;
    c->image_bytes = (uint32_t)total;
    if (temp)
        gl_heap_free(&mem, gl_batch_fence());
    gl_device_unlock();
}

static BOOL face_of(GLenum target, uint32_t *slice)
{
    *slice = 0;
    if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) {
        *slice = target - GL_TEXTURE_CUBE_MAP_POSITIVE_X;
        return TRUE;
    }
    return FALSE;
}

/* builds a texture's mip chain on the host. */
static void generate_mipmaps(GlContext *ctx, GlTexture *t)
{
    uint32_t want = full_levels(t->width, t->height, t->mtl_type == MTL_TEX_3D ? t->depth : 1);
    if (t->levels < want)
        create_host(t, want);
    t->defined_levels = (uint16_t)((1u << want) - 1);
    outside_pass(ctx);
    gl_device_lock();
    MimeGpuObject *o = gl_emit(MIME_GPU_TEXTURE_MIPMAPS, sizeof(MimeGpuObject));
    o->id = t->id;
    gl_device_unlock();
    t->sampler_id = 0;
}

GLAPI void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,
                                 GLenum format, GLenum type, const void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t || w < 0 || h < 0) {
        gl_error(ctx, t ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }
    uint32_t slice;
    face_of(target, &slice);
    if (target == GL_PROXY_TEXTURE_2D)
        return;
    if (!define(ctx, t, level, (GLenum)internal, format, type, (uint32_t)w, (uint32_t)h, 1)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (pixels || ctx->pixel_unpack)
        upload(ctx, t, level, slice, 0, 0, 0, (uint32_t)w, (uint32_t)h, 1, format, type, pixels);
    if (t->generate_mipmap && level == 0 && t->id && !t->compressed)
        generate_mipmaps(ctx, t);
}

GLAPI void APIENTRY glTexImage1D(GLenum target, GLint level, GLint internal, GLsizei w, GLint border, GLenum format,
                                 GLenum type, const void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t || !define(ctx, t, level, (GLenum)internal, format, type, (uint32_t)w, 1, 1))
        return;
    if (pixels || ctx->pixel_unpack)
        upload(ctx, t, level, 0, 0, 0, 0, (uint32_t)w, 1, 1, format, type, pixels);
}

GLAPI void APIENTRY glTexImage3D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d,
                                 GLint border, GLenum format, GLenum type, const void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (t && t->target == GL_TEXTURE_CUBE_MAP_ARRAY && (d % 6 || w != h)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!t || !define(ctx, t, level, (GLenum)internal, format, type, (uint32_t)w, (uint32_t)h, (uint32_t)d))
        return;
    if (!pixels && !ctx->pixel_unpack)
        return;
    if (t->target == GL_TEXTURE_3D) {
        upload(ctx, t, level, 0, 0, 0, 0, (uint32_t)w, (uint32_t)h, (uint32_t)d, format, type, pixels);
        return;
    }
    /* array layers are slices, one upload each. */
    uint32_t layer = (uint32_t)h * (uint32_t)(((uint64_t)w * gl_client_pixel_bytes(format, type) + ctx->unpack_alignment - 1) /
                                              ctx->unpack_alignment * ctx->unpack_alignment);
    for (GLsizei z = 0; z < d; z++)
        upload(ctx, t, level, (uint32_t)z, 0, 0, 0, (uint32_t)w, (uint32_t)h, 1, format, type,
               (const uint8_t *)pixels + (uint64_t)z * layer);
}

GLAPI void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                                    GLenum type, const void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t || !t->id || x < 0 || y < 0 || w < 0 || h < 0) {
        gl_error(ctx, t ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }
    uint32_t slice;
    face_of(target, &slice);
    upload(ctx, t, level, slice, x, y, 0, (uint32_t)w, (uint32_t)h, 1, format, type, pixels);
    if (t->generate_mipmap && level == 0 && !t->compressed)
        generate_mipmaps(ctx, t);
}

GLAPI void APIENTRY glTexSubImage1D(GLenum target, GLint level, GLint x, GLsizei w, GLenum format, GLenum type,
                                    const void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (t && t->id)
        upload(ctx, t, level, 0, x, 0, 0, (uint32_t)w, 1, 1, format, type, pixels);
}

GLAPI void APIENTRY glTexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h,
                                    GLsizei d, GLenum format, GLenum type, const void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t || !t->id)
        return;
    if (t->target == GL_TEXTURE_3D) {
        upload(ctx, t, level, 0, x, y, z, (uint32_t)w, (uint32_t)h, (uint32_t)d, format, type, pixels);
        return;
    }
    uint32_t layer = (uint32_t)h * (uint32_t)(((uint64_t)w * gl_client_pixel_bytes(format, type) + ctx->unpack_alignment - 1) /
                                              ctx->unpack_alignment * ctx->unpack_alignment);
    for (GLsizei k = 0; k < d; k++)
        upload(ctx, t, level, (uint32_t)(z + k), x, y, 0, (uint32_t)w, (uint32_t)h, 1, format, type,
               (const uint8_t *)pixels + (uint64_t)k * layer);
}

GLAPI void APIENTRY glCompressedTexImage2D(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h,
                                           GLint border, GLsizei size, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t || !define(ctx, t, level, internal, 0, 0, (uint32_t)w, (uint32_t)h, 1)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    uint32_t slice;
    face_of(target, &slice);
    if (t->compressed)
        upload_compressed(ctx, t, level, slice, 0, 0, (uint32_t)w, (uint32_t)h, size, data);
}

GLAPI void APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                              GLenum format, GLsizei size, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    uint32_t slice;
    face_of(target, &slice);
    if (t && t->compressed)
        upload_compressed(ctx, t, level, slice, x, y, (uint32_t)w, (uint32_t)h, size, data);
}

GLAPI void APIENTRY glTexStorage2D(GLenum target, GLsizei levels, GLenum internal, GLsizei w, GLsizei h)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t || levels < 1) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    uint32_t swizzle;
    BOOL compressed;
    destroy_host(t);
    t->mtl_format = gl_mtl_format(internal, GL_RGBA, GL_UNSIGNED_BYTE, &swizzle, &compressed);
    if (!t->mtl_format)
        t->mtl_format = 70;
    t->mtl_type = mtl_type_of(t->target);
    t->internal_format = internal;
    t->swizzle = swizzle;
    t->compressed = compressed;
    t->width = (uint32_t)w;
    t->height = (uint32_t)h;
    t->depth = t->target == GL_TEXTURE_CUBE_MAP ? 6 : 1;
    create_host(t, (uint32_t)levels);
    t->defined_levels = (uint16_t)((1u << levels) - 1);
    t->sampler_id = 0;
    ctx->dirty |= DIRTY_TEXTURES;
}

GLAPI void APIENTRY glTexStorage3D(GLenum target, GLsizei levels, GLenum internal, GLsizei w, GLsizei h, GLsizei d)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t)
        return;
    if (t->target == GL_TEXTURE_CUBE_MAP_ARRAY && (d % 6 || w != h)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    uint32_t swizzle;
    BOOL compressed;
    destroy_host(t);
    t->mtl_format = gl_mtl_format(internal, GL_RGBA, GL_UNSIGNED_BYTE, &swizzle, &compressed);
    t->mtl_type = mtl_type_of(t->target);
    t->internal_format = internal;
    t->swizzle = swizzle;
    t->width = (uint32_t)w;
    t->height = (uint32_t)h;
    t->depth = (uint32_t)d;
    create_host(t, (uint32_t)levels);
    t->defined_levels = (uint16_t)((1u << levels) - 1);
    ctx->dirty |= DIRTY_TEXTURES;
}

GLAPI void APIENTRY glTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internal, GLsizei w, GLsizei h,
                                            GLboolean fixed)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t)
        return;
    uint32_t swizzle;
    BOOL compressed;
    destroy_host(t);
    t->mtl_format = gl_mtl_format(internal, GL_RGBA, GL_UNSIGNED_BYTE, &swizzle, &compressed);
    t->mtl_type = MTL_TEX_2D_MS;
    t->internal_format = internal;
    t->width = (uint32_t)w;
    t->height = (uint32_t)h;
    t->depth = 1;
    t->samples = samples > 4 ? 4 : samples < 1 ? 1 : (uint32_t)samples;
    create_host(t, 1);
    t->defined_levels = 1;
}

GLAPI void APIENTRY glGenerateMipmap(GLenum target)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t || !t->id || t->compressed)
        return;
    generate_mipmaps(ctx, t);
}

/* copies from the read framebuffer, through the host's blit. */
static void copy_from_framebuffer(GlContext *ctx, GlTexture *t, GLint level, uint32_t slice, GLint dx, GLint dy,
                                  GLint x, GLint y, GLsizei w, GLsizei h)
{
    MimeGpuAttachment src;
    uint32_t format, sw, sh;
    if (!gl_read_source(ctx, &src, &format, &sw, &sh) || !t->id || w <= 0 || h <= 0)
        return;
    outside_pass(ctx);
    gl_device_lock();
    MimeGpuBlit *b = gl_emit(MIME_GPU_BLIT, sizeof(MimeGpuBlit));
    b->src = src;
    b->dst.id = t->id;
    b->dst.level = (uint32_t)level;
    b->dst.slice = slice;
    b->src_x0 = x;
    b->src_y0 = y;
    b->src_x1 = x + w;
    b->src_y1 = y + h;
    b->dst_x0 = dx;
    b->dst_y0 = dy;
    b->dst_x1 = dx + w;
    b->dst_y1 = dy + h;
    b->mask = MIME_GPU_CLEAR_COLOR;
    gl_device_unlock();
}

GLAPI void APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internal, GLint x, GLint y, GLsizei w,
                                     GLsizei h, GLint border)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t || !define(ctx, t, level, internal, GL_RGBA, GL_UNSIGNED_BYTE, (uint32_t)w, (uint32_t)h, 1))
        return;
    uint32_t slice;
    face_of(target, &slice);
    copy_from_framebuffer(ctx, t, level, slice, 0, 0, x, y, w, h);
}

GLAPI void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint dx, GLint dy, GLint x, GLint y, GLsizei w,
                                        GLsizei h)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    uint32_t slice;
    face_of(target, &slice);
    if (t)
        copy_from_framebuffer(ctx, t, level, slice, dx, dy, x, y, w, h);
}

/* ---------------------------------------------------------------- parameters */

static void tex_parameter(GlContext *ctx, GlTexture *t, GLenum pname, const GLfloat *v)
{
    GlSamplerState *s = &t->sampler;
    GLenum e = (GLenum)v[0];
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
        s->min_filter = e;
        break;
    case GL_TEXTURE_MAG_FILTER:
        s->mag_filter = e;
        break;
    case GL_TEXTURE_WRAP_S:
        s->wrap_s = e;
        break;
    case GL_TEXTURE_WRAP_T:
        s->wrap_t = e;
        break;
    case GL_TEXTURE_WRAP_R:
        s->wrap_r = e;
        break;
    case GL_TEXTURE_MIN_LOD:
        s->min_lod = v[0];
        break;
    case GL_TEXTURE_MAX_LOD:
        s->max_lod = v[0];
        break;
    case GL_TEXTURE_LOD_BIAS:
        s->lod_bias = v[0];
        break;
    case GL_TEXTURE_BASE_LEVEL:
        t->base_level = (GLint)v[0];
        break;
    case GL_TEXTURE_MAX_LEVEL:
        t->max_level = (GLint)v[0];
        break;
    case GL_TEXTURE_COMPARE_MODE:
        s->compare_mode = e;
        break;
    case GL_TEXTURE_COMPARE_FUNC:
        s->compare_func = e;
        break;
    case GL_TEXTURE_MAX_ANISOTROPY_EXT:
        s->max_anisotropy = v[0];
        break;
    case GL_TEXTURE_BORDER_COLOR:
        memcpy(s->border, v, sizeof(s->border));
        break;
    case GL_GENERATE_MIPMAP:
        t->generate_mipmap = v[0] != 0;
        break;
    case GL_DEPTH_TEXTURE_MODE:
    case GL_TEXTURE_PRIORITY:
        break;
    case GL_TEXTURE_SWIZZLE_R:
    case GL_TEXTURE_SWIZZLE_G:
    case GL_TEXTURE_SWIZZLE_B:
    case GL_TEXTURE_SWIZZLE_A:
    case GL_TEXTURE_SWIZZLE_RGBA: {
        unsigned first = pname == GL_TEXTURE_SWIZZLE_RGBA ? 0 : pname - GL_TEXTURE_SWIZZLE_R;
        unsigned count = pname == GL_TEXTURE_SWIZZLE_RGBA ? 4 : 1;
        for (unsigned i = 0; i < count; i++) {
            if (swizzle_component((GLenum)v[i]) < 0) {
                gl_error(ctx, GL_INVALID_ENUM);
                return;
            }
        }
        for (unsigned i = 0; i < count; i++)
            t->component_swizzle[first + i] = (GLenum)v[i];
        if (t->id) {
            gl_device_lock();
            MimeGpuTextureSwizzle *c = gl_emit(MIME_GPU_TEXTURE_SWIZZLE, sizeof(MimeGpuTextureSwizzle));
            c->id = t->id;
            c->swizzle = sampling_swizzle(t);
            gl_device_unlock();
        }
        break;
    }
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    t->sampler_id = 0;
    ctx->dirty |= DIRTY_TEXTURES;
}

GLAPI void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    GL_CONTEXT_OR_RETURN();
    if (pname == GL_TEXTURE_SWIZZLE_RGBA || pname == GL_TEXTURE_BORDER_COLOR) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlTexture *t = bound(ctx, target);
    GLfloat v[4] = {param};
    if (t)
        tex_parameter(ctx, t, pname, v);
    else
        gl_error(ctx, GL_INVALID_OPERATION);
}

GLAPI void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    glTexParameterf(target, pname, (GLfloat)param);
}

GLAPI void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    GLfloat v[4] = {params[0]};
    if (pname == GL_TEXTURE_BORDER_COLOR || pname == GL_TEXTURE_SWIZZLE_RGBA)
        memcpy(v, params, sizeof(v));
    if (t)
        tex_parameter(ctx, t, pname, v);
}

GLAPI void APIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
    GLfloat v[4] = {(GLfloat)params[0]};
    if (pname == GL_TEXTURE_BORDER_COLOR)
        for (int i = 0; i < 4; i++)
            v[i] = (GLfloat)params[i] / 2147483647.0f;
    if (pname == GL_TEXTURE_SWIZZLE_RGBA)
        for (int i = 0; i < 4; i++)
            v[i] = (GLfloat)params[i];
    glTexParameterfv(target, pname, v);
}

GLAPI void APIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    switch (pname) {
    case GL_TEXTURE_SWIZZLE_R:
    case GL_TEXTURE_SWIZZLE_G:
    case GL_TEXTURE_SWIZZLE_B:
    case GL_TEXTURE_SWIZZLE_A:
        *params = (GLint)t->component_swizzle[pname - GL_TEXTURE_SWIZZLE_R];
        break;
    case GL_TEXTURE_SWIZZLE_RGBA:
        for (int i = 0; i < 4; i++)
            params[i] = (GLint)t->component_swizzle[i];
        break;
    case GL_TEXTURE_MIN_FILTER:
        *params = (GLint)t->sampler.min_filter;
        break;
    case GL_TEXTURE_MAG_FILTER:
        *params = (GLint)t->sampler.mag_filter;
        break;
    case GL_TEXTURE_WRAP_S:
        *params = (GLint)t->sampler.wrap_s;
        break;
    case GL_TEXTURE_WRAP_T:
        *params = (GLint)t->sampler.wrap_t;
        break;
    case GL_TEXTURE_WRAP_R:
        *params = (GLint)t->sampler.wrap_r;
        break;
    case GL_TEXTURE_BASE_LEVEL:
        *params = t->base_level;
        break;
    case GL_TEXTURE_MAX_LEVEL:
        *params = t->max_level;
        break;
    case GL_TEXTURE_COMPARE_MODE:
        *params = (GLint)t->sampler.compare_mode;
        break;
    case GL_TEXTURE_COMPARE_FUNC:
        *params = (GLint)t->sampler.compare_func;
        break;
    case GL_TEXTURE_IMMUTABLE_FORMAT:
        *params = 0;
        break;
    case GL_GENERATE_MIPMAP:
        *params = t->generate_mipmap;
        break;
    default:
        *params = 0;
        break;
    }
}

GLAPI void APIENTRY glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
    GLint v[4] = {};
    glGetTexParameteriv(target, pname, v);
    int count = pname == GL_TEXTURE_SWIZZLE_RGBA ? 4 : 1;
    for (int i = 0; i < count; i++)
        params[i] = (GLfloat)v[i];
}

GLAPI void APIENTRY glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t || level < 0 || level > 15) {
        *params = 0;
        return;
    }
    BOOL defined = (t->defined_levels >> level) & 1;
    uint32_t w = t->width >> level ? t->width >> level : 1, h = t->height >> level ? t->height >> level : 1;
    switch (pname) {
    case GL_TEXTURE_WIDTH:
        *params = defined ? (GLint)w : 0;
        break;
    case GL_TEXTURE_HEIGHT:
        *params = defined ? (GLint)h : 0;
        break;
    case GL_TEXTURE_DEPTH:
        *params = defined ? (GLint)t->depth : 0;
        break;
    case GL_TEXTURE_INTERNAL_FORMAT:
        *params = defined ? (GLint)t->internal_format : GL_RGBA;
        break;
    case GL_TEXTURE_COMPRESSED:
        *params = t->compressed;
        break;
    case GL_TEXTURE_SAMPLES:
        *params = (GLint)t->samples;
        break;
    default:
        *params = defined ? 8 : 0;
        break;
    }
}

GLAPI void APIENTRY glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params)
{
    GLint v = 0;
    glGetTexLevelParameteriv(target, level, pname, &v);
    *params = (GLfloat)v;
}

/* ---------------------------------------------------------------- samplers */

static uint8_t address_mode(GLenum wrap)
{
    switch (wrap) {
    case GL_REPEAT:
        return 2;
    case GL_MIRRORED_REPEAT:
        return 3;
    case GL_CLAMP_TO_BORDER:
        return 5;
    case GL_MIRROR_CLAMP_TO_EDGE:
        return 1;
    default:
        return 0;
    }
}

static uint8_t compare_function(GLenum func)
{
    switch (func) {
    case GL_NEVER:
        return 0;
    case GL_LESS:
        return 1;
    case GL_EQUAL:
        return 2;
    case GL_LEQUAL:
        return 3;
    case GL_GREATER:
        return 4;
    case GL_NOTEQUAL:
        return 5;
    case GL_GEQUAL:
        return 6;
    default:
        return 7;
    }
}

/* the host sampler state of a texture's parameters. */
uint32_t gl_texture_sampler(GlTexture *t)
{
    if (t->sampler_id)
        return t->sampler_id;
    GlSamplerState *s = &t->sampler;
    MimeGpuSamplerCreate d;
    memset(&d, 0, sizeof(d));
    BOOL mips = t->levels > 1;
    switch (s->min_filter) {
    case GL_NEAREST:
        d.min_filter = 0;
        break;
    case GL_LINEAR:
        d.min_filter = 1;
        break;
    case GL_NEAREST_MIPMAP_NEAREST:
        d.min_filter = 0;
        d.mip_filter = mips ? 1 : 0;
        break;
    case GL_LINEAR_MIPMAP_NEAREST:
        d.min_filter = 1;
        d.mip_filter = mips ? 1 : 0;
        break;
    case GL_NEAREST_MIPMAP_LINEAR:
        d.min_filter = 0;
        d.mip_filter = mips ? 2 : 0;
        break;
    default:
        d.min_filter = 1;
        d.mip_filter = mips ? 2 : 0;
        break;
    }
    d.mag_filter = s->mag_filter == GL_NEAREST ? 0 : 1;
    d.address_s = address_mode(s->wrap_s);
    d.address_t = address_mode(s->wrap_t);
    d.address_r = address_mode(s->wrap_r);
    if (s->compare_mode == GL_COMPARE_REF_TO_TEXTURE)
        d.compare = compare_function(s->compare_func);
    d.border = s->border[3] == 0 ? 0 : s->border[0] > 0.5f ? 2 : 1;
    d.max_anisotropy = (uint8_t)(s->max_anisotropy < 1 ? 1 : s->max_anisotropy > 16 ? 16 : s->max_anisotropy);
    if (d.max_anisotropy > 1 && !d.mip_filter)
        d.max_anisotropy = 1;
    d.lod_min = s->min_lod < (GLfloat)t->base_level ? (GLfloat)t->base_level : s->min_lod;
    if (d.lod_min < 0)
        d.lod_min = 0;
    d.lod_max = s->max_lod > (GLfloat)t->max_level ? (GLfloat)t->max_level : s->max_lod;
    if (d.lod_max < d.lod_min)
        d.lod_max = d.lod_min;
    BOOL created;
    uint32_t id = (uint32_t)gl_cache_lookup(ID_SAMPLER, (const uint8_t *)&d + sizeof(MimeGpuCmd),
                                            sizeof(d) - sizeof(MimeGpuCmd), &created);
    if (created) {
        gl_device_lock();
        MimeGpuSamplerCreate *c = gl_emit(MIME_GPU_SAMPLER_CREATE, sizeof(MimeGpuSamplerCreate));
        memcpy((uint8_t *)c + sizeof(MimeGpuCmd), (uint8_t *)&d + sizeof(MimeGpuCmd), sizeof(d) - sizeof(MimeGpuCmd));
        c->id = id;
        gl_device_unlock();
    }
    t->sampler_id = id;
    return id;
}

BOOL gl_texture_ready(GlContext *ctx, GlTexture *t)
{
    return t && t->id && (t->defined_levels & 1);
}

/* a 1x1 black texture for samplers without a complete texture, as GL samples them. */
uint32_t gl_dummy_texture(GlContext *ctx)
{
    static uint32_t id;
    if (id)
        return id;
    id = gl_new_id(ID_TEXTURE);
    GlMem mem;
    if (!gl_stream_alloc(4, 256, &mem))
        return id;
    static const uint8_t black[4] = {0, 0, 0, 255};
    memcpy(mem.ptr, black, 4);
    gl_device_lock();
    MimeGpuTextureCreate *c = gl_emit(MIME_GPU_TEXTURE_CREATE, sizeof(MimeGpuTextureCreate));
    c->id = id;
    c->type = MTL_TEX_2D;
    c->format = 70;
    c->width = c->height = c->depth = c->levels = c->samples = 1;
    c->usage = MIME_GPU_USAGE_SAMPLED;
    c->swizzle = SWIZZLE(SW_R, SW_G, SW_B, SW_A);
    MimeGpuTextureCopy *u = gl_emit(MIME_GPU_TEXTURE_UPLOAD, sizeof(MimeGpuTextureCopy));
    u->id = id;
    u->width = u->height = u->depth = 1;
    u->mem = gl_mem(&mem);
    u->row_bytes = u->image_bytes = 4;
    gl_device_unlock();
    return id;
}

/* ---------------------------------------------------------------- readback */

BOOL gl_read_pixels_into(GlContext *ctx, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                         void *pixels)
{
    MimeGpuAttachment src;
    uint32_t mtl, sw, sh, flags = MIME_GPU_COPY_TEXTURE;
    if (format == GL_DEPTH_COMPONENT || format == GL_STENCIL_INDEX) {
        if (!gl_read_depth_source(ctx, format == GL_STENCIL_INDEX, &src, &mtl, &flags, &sw, &sh)) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return TRUE;
        }
    } else if (!gl_read_source(ctx, &src, &mtl, &sw, &sh)) {
        return FALSE;
    } else if (src.surface) {
        flags = MIME_GPU_COPY_SURFACE;
    }
    GLenum error = gl_pixel_transfer_error(mtl, format, type);
    if (error) {
        gl_error(ctx, error);
        return FALSE;
    }
    /* the part inside the source; the rest of the destination is left alone. */
    GLint x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    GLint x1 = x + w > (GLint)sw ? (GLint)sw : x + w, y1 = y + h > (GLint)sh ? (GLint)sh : y + h;
    if (x1 <= x0 || y1 <= y0)
        return TRUE;
    uint32_t cw = (uint32_t)(x1 - x0), ch = (uint32_t)(y1 - y0);
    uint32_t bpp = gl_format_bytes(mtl), row = cw * bpp;
    uint64_t total = (uint64_t)row * ch;
    GlMem mem;
    BOOL temp = !gl_stream_alloc(total, 256, &mem);
    if (temp && !gl_heap_alloc(total, &mem))
        return FALSE;
    outside_pass(ctx);
    gl_device_lock();
    MimeGpuTextureCopy *c = gl_emit(MIME_GPU_TEXTURE_READ, sizeof(MimeGpuTextureCopy));
    c->id = src.id;
    c->surface = flags;
    c->level = src.level;
    c->slice = src.slice;
    c->x = (uint32_t)x0;
    c->y = (uint32_t)y0;
    c->width = cw;
    c->height = ch;
    c->depth = 1;
    c->mem = gl_mem(&mem);
    c->row_bytes = row;
    c->image_bytes = (uint32_t)total;
    uint64_t fence = gl_fence();
    gl_device_unlock();
    gl_wait(fence);

    uint8_t *dst = pixels;
    if (ctx->pixel_pack) {
        GlBuffer *b = gl_buffer(ctx, ctx->pixel_pack);
        dst = b && b->mem.ptr ? b->mem.ptr + (ULONG_PTR)pixels : nullptr;
    }
    if (dst) {
        uint32_t dbpp = gl_client_pixel_bytes(format, type);
        uint32_t row_len = ctx->pack_row_length ? (uint32_t)ctx->pack_row_length : (uint32_t)w;
        uint32_t align = ctx->pack_alignment ? (uint32_t)ctx->pack_alignment : 1;
        uint64_t stride = ((uint64_t)row_len * dbpp + align - 1) / align * align;
        dst += (uint64_t)ctx->pack_skip_rows * stride + (uint64_t)ctx->pack_skip_pixels * dbpp;
        for (uint32_t r = 0; r < ch; r++)
            gl_pack_row(mtl, mem.ptr + (uint64_t)r * row, format, type,
                        dst + (uint64_t)(r + (uint32_t)(y0 - y)) * stride + (uint64_t)(x0 - x) * dbpp, cw);
    }
    if (temp)
        gl_heap_free(&mem, fence);
    return TRUE;
}

GLAPI void APIENTRY glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    if (w < 0 || h < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!gl_read_pixels_into(ctx, x, y, w, h, format, type, pixels))
        gl_error(ctx, GL_INVALID_FRAMEBUFFER_OPERATION);
}

GLAPI void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = bound(ctx, target);
    if (!t || !t->id)
        return;
    /* reads through a temporary framebuffer over the texture. */
    GLuint saved = ctx->read_fbo, fbo;
    extern void APIENTRY glGenFramebuffers(GLsizei, GLuint *);
    extern void APIENTRY glDeleteFramebuffers(GLsizei, const GLuint *);
    extern void APIENTRY glBindFramebuffer(GLenum, GLuint);
    extern void APIENTRY glFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, t->name, level);
    uint32_t w = t->width >> level ? t->width >> level : 1, h = t->height >> level ? t->height >> level : 1;
    gl_read_pixels_into(ctx, 0, 0, (GLsizei)w, (GLsizei)h, format, type, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, saved);
    glDeleteFramebuffers(1, &fbo);
}

/* ---------------------------------------------------------------- bindings and storage of OpenGL 4.4 and 4.5 */

void APIENTRY glTexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d,
                              GLenum format, GLenum type, const void *pixels);

GLAPI void APIENTRY glBindTextures(GLuint first, GLsizei count, const GLuint *textures)
{
    GL_CONTEXT_OR_RETURN();
    if (count < 0 || first + (GLuint)count > 32) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    for (GLsizei i = 0; textures && i < count; i++)
        if (textures[i] && !gl_texture(ctx, textures[i])) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return;
        }
    for (GLsizei i = 0; i < count; i++) {
        GLuint name = textures ? textures[i] : 0;
        GlTexture *t = gl_texture(ctx, name);
        GLuint *unit = ctx->units[first + (GLuint)i];
        if (!t) {
            for (int s = 0; s < SLOT_COUNT; s++)
                unit[s] = 0;
            continue;
        }
        int s = slot_of(t->target);
        if (s >= 0)
            unit[s] = name;
    }
    ctx->dirty |= DIRTY_TEXTURES;
}

GLAPI void APIENTRY glTexStorage2DMultisample(GLenum target, GLsizei samples, GLenum internal, GLsizei w, GLsizei h,
                                              GLboolean fixed)
{
    GL_CONTEXT_OR_RETURN();
    if (target != GL_TEXTURE_2D_MULTISAMPLE) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (w < 1 || h < 1 || samples < 1) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    glTexImage2DMultisample(target, samples, internal, w, h, fixed);
}

/*
 * fills a region of a level with one pixel through the upload path, with the pixel store of the call and the texture bound
 * on the way: the texture goes into the slot of its target on the active unit, and the old one is put back.
 */
GLAPI void APIENTRY glClearTexSubImage(GLuint texture, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h,
                                       GLsizei d, GLenum format, GLenum type, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = gl_texture(ctx, texture);
    if (!t || !t->id || level < 0 || (uint32_t)level >= t->levels || t->compressed) {
        gl_error(ctx, !t || !t->id || t->compressed ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }
    uint32_t pixel = gl_client_pixel_bytes(format, type);
    if (!pixel) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    uint32_t lw = t->width >> level ? t->width >> level : 1, lh = t->height >> level ? t->height >> level : 1;
    uint32_t ld = t->target == GL_TEXTURE_3D ? (t->depth >> level ? t->depth >> level : 1) : t->depth ? t->depth : 1;
    BOOL cube = t->target == GL_TEXTURE_CUBE_MAP;
    if (cube)
        ld = 6;
    if (x < 0 || y < 0 || z < 0 || w < 0 || h < 0 || d < 0 || (uint32_t)(x + w) > lw || (uint32_t)(y + h) > lh ||
        (uint32_t)(z + d) > ld) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!w || !h || !d)
        return;
    uint8_t *pixels = gl_alloc((SIZE_T)pixel * (SIZE_T)w * (SIZE_T)h);
    if (!pixels) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    if (data)
        for (SIZE_T i = 0; i < (SIZE_T)w * (SIZE_T)h; i++)
            memcpy(pixels + i * pixel, data, pixel);

    int slot = slot_of(t->target);
    GLuint old = slot >= 0 ? ctx->units[ctx->active_texture][slot] : 0;
    GLuint old_unpack = ctx->pixel_unpack;
    GLint store[7] = {ctx->unpack_alignment, ctx->unpack_row_length, ctx->unpack_skip_rows, ctx->unpack_skip_pixels,
                      ctx->unpack_image_height, ctx->unpack_skip_images, 0};
    ctx->units[ctx->active_texture][slot] = texture;
    ctx->pixel_unpack = 0;
    ctx->unpack_alignment = 1;
    ctx->unpack_row_length = ctx->unpack_skip_rows = ctx->unpack_skip_pixels = 0;
    ctx->unpack_image_height = ctx->unpack_skip_images = 0;
    switch (t->target) {
    case GL_TEXTURE_1D:
        glTexSubImage1D(GL_TEXTURE_1D, level, x, w, format, type, pixels);
        break;
    case GL_TEXTURE_CUBE_MAP:
        for (GLint face = z; face < z + d; face++)
            glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face, level, x, y, w, h, format, type, pixels);
        break;
    case GL_TEXTURE_3D:
    case GL_TEXTURE_2D_ARRAY:
        /* the same pixels for every slice: one call for each, as an array's slices sit apart. */
        for (GLint slice = z; slice < z + d; slice++)
            glTexSubImage3D(t->target, level, x, y, slice, w, h, 1, format, type, pixels);
        break;
    default:
        glTexSubImage2D(t->target, level, x, y, w, h, format, type, pixels);
        break;
    }
    ctx->unpack_alignment = store[0];
    ctx->unpack_row_length = store[1];
    ctx->unpack_skip_rows = store[2];
    ctx->unpack_skip_pixels = store[3];
    ctx->unpack_image_height = store[4];
    ctx->unpack_skip_images = store[5];
    ctx->pixel_unpack = old_unpack;
    ctx->units[ctx->active_texture][slot] = old;
    gl_free(pixels);
}

GLAPI void APIENTRY glClearTexImage(GLuint texture, GLint level, GLenum format, GLenum type, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = gl_texture(ctx, texture);
    if (!t || !t->id || level < 0 || (uint32_t)level >= t->levels) {
        gl_error(ctx, !t || !t->id ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }
    GLsizei w = (GLsizei)(t->width >> level ? t->width >> level : 1);
    GLsizei h = (GLsizei)(t->height >> level ? t->height >> level : 1);
    GLsizei d = t->target == GL_TEXTURE_CUBE_MAP ? 6
                : t->target == GL_TEXTURE_3D     ? (GLsizei)(t->depth >> level ? t->depth >> level : 1)
                                                 : (GLsizei)t->depth;
    glClearTexSubImage(texture, level, 0, 0, 0, w, h, d, format, type, data);
}
