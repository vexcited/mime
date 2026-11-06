/*
 * buffer textures, GL_ARB_texture_buffer_object and GL_ARB_texture_buffer_range: a texture whose texels are a range of a
 * buffer object. the host reads the heap memory the buffer has, so the host texture is made again whenever that memory is
 * not the one it was made over, after glBufferData gave the buffer new memory for instance.
 */
#include "opengl32.h"

GlTexture *gl_texture_bound(GlContext *ctx, GLenum target);

#define TEXTURE_BUFFER_ALIGNMENT 256
#define MAX_TEXTURE_BUFFER_TEXELS (1 << 27)

/* the Metal format of each internal format a buffer texture takes, 0 for another one. */
static uint32_t buffer_format(GLenum internal)
{
    switch (internal) {
    case GL_R8: return 10;
    case GL_R8UI: return 13;
    case GL_R8I: return 14;
    case GL_R16: return 20;
    case GL_R16UI: return 23;
    case GL_R16I: return 24;
    case GL_R16F: return 25;
    case GL_RG8: return 30;
    case GL_RG8UI: return 33;
    case GL_RG8I: return 34;
    case GL_R32UI: return 53;
    case GL_R32I: return 54;
    case GL_R32F: return 55;
    case GL_RG16: return 60;
    case GL_RG16UI: return 63;
    case GL_RG16I: return 64;
    case GL_RG16F: return 65;
    case GL_RGBA8: return 70;
    case GL_RGBA8UI: return 73;
    case GL_RGBA8I: return 74;
    case GL_RG32UI: return 103;
    case GL_RG32I: return 104;
    case GL_RG32F: return 105;
    case GL_RGBA16: return 110;
    case GL_RGBA16UI: return 113;
    case GL_RGBA16I: return 114;
    case GL_RGBA16F: return 115;
    case GL_RGBA32UI: return 123;
    case GL_RGBA32I: return 124;
    case GL_RGBA32F: return 125;
    default: return 0;
    }
}

int gl_texbuffer_get(GlContext *ctx, GLenum pname, GLdouble *v)
{
    if (!gl_host_has(MIME_GPU_FEATURE_TEXEL_BUFFER))
        return 0;
    switch (pname) {
    case GL_MAX_TEXTURE_BUFFER_SIZE:
        v[0] = MAX_TEXTURE_BUFFER_TEXELS;
        return 1;
    case GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
        v[0] = TEXTURE_BUFFER_ALIGNMENT;
        return 1;
    case GL_TEXTURE_BINDING_BUFFER: {
        GlTexture *t = gl_texture_bound(ctx, GL_TEXTURE_BUFFER);
        v[0] = t ? t->name : 0;
        return 1;
    }
    case GL_TEXTURE_BUFFER_BINDING:
        v[0] = ctx->texture_buffer;
        return 1;
    default:
        return 0;
    }
}

/* the part of a buffer texture's range that the buffer has, in whole texels. */
static uint64_t range_of(const GlTexture *t, const GlBuffer *b, uint64_t *offset)
{
    *offset = (uint64_t)t->tb.offset;
    if (*offset >= b->size)
        return 0;
    uint64_t size = t->tb.ranged ? (uint64_t)t->tb.size : b->size - *offset;
    if (size > b->size - *offset)
        size = b->size - *offset;
    uint32_t texel = gl_mtl_texel_bytes(t->tb.mtl);
    return texel ? size - size % texel : 0;
}

/* makes the host texture fit the buffer as it is now. FALSE when there is nothing to read. */
BOOL gl_texbuffer_ready(GlContext *ctx, GlTexture *t)
{
    GlBuffer *b = t->tb.buffer ? gl_buffer(ctx, t->tb.buffer) : nullptr;
    if (!b || !b->mem.ptr || !t->tb.mtl)
        return FALSE;
    uint64_t offset, size = range_of(t, b, &offset);
    if (!size)
        return FALSE;
    GlMem mem = b->mem;
    mem.offset += offset;
    if (!t->id || t->tb.host.block != mem.block || t->tb.host.offset != mem.offset || t->tb.host.size != size ||
        t->tb.host_mtl != t->tb.mtl) {
        uint32_t id = gl_new_id(ID_TEXTURE);
        gl_device_lock();
        MimeGpuTextureBuffer *c = gl_emit(MIME_GPU_TEXTURE_BUFFER, sizeof(MimeGpuTextureBuffer));
        c->id = id;
        c->format = t->tb.mtl;
        c->mem = gl_mem(&mem);
        c->size = size;
        if (t->id) {
            MimeGpuObject *o = gl_emit(MIME_GPU_TEXTURE_DESTROY, sizeof(MimeGpuObject));
            o->id = t->id;
        }
        gl_device_unlock();
        gl_free_id(ID_TEXTURE, t->id);
        t->id = id;
        t->tb.host = mem;
        t->tb.host.size = size;
        t->tb.host.ptr = nullptr;
        t->tb.host_mtl = t->tb.mtl;
    }
    b->last_use = gl_batch_fence();
    return TRUE;
}

