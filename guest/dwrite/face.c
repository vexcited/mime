/*
 * IDWriteFontFace: one face of a font file, with the metrics and tables
 * its data gives and the glyph runs it draws.
 */
#include "dw_private.h"

static float round_f(float v) { return v >= 0 ? (float)(int)(v + 0.5f) : (float)(int)(v - 0.5f); }

DwFace *DwFaceFrom(IDWriteFontFace *face);

static HRESULT WINAPI face_query(IDWriteFontFace2 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontFace) ||
        IsEqualGUID(riid, &IID_IDWriteFontFace1) || IsEqualGUID(riid, &IID_IDWriteFontFace2)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI face_addref(IDWriteFontFace2 *This) { return InterlockedIncrement(&((DwFace *)This)->refs); }

static ULONG WINAPI face_release(IDWriteFontFace2 *This)
{
    DwFace *f = (DwFace *)This;
    ULONG refs = InterlockedDecrement(&f->refs);
    if (!refs) {
        DwFontDataRelease(f->data);
        f->file->lpVtbl->Release(f->file);
        DwFree(f);
    }
    return refs;
}

static DWRITE_FONT_FACE_TYPE WINAPI face_type(IDWriteFontFace2 *This) { return ((DwFace *)This)->type; }

static HRESULT WINAPI face_files(IDWriteFontFace2 *This, UINT32 *count, IDWriteFontFile **files)
{
    DwFace *f = (DwFace *)This;
    if (!count)
        return E_INVALIDARG;
    if (!files) {
        *count = 1;
        return S_OK;
    }
    if (*count < 1)
        return E_INVALIDARG;
    *count = 1;
    files[0] = f->file;
    f->file->lpVtbl->AddRef(f->file);
    return S_OK;
}

static UINT32 WINAPI face_index(IDWriteFontFace2 *This) { return ((DwFace *)This)->index; }
static DWRITE_FONT_SIMULATIONS WINAPI face_simulations(IDWriteFontFace2 *This)
{
    return ((DwFace *)This)->simulations;
}
static BOOL WINAPI face_symbol(IDWriteFontFace2 *This)
{
    DwAttributes a;
    DwFontDataAttributes(((DwFace *)This)->data, &a);
    return a.symbol;
}

static void WINAPI face_metrics(IDWriteFontFace2 *This, DWRITE_FONT_METRICS *metrics)
{
    memcpy(metrics, &((DwFace *)This)->metrics, sizeof(*metrics));
}

static UINT16 WINAPI face_glyph_count(IDWriteFontFace2 *This) { return ((DwFace *)This)->data->ttf.num_glyphs; }

static HRESULT WINAPI face_design_metrics(IDWriteFontFace2 *This, const UINT16 *glyphs, UINT32 count,
                                          DWRITE_GLYPH_METRICS *metrics, BOOL sideways)
{
    DwFace *f = (DwFace *)This;
    if (count && (!glyphs || !metrics))
        return E_INVALIDARG;
    for (UINT32 i = 0; i < count; i++) {
        if (glyphs[i] >= f->data->ttf.num_glyphs) {
            memset(&metrics[i], 0, sizeof(metrics[i]));
            return E_INVALIDARG;
        }
        DwFontDataGlyphMetrics(f->data, glyphs[i], &metrics[i]);
    }
    return S_OK;
}

static HRESULT WINAPI face_glyph_indices(IDWriteFontFace2 *This, const UINT32 *code_points, UINT32 count,
                                         UINT16 *glyphs)
{
    DwFace *f = (DwFace *)This;
    if (count && (!code_points || !glyphs))
        return E_INVALIDARG;
    for (UINT32 i = 0; i < count; i++)
        glyphs[i] = mime_ttf_glyph_index(&f->data->ttf, code_points[i]);
    return S_OK;
}

