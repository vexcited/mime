/*
 * direct state access: the commands that name the object they change instead of using what is bound, and the
 * framebuffer parameters of OpenGL 4.3. an object has to exist, made by the glCreate command of its kind or bound before:
 * a name that was only generated is a GL_INVALID_OPERATION. the commands bind the object in a slot of the context
 * the classic command reads, call it and put the old binding back, so the checks and the work are the classic ones and
 * no binding of the application changes.
 */
#define GL_GLEXT_PROTOTYPES
#include "opengl32.h"

#define COPY_BUFFER_SLOT(ctx) ((ctx)->copy_write)

/* ---------------------------------------------------------------- limits */

int gl_dsa_get(GlContext *ctx, GLenum pname, GLdouble *v)
{
    switch (pname) {
    case GL_MAX_VERTEX_ATTRIB_BINDINGS:
        v[0] = MIME_GPU_MAX_ATTRIBS;
        return 1;
    case GL_MAX_VERTEX_ATTRIB_RELATIVE_OFFSET:
        v[0] = 2047;
        return 1;
    case GL_MAX_VERTEX_ATTRIB_STRIDE:
        v[0] = 2048;
        return 1;
    case GL_MAX_FRAMEBUFFER_WIDTH:
    case GL_MAX_FRAMEBUFFER_HEIGHT:
        v[0] = 16384;
        return 1;
    case GL_MAX_FRAMEBUFFER_LAYERS:
        v[0] = 2048;
        return 1;
    case GL_MAX_FRAMEBUFFER_SAMPLES:
        v[0] = 4;
        return 1;
    case GL_RESET_NOTIFICATION_STRATEGY:
        v[0] = ctx->reset_strategy ? ctx->reset_strategy : GL_NO_RESET_NOTIFICATION;
        return 1;
    case GL_POLYGON_OFFSET_CLAMP:
        v[0] = ctx->offset_clamp;
        return 1;
    case GL_CLIP_ORIGIN:
        v[0] = ctx->clip_upper_left ? GL_UPPER_LEFT : GL_LOWER_LEFT;
        return 1;
    case GL_CLIP_DEPTH_MODE:
        v[0] = ctx->clip_zero_to_one ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE;
        return 1;
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------- buffers */

static GlBuffer *buffer_named(GlContext *ctx, GLuint name)
{
    GlBuffer *b = name ? gl_buffer(ctx, name) : nullptr;
    if (!b)
        gl_error(ctx, GL_INVALID_OPERATION);
    return b;
}

#define WITH_BUFFER(buffer, call)                                                                                      \
    do {                                                                                                               \
        GLuint saved_ = COPY_BUFFER_SLOT(ctx);                                                                         \
        COPY_BUFFER_SLOT(ctx) = (buffer);                                                                              \
        call;                                                                                                          \
        COPY_BUFFER_SLOT(ctx) = saved_;                                                                                \
    } while (0)

GLAPI void APIENTRY glNamedBufferStorage(GLuint buffer, GLsizeiptr size, const void *data, GLbitfield flags)
{
    GL_CONTEXT_OR_RETURN();
    if (buffer_named(ctx, buffer))
        WITH_BUFFER(buffer, glBufferStorage(GL_COPY_WRITE_BUFFER, size, data, flags));
}

GLAPI void APIENTRY glNamedBufferData(GLuint buffer, GLsizeiptr size, const void *data, GLenum usage)
{
    GL_CONTEXT_OR_RETURN();
    if (buffer_named(ctx, buffer))
        WITH_BUFFER(buffer, glBufferData(GL_COPY_WRITE_BUFFER, size, data, usage));
}

GLAPI void APIENTRY glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    if (buffer_named(ctx, buffer))
        WITH_BUFFER(buffer, glBufferSubData(GL_COPY_WRITE_BUFFER, offset, size, data));
}

GLAPI void APIENTRY glCopyNamedBufferSubData(GLuint read, GLuint write, GLintptr read_offset, GLintptr write_offset,
                                             GLsizeiptr size)
{
    GL_CONTEXT_OR_RETURN();
    if (!buffer_named(ctx, read) || !buffer_named(ctx, write))
        return;
    GLuint saved_read = ctx->copy_read, saved_write = ctx->copy_write;
    ctx->copy_read = read;
    ctx->copy_write = write;
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, read_offset, write_offset, size);
    ctx->copy_read = saved_read;
    ctx->copy_write = saved_write;
}

GLAPI void APIENTRY glClearNamedBufferData(GLuint buffer, GLenum internal, GLenum format, GLenum type, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    if (buffer_named(ctx, buffer))
        WITH_BUFFER(buffer, glClearBufferData(GL_COPY_WRITE_BUFFER, internal, format, type, data));
}

GLAPI void APIENTRY glClearNamedBufferSubData(GLuint buffer, GLenum internal, GLintptr offset, GLsizeiptr size,
                                              GLenum format, GLenum type, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    if (buffer_named(ctx, buffer))
        WITH_BUFFER(buffer, glClearBufferSubData(GL_COPY_WRITE_BUFFER, internal, offset, size, format, type, data));
}

GLAPI void *APIENTRY glMapNamedBuffer(GLuint buffer, GLenum access)
{
    GL_CONTEXT_OR_RETURN(nullptr);
    void *p = nullptr;
    if (buffer_named(ctx, buffer))
        WITH_BUFFER(buffer, p = glMapBuffer(GL_COPY_WRITE_BUFFER, access));
    return p;
}

GLAPI void *APIENTRY glMapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
    GL_CONTEXT_OR_RETURN(nullptr);
    void *p = nullptr;
    if (buffer_named(ctx, buffer))
        WITH_BUFFER(buffer, p = glMapBufferRange(GL_COPY_WRITE_BUFFER, offset, length, access));
    return p;
}

GLAPI GLboolean APIENTRY glUnmapNamedBuffer(GLuint buffer)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    GLboolean r = GL_FALSE;
    if (buffer_named(ctx, buffer))
        WITH_BUFFER(buffer, r = glUnmapBuffer(GL_COPY_WRITE_BUFFER));
    return r;
}

