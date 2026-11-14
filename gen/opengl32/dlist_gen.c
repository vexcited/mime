#define MIME_GL_NO_RENAME
#include "opengl32.h"
#include "dlist.h"

void APIENTRY gl_do_glActiveTexture(GLenum texture);
void APIENTRY gl_do_glAlphaFunc(GLenum func, GLfloat ref);
void APIENTRY gl_do_glBeginQuery(GLenum target, GLuint name);
void APIENTRY gl_do_glBindTexture(GLenum target, GLuint name);
void APIENTRY gl_do_glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void APIENTRY gl_do_glBlendEquation(GLenum mode);
void APIENTRY gl_do_glBlendEquationSeparate(GLenum rgb, GLenum alpha);
void APIENTRY gl_do_glBlendFunc(GLenum src, GLenum dst);
void APIENTRY gl_do_glBlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha);
void APIENTRY gl_do_glClear(GLbitfield mask);
void APIENTRY gl_do_glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void APIENTRY gl_do_glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void APIENTRY gl_do_glClearDepth(GLdouble d);
void APIENTRY gl_do_glClearDepthf(GLfloat d);
void APIENTRY gl_do_glClearIndex(GLfloat c);
void APIENTRY gl_do_glClearStencil(GLint s);
void APIENTRY gl_do_glClipPlane(GLenum plane, const GLdouble *equation);
void APIENTRY gl_do_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void APIENTRY gl_do_glColorMaterial(GLenum face, GLenum mode);
void APIENTRY gl_do_glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type);
void APIENTRY gl_do_glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border);
void APIENTRY gl_do_glCopyTexImage2D(GLenum target, GLint level, GLenum internal, GLint x, GLint y, GLsizei w,                                      GLsizei h, GLint border);
void APIENTRY gl_do_glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
void APIENTRY gl_do_glCopyTexSubImage2D(GLenum target, GLint level, GLint dx, GLint dy, GLint x, GLint y, GLsizei w,                                         GLsizei h);
void APIENTRY gl_do_glCullFace(GLenum mode);
void APIENTRY gl_do_glDepthFunc(GLenum func);
void APIENTRY gl_do_glDepthMask(GLboolean flag);
void APIENTRY gl_do_glDepthRange(GLdouble n, GLdouble f);
void APIENTRY gl_do_glDepthRangef(GLfloat n, GLfloat f);
void APIENTRY gl_do_glDisable(GLenum cap);
void APIENTRY gl_do_glDrawBuffer(GLenum buf);
void APIENTRY gl_do_glDrawBuffers(GLsizei n, const GLenum *bufs);
void APIENTRY gl_do_glEnable(GLenum cap);
void APIENTRY gl_do_glEndQuery(GLenum target);
void APIENTRY gl_do_glFogf(GLenum pname, GLfloat param);
void APIENTRY gl_do_glFogfv(GLenum pname, const GLfloat *params);
void APIENTRY gl_do_glFogi(GLenum pname, GLint param);
void APIENTRY gl_do_glFogiv(GLenum pname, const GLint *params);
void APIENTRY gl_do_glFrontFace(GLenum mode);
void APIENTRY gl_do_glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top_, GLdouble near_val,                               GLdouble far_val);
void APIENTRY gl_do_glInitNames(void);
void APIENTRY gl_do_glLightModelf(GLenum pname, GLfloat param);
void APIENTRY gl_do_glLightModelfv(GLenum pname, const GLfloat *params);
void APIENTRY gl_do_glLightModeli(GLenum pname, GLint param);
void APIENTRY gl_do_glLightModeliv(GLenum pname, const GLint *params);
void APIENTRY gl_do_glLightf(GLenum l, GLenum pname, GLfloat param);
void APIENTRY gl_do_glLightfv(GLenum l, GLenum pname, const GLfloat *params);
void APIENTRY gl_do_glLighti(GLenum l, GLenum pname, GLint param);
void APIENTRY gl_do_glLightiv(GLenum l, GLenum pname, const GLint *params);
void APIENTRY gl_do_glLineStipple(GLint factor, GLushort pattern);
void APIENTRY gl_do_glLineWidth(GLfloat width);
void APIENTRY gl_do_glListBase(GLuint base);
void APIENTRY gl_do_glLoadIdentity(void);
void APIENTRY gl_do_glLoadMatrixd(const GLdouble *m);
void APIENTRY gl_do_glLoadMatrixf(const GLfloat *m);
void APIENTRY gl_do_glLoadName(GLuint name);
void APIENTRY gl_do_glLoadTransposeMatrixd(const GLdouble *m);
void APIENTRY gl_do_glLoadTransposeMatrixdARB(const GLdouble *m);
void APIENTRY gl_do_glLoadTransposeMatrixf(const GLfloat *m);
void APIENTRY gl_do_glLoadTransposeMatrixfARB(const GLfloat *m);
void APIENTRY gl_do_glLogicOp(GLenum opcode);
void APIENTRY gl_do_glMaterialf(GLenum face, GLenum pname, GLfloat param);
void APIENTRY gl_do_glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);
void APIENTRY gl_do_glMateriali(GLenum face, GLenum pname, GLint param);
void APIENTRY gl_do_glMaterialiv(GLenum face, GLenum pname, const GLint *params);
void APIENTRY gl_do_glMatrixMode(GLenum mode);
void APIENTRY gl_do_glMultMatrixd(const GLdouble *m);
void APIENTRY gl_do_glMultMatrixf(const GLfloat *m);
void APIENTRY gl_do_glMultTransposeMatrixd(const GLdouble *m);
void APIENTRY gl_do_glMultTransposeMatrixdARB(const GLdouble *m);
void APIENTRY gl_do_glMultTransposeMatrixf(const GLfloat *m);
void APIENTRY gl_do_glMultTransposeMatrixfARB(const GLfloat *m);
void APIENTRY gl_do_glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top_, GLdouble near_val,                             GLdouble far_val);
void APIENTRY gl_do_glPassThrough(GLfloat token);
void APIENTRY gl_do_glPixelTransferf(GLenum pname, GLfloat param);
void APIENTRY gl_do_glPixelTransferi(GLenum pname, GLint param);
void APIENTRY gl_do_glPixelZoom(GLfloat xfactor, GLfloat yfactor);
void APIENTRY gl_do_glPointParameterf(GLenum pname, GLfloat value);
void APIENTRY gl_do_glPointParameterfARB(GLenum pname, GLfloat value);
void APIENTRY gl_do_glPointParameterfEXT(GLenum pname, GLfloat value);
void APIENTRY gl_do_glPointParameterfv(GLenum pname, const GLfloat *values);
void APIENTRY gl_do_glPointParameterfvARB(GLenum pname, const GLfloat *values);
void APIENTRY gl_do_glPointParameterfvEXT(GLenum pname, const GLfloat *values);
void APIENTRY gl_do_glPointParameteri(GLenum pname, GLint value);
void APIENTRY gl_do_glPointParameteriv(GLenum pname, const GLint *values);
void APIENTRY gl_do_glPointSize(GLfloat size);
void APIENTRY gl_do_glPolygonMode(GLenum face, GLenum mode);
void APIENTRY gl_do_glPolygonOffset(GLfloat factor, GLfloat units);
void APIENTRY gl_do_glPolygonOffsetClamp(GLfloat factor, GLfloat units, GLfloat clamp);
void APIENTRY gl_do_glPopMatrix(void);
void APIENTRY gl_do_glPopName(void);
void APIENTRY gl_do_glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities);
void APIENTRY gl_do_glPushAttrib(GLbitfield mask);
void APIENTRY gl_do_glPushMatrix(void);
void APIENTRY gl_do_glPushName(GLuint name);
void APIENTRY gl_do_glReadBuffer(GLenum buf);
void APIENTRY gl_do_glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void APIENTRY gl_do_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void APIENTRY gl_do_glScaled(GLdouble x, GLdouble y, GLdouble z);
void APIENTRY gl_do_glScalef(GLfloat x, GLfloat y, GLfloat z);
void APIENTRY gl_do_glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
void APIENTRY gl_do_glShadeModel(GLenum mode);
void APIENTRY gl_do_glStencilFunc(GLenum func, GLint ref, GLuint mask);
void APIENTRY gl_do_glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
void APIENTRY gl_do_glStencilMask(GLuint mask);
void APIENTRY gl_do_glStencilMaskSeparate(GLenum face, GLuint mask);
void APIENTRY gl_do_glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void APIENTRY gl_do_glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass);
void APIENTRY gl_do_glTexEnvf(GLenum target, GLenum pname, GLfloat param);
void APIENTRY gl_do_glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);
void APIENTRY gl_do_glTexEnvi(GLenum target, GLenum pname, GLint param);
void APIENTRY gl_do_glTexEnviv(GLenum target, GLenum pname, const GLint *params);
void APIENTRY gl_do_glTexGend(GLenum coord, GLenum pname, GLdouble param);
void APIENTRY gl_do_glTexGendv(GLenum coord, GLenum pname, const GLdouble *params);
void APIENTRY gl_do_glTexGenf(GLenum coord, GLenum pname, GLfloat param);
void APIENTRY gl_do_glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params);
void APIENTRY gl_do_glTexGeni(GLenum coord, GLenum pname, GLint param);
void APIENTRY gl_do_glTexGeniv(GLenum coord, GLenum pname, const GLint *params);
void APIENTRY gl_do_glTexParameterf(GLenum target, GLenum pname, GLfloat param);
void APIENTRY gl_do_glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
void APIENTRY gl_do_glTexParameteri(GLenum target, GLenum pname, GLint param);
void APIENTRY gl_do_glTexParameteriv(GLenum target, GLenum pname, const GLint *params);
void APIENTRY gl_do_glTranslated(GLdouble x, GLdouble y, GLdouble z);
void APIENTRY gl_do_glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void APIENTRY gl_do_glUniform1f(GLint l, GLfloat a);
void APIENTRY gl_do_glUniform1fv(GLint l, GLsizei n, const GLfloat *v);
void APIENTRY gl_do_glUniform1i(GLint l, GLint a);
void APIENTRY gl_do_glUniform1iv(GLint l, GLsizei n, const GLint *v);
void APIENTRY gl_do_glUniform1ui(GLint l, GLuint a);
void APIENTRY gl_do_glUniform1uiv(GLint l, GLsizei n, const GLuint *v);
void APIENTRY gl_do_glUniform2f(GLint l, GLfloat a, GLfloat b);
void APIENTRY gl_do_glUniform2fv(GLint l, GLsizei n, const GLfloat *v);
void APIENTRY gl_do_glUniform2i(GLint l, GLint a, GLint b);
void APIENTRY gl_do_glUniform2iv(GLint l, GLsizei n, const GLint *v);
void APIENTRY gl_do_glUniform2ui(GLint l, GLuint a, GLuint b);
void APIENTRY gl_do_glUniform2uiv(GLint l, GLsizei n, const GLuint *v);
void APIENTRY gl_do_glUniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c);
void APIENTRY gl_do_glUniform3fv(GLint l, GLsizei n, const GLfloat *v);
void APIENTRY gl_do_glUniform3i(GLint l, GLint a, GLint b, GLint c);
void APIENTRY gl_do_glUniform3iv(GLint l, GLsizei n, const GLint *v);
void APIENTRY gl_do_glUniform3ui(GLint l, GLuint a, GLuint b, GLuint c);
void APIENTRY gl_do_glUniform3uiv(GLint l, GLsizei n, const GLuint *v);
void APIENTRY gl_do_glUniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d);
void APIENTRY gl_do_glUniform4fv(GLint l, GLsizei n, const GLfloat *v);
void APIENTRY gl_do_glUniform4i(GLint l, GLint a, GLint b, GLint c, GLint d);
void APIENTRY gl_do_glUniform4iv(GLint l, GLsizei n, const GLint *v);
void APIENTRY gl_do_glUniform4ui(GLint l, GLuint a, GLuint b, GLuint c, GLuint d);
void APIENTRY gl_do_glUniform4uiv(GLint l, GLsizei n, const GLuint *v);
void APIENTRY gl_do_glUniformMatrix2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY gl_do_glUniformMatrix2x3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY gl_do_glUniformMatrix2x4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY gl_do_glUniformMatrix3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY gl_do_glUniformMatrix3x2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY gl_do_glUniformMatrix3x4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY gl_do_glUniformMatrix4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY gl_do_glUniformMatrix4x2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY gl_do_glUniformMatrix4x3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY gl_do_glUseProgram(GLuint name);
void APIENTRY gl_do_glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void APIENTRY gl_do_glArrayElement(GLint i);
BOOL gl_dl_custom_glArrayElement(GlContext *ctx, GLint i);
void APIENTRY gl_do_glBegin(GLenum mode);
BOOL gl_dl_custom_glBegin(GlContext *ctx, GLenum mode);
void APIENTRY gl_do_glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap);
BOOL gl_dl_custom_glBitmap(GlContext *ctx, GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap);
void APIENTRY gl_do_glCallList(GLuint list);
BOOL gl_dl_custom_glCallList(GlContext *ctx, GLuint list);
void APIENTRY gl_do_glCallLists(GLsizei n, GLenum type, const GLvoid *lists);
BOOL gl_dl_custom_glCallLists(GlContext *ctx, GLsizei n, GLenum type, const GLvoid *lists);
void APIENTRY gl_do_glCompressedTexImage2D(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h,                                            GLint border, GLsizei size, const void *data);
BOOL gl_dl_custom_glCompressedTexImage2D(GlContext *ctx, GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h,                                            GLint border, GLsizei size, const void *data);
void APIENTRY gl_do_glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,                                               GLenum format, GLsizei size, const void *data);
BOOL gl_dl_custom_glCompressedTexSubImage2D(GlContext *ctx, GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,                                               GLenum format, GLsizei size, const void *data);
void APIENTRY gl_do_glDrawArrays(GLenum mode, GLint first, GLsizei count);
BOOL gl_dl_custom_glDrawArrays(GlContext *ctx, GLenum mode, GLint first, GLsizei count);
void APIENTRY gl_do_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
BOOL gl_dl_custom_glDrawElements(GlContext *ctx, GLenum mode, GLsizei count, GLenum type, const void *indices);
void APIENTRY gl_do_glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
BOOL gl_dl_custom_glDrawPixels(GlContext *ctx, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void APIENTRY gl_do_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,                                         const void *indices);
BOOL gl_dl_custom_glDrawRangeElements(GlContext *ctx, GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,                                         const void *indices);
void APIENTRY gl_do_glEdgeFlag(GLboolean flag);
BOOL gl_dl_custom_glEdgeFlag(GlContext *ctx, GLboolean flag);
void APIENTRY gl_do_glEdgeFlagv(const GLboolean *flag);
BOOL gl_dl_custom_glEdgeFlagv(GlContext *ctx, const GLboolean *flag);
void APIENTRY gl_do_glEnd(void);
BOOL gl_dl_custom_glEnd(GlContext *ctx);
void APIENTRY gl_do_glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei n);
BOOL gl_dl_custom_glMultiDrawArrays(GlContext *ctx, GLenum mode, const GLint *first, const GLsizei *count, GLsizei n);
void APIENTRY gl_do_glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const void *const *indices,                                         GLsizei n);
BOOL gl_dl_custom_glMultiDrawElements(GlContext *ctx, GLenum mode, const GLsizei *count, GLenum type, const void *const *indices,                                         GLsizei n);
void APIENTRY gl_do_glPolygonStipple(const GLubyte *mask);
BOOL gl_dl_custom_glPolygonStipple(GlContext *ctx, const GLubyte *mask);
void APIENTRY gl_do_glPopAttrib(void);
BOOL gl_dl_custom_glPopAttrib(GlContext *ctx);
void APIENTRY gl_do_glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2);
BOOL gl_dl_custom_glRectd(GlContext *ctx, GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2);
void APIENTRY gl_do_glRectdv(const GLdouble *v1, const GLdouble *v2);
BOOL gl_dl_custom_glRectdv(GlContext *ctx, const GLdouble *v1, const GLdouble *v2);
void APIENTRY gl_do_glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
BOOL gl_dl_custom_glRectf(GlContext *ctx, GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
void APIENTRY gl_do_glRectfv(const GLfloat *v1, const GLfloat *v2);
BOOL gl_dl_custom_glRectfv(GlContext *ctx, const GLfloat *v1, const GLfloat *v2);
void APIENTRY gl_do_glRecti(GLint x1, GLint y1, GLint x2, GLint y2);
BOOL gl_dl_custom_glRecti(GlContext *ctx, GLint x1, GLint y1, GLint x2, GLint y2);
void APIENTRY gl_do_glRectiv(const GLint *v1, const GLint *v2);
BOOL gl_dl_custom_glRectiv(GlContext *ctx, const GLint *v1, const GLint *v2);
void APIENTRY gl_do_glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2);
BOOL gl_dl_custom_glRects(GlContext *ctx, GLshort x1, GLshort y1, GLshort x2, GLshort y2);
void APIENTRY gl_do_glRectsv(const GLshort *v1, const GLshort *v2);
BOOL gl_dl_custom_glRectsv(GlContext *ctx, const GLshort *v1, const GLshort *v2);
void APIENTRY gl_do_glTexImage1D(GLenum target, GLint level, GLint internal, GLsizei w, GLint border, GLenum format,                                  GLenum type, const void *pixels);
BOOL gl_dl_custom_glTexImage1D(GlContext *ctx, GLenum target, GLint level, GLint internal, GLsizei w, GLint border, GLenum format,                                  GLenum type, const void *pixels);
void APIENTRY gl_do_glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,                                  GLenum format, GLenum type, const void *pixels);
BOOL gl_dl_custom_glTexImage2D(GlContext *ctx, GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,                                  GLenum format, GLenum type, const void *pixels);
void APIENTRY gl_do_glTexImage3D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d,                                  GLint border, GLenum format, GLenum type, const void *pixels);
BOOL gl_dl_custom_glTexImage3D(GlContext *ctx, GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d,                                  GLint border, GLenum format, GLenum type, const void *pixels);
void APIENTRY gl_do_glTexSubImage1D(GLenum target, GLint level, GLint x, GLsizei w, GLenum format, GLenum type,                                     const void *pixels);
BOOL gl_dl_custom_glTexSubImage1D(GlContext *ctx, GLenum target, GLint level, GLint x, GLsizei w, GLenum format, GLenum type,                                     const void *pixels);
void APIENTRY gl_do_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,                                     GLenum type, const void *pixels);
BOOL gl_dl_custom_glTexSubImage2D(GlContext *ctx, GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,                                     GLenum type, const void *pixels);
void APIENTRY gl_do_glTexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h,                                     GLsizei d, GLenum format, GLenum type, const void *pixels);
BOOL gl_dl_custom_glTexSubImage3D(GlContext *ctx, GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h,                                     GLsizei d, GLenum format, GLenum type, const void *pixels);

