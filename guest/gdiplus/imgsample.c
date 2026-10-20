/*
 * the samplers: how a run of device pixels reads the premultiplied rows of a picture. Which one is used is decided
 * per draw from the matrix and the interpolation mode, see sampler_create and docs/gdiplus.md.
 *
 * positions in the picture are the coordinates where pixel k has its center at k, and a device pixel x, y samples at
 * the inverse matrix of x, y.
 */
#include "image_private.h"

#if defined(__i386__)
#define HAVE_FIXED_BILINEAR 1
#endif

/* the wrap modes of the attributes: 0 to 3 tile, with a mirror on the x axis, the y axis or both, and 4 is clamp. */
#define WRAP_CLAMP 4

static BOOL mirror_x(INT wrap) { return wrap == 1 || wrap == 3; }
static BOOL mirror_y(INT wrap) { return wrap == 2 || wrap == 3; }

/* an index brought into 0 to n - 1 by tiling, or by mirroring on every odd lap. */
static INT wrap_axis(INT i, INT n, BOOL mirror)
{
    INT r = i >= 0 ? i % n : n - ((~i) % n) - 1;
    if (mirror && (((i - r) / n) & 1))
        r = n - r - 1;
    return r;
}

/* the position of a pixel after a wrap mode that tiles, others leave it. */
static void wrap_point(INT wrap, INT *x, INT *y, INT w, INT h)
{
    if ((UINT)wrap > 3)
        return;
    *x = wrap_axis(*x, w, mirror_x(wrap));
    *y = wrap_axis(*y, h, mirror_y(wrap));
}

static inline UINT32 pixel_at(const Sampler *s, INT x, INT y) { return s->px[(SSIZE_T)y * s->stride + x]; }

static inline BOOL in_picture(const Sampler *s, INT x, INT y)
{
    return x >= 0 && y >= 0 && x < s->w && y < s->h;
}

/* the pixel at a position that may be outside: wrapped when the mode tiles, else the clamp color. */
static UINT32 pixel_wrapped(const Sampler *s, INT x, INT y)
{
    if (!in_picture(s, x, y)) {
        wrap_point(s->wrap, &x, &y, s->w, s->h);
        if (!in_picture(s, x, y))
            return s->clamp;
    }
    return pixel_at(s, x, y);
}

static void position_at(const Sampler *s, INT x, INT y, float *u, float *v)
{
    float p[2] = {(float)x, (float)y};
    mat_apply(&s->inv, p, 1);
    *u = p[0];
    *v = p[1];
}

/* floor of a float that may be far out, held to the 32 bit range. */
static INT floor_wide(float f)
{
    if (!(f > -2147483648.0f))
        return (INT)0x80000000;
    if (!(f < 2147483648.0f))
        return 0x7fffffff;
    INT i = (INT)f;
    return (float)i > f ? i - 1 : i;
}

/* the 16.16 start and step of the positions of a run, from the float positions at its two ends. */
static void fixed_walk(const Sampler *s, INT y, INT x0, INT x1, INT *u, INT *v, INT *du, INT *dv)
{
    float uf, vf, ue, ve;
    position_at(s, x0, y, &uf, &vf);
    position_at(s, x1, y, &ue, &ve);
    float n = (float)(x1 - x0);
    *u = fix16(uf);
    *v = fix16(vf);
    *du = floor_wide(((ue - uf) * 65536.0f) / n + 0.5f);
    *dv = floor_wide(((ve - vf) * 65536.0f) / n + 0.5f);
}

/* ------------------------------------------------------------------ a copy */

static void run_copy(const Sampler *s, INT y, INT x0, INT x1, UINT32 *out)
{
    INT n = x1 - x0;
    INT row = y + s->shift_y, col = x0 + s->shift_x;
    if (s->wrap == 0) {
        row = wrap_axis(row, s->h, FALSE);
        col = wrap_axis(col, s->w, FALSE);
        const UINT32 *src = s->px + (SSIZE_T)row * s->stride;
        INT done = 0;
        while (done < n) {
            INT take = s->w - col < n - done ? s->w - col : n - done;
            memcpy(out + done, src + col, sizeof(UINT32) * (SIZE_T)take);
            done += take;
            col = 0;
        }
        return;
    }
    if (row < 0 || row >= s->h || col >= s->w || col + n <= 0) {
        for (INT i = 0; i < n; i++)
            out[i] = s->clamp;
        return;
    }
    INT i = 0;
    for (; i < n && col + i < 0; i++)
        out[i] = s->clamp;
    INT inner = s->w - (col + i) < n - i ? s->w - (col + i) : n - i;
    memcpy(out + i, s->px + (SSIZE_T)row * s->stride + col + i, sizeof(UINT32) * (SIZE_T)inner);
    for (i += inner; i < n; i++)
        out[i] = s->clamp;
}

