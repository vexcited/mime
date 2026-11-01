/*
 * the text layout, part one: the ranges of formatting, the fonts of a run, and the shaping of the text into runs and
 * clusters.
 */
#include "layout_private.h"

/* ------------------------------------------------------------------ spans */

static void span_free(DwSpan *s)
{
    DwFree(s->family);
    DwFree(s->locale);
    DwFree(s->axes);
    if (s->collection)
        s->collection->lpVtbl->Release(s->collection);
    if (s->effect)
        s->effect->lpVtbl->Release(s->effect);
    if (s->inline_object)
        s->inline_object->lpVtbl->Release(s->inline_object);
    if (s->typography)
        s->typography->lpVtbl->Release(s->typography);
    memset(s, 0, sizeof(*s));
}

static HRESULT span_clone(DwSpan *dst, const DwSpan *src)
{
    *dst = *src;
    dst->family = DwStrDup(src->family);
    dst->locale = DwStrDup(src->locale);
    dst->axes = nullptr;
    if (src->axis_count) {
        dst->axes = DwAlloc(src->axis_count * sizeof(DWRITE_FONT_AXIS_VALUE));
        if (dst->axes)
            memcpy(dst->axes, src->axes, src->axis_count * sizeof(DWRITE_FONT_AXIS_VALUE));
        else
            dst->axis_count = 0;
    }
    if (dst->collection)
        dst->collection->lpVtbl->AddRef(dst->collection);
    if (dst->effect)
        dst->effect->lpVtbl->AddRef(dst->effect);
    if (dst->inline_object)
        dst->inline_object->lpVtbl->AddRef(dst->inline_object);
    if (dst->typography)
        dst->typography->lpVtbl->AddRef(dst->typography);
    return dst->family && dst->locale ? S_OK : E_OUTOFMEMORY;
}

BOOL DwSpanPropEqual(const DwSpan *a, const DwSpan *b, int prop)
{
    switch (prop) {
    case P_FAMILY:
        return !wcscmp(a->family, b->family);
    case P_COLLECTION:
        return a->collection == b->collection;
    case P_WEIGHT:
        return a->weight == b->weight;
    case P_STYLE:
        return a->style == b->style;
    case P_STRETCH:
        return a->stretch == b->stretch;
    case P_SIZE:
        return a->size == b->size;
    case P_UNDERLINE:
        return a->underline == b->underline;
    case P_STRIKE:
        return a->strike == b->strike;
    case P_EFFECT:
        return a->effect == b->effect;
    case P_INLINE:
        return a->inline_object == b->inline_object;
    case P_TYPOGRAPHY:
        return a->typography == b->typography;
    case P_LOCALE:
        return !wcscmp(a->locale, b->locale);
    case P_KERNING:
        return a->kerning == b->kerning;
    case P_SPACING:
        return a->lead_spacing == b->lead_spacing && a->trail_spacing == b->trail_spacing &&
               a->min_advance == b->min_advance;
    case P_AXES:
        return a->axis_count == b->axis_count &&
               (!a->axis_count || !memcmp(a->axes, b->axes, a->axis_count * sizeof(DWRITE_FONT_AXIS_VALUE)));
    }
    return TRUE;
}

static BOOL span_equal(const DwSpan *a, const DwSpan *b)
{
    for (int p = 0; p < P_COUNT; p++)
        if (!DwSpanPropEqual(a, b, p))
            return FALSE;
    return TRUE;
}

const DwSpan *DwSpanAt(const DwLayout *l, UINT32 position)
{
    UINT32 lo = 0, hi = l->span_count;
    while (hi - lo > 1) {
        UINT32 mid = (lo + hi) / 2;
        if (l->spans[mid].start <= position)
            lo = mid;
        else
            hi = mid;
    }
    return &l->spans[lo];
}

static HRESULT spans_insert(DwLayout *l, UINT32 index, const DwSpan *clone)
{
    if (l->span_count == l->span_capacity) {
        UINT32 cap = l->span_capacity ? l->span_capacity * 2 : 8;
        DwSpan *grown = DwAlloc(cap * sizeof(DwSpan));
        if (!grown)
            return E_OUTOFMEMORY;
        if (l->span_count)
            memcpy(grown, l->spans, l->span_count * sizeof(DwSpan));
        DwFree(l->spans);
        l->spans = grown;
        l->span_capacity = cap;
    }
    memmove(&l->spans[index + 1], &l->spans[index], (l->span_count - index) * sizeof(DwSpan));
    l->spans[index] = *clone;
    l->span_count++;
    return S_OK;
}

