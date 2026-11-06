/*
 * separate vertex attribute format and binding, as ARB_vertex_attrib_binding has it, and the vertex array commands that
 * take the object by name. an attribute holds its format and points at a binding index; the binding holds the buffer,
 * offset, stride and divisor. the draw code reads the attribute, so a change of either side is copied to the
 * attributes it reaches. glVertexAttribPointer and glVertexAttribDivisor are the specification's shorthand for the
 * format and the binding of the same index and call back here.
 */
#define GL_GLEXT_PROTOTYPES
#include "opengl32.h"

#define MAX_BINDINGS MIME_GPU_MAX_ATTRIBS
#define MAX_RELATIVE_OFFSET 2047
#define MAX_STRIDE 2048

GlVertexArray *gl_vao_get(GlContext *ctx, GLuint name)
{
    if (!name)
        return &ctx->default_vao;
    void *v = gl_name_get(&ctx->vaos, name);
    return v == GL_RESERVED ? nullptr : v;
}

static GLuint binding_of(const GlVertexArray *v, GLuint index)
{
    return v->attrib_binding[index] ? (GLuint)v->attrib_binding[index] - 1 : index;
}

/* copies what the attribute's binding holds into the attribute. */
static void resolve(GlVertexArray *v, GLuint index)
{
    GlVertexAttrib *a = &v->attribs[index];
    const GlVertexBinding *b = &v->bindings[binding_of(v, index)];
    a->buffer = b->buffer;
    a->pointer = (const void *)((ULONG_PTR)b->offset + v->relative_offset[index]);
    a->stride = b->stride;
    a->divisor = b->divisor;
}

static void resolve_binding(GlVertexArray *v, GLuint binding)
{
    for (GLuint i = 0; i < MIME_GPU_MAX_ATTRIBS; i++)
        if (binding_of(v, i) == binding)
            resolve(v, i);
}

void gl_vao_legacy_pointer(GlVertexArray *v, GLuint index)
{
    GlVertexAttrib *a = &v->attribs[index];
    v->attrib_binding[index] = (uint8_t)(index + 1);
    v->relative_offset[index] = 0;
    GlVertexBinding *b = &v->bindings[index];
    b->buffer = a->buffer;
    b->offset = (GLintptr)(ULONG_PTR)a->pointer;
    b->stride = a->stride;
    resolve_binding(v, index);
}

void gl_vao_legacy_divisor(GlVertexArray *v, GLuint index)
{
    v->attrib_binding[index] = (uint8_t)(index + 1);
    v->bindings[index].divisor = v->attribs[index].divisor;
    resolve_binding(v, index);
}

/* the commands that change a vertex array object other than the default one in a core context. */
static BOOL usable(GlContext *ctx, const GlVertexArray *v)
{
    if (v == &ctx->default_vao && ctx->profile == WGL_CONTEXT_CORE_PROFILE_BIT_ARB) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return FALSE;
    }
    return TRUE;
}

static BOOL packed_type(GLenum type)
{
    return type == GL_INT_2_10_10_10_REV || type == GL_UNSIGNED_INT_2_10_10_10_REV || type == GL_UNSIGNED_INT_10F_11F_11F_REV;
}