/* ------------------------------------------------------------------ nearest */

static void run_nearest(const Sampler *s, INT y, INT x0, INT x1, UINT32 *out)
{
    INT u, v, du, dv;
    fixed_walk(s, y, x0, x1, &u, &v, &du, &dv);
    for (INT i = 0; i < x1 - x0; i++) {
        INT px = (INT)((UINT32)u + 0x8000u) >> 16, py = (INT)((UINT32)v + 0x8000u) >> 16;
        out[i] = pixel_wrapped(s, px, py);
        u = (INT)((UINT32)u + (UINT32)du);
        v = (INT)((UINT32)v + (UINT32)dv);
    }
}

/* ------------------------------------------------------------------ bilinear */

/* the 2 by 2 window with its corner at ix, iy: the neighbours that are outside follow the wrap mode. */
static void window_of(const Sampler *s, INT ix, INT iy, UINT32 q[4])
{
    INT x0 = ix, y0 = iy, x1 = ix + 1, y1 = iy + 1;
    if ((UINT)ix >= (UINT)(s->w - 1) || (UINT)iy >= (UINT)(s->h - 1)) {
        wrap_point(s->wrap, &x0, &y0, s->w, s->h);
        wrap_point(s->wrap, &x1, &y1, s->w, s->h);
        if (x1 < 0 || x0 >= s->w || y1 < 0 || y0 >= s->h) {
            q[0] = q[1] = q[2] = q[3] = s->clamp;
            return;
        }
    }
    q[0] = in_picture(s, x0, y0) ? pixel_at(s, x0, y0) : s->clamp;
    q[1] = in_picture(s, x1, y0) ? pixel_at(s, x1, y0) : s->clamp;
    q[2] = in_picture(s, x0, y1) ? pixel_at(s, x0, y1) : s->clamp;
    q[3] = in_picture(s, x1, y1) ? pixel_at(s, x1, y1) : s->clamp;
}

/* the four pixels blended with weights of 11 bits, each byte on its own. */
static UINT32 blend11(const UINT32 q[4], INT fx, INT fy)
{
    if (fx == 0 && fy == 0)
        return q[0];
    UINT32 out = 0;
    for (INT shift = 0; shift < 32; shift += 8) {
        INT a = (INT)((q[0] >> shift) & 255), b = (INT)((q[1] >> shift) & 255);
        INT c = (INT)((q[2] >> shift) & 255), d = (INT)((q[3] >> shift) & 255);
        INT top = (a << 11) + fx * (b - a), bottom = (c << 11) + fx * (d - c);
        INT r = (fy * bottom + (2048 - fy) * top + 0x200000) >> 22;
        out |= (UINT32)(r & 255) << shift;
    }
    return out;
}

static void run_bilinear(const Sampler *s, INT y, INT x0, INT x1, UINT32 *out)
{
    INT n = x1 - x0;
    float u, v, ue, ve;
    position_at(s, x0, y, &u, &v);
    position_at(s, x1, y, &ue, &ve);
    float du = (ue - u) / (float)n, dv = (ve - v) / (float)n;
    for (INT i = 0; i < n; i++) {
        INT ix = floor_int(u), iy = floor_int(v);
        INT fx = floor_int((u - (float)ix) * 2048.0f + 0.5f), fy = floor_int((v - (float)iy) * 2048.0f + 0.5f);
        UINT32 q[4];
        window_of(s, ix, iy, q);
        out[i] = blend11(q, fx, fy);
        u += du;
        v += dv;
    }
}

#ifdef HAVE_FIXED_BILINEAR
/* the same with 16.16 positions and weights of 8 bits, which is how the 32 bit library rounds. */
static UINT32 blend8(const UINT32 q[4], INT fx, INT fy)
{
    UINT32 out = 0;
    for (INT shift = 0; shift < 32; shift += 8) {
        INT a = (INT)((q[0] >> shift) & 255), b = (INT)((q[1] >> shift) & 255);
        INT c = (INT)((q[2] >> shift) & 255), d = (INT)((q[3] >> shift) & 255);
        INT top = a + (((b - a) * fx + 128) >> 8), bottom = c + (((d - c) * fx + 128) >> 8);
        INT r = top + (((bottom - top) * fy + 128) >> 8);
        out |= (UINT32)(r & 255) << shift;
    }
    return out;
}

