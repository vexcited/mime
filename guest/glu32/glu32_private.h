/*
 * glu32 internals: the types of opengl, the entry points of opengl32 that the library draws with and the helpers
 * shared by its parts.
 */
#pragma once
#include <stddef.h>
#include <mime/win32/winbase.h>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef double GLdouble;
typedef void GLvoid;

#define GLUAPI DECLSPEC_EXPORT
#define GLAPIENTRY WINAPI

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_QUADS 0x0007
#define GL_QUAD_STRIP 0x0008
#define GL_POLYGON 0x0009
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_STACK_OVERFLOW 0x0503
#define GL_STACK_UNDERFLOW 0x0504
#define GL_OUT_OF_MEMORY 0x0505
#define GL_TABLE_TOO_LARGE 0x8031
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_BITMAP 0x1A00
#define GL_COLOR_INDEX 0x1900
#define GL_STENCIL_INDEX 0x1901
#define GL_DEPTH_COMPONENT 0x1902
#define GL_RED 0x1903
#define GL_GREEN 0x1904
#define GL_BLUE 0x1905
#define GL_ALPHA 0x1906
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_BGR_EXT 0x80E0
#define GL_BGRA_EXT 0x80E1
#define GL_TEXTURE_1D 0x0DE0
#define GL_TEXTURE_2D 0x0DE1
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_VIEWPORT 0x0BA2
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_UNPACK_SWAP_BYTES 0x0CF0
#define GL_UNPACK_LSB_FIRST 0x0CF1
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_SWAP_BYTES 0x0D00
#define GL_PACK_LSB_FIRST 0x0D01
#define GL_PACK_ROW_LENGTH 0x0D02
#define GL_PACK_SKIP_ROWS 0x0D03
#define GL_PACK_SKIP_PIXELS 0x0D04
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_MAP1_VERTEX_3 0x0D97
#define GL_MAP1_VERTEX_4 0x0D98
#define GL_MAP2_VERTEX_3 0x0DB7
#define GL_MAP2_VERTEX_4 0x0DB8

#define GLU_INVALID_ENUM 100900
#define GLU_INVALID_VALUE 100901
#define GLU_OUT_OF_MEMORY 100902
#define GLU_INCOMPATIBLE_GL_VERSION 100903
#define GLU_INVALID_OPERATION 100904
#define GLU_VERSION 100800
#define GLU_EXTENSIONS 100801
#define GLU_ERROR 100103

/* the functions of opengl32 the library calls, found by name when first needed. */
typedef struct GL_API {
    void(GLAPIENTRY *Begin)(GLenum mode);
    void(GLAPIENTRY *End)(void);
    void(GLAPIENTRY *Vertex2d)(GLdouble x, GLdouble y);
    void(GLAPIENTRY *Vertex3d)(GLdouble x, GLdouble y, GLdouble z);
    void(GLAPIENTRY *Normal3d)(GLdouble x, GLdouble y, GLdouble z);
    void(GLAPIENTRY *TexCoord1d)(GLdouble s);
    void(GLAPIENTRY *TexCoord2d)(GLdouble s, GLdouble t);
    void(GLAPIENTRY *GetIntegerv)(GLenum name, GLint *values);
    void(GLAPIENTRY *GetDoublev)(GLenum name, GLdouble *values);
    GLenum(GLAPIENTRY *GetError)(void);
    void(GLAPIENTRY *PixelStorei)(GLenum name, GLint value);
    void(GLAPIENTRY *MultMatrixd)(const GLdouble *matrix);
    void(GLAPIENTRY *Ortho)(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_plane,
                            GLdouble far_plane);
    void(GLAPIENTRY *Translated)(GLdouble x, GLdouble y, GLdouble z);
    void(GLAPIENTRY *Scaled)(GLdouble x, GLdouble y, GLdouble z);
    void(GLAPIENTRY *TexImage1D)(GLenum target, GLint level, GLint internal_format, GLsizei width, GLint border,
                                 GLenum format, GLenum type, const GLvoid *pixels);
    void(GLAPIENTRY *TexImage2D)(GLenum target, GLint level, GLint internal_format, GLsizei width, GLsizei height,
                                 GLint border, GLenum format, GLenum type, const GLvoid *pixels);
} GL_API;

const GL_API *GluGl(void);

/* math.c */
double GluSin(double radians);
double GluCos(double radians);
double GluSqrt(double value);

/* mipmap.c */
int GluPixelSize(GLenum format, GLenum type, int *components);
