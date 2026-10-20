/*
 * the matrices of image drawing and the spans that sample a picture. The arithmetic follows gdiplus.dll step by
 * step in single precision, because where a pixel falls, and with what weight, depends on the last bit of a
 * product.
 */
#include "image_private.h"

/* ------------------------------------------------------------------ matrices */

void aff_identity(Affine *a)
{
    a->m11 = a->m22 = 1.0f;
    a->m12 = a->m21 = a->dx = a->dy = 0.0f;
    a->cx = 0;
}

INT aff_complexity(const Affine *a)
{
    const float tolerance = 0.00059604645f;
    INT result = 15;
    /* the element that dominates, on the diagonal and off it, and the larger of the two sets the tolerance */
    float diag = absf(a->m11) > absf(a->m22) ? a->m11 : a->m22;
    float off = absf(a->m12) > absf(a->m21) ? a->m12 : a->m21;
    float big = absf(diag) <= absf(off) ? off : diag;
    float limit = absf(big) * tolerance;
    if (limit <= absf(a->m12) || limit <= absf(a->m21)) {
        if (limit > absf(a->m11 - a->m22) && limit > absf(a->m21 + a->m12)) {
            result = 7;
            if (absf((a->m12 * a->m12 + a->m11 * a->m11) - 1.0f) < tolerance)
                result = 5;
        }
    } else {
        result = 3;
        if (absf(a->m11 - 1.0f) < tolerance && absf(a->m22 - 1.0f) < tolerance)
            result = 1;
    }
    if (a->dx == 0.0f && a->dy == 0.0f)
        return result & ~1;
    return result;
}

void aff_set(Affine *a, float m11, float m12, float m21, float m22, float dx, float dy)
{
    a->m11 = m11;
    a->m12 = m12;
    a->m21 = m21;
    a->m22 = m22;
    a->dx = dx;
    a->dy = dy;
    a->cx = aff_complexity(a);
}

void aff_mul(Affine *r, const Affine *a, const Affine *b)
{
    float m11 = b->m11 * a->m11 + b->m21 * a->m12;
    float m12 = a->m11 * b->m12 + a->m12 * b->m22;
    float m21 = b->m21 * a->m22 + b->m11 * a->m21;
    float m22 = b->m12 * a->m21 + a->m22 * b->m22;
    float dx = (a->dx * b->m11 + a->dy * b->m21) + b->dx;
    float dy = (a->dx * b->m12 + a->dy * b->m22) + b->dy;
    aff_set(r, m11, m12, m21, m22, dx, dy);
}

BOOL aff_invert(Affine *a)
{
    if (a->cx == 0)
        return TRUE;
    float det = a->m11 * a->m22 - a->m12 * a->m21;
    float divisor = det == 0.0f ? 1.0f : det;
    if (absf((0.0f - det) / divisor) < 0.0000011920929f)
        return FALSE;
    float inv = 1.0f / det;
    float m11 = a->m22 * inv, m12 = -a->m12 * inv, m21 = -a->m21 * inv, m22 = a->m11 * inv;
    float dx = (a->dy * a->m21 - a->dx * a->m22) * inv;
    float dy = (a->dx * a->m12 - a->dy * a->m11) * inv;
    if (!mime_vec_finite(m11) || !mime_vec_finite(m12) || !mime_vec_finite(m21) || !mime_vec_finite(m22) ||
        !mime_vec_finite(dx) || !mime_vec_finite(dy))
        return FALSE;
    aff_set(a, m11, m12, m21, m22, dx, dy);
    return TRUE;
}

BOOL aff_infer(Affine *a, const float p[6], const float r[4])
{
    float x = r[0], y = r[1], w = r[2], h = r[3];
    float area = ((w + x) * (y + h) - y * x) + ((-h * x) - y * w);
    if (absf(area) < 0.00000011920929f)
        return FALSE;
    float inv = 1.0f / area;
    float m11 = (h * p[2] + (-h) * p[0]) * inv;
    float m12 = (h * p[3] + (-h) * p[1]) * inv;
    float m22 = (w * p[5] + (-w) * p[1]) * inv;
    float m21 = (w * p[4] + (-w) * p[0]) * inv;
    float kx = (-x) * h;
    float kc = ((x + w) * (y + h)) - (x * y);
    float ky = (-y) * w;
    float dy = (((kx * p[3]) + (kc * p[1])) + (ky * p[5])) * inv;
    float dx = (((kx * p[2]) + (kc * p[0])) + (ky * p[4])) * inv;
    aff_set(a, m11, m12, m21, m22, dx, dy);
    return TRUE;
}

