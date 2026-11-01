/*
 * IDWriteTextAnalyzer, 1 and 2: the script, bidirectional, number and line break analysis of text, the shaping
 * entry points, and the helpers for spacing, justification and orientation. also IDWriteNumberSubstitution.
 */
#include "text_private.h"

typedef struct DwAnalyzer {
    const IDWriteTextAnalyzer2Vtbl *lpVtbl;
    LONG refs;
    DwFactory *factory;
} DwAnalyzer;

/* ------------------------------------------------------------------ number substitution */

typedef struct DwNumberSub {
    const IDWriteNumberSubstitutionVtbl *lpVtbl;
    LONG refs;
    DWRITE_NUMBER_SUBSTITUTION_METHOD method;
    WCHAR *locale;
    BOOL ignore_override;
} DwNumberSub;

static HRESULT WINAPI numsub_query(IDWriteNumberSubstitution *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteNumberSubstitution)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI numsub_addref(IDWriteNumberSubstitution *This)
{
    return InterlockedIncrement(&((DwNumberSub *)This)->refs);
}

static ULONG WINAPI numsub_release(IDWriteNumberSubstitution *This)
{
    DwNumberSub *n = (DwNumberSub *)This;
    ULONG refs = InterlockedDecrement(&n->refs);
    if (!refs) {
        DwFree(n->locale);
        DwFree(n);
    }
    return refs;
}

static const IDWriteNumberSubstitutionVtbl g_numsub_vtbl = {numsub_query, numsub_addref, numsub_release};

HRESULT DwNumberSubstitutionCreate(DWRITE_NUMBER_SUBSTITUTION_METHOD method, const WCHAR *locale, BOOL ignore_override,
                                   IDWriteNumberSubstitution **out)
{
    DwNumberSub *n = DwAllocZero(sizeof(*n));
    if (!n)
        return E_OUTOFMEMORY;
    n->lpVtbl = &g_numsub_vtbl;
    n->refs = 1;
    n->method = method;
    n->ignore_override = ignore_override;
    n->locale = locale ? DwStrDup(locale) : DwStrDup(L"");
    if (!n->locale) {
        DwFree(n);
        return E_OUTOFMEMORY;
    }
    DwLower(n->locale);
    *out = (IDWriteNumberSubstitution *)n;
    return S_OK;
}

BOOL DwNumberSubstitutionInfo(IDWriteNumberSubstitution *s, DWRITE_NUMBER_SUBSTITUTION_METHOD *method,
                              const WCHAR **locale)
{
    if (!s || s->lpVtbl != &g_numsub_vtbl)
        return FALSE;
    DwNumberSub *n = (DwNumberSub *)s;
    *method = n->method;
    *locale = n->locale;
    return TRUE;
}

