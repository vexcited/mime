/*
 * glBitmap, glDrawPixels and glCopyPixels. a picture becomes a texture of its own, drawn as a rectangle of window coordinates
 * by the fixed function program, so every per fragment test and blend of the context applies to it as to any fragment. the
 * raster position they start at is in raster.c.
 */
#include "fixed.h"
#include "immediate.h"

enum { MTL_R8 = 10, MTL_RG8 = 30, MTL_RGBA8 = 70 };

static GLfloat round_f(GLfloat v)
{
    v += 0.5f;
    if (v > 1e9f || v < -1e9f)
        return v;
    int64_t i = (int64_t)v;
    return (GLfloat)(v < (GLfloat)i ? i - 1 : i);
}

typedef struct SavedState {
    BOOL cull_face, polygon_offset_fill;
    GLenum polygon_mode, shade_model;
    GLint viewport[4];
    GlProgram *program;
    GLfloat color[4];
} SavedState;

/* the unpack state of a picture is the program's, and the texture it is copied to wants the plain one. */
static void default_unpack(GlContext *ctx, GLint saved[8])
{
    saved[0] = ctx->unpack_alignment;
    saved[1] = ctx->unpack_row_length;
    saved[2] = ctx->unpack_skip_rows;
    saved[3] = ctx->unpack_skip_pixels;
    saved[4] = ctx->unpack_image_height;
    saved[5] = ctx->unpack_skip_images;
    saved[6] = (GLint)ctx->pixel_unpack;
    saved[7] = ctx->unpack_lsb_first | (ctx->unpack_swap_bytes << 1);
    ctx->unpack_alignment = 1;
    ctx->unpack_row_length = ctx->unpack_skip_rows = ctx->unpack_skip_pixels = 0;
    ctx->unpack_image_height = ctx->unpack_skip_images = 0;
    ctx->pixel_unpack = 0;
    ctx->unpack_lsb_first = ctx->unpack_swap_bytes = FALSE;
}

static void restore_unpack(GlContext *ctx, const GLint saved[8])
{
    ctx->unpack_alignment = saved[0];
    ctx->unpack_row_length = saved[1];
    ctx->unpack_skip_rows = saved[2];
    ctx->unpack_skip_pixels = saved[3];
    ctx->unpack_image_height = saved[4];
    ctx->unpack_skip_images = saved[5];
    ctx->pixel_unpack = (GLuint)saved[6];
    ctx->unpack_lsb_first = saved[7] & 1;
    ctx->unpack_swap_bytes = (saved[7] & 2) != 0;
}

/*
 * draws w by h rgba pixels with the lower left corner at x, y of the window, each one zoom wide and high, at the depth z, with
 * the fixed function program whatever program the context has. op says how the fragments get their color.
 */
static void draw_picture(GlContext *ctx, int op, GLfloat x, GLfloat y, GLfloat z, uint32_t w, uint32_t h, const uint8_t *rgba,
                         const GLfloat color[4], GLfloat zoom_x, GLfloat zoom_y)
{
    GlFixed *f = &ctx->ff;
    GlTarget target;
    const void *owner;
    uint32_t serial;
    gl_resolve_target(ctx, TRUE, &target, &owner, &serial);
    if (!target.width || !target.height)
        return;

    GLuint previous_unit = ctx->active_texture;
    glActiveTexture(GL_TEXTURE0);
    GLint previous_texture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint unpack[8];
    default_unpack(ctx, unpack);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    restore_unpack(ctx, unpack);

    SavedState s = {ctx->cull_face, ctx->polygon_offset_fill, ctx->polygon_mode, f->shade_model,
                    {ctx->viewport[0], ctx->viewport[1], ctx->viewport[2], ctx->viewport[3]}, ctx->current_program};
    memcpy(s.color, f->current[0], sizeof(s.color));
    ctx->current_program = nullptr;
    ctx->cull_face = FALSE;
    ctx->polygon_offset_fill = FALSE;
    ctx->polygon_mode = GL_FILL;
    f->shade_model = GL_SMOOTH;
    ctx->viewport[0] = ctx->viewport[1] = 0;
    ctx->viewport[2] = (GLint)target.width;
    ctx->viewport[3] = (GLint)target.height;
    ctx->dirty |= DIRTY_RASTER | DIRTY_VIEWPORT | DIRTY_PIPELINE | DIRTY_VERTEX | DIRTY_TEXTURES;

    memset(f->window_mvp, 0, sizeof(f->window_mvp));
    f->window_mvp[0] = 2.0f / (GLfloat)target.width;
    f->window_mvp[5] = 2.0f / (GLfloat)target.height;
    f->window_mvp[12] = f->window_mvp[13] = -1;
    f->window_mvp[15] = 1;
    double range = ctx->depth_range[1] - ctx->depth_range[0];
    if (range != 0) {
        f->window_mvp[10] = (GLfloat)(2 / range);
        f->window_mvp[14] = (GLfloat)(-2 * ctx->depth_range[0] / range - 1);
    }
    f->window_draw = TRUE;
    f->raster_op = op;
    f->dirty = TRUE;
    memcpy(f->current[0], color, 16);

    GLfloat x1 = x + (GLfloat)w * zoom_x, y1 = y + (GLfloat)h * zoom_y;
    f->in_begin = TRUE;
    f->begin_mode = GL_TRIANGLE_STRIP;
    f->vertex_count = 0;
    f->varying = 0;
    gl_ff_texcoord(ctx, 0, 0, 0, 0, 1);
    gl_ff_vertex(ctx, x, y, z, 1);
    gl_ff_texcoord(ctx, 0, 1, 0, 0, 1);
    gl_ff_vertex(ctx, x1, y, z, 1);
    gl_ff_texcoord(ctx, 0, 0, 1, 0, 1);
    gl_ff_vertex(ctx, x, y1, z, 1);
    gl_ff_texcoord(ctx, 0, 1, 1, 0, 1);
    gl_ff_vertex(ctx, x1, y1, z, 1);
    f->in_begin = FALSE;
    gl_draw_immediate(ctx);

    f->window_draw = FALSE;
    f->raster_op = 0;
    f->dirty = TRUE;
    memcpy(f->current[0], s.color, sizeof(s.color));
    ctx->current_program = s.program;
    ctx->cull_face = s.cull_face;
    ctx->polygon_offset_fill = s.polygon_offset_fill;
    ctx->polygon_mode = s.polygon_mode;
    f->shade_model = s.shade_model;
    for (int i = 0; i < 4; i++)
        ctx->viewport[i] = s.viewport[i];
    ctx->dirty |= DIRTY_RASTER | DIRTY_VIEWPORT | DIRTY_PIPELINE | DIRTY_VERTEX | DIRTY_TEXTURES;

    glDeleteTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture);
    glActiveTexture(GL_TEXTURE0 + previous_unit);
}

