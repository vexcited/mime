/*
 * vertex arrays and draws.
 *
 * a draw sends only what changed since the last one: the pipeline, made of program, vertex formats, blending and
 * targets and deduplicated by content, then depth and stencil, rasterizer state, viewport, scissor, uniforms, textures
 * and vertex buffers. each attribute location has its own vertex buffer slot. buffer objects are bound in place;
 * client arrays, unaligned data and doubles are copied to the stream for the vertices the draw reads, and primitives
 * Metal lacks become indexed triangles or line strips.
 */
#include "opengl32.h"
#include "dlist.h"
#include "fixed.h"

void gl_invalidate_bindings(GlContext *ctx)
{
    memset(ctx->bound_tex, 0xFF, sizeof(ctx->bound_tex));
    memset(ctx->bound_vb, 0xFF, sizeof(ctx->bound_vb));
    memset(&ctx->pipeline_desc, 0, sizeof(ctx->pipeline_desc));
    ctx->pipeline_id = 0;
    ctx->depth_id = 0;
    ctx->uniforms_of = nullptr;
}

/* ---------------------------------------------------------------- vertex arrays */

static GlVertexArray *vao(GlContext *ctx, GLuint name)
{
    void *v = gl_name_get(&ctx->vaos, name);
    return v == GL_RESERVED ? nullptr : v;
}

GLAPI void APIENTRY glGenVertexArrays(GLsizei n, GLuint *arrays)
{
    GL_CONTEXT_OR_RETURN();
    for (GLsizei i = 0; i < n; i++)
        arrays[i] = gl_name_new(&ctx->vaos);
}

GLAPI void APIENTRY glBindVertexArray(GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexArray *v = &ctx->default_vao;
    if (name) {
        v = vao(ctx, name);
        if (!v) {
            if (!gl_name_get(&ctx->vaos, name)) {
                gl_error(ctx, GL_INVALID_OPERATION);
                return;
            }
            v = gl_alloc(sizeof(GlVertexArray));
            if (!v)
                return;
            v->name = name;
            gl_name_set(&ctx->vaos, name, v);
        }
    }
    if (ctx->vao != v) {
        ctx->vao = v;
        ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
    }
}

GLAPI void APIENTRY glDeleteVertexArrays(GLsizei n, const GLuint *arrays)
{
    GL_CONTEXT_OR_RETURN();
    for (GLsizei i = 0; i < n; i++) {
        void *o = gl_name_get(&ctx->vaos, arrays[i]);
        if (!o)
            continue;
        if (o == ctx->vao)
            glBindVertexArray(0);
        if (o != GL_RESERVED)
            gl_free(o);
        gl_name_set(&ctx->vaos, arrays[i], nullptr);
    }
}

GLAPI GLboolean APIENTRY glIsVertexArray(GLuint name)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    return vao(ctx, name) ? GL_TRUE : GL_FALSE;
}

static void attrib_pointer(GLuint index, GLint size, GLenum type, BOOL normalized, BOOL integer, GLsizei stride,
                           const void *pointer)
{
    GL_CONTEXT_OR_RETURN();
    if (index >= MIME_GPU_MAX_ATTRIBS || stride < 0 || ((size < 1 || size > 4) && size != GL_BGRA)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (integer && size == GL_BGRA) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (integer && type != GL_BYTE && type != GL_UNSIGNED_BYTE && type != GL_SHORT &&
        type != GL_UNSIGNED_SHORT && type != GL_INT && type != GL_UNSIGNED_INT) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlVertexAttrib *a = &ctx->vao->attribs[index];
    a->size = size;
    a->type = type;
    a->normalized = normalized;
    a->integer = integer;
    a->is_long = FALSE;
    a->stride = stride;
    a->pointer = pointer;
    a->buffer = ctx->array_buffer;
    gl_vao_legacy_pointer(ctx->vao, index);
    ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
}

GLAPI void APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                                          const void *pointer)
{
    attrib_pointer(index, size, type, normalized, FALSE, stride, pointer);
}

GLAPI void APIENTRY glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    attrib_pointer(index, size, type, FALSE, TRUE, stride, pointer);
}

GLAPI void APIENTRY glVertexAttribLPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    GL_CONTEXT_OR_RETURN();
    if (type != GL_DOUBLE) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    attrib_pointer(index, size, type, FALSE, FALSE, stride, pointer);
    if (index < MIME_GPU_MAX_ATTRIBS && size >= 1 && size <= 4)
        ctx->vao->attribs[index].is_long = TRUE;
}

