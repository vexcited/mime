/*
 * the commands OpenGL 1.2 to 3.3 added to the vertex pipeline and that the other files do not need: the secondary
 * color, the fog coordinate and the window position, the generic vertex attributes in every type, and the packed
 * vertex formats. the secondary color and the fog coordinate are state: queried, kept by lists and push, and not read by
 * the fixed function pipeline.
 */
#include "immediate.h"

void APIENTRY glVertexAttrib1f(GLuint index, GLfloat x);
void APIENTRY glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y);
void APIENTRY glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z);
void APIENTRY glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void APIENTRY glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w);
void APIENTRY glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w);
void APIENTRY glVertex2f(GLfloat x, GLfloat y);
void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void APIENTRY glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void APIENTRY glNormal3f(GLfloat x, GLfloat y, GLfloat z);
void APIENTRY glColor3f(GLfloat r, GLfloat g, GLfloat b);
void APIENTRY glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void APIENTRY glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void APIENTRY glSecondaryColor3f(GLfloat r, GLfloat g, GLfloat b);

#define S_B(x) ((GLfloat)((2.0 * (x) + 1.0) / 255.0))
#define S_UB(x) ((GLfloat)((x) / 255.0))
#define S_S(x) ((GLfloat)((2.0 * (x) + 1.0) / 65535.0))
#define S_US(x) ((GLfloat)((x) / 65535.0))
#define S_I(x) ((GLfloat)((2.0 * (x) + 1.0) / 4294967295.0))
#define S_UI(x) ((GLfloat)((x) / 4294967295.0))
#define S_F(x) ((GLfloat)(x))

static void secondary_color(GlContext *ctx, GLfloat r, GLfloat g, GLfloat b)
{
    GLfloat *c = ctx->ff.secondary;
    c[0] = r;
    c[1] = g;
    c[2] = b;
    c[3] = 1;
}

GLAPI void APIENTRY glSecondaryColor3b(GLbyte red, GLbyte green, GLbyte blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_B(red), S_B(green), S_B(blue));
}

GLAPI void APIENTRY glSecondaryColor3bv(const GLbyte *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_B(v[0]), S_B(v[1]), S_B(v[2]));
}

GLAPI void APIENTRY glSecondaryColor3d(GLdouble red, GLdouble green, GLdouble blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_F(red), S_F(green), S_F(blue));
}

GLAPI void APIENTRY glSecondaryColor3dv(const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_F(v[0]), S_F(v[1]), S_F(v[2]));
}

GLAPI void APIENTRY glSecondaryColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_F(red), S_F(green), S_F(blue));
}

GLAPI void APIENTRY glSecondaryColor3fv(const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_F(v[0]), S_F(v[1]), S_F(v[2]));
}

GLAPI void APIENTRY glSecondaryColor3i(GLint red, GLint green, GLint blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_I(red), S_I(green), S_I(blue));
}

GLAPI void APIENTRY glSecondaryColor3iv(const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_I(v[0]), S_I(v[1]), S_I(v[2]));
}

GLAPI void APIENTRY glSecondaryColor3s(GLshort red, GLshort green, GLshort blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_S(red), S_S(green), S_S(blue));
}

GLAPI void APIENTRY glSecondaryColor3sv(const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_S(v[0]), S_S(v[1]), S_S(v[2]));
}

GLAPI void APIENTRY glSecondaryColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_UB(red), S_UB(green), S_UB(blue));
}

GLAPI void APIENTRY glSecondaryColor3ubv(const GLubyte *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_UB(v[0]), S_UB(v[1]), S_UB(v[2]));
}

GLAPI void APIENTRY glSecondaryColor3ui(GLuint red, GLuint green, GLuint blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_UI(red), S_UI(green), S_UI(blue));
}

GLAPI void APIENTRY glSecondaryColor3uiv(const GLuint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_UI(v[0]), S_UI(v[1]), S_UI(v[2]));
}

GLAPI void APIENTRY glSecondaryColor3us(GLushort red, GLushort green, GLushort blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_US(red), S_US(green), S_US(blue));
}

GLAPI void APIENTRY glSecondaryColor3usv(const GLushort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    secondary_color(ctx, S_US(v[0]), S_US(v[1]), S_US(v[2]));
}

