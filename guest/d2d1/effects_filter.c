#include "effects.h"

static inline float sat(float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; }

static inline void unpremul(const float *p, float *c)
{
    if (p[3] == 0.0f) {
        c[0] = c[1] = c[2] = 0.0f;
    } else {
        c[0] = p[0] / p[3];
        c[1] = p[1] / p[3];
        c[2] = p[2] / p[3];
    }
    c[3] = p[3];
}

static int iceil(float v) { return (int)D2dFloorF(v) + (D2dFloorF(v) < v ? 1 : 0); }

/* ------------------------------------------------------------------ gaussian kernels */

/*
 * d2d's gaussian: taps in pairs from the center out to an odd offset that covers sigma times the range of the
 * optimization, weighted by the gaussian at whole pixels and normalized to one.
 */
static int gauss_kernel(float sigma, float range, float **out)
{
    int span = (int)D2dMaxF(1.0f, (float)iceil(sigma * range));
    int pairs = iceil((float)span * 0.5f), r = pairs * 2 - 1;
    float *k = D2dAlloc((SIZE_T)(r + 1) * sizeof(float));
    if (!k)
        return -1;
    float total = 0.0f, two = 2.0f * sigma * sigma;
    for (int i = 0; i <= r; i++) {
        k[i] = D2dExpF(-(float)(i * i) / two);
        total += i ? 2.0f * k[i] : k[i];
    }
    for (int i = 0; i <= r; i++)
        k[i] /= total;
    *out = k;
    return r;
}

static void blur_axis(FxImg *im, const float *k, int r, BOOL vertical, BOOL hard, float *tmp)
{
    int n = vertical ? im->h : im->w, lines = vertical ? im->w : im->h;
    SIZE_T step = vertical ? (SIZE_T)im->w * 4 : 4;
    for (int line = 0; line < lines; line++) {
        float *base = vertical ? im->px + (SIZE_T)line * 4 : im->px + (SIZE_T)line * im->w * 4;
        for (int i = 0; i < n; i++) {
            float a[4] = {};
            for (int j = -r; j <= r; j++) {
                int t = i + j;
                if (t < 0 || t >= n) {
                    if (!hard)
                        continue;
                    t = t < 0 ? 0 : n - 1;
                }
                float w = k[j < 0 ? -j : j];
                const float *p = base + (SIZE_T)t * step;
                a[0] += p[0] * w;
                a[1] += p[1] * w;
                a[2] += p[2] * w;
                a[3] += p[3] * w;
            }
            memcpy(tmp + (SIZE_T)i * 4, a, sizeof(a));
        }
        for (int i = 0; i < n; i++)
            memcpy(base + (SIZE_T)i * step, tmp + (SIZE_T)i * 4, 16);
    }
}

void FxGaussian(FxImg *im, float sigma_x, float sigma_y, float range, BOOL hard)
{
    if (!im->px)
        return;
    float *tmp = D2dAlloc((SIZE_T)(im->w > im->h ? im->w : im->h) * 16);
    if (!tmp)
        return;
    float *k;
    int r;
    if (sigma_x > 0.0f && (r = gauss_kernel(sigma_x, range, &k)) >= 0) {
        blur_axis(im, k, r, FALSE, hard, tmp);
        D2dFree(k);
    }
    if (sigma_y > 0.0f && (r = gauss_kernel(sigma_y, range, &k)) >= 0) {
        blur_axis(im, k, r, TRUE, hard, tmp);
        D2dFree(k);
    }
    D2dFree(tmp);
}

/* the reach of a blur kernel in pixels. */
static int gauss_reach(float sigma, float range)
{
    if (!(sigma > 0.0f))
        return 0;
    int span = (int)D2dMaxF(1.0f, (float)iceil(sigma * range));
    return iceil((float)span * 0.5f) * 2;
}

/* the kernel range of each optimization: speed, balanced and quality. */
static const float g_blur_range[3] = {2.5f, 2.5f, 3.0f};

/* ------------------------------------------------------------------ blur and shadow */

static void blur_params(FxCtx *c, float *sigma, float *range, BOOL *hard)
{
    D2dEffect *e = c->e;
    *sigma = D2dFxF(e, 0)[0] * c->scale;
    UINT32 opt = e->id == EFF_SHADOW ? *D2dFxU(e, 2) : *D2dFxU(e, 1);
    *range = g_blur_range[opt > 2 ? 1 : opt];
    *hard = e->id == EFF_BLUR && *D2dFxU(e, 2) == FX_HARD;
}

/* a large blur runs on an image scaled down by the factor windows' balanced optimization picks. */
static float blur_reduction(float sigma) { return D2dClampF(8.0f / (sigma + 6.0f), 0.1f, 1.0f); }