/* a boundary at pos, so a range can start or end there. */
static HRESULT spans_split(DwLayout *l, UINT32 pos)
{
    if (pos == 0 || pos == DW_OPEN_END)
        return S_OK;
    for (UINT32 i = 0; i < l->span_count; i++) {
        DwSpan *s = &l->spans[i];
        if (s->start < pos && pos < s->end) {
            DwSpan tail;
            HRESULT hr = span_clone(&tail, s);
            if (FAILED(hr))
                return hr;
            tail.start = pos;
            tail.end = s->end;
            s->end = pos;
            return spans_insert(l, i + 1, &tail);
        }
    }
    return S_OK;
}

static void spans_merge(DwLayout *l)
{
    for (UINT32 i = 0; i + 1 < l->span_count;) {
        if (span_equal(&l->spans[i], &l->spans[i + 1])) {
            l->spans[i].end = l->spans[i + 1].end;
            span_free(&l->spans[i + 1]);
            memmove(&l->spans[i + 1], &l->spans[i + 2], (l->span_count - i - 2) * sizeof(DwSpan));
            l->span_count--;
        } else {
            i++;
        }
    }
}

/* splits at the ends of a range and gives the spans inside, E_INVALIDARG for a range that wraps. */
HRESULT DwLayoutRange(DwLayout *l, DWRITE_TEXT_RANGE r, UINT32 *first, UINT32 *last)
{
    UINT64 end = (UINT64)r.startPosition + r.length;
    if (end > DW_OPEN_END)
        return E_INVALIDARG;
    *first = *last = 0;
    if (!r.length)
        return S_OK;
    HRESULT hr = spans_split(l, r.startPosition);
    if (SUCCEEDED(hr))
        hr = spans_split(l, (UINT32)end);
    if (FAILED(hr))
        return hr;
    UINT32 i = 0;
    while (i < l->span_count && l->spans[i].end <= r.startPosition)
        i++;
    UINT32 j = i;
    while (j < l->span_count && l->spans[j].start < (UINT32)end)
        j++;
    *first = i;
    *last = j;
    return S_OK;
}

void DwLayoutSpansDone(DwLayout *l)
{
    spans_merge(l);
    DwLayoutInvalidate(l, TRUE);
}

HRESULT DwLayoutSpansInit(DwLayout *l)
{
    DwSpan s;
    memset(&s, 0, sizeof(s));
    s.start = 0;
    s.end = DW_OPEN_END;
    s.family = DwStrDup(l->fmt.family);
    s.locale = DwStrDup(l->fmt.locale);
    if (!s.family || !s.locale) {
        DwFree(s.family);
        DwFree(s.locale);
        return E_OUTOFMEMORY;
    }
    s.collection = l->fmt.collection;
    if (s.collection)
        s.collection->lpVtbl->AddRef(s.collection);
    s.weight = l->fmt.weight;
    s.style = l->fmt.style;
    s.stretch = l->fmt.stretch;
    s.size = l->fmt.size;
    s.axes = nullptr;
    if (l->fmt.axis_count) {
        s.axes = DwAlloc(l->fmt.axis_count * sizeof(DWRITE_FONT_AXIS_VALUE));
        if (s.axes) {
            memcpy(s.axes, l->fmt.axis_values, l->fmt.axis_count * sizeof(DWRITE_FONT_AXIS_VALUE));
            s.axis_count = l->fmt.axis_count;
        }
    }
    return spans_insert(l, 0, &s);
}

void DwLayoutSpansFree(DwLayout *l)
{
    for (UINT32 i = 0; i < l->span_count; i++)
        span_free(&l->spans[i]);
    DwFree(l->spans);
    l->spans = nullptr;
    l->span_count = l->span_capacity = 0;
}

/* ------------------------------------------------------------------ the text as an analysis source */

