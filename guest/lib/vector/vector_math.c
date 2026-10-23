/* float math without libm, and 2x3 matrices. */
#include "vector.h"

#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

#define PI_F 3.14159265358979f
#define HALF_PI_F 1.57079632679490f

float mime_vec_sqrtf(float x)
{
    if (!(x > 0.0f))
        return 0.0f;
#if defined(__SSE2__)
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));
#else
    if (x - x != 0.0f)
        return x;
    /* initial estimate by halving the exponent, then Newton steps. */
    union {
        float f;
        uint32_t u;
    } v = {x};
    v.u = (v.u >> 1) + 0x1fbd1df5u;
    float y = v.f;
    for (int i = 0; i < 4; i++)
        y = 0.5f * (y + x / y);
    return y;
#endif
}

float mime_vec_floorf(float x)
{
    if (!(x > -8388608.0f && x < 8388608.0f))
        return x;
    int32_t i = (int32_t)x;
    float f = (float)i;
    return f > x ? f - 1.0f : f;
}

float mime_vec_ceilf(float x)
{
    if (!(x > -8388608.0f && x < 8388608.0f))
        return x;
    int32_t i = (int32_t)x;
    float f = (float)i;
    return f < x ? f + 1.0f : f;
}

/* sine and cosine on [-pi/4, pi/4]. */
static void sincos_small(float x, float *s, float *c)
{
    float x2 = x * x;
    *s = x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880)))));
    *c = 1.0f + x2 * (-0.5f + x2 * (1.0f / 24 + x2 * (-1.0f / 720 + x2 * (1.0f / 40320 + x2 * (-1.0f / 3628800)))));
}

void mime_vec_sincosf(float a, float *s, float *c)
{
    if (!mime_vec_finite(a)) {
        *s = 0.0f;
        *c = 1.0f;
        return;
    }
    /* quadrant by rounding a / pi/2, the remainder subtracts a three part constant. */
    float qf = mime_vec_floorf(a * (1.0f / HALF_PI_F) + 0.5f);
    float r;
    int q;
    if (qf > -1.0e6f && qf < 1.0e6f) {
        q = (int)qf;
        r = ((a - qf * 1.5703125f) - qf * 4.837512969970703125e-4f) - qf * 7.549789948768648e-8f;
    } else {
        /* absurdly large angle, reduce coarsely. */
        float t = a - mime_vec_floorf(a * (1.0f / (2 * PI_F))) * (2 * PI_F);
        qf = mime_vec_floorf(t * (1.0f / HALF_PI_F) + 0.5f);
        q = (int)qf;
        r = t - qf * HALF_PI_F;
    }
    float sr, cr;
    sincos_small(r, &sr, &cr);
    switch (q & 3) {
    case 0:
        *s = sr;
        *c = cr;
        break;
    case 1:
        *s = cr;
        *c = -sr;
        break;
    case 2:
        *s = -sr;
        *c = -cr;
        break;
    default:
        *s = -cr;
        *c = sr;
        break;
    }
}

/* arctangent for z up to tan of pi over 8. */
static float atan_small(float z)
{
    /* one more reduction around pi/16 keeps the series short. */
    float base = 0.0f;
    float sign = 1.0f;
    if (z < 0) {
        z = -z;
        sign = -1.0f;
    }
    if (z > 0.2f) {
        const float t16 = 0.198912367f;
        z = (z - t16) / (1.0f + z * t16);
        base = PI_F / 16.0f;
    }
    float z2 = z * z;
    float r = z * (1.0f + z2 * (-1.0f / 3 + z2 * (1.0f / 5 + z2 * (-1.0f / 7 + z2 * (1.0f / 9 + z2 * (-1.0f / 11 + z2 * (1.0f / 13)))))));
    return sign * (base + r);
}

float mime_vec_atan2f(float y, float x)
{
    if (!(x == x) || !(y == y))
        return 0.0f;
    if (x > 1.0e30f)
        x = 1.0e30f;
    else if (x < -1.0e30f)
        x = -1.0e30f;
    if (y > 1.0e30f)
        y = 1.0e30f;
    else if (y < -1.0e30f)
        y = -1.0e30f;
    float ax = x < 0 ? -x : x;
    float ay = y < 0 ? -y : y;
    if (ax == 0.0f && ay == 0.0f)
        return 0.0f;
    float a;
    if (ay <= ax) {
        float z = ay / ax; /* [0, 1]. */
        if (z > 0.41421356f)
            a = PI_F / 4 + atan_small((z - 1.0f) / (z + 1.0f));
        else
            a = atan_small(z);
    } else {
        float z = ax / ay;
        if (z > 0.41421356f)
            a = PI_F / 4 + atan_small((z - 1.0f) / (z + 1.0f));
        else
            a = atan_small(z);
        a = HALF_PI_F - a;
    }
    if (x < 0)
        a = PI_F - a;
    return y < 0 ? -a : a;
}