static const uint8_t *unpack_source(GlContext *ctx, const void *pixels)
{
    if (ctx->pixel_unpack) {
        GlBuffer *b = gl_buffer(ctx, ctx->pixel_unpack);
        return b && b->mem.ptr ? b->mem.ptr + (ULONG_PTR)pixels : nullptr;
    }
    return pixels;
}

GLAPI void APIENTRY glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap)
{
    GL_CONTEXT_OR_RETURN();
    if (width < 0 || height < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GlRaster *r = &ctx->raster;
    if (!r->valid)
        return;
    const uint8_t *bits = width && height ? unpack_source(ctx, bitmap) : nullptr;
    if (bits) {
        uint32_t w = (uint32_t)width, h = (uint32_t)height;
        uint8_t *rgba = gl_alloc((SIZE_T)w * h * 4);
        if (!rgba) {
            gl_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
        uint32_t row_length = ctx->unpack_row_length > 0 ? (uint32_t)ctx->unpack_row_length : w;
        uint32_t align = ctx->unpack_alignment > 0 ? (uint32_t)ctx->unpack_alignment : 1;
        uint64_t stride = (((uint64_t)row_length + 7) / 8 + align - 1) / align * align;
        bits += (uint64_t)(ctx->unpack_skip_rows > 0 ? ctx->unpack_skip_rows : 0) * stride;
        uint32_t skip = ctx->unpack_skip_pixels > 0 ? (uint32_t)ctx->unpack_skip_pixels : 0;
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t *row = bits + (uint64_t)y * stride;
            uint8_t *d = rgba + (uint64_t)y * w * 4;
            for (uint32_t x = 0; x < w; x++, d += 4) {
                uint32_t bit = skip + x;
                uint32_t shift = ctx->unpack_lsb_first ? (bit & 7) : 7 - (bit & 7);
                d[0] = d[1] = d[2] = 255;
                d[3] = (row[bit >> 3] >> shift) & 1 ? 255 : 0;
            }
        }
        draw_picture(ctx, FF_RASTER_BITMAP, round_f(r->position[0] - xorig), round_f(r->position[1] - yorig), r->position[2], w,
                     h, rgba, r->color, 1, 1);
        gl_free(rgba);
    }
    r->position[0] += xmove;
    r->position[1] += ymove;
}

/* one row of a picture as rgba bytes, FALSE for a format the picture cannot take. */
static BOOL picture_row(GLenum format, GLenum type, const uint8_t *src, uint32_t w, uint8_t *dst, uint8_t *scratch)
{
    switch (format) {
    case GL_RGBA:
    case GL_BGRA:
    case GL_RGB:
    case GL_BGR:
        gl_unpack_row(MTL_RGBA8, dst, format, type, src, w);
        return TRUE;
    case GL_LUMINANCE:
    case GL_RED:
    case GL_ALPHA:
        gl_unpack_row(MTL_R8, scratch, format, type, src, w);
        for (uint32_t x = 0; x < w; x++, dst += 4) {
            uint8_t v = scratch[x];
            dst[0] = format == GL_ALPHA ? 0 : v;
            dst[1] = dst[2] = format == GL_LUMINANCE ? v : 0;
            dst[3] = format == GL_ALPHA ? v : 255;
        }
        return TRUE;
    case GL_LUMINANCE_ALPHA:
        gl_unpack_row(MTL_RG8, scratch, format, type, src, w);
        for (uint32_t x = 0; x < w; x++, dst += 4) {
            dst[0] = dst[1] = dst[2] = scratch[x * 2];
            dst[3] = scratch[x * 2 + 1];
        }
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL picture_format(GLenum format)
{
    return format == GL_RGBA || format == GL_BGRA || format == GL_RGB || format == GL_BGR || format == GL_LUMINANCE ||
           format == GL_RED || format == GL_ALPHA || format == GL_LUMINANCE_ALPHA;
}

GLAPI void APIENTRY glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    GL_CONTEXT_OR_RETURN();
    if (width < 0 || height < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    /* depth, stencil and color index pictures are not drawn. */
    BOOL ignored = format == GL_DEPTH_COMPONENT || format == GL_STENCIL_INDEX || format == GL_COLOR_INDEX;
    if (!picture_format(format) && !ignored) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    uint32_t sbpp = gl_client_pixel_bytes(format, type);
    if (!sbpp && !ignored) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlRaster *r = &ctx->raster;
    const uint8_t *src = width && height && !ignored && r->valid ? unpack_source(ctx, pixels) : nullptr;
    if (!src)
        return;
    uint32_t w = (uint32_t)width, h = (uint32_t)height;
    uint8_t *rgba = gl_alloc((SIZE_T)w * h * 4 + (SIZE_T)w * 2);
    if (!rgba) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    uint8_t *scratch = rgba + (SIZE_T)w * h * 4;
    uint32_t row_length = ctx->unpack_row_length > 0 ? (uint32_t)ctx->unpack_row_length : w;
    uint32_t align = ctx->unpack_alignment > 0 ? (uint32_t)ctx->unpack_alignment : 1;
    uint64_t stride = ((uint64_t)row_length * sbpp + align - 1) / align * align;
    src += (uint64_t)(ctx->unpack_skip_rows > 0 ? ctx->unpack_skip_rows : 0) * stride +
           (uint64_t)(ctx->unpack_skip_pixels > 0 ? ctx->unpack_skip_pixels : 0) * sbpp;
    for (uint32_t y = 0; y < h; y++)
        picture_row(format, type, src + (uint64_t)y * stride, w, rgba + (uint64_t)y * w * 4, scratch);
    GLfloat white[4] = {1, 1, 1, 1};
    draw_picture(ctx, FF_RASTER_PIXELS, round_f(r->position[0]), round_f(r->position[1]), r->position[2], w, h, rgba, white,
                 r->zoom[0], r->zoom[1]);
    gl_free(rgba);
}

GLAPI void APIENTRY glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type)
{
    GL_CONTEXT_OR_RETURN();
    if (width < 0 || height < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (type != GL_COLOR && type != GL_DEPTH && type != GL_STENCIL) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlRaster *r = &ctx->raster;
    if (type != GL_COLOR || !width || !height || !r->valid)
        return;
    uint8_t *rgba = gl_alloc((SIZE_T)width * height * 4);
    if (!rgba) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    GLint alignment = ctx->pack_alignment, row = ctx->pack_row_length, rows = ctx->pack_skip_rows, pixels = ctx->pack_skip_pixels;
    GLuint buffer = ctx->pixel_pack;
    ctx->pack_alignment = 1;
    ctx->pack_row_length = ctx->pack_skip_rows = ctx->pack_skip_pixels = 0;
    ctx->pixel_pack = 0;
    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    ctx->pack_alignment = alignment;
    ctx->pack_row_length = row;
    ctx->pack_skip_rows = rows;
    ctx->pack_skip_pixels = pixels;
    ctx->pixel_pack = buffer;
    GLfloat white[4] = {1, 1, 1, 1};
    draw_picture(ctx, FF_RASTER_PIXELS, round_f(r->position[0]), round_f(r->position[1]), r->position[2], (uint32_t)width,
                 (uint32_t)height, rgba, white, r->zoom[0], r->zoom[1]);
    gl_free(rgba);
}

GLAPI void APIENTRY glPixelZoom(GLfloat xfactor, GLfloat yfactor)
{
    GL_CONTEXT_OR_RETURN();
    ctx->raster.zoom[0] = xfactor;
    ctx->raster.zoom[1] = yfactor;
}