typedef struct TextSource {
    const IDWriteTextAnalysisSourceVtbl *lpVtbl;
    const WCHAR *text;
    UINT32 length;
    const WCHAR *locale;
    DWRITE_READING_DIRECTION direction;
} TextSource;

static HRESULT WINAPI ts_query(IDWriteTextAnalysisSource *This, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteTextAnalysisSource)) {
        *out = This;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}
static ULONG WINAPI ts_ref(IDWriteTextAnalysisSource *This) { return 1; }
static HRESULT WINAPI ts_at(IDWriteTextAnalysisSource *This, UINT32 pos, const WCHAR **text, UINT32 *length)
{
    TextSource *s = (TextSource *)This;
    if (pos >= s->length) {
        *text = nullptr;
        *length = 0;
    } else {
        *text = s->text + pos;
        *length = s->length - pos;
    }
    return S_OK;
}
static HRESULT WINAPI ts_before(IDWriteTextAnalysisSource *This, UINT32 pos, const WCHAR **text, UINT32 *length)
{
    TextSource *s = (TextSource *)This;
    if (!pos || pos > s->length) {
        *text = nullptr;
        *length = 0;
    } else {
        *text = s->text;
        *length = pos;
    }
    return S_OK;
}
static DWRITE_READING_DIRECTION WINAPI ts_direction(IDWriteTextAnalysisSource *This)
{
    return ((TextSource *)This)->direction;
}
static HRESULT WINAPI ts_locale(IDWriteTextAnalysisSource *This, UINT32 pos, UINT32 *length, const WCHAR **locale)
{
    TextSource *s = (TextSource *)This;
    *length = s->length > pos ? s->length - pos : 0;
    *locale = s->locale;
    return S_OK;
}
static HRESULT WINAPI ts_numbers(IDWriteTextAnalysisSource *This, UINT32 pos, UINT32 *length,
                                 IDWriteNumberSubstitution **sub)
{
    TextSource *s = (TextSource *)This;
    *length = s->length > pos ? s->length - pos : 0;
    *sub = nullptr;
    return S_OK;
}
static const IDWriteTextAnalysisSourceVtbl g_text_source_vtbl = {ts_query,  ts_ref,       ts_ref,    ts_at,
                                                                 ts_before, ts_direction, ts_locale, ts_numbers};

/* ------------------------------------------------------------------ fonts */

static HRESULT map_font(DwFactory *factory, IDWriteFontFallback *custom, const WCHAR *text, UINT32 text_length,
                        const WCHAR *locale, UINT32 position, UINT32 length, IDWriteFontCollection *collection,
                        const WCHAR *family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
                        DWRITE_FONT_STRETCH stretch, UINT32 *mapped, IDWriteFontFace **face, FLOAT *scale)
{
    *face = nullptr;
    *mapped = 0;
    *scale = 1.0f;
    IDWriteFontFallback *fallback = custom;
    if (fallback) {
        fallback->lpVtbl->AddRef(fallback);
    } else {
        IDWriteFactory2 *f2 = (IDWriteFactory2 *)factory;
        HRESULT hr = f2->lpVtbl->GetSystemFontFallback(f2, &fallback);
        if (FAILED(hr))
            return hr;
    }
    TextSource source = {&g_text_source_vtbl, text, text_length, locale, DWRITE_READING_DIRECTION_LEFT_TO_RIGHT};
    IDWriteFont *font = nullptr;
    HRESULT hr = fallback->lpVtbl->MapCharacters(fallback, (IDWriteTextAnalysisSource *)&source, position, length,
                                                 collection, family, weight, style, stretch, mapped, &font, scale);
    fallback->lpVtbl->Release(fallback);
    if (FAILED(hr))
        return hr;
    if (font) {
        hr = font->lpVtbl->CreateFontFace(font, face);
        font->lpVtbl->Release(font);
    }
    return hr;
}

HRESULT DwFontForText(DwFactory *factory, const DwFmt *fmt, const WCHAR *text, UINT32 length, IDWriteFontFace **face,
                      FLOAT *scale)
{
    UINT32 mapped;
    HRESULT hr = map_font(factory, fmt->fallback, text, length, fmt->locale, 0, length, fmt->collection, fmt->family,
                          fmt->weight, fmt->style, fmt->stretch, &mapped, face, scale);
    if (SUCCEEDED(hr) && !*face)
        hr = DWRITE_E_NOFONT;
    return hr;
}

