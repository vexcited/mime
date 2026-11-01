/*
 * images: bitmaps, effects and command lists as pixels, and the built in effects that have a renderer here.
 */
#include "d2d_private.h"
#include <float.h>

#define INF_EXTENT (1 << 24)

typedef struct IRect {
    int x0, y0, x1, y1;
} IRect;

static IRect irect_intersect(IRect a, IRect b)
{
    IRect r = {a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1};
    return r;
}
static IRect irect_union(IRect a, IRect b)
{
    if (a.x1 <= a.x0 || a.y1 <= a.y0)
        return b;
    if (b.x1 <= b.x0 || b.y1 <= b.y0)
        return a;
    IRect r = {a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
    return r;
}
static BOOL irect_empty(IRect r) { return r.x1 <= r.x0 || r.y1 <= r.y0; }

void D2dImageDataFree(D2dImageData *d) { D2dSurfaceFree(&d->surface); }

static HRESULT data_new(D2dImageData *d, IRect r)
{
    d->x = r.x0;
    d->y = r.y0;
    int w = r.x1 - r.x0, h = r.y1 - r.y0;
    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;
    return D2dSurfaceInit(&d->surface, w, h) ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ command lists */

typedef struct D2dCommandList {
    D2dObj obj;
    D2dSurface surface;
    float dpi;
    BOOL closed;
    IRect dirty;
} D2dCommandList;

static D2dClass g_cmd_class;

HRESULT D2dCommandListSurface(ID2D1CommandList *list, D2dSurface **surface, float *dpi)
{
    if (!D2dIsOfClass(list, &g_cmd_class))
        return E_INVALIDARG;
    D2dCommandList *c = (D2dCommandList *)list;
    if (c->closed)
        return D2DERR_WRONG_STATE;
    *surface = &c->surface;
    *dpi = c->dpi;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cl_stream(ID2D1CommandList *This, ID2D1CommandSink *sink)
{
    D2dCommandList *c = (D2dCommandList *)This;
    if (!sink)
        return E_INVALIDARG;
    if (!c->closed)
        return D2DERR_WRONG_STATE;
    /* the recorded pixels go out as one bitmap, since commands are drawn when they are recorded. */
    D2dBitmap *b;
    HRESULT hr = D2dBitmapCreate(c->obj.factory, (UINT32)c->surface.w, (UINT32)c->surface.h, (DXGI_FORMAT)87, D2D1_ALPHA_MODE_PREMULTIPLIED,
                                 c->dpi, c->dpi, D2D1_BITMAP_OPTIONS_NONE, TRUE, &b);
    if (FAILED(hr))
        return hr;
    memcpy(b->native, c->surface.px, (SIZE_T)c->surface.w * c->surface.h * 4);
    hr = sink->lpVtbl->BeginDraw(sink);
    if (SUCCEEDED(hr))
        hr = sink->lpVtbl->DrawBitmap(sink, (ID2D1Bitmap *)b, nullptr, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
    if (SUCCEEDED(hr))
        hr = sink->lpVtbl->EndDraw(sink);
    D2dRelease(b);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cl_close(ID2D1CommandList *This)
{
    D2dCommandList *c = (D2dCommandList *)This;
    if (c->closed)
        return D2DERR_WRONG_STATE;
    c->closed = TRUE;
    return S_OK;
}
static void cl_destroy(D2dObj *o) { D2dSurfaceFree(&((D2dCommandList *)o)->surface); }
static const void *const g_cmd_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, cl_stream, cl_close};
D2D_IIDS(g_cmd_iids, &IID_ID2D1Resource, &IID_ID2D1Image, &IID_ID2D1CommandList);
static D2dClass g_cmd_class = {"command list", g_cmd_iids, cl_destroy, nullptr};

HRESULT D2dCreateCommandList(D2dRt *rt, ID2D1CommandList **out)
{
    D2dCommandList *c = D2dNew(sizeof(*c), g_cmd_vtbl, &g_cmd_class, rt->obj.factory);
    if (!c)
        return E_OUTOFMEMORY;
    c->dpi = rt->dpi_x;
    if (!D2dSurfaceInit(&c->surface, 2048, 2048)) {
        D2dRelease(c);
        return E_OUTOFMEMORY;
    }
    *out = (ID2D1CommandList *)c;
    return S_OK;
}

/* ------------------------------------------------------------------ effects */

enum {
    EFF_FLOOD, EFF_COLORMATRIX, EFF_BLUR, EFF_COMPOSITE, EFF_CROP, EFF_AFFINE, EFF_OPACITY, EFF_TINT, EFF_SHADOW,
    EFF_BRIGHTNESS, EFF_SATURATION, EFF_GRAYSCALE, EFF_INVERT, EFF_SEPIA, EFF_PREMULTIPLY, EFF_UNPREMULTIPLY, EFF_BLEND,
    EFF_BORDER, EFF_ARITHMETIC, EFF_HUEROTATE, EFF_CONTRAST, EFF_EXPOSURE, EFF_LUMINANCE, EFF_TILE, EFF_COUNT
};

typedef struct EffProp {
    const WCHAR *name;
    D2D1_PROPERTY_TYPE type;
    UINT32 size;
} EffProp;

typedef struct EffDesc {
    const GUID *clsid;
    const WCHAR *name;
    UINT32 min_inputs, max_inputs, default_inputs;
    UINT32 nprops;
    EffProp props[5];
} EffDesc;

#define P_F(n) {L##n, D2D1_PROPERTY_TYPE_FLOAT, 4}
#define P_V2(n) {L##n, D2D1_PROPERTY_TYPE_VECTOR2, 8}
#define P_V4(n) {L##n, D2D1_PROPERTY_TYPE_VECTOR4, 16}
#define P_E(n) {L##n, D2D1_PROPERTY_TYPE_ENUM, 4}
#define P_B(n) {L##n, D2D1_PROPERTY_TYPE_BOOL, 4}
#define P_M(n) {L##n, D2D1_PROPERTY_TYPE_MATRIX_3X2, 24}
#define P_M5(n) {L##n, D2D1_PROPERTY_TYPE_MATRIX_5X4, 80}

static const EffDesc g_effects[EFF_COUNT] = {
    [EFF_FLOOD] = {&CLSID_D2D1Flood, L"Flood", 0, 0, 0, 1, {P_V4("Color")}},
    [EFF_COLORMATRIX] = {&CLSID_D2D1ColorMatrix, L"Color Matrix", 1, 1, 1, 3, {P_M5("ColorMatrix"), P_E("AlphaMode"), P_B("ClampOutput")}},
    [EFF_BLUR] = {&CLSID_D2D1GaussianBlur, L"Gaussian Blur", 1, 1, 1, 3, {P_F("StandardDeviation"), P_E("Optimization"), P_E("BorderMode")}},
    [EFF_COMPOSITE] = {&CLSID_D2D1Composite, L"Composite", 1, 0xffffffff, 2, 1, {P_E("Mode")}},
    [EFF_CROP] = {&CLSID_D2D1Crop, L"Crop", 1, 1, 1, 2, {P_V4("Rect"), P_E("BorderMode")}},
    [EFF_AFFINE] = {&CLSID_D2D12DAffineTransform, L"2D Affine Transform", 1, 1, 1, 3, {P_E("InterpolationMode"), P_E("BorderMode"), P_M("TransformMatrix")}},
    [EFF_OPACITY] = {&CLSID_D2D1Opacity, L"Opacity", 1, 1, 1, 1, {P_F("Opacity")}},
    [EFF_TINT] = {&CLSID_D2D1Tint, L"Tint", 1, 1, 1, 2, {P_V4("Color"), P_B("ClampOutput")}},
    [EFF_SHADOW] = {&CLSID_D2D1Shadow, L"Shadow", 1, 1, 1, 3, {P_F("BlurStandardDeviation"), P_V4("Color"), P_E("Optimization")}},
    [EFF_BRIGHTNESS] = {&CLSID_D2D1Brightness, L"Brightness", 1, 1, 1, 3, {P_V2("WhitePoint"), P_V2("BlackPoint"), P_B("ClampOutput")}},
    [EFF_SATURATION] = {&CLSID_D2D1Saturation, L"Saturation", 1, 1, 1, 1, {P_F("Saturation")}},
    [EFF_GRAYSCALE] = {&CLSID_D2D1Grayscale, L"Grayscale", 1, 1, 1, 0, {}},
    [EFF_INVERT] = {&CLSID_D2D1Invert, L"Invert", 1, 1, 1, 0, {}},
    [EFF_SEPIA] = {&CLSID_D2D1Sepia, L"Sepia", 1, 1, 1, 2, {P_F("Intensity"), P_E("AlphaMode")}},
    [EFF_PREMULTIPLY] = {&CLSID_D2D1Premultiply, L"Premultiply", 1, 1, 1, 0, {}},
    [EFF_UNPREMULTIPLY] = {&CLSID_D2D1UnPremultiply, L"Unpremultiply", 1, 1, 1, 0, {}},
    [EFF_BLEND] = {&CLSID_D2D1Blend, L"Blend", 2, 2, 2, 1, {P_E("Mode")}},
    [EFF_BORDER] = {&CLSID_D2D1Border, L"Border", 1, 1, 1, 2, {P_E("EdgeModeX"), P_E("EdgeModeY")}},
    [EFF_ARITHMETIC] = {&CLSID_D2D1ArithmeticComposite, L"Arithmetic Composite", 2, 2, 2, 2, {P_V4("Coefficients"), P_B("ClampOutput")}},
    [EFF_HUEROTATE] = {&CLSID_D2D1HueRotation, L"Hue Rotation", 1, 1, 1, 1, {P_F("Angle")}},
    [EFF_CONTRAST] = {&CLSID_D2D1Contrast, L"Contrast", 1, 1, 1, 2, {P_F("Contrast"), P_B("ClampInput")}},
    [EFF_EXPOSURE] = {&CLSID_D2D1Exposure, L"Exposure", 1, 1, 1, 1, {P_F("ExposureValue")}},
    [EFF_LUMINANCE] = {&CLSID_D2D1LuminanceToAlpha, L"Luminance To Alpha", 1, 1, 1, 0, {}},
    [EFF_TILE] = {&CLSID_D2D1Tile, L"Tile", 1, 1, 1, 1, {P_V4("Rect")}},
};

struct D2dEffect {
    D2dObj obj;
    int id;
    BYTE props[5][80];
    ID2D1Image *inputs[8];
    UINT32 input_count;
    BOOL cached;
    D2D1_BUFFER_PRECISION precision;
    D2dRt *rt;
};

static D2dClass g_effect_class;

static float *pf(D2dEffect *e, int i) { return (float *)e->props[i]; }
static UINT32 *pu(D2dEffect *e, int i) { return (UINT32 *)e->props[i]; }

static void effect_defaults(D2dEffect *e)
{
    switch (e->id) {
    case EFF_FLOOD:
        pf(e, 0)[3] = 1.0f;
        break;
    case EFF_COLORMATRIX:
        for (int i = 0; i < 4; i++)
            pf(e, 0)[i * 5] = 1.0f;
        *pu(e, 1) = D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED;
        break;
    case EFF_BLUR:
        *pu(e, 1) = 1;
        break;
    case EFF_CROP:
        pf(e, 0)[0] = pf(e, 0)[1] = -FLT_MAX;
        pf(e, 0)[2] = pf(e, 0)[3] = FLT_MAX;
        break;
    case EFF_AFFINE:
        *pu(e, 0) = 1;
        pf(e, 2)[0] = pf(e, 2)[3] = 1.0f;
        break;
    case EFF_OPACITY:
        pf(e, 0)[0] = 1.0f;
        break;
    case EFF_TINT:
        pf(e, 0)[0] = pf(e, 0)[1] = pf(e, 0)[2] = pf(e, 0)[3] = 1.0f;
        break;
    case EFF_SHADOW:
        pf(e, 0)[0] = 3.0f;
        pf(e, 1)[3] = 0.75f;
        *pu(e, 2) = 1;
        break;
    case EFF_BRIGHTNESS:
        pf(e, 0)[0] = pf(e, 0)[1] = 1.0f;
        break;
    case EFF_SATURATION:
        pf(e, 0)[0] = 0.5f;
        break;
    case EFF_SEPIA:
        pf(e, 0)[0] = 0.5f;
        *pu(e, 1) = D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED;
        break;
    case EFF_ARITHMETIC:
        pf(e, 0)[0] = 1.0f;
        break;
    case EFF_TILE:
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ image access */

static float ctx_scale(D2dRt *rt) { return rt->dpi_x / 96.0f; }

static HRESULT image_get(D2dRt *rt, ID2D1Image *image, IRect want, D2dImageData *out, int depth);
static HRESULT image_bounds(D2dRt *rt, ID2D1Image *image, IRect *b, int depth);

/* the image an effect draws is an object of its own that keeps the effect alive. */
typedef struct D2dEffOut {
    D2dObj obj;
    D2dEffect *effect;
} D2dEffOut;

static D2dClass g_effout_class;

static D2dEffect *effect_of(void *p)
{
    if (D2dIsOfClass(p, &g_effect_class))
        return (D2dEffect *)p;
    if (D2dIsOfClass(p, &g_effout_class))
        return ((D2dEffOut *)p)->effect;
    return nullptr;
}

static HRESULT bitmap_data(D2dRt *rt, D2dBitmap *b, D2dImageData *out)
{
    const D2dSurface *v = D2dBitmapView(b);
    if (!v)
        return E_OUTOFMEMORY;
    float sx = rt->dpi_x / b->dpi_x, sy = rt->dpi_y / b->dpi_y;
    int w = sx == 1.0f ? v->w : (int)(v->w * sx + 0.5f), h = sy == 1.0f ? v->h : (int)(v->h * sy + 0.5f);
    IRect r = {0, 0, w, h};
    HRESULT hr = data_new(out, r);
    if (FAILED(hr))
        return hr;
    if (w == v->w && h == v->h) {
        memcpy(out->surface.px, v->px, (SIZE_T)w * h * 4);
        return S_OK;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float u = (x + 0.5f) / sx - 0.5f, vv = (y + 0.5f) / sy - 0.5f;
            int i0 = (int)D2dFloorF(u), j0 = (int)D2dFloorF(vv);
            float fu = u - i0, fv = vv - j0;
            int i1 = i0 + 1, j1 = j0 + 1;
            i0 = i0 < 0 ? 0 : (i0 >= v->w ? v->w - 1 : i0);
            i1 = i1 < 0 ? 0 : (i1 >= v->w ? v->w - 1 : i1);
            j0 = j0 < 0 ? 0 : (j0 >= v->h ? v->h - 1 : j0);
            j1 = j1 < 0 ? 0 : (j1 >= v->h ? v->h - 1 : j1);
            uint32_t p[4] = {v->px[(SIZE_T)j0 * v->w + i0], v->px[(SIZE_T)j0 * v->w + i1], v->px[(SIZE_T)j1 * v->w + i0], v->px[(SIZE_T)j1 * v->w + i1]};
            uint32_t o = 0;
            for (int s = 0; s < 32; s += 8) {
                float t = ((p[0] >> s) & 255) * (1 - fu) * (1 - fv) + ((p[1] >> s) & 255) * fu * (1 - fv) + ((p[2] >> s) & 255) * (1 - fu) * fv + ((p[3] >> s) & 255) * fu * fv;
                o |= (uint32_t)(t + 0.5f) << s;
            }
            D2dSurfaceRow(&out->surface, y)[x] = o;
        }
    }
    return S_OK;
}

/* copies the part of src that falls into want, with transparent edges. */
static HRESULT crop_data(const D2dImageData *src, IRect want, D2dImageData *out)
{
    HRESULT hr = data_new(out, want);
    if (FAILED(hr))
        return hr;
    for (int y = want.y0; y < want.y1; y++) {
        int sy = y - src->y;
        if (sy < 0 || sy >= src->surface.h)
            continue;
        for (int x = want.x0; x < want.x1; x++) {
            int sx = x - src->x;
            if (sx >= 0 && sx < src->surface.w)
                D2dSurfaceRow(&out->surface, y - want.y0)[x - want.x0] = D2dSurfaceRow(&src->surface, sy)[sx];
        }
    }
    return S_OK;
}

/* an input of an effect over an area, or transparent black for a missing one. */
static HRESULT input_area(D2dRt *rt, D2dEffect *e, UINT32 i, IRect want, D2dImageData *out, int depth)
{
    if (i >= e->input_count || !e->inputs[i]) {
        return data_new(out, want);
    }
    D2dImageData raw = {};
    HRESULT hr = image_get(rt, e->inputs[i], want, &raw, depth + 1);
    if (FAILED(hr))
        return hr;
    hr = crop_data(&raw, want, out);
    D2dImageDataFree(&raw);
    return hr;
}


/* straight color of a premultiplied pixel as floats 0..1 */
static void unpack(uint32_t p, float c[4])
{
    float a = (p >> 24) / 255.0f;
    c[3] = a;
    if (a > 0) {
        c[0] = ((p >> 16) & 255) / 255.0f / a;
        c[1] = ((p >> 8) & 255) / 255.0f / a;
        c[2] = (p & 255) / 255.0f / a;
    } else {
        c[0] = c[1] = c[2] = 0;
    }
}

static void gaussian(D2dSurface *s, float sigma)
{
    if (!(sigma > 0.0f))
        return;
    int radius = (int)(sigma * 3.0f + 0.999f);
    if (radius < 1)
        radius = 1;
    float *k = D2dAlloc((SIZE_T)(2 * radius + 1) * sizeof(float));
    float *tmp = D2dAlloc((SIZE_T)s->w * s->h * 4 * sizeof(float));
    if (!k || !tmp) {
        D2dFree(k);
        D2dFree(tmp);
        return;
    }
    float sum = 0;
    for (int i = -radius; i <= radius; i++) {
        k[i + radius] = D2dExpF(-(i * i) / (2 * sigma * sigma));
        sum += k[i + radius];
    }
    for (int i = 0; i <= 2 * radius; i++)
        k[i] /= sum;
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            float a[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; i++) {
                int xx = x + i;
                if (xx < 0 || xx >= s->w)
                    continue;
                uint32_t p = D2dSurfaceRow(s, y)[xx];
                float w = k[i + radius];
                a[0] += ((p >> 24)) * w;
                a[1] += ((p >> 16) & 255) * w;
                a[2] += ((p >> 8) & 255) * w;
                a[3] += (p & 255) * w;
            }
            float *t = tmp + ((SIZE_T)y * s->w + x) * 4;
            t[0] = a[0]; t[1] = a[1]; t[2] = a[2]; t[3] = a[3];
        }
    }
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            float a[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; i++) {
                int yy = y + i;
                if (yy < 0 || yy >= s->h)
                    continue;
                const float *t = tmp + ((SIZE_T)yy * s->w + x) * 4;
                float w = k[i + radius];
                a[0] += t[0] * w; a[1] += t[1] * w; a[2] += t[2] * w; a[3] += t[3] * w;
            }
            D2dSurfaceRow(s, y)[x] = ((uint32_t)(a[0] + 0.5f) << 24) | ((uint32_t)(a[1] + 0.5f) << 16) | ((uint32_t)(a[2] + 0.5f) << 8) | (uint32_t)(a[3] + 0.5f);
        }
    }
    D2dFree(k);
    D2dFree(tmp);
}

static uint32_t blend_mode_px(uint32_t d, uint32_t s, UINT32 mode)
{
    float dc[4], sc[4];
    unpack(d, dc);
    unpack(s, sc);
    float ad = dc[3], as = sc[3], ar = as + ad - as * ad, o[3];
    for (int i = 0; i < 3; i++) {
        float cb = dc[i], cs = sc[i], b;
        switch (mode) {
        case D2D1_BLEND_MODE_MULTIPLY: b = cb * cs; break;
        case D2D1_BLEND_MODE_SCREEN: b = cb + cs - cb * cs; break;
        case D2D1_BLEND_MODE_DARKEN: b = D2dMinF(cb, cs); break;
        case D2D1_BLEND_MODE_LIGHTEN: b = D2dMaxF(cb, cs); break;
        case D2D1_BLEND_MODE_LINEAR_BURN: b = D2dMaxF(cb + cs - 1, 0); break;
        case D2D1_BLEND_MODE_LINEAR_DODGE: b = D2dMinF(cb + cs, 1); break;
        case D2D1_BLEND_MODE_OVERLAY: b = cb <= 0.5f ? 2 * cb * cs : 1 - 2 * (1 - cb) * (1 - cs); break;
        case D2D1_BLEND_MODE_HARD_LIGHT: b = cs <= 0.5f ? 2 * cb * cs : 1 - 2 * (1 - cb) * (1 - cs); break;
        case D2D1_BLEND_MODE_DIFFERENCE: b = cb > cs ? cb - cs : cs - cb; break;
        case D2D1_BLEND_MODE_EXCLUSION: b = cb + cs - 2 * cb * cs; break;
        case D2D1_BLEND_MODE_COLOR_BURN: b = cs <= 0 ? 0 : D2dMaxF(1 - (1 - cb) / cs, 0); break;
        case D2D1_BLEND_MODE_COLOR_DODGE: b = cs >= 1 ? 1 : D2dMinF(cb / (1 - cs), 1); break;
        case D2D1_BLEND_MODE_SUBTRACT: b = D2dMaxF(cb - cs, 0); break;
        case D2D1_BLEND_MODE_DIVISION: b = cs <= 0 ? 1 : D2dMinF(cb / cs, 1); break;
        default: b = cs; break;
        }
        /* the standard compositing formula with the blend function in place of the source color. */
        float co = (1 - as) * ad * cb + (1 - ad) * as * cs + as * ad * b;
        o[i] = ar > 0 ? co / ar : 0;
        (void)o;
        o[i] = co;
    }
    return ((uint32_t)(D2dClampF(ar, 0, 1) * 255 + 0.5f) << 24) | ((uint32_t)(D2dClampF(o[0], 0, 1) * 255 + 0.5f) << 16) |
           ((uint32_t)(D2dClampF(o[1], 0, 1) * 255 + 0.5f) << 8) | (uint32_t)(D2dClampF(o[2], 0, 1) * 255 + 0.5f);
}

static uint32_t composite_px(uint32_t d, uint32_t s, UINT32 mode)
{
    uint32_t sa = s >> 24, da = d >> 24;
    uint32_t fs, fd;
    switch (mode) {
    case D2D1_COMPOSITE_MODE_DESTINATION_OVER: fs = 255 - da; fd = 255; break;
    case D2D1_COMPOSITE_MODE_SOURCE_IN: fs = da; fd = 0; break;
    case D2D1_COMPOSITE_MODE_DESTINATION_IN: fs = 0; fd = sa; break;
    case D2D1_COMPOSITE_MODE_SOURCE_OUT: fs = 255 - da; fd = 0; break;
    case D2D1_COMPOSITE_MODE_DESTINATION_OUT: fs = 0; fd = 255 - sa; break;
    case D2D1_COMPOSITE_MODE_SOURCE_ATOP: fs = da; fd = 255 - sa; break;
    case D2D1_COMPOSITE_MODE_DESTINATION_ATOP: fs = 255 - da; fd = sa; break;
    case D2D1_COMPOSITE_MODE_XOR: fs = 255 - da; fd = 255 - sa; break;
    case D2D1_COMPOSITE_MODE_PLUS: fs = 255; fd = 255; break;
    case D2D1_COMPOSITE_MODE_SOURCE_COPY:
    case D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY: fs = 255; fd = 0; break;
    case D2D1_COMPOSITE_MODE_MASK_INVERT: fs = 0; fd = 255 - sa; break;
    default: fs = 255; fd = 255 - sa; break;
    }
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t v = mime_vec_div255(((s >> sh) & 255) * fs) + mime_vec_div255(((d >> sh) & 255) * fd);
        out |= (v > 255 ? 255 : v) << sh;
    }
    return out;
}