void aff_transform(const Affine *a, float *p, INT count)
{
    if (a->cx == 0)
        return;
    for (INT i = 0; i < count; i++, p += 2) {
        float x = p[0], y = p[1];
        if (a->cx & ~1) {
            if (a->cx & ~3) {
                p[0] = (x * a->m11 + y * a->m21) + a->dx;
                p[1] = (x * a->m12 + y * a->m22) + a->dy;
            } else {
                p[0] = a->m11 * x + a->dx;
                p[1] = y * a->m22 + a->dy;
            }
        } else {
            p[0] = a->dx + x;
            p[1] = a->dy + y;
        }
    }
}

void aff_translate(Affine *a, float tx, float ty, BOOL append)
{
    float y = ty;
    if (!append) {
        ty = tx * a->m12 + ty * a->m22;
        tx = tx * a->m11 + y * a->m21;
    }
    a->dx += tx;
    a->dy += ty;
    a->cx |= 1;
}

void aff_scale(Affine *a, float sx, float sy, BOOL append)
{
    float m21 = a->m21, m12 = a->m12;
    a->m11 *= sx;
    a->m22 *= sy;
    if (append) {
        a->m21 = m21 * sx;
        a->m12 = m12 * sy;
        a->dx = sx * a->dx;
        a->dy = sy * a->dy;
    } else {
        a->m12 = m12 * sx;
        a->m21 = m21 * sy;
    }
    a->cx = aff_complexity(a);
}

BOOL aff_integer_translate(const Affine *a)
{
    if (a->cx & ~1)
        return FALSE;
    if (absf((float)floor_int(a->dx + 0.5f) - a->dx) > 0.015625f)
        return FALSE;
    return absf((float)floor_int(a->dy + 0.5f) - a->dy) <= 0.015625f;
}

void aff_bounds(const Affine *a, float x0, float y0, float x1, float y1, float out[4])
{
    float lo_x = x0, hi_x = x1, lo_y = y0, hi_y = y1;
    if (a->cx != 0) {
        if (a->cx & ~3) {
            /* the four corners of a turned rectangle */
            float cx_ = a->m21 * y0, dx_ = a->m21 * y1, ex = a->m22 * y0, fx = a->m22 * y1;
            float gx = a->m12 * x1, hx = a->m11 * x1, ix = a->m12 * x0, jx = a->m11 * x0;
            float c[4], d[4];
            c[0] = (jx + cx_) + a->dx;
            c[1] = (hx + cx_) + a->dx;
            c[2] = (hx + dx_) + a->dx;
            c[3] = (jx + dx_) + a->dx;
            d[0] = (ix + ex) + a->dy;
            d[1] = (gx + ex) + a->dy;
            d[2] = (gx + fx) + a->dy;
            d[3] = (ix + fx) + a->dy;
            lo_x = hi_x = c[0];
            lo_y = hi_y = d[0];
            for (INT i = 1; i < 4; i++) {
                if (c[i] < lo_x)
                    lo_x = c[i];
                if (c[i] > hi_x)
                    hi_x = c[i];
                if (d[i] < lo_y)
                    lo_y = d[i];
                if (d[i] > hi_y)
                    hi_y = d[i];
            }
        } else {
            float p[4] = {x0, y0, x1, y1};
            aff_transform(a, p, 2);
            lo_x = p[0] < p[2] ? p[0] : p[2];
            hi_x = p[0] < p[2] ? p[2] : p[0];
            lo_y = p[1] < p[3] ? p[1] : p[3];
            hi_y = p[1] < p[3] ? p[3] : p[1];
        }
    }
    float w = hi_x - lo_x, h = hi_y - lo_y;
    out[0] = lo_x;
    out[1] = lo_y;
    out[2] = w <= 0.00059604645f ? 0.0f : w;
    out[3] = h <= 0.00059604645f ? 0.0f : h;
}

/* ------------------------------------------------------------------ the cubic kernel */