static void rec_glActiveTexture(GlContext *ctx, GLenum texture)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 0, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)texture);
}

static void run_glActiveTexture(const uint8_t *p)
{
    gl_do_glActiveTexture((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glAlphaFunc(GlContext *ctx, GLenum func, GLfloat ref)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 1, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)func);
    gl_dl_put_f(p, 1, ref);
}

static void run_glAlphaFunc(const uint8_t *p)
{
    gl_do_glAlphaFunc((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_f(p, 1));
}

static void rec_glBeginQuery(GlContext *ctx, GLenum target, GLuint name)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 2, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)name);
}

static void run_glBeginQuery(const uint8_t *p)
{
    gl_do_glBeginQuery((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLuint)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glBindTexture(GlContext *ctx, GLenum target, GLuint name)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 3, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)name);
}

static void run_glBindTexture(const uint8_t *p)
{
    gl_do_glBindTexture((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLuint)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glBlendColor(GlContext *ctx, GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 4, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, r);
    gl_dl_put_f(p, 1, g);
    gl_dl_put_f(p, 2, b);
    gl_dl_put_f(p, 3, a);
}

static void run_glBlendColor(const uint8_t *p)
{
    gl_do_glBlendColor(gl_dl_get_f(p, 0), gl_dl_get_f(p, 1), gl_dl_get_f(p, 2), gl_dl_get_f(p, 3));
}

static void rec_glBlendEquation(GlContext *ctx, GLenum mode)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 5, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)mode);
}

static void run_glBlendEquation(const uint8_t *p)
{
    gl_do_glBlendEquation((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glBlendEquationSeparate(GlContext *ctx, GLenum rgb, GLenum alpha)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 6, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)rgb);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)alpha);
}

static void run_glBlendEquationSeparate(const uint8_t *p)
{
    gl_do_glBlendEquationSeparate((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glBlendFunc(GlContext *ctx, GLenum src, GLenum dst)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 7, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)src);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)dst);
}

static void run_glBlendFunc(const uint8_t *p)
{
    gl_do_glBlendFunc((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glBlendFuncSeparate(GlContext *ctx, GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 8, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)src_rgb);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)dst_rgb);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)src_alpha);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)dst_alpha);
}

static void run_glBlendFuncSeparate(const uint8_t *p)
{
    gl_do_glBlendFuncSeparate((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), (GLenum)(int64_t)gl_dl_get_i(p, 2), (GLenum)(int64_t)gl_dl_get_i(p, 3));
}

static void rec_glClear(GlContext *ctx, GLbitfield mask)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 9, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)mask);
}

static void run_glClear(const uint8_t *p)
{
    gl_do_glClear((GLbitfield)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glClearAccum(GlContext *ctx, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 10, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, red);
    gl_dl_put_f(p, 1, green);
    gl_dl_put_f(p, 2, blue);
    gl_dl_put_f(p, 3, alpha);
}

static void run_glClearAccum(const uint8_t *p)
{
    gl_do_glClearAccum(gl_dl_get_f(p, 0), gl_dl_get_f(p, 1), gl_dl_get_f(p, 2), gl_dl_get_f(p, 3));
}

static void rec_glClearColor(GlContext *ctx, GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 11, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, r);
    gl_dl_put_f(p, 1, g);
    gl_dl_put_f(p, 2, b);
    gl_dl_put_f(p, 3, a);
}

static void run_glClearColor(const uint8_t *p)
{
    gl_do_glClearColor(gl_dl_get_f(p, 0), gl_dl_get_f(p, 1), gl_dl_get_f(p, 2), gl_dl_get_f(p, 3));
}

static void rec_glClearDepth(GlContext *ctx, GLdouble d)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 12, at);
    if (!p)
        return;
    gl_dl_put_d(p, 0, d);
}

static void run_glClearDepth(const uint8_t *p)
{
    gl_do_glClearDepth(gl_dl_get_d(p, 0));
}

static void rec_glClearDepthf(GlContext *ctx, GLfloat d)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 13, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, d);
}

static void run_glClearDepthf(const uint8_t *p)
{
    gl_do_glClearDepthf(gl_dl_get_f(p, 0));
}

static void rec_glClearIndex(GlContext *ctx, GLfloat c)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 14, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, c);
}

static void run_glClearIndex(const uint8_t *p)
{
    gl_do_glClearIndex(gl_dl_get_f(p, 0));
}

static void rec_glClearStencil(GlContext *ctx, GLint s)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 15, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)s);
}

static void run_glClearStencil(const uint8_t *p)
{
    gl_do_glClearStencil((GLint)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glClipPlane(GlContext *ctx, GLenum plane, const GLdouble *equation)
{
    size_t at = 2 * 8;
    size_t at_equation = at, bytes_equation = equation ? gl_dl_bytes((long long)(4), sizeof(GLdouble)) : 0;
    at += gl_dl_blob(bytes_equation);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 16, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)plane);
    if (equation) {
        gl_dl_put_i(p, 1, at_equation);
        memcpy(p + at_equation, equation, bytes_equation);
    }
}

static void run_glClipPlane(const uint8_t *p)
{
    gl_do_glClipPlane((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_i(p, 1) ? (const GLdouble *)(p + gl_dl_get_i(p, 1)) : nullptr);
}

static void rec_glColorMask(GlContext *ctx, GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 17, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)r);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)g);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)b);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)a);
}

static void run_glColorMask(const uint8_t *p)
{
    gl_do_glColorMask((GLboolean)(int64_t)gl_dl_get_i(p, 0), (GLboolean)(int64_t)gl_dl_get_i(p, 1), (GLboolean)(int64_t)gl_dl_get_i(p, 2), (GLboolean)(int64_t)gl_dl_get_i(p, 3));
}

static void rec_glColorMaterial(GlContext *ctx, GLenum face, GLenum mode)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 18, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)face);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)mode);
}

static void run_glColorMaterial(const uint8_t *p)
{
    gl_do_glColorMaterial((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glCopyPixels(GlContext *ctx, GLint x, GLint y, GLsizei width, GLsizei height, GLenum type)
{
    size_t at = 5 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 19, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)x);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)y);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)width);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)height);
    gl_dl_put_i(p, 4, (uint64_t)(int64_t)type);
}

static void run_glCopyPixels(const uint8_t *p)
{
    gl_do_glCopyPixels((GLint)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1), (GLsizei)(int64_t)gl_dl_get_i(p, 2), (GLsizei)(int64_t)gl_dl_get_i(p, 3), (GLenum)(int64_t)gl_dl_get_i(p, 4));
}

static void rec_glCopyTexImage1D(GlContext *ctx, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border)
{
    size_t at = 7 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 20, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)level);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)internalformat);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)x);
    gl_dl_put_i(p, 4, (uint64_t)(int64_t)y);
    gl_dl_put_i(p, 5, (uint64_t)(int64_t)width);
    gl_dl_put_i(p, 6, (uint64_t)(int64_t)border);
}

static void run_glCopyTexImage1D(const uint8_t *p)
{
    gl_do_glCopyTexImage1D((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1), (GLenum)(int64_t)gl_dl_get_i(p, 2), (GLint)(int64_t)gl_dl_get_i(p, 3), (GLint)(int64_t)gl_dl_get_i(p, 4), (GLsizei)(int64_t)gl_dl_get_i(p, 5), (GLint)(int64_t)gl_dl_get_i(p, 6));
}

static void rec_glCopyTexImage2D(GlContext *ctx, GLenum target, GLint level, GLenum internal, GLint x, GLint y, GLsizei w,                                      GLsizei h, GLint border)
{
    size_t at = 8 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 21, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)level);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)internal);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)x);
    gl_dl_put_i(p, 4, (uint64_t)(int64_t)y);
    gl_dl_put_i(p, 5, (uint64_t)(int64_t)w);
    gl_dl_put_i(p, 6, (uint64_t)(int64_t)h);
    gl_dl_put_i(p, 7, (uint64_t)(int64_t)border);
}

static void run_glCopyTexImage2D(const uint8_t *p)
{
    gl_do_glCopyTexImage2D((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1), (GLenum)(int64_t)gl_dl_get_i(p, 2), (GLint)(int64_t)gl_dl_get_i(p, 3), (GLint)(int64_t)gl_dl_get_i(p, 4), (GLsizei)(int64_t)gl_dl_get_i(p, 5), (GLsizei)(int64_t)gl_dl_get_i(p, 6), (GLint)(int64_t)gl_dl_get_i(p, 7));
}

static void rec_glCopyTexSubImage1D(GlContext *ctx, GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width)
{
    size_t at = 6 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 22, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)level);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)xoffset);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)x);
    gl_dl_put_i(p, 4, (uint64_t)(int64_t)y);
    gl_dl_put_i(p, 5, (uint64_t)(int64_t)width);
}

static void run_glCopyTexSubImage1D(const uint8_t *p)
{
    gl_do_glCopyTexSubImage1D((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1), (GLint)(int64_t)gl_dl_get_i(p, 2), (GLint)(int64_t)gl_dl_get_i(p, 3), (GLint)(int64_t)gl_dl_get_i(p, 4), (GLsizei)(int64_t)gl_dl_get_i(p, 5));
}

static void rec_glCopyTexSubImage2D(GlContext *ctx, GLenum target, GLint level, GLint dx, GLint dy, GLint x, GLint y, GLsizei w,                                         GLsizei h)
{
    size_t at = 8 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 23, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)level);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)dx);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)dy);
    gl_dl_put_i(p, 4, (uint64_t)(int64_t)x);
    gl_dl_put_i(p, 5, (uint64_t)(int64_t)y);
    gl_dl_put_i(p, 6, (uint64_t)(int64_t)w);
    gl_dl_put_i(p, 7, (uint64_t)(int64_t)h);
}

static void run_glCopyTexSubImage2D(const uint8_t *p)
{
    gl_do_glCopyTexSubImage2D((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1), (GLint)(int64_t)gl_dl_get_i(p, 2), (GLint)(int64_t)gl_dl_get_i(p, 3), (GLint)(int64_t)gl_dl_get_i(p, 4), (GLint)(int64_t)gl_dl_get_i(p, 5), (GLsizei)(int64_t)gl_dl_get_i(p, 6), (GLsizei)(int64_t)gl_dl_get_i(p, 7));
}

static void rec_glCullFace(GlContext *ctx, GLenum mode)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 24, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)mode);
}

static void run_glCullFace(const uint8_t *p)
{
    gl_do_glCullFace((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glDepthFunc(GlContext *ctx, GLenum func)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 25, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)func);
}

static void run_glDepthFunc(const uint8_t *p)
{
    gl_do_glDepthFunc((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glDepthMask(GlContext *ctx, GLboolean flag)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 26, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)flag);
}