static BOOL set_format(GlContext *ctx, GlVertexArray *v, GLuint index, GLint size, GLenum type, GLboolean normalized,
                       BOOL integer, BOOL longs, GLuint relative)
{
    if (!usable(ctx, v))
        return FALSE;
    if (index >= MIME_GPU_MAX_ATTRIBS || relative > MAX_RELATIVE_OFFSET || ((size < 1 || size > 4) && size != GL_BGRA)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return FALSE;
    }
    BOOL ok;
    if (longs) {
        ok = type == GL_DOUBLE;
    } else if (integer) {
        ok = type == GL_BYTE || type == GL_UNSIGNED_BYTE || type == GL_SHORT || type == GL_UNSIGNED_SHORT ||
             type == GL_INT || type == GL_UNSIGNED_INT;
    } else {
        ok = type == GL_BYTE || type == GL_UNSIGNED_BYTE || type == GL_SHORT || type == GL_UNSIGNED_SHORT ||
             type == GL_INT || type == GL_UNSIGNED_INT || type == GL_HALF_FLOAT || type == GL_FLOAT || type == GL_DOUBLE ||
             type == GL_FIXED || packed_type(type);
    }
    if (!ok) {
        gl_error(ctx, GL_INVALID_ENUM);
        return FALSE;
    }
    if (size == GL_BGRA && (integer || longs || (type != GL_UNSIGNED_BYTE && type != GL_INT_2_10_10_10_REV &&
                                                  type != GL_UNSIGNED_INT_2_10_10_10_REV))) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return FALSE;
    }
    if (packed_type(type) && size != 4 && size != GL_BGRA) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return FALSE;
    }
    GlVertexAttrib *a = &v->attribs[index];
    a->size = size;
    a->type = type;
    a->normalized = normalized;
    a->integer = integer;
    a->is_long = longs;
    v->long_mask = (uint16_t)((v->long_mask & ~(1u << index)) | (longs ? 1u << index : 0));
    v->relative_offset[index] = (uint16_t)relative;
    resolve(v, index);
    ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
    return TRUE;
}

static void set_attrib_binding(GlContext *ctx, GlVertexArray *v, GLuint index, GLuint binding)
{
    if (!usable(ctx, v))
        return;
    if (index >= MIME_GPU_MAX_ATTRIBS || binding >= MAX_BINDINGS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    v->attrib_binding[index] = (uint8_t)(binding + 1);
    resolve(v, index);
    ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
}

static BOOL set_buffer(GlContext *ctx, GlVertexArray *v, GLuint binding, GLuint buffer, GLintptr offset, GLsizei stride)
{
    if (binding >= MAX_BINDINGS || offset < 0 || stride < 0 || stride > MAX_STRIDE) {
        gl_error(ctx, GL_INVALID_VALUE);
        return FALSE;
    }
    GlVertexBinding *b = &v->bindings[binding];
    b->buffer = buffer;
    b->offset = offset;
    b->stride = stride;
    resolve_binding(v, binding);
    ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
    return TRUE;
}

/* the buffers must all be zero or existing ones, before any binding changes. */
static BOOL buffers_exist(GlContext *ctx, GLsizei count, const GLuint *buffers)
{
    for (GLsizei i = 0; buffers && i < count; i++)
        if (buffers[i] && !gl_buffer(ctx, buffers[i])) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return FALSE;
        }
    return TRUE;
}

static void bind_buffers(GlContext *ctx, GlVertexArray *v, GLuint first, GLsizei count, const GLuint *buffers,
                         const GLintptr *offsets, const GLsizei *strides)
{
    if (!usable(ctx, v))
        return;
    if (count < 0 || first + (GLuint)count > MAX_BINDINGS) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (!buffers_exist(ctx, count, buffers))
        return;
    for (GLsizei i = 0; buffers && i < count; i++)
        if (offsets[i] < 0 || strides[i] < 0 || strides[i] > MAX_STRIDE) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
    for (GLsizei i = 0; i < count; i++)
        set_buffer(ctx, v, first + (GLuint)i, buffers ? buffers[i] : 0, buffers ? offsets[i] : 0, buffers ? strides[i] : 16);
}

