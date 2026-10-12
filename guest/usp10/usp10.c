/*
 * uniscribe for left to right text and simple right to left runs: items by
 * script class and bidi level, one glyph per code point, widths from the
 * font. without contextual shaping, complex scripts render their nominal
 * glyphs.
 *
 * glyph ids come from gdi32's GetGlyphIndicesW and GetCharABCWidthsI when
 * gdi32 exports them. until it does, glyphs are the characters themselves,
 * reported with fNoGlyphIndex, which is also how mime's ExtTextOutW reads
 * ETO_GLYPH_INDEX text today.
 */
#define MIME_BUILD_USP10
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/usp10.h>

#ifndef ARABIC_CHARSET
#define HEBREW_CHARSET 177
#define ARABIC_CHARSET 178
#endif

/* the scripts of this implementation, indexes of g_props. */
enum {
    USP_UNDEFINED = SCRIPT_UNDEFINED,
    USP_LATIN = 1,
    USP_ARABIC = 2,
    USP_HEBREW = 3,
    USP_COMPLEX = 4,
    USP_DIGIT = 5,
    USP_SCRIPT_COUNT,
};

static const SCRIPT_PROPERTIES g_props[USP_SCRIPT_COUNT] = {
    {.langid = 0, .bCharSet = DEFAULT_CHARSET},
    {.langid = LANG_ENGLISH, .bCharSet = ANSI_CHARSET},
    {.langid = LANG_ARABIC, .fComplex = 1, .fNeedsCaretInfo = 1, .bCharSet = ARABIC_CHARSET, .fCDM = 1},
    {.langid = LANG_HEBREW, .fComplex = 1, .fNeedsCaretInfo = 1, .bCharSet = HEBREW_CHARSET, .fCDM = 1},
    {.langid = 0, .fComplex = 1, .fNeedsWordBreaking = 1, .fNeedsCaretInfo = 1, .bCharSet = DEFAULT_CHARSET,
     .fClusterSizeVaries = 1},
    {.langid = LANG_ENGLISH, .fNumeric = 1, .bCharSet = ANSI_CHARSET},
};
static const SCRIPT_PROPERTIES *g_prop_list[USP_SCRIPT_COUNT] = {&g_props[0], &g_props[1], &g_props[2],
                                                                  &g_props[3], &g_props[4], &g_props[5]};

typedef DWORD(WINAPI *GET_GLYPH_INDICES)(HDC dc, LPCWSTR text, int count, LPWORD glyphs, DWORD flags);
typedef BOOL(WINAPI *GET_ABC_I)(HDC dc, UINT first, UINT count, LPWORD glyphs, LPABC abc);

static struct {
    RTL_RUN_ONCE Once;
    GET_GLYPH_INDICES GetGlyphIndices;
    GET_ABC_I GetAbcI;
} g_gdi;

static ULONG NTAPI init_gdi(PRTL_RUN_ONCE once, PVOID param, PVOID *context)
{
    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    if (gdi) {
        g_gdi.GetGlyphIndices = (GET_GLYPH_INDICES)(void *)GetProcAddress(gdi, "GetGlyphIndicesW");
        g_gdi.GetAbcI = (GET_ABC_I)(void *)GetProcAddress(gdi, "GetCharABCWidthsI");
    }
    if (!g_gdi.GetGlyphIndices || !g_gdi.GetAbcI) {
        g_gdi.GetGlyphIndices = NULL;
        g_gdi.GetAbcI = NULL;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ characters */

static BOOL is_rtl_char(UINT c)
{
    return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF) ||
           (c >= 0x10800 && c <= 0x10FFF) || (c >= 0x1E800 && c <= 0x1EFFF);
}

static BOOL is_complex_ltr(UINT c)
{
    return (c >= 0x0900 && c <= 0x0DFF) || (c >= 0x0E00 && c <= 0x0FFF) || (c >= 0x1000 && c <= 0x109F) ||
           (c >= 0x1780 && c <= 0x18AF) || (c >= 0xA980 && c <= 0xAADF);
}

static BOOL is_mark(UINT c)
{
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
           (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F);
}

static BOOL is_zero_width(UINT c)
{
    return c < 0x20 || (c >= 0x7F && c < 0xA0) || (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) ||
           (c >= 0x2060 && c <= 0x2064) || c == 0xFEFF;
}

static BOOL is_space(UINT c)
{
    return c == ' ' || c == '\t' || c == 0xA0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200A);
}

