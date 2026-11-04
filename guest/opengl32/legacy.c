/*
 * legacy commands accepted so programs link and start, which draw nothing: display lists, evaluators, the raster
 * position and pixel drawing, color indexes, texture coordinate generation, clip planes, stipples, the logic operation,
 * pixel transfer and the accumulation buffer. what the specification makes an error is still one.
 */
#include "opengl32.h"

/* ---------------------------------------------------------------- display lists */

GLAPI GLuint APIENTRY glGenLists(GLsizei range)
{
    GL_CONTEXT_OR_RETURN(0);
    if (range < 0)
        gl_error(ctx, GL_INVALID_VALUE);
    return 0;
}

GLAPI void APIENTRY glNewList(GLuint list, GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    if (list == 0 || (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE)) {
        gl_error(ctx, list == 0 ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }
    /* nothing records commands, so a list cannot be made. */
    gl_error(ctx, GL_OUT_OF_MEMORY);
}

GLAPI void APIENTRY glEndList(void)
{
    GL_CONTEXT_OR_RETURN();
    gl_error(ctx, GL_INVALID_OPERATION);
}

GLAPI GLboolean APIENTRY glIsList(GLuint list)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    return GL_FALSE;
}

GLAPI void APIENTRY glDeleteLists(GLuint list, GLsizei range)
{
    GL_CONTEXT_OR_RETURN();
    if (range < 0)
        gl_error(ctx, GL_INVALID_VALUE);
}

GLAPI void APIENTRY glCallList(GLuint list)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glCallLists(GLsizei n, GLenum type, const GLvoid *lists)
{
    GL_CONTEXT_OR_RETURN();
    if (n < 0)
        gl_error(ctx, GL_INVALID_VALUE);
    else if (type < GL_BYTE || type > GL_4_BYTES)
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glListBase(GLuint base)
{
    GL_CONTEXT_OR_RETURN();
}

/* ---------------------------------------------------------------- raster position and pixels */

GLAPI void APIENTRY glPixelTransferf(GLenum pname, GLfloat param)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glPixelTransferi(GLenum pname, GLint param)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat *values)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint *values)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort *values)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glGetPixelMapfv(GLenum map, GLfloat *values)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glGetPixelMapuiv(GLenum map, GLuint *values)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glGetPixelMapusv(GLenum map, GLushort *values)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glAccum(GLenum op, GLfloat value)
{
    GL_CONTEXT_OR_RETURN();
    /* there is no accumulation buffer, so the operation is an error. */
    gl_error(ctx, GL_INVALID_OPERATION);
}

GLAPI void APIENTRY glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    GL_CONTEXT_OR_RETURN();
}

/* ---------------------------------------------------------------- color indexes */

GLAPI void APIENTRY glClearIndex(GLfloat c)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glIndexMask(GLuint mask)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glIndexd(GLdouble c)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glIndexdv(const GLdouble *c)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glIndexf(GLfloat c)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glIndexfv(const GLfloat *c)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glIndexi(GLint c)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glIndexiv(const GLint *c)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glIndexs(GLshort c)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glIndexsv(const GLshort *c)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glIndexub(GLubyte c)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glIndexubv(const GLubyte *c)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

/* ---------------------------------------------------------------- generation, planes, stipples */

GLAPI void APIENTRY glTexGend(GLenum coord, GLenum pname, GLdouble param)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glTexGenf(GLenum coord, GLenum pname, GLfloat param)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glTexGeni(GLenum coord, GLenum pname, GLint param)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glTexGendv(GLenum coord, GLenum pname, const GLdouble *params)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glTexGeniv(GLenum coord, GLenum pname, const GLint *params)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glGetTexGendv(GLenum coord, GLenum pname, GLdouble *params)
{
    GL_CONTEXT_OR_RETURN();
    if (coord < GL_S || coord > GL_Q || (pname != GL_TEXTURE_GEN_MODE && pname != GL_OBJECT_PLANE && pname != GL_EYE_PLANE)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_TEXTURE_GEN_MODE) {
        params[0] = (GLdouble)GL_EYE_LINEAR;
        return;
    }
    for (int i = 0; i < 4; i++)
        params[i] = (GLdouble)(i == (coord == GL_S ? 0 : coord == GL_T ? 1 : 4) ? 1 : 0);
}

