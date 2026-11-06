/*
 * robustness: the reads that know how much memory they may write, and the reset status. nothing here can lose the
 * context, so the status is always GL_NO_ERROR whatever the reset notification strategy.
 */
#define GL_GLEXT_PROTOTYPES
#include "opengl32.h"

GLAPI GLenum APIENTRY glGetGraphicsResetStatus(void)
{
    GL_CONTEXT_OR_RETURN(GL_NO_ERROR);
    return GL_NO_ERROR;
}

GLAPI GLenum APIENTRY glGetGraphicsResetStatusARB(void)
{
    return glGetGraphicsResetStatus();
}

GLAPI void APIENTRY glReadnPixelsARB(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLsizei size,
                                     void *pixels)
{
    glReadnPixels(x, y, w, h, format, type, size, pixels);
}

GLAPI void APIENTRY glGetnTexImageARB(GLenum target, GLint level, GLenum format, GLenum type, GLsizei size, void *pixels)
{
    glGetnTexImage(target, level, format, type, size, pixels);
}

GLAPI void APIENTRY glGetnCompressedTexImageARB(GLenum target, GLint level, GLsizei size, void *pixels)
{
    glGetnCompressedTexImage(target, level, size, pixels);
}

/* the values a uniform of the type holds: a matrix counts its columns times its rows. */
static int uniform_values(GLenum type)
{
    switch (type) {
    case GL_FLOAT: case GL_INT: case GL_UNSIGNED_INT: case GL_BOOL: case GL_DOUBLE:
        return 1;
    case GL_FLOAT_VEC2: case GL_INT_VEC2: case GL_UNSIGNED_INT_VEC2: case GL_BOOL_VEC2: case GL_DOUBLE_VEC2:
        return 2;
    case GL_FLOAT_VEC3: case GL_INT_VEC3: case GL_UNSIGNED_INT_VEC3: case GL_BOOL_VEC3: case GL_DOUBLE_VEC3:
        return 3;
    case GL_FLOAT_VEC4: case GL_INT_VEC4: case GL_UNSIGNED_INT_VEC4: case GL_BOOL_VEC4: case GL_DOUBLE_VEC4:
    case GL_FLOAT_MAT2: case GL_DOUBLE_MAT2:
        return 4;
    case GL_FLOAT_MAT2x3: case GL_FLOAT_MAT3x2: case GL_DOUBLE_MAT2x3: case GL_DOUBLE_MAT3x2:
        return 6;
    case GL_FLOAT_MAT2x4: case GL_FLOAT_MAT4x2: case GL_DOUBLE_MAT2x4: case GL_DOUBLE_MAT4x2:
        return 8;
    case GL_FLOAT_MAT3: case GL_DOUBLE_MAT3:
        return 9;
    case GL_FLOAT_MAT3x4: case GL_FLOAT_MAT4x3: case GL_DOUBLE_MAT3x4: case GL_DOUBLE_MAT4x3:
        return 12;
    case GL_FLOAT_MAT4: case GL_DOUBLE_MAT4:
        return 16;
    default:
        return 1;
    }
}

/* the values at a location, 0 after raising the error when the program or the location is wrong. */
static int values_at(GlContext *ctx, GLuint program, GLint location)
{
    GlProgram *p = gl_program(ctx, program);
    if (!p || !gl_program_ready(p) || !p->link_status) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return 0;
    }
    GLint index = location / 4096;
    if (location < 0 || (uint32_t)index >= p->uniform_count) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return 0;
    }
    return uniform_values(p->uniforms[index].type);
}