static void set_divisor(GlContext *ctx, GlVertexArray *v, GLuint binding, GLuint divisor)
{
    if (!usable(ctx, v))
        return;
    if (binding >= MAX_BINDINGS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    v->bindings[binding].divisor = divisor;
    resolve_binding(v, binding);
    ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
}

/* ---------------------------------------------------------------- the bound vertex array */

GLAPI void APIENTRY glVertexAttribFormat(GLuint index, GLint size, GLenum type, GLboolean normalized, GLuint relative)
{
    GL_CONTEXT_OR_RETURN();
    set_format(ctx, ctx->vao, index, size, type, normalized, FALSE, FALSE, relative);
}

GLAPI void APIENTRY glVertexAttribIFormat(GLuint index, GLint size, GLenum type, GLuint relative)
{
    GL_CONTEXT_OR_RETURN();
    set_format(ctx, ctx->vao, index, size, type, GL_FALSE, TRUE, FALSE, relative);
}

GLAPI void APIENTRY glVertexAttribLFormat(GLuint index, GLint size, GLenum type, GLuint relative)
{
    GL_CONTEXT_OR_RETURN();
    set_format(ctx, ctx->vao, index, size, type, GL_FALSE, FALSE, TRUE, relative);
}

GLAPI void APIENTRY glVertexAttribBinding(GLuint index, GLuint binding)
{
    GL_CONTEXT_OR_RETURN();
    set_attrib_binding(ctx, ctx->vao, index, binding);
}

GLAPI void APIENTRY glBindVertexBuffer(GLuint binding, GLuint buffer, GLintptr offset, GLsizei stride)
{
    GL_CONTEXT_OR_RETURN();
    if (!usable(ctx, ctx->vao) || !buffers_exist(ctx, 1, &buffer))
        return;
    set_buffer(ctx, ctx->vao, binding, buffer, offset, stride);
}

GLAPI void APIENTRY glBindVertexBuffers(GLuint first, GLsizei count, const GLuint *buffers, const GLintptr *offsets,
                                        const GLsizei *strides)
{
    GL_CONTEXT_OR_RETURN();
    bind_buffers(ctx, ctx->vao, first, count, buffers, offsets, strides);
}

GLAPI void APIENTRY glVertexBindingDivisor(GLuint binding, GLuint divisor)
{
    GL_CONTEXT_OR_RETURN();
    set_divisor(ctx, ctx->vao, binding, divisor);
}

/* ---------------------------------------------------------------- vertex arrays by name */

GLAPI void APIENTRY glCreateVertexArrays(GLsizei n, GLuint *arrays)
{
    GL_CONTEXT_OR_RETURN();
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < n; i++) {
        GLuint name = gl_name_new(&ctx->vaos);
        GlVertexArray *v = gl_alloc(sizeof(GlVertexArray));
        if (!v) {
            gl_error(ctx, GL_OUT_OF_MEMORY);
            arrays[i] = 0;
            continue;
        }
        v->name = name;
        gl_name_set(&ctx->vaos, name, v);
        arrays[i] = name;
    }
}

/* a name made by glCreateVertexArrays or bound before; 0 is the default array only for the queries. */
static GlVertexArray *named(GlContext *ctx, GLuint name)
{
    GlVertexArray *v = name ? gl_vao_get(ctx, name) : nullptr;
    if (!v)
        gl_error(ctx, GL_INVALID_OPERATION);
    return v;
}

GLAPI void APIENTRY glEnableVertexArrayAttrib(GLuint array, GLuint index)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = named(ctx, array);
    if (!v)
        return;
    if (index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    v->attribs[index].enabled = TRUE;
    ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
}

GLAPI void APIENTRY glDisableVertexArrayAttrib(GLuint array, GLuint index)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = named(ctx, array);
    if (!v)
        return;
    if (index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    v->attribs[index].enabled = FALSE;
    ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
}

GLAPI void APIENTRY glVertexArrayElementBuffer(GLuint array, GLuint buffer)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = named(ctx, array);
    if (!v)
        return;
    if (buffer && !gl_buffer(ctx, buffer)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    v->element_buffer = buffer;
    ctx->dirty |= DIRTY_VERTEX;
}

GLAPI void APIENTRY glVertexArrayVertexBuffer(GLuint array, GLuint binding, GLuint buffer, GLintptr offset, GLsizei stride)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = named(ctx, array);
    if (v && buffers_exist(ctx, 1, &buffer))
        set_buffer(ctx, v, binding, buffer, offset, stride);
}

GLAPI void APIENTRY glVertexArrayVertexBuffers(GLuint array, GLuint first, GLsizei count, const GLuint *buffers,
                                               const GLintptr *offsets, const GLsizei *strides)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = named(ctx, array);
    if (v)
        bind_buffers(ctx, v, first, count, buffers, offsets, strides);
}

GLAPI void APIENTRY glVertexArrayAttribFormat(GLuint array, GLuint index, GLint size, GLenum type, GLboolean normalized,
                                              GLuint relative)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = named(ctx, array);
    if (v)
        set_format(ctx, v, index, size, type, normalized, FALSE, FALSE, relative);
}

