#include "effects.h"

static inline float sat(float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; }

typedef struct Light {
    int kind;
    BOOL specular;
    float surface;
    float k;
    float exponent;
    float color[3];
    float dir[3];
    float pos[3];
    float at[3];
    float focus, cos_inner, cos_outer;
    float unit[2];
    int scale_mode;
} Light;

enum { LIGHT_DISTANT, LIGHT_POINT, LIGHT_SPOT };

static float deg(float d) { return (float)(d * 3.141592653589793 / 180.0); }

static void light_setup(FxCtx *c, Light *l)
{
    D2dEffect *e = c->e;
    memset(l, 0, sizeof(*l));
    l->unit[0] = l->unit[1] = 1.0f;
    l->color[0] = l->color[1] = l->color[2] = 1.0f;
    l->scale_mode = 1;
    int i = 0;
    float az = 0.0f, el = 0.0f;
    switch (e->id) {
    case EFF_EMBOSS:
        l->kind = LIGHT_DISTANT;
        az = D2dFxF(e, 1)[0];
        el = 50.0f;
        l->surface = D2dFxF(e, 0)[0];
        l->k = 1.0f;
        break;
    case EFF_DISTANTDIFFUSE:
    case EFF_DISTANTSPECULAR:
        l->kind = LIGHT_DISTANT;
        az = D2dFxF(e, i++)[0];
        el = D2dFxF(e, i++)[0];
        break;
    case EFF_POINTDIFFUSE:
    case EFF_POINTSPECULAR:
        l->kind = LIGHT_POINT;
        memcpy(l->pos, D2dFxF(e, i++), 12);
        break;
    default: {
        l->kind = LIGHT_SPOT;
        memcpy(l->pos, D2dFxF(e, i++), 12);
        memcpy(l->at, D2dFxF(e, i++), 12);
        l->focus = D2dFxF(e, i++)[0];
        float cone = D2dAbsF(D2dFxF(e, i++)[0]);
        float sn;
        D2dSinCosF(deg(cone), &sn, &l->cos_inner);
        /* the light fades to nothing within one more degree, or a fifth more of a narrow cone. */
        D2dSinCosF(cone >= 5.0f ? deg(cone + 1.0f) : deg(cone * 1.2f), &sn, &l->cos_outer);
        break;
    }
    }
    if (e->id != EFF_EMBOSS) {
        l->specular = e->id == EFF_DISTANTSPECULAR || e->id == EFF_POINTSPECULAR || e->id == EFF_SPOTSPECULAR;
        if (l->specular)
            l->exponent = D2dFxF(e, i++)[0];
        l->k = D2dFxF(e, i++)[0];
        l->surface = D2dFxF(e, i++)[0];
        memcpy(l->color, D2dFxF(e, i++), 12);
        memcpy(l->unit, D2dFxF(e, i++), 8);
        UINT32 m = *D2dFxU(e, i);
        l->scale_mode = m == 0 ? FX_NEAREST : m == 2 ? FX_CUBIC : m == 3 ? FX_MULTISAMPLE : m == 4 ? FX_ANISOTROPIC : m == 5 ? FX_HQCUBIC : FX_LINEAR;
    }
    if (el == 270.0f)
        el += 0.001f;
    float sa, ca, se, ce;
    D2dSinCosF(deg(az), &sa, &ca);
    D2dSinCosF(deg(el), &se, &ce);
    l->dir[0] = ca * ce;
    l->dir[1] = sa * ce;
    l->dir[2] = se;
    for (int k = 0; k < 3; k++) {
        l->pos[k] *= c->scale;
        l->at[k] *= c->scale;
    }
}

static void normalize3(float *v)
{
    float n = D2dSqrtF(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 0.0f) {
        v[0] /= n;
        v[1] /= n;
        v[2] /= n;
    }
}

/* the height at a pixel of the surface, its alpha. */
static float height(const FxImg *im, int x, int y) { return fx_px(im, x, y)[3]; }

/* svg's surface normal: sobel where every neighbour is there, one sided at the edges of the image. */
static void surface_normal(const FxImg *im, int x, int y, float ss, float *n)
{
    int x0 = im->x, y0 = im->y, x1 = im->x + im->w - 1, y1 = im->y + im->h - 1;
    int xl = x > x0 ? x - 1 : x, xr = x < x1 ? x + 1 : x, yt = y > y0 ? y - 1 : y, yb = y < y1 ? y + 1 : y;
    float sx = 0.0f, wx = 0.0f, sy = 0.0f, wy = 0.0f;
    for (int j = -1; j <= 1; j++) {
        int yy = y + j;
        if (yy < y0 || yy > y1)
            continue;
        float w = j ? 1.0f : 2.0f;
        sx += w * (height(im, xr, yy) - height(im, xl, yy));
        wx += w;
    }
    for (int i = -1; i <= 1; i++) {
        int xx = x + i;
        if (xx < x0 || xx > x1)
            continue;
        float w = i ? 1.0f : 2.0f;
        sy += w * (height(im, xx, yb) - height(im, xx, yt));
        wy += w;
    }
    n[0] = xr > xl ? -ss * 2.0f / (wx * (float)(xr - xl)) * sx : 0.0f;
    n[1] = yb > yt ? -ss * 2.0f / (wy * (float)(yb - yt)) * sy : 0.0f;
    n[2] = 1.0f;
    normalize3(n);
}

