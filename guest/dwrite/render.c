/*
 * glyph run rendering: rendering parameters, IDWriteGlyphRunAnalysis coverage
 * textures and outlines for a geometry sink. glyphs are unhinted and placed at
 * their exact subpixel position.
 */
#include "dw_private.h"

/* the slant of a simulated oblique, tan 12 degrees. */
#define OBLIQUE_SHEAR 0.2126f

static float absf(float v) { return v < 0 ? -v : v; }

/* ------------------------------------------------------------------ rendering parameters */

static HRESULT WINAPI params_query(IDWriteRenderingParams2 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteRenderingParams) ||
        IsEqualGUID(riid, &IID_IDWriteRenderingParams1) || IsEqualGUID(riid, &IID_IDWriteRenderingParams2) ||
        IsEqualGUID(riid, &IID_IDWriteRenderingParams3)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI params_addref(IDWriteRenderingParams2 *This)
{
    return InterlockedIncrement(&((DwRenderingParams *)This)->refs);
}

static ULONG WINAPI params_release(IDWriteRenderingParams2 *This)
{
    DwRenderingParams *p = (DwRenderingParams *)This;
    ULONG refs = InterlockedDecrement(&p->refs);
    if (!refs)
        DwFree(p);
    return refs;
}

static FLOAT WINAPI params_gamma(IDWriteRenderingParams2 *This) { return ((DwRenderingParams *)This)->gamma; }
static FLOAT WINAPI params_contrast(IDWriteRenderingParams2 *This)
{
    return ((DwRenderingParams *)This)->enhanced_contrast;
}
static FLOAT WINAPI params_level(IDWriteRenderingParams2 *This) { return ((DwRenderingParams *)This)->cleartype_level; }
static DWRITE_PIXEL_GEOMETRY WINAPI params_geometry(IDWriteRenderingParams2 *This)
{
    return ((DwRenderingParams *)This)->geometry;
}
static DWRITE_RENDERING_MODE WINAPI params_mode(IDWriteRenderingParams2 *This)
{
    return ((DwRenderingParams *)This)->mode;
}
static FLOAT WINAPI params_grayscale(IDWriteRenderingParams2 *This)
{
    return ((DwRenderingParams *)This)->grayscale_contrast;
}
static DWRITE_GRID_FIT_MODE WINAPI params_grid(IDWriteRenderingParams2 *This)
{
    return ((DwRenderingParams *)This)->grid_fit;
}

static DWRITE_RENDERING_MODE1 WINAPI params_mode1(IDWriteRenderingParams2 *This)
{
    return ((DwRenderingParams *)This)->mode1;
}

static const IDWriteRenderingParams3Vtbl g_params_vtbl = {
    (void *)params_query,     (void *)params_addref, (void *)params_release, (void *)params_gamma,
    (void *)params_contrast,  (void *)params_level,  (void *)params_geometry, (void *)params_mode,
    (void *)params_grayscale, (void *)params_grid,   (void *)params_mode1,
};

HRESULT DwRenderingParamsCreate(FLOAT gamma, FLOAT contrast, FLOAT grayscale_contrast, FLOAT cleartype_level,
                                DWRITE_PIXEL_GEOMETRY geometry, DWRITE_RENDERING_MODE1 mode,
                                DWRITE_GRID_FIT_MODE grid_fit, DwRenderingParams **out)
{
    /* the negated tests also refuse nan. */
    if (!(gamma > 0) || !(contrast >= 0) || !(grayscale_contrast >= 0) || !(cleartype_level >= 0) ||
        !(cleartype_level <= 1))
        return E_INVALIDARG;
    if ((unsigned)geometry > DWRITE_PIXEL_GEOMETRY_BGR || (unsigned)mode > DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED ||
        (unsigned)grid_fit > DWRITE_GRID_FIT_MODE_ENABLED)
        return E_INVALIDARG;
    DwRenderingParams *p = DwAllocZero(sizeof(*p));
    if (!p)
        return E_OUTOFMEMORY;
    p->lpVtbl = &g_params_vtbl;
    p->refs = 1;
    p->gamma = gamma;
    p->enhanced_contrast = contrast;
    p->grayscale_contrast = grayscale_contrast;
    p->cleartype_level = cleartype_level;
    p->geometry = geometry;
    /* the older interfaces see the downsampled mode as the symmetric one it refines. */
    p->mode = mode == DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED ? DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC
                                                                           : (DWRITE_RENDERING_MODE)mode;
    p->mode1 = mode;
    p->grid_fit = grid_fit;
    *out = p;
    return S_OK;
}