static HRESULT effect_render(D2dRt *rt, D2dEffect *e, IRect want, D2dImageData *out, int depth);

static HRESULT effect_bounds(D2dRt *rt, D2dEffect *e, IRect *b, int depth)
{
    IRect in = {0, 0, 0, 0};
    BOOL have = FALSE;
    for (UINT32 i = 0; i < e->input_count; i++) {
        IRect r = {0, 0, 0, 0};
        if (e->inputs[i] && SUCCEEDED(image_bounds(rt, e->inputs[i], &r, depth + 1))) {
            in = have ? irect_union(in, r) : r;
            have = TRUE;
        }
    }
    float s = ctx_scale(rt);
    switch (e->id) {
    case EFF_FLOOD:
    case EFF_BORDER:
    case EFF_TILE:
        in.x0 = in.y0 = -INF_EXTENT;
        in.x1 = in.y1 = INF_EXTENT;
        break;
    case EFF_BLUR:
    case EFF_SHADOW: {
        int r = (int)(pf(e, 0)[0] * s * 3.0f + 0.999f);
        in.x0 -= r; in.y0 -= r; in.x1 += r; in.y1 += r;
        break;
    }
    case EFF_CROP: {
        const float *v = pf(e, 0);
        IRect c = {v[0] <= -1e30f ? -INF_EXTENT : (int)D2dFloorF(v[0] * s), v[1] <= -1e30f ? -INF_EXTENT : (int)D2dFloorF(v[1] * s),
                   v[2] >= 1e30f ? INF_EXTENT : (int)(v[2] * s + 0.999f), v[3] >= 1e30f ? INF_EXTENT : (int)(v[3] * s + 0.999f)};
        in = irect_intersect(in, c);
        break;
    }
    case EFF_AFFINE: {
        const float *m = pf(e, 2);
        D2D1_POINT_2F c[4] = {{(float)in.x0, (float)in.y0}, {(float)in.x1, (float)in.y0}, {(float)in.x1, (float)in.y1}, {(float)in.x0, (float)in.y1}};
        float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
        for (int i = 0; i < 4; i++) {
            float x = c[i].x * m[0] + c[i].y * m[2] + m[4] * s, y = c[i].x * m[1] + c[i].y * m[3] + m[5] * s;
            x0 = D2dMinF(x0, x); y0 = D2dMinF(y0, y); x1 = D2dMaxF(x1, x); y1 = D2dMaxF(y1, y);
        }
        in.x0 = (int)D2dFloorF(x0); in.y0 = (int)D2dFloorF(y0); in.x1 = (int)(x1 + 0.999f); in.y1 = (int)(y1 + 0.999f);
        break;
    }
    default:
        break;
    }
    *b = in;
    return S_OK;
}

