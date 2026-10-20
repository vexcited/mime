/*
 * matrices: the 3 by 2 affine transform of gdi+ with its six elements m11 m12 m21 m22 dx dy. Points are rows, so a
 * point goes through x' = x m11 + y m21 + dx and y' = x m12 + y m22 + dy, and prepending an operation applies it
 * to the points before the ones already in the matrix.
 */
#include "gp_private.h"

#define MATRIX_ELEMENTS 6

/* a determinant below this is a matrix that cannot be inverted, a guess at the limit windows uses. */
#define DETERMINANT_LIMIT 1.0e-6f

static GMatrix *matrix_cast(const void *matrix)
{
    return GP_CAST(GMatrix, GP_TAG_MATRIX, matrix);
}

GMatrix *gp_matrix_cast(const void *p) { return matrix_cast(p); }

/* a followed by b: the product that applies a to a point first. */
void gp_matrix_multiply(const float *a, const float *b, float *out)
{
    float r[MATRIX_ELEMENTS];
    r[0] = a[0] * b[0] + a[1] * b[2];
    r[1] = a[0] * b[1] + a[1] * b[3];
    r[2] = a[2] * b[0] + a[3] * b[2];
    r[3] = a[2] * b[1] + a[3] * b[3];
    r[4] = a[4] * b[0] + a[5] * b[2] + b[4];
    r[5] = a[4] * b[1] + a[5] * b[3] + b[5];
    memcpy(out, r, sizeof(r));
}

void gp_matrix_point(const float *m, float x, float y, float *ox, float *oy)
{
    *ox = x * m[0] + y * m[2] + m[4];
    *oy = x * m[1] + y * m[3] + m[5];
}

static GpStatus matrix_new(float m11, float m12, float m21, float m22, float dx, float dy, GpMatrix **out)
{
    GMatrix *m = gp_alloc(sizeof(*m));
    if (!m)
        return OutOfMemory;
    m->tag = GP_TAG_MATRIX;
    m->M[0] = m11;
    m->M[1] = m12;
    m->M[2] = m21;
    m->M[3] = m22;
    m->M[4] = dx;
    m->M[5] = dy;
    *out = m;
    return Ok;
}

static float determinant(const float *m)
{
    return m[0] * m[3] - m[1] * m[2];
}

/* ------------------------------------------------------------------ creation */

