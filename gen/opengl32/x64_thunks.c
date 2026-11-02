/* generated, do not edit. */
#include "opengl32.h"
#include <mime/mx64.h>

#define I(c, i) ((i) < 4 ? (c)->gpr[i] : (c)->stack[(i) - 4])
static inline float F(const MX_NATIVE_CALL *c, int i)
{
    unsigned long long v = i < 4 ? c->xmm[i] : c->stack[i - 4];
    float f;
    memcpy(&f, &v, sizeof(f));
    return f;
}
static inline double D(const MX_NATIVE_CALL *c, int i)
{
    unsigned long long v = i < 4 ? c->xmm[i] : c->stack[i - 4];
    double d;
    memcpy(&d, &v, sizeof(d));
    return d;
}

void APIENTRY glAccum(GLenum op, GLfloat value);
static void x_glAccum(MX_NATIVE_CALL *c)
{
    glAccum((GLenum)I(c, 0), F(c, 1));
}

void APIENTRY glActiveTexture(GLenum texture);
static void x_glActiveTexture(MX_NATIVE_CALL *c)
{
    glActiveTexture((GLenum)I(c, 0));
}

void APIENTRY glAlphaFunc(GLenum func, GLfloat ref);
static void x_glAlphaFunc(MX_NATIVE_CALL *c)
{
    glAlphaFunc((GLenum)I(c, 0), F(c, 1));
}

GLboolean APIENTRY glAreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences);
static void x_glAreTexturesResident(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glAreTexturesResident((GLsizei)I(c, 0), (const GLuint *)I(c, 1), (GLboolean *)I(c, 2));
}

void APIENTRY glArrayElement(GLint i);
static void x_glArrayElement(MX_NATIVE_CALL *c)
{
    glArrayElement((GLint)I(c, 0));
}