GLAPI void APIENTRY glFlushMappedNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *b = buffer_named(ctx, buffer);
    if (!b)
        return;
    if (!b->mapped || !(b->map_access & GL_MAP_FLUSH_EXPLICIT_BIT)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (offset < 0 || length < 0 || (uint64_t)offset + (uint64_t)length > b->map_length)
        gl_error(ctx, GL_INVALID_VALUE);
}

GLAPI void APIENTRY glGetNamedBufferParameteriv(GLuint buffer, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    if (buffer_named(ctx, buffer))
        WITH_BUFFER(buffer, glGetBufferParameteriv(GL_COPY_WRITE_BUFFER, pname, params));
}

GLAPI void APIENTRY glGetNamedBufferParameteri64v(GLuint buffer, GLenum pname, GLint64 *params)
{
    GL_CONTEXT_OR_RETURN();
    if (buffer_named(ctx, buffer))
        WITH_BUFFER(buffer, glGetBufferParameteri64v(GL_COPY_WRITE_BUFFER, pname, params));
}

GLAPI void APIENTRY glGetNamedBufferPointerv(GLuint buffer, GLenum pname, void **params)
{
    GL_CONTEXT_OR_RETURN();
    if (pname != GL_BUFFER_MAP_POINTER) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (buffer_named(ctx, buffer))
        WITH_BUFFER(buffer, glGetBufferPointerv(GL_COPY_WRITE_BUFFER, pname, params));
}

GLAPI void APIENTRY glGetNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, void *data)
{
    GL_CONTEXT_OR_RETURN();
    if (buffer_named(ctx, buffer))
        WITH_BUFFER(buffer, glGetBufferSubData(GL_COPY_WRITE_BUFFER, offset, size, data));
}

/* ---------------------------------------------------------------- textures */

typedef struct TexBind {
    GLuint *slot;
    GLuint saved;
    GlTexture *t;
} TexBind;

static BOOL tex_bind(GlContext *ctx, GLuint name, TexBind *b)
{
    b->t = name ? gl_texture(ctx, name) : nullptr;
    if (!b->t) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return FALSE;
    }
    b->slot = gl_texture_slot(ctx, b->t->target);
    if (!b->slot) {
        gl_error(ctx, GL_INVALID_ENUM);
        return FALSE;
    }
    b->saved = *b->slot;
    *b->slot = name;
    return TRUE;
}

static void tex_unbind(GlContext *ctx, TexBind *b)
{
    *b->slot = b->saved;
    ctx->dirty |= DIRTY_TEXTURES;
}

GLAPI void APIENTRY glCreateTextures(GLenum target, GLsizei n, GLuint *textures)
{
    GL_CONTEXT_OR_RETURN();
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!gl_texture_slot(ctx, target)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    for (GLsizei i = 0; i < n; i++) {
        GLuint name = gl_name_new(&ctx->shared->textures);
        textures[i] = name;
        if (!gl_texture_create(ctx, name, target))
            gl_error(ctx, GL_OUT_OF_MEMORY);
    }
}

static BOOL target_in(GLenum target, const GLenum *list, int count)
{
    for (int i = 0; i < count; i++)
        if (list[i] == target)
            return TRUE;
    return FALSE;
}

#define TARGET_IS(...) (target_in(b.t->target, (const GLenum[]){__VA_ARGS__}, (int)(sizeof((const GLenum[]){__VA_ARGS__}) / sizeof(GLenum))))

/* binds the texture and checks that its target is one of the list, or leaves with an error. */
#define BIND_OR_RETURN(ret, ...)                                                                                       \
    TexBind b;                                                                                                         \
    if (!tex_bind(ctx, texture, &b))                                                                                   \
        return ret;                                                                                                    \
    if (!TARGET_IS(__VA_ARGS__)) {                                                                                     \
        tex_unbind(ctx, &b);                                                                                           \
        gl_error(ctx, GL_INVALID_OPERATION);                                                                           \
        return ret;                                                                                                    \
    }

GLAPI void APIENTRY glTextureStorage1D(GLuint texture, GLsizei levels, GLenum internal, GLsizei w)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_1D);
    glTexStorage1D(GL_TEXTURE_1D, levels, internal, w);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glTextureStorage2D(GLuint texture, GLsizei levels, GLenum internal, GLsizei w, GLsizei h)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_2D, GL_TEXTURE_RECTANGLE, GL_TEXTURE_1D_ARRAY, GL_TEXTURE_CUBE_MAP);
    glTexStorage2D(b.t->target, levels, internal, w, h);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glTextureStorage3D(GLuint texture, GLsizei levels, GLenum internal, GLsizei w, GLsizei h, GLsizei d)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_3D, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_CUBE_MAP_ARRAY);
    glTexStorage3D(b.t->target, levels, internal, w, h, d);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glTextureStorage2DMultisample(GLuint texture, GLsizei samples, GLenum internal, GLsizei w, GLsizei h,
                                                  GLboolean fixed)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_2D_MULTISAMPLE);
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, internal, w, h, fixed);
    tex_unbind(ctx, &b);
}

/* multisample array textures do not exist here, so no texture has the target this one needs. */
GLAPI void APIENTRY glTextureStorage3DMultisample(GLuint texture, GLsizei samples, GLenum internal, GLsizei w, GLsizei h,
                                                  GLsizei d, GLboolean fixed)
{
    GL_CONTEXT_OR_RETURN();
    gl_error(ctx, GL_INVALID_OPERATION);
}

GLAPI void APIENTRY glTextureSubImage1D(GLuint texture, GLint level, GLint x, GLsizei w, GLenum format, GLenum type,
                                        const void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_1D);
    glTexSubImage1D(GL_TEXTURE_1D, level, x, w, format, type, pixels);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glTextureSubImage2D(GLuint texture, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                        GLenum format, GLenum type, const void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_2D, GL_TEXTURE_RECTANGLE, GL_TEXTURE_1D_ARRAY);
    glTexSubImage2D(b.t->target, level, x, y, w, h, format, type, pixels);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glTextureSubImage3D(GLuint texture, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h,
                                        GLsizei d, GLenum format, GLenum type, const void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_3D, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_CUBE_MAP_ARRAY);
    if (b.t->target != GL_TEXTURE_CUBE_MAP) {
        glTexSubImage3D(b.t->target, level, x, y, z, w, h, d, format, type, pixels);
    } else {
        /* the faces are the layers, one upload each, in the pixels one after the other. */
        if (z < 0 || z + d > 6 || d < 0) {
            tex_unbind(ctx, &b);
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        uint32_t row = ((uint32_t)w * gl_client_pixel_bytes(format, type) + (uint32_t)ctx->unpack_alignment - 1) /
                       (uint32_t)ctx->unpack_alignment * (uint32_t)ctx->unpack_alignment;
        for (GLsizei k = 0; k < d; k++)
            glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)(z + k), level, x, y, w, h, format, type,
                            (const uint8_t *)pixels + (uint64_t)k * row * (uint32_t)h);
    }
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glCompressedTextureSubImage1D(GLuint texture, GLint level, GLint x, GLsizei w, GLenum format,
                                                  GLsizei size, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_1D);
    /* block compressed formats have no one dimensional images here. */
    tex_unbind(ctx, &b);
    gl_error(ctx, GL_INVALID_OPERATION);
}

