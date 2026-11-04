/*
 * the text layout, part three: the interfaces. IDWriteTextLayout through IDWriteTextLayout4 on one object, the
 * metrics, hit testing and drawing.
 */
#include "layout_private.h"

HRESULT DwLayoutRange(DwLayout *l, DWRITE_TEXT_RANGE r, UINT32 *first, UINT32 *last);
void DwLayoutSpansDone(DwLayout *l);
HRESULT DwLayoutSpansInit(DwLayout *l);
void DwLayoutSpansFree(DwLayout *l);

#define L(This) ((DwLayout *)(This))
#define LOCKED_ENSURE(l) DwLayoutEnsure(l)

static FLOAT round_f(FLOAT v) { return v >= 0 ? (FLOAT)(INT64)(v + 0.5f) : (FLOAT)(INT64)(v - 0.5f); }

/* ------------------------------------------------------------------ unknown */

static const IDWriteTextFormat3Vtbl g_layout_format_vtbl;

static HRESULT WINAPI ly_query(IDWriteTextLayout4 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IDWriteTextFormat1) || IsEqualGUID(riid, &IID_IDWriteTextFormat2) ||
        IsEqualGUID(riid, &IID_IDWriteTextFormat3)) {
        *out = &L(This)->format_vtbl;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteTextFormat) ||
        IsEqualGUID(riid, &IID_IDWriteTextLayout) || IsEqualGUID(riid, &IID_IDWriteTextLayout1) ||
        IsEqualGUID(riid, &IID_IDWriteTextLayout2) || IsEqualGUID(riid, &IID_IDWriteTextLayout3) ||
        IsEqualGUID(riid, &IID_IDWriteTextLayout4)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI ly_addref(IDWriteTextLayout4 *This) { return InterlockedIncrement(&L(This)->refs); }

static ULONG WINAPI ly_release(IDWriteTextLayout4 *This)
{
    DwLayout *l = L(This);
    ULONG refs = InterlockedDecrement(&l->refs);
    if (!refs) {
        DwLayoutFreeBuilt(l, TRUE);
        DwLayoutSpansFree(l);
        DwFmtFree(&l->fmt);
        DwFree(l->text);
        DwFree(l);
    }
    return refs;
}

/* ------------------------------------------------------------------ the format methods */

#define SET_FMT(name, call, type)                                                                                      \
    static HRESULT WINAPI name(IDWriteTextLayout4 *This, type v)                                                       \
    {                                                                                                                  \
        HRESULT hr = call(&L(This)->fmt, v);                                                                           \
        if (SUCCEEDED(hr))                                                                                             \
            DwLayoutInvalidate(L(This), TRUE);                                                                         \
        return hr;                                                                                                     \
    }

SET_FMT(ly_set_text_alignment, DwFmtSetTextAlignment, DWRITE_TEXT_ALIGNMENT)
SET_FMT(ly_set_para_alignment, DwFmtSetParagraphAlignment, DWRITE_PARAGRAPH_ALIGNMENT)
SET_FMT(ly_set_wrapping, DwFmtSetWordWrapping, DWRITE_WORD_WRAPPING)
SET_FMT(ly_set_reading, DwFmtSetReadingDirection, DWRITE_READING_DIRECTION)
SET_FMT(ly_set_flow, DwFmtSetFlowDirection, DWRITE_FLOW_DIRECTION)
SET_FMT(ly_set_tab, DwFmtSetTabStop, FLOAT)

static HRESULT WINAPI ly_set_trimming(IDWriteTextLayout4 *This, const DWRITE_TRIMMING *t, IDWriteInlineObject *sign)
{
    HRESULT hr = DwFmtSetTrimming(&L(This)->fmt, t, sign);
    if (SUCCEEDED(hr))
        DwLayoutInvalidate(L(This), FALSE);
    return hr;
}

static HRESULT WINAPI ly_set_spacing(IDWriteTextLayout4 *This, DWRITE_LINE_SPACING_METHOD method, FLOAT height,
                                     FLOAT baseline)
{
    DWRITE_LINE_SPACING s = L(This)->fmt.spacing;
    s.method = method;
    s.height = height;
    s.baseline = baseline;
    HRESULT hr = DwFmtSetLineSpacing(&L(This)->fmt, &s);
    if (SUCCEEDED(hr))
        DwLayoutInvalidate(L(This), FALSE);
    return hr;
}

static DWRITE_TEXT_ALIGNMENT WINAPI ly_get_text_alignment(IDWriteTextLayout4 *This) { return L(This)->fmt.text_align; }
static DWRITE_PARAGRAPH_ALIGNMENT WINAPI ly_get_para_alignment(IDWriteTextLayout4 *This)
{
    return L(This)->fmt.para_align;
}
static DWRITE_WORD_WRAPPING WINAPI ly_get_wrapping(IDWriteTextLayout4 *This) { return L(This)->fmt.wrap; }
static DWRITE_READING_DIRECTION WINAPI ly_get_reading(IDWriteTextLayout4 *This) { return L(This)->fmt.reading; }
static DWRITE_FLOW_DIRECTION WINAPI ly_get_flow(IDWriteTextLayout4 *This) { return L(This)->fmt.flow; }
static FLOAT WINAPI ly_get_tab(IDWriteTextLayout4 *This) { return L(This)->fmt.tab; }

static HRESULT WINAPI ly_get_trimming(IDWriteTextLayout4 *This, DWRITE_TRIMMING *t, IDWriteInlineObject **sign)
{
    if (!t || !sign)
        return E_INVALIDARG;
    *t = L(This)->fmt.trimming;
    *sign = L(This)->fmt.sign;
    if (*sign)
        (*sign)->lpVtbl->AddRef(*sign);
    return S_OK;
}

static HRESULT WINAPI ly_get_spacing(IDWriteTextLayout4 *This, DWRITE_LINE_SPACING_METHOD *method, FLOAT *height,
                                     FLOAT *baseline)
{
    if (!method || !height || !baseline)
        return E_INVALIDARG;
    *method = L(This)->fmt.spacing.method;
    *height = L(This)->fmt.spacing.height;
    *baseline = L(This)->fmt.spacing.baseline;
    return S_OK;
}

static HRESULT WINAPI ly_get_fmt_collection(IDWriteTextLayout4 *This, IDWriteFontCollection **c)
{
    if (!c)
        return E_INVALIDARG;
    *c = L(This)->fmt.collection;
    if (*c)
        (*c)->lpVtbl->AddRef(*c);
    return S_OK;
}
static UINT32 WINAPI ly_fmt_family_length(IDWriteTextLayout4 *This) { return (UINT32)wcslen(L(This)->fmt.family); }
static HRESULT WINAPI ly_fmt_family_name(IDWriteTextLayout4 *This, WCHAR *n, UINT32 size)
{
    return DwFmtGetString(L(This)->fmt.family, n, size);
}
static DWRITE_FONT_WEIGHT WINAPI ly_fmt_weight(IDWriteTextLayout4 *This) { return L(This)->fmt.weight; }
static DWRITE_FONT_STYLE WINAPI ly_fmt_style(IDWriteTextLayout4 *This) { return L(This)->fmt.style; }
static DWRITE_FONT_STRETCH WINAPI ly_fmt_stretch(IDWriteTextLayout4 *This) { return L(This)->fmt.stretch; }
static FLOAT WINAPI ly_fmt_size(IDWriteTextLayout4 *This) { return L(This)->fmt.size; }
static UINT32 WINAPI ly_fmt_locale_length(IDWriteTextLayout4 *This) { return (UINT32)wcslen(L(This)->fmt.locale); }
static HRESULT WINAPI ly_fmt_locale_name(IDWriteTextLayout4 *This, WCHAR *n, UINT32 size)
{
    return DwFmtGetString(L(This)->fmt.locale, n, size);
}

/* ------------------------------------------------------------------ the properties of ranges */

static void assign(DwSpan *s, int prop, const void *value)
{
    switch (prop) {
    case P_FAMILY: {
        WCHAR *copy = DwStrDup(value);
        if (copy) {
            DwFree(s->family);
            s->family = copy;
        }
        break;
    }
    case P_COLLECTION: {
        IDWriteFontCollection *c = (IDWriteFontCollection *)value;
        if (c)
            c->lpVtbl->AddRef(c);
        if (s->collection)
            s->collection->lpVtbl->Release(s->collection);
        s->collection = c;
        break;
    }
    case P_WEIGHT:
        s->weight = *(const DWRITE_FONT_WEIGHT *)value;
        break;
    case P_STYLE:
        s->style = *(const DWRITE_FONT_STYLE *)value;
        break;
    case P_STRETCH:
        s->stretch = *(const DWRITE_FONT_STRETCH *)value;
        break;
    case P_SIZE:
        s->size = *(const FLOAT *)value;
        break;
    case P_UNDERLINE:
        s->underline = *(const BOOL *)value != 0;
        break;
    case P_STRIKE:
        s->strike = *(const BOOL *)value != 0;
        break;
    case P_EFFECT: {
        IUnknown *u = (IUnknown *)value;
        if (u)
            u->lpVtbl->AddRef(u);
        if (s->effect)
            s->effect->lpVtbl->Release(s->effect);
        s->effect = u;
        break;
    }
    case P_INLINE: {
        IDWriteInlineObject *o = (IDWriteInlineObject *)value;
        if (o)
            o->lpVtbl->AddRef(o);
        if (s->inline_object)
            s->inline_object->lpVtbl->Release(s->inline_object);
        s->inline_object = o;
        break;
    }
    case P_TYPOGRAPHY: {
        IDWriteTypography *t = (IDWriteTypography *)value;
        if (t)
            t->lpVtbl->AddRef(t);
        if (s->typography)
            s->typography->lpVtbl->Release(s->typography);
        s->typography = t;
        break;
    }
    case P_LOCALE: {
        WCHAR *copy = DwStrDup(value);
        if (copy) {
            DwLower(copy);
            DwFree(s->locale);
            s->locale = copy;
        }
        break;
    }
    case P_KERNING:
        s->kerning = *(const BOOL *)value != 0;
        break;
    case P_SPACING: {
        const FLOAT *v = value;
        s->lead_spacing = v[0];
        s->trail_spacing = v[1];
        s->min_advance = v[2];
        break;
    }
    }
}

