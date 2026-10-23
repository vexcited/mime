/*
 * the immediate mode vertex commands: positions, colors, normals and texture coordinates in every type GL has, each
 * with its vector form. integer colors and normals are scaled to the range of their type. the work is in immediate.h.
 */
#include "immediate.h"

/* ---------------------------------------------------------------- vertices */

GLAPI void APIENTRY glVertex2d(GLdouble x, GLdouble y)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)x, (GLfloat)y, 0, 1);
}

GLAPI void APIENTRY glVertex2dv(const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)v[0], (GLfloat)v[1], 0, 1);
}

GLAPI void APIENTRY glVertex2f(GLfloat x, GLfloat y)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)x, (GLfloat)y, 0, 1);
}

GLAPI void APIENTRY glVertex2fv(const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)v[0], (GLfloat)v[1], 0, 1);
}

GLAPI void APIENTRY glVertex2i(GLint x, GLint y)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)x, (GLfloat)y, 0, 1);
}

GLAPI void APIENTRY glVertex2iv(const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)v[0], (GLfloat)v[1], 0, 1);
}

GLAPI void APIENTRY glVertex2s(GLshort x, GLshort y)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)x, (GLfloat)y, 0, 1);
}

GLAPI void APIENTRY glVertex2sv(const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)v[0], (GLfloat)v[1], 0, 1);
}

GLAPI void APIENTRY glVertex3d(GLdouble x, GLdouble y, GLdouble z)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)x, (GLfloat)y, (GLfloat)z, 1);
}

GLAPI void APIENTRY glVertex3dv(const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);
}

GLAPI void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)x, (GLfloat)y, (GLfloat)z, 1);
}

GLAPI void APIENTRY glVertex3fv(const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);
}

GLAPI void APIENTRY glVertex3i(GLint x, GLint y, GLint z)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)x, (GLfloat)y, (GLfloat)z, 1);
}

GLAPI void APIENTRY glVertex3iv(const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);
}

GLAPI void APIENTRY glVertex3s(GLshort x, GLshort y, GLshort z)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)x, (GLfloat)y, (GLfloat)z, 1);
}

GLAPI void APIENTRY glVertex3sv(const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);
}

GLAPI void APIENTRY glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

GLAPI void APIENTRY glVertex4dv(const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

GLAPI void APIENTRY glVertex4fv(const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glVertex4i(GLint x, GLint y, GLint z, GLint w)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

GLAPI void APIENTRY glVertex4iv(const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

GLAPI void APIENTRY glVertex4sv(const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_vertex(ctx, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

/* ---------------------------------------------------------------- colors */

GLAPI void APIENTRY glColor3b(GLbyte red, GLbyte green, GLbyte blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((2.0 * (red) + 1.0) / 255.0), (GLfloat)((2.0 * (green) + 1.0) / 255.0), (GLfloat)((2.0 * (blue) + 1.0) / 255.0), 1.0f);
}

GLAPI void APIENTRY glColor3bv(const GLbyte *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((2.0 * (v[0]) + 1.0) / 255.0), (GLfloat)((2.0 * (v[1]) + 1.0) / 255.0), (GLfloat)((2.0 * (v[2]) + 1.0) / 255.0), 1.0f);
}

GLAPI void APIENTRY glColor3d(GLdouble red, GLdouble green, GLdouble blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)(red), (GLfloat)(green), (GLfloat)(blue), 1.0f);
}

GLAPI void APIENTRY glColor3dv(const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)(v[0]), (GLfloat)(v[1]), (GLfloat)(v[2]), 1.0f);
}

GLAPI void APIENTRY glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)(red), (GLfloat)(green), (GLfloat)(blue), 1.0f);
}

GLAPI void APIENTRY glColor3fv(const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)(v[0]), (GLfloat)(v[1]), (GLfloat)(v[2]), 1.0f);
}

GLAPI void APIENTRY glColor3i(GLint red, GLint green, GLint blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((2.0 * (red) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (green) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (blue) + 1.0) / 4294967295.0), 1.0f);
}

GLAPI void APIENTRY glColor3iv(const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((2.0 * (v[0]) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (v[1]) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (v[2]) + 1.0) / 4294967295.0), 1.0f);
}

GLAPI void APIENTRY glColor3s(GLshort red, GLshort green, GLshort blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((2.0 * (red) + 1.0) / 65535.0), (GLfloat)((2.0 * (green) + 1.0) / 65535.0), (GLfloat)((2.0 * (blue) + 1.0) / 65535.0), 1.0f);
}