GLAPI void APIENTRY glVertexArrayAttribIFormat(GLuint array, GLuint index, GLint size, GLenum type, GLuint relative)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = named(ctx, array);
    if (v)
        set_format(ctx, v, index, size, type, GL_FALSE, TRUE, FALSE, relative);
}

GLAPI void APIENTRY glVertexArrayAttribLFormat(GLuint array, GLuint index, GLint size, GLenum type, GLuint relative)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = named(ctx, array);
    if (v)
        set_format(ctx, v, index, size, type, GL_FALSE, FALSE, TRUE, relative);
}

GLAPI void APIENTRY glVertexArrayAttribBinding(GLuint array, GLuint index, GLuint binding)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = named(ctx, array);
    if (v)
        set_attrib_binding(ctx, v, index, binding);
}

GLAPI void APIENTRY glVertexArrayBindingDivisor(GLuint array, GLuint binding, GLuint divisor)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = named(ctx, array);
    if (v)
        set_divisor(ctx, v, binding, divisor);
}

GLAPI void APIENTRY glGetVertexArrayiv(GLuint array, GLenum pname, GLint *param)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = named(ctx, array);
    if (!v)
        return;
    if (pname != GL_ELEMENT_ARRAY_BUFFER_BINDING) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    *param = (GLint)v->element_buffer;
}

static BOOL indexed(GlContext *ctx, GLuint array, GLuint index, GLenum pname, GLint64 *value)
{
    GlVertexArray *v = named(ctx, array);
    if (!v)
        return FALSE;
    if (index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return FALSE;
    }
    const GlVertexAttrib *a = &v->attribs[index];
    switch (pname) {
    case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
        *value = a->enabled;
        return TRUE;
    case GL_VERTEX_ATTRIB_ARRAY_SIZE:
        *value = a->size ? a->size : 4;
        return TRUE;
    case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
        *value = a->stride;
        return TRUE;
    case GL_VERTEX_ATTRIB_ARRAY_TYPE:
        *value = a->type ? (GLint)a->type : GL_FLOAT;
        return TRUE;
    case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
        *value = a->normalized;
        return TRUE;
    case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
        *value = a->integer;
        return TRUE;
    case GL_VERTEX_ATTRIB_ARRAY_LONG:
        *value = (v->long_mask >> index) & 1;
        return TRUE;
    case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
        *value = a->divisor;
        return TRUE;
    case GL_VERTEX_ATTRIB_BINDING:
        *value = binding_of(v, index);
        return TRUE;
    case GL_VERTEX_ATTRIB_RELATIVE_OFFSET:
        *value = v->relative_offset[index];
        return TRUE;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return FALSE;
    }
}

GLAPI void APIENTRY glGetVertexArrayIndexediv(GLuint array, GLuint index, GLenum pname, GLint *param)
{
    GL_CONTEXT_OR_RETURN();
    GLint64 value;
    if (indexed(ctx, array, index, pname, &value))
        *param = (GLint)value;
}

GLAPI void APIENTRY glGetVertexArrayIndexed64iv(GLuint array, GLuint index, GLenum pname, GLint64 *param)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = named(ctx, array);
    if (!v)
        return;
    if (pname != GL_VERTEX_BINDING_OFFSET) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (index >= MAX_BINDINGS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    *param = v->bindings[index].offset;
}

/* the state the indexed gets of vertex bindings answer. */
BOOL gl_vertex_binding_get(GlContext *ctx, GLenum pname, GLuint index, GLint64 *value)
{
    if (index >= MAX_BINDINGS)
        return FALSE;
    const GlVertexBinding *b = &ctx->vao->bindings[index];
    switch (pname) {
    case GL_VERTEX_BINDING_DIVISOR:
        *value = b->divisor;
        return TRUE;
    case GL_VERTEX_BINDING_OFFSET:
        *value = b->offset;
        return TRUE;
    case GL_VERTEX_BINDING_STRIDE:
        *value = b->stride;
        return TRUE;
    case GL_VERTEX_BINDING_BUFFER:
        *value = b->buffer;
        return TRUE;
    default:
        return FALSE;
    }
}
