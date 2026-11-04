#include "effects.h"

static inline float sat(float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; }

static inline void unpremul3(const float *p, float *c)
{
    if (p[3] == 0.0f) {
        c[0] = c[1] = c[2] = 0.0f;
    } else {
        c[0] = p[0] / p[3];
        c[1] = p[1] / p[3];
        c[2] = p[2] / p[3];
    }
}

/* ------------------------------------------------------------------ porter duff */

static void composite_px(float *d, const float *s, UINT32 mode)
{
    float sa = s[3], da = d[3], fs, fd;
    switch (mode) {
    case D2D1_COMPOSITE_MODE_DESTINATION_OVER: fs = 1 - da; fd = 1; break;
    case D2D1_COMPOSITE_MODE_SOURCE_IN: fs = da; fd = 0; break;
    case D2D1_COMPOSITE_MODE_DESTINATION_IN: fs = 0; fd = sa; break;
    case D2D1_COMPOSITE_MODE_SOURCE_OUT: fs = 1 - da; fd = 0; break;
    case D2D1_COMPOSITE_MODE_DESTINATION_OUT: fs = 0; fd = 1 - sa; break;
    case D2D1_COMPOSITE_MODE_SOURCE_ATOP: fs = da; fd = 1 - sa; break;
    case D2D1_COMPOSITE_MODE_DESTINATION_ATOP: fs = 1 - da; fd = sa; break;
    case D2D1_COMPOSITE_MODE_XOR: fs = 1 - da; fd = 1 - sa; break;
    case D2D1_COMPOSITE_MODE_PLUS: fs = 1; fd = 1; break;
    case D2D1_COMPOSITE_MODE_SOURCE_COPY:
    case D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY: fs = 1; fd = 0; break;
    case D2D1_COMPOSITE_MODE_MASK_INVERT: {
        /* blended with the inverse of the destination color as the source factor. */
        for (int i = 0; i < 3; i++)
            d[i] = s[i] * (1 - d[i]) + d[i] * (1 - sa);
        d[3] = sa + da * (1 - sa);
        return;
    }
    default: fs = 1; fd = 1 - sa; break;
    }
    for (int i = 0; i < 4; i++)
        d[i] = s[i] * fs + d[i] * fd;
}

/* ------------------------------------------------------------------ blend modes */

static float lum(const float *c) { return c[0] * 0.3f + c[1] * 0.59f + c[2] * 0.11f; }

static void clip_color(float *c)
{
    float l = lum(c), n = D2dMinF(D2dMinF(c[0], c[1]), c[2]), x = D2dMaxF(D2dMaxF(c[0], c[1]), c[2]);
    for (int i = 0; i < 3; i++) {
        if (n < 0.0f)
            c[i] = l + (c[i] - l) * l / (l - n);
        if (x > 1.0f)
            c[i] = l + (c[i] - l) * (1.0f - l) / (x - l);
    }
}

static void set_lum(float *c, float l)
{
    float d = l - lum(c);
    for (int i = 0; i < 3; i++)
        c[i] += d;
    clip_color(c);
}

static void set_sat(float *c, float s)
{
    int hi = 0, lo = 0;
    for (int i = 1; i < 3; i++) {
        if (c[i] > c[hi])
            hi = i;
        if (c[i] < c[lo])
            lo = i;
    }
    if (hi == lo) {
        c[0] = c[1] = c[2] = 0.0f;
        return;
    }
    int mid = 3 - hi - lo;
    float range = c[hi] - c[lo];
    c[mid] = (c[mid] - c[lo]) * s / range;
    c[hi] = s;
    c[lo] = 0.0f;
}

static float satof(const float *c) { return D2dMaxF(D2dMaxF(c[0], c[1]), c[2]) - D2dMinF(D2dMinF(c[0], c[1]), c[2]); }