GLAPI void APIENTRY glCompressedTextureSubImage2D(GLuint texture, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                                  GLenum format, GLsizei size, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_2D, GL_TEXTURE_RECTANGLE);
    glCompressedTexSubImage2D(b.t->target, level, x, y, w, h, format, size, data);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glCompressedTextureSubImage3D(GLuint texture, GLint level, GLint x, GLint y, GLint z, GLsizei w,
                                                  GLsizei h, GLsizei d, GLenum format, GLsizei size, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_CUBE_MAP);
    if (z < 0 || z + d > 6 || d < 0) {
        tex_unbind(ctx, &b);
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GLsizei each = d ? size / d : 0;
    for (GLsizei k = 0; k < d; k++)
        glCompressedTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)(z + k), level, x, y, w, h, format, each,
                                  (const uint8_t *)data + (uint64_t)k * (uint32_t)each);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glCopyTextureSubImage1D(GLuint texture, GLint level, GLint x, GLint sx, GLint sy, GLsizei w)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_1D);
    glCopyTexSubImage1D(GL_TEXTURE_1D, level, x, sx, sy, w);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glCopyTextureSubImage2D(GLuint texture, GLint level, GLint x, GLint y, GLint sx, GLint sy, GLsizei w,
                                            GLsizei h)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_2D, GL_TEXTURE_RECTANGLE, GL_TEXTURE_1D_ARRAY);
    glCopyTexSubImage2D(b.t->target, level, x, y, sx, sy, w, h);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glCopyTextureSubImage3D(GLuint texture, GLint level, GLint x, GLint y, GLint z, GLint sx, GLint sy,
                                            GLsizei w, GLsizei h)
{
    GL_CONTEXT_OR_RETURN();
    BIND_OR_RETURN(, GL_TEXTURE_3D, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_CUBE_MAP_ARRAY);
    if (z < 0 || level < 0 || w < 0 || h < 0) {
        tex_unbind(ctx, &b);
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    gl_texture_copy_sub(ctx, b.t, level, (uint32_t)z, x, y, sx, sy, w, h);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glTextureParameterf(GLuint texture, GLenum pname, GLfloat param)
{
    GL_CONTEXT_OR_RETURN();
    TexBind b;
    if (!tex_bind(ctx, texture, &b))
        return;
    glTexParameterf(b.t->target, pname, param);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glTextureParameteri(GLuint texture, GLenum pname, GLint param)
{
    GL_CONTEXT_OR_RETURN();
    TexBind b;
    if (!tex_bind(ctx, texture, &b))
        return;
    glTexParameteri(b.t->target, pname, param);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glTextureParameterfv(GLuint texture, GLenum pname, const GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    TexBind b;
    if (!tex_bind(ctx, texture, &b))
        return;
    glTexParameterfv(b.t->target, pname, params);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glTextureParameteriv(GLuint texture, GLenum pname, const GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    TexBind b;
    if (!tex_bind(ctx, texture, &b))
        return;
    glTexParameteriv(b.t->target, pname, params);
    tex_unbind(ctx, &b);
}

/* the integer forms differ from the others only in a border color that is taken as it is. */
GLAPI void APIENTRY glTextureParameterIiv(GLuint texture, GLenum pname, const GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    TexBind b;
    if (!tex_bind(ctx, texture, &b))
        return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        GLfloat v[4] = {(GLfloat)params[0], (GLfloat)params[1], (GLfloat)params[2], (GLfloat)params[3]};
        glTexParameterfv(b.t->target, pname, v);
    } else {
        glTexParameteriv(b.t->target, pname, params);
    }
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glTextureParameterIuiv(GLuint texture, GLenum pname, const GLuint *params)
{
    GL_CONTEXT_OR_RETURN();
    TexBind b;
    if (!tex_bind(ctx, texture, &b))
        return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        GLfloat v[4] = {(GLfloat)params[0], (GLfloat)params[1], (GLfloat)params[2], (GLfloat)params[3]};
        glTexParameterfv(b.t->target, pname, v);
    } else {
        glTexParameteriv(b.t->target, pname, (const GLint *)params);
    }
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glGenerateTextureMipmap(GLuint texture)
{
    GL_CONTEXT_OR_RETURN();
    TexBind b;
    if (!tex_bind(ctx, texture, &b))
        return;
    glGenerateMipmap(b.t->target);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glBindTextureUnit(GLuint unit, GLuint texture)
{
    GL_CONTEXT_OR_RETURN();
    if (unit >= 32) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GlTexture *t = texture ? gl_texture(ctx, texture) : nullptr;
    if (texture && !t) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GLuint active = ctx->active_texture;
    ctx->active_texture = unit;
    if (t) {
        GLuint *slot = gl_texture_slot(ctx, t->target);
        if (slot)
            *slot = texture;
    } else {
        static const GLenum targets[] = {GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_CUBE_MAP,
                                         GL_TEXTURE_RECTANGLE, GL_TEXTURE_1D_ARRAY, GL_TEXTURE_2D_ARRAY,
                                         GL_TEXTURE_2D_MULTISAMPLE};
        for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
            GLuint *slot = gl_texture_slot(ctx, targets[i]);
            if (slot)
                *slot = 0;
        }
    }
    ctx->active_texture = active;
    ctx->dirty |= DIRTY_TEXTURES;
}

GLAPI void APIENTRY glGetTextureParameterfv(GLuint texture, GLenum pname, GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    TexBind b;
    if (!tex_bind(ctx, texture, &b))
        return;
    glGetTexParameterfv(b.t->target, pname, params);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glGetTextureParameteriv(GLuint texture, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    TexBind b;
    if (!tex_bind(ctx, texture, &b))
        return;
    glGetTexParameteriv(b.t->target, pname, params);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glGetTextureParameterIiv(GLuint texture, GLenum pname, GLint *params)
{
    glGetTextureParameteriv(texture, pname, params);
}

GLAPI void APIENTRY glGetTextureParameterIuiv(GLuint texture, GLenum pname, GLuint *params)
{
    glGetTextureParameteriv(texture, pname, (GLint *)params);
}

GLAPI void APIENTRY glGetTextureLevelParameteriv(GLuint texture, GLint level, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    TexBind b;
    if (!tex_bind(ctx, texture, &b))
        return;
    glGetTexLevelParameteriv(b.t->target, level, pname, params);
    tex_unbind(ctx, &b);
}

GLAPI void APIENTRY glGetTextureLevelParameterfv(GLuint texture, GLint level, GLenum pname, GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    TexBind b;
    if (!tex_bind(ctx, texture, &b))
        return;
    glGetTexLevelParameterfv(b.t->target, level, pname, params);
    tex_unbind(ctx, &b);
}

/* ---------------------------------------------------------------- reading an image back */

static uint32_t level_size(uint32_t base, GLint level)
{
    return base >> level ? base >> level : 1;
}

static BOOL layered(GLenum target)
{
    return target == GL_TEXTURE_3D || target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_CUBE_MAP ||
           target == GL_TEXTURE_CUBE_MAP_ARRAY || target == GL_TEXTURE_1D_ARRAY;
}

/*
 * the region of a level, layer by layer through a framebuffer over the texture, into the pixels in the order the pixel
 * store packs them. returns FALSE after raising the error.
 */
static BOOL read_region(GlContext *ctx, GlTexture *t, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h,
                        GLsizei d, GLenum format, GLenum type, GLsizei buf_size, void *pixels, BOOL compressed)
{
    if (!t->id || level < 0 || (uint32_t)level >= t->levels || w < 0 || h < 0 || d < 0) {
        gl_error(ctx, t->id && level >= 0 && (uint32_t)level < t->levels ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return FALSE;
    }
    if (compressed != (t->compressed != 0)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return FALSE;
    }
    uint32_t lw = level_size(t->width, level), lh = t->target == GL_TEXTURE_1D ? 1 : level_size(t->height, level);
    uint32_t layers = t->target == GL_TEXTURE_3D ? level_size(t->depth, level)
                      : t->target == GL_TEXTURE_CUBE_MAP ? 6 : layered(t->target) ? t->depth : 1;
    if (t->target == GL_TEXTURE_1D_ARRAY) {
        layers = t->height;
        lh = 1;
    }
    if (x < 0 || y < 0 || z < 0 || (uint64_t)x + (uint64_t)w > lw || (uint64_t)y + (uint64_t)h > lh ||
        (uint64_t)z + (uint64_t)d > layers) {
        gl_error(ctx, GL_INVALID_VALUE);
        return FALSE;
    }
    if (!w || !h || !d)
        return TRUE;
    uint32_t bpp = 0, row = 0;
    if (compressed) {
        row = ((uint32_t)w + 3) / 4 * (t->mtl_format == 130 ? 8 : 16);
    } else {
        bpp = gl_client_pixel_bytes(format, type);
        if (!bpp) {
            gl_error(ctx, GL_INVALID_ENUM);
            return FALSE;
        }
        uint32_t row_len = ctx->pack_row_length ? (uint32_t)ctx->pack_row_length : (uint32_t)w;
        uint32_t align = ctx->pack_alignment ? (uint32_t)ctx->pack_alignment : 1;
        row = (row_len * bpp + align - 1) / align * align;
    }
    uint64_t image = compressed ? (uint64_t)row * (((uint32_t)h + 3) / 4) : (uint64_t)row * (uint32_t)h;
    uint64_t need = image * (uint32_t)(d - 1) + (compressed ? image : (uint64_t)row * (uint32_t)(h - 1) + (uint64_t)w * bpp);
    if (!ctx->pixel_pack && (uint64_t)buf_size < need) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return FALSE;
    }
    GLuint saved = ctx->read_fbo, fbo;
    BOOL depth = !compressed && gl_format_is_depth(t->mtl_format) && (format == GL_DEPTH_COMPONENT || format == GL_STENCIL_INDEX ||
                                                                       format == GL_DEPTH_STENCIL);
    GLenum point = depth ? (format == GL_STENCIL_INDEX ? GL_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT) : GL_COLOR_ATTACHMENT0;
    if (!compressed) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        if (point == GL_STENCIL_ATTACHMENT && gl_format_has_stencil(t->mtl_format) && gl_format_is_depth(t->mtl_format))
            point = GL_DEPTH_STENCIL_ATTACHMENT;
    }
    BOOL ok = TRUE;
    for (GLsizei k = 0; k < d && ok; k++) {
        uint8_t *dst = (uint8_t *)pixels + (uint64_t)k * image;
        if (compressed) {
            ok = gl_texture_read_compressed(ctx, t, level, (uint32_t)(z + k), x, y, w, h, dst);
            continue;
        }
        if (layered(t->target) && t->target != GL_TEXTURE_1D_ARRAY)
            glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, point, t->name, level, z + k);
        else
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, point, t->target, t->name, level);
        ok = gl_read_pixels_into(ctx, x, y, w, h, format, type, dst);
    }
    if (!compressed) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, saved);
        glDeleteFramebuffers(1, &fbo);
    }
    return ok;
}

static void get_image(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei size, void *pixels, BOOL compressed)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = texture ? gl_texture(ctx, texture) : nullptr;
    if (!t) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    uint32_t layers = t->target == GL_TEXTURE_3D ? level_size(t->depth, level)
                      : t->target == GL_TEXTURE_CUBE_MAP ? 6 : layered(t->target) ? t->depth : 1;
    uint32_t h = t->target == GL_TEXTURE_1D ? 1 : level_size(t->height, level);
    if (t->target == GL_TEXTURE_1D_ARRAY) {
        layers = t->height;
        h = 1;
    }
    read_region(ctx, t, level, 0, 0, 0, (GLsizei)level_size(t->width, level), (GLsizei)h, (GLsizei)layers, format, type,
                size, pixels, compressed);
}

GLAPI void APIENTRY glGetTextureImage(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei size, void *pixels)
{
    get_image(texture, level, format, type, size, pixels, FALSE);
}

GLAPI void APIENTRY glGetCompressedTextureImage(GLuint texture, GLint level, GLsizei size, void *pixels)
{
    get_image(texture, level, 0, 0, size, pixels, TRUE);
}

GLAPI void APIENTRY glGetTextureSubImage(GLuint texture, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h,
                                         GLsizei d, GLenum format, GLenum type, GLsizei size, void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = texture ? gl_texture(ctx, texture) : nullptr;
    if (!t) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    read_region(ctx, t, level, x, y, z, w, h, d, format, type, size, pixels, FALSE);
}

GLAPI void APIENTRY glGetCompressedTextureSubImage(GLuint texture, GLint level, GLint x, GLint y, GLint z, GLsizei w,
                                                   GLsizei h, GLsizei d, GLsizei size, void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = texture ? gl_texture(ctx, texture) : nullptr;
    if (!t) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    /* whole blocks only. */
    if ((x & 3) || (y & 3) || ((w & 3) && (uint32_t)(x + w) != level_size(t->width, level)) ||
        ((h & 3) && (uint32_t)(y + h) != level_size(t->height, level))) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    read_region(ctx, t, level, x, y, z, w, h, d, 0, 0, size, pixels, TRUE);
}

/* the commands that read like glReadPixels and glGetTexImage but know the size of the memory they write. */
GLAPI void APIENTRY glReadnPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLsizei size,
                                  void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    uint32_t bpp = gl_client_pixel_bytes(format, type);
    if (w < 0 || h < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (bpp && w && h && !ctx->pixel_pack) {
        uint32_t row_len = ctx->pack_row_length ? (uint32_t)ctx->pack_row_length : (uint32_t)w;
        uint32_t align = ctx->pack_alignment ? (uint32_t)ctx->pack_alignment : 1;
        uint64_t row = ((uint64_t)row_len * bpp + align - 1) / align * align;
        uint64_t need = (uint64_t)ctx->pack_skip_rows * row + (uint64_t)ctx->pack_skip_pixels * bpp + row * (uint32_t)(h - 1) +
                        (uint64_t)w * bpp;
        if (need > (uint64_t)size) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return;
        }
    }
    glReadPixels(x, y, w, h, format, type, pixels);
}