static BOOL is_digit(UINT c)
{
    return c >= '0' && c <= '9';
}

/* neither strong left to right nor right to left: spaces, punctuation, symbols, digits, controls. */
static BOOL is_neutral(UINT c)
{
    if (c < 0x80)
        return !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
    return is_space(c) || is_zero_width(c) || (c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F) ||
           (c >= 0xFE30 && c <= 0xFE4F) || c == 0xA0 || (c >= 0xA1 && c <= 0xBF) || c == 0xD7 || c == 0xF7;
}

static int char_script(UINT c)
{
    if (c >= 0x0590 && c <= 0x05FF)
        return USP_HEBREW;
    if (is_rtl_char(c))
        return USP_ARABIC;
    if (is_complex_ltr(c))
        return USP_COMPLEX;
    return USP_LATIN;
}

/* reads the code point at *i, advancing past a surrogate pair. */
static UINT next_cp(const WCHAR *s, int count, int *i)
{
    UINT c = s[(*i)++];
    if (c >= 0xD800 && c <= 0xDBFF && *i < count && s[*i] >= 0xDC00 && s[*i] <= 0xDFFF)
        c = 0x10000 + ((c - 0xD800) << 10) + (s[(*i)++] - 0xDC00);
    return c;
}

USPAPI ScriptIsComplex(const WCHAR *chars, int count, DWORD flags)
{
    if (!chars || count < 0)
        return E_INVALIDARG;
    for (int i = 0; i < count;) {
        UINT c = next_cp(chars, count, &i);
        if (is_rtl_char(c) || is_complex_ltr(c) || is_mark(c))
            return S_OK;
        if ((flags & SIC_ASCIIDIGIT) && is_digit(c))
            return S_OK;
        if ((flags & SIC_NEUTRAL) && c >= 0x80 && is_neutral(c))
            return S_OK;
    }
    return S_FALSE;
}

/* ------------------------------------------------------------------ itemizing */

USPAPI ScriptItemize(const WCHAR *chars, int count, int max_items, const SCRIPT_CONTROL *control,
                     const SCRIPT_STATE *state, SCRIPT_ITEM *items, int *item_count)
{
    if (!chars || count <= 0 || !items || max_items < 2)
        return E_INVALIDARG;
    int base = state ? state->uBidiLevel : 0;
    int ltr_level = (base & 1) ? base + 1 : base, rtl_level = (base & 1) ? base : base + 1;
    int n = 0, cur_script = -1, cur_level = -1;
    /* neutrals take the run of the previous strong character, or of the first one at the start. */
    int first_script = USP_LATIN, first_level = (base & 1) ? rtl_level : ltr_level;
    for (int i = 0; i < count;) {
        UINT c = next_cp(chars, count, &i);
        if (!is_neutral(c)) {
            first_script = char_script(c);
            first_level = (first_script == USP_ARABIC || first_script == USP_HEBREW) ? rtl_level : ltr_level;
            break;
        }
    }
    for (int i = 0; i < count;) {
        int start = i;
        UINT c = next_cp(chars, count, &i);
        int script, level;
        if (is_neutral(c) || is_mark(c)) {
            script = cur_script >= 0 ? cur_script : first_script;
            level = cur_level >= 0 ? cur_level : first_level;
        } else {
            script = char_script(c);
            level = (script == USP_ARABIC || script == USP_HEBREW) ? rtl_level : ltr_level;
        }
        if (state && state->fOverrideDirection)
            level = base;
        if (script == cur_script && level == cur_level)
            continue;
        if (n >= max_items - 1)
            return E_OUTOFMEMORY;
        SCRIPT_ITEM *it = &items[n++];
        memset(it, 0, sizeof(*it));
        it->iCharPos = start;
        it->a.eScript = script;
        it->a.fRTL = level & 1;
        it->a.fLayoutRTL = level & 1;
        if (state)
            it->a.s = *state;
        it->a.s.uBidiLevel = level;
        cur_script = script;
        cur_level = level;
    }
    memset(&items[n], 0, sizeof(items[n]));
    items[n].iCharPos = count;
    if (item_count)
        *item_count = n;
    return S_OK;
}

