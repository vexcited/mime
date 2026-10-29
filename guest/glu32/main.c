/*
 * glu32.dll: the utility library of opengl. it draws with opengl32 and keeps no state of its own besides the objects
 * the programs make. this file has the loading of opengl32, the error and version strings, and the projection
 * helpers.
 */
#include "glu32_private.h"

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, PVOID reserved)
{
    return TRUE;
}

/* ------------------------------------------------------------------ opengl32 */

static GL_API g_gl;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static void nothing(void) {}

static BOOL WINAPI load_gl(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    HMODULE module = LoadLibraryW(L"opengl32.dll");
    static const struct {
        const char *Name;
        SIZE_T Offset;
    } names[] = {
        {"glBegin", offsetof(GL_API, Begin)},
        {"glEnd", offsetof(GL_API, End)},
        {"glVertex2d", offsetof(GL_API, Vertex2d)},
        {"glVertex3d", offsetof(GL_API, Vertex3d)},
        {"glNormal3d", offsetof(GL_API, Normal3d)},
        {"glTexCoord1d", offsetof(GL_API, TexCoord1d)},
        {"glTexCoord2d", offsetof(GL_API, TexCoord2d)},
        {"glGetIntegerv", offsetof(GL_API, GetIntegerv)},
        {"glGetDoublev", offsetof(GL_API, GetDoublev)},
        {"glGetError", offsetof(GL_API, GetError)},
        {"glPixelStorei", offsetof(GL_API, PixelStorei)},
        {"glMultMatrixd", offsetof(GL_API, MultMatrixd)},
        {"glOrtho", offsetof(GL_API, Ortho)},
        {"glTranslated", offsetof(GL_API, Translated)},
        {"glScaled", offsetof(GL_API, Scaled)},
        {"glTexImage1D", offsetof(GL_API, TexImage1D)},
        {"glTexImage2D", offsetof(GL_API, TexImage2D)},
    };
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(names); i++) {
        void *proc = module ? (void *)GetProcAddress(module, names[i].Name) : nullptr;
        *(void **)((BYTE *)&g_gl + names[i].Offset) = proc ? proc : (void *)nothing;
    }
    return TRUE;
}

/* a function that opengl32 does not have does nothing. */
const GL_API *GluGl(void)
{
    InitOnceExecuteOnce(&g_once, load_gl, nullptr, nullptr);
    return &g_gl;
}

/* ------------------------------------------------------------------ strings */

static const char *const g_nurbs_errors[] = {
    "spline order un-supported",
    "too few knots",
    "valid knot range is empty",
    "decreasing knot sequence knot",
    "knot multiplicity greater than order of spline",
    "endcurve() must follow bgncurve()",
    "bgncurve() must precede endcurve()",
    "missing or extra geometric data",
    "can't draw pwlcurves",
    "missing bgncurve()",
    "missing bgnsurface()",
    "endtrim() must precede endsurface()",
    "bgnsurface() must precede endsurface()",
    "curve of improper type passed as trim curve",
    "bgnsurface() must precede bgntrim()",
    "endtrim() must follow bgntrim()",
    "bgntrim() must precede endtrim()",
    "invalid or missing trim curve",
    "bgntrim() must precede pwlcurve()",
    "pwlcurve referenced twice",
    "pwlcurve and nurbscurve mixed",
    "improper usage of trim data type",
    "nurbscurve referenced twice",
    "nurbscurve and pwlcurve mixed",
    "nurbssurface referenced twice",
    "invalid property",
    "endsurface() must follow bgnsurface()",
    "misoriented trim curves",
    "intersecting trim curves",
    "UNUSED",
    "unconnected trim curves",
    "unknown knot error",
    "negative vertex count encountered",
    "negative byte-stride encountered",
    "unknown type descriptor",
    "null control point reference",
    "duplicate point on pwlcurve",
};

static const char *const g_tess_errors[] = {
    "missing gluBeginPolygon",
    "missing gluBeginContour",
    "missing gluEndPolygon",
    "missing gluEndContour",
    "misoriented or self-intersecting loops",
    "coincident vertices",
    "colinear vertices",
    "intersecting edges",
    "not coplanar contours",
};

