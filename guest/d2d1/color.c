#include "d2d_private.h"
#include <icc/icc.h>

/* ------------------------------------------------------------------ color contexts */

/* the primaries, white point and gamma of a simple color profile, as D2D1_SIMPLE_COLOR_PROFILE lays them out. */
typedef struct D2dSimpleProfile {
    D2D1_POINT_2F red, green, blue, white_xz;
    UINT32 gamma;
} D2dSimpleProfile;

/* D2D1_COLOR_CONTEXT_TYPE: an icc profile, a simple profile or a dxgi color space. */
enum { CC_TYPE_ICC = 0, CC_TYPE_SIMPLE = 1, CC_TYPE_DXGI = 2 };
#define CC_DXGI_CUSTOM 0xffffffffu

typedef struct D2dColorContext {
    D2dObj obj;
    D2D1_COLOR_SPACE space;
    BYTE *profile;
    UINT32 size;
    UINT32 type;
    UINT32 dxgi;
    D2dSimpleProfile simple;
    /* names the context in the cache of transforms, as addresses come back after a context is freed. */
    LONG serial;
} D2dColorContext;

static D2dClass g_cc_class;
static LONG g_cc_serial;

static D2D1_COLOR_SPACE STDMETHODCALLTYPE cc_space(ID2D1ColorContext *This) { return ((D2dColorContext *)This)->space; }
static UINT32 STDMETHODCALLTYPE cc_size(ID2D1ColorContext *This) { return ((D2dColorContext *)This)->size; }
static HRESULT STDMETHODCALLTYPE cc_profile(ID2D1ColorContext *This, BYTE *profile, UINT32 size)
{
    D2dColorContext *c = (D2dColorContext *)This;
    if (size < c->size)
        return D2DERR_INSUFFICIENT_BUFFER_HR;
    memcpy(profile, c->profile, c->size);
    return S_OK;
}
static UINT32 STDMETHODCALLTYPE cc_type(ID2D1ColorContext *This) { return ((D2dColorContext *)This)->type; }
static UINT32 STDMETHODCALLTYPE cc_dxgi(ID2D1ColorContext *This) { return ((D2dColorContext *)This)->dxgi; }
static HRESULT STDMETHODCALLTYPE cc_simple(ID2D1ColorContext *This, D2dSimpleProfile *out)
{
    D2dColorContext *c = (D2dColorContext *)This;
    if (!out)
        return E_INVALIDARG;
    if (c->type != CC_TYPE_SIMPLE)
        return D2DERR_INVALID_CALL;
    *out = c->simple;
    return S_OK;
}
static const void *const g_cc_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, cc_space, cc_size,
                                        cc_profile, cc_type, cc_dxgi, cc_simple};
D2D_IIDS(g_cc_iids, &IID_ID2D1Resource, &IID_ID2D1ColorContext, &IID_ID2D1ColorContext1);
static void cc_destroy(D2dObj *o) { D2dFree(((D2dColorContext *)o)->profile); }
static D2dClass g_cc_class = {"color context", g_cc_iids, cc_destroy, nullptr};

static D2dColorContext *cc_new(D2dFactory *f)
{
    D2dColorContext *c = D2dNew(sizeof(*c), g_cc_vtbl, &g_cc_class, f);
    if (c)
        c->serial = InterlockedIncrement(&g_cc_serial);
    return c;
}

/* the dxgi color spaces of srgb and scrgb, 0 and 1, are those of the matching d2d spaces. */
HRESULT D2dCreateColorContextDxgi(D2dFactory *f, UINT32 dxgi, ID2D1ColorContext **out)
{
    if (dxgi > 24)
        return E_INVALIDARG;
    D2D1_COLOR_SPACE space = dxgi == 0 ? D2D1_COLOR_SPACE_SRGB : dxgi == 1 ? D2D1_COLOR_SPACE_SCRGB : D2D1_COLOR_SPACE_CUSTOM;
    D2dColorContext *c = cc_new(f);
    if (!c)
        return E_OUTOFMEMORY;
    c->space = space;
    c->type = CC_TYPE_DXGI;
    c->dxgi = dxgi;
    *out = (ID2D1ColorContext *)c;
    return S_OK;
}