void mime_vec_matrix_identity(MimeVecMatrix *m)
{
    m->m11 = 1.0f;
    m->m12 = 0.0f;
    m->m21 = 0.0f;
    m->m22 = 1.0f;
    m->dx = 0.0f;
    m->dy = 0.0f;
}

void mime_vec_matrix_multiply(MimeVecMatrix *out, const MimeVecMatrix *a, const MimeVecMatrix *b)
{
    MimeVecMatrix r;
    r.m11 = a->m11 * b->m11 + a->m12 * b->m21;
    r.m12 = a->m11 * b->m12 + a->m12 * b->m22;
    r.m21 = a->m21 * b->m11 + a->m22 * b->m21;
    r.m22 = a->m21 * b->m12 + a->m22 * b->m22;
    r.dx = a->dx * b->m11 + a->dy * b->m21 + b->dx;
    r.dy = a->dx * b->m12 + a->dy * b->m22 + b->dy;
    *out = r;
}

bool mime_vec_matrix_invert(MimeVecMatrix *out, const MimeVecMatrix *m)
{
    float det = m->m11 * m->m22 - m->m12 * m->m21;
    float adet = det < 0 ? -det : det;
    if (!mime_vec_finite(det) || !(adet > 1.0e-30f))
        return false;
    float inv = 1.0f / det;
    MimeVecMatrix r;
    r.m11 = m->m22 * inv;
    r.m12 = -m->m12 * inv;
    r.m21 = -m->m21 * inv;
    r.m22 = m->m11 * inv;
    r.dx = (m->m21 * m->dy - m->m22 * m->dx) * inv;
    r.dy = (m->m12 * m->dx - m->m11 * m->dy) * inv;
    if (!mime_vec_finite(r.m11 + r.m12 + r.m21 + r.m22 + r.dx + r.dy))
        return false;
    *out = r;
    return true;
}

void mime_vec_matrix_translate(MimeVecMatrix *m, float dx, float dy)
{
    MimeVecMatrix t = {1.0f, 0.0f, 0.0f, 1.0f, dx, dy};
    mime_vec_matrix_multiply(m, &t, m);
}

void mime_vec_matrix_scale(MimeVecMatrix *m, float sx, float sy)
{
    MimeVecMatrix t = {sx, 0.0f, 0.0f, sy, 0.0f, 0.0f};
    mime_vec_matrix_multiply(m, &t, m);
}

void mime_vec_matrix_rotate(MimeVecMatrix *m, float degrees)
{
    float s, c;
    if (!mime_vec_finite(degrees))
        return;
    degrees -= mime_vec_floorf(degrees * (1.0f / 360.0f)) * 360.0f;
    /* exact values at the right angles, so a quarter turn stays exact. */
    if (degrees == 0.0f) {
        s = 0.0f;
        c = 1.0f;
    } else if (degrees == 90.0f) {
        s = 1.0f;
        c = 0.0f;
    } else if (degrees == 180.0f) {
        s = 0.0f;
        c = -1.0f;
    } else if (degrees == 270.0f) {
        s = -1.0f;
        c = 0.0f;
    } else {
        mime_vec_sincosf(degrees * (PI_F / 180.0f), &s, &c);
    }
    MimeVecMatrix t = {c, s, -s, c, 0.0f, 0.0f};
    mime_vec_matrix_multiply(m, &t, m);
}

float mime_vec_matrix_max_scale(const MimeVecMatrix *m)
{
    /* largest singular value. */
    float a = m->m11 * m->m11 + m->m12 * m->m12 + m->m21 * m->m21 + m->m22 * m->m22;
    float det = m->m11 * m->m22 - m->m12 * m->m21;
    float d = a * a - 4.0f * det * det;
    if (d < 0)
        d = 0;
    float s2 = 0.5f * (a + mime_vec_sqrtf(d));
    float s = mime_vec_sqrtf(s2);
    return mime_vec_finite(s) ? s : 1.0f;
}