static void run_glDepthMask(const uint8_t *p)
{
    gl_do_glDepthMask((GLboolean)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glDepthRange(GlContext *ctx, GLdouble n, GLdouble f)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 27, at);
    if (!p)
        return;
    gl_dl_put_d(p, 0, n);
    gl_dl_put_d(p, 1, f);
}

static void run_glDepthRange(const uint8_t *p)
{
    gl_do_glDepthRange(gl_dl_get_d(p, 0), gl_dl_get_d(p, 1));
}

static void rec_glDepthRangef(GlContext *ctx, GLfloat n, GLfloat f)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 28, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, n);
    gl_dl_put_f(p, 1, f);
}

static void run_glDepthRangef(const uint8_t *p)
{
    gl_do_glDepthRangef(gl_dl_get_f(p, 0), gl_dl_get_f(p, 1));
}

static void rec_glDisable(GlContext *ctx, GLenum cap)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 29, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)cap);
}

static void run_glDisable(const uint8_t *p)
{
    gl_do_glDisable((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glDrawBuffer(GlContext *ctx, GLenum buf)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 30, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)buf);
}

static void run_glDrawBuffer(const uint8_t *p)
{
    gl_do_glDrawBuffer((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glDrawBuffers(GlContext *ctx, GLsizei n, const GLenum *bufs)
{
    size_t at = 2 * 8;
    size_t at_bufs = at, bytes_bufs = bufs ? gl_dl_bytes((long long)(n), sizeof(GLenum)) : 0;
    at += gl_dl_blob(bytes_bufs);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 31, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)n);
    if (bufs) {
        gl_dl_put_i(p, 1, at_bufs);
        memcpy(p + at_bufs, bufs, bytes_bufs);
    }
}

static void run_glDrawBuffers(const uint8_t *p)
{
    gl_do_glDrawBuffers((GLsizei)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_i(p, 1) ? (const GLenum *)(p + gl_dl_get_i(p, 1)) : nullptr);
}

static void rec_glEnable(GlContext *ctx, GLenum cap)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 32, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)cap);
}

static void run_glEnable(const uint8_t *p)
{
    gl_do_glEnable((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glEndQuery(GlContext *ctx, GLenum target)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 33, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
}

static void run_glEndQuery(const uint8_t *p)
{
    gl_do_glEndQuery((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glFogf(GlContext *ctx, GLenum pname, GLfloat param)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 34, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    gl_dl_put_f(p, 1, param);
}

static void run_glFogf(const uint8_t *p)
{
    gl_do_glFogf((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_f(p, 1));
}

static void rec_glFogfv(GlContext *ctx, GLenum pname, const GLfloat *params)
{
    size_t at = 2 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_fog_count(pname)), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 35, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 1, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glFogfv(const uint8_t *p)
{
    gl_do_glFogfv((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_i(p, 1) ? (const GLfloat *)(p + gl_dl_get_i(p, 1)) : nullptr);
}

static void rec_glFogi(GlContext *ctx, GLenum pname, GLint param)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 36, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)param);
}

static void run_glFogi(const uint8_t *p)
{
    gl_do_glFogi((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glFogiv(GlContext *ctx, GLenum pname, const GLint *params)
{
    size_t at = 2 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_fog_count(pname)), sizeof(GLint)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 37, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 1, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glFogiv(const uint8_t *p)
{
    gl_do_glFogiv((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_i(p, 1) ? (const GLint *)(p + gl_dl_get_i(p, 1)) : nullptr);
}

static void rec_glFrontFace(GlContext *ctx, GLenum mode)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 38, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)mode);
}

static void run_glFrontFace(const uint8_t *p)
{
    gl_do_glFrontFace((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glFrustum(GlContext *ctx, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top_, GLdouble near_val,                               GLdouble far_val)
{
    size_t at = 6 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 39, at);
    if (!p)
        return;
    gl_dl_put_d(p, 0, left);
    gl_dl_put_d(p, 1, right);
    gl_dl_put_d(p, 2, bottom);
    gl_dl_put_d(p, 3, top_);
    gl_dl_put_d(p, 4, near_val);
    gl_dl_put_d(p, 5, far_val);
}

static void run_glFrustum(const uint8_t *p)
{
    gl_do_glFrustum(gl_dl_get_d(p, 0), gl_dl_get_d(p, 1), gl_dl_get_d(p, 2), gl_dl_get_d(p, 3), gl_dl_get_d(p, 4), gl_dl_get_d(p, 5));
}

static void rec_glInitNames(GlContext *ctx)
{
    size_t at = 0 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 40, at);
    if (!p)
        return;
}

static void run_glInitNames(const uint8_t *p)
{
    gl_do_glInitNames();
}

static void rec_glLightModelf(GlContext *ctx, GLenum pname, GLfloat param)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 41, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    gl_dl_put_f(p, 1, param);
}

static void run_glLightModelf(const uint8_t *p)
{
    gl_do_glLightModelf((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_f(p, 1));
}

static void rec_glLightModelfv(GlContext *ctx, GLenum pname, const GLfloat *params)
{
    size_t at = 2 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_light_model_count(pname)), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 42, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 1, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glLightModelfv(const uint8_t *p)
{
    gl_do_glLightModelfv((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_i(p, 1) ? (const GLfloat *)(p + gl_dl_get_i(p, 1)) : nullptr);
}

static void rec_glLightModeli(GlContext *ctx, GLenum pname, GLint param)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 43, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)param);
}

static void run_glLightModeli(const uint8_t *p)
{
    gl_do_glLightModeli((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glLightModeliv(GlContext *ctx, GLenum pname, const GLint *params)
{
    size_t at = 2 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_light_model_count(pname)), sizeof(GLint)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 44, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 1, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glLightModeliv(const uint8_t *p)
{
    gl_do_glLightModeliv((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_i(p, 1) ? (const GLint *)(p + gl_dl_get_i(p, 1)) : nullptr);
}

static void rec_glLightf(GlContext *ctx, GLenum l, GLenum pname, GLfloat param)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 45, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    gl_dl_put_f(p, 2, param);
}

static void run_glLightf(const uint8_t *p)
{
    gl_do_glLightf((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_f(p, 2));
}

static void rec_glLightfv(GlContext *ctx, GLenum l, GLenum pname, const GLfloat *params)
{
    size_t at = 3 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_light_count(pname)), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 46, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 2, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glLightfv(const uint8_t *p)
{
    gl_do_glLightfv((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLfloat *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glLighti(GlContext *ctx, GLenum l, GLenum pname, GLint param)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 47, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)param);
}

static void run_glLighti(const uint8_t *p)
{
    gl_do_glLighti((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), (GLint)(int64_t)gl_dl_get_i(p, 2));
}

static void rec_glLightiv(GlContext *ctx, GLenum l, GLenum pname, const GLint *params)
{
    size_t at = 3 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_light_count(pname)), sizeof(GLint)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 48, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 2, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glLightiv(const uint8_t *p)
{
    gl_do_glLightiv((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glLineStipple(GlContext *ctx, GLint factor, GLushort pattern)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 49, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)factor);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pattern);
}

static void run_glLineStipple(const uint8_t *p)
{
    gl_do_glLineStipple((GLint)(int64_t)gl_dl_get_i(p, 0), (GLushort)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glLineWidth(GlContext *ctx, GLfloat width)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 50, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, width);
}

static void run_glLineWidth(const uint8_t *p)
{
    gl_do_glLineWidth(gl_dl_get_f(p, 0));
}

static void rec_glListBase(GlContext *ctx, GLuint base)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 51, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)base);
}

static void run_glListBase(const uint8_t *p)
{
    gl_do_glListBase((GLuint)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glLoadIdentity(GlContext *ctx)
{
    size_t at = 0 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 52, at);
    if (!p)
        return;
}

static void run_glLoadIdentity(const uint8_t *p)
{
    gl_do_glLoadIdentity();
}

static void rec_glLoadMatrixd(GlContext *ctx, const GLdouble *m)
{
    size_t at = 1 * 8;
    size_t at_m = at, bytes_m = m ? gl_dl_bytes((long long)(16), sizeof(GLdouble)) : 0;
    at += gl_dl_blob(bytes_m);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 53, at);
    if (!p)
        return;
    if (m) {
        gl_dl_put_i(p, 0, at_m);
        memcpy(p + at_m, m, bytes_m);
    }
}

static void run_glLoadMatrixd(const uint8_t *p)
{
    gl_do_glLoadMatrixd(gl_dl_get_i(p, 0) ? (const GLdouble *)(p + gl_dl_get_i(p, 0)) : nullptr);
}

static void rec_glLoadMatrixf(GlContext *ctx, const GLfloat *m)
{
    size_t at = 1 * 8;
    size_t at_m = at, bytes_m = m ? gl_dl_bytes((long long)(16), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_m);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 54, at);
    if (!p)
        return;
    if (m) {
        gl_dl_put_i(p, 0, at_m);
        memcpy(p + at_m, m, bytes_m);
    }
}

static void run_glLoadMatrixf(const uint8_t *p)
{
    gl_do_glLoadMatrixf(gl_dl_get_i(p, 0) ? (const GLfloat *)(p + gl_dl_get_i(p, 0)) : nullptr);
}

static void rec_glLoadName(GlContext *ctx, GLuint name)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 55, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)name);
}

static void run_glLoadName(const uint8_t *p)
{
    gl_do_glLoadName((GLuint)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glLoadTransposeMatrixd(GlContext *ctx, const GLdouble *m)
{
    size_t at = 1 * 8;
    size_t at_m = at, bytes_m = m ? gl_dl_bytes((long long)(16), sizeof(GLdouble)) : 0;
    at += gl_dl_blob(bytes_m);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 56, at);
    if (!p)
        return;
    if (m) {
        gl_dl_put_i(p, 0, at_m);
        memcpy(p + at_m, m, bytes_m);
    }
}

static void run_glLoadTransposeMatrixd(const uint8_t *p)
{
    gl_do_glLoadTransposeMatrixd(gl_dl_get_i(p, 0) ? (const GLdouble *)(p + gl_dl_get_i(p, 0)) : nullptr);
}

static void rec_glLoadTransposeMatrixdARB(GlContext *ctx, const GLdouble *m)
{
    size_t at = 1 * 8;
    size_t at_m = at, bytes_m = m ? gl_dl_bytes((long long)(16), sizeof(GLdouble)) : 0;
    at += gl_dl_blob(bytes_m);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 57, at);
    if (!p)
        return;
    if (m) {
        gl_dl_put_i(p, 0, at_m);
        memcpy(p + at_m, m, bytes_m);
    }
}

static void run_glLoadTransposeMatrixdARB(const uint8_t *p)
{
    gl_do_glLoadTransposeMatrixdARB(gl_dl_get_i(p, 0) ? (const GLdouble *)(p + gl_dl_get_i(p, 0)) : nullptr);
}

static void rec_glLoadTransposeMatrixf(GlContext *ctx, const GLfloat *m)
{
    size_t at = 1 * 8;
    size_t at_m = at, bytes_m = m ? gl_dl_bytes((long long)(16), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_m);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 58, at);
    if (!p)
        return;
    if (m) {
        gl_dl_put_i(p, 0, at_m);
        memcpy(p + at_m, m, bytes_m);
    }
}

static void run_glLoadTransposeMatrixf(const uint8_t *p)
{
    gl_do_glLoadTransposeMatrixf(gl_dl_get_i(p, 0) ? (const GLfloat *)(p + gl_dl_get_i(p, 0)) : nullptr);
}

static void rec_glLoadTransposeMatrixfARB(GlContext *ctx, const GLfloat *m)
{
    size_t at = 1 * 8;
    size_t at_m = at, bytes_m = m ? gl_dl_bytes((long long)(16), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_m);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 59, at);
    if (!p)
        return;
    if (m) {
        gl_dl_put_i(p, 0, at_m);
        memcpy(p + at_m, m, bytes_m);
    }
}

static void run_glLoadTransposeMatrixfARB(const uint8_t *p)
{
    gl_do_glLoadTransposeMatrixfARB(gl_dl_get_i(p, 0) ? (const GLfloat *)(p + gl_dl_get_i(p, 0)) : nullptr);
}

static void rec_glLogicOp(GlContext *ctx, GLenum opcode)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 60, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)opcode);
}

static void run_glLogicOp(const uint8_t *p)
{
    gl_do_glLogicOp((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glMaterialf(GlContext *ctx, GLenum face, GLenum pname, GLfloat param)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 61, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)face);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    gl_dl_put_f(p, 2, param);
}

static void run_glMaterialf(const uint8_t *p)
{
    gl_do_glMaterialf((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_f(p, 2));
}

static void rec_glMaterialfv(GlContext *ctx, GLenum face, GLenum pname, const GLfloat *params)
{
    size_t at = 3 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_material_count(pname)), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 62, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)face);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 2, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glMaterialfv(const uint8_t *p)
{
    gl_do_glMaterialfv((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLfloat *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glMateriali(GlContext *ctx, GLenum face, GLenum pname, GLint param)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 63, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)face);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)param);
}

static void run_glMateriali(const uint8_t *p)
{
    gl_do_glMateriali((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), (GLint)(int64_t)gl_dl_get_i(p, 2));
}

static void rec_glMaterialiv(GlContext *ctx, GLenum face, GLenum pname, const GLint *params)
{
    size_t at = 3 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_material_count(pname)), sizeof(GLint)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 64, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)face);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 2, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glMaterialiv(const uint8_t *p)
{
    gl_do_glMaterialiv((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glMatrixMode(GlContext *ctx, GLenum mode)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 65, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)mode);
}

static void run_glMatrixMode(const uint8_t *p)
{
    gl_do_glMatrixMode((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glMultMatrixd(GlContext *ctx, const GLdouble *m)
{
    size_t at = 1 * 8;
    size_t at_m = at, bytes_m = m ? gl_dl_bytes((long long)(16), sizeof(GLdouble)) : 0;
    at += gl_dl_blob(bytes_m);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 66, at);
    if (!p)
        return;
    if (m) {
        gl_dl_put_i(p, 0, at_m);
        memcpy(p + at_m, m, bytes_m);
    }
}

static void run_glMultMatrixd(const uint8_t *p)
{
    gl_do_glMultMatrixd(gl_dl_get_i(p, 0) ? (const GLdouble *)(p + gl_dl_get_i(p, 0)) : nullptr);
}

static void rec_glMultMatrixf(GlContext *ctx, const GLfloat *m)
{
    size_t at = 1 * 8;
    size_t at_m = at, bytes_m = m ? gl_dl_bytes((long long)(16), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_m);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 67, at);
    if (!p)
        return;
    if (m) {
        gl_dl_put_i(p, 0, at_m);
        memcpy(p + at_m, m, bytes_m);
    }
}

static void run_glMultMatrixf(const uint8_t *p)
{
    gl_do_glMultMatrixf(gl_dl_get_i(p, 0) ? (const GLfloat *)(p + gl_dl_get_i(p, 0)) : nullptr);
}

static void rec_glMultTransposeMatrixd(GlContext *ctx, const GLdouble *m)
{
    size_t at = 1 * 8;
    size_t at_m = at, bytes_m = m ? gl_dl_bytes((long long)(16), sizeof(GLdouble)) : 0;
    at += gl_dl_blob(bytes_m);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 68, at);
    if (!p)
        return;
    if (m) {
        gl_dl_put_i(p, 0, at_m);
        memcpy(p + at_m, m, bytes_m);
    }
}