static void shade(const Light *l, const FxImg *im, int x, int y, float *o)
{
    float n[3], L[3], col[3] = {l->color[0], l->color[1], l->color[2]};
    surface_normal(im, x, y, l->surface, n);
    if (l->kind == LIGHT_DISTANT) {
        memcpy(L, l->dir, sizeof(L));
    } else {
        float p[3] = {(float)x + 0.5f, (float)y + 0.5f, l->surface * height(im, x, y)};
        L[0] = l->pos[0] / l->unit[0] - p[0];
        L[1] = l->pos[1] / l->unit[1] - p[1];
        L[2] = l->pos[2] - p[2];
        normalize3(L);
        if (l->kind == LIGHT_SPOT) {
            float s[3] = {l->at[0] - l->pos[0], l->at[1] - l->pos[1], l->at[2] - l->pos[2]};
            normalize3(s);
            float m = sat(-(L[0] * s[0] + L[1] * s[1] + L[2] * s[2]));
            float spot = m > 0.0f ? D2dPowF(m, l->focus) : 0.0f;
            float t = sat((m - l->cos_outer) / (l->cos_inner - l->cos_outer));
            t = t * t * (3.0f - 2.0f * t);
            for (int k = 0; k < 3; k++)
                col[k] *= spot * t;
        }
    }
    if (!l->specular) {
        float d = D2dMaxF(n[0] * L[0] + n[1] * L[1] + n[2] * L[2], 0.0f) * l->k;
        o[0] = col[0] * d;
        o[1] = col[1] * d;
        o[2] = col[2] * d;
        o[3] = 1.0f;
        return;
    }
    float h[3] = {L[0], L[1], L[2] + 1.0f};
    normalize3(h);
    float d = D2dMaxF(n[0] * h[0] + n[1] * h[1] + n[2] * h[2], 0.0f);
    float v = (d > 0.0f ? D2dPowF(d, l->exponent) : (l->exponent == 0.0f ? 1.0f : 0.0f)) * l->k;
    float r = col[0] * v, g = col[1] * v, b = col[2] * v, a = D2dMaxF(D2dMaxF(b, g), r);
    o[0] = r * a;
    o[1] = g * a;
    o[2] = b * a;
    o[3] = a;
}

HRESULT FxLightBounds(FxCtx *c, FxRect *b) { return FxInputBounds(c, 0, b); }

HRESULT FxLightRender(FxCtx *c, FxRect area, FxImg *out)
{
    Light l;
    light_setup(c, &l);
    FxRect in_b;
    HRESULT hr = FxInputBounds(c, 0, &in_b);
    if (FAILED(hr))
        return hr;
    /* the normals at the edges of the image need the whole image or a margin around the area. */
    int mx = (int)(l.unit[0] * 2.0f) + 2, my = (int)(l.unit[1] * 2.0f) + 2;
    FxImg src;
    hr = FxInput(c, 0, fx_intersect(fx_inflate(area, mx, my), in_b), &src);
    if (FAILED(hr))
        return hr;
    if (c->e->id == EFF_EMBOSS && src.px) {
        for (SIZE_T i = 0; i < (SIZE_T)src.w * src.h; i++) {
            float *p = src.px + i * 4;
            float a = p[3];
            p[3] = a == 0.0f ? 0.0f : (p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f) / a;
            p[0] = p[1] = p[2] = 0.0f;
        }
    }
    BOOL unit = l.unit[0] != 1.0f || l.unit[1] != 1.0f;
    FxImg work = src, lit = {};
    if (unit && src.px) {
        hr = FxScale(&src, 1.0f / l.unit[0], 1.0f / l.unit[1], l.scale_mode, &work);
        FxFree(&src);
    }
    if (SUCCEEDED(hr))
        hr = FxNew(&lit, unit ? fx_rect(&work) : fx_intersect(area, fx_rect(&work)));
    if (SUCCEEDED(hr) && lit.px) {
        for (int y = lit.y; y < lit.y + lit.h; y++)
            for (int x = lit.x; x < lit.x + lit.w; x++)
                shade(&l, &work, x, y, fx_px(&lit, x, y));
    }
    FxFree(&work);
    if (SUCCEEDED(hr) && unit) {
        FxImg back;
        hr = FxScale(&lit, l.unit[0], l.unit[1], l.scale_mode, &back);
        FxFree(&lit);
        lit = back;
    }
    if (SUCCEEDED(hr))
        hr = FxNew(out, area);
    if (SUCCEEDED(hr) && out->px && lit.px) {
        FxRect have = fx_intersect(area, fx_rect(&lit));
        for (int y = have.y0; y < have.y1; y++)
            memcpy(fx_px(out, have.x0, y), fx_px(&lit, have.x0, y), (SIZE_T)(have.x1 - have.x0) * 16);
    }
    FxFree(&lit);
    return hr;
}