BOOL gl_texbuffer_param(GlContext *ctx, const GlTexture *t, GLenum pname, GLint *value)
{
    switch (pname) {
    case GL_TEXTURE_BUFFER_DATA_STORE_BINDING:
        *value = (GLint)t->tb.buffer;
        return TRUE;
    case GL_TEXTURE_BUFFER_OFFSET:
        *value = (GLint)t->tb.offset;
        return TRUE;
    case GL_TEXTURE_BUFFER_SIZE:
    {
        GlBuffer *b = t->tb.buffer ? gl_buffer(ctx, t->tb.buffer) : nullptr;
        *value = t->tb.ranged ? (GLint)t->tb.size : b ? (GLint)b->size : 0;
        return TRUE;
    }
    default:
        return FALSE;
    }
}

/* the specification's glTexBuffer and glTexBufferRange on a texture that is known to be a buffer texture. */
static void attach(GlContext *ctx, GlTexture *t, GLenum internal, GLuint buffer, GLintptr offset, GLsizeiptr size,
                   BOOL ranged)
{
    if (!buffer) {
        t->tb = (typeof(t->tb)){};
        t->width = 0;
        t->height = t->depth = 1;
        t->internal_format = GL_R8;
        t->defined_levels = 0;
        return;
    }
    uint32_t mtl = buffer_format(internal);
    GlBuffer *b = gl_buffer(ctx, buffer);
    if (!b) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (ranged && (offset < 0 || size <= 0 || offset % TEXTURE_BUFFER_ALIGNMENT || (uint64_t)offset > b->size ||
                   (uint64_t)size > b->size - (uint64_t)offset)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    t->tb.buffer = buffer;
    t->tb.format = internal;
    t->tb.mtl = mtl;
    t->tb.offset = ranged ? offset : 0;
    t->tb.size = ranged ? size : 0;
    t->tb.ranged = ranged;
    t->internal_format = internal;
    t->mtl_format = mtl;
    uint64_t start, bytes = range_of(t, b, &start);
    t->width = (uint32_t)(bytes / gl_mtl_texel_bytes(mtl));
    t->height = t->depth = 1;
    t->defined_levels = 1;
    ctx->dirty |= DIRTY_TEXTURES;
}

static void texbuffer(GlContext *ctx, GlTexture *t, GLenum internal, GLuint buffer, GLintptr offset, GLsizeiptr size,
                      BOOL ranged)
{
    if (!buffer_format(internal)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    attach(ctx, t, internal, buffer, offset, size, ranged);
}

GLAPI void APIENTRY glTexBuffer(GLenum target, GLenum internal, GLuint buffer)
{
    GL_CONTEXT_OR_RETURN();
    if (target != GL_TEXTURE_BUFFER || !gl_host_has(MIME_GPU_FEATURE_TEXEL_BUFFER)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlTexture *t = gl_texture_bound(ctx, target);
    if (!t) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    texbuffer(ctx, t, internal, buffer, 0, 0, FALSE);
}

GLAPI void APIENTRY glTexBufferRange(GLenum target, GLenum internal, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
    GL_CONTEXT_OR_RETURN();
    if (target != GL_TEXTURE_BUFFER || !gl_host_has(MIME_GPU_FEATURE_TEXEL_BUFFER)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlTexture *t = gl_texture_bound(ctx, target);
    if (!t) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    texbuffer(ctx, t, internal, buffer, offset, size, TRUE);
}

GLAPI void APIENTRY glTextureBuffer(GLuint texture, GLenum internal, GLuint buffer)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = gl_texture(ctx, texture);
    if (!t || t->target != GL_TEXTURE_BUFFER) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    texbuffer(ctx, t, internal, buffer, 0, 0, FALSE);
}

GLAPI void APIENTRY glTextureBufferRange(GLuint texture, GLenum internal, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
    GL_CONTEXT_OR_RETURN();
    GlTexture *t = gl_texture(ctx, texture);
    if (!t || t->target != GL_TEXTURE_BUFFER) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    texbuffer(ctx, t, internal, buffer, offset, size, TRUE);
}