static void run_glMultTransposeMatrixd(const uint8_t *p)
{
    gl_do_glMultTransposeMatrixd(gl_dl_get_i(p, 0) ? (const GLdouble *)(p + gl_dl_get_i(p, 0)) : nullptr);
}

static void rec_glMultTransposeMatrixdARB(GlContext *ctx, const GLdouble *m)
{
    size_t at = 1 * 8;
    size_t at_m = at, bytes_m = m ? gl_dl_bytes((long long)(16), sizeof(GLdouble)) : 0;
    at += gl_dl_blob(bytes_m);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 69, at);
    if (!p)
        return;
    if (m) {
        gl_dl_put_i(p, 0, at_m);
        memcpy(p + at_m, m, bytes_m);
    }
}

static void run_glMultTransposeMatrixdARB(const uint8_t *p)
{
    gl_do_glMultTransposeMatrixdARB(gl_dl_get_i(p, 0) ? (const GLdouble *)(p + gl_dl_get_i(p, 0)) : nullptr);
}

static void rec_glMultTransposeMatrixf(GlContext *ctx, const GLfloat *m)
{
    size_t at = 1 * 8;
    size_t at_m = at, bytes_m = m ? gl_dl_bytes((long long)(16), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_m);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 70, at);
    if (!p)
        return;
    if (m) {
        gl_dl_put_i(p, 0, at_m);
        memcpy(p + at_m, m, bytes_m);
    }
}

static void run_glMultTransposeMatrixf(const uint8_t *p)
{
    gl_do_glMultTransposeMatrixf(gl_dl_get_i(p, 0) ? (const GLfloat *)(p + gl_dl_get_i(p, 0)) : nullptr);
}

static void rec_glMultTransposeMatrixfARB(GlContext *ctx, const GLfloat *m)
{
    size_t at = 1 * 8;
    size_t at_m = at, bytes_m = m ? gl_dl_bytes((long long)(16), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_m);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 71, at);
    if (!p)
        return;
    if (m) {
        gl_dl_put_i(p, 0, at_m);
        memcpy(p + at_m, m, bytes_m);
    }
}

static void run_glMultTransposeMatrixfARB(const uint8_t *p)
{
    gl_do_glMultTransposeMatrixfARB(gl_dl_get_i(p, 0) ? (const GLfloat *)(p + gl_dl_get_i(p, 0)) : nullptr);
}

static void rec_glOrtho(GlContext *ctx, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top_, GLdouble near_val,                             GLdouble far_val)
{
    size_t at = 6 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 72, at);
    if (!p)
        return;
    gl_dl_put_d(p, 0, left);
    gl_dl_put_d(p, 1, right);
    gl_dl_put_d(p, 2, bottom);
    gl_dl_put_d(p, 3, top_);
    gl_dl_put_d(p, 4, near_val);
    gl_dl_put_d(p, 5, far_val);
}

static void run_glOrtho(const uint8_t *p)
{
    gl_do_glOrtho(gl_dl_get_d(p, 0), gl_dl_get_d(p, 1), gl_dl_get_d(p, 2), gl_dl_get_d(p, 3), gl_dl_get_d(p, 4), gl_dl_get_d(p, 5));
}

static void rec_glPassThrough(GlContext *ctx, GLfloat token)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 73, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, token);
}

static void run_glPassThrough(const uint8_t *p)
{
    gl_do_glPassThrough(gl_dl_get_f(p, 0));
}

static void rec_glPixelTransferf(GlContext *ctx, GLenum pname, GLfloat param)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 74, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    gl_dl_put_f(p, 1, param);
}

static void run_glPixelTransferf(const uint8_t *p)
{
    gl_do_glPixelTransferf((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_f(p, 1));
}

static void rec_glPixelTransferi(GlContext *ctx, GLenum pname, GLint param)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 75, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)param);
}

static void run_glPixelTransferi(const uint8_t *p)
{
    gl_do_glPixelTransferi((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glPixelZoom(GlContext *ctx, GLfloat xfactor, GLfloat yfactor)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 76, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, xfactor);
    gl_dl_put_f(p, 1, yfactor);
}

static void run_glPixelZoom(const uint8_t *p)
{
    gl_do_glPixelZoom(gl_dl_get_f(p, 0), gl_dl_get_f(p, 1));
}

static void rec_glPointParameterf(GlContext *ctx, GLenum pname, GLfloat value)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 77, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    gl_dl_put_f(p, 1, value);
}

static void run_glPointParameterf(const uint8_t *p)
{
    gl_do_glPointParameterf((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_f(p, 1));
}

static void rec_glPointParameterfARB(GlContext *ctx, GLenum pname, GLfloat value)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 78, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    gl_dl_put_f(p, 1, value);
}

static void run_glPointParameterfARB(const uint8_t *p)
{
    gl_do_glPointParameterfARB((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_f(p, 1));
}

static void rec_glPointParameterfEXT(GlContext *ctx, GLenum pname, GLfloat value)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 79, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    gl_dl_put_f(p, 1, value);
}

static void run_glPointParameterfEXT(const uint8_t *p)
{
    gl_do_glPointParameterfEXT((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_f(p, 1));
}

static void rec_glPointParameterfv(GlContext *ctx, GLenum pname, const GLfloat *values)
{
    size_t at = 2 * 8;
    size_t at_values = at, bytes_values = values ? gl_dl_bytes((long long)(gl_dl_point_parameter_count(pname)), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_values);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 80, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    if (values) {
        gl_dl_put_i(p, 1, at_values);
        memcpy(p + at_values, values, bytes_values);
    }
}

static void run_glPointParameterfv(const uint8_t *p)
{
    gl_do_glPointParameterfv((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_i(p, 1) ? (const GLfloat *)(p + gl_dl_get_i(p, 1)) : nullptr);
}

static void rec_glPointParameterfvARB(GlContext *ctx, GLenum pname, const GLfloat *values)
{
    size_t at = 2 * 8;
    size_t at_values = at, bytes_values = values ? gl_dl_bytes((long long)(gl_dl_point_parameter_count(pname)), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_values);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 81, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    if (values) {
        gl_dl_put_i(p, 1, at_values);
        memcpy(p + at_values, values, bytes_values);
    }
}

static void run_glPointParameterfvARB(const uint8_t *p)
{
    gl_do_glPointParameterfvARB((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_i(p, 1) ? (const GLfloat *)(p + gl_dl_get_i(p, 1)) : nullptr);
}

static void rec_glPointParameterfvEXT(GlContext *ctx, GLenum pname, const GLfloat *values)
{
    size_t at = 2 * 8;
    size_t at_values = at, bytes_values = values ? gl_dl_bytes((long long)(gl_dl_point_parameter_count(pname)), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_values);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 82, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    if (values) {
        gl_dl_put_i(p, 1, at_values);
        memcpy(p + at_values, values, bytes_values);
    }
}

static void run_glPointParameterfvEXT(const uint8_t *p)
{
    gl_do_glPointParameterfvEXT((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_i(p, 1) ? (const GLfloat *)(p + gl_dl_get_i(p, 1)) : nullptr);
}

static void rec_glPointParameteri(GlContext *ctx, GLenum pname, GLint value)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 83, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)value);
}

static void run_glPointParameteri(const uint8_t *p)
{
    gl_do_glPointParameteri((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glPointParameteriv(GlContext *ctx, GLenum pname, const GLint *values)
{
    size_t at = 2 * 8;
    size_t at_values = at, bytes_values = values ? gl_dl_bytes((long long)(gl_dl_point_parameter_count(pname)), sizeof(GLint)) : 0;
    at += gl_dl_blob(bytes_values);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 84, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)pname);
    if (values) {
        gl_dl_put_i(p, 1, at_values);
        memcpy(p + at_values, values, bytes_values);
    }
}

static void run_glPointParameteriv(const uint8_t *p)
{
    gl_do_glPointParameteriv((GLenum)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_i(p, 1) ? (const GLint *)(p + gl_dl_get_i(p, 1)) : nullptr);
}

static void rec_glPointSize(GlContext *ctx, GLfloat size)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 85, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, size);
}

static void run_glPointSize(const uint8_t *p)
{
    gl_do_glPointSize(gl_dl_get_f(p, 0));
}

static void rec_glPolygonMode(GlContext *ctx, GLenum face, GLenum mode)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 86, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)face);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)mode);
}

static void run_glPolygonMode(const uint8_t *p)
{
    gl_do_glPolygonMode((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glPolygonOffset(GlContext *ctx, GLfloat factor, GLfloat units)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 87, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, factor);
    gl_dl_put_f(p, 1, units);
}

static void run_glPolygonOffset(const uint8_t *p)
{
    gl_do_glPolygonOffset(gl_dl_get_f(p, 0), gl_dl_get_f(p, 1));
}

static void rec_glPolygonOffsetClamp(GlContext *ctx, GLfloat factor, GLfloat units, GLfloat clamp)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 88, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, factor);
    gl_dl_put_f(p, 1, units);
    gl_dl_put_f(p, 2, clamp);
}

static void run_glPolygonOffsetClamp(const uint8_t *p)
{
    gl_do_glPolygonOffsetClamp(gl_dl_get_f(p, 0), gl_dl_get_f(p, 1), gl_dl_get_f(p, 2));
}

static void rec_glPopMatrix(GlContext *ctx)
{
    size_t at = 0 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 89, at);
    if (!p)
        return;
}

static void run_glPopMatrix(const uint8_t *p)
{
    gl_do_glPopMatrix();
}

static void rec_glPopName(GlContext *ctx)
{
    size_t at = 0 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 90, at);
    if (!p)
        return;
}

static void run_glPopName(const uint8_t *p)
{
    gl_do_glPopName();
}

static void rec_glPrioritizeTextures(GlContext *ctx, GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
    size_t at = 3 * 8;
    size_t at_textures = at, bytes_textures = textures ? gl_dl_bytes((long long)(n), sizeof(GLuint)) : 0;
    at += gl_dl_blob(bytes_textures);
    size_t at_priorities = at, bytes_priorities = priorities ? gl_dl_bytes((long long)(n), sizeof(GLclampf)) : 0;
    at += gl_dl_blob(bytes_priorities);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 91, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)n);
    if (textures) {
        gl_dl_put_i(p, 1, at_textures);
        memcpy(p + at_textures, textures, bytes_textures);
    }
    if (priorities) {
        gl_dl_put_i(p, 2, at_priorities);
        memcpy(p + at_priorities, priorities, bytes_priorities);
    }
}

static void run_glPrioritizeTextures(const uint8_t *p)
{
    gl_do_glPrioritizeTextures((GLsizei)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_i(p, 1) ? (const GLuint *)(p + gl_dl_get_i(p, 1)) : nullptr, gl_dl_get_i(p, 2) ? (const GLclampf *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glPushAttrib(GlContext *ctx, GLbitfield mask)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 92, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)mask);
}

static void run_glPushAttrib(const uint8_t *p)
{
    gl_do_glPushAttrib((GLbitfield)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glPushMatrix(GlContext *ctx)
{
    size_t at = 0 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 93, at);
    if (!p)
        return;
}

static void run_glPushMatrix(const uint8_t *p)
{
    gl_do_glPushMatrix();
}

static void rec_glPushName(GlContext *ctx, GLuint name)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 94, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)name);
}

static void run_glPushName(const uint8_t *p)
{
    gl_do_glPushName((GLuint)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glReadBuffer(GlContext *ctx, GLenum buf)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 95, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)buf);
}

static void run_glReadBuffer(const uint8_t *p)
{
    gl_do_glReadBuffer((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glRotated(GlContext *ctx, GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 96, at);
    if (!p)
        return;
    gl_dl_put_d(p, 0, angle);
    gl_dl_put_d(p, 1, x);
    gl_dl_put_d(p, 2, y);
    gl_dl_put_d(p, 3, z);
}

static void run_glRotated(const uint8_t *p)
{
    gl_do_glRotated(gl_dl_get_d(p, 0), gl_dl_get_d(p, 1), gl_dl_get_d(p, 2), gl_dl_get_d(p, 3));
}

static void rec_glRotatef(GlContext *ctx, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 97, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, angle);
    gl_dl_put_f(p, 1, x);
    gl_dl_put_f(p, 2, y);
    gl_dl_put_f(p, 3, z);
}

static void run_glRotatef(const uint8_t *p)
{
    gl_do_glRotatef(gl_dl_get_f(p, 0), gl_dl_get_f(p, 1), gl_dl_get_f(p, 2), gl_dl_get_f(p, 3));
}

static void rec_glScaled(GlContext *ctx, GLdouble x, GLdouble y, GLdouble z)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 98, at);
    if (!p)
        return;
    gl_dl_put_d(p, 0, x);
    gl_dl_put_d(p, 1, y);
    gl_dl_put_d(p, 2, z);
}

static void run_glScaled(const uint8_t *p)
{
    gl_do_glScaled(gl_dl_get_d(p, 0), gl_dl_get_d(p, 1), gl_dl_get_d(p, 2));
}

static void rec_glScalef(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 99, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, x);
    gl_dl_put_f(p, 1, y);
    gl_dl_put_f(p, 2, z);
}

static void run_glScalef(const uint8_t *p)
{
    gl_do_glScalef(gl_dl_get_f(p, 0), gl_dl_get_f(p, 1), gl_dl_get_f(p, 2));
}

static void rec_glScissor(GlContext *ctx, GLint x, GLint y, GLsizei w, GLsizei h)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 100, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)x);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)y);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)w);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)h);
}

static void run_glScissor(const uint8_t *p)
{
    gl_do_glScissor((GLint)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1), (GLsizei)(int64_t)gl_dl_get_i(p, 2), (GLsizei)(int64_t)gl_dl_get_i(p, 3));
}

static void rec_glShadeModel(GlContext *ctx, GLenum mode)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 101, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)mode);
}

static void run_glShadeModel(const uint8_t *p)
{
    gl_do_glShadeModel((GLenum)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glStencilFunc(GlContext *ctx, GLenum func, GLint ref, GLuint mask)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 102, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)func);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)ref);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)mask);
}

static void run_glStencilFunc(const uint8_t *p)
{
    gl_do_glStencilFunc((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1), (GLuint)(int64_t)gl_dl_get_i(p, 2));
}

static void rec_glStencilFuncSeparate(GlContext *ctx, GLenum face, GLenum func, GLint ref, GLuint mask)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 103, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)face);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)func);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)ref);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)mask);
}