static HRESULT set_prop(DwLayout *l, int prop, DWRITE_TEXT_RANGE range, const void *value)
{
    UINT32 first, last;
    HRESULT hr = DwLayoutRange(l, range, &first, &last);
    if (FAILED(hr))
        return hr;
    for (UINT32 i = first; i < last; i++)
        assign(&l->spans[i], prop, value);
    DwLayoutSpansDone(l);
    return S_OK;
}

static void prop_range(const DwLayout *l, UINT32 pos, int prop, DWRITE_TEXT_RANGE *range)
{
    const DwSpan *sp = DwSpanAt(l, pos);
    if (range) {
        range->startPosition = sp->start;
        range->length = sp->end - sp->start;
    }
}

static HRESULT WINAPI ly_set_max_width(IDWriteTextLayout4 *This, FLOAT v)
{
    if (!(v >= 0))
        return E_INVALIDARG;
    L(This)->max_width = v;
    DwLayoutInvalidate(L(This), FALSE);
    return S_OK;
}

static HRESULT WINAPI ly_set_max_height(IDWriteTextLayout4 *This, FLOAT v)
{
    if (!(v >= 0))
        return E_INVALIDARG;
    L(This)->max_height = v;
    DwLayoutInvalidate(L(This), FALSE);
    return S_OK;
}

static HRESULT WINAPI ly_set_collection(IDWriteTextLayout4 *This, IDWriteFontCollection *c, DWRITE_TEXT_RANGE r)
{
    return set_prop(L(This), P_COLLECTION, r, c);
}
static HRESULT WINAPI ly_set_family(IDWriteTextLayout4 *This, const WCHAR *name, DWRITE_TEXT_RANGE r)
{
    if (!name)
        return E_INVALIDARG;
    return set_prop(L(This), P_FAMILY, r, name);
}
static HRESULT WINAPI ly_set_weight(IDWriteTextLayout4 *This, DWRITE_FONT_WEIGHT v, DWRITE_TEXT_RANGE r)
{
    if (!DwValidWeight(v))
        return E_INVALIDARG;
    return set_prop(L(This), P_WEIGHT, r, &v);
}
static HRESULT WINAPI ly_set_style(IDWriteTextLayout4 *This, DWRITE_FONT_STYLE v, DWRITE_TEXT_RANGE r)
{
    if (!DwValidStyle(v))
        return E_INVALIDARG;
    return set_prop(L(This), P_STYLE, r, &v);
}
static HRESULT WINAPI ly_set_stretch(IDWriteTextLayout4 *This, DWRITE_FONT_STRETCH v, DWRITE_TEXT_RANGE r)
{
    if (!DwValidStretch(v))
        return E_INVALIDARG;
    return set_prop(L(This), P_STRETCH, r, &v);
}
static HRESULT WINAPI ly_set_size(IDWriteTextLayout4 *This, FLOAT v, DWRITE_TEXT_RANGE r)
{
    if (!(v > 0))
        return E_INVALIDARG;
    return set_prop(L(This), P_SIZE, r, &v);
}
static HRESULT WINAPI ly_set_underline(IDWriteTextLayout4 *This, BOOL v, DWRITE_TEXT_RANGE r)
{
    return set_prop(L(This), P_UNDERLINE, r, &v);
}
static HRESULT WINAPI ly_set_strike(IDWriteTextLayout4 *This, BOOL v, DWRITE_TEXT_RANGE r)
{
    return set_prop(L(This), P_STRIKE, r, &v);
}
static HRESULT WINAPI ly_set_effect(IDWriteTextLayout4 *This, IUnknown *e, DWRITE_TEXT_RANGE r)
{
    return set_prop(L(This), P_EFFECT, r, e);
}
static HRESULT WINAPI ly_set_inline(IDWriteTextLayout4 *This, IDWriteInlineObject *o, DWRITE_TEXT_RANGE r)
{
    return set_prop(L(This), P_INLINE, r, o);
}
static HRESULT WINAPI ly_set_typography(IDWriteTextLayout4 *This, IDWriteTypography *t, DWRITE_TEXT_RANGE r)
{
    return set_prop(L(This), P_TYPOGRAPHY, r, t);
}
static HRESULT WINAPI ly_set_locale(IDWriteTextLayout4 *This, const WCHAR *name, DWRITE_TEXT_RANGE r)
{
    if (!name)
        return E_INVALIDARG;
    return set_prop(L(This), P_LOCALE, r, name);
}

static FLOAT WINAPI ly_get_max_width(IDWriteTextLayout4 *This) { return L(This)->max_width; }
static FLOAT WINAPI ly_get_max_height(IDWriteTextLayout4 *This) { return L(This)->max_height; }

static HRESULT WINAPI ly_get_collection(IDWriteTextLayout4 *This, UINT32 pos, IDWriteFontCollection **c,
                                        DWRITE_TEXT_RANGE *r)
{
    if (!c)
        return E_INVALIDARG;
    const DwSpan *sp = DwSpanAt(L(This), pos);
    *c = sp->collection;
    if (*c)
        (*c)->lpVtbl->AddRef(*c);
    prop_range(L(This), pos, P_COLLECTION, r);
    return S_OK;
}
static HRESULT WINAPI ly_get_family_length(IDWriteTextLayout4 *This, UINT32 pos, UINT32 *len, DWRITE_TEXT_RANGE *r)
{
    if (!len)
        return E_INVALIDARG;
    *len = (UINT32)wcslen(DwSpanAt(L(This), pos)->family);
    prop_range(L(This), pos, P_FAMILY, r);
    return S_OK;
}
static HRESULT WINAPI ly_get_family_name(IDWriteTextLayout4 *This, UINT32 pos, WCHAR *name, UINT32 size,
                                         DWRITE_TEXT_RANGE *r)
{
    HRESULT hr = DwFmtGetString(DwSpanAt(L(This), pos)->family, name, size);
    prop_range(L(This), pos, P_FAMILY, r);
    return hr;
}
static HRESULT WINAPI ly_get_weight(IDWriteTextLayout4 *This, UINT32 pos, DWRITE_FONT_WEIGHT *v, DWRITE_TEXT_RANGE *r)
{
    if (!v)
        return E_INVALIDARG;
    *v = DwSpanAt(L(This), pos)->weight;
    prop_range(L(This), pos, P_WEIGHT, r);
    return S_OK;
}
static HRESULT WINAPI ly_get_style(IDWriteTextLayout4 *This, UINT32 pos, DWRITE_FONT_STYLE *v, DWRITE_TEXT_RANGE *r)
{
    if (!v)
        return E_INVALIDARG;
    *v = DwSpanAt(L(This), pos)->style;
    prop_range(L(This), pos, P_STYLE, r);
    return S_OK;
}
static HRESULT WINAPI ly_get_stretch(IDWriteTextLayout4 *This, UINT32 pos, DWRITE_FONT_STRETCH *v, DWRITE_TEXT_RANGE *r)
{
    if (!v)
        return E_INVALIDARG;
    *v = DwSpanAt(L(This), pos)->stretch;
    prop_range(L(This), pos, P_STRETCH, r);
    return S_OK;
}
static HRESULT WINAPI ly_get_size(IDWriteTextLayout4 *This, UINT32 pos, FLOAT *v, DWRITE_TEXT_RANGE *r)
{
    if (!v)
        return E_INVALIDARG;
    *v = DwSpanAt(L(This), pos)->size;
    prop_range(L(This), pos, P_SIZE, r);
    return S_OK;
}
static HRESULT WINAPI ly_get_underline(IDWriteTextLayout4 *This, UINT32 pos, BOOL *v, DWRITE_TEXT_RANGE *r)
{
    if (!v)
        return E_INVALIDARG;
    *v = DwSpanAt(L(This), pos)->underline;
    prop_range(L(This), pos, P_UNDERLINE, r);
    return S_OK;
}
static HRESULT WINAPI ly_get_strike(IDWriteTextLayout4 *This, UINT32 pos, BOOL *v, DWRITE_TEXT_RANGE *r)
{
    if (!v)
        return E_INVALIDARG;
    *v = DwSpanAt(L(This), pos)->strike;
    prop_range(L(This), pos, P_STRIKE, r);
    return S_OK;
}
static HRESULT WINAPI ly_get_effect(IDWriteTextLayout4 *This, UINT32 pos, IUnknown **v, DWRITE_TEXT_RANGE *r)
{
    if (!v)
        return E_INVALIDARG;
    *v = DwSpanAt(L(This), pos)->effect;
    if (*v)
        (*v)->lpVtbl->AddRef(*v);
    prop_range(L(This), pos, P_EFFECT, r);
    return S_OK;
}
static HRESULT WINAPI ly_get_inline(IDWriteTextLayout4 *This, UINT32 pos, IDWriteInlineObject **v, DWRITE_TEXT_RANGE *r)
{
    if (!v)
        return E_INVALIDARG;
    *v = DwSpanAt(L(This), pos)->inline_object;
    if (*v)
        (*v)->lpVtbl->AddRef(*v);
    prop_range(L(This), pos, P_INLINE, r);
    return S_OK;
}
static HRESULT WINAPI ly_get_typography(IDWriteTextLayout4 *This, UINT32 pos, IDWriteTypography **v,
                                        DWRITE_TEXT_RANGE *r)
{
    if (!v)
        return E_INVALIDARG;
    *v = DwSpanAt(L(This), pos)->typography;
    if (*v)
        (*v)->lpVtbl->AddRef(*v);
    prop_range(L(This), pos, P_TYPOGRAPHY, r);
    return S_OK;
}
static HRESULT WINAPI ly_get_locale_length(IDWriteTextLayout4 *This, UINT32 pos, UINT32 *len, DWRITE_TEXT_RANGE *r)
{
    if (!len)
        return E_INVALIDARG;
    *len = (UINT32)wcslen(DwSpanAt(L(This), pos)->locale);
    prop_range(L(This), pos, P_LOCALE, r);
    return S_OK;
}
static HRESULT WINAPI ly_get_locale_name(IDWriteTextLayout4 *This, UINT32 pos, WCHAR *name, UINT32 size,
                                         DWRITE_TEXT_RANGE *r)
{
    HRESULT hr = DwFmtGetString(DwSpanAt(L(This), pos)->locale, name, size);
    prop_range(L(This), pos, P_LOCALE, r);
    return hr;
}

