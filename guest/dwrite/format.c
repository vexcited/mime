/*
 * IDWriteTextFormat through IDWriteTextFormat3, IDWriteTypography and the ellipsis trimming sign.
 */
#include "text_private.h"

BOOL DwValidWeight(UINT32 w) { return w >= 1 && w <= 999; }
BOOL DwValidStyle(UINT32 s) { return s <= DWRITE_FONT_STYLE_ITALIC; }
BOOL DwValidStretch(UINT32 s)
{
    return s >= DWRITE_FONT_STRETCH_ULTRA_CONDENSED && s <= DWRITE_FONT_STRETCH_ULTRA_EXPANDED;
}

HRESULT DwFmtGetString(const WCHAR *s, WCHAR *out, UINT32 size)
{
    UINT32 len = (UINT32)wcslen(s);
    if (!out)
        return E_INVALIDARG;
    if (size < len + 1)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    memcpy(out, s, (len + 1) * sizeof(WCHAR));
    return S_OK;
}

HRESULT DwFmtInit(DwFmt *f, const WCHAR *family, IDWriteFontCollection *collection, DWRITE_FONT_WEIGHT weight,
                  DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch, FLOAT size, const WCHAR *locale)
{
    memset(f, 0, sizeof(*f));
    f->family = DwStrDup(family);
    f->locale = DwStrDup(locale);
    if (!f->family || !f->locale) {
        DwFmtFree(f);
        return E_OUTOFMEMORY;
    }
    DwLower(f->locale);
    f->collection = collection;
    if (collection)
        collection->lpVtbl->AddRef(collection);
    f->weight = weight;
    f->style = style;
    f->stretch = stretch;
    f->size = size;
    f->tab = 4.0f * size;
    f->last_line_wrap = TRUE;
    return S_OK;
}