static void run_glStencilFuncSeparate(const uint8_t *p)
{
    gl_do_glStencilFuncSeparate((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), (GLint)(int64_t)gl_dl_get_i(p, 2), (GLuint)(int64_t)gl_dl_get_i(p, 3));
}

static void rec_glStencilMask(GlContext *ctx, GLuint mask)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 104, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)mask);
}

static void run_glStencilMask(const uint8_t *p)
{
    gl_do_glStencilMask((GLuint)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glStencilMaskSeparate(GlContext *ctx, GLenum face, GLuint mask)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 105, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)face);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)mask);
}

static void run_glStencilMaskSeparate(const uint8_t *p)
{
    gl_do_glStencilMaskSeparate((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLuint)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glStencilOp(GlContext *ctx, GLenum fail, GLenum zfail, GLenum zpass)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 106, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)fail);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)zfail);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)zpass);
}

static void run_glStencilOp(const uint8_t *p)
{
    gl_do_glStencilOp((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), (GLenum)(int64_t)gl_dl_get_i(p, 2));
}

static void rec_glStencilOpSeparate(GlContext *ctx, GLenum face, GLenum fail, GLenum zfail, GLenum zpass)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 107, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)face);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)fail);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)zfail);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)zpass);
}

static void run_glStencilOpSeparate(const uint8_t *p)
{
    gl_do_glStencilOpSeparate((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), (GLenum)(int64_t)gl_dl_get_i(p, 2), (GLenum)(int64_t)gl_dl_get_i(p, 3));
}

static void rec_glTexEnvf(GlContext *ctx, GLenum target, GLenum pname, GLfloat param)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 108, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    gl_dl_put_f(p, 2, param);
}

static void run_glTexEnvf(const uint8_t *p)
{
    gl_do_glTexEnvf((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_f(p, 2));
}

static void rec_glTexEnvfv(GlContext *ctx, GLenum target, GLenum pname, const GLfloat *params)
{
    size_t at = 3 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_tex_env_count(pname)), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 109, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 2, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glTexEnvfv(const uint8_t *p)
{
    gl_do_glTexEnvfv((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLfloat *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glTexEnvi(GlContext *ctx, GLenum target, GLenum pname, GLint param)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 110, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)param);
}

static void run_glTexEnvi(const uint8_t *p)
{
    gl_do_glTexEnvi((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), (GLint)(int64_t)gl_dl_get_i(p, 2));
}

static void rec_glTexEnviv(GlContext *ctx, GLenum target, GLenum pname, const GLint *params)
{
    size_t at = 3 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_tex_env_count(pname)), sizeof(GLint)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 111, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 2, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glTexEnviv(const uint8_t *p)
{
    gl_do_glTexEnviv((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glTexGend(GlContext *ctx, GLenum coord, GLenum pname, GLdouble param)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 112, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)coord);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    gl_dl_put_d(p, 2, param);
}

static void run_glTexGend(const uint8_t *p)
{
    gl_do_glTexGend((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_d(p, 2));
}

static void rec_glTexGendv(GlContext *ctx, GLenum coord, GLenum pname, const GLdouble *params)
{
    size_t at = 3 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_tex_gen_count(pname)), sizeof(GLdouble)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 113, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)coord);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 2, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glTexGendv(const uint8_t *p)
{
    gl_do_glTexGendv((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLdouble *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glTexGenf(GlContext *ctx, GLenum coord, GLenum pname, GLfloat param)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 114, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)coord);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    gl_dl_put_f(p, 2, param);
}

static void run_glTexGenf(const uint8_t *p)
{
    gl_do_glTexGenf((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_f(p, 2));
}

static void rec_glTexGenfv(GlContext *ctx, GLenum coord, GLenum pname, const GLfloat *params)
{
    size_t at = 3 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_tex_gen_count(pname)), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 115, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)coord);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 2, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glTexGenfv(const uint8_t *p)
{
    gl_do_glTexGenfv((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLfloat *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glTexGeni(GlContext *ctx, GLenum coord, GLenum pname, GLint param)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 116, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)coord);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)param);
}

static void run_glTexGeni(const uint8_t *p)
{
    gl_do_glTexGeni((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), (GLint)(int64_t)gl_dl_get_i(p, 2));
}

static void rec_glTexGeniv(GlContext *ctx, GLenum coord, GLenum pname, const GLint *params)
{
    size_t at = 3 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_tex_gen_count(pname)), sizeof(GLint)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 117, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)coord);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 2, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glTexGeniv(const uint8_t *p)
{
    gl_do_glTexGeniv((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glTexParameterf(GlContext *ctx, GLenum target, GLenum pname, GLfloat param)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 118, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    gl_dl_put_f(p, 2, param);
}

static void run_glTexParameterf(const uint8_t *p)
{
    gl_do_glTexParameterf((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_f(p, 2));
}

static void rec_glTexParameterfv(GlContext *ctx, GLenum target, GLenum pname, const GLfloat *params)
{
    size_t at = 3 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_tex_parameter_count(pname)), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 119, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 2, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glTexParameterfv(const uint8_t *p)
{
    gl_do_glTexParameterfv((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLfloat *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glTexParameteri(GlContext *ctx, GLenum target, GLenum pname, GLint param)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 120, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)param);
}

static void run_glTexParameteri(const uint8_t *p)
{
    gl_do_glTexParameteri((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), (GLint)(int64_t)gl_dl_get_i(p, 2));
}

static void rec_glTexParameteriv(GlContext *ctx, GLenum target, GLenum pname, const GLint *params)
{
    size_t at = 3 * 8;
    size_t at_params = at, bytes_params = params ? gl_dl_bytes((long long)(gl_dl_tex_parameter_count(pname)), sizeof(GLint)) : 0;
    at += gl_dl_blob(bytes_params);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 121, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)target);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)pname);
    if (params) {
        gl_dl_put_i(p, 2, at_params);
        memcpy(p + at_params, params, bytes_params);
    }
}

static void run_glTexParameteriv(const uint8_t *p)
{
    gl_do_glTexParameteriv((GLenum)(int64_t)gl_dl_get_i(p, 0), (GLenum)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glTranslated(GlContext *ctx, GLdouble x, GLdouble y, GLdouble z)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 122, at);
    if (!p)
        return;
    gl_dl_put_d(p, 0, x);
    gl_dl_put_d(p, 1, y);
    gl_dl_put_d(p, 2, z);
}

static void run_glTranslated(const uint8_t *p)
{
    gl_do_glTranslated(gl_dl_get_d(p, 0), gl_dl_get_d(p, 1), gl_dl_get_d(p, 2));
}

static void rec_glTranslatef(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 123, at);
    if (!p)
        return;
    gl_dl_put_f(p, 0, x);
    gl_dl_put_f(p, 1, y);
    gl_dl_put_f(p, 2, z);
}

static void run_glTranslatef(const uint8_t *p)
{
    gl_do_glTranslatef(gl_dl_get_f(p, 0), gl_dl_get_f(p, 1), gl_dl_get_f(p, 2));
}

static void rec_glUniform1f(GlContext *ctx, GLint l, GLfloat a)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 124, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_f(p, 1, a);
}

static void run_glUniform1f(const uint8_t *p)
{
    gl_do_glUniform1f((GLint)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_f(p, 1));
}

static void rec_glUniform1fv(GlContext *ctx, GLint l, GLsizei n, const GLfloat *v)
{
    size_t at = 3 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 125, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    if (v) {
        gl_dl_put_i(p, 2, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniform1fv(const uint8_t *p)
{
    gl_do_glUniform1fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLfloat *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glUniform1i(GlContext *ctx, GLint l, GLint a)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 126, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)a);
}

static void run_glUniform1i(const uint8_t *p)
{
    gl_do_glUniform1i((GLint)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glUniform1iv(GlContext *ctx, GLint l, GLsizei n, const GLint *v)
{
    size_t at = 3 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n), sizeof(GLint)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 127, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    if (v) {
        gl_dl_put_i(p, 2, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniform1iv(const uint8_t *p)
{
    gl_do_glUniform1iv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glUniform1ui(GlContext *ctx, GLint l, GLuint a)
{
    size_t at = 2 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 128, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)a);
}

static void run_glUniform1ui(const uint8_t *p)
{
    gl_do_glUniform1ui((GLint)(int64_t)gl_dl_get_i(p, 0), (GLuint)(int64_t)gl_dl_get_i(p, 1));
}

static void rec_glUniform1uiv(GlContext *ctx, GLint l, GLsizei n, const GLuint *v)
{
    size_t at = 3 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n), sizeof(GLuint)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 129, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    if (v) {
        gl_dl_put_i(p, 2, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniform1uiv(const uint8_t *p)
{
    gl_do_glUniform1uiv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLuint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glUniform2f(GlContext *ctx, GLint l, GLfloat a, GLfloat b)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 130, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_f(p, 1, a);
    gl_dl_put_f(p, 2, b);
}

static void run_glUniform2f(const uint8_t *p)
{
    gl_do_glUniform2f((GLint)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_f(p, 1), gl_dl_get_f(p, 2));
}

static void rec_glUniform2fv(GlContext *ctx, GLint l, GLsizei n, const GLfloat *v)
{
    size_t at = 3 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*2), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 131, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    if (v) {
        gl_dl_put_i(p, 2, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniform2fv(const uint8_t *p)
{
    gl_do_glUniform2fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLfloat *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glUniform2i(GlContext *ctx, GLint l, GLint a, GLint b)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 132, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)a);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)b);
}

static void run_glUniform2i(const uint8_t *p)
{
    gl_do_glUniform2i((GLint)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1), (GLint)(int64_t)gl_dl_get_i(p, 2));
}

static void rec_glUniform2iv(GlContext *ctx, GLint l, GLsizei n, const GLint *v)
{
    size_t at = 3 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*2), sizeof(GLint)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 133, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    if (v) {
        gl_dl_put_i(p, 2, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniform2iv(const uint8_t *p)
{
    gl_do_glUniform2iv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glUniform2ui(GlContext *ctx, GLint l, GLuint a, GLuint b)
{
    size_t at = 3 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 134, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)a);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)b);
}

static void run_glUniform2ui(const uint8_t *p)
{
    gl_do_glUniform2ui((GLint)(int64_t)gl_dl_get_i(p, 0), (GLuint)(int64_t)gl_dl_get_i(p, 1), (GLuint)(int64_t)gl_dl_get_i(p, 2));
}

static void rec_glUniform2uiv(GlContext *ctx, GLint l, GLsizei n, const GLuint *v)
{
    size_t at = 3 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*2), sizeof(GLuint)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 135, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    if (v) {
        gl_dl_put_i(p, 2, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniform2uiv(const uint8_t *p)
{
    gl_do_glUniform2uiv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLuint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glUniform3f(GlContext *ctx, GLint l, GLfloat a, GLfloat b, GLfloat c)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 136, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_f(p, 1, a);
    gl_dl_put_f(p, 2, b);
    gl_dl_put_f(p, 3, c);
}

static void run_glUniform3f(const uint8_t *p)
{
    gl_do_glUniform3f((GLint)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_f(p, 1), gl_dl_get_f(p, 2), gl_dl_get_f(p, 3));
}

static void rec_glUniform3fv(GlContext *ctx, GLint l, GLsizei n, const GLfloat *v)
{
    size_t at = 3 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*3), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 137, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    if (v) {
        gl_dl_put_i(p, 2, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniform3fv(const uint8_t *p)
{
    gl_do_glUniform3fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLfloat *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glUniform3i(GlContext *ctx, GLint l, GLint a, GLint b, GLint c)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 138, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)a);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)b);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)c);
}

static void run_glUniform3i(const uint8_t *p)
{
    gl_do_glUniform3i((GLint)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1), (GLint)(int64_t)gl_dl_get_i(p, 2), (GLint)(int64_t)gl_dl_get_i(p, 3));
}

static void rec_glUniform3iv(GlContext *ctx, GLint l, GLsizei n, const GLint *v)
{
    size_t at = 3 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*3), sizeof(GLint)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 139, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    if (v) {
        gl_dl_put_i(p, 2, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniform3iv(const uint8_t *p)
{
    gl_do_glUniform3iv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glUniform3ui(GlContext *ctx, GLint l, GLuint a, GLuint b, GLuint c)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 140, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)a);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)b);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)c);
}

static void run_glUniform3ui(const uint8_t *p)
{
    gl_do_glUniform3ui((GLint)(int64_t)gl_dl_get_i(p, 0), (GLuint)(int64_t)gl_dl_get_i(p, 1), (GLuint)(int64_t)gl_dl_get_i(p, 2), (GLuint)(int64_t)gl_dl_get_i(p, 3));
}

static void rec_glUniform3uiv(GlContext *ctx, GLint l, GLsizei n, const GLuint *v)
{
    size_t at = 3 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*3), sizeof(GLuint)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 141, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    if (v) {
        gl_dl_put_i(p, 2, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniform3uiv(const uint8_t *p)
{
    gl_do_glUniform3uiv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLuint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glUniform4f(GlContext *ctx, GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d)
{
    size_t at = 5 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 142, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_f(p, 1, a);
    gl_dl_put_f(p, 2, b);
    gl_dl_put_f(p, 3, c);
    gl_dl_put_f(p, 4, d);
}

static void run_glUniform4f(const uint8_t *p)
{
    gl_do_glUniform4f((GLint)(int64_t)gl_dl_get_i(p, 0), gl_dl_get_f(p, 1), gl_dl_get_f(p, 2), gl_dl_get_f(p, 3), gl_dl_get_f(p, 4));
}

static void rec_glUniform4fv(GlContext *ctx, GLint l, GLsizei n, const GLfloat *v)
{
    size_t at = 3 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*4), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 143, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    if (v) {
        gl_dl_put_i(p, 2, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniform4fv(const uint8_t *p)
{
    gl_do_glUniform4fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLfloat *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glUniform4i(GlContext *ctx, GLint l, GLint a, GLint b, GLint c, GLint d)
{
    size_t at = 5 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 144, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)a);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)b);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)c);
    gl_dl_put_i(p, 4, (uint64_t)(int64_t)d);
}

static void run_glUniform4i(const uint8_t *p)
{
    gl_do_glUniform4i((GLint)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1), (GLint)(int64_t)gl_dl_get_i(p, 2), (GLint)(int64_t)gl_dl_get_i(p, 3), (GLint)(int64_t)gl_dl_get_i(p, 4));
}

static void rec_glUniform4iv(GlContext *ctx, GLint l, GLsizei n, const GLint *v)
{
    size_t at = 3 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*4), sizeof(GLint)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 145, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    if (v) {
        gl_dl_put_i(p, 2, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniform4iv(const uint8_t *p)
{
    gl_do_glUniform4iv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glUniform4ui(GlContext *ctx, GLint l, GLuint a, GLuint b, GLuint c, GLuint d)
{
    size_t at = 5 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 146, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)a);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)b);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)c);
    gl_dl_put_i(p, 4, (uint64_t)(int64_t)d);
}