GLAPI void APIENTRY glEnableVertexAttribArray(GLuint index)
{
    GL_CONTEXT_OR_RETURN();
    if (index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->vao->attribs[index].enabled = TRUE;
    ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
}

GLAPI void APIENTRY glDisableVertexAttribArray(GLuint index)
{
    GL_CONTEXT_OR_RETURN();
    if (index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->vao->attribs[index].enabled = FALSE;
    ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
}

GLAPI void APIENTRY glVertexAttribDivisor(GLuint index, GLuint divisor)
{
    GL_CONTEXT_OR_RETURN();
    if (index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->vao->attribs[index].divisor = divisor;
    gl_vao_legacy_divisor(ctx->vao, index);
    ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
}

static void generic(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    GL_CONTEXT_OR_RETURN();
    if (index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->generic_type[index] = GL_FLOAT;
    ctx->generic[index][0] = x;
    ctx->generic[index][1] = y;
    ctx->generic[index][2] = z;
    ctx->generic[index][3] = w;
    ctx->dirty |= DIRTY_VERTEX;
}

GLAPI void APIENTRY glVertexAttrib1f(GLuint i, GLfloat x)
{
    generic(i, x, 0, 0, 1);
}

GLAPI void APIENTRY glVertexAttrib2f(GLuint i, GLfloat x, GLfloat y)
{
    generic(i, x, y, 0, 1);
}

GLAPI void APIENTRY glVertexAttrib3f(GLuint i, GLfloat x, GLfloat y, GLfloat z)
{
    generic(i, x, y, z, 1);
}

GLAPI void APIENTRY glVertexAttrib4f(GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    generic(i, x, y, z, w);
}

GLAPI void APIENTRY glVertexAttrib1fv(GLuint i, const GLfloat *v)
{
    generic(i, v[0], 0, 0, 1);
}

GLAPI void APIENTRY glVertexAttrib2fv(GLuint i, const GLfloat *v)
{
    generic(i, v[0], v[1], 0, 1);
}

GLAPI void APIENTRY glVertexAttrib3fv(GLuint i, const GLfloat *v)
{
    generic(i, v[0], v[1], v[2], 1);
}

GLAPI void APIENTRY glVertexAttrib4fv(GLuint i, const GLfloat *v)
{
    generic(i, v[0], v[1], v[2], v[3]);
}

GLAPI void APIENTRY glVertexAttrib4Nub(GLuint i, GLubyte x, GLubyte y, GLubyte z, GLubyte w)
{
    generic(i, x / 255.0f, y / 255.0f, z / 255.0f, w / 255.0f);
}

static void generic_integer(GLuint index, GLenum type, GLuint x, GLuint y, GLuint z, GLuint w)
{
    GL_CONTEXT_OR_RETURN();
    if (index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->generic_type[index] = type;
    ctx->generic_integer[index][0] = x;
    ctx->generic_integer[index][1] = y;
    ctx->generic_integer[index][2] = z;
    ctx->generic_integer[index][3] = w;
    ctx->dirty |= DIRTY_VERTEX;
}

static double generic_component(const GlContext *ctx, GLuint index, unsigned int component)
{
    if (ctx->generic_type[index] == GL_INT)
        return (GLint)ctx->generic_integer[index][component];
    if (ctx->generic_type[index] == GL_UNSIGNED_INT)
        return ctx->generic_integer[index][component];
    return ctx->generic[index][component];
}

GLAPI void APIENTRY glVertexAttribI4i(GLuint i, GLint x, GLint y, GLint z, GLint w)
{
    generic_integer(i, GL_INT, (GLuint)x, (GLuint)y, (GLuint)z, (GLuint)w);
}

GLAPI void APIENTRY glVertexAttribI4ui(GLuint i, GLuint x, GLuint y, GLuint z, GLuint w)
{
    generic_integer(i, GL_UNSIGNED_INT, x, y, z, w);
}

GLAPI void APIENTRY glVertexAttribI4iv(GLuint i, const GLint *v)
{
    glVertexAttribI4i(i, v[0], v[1], v[2], v[3]);
}

GLAPI void APIENTRY glVertexAttribI4uiv(GLuint i, const GLuint *v)
{
    glVertexAttribI4ui(i, v[0], v[1], v[2], v[3]);
}

GLAPI void APIENTRY glVertexAttribL1d(GLuint i, GLdouble x)
{
    generic(i, (GLfloat)x, 0, 0, 1);
}

GLAPI void APIENTRY glVertexAttribL2d(GLuint i, GLdouble x, GLdouble y)
{
    generic(i, (GLfloat)x, (GLfloat)y, 0, 1);
}

GLAPI void APIENTRY glVertexAttribL3d(GLuint i, GLdouble x, GLdouble y, GLdouble z)
{
    generic(i, (GLfloat)x, (GLfloat)y, (GLfloat)z, 1);
}

GLAPI void APIENTRY glVertexAttribL4d(GLuint i, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
    generic(i, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

GLAPI void APIENTRY glVertexAttribL1dv(GLuint i, const GLdouble *v)
{
    glVertexAttribL1d(i, v[0]);
}

GLAPI void APIENTRY glVertexAttribL2dv(GLuint i, const GLdouble *v)
{
    glVertexAttribL2d(i, v[0], v[1]);
}

GLAPI void APIENTRY glVertexAttribL3dv(GLuint i, const GLdouble *v)
{
    glVertexAttribL3d(i, v[0], v[1], v[2]);
}

GLAPI void APIENTRY glVertexAttribL4dv(GLuint i, const GLdouble *v)
{
    glVertexAttribL4d(i, v[0], v[1], v[2], v[3]);
}

GLAPI void APIENTRY glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    if (index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GlVertexAttrib *a = &ctx->vao->attribs[index];
    switch (pname) {
    case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
        *params = a->enabled;
        break;
    case GL_VERTEX_ATTRIB_ARRAY_SIZE:
        *params = a->size ? a->size : 4;
        break;
    case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
        *params = a->stride;
        break;
    case GL_VERTEX_ATTRIB_ARRAY_TYPE:
        *params = a->type ? (GLint)a->type : GL_FLOAT;
        break;
    case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
        *params = a->normalized;
        break;
    case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
        *params = a->integer;
        break;
    case GL_VERTEX_ATTRIB_ARRAY_LONG:
        *params = a->is_long;
        break;
    case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
        *params = (GLint)a->divisor;
        break;
    case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
        *params = (GLint)a->buffer;
        break;
    case GL_VERTEX_ATTRIB_BINDING:
        *params = ctx->vao->attrib_binding[index] ? ctx->vao->attrib_binding[index] - 1 : (GLint)index;
        break;
    case GL_VERTEX_ATTRIB_RELATIVE_OFFSET:
        *params = ctx->vao->relative_offset[index];
        break;
    case GL_CURRENT_VERTEX_ATTRIB:
        for (int i = 0; i < 4; i++)
            params[i] = (GLint)generic_component(ctx, index, i);
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        break;
    }
}

GLAPI void APIENTRY glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    if (pname == GL_CURRENT_VERTEX_ATTRIB && index < MIME_GPU_MAX_ATTRIBS) {
        for (unsigned int i = 0; i < 4; ++i)
            params[i] = (GLfloat)generic_component(ctx, index, i);
        return;
    }
    GLint v = 0;
    glGetVertexAttribiv(index, pname, &v);
    *params = (GLfloat)v;
}

GLAPI void APIENTRY glGetVertexAttribIiv(GLuint index, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    if (index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (pname == GL_CURRENT_VERTEX_ATTRIB) {
        for (unsigned int i = 0; i < 4; ++i)
            params[i] = ctx->generic_type[index] == GL_UNSIGNED_INT ?
                (GLint)ctx->generic_integer[index][i] : (GLint)generic_component(ctx, index, i);
    } else {
        glGetVertexAttribiv(index, pname, params);
    }
}

GLAPI void APIENTRY glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params)
{
    GL_CONTEXT_OR_RETURN();
    if (index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (pname == GL_CURRENT_VERTEX_ATTRIB) {
        for (unsigned int i = 0; i < 4; ++i)
            params[i] = ctx->generic_type[index] == GL_INT ?
                (GLuint)(GLint)ctx->generic_integer[index][i] : (GLuint)generic_component(ctx, index, i);
    } else {
        GLint value = 0;
        glGetVertexAttribiv(index, pname, &value);
        *params = (GLuint)value;
    }
}

GLAPI void APIENTRY glGetVertexAttribLdv(GLuint index, GLenum pname, GLdouble *params)
{
    GL_CONTEXT_OR_RETURN();
    if (index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (pname == GL_CURRENT_VERTEX_ATTRIB) {
        for (unsigned int i = 0; i < 4; ++i)
            params[i] = generic_component(ctx, index, i);
    } else {
        GLint value = 0;
        glGetVertexAttribiv(index, pname, &value);
        *params = value;
    }
}

GLAPI void APIENTRY glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer)
{
    GL_CONTEXT_OR_RETURN();
    *pointer = index < MIME_GPU_MAX_ATTRIBS ? (void *)ctx->vao->attribs[index].pointer : nullptr;
}

/* ---------------------------------------------------------------- formats */

static uint32_t type_bytes(GLenum type)
{
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_HALF_FLOAT:
        return 2;
    case GL_DOUBLE:
        return 8;
    default:
        return 4;
    }
}

static uint32_t element_bytes(const GlVertexAttrib *a)
{
    if (a->type == GL_INT_2_10_10_10_REV || a->type == GL_UNSIGNED_INT_2_10_10_10_REV)
        return 4;
    return (a->size == GL_BGRA ? 4 : (uint32_t)a->size) * type_bytes(a->type);
}

/* the Metal vertex format of an attribute, after any copy. */
static uint8_t vertex_format(const GlVertexAttrib *a)
{
    int n = a->size == GL_BGRA ? 4 : a->size;
    BOOL norm = a->normalized && !a->integer;
    if (a->is_long)
        return (uint8_t)(120 + n - 1);
    switch (a->type) {
    case GL_FLOAT:
    case GL_DOUBLE:
        return (uint8_t)(28 + n - 1);
    case GL_HALF_FLOAT:
        return n == 1 ? 53 : (uint8_t)(25 + n - 2);
    case GL_UNSIGNED_BYTE:
        if (a->size == GL_BGRA)
            return 42;
        if (n == 1)
            return norm ? 47 : 45;
        return (uint8_t)((norm ? 7 : 1) + n - 2);
    case GL_BYTE:
        if (n == 1)
            return norm ? 48 : 46;
        return (uint8_t)((norm ? 10 : 4) + n - 2);
    case GL_UNSIGNED_SHORT:
        if (n == 1)
            return norm ? 51 : 49;
        return (uint8_t)((norm ? 19 : 13) + n - 2);
    case GL_SHORT:
        if (n == 1)
            return norm ? 52 : 50;
        return (uint8_t)((norm ? 22 : 16) + n - 2);
    case GL_INT:
        return (uint8_t)(32 + n - 1);
    case GL_UNSIGNED_INT:
        return (uint8_t)(36 + n - 1);
    case GL_INT_2_10_10_10_REV:
        return 40;
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        return 41;
    default:
        return 31;
    }
}

/* ---------------------------------------------------------------- per draw state */

static uint8_t blend_factor(GLenum f)
{
    switch (f) {
    case GL_ZERO:
        return 0;
    case GL_ONE:
        return 1;
    case GL_SRC_COLOR:
        return 2;
    case GL_ONE_MINUS_SRC_COLOR:
        return 3;
    case GL_SRC_ALPHA:
        return 4;
    case GL_ONE_MINUS_SRC_ALPHA:
        return 5;
    case GL_DST_COLOR:
        return 6;
    case GL_ONE_MINUS_DST_COLOR:
        return 7;
    case GL_DST_ALPHA:
        return 8;
    case GL_ONE_MINUS_DST_ALPHA:
        return 9;
    case GL_SRC_ALPHA_SATURATE:
        return 10;
    case GL_CONSTANT_COLOR:
        return 11;
    case GL_ONE_MINUS_CONSTANT_COLOR:
        return 12;
    case GL_CONSTANT_ALPHA:
        return 13;
    case GL_ONE_MINUS_CONSTANT_ALPHA:
        return 14;
    case GL_SRC1_COLOR:
        return 15;
    case GL_ONE_MINUS_SRC1_COLOR:
        return 16;
    case GL_SRC1_ALPHA:
        return 17;
    case GL_ONE_MINUS_SRC1_ALPHA:
        return 18;
    default:
        return 1;
    }
}

static uint8_t blend_op(GLenum e)
{
    switch (e) {
    case GL_FUNC_SUBTRACT:
        return 1;
    case GL_FUNC_REVERSE_SUBTRACT:
        return 2;
    case GL_MIN:
        return 3;
    case GL_MAX:
        return 4;
    default:
        return 0;
    }
}

static uint8_t compare_op(GLenum f)
{
    switch (f) {
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

static uint8_t stencil_op(GLenum o)
{
    switch (o) {
    case GL_ZERO:
        return 1;
    case GL_REPLACE:
        return 2;
    case GL_INCR:
        return 3;
    case GL_DECR:
        return 4;
    case GL_INVERT:
        return 5;
    case GL_INCR_WRAP:
        return 6;
    case GL_DECR_WRAP:
        return 7;
    default:
        return 0;
    }
}

/* where an attribute's data comes from for this draw. */
typedef struct Source {
    BOOL used, copied, constant;
    MimeGpuMem mem;
    uint32_t stride;
    uint8_t format;
} Source;

/* copied vertex data starts at vertex rebase, so data bound in place is shifted by as many vertices. */
typedef struct DrawRange {
    uint32_t first, count; /* vertices the draw reads. */
    uint32_t instances, base_instance;
    uint32_t rebase;       /* first when anything is copied, else 0. */
} DrawRange;

/*
 * integer data feeding a float input is converted to floats in the copy when the host cannot do it on the way: Metal
 * has no normalized 32-bit formats, Vulkan reads integer formats as integers and lacks three component 8 and 16-bit
 * ones on some devices.
 */
static BOOL widened(const GlVertexAttrib *a)
{
    if (a->integer)
        return FALSE;
    switch (a->type) {
    case GL_INT:
    case GL_UNSIGNED_INT:
        return TRUE;
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        return a->size == 3 || (a->size != GL_BGRA && !a->normalized);
    default:
        return FALSE;
    }
}

static double widen(const uint8_t *p, GLenum type, BOOL normalized)
{
    switch (type) {
    case GL_BYTE: {
        int8_t v = (int8_t)*p;
        return normalized ? (2.0 * v + 1.0) / 255.0 : v;
    }
    case GL_UNSIGNED_BYTE:
        return normalized ? *p / 255.0 : *p;
    case GL_SHORT: {
        int16_t v;
        memcpy(&v, p, 2);
        return normalized ? (2.0 * v + 1.0) / 65535.0 : v;
    }
    case GL_UNSIGNED_SHORT: {
        uint16_t v;
        memcpy(&v, p, 2);
        return normalized ? v / 65535.0 : v;
    }
    case GL_INT: {
        int32_t v;
        memcpy(&v, p, 4);
        return normalized ? (2.0 * v + 1.0) / 4294967295.0 : v;
    }
    default: {
        uint32_t v;
        memcpy(&v, p, 4);
        return normalized ? v / 4294967295.0 : v;
    }
    }
}

/* copies count vertices starting at first into the stream, tightly packed and aligned. */
static BOOL copy_attrib(const GlVertexAttrib *a, const uint8_t *base, uint32_t first, uint32_t count, Source *s)
{
    uint32_t elem = element_bytes(a);
    uint32_t src_stride = a->stride ? (uint32_t)a->stride : elem;
    BOOL dbl = (a->type == GL_DOUBLE && !a->is_long) || widened(a);
    uint32_t out_elem = dbl ? (uint32_t)a->size * 4 : elem;
    uint32_t stride = a->is_long ? (out_elem + 7) & ~7u : (out_elem + 3) & ~3u;
    GlMem mem;
    if (!gl_stream_alloc((uint64_t)stride * (count ? count : 1), 16, &mem))
        return FALSE;
    const uint8_t *src = base + (uint64_t)first * src_stride;
    for (uint32_t v = 0; v < count; v++) {
        uint8_t *d = mem.ptr + (uint64_t)v * stride;
        const uint8_t *p = src + (uint64_t)v * src_stride;
        if (a->type == GL_DOUBLE && !a->is_long) {
            for (int c = 0; c < a->size; c++) {
                double x;
                memcpy(&x, p + c * 8, 8);
                float f = (float)x;
                memcpy(d + c * 4, &f, 4);
            }
        } else if (dbl) {
            for (int c = 0; c < a->size; c++) {
                float f = (float)widen(p + c * type_bytes(a->type), a->type, a->normalized);
                memcpy(d + c * 4, &f, 4);
            }
        } else {
            memcpy(d, p, elem);
        }
    }
    s->copied = TRUE;
    s->mem = gl_mem(&mem);
    s->stride = stride;
    if (widened(a))
        s->format = (uint8_t)(28 + a->size - 1);
    return TRUE;
}

/* resolves every attribute location the program reads; FALSE when the draw cannot run. */
static BOOL setup_vertices(GlContext *ctx, GlProgram *p, const GlVertexInput *in, const DrawRange *r, Source *src)
{
    memset(src, 0, sizeof(Source) * MIME_GPU_MAX_ATTRIBS);
    uint64_t batch = gl_batch_fence();
    for (uint32_t m = in->mask, l; m; m &= m - 1) {
        l = (uint32_t)__builtin_ctz(m);
        const GlVertexAttrib *a = &in->attribs[l];
        Source *s = &src[l];
        s->used = TRUE;
        if (!a->enabled && in->constants) {
            GlMem mem;
            if (!gl_stream_alloc(16, 16, &mem))
                return FALSE;
            memcpy(mem.ptr, in->constants[l], 16);
            s->constant = TRUE;
            s->mem = gl_mem(&mem);
            s->format = 31;
            continue;
        }
        if (!a->enabled) {
            GlMem mem;
            if (!gl_stream_alloc(16, 16, &mem))
                return FALSE;
            GLenum type = GL_FLOAT;
            for (uint32_t i = 0; i < p->attrib_count; ++i) {
                const GlAttrib *attrib = &p->attribs[i];
                if (l < (uint32_t)attrib->location || l >= (uint32_t)attrib->location + attrib->slots)
                    continue;
                if (attrib->type == GL_INT || (attrib->type >= GL_INT_VEC2 && attrib->type <= GL_INT_VEC4))
                    type = GL_INT;
                else if (attrib->type == GL_UNSIGNED_INT ||
                         (attrib->type >= GL_UNSIGNED_INT_VEC2 && attrib->type <= GL_UNSIGNED_INT_VEC4))
                    type = GL_UNSIGNED_INT;
                break;
            }
            for (unsigned int i = 0; i < 4; ++i) {
                double value = generic_component(ctx, l, i);
                if (type == GL_INT)
                    ((GLint *)mem.ptr)[i] = ctx->generic_type[l] == GL_UNSIGNED_INT ?
                        (GLint)ctx->generic_integer[l][i] : (GLint)value;
                else if (type == GL_UNSIGNED_INT)
                    ((GLuint *)mem.ptr)[i] = ctx->generic_type[l] == GL_INT ?
                        (GLuint)(GLint)ctx->generic_integer[l][i] : (GLuint)value;
                else
                    ((GLfloat *)mem.ptr)[i] = (GLfloat)value;
            }
            s->constant = TRUE;
            s->mem = gl_mem(&mem);
            s->format = type == GL_INT ? 35 : type == GL_UNSIGNED_INT ? 39 : 31;
            continue;
        }
        s->format = vertex_format(a);
        uint32_t elem = element_bytes(a);
        uint32_t stride = a->stride ? (uint32_t)a->stride : elem;
        uint32_t first = a->divisor ? r->base_instance : r->first;
        uint32_t count = a->divisor ? (r->instances + a->divisor - 1) / a->divisor : r->count;
        if (a->buffer) {
            GlBuffer *b = gl_buffer(ctx, a->buffer);
            if (!b || !b->mem.ptr)
                return FALSE;
            b->last_use = batch;
            uint64_t offset = (uint64_t)(ULONG_PTR)a->pointer;
            uint32_t mask = a->is_long ? 7 : 3;
            BOOL direct = !(offset & mask) && !(stride & mask) && (a->type != GL_DOUBLE || a->is_long) && !widened(a);
            if (direct) {
                s->mem = gl_mem(&b->mem);
                s->mem.offset += offset;
                if (!a->divisor)
                    s->mem.offset += (uint64_t)r->rebase * stride;
                s->stride = stride;
                continue;
            }
            if (!copy_attrib(a, b->mem.ptr + offset, first, count, s))
                return FALSE;
            continue;
        }
        if (!a->pointer)
            return FALSE;
        if (!copy_attrib(a, a->pointer, first, count, s))
            return FALSE;
    }
    return TRUE;
}

/* whether an attribute the program reads cannot be bound where it is. */
static BOOL needs_copy(const GlVertexInput *in)
{
    for (uint32_t m = in->mask, l; m; m &= m - 1) {
        l = (uint32_t)__builtin_ctz(m);
        const GlVertexAttrib *a = &in->attribs[l];
        if (!a->enabled || a->divisor)
            continue;
        uint32_t stride = a->stride ? (uint32_t)a->stride : element_bytes(a);
        uint32_t mask = a->is_long ? 7 : 3;
        if (!a->buffer || ((ULONG_PTR)a->pointer & mask) || (stride & mask) || (a->type == GL_DOUBLE && !a->is_long) ||
            widened(a))
            return TRUE;
    }
    return FALSE;
}

static void emit_pipeline(GlContext *ctx, GlProgram *p, const GlVertexInput *in, const Source *src)
{
    MimeGpuPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.program = p->id;
    d.program_serial = p->serial;
    const GlTarget *t = &ctx->target;
    uint8_t logic = gl_logic_op(ctx);
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++) {
        MimeGpuColorTarget *c = &d.color[i];
        c->format = t->color_format[i];
        if (!c->format)
            continue;
        const GLboolean *m = gl_color_mask_at(ctx, (unsigned)i);
        c->write_mask = (uint8_t)((m[0] ? 8 : 0) | (m[1] ? 4 : 0) | (m[2] ? 2 : 0) | (m[3] ? 1 : 0));
        c->logic_op = logic;
        const GlBlend *blend = gl_blend_at(ctx, (unsigned)i);
        if (blend->enabled && !logic) {
            c->blend = 1;
            c->rgb_op = blend_op(blend->eq_rgb);
            c->alpha_op = blend_op(blend->eq_alpha);
            c->src_rgb = blend_factor(blend->src_rgb);
            c->dst_rgb = blend_factor(blend->dst_rgb);
            c->src_alpha = blend_factor(blend->src_alpha);
            c->dst_alpha = blend_factor(blend->dst_alpha);
        }
    }
    d.depth_format = t->depth_format;
    d.stencil_format = t->stencil_format;
    d.samples = t->samples ? t->samples : 1;
    d.topology = gl_pipeline_topology(ctx, p);
    for (int l = 0; l < MIME_GPU_MAX_ATTRIBS; l++) {
        if (!src[l].used)
            continue;
        d.attribs[l].format = src[l].format;
        d.attribs[l].slot = (uint8_t)l;
        d.layouts[l].stride = src[l].constant ? 0 : src[l].stride;
        d.layouts[l].instance_divisor = src[l].constant ? 0 : in->attribs[l].divisor;
    }
    if (ctx->pipeline_id && !memcmp(&d, &ctx->pipeline_desc, sizeof(d)))
        return;
    BOOL created;
    uint32_t id = (uint32_t)gl_cache_lookup(ID_PIPELINE, &d, sizeof(d), &created);
    if (created) {
        MimeGpuPipelineCreate *c = gl_emit(MIME_GPU_PIPELINE_CREATE, sizeof(MimeGpuPipelineCreate));
        c->id = id;
        c->desc = d;
    }
    ctx->pipeline_desc = d;
    if (id != ctx->pipeline_id) {
        MimeGpuObject *o = gl_emit(MIME_GPU_SET_PIPELINE, sizeof(MimeGpuObject));
        o->id = id;
        ctx->pipeline_id = id;
    }
}

static void emit_depth(GlContext *ctx)
{
    MimeGpuDepthStencilCreate d;
    memset(&d, 0, sizeof(d));
    BOOL depth = ctx->depth_test && ctx->target.depth_format;
    d.depth_func = depth ? compare_op(ctx->depth_func) : 7;
    d.depth_write = depth && ctx->depth_mask;
    d.stencil = ctx->stencil_test && ctx->target.stencil_format;
    if (d.stencil) {
        MimeGpuStencilFace *faces[2] = {&d.front, &d.back};
        for (int f = 0; f < 2; f++) {
            faces[f]->func = compare_op(ctx->stencil_func[f]);
            faces[f]->fail = stencil_op(ctx->stencil_fail[f]);
            faces[f]->depth_fail = stencil_op(ctx->stencil_zfail[f]);
            faces[f]->pass = stencil_op(ctx->stencil_zpass[f]);
            faces[f]->read_mask = (uint8_t)ctx->stencil_value_mask[f];
            faces[f]->write_mask = (uint8_t)ctx->stencil_write_mask[f];
        }
    }
    BOOL created;
    uint32_t id = (uint32_t)gl_cache_lookup(ID_DEPTH_STENCIL, (uint8_t *)&d + sizeof(MimeGpuCmd),
                                            sizeof(d) - sizeof(MimeGpuCmd), &created);
    if (created) {
        MimeGpuDepthStencilCreate *c = gl_emit(MIME_GPU_DEPTH_STENCIL_CREATE, sizeof(d));
        memcpy((uint8_t *)c + sizeof(MimeGpuCmd), (uint8_t *)&d + sizeof(MimeGpuCmd), sizeof(d) - sizeof(MimeGpuCmd));
        c->id = id;
    }
    MimeGpuSetDepthStencil *s = gl_emit(MIME_GPU_SET_DEPTH_STENCIL, sizeof(MimeGpuSetDepthStencil));
    s->id = id;
    s->front_ref = (uint32_t)ctx->stencil_ref[0];
    s->back_ref = (uint32_t)ctx->stencil_ref[1];
    ctx->depth_id = id;
}

static void emit_raster(GlContext *ctx)
{
    MimeGpuSetRaster *r = gl_emit(MIME_GPU_SET_RASTER, sizeof(MimeGpuSetRaster));
    r->cull = !ctx->cull_face ? 0 : ctx->cull_mode == GL_FRONT ? 1 : ctx->cull_mode == GL_BACK ? 2 : 0;
    /* rows are flipped on the way to Metal, which reverses the winding. */
    r->winding = ctx->front_face == GL_CCW ? 0 : 1;
    r->fill = ctx->polygon_mode == GL_LINE ? 1 : 0;
    r->depth_clip = (!ctx->depth_clamp ? 1 : 0) | (ctx->st.rasterizer_discard ? 2 : 0);
    if (!ctx->ff.window_draw) {
        /* the rows are flipped by the viewport and the facing is the one in clip space. */
        if (ctx->clip_upper_left)
            r->winding ^= 1;
        r->zero_to_one = ctx->clip_zero_to_one;
    }
    if (ctx->polygon_offset_fill) {
        r->depth_bias = ctx->offset_units;
        r->slope_scale = ctx->offset_factor;
        r->bias_clamp = ctx->offset_clamp;
    }
}

static void emit_textures(GlContext *ctx, GlProgram *p)
{
    for (uint32_t i = 0; i < p->uniform_count; i++) {
        GlUniform *u = &p->uniforms[i];
        if (!u->units || gl_is_image_type(u->type))
            continue;
        for (GLint e = 0; e < u->size; e++) {
            GlTexture *t = gl_unit_texture(ctx, (GLuint)u->units[e], u->type);
            uint32_t id, sampler;
            if (gl_texture_ready(ctx, t)) {
                id = t->id;
                sampler = gl_texture_sampler(t);
            } else {
                id = gl_dummy_texture(ctx);
                static GlTexture plain;
                if (!plain.sampler.mag_filter) {
                    plain.sampler.min_filter = plain.sampler.mag_filter = GL_NEAREST;
                    plain.sampler.wrap_s = plain.sampler.wrap_t = plain.sampler.wrap_r = GL_CLAMP_TO_EDGE;
                    plain.sampler.max_lod = 1000;
                    plain.levels = 1;
                }
                sampler = gl_texture_sampler(&plain);
            }
            for (int s = 0; s < MIME_GPU_STAGES; s++) {
                if (u->texture[s] < 0)
                    continue;
                uint32_t index = (uint32_t)u->texture[s] + (uint32_t)e;
                if (index >= MIME_GPU_MAX_TEXTURES)
                    continue;
                if (ctx->bound_tex[s][index].id == id && ctx->bound_tex[s][index].sampler == sampler)
                    continue;
                MimeGpuSetTexture *c = gl_emit(MIME_GPU_SET_TEXTURE, sizeof(MimeGpuSetTexture));
                c->stage = (uint8_t)s;
                c->index = (uint8_t)index;
                c->id = id;
                c->sampler = sampler;
                ctx->bound_tex[s][index].id = id;
                ctx->bound_tex[s][index].sampler = sampler;
            }
        }
    }
}

/* sends the state the host does not have yet, before a draw. */
static BOOL prepare(GlContext *ctx, GlProgram *p, const GlVertexInput *in, const Source *src)
{
    for (uint32_t i = 0; i < p->block_count; i++) {
        GlUniformBlock *block = &p->blocks[i];
        GlBufferBinding *binding = &ctx->uniform_bindings[block->binding];
        GlBuffer *buffer = gl_buffer(ctx, binding->name);
        if (!buffer || (buffer->mapped && !(buffer->map_access & GL_MAP_PERSISTENT_BIT)) || !buffer->mem.ptr ||
            binding->offset > buffer->size ||
            block->bytes > buffer->size - binding->offset || (!binding->whole && block->bytes > binding->size)) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return FALSE;
        }
    }
    uint32_t dirty = ctx->dirty;
    emit_pipeline(ctx, p, in, src);
    if ((dirty & DIRTY_DEPTH) || !ctx->depth_id)
        emit_depth(ctx);
    if (dirty & DIRTY_RASTER)
        emit_raster(ctx);
    if (dirty & DIRTY_VIEWPORT) {
        MimeGpuSetViewport *v = gl_emit(MIME_GPU_SET_VIEWPORT, sizeof(MimeGpuSetViewport));
        v->x = (float)ctx->viewport[0];
        v->y = (float)ctx->viewport[1];
        v->width = (float)ctx->viewport[2];
        v->height = (float)ctx->viewport[3];
        if (ctx->clip_upper_left && !ctx->ff.window_draw && v->height > 0) {
            v->y += v->height;
            v->height = -v->height;
        }
        v->znear = (float)ctx->depth_range[0];
        v->zfar = (float)ctx->depth_range[1];
    }
    if (dirty & DIRTY_SCISSOR) {
        MimeGpuRect *r = gl_emit(MIME_GPU_SET_SCISSOR, sizeof(MimeGpuRect));
        if (ctx->scissor_test) {
            r->x = ctx->scissor[0];
            r->y = ctx->scissor[1];
            r->width = (uint32_t)ctx->scissor[2];
            r->height = (uint32_t)ctx->scissor[3];
        } else {
            r->width = ctx->target.width;
            r->height = ctx->target.height;
        }
    }
    gl_stage_emit_viewports(ctx, dirty);
    gl_xfb_prepare(ctx);
    if (dirty & DIRTY_BLEND_COLOR) {
        MimeGpuSetBlendColor *b = gl_emit(MIME_GPU_SET_BLEND_COLOR, sizeof(MimeGpuSetBlendColor));
        memcpy(b->rgba, ctx->blend_color, sizeof(b->rgba));
    }
    if (ctx->uniforms_of != p || ctx->uniforms_serial != p->serial || p->ublock_dirty) {
        uint32_t n = p->ublock_size > 4096 ? 4096 : p->ublock_size;
        if (n) {
            MimeGpuSetUniforms *u = gl_emit(MIME_GPU_SET_UNIFORMS, (uint32_t)sizeof(MimeGpuSetUniforms) + n);
            u->bytes = n;
            memcpy(u + 1, p->ublock, n);
        }
        p->ublock_dirty = FALSE;
        ctx->uniforms_of = p;
        ctx->uniforms_serial = p->serial;
    }
    if (dirty & (DIRTY_TEXTURES | DIRTY_PIPELINE))
        emit_textures(ctx, p);
    for (uint32_t i = 0; i < p->block_count; i++) {
        GlUniformBlock *block = &p->blocks[i];
        GlBufferBinding *binding = &ctx->uniform_bindings[block->binding];
        GlBuffer *buffer = gl_buffer(ctx, binding->name);
        MimeGpuSetUniformBuffer *command = gl_emit(MIME_GPU_SET_UNIFORM_BUFFER, sizeof(MimeGpuSetUniformBuffer));
        command->slot = i;
        command->stages = block->stages;
        command->mem = gl_mem(&buffer->mem);
        command->mem.offset += binding->offset;
        buffer->last_use = gl_batch_fence();
    }
    gl_emit_storage(ctx, p);
    gl_emit_images(ctx, p);
    for (int l = 0; l < MIME_GPU_MAX_ATTRIBS; l++) {
        if (!src[l].used)
            continue;
        if (!src[l].copied && !src[l].constant && !memcmp(&ctx->bound_vb[l], &src[l].mem, sizeof(MimeGpuMem)))
            continue;
        MimeGpuSetVertexBuffer *v = gl_emit(MIME_GPU_SET_VERTEX_BUFFER, sizeof(MimeGpuSetVertexBuffer));
        v->slot = (uint32_t)l;
        v->mem = src[l].mem;
        ctx->bound_vb[l] = src[l].mem;
    }
    ctx->dirty = 0;
    return TRUE;
}

/* the state a dispatch needs: the loose uniforms, textures and uniform blocks of the compute program. */
void gl_emit_compute_state(GlContext *ctx, GlProgram *p)
{
    if (ctx->uniforms_of != p || ctx->uniforms_serial != p->serial || p->ublock_dirty) {
        uint32_t n = p->ublock_size > 4096 ? 4096 : p->ublock_size;
        if (n) {
            MimeGpuSetUniforms *u = gl_emit(MIME_GPU_SET_UNIFORMS, (uint32_t)sizeof(MimeGpuSetUniforms) + n);
            u->bytes = n;
            memcpy(u + 1, p->ublock, n);
        }
        p->ublock_dirty = FALSE;
        ctx->uniforms_of = p;
        ctx->uniforms_serial = p->serial;
    }
    emit_textures(ctx, p);
    for (uint32_t i = 0; i < p->block_count; i++) {
        GlUniformBlock *block = &p->blocks[i];
        GlBufferBinding *binding = &ctx->uniform_bindings[block->binding];
        GlBuffer *buffer = gl_buffer(ctx, binding->name);
        if (!buffer || !buffer->mem.ptr || binding->offset > buffer->size || block->bytes > buffer->size - binding->offset)
            continue;
        MimeGpuSetUniformBuffer *command = gl_emit(MIME_GPU_SET_UNIFORM_BUFFER, sizeof(MimeGpuSetUniformBuffer));
        command->slot = i;
        command->stages = block->stages;
        command->mem = gl_mem(&buffer->mem);
        command->mem.offset += binding->offset;
        buffer->last_use = gl_batch_fence();
    }
}

/* ---------------------------------------------------------------- draws */

static uint32_t mtl_primitive(GLenum mode)
{
    if (mode > GL_POLYGON)
        return gl_primitive_of(mode);
    switch (mode) {
    case GL_POINTS:
        return 0;
    case GL_LINES:
        return 1;
    case GL_LINE_STRIP:
        return 2;
    case GL_TRIANGLE_STRIP:
    case GL_QUAD_STRIP:
        return 4;
    default:
        return 3;
    }
}

static uint32_t read_index(const void *indices, GLenum type, uint32_t i)
{
    switch (type) {
    case GL_UNSIGNED_BYTE:
        return ((const uint8_t *)indices)[i];
    case GL_UNSIGNED_SHORT:
        return ((const uint16_t *)indices)[i];
    default:
        return ((const uint32_t *)indices)[i];
    }
}

/*
 * flat shading takes the color of the last vertex of lines, triangles and quads and the first vertex of polygons. Metal
 * takes the first vertex of each primitive, so flat draws list the vertex GL picks first, in an order that keeps the
 * winding.
 */
static BOOL flat_mode(GLenum mode)
{
    switch (mode) {
    case GL_LINES:
    case GL_LINE_STRIP:
    case GL_TRIANGLES:
    case GL_TRIANGLE_STRIP:
    case GL_QUAD_STRIP:
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL needs_conversion(GLenum mode, BOOL flat)
{
    return mode == GL_TRIANGLE_FAN || mode == GL_LINE_LOOP || mode == GL_QUADS || mode == GL_POLYGON ||
           (flat && flat_mode(mode));
}

static uint32_t converted_count(GLenum mode, BOOL flat, uint32_t count, uint32_t *primitive)
{
    *primitive = 3;
    switch (mode) {
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
    case GL_TRIANGLE_STRIP:
        return count >= 3 ? (count - 2) * 3 : 0;
    case GL_TRIANGLES:
        return count / 3 * 3;
    case GL_QUADS:
        return count / 4 * 6;
    case GL_QUAD_STRIP:
        return count >= 4 ? (count / 2 - 1) * 6 : 0;
    case GL_LINES:
        *primitive = 1;
        return count / 2 * 2;
    case GL_LINE_STRIP:
        *primitive = 1;
        return count >= 2 ? (count - 1) * 2 : 0;
    case GL_LINE_LOOP:
        if (flat) {
            *primitive = 1;
            return count >= 2 ? count * 2 : 0;
        }
        *primitive = 2;
        return count >= 2 ? count + 1 : 0;
    default:
        return 0;
    }
}

/*
 * lists fans, loops, quads and polygons, and every primitive when flat shading, as triangles, lines or a strip. NULL
 * indices count from first.
 */
static BOOL convert_primitive(GLenum mode, BOOL flat, const void *indices, GLenum type, uint32_t first, uint32_t count,
                              GlMem *out, uint32_t *out_count, uint32_t *primitive)
{
    uint32_t n = converted_count(mode, flat, count, primitive);
    if (!n || !gl_stream_alloc((uint64_t)n * 4, 16, out))
        return FALSE;
    uint32_t *d = (uint32_t *)out->ptr;
#define IDX(i) (indices ? read_index(indices, type, (i)) : first + (i))
    uint32_t k = 0;
    switch (mode) {
    case GL_LINE_LOOP:
        if (flat) {
            for (uint32_t i = 0; i + 1 < count; i++) {
                d[k++] = IDX(i + 1);
                d[k++] = IDX(i);
            }
            d[k++] = IDX(0);
            d[k++] = IDX(count - 1);
        } else {
            for (uint32_t i = 0; i < count; i++)
                d[k++] = IDX(i);
            d[k++] = IDX(0);
        }
        break;
    case GL_LINES:
        for (uint32_t i = 0; i + 1 < count; i += 2) {
            d[k++] = IDX(i + 1);
            d[k++] = IDX(i);
        }
        break;
    case GL_LINE_STRIP:
        for (uint32_t i = 0; i + 1 < count; i++) {
            d[k++] = IDX(i + 1);
            d[k++] = IDX(i);
        }
        break;
    case GL_TRIANGLES:
        for (uint32_t i = 0; i + 2 < count; i += 3) {
            d[k++] = IDX(i + 2);
            d[k++] = IDX(i);
            d[k++] = IDX(i + 1);
        }
        break;
    case GL_TRIANGLE_STRIP:
        for (uint32_t i = 0; i + 2 < count; i++) {
            d[k++] = IDX(i + 2);
            d[k++] = IDX(i + (i & 1 ? 1 : 0));
            d[k++] = IDX(i + (i & 1 ? 0 : 1));
        }
        break;
    case GL_TRIANGLE_FAN:
        for (uint32_t i = 1; i + 1 < count; i++) {
            if (flat) {
                d[k++] = IDX(i + 1);
                d[k++] = IDX(0);
                d[k++] = IDX(i);
            } else {
                d[k++] = IDX(0);
                d[k++] = IDX(i);
                d[k++] = IDX(i + 1);
            }
        }
        break;
    case GL_POLYGON:
        for (uint32_t i = 1; i + 1 < count; i++) {
            d[k++] = IDX(0);
            d[k++] = IDX(i);
            d[k++] = IDX(i + 1);
        }
        break;
    case GL_QUADS:
        for (uint32_t q = 0; q + 3 < count; q += 4) {
            if (flat) {
                /* both triangles hold the last vertex, whose color GL takes. */
                d[k++] = IDX(q + 3);
                d[k++] = IDX(q);
                d[k++] = IDX(q + 1);
                d[k++] = IDX(q + 3);
                d[k++] = IDX(q + 1);
                d[k++] = IDX(q + 2);
            } else {
                d[k++] = IDX(q);
                d[k++] = IDX(q + 1);
                d[k++] = IDX(q + 2);
                d[k++] = IDX(q);
                d[k++] = IDX(q + 2);
                d[k++] = IDX(q + 3);
            }
        }
        break;
    case GL_QUAD_STRIP:
        for (uint32_t q = 0; q + 3 < count; q += 2) {
            d[k++] = IDX(q + 3);
            d[k++] = IDX(q);
            d[k++] = IDX(q + 1);
            d[k++] = IDX(q + 3);
            d[k++] = IDX(q + 2);
            d[k++] = IDX(q);
        }
        break;
    default:
        break;
    }
#undef IDX
    *out_count = k;
    return TRUE;
}

/* the program a draw uses and an open pass on its target, or NULL when it draws nothing. */
static GlProgram *begin_draw(GlContext *ctx, GlVertexInput *in, BOOL immediate, GLenum mode)
{
    GlProgram *p = ctx->current_program;
    if (p && immediate)
        return nullptr;
    if (ctx->st.skip)
        return nullptr;
    if (!p && ctx->st.pipeline && !immediate)
        p = gl_pipeline_resolve(ctx);
    if (mode > GL_POLYGON && (!p || !gl_draw_mode_fits(ctx, p, mode))) {
        if (!p)
            gl_error(ctx, GL_INVALID_OPERATION);
        return nullptr;
    }
    if (p && mode <= GL_POLYGON && !gl_draw_mode_fits(ctx, p, mode))
        return nullptr;
    if (p) {
        in->attribs = ctx->vao->attribs;
        in->mask = p->attrib_mask;
        in->constants = nullptr;
        in->fixed = in->flat = FALSE;
        if (!gl_program_ready(p))
            return nullptr;
    } else {
        p = gl_ff_begin_draw(ctx, in, immediate, mode);
        if (!p)
            return nullptr;
    }
    if (ctx->cull_face && ctx->cull_mode == GL_FRONT_AND_BACK)
        return nullptr;
    gl_begin_pass(ctx);
    if (!ctx->pass_open)
        return nullptr;
    if (in->fixed)
        gl_ff_prepare(ctx, p);
    return p;
}

static void draw_arrays(GLenum mode, GLint first, GLsizei count, GLsizei instances, GLuint base_instance)
{
    GL_CONTEXT_OR_RETURN();
    if (first < 0 || count < 0 || instances < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!gl_draw_mode_valid(ctx, mode)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!count || !instances)
        return;
    if (gl_lines_wanted(ctx, mode)) {
        gl_lines_draw_arrays(ctx, mode, first, count);
        return;
    }
    gl_device_lock();
    GlVertexInput in;
    GlProgram *p = begin_draw(ctx, &in, FALSE, mode);
    if (!p) {
        gl_device_unlock();
        return;
    }
    DrawRange r = {(uint32_t)first, (uint32_t)count, (uint32_t)instances, base_instance, 0};
    BOOL copy = needs_copy(&in);
    if (copy)
        r.rebase = r.first;
    Source src[MIME_GPU_MAX_ATTRIBS];
    if (!setup_vertices(ctx, p, &in, &r, src)) {
        gl_device_unlock();
        return;
    }
    if (!prepare(ctx, p, &in, src)) {
        gl_device_unlock();
        return;
    }
    uint32_t start = copy ? 0 : r.first;
    if (needs_conversion(mode, in.flat)) {
        GlMem idx;
        uint32_t n, prim;
        if (convert_primitive(mode, in.flat, nullptr, 0, start, r.count, &idx, &n, &prim)) {
            MimeGpuDrawIndexed *d = gl_emit(MIME_GPU_DRAW_INDEXED, sizeof(MimeGpuDrawIndexed));
            d->primitive = prim;
            d->count = n;
            d->index_type = 1;
            d->instances = r.instances;
            d->base_instance = base_instance;
            d->indices = gl_mem(&idx);
        }
    } else {
        MimeGpuDraw *d = gl_emit(MIME_GPU_DRAW, sizeof(MimeGpuDraw));
        d->primitive = mtl_primitive(mode);
        d->first = start;
        d->count = r.count;
        d->instances = r.instances;
        d->base_instance = base_instance;
    }
    gl_device_unlock();
}

GLAPI void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    draw_arrays(mode, first, count, 1, 0);
}

GLAPI void APIENTRY glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances)
{
    draw_arrays(mode, first, count, instances, 0);
}

GLAPI void APIENTRY glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei instances,
                                                      GLuint base_instance)
{
    draw_arrays(mode, first, count, instances, base_instance);
}

GLAPI void APIENTRY glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei n)
{
    for (GLsizei i = 0; i < n; i++)
        draw_arrays(mode, first[i], count[i], 1, 0);
}

static uint32_t index_size(GLenum type)
{
    return type == GL_UNSIGNED_BYTE ? 1 : type == GL_UNSIGNED_SHORT ? 2 : 4;
}

static void draw_elements(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instances,
                          GLint base_vertex, GLuint base_instance)
{
    GL_CONTEXT_OR_RETURN();
    if (count < 0 || instances < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!gl_draw_mode_valid(ctx, mode)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!count || !instances)
        return;
    if (gl_lines_wanted(ctx, mode)) {
        gl_lines_draw_elements(ctx, mode, count, type, indices, base_vertex);
        return;
    }
    gl_device_lock();
    GlVertexInput in;
    GlProgram *p = begin_draw(ctx, &in, FALSE, mode);
    if (!p) {
        gl_device_unlock();
        return;
    }
    /* the indices, as memory the cpu reads and maybe the gpu too. */
    GlBuffer *eb = ctx->vao->element_buffer ? gl_buffer(ctx, ctx->vao->element_buffer) : nullptr;
    const uint8_t *data;
    if (eb) {
        if (!eb->mem.ptr || (uint64_t)(ULONG_PTR)indices + (uint64_t)count * index_size(type) > eb->size) {
            gl_device_unlock();
            return;
        }
        data = eb->mem.ptr + (ULONG_PTR)indices;
    } else {
        data = indices;
        if (!data) {
            gl_device_unlock();
            return;
        }
    }

    DrawRange r = {0, 0, (uint32_t)instances, base_instance, 0};
    BOOL copy = needs_copy(&in);
    if (copy) {
        uint32_t lo = 0xFFFFFFFF, hi = 0;
        for (GLsizei i = 0; i < count; i++) {
            uint32_t v = read_index(data, type, (uint32_t)i);
            if ((type == GL_UNSIGNED_SHORT && v == 0xFFFF) || v == 0xFFFFFFFF)
                continue;
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
        if (lo > hi) {
            gl_device_unlock();
            return;
        }
        r.first = (uint32_t)((int64_t)lo + base_vertex);
        r.count = hi - lo + 1;
        r.rebase = r.first;
    }
    Source src[MIME_GPU_MAX_ATTRIBS];
    if (!setup_vertices(ctx, p, &in, &r, src)) {
        gl_device_unlock();
        return;
    }
    if (!prepare(ctx, p, &in, src)) {
        gl_device_unlock();
        return;
    }

    MimeGpuMem mem;
    uint32_t n = (uint32_t)count, itype, prim = mtl_primitive(mode);
    if (needs_conversion(mode, in.flat)) {
        GlMem idx;
        if (!convert_primitive(mode, in.flat, data, type, 0, (uint32_t)count, &idx, &n, &prim)) {
            gl_device_unlock();
            return;
        }
        mem = gl_mem(&idx);
        itype = 1;
    } else if (type == GL_UNSIGNED_BYTE || !eb || ((ULONG_PTR)indices & 3)) {
        /* bytes become shorts; client indices go to the stream. */
        uint32_t size = type == GL_UNSIGNED_INT ? 4 : 2;
        GlMem idx;
        if (!gl_stream_alloc((uint64_t)count * size, 16, &idx)) {
            gl_device_unlock();
            return;
        }
        if (type == GL_UNSIGNED_BYTE)
            for (GLsizei i = 0; i < count; i++)
                ((uint16_t *)idx.ptr)[i] = data[i] == 0xFF ? 0xFF : data[i];
        else
            memcpy(idx.ptr, data, (SIZE_T)count * size);
        mem = gl_mem(&idx);
        itype = size == 4;
    } else {
        mem = gl_mem(&eb->mem);
        mem.offset += (ULONG_PTR)indices;
        eb->last_use = gl_batch_fence();
        itype = type == GL_UNSIGNED_INT;
    }
    MimeGpuDrawIndexed *d = gl_emit(MIME_GPU_DRAW_INDEXED, sizeof(MimeGpuDrawIndexed));
    d->primitive = prim;
    d->count = n;
    d->index_type = itype;
    d->instances = r.instances;
    d->base_vertex = (int32_t)((int64_t)base_vertex - (int64_t)r.rebase);
    d->base_instance = base_instance;
    d->indices = mem;
    gl_device_unlock();
}

GLAPI void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    draw_elements(mode, count, type, indices, 1, 0, 0);
}

GLAPI void APIENTRY glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                                        const void *indices)
{
    draw_elements(mode, count, type, indices, 1, 0, 0);
}

GLAPI void APIENTRY glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices,
                                            GLsizei instances)
{
    draw_elements(mode, count, type, indices, instances, 0, 0);
}

GLAPI void APIENTRY glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices,
                                             GLint base_vertex)
{
    draw_elements(mode, count, type, indices, 1, base_vertex, 0);
}

GLAPI void APIENTRY glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                                                  const void *indices, GLint base_vertex)
{
    draw_elements(mode, count, type, indices, 1, base_vertex, 0);
}

