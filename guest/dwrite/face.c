/*
 * IDWriteFontFace: one face of a font file, its metrics, tables and glyph
 * runs.
 */
#include "dw_private.h"

static float round_f(float v) { return v >= 0 ? (float)(int)(v + 0.5f) : (float)(int)(v - 0.5f); }

DwFace *DwFaceFrom(IDWriteFontFace *face);

static HRESULT WINAPI face_query(IDWriteFontFace2 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontFace) ||
        IsEqualGUID(riid, &IID_IDWriteFontFace1) || IsEqualGUID(riid, &IID_IDWriteFontFace2) ||
        IsEqualGUID(riid, &IID_IDWriteFontFace3) || IsEqualGUID(riid, &IID_IDWriteFontFace4) ||
        IsEqualGUID(riid, &IID_IDWriteFontFace5) || IsEqualGUID(riid, &IID_IDWriteFontFace6) ||
        IsEqualGUID(riid, &IID_IDWriteFontFace7)) {
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
    /* tags come with the first letter in the low byte. */
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

/* the whole pixel advance of a glyph given back in design units. */
int DwGdiAdvanceDesign(const DwFace *f, UINT16 glyph, int ppem, BOOL natural)
{
    int pixels = DwGdiAdvancePixels(f->data, glyph, ppem, natural);
    return (int)round_f((float)pixels * (float)f->metrics.designUnitsPerEm / (float)ppem);
}

/* GDI's metrics: whole pixel sizes, given back in design units. */
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
    /* fonts with a VDMX table say the ascent and descent they need at the size. */
    int y_max, y_min;
    if (mime_ttf_vdmx(&f->data->ttf, 1, 1, (int)ppem, &y_max, &y_min) && y_max >= 0 && y_min <= 0) {
        out->ascent = (UINT16)round_f((float)y_max / scale);
        out->descent = (UINT16)round_f((float)-y_min / scale);
    }
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
    int ppem = (int)round_f(em_size * pixels_per_dip);
    for (UINT32 i = 0; i < count; i++) {
        if (glyphs[i] >= f->data->ttf.num_glyphs) {
            memset(&metrics[i], 0, sizeof(metrics[i]));
            return E_INVALIDARG;
        }
        DwFontDataGlyphMetrics(f->data, glyphs[i], &metrics[i]);
        if (ppem >= 1) {
            /* the advance is whole pixels, and the bearings keep the outline in place. */
            int delta = DwGdiAdvanceDesign(f, glyphs[i], ppem, natural) - (int)metrics[i].advanceWidth;
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
    int ppem = (int)round_f(em_size * pixels_per_dip);
    for (UINT32 i = 0; i < count; i++) {
        if (glyphs[i] >= f->data->ttf.num_glyphs) {
            advances[i] = 0;
            return E_INVALIDARG;
        }
        int advance;
        mime_ttf_glyph_hmetrics(&f->data->ttf, glyphs[i], &advance, nullptr);
        advances[i] = ppem >= 1 ? DwGdiAdvanceDesign(f, glyphs[i], ppem, natural) : advance;
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

/* ------------------------------------------------------------------ versions three to seven */

static DwFontKey face_key(const DwFace *f)
{
    DwFontKey key = {f->file, f->index, f->simulations};
    key.axis_count = f->axis_count;
    if (f->axis_count)
        memcpy(key.axes, f->axes, f->axis_count * sizeof(f->axes[0]));
    return key;
}

static HRESULT WINAPI face_reference(IDWriteFontFace2 *This, IDWriteFontFaceReference **reference)
{
    DwFace *f = (DwFace *)This;
    if (!reference)
        return E_POINTER;
    DwFontKey key = face_key(f);
    return DwFaceReferenceCreate(f->factory, &key, (IDWriteFontFaceReference1 **)reference);
}

static void WINAPI face_panose(IDWriteFontFace2 *This, DWRITE_PANOSE *panose)
{
    DwFace *f = (DwFace *)This;
    memset(panose, 0, sizeof(*panose));
    if (f->data->ttf.os2.data && f->data->ttf.os2.size >= 42)
        memcpy(panose->values, f->data->ttf.os2.data + 32, 10);
}

/* the attributes of the face's family model, with the instance and the simulations applied. */
static void face_attributes(const DwFace *f, DwAttributes *a)
{
    DwFontDataAttributes(f->data, a);
    DwStrings *family, *face;
    DwModelNames(f->data, DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE, a, &family, &face);
    DwStringsRelease(family);
    DwStringsRelease(face);
    DwAxesToAttributes(f->axes, f->axis_count, &a->weight, &a->stretch, &a->style);
    if (f->simulations & DWRITE_FONT_SIMULATIONS_BOLD)
        a->weight = DWRITE_FONT_WEIGHT_BOLD;
    if ((f->simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) && a->style == DWRITE_FONT_STYLE_NORMAL)
        a->style = DWRITE_FONT_STYLE_OBLIQUE;
}

static DWRITE_FONT_WEIGHT WINAPI face_weight(IDWriteFontFace2 *This)
{
    DwAttributes a;
    face_attributes((DwFace *)This, &a);
    return a.weight;
}

static DWRITE_FONT_STRETCH WINAPI face_stretch(IDWriteFontFace2 *This)
{
    DwAttributes a;
    face_attributes((DwFace *)This, &a);
    return a.stretch;
}

static DWRITE_FONT_STYLE WINAPI face_style(IDWriteFontFace2 *This)
{
    DwAttributes a;
    face_attributes((DwFace *)This, &a);
    return a.style;
}

static HRESULT model_names(DwFace *f, DWRITE_FONT_FAMILY_MODEL model, BOOL want_family, IDWriteLocalizedStrings **names)
{
    if (!names)
        return E_POINTER;
    *names = nullptr;
    if (model != DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC && model != DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE)
        return E_INVALIDARG;
    DwAttributes a;
    DwFontDataAttributes(f->data, &a);
    DwStrings *family, *face;
    DwModelNames(f->data, model, &a, &family, &face);
    DwStrings *kept = want_family ? family : face;
    DwStringsRelease(want_family ? face : family);
    if (!kept)
        kept = DwStringsNew();
    if (!kept)
        return E_OUTOFMEMORY;
    *names = DwStringsInterface(kept);
    return S_OK;
}

static HRESULT WINAPI face_family_names(IDWriteFontFace2 *This, IDWriteLocalizedStrings **names)
{
    return model_names((DwFace *)This, DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE, TRUE, names);
}

static HRESULT WINAPI face_face_names(IDWriteFontFace2 *This, IDWriteLocalizedStrings **names)
{
    return model_names((DwFace *)This, DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE, FALSE, names);
}

static HRESULT WINAPI face_informational(IDWriteFontFace2 *This, DWRITE_INFORMATIONAL_STRING_ID id,
                                         IDWriteLocalizedStrings **strings, BOOL *exists)
{
    if (!strings || !exists)
        return E_POINTER;
    *strings = nullptr;
    *exists = FALSE;
    DwStrings *names = DwInformationalStrings(((DwFace *)This)->data, id);
    if (names) {
        *strings = DwStringsInterface(names);
        *exists = TRUE;
    }
    return S_OK;
}

static BOOL WINAPI face_has_character(IDWriteFontFace2 *This, UINT32 code_point)
{
    return DwFontDataHasCharacter(((DwFace *)This)->data, code_point);
}

static HRESULT WINAPI face_recommended3(IDWriteFontFace2 *This, FLOAT em_size, FLOAT dpi_x, FLOAT dpi_y,
                                        const DWRITE_MATRIX *transform, BOOL sideways, DWRITE_OUTLINE_THRESHOLD threshold,
                                        DWRITE_MEASURING_MODE measuring, IDWriteRenderingParams *params,
                                        DWRITE_RENDERING_MODE1 *mode, DWRITE_GRID_FIT_MODE *grid_fit)
{
    if (!mode || !grid_fit)
        return E_POINTER;
    DWRITE_RENDERING_MODE old;
    HRESULT hr = face_recommended2(This, em_size, dpi_x, dpi_y, transform, sideways, threshold, measuring, params, &old,
                                   grid_fit);
    if (FAILED(hr))
        return hr;
    *mode = (DWRITE_RENDERING_MODE1)old;
    /* a downsampled request of the third parameters stays downsampled where the symmetric mode was chosen. */
    IDWriteRenderingParams3 *three;
    if (params && old == DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC &&
        SUCCEEDED(params->lpVtbl->QueryInterface(params, &IID_IDWriteRenderingParams3, (void **)&three))) {
        if (three->lpVtbl->GetRenderingMode1(three) == DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED)
            *mode = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED;
        three->lpVtbl->Release(three);
    }
    return S_OK;
}

/* every file is local. */
static BOOL WINAPI face_character_local(IDWriteFontFace2 *This, UINT32 code_point) { return TRUE; }
static BOOL WINAPI face_glyph_local(IDWriteFontFace2 *This, UINT16 glyph) { return TRUE; }

static HRESULT WINAPI face_characters_local(IDWriteFontFace2 *This, const WCHAR *characters, UINT32 count, BOOL enqueue,
                                            BOOL *local)
{
    if (!local)
        return E_POINTER;
    *local = TRUE;
    return count && !characters ? E_INVALIDARG : S_OK;
}

static HRESULT WINAPI face_glyphs_local(IDWriteFontFace2 *This, const UINT16 *glyphs, UINT32 count, BOOL enqueue,
                                        BOOL *local)
{
    if (!local)
        return E_POINTER;
    *local = TRUE;
    return count && !glyphs ? E_INVALIDARG : S_OK;
}

/* the outlines are the only image of a glyph that Mime draws. */
static DWRITE_GLYPH_IMAGE_FORMATS outline_format(const DwFace *f)
{
    return f->data->cff ? DWRITE_GLYPH_IMAGE_FORMATS_CFF : DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE;
}

static HRESULT WINAPI face_glyph_formats(IDWriteFontFace2 *This, UINT16 glyph, UINT32 ppem_first, UINT32 ppem_last,
                                         DWRITE_GLYPH_IMAGE_FORMATS *formats)
{
    DwFace *f = (DwFace *)This;
    if (!formats)
        return E_POINTER;
    *formats = DWRITE_GLYPH_IMAGE_FORMATS_NONE;
    if (glyph >= f->data->ttf.num_glyphs)
        return E_INVALIDARG;
    *formats = outline_format(f);
    return S_OK;
}

static DWRITE_GLYPH_IMAGE_FORMATS WINAPI face_formats(IDWriteFontFace2 *This) { return outline_format((DwFace *)This); }

static HRESULT WINAPI face_image_data(IDWriteFontFace2 *This, UINT16 glyph, UINT32 ppem,
                                      DWRITE_GLYPH_IMAGE_FORMATS format, DWRITE_GLYPH_IMAGE_DATA *data, void **context)
{
    if (!data || !context)
        return E_POINTER;
    memset(data, 0, sizeof(*data));
    *context = nullptr;
    /* outlines are read with GetGlyphRunOutline, and there are no bitmaps. */
    return E_INVALIDARG;
}

static void WINAPI face_release_image(IDWriteFontFace2 *This, void *context) {}

/* the value of each fvar axis, the defaults where the face's instance names none. */
static UINT32 face_axis_values(const DwFace *f, DWRITE_FONT_AXIS_VALUE *values)
{
    DwAxis axes[DW_MAX_AXES];
    UINT32 count = DwFontDataAxes(f->data, axes, DW_MAX_AXES);
    for (UINT32 i = 0; i < count; i++) {
        values[i].axisTag = (DWRITE_FONT_AXIS_TAG)axes[i].tag;
        values[i].value = axes[i].def;
        for (UINT32 k = 0; k < f->axis_count; k++) {
            if (f->axes[k].axisTag == values[i].axisTag)
                values[i].value = f->axes[k].value;
        }
    }
    return count;
}

static UINT32 WINAPI face_axis_count(IDWriteFontFace2 *This)
{
    DWRITE_FONT_AXIS_VALUE values[DW_MAX_AXES];
    return face_axis_values((DwFace *)This, values);
}

static HRESULT WINAPI face_axes(IDWriteFontFace2 *This, DWRITE_FONT_AXIS_VALUE *values, UINT32 count)
{
    DWRITE_FONT_AXIS_VALUE all[DW_MAX_AXES];
    UINT32 n = face_axis_values((DwFace *)This, all);
    if (count < n || (n && !values))
        return E_NOT_SUFFICIENT_BUFFER;
    if (n)
        memcpy(values, all, n * sizeof(*values));
    return S_OK;
}

static BOOL WINAPI face_has_variations(IDWriteFontFace2 *This)
{
    DwAxis axes[DW_MAX_AXES];
    UINT32 count = DwFontDataAxes(((DwFace *)This)->data, axes, DW_MAX_AXES);
    for (UINT32 i = 0; i < count; i++) {
        if (axes[i].min < axes[i].max)
            return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI face_resource(IDWriteFontFace2 *This, IDWriteFontResource **resource)
{
    DwFace *f = (DwFace *)This;
    return DwFontResourceCreate(f->factory, f->file, f->index, resource);
}

static BOOL WINAPI face_equals(IDWriteFontFace2 *This, IDWriteFontFace *other)
{
    DwFace *f = (DwFace *)This, *g = DwFaceFrom(other);
    if (!g)
        return FALSE;
    if (f == g)
        return TRUE;
    DWRITE_FONT_AXIS_VALUE a[DW_MAX_AXES], b[DW_MAX_AXES];
    UINT32 na = face_axis_values(f, a), nb = face_axis_values(g, b);
    return f->index == g->index && f->simulations == g->simulations && na == nb && !memcmp(a, b, na * sizeof(*a)) &&
           DwSameFile(f->file, g->file);
}

static HRESULT WINAPI face_family_names6(IDWriteFontFace2 *This, DWRITE_FONT_FAMILY_MODEL model,
                                         IDWriteLocalizedStrings **names)
{
    return model_names((DwFace *)This, model, TRUE, names);
}

static HRESULT WINAPI face_face_names6(IDWriteFontFace2 *This, DWRITE_FONT_FAMILY_MODEL model,
                                       IDWriteLocalizedStrings **names)
{
    return model_names((DwFace *)This, model, FALSE, names);
}

static DWRITE_PAINT_FEATURE_LEVEL WINAPI face_paint_level(IDWriteFontFace2 *This, DWRITE_GLYPH_IMAGE_FORMATS format)
{
    return DWRITE_PAINT_FEATURE_LEVEL_NONE;
}

static HRESULT WINAPI face_paint_reader(IDWriteFontFace2 *This, DWRITE_GLYPH_IMAGE_FORMATS format,
                                        DWRITE_PAINT_FEATURE_LEVEL level, IDWritePaintReader **reader)
{
    if (!reader)
        return E_POINTER;
    *reader = nullptr;
    return DWRITE_E_NOCOLOR;
}

static const IDWriteFontFace7Vtbl g_face_vtbl = {
    (void *)face_query,          (void *)face_addref,         (void *)face_release,         (void *)face_type,
    (void *)face_files,          (void *)face_index,          (void *)face_simulations,     (void *)face_symbol,
    (void *)face_metrics,        (void *)face_glyph_count,    (void *)face_design_metrics,  (void *)face_glyph_indices,
    (void *)face_try_table,      (void *)face_release_table,  (void *)face_outline,         (void *)face_recommended,
    (void *)face_gdi_metrics,    (void *)face_gdi_glyph_metrics,
    (void *)face_metrics1,       (void *)face_gdi_metrics1,   (void *)face_caret,           (void *)face_ranges,
    (void *)face_monospaced,     (void *)face_design_advances, (void *)face_gdi_advances,   (void *)face_kerning,
    (void *)face_has_kerning,    (void *)face_recommended1,   (void *)face_vertical_variants, (void *)face_has_vertical,
    (void *)face_color,          (void *)face_palette_count,  (void *)face_entry_count,     (void *)face_palette,
    (void *)face_recommended2,
    (void *)face_reference,      (void *)face_panose,         (void *)face_weight,          (void *)face_stretch,
    (void *)face_style,          (void *)face_family_names,   (void *)face_face_names,      (void *)face_informational,
    (void *)face_has_character,  (void *)face_recommended3,   (void *)face_character_local, (void *)face_glyph_local,
    (void *)face_characters_local, (void *)face_glyphs_local,
    (void *)face_glyph_formats,  (void *)face_formats,        (void *)face_image_data,      (void *)face_release_image,
    (void *)face_axis_count,     (void *)face_axes,           (void *)face_has_variations,  (void *)face_resource,
    (void *)face_equals,
    (void *)face_family_names6,  (void *)face_face_names6,
    (void *)face_paint_level,    (void *)face_paint_reader,
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
    f->lpVtbl = (const IDWriteFontFace2Vtbl *)&g_face_vtbl;
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
    DwSimulateMetrics(&f->metrics, simulations);
    *out = (IDWriteFontFace *)f;
    return S_OK;
}

HRESULT DwFaceCreateWithAxes(DwFactory *factory, IDWriteFontFile *file, UINT32 index, DWRITE_FONT_SIMULATIONS simulations,
                             const DWRITE_FONT_AXIS_VALUE *axes, UINT32 axis_count, IDWriteFontFace **out)
{
    HRESULT hr = DwFaceCreate(factory, file, DWRITE_FONT_FACE_TYPE_UNKNOWN, index, simulations, out);
    if (FAILED(hr) || !axis_count)
        return hr;
    /* only the axes the face has are kept, clamped to their ranges. */
    DwFace *f = (DwFace *)*out;
    DwAxis known[DW_MAX_AXES];
    UINT32 count = DwFontDataAxes(f->data, known, DW_MAX_AXES);
    for (UINT32 i = 0; i < axis_count; i++) {
        for (UINT32 k = 0; k < count && f->axis_count < DW_MAX_AXES; k++) {
            if ((UINT32)axes[i].axisTag != known[k].tag)
                continue;
            FLOAT v = axes[i].value;
            f->axes[f->axis_count].axisTag = axes[i].axisTag;
            f->axes[f->axis_count].value = v < known[k].min ? known[k].min : v > known[k].max ? known[k].max : v;
            f->axis_count++;
        }
    }
    return S_OK;
}