HRESULT DwFmtCopy(DwFmt *dst, const DwFmt *src)
{
    *dst = *src;
    dst->family = DwStrDup(src->family);
    dst->locale = DwStrDup(src->locale);
    dst->axis_values = nullptr;
    if (dst->collection)
        dst->collection->lpVtbl->AddRef(dst->collection);
    if (dst->sign)
        dst->sign->lpVtbl->AddRef(dst->sign);
    if (dst->fallback)
        dst->fallback->lpVtbl->AddRef(dst->fallback);
    if (src->axis_count) {
        dst->axis_values = DwAlloc(src->axis_count * sizeof(DWRITE_FONT_AXIS_VALUE));
        if (dst->axis_values)
            memcpy(dst->axis_values, src->axis_values, src->axis_count * sizeof(DWRITE_FONT_AXIS_VALUE));
        else
            dst->axis_count = 0;
    }
    if (!dst->family || !dst->locale) {
        DwFmtFree(dst);
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

void DwFmtFree(DwFmt *f)
{
    DwFree(f->family);
    DwFree(f->locale);
    DwFree(f->axis_values);
    if (f->collection)
        f->collection->lpVtbl->Release(f->collection);
    if (f->sign)
        f->sign->lpVtbl->Release(f->sign);
    if (f->fallback)
        f->fallback->lpVtbl->Release(f->fallback);
    memset(f, 0, sizeof(*f));
}

HRESULT DwFmtSetTextAlignment(DwFmt *f, DWRITE_TEXT_ALIGNMENT v)
{
    if ((unsigned)v > DWRITE_TEXT_ALIGNMENT_JUSTIFIED)
        return E_INVALIDARG;
    f->text_align = v;
    return S_OK;
}

HRESULT DwFmtSetParagraphAlignment(DwFmt *f, DWRITE_PARAGRAPH_ALIGNMENT v)
{
    if ((unsigned)v > DWRITE_PARAGRAPH_ALIGNMENT_CENTER)
        return E_INVALIDARG;
    f->para_align = v;
    return S_OK;
}

HRESULT DwFmtSetWordWrapping(DwFmt *f, DWRITE_WORD_WRAPPING v)
{
    if ((unsigned)v > DWRITE_WORD_WRAPPING_CHARACTER)
        return E_INVALIDARG;
    f->wrap = v;
    return S_OK;
}

HRESULT DwFmtSetReadingDirection(DwFmt *f, DWRITE_READING_DIRECTION v)
{
    if ((unsigned)v > DWRITE_READING_DIRECTION_BOTTOM_TO_TOP)
        return E_INVALIDARG;
    f->reading = v;
    return S_OK;
}

HRESULT DwFmtSetFlowDirection(DwFmt *f, DWRITE_FLOW_DIRECTION v)
{
    if ((unsigned)v > DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT)
        return E_INVALIDARG;
    f->flow = v;
    return S_OK;
}

HRESULT DwFmtSetTabStop(DwFmt *f, FLOAT v)
{
    if (!(v > 0))
        return E_INVALIDARG;
    f->tab = v;
    return S_OK;
}

HRESULT DwFmtSetTrimming(DwFmt *f, const DWRITE_TRIMMING *trimming, IDWriteInlineObject *sign)
{
    if (!trimming || (unsigned)trimming->granularity > DWRITE_TRIMMING_GRANULARITY_WORD)
        return E_INVALIDARG;
    f->trimming = *trimming;
    if (sign)
        sign->lpVtbl->AddRef(sign);
    if (f->sign)
        f->sign->lpVtbl->Release(f->sign);
    f->sign = sign;
    return S_OK;
}

HRESULT DwFmtSetLineSpacing(DwFmt *f, const DWRITE_LINE_SPACING *s)
{
    if (!s || (unsigned)s->method > DWRITE_LINE_SPACING_METHOD_PROPORTIONAL ||
        (unsigned)s->fontLineGapUsage > DWRITE_FONT_LINE_GAP_USAGE_ENABLED)
        return E_INVALIDARG;
    if (s->method != DWRITE_LINE_SPACING_METHOD_DEFAULT && (!(s->height >= 0) || s->leadingBefore != 0))
        return E_INVALIDARG;
    f->spacing = *s;
    return S_OK;
}

HRESULT DwFmtSetFontFallback(DwFmt *f, IDWriteFontFallback *fallback)
{
    if (fallback)
        fallback->lpVtbl->AddRef(fallback);
    if (f->fallback)
        f->fallback->lpVtbl->Release(f->fallback);
    f->fallback = fallback;
    return S_OK;
}

HRESULT DwFmtSetAxisValues(DwFmt *f, const DWRITE_FONT_AXIS_VALUE *values, UINT32 count)
{
    if (count && !values)
        return E_INVALIDARG;
    DWRITE_FONT_AXIS_VALUE *copy = nullptr;
    if (count) {
        copy = DwAlloc(count * sizeof(*copy));
        if (!copy)
            return E_OUTOFMEMORY;
        memcpy(copy, values, count * sizeof(*copy));
    }
    DwFree(f->axis_values);
    f->axis_values = copy;
    f->axis_count = count;
    return S_OK;
}

/* ------------------------------------------------------------------ the format object */

typedef struct DwFormat {
    const IDWriteTextFormat3Vtbl *lpVtbl;
    LONG refs;
    DwFactory *factory;
    DwFmt fmt;
} DwFormat;

static const IDWriteTextFormat3Vtbl g_format_vtbl;

const DwFmt *DwFmtOf(IDWriteTextFormat *format)
{
    if (!format || format->lpVtbl != (const IDWriteTextFormatVtbl *)&g_format_vtbl)
        return nullptr;
    return &((DwFormat *)format)->fmt;
}

#define FMT(This) (&((DwFormat *)(This))->fmt)

static HRESULT WINAPI fm_query(IDWriteTextFormat3 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteTextFormat) ||
        IsEqualGUID(riid, &IID_IDWriteTextFormat1) || IsEqualGUID(riid, &IID_IDWriteTextFormat2) ||
        IsEqualGUID(riid, &IID_IDWriteTextFormat3)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI fm_addref(IDWriteTextFormat3 *This) { return InterlockedIncrement(&((DwFormat *)This)->refs); }

static ULONG WINAPI fm_release(IDWriteTextFormat3 *This)
{
    DwFormat *f = (DwFormat *)This;
    ULONG refs = InterlockedDecrement(&f->refs);
    if (!refs) {
        DwFmtFree(&f->fmt);
        DwFree(f);
    }
    return refs;
}

static HRESULT WINAPI fm_set_text_alignment(IDWriteTextFormat3 *This, DWRITE_TEXT_ALIGNMENT v)
{
    return DwFmtSetTextAlignment(FMT(This), v);
}
static HRESULT WINAPI fm_set_para_alignment(IDWriteTextFormat3 *This, DWRITE_PARAGRAPH_ALIGNMENT v)
{
    return DwFmtSetParagraphAlignment(FMT(This), v);
}
static HRESULT WINAPI fm_set_wrapping(IDWriteTextFormat3 *This, DWRITE_WORD_WRAPPING v)
{
    return DwFmtSetWordWrapping(FMT(This), v);
}
static HRESULT WINAPI fm_set_reading(IDWriteTextFormat3 *This, DWRITE_READING_DIRECTION v)
{
    return DwFmtSetReadingDirection(FMT(This), v);
}
static HRESULT WINAPI fm_set_flow(IDWriteTextFormat3 *This, DWRITE_FLOW_DIRECTION v)
{
    return DwFmtSetFlowDirection(FMT(This), v);
}
static HRESULT WINAPI fm_set_tab(IDWriteTextFormat3 *This, FLOAT v) { return DwFmtSetTabStop(FMT(This), v); }
static HRESULT WINAPI fm_set_trimming(IDWriteTextFormat3 *This, const DWRITE_TRIMMING *t, IDWriteInlineObject *sign)
{
    return DwFmtSetTrimming(FMT(This), t, sign);
}

static HRESULT WINAPI fm_set_spacing(IDWriteTextFormat3 *This, DWRITE_LINE_SPACING_METHOD method, FLOAT height,
                                     FLOAT baseline)
{
    DWRITE_LINE_SPACING s = FMT(This)->spacing;
    s.method = method;
    s.height = height;
    s.baseline = baseline;
    return DwFmtSetLineSpacing(FMT(This), &s);
}

static DWRITE_TEXT_ALIGNMENT WINAPI fm_get_text_alignment(IDWriteTextFormat3 *This) { return FMT(This)->text_align; }
static DWRITE_PARAGRAPH_ALIGNMENT WINAPI fm_get_para_alignment(IDWriteTextFormat3 *This)
{
    return FMT(This)->para_align;
}
static DWRITE_WORD_WRAPPING WINAPI fm_get_wrapping(IDWriteTextFormat3 *This) { return FMT(This)->wrap; }
static DWRITE_READING_DIRECTION WINAPI fm_get_reading(IDWriteTextFormat3 *This) { return FMT(This)->reading; }
static DWRITE_FLOW_DIRECTION WINAPI fm_get_flow(IDWriteTextFormat3 *This) { return FMT(This)->flow; }
static FLOAT WINAPI fm_get_tab(IDWriteTextFormat3 *This) { return FMT(This)->tab; }

static HRESULT WINAPI fm_get_trimming(IDWriteTextFormat3 *This, DWRITE_TRIMMING *t, IDWriteInlineObject **sign)
{
    if (!t || !sign)
        return E_INVALIDARG;
    *t = FMT(This)->trimming;
    *sign = FMT(This)->sign;
    if (*sign)
        (*sign)->lpVtbl->AddRef(*sign);
    return S_OK;
}

static HRESULT WINAPI fm_get_spacing(IDWriteTextFormat3 *This, DWRITE_LINE_SPACING_METHOD *method, FLOAT *height,
                                     FLOAT *baseline)
{
    if (!method || !height || !baseline)
        return E_INVALIDARG;
    *method = FMT(This)->spacing.method;
    *height = FMT(This)->spacing.height;
    *baseline = FMT(This)->spacing.baseline;
    return S_OK;
}

static HRESULT WINAPI fm_get_collection(IDWriteTextFormat3 *This, IDWriteFontCollection **c)
{
    if (!c)
        return E_INVALIDARG;
    *c = FMT(This)->collection;
    if (*c)
        (*c)->lpVtbl->AddRef(*c);
    return S_OK;
}

static UINT32 WINAPI fm_family_length(IDWriteTextFormat3 *This) { return (UINT32)wcslen(FMT(This)->family); }
static HRESULT WINAPI fm_family_name(IDWriteTextFormat3 *This, WCHAR *name, UINT32 size)
{
    return DwFmtGetString(FMT(This)->family, name, size);
}
static DWRITE_FONT_WEIGHT WINAPI fm_weight(IDWriteTextFormat3 *This) { return FMT(This)->weight; }
static DWRITE_FONT_STYLE WINAPI fm_style(IDWriteTextFormat3 *This) { return FMT(This)->style; }
static DWRITE_FONT_STRETCH WINAPI fm_stretch(IDWriteTextFormat3 *This) { return FMT(This)->stretch; }
static FLOAT WINAPI fm_size(IDWriteTextFormat3 *This) { return FMT(This)->size; }
static UINT32 WINAPI fm_locale_length(IDWriteTextFormat3 *This) { return (UINT32)wcslen(FMT(This)->locale); }
static HRESULT WINAPI fm_locale_name(IDWriteTextFormat3 *This, WCHAR *name, UINT32 size)
{
    return DwFmtGetString(FMT(This)->locale, name, size);
}

static HRESULT WINAPI fm_set_vertical(IDWriteTextFormat3 *This, DWRITE_VERTICAL_GLYPH_ORIENTATION v)
{
    if ((unsigned)v > DWRITE_VERTICAL_GLYPH_ORIENTATION_STACKED)
        return E_INVALIDARG;
    FMT(This)->vertical = v;
    return S_OK;
}
static DWRITE_VERTICAL_GLYPH_ORIENTATION WINAPI fm_get_vertical(IDWriteTextFormat3 *This)
{
    return FMT(This)->vertical;
}
static HRESULT WINAPI fm_set_last_line(IDWriteTextFormat3 *This, BOOL v)
{
    FMT(This)->last_line_wrap = v != 0;
    return S_OK;
}
static BOOL WINAPI fm_get_last_line(IDWriteTextFormat3 *This) { return FMT(This)->last_line_wrap; }
static HRESULT WINAPI fm_set_optical(IDWriteTextFormat3 *This, DWRITE_OPTICAL_ALIGNMENT v)
{
    if ((unsigned)v > DWRITE_OPTICAL_ALIGNMENT_NO_SIDE_BEARINGS)
        return E_INVALIDARG;
    FMT(This)->optical = v;
    return S_OK;
}
static DWRITE_OPTICAL_ALIGNMENT WINAPI fm_get_optical(IDWriteTextFormat3 *This) { return FMT(This)->optical; }
static HRESULT WINAPI fm_set_fallback(IDWriteTextFormat3 *This, IDWriteFontFallback *fb)
{
    return DwFmtSetFontFallback(FMT(This), fb);
}
static HRESULT WINAPI fm_get_fallback(IDWriteTextFormat3 *This, IDWriteFontFallback **fb)
{
    if (!fb)
        return E_INVALIDARG;
    *fb = FMT(This)->fallback;
    if (*fb)
        (*fb)->lpVtbl->AddRef(*fb);
    return S_OK;
}

static HRESULT WINAPI fm2_set_spacing(IDWriteTextFormat3 *This, const DWRITE_LINE_SPACING *s)
{
    return DwFmtSetLineSpacing(FMT(This), s);
}
static HRESULT WINAPI fm2_get_spacing(IDWriteTextFormat3 *This, DWRITE_LINE_SPACING *s)
{
    if (!s)
        return E_INVALIDARG;
    *s = FMT(This)->spacing;
    return S_OK;
}

static HRESULT WINAPI fm3_set_axes(IDWriteTextFormat3 *This, const DWRITE_FONT_AXIS_VALUE *v, UINT32 n)
{
    return DwFmtSetAxisValues(FMT(This), v, n);
}
static UINT32 WINAPI fm3_axis_count(IDWriteTextFormat3 *This) { return FMT(This)->axis_count; }
static HRESULT WINAPI fm3_get_axes(IDWriteTextFormat3 *This, DWRITE_FONT_AXIS_VALUE *v, UINT32 n)
{
    if (n < FMT(This)->axis_count || (n && !v))
        return E_INVALIDARG;
    if (FMT(This)->axis_count)
        memcpy(v, FMT(This)->axis_values, FMT(This)->axis_count * sizeof(*v));
    return S_OK;
}
static DWRITE_AUTOMATIC_FONT_AXES WINAPI fm3_get_auto(IDWriteTextFormat3 *This) { return FMT(This)->axes; }
static HRESULT WINAPI fm3_set_auto(IDWriteTextFormat3 *This, DWRITE_AUTOMATIC_FONT_AXES a)
{
    if ((unsigned)a > DWRITE_AUTOMATIC_FONT_AXES_OPTICAL_SIZE)
        return E_INVALIDARG;
    FMT(This)->axes = a;
    return S_OK;
}

static const IDWriteTextFormat3Vtbl g_format_vtbl = {
    fm_query,
    fm_addref,
    fm_release,
    fm_set_text_alignment,
    fm_set_para_alignment,
    fm_set_wrapping,
    fm_set_reading,
    fm_set_flow,
    fm_set_tab,
    fm_set_trimming,
    fm_set_spacing,
    fm_get_text_alignment,
    fm_get_para_alignment,
    fm_get_wrapping,
    fm_get_reading,
    fm_get_flow,
    fm_get_tab,
    fm_get_trimming,
    fm_get_spacing,
    fm_get_collection,
    fm_family_length,
    fm_family_name,
    fm_weight,
    fm_style,
    fm_stretch,
    fm_size,
    fm_locale_length,
    fm_locale_name,
    fm_set_vertical,
    fm_get_vertical,
    fm_set_last_line,
    fm_get_last_line,
    fm_set_optical,
    fm_get_optical,
    fm_set_fallback,
    fm_get_fallback,
    fm2_set_spacing,
    fm2_get_spacing,
    fm3_set_axes,
    fm3_axis_count,
    fm3_get_axes,
    fm3_get_auto,
    fm3_set_auto,
};

HRESULT DwTextFormatCreate(DwFactory *factory, const WCHAR *family, IDWriteFontCollection *collection,
                           DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch, FLOAT size,
                           const WCHAR *locale, IDWriteTextFormat **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!family || !locale || !(size > 0) || !DwValidWeight(weight) || !DwValidStyle(style) || !DwValidStretch(stretch))
        return E_INVALIDARG;
    DwFormat *f = DwAllocZero(sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->lpVtbl = &g_format_vtbl;
    f->refs = 1;
    f->factory = factory;
    IDWriteFontCollection *system = nullptr;
    if (!collection) {
        if (FAILED(DwSystemCollection(factory, &system))) {
            DwFree(f);
            return E_FAIL;
        }
        collection = system;
    }
    HRESULT hr = DwFmtInit(&f->fmt, family, collection, weight, style, stretch, size, locale);
    if (system)
        system->lpVtbl->Release(system);
    if (FAILED(hr)) {
        DwFree(f);
        return hr;
    }
    *out = (IDWriteTextFormat *)f;
    return S_OK;
}

/* ------------------------------------------------------------------ typography */

typedef struct DwTypography {
    const IDWriteTypographyVtbl *lpVtbl;
    LONG refs;
    DWRITE_FONT_FEATURE *features;
    UINT32 count, capacity;
} DwTypography;

static HRESULT WINAPI ty_query(IDWriteTypography *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteTypography)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}
static ULONG WINAPI ty_addref(IDWriteTypography *This) { return InterlockedIncrement(&((DwTypography *)This)->refs); }
static ULONG WINAPI ty_release(IDWriteTypography *This)
{
    DwTypography *t = (DwTypography *)This;
    ULONG refs = InterlockedDecrement(&t->refs);
    if (!refs) {
        DwFree(t->features);
        DwFree(t);
    }
    return refs;
}
static HRESULT WINAPI ty_add(IDWriteTypography *This, DWRITE_FONT_FEATURE feature)
{
    DwTypography *t = (DwTypography *)This;
    if (t->count == t->capacity) {
        UINT32 cap = t->capacity ? t->capacity * 2 : 4;
        DWRITE_FONT_FEATURE *grown = DwAlloc(cap * sizeof(*grown));
        if (!grown)
            return E_OUTOFMEMORY;
        if (t->count)
            memcpy(grown, t->features, t->count * sizeof(*grown));
        DwFree(t->features);
        t->features = grown;
        t->capacity = cap;
    }
    t->features[t->count++] = feature;
    return S_OK;
}
static UINT32 WINAPI ty_count(IDWriteTypography *This) { return ((DwTypography *)This)->count; }
static HRESULT WINAPI ty_get(IDWriteTypography *This, UINT32 index, DWRITE_FONT_FEATURE *feature)
{
    DwTypography *t = (DwTypography *)This;
    if (!feature || index >= t->count)
        return E_INVALIDARG;
    *feature = t->features[index];
    return S_OK;
}
static const IDWriteTypographyVtbl g_typography_vtbl = {ty_query, ty_addref, ty_release, ty_add, ty_count, ty_get};

HRESULT DwTypographyCreate(IDWriteTypography **out)
{
    DwTypography *t = DwAllocZero(sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    t->lpVtbl = &g_typography_vtbl;
    t->refs = 1;
    *out = (IDWriteTypography *)t;
    return S_OK;
}

/* ------------------------------------------------------------------ the ellipsis trimming sign */

typedef struct DwSign {
    const IDWriteInlineObjectVtbl *lpVtbl;
    LONG refs;
    IDWriteFontFace *face;
    FLOAT size;
    UINT16 glyph;
    FLOAT advance;
    WCHAR *locale;
    DWRITE_OVERHANG_METRICS overhang;
} DwSign;

static HRESULT WINAPI sg_query(IDWriteInlineObject *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteInlineObject)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}
static ULONG WINAPI sg_addref(IDWriteInlineObject *This) { return InterlockedIncrement(&((DwSign *)This)->refs); }
static ULONG WINAPI sg_release(IDWriteInlineObject *This)
{
    DwSign *s = (DwSign *)This;
    ULONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        s->face->lpVtbl->Release(s->face);
        DwFree(s->locale);
        DwFree(s);
    }
    return refs;
}