GLAPI void APIENTRY glGetnTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLsizei size, void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    GLuint *slot = gl_texture_slot(ctx, target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
                                            ? GL_TEXTURE_CUBE_MAP : target);
    GlTexture *t = slot ? gl_texture(ctx, *slot) : nullptr;
    if (!t) {
        gl_error(ctx, slot ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return;
    }
    if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) {
        read_region(ctx, t, level, 0, 0, (GLint)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X),
                    (GLsizei)level_size(t->width, level), (GLsizei)level_size(t->height, level), 1, format, type, size, pixels,
                    FALSE);
        return;
    }
    get_image(t->name, level, format, type, size, pixels, FALSE);
}

GLAPI void APIENTRY glGetnCompressedTexImage(GLenum target, GLint level, GLsizei size, void *pixels)
{
    GL_CONTEXT_OR_RETURN();
    GLuint *slot = gl_texture_slot(ctx, target);
    GlTexture *t = slot ? gl_texture(ctx, *slot) : nullptr;
    if (!t) {
        gl_error(ctx, slot ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return;
    }
    get_image(t->name, level, 0, 0, size, pixels, TRUE);
}

/* ---------------------------------------------------------------- renderbuffers */

GLAPI void APIENTRY glCreateRenderbuffers(GLsizei n, GLuint *renderbuffers)
{
    GL_CONTEXT_OR_RETURN();
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GLuint saved = ctx->renderbuffer;
    for (GLsizei i = 0; i < n; i++) {
        glGenRenderbuffers(1, &renderbuffers[i]);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[i]);
    }
    ctx->renderbuffer = saved;
}