static HRESULT blur_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    float sigma, range;
    BOOL hard;
    blur_params(c, &sigma, &range, &hard);
    FxRect in_b;
    HRESULT hr = FxInputBounds(c, 0, &in_b);
    if (FAILED(hr))
        return hr;
    int reach = gauss_reach(sigma, range) + 2;
    FxRect want = fx_inflate(area, reach, reach);
    if (hard)
        want = fx_intersect(want, in_b);
    FxImg src;
    hr = FxInput(c, 0, want, &src);
    if (FAILED(hr))
        return hr;
    if (e->id == EFF_SHADOW && src.px) {
        const float *col = D2dFxF(e, 1);
        for (SIZE_T i = 0; i < (SIZE_T)src.w * src.h; i++) {
            float *p = src.px + i * 4, a = p[3] * col[3];
            p[0] = col[0] * a;
            p[1] = col[1] * a;
            p[2] = col[2] * a;
            p[3] = a;
        }
    }
    float s = blur_reduction(sigma);
    if (s > 0.5f || !src.px) {
        FxGaussian(&src, sigma, sigma, range, hard);
    } else {
        FxImg small;
        hr = FxScale(&src, s, s, FX_MULTISAMPLE, &small);
        if (SUCCEEDED(hr)) {
            /* the reduced image starts at the scaled origin of the source, its edges held when hard. */
            FxGaussian(&small, sigma * s, sigma * s, range, hard);
            float *px = src.px;
            for (int y = src.y; y < src.y + src.h; y++) {
                for (int x = src.x; x < src.x + src.w; x++, px += 4)
                    FxSample(&small, ((float)x + 0.5f) * s, ((float)y + 0.5f) * s, FX_LINEAR, TRUE, px);
            }
            FxFree(&small);
        }
    }
    if (SUCCEEDED(hr))
        hr = FxNew(out, area);
    if (SUCCEEDED(hr) && out->px && src.px) {
        FxRect have = fx_intersect(area, fx_rect(&src));
        for (int y = have.y0; y < have.y1; y++)
            memcpy(fx_px(out, have.x0, y), fx_px(&src, have.x0, y), (SIZE_T)(have.x1 - have.x0) * 16);
    }
    FxFree(&src);
    return hr;
}

/* ------------------------------------------------------------------ directional blur */

static void dirblur_vector(FxCtx *c, float *dx, float *dy, float *sigma)
{
    D2dEffect *e = c->e;
    *sigma = D2dFxF(e, 0)[0] * c->scale;
    float sn, cs;
    D2dSinCosF((float)(D2dFxF(e, 1)[0] * 3.141592653589793 / 180.0), &sn, &cs);
    /* counterclockwise on screen, y down. */
    *dx = cs;
    *dy = -sn;
}

static HRESULT dirblur_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    float dx, dy, sigma;
    dirblur_vector(c, &dx, &dy, &sigma);
    UINT32 opt = *D2dFxU(e, 2);
    float range = g_blur_range[opt > 2 ? 1 : opt];
    BOOL hard = *D2dFxU(e, 3) == FX_HARD;
    FxRect in_b;
    HRESULT hr = FxInputBounds(c, 0, &in_b);
    if (FAILED(hr))
        return hr;
    float *k;
    int r = sigma > 0.0f ? gauss_kernel(sigma, range, &k) : 0;
    if (r < 0)
        return E_OUTOFMEMORY;
    if (!r)
        return FxInput(c, 0, area, out);
    int mx = iceil(D2dAbsF(dx) * (float)r) + 2, my = iceil(D2dAbsF(dy) * (float)r) + 2;
    FxRect want = fx_inflate(area, mx, my);
    if (hard)
        want = fx_intersect(want, in_b);
    FxImg src;
    hr = FxInput(c, 0, want, &src);
    if (SUCCEEDED(hr))
        hr = FxNew(out, area);
    if (SUCCEEDED(hr) && out->px && src.px) {
        for (int y = area.y0; y < area.y1; y++) {
            for (int x = area.x0; x < area.x1; x++) {
                float a[4] = {}, t[4];
                for (int j = -r; j <= r; j++) {
                    FxSample(&src, (float)x + 0.5f + dx * (float)j, (float)y + 0.5f + dy * (float)j, FX_LINEAR, hard, t);
                    float w = k[j < 0 ? -j : j];
                    for (int ch = 0; ch < 4; ch++)
                        a[ch] += t[ch] * w;
                }
                memcpy(fx_px(out, x, y), a, sizeof(a));
            }
        }
    }
    D2dFree(k);
    FxFree(&src);
    return hr;
}

/* ------------------------------------------------------------------ convolution */

typedef struct Tap {
    float x, y, w;
} Tap;