GLAPI void APIENTRY glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices,
                                                      GLsizei instances, GLint base_vertex)
{
    draw_elements(mode, count, type, indices, instances, base_vertex, 0);
}

GLAPI void APIENTRY glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const void *const *indices,
                                        GLsizei n)
{
    for (GLsizei i = 0; i < n; i++)
        draw_elements(mode, count[i], type, indices[i], 1, 0, 0);
}

GLAPI void APIENTRY glDrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type, const void *indices,
                                                        GLsizei instances, GLuint base_instance)
{
    draw_elements(mode, count, type, indices, instances, 0, base_instance);
}

GLAPI void APIENTRY glDrawElementsInstancedBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type,
                                                                  const void *indices, GLsizei instances,
                                                                  GLint base_vertex, GLuint base_instance)
{
    draw_elements(mode, count, type, indices, instances, base_vertex, base_instance);
}

GLAPI void APIENTRY glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count, GLenum type,
                                                  const void *const *indices, GLsizei n, const GLint *base_vertex)
{
    for (GLsizei i = 0; i < n; i++)
        draw_elements(mode, count[i], type, indices[i], 1, base_vertex[i], 0);
}

/* ---------------------------------------------------------------- indirect draws */

/*
 * the commands are read here, once the gpu is done with a buffer a shader wrote, and become direct draws. the host needs
 * nothing for them, and a buffer written by a shader costs a wait.
 */