/* ------------------------------------------------------------------ rasterizer allocator */

static void *ttf_alloc(void *ctx, size_t size) { return DwAlloc(size); }
static void ttf_free(void *ctx, void *p) { DwFree(p); }
static const MimeAllocator g_ttf_alloc = {ttf_alloc, ttf_free, nullptr};

/* ------------------------------------------------------------------ glyph run analysis */

typedef struct DwAnalysis {
    const IDWriteGlyphRunAnalysisVtbl *lpVtbl;
    LONG refs;
    DwFace *face;
    UINT32 count;
    UINT16 *glyphs;
    float *x, *y; /* device position of each glyph's origin. */
    /* font units, y up, to device pixels, y down. */
    float xx, xy, yx, yy;
    DWRITE_RENDERING_MODE mode;
    BOOL bold;
    BOOL grayscale; /* the aliased texture holds coverage instead of on and off. */
    int over_y; /* ClearType rows a pixel is scanned in, five for the symmetric modes. */
    MimeTtfHinter *hinter; /* outlines are hinted for the size when it is set. */
} DwAnalysis;

static HRESULT WINAPI analysis_query(IDWriteGlyphRunAnalysis *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteGlyphRunAnalysis)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI analysis_addref(IDWriteGlyphRunAnalysis *This)
{
    return InterlockedIncrement(&((DwAnalysis *)This)->refs);
}

static ULONG WINAPI analysis_release(IDWriteGlyphRunAnalysis *This)
{
    DwAnalysis *a = (DwAnalysis *)This;
    ULONG refs = InterlockedDecrement(&a->refs);
    if (!refs) {
        ((IDWriteFontFace *)a->face)->lpVtbl->Release((IDWriteFontFace *)a->face);
        DwFree(a->glyphs);
        DwFree(a->x);
        DwFree(a);
    }
    return refs;
}

static void glyph_params(const DwAnalysis *a, UINT32 i, float x_scale, float shift_x, float shift_y,
                         MimeTtfRenderParams *p)
{
    memset(p, 0, sizeof(*p));
    p->use_matrix = true;
    p->xx = a->xx * x_scale;
    p->xy = a->xy * x_scale;
    p->yx = a->yx;
    p->yy = a->yy;
    if (a->hinter) {
        /* the hinted outline is in pixels already. */
        p->hinter = a->hinter;
        p->xx = x_scale;
        p->xy = p->yx = 0;
        p->yy = -1;
    }
    p->shift_x = a->x[i] * x_scale + shift_x;
    p->shift_y = a->y[i] + shift_y;
    if (a->bold) {
        /* a simulated bold widens each stem by a thirty second of the em. */
        float em = absf(a->xx) * (float)a->face->data->ttf.units_per_em;
        p->embolden = em * x_scale / 32.0f;
    }
}

static int ifloor(float v)
{
    int i = (int)v;
    return (float)i > v ? i - 1 : i;
}

static void run_bounds(const DwAnalysis *a, float x_scale, RECT *box)
{
    box->left = box->top = box->right = box->bottom = 0;
    BOOL any = FALSE;
    for (UINT32 i = 0; i < a->count; i++) {
        MimeTtfRenderParams p;
        glyph_params(a, i, x_scale, 0, 0, &p);
        int x0, y0, x1, y1;
        if (!mime_ttf_glyph_box_ex(&a->face->data->ttf, a->glyphs[i], &p, &x0, &y0, &x1, &y1) || x0 == x1 || y0 == y1)
            continue;
        if (!any) {
            box->left = x0;
            box->top = y0;
            box->right = x1;
            box->bottom = y1;
            any = TRUE;
        } else {
            if (x0 < box->left) box->left = x0;
            if (y0 < box->top) box->top = y0;
            if (x1 > box->right) box->right = x1;
            if (y1 > box->bottom) box->bottom = y1;
        }
    }
}