GLAPI void APIENTRY glFogCoordf(GLfloat coord)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    ctx->ff.fog_coord = (GLfloat)coord;
}

GLAPI void APIENTRY glFogCoordfv(const GLfloat *coord)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    ctx->ff.fog_coord = (GLfloat)coord[0];
}

GLAPI void APIENTRY glFogCoordd(GLdouble coord)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    ctx->ff.fog_coord = (GLfloat)coord;
}

GLAPI void APIENTRY glFogCoorddv(const GLdouble *coord)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    ctx->ff.fog_coord = (GLfloat)coord[0];
}

/* the window position is the raster position without the transformation: z is clamped and goes through the depth range. */
static void window_position(GLfloat x, GLfloat y, GLfloat z)
{
    GL_CONTEXT_OR_RETURN();
    GlRaster *r = &ctx->raster;
    GLfloat clamped = z < 0 ? 0 : z > 1 ? 1 : z;
    r->valid = TRUE;
    r->position[0] = x;
    r->position[1] = y;
    r->position[2] = (GLfloat)(ctx->depth_range[0] + clamped * (ctx->depth_range[1] - ctx->depth_range[0]));
    r->position[3] = 1;
    memcpy(r->color, ctx->ff.current[0], sizeof(r->color));
}

GLAPI void APIENTRY glWindowPos2d(GLdouble x, GLdouble y)
{
    window_position((GLfloat)x, (GLfloat)y, 0);
}

GLAPI void APIENTRY glWindowPos2dv(const GLdouble *v)
{
    window_position((GLfloat)v[0], (GLfloat)v[1], 0);
}

GLAPI void APIENTRY glWindowPos2f(GLfloat x, GLfloat y)
{
    window_position((GLfloat)x, (GLfloat)y, 0);
}

GLAPI void APIENTRY glWindowPos2fv(const GLfloat *v)
{
    window_position((GLfloat)v[0], (GLfloat)v[1], 0);
}

GLAPI void APIENTRY glWindowPos2i(GLint x, GLint y)
{
    window_position((GLfloat)x, (GLfloat)y, 0);
}

GLAPI void APIENTRY glWindowPos2iv(const GLint *v)
{
    window_position((GLfloat)v[0], (GLfloat)v[1], 0);
}

GLAPI void APIENTRY glWindowPos2s(GLshort x, GLshort y)
{
    window_position((GLfloat)x, (GLfloat)y, 0);
}

GLAPI void APIENTRY glWindowPos2sv(const GLshort *v)
{
    window_position((GLfloat)v[0], (GLfloat)v[1], 0);
}

GLAPI void APIENTRY glWindowPos3d(GLdouble x, GLdouble y, GLdouble z)
{
    window_position((GLfloat)x, (GLfloat)y, (GLfloat)z);
}

GLAPI void APIENTRY glWindowPos3dv(const GLdouble *v)
{
    window_position((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2]);
}

GLAPI void APIENTRY glWindowPos3f(GLfloat x, GLfloat y, GLfloat z)
{
    window_position((GLfloat)x, (GLfloat)y, (GLfloat)z);
}

GLAPI void APIENTRY glWindowPos3fv(const GLfloat *v)
{
    window_position((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2]);
}

GLAPI void APIENTRY glWindowPos3i(GLint x, GLint y, GLint z)
{
    window_position((GLfloat)x, (GLfloat)y, (GLfloat)z);
}

GLAPI void APIENTRY glWindowPos3iv(const GLint *v)
{
    window_position((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2]);
}

GLAPI void APIENTRY glWindowPos3s(GLshort x, GLshort y, GLshort z)
{
    window_position((GLfloat)x, (GLfloat)y, (GLfloat)z);
}

GLAPI void APIENTRY glWindowPos3sv(const GLshort *v)
{
    window_position((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2]);
}

/* ---------------------------------------------------------------- generic attributes */

GLAPI void APIENTRY glVertexAttrib1d(GLuint index, GLdouble x)
{
    glVertexAttrib1f(index, (GLfloat)x);
}

GLAPI void APIENTRY glVertexAttrib1dv(GLuint index, const GLdouble *v)
{
    glVertexAttrib1f(index, (GLfloat)v[0]);
}

GLAPI void APIENTRY glVertexAttrib1s(GLuint index, GLshort x)
{
    glVertexAttrib1f(index, (GLfloat)x);
}