/* a=-0.5 cubic convolution, 65536 at the center, tabulated every 1/64 up to 2. */
const INT *cubic_table(void)
{
    static INT table[129];
    static BOOL ready;
    if (!ready) {
        for (INT k = 0; k <= 128; k++) {
            double x = (double)k / 64.0, w;
            if (x < 1.0)
                w = 1.5 * x * x * x - 2.5 * x * x + 1.0;
            else
                w = -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
            double scaled = w * 65536.0 + 0.5;
            INT v = (INT)scaled;
            if ((double)v > scaled)
                v--;
            table[k] = v;
        }
        ready = TRUE;
    }
    return table;
}

/* ------------------------------------------------------------------ wrapping */

/* the index n times around a row of w pixels, a flipped copy on the odd laps. */
static INT wrap_index(INT i, INT w, BOOL flip)
{
    INT r = i < 0 ? w - (~i % w) - 1 : i % w;
    if (flip && (((i - r) / w) & 1))
        r = w - r - 1;
    return r;
}

/* the wrap mode on a pair of coordinates, and nothing for the clamp mode and beyond. */
static void wrap_pair(INT mode, INT *x, INT *y, INT w, INT h)
{
    if (mode < 0 || mode > 3)
        return;
    *x = wrap_index(*x, w, mode == 1 || mode == 3);
    *y = wrap_index(*y, h, mode == 2 || mode == 3);
}

static inline UINT32 pixel_at(const ImgSpan *s, INT x, INT y)
{
    if ((UINT)x >= (UINT)s->w || (UINT)y >= (UINT)s->h)
        return s->clamp;
    return s->px[(SSIZE_T)y * s->stride + x];
}

/* ------------------------------------------------------------------ positions */

/* the picture position of the device point x, y. */
static inline void position(const Affine *a, INT x, INT y, float *u, float *v)
{
    float fx = (float)x, fy = (float)y;
    if (a->cx == 0) {
        *u = fx;
        *v = fy;
        return;
    }
    float uu = fx, vv = fy;
    if (a->cx & ~1) {
        if (a->cx & ~3) {
            uu = fy * a->m21 + fx * a->m11;
            vv = fx * a->m12 + fy * a->m22;
        } else {
            uu = fx * a->m11;
            vv = fy * a->m22;
        }
    }
    *u = uu + a->dx;
    *v = vv + a->dy;
}

/* ------------------------------------------------------------------ identity */

static void run_identity(const ImgSpan *s, INT y, INT x0, INT x1, UINT32 *out)
{
    INT n = x1 - x0;
    INT sy = y + s->off_y, sx = x0 + s->off_x;
    if (s->wrap != 0) {
        /* clamp: the color outside the picture */
        if ((UINT)sy >= (UINT)s->h || sx >= s->w || sx + n <= 0) {
            for (INT i = 0; i < n; i++)
                out[i] = s->clamp;
            return;
        }
        const UINT32 *row = s->px + (SSIZE_T)sy * s->stride;
        INT i = 0;
        for (; i < n && sx + i < 0; i++)
            out[i] = s->clamp;
        INT inside = n - i;
        if (inside > s->w - (sx + i))
            inside = s->w - (sx + i);
        if (inside > 0) {
            memcpy(out + i, row + sx + i, (SIZE_T)inside * 4);
            i += inside;
        }
        for (; i < n; i++)
            out[i] = s->clamp;
        return;
    }
    sy = wrap_index(sy, s->h, FALSE);
    sx = wrap_index(sx, s->w, FALSE);
    const UINT32 *row = s->px + (SSIZE_T)sy * s->stride;
    for (INT i = 0; i < n;) {
        INT take = s->w - sx;
        if (take > n - i)
            take = n - i;
        memcpy(out + i, row + sx, (SIZE_T)take * 4);
        i += take;
        sx = 0;
    }
}

/* ------------------------------------------------------------------ bilinear */