typedef struct DrawArraysCommand {
    GLuint count, instances, first, base_instance;
} DrawArraysCommand;

typedef struct DrawElementsCommand {
    GLuint count, instances, first_index;
    GLint base_vertex;
    GLuint base_instance;
} DrawElementsCommand;

static GlBuffer *indirect_buffer(GlContext *ctx, GLintptr offset, GLsizei stride, GLsizei count, uint32_t size)
{
    GlBuffer *b = gl_buffer(ctx, ctx->draw_indirect_buffer);
    if (!b || !b->mem.ptr) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return nullptr;
    }
    if (offset < 0 || (offset & 3) || stride < 0 || (stride & 3) || count < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return nullptr;
    }
    uint32_t step = stride ? (uint32_t)stride : size;
    if (count && (uint64_t)offset + (uint64_t)(count - 1) * step + size > b->size) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return nullptr;
    }
    gl_buffer_read_ready(b);
    return b;
}

static void indirect_arrays(GlContext *ctx, GLenum mode, GLintptr offset, GLsizei count, GLsizei stride)
{
    GlBuffer *b = indirect_buffer(ctx, offset, stride, count, sizeof(DrawArraysCommand));
    if (!b)
        return;
    uint32_t step = stride ? (uint32_t)stride : sizeof(DrawArraysCommand);
    for (GLsizei i = 0; i < count; i++) {
        DrawArraysCommand c;
        memcpy(&c, b->mem.ptr + offset + (uint64_t)i * step, sizeof(c));
        draw_arrays(mode, (GLint)c.first, (GLsizei)c.count, (GLsizei)c.instances, c.base_instance);
    }
}