GLUAPI const GLubyte *GLAPIENTRY gluErrorString(GLenum error)
{
    switch (error) {
    case GL_NO_ERROR:
        return (const GLubyte *)"no error";
    case GL_INVALID_ENUM:
    case GLU_INVALID_ENUM:
        return (const GLubyte *)"invalid enumerant";
    case GL_INVALID_VALUE:
    case GLU_INVALID_VALUE:
        return (const GLubyte *)"invalid value";
    case GL_INVALID_OPERATION:
    case GLU_INVALID_OPERATION:
        return (const GLubyte *)"invalid operation";
    case GL_STACK_OVERFLOW:
        return (const GLubyte *)"stack overflow";
    case GL_STACK_UNDERFLOW:
        return (const GLubyte *)"stack underflow";
    case GL_OUT_OF_MEMORY:
    case GLU_OUT_OF_MEMORY:
        return (const GLubyte *)"out of memory";
    case GL_TABLE_TOO_LARGE:
        return (const GLubyte *)"table too large";
    case GLU_INCOMPATIBLE_GL_VERSION:
        return (const GLubyte *)"incompatible gl version";
    }
    if (error >= 100251 && error < 100251 + RTL_NUMBER_OF(g_nurbs_errors))
        return (const GLubyte *)g_nurbs_errors[error - 100251];
    if (error >= 100151 && error < 100151 + RTL_NUMBER_OF(g_tess_errors))
        return (const GLubyte *)g_tess_errors[error - 100151];
    return nullptr;
}

/* the same text in wide characters, one buffer per thread of the caller's would be exact; this one is shared. */
GLUAPI const WCHAR *GLAPIENTRY gluErrorUnicodeStringEXT(GLenum error)
{
    static WCHAR text[64];
    const GLubyte *narrow = gluErrorString(error);
    if (!narrow)
        return nullptr;
    int i = 0;
    for (; narrow[i] && i < 63; i++)
        text[i] = narrow[i];
    text[i] = 0;
    return text;
}

GLUAPI const GLubyte *GLAPIENTRY gluGetString(GLenum name)
{
    switch (name) {
    case GLU_VERSION:
        return (const GLubyte *)"1.2.2.0 Microsoft Corporation";
    case GLU_EXTENSIONS:
        return (const GLubyte *)"GL_EXT_bgra";
    }
    return nullptr;
}

/* ------------------------------------------------------------------ projections */

GLUAPI void GLAPIENTRY gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top)
{
    GluGl()->Ortho(left, right, bottom, top, -1.0, 1.0);
}

GLUAPI void GLAPIENTRY gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble near_plane, GLdouble far_plane)
{
    double radians = fovy / 2 * 3.14159265358979323846 / 180;
    double depth = far_plane - near_plane, sine = GluSin(radians);
    if (depth == 0 || sine == 0 || aspect == 0)
        return;
    double cotangent = GluCos(radians) / sine;
    GLdouble matrix[16] = {};
    matrix[0] = cotangent / aspect;
    matrix[5] = cotangent;
    matrix[10] = -(far_plane + near_plane) / depth;
    matrix[11] = -1;
    matrix[14] = -2 * near_plane * far_plane / depth;
    GluGl()->MultMatrixd(matrix);
}

GLUAPI void GLAPIENTRY gluLookAt(GLdouble eye_x, GLdouble eye_y, GLdouble eye_z, GLdouble center_x,
                                 GLdouble center_y, GLdouble center_z, GLdouble up_x, GLdouble up_y, GLdouble up_z)
{
    double forward[3] = {center_x - eye_x, center_y - eye_y, center_z - eye_z};
    double length = GluSqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (length == 0)
        return;
    for (int i = 0; i < 3; i++)
        forward[i] /= length;
    /* side = forward x up, then up = side x forward. */
    double up[3] = {up_x, up_y, up_z};
    double side[3] = {forward[1] * up[2] - forward[2] * up[1], forward[2] * up[0] - forward[0] * up[2],
                      forward[0] * up[1] - forward[1] * up[0]};
    length = GluSqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
    if (length == 0)
        return;
    for (int i = 0; i < 3; i++)
        side[i] /= length;
    up[0] = side[1] * forward[2] - side[2] * forward[1];
    up[1] = side[2] * forward[0] - side[0] * forward[2];
    up[2] = side[0] * forward[1] - side[1] * forward[0];
    GLdouble matrix[16] = {side[0], up[0], -forward[0], 0, side[1], up[1], -forward[1], 0,
                           side[2], up[2], -forward[2], 0, 0,       0,     0,           1};
    const GL_API *gl = GluGl();
    gl->MultMatrixd(matrix);
    gl->Translated(-eye_x, -eye_y, -eye_z);
}

