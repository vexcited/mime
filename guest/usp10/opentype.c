/*
 * the opentype side of uniscribe: script, language and feature tags from the
 * font's GSUB and GPOS tables, and single glyph lookups by feature: single and
 * alternate substitution, single positioning. shaping stays nominal, so
 * features apply only when a range asks for them.
 */
#include "usp10_private.h"

#define TAG(a, b, c, d) ((OPENTYPE_TAG)(a) | (OPENTYPE_TAG)(b) << 8 | (OPENTYPE_TAG)(c) << 16 | (OPENTYPE_TAG)(d) << 24)
#define TAG_DFLT TAG('D', 'F', 'L', 'T')
#define TABLE_GSUB TAG('G', 'S', 'U', 'B')
#define TABLE_GPOS TAG('G', 'P', 'O', 'S')
#define TABLE_HEAD TAG('h', 'e', 'a', 'd')

/* ------------------------------------------------------------------ reading the tables */

static UINT u16(const USP_TABLE *t, DWORD offset)
{
    return offset + 2 <= t->Size ? (UINT)t->Data[offset] << 8 | t->Data[offset + 1] : 0;
}

static DWORD u32(const USP_TABLE *t, DWORD offset)
{
    return (DWORD)u16(t, offset) << 16 | u16(t, offset + 2);
}

/* bytes in the font's order. */
static OPENTYPE_TAG tag_at(const USP_TABLE *t, DWORD offset)
{
    return offset + 4 <= t->Size ? TAG(t->Data[offset], t->Data[offset + 1], t->Data[offset + 2], t->Data[offset + 3])
                                 : 0;
}

static void read_table(HDC dc, DWORD tag, USP_TABLE *t)
{
    DWORD size = GetFontData(dc, tag, 0, nullptr, 0);
    if (size == GDI_ERROR || !size || !(t->Data = UspAlloc(size)))
        return;
    if (GetFontData(dc, tag, 0, t->Data, size) != size) {
        UspFree(t->Data);
        t->Data = nullptr;
        return;
    }
    t->Size = size;
}

/* layout tables are read from dc the first time. */
static HRESULT get_tables(HDC dc, SCRIPT_CACHE *cache, USP_CACHE **out)
{
    HRESULT hr = UspGetCache(dc, cache, out);
    if (FAILED(hr))
        return hr;
    USP_CACHE *c = *out;
    if (c->TablesRead)
        return S_OK;
    if (!dc)
        return E_PENDING;
    read_table(dc, TABLE_GSUB, &c->Gsub);
    read_table(dc, TABLE_GPOS, &c->Gpos);
    BYTE head[20];
    c->UnitsPerEm = GetFontData(dc, TABLE_HEAD, 0, head, sizeof(head)) == sizeof(head) ? head[18] << 8 | head[19] : 0;
    c->TablesRead = TRUE;
    return S_OK;
}

/* falls back to the default script when the font lacks this one. */
static DWORD find_script(const USP_TABLE *t, OPENTYPE_TAG script)
{
    DWORD list = u16(t, 4);
    if (!list)
        return 0;
    UINT count = u16(t, list);
    DWORD fallback = 0;
    for (UINT i = 0; i < count; i++) {
        DWORD record = list + 2 + 6 * i;
        OPENTYPE_TAG tag = tag_at(t, record);
        if (tag == script)
            return list + u16(t, record + 4);
        if (tag == TAG_DFLT || (!fallback && tag == TAG('l', 'a', 't', 'n')))
            fallback = list + u16(t, record + 4);
    }
    return fallback;
}

/* language 0 or a missing language gives the default language system. */
static DWORD find_language(const USP_TABLE *t, DWORD script, OPENTYPE_TAG language)
{
    if (!script)
        return 0;
    UINT count = u16(t, script + 2);
    for (UINT i = 0; language && i < count; i++) {
        DWORD record = script + 4 + 6 * i;
        if (tag_at(t, record) == language)
            return script + u16(t, record + 4);
    }
    DWORD def = u16(t, script);
    return def ? script + def : 0;
}

static DWORD find_feature(const USP_TABLE *t, DWORD langsys, OPENTYPE_TAG feature)
{
    DWORD list = u16(t, 6);
    if (!langsys || !list)
        return 0;
    UINT features = u16(t, list), count = u16(t, langsys + 4), required = u16(t, langsys + 2);
    for (UINT i = 0; i <= count; i++) {
        UINT index = i < count ? u16(t, langsys + 6 + 2 * i) : required;
        if (index >= features)
            continue;
        DWORD record = list + 2 + 6 * index;
        if (tag_at(t, record) == feature)
            return list + u16(t, record + 4);
    }
    return 0;
}