/* ------------------------------------------------------------------ the font cache */

#define CACHE_MAGIC 0x50535555 /* "UUSP" */

typedef struct USP_CACHE {
    DWORD Magic;
    BOOL Real; /* glyph ids from gdi32, else characters. */
    TEXTMETRICW Metrics;
    WORD *Glyphs[256]; /* character to glyph id plus one, by page of 256. */
    ABC *Widths[256];  /* by glyph id. */
    BYTE *Known[256];
} USP_CACHE;

static void *usp_alloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, size);
}

static void usp_free(void *p)
{
    if (p)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
}

/* the cache of *cache, created from dc. E_PENDING when neither exists, like windows. */
static HRESULT get_cache(HDC dc, SCRIPT_CACHE *cache, USP_CACHE **out)
{
    if (!cache)
        return E_INVALIDARG;
    USP_CACHE *c = *cache;
    if (c) {
        if (c->Magic != CACHE_MAGIC)
            return E_INVALIDARG;
        *out = c;
        return S_OK;
    }
    if (!dc)
        return E_PENDING;
    RtlRunOnceExecuteOnce(&g_gdi.Once, init_gdi, NULL, NULL);
    c = usp_alloc(sizeof(*c));
    if (!c)
        return E_OUTOFMEMORY;
    c->Magic = CACHE_MAGIC;
    c->Real = g_gdi.GetGlyphIndices != NULL;
    if (!GetTextMetricsW(dc, &c->Metrics)) {
        usp_free(c);
        return E_INVALIDARG;
    }
    *cache = c;
    *out = c;
    return S_OK;
}

USPAPI ScriptFreeCache(SCRIPT_CACHE *cache)
{
    if (!cache)
        return E_INVALIDARG;
    USP_CACHE *c = *cache;
    if (c && c->Magic == CACHE_MAGIC) {
        for (int i = 0; i < 256; i++) {
            usp_free(c->Glyphs[i]);
            usp_free(c->Widths[i]);
            usp_free(c->Known[i]);
        }
        c->Magic = 0;
        usp_free(c);
    }
    *cache = NULL;
    return S_OK;
}

/* the glyph of a bmp character; 0xFFFF when unknown and dc is NULL, glyph 0 when the font lacks it. */
static WORD lookup_glyph(USP_CACHE *c, HDC dc, WCHAR ch, BOOL *missing)
{
    *missing = FALSE;
    if (!c->Real)
        return ch;
    WORD *page = c->Glyphs[ch >> 8];
    if (page && page[ch & 0xFF])
        return page[ch & 0xFF] - 1;
    if (!dc)
        return 0xFFFF;
    WORD g = 0xFFFF;
    if (g_gdi.GetGlyphIndices(dc, &ch, 1, &g, 1 /* GGI_MARK_NONEXISTING_GLYPHS */) == GDI_ERROR)
        g = 0xFFFF;
    if (g == 0xFFFF) {
        *missing = TRUE;
        g = 0;
    }
    if (!page)
        page = c->Glyphs[ch >> 8] = usp_alloc(256 * sizeof(WORD));
    if (page && !*missing)
        page[ch & 0xFF] = (WORD)(g + 1);
    return g;
}