static HRESULT WINAPI face_try_table(IDWriteFontFace2 *This, UINT32 tag, const void **data, UINT32 *size,
                                     void **context, BOOL *exists)
{
    DwFace *f = (DwFace *)This;
    if (!data || !size || !context || !exists)
        return E_POINTER;
    /* the tag comes with its first letter in the low byte. */
    UINT32 swapped = (tag << 24) | ((tag & 0xff00) << 8) | ((tag >> 8) & 0xff00) | (tag >> 24);
    UINT32 length = 0;
    const BYTE *table = DwFontDataTable(f->data, swapped, &length);
    *context = nullptr;
    *data = table;
    *size = table ? length : 0;
    *exists = table != nullptr;
    return S_OK;
}

static void WINAPI face_release_table(IDWriteFontFace2 *This, void *context) {}

static HRESULT WINAPI face_outline(IDWriteFontFace2 *This, FLOAT em_size, const UINT16 *glyphs, const FLOAT *advances,
                                   const DWRITE_GLYPH_OFFSET *offsets, UINT32 count, BOOL sideways, BOOL rtl,
                                   IDWriteGeometrySink *sink)
{
    return DwGlyphRunOutline((DwFace *)This, em_size, glyphs, advances, offsets, count, sideways, rtl, sink);
}

DWRITE_RENDERING_MODE DwRecommendedMode(FLOAT pixels_per_em, DWRITE_MEASURING_MODE measuring,
                                        DWRITE_RENDERING_MODE requested)
{
    if (requested != DWRITE_RENDERING_MODE_DEFAULT)
        return requested;
    if (pixels_per_em > 2048)
        return DWRITE_RENDERING_MODE_OUTLINE;
    if (measuring == DWRITE_MEASURING_MODE_GDI_CLASSIC)
        return DWRITE_RENDERING_MODE_GDI_CLASSIC;
    if (measuring == DWRITE_MEASURING_MODE_GDI_NATURAL)
        return DWRITE_RENDERING_MODE_GDI_NATURAL;
    return pixels_per_em < 16 ? DWRITE_RENDERING_MODE_NATURAL : DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
}

static HRESULT WINAPI face_recommended(IDWriteFontFace2 *This, FLOAT em_size, FLOAT pixels_per_dip,
                                       DWRITE_MEASURING_MODE measuring, IDWriteRenderingParams *params,
                                       DWRITE_RENDERING_MODE *mode)
{
    if (!mode)
        return E_POINTER;
    if (!(em_size > 0) || !(pixels_per_dip > 0))
        return E_INVALIDARG;
    if ((unsigned)measuring > DWRITE_MEASURING_MODE_GDI_NATURAL)
        return E_INVALIDARG;
    DWRITE_RENDERING_MODE requested = params ? params->lpVtbl->GetRenderingMode(params) : DWRITE_RENDERING_MODE_DEFAULT;
    *mode = DwRecommendedMode(em_size * pixels_per_dip, measuring, requested);
    return S_OK;
}

/* the metrics GDI would have: sizes in whole pixels, given back in design units. */
static void gdi_round_metrics(const DwFace *f, FLOAT em_size, FLOAT pixels_per_dip, DWRITE_FONT_METRICS1 *out)
{
    *out = f->metrics;
    float ppem = round_f(em_size * pixels_per_dip);
    if (!(ppem >= 1))
        return;
    float upem = f->metrics.designUnitsPerEm, scale = ppem / upem;
#define ROUND(field) out->field = (__typeof__(out->field))round_f(round_f((float)f->metrics.field * scale) / scale)
    ROUND(ascent);
    ROUND(descent);
    ROUND(lineGap);
    ROUND(capHeight);
    ROUND(xHeight);
    ROUND(underlinePosition);
    ROUND(underlineThickness);
    ROUND(strikethroughPosition);
    ROUND(strikethroughThickness);
#undef ROUND
}