/* the bilinear span of the 64 bit library: positions in floats, weights of 11 bits. */
static void run_bilinear_float(const ImgSpan *s, INT y, INT x0, INT x1, UINT32 *out)
{
    INT n = x1 - x0;
    float u, v, ue, ve;
    position(&s->inv, x0, y, &u, &v);
    position(&s->inv, x1, y, &ue, &ve);
    float du = (ue - u) / (float)n, dv = (ve - v) / (float)n;
    for (INT i = 0; i < n; i++, u += du, v += dv) {
        INT ix = floor_int(u), iy = floor_int(v);
        INT fx = floor_int((u - (float)ix) * 2048.0f + 0.5f), fy = floor_int((v - (float)iy) * 2048.0f + 0.5f);
        INT xa = ix, xb = ix + 1, ya = iy, yb = iy + 1;
        if ((UINT)ix >= (UINT)(s->w - 1) || (UINT)iy >= (UINT)(s->h - 1)) {
            wrap_pair(s->wrap, &xa, &ya, s->w, s->h);
            wrap_pair(s->wrap, &xb, &yb, s->w, s->h);
        }
        if (xb < 0 || xa >= s->w || yb < 0 || ya >= s->h) {
            out[i] = s->clamp;
            continue;
        }
        UINT32 p00 = pixel_at(s, xa, ya), p10 = pixel_at(s, xb, ya), p01 = pixel_at(s, xa, yb),
               p11 = pixel_at(s, xb, yb);
        UINT32 r = 0;
        for (INT sh = 0; sh < 32; sh += 8) {
            INT a = (INT)((p00 >> sh) & 255), b = (INT)((p10 >> sh) & 255), c = (INT)((p01 >> sh) & 255),
                d = (INT)((p11 >> sh) & 255);
            INT top = (a << 11) + fx * (b - a), bot = (c << 11) + fx * (d - c);
            UINT c8 = (UINT)((fy * bot + (2048 - fy) * top + 0x200000) >> 22);
            r |= (c8 & 255) << sh;
        }
        out[i] = r;
    }
}

#if defined(__i386__)

/*
 * the 32 bit library interpolates in fixed point: the inverse matrix is rounded to 16.16, the position of a pixel
 * is the first position plus whole steps, and the weights are the high 8 bits of the fraction.
 */
static void bilinear_init(ImgSpan *s)
{
    const Affine *m = &s->inv;
    s->fix_ok = m->m11 >= -32768.0f && m->m11 <= 32767.0f && m->m12 >= -32768.0f && m->m12 <= 32767.0f &&
                m->m21 >= -32768.0f && m->m21 <= 32767.0f && m->m22 >= -32768.0f && m->m22 <= 32767.0f;
    s->fix_matrix_ok = FALSE;
    if (!s->fix_ok)
        return;
    float su = 1.0f, sv = 0.0f;
    if (m->cx != 0) {
        su = m->m21 * 0.0f + m->m11;
        sv = 0.0f * m->m22 + m->m12;
    }
    s->fix_step_u = floor_int(su * 65536.0f + 0.5f);
    s->fix_step_v = floor_int(sv * 65536.0f + 0.5f);
    s->fix_matrix_ok = m->dx >= -32768.0f && m->dx <= 32767.0f && m->dy >= -32768.0f && m->dy <= 32767.0f;
    if (s->fix_matrix_ok) {
        s->fix_m11 = floor_int(m->m11 * 65536.0f + 0.5f);
        s->fix_m12 = floor_int(m->m12 * 65536.0f + 0.5f);
        s->fix_m21 = floor_int(m->m21 * 65536.0f + 0.5f);
        s->fix_m22 = floor_int(m->m22 * 65536.0f + 0.5f);
        s->fix_dx = floor_int(m->dx * 65536.0f + 0.5f);
        s->fix_dy = floor_int(m->dy * 65536.0f + 0.5f);
    }
}

/* a byte lane of a lerp with an 8 bit weight, rounded. */
static inline UINT lerp8(UINT a, UINT b, UINT f) { return (UINT)((INT)a + ((((INT)b - (INT)a) * (INT)f + 128) >> 8)); }

static UINT32 mix8(UINT32 p00, UINT32 p10, UINT32 p01, UINT32 p11, UINT fx, UINT fy)
{
    UINT32 out = 0;
    for (INT sh = 0; sh < 32; sh += 8) {
        UINT top = lerp8((p00 >> sh) & 255, (p10 >> sh) & 255, fx);
        UINT bot = lerp8((p01 >> sh) & 255, (p11 >> sh) & 255, fx);
        out |= (lerp8(top, bot, fy) & 255) << sh;
    }
    return out;
}

/* the index in one axis with the mirrored copies of a flip mode, and the one next to it. */
static void fix_axis(INT i, INT n, BOOL flip, INT *a, INT *b)
{
    if (!flip) {
        *a = i;
        *b = i + 1 < n ? i + 1 : 0;
        return;
    }
    if (i < n) {
        *a = i;
        *b = i + 1 < n ? i + 1 : n - 1;
    } else {
        *a = 2 * n - 1 - i;
        *b = i + 1 < 2 * n ? 2 * n - 2 - i : 0;
    }
}

