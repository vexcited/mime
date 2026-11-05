#include "effects.h"
#include <mime/win32/wincodec.h>

static int iceil(float v) { return (int)D2dFloorF(v) + (D2dFloorF(v) < v ? 1 : 0); }
static int ifloor(float v) { return (int)D2dFloorF(v); }

/* ------------------------------------------------------------------ sampling */

/* keys' cubic: a of -0.5 is catmull-rom, high quality cubic sharpens it with the sharpness property. */
static float keys(float x, float a)
{
    x = D2dAbsF(x);
    if (x < 1.0f)
        return ((a + 2.0f) * x - (a + 3.0f)) * x * x + 1.0f;
    if (x < 2.0f)
        return (((x * a) - a * 5.0f) * x + a * 8.0f) * x - a * 4.0f;
    return 0.0f;
}

static const float *texel(const FxImg *im, int x, int y, BOOL clamp)
{
    static const float zero[4] = {};
    if (x < im->x || y < im->y || x >= im->x + im->w || y >= im->y + im->h) {
        if (!clamp || !im->w || !im->h)
            return zero;
        x = x < im->x ? im->x : (x >= im->x + im->w ? im->x + im->w - 1 : x);
        y = y < im->y ? im->y : (y >= im->y + im->h ? im->y + im->h - 1 : y);
    }
    return fx_px(im, x, y);
}

static void sample_cubic(const FxImg *im, float x, float y, float a, BOOL clamp, float *out)
{
    float u = x - 0.5f, v = y - 0.5f;
    int i0 = ifloor(u), j0 = ifloor(v);
    float fu = u - (float)i0, fv = v - (float)j0, wx[4], wy[4];
    for (int k = 0; k < 4; k++) {
        wx[k] = keys(fu - (float)(k - 1), a);
        wy[k] = keys(fv - (float)(k - 1), a);
    }
    for (int ch = 0; ch < 4; ch++)
        out[ch] = 0.0f;
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            float w = wx[i] * wy[j];
            if (w == 0.0f)
                continue;
            const float *p = texel(im, i0 - 1 + i, j0 - 1 + j, clamp);
            for (int ch = 0; ch < 4; ch++)
                out[ch] += p[ch] * w;
        }
    }
}

void FxSample(const FxImg *im, float x, float y, int mode, BOOL clamp, float *out)
{
    if (mode == FX_NEAREST) {
        memcpy(out, texel(im, ifloor(x), ifloor(y), clamp), 16);
        return;
    }
    if (mode == FX_CUBIC || mode == FX_HQCUBIC) {
        sample_cubic(im, x, y, -0.5f, clamp, out);
        return;
    }
    float u = x - 0.5f, v = y - 0.5f;
    int i0 = ifloor(u), j0 = ifloor(v);
    float fu = u - (float)i0, fv = v - (float)j0;
    const float *p00 = texel(im, i0, j0, clamp), *p10 = texel(im, i0 + 1, j0, clamp), *p01 = texel(im, i0, j0 + 1, clamp),
                *p11 = texel(im, i0 + 1, j0 + 1, clamp);
    for (int ch = 0; ch < 4; ch++)
        out[ch] = (p00[ch] * (1.0f - fu) + p10[ch] * fu) * (1.0f - fv) + (p01[ch] * (1.0f - fu) + p11[ch] * fu) * fv;
}

/* one axis of a resampling: the weights of the source pixels around each output pixel. */
static void resample_axis(const FxImg *src, FxImg *dst, float s, int mode, float a, BOOL vertical)
{
    int n_out = vertical ? dst->h : dst->w, o0 = vertical ? dst->y : dst->x;
    int n_in = vertical ? src->h : src->w, i0 = vertical ? src->y : src->x;
    int lines = vertical ? dst->w : dst->h;
    BOOL stretch = s < 1.0f && mode != FX_NEAREST && mode != FX_CUBIC;
    float support = mode == FX_NEAREST ? 0.5f : (mode == FX_CUBIC || mode == FX_HQCUBIC ? 2.0f : 1.0f);
    float k = stretch ? 1.0f / s : 1.0f;
    for (int o = 0; o < n_out; o++) {
        float center = ((float)(o0 + o) + 0.5f) / s - 0.5f;
        int lo, hi;
        if (mode == FX_NEAREST) {
            lo = hi = ifloor(center + 0.5f);
        } else {
            lo = iceil(center - support * k);
            hi = ifloor(center + support * k);
        }
        float wsum = 0.0f;
        float w[512];
        int count = hi - lo + 1;
        if (count > 512) {
            lo = ifloor(center) - 255;
            count = 512;
        }
        for (int t = 0; t < count; t++) {
            float d = ((float)(lo + t) - center) / k;
            float v;
            if (mode == FX_NEAREST)
                v = 1.0f;
            else if (mode == FX_CUBIC || mode == FX_HQCUBIC)
                v = keys(d, a);
            else
                v = D2dMaxF(0.0f, 1.0f - D2dAbsF(d));
            w[t] = v;
            wsum += v;
        }
        if (wsum == 0.0f)
            continue;
        for (int line = 0; line < lines; line++) {
            float acc[4] = {};
            for (int t = 0; t < count; t++) {
                int si = lo + t - i0;
                if (si < 0 || si >= n_in || w[t] == 0.0f)
                    continue;
                const float *p = vertical ? src->px + ((SIZE_T)si * src->w + line) * 4 : src->px + ((SIZE_T)line * src->w + si) * 4;
                for (int ch = 0; ch < 4; ch++)
                    acc[ch] += p[ch] * w[t];
            }
            float *d = vertical ? dst->px + ((SIZE_T)o * dst->w + line) * 4 : dst->px + ((SIZE_T)line * dst->w + o) * 4;
            for (int ch = 0; ch < 4; ch++)
                d[ch] = acc[ch] / (stretch ? wsum : 1.0f);
        }
    }
}