static HRESULT WINAPI face_gdi_metrics(IDWriteFontFace2 *This, FLOAT em_size, FLOAT pixels_per_dip,
                                       const DWRITE_MATRIX *transform, DWRITE_FONT_METRICS *metrics)
{
    if (!metrics)
        return E_POINTER;
    if (!(em_size > 0) || !(pixels_per_dip > 0))
        return E_INVALIDARG;
    DWRITE_FONT_METRICS1 full;
    gdi_round_metrics((DwFace *)This, em_size, pixels_per_dip, &full);
    memcpy(metrics, &full, sizeof(*metrics));
    return S_OK;
}

static HRESULT WINAPI face_gdi_glyph_metrics(IDWriteFontFace2 *This, FLOAT em_size, FLOAT pixels_per_dip,
                                             const DWRITE_MATRIX *transform, BOOL natural, const UINT16 *glyphs,
                                             UINT32 count, DWRITE_GLYPH_METRICS *metrics, BOOL sideways)
{
    DwFace *f = (DwFace *)This;
    if (!(em_size > 0) || !(pixels_per_dip > 0) || (count && (!glyphs || !metrics)))
        return E_INVALIDARG;
    float ppem = round_f(em_size * pixels_per_dip);
    float scale = ppem >= 1 ? ppem / (float)f->metrics.designUnitsPerEm : 0;
    for (UINT32 i = 0; i < count; i++) {
        if (glyphs[i] >= f->data->ttf.num_glyphs) {
            memset(&metrics[i], 0, sizeof(metrics[i]));
            return E_INVALIDARG;
        }
        DwFontDataGlyphMetrics(f->data, glyphs[i], &metrics[i]);
        if (scale > 0) {
            /* the advance is a whole number of pixels, and the bearings keep the outline where it was. */
            float advance = round_f((float)metrics[i].advanceWidth * scale);
            int delta = (int)round_f(advance / scale) - (int)metrics[i].advanceWidth;
            metrics[i].advanceWidth = (UINT32)((int)metrics[i].advanceWidth + delta);
            metrics[i].rightSideBearing += delta;
        }
    }
    return S_OK;
}

static void WINAPI face_metrics1(IDWriteFontFace2 *This, DWRITE_FONT_METRICS1 *metrics)
{
    *metrics = ((DwFace *)This)->metrics;
}

static HRESULT WINAPI face_gdi_metrics1(IDWriteFontFace2 *This, FLOAT em_size, FLOAT pixels_per_dip,
                                        const DWRITE_MATRIX *transform, DWRITE_FONT_METRICS1 *metrics)
{
    if (!metrics)
        return E_POINTER;
    if (!(em_size > 0) || !(pixels_per_dip > 0))
        return E_INVALIDARG;
    gdi_round_metrics((DwFace *)This, em_size, pixels_per_dip, metrics);
    return S_OK;
}

static void WINAPI face_caret(IDWriteFontFace2 *This, DWRITE_CARET_METRICS *metrics)
{
    DwFace *f = (DwFace *)This;
    memset(metrics, 0, sizeof(*metrics));
    const MimeTtfTable *hhea = &f->data->ttf.hhea;
    if (hhea->data && hhea->size >= 22) {
        metrics->slopeRise = (INT16)DwBe16(hhea->data + 18);
        metrics->slopeRun = (INT16)DwBe16(hhea->data + 20);
        metrics->offset = (INT16)DwBe16(hhea->data + 22);
    }
}

static HRESULT WINAPI face_ranges(IDWriteFontFace2 *This, UINT32 max, DWRITE_UNICODE_RANGE *ranges, UINT32 *actual)
{
    if (!actual)
        return E_POINTER;
    *actual = DwFontDataRanges(((DwFace *)This)->data, ranges, max);
    return *actual > max ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) : S_OK;
}

static BOOL WINAPI face_monospaced(IDWriteFontFace2 *This)
{
    DwAttributes a;
    DwFontDataAttributes(((DwFace *)This)->data, &a);
    return a.monospaced;
}