static void run_glUniform4ui(const uint8_t *p)
{
    gl_do_glUniform4ui((GLint)(int64_t)gl_dl_get_i(p, 0), (GLuint)(int64_t)gl_dl_get_i(p, 1), (GLuint)(int64_t)gl_dl_get_i(p, 2), (GLuint)(int64_t)gl_dl_get_i(p, 3), (GLuint)(int64_t)gl_dl_get_i(p, 4));
}

static void rec_glUniform4uiv(GlContext *ctx, GLint l, GLsizei n, const GLuint *v)
{
    size_t at = 3 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*4), sizeof(GLuint)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 147, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    if (v) {
        gl_dl_put_i(p, 2, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniform4uiv(const uint8_t *p)
{
    gl_do_glUniform4uiv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), gl_dl_get_i(p, 2) ? (const GLuint *)(p + gl_dl_get_i(p, 2)) : nullptr);
}

static void rec_glUniformMatrix2fv(GlContext *ctx, GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    size_t at = 4 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*4), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 148, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)t);
    if (v) {
        gl_dl_put_i(p, 3, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniformMatrix2fv(const uint8_t *p)
{
    gl_do_glUniformMatrix2fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), (GLboolean)(int64_t)gl_dl_get_i(p, 2), gl_dl_get_i(p, 3) ? (const GLfloat *)(p + gl_dl_get_i(p, 3)) : nullptr);
}

static void rec_glUniformMatrix2x3fv(GlContext *ctx, GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    size_t at = 4 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*6), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 149, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)t);
    if (v) {
        gl_dl_put_i(p, 3, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniformMatrix2x3fv(const uint8_t *p)
{
    gl_do_glUniformMatrix2x3fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), (GLboolean)(int64_t)gl_dl_get_i(p, 2), gl_dl_get_i(p, 3) ? (const GLfloat *)(p + gl_dl_get_i(p, 3)) : nullptr);
}

static void rec_glUniformMatrix2x4fv(GlContext *ctx, GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    size_t at = 4 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*8), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 150, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)t);
    if (v) {
        gl_dl_put_i(p, 3, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniformMatrix2x4fv(const uint8_t *p)
{
    gl_do_glUniformMatrix2x4fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), (GLboolean)(int64_t)gl_dl_get_i(p, 2), gl_dl_get_i(p, 3) ? (const GLfloat *)(p + gl_dl_get_i(p, 3)) : nullptr);
}

static void rec_glUniformMatrix3fv(GlContext *ctx, GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    size_t at = 4 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*9), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 151, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)t);
    if (v) {
        gl_dl_put_i(p, 3, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniformMatrix3fv(const uint8_t *p)
{
    gl_do_glUniformMatrix3fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), (GLboolean)(int64_t)gl_dl_get_i(p, 2), gl_dl_get_i(p, 3) ? (const GLfloat *)(p + gl_dl_get_i(p, 3)) : nullptr);
}

static void rec_glUniformMatrix3x2fv(GlContext *ctx, GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    size_t at = 4 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*6), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 152, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)t);
    if (v) {
        gl_dl_put_i(p, 3, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniformMatrix3x2fv(const uint8_t *p)
{
    gl_do_glUniformMatrix3x2fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), (GLboolean)(int64_t)gl_dl_get_i(p, 2), gl_dl_get_i(p, 3) ? (const GLfloat *)(p + gl_dl_get_i(p, 3)) : nullptr);
}

static void rec_glUniformMatrix3x4fv(GlContext *ctx, GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    size_t at = 4 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*12), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 153, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)t);
    if (v) {
        gl_dl_put_i(p, 3, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniformMatrix3x4fv(const uint8_t *p)
{
    gl_do_glUniformMatrix3x4fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), (GLboolean)(int64_t)gl_dl_get_i(p, 2), gl_dl_get_i(p, 3) ? (const GLfloat *)(p + gl_dl_get_i(p, 3)) : nullptr);
}

static void rec_glUniformMatrix4fv(GlContext *ctx, GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    size_t at = 4 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*16), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 154, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)t);
    if (v) {
        gl_dl_put_i(p, 3, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniformMatrix4fv(const uint8_t *p)
{
    gl_do_glUniformMatrix4fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), (GLboolean)(int64_t)gl_dl_get_i(p, 2), gl_dl_get_i(p, 3) ? (const GLfloat *)(p + gl_dl_get_i(p, 3)) : nullptr);
}

static void rec_glUniformMatrix4x2fv(GlContext *ctx, GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    size_t at = 4 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*8), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 155, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)t);
    if (v) {
        gl_dl_put_i(p, 3, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniformMatrix4x2fv(const uint8_t *p)
{
    gl_do_glUniformMatrix4x2fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), (GLboolean)(int64_t)gl_dl_get_i(p, 2), gl_dl_get_i(p, 3) ? (const GLfloat *)(p + gl_dl_get_i(p, 3)) : nullptr);
}

static void rec_glUniformMatrix4x3fv(GlContext *ctx, GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    size_t at = 4 * 8;
    size_t at_v = at, bytes_v = v ? gl_dl_bytes((long long)(n*12), sizeof(GLfloat)) : 0;
    at += gl_dl_blob(bytes_v);
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 156, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)l);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)n);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)t);
    if (v) {
        gl_dl_put_i(p, 3, at_v);
        memcpy(p + at_v, v, bytes_v);
    }
}

static void run_glUniformMatrix4x3fv(const uint8_t *p)
{
    gl_do_glUniformMatrix4x3fv((GLint)(int64_t)gl_dl_get_i(p, 0), (GLsizei)(int64_t)gl_dl_get_i(p, 1), (GLboolean)(int64_t)gl_dl_get_i(p, 2), gl_dl_get_i(p, 3) ? (const GLfloat *)(p + gl_dl_get_i(p, 3)) : nullptr);
}

static void rec_glUseProgram(GlContext *ctx, GLuint name)
{
    size_t at = 1 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 157, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)name);
}

static void run_glUseProgram(const uint8_t *p)
{
    gl_do_glUseProgram((GLuint)(int64_t)gl_dl_get_i(p, 0));
}

static void rec_glViewport(GlContext *ctx, GLint x, GLint y, GLsizei w, GLsizei h)
{
    size_t at = 4 * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_FIRST_GENERATED + 158, at);
    if (!p)
        return;
    gl_dl_put_i(p, 0, (uint64_t)(int64_t)x);
    gl_dl_put_i(p, 1, (uint64_t)(int64_t)y);
    gl_dl_put_i(p, 2, (uint64_t)(int64_t)w);
    gl_dl_put_i(p, 3, (uint64_t)(int64_t)h);
}

static void run_glViewport(const uint8_t *p)
{
    gl_do_glViewport((GLint)(int64_t)gl_dl_get_i(p, 0), (GLint)(int64_t)gl_dl_get_i(p, 1), (GLsizei)(int64_t)gl_dl_get_i(p, 2), (GLsizei)(int64_t)gl_dl_get_i(p, 3));
}

const GlDlRun gl_dl_run[] = {
    run_glActiveTexture,
    run_glAlphaFunc,
    run_glBeginQuery,
    run_glBindTexture,
    run_glBlendColor,
    run_glBlendEquation,
    run_glBlendEquationSeparate,
    run_glBlendFunc,
    run_glBlendFuncSeparate,
    run_glClear,
    run_glClearAccum,
    run_glClearColor,
    run_glClearDepth,
    run_glClearDepthf,
    run_glClearIndex,
    run_glClearStencil,
    run_glClipPlane,
    run_glColorMask,
    run_glColorMaterial,
    run_glCopyPixels,
    run_glCopyTexImage1D,
    run_glCopyTexImage2D,
    run_glCopyTexSubImage1D,
    run_glCopyTexSubImage2D,
    run_glCullFace,
    run_glDepthFunc,
    run_glDepthMask,
    run_glDepthRange,
    run_glDepthRangef,
    run_glDisable,
    run_glDrawBuffer,
    run_glDrawBuffers,
    run_glEnable,
    run_glEndQuery,
    run_glFogf,
    run_glFogfv,
    run_glFogi,
    run_glFogiv,
    run_glFrontFace,
    run_glFrustum,
    run_glInitNames,
    run_glLightModelf,
    run_glLightModelfv,
    run_glLightModeli,
    run_glLightModeliv,
    run_glLightf,
    run_glLightfv,
    run_glLighti,
    run_glLightiv,
    run_glLineStipple,
    run_glLineWidth,
    run_glListBase,
    run_glLoadIdentity,
    run_glLoadMatrixd,
    run_glLoadMatrixf,
    run_glLoadName,
    run_glLoadTransposeMatrixd,
    run_glLoadTransposeMatrixdARB,
    run_glLoadTransposeMatrixf,
    run_glLoadTransposeMatrixfARB,
    run_glLogicOp,
    run_glMaterialf,
    run_glMaterialfv,
    run_glMateriali,
    run_glMaterialiv,
    run_glMatrixMode,
    run_glMultMatrixd,
    run_glMultMatrixf,
    run_glMultTransposeMatrixd,
    run_glMultTransposeMatrixdARB,
    run_glMultTransposeMatrixf,
    run_glMultTransposeMatrixfARB,
    run_glOrtho,
    run_glPassThrough,
    run_glPixelTransferf,
    run_glPixelTransferi,
    run_glPixelZoom,
    run_glPointParameterf,
    run_glPointParameterfARB,
    run_glPointParameterfEXT,
    run_glPointParameterfv,
    run_glPointParameterfvARB,
    run_glPointParameterfvEXT,
    run_glPointParameteri,
    run_glPointParameteriv,
    run_glPointSize,
    run_glPolygonMode,
    run_glPolygonOffset,
    run_glPolygonOffsetClamp,
    run_glPopMatrix,
    run_glPopName,
    run_glPrioritizeTextures,
    run_glPushAttrib,
    run_glPushMatrix,
    run_glPushName,
    run_glReadBuffer,
    run_glRotated,
    run_glRotatef,
    run_glScaled,
    run_glScalef,
    run_glScissor,
    run_glShadeModel,
    run_glStencilFunc,
    run_glStencilFuncSeparate,
    run_glStencilMask,
    run_glStencilMaskSeparate,
    run_glStencilOp,
    run_glStencilOpSeparate,
    run_glTexEnvf,
    run_glTexEnvfv,
    run_glTexEnvi,
    run_glTexEnviv,
    run_glTexGend,
    run_glTexGendv,
    run_glTexGenf,
    run_glTexGenfv,
    run_glTexGeni,
    run_glTexGeniv,
    run_glTexParameterf,
    run_glTexParameterfv,
    run_glTexParameteri,
    run_glTexParameteriv,
    run_glTranslated,
    run_glTranslatef,
    run_glUniform1f,
    run_glUniform1fv,
    run_glUniform1i,
    run_glUniform1iv,
    run_glUniform1ui,
    run_glUniform1uiv,
    run_glUniform2f,
    run_glUniform2fv,
    run_glUniform2i,
    run_glUniform2iv,
    run_glUniform2ui,
    run_glUniform2uiv,
    run_glUniform3f,
    run_glUniform3fv,
    run_glUniform3i,
    run_glUniform3iv,
    run_glUniform3ui,
    run_glUniform3uiv,
    run_glUniform4f,
    run_glUniform4fv,
    run_glUniform4i,
    run_glUniform4iv,
    run_glUniform4ui,
    run_glUniform4uiv,
    run_glUniformMatrix2fv,
    run_glUniformMatrix2x3fv,
    run_glUniformMatrix2x4fv,
    run_glUniformMatrix3fv,
    run_glUniformMatrix3x2fv,
    run_glUniformMatrix3x4fv,
    run_glUniformMatrix4fv,
    run_glUniformMatrix4x2fv,
    run_glUniformMatrix4x3fv,
    run_glUseProgram,
    run_glViewport,
};
const uint32_t gl_dl_run_count = 159;

GLAPI void APIENTRY glActiveTexture(GLenum texture)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glActiveTexture(ctx, texture);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glActiveTexture(texture);
}

GLAPI void APIENTRY glAlphaFunc(GLenum func, GLfloat ref)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glAlphaFunc(ctx, func, ref);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glAlphaFunc(func, ref);
}

GLAPI void APIENTRY glBeginQuery(GLenum target, GLuint name)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glBeginQuery(ctx, target, name);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glBeginQuery(target, name);
}

GLAPI void APIENTRY glBindTexture(GLenum target, GLuint name)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glBindTexture(ctx, target, name);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glBindTexture(target, name);
}

GLAPI void APIENTRY glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glBlendColor(ctx, r, g, b, a);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glBlendColor(r, g, b, a);
}

GLAPI void APIENTRY glBlendEquation(GLenum mode)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glBlendEquation(ctx, mode);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glBlendEquation(mode);
}

GLAPI void APIENTRY glBlendEquationSeparate(GLenum rgb, GLenum alpha)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glBlendEquationSeparate(ctx, rgb, alpha);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glBlendEquationSeparate(rgb, alpha);
}

GLAPI void APIENTRY glBlendFunc(GLenum src, GLenum dst)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glBlendFunc(ctx, src, dst);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glBlendFunc(src, dst);
}

GLAPI void APIENTRY glBlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glBlendFuncSeparate(ctx, src_rgb, dst_rgb, src_alpha, dst_alpha);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glBlendFuncSeparate(src_rgb, dst_rgb, src_alpha, dst_alpha);
}

GLAPI void APIENTRY glClear(GLbitfield mask)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glClear(ctx, mask);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glClear(mask);
}

GLAPI void APIENTRY glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glClearAccum(ctx, red, green, blue, alpha);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glClearAccum(red, green, blue, alpha);
}

GLAPI void APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glClearColor(ctx, r, g, b, a);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glClearColor(r, g, b, a);
}

GLAPI void APIENTRY glClearDepth(GLdouble d)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glClearDepth(ctx, d);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glClearDepth(d);
}

GLAPI void APIENTRY glClearDepthf(GLfloat d)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glClearDepthf(ctx, d);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glClearDepthf(d);
}

GLAPI void APIENTRY glClearIndex(GLfloat c)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glClearIndex(ctx, c);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glClearIndex(c);
}

GLAPI void APIENTRY glClearStencil(GLint s)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glClearStencil(ctx, s);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glClearStencil(s);
}

GLAPI void APIENTRY glClipPlane(GLenum plane, const GLdouble *equation)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glClipPlane(ctx, plane, equation);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glClipPlane(plane, equation);
}

GLAPI void APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glColorMask(ctx, r, g, b, a);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glColorMask(r, g, b, a);
}

GLAPI void APIENTRY glColorMaterial(GLenum face, GLenum mode)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glColorMaterial(ctx, face, mode);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glColorMaterial(face, mode);
}