static void run_bilinear_fixed(const ImgSpan *s, INT y, INT x0, INT x1, UINT32 *out)
{
    INT n = x1 - x0;
    INT u, v;
    if (!s->fix_matrix_ok) {
        float fu, fv;
        position(&s->inv, x0, y, &fu, &fv);
        u = floor_int(fu * 65536.0f + 0.5f);
        v = floor_int(fv * 65536.0f + 0.5f);
    } else {
        u = (INT)((UINT)s->fix_dx + (UINT)x0 * (UINT)s->fix_m11 + (UINT)y * (UINT)s->fix_m21);
        v = (INT)((UINT)s->fix_dy + (UINT)x0 * (UINT)s->fix_m12 + (UINT)y * (UINT)s->fix_m22);
    }
    BOOL flip_x = s->wrap == 1 || s->wrap == 3, flip_y = s->wrap == 2 || s->wrap == 3;
    UINT period_x = (UINT)s->w << 16, period_y = (UINT)s->h << 16;
    if (flip_x)
        period_x *= 2;
    if (flip_y)
        period_y *= 2;
    UINT edge_x = (UINT)(s->w - 1) << 16, edge_y = (UINT)(s->h - 1) << 16;
    for (INT i = 0; i < n; i++) {
        UINT32 p00, p10, p01, p11;
        if ((UINT)u < edge_x && (UINT)v < edge_y) {
            const UINT32 *q = s->px + (SSIZE_T)(v >> 16) * s->stride + (u >> 16);
            p00 = q[0];
            p10 = q[1];
            p01 = q[s->stride];
            p11 = q[s->stride + 1];
        } else if (s->wrap == 4) {
            INT ix = u >> 16, iy = v >> 16;
            p00 = p10 = p01 = p11 = s->clamp;
            if (ix >= -1 && ix < s->w && iy >= -1 && iy < s->h) {
                if (iy >= 0) {
                    if (ix >= 0)
                        p00 = s->px[(SSIZE_T)iy * s->stride + ix];
                    if (ix < s->w - 1)
                        p10 = s->px[(SSIZE_T)iy * s->stride + ix + 1];
                }
                if (iy < s->h - 1) {
                    if (ix >= 0)
                        p01 = s->px[(SSIZE_T)(iy + 1) * s->stride + ix];
                    if (ix < s->w - 1)
                        p11 = s->px[(SSIZE_T)(iy + 1) * s->stride + ix + 1];
                }
            }
        } else if (s->wrap > 4 || s->wrap < 0) {
            p00 = p10 = p01 = p11 = s->clamp;
        } else {
            INT wu = u, wv = v;
            if ((UINT)wu >= period_x) {
                wu %= (INT)period_x;
                if (wu < 0)
                    wu += (INT)period_x;
            }
            if ((UINT)wv >= period_y) {
                wv %= (INT)period_y;
                if (wv < 0)
                    wv += (INT)period_y;
            }
            INT xa, xb, ya, yb;
            fix_axis(wu >> 16, s->w, flip_x, &xa, &xb);
            fix_axis(wv >> 16, s->h, flip_y, &ya, &yb);
            p00 = s->px[(SSIZE_T)ya * s->stride + xa];
            p10 = s->px[(SSIZE_T)ya * s->stride + xb];
            p01 = s->px[(SSIZE_T)yb * s->stride + xa];
            p11 = s->px[(SSIZE_T)yb * s->stride + xb];
        }
        out[i] = mix8(p00, p10, p01, p11, ((UINT)u >> 8) & 255, ((UINT)v >> 8) & 255);
        u = (INT)((UINT)u + (UINT)s->fix_step_u);
        v = (INT)((UINT)v + (UINT)s->fix_step_v);
    }
}

#endif

/* ------------------------------------------------------------------ 16.16 spans: nearest neighbor and bicubic */

