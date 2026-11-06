/*
 * glBegin and glEnd, the fixed function client arrays and glArrayElement. vertices between glBegin and glEnd are
 * collected here and drawn at glEnd in one draw by gl_draw_immediate in draw.c.
 */
#include "immediate.h"

BOOL gl_ff_grow(GlFixed *f)
{
    uint32_t cap = f->vertex_cap ? f->vertex_cap * 2 : 256;
    if (cap < f->vertex_cap || cap > (0x40000000u / sizeof(GlFfVertex)))
        return FALSE;
    GlFfVertex *v = gl_realloc(f->vertices, (SIZE_T)cap * sizeof(GlFfVertex));
    if (!v)
        return FALSE;
    f->vertices = v;
    f->vertex_cap = cap;
    return TRUE;
}

GLAPI void APIENTRY glBegin(GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    if (mode > GL_POLYGON) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlFixed *f = &ctx->ff;
    f->in_begin = TRUE;
    f->begin_mode = mode;
    f->vertex_count = 0;
    f->varying = 0;
}

GLAPI void APIENTRY glEnd(void)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    GlFixed *f = &ctx->ff;
    if (!f->in_begin) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    f->in_begin = FALSE;
    gl_draw_immediate(ctx);
    /* a huge batch does not keep its memory. */
    if (f->vertex_cap > (1u << 16)) {
        gl_free(f->vertices);
        f->vertices = nullptr;
        f->vertex_cap = 0;
    }
}

GLAPI void APIENTRY glEdgeFlag(GLboolean flag)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    ctx->ff.edge_flag = flag;
}

GLAPI void APIENTRY glEdgeFlagv(const GLboolean *flag)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    ctx->ff.edge_flag = *flag;
}

/* a rectangle is a polygon of four vertices. */
static void rectangle(GlContext *ctx, GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    GlFixed *f = &ctx->ff;
    f->in_begin = TRUE;
    f->begin_mode = GL_POLYGON;
    f->vertex_count = 0;
    f->varying = 0;
    gl_ff_vertex(ctx, x1, y1, 0, 1);
    gl_ff_vertex(ctx, x2, y1, 0, 1);
    gl_ff_vertex(ctx, x2, y2, 0, 1);
    gl_ff_vertex(ctx, x1, y2, 0, 1);
    f->in_begin = FALSE;
    gl_draw_immediate(ctx);
}

GLAPI void APIENTRY glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    GL_CONTEXT_OR_RETURN();
    rectangle(ctx, x1, y1, x2, y2);
}

GLAPI void APIENTRY glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2)
{
    GL_CONTEXT_OR_RETURN();
    rectangle(ctx, (GLfloat)x1, (GLfloat)y1, (GLfloat)x2, (GLfloat)y2);
}

GLAPI void APIENTRY glRecti(GLint x1, GLint y1, GLint x2, GLint y2)
{
    GL_CONTEXT_OR_RETURN();
    rectangle(ctx, (GLfloat)x1, (GLfloat)y1, (GLfloat)x2, (GLfloat)y2);
}

GLAPI void APIENTRY glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2)
{
    GL_CONTEXT_OR_RETURN();
    rectangle(ctx, (GLfloat)x1, (GLfloat)y1, (GLfloat)x2, (GLfloat)y2);
}

GLAPI void APIENTRY glRectfv(const GLfloat *v1, const GLfloat *v2)
{
    glRectf(v1[0], v1[1], v2[0], v2[1]);
}

GLAPI void APIENTRY glRectdv(const GLdouble *v1, const GLdouble *v2)
{
    glRectd(v1[0], v1[1], v2[0], v2[1]);
}

GLAPI void APIENTRY glRectiv(const GLint *v1, const GLint *v2)
{
    glRecti(v1[0], v1[1], v2[0], v2[1]);
}

GLAPI void APIENTRY glRectsv(const GLshort *v1, const GLshort *v2)
{
    glRects(v1[0], v1[1], v2[0], v2[1]);
}

/* ---------------------------------------------------------------- client arrays */

/* the array of a client state, or NULL for those that are accepted and do nothing. */
static GlVertexAttrib *client_array(GlContext *ctx, GLenum array)
{
    switch (array) {
    case GL_VERTEX_ARRAY:
        return &ctx->vao->legacy[0];
    case GL_COLOR_ARRAY:
        return &ctx->vao->legacy[1];
    case GL_NORMAL_ARRAY:
        return &ctx->vao->legacy[2];
    case GL_TEXTURE_COORD_ARRAY:
        return &ctx->vao->legacy[3 + (ctx->ff.client_unit < FF_UNITS ? ctx->ff.client_unit : FF_UNITS - 1)];
    default:
        return nullptr;
    }
}