/* the glyph as the scan converter takes it: pixel positions, and the matrix or the hinted outline. */
static void scan_params(const DwAnalysis *a, UINT32 i, MimeTtfRenderParams *p, MimeTtfScan *scan)
{
    memset(p, 0, sizeof(*p));
    if (a->hinter) {
        p->hinter = a->hinter;
    } else {
        p->use_matrix = true;
        p->xx = a->xx;
        p->xy = a->xy;
        p->yx = a->yx;
        p->yy = a->yy;
    }
    p->shift_x = a->x[i];
    p->shift_y = a->y[i];
    scan->control = scan->type = 0;
    scan->ppem = (int)(absf(a->yy) * (float)a->face->data->ttf.units_per_em + 0.5f);
    scan->rotated = a->xy != 0 && a->yx != 0;
    scan->stretched = false;
    if (a->hinter)
        mime_ttf_hinter_scan(a->hinter, &scan->control, &scan->type);
}

/* the pixel box and levels of each glyph of the run, false when a glyph could not be drawn. */
static BOOL glyph_clear_type(const DwAnalysis *a, UINT32 i, MimeTtfBitmap *bm)
{
    MimeTtfRenderParams p;
    MimeTtfScan scan;
    scan_params(a, i, &p, &scan);
    return mime_ttf_cleartype_bitmap(&a->face->data->ttf, &g_ttf_alloc, a->glyphs[i], &p, a->over_y, &scan, bm);
}

static HRESULT WINAPI analysis_bounds(IDWriteGlyphRunAnalysis *This, DWRITE_TEXTURE_TYPE type, RECT *bounds)
{
    DwAnalysis *a = (DwAnalysis *)This;
    if (!bounds)
        return E_POINTER;
    if (type != DWRITE_TEXTURE_ALIASED_1x1 && type != DWRITE_TEXTURE_CLEARTYPE_3x1) {
        memset(bounds, 0, sizeof(*bounds));
        return E_INVALIDARG;
    }
    if (type == DWRITE_TEXTURE_CLEARTYPE_3x1) {
        memset(bounds, 0, sizeof(*bounds));
        BOOL any = FALSE;
        for (UINT32 i = 0; i < a->count; i++) {
            MimeTtfBitmap bm;
            if (!glyph_clear_type(a, i, &bm))
                continue;
            if (bm.data) {
                if (!any) {
                    bounds->left = bm.x0;
                    bounds->top = bm.y0;
                    bounds->right = bm.x1;
                    bounds->bottom = bm.y1;
                    any = TRUE;
                } else {
                    if (bm.x0 < bounds->left) bounds->left = bm.x0;
                    if (bm.y0 < bounds->top) bounds->top = bm.y0;
                    if (bm.x1 > bounds->right) bounds->right = bm.x1;
                    if (bm.y1 > bounds->bottom) bounds->bottom = bm.y1;
                }
            }
            mime_ttf_bitmap_free(&g_ttf_alloc, &bm);
        }
        return S_OK;
    }
    if (!a->grayscale) {
        memset(bounds, 0, sizeof(*bounds));
        BOOL any = FALSE;
        for (UINT32 i = 0; i < a->count; i++) {
            MimeTtfRenderParams p;
            MimeTtfScan scan;
            MimeTtfBitmap bm;
            scan_params(a, i, &p, &scan);
            if (!mime_ttf_scan_bitmap(&a->face->data->ttf, &g_ttf_alloc, a->glyphs[i], &p, &scan, &bm))
                continue;
            if (bm.data) {
                if (!any) {
                    bounds->left = bm.x0;
                    bounds->top = bm.y0;
                    bounds->right = bm.x1;
                    bounds->bottom = bm.y1;
                    any = TRUE;
                } else {
                    if (bm.x0 < bounds->left) bounds->left = bm.x0;
                    if (bm.y0 < bounds->top) bounds->top = bm.y0;
                    if (bm.x1 > bounds->right) bounds->right = bm.x1;
                    if (bm.y1 > bounds->bottom) bounds->bottom = bm.y1;
                }
            }
            mime_ttf_bitmap_free(&g_ttf_alloc, &bm);
        }
        return S_OK;
    }
    run_bounds(a, 1.0f, bounds);
    return S_OK;
}

/* overlapping coverage adds like alpha, so it never exceeds 255. */
static inline UINT8 combine(UINT8 under, UINT8 over)
{
    return (UINT8)(under + over - (under * over + 127) / 255);
}