GLAPI void APIENTRY glVertexAttrib1sv(GLuint index, const GLshort *v)
{
    glVertexAttrib1f(index, (GLfloat)v[0]);
}

GLAPI void APIENTRY glVertexAttrib2d(GLuint index, GLdouble x, GLdouble y)
{
    glVertexAttrib2f(index, (GLfloat)x, (GLfloat)y);
}

GLAPI void APIENTRY glVertexAttrib2dv(GLuint index, const GLdouble *v)
{
    glVertexAttrib2f(index, (GLfloat)v[0], (GLfloat)v[1]);
}

GLAPI void APIENTRY glVertexAttrib2s(GLuint index, GLshort x, GLshort y)
{
    glVertexAttrib2f(index, (GLfloat)x, (GLfloat)y);
}

GLAPI void APIENTRY glVertexAttrib2sv(GLuint index, const GLshort *v)
{
    glVertexAttrib2f(index, (GLfloat)v[0], (GLfloat)v[1]);
}

GLAPI void APIENTRY glVertexAttrib3d(GLuint index, GLdouble x, GLdouble y, GLdouble z)
{
    glVertexAttrib3f(index, (GLfloat)x, (GLfloat)y, (GLfloat)z);
}

GLAPI void APIENTRY glVertexAttrib3dv(GLuint index, const GLdouble *v)
{
    glVertexAttrib3f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2]);
}

GLAPI void APIENTRY glVertexAttrib3s(GLuint index, GLshort x, GLshort y, GLshort z)
{
    glVertexAttrib3f(index, (GLfloat)x, (GLfloat)y, (GLfloat)z);
}

GLAPI void APIENTRY glVertexAttrib3sv(GLuint index, const GLshort *v)
{
    glVertexAttrib3f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2]);
}