/* ------------------------------------------------------------------ metrics */

static HRESULT WINAPI ly_line_metrics(IDWriteTextLayout4 *This, DWRITE_LINE_METRICS *m, UINT32 max, UINT32 *actual)
{
    DwLayout *l = L(This);
    if (!actual)
        return E_INVALIDARG;
    HRESULT hr = LOCKED_ENSURE(l);
    if (FAILED(hr))
        return hr;
    *actual = l->line_count;
    if (max < l->line_count || (l->line_count && !m))
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    for (UINT32 i = 0; i < l->line_count; i++) {
        const DwLine *ln = &l->lines[i];
        m[i].length = ln->length;
        m[i].trailingWhitespaceLength = ln->tws_length;
        m[i].newlineLength = ln->newline_length;
        m[i].height = ln->height;
        /* the lines of vertical text keep their baseline in the middle. */
        m[i].baseline = l->vertical ? ln->height * 0.5f : ln->baseline;
        m[i].isTrimmed = ln->trimmed;
    }
    return S_OK;
}

static HRESULT WINAPI ly_metrics(IDWriteTextLayout4 *This, DWRITE_TEXT_METRICS *m)
{
    DwLayout *l = L(This);
    if (!m)
        return E_INVALIDARG;
    HRESULT hr = LOCKED_ENSURE(l);
    if (FAILED(hr))
        return hr;
    memcpy(m, &l->metrics, sizeof(*m));
    return S_OK;
}

/* the ink of the glyphs of a cluster against the layout box, turned over inside its line when the glyphs lie sideways. */
static void cluster_ink(const DwLayout *l, const DwLine *ln, const DwItem *it, BOOL flip, FLOAT *x0, FLOAT *y0,
                        FLOAT *x1, FLOAT *y1, BOOL *any)
{
    const DwCluster *c = &l->clusters[it->cluster];
    const DwRun *r = &l->runs[c->run];
    FLOAT base_y = ln->top + ln->baseline;
    if (r->object) {
        DWRITE_OVERHANG_METRICS om = {0, 0, 0, 0};
        r->object->lpVtbl->GetOverhangMetrics(r->object, &om);
        FLOAT top = base_y - r->object_metrics.baseline;
        FLOAT v[4] = {it->x - om.left, top - om.top, it->x + it->width + om.right,
                      top + r->object_metrics.height + om.bottom};
        if (!*any || v[0] < *x0)
            *x0 = v[0];
        if (!*any || v[1] < *y0)
            *y0 = v[1];
        if (!*any || v[2] > *x1)
            *x1 = v[2];
        if (!*any || v[3] > *y1)
            *y1 = v[3];
        *any = TRUE;
        return;
    }
    if (!r->face || c->glyph1 <= c->glyph0)
        return;
    DWRITE_FONT_METRICS fm;
    r->face->lpVtbl->GetMetrics(r->face, &fm);
    FLOAT k = r->em / (FLOAT)fm.designUnitsPerEm;
    BOOL rtl = c->level & 1;
    FLOAT pen = rtl ? it->x + it->width : it->x;
    for (UINT32 g = c->glyph0; g < c->glyph1; g++) {
        DWRITE_GLYPH_METRICS gm;
        UINT16 gid = r->glyphs[g];
        if (FAILED(r->face->lpVtbl->GetDesignGlyphMetrics(r->face, &gid, 1, &gm, FALSE)))
            continue;
        INT32 ink_w = (INT32)gm.advanceWidth - gm.leftSideBearing - gm.rightSideBearing;
        INT32 ink_h = (INT32)gm.advanceHeight - gm.topSideBearing - gm.bottomSideBearing;
        FLOAT adv = r->advances[g];
        FLOAT gx = rtl ? pen - adv : pen;
        gx += r->offsets[g].advanceOffset;
        if (ink_w > 0 && ink_h > 0) {
            FLOAT left = gx + (FLOAT)gm.leftSideBearing * k;
            FLOAT right = left + (FLOAT)ink_w * k;
            FLOAT top =
                base_y - r->offsets[g].ascenderOffset - ((FLOAT)gm.verticalOriginY - (FLOAT)gm.topSideBearing) * k;
            FLOAT bottom = top + (FLOAT)ink_h * k;
            if (flip) {
                FLOAT turned = 2 * ln->top + ln->height - bottom;
                bottom = 2 * ln->top + ln->height - top;
                top = turned;
            }
            if (!*any || left < *x0)
                *x0 = left;
            if (!*any || top < *y0)
                *y0 = top;
            if (!*any || right > *x1)
                *x1 = right;
            if (!*any || bottom > *y1)
                *y1 = bottom;
            *any = TRUE;
        }
        pen += rtl ? -adv : adv;
    }
}

static HRESULT WINAPI overhang(IDWriteTextLayout4 *This, DWRITE_OVERHANG_METRICS *o)
{
    DwLayout *l = L(This);
    if (!o)
        return E_INVALIDARG;
    HRESULT hr = LOCKED_ENSURE(l);
    if (FAILED(hr))
        return hr;
    FLOAT x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    BOOL any = FALSE;
    BOOL flip = l->vertical && l->fmt.flow != DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT;
    for (UINT32 li = 0; li < l->line_count; li++) {
        const DwLine *ln = &l->lines[li];
        if (ln->hidden)
            continue;
        for (UINT32 q = 0; q < ln->item_count; q++) {
            if (ln->items[q].cluster == DW_OPEN_END)
                continue;
            cluster_ink(l, ln, &ln->items[q], flip, &x0, &y0, &x1, &y1, &any);
        }
    }
    if (!any) {
        o->left = o->top = 0;
        o->right = -l->max_width;
        o->bottom = -l->max_height;
        return S_OK;
    }
    o->left = -x0;
    o->top = -y0;
    o->right = x1 - l->max_width;
    o->bottom = y1 - l->max_height;
    return S_OK;
}

static HRESULT WINAPI ly_cluster_metrics(IDWriteTextLayout4 *This, DWRITE_CLUSTER_METRICS *m, UINT32 max,
                                         UINT32 *actual)
{
    DwLayout *l = L(This);
    if (!actual)
        return E_INVALIDARG;
    HRESULT hr = LOCKED_ENSURE(l);
    if (FAILED(hr))
        return hr;
    *actual = l->cluster_count;
    if (max < l->cluster_count || (l->cluster_count && !m))
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    for (UINT32 i = 0; i < l->cluster_count; i++) {
        const DwCluster *c = &l->clusters[i];
        memset(&m[i], 0, sizeof(m[i]));
        m[i].width = c->width;
        m[i].length = (UINT16)c->length;
        m[i].canWrapLineAfter = (c->flags & DWC_WRAP) != 0;
        m[i].isWhitespace = (c->flags & DWC_WHITESPACE) != 0;
        m[i].isNewline = (c->flags & DWC_NEWLINE) != 0;
        m[i].isSoftHyphen = (c->flags & DWC_SOFT_HYPHEN) != 0;
        m[i].isRightToLeft = c->level & 1;
    }
    return S_OK;
}

static HRESULT WINAPI ly_min_width(IDWriteTextLayout4 *This, FLOAT *min_width)
{
    DwLayout *l = L(This);
    if (!min_width)
        return E_INVALIDARG;
    HRESULT hr = LOCKED_ENSURE(l);
    if (FAILED(hr))
        return hr;
    FLOAT best = 0, w = 0;
    for (UINT32 i = 0; i < l->cluster_count; i++) {
        const DwCluster *c = &l->clusters[i];
        if (!(c->flags & DWC_WHITESPACE))
            w += c->width;
        if (c->flags & DWC_WRAP) {
            if (w > best)
                best = w;
            w = 0;
        }
    }
    if (w > best)
        best = w;
    *min_width = best;
    return S_OK;
}

/* ------------------------------------------------------------------ hit testing */

static UINT32 line_of_cluster(const DwLayout *l, UINT32 ci)
{
    for (UINT32 i = 0; i < l->line_count; i++)
        if (ci >= l->lines[i].first_cluster && ci < l->lines[i].first_cluster + l->lines[i].cluster_count)
            return i;
    return l->line_count ? l->line_count - 1 : 0;
}