/* the taps of the kernel: flipped in both axes as a convolution, centered with the kernel offset added. */
static int convolve_taps(D2dEffect *e, Tap **out, float *ext)
{
    UINT32 sx = *D2dFxU(e, 2), sy = *D2dFxU(e, 3);
    const EffBlob *kb = D2dFxB(e, 4);
    const float *km = (const float *)kb->data;
    UINT32 count = kb->size / 4;
    float div = D2dFxF(e, 5)[0];
    if (div == 0.0f)
        div = 1.0f;
    const float *off = D2dFxF(e, 7);
    Tap *t = D2dAlloc((SIZE_T)sx * sy * sizeof(Tap) + sizeof(Tap));
    if (!t)
        return -1;
    int n = 0;
    float ox = off[0] - ((float)sx * 0.5f - 0.5f), oy = off[1] - ((float)sy * 0.5f - 0.5f);
    *ext = D2dMaxF(D2dAbsF(off[0]), D2dAbsF(off[1]));
    for (UINT32 x = 0; x < sx; x++) {
        for (UINT32 y = 0; y < sy; y++) {
            UINT32 i = sx + sx * (sy - 1 - y) - x - 1;
            if (i >= count || km[i] == 0.0f)
                continue;
            t[n++] = (Tap){(float)x + ox, (float)y + oy, km[i] / div};
            *ext = D2dMaxF(*ext, D2dMaxF(D2dAbsF((float)x + ox), D2dAbsF((float)y + oy)));
        }
    }
    *out = t;
    return n;
}

static int scale_mode(UINT32 m) { return m == 0 ? FX_NEAREST : m == 2 ? FX_CUBIC : m == 3 ? FX_MULTISAMPLE : m == 4 ? FX_ANISOTROPIC : m == 5 ? FX_HQCUBIC : FX_LINEAR; }

static HRESULT convolve_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    const float *kul = D2dFxF(e, 0);
    int mode = scale_mode(*D2dFxU(e, 1));
    float bias = D2dFxF(e, 6)[0];
    BOOL preserve = *D2dFxU(e, 8) != 0, hard = *D2dFxU(e, 9) == FX_HARD, clamp = *D2dFxU(e, 10) != 0;
    Tap *taps;
    float ext;
    int n = convolve_taps(e, &taps, &ext);
    if (n < 0)
        return E_OUTOFMEMORY;
    FxRect in_b;
    HRESULT hr = FxInputBounds(c, 0, &in_b);
    float ux = kul[0], uy = kul[1];
    int mx = iceil(ext * ux) + 2, my = iceil(ext * uy) + 2;
    FxRect want = fx_inflate(area, mx, my);
    if (hard)
        want = fx_intersect(want, in_b);
    FxImg src = {}, work = {};
    if (SUCCEEDED(hr))
        hr = FxInput(c, 0, want, &src);
    if (SUCCEEDED(hr) && preserve && src.px) {
        for (SIZE_T i = 0; i < (SIZE_T)src.w * src.h; i++) {
            float q[4];
            unpremul(src.px + i * 4, q);
            memcpy(src.px + i * 4, q, sizeof(q));
        }
    }
    /* a kernel unit longer than a pixel spreads the kernel: the work happens on the image scaled down. */
    BOOL unit = ux != 1.0f || uy != 1.0f;
    if (SUCCEEDED(hr) && unit && src.px) {
        hr = FxScale(&src, 1.0f / ux, 1.0f / uy, mode, &work);
    } else if (SUCCEEDED(hr)) {
        work = src;
        src.px = nullptr;
    }
    FxImg conv = {};
    if (SUCCEEDED(hr)) {
        FxRect r = unit ? fx_rect(&work) : area;
        hr = FxNew(&conv, r);
    }
    if (SUCCEEDED(hr) && conv.px && work.px) {
        for (int y = conv.y; y < conv.y + conv.h; y++) {
            for (int x = conv.x; x < conv.x + conv.w; x++) {
                float a[4] = {}, t[4];
                for (int i = 0; i < n; i++) {
                    FxSample(&work, (float)x + 0.5f + taps[i].x, (float)y + 0.5f + taps[i].y, FX_LINEAR, hard, t);
                    for (int ch = 0; ch < 4; ch++)
                        a[ch] += t[ch] * taps[i].w;
                }
                float *o = fx_px(&conv, x, y);
                if (preserve) {
                    const float *p = fx_px(&work, x, y);
                    a[3] = (x >= work.x && y >= work.y && x < work.x + work.w && y < work.y + work.h) ? p[3] : 0.0f;
                }
                for (int ch = 0; ch < 4; ch++) {
                    float v = ch == 3 && preserve ? a[3] : a[ch] + bias;
                    o[ch] = clamp ? sat(v) : v;
                }
                if (preserve) {
                    o[0] *= o[3];
                    o[1] *= o[3];
                    o[2] *= o[3];
                }
            }
        }
    }
    FxFree(&work);
    FxFree(&src);
    D2dFree(taps);
    if (FAILED(hr)) {
        FxFree(&conv);
        return hr;
    }
    if (!unit) {
        *out = conv;
        return S_OK;
    }
    FxImg back;
    hr = FxScale(&conv, ux, uy, mode, &back);
    FxFree(&conv);
    if (SUCCEEDED(hr))
        hr = FxNew(out, area);
    if (SUCCEEDED(hr) && out->px && back.px) {
        FxRect have = fx_intersect(area, fx_rect(&back));
        for (int y = have.y0; y < have.y1; y++)
            memcpy(fx_px(out, have.x0, y), fx_px(&back, have.x0, y), (SIZE_T)(have.x1 - have.x0) * 16);
    }
    FxFree(&back);
    return hr;
}

/* ------------------------------------------------------------------ morphology */