GLAPI void APIENTRY glColor3sv(const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((2.0 * (v[0]) + 1.0) / 65535.0), (GLfloat)((2.0 * (v[1]) + 1.0) / 65535.0), (GLfloat)((2.0 * (v[2]) + 1.0) / 65535.0), 1.0f);
}

GLAPI void APIENTRY glColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((red) / 255.0), (GLfloat)((green) / 255.0), (GLfloat)((blue) / 255.0), 1.0f);
}

GLAPI void APIENTRY glColor3ubv(const GLubyte *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((v[0]) / 255.0), (GLfloat)((v[1]) / 255.0), (GLfloat)((v[2]) / 255.0), 1.0f);
}

GLAPI void APIENTRY glColor3ui(GLuint red, GLuint green, GLuint blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((red) / 4294967295.0), (GLfloat)((green) / 4294967295.0), (GLfloat)((blue) / 4294967295.0), 1.0f);
}

GLAPI void APIENTRY glColor3uiv(const GLuint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((v[0]) / 4294967295.0), (GLfloat)((v[1]) / 4294967295.0), (GLfloat)((v[2]) / 4294967295.0), 1.0f);
}

GLAPI void APIENTRY glColor3us(GLushort red, GLushort green, GLushort blue)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((red) / 65535.0), (GLfloat)((green) / 65535.0), (GLfloat)((blue) / 65535.0), 1.0f);
}

GLAPI void APIENTRY glColor3usv(const GLushort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((v[0]) / 65535.0), (GLfloat)((v[1]) / 65535.0), (GLfloat)((v[2]) / 65535.0), 1.0f);
}

GLAPI void APIENTRY glColor4b(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((2.0 * (red) + 1.0) / 255.0), (GLfloat)((2.0 * (green) + 1.0) / 255.0), (GLfloat)((2.0 * (blue) + 1.0) / 255.0), (GLfloat)((2.0 * (alpha) + 1.0) / 255.0));
}

GLAPI void APIENTRY glColor4bv(const GLbyte *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((2.0 * (v[0]) + 1.0) / 255.0), (GLfloat)((2.0 * (v[1]) + 1.0) / 255.0), (GLfloat)((2.0 * (v[2]) + 1.0) / 255.0), (GLfloat)((2.0 * (v[3]) + 1.0) / 255.0));
}

GLAPI void APIENTRY glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)(red), (GLfloat)(green), (GLfloat)(blue), (GLfloat)(alpha));
}

GLAPI void APIENTRY glColor4dv(const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)(v[0]), (GLfloat)(v[1]), (GLfloat)(v[2]), (GLfloat)(v[3]));
}

GLAPI void APIENTRY glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)(red), (GLfloat)(green), (GLfloat)(blue), (GLfloat)(alpha));
}

GLAPI void APIENTRY glColor4fv(const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)(v[0]), (GLfloat)(v[1]), (GLfloat)(v[2]), (GLfloat)(v[3]));
}

GLAPI void APIENTRY glColor4i(GLint red, GLint green, GLint blue, GLint alpha)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((2.0 * (red) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (green) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (blue) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (alpha) + 1.0) / 4294967295.0));
}

GLAPI void APIENTRY glColor4iv(const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((2.0 * (v[0]) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (v[1]) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (v[2]) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (v[3]) + 1.0) / 4294967295.0));
}

GLAPI void APIENTRY glColor4s(GLshort red, GLshort green, GLshort blue, GLshort alpha)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((2.0 * (red) + 1.0) / 65535.0), (GLfloat)((2.0 * (green) + 1.0) / 65535.0), (GLfloat)((2.0 * (blue) + 1.0) / 65535.0), (GLfloat)((2.0 * (alpha) + 1.0) / 65535.0));
}

GLAPI void APIENTRY glColor4sv(const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((2.0 * (v[0]) + 1.0) / 65535.0), (GLfloat)((2.0 * (v[1]) + 1.0) / 65535.0), (GLfloat)((2.0 * (v[2]) + 1.0) / 65535.0), (GLfloat)((2.0 * (v[3]) + 1.0) / 65535.0));
}

GLAPI void APIENTRY glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((red) / 255.0), (GLfloat)((green) / 255.0), (GLfloat)((blue) / 255.0), (GLfloat)((alpha) / 255.0));
}

GLAPI void APIENTRY glColor4ubv(const GLubyte *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((v[0]) / 255.0), (GLfloat)((v[1]) / 255.0), (GLfloat)((v[2]) / 255.0), (GLfloat)((v[3]) / 255.0));
}

GLAPI void APIENTRY glColor4ui(GLuint red, GLuint green, GLuint blue, GLuint alpha)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((red) / 4294967295.0), (GLfloat)((green) / 4294967295.0), (GLfloat)((blue) / 4294967295.0), (GLfloat)((alpha) / 4294967295.0));
}

