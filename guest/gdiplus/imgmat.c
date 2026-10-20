/*
 * the matrices of image drawing. Each one carries a kind that says which parts of it matter, so the samplers can skip
 * the work of the parts that do not. The tolerances are the ones in docs/gdiplus.md.
 */
#include "image_private.h"

#define SMALL (1.1920929e-7f)
#define KIND_TOLERANCE (0.00059604645f)

static float mag(float v) { return v < 0.0f ? -v : v; }

void mat_identity(ImgMat *m) { mat_set(m, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f); }

void mat_classify(ImgMat *m)
{
    float diag = mag(m->m11) > mag(m->m22) ? m->m11 : m->m22;
    float off = mag(m->m12) > mag(m->m21) ? m->m12 : m->m21;
    float big = mag(diag) <= mag(off) ? off : diag;
    float limit = mag(big) * KIND_TOLERANCE;
    UINT kind;
    if (limit <= mag(m->m12) || limit <= mag(m->m21)) {
        /* the off diagonal part counts. Equal diagonals and opposite off diagonals make a turn with a uniform scale. */
        kind = MAT_SHIFT | MAT_SCALE | MAT_ROTATE | MAT_SHEAR;
        if (limit > mag(m->m11 - m->m22) && limit > mag(m->m21 + m->m12)) {
            kind = MAT_SHIFT | MAT_SCALE | MAT_ROTATE;
            if (mag((m->m12 * m->m12 + m->m11 * m->m11) - 1.0f) < KIND_TOLERANCE)
                kind = MAT_SHIFT | MAT_ROTATE;
        }
    } else {
        kind = MAT_SHIFT | MAT_SCALE;
        if (mag(m->m11 - 1.0f) < KIND_TOLERANCE && mag(m->m22 - 1.0f) < KIND_TOLERANCE)
            kind = MAT_SHIFT;
    }
    if (m->dx == 0.0f && m->dy == 0.0f)
        kind &= ~(UINT)MAT_SHIFT;
    m->kind = kind;
}

void mat_set(ImgMat *m, float m11, float m12, float m21, float m22, float dx, float dy)
{
    m->m11 = m11;
    m->m12 = m12;
    m->m21 = m21;
    m->m22 = m22;
    m->dx = dx;
    m->dy = dy;
    mat_classify(m);
}

void mat_from_vec(ImgMat *m, const MimeVecMatrix *v)
{
    mat_set(m, v->m11, v->m12, v->m21, v->m22, v->dx, v->dy);
}

void mat_then(ImgMat *out, const ImgMat *first, const ImgMat *second)
{
    const ImgMat a = *first, b = *second;
    float m11 = b.m11 * a.m11 + b.m21 * a.m12;
    float m12 = a.m11 * b.m12 + a.m12 * b.m22;
    float m21 = b.m21 * a.m22 + b.m11 * a.m21;
    float m22 = b.m12 * a.m21 + a.m22 * b.m22;
    float dx = (a.dx * b.m11 + a.dy * b.m21) + b.dx;
    float dy = (a.dx * b.m12 + a.dy * b.m22) + b.dy;
    mat_set(out, m11, m12, m21, m22, dx, dy);
}

static BOOL finite_float(float v) { return v - v == 0.0f; }

BOOL mat_invert(ImgMat *m)
{
    if (m->kind == 0)
        return TRUE;
    float det = m->m11 * m->m22 - m->m12 * m->m21;
    if (det == 0.0f)
        return FALSE;
    float k = 1.0f / det;
    float m11 = m->m22 * k, m12 = (-m->m12) * k, m21 = (-m->m21) * k, m22 = m->m11 * k;
    float dx = (m->dy * m->m21 - m->dx * m->m22) * k;
    float dy = (m->dx * m->m12 - m->dy * m->m11) * k;
    if (!finite_float(m11) || !finite_float(m12) || !finite_float(m21) || !finite_float(m22) || !finite_float(dx) ||
        !finite_float(dy))
        return FALSE;
    mat_set(m, m11, m12, m21, m22, dx, dy);
    return TRUE;
}