static HRESULT WINAPI face_design_advances(IDWriteFontFace2 *This, UINT32 count, const UINT16 *glyphs, INT32 *advances,
                                           BOOL sideways)
{
    DwFace *f = (DwFace *)This;
    if (count && (!glyphs || !advances))
        return E_INVALIDARG;
    for (UINT32 i = 0; i < count; i++) {
        if (glyphs[i] >= f->data->ttf.num_glyphs) {
            advances[i] = 0;
            return E_INVALIDARG;
        }
        int advance;
        mime_ttf_glyph_hmetrics(&f->data->ttf, glyphs[i], &advance, nullptr);
        advances[i] = sideways ? f->metrics.ascent + f->metrics.descent : advance;
    }
    return S_OK;
}

static HRESULT WINAPI face_gdi_advances(IDWriteFontFace2 *This, FLOAT em_size, FLOAT pixels_per_dip,
                                        const DWRITE_MATRIX *transform, BOOL natural, BOOL sideways, UINT32 count,
                                        const UINT16 *glyphs, INT32 *advances)
{
    DwFace *f = (DwFace *)This;
    if (!(em_size > 0) || !(pixels_per_dip > 0) || (count && (!glyphs || !advances)))
        return E_INVALIDARG;
    float ppem = round_f(em_size * pixels_per_dip);
    float scale = ppem >= 1 ? ppem / (float)f->metrics.designUnitsPerEm : 0;
    for (UINT32 i = 0; i < count; i++) {
        if (glyphs[i] >= f->data->ttf.num_glyphs) {
            advances[i] = 0;
            return E_INVALIDARG;
        }
        int advance;
        mime_ttf_glyph_hmetrics(&f->data->ttf, glyphs[i], &advance, nullptr);
        advances[i] = scale > 0 ? (INT32)round_f(round_f((float)advance * scale) / scale) : advance;
    }
    return S_OK;
}

static HRESULT WINAPI face_kerning(IDWriteFontFace2 *This, UINT32 count, const UINT16 *glyphs, INT32 *adjustments)
{
    DwFace *f = (DwFace *)This;
    if (count && (!glyphs || !adjustments))
        return E_INVALIDARG;
    for (UINT32 i = 0; i < count; i++)
        adjustments[i] = i + 1 < count ? mime_ttf_kerning(&f->data->ttf, glyphs[i], glyphs[i + 1]) : 0;
    return S_OK;
}

static BOOL WINAPI face_has_kerning(IDWriteFontFace2 *This) { return ((DwFace *)This)->data->ttf.kern_count != 0; }

static HRESULT WINAPI face_recommended1(IDWriteFontFace2 *This, FLOAT em_size, FLOAT dpi_x, FLOAT dpi_y,
                                        const DWRITE_MATRIX *transform, BOOL sideways, DWRITE_OUTLINE_THRESHOLD threshold,
                                        DWRITE_MEASURING_MODE measuring, DWRITE_RENDERING_MODE *mode)
{
    if (!mode)
        return E_POINTER;
    if (!(em_size > 0) || !(dpi_x > 0) || !(dpi_y > 0))
        return E_INVALIDARG;
    *mode = DwRecommendedMode(em_size * dpi_y / 96.0f, measuring, DWRITE_RENDERING_MODE_DEFAULT);
    return S_OK;
}

static HRESULT WINAPI face_vertical_variants(IDWriteFontFace2 *This, UINT32 count, const UINT16 *nominal,
                                             UINT16 *vertical)
{
    if (count && (!nominal || !vertical))
        return E_INVALIDARG;
    for (UINT32 i = 0; i < count; i++)
        vertical[i] = nominal[i];
    return S_OK;
}

static BOOL WINAPI face_has_vertical(IDWriteFontFace2 *This) { return FALSE; }