/* the width of a glyph, a character when no_index; FALSE when unknown and dc is NULL. */
static BOOL lookup_abc(USP_CACHE *c, HDC dc, WORD glyph, BOOL no_index, ABC *abc)
{
    BYTE *known = c->Known[glyph >> 8];
    if (known && known[glyph & 0xFF]) {
        *abc = c->Widths[glyph >> 8][glyph & 0xFF];
        return TRUE;
    }
    if (!dc)
        return FALSE;
    BOOL ok;
    if (c->Real && !no_index) {
        ok = g_gdi.GetAbcI(dc, glyph, 1, NULL, abc);
    } else {
        ok = GetCharABCWidthsW(dc, glyph, glyph, abc);
        if (!ok) {
            INT w = 0;
            ok = GetCharWidth32W(dc, glyph, glyph, &w);
            abc->abcA = 0;
            abc->abcB = (UINT)w;
            abc->abcC = 0;
        }
    }
    if (!ok)
        return FALSE;
    if (!c->Known[glyph >> 8]) {
        c->Known[glyph >> 8] = usp_alloc(256);
        c->Widths[glyph >> 8] = usp_alloc(256 * sizeof(ABC));
        if (!c->Known[glyph >> 8] || !c->Widths[glyph >> 8]) {
            usp_free(c->Known[glyph >> 8]);
            usp_free(c->Widths[glyph >> 8]);
            c->Known[glyph >> 8] = NULL;
            c->Widths[glyph >> 8] = NULL;
            return TRUE;
        }
    }
    c->Widths[glyph >> 8][glyph & 0xFF] = *abc;
    c->Known[glyph >> 8][glyph & 0xFF] = 1;
    return TRUE;
}

/* ------------------------------------------------------------------ shaping and placing */

USPAPI ScriptShape(HDC dc, SCRIPT_CACHE *cache, const WCHAR *chars, int count, int max_glyphs,
                   SCRIPT_ANALYSIS *analysis, WORD *glyphs, WORD *clusters, SCRIPT_VISATTR *attrs, int *glyph_count)
{
    if (!chars || count <= 0 || !glyphs || !clusters || !attrs || !glyph_count || !analysis)
        return E_INVALIDARG;
    USP_CACHE *c;
    HRESULT hr = get_cache(dc, cache, &c);
    if (FAILED(hr))
        return hr;
    int needed = 0;
    for (int i = 0; i < count;) {
        next_cp(chars, count, &i);
        needed++;
    }
    if (max_glyphs < needed)
        return E_OUTOFMEMORY;
    if (!c->Real)
        analysis->fNoGlyphIndex = 1;
    BOOL rtl = analysis->fRTL && !analysis->fLogicalOrder;
    BOOL any_missing = FALSE;
    WCHAR blank = ' ';
    int g = 0;
    for (int i = 0; i < count; g++) {
        int start = i;
        UINT cp = next_cp(chars, count, &i);
        int slot = rtl ? needed - 1 - g : g;
        BOOL missing = FALSE, zero = is_zero_width(cp) && !analysis->s.fDisplayZWG;
        WORD id;
        if (zero)
            id = lookup_glyph(c, dc, blank, &missing);
        else if (cp > 0xFFFF) {
            /* 16 bit glyph ids cannot hold supplementary characters without a font cmap. */
            id = c->Real ? 0 : (WORD)c->Metrics.tmDefaultChar;
            missing = c->Real;
        } else
            id = lookup_glyph(c, dc, (WCHAR)cp, &missing);
        if (id == 0xFFFF && !missing)
            return E_PENDING;
        any_missing |= missing;
        glyphs[slot] = id;
        for (int k = start; k < i; k++)
            clusters[k] = (WORD)slot;
        SCRIPT_VISATTR *va = &attrs[slot];
        memset(va, 0, sizeof(*va));
        va->fClusterStart = 1;
        va->fDiacritic = is_mark(cp);
        va->fZeroWidth = zero;
        if (is_space(cp))
            va->uJustification = SCRIPT_JUSTIFY_BLANK;
        else if ((cp >= 0x3040 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF))
            va->uJustification = SCRIPT_JUSTIFY_CHARACTER;
    }
    *glyph_count = needed;
    if (any_missing && analysis->eScript != USP_UNDEFINED && g_props[analysis->eScript % USP_SCRIPT_COUNT].fComplex)
        return USP_E_SCRIPT_NOT_IN_FONT;
    return S_OK;
}

