/*
 * the pieces of OpenGL 4.3 and 4.4 that are state or queries: invalidating, what a format supports, and copying a region
 * between images.
 */
#include "opengl32.h"

/* an invalidated image keeps its contents here: the specification lets the driver keep them. */
GLAPI void APIENTRY glInvalidateSubFramebuffer(GLenum target, GLsizei n, const GLenum *attachments, GLint x, GLint y,
                                               GLsizei w, GLsizei h)
{
    GL_CONTEXT_OR_RETURN();
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (n < 0 || w < 0 || h < 0)
        gl_error(ctx, GL_INVALID_VALUE);
}

GLAPI void APIENTRY glInvalidateTexSubImage(GLuint texture, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h,
                                            GLsizei d)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = gl_texture(ctx, texture);
    if (!t || level < 0 || w < 0 || h < 0 || d < 0)
        gl_error(ctx, GL_INVALID_VALUE);
}

GLAPI void APIENTRY glInvalidateTexImage(GLuint texture, GLint level)
{
    glInvalidateTexSubImage(texture, level, 0, 0, 0, 0, 0, 0);
}

/* ---------------------------------------------------------------- internal format queries */

static BOOL multisample_target(GLenum target)
{
    return target == GL_RENDERBUFFER || target == GL_TEXTURE_2D_MULTISAMPLE;
}

static BOOL color_renderable(uint32_t format)
{
    return format && !gl_format_is_depth(format) && !gl_format_has_stencil(format);
}

static BOOL known_target(GLenum target)
{
    switch (target) {
    case GL_TEXTURE_1D:
    case GL_TEXTURE_2D:
    case GL_TEXTURE_3D:
    case GL_TEXTURE_CUBE_MAP:
    case GL_TEXTURE_RECTANGLE:
    case GL_TEXTURE_1D_ARRAY:
    case GL_TEXTURE_2D_ARRAY:
    case GL_TEXTURE_2D_MULTISAMPLE:
    case GL_RENDERBUFFER:
        return TRUE;
    default:
        return FALSE;
    }
}