static void morph_size(FxCtx *c, int *w, int *h)
{
    D2dEffect *e = c->e;
    *w = (int)((float)*D2dFxU(e, 1) * c->scale + 0.5f);
    *h = (int)((float)*D2dFxU(e, 2) * c->rt->dpi_y / 96.0f + 0.5f);
    if (*w < 1)
        *w = 1;
    if (*h < 1)
        *h = 1;
}

static void morph_axis(FxImg *im, int size, BOOL vertical, BOOL dilate, float *tmp)
{
    int n = vertical ? im->h : im->w, lines = vertical ? im->w : im->h, lo = -(size >> 1), hi = lo + size - 1;
    SIZE_T step = vertical ? (SIZE_T)im->w * 4 : 4;
    for (int line = 0; line < lines; line++) {
        float *base = vertical ? im->px + (SIZE_T)line * 4 : im->px + (SIZE_T)line * im->w * 4;
        for (int i = 0; i < n; i++) {
            float m[4];
            for (int ch = 0; ch < 4; ch++)
                m[ch] = dilate ? 0.0f : 1e30f;
            for (int j = lo; j <= hi; j++) {
                int t = i + j;
                const float zero[4] = {};
                const float *p = t < 0 || t >= n ? zero : base + (SIZE_T)t * step;
                for (int ch = 0; ch < 4; ch++)
                    m[ch] = dilate ? D2dMaxF(m[ch], p[ch]) : D2dMinF(m[ch], p[ch]);
            }
            memcpy(tmp + (SIZE_T)i * 4, m, sizeof(m));
        }
        for (int i = 0; i < n; i++)
            memcpy(base + (SIZE_T)i * step, tmp + (SIZE_T)i * 4, 16);
    }
}

static HRESULT morph_render(FxCtx *c, FxRect area, FxImg *out)
{
    int w, h;
    morph_size(c, &w, &h);
    BOOL dilate = *D2dFxU(c->e, 0) == FX_DILATE;
    FxImg src;
    HRESULT hr = FxInput(c, 0, fx_inflate(area, w, h), &src);
    if (FAILED(hr))
        return hr;
    if (src.px && (w > 1 || h > 1)) {
        float *tmp = D2dAlloc((SIZE_T)(src.w > src.h ? src.w : src.h) * 16);
        if (tmp) {
            if (w > 1)
                morph_axis(&src, w, FALSE, dilate, tmp);
            if (h > 1)
                morph_axis(&src, h, TRUE, dilate, tmp);
            D2dFree(tmp);
        }
    }
    hr = FxNew(out, area);
    if (SUCCEEDED(hr) && out->px && src.px) {
        for (int y = area.y0; y < area.y1; y++)
            memcpy(fx_px(out, area.x0, y), fx_px(&src, area.x0, y), (SIZE_T)(area.x1 - area.x0) * 16);
    }
    FxFree(&src);
    return hr;
}

/* ------------------------------------------------------------------ sharpen */

static inline float luma(const float *c) { return c[0] * 0.299f + c[1] * 0.587f + c[2] * 0.114f; }

/* straight colors of an image, transparent black outside it. */
static void unpremul_image(FxImg *im)
{
    for (SIZE_T i = 0; i < (SIZE_T)im->w * im->h; i++) {
        float q[4];
        unpremul(im->px + i * 4, q);
        memcpy(im->px + i * 4, q, sizeof(q));
    }
}

static const float *at_or_zero(const FxImg *im, int x, int y)
{
    static const float zero[4] = {};
    if (x < im->x || y < im->y || x >= im->x + im->w || y >= im->y + im->h)
        return zero;
    return fx_px(im, x, y);
}

static HRESULT sharpen_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    float sharp = D2dFxF(e, 0)[0], thr = D2dFxF(e, 1)[0];
    if (sharp < 1.1920929e-7f)
        return FxInput(c, 0, area, out);
    FxImg src;
    HRESULT hr = FxInput(c, 0, fx_inflate(area, 1, 1), &src);
    if (FAILED(hr))
        return hr;
    if (src.px)
        unpremul_image(&src);
    hr = FxNew(out, area);
    if (SUCCEEDED(hr) && out->px && src.px) {
        for (int y = area.y0; y < area.y1; y++) {
            for (int x = area.x0; x < area.x1; x++) {
                const float *ctr = fx_px(&src, x, y);
                float b[3];
                for (int ch = 0; ch < 3; ch++) {
                    float edge = at_or_zero(&src, x - 1, y)[ch] + at_or_zero(&src, x + 1, y)[ch] + at_or_zero(&src, x, y - 1)[ch] +
                                 at_or_zero(&src, x, y + 1)[ch];
                    float corner = at_or_zero(&src, x - 1, y - 1)[ch] + at_or_zero(&src, x + 1, y - 1)[ch] + at_or_zero(&src, x - 1, y + 1)[ch] +
                                   at_or_zero(&src, x + 1, y + 1)[ch];
                    b[ch] = ctr[ch] * 0.36f + corner * 0.039731f + edge * 0.120269f;
                }
                float d = luma(ctr) - luma(b), k = sharp * d;
                if (thr > 0.0f && D2dAbsF(d) < thr)
                    k *= D2dAbsF(d) / thr;
                float *o = fx_px(out, x, y);
                for (int ch = 0; ch < 3; ch++)
                    o[ch] = sat(ctr[ch] + k) * ctr[3];
                o[3] = ctr[3];
            }
        }
    }
    FxFree(&src);
    return hr;
}