GLAPI void APIENTRY glColor4uiv(const GLuint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((v[0]) / 4294967295.0), (GLfloat)((v[1]) / 4294967295.0), (GLfloat)((v[2]) / 4294967295.0), (GLfloat)((v[3]) / 4294967295.0));
}

GLAPI void APIENTRY glColor4us(GLushort red, GLushort green, GLushort blue, GLushort alpha)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((red) / 65535.0), (GLfloat)((green) / 65535.0), (GLfloat)((blue) / 65535.0), (GLfloat)((alpha) / 65535.0));
}

GLAPI void APIENTRY glColor4usv(const GLushort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_color(ctx, (GLfloat)((v[0]) / 65535.0), (GLfloat)((v[1]) / 65535.0), (GLfloat)((v[2]) / 65535.0), (GLfloat)((v[3]) / 65535.0));
}

/* ---------------------------------------------------------------- normals */

GLAPI void APIENTRY glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_normal(ctx, (GLfloat)((2.0 * (nx) + 1.0) / 255.0), (GLfloat)((2.0 * (ny) + 1.0) / 255.0), (GLfloat)((2.0 * (nz) + 1.0) / 255.0));
}

GLAPI void APIENTRY glNormal3bv(const GLbyte *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_normal(ctx, (GLfloat)((2.0 * (v[0]) + 1.0) / 255.0), (GLfloat)((2.0 * (v[1]) + 1.0) / 255.0), (GLfloat)((2.0 * (v[2]) + 1.0) / 255.0));
}

GLAPI void APIENTRY glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_normal(ctx, (GLfloat)(nx), (GLfloat)(ny), (GLfloat)(nz));
}

GLAPI void APIENTRY glNormal3dv(const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_normal(ctx, (GLfloat)(v[0]), (GLfloat)(v[1]), (GLfloat)(v[2]));
}

GLAPI void APIENTRY glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_normal(ctx, (GLfloat)(nx), (GLfloat)(ny), (GLfloat)(nz));
}

GLAPI void APIENTRY glNormal3fv(const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_normal(ctx, (GLfloat)(v[0]), (GLfloat)(v[1]), (GLfloat)(v[2]));
}

GLAPI void APIENTRY glNormal3i(GLint nx, GLint ny, GLint nz)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_normal(ctx, (GLfloat)((2.0 * (nx) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (ny) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (nz) + 1.0) / 4294967295.0));
}

GLAPI void APIENTRY glNormal3iv(const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_normal(ctx, (GLfloat)((2.0 * (v[0]) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (v[1]) + 1.0) / 4294967295.0), (GLfloat)((2.0 * (v[2]) + 1.0) / 4294967295.0));
}

GLAPI void APIENTRY glNormal3s(GLshort nx, GLshort ny, GLshort nz)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_normal(ctx, (GLfloat)((2.0 * (nx) + 1.0) / 65535.0), (GLfloat)((2.0 * (ny) + 1.0) / 65535.0), (GLfloat)((2.0 * (nz) + 1.0) / 65535.0));
}

GLAPI void APIENTRY glNormal3sv(const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_normal(ctx, (GLfloat)((2.0 * (v[0]) + 1.0) / 65535.0), (GLfloat)((2.0 * (v[1]) + 1.0) / 65535.0), (GLfloat)((2.0 * (v[2]) + 1.0) / 65535.0));
}

/* ---------------------------------------------------------------- texture coordinates */

GLAPI void APIENTRY glTexCoord1d(GLdouble s)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, 0, 0, 1);
}

GLAPI void APIENTRY glTexCoord1dv(const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], 0, 0, 1);
}

GLAPI void APIENTRY glTexCoord1f(GLfloat s)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, 0, 0, 1);
}

GLAPI void APIENTRY glTexCoord1fv(const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], 0, 0, 1);
}

GLAPI void APIENTRY glTexCoord1i(GLint s)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, 0, 0, 1);
}

GLAPI void APIENTRY glTexCoord1iv(const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], 0, 0, 1);
}

GLAPI void APIENTRY glTexCoord1s(GLshort s)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, 0, 0, 1);
}

GLAPI void APIENTRY glTexCoord1sv(const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], 0, 0, 1);
}

GLAPI void APIENTRY glTexCoord2d(GLdouble s, GLdouble t)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, (GLfloat)t, 0, 1);
}

GLAPI void APIENTRY glTexCoord2dv(const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], (GLfloat)v[1], 0, 1);
}

GLAPI void APIENTRY glTexCoord2f(GLfloat s, GLfloat t)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, (GLfloat)t, 0, 1);
}

