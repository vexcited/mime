/* generated, do not edit. */
#include "opengl32.h"

void APIENTRY glAccum(GLenum op, GLfloat value);
void APIENTRY glActiveTexture(GLenum texture);
void APIENTRY glAlphaFunc(GLenum func, GLfloat ref);
GLboolean APIENTRY glAreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences);
void APIENTRY glArrayElement(GLint i);
void APIENTRY glAttachShader(GLuint program, GLuint name);
void APIENTRY glBegin(GLenum mode);
void APIENTRY glBeginQuery(GLenum target, GLuint name);
void APIENTRY glBindAttribLocation(GLuint program, GLuint index, const GLchar *name);
void APIENTRY glBindBuffer(GLenum target, GLuint name);
void APIENTRY glBindBufferBase(GLenum target, GLuint index, GLuint name);
void APIENTRY glBindBufferRange(GLenum target, GLuint index, GLuint name, GLintptr offset, GLsizeiptr size);
void APIENTRY glBindFragDataLocation(GLuint program, GLuint color, const GLchar *name);
void APIENTRY glBindFramebuffer(GLenum target, GLuint name);
void APIENTRY glBindRenderbuffer(GLenum target, GLuint name);
void APIENTRY glBindTexture(GLenum target, GLuint name);
void APIENTRY glBindVertexArray(GLuint name);
void APIENTRY glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap);
void APIENTRY glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void APIENTRY glBlendEquation(GLenum mode);
void APIENTRY glBlendEquationSeparate(GLenum rgb, GLenum alpha);
void APIENTRY glBlendFunc(GLenum src, GLenum dst);
void APIENTRY glBlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha);
void APIENTRY glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1,                                       GLint dy1, GLbitfield mask, GLenum filter);
void APIENTRY glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void APIENTRY glBufferStorage(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags);
void APIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
void APIENTRY glCallList(GLuint list);
void APIENTRY glCallLists(GLsizei n, GLenum type, const GLvoid *lists);
GLenum APIENTRY glCheckFramebufferStatus(GLenum target);
void APIENTRY glClear(GLbitfield mask);
void APIENTRY glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void APIENTRY glClearBufferfi(GLenum buffer, GLint index, GLfloat depth, GLint stencil);
void APIENTRY glClearBufferfv(GLenum buffer, GLint index, const GLfloat *value);
void APIENTRY glClearBufferiv(GLenum buffer, GLint index, const GLint *value);
void APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void APIENTRY glClearDepth(GLdouble d);
void APIENTRY glClearDepthf(GLfloat d);
void APIENTRY glClearIndex(GLfloat c);
void APIENTRY glClearStencil(GLint s);
void APIENTRY glClientActiveTexture(GLenum texture);
void APIENTRY glClientActiveTextureARB(GLenum texture);
GLenum APIENTRY glClientWaitSync(GLsync handle, GLbitfield flags, GLuint64 timeout);
void APIENTRY glClipPlane(GLenum plane, const GLdouble *equation);
void APIENTRY glColor3b(GLbyte red, GLbyte green, GLbyte blue);
void APIENTRY glColor3bv(const GLbyte *v);
void APIENTRY glColor3d(GLdouble red, GLdouble green, GLdouble blue);
void APIENTRY glColor3dv(const GLdouble *v);
void APIENTRY glColor3f(GLfloat red, GLfloat green, GLfloat blue);
void APIENTRY glColor3fv(const GLfloat *v);
void APIENTRY glColor3i(GLint red, GLint green, GLint blue);
void APIENTRY glColor3iv(const GLint *v);
void APIENTRY glColor3s(GLshort red, GLshort green, GLshort blue);
void APIENTRY glColor3sv(const GLshort *v);
void APIENTRY glColor3ub(GLubyte red, GLubyte green, GLubyte blue);
void APIENTRY glColor3ubv(const GLubyte *v);
void APIENTRY glColor3ui(GLuint red, GLuint green, GLuint blue);
void APIENTRY glColor3uiv(const GLuint *v);
void APIENTRY glColor3us(GLushort red, GLushort green, GLushort blue);
void APIENTRY glColor3usv(const GLushort *v);
void APIENTRY glColor4b(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha);
void APIENTRY glColor4bv(const GLbyte *v);
void APIENTRY glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha);
void APIENTRY glColor4dv(const GLdouble *v);
void APIENTRY glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void APIENTRY glColor4fv(const GLfloat *v);
void APIENTRY glColor4i(GLint red, GLint green, GLint blue, GLint alpha);
void APIENTRY glColor4iv(const GLint *v);
void APIENTRY glColor4s(GLshort red, GLshort green, GLshort blue, GLshort alpha);
void APIENTRY glColor4sv(const GLshort *v);
void APIENTRY glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
void APIENTRY glColor4ubv(const GLubyte *v);
void APIENTRY glColor4ui(GLuint red, GLuint green, GLuint blue, GLuint alpha);
void APIENTRY glColor4uiv(const GLuint *v);
void APIENTRY glColor4us(GLushort red, GLushort green, GLushort blue, GLushort alpha);
void APIENTRY glColor4usv(const GLushort *v);
void APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void APIENTRY glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void APIENTRY glColorMaterial(GLenum face, GLenum mode);
void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void APIENTRY glCompileShader(GLuint name);
void APIENTRY glCompressedTexImage2D(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h,                                            GLint border, GLsizei size, const void *data);
void APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,                                               GLenum format, GLsizei size, const void *data);
void APIENTRY glCopyBufferSubData(GLenum read_target, GLenum write_target, GLintptr read_offset,                                         GLintptr write_offset, GLsizeiptr size);
void APIENTRY glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type);
void APIENTRY glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border);
void APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internal, GLint x, GLint y, GLsizei w,                                      GLsizei h, GLint border);
void APIENTRY glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint dx, GLint dy, GLint x, GLint y, GLsizei w,                                         GLsizei h);
void APIENTRY glCreateBuffers(GLsizei n, GLuint *buffers);
GLuint APIENTRY glCreateProgram(void);
GLuint APIENTRY glCreateShader(GLenum type);
void APIENTRY glCullFace(GLenum mode);
void APIENTRY glDebugMessageCallback(GLDEBUGPROC callback, const void *user);
void APIENTRY glDebugMessageControl(GLenum source, GLenum type, GLenum severity, GLsizei count,                                           const GLuint *ids, GLboolean enabled);
void APIENTRY glDebugMessageInsert(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,                                          const GLchar *buf);
void APIENTRY glDeleteBuffers(GLsizei n, const GLuint *buffers);
void APIENTRY glDeleteFramebuffers(GLsizei n, const GLuint *names);
void APIENTRY glDeleteLists(GLuint list, GLsizei range);
void APIENTRY glDeleteProgram(GLuint name);
void APIENTRY glDeleteQueries(GLsizei count, const GLuint *names);
void APIENTRY glDeleteRenderbuffers(GLsizei n, const GLuint *names);
void APIENTRY glDeleteShader(GLuint name);
void APIENTRY glDeleteSync(GLsync handle);
void APIENTRY glDeleteTextures(GLsizei n, const GLuint *textures);
void APIENTRY glDeleteVertexArrays(GLsizei n, const GLuint *arrays);
void APIENTRY glDepthFunc(GLenum func);
void APIENTRY glDepthMask(GLboolean flag);
void APIENTRY glDepthRange(GLdouble n, GLdouble f);
void APIENTRY glDepthRangef(GLfloat n, GLfloat f);
void APIENTRY glDetachShader(GLuint program, GLuint name);
void APIENTRY glDisable(GLenum cap);
void APIENTRY glDisableClientState(GLenum array);
void APIENTRY glDisableVertexAttribArray(GLuint index);
void APIENTRY glDisablei(GLenum cap, GLuint index);
void APIENTRY glDiscardFramebufferEXT(GLenum target, GLsizei n, const GLenum *attachments);
void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count);
void APIENTRY glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances);
void APIENTRY glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei instances,                                                       GLuint base_instance);
void APIENTRY glDrawBuffer(GLenum buf);
void APIENTRY glDrawBuffers(GLsizei n, const GLenum *bufs);
void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void APIENTRY glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices,                                              GLint base_vertex);
void APIENTRY glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices,                                             GLsizei instances);
void APIENTRY glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices,                                                       GLsizei instances, GLint base_vertex);
void APIENTRY glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void APIENTRY glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,                                         const void *indices);
void APIENTRY glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,                                                   const void *indices, GLint base_vertex);
void APIENTRY glEdgeFlag(GLboolean flag);
void APIENTRY glEdgeFlagPointer(GLsizei stride, const void *pointer);
void APIENTRY glEdgeFlagv(const GLboolean *flag);
void APIENTRY glEnable(GLenum cap);
void APIENTRY glEnableClientState(GLenum array);
void APIENTRY glEnableVertexAttribArray(GLuint index);
void APIENTRY glEnablei(GLenum cap, GLuint index);
void APIENTRY glEnd(void);
void APIENTRY glEndList(void);
void APIENTRY glEndQuery(GLenum target);
void APIENTRY glEvalCoord1d(GLdouble u);
void APIENTRY glEvalCoord1dv(const GLdouble *u);
void APIENTRY glEvalCoord1f(GLfloat u);
void APIENTRY glEvalCoord1fv(const GLfloat *u);
void APIENTRY glEvalCoord2d(GLdouble u, GLdouble v);
void APIENTRY glEvalCoord2dv(const GLdouble *u);
void APIENTRY glEvalCoord2f(GLfloat u, GLfloat v);
void APIENTRY glEvalCoord2fv(const GLfloat *u);
void APIENTRY glEvalMesh1(GLenum mode, GLint i1, GLint i2);
void APIENTRY glEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2);
void APIENTRY glEvalPoint1(GLint i);
void APIENTRY glEvalPoint2(GLint i, GLint j);
void APIENTRY glFeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer);
GLsync APIENTRY glFenceSync(GLenum condition, GLbitfield flags);
void APIENTRY glFinish(void);
void APIENTRY glFlush(void);
void APIENTRY glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length);
void APIENTRY glFogf(GLenum pname, GLfloat param);
void APIENTRY glFogfv(GLenum pname, const GLfloat *params);
void APIENTRY glFogi(GLenum pname, GLint param);
void APIENTRY glFogiv(GLenum pname, const GLint *params);
void APIENTRY glFramebufferRenderbuffer(GLenum target, GLenum point, GLenum rbtarget, GLuint rb);
void APIENTRY glFramebufferTexture(GLenum target, GLenum point, GLuint texture, GLint level);
void APIENTRY glFramebufferTexture1D(GLenum target, GLenum point, GLenum textarget, GLuint texture, GLint level);
void APIENTRY glFramebufferTexture2D(GLenum target, GLenum point, GLenum textarget, GLuint texture, GLint level);
void APIENTRY glFramebufferTextureLayer(GLenum target, GLenum point, GLuint texture, GLint level, GLint layer);
void APIENTRY glFrontFace(GLenum mode);
void APIENTRY glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top_, GLdouble near_val,                               GLdouble far_val);
void APIENTRY glGenBuffers(GLsizei n, GLuint *buffers);
void APIENTRY glGenFramebuffers(GLsizei n, GLuint *names);
GLuint APIENTRY glGenLists(GLsizei range);
void APIENTRY glGenQueries(GLsizei count, GLuint *names);
void APIENTRY glGenRenderbuffers(GLsizei n, GLuint *names);
void APIENTRY glGenTextures(GLsizei n, GLuint *textures);
void APIENTRY glGenVertexArrays(GLsizei n, GLuint *arrays);
void APIENTRY glGenerateMipmap(GLenum target);
void APIENTRY glGetActiveAttrib(GLuint name, GLuint index, GLsizei size, GLsizei *length, GLint *count,                                       GLenum *type, GLchar *out);
void APIENTRY glGetActiveUniform(GLuint name, GLuint index, GLsizei size, GLsizei *length, GLint *count,                                        GLenum *type, GLchar *out);
void APIENTRY glGetActiveUniformBlockName(GLuint program, GLuint index, GLsizei size, GLsizei *length, GLchar *name);
void APIENTRY glGetActiveUniformBlockiv(GLuint program, GLuint index, GLenum pname, GLint *params);
void APIENTRY glGetActiveUniformsiv(GLuint name, GLsizei count, const GLuint *indices, GLenum pname,                                           GLint *params);
void APIENTRY glGetAttachedShaders(GLuint program, GLsizei max, GLsizei *count, GLuint *shaders);
GLint APIENTRY glGetAttribLocation(GLuint name, const GLchar *attrib);
void APIENTRY glGetBooleani_v(GLenum target, GLuint index, GLboolean *data);
void APIENTRY glGetBooleanv(GLenum pname, GLboolean *data);
void APIENTRY glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params);
void APIENTRY glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params);
void APIENTRY glGetBufferPointerv(GLenum target, GLenum pname, void **params);
void APIENTRY glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void *data);
void APIENTRY glGetClipPlane(GLenum plane, GLdouble *equation);
GLuint APIENTRY glGetDebugMessageLog(GLuint count, GLsizei size, GLenum *sources, GLenum *types, GLuint *ids,                                            GLenum *severities, GLsizei *lengths, GLchar *log);
void APIENTRY glGetDoublev(GLenum pname, GLdouble *data);
GLenum APIENTRY glGetError(void);
void APIENTRY glGetFloatv(GLenum pname, GLfloat *data);
void APIENTRY glGetFramebufferAttachmentParameteriv(GLenum target, GLenum point, GLenum pname, GLint *params);
void APIENTRY glGetInteger64v(GLenum pname, GLint64 *data);
void APIENTRY glGetIntegeri_v(GLenum target, GLuint index, GLint *data);
void APIENTRY glGetIntegerv(GLenum pname, GLint *data);
void APIENTRY glGetLightfv(GLenum l, GLenum pname, GLfloat *params);
void APIENTRY glGetLightiv(GLenum l, GLenum pname, GLint *params);
void APIENTRY glGetMapdv(GLenum target, GLenum query, GLdouble *v);
void APIENTRY glGetMapfv(GLenum target, GLenum query, GLfloat *v);
void APIENTRY glGetMapiv(GLenum target, GLenum query, GLint *v);
void APIENTRY glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params);
void APIENTRY glGetMaterialiv(GLenum face, GLenum pname, GLint *params);
void APIENTRY glGetPixelMapfv(GLenum map, GLfloat *values);
void APIENTRY glGetPixelMapuiv(GLenum map, GLuint *values);
void APIENTRY glGetPixelMapusv(GLenum map, GLushort *values);
void APIENTRY glGetPointerv(GLenum pname, void **params);
void APIENTRY glGetPolygonStipple(GLubyte *mask);
void APIENTRY glGetProgramInfoLog(GLuint name, GLsizei size, GLsizei *length, GLchar *log);
void APIENTRY glGetProgramiv(GLuint name, GLenum pname, GLint *params);
void APIENTRY glGetQueryObjecti64v(GLuint name, GLenum pname, GLint64 *value);
void APIENTRY glGetQueryObjectiv(GLuint name, GLenum pname, GLint *value);
void APIENTRY glGetQueryObjectui64v(GLuint name, GLenum pname, GLuint64 *value);
void APIENTRY glGetQueryObjectuiv(GLuint name, GLenum pname, GLuint *value);
void APIENTRY glGetQueryiv(GLenum target, GLenum pname, GLint *value);
void APIENTRY glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params);
void APIENTRY glGetShaderInfoLog(GLuint name, GLsizei size, GLsizei *length, GLchar *log);
void APIENTRY glGetShaderPrecisionFormat(GLenum shader_type, GLenum precision, GLint *range, GLint *prec);
void APIENTRY glGetShaderSource(GLuint name, GLsizei size, GLsizei *length, GLchar *source);
void APIENTRY glGetShaderiv(GLuint name, GLenum pname, GLint *params);
const GLubyte* APIENTRY glGetString(GLenum name);
const GLubyte* APIENTRY glGetStringi(GLenum name, GLuint index);
void APIENTRY glGetSynciv(GLsync handle, GLenum pname, GLsizei size, GLsizei *length, GLint *values);
void APIENTRY glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params);
void APIENTRY glGetTexEnviv(GLenum target, GLenum pname, GLint *params);
void APIENTRY glGetTexGendv(GLenum coord, GLenum pname, GLdouble *params);
void APIENTRY glGetTexGenfv(GLenum coord, GLenum pname, GLfloat *params);
void APIENTRY glGetTexGeniv(GLenum coord, GLenum pname, GLint *params);
void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels);
void APIENTRY glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params);
void APIENTRY glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params);
void APIENTRY glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params);
void APIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint *params);
GLuint APIENTRY glGetUniformBlockIndex(GLuint program, const GLchar *name);
GLint APIENTRY glGetUniformLocation(GLuint name, const GLchar *uniform);
void APIENTRY glGetUniformfv(GLuint name, GLint location, GLfloat *params);
void APIENTRY glGetUniformiv(GLuint name, GLint location, GLint *params);
void APIENTRY glGetVertexAttribIiv(GLuint index, GLenum pname, GLint *params);
void APIENTRY glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params);
void APIENTRY glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer);
void APIENTRY glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params);
void APIENTRY glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params);
void APIENTRY glHint(GLenum target, GLenum mode);
void APIENTRY glIndexMask(GLuint mask);
void APIENTRY glIndexPointer(GLenum type, GLsizei stride, const void *pointer);
void APIENTRY glIndexd(GLdouble c);
void APIENTRY glIndexdv(const GLdouble *c);
void APIENTRY glIndexf(GLfloat c);
void APIENTRY glIndexfv(const GLfloat *c);
void APIENTRY glIndexi(GLint c);
void APIENTRY glIndexiv(const GLint *c);
void APIENTRY glIndexs(GLshort c);
void APIENTRY glIndexsv(const GLshort *c);
void APIENTRY glIndexub(GLubyte c);
void APIENTRY glIndexubv(const GLubyte *c);
void APIENTRY glInitNames(void);
void APIENTRY glInterleavedArrays(GLenum format, GLsizei stride, const void *pointer);
void APIENTRY glInvalidateFramebuffer(GLenum target, GLsizei n, const GLenum *attachments);
GLboolean APIENTRY glIsBuffer(GLuint name);
GLboolean APIENTRY glIsEnabled(GLenum cap);
GLboolean APIENTRY glIsEnabledi(GLenum cap, GLuint index);
GLboolean APIENTRY glIsFramebuffer(GLuint name);
GLboolean APIENTRY glIsList(GLuint list);
GLboolean APIENTRY glIsProgram(GLuint name);
GLboolean APIENTRY glIsQuery(GLuint name);
GLboolean APIENTRY glIsRenderbuffer(GLuint name);
GLboolean APIENTRY glIsShader(GLuint name);
GLboolean APIENTRY glIsSync(GLsync handle);
GLboolean APIENTRY glIsTexture(GLuint name);
GLboolean APIENTRY glIsVertexArray(GLuint name);
void APIENTRY glLightModelf(GLenum pname, GLfloat param);
void APIENTRY glLightModelfv(GLenum pname, const GLfloat *params);
void APIENTRY glLightModeli(GLenum pname, GLint param);
void APIENTRY glLightModeliv(GLenum pname, const GLint *params);
void APIENTRY glLightf(GLenum l, GLenum pname, GLfloat param);
void APIENTRY glLightfv(GLenum l, GLenum pname, const GLfloat *params);
void APIENTRY glLighti(GLenum l, GLenum pname, GLint param);
void APIENTRY glLightiv(GLenum l, GLenum pname, const GLint *params);
void APIENTRY glLineStipple(GLint factor, GLushort pattern);
void APIENTRY glLineWidth(GLfloat width);
void APIENTRY glLinkProgram(GLuint name);
void APIENTRY glListBase(GLuint base);
void APIENTRY glLoadIdentity(void);
void APIENTRY glLoadMatrixd(const GLdouble *m);
void APIENTRY glLoadMatrixf(const GLfloat *m);
void APIENTRY glLoadName(GLuint name);
void APIENTRY glLoadTransposeMatrixd(const GLdouble *m);
void APIENTRY glLoadTransposeMatrixdARB(const GLdouble *m);
void APIENTRY glLoadTransposeMatrixf(const GLfloat *m);
void APIENTRY glLoadTransposeMatrixfARB(const GLfloat *m);
void APIENTRY glLogicOp(GLenum opcode);
void APIENTRY glMap1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points);
void APIENTRY glMap1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points);
void APIENTRY glMap2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points);
void APIENTRY glMap2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points);
void* APIENTRY glMapBuffer(GLenum target, GLenum access);
void* APIENTRY glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
void APIENTRY glMapGrid1d(GLint un, GLdouble u1, GLdouble u2);
void APIENTRY glMapGrid1f(GLint un, GLfloat u1, GLfloat u2);
void APIENTRY glMapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2);
void APIENTRY glMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2);
void APIENTRY glMaterialf(GLenum face, GLenum pname, GLfloat param);
void APIENTRY glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);
void APIENTRY glMateriali(GLenum face, GLenum pname, GLint param);
void APIENTRY glMaterialiv(GLenum face, GLenum pname, const GLint *params);
void APIENTRY glMatrixMode(GLenum mode);
void APIENTRY glMultMatrixd(const GLdouble *m);
void APIENTRY glMultMatrixf(const GLfloat *m);
void APIENTRY glMultTransposeMatrixd(const GLdouble *m);
void APIENTRY glMultTransposeMatrixdARB(const GLdouble *m);
void APIENTRY glMultTransposeMatrixf(const GLfloat *m);
void APIENTRY glMultTransposeMatrixfARB(const GLfloat *m);
void APIENTRY glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei n);
void APIENTRY glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const void *const *indices,                                         GLsizei n);
void APIENTRY glMultiTexCoord1d(GLenum target, GLdouble s);
void APIENTRY glMultiTexCoord1dARB(GLenum target, GLdouble s);
void APIENTRY glMultiTexCoord1dv(GLenum target, const GLdouble *v);
void APIENTRY glMultiTexCoord1dvARB(GLenum target, const GLdouble *v);
void APIENTRY glMultiTexCoord1f(GLenum target, GLfloat s);
void APIENTRY glMultiTexCoord1fARB(GLenum target, GLfloat s);
void APIENTRY glMultiTexCoord1fv(GLenum target, const GLfloat *v);
void APIENTRY glMultiTexCoord1fvARB(GLenum target, const GLfloat *v);
void APIENTRY glMultiTexCoord1i(GLenum target, GLint s);
void APIENTRY glMultiTexCoord1iARB(GLenum target, GLint s);
void APIENTRY glMultiTexCoord1iv(GLenum target, const GLint *v);
void APIENTRY glMultiTexCoord1ivARB(GLenum target, const GLint *v);
void APIENTRY glMultiTexCoord1s(GLenum target, GLshort s);
void APIENTRY glMultiTexCoord1sARB(GLenum target, GLshort s);
void APIENTRY glMultiTexCoord1sv(GLenum target, const GLshort *v);
void APIENTRY glMultiTexCoord1svARB(GLenum target, const GLshort *v);
void APIENTRY glMultiTexCoord2d(GLenum target, GLdouble s, GLdouble t);
void APIENTRY glMultiTexCoord2dARB(GLenum target, GLdouble s, GLdouble t);
void APIENTRY glMultiTexCoord2dv(GLenum target, const GLdouble *v);
void APIENTRY glMultiTexCoord2dvARB(GLenum target, const GLdouble *v);
void APIENTRY glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t);
void APIENTRY glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t);
void APIENTRY glMultiTexCoord2fv(GLenum target, const GLfloat *v);
void APIENTRY glMultiTexCoord2fvARB(GLenum target, const GLfloat *v);
void APIENTRY glMultiTexCoord2i(GLenum target, GLint s, GLint t);
void APIENTRY glMultiTexCoord2iARB(GLenum target, GLint s, GLint t);
void APIENTRY glMultiTexCoord2iv(GLenum target, const GLint *v);
void APIENTRY glMultiTexCoord2ivARB(GLenum target, const GLint *v);
void APIENTRY glMultiTexCoord2s(GLenum target, GLshort s, GLshort t);
void APIENTRY glMultiTexCoord2sARB(GLenum target, GLshort s, GLshort t);
void APIENTRY glMultiTexCoord2sv(GLenum target, const GLshort *v);
void APIENTRY glMultiTexCoord2svARB(GLenum target, const GLshort *v);
void APIENTRY glMultiTexCoord3d(GLenum target, GLdouble s, GLdouble t, GLdouble r);
void APIENTRY glMultiTexCoord3dARB(GLenum target, GLdouble s, GLdouble t, GLdouble r);
void APIENTRY glMultiTexCoord3dv(GLenum target, const GLdouble *v);
void APIENTRY glMultiTexCoord3dvARB(GLenum target, const GLdouble *v);
void APIENTRY glMultiTexCoord3f(GLenum target, GLfloat s, GLfloat t, GLfloat r);
void APIENTRY glMultiTexCoord3fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r);
void APIENTRY glMultiTexCoord3fv(GLenum target, const GLfloat *v);
void APIENTRY glMultiTexCoord3fvARB(GLenum target, const GLfloat *v);
void APIENTRY glMultiTexCoord3i(GLenum target, GLint s, GLint t, GLint r);
void APIENTRY glMultiTexCoord3iARB(GLenum target, GLint s, GLint t, GLint r);
void APIENTRY glMultiTexCoord3iv(GLenum target, const GLint *v);
void APIENTRY glMultiTexCoord3ivARB(GLenum target, const GLint *v);
void APIENTRY glMultiTexCoord3s(GLenum target, GLshort s, GLshort t, GLshort r);
void APIENTRY glMultiTexCoord3sARB(GLenum target, GLshort s, GLshort t, GLshort r);
void APIENTRY glMultiTexCoord3sv(GLenum target, const GLshort *v);
void APIENTRY glMultiTexCoord3svARB(GLenum target, const GLshort *v);
void APIENTRY glMultiTexCoord4d(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
void APIENTRY glMultiTexCoord4dARB(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
void APIENTRY glMultiTexCoord4dv(GLenum target, const GLdouble *v);
void APIENTRY glMultiTexCoord4dvARB(GLenum target, const GLdouble *v);
void APIENTRY glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void APIENTRY glMultiTexCoord4fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void APIENTRY glMultiTexCoord4fv(GLenum target, const GLfloat *v);
void APIENTRY glMultiTexCoord4fvARB(GLenum target, const GLfloat *v);
void APIENTRY glMultiTexCoord4i(GLenum target, GLint s, GLint t, GLint r, GLint q);
void APIENTRY glMultiTexCoord4iARB(GLenum target, GLint s, GLint t, GLint r, GLint q);
void APIENTRY glMultiTexCoord4iv(GLenum target, const GLint *v);
void APIENTRY glMultiTexCoord4ivARB(GLenum target, const GLint *v);
void APIENTRY glMultiTexCoord4s(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
void APIENTRY glMultiTexCoord4sARB(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
void APIENTRY glMultiTexCoord4sv(GLenum target, const GLshort *v);
void APIENTRY glMultiTexCoord4svARB(GLenum target, const GLshort *v);
void APIENTRY glNewList(GLuint list, GLenum mode);
void APIENTRY glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz);
void APIENTRY glNormal3bv(const GLbyte *v);
void APIENTRY glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz);
void APIENTRY glNormal3dv(const GLdouble *v);
void APIENTRY glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
void APIENTRY glNormal3fv(const GLfloat *v);
void APIENTRY glNormal3i(GLint nx, GLint ny, GLint nz);
void APIENTRY glNormal3iv(const GLint *v);
void APIENTRY glNormal3s(GLshort nx, GLshort ny, GLshort nz);
void APIENTRY glNormal3sv(const GLshort *v);
void APIENTRY glNormalPointer(GLenum type, GLsizei stride, const void *pointer);
void APIENTRY glObjectLabel(GLenum identifier, GLuint name, GLsizei length, const GLchar *label);
void APIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top_, GLdouble near_val,                             GLdouble far_val);
void APIENTRY glPassThrough(GLfloat token);
void APIENTRY glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat *values);
void APIENTRY glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint *values);
void APIENTRY glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort *values);
void APIENTRY glPixelStoref(GLenum pname, GLfloat param);
void APIENTRY glPixelStorei(GLenum pname, GLint param);
void APIENTRY glPixelTransferf(GLenum pname, GLfloat param);
void APIENTRY glPixelTransferi(GLenum pname, GLint param);
void APIENTRY glPixelZoom(GLfloat xfactor, GLfloat yfactor);
void APIENTRY glPointParameterf(GLenum pname, GLfloat value);
void APIENTRY glPointParameterfARB(GLenum pname, GLfloat value);
void APIENTRY glPointParameterfEXT(GLenum pname, GLfloat value);
void APIENTRY glPointParameterfv(GLenum pname, const GLfloat *values);
void APIENTRY glPointParameterfvARB(GLenum pname, const GLfloat *values);
void APIENTRY glPointParameterfvEXT(GLenum pname, const GLfloat *values);
void APIENTRY glPointParameteri(GLenum pname, GLint value);
void APIENTRY glPointParameteriv(GLenum pname, const GLint *values);
void APIENTRY glPointSize(GLfloat size);
void APIENTRY glPolygonMode(GLenum face, GLenum mode);
void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units);
void APIENTRY glPolygonStipple(const GLubyte *mask);
void APIENTRY glPopAttrib(void);
void APIENTRY glPopClientAttrib(void);
void APIENTRY glPopDebugGroup(void);
void APIENTRY glPopMatrix(void);
void APIENTRY glPopName(void);
void APIENTRY glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities);
void APIENTRY glPushAttrib(GLbitfield mask);
void APIENTRY glPushClientAttrib(GLbitfield mask);
void APIENTRY glPushDebugGroup(GLenum source, GLuint id, GLsizei length, const GLchar *message);
void APIENTRY glPushMatrix(void);
void APIENTRY glPushName(GLuint name);
void APIENTRY glQueryCounter(GLuint name, GLenum target);
void APIENTRY glRasterPos2d(GLdouble x, GLdouble y);
void APIENTRY glRasterPos2dv(const GLdouble *v);
void APIENTRY glRasterPos2f(GLfloat x, GLfloat y);
void APIENTRY glRasterPos2fv(const GLfloat *v);
void APIENTRY glRasterPos2i(GLint x, GLint y);
void APIENTRY glRasterPos2iv(const GLint *v);
void APIENTRY glRasterPos2s(GLshort x, GLshort y);
void APIENTRY glRasterPos2sv(const GLshort *v);
void APIENTRY glRasterPos3d(GLdouble x, GLdouble y, GLdouble z);
void APIENTRY glRasterPos3dv(const GLdouble *v);
void APIENTRY glRasterPos3f(GLfloat x, GLfloat y, GLfloat z);
void APIENTRY glRasterPos3fv(const GLfloat *v);
void APIENTRY glRasterPos3i(GLint x, GLint y, GLint z);
void APIENTRY glRasterPos3iv(const GLint *v);
void APIENTRY glRasterPos3s(GLshort x, GLshort y, GLshort z);
void APIENTRY glRasterPos3sv(const GLshort *v);
void APIENTRY glRasterPos4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void APIENTRY glRasterPos4dv(const GLdouble *v);
void APIENTRY glRasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void APIENTRY glRasterPos4fv(const GLfloat *v);
void APIENTRY glRasterPos4i(GLint x, GLint y, GLint z, GLint w);
void APIENTRY glRasterPos4iv(const GLint *v);
void APIENTRY glRasterPos4s(GLshort x, GLshort y, GLshort z, GLshort w);
void APIENTRY glRasterPos4sv(const GLshort *v);
void APIENTRY glReadBuffer(GLenum buf);
void APIENTRY glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels);
void APIENTRY glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2);
void APIENTRY glRectdv(const GLdouble *v1, const GLdouble *v2);
void APIENTRY glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
void APIENTRY glRectfv(const GLfloat *v1, const GLfloat *v2);
void APIENTRY glRecti(GLint x1, GLint y1, GLint x2, GLint y2);
void APIENTRY glRectiv(const GLint *v1, const GLint *v2);
void APIENTRY glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2);
void APIENTRY glRectsv(const GLshort *v1, const GLshort *v2);
void APIENTRY glReleaseShaderCompiler(void);
GLint APIENTRY glRenderMode(GLenum mode);
void APIENTRY glRenderbufferStorage(GLenum target, GLenum internal, GLsizei w, GLsizei h);
void APIENTRY glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internal, GLsizei w,                                                      GLsizei h);
void APIENTRY glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void APIENTRY glScaled(GLdouble x, GLdouble y, GLdouble z);
void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z);
void APIENTRY glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
void APIENTRY glSelectBuffer(GLsizei size, GLuint *buffer);
void APIENTRY glShadeModel(GLenum mode);
void APIENTRY glShaderSource(GLuint name, GLsizei count, const GLchar *const *strings, const GLint *lengths);
void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask);
void APIENTRY glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
void APIENTRY glStencilMask(GLuint mask);
void APIENTRY glStencilMaskSeparate(GLenum face, GLuint mask);
void APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void APIENTRY glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass);
void APIENTRY glTexCoord1d(GLdouble s);
void APIENTRY glTexCoord1dv(const GLdouble *v);
void APIENTRY glTexCoord1f(GLfloat s);
void APIENTRY glTexCoord1fv(const GLfloat *v);
void APIENTRY glTexCoord1i(GLint s);
void APIENTRY glTexCoord1iv(const GLint *v);
void APIENTRY glTexCoord1s(GLshort s);
void APIENTRY glTexCoord1sv(const GLshort *v);
void APIENTRY glTexCoord2d(GLdouble s, GLdouble t);
void APIENTRY glTexCoord2dv(const GLdouble *v);
void APIENTRY glTexCoord2f(GLfloat s, GLfloat t);
void APIENTRY glTexCoord2fv(const GLfloat *v);
void APIENTRY glTexCoord2i(GLint s, GLint t);
void APIENTRY glTexCoord2iv(const GLint *v);
void APIENTRY glTexCoord2s(GLshort s, GLshort t);
void APIENTRY glTexCoord2sv(const GLshort *v);
void APIENTRY glTexCoord3d(GLdouble s, GLdouble t, GLdouble r);
void APIENTRY glTexCoord3dv(const GLdouble *v);
void APIENTRY glTexCoord3f(GLfloat s, GLfloat t, GLfloat r);
void APIENTRY glTexCoord3fv(const GLfloat *v);
void APIENTRY glTexCoord3i(GLint s, GLint t, GLint r);
void APIENTRY glTexCoord3iv(const GLint *v);
void APIENTRY glTexCoord3s(GLshort s, GLshort t, GLshort r);
void APIENTRY glTexCoord3sv(const GLshort *v);
void APIENTRY glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q);
void APIENTRY glTexCoord4dv(const GLdouble *v);
void APIENTRY glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void APIENTRY glTexCoord4fv(const GLfloat *v);
void APIENTRY glTexCoord4i(GLint s, GLint t, GLint r, GLint q);
void APIENTRY glTexCoord4iv(const GLint *v);
void APIENTRY glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q);
void APIENTRY glTexCoord4sv(const GLshort *v);
void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param);
void APIENTRY glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);
void APIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param);
void APIENTRY glTexEnviv(GLenum target, GLenum pname, const GLint *params);
void APIENTRY glTexGend(GLenum coord, GLenum pname, GLdouble param);
void APIENTRY glTexGendv(GLenum coord, GLenum pname, const GLdouble *params);
void APIENTRY glTexGenf(GLenum coord, GLenum pname, GLfloat param);
void APIENTRY glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params);
void APIENTRY glTexGeni(GLenum coord, GLenum pname, GLint param);
void APIENTRY glTexGeniv(GLenum coord, GLenum pname, const GLint *params);
void APIENTRY glTexImage1D(GLenum target, GLint level, GLint internal, GLsizei w, GLint border, GLenum format,                                  GLenum type, const void *pixels);
void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,                                  GLenum format, GLenum type, const void *pixels);
void APIENTRY glTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internal, GLsizei w, GLsizei h,                                             GLboolean fixed);
void APIENTRY glTexImage3D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d,                                  GLint border, GLenum format, GLenum type, const void *pixels);
void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param);
void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param);
void APIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint *params);
void APIENTRY glTexStorage2D(GLenum target, GLsizei levels, GLenum internal, GLsizei w, GLsizei h);
void APIENTRY glTexStorage3D(GLenum target, GLsizei levels, GLenum internal, GLsizei w, GLsizei h, GLsizei d);
void APIENTRY glTexSubImage1D(GLenum target, GLint level, GLint x, GLsizei w, GLenum format, GLenum type,                                     const void *pixels);
void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,                                     GLenum type, const void *pixels);
void APIENTRY glTexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h,                                     GLsizei d, GLenum format, GLenum type, const void *pixels);
void APIENTRY glTranslated(GLdouble x, GLdouble y, GLdouble z);
void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void APIENTRY glUniform1f(GLint l, GLfloat a);
void APIENTRY glUniform1fv(GLint l, GLsizei n, const GLfloat *v);
void APIENTRY glUniform1i(GLint l, GLint a);
void APIENTRY glUniform1iv(GLint l, GLsizei n, const GLint *v);
void APIENTRY glUniform1ui(GLint l, GLuint a);
void APIENTRY glUniform1uiv(GLint l, GLsizei n, const GLuint *v);
void APIENTRY glUniform2f(GLint l, GLfloat a, GLfloat b);
void APIENTRY glUniform2fv(GLint l, GLsizei n, const GLfloat *v);
void APIENTRY glUniform2i(GLint l, GLint a, GLint b);
void APIENTRY glUniform2iv(GLint l, GLsizei n, const GLint *v);
void APIENTRY glUniform2ui(GLint l, GLuint a, GLuint b);
void APIENTRY glUniform2uiv(GLint l, GLsizei n, const GLuint *v);
void APIENTRY glUniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c);
void APIENTRY glUniform3fv(GLint l, GLsizei n, const GLfloat *v);
void APIENTRY glUniform3i(GLint l, GLint a, GLint b, GLint c);
void APIENTRY glUniform3iv(GLint l, GLsizei n, const GLint *v);
void APIENTRY glUniform3ui(GLint l, GLuint a, GLuint b, GLuint c);
void APIENTRY glUniform3uiv(GLint l, GLsizei n, const GLuint *v);
void APIENTRY glUniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d);
void APIENTRY glUniform4fv(GLint l, GLsizei n, const GLfloat *v);
void APIENTRY glUniform4i(GLint l, GLint a, GLint b, GLint c, GLint d);
void APIENTRY glUniform4iv(GLint l, GLsizei n, const GLint *v);
void APIENTRY glUniform4ui(GLint l, GLuint a, GLuint b, GLuint c, GLuint d);
void APIENTRY glUniform4uiv(GLint l, GLsizei n, const GLuint *v);
void APIENTRY glUniformBlockBinding(GLuint program, GLuint index, GLuint binding);
void APIENTRY glUniformMatrix2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY glUniformMatrix2x3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY glUniformMatrix2x4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY glUniformMatrix3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY glUniformMatrix3x2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY glUniformMatrix3x4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY glUniformMatrix4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY glUniformMatrix4x2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
void APIENTRY glUniformMatrix4x3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v);
GLboolean APIENTRY glUnmapBuffer(GLenum target);
void APIENTRY glUseProgram(GLuint name);
void APIENTRY glValidateProgram(GLuint name);
void APIENTRY glVertex2d(GLdouble x, GLdouble y);
void APIENTRY glVertex2dv(const GLdouble *v);
void APIENTRY glVertex2f(GLfloat x, GLfloat y);
void APIENTRY glVertex2fv(const GLfloat *v);
void APIENTRY glVertex2i(GLint x, GLint y);
void APIENTRY glVertex2iv(const GLint *v);
void APIENTRY glVertex2s(GLshort x, GLshort y);
void APIENTRY glVertex2sv(const GLshort *v);
void APIENTRY glVertex3d(GLdouble x, GLdouble y, GLdouble z);
void APIENTRY glVertex3dv(const GLdouble *v);
void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void APIENTRY glVertex3fv(const GLfloat *v);
void APIENTRY glVertex3i(GLint x, GLint y, GLint z);
void APIENTRY glVertex3iv(const GLint *v);
void APIENTRY glVertex3s(GLshort x, GLshort y, GLshort z);
void APIENTRY glVertex3sv(const GLshort *v);
void APIENTRY glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void APIENTRY glVertex4dv(const GLdouble *v);
void APIENTRY glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void APIENTRY glVertex4fv(const GLfloat *v);
void APIENTRY glVertex4i(GLint x, GLint y, GLint z, GLint w);
void APIENTRY glVertex4iv(const GLint *v);
void APIENTRY glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w);
void APIENTRY glVertex4sv(const GLshort *v);
void APIENTRY glVertexAttrib1f(GLuint i, GLfloat x);
void APIENTRY glVertexAttrib1fv(GLuint i, const GLfloat *v);
void APIENTRY glVertexAttrib2f(GLuint i, GLfloat x, GLfloat y);
void APIENTRY glVertexAttrib2fv(GLuint i, const GLfloat *v);
void APIENTRY glVertexAttrib3f(GLuint i, GLfloat x, GLfloat y, GLfloat z);
void APIENTRY glVertexAttrib3fv(GLuint i, const GLfloat *v);
void APIENTRY glVertexAttrib4Nub(GLuint i, GLubyte x, GLubyte y, GLubyte z, GLubyte w);
void APIENTRY glVertexAttrib4f(GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void APIENTRY glVertexAttrib4fv(GLuint i, const GLfloat *v);
void APIENTRY glVertexAttribDivisor(GLuint index, GLuint divisor);
void APIENTRY glVertexAttribI4i(GLuint i, GLint x, GLint y, GLint z, GLint w);
void APIENTRY glVertexAttribI4iv(GLuint i, const GLint *v);
void APIENTRY glVertexAttribI4ui(GLuint i, GLuint x, GLuint y, GLuint z, GLuint w);
void APIENTRY glVertexAttribI4uiv(GLuint i, const GLuint *v);
void APIENTRY glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer);
void APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,                                           const void *pointer);
void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void APIENTRY glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void APIENTRY glWaitSync(GLsync handle, GLbitfield flags, GLuint64 timeout);
int APIENTRY wglChoosePixelFormat(HDC dc, const PIXELFORMATDESCRIPTOR *want);
BOOL APIENTRY wglChoosePixelFormatARB(HDC dc, const int *iattribs, const FLOAT *fattribs, UINT max, int *formats,                                            UINT *count);
BOOL APIENTRY wglCopyContext(HGLRC src, HGLRC dst, UINT mask);
HGLRC APIENTRY wglCreateContext(HDC dc);
HGLRC APIENTRY wglCreateContextAttribsARB(HDC dc, HGLRC share, const int *attribs);
HGLRC APIENTRY wglCreateLayerContext(HDC dc, int layer);
BOOL APIENTRY wglDeleteContext(HGLRC rc);
BOOL APIENTRY wglDescribeLayerPlane(HDC dc, int format, int layer, UINT size, LPLAYERPLANEDESCRIPTOR desc);
int APIENTRY wglDescribePixelFormat(HDC dc, int format, UINT size, PIXELFORMATDESCRIPTOR *pfd);
HGLRC APIENTRY wglGetCurrentContext(void);
HDC APIENTRY wglGetCurrentDC(void);
HDC APIENTRY wglGetCurrentReadDCARB(void);
const char* APIENTRY wglGetExtensionsStringARB(HDC dc);
const char* APIENTRY wglGetExtensionsStringEXT(void);
int APIENTRY wglGetLayerPaletteEntries(HDC dc, int layer, int start, int count, COLORREF *entries);
int APIENTRY wglGetPixelFormat(HDC dc);
BOOL APIENTRY wglGetPixelFormatAttribfvARB(HDC dc, int format, int layer, UINT count, const int *attribs,                                                 FLOAT *values);
BOOL APIENTRY wglGetPixelFormatAttribivARB(HDC dc, int format, int layer, UINT count, const int *attribs,                                                 int *values);
PROC APIENTRY wglGetProcAddress(LPCSTR name);
int APIENTRY wglGetSwapIntervalEXT(void);
BOOL APIENTRY wglMakeContextCurrentARB(HDC draw, HDC read, HGLRC rc);
BOOL APIENTRY wglMakeCurrent(HDC dc, HGLRC rc);
BOOL APIENTRY wglRealizeLayerPalette(HDC dc, int layer, BOOL realize);
int APIENTRY wglSetLayerPaletteEntries(HDC dc, int layer, int start, int count, const COLORREF *entries);
BOOL APIENTRY wglSetPixelFormat(HDC dc, int format, const PIXELFORMATDESCRIPTOR *pfd);
BOOL APIENTRY wglShareLists(HGLRC a, HGLRC b);
BOOL APIENTRY wglSwapBuffers(HDC dc);
BOOL APIENTRY wglSwapIntervalEXT(int interval);
BOOL APIENTRY wglSwapLayerBuffers(HDC dc, UINT planes);
BOOL APIENTRY wglUseFontBitmapsA(HDC dc, DWORD first, DWORD count, DWORD list_base);
BOOL APIENTRY wglUseFontBitmapsW(HDC dc, DWORD first, DWORD count, DWORD list_base);
BOOL APIENTRY wglUseFontOutlinesA(HDC dc, DWORD first, DWORD count, DWORD list_base, FLOAT deviation,                                        FLOAT extrusion, int format, LPGLYPHMETRICSFLOAT metrics);
BOOL APIENTRY wglUseFontOutlinesW(HDC dc, DWORD first, DWORD count, DWORD list_base, FLOAT deviation,                                        FLOAT extrusion, int format, LPGLYPHMETRICSFLOAT metrics);