static void run_bilinear16(const Sampler *s, INT y, INT x0, INT x1, UINT32 *out)
{
    INT u, v;
    if (s->inv.kind == 0) {
        u = x0 << 16;
        v = y << 16;
    } else if (s->fixed_matrix) {
        u = (INT)((INT64)x0 * s->f11 + (INT64)y * s->f21 + s->fdx);
        v = (INT)((INT64)x0 * s->f12 + (INT64)y * s->f22 + s->fdy);
    } else {
        float uf, vf;
        position_at(s, x0, y, &uf, &vf);
        u = fix16(uf);
        v = fix16(vf);
    }
    for (INT i = 0; i < x1 - x0; i++) {
        UINT32 q[4];
        window_of(s, u >> 16, v >> 16, q);
        out[i] = blend8(q, (u >> 8) & 255, (v >> 8) & 255);
        u = (INT)((UINT32)u + (UINT32)s->step_u);
        v = (INT)((UINT32)v + (UINT32)s->step_v);
    }
}
#endif

/* ------------------------------------------------------------------ bicubic */

static INT g_cubic[129];
static volatile LONG g_cubic_ready;

/* the weights of the cubic convolution with parameter -0.5 at k / 64 pixels from the center, as 16.16. */
static void cubic_prepare(void)
{
    if (g_cubic_ready)
        return;
    for (INT k = 0; k <= 128; k++) {
        double x = (double)k / 64.0, w;
        if (x < 1.0)
            w = 1.5 * x * x * x - 2.5 * x * x + 1.0;
        else
            w = -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
        double scaled = w * 65536.0 + 0.5;
        INT v = (INT)scaled;
        g_cubic[k] = (double)v > scaled ? v - 1 : v;
    }
    g_cubic_ready = 1;
}

/* four pixels in a line blended by the weights for a fraction of 64ths, the colors held to the alpha. */
static UINT32 blend_cubic(const UINT32 p[4], INT f)
{
    const INT wt[4] = {g_cubic[64 + f], g_cubic[f], g_cubic[64 - f], g_cubic[128 - f]};
    INT c[4];
    for (INT k = 0; k < 4; k++) {
        INT shift = 8 * k;
#ifdef __i386__
        /* the 32 bit library has the weights in 1/16384 and adds products that were cut to 16 bits */
        INT sum = 0;
        for (INT t = 0; t < 4; t++) {
            INT mag = wt[t] < 0 ? -wt[t] : wt[t];
            INT w16 = ((mag + 2) >> 2) * (wt[t] < 0 ? -1 : 1);
            sum += (((INT)((p[t] >> shift) & 255) * 32) * w16) >> 16;
        }
        c[k] = (sum + 4) >> 3;
#else
        INT sum = 0;
        for (INT t = 0; t < 4; t++)
            sum += wt[t] * (INT)((p[t] >> shift) & 255);
        c[k] = sum >> 16;
#endif
    }
    INT a = c[3] < 0 ? 0 : (c[3] > 255 ? 255 : c[3]);
    for (INT k = 0; k < 3; k++)
        c[k] = c[k] < 0 ? 0 : (c[k] > a ? a : c[k]);
    return (UINT32)c[0] | (UINT32)c[1] << 8 | (UINT32)c[2] << 16 | (UINT32)a << 24;
}

static void run_bicubic(const Sampler *s, INT y, INT x0, INT x1, UINT32 *out)
{
    INT u, v, du, dv;
    fixed_walk(s, y, x0, x1, &u, &v, &du, &dv);
    for (INT i = 0; i < x1 - x0; i++) {
        INT xs[4], ys[4];
        BOOL outside = FALSE;
        for (INT k = 0; k < 4; k++) {
            xs[k] = (u >> 16) + k - 1;
            ys[k] = (v >> 16) + k - 1;
            if (xs[k] < 0 || xs[k] >= s->w || ys[k] < 0 || ys[k] >= s->h)
                outside = TRUE;
        }
        if (outside && s->wrap != WRAP_CLAMP)
            for (INT k = 0; k < 4; k++)
                wrap_point(s->wrap, &xs[k], &ys[k], s->w, s->h);
        UINT32 cols[4];
        for (INT c = 0; c < 4; c++) {
            UINT32 col[4];
            for (INT r = 0; r < 4; r++)
                col[r] = in_picture(s, xs[c], ys[r]) ? pixel_at(s, xs[c], ys[r]) : s->clamp;
            cols[c] = blend_cubic(col, (v >> 10) & 63);
        }
        out[i] = blend_cubic(cols, (u >> 10) & 63);
        u = (INT)((UINT32)u + (UINT32)du);
        v = (INT)((UINT32)v + (UINT32)dv);
    }
}