static HRESULT WINAPI analysis_texture(IDWriteGlyphRunAnalysis *This, DWRITE_TEXTURE_TYPE type, const RECT *bounds,
                                       BYTE *alpha, UINT32 size)
{
    DwAnalysis *a = (DwAnalysis *)This;
    if (!bounds || !alpha)
        return E_POINTER;
    if (type != DWRITE_TEXTURE_ALIASED_1x1 && type != DWRITE_TEXTURE_CLEARTYPE_3x1)
        return E_INVALIDARG;
    int width = bounds->right - bounds->left, height = bounds->bottom - bounds->top;
    if (width < 0 || height < 0)
        return E_INVALIDARG;
    UINT32 bytes_per_pixel = type == DWRITE_TEXTURE_CLEARTYPE_3x1 ? 3 : 1;
    UINT64 needed = (UINT64)width * height * bytes_per_pixel;
    if (needed > size)
        return E_INVALIDARG;
    memset(alpha, 0, (size_t)needed);
    if (!needed)
        return S_OK;
    if (type == DWRITE_TEXTURE_CLEARTYPE_3x1 || !a->grayscale) {
        for (UINT32 i = 0; i < a->count; i++) {
            MimeTtfBitmap bm;
            if (type == DWRITE_TEXTURE_CLEARTYPE_3x1) {
                if (!glyph_clear_type(a, i, &bm))
                    continue;
            } else {
                MimeTtfRenderParams p;
                MimeTtfScan scan;
                scan_params(a, i, &p, &scan);
                if (!mime_ttf_scan_bitmap(&a->face->data->ttf, &g_ttf_alloc, a->glyphs[i], &p, &scan, &bm))
                    continue;
            }
            for (int row = 0; bm.data && row < bm.y1 - bm.y0; row++) {
                int y = bm.y0 + row - bounds->top;
                if (y < 0 || y >= height)
                    continue;
                const UINT8 *src = bm.data + (size_t)row * (size_t)bm.stride;
                UINT8 *dst = alpha + (size_t)y * width * bytes_per_pixel;
                int cells = (bm.x1 - bm.x0) * (int)bytes_per_pixel;
                for (int col = 0; col < cells; col++) {
                    int x = (bm.x0 - bounds->left) * (int)bytes_per_pixel + col;
                    if (x < 0 || x >= width * (int)bytes_per_pixel || !src[col])
                        continue;
                    if (type == DWRITE_TEXTURE_CLEARTYPE_3x1)
                        dst[x] = combine(dst[x], (UINT8)((src[col] * 255 + 3) / 6));
                    else
                        dst[x] = 255;
                }
            }
            mime_ttf_bitmap_free(&g_ttf_alloc, &bm);
        }
        return S_OK;
    }
    for (UINT32 i = 0; i < a->count; i++) {
        MimeTtfRenderParams p;
        /* the bitmap's origin is the corner of the requested area. */
        glyph_params(a, i, 1.0f, -(float)bounds->left, -(float)bounds->top, &p);
        int x0, y0, x1, y1;
        if (!mime_ttf_glyph_box_ex(&a->face->data->ttf, a->glyphs[i], &p, &x0, &y0, &x1, &y1) || x0 == x1 || y0 == y1)
            continue;
        int gw = x1 - x0, gh = y1 - y0;
        UINT8 *bitmap = DwAlloc((size_t)gw * gh);
        if (!bitmap)
            return E_OUTOFMEMORY;
        if (!mime_ttf_render_glyph_ex(&a->face->data->ttf, &g_ttf_alloc, a->glyphs[i], &p, bitmap, gw, gh, gw)) {
            DwFree(bitmap);
            continue;
        }
        for (int row = 0; row < gh; row++) {
            int y = y0 + row;
            if (y < 0 || y >= height)
                continue;
            const UINT8 *src = bitmap + (size_t)row * gw;
            UINT8 *dst = alpha + (size_t)y * width;
            for (int col = 0; col < gw; col++) {
                int x = x0 + col;
                if (x < 0 || x >= width || !src[col])
                    continue;
                dst[x] = combine(dst[x], src[col]);
            }
        }
        DwFree(bitmap);
    }
    return S_OK;
}