BOOL mat_from_points(ImgMat *m, const float p[6], const float r[4])
{
    float x = r[0], y = r[1], w = r[2], h = r[3];
    float area = ((w + x) * (y + h) - y * x) + ((-h * x) - y * w);
    if (mag(area) < SMALL)
        return FALSE;
    float k = 1.0f / area;
    float m11 = (h * p[2] + (-h) * p[0]) * k;
    float m12 = (h * p[3] + (-h) * p[1]) * k;
    float m21 = (w * p[4] + (-w) * p[0]) * k;
    float m22 = (w * p[5] + (-w) * p[1]) * k;
    float kx = (-x) * h, ky = (-y) * w, kc = ((x + w) * (y + h)) - (x * y);
    float dx = (((kx * p[2]) + (kc * p[0])) + (ky * p[4])) * k;
    float dy = (((kx * p[3]) + (kc * p[1])) + (ky * p[5])) * k;
    mat_set(m, m11, m12, m21, m22, dx, dy);
    return TRUE;
}

void mat_apply(const ImgMat *m, float *pts, INT count)
{
    if (!m->kind)
        return;
    for (INT i = 0; i < count; i++) {
        float x = pts[2 * i], y = pts[2 * i + 1];
        float u = x, v = y;
        if (m->kind & MAT_ANY_LINEAR) {
            if (m->kind & MAT_TURNING) {
                u = y * m->m21 + x * m->m11;
                v = x * m->m12 + y * m->m22;
            } else {
                u = x * m->m11;
                v = y * m->m22;
            }
        }
        pts[2 * i] = u + m->dx;
        pts[2 * i + 1] = v + m->dy;
    }
}

void mat_shift(ImgMat *m, float tx, float ty, BOOL after)
{
    if (after) {
        m->dx += tx;
        m->dy += ty;
    } else {
        m->dx += tx * m->m11 + ty * m->m21;
        m->dy += tx * m->m12 + ty * m->m22;
    }
    mat_classify(m);
}

void mat_stretch(ImgMat *m, float sx, float sy, BOOL after)
{
    if (after) {
        m->m11 *= sx;
        m->m21 *= sx;
        m->dx *= sx;
        m->m12 *= sy;
        m->m22 *= sy;
        m->dy *= sy;
    } else {
        m->m11 *= sx;
        m->m12 *= sx;
        m->m21 *= sy;
        m->m22 *= sy;
    }
    mat_classify(m);
}

BOOL mat_is_pixel_shift(const ImgMat *m)
{
    if (m->kind & ~(UINT)MAT_SHIFT)
        return FALSE;
    float rx = (float)floor_int(m->dx + 0.5f), ry = (float)floor_int(m->dy + 0.5f);
    return mag(rx - m->dx) <= 0.015625f && mag(ry - m->dy) <= 0.015625f;
}

void mat_box(const ImgMat *m, float x0, float y0, float x1, float y1, float out[4])
{
    float c[8] = {x0, y0, x1, y0, x0, y1, x1, y1};
    mat_apply(m, c, 4);
    float lo_x = c[0], hi_x = c[0], lo_y = c[1], hi_y = c[1];
    BOOL bad = FALSE;
    for (INT i = 0; i < 4; i++) {
        float x = c[2 * i], y = c[2 * i + 1];
        if (x != x || y != y)
            bad = TRUE;
        if (x < lo_x)
            lo_x = x;
        if (x > hi_x)
            hi_x = x;
        if (y < lo_y)
            lo_y = y;
        if (y > hi_y)
            hi_y = y;
    }
    float w = hi_x - lo_x, h = hi_y - lo_y;
    if (bad) {
        lo_x = lo_y = w = h = __builtin_nanf("");
    } else {
        if (w < 0.0006f)
            w = 0.0f;
        if (h < 0.0006f)
            h = 0.0f;
    }
    out[0] = lo_x;
    out[1] = lo_y;
    out[2] = w;
    out[3] = h;
}

INT mat_turn_type(const ImgMat *m)
{
    #define NEAR(v, target) (mag((v) - (target)) < SMALL)
    if (mag(m->m11) < SMALL && mag(m->m22) < SMALL) {
        if (NEAR(m->m21, 1.0f)) {
            if (NEAR(m->m12, 1.0f))
                return 5;
            if (NEAR(m->m12, -1.0f))
                return 3;
        } else if (NEAR(m->m21, -1.0f)) {
            if (NEAR(m->m12, 1.0f))
                return 1;
            if (NEAR(m->m12, -1.0f))
                return 7;
        }
    }
    if (mag(m->m12) < SMALL && mag(m->m21) < SMALL) {
        if (NEAR(m->m11, 1.0f) && NEAR(m->m22, -1.0f))
            return 6;
        if (NEAR(m->m11, -1.0f)) {
            if (NEAR(m->m22, 1.0f))
                return 4;
            if (NEAR(m->m22, -1.0f))
                return 2;
        }
    }
    #undef NEAR
    return 0;
}
