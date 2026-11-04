/*
 * the user clip planes, the polygon stipple and the logic operation of the fixed function pipeline. a plane is kept in eye
 * coordinates, as glClipPlane transformed it, and the vertex shader measures the distance of each vertex to the enabled ones.
 * the stipple is a 32 by 32 mask the fragment shader looks at by window position. the logic operation goes to the host with
 * the pipeline, which applies it as the hardware would.
 */
#include "fixed.h"

/* ---------------------------------------------------------------- clip planes */

/* the inverse of a column major matrix; FALSE when it has none. */
static BOOL invert(const GLfloat *m, GLdouble *r)
{
    double a[16], inv[16];
    for (int i = 0; i < 16; i++)
        a[i] = m[i];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] -
             a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] +
             a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] -
             a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] +
              a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] +
             a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] -
             a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] +
             a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] -
              a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] -
             a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] +
             a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] -
              a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] +
              a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] +
             a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] -
             a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] +
              a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] -
              a[8] * a[2] * a[5];
    double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (det == 0)
        return FALSE;
    det = 1.0 / det;
    for (int i = 0; i < 16; i++)
        r[i] = inv[i] * det;
    return TRUE;
}

GLAPI void APIENTRY glClipPlane(GLenum plane, const GLdouble *equation)
{
    GL_CONTEXT_OR_RETURN();
    if (plane < GL_CLIP_PLANE0 || plane >= GL_CLIP_PLANE0 + FF_CLIP_PLANES) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlFixed *f = &ctx->ff;
    GLdouble inverse[16];
    GLdouble *eye = f->clip_plane[plane - GL_CLIP_PLANE0];
    /* the plane in eye coordinates is the equation times the inverse of the modelview matrix. */
    if (invert(f->modelview[f->modelview_depth], inverse)) {
        for (int j = 0; j < 4; j++) {
            GLdouble s = 0;
            for (int i = 0; i < 4; i++)
                s += equation[i] * inverse[j * 4 + i];
            eye[j] = s;
        }
    } else {
        for (int j = 0; j < 4; j++)
            eye[j] = equation[j];
    }
    f->dirty = TRUE;
}

GLAPI void APIENTRY glGetClipPlane(GLenum plane, GLdouble *equation)
{
    GL_CONTEXT_OR_RETURN();
    if (plane < GL_CLIP_PLANE0 || plane >= GL_CLIP_PLANE0 + FF_CLIP_PLANES) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    for (int i = 0; i < 4; i++)
        equation[i] = ctx->ff.clip_plane[plane - GL_CLIP_PLANE0][i];
}

BOOL gl_clip_planes_enabled(GlContext *ctx)
{
    for (int i = 0; i < FF_CLIP_PLANES; i++)
        if (gl_ff_stored(ctx, GL_CLIP_PLANE0 + (GLenum)i))
            return TRUE;
    return FALSE;
}

/* ---------------------------------------------------------------- the logic operation */

GLAPI void APIENTRY glLogicOp(GLenum opcode)
{
    GL_CONTEXT_OR_RETURN();
    if (opcode < GL_CLEAR || opcode > GL_SET) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (ctx->ff.logic_op != opcode) {
        ctx->ff.logic_op = opcode;
        ctx->dirty |= DIRTY_PIPELINE;
    }
}

/* the operation the pipeline applies: 0 for none, else 1 and the number of the operation from GL_CLEAR. */
uint8_t gl_logic_op(GlContext *ctx)
{
    if (!gl_ff_stored(ctx, GL_COLOR_LOGIC_OP))
        return 0;
    return (uint8_t)(1 + ctx->ff.logic_op - GL_CLEAR);
}

/* ---------------------------------------------------------------- the polygon stipple */

/* the bits of a 32 by 32 bitmap read with the pixel store: rows of four bytes, the most significant bit of a byte first. */
static void unpack_mask(GlContext *ctx, const GLubyte *mask, GLubyte *out)
{
    const uint8_t *src = mask;
    if (ctx->pixel_unpack) {
        GlBuffer *b = gl_buffer(ctx, ctx->pixel_unpack);
        if (!b || !b->mem.ptr)
            return;
        src = b->mem.ptr + (ULONG_PTR)mask;
    }
    uint64_t row_length = ctx->unpack_row_length > 0 ? (uint64_t)ctx->unpack_row_length : 32;
    uint64_t align = ctx->unpack_alignment > 0 ? (uint64_t)ctx->unpack_alignment : 1;
    uint64_t stride = ((row_length + 7) / 8 + align - 1) / align * align;
    src += (uint64_t)(ctx->unpack_skip_rows > 0 ? ctx->unpack_skip_rows : 0) * stride;
    uint64_t skip = ctx->unpack_skip_pixels > 0 ? (uint64_t)ctx->unpack_skip_pixels : 0;
    memset(out, 0, 128);
    for (int y = 0; y < 32; y++) {
        const uint8_t *row = src + (uint64_t)y * stride;
        for (int x = 0; x < 32; x++) {
            uint64_t bit = skip + (uint64_t)x;
            unsigned shift = ctx->unpack_lsb_first ? (unsigned)(bit & 7) : 7 - (unsigned)(bit & 7);
            if ((row[bit >> 3] >> shift) & 1)
                out[y * 4 + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
        }
    }
}

GLAPI void APIENTRY glPolygonStipple(const GLubyte *mask)
{
    GL_CONTEXT_OR_RETURN();
    if (!mask && !ctx->pixel_unpack)
        return;
    unpack_mask(ctx, mask, ctx->ff.polygon_stipple);
    ctx->ff.dirty = TRUE;
}

GLAPI void APIENTRY glGetPolygonStipple(GLubyte *mask)
{
    GL_CONTEXT_OR_RETURN();
    uint8_t *dst = mask;
    if (ctx->pixel_pack) {
        GlBuffer *b = gl_buffer(ctx, ctx->pixel_pack);
        if (!b || !b->mem.ptr)
            return;
        dst = b->mem.ptr + (ULONG_PTR)mask;
    }
    if (!dst)
        return;
    uint64_t row_length = ctx->pack_row_length > 0 ? (uint64_t)ctx->pack_row_length : 32;
    uint64_t align = ctx->pack_alignment > 0 ? (uint64_t)ctx->pack_alignment : 1;
    uint64_t stride = ((row_length + 7) / 8 + align - 1) / align * align;
    dst += (uint64_t)(ctx->pack_skip_rows > 0 ? ctx->pack_skip_rows : 0) * stride +
           (uint64_t)(ctx->pack_skip_pixels > 0 ? ctx->pack_skip_pixels : 0) / 8;
    for (int y = 0; y < 32; y++)
        memcpy(dst + (uint64_t)y * stride, ctx->ff.polygon_stipple + y * 4, 4);
}

/* the mask as the shader reads it: for each row two numbers of sixteen bits, the bit of a column set when it is drawn. */
void gl_stipple_pack(const GlContext *ctx, GLfloat (*u)[4])
{
    for (int row = 0; row < 32; row++) {
        uint32_t bits = 0;
        for (int x = 0; x < 32; x++)
            if ((ctx->ff.polygon_stipple[row * 4 + (x >> 3)] >> (7 - (x & 7))) & 1)
                bits |= 1u << x;
        GLfloat *slot = u[FF_U_STIPPLE + row / 2] + (row & 1) * 2;
        slot[0] = (GLfloat)(bits & 0xFFFF);
        slot[1] = (GLfloat)(bits >> 16);
    }
}