/* where the run starts and how far each pixel steps, in 16.16. */
static void fixed_run(const Affine *a, INT y, INT x0, INT x1, INT *u, INT *v, INT *du, INT *dv)
{
    float fu, fv, fue, fve;
    position(a, x0, y, &fu, &fv);
    position(a, x1, y, &fue, &fve);
    float n = (float)(x1 - x0);
    *u = floor_int(fu * 65536.0f + 0.5f);
    *v = floor_int(fv * 65536.0f + 0.5f);
    *du = floor_int((fue - fu) * 65536.0f / n + 0.5f);
    *dv = floor_int((fve - fv) * 65536.0f / n + 0.5f);
}

static void run_nearest(const ImgSpan *s, INT y, INT x0, INT x1, UINT32 *out)
{
    INT u, v, du, dv;
    fixed_run(&s->inv, y, x0, x1, &u, &v, &du, &dv);
    UINT uu = (UINT)u + 0x8000u, vv = (UINT)v + 0x8000u;
    for (INT i = 0; i < x1 - x0; i++, uu += (UINT)du, vv += (UINT)dv) {
        INT ix = (INT)uu >> 16, iy = (INT)vv >> 16;
        if ((UINT)ix >= (UINT)s->w || (UINT)iy >= (UINT)s->h)
            wrap_pair(s->wrap, &ix, &iy, s->w, s->h);
        out[i] = pixel_at(s, ix, iy);
    }
}

#if defined(__i386__)

/* the same weights at 16384, which the 32 bit library has: a quarter of the weight rounded away from zero. */
static INT cubic16(INT k)
{
    INT t = cubic_table()[k];
    INT m = ((t < 0 ? -t : t) + 2) >> 2;
    return t < 0 ? -m : m;
}

/* the weights the 32 bit library uses: 16384 for one, and every term cut at 1/16 of a level. */
static UINT32 cubic_pass(const UINT32 p[4], INT frac)
{
    INT w[4] = {cubic16(64 + frac), cubic16(frac), cubic16(64 - frac), cubic16(128 - frac)};
    INT c[4];
    for (INT k = 0; k < 4; k++) {
        INT sh = 8 * k, sum = 0;
        for (INT t = 0; t < 4; t++)
            sum += ((INT)((p[t] >> sh) & 255) * 32 * w[t]) >> 16;
        sum = (sum + 4) >> 3;
        c[k] = sum < 0 ? 0 : (sum > 255 ? 255 : sum);
    }
    for (INT k = 0; k < 3; k++) {
        if (c[k] > c[3])
            c[k] = c[3];
    }
    return (UINT32)c[0] | (UINT32)c[1] << 8 | (UINT32)c[2] << 16 | (UINT32)c[3] << 24;
}

#else

/* one pass over four pixels: each channel is the weighted sum cut at a level, colors held to the alpha. */
static UINT32 cubic_pass(const UINT32 p[4], INT frac)
{
    const INT *t = cubic_table();
    INT w[4] = {t[64 + frac], t[frac], t[64 - frac], t[128 - frac]};
    INT c[4];
    for (INT k = 0; k < 4; k++) {
        INT sh = 8 * k;
        INT sum = w[0] * (INT)((p[0] >> sh) & 255) + w[1] * (INT)((p[1] >> sh) & 255) +
                  w[2] * (INT)((p[2] >> sh) & 255) + w[3] * (INT)((p[3] >> sh) & 255);
        c[k] = sum >> 16;
    }
    INT a = c[3] < 0 ? 0 : (c[3] > 255 ? 255 : c[3]);
    for (INT k = 0; k < 3; k++)
        c[k] = c[k] < 0 ? 0 : (c[k] > a ? a : c[k]);
    return (UINT32)c[0] | (UINT32)c[1] << 8 | (UINT32)c[2] << 16 | (UINT32)a << 24;
}

#endif

static void run_bicubic(const ImgSpan *s, INT y, INT x0, INT x1, UINT32 *out)
{
    INT u, v, du, dv;
    fixed_run(&s->inv, y, x0, x1, &u, &v, &du, &dv);
    INT wmin = s->w - 4 > 0 ? s->w - 4 : 0, hmin = s->h - 4 > 0 ? s->h - 4 : 0;
    for (INT i = 0; i < x1 - x0; i++, u = (INT)((UINT)u + (UINT)du), v = (INT)((UINT)v + (UINT)dv)) {
        INT xs[4], ys[4];
        for (INT k = 0; k < 4; k++) {
            xs[k] = (u >> 16) + k - 1;
            ys[k] = (v >> 16) + k - 1;
        }
        if (s->wrap != 4 && ((UINT)xs[0] >= (UINT)wmin || (UINT)ys[0] >= (UINT)hmin)) {
            for (INT k = 0; k < 4; k++)
                wrap_pair(s->wrap, &xs[k], &ys[k], s->w, s->h);
        }
        UINT32 col[4];
        for (INT c = 0; c < 4; c++) {
            UINT32 p[4];
            for (INT r = 0; r < 4; r++)
                p[r] = pixel_at(s, xs[c], ys[r]);
            col[c] = cubic_pass(p, (v >> 10) & 0x3f);
        }
        out[i] = cubic_pass(col, (u >> 10) & 0x3f);
    }
}