GLAPI void APIENTRY glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glCopyPixels(ctx, x, y, width, height, type);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glCopyPixels(x, y, width, height, type);
}

GLAPI void APIENTRY glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glCopyTexImage1D(ctx, target, level, internalformat, x, y, width, border);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glCopyTexImage1D(target, level, internalformat, x, y, width, border);
}

GLAPI void APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internal, GLint x, GLint y, GLsizei w,                                      GLsizei h, GLint border)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glCopyTexImage2D(ctx, target, level, internal, x, y, w, h, border);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glCopyTexImage2D(target, level, internal, x, y, w, h, border);
}

GLAPI void APIENTRY glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glCopyTexSubImage1D(ctx, target, level, xoffset, x, y, width);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glCopyTexSubImage1D(target, level, xoffset, x, y, width);
}

GLAPI void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint dx, GLint dy, GLint x, GLint y, GLsizei w,                                         GLsizei h)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glCopyTexSubImage2D(ctx, target, level, dx, dy, x, y, w, h);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glCopyTexSubImage2D(target, level, dx, dy, x, y, w, h);
}

GLAPI void APIENTRY glCullFace(GLenum mode)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glCullFace(ctx, mode);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glCullFace(mode);
}

GLAPI void APIENTRY glDepthFunc(GLenum func)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glDepthFunc(ctx, func);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glDepthFunc(func);
}

GLAPI void APIENTRY glDepthMask(GLboolean flag)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glDepthMask(ctx, flag);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glDepthMask(flag);
}

GLAPI void APIENTRY glDepthRange(GLdouble n, GLdouble f)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glDepthRange(ctx, n, f);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glDepthRange(n, f);
}

GLAPI void APIENTRY glDepthRangef(GLfloat n, GLfloat f)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glDepthRangef(ctx, n, f);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glDepthRangef(n, f);
}

GLAPI void APIENTRY glDisable(GLenum cap)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glDisable(ctx, cap);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glDisable(cap);
}

GLAPI void APIENTRY glDrawBuffer(GLenum buf)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glDrawBuffer(ctx, buf);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glDrawBuffer(buf);
}

GLAPI void APIENTRY glDrawBuffers(GLsizei n, const GLenum *bufs)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glDrawBuffers(ctx, n, bufs);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glDrawBuffers(n, bufs);
}

GLAPI void APIENTRY glEnable(GLenum cap)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glEnable(ctx, cap);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glEnable(cap);
}

GLAPI void APIENTRY glEndQuery(GLenum target)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glEndQuery(ctx, target);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glEndQuery(target);
}

GLAPI void APIENTRY glFogf(GLenum pname, GLfloat param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glFogf(ctx, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glFogf(pname, param);
}

GLAPI void APIENTRY glFogfv(GLenum pname, const GLfloat *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glFogfv(ctx, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glFogfv(pname, params);
}

GLAPI void APIENTRY glFogi(GLenum pname, GLint param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glFogi(ctx, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glFogi(pname, param);
}

GLAPI void APIENTRY glFogiv(GLenum pname, const GLint *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glFogiv(ctx, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glFogiv(pname, params);
}

GLAPI void APIENTRY glFrontFace(GLenum mode)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glFrontFace(ctx, mode);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glFrontFace(mode);
}

GLAPI void APIENTRY glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top_, GLdouble near_val,                               GLdouble far_val)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glFrustum(ctx, left, right, bottom, top_, near_val, far_val);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glFrustum(left, right, bottom, top_, near_val, far_val);
}

GLAPI void APIENTRY glInitNames(void)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glInitNames(ctx);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glInitNames();
}

GLAPI void APIENTRY glLightModelf(GLenum pname, GLfloat param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLightModelf(ctx, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLightModelf(pname, param);
}

GLAPI void APIENTRY glLightModelfv(GLenum pname, const GLfloat *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLightModelfv(ctx, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLightModelfv(pname, params);
}

GLAPI void APIENTRY glLightModeli(GLenum pname, GLint param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLightModeli(ctx, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLightModeli(pname, param);
}

GLAPI void APIENTRY glLightModeliv(GLenum pname, const GLint *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLightModeliv(ctx, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLightModeliv(pname, params);
}

GLAPI void APIENTRY glLightf(GLenum l, GLenum pname, GLfloat param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLightf(ctx, l, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLightf(l, pname, param);
}

GLAPI void APIENTRY glLightfv(GLenum l, GLenum pname, const GLfloat *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLightfv(ctx, l, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLightfv(l, pname, params);
}

GLAPI void APIENTRY glLighti(GLenum l, GLenum pname, GLint param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLighti(ctx, l, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLighti(l, pname, param);
}

GLAPI void APIENTRY glLightiv(GLenum l, GLenum pname, const GLint *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLightiv(ctx, l, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLightiv(l, pname, params);
}

GLAPI void APIENTRY glLineStipple(GLint factor, GLushort pattern)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLineStipple(ctx, factor, pattern);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLineStipple(factor, pattern);
}

GLAPI void APIENTRY glLineWidth(GLfloat width)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLineWidth(ctx, width);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLineWidth(width);
}

GLAPI void APIENTRY glListBase(GLuint base)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glListBase(ctx, base);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glListBase(base);
}

GLAPI void APIENTRY glLoadIdentity(void)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLoadIdentity(ctx);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLoadIdentity();
}

GLAPI void APIENTRY glLoadMatrixd(const GLdouble *m)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLoadMatrixd(ctx, m);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLoadMatrixd(m);
}

GLAPI void APIENTRY glLoadMatrixf(const GLfloat *m)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLoadMatrixf(ctx, m);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLoadMatrixf(m);
}

GLAPI void APIENTRY glLoadName(GLuint name)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLoadName(ctx, name);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLoadName(name);
}

GLAPI void APIENTRY glLoadTransposeMatrixd(const GLdouble *m)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLoadTransposeMatrixd(ctx, m);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLoadTransposeMatrixd(m);
}

GLAPI void APIENTRY glLoadTransposeMatrixdARB(const GLdouble *m)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLoadTransposeMatrixdARB(ctx, m);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLoadTransposeMatrixdARB(m);
}

GLAPI void APIENTRY glLoadTransposeMatrixf(const GLfloat *m)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLoadTransposeMatrixf(ctx, m);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLoadTransposeMatrixf(m);
}

GLAPI void APIENTRY glLoadTransposeMatrixfARB(const GLfloat *m)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLoadTransposeMatrixfARB(ctx, m);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLoadTransposeMatrixfARB(m);
}

GLAPI void APIENTRY glLogicOp(GLenum opcode)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glLogicOp(ctx, opcode);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glLogicOp(opcode);
}

GLAPI void APIENTRY glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !1) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glMaterialf(ctx, face, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glMaterialf(face, pname, param);
}

GLAPI void APIENTRY glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !1) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glMaterialfv(ctx, face, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glMaterialfv(face, pname, params);
}

GLAPI void APIENTRY glMateriali(GLenum face, GLenum pname, GLint param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !1) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glMateriali(ctx, face, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glMateriali(face, pname, param);
}

GLAPI void APIENTRY glMaterialiv(GLenum face, GLenum pname, const GLint *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !1) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glMaterialiv(ctx, face, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glMaterialiv(face, pname, params);
}

GLAPI void APIENTRY glMatrixMode(GLenum mode)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glMatrixMode(ctx, mode);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glMatrixMode(mode);
}

GLAPI void APIENTRY glMultMatrixd(const GLdouble *m)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glMultMatrixd(ctx, m);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glMultMatrixd(m);
}

GLAPI void APIENTRY glMultMatrixf(const GLfloat *m)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glMultMatrixf(ctx, m);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glMultMatrixf(m);
}

GLAPI void APIENTRY glMultTransposeMatrixd(const GLdouble *m)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glMultTransposeMatrixd(ctx, m);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glMultTransposeMatrixd(m);
}

GLAPI void APIENTRY glMultTransposeMatrixdARB(const GLdouble *m)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glMultTransposeMatrixdARB(ctx, m);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glMultTransposeMatrixdARB(m);
}

GLAPI void APIENTRY glMultTransposeMatrixf(const GLfloat *m)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glMultTransposeMatrixf(ctx, m);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glMultTransposeMatrixf(m);
}

GLAPI void APIENTRY glMultTransposeMatrixfARB(const GLfloat *m)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glMultTransposeMatrixfARB(ctx, m);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glMultTransposeMatrixfARB(m);
}

GLAPI void APIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top_, GLdouble near_val,                             GLdouble far_val)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glOrtho(ctx, left, right, bottom, top_, near_val, far_val);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glOrtho(left, right, bottom, top_, near_val, far_val);
}

GLAPI void APIENTRY glPassThrough(GLfloat token)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPassThrough(ctx, token);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPassThrough(token);
}

GLAPI void APIENTRY glPixelTransferf(GLenum pname, GLfloat param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPixelTransferf(ctx, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPixelTransferf(pname, param);
}

GLAPI void APIENTRY glPixelTransferi(GLenum pname, GLint param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPixelTransferi(ctx, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPixelTransferi(pname, param);
}

GLAPI void APIENTRY glPixelZoom(GLfloat xfactor, GLfloat yfactor)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPixelZoom(ctx, xfactor, yfactor);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPixelZoom(xfactor, yfactor);
}

GLAPI void APIENTRY glPointParameterf(GLenum pname, GLfloat value)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPointParameterf(ctx, pname, value);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPointParameterf(pname, value);
}

GLAPI void APIENTRY glPointParameterfARB(GLenum pname, GLfloat value)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPointParameterfARB(ctx, pname, value);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPointParameterfARB(pname, value);
}

GLAPI void APIENTRY glPointParameterfEXT(GLenum pname, GLfloat value)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPointParameterfEXT(ctx, pname, value);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPointParameterfEXT(pname, value);
}

GLAPI void APIENTRY glPointParameterfv(GLenum pname, const GLfloat *values)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPointParameterfv(ctx, pname, values);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPointParameterfv(pname, values);
}

GLAPI void APIENTRY glPointParameterfvARB(GLenum pname, const GLfloat *values)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPointParameterfvARB(ctx, pname, values);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPointParameterfvARB(pname, values);
}

GLAPI void APIENTRY glPointParameterfvEXT(GLenum pname, const GLfloat *values)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPointParameterfvEXT(ctx, pname, values);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPointParameterfvEXT(pname, values);
}

GLAPI void APIENTRY glPointParameteri(GLenum pname, GLint value)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPointParameteri(ctx, pname, value);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPointParameteri(pname, value);
}

GLAPI void APIENTRY glPointParameteriv(GLenum pname, const GLint *values)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPointParameteriv(ctx, pname, values);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPointParameteriv(pname, values);
}

GLAPI void APIENTRY glPointSize(GLfloat size)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPointSize(ctx, size);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPointSize(size);
}

GLAPI void APIENTRY glPolygonMode(GLenum face, GLenum mode)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPolygonMode(ctx, face, mode);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPolygonMode(face, mode);
}

GLAPI void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPolygonOffset(ctx, factor, units);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPolygonOffset(factor, units);
}

GLAPI void APIENTRY glPolygonOffsetClamp(GLfloat factor, GLfloat units, GLfloat clamp)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPolygonOffsetClamp(ctx, factor, units, clamp);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPolygonOffsetClamp(factor, units, clamp);
}

GLAPI void APIENTRY glPopMatrix(void)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPopMatrix(ctx);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPopMatrix();
}

GLAPI void APIENTRY glPopName(void)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPopName(ctx);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPopName();
}

GLAPI void APIENTRY glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPrioritizeTextures(ctx, n, textures, priorities);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPrioritizeTextures(n, textures, priorities);
}

GLAPI void APIENTRY glPushAttrib(GLbitfield mask)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPushAttrib(ctx, mask);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPushAttrib(mask);
}

GLAPI void APIENTRY glPushMatrix(void)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPushMatrix(ctx);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPushMatrix();
}

GLAPI void APIENTRY glPushName(GLuint name)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glPushName(ctx, name);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glPushName(name);
}

GLAPI void APIENTRY glReadBuffer(GLenum buf)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glReadBuffer(ctx, buf);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glReadBuffer(buf);
}

GLAPI void APIENTRY glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glRotated(ctx, angle, x, y, z);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glRotated(angle, x, y, z);
}

GLAPI void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glRotatef(ctx, angle, x, y, z);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glRotatef(angle, x, y, z);
}

GLAPI void APIENTRY glScaled(GLdouble x, GLdouble y, GLdouble z)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glScaled(ctx, x, y, z);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glScaled(x, y, z);
}

GLAPI void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glScalef(ctx, x, y, z);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glScalef(x, y, z);
}

GLAPI void APIENTRY glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glScissor(ctx, x, y, w, h);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glScissor(x, y, w, h);
}

GLAPI void APIENTRY glShadeModel(GLenum mode)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glShadeModel(ctx, mode);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glShadeModel(mode);
}

GLAPI void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glStencilFunc(ctx, func, ref, mask);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glStencilFunc(func, ref, mask);
}

GLAPI void APIENTRY glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glStencilFuncSeparate(ctx, face, func, ref, mask);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glStencilFuncSeparate(face, func, ref, mask);
}

GLAPI void APIENTRY glStencilMask(GLuint mask)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glStencilMask(ctx, mask);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glStencilMask(mask);
}

GLAPI void APIENTRY glStencilMaskSeparate(GLenum face, GLuint mask)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glStencilMaskSeparate(ctx, face, mask);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glStencilMaskSeparate(face, mask);
}

GLAPI void APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glStencilOp(ctx, fail, zfail, zpass);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glStencilOp(fail, zfail, zpass);
}

GLAPI void APIENTRY glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glStencilOpSeparate(ctx, face, fail, zfail, zpass);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glStencilOpSeparate(face, fail, zfail, zpass);
}

GLAPI void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexEnvf(ctx, target, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexEnvf(target, pname, param);
}

GLAPI void APIENTRY glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexEnvfv(ctx, target, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexEnvfv(target, pname, params);
}

GLAPI void APIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexEnvi(ctx, target, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexEnvi(target, pname, param);
}

GLAPI void APIENTRY glTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexEnviv(ctx, target, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexEnviv(target, pname, params);
}