static GlRenderbuffer *renderbuffer_named(GlContext *ctx, GLuint name)
{
    GlRenderbuffer *r = name ? gl_renderbuffer(ctx, name) : nullptr;
    if (!r)
        gl_error(ctx, GL_INVALID_OPERATION);
    return r;
}

GLAPI void APIENTRY glNamedRenderbufferStorage(GLuint renderbuffer, GLenum internal, GLsizei w, GLsizei h)
{
    GL_CONTEXT_OR_RETURN();
    if (!renderbuffer_named(ctx, renderbuffer))
        return;
    GLuint saved = ctx->renderbuffer;
    ctx->renderbuffer = renderbuffer;
    glRenderbufferStorage(GL_RENDERBUFFER, internal, w, h);
    ctx->renderbuffer = saved;
}

GLAPI void APIENTRY glNamedRenderbufferStorageMultisample(GLuint renderbuffer, GLsizei samples, GLenum internal, GLsizei w,
                                                          GLsizei h)
{
    GL_CONTEXT_OR_RETURN();
    if (!renderbuffer_named(ctx, renderbuffer))
        return;
    GLuint saved = ctx->renderbuffer;
    ctx->renderbuffer = renderbuffer;
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, internal, w, h);
    ctx->renderbuffer = saved;
}

GLAPI void APIENTRY glGetNamedRenderbufferParameteriv(GLuint renderbuffer, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    if (!renderbuffer_named(ctx, renderbuffer))
        return;
    GLuint saved = ctx->renderbuffer;
    ctx->renderbuffer = renderbuffer;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, pname, params);
    ctx->renderbuffer = saved;
}

/* ---------------------------------------------------------------- framebuffers */

typedef struct FboSave {
    GLuint draw, read;
} FboSave;

static void fbo_push(GlContext *ctx, GLuint draw, GLuint read, FboSave *s)
{
    s->draw = ctx->draw_fbo;
    s->read = ctx->read_fbo;
    ctx->draw_fbo = draw;
    ctx->read_fbo = read;
    ctx->dirty |= DIRTY_PIPELINE | DIRTY_SCISSOR | DIRTY_VIEWPORT;
}