GLAPI void APIENTRY glTexCoord2fv(const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], (GLfloat)v[1], 0, 1);
}

GLAPI void APIENTRY glTexCoord2i(GLint s, GLint t)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, (GLfloat)t, 0, 1);
}

GLAPI void APIENTRY glTexCoord2iv(const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], (GLfloat)v[1], 0, 1);
}

GLAPI void APIENTRY glTexCoord2s(GLshort s, GLshort t)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, (GLfloat)t, 0, 1);
}

GLAPI void APIENTRY glTexCoord2sv(const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], (GLfloat)v[1], 0, 1);
}

GLAPI void APIENTRY glTexCoord3d(GLdouble s, GLdouble t, GLdouble r)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1);
}

GLAPI void APIENTRY glTexCoord3dv(const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);
}

GLAPI void APIENTRY glTexCoord3f(GLfloat s, GLfloat t, GLfloat r)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1);
}

GLAPI void APIENTRY glTexCoord3fv(const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);
}

GLAPI void APIENTRY glTexCoord3i(GLint s, GLint t, GLint r)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1);
}

GLAPI void APIENTRY glTexCoord3iv(const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);
}

GLAPI void APIENTRY glTexCoord3s(GLshort s, GLshort t, GLshort r)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1);
}

GLAPI void APIENTRY glTexCoord3sv(const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);
}

GLAPI void APIENTRY glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q);
}

GLAPI void APIENTRY glTexCoord4dv(const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q);
}

GLAPI void APIENTRY glTexCoord4fv(const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glTexCoord4i(GLint s, GLint t, GLint r, GLint q)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q);
}

GLAPI void APIENTRY glTexCoord4iv(const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q);
}

GLAPI void APIENTRY glTexCoord4sv(const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    gl_ff_texcoord(ctx, 0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glMultiTexCoord1d(GLenum target, GLdouble s)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1dv(GLenum target, const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1f(GLenum target, GLfloat s)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1fv(GLenum target, const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1i(GLenum target, GLint s)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1iv(GLenum target, const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1s(GLenum target, GLshort s)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1sv(GLenum target, const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2d(GLenum target, GLdouble s, GLdouble t)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2dv(GLenum target, const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2fv(GLenum target, const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2i(GLenum target, GLint s, GLint t)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2iv(GLenum target, const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2s(GLenum target, GLshort s, GLshort t)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2sv(GLenum target, const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3d(GLenum target, GLdouble s, GLdouble t, GLdouble r)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3dv(GLenum target, const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3f(GLenum target, GLfloat s, GLfloat t, GLfloat r)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3fv(GLenum target, const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3i(GLenum target, GLint s, GLint t, GLint r)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3iv(GLenum target, const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3s(GLenum target, GLshort s, GLshort t, GLshort r)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3sv(GLenum target, const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4d(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4dv(GLenum target, const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4fv(GLenum target, const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4i(GLenum target, GLint s, GLint t, GLint r, GLint q)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4iv(GLenum target, const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4s(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4sv(GLenum target, const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1dARB(GLenum target, GLdouble s)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1dvARB(GLenum target, const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1fARB(GLenum target, GLfloat s)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1fvARB(GLenum target, const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1iARB(GLenum target, GLint s)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1ivARB(GLenum target, const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1sARB(GLenum target, GLshort s)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord1svARB(GLenum target, const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], 0, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2dARB(GLenum target, GLdouble s, GLdouble t)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2dvARB(GLenum target, const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2fvARB(GLenum target, const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2iARB(GLenum target, GLint s, GLint t)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2ivARB(GLenum target, const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2sARB(GLenum target, GLshort s, GLshort t)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord2svARB(GLenum target, const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], 0, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3dARB(GLenum target, GLdouble s, GLdouble t, GLdouble r)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3dvARB(GLenum target, const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3fvARB(GLenum target, const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3iARB(GLenum target, GLint s, GLint t, GLint r)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3ivARB(GLenum target, const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3sARB(GLenum target, GLshort s, GLshort t, GLshort r)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord3svARB(GLenum target, const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4dARB(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4dvARB(GLenum target, const GLdouble *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4fvARB(GLenum target, const GLfloat *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4iARB(GLenum target, GLint s, GLint t, GLint r, GLint q)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4ivARB(GLenum target, const GLint *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4sARB(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q))
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glMultiTexCoord4svARB(GLenum target, const GLshort *v)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (target < GL_TEXTURE0 || !gl_ff_texcoord(ctx, target - GL_TEXTURE0, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]))
        gl_error(ctx, GL_INVALID_ENUM);
}