GLAPI void APIENTRY glVertexAttrib4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
    glVertexAttrib4f(index, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

GLAPI void APIENTRY glVertexAttrib4dv(GLuint index, const GLdouble *v)
{
    glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glVertexAttrib4s(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w)
{
    glVertexAttrib4f(index, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

GLAPI void APIENTRY glVertexAttrib4sv(GLuint index, const GLshort *v)
{
    glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glVertexAttrib4Nbv(GLuint index, const GLbyte *v)
{
    glVertexAttrib4f(index, S_B(v[0]), S_B(v[1]), S_B(v[2]), S_B(v[3]));
}

GLAPI void APIENTRY glVertexAttrib4Niv(GLuint index, const GLint *v)
{
    glVertexAttrib4f(index, S_I(v[0]), S_I(v[1]), S_I(v[2]), S_I(v[3]));
}

GLAPI void APIENTRY glVertexAttrib4Nsv(GLuint index, const GLshort *v)
{
    glVertexAttrib4f(index, S_S(v[0]), S_S(v[1]), S_S(v[2]), S_S(v[3]));
}

GLAPI void APIENTRY glVertexAttrib4Nubv(GLuint index, const GLubyte *v)
{
    glVertexAttrib4f(index, S_UB(v[0]), S_UB(v[1]), S_UB(v[2]), S_UB(v[3]));
}

GLAPI void APIENTRY glVertexAttrib4Nuiv(GLuint index, const GLuint *v)
{
    glVertexAttrib4f(index, S_UI(v[0]), S_UI(v[1]), S_UI(v[2]), S_UI(v[3]));
}

GLAPI void APIENTRY glVertexAttrib4Nusv(GLuint index, const GLushort *v)
{
    glVertexAttrib4f(index, S_US(v[0]), S_US(v[1]), S_US(v[2]), S_US(v[3]));
}

GLAPI void APIENTRY glVertexAttrib4bv(GLuint index, const GLbyte *v)
{
    glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glVertexAttrib4iv(GLuint index, const GLint *v)
{
    glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glVertexAttrib4ubv(GLuint index, const GLubyte *v)
{
    glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glVertexAttrib4uiv(GLuint index, const GLuint *v)
{
    glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glVertexAttrib4usv(GLuint index, const GLushort *v)
{
    glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glVertexAttribI1i(GLuint index, GLint x)
{
    glVertexAttribI4i(index, x, 0, 0, 1);
}

GLAPI void APIENTRY glVertexAttribI1iv(GLuint index, const GLint *v)
{
    glVertexAttribI4i(index, v[0], 0, 0, 1);
}

GLAPI void APIENTRY glVertexAttribI1ui(GLuint index, GLuint x)
{
    glVertexAttribI4ui(index, x, 0, 0, 1);
}

GLAPI void APIENTRY glVertexAttribI1uiv(GLuint index, const GLuint *v)
{
    glVertexAttribI4ui(index, v[0], 0, 0, 1);
}

GLAPI void APIENTRY glVertexAttribI2i(GLuint index, GLint x, GLint y)
{
    glVertexAttribI4i(index, x, y, 0, 1);
}

GLAPI void APIENTRY glVertexAttribI2iv(GLuint index, const GLint *v)
{
    glVertexAttribI4i(index, v[0], v[1], 0, 1);
}

GLAPI void APIENTRY glVertexAttribI2ui(GLuint index, GLuint x, GLuint y)
{
    glVertexAttribI4ui(index, x, y, 0, 1);
}

GLAPI void APIENTRY glVertexAttribI2uiv(GLuint index, const GLuint *v)
{
    glVertexAttribI4ui(index, v[0], v[1], 0, 1);
}

GLAPI void APIENTRY glVertexAttribI3i(GLuint index, GLint x, GLint y, GLint z)
{
    glVertexAttribI4i(index, x, y, z, 1);
}

GLAPI void APIENTRY glVertexAttribI3iv(GLuint index, const GLint *v)
{
    glVertexAttribI4i(index, v[0], v[1], v[2], 1);
}

GLAPI void APIENTRY glVertexAttribI3ui(GLuint index, GLuint x, GLuint y, GLuint z)
{
    glVertexAttribI4ui(index, x, y, z, 1);
}

GLAPI void APIENTRY glVertexAttribI3uiv(GLuint index, const GLuint *v)
{
    glVertexAttribI4ui(index, v[0], v[1], v[2], 1);
}

GLAPI void APIENTRY glVertexAttribI4bv(GLuint index, const GLbyte *v)
{
    glVertexAttribI4i(index, v[0], v[1], v[2], v[3]);
}

GLAPI void APIENTRY glVertexAttribI4sv(GLuint index, const GLshort *v)
{
    glVertexAttribI4i(index, v[0], v[1], v[2], v[3]);
}

GLAPI void APIENTRY glVertexAttribI4ubv(GLuint index, const GLubyte *v)
{
    glVertexAttribI4ui(index, v[0], v[1], v[2], v[3]);
}

GLAPI void APIENTRY glVertexAttribI4usv(GLuint index, const GLushort *v)
{
    glVertexAttribI4ui(index, v[0], v[1], v[2], v[3]);
}

/* ---------------------------------------------------------------- packed formats */

/* unpacks the components of the 2_10_10_10 and 10F_11F_11F formats. returns the component count the format has. */
static float small_float(uint32_t bits, int mantissa)
{
    uint32_t exponent = bits >> mantissa, fraction = bits & ((1u << mantissa) - 1);
    if (exponent == 0)
        return (float)fraction / (float)(1u << mantissa) / 16384.0f;
    if (exponent == 31)
        return fraction ? 0.0f / 0.0f : 1.0f / 0.0f;
    float scale = exponent >= 15 ? (float)(1u << (exponent - 15)) : 1.0f / (float)(1u << (15 - exponent));
    return scale * (1.0f + (float)fraction / (float)(1u << mantissa));
}

static BOOL unpack_vertex(GLenum type, GLuint value, BOOL normalized, GLfloat out[4])
{
    out[0] = out[1] = out[2] = 0;
    out[3] = 1;
    if (type == GL_INT_2_10_10_10_REV) {
        for (int i = 0; i < 3; i++) {
            GLint c = (GLint)(value << (22 - 10 * i)) >> 22;
            out[i] = normalized ? (c / 511.0f < -1.0f ? -1.0f : c / 511.0f) : (GLfloat)c;
        }
        GLint a = (GLint)value >> 30;
        out[3] = normalized ? (a < -1 ? -1.0f : (GLfloat)a) : (GLfloat)a;
        return TRUE;
    }
    if (type == GL_UNSIGNED_INT_2_10_10_10_REV) {
        for (int i = 0; i < 3; i++) {
            GLuint c = (value >> (10 * i)) & 1023;
            out[i] = normalized ? c / 1023.0f : (GLfloat)c;
        }
        GLuint a = value >> 30;
        out[3] = normalized ? a / 3.0f : (GLfloat)a;
        return TRUE;
    }
    if (type == GL_UNSIGNED_INT_10F_11F_11F_REV) {
        out[0] = small_float(value & 2047, 6);
        out[1] = small_float((value >> 11) & 2047, 6);
        out[2] = small_float(value >> 22, 5);
        return TRUE;
    }
    return FALSE;
}

static BOOL packed(GlContext *ctx, GLenum type, GLuint value, BOOL normalized, GLfloat out[4], int count)
{
    /* the 10F_11F_11F format has three components, so it does not fit a fourth. */
    if (!unpack_vertex(type, value, normalized, out) || (type == GL_UNSIGNED_INT_10F_11F_11F_REV && count == 4)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return FALSE;
    }
    return TRUE;
}

static void attrib_packed(GLuint index, GLenum type, GLboolean normalized, GLuint value, int count)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4];
    if (packed(ctx, type, value, normalized, v, count))
        glVertexAttrib4f(index, v[0], count > 1 ? v[1] : 0, count > 2 ? v[2] : 0, count > 3 ? v[3] : 1);
}

GLAPI void APIENTRY glVertexAttribP1ui(GLuint index, GLenum type, GLboolean normalized, GLuint value)
{
    attrib_packed(index, type, normalized, value, 1);
}

GLAPI void APIENTRY glVertexAttribP1uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value)
{
    attrib_packed(index, type, normalized, *value, 1);
}

GLAPI void APIENTRY glVertexAttribP2ui(GLuint index, GLenum type, GLboolean normalized, GLuint value)
{
    attrib_packed(index, type, normalized, value, 2);
}

GLAPI void APIENTRY glVertexAttribP2uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value)
{
    attrib_packed(index, type, normalized, *value, 2);
}

GLAPI void APIENTRY glVertexAttribP3ui(GLuint index, GLenum type, GLboolean normalized, GLuint value)
{
    attrib_packed(index, type, normalized, value, 3);
}

GLAPI void APIENTRY glVertexAttribP3uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value)
{
    attrib_packed(index, type, normalized, *value, 3);
}

GLAPI void APIENTRY glVertexAttribP4ui(GLuint index, GLenum type, GLboolean normalized, GLuint value)
{
    attrib_packed(index, type, normalized, value, 4);
}

GLAPI void APIENTRY glVertexAttribP4uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value)
{
    attrib_packed(index, type, normalized, *value, 4);
}

GLAPI void APIENTRY glVertexP2ui(GLenum type, GLuint value)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    GLfloat v[4];
    if (packed(ctx, type, value, FALSE, v, 2))
        glVertex2f(v[0], v[1]);
}

GLAPI void APIENTRY glVertexP2uiv(GLenum type, const GLuint *value)
{
    glVertexP2ui(type, *value);
}

GLAPI void APIENTRY glVertexP3ui(GLenum type, GLuint value)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    GLfloat v[4];
    if (packed(ctx, type, value, FALSE, v, 3))
        glVertex3f(v[0], v[1], v[2]);
}

GLAPI void APIENTRY glVertexP3uiv(GLenum type, const GLuint *value)
{
    glVertexP3ui(type, *value);
}

GLAPI void APIENTRY glVertexP4ui(GLenum type, GLuint value)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    GLfloat v[4];
    if (packed(ctx, type, value, FALSE, v, 4))
        glVertex4f(v[0], v[1], v[2], v[3]);
}

GLAPI void APIENTRY glVertexP4uiv(GLenum type, const GLuint *value)
{
    glVertexP4ui(type, *value);
}

static void texcoord_packed(GLenum unit, GLenum type, GLuint value, int count)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    GLfloat v[4];
    if (packed(ctx, type, value, FALSE, v, count))
        glMultiTexCoord4f(unit, v[0], count > 1 ? v[1] : 0, count > 2 ? v[2] : 0, count > 3 ? v[3] : 1);
}