static HRESULT image_bounds(D2dRt *rt, ID2D1Image *image, IRect *b, int depth)
{
    if (depth > 16)
        return D2DERR_CYCLIC_GRAPH;
    D2dBitmap *bm = D2dBitmapOf(image);
    if (bm) {
        b->x0 = b->y0 = 0;
        b->x1 = (int)(bm->width * rt->dpi_x / bm->dpi_x + 0.5f);
        b->y1 = (int)(bm->height * rt->dpi_y / bm->dpi_y + 0.5f);
        return S_OK;
    }
    D2dEffect *e = effect_of(image);
    if (e)
        return effect_bounds(rt, e, b, depth);
    if (D2dIsOfClass(image, &g_cmd_class)) {
        D2dCommandList *c = (D2dCommandList *)image;
        b->x0 = b->y0 = 0;
        b->x1 = c->surface.w;
        b->y1 = c->surface.h;
        return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT image_get(D2dRt *rt, ID2D1Image *image, IRect want, D2dImageData *out, int depth)
{
    memset(out, 0, sizeof(*out));
    if (depth > 16)
        return D2DERR_CYCLIC_GRAPH;
    D2dBitmap *bm = D2dBitmapOf(image);
    if (bm)
        return bitmap_data(rt, bm, out);
    D2dEffect *e = effect_of(image);
    if (e)
        return effect_render(rt, e, want, out, depth);
    if (D2dIsOfClass(image, &g_cmd_class)) {
        D2dCommandList *c = (D2dCommandList *)image;
        IRect r = {0, 0, c->surface.w, c->surface.h};
        HRESULT hr = data_new(out, r);
        if (SUCCEEDED(hr))
            memcpy(out->surface.px, c->surface.px, (SIZE_T)r.x1 * r.y1 * 4);
        return hr;
    }
    return E_INVALIDARG;
}

HRESULT D2dRenderImage(D2dRt *rt, ID2D1Image *image, D2dImageData *out)
{
    IRect want = {0, 0, rt->draw ? rt->draw->w : 0, rt->draw ? rt->draw->h : 0};
    IRect b;
    if (SUCCEEDED(image_bounds(rt, image, &b, 0)))
        want = irect_intersect(b, (IRect){want.x0 - 64, want.y0 - 64, want.x1 + 64, want.y1 + 64});
    return image_get(rt, image, want, out, 0);
}

HRESULT D2dImageBounds(D2dRt *rt, ID2D1Image *image, D2D1_RECT_F *bounds)
{
    IRect b;
    HRESULT hr = image_bounds(rt, image, &b, 0);
    if (FAILED(hr))
        return hr;
    float inv = 96.0f / rt->dpi_x, invy = 96.0f / rt->dpi_y;
    if (b.x0 <= -INF_EXTENT) {
        bounds->left = bounds->top = -FLT_MAX;
        bounds->right = bounds->bottom = FLT_MAX;
        return S_OK;
    }
    bounds->left = b.x0 * inv;
    bounds->right = b.x1 * inv;
    bounds->top = b.y0 * invy;
    bounds->bottom = b.y1 * invy;
    return S_OK;
}

static HRESULT effect_render(D2dRt *rt, D2dEffect *e, IRect want, D2dImageData *out, int depth)
{
    float s = ctx_scale(rt);
    IRect natural;
    effect_bounds(rt, e, &natural, depth);
    IRect area = irect_intersect(want, natural);
    if (irect_empty(area))
        area = (IRect){want.x0, want.y0, want.x0, want.y0};
    HRESULT hr = S_OK;
    switch (e->id) {
    case EFF_FLOOD: {
        hr = data_new(out, area);
        if (FAILED(hr))
            break;
        const float *c = pf(e, 0);
        uint32_t px = D2dPackPremul(c[0], c[1], c[2], c[3]);
        for (int i = 0; i < out->surface.w * out->surface.h; i++)
            out->surface.px[i] = px;
        break;
    }
    case EFF_BLUR:
    case EFF_SHADOW: {
        float sigma = pf(e, 0)[0] * s;
        int r = (int)(sigma * 3.0f + 0.999f);
        IRect src_area = {area.x0 - r, area.y0 - r, area.x1 + r, area.y1 + r};
        D2dImageData in;
        hr = input_area(rt, e, 0, src_area, &in, depth);
        if (FAILED(hr))
            break;
        if (e->id == EFF_SHADOW) {
            const float *c = pf(e, 1);
            for (int i = 0; i < in.surface.w * in.surface.h; i++) {
                uint32_t a = in.surface.px[i] >> 24;
                in.surface.px[i] = D2dPackPremul(c[0], c[1], c[2], c[3] * a / 255.0f);
            }
        }
        gaussian(&in.surface, sigma);
        hr = crop_data(&in, area, out);
        D2dImageDataFree(&in);
        break;
    }
    case EFF_COMPOSITE:
    case EFF_BLEND:
    case EFF_ARITHMETIC: {
        hr = data_new(out, area);
        if (FAILED(hr))
            break;
        UINT32 mode = e->id == EFF_ARITHMETIC ? 0 : *pu(e, 0);
        if (e->input_count == 0)
            break;
        D2dImageData base;
        hr = input_area(rt, e, 0, area, &base, depth);
        if (FAILED(hr))
            break;
        memcpy(out->surface.px, base.surface.px, (SIZE_T)base.surface.w * base.surface.h * 4);
        D2dImageDataFree(&base);
        for (UINT32 i = 1; i < e->input_count; i++) {
            D2dImageData in;
            hr = input_area(rt, e, i, area, &in, depth);
            if (FAILED(hr))
                break;
            /* the first input is the destination, the later ones the sources. */
            for (int j = 0; j < out->surface.w * out->surface.h; j++) {
                uint32_t d = out->surface.px[j], sp = in.surface.px[j];
                if (e->id == EFF_COMPOSITE) {
                    out->surface.px[j] = composite_px(d, sp, mode);
                } else if (e->id == EFF_BLEND) {
                    /* in blend the first input is the background and the second the foreground. */
                    out->surface.px[j] = blend_mode_px(d, sp, mode);
                } else {
                    const float *k = pf(e, 0);
                    uint32_t o = 0;
                    for (int sh = 0; sh < 32; sh += 8) {
                        float sv = ((sp >> sh) & 255) / 255.0f, dv = ((d >> sh) & 255) / 255.0f;
                        float v = k[0] * sv * dv + k[1] * sv + k[2] * dv + k[3];
                        o |= (uint32_t)(D2dClampF(v, 0, 1) * 255 + 0.5f) << sh;
                    }
                    uint32_t a = o >> 24;
                    for (int sh = 0; sh < 24; sh += 8) {
                        uint32_t c = (o >> sh) & 255;
                        if (c > a)
                            o = (o & ~(255u << sh)) | (a << sh);
                    }
                    out->surface.px[j] = o;
                }
            }
            D2dImageDataFree(&in);
        }
        break;
    }
    case EFF_CROP: {
        D2dImageData in;
        hr = input_area(rt, e, 0, area, &in, depth);
        if (FAILED(hr))
            break;
        *out = in;
        break;
    }
    case EFF_AFFINE: {
        D2dImageData in;
        IRect ib;
        image_bounds(rt, e->inputs[0] ? e->inputs[0] : nullptr, &ib, depth + 1);
        hr = input_area(rt, e, 0, ib, &in, depth);
        if (FAILED(hr))
            break;
        hr = data_new(out, area);
        if (FAILED(hr)) {
            D2dImageDataFree(&in);
            break;
        }
        D2dMatrix m = {pf(e, 2)[0], pf(e, 2)[1], pf(e, 2)[2], pf(e, 2)[3], pf(e, 2)[4] * s, pf(e, 2)[5] * s}, inv;
        if (D2dMatInvert(&inv, &m)) {
            D2dPaint p;
            D2dMatrix to_dev = m;
            to_dev._31 += 0;
            MimeVecRect clamp = {0, 0, in.surface.w, in.surface.h};
            D2dMatrix tex_to_dev = {m._11, m._12, m._21, m._22, m._31 + in.x * m._11 + in.y * m._21, m._32 + in.x * m._12 + in.y * m._22};
            D2dMatrix to_out = {1, 0, 0, 1, (float)-area.x0, (float)-area.y0};
            D2dMatMul(&tex_to_dev, &tex_to_dev, &to_out);
            extern BOOL D2dPaintTexture(D2dPaint *, const D2dSurface *, const D2dMatrix *, float, D2D1_INTERPOLATION_MODE, D2D1_EXTEND_MODE, D2D1_EXTEND_MODE, const MimeVecRect *);
            if (D2dPaintTexture(&p, &in.surface, &tex_to_dev, 1.0f, *pu(e, 0) == 0 ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_INTERPOLATION_MODE_LINEAR, D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, &clamp)) {
                for (int y = 0; y < out->surface.h; y++) {
                    D2dPaintRow(rt, &p, 0, y, out->surface.w, D2dSurfaceRow(&out->surface, y));
                    /* pixels that fall outside the source stay transparent. */
                    for (int x = 0; x < out->surface.w; x++) {
                        float px = (x + 0.5f) * p.inverse._11 + (y + 0.5f) * p.inverse._21 + p.inverse._31;
                        float py = (x + 0.5f) * p.inverse._12 + (y + 0.5f) * p.inverse._22 + p.inverse._32;
                        if (px < 0 || py < 0 || px > in.surface.w || py > in.surface.h)
                            D2dSurfaceRow(&out->surface, y)[x] = 0;
                    }
                }
            }
        }
        D2dImageDataFree(&in);
        break;
    }
    case EFF_BORDER:
    case EFF_TILE: {
        D2dImageData in;
        IRect ib;
        if (!e->inputs[0] || FAILED(image_bounds(rt, e->inputs[0], &ib, depth + 1))) {
            hr = data_new(out, area);
            break;
        }
        if (e->id == EFF_TILE) {
            const float *v = pf(e, 0);
            IRect t = {(int)D2dFloorF(v[0] * s), (int)D2dFloorF(v[1] * s), (int)(v[2] * s + 0.999f), (int)(v[3] * s + 0.999f)};
            ib = irect_intersect(ib, t);
        }
        hr = input_area(rt, e, 0, ib, &in, depth);
        if (FAILED(hr))
            break;
        hr = data_new(out, area);
        if (SUCCEEDED(hr) && in.surface.w > 0 && in.surface.h > 0) {
            int w = in.surface.w, h = in.surface.h;
            for (int y = area.y0; y < area.y1; y++) {
                for (int x = area.x0; x < area.x1; x++) {
                    int sx = x - in.x, sy = y - in.y;
                    if (e->id == EFF_TILE) {
                        sx = ((sx % w) + w) % w;
                        sy = ((sy % h) + h) % h;
                    } else {
                        UINT32 mx = *pu(e, 0), my = *pu(e, 1);
                        if (sx < 0 || sx >= w)
                            sx = mx == 0 ? (sx < 0 ? 0 : w - 1) : (mx == 1 ? ((sx % w) + w) % w : -1);
                        if (sy < 0 || sy >= h)
                            sy = my == 0 ? (sy < 0 ? 0 : h - 1) : (my == 1 ? ((sy % h) + h) % h : -1);
                        if (sx < 0 || sy < 0)
                            continue;
                    }
                    D2dSurfaceRow(&out->surface, y - area.y0)[x - area.x0] = D2dSurfaceRow(&in.surface, sy)[sx];
                }
            }
        }
        D2dImageDataFree(&in);
        break;
    }
    default: {
        /* the per pixel effects. */
        D2dImageData in;
        hr = input_area(rt, e, 0, area, &in, depth);
        if (FAILED(hr))
            break;
        for (int i = 0; i < in.surface.w * in.surface.h; i++) {
            uint32_t p = in.surface.px[i];
            float c[4];
            unpack(p, c);
            float o[4] = {c[0], c[1], c[2], c[3]};
            switch (e->id) {
            case EFF_COLORMATRIX:
            case EFF_GRAYSCALE:
            case EFF_SEPIA:
            case EFF_SATURATION:
            case EFF_HUEROTATE:
            case EFF_LUMINANCE:
            case EFF_INVERT:
            case EFF_BRIGHTNESS:
            case EFF_CONTRAST:
            case EFF_EXPOSURE:
            case EFF_TINT:
            case EFF_OPACITY: {
                float m[5][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 0, 0, 0}};
                BOOL straight = TRUE;
                switch (e->id) {
                case EFF_COLORMATRIX:
                    memcpy(m, pf(e, 0), sizeof(m));
                    straight = *pu(e, 1) == D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT;
                    break;
                case EFF_GRAYSCALE:
                    for (int r = 0; r < 3; r++) {
                        m[0][r] = 0.2125f; m[1][r] = 0.7154f; m[2][r] = 0.0721f;
                    }
                    break;
                case EFF_SEPIA: {
                    float t = D2dClampF(pf(e, 0)[0], 0, 1);
                    float sepia[3][3] = {{0.393f, 0.349f, 0.272f}, {0.769f, 0.686f, 0.534f}, {0.189f, 0.168f, 0.131f}};
                    for (int a = 0; a < 3; a++)
                        for (int b = 0; b < 3; b++)
                            m[a][b] = (a == b ? 1.0f - t : 0.0f) + t * sepia[a][b];
                    break;
                }
                case EFF_SATURATION: {
                    float sat = pf(e, 0)[0];
                    float lr = 0.2125f, lg = 0.7154f, lb = 0.0721f;
                    m[0][0] = lr * (1 - sat) + sat; m[0][1] = lr * (1 - sat); m[0][2] = lr * (1 - sat);
                    m[1][0] = lg * (1 - sat); m[1][1] = lg * (1 - sat) + sat; m[1][2] = lg * (1 - sat);
                    m[2][0] = lb * (1 - sat); m[2][1] = lb * (1 - sat); m[2][2] = lb * (1 - sat) + sat;
                    break;
                }
                case EFF_HUEROTATE: {
                    float sn, cs;
                    D2dSinCosF(pf(e, 0)[0] * 0.017453292f, &sn, &cs);
                    float h[3][3] = {{0.213f + cs * 0.787f - sn * 0.213f, 0.213f - cs * 0.213f + sn * 0.143f, 0.213f - cs * 0.213f - sn * 0.787f},
                                     {0.715f - cs * 0.715f - sn * 0.715f, 0.715f + cs * 0.285f + sn * 0.140f, 0.715f - cs * 0.715f + sn * 0.715f},
                                     {0.072f - cs * 0.072f + sn * 0.928f, 0.072f - cs * 0.072f - sn * 0.283f, 0.072f + cs * 0.928f + sn * 0.072f}};
                    for (int a = 0; a < 3; a++)
                        for (int b = 0; b < 3; b++)
                            m[a][b] = h[a][b];
                    break;
                }
                case EFF_LUMINANCE:
                    m[0][0] = m[1][1] = m[2][2] = 0;
                    m[0][3] = 0.2125f; m[1][3] = 0.7154f; m[2][3] = 0.0721f; m[3][3] = 0;
                    break;
                case EFF_INVERT:
                    m[0][0] = m[1][1] = m[2][2] = -1;
                    m[4][0] = m[4][1] = m[4][2] = 1;
                    break;
                case EFF_BRIGHTNESS: {
                    const float *w = pf(e, 0), *bl = pf(e, 1);
                    float tr[3] = {c[0], c[1], c[2]};
                    for (int r = 0; r < 3; r++) {
                        /* a curve through the black and white points of each of the two ends. */
                        float x = tr[r];
                        float lo = bl[0], hi = w[0];
                        float lo2 = bl[1], hi2 = w[1];
                        (void)lo2;
                        (void)hi2;
                        o[r] = lo + (hi - lo) * x;
                    }
                    straight = TRUE;
                    
                    break;
                }
                case EFF_CONTRAST: {
                    float k = pf(e, 0)[0];
                    float slope = k > 0 ? 1.0f / (1.0f - D2dMinF(k, 0.999f)) : 1.0f + k;
                    for (int r = 0; r < 3; r++) {
                        m[r][r] = slope;
                        m[4][r] = 0.5f - slope * 0.5f;
                    }
                    break;
                }
                case EFF_EXPOSURE: {
                    float g = D2dPowF(2.0f, pf(e, 0)[0]);
                    for (int r = 0; r < 3; r++)
                        m[r][r] = g;
                    break;
                }
                case EFF_TINT: {
                    const float *t = pf(e, 0);
                    m[0][0] = t[0]; m[1][1] = t[1]; m[2][2] = t[2]; m[3][3] = t[3];
                    break;
                }
                default:
                    m[3][3] = pf(e, 0)[0];
                    straight = FALSE;
                    break;
                }
                if (e->id != EFF_BRIGHTNESS) {
                    float in4[4] = {c[0], c[1], c[2], c[3]};
                    if (!straight) {
                        in4[0] *= c[3]; in4[1] *= c[3]; in4[2] *= c[3];
                    }
                    for (int k = 0; k < 4; k++)
                        o[k] = in4[0] * m[0][k] + in4[1] * m[1][k] + in4[2] * m[2][k] + in4[3] * m[3][k] + m[4][k];
                    if (!straight) {
                        /* premultiplied in and out. */
                        o[0] = D2dClampF(o[0], 0, 1); o[1] = D2dClampF(o[1], 0, 1); o[2] = D2dClampF(o[2], 0, 1); o[3] = D2dClampF(o[3], 0, 1);
                        in.surface.px[i] = ((uint32_t)(o[3] * 255 + 0.5f) << 24) | ((uint32_t)(D2dMinF(o[0], o[3]) * 255 + 0.5f) << 16) |
                                           ((uint32_t)(D2dMinF(o[1], o[3]) * 255 + 0.5f) << 8) | (uint32_t)(D2dMinF(o[2], o[3]) * 255 + 0.5f);
                        continue;
                    }
                }
                break;
            }
            case EFF_PREMULTIPLY:
                in.surface.px[i] = D2dPremulPixel(p);
                continue;
            case EFF_UNPREMULTIPLY:
                in.surface.px[i] = D2dUnpremulPixel(p);
                continue;
            default:
                break;
            }
            in.surface.px[i] = D2dPackPremul(o[0], o[1], o[2], o[3]);
        }
        *out = in;
        break;
    }
    }
    return hr;
}

/* ------------------------------------------------------------------ drawing images */

void D2dDrawImageCore(D2dRt *rt, ID2D1Image *image, const D2D1_POINT_2F *offset, const D2D1_RECT_F *source,
                      D2D1_INTERPOLATION_MODE mode, UINT32 composite)
{
    if (D2dClipIsEmpty(rt))
        return;
    if (!rt->draw) {
        rt->error = rt->error ? rt->error : D2DERR_WRONG_STATE;
        return;
    }
    D2dEffect *eff = effect_of(image);
    if (!eff && !D2dBitmapOf(image) && !D2dIsOfClass(image, &g_cmd_class)) {
        rt->error = rt->error ? rt->error : E_INVALIDARG;
        return;
    }
    float sx = rt->dpi_x / 96.0f, sy = rt->dpi_y / 96.0f;
    IRect bounds;
    HRESULT hr = image_bounds(rt, image, &bounds, 0);
    if (FAILED(hr)) {
        rt->error = rt->error ? rt->error : hr;
        return;
    }
    IRect src = bounds;
    D2D1_POINT_2F topleft;
    if (source) {
        IRect s = {(int)D2dFloorF(source->left * sx), (int)D2dFloorF(source->top * sy), (int)(source->right * sx + 0.999f),
                   (int)(source->bottom * sy + 0.999f)};
        src = irect_intersect(bounds, s);
        topleft.x = source->left;
        topleft.y = source->top;
    } else {
        topleft.x = topleft.y = 0.0f;
    }
    if (irect_empty(src))
        return;
    /* the area worth rendering: what the target can show of the source, mapped back into image space. */
    D2dMatrix world, inv;
    D2dWorldMatrix(rt, &world);
    D2D1_POINT_2F off = offset ? *offset : (D2D1_POINT_2F){0, 0};
    D2dMatrix px_to_user = {1.0f / sx, 0, 0, 1.0f / sy, off.x - topleft.x, off.y - topleft.y};
    D2dMatrix px_to_dev;
    D2dMatMul(&px_to_dev, &px_to_user, &world);
    IRect want = src;
    if (D2dMatInvert(&inv, &px_to_dev)) {
        D2D1_POINT_2F c[4] = {{0, 0}, {(float)rt->draw->w, 0}, {(float)rt->draw->w, (float)rt->draw->h}, {0, (float)rt->draw->h}};
        float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
        for (int i = 0; i < 4; i++) {
            D2D1_POINT_2F p = D2dMatApply(&inv, c[i].x, c[i].y);
            x0 = D2dMinF(x0, p.x); y0 = D2dMinF(y0, p.y); x1 = D2dMaxF(x1, p.x); y1 = D2dMaxF(y1, p.y);
        }
        IRect t = {(int)D2dFloorF(x0) - 2, (int)D2dFloorF(y0) - 2, (int)(x1 + 0.999f) + 2, (int)(y1 + 0.999f) + 2};
        want = irect_intersect(src, t);
    }
    if (irect_empty(want))
        return;
    D2dImageData data;
    hr = image_get(rt, image, want, &data, 0);
    if (FAILED(hr)) {
        rt->error = rt->error ? rt->error : hr;
        return;
    }
    /* the pixels cut to the source rectangle, drawn through the transform of the target. */
    IRect avail = {data.x, data.y, data.x + data.surface.w, data.y + data.surface.h};
    IRect use = irect_intersect(avail, src);
    if (!irect_empty(use)) {
        MimeVecRect clamp = {use.x0 - data.x, use.y0 - data.y, use.x1 - data.x, use.y1 - data.y};
        D2dMatrix tex_to_dev = {px_to_dev._11, px_to_dev._12, px_to_dev._21, px_to_dev._22,
                                px_to_dev._31 + data.x * px_to_dev._11 + data.y * px_to_dev._21,
                                px_to_dev._32 + data.x * px_to_dev._12 + data.y * px_to_dev._22};
        BOOL aligned = D2dAbsF(tex_to_dev._11 - 1) < 1e-6f && D2dAbsF(tex_to_dev._22 - 1) < 1e-6f && tex_to_dev._12 == 0 && tex_to_dev._21 == 0 &&
                       D2dAbsF(tex_to_dev._31 - D2dFloorF(tex_to_dev._31 + 0.5f)) < 1e-4f && D2dAbsF(tex_to_dev._32 - D2dFloorF(tex_to_dev._32 + 0.5f)) < 1e-4f;
        if (aligned && composite == D2D1_COMPOSITE_MODE_SOURCE_OVER) {
            /* pixel for pixel, no sampling. */
            D2dSurface view = {clamp.right - clamp.left, clamp.bottom - clamp.top, nullptr};
            D2dSurface cut;
            if (D2dSurfaceInit(&cut, view.w, view.h)) {
                for (int y = 0; y < cut.h; y++)
                    memcpy(D2dSurfaceRow(&cut, y), D2dSurfaceRow(&data.surface, y + clamp.top) + clamp.left, (SIZE_T)cut.w * 4);
                D2dBlendSurface(rt, &cut, (int)D2dFloorF(tex_to_dev._31 + 0.5f) + clamp.left, (int)D2dFloorF(tex_to_dev._32 + 0.5f) + clamp.top, 1.0f);
                D2dSurfaceFree(&cut);
            }
        } else {
            MimeVecPolys polys;
            mime_vec_polys_init(&polys, &d2d_allocator);
            D2D1_POINT_2F q[4] = {{(float)clamp.left, (float)clamp.top}, {(float)clamp.right, (float)clamp.top}, {(float)clamp.right, (float)clamp.bottom}, {(float)clamp.left, (float)clamp.bottom}};
            MimeVecPoint pts[4];
            for (int i = 0; i < 4; i++) {
                D2D1_POINT_2F p = D2dMatApply(&tex_to_dev, q[i].x, q[i].y);
                pts[i].x = p.x;
                pts[i].y = p.y;
            }
            mime_vec_polys_add_sub(&polys, pts, 4, true);
            D2D1_PRIMITIVE_BLEND saved = rt->state.blend;
            if (composite == D2D1_COMPOSITE_MODE_SOURCE_COPY || composite == D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY)
                rt->state.blend = D2D1_PRIMITIVE_BLEND_COPY;
            else if (composite == D2D1_COMPOSITE_MODE_PLUS)
                rt->state.blend = D2D1_PRIMITIVE_BLEND_ADD;
            extern void D2dDrawTexture(D2dRt *, const D2dSurface *, const D2dMatrix *, const MimeVecPolys *, float, D2D1_INTERPOLATION_MODE, const MimeVecRect *);
            D2dDrawTexture(rt, &data.surface, &tex_to_dev, &polys, 1.0f, mode, &clamp);
            rt->state.blend = saved;
            mime_vec_polys_free(&polys);
        }
    }
    D2dImageDataFree(&data);
}

/* ------------------------------------------------------------------ the effect object */

#define EF D2dEffect *e = (D2dEffect *)This
#define SYS(i) ((i) >= 0x80000000u)

static const EffDesc *desc_of(const D2dEffect *e) { return &g_effects[e->id]; }

static UINT32 STDMETHODCALLTYPE ef_count(ID2D1Effect *This) { EF; return desc_of(e)->nprops; }

static HRESULT STDMETHODCALLTYPE ef_name(ID2D1Effect *This, UINT32 index, WCHAR *name, UINT32 count)
{
    EF;
    if (index >= desc_of(e)->nprops)
        return E_INVALIDARG;
    const WCHAR *n = desc_of(e)->props[index].name;
    UINT32 len = (UINT32)wcslen(n);
    if (count < len + 1)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    memcpy(name, n, (len + 1) * sizeof(WCHAR));
    return S_OK;
}
static UINT32 STDMETHODCALLTYPE ef_name_length(ID2D1Effect *This, UINT32 index)
{
    EF;
    return index < desc_of(e)->nprops ? (UINT32)wcslen(desc_of(e)->props[index].name) : 0;
}

static D2D1_PROPERTY_TYPE sys_type(UINT32 i)
{
    switch (i) {
    case D2D1_PROPERTY_CLSID: return D2D1_PROPERTY_TYPE_CLSID;
    case D2D1_PROPERTY_DISPLAYNAME:
    case D2D1_PROPERTY_AUTHOR:
    case D2D1_PROPERTY_CATEGORY:
    case D2D1_PROPERTY_DESCRIPTION: return D2D1_PROPERTY_TYPE_STRING;
    case D2D1_PROPERTY_INPUTS: return D2D1_PROPERTY_TYPE_ARRAY;
    case D2D1_PROPERTY_CACHED: return D2D1_PROPERTY_TYPE_BOOL;
    case D2D1_PROPERTY_PRECISION: return D2D1_PROPERTY_TYPE_ENUM;
    case D2D1_PROPERTY_MIN_INPUTS:
    case D2D1_PROPERTY_MAX_INPUTS: return D2D1_PROPERTY_TYPE_UINT32;
    default: return D2D1_PROPERTY_TYPE_UNKNOWN;
    }
}

static D2D1_PROPERTY_TYPE STDMETHODCALLTYPE ef_type(ID2D1Effect *This, UINT32 index)
{
    EF;
    if (SYS(index))
        return sys_type(index);
    return index < desc_of(e)->nprops ? desc_of(e)->props[index].type : D2D1_PROPERTY_TYPE_UNKNOWN;
}

static UINT32 STDMETHODCALLTYPE ef_index(ID2D1Effect *This, const WCHAR *name)
{
    EF;
    if (!name)
        return 0xffffffffu;
    for (UINT32 i = 0; i < desc_of(e)->nprops; i++) {
        if (wcscmp(desc_of(e)->props[i].name, name) == 0)
            return i;
    }
    static const WCHAR *sys[] = {L"Clsid", L"DisplayName", L"Author", L"Category", L"Description", L"Inputs", L"Cached", L"Precision", L"MinInputs", L"MaxInputs"};
    (void)sys;
    return 0xffffffffu;
}

static HRESULT sys_value(D2dEffect *e, UINT32 index, void *data, UINT32 size, UINT32 *needed)
{
    const EffDesc *d = desc_of(e);
    static const WCHAR author[] = L"Microsoft Corporation", category[] = L"Stock", description[] = L"";
    switch (index) {
    case D2D1_PROPERTY_CLSID:
        *needed = 16;
        if (size == 16)
            memcpy(data, d->clsid, 16);
        return S_OK;
    case D2D1_PROPERTY_DISPLAYNAME:
        *needed = (UINT32)(wcslen(d->name) + 1) * 2;
        if (size >= *needed)
            memcpy(data, d->name, *needed);
        return S_OK;
    case D2D1_PROPERTY_AUTHOR:
        *needed = sizeof(author);
        if (size >= *needed)
            memcpy(data, author, *needed);
        return S_OK;
    case D2D1_PROPERTY_CATEGORY:
        *needed = sizeof(category);
        if (size >= *needed)
            memcpy(data, category, *needed);
        return S_OK;
    case D2D1_PROPERTY_DESCRIPTION:
        *needed = sizeof(description);
        if (size >= *needed)
            memcpy(data, description, *needed);
        return S_OK;
    case D2D1_PROPERTY_CACHED:
        *needed = 4;
        if (size == 4)
            *(BOOL *)data = e->cached;
        return S_OK;
    case D2D1_PROPERTY_PRECISION:
        *needed = 4;
        if (size == 4)
            *(UINT32 *)data = e->precision;
        return S_OK;
    case D2D1_PROPERTY_MIN_INPUTS:
        *needed = 4;
        if (size == 4)
            *(UINT32 *)data = d->min_inputs;
        return S_OK;
    case D2D1_PROPERTY_MAX_INPUTS:
        *needed = 4;
        if (size == 4)
            *(UINT32 *)data = d->max_inputs;
        return S_OK;
    default:
        return D2DERR_INVALID_PROPERTY;
    }
}

static HRESULT STDMETHODCALLTYPE ef_set(ID2D1Effect *This, UINT32 index, D2D1_PROPERTY_TYPE type, const BYTE *data, UINT32 size)
{
    EF;
    if (!data)
        return E_INVALIDARG;
    if (SYS(index)) {
        if (index == (UINT32)D2D1_PROPERTY_CACHED && size == 4) {
            e->cached = *(const BOOL *)data != 0;
            return S_OK;
        }
        if (index == (UINT32)D2D1_PROPERTY_PRECISION && size == 4) {
            e->precision = *(const UINT32 *)data;
            return S_OK;
        }
        return D2DERR_INVALID_PROPERTY;
    }
    if (index >= desc_of(e)->nprops)
        return D2DERR_INVALID_PROPERTY;
    const EffProp *p = &desc_of(e)->props[index];
    if ((type != D2D1_PROPERTY_TYPE_UNKNOWN && type != p->type) || size != p->size)
        return E_INVALIDARG;
    memcpy(e->props[index], data, size);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ef_set_by_name(ID2D1Effect *This, const WCHAR *name, D2D1_PROPERTY_TYPE type, const BYTE *data, UINT32 size)
{
    UINT32 i = ef_index(This, name);
    if (i == 0xffffffffu)
        return D2DERR_INVALID_PROPERTY;
    return ef_set(This, i, type, data, size);
}
static HRESULT STDMETHODCALLTYPE ef_get(ID2D1Effect *This, UINT32 index, D2D1_PROPERTY_TYPE type, BYTE *data, UINT32 size)
{
    EF;
    if (!data)
        return E_INVALIDARG;
    if (SYS(index)) {
        UINT32 need;
        HRESULT hr = sys_value(e, index, data, size, &need);
        if (FAILED(hr))
            return hr;
        return size < need ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) : S_OK;
    }
    if (index >= desc_of(e)->nprops)
        return D2DERR_INVALID_PROPERTY;
    const EffProp *p = &desc_of(e)->props[index];
    if (type != D2D1_PROPERTY_TYPE_UNKNOWN && type != p->type)
        return E_INVALIDARG;
    if (size < p->size)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    memcpy(data, e->props[index], p->size);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ef_get_by_name(ID2D1Effect *This, const WCHAR *name, D2D1_PROPERTY_TYPE type, BYTE *data, UINT32 size)
{
    UINT32 i = ef_index(This, name);
    if (i == 0xffffffffu)
        return D2DERR_INVALID_PROPERTY;
    return ef_get(This, i, type, data, size);
}
static UINT32 STDMETHODCALLTYPE ef_value_size(ID2D1Effect *This, UINT32 index)
{
    EF;
    if (SYS(index)) {
        UINT32 need = 0;
        BYTE tmp[1];
        sys_value(e, index, tmp, 0, &need);
        return need;
    }
    return index < desc_of(e)->nprops ? desc_of(e)->props[index].size : 0;
}
static HRESULT STDMETHODCALLTYPE ef_subprops(ID2D1Effect *This, UINT32 index, ID2D1Properties **out)
{
    if (out)
        *out = nullptr;
    return D2DERR_NO_SUBPROPERTIES;
}

static void STDMETHODCALLTYPE ef_set_input(ID2D1Effect *This, UINT32 index, ID2D1Image *input, BOOL invalidate)
{
    EF;
    if (index >= 8)
        return;
    D2D_ADDREF(input);
    if (index < e->input_count)
        D2D_RELEASE(e->inputs[index]);
    e->inputs[index] = input;
    if (index >= e->input_count)
        e->input_count = index + 1;
}
static HRESULT STDMETHODCALLTYPE ef_set_input_count(ID2D1Effect *This, UINT32 count)
{
    EF;
    const EffDesc *d = desc_of(e);
    if (count < d->min_inputs || count > d->max_inputs || count > 8)
        return E_INVALIDARG;
    for (UINT32 i = count; i < e->input_count; i++)
        D2D_RELEASE(e->inputs[i]);
    e->input_count = count;
    return S_OK;
}
static void STDMETHODCALLTYPE ef_get_input(ID2D1Effect *This, UINT32 index, ID2D1Image **out)
{
    EF;
    *out = index < e->input_count ? e->inputs[index] : nullptr;
    D2D_ADDREF(*out);
}
static UINT32 STDMETHODCALLTYPE ef_get_input_count(ID2D1Effect *This) { EF; return e->input_count; }
static void effout_destroy(D2dObj *o) { D2dRelease(((D2dEffOut *)o)->effect); }
static const void *const g_effout_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory};
D2D_IIDS(g_effout_iids, &IID_ID2D1Resource, &IID_ID2D1Image);
static D2dClass g_effout_class = {"effect output", g_effout_iids, effout_destroy, nullptr};

static void STDMETHODCALLTYPE ef_get_output(ID2D1Effect *This, ID2D1Image **out)
{
    EF;
    D2dEffOut *o = D2dNew(sizeof(*o), g_effout_vtbl, &g_effout_class, e->obj.factory);
    *out = (ID2D1Image *)o;
    if (o) {
        o->effect = e;
        D2dAddRef(e);
    }
}

static void effect_destroy(D2dObj *o)
{
    D2dEffect *e = (D2dEffect *)o;
    for (UINT32 i = 0; i < e->input_count; i++)
        D2D_RELEASE(e->inputs[i]);
}

static const void *const g_effect_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, ef_count, ef_name, ef_name_length,
                                            ef_type, ef_index, ef_set_by_name, ef_set, ef_get_by_name, ef_get, ef_value_size,
                                            ef_subprops, ef_set_input, ef_set_input_count, ef_get_input, ef_get_input_count, ef_get_output};