USPAPI ScriptPlace(HDC dc, SCRIPT_CACHE *cache, const WORD *glyphs, int count, const SCRIPT_VISATTR *attrs,
                   SCRIPT_ANALYSIS *analysis, int *advances, GOFFSET *offsets, ABC *abc)
{
    if (!glyphs || count <= 0 || !attrs || !advances || !analysis)
        return E_INVALIDARG;
    USP_CACHE *c;
    HRESULT hr = get_cache(dc, cache, &c);
    if (FAILED(hr))
        return hr;
    int total = 0, first_a = 0, last_c = 0;
    for (int i = 0; i < count; i++) {
        ABC w = {0, 0, 0};
        if (!attrs[i].fZeroWidth && !lookup_abc(c, dc, glyphs[i], analysis->fNoGlyphIndex, &w))
            return E_PENDING;
        advances[i] = w.abcA + (int)w.abcB + w.abcC;
        if (offsets)
            offsets[i].du = offsets[i].dv = 0;
        if (i == 0)
            first_a = w.abcA;
        if (i == count - 1)
            last_c = w.abcC;
        total += advances[i];
    }
    if (abc) {
        abc->abcA = first_a;
        abc->abcC = last_c;
        abc->abcB = (UINT)(total - first_a - last_c);
    }
    return S_OK;
}

USPAPI ScriptTextOut(const HDC dc, SCRIPT_CACHE *cache, int x, int y, UINT options, const RECT *rect,
                     const SCRIPT_ANALYSIS *analysis, const WCHAR *reserved, int reserved2, const WORD *glyphs,
                     int count, const int *advances, const int *justify, const GOFFSET *offsets)
{
    if (!dc || !glyphs || count <= 0 || !advances || !analysis)
        return E_INVALIDARG;
    const int *adv = justify ? justify : advances;
    UINT flags = options & (ETO_CLIPPED | ETO_OPAQUE);
    if (!analysis->fNoGlyphIndex)
        flags |= ETO_GLYPH_INDEX;
    BOOL shifted = FALSE;
    for (int i = 0; offsets && i < count; i++)
        if (offsets[i].du || offsets[i].dv)
            shifted = TRUE;
    if (!shifted)
        return ExtTextOutW(dc, x, y, flags, rect, (LPCWSTR)glyphs, (UINT)count, (const INT *)adv) ? S_OK : E_FAIL;
    if (flags & ETO_OPAQUE)
        ExtTextOutW(dc, x, y, ETO_OPAQUE, rect, NULL, 0, NULL);
    flags &= ~(UINT)ETO_OPAQUE;
    for (int i = 0; i < count; i++) {
        ExtTextOutW(dc, x + offsets[i].du, y - offsets[i].dv, flags, rect, (LPCWSTR)&glyphs[i], 1, &adv[i]);
        x += adv[i];
    }
    return S_OK;
}

USPAPI ScriptGetProperties(const SCRIPT_PROPERTIES ***properties, int *count)
{
    if (!properties && !count)
        return E_INVALIDARG;
    if (properties)
        *properties = g_prop_list;
    if (count)
        *count = USP_SCRIPT_COUNT;
    return S_OK;
}