/* a pick region of the viewport is made the whole of it. */
GLUAPI void GLAPIENTRY gluPickMatrix(GLdouble x, GLdouble y, GLdouble width, GLdouble height, GLint viewport[4])
{
    if (width <= 0 || height <= 0)
        return;
    const GL_API *gl = GluGl();
    gl->Translated((viewport[2] - 2 * (x - viewport[0])) / width, (viewport[3] - 2 * (y - viewport[1])) / height, 0);
    gl->Scaled(viewport[2] / width, viewport[3] / height, 1.0);
}

static void multiply_vector(const GLdouble m[16], const double in[4], double out[4])
{
    for (int i = 0; i < 4; i++)
        out[i] = in[0] * m[i] + in[1] * m[4 + i] + in[2] * m[8 + i] + in[3] * m[12 + i];
}

static void multiply_matrices(const GLdouble a[16], const GLdouble b[16], GLdouble out[16])
{
    for (int row = 0; row < 4; row++)
        for (int column = 0; column < 4; column++)
            out[column * 4 + row] = a[row] * b[column * 4] + a[4 + row] * b[column * 4 + 1] +
                                    a[8 + row] * b[column * 4 + 2] + a[12 + row] * b[column * 4 + 3];
}

/* the inverse by cofactors, false for a matrix without one. */
static BOOL invert(const GLdouble m[16], GLdouble out[16])
{
    double inverse[16];
    inverse[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
                 m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inverse[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
                 m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inverse[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
                 m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inverse[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
                  m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inverse[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
                 m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inverse[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
                 m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inverse[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
                 m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inverse[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
                  m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inverse[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
                 m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inverse[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
                 m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inverse[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
                  m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inverse[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
                  m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inverse[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
                 m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inverse[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
                 m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inverse[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
                  m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inverse[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
                  m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    double determinant = m[0] * inverse[0] + m[1] * inverse[4] + m[2] * inverse[8] + m[3] * inverse[12];
    if (determinant == 0)
        return FALSE;
    for (int i = 0; i < 16; i++)
        out[i] = inverse[i] / determinant;
    return TRUE;
}

GLUAPI GLint GLAPIENTRY gluProject(GLdouble obj_x, GLdouble obj_y, GLdouble obj_z, const GLdouble model[16],
                                   const GLdouble projection[16], const GLint viewport[4], GLdouble *win_x,
                                   GLdouble *win_y, GLdouble *win_z)
{
    double in[4] = {obj_x, obj_y, obj_z, 1.0}, eye[4], clip[4];
    multiply_vector(model, in, eye);
    multiply_vector(projection, eye, clip);
    if (clip[3] == 0)
        return GL_FALSE;
    for (int i = 0; i < 3; i++)
        clip[i] = clip[i] / clip[3] * 0.5 + 0.5;
    *win_x = clip[0] * viewport[2] + viewport[0];
    *win_y = clip[1] * viewport[3] + viewport[1];
    *win_z = clip[2];
    return GL_TRUE;
}

GLUAPI GLint GLAPIENTRY gluUnProject(GLdouble win_x, GLdouble win_y, GLdouble win_z, const GLdouble model[16],
                                     const GLdouble projection[16], const GLint viewport[4], GLdouble *obj_x,
                                     GLdouble *obj_y, GLdouble *obj_z)
{
    GLdouble both[16], inverse[16];
    multiply_matrices(projection, model, both);
    if (!invert(both, inverse))
        return GL_FALSE;
    double in[4] = {(win_x - viewport[0]) / viewport[2] * 2 - 1, (win_y - viewport[1]) / viewport[3] * 2 - 1,
                    win_z * 2 - 1, 1.0},
           out[4];
    multiply_vector(inverse, in, out);
    if (out[3] == 0)
        return GL_FALSE;
    *obj_x = out[0] / out[3];
    *obj_y = out[1] / out[3];
    *obj_z = out[2] / out[3];
    return GL_TRUE;
}