D2D_IIDS(g_effect_iids, &IID_ID2D1Properties, &IID_ID2D1Effect);
static D2dClass g_effect_class = {"effect", g_effect_iids, effect_destroy, nullptr};

/* the stock effects that exist on windows and are not drawn here. */
static const GUID *const g_unimplemented[] = {
    &CLSID_D2D1YCbCr, &CLSID_D2D1RgbToHue, &CLSID_D2D1HueToRgb, &CLSID_D2D1ChromaKey, &CLSID_D2D1Emboss, &CLSID_D2D1Posterize,
    &CLSID_D2D1Sharpen, &CLSID_D2D1Straighten, &CLSID_D2D1TemperatureTint, &CLSID_D2D1Vignette, &CLSID_D2D1EdgeDetection,
    &CLSID_D2D1HighlightsShadows, &CLSID_D2D1LookupTable3D, &CLSID_D2D1AlphaMask, &CLSID_D2D1CrossFade, &CLSID_D2D1WhiteLevelAdjustment,
    &CLSID_D2D1HdrToneMap, &CLSID_D2D13DPerspectiveTransform, &CLSID_D2D13DTransform, &CLSID_D2D1Atlas, &CLSID_D2D1BitmapSource,
    &CLSID_D2D1ColorManagement, &CLSID_D2D1ConvolveMatrix, &CLSID_D2D1DirectionalBlur, &CLSID_D2D1DiscreteTransfer,
    &CLSID_D2D1DisplacementMap, &CLSID_D2D1DistantDiffuse, &CLSID_D2D1DistantSpecular, &CLSID_D2D1DpiCompensation,
    &CLSID_D2D1GammaTransfer, &CLSID_D2D1Scale, &CLSID_D2D1Histogram, &CLSID_D2D1LinearTransfer, &CLSID_D2D1Morphology,
    &CLSID_D2D1OpacityMetadata, &CLSID_D2D1PointDiffuse, &CLSID_D2D1PointSpecular, &CLSID_D2D1SpotDiffuse, &CLSID_D2D1SpotSpecular,
    &CLSID_D2D1TableTransfer, &CLSID_D2D1Turbulence};