HRESULT DwResolveFace(DwLayout *l, const DwSpan *span, UINT32 position, UINT32 length, UINT32 *mapped,
                      IDWriteFontFace **face, FLOAT *scale)
{
    HRESULT hr = map_font(l->factory, l->fmt.fallback, l->text, l->length, span->locale, position, length,
                          span->collection ? span->collection : l->fmt.collection, span->family, span->weight,
                          span->style, span->stretch, mapped, face, scale);
    if (FAILED(hr))
        return hr;
    if (!*face || !*mapped) {
        /* nothing has the character: the notdef of the default font stands in for one character. */
        if (*face)
            (*face)->lpVtbl->Release(*face);
        *face = nullptr;
        static const WCHAR space[] = {' ', 0};
        DwFmt probe = l->fmt;
        probe.family = span->family;
        probe.weight = span->weight;
        probe.style = span->style;
        probe.stretch = span->stretch;
        probe.fallback = l->fmt.fallback;
        hr = DwFontForText(l->factory, &probe, space, 1, face, scale);
        UINT32 units;
        DwCodePointAt(l->text, l->length, position, &units);
        *mapped = units;
    }
    return hr;
}

/* the font of the span's own family, for characters that have nothing to show. */
static HRESULT resolve_base_face(DwLayout *l, const DwSpan *span, IDWriteFontFace **face, FLOAT *scale)
{
    IDWriteFontCollection *collection = span->collection ? span->collection : l->fmt.collection;
    *scale = 1.0f;
    *face = nullptr;
    UINT32 index = 0;
    DwCollection *dc = collection ? DwCollectionOf(l->factory, collection) : nullptr;
    BOOL exists = dc ? DwFindFamilyExact(dc, span->family, &index) : FALSE;
    if (!dc && collection)
        exists = SUCCEEDED(collection->lpVtbl->FindFamilyName(collection, span->family, &index, &exists)) && exists;
    if (collection && exists) {
        IDWriteFontFamily *family;
        IDWriteFont *font;
        if (SUCCEEDED(collection->lpVtbl->GetFontFamily(collection, index, &family))) {
            HRESULT hr = family->lpVtbl->GetFirstMatchingFont(family, span->weight, span->stretch, span->style, &font);
            family->lpVtbl->Release(family);
            if (SUCCEEDED(hr)) {
                hr = font->lpVtbl->CreateFontFace(font, face);
                font->lpVtbl->Release(font);
                return hr;
            }
        }
    }
    static const WCHAR space[] = {' ', 0};
    DwFmt probe = l->fmt;
    probe.family = span->family;
    probe.weight = span->weight;
    probe.style = span->style;
    probe.stretch = span->stretch;
    return DwFontForText(l->factory, &probe, space, 1, face, scale);
}

/* ------------------------------------------------------------------ runs */

typedef struct Build {
    DwLayout *l;
    UINT16 *script;
    UINT8 *shapes;
    UINT8 *explicit_level;
    UINT8 *level;
    UINT8 *gaps;     /* 0 none, 1 allowed, 2 required after each code unit. */
    UINT8 *gap_cond; /* the break conditions after, once inline objects had their say. */
    DwRun *runs;
    UINT32 run_count, run_capacity;
    DwCluster *clusters;
    UINT32 cluster_count, cluster_capacity;
} Build;

static BOOL run_push(Build *b, const DwRun *r)
{
    if (b->run_count == b->run_capacity) {
        UINT32 cap = b->run_capacity ? b->run_capacity * 2 : 16;
        DwRun *grown = DwAlloc(cap * sizeof(DwRun));
        if (!grown)
            return FALSE;
        if (b->run_count)
            memcpy(grown, b->runs, b->run_count * sizeof(DwRun));
        DwFree(b->runs);
        b->runs = grown;
        b->run_capacity = cap;
    }
    b->runs[b->run_count++] = *r;
    return TRUE;
}