static HRESULT WINAPI sg_draw(IDWriteInlineObject *This, void *ctx, IDWriteTextRenderer *renderer, FLOAT x, FLOAT y,
                              BOOL sideways, BOOL rtl, IUnknown *effect)
{
    DwSign *s = (DwSign *)This;
    if (!renderer)
        return E_INVALIDARG;
    static const WCHAR ellipsis[] = {0x2026, 0};
    UINT16 cluster = 0;
    FLOAT advance = s->advance;
    DWRITE_GLYPH_RUN run = {s->face, s->size, 1, &s->glyph, &advance, nullptr, sideways, rtl ? 1u : 0u};
    DWRITE_GLYPH_RUN_DESCRIPTION desc = {s->locale, ellipsis, 1, &cluster, 0};
    return renderer->lpVtbl->DrawGlyphRun(renderer, ctx, x, y, DWRITE_MEASURING_MODE_NATURAL, &run, &desc, effect);
}

static HRESULT WINAPI sg_metrics(IDWriteInlineObject *This, DWRITE_INLINE_OBJECT_METRICS *m)
{
    if (!m)
        return E_INVALIDARG;
    m->width = ((DwSign *)This)->advance;
    m->height = 0;
    m->baseline = 0;
    m->supportsSideways = FALSE;
    return S_OK;
}