const GL_PROC GlProcs[] = {
    {"glAccum", (const void *)glAccum},
    {"glActiveTexture", (const void *)glActiveTexture},
    {"glAlphaFunc", (const void *)glAlphaFunc},
    {"glAreTexturesResident", (const void *)glAreTexturesResident},
    {"glArrayElement", (const void *)glArrayElement},
    {"glAttachShader", (const void *)glAttachShader},
    {"glBegin", (const void *)glBegin},
    {"glBeginQuery", (const void *)glBeginQuery},
    {"glBindAttribLocation", (const void *)glBindAttribLocation},
    {"glBindBuffer", (const void *)glBindBuffer},
    {"glBindBufferBase", (const void *)glBindBufferBase},
    {"glBindBufferRange", (const void *)glBindBufferRange},
    {"glBindFragDataLocation", (const void *)glBindFragDataLocation},
    {"glBindFramebuffer", (const void *)glBindFramebuffer},
    {"glBindRenderbuffer", (const void *)glBindRenderbuffer},
    {"glBindTexture", (const void *)glBindTexture},
    {"glBindVertexArray", (const void *)glBindVertexArray},
    {"glBitmap", (const void *)glBitmap},
    {"glBlendColor", (const void *)glBlendColor},
    {"glBlendEquation", (const void *)glBlendEquation},
    {"glBlendEquationSeparate", (const void *)glBlendEquationSeparate},
    {"glBlendFunc", (const void *)glBlendFunc},
    {"glBlendFuncSeparate", (const void *)glBlendFuncSeparate},
    {"glBlitFramebuffer", (const void *)glBlitFramebuffer},
    {"glBufferData", (const void *)glBufferData},
    {"glBufferStorage", (const void *)glBufferStorage},
    {"glBufferSubData", (const void *)glBufferSubData},
    {"glCallList", (const void *)glCallList},
    {"glCallLists", (const void *)glCallLists},
    {"glCheckFramebufferStatus", (const void *)glCheckFramebufferStatus},
    {"glClear", (const void *)glClear},
    {"glClearAccum", (const void *)glClearAccum},
    {"glClearBufferfi", (const void *)glClearBufferfi},
    {"glClearBufferfv", (const void *)glClearBufferfv},
    {"glClearBufferiv", (const void *)glClearBufferiv},
    {"glClearColor", (const void *)glClearColor},
    {"glClearDepth", (const void *)glClearDepth},
    {"glClearDepthf", (const void *)glClearDepthf},
    {"glClearIndex", (const void *)glClearIndex},
    {"glClearStencil", (const void *)glClearStencil},
    {"glClientActiveTexture", (const void *)glClientActiveTexture},
    {"glClientActiveTextureARB", (const void *)glClientActiveTextureARB},
    {"glClientWaitSync", (const void *)glClientWaitSync},
    {"glClipPlane", (const void *)glClipPlane},
    {"glColor3b", (const void *)glColor3b},
    {"glColor3bv", (const void *)glColor3bv},
    {"glColor3d", (const void *)glColor3d},
    {"glColor3dv", (const void *)glColor3dv},
    {"glColor3f", (const void *)glColor3f},
    {"glColor3fv", (const void *)glColor3fv},
    {"glColor3i", (const void *)glColor3i},
    {"glColor3iv", (const void *)glColor3iv},
    {"glColor3s", (const void *)glColor3s},
    {"glColor3sv", (const void *)glColor3sv},
    {"glColor3ub", (const void *)glColor3ub},
    {"glColor3ubv", (const void *)glColor3ubv},
    {"glColor3ui", (const void *)glColor3ui},
    {"glColor3uiv", (const void *)glColor3uiv},
    {"glColor3us", (const void *)glColor3us},
    {"glColor3usv", (const void *)glColor3usv},
    {"glColor4b", (const void *)glColor4b},
    {"glColor4bv", (const void *)glColor4bv},
    {"glColor4d", (const void *)glColor4d},
    {"glColor4dv", (const void *)glColor4dv},
    {"glColor4f", (const void *)glColor4f},
    {"glColor4fv", (const void *)glColor4fv},
    {"glColor4i", (const void *)glColor4i},
    {"glColor4iv", (const void *)glColor4iv},
    {"glColor4s", (const void *)glColor4s},
    {"glColor4sv", (const void *)glColor4sv},
    {"glColor4ub", (const void *)glColor4ub},
    {"glColor4ubv", (const void *)glColor4ubv},
    {"glColor4ui", (const void *)glColor4ui},
    {"glColor4uiv", (const void *)glColor4uiv},
    {"glColor4us", (const void *)glColor4us},
    {"glColor4usv", (const void *)glColor4usv},
    {"glColorMask", (const void *)glColorMask},
    {"glColorMaski", (const void *)glColorMaski},
    {"glColorMaterial", (const void *)glColorMaterial},
    {"glColorPointer", (const void *)glColorPointer},
    {"glCompileShader", (const void *)glCompileShader},
    {"glCompressedTexImage2D", (const void *)glCompressedTexImage2D},
    {"glCompressedTexSubImage2D", (const void *)glCompressedTexSubImage2D},
    {"glCopyBufferSubData", (const void *)glCopyBufferSubData},
    {"glCopyPixels", (const void *)glCopyPixels},
    {"glCopyTexImage1D", (const void *)glCopyTexImage1D},
    {"glCopyTexImage2D", (const void *)glCopyTexImage2D},
    {"glCopyTexSubImage1D", (const void *)glCopyTexSubImage1D},
    {"glCopyTexSubImage2D", (const void *)glCopyTexSubImage2D},
    {"glCreateBuffers", (const void *)glCreateBuffers},
    {"glCreateProgram", (const void *)glCreateProgram},
    {"glCreateShader", (const void *)glCreateShader},
    {"glCullFace", (const void *)glCullFace},
    {"glDebugMessageCallback", (const void *)glDebugMessageCallback},
    {"glDebugMessageControl", (const void *)glDebugMessageControl},
    {"glDebugMessageInsert", (const void *)glDebugMessageInsert},
    {"glDeleteBuffers", (const void *)glDeleteBuffers},
    {"glDeleteFramebuffers", (const void *)glDeleteFramebuffers},
    {"glDeleteLists", (const void *)glDeleteLists},
    {"glDeleteProgram", (const void *)glDeleteProgram},
    {"glDeleteQueries", (const void *)glDeleteQueries},
    {"glDeleteRenderbuffers", (const void *)glDeleteRenderbuffers},
    {"glDeleteShader", (const void *)glDeleteShader},
    {"glDeleteSync", (const void *)glDeleteSync},
    {"glDeleteTextures", (const void *)glDeleteTextures},
    {"glDeleteVertexArrays", (const void *)glDeleteVertexArrays},
    {"glDepthFunc", (const void *)glDepthFunc},
    {"glDepthMask", (const void *)glDepthMask},
    {"glDepthRange", (const void *)glDepthRange},
    {"glDepthRangef", (const void *)glDepthRangef},
    {"glDetachShader", (const void *)glDetachShader},
    {"glDisable", (const void *)glDisable},
    {"glDisableClientState", (const void *)glDisableClientState},
    {"glDisableVertexAttribArray", (const void *)glDisableVertexAttribArray},
    {"glDisablei", (const void *)glDisablei},
    {"glDiscardFramebufferEXT", (const void *)glDiscardFramebufferEXT},
    {"glDrawArrays", (const void *)glDrawArrays},
    {"glDrawArraysInstanced", (const void *)glDrawArraysInstanced},
    {"glDrawArraysInstancedBaseInstance", (const void *)glDrawArraysInstancedBaseInstance},
    {"glDrawBuffer", (const void *)glDrawBuffer},
    {"glDrawBuffers", (const void *)glDrawBuffers},
    {"glDrawElements", (const void *)glDrawElements},
    {"glDrawElementsBaseVertex", (const void *)glDrawElementsBaseVertex},
    {"glDrawElementsInstanced", (const void *)glDrawElementsInstanced},
    {"glDrawElementsInstancedBaseVertex", (const void *)glDrawElementsInstancedBaseVertex},
    {"glDrawPixels", (const void *)glDrawPixels},
    {"glDrawRangeElements", (const void *)glDrawRangeElements},
    {"glDrawRangeElementsBaseVertex", (const void *)glDrawRangeElementsBaseVertex},
    {"glEdgeFlag", (const void *)glEdgeFlag},
    {"glEdgeFlagPointer", (const void *)glEdgeFlagPointer},
    {"glEdgeFlagv", (const void *)glEdgeFlagv},
    {"glEnable", (const void *)glEnable},
    {"glEnableClientState", (const void *)glEnableClientState},
    {"glEnableVertexAttribArray", (const void *)glEnableVertexAttribArray},
    {"glEnablei", (const void *)glEnablei},
    {"glEnd", (const void *)glEnd},
    {"glEndList", (const void *)glEndList},
    {"glEndQuery", (const void *)glEndQuery},
    {"glEvalCoord1d", (const void *)glEvalCoord1d},
    {"glEvalCoord1dv", (const void *)glEvalCoord1dv},
    {"glEvalCoord1f", (const void *)glEvalCoord1f},
    {"glEvalCoord1fv", (const void *)glEvalCoord1fv},
    {"glEvalCoord2d", (const void *)glEvalCoord2d},
    {"glEvalCoord2dv", (const void *)glEvalCoord2dv},
    {"glEvalCoord2f", (const void *)glEvalCoord2f},
    {"glEvalCoord2fv", (const void *)glEvalCoord2fv},
    {"glEvalMesh1", (const void *)glEvalMesh1},
    {"glEvalMesh2", (const void *)glEvalMesh2},
    {"glEvalPoint1", (const void *)glEvalPoint1},
    {"glEvalPoint2", (const void *)glEvalPoint2},
    {"glFeedbackBuffer", (const void *)glFeedbackBuffer},
    {"glFenceSync", (const void *)glFenceSync},
    {"glFinish", (const void *)glFinish},
    {"glFlush", (const void *)glFlush},
    {"glFlushMappedBufferRange", (const void *)glFlushMappedBufferRange},
    {"glFogf", (const void *)glFogf},
    {"glFogfv", (const void *)glFogfv},
    {"glFogi", (const void *)glFogi},
    {"glFogiv", (const void *)glFogiv},
    {"glFramebufferRenderbuffer", (const void *)glFramebufferRenderbuffer},
    {"glFramebufferTexture", (const void *)glFramebufferTexture},
    {"glFramebufferTexture1D", (const void *)glFramebufferTexture1D},
    {"glFramebufferTexture2D", (const void *)glFramebufferTexture2D},
    {"glFramebufferTextureLayer", (const void *)glFramebufferTextureLayer},
    {"glFrontFace", (const void *)glFrontFace},
    {"glFrustum", (const void *)glFrustum},
    {"glGenBuffers", (const void *)glGenBuffers},
    {"glGenFramebuffers", (const void *)glGenFramebuffers},
    {"glGenLists", (const void *)glGenLists},
    {"glGenQueries", (const void *)glGenQueries},
    {"glGenRenderbuffers", (const void *)glGenRenderbuffers},
    {"glGenTextures", (const void *)glGenTextures},
    {"glGenVertexArrays", (const void *)glGenVertexArrays},
    {"glGenerateMipmap", (const void *)glGenerateMipmap},
    {"glGetActiveAttrib", (const void *)glGetActiveAttrib},
    {"glGetActiveUniform", (const void *)glGetActiveUniform},
    {"glGetActiveUniformBlockName", (const void *)glGetActiveUniformBlockName},
    {"glGetActiveUniformBlockiv", (const void *)glGetActiveUniformBlockiv},
    {"glGetActiveUniformsiv", (const void *)glGetActiveUniformsiv},
    {"glGetAttachedShaders", (const void *)glGetAttachedShaders},
    {"glGetAttribLocation", (const void *)glGetAttribLocation},
    {"glGetBooleani_v", (const void *)glGetBooleani_v},
    {"glGetBooleanv", (const void *)glGetBooleanv},
    {"glGetBufferParameteri64v", (const void *)glGetBufferParameteri64v},
    {"glGetBufferParameteriv", (const void *)glGetBufferParameteriv},
    {"glGetBufferPointerv", (const void *)glGetBufferPointerv},
    {"glGetBufferSubData", (const void *)glGetBufferSubData},
    {"glGetClipPlane", (const void *)glGetClipPlane},
    {"glGetDebugMessageLog", (const void *)glGetDebugMessageLog},
    {"glGetDoublev", (const void *)glGetDoublev},
    {"glGetError", (const void *)glGetError},
    {"glGetFloatv", (const void *)glGetFloatv},
    {"glGetFramebufferAttachmentParameteriv", (const void *)glGetFramebufferAttachmentParameteriv},
    {"glGetInteger64v", (const void *)glGetInteger64v},
    {"glGetIntegeri_v", (const void *)glGetIntegeri_v},
    {"glGetIntegerv", (const void *)glGetIntegerv},
    {"glGetLightfv", (const void *)glGetLightfv},
    {"glGetLightiv", (const void *)glGetLightiv},
    {"glGetMapdv", (const void *)glGetMapdv},
    {"glGetMapfv", (const void *)glGetMapfv},
    {"glGetMapiv", (const void *)glGetMapiv},
    {"glGetMaterialfv", (const void *)glGetMaterialfv},
    {"glGetMaterialiv", (const void *)glGetMaterialiv},
    {"glGetPixelMapfv", (const void *)glGetPixelMapfv},
    {"glGetPixelMapuiv", (const void *)glGetPixelMapuiv},
    {"glGetPixelMapusv", (const void *)glGetPixelMapusv},
    {"glGetPointerv", (const void *)glGetPointerv},
    {"glGetPolygonStipple", (const void *)glGetPolygonStipple},
    {"glGetProgramInfoLog", (const void *)glGetProgramInfoLog},
    {"glGetProgramiv", (const void *)glGetProgramiv},
    {"glGetQueryObjecti64v", (const void *)glGetQueryObjecti64v},
    {"glGetQueryObjectiv", (const void *)glGetQueryObjectiv},
    {"glGetQueryObjectui64v", (const void *)glGetQueryObjectui64v},
    {"glGetQueryObjectuiv", (const void *)glGetQueryObjectuiv},
    {"glGetQueryiv", (const void *)glGetQueryiv},
    {"glGetRenderbufferParameteriv", (const void *)glGetRenderbufferParameteriv},
    {"glGetShaderInfoLog", (const void *)glGetShaderInfoLog},
    {"glGetShaderPrecisionFormat", (const void *)glGetShaderPrecisionFormat},
    {"glGetShaderSource", (const void *)glGetShaderSource},
    {"glGetShaderiv", (const void *)glGetShaderiv},
    {"glGetString", (const void *)glGetString},
    {"glGetStringi", (const void *)glGetStringi},
    {"glGetSynciv", (const void *)glGetSynciv},
    {"glGetTexEnvfv", (const void *)glGetTexEnvfv},
    {"glGetTexEnviv", (const void *)glGetTexEnviv},
    {"glGetTexGendv", (const void *)glGetTexGendv},
    {"glGetTexGenfv", (const void *)glGetTexGenfv},
    {"glGetTexGeniv", (const void *)glGetTexGeniv},
    {"glGetTexImage", (const void *)glGetTexImage},
    {"glGetTexLevelParameterfv", (const void *)glGetTexLevelParameterfv},
    {"glGetTexLevelParameteriv", (const void *)glGetTexLevelParameteriv},
    {"glGetTexParameterfv", (const void *)glGetTexParameterfv},
    {"glGetTexParameteriv", (const void *)glGetTexParameteriv},
    {"glGetUniformBlockIndex", (const void *)glGetUniformBlockIndex},
    {"glGetUniformLocation", (const void *)glGetUniformLocation},
    {"glGetUniformfv", (const void *)glGetUniformfv},
    {"glGetUniformiv", (const void *)glGetUniformiv},
    {"glGetVertexAttribIiv", (const void *)glGetVertexAttribIiv},
    {"glGetVertexAttribIuiv", (const void *)glGetVertexAttribIuiv},
    {"glGetVertexAttribPointerv", (const void *)glGetVertexAttribPointerv},
    {"glGetVertexAttribfv", (const void *)glGetVertexAttribfv},
    {"glGetVertexAttribiv", (const void *)glGetVertexAttribiv},
    {"glHint", (const void *)glHint},
    {"glIndexMask", (const void *)glIndexMask},
    {"glIndexPointer", (const void *)glIndexPointer},
    {"glIndexd", (const void *)glIndexd},
    {"glIndexdv", (const void *)glIndexdv},
    {"glIndexf", (const void *)glIndexf},
    {"glIndexfv", (const void *)glIndexfv},
    {"glIndexi", (const void *)glIndexi},
    {"glIndexiv", (const void *)glIndexiv},
    {"glIndexs", (const void *)glIndexs},
    {"glIndexsv", (const void *)glIndexsv},
    {"glIndexub", (const void *)glIndexub},
    {"glIndexubv", (const void *)glIndexubv},
    {"glInitNames", (const void *)glInitNames},
    {"glInterleavedArrays", (const void *)glInterleavedArrays},
    {"glInvalidateFramebuffer", (const void *)glInvalidateFramebuffer},
    {"glIsBuffer", (const void *)glIsBuffer},
    {"glIsEnabled", (const void *)glIsEnabled},
    {"glIsEnabledi", (const void *)glIsEnabledi},
    {"glIsFramebuffer", (const void *)glIsFramebuffer},
    {"glIsList", (const void *)glIsList},
    {"glIsProgram", (const void *)glIsProgram},
    {"glIsQuery", (const void *)glIsQuery},
    {"glIsRenderbuffer", (const void *)glIsRenderbuffer},
    {"glIsShader", (const void *)glIsShader},
    {"glIsSync", (const void *)glIsSync},
    {"glIsTexture", (const void *)glIsTexture},
    {"glIsVertexArray", (const void *)glIsVertexArray},
    {"glLightModelf", (const void *)glLightModelf},
    {"glLightModelfv", (const void *)glLightModelfv},
    {"glLightModeli", (const void *)glLightModeli},
    {"glLightModeliv", (const void *)glLightModeliv},
    {"glLightf", (const void *)glLightf},
    {"glLightfv", (const void *)glLightfv},
    {"glLighti", (const void *)glLighti},
    {"glLightiv", (const void *)glLightiv},
    {"glLineStipple", (const void *)glLineStipple},
    {"glLineWidth", (const void *)glLineWidth},
    {"glLinkProgram", (const void *)glLinkProgram},
    {"glListBase", (const void *)glListBase},
    {"glLoadIdentity", (const void *)glLoadIdentity},
    {"glLoadMatrixd", (const void *)glLoadMatrixd},
    {"glLoadMatrixf", (const void *)glLoadMatrixf},
    {"glLoadName", (const void *)glLoadName},
    {"glLoadTransposeMatrixd", (const void *)glLoadTransposeMatrixd},
    {"glLoadTransposeMatrixdARB", (const void *)glLoadTransposeMatrixdARB},
    {"glLoadTransposeMatrixf", (const void *)glLoadTransposeMatrixf},
    {"glLoadTransposeMatrixfARB", (const void *)glLoadTransposeMatrixfARB},
    {"glLogicOp", (const void *)glLogicOp},
    {"glMap1d", (const void *)glMap1d},
    {"glMap1f", (const void *)glMap1f},
    {"glMap2d", (const void *)glMap2d},
    {"glMap2f", (const void *)glMap2f},
    {"glMapBuffer", (const void *)glMapBuffer},
    {"glMapBufferRange", (const void *)glMapBufferRange},
    {"glMapGrid1d", (const void *)glMapGrid1d},
    {"glMapGrid1f", (const void *)glMapGrid1f},
    {"glMapGrid2d", (const void *)glMapGrid2d},
    {"glMapGrid2f", (const void *)glMapGrid2f},
    {"glMaterialf", (const void *)glMaterialf},
    {"glMaterialfv", (const void *)glMaterialfv},
    {"glMateriali", (const void *)glMateriali},
    {"glMaterialiv", (const void *)glMaterialiv},
    {"glMatrixMode", (const void *)glMatrixMode},
    {"glMultMatrixd", (const void *)glMultMatrixd},
    {"glMultMatrixf", (const void *)glMultMatrixf},
    {"glMultTransposeMatrixd", (const void *)glMultTransposeMatrixd},
    {"glMultTransposeMatrixdARB", (const void *)glMultTransposeMatrixdARB},
    {"glMultTransposeMatrixf", (const void *)glMultTransposeMatrixf},
    {"glMultTransposeMatrixfARB", (const void *)glMultTransposeMatrixfARB},
    {"glMultiDrawArrays", (const void *)glMultiDrawArrays},
    {"glMultiDrawElements", (const void *)glMultiDrawElements},
    {"glMultiTexCoord1d", (const void *)glMultiTexCoord1d},
    {"glMultiTexCoord1dARB", (const void *)glMultiTexCoord1dARB},
    {"glMultiTexCoord1dv", (const void *)glMultiTexCoord1dv},
    {"glMultiTexCoord1dvARB", (const void *)glMultiTexCoord1dvARB},
    {"glMultiTexCoord1f", (const void *)glMultiTexCoord1f},
    {"glMultiTexCoord1fARB", (const void *)glMultiTexCoord1fARB},
    {"glMultiTexCoord1fv", (const void *)glMultiTexCoord1fv},
    {"glMultiTexCoord1fvARB", (const void *)glMultiTexCoord1fvARB},
    {"glMultiTexCoord1i", (const void *)glMultiTexCoord1i},
    {"glMultiTexCoord1iARB", (const void *)glMultiTexCoord1iARB},
    {"glMultiTexCoord1iv", (const void *)glMultiTexCoord1iv},
    {"glMultiTexCoord1ivARB", (const void *)glMultiTexCoord1ivARB},
    {"glMultiTexCoord1s", (const void *)glMultiTexCoord1s},
    {"glMultiTexCoord1sARB", (const void *)glMultiTexCoord1sARB},
    {"glMultiTexCoord1sv", (const void *)glMultiTexCoord1sv},
    {"glMultiTexCoord1svARB", (const void *)glMultiTexCoord1svARB},
    {"glMultiTexCoord2d", (const void *)glMultiTexCoord2d},
    {"glMultiTexCoord2dARB", (const void *)glMultiTexCoord2dARB},
    {"glMultiTexCoord2dv", (const void *)glMultiTexCoord2dv},
    {"glMultiTexCoord2dvARB", (const void *)glMultiTexCoord2dvARB},
    {"glMultiTexCoord2f", (const void *)glMultiTexCoord2f},
    {"glMultiTexCoord2fARB", (const void *)glMultiTexCoord2fARB},
    {"glMultiTexCoord2fv", (const void *)glMultiTexCoord2fv},
    {"glMultiTexCoord2fvARB", (const void *)glMultiTexCoord2fvARB},
    {"glMultiTexCoord2i", (const void *)glMultiTexCoord2i},
    {"glMultiTexCoord2iARB", (const void *)glMultiTexCoord2iARB},
    {"glMultiTexCoord2iv", (const void *)glMultiTexCoord2iv},
    {"glMultiTexCoord2ivARB", (const void *)glMultiTexCoord2ivARB},
    {"glMultiTexCoord2s", (const void *)glMultiTexCoord2s},
    {"glMultiTexCoord2sARB", (const void *)glMultiTexCoord2sARB},
    {"glMultiTexCoord2sv", (const void *)glMultiTexCoord2sv},
    {"glMultiTexCoord2svARB", (const void *)glMultiTexCoord2svARB},
    {"glMultiTexCoord3d", (const void *)glMultiTexCoord3d},
    {"glMultiTexCoord3dARB", (const void *)glMultiTexCoord3dARB},
    {"glMultiTexCoord3dv", (const void *)glMultiTexCoord3dv},
    {"glMultiTexCoord3dvARB", (const void *)glMultiTexCoord3dvARB},
    {"glMultiTexCoord3f", (const void *)glMultiTexCoord3f},
    {"glMultiTexCoord3fARB", (const void *)glMultiTexCoord3fARB},
    {"glMultiTexCoord3fv", (const void *)glMultiTexCoord3fv},
    {"glMultiTexCoord3fvARB", (const void *)glMultiTexCoord3fvARB},
    {"glMultiTexCoord3i", (const void *)glMultiTexCoord3i},
    {"glMultiTexCoord3iARB", (const void *)glMultiTexCoord3iARB},
    {"glMultiTexCoord3iv", (const void *)glMultiTexCoord3iv},
    {"glMultiTexCoord3ivARB", (const void *)glMultiTexCoord3ivARB},
    {"glMultiTexCoord3s", (const void *)glMultiTexCoord3s},
    {"glMultiTexCoord3sARB", (const void *)glMultiTexCoord3sARB},
    {"glMultiTexCoord3sv", (const void *)glMultiTexCoord3sv},
    {"glMultiTexCoord3svARB", (const void *)glMultiTexCoord3svARB},
    {"glMultiTexCoord4d", (const void *)glMultiTexCoord4d},
    {"glMultiTexCoord4dARB", (const void *)glMultiTexCoord4dARB},
    {"glMultiTexCoord4dv", (const void *)glMultiTexCoord4dv},
    {"glMultiTexCoord4dvARB", (const void *)glMultiTexCoord4dvARB},
    {"glMultiTexCoord4f", (const void *)glMultiTexCoord4f},
    {"glMultiTexCoord4fARB", (const void *)glMultiTexCoord4fARB},
    {"glMultiTexCoord4fv", (const void *)glMultiTexCoord4fv},
    {"glMultiTexCoord4fvARB", (const void *)glMultiTexCoord4fvARB},
    {"glMultiTexCoord4i", (const void *)glMultiTexCoord4i},
    {"glMultiTexCoord4iARB", (const void *)glMultiTexCoord4iARB},
    {"glMultiTexCoord4iv", (const void *)glMultiTexCoord4iv},
    {"glMultiTexCoord4ivARB", (const void *)glMultiTexCoord4ivARB},
    {"glMultiTexCoord4s", (const void *)glMultiTexCoord4s},
    {"glMultiTexCoord4sARB", (const void *)glMultiTexCoord4sARB},
    {"glMultiTexCoord4sv", (const void *)glMultiTexCoord4sv},
    {"glMultiTexCoord4svARB", (const void *)glMultiTexCoord4svARB},
    {"glNewList", (const void *)glNewList},
    {"glNormal3b", (const void *)glNormal3b},
    {"glNormal3bv", (const void *)glNormal3bv},
    {"glNormal3d", (const void *)glNormal3d},
    {"glNormal3dv", (const void *)glNormal3dv},
    {"glNormal3f", (const void *)glNormal3f},
    {"glNormal3fv", (const void *)glNormal3fv},
    {"glNormal3i", (const void *)glNormal3i},
    {"glNormal3iv", (const void *)glNormal3iv},
    {"glNormal3s", (const void *)glNormal3s},
    {"glNormal3sv", (const void *)glNormal3sv},
    {"glNormalPointer", (const void *)glNormalPointer},
    {"glObjectLabel", (const void *)glObjectLabel},
    {"glOrtho", (const void *)glOrtho},
    {"glPassThrough", (const void *)glPassThrough},
    {"glPixelMapfv", (const void *)glPixelMapfv},
    {"glPixelMapuiv", (const void *)glPixelMapuiv},
    {"glPixelMapusv", (const void *)glPixelMapusv},
    {"glPixelStoref", (const void *)glPixelStoref},
    {"glPixelStorei", (const void *)glPixelStorei},
    {"glPixelTransferf", (const void *)glPixelTransferf},
    {"glPixelTransferi", (const void *)glPixelTransferi},
    {"glPixelZoom", (const void *)glPixelZoom},
    {"glPointParameterf", (const void *)glPointParameterf},
    {"glPointParameterfARB", (const void *)glPointParameterfARB},
    {"glPointParameterfEXT", (const void *)glPointParameterfEXT},
    {"glPointParameterfv", (const void *)glPointParameterfv},
    {"glPointParameterfvARB", (const void *)glPointParameterfvARB},
    {"glPointParameterfvEXT", (const void *)glPointParameterfvEXT},
    {"glPointParameteri", (const void *)glPointParameteri},
    {"glPointParameteriv", (const void *)glPointParameteriv},
    {"glPointSize", (const void *)glPointSize},
    {"glPolygonMode", (const void *)glPolygonMode},
    {"glPolygonOffset", (const void *)glPolygonOffset},
    {"glPolygonStipple", (const void *)glPolygonStipple},
    {"glPopAttrib", (const void *)glPopAttrib},
    {"glPopClientAttrib", (const void *)glPopClientAttrib},
    {"glPopDebugGroup", (const void *)glPopDebugGroup},
    {"glPopMatrix", (const void *)glPopMatrix},
    {"glPopName", (const void *)glPopName},
    {"glPrioritizeTextures", (const void *)glPrioritizeTextures},
    {"glPushAttrib", (const void *)glPushAttrib},
    {"glPushClientAttrib", (const void *)glPushClientAttrib},
    {"glPushDebugGroup", (const void *)glPushDebugGroup},
    {"glPushMatrix", (const void *)glPushMatrix},
    {"glPushName", (const void *)glPushName},
    {"glQueryCounter", (const void *)glQueryCounter},
    {"glRasterPos2d", (const void *)glRasterPos2d},
    {"glRasterPos2dv", (const void *)glRasterPos2dv},
    {"glRasterPos2f", (const void *)glRasterPos2f},
    {"glRasterPos2fv", (const void *)glRasterPos2fv},
    {"glRasterPos2i", (const void *)glRasterPos2i},
    {"glRasterPos2iv", (const void *)glRasterPos2iv},
    {"glRasterPos2s", (const void *)glRasterPos2s},
    {"glRasterPos2sv", (const void *)glRasterPos2sv},
    {"glRasterPos3d", (const void *)glRasterPos3d},
    {"glRasterPos3dv", (const void *)glRasterPos3dv},
    {"glRasterPos3f", (const void *)glRasterPos3f},
    {"glRasterPos3fv", (const void *)glRasterPos3fv},
    {"glRasterPos3i", (const void *)glRasterPos3i},
    {"glRasterPos3iv", (const void *)glRasterPos3iv},
    {"glRasterPos3s", (const void *)glRasterPos3s},
    {"glRasterPos3sv", (const void *)glRasterPos3sv},
    {"glRasterPos4d", (const void *)glRasterPos4d},
    {"glRasterPos4dv", (const void *)glRasterPos4dv},
    {"glRasterPos4f", (const void *)glRasterPos4f},
    {"glRasterPos4fv", (const void *)glRasterPos4fv},
    {"glRasterPos4i", (const void *)glRasterPos4i},
    {"glRasterPos4iv", (const void *)glRasterPos4iv},
    {"glRasterPos4s", (const void *)glRasterPos4s},
    {"glRasterPos4sv", (const void *)glRasterPos4sv},
    {"glReadBuffer", (const void *)glReadBuffer},
    {"glReadPixels", (const void *)glReadPixels},
    {"glRectd", (const void *)glRectd},
    {"glRectdv", (const void *)glRectdv},
    {"glRectf", (const void *)glRectf},
    {"glRectfv", (const void *)glRectfv},
    {"glRecti", (const void *)glRecti},
    {"glRectiv", (const void *)glRectiv},
    {"glRects", (const void *)glRects},
    {"glRectsv", (const void *)glRectsv},
    {"glReleaseShaderCompiler", (const void *)glReleaseShaderCompiler},
    {"glRenderMode", (const void *)glRenderMode},
    {"glRenderbufferStorage", (const void *)glRenderbufferStorage},
    {"glRenderbufferStorageMultisample", (const void *)glRenderbufferStorageMultisample},
    {"glRotated", (const void *)glRotated},
    {"glRotatef", (const void *)glRotatef},
    {"glScaled", (const void *)glScaled},
    {"glScalef", (const void *)glScalef},
    {"glScissor", (const void *)glScissor},
    {"glSelectBuffer", (const void *)glSelectBuffer},
    {"glShadeModel", (const void *)glShadeModel},
    {"glShaderSource", (const void *)glShaderSource},
    {"glStencilFunc", (const void *)glStencilFunc},
    {"glStencilFuncSeparate", (const void *)glStencilFuncSeparate},
    {"glStencilMask", (const void *)glStencilMask},
    {"glStencilMaskSeparate", (const void *)glStencilMaskSeparate},
    {"glStencilOp", (const void *)glStencilOp},
    {"glStencilOpSeparate", (const void *)glStencilOpSeparate},
    {"glTexCoord1d", (const void *)glTexCoord1d},
    {"glTexCoord1dv", (const void *)glTexCoord1dv},
    {"glTexCoord1f", (const void *)glTexCoord1f},
    {"glTexCoord1fv", (const void *)glTexCoord1fv},
    {"glTexCoord1i", (const void *)glTexCoord1i},
    {"glTexCoord1iv", (const void *)glTexCoord1iv},
    {"glTexCoord1s", (const void *)glTexCoord1s},
    {"glTexCoord1sv", (const void *)glTexCoord1sv},
    {"glTexCoord2d", (const void *)glTexCoord2d},
    {"glTexCoord2dv", (const void *)glTexCoord2dv},
    {"glTexCoord2f", (const void *)glTexCoord2f},
    {"glTexCoord2fv", (const void *)glTexCoord2fv},
    {"glTexCoord2i", (const void *)glTexCoord2i},
    {"glTexCoord2iv", (const void *)glTexCoord2iv},
    {"glTexCoord2s", (const void *)glTexCoord2s},
    {"glTexCoord2sv", (const void *)glTexCoord2sv},
    {"glTexCoord3d", (const void *)glTexCoord3d},
    {"glTexCoord3dv", (const void *)glTexCoord3dv},
    {"glTexCoord3f", (const void *)glTexCoord3f},
    {"glTexCoord3fv", (const void *)glTexCoord3fv},
    {"glTexCoord3i", (const void *)glTexCoord3i},
    {"glTexCoord3iv", (const void *)glTexCoord3iv},
    {"glTexCoord3s", (const void *)glTexCoord3s},
    {"glTexCoord3sv", (const void *)glTexCoord3sv},
    {"glTexCoord4d", (const void *)glTexCoord4d},
    {"glTexCoord4dv", (const void *)glTexCoord4dv},
    {"glTexCoord4f", (const void *)glTexCoord4f},
    {"glTexCoord4fv", (const void *)glTexCoord4fv},
    {"glTexCoord4i", (const void *)glTexCoord4i},
    {"glTexCoord4iv", (const void *)glTexCoord4iv},
    {"glTexCoord4s", (const void *)glTexCoord4s},
    {"glTexCoord4sv", (const void *)glTexCoord4sv},
    {"glTexCoordPointer", (const void *)glTexCoordPointer},
    {"glTexEnvf", (const void *)glTexEnvf},
    {"glTexEnvfv", (const void *)glTexEnvfv},
    {"glTexEnvi", (const void *)glTexEnvi},
    {"glTexEnviv", (const void *)glTexEnviv},
    {"glTexGend", (const void *)glTexGend},
    {"glTexGendv", (const void *)glTexGendv},
    {"glTexGenf", (const void *)glTexGenf},
    {"glTexGenfv", (const void *)glTexGenfv},
    {"glTexGeni", (const void *)glTexGeni},
    {"glTexGeniv", (const void *)glTexGeniv},
    {"glTexImage1D", (const void *)glTexImage1D},
    {"glTexImage2D", (const void *)glTexImage2D},
    {"glTexImage2DMultisample", (const void *)glTexImage2DMultisample},
    {"glTexImage3D", (const void *)glTexImage3D},
    {"glTexParameterf", (const void *)glTexParameterf},
    {"glTexParameterfv", (const void *)glTexParameterfv},
    {"glTexParameteri", (const void *)glTexParameteri},
    {"glTexParameteriv", (const void *)glTexParameteriv},
    {"glTexStorage2D", (const void *)glTexStorage2D},
    {"glTexStorage3D", (const void *)glTexStorage3D},
    {"glTexSubImage1D", (const void *)glTexSubImage1D},
    {"glTexSubImage2D", (const void *)glTexSubImage2D},
    {"glTexSubImage3D", (const void *)glTexSubImage3D},
    {"glTranslated", (const void *)glTranslated},
    {"glTranslatef", (const void *)glTranslatef},
    {"glUniform1f", (const void *)glUniform1f},
    {"glUniform1fv", (const void *)glUniform1fv},
    {"glUniform1i", (const void *)glUniform1i},
    {"glUniform1iv", (const void *)glUniform1iv},
    {"glUniform1ui", (const void *)glUniform1ui},
    {"glUniform1uiv", (const void *)glUniform1uiv},
    {"glUniform2f", (const void *)glUniform2f},
    {"glUniform2fv", (const void *)glUniform2fv},
    {"glUniform2i", (const void *)glUniform2i},
    {"glUniform2iv", (const void *)glUniform2iv},
    {"glUniform2ui", (const void *)glUniform2ui},
    {"glUniform2uiv", (const void *)glUniform2uiv},
    {"glUniform3f", (const void *)glUniform3f},
    {"glUniform3fv", (const void *)glUniform3fv},
    {"glUniform3i", (const void *)glUniform3i},
    {"glUniform3iv", (const void *)glUniform3iv},
    {"glUniform3ui", (const void *)glUniform3ui},
    {"glUniform3uiv", (const void *)glUniform3uiv},
    {"glUniform4f", (const void *)glUniform4f},
    {"glUniform4fv", (const void *)glUniform4fv},
    {"glUniform4i", (const void *)glUniform4i},
    {"glUniform4iv", (const void *)glUniform4iv},
    {"glUniform4ui", (const void *)glUniform4ui},
    {"glUniform4uiv", (const void *)glUniform4uiv},
    {"glUniformBlockBinding", (const void *)glUniformBlockBinding},
    {"glUniformMatrix2fv", (const void *)glUniformMatrix2fv},
    {"glUniformMatrix2x3fv", (const void *)glUniformMatrix2x3fv},
    {"glUniformMatrix2x4fv", (const void *)glUniformMatrix2x4fv},
    {"glUniformMatrix3fv", (const void *)glUniformMatrix3fv},
    {"glUniformMatrix3x2fv", (const void *)glUniformMatrix3x2fv},
    {"glUniformMatrix3x4fv", (const void *)glUniformMatrix3x4fv},
    {"glUniformMatrix4fv", (const void *)glUniformMatrix4fv},
    {"glUniformMatrix4x2fv", (const void *)glUniformMatrix4x2fv},
    {"glUniformMatrix4x3fv", (const void *)glUniformMatrix4x3fv},
    {"glUnmapBuffer", (const void *)glUnmapBuffer},
    {"glUseProgram", (const void *)glUseProgram},
    {"glValidateProgram", (const void *)glValidateProgram},
    {"glVertex2d", (const void *)glVertex2d},
    {"glVertex2dv", (const void *)glVertex2dv},
    {"glVertex2f", (const void *)glVertex2f},
    {"glVertex2fv", (const void *)glVertex2fv},
    {"glVertex2i", (const void *)glVertex2i},
    {"glVertex2iv", (const void *)glVertex2iv},
    {"glVertex2s", (const void *)glVertex2s},
    {"glVertex2sv", (const void *)glVertex2sv},
    {"glVertex3d", (const void *)glVertex3d},
    {"glVertex3dv", (const void *)glVertex3dv},
    {"glVertex3f", (const void *)glVertex3f},
    {"glVertex3fv", (const void *)glVertex3fv},
    {"glVertex3i", (const void *)glVertex3i},
    {"glVertex3iv", (const void *)glVertex3iv},
    {"glVertex3s", (const void *)glVertex3s},
    {"glVertex3sv", (const void *)glVertex3sv},
    {"glVertex4d", (const void *)glVertex4d},
    {"glVertex4dv", (const void *)glVertex4dv},
    {"glVertex4f", (const void *)glVertex4f},
    {"glVertex4fv", (const void *)glVertex4fv},
    {"glVertex4i", (const void *)glVertex4i},
    {"glVertex4iv", (const void *)glVertex4iv},
    {"glVertex4s", (const void *)glVertex4s},
    {"glVertex4sv", (const void *)glVertex4sv},
    {"glVertexAttrib1f", (const void *)glVertexAttrib1f},
    {"glVertexAttrib1fv", (const void *)glVertexAttrib1fv},
    {"glVertexAttrib2f", (const void *)glVertexAttrib2f},
    {"glVertexAttrib2fv", (const void *)glVertexAttrib2fv},
    {"glVertexAttrib3f", (const void *)glVertexAttrib3f},
    {"glVertexAttrib3fv", (const void *)glVertexAttrib3fv},
    {"glVertexAttrib4Nub", (const void *)glVertexAttrib4Nub},
    {"glVertexAttrib4f", (const void *)glVertexAttrib4f},
    {"glVertexAttrib4fv", (const void *)glVertexAttrib4fv},
    {"glVertexAttribDivisor", (const void *)glVertexAttribDivisor},
    {"glVertexAttribI4i", (const void *)glVertexAttribI4i},
    {"glVertexAttribI4iv", (const void *)glVertexAttribI4iv},
    {"glVertexAttribI4ui", (const void *)glVertexAttribI4ui},
    {"glVertexAttribI4uiv", (const void *)glVertexAttribI4uiv},
    {"glVertexAttribIPointer", (const void *)glVertexAttribIPointer},
    {"glVertexAttribPointer", (const void *)glVertexAttribPointer},
    {"glVertexPointer", (const void *)glVertexPointer},
    {"glViewport", (const void *)glViewport},
    {"glWaitSync", (const void *)glWaitSync},
    {"wglChoosePixelFormat", (const void *)wglChoosePixelFormat},
    {"wglChoosePixelFormatARB", (const void *)wglChoosePixelFormatARB},
    {"wglCopyContext", (const void *)wglCopyContext},
    {"wglCreateContext", (const void *)wglCreateContext},
    {"wglCreateContextAttribsARB", (const void *)wglCreateContextAttribsARB},
    {"wglCreateLayerContext", (const void *)wglCreateLayerContext},
    {"wglDeleteContext", (const void *)wglDeleteContext},
    {"wglDescribeLayerPlane", (const void *)wglDescribeLayerPlane},
    {"wglDescribePixelFormat", (const void *)wglDescribePixelFormat},
    {"wglGetCurrentContext", (const void *)wglGetCurrentContext},
    {"wglGetCurrentDC", (const void *)wglGetCurrentDC},
    {"wglGetCurrentReadDCARB", (const void *)wglGetCurrentReadDCARB},
    {"wglGetExtensionsStringARB", (const void *)wglGetExtensionsStringARB},
    {"wglGetExtensionsStringEXT", (const void *)wglGetExtensionsStringEXT},
    {"wglGetLayerPaletteEntries", (const void *)wglGetLayerPaletteEntries},
    {"wglGetPixelFormat", (const void *)wglGetPixelFormat},
    {"wglGetPixelFormatAttribfvARB", (const void *)wglGetPixelFormatAttribfvARB},
    {"wglGetPixelFormatAttribivARB", (const void *)wglGetPixelFormatAttribivARB},
    {"wglGetProcAddress", (const void *)wglGetProcAddress},
    {"wglGetSwapIntervalEXT", (const void *)wglGetSwapIntervalEXT},
    {"wglMakeContextCurrentARB", (const void *)wglMakeContextCurrentARB},
    {"wglMakeCurrent", (const void *)wglMakeCurrent},
    {"wglRealizeLayerPalette", (const void *)wglRealizeLayerPalette},
    {"wglSetLayerPaletteEntries", (const void *)wglSetLayerPaletteEntries},
    {"wglSetPixelFormat", (const void *)wglSetPixelFormat},
    {"wglShareLists", (const void *)wglShareLists},
    {"wglSwapBuffers", (const void *)wglSwapBuffers},
    {"wglSwapIntervalEXT", (const void *)wglSwapIntervalEXT},
    {"wglSwapLayerBuffers", (const void *)wglSwapLayerBuffers},
    {"wglUseFontBitmapsA", (const void *)wglUseFontBitmapsA},
    {"wglUseFontBitmapsW", (const void *)wglUseFontBitmapsW},
    {"wglUseFontOutlinesA", (const void *)wglUseFontOutlinesA},
    {"wglUseFontOutlinesW", (const void *)wglUseFontOutlinesW},
};
const ULONG GlProcCount = 648;