static HRESULT scale_axes(const FxImg *src, float sx, float sy, int mode, float a, FxImg *out)
{
    FxRect r = {ifloor((float)src->x * sx), ifloor((float)src->y * sy), iceil((float)(src->x + src->w) * sx), iceil((float)(src->y + src->h) * sy)};
    if (mode != FX_NEAREST)
        r = fx_inflate(r, 1, 1);
    FxImg mid;
    FxRect mr = {r.x0, src->y, r.x1, src->y + src->h};
    HRESULT hr = FxNew(&mid, mr);
    if (FAILED(hr))
        return hr;
    if (mid.px && src->px)
        resample_axis(src, &mid, sx, mode, a, FALSE);
    hr = FxNew(out, r);
    if (SUCCEEDED(hr) && out->px && mid.px)
        resample_axis(&mid, out, sy, mode, a, TRUE);
    FxFree(&mid);
    return hr;
}

HRESULT FxScale(const FxImg *src, float sx, float sy, int mode, FxImg *out) { return scale_axes(src, sx, sy, mode, -0.5f, out); }

/* ------------------------------------------------------------------ projective transforms */

/* a mapping between the input and the output, both in device pixels: forward for bounds, inverse for sampling. */
typedef struct Xf {
    double fwd[3][3], inv[3][3];
    int mode;
    float sharpness;
    BOOL hard;
} Xf;

static void apply(const double m[3][3], double x, double y, double *ox, double *oy, double *ow)
{
    double X = x * m[0][0] + y * m[1][0] + m[2][0], Y = x * m[0][1] + y * m[1][1] + m[2][1], W = x * m[0][2] + y * m[1][2] + m[2][2];
    *ow = W;
    *ox = W != 0.0 ? X / W : 0.0;
    *oy = W != 0.0 ? Y / W : 0.0;
}