/* ------------------------------------------------------------------ the span of a draw */

BOOL span_create(ImgSpan *s, const SpanSetup *setup)
{
    memset(s, 0, sizeof(*s));
    s->px = setup->px;
    s->w = setup->w;
    s->h = setup->h;
    s->stride = setup->stride;
    s->wrap = setup->wrap;
    s->clamp = setup->clamp;
    s->inv = setup->fwd;
    if (!aff_invert(&s->inv))
        aff_identity(&s->inv);
    InterpolationMode mode = setup->mode;
    BOOL integer = aff_integer_translate(&setup->fwd);
    BOOL flat = (setup->fwd.cx & ~3) == 0;
    SpanKind kind = SPAN_BILINEAR;
    BOOL generic = FALSE;
    if (!integer) {
        switch (mode) {
        case InterpolationModeBicubic:
            kind = SPAN_BICUBIC;
            break;
        case InterpolationModeNearestNeighbor:
            kind = SPAN_NEAREST;
            break;
        case InterpolationModeHighQualityBilinear:
            if (flat) {
                s->stretch = stretch_create(setup, FALSE);
                /* a filter that cannot be made leaves the draw to the generic bilinear span */
                generic = s->stretch == nullptr;
                if (s->stretch)
                    kind = SPAN_STRETCH;
            }
            break;
        case InterpolationModeHighQuality:
        case InterpolationModeHighQualityBicubic:
            if (!flat) {
                kind = SPAN_BICUBIC;
            } else {
                s->stretch = stretch_create(setup, TRUE);
                generic = s->stretch == nullptr;
                if (s->stretch)
                    kind = SPAN_STRETCH;
            }
            break;
        default:
            break;
        }
    }
    if (kind == SPAN_BILINEAR && !generic) {
        if (integer && (setup->wrap & ~4) == 0) {
            kind = SPAN_IDENTITY;
            s->off_x = -floor_int(setup->fwd.dx + 0.5f);
            s->off_y = -floor_int(setup->fwd.dy + 0.5f);
        } else {
#if defined(__i386__)
            /* the fixed point span takes pictures whose size, doubled for the flips, fits 16 bits */
            BOOL fits = s->w + 0x8000 >= 0 && s->w <= 0x7fff && s->h + 0x8000 >= 0 && s->h <= 0x7fff;
            if ((setup->wrap == 1 || setup->wrap == 3) && 2 * s->w > 0x7fff)
                fits = FALSE;
            if ((setup->wrap == 2 || setup->wrap == 3) && 2 * s->h > 0x7fff)
                fits = FALSE;
            if (fits) {
                kind = SPAN_BILINEAR_FIXED;
                bilinear_init(s);
            }
#endif
        }
    }
    s->kind = kind;
    return s->w > 0 && s->h > 0;
}

void span_free(ImgSpan *s)
{
    stretch_free(s->stretch);
    s->stretch = nullptr;
}

void span_run(ImgSpan *s, INT y, INT x0, INT x1, UINT32 *out)
{
    if (x1 <= x0)
        return;
    switch (s->kind) {
    case SPAN_IDENTITY:
        run_identity(s, y, x0, x1, out);
        break;
    case SPAN_NEAREST:
        run_nearest(s, y, x0, x1, out);
        break;
    case SPAN_BICUBIC:
        run_bicubic(s, y, x0, x1, out);
        break;
    case SPAN_STRETCH:
        stretch_run(s->stretch, y, x0, x1, out);
        break;
#if defined(__i386__)
    case SPAN_BILINEAR_FIXED:
        run_bilinear_fixed(s, y, x0, x1, out);
        break;
#endif
    default:
        run_bilinear_float(s, y, x0, x1, out);
        break;
    }
}