GLAPI void APIENTRY glGetnUniformfv(GLuint program, GLint location, GLsizei size, GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    int n = values_at(ctx, program, location);
    if (!n)
        return;
    if (size < 0 || (uint64_t)size < (uint64_t)n * sizeof(GLfloat)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    glGetUniformfv(program, location, params);
}

GLAPI void APIENTRY glGetnUniformiv(GLuint program, GLint location, GLsizei size, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    int n = values_at(ctx, program, location);
    if (!n)
        return;
    if (size < 0 || (uint64_t)size < (uint64_t)n * sizeof(GLint)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    glGetUniformiv(program, location, params);
}

GLAPI void APIENTRY glGetnUniformuiv(GLuint program, GLint location, GLsizei size, GLuint *params)
{
    GL_CONTEXT_OR_RETURN();
    int n = values_at(ctx, program, location);
    if (!n)
        return;
    if (size < 0 || (uint64_t)size < (uint64_t)n * sizeof(GLuint)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    glGetUniformiv(program, location, (GLint *)params);
}

GLAPI void APIENTRY glGetnUniformdv(GLuint program, GLint location, GLsizei size, GLdouble *params)
{
    GL_CONTEXT_OR_RETURN();
    int n = values_at(ctx, program, location);
    if (!n)
        return;
    if (size < 0 || (uint64_t)size < (uint64_t)n * sizeof(GLdouble)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GLfloat v[16];
    glGetUniformfv(program, location, v);
    for (int i = 0; i < n; i++)
        params[i] = v[i];
}

GLAPI void APIENTRY glGetnUniformfvARB(GLuint program, GLint location, GLsizei size, GLfloat *params)
{
    glGetnUniformfv(program, location, size, params);
}

GLAPI void APIENTRY glGetnUniformivARB(GLuint program, GLint location, GLsizei size, GLint *params)
{
    glGetnUniformiv(program, location, size, params);
}

GLAPI void APIENTRY glGetnUniformuivARB(GLuint program, GLint location, GLsizei size, GLuint *params)
{
    glGetnUniformuiv(program, location, size, params);
}

GLAPI void APIENTRY glGetnUniformdvARB(GLuint program, GLint location, GLsizei size, GLdouble *params)
{
    glGetnUniformdv(program, location, size, params);
}

static BOOL room(GlContext *ctx, GLsizei size, SIZE_T element)
{
    if (size < 0 || (SIZE_T)size < element) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return FALSE;
    }
    return TRUE;
}

GLAPI void APIENTRY glGetnMapdv(GLenum target, GLenum query, GLsizei size, GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    if (room(ctx, size, sizeof(GLdouble)))
        glGetMapdv(target, query, v);
}

GLAPI void APIENTRY glGetnMapfv(GLenum target, GLenum query, GLsizei size, GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    if (room(ctx, size, sizeof(GLfloat)))
        glGetMapfv(target, query, v);
}

GLAPI void APIENTRY glGetnMapiv(GLenum target, GLenum query, GLsizei size, GLint *v)
{
    GL_CONTEXT_OR_RETURN();
    if (room(ctx, size, sizeof(GLint)))
        glGetMapiv(target, query, v);
}

GLAPI void APIENTRY glGetnPixelMapfv(GLenum map, GLsizei size, GLfloat *values)
{
    GL_CONTEXT_OR_RETURN();
    if (room(ctx, size, sizeof(GLfloat)))
        glGetPixelMapfv(map, values);
}

GLAPI void APIENTRY glGetnPixelMapuiv(GLenum map, GLsizei size, GLuint *values)
{
    GL_CONTEXT_OR_RETURN();
    if (room(ctx, size, sizeof(GLuint)))
        glGetPixelMapuiv(map, values);
}

GLAPI void APIENTRY glGetnPixelMapusv(GLenum map, GLsizei size, GLushort *values)
{
    GL_CONTEXT_OR_RETURN();
    if (room(ctx, size, sizeof(GLushort)))
        glGetPixelMapusv(map, values);
}

GLAPI void APIENTRY glGetnPolygonStipple(GLsizei size, GLubyte *pattern)
{
    GL_CONTEXT_OR_RETURN();
    if (room(ctx, size, 128))
        glGetPolygonStipple(pattern);
}

GLAPI void APIENTRY glGetnMapdvARB(GLenum target, GLenum query, GLsizei size, GLdouble *v)
{
    glGetnMapdv(target, query, size, v);
}

GLAPI void APIENTRY glGetnMapfvARB(GLenum target, GLenum query, GLsizei size, GLfloat *v)
{
    glGetnMapfv(target, query, size, v);
}

GLAPI void APIENTRY glGetnMapivARB(GLenum target, GLenum query, GLsizei size, GLint *v)
{
    glGetnMapiv(target, query, size, v);
}

GLAPI void APIENTRY glGetnPixelMapfvARB(GLenum map, GLsizei size, GLfloat *values)
{
    glGetnPixelMapfv(map, size, values);
}

GLAPI void APIENTRY glGetnPixelMapuivARB(GLenum map, GLsizei size, GLuint *values)
{
    glGetnPixelMapuiv(map, size, values);
}

GLAPI void APIENTRY glGetnPixelMapusvARB(GLenum map, GLsizei size, GLushort *values)
{
    glGetnPixelMapusv(map, size, values);
}

GLAPI void APIENTRY glGetnPolygonStippleARB(GLsizei size, GLubyte *pattern)
{
    glGetnPolygonStipple(size, pattern);
}

/* the imaging subset is not part of this context, its targets are tokens the commands do not know. */
GLAPI void APIENTRY glGetnColorTable(GLenum target, GLenum format, GLenum type, GLsizei size, void *table)
{
    GL_CONTEXT_OR_RETURN();
    gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glGetnColorTableARB(GLenum target, GLenum format, GLenum type, GLsizei size, void *table)
{
    glGetnColorTable(target, format, type, size, table);
}

GLAPI void APIENTRY glGetnConvolutionFilter(GLenum target, GLenum format, GLenum type, GLsizei size, void *image)
{
    GL_CONTEXT_OR_RETURN();
    gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glGetnConvolutionFilterARB(GLenum target, GLenum format, GLenum type, GLsizei size, void *image)
{
    glGetnConvolutionFilter(target, format, type, size, image);
}

GLAPI void APIENTRY glGetnSeparableFilter(GLenum target, GLenum format, GLenum type, GLsizei row_size, void *row,
                                          GLsizei column_size, void *column, void *span)
{
    GL_CONTEXT_OR_RETURN();
    gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glGetnSeparableFilterARB(GLenum target, GLenum format, GLenum type, GLsizei row_size, void *row,
                                             GLsizei column_size, void *column, void *span)
{
    glGetnSeparableFilter(target, format, type, row_size, row, column_size, column, span);
}

GLAPI void APIENTRY glGetnHistogram(GLenum target, GLboolean reset, GLenum format, GLenum type, GLsizei size, void *values)
{
    GL_CONTEXT_OR_RETURN();
    gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glGetnHistogramARB(GLenum target, GLboolean reset, GLenum format, GLenum type, GLsizei size, void *values)
{
    glGetnHistogram(target, reset, format, type, size, values);
}

GLAPI void APIENTRY glGetnMinmax(GLenum target, GLboolean reset, GLenum format, GLenum type, GLsizei size, void *values)
{
    GL_CONTEXT_OR_RETURN();
    gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glGetnMinmaxARB(GLenum target, GLboolean reset, GLenum format, GLenum type, GLsizei size, void *values)
{
    glGetnMinmax(target, reset, format, type, size, values);
}