/* ------------------------------------------------------------------ edge detection */

static HRESULT edge_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    float strength = D2dFxF(e, 0)[0], radius = D2dFxF(e, 1)[0] * c->scale;
    BOOL prewitt = *D2dFxU(e, 2) == FX_PREWITT, overlay = *D2dFxU(e, 3) != 0;
    BOOL premul = *D2dFxU(e, 4) != FX_STRAIGHT;
    if (strength < 1.1920929e-7f && overlay)
        return FxInput(c, 0, area, out);
    FxRect in_b;
    HRESULT hr = FxInputBounds(c, 0, &in_b);
    if (FAILED(hr))
        return hr;
    int reach = gauss_reach(radius, 2.5f) + 2;
    FxImg src;
    hr = FxInput(c, 0, fx_intersect(fx_inflate(area, reach, reach), in_b), &src);
    if (FAILED(hr))
        return hr;
    FxImg orig = {};
    if (overlay && src.px) {
        hr = FxNew(&orig, fx_rect(&src));
        if (SUCCEEDED(hr) && orig.px)
            memcpy(orig.px, src.px, (SIZE_T)src.w * src.h * 16);
    }
    if (src.px && premul)
        unpremul_image(&src);
    if (src.px && radius > 1.1920929e-7f)
        FxGaussian(&src, radius, radius, 2.5f, TRUE);
    if (SUCCEEDED(hr))
        hr = FxNew(out, area);
    if (SUCCEEDED(hr) && out->px && src.px) {
        float side = prewitt ? 1.0f : 2.0f;
        FxRect have = fx_intersect(area, fx_rect(&src));
        for (int y = have.y0; y < have.y1; y++) {
            for (int x = have.x0; x < have.x1; x++) {
                /* neighbours clamped to the input, as the border transform before the kernel does. */
                const float *p[3][3];
                for (int j = -1; j <= 1; j++) {
                    for (int i = -1; i <= 1; i++) {
                        int xx = x + i, yy = y + j;
                        xx = xx < src.x ? src.x : (xx >= src.x + src.w ? src.x + src.w - 1 : xx);
                        yy = yy < src.y ? src.y : (yy >= src.y + src.h ? src.y + src.h - 1 : yy);
                        p[j + 1][i + 1] = fx_px(&src, xx, yy);
                    }
                }
                float *o = fx_px(out, x, y);
                for (int ch = 0; ch < 3; ch++) {
                    float gx = (p[0][2][ch] + side * p[1][2][ch] + p[2][2][ch]) - (p[0][0][ch] + side * p[1][0][ch] + p[2][0][ch]);
                    float gy = (p[0][0][ch] + side * p[0][1][ch] + p[0][2][ch]) - (p[2][0][ch] + side * p[2][1][ch] + p[2][2][ch]);
                    o[ch] = sat((gx * gx + gy * gy) * strength);
                }
                o[3] = 1.0f;
                if (overlay) {
                    float q[4];
                    unpremul(fx_px(&orig, x, y), q);
                    for (int ch = 0; ch < 3; ch++)
                        o[ch] = (q[ch] - o[ch]) * q[3];
                    o[3] = q[3];
                }
            }
        }
    }
    FxFree(&orig);
    FxFree(&src);
    return hr;
}

/* ------------------------------------------------------------------ highlights and shadows */

static inline float luma709(const float *c) { return c[0] * 0.21266f + c[1] * 0.71515f + c[2] * 0.07219f; }

static float gamma_in(float v) { return v > 0.0f ? D2dPowF(v, 0.454545468f) : 0.0f; }