/* the blend of one channel, cb the backdrop (the first input) and cs the foreground. */
static float blend_one(UINT32 mode, float cb, float cs)
{
    switch (mode) {
    case D2D1_BLEND_MODE_MULTIPLY: return cb * cs;
    case D2D1_BLEND_MODE_SCREEN: return cb + cs - cb * cs;
    case D2D1_BLEND_MODE_DARKEN: return D2dMinF(cb, cs);
    case D2D1_BLEND_MODE_LIGHTEN: return D2dMaxF(cb, cs);
    case D2D1_BLEND_MODE_COLOR_BURN:
        if (cb >= 1.0f)
            return 1.0f;
        return cs + cb - 1.0f > 0.0f ? sat((cs + cb - 1.0f) / cs) : 0.0f;
    case D2D1_BLEND_MODE_LINEAR_BURN: return sat(cs + cb - 1.0f);
    case D2D1_BLEND_MODE_COLOR_DODGE:
        if (cs < 1.0f)
            return sat(cb / (1.0f - cs));
        return cb > 0.0f ? 1.0f : 0.0f;
    case D2D1_BLEND_MODE_LINEAR_DODGE: return sat(cs + cb);
    case D2D1_BLEND_MODE_OVERLAY: return cb > 0.5f ? 2.0f * (cs + cb - cs * cb) - 1.0f : 2.0f * cs * cb;
    case D2D1_BLEND_MODE_SOFT_LIGHT:
        if (cs > 0.5f) {
            if (cb > 0.141625f)
                return cb + (2.0f * cs - 1.0f) * (D2dSqrtF(cb) - cb);
            return cb * ((9.0f - 18.0f * cs) * cb + 5.76f * cs - 1.88f);
        }
        return cb * (1.0f + (2.0f * cs - 1.0f) * (1.0f - cb));
    case D2D1_BLEND_MODE_HARD_LIGHT: return cs > 0.5f ? 2.0f * (cs + cb - cs * cb) - 1.0f : 2.0f * cs * cb;
    case D2D1_BLEND_MODE_VIVID_LIGHT:
        if (cs < 0.5f)
            return sat(cs > 0.0f ? (cs + (cb - 1.0f) * 0.5f) / cs : 0.0f);
        return sat(cs < 1.0f ? cb * 0.5f / (1.0f - cs) : 1.0f);
    case D2D1_BLEND_MODE_LINEAR_LIGHT: return sat(2.0f * cs + cb - 1.0f);
    case D2D1_BLEND_MODE_PIN_LIGHT: return cs > 0.5f ? D2dMaxF(2.0f * cs - 1.0f, cb) : D2dMinF(cb, 2.0f * cs);
    case D2D1_BLEND_MODE_HARD_MIX: {
        float s = cs + cb;
        if (s < 0.999f)
            return 0.0f;
        return cb >= cs || s > 1.001f ? 1.0f : 0.0f;
    }
    case D2D1_BLEND_MODE_DIFFERENCE: return D2dAbsF(cs - cb);
    case D2D1_BLEND_MODE_EXCLUSION: return cs + cb - 2.0f * cs * cb;
    case D2D1_BLEND_MODE_SUBTRACT: return sat(cb - cs);
    case D2D1_BLEND_MODE_DIVISION:
        if (cb <= 0.0f)
            return 0.0f;
        return cs == 0.0f ? 1.0f : sat(cb / cs);
    default: return cs;
    }
}

/* the hash windows' dissolve draws its threshold from, seeded by the pixel center. */
static float dissolve_threshold(float x, float y)
{
    static const float k[4] = {97.4090881f, 54.5981483f, 56.2054100f, 44.6878052f};
    float r[4];
    float t = x * k[0] + y * k[1] + x * k[2] + y * k[3];
    r[0] = t - D2dFloorF(t);
    r[2] = x;
    r[3] = y;
    t = r[0] * k[0] + r[3] * k[1] + r[2] * k[2] + r[3] * k[3];
    r[1] = t - D2dFloorF(t);
    static const int order[7] = {2, 3, 0, 1, 2, 3, 0};
    for (int i = 0; i < 7; i++) {
        t = r[0] * k[0] + r[1] * k[1] + r[2] * k[2] + r[3] * k[3];
        r[order[i]] = t - D2dFloorF(t);
    }
    return r[0];
}

static void blend_px(float *d, const float *s, UINT32 mode, int x, int y)
{
    float cb[3], cs[3], b[3];
    float ab = d[3], as = s[3];
    if (mode == D2D1_BLEND_MODE_DISSOLVE) {
        if (as > 0.0f && as >= dissolve_threshold((float)x + 0.5f, (float)y + 0.5f)) {
            unpremul3(s, d);
            d[3] = 1.0f;
        }
        return;
    }
    unpremul3(d, cb);
    unpremul3(s, cs);
    switch (mode) {
    case D2D1_BLEND_MODE_DARKER_COLOR:
    case D2D1_BLEND_MODE_LIGHTER_COLOR: {
        BOOL take = mode == D2D1_BLEND_MODE_DARKER_COLOR ? lum(cs) < lum(cb) : lum(cb) < lum(cs);
        memcpy(b, take ? cs : cb, sizeof(b));
        break;
    }
    case D2D1_BLEND_MODE_HUE:
        memcpy(b, cs, sizeof(b));
        set_sat(b, satof(cb));
        set_lum(b, lum(cb));
        break;
    case D2D1_BLEND_MODE_SATURATION:
        memcpy(b, cb, sizeof(b));
        set_sat(b, satof(cs));
        set_lum(b, lum(cb));
        break;
    case D2D1_BLEND_MODE_COLOR:
        memcpy(b, cs, sizeof(b));
        set_lum(b, lum(cb));
        break;
    case D2D1_BLEND_MODE_LUMINOSITY:
        memcpy(b, cb, sizeof(b));
        set_lum(b, lum(cs));
        break;
    default:
        for (int i = 0; i < 3; i++)
            b[i] = blend_one(mode, cb[i], cs[i]);
        break;
    }
    for (int i = 0; i < 3; i++)
        d[i] = s[i] * (1.0f - ab) + d[i] * (1.0f - as) + b[i] * as * ab;
    d[3] = as + ab - as * ab;
}