GLAPI void APIENTRY glGetTexGenfv(GLenum coord, GLenum pname, GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    if (coord < GL_S || coord > GL_Q || (pname != GL_TEXTURE_GEN_MODE && pname != GL_OBJECT_PLANE && pname != GL_EYE_PLANE)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_TEXTURE_GEN_MODE) {
        params[0] = (GLfloat)GL_EYE_LINEAR;
        return;
    }
    for (int i = 0; i < 4; i++)
        params[i] = (GLfloat)(i == (coord == GL_S ? 0 : coord == GL_T ? 1 : 4) ? 1 : 0);
}

GLAPI void APIENTRY glGetTexGeniv(GLenum coord, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    if (coord < GL_S || coord > GL_Q || (pname != GL_TEXTURE_GEN_MODE && pname != GL_OBJECT_PLANE && pname != GL_EYE_PLANE)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_TEXTURE_GEN_MODE) {
        params[0] = (GLint)GL_EYE_LINEAR;
        return;
    }
    for (int i = 0; i < 4; i++)
        params[i] = (GLint)(i == (coord == GL_S ? 0 : coord == GL_T ? 1 : 4) ? 1 : 0);
}

GLAPI void APIENTRY glClipPlane(GLenum plane, const GLdouble *equation)
{
    GL_CONTEXT_OR_RETURN();
    if (plane < GL_CLIP_PLANE0 || plane > GL_CLIP_PLANE5)
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glGetClipPlane(GLenum plane, GLdouble *equation)
{
    GL_CONTEXT_OR_RETURN();
    if (plane < GL_CLIP_PLANE0 || plane > GL_CLIP_PLANE5) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    for (int i = 0; i < 4; i++)
        equation[i] = 0;
}

GLAPI void APIENTRY glPolygonStipple(const GLubyte *mask)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glGetPolygonStipple(GLubyte *mask)
{
    GL_CONTEXT_OR_RETURN();
    for (int i = 0; i < 128; i++)
        mask[i] = 0xFF;
}

GLAPI void APIENTRY glLogicOp(GLenum opcode)
{
    GL_CONTEXT_OR_RETURN();
    if (opcode < GL_CLEAR || opcode > GL_SET)
        gl_error(ctx, GL_INVALID_ENUM);
}

/* ---------------------------------------------------------------- evaluators */

GLAPI void APIENTRY glEvalCoord1d(GLdouble u)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glEvalCoord1dv(const GLdouble *u)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glEvalCoord2d(GLdouble u, GLdouble v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glEvalCoord2dv(const GLdouble *u)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glMapGrid1d(GLint un, GLdouble u1, GLdouble u2)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glMapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glMap1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glMap2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glEvalCoord1f(GLfloat u)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glEvalCoord1fv(const GLfloat *u)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glEvalCoord2f(GLfloat u, GLfloat v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glEvalCoord2fv(const GLfloat *u)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glMapGrid1f(GLint un, GLfloat u1, GLfloat u2)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glMap1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glMap2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glEvalMesh1(GLenum mode, GLint i1, GLint i2)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glEvalPoint1(GLint i)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glEvalPoint2(GLint i, GLint j)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
}

GLAPI void APIENTRY glGetMapdv(GLenum target, GLenum query, GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glGetMapfv(GLenum target, GLenum query, GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glGetMapiv(GLenum target, GLenum query, GLint *v)
{
    GL_CONTEXT_OR_RETURN();
}

/* ---------------------------------------------------------------- textures */

GLAPI GLboolean APIENTRY glAreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return GL_FALSE;
    }
    for (GLsizei i = 0; i < n; i++) {
        if (!textures[i] || !glIsTexture(textures[i])) {
            gl_error(ctx, GL_INVALID_VALUE);
            return GL_FALSE;
        }
        residences[i] = GL_TRUE;
    }
    return GL_TRUE;
}

GLAPI void APIENTRY glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
    GL_CONTEXT_OR_RETURN();
    if (n < 0)
        gl_error(ctx, GL_INVALID_VALUE);
}

GLAPI void APIENTRY glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border)
{
    GL_CONTEXT_OR_RETURN();
}

GLAPI void APIENTRY glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width)
{
    GL_CONTEXT_OR_RETURN();
}