static DwCluster *cluster_push(Build *b)
{
    if (b->cluster_count == b->cluster_capacity) {
        UINT32 cap = b->cluster_capacity ? b->cluster_capacity * 2 : 64;
        DwCluster *grown = DwAlloc(cap * sizeof(DwCluster));
        if (!grown)
            return nullptr;
        if (b->cluster_count)
            memcpy(grown, b->clusters, b->cluster_count * sizeof(DwCluster));
        DwFree(b->clusters);
        b->clusters = grown;
        b->cluster_capacity = cap;
    }
    DwCluster *c = &b->clusters[b->cluster_count++];
    memset(c, 0, sizeof(*c));
    return c;
}

void DwLayoutFreeBuilt(DwLayout *l, BOOL runs)
{
    if (l->lines) {
        for (UINT32 i = 0; i < l->line_count; i++) {
            DwFree(l->lines[i].items);
            if (l->lines[i].sign)
                l->lines[i].sign->lpVtbl->Release(l->lines[i].sign);
        }
        DwFree(l->lines);
        l->lines = nullptr;
        l->line_count = 0;
    }
    l->lines_valid = FALSE;
    if (!runs)
        return;
    for (UINT32 i = 0; i < l->run_count; i++) {
        DwRun *r = &l->runs[i];
        DwFree(r->glyphs);
        DwFree(r->advances);
        DwFree(r->offsets);
        DwFree(r->cluster_map);
        if (r->face)
            r->face->lpVtbl->Release(r->face);
        if (r->object)
            r->object->lpVtbl->Release(r->object);
    }
    DwFree(l->runs);
    DwFree(l->clusters);
    l->runs = nullptr;
    l->clusters = nullptr;
    l->run_count = l->cluster_count = 0;
    l->runs_valid = FALSE;
}

void DwLayoutInvalidate(DwLayout *l, BOOL runs) { DwLayoutFreeBuilt(l, runs); }

/* whether a span boundary matters for the shaping of the text across it. */
static BOOL shaping_differs(const DwSpan *a, const DwSpan *b)
{
    static const int props[] = {P_FAMILY, P_COLLECTION, P_WEIGHT, P_STYLE,   P_STRETCH, P_SIZE,
                                P_INLINE, P_TYPOGRAPHY, P_LOCALE, P_KERNING, P_SPACING, P_AXES};
    for (UINT32 i = 0; i < sizeof(props) / sizeof(props[0]); i++)
        if (!DwSpanPropEqual(a, b, props[i]))
            return TRUE;
    return FALSE;
}

static void run_metrics(DwLayout *l, DwRun *r)
{
    DWRITE_FONT_METRICS m;
    IDWriteFontFace *face = r->face;
    if (l->gdi) {
        if (FAILED(face->lpVtbl->GetGdiCompatibleMetrics(face, r->em, l->ppd, &l->transform, &m)))
            face->lpVtbl->GetMetrics(face, &m);
    } else {
        face->lpVtbl->GetMetrics(face, &m);
    }
    FLOAT k = m.designUnitsPerEm ? r->em / (FLOAT)m.designUnitsPerEm : 0;
    r->ascent = (FLOAT)m.ascent * k;
    r->descent = (FLOAT)m.descent * k;
    r->line_gap = (FLOAT)m.lineGap * k;
    r->underline_offset = -(FLOAT)m.underlinePosition * k;
    r->underline_thickness = (FLOAT)m.underlineThickness * k;
    r->strike_offset = -(FLOAT)m.strikethroughPosition * k;
    r->strike_thickness = (FLOAT)m.strikethroughThickness * k;
    r->cap_height = (FLOAT)m.capHeight * k;
}

static BOOL is_tab(const WCHAR *t, UINT32 i) { return t[i] == '\t'; }

/* appends the clusters of a run that has no glyphs of its own: control characters and inline objects. */
static BOOL clusters_unshaped(Build *b, UINT32 run_index, UINT32 start, UINT32 end)
{
    DwLayout *l = b->l;
    for (UINT32 i = start; i < end; i++) {
        DwCluster *c = cluster_push(b);
        if (!c)
            return FALSE;
        c->start = i;
        c->length = 1;
        c->run = run_index;
        c->level = b->level[i];
        c->flags = DWC_NO_VISUAL;
        WCHAR ch = l->text[i];
        if (ch == 0x00AD)
            c->flags |= DWC_SOFT_HYPHEN;
        if (is_tab(l->text, i))
            c->flags |= DWC_TAB | DWC_WHITESPACE;
        else if (DwIsWhitespace(ch))
            c->flags |= DWC_WHITESPACE;
        if (DwIsNewline(ch) && !(ch == 0x0D && i + 1 < l->length && l->text[i + 1] == 0x0A))
            c->flags |= DWC_NEWLINE | DWC_WHITESPACE;
        if (b->gap_cond[i])
            c->flags |= DWC_WRAP;
    }
    return TRUE;
}