static UINT32 cluster_at(const DwLayout *l, UINT32 pos)
{
    UINT32 lo = 0, hi = l->cluster_count;
    while (hi - lo > 1) {
        UINT32 mid = (lo + hi) / 2;
        if (l->clusters[mid].start <= pos)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

static const DwItem *item_of(const DwLine *ln, UINT32 cluster)
{
    for (UINT32 i = 0; i < ln->item_count; i++)
        if (ln->items[i].cluster == cluster)
            return &ln->items[i];
    return nullptr;
}

/* the position after the last character: where the caret goes at the end of the text. */
static void end_metrics(const DwLayout *l, DWRITE_HIT_TEST_METRICS *m, FLOAT *x, FLOAT *y)
{
    const DwLine *ln = &l->lines[l->line_count - 1];
    BOOL rtl = DW_READS_RTL(l);
    FLOAT px = rtl ? ln->x - ln->tws_width : ln->x + ln->width + ln->tws_width;
    if (ln->trimmed && !ln->hidden)
        px = rtl ? ln->x : ln->x + ln->width;
    memset(m, 0, sizeof(*m));
    m->textPosition = l->length;
    m->length = 0;
    m->left = px;
    m->top = ln->top;
    m->width = 0;
    m->height = ln->height;
    m->bidiLevel = rtl ? 1 : 0;
    m->isText = TRUE;
    m->isTrimmed = ln->trimmed;
    *x = px;
    *y = ln->top;
}

static void cluster_metrics(const DwLayout *l, const DwLine *ln, const DwItem *it, DWRITE_HIT_TEST_METRICS *m)
{
    const DwCluster *c = &l->clusters[it->cluster];
    memset(m, 0, sizeof(*m));
    m->textPosition = c->start;
    m->length = c->length;
    m->left = it->x;
    m->top = ln->top;
    m->width = it->width;
    m->height = ln->height;
    m->bidiLevel = c->level;
    m->isText = TRUE;
}

static HRESULT WINAPI hit_text_position(IDWriteTextLayout4 *This, UINT32 pos, BOOL trailing, FLOAT *px, FLOAT *py,
                                           DWRITE_HIT_TEST_METRICS *m)
{
    DwLayout *l = L(This);
    if (!px || !py || !m)
        return E_INVALIDARG;
    HRESULT hr = LOCKED_ENSURE(l);
    if (FAILED(hr))
        return hr;
    if (pos >= l->length || !l->cluster_count) {
        end_metrics(l, m, px, py);
        return S_OK;
    }
    UINT32 ci = cluster_at(l, pos);
    const DwLine *ln = &l->lines[line_of_cluster(l, ci)];
    if (ln->trimmed && ci >= ln->trim_first && ci < ln->trim_first + ln->trim_count) {
        memset(m, 0, sizeof(*m));
        const DwCluster *first = &l->clusters[ln->trim_first];
        const DwCluster *last = &l->clusters[ln->first_cluster + ln->cluster_count - 1];
        m->textPosition = first->start;
        m->length = last->start + last->length - first->start;
        m->left = ln->sign_x;
        m->top = ln->top;
        m->width = ln->sign_width;
        m->height = ln->height;
        m->bidiLevel = DW_READS_RTL(l) ? 1 : 0;
        m->isText = TRUE;
        m->isTrimmed = TRUE;
        *px = trailing ? m->left + m->width : m->left;
        *py = ln->top;
        return S_OK;
    }
    const DwItem *it = item_of(ln, ci);
    if (!it) {
        /* trailing whitespace dropped by trimming. */
        end_metrics(l, m, px, py);
        return S_OK;
    }
    cluster_metrics(l, ln, it, m);
    if (l->clusters[ci].flags & DWC_OBJECT)
        m->isText = FALSE;
    BOOL rtl = l->clusters[ci].level & 1;
    *px = (trailing != rtl) ? m->left + m->width : m->left;
    *py = ln->top;
    return S_OK;
}

static HRESULT WINAPI hit_point(IDWriteTextLayout4 *This, FLOAT x, FLOAT y, BOOL *trailing, BOOL *inside,
                                   DWRITE_HIT_TEST_METRICS *m)
{
    DwLayout *l = L(This);
    if (!trailing || !inside || !m)
        return E_INVALIDARG;
    HRESULT hr = LOCKED_ENSURE(l);
    if (FAILED(hr))
        return hr;
    *trailing = FALSE;
    *inside = FALSE;
    memset(m, 0, sizeof(*m));
    if (!l->line_count)
        return S_OK;
    UINT32 li = 0;
    UINT32 visible = 0;
    for (UINT32 i = 0; i < l->line_count; i++)
        if (!l->lines[i].hidden)
            visible = i;
    if (y >= l->lines[0].top) {
        li = visible;
        for (UINT32 i = 0; i <= visible; i++)
            if (y < l->lines[i].top + l->lines[i].height) {
                li = i;
                break;
            }
    }
    const DwLine *ln = &l->lines[li];
    BOOL in_y = y >= ln->top && y < ln->top + ln->height;
    if (!ln->item_count) {
        m->textPosition = l->length;
        m->left = ln->x;
        m->top = ln->top;
        m->height = ln->height;
        m->isText = TRUE;
        return S_OK;
    }
    UINT32 pick = 0;
    BOOL found = FALSE;
    for (UINT32 q = 0; q < ln->item_count; q++) {
        if (x >= ln->items[q].x && x < ln->items[q].x + ln->items[q].width) {
            pick = q;
            found = TRUE;
            break;
        }
    }
    const DwItem *first = &ln->items[0], *last = &ln->items[ln->item_count - 1];
    BOOL in_x = x >= first->x && x <= last->x + last->width;
    if (!found) {
        if (x < first->x) {
            pick = 0;
        } else {
            pick = ln->item_count - 1;
        }
    }
    const DwItem *it = &ln->items[pick];
    BOOL sign = it->cluster == DW_OPEN_END;
    BOOL level_odd = sign ? DW_READS_RTL(l) : (it->level & 1);
    if (found)
        *trailing = level_odd ? x < it->x + it->width / 2 : x >= it->x + it->width / 2;
    else if (x < first->x)
        *trailing = level_odd;
    else
        *trailing = !level_odd;
    *inside = found && in_y && in_x;
    if (sign) {
        const DwCluster *fc = &l->clusters[ln->trim_first];
        const DwCluster *lc = &l->clusters[ln->first_cluster + ln->cluster_count - 1];
        DWRITE_INLINE_OBJECT_METRICS om = {0, 0, 0, 0};
        if (ln->sign)
            ln->sign->lpVtbl->GetMetrics(ln->sign, &om);
        m->textPosition = fc->start;
        m->length = lc->start + lc->length - fc->start;
        m->left = ln->sign_x;
        m->top = ln->top + ln->baseline - om.baseline;
        m->width = ln->sign_width;
        m->height = om.height;
        m->bidiLevel = level_odd ? 1 : 0;
        m->isText = FALSE;
        m->isTrimmed = TRUE;
        return S_OK;
    }
    cluster_metrics(l, ln, it, m);
    const DwRun *r = &l->runs[l->clusters[it->cluster].run];
    if (r->object) {
        m->top = ln->top + ln->baseline - r->object_metrics.baseline;
        m->height = r->object_metrics.height;
        m->isText = FALSE;
    }
    return S_OK;
}

static HRESULT WINAPI hit_range(IDWriteTextLayout4 *This, UINT32 pos, UINT32 len, FLOAT ox, FLOAT oy,
                                   DWRITE_HIT_TEST_METRICS *out, UINT32 max, UINT32 *actual)
{
    DwLayout *l = L(This);
    if (!actual)
        return E_INVALIDARG;
    HRESULT hr = LOCKED_ENSURE(l);
    if (FAILED(hr))
        return hr;
    UINT32 total = 0;
    BOOL overflow = FALSE;
    if (pos >= l->length || !l->cluster_count) {
        DWRITE_HIT_TEST_METRICS m;
        FLOAT x, y;
        end_metrics(l, &m, &x, &y);
        if (total < max && out) {
            out[total] = m;
            out[total].left += ox;
            out[total].top += oy;
        } else {
            overflow = TRUE;
        }
        total = 1;
        *actual = total;
        return overflow ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) : S_OK;
    }
    UINT64 end64 = (UINT64)pos + len;
    UINT32 end = end64 > l->length ? l->length : (UINT32)end64;
    for (UINT32 li = 0; li < l->line_count; li++) {
        const DwLine *ln = &l->lines[li];
        if (ln->hidden)
            continue;
        UINT32 group_level = 0, group_start = 0, group_end = 0;
        BOOL group_object = FALSE;
        FLOAT gl = 0, gr = 0;
        BOOL open = FALSE;
        for (UINT32 q = 0; q <= ln->item_count; q++) {
            BOOL take = FALSE;
            const DwCluster *c = nullptr;
            const DwItem *it = nullptr;
            if (q < ln->item_count) {
                it = &ln->items[q];
                if (it->cluster != DW_OPEN_END) {
                    c = &l->clusters[it->cluster];
                    if (len == 0)
                        take = c->start <= pos && pos < c->start + c->length;
                    else
                        take = c->start < end && c->start + c->length > pos;
                }
            }
            if (take && open && c->level == group_level && !(c->flags & DWC_OBJECT) && !group_object &&
                it->x <= gr + 0.001f && it->x + it->width >= gl - 0.001f) {
                if (c->start < group_start)
                    group_start = c->start;
                if (c->start + c->length > group_end)
                    group_end = c->start + c->length;
                if (it->x < gl)
                    gl = it->x;
                if (it->x + it->width > gr)
                    gr = it->x + it->width;
                continue;
            }
            if (open) {
                DWRITE_HIT_TEST_METRICS m;
                memset(&m, 0, sizeof(m));
                m.textPosition = group_start;
                m.length = len == 0 ? 0 : group_end - group_start;
                m.left = gl + ox;
                m.top = ln->top + oy;
                m.width = len == 0 ? 0 : gr - gl;
                m.height = ln->height;
                m.bidiLevel = group_level;
                m.isText = !group_object;
                if (len == 0) {
                    BOOL rtl = group_level & 1;
                    m.left = (rtl ? gr : gl) + ox;
                    m.textPosition = pos;
                }
                if (out && total < max)
                    out[total] = m;
                else
                    overflow = TRUE;
                total++;
                open = FALSE;
            }
            if (take) {
                open = TRUE;
                group_object = (c->flags & DWC_OBJECT) != 0;
                group_level = c->level;
                group_start = c->start;
                group_end = c->start + c->length;
                gl = it->x;
                gr = it->x + it->width;
            }
        }
    }
    *actual = total;
    return overflow ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) : S_OK;
}

/* ------------------------------------------------------------------ drawing */

typedef struct Rend {
    IDWriteTextRenderer *r;
    IDWriteTextRenderer1 *r1;
    void *ctx;
    BOOL snap;
    FLOAT ppd;
    DWRITE_MEASURING_MODE mm;
    DWRITE_READING_DIRECTION reading;
    DWRITE_FLOW_DIRECTION flow;
    BOOL vertical; /* the geometry is turned a quarter, glyphs lie sideways. */
    BOOL mirror; /* lines stack from the right, so x runs from the right edge of the layout. */
    FLOAT origin_x, extent; /* of the layout, to mirror about. */
} Rend;

static DWRITE_GLYPH_ORIENTATION_ANGLE angle_of(const Rend *r)
{
    if (!r->vertical)
        return DWRITE_GLYPH_ORIENTATION_ANGLE_0_DEGREES;
    return r->reading == DWRITE_READING_DIRECTION_BOTTOM_TO_TOP ? DWRITE_GLYPH_ORIENTATION_ANGLE_270_DEGREES
                                                                 : DWRITE_GLYPH_ORIENTATION_ANGLE_90_DEGREES;
}

static void turn(const Rend *r, FLOAT *x, FLOAT *y)
{
    if (r->vertical) {
        FLOAT t = *x;
        *x = *y;
        *y = t;
        if (r->mirror)
            *x = 2 * r->origin_x + r->extent - *x;
    }
}