static void client_state(GLenum array, BOOL on)
{
    GL_CONTEXT_OR_RETURN();
    GlVertexAttrib *a = client_array(ctx, array);
    if (a) {
        a->enabled = on;
        ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
        return;
    }
    int unused = gl_ff_unused_array(array);
    if (unused < 0) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->ff.unused_arrays[unused] = on;
    /* the secondary color and fog coordinate arrays feed the built-in inputs of GLSL, which alias attributes 4 and 5. */
    if (array == GL_SECONDARY_COLOR_ARRAY || array == GL_FOG_COORD_ARRAY) {
        ctx->vao->attribs[array == GL_FOG_COORD_ARRAY ? 5 : 4].enabled = on;
        ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
    }
}

GLAPI void APIENTRY glEnableClientState(GLenum array)
{
    client_state(array, TRUE);
}

GLAPI void APIENTRY glDisableClientState(GLenum array)
{
    client_state(array, FALSE);
}

GLAPI void APIENTRY glClientActiveTexture(GLenum texture)
{
    GL_CONTEXT_OR_RETURN();
    if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + 32) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->ff.client_unit = texture - GL_TEXTURE0;
}

GLAPI void APIENTRY glClientActiveTextureARB(GLenum texture)
{
    glClientActiveTexture(texture);
}

/* size and type were already checked. */
static void array_pointer(GlContext *ctx, GlVertexAttrib *a, GLint size, GLenum type, BOOL normalized, GLsizei stride,
                          const void *pointer)
{
    if (!a)
        return;
    a->size = size;
    a->type = type;
    a->normalized = normalized;
    a->integer = FALSE;
    a->stride = stride;
    a->pointer = pointer;
    a->buffer = ctx->array_buffer;
    ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
}

static BOOL type_in(GLenum type, const GLenum *list, int count)
{
    for (int i = 0; i < count; i++)
        if (list[i] == type)
            return TRUE;
    return FALSE;
}