static HRESULT WINAPI sg_overhang(IDWriteInlineObject *This, DWRITE_OVERHANG_METRICS *o)
{
    if (!o)
        return E_INVALIDARG;
    *o = ((DwSign *)This)->overhang;
    return S_OK;
}

static HRESULT WINAPI sg_breaks(IDWriteInlineObject *This, DWRITE_BREAK_CONDITION *before,
                                DWRITE_BREAK_CONDITION *after)
{
    if (!before || !after)
        return E_INVALIDARG;
    *before = *after = DWRITE_BREAK_CONDITION_NEUTRAL;
    return S_OK;
}

static const IDWriteInlineObjectVtbl g_sign_vtbl = {sg_query,   sg_addref,   sg_release, sg_draw,
                                                    sg_metrics, sg_overhang, sg_breaks};

HRESULT DwEllipsisSignCreate(DwFactory *factory, IDWriteTextFormat *format, IDWriteInlineObject **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!format)
        return E_INVALIDARG;
    const DwFmt *f = DwFmtOf(format);
    DwFmt local;
    BOOL copied = FALSE;
    if (!f) {
        /* a format of another implementation, read through its interface. */
        UINT32 flen = format->lpVtbl->GetFontFamilyNameLength(format);
        WCHAR *family = DwAlloc((flen + 1) * sizeof(WCHAR));
        UINT32 llen = format->lpVtbl->GetLocaleNameLength(format);
        WCHAR *locale = DwAlloc((llen + 1) * sizeof(WCHAR));
        if (!family || !locale) {
            DwFree(family);
            DwFree(locale);
            return E_OUTOFMEMORY;
        }
        format->lpVtbl->GetFontFamilyName(format, family, flen + 1);
        format->lpVtbl->GetLocaleName(format, locale, llen + 1);
        IDWriteFontCollection *c = nullptr;
        format->lpVtbl->GetFontCollection(format, &c);
        HRESULT hr =
            DwFmtInit(&local, family, c, format->lpVtbl->GetFontWeight(format), format->lpVtbl->GetFontStyle(format),
                      format->lpVtbl->GetFontStretch(format), format->lpVtbl->GetFontSize(format), locale);
        if (c)
            c->lpVtbl->Release(c);
        DwFree(family);
        DwFree(locale);
        if (FAILED(hr))
            return hr;
        f = &local;
        copied = TRUE;
    }
    IDWriteFontFace *face = nullptr;
    FLOAT scale = 1.0f;
    static const WCHAR ellipsis[] = {0x2026, 0};
    HRESULT hr = DwFontForText(factory, f, ellipsis, 1, &face, &scale);
    if (FAILED(hr) || !face) {
        if (copied)
            DwFmtFree(&local);
        return FAILED(hr) ? hr : E_FAIL;
    }
    DwSign *s = DwAllocZero(sizeof(*s));
    if (!s) {
        face->lpVtbl->Release(face);
        if (copied)
            DwFmtFree(&local);
        return E_OUTOFMEMORY;
    }
    s->lpVtbl = &g_sign_vtbl;
    s->refs = 1;
    s->face = face;
    s->size = f->size * scale;
    s->locale = DwStrDup(f->locale);
    UINT32 cp = 0x2026;
    face->lpVtbl->GetGlyphIndices(face, &cp, 1, &s->glyph);
    DWRITE_FONT_METRICS fm;
    face->lpVtbl->GetMetrics(face, &fm);
    DWRITE_GLYPH_METRICS gm;
    face->lpVtbl->GetDesignGlyphMetrics(face, &s->glyph, 1, &gm, FALSE);
    FLOAT k = s->size / (FLOAT)fm.designUnitsPerEm;
    s->advance = (FLOAT)gm.advanceWidth * k;
    /* the ink of the glyph against a box as wide as the advance, from the baseline up to the baseline. */
    s->overhang.left = -(FLOAT)gm.leftSideBearing * k;
    s->overhang.right = -(FLOAT)gm.rightSideBearing * k;
    {
        INT32 y_max = (INT32)gm.verticalOriginY - (INT32)gm.topSideBearing;
        INT32 y_min = (INT32)gm.bottomSideBearing - (INT32)gm.advanceHeight + (INT32)gm.verticalOriginY;
        s->overhang.top = (FLOAT)y_max * k;
        s->overhang.bottom = -(FLOAT)y_min * k;
    }
    if (copied)
        DwFmtFree(&local);
    *out = (IDWriteInlineObject *)s;
    return S_OK;
}