HRESULT D2dCreateColorContextSimple(D2dFactory *f, const void *profile, ID2D1ColorContext **out)
{
    const D2dSimpleProfile *p = profile;
    /* D2D1_GAMMA1_G22, D2D1_GAMMA1_G10 and D2D1_GAMMA1_G2084. */
    if (!p || p->gamma > 2)
        return E_INVALIDARG;
    D2dColorContext *c = cc_new(f);
    if (!c)
        return E_OUTOFMEMORY;
    c->space = D2D1_COLOR_SPACE_CUSTOM;
    c->type = CC_TYPE_SIMPLE;
    c->dxgi = CC_DXGI_CUSTOM;
    c->simple = *p;
    *out = (ID2D1ColorContext *)c;
    return S_OK;
}

static void *icc_heap_alloc(void *ctx, size_t size) { return D2dAlloc(size ? size : 1); }
static void icc_heap_free(void *ctx, void *p) { D2dFree(p); }
static const MimeAllocator g_icc_alloc = {icc_heap_alloc, icc_heap_free, nullptr};

static const double g_primaries_709[3][2] = {{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}};
static const double g_primaries_2020[3][2] = {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}};
static const double g_white_d65[2] = {0.3127, 0.3290};

/*
 * the icc profile windows gives the srgb and scrgb spaces: a version 2 matrix and curves profile with the srgb
 * primaries adapted to d50, and the srgb curve or a linear one.
 */
static BYTE *rgb_profile(BOOL linear, UINT32 *size)
{
    IccRgbSpec spec = {.curve = linear ? ICC_CURVE_LINEAR : ICC_CURVE_SRGB_TABLE,
                       .version = 2,
                       .description = linear ? "scRGB" : "sRGB"};
    memcpy(spec.primaries, g_primaries_709, sizeof(spec.primaries));
    memcpy(spec.white, g_white_d65, sizeof(spec.white));
    static const double d50[3] = {0.9642, 1.0, 0.8249};
    spec.media_white = d50;
    return icc_build_rgb(&g_icc_alloc, &spec, size);
}

HRESULT D2dCreateColorContext(D2dFactory *f, D2D1_COLOR_SPACE space, const BYTE *profile, UINT32 size,
                              ID2D1ColorContext **out)
{
    if (space == D2D1_COLOR_SPACE_CUSTOM && (!profile || !size))
        return E_INVALIDARG;
    if (space > D2D1_COLOR_SPACE_SCRGB)
        return E_INVALIDARG;
    D2dColorContext *c = cc_new(f);
    if (!c)
        return E_OUTOFMEMORY;
    c->space = space;
    c->type = CC_TYPE_ICC;
    c->dxgi = space == D2D1_COLOR_SPACE_SRGB ? 0 : space == D2D1_COLOR_SPACE_SCRGB ? 1 : CC_DXGI_CUSTOM;
    if (space == D2D1_COLOR_SPACE_CUSTOM) {
        c->profile = D2dAlloc(size);
        if (!c->profile) {
            D2dRelease(c);
            return E_OUTOFMEMORY;
        }
        memcpy(c->profile, profile, size);
        c->size = size;
    } else {
        c->profile = rgb_profile(space == D2D1_COLOR_SPACE_SCRGB, &c->size);
        if (!c->profile) {
            D2dRelease(c);
            return E_OUTOFMEMORY;
        }
    }
    *out = (ID2D1ColorContext *)c;
    return S_OK;
}

/*
 * the profile the engine converts with: the icc bytes of the context, or a version 4 profile made from the primaries
 * and gamma of a simple profile or a dxgi color space. a made one goes to *made for the caller to free.
 */