void APIENTRY glAttachShader(GLuint program, GLuint name);
static void x_glAttachShader(MX_NATIVE_CALL *c)
{
    glAttachShader((GLuint)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glBegin(GLenum mode);
static void x_glBegin(MX_NATIVE_CALL *c)
{
    glBegin((GLenum)I(c, 0));
}

void APIENTRY glBeginQuery(GLenum target, GLuint name);
static void x_glBeginQuery(MX_NATIVE_CALL *c)
{
    glBeginQuery((GLenum)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glBindAttribLocation(GLuint program, GLuint index, const GLchar *name);
static void x_glBindAttribLocation(MX_NATIVE_CALL *c)
{
    glBindAttribLocation((GLuint)I(c, 0), (GLuint)I(c, 1), (const GLchar *)I(c, 2));
}

void APIENTRY glBindBuffer(GLenum target, GLuint name);
static void x_glBindBuffer(MX_NATIVE_CALL *c)
{
    glBindBuffer((GLenum)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glBindBufferBase(GLenum target, GLuint index, GLuint name);
static void x_glBindBufferBase(MX_NATIVE_CALL *c)
{
    glBindBufferBase((GLenum)I(c, 0), (GLuint)I(c, 1), (GLuint)I(c, 2));
}

void APIENTRY glBindBufferRange(GLenum target, GLuint index, GLuint name, GLintptr offset, GLsizeiptr size);
static void x_glBindBufferRange(MX_NATIVE_CALL *c)
{
    glBindBufferRange((GLenum)I(c, 0), (GLuint)I(c, 1), (GLuint)I(c, 2), (GLintptr)I(c, 3), (GLsizeiptr)I(c, 4));
}

void APIENTRY glBindFragDataLocation(GLuint program, GLuint color, const GLchar *name);
static void x_glBindFragDataLocation(MX_NATIVE_CALL *c)
{
    glBindFragDataLocation((GLuint)I(c, 0), (GLuint)I(c, 1), (const GLchar *)I(c, 2));
}

void APIENTRY glBindFramebuffer(GLenum target, GLuint name);
static void x_glBindFramebuffer(MX_NATIVE_CALL *c)
{
    glBindFramebuffer((GLenum)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glBindRenderbuffer(GLenum target, GLuint name);
static void x_glBindRenderbuffer(MX_NATIVE_CALL *c)
{
    glBindRenderbuffer((GLenum)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glBindTexture(GLenum target, GLuint name);
static void x_glBindTexture(MX_NATIVE_CALL *c)
{
    glBindTexture((GLenum)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glBindVertexArray(GLuint name);
static void x_glBindVertexArray(MX_NATIVE_CALL *c)
{
    glBindVertexArray((GLuint)I(c, 0));
}

void APIENTRY glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap);
static void x_glBitmap(MX_NATIVE_CALL *c)
{
    glBitmap((GLsizei)I(c, 0), (GLsizei)I(c, 1), F(c, 2), F(c, 3), F(c, 4), F(c, 5), (const GLubyte *)I(c, 6));
}

void APIENTRY glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
static void x_glBlendColor(MX_NATIVE_CALL *c)
{
    glBlendColor(F(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glBlendEquation(GLenum mode);
static void x_glBlendEquation(MX_NATIVE_CALL *c)
{
    glBlendEquation((GLenum)I(c, 0));
}

void APIENTRY glBlendEquationSeparate(GLenum rgb, GLenum alpha);
static void x_glBlendEquationSeparate(MX_NATIVE_CALL *c)
{
    glBlendEquationSeparate((GLenum)I(c, 0), (GLenum)I(c, 1));
}

void APIENTRY glBlendFunc(GLenum src, GLenum dst);
static void x_glBlendFunc(MX_NATIVE_CALL *c)
{
    glBlendFunc((GLenum)I(c, 0), (GLenum)I(c, 1));
}

void APIENTRY glBlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha);
static void x_glBlendFuncSeparate(MX_NATIVE_CALL *c)
{
    glBlendFuncSeparate((GLenum)I(c, 0), (GLenum)I(c, 1), (GLenum)I(c, 2), (GLenum)I(c, 3));
}

void APIENTRY glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1,                                       GLint dy1, GLbitfield mask, GLenum filter);
static void x_glBlitFramebuffer(MX_NATIVE_CALL *c)
{
    glBlitFramebuffer((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3), (GLint)I(c, 4), (GLint)I(c, 5), (GLint)I(c, 6), (GLint)I(c, 7), (GLbitfield)I(c, 8), (GLenum)I(c, 9));
}

void APIENTRY glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
static void x_glBufferData(MX_NATIVE_CALL *c)
{
    glBufferData((GLenum)I(c, 0), (GLsizeiptr)I(c, 1), (const void *)I(c, 2), (GLenum)I(c, 3));
}

void APIENTRY glBufferStorage(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags);
static void x_glBufferStorage(MX_NATIVE_CALL *c)
{
    glBufferStorage((GLenum)I(c, 0), (GLsizeiptr)I(c, 1), (const void *)I(c, 2), (GLbitfield)I(c, 3));
}

void APIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
static void x_glBufferSubData(MX_NATIVE_CALL *c)
{
    glBufferSubData((GLenum)I(c, 0), (GLintptr)I(c, 1), (GLsizeiptr)I(c, 2), (const void *)I(c, 3));
}

void APIENTRY glCallList(GLuint list);
static void x_glCallList(MX_NATIVE_CALL *c)
{
    glCallList((GLuint)I(c, 0));
}

void APIENTRY glCallLists(GLsizei n, GLenum type, const GLvoid *lists);
static void x_glCallLists(MX_NATIVE_CALL *c)
{
    glCallLists((GLsizei)I(c, 0), (GLenum)I(c, 1), (const GLvoid *)I(c, 2));
}

GLenum APIENTRY glCheckFramebufferStatus(GLenum target);
static void x_glCheckFramebufferStatus(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glCheckFramebufferStatus((GLenum)I(c, 0));
}

void APIENTRY glClear(GLbitfield mask);
static void x_glClear(MX_NATIVE_CALL *c)
{
    glClear((GLbitfield)I(c, 0));
}

void APIENTRY glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
static void x_glClearAccum(MX_NATIVE_CALL *c)
{
    glClearAccum(F(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glClearBufferfi(GLenum buffer, GLint index, GLfloat depth, GLint stencil);
static void x_glClearBufferfi(MX_NATIVE_CALL *c)
{
    glClearBufferfi((GLenum)I(c, 0), (GLint)I(c, 1), F(c, 2), (GLint)I(c, 3));
}

void APIENTRY glClearBufferfv(GLenum buffer, GLint index, const GLfloat *value);
static void x_glClearBufferfv(MX_NATIVE_CALL *c)
{
    glClearBufferfv((GLenum)I(c, 0), (GLint)I(c, 1), (const GLfloat *)I(c, 2));
}

void APIENTRY glClearBufferiv(GLenum buffer, GLint index, const GLint *value);
static void x_glClearBufferiv(MX_NATIVE_CALL *c)
{
    glClearBufferiv((GLenum)I(c, 0), (GLint)I(c, 1), (const GLint *)I(c, 2));
}

void APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
static void x_glClearColor(MX_NATIVE_CALL *c)
{
    glClearColor(F(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glClearDepth(GLdouble d);
static void x_glClearDepth(MX_NATIVE_CALL *c)
{
    glClearDepth(D(c, 0));
}

void APIENTRY glClearDepthf(GLfloat d);
static void x_glClearDepthf(MX_NATIVE_CALL *c)
{
    glClearDepthf(F(c, 0));
}

void APIENTRY glClearIndex(GLfloat c);
static void x_glClearIndex(MX_NATIVE_CALL *c)
{
    glClearIndex(F(c, 0));
}

void APIENTRY glClearStencil(GLint s);
static void x_glClearStencil(MX_NATIVE_CALL *c)
{
    glClearStencil((GLint)I(c, 0));
}

void APIENTRY glClientActiveTexture(GLenum texture);
static void x_glClientActiveTexture(MX_NATIVE_CALL *c)
{
    glClientActiveTexture((GLenum)I(c, 0));
}

void APIENTRY glClientActiveTextureARB(GLenum texture);
static void x_glClientActiveTextureARB(MX_NATIVE_CALL *c)
{
    glClientActiveTextureARB((GLenum)I(c, 0));
}

GLenum APIENTRY glClientWaitSync(GLsync handle, GLbitfield flags, GLuint64 timeout);
static void x_glClientWaitSync(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glClientWaitSync((GLsync)I(c, 0), (GLbitfield)I(c, 1), (GLuint64)I(c, 2));
}

void APIENTRY glClipPlane(GLenum plane, const GLdouble *equation);
static void x_glClipPlane(MX_NATIVE_CALL *c)
{
    glClipPlane((GLenum)I(c, 0), (const GLdouble *)I(c, 1));
}

void APIENTRY glColor3b(GLbyte red, GLbyte green, GLbyte blue);
static void x_glColor3b(MX_NATIVE_CALL *c)
{
    glColor3b((GLbyte)I(c, 0), (GLbyte)I(c, 1), (GLbyte)I(c, 2));
}

void APIENTRY glColor3bv(const GLbyte *v);
static void x_glColor3bv(MX_NATIVE_CALL *c)
{
    glColor3bv((const GLbyte *)I(c, 0));
}

void APIENTRY glColor3d(GLdouble red, GLdouble green, GLdouble blue);
static void x_glColor3d(MX_NATIVE_CALL *c)
{
    glColor3d(D(c, 0), D(c, 1), D(c, 2));
}

void APIENTRY glColor3dv(const GLdouble *v);
static void x_glColor3dv(MX_NATIVE_CALL *c)
{
    glColor3dv((const GLdouble *)I(c, 0));
}

void APIENTRY glColor3f(GLfloat red, GLfloat green, GLfloat blue);
static void x_glColor3f(MX_NATIVE_CALL *c)
{
    glColor3f(F(c, 0), F(c, 1), F(c, 2));
}

void APIENTRY glColor3fv(const GLfloat *v);
static void x_glColor3fv(MX_NATIVE_CALL *c)
{
    glColor3fv((const GLfloat *)I(c, 0));
}

void APIENTRY glColor3i(GLint red, GLint green, GLint blue);
static void x_glColor3i(MX_NATIVE_CALL *c)
{
    glColor3i((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glColor3iv(const GLint *v);
static void x_glColor3iv(MX_NATIVE_CALL *c)
{
    glColor3iv((const GLint *)I(c, 0));
}

void APIENTRY glColor3s(GLshort red, GLshort green, GLshort blue);
static void x_glColor3s(MX_NATIVE_CALL *c)
{
    glColor3s((GLshort)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2));
}

void APIENTRY glColor3sv(const GLshort *v);
static void x_glColor3sv(MX_NATIVE_CALL *c)
{
    glColor3sv((const GLshort *)I(c, 0));
}

void APIENTRY glColor3ub(GLubyte red, GLubyte green, GLubyte blue);
static void x_glColor3ub(MX_NATIVE_CALL *c)
{
    glColor3ub((GLubyte)I(c, 0), (GLubyte)I(c, 1), (GLubyte)I(c, 2));
}

void APIENTRY glColor3ubv(const GLubyte *v);
static void x_glColor3ubv(MX_NATIVE_CALL *c)
{
    glColor3ubv((const GLubyte *)I(c, 0));
}

void APIENTRY glColor3ui(GLuint red, GLuint green, GLuint blue);
static void x_glColor3ui(MX_NATIVE_CALL *c)
{
    glColor3ui((GLuint)I(c, 0), (GLuint)I(c, 1), (GLuint)I(c, 2));
}

void APIENTRY glColor3uiv(const GLuint *v);
static void x_glColor3uiv(MX_NATIVE_CALL *c)
{
    glColor3uiv((const GLuint *)I(c, 0));
}

void APIENTRY glColor3us(GLushort red, GLushort green, GLushort blue);
static void x_glColor3us(MX_NATIVE_CALL *c)
{
    glColor3us((GLushort)I(c, 0), (GLushort)I(c, 1), (GLushort)I(c, 2));
}

void APIENTRY glColor3usv(const GLushort *v);
static void x_glColor3usv(MX_NATIVE_CALL *c)
{
    glColor3usv((const GLushort *)I(c, 0));
}

void APIENTRY glColor4b(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha);
static void x_glColor4b(MX_NATIVE_CALL *c)
{
    glColor4b((GLbyte)I(c, 0), (GLbyte)I(c, 1), (GLbyte)I(c, 2), (GLbyte)I(c, 3));
}

void APIENTRY glColor4bv(const GLbyte *v);
static void x_glColor4bv(MX_NATIVE_CALL *c)
{
    glColor4bv((const GLbyte *)I(c, 0));
}

void APIENTRY glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha);
static void x_glColor4d(MX_NATIVE_CALL *c)
{
    glColor4d(D(c, 0), D(c, 1), D(c, 2), D(c, 3));
}

void APIENTRY glColor4dv(const GLdouble *v);
static void x_glColor4dv(MX_NATIVE_CALL *c)
{
    glColor4dv((const GLdouble *)I(c, 0));
}

void APIENTRY glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
static void x_glColor4f(MX_NATIVE_CALL *c)
{
    glColor4f(F(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glColor4fv(const GLfloat *v);
static void x_glColor4fv(MX_NATIVE_CALL *c)
{
    glColor4fv((const GLfloat *)I(c, 0));
}

void APIENTRY glColor4i(GLint red, GLint green, GLint blue, GLint alpha);
static void x_glColor4i(MX_NATIVE_CALL *c)
{
    glColor4i((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3));
}

void APIENTRY glColor4iv(const GLint *v);
static void x_glColor4iv(MX_NATIVE_CALL *c)
{
    glColor4iv((const GLint *)I(c, 0));
}

void APIENTRY glColor4s(GLshort red, GLshort green, GLshort blue, GLshort alpha);
static void x_glColor4s(MX_NATIVE_CALL *c)
{
    glColor4s((GLshort)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2), (GLshort)I(c, 3));
}

void APIENTRY glColor4sv(const GLshort *v);
static void x_glColor4sv(MX_NATIVE_CALL *c)
{
    glColor4sv((const GLshort *)I(c, 0));
}

void APIENTRY glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
static void x_glColor4ub(MX_NATIVE_CALL *c)
{
    glColor4ub((GLubyte)I(c, 0), (GLubyte)I(c, 1), (GLubyte)I(c, 2), (GLubyte)I(c, 3));
}

void APIENTRY glColor4ubv(const GLubyte *v);
static void x_glColor4ubv(MX_NATIVE_CALL *c)
{
    glColor4ubv((const GLubyte *)I(c, 0));
}

void APIENTRY glColor4ui(GLuint red, GLuint green, GLuint blue, GLuint alpha);
static void x_glColor4ui(MX_NATIVE_CALL *c)
{
    glColor4ui((GLuint)I(c, 0), (GLuint)I(c, 1), (GLuint)I(c, 2), (GLuint)I(c, 3));
}

void APIENTRY glColor4uiv(const GLuint *v);
static void x_glColor4uiv(MX_NATIVE_CALL *c)
{
    glColor4uiv((const GLuint *)I(c, 0));
}

void APIENTRY glColor4us(GLushort red, GLushort green, GLushort blue, GLushort alpha);
static void x_glColor4us(MX_NATIVE_CALL *c)
{
    glColor4us((GLushort)I(c, 0), (GLushort)I(c, 1), (GLushort)I(c, 2), (GLushort)I(c, 3));
}

void APIENTRY glColor4usv(const GLushort *v);
static void x_glColor4usv(MX_NATIVE_CALL *c)
{
    glColor4usv((const GLushort *)I(c, 0));
}

void APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
static void x_glColorMask(MX_NATIVE_CALL *c)
{
    glColorMask((GLboolean)I(c, 0), (GLboolean)I(c, 1), (GLboolean)I(c, 2), (GLboolean)I(c, 3));
}

void APIENTRY glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a);
static void x_glColorMaski(MX_NATIVE_CALL *c)
{
    glColorMaski((GLuint)I(c, 0), (GLboolean)I(c, 1), (GLboolean)I(c, 2), (GLboolean)I(c, 3), (GLboolean)I(c, 4));
}

void APIENTRY glColorMaterial(GLenum face, GLenum mode);
static void x_glColorMaterial(MX_NATIVE_CALL *c)
{
    glColorMaterial((GLenum)I(c, 0), (GLenum)I(c, 1));
}

void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
static void x_glColorPointer(MX_NATIVE_CALL *c)
{
    glColorPointer((GLint)I(c, 0), (GLenum)I(c, 1), (GLsizei)I(c, 2), (const void *)I(c, 3));
}

void APIENTRY glCompileShader(GLuint name);
static void x_glCompileShader(MX_NATIVE_CALL *c)
{
    glCompileShader((GLuint)I(c, 0));
}

void APIENTRY glCompressedTexImage2D(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h,                                            GLint border, GLsizei size, const void *data);
static void x_glCompressedTexImage2D(MX_NATIVE_CALL *c)
{
    glCompressedTexImage2D((GLenum)I(c, 0), (GLint)I(c, 1), (GLenum)I(c, 2), (GLsizei)I(c, 3), (GLsizei)I(c, 4), (GLint)I(c, 5), (GLsizei)I(c, 6), (const void *)I(c, 7));
}

void APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,                                               GLenum format, GLsizei size, const void *data);
static void x_glCompressedTexSubImage2D(MX_NATIVE_CALL *c)
{
    glCompressedTexSubImage2D((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3), (GLsizei)I(c, 4), (GLsizei)I(c, 5), (GLenum)I(c, 6), (GLsizei)I(c, 7), (const void *)I(c, 8));
}

void APIENTRY glCopyBufferSubData(GLenum read_target, GLenum write_target, GLintptr read_offset,                                         GLintptr write_offset, GLsizeiptr size);
static void x_glCopyBufferSubData(MX_NATIVE_CALL *c)
{
    glCopyBufferSubData((GLenum)I(c, 0), (GLenum)I(c, 1), (GLintptr)I(c, 2), (GLintptr)I(c, 3), (GLsizeiptr)I(c, 4));
}

void APIENTRY glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type);
static void x_glCopyPixels(MX_NATIVE_CALL *c)
{
    glCopyPixels((GLint)I(c, 0), (GLint)I(c, 1), (GLsizei)I(c, 2), (GLsizei)I(c, 3), (GLenum)I(c, 4));
}

void APIENTRY glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border);
static void x_glCopyTexImage1D(MX_NATIVE_CALL *c)
{
    glCopyTexImage1D((GLenum)I(c, 0), (GLint)I(c, 1), (GLenum)I(c, 2), (GLint)I(c, 3), (GLint)I(c, 4), (GLsizei)I(c, 5), (GLint)I(c, 6));
}

void APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internal, GLint x, GLint y, GLsizei w,                                      GLsizei h, GLint border);
static void x_glCopyTexImage2D(MX_NATIVE_CALL *c)
{
    glCopyTexImage2D((GLenum)I(c, 0), (GLint)I(c, 1), (GLenum)I(c, 2), (GLint)I(c, 3), (GLint)I(c, 4), (GLsizei)I(c, 5), (GLsizei)I(c, 6), (GLint)I(c, 7));
}

void APIENTRY glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
static void x_glCopyTexSubImage1D(MX_NATIVE_CALL *c)
{
    glCopyTexSubImage1D((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3), (GLint)I(c, 4), (GLsizei)I(c, 5));
}

void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint dx, GLint dy, GLint x, GLint y, GLsizei w,                                         GLsizei h);
static void x_glCopyTexSubImage2D(MX_NATIVE_CALL *c)
{
    glCopyTexSubImage2D((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3), (GLint)I(c, 4), (GLint)I(c, 5), (GLsizei)I(c, 6), (GLsizei)I(c, 7));
}

void APIENTRY glCreateBuffers(GLsizei n, GLuint *buffers);
static void x_glCreateBuffers(MX_NATIVE_CALL *c)
{
    glCreateBuffers((GLsizei)I(c, 0), (GLuint *)I(c, 1));
}

GLuint APIENTRY glCreateProgram(void);
static void x_glCreateProgram(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glCreateProgram();
}

GLuint APIENTRY glCreateShader(GLenum type);
static void x_glCreateShader(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glCreateShader((GLenum)I(c, 0));
}

void APIENTRY glCullFace(GLenum mode);
static void x_glCullFace(MX_NATIVE_CALL *c)
{
    glCullFace((GLenum)I(c, 0));
}

void APIENTRY glDebugMessageCallback(GLDEBUGPROC callback, const void *user);
static void x_glDebugMessageCallback(MX_NATIVE_CALL *c)
{
    glDebugMessageCallback((GLDEBUGPROC)I(c, 0), (const void *)I(c, 1));
}

void APIENTRY glDebugMessageControl(GLenum source, GLenum type, GLenum severity, GLsizei count,                                           const GLuint *ids, GLboolean enabled);
static void x_glDebugMessageControl(MX_NATIVE_CALL *c)
{
    glDebugMessageControl((GLenum)I(c, 0), (GLenum)I(c, 1), (GLenum)I(c, 2), (GLsizei)I(c, 3), (const GLuint *)I(c, 4), (GLboolean)I(c, 5));
}

void APIENTRY glDebugMessageInsert(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,                                          const GLchar *buf);
static void x_glDebugMessageInsert(MX_NATIVE_CALL *c)
{
    glDebugMessageInsert((GLenum)I(c, 0), (GLenum)I(c, 1), (GLuint)I(c, 2), (GLenum)I(c, 3), (GLsizei)I(c, 4), (const GLchar *)I(c, 5));
}

void APIENTRY glDeleteBuffers(GLsizei n, const GLuint *buffers);
static void x_glDeleteBuffers(MX_NATIVE_CALL *c)
{
    glDeleteBuffers((GLsizei)I(c, 0), (const GLuint *)I(c, 1));
}

void APIENTRY glDeleteFramebuffers(GLsizei n, const GLuint *names);
static void x_glDeleteFramebuffers(MX_NATIVE_CALL *c)
{
    glDeleteFramebuffers((GLsizei)I(c, 0), (const GLuint *)I(c, 1));
}

void APIENTRY glDeleteLists(GLuint list, GLsizei range);
static void x_glDeleteLists(MX_NATIVE_CALL *c)
{
    glDeleteLists((GLuint)I(c, 0), (GLsizei)I(c, 1));
}

void APIENTRY glDeleteProgram(GLuint name);
static void x_glDeleteProgram(MX_NATIVE_CALL *c)
{
    glDeleteProgram((GLuint)I(c, 0));
}

void APIENTRY glDeleteQueries(GLsizei count, const GLuint *names);
static void x_glDeleteQueries(MX_NATIVE_CALL *c)
{
    glDeleteQueries((GLsizei)I(c, 0), (const GLuint *)I(c, 1));
}

void APIENTRY glDeleteRenderbuffers(GLsizei n, const GLuint *names);
static void x_glDeleteRenderbuffers(MX_NATIVE_CALL *c)
{
    glDeleteRenderbuffers((GLsizei)I(c, 0), (const GLuint *)I(c, 1));
}

void APIENTRY glDeleteShader(GLuint name);
static void x_glDeleteShader(MX_NATIVE_CALL *c)
{
    glDeleteShader((GLuint)I(c, 0));
}

void APIENTRY glDeleteSync(GLsync handle);
static void x_glDeleteSync(MX_NATIVE_CALL *c)
{
    glDeleteSync((GLsync)I(c, 0));
}

void APIENTRY glDeleteTextures(GLsizei n, const GLuint *textures);
static void x_glDeleteTextures(MX_NATIVE_CALL *c)
{
    glDeleteTextures((GLsizei)I(c, 0), (const GLuint *)I(c, 1));
}

void APIENTRY glDeleteVertexArrays(GLsizei n, const GLuint *arrays);
static void x_glDeleteVertexArrays(MX_NATIVE_CALL *c)
{
    glDeleteVertexArrays((GLsizei)I(c, 0), (const GLuint *)I(c, 1));
}

void APIENTRY glDepthFunc(GLenum func);
static void x_glDepthFunc(MX_NATIVE_CALL *c)
{
    glDepthFunc((GLenum)I(c, 0));
}

void APIENTRY glDepthMask(GLboolean flag);
static void x_glDepthMask(MX_NATIVE_CALL *c)
{
    glDepthMask((GLboolean)I(c, 0));
}

void APIENTRY glDepthRange(GLdouble n, GLdouble f);
static void x_glDepthRange(MX_NATIVE_CALL *c)
{
    glDepthRange(D(c, 0), D(c, 1));
}

void APIENTRY glDepthRangef(GLfloat n, GLfloat f);
static void x_glDepthRangef(MX_NATIVE_CALL *c)
{
    glDepthRangef(F(c, 0), F(c, 1));
}

void APIENTRY glDetachShader(GLuint program, GLuint name);
static void x_glDetachShader(MX_NATIVE_CALL *c)
{
    glDetachShader((GLuint)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glDisable(GLenum cap);
static void x_glDisable(MX_NATIVE_CALL *c)
{
    glDisable((GLenum)I(c, 0));
}

void APIENTRY glDisableClientState(GLenum array);
static void x_glDisableClientState(MX_NATIVE_CALL *c)
{
    glDisableClientState((GLenum)I(c, 0));
}

void APIENTRY glDisableVertexAttribArray(GLuint index);
static void x_glDisableVertexAttribArray(MX_NATIVE_CALL *c)
{
    glDisableVertexAttribArray((GLuint)I(c, 0));
}

void APIENTRY glDisablei(GLenum cap, GLuint index);
static void x_glDisablei(MX_NATIVE_CALL *c)
{
    glDisablei((GLenum)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glDiscardFramebufferEXT(GLenum target, GLsizei n, const GLenum *attachments);
static void x_glDiscardFramebufferEXT(MX_NATIVE_CALL *c)
{
    glDiscardFramebufferEXT((GLenum)I(c, 0), (GLsizei)I(c, 1), (const GLenum *)I(c, 2));
}

void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count);
static void x_glDrawArrays(MX_NATIVE_CALL *c)
{
    glDrawArrays((GLenum)I(c, 0), (GLint)I(c, 1), (GLsizei)I(c, 2));
}

void APIENTRY glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances);
static void x_glDrawArraysInstanced(MX_NATIVE_CALL *c)
{
    glDrawArraysInstanced((GLenum)I(c, 0), (GLint)I(c, 1), (GLsizei)I(c, 2), (GLsizei)I(c, 3));
}

void APIENTRY glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei instances,                                                       GLuint base_instance);
static void x_glDrawArraysInstancedBaseInstance(MX_NATIVE_CALL *c)
{
    glDrawArraysInstancedBaseInstance((GLenum)I(c, 0), (GLint)I(c, 1), (GLsizei)I(c, 2), (GLsizei)I(c, 3), (GLuint)I(c, 4));
}

void APIENTRY glDrawBuffer(GLenum buf);
static void x_glDrawBuffer(MX_NATIVE_CALL *c)
{
    glDrawBuffer((GLenum)I(c, 0));
}

void APIENTRY glDrawBuffers(GLsizei n, const GLenum *bufs);
static void x_glDrawBuffers(MX_NATIVE_CALL *c)
{
    glDrawBuffers((GLsizei)I(c, 0), (const GLenum *)I(c, 1));
}

void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
static void x_glDrawElements(MX_NATIVE_CALL *c)
{
    glDrawElements((GLenum)I(c, 0), (GLsizei)I(c, 1), (GLenum)I(c, 2), (const void *)I(c, 3));
}

void APIENTRY glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices,                                              GLint base_vertex);
static void x_glDrawElementsBaseVertex(MX_NATIVE_CALL *c)
{
    glDrawElementsBaseVertex((GLenum)I(c, 0), (GLsizei)I(c, 1), (GLenum)I(c, 2), (const void *)I(c, 3), (GLint)I(c, 4));
}

void APIENTRY glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices,                                             GLsizei instances);
static void x_glDrawElementsInstanced(MX_NATIVE_CALL *c)
{
    glDrawElementsInstanced((GLenum)I(c, 0), (GLsizei)I(c, 1), (GLenum)I(c, 2), (const void *)I(c, 3), (GLsizei)I(c, 4));
}

void APIENTRY glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices,                                                       GLsizei instances, GLint base_vertex);
static void x_glDrawElementsInstancedBaseVertex(MX_NATIVE_CALL *c)
{
    glDrawElementsInstancedBaseVertex((GLenum)I(c, 0), (GLsizei)I(c, 1), (GLenum)I(c, 2), (const void *)I(c, 3), (GLsizei)I(c, 4), (GLint)I(c, 5));
}

void APIENTRY glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
static void x_glDrawPixels(MX_NATIVE_CALL *c)
{
    glDrawPixels((GLsizei)I(c, 0), (GLsizei)I(c, 1), (GLenum)I(c, 2), (GLenum)I(c, 3), (const GLvoid *)I(c, 4));
}

void APIENTRY glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,                                         const void *indices);
static void x_glDrawRangeElements(MX_NATIVE_CALL *c)
{
    glDrawRangeElements((GLenum)I(c, 0), (GLuint)I(c, 1), (GLuint)I(c, 2), (GLsizei)I(c, 3), (GLenum)I(c, 4), (const void *)I(c, 5));
}

void APIENTRY glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,                                                   const void *indices, GLint base_vertex);
static void x_glDrawRangeElementsBaseVertex(MX_NATIVE_CALL *c)
{
    glDrawRangeElementsBaseVertex((GLenum)I(c, 0), (GLuint)I(c, 1), (GLuint)I(c, 2), (GLsizei)I(c, 3), (GLenum)I(c, 4), (const void *)I(c, 5), (GLint)I(c, 6));
}

void APIENTRY glEdgeFlag(GLboolean flag);
static void x_glEdgeFlag(MX_NATIVE_CALL *c)
{
    glEdgeFlag((GLboolean)I(c, 0));
}

void APIENTRY glEdgeFlagPointer(GLsizei stride, const void *pointer);
static void x_glEdgeFlagPointer(MX_NATIVE_CALL *c)
{
    glEdgeFlagPointer((GLsizei)I(c, 0), (const void *)I(c, 1));
}

void APIENTRY glEdgeFlagv(const GLboolean *flag);
static void x_glEdgeFlagv(MX_NATIVE_CALL *c)
{
    glEdgeFlagv((const GLboolean *)I(c, 0));
}

void APIENTRY glEnable(GLenum cap);
static void x_glEnable(MX_NATIVE_CALL *c)
{
    glEnable((GLenum)I(c, 0));
}

void APIENTRY glEnableClientState(GLenum array);
static void x_glEnableClientState(MX_NATIVE_CALL *c)
{
    glEnableClientState((GLenum)I(c, 0));
}

void APIENTRY glEnableVertexAttribArray(GLuint index);
static void x_glEnableVertexAttribArray(MX_NATIVE_CALL *c)
{
    glEnableVertexAttribArray((GLuint)I(c, 0));
}

void APIENTRY glEnablei(GLenum cap, GLuint index);
static void x_glEnablei(MX_NATIVE_CALL *c)
{
    glEnablei((GLenum)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glEnd(void);
static void x_glEnd(MX_NATIVE_CALL *c)
{
    glEnd();
}

void APIENTRY glEndList(void);
static void x_glEndList(MX_NATIVE_CALL *c)
{
    glEndList();
}

void APIENTRY glEndQuery(GLenum target);
static void x_glEndQuery(MX_NATIVE_CALL *c)
{
    glEndQuery((GLenum)I(c, 0));
}

void APIENTRY glEvalCoord1d(GLdouble u);
static void x_glEvalCoord1d(MX_NATIVE_CALL *c)
{
    glEvalCoord1d(D(c, 0));
}

void APIENTRY glEvalCoord1dv(const GLdouble *u);
static void x_glEvalCoord1dv(MX_NATIVE_CALL *c)
{
    glEvalCoord1dv((const GLdouble *)I(c, 0));
}

void APIENTRY glEvalCoord1f(GLfloat u);
static void x_glEvalCoord1f(MX_NATIVE_CALL *c)
{
    glEvalCoord1f(F(c, 0));
}

void APIENTRY glEvalCoord1fv(const GLfloat *u);
static void x_glEvalCoord1fv(MX_NATIVE_CALL *c)
{
    glEvalCoord1fv((const GLfloat *)I(c, 0));
}

void APIENTRY glEvalCoord2d(GLdouble u, GLdouble v);
static void x_glEvalCoord2d(MX_NATIVE_CALL *c)
{
    glEvalCoord2d(D(c, 0), D(c, 1));
}

void APIENTRY glEvalCoord2dv(const GLdouble *u);
static void x_glEvalCoord2dv(MX_NATIVE_CALL *c)
{
    glEvalCoord2dv((const GLdouble *)I(c, 0));
}

void APIENTRY glEvalCoord2f(GLfloat u, GLfloat v);
static void x_glEvalCoord2f(MX_NATIVE_CALL *c)
{
    glEvalCoord2f(F(c, 0), F(c, 1));
}

void APIENTRY glEvalCoord2fv(const GLfloat *u);
static void x_glEvalCoord2fv(MX_NATIVE_CALL *c)
{
    glEvalCoord2fv((const GLfloat *)I(c, 0));
}

void APIENTRY glEvalMesh1(GLenum mode, GLint i1, GLint i2);
static void x_glEvalMesh1(MX_NATIVE_CALL *c)
{
    glEvalMesh1((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2);
static void x_glEvalMesh2(MX_NATIVE_CALL *c)
{
    glEvalMesh2((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3), (GLint)I(c, 4));
}

void APIENTRY glEvalPoint1(GLint i);
static void x_glEvalPoint1(MX_NATIVE_CALL *c)
{
    glEvalPoint1((GLint)I(c, 0));
}

void APIENTRY glEvalPoint2(GLint i, GLint j);
static void x_glEvalPoint2(MX_NATIVE_CALL *c)
{
    glEvalPoint2((GLint)I(c, 0), (GLint)I(c, 1));
}

void APIENTRY glFeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer);
static void x_glFeedbackBuffer(MX_NATIVE_CALL *c)
{
    glFeedbackBuffer((GLsizei)I(c, 0), (GLenum)I(c, 1), (GLfloat *)I(c, 2));
}

GLsync APIENTRY glFenceSync(GLenum condition, GLbitfield flags);
static void x_glFenceSync(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glFenceSync((GLenum)I(c, 0), (GLbitfield)I(c, 1));
}

void APIENTRY glFinish(void);
static void x_glFinish(MX_NATIVE_CALL *c)
{
    glFinish();
}

void APIENTRY glFlush(void);
static void x_glFlush(MX_NATIVE_CALL *c)
{
    glFlush();
}

void APIENTRY glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length);
static void x_glFlushMappedBufferRange(MX_NATIVE_CALL *c)
{
    glFlushMappedBufferRange((GLenum)I(c, 0), (GLintptr)I(c, 1), (GLsizeiptr)I(c, 2));
}

void APIENTRY glFogf(GLenum pname, GLfloat param);
static void x_glFogf(MX_NATIVE_CALL *c)
{
    glFogf((GLenum)I(c, 0), F(c, 1));
}

void APIENTRY glFogfv(GLenum pname, const GLfloat *params);
static void x_glFogfv(MX_NATIVE_CALL *c)
{
    glFogfv((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glFogi(GLenum pname, GLint param);
static void x_glFogi(MX_NATIVE_CALL *c)
{
    glFogi((GLenum)I(c, 0), (GLint)I(c, 1));
}

void APIENTRY glFogiv(GLenum pname, const GLint *params);
static void x_glFogiv(MX_NATIVE_CALL *c)
{
    glFogiv((GLenum)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glFramebufferRenderbuffer(GLenum target, GLenum point, GLenum rbtarget, GLuint rb);
static void x_glFramebufferRenderbuffer(MX_NATIVE_CALL *c)
{
    glFramebufferRenderbuffer((GLenum)I(c, 0), (GLenum)I(c, 1), (GLenum)I(c, 2), (GLuint)I(c, 3));
}

void APIENTRY glFramebufferTexture(GLenum target, GLenum point, GLuint texture, GLint level);
static void x_glFramebufferTexture(MX_NATIVE_CALL *c)
{
    glFramebufferTexture((GLenum)I(c, 0), (GLenum)I(c, 1), (GLuint)I(c, 2), (GLint)I(c, 3));
}

void APIENTRY glFramebufferTexture1D(GLenum target, GLenum point, GLenum textarget, GLuint texture, GLint level);
static void x_glFramebufferTexture1D(MX_NATIVE_CALL *c)
{
    glFramebufferTexture1D((GLenum)I(c, 0), (GLenum)I(c, 1), (GLenum)I(c, 2), (GLuint)I(c, 3), (GLint)I(c, 4));
}

void APIENTRY glFramebufferTexture2D(GLenum target, GLenum point, GLenum textarget, GLuint texture, GLint level);
static void x_glFramebufferTexture2D(MX_NATIVE_CALL *c)
{
    glFramebufferTexture2D((GLenum)I(c, 0), (GLenum)I(c, 1), (GLenum)I(c, 2), (GLuint)I(c, 3), (GLint)I(c, 4));
}

void APIENTRY glFramebufferTextureLayer(GLenum target, GLenum point, GLuint texture, GLint level, GLint layer);
static void x_glFramebufferTextureLayer(MX_NATIVE_CALL *c)
{
    glFramebufferTextureLayer((GLenum)I(c, 0), (GLenum)I(c, 1), (GLuint)I(c, 2), (GLint)I(c, 3), (GLint)I(c, 4));
}

void APIENTRY glFrontFace(GLenum mode);
static void x_glFrontFace(MX_NATIVE_CALL *c)
{
    glFrontFace((GLenum)I(c, 0));
}

void APIENTRY glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top_, GLdouble near_val,                               GLdouble far_val);
static void x_glFrustum(MX_NATIVE_CALL *c)
{
    glFrustum(D(c, 0), D(c, 1), D(c, 2), D(c, 3), D(c, 4), D(c, 5));
}

void APIENTRY glGenBuffers(GLsizei n, GLuint *buffers);
static void x_glGenBuffers(MX_NATIVE_CALL *c)
{
    glGenBuffers((GLsizei)I(c, 0), (GLuint *)I(c, 1));
}

void APIENTRY glGenFramebuffers(GLsizei n, GLuint *names);
static void x_glGenFramebuffers(MX_NATIVE_CALL *c)
{
    glGenFramebuffers((GLsizei)I(c, 0), (GLuint *)I(c, 1));
}

GLuint APIENTRY glGenLists(GLsizei range);
static void x_glGenLists(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glGenLists((GLsizei)I(c, 0));
}

void APIENTRY glGenQueries(GLsizei count, GLuint *names);
static void x_glGenQueries(MX_NATIVE_CALL *c)
{
    glGenQueries((GLsizei)I(c, 0), (GLuint *)I(c, 1));
}

void APIENTRY glGenRenderbuffers(GLsizei n, GLuint *names);
static void x_glGenRenderbuffers(MX_NATIVE_CALL *c)
{
    glGenRenderbuffers((GLsizei)I(c, 0), (GLuint *)I(c, 1));
}

void APIENTRY glGenTextures(GLsizei n, GLuint *textures);
static void x_glGenTextures(MX_NATIVE_CALL *c)
{
    glGenTextures((GLsizei)I(c, 0), (GLuint *)I(c, 1));
}

void APIENTRY glGenVertexArrays(GLsizei n, GLuint *arrays);
static void x_glGenVertexArrays(MX_NATIVE_CALL *c)
{
    glGenVertexArrays((GLsizei)I(c, 0), (GLuint *)I(c, 1));
}

void APIENTRY glGenerateMipmap(GLenum target);
static void x_glGenerateMipmap(MX_NATIVE_CALL *c)
{
    glGenerateMipmap((GLenum)I(c, 0));
}

void APIENTRY glGetActiveAttrib(GLuint name, GLuint index, GLsizei size, GLsizei *length, GLint *count,                                       GLenum *type, GLchar *out);
static void x_glGetActiveAttrib(MX_NATIVE_CALL *c)
{
    glGetActiveAttrib((GLuint)I(c, 0), (GLuint)I(c, 1), (GLsizei)I(c, 2), (GLsizei *)I(c, 3), (GLint *)I(c, 4), (GLenum *)I(c, 5), (GLchar *)I(c, 6));
}

void APIENTRY glGetActiveUniform(GLuint name, GLuint index, GLsizei size, GLsizei *length, GLint *count,                                        GLenum *type, GLchar *out);
static void x_glGetActiveUniform(MX_NATIVE_CALL *c)
{
    glGetActiveUniform((GLuint)I(c, 0), (GLuint)I(c, 1), (GLsizei)I(c, 2), (GLsizei *)I(c, 3), (GLint *)I(c, 4), (GLenum *)I(c, 5), (GLchar *)I(c, 6));
}

void APIENTRY glGetActiveUniformBlockName(GLuint program, GLuint index, GLsizei size, GLsizei *length, GLchar *name);
static void x_glGetActiveUniformBlockName(MX_NATIVE_CALL *c)
{
    glGetActiveUniformBlockName((GLuint)I(c, 0), (GLuint)I(c, 1), (GLsizei)I(c, 2), (GLsizei *)I(c, 3), (GLchar *)I(c, 4));
}

void APIENTRY glGetActiveUniformBlockiv(GLuint program, GLuint index, GLenum pname, GLint *params);
static void x_glGetActiveUniformBlockiv(MX_NATIVE_CALL *c)
{
    glGetActiveUniformBlockiv((GLuint)I(c, 0), (GLuint)I(c, 1), (GLenum)I(c, 2), (GLint *)I(c, 3));
}

void APIENTRY glGetActiveUniformsiv(GLuint name, GLsizei count, const GLuint *indices, GLenum pname,                                           GLint *params);
static void x_glGetActiveUniformsiv(MX_NATIVE_CALL *c)
{
    glGetActiveUniformsiv((GLuint)I(c, 0), (GLsizei)I(c, 1), (const GLuint *)I(c, 2), (GLenum)I(c, 3), (GLint *)I(c, 4));
}

void APIENTRY glGetAttachedShaders(GLuint program, GLsizei max, GLsizei *count, GLuint *shaders);
static void x_glGetAttachedShaders(MX_NATIVE_CALL *c)
{
    glGetAttachedShaders((GLuint)I(c, 0), (GLsizei)I(c, 1), (GLsizei *)I(c, 2), (GLuint *)I(c, 3));
}

GLint APIENTRY glGetAttribLocation(GLuint name, const GLchar *attrib);
static void x_glGetAttribLocation(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glGetAttribLocation((GLuint)I(c, 0), (const GLchar *)I(c, 1));
}

void APIENTRY glGetBooleani_v(GLenum target, GLuint index, GLboolean *data);
static void x_glGetBooleani_v(MX_NATIVE_CALL *c)
{
    glGetBooleani_v((GLenum)I(c, 0), (GLuint)I(c, 1), (GLboolean *)I(c, 2));
}

void APIENTRY glGetBooleanv(GLenum pname, GLboolean *data);
static void x_glGetBooleanv(MX_NATIVE_CALL *c)
{
    glGetBooleanv((GLenum)I(c, 0), (GLboolean *)I(c, 1));
}

void APIENTRY glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params);
static void x_glGetBufferParameteri64v(MX_NATIVE_CALL *c)
{
    glGetBufferParameteri64v((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint64 *)I(c, 2));
}

void APIENTRY glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params);
static void x_glGetBufferParameteriv(MX_NATIVE_CALL *c)
{
    glGetBufferParameteriv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetBufferPointerv(GLenum target, GLenum pname, void **params);
static void x_glGetBufferPointerv(MX_NATIVE_CALL *c)
{
    glGetBufferPointerv((GLenum)I(c, 0), (GLenum)I(c, 1), (void **)I(c, 2));
}

void APIENTRY glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void *data);
static void x_glGetBufferSubData(MX_NATIVE_CALL *c)
{
    glGetBufferSubData((GLenum)I(c, 0), (GLintptr)I(c, 1), (GLsizeiptr)I(c, 2), (void *)I(c, 3));
}

void APIENTRY glGetClipPlane(GLenum plane, GLdouble *equation);
static void x_glGetClipPlane(MX_NATIVE_CALL *c)
{
    glGetClipPlane((GLenum)I(c, 0), (GLdouble *)I(c, 1));
}

GLuint APIENTRY glGetDebugMessageLog(GLuint count, GLsizei size, GLenum *sources, GLenum *types, GLuint *ids,                                            GLenum *severities, GLsizei *lengths, GLchar *log);
static void x_glGetDebugMessageLog(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glGetDebugMessageLog((GLuint)I(c, 0), (GLsizei)I(c, 1), (GLenum *)I(c, 2), (GLenum *)I(c, 3), (GLuint *)I(c, 4), (GLenum *)I(c, 5), (GLsizei *)I(c, 6), (GLchar *)I(c, 7));
}

void APIENTRY glGetDoublev(GLenum pname, GLdouble *data);
static void x_glGetDoublev(MX_NATIVE_CALL *c)
{
    glGetDoublev((GLenum)I(c, 0), (GLdouble *)I(c, 1));
}

GLenum APIENTRY glGetError(void);
static void x_glGetError(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glGetError();
}

void APIENTRY glGetFloatv(GLenum pname, GLfloat *data);
static void x_glGetFloatv(MX_NATIVE_CALL *c)
{
    glGetFloatv((GLenum)I(c, 0), (GLfloat *)I(c, 1));
}

void APIENTRY glGetFramebufferAttachmentParameteriv(GLenum target, GLenum point, GLenum pname, GLint *params);
static void x_glGetFramebufferAttachmentParameteriv(MX_NATIVE_CALL *c)
{
    glGetFramebufferAttachmentParameteriv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLenum)I(c, 2), (GLint *)I(c, 3));
}

void APIENTRY glGetInteger64v(GLenum pname, GLint64 *data);
static void x_glGetInteger64v(MX_NATIVE_CALL *c)
{
    glGetInteger64v((GLenum)I(c, 0), (GLint64 *)I(c, 1));
}

void APIENTRY glGetIntegeri_v(GLenum target, GLuint index, GLint *data);
static void x_glGetIntegeri_v(MX_NATIVE_CALL *c)
{
    glGetIntegeri_v((GLenum)I(c, 0), (GLuint)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetIntegerv(GLenum pname, GLint *data);
static void x_glGetIntegerv(MX_NATIVE_CALL *c)
{
    glGetIntegerv((GLenum)I(c, 0), (GLint *)I(c, 1));
}

void APIENTRY glGetLightfv(GLenum l, GLenum pname, GLfloat *params);
static void x_glGetLightfv(MX_NATIVE_CALL *c)
{
    glGetLightfv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLfloat *)I(c, 2));
}

void APIENTRY glGetLightiv(GLenum l, GLenum pname, GLint *params);
static void x_glGetLightiv(MX_NATIVE_CALL *c)
{
    glGetLightiv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetMapdv(GLenum target, GLenum query, GLdouble *v);
static void x_glGetMapdv(MX_NATIVE_CALL *c)
{
    glGetMapdv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLdouble *)I(c, 2));
}

void APIENTRY glGetMapfv(GLenum target, GLenum query, GLfloat *v);
static void x_glGetMapfv(MX_NATIVE_CALL *c)
{
    glGetMapfv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLfloat *)I(c, 2));
}

void APIENTRY glGetMapiv(GLenum target, GLenum query, GLint *v);
static void x_glGetMapiv(MX_NATIVE_CALL *c)
{
    glGetMapiv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params);
static void x_glGetMaterialfv(MX_NATIVE_CALL *c)
{
    glGetMaterialfv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLfloat *)I(c, 2));
}

void APIENTRY glGetMaterialiv(GLenum face, GLenum pname, GLint *params);
static void x_glGetMaterialiv(MX_NATIVE_CALL *c)
{
    glGetMaterialiv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetPixelMapfv(GLenum map, GLfloat *values);
static void x_glGetPixelMapfv(MX_NATIVE_CALL *c)
{
    glGetPixelMapfv((GLenum)I(c, 0), (GLfloat *)I(c, 1));
}

void APIENTRY glGetPixelMapuiv(GLenum map, GLuint *values);
static void x_glGetPixelMapuiv(MX_NATIVE_CALL *c)
{
    glGetPixelMapuiv((GLenum)I(c, 0), (GLuint *)I(c, 1));
}

void APIENTRY glGetPixelMapusv(GLenum map, GLushort *values);
static void x_glGetPixelMapusv(MX_NATIVE_CALL *c)
{
    glGetPixelMapusv((GLenum)I(c, 0), (GLushort *)I(c, 1));
}

void APIENTRY glGetPointerv(GLenum pname, void **params);
static void x_glGetPointerv(MX_NATIVE_CALL *c)
{
    glGetPointerv((GLenum)I(c, 0), (void **)I(c, 1));
}

void APIENTRY glGetPolygonStipple(GLubyte *mask);
static void x_glGetPolygonStipple(MX_NATIVE_CALL *c)
{
    glGetPolygonStipple((GLubyte *)I(c, 0));
}

void APIENTRY glGetProgramInfoLog(GLuint name, GLsizei size, GLsizei *length, GLchar *log);
static void x_glGetProgramInfoLog(MX_NATIVE_CALL *c)
{
    glGetProgramInfoLog((GLuint)I(c, 0), (GLsizei)I(c, 1), (GLsizei *)I(c, 2), (GLchar *)I(c, 3));
}

void APIENTRY glGetProgramiv(GLuint name, GLenum pname, GLint *params);
static void x_glGetProgramiv(MX_NATIVE_CALL *c)
{
    glGetProgramiv((GLuint)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetQueryObjecti64v(GLuint name, GLenum pname, GLint64 *value);
static void x_glGetQueryObjecti64v(MX_NATIVE_CALL *c)
{
    glGetQueryObjecti64v((GLuint)I(c, 0), (GLenum)I(c, 1), (GLint64 *)I(c, 2));
}

void APIENTRY glGetQueryObjectiv(GLuint name, GLenum pname, GLint *value);
static void x_glGetQueryObjectiv(MX_NATIVE_CALL *c)
{
    glGetQueryObjectiv((GLuint)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetQueryObjectui64v(GLuint name, GLenum pname, GLuint64 *value);
static void x_glGetQueryObjectui64v(MX_NATIVE_CALL *c)
{
    glGetQueryObjectui64v((GLuint)I(c, 0), (GLenum)I(c, 1), (GLuint64 *)I(c, 2));
}

void APIENTRY glGetQueryObjectuiv(GLuint name, GLenum pname, GLuint *value);
static void x_glGetQueryObjectuiv(MX_NATIVE_CALL *c)
{
    glGetQueryObjectuiv((GLuint)I(c, 0), (GLenum)I(c, 1), (GLuint *)I(c, 2));
}

void APIENTRY glGetQueryiv(GLenum target, GLenum pname, GLint *value);
static void x_glGetQueryiv(MX_NATIVE_CALL *c)
{
    glGetQueryiv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params);
static void x_glGetRenderbufferParameteriv(MX_NATIVE_CALL *c)
{
    glGetRenderbufferParameteriv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetShaderInfoLog(GLuint name, GLsizei size, GLsizei *length, GLchar *log);
static void x_glGetShaderInfoLog(MX_NATIVE_CALL *c)
{
    glGetShaderInfoLog((GLuint)I(c, 0), (GLsizei)I(c, 1), (GLsizei *)I(c, 2), (GLchar *)I(c, 3));
}

void APIENTRY glGetShaderPrecisionFormat(GLenum shader_type, GLenum precision, GLint *range, GLint *prec);
static void x_glGetShaderPrecisionFormat(MX_NATIVE_CALL *c)
{
    glGetShaderPrecisionFormat((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2), (GLint *)I(c, 3));
}

void APIENTRY glGetShaderSource(GLuint name, GLsizei size, GLsizei *length, GLchar *source);
static void x_glGetShaderSource(MX_NATIVE_CALL *c)
{
    glGetShaderSource((GLuint)I(c, 0), (GLsizei)I(c, 1), (GLsizei *)I(c, 2), (GLchar *)I(c, 3));
}

void APIENTRY glGetShaderiv(GLuint name, GLenum pname, GLint *params);
static void x_glGetShaderiv(MX_NATIVE_CALL *c)
{
    glGetShaderiv((GLuint)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

const GLubyte* APIENTRY glGetString(GLenum name);
static void x_glGetString(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glGetString((GLenum)I(c, 0));
}

const GLubyte* APIENTRY glGetStringi(GLenum name, GLuint index);
static void x_glGetStringi(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glGetStringi((GLenum)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glGetSynciv(GLsync handle, GLenum pname, GLsizei size, GLsizei *length, GLint *values);
static void x_glGetSynciv(MX_NATIVE_CALL *c)
{
    glGetSynciv((GLsync)I(c, 0), (GLenum)I(c, 1), (GLsizei)I(c, 2), (GLsizei *)I(c, 3), (GLint *)I(c, 4));
}

void APIENTRY glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params);
static void x_glGetTexEnvfv(MX_NATIVE_CALL *c)
{
    glGetTexEnvfv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLfloat *)I(c, 2));
}

void APIENTRY glGetTexEnviv(GLenum target, GLenum pname, GLint *params);
static void x_glGetTexEnviv(MX_NATIVE_CALL *c)
{
    glGetTexEnviv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetTexGendv(GLenum coord, GLenum pname, GLdouble *params);
static void x_glGetTexGendv(MX_NATIVE_CALL *c)
{
    glGetTexGendv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLdouble *)I(c, 2));
}

void APIENTRY glGetTexGenfv(GLenum coord, GLenum pname, GLfloat *params);
static void x_glGetTexGenfv(MX_NATIVE_CALL *c)
{
    glGetTexGenfv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLfloat *)I(c, 2));
}

void APIENTRY glGetTexGeniv(GLenum coord, GLenum pname, GLint *params);
static void x_glGetTexGeniv(MX_NATIVE_CALL *c)
{
    glGetTexGeniv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels);
static void x_glGetTexImage(MX_NATIVE_CALL *c)
{
    glGetTexImage((GLenum)I(c, 0), (GLint)I(c, 1), (GLenum)I(c, 2), (GLenum)I(c, 3), (void *)I(c, 4));
}

void APIENTRY glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params);
static void x_glGetTexLevelParameterfv(MX_NATIVE_CALL *c)
{
    glGetTexLevelParameterfv((GLenum)I(c, 0), (GLint)I(c, 1), (GLenum)I(c, 2), (GLfloat *)I(c, 3));
}

void APIENTRY glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params);
static void x_glGetTexLevelParameteriv(MX_NATIVE_CALL *c)
{
    glGetTexLevelParameteriv((GLenum)I(c, 0), (GLint)I(c, 1), (GLenum)I(c, 2), (GLint *)I(c, 3));
}

void APIENTRY glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params);
static void x_glGetTexParameterfv(MX_NATIVE_CALL *c)
{
    glGetTexParameterfv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLfloat *)I(c, 2));
}

void APIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint *params);
static void x_glGetTexParameteriv(MX_NATIVE_CALL *c)
{
    glGetTexParameteriv((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

GLuint APIENTRY glGetUniformBlockIndex(GLuint program, const GLchar *name);
static void x_glGetUniformBlockIndex(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glGetUniformBlockIndex((GLuint)I(c, 0), (const GLchar *)I(c, 1));
}

GLint APIENTRY glGetUniformLocation(GLuint name, const GLchar *uniform);
static void x_glGetUniformLocation(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glGetUniformLocation((GLuint)I(c, 0), (const GLchar *)I(c, 1));
}

void APIENTRY glGetUniformfv(GLuint name, GLint location, GLfloat *params);
static void x_glGetUniformfv(MX_NATIVE_CALL *c)
{
    glGetUniformfv((GLuint)I(c, 0), (GLint)I(c, 1), (GLfloat *)I(c, 2));
}

void APIENTRY glGetUniformiv(GLuint name, GLint location, GLint *params);
static void x_glGetUniformiv(MX_NATIVE_CALL *c)
{
    glGetUniformiv((GLuint)I(c, 0), (GLint)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetVertexAttribIiv(GLuint index, GLenum pname, GLint *params);
static void x_glGetVertexAttribIiv(MX_NATIVE_CALL *c)
{
    glGetVertexAttribIiv((GLuint)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params);
static void x_glGetVertexAttribIuiv(MX_NATIVE_CALL *c)
{
    glGetVertexAttribIuiv((GLuint)I(c, 0), (GLenum)I(c, 1), (GLuint *)I(c, 2));
}

void APIENTRY glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer);
static void x_glGetVertexAttribPointerv(MX_NATIVE_CALL *c)
{
    glGetVertexAttribPointerv((GLuint)I(c, 0), (GLenum)I(c, 1), (void **)I(c, 2));
}

void APIENTRY glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params);
static void x_glGetVertexAttribfv(MX_NATIVE_CALL *c)
{
    glGetVertexAttribfv((GLuint)I(c, 0), (GLenum)I(c, 1), (GLfloat *)I(c, 2));
}

void APIENTRY glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params);
static void x_glGetVertexAttribiv(MX_NATIVE_CALL *c)
{
    glGetVertexAttribiv((GLuint)I(c, 0), (GLenum)I(c, 1), (GLint *)I(c, 2));
}

void APIENTRY glHint(GLenum target, GLenum mode);
static void x_glHint(MX_NATIVE_CALL *c)
{
    glHint((GLenum)I(c, 0), (GLenum)I(c, 1));
}

void APIENTRY glIndexMask(GLuint mask);
static void x_glIndexMask(MX_NATIVE_CALL *c)
{
    glIndexMask((GLuint)I(c, 0));
}

void APIENTRY glIndexPointer(GLenum type, GLsizei stride, const void *pointer);
static void x_glIndexPointer(MX_NATIVE_CALL *c)
{
    glIndexPointer((GLenum)I(c, 0), (GLsizei)I(c, 1), (const void *)I(c, 2));
}

void APIENTRY glIndexd(GLdouble c);
static void x_glIndexd(MX_NATIVE_CALL *c)
{
    glIndexd(D(c, 0));
}

void APIENTRY glIndexdv(const GLdouble *c);
static void x_glIndexdv(MX_NATIVE_CALL *c)
{
    glIndexdv((const GLdouble *)I(c, 0));
}

void APIENTRY glIndexf(GLfloat c);
static void x_glIndexf(MX_NATIVE_CALL *c)
{
    glIndexf(F(c, 0));
}

void APIENTRY glIndexfv(const GLfloat *c);
static void x_glIndexfv(MX_NATIVE_CALL *c)
{
    glIndexfv((const GLfloat *)I(c, 0));
}

void APIENTRY glIndexi(GLint c);
static void x_glIndexi(MX_NATIVE_CALL *c)
{
    glIndexi((GLint)I(c, 0));
}

void APIENTRY glIndexiv(const GLint *c);
static void x_glIndexiv(MX_NATIVE_CALL *c)
{
    glIndexiv((const GLint *)I(c, 0));
}

void APIENTRY glIndexs(GLshort c);
static void x_glIndexs(MX_NATIVE_CALL *c)
{
    glIndexs((GLshort)I(c, 0));
}

void APIENTRY glIndexsv(const GLshort *c);
static void x_glIndexsv(MX_NATIVE_CALL *c)
{
    glIndexsv((const GLshort *)I(c, 0));
}

void APIENTRY glIndexub(GLubyte c);
static void x_glIndexub(MX_NATIVE_CALL *c)
{
    glIndexub((GLubyte)I(c, 0));
}

void APIENTRY glIndexubv(const GLubyte *c);
static void x_glIndexubv(MX_NATIVE_CALL *c)
{
    glIndexubv((const GLubyte *)I(c, 0));
}

void APIENTRY glInitNames(void);
static void x_glInitNames(MX_NATIVE_CALL *c)
{
    glInitNames();
}

void APIENTRY glInterleavedArrays(GLenum format, GLsizei stride, const void *pointer);
static void x_glInterleavedArrays(MX_NATIVE_CALL *c)
{
    glInterleavedArrays((GLenum)I(c, 0), (GLsizei)I(c, 1), (const void *)I(c, 2));
}

void APIENTRY glInvalidateFramebuffer(GLenum target, GLsizei n, const GLenum *attachments);
static void x_glInvalidateFramebuffer(MX_NATIVE_CALL *c)
{
    glInvalidateFramebuffer((GLenum)I(c, 0), (GLsizei)I(c, 1), (const GLenum *)I(c, 2));
}

GLboolean APIENTRY glIsBuffer(GLuint name);
static void x_glIsBuffer(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glIsBuffer((GLuint)I(c, 0));
}

GLboolean APIENTRY glIsEnabled(GLenum cap);
static void x_glIsEnabled(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glIsEnabled((GLenum)I(c, 0));
}

GLboolean APIENTRY glIsEnabledi(GLenum cap, GLuint index);
static void x_glIsEnabledi(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glIsEnabledi((GLenum)I(c, 0), (GLuint)I(c, 1));
}

GLboolean APIENTRY glIsFramebuffer(GLuint name);
static void x_glIsFramebuffer(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glIsFramebuffer((GLuint)I(c, 0));
}

GLboolean APIENTRY glIsList(GLuint list);
static void x_glIsList(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glIsList((GLuint)I(c, 0));
}

GLboolean APIENTRY glIsProgram(GLuint name);
static void x_glIsProgram(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glIsProgram((GLuint)I(c, 0));
}

GLboolean APIENTRY glIsQuery(GLuint name);
static void x_glIsQuery(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glIsQuery((GLuint)I(c, 0));
}

GLboolean APIENTRY glIsRenderbuffer(GLuint name);
static void x_glIsRenderbuffer(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glIsRenderbuffer((GLuint)I(c, 0));
}

GLboolean APIENTRY glIsShader(GLuint name);
static void x_glIsShader(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glIsShader((GLuint)I(c, 0));
}

GLboolean APIENTRY glIsSync(GLsync handle);
static void x_glIsSync(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glIsSync((GLsync)I(c, 0));
}

GLboolean APIENTRY glIsTexture(GLuint name);
static void x_glIsTexture(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glIsTexture((GLuint)I(c, 0));
}

GLboolean APIENTRY glIsVertexArray(GLuint name);
static void x_glIsVertexArray(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glIsVertexArray((GLuint)I(c, 0));
}

void APIENTRY glLightModelf(GLenum pname, GLfloat param);
static void x_glLightModelf(MX_NATIVE_CALL *c)
{
    glLightModelf((GLenum)I(c, 0), F(c, 1));
}

void APIENTRY glLightModelfv(GLenum pname, const GLfloat *params);
static void x_glLightModelfv(MX_NATIVE_CALL *c)
{
    glLightModelfv((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glLightModeli(GLenum pname, GLint param);
static void x_glLightModeli(MX_NATIVE_CALL *c)
{
    glLightModeli((GLenum)I(c, 0), (GLint)I(c, 1));
}

void APIENTRY glLightModeliv(GLenum pname, const GLint *params);
static void x_glLightModeliv(MX_NATIVE_CALL *c)
{
    glLightModeliv((GLenum)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glLightf(GLenum l, GLenum pname, GLfloat param);
static void x_glLightf(MX_NATIVE_CALL *c)
{
    glLightf((GLenum)I(c, 0), (GLenum)I(c, 1), F(c, 2));
}

void APIENTRY glLightfv(GLenum l, GLenum pname, const GLfloat *params);
static void x_glLightfv(MX_NATIVE_CALL *c)
{
    glLightfv((GLenum)I(c, 0), (GLenum)I(c, 1), (const GLfloat *)I(c, 2));
}

void APIENTRY glLighti(GLenum l, GLenum pname, GLint param);
static void x_glLighti(MX_NATIVE_CALL *c)
{
    glLighti((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glLightiv(GLenum l, GLenum pname, const GLint *params);
static void x_glLightiv(MX_NATIVE_CALL *c)
{
    glLightiv((GLenum)I(c, 0), (GLenum)I(c, 1), (const GLint *)I(c, 2));
}

void APIENTRY glLineStipple(GLint factor, GLushort pattern);
static void x_glLineStipple(MX_NATIVE_CALL *c)
{
    glLineStipple((GLint)I(c, 0), (GLushort)I(c, 1));
}

void APIENTRY glLineWidth(GLfloat width);
static void x_glLineWidth(MX_NATIVE_CALL *c)
{
    glLineWidth(F(c, 0));
}

void APIENTRY glLinkProgram(GLuint name);
static void x_glLinkProgram(MX_NATIVE_CALL *c)
{
    glLinkProgram((GLuint)I(c, 0));
}

void APIENTRY glListBase(GLuint base);
static void x_glListBase(MX_NATIVE_CALL *c)
{
    glListBase((GLuint)I(c, 0));
}

void APIENTRY glLoadIdentity(void);
static void x_glLoadIdentity(MX_NATIVE_CALL *c)
{
    glLoadIdentity();
}

void APIENTRY glLoadMatrixd(const GLdouble *m);
static void x_glLoadMatrixd(MX_NATIVE_CALL *c)
{
    glLoadMatrixd((const GLdouble *)I(c, 0));
}

void APIENTRY glLoadMatrixf(const GLfloat *m);
static void x_glLoadMatrixf(MX_NATIVE_CALL *c)
{
    glLoadMatrixf((const GLfloat *)I(c, 0));
}

void APIENTRY glLoadName(GLuint name);
static void x_glLoadName(MX_NATIVE_CALL *c)
{
    glLoadName((GLuint)I(c, 0));
}

void APIENTRY glLoadTransposeMatrixd(const GLdouble *m);
static void x_glLoadTransposeMatrixd(MX_NATIVE_CALL *c)
{
    glLoadTransposeMatrixd((const GLdouble *)I(c, 0));
}

void APIENTRY glLoadTransposeMatrixdARB(const GLdouble *m);
static void x_glLoadTransposeMatrixdARB(MX_NATIVE_CALL *c)
{
    glLoadTransposeMatrixdARB((const GLdouble *)I(c, 0));
}

void APIENTRY glLoadTransposeMatrixf(const GLfloat *m);
static void x_glLoadTransposeMatrixf(MX_NATIVE_CALL *c)
{
    glLoadTransposeMatrixf((const GLfloat *)I(c, 0));
}

void APIENTRY glLoadTransposeMatrixfARB(const GLfloat *m);
static void x_glLoadTransposeMatrixfARB(MX_NATIVE_CALL *c)
{
    glLoadTransposeMatrixfARB((const GLfloat *)I(c, 0));
}

void APIENTRY glLogicOp(GLenum opcode);
static void x_glLogicOp(MX_NATIVE_CALL *c)
{
    glLogicOp((GLenum)I(c, 0));
}

void APIENTRY glMap1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points);
static void x_glMap1d(MX_NATIVE_CALL *c)
{
    glMap1d((GLenum)I(c, 0), D(c, 1), D(c, 2), (GLint)I(c, 3), (GLint)I(c, 4), (const GLdouble *)I(c, 5));
}

void APIENTRY glMap1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points);
static void x_glMap1f(MX_NATIVE_CALL *c)
{
    glMap1f((GLenum)I(c, 0), F(c, 1), F(c, 2), (GLint)I(c, 3), (GLint)I(c, 4), (const GLfloat *)I(c, 5));
}

void APIENTRY glMap2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points);
static void x_glMap2d(MX_NATIVE_CALL *c)
{
    glMap2d((GLenum)I(c, 0), D(c, 1), D(c, 2), (GLint)I(c, 3), (GLint)I(c, 4), D(c, 5), D(c, 6), (GLint)I(c, 7), (GLint)I(c, 8), (const GLdouble *)I(c, 9));
}

void APIENTRY glMap2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points);
static void x_glMap2f(MX_NATIVE_CALL *c)
{
    glMap2f((GLenum)I(c, 0), F(c, 1), F(c, 2), (GLint)I(c, 3), (GLint)I(c, 4), F(c, 5), F(c, 6), (GLint)I(c, 7), (GLint)I(c, 8), (const GLfloat *)I(c, 9));
}

void* APIENTRY glMapBuffer(GLenum target, GLenum access);
static void x_glMapBuffer(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glMapBuffer((GLenum)I(c, 0), (GLenum)I(c, 1));
}

void* APIENTRY glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
static void x_glMapBufferRange(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glMapBufferRange((GLenum)I(c, 0), (GLintptr)I(c, 1), (GLsizeiptr)I(c, 2), (GLbitfield)I(c, 3));
}

void APIENTRY glMapGrid1d(GLint un, GLdouble u1, GLdouble u2);
static void x_glMapGrid1d(MX_NATIVE_CALL *c)
{
    glMapGrid1d((GLint)I(c, 0), D(c, 1), D(c, 2));
}

void APIENTRY glMapGrid1f(GLint un, GLfloat u1, GLfloat u2);
static void x_glMapGrid1f(MX_NATIVE_CALL *c)
{
    glMapGrid1f((GLint)I(c, 0), F(c, 1), F(c, 2));
}

void APIENTRY glMapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2);
static void x_glMapGrid2d(MX_NATIVE_CALL *c)
{
    glMapGrid2d((GLint)I(c, 0), D(c, 1), D(c, 2), (GLint)I(c, 3), D(c, 4), D(c, 5));
}

void APIENTRY glMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2);
static void x_glMapGrid2f(MX_NATIVE_CALL *c)
{
    glMapGrid2f((GLint)I(c, 0), F(c, 1), F(c, 2), (GLint)I(c, 3), F(c, 4), F(c, 5));
}

void APIENTRY glMaterialf(GLenum face, GLenum pname, GLfloat param);
static void x_glMaterialf(MX_NATIVE_CALL *c)
{
    glMaterialf((GLenum)I(c, 0), (GLenum)I(c, 1), F(c, 2));
}

void APIENTRY glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);
static void x_glMaterialfv(MX_NATIVE_CALL *c)
{
    glMaterialfv((GLenum)I(c, 0), (GLenum)I(c, 1), (const GLfloat *)I(c, 2));
}

void APIENTRY glMateriali(GLenum face, GLenum pname, GLint param);
static void x_glMateriali(MX_NATIVE_CALL *c)
{
    glMateriali((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glMaterialiv(GLenum face, GLenum pname, const GLint *params);
static void x_glMaterialiv(MX_NATIVE_CALL *c)
{
    glMaterialiv((GLenum)I(c, 0), (GLenum)I(c, 1), (const GLint *)I(c, 2));
}

void APIENTRY glMatrixMode(GLenum mode);
static void x_glMatrixMode(MX_NATIVE_CALL *c)
{
    glMatrixMode((GLenum)I(c, 0));
}

void APIENTRY glMultMatrixd(const GLdouble *m);
static void x_glMultMatrixd(MX_NATIVE_CALL *c)
{
    glMultMatrixd((const GLdouble *)I(c, 0));
}

void APIENTRY glMultMatrixf(const GLfloat *m);
static void x_glMultMatrixf(MX_NATIVE_CALL *c)
{
    glMultMatrixf((const GLfloat *)I(c, 0));
}

void APIENTRY glMultTransposeMatrixd(const GLdouble *m);
static void x_glMultTransposeMatrixd(MX_NATIVE_CALL *c)
{
    glMultTransposeMatrixd((const GLdouble *)I(c, 0));
}

void APIENTRY glMultTransposeMatrixdARB(const GLdouble *m);
static void x_glMultTransposeMatrixdARB(MX_NATIVE_CALL *c)
{
    glMultTransposeMatrixdARB((const GLdouble *)I(c, 0));
}

void APIENTRY glMultTransposeMatrixf(const GLfloat *m);
static void x_glMultTransposeMatrixf(MX_NATIVE_CALL *c)
{
    glMultTransposeMatrixf((const GLfloat *)I(c, 0));
}

void APIENTRY glMultTransposeMatrixfARB(const GLfloat *m);
static void x_glMultTransposeMatrixfARB(MX_NATIVE_CALL *c)
{
    glMultTransposeMatrixfARB((const GLfloat *)I(c, 0));
}

void APIENTRY glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei n);
static void x_glMultiDrawArrays(MX_NATIVE_CALL *c)
{
    glMultiDrawArrays((GLenum)I(c, 0), (const GLint *)I(c, 1), (const GLsizei *)I(c, 2), (GLsizei)I(c, 3));
}

void APIENTRY glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const void *const *indices,                                         GLsizei n);
static void x_glMultiDrawElements(MX_NATIVE_CALL *c)
{
    glMultiDrawElements((GLenum)I(c, 0), (const GLsizei *)I(c, 1), (GLenum)I(c, 2), (const void *const *)I(c, 3), (GLsizei)I(c, 4));
}

void APIENTRY glMultiTexCoord1d(GLenum target, GLdouble s);
static void x_glMultiTexCoord1d(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1d((GLenum)I(c, 0), D(c, 1));
}

void APIENTRY glMultiTexCoord1dARB(GLenum target, GLdouble s);
static void x_glMultiTexCoord1dARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1dARB((GLenum)I(c, 0), D(c, 1));
}

void APIENTRY glMultiTexCoord1dv(GLenum target, const GLdouble *v);
static void x_glMultiTexCoord1dv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1dv((GLenum)I(c, 0), (const GLdouble *)I(c, 1));
}

void APIENTRY glMultiTexCoord1dvARB(GLenum target, const GLdouble *v);
static void x_glMultiTexCoord1dvARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1dvARB((GLenum)I(c, 0), (const GLdouble *)I(c, 1));
}

void APIENTRY glMultiTexCoord1f(GLenum target, GLfloat s);
static void x_glMultiTexCoord1f(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1f((GLenum)I(c, 0), F(c, 1));
}

void APIENTRY glMultiTexCoord1fARB(GLenum target, GLfloat s);
static void x_glMultiTexCoord1fARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1fARB((GLenum)I(c, 0), F(c, 1));
}

void APIENTRY glMultiTexCoord1fv(GLenum target, const GLfloat *v);
static void x_glMultiTexCoord1fv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1fv((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glMultiTexCoord1fvARB(GLenum target, const GLfloat *v);
static void x_glMultiTexCoord1fvARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1fvARB((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glMultiTexCoord1i(GLenum target, GLint s);
static void x_glMultiTexCoord1i(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1i((GLenum)I(c, 0), (GLint)I(c, 1));
}

void APIENTRY glMultiTexCoord1iARB(GLenum target, GLint s);
static void x_glMultiTexCoord1iARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1iARB((GLenum)I(c, 0), (GLint)I(c, 1));
}

void APIENTRY glMultiTexCoord1iv(GLenum target, const GLint *v);
static void x_glMultiTexCoord1iv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1iv((GLenum)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glMultiTexCoord1ivARB(GLenum target, const GLint *v);
static void x_glMultiTexCoord1ivARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1ivARB((GLenum)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glMultiTexCoord1s(GLenum target, GLshort s);
static void x_glMultiTexCoord1s(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1s((GLenum)I(c, 0), (GLshort)I(c, 1));
}

void APIENTRY glMultiTexCoord1sARB(GLenum target, GLshort s);
static void x_glMultiTexCoord1sARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1sARB((GLenum)I(c, 0), (GLshort)I(c, 1));
}

void APIENTRY glMultiTexCoord1sv(GLenum target, const GLshort *v);
static void x_glMultiTexCoord1sv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1sv((GLenum)I(c, 0), (const GLshort *)I(c, 1));
}

void APIENTRY glMultiTexCoord1svARB(GLenum target, const GLshort *v);
static void x_glMultiTexCoord1svARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord1svARB((GLenum)I(c, 0), (const GLshort *)I(c, 1));
}

void APIENTRY glMultiTexCoord2d(GLenum target, GLdouble s, GLdouble t);
static void x_glMultiTexCoord2d(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2d((GLenum)I(c, 0), D(c, 1), D(c, 2));
}

void APIENTRY glMultiTexCoord2dARB(GLenum target, GLdouble s, GLdouble t);
static void x_glMultiTexCoord2dARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2dARB((GLenum)I(c, 0), D(c, 1), D(c, 2));
}

void APIENTRY glMultiTexCoord2dv(GLenum target, const GLdouble *v);
static void x_glMultiTexCoord2dv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2dv((GLenum)I(c, 0), (const GLdouble *)I(c, 1));
}

void APIENTRY glMultiTexCoord2dvARB(GLenum target, const GLdouble *v);
static void x_glMultiTexCoord2dvARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2dvARB((GLenum)I(c, 0), (const GLdouble *)I(c, 1));
}

void APIENTRY glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t);
static void x_glMultiTexCoord2f(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2f((GLenum)I(c, 0), F(c, 1), F(c, 2));
}

void APIENTRY glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t);
static void x_glMultiTexCoord2fARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2fARB((GLenum)I(c, 0), F(c, 1), F(c, 2));
}

void APIENTRY glMultiTexCoord2fv(GLenum target, const GLfloat *v);
static void x_glMultiTexCoord2fv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2fv((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glMultiTexCoord2fvARB(GLenum target, const GLfloat *v);
static void x_glMultiTexCoord2fvARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2fvARB((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glMultiTexCoord2i(GLenum target, GLint s, GLint t);
static void x_glMultiTexCoord2i(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2i((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glMultiTexCoord2iARB(GLenum target, GLint s, GLint t);
static void x_glMultiTexCoord2iARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2iARB((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glMultiTexCoord2iv(GLenum target, const GLint *v);
static void x_glMultiTexCoord2iv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2iv((GLenum)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glMultiTexCoord2ivARB(GLenum target, const GLint *v);
static void x_glMultiTexCoord2ivARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2ivARB((GLenum)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glMultiTexCoord2s(GLenum target, GLshort s, GLshort t);
static void x_glMultiTexCoord2s(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2s((GLenum)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2));
}

void APIENTRY glMultiTexCoord2sARB(GLenum target, GLshort s, GLshort t);
static void x_glMultiTexCoord2sARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2sARB((GLenum)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2));
}

void APIENTRY glMultiTexCoord2sv(GLenum target, const GLshort *v);
static void x_glMultiTexCoord2sv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2sv((GLenum)I(c, 0), (const GLshort *)I(c, 1));
}

void APIENTRY glMultiTexCoord2svARB(GLenum target, const GLshort *v);
static void x_glMultiTexCoord2svARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord2svARB((GLenum)I(c, 0), (const GLshort *)I(c, 1));
}

void APIENTRY glMultiTexCoord3d(GLenum target, GLdouble s, GLdouble t, GLdouble r);
static void x_glMultiTexCoord3d(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3d((GLenum)I(c, 0), D(c, 1), D(c, 2), D(c, 3));
}

void APIENTRY glMultiTexCoord3dARB(GLenum target, GLdouble s, GLdouble t, GLdouble r);
static void x_glMultiTexCoord3dARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3dARB((GLenum)I(c, 0), D(c, 1), D(c, 2), D(c, 3));
}

void APIENTRY glMultiTexCoord3dv(GLenum target, const GLdouble *v);
static void x_glMultiTexCoord3dv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3dv((GLenum)I(c, 0), (const GLdouble *)I(c, 1));
}

void APIENTRY glMultiTexCoord3dvARB(GLenum target, const GLdouble *v);
static void x_glMultiTexCoord3dvARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3dvARB((GLenum)I(c, 0), (const GLdouble *)I(c, 1));
}

void APIENTRY glMultiTexCoord3f(GLenum target, GLfloat s, GLfloat t, GLfloat r);
static void x_glMultiTexCoord3f(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3f((GLenum)I(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glMultiTexCoord3fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r);
static void x_glMultiTexCoord3fARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3fARB((GLenum)I(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glMultiTexCoord3fv(GLenum target, const GLfloat *v);
static void x_glMultiTexCoord3fv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3fv((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glMultiTexCoord3fvARB(GLenum target, const GLfloat *v);
static void x_glMultiTexCoord3fvARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3fvARB((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glMultiTexCoord3i(GLenum target, GLint s, GLint t, GLint r);
static void x_glMultiTexCoord3i(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3i((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3));
}

void APIENTRY glMultiTexCoord3iARB(GLenum target, GLint s, GLint t, GLint r);
static void x_glMultiTexCoord3iARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3iARB((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3));
}

void APIENTRY glMultiTexCoord3iv(GLenum target, const GLint *v);
static void x_glMultiTexCoord3iv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3iv((GLenum)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glMultiTexCoord3ivARB(GLenum target, const GLint *v);
static void x_glMultiTexCoord3ivARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3ivARB((GLenum)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glMultiTexCoord3s(GLenum target, GLshort s, GLshort t, GLshort r);
static void x_glMultiTexCoord3s(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3s((GLenum)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2), (GLshort)I(c, 3));
}

void APIENTRY glMultiTexCoord3sARB(GLenum target, GLshort s, GLshort t, GLshort r);
static void x_glMultiTexCoord3sARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3sARB((GLenum)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2), (GLshort)I(c, 3));
}

void APIENTRY glMultiTexCoord3sv(GLenum target, const GLshort *v);
static void x_glMultiTexCoord3sv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3sv((GLenum)I(c, 0), (const GLshort *)I(c, 1));
}

void APIENTRY glMultiTexCoord3svARB(GLenum target, const GLshort *v);
static void x_glMultiTexCoord3svARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord3svARB((GLenum)I(c, 0), (const GLshort *)I(c, 1));
}

void APIENTRY glMultiTexCoord4d(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
static void x_glMultiTexCoord4d(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4d((GLenum)I(c, 0), D(c, 1), D(c, 2), D(c, 3), D(c, 4));
}

void APIENTRY glMultiTexCoord4dARB(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
static void x_glMultiTexCoord4dARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4dARB((GLenum)I(c, 0), D(c, 1), D(c, 2), D(c, 3), D(c, 4));
}

void APIENTRY glMultiTexCoord4dv(GLenum target, const GLdouble *v);
static void x_glMultiTexCoord4dv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4dv((GLenum)I(c, 0), (const GLdouble *)I(c, 1));
}

void APIENTRY glMultiTexCoord4dvARB(GLenum target, const GLdouble *v);
static void x_glMultiTexCoord4dvARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4dvARB((GLenum)I(c, 0), (const GLdouble *)I(c, 1));
}

void APIENTRY glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
static void x_glMultiTexCoord4f(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4f((GLenum)I(c, 0), F(c, 1), F(c, 2), F(c, 3), F(c, 4));
}

void APIENTRY glMultiTexCoord4fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
static void x_glMultiTexCoord4fARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4fARB((GLenum)I(c, 0), F(c, 1), F(c, 2), F(c, 3), F(c, 4));
}

void APIENTRY glMultiTexCoord4fv(GLenum target, const GLfloat *v);
static void x_glMultiTexCoord4fv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4fv((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glMultiTexCoord4fvARB(GLenum target, const GLfloat *v);
static void x_glMultiTexCoord4fvARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4fvARB((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glMultiTexCoord4i(GLenum target, GLint s, GLint t, GLint r, GLint q);
static void x_glMultiTexCoord4i(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4i((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3), (GLint)I(c, 4));
}

void APIENTRY glMultiTexCoord4iARB(GLenum target, GLint s, GLint t, GLint r, GLint q);
static void x_glMultiTexCoord4iARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4iARB((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3), (GLint)I(c, 4));
}

void APIENTRY glMultiTexCoord4iv(GLenum target, const GLint *v);
static void x_glMultiTexCoord4iv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4iv((GLenum)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glMultiTexCoord4ivARB(GLenum target, const GLint *v);
static void x_glMultiTexCoord4ivARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4ivARB((GLenum)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glMultiTexCoord4s(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
static void x_glMultiTexCoord4s(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4s((GLenum)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2), (GLshort)I(c, 3), (GLshort)I(c, 4));
}

void APIENTRY glMultiTexCoord4sARB(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
static void x_glMultiTexCoord4sARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4sARB((GLenum)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2), (GLshort)I(c, 3), (GLshort)I(c, 4));
}

void APIENTRY glMultiTexCoord4sv(GLenum target, const GLshort *v);
static void x_glMultiTexCoord4sv(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4sv((GLenum)I(c, 0), (const GLshort *)I(c, 1));
}

void APIENTRY glMultiTexCoord4svARB(GLenum target, const GLshort *v);
static void x_glMultiTexCoord4svARB(MX_NATIVE_CALL *c)
{
    glMultiTexCoord4svARB((GLenum)I(c, 0), (const GLshort *)I(c, 1));
}

void APIENTRY glNewList(GLuint list, GLenum mode);
static void x_glNewList(MX_NATIVE_CALL *c)
{
    glNewList((GLuint)I(c, 0), (GLenum)I(c, 1));
}

void APIENTRY glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz);
static void x_glNormal3b(MX_NATIVE_CALL *c)
{
    glNormal3b((GLbyte)I(c, 0), (GLbyte)I(c, 1), (GLbyte)I(c, 2));
}

void APIENTRY glNormal3bv(const GLbyte *v);
static void x_glNormal3bv(MX_NATIVE_CALL *c)
{
    glNormal3bv((const GLbyte *)I(c, 0));
}

void APIENTRY glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz);
static void x_glNormal3d(MX_NATIVE_CALL *c)
{
    glNormal3d(D(c, 0), D(c, 1), D(c, 2));
}

void APIENTRY glNormal3dv(const GLdouble *v);
static void x_glNormal3dv(MX_NATIVE_CALL *c)
{
    glNormal3dv((const GLdouble *)I(c, 0));
}

void APIENTRY glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
static void x_glNormal3f(MX_NATIVE_CALL *c)
{
    glNormal3f(F(c, 0), F(c, 1), F(c, 2));
}

void APIENTRY glNormal3fv(const GLfloat *v);
static void x_glNormal3fv(MX_NATIVE_CALL *c)
{
    glNormal3fv((const GLfloat *)I(c, 0));
}

void APIENTRY glNormal3i(GLint nx, GLint ny, GLint nz);
static void x_glNormal3i(MX_NATIVE_CALL *c)
{
    glNormal3i((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glNormal3iv(const GLint *v);
static void x_glNormal3iv(MX_NATIVE_CALL *c)
{
    glNormal3iv((const GLint *)I(c, 0));
}

void APIENTRY glNormal3s(GLshort nx, GLshort ny, GLshort nz);
static void x_glNormal3s(MX_NATIVE_CALL *c)
{
    glNormal3s((GLshort)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2));
}

void APIENTRY glNormal3sv(const GLshort *v);
static void x_glNormal3sv(MX_NATIVE_CALL *c)
{
    glNormal3sv((const GLshort *)I(c, 0));
}

void APIENTRY glNormalPointer(GLenum type, GLsizei stride, const void *pointer);
static void x_glNormalPointer(MX_NATIVE_CALL *c)
{
    glNormalPointer((GLenum)I(c, 0), (GLsizei)I(c, 1), (const void *)I(c, 2));
}

void APIENTRY glObjectLabel(GLenum identifier, GLuint name, GLsizei length, const GLchar *label);
static void x_glObjectLabel(MX_NATIVE_CALL *c)
{
    glObjectLabel((GLenum)I(c, 0), (GLuint)I(c, 1), (GLsizei)I(c, 2), (const GLchar *)I(c, 3));
}

void APIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top_, GLdouble near_val,                             GLdouble far_val);
static void x_glOrtho(MX_NATIVE_CALL *c)
{
    glOrtho(D(c, 0), D(c, 1), D(c, 2), D(c, 3), D(c, 4), D(c, 5));
}

void APIENTRY glPassThrough(GLfloat token);
static void x_glPassThrough(MX_NATIVE_CALL *c)
{
    glPassThrough(F(c, 0));
}

void APIENTRY glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat *values);
static void x_glPixelMapfv(MX_NATIVE_CALL *c)
{
    glPixelMapfv((GLenum)I(c, 0), (GLsizei)I(c, 1), (const GLfloat *)I(c, 2));
}

void APIENTRY glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint *values);
static void x_glPixelMapuiv(MX_NATIVE_CALL *c)
{
    glPixelMapuiv((GLenum)I(c, 0), (GLsizei)I(c, 1), (const GLuint *)I(c, 2));
}

void APIENTRY glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort *values);
static void x_glPixelMapusv(MX_NATIVE_CALL *c)
{
    glPixelMapusv((GLenum)I(c, 0), (GLsizei)I(c, 1), (const GLushort *)I(c, 2));
}

void APIENTRY glPixelStoref(GLenum pname, GLfloat param);
static void x_glPixelStoref(MX_NATIVE_CALL *c)
{
    glPixelStoref((GLenum)I(c, 0), F(c, 1));
}

void APIENTRY glPixelStorei(GLenum pname, GLint param);
static void x_glPixelStorei(MX_NATIVE_CALL *c)
{
    glPixelStorei((GLenum)I(c, 0), (GLint)I(c, 1));
}

void APIENTRY glPixelTransferf(GLenum pname, GLfloat param);
static void x_glPixelTransferf(MX_NATIVE_CALL *c)
{
    glPixelTransferf((GLenum)I(c, 0), F(c, 1));
}

void APIENTRY glPixelTransferi(GLenum pname, GLint param);
static void x_glPixelTransferi(MX_NATIVE_CALL *c)
{
    glPixelTransferi((GLenum)I(c, 0), (GLint)I(c, 1));
}

void APIENTRY glPixelZoom(GLfloat xfactor, GLfloat yfactor);
static void x_glPixelZoom(MX_NATIVE_CALL *c)
{
    glPixelZoom(F(c, 0), F(c, 1));
}

void APIENTRY glPointParameterf(GLenum pname, GLfloat value);
static void x_glPointParameterf(MX_NATIVE_CALL *c)
{
    glPointParameterf((GLenum)I(c, 0), F(c, 1));
}

void APIENTRY glPointParameterfARB(GLenum pname, GLfloat value);
static void x_glPointParameterfARB(MX_NATIVE_CALL *c)
{
    glPointParameterfARB((GLenum)I(c, 0), F(c, 1));
}

void APIENTRY glPointParameterfEXT(GLenum pname, GLfloat value);
static void x_glPointParameterfEXT(MX_NATIVE_CALL *c)
{
    glPointParameterfEXT((GLenum)I(c, 0), F(c, 1));
}

void APIENTRY glPointParameterfv(GLenum pname, const GLfloat *values);
static void x_glPointParameterfv(MX_NATIVE_CALL *c)
{
    glPointParameterfv((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glPointParameterfvARB(GLenum pname, const GLfloat *values);
static void x_glPointParameterfvARB(MX_NATIVE_CALL *c)
{
    glPointParameterfvARB((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glPointParameterfvEXT(GLenum pname, const GLfloat *values);
static void x_glPointParameterfvEXT(MX_NATIVE_CALL *c)
{
    glPointParameterfvEXT((GLenum)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glPointParameteri(GLenum pname, GLint value);
static void x_glPointParameteri(MX_NATIVE_CALL *c)
{
    glPointParameteri((GLenum)I(c, 0), (GLint)I(c, 1));
}

void APIENTRY glPointParameteriv(GLenum pname, const GLint *values);
static void x_glPointParameteriv(MX_NATIVE_CALL *c)
{
    glPointParameteriv((GLenum)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glPointSize(GLfloat size);
static void x_glPointSize(MX_NATIVE_CALL *c)
{
    glPointSize(F(c, 0));
}

void APIENTRY glPolygonMode(GLenum face, GLenum mode);
static void x_glPolygonMode(MX_NATIVE_CALL *c)
{
    glPolygonMode((GLenum)I(c, 0), (GLenum)I(c, 1));
}

void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units);
static void x_glPolygonOffset(MX_NATIVE_CALL *c)
{
    glPolygonOffset(F(c, 0), F(c, 1));
}

void APIENTRY glPolygonStipple(const GLubyte *mask);
static void x_glPolygonStipple(MX_NATIVE_CALL *c)
{
    glPolygonStipple((const GLubyte *)I(c, 0));
}

void APIENTRY glPopAttrib(void);
static void x_glPopAttrib(MX_NATIVE_CALL *c)
{
    glPopAttrib();
}

void APIENTRY glPopClientAttrib(void);
static void x_glPopClientAttrib(MX_NATIVE_CALL *c)
{
    glPopClientAttrib();
}

void APIENTRY glPopDebugGroup(void);
static void x_glPopDebugGroup(MX_NATIVE_CALL *c)
{
    glPopDebugGroup();
}

void APIENTRY glPopMatrix(void);
static void x_glPopMatrix(MX_NATIVE_CALL *c)
{
    glPopMatrix();
}

void APIENTRY glPopName(void);
static void x_glPopName(MX_NATIVE_CALL *c)
{
    glPopName();
}

void APIENTRY glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities);
static void x_glPrioritizeTextures(MX_NATIVE_CALL *c)
{
    glPrioritizeTextures((GLsizei)I(c, 0), (const GLuint *)I(c, 1), (const GLclampf *)I(c, 2));
}

void APIENTRY glPushAttrib(GLbitfield mask);
static void x_glPushAttrib(MX_NATIVE_CALL *c)
{
    glPushAttrib((GLbitfield)I(c, 0));
}

void APIENTRY glPushClientAttrib(GLbitfield mask);
static void x_glPushClientAttrib(MX_NATIVE_CALL *c)
{
    glPushClientAttrib((GLbitfield)I(c, 0));
}

void APIENTRY glPushDebugGroup(GLenum source, GLuint id, GLsizei length, const GLchar *message);
static void x_glPushDebugGroup(MX_NATIVE_CALL *c)
{
    glPushDebugGroup((GLenum)I(c, 0), (GLuint)I(c, 1), (GLsizei)I(c, 2), (const GLchar *)I(c, 3));
}

void APIENTRY glPushMatrix(void);
static void x_glPushMatrix(MX_NATIVE_CALL *c)
{
    glPushMatrix();
}

void APIENTRY glPushName(GLuint name);
static void x_glPushName(MX_NATIVE_CALL *c)
{
    glPushName((GLuint)I(c, 0));
}

void APIENTRY glQueryCounter(GLuint name, GLenum target);
static void x_glQueryCounter(MX_NATIVE_CALL *c)
{
    glQueryCounter((GLuint)I(c, 0), (GLenum)I(c, 1));
}

void APIENTRY glRasterPos2d(GLdouble x, GLdouble y);
static void x_glRasterPos2d(MX_NATIVE_CALL *c)
{
    glRasterPos2d(D(c, 0), D(c, 1));
}

void APIENTRY glRasterPos2dv(const GLdouble *v);
static void x_glRasterPos2dv(MX_NATIVE_CALL *c)
{
    glRasterPos2dv((const GLdouble *)I(c, 0));
}

void APIENTRY glRasterPos2f(GLfloat x, GLfloat y);
static void x_glRasterPos2f(MX_NATIVE_CALL *c)
{
    glRasterPos2f(F(c, 0), F(c, 1));
}

void APIENTRY glRasterPos2fv(const GLfloat *v);
static void x_glRasterPos2fv(MX_NATIVE_CALL *c)
{
    glRasterPos2fv((const GLfloat *)I(c, 0));
}

void APIENTRY glRasterPos2i(GLint x, GLint y);
static void x_glRasterPos2i(MX_NATIVE_CALL *c)
{
    glRasterPos2i((GLint)I(c, 0), (GLint)I(c, 1));
}

void APIENTRY glRasterPos2iv(const GLint *v);
static void x_glRasterPos2iv(MX_NATIVE_CALL *c)
{
    glRasterPos2iv((const GLint *)I(c, 0));
}

void APIENTRY glRasterPos2s(GLshort x, GLshort y);
static void x_glRasterPos2s(MX_NATIVE_CALL *c)
{
    glRasterPos2s((GLshort)I(c, 0), (GLshort)I(c, 1));
}

void APIENTRY glRasterPos2sv(const GLshort *v);
static void x_glRasterPos2sv(MX_NATIVE_CALL *c)
{
    glRasterPos2sv((const GLshort *)I(c, 0));
}

void APIENTRY glRasterPos3d(GLdouble x, GLdouble y, GLdouble z);
static void x_glRasterPos3d(MX_NATIVE_CALL *c)
{
    glRasterPos3d(D(c, 0), D(c, 1), D(c, 2));
}

void APIENTRY glRasterPos3dv(const GLdouble *v);
static void x_glRasterPos3dv(MX_NATIVE_CALL *c)
{
    glRasterPos3dv((const GLdouble *)I(c, 0));
}

void APIENTRY glRasterPos3f(GLfloat x, GLfloat y, GLfloat z);
static void x_glRasterPos3f(MX_NATIVE_CALL *c)
{
    glRasterPos3f(F(c, 0), F(c, 1), F(c, 2));
}

void APIENTRY glRasterPos3fv(const GLfloat *v);
static void x_glRasterPos3fv(MX_NATIVE_CALL *c)
{
    glRasterPos3fv((const GLfloat *)I(c, 0));
}

void APIENTRY glRasterPos3i(GLint x, GLint y, GLint z);
static void x_glRasterPos3i(MX_NATIVE_CALL *c)
{
    glRasterPos3i((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glRasterPos3iv(const GLint *v);
static void x_glRasterPos3iv(MX_NATIVE_CALL *c)
{
    glRasterPos3iv((const GLint *)I(c, 0));
}

void APIENTRY glRasterPos3s(GLshort x, GLshort y, GLshort z);
static void x_glRasterPos3s(MX_NATIVE_CALL *c)
{
    glRasterPos3s((GLshort)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2));
}

void APIENTRY glRasterPos3sv(const GLshort *v);
static void x_glRasterPos3sv(MX_NATIVE_CALL *c)
{
    glRasterPos3sv((const GLshort *)I(c, 0));
}

void APIENTRY glRasterPos4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
static void x_glRasterPos4d(MX_NATIVE_CALL *c)
{
    glRasterPos4d(D(c, 0), D(c, 1), D(c, 2), D(c, 3));
}

void APIENTRY glRasterPos4dv(const GLdouble *v);
static void x_glRasterPos4dv(MX_NATIVE_CALL *c)
{
    glRasterPos4dv((const GLdouble *)I(c, 0));
}

void APIENTRY glRasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
static void x_glRasterPos4f(MX_NATIVE_CALL *c)
{
    glRasterPos4f(F(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glRasterPos4fv(const GLfloat *v);
static void x_glRasterPos4fv(MX_NATIVE_CALL *c)
{
    glRasterPos4fv((const GLfloat *)I(c, 0));
}

void APIENTRY glRasterPos4i(GLint x, GLint y, GLint z, GLint w);
static void x_glRasterPos4i(MX_NATIVE_CALL *c)
{
    glRasterPos4i((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3));
}

void APIENTRY glRasterPos4iv(const GLint *v);
static void x_glRasterPos4iv(MX_NATIVE_CALL *c)
{
    glRasterPos4iv((const GLint *)I(c, 0));
}

void APIENTRY glRasterPos4s(GLshort x, GLshort y, GLshort z, GLshort w);
static void x_glRasterPos4s(MX_NATIVE_CALL *c)
{
    glRasterPos4s((GLshort)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2), (GLshort)I(c, 3));
}

void APIENTRY glRasterPos4sv(const GLshort *v);
static void x_glRasterPos4sv(MX_NATIVE_CALL *c)
{
    glRasterPos4sv((const GLshort *)I(c, 0));
}

void APIENTRY glReadBuffer(GLenum buf);
static void x_glReadBuffer(MX_NATIVE_CALL *c)
{
    glReadBuffer((GLenum)I(c, 0));
}

void APIENTRY glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels);
static void x_glReadPixels(MX_NATIVE_CALL *c)
{
    glReadPixels((GLint)I(c, 0), (GLint)I(c, 1), (GLsizei)I(c, 2), (GLsizei)I(c, 3), (GLenum)I(c, 4), (GLenum)I(c, 5), (void *)I(c, 6));
}

void APIENTRY glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2);
static void x_glRectd(MX_NATIVE_CALL *c)
{
    glRectd(D(c, 0), D(c, 1), D(c, 2), D(c, 3));
}

void APIENTRY glRectdv(const GLdouble *v1, const GLdouble *v2);
static void x_glRectdv(MX_NATIVE_CALL *c)
{
    glRectdv((const GLdouble *)I(c, 0), (const GLdouble *)I(c, 1));
}

void APIENTRY glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
static void x_glRectf(MX_NATIVE_CALL *c)
{
    glRectf(F(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glRectfv(const GLfloat *v1, const GLfloat *v2);
static void x_glRectfv(MX_NATIVE_CALL *c)
{
    glRectfv((const GLfloat *)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glRecti(GLint x1, GLint y1, GLint x2, GLint y2);
static void x_glRecti(MX_NATIVE_CALL *c)
{
    glRecti((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3));
}

void APIENTRY glRectiv(const GLint *v1, const GLint *v2);
static void x_glRectiv(MX_NATIVE_CALL *c)
{
    glRectiv((const GLint *)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2);
static void x_glRects(MX_NATIVE_CALL *c)
{
    glRects((GLshort)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2), (GLshort)I(c, 3));
}

void APIENTRY glRectsv(const GLshort *v1, const GLshort *v2);
static void x_glRectsv(MX_NATIVE_CALL *c)
{
    glRectsv((const GLshort *)I(c, 0), (const GLshort *)I(c, 1));
}

void APIENTRY glReleaseShaderCompiler(void);
static void x_glReleaseShaderCompiler(MX_NATIVE_CALL *c)
{
    glReleaseShaderCompiler();
}

GLint APIENTRY glRenderMode(GLenum mode);
static void x_glRenderMode(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glRenderMode((GLenum)I(c, 0));
}

void APIENTRY glRenderbufferStorage(GLenum target, GLenum internal, GLsizei w, GLsizei h);
static void x_glRenderbufferStorage(MX_NATIVE_CALL *c)
{
    glRenderbufferStorage((GLenum)I(c, 0), (GLenum)I(c, 1), (GLsizei)I(c, 2), (GLsizei)I(c, 3));
}

void APIENTRY glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internal, GLsizei w,                                                      GLsizei h);
static void x_glRenderbufferStorageMultisample(MX_NATIVE_CALL *c)
{
    glRenderbufferStorageMultisample((GLenum)I(c, 0), (GLsizei)I(c, 1), (GLenum)I(c, 2), (GLsizei)I(c, 3), (GLsizei)I(c, 4));
}

void APIENTRY glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
static void x_glRotated(MX_NATIVE_CALL *c)
{
    glRotated(D(c, 0), D(c, 1), D(c, 2), D(c, 3));
}

void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
static void x_glRotatef(MX_NATIVE_CALL *c)
{
    glRotatef(F(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glScaled(GLdouble x, GLdouble y, GLdouble z);
static void x_glScaled(MX_NATIVE_CALL *c)
{
    glScaled(D(c, 0), D(c, 1), D(c, 2));
}

void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z);
static void x_glScalef(MX_NATIVE_CALL *c)
{
    glScalef(F(c, 0), F(c, 1), F(c, 2));
}

void APIENTRY glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
static void x_glScissor(MX_NATIVE_CALL *c)
{
    glScissor((GLint)I(c, 0), (GLint)I(c, 1), (GLsizei)I(c, 2), (GLsizei)I(c, 3));
}

void APIENTRY glSelectBuffer(GLsizei size, GLuint *buffer);
static void x_glSelectBuffer(MX_NATIVE_CALL *c)
{
    glSelectBuffer((GLsizei)I(c, 0), (GLuint *)I(c, 1));
}

void APIENTRY glShadeModel(GLenum mode);
static void x_glShadeModel(MX_NATIVE_CALL *c)
{
    glShadeModel((GLenum)I(c, 0));
}

void APIENTRY glShaderSource(GLuint name, GLsizei count, const GLchar *const *strings, const GLint *lengths);
static void x_glShaderSource(MX_NATIVE_CALL *c)
{
    glShaderSource((GLuint)I(c, 0), (GLsizei)I(c, 1), (const GLchar *const *)I(c, 2), (const GLint *)I(c, 3));
}

void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask);
static void x_glStencilFunc(MX_NATIVE_CALL *c)
{
    glStencilFunc((GLenum)I(c, 0), (GLint)I(c, 1), (GLuint)I(c, 2));
}

void APIENTRY glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
static void x_glStencilFuncSeparate(MX_NATIVE_CALL *c)
{
    glStencilFuncSeparate((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint)I(c, 2), (GLuint)I(c, 3));
}

void APIENTRY glStencilMask(GLuint mask);
static void x_glStencilMask(MX_NATIVE_CALL *c)
{
    glStencilMask((GLuint)I(c, 0));
}

void APIENTRY glStencilMaskSeparate(GLenum face, GLuint mask);
static void x_glStencilMaskSeparate(MX_NATIVE_CALL *c)
{
    glStencilMaskSeparate((GLenum)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
static void x_glStencilOp(MX_NATIVE_CALL *c)
{
    glStencilOp((GLenum)I(c, 0), (GLenum)I(c, 1), (GLenum)I(c, 2));
}

void APIENTRY glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass);
static void x_glStencilOpSeparate(MX_NATIVE_CALL *c)
{
    glStencilOpSeparate((GLenum)I(c, 0), (GLenum)I(c, 1), (GLenum)I(c, 2), (GLenum)I(c, 3));
}

void APIENTRY glTexCoord1d(GLdouble s);
static void x_glTexCoord1d(MX_NATIVE_CALL *c)
{
    glTexCoord1d(D(c, 0));
}

void APIENTRY glTexCoord1dv(const GLdouble *v);
static void x_glTexCoord1dv(MX_NATIVE_CALL *c)
{
    glTexCoord1dv((const GLdouble *)I(c, 0));
}

void APIENTRY glTexCoord1f(GLfloat s);
static void x_glTexCoord1f(MX_NATIVE_CALL *c)
{
    glTexCoord1f(F(c, 0));
}

void APIENTRY glTexCoord1fv(const GLfloat *v);
static void x_glTexCoord1fv(MX_NATIVE_CALL *c)
{
    glTexCoord1fv((const GLfloat *)I(c, 0));
}

void APIENTRY glTexCoord1i(GLint s);
static void x_glTexCoord1i(MX_NATIVE_CALL *c)
{
    glTexCoord1i((GLint)I(c, 0));
}

void APIENTRY glTexCoord1iv(const GLint *v);
static void x_glTexCoord1iv(MX_NATIVE_CALL *c)
{
    glTexCoord1iv((const GLint *)I(c, 0));
}

void APIENTRY glTexCoord1s(GLshort s);
static void x_glTexCoord1s(MX_NATIVE_CALL *c)
{
    glTexCoord1s((GLshort)I(c, 0));
}

void APIENTRY glTexCoord1sv(const GLshort *v);
static void x_glTexCoord1sv(MX_NATIVE_CALL *c)
{
    glTexCoord1sv((const GLshort *)I(c, 0));
}

void APIENTRY glTexCoord2d(GLdouble s, GLdouble t);
static void x_glTexCoord2d(MX_NATIVE_CALL *c)
{
    glTexCoord2d(D(c, 0), D(c, 1));
}

void APIENTRY glTexCoord2dv(const GLdouble *v);
static void x_glTexCoord2dv(MX_NATIVE_CALL *c)
{
    glTexCoord2dv((const GLdouble *)I(c, 0));
}

void APIENTRY glTexCoord2f(GLfloat s, GLfloat t);
static void x_glTexCoord2f(MX_NATIVE_CALL *c)
{
    glTexCoord2f(F(c, 0), F(c, 1));
}

void APIENTRY glTexCoord2fv(const GLfloat *v);
static void x_glTexCoord2fv(MX_NATIVE_CALL *c)
{
    glTexCoord2fv((const GLfloat *)I(c, 0));
}

void APIENTRY glTexCoord2i(GLint s, GLint t);
static void x_glTexCoord2i(MX_NATIVE_CALL *c)
{
    glTexCoord2i((GLint)I(c, 0), (GLint)I(c, 1));
}

void APIENTRY glTexCoord2iv(const GLint *v);
static void x_glTexCoord2iv(MX_NATIVE_CALL *c)
{
    glTexCoord2iv((const GLint *)I(c, 0));
}

void APIENTRY glTexCoord2s(GLshort s, GLshort t);
static void x_glTexCoord2s(MX_NATIVE_CALL *c)
{
    glTexCoord2s((GLshort)I(c, 0), (GLshort)I(c, 1));
}

void APIENTRY glTexCoord2sv(const GLshort *v);
static void x_glTexCoord2sv(MX_NATIVE_CALL *c)
{
    glTexCoord2sv((const GLshort *)I(c, 0));
}

void APIENTRY glTexCoord3d(GLdouble s, GLdouble t, GLdouble r);
static void x_glTexCoord3d(MX_NATIVE_CALL *c)
{
    glTexCoord3d(D(c, 0), D(c, 1), D(c, 2));
}

void APIENTRY glTexCoord3dv(const GLdouble *v);
static void x_glTexCoord3dv(MX_NATIVE_CALL *c)
{
    glTexCoord3dv((const GLdouble *)I(c, 0));
}

void APIENTRY glTexCoord3f(GLfloat s, GLfloat t, GLfloat r);
static void x_glTexCoord3f(MX_NATIVE_CALL *c)
{
    glTexCoord3f(F(c, 0), F(c, 1), F(c, 2));
}

void APIENTRY glTexCoord3fv(const GLfloat *v);
static void x_glTexCoord3fv(MX_NATIVE_CALL *c)
{
    glTexCoord3fv((const GLfloat *)I(c, 0));
}

void APIENTRY glTexCoord3i(GLint s, GLint t, GLint r);
static void x_glTexCoord3i(MX_NATIVE_CALL *c)
{
    glTexCoord3i((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glTexCoord3iv(const GLint *v);
static void x_glTexCoord3iv(MX_NATIVE_CALL *c)
{
    glTexCoord3iv((const GLint *)I(c, 0));
}

void APIENTRY glTexCoord3s(GLshort s, GLshort t, GLshort r);
static void x_glTexCoord3s(MX_NATIVE_CALL *c)
{
    glTexCoord3s((GLshort)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2));
}

void APIENTRY glTexCoord3sv(const GLshort *v);
static void x_glTexCoord3sv(MX_NATIVE_CALL *c)
{
    glTexCoord3sv((const GLshort *)I(c, 0));
}

void APIENTRY glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q);
static void x_glTexCoord4d(MX_NATIVE_CALL *c)
{
    glTexCoord4d(D(c, 0), D(c, 1), D(c, 2), D(c, 3));
}

void APIENTRY glTexCoord4dv(const GLdouble *v);
static void x_glTexCoord4dv(MX_NATIVE_CALL *c)
{
    glTexCoord4dv((const GLdouble *)I(c, 0));
}

void APIENTRY glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
static void x_glTexCoord4f(MX_NATIVE_CALL *c)
{
    glTexCoord4f(F(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glTexCoord4fv(const GLfloat *v);
static void x_glTexCoord4fv(MX_NATIVE_CALL *c)
{
    glTexCoord4fv((const GLfloat *)I(c, 0));
}

void APIENTRY glTexCoord4i(GLint s, GLint t, GLint r, GLint q);
static void x_glTexCoord4i(MX_NATIVE_CALL *c)
{
    glTexCoord4i((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3));
}

void APIENTRY glTexCoord4iv(const GLint *v);
static void x_glTexCoord4iv(MX_NATIVE_CALL *c)
{
    glTexCoord4iv((const GLint *)I(c, 0));
}

void APIENTRY glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q);
static void x_glTexCoord4s(MX_NATIVE_CALL *c)
{
    glTexCoord4s((GLshort)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2), (GLshort)I(c, 3));
}

void APIENTRY glTexCoord4sv(const GLshort *v);
static void x_glTexCoord4sv(MX_NATIVE_CALL *c)
{
    glTexCoord4sv((const GLshort *)I(c, 0));
}

void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
static void x_glTexCoordPointer(MX_NATIVE_CALL *c)
{
    glTexCoordPointer((GLint)I(c, 0), (GLenum)I(c, 1), (GLsizei)I(c, 2), (const void *)I(c, 3));
}

void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param);
static void x_glTexEnvf(MX_NATIVE_CALL *c)
{
    glTexEnvf((GLenum)I(c, 0), (GLenum)I(c, 1), F(c, 2));
}

void APIENTRY glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);
static void x_glTexEnvfv(MX_NATIVE_CALL *c)
{
    glTexEnvfv((GLenum)I(c, 0), (GLenum)I(c, 1), (const GLfloat *)I(c, 2));
}

void APIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param);
static void x_glTexEnvi(MX_NATIVE_CALL *c)
{
    glTexEnvi((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glTexEnviv(GLenum target, GLenum pname, const GLint *params);
static void x_glTexEnviv(MX_NATIVE_CALL *c)
{
    glTexEnviv((GLenum)I(c, 0), (GLenum)I(c, 1), (const GLint *)I(c, 2));
}

void APIENTRY glTexGend(GLenum coord, GLenum pname, GLdouble param);
static void x_glTexGend(MX_NATIVE_CALL *c)
{
    glTexGend((GLenum)I(c, 0), (GLenum)I(c, 1), D(c, 2));
}

void APIENTRY glTexGendv(GLenum coord, GLenum pname, const GLdouble *params);
static void x_glTexGendv(MX_NATIVE_CALL *c)
{
    glTexGendv((GLenum)I(c, 0), (GLenum)I(c, 1), (const GLdouble *)I(c, 2));
}

void APIENTRY glTexGenf(GLenum coord, GLenum pname, GLfloat param);
static void x_glTexGenf(MX_NATIVE_CALL *c)
{
    glTexGenf((GLenum)I(c, 0), (GLenum)I(c, 1), F(c, 2));
}

void APIENTRY glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params);
static void x_glTexGenfv(MX_NATIVE_CALL *c)
{
    glTexGenfv((GLenum)I(c, 0), (GLenum)I(c, 1), (const GLfloat *)I(c, 2));
}

void APIENTRY glTexGeni(GLenum coord, GLenum pname, GLint param);
static void x_glTexGeni(MX_NATIVE_CALL *c)
{
    glTexGeni((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glTexGeniv(GLenum coord, GLenum pname, const GLint *params);
static void x_glTexGeniv(MX_NATIVE_CALL *c)
{
    glTexGeniv((GLenum)I(c, 0), (GLenum)I(c, 1), (const GLint *)I(c, 2));
}

void APIENTRY glTexImage1D(GLenum target, GLint level, GLint internal, GLsizei w, GLint border, GLenum format,                                  GLenum type, const void *pixels);
static void x_glTexImage1D(MX_NATIVE_CALL *c)
{
    glTexImage1D((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLsizei)I(c, 3), (GLint)I(c, 4), (GLenum)I(c, 5), (GLenum)I(c, 6), (const void *)I(c, 7));
}

void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,                                  GLenum format, GLenum type, const void *pixels);
static void x_glTexImage2D(MX_NATIVE_CALL *c)
{
    glTexImage2D((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLsizei)I(c, 3), (GLsizei)I(c, 4), (GLint)I(c, 5), (GLenum)I(c, 6), (GLenum)I(c, 7), (const void *)I(c, 8));
}

void APIENTRY glTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internal, GLsizei w, GLsizei h,                                             GLboolean fixed);
static void x_glTexImage2DMultisample(MX_NATIVE_CALL *c)
{
    glTexImage2DMultisample((GLenum)I(c, 0), (GLsizei)I(c, 1), (GLenum)I(c, 2), (GLsizei)I(c, 3), (GLsizei)I(c, 4), (GLboolean)I(c, 5));
}

void APIENTRY glTexImage3D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d,                                  GLint border, GLenum format, GLenum type, const void *pixels);
static void x_glTexImage3D(MX_NATIVE_CALL *c)
{
    glTexImage3D((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLsizei)I(c, 3), (GLsizei)I(c, 4), (GLsizei)I(c, 5), (GLint)I(c, 6), (GLenum)I(c, 7), (GLenum)I(c, 8), (const void *)I(c, 9));
}

void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param);
static void x_glTexParameterf(MX_NATIVE_CALL *c)
{
    glTexParameterf((GLenum)I(c, 0), (GLenum)I(c, 1), F(c, 2));
}

void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
static void x_glTexParameterfv(MX_NATIVE_CALL *c)
{
    glTexParameterfv((GLenum)I(c, 0), (GLenum)I(c, 1), (const GLfloat *)I(c, 2));
}

void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param);
static void x_glTexParameteri(MX_NATIVE_CALL *c)
{
    glTexParameteri((GLenum)I(c, 0), (GLenum)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint *params);
static void x_glTexParameteriv(MX_NATIVE_CALL *c)
{
    glTexParameteriv((GLenum)I(c, 0), (GLenum)I(c, 1), (const GLint *)I(c, 2));
}

void APIENTRY glTexStorage2D(GLenum target, GLsizei levels, GLenum internal, GLsizei w, GLsizei h);
static void x_glTexStorage2D(MX_NATIVE_CALL *c)
{
    glTexStorage2D((GLenum)I(c, 0), (GLsizei)I(c, 1), (GLenum)I(c, 2), (GLsizei)I(c, 3), (GLsizei)I(c, 4));
}

void APIENTRY glTexStorage3D(GLenum target, GLsizei levels, GLenum internal, GLsizei w, GLsizei h, GLsizei d);
static void x_glTexStorage3D(MX_NATIVE_CALL *c)
{
    glTexStorage3D((GLenum)I(c, 0), (GLsizei)I(c, 1), (GLenum)I(c, 2), (GLsizei)I(c, 3), (GLsizei)I(c, 4), (GLsizei)I(c, 5));
}

void APIENTRY glTexSubImage1D(GLenum target, GLint level, GLint x, GLsizei w, GLenum format, GLenum type,                                     const void *pixels);
static void x_glTexSubImage1D(MX_NATIVE_CALL *c)
{
    glTexSubImage1D((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLsizei)I(c, 3), (GLenum)I(c, 4), (GLenum)I(c, 5), (const void *)I(c, 6));
}

void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,                                     GLenum type, const void *pixels);
static void x_glTexSubImage2D(MX_NATIVE_CALL *c)
{
    glTexSubImage2D((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3), (GLsizei)I(c, 4), (GLsizei)I(c, 5), (GLenum)I(c, 6), (GLenum)I(c, 7), (const void *)I(c, 8));
}

void APIENTRY glTexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h,                                     GLsizei d, GLenum format, GLenum type, const void *pixels);
static void x_glTexSubImage3D(MX_NATIVE_CALL *c)
{
    glTexSubImage3D((GLenum)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3), (GLint)I(c, 4), (GLsizei)I(c, 5), (GLsizei)I(c, 6), (GLsizei)I(c, 7), (GLenum)I(c, 8), (GLenum)I(c, 9), (const void *)I(c, 10));
}

void APIENTRY glTranslated(GLdouble x, GLdouble y, GLdouble z);
static void x_glTranslated(MX_NATIVE_CALL *c)
{
    glTranslated(D(c, 0), D(c, 1), D(c, 2));
}

void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z);
static void x_glTranslatef(MX_NATIVE_CALL *c)
{
    glTranslatef(F(c, 0), F(c, 1), F(c, 2));
}

void APIENTRY glUniform1f(GLint l, GLfloat a);
static void x_glUniform1f(MX_NATIVE_CALL *c)
{
    glUniform1f((GLint)I(c, 0), F(c, 1));
}

void APIENTRY glUniform1fv(GLint l, GLsizei n, const GLfloat *v);
static void x_glUniform1fv(MX_NATIVE_CALL *c)
{
    glUniform1fv((GLint)I(c, 0), (GLsizei)I(c, 1), (const GLfloat *)I(c, 2));
}

void APIENTRY glUniform1i(GLint l, GLint a);
static void x_glUniform1i(MX_NATIVE_CALL *c)
{
    glUniform1i((GLint)I(c, 0), (GLint)I(c, 1));
}

void APIENTRY glUniform1iv(GLint l, GLsizei n, const GLint *v);
static void x_glUniform1iv(MX_NATIVE_CALL *c)
{
    glUniform1iv((GLint)I(c, 0), (GLsizei)I(c, 1), (const GLint *)I(c, 2));
}

void APIENTRY glUniform1ui(GLint l, GLuint a);
static void x_glUniform1ui(MX_NATIVE_CALL *c)
{
    glUniform1ui((GLint)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glUniform1uiv(GLint l, GLsizei n, const GLuint *v);
static void x_glUniform1uiv(MX_NATIVE_CALL *c)
{
    glUniform1uiv((GLint)I(c, 0), (GLsizei)I(c, 1), (const GLuint *)I(c, 2));
}

void APIENTRY glUniform2f(GLint l, GLfloat a, GLfloat b);
static void x_glUniform2f(MX_NATIVE_CALL *c)
{
    glUniform2f((GLint)I(c, 0), F(c, 1), F(c, 2));
}

void APIENTRY glUniform2fv(GLint l, GLsizei n, const GLfloat *v);
static void x_glUniform2fv(MX_NATIVE_CALL *c)
{
    glUniform2fv((GLint)I(c, 0), (GLsizei)I(c, 1), (const GLfloat *)I(c, 2));
}

void APIENTRY glUniform2i(GLint l, GLint a, GLint b);
static void x_glUniform2i(MX_NATIVE_CALL *c)
{
    glUniform2i((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glUniform2iv(GLint l, GLsizei n, const GLint *v);
static void x_glUniform2iv(MX_NATIVE_CALL *c)
{
    glUniform2iv((GLint)I(c, 0), (GLsizei)I(c, 1), (const GLint *)I(c, 2));
}

void APIENTRY glUniform2ui(GLint l, GLuint a, GLuint b);
static void x_glUniform2ui(MX_NATIVE_CALL *c)
{
    glUniform2ui((GLint)I(c, 0), (GLuint)I(c, 1), (GLuint)I(c, 2));
}

void APIENTRY glUniform2uiv(GLint l, GLsizei n, const GLuint *v);
static void x_glUniform2uiv(MX_NATIVE_CALL *c)
{
    glUniform2uiv((GLint)I(c, 0), (GLsizei)I(c, 1), (const GLuint *)I(c, 2));
}

void APIENTRY glUniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c);
static void x_glUniform3f(MX_NATIVE_CALL *c)
{
    glUniform3f((GLint)I(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glUniform3fv(GLint l, GLsizei n, const GLfloat *v);
static void x_glUniform3fv(MX_NATIVE_CALL *c)
{
    glUniform3fv((GLint)I(c, 0), (GLsizei)I(c, 1), (const GLfloat *)I(c, 2));
}

void APIENTRY glUniform3i(GLint l, GLint a, GLint b, GLint c);
static void x_glUniform3i(MX_NATIVE_CALL *c)
{
    glUniform3i((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3));
}

void APIENTRY glUniform3iv(GLint l, GLsizei n, const GLint *v);
static void x_glUniform3iv(MX_NATIVE_CALL *c)
{
    glUniform3iv((GLint)I(c, 0), (GLsizei)I(c, 1), (const GLint *)I(c, 2));
}

void APIENTRY glUniform3ui(GLint l, GLuint a, GLuint b, GLuint c);
static void x_glUniform3ui(MX_NATIVE_CALL *c)
{
    glUniform3ui((GLint)I(c, 0), (GLuint)I(c, 1), (GLuint)I(c, 2), (GLuint)I(c, 3));
}

void APIENTRY glUniform3uiv(GLint l, GLsizei n, const GLuint *v);
static void x_glUniform3uiv(MX_NATIVE_CALL *c)
{
    glUniform3uiv((GLint)I(c, 0), (GLsizei)I(c, 1), (const GLuint *)I(c, 2));
}

void APIENTRY glUniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d);
static void x_glUniform4f(MX_NATIVE_CALL *c)
{
    glUniform4f((GLint)I(c, 0), F(c, 1), F(c, 2), F(c, 3), F(c, 4));
}

void APIENTRY glUniform4fv(GLint l, GLsizei n, const GLfloat *v);
static void x_glUniform4fv(MX_NATIVE_CALL *c)
{
    glUniform4fv((GLint)I(c, 0), (GLsizei)I(c, 1), (const GLfloat *)I(c, 2));
}

void APIENTRY glUniform4i(GLint l, GLint a, GLint b, GLint c, GLint d);
static void x_glUniform4i(MX_NATIVE_CALL *c)
{
    glUniform4i((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3), (GLint)I(c, 4));
}

void APIENTRY glUniform4iv(GLint l, GLsizei n, const GLint *v);
static void x_glUniform4iv(MX_NATIVE_CALL *c)
{
    glUniform4iv((GLint)I(c, 0), (GLsizei)I(c, 1), (const GLint *)I(c, 2));
}

void APIENTRY glUniform4ui(GLint l, GLuint a, GLuint b, GLuint c, GLuint d);
static void x_glUniform4ui(MX_NATIVE_CALL *c)
{
    glUniform4ui((GLint)I(c, 0), (GLuint)I(c, 1), (GLuint)I(c, 2), (GLuint)I(c, 3), (GLuint)I(c, 4));
}

void APIENTRY glUniform4uiv(GLint l, GLsizei n, const GLuint *v);
static void x_glUniform4uiv(MX_NATIVE_CALL *c)
{
    glUniform4uiv((GLint)I(c, 0), (GLsizei)I(c, 1), (const GLuint *)I(c, 2));
}

void APIENTRY glUniformBlockBinding(GLuint program, GLuint index, GLuint binding);
static void x_glUniformBlockBinding(MX_NATIVE_CALL *c)
{
    glUniformBlockBinding((GLuint)I(c, 0), (GLuint)I(c, 1), (GLuint)I(c, 2));
}

void APIENTRY glUniformMatrix2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
static void x_glUniformMatrix2fv(MX_NATIVE_CALL *c)
{
    glUniformMatrix2fv((GLint)I(c, 0), (GLsizei)I(c, 1), (GLboolean)I(c, 2), (const GLfloat *)I(c, 3));
}

void APIENTRY glUniformMatrix2x3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
static void x_glUniformMatrix2x3fv(MX_NATIVE_CALL *c)
{
    glUniformMatrix2x3fv((GLint)I(c, 0), (GLsizei)I(c, 1), (GLboolean)I(c, 2), (const GLfloat *)I(c, 3));
}

void APIENTRY glUniformMatrix2x4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
static void x_glUniformMatrix2x4fv(MX_NATIVE_CALL *c)
{
    glUniformMatrix2x4fv((GLint)I(c, 0), (GLsizei)I(c, 1), (GLboolean)I(c, 2), (const GLfloat *)I(c, 3));
}

void APIENTRY glUniformMatrix3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
static void x_glUniformMatrix3fv(MX_NATIVE_CALL *c)
{
    glUniformMatrix3fv((GLint)I(c, 0), (GLsizei)I(c, 1), (GLboolean)I(c, 2), (const GLfloat *)I(c, 3));
}

void APIENTRY glUniformMatrix3x2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
static void x_glUniformMatrix3x2fv(MX_NATIVE_CALL *c)
{
    glUniformMatrix3x2fv((GLint)I(c, 0), (GLsizei)I(c, 1), (GLboolean)I(c, 2), (const GLfloat *)I(c, 3));
}

void APIENTRY glUniformMatrix3x4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
static void x_glUniformMatrix3x4fv(MX_NATIVE_CALL *c)
{
    glUniformMatrix3x4fv((GLint)I(c, 0), (GLsizei)I(c, 1), (GLboolean)I(c, 2), (const GLfloat *)I(c, 3));
}

void APIENTRY glUniformMatrix4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
static void x_glUniformMatrix4fv(MX_NATIVE_CALL *c)
{
    glUniformMatrix4fv((GLint)I(c, 0), (GLsizei)I(c, 1), (GLboolean)I(c, 2), (const GLfloat *)I(c, 3));
}

void APIENTRY glUniformMatrix4x2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
static void x_glUniformMatrix4x2fv(MX_NATIVE_CALL *c)
{
    glUniformMatrix4x2fv((GLint)I(c, 0), (GLsizei)I(c, 1), (GLboolean)I(c, 2), (const GLfloat *)I(c, 3));
}

void APIENTRY glUniformMatrix4x3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
static void x_glUniformMatrix4x3fv(MX_NATIVE_CALL *c)
{
    glUniformMatrix4x3fv((GLint)I(c, 0), (GLsizei)I(c, 1), (GLboolean)I(c, 2), (const GLfloat *)I(c, 3));
}

GLboolean APIENTRY glUnmapBuffer(GLenum target);
static void x_glUnmapBuffer(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)glUnmapBuffer((GLenum)I(c, 0));
}

void APIENTRY glUseProgram(GLuint name);
static void x_glUseProgram(MX_NATIVE_CALL *c)
{
    glUseProgram((GLuint)I(c, 0));
}

void APIENTRY glValidateProgram(GLuint name);
static void x_glValidateProgram(MX_NATIVE_CALL *c)
{
    glValidateProgram((GLuint)I(c, 0));
}

void APIENTRY glVertex2d(GLdouble x, GLdouble y);
static void x_glVertex2d(MX_NATIVE_CALL *c)
{
    glVertex2d(D(c, 0), D(c, 1));
}

void APIENTRY glVertex2dv(const GLdouble *v);
static void x_glVertex2dv(MX_NATIVE_CALL *c)
{
    glVertex2dv((const GLdouble *)I(c, 0));
}

void APIENTRY glVertex2f(GLfloat x, GLfloat y);
static void x_glVertex2f(MX_NATIVE_CALL *c)
{
    glVertex2f(F(c, 0), F(c, 1));
}

void APIENTRY glVertex2fv(const GLfloat *v);
static void x_glVertex2fv(MX_NATIVE_CALL *c)
{
    glVertex2fv((const GLfloat *)I(c, 0));
}

void APIENTRY glVertex2i(GLint x, GLint y);
static void x_glVertex2i(MX_NATIVE_CALL *c)
{
    glVertex2i((GLint)I(c, 0), (GLint)I(c, 1));
}

void APIENTRY glVertex2iv(const GLint *v);
static void x_glVertex2iv(MX_NATIVE_CALL *c)
{
    glVertex2iv((const GLint *)I(c, 0));
}

void APIENTRY glVertex2s(GLshort x, GLshort y);
static void x_glVertex2s(MX_NATIVE_CALL *c)
{
    glVertex2s((GLshort)I(c, 0), (GLshort)I(c, 1));
}

void APIENTRY glVertex2sv(const GLshort *v);
static void x_glVertex2sv(MX_NATIVE_CALL *c)
{
    glVertex2sv((const GLshort *)I(c, 0));
}

void APIENTRY glVertex3d(GLdouble x, GLdouble y, GLdouble z);
static void x_glVertex3d(MX_NATIVE_CALL *c)
{
    glVertex3d(D(c, 0), D(c, 1), D(c, 2));
}

void APIENTRY glVertex3dv(const GLdouble *v);
static void x_glVertex3dv(MX_NATIVE_CALL *c)
{
    glVertex3dv((const GLdouble *)I(c, 0));
}

void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z);
static void x_glVertex3f(MX_NATIVE_CALL *c)
{
    glVertex3f(F(c, 0), F(c, 1), F(c, 2));
}

void APIENTRY glVertex3fv(const GLfloat *v);
static void x_glVertex3fv(MX_NATIVE_CALL *c)
{
    glVertex3fv((const GLfloat *)I(c, 0));
}

void APIENTRY glVertex3i(GLint x, GLint y, GLint z);
static void x_glVertex3i(MX_NATIVE_CALL *c)
{
    glVertex3i((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2));
}

void APIENTRY glVertex3iv(const GLint *v);
static void x_glVertex3iv(MX_NATIVE_CALL *c)
{
    glVertex3iv((const GLint *)I(c, 0));
}

void APIENTRY glVertex3s(GLshort x, GLshort y, GLshort z);
static void x_glVertex3s(MX_NATIVE_CALL *c)
{
    glVertex3s((GLshort)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2));
}

void APIENTRY glVertex3sv(const GLshort *v);
static void x_glVertex3sv(MX_NATIVE_CALL *c)
{
    glVertex3sv((const GLshort *)I(c, 0));
}

void APIENTRY glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
static void x_glVertex4d(MX_NATIVE_CALL *c)
{
    glVertex4d(D(c, 0), D(c, 1), D(c, 2), D(c, 3));
}

void APIENTRY glVertex4dv(const GLdouble *v);
static void x_glVertex4dv(MX_NATIVE_CALL *c)
{
    glVertex4dv((const GLdouble *)I(c, 0));
}

void APIENTRY glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
static void x_glVertex4f(MX_NATIVE_CALL *c)
{
    glVertex4f(F(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glVertex4fv(const GLfloat *v);
static void x_glVertex4fv(MX_NATIVE_CALL *c)
{
    glVertex4fv((const GLfloat *)I(c, 0));
}

void APIENTRY glVertex4i(GLint x, GLint y, GLint z, GLint w);
static void x_glVertex4i(MX_NATIVE_CALL *c)
{
    glVertex4i((GLint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3));
}

void APIENTRY glVertex4iv(const GLint *v);
static void x_glVertex4iv(MX_NATIVE_CALL *c)
{
    glVertex4iv((const GLint *)I(c, 0));
}

void APIENTRY glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w);
static void x_glVertex4s(MX_NATIVE_CALL *c)
{
    glVertex4s((GLshort)I(c, 0), (GLshort)I(c, 1), (GLshort)I(c, 2), (GLshort)I(c, 3));
}

void APIENTRY glVertex4sv(const GLshort *v);
static void x_glVertex4sv(MX_NATIVE_CALL *c)
{
    glVertex4sv((const GLshort *)I(c, 0));
}

void APIENTRY glVertexAttrib1f(GLuint i, GLfloat x);
static void x_glVertexAttrib1f(MX_NATIVE_CALL *c)
{
    glVertexAttrib1f((GLuint)I(c, 0), F(c, 1));
}

void APIENTRY glVertexAttrib1fv(GLuint i, const GLfloat *v);
static void x_glVertexAttrib1fv(MX_NATIVE_CALL *c)
{
    glVertexAttrib1fv((GLuint)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glVertexAttrib2f(GLuint i, GLfloat x, GLfloat y);
static void x_glVertexAttrib2f(MX_NATIVE_CALL *c)
{
    glVertexAttrib2f((GLuint)I(c, 0), F(c, 1), F(c, 2));
}

void APIENTRY glVertexAttrib2fv(GLuint i, const GLfloat *v);
static void x_glVertexAttrib2fv(MX_NATIVE_CALL *c)
{
    glVertexAttrib2fv((GLuint)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glVertexAttrib3f(GLuint i, GLfloat x, GLfloat y, GLfloat z);
static void x_glVertexAttrib3f(MX_NATIVE_CALL *c)
{
    glVertexAttrib3f((GLuint)I(c, 0), F(c, 1), F(c, 2), F(c, 3));
}

void APIENTRY glVertexAttrib3fv(GLuint i, const GLfloat *v);
static void x_glVertexAttrib3fv(MX_NATIVE_CALL *c)
{
    glVertexAttrib3fv((GLuint)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glVertexAttrib4Nub(GLuint i, GLubyte x, GLubyte y, GLubyte z, GLubyte w);
static void x_glVertexAttrib4Nub(MX_NATIVE_CALL *c)
{
    glVertexAttrib4Nub((GLuint)I(c, 0), (GLubyte)I(c, 1), (GLubyte)I(c, 2), (GLubyte)I(c, 3), (GLubyte)I(c, 4));
}

void APIENTRY glVertexAttrib4f(GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
static void x_glVertexAttrib4f(MX_NATIVE_CALL *c)
{
    glVertexAttrib4f((GLuint)I(c, 0), F(c, 1), F(c, 2), F(c, 3), F(c, 4));
}

void APIENTRY glVertexAttrib4fv(GLuint i, const GLfloat *v);
static void x_glVertexAttrib4fv(MX_NATIVE_CALL *c)
{
    glVertexAttrib4fv((GLuint)I(c, 0), (const GLfloat *)I(c, 1));
}

void APIENTRY glVertexAttribDivisor(GLuint index, GLuint divisor);
static void x_glVertexAttribDivisor(MX_NATIVE_CALL *c)
{
    glVertexAttribDivisor((GLuint)I(c, 0), (GLuint)I(c, 1));
}

void APIENTRY glVertexAttribI4i(GLuint i, GLint x, GLint y, GLint z, GLint w);
static void x_glVertexAttribI4i(MX_NATIVE_CALL *c)
{
    glVertexAttribI4i((GLuint)I(c, 0), (GLint)I(c, 1), (GLint)I(c, 2), (GLint)I(c, 3), (GLint)I(c, 4));
}

void APIENTRY glVertexAttribI4iv(GLuint i, const GLint *v);
static void x_glVertexAttribI4iv(MX_NATIVE_CALL *c)
{
    glVertexAttribI4iv((GLuint)I(c, 0), (const GLint *)I(c, 1));
}

void APIENTRY glVertexAttribI4ui(GLuint i, GLuint x, GLuint y, GLuint z, GLuint w);
static void x_glVertexAttribI4ui(MX_NATIVE_CALL *c)
{
    glVertexAttribI4ui((GLuint)I(c, 0), (GLuint)I(c, 1), (GLuint)I(c, 2), (GLuint)I(c, 3), (GLuint)I(c, 4));
}

void APIENTRY glVertexAttribI4uiv(GLuint i, const GLuint *v);
static void x_glVertexAttribI4uiv(MX_NATIVE_CALL *c)
{
    glVertexAttribI4uiv((GLuint)I(c, 0), (const GLuint *)I(c, 1));
}

void APIENTRY glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer);
static void x_glVertexAttribIPointer(MX_NATIVE_CALL *c)
{
    glVertexAttribIPointer((GLuint)I(c, 0), (GLint)I(c, 1), (GLenum)I(c, 2), (GLsizei)I(c, 3), (const void *)I(c, 4));
}

void APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,                                           const void *pointer);
static void x_glVertexAttribPointer(MX_NATIVE_CALL *c)
{
    glVertexAttribPointer((GLuint)I(c, 0), (GLint)I(c, 1), (GLenum)I(c, 2), (GLboolean)I(c, 3), (GLsizei)I(c, 4), (const void *)I(c, 5));
}

void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
static void x_glVertexPointer(MX_NATIVE_CALL *c)
{
    glVertexPointer((GLint)I(c, 0), (GLenum)I(c, 1), (GLsizei)I(c, 2), (const void *)I(c, 3));
}

void APIENTRY glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
static void x_glViewport(MX_NATIVE_CALL *c)
{
    glViewport((GLint)I(c, 0), (GLint)I(c, 1), (GLsizei)I(c, 2), (GLsizei)I(c, 3));
}

void APIENTRY glWaitSync(GLsync handle, GLbitfield flags, GLuint64 timeout);
static void x_glWaitSync(MX_NATIVE_CALL *c)
{
    glWaitSync((GLsync)I(c, 0), (GLbitfield)I(c, 1), (GLuint64)I(c, 2));
}

int APIENTRY wglChoosePixelFormat(HDC dc, const PIXELFORMATDESCRIPTOR *want);
static void x_wglChoosePixelFormat(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglChoosePixelFormat((HDC)I(c, 0), (const PIXELFORMATDESCRIPTOR *)I(c, 1));
}

BOOL APIENTRY wglChoosePixelFormatARB(HDC dc, const int *iattribs, const FLOAT *fattribs, UINT max, int *formats,                                            UINT *count);
static void x_wglChoosePixelFormatARB(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglChoosePixelFormatARB((HDC)I(c, 0), (const int *)I(c, 1), (const FLOAT *)I(c, 2), (UINT)I(c, 3), (int *)I(c, 4), (UINT *)I(c, 5));
}

BOOL APIENTRY wglCopyContext(HGLRC src, HGLRC dst, UINT mask);
static void x_wglCopyContext(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglCopyContext((HGLRC)I(c, 0), (HGLRC)I(c, 1), (UINT)I(c, 2));
}

HGLRC APIENTRY wglCreateContext(HDC dc);
static void x_wglCreateContext(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglCreateContext((HDC)I(c, 0));
}

HGLRC APIENTRY wglCreateContextAttribsARB(HDC dc, HGLRC share, const int *attribs);
static void x_wglCreateContextAttribsARB(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglCreateContextAttribsARB((HDC)I(c, 0), (HGLRC)I(c, 1), (const int *)I(c, 2));
}

HGLRC APIENTRY wglCreateLayerContext(HDC dc, int layer);
static void x_wglCreateLayerContext(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglCreateLayerContext((HDC)I(c, 0), (int)I(c, 1));
}

BOOL APIENTRY wglDeleteContext(HGLRC rc);
static void x_wglDeleteContext(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglDeleteContext((HGLRC)I(c, 0));
}

BOOL APIENTRY wglDescribeLayerPlane(HDC dc, int format, int layer, UINT size, LPLAYERPLANEDESCRIPTOR desc);
static void x_wglDescribeLayerPlane(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglDescribeLayerPlane((HDC)I(c, 0), (int)I(c, 1), (int)I(c, 2), (UINT)I(c, 3), (LPLAYERPLANEDESCRIPTOR)I(c, 4));
}

int APIENTRY wglDescribePixelFormat(HDC dc, int format, UINT size, PIXELFORMATDESCRIPTOR *pfd);
static void x_wglDescribePixelFormat(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglDescribePixelFormat((HDC)I(c, 0), (int)I(c, 1), (UINT)I(c, 2), (PIXELFORMATDESCRIPTOR *)I(c, 3));
}

HGLRC APIENTRY wglGetCurrentContext(void);
static void x_wglGetCurrentContext(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglGetCurrentContext();
}

HDC APIENTRY wglGetCurrentDC(void);
static void x_wglGetCurrentDC(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglGetCurrentDC();
}

HDC APIENTRY wglGetCurrentReadDCARB(void);
static void x_wglGetCurrentReadDCARB(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglGetCurrentReadDCARB();
}

const char* APIENTRY wglGetExtensionsStringARB(HDC dc);
static void x_wglGetExtensionsStringARB(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglGetExtensionsStringARB((HDC)I(c, 0));
}

const char* APIENTRY wglGetExtensionsStringEXT(void);
static void x_wglGetExtensionsStringEXT(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglGetExtensionsStringEXT();
}

int APIENTRY wglGetLayerPaletteEntries(HDC dc, int layer, int start, int count, COLORREF *entries);
static void x_wglGetLayerPaletteEntries(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglGetLayerPaletteEntries((HDC)I(c, 0), (int)I(c, 1), (int)I(c, 2), (int)I(c, 3), (COLORREF *)I(c, 4));
}

int APIENTRY wglGetPixelFormat(HDC dc);
static void x_wglGetPixelFormat(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglGetPixelFormat((HDC)I(c, 0));
}

BOOL APIENTRY wglGetPixelFormatAttribfvARB(HDC dc, int format, int layer, UINT count, const int *attribs,                                                 FLOAT *values);
static void x_wglGetPixelFormatAttribfvARB(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglGetPixelFormatAttribfvARB((HDC)I(c, 0), (int)I(c, 1), (int)I(c, 2), (UINT)I(c, 3), (const int *)I(c, 4), (FLOAT *)I(c, 5));
}

BOOL APIENTRY wglGetPixelFormatAttribivARB(HDC dc, int format, int layer, UINT count, const int *attribs,                                                 int *values);
static void x_wglGetPixelFormatAttribivARB(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglGetPixelFormatAttribivARB((HDC)I(c, 0), (int)I(c, 1), (int)I(c, 2), (UINT)I(c, 3), (const int *)I(c, 4), (int *)I(c, 5));
}

int APIENTRY wglGetSwapIntervalEXT(void);
static void x_wglGetSwapIntervalEXT(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglGetSwapIntervalEXT();
}

BOOL APIENTRY wglMakeContextCurrentARB(HDC draw, HDC read, HGLRC rc);
static void x_wglMakeContextCurrentARB(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglMakeContextCurrentARB((HDC)I(c, 0), (HDC)I(c, 1), (HGLRC)I(c, 2));
}

BOOL APIENTRY wglMakeCurrent(HDC dc, HGLRC rc);
static void x_wglMakeCurrent(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglMakeCurrent((HDC)I(c, 0), (HGLRC)I(c, 1));
}

BOOL APIENTRY wglRealizeLayerPalette(HDC dc, int layer, BOOL realize);
static void x_wglRealizeLayerPalette(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglRealizeLayerPalette((HDC)I(c, 0), (int)I(c, 1), (BOOL)I(c, 2));
}

int APIENTRY wglSetLayerPaletteEntries(HDC dc, int layer, int start, int count, const COLORREF *entries);
static void x_wglSetLayerPaletteEntries(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglSetLayerPaletteEntries((HDC)I(c, 0), (int)I(c, 1), (int)I(c, 2), (int)I(c, 3), (const COLORREF *)I(c, 4));
}

BOOL APIENTRY wglSetPixelFormat(HDC dc, int format, const PIXELFORMATDESCRIPTOR *pfd);
static void x_wglSetPixelFormat(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglSetPixelFormat((HDC)I(c, 0), (int)I(c, 1), (const PIXELFORMATDESCRIPTOR *)I(c, 2));
}

BOOL APIENTRY wglShareLists(HGLRC a, HGLRC b);
static void x_wglShareLists(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglShareLists((HGLRC)I(c, 0), (HGLRC)I(c, 1));
}

BOOL APIENTRY wglSwapBuffers(HDC dc);
static void x_wglSwapBuffers(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglSwapBuffers((HDC)I(c, 0));
}

BOOL APIENTRY wglSwapIntervalEXT(int interval);
static void x_wglSwapIntervalEXT(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglSwapIntervalEXT((int)I(c, 0));
}

BOOL APIENTRY wglSwapLayerBuffers(HDC dc, UINT planes);
static void x_wglSwapLayerBuffers(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglSwapLayerBuffers((HDC)I(c, 0), (UINT)I(c, 1));
}

BOOL APIENTRY wglUseFontBitmapsA(HDC dc, DWORD first, DWORD count, DWORD list_base);
static void x_wglUseFontBitmapsA(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglUseFontBitmapsA((HDC)I(c, 0), (DWORD)I(c, 1), (DWORD)I(c, 2), (DWORD)I(c, 3));
}

BOOL APIENTRY wglUseFontBitmapsW(HDC dc, DWORD first, DWORD count, DWORD list_base);
static void x_wglUseFontBitmapsW(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglUseFontBitmapsW((HDC)I(c, 0), (DWORD)I(c, 1), (DWORD)I(c, 2), (DWORD)I(c, 3));
}

BOOL APIENTRY wglUseFontOutlinesA(HDC dc, DWORD first, DWORD count, DWORD list_base, FLOAT deviation,                                        FLOAT extrusion, int format, LPGLYPHMETRICSFLOAT metrics);
static void x_wglUseFontOutlinesA(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglUseFontOutlinesA((HDC)I(c, 0), (DWORD)I(c, 1), (DWORD)I(c, 2), (DWORD)I(c, 3), (FLOAT)I(c, 4), (FLOAT)I(c, 5), (int)I(c, 6), (LPGLYPHMETRICSFLOAT)I(c, 7));
}

BOOL APIENTRY wglUseFontOutlinesW(HDC dc, DWORD first, DWORD count, DWORD list_base, FLOAT deviation,                                        FLOAT extrusion, int format, LPGLYPHMETRICSFLOAT metrics);
static void x_wglUseFontOutlinesW(MX_NATIVE_CALL *c)
{
    c->rax = (unsigned long long)(ULONG_PTR)wglUseFontOutlinesW((HDC)I(c, 0), (DWORD)I(c, 1), (DWORD)I(c, 2), (DWORD)I(c, 3), (FLOAT)I(c, 4), (FLOAT)I(c, 5), (int)I(c, 6), (LPGLYPHMETRICSFLOAT)I(c, 7));
}

static const MX_NATIVE_FN g_functions[] = {
    x_glAccum,
    x_glActiveTexture,
    x_glAlphaFunc,
    x_glAreTexturesResident,
    x_glArrayElement,
    x_glAttachShader,
    x_glBegin,
    x_glBeginQuery,
    x_glBindAttribLocation,
    x_glBindBuffer,
    x_glBindBufferBase,
    x_glBindBufferRange,
    x_glBindFragDataLocation,
    x_glBindFramebuffer,
    x_glBindRenderbuffer,
    x_glBindTexture,
    x_glBindVertexArray,
    x_glBitmap,
    x_glBlendColor,
    x_glBlendEquation,
    x_glBlendEquationSeparate,
    x_glBlendFunc,
    x_glBlendFuncSeparate,
    x_glBlitFramebuffer,
    x_glBufferData,
    x_glBufferStorage,
    x_glBufferSubData,
    x_glCallList,
    x_glCallLists,
    x_glCheckFramebufferStatus,
    x_glClear,
    x_glClearAccum,
    x_glClearBufferfi,
    x_glClearBufferfv,
    x_glClearBufferiv,
    x_glClearColor,
    x_glClearDepth,
    x_glClearDepthf,
    x_glClearIndex,
    x_glClearStencil,
    x_glClientActiveTexture,
    x_glClientActiveTextureARB,
    x_glClientWaitSync,
    x_glClipPlane,
    x_glColor3b,
    x_glColor3bv,
    x_glColor3d,
    x_glColor3dv,
    x_glColor3f,
    x_glColor3fv,
    x_glColor3i,
    x_glColor3iv,
    x_glColor3s,
    x_glColor3sv,
    x_glColor3ub,
    x_glColor3ubv,
    x_glColor3ui,
    x_glColor3uiv,
    x_glColor3us,
    x_glColor3usv,
    x_glColor4b,
    x_glColor4bv,
    x_glColor4d,
    x_glColor4dv,
    x_glColor4f,
    x_glColor4fv,
    x_glColor4i,
    x_glColor4iv,
    x_glColor4s,
    x_glColor4sv,
    x_glColor4ub,
    x_glColor4ubv,
    x_glColor4ui,
    x_glColor4uiv,
    x_glColor4us,
    x_glColor4usv,
    x_glColorMask,
    x_glColorMaski,
    x_glColorMaterial,
    x_glColorPointer,
    x_glCompileShader,
    x_glCompressedTexImage2D,
    x_glCompressedTexSubImage2D,
    x_glCopyBufferSubData,
    x_glCopyPixels,
    x_glCopyTexImage1D,
    x_glCopyTexImage2D,
    x_glCopyTexSubImage1D,
    x_glCopyTexSubImage2D,
    x_glCreateBuffers,
    x_glCreateProgram,
    x_glCreateShader,
    x_glCullFace,
    x_glDebugMessageCallback,
    x_glDebugMessageControl,
    x_glDebugMessageInsert,
    x_glDeleteBuffers,
    x_glDeleteFramebuffers,
    x_glDeleteLists,
    x_glDeleteProgram,
    x_glDeleteQueries,
    x_glDeleteRenderbuffers,
    x_glDeleteShader,
    x_glDeleteSync,
    x_glDeleteTextures,
    x_glDeleteVertexArrays,
    x_glDepthFunc,
    x_glDepthMask,
    x_glDepthRange,
    x_glDepthRangef,
    x_glDetachShader,
    x_glDisable,
    x_glDisableClientState,
    x_glDisableVertexAttribArray,
    x_glDisablei,
    x_glDiscardFramebufferEXT,
    x_glDrawArrays,
    x_glDrawArraysInstanced,
    x_glDrawArraysInstancedBaseInstance,
    x_glDrawBuffer,
    x_glDrawBuffers,
    x_glDrawElements,
    x_glDrawElementsBaseVertex,
    x_glDrawElementsInstanced,
    x_glDrawElementsInstancedBaseVertex,
    x_glDrawPixels,
    x_glDrawRangeElements,
    x_glDrawRangeElementsBaseVertex,
    x_glEdgeFlag,
    x_glEdgeFlagPointer,
    x_glEdgeFlagv,
    x_glEnable,
    x_glEnableClientState,
    x_glEnableVertexAttribArray,
    x_glEnablei,
    x_glEnd,
    x_glEndList,
    x_glEndQuery,
    x_glEvalCoord1d,
    x_glEvalCoord1dv,
    x_glEvalCoord1f,
    x_glEvalCoord1fv,
    x_glEvalCoord2d,
    x_glEvalCoord2dv,
    x_glEvalCoord2f,
    x_glEvalCoord2fv,
    x_glEvalMesh1,
    x_glEvalMesh2,
    x_glEvalPoint1,
    x_glEvalPoint2,
    x_glFeedbackBuffer,
    x_glFenceSync,
    x_glFinish,
    x_glFlush,
    x_glFlushMappedBufferRange,
    x_glFogf,
    x_glFogfv,
    x_glFogi,
    x_glFogiv,
    x_glFramebufferRenderbuffer,
    x_glFramebufferTexture,
    x_glFramebufferTexture1D,
    x_glFramebufferTexture2D,
    x_glFramebufferTextureLayer,
    x_glFrontFace,
    x_glFrustum,
    x_glGenBuffers,
    x_glGenFramebuffers,
    x_glGenLists,
    x_glGenQueries,
    x_glGenRenderbuffers,
    x_glGenTextures,
    x_glGenVertexArrays,
    x_glGenerateMipmap,
    x_glGetActiveAttrib,
    x_glGetActiveUniform,
    x_glGetActiveUniformBlockName,
    x_glGetActiveUniformBlockiv,
    x_glGetActiveUniformsiv,
    x_glGetAttachedShaders,
    x_glGetAttribLocation,
    x_glGetBooleani_v,
    x_glGetBooleanv,
    x_glGetBufferParameteri64v,
    x_glGetBufferParameteriv,
    x_glGetBufferPointerv,
    x_glGetBufferSubData,
    x_glGetClipPlane,
    x_glGetDebugMessageLog,
    x_glGetDoublev,
    x_glGetError,
    x_glGetFloatv,
    x_glGetFramebufferAttachmentParameteriv,
    x_glGetInteger64v,
    x_glGetIntegeri_v,
    x_glGetIntegerv,
    x_glGetLightfv,
    x_glGetLightiv,
    x_glGetMapdv,
    x_glGetMapfv,
    x_glGetMapiv,
    x_glGetMaterialfv,
    x_glGetMaterialiv,
    x_glGetPixelMapfv,
    x_glGetPixelMapuiv,
    x_glGetPixelMapusv,
    x_glGetPointerv,
    x_glGetPolygonStipple,
    x_glGetProgramInfoLog,
    x_glGetProgramiv,
    x_glGetQueryObjecti64v,
    x_glGetQueryObjectiv,
    x_glGetQueryObjectui64v,
    x_glGetQueryObjectuiv,
    x_glGetQueryiv,
    x_glGetRenderbufferParameteriv,
    x_glGetShaderInfoLog,
    x_glGetShaderPrecisionFormat,
    x_glGetShaderSource,
    x_glGetShaderiv,
    x_glGetString,
    x_glGetStringi,
    x_glGetSynciv,
    x_glGetTexEnvfv,
    x_glGetTexEnviv,
    x_glGetTexGendv,
    x_glGetTexGenfv,
    x_glGetTexGeniv,
    x_glGetTexImage,
    x_glGetTexLevelParameterfv,
    x_glGetTexLevelParameteriv,
    x_glGetTexParameterfv,
    x_glGetTexParameteriv,
    x_glGetUniformBlockIndex,
    x_glGetUniformLocation,
    x_glGetUniformfv,
    x_glGetUniformiv,
    x_glGetVertexAttribIiv,
    x_glGetVertexAttribIuiv,
    x_glGetVertexAttribPointerv,
    x_glGetVertexAttribfv,
    x_glGetVertexAttribiv,
    x_glHint,
    x_glIndexMask,
    x_glIndexPointer,
    x_glIndexd,
    x_glIndexdv,
    x_glIndexf,
    x_glIndexfv,
    x_glIndexi,
    x_glIndexiv,
    x_glIndexs,
    x_glIndexsv,
    x_glIndexub,
    x_glIndexubv,
    x_glInitNames,
    x_glInterleavedArrays,
    x_glInvalidateFramebuffer,
    x_glIsBuffer,
    x_glIsEnabled,
    x_glIsEnabledi,
    x_glIsFramebuffer,
    x_glIsList,
    x_glIsProgram,
    x_glIsQuery,
    x_glIsRenderbuffer,
    x_glIsShader,
    x_glIsSync,
    x_glIsTexture,
    x_glIsVertexArray,
    x_glLightModelf,
    x_glLightModelfv,
    x_glLightModeli,
    x_glLightModeliv,
    x_glLightf,
    x_glLightfv,
    x_glLighti,
    x_glLightiv,
    x_glLineStipple,
    x_glLineWidth,
    x_glLinkProgram,
    x_glListBase,
    x_glLoadIdentity,
    x_glLoadMatrixd,
    x_glLoadMatrixf,
    x_glLoadName,
    x_glLoadTransposeMatrixd,
    x_glLoadTransposeMatrixdARB,
    x_glLoadTransposeMatrixf,
    x_glLoadTransposeMatrixfARB,
    x_glLogicOp,
    x_glMap1d,
    x_glMap1f,
    x_glMap2d,
    x_glMap2f,
    x_glMapBuffer,
    x_glMapBufferRange,
    x_glMapGrid1d,
    x_glMapGrid1f,
    x_glMapGrid2d,
    x_glMapGrid2f,
    x_glMaterialf,
    x_glMaterialfv,
    x_glMateriali,
    x_glMaterialiv,
    x_glMatrixMode,
    x_glMultMatrixd,
    x_glMultMatrixf,
    x_glMultTransposeMatrixd,
    x_glMultTransposeMatrixdARB,
    x_glMultTransposeMatrixf,
    x_glMultTransposeMatrixfARB,
    x_glMultiDrawArrays,
    x_glMultiDrawElements,
    x_glMultiTexCoord1d,
    x_glMultiTexCoord1dARB,
    x_glMultiTexCoord1dv,
    x_glMultiTexCoord1dvARB,
    x_glMultiTexCoord1f,
    x_glMultiTexCoord1fARB,
    x_glMultiTexCoord1fv,
    x_glMultiTexCoord1fvARB,
    x_glMultiTexCoord1i,
    x_glMultiTexCoord1iARB,
    x_glMultiTexCoord1iv,
    x_glMultiTexCoord1ivARB,
    x_glMultiTexCoord1s,
    x_glMultiTexCoord1sARB,
    x_glMultiTexCoord1sv,
    x_glMultiTexCoord1svARB,
    x_glMultiTexCoord2d,
    x_glMultiTexCoord2dARB,
    x_glMultiTexCoord2dv,
    x_glMultiTexCoord2dvARB,
    x_glMultiTexCoord2f,
    x_glMultiTexCoord2fARB,
    x_glMultiTexCoord2fv,
    x_glMultiTexCoord2fvARB,
    x_glMultiTexCoord2i,
    x_glMultiTexCoord2iARB,
    x_glMultiTexCoord2iv,
    x_glMultiTexCoord2ivARB,
    x_glMultiTexCoord2s,
    x_glMultiTexCoord2sARB,
    x_glMultiTexCoord2sv,
    x_glMultiTexCoord2svARB,
    x_glMultiTexCoord3d,
    x_glMultiTexCoord3dARB,
    x_glMultiTexCoord3dv,
    x_glMultiTexCoord3dvARB,
    x_glMultiTexCoord3f,
    x_glMultiTexCoord3fARB,
    x_glMultiTexCoord3fv,
    x_glMultiTexCoord3fvARB,
    x_glMultiTexCoord3i,
    x_glMultiTexCoord3iARB,
    x_glMultiTexCoord3iv,
    x_glMultiTexCoord3ivARB,
    x_glMultiTexCoord3s,
    x_glMultiTexCoord3sARB,
    x_glMultiTexCoord3sv,
    x_glMultiTexCoord3svARB,
    x_glMultiTexCoord4d,
    x_glMultiTexCoord4dARB,
    x_glMultiTexCoord4dv,
    x_glMultiTexCoord4dvARB,
    x_glMultiTexCoord4f,
    x_glMultiTexCoord4fARB,
    x_glMultiTexCoord4fv,
    x_glMultiTexCoord4fvARB,
    x_glMultiTexCoord4i,
    x_glMultiTexCoord4iARB,
    x_glMultiTexCoord4iv,
    x_glMultiTexCoord4ivARB,
    x_glMultiTexCoord4s,
    x_glMultiTexCoord4sARB,
    x_glMultiTexCoord4sv,
    x_glMultiTexCoord4svARB,
    x_glNewList,
    x_glNormal3b,
    x_glNormal3bv,
    x_glNormal3d,
    x_glNormal3dv,
    x_glNormal3f,
    x_glNormal3fv,
    x_glNormal3i,
    x_glNormal3iv,
    x_glNormal3s,
    x_glNormal3sv,
    x_glNormalPointer,
    x_glObjectLabel,
    x_glOrtho,
    x_glPassThrough,
    x_glPixelMapfv,
    x_glPixelMapuiv,
    x_glPixelMapusv,
    x_glPixelStoref,
    x_glPixelStorei,
    x_glPixelTransferf,
    x_glPixelTransferi,
    x_glPixelZoom,
    x_glPointParameterf,
    x_glPointParameterfARB,
    x_glPointParameterfEXT,
    x_glPointParameterfv,
    x_glPointParameterfvARB,
    x_glPointParameterfvEXT,
    x_glPointParameteri,
    x_glPointParameteriv,
    x_glPointSize,
    x_glPolygonMode,
    x_glPolygonOffset,
    x_glPolygonStipple,
    x_glPopAttrib,
    x_glPopClientAttrib,
    x_glPopDebugGroup,
    x_glPopMatrix,
    x_glPopName,
    x_glPrioritizeTextures,
    x_glPushAttrib,
    x_glPushClientAttrib,
    x_glPushDebugGroup,
    x_glPushMatrix,
    x_glPushName,
    x_glQueryCounter,
    x_glRasterPos2d,
    x_glRasterPos2dv,
    x_glRasterPos2f,
    x_glRasterPos2fv,
    x_glRasterPos2i,
    x_glRasterPos2iv,
    x_glRasterPos2s,
    x_glRasterPos2sv,
    x_glRasterPos3d,
    x_glRasterPos3dv,
    x_glRasterPos3f,
    x_glRasterPos3fv,
    x_glRasterPos3i,
    x_glRasterPos3iv,
    x_glRasterPos3s,
    x_glRasterPos3sv,
    x_glRasterPos4d,
    x_glRasterPos4dv,
    x_glRasterPos4f,
    x_glRasterPos4fv,
    x_glRasterPos4i,
    x_glRasterPos4iv,
    x_glRasterPos4s,
    x_glRasterPos4sv,
    x_glReadBuffer,
    x_glReadPixels,
    x_glRectd,
    x_glRectdv,
    x_glRectf,
    x_glRectfv,
    x_glRecti,
    x_glRectiv,
    x_glRects,
    x_glRectsv,
    x_glReleaseShaderCompiler,
    x_glRenderMode,
    x_glRenderbufferStorage,
    x_glRenderbufferStorageMultisample,
    x_glRotated,
    x_glRotatef,
    x_glScaled,
    x_glScalef,
    x_glScissor,
    x_glSelectBuffer,
    x_glShadeModel,
    x_glShaderSource,
    x_glStencilFunc,
    x_glStencilFuncSeparate,
    x_glStencilMask,
    x_glStencilMaskSeparate,
    x_glStencilOp,
    x_glStencilOpSeparate,
    x_glTexCoord1d,
    x_glTexCoord1dv,
    x_glTexCoord1f,
    x_glTexCoord1fv,
    x_glTexCoord1i,
    x_glTexCoord1iv,
    x_glTexCoord1s,
    x_glTexCoord1sv,
    x_glTexCoord2d,
    x_glTexCoord2dv,
    x_glTexCoord2f,
    x_glTexCoord2fv,
    x_glTexCoord2i,
    x_glTexCoord2iv,
    x_glTexCoord2s,
    x_glTexCoord2sv,
    x_glTexCoord3d,
    x_glTexCoord3dv,
    x_glTexCoord3f,
    x_glTexCoord3fv,
    x_glTexCoord3i,
    x_glTexCoord3iv,
    x_glTexCoord3s,
    x_glTexCoord3sv,
    x_glTexCoord4d,
    x_glTexCoord4dv,
    x_glTexCoord4f,
    x_glTexCoord4fv,
    x_glTexCoord4i,
    x_glTexCoord4iv,
    x_glTexCoord4s,
    x_glTexCoord4sv,
    x_glTexCoordPointer,
    x_glTexEnvf,
    x_glTexEnvfv,
    x_glTexEnvi,
    x_glTexEnviv,
    x_glTexGend,
    x_glTexGendv,
    x_glTexGenf,
    x_glTexGenfv,
    x_glTexGeni,
    x_glTexGeniv,
    x_glTexImage1D,
    x_glTexImage2D,
    x_glTexImage2DMultisample,
    x_glTexImage3D,
    x_glTexParameterf,
    x_glTexParameterfv,
    x_glTexParameteri,
    x_glTexParameteriv,
    x_glTexStorage2D,
    x_glTexStorage3D,
    x_glTexSubImage1D,
    x_glTexSubImage2D,
    x_glTexSubImage3D,
    x_glTranslated,
    x_glTranslatef,
    x_glUniform1f,
    x_glUniform1fv,
    x_glUniform1i,
    x_glUniform1iv,
    x_glUniform1ui,
    x_glUniform1uiv,
    x_glUniform2f,
    x_glUniform2fv,
    x_glUniform2i,
    x_glUniform2iv,
    x_glUniform2ui,
    x_glUniform2uiv,
    x_glUniform3f,
    x_glUniform3fv,
    x_glUniform3i,
    x_glUniform3iv,
    x_glUniform3ui,
    x_glUniform3uiv,
    x_glUniform4f,
    x_glUniform4fv,
    x_glUniform4i,
    x_glUniform4iv,
    x_glUniform4ui,
    x_glUniform4uiv,
    x_glUniformBlockBinding,
    x_glUniformMatrix2fv,
    x_glUniformMatrix2x3fv,
    x_glUniformMatrix2x4fv,
    x_glUniformMatrix3fv,
    x_glUniformMatrix3x2fv,
    x_glUniformMatrix3x4fv,
    x_glUniformMatrix4fv,
    x_glUniformMatrix4x2fv,
    x_glUniformMatrix4x3fv,
    x_glUnmapBuffer,
    x_glUseProgram,
    x_glValidateProgram,
    x_glVertex2d,
    x_glVertex2dv,
    x_glVertex2f,
    x_glVertex2fv,
    x_glVertex2i,
    x_glVertex2iv,
    x_glVertex2s,
    x_glVertex2sv,
    x_glVertex3d,
    x_glVertex3dv,
    x_glVertex3f,
    x_glVertex3fv,
    x_glVertex3i,
    x_glVertex3iv,
    x_glVertex3s,
    x_glVertex3sv,
    x_glVertex4d,
    x_glVertex4dv,
    x_glVertex4f,
    x_glVertex4fv,
    x_glVertex4i,
    x_glVertex4iv,
    x_glVertex4s,
    x_glVertex4sv,
    x_glVertexAttrib1f,
    x_glVertexAttrib1fv,
    x_glVertexAttrib2f,
    x_glVertexAttrib2fv,
    x_glVertexAttrib3f,
    x_glVertexAttrib3fv,
    x_glVertexAttrib4Nub,
    x_glVertexAttrib4f,
    x_glVertexAttrib4fv,
    x_glVertexAttribDivisor,
    x_glVertexAttribI4i,
    x_glVertexAttribI4iv,
    x_glVertexAttribI4ui,
    x_glVertexAttribI4uiv,
    x_glVertexAttribIPointer,
    x_glVertexAttribPointer,
    x_glVertexPointer,
    x_glViewport,
    x_glWaitSync,
    x_wglChoosePixelFormat,
    x_wglChoosePixelFormatARB,
    x_wglCopyContext,
    x_wglCreateContext,
    x_wglCreateContextAttribsARB,
    x_wglCreateLayerContext,
    x_wglDeleteContext,
    x_wglDescribeLayerPlane,
    x_wglDescribePixelFormat,
    x_wglGetCurrentContext,
    x_wglGetCurrentDC,
    x_wglGetCurrentReadDCARB,
    x_wglGetExtensionsStringARB,
    x_wglGetExtensionsStringEXT,
    x_wglGetLayerPaletteEntries,
    x_wglGetPixelFormat,
    x_wglGetPixelFormatAttribfvARB,
    x_wglGetPixelFormatAttribivARB,
    x_wglGetSwapIntervalEXT,
    x_wglMakeContextCurrentARB,
    x_wglMakeCurrent,
    x_wglRealizeLayerPalette,
    x_wglSetLayerPaletteEntries,
    x_wglSetPixelFormat,
    x_wglShareLists,
    x_wglSwapBuffers,
    x_wglSwapIntervalEXT,
    x_wglSwapLayerBuffers,
    x_wglUseFontBitmapsA,
    x_wglUseFontBitmapsW,
    x_wglUseFontOutlinesA,
    x_wglUseFontOutlinesW,
};

const MX_NATIVE_TABLE MimeX64Thunks = {647, 0, g_functions};