USPAPI ScriptGetFontProperties(HDC dc, SCRIPT_CACHE *cache, SCRIPT_FONTPROPERTIES *properties)
{
    if (!properties)
        return E_INVALIDARG;
    if (properties->cBytes != sizeof(SCRIPT_FONTPROPERTIES))
        return E_INVALIDARG;
    USP_CACHE *c;
    HRESULT hr = get_cache(dc, cache, &c);
    if (FAILED(hr))
        return hr;
    BOOL missing;
    WORD blank = lookup_glyph(c, dc, ' ', &missing);
    if (blank == 0xFFFF)
        return E_PENDING;
    properties->wgBlank = blank;
    properties->wgDefault = c->Real ? 0 : (WORD)c->Metrics.tmDefaultChar;
    properties->wgInvalid = properties->wgDefault;
    properties->wgKashida = 0xFFFF;
    properties->iKashidaWidth = 0;
    return S_OK;
}

USPAPI ScriptGetCMap(HDC dc, SCRIPT_CACHE *cache, const WCHAR *chars, int count, DWORD flags, WORD *glyphs)
{
    if (!chars || count < 0 || !glyphs)
        return E_INVALIDARG;
    USP_CACHE *c;
    HRESULT hr = get_cache(dc, cache, &c);
    if (FAILED(hr))
        return hr;
    hr = S_OK;
    for (int i = 0; i < count; i++) {
        BOOL missing;
        WORD g = lookup_glyph(c, dc, chars[i], &missing);
        if (g == 0xFFFF && !missing)
            return E_PENDING;
        glyphs[i] = g;
        if (missing)
            hr = S_FALSE;
    }
    return hr;
}

USPAPI ScriptGetGlyphABCWidth(HDC dc, SCRIPT_CACHE *cache, WORD glyph, ABC *abc)
{
    if (!abc)
        return E_INVALIDARG;
    USP_CACHE *c;
    HRESULT hr = get_cache(dc, cache, &c);
    if (FAILED(hr))
        return hr;
    return lookup_abc(c, dc, glyph, !c->Real, abc) ? S_OK : E_PENDING;
}

USPAPI ScriptCacheGetHeight(HDC dc, SCRIPT_CACHE *cache, LONG *height)
{
    if (!height)
        return E_INVALIDARG;
    USP_CACHE *c;
    HRESULT hr = get_cache(dc, cache, &c);
    if (FAILED(hr))
        return hr;
    *height = c->Metrics.tmHeight;
    return S_OK;
}

/* ------------------------------------------------------------------ breaking and caret positions */

USPAPI ScriptBreak(const WCHAR *chars, int count, const SCRIPT_ANALYSIS *analysis, SCRIPT_LOGATTR *attrs)
{
    if (!chars || count <= 0 || !analysis || !attrs)
        return E_INVALIDARG;
    BOOL prev_space = FALSE;
    for (int i = 0; i < count; i++) {
        WCHAR c = chars[i];
        SCRIPT_LOGATTR *la = &attrs[i];
        memset(la, 0, sizeof(*la));
        BOOL low = c >= 0xDC00 && c <= 0xDFFF && i > 0 && chars[i - 1] >= 0xD800 && chars[i - 1] <= 0xDBFF;
        BOOL space = is_space(c) || c == '\r' || c == '\n';
        la->fCharStop = !low && !is_mark(c);
        la->fWhiteSpace = space;
        if (i > 0 && prev_space && !space && !low) {
            la->fSoftBreak = 1;
            la->fWordStop = 1;
        }
        if (i == 0)
            la->fWordStop = !space;
        /* ideographs break anywhere. */
        if (i > 0 && c >= 0x3040 && c <= 0x9FFF)
            la->fSoftBreak = la->fWordStop = 1;
        prev_space = space;
    }
    return S_OK;
}