static BOOL invert3(double o[3][3], const double m[3][3])
{
    double d = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (d == 0.0 || d != d)
        return FALSE;
    double t[3][3] = {{(m[1][1] * m[2][2] - m[1][2] * m[2][1]) / d, (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / d, (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / d},
                      {(m[1][2] * m[2][0] - m[1][0] * m[2][2]) / d, (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / d, (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / d},
                      {(m[1][0] * m[2][1] - m[1][1] * m[2][0]) / d, (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / d, (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / d}};
    memcpy(o, t, sizeof(t));
    return TRUE;
}

typedef float Mat4[4][4];

static void mat4_mul(Mat4 o, const Mat4 a, const Mat4 b)
{
    Mat4 t;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
    memcpy(o, t, sizeof(t));
}

static void mat4_identity(Mat4 m)
{
    memset(m, 0, sizeof(Mat4));
    for (int i = 0; i < 4; i++)
        m[i][i] = 1.0f;
}

static void mat4_translate(Mat4 m, float x, float y, float z)
{
    mat4_identity(m);
    m[3][0] = x;
    m[3][1] = y;
    m[3][2] = z;
}

/* rotations about the axes as d2d's matrix helpers build them, in degrees. */
static void mat4_rotate(Mat4 m, int axis, float degrees)
{
    float s, c;
    D2dSinCosF((float)(degrees * 3.141592653589793 / 180.0), &s, &c);
    mat4_identity(m);
    if (axis == 0) {
        m[1][1] = c; m[1][2] = s; m[2][1] = -s; m[2][2] = c;
    } else if (axis == 1) {
        m[0][0] = c; m[0][2] = -s; m[2][0] = s; m[2][2] = c;
    } else {
        m[0][0] = c; m[0][1] = s; m[1][0] = -s; m[1][1] = c;
    }
}

/* the projective part of a 4 by 4 matrix taken on the plane z = 0, moved from dips to device pixels. */
static void xf_from_mat4(Xf *xf, const Mat4 m, float scale)
{
    double h[3][3] = {{m[0][0], m[0][1], m[0][3]}, {m[1][0], m[1][1], m[1][3]}, {m[3][0], m[3][1], m[3][3]}};
    /* pixels to dips, the matrix, dips to pixels. */
    for (int j = 0; j < 3; j++) {
        h[0][j] /= scale;
        h[1][j] /= scale;
    }
    for (int i = 0; i < 3; i++) {
        h[i][0] *= scale;
        h[i][1] *= scale;
    }
    memcpy(xf->fwd, h, sizeof(h));
}

static void xf_from_affine(Xf *xf, const float *m, float scale)
{
    Mat4 t;
    mat4_identity(t);
    t[0][0] = m[0];
    t[0][1] = m[1];
    t[1][0] = m[2];
    t[1][1] = m[3];
    t[3][0] = m[4];
    t[3][1] = m[5];
    xf_from_mat4(xf, t, scale);
}

static int interp_of(UINT32 v) { return v == 0 ? FX_NEAREST : v == 2 ? FX_CUBIC : v == 3 ? FX_MULTISAMPLE : v == 4 ? FX_ANISOTROPIC : v == 5 ? FX_HQCUBIC : FX_LINEAR; }

/* the mapping of the effects that are a matrix transform, false for the others. */
static BOOL effect_xf(FxCtx *c, Xf *xf)
{
    D2dEffect *e = c->e;
    memset(xf, 0, sizeof(*xf));
    Mat4 m, t;
    switch (e->id) {
    case EFF_AFFINE:
        xf->mode = interp_of(*D2dFxU(e, 0));
        xf->hard = *D2dFxU(e, 1) == FX_HARD;
        xf->sharpness = D2dFxF(e, 3)[0];
        xf_from_affine(xf, D2dFxF(e, 2), c->scale);
        break;
    case EFF_SCALE: {
        const float *s = D2dFxF(e, 0), *ctr = D2dFxF(e, 1);
        float a[6] = {s[0], 0, 0, s[1], ctr[0] - ctr[0] * s[0], ctr[1] - ctr[1] * s[1]};
        xf->mode = interp_of(*D2dFxU(e, 2));
        xf->hard = *D2dFxU(e, 3) == FX_HARD;
        xf->sharpness = D2dFxF(e, 4)[0];
        xf_from_affine(xf, a, c->scale);
        break;
    }
    case EFF_DPICOMPENSATION: {
        const float *dpi = D2dFxF(e, 2);
        float a[6] = {dpi[0] > 0 ? c->rt->dpi_x / dpi[0] : 1.0f, 0, 0, dpi[1] > 0 ? c->rt->dpi_y / dpi[1] : 1.0f, 0, 0};
        xf->mode = interp_of(*D2dFxU(e, 0));
        xf->hard = *D2dFxU(e, 1) == FX_HARD;
        xf_from_affine(xf, a, c->scale);
        break;
    }
    case EFF_3DTRANSFORM:
        xf->mode = interp_of(*D2dFxU(e, 0));
        xf->hard = *D2dFxU(e, 1) == FX_HARD;
        memcpy(m, D2dFxF(e, 2), sizeof(m));
        xf_from_mat4(xf, m, c->scale);
        break;
    case EFF_3DPERSPECTIVE: {
        float depth = D2dFxF(e, 2)[0];
        const float *po = D2dFxF(e, 3), *lo = D2dFxF(e, 4), *go = D2dFxF(e, 5), *ro = D2dFxF(e, 6), *rot = D2dFxF(e, 7);
        xf->mode = interp_of(*D2dFxU(e, 0));
        xf->hard = *D2dFxU(e, 1) == FX_HARD;
        mat4_translate(m, lo[0], lo[1], lo[2]);
        mat4_translate(t, -ro[0], -ro[1], -ro[2]);
        mat4_mul(m, m, t);
        for (int axis = 0; axis < 3; axis++) {
            mat4_rotate(t, axis, rot[axis]);
            mat4_mul(m, m, t);
        }
        mat4_translate(t, ro[0], ro[1], ro[2]);
        mat4_mul(m, m, t);
        mat4_translate(t, go[0], go[1], go[2]);
        mat4_mul(m, m, t);
        mat4_translate(t, -po[0], -po[1], 0);
        mat4_mul(m, m, t);
        mat4_identity(t);
        t[2][3] = depth > 0.0f ? -1.0f / depth : 0.0f;
        mat4_mul(m, m, t);
        mat4_translate(t, po[0], po[1], 0);
        mat4_mul(m, m, t);
        xf_from_mat4(xf, m, c->scale);
        break;
    }
    case EFF_STRAIGHTEN: {
        FxRect in;
        if (FAILED(FxInputBounds(c, 0, &in)) || fx_infinite(in) || fx_empty(in))
            return FALSE;
        float angle = D2dFxF(e, 0)[0];
        xf->mode = interp_of(*D2dFxU(e, 2));
        float w = (float)(in.x1 - in.x0), h = (float)(in.y1 - in.y0), cx = w * 0.5f + (float)in.x0, cy = h * 0.5f + (float)in.y0;
        float k = 1.0f;
        if (*D2dFxU(e, 1)) {
            float s, co, ratio = h <= w ? w / h : h / w;
            D2dSinCosF((float)(D2dAbsF(angle) * 3.141592653589793 / 180.0), &s, &co);
            k = s * ratio + co;
        }
        mat4_translate(m, -cx, -cy, 0);
        mat4_rotate(t, 2, angle);
        mat4_mul(m, m, t);
        mat4_identity(t);
        t[0][0] = t[1][1] = k;
        mat4_mul(m, m, t);
        mat4_translate(t, cx, cy, 0);
        mat4_mul(m, m, t);
        /* the rectangle of the input is in pixels already. */
        xf_from_mat4(xf, m, 1.0f);
        break;
    }
    default:
        return FALSE;
    }
    return invert3(xf->inv, xf->fwd);
}

/* the reach of the sampling filter outside the input, in input pixels. */
static float xf_reach(const Xf *xf) { return xf->hard ? 0.0f : (xf->mode == FX_NEAREST ? 0.0f : xf->mode == FX_CUBIC || xf->mode == FX_HQCUBIC ? 2.0f : 1.0f); }

static FxRect xf_bounds(const Xf *xf, FxRect in)
{
    if (fx_infinite(in))
        return fx_inf;
    if (fx_empty(in))
        return (FxRect){0, 0, 0, 0};
    float r = xf_reach(xf);
    double cx[4] = {in.x0 - r, in.x1 + r, in.x1 + r, in.x0 - r}, cy[4] = {in.y0 - r, in.y0 - r, in.y1 + r, in.y1 + r};
    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    for (int i = 0; i < 4; i++) {
        double x, y, w;
        apply(xf->fwd, cx[i], cy[i], &x, &y, &w);
        /* a corner behind the viewer lets the plane reach anywhere. */
        if (w <= 0.0)
            return fx_inf;
        x0 = x < x0 ? x : x0;
        y0 = y < y0 ? y : y0;
        x1 = x > x1 ? x : x1;
        y1 = y > y1 ? y : y1;
    }
    if (x0 < -D2D_INF_EXTENT || y0 < -D2D_INF_EXTENT || x1 > D2D_INF_EXTENT || y1 > D2D_INF_EXTENT)
        return fx_inf;
    return (FxRect){ifloor((float)x0 + 1e-3f), ifloor((float)y0 + 1e-3f), iceil((float)x1 - 1e-3f), iceil((float)y1 - 1e-3f)};
}

/* the source area output pixels read, with the filter's reach and a pixel of slack. */
static FxRect xf_source(const Xf *xf, FxRect area, FxRect in)
{
    double cx[4] = {area.x0, area.x1, area.x1, area.x0}, cy[4] = {area.y0, area.y0, area.y1, area.y1};
    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    for (int i = 0; i < 4; i++) {
        double x, y, w;
        apply(xf->inv, cx[i], cy[i], &x, &y, &w);
        if (w <= 0.0)
            return in;
        x0 = x < x0 ? x : x0;
        y0 = y < y0 ? y : y0;
        x1 = x > x1 ? x : x1;
        y1 = y > y1 ? y : y1;
    }
    if (x0 < -D2D_INF_EXTENT || y0 < -D2D_INF_EXTENT || x1 > D2D_INF_EXTENT || y1 > D2D_INF_EXTENT)
        return in;
    FxRect r = {ifloor((float)x0) - 3, ifloor((float)y0) - 3, iceil((float)x1) + 3, iceil((float)y1) + 3};
    return fx_intersect(r, in);
}

static void xf_point(const Xf *xf, double x, double y, double *sx, double *sy, double *w) { apply(xf->inv, x, y, sx, sy, w); }

static void xf_pixel(const Xf *xf, const FxImg *src, FxRect in, int X, int Y, float *out)
{
    double px = X + 0.5, py = Y + 0.5, sx, sy, w;
    xf_point(xf, px, py, &sx, &sy, &w);
    memset(out, 0, 16);
    if (w <= 0.0)
        return;
    if (xf->hard && (sx < in.x0 || sy < in.y0 || sx >= in.x1 || sy >= in.y1))
        return;
    int mode = xf->mode;
    float a = -0.5f - 1.5f * xf->sharpness;
    if (mode == FX_MULTISAMPLE) {
        static const float off[4][2] = {{0.375f, -0.125f}, {-0.125f, -0.375f}, {-0.375f, 0.125f}, {0.125f, 0.375f}};
        for (int k = 0; k < 4; k++) {
            double qx, qy, qw;
            float t[4];
            xf_point(xf, px + off[k][0], py + off[k][1], &qx, &qy, &qw);
            FxSample(src, (float)qx, (float)qy, FX_LINEAR, xf->hard, t);
            for (int ch = 0; ch < 4; ch++)
                out[ch] += t[ch] * 0.25f;
        }
        return;
    }
    if (mode == FX_ANISOTROPIC || mode == FX_HQCUBIC) {
        /* the footprint of the output pixel in the input: averaged over a grid when it covers several pixels. */
        double ax, ay, aw, bx, by, bw;
        xf_point(xf, px + 1.0, py, &ax, &ay, &aw);
        xf_point(xf, px, py + 1.0, &bx, &by, &bw);
        float fx = (float)D2dSqrtF((float)((ax - sx) * (ax - sx) + (ay - sy) * (ay - sy)));
        float fy = (float)D2dSqrtF((float)((bx - sx) * (bx - sx) + (by - sy) * (by - sy)));
        int nx = fx > 1.0f ? (int)D2dMinF(16.0f, (float)iceil(fx)) : 1, ny = fy > 1.0f ? (int)D2dMinF(16.0f, (float)iceil(fy)) : 1;
        float k = 1.0f / (float)(nx * ny);
        for (int j = 0; j < ny; j++) {
            for (int i = 0; i < nx; i++) {
                double qx, qy, qw;
                float t[4];
                xf_point(xf, px + ((double)i + 0.5) / nx - 0.5, py + ((double)j + 0.5) / ny - 0.5, &qx, &qy, &qw);
                if (mode == FX_HQCUBIC)
                    sample_cubic(src, (float)qx, (float)qy, a, xf->hard, t);
                else
                    FxSample(src, (float)qx, (float)qy, FX_LINEAR, xf->hard, t);
                for (int ch = 0; ch < 4; ch++)
                    out[ch] += t[ch] * k;
            }
        }
        return;
    }
    FxSample(src, (float)sx, (float)sy, mode, xf->hard, out);
}

static HRESULT xf_render(FxCtx *c, const Xf *xf, FxRect area, FxImg *out)
{
    FxRect in;
    HRESULT hr = FxInputBounds(c, 0, &in);
    if (FAILED(hr))
        return hr;
    FxImg src;
    hr = FxInput(c, 0, xf_source(xf, area, in), &src);
    if (SUCCEEDED(hr))
        hr = FxNew(out, area);
    if (SUCCEEDED(hr) && out->px && src.px) {
        for (int y = area.y0; y < area.y1; y++)
            for (int x = area.x0; x < area.x1; x++)
                xf_pixel(xf, &src, in, x, y, fx_px(out, x, y));
    }
    FxFree(&src);
    return hr;
}

/* ------------------------------------------------------------------ crop, border, tile, atlas */

static FxRect dip_rect(FxCtx *c, const float *v, BOOL round)
{
    float sx = c->scale, sy = c->rt->dpi_y / 96.0f;
    FxRect r;
    r.x0 = v[0] <= -1e30f ? -D2D_INF_EXTENT : round ? ifloor(v[0] * sx + 0.5f) : ifloor(v[0] * sx);
    r.y0 = v[1] <= -1e30f ? -D2D_INF_EXTENT : round ? ifloor(v[1] * sy + 0.5f) : ifloor(v[1] * sy);
    r.x1 = v[2] >= 1e30f ? D2D_INF_EXTENT : round ? ifloor(v[2] * sx + 0.5f) : iceil(v[2] * sx);
    r.y1 = v[3] >= 1e30f ? D2D_INF_EXTENT : round ? ifloor(v[3] * sy + 0.5f) : iceil(v[3] * sy);
    return r;
}

static float coverage(float lo, float hi, int p)
{
    float a = D2dMaxF(lo, (float)p), b = D2dMinF(hi, (float)p + 1.0f);
    return b > a ? b - a : 0.0f;
}

static HRESULT crop_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    HRESULT hr = FxInput(c, 0, area, out);
    if (FAILED(hr) || !out->px || *D2dFxU(e, 1) == FX_HARD)
        return hr;
    /* the soft border keeps the part of each edge pixel the rectangle covers. */
    const float *v = D2dFxF(e, 0);
    float sx = c->scale, sy = c->rt->dpi_y / 96.0f;
    float x0 = v[0] <= -1e30f ? -1e30f : v[0] * sx, y0 = v[1] <= -1e30f ? -1e30f : v[1] * sy;
    float x1 = v[2] >= 1e30f ? 1e30f : v[2] * sx, y1 = v[3] >= 1e30f ? 1e30f : v[3] * sy;
    for (int y = area.y0; y < area.y1; y++) {
        float cy = coverage(y0, y1, y);
        for (int x = area.x0; x < area.x1; x++) {
            float k = cy * coverage(x0, x1, x);
            if (k >= 1.0f)
                continue;
            float *p = fx_px(out, x, y);
            for (int ch = 0; ch < 4; ch++)
                p[ch] *= k;
        }
    }
    return S_OK;
}

static int edge_index(int i, int n, UINT32 mode)
{
    if (i >= 0 && i < n)
        return i;
    switch (mode) {
    case 1:
        return ((i % n) + n) % n;
    case 2: {
        int p = ((i % (2 * n)) + 2 * n) % (2 * n);
        return p < n ? p : 2 * n - 1 - p;
    }
    default:
        return i < 0 ? 0 : n - 1;
    }
}

static HRESULT repeat_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    FxRect src_r;
    HRESULT hr = FxInputBounds(c, 0, &src_r);
    if (FAILED(hr))
        return hr;
    if (e->id == EFF_TILE)
        src_r = fx_intersect(src_r, dip_rect(c, D2dFxF(e, 0), FALSE));
    /* an input without edges along an axis has nothing to extend there, so it shows as it is. */
    BOOL open_x = src_r.x0 <= -D2D_INF_EXTENT || src_r.x1 >= D2D_INF_EXTENT, open_y = src_r.y0 <= -D2D_INF_EXTENT || src_r.y1 >= D2D_INF_EXTENT;
    if (open_x && open_y)
        return FxInput(c, 0, area, out);
    if (open_x) {
        src_r.x0 = area.x0;
        src_r.x1 = area.x1;
    }
    if (open_y) {
        src_r.y0 = area.y0;
        src_r.y1 = area.y1;
    }
    hr = FxNew(out, area);
    if (FAILED(hr) || !out->px || fx_empty(src_r))
        return hr;
    FxImg src;
    hr = FxInput(c, 0, src_r, &src);
    if (FAILED(hr))
        return hr;
    UINT32 mx = e->id == EFF_TILE ? 1 : *D2dFxU(e, 0), my = e->id == EFF_TILE ? 1 : *D2dFxU(e, 1);
    for (int y = area.y0; y < area.y1; y++) {
        int sy = open_y ? y - src.y : edge_index(y - src.y, src.h, my);
        for (int x = area.x0; x < area.x1; x++) {
            int sx = open_x ? x - src.x : edge_index(x - src.x, src.w, mx);
            memcpy(fx_px(out, x, y), src.px + ((SIZE_T)sy * src.w + sx) * 4, 16);
        }
    }
    FxFree(&src);
    return S_OK;
}

/* ------------------------------------------------------------------ bitmap source */

/* the pixel of the source an oriented pixel shows: w and h are the size of the source. */
static void orient(UINT32 o, int u, int v, int w, int h, int *x, int *y)
{
    switch (o) {
    case 2: *x = w - 1 - u; *y = v; break;
    case 3: *x = w - 1 - u; *y = h - 1 - v; break;
    case 4: *x = u; *y = h - 1 - v; break;
    case 5: *x = v; *y = u; break;
    case 6: *x = v; *y = h - 1 - u; break;
    case 7: *x = w - 1 - v; *y = h - 1 - u; break;
    case 8: *x = w - 1 - v; *y = u; break;
    default: *x = u; *y = v; break;
    }
}

static void bitmapsource_size(FxCtx *c, IWICBitmapSource *s, UINT32 *w, UINT32 *h, float *kx, float *ky)
{
    D2dEffect *e = c->e;
    UINT32 sw = 0, sh = 0;
    s->lpVtbl->GetSize(s, &sw, &sh);
    UINT32 o = *D2dFxU(e, 5);
    if (o >= 5) {
        UINT32 t = sw;
        sw = sh;
        sh = t;
    }
    const float *scale = D2dFxF(e, 1);
    *kx = scale[0];
    *ky = scale[1];
    if (*D2dFxU(e, 3)) {
        double dx = 96.0, dy = 96.0;
        if (FAILED(s->lpVtbl->GetResolution(s, &dx, &dy)) || dx <= 0.0 || dy <= 0.0)
            dx = dy = 96.0;
        if (o >= 5) {
            double t = dx;
            dx = dy;
            dy = t;
        }
        *kx *= c->rt->dpi_x / (float)dx;
        *ky *= c->rt->dpi_y / (float)dy;
    }
    *w = (UINT32)((float)sw * *kx + 0.5f);
    *h = (UINT32)((float)sh * *ky + 0.5f);
    if (sw && !*w)
        *w = 1;
    if (sh && !*h)
        *h = 1;
}

static HRESULT bitmapsource_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    IWICBitmapSource *s = *(IWICBitmapSource **)e->props[0];
    HRESULT hr = FxNew(out, area);
    if (FAILED(hr) || !out->px || !s)
        return hr;
    UINT32 w, h, sw = 0, sh = 0;
    float kx, ky;
    bitmapsource_size(c, s, &w, &h, &kx, &ky);
    s->lpVtbl->GetSize(s, &sw, &sh);
    if (!sw || !sh)
        return S_OK;
    BOOL straight = *D2dFxU(e, 4) == FX_STRAIGHT;
    IWICImagingFactory *wf = nullptr;
    IWICFormatConverter *conv = nullptr;
    hr = CoCreateInstance(&CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&wf);
    if (SUCCEEDED(hr))
        hr = wf->lpVtbl->CreateFormatConverter(wf, &conv);
    if (SUCCEEDED(hr))
        hr = conv->lpVtbl->Initialize(conv, s, straight ? &GUID_WICPixelFormat32bppBGRA : &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                      nullptr, 0.0, WICBitmapPaletteTypeCustom);
    uint32_t *raw = SUCCEEDED(hr) ? D2dAlloc((SIZE_T)sw * sh * 4) : nullptr;
    if (SUCCEEDED(hr) && !raw)
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
        hr = conv->lpVtbl->CopyPixels(conv, nullptr, sw * 4, sw * sh * 4, (BYTE *)raw);
    if (conv)
        conv->lpVtbl->Release(conv);
    if (wf)
        wf->lpVtbl->Release(wf);
    if (FAILED(hr)) {
        D2dFree(raw);
        return hr;
    }
    /* oriented first, then scaled with the interpolation mode, as d2d's wic scalers do. */
    UINT32 o = *D2dFxU(e, 5);
    int ow = o >= 5 ? (int)sh : (int)sw, oh = o >= 5 ? (int)sw : (int)sh;
    FxImg full;
    hr = FxNew(&full, (FxRect){0, 0, ow, oh});
    if (SUCCEEDED(hr) && full.px) {
        for (int v = 0; v < oh; v++) {
            for (int u = 0; u < ow; u++) {
                int x, y;
                orient(o, u, v, (int)sw, (int)sh, &x, &y);
                uint32_t p = raw[(SIZE_T)y * sw + x];
                float *d = fx_px(&full, u, v);
                d[0] = ((p >> 16) & 255) / 255.0f;
                d[1] = ((p >> 8) & 255) / 255.0f;
                d[2] = (p & 255) / 255.0f;
                d[3] = (p >> 24) / 255.0f;
            }
        }
    }
    D2dFree(raw);
    if (FAILED(hr))
        return hr;
    FxImg scaled = full;
    BOOL resized = (int)w != ow || (int)h != oh;
    if (resized) {
        UINT32 m = *D2dFxU(e, 2);
        /* fant averages areas, mipmap linear reads a box filtered level: both read as an area filter here. */
        int mode = m == 0 ? FX_NEAREST : m == 2 ? FX_CUBIC : m == 1 ? FX_LINEAR : FX_ANISOTROPIC;
        hr = FxScale(&full, (float)w / (float)ow, (float)h / (float)oh, mode, &scaled);
        FxFree(&full);
        if (FAILED(hr))
            return hr;
    }
    FxRect have = fx_intersect(area, fx_intersect(fx_rect(&scaled), (FxRect){0, 0, (int)w, (int)h}));
    for (int y = have.y0; y < have.y1; y++)
        memcpy(fx_px(out, have.x0, y), fx_px(&scaled, have.x0, y), (SIZE_T)(have.x1 - have.x0) * 16);
    FxFree(&scaled);
    return S_OK;
}

/* ------------------------------------------------------------------ ycbcr */

static HRESULT ycbcr_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    FxRect yb, cb;
    HRESULT hr = FxInputBounds(c, 0, &yb);
    if (SUCCEEDED(hr))
        hr = FxInputBounds(c, 1, &cb);
    if (FAILED(hr))
        return hr;
    UINT32 sub = *D2dFxU(e, 0);
    float fx = 1.0f, fy = 1.0f;
    if (sub == 0 && !fx_empty(cb) && !fx_infinite(cb) && !fx_infinite(yb)) {
        fx = (float)(yb.x1 - yb.x0) / (float)(cb.x1 - cb.x0);
        fy = (float)(yb.y1 - yb.y0) / (float)(cb.y1 - cb.y0);
    } else if (sub == 1) {
        fx = fy = 2.0f;
    } else if (sub == 2) {
        fx = 2.0f;
    } else if (sub == 4) {
        fy = 2.0f;
    }
    /* the planes in the space of the luma plane, the transform of the effect after them. */
    float a[6];
    memcpy(a, D2dFxF(e, 1), sizeof(a));
    Xf xf = {};
    xf_from_affine(&xf, a, c->scale);
    if (!invert3(xf.inv, xf.fwd))
        return FxNew(out, area);
    int mode = interp_of(*D2dFxU(e, 2));
    FxImg yi, ci;
    FxRect ys = xf_source(&xf, area, yb);
    hr = FxInput(c, 0, ys, &yi);
    if (FAILED(hr))
        return hr;
    FxRect cs = {ifloor((float)ys.x0 / fx) - 2, ifloor((float)ys.y0 / fy) - 2, iceil((float)ys.x1 / fx) + 2, iceil((float)ys.y1 / fy) + 2};
    hr = FxInput(c, 1, fx_intersect(cs, cb), &ci);
    if (SUCCEEDED(hr))
        hr = FxNew(out, area);
    if (SUCCEEDED(hr) && out->px && yi.px) {
        for (int y = area.y0; y < area.y1; y++) {
            for (int x = area.x0; x < area.x1; x++) {
                double sx, sy, w;
                apply(xf.inv, x + 0.5, y + 0.5, &sx, &sy, &w);
                float yv[4], cv[4] = {0.5f, 0.5f, 0, 0};
                FxSample(&yi, (float)sx, (float)sy, mode, TRUE, yv);
                if (ci.px)
                    FxSample(&ci, (float)sx / fx, (float)sy / fy, mode, TRUE, cv);
                float Y = yv[0], u = cv[0] - 0.501960814f, v = cv[1] - 0.501960814f;
                float *o = fx_px(out, x, y);
                o[0] = D2dClampF(Y + 1.402f * v, 0.0f, 1.0f);
                o[1] = D2dClampF(Y - 0.344139993f * u - 0.714139998f * v, 0.0f, 1.0f);
                o[2] = D2dClampF(Y + 1.772f * u, 0.0f, 1.0f);
                o[3] = 1.0f;
            }
        }
    }
    FxFree(&ci);
    FxFree(&yi);
    return hr;
}

/* ------------------------------------------------------------------ dispatch */

HRESULT FxTransformBounds(FxCtx *c, FxRect *b)
{
    D2dEffect *e = c->e;
    Xf xf;
    if (e->id == EFF_BITMAPSOURCE) {
        IWICBitmapSource *s = *(IWICBitmapSource **)e->props[0];
        *b = (FxRect){0, 0, 0, 0};
        if (s) {
            UINT32 w, h;
            float kx, ky;
            bitmapsource_size(c, s, &w, &h, &kx, &ky);
            b->x1 = (int)w;
            b->y1 = (int)h;
        }
        return S_OK;
    }
    FxRect in;
    HRESULT hr = FxInputBounds(c, 0, &in);
    if (FAILED(hr))
        return hr;
    switch (e->id) {
    case EFF_CROP:
        *b = fx_intersect(in, dip_rect(c, D2dFxF(e, 0), *D2dFxU(e, 1) == FX_HARD));
        return S_OK;
    case EFF_BORDER:
    case EFF_TILE:
        *b = fx_inf;
        return S_OK;
    case EFF_ATLAS:
        *b = fx_intersect(in, dip_rect(c, D2dFxF(e, 0), FALSE));
        return S_OK;
    case EFF_YCBCR: {
        Xf y = {};
        xf_from_affine(&y, D2dFxF(e, 1), c->scale);
        y.mode = interp_of(*D2dFxU(e, 2));
        y.hard = TRUE;
        *b = xf_bounds(&y, in);
        return S_OK;
    }
    case EFF_STRAIGHTEN:
        *b = in;
        return S_OK;
    default:
        break;
    }
    if (!effect_xf(c, &xf)) {
        *b = (FxRect){0, 0, 0, 0};
        return S_OK;
    }
    *b = xf_bounds(&xf, in);
    return S_OK;
}

HRESULT FxTransformRender(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    switch (e->id) {
    case EFF_CROP:
        return crop_render(c, area, out);
    case EFF_BORDER:
    case EFF_TILE:
        return repeat_render(c, area, out);
    case EFF_ATLAS: {
        FxRect pad = dip_rect(c, D2dFxF(e, 1), FALSE);
        HRESULT hr = FxInput(c, 0, area, out);
        if (SUCCEEDED(hr) && out->px) {
            for (int y = area.y0; y < area.y1; y++)
                for (int x = area.x0; x < area.x1; x++)
                    if (x < pad.x0 || y < pad.y0 || x >= pad.x1 || y >= pad.y1)
                        memset(fx_px(out, x, y), 0, 16);
        }
        return hr;
    }
    case EFF_BITMAPSOURCE:
        return bitmapsource_render(c, area, out);
    case EFF_YCBCR:
        return ycbcr_render(c, area, out);
    default: {
        Xf xf;
        if (!effect_xf(c, &xf))
            return FxNew(out, area);
        return xf_render(c, &xf, area, out);
    }
    }
}