static void indirect_elements(GlContext *ctx, GLenum mode, GLenum type, GLintptr offset, GLsizei count, GLsizei stride)
{
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlBuffer *b = indirect_buffer(ctx, offset, stride, count, sizeof(DrawElementsCommand));
    if (!b)
        return;
    uint32_t step = stride ? (uint32_t)stride : sizeof(DrawElementsCommand);
    for (GLsizei i = 0; i < count; i++) {
        DrawElementsCommand c;
        memcpy(&c, b->mem.ptr + offset + (uint64_t)i * step, sizeof(c));
        draw_elements(mode, (GLsizei)c.count, type, (const void *)(ULONG_PTR)((uint64_t)c.first_index * index_size(type)),
                      (GLsizei)c.instances, c.base_vertex, c.base_instance);
    }
}

GLAPI void APIENTRY glDrawArraysIndirect(GLenum mode, const void *indirect)
{
    GL_CONTEXT_OR_RETURN();
    indirect_arrays(ctx, mode, (GLintptr)indirect, 1, 0);
}

GLAPI void APIENTRY glDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect)
{
    GL_CONTEXT_OR_RETURN();
    indirect_elements(ctx, mode, type, (GLintptr)indirect, 1, 0);
}

GLAPI void APIENTRY glMultiDrawArraysIndirect(GLenum mode, const void *indirect, GLsizei count, GLsizei stride)
{
    GL_CONTEXT_OR_RETURN();
    indirect_arrays(ctx, mode, (GLintptr)indirect, count, stride);
}