GLAPI void APIENTRY glTexCoordP1ui(GLenum type, GLuint coords)
{
    texcoord_packed(GL_TEXTURE0, type, coords, 1);
}

GLAPI void APIENTRY glTexCoordP1uiv(GLenum type, const GLuint *coords)
{
    texcoord_packed(GL_TEXTURE0, type, *coords, 1);
}

GLAPI void APIENTRY glMultiTexCoordP1ui(GLenum texture, GLenum type, GLuint coords)
{
    texcoord_packed(texture, type, coords, 1);
}

GLAPI void APIENTRY glMultiTexCoordP1uiv(GLenum texture, GLenum type, const GLuint *coords)
{
    texcoord_packed(texture, type, *coords, 1);
}

GLAPI void APIENTRY glTexCoordP2ui(GLenum type, GLuint coords)
{
    texcoord_packed(GL_TEXTURE0, type, coords, 2);
}

GLAPI void APIENTRY glTexCoordP2uiv(GLenum type, const GLuint *coords)
{
    texcoord_packed(GL_TEXTURE0, type, *coords, 2);
}

GLAPI void APIENTRY glMultiTexCoordP2ui(GLenum texture, GLenum type, GLuint coords)
{
    texcoord_packed(texture, type, coords, 2);
}

GLAPI void APIENTRY glMultiTexCoordP2uiv(GLenum texture, GLenum type, const GLuint *coords)
{
    texcoord_packed(texture, type, *coords, 2);
}