static BOOL shape_run(Build *b, const DwSpan *span, UINT32 a, UINT32 e, IDWriteFontFace *face, FLOAT em,
                      UINT32 span_index)
{
    DwLayout *l = b->l;
    DwRun run;
    memset(&run, 0, sizeof(run));
    run.start = a;
    run.length = e - a;
    run.face = face;
    face->lpVtbl->AddRef(face);
    run.em = em;
    run.level = b->level[a];
    run.locale = span->locale;
    run.span = span_index;
    run_metrics(l, &run);
    UINT32 run_index = b->run_count;
    BOOL no_visual = b->shapes[a] != 0;
    if (no_visual) {
        if (!run_push(b, &run))
            return FALSE;
        return clusters_unshaped(b, run_index, a, e);
    }
    DwOtFont *font = DwOtOpen(face);
    if (!font)
        return FALSE;
    /* features of the typography and the pair kerning setting. */
    DwFeatureRange *ranges = nullptr;
    UINT32 nranges = 0;
    if (span->typography) {
        UINT32 count = span->typography->lpVtbl->GetFontFeatureCount(span->typography);
        ranges = DwAlloc((count + 1) * sizeof(DwFeatureRange));
        for (UINT32 i = 0; ranges && i < count; i++) {
            DWRITE_FONT_FEATURE f;
            if (SUCCEEDED(span->typography->lpVtbl->GetFontFeature(span->typography, i, &f))) {
                ranges[nranges].tag = f.nameTag;
                ranges[nranges].value = f.parameter;
                ranges[nranges].start = 0;
                ranges[nranges].end = e - a;
                nranges++;
            }
        }
    }
    DwShapeInput in = {l->text + a, e - a,        font,   b->script[a], FALSE,   (run.level & 1) != 0,
                       FALSE,       span->locale, ranges, nranges,      nullptr, nranges > 0};
    DwShaped shaped;
    HRESULT hr = DwShape(&in, &shaped);
    DwFree(ranges);
    if (FAILED(hr)) {
        DwOtClose(font);
        return FALSE;
    }
    FLOAT k = em / (FLOAT)DwOtUnitsPerEm(font);
    run.glyph_count = shaped.glyph_count;
    run.glyphs = shaped.glyphs;
    run.cluster_map = shaped.cluster_map;
    run.advances = DwAlloc((shaped.glyph_count + 1) * sizeof(FLOAT));
    run.offsets = DwAlloc((shaped.glyph_count + 1) * sizeof(DWRITE_GLYPH_OFFSET));
    if (!run.advances || !run.offsets) {
        DwOtClose(font);
        return FALSE;
    }
    for (UINT32 g = 0; g < shaped.glyph_count; g++) {
        FLOAT w = (FLOAT)shaped.advances[g] * k;
        if (l->gdi && l->ppd > 0) {
            FLOAT px = w * l->ppd;
            px = px >= 0 ? (FLOAT)(INT32)(px + 0.5f) : (FLOAT)(INT32)(px - 0.5f);
            w = px / l->ppd;
        }
        run.advances[g] = w;
        run.offsets[g].advanceOffset = (FLOAT)shaped.offset_x[g] * k;
        run.offsets[g].ascenderOffset = (FLOAT)shaped.offset_y[g] * k;
    }
    DwFree(shaped.text_flags);
    DwFree(shaped.glyph_flags);
    DwFree(shaped.advances);
    DwFree(shaped.offset_x);
    DwFree(shaped.offset_y);
    DwOtClose(font);
    if (!run_push(b, &run))
        return FALSE;
    DwRun *r = &b->runs[run_index];
    /* clusters from the map of characters to glyphs. */
    UINT32 len = e - a;
    for (UINT32 i = 0; i < len;) {
        UINT32 j = i + 1;
        while (j < len && r->cluster_map[j] == r->cluster_map[i])
            j++;
        DwCluster *c = cluster_push(b);
        if (!c)
            return FALSE;
        c->start = a + i;
        c->length = j - i;
        c->run = run_index;
        c->level = b->level[a + i];
        c->glyph0 = r->cluster_map[i];
        c->glyph1 = j < len ? r->cluster_map[j] : r->glyph_count;
        if (c->glyph1 < c->glyph0)
            c->glyph1 = c->glyph0;
        FLOAT w = 0;
        for (UINT32 g = c->glyph0; g < c->glyph1; g++)
            w += r->advances[g];
        c->width = w;
        BOOL ws = TRUE;
        for (UINT32 u = i; u < j; u++)
            if (!DwIsWhitespace(l->text[a + u]))
                ws = FALSE;
        if (ws)
            c->flags |= DWC_WHITESPACE;
        if (b->gap_cond[a + j - 1])
            c->flags |= DWC_WRAP;
        i = j;
    }
    /* character spacing of the span. */
    if (span->lead_spacing != 0 || span->trail_spacing != 0 || span->min_advance != 0) {
        for (UINT32 ci = b->cluster_count; ci > 0 && b->clusters[ci - 1].run == run_index; ci--) {
            DwCluster *c = &b->clusters[ci - 1];
            if (c->glyph1 <= c->glyph0)
                continue;
            r->advances[c->glyph0] += span->lead_spacing;
            r->offsets[c->glyph0].advanceOffset += span->lead_spacing;
            r->advances[c->glyph1 - 1] += span->trail_spacing;
            FLOAT w = c->width + span->lead_spacing + span->trail_spacing;
            if (w < span->min_advance) {
                FLOAT extra = span->min_advance - w;
                r->advances[c->glyph1 - 1] += extra / 2;
                r->advances[c->glyph0] += extra / 2;
                r->offsets[c->glyph0].advanceOffset += extra / 2;
                w = span->min_advance;
            }
            c->width = w;
        }
    }
    return TRUE;
}