static HRESULT highlights_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    float hi = D2dFxF(e, 0)[0], sh = D2dFxF(e, 1)[0], cl = D2dFxF(e, 2)[0];
    BOOL srgb = *D2dFxU(e, 3) == FX_GAMMA_SRGB;
    float radius = D2dFxF(e, 4)[0];
    if (D2dAbsF(hi) < 1.1920929e-7f && D2dAbsF(sh) < 1.1920929e-7f && D2dAbsF(cl) < 1.1920929e-7f)
        return FxInput(c, 0, area, out);
    float kh = D2dPowF(4.0f, hi * (hi <= 0.0f ? 1.5f : 2.0f)), ks = D2dPowF(5.0f, sh * (sh <= 0.0f ? 1.5f : 0.8f));
    float kc = cl * (cl <= 0.0f ? 1.0f : 2.0f);
    float gk = radius * 0.5f * radius * 3.1415927f * 0.7f;
    float sigma = radius * c->scale;
    FxRect in_b;
    HRESULT hr = FxInputBounds(c, 0, &in_b);
    if (FAILED(hr))
        return hr;
    int reach = gauss_reach(sigma, 2.5f) + 2;
    FxImg src, mask = {};
    hr = FxInput(c, 0, fx_intersect(fx_inflate(area, reach, reach), in_b), &src);
    if (FAILED(hr))
        return hr;
    if (src.px)
        unpremul_image(&src);
    if (src.px)
        hr = FxNew(&mask, fx_rect(&src));
    if (SUCCEEDED(hr) && mask.px) {
        /* the mask: the straight colors with the squared luminance gradient as alpha, then blurred. */
        for (int y = src.y; y < src.y + src.h; y++) {
            for (int x = src.x; x < src.x + src.w; x++) {
                float *m = fx_px(&mask, x, y);
                memcpy(m, fx_px(&src, x, y), 12);
                float gy = luma709(at_or_zero(&src, x, y + 1)) - luma709(at_or_zero(&src, x, y - 1));
                float gx = luma709(at_or_zero(&src, x + 1, y)) - luma709(at_or_zero(&src, x - 1, y));
                m[3] = sat((gy * gy + gx * gx) * gk);
            }
        }
        if (sigma > 1.1920929e-7f)
            FxGaussian(&mask, sigma, sigma, 2.5f, TRUE);
    }
    if (SUCCEEDED(hr))
        hr = FxNew(out, area);
    if (SUCCEEDED(hr) && out->px && src.px) {
        FxRect have = fx_intersect(area, fx_rect(&src));
        for (int y = have.y0; y < have.y1; y++) {
            for (int x = have.x0; x < have.x1; x++) {
                const float *p = fx_px(&src, x, y), *m = fx_px(&mask, x, y);
                float rgb[3] = {p[0], p[1], p[2]}, mr[3] = {m[0], m[1], m[2]};
                if (srgb) {
                    for (int ch = 0; ch < 3; ch++) {
                        rgb[ch] = gamma_in(rgb[ch]);
                        mr[ch] = gamma_in(mr[ch]);
                    }
                }
                float L = luma709(rgb), ml = luma709(mr);
                float shadow = L * ks / (L * ks - L + 1.0f), light = L * kh / (L * kh - L + 1.0f);
                float d = L - ml, mm = m[3] * d + ml, Lc = kc * d + L;
                float w = 1.0f - mm;
                float mid = 1.0f - w * w - mm * mm;
                float nl = sat(Lc * mid + (shadow * w * w + light * mm * mm));
                float u = rgb[0] * -0.0999099985f + rgb[1] * -0.336089998f + rgb[2] * 0.435999990f;
                float v = rgb[0] * 0.615000010f + rgb[1] * -0.558610022f + rgb[2] * -0.0563899986f;
                float o[3] = {nl + 1.28032994f * v, nl - 0.214819998f * u - 0.380589992f * v, nl + 2.12797999f * u};
                float mx = D2dMaxF(D2dMaxF(o[2], o[1]), o[0]), k = mx > 1.0f ? 1.0f / mx : 1.0f;
                float *q = fx_px(out, x, y);
                for (int ch = 0; ch < 3; ch++) {
                    float t = o[ch] * k;
                    if (srgb)
                        t = t > 0.0f ? D2dPowF(t, 2.2f) : 0.0f;
                    q[ch] = t * p[3];
                }
                q[3] = p[3];
            }
        }
    }
    FxFree(&mask);
    FxFree(&src);
    return hr;
}

/* ------------------------------------------------------------------ displacement map */

static HRESULT displacement_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    float scale = D2dFxF(e, 0)[0] * c->scale;
    UINT32 cx = *D2dFxU(e, 1), cy = *D2dFxU(e, 2);
    int reach = iceil(D2dAbsF(scale) * 0.5f) + 2;
    FxImg src, map;
    HRESULT hr = FxInput(c, 0, fx_inflate(area, reach, reach), &src);
    if (FAILED(hr))
        return hr;
    hr = FxInput(c, 1, area, &map);
    if (SUCCEEDED(hr))
        hr = FxNew(out, area);
    if (SUCCEEDED(hr) && out->px && src.px) {
        for (int y = area.y0; y < area.y1; y++) {
            for (int x = area.x0; x < area.x1; x++) {
                const float *m = fx_px(&map, x, y);
                float ox = (sat(m[cx > 3 ? 3 : cx]) - 0.5f) * scale, oy = (sat(m[cy > 3 ? 3 : cy]) - 0.5f) * scale;
                FxSample(&src, (float)x + 0.5f + ox, (float)y + 0.5f + oy, FX_LINEAR, FALSE, fx_px(out, x, y));
            }
        }
    }
    FxFree(&map);
    FxFree(&src);
    return hr;
}

/* ------------------------------------------------------------------ turbulence */

/* svg's turbulence: perlin noise of the reference implementation, its lattice seeded the same way. */
enum { TB_SIZE = 0x100, TB_MASK = 0xff, TB_N = 0x1000 };

typedef struct Turb {
    int lattice[TB_SIZE + TB_SIZE + 2];
    float gradient[4][TB_SIZE + TB_SIZE + 2][2];
} Turb;