GLAPI void APIENTRY glTexCoordP3ui(GLenum type, GLuint coords)
{
    texcoord_packed(GL_TEXTURE0, type, coords, 3);
}

GLAPI void APIENTRY glTexCoordP3uiv(GLenum type, const GLuint *coords)
{
    texcoord_packed(GL_TEXTURE0, type, *coords, 3);
}

GLAPI void APIENTRY glMultiTexCoordP3ui(GLenum texture, GLenum type, GLuint coords)
{
    texcoord_packed(texture, type, coords, 3);
}

GLAPI void APIENTRY glMultiTexCoordP3uiv(GLenum texture, GLenum type, const GLuint *coords)
{
    texcoord_packed(texture, type, *coords, 3);
}

GLAPI void APIENTRY glTexCoordP4ui(GLenum type, GLuint coords)
{
    texcoord_packed(GL_TEXTURE0, type, coords, 4);
}

GLAPI void APIENTRY glTexCoordP4uiv(GLenum type, const GLuint *coords)
{
    texcoord_packed(GL_TEXTURE0, type, *coords, 4);
}

GLAPI void APIENTRY glMultiTexCoordP4ui(GLenum texture, GLenum type, GLuint coords)
{
    texcoord_packed(texture, type, coords, 4);
}

GLAPI void APIENTRY glMultiTexCoordP4uiv(GLenum texture, GLenum type, const GLuint *coords)
{
    texcoord_packed(texture, type, *coords, 4);
}

GLAPI void APIENTRY glNormalP3ui(GLenum type, GLuint coords)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    GLfloat v[4];
    if (type != GL_UNSIGNED_INT_10F_11F_11F_REV && packed(ctx, type, coords, TRUE, v, 3))
        glNormal3f(v[0], v[1], v[2]);
    else if (type == GL_UNSIGNED_INT_10F_11F_11F_REV)
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glNormalP3uiv(GLenum type, const GLuint *coords)
{
    glNormalP3ui(type, *coords);
}

GLAPI void APIENTRY glColorP3ui(GLenum type, GLuint color)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    GLfloat v[4];
    if (type != GL_UNSIGNED_INT_10F_11F_11F_REV && packed(ctx, type, color, TRUE, v, 3))
        glColor3f(v[0], v[1], v[2]);
    else if (type == GL_UNSIGNED_INT_10F_11F_11F_REV)
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glColorP3uiv(GLenum type, const GLuint *color)
{
    glColorP3ui(type, *color);
}

GLAPI void APIENTRY glColorP4ui(GLenum type, GLuint color)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    GLfloat v[4];
    if (type != GL_UNSIGNED_INT_10F_11F_11F_REV && packed(ctx, type, color, TRUE, v, 4))
        glColor4f(v[0], v[1], v[2], v[3]);
    else if (type == GL_UNSIGNED_INT_10F_11F_11F_REV)
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glColorP4uiv(GLenum type, const GLuint *color)
{
    glColorP4ui(type, *color);
}