static const BYTE *context_profile(D2dColorContext *c, UINT32 *size, BYTE **made)
{
    *made = nullptr;
    if (c->type == CC_TYPE_ICC && c->profile) {
        *size = c->size;
        return c->profile;
    }
    IccRgbSpec spec = {.version = 4, .curve = ICC_CURVE_SRGB, .description = "Direct2D"};
    memcpy(spec.primaries, g_primaries_709, sizeof(spec.primaries));
    memcpy(spec.white, g_white_d65, sizeof(spec.white));
    if (c->type == CC_TYPE_SIMPLE) {
        const D2dSimpleProfile *s = &c->simple;
        const D2D1_POINT_2F *p[3] = {&s->red, &s->green, &s->blue};
        for (int i = 0; i < 3; i++) {
            spec.primaries[i][0] = p[i]->x;
            spec.primaries[i][1] = p[i]->y;
        }
        /* the white is given as x and z of a white of luminance 1. */
        double sum = s->white_xz.x + 1.0 + s->white_xz.y;
        spec.white[0] = s->white_xz.x / sum;
        spec.white[1] = 1.0 / sum;
        spec.curve = s->gamma == 1 ? ICC_CURVE_LINEAR : s->gamma == 2 ? ICC_CURVE_PQ : ICC_CURVE_SRGB;
    } else if (c->type == CC_TYPE_DXGI && c->dxgi <= 1) {
        /* the same bytes as the srgb and scrgb spaces, so converting between them changes nothing. */
        *made = rgb_profile(c->dxgi == 1, size);
        return *made;
    } else if (c->type == CC_TYPE_DXGI) {
        switch (c->dxgi) {
        case 3:  /* RGB_STUDIO_G22_NONE_P2020 */
        case 17: /* RGB_FULL_G22_NONE_P2020 */
            memcpy(spec.primaries, g_primaries_2020, sizeof(spec.primaries));
            break;
        case 12: /* RGB_FULL_G2084_NONE_P2020 */
        case 14: /* RGB_STUDIO_G2084_NONE_P2020 */
            memcpy(spec.primaries, g_primaries_2020, sizeof(spec.primaries));
            spec.curve = ICC_CURVE_PQ;
            break;
        case 20: /* RGB_STUDIO_G24_NONE_P709 */
            spec.curve = ICC_CURVE_GAMMA;
            spec.gamma = 2.4;
            break;
        case 21: /* RGB_STUDIO_G24_NONE_P2020 */
            memcpy(spec.primaries, g_primaries_2020, sizeof(spec.primaries));
            spec.curve = ICC_CURVE_GAMMA;
            spec.gamma = 2.4;
            break;
        }
    } else {
        spec.curve = c->space == D2D1_COLOR_SPACE_SCRGB ? ICC_CURVE_LINEAR : ICC_CURVE_SRGB;
    }
    *made = icc_build_rgb(&g_icc_alloc, &spec, size);
    return *made;
}

/* ------------------------------------------------------------------ the color management effect */

/* transforms are kept for the contexts and intents they were made for, a context never changing. */
typedef struct CachedXform {
    LONG src, dst;
    UINT32 src_intent, dst_intent;
    IccXform *xform;
} CachedXform;

#define XFORM_CACHE 8
static CachedXform g_cache[XFORM_CACHE];
static UINT32 g_cache_next;
static SRWLOCK g_cache_lock = SRWLOCK_INIT;

static IccXform *make_xform(D2dColorContext *src, UINT32 src_intent, D2dColorContext *dst, UINT32 dst_intent)
{
    BYTE *made_src, *made_dst;
    UINT32 src_size = 0, dst_size = 0;
    const BYTE *a = context_profile(src, &src_size, &made_src), *b = context_profile(dst, &dst_size, &made_dst);
    IccXform *x = nullptr;
    if (a && b) {
        IccSource sources[2] = {{a, src_size}, {b, dst_size}};
        UINT32 intents[2] = {src_intent & 3, dst_intent & 3};
        x = icc_xform_create(&g_icc_alloc, sources, 2, intents, 2, 0, nullptr);
        if (x && !icc_xform_prepare8(x)) {
            icc_xform_destroy(x);
            x = nullptr;
        }
    }
    D2dFree(made_src);
    D2dFree(made_dst);
    return x;
}

/* the transform of two contexts, shared: callers hold the cache lock shared while they use it. */
static IccXform *cached_xform(D2dColorContext *src, UINT32 src_intent, D2dColorContext *dst, UINT32 dst_intent)
{
    for (UINT32 i = 0; i < XFORM_CACHE; i++) {
        CachedXform *c = &g_cache[i];
        if (c->xform && c->src == src->serial && c->dst == dst->serial && c->src_intent == src_intent && c->dst_intent == dst_intent)
            return c->xform;
    }
    return nullptr;
}