GLAPI void APIENTRY glMultiDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect, GLsizei count,
                                                GLsizei stride)
{
    GL_CONTEXT_OR_RETURN();
    indirect_elements(ctx, mode, type, (GLintptr)indirect, count, stride);
}

/* the count the parameter buffer holds at offset, capped by maxdrawcount. */
static GLsizei parameter_count(GlContext *ctx, GLintptr offset, GLsizei max)
{
    GlBuffer *b = gl_buffer(ctx, ctx->parameter_buffer);
    if (!b || !b->mem.ptr || offset < 0 || (offset & 3) || (uint64_t)offset + 4 > b->size) {
        gl_error(ctx, !b ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return -1;
    }
    gl_buffer_read_ready(b);
    GLuint n;
    memcpy(&n, b->mem.ptr + offset, sizeof(n));
    return n < (GLuint)max ? (GLsizei)n : max;
}

GLAPI void APIENTRY glMultiDrawArraysIndirectCount(GLenum mode, const void *indirect, GLintptr drawcount,
                                                   GLsizei maxdrawcount, GLsizei stride)
{
    GL_CONTEXT_OR_RETURN();
    GLsizei n = parameter_count(ctx, drawcount, maxdrawcount);
    if (n >= 0)
        indirect_arrays(ctx, mode, (GLintptr)indirect, n, stride);
}

GLAPI void APIENTRY glMultiDrawElementsIndirectCount(GLenum mode, GLenum type, const void *indirect, GLintptr drawcount,
                                                     GLsizei maxdrawcount, GLsizei stride)
{
    GL_CONTEXT_OR_RETURN();
    GLsizei n = parameter_count(ctx, drawcount, maxdrawcount);
    if (n >= 0)
        indirect_elements(ctx, mode, type, (GLintptr)indirect, n, stride);
}

/* draws the vertices a transform feedback captured, as many as its byte counters say. */
void gl_draw_feedback(GlContext *ctx, GLenum mode, uint32_t object, uint32_t buffer, uint32_t stride, GLsizei instances)
{
    gl_device_lock();
    GlVertexInput in;
    GlProgram *p = begin_draw(ctx, &in, FALSE, mode);
    if (!p) {
        gl_device_unlock();
        return;
    }
    DrawRange r = {0, 0, (uint32_t)instances, 0, 0};
    Source src[MIME_GPU_MAX_ATTRIBS];
    /* the count is the host's to know, so nothing can be copied for it. */
    if (needs_copy(&in) || !setup_vertices(ctx, p, &in, &r, src) || !prepare(ctx, p, &in, src)) {
        gl_device_unlock();
        return;
    }
    MimeGpuDrawXfb *d = gl_emit(MIME_GPU_DRAW_XFB, sizeof(MimeGpuDrawXfb));
    d->primitive = mtl_primitive(mode);
    d->object = object;
    d->buffer = buffer;
    d->stride = stride;
    d->instances = (uint32_t)instances;
    gl_device_unlock();
}

/* ---------------------------------------------------------------- glBegin and glEnd */

static const GlVertexAttrib g_no_attribs[MIME_GPU_MAX_ATTRIBS];

/* one fixed function draw for the vertices collected between glBegin and glEnd. */
void gl_draw_immediate(GlContext *ctx)
{
    GlFixed *f = &ctx->ff;
    uint32_t count = f->vertex_count;
    f->vertex_count = 0;
    if (gl_lines_wanted(ctx, f->begin_mode))
        gl_lines_draw(ctx, f->begin_mode, f->vertices, count);
    else
        gl_draw_vertices(ctx, f->begin_mode, f->vertices, count, f->varying);
}

/* the number of vertices that make whole primitives, and 0 when there are none. */
uint32_t gl_whole_primitives(GLenum mode, uint32_t count)
{
    uint32_t minimum = 1;
    switch (mode) {
    case GL_LINES:
        count &= ~1u;
        minimum = 2;
        break;
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        minimum = 2;
        break;
    case GL_TRIANGLES:
        count -= count % 3;
        minimum = 3;
        break;
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        minimum = 3;
        break;
    case GL_QUADS:
        count &= ~3u;
        minimum = 4;
        break;
    case GL_QUAD_STRIP:
        count &= ~1u;
        minimum = 4;
        break;
    default:
        break;
    }
    return count < minimum ? 0 : count;
}

/*
 * the draw of count vertices that are in memory the gpu reads: stride bytes apart, the position first and the color, normal
 * and texture coordinate at the offsets in at when they are stored, else the three vectors of constants. a batch of a display
 * list keeps the indices it needs for its primitives.
 */
static void draw_stored(GlContext *ctx, GlProgram *p, GlVertexInput *in, GLenum mode, uint32_t count, const GlMem *mem,
                        BOOL copied, uint32_t stride, const uint32_t at[4], const GlMem *constants, GlDlBatch *batch)
{
    Source src[MIME_GPU_MAX_ATTRIBS];
    memset(src, 0, sizeof(src));
    for (int l = 0; l < 4; l++) {
        src[l].used = TRUE;
        src[l].format = 31;
        if (l && !at[l]) {
            src[l].constant = TRUE;
            src[l].mem = gl_mem(constants);
            src[l].mem.offset += (uint64_t)(l - 1) * 16;
            continue;
        }
        src[l].copied = copied;
        src[l].mem = gl_mem(mem);
        src[l].mem.offset += at[l];
        src[l].stride = stride;
    }
    in->attribs = g_no_attribs;
    if (!prepare(ctx, p, in, src))
        return;
    if (needs_conversion(mode, in->flat)) {
        GlMem idx = {};
        uint32_t n = 0, prim = 0;
        BOOL ok;
        if (batch) {
            int slot = in->flat ? 1 : 0;
            if (!batch->idx[slot].size) {
                GlMem temporary;
                if (convert_primitive(mode, in->flat, nullptr, 0, 0, count, &temporary, &n, &prim) &&
                    gl_heap_alloc((uint64_t)n * 4, &batch->idx[slot])) {
                    memcpy(batch->idx[slot].ptr, temporary.ptr, (size_t)n * 4);
                    batch->idx_count[slot] = n;
                    batch->idx_prim[slot] = prim;
                }
            }
            ok = batch->idx[slot].size != 0;
            idx = batch->idx[slot];
            n = batch->idx_count[slot];
            prim = batch->idx_prim[slot];
        } else {
            ok = convert_primitive(mode, in->flat, nullptr, 0, 0, count, &idx, &n, &prim);
        }
        if (ok) {
            MimeGpuDrawIndexed *d = gl_emit(MIME_GPU_DRAW_INDEXED, sizeof(MimeGpuDrawIndexed));
            d->primitive = prim;
            d->count = n;
            d->index_type = 1;
            d->instances = 1;
            d->indices = gl_mem(&idx);
        }
    } else {
        MimeGpuDraw *d = gl_emit(MIME_GPU_DRAW, sizeof(MimeGpuDraw));
        d->primitive = mtl_primitive(mode);
        d->first = 0;
        d->count = count;
        d->instances = 1;
    }
}

/*
 * a batch too large for a block of the heap is drawn in pieces that are. what stands alone is cut anywhere on a primitive,
 * strips go on from the last vertices of the piece before, an even number of them so the winding goes on too.
 */
static BOOL draw_in_pieces(GlContext *ctx, GLenum mode, const GlFfVertex *vertices, uint32_t count, uint32_t varying)
{
    uint32_t stride = 16 + 16 * (uint32_t)__builtin_popcount(varying & 7);
    uint32_t most = (32u << 20) / stride, overlap = 0;
    if (count <= most)
        return FALSE;
    switch (mode) {
    case GL_POINTS:
    case GL_LINES:
    case GL_TRIANGLES:
    case GL_QUADS:
        most -= most % 12;
        break;
    case GL_LINE_STRIP:
        overlap = 1;
        break;
    case GL_TRIANGLE_STRIP:
    case GL_QUAD_STRIP:
        most &= ~1u;
        overlap = 2;
        break;
    default:
        return FALSE;
    }
    for (uint32_t start = 0;;) {
        uint32_t n = count - start < most ? count - start : most;
        gl_draw_vertices(ctx, mode, vertices + start, n, varying);
        if (start + n >= count)
            break;
        start += n - overlap;
    }
    return TRUE;
}

/* one fixed function draw of vertices that are in a batch of their own. */
void gl_draw_vertices(GlContext *ctx, GLenum mode, const GlFfVertex *vertices, uint32_t count, uint32_t varying)
{
    count = gl_whole_primitives(mode, count);
    if (!count)
        return;
    if (draw_in_pieces(ctx, mode, vertices, count, varying))
        return;
    gl_device_lock();
    GlVertexInput in;
    GlProgram *p = begin_draw(ctx, &in, TRUE, mode);
    if (!p) {
        gl_device_unlock();
        return;
    }
    /* attributes that vary per vertex go into the vertices, the rest become constants. */
    uint32_t stride = 16, color = 0, normal = 0, texcoord = 0;
    if (varying & 1) {
        color = stride;
        stride += 16;
    }
    if (varying & 2) {
        normal = stride;
        stride += 16;
    }
    if (varying & 4) {
        texcoord = stride;
        stride += 16;
    }
    uint64_t bytes = (uint64_t)count * stride;
    BOOL heap = bytes > (1u << 20);
    GlMem mem, constants;
    if (!(heap ? gl_heap_alloc(bytes, &mem) : gl_stream_alloc(bytes, 16, &mem)) ||
        !gl_stream_alloc(48, 16, &constants)) {
        gl_device_unlock();
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        const GlFfVertex *v = &vertices[i];
        uint8_t *d = mem.ptr + (uint64_t)i * stride;
        memcpy(d, v->position, 16);
        if (color)
            memcpy(d + color, v->color, 16);
        if (normal)
            memcpy(d + normal, v->normal, 16);
        if (texcoord)
            memcpy(d + texcoord, v->texcoord, 16);
    }
    memcpy(constants.ptr, vertices[0].color, 16);
    memcpy(constants.ptr + 16, vertices[0].normal, 16);
    memcpy(constants.ptr + 32, vertices[0].texcoord, 16);
    const uint32_t at[4] = {0, color, normal, texcoord};
    draw_stored(ctx, p, &in, mode, count, &mem, TRUE, stride, at, &constants, nullptr);
    if (heap)
        gl_heap_free(&mem, gl_batch_fence());
    gl_device_unlock();
}

/* ---------------------------------------------------------------- display list batches */

void gl_batch_free(GlDlBatch *b)
{
    if (!b)
        return;
    gl_heap_free(&b->vb, b->last_use);
    gl_heap_free(&b->idx[0], b->last_use);
    gl_heap_free(&b->idx[1], b->last_use);
    gl_free(b->cpu);
    gl_free(b);
}

/* the vertices of a batch as a glBegin collects them, with the current values where the list did not know them. */
static GlFfVertex *batch_vertices(GlContext *ctx, const GlDlBatch *b, uint32_t *varying)
{
    GlFfVertex *v = gl_alloc((SIZE_T)b->count * sizeof(GlFfVertex));
    if (!v)
        return nullptr;
    const GlFixed *f = &ctx->ff;
    for (uint32_t i = 0; i < b->count; i++) {
        GlFfVertex *d = &v[i];
        if (b->cpu) {
            *d = b->cpu[i];
            continue;
        }
        const uint8_t *s = b->vb.ptr + (uint64_t)i * b->stride;
        memcpy(d->position, s, 16);
        for (int a = 0; a < 3; a++) {
            GLfloat *to = a == 0 ? d->color : a == 1 ? d->normal : d->texcoord;
            if (b->offset[a])
                memcpy(to, s + b->offset[a], 16);
            else
                memcpy(to, b->constant[a], 16);
        }
    }
    for (int a = 0; a < 3; a++) {
        uint32_t first = (b->dep >> a) & 1 ? b->count : (b->mixed >> a) & 1 ? b->prefix[a] : 0;
        for (uint32_t i = 0; i < first; i++) {
            GlFfVertex *d = &v[i];
            memcpy(a == 0 ? d->color : a == 1 ? d->normal : d->texcoord, f->current[a], 16);
        }
    }
    *varying = b->varying | b->mixed;
    return v;
}

/* one fixed function draw of a batch of a display list. */
void gl_draw_batch(GlContext *ctx, GlDlBatch *b)
{
    uint32_t count = gl_whole_primitives(b->mode, b->count);
    if (!count)
        return;
    if (b->cpu || b->mixed || gl_lines_wanted(ctx, b->mode)) {
        uint32_t varying;
        GlFfVertex *v = batch_vertices(ctx, b, &varying);
        if (!v)
            return;
        if (gl_lines_wanted(ctx, b->mode))
            gl_lines_draw(ctx, b->mode, v, count);
        else
            gl_draw_vertices(ctx, b->mode, v, count, varying);
        gl_free(v);
        return;
    }
    gl_device_lock();
    GlVertexInput in;
    GlProgram *p = begin_draw(ctx, &in, TRUE, b->mode);
    if (!p) {
        gl_device_unlock();
        return;
    }
    GlMem constants;
    if (!gl_stream_alloc(48, 16, &constants)) {
        gl_device_unlock();
        return;
    }
    for (int a = 0; a < 3; a++)
        memcpy(constants.ptr + 16 * a, (b->dep >> a) & 1 ? ctx->ff.current[a] : b->constant[a], 16);
    const uint32_t at[4] = {0, b->offset[0], b->offset[1], b->offset[2]};
    draw_stored(ctx, p, &in, b->mode, count, &b->vb, FALSE, b->stride, at, &constants, b);
    b->last_use = gl_batch_fence();
    gl_device_unlock();
}