static HRESULT WINAPI analysis_blend(IDWriteGlyphRunAnalysis *This, IDWriteRenderingParams *params, FLOAT *gamma,
                                     FLOAT *contrast, FLOAT *level)
{
    if (!gamma || !contrast || !level)
        return E_POINTER;
    if (!params)
        return E_INVALIDARG;
    *gamma = params->lpVtbl->GetGamma(params);
    *contrast = params->lpVtbl->GetEnhancedContrast(params);
    *level = params->lpVtbl->GetClearTypeLevel(params);
    return S_OK;
}

static const IDWriteGlyphRunAnalysisVtbl g_analysis_vtbl = {
    analysis_query, analysis_addref, analysis_release, analysis_bounds, analysis_texture, analysis_blend,
};

HRESULT DwGlyphRunAnalysisCreate(const DWRITE_GLYPH_RUN *run, FLOAT pixels_per_dip, const DWRITE_MATRIX *transform,
                                 DWRITE_RENDERING_MODE mode, DWRITE_MEASURING_MODE measuring,
                                 DWRITE_TEXT_ANTIALIAS_MODE antialias, FLOAT origin_x, FLOAT origin_y,
                                 IDWriteGlyphRunAnalysis **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!run || !run->fontFace || (run->glyphCount && !run->glyphIndices) || !(pixels_per_dip > 0) ||
        !(run->fontEmSize >= 0) || (unsigned)mode > DWRITE_RENDERING_MODE_OUTLINE ||
        (unsigned)measuring > DWRITE_MEASURING_MODE_GDI_NATURAL || run->glyphCount > 0x100000)
        return E_INVALIDARG;
    DwFace *face = DwFaceFrom(run->fontFace);
    if (!face)
        return E_INVALIDARG;
    DwAnalysis *a = DwAllocZero(sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->lpVtbl = &g_analysis_vtbl;
    a->refs = 1;
    a->face = face;
    ((IDWriteFontFace *)face)->lpVtbl->AddRef((IDWriteFontFace *)face);
    a->count = run->glyphCount;
    a->mode = mode;
    a->over_y = mode == DWRITE_RENDERING_MODE_DEFAULT || mode == DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC ? 5 : 1;
    a->grayscale = antialias == DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE && mode != DWRITE_RENDERING_MODE_ALIASED;
    a->bold = (face->simulations & DWRITE_FONT_SIMULATIONS_BOLD) != 0;
    a->glyphs = DwAlloc(run->glyphCount * sizeof(UINT16));
    a->x = DwAlloc(run->glyphCount * 2 * sizeof(float));
    if (!a->glyphs || !a->x) {
        analysis_release((IDWriteGlyphRunAnalysis *)a);
        return E_OUTOFMEMORY;
    }
    a->y = a->x + run->glyphCount;
    memcpy(a->glyphs, run->glyphIndices, run->glyphCount * sizeof(UINT16));
    float upem = (float)face->metrics.designUnitsPerEm;
    float scale = run->fontEmSize * pixels_per_dip / upem;
    DWRITE_MATRIX m = transform ? *transform : (DWRITE_MATRIX){1, 0, 0, 1, 0, 0};
    a->xx = m.m11 * scale;
    a->xy = -m.m21 * scale;
    a->yx = m.m12 * scale;
    a->yy = -m.m22 * scale;
    if (face->simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) {
        a->xy += a->xx * OBLIQUE_SHEAR;
        a->yy += a->yx * OBLIQUE_SHEAR;
    }
    /* glyphs at a whole pixel size without a transform are hinted, and sit on whole pixels so the hints hold. */
    float ppem_exact = run->fontEmSize * pixels_per_dip;
    int ppem = (int)(ppem_exact + 0.5f);
    BOOL plain = m.m11 == 1 && m.m22 == 1 && m.m12 == 0 && m.m21 == 0 && !face->simulations;
    UINT32 hint_flags = 0;
    BOOL natural_positions = FALSE;
    if (plain && ppem >= 1 && absf(ppem_exact - (float)ppem) < 0.001f && mode != DWRITE_RENDERING_MODE_OUTLINE) {
        DWRITE_RENDERING_MODE kind = mode == DWRITE_RENDERING_MODE_DEFAULT ? DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC : mode;
        if (kind != DWRITE_RENDERING_MODE_ALIASED) {
            hint_flags = MIME_TTF_HINT_CLEARTYPE;
            if (a->grayscale)
                hint_flags |= MIME_TTF_HINT_GRAY;
            if (kind == DWRITE_RENDERING_MODE_GDI_CLASSIC)
                hint_flags |= MIME_TTF_HINT_COMPAT_WIDTHS;
            if (kind == DWRITE_RENDERING_MODE_NATURAL || kind == DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC) {
                hint_flags |= MIME_TTF_HINT_SUBPIXEL;
                natural_positions = TRUE;
            }
            if (kind == DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC)
                hint_flags |= MIME_TTF_HINT_SYMMETRIC;
        }
        a->hinter = DwFontDataHinter(face->data, ppem, hint_flags);
    }
    float pen = 0;
    for (UINT32 i = 0; i < run->glyphCount; i++) {
        float advance_offset = run->glyphOffsets ? run->glyphOffsets[i].advanceOffset : 0;
        float ascender_offset = run->glyphOffsets ? run->glyphOffsets[i].ascenderOffset : 0;
        float lx = (origin_x + pen + advance_offset) * pixels_per_dip;
        float ly = (origin_y - ascender_offset) * pixels_per_dip;
        a->x[i] = m.m11 * lx + m.m21 * ly + m.dx;
        a->y[i] = m.m12 * lx + m.m22 * ly + m.dy;
        if (a->hinter) {
            /* natural glyphs sit on sixths of a pixel across, the others on whole pixels. */
            float step = natural_positions ? 6.0f : 1.0f;
            a->x[i] = ifloor(a->x[i] * step + 0.5f) / step;
            a->y[i] = (float)ifloor(a->y[i] + 0.5f);
        }
        if (run->glyphAdvances) {
            pen += run->glyphAdvances[i];
        } else {
            int advance;
            mime_ttf_glyph_hmetrics(&face->data->ttf, run->glyphIndices[i], &advance, nullptr);
            pen += (float)advance * run->fontEmSize / upem;
        }
    }
    *out = (IDWriteGlyphRunAnalysis *)a;
    return S_OK;
}