DECLSPEC_EXPORT GpStatus WINAPI GdipCreateMatrix(GpMatrix **matrix)
{
    if (!matrix)
        return InvalidParameter;
    return matrix_new(1, 0, 0, 1, 0, 0, matrix);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipCreateMatrix2(float m11, float m12, float m21, float m22, float dx, float dy,
                                                  GpMatrix **matrix)
{
    if (!matrix)
        return InvalidParameter;
    return matrix_new(m11, m12, m21, m22, dx, dy, matrix);
}

/* the transform that maps the rectangle to the parallelogram of three points: top left, top right, bottom left. */
DECLSPEC_EXPORT GpStatus WINAPI GdipCreateMatrix3(const GpRectF *rect, const GpPointF *points, GpMatrix **matrix)
{
    if (!rect || !points || !matrix || rect->Width == 0 || rect->Height == 0)
        return InvalidParameter;
    float m11 = (points[1].X - points[0].X) / rect->Width, m12 = (points[1].Y - points[0].Y) / rect->Width;
    float m21 = (points[2].X - points[0].X) / rect->Height, m22 = (points[2].Y - points[0].Y) / rect->Height;
    float dx = points[0].X - rect->X * m11 - rect->Y * m21, dy = points[0].Y - rect->X * m12 - rect->Y * m22;
    return matrix_new(m11, m12, m21, m22, dx, dy, matrix);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipCreateMatrix3I(const GpRect *rect, const GpPoint *points, GpMatrix **matrix)
{
    if (!rect || !points)
        return InvalidParameter;
    GpRectF r = {(float)rect->X, (float)rect->Y, (float)rect->Width, (float)rect->Height};
    GpPointF p[3];
    for (int i = 0; i < 3; i++) {
        p[i].X = (float)points[i].X;
        p[i].Y = (float)points[i].Y;
    }
    return GdipCreateMatrix3(&r, p, matrix);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipCloneMatrix(GpMatrix *matrix, GpMatrix **clone)
{
    GMatrix *m = matrix_cast(matrix);
    if (!m || !clone)
        return InvalidParameter;
    return matrix_new(m->M[0], m->M[1], m->M[2], m->M[3], m->M[4], m->M[5], clone);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipDeleteMatrix(GpMatrix *matrix)
{
    GMatrix *m = matrix_cast(matrix);
    if (!m)
        return InvalidParameter;
    m->tag = 0;
    gp_free(m);
    return Ok;
}

/* ------------------------------------------------------------------ elements */

DECLSPEC_EXPORT GpStatus WINAPI GdipSetMatrixElements(GpMatrix *matrix, float m11, float m12, float m21, float m22,
                                                      float dx, float dy)
{
    GMatrix *m = matrix_cast(matrix);
    if (!m)
        return InvalidParameter;
    m->M[0] = m11;
    m->M[1] = m12;
    m->M[2] = m21;
    m->M[3] = m22;
    m->M[4] = dx;
    m->M[5] = dy;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetMatrixElements(const GpMatrix *matrix, float *elements)
{
    GMatrix *m = matrix_cast(matrix);
    if (!m || !elements)
        return InvalidParameter;
    memcpy(elements, m->M, sizeof(m->M));
    return Ok;
}

/* ------------------------------------------------------------------ operations */

static GpStatus apply(GpMatrix *matrix, const float *op, MatrixOrder order)
{
    GMatrix *m = matrix_cast(matrix);
    if (!m || (order != MatrixOrderPrepend && order != MatrixOrderAppend))
        return InvalidParameter;
    if (order == MatrixOrderPrepend)
        gp_matrix_multiply(op, m->M, m->M);
    else
        gp_matrix_multiply(m->M, op, m->M);
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipMultiplyMatrix(GpMatrix *matrix, GpMatrix *other, GpMatrixOrder order)
{
    GMatrix *o = matrix_cast(other);
    if (!o)
        return InvalidParameter;
    float copy[MATRIX_ELEMENTS];
    memcpy(copy, o->M, sizeof(copy));
    return apply(matrix, copy, order);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipTranslateMatrix(GpMatrix *matrix, float dx, float dy, MatrixOrder order)
{
    float op[MATRIX_ELEMENTS] = {1, 0, 0, 1, dx, dy};
    return apply(matrix, op, order);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipScaleMatrix(GpMatrix *matrix, float sx, float sy, MatrixOrder order)
{
    float op[MATRIX_ELEMENTS] = {sx, 0, 0, sy, 0, 0};
    return apply(matrix, op, order);
}

/* the integer part of a number below 2^62 in size, rounded toward minus infinity. */
static double floor_double(double v)
{
    double t = (double)(long long)v;
    return t > v ? t - 1 : t;
}

static float floor_float(float v)
{
    float t = (float)(long long)v;
    return t > v ? t - 1 : t;
}

static float fabs_float(float v)
{
    return v < 0 ? -v : v;
}

/*
 * the sine and cosine of an angle in degrees: the angle is brought into the first quadrant, where the series
 * converge in a dozen terms, and the quadrant turns the pair. Right angles come out whole.
 */
static void sine_cosine(double degrees, double *s, double *c)
{
    double turns = degrees - 360.0 * floor_double(degrees / 360.0);
    int quadrant = (int)(turns / 90.0);
    double rest = turns - 90.0 * quadrant;
    double sin_r = 0, cos_r = 1;
    if (rest != 0) {
        double r = rest * 3.14159265358979323846 / 180.0, r2 = r * r, term = r;
        sin_r = r;
        for (int n = 1; n < 12; n++) {
            term *= -r2 / ((2 * n) * (2 * n + 1));
            sin_r += term;
        }
        term = 1;
        cos_r = 1;
        for (int n = 1; n < 12; n++) {
            term *= -r2 / ((2 * n - 1) * (2 * n));
            cos_r += term;
        }
    }
    switch (quadrant & 3) {
    case 0:
        *s = sin_r;
        *c = cos_r;
        break;
    case 1:
        *s = cos_r;
        *c = -sin_r;
        break;
    case 2:
        *s = -sin_r;
        *c = -cos_r;
        break;
    default:
        *s = -cos_r;
        *c = sin_r;
        break;
    }
}

DECLSPEC_EXPORT GpStatus WINAPI GdipRotateMatrix(GpMatrix *matrix, float angle, MatrixOrder order)
{
    double s, c;
    sine_cosine(angle, &s, &c);
    float op[MATRIX_ELEMENTS] = {(float)c, (float)s, (float)-s, (float)c, 0, 0};
    return apply(matrix, op, order);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipShearMatrix(GpMatrix *matrix, float shear_x, float shear_y, MatrixOrder order)
{
    float op[MATRIX_ELEMENTS] = {1, shear_y, shear_x, 1, 0, 0};
    return apply(matrix, op, order);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipInvertMatrix(GpMatrix *matrix)
{
    GMatrix *m = matrix_cast(matrix);
    if (!m)
        return InvalidParameter;
    float det = determinant(m->M);
    if (fabs_float(det) < DETERMINANT_LIMIT)
        return InvalidParameter;
    float *e = m->M;
    float r[MATRIX_ELEMENTS];
    r[0] = e[3] / det;
    r[1] = -e[1] / det;
    r[2] = -e[2] / det;
    r[3] = e[0] / det;
    r[4] = (e[2] * e[5] - e[3] * e[4]) / det;
    r[5] = (e[1] * e[4] - e[0] * e[5]) / det;
    memcpy(e, r, sizeof(r));
    return Ok;
}

/* ------------------------------------------------------------------ points */

static GpStatus transform_points(GpMatrix *matrix, GpPointF *points, INT count, BOOL translate)
{
    GMatrix *m = matrix_cast(matrix);
    if (!m || !points || count <= 0)
        return InvalidParameter;
    for (INT i = 0; i < count; i++) {
        float x = points[i].X, y = points[i].Y;
        points[i].X = x * m->M[0] + y * m->M[2] + (translate ? m->M[4] : 0);
        points[i].Y = x * m->M[1] + y * m->M[3] + (translate ? m->M[5] : 0);
    }
    return Ok;
}

static INT round_float(float v)
{
    return (INT)floor_float(v + 0.5f);
}

static GpStatus transform_points_int(GpMatrix *matrix, GpPoint *points, INT count, BOOL translate)
{
    GMatrix *m = matrix_cast(matrix);
    if (!m || !points || count <= 0)
        return InvalidParameter;
    for (INT i = 0; i < count; i++) {
        float x = (float)points[i].X, y = (float)points[i].Y;
        points[i].X = round_float(x * m->M[0] + y * m->M[2] + (translate ? m->M[4] : 0));
        points[i].Y = round_float(x * m->M[1] + y * m->M[3] + (translate ? m->M[5] : 0));
    }
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipTransformMatrixPoints(GpMatrix *matrix, GpPointF *points, INT count)
{
    return transform_points(matrix, points, count, TRUE);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipTransformMatrixPointsI(GpMatrix *matrix, GpPoint *points, INT count)
{
    return transform_points_int(matrix, points, count, TRUE);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipVectorTransformMatrixPoints(GpMatrix *matrix, GpPointF *points, INT count)
{
    return transform_points(matrix, points, count, FALSE);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipVectorTransformMatrixPointsI(GpMatrix *matrix, GpPoint *points, INT count)
{
    return transform_points_int(matrix, points, count, FALSE);
}

/* ------------------------------------------------------------------ tests */

DECLSPEC_EXPORT GpStatus WINAPI GdipIsMatrixInvertible(const GpMatrix *matrix, BOOL *result)
{
    GMatrix *m = matrix_cast(matrix);
    if (!m || !result)
        return InvalidParameter;
    *result = fabs_float(determinant(m->M)) >= DETERMINANT_LIMIT;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipIsMatrixIdentity(const GpMatrix *matrix, BOOL *result)
{
    GMatrix *m = matrix_cast(matrix);
    if (!m || !result)
        return InvalidParameter;
    *result = m->M[0] == 1 && m->M[1] == 0 && m->M[2] == 0 && m->M[3] == 1 && m->M[4] == 0 && m->M[5] == 0;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipIsMatrixEqual(const GpMatrix *matrix, const GpMatrix *other, BOOL *result)
{
    GMatrix *a = matrix_cast(matrix), *b = matrix_cast(other);
    if (!a || !b || !result)
        return InvalidParameter;
    *result = !memcmp(a->M, b->M, sizeof(a->M)) || (a->M[0] == b->M[0] && a->M[1] == b->M[1] && a->M[2] == b->M[2] &&
                                                    a->M[3] == b->M[3] && a->M[4] == b->M[4] && a->M[5] == b->M[5]);
    return Ok;
}

/* ------------------------------------------------------------------ helpers of the other modules */

BOOL gp_matrix_invertible(const MimeVecMatrix *m)
{
    return fabs_float(m->m11 * m->m22 - m->m12 * m->m21) >= DETERMINANT_LIMIT;
}

void gp_matrix_apply(MimeVecMatrix *m, const MimeVecMatrix *op, MatrixOrder order)
{
    if (order == MatrixOrderPrepend)
        gp_matrix_multiply(&op->m11, &m->m11, &m->m11);
    else
        gp_matrix_multiply(&m->m11, &op->m11, &m->m11);
}

BOOL gp_matrix_inverse(MimeVecMatrix *out, const MimeVecMatrix *m)
{
    float det = m->m11 * m->m22 - m->m12 * m->m21;
    if (fabs_float(det) < DETERMINANT_LIMIT)
        return FALSE;
    MimeVecMatrix r;
    r.m11 = m->m22 / det;
    r.m12 = -m->m12 / det;
    r.m21 = -m->m21 / det;
    r.m22 = m->m11 / det;
    r.dx = (m->m21 * m->dy - m->m22 * m->dx) / det;
    r.dy = (m->m12 * m->dx - m->m11 * m->dy) / det;
    *out = r;
    return TRUE;
}