/* ------------------------------------------------------------------ choosing and running */

#ifdef HAVE_FIXED_BILINEAR
static BOOL small_number(float v) { return v >= -32768.0f && v <= 32767.0f; }

/* the 16.16 form of the inverse matrix, false when its elements are too big for it. */
static BOOL fixed_prepare(Sampler *s)
{
    const ImgMat *m = &s->inv;
    if (!small_number(m->m11) || !small_number(m->m12) || !small_number(m->m21) || !small_number(m->m22))
        return FALSE;
    float su = 1.0f, sv = 0.0f;
    if (m->kind) {
        su = m->m11;
        sv = m->m12;
    }
    s->step_u = fix16(su);
    s->step_v = fix16(sv);
    s->fixed_matrix = small_number(m->dx) && small_number(m->dy);
    if (s->fixed_matrix) {
        s->f11 = fix16(m->m11);
        s->f12 = fix16(m->m12);
        s->f21 = fix16(m->m21);
        s->f22 = fix16(m->m22);
        s->fdx = fix16(m->dx);
        s->fdy = fix16(m->dy);
    }
    return TRUE;
}
#endif

BOOL sampler_create(Sampler *s, const SamplerSetup *u)
{
    memset(s, 0, sizeof(*s));
    if (u->w <= 0 || u->h <= 0)
        return FALSE;
    cubic_prepare();
    s->px = u->px;
    s->w = u->w;
    s->h = u->h;
    s->stride = u->stride;
    s->wrap = u->wrap;
    s->clamp = u->clamp;
    s->inv = u->fwd;
    if (!mat_invert(&s->inv))
        mat_identity(&s->inv);
    BOOL shift_only = mat_is_pixel_shift(&u->fwd);
    BOOL plain_scale = (u->fwd.kind & ~3u) == 0;
    BOOL bilinear_only = FALSE;

    if (!shift_only) {
        switch (u->mode) {
        case InterpolationModeBicubic:
            s->kind = SAMPLER_BICUBIC;
            return TRUE;
        case InterpolationModeNearestNeighbor:
            s->kind = SAMPLER_NEAREST;
            return TRUE;
        case InterpolationModeHighQualityBilinear:
            if (plain_scale) {
                s->stretch = stretch_create(u, FALSE);
                if (s->stretch) {
                    s->kind = SAMPLER_STRETCH;
                    return TRUE;
                }
                bilinear_only = TRUE;
            }
            break;
        case InterpolationModeHighQuality:
        case InterpolationModeHighQualityBicubic:
            if (!plain_scale) {
                s->kind = SAMPLER_BICUBIC;
                return TRUE;
            }
            s->stretch = stretch_create(u, TRUE);
            if (s->stretch) {
                s->kind = SAMPLER_STRETCH;
                return TRUE;
            }
            bilinear_only = TRUE;
            break;
        default:
            break;
        }
    }
    if (!bilinear_only) {
        if (shift_only && (s->wrap == 0 || s->wrap == WRAP_CLAMP)) {
            s->kind = SAMPLER_COPY;
            s->shift_x = -floor_int(u->fwd.dx + 0.5f);
            s->shift_y = -floor_int(u->fwd.dy + 0.5f);
            return TRUE;
        }
#ifdef HAVE_FIXED_BILINEAR
        BOOL flips = s->wrap >= 1 && s->wrap <= 3;
        INT limit = flips ? 16383 : 32767;
        if (s->w <= limit && s->h <= limit && fixed_prepare(s)) {
            s->kind = SAMPLER_BILINEAR16;
            return TRUE;
        }
#endif
    }
    s->kind = SAMPLER_BILINEAR;
    return TRUE;
}

void sampler_free(Sampler *s)
{
    stretch_free(s->stretch);
    s->stretch = nullptr;
}

void sampler_run(Sampler *s, INT y, INT x0, INT x1, UINT32 *out)
{
    if (x1 <= x0)
        return;
    switch (s->kind) {
    case SAMPLER_COPY:
        run_copy(s, y, x0, x1, out);
        break;
    case SAMPLER_NEAREST:
        run_nearest(s, y, x0, x1, out);
        break;
    case SAMPLER_BICUBIC:
        run_bicubic(s, y, x0, x1, out);
        break;
    case SAMPLER_STRETCH:
        stretch_run(s->stretch, y, x0, x1, out);
        break;
#ifdef HAVE_FIXED_BILINEAR
    case SAMPLER_BILINEAR16:
        run_bilinear16(s, y, x0, x1, out);
        break;
#endif
    default:
        run_bilinear(s, y, x0, x1, out);
        break;
    }
}