GLAPI void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    GL_CONTEXT_OR_RETURN();
    static const GLenum types[] = {GL_SHORT, GL_INT, GL_FLOAT, GL_DOUBLE};
    if (!type_in(type, types, 4)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (size < 2 || size > 4 || stride < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    array_pointer(ctx, client_array(ctx, GL_VERTEX_ARRAY), size, type, FALSE, stride, pointer);
}

GLAPI void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    GL_CONTEXT_OR_RETURN();
    static const GLenum types[] = {GL_BYTE, GL_UNSIGNED_BYTE, GL_SHORT, GL_UNSIGNED_SHORT,
                                   GL_INT,  GL_UNSIGNED_INT,  GL_FLOAT, GL_DOUBLE};
    if (!type_in(type, types, 8)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if ((size != 3 && size != 4) || stride < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    array_pointer(ctx, client_array(ctx, GL_COLOR_ARRAY), size, type, TRUE, stride, pointer);
}

GLAPI void APIENTRY glNormalPointer(GLenum type, GLsizei stride, const void *pointer)
{
    GL_CONTEXT_OR_RETURN();
    static const GLenum types[] = {GL_BYTE, GL_SHORT, GL_INT, GL_FLOAT, GL_DOUBLE};
    if (!type_in(type, types, 5)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (stride < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    array_pointer(ctx, client_array(ctx, GL_NORMAL_ARRAY), 3, type, TRUE, stride, pointer);
}

GLAPI void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    GL_CONTEXT_OR_RETURN();
    static const GLenum types[] = {GL_SHORT, GL_INT, GL_FLOAT, GL_DOUBLE};
    if (!type_in(type, types, 4)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (size < 1 || size > 4 || stride < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    array_pointer(ctx, client_array(ctx, GL_TEXTURE_COORD_ARRAY), size, type, FALSE, stride, pointer);
}

/* edge flags, color indexes, secondary colors and fog coordinates are accepted and do nothing. */
GLAPI void APIENTRY glEdgeFlagPointer(GLsizei stride, const void *pointer)
{
    GL_CONTEXT_OR_RETURN();
    if (stride < 0)
        gl_error(ctx, GL_INVALID_VALUE);
}

GLAPI void APIENTRY glIndexPointer(GLenum type, GLsizei stride, const void *pointer)
{
    GL_CONTEXT_OR_RETURN();
    if (type != GL_UNSIGNED_BYTE && type != GL_SHORT && type != GL_INT && type != GL_FLOAT && type != GL_DOUBLE)
        gl_error(ctx, GL_INVALID_ENUM);
    else if (stride < 0)
        gl_error(ctx, GL_INVALID_VALUE);
}

/* glInterleavedArrays formats: sizes of the texture coordinate, color, normal and vertex. */
typedef struct Interleaved {
    GLenum format;
    int texture, color, normal, vertex;
    BOOL color_bytes;
} Interleaved;

static const Interleaved g_interleaved[] = {
    {GL_V2F, 0, 0, 0, 2, FALSE},
    {GL_V3F, 0, 0, 0, 3, FALSE},
    {GL_C4UB_V2F, 0, 4, 0, 2, TRUE},
    {GL_C4UB_V3F, 0, 4, 0, 3, TRUE},
    {GL_C3F_V3F, 0, 3, 0, 3, FALSE},
    {GL_N3F_V3F, 0, 0, 3, 3, FALSE},
    {GL_C4F_N3F_V3F, 0, 4, 3, 3, FALSE},
    {GL_T2F_V3F, 2, 0, 0, 3, FALSE},
    {GL_T4F_V4F, 4, 0, 0, 4, FALSE},
    {GL_T2F_C4UB_V3F, 2, 4, 0, 3, TRUE},
    {GL_T2F_C3F_V3F, 2, 3, 0, 3, FALSE},
    {GL_T2F_N3F_V3F, 2, 0, 3, 3, FALSE},
    {GL_T2F_C4F_N3F_V3F, 2, 4, 3, 3, FALSE},
    {GL_T4F_C4F_N3F_V4F, 4, 4, 3, 4, FALSE},
};

GLAPI void APIENTRY glInterleavedArrays(GLenum format, GLsizei stride, const void *pointer)
{
    GL_CONTEXT_OR_RETURN();
    const Interleaved *layout = nullptr;
    for (size_t i = 0; i < sizeof(g_interleaved) / sizeof(g_interleaved[0]); i++)
        if (g_interleaved[i].format == format)
            layout = &g_interleaved[i];
    if (!layout) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (stride < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    /* a byte color takes a whole float's room. */
    GLsizei f = (GLsizei)sizeof(GLfloat);
    GLsizei color_bytes = layout->color_bytes ? f : layout->color * f;
    GLsizei natural = (layout->texture + layout->normal + layout->vertex) * f + color_bytes;
    if (!stride)
        stride = natural;
    const uint8_t *base = pointer;
    GLsizei offset = 0;
    GlVertexAttrib *tex = client_array(ctx, GL_TEXTURE_COORD_ARRAY);
    GlVertexAttrib *col = client_array(ctx, GL_COLOR_ARRAY);
    GlVertexAttrib *nor = client_array(ctx, GL_NORMAL_ARRAY);
    GlVertexAttrib *ver = client_array(ctx, GL_VERTEX_ARRAY);
    tex->enabled = layout->texture != 0;
    col->enabled = layout->color != 0;
    nor->enabled = layout->normal != 0;
    ver->enabled = TRUE;
    if (layout->texture) {
        array_pointer(ctx, tex, layout->texture, GL_FLOAT, FALSE, stride, base + offset);
        offset += layout->texture * f;
    }
    if (layout->color) {
        array_pointer(ctx, col, layout->color, layout->color_bytes ? GL_UNSIGNED_BYTE : GL_FLOAT, TRUE, stride,
                      base + offset);
        offset += color_bytes;
    }
    if (layout->normal) {
        array_pointer(ctx, nor, 3, GL_FLOAT, TRUE, stride, base + offset);
        offset += layout->normal * f;
    }
    array_pointer(ctx, ver, layout->vertex, GL_FLOAT, FALSE, stride, base + offset);
    for (int i = 0; i < 4; i++)
        ctx->ff.unused_arrays[i] = FALSE;
}

/* ---------------------------------------------------------------- glArrayElement */

/* reads vertex i of an array as floats, scaled to the range of an integer type when the array is normalized. */
int gl_read_array(GlContext *ctx, const GlVertexAttrib *a, GLint i, GLfloat *out)
{
    if (!a->enabled)
        return 0;
    const uint8_t *base = a->pointer;
    if (a->buffer) {
        GlBuffer *b = gl_buffer(ctx, a->buffer);
        if (!b || !b->mem.ptr)
            return 0;
        base = b->mem.ptr + (ULONG_PTR)a->pointer;
    }
    if (!base)
        return 0;
    uint32_t elem;
    switch (a->type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        elem = 1;
        break;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        elem = 2;
        break;
    case GL_DOUBLE:
        elem = 8;
        break;
    default:
        elem = 4;
        break;
    }
    uint32_t stride = a->stride ? (uint32_t)a->stride : elem * (uint32_t)a->size;
    const uint8_t *p = base + (uint64_t)i * stride;
    for (int c = 0; c < a->size; c++) {
        const uint8_t *q = p + c * elem;
        double v;
        switch (a->type) {
        case GL_BYTE:
            v = *(const int8_t *)q;
            if (a->normalized)
                v = (2.0 * v + 1.0) / 255.0;
            break;
        case GL_UNSIGNED_BYTE:
            v = *q;
            if (a->normalized)
                v /= 255.0;
            break;
        case GL_SHORT: {
            int16_t s;
            memcpy(&s, q, 2);
            v = s;
            if (a->normalized)
                v = (2.0 * v + 1.0) / 65535.0;
            break;
        }
        case GL_UNSIGNED_SHORT: {
            uint16_t s;
            memcpy(&s, q, 2);
            v = s;
            if (a->normalized)
                v /= 65535.0;
            break;
        }
        case GL_INT: {
            int32_t s;
            memcpy(&s, q, 4);
            v = s;
            if (a->normalized)
                v = (2.0 * v + 1.0) / 4294967295.0;
            break;
        }
        case GL_UNSIGNED_INT: {
            uint32_t s;
            memcpy(&s, q, 4);
            v = s;
            if (a->normalized)
                v /= 4294967295.0;
            break;
        }
        case GL_DOUBLE: {
            double s;
            memcpy(&s, q, 8);
            v = s;
            break;
        }
        default: {
            float s;
            memcpy(&s, q, 4);
            v = s;
            break;
        }
        }
        out[c] = (GLfloat)v;
    }
    return a->size;
}

GLAPI void APIENTRY glArrayElement(GLint i)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    const GlVertexArray *vao = ctx->vao;
    GLfloat v[4];
    int n = gl_read_array(ctx, &vao->legacy[1], i, v);
    if (n)
        gl_ff_color(ctx, v[0], v[1], v[2], n > 3 ? v[3] : 1);
    n = gl_read_array(ctx, &vao->legacy[2], i, v);
    if (n)
        gl_ff_normal(ctx, v[0], v[1], v[2]);
    for (GLuint unit = 0; unit < FF_UNITS; unit++) {
        GLfloat t[4] = {0, 0, 0, 1};
        n = gl_read_array(ctx, &vao->legacy[3 + unit], i, t);
        if (n)
            gl_ff_texcoord(ctx, unit, t[0], n > 1 ? t[1] : 0, n > 2 ? t[2] : 0, n > 3 ? t[3] : 1);
    }
    GLfloat p[4] = {0, 0, 0, 1};
    n = gl_read_array(ctx, &vao->legacy[0], i, p);
    if (n)
        gl_ff_vertex(ctx, p[0], p[1], n > 2 ? p[2] : 0, n > 3 ? p[3] : 1);
}

/* the vertex glArrayElement would send for index i, with the current values for the arrays that are not enabled. */
void gl_ff_array_vertex(GlContext *ctx, GLint i, GlFfVertex *out)
{
    const GlFixed *f = &ctx->ff;
    const GlVertexArray *vao = ctx->vao;
    memcpy(out->color, f->current[0], 16);
    memcpy(out->normal, f->current[1], 16);
    memcpy(out->texcoord, f->current[2], 16);
    GLfloat v[4];
    int n = gl_read_array(ctx, &vao->legacy[1], i, v);
    if (n) {
        memcpy(out->color, v, 3 * sizeof(GLfloat));
        out->color[3] = n > 3 ? v[3] : 1;
    }
    n = gl_read_array(ctx, &vao->legacy[2], i, v);
    if (n)
        memcpy(out->normal, v, 3 * sizeof(GLfloat));
    GLfloat t[4] = {0, 0, 0, 1};
    n = gl_read_array(ctx, &vao->legacy[3], i, t);
    if (n)
        memcpy(out->texcoord, t, sizeof(t));
    GLfloat p[4] = {0, 0, 0, 1};
    gl_read_array(ctx, &vao->legacy[0], i, p);
    memcpy(out->position, p, sizeof(p));
}