/* -1 when the table does not cover the glyph. */
static int coverage_index(const USP_TABLE *t, DWORD coverage, WORD glyph)
{
    UINT format = u16(t, coverage), count = u16(t, coverage + 2);
    if (format == 1) {
        for (UINT i = 0; i < count; i++) {
            if (u16(t, coverage + 4 + 2 * i) == glyph)
                return (int)i;
        }
    } else if (format == 2) {
        for (UINT i = 0; i < count; i++) {
            DWORD range = coverage + 4 + 6 * i;
            UINT start = u16(t, range), end = u16(t, range + 2);
            if (glyph >= start && glyph <= end)
                return (int)(u16(t, range + 4) + glyph - start);
        }
    }
    return -1;
}

typedef BOOL (*SUBTABLE_VISIT)(const USP_TABLE *t, UINT type, DWORD subtable, void *context);

/* walks every subtable of every lookup, through extension lookups, until visit says stop. */
static BOOL visit_feature(const USP_TABLE *t, DWORD feature, UINT extension, SUBTABLE_VISIT visit, void *context)
{
    DWORD lookups = u16(t, 8);
    if (!feature || !lookups)
        return FALSE;
    UINT count = u16(t, feature + 2), total = u16(t, lookups);
    for (UINT i = 0; i < count; i++) {
        UINT index = u16(t, feature + 4 + 2 * i);
        if (index >= total)
            continue;
        DWORD lookup = lookups + u16(t, lookups + 2 + 2 * index);
        UINT type = u16(t, lookup), subtables = u16(t, lookup + 4);
        for (UINT k = 0; k < subtables; k++) {
            DWORD subtable = lookup + u16(t, lookup + 6 + 2 * k);
            UINT real = type;
            if (type == extension && u16(t, subtable) == 1) {
                real = u16(t, subtable + 2);
                subtable += u32(t, subtable + 4);
            }
            if (visit(t, real, subtable, context))
                return TRUE;
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ substitution and positioning */

typedef struct SUBSTITUTION {
    WORD Glyph;
    LONG Choice; /* 1-based. */
    int Max;
    WORD *Out;   /* set when listing alternates. */
    int Count;
} SUBSTITUTION;

static BOOL substitute(const USP_TABLE *t, UINT type, DWORD subtable, void *context)
{
    SUBSTITUTION *s = context;
    UINT format = u16(t, subtable);
    int index = coverage_index(t, subtable + u16(t, subtable + 2), s->Glyph);
    if (index < 0)
        return FALSE;
    if (type == 1) {
        if (format != 1 && (UINT)index >= u16(t, subtable + 4))
            return FALSE;
        WORD to = format == 1 ? (WORD)(s->Glyph + (SHORT)u16(t, subtable + 4))
                              : (WORD)u16(t, subtable + 6 + 2 * (DWORD)index);
        if (s->Out) {
            if (s->Count < s->Max)
                s->Out[s->Count] = to;
            s->Count++;
        } else {
            s->Glyph = to;
        }
        return TRUE;
    }
    if (type == 3 && format == 1 && (UINT)index < u16(t, subtable + 4)) {
        DWORD set = subtable + u16(t, subtable + 6 + 2 * (DWORD)index);
        UINT count = u16(t, set);
        if (s->Out) {
            for (UINT i = 0; i < count; i++, s->Count++) {
                if (s->Count < s->Max)
                    s->Out[s->Count] = (WORD)u16(t, set + 2 + 2 * i);
            }
        } else if (s->Choice >= 1 && (UINT)s->Choice <= count) {
            s->Glyph = (WORD)u16(t, set + 2 + 2 * (DWORD)(s->Choice - 1));
        }
        return TRUE;
    }
    return FALSE;
}

typedef struct POSITIONING {
    WORD Glyph;
    int X, Y, Advance; /* design units. */
} POSITIONING;

static int value_size(UINT format)
{
    int n = 0;
    for (UINT bits = format & 0xFF; bits; bits &= bits - 1)
        n += 2;
    return n;
}

static BOOL position(const USP_TABLE *t, UINT type, DWORD subtable, void *context)
{
    POSITIONING *p = context;
    if (type != 1)
        return FALSE;
    UINT format = u16(t, subtable), value_format = u16(t, subtable + 4);
    int index = coverage_index(t, subtable + u16(t, subtable + 2), p->Glyph);
    if (index < 0)
        return FALSE;
    DWORD value = subtable + 6;
    if (format == 2) {
        if ((UINT)index >= u16(t, subtable + 6))
            return FALSE;
        value = subtable + 8 + (DWORD)(value_size(value_format) * index);
    }
    if (value_format & 1)
        p->X += (SHORT)u16(t, value), value += 2;
    if (value_format & 2)
        p->Y += (SHORT)u16(t, value), value += 2;
    if (value_format & 4)
        p->Advance += (SHORT)u16(t, value);
    return TRUE;
}

static DWORD feature_of(const USP_TABLE *t, OPENTYPE_TAG script, OPENTYPE_TAG language, OPENTYPE_TAG feature)
{
    if (!t->Data)
        return 0;
    return find_feature(t, find_language(t, find_script(t, script), language), feature);
}

/* design units to the font's pixels. */
static int scale(const USP_CACHE *c, int units)
{
    int em = c->Metrics.tmHeight - c->Metrics.tmInternalLeading;
    return c->UnitsPerEm ? MulDiv(units, em, (int)c->UnitsPerEm) : units;
}

/* ------------------------------------------------------------------ tags */

#define MAX_TAGS 512

typedef struct TAG_LIST {
    OPENTYPE_TAG Tags[MAX_TAGS];
    int Count;
} TAG_LIST;

/* adds each tag once. */
static void add_tags(const USP_TABLE *t, DWORD records, UINT count, UINT stride, TAG_LIST *list)
{
    for (UINT i = 0; i < count; i++) {
        OPENTYPE_TAG tag = tag_at(t, records + stride * i);
        BOOL seen = FALSE;
        for (int k = 0; k < list->Count; k++)
            seen |= list->Tags[k] == tag;
        if (!seen && list->Count < MAX_TAGS)
            list->Tags[list->Count++] = tag;
    }
}

/* copies as many as fit; E_OUTOFMEMORY when some did not. */
static HRESULT finish_tags(const TAG_LIST *list, int max, OPENTYPE_TAG *tags, int *count)
{
    for (int i = 0; i < list->Count && i < max; i++)
        tags[i] = list->Tags[i];
    *count = list->Count;
    return list->Count > max ? E_OUTOFMEMORY : S_OK;
}

/* without layout tables, only the run's own script. */
USPAPI ScriptGetFontScriptTags(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, int max_tags,
                               OPENTYPE_TAG *tags, int *count)
{
    if (!tags || !count || max_tags <= 0)
        return E_INVALIDARG;
    USP_CACHE *c;
    HRESULT hr = get_tables(dc, cache, &c);
    if (FAILED(hr))
        return hr;
    TAG_LIST *found = UspAlloc(sizeof(*found));
    if (!found)
        return E_OUTOFMEMORY;
    const USP_TABLE *tables[2] = {&c->Gsub, &c->Gpos};
    for (int i = 0; i < 2; i++) {
        DWORD list = tables[i]->Data ? u16(tables[i], 4) : 0;
        if (list)
            add_tags(tables[i], list + 2, u16(tables[i], list), 6, found);
    }
    if (!found->Count) {
        OPENTYPE_TAG own = analysis ? UspScriptTag(analysis->eScript) : SCRIPT_TAG_UNKNOWN;
        found->Tags[found->Count++] = own ? own : TAG_DFLT;
    }
    hr = finish_tags(found, max_tags, tags, count);
    UspFree(found);
    return hr;
}

USPAPI ScriptGetFontLanguageTags(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                                 int max_tags, OPENTYPE_TAG *tags, int *count)
{
    if (!tags || !count || max_tags <= 0)
        return E_INVALIDARG;
    USP_CACHE *c;
    HRESULT hr = get_tables(dc, cache, &c);
    if (FAILED(hr))
        return hr;
    TAG_LIST *found = UspAlloc(sizeof(*found));
    if (!found)
        return E_OUTOFMEMORY;
    const USP_TABLE *tables[2] = {&c->Gsub, &c->Gpos};
    for (int i = 0; i < 2; i++) {
        DWORD s = tables[i]->Data ? find_script(tables[i], script) : 0;
        if (s)
            add_tags(tables[i], s + 4, u16(tables[i], s + 2), 6, found);
    }
    hr = finish_tags(found, max_tags, tags, count);
    UspFree(found);
    return hr;
}

USPAPI ScriptGetFontFeatureTags(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                                OPENTYPE_TAG language, int max_tags, OPENTYPE_TAG *tags, int *count)
{
    if (!tags || !count || max_tags <= 0)
        return E_INVALIDARG;
    USP_CACHE *c;
    HRESULT hr = get_tables(dc, cache, &c);
    if (FAILED(hr))
        return hr;
    TAG_LIST *found = UspAlloc(sizeof(*found));
    if (!found)
        return E_OUTOFMEMORY;
    const USP_TABLE *tables[2] = {&c->Gsub, &c->Gpos};
    for (int i = 0; i < 2; i++) {
        const USP_TABLE *t = tables[i];
        DWORD langsys = t->Data ? find_language(t, find_script(t, script), language) : 0, list = t->Data ? u16(t, 6) : 0;
        if (!langsys || !list)
            continue;
        UINT features = u16(t, list), indices = u16(t, langsys + 4), required = u16(t, langsys + 2);
        for (UINT k = 0; k <= indices; k++) {
            UINT index = k < indices ? u16(t, langsys + 6 + 2 * k) : required;
            if (index < features)
                add_tags(t, list + 2 + 6 * index, 1, 6, found);
        }
    }
    hr = finish_tags(found, max_tags, tags, count);
    UspFree(found);
    return hr;
}

/* the glyph itself comes first, then its alternates. */
USPAPI ScriptGetFontAlternateGlyphs(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                                    OPENTYPE_TAG language, OPENTYPE_TAG feature, WORD glyph, int max_alternates,
                                    WORD *alternates, int *count)
{
    if (!alternates || !count || max_alternates <= 0)
        return E_INVALIDARG;
    USP_CACHE *c;
    HRESULT hr = get_tables(dc, cache, &c);
    if (FAILED(hr))
        return hr;
    alternates[0] = glyph;
    SUBSTITUTION s = {.Glyph = glyph, .Max = max_alternates - 1, .Out = alternates + 1};
    visit_feature(&c->Gsub, feature_of(&c->Gsub, script, language, feature), 7, substitute, &s);
    *count = s.Count + 1;
    return *count > max_alternates ? E_OUTOFMEMORY : S_OK;
}

USPAPI ScriptSubstituteSingleGlyph(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                                   OPENTYPE_TAG language, OPENTYPE_TAG feature, LONG parameter, WORD glyph,
                                   WORD *out_glyph)
{
    if (!out_glyph)
        return E_INVALIDARG;
    USP_CACHE *c;
    HRESULT hr = get_tables(dc, cache, &c);
    if (FAILED(hr))
        return hr;
    SUBSTITUTION s = {.Glyph = glyph, .Choice = parameter};
    if (parameter)
        visit_feature(&c->Gsub, feature_of(&c->Gsub, script, language, feature), 7, substitute, &s);
    *out_glyph = s.Glyph;
    return S_OK;
}

USPAPI ScriptPositionSingleGlyph(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                                 OPENTYPE_TAG language, OPENTYPE_TAG feature, LONG parameter, WORD glyph,
                                 int advance, GOFFSET offset, int *out_advance, GOFFSET *out_offset)
{
    if (!out_advance || !out_offset)
        return E_INVALIDARG;
    USP_CACHE *c;
    HRESULT hr = get_tables(dc, cache, &c);
    if (FAILED(hr))
        return hr;
    POSITIONING p = {.Glyph = glyph};
    if (parameter)
        visit_feature(&c->Gpos, feature_of(&c->Gpos, script, language, feature), 9, position, &p);
    *out_advance = advance + scale(c, p.Advance);
    out_offset->du = offset.du + scale(c, p.X);
    out_offset->dv = offset.dv + scale(c, p.Y);
    return S_OK;
}

/* ------------------------------------------------------------------ items, shaping and placing */

USPAPI ScriptItemizeOpenType(const WCHAR *chars, int count, int max_items, const SCRIPT_CONTROL *control,
                             const SCRIPT_STATE *state, SCRIPT_ITEM *items, OPENTYPE_TAG *script_tags,
                             int *item_count)
{
    if (!script_tags)
        return E_INVALIDARG;
    int n = 0;
    HRESULT hr = ScriptItemize(chars, count, max_items, control, state, items, &n);
    if (FAILED(hr))
        return hr;
    for (int i = 0; i < n; i++)
        script_tags[i] = UspScriptTag(items[i].a.eScript);
    if (item_count)
        *item_count = n;
    return S_OK;
}

static int range_of(const int *range_chars, int ranges, int c)
{
    for (int r = 0, end = 0; r < ranges; r++) {
        end += range_chars[r];
        if (c < end)
            return r;
    }
    return -1;
}

USPAPI ScriptShapeOpenType(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                           OPENTYPE_TAG language, int *range_chars, TEXTRANGE_PROPERTIES **range_properties,
                           int ranges, const WCHAR *chars, int count, int max_glyphs, WORD *clusters,
                           SCRIPT_CHARPROP *char_props, WORD *glyphs, SCRIPT_GLYPHPROP *glyph_props,
                           int *glyph_count)
{
    if (!char_props || !glyph_props || (ranges && (!range_chars || !range_properties)))
        return E_INVALIDARG;
    SCRIPT_VISATTR *attrs = UspAlloc((SIZE_T)(max_glyphs > 0 ? max_glyphs : 1) * sizeof(SCRIPT_VISATTR));
    if (!attrs)
        return E_OUTOFMEMORY;
    HRESULT hr = ScriptShape(dc, cache, chars, count, max_glyphs, analysis, glyphs, clusters, attrs, glyph_count);
    if (SUCCEEDED(hr)) {
        for (int i = 0; i < *glyph_count; i++)
            glyph_props[i] = (SCRIPT_GLYPHPROP){attrs[i], 0};
        for (int i = 0; i < count; i++)
            char_props[i] = (SCRIPT_CHARPROP){.fCanGlyphAlone = 1};
    }
    UspFree(attrs);
    if (FAILED(hr) || !ranges || analysis->fNoGlyphIndex)
        return hr;

    /* each range's features apply to the glyphs of its characters. */
    for (int c = 0; c < count; c++) {
        int r = range_of(range_chars, ranges, c);
        const TEXTRANGE_PROPERTIES *props = r >= 0 ? range_properties[r] : nullptr;
        if (!props || (c > 0 && clusters[c] == clusters[c - 1]))
            continue;
        for (int f = 0; f < props->cotfRecords; f++) {
            const OPENTYPE_FEATURE_RECORD *record = &props->potfRecords[f];
            if (record->lParameter)
                ScriptSubstituteSingleGlyph(dc, cache, analysis, script, language, record->tagFeature,
                                            record->lParameter, glyphs[clusters[c]], &glyphs[clusters[c]]);
        }
    }
    return S_OK;
}

USPAPI ScriptPlaceOpenType(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                           OPENTYPE_TAG language, int *range_chars, TEXTRANGE_PROPERTIES **range_properties,
                           int ranges, const WCHAR *chars, WORD *clusters, SCRIPT_CHARPROP *char_props, int count,
                           const WORD *glyphs, const SCRIPT_GLYPHPROP *glyph_props, int glyph_count,
                           int *advances, GOFFSET *offsets, ABC *abc)
{
    if (!glyph_props || glyph_count <= 0 || (ranges && (!range_chars || !range_properties)))
        return E_INVALIDARG;
    SCRIPT_VISATTR *attrs = UspAlloc((SIZE_T)glyph_count * sizeof(SCRIPT_VISATTR));
    if (!attrs)
        return E_OUTOFMEMORY;
    for (int i = 0; i < glyph_count; i++)
        attrs[i] = glyph_props[i].sva;
    HRESULT hr = ScriptPlace(dc, cache, glyphs, glyph_count, attrs, analysis, advances, offsets, abc);
    UspFree(attrs);
    if (FAILED(hr) || !ranges || !clusters || !offsets || analysis->fNoGlyphIndex)
        return hr;

    BOOL changed = FALSE;
    for (int c = 0; c < count; c++) {
        int r = range_of(range_chars, ranges, c);
        const TEXTRANGE_PROPERTIES *props = r >= 0 ? range_properties[r] : nullptr;
        if (!props || (c > 0 && clusters[c] == clusters[c - 1]))
            continue;
        int g = clusters[c];
        for (int f = 0; f < props->cotfRecords; f++) {
            const OPENTYPE_FEATURE_RECORD *record = &props->potfRecords[f];
            if (!record->lParameter)
                continue;
            ScriptPositionSingleGlyph(dc, cache, analysis, script, language, record->tagFeature, record->lParameter,
                                      glyphs[g], advances[g], offsets[g], &advances[g], &offsets[g]);
            changed = TRUE;
        }
    }
    if (changed && abc) {
        int total = 0;
        for (int i = 0; i < glyph_count; i++)
            total += advances[i];
        abc->abcB = (UINT)(total - abc->abcA - abc->abcC);
    }
    return S_OK;
}