static BOOL WINAPI face_color(IDWriteFontFace2 *This) { return FALSE; }
static UINT32 WINAPI face_palette_count(IDWriteFontFace2 *This) { return 0; }
static UINT32 WINAPI face_entry_count(IDWriteFontFace2 *This) { return 0; }
static HRESULT WINAPI face_palette(IDWriteFontFace2 *This, UINT32 palette, UINT32 first, UINT32 count,
                                   DWRITE_COLOR_F *entries)
{
    return DWRITE_E_NOCOLOR;
}

static HRESULT WINAPI face_recommended2(IDWriteFontFace2 *This, FLOAT em_size, FLOAT dpi_x, FLOAT dpi_y,
                                        const DWRITE_MATRIX *transform, BOOL sideways, DWRITE_OUTLINE_THRESHOLD threshold,
                                        DWRITE_MEASURING_MODE measuring, IDWriteRenderingParams *params,
                                        DWRITE_RENDERING_MODE *mode, DWRITE_GRID_FIT_MODE *grid_fit)
{
    if (!mode || !grid_fit)
        return E_POINTER;
    if (!(em_size > 0) || !(dpi_x > 0) || !(dpi_y > 0))
        return E_INVALIDARG;
    DWRITE_RENDERING_MODE requested = params ? params->lpVtbl->GetRenderingMode(params) : DWRITE_RENDERING_MODE_DEFAULT;
    *mode = DwRecommendedMode(em_size * dpi_y / 96.0f, measuring, requested);
    *grid_fit = DWRITE_GRID_FIT_MODE_DISABLED;
    return S_OK;
}

static const IDWriteFontFace2Vtbl g_face_vtbl = {
    face_query,         face_addref,        face_release,        face_type,           face_files,
    face_index,         face_simulations,   face_symbol,         face_metrics,        face_glyph_count,
    face_design_metrics, face_glyph_indices, face_try_table,     face_release_table,  face_outline,
    face_recommended,   face_gdi_metrics,   face_gdi_glyph_metrics,
    face_metrics1,      face_gdi_metrics1,  face_caret,          face_ranges,         face_monospaced,
    face_design_advances, face_gdi_advances, face_kerning,       face_has_kerning,    face_recommended1,
    face_vertical_variants, face_has_vertical,
    face_color,         face_palette_count, face_entry_count,    face_palette,        face_recommended2,
};

DwFace *DwFaceFrom(IDWriteFontFace *face)
{
    return face && face->lpVtbl == (const IDWriteFontFaceVtbl *)&g_face_vtbl ? (DwFace *)face : nullptr;
}

HRESULT DwFaceCreate(DwFactory *factory, IDWriteFontFile *file, DWRITE_FONT_FACE_TYPE type, UINT32 index,
                     DWRITE_FONT_SIMULATIONS simulations, IDWriteFontFace **out)
{
    if (!file || !out)
        return E_INVALIDARG;
    *out = nullptr;
    if (simulations & ~(DWRITE_FONT_SIMULATIONS_BOLD | DWRITE_FONT_SIMULATIONS_OBLIQUE))
        return E_INVALIDARG;
    DwFontData *data;
    HRESULT hr = DwFontDataOpen(file, index, &data);
    if (FAILED(hr))
        return hr;
    DwFace *f = DwAllocZero(sizeof(*f));
    if (!f) {
        DwFontDataRelease(data);
        return E_OUTOFMEMORY;
    }
    f->lpVtbl = &g_face_vtbl;
    f->refs = 1;
    f->factory = factory;
    f->data = data;
    f->file = file;
    file->lpVtbl->AddRef(file);
    f->type = type == DWRITE_FONT_FACE_TYPE_UNKNOWN
                  ? (data->collection ? DWRITE_FONT_FACE_TYPE_TRUETYPE_COLLECTION
                     : data->cff      ? DWRITE_FONT_FACE_TYPE_CFF
                                      : DWRITE_FONT_FACE_TYPE_TRUETYPE)
                  : type;
    f->simulations = simulations;
    f->index = index;
    DwFontDataMetrics(data, &f->metrics);
    *out = (IDWriteFontFace *)f;
    return S_OK;
}