static void fbo_pop(GlContext *ctx, const FboSave *s)
{
    ctx->draw_fbo = s->draw;
    ctx->read_fbo = s->read;
    ctx->dirty |= DIRTY_PIPELINE | DIRTY_SCISSOR | DIRTY_VIEWPORT;
}

/* zero is the default framebuffer, which the commands that read or draw it accept. */
static BOOL framebuffer_named(GlContext *ctx, GLuint name, BOOL allow_default)
{
    if (name ? !gl_framebuffer(ctx, name) : !allow_default) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return FALSE;
    }
    return TRUE;
}

GLAPI void APIENTRY glCreateFramebuffers(GLsizei n, GLuint *framebuffers)
{
    GL_CONTEXT_OR_RETURN();
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    FboSave s;
    fbo_push(ctx, ctx->draw_fbo, ctx->read_fbo, &s);
    for (GLsizei i = 0; i < n; i++) {
        glGenFramebuffers(1, &framebuffers[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[i]);
    }
    fbo_pop(ctx, &s);
}

#define WITH_FRAMEBUFFER(fb, call)                                                                                     \
    do {                                                                                                               \
        FboSave s_;                                                                                                    \
        fbo_push(ctx, (fb), (fb), &s_);                                                                                \
        call;                                                                                                          \
        fbo_pop(ctx, &s_);                                                                                             \
    } while (0)

GLAPI void APIENTRY glNamedFramebufferRenderbuffer(GLuint framebuffer, GLenum attachment, GLenum rbtarget, GLuint renderbuffer)
{
    GL_CONTEXT_OR_RETURN();
    if (!framebuffer_named(ctx, framebuffer, FALSE))
        return;
    if (rbtarget != GL_RENDERBUFFER) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (renderbuffer && !gl_renderbuffer(ctx, renderbuffer)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    WITH_FRAMEBUFFER(framebuffer, glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, rbtarget, renderbuffer));
}

GLAPI void APIENTRY glNamedFramebufferTexture(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level)
{
    GL_CONTEXT_OR_RETURN();
    if (!framebuffer_named(ctx, framebuffer, FALSE))
        return;
    if (texture && !gl_texture(ctx, texture)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    WITH_FRAMEBUFFER(framebuffer, glFramebufferTexture(GL_FRAMEBUFFER, attachment, texture, level));
}

GLAPI void APIENTRY glNamedFramebufferTextureLayer(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level,
                                                   GLint layer)
{
    GL_CONTEXT_OR_RETURN();
    if (!framebuffer_named(ctx, framebuffer, FALSE))
        return;
    if (texture && !gl_texture(ctx, texture)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (layer < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    WITH_FRAMEBUFFER(framebuffer, glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, texture, level, layer));
}

GLAPI void APIENTRY glNamedFramebufferDrawBuffer(GLuint framebuffer, GLenum buf)
{
    GL_CONTEXT_OR_RETURN();
    if (!framebuffer_named(ctx, framebuffer, TRUE))
        return;
    FboSave s;
    fbo_push(ctx, framebuffer, ctx->read_fbo, &s);
    glDrawBuffer(buf);
    fbo_pop(ctx, &s);
}

GLAPI void APIENTRY glNamedFramebufferDrawBuffers(GLuint framebuffer, GLsizei n, const GLenum *bufs)
{
    GL_CONTEXT_OR_RETURN();
    if (!framebuffer_named(ctx, framebuffer, TRUE))
        return;
    if (n < 0 || n > MIME_GPU_MAX_COLORS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    FboSave s;
    fbo_push(ctx, framebuffer, ctx->read_fbo, &s);
    glDrawBuffers(n, bufs);
    fbo_pop(ctx, &s);
}

GLAPI void APIENTRY glNamedFramebufferReadBuffer(GLuint framebuffer, GLenum src)
{
    GL_CONTEXT_OR_RETURN();
    if (!framebuffer_named(ctx, framebuffer, TRUE))
        return;
    FboSave s;
    fbo_push(ctx, ctx->draw_fbo, framebuffer, &s);
    glReadBuffer(src);
    fbo_pop(ctx, &s);
}

GLAPI GLenum APIENTRY glCheckNamedFramebufferStatus(GLuint framebuffer, GLenum target)
{
    GL_CONTEXT_OR_RETURN(0);
    if (!framebuffer_named(ctx, framebuffer, TRUE))
        return 0;
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        gl_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
    GLenum status = 0;
    WITH_FRAMEBUFFER(framebuffer, status = glCheckFramebufferStatus(target));
    return status;
}

GLAPI void APIENTRY glGetNamedFramebufferAttachmentParameteriv(GLuint framebuffer, GLenum attachment, GLenum pname,
                                                               GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    if (!framebuffer_named(ctx, framebuffer, TRUE))
        return;
    WITH_FRAMEBUFFER(framebuffer, glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, attachment, pname, params));
}

GLAPI void APIENTRY glBlitNamedFramebuffer(GLuint read, GLuint draw, GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0,
                                           GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
{
    GL_CONTEXT_OR_RETURN();
    if (!framebuffer_named(ctx, read, TRUE) || !framebuffer_named(ctx, draw, TRUE))
        return;
    FboSave s;
    fbo_push(ctx, draw, read, &s);
    glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
    fbo_pop(ctx, &s);
}

/* the attachments of an invalidate are checked, nothing else happens: the contents stay as they were. */
static BOOL attachments_valid(GlContext *ctx, GLuint framebuffer, GLsizei n, const GLenum *list)
{
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return FALSE;
    }
    for (GLsizei i = 0; i < n; i++) {
        GLenum a = list[i];
        BOOL color = a >= GL_COLOR_ATTACHMENT0 && a < GL_COLOR_ATTACHMENT0 + MIME_GPU_MAX_COLORS;
        BOOL plain = a == GL_DEPTH_ATTACHMENT || a == GL_STENCIL_ATTACHMENT || a == GL_DEPTH_STENCIL_ATTACHMENT;
        BOOL window = a == GL_COLOR || a == GL_DEPTH || a == GL_STENCIL || a == GL_FRONT_LEFT || a == GL_BACK_LEFT;
        if (framebuffer ? !(color || plain) : !(window || a == GL_FRONT || a == GL_BACK)) {
            gl_error(ctx, GL_INVALID_ENUM);
            return FALSE;
        }
    }
    return TRUE;
}

GLAPI void APIENTRY glInvalidateNamedFramebufferData(GLuint framebuffer, GLsizei n, const GLenum *attachments)
{
    GL_CONTEXT_OR_RETURN();
    if (framebuffer_named(ctx, framebuffer, TRUE))
        attachments_valid(ctx, framebuffer, n, attachments);
}

GLAPI void APIENTRY glInvalidateNamedFramebufferSubData(GLuint framebuffer, GLsizei n, const GLenum *attachments, GLint x,
                                                        GLint y, GLsizei w, GLsizei h)
{
    GL_CONTEXT_OR_RETURN();
    if (!framebuffer_named(ctx, framebuffer, TRUE) || !attachments_valid(ctx, framebuffer, n, attachments))
        return;
    if (w < 0 || h < 0)
        gl_error(ctx, GL_INVALID_VALUE);
}

/*
 * a clear of one buffer of a framebuffer: the other draw buffers are left out while glClear runs, so that the clear
 * reaches only the one asked for, and the clear values are the ones given.
 */
static void clear_one(GlContext *ctx, GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat *color,
                      GLdouble depth, GLint stencil)
{
    GlFramebuffer *f = framebuffer ? gl_framebuffer(ctx, framebuffer) : nullptr;
    GLenum saved_draw[MIME_GPU_MAX_COLORS];
    GLfloat saved_color[4];
    GLdouble saved_depth = ctx->clear_depth;
    GLint saved_stencil = ctx->clear_stencil;
    memcpy(saved_color, ctx->clear_color, sizeof(saved_color));
    if (f && buffer == GL_COLOR) {
        memcpy(saved_draw, f->draw_buffers, sizeof(saved_draw));
        for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
            if (i != drawbuffer)
                f->draw_buffers[i] = GL_NONE;
        f->serial++;
    }
    FboSave s;
    fbo_push(ctx, framebuffer, ctx->read_fbo, &s);
    GLbitfield mask = 0;
    if (buffer == GL_COLOR) {
        memcpy(ctx->clear_color, color, sizeof(saved_color));
        mask = GL_COLOR_BUFFER_BIT;
    }
    if (buffer == GL_DEPTH || buffer == GL_DEPTH_STENCIL) {
        ctx->clear_depth = depth;
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (buffer == GL_STENCIL || buffer == GL_DEPTH_STENCIL) {
        ctx->clear_stencil = stencil;
        mask |= GL_STENCIL_BUFFER_BIT;
    }
    glClear(mask);
    fbo_pop(ctx, &s);
    if (f && buffer == GL_COLOR) {
        memcpy(f->draw_buffers, saved_draw, sizeof(saved_draw));
        f->serial++;
    }
    memcpy(ctx->clear_color, saved_color, sizeof(saved_color));
    ctx->clear_depth = saved_depth;
    ctx->clear_stencil = saved_stencil;
}

static BOOL clear_valid(GlContext *ctx, GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLenum *allowed, int count)
{
    if (!framebuffer_named(ctx, framebuffer, TRUE))
        return FALSE;
    if (!target_in(buffer, allowed, count)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return FALSE;
    }
    if (drawbuffer < 0 || drawbuffer >= (buffer == GL_COLOR ? MIME_GPU_MAX_COLORS : 1)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return FALSE;
    }
    return TRUE;
}

GLAPI void APIENTRY glClearNamedFramebufferfv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat *value)
{
    GL_CONTEXT_OR_RETURN();
    if (clear_valid(ctx, framebuffer, buffer, drawbuffer, (const GLenum[]){GL_COLOR, GL_DEPTH}, 2))
        clear_one(ctx, framebuffer, buffer, drawbuffer, value, value[0], 0);
}

GLAPI void APIENTRY glClearNamedFramebufferiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLint *value)
{
    GL_CONTEXT_OR_RETURN();
    if (!clear_valid(ctx, framebuffer, buffer, drawbuffer, (const GLenum[]){GL_COLOR, GL_STENCIL}, 2))
        return;
    GLfloat color[4] = {(GLfloat)value[0], (GLfloat)value[1], (GLfloat)value[2], (GLfloat)value[3]};
    clear_one(ctx, framebuffer, buffer, drawbuffer, color, 0, value[0]);
}

GLAPI void APIENTRY glClearNamedFramebufferuiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLuint *value)
{
    GL_CONTEXT_OR_RETURN();
    if (!clear_valid(ctx, framebuffer, buffer, drawbuffer, (const GLenum[]){GL_COLOR}, 1))
        return;
    GLfloat color[4] = {(GLfloat)value[0], (GLfloat)value[1], (GLfloat)value[2], (GLfloat)value[3]};
    clear_one(ctx, framebuffer, buffer, drawbuffer, color, 0, 0);
}

GLAPI void APIENTRY glClearNamedFramebufferfi(GLuint framebuffer, GLenum buffer, GLint drawbuffer, GLfloat depth,
                                              GLint stencil)
{
    GL_CONTEXT_OR_RETURN();
    if (clear_valid(ctx, framebuffer, buffer, drawbuffer, (const GLenum[]){GL_DEPTH_STENCIL}, 1))
        clear_one(ctx, framebuffer, buffer, 0, nullptr, depth, stencil);
}

/* ---------------------------------------------------------------- framebuffer parameters */

static void set_parameter(GlContext *ctx, GlFramebuffer *f, GLenum pname, GLint param)
{
    if (!f) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    switch (pname) {
    case GL_FRAMEBUFFER_DEFAULT_WIDTH:
    case GL_FRAMEBUFFER_DEFAULT_HEIGHT:
        if (param < 0 || param > 16384) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        *(pname == GL_FRAMEBUFFER_DEFAULT_WIDTH ? &f->default_width : &f->default_height) = param;
        break;
    case GL_FRAMEBUFFER_DEFAULT_LAYERS:
        if (param < 0 || param > 2048) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        f->default_layers = param;
        break;
    case GL_FRAMEBUFFER_DEFAULT_SAMPLES:
        if (param < 0 || param > 4) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        f->default_samples = param;
        break;
    case GL_FRAMEBUFFER_DEFAULT_FIXED_SAMPLE_LOCATIONS:
        f->default_fixed_locations = param != 0;
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    f->serial++;
}

static void get_parameter(GlContext *ctx, GlFramebuffer *f, GLuint name, GLenum pname, GLint *params)
{
    if (!f) {
        if (name) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        switch (pname) {
        case GL_DOUBLEBUFFER:
            *params = (ctx->pfd.dwFlags & PFD_DOUBLEBUFFER) != 0;
            return;
        case GL_STEREO:
        case GL_SAMPLES:
        case GL_SAMPLE_BUFFERS:
            *params = 0;
            return;
        case GL_IMPLEMENTATION_COLOR_READ_FORMAT:
            *params = GL_RGBA;
            return;
        case GL_IMPLEMENTATION_COLOR_READ_TYPE:
            *params = GL_UNSIGNED_BYTE;
            return;
        default:
            gl_error(ctx, GL_INVALID_ENUM);
            return;
        }
    }
    switch (pname) {
    case GL_FRAMEBUFFER_DEFAULT_WIDTH:
        *params = f->default_width;
        break;
    case GL_FRAMEBUFFER_DEFAULT_HEIGHT:
        *params = f->default_height;
        break;
    case GL_FRAMEBUFFER_DEFAULT_LAYERS:
        *params = f->default_layers;
        break;
    case GL_FRAMEBUFFER_DEFAULT_SAMPLES:
        *params = f->default_samples;
        break;
    case GL_FRAMEBUFFER_DEFAULT_FIXED_SAMPLE_LOCATIONS:
        *params = f->default_fixed_locations;
        break;
    case GL_DOUBLEBUFFER:
    case GL_STEREO:
        *params = 0;
        break;
    case GL_SAMPLES:
    case GL_SAMPLE_BUFFERS:
        *params = 0;
        break;
    case GL_IMPLEMENTATION_COLOR_READ_FORMAT:
        *params = GL_RGBA;
        break;
    case GL_IMPLEMENTATION_COLOR_READ_TYPE:
        *params = GL_UNSIGNED_BYTE;
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        break;
    }
}

GLAPI void APIENTRY glFramebufferParameteri(GLenum target, GLenum pname, GLint param)
{
    GL_CONTEXT_OR_RETURN();
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GLuint name = target == GL_READ_FRAMEBUFFER ? ctx->read_fbo : ctx->draw_fbo;
    set_parameter(ctx, name ? gl_framebuffer(ctx, name) : nullptr, pname, param);
}

GLAPI void APIENTRY glGetFramebufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GLuint name = target == GL_READ_FRAMEBUFFER ? ctx->read_fbo : ctx->draw_fbo;
    get_parameter(ctx, name ? gl_framebuffer(ctx, name) : nullptr, name, pname, params);
}

GLAPI void APIENTRY glNamedFramebufferParameteri(GLuint framebuffer, GLenum pname, GLint param)
{
    GL_CONTEXT_OR_RETURN();
    if (framebuffer_named(ctx, framebuffer, FALSE))
        set_parameter(ctx, gl_framebuffer(ctx, framebuffer), pname, param);
}

GLAPI void APIENTRY glGetNamedFramebufferParameteriv(GLuint framebuffer, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    if (framebuffer_named(ctx, framebuffer, TRUE))
        get_parameter(ctx, framebuffer ? gl_framebuffer(ctx, framebuffer) : nullptr, framebuffer, pname, params);
}

/* ---------------------------------------------------------------- queries */

GLAPI void APIENTRY glCreateQueries(GLenum target, GLsizei n, GLuint *ids)
{
    GL_CONTEXT_OR_RETURN();
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    switch (target) {
    case GL_SAMPLES_PASSED:
    case GL_ANY_SAMPLES_PASSED:
    case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
    case GL_PRIMITIVES_GENERATED:
    case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
    case GL_TIME_ELAPSED:
    case GL_TIMESTAMP:
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    glGenQueries(n, ids);
    for (GLsizei i = 0; i < n; i++)
        gl_query_create(ctx, ids[i], target);
}

static void query_buffer_object(GLuint id, GLuint buffer, GLenum pname, GLintptr offset, int bytes, BOOL is_signed)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *b = buffer ? gl_buffer(ctx, buffer) : nullptr;
    if (!glIsQuery(id) || !b) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (pname != GL_QUERY_RESULT && pname != GL_QUERY_RESULT_NO_WAIT && pname != GL_QUERY_RESULT_AVAILABLE &&
        pname != GL_QUERY_TARGET) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (offset < 0 || (offset & (bytes - 1)) || (uint64_t)offset + (uint64_t)bytes > b->size) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GLuint64 value = 0;
    if (pname == GL_QUERY_TARGET) {
        value = GL_TIME_ELAPSED;
    } else if (pname == GL_QUERY_RESULT_NO_WAIT) {
        GLuint64 available = 0;
        glGetQueryObjectui64v(id, GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available)
            return;
        glGetQueryObjectui64v(id, GL_QUERY_RESULT, &value);
    } else {
        glGetQueryObjectui64v(id, pname, &value);
    }
    if (bytes == 4 && !is_signed && value > 0xffffffffu)
        value = 0xffffffffu;
    if (bytes == 4 && is_signed && value > 0x7fffffffu)
        value = 0x7fffffffu;
    gl_buffer_read_ready(b);
    memcpy(b->mem.ptr + offset, &value, (SIZE_T)bytes);
    b->dirty_index_range = TRUE;
    ctx->dirty |= DIRTY_VERTEX;
}

GLAPI void APIENTRY glGetQueryBufferObjecti64v(GLuint id, GLuint buffer, GLenum pname, GLintptr offset)
{
    query_buffer_object(id, buffer, pname, offset, 8, TRUE);
}

GLAPI void APIENTRY glGetQueryBufferObjectiv(GLuint id, GLuint buffer, GLenum pname, GLintptr offset)
{
    query_buffer_object(id, buffer, pname, offset, 4, TRUE);
}

GLAPI void APIENTRY glGetQueryBufferObjectui64v(GLuint id, GLuint buffer, GLenum pname, GLintptr offset)
{
    query_buffer_object(id, buffer, pname, offset, 8, FALSE);
}

GLAPI void APIENTRY glGetQueryBufferObjectuiv(GLuint id, GLuint buffer, GLenum pname, GLintptr offset)
{
    query_buffer_object(id, buffer, pname, offset, 4, FALSE);
}