HRESULT DwLayoutBuildRuns(DwLayout *l)
{
    UINT32 n = l->length;
    Build b;
    memset(&b, 0, sizeof(b));
    b.l = l;
    HRESULT hr = S_OK;
    BOOL ok = TRUE;
    UINT8 *mark = nullptr;
    if (n) {
        b.script = DwAlloc(n * sizeof(UINT16));
        b.shapes = DwAlloc(n);
        b.explicit_level = DwAlloc(n);
        b.level = DwAlloc(n);
        b.gaps = DwAlloc(n);
        b.gap_cond = DwAlloc(n);
        mark = DwAllocZero(n + 1);
        if (!b.script || !b.shapes || !b.explicit_level || !b.level || !b.gaps || !b.gap_cond || !mark)
            ok = FALSE;
    }
    if (ok && n) {
        DwResolveScripts(l->text, n, b.script, b.shapes);
        DwBidiLevels(l->text, n, l->fmt.reading == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT ? 1 : 0, b.explicit_level,
                     b.level);
        DwLineBreaks(l->text, n, b.gaps);
        for (UINT32 i = 0; i < n; i++)
            b.gap_cond[i] = b.gaps[i] != 0;
        b.gap_cond[n - 1] = 1;
        /* inline objects say how they break around themselves. */
        for (UINT32 s = 0; s < l->span_count; s++) {
            const DwSpan *sp = &l->spans[s];
            if (!sp->inline_object || sp->start >= n)
                continue;
            UINT32 end = sp->end < n ? sp->end : n;
            DWRITE_BREAK_CONDITION before = DWRITE_BREAK_CONDITION_NEUTRAL, after = DWRITE_BREAK_CONDITION_NEUTRAL;
            sp->inline_object->lpVtbl->GetBreakConditions(sp->inline_object, &before, &after);
            if (sp->start > 0 && before != DWRITE_BREAK_CONDITION_NEUTRAL)
                b.gap_cond[sp->start - 1] =
                    before == DWRITE_BREAK_CONDITION_CAN_BREAK || before == DWRITE_BREAK_CONDITION_MUST_BREAK;
            if (after != DWRITE_BREAK_CONDITION_NEUTRAL)
                b.gap_cond[end - 1] =
                    after == DWRITE_BREAK_CONDITION_CAN_BREAK || after == DWRITE_BREAK_CONDITION_MUST_BREAK;
        }
        mark[0] = mark[n] = 1;
        for (UINT32 i = 1; i < n; i++) {
            if (b.script[i] != b.script[i - 1] || b.shapes[i] != b.shapes[i - 1] || b.level[i] != b.level[i - 1])
                mark[i] = 1;
            if (DwIsNewline(l->text[i]) || DwIsNewline(l->text[i - 1]))
                mark[i] = 1;
        }
        for (UINT32 s = 1; s < l->span_count; s++) {
            UINT32 at = l->spans[s].start;
            if (at < n && shaping_differs(&l->spans[s - 1], &l->spans[s]))
                mark[at] = 1;
        }
        for (UINT32 s = 0; s < l->span_count; s++) {
            const DwSpan *sp = &l->spans[s];
            if (!sp->inline_object || sp->start >= n)
                continue;
            UINT32 end = sp->end < n ? sp->end : n;
            for (UINT32 i = sp->start + 1; i < end; i++)
                mark[i] = 0;
            mark[sp->start] = 1;
            mark[end] = 1;
        }
    }
    UINT32 a = 0;
    while (ok && a < n) {
        UINT32 e = a + 1;
        while (!mark[e])
            e++;
        const DwSpan *span = DwSpanAt(l, a);
        UINT32 span_index = (UINT32)(span - l->spans);
        if (span->inline_object) {
            DwRun run;
            memset(&run, 0, sizeof(run));
            run.start = a;
            run.length = e - a;
            run.level = b.level[a];
            run.object = span->inline_object;
            span->inline_object->lpVtbl->AddRef(span->inline_object);
            run.locale = span->locale;
            run.span = span_index;
            hr = span->inline_object->lpVtbl->GetMetrics(span->inline_object, &run.object_metrics);
            if (FAILED(hr)) {
                if (run.object)
                    run.object->lpVtbl->Release(run.object);
                ok = FALSE;
                break;
            }
            UINT32 run_index = b.run_count;
            ok = run_push(&b, &run);
            DwCluster *c = ok ? cluster_push(&b) : nullptr;
            if (!c) {
                ok = FALSE;
                break;
            }
            c->start = a;
            c->length = e - a;
            c->run = run_index;
            c->level = b.level[a];
            c->flags = DWC_OBJECT;
            c->width = run.object_metrics.width;
            if (b.gap_cond[e - 1])
                c->flags |= DWC_WRAP;
            a = e;
            continue;
        }
        /* the fonts for the segment, one run for each stretch a font covers. */
        UINT32 p = a;
        while (ok && p < e) {
            IDWriteFontFace *face = nullptr;
            UINT32 mapped = 0;
            FLOAT scale = 1.0f;
            if (b.shapes[p]) {
                hr = resolve_base_face(l, span, &face, &scale);
                mapped = e - p;
            } else {
                hr = DwResolveFace(l, span, p, e - p, &mapped, &face, &scale);
            }
            if (FAILED(hr) || !face) {
                ok = FALSE;
                if (SUCCEEDED(hr))
                    hr = DWRITE_E_NOFONT;
                break;
            }
            if (mapped > e - p)
                mapped = e - p;
            if (!mapped)
                mapped = 1;
            ok = shape_run(&b, span, p, p + mapped, face, span->size * scale, span_index);
            face->lpVtbl->Release(face);
            p += mapped;
        }
        a = e;
    }
    DwFree(mark);
    DwFree(b.script);
    DwFree(b.shapes);
    DwFree(b.explicit_level);
    DwFree(b.level);
    DwFree(b.gaps);
    DwFree(b.gap_cond);
    if (!ok) {
        l->runs = b.runs;
        l->run_count = b.run_count;
        l->clusters = b.clusters;
        l->cluster_count = b.cluster_count;
        DwLayoutFreeBuilt(l, TRUE);
        return FAILED(hr) ? hr : E_OUTOFMEMORY;
    }
    l->runs = b.runs;
    l->run_count = b.run_count;
    l->clusters = b.clusters;
    l->cluster_count = b.cluster_count;
    l->runs_valid = TRUE;
    return S_OK;
}