/* ------------------------------------------------------------------ bounds */

static void arithmetic_coefficients(D2dEffect *e, float *k)
{
    if (e->id == EFF_CROSSFADE) {
        float w = D2dFxF(e, 0)[0];
        k[0] = 0.0f;
        k[1] = w;
        k[2] = 1.0f - w;
        k[3] = 0.0f;
    } else {
        memcpy(k, D2dFxF(e, 0), 16);
    }
}

HRESULT FxCompositeBounds(FxCtx *c, FxRect *b)
{
    D2dEffect *e = c->e;
    FxRect r[8] = {};
    UINT32 n = e->input_count;
    for (UINT32 i = 0; i < n && i < 8; i++) {
        HRESULT hr = FxInputBounds(c, i, &r[i]);
        if (FAILED(hr))
            return hr;
    }
    if (!n) {
        *b = (FxRect){0, 0, 0, 0};
        return S_OK;
    }
    switch (e->id) {
    case EFF_ALPHAMASK:
        *b = fx_intersect(r[0], r[1]);
        return S_OK;
    case EFF_ARITHMETIC:
    case EFF_CROSSFADE: {
        float k[4];
        arithmetic_coefficients(e, k);
        if (k[3] != 0.0f) {
            *b = fx_inf;
            return S_OK;
        }
        FxRect o = {0, 0, 0, 0};
        if (k[0] != 0.0f)
            o = fx_union(o, fx_intersect(r[0], r[1]));
        if (k[1] != 0.0f)
            o = fx_union(o, r[1]);
        if (k[2] != 0.0f)
            o = fx_union(o, r[0]);
        *b = o;
        return S_OK;
    }
    case EFF_COMPOSITE: {
        UINT32 mode = *D2dFxU(e, 0);
        FxRect o = r[0];
        for (UINT32 i = 1; i < n && i < 8; i++) {
            switch (mode) {
            case D2D1_COMPOSITE_MODE_SOURCE_IN:
            case D2D1_COMPOSITE_MODE_DESTINATION_IN:
                o = fx_intersect(o, r[i]);
                break;
            case D2D1_COMPOSITE_MODE_SOURCE_OUT:
            case D2D1_COMPOSITE_MODE_DESTINATION_ATOP:
            case D2D1_COMPOSITE_MODE_SOURCE_COPY:
                o = r[i];
                break;
            case D2D1_COMPOSITE_MODE_DESTINATION_OUT:
            case D2D1_COMPOSITE_MODE_SOURCE_ATOP:
                break;
            default:
                o = fx_union(o, r[i]);
                break;
            }
        }
        *b = o;
        return S_OK;
    }
    default: {
        FxRect o = r[0];
        for (UINT32 i = 1; i < n && i < 8; i++)
            o = fx_union(o, r[i]);
        *b = o;
        return S_OK;
    }
    }
}

/* ------------------------------------------------------------------ rendering */

HRESULT FxCompositeRender(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    HRESULT hr = FxInput(c, 0, area, out);
    if (FAILED(hr) || !out->px)
        return hr;
    SIZE_T n = (SIZE_T)out->w * out->h;
    for (UINT32 i = 1; i < e->input_count && i < 8; i++) {
        FxImg in;
        hr = FxInput(c, i, area, &in);
        if (FAILED(hr))
            return hr;
        float *d = out->px;
        const float *s = in.px;
        switch (e->id) {
        case EFF_COMPOSITE: {
            UINT32 mode = *D2dFxU(e, 0);
            for (SIZE_T j = 0; j < n; j++)
                composite_px(d + j * 4, s + j * 4, mode);
            break;
        }
        case EFF_ALPHAMASK:
            for (SIZE_T j = 0; j < n; j++)
                composite_px(d + j * 4, s + j * 4, D2D1_COMPOSITE_MODE_DESTINATION_IN);
            break;
        case EFF_BLEND: {
            UINT32 mode = *D2dFxU(e, 0);
            for (int y = 0; y < out->h; y++) {
                for (int x = 0; x < out->w; x++) {
                    SIZE_T j = (SIZE_T)y * out->w + x;
                    blend_px(d + j * 4, s + j * 4, mode, out->x + x, out->y + y);
                }
            }
            break;
        }
        default: {
            /* arithmetic: the destination is the first input, the source the second. */
            float k[4];
            arithmetic_coefficients(e, k);
            BOOL clamp = e->id == EFF_ARITHMETIC && *D2dFxU(e, 1) != 0;
            for (SIZE_T j = 0; j < n * 4; j++) {
                float v = k[0] * s[j] * d[j] + k[1] * s[j] + k[2] * d[j] + k[3];
                d[j] = clamp ? sat(v) : v;
            }
            break;
        }
        }
        FxFree(&in);
    }
    return S_OK;
}