GLAPI void APIENTRY glTexGend(GLenum coord, GLenum pname, GLdouble param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexGend(ctx, coord, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexGend(coord, pname, param);
}

GLAPI void APIENTRY glTexGendv(GLenum coord, GLenum pname, const GLdouble *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexGendv(ctx, coord, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexGendv(coord, pname, params);
}

GLAPI void APIENTRY glTexGenf(GLenum coord, GLenum pname, GLfloat param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexGenf(ctx, coord, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexGenf(coord, pname, param);
}

GLAPI void APIENTRY glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexGenfv(ctx, coord, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexGenfv(coord, pname, params);
}

GLAPI void APIENTRY glTexGeni(GLenum coord, GLenum pname, GLint param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexGeni(ctx, coord, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexGeni(coord, pname, param);
}

GLAPI void APIENTRY glTexGeniv(GLenum coord, GLenum pname, const GLint *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexGeniv(ctx, coord, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexGeniv(coord, pname, params);
}

GLAPI void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexParameterf(ctx, target, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexParameterf(target, pname, param);
}

GLAPI void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexParameterfv(ctx, target, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexParameterfv(target, pname, params);
}

GLAPI void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexParameteri(ctx, target, pname, param);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexParameteri(target, pname, param);
}

GLAPI void APIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTexParameteriv(ctx, target, pname, params);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTexParameteriv(target, pname, params);
}

GLAPI void APIENTRY glTranslated(GLdouble x, GLdouble y, GLdouble z)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTranslated(ctx, x, y, z);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTranslated(x, y, z);
}

GLAPI void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glTranslatef(ctx, x, y, z);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glTranslatef(x, y, z);
}

GLAPI void APIENTRY glUniform1f(GLint l, GLfloat a)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform1f(ctx, l, a);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform1f(l, a);
}

GLAPI void APIENTRY glUniform1fv(GLint l, GLsizei n, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform1fv(ctx, l, n, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform1fv(l, n, v);
}

GLAPI void APIENTRY glUniform1i(GLint l, GLint a)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform1i(ctx, l, a);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform1i(l, a);
}

GLAPI void APIENTRY glUniform1iv(GLint l, GLsizei n, const GLint *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform1iv(ctx, l, n, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform1iv(l, n, v);
}

GLAPI void APIENTRY glUniform1ui(GLint l, GLuint a)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform1ui(ctx, l, a);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform1ui(l, a);
}

GLAPI void APIENTRY glUniform1uiv(GLint l, GLsizei n, const GLuint *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform1uiv(ctx, l, n, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform1uiv(l, n, v);
}

GLAPI void APIENTRY glUniform2f(GLint l, GLfloat a, GLfloat b)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform2f(ctx, l, a, b);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform2f(l, a, b);
}

GLAPI void APIENTRY glUniform2fv(GLint l, GLsizei n, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform2fv(ctx, l, n, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform2fv(l, n, v);
}

GLAPI void APIENTRY glUniform2i(GLint l, GLint a, GLint b)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform2i(ctx, l, a, b);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform2i(l, a, b);
}

GLAPI void APIENTRY glUniform2iv(GLint l, GLsizei n, const GLint *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform2iv(ctx, l, n, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform2iv(l, n, v);
}

GLAPI void APIENTRY glUniform2ui(GLint l, GLuint a, GLuint b)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform2ui(ctx, l, a, b);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform2ui(l, a, b);
}

GLAPI void APIENTRY glUniform2uiv(GLint l, GLsizei n, const GLuint *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform2uiv(ctx, l, n, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform2uiv(l, n, v);
}

GLAPI void APIENTRY glUniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform3f(ctx, l, a, b, c);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform3f(l, a, b, c);
}

GLAPI void APIENTRY glUniform3fv(GLint l, GLsizei n, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform3fv(ctx, l, n, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform3fv(l, n, v);
}

GLAPI void APIENTRY glUniform3i(GLint l, GLint a, GLint b, GLint c)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform3i(ctx, l, a, b, c);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform3i(l, a, b, c);
}

GLAPI void APIENTRY glUniform3iv(GLint l, GLsizei n, const GLint *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform3iv(ctx, l, n, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform3iv(l, n, v);
}

GLAPI void APIENTRY glUniform3ui(GLint l, GLuint a, GLuint b, GLuint c)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform3ui(ctx, l, a, b, c);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform3ui(l, a, b, c);
}

GLAPI void APIENTRY glUniform3uiv(GLint l, GLsizei n, const GLuint *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform3uiv(ctx, l, n, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform3uiv(l, n, v);
}

GLAPI void APIENTRY glUniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform4f(ctx, l, a, b, c, d);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform4f(l, a, b, c, d);
}

GLAPI void APIENTRY glUniform4fv(GLint l, GLsizei n, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform4fv(ctx, l, n, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform4fv(l, n, v);
}

GLAPI void APIENTRY glUniform4i(GLint l, GLint a, GLint b, GLint c, GLint d)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform4i(ctx, l, a, b, c, d);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform4i(l, a, b, c, d);
}

GLAPI void APIENTRY glUniform4iv(GLint l, GLsizei n, const GLint *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform4iv(ctx, l, n, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform4iv(l, n, v);
}

GLAPI void APIENTRY glUniform4ui(GLint l, GLuint a, GLuint b, GLuint c, GLuint d)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform4ui(ctx, l, a, b, c, d);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform4ui(l, a, b, c, d);
}

GLAPI void APIENTRY glUniform4uiv(GLint l, GLsizei n, const GLuint *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniform4uiv(ctx, l, n, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniform4uiv(l, n, v);
}

GLAPI void APIENTRY glUniformMatrix2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniformMatrix2fv(ctx, l, n, t, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniformMatrix2fv(l, n, t, v);
}

GLAPI void APIENTRY glUniformMatrix2x3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniformMatrix2x3fv(ctx, l, n, t, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniformMatrix2x3fv(l, n, t, v);
}

GLAPI void APIENTRY glUniformMatrix2x4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniformMatrix2x4fv(ctx, l, n, t, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniformMatrix2x4fv(l, n, t, v);
}

GLAPI void APIENTRY glUniformMatrix3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniformMatrix3fv(ctx, l, n, t, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniformMatrix3fv(l, n, t, v);
}

GLAPI void APIENTRY glUniformMatrix3x2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniformMatrix3x2fv(ctx, l, n, t, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniformMatrix3x2fv(l, n, t, v);
}

GLAPI void APIENTRY glUniformMatrix3x4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniformMatrix3x4fv(ctx, l, n, t, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniformMatrix3x4fv(l, n, t, v);
}

GLAPI void APIENTRY glUniformMatrix4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniformMatrix4fv(ctx, l, n, t, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniformMatrix4fv(l, n, t, v);
}

GLAPI void APIENTRY glUniformMatrix4x2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniformMatrix4x2fv(ctx, l, n, t, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniformMatrix4x2fv(l, n, t, v);
}

GLAPI void APIENTRY glUniformMatrix4x3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUniformMatrix4x3fv(ctx, l, n, t, v);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUniformMatrix4x3fv(l, n, t, v);
}

GLAPI void APIENTRY glUseProgram(GLuint name)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glUseProgram(ctx, name);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glUseProgram(name);
}

GLAPI void APIENTRY glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            rec_glViewport(ctx, x, y, w, h);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        }
        if (!run)
            return;
    }
    gl_do_glViewport(x, y, w, h);
}

GLAPI void APIENTRY glArrayElement(GLint i)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !1) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glArrayElement(ctx, i);
        }
        if (!run)
            return;
    }
    gl_do_glArrayElement(i);
}

GLAPI void APIENTRY glBegin(GLenum mode)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glBegin(ctx, mode);
        }
        if (!run)
            return;
    }
    gl_do_glBegin(mode);
}

GLAPI void APIENTRY glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glBitmap(ctx, width, height, xorig, yorig, xmove, ymove, bitmap);
        }
        if (!run)
            return;
    }
    gl_do_glBitmap(width, height, xorig, yorig, xmove, ymove, bitmap);
}

GLAPI void APIENTRY glCallList(GLuint list)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !1) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glCallList(ctx, list);
        }
        if (!run)
            return;
    }
    gl_do_glCallList(list);
}

GLAPI void APIENTRY glCallLists(GLsizei n, GLenum type, const GLvoid *lists)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !1) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glCallLists(ctx, n, type, lists);
        }
        if (!run)
            return;
    }
    gl_do_glCallLists(n, type, lists);
}

GLAPI void APIENTRY glCompressedTexImage2D(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h,                                            GLint border, GLsizei size, const void *data)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glCompressedTexImage2D(ctx, target, level, internal, w, h, border, size, data);
        }
        if (!run)
            return;
    }
    gl_do_glCompressedTexImage2D(target, level, internal, w, h, border, size, data);
}

GLAPI void APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,                                               GLenum format, GLsizei size, const void *data)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glCompressedTexSubImage2D(ctx, target, level, x, y, w, h, format, size, data);
        }
        if (!run)
            return;
    }
    gl_do_glCompressedTexSubImage2D(target, level, x, y, w, h, format, size, data);
}

GLAPI void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glDrawArrays(ctx, mode, first, count);
        }
        if (!run)
            return;
    }
    gl_do_glDrawArrays(mode, first, count);
}

GLAPI void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glDrawElements(ctx, mode, count, type, indices);
        }
        if (!run)
            return;
    }
    gl_do_glDrawElements(mode, count, type, indices);
}

GLAPI void APIENTRY glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glDrawPixels(ctx, width, height, format, type, pixels);
        }
        if (!run)
            return;
    }
    gl_do_glDrawPixels(width, height, format, type, pixels);
}

GLAPI void APIENTRY glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,                                         const void *indices)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glDrawRangeElements(ctx, mode, start, end, count, type, indices);
        }
        if (!run)
            return;
    }
    gl_do_glDrawRangeElements(mode, start, end, count, type, indices);
}

GLAPI void APIENTRY glEdgeFlag(GLboolean flag)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !1) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glEdgeFlag(ctx, flag);
        }
        if (!run)
            return;
    }
    gl_do_glEdgeFlag(flag);
}

GLAPI void APIENTRY glEdgeFlagv(const GLboolean *flag)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !1) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glEdgeFlagv(ctx, flag);
        }
        if (!run)
            return;
    }
    gl_do_glEdgeFlagv(flag);
}

GLAPI void APIENTRY glEnd(void)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !1) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glEnd(ctx);
        }
        if (!run)
            return;
    }
    gl_do_glEnd();
}

GLAPI void APIENTRY glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei n)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glMultiDrawArrays(ctx, mode, first, count, n);
        }
        if (!run)
            return;
    }
    gl_do_glMultiDrawArrays(mode, first, count, n);
}

GLAPI void APIENTRY glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const void *const *indices,                                         GLsizei n)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glMultiDrawElements(ctx, mode, count, type, indices, n);
        }
        if (!run)
            return;
    }
    gl_do_glMultiDrawElements(mode, count, type, indices, n);
}

GLAPI void APIENTRY glPolygonStipple(const GLubyte *mask)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glPolygonStipple(ctx, mask);
        }
        if (!run)
            return;
    }
    gl_do_glPolygonStipple(mask);
}

GLAPI void APIENTRY glPopAttrib(void)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glPopAttrib(ctx);
        }
        if (!run)
            return;
    }
    gl_do_glPopAttrib();
}

GLAPI void APIENTRY glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glRectd(ctx, x1, y1, x2, y2);
        }
        if (!run)
            return;
    }
    gl_do_glRectd(x1, y1, x2, y2);
}

GLAPI void APIENTRY glRectdv(const GLdouble *v1, const GLdouble *v2)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glRectdv(ctx, v1, v2);
        }
        if (!run)
            return;
    }
    gl_do_glRectdv(v1, v2);
}

GLAPI void APIENTRY glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glRectf(ctx, x1, y1, x2, y2);
        }
        if (!run)
            return;
    }
    gl_do_glRectf(x1, y1, x2, y2);
}

GLAPI void APIENTRY glRectfv(const GLfloat *v1, const GLfloat *v2)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glRectfv(ctx, v1, v2);
        }
        if (!run)
            return;
    }
    gl_do_glRectfv(v1, v2);
}

GLAPI void APIENTRY glRecti(GLint x1, GLint y1, GLint x2, GLint y2)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glRecti(ctx, x1, y1, x2, y2);
        }
        if (!run)
            return;
    }
    gl_do_glRecti(x1, y1, x2, y2);
}

GLAPI void APIENTRY glRectiv(const GLint *v1, const GLint *v2)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glRectiv(ctx, v1, v2);
        }
        if (!run)
            return;
    }
    gl_do_glRectiv(v1, v2);
}

GLAPI void APIENTRY glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glRects(ctx, x1, y1, x2, y2);
        }
        if (!run)
            return;
    }
    gl_do_glRects(x1, y1, x2, y2);
}

GLAPI void APIENTRY glRectsv(const GLshort *v1, const GLshort *v2)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glRectsv(ctx, v1, v2);
        }
        if (!run)
            return;
    }
    gl_do_glRectsv(v1, v2);
}

GLAPI void APIENTRY glTexImage1D(GLenum target, GLint level, GLint internal, GLsizei w, GLint border, GLenum format,                                  GLenum type, const void *pixels)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glTexImage1D(ctx, target, level, internal, w, border, format, type, pixels);
        }
        if (!run)
            return;
    }
    gl_do_glTexImage1D(target, level, internal, w, border, format, type, pixels);
}

GLAPI void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,                                  GLenum format, GLenum type, const void *pixels)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glTexImage2D(ctx, target, level, internal, w, h, border, format, type, pixels);
        }
        if (!run)
            return;
    }
    gl_do_glTexImage2D(target, level, internal, w, h, border, format, type, pixels);
}

GLAPI void APIENTRY glTexImage3D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d,                                  GLint border, GLenum format, GLenum type, const void *pixels)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glTexImage3D(ctx, target, level, internal, w, h, d, border, format, type, pixels);
        }
        if (!run)
            return;
    }
    gl_do_glTexImage3D(target, level, internal, w, h, d, border, format, type, pixels);
}

GLAPI void APIENTRY glTexSubImage1D(GLenum target, GLint level, GLint x, GLsizei w, GLenum format, GLenum type,                                     const void *pixels)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glTexSubImage1D(ctx, target, level, x, w, format, type, pixels);
        }
        if (!run)
            return;
    }
    gl_do_glTexSubImage1D(target, level, x, w, format, type, pixels);
}

GLAPI void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,                                     GLenum type, const void *pixels)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glTexSubImage2D(ctx, target, level, x, y, w, h, format, type, pixels);
        }
        if (!run)
            return;
    }
    gl_do_glTexSubImage2D(target, level, x, y, w, h, format, type, pixels);
}

GLAPI void APIENTRY glTexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h,                                     GLsizei d, GLenum format, GLenum type, const void *pixels)
{
    GlContext *ctx = gl_current();
    if (ctx && ctx->dl.mode) {
        BOOL run;
        if (ctx->dl.in_begin && !0) {
            gl_dl_error(ctx, GL_INVALID_OPERATION);
            run = ctx->dl.mode == GL_COMPILE_AND_EXECUTE;
        } else {
            run = gl_dl_custom_glTexSubImage3D(ctx, target, level, x, y, z, w, h, d, format, type, pixels);
        }
        if (!run)
            return;
    }
    gl_do_glTexSubImage3D(target, level, x, y, z, w, h, d, format, type, pixels);
}