HRESULT D2dCreateEffect(D2dRt *rt, REFCLSID clsid, ID2D1Effect **out)
{
    for (int i = 0; i < EFF_COUNT; i++) {
        if (!IsEqualGUID(clsid, g_effects[i].clsid))
            continue;
        D2dEffect *e = D2dNew(sizeof(*e), g_effect_vtbl, &g_effect_class, rt->obj.factory);
        if (!e)
            return E_OUTOFMEMORY;
        e->id = i;
        e->input_count = g_effects[i].default_inputs;
        e->precision = D2D1_BUFFER_PRECISION_UNKNOWN;
        effect_defaults(e);
        *out = (ID2D1Effect *)e;
        return S_OK;
    }
    for (size_t i = 0; i < sizeof(g_unimplemented) / sizeof(g_unimplemented[0]); i++) {
        if (IsEqualGUID(clsid, g_unimplemented[i]))
            return E_NOTIMPL;
    }
    return D2DERR_EFFECT_IS_NOT_REGISTERED;
}

HRESULT D2dEffectEnumerate(D2dFactory *f, CLSID *list, UINT32 count, UINT32 *returned, UINT32 *registered)
{
    UINT32 n = 0;
    for (int i = 0; i < EFF_COUNT; i++) {
        if (list && n < count)
            list[n] = *g_effects[i].clsid;
        n++;
    }
    UINT32 shown = n < count ? n : count;
    if (returned)
        *returned = list ? shown : 0;
    if (registered)
        *registered = n;
    return list && count < n ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) : S_OK;
}

HRESULT D2dEffectUnregister(D2dFactory *f, REFCLSID clsid)
{
    for (int i = 0; i < EFF_COUNT; i++) {
        if (IsEqualGUID(clsid, g_effects[i].clsid))
            return D2DERR_INVALID_CALL;
    }
    return D2DERR_EFFECT_IS_NOT_REGISTERED;
}

HRESULT D2dEffectProperties(D2dFactory *f, REFCLSID clsid, ID2D1Properties **out)
{
    for (int i = 0; i < EFF_COUNT; i++) {
        if (IsEqualGUID(clsid, g_effects[i].clsid)) {
            D2dEffect *e = D2dNew(sizeof(*e), g_effect_vtbl, &g_effect_class, f);
            if (!e)
                return E_OUTOFMEMORY;
            e->id = i;
            e->input_count = g_effects[i].default_inputs;
            effect_defaults(e);
            *out = (ID2D1Properties *)e;
            return S_OK;
        }
    }
    return D2DERR_EFFECT_IS_NOT_REGISTERED;
}