/* the digit zero a language writes numbers with, in national and traditional use. */
static UINT32 language_digits(const WCHAR *locale)
{
    static const struct {
        const char *lang;
        UINT32 zero;
    } map[] = {
        {"ar", 0x0660}, {"fa", 0x06F0}, {"ur", 0x06F0}, {"ps", 0x06F0}, {"ku", 0x0660}, {"sd", 0x0660}, {"bn", 0x09E6},
        {"as", 0x09E6}, {"hi", 0x0966}, {"mr", 0x0966}, {"ne", 0x0966}, {"sa", 0x0966}, {"gu", 0x0AE6}, {"pa", 0x0A66},
        {"or", 0x0B66}, {"ta", 0x0BE6}, {"te", 0x0C66}, {"kn", 0x0CE6}, {"ml", 0x0D66}, {"th", 0x0E50}, {"lo", 0x0ED0},
        {"my", 0x1040}, {"km", 0x17E0}, {"bo", 0x0F20}, {"dz", 0x0F20}, {"mn", 0x1810},
    };
    if (!locale || !locale[0] || !locale[1])
        return 0;
    char a = (char)(locale[0] | 0x20), b = (char)(locale[1] | 0x20);
    for (UINT32 i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (map[i].lang[0] == a && map[i].lang[1] == b && (!locale[2] || locale[2] == '-' || locale[2] == '_'))
            return map[i].zero;
    return 0;
}

UINT32 DwNumberDigitZero(DWRITE_NUMBER_SUBSTITUTION_METHOD method, const WCHAR *locale, UINT32 context_script)
{
    UINT32 zero = language_digits(locale);
    switch (method) {
    case DWRITE_NUMBER_SUBSTITUTION_METHOD_NONE:
        return 0;
    case DWRITE_NUMBER_SUBSTITUTION_METHOD_NATIONAL:
    case DWRITE_NUMBER_SUBSTITUTION_METHOD_TRADITIONAL:
        return zero;
    default:
        /* by the script of the text before: arabic digits after arabic text, and the locale's after its own script. */
        if (context_script == DW_SCRIPT_ARAB)
            return zero == 0x06F0 ? zero : 0x0660;
        return 0;
    }
}

/* ------------------------------------------------------------------ the analyzer object */

static HRESULT WINAPI an_query(IDWriteTextAnalyzer2 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteTextAnalyzer) ||
        IsEqualGUID(riid, &IID_IDWriteTextAnalyzer1) || IsEqualGUID(riid, &IID_IDWriteTextAnalyzer2)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI an_addref(IDWriteTextAnalyzer2 *This) { return InterlockedIncrement(&((DwAnalyzer *)This)->refs); }

static ULONG WINAPI an_release(IDWriteTextAnalyzer2 *This)
{
    DwAnalyzer *a = (DwAnalyzer *)This;
    ULONG refs = InterlockedDecrement(&a->refs);
    if (!refs)
        DwFree(a);
    return refs;
}

/* the text of a range read through an analysis source, in one block. */
static HRESULT source_text(IDWriteTextAnalysisSource *source, UINT32 position, UINT32 length, WCHAR **out)
{
    WCHAR *text = DwAlloc(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (!text)
        return E_OUTOFMEMORY;
    UINT32 have = 0;
    while (have < length) {
        const WCHAR *chunk = nullptr;
        UINT32 n = 0;
        HRESULT hr = source->lpVtbl->GetTextAtPosition(source, position + have, &chunk, &n);
        if (FAILED(hr)) {
            DwFree(text);
            return hr;
        }
        if (!n || !chunk) {
            DwFree(text);
            return E_INVALIDARG;
        }
        if (n > length - have)
            n = length - have;
        memcpy(text + have, chunk, n * sizeof(WCHAR));
        have += n;
    }
    text[length] = 0;
    *out = text;
    return S_OK;
}

/* the script of each code unit: neutral characters take the script before them, or the one after them at the start. */
void DwResolveScripts(const WCHAR *text, UINT32 length, UINT16 *script, UINT8 *shapes)
{
    for (UINT32 i = 0; i < length;) {
        UINT32 units;
        UINT32 raw = DwCharScript(DwCodePointAt(text, length, i, &units));
        for (UINT32 u = 0; u < units; u++) {
            script[i + u] = (UINT16)(raw & 0xFF);
            shapes[i + u] = (UINT8)(raw >> 8);
        }
        i += units;
    }
    UINT16 last = 0;
    for (UINT32 i = 0; i < length; i++) {
        if (shapes[i]) {
            last = 0;
        } else if (script[i] == 0) {
            script[i] = last;
        } else {
            last = script[i];
        }
    }
    UINT16 next = 0;
    for (UINT32 i = length; i > 0; i--) {
        UINT32 k = i - 1;
        if (shapes[k])
            next = 0;
        else if (script[k] == 0)
            script[k] = next;
        else
            next = script[k];
    }
}

static HRESULT WINAPI an_script(IDWriteTextAnalyzer2 *This, IDWriteTextAnalysisSource *source, UINT32 position,
                                UINT32 length, IDWriteTextAnalysisSink *sink)
{
    if (!source || !sink)
        return E_INVALIDARG;
    if (!length)
        return S_OK;
    WCHAR *text;
    HRESULT hr = source_text(source, position, length, &text);
    if (FAILED(hr))
        return hr;
    UINT16 *script = DwAlloc(length * sizeof(UINT16));
    UINT8 *shapes = DwAlloc(length);
    if (!script || !shapes) {
        DwFree(text);
        DwFree(script);
        DwFree(shapes);
        return E_OUTOFMEMORY;
    }
    DwResolveScripts(text, length, script, shapes);
    for (UINT32 i = 0; i < length;) {
        UINT32 j = i + 1;
        while (j < length && script[j] == script[i] && shapes[j] == shapes[i])
            j++;
        DWRITE_SCRIPT_ANALYSIS sa = {script[i],
                                     shapes[i] ? DWRITE_SCRIPT_SHAPES_NO_VISUAL : DWRITE_SCRIPT_SHAPES_DEFAULT};
        hr = sink->lpVtbl->SetScriptAnalysis(sink, position + i, j - i, &sa);
        if (FAILED(hr))
            break;
        i = j;
    }
    DwFree(text);
    DwFree(script);
    DwFree(shapes);
    return hr;
}

static HRESULT WINAPI an_bidi(IDWriteTextAnalyzer2 *This, IDWriteTextAnalysisSource *source, UINT32 position,
                              UINT32 length, IDWriteTextAnalysisSink *sink)
{
    if (!source || !sink)
        return E_INVALIDARG;
    if (!length)
        return S_OK;
    WCHAR *text;
    HRESULT hr = source_text(source, position, length, &text);
    if (FAILED(hr))
        return hr;
    DWRITE_READING_DIRECTION dir = source->lpVtbl->GetParagraphReadingDirection(source);
    UINT8 *explicit_levels = DwAlloc(length);
    UINT8 *resolved = DwAlloc(length);
    if (!explicit_levels || !resolved) {
        DwFree(text);
        DwFree(explicit_levels);
        DwFree(resolved);
        return E_OUTOFMEMORY;
    }
    DwBidiLevels(text, length, dir == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT ? 1 : 0, explicit_levels, resolved);
    for (UINT32 i = 0; i < length;) {
        UINT32 j = i + 1;
        while (j < length && explicit_levels[j] == explicit_levels[i] && resolved[j] == resolved[i])
            j++;
        hr = sink->lpVtbl->SetBidiLevel(sink, position + i, j - i, explicit_levels[i], resolved[i]);
        if (FAILED(hr))
            break;
        i = j;
    }
    DwFree(text);
    DwFree(explicit_levels);
    DwFree(resolved);
    return hr;
}

static BOOL is_digit(WCHAR c)
{
    return c >= '0' && c <= '9';
}

/* the end of the number that starts at a digit: digits, a separator between digits and a percent sign after them. */
static UINT32 number_end(const WCHAR *text, UINT32 from, UINT32 to)
{
    UINT32 i = from;
    while (i < to) {
        WCHAR c = text[i];
        if (is_digit(c) || (c == '%' && i > from && is_digit(text[i - 1])))
            i++;
        else if ((c == ',' || c == '.') && i > from && is_digit(text[i - 1]) && i + 1 < to && is_digit(text[i + 1]))
            i++;
        else
            break;
    }
    return i;
}

static BOOL arabic_script(WCHAR c)
{
    return (c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F) || (c >= 0xFB50 && c <= 0xFDFF) ||
           (c >= 0xFE70 && c <= 0xFEFF);
}

/*
 * a substitution that is none, or not given, never reaches the sink. otherwise the sink hears of each number in
 * the range. a contextual substitution only applies to the numbers that follow text of the arabic script, the
 * others to every number.
 */
static HRESULT WINAPI an_numbers(IDWriteTextAnalyzer2 *This, IDWriteTextAnalysisSource *source, UINT32 position,
                                 UINT32 length, IDWriteTextAnalysisSink *sink)
{
    if (!source || !sink)
        return E_INVALIDARG;
    if (!length)
        return S_OK;
    WCHAR *text;
    HRESULT hr = source_text(source, position, length, &text);
    if (FAILED(hr))
        return hr;
    BOOL arabic = FALSE;
    UINT32 done = 0;
    while (done < length) {
        UINT32 n = 0;
        IDWriteNumberSubstitution *sub = nullptr;
        hr = source->lpVtbl->GetNumberSubstitution(source, position + done, &n, &sub);
        if (FAILED(hr))
            break;
        if (!n) {
            hr = E_INVALIDARG;
            if (sub)
                sub->lpVtbl->Release(sub);
            break;
        }
        if (n > length - done)
            n = length - done;
        DWRITE_NUMBER_SUBSTITUTION_METHOD method = DWRITE_NUMBER_SUBSTITUTION_METHOD_NONE;
        const WCHAR *locale;
        BOOL active = sub && DwNumberSubstitutionInfo(sub, &method, &locale) &&
                      method != DWRITE_NUMBER_SUBSTITUTION_METHOD_NONE;
        BOOL contextual = method == DWRITE_NUMBER_SUBSTITUTION_METHOD_FROM_CULTURE ||
                          method == DWRITE_NUMBER_SUBSTITUTION_METHOD_CONTEXTUAL;
        for (UINT32 i = done; i < done + n && SUCCEEDED(hr);) {
            if (is_digit(text[i])) {
                UINT32 end = number_end(text, i, done + n);
                if (active && (!contextual || arabic))
                    hr = sink->lpVtbl->SetNumberSubstitution(sink, position + i, end - i, sub);
                i = end;
                continue;
            }
            if (arabic_script(text[i]))
                arabic = TRUE;
            else if ((text[i] | 0x20) >= 'a' && (text[i] | 0x20) <= 'z')
                arabic = FALSE;
            i++;
        }
        if (sub)
            sub->lpVtbl->Release(sub);
        if (FAILED(hr))
            break;
        done += n;
    }
    DwFree(text);
    return hr;
}

static HRESULT WINAPI an_breaks(IDWriteTextAnalyzer2 *This, IDWriteTextAnalysisSource *source, UINT32 position,
                                UINT32 length, IDWriteTextAnalysisSink *sink)
{
    if (!source || !sink)
        return E_INVALIDARG;
    if (!length)
        return S_OK;
    WCHAR *text;
    HRESULT hr = source_text(source, position, length, &text);
    if (FAILED(hr))
        return hr;
    UINT8 *breaks = DwAlloc(length);
    DWRITE_LINE_BREAKPOINT *points = DwAllocZero(length * sizeof(DWRITE_LINE_BREAKPOINT));
    if (!breaks || !points) {
        DwFree(text);
        DwFree(breaks);
        DwFree(points);
        return E_OUTOFMEMORY;
    }
    DwLineBreaks(text, length, breaks);
    for (UINT32 i = 0; i < length; i++) {
        UINT8 gap = breaks[i];
        DWRITE_BREAK_CONDITION cond = gap == 2   ? DWRITE_BREAK_CONDITION_MUST_BREAK
                                      : gap == 1 ? DWRITE_BREAK_CONDITION_CAN_BREAK
                                                 : DWRITE_BREAK_CONDITION_MAY_NOT_BREAK;
        if (i + 1 == length && gap == 0)
            cond = DWRITE_BREAK_CONDITION_CAN_BREAK;
        points[i].breakConditionAfter = cond;
        if (i + 1 < length)
            points[i + 1].breakConditionBefore = cond;
    }
    points[0].breakConditionBefore = DWRITE_BREAK_CONDITION_MAY_NOT_BREAK;
    for (UINT32 i = 0; i < length;) {
        UINT32 units;
        UINT32 cp = DwCodePointAt(text, length, i, &units);
        for (UINT32 u = 0; u < units; u++) {
            points[i + u].isWhitespace = DwIsWhitespace(cp);
            points[i + u].isSoftHyphen = cp == 0xAD;
        }
        i += units;
    }
    hr = sink->lpVtbl->SetLineBreakpoints(sink, position, length, points);
    DwFree(text);
    DwFree(breaks);
    DwFree(points);
    return hr;
}

/* ------------------------------------------------------------------ shaping */

static BOOL gather_features(UINT32 text_length, const DWRITE_TYPOGRAPHIC_FEATURES **features,
                            const UINT32 *range_lengths, UINT32 range_count, DwFeatureRange **out, UINT32 *count)
{
    *out = nullptr;
    *count = 0;
    if (!range_count || !features)
        return TRUE;
    UINT32 total = 0;
    for (UINT32 r = 0; r < range_count; r++)
        total += features[r] ? features[r]->featureCount : 0;
    if (!total)
        return TRUE;
    DwFeatureRange *list = DwAlloc(total * sizeof(DwFeatureRange));
    if (!list)
        return FALSE;
    UINT32 start = 0, n = 0;
    for (UINT32 r = 0; r < range_count; r++) {
        UINT32 len = range_lengths ? range_lengths[r] : text_length;
        if (features[r]) {
            for (UINT32 f = 0; f < features[r]->featureCount; f++) {
                list[n].tag = features[r]->features[f].nameTag;
                list[n].value = features[r]->features[f].parameter;
                list[n].start = start;
                list[n].end = start + len;
                n++;
            }
        }
        start += len;
    }
    *out = list;
    *count = n;
    return TRUE;
}

static HRESULT WINAPI an_glyphs(IDWriteTextAnalyzer2 *This, const WCHAR *text, UINT32 length, IDWriteFontFace *face,
                                BOOL sideways, BOOL rtl, const DWRITE_SCRIPT_ANALYSIS *analysis, const WCHAR *locale,
                                IDWriteNumberSubstitution *substitution, const DWRITE_TYPOGRAPHIC_FEATURES **features,
                                const UINT32 *range_lengths, UINT32 range_count, UINT32 max_glyphs, UINT16 *cluster_map,
                                DWRITE_SHAPING_TEXT_PROPERTIES *text_props, UINT16 *glyph_indices,
                                DWRITE_SHAPING_GLYPH_PROPERTIES *glyph_props, UINT32 *actual)
{
    if (!text || !length || !face || !analysis || !cluster_map || !text_props || !glyph_indices || !glyph_props ||
        !actual)
        return E_INVALIDARG;
    if (analysis->script >= DwScriptCount())
        return E_INVALIDARG;
    DwOtFont *font = DwOtOpen(face);
    if (!font)
        return E_OUTOFMEMORY;
    DwFeatureRange *ranges;
    UINT32 nranges;
    if (!gather_features(length, features, range_lengths, range_count, &ranges, &nranges)) {
        DwOtClose(font);
        return E_OUTOFMEMORY;
    }
    UINT32 zero = 0;
    if (substitution) {
        DWRITE_NUMBER_SUBSTITUTION_METHOD method;
        const WCHAR *sub_locale;
        if (DwNumberSubstitutionInfo(substitution, &method, &sub_locale))
            zero = DwNumberDigitZero(method, sub_locale[0] ? sub_locale : locale, DW_SCRIPT_ZZZZ);
    }
    DwShapeInput in = {text,     length, font,   analysis->script, analysis->shapes != 0,  rtl,
                       sideways, locale, ranges, nranges,          zero ? &zero : nullptr, nranges > 0};
    DwShaped shaped;
    HRESULT hr = DwShape(&in, &shaped);
    DwFree(ranges);
    if (SUCCEEDED(hr)) {
        *actual = shaped.glyph_count;
        if (shaped.glyph_count > max_glyphs) {
            hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        } else {
            memcpy(glyph_indices, shaped.glyphs, shaped.glyph_count * sizeof(UINT16));
            memcpy(cluster_map, shaped.cluster_map, length * sizeof(UINT16));
            for (UINT32 i = 0; i < length; i++) {
                memset(&text_props[i], 0, sizeof(text_props[i]));
                text_props[i].isShapedAlone = shaped.text_flags[i] & 1;
                text_props[i].canBreakShapingAfter = (shaped.text_flags[i] >> 2) & 1;
            }
            for (UINT32 i = 0; i < shaped.glyph_count; i++) {
                memset(&glyph_props[i], 0, sizeof(glyph_props[i]));
                glyph_props[i].justification = shaped.glyph_flags[i] & 15;
                glyph_props[i].isClusterStart = (shaped.glyph_flags[i] >> 4) & 1;
                glyph_props[i].isDiacritic = (shaped.glyph_flags[i] >> 5) & 1;
                glyph_props[i].isZeroWidthSpace = (shaped.glyph_flags[i] >> 6) & 1;
            }
        }
        DwShapedFree(&shaped);
    }
    DwOtClose(font);
    return hr;
}

/* the advances and offsets of glyphs, shaped again from the text so positioning rules apply. */
static HRESULT place(const WCHAR *text, UINT32 length, const UINT16 *glyph_indices, UINT32 glyph_count,
                     IDWriteFontFace *face, FLOAT em_size, BOOL sideways, BOOL rtl,
                     const DWRITE_SCRIPT_ANALYSIS *analysis, const WCHAR *locale,
                     const DWRITE_TYPOGRAPHIC_FEATURES **features, const UINT32 *range_lengths, UINT32 range_count,
                     BOOL gdi, FLOAT pixels_per_dip, BOOL natural, FLOAT *advances, DWRITE_GLYPH_OFFSET *offsets)
{
    DwOtFont *font = DwOtOpen(face);
    if (!font)
        return E_OUTOFMEMORY;
    DwFeatureRange *ranges;
    UINT32 nranges;
    if (!gather_features(length, features, range_lengths, range_count, &ranges, &nranges)) {
        DwOtClose(font);
        return E_OUTOFMEMORY;
    }
    DwShapeInput in = {text,   length,  font,    analysis->script, analysis->shapes != 0, rtl, sideways, locale,
                       ranges, nranges, nullptr, nranges > 0};
    DwShaped shaped;
    BOOL have = SUCCEEDED(DwShape(&in, &shaped)) && shaped.glyph_count == glyph_count &&
                !memcmp(shaped.glyphs, glyph_indices, glyph_count * sizeof(UINT16));
    FLOAT scale = em_size / (FLOAT)DwOtUnitsPerEm(font);
    for (UINT32 i = 0; i < glyph_count; i++) {
        INT32 adv = have ? shaped.advances[i] : DwOtAdvance(font, glyph_indices[i]);
        FLOAT w = (FLOAT)adv * scale;
        if (gdi && !natural && pixels_per_dip > 0) {
            FLOAT px = w * pixels_per_dip;
            px = px >= 0 ? (FLOAT)(INT32)(px + 0.5f) : (FLOAT)(INT32)(px - 0.5f);
            w = px / pixels_per_dip;
        }
        advances[i] = w;
        offsets[i].advanceOffset = have ? (FLOAT)shaped.offset_x[i] * scale : 0;
        offsets[i].ascenderOffset = have ? (FLOAT)shaped.offset_y[i] * scale : 0;
    }
    if (have)
        DwShapedFree(&shaped);
    DwFree(ranges);
    DwOtClose(font);
    return S_OK;
}

static HRESULT WINAPI an_placements(IDWriteTextAnalyzer2 *This, const WCHAR *text, const UINT16 *cluster_map,
                                    DWRITE_SHAPING_TEXT_PROPERTIES *text_props, UINT32 length,
                                    const UINT16 *glyph_indices, const DWRITE_SHAPING_GLYPH_PROPERTIES *glyph_props,
                                    UINT32 glyph_count, IDWriteFontFace *face, FLOAT em_size, BOOL sideways, BOOL rtl,
                                    const DWRITE_SCRIPT_ANALYSIS *analysis, const WCHAR *locale,
                                    const DWRITE_TYPOGRAPHIC_FEATURES **features, const UINT32 *range_lengths,
                                    UINT32 range_count, FLOAT *advances, DWRITE_GLYPH_OFFSET *offsets)
{
    if (!text || !cluster_map || !text_props || !glyph_indices || !glyph_props || !face || !analysis || !advances ||
        !offsets || !length || !glyph_count || !(em_size > 0))
        return E_INVALIDARG;
    return place(text, length, glyph_indices, glyph_count, face, em_size, sideways, rtl, analysis, locale, features,
                 range_lengths, range_count, FALSE, 1.0f, TRUE, advances, offsets);
}

static HRESULT WINAPI an_gdi_placements(IDWriteTextAnalyzer2 *This, const WCHAR *text, const UINT16 *cluster_map,
                                        DWRITE_SHAPING_TEXT_PROPERTIES *text_props, UINT32 length,
                                        const UINT16 *glyph_indices, const DWRITE_SHAPING_GLYPH_PROPERTIES *glyph_props,
                                        UINT32 glyph_count, IDWriteFontFace *face, FLOAT em_size, FLOAT pixels_per_dip,
                                        const DWRITE_MATRIX *transform, BOOL use_natural, BOOL sideways, BOOL rtl,
                                        const DWRITE_SCRIPT_ANALYSIS *analysis, const WCHAR *locale,
                                        const DWRITE_TYPOGRAPHIC_FEATURES **features, const UINT32 *range_lengths,
                                        UINT32 range_count, FLOAT *advances, DWRITE_GLYPH_OFFSET *offsets)
{
    if (!text || !cluster_map || !text_props || !glyph_indices || !glyph_props || !face || !analysis || !advances ||
        !offsets || !length || !glyph_count || !(em_size > 0) || !(pixels_per_dip > 0))
        return E_INVALIDARG;
    return place(text, length, glyph_indices, glyph_count, face, em_size, sideways, rtl, analysis, locale, features,
                 range_lengths, range_count, TRUE, pixels_per_dip, use_natural, advances, offsets);
}

/* ------------------------------------------------------------------ analyzer 1 */

static HRESULT WINAPI an_spacing(IDWriteTextAnalyzer2 *This, FLOAT leading, FLOAT trailing, FLOAT min_advance,
                                 UINT32 length, UINT32 glyph_count, const UINT16 *cluster_map, const FLOAT *advances,
                                 const DWRITE_GLYPH_OFFSET *offsets, const DWRITE_SHAPING_GLYPH_PROPERTIES *props,
                                 FLOAT *new_advances, DWRITE_GLYPH_OFFSET *new_offsets)
{
    if (!cluster_map || !advances || !offsets || !props || !new_advances || !new_offsets || !(min_advance >= 0))
        return E_INVALIDARG;
    UINT32 i = 0;
    while (i < glyph_count) {
        /* a cluster runs to the next glyph that starts one. */
        UINT32 j = i + 1;
        while (j < glyph_count && !props[j].isClusterStart)
            j++;
        for (UINT32 k = i; k < j; k++) {
            new_advances[k] = advances[k];
            new_offsets[k] = offsets[k];
        }
        new_advances[i] += leading;
        new_offsets[i].advanceOffset += leading;
        new_advances[j - 1] += trailing;
        FLOAT total = 0;
        for (UINT32 k = i; k < j; k++)
            total += new_advances[k];
        if (total < min_advance) {
            FLOAT extra = min_advance - total;
            new_advances[j - 1] += extra / 2;
            new_advances[i] += extra / 2;
            new_offsets[i].advanceOffset += extra / 2;
        }
        i = j;
    }
    return S_OK;
}

static HRESULT WINAPI an_baseline(IDWriteTextAnalyzer2 *This, IDWriteFontFace *face, DWRITE_BASELINE baseline,
                                  BOOL vertical, BOOL simulation, DWRITE_SCRIPT_ANALYSIS sa, const WCHAR *locale,
                                  INT32 *coord, BOOL *exists)
{
    if (!face || !coord || !exists)
        return E_INVALIDARG;
    DWRITE_FONT_METRICS m;
    face->lpVtbl->GetMetrics(face, &m);
    *coord = 0;
    *exists = FALSE;
    switch (baseline) {
    case DWRITE_BASELINE_DEFAULT:
    case DWRITE_BASELINE_ROMAN:
        *exists = !vertical;
        if (vertical && simulation) {
            *coord = (m.ascent + m.descent) / 2;
            *exists = TRUE;
        }
        return S_OK;
    case DWRITE_BASELINE_CENTRAL:
        if (simulation) {
            *coord = vertical ? 0 : (INT32)(m.capHeight / 2);
            *exists = TRUE;
        }
        return S_OK;
    case DWRITE_BASELINE_MATH:
        if (simulation) {
            *coord = (INT32)(m.xHeight ? m.xHeight / 2 + 0 : m.capHeight / 2);
            *exists = TRUE;
        }
        return S_OK;
    case DWRITE_BASELINE_MINIMUM:
        *coord = -(INT32)m.descent;
        *exists = TRUE;
        return S_OK;
    case DWRITE_BASELINE_MAXIMUM:
        *coord = (INT32)m.ascent;
        *exists = TRUE;
        return S_OK;
    default:
        if (simulation) {
            *coord = 0;
            *exists = TRUE;
        }
        return S_OK;
    }
}

static BOOL upright_script(UINT32 script)
{
    return script == DW_SCRIPT_HANI || script == DW_SCRIPT_HIRA || script == DW_SCRIPT_KANA ||
           script == DW_SCRIPT_HANG || script == DW_SCRIPT_BOPO || script == DW_SCRIPT_YIII;
}

static HRESULT WINAPI an_vertical(IDWriteTextAnalyzer2 *This, IDWriteTextAnalysisSource1 *source, UINT32 position,
                                  UINT32 length, IDWriteTextAnalysisSink1 *sink)
{
    if (!source || !sink)
        return E_INVALIDARG;
    UINT32 done = 0;
    while (done < length) {
        UINT32 n = 0;
        DWRITE_VERTICAL_GLYPH_ORIENTATION orientation = DWRITE_VERTICAL_GLYPH_ORIENTATION_DEFAULT;
        UINT8 level = 0;
        HRESULT hr = source->lpVtbl->GetVerticalGlyphOrientation(source, position + done, &n, &orientation, &level);
        if (FAILED(hr))
            return hr;
        if (!n)
            return E_INVALIDARG;
        if (n > length - done)
            n = length - done;
        const WCHAR *text = nullptr;
        UINT32 avail = 0;
        BOOL upright = orientation == DWRITE_VERTICAL_GLYPH_ORIENTATION_STACKED;
        if (!upright && SUCCEEDED(source->lpVtbl->GetTextAtPosition(source, position + done, &text, &avail)) && text &&
            avail) {
            UINT32 units;
            upright = upright_script(DwCharScript(DwCodePointAt(text, avail, 0, &units)) & 0xFF);
        }
        hr = sink->lpVtbl->SetGlyphOrientation(sink, position + done, n,
                                               upright ? DWRITE_GLYPH_ORIENTATION_ANGLE_0_DEGREES
                                                       : DWRITE_GLYPH_ORIENTATION_ANGLE_90_DEGREES,
                                               level, !upright, (level & 1) != 0);
        if (FAILED(hr))
            return hr;
        done += n;
    }
    return S_OK;
}

static HRESULT orientation_matrix(DWRITE_GLYPH_ORIENTATION_ANGLE angle, BOOL sideways, FLOAT x, FLOAT y,
                                  DWRITE_MATRIX *m)
{
    if (!m || (unsigned)angle > DWRITE_GLYPH_ORIENTATION_ANGLE_270_DEGREES)
        return E_INVALIDARG;
    switch (angle) {
    case DWRITE_GLYPH_ORIENTATION_ANGLE_0_DEGREES:
        *m = (DWRITE_MATRIX){1, 0, 0, 1, 0, 0};
        break;
    case DWRITE_GLYPH_ORIENTATION_ANGLE_90_DEGREES:
        *m = (DWRITE_MATRIX){0, 1, -1, 0, x + y, y - x};
        break;
    case DWRITE_GLYPH_ORIENTATION_ANGLE_180_DEGREES:
        *m = (DWRITE_MATRIX){-1, 0, 0, -1, x + x, y + y};
        break;
    default:
        *m = (DWRITE_MATRIX){0, -1, 1, 0, x - y, x + y};
        break;
    }
    return S_OK;
}

static HRESULT WINAPI an_orientation(IDWriteTextAnalyzer2 *This, DWRITE_GLYPH_ORIENTATION_ANGLE angle, BOOL sideways,
                                     DWRITE_MATRIX *transform)
{
    return orientation_matrix(angle, sideways, 0, 0, transform);
}

static HRESULT WINAPI an_script_props(IDWriteTextAnalyzer2 *This, DWRITE_SCRIPT_ANALYSIS sa,
                                      DWRITE_SCRIPT_PROPERTIES *props)
{
    if (!props)
        return E_INVALIDARG;
    if (!DwScriptProperties(sa.script, props))
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT WINAPI an_complexity(IDWriteTextAnalyzer2 *This, const WCHAR *text, UINT32 length, IDWriteFontFace *face,
                                    BOOL *is_simple, UINT32 *length_read, UINT16 *indices)
{
    if (!text || !face || !is_simple || !length_read)
        return E_INVALIDARG;
    *is_simple = TRUE;
    UINT32 read = 0;
    while (read < length) {
        UINT32 units;
        UINT32 cp = DwCodePointAt(text, length, read, &units);
        UINT8 gcb = DwGraphemeClass(cp) & 0x3F;
        UINT8 bidi = DwBidiClass(cp);
        UINT32 script = DwCharScript(cp) & 0xFF;
        if (gcb == DW_GCB_EXTEND || gcb == DW_GCB_ZWJ || gcb == DW_GCB_SPACINGMARK || gcb == DW_GCB_PREPEND ||
            bidi == DW_BIDI_R || bidi == DW_BIDI_AL || script == DW_SCRIPT_ARAB || script == DW_SCRIPT_DEVA ||
            script == DW_SCRIPT_BENG || script == DW_SCRIPT_THAI || cp >= 0x10000) {
            *is_simple = FALSE;
            break;
        }
        UINT16 g = 0;
        face->lpVtbl->GetGlyphIndices(face, &cp, 1, &g);
        if (!g) {
            *is_simple = FALSE;
            break;
        }
        if (indices)
            indices[read] = g;
        read += units;
    }
    *length_read = read;
    return S_OK;
}

static HRESULT WINAPI an_just_opp(IDWriteTextAnalyzer2 *This, IDWriteFontFace *face, FLOAT em_size,
                                  DWRITE_SCRIPT_ANALYSIS sa, UINT32 length, UINT32 glyph_count, const WCHAR *text,
                                  const UINT16 *cluster_map, const DWRITE_SHAPING_GLYPH_PROPERTIES *props,
                                  DWRITE_JUSTIFICATION_OPPORTUNITY *ops)
{
    if (!face || !text || !cluster_map || !props || !ops || !(em_size > 0))
        return E_INVALIDARG;
    for (UINT32 i = 0; i < glyph_count; i++) {
        memset(&ops[i], 0, sizeof(ops[i]));
        if (props[i].justification == 4) {
            ops[i].expansionMinimum = 0;
            ops[i].expansionMaximum = em_size * 100.0f;
            ops[i].compressionMaximum = 0;
            ops[i].expansionPriority = 1;
            ops[i].compressionPriority = 1;
            ops[i].allowResidualExpansion = 1;
            ops[i].applyToTrailingEdge = 1;
        }
    }
    return S_OK;
}

static HRESULT WINAPI an_justify(IDWriteTextAnalyzer2 *This, FLOAT line_width, UINT32 glyph_count,
                                 const DWRITE_JUSTIFICATION_OPPORTUNITY *ops, const FLOAT *advances,
                                 const DWRITE_GLYPH_OFFSET *offsets, FLOAT *new_advances,
                                 DWRITE_GLYPH_OFFSET *new_offsets)
{
    if (!ops || !advances || !offsets || !new_advances || !new_offsets)
        return E_INVALIDARG;
    FLOAT sum = 0;
    UINT32 slots = 0;
    for (UINT32 i = 0; i < glyph_count; i++) {
        sum += advances[i];
        if (ops[i].expansionMaximum > 0)
            slots++;
    }
    FLOAT extra = line_width - sum;
    for (UINT32 i = 0; i < glyph_count; i++) {
        new_advances[i] = advances[i];
        new_offsets[i] = offsets[i];
        if (slots && extra > 0 && ops[i].expansionMaximum > 0) {
            FLOAT add = extra / (FLOAT)slots;
            if (add > ops[i].expansionMaximum)
                add = ops[i].expansionMaximum;
            new_advances[i] += add;
        }
    }
    return S_OK;
}

static HRESULT WINAPI an_justified_glyphs(IDWriteTextAnalyzer2 *This, IDWriteFontFace *face, FLOAT em_size,
                                          DWRITE_SCRIPT_ANALYSIS sa, UINT32 length, UINT32 glyph_count,
                                          UINT32 max_glyphs, const UINT16 *cluster_map, const UINT16 *indices,
                                          const FLOAT *advances, const FLOAT *justified_advances,
                                          const DWRITE_GLYPH_OFFSET *justified_offsets,
                                          const DWRITE_SHAPING_GLYPH_PROPERTIES *props, UINT32 *actual_count,
                                          UINT16 *modified_cluster_map, UINT16 *modified_indices,
                                          FLOAT *modified_advances, DWRITE_GLYPH_OFFSET *modified_offsets)
{
    if (!face || !cluster_map || !indices || !advances || !justified_advances || !justified_offsets || !props ||
        !actual_count)
        return E_INVALIDARG;
    *actual_count = glyph_count;
    if (glyph_count > max_glyphs)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    if (modified_cluster_map)
        memcpy(modified_cluster_map, cluster_map, length * sizeof(UINT16));
    if (modified_indices)
        memcpy(modified_indices, indices, glyph_count * sizeof(UINT16));
    if (modified_advances)
        memcpy(modified_advances, justified_advances, glyph_count * sizeof(FLOAT));
    if (modified_offsets)
        memcpy(modified_offsets, justified_offsets, glyph_count * sizeof(DWRITE_GLYPH_OFFSET));
    return S_OK;
}

/* ------------------------------------------------------------------ analyzer 2 */

static HRESULT WINAPI an2_orientation(IDWriteTextAnalyzer2 *This, DWRITE_GLYPH_ORIENTATION_ANGLE angle, BOOL sideways,
                                      FLOAT origin_x, FLOAT origin_y, DWRITE_MATRIX *transform)
{
    return orientation_matrix(angle, sideways, origin_x, origin_y, transform);
}

/* the feature tags of the script's default language, from both tables. */
static UINT32 table_features(IDWriteFontFace *face, UINT32 table_tag, DWRITE_SCRIPT_ANALYSIS sa, UINT32 *tags,
                             UINT32 max)
{
    const void *data;
    UINT32 size;
    void *ctx;
    BOOL exists;
    UINT32 count = 0;
    if (FAILED(face->lpVtbl->TryGetFontTable(face, table_tag, &data, &size, &ctx, &exists)) || !exists)
        return 0;
    const BYTE *t = data;
    if (size >= 10) {
        UINT32 feature_list = DwBe16(t + 6);
        UINT32 n = feature_list + 2 <= size ? DwBe16(t + feature_list) : 0;
        for (UINT32 i = 0; i < n && feature_list + 2 + i * 6 + 4 <= size; i++) {
            UINT32 tag = DwBe32(t + feature_list + 2 + i * 6);
            UINT32 swapped = DwTagSwap(tag);
            UINT32 j;
            for (j = 0; j < count; j++)
                if (tags[j] == swapped)
                    break;
            if (j == count && count < max)
                tags[count++] = swapped;
        }
    }
    face->lpVtbl->ReleaseFontTable(face, ctx);
    return count;
}

static HRESULT WINAPI an2_features(IDWriteTextAnalyzer2 *This, IDWriteFontFace *face, DWRITE_SCRIPT_ANALYSIS sa,
                                   const WCHAR *locale, UINT32 max_tags, UINT32 *actual, DWRITE_FONT_FEATURE_TAG *tags)
{
    if (!face || !actual)
        return E_INVALIDARG;
    UINT32 all[128];
    UINT32 n = table_features(face, DW_TAG_LE('G', 'S', 'U', 'B'), sa, all, 128);
    UINT32 m = table_features(face, DW_TAG_LE('G', 'P', 'O', 'S'), sa, all + n, 128 - n);
    UINT32 total = n;
    for (UINT32 i = 0; i < m; i++) {
        UINT32 j;
        for (j = 0; j < n; j++)
            if (all[j] == all[n + i])
                break;
        if (j == n)
            all[total++] = all[n + i];
    }
    *actual = total;
    if (total > max_tags)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    for (UINT32 i = 0; i < total; i++)
        tags[i] = (DWRITE_FONT_FEATURE_TAG)all[i];
    return S_OK;
}

static HRESULT WINAPI an2_check(IDWriteTextAnalyzer2 *This, IDWriteFontFace *face, DWRITE_SCRIPT_ANALYSIS sa,
                                const WCHAR *locale, DWRITE_FONT_FEATURE_TAG feature, UINT32 glyph_count,
                                const UINT16 *indices, UINT8 *applies)
{
    if (!face || !indices || !applies)
        return E_INVALIDARG;
    UINT32 all[128];
    UINT32 n = table_features(face, DW_TAG_LE('G', 'S', 'U', 'B'), sa, all, 128);
    UINT32 m = table_features(face, DW_TAG_LE('G', 'P', 'O', 'S'), sa, all + n, 128 - n);
    BOOL present = FALSE;
    for (UINT32 i = 0; i < n + m; i++)
        if (all[i] == (UINT32)feature)
            present = TRUE;
    for (UINT32 i = 0; i < glyph_count; i++)
        applies[i] = present ? 1 : 0;
    return S_OK;
}

static const IDWriteTextAnalyzer2Vtbl g_analyzer_vtbl = {
    an_query,      an_addref,   an_release,  an_script,           an_bidi,
    an_numbers,    an_breaks,   an_glyphs,   an_placements,       an_gdi_placements,
    an_spacing,    an_baseline, an_vertical, an_orientation,      an_script_props,
    an_complexity, an_just_opp, an_justify,  an_justified_glyphs, an2_orientation,
    an2_features,  an2_check,
};

HRESULT DwAnalyzerCreate(DwFactory *factory, IDWriteTextAnalyzer **out)
{
    DwAnalyzer *a = DwAllocZero(sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->lpVtbl = &g_analyzer_vtbl;
    a->refs = 1;
    a->factory = factory;
    *out = (IDWriteTextAnalyzer *)a;
    return S_OK;
}