/* ------------------------------------------------------------------ outlines */

typedef struct Contour {
    float (*points)[2];
    BOOL *on;
    UINT32 count, capacity;
    BOOL failed;
} Contour;

typedef struct OutlineState {
    Contour contour;
    IDWriteGeometrySink *sink;
    float xx, xy, yx, yy, tx, ty;
} OutlineState;

static void map_point(const OutlineState *s, float fx, float fy, D2D1_POINT_2F *out)
{
    out->x = s->xx * fx + s->xy * fy + s->tx;
    out->y = s->yx * fx + s->yy * fy + s->ty;
}

static void outline_point(void *ctx, float x, float y, bool on)
{
    OutlineState *s = ctx;
    Contour *c = &s->contour;
    if (c->failed)
        return;
    if (c->count == c->capacity) {
        UINT32 capacity = c->capacity ? c->capacity * 2 : 64;
        float(*points)[2] = DwAlloc(capacity * sizeof(*points));
        BOOL *flags = DwAlloc(capacity * sizeof(BOOL));
        if (!points || !flags) {
            DwFree(points);
            DwFree(flags);
            c->failed = TRUE;
            return;
        }
        if (c->count) {
            memcpy(points, c->points, c->count * sizeof(*points));
            memcpy(flags, c->on, c->count * sizeof(BOOL));
        }
        DwFree(c->points);
        DwFree(c->on);
        c->points = points;
        c->on = flags;
        c->capacity = capacity;
    }
    c->points[c->count][0] = x;
    c->points[c->count][1] = y;
    c->on[c->count] = on;
    c->count++;
}

static D2D1_BEZIER_SEGMENT cubic_of(D2D1_POINT_2F p0, D2D1_POINT_2F c, D2D1_POINT_2F p1)
{
    D2D1_BEZIER_SEGMENT b;
    b.point1.x = p0.x + 2.0f / 3.0f * (c.x - p0.x);
    b.point1.y = p0.y + 2.0f / 3.0f * (c.y - p0.y);
    b.point2.x = p1.x + 2.0f / 3.0f * (c.x - p1.x);
    b.point2.y = p1.y + 2.0f / 3.0f * (c.y - p1.y);
    b.point3 = p1;
    return b;
}