GLAPI void APIENTRY glSecondaryColorP3ui(GLenum type, GLuint color)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    GLfloat v[4];
    if (type != GL_UNSIGNED_INT_10F_11F_11F_REV && packed(ctx, type, color, TRUE, v, 3))
        glSecondaryColor3f(v[0], v[1], v[2]);
    else if (type == GL_UNSIGNED_INT_10F_11F_11F_REV)
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glSecondaryColorP3uiv(GLenum type, const GLuint *color)
{
    glSecondaryColorP3ui(type, *color);
}

/* ---------------------------------------------------------------- client arrays the pipeline ignores */

static void legacy_pointer(GLint size, GLenum type, GLsizei stride, GLint min_size, GLint max_size)
{
    GL_CONTEXT_OR_RETURN();
    if (size < min_size || size > max_size || stride < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (type != GL_BYTE && type != GL_UNSIGNED_BYTE && type != GL_SHORT && type != GL_UNSIGNED_SHORT && type != GL_INT &&
        type != GL_UNSIGNED_INT && type != GL_FLOAT && type != GL_DOUBLE && type != GL_HALF_FLOAT)
        gl_error(ctx, GL_INVALID_ENUM);
}

void APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                                    const void *pointer);

/* both arrays are the generic attributes the built-in inputs alias, 4 and 5, read when a program uses those inputs. */
GLAPI void APIENTRY glSecondaryColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    legacy_pointer(size, type, stride, 3, 3);
    glVertexAttribPointer(4, size, type, type != GL_FLOAT && type != GL_DOUBLE && type != GL_HALF_FLOAT, stride, pointer);
}

GLAPI void APIENTRY glFogCoordPointer(GLenum type, GLsizei stride, const void *pointer)
{
    legacy_pointer(1, type, stride, 1, 1);
    glVertexAttribPointer(5, 1, type, GL_FALSE, stride, pointer);
}

/* ---------------------------------------------------------------- state kept for queries */

GLAPI void APIENTRY glProvokingVertex(GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    if (mode != GL_FIRST_VERTEX_CONVENTION && mode != GL_LAST_VERTEX_CONVENTION) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->provoking_vertex = mode;
}

GLAPI void APIENTRY glClampColor(GLenum target, GLenum clamp)
{
    GL_CONTEXT_OR_RETURN();
    if (target != GL_CLAMP_READ_COLOR && target != GL_CLAMP_VERTEX_COLOR && target != GL_CLAMP_FRAGMENT_COLOR) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (clamp != GL_TRUE && clamp != GL_FALSE && (clamp != GL_FIXED_ONLY || target == GL_CLAMP_VERTEX_COLOR)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->clamp[target - GL_CLAMP_VERTEX_COLOR] = clamp;
}

GLAPI void APIENTRY glSampleCoverage(GLfloat value, GLboolean invert)
{
    GL_CONTEXT_OR_RETURN();
    ctx->sample_coverage = value < 0 ? 0 : value > 1 ? 1 : value;
    ctx->sample_coverage_invert = invert;
}

GLAPI void APIENTRY glSampleMaski(GLuint index, GLbitfield mask)
{
    GL_CONTEXT_OR_RETURN();
    if (index >= 1) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->sample_mask = mask;
}

GLAPI void APIENTRY glMinSampleShading(GLfloat value)
{
    GL_CONTEXT_OR_RETURN();
    ctx->min_sample_shading = value < 0 ? 0 : value > 1 ? 1 : value;
}

GLAPI void APIENTRY glPrimitiveRestartIndex(GLuint index)
{
    GL_CONTEXT_OR_RETURN();
    ctx->restart_index = index;
}

GLAPI void APIENTRY glGetMultisamplefv(GLenum pname, GLuint index, GLfloat *val)
{
    GL_CONTEXT_OR_RETURN();
    static const GLfloat four[4][2] = {{0.375f, 0.125f}, {0.875f, 0.375f}, {0.125f, 0.625f}, {0.625f, 0.875f}};
    if (pname != GL_SAMPLE_POSITION) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (index >= 4) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    val[0] = four[index][0];
    val[1] = four[index][1];
}

void APIENTRY glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params);

GLAPI void APIENTRY glGetVertexAttribdv(GLuint index, GLenum pname, GLdouble *params)
{
    GLfloat v[4] = {};
    glGetVertexAttribfv(index, pname, v);
    GL_CONTEXT_OR_RETURN();
    int n = pname == GL_CURRENT_VERTEX_ATTRIB ? 4 : 1;
    for (int i = 0; i < n; i++)
        params[i] = v[i];
}