GLAPI void APIENTRY glGetInternalformati64v(GLenum target, GLenum internal, GLenum pname, GLsizei size, GLint64 *params)
{
    GL_CONTEXT_OR_RETURN();
    if (!known_target(target)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (size < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    uint32_t swizzle;
    BOOL compressed;
    uint32_t format = gl_mtl_format(internal, GL_RGBA, GL_UNSIGNED_BYTE, &swizzle, &compressed);
    BOOL supported = format != 0;
    BOOL integer = internal == GL_R32UI || internal == GL_RGBA8UI;
    /* the sample counts the host renders to, largest first. */
    static const GLint samples[] = {4, 2, 1};
    GLint64 v[3] = {0, 0, 0};
    GLsizei n = 1;
    switch (pname) {
    case GL_NUM_SAMPLE_COUNTS:
        v[0] = supported && multisample_target(target) && !compressed && !integer ? 3 : 0;
        break;
    case GL_SAMPLES:
        n = 0;
        if (supported && multisample_target(target) && !compressed && !integer)
            for (; n < 3 && n < size; n++)
                v[n] = samples[n];
        if (!n)
            return;
        break;
    case GL_INTERNALFORMAT_SUPPORTED:
        v[0] = supported;
        break;
    case GL_INTERNALFORMAT_PREFERRED:
        v[0] = supported ? internal : GL_NONE;
        break;
    case GL_INTERNALFORMAT_RED_SIZE:
    case GL_INTERNALFORMAT_GREEN_SIZE:
    case GL_INTERNALFORMAT_BLUE_SIZE:
    case GL_INTERNALFORMAT_ALPHA_SIZE:
        v[0] = supported && color_renderable(format) ? gl_format_bytes(format) * 8 / (format == 70 || format == 71 || format == 80 ? 4 : 1) : 0;
        break;
    case GL_INTERNALFORMAT_DEPTH_SIZE:
        v[0] = supported && gl_format_is_depth(format) ? 32 : 0;
        break;
    case GL_INTERNALFORMAT_STENCIL_SIZE:
        v[0] = supported && gl_format_has_stencil(format) ? 8 : 0;
        break;
    case GL_COLOR_RENDERABLE:
    case GL_FRAMEBUFFER_RENDERABLE:
    case GL_FRAMEBUFFER_RENDERABLE_LAYERED:
        v[0] = supported && !compressed && !integer ? GL_FULL_SUPPORT : GL_NONE;
        break;
    case GL_DEPTH_RENDERABLE:
        v[0] = supported && gl_format_is_depth(format) ? GL_FULL_SUPPORT : GL_NONE;
        break;
    case GL_STENCIL_RENDERABLE:
        v[0] = supported && gl_format_has_stencil(format) ? GL_FULL_SUPPORT : GL_NONE;
        break;
    case GL_FRAMEBUFFER_BLEND:
    case GL_FILTER:
    case GL_MIPMAP:
    case GL_GENERATE_MIPMAP:
    case GL_AUTO_GENERATE_MIPMAP:
        v[0] = supported && !integer && !gl_format_is_depth(format) ? GL_FULL_SUPPORT : GL_NONE;
        break;
    case GL_TEXTURE_COMPRESSED:
        v[0] = compressed;
        break;
    case GL_SHADER_IMAGE_LOAD:
    case GL_SHADER_IMAGE_STORE:
        v[0] = supported && !compressed && !gl_format_is_depth(format) && gl_host_has(MIME_GPU_FEATURE_COMPUTE)
                   ? GL_FULL_SUPPORT
                   : GL_NONE;
        break;
    case GL_MAX_WIDTH:
    case GL_MAX_HEIGHT:
        v[0] = supported ? 16384 : 0;
        break;
    case GL_MAX_DEPTH:
        v[0] = supported ? 2048 : 0;
        break;
    case GL_MAX_LAYERS:
        v[0] = supported ? 2048 : 0;
        break;
    case GL_MAX_COMBINED_DIMENSIONS:
        v[0] = supported ? 16384ll * 16384 : 0;
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    for (GLsizei i = 0; i < n && i < size; i++)
        params[i] = v[i];
}

GLAPI void APIENTRY glGetInternalformativ(GLenum target, GLenum internal, GLenum pname, GLsizei size, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    if (size < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GLint64 v[16];
    GLsizei count = size < 16 ? size : 16;
    for (GLsizei i = 0; i < count; i++)
        v[i] = 0;
    glGetInternalformati64v(target, internal, pname, count, v);
    for (GLsizei i = 0; i < count; i++)
        params[i] = (GLint)v[i];
}

/* ---------------------------------------------------------------- copying between images */

typedef struct ImageRef {
    uint32_t id, mtl_format, levels, width, height, depth, samples;
    GLenum target;
} ImageRef;

static BOOL image_ref(GlContext *ctx, GLuint name, GLenum target, ImageRef *r)
{
    if (target == GL_RENDERBUFFER) {
        GlRenderbuffer *rb = gl_renderbuffer(ctx, name);
        if (!rb || !rb->id)
            return FALSE;
        *r = (ImageRef){rb->id, rb->mtl_format, 1, rb->width, rb->height, 1, rb->samples ? rb->samples : 1, target};
        return TRUE;
    }
    GlTexture *t = gl_texture(ctx, name);
    if (!t || !t->id || t->target != target)
        return FALSE;
    *r = (ImageRef){t->id, t->mtl_format, t->levels, t->width, t->height, t->depth ? t->depth : 1,
                    t->samples ? t->samples : 1, target};
    return TRUE;
}

/* the layer count of a target: the depth of a 3d image is its slices, of a cube it is six faces. */
static BOOL layered(GLenum target)
{
    return target == GL_TEXTURE_3D || target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_CUBE_MAP ||
           target == GL_TEXTURE_1D_ARRAY;
}

GLAPI void APIENTRY glCopyImageSubData(GLuint src, GLenum src_target, GLint src_level, GLint sx, GLint sy, GLint sz,
                                       GLuint dst, GLenum dst_target, GLint dst_level, GLint dx, GLint dy, GLint dz,
                                       GLsizei w, GLsizei h, GLsizei d)
{
    GL_CONTEXT_OR_RETURN();
    if (!gl_host_has(MIME_GPU_FEATURE_COMPUTE)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    ImageRef s, t;
    if (!known_target(src_target) || !known_target(dst_target)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!image_ref(ctx, src, src_target, &s) || !image_ref(ctx, dst, dst_target, &t)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (src_level < 0 || dst_level < 0 || (uint32_t)src_level >= s.levels || (uint32_t)dst_level >= t.levels ||
        w < 0 || h < 0 || d < 0 || sx < 0 || sy < 0 || sz < 0 || dx < 0 || dy < 0 || dz < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (s.mtl_format != t.mtl_format || s.samples != t.samples) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    uint32_t sw = s.width >> src_level ? s.width >> src_level : 1, sh = s.height >> src_level ? s.height >> src_level : 1;
    uint32_t tw = t.width >> dst_level ? t.width >> dst_level : 1, th = t.height >> dst_level ? t.height >> dst_level : 1;
    uint32_t sd = src_target == GL_TEXTURE_3D ? (s.depth >> src_level ? s.depth >> src_level : 1)
                  : src_target == GL_TEXTURE_CUBE_MAP ? 6 : s.depth;
    uint32_t td = dst_target == GL_TEXTURE_3D ? (t.depth >> dst_level ? t.depth >> dst_level : 1)
                  : dst_target == GL_TEXTURE_CUBE_MAP ? 6 : t.depth;
    if ((uint32_t)(sx + w) > sw || (uint32_t)(sy + h) > sh || (uint32_t)(dx + w) > tw || (uint32_t)(dy + h) > th ||
        (uint32_t)(sz + d) > sd || (uint32_t)(dz + d) > td || (!layered(src_target) && sz) || (!layered(dst_target) && dz)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!w || !h || !d)
        return;
    gl_device_lock();
    gl_flush_clears(ctx);
    MimeGpuCopyImage *c = gl_emit(MIME_GPU_COPY_IMAGE, sizeof(MimeGpuCopyImage));
    c->src = s.id;
    c->dst = t.id;
    c->src_level = (uint32_t)src_level;
    c->dst_level = (uint32_t)dst_level;
    c->src_x = (uint32_t)sx;
    c->src_y = (uint32_t)sy;
    c->src_z = (uint32_t)sz;
    c->dst_x = (uint32_t)dx;
    c->dst_y = (uint32_t)dy;
    c->dst_z = (uint32_t)dz;
    c->width = (uint32_t)w;
    c->height = (uint32_t)h;
    c->depth = (uint32_t)d;
    c->src_3d = src_target == GL_TEXTURE_3D;
    c->dst_3d = dst_target == GL_TEXTURE_3D;
    gl_device_unlock();
}