static HRESULT rend_glyphs(Rend *r, FLOAT x, FLOAT y, const DWRITE_GLYPH_RUN *run,
                           const DWRITE_GLYPH_RUN_DESCRIPTION *desc, IUnknown *effect)
{
    turn(r, &x, &y);
    if (r->r1)
        return r->r1->lpVtbl->IDWriteTextRenderer1_DrawGlyphRun(r->r1, r->ctx, x, y, angle_of(r), r->mm, run, desc,
                                                                effect);
    DWRITE_GLYPH_RUN sideways = *run;
    if (r->vertical)
        sideways.isSideways = TRUE;
    return r->r->lpVtbl->DrawGlyphRun(r->r, r->ctx, x, y, r->mm, &sideways, desc, effect);
}

static HRESULT rend_underline(Rend *r, FLOAT x, FLOAT y, const DWRITE_UNDERLINE *u, IUnknown *effect)
{
    turn(r, &x, &y);
    if (r->r1)
        return r->r1->lpVtbl->IDWriteTextRenderer1_DrawUnderline(r->r1, r->ctx, x, y, angle_of(r), u, effect);
    return r->r->lpVtbl->DrawUnderline(r->r, r->ctx, x, y, u, effect);
}

static HRESULT rend_strike(Rend *r, FLOAT x, FLOAT y, const DWRITE_STRIKETHROUGH *s, IUnknown *effect)
{
    turn(r, &x, &y);
    if (r->r1)
        return r->r1->lpVtbl->IDWriteTextRenderer1_DrawStrikethrough(r->r1, r->ctx, x, y, angle_of(r), s, effect);
    return r->r->lpVtbl->DrawStrikethrough(r->r, r->ctx, x, y, s, effect);
}

static HRESULT rend_inline(Rend *r, FLOAT x, FLOAT y, IDWriteInlineObject *o, BOOL sideways, BOOL rtl, IUnknown *effect)
{
    turn(r, &x, &y);
    if (r->r1)
        return r->r1->lpVtbl->IDWriteTextRenderer1_DrawInlineObject(r->r1, r->ctx, x, y, angle_of(r), o, sideways, rtl,
                                                                    effect);
    return r->r->lpVtbl->DrawInlineObject(r->r, r->ctx, x, y, o, sideways, rtl, effect);
}

typedef struct Piece {
    FLOAT left, right;
    FLOAT base_y, snap_y;
    BOOL underline, strike;
    IUnknown *effect;
    const DwRun *run;
} Piece;

typedef struct ObjectDraw {
    IDWriteInlineObject *object;
    FLOAT x, y;
    BOOL rtl;
    IUnknown *effect;
} ObjectDraw;

/* clusters that stay with each other in one glyph run. */
static BOOL same_piece(const DwLayout *l, UINT32 a, UINT32 b, UINT32 content_end)
{
    const DwCluster *ca = &l->clusters[a], *cb = &l->clusters[b];
    if (ca->run != cb->run || ca->level != cb->level)
        return FALSE;
    if ((a < content_end) != (b < content_end))
        return FALSE;
    const DwSpan *sa = DwSpanAt(l, ca->start), *sb = DwSpanAt(l, cb->start);
    return sa->effect == sb->effect && sa->underline == sb->underline && sa->strike == sb->strike &&
           !wcscmp(sa->locale, sb->locale);
}