static long turb_random(long seed)
{
    long hi = seed / 127773, lo = seed % 127773;
    seed = 16807 * lo - 2836 * hi;
    if (seed <= 0)
        seed += 2147483647;
    return seed;
}

static void turb_init(Turb *t, long seed)
{
    if (seed <= 0)
        seed = -(seed % (2147483647 - 1)) + 1;
    if (seed > 2147483647 - 1)
        seed = 2147483647 - 1;
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < TB_SIZE; i++) {
            t->lattice[i] = i;
            for (int j = 0; j < 2; j++)
                t->gradient[k][i][j] = (float)(((seed = turb_random(seed)) % (TB_SIZE + TB_SIZE)) - TB_SIZE) / TB_SIZE;
            float s = D2dSqrtF(t->gradient[k][i][0] * t->gradient[k][i][0] + t->gradient[k][i][1] * t->gradient[k][i][1]);
            if (s > 0.0f) {
                t->gradient[k][i][0] /= s;
                t->gradient[k][i][1] /= s;
            }
        }
    }
    for (int i = TB_SIZE - 1; i > 0; i--) {
        int k = t->lattice[i], j = (int)((seed = turb_random(seed)) % TB_SIZE);
        t->lattice[i] = t->lattice[j];
        t->lattice[j] = k;
    }
    for (int i = 0; i < TB_SIZE + 2; i++) {
        t->lattice[TB_SIZE + i] = t->lattice[i];
        for (int k = 0; k < 4; k++)
            for (int j = 0; j < 2; j++)
                t->gradient[k][TB_SIZE + i][j] = t->gradient[k][i][j];
    }
}

typedef struct Stitch {
    int width, height, wrap_x, wrap_y;
} Stitch;

static float s_curve(float t) { return t * t * (3.0f - 2.0f * t); }
static float lerpf(float t, float a, float b) { return a + t * (b - a); }

static float turb_noise(const Turb *t, int ch, float vx, float vy, const Stitch *st)
{
    float tx = vx + TB_N, ty = vy + TB_N;
    int bx0 = (int)tx, by0 = (int)ty;
    int bx1 = bx0 + 1, by1 = by0 + 1;
    float rx0 = tx - (float)(int)tx, ry0 = ty - (float)(int)ty, rx1 = rx0 - 1.0f, ry1 = ry0 - 1.0f;
    if (st) {
        if (bx0 >= st->wrap_x)
            bx0 -= st->width;
        if (bx1 >= st->wrap_x)
            bx1 -= st->width;
        if (by0 >= st->wrap_y)
            by0 -= st->height;
        if (by1 >= st->wrap_y)
            by1 -= st->height;
    }
    bx0 &= TB_MASK;
    bx1 &= TB_MASK;
    by0 &= TB_MASK;
    by1 &= TB_MASK;
    int i = t->lattice[bx0], j = t->lattice[bx1];
    int b00 = t->lattice[i + by0], b10 = t->lattice[j + by0], b01 = t->lattice[i + by1], b11 = t->lattice[j + by1];
    float sx = s_curve(rx0), sy = s_curve(ry0);
    const float *q = t->gradient[ch][b00];
    float u = rx0 * q[0] + ry0 * q[1];
    q = t->gradient[ch][b10];
    float v = rx1 * q[0] + ry0 * q[1];
    float a = lerpf(sx, u, v);
    q = t->gradient[ch][b01];
    u = rx0 * q[0] + ry1 * q[1];
    q = t->gradient[ch][b11];
    v = rx1 * q[0] + ry1 * q[1];
    float b = lerpf(sx, u, v);
    return lerpf(sy, a, b);
}

static float turbulence(const Turb *t, int ch, float px, float py, float fx, float fy, int octaves, BOOL fractal, BOOL stitch,
                        float tx, float ty, float tw, float th)
{
    Stitch st, *pst = nullptr;
    if (stitch) {
        if (fx != 0.0f) {
            float lo = D2dFloorF(tw * fx) / tw, hi = (D2dFloorF(tw * fx) + 1.0f) / tw;
            fx = fx / lo < hi / fx ? lo : hi;
        }
        if (fy != 0.0f) {
            float lo = D2dFloorF(th * fy) / th, hi = (D2dFloorF(th * fy) + 1.0f) / th;
            fy = fy / lo < hi / fy ? lo : hi;
        }
        st.width = (int)(tw * fx + 0.5f);
        st.wrap_x = (int)(tx * fx + TB_N + (float)st.width);
        st.height = (int)(th * fy + 0.5f);
        st.wrap_y = (int)(ty * fy + TB_N + (float)st.height);
        pst = &st;
    }
    float sum = 0.0f, ratio = 1.0f, vx = px * fx, vy = py * fy;
    for (int o = 0; o < octaves; o++) {
        float n = turb_noise(t, ch, vx, vy, pst);
        sum += fractal ? n / ratio : D2dAbsF(n) / ratio;
        vx *= 2.0f;
        vy *= 2.0f;
        ratio *= 2.0f;
        if (pst) {
            st.width *= 2;
            st.wrap_x = 2 * st.wrap_x - TB_N;
            st.height *= 2;
            st.wrap_y = 2 * st.wrap_y - TB_N;
        }
    }
    return sum;
}