static void put_xform(D2dColorContext *src, UINT32 src_intent, D2dColorContext *dst, UINT32 dst_intent, IccXform *x)
{
    CachedXform *c = &g_cache[g_cache_next++ % XFORM_CACHE];
    icc_xform_destroy(c->xform);
    *c = (CachedXform){src->serial, dst->serial, src_intent, dst_intent, x};
}

/*
 * converts premultiplied bgra pixels in place from the source context to the destination one, srgb standing for a
 * missing context. with the straight alpha mode the colors are taken as they are.
 */
void D2dColorManage(ID2D1ColorContext *source, UINT32 source_intent, ID2D1ColorContext *destination,
                    UINT32 destination_intent, UINT32 alpha_mode, D2dSurface *surface)
{
    D2dColorContext *src = D2dIsOfClass(source, &g_cc_class) ? (D2dColorContext *)source : nullptr;
    D2dColorContext *dst = D2dIsOfClass(destination, &g_cc_class) ? (D2dColorContext *)destination : nullptr;
    if ((!src && !dst) || src == dst || surface->w <= 0 || surface->h <= 0)
        return;
    D2dColorContext srgb = {.space = D2D1_COLOR_SPACE_SRGB, .type = CC_TYPE_DXGI, .dxgi = 0, .serial = 0};
    if (!src)
        src = &srgb;
    if (!dst)
        dst = &srgb;
    AcquireSRWLockShared(&g_cache_lock);
    IccXform *x = cached_xform(src, source_intent, dst, destination_intent);
    if (!x) {
        ReleaseSRWLockShared(&g_cache_lock);
        IccXform *made = make_xform(src, source_intent, dst, destination_intent);
        if (!made)
            return;
        AcquireSRWLockExclusive(&g_cache_lock);
        x = cached_xform(src, source_intent, dst, destination_intent);
        if (x)
            icc_xform_destroy(made);
        else
            put_xform(src, source_intent, dst, destination_intent, x = made);
        ReleaseSRWLockExclusive(&g_cache_lock);
        AcquireSRWLockShared(&g_cache_lock);
        x = cached_xform(src, source_intent, dst, destination_intent);
        if (!x) {
            ReleaseSRWLockShared(&g_cache_lock);
            return;
        }
    }
    UINT32 ni = icc_xform_inputs(x), no = icc_xform_outputs(x);
    if (icc_xform_identity(x) || (ni != 3 && ni != 1) || (no != 3 && no != 1)) {
        ReleaseSRWLockShared(&g_cache_lock);
        return;
    }
    BOOL premultiplied = alpha_mode != D2D1_COLORMANAGEMENT_ALPHA_MODE_STRAIGHT;
    int w = surface->w;
    BYTE *row = D2dAlloc((SIZE_T)w * 6);
    if (!row) {
        ReleaseSRWLockShared(&g_cache_lock);
        return;
    }
    BYTE *in = row, *out = row + (SIZE_T)w * 3;
    for (int y = 0; y < surface->h; y++) {
        uint32_t *px = D2dSurfaceRow(surface, y);
        for (int i = 0; i < w; i++) {
            uint32_t p = px[i], a = p >> 24;
            uint32_t r = p >> 16 & 255, g = p >> 8 & 255, b = p & 255;
            if (premultiplied && a && a != 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
                r = r > 255 ? 255 : r;
                g = g > 255 ? 255 : g;
                b = b > 255 ? 255 : b;
            }
            if (ni == 1) {
                in[i] = (BYTE)((r * 77 + g * 150 + b * 29 + 128) >> 8);
            } else {
                in[i * 3] = (BYTE)r;
                in[i * 3 + 1] = (BYTE)g;
                in[i * 3 + 2] = (BYTE)b;
            }
        }
        icc_xform_eval8(x, in, ni, out, no, (size_t)w);
        for (int i = 0; i < w; i++) {
            uint32_t a = px[i] >> 24;
            uint32_t r = out[i * no], g = out[i * no + (no == 3 ? 1 : 0)], b = out[i * no + (no == 3 ? 2 : 0)];
            if (premultiplied && a != 255) {
                r = (r * a + 127) / 255;
                g = (g * a + 127) / 255;
                b = (b * a + 127) / 255;
            }
            px[i] = a << 24 | r << 16 | g << 8 | b;
        }
    }
    D2dFree(row);
    ReleaseSRWLockShared(&g_cache_lock);
}
