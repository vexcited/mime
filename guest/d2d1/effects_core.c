#include "effects.h"

HRESULT FxNew(FxImg *im, FxRect r)
{
    im->x = r.x0;
    im->y = r.y0;
    im->w = r.x1 > r.x0 ? r.x1 - r.x0 : 0;
    im->h = r.y1 > r.y0 ? r.y1 - r.y0 : 0;
    im->px = nullptr;
    if (!im->w || !im->h)
        return S_OK;
    if ((SIZE_T)im->w * im->h > (SIZE_T)16384 * 16384)
        return D2DERR_INTERMEDIATE_TOO_LARGE;
    im->px = D2dAllocZero((SIZE_T)im->w * im->h * 16);
    return im->px ? S_OK : E_OUTOFMEMORY;
}

void FxFree(FxImg *im)
{
    D2dFree(im->px);
    im->px = nullptr;
}

/* ------------------------------------------------------------------ inputs */

HRESULT FxInputBounds(FxCtx *c, UINT32 i, FxRect *b)
{
    D2dEffect *e = c->e;
    if (i >= e->input_count || !e->inputs[i]) {
        *b = (FxRect){0, 0, 0, 0};
        return S_OK;
    }
    return D2dImageBoundsPx(c->rt, e->inputs[i], b, c->depth + 1);
}

HRESULT FxInput(FxCtx *c, UINT32 i, FxRect want, FxImg *out)
{
    HRESULT hr = FxNew(out, want);
    if (FAILED(hr) || !out->px)
        return hr;
    D2dEffect *e = c->e;
    if (i >= e->input_count || !e->inputs[i])
        return S_OK;
    D2dImageData raw = {};
    hr = D2dImageGet(c->rt, e->inputs[i], want, &raw, c->depth + 1);
    if (FAILED(hr)) {
        FxFree(out);
        return hr;
    }
    if (raw.fpx)
        c->keep_float = TRUE;
    FxRect have = fx_intersect(want, (FxRect){raw.x, raw.y, raw.x + raw.surface.w, raw.y + raw.surface.h});
    for (int y = have.y0; y < have.y1; y++) {
        float *d = fx_px(out, have.x0, y);
        SIZE_T at = (SIZE_T)(y - raw.y) * raw.surface.w + (have.x0 - raw.x);
        if (raw.fpx) {
            memcpy(d, raw.fpx + at * 4, (SIZE_T)(have.x1 - have.x0) * 16);
            continue;
        }
        const uint32_t *s = raw.surface.px + at;
        for (int x = have.x0; x < have.x1; x++, s++, d += 4) {
            uint32_t p = *s;
            d[0] = ((p >> 16) & 255) * (1.0f / 255.0f);
            d[1] = ((p >> 8) & 255) * (1.0f / 255.0f);
            d[2] = (p & 255) * (1.0f / 255.0f);
            d[3] = (p >> 24) * (1.0f / 255.0f);
        }
    }
    D2dImageDataFree(&raw);
    return S_OK;
}

static inline uint32_t to8(float v)
{
    if (!(v > 0.0f))
        return 0;
    if (v >= 1.0f)
        return 255;
    return (uint32_t)(v * 255.0f + 0.5f);
}