HRESULT DwLayoutDraw(DwLayout *l, void *context, IDWriteTextRenderer *renderer, FLOAT ox, FLOAT oy)
{
    HRESULT hr = LOCKED_ENSURE(l);
    if (FAILED(hr))
        return hr;
    Rend r;
    memset(&r, 0, sizeof(r));
    r.r = renderer;
    r.ctx = context;
    r.mm = l->gdi ? (l->natural ? DWRITE_MEASURING_MODE_GDI_NATURAL : DWRITE_MEASURING_MODE_GDI_CLASSIC)
                  : DWRITE_MEASURING_MODE_NATURAL;
    r.reading = l->fmt.reading;
    r.vertical = l->vertical;
    r.flow = l->fmt.flow;
    r.mirror = l->vertical && l->fmt.flow == DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT;
    r.origin_x = oy;
    r.extent = l->max_width;
    if (FAILED(renderer->lpVtbl->QueryInterface(renderer, &IID_IDWriteTextRenderer1, (void **)&r.r1)))
        r.r1 = nullptr;
    BOOL disabled = FALSE;
    hr = renderer->lpVtbl->IsPixelSnappingDisabled(renderer, context, &disabled);
    if (FAILED(hr)) {
        if (r.r1)
            r.r1->lpVtbl->Release(r.r1);
        return hr;
    }
    r.ppd = 1.0f;
    DWRITE_MATRIX m = {1, 0, 0, 1, 0, 0};
    renderer->lpVtbl->GetCurrentTransform(renderer, context, &m);
    renderer->lpVtbl->GetPixelsPerDip(renderer, context, &r.ppd);
    r.snap = !disabled && r.ppd > 0 && m.m12 == 0 && m.m21 == 0;
    /* what comes after the glyph runs: inline objects, then underlines, then strikethroughs. */
    UINT32 piece_cap = 32, piece_count = 0, object_cap = 8, object_count = 0;
    Piece *pieces = DwAlloc(piece_cap * sizeof(Piece));
    ObjectDraw *objects = DwAlloc(object_cap * sizeof(ObjectDraw));
    if (!pieces || !objects) {
        DwFree(pieces);
        DwFree(objects);
        if (r.r1)
            r.r1->lpVtbl->Release(r.r1);
        return E_OUTOFMEMORY;
    }
    for (UINT32 li = 0; li < l->line_count && SUCCEEDED(hr); li++) {
        const DwLine *ln = &l->lines[li];
        if (ln->hidden)
            continue;
        FLOAT base_y = oy + ln->top + ln->baseline;
        FLOAT snap_y = r.snap ? round_f(base_y * r.ppd) / r.ppd : base_y;
        UINT32 first = ln->first_cluster, end = first + ln->cluster_count;
        UINT32 content_end = end;
        while (content_end > first && (l->clusters[content_end - 1].flags & DWC_WHITESPACE))
            content_end--;
        UINT32 i = first;
        while (i < end && SUCCEEDED(hr)) {
            const DwCluster *c = &l->clusters[i];
            if (ln->trimmed && i >= ln->trim_first && i < ln->trim_first + ln->trim_count) {
                i = ln->trim_first + ln->trim_count;
                continue;
            }
            if (ln->trimmed && i >= content_end)
                break;
            if (c->flags & DWC_OBJECT) {
                const DwItem *it = item_of(ln, i);
                if (it) {
                    if (object_count == object_cap) {
                        ObjectDraw *grown = DwAlloc(object_cap * 2 * sizeof(ObjectDraw));
                        if (!grown) {
                            hr = E_OUTOFMEMORY;
                            break;
                        }
                        memcpy(grown, objects, object_count * sizeof(ObjectDraw));
                        DwFree(objects);
                        objects = grown;
                        object_cap *= 2;
                    }
                    const DwRun *run = &l->runs[c->run];
                    objects[object_count].object = run->object;
                    objects[object_count].x = ox + it->x;
                    objects[object_count].y = snap_y - run->object_metrics.baseline;
                    objects[object_count].rtl = (c->level & 1) != 0;
                    objects[object_count].effect = DwSpanAt(l, c->start)->effect;
                    object_count++;
                }
                i++;
                continue;
            }
            UINT32 j = i + 1;
            while (j < end && !(l->clusters[j].flags & DWC_OBJECT) && same_piece(l, j - 1, j, content_end) &&
                   !(ln->trimmed && j >= ln->trim_first && j < ln->trim_first + ln->trim_count) &&
                   !(ln->trimmed && j >= content_end))
                j++;
            const DwRun *run = &l->runs[c->run];
            const DwCluster *lastc = &l->clusters[j - 1];
            /* the glyphs, in logical order, with the room justification added to spaces. */
            UINT32 g0 = c->glyph0, g1 = lastc->glyph1;
            UINT32 count = g1 > g0 ? g1 - g0 : 0;
            FLOAT *adv = DwAlloc((count + 1) * sizeof(FLOAT));
            DWRITE_GLYPH_OFFSET *off = DwAlloc((count + 1) * sizeof(DWRITE_GLYPH_OFFSET));
            UINT32 text_start = c->start;
            UINT32 text_len = lastc->start + lastc->length - text_start;
            UINT16 *map = DwAllocZero((text_len + 1) * sizeof(UINT16));
            if (!adv || !off || !map) {
                DwFree(adv);
                DwFree(off);
                DwFree(map);
                hr = E_OUTOFMEMORY;
                break;
            }
            for (UINT32 g = 0; g < count; g++) {
                adv[g] = run->advances[g0 + g];
                off[g] = run->offsets[g0 + g];
            }
            for (UINT32 k = i; k < j; k++) {
                const DwCluster *ck = &l->clusters[k];
                if (ck->extra != 0 && ck->glyph1 > ck->glyph0)
                    adv[ck->glyph1 - 1 - g0] += ck->extra;
            }
            if (count)
                for (UINT32 u = 0; u < text_len; u++)
                    map[u] = (UINT16)(run->cluster_map[text_start - run->start + u] - g0);
            /* where the piece sits. */
            FLOAT left = 1e30f, right = -1e30f;
            for (UINT32 k = i; k < j; k++) {
                const DwItem *it = item_of(ln, k);
                if (!it)
                    continue;
                if (it->x < left)
                    left = it->x;
                if (it->x + it->width > right)
                    right = it->x + it->width;
            }
            BOOL rtl = c->level & 1;
            const DwSpan *sp = DwSpanAt(l, c->start);
            DWRITE_GLYPH_RUN gr;
            gr.fontFace = run->face;
            gr.fontEmSize = run->em;
            gr.glyphCount = count;
            gr.glyphIndices = count ? run->glyphs + g0 : nullptr;
            gr.glyphAdvances = adv;
            gr.glyphOffsets = off;
            gr.isSideways = FALSE;
            gr.bidiLevel = c->level;
            DWRITE_GLYPH_RUN_DESCRIPTION desc;
            desc.localeName = sp->locale;
            desc.string = l->text + text_start;
            desc.stringLength = text_len;
            desc.clusterMap = map;
            desc.textPosition = text_start;
            hr = rend_glyphs(&r, ox + (rtl ? right : left), snap_y, &gr, &desc, sp->effect);
            DwFree(adv);
            DwFree(off);
            DwFree(map);
            if (piece_count == piece_cap) {
                Piece *grown = DwAlloc(piece_cap * 2 * sizeof(Piece));
                if (!grown) {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                memcpy(grown, pieces, piece_count * sizeof(Piece));
                DwFree(pieces);
                pieces = grown;
                piece_cap *= 2;
            }
            Piece *p = &pieces[piece_count++];
            p->left = left;
            p->right = right;
            p->base_y = base_y;
            p->snap_y = snap_y;
            p->underline = sp->underline;
            p->strike = sp->strike;
            p->effect = sp->effect;
            p->run = run;
            i = j;
        }
        if (ln->sign && SUCCEEDED(hr)) {
            const DwItem *it = nullptr;
            for (UINT32 q = 0; q < ln->item_count; q++)
                if (ln->items[q].cluster == DW_OPEN_END)
                    it = &ln->items[q];
            if (it) {
                DWRITE_INLINE_OBJECT_METRICS om = {0, 0, 0, 0};
                ln->sign->lpVtbl->GetMetrics(ln->sign, &om);
                if (object_count == object_cap) {
                    ObjectDraw *grown = DwAlloc(object_cap * 2 * sizeof(ObjectDraw));
                    if (grown) {
                        memcpy(grown, objects, object_count * sizeof(ObjectDraw));
                        DwFree(objects);
                        objects = grown;
                        object_cap *= 2;
                    }
                }
                if (object_count < object_cap) {
                    objects[object_count].object = ln->sign;
                    objects[object_count].x = ox + it->x;
                    objects[object_count].y = snap_y - om.baseline;
                    objects[object_count].rtl = DW_READS_RTL(l);
                    objects[object_count].effect = nullptr;
                    object_count++;
                }
            }
        }
    }
    for (UINT32 q = 0; q < object_count && SUCCEEDED(hr); q++)
        hr = rend_inline(&r, objects[q].x, objects[q].y, objects[q].object, FALSE, objects[q].rtl, objects[q].effect);
    for (UINT32 p = 0; p < piece_count && SUCCEEDED(hr); p++) {
        if (!pieces[p].underline)
            continue;
        DWRITE_UNDERLINE u;
        u.width = pieces[p].right - pieces[p].left;
        u.thickness = pieces[p].run->underline_thickness;
        /* the underline sits a whole number of pixels below the baseline, at least one. */
        FLOAT px = round_f(pieces[p].run->underline_offset * r.ppd);
        u.offset = (px < 1.0f ? 1.0f : px) / r.ppd;
        u.runHeight = pieces[p].run->cap_height;
        u.readingDirection = r.reading;
        u.flowDirection = r.flow;
        u.localeName = pieces[p].run->locale;
        u.measuringMode = r.mm;
        hr = rend_underline(&r, ox + pieces[p].left, pieces[p].snap_y, &u, pieces[p].effect);
    }
    for (UINT32 p = 0; p < piece_count && SUCCEEDED(hr); p++) {
        if (!pieces[p].strike)
            continue;
        DWRITE_STRIKETHROUGH s;
        s.width = pieces[p].right - pieces[p].left;
        s.thickness = pieces[p].run->strike_thickness;
        s.offset = pieces[p].run->strike_offset;
        s.readingDirection = r.reading;
        s.flowDirection = r.flow;
        s.localeName = pieces[p].run->locale;
        s.measuringMode = r.mm;
        hr = rend_strike(&r, ox + pieces[p].left, pieces[p].base_y, &s, pieces[p].effect);
    }
    DwFree(pieces);
    DwFree(objects);
    if (r.r1)
        r.r1->lpVtbl->Release(r.r1);
    return hr;
}

/* vertical layouts keep their geometry along the lines, so the answers swap their axes on the way out. */
static void swap_hit(DWRITE_HIT_TEST_METRICS *m)
{
    FLOAT t = m->left;
    m->left = m->top;
    m->top = t;
    t = m->width;
    m->width = m->height;
    m->height = t;
}

/* lines stack from the right edge when the text flows right to left. */
static BOOL mirrored(const DwLayout *l)
{
    return l->vertical && l->fmt.flow == DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT;
}

static void mirror_hit(const DwLayout *l, DWRITE_HIT_TEST_METRICS *m)
{
    if (mirrored(l))
        m->left = l->max_width - m->left - m->width;
}

static HRESULT WINAPI ly_hit_text_position(IDWriteTextLayout4 *This, UINT32 pos, BOOL trailing, FLOAT *px, FLOAT *py,
                                           DWRITE_HIT_TEST_METRICS *m)
{
    HRESULT hr = hit_text_position(This, pos, trailing, px, py, m);
    if (SUCCEEDED(hr) && L(This)->vertical) {
        *py = *px;
        swap_hit(m);
        mirror_hit(L(This), m);
        /* the caret of a vertical line sits on the far side of its box. */
        *px = m->left + m->width;
    }
    return hr;
}

static HRESULT WINAPI ly_hit_point(IDWriteTextLayout4 *This, FLOAT x, FLOAT y, BOOL *trailing, BOOL *inside,
                                   DWRITE_HIT_TEST_METRICS *m)
{
    if (!trailing || !inside || !m)
        return E_INVALIDARG;
    if (FAILED(DwLayoutEnsure(L(This))) || !L(This)->vertical)
        return hit_point(This, x, y, trailing, inside, m);
    if (mirrored(L(This)))
        x = L(This)->max_width - x;
    HRESULT hr = hit_point(This, y, x, trailing, inside, m);
    if (SUCCEEDED(hr)) {
        swap_hit(m);
        mirror_hit(L(This), m);
    }
    return hr;
}

static HRESULT WINAPI ly_hit_range(IDWriteTextLayout4 *This, UINT32 pos, UINT32 len, FLOAT ox, FLOAT oy,
                                   DWRITE_HIT_TEST_METRICS *out, UINT32 max, UINT32 *actual)
{
    if (FAILED(DwLayoutEnsure(L(This))) || !L(This)->vertical)
        return hit_range(This, pos, len, ox, oy, out, max, actual);
    BOOL mirror = mirrored(L(This));
    HRESULT hr = hit_range(This, pos, len, oy, mirror ? 0 : ox, out, max, actual);
    if (out && actual)
        for (UINT32 i = 0; i < *actual && i < max; i++) {
            swap_hit(&out[i]);
            if (mirror) {
                mirror_hit(L(This), &out[i]);
                out[i].left += ox;
            }
        }
    return hr;
}

static HRESULT WINAPI ly_overhang(IDWriteTextLayout4 *This, DWRITE_OVERHANG_METRICS *o)
{
    DwLayout *l = L(This);
    if (!o)
        return E_INVALIDARG;
    if (FAILED(DwLayoutEnsure(l)) || !l->vertical)
        return overhang(This, o);
    FLOAT w = l->max_width, h = l->max_height;
    l->max_width = h;
    l->max_height = w;
    HRESULT hr = overhang(This, o);
    l->max_width = w;
    l->max_height = h;
    FLOAT t = o->left;
    o->left = o->top;
    o->top = t;
    t = o->right;
    o->right = o->bottom;
    o->bottom = t;
    if (mirrored(l)) {
        t = o->left;
        o->left = o->right;
        o->right = t;
    }
    return hr;
}

static HRESULT WINAPI ly_draw(IDWriteTextLayout4 *This, void *context, IDWriteTextRenderer *renderer, FLOAT x, FLOAT y)
{
    if (!renderer)
        return E_INVALIDARG;
    DwLayout *l = L(This);
    if (SUCCEEDED(DwLayoutEnsure(l)) && l->vertical)
        return DwLayoutDraw(l, context, renderer, y, x);
    return DwLayoutDraw(l, context, renderer, x, y);
}

/* ------------------------------------------------------------------ layout 1 to 4 */

static HRESULT WINAPI ly1_set_pair_kerning(IDWriteTextLayout4 *This, BOOL v, DWRITE_TEXT_RANGE r)
{
    return set_prop(L(This), P_KERNING, r, &v);
}
static HRESULT WINAPI ly1_get_pair_kerning(IDWriteTextLayout4 *This, UINT32 pos, BOOL *v, DWRITE_TEXT_RANGE *r)
{
    if (!v)
        return E_INVALIDARG;
    *v = DwSpanAt(L(This), pos)->kerning;
    prop_range(L(This), pos, P_KERNING, r);
    return S_OK;
}
static HRESULT WINAPI ly1_set_spacing(IDWriteTextLayout4 *This, FLOAT lead, FLOAT trail, FLOAT min_advance,
                                      DWRITE_TEXT_RANGE r)
{
    if (!(min_advance >= 0))
        return E_INVALIDARG;
    FLOAT v[3] = {lead, trail, min_advance};
    return set_prop(L(This), P_SPACING, r, v);
}
static HRESULT WINAPI ly1_get_spacing(IDWriteTextLayout4 *This, UINT32 pos, FLOAT *lead, FLOAT *trail,
                                      FLOAT *min_advance, DWRITE_TEXT_RANGE *r)
{
    if (!lead || !trail || !min_advance)
        return E_INVALIDARG;
    const DwSpan *sp = DwSpanAt(L(This), pos);
    *lead = sp->lead_spacing;
    *trail = sp->trail_spacing;
    *min_advance = sp->min_advance;
    prop_range(L(This), pos, P_SPACING, r);
    return S_OK;
}

static HRESULT WINAPI ly2_metrics(IDWriteTextLayout4 *This, DWRITE_TEXT_METRICS1 *m)
{
    DwLayout *l = L(This);
    if (!m)
        return E_INVALIDARG;
    HRESULT hr = LOCKED_ENSURE(l);
    if (FAILED(hr))
        return hr;
    *m = l->metrics;
    return S_OK;
}
static HRESULT WINAPI ly2_set_vertical(IDWriteTextLayout4 *This, DWRITE_VERTICAL_GLYPH_ORIENTATION v)
{
    if ((unsigned)v > DWRITE_VERTICAL_GLYPH_ORIENTATION_STACKED)
        return E_INVALIDARG;
    L(This)->fmt.vertical = v;
    DwLayoutInvalidate(L(This), TRUE);
    return S_OK;
}
static DWRITE_VERTICAL_GLYPH_ORIENTATION WINAPI ly2_get_vertical(IDWriteTextLayout4 *This)
{
    return L(This)->fmt.vertical;
}
static HRESULT WINAPI ly2_set_last_line(IDWriteTextLayout4 *This, BOOL v)
{
    L(This)->fmt.last_line_wrap = v != 0;
    DwLayoutInvalidate(L(This), FALSE);
    return S_OK;
}
static BOOL WINAPI ly2_get_last_line(IDWriteTextLayout4 *This) { return L(This)->fmt.last_line_wrap; }
static HRESULT WINAPI ly2_set_optical(IDWriteTextLayout4 *This, DWRITE_OPTICAL_ALIGNMENT v)
{
    if ((unsigned)v > DWRITE_OPTICAL_ALIGNMENT_NO_SIDE_BEARINGS)
        return E_INVALIDARG;
    L(This)->fmt.optical = v;
    DwLayoutInvalidate(L(This), FALSE);
    return S_OK;
}
static DWRITE_OPTICAL_ALIGNMENT WINAPI ly2_get_optical(IDWriteTextLayout4 *This) { return L(This)->fmt.optical; }
static HRESULT WINAPI ly2_set_fallback(IDWriteTextLayout4 *This, IDWriteFontFallback *fb)
{
    HRESULT hr = DwFmtSetFontFallback(&L(This)->fmt, fb);
    DwLayoutInvalidate(L(This), TRUE);
    return hr;
}
static HRESULT WINAPI ly2_get_fallback(IDWriteTextLayout4 *This, IDWriteFontFallback **fb)
{
    if (!fb)
        return E_INVALIDARG;
    *fb = L(This)->fmt.fallback;
    if (*fb)
        (*fb)->lpVtbl->AddRef(*fb);
    return S_OK;
}

static HRESULT WINAPI ly3_invalidate(IDWriteTextLayout4 *This)
{
    DwLayoutInvalidate(L(This), TRUE);
    return S_OK;
}
static HRESULT WINAPI ly3_set_spacing(IDWriteTextLayout4 *This, const DWRITE_LINE_SPACING *s)
{
    HRESULT hr = DwFmtSetLineSpacing(&L(This)->fmt, s);
    if (SUCCEEDED(hr))
        DwLayoutInvalidate(L(This), FALSE);
    return hr;
}
static HRESULT WINAPI ly3_get_spacing(IDWriteTextLayout4 *This, DWRITE_LINE_SPACING *s)
{
    if (!s)
        return E_INVALIDARG;
    *s = L(This)->fmt.spacing;
    return S_OK;
}
static HRESULT WINAPI ly3_line_metrics(IDWriteTextLayout4 *This, DWRITE_LINE_METRICS1 *m, UINT32 max, UINT32 *actual)
{
    DwLayout *l = L(This);
    if (!actual)
        return E_INVALIDARG;
    HRESULT hr = LOCKED_ENSURE(l);
    if (FAILED(hr))
        return hr;
    *actual = l->line_count;
    if (max < l->line_count || (l->line_count && !m))
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    for (UINT32 i = 0; i < l->line_count; i++) {
        const DwLine *ln = &l->lines[i];
        m[i].length = ln->length;
        m[i].trailingWhitespaceLength = ln->tws_length;
        m[i].newlineLength = ln->newline_length;
        m[i].height = ln->height;
        /* the lines of vertical text keep their baseline in the middle. */
        m[i].baseline = l->vertical ? ln->height * 0.5f : ln->baseline;
        m[i].isTrimmed = ln->trimmed;
        m[i].leadingBefore = ln->leading_before;
        m[i].leadingAfter = ln->leading_after;
    }
    return S_OK;
}

static HRESULT WINAPI ly4_set_axes(IDWriteTextLayout4 *This, const DWRITE_FONT_AXIS_VALUE *v, UINT32 n,
                                   DWRITE_TEXT_RANGE r)
{
    if (n && !v)
        return E_INVALIDARG;
    DwLayout *l = L(This);
    UINT32 first, last;
    HRESULT hr = DwLayoutRange(l, r, &first, &last);
    if (FAILED(hr))
        return hr;
    for (UINT32 i = first; i < last; i++) {
        DWRITE_FONT_AXIS_VALUE *copy = n ? DwAlloc(n * sizeof(*copy)) : nullptr;
        if (n && !copy)
            return E_OUTOFMEMORY;
        if (n)
            memcpy(copy, v, n * sizeof(*copy));
        DwFree(l->spans[i].axes);
        l->spans[i].axes = copy;
        l->spans[i].axis_count = n;
    }
    DwLayoutSpansDone(l);
    return S_OK;
}
static UINT32 WINAPI ly4_axis_count(IDWriteTextLayout4 *This, UINT32 pos) { return DwSpanAt(L(This), pos)->axis_count; }
static HRESULT WINAPI ly4_get_axes(IDWriteTextLayout4 *This, UINT32 pos, DWRITE_FONT_AXIS_VALUE *v, UINT32 n,
                                   DWRITE_TEXT_RANGE *r)
{
    const DwSpan *sp = DwSpanAt(L(This), pos);
    if (n < sp->axis_count || (n && !v))
        return E_INVALIDARG;
    if (sp->axis_count)
        memcpy(v, sp->axes, sp->axis_count * sizeof(*v));
    prop_range(L(This), pos, P_AXES, r);
    return S_OK;
}
static DWRITE_AUTOMATIC_FONT_AXES WINAPI ly4_get_auto(IDWriteTextLayout4 *This) { return L(This)->fmt.axes; }
static HRESULT WINAPI ly4_set_auto(IDWriteTextLayout4 *This, DWRITE_AUTOMATIC_FONT_AXES a)
{
    if ((unsigned)a > DWRITE_AUTOMATIC_FONT_AXES_OPTICAL_SIZE)
        return E_INVALIDARG;
    L(This)->fmt.axes = a;
    return S_OK;
}

static const IDWriteTextLayout4Vtbl g_layout_vtbl = {
    ly_query,
    ly_addref,
    ly_release,
    /* the text format methods. */
    ly_set_text_alignment,
    ly_set_para_alignment,
    ly_set_wrapping,
    ly_set_reading,
    ly_set_flow,
    ly_set_tab,
    ly_set_trimming,
    ly_set_spacing,
    ly_get_text_alignment,
    ly_get_para_alignment,
    ly_get_wrapping,
    ly_get_reading,
    ly_get_flow,
    ly_get_tab,
    ly_get_trimming,
    ly_get_spacing,
    ly_get_fmt_collection,
    ly_fmt_family_length,
    ly_fmt_family_name,
    ly_fmt_weight,
    ly_fmt_style,
    ly_fmt_stretch,
    ly_fmt_size,
    ly_fmt_locale_length,
    ly_fmt_locale_name,
    /* the layout. */
    ly_set_max_width,
    ly_set_max_height,
    ly_set_collection,
    ly_set_family,
    ly_set_weight,
    ly_set_style,
    ly_set_stretch,
    ly_set_size,
    ly_set_underline,
    ly_set_strike,
    ly_set_effect,
    ly_set_inline,
    ly_set_typography,
    ly_set_locale,
    ly_get_max_width,
    ly_get_max_height,
    ly_get_collection,
    ly_get_family_length,
    ly_get_family_name,
    ly_get_weight,
    ly_get_style,
    ly_get_stretch,
    ly_get_size,
    ly_get_underline,
    ly_get_strike,
    ly_get_effect,
    ly_get_inline,
    ly_get_typography,
    ly_get_locale_length,
    ly_get_locale_name,
    ly_draw,
    ly_line_metrics,
    ly_metrics,
    ly_overhang,
    ly_cluster_metrics,
    ly_min_width,
    ly_hit_point,
    ly_hit_text_position,
    ly_hit_range,
    /* layout 1. */
    ly1_set_pair_kerning,
    ly1_get_pair_kerning,
    ly1_set_spacing,
    ly1_get_spacing,
    /* layout 2. */
    ly2_metrics,
    ly2_set_vertical,
    ly2_get_vertical,
    ly2_set_last_line,
    ly2_get_last_line,
    ly2_set_optical,
    ly2_get_optical,
    ly2_set_fallback,
    ly2_get_fallback,
    /* layout 3. */
    ly3_invalidate,
    ly3_set_spacing,
    ly3_get_spacing,
    ly3_line_metrics,
    /* layout 4. */
    ly4_set_axes,
    ly4_axis_count,
    ly4_get_axes,
    ly4_get_auto,
    ly4_set_auto,
};

HRESULT DwTextLayoutCreate(DwFactory *factory, const WCHAR *text, UINT32 length, IDWriteTextFormat *format,
                           FLOAT max_width, FLOAT max_height, FLOAT pixels_per_dip, const DWRITE_MATRIX *transform,
                           BOOL natural, BOOL gdi, IDWriteTextLayout **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!text || !format || !(max_width >= 0) || !(max_height >= 0))
        return E_INVALIDARG;
    if (gdi && pixels_per_dip < 0)
        return E_INVALIDARG;
    DwLayout *l = DwAllocZero(sizeof(*l));
    if (!l)
        return E_OUTOFMEMORY;
    l->lpVtbl = &g_layout_vtbl;
    l->format_vtbl = &g_layout_format_vtbl;
    l->refs = 1;
    l->factory = factory;
    l->max_width = max_width;
    l->max_height = max_height;
    l->gdi = gdi;
    l->natural = natural;
    l->ppd = gdi && pixels_per_dip > 0 ? pixels_per_dip : 1.0f;
    if (transform)
        l->transform = *transform;
    else
        l->transform = (DWRITE_MATRIX){1, 0, 0, 1, 0, 0};
    InitializeSRWLock(&l->lock);
    const DwFmt *fmt = DwFmtOf(format);
    HRESULT hr;
    if (fmt) {
        hr = DwFmtCopy(&l->fmt, fmt);
    } else {
        /* a format of another implementation, copied through its interface. */
        UINT32 flen = format->lpVtbl->GetFontFamilyNameLength(format);
        UINT32 llen = format->lpVtbl->GetLocaleNameLength(format);
        WCHAR *family = DwAlloc((flen + 1) * sizeof(WCHAR)), *locale = DwAlloc((llen + 1) * sizeof(WCHAR));
        hr = family && locale ? S_OK : E_OUTOFMEMORY;
        if (SUCCEEDED(hr)) {
            format->lpVtbl->GetFontFamilyName(format, family, flen + 1);
            format->lpVtbl->GetLocaleName(format, locale, llen + 1);
            IDWriteFontCollection *c = nullptr;
            format->lpVtbl->GetFontCollection(format, &c);
            hr = DwFmtInit(&l->fmt, family, c, format->lpVtbl->GetFontWeight(format),
                           format->lpVtbl->GetFontStyle(format), format->lpVtbl->GetFontStretch(format),
                           format->lpVtbl->GetFontSize(format), locale);
            if (c)
                c->lpVtbl->Release(c);
            if (SUCCEEDED(hr)) {
                l->fmt.text_align = format->lpVtbl->GetTextAlignment(format);
                l->fmt.para_align = format->lpVtbl->GetParagraphAlignment(format);
                l->fmt.wrap = format->lpVtbl->GetWordWrapping(format);
                l->fmt.reading = format->lpVtbl->GetReadingDirection(format);
                l->fmt.flow = format->lpVtbl->GetFlowDirection(format);
                l->fmt.tab = format->lpVtbl->GetIncrementalTabStop(format);
                IDWriteInlineObject *sign = nullptr;
                format->lpVtbl->GetTrimming(format, &l->fmt.trimming, &sign);
                l->fmt.sign = sign;
                DWRITE_LINE_SPACING_METHOD method;
                FLOAT h, b;
                if (SUCCEEDED(format->lpVtbl->GetLineSpacing(format, &method, &h, &b))) {
                    l->fmt.spacing.method = method;
                    l->fmt.spacing.height = h;
                    l->fmt.spacing.baseline = b;
                }
            }
        }
        DwFree(family);
        DwFree(locale);
    }
    if (FAILED(hr)) {
        DwFree(l);
        return hr;
    }
    l->text = DwAlloc(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (!l->text) {
        DwFmtFree(&l->fmt);
        DwFree(l);
        return E_OUTOFMEMORY;
    }
    memcpy(l->text, text, length * sizeof(WCHAR));
    l->text[length] = 0;
    l->length = length;
    hr = DwLayoutSpansInit(l);
    if (FAILED(hr)) {
        DwFree(l->text);
        DwFmtFree(&l->fmt);
        DwFree(l);
        return hr;
    }
    *out = (IDWriteTextLayout *)l;
    return S_OK;
}

/* ------------------------------------------------------------------ the layout as a newer text format */

/* the layout behind its format table, as the interface of its own table. */
static IDWriteTextLayout4 *layout_of(IDWriteTextFormat3 *format)
{
    return (IDWriteTextLayout4 *)((char *)format - offsetof(DwLayout, format_vtbl));
}

#define LF(This) layout_of(This)
#define FORWARD0(name, type, method)                                                                                   \
    static type WINAPI name(IDWriteTextFormat3 *This) { return LF(This)->lpVtbl->method(LF(This)); }
#define FORWARD1(name, type, method, a)                                                                                \
    static type WINAPI name(IDWriteTextFormat3 *This, a v) { return LF(This)->lpVtbl->method(LF(This), v); }
#define FORWARD2(name, type, method, a, b)                                                                             \
    static type WINAPI name(IDWriteTextFormat3 *This, a v, b w) { return LF(This)->lpVtbl->method(LF(This), v, w); }
#define FORWARD3(name, type, method, a, b, c)                                                                          \
    static type WINAPI name(IDWriteTextFormat3 *This, a v, b w, c x)                                                   \
    {                                                                                                                  \
        return LF(This)->lpVtbl->method(LF(This), v, w, x);                                                            \
    }

FORWARD2(lf_query, HRESULT, QueryInterface, REFIID, void **)
FORWARD0(lf_addref, ULONG, AddRef)
FORWARD0(lf_release, ULONG, Release)
FORWARD1(lf_set_text_alignment, HRESULT, SetTextAlignment, DWRITE_TEXT_ALIGNMENT)
FORWARD1(lf_set_para_alignment, HRESULT, SetParagraphAlignment, DWRITE_PARAGRAPH_ALIGNMENT)
FORWARD1(lf_set_wrapping, HRESULT, SetWordWrapping, DWRITE_WORD_WRAPPING)
FORWARD1(lf_set_reading, HRESULT, SetReadingDirection, DWRITE_READING_DIRECTION)
FORWARD1(lf_set_flow, HRESULT, SetFlowDirection, DWRITE_FLOW_DIRECTION)
FORWARD1(lf_set_tab, HRESULT, SetIncrementalTabStop, FLOAT)
FORWARD2(lf_set_trimming, HRESULT, SetTrimming, const DWRITE_TRIMMING *, IDWriteInlineObject *)
FORWARD3(lf_set_spacing, HRESULT, SetLineSpacing, DWRITE_LINE_SPACING_METHOD, FLOAT, FLOAT)
FORWARD0(lf_get_text_alignment, DWRITE_TEXT_ALIGNMENT, GetTextAlignment)
FORWARD0(lf_get_para_alignment, DWRITE_PARAGRAPH_ALIGNMENT, GetParagraphAlignment)
FORWARD0(lf_get_wrapping, DWRITE_WORD_WRAPPING, GetWordWrapping)
FORWARD0(lf_get_reading, DWRITE_READING_DIRECTION, GetReadingDirection)
FORWARD0(lf_get_flow, DWRITE_FLOW_DIRECTION, GetFlowDirection)
FORWARD0(lf_get_tab, FLOAT, GetIncrementalTabStop)
FORWARD2(lf_get_trimming, HRESULT, GetTrimming, DWRITE_TRIMMING *, IDWriteInlineObject **)
FORWARD3(lf_get_spacing, HRESULT, GetLineSpacing, DWRITE_LINE_SPACING_METHOD *, FLOAT *, FLOAT *)
FORWARD1(lf_get_collection, HRESULT, GetFontCollection, IDWriteFontCollection **)
FORWARD0(lf_family_length, UINT32, GetFontFamilyNameLength)
FORWARD2(lf_family_name, HRESULT, GetFontFamilyName, WCHAR *, UINT32)
FORWARD0(lf_weight, DWRITE_FONT_WEIGHT, GetFontWeight)
FORWARD0(lf_style, DWRITE_FONT_STYLE, GetFontStyle)
FORWARD0(lf_stretch, DWRITE_FONT_STRETCH, GetFontStretch)
FORWARD0(lf_size, FLOAT, GetFontSize)
FORWARD0(lf_locale_length, UINT32, GetLocaleNameLength)
FORWARD2(lf_locale_name, HRESULT, GetLocaleName, WCHAR *, UINT32)
FORWARD1(lf_set_vertical, HRESULT, SetVerticalGlyphOrientation, DWRITE_VERTICAL_GLYPH_ORIENTATION)
FORWARD0(lf_get_vertical, DWRITE_VERTICAL_GLYPH_ORIENTATION, GetVerticalGlyphOrientation)
FORWARD1(lf_set_last_line, HRESULT, SetLastLineWrapping, BOOL)
FORWARD0(lf_get_last_line, BOOL, GetLastLineWrapping)
FORWARD1(lf_set_optical, HRESULT, SetOpticalAlignment, DWRITE_OPTICAL_ALIGNMENT)
FORWARD0(lf_get_optical, DWRITE_OPTICAL_ALIGNMENT, GetOpticalAlignment)
FORWARD1(lf_set_fallback, HRESULT, SetFontFallback, IDWriteFontFallback *)
FORWARD1(lf_get_fallback, HRESULT, GetFontFallback, IDWriteFontFallback **)
FORWARD1(lf_set_spacing2, HRESULT, IDWriteTextLayout3_SetLineSpacing, const DWRITE_LINE_SPACING *)
FORWARD1(lf_get_spacing2, HRESULT, IDWriteTextLayout3_GetLineSpacing, DWRITE_LINE_SPACING *)
FORWARD0(lf_get_auto, DWRITE_AUTOMATIC_FONT_AXES, GetAutomaticFontAxes)
FORWARD1(lf_set_auto, HRESULT, SetAutomaticFontAxes, DWRITE_AUTOMATIC_FONT_AXES)

/* the axis values of a format are those of the whole text. */
static HRESULT WINAPI lf_set_axes(IDWriteTextFormat3 *This, const DWRITE_FONT_AXIS_VALUE *values, UINT32 count)
{
    DWRITE_TEXT_RANGE all = {0, ~0u};
    return LF(This)->lpVtbl->SetFontAxisValues(LF(This), values, count, all);
}

static UINT32 WINAPI lf_axis_count(IDWriteTextFormat3 *This)
{
    return LF(This)->lpVtbl->GetFontAxisValueCount(LF(This), 0);
}

static HRESULT WINAPI lf_get_axes(IDWriteTextFormat3 *This, DWRITE_FONT_AXIS_VALUE *values, UINT32 count)
{
    return LF(This)->lpVtbl->GetFontAxisValues(LF(This), 0, values, count, nullptr);
}

static const IDWriteTextFormat3Vtbl g_layout_format_vtbl = {
    lf_query,          lf_addref,         lf_release,          lf_set_text_alignment, lf_set_para_alignment,
    lf_set_wrapping,   lf_set_reading,    lf_set_flow,         lf_set_tab,            lf_set_trimming,
    lf_set_spacing,    lf_get_text_alignment, lf_get_para_alignment, lf_get_wrapping, lf_get_reading,
    lf_get_flow,       lf_get_tab,        lf_get_trimming,     lf_get_spacing,        lf_get_collection,
    lf_family_length,  lf_family_name,    lf_weight,           lf_style,              lf_stretch,
    lf_size,           lf_locale_length,  lf_locale_name,      lf_set_vertical,       lf_get_vertical,
    lf_set_last_line,  lf_get_last_line,  lf_set_optical,      lf_get_optical,        lf_set_fallback,
    lf_get_fallback,   lf_set_spacing2,   lf_get_spacing2,     lf_set_axes,           lf_axis_count,
    lf_get_axes,       lf_get_auto,       lf_set_auto,
};