/* on curve points are corners, off curve ones quadratic controls. */
static void outline_end(void *ctx)
{
    OutlineState *s = ctx;
    Contour *c = &s->contour;
    UINT32 n = c->count;
    c->count = 0;
    if (c->failed || n < 2)
        return;
    IDWriteGeometrySink *sink = s->sink;
    /* the first on curve point starts the figure, or the middle of two off curve ones. */
    UINT32 first = 0;
    while (first < n && !c->on[first])
        first++;
    D2D1_POINT_2F start, current;
    UINT32 begin;
    if (first < n) {
        map_point(s, c->points[first][0], c->points[first][1], &start);
        begin = first + 1;
    } else {
        D2D1_POINT_2F a, b;
        map_point(s, c->points[0][0], c->points[0][1], &a);
        map_point(s, c->points[1][0], c->points[1][1], &b);
        start.x = (a.x + b.x) * 0.5f;
        start.y = (a.y + b.y) * 0.5f;
        first = 0;
        begin = 1;
    }
    current = start;
    sink->lpVtbl->BeginFigure(sink, start, D2D1_FIGURE_BEGIN_FILLED);
    BOOL pending = FALSE;
    D2D1_POINT_2F control = {};
    /* walk once around the contour, ending back on the start. */
    UINT32 total = first < n && c->on[first] ? n : n + 1;
    for (UINT32 k = 0; k < total; k++) {
        UINT32 i = (begin + k) % n;
        BOOL last = k + 1 == total;
        D2D1_POINT_2F p;
        BOOL on;
        if (last) {
            p = start;
            on = TRUE;
        } else {
            map_point(s, c->points[i][0], c->points[i][1], &p);
            on = c->on[i];
        }
        if (on) {
            if (pending) {
                D2D1_BEZIER_SEGMENT b = cubic_of(current, control, p);
                sink->lpVtbl->AddBeziers(sink, &b, 1);
                pending = FALSE;
            } else {
                sink->lpVtbl->AddLines(sink, &p, 1);
            }
            current = p;
        } else {
            if (pending) {
                D2D1_POINT_2F mid = {(control.x + p.x) * 0.5f, (control.y + p.y) * 0.5f};
                D2D1_BEZIER_SEGMENT b = cubic_of(current, control, mid);
                sink->lpVtbl->AddBeziers(sink, &b, 1);
                current = mid;
            }
            control = p;
            pending = TRUE;
        }
    }
    sink->lpVtbl->EndFigure(sink, D2D1_FIGURE_END_CLOSED);
}

HRESULT DwGlyphRunOutline(DwFace *face, FLOAT em_size, const UINT16 *glyphs, const FLOAT *advances,
                          const DWRITE_GLYPH_OFFSET *offsets, UINT32 count, BOOL sideways, BOOL rtl,
                          IDWriteGeometrySink *sink)
{
    if (!sink || (count && !glyphs) || !(em_size >= 0))
        return E_INVALIDARG;
    OutlineState state = {};
    state.sink = sink;
    float scale = em_size / (float)face->metrics.designUnitsPerEm;
    state.xx = scale;
    state.yy = -scale;
    if (face->simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE)
        state.xy = scale * OBLIQUE_SHEAR;
    sink->lpVtbl->SetFillMode(sink, D2D1_FILL_MODE_WINDING);
    MimeTtfSink walker = {outline_point, outline_end, &state};
    float pen = 0;
    HRESULT hr = S_OK;
    for (UINT32 i = 0; i < count; i++) {
        if (glyphs[i] >= face->data->ttf.num_glyphs) {
            hr = E_INVALIDARG;
            break;
        }
        state.tx = pen + (offsets ? offsets[i].advanceOffset : 0);
        state.ty = offsets ? -offsets[i].ascenderOffset : 0;
        /* a face without outlines draws nothing. */
        mime_ttf_walk_outline(&face->data->ttf, glyphs[i], &walker);
        if (state.contour.failed) {
            hr = E_OUTOFMEMORY;
            break;
        }
        if (advances) {
            pen += advances[i];
        } else {
            int advance;
            mime_ttf_glyph_hmetrics(&face->data->ttf, glyphs[i], &advance, nullptr);
            pen += (float)advance * scale;
        }
    }
    DwFree(state.contour.points);
    DwFree(state.contour.on);
    return hr;
}