/* glyph advances in logical order and the total width. */
static int logical_advance(int glyph, int glyphs, const int *advances, BOOL rtl)
{
    return advances[rtl ? glyphs - 1 - glyph : glyph];
}

/* the logical x of the leading or trailing edge of cp, from the logical start of the run. */
static int logical_x(int cp, BOOL trailing, int chars, int glyphs, const WORD *clusters, const int *advances,
                     BOOL rtl, int total)
{
    if (cp < 0)
        return 0;
    if (cp >= chars)
        return total;
    /* clusters hold visual glyph positions; logical glyph order reverses them for rtl runs. */
    int g = rtl ? glyphs - 1 - clusters[cp] : clusters[cp];
    int first = cp, last = cp;
    while (first > 0 && clusters[first - 1] == clusters[cp])
        first--;
    while (last + 1 < chars && clusters[last + 1] == clusters[cp])
        last++;
    int g_end = glyphs;
    if (last + 1 < chars)
        g_end = rtl ? glyphs - 1 - clusters[last + 1] : clusters[last + 1];
    if (g_end <= g)
        g_end = g + 1;
    int x = 0, width = 0;
    for (int i = 0; i < g; i++)
        x += logical_advance(i, glyphs, advances, rtl);
    for (int i = g; i < g_end && i < glyphs; i++)
        width += logical_advance(i, glyphs, advances, rtl);
    int in = last - first + 1, at = cp - first + (trailing ? 1 : 0);
    return x + width * at / in;
}

USPAPI ScriptCPtoX(int cp, BOOL trailing, int chars, int glyphs, const WORD *clusters, const SCRIPT_VISATTR *attrs,
                   const int *advances, const SCRIPT_ANALYSIS *analysis, int *x)
{
    if (!clusters || !advances || !analysis || !x || chars <= 0 || glyphs <= 0)
        return E_INVALIDARG;
    BOOL rtl = analysis->fRTL && !analysis->fLogicalOrder;
    int total = 0;
    for (int i = 0; i < glyphs; i++)
        total += advances[i];
    int lx = logical_x(cp, trailing, chars, glyphs, clusters, advances, rtl, total);
    *x = analysis->fRTL ? total - lx : lx;
    return S_OK;
}

USPAPI ScriptXtoCP(int x, int chars, int glyphs, const WORD *clusters, const SCRIPT_VISATTR *attrs,
                   const int *advances, const SCRIPT_ANALYSIS *analysis, int *cp, int *trailing)
{
    if (!clusters || !advances || !analysis || !cp || !trailing || chars <= 0 || glyphs <= 0)
        return E_INVALIDARG;
    BOOL rtl = analysis->fRTL && !analysis->fLogicalOrder;
    int total = 0;
    for (int i = 0; i < glyphs; i++)
        total += advances[i];
    int lx = analysis->fRTL ? total - x : x;
    if (lx < 0) {
        *cp = analysis->fRTL ? chars : -1;
        *trailing = analysis->fRTL ? 0 : 1;
        return S_OK;
    }
    if (lx >= total) {
        *cp = analysis->fRTL ? -1 : chars;
        *trailing = analysis->fRTL ? 1 : 0;
        return S_OK;
    }
    for (int i = 0; i < chars; i++) {
        int lead = logical_x(i, FALSE, chars, glyphs, clusters, advances, rtl, total);
        int trail = logical_x(i, TRUE, chars, glyphs, clusters, advances, rtl, total);
        if (lx < trail || i == chars - 1) {
            *cp = i;
            *trailing = lx - lead >= (trail - lead + 1) / 2 ? 1 : 0;
            return S_OK;
        }
    }
    return S_OK;
}