static void turbulence_rect(FxCtx *c, FxRect *b)
{
    const float *size = D2dFxF(c->e, 1);
    *b = (FxRect){0, 0, iceil(size[0] * c->scale), iceil(size[1] * c->rt->dpi_y / 96.0f)};
}

static HRESULT turbulence_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    const float *offset = D2dFxF(e, 0), *size = D2dFxF(e, 1), *freq = D2dFxF(e, 2);
    int octaves = (int)*D2dFxU(e, 3);
    INT32 seed = (INT32)*D2dFxU(e, 4);
    BOOL fractal = *D2dFxU(e, 5) == FX_FRACTAL_SUM, stitch = *D2dFxU(e, 6) != 0;
    HRESULT hr = FxNew(out, area);
    if (FAILED(hr) || !out->px)
        return hr;
    Turb *t = D2dAlloc(sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    turb_init(t, seed);
    for (int y = area.y0; y < area.y1; y++) {
        for (int x = area.x0; x < area.x1; x++) {
            float px = (float)x / c->scale + offset[0], py = (float)y / c->scale + offset[1], col[4];
            for (int ch = 0; ch < 4; ch++) {
                float v = turbulence(t, ch, px, py, freq[0], freq[1], octaves, fractal, stitch, offset[0], offset[1], size[0], size[1]);
                col[ch] = sat(fractal ? (v + 1.0f) * 0.5f : v);
            }
            float *o = fx_px(out, x, y);
            o[0] = col[0] * col[3];
            o[1] = col[1] * col[3];
            o[2] = col[2] * col[3];
            o[3] = col[3];
        }
    }
    D2dFree(t);
    return S_OK;
}

/* ------------------------------------------------------------------ dispatch */

HRESULT FxFilterBounds(FxCtx *c, FxRect *b)
{
    D2dEffect *e = c->e;
    if (e->id == EFF_TURBULENCE) {
        turbulence_rect(c, b);
        return S_OK;
    }
    FxRect in;
    HRESULT hr = FxInputBounds(c, 0, &in);
    if (FAILED(hr))
        return hr;
    switch (e->id) {
    case EFF_BLUR:
    case EFF_SHADOW: {
        float sigma, range;
        BOOL hard;
        blur_params(c, &sigma, &range, &hard);
        if (!hard) {
            int r = gauss_reach(sigma, range);
            in = fx_inflate(in, r, r);
        }
        break;
    }
    case EFF_DIRECTIONALBLUR: {
        float dx, dy, sigma;
        dirblur_vector(c, &dx, &dy, &sigma);
        if (*D2dFxU(e, 3) != FX_HARD) {
            UINT32 opt = *D2dFxU(e, 2);
            int r = gauss_reach(sigma, g_blur_range[opt > 2 ? 1 : opt]);
            in = fx_inflate(in, iceil(D2dAbsF(dx) * (float)r), iceil(D2dAbsF(dy) * (float)r));
        }
        break;
    }
    case EFF_CONVOLVE:
        if (*D2dFxU(e, 9) != FX_HARD) {
            Tap *t;
            float ext;
            if (convolve_taps(e, &t, &ext) >= 0) {
                D2dFree(t);
                const float *kul = D2dFxF(e, 0);
                in = fx_inflate(in, iceil(ext * kul[0]) + 1, iceil(ext * kul[1]) + 1);
            }
        }
        break;
    case EFF_MORPHOLOGY:
        if (*D2dFxU(e, 0) == FX_DILATE) {
            int w, h;
            morph_size(c, &w, &h);
            in = fx_inflate(in, w >> 1, h >> 1);
        }
        break;
    case EFF_DISPLACEMENT: {
        int r = iceil(D2dAbsF(D2dFxF(e, 0)[0] * c->scale) * 0.5f) + 1;
        in = fx_inflate(in, r, r);
        break;
    }
    default:
        break;
    }
    *b = in;
    return S_OK;
}

HRESULT FxFilterRender(FxCtx *c, FxRect area, FxImg *out)
{
    switch (c->e->id) {
    case EFF_BLUR:
    case EFF_SHADOW:
        return blur_render(c, area, out);
    case EFF_DIRECTIONALBLUR:
        return dirblur_render(c, area, out);
    case EFF_CONVOLVE:
        return convolve_render(c, area, out);
    case EFF_MORPHOLOGY:
        return morph_render(c, area, out);
    case EFF_SHARPEN:
        return sharpen_render(c, area, out);
    case EFF_EDGEDETECTION:
        return edge_render(c, area, out);
    case EFF_HIGHLIGHTSSHADOWS:
        return highlights_render(c, area, out);
    case EFF_DISPLACEMENT:
        return displacement_render(c, area, out);
    case EFF_TURBULENCE:
        return turbulence_render(c, area, out);
    default:
        return FxInput(c, 0, area, out);
    }
}