/* the float image as image data: 8 bit pixels always, the floats kept when asked. */
static HRESULT fx_output(FxImg *im, BOOL keep_float, D2dImageData *out)
{
    memset(out, 0, sizeof(*out));
    out->x = im->x;
    out->y = im->y;
    if (!im->px)
        return S_OK;
    if (!D2dSurfaceInit(&out->surface, im->w, im->h))
        return E_OUTOFMEMORY;
    SIZE_T n = (SIZE_T)im->w * im->h;
    const float *s = im->px;
    for (SIZE_T i = 0; i < n; i++, s += 4)
        out->surface.px[i] = to8(s[3]) << 24 | to8(s[0]) << 16 | to8(s[1]) << 8 | to8(s[2]);
    if (keep_float) {
        out->fpx = im->px;
        im->px = nullptr;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ dispatch */

typedef struct FxPart {
    FxBoundsFn bounds;
    FxRenderFn render;
} FxPart;

static const FxPart g_color = {FxColorBounds, FxColorRender}, g_composite = {FxCompositeBounds, FxCompositeRender},
                    g_filter = {FxFilterBounds, FxFilterRender}, g_transform = {FxTransformBounds, FxTransformRender},
                    g_light = {FxLightBounds, FxLightRender};

static const FxPart *part_of(int id)
{
    switch (id) {
    case EFF_COMPOSITE:
    case EFF_BLEND:
    case EFF_ARITHMETIC:
    case EFF_ALPHAMASK:
    case EFF_CROSSFADE:
        return &g_composite;
    case EFF_BLUR:
    case EFF_SHADOW:
    case EFF_CONVOLVE:
    case EFF_DIRECTIONALBLUR:
    case EFF_MORPHOLOGY:
    case EFF_SHARPEN:
    case EFF_EDGEDETECTION:
    case EFF_HIGHLIGHTSSHADOWS:
    case EFF_DISPLACEMENT:
    case EFF_TURBULENCE:
        return &g_filter;
    case EFF_CROP:
    case EFF_AFFINE:
    case EFF_BORDER:
    case EFF_TILE:
    case EFF_3DTRANSFORM:
    case EFF_3DPERSPECTIVE:
    case EFF_ATLAS:
    case EFF_BITMAPSOURCE:
    case EFF_DPICOMPENSATION:
    case EFF_SCALE:
    case EFF_STRAIGHTEN:
    case EFF_YCBCR:
        return &g_transform;
    case EFF_DISTANTDIFFUSE:
    case EFF_DISTANTSPECULAR:
    case EFF_POINTDIFFUSE:
    case EFF_POINTSPECULAR:
    case EFF_SPOTDIFFUSE:
    case EFF_SPOTSPECULAR:
    case EFF_EMBOSS:
        return &g_light;
    default:
        return &g_color;
    }
}

static void ctx_init(FxCtx *c, D2dRt *rt, D2dEffect *e, int depth)
{
    c->rt = rt;
    c->e = e;
    c->depth = depth;
    c->scale = rt->dpi_x / 96.0f;
    D2D1_BUFFER_PRECISION p = e->precision ? e->precision : rt->controls.bufferPrecision;
    c->keep_float = p == D2D1_BUFFER_PRECISION_16BPC_FLOAT || p == D2D1_BUFFER_PRECISION_32BPC_FLOAT;
}

HRESULT D2dFxBounds(D2dRt *rt, D2dEffect *e, FxRect *b, int depth)
{
    FxCtx c;
    ctx_init(&c, rt, e, depth);
    return part_of(e->id)->bounds(&c, b);
}

HRESULT D2dFxRender(D2dRt *rt, D2dEffect *e, FxRect want, D2dImageData *out, int depth)
{
    memset(out, 0, sizeof(*out));
    FxCtx c;
    ctx_init(&c, rt, e, depth);
    const FxPart *part = part_of(e->id);
    FxRect b;
    HRESULT hr = part->bounds(&c, &b);
    if (FAILED(hr))
        return hr;
    FxRect area = fx_intersect(want, b);
    if (fx_empty(area))
        area = (FxRect){want.x0, want.y0, want.x0, want.y0};
    if (!fx_empty(area) && !c.keep_float) {
        hr = FxGpuRender(&c, area, out);
        if (hr != S_FALSE)
            return hr;
        memset(out, 0, sizeof(*out));
    }
    FxImg im = {area.x0, area.y0, 0, 0, nullptr};
    if (!fx_empty(area)) {
        hr = part->render(&c, area, &im);
        if (FAILED(hr))
            return hr;
    }
    hr = fx_output(&im, c.keep_float, out);
    FxFree(&im);
    return hr;
}

/* ------------------------------------------------------------------ property values */

enum { CK_CLAMP, CK_ENUM, CK_ENUM1, CK_WRAP, CK_READONLY, CK_BLOBMAX };

typedef struct FxCheck {
    UINT8 id;
    UINT8 index;
    UINT8 kind;
    float lo, hi;
} FxCheck;

/* the ranges windows' setters clamp numbers to, and the largest value of each enumeration. */
static const FxCheck g_checks[] = {
    {EFF_COLORMATRIX, 1, CK_ENUM1, 1, 2},
    {EFF_BLUR, 0, CK_CLAMP, 0, 250}, {EFF_BLUR, 1, CK_ENUM, 0, 2}, {EFF_BLUR, 2, CK_ENUM, 0, 1},
    {EFF_COMPOSITE, 0, CK_ENUM, 0, 12},
    {EFF_CROP, 1, CK_ENUM, 0, 1},
    {EFF_AFFINE, 0, CK_ENUM, 0, 5}, {EFF_AFFINE, 1, CK_ENUM, 0, 1}, {EFF_AFFINE, 3, CK_CLAMP, 0, 1},
    {EFF_OPACITY, 0, CK_CLAMP, 0, 1},
    {EFF_SHADOW, 0, CK_CLAMP, 0, 250}, {EFF_SHADOW, 2, CK_ENUM, 0, 2},
    {EFF_BRIGHTNESS, 0, CK_CLAMP, 0, 1}, {EFF_BRIGHTNESS, 1, CK_CLAMP, 0, 1},
    {EFF_SATURATION, 0, CK_CLAMP, 0, 2},
    {EFF_SEPIA, 0, CK_CLAMP, 0, 1}, {EFF_SEPIA, 1, CK_ENUM1, 1, 2},
    {EFF_BLEND, 0, CK_ENUM, 0, 25},
    {EFF_BORDER, 0, CK_ENUM, 0, 2}, {EFF_BORDER, 1, CK_ENUM, 0, 2},
    {EFF_HUEROTATE, 0, CK_WRAP, 0, 360},
    {EFF_CONTRAST, 0, CK_CLAMP, -1, 1},
    {EFF_EXPOSURE, 0, CK_CLAMP, -2, 2},
    {EFF_3DTRANSFORM, 0, CK_ENUM, 0, 4}, {EFF_3DTRANSFORM, 1, CK_ENUM, 0, 1},
    {EFF_3DPERSPECTIVE, 0, CK_ENUM, 0, 4}, {EFF_3DPERSPECTIVE, 1, CK_ENUM, 0, 1}, {EFF_3DPERSPECTIVE, 2, CK_CLAMP, 0, __builtin_inff()},
    {EFF_BITMAPSOURCE, 1, CK_CLAMP, 0, 20}, {EFF_BITMAPSOURCE, 2, CK_ENUM, 0, 7}, {EFF_BITMAPSOURCE, 4, CK_ENUM1, 1, 2}, {EFF_BITMAPSOURCE, 5, CK_ENUM1, 1, 8},
    {EFF_CONVOLVE, 0, CK_CLAMP, 0.01f, 100}, {EFF_CONVOLVE, 1, CK_ENUM, 0, 5}, {EFF_CONVOLVE, 2, CK_CLAMP, 1, 100},
    {EFF_CONVOLVE, 3, CK_CLAMP, 1, 100}, {EFF_CONVOLVE, 4, CK_BLOBMAX, 0, 40000}, {EFF_CONVOLVE, 7, CK_CLAMP, -50, 50},
    {EFF_CONVOLVE, 9, CK_ENUM, 0, 1},
    {EFF_DIRECTIONALBLUR, 0, CK_CLAMP, 0, 250}, {EFF_DIRECTIONALBLUR, 1, CK_WRAP, 0, 360}, {EFF_DIRECTIONALBLUR, 2, CK_ENUM, 0, 2},
    {EFF_DIRECTIONALBLUR, 3, CK_ENUM, 0, 1},
    {EFF_DISPLACEMENT, 1, CK_ENUM, 0, 3}, {EFF_DISPLACEMENT, 2, CK_ENUM, 0, 3},
    {EFF_DISTANTDIFFUSE, 0, CK_CLAMP, 0, 360}, {EFF_DISTANTDIFFUSE, 1, CK_CLAMP, 0, 360}, {EFF_DISTANTDIFFUSE, 2, CK_CLAMP, 0, 10000},
    {EFF_DISTANTDIFFUSE, 3, CK_CLAMP, -10000, 10000}, {EFF_DISTANTDIFFUSE, 5, CK_CLAMP, 0.01f, 100}, {EFF_DISTANTDIFFUSE, 6, CK_ENUM, 0, 5},
    {EFF_DISTANTSPECULAR, 0, CK_CLAMP, 0, 360}, {EFF_DISTANTSPECULAR, 1, CK_CLAMP, 0, 360}, {EFF_DISTANTSPECULAR, 2, CK_CLAMP, -10000, 10000},
    {EFF_DISTANTSPECULAR, 3, CK_CLAMP, 0, 10000}, {EFF_DISTANTSPECULAR, 4, CK_CLAMP, -10000, 10000}, {EFF_DISTANTSPECULAR, 6, CK_CLAMP, 0.01f, 100},
    {EFF_DISTANTSPECULAR, 7, CK_ENUM, 0, 5},
    {EFF_DPICOMPENSATION, 0, CK_ENUM, 0, 5}, {EFF_DPICOMPENSATION, 1, CK_ENUM, 0, 1},
    {EFF_SCALE, 0, CK_CLAMP, 0.01f, __builtin_inff()}, {EFF_SCALE, 2, CK_ENUM, 0, 5}, {EFF_SCALE, 3, CK_ENUM, 0, 1}, {EFF_SCALE, 4, CK_CLAMP, 0, 1},
    {EFF_HISTOGRAM, 0, CK_CLAMP, 2, 1024}, {EFF_HISTOGRAM, 1, CK_ENUM, 0, 3}, {EFF_HISTOGRAM, 2, CK_READONLY, 0, 0},
    {EFF_MORPHOLOGY, 0, CK_ENUM, 0, 1}, {EFF_MORPHOLOGY, 1, CK_CLAMP, 1, 100}, {EFF_MORPHOLOGY, 2, CK_CLAMP, 1, 100},
    {EFF_POINTDIFFUSE, 1, CK_CLAMP, 0, 10000}, {EFF_POINTDIFFUSE, 2, CK_CLAMP, -10000, 10000}, {EFF_POINTDIFFUSE, 4, CK_CLAMP, 0.01f, 100},
    {EFF_POINTDIFFUSE, 5, CK_ENUM, 0, 5},
    {EFF_POINTSPECULAR, 1, CK_CLAMP, -10000, 10000}, {EFF_POINTSPECULAR, 2, CK_CLAMP, 0, 10000}, {EFF_POINTSPECULAR, 3, CK_CLAMP, -10000, 10000},
    {EFF_POINTSPECULAR, 5, CK_CLAMP, 0.01f, 100}, {EFF_POINTSPECULAR, 6, CK_ENUM, 0, 5},
    {EFF_SPOTDIFFUSE, 2, CK_CLAMP, -10000, 10000}, {EFF_SPOTDIFFUSE, 3, CK_CLAMP, -90, 90}, {EFF_SPOTDIFFUSE, 4, CK_CLAMP, 0, 10000},
    {EFF_SPOTDIFFUSE, 5, CK_CLAMP, -10000, 10000}, {EFF_SPOTDIFFUSE, 7, CK_CLAMP, 0.01f, 100}, {EFF_SPOTDIFFUSE, 8, CK_ENUM, 0, 5},
    {EFF_SPOTSPECULAR, 2, CK_CLAMP, -10000, 10000}, {EFF_SPOTSPECULAR, 3, CK_CLAMP, -90, 90}, {EFF_SPOTSPECULAR, 4, CK_CLAMP, -10000, 10000},
    {EFF_SPOTSPECULAR, 5, CK_CLAMP, 0, 10000}, {EFF_SPOTSPECULAR, 6, CK_CLAMP, -10000, 10000}, {EFF_SPOTSPECULAR, 8, CK_CLAMP, 0.01f, 100},
    {EFF_SPOTSPECULAR, 9, CK_ENUM, 0, 5},
    {EFF_TURBULENCE, 1, CK_CLAMP, 0, __builtin_inff()}, {EFF_TURBULENCE, 2, CK_CLAMP, 0, 1000}, {EFF_TURBULENCE, 3, CK_CLAMP, 1, 15},
    {EFF_TURBULENCE, 4, CK_CLAMP, -10000, 10000}, {EFF_TURBULENCE, 5, CK_ENUM, 0, 1},
    {EFF_YCBCR, 0, CK_ENUM, 0, 4}, {EFF_YCBCR, 2, CK_ENUM, 0, 5},
    {EFF_RGBTOHUE, 0, CK_ENUM, 0, 1}, {EFF_HUETORGB, 0, CK_ENUM, 0, 1},
    {EFF_CHROMAKEY, 1, CK_CLAMP, 0, 1},
    {EFF_EMBOSS, 0, CK_CLAMP, 0, 10}, {EFF_EMBOSS, 1, CK_CLAMP, 0, 360},
    {EFF_POSTERIZE, 0, CK_CLAMP, 2, 16}, {EFF_POSTERIZE, 1, CK_CLAMP, 2, 16}, {EFF_POSTERIZE, 2, CK_CLAMP, 2, 16},
    {EFF_SHARPEN, 0, CK_CLAMP, 0, 10}, {EFF_SHARPEN, 1, CK_CLAMP, 0, 10},
    {EFF_STRAIGHTEN, 0, CK_CLAMP, -45, 45}, {EFF_STRAIGHTEN, 2, CK_ENUM, 0, 4},
    {EFF_TEMPERATURETINT, 0, CK_CLAMP, -1, 1}, {EFF_TEMPERATURETINT, 1, CK_CLAMP, -1, 1},
    {EFF_VIGNETTE, 1, CK_CLAMP, 0, 1}, {EFF_VIGNETTE, 2, CK_CLAMP, 0, 1},
    {EFF_EDGEDETECTION, 0, CK_CLAMP, 0, 1}, {EFF_EDGEDETECTION, 1, CK_CLAMP, 0, 10}, {EFF_EDGEDETECTION, 2, CK_ENUM, 0, 1},
    {EFF_EDGEDETECTION, 4, CK_ENUM1, 1, 2},
    {EFF_HIGHLIGHTSSHADOWS, 0, CK_CLAMP, -1, 1}, {EFF_HIGHLIGHTSSHADOWS, 1, CK_CLAMP, -1, 1}, {EFF_HIGHLIGHTSSHADOWS, 2, CK_CLAMP, -1, 1},
    {EFF_HIGHLIGHTSSHADOWS, 3, CK_ENUM, 0, 1}, {EFF_HIGHLIGHTSSHADOWS, 4, CK_CLAMP, 0, 10},
    {EFF_LOOKUPTABLE3D, 1, CK_ENUM1, 1, 2},
    {EFF_CROSSFADE, 0, CK_CLAMP, 0, 1},
    {EFF_HDRTONEMAP, 2, CK_ENUM, 0, 1},
};

static float wrap360(float v)
{
    float r = v - 360.0f * D2dFloorF(v / 360.0f);
    return r >= 360.0f ? 0.0f : r;
}

HRESULT D2dFxCheckValue(D2dEffect *e, UINT32 index, BYTE *data, UINT32 size)
{
    for (SIZE_T i = 0; i < sizeof(g_checks) / sizeof(g_checks[0]); i++) {
        const FxCheck *k = &g_checks[i];
        if (k->id != e->id || k->index != index)
            continue;
        switch (k->kind) {
        case CK_READONLY:
            return D2DERR_INVALID_PROPERTY;
        case CK_BLOBMAX:
            return size > k->hi ? E_INVALIDARG : S_OK;
        case CK_ENUM:
        case CK_ENUM1: {
            UINT32 v;
            memcpy(&v, data, 4);
            if (v < (UINT32)k->lo || v > (UINT32)k->hi)
                return E_INVALIDARG;
            if (e->id == EFF_BITMAPSOURCE && index == 2 && v > 2 && v != 6 && v != 7)
                return E_INVALIDARG;
            return S_OK;
        }
        case CK_WRAP: {
            float v;
            memcpy(&v, data, 4);
            v = wrap360(v);
            memcpy(data, &v, 4);
            return S_OK;
        }
        default: {
            UINT32 n = size / 4;
            BOOL integer = FALSE, sign = FALSE;
            if (e->id == EFF_CONVOLVE && (index == 2 || index == 3))
                integer = TRUE;
            if ((e->id == EFF_HISTOGRAM && index == 0) || (e->id == EFF_MORPHOLOGY && index > 0) || e->id == EFF_POSTERIZE ||
                (e->id == EFF_TURBULENCE && index == 3))
                integer = TRUE;
            if (e->id == EFF_TURBULENCE && index == 4)
                integer = sign = TRUE;
            for (UINT32 j = 0; j < n; j++) {
                if (integer) {
                    INT64 v;
                    if (sign) {
                        INT32 s;
                        memcpy(&s, data + j * 4, 4);
                        v = s;
                    } else {
                        UINT32 u;
                        memcpy(&u, data + j * 4, 4);
                        v = u;
                    }
                    if (v < (INT64)k->lo)
                        v = (INT64)k->lo;
                    if (v > (INT64)k->hi)
                        v = (INT64)k->hi;
                    UINT32 u = (UINT32)v;
                    memcpy(data + j * 4, &u, 4);
                } else {
                    float v;
                    memcpy(&v, data + j * 4, 4);
                    v = D2dClampF(v, k->lo, k->hi);
                    memcpy(data + j * 4, &v, 4);
                }
            }
            return S_OK;
        }
        }
    }
    return S_OK;
}

HRESULT D2dFxHistogram(D2dEffect *e, BYTE *data, UINT32 size)
{
    UINT32 bins = *D2dFxU(e, 0);
    if (size < bins * 4)
        return D2DERR_INSUFFICIENT_BUFFER_HR;
    memset(data, 0, bins * 4);
    if (e->histogram && e->histogram_bins == bins)
        memcpy(data, e->histogram, bins * 4);
    return S_OK;
}

/* ------------------------------------------------------------------ 3d lookup tables */

static const void *const g_lut_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory};
D2D_IIDS(g_lut_iids, &IID_ID2D1Resource, &IID_ID2D1LookupTable3D);
static void lut_destroy(D2dObj *o) { D2dFree(((D2dLut3D *)o)->data); }
static D2dClass g_lut_class = {"lookup table 3d", g_lut_iids, lut_destroy, nullptr};

D2dLut3D *D2dLut3DOf(void *p) { return D2dIsOfClass(p, &g_lut_class) ? p : nullptr; }

static float half_float(uint16_t h)
{
    uint32_t sign = (uint32_t)(h & 0x8000) << 16, exp = (h >> 10) & 31, man = h & 1023;
    union {
        uint32_t u;
        float f;
    } v;
    if (!exp) {
        float f = (float)man * (1.0f / 1024.0f) * 6.103515625e-5f;
        return sign ? -f : f;
    }
    v.u = sign | (exp == 31 ? 0x7f800000u | man << 13 : (exp + 112) << 23 | man << 13);
    return v.f;
}

HRESULT D2dLut3DCreate(D2dFactory *f, D2D1_BUFFER_PRECISION precision, const UINT32 *extents, const BYTE *data, UINT32 size,
                       const UINT32 *strides, ID2D1LookupTable3D **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!extents || !data || !strides)
        return E_INVALIDARG;
    UINT32 texel;
    switch (precision) {
    case D2D1_BUFFER_PRECISION_8BPC_UNORM:
    case D2D1_BUFFER_PRECISION_8BPC_UNORM_SRGB:
        texel = 4;
        break;
    case D2D1_BUFFER_PRECISION_16BPC_UNORM:
    case D2D1_BUFFER_PRECISION_16BPC_FLOAT:
        texel = 8;
        break;
    case D2D1_BUFFER_PRECISION_32BPC_FLOAT:
        texel = 16;
        break;
    default:
        return E_INVALIDARG;
    }
    for (int i = 0; i < 3; i++) {
        if (!extents[i] || extents[i] > 256)
            return E_INVALIDARG;
    }
    /* rows run along blue, then green, slices along red. */
    UINT64 plane = (UINT64)texel * extents[0] * extents[1];
    if (strides[0] < texel * extents[0] || strides[1] < plane)
        return E_INVALIDARG;
    if ((UINT64)strides[1] * extents[2] > 0xffffffffu)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    if (size < strides[1] * extents[2])
        return E_UNEXPECTED;
    D2dLut3D *l = D2dNew(sizeof(*l), g_lut_vtbl, &g_lut_class, f);
    if (!l)
        return E_OUTOFMEMORY;
    memcpy(l->n, extents, sizeof(l->n));
    l->data = D2dAlloc((SIZE_T)extents[0] * extents[1] * extents[2] * 16);
    if (!l->data) {
        D2dRelease(l);
        return E_OUTOFMEMORY;
    }
    float *d = l->data;
    for (UINT32 z = 0; z < extents[2]; z++) {
        for (UINT32 y = 0; y < extents[1]; y++) {
            const BYTE *row = data + (SIZE_T)z * strides[1] + (SIZE_T)y * strides[0];
            for (UINT32 x = 0; x < extents[0]; x++, d += 4) {
                for (int k = 0; k < 4; k++) {
                    switch (precision) {
                    case D2D1_BUFFER_PRECISION_16BPC_UNORM:
                        d[k] = ((const uint16_t *)row)[x * 4 + k] * (1.0f / 65535.0f);
                        break;
                    case D2D1_BUFFER_PRECISION_16BPC_FLOAT:
                        d[k] = half_float(((const uint16_t *)row)[x * 4 + k]);
                        break;
                    case D2D1_BUFFER_PRECISION_32BPC_FLOAT:
                        d[k] = ((const float *)row)[x * 4 + k];
                        break;
                    case D2D1_BUFFER_PRECISION_8BPC_UNORM_SRGB:
                        d[k] = k == 3 ? row[x * 4 + k] * (1.0f / 255.0f) : D2dSrgbToLinear(row[x * 4 + k] * (1.0f / 255.0f));
                        break;
                    default:
                        d[k] = row[x * 4 + k] * (1.0f / 255.0f);
                        break;
                    }
                }
            }
        }
    }
    *out = (ID2D1LookupTable3D *)l;
    return S_OK;
}