/* visual order of runs: from the highest level down to the lowest odd one, reverse every sequence at or above it. */
USPAPI ScriptLayout(int runs, const BYTE *levels, int *visual_to_logical, int *logical_to_visual)
{
    if (!levels || runs <= 0 || (!visual_to_logical && !logical_to_visual))
        return E_INVALIDARG;
    int stack[64], *order = runs <= 64 ? stack : usp_alloc((SIZE_T)runs * sizeof(int));
    if (!order)
        return E_OUTOFMEMORY;
    int high = 0, low_odd = 255;
    for (int i = 0; i < runs; i++) {
        order[i] = i;
        int lv = levels[i] & 0x3F;
        if (lv > high)
            high = lv;
        if ((lv & 1) && lv < low_odd)
            low_odd = lv;
    }
    for (int level = high; level >= low_odd && level > 0; level--) {
        for (int i = 0; i < runs;) {
            if ((levels[order[i]] & 0x3F) < level) {
                i++;
                continue;
            }
            int j = i;
            while (j < runs && (levels[order[j]] & 0x3F) >= level)
                j++;
            for (int a = i, b = j - 1; a < b; a++, b--) {
                int t = order[a];
                order[a] = order[b];
                order[b] = t;
            }
            i = j;
        }
    }
    for (int i = 0; i < runs; i++) {
        if (visual_to_logical)
            visual_to_logical[i] = order[i];
        if (logical_to_visual)
            logical_to_visual[order[i]] = i;
    }
    if (order != stack)
        usp_free(order);
    return S_OK;
}

/* ------------------------------------------------------------------ digit substitution */

USPAPI ScriptRecordDigitSubstitution(LCID locale, SCRIPT_DIGITSUBSTITUTE *sds)
{
    if (!sds)
        return E_POINTER;
    if (locale == LOCALE_USER_DEFAULT)
        locale = GetUserDefaultLCID();
    WCHAR v[4] = {0};
    int mode = SCRIPT_DIGITSUBSTITUTE_NONE;
    if (GetLocaleInfoW(locale, 0x1014 /* LOCALE_IDIGITSUBSTITUTION */, v, 4) > 0) {
        if (v[0] == '0')
            mode = SCRIPT_DIGITSUBSTITUTE_CONTEXT;
        else if (v[0] == '2')
            mode = SCRIPT_DIGITSUBSTITUTE_NATIONAL;
    }
    sds->NationalDigitLanguage = PRIMARYLANGID(LANGIDFROMLCID(locale));
    sds->TraditionalDigitLanguage = PRIMARYLANGID(LANGIDFROMLCID(locale));
    sds->DigitSubstitute = mode;
    sds->dwReserved = 0;
    return S_OK;
}

USPAPI ScriptApplyDigitSubstitution(const SCRIPT_DIGITSUBSTITUTE *sds, SCRIPT_CONTROL *control,
                                    SCRIPT_STATE *state)
{
    SCRIPT_DIGITSUBSTITUTE local;
    if (!control || !state)
        return E_INVALIDARG;
    if (!sds) {
        ScriptRecordDigitSubstitution(LOCALE_USER_DEFAULT, &local);
        sds = &local;
    }
    control->fContextDigits = 0;
    state->fDigitSubstitute = 0;
    switch (sds->DigitSubstitute) {
    case SCRIPT_DIGITSUBSTITUTE_CONTEXT:
        control->fContextDigits = 1;
        state->fDigitSubstitute = 1;
        control->uDefaultLanguage = sds->TraditionalDigitLanguage;
        break;
    case SCRIPT_DIGITSUBSTITUTE_NONE:
        break;
    case SCRIPT_DIGITSUBSTITUTE_NATIONAL:
        state->fDigitSubstitute = 1;
        control->uDefaultLanguage = sds->NationalDigitLanguage;
        break;
    case SCRIPT_DIGITSUBSTITUTE_TRADITIONAL:
        state->fDigitSubstitute = 1;
        control->uDefaultLanguage = sds->TraditionalDigitLanguage;
        break;
    default:
        return E_INVALIDARG;
    }
    return S_OK;
}
