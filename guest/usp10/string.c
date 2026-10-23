/*
 * ScriptStringAnalyse and friends: itemize, shape and place a whole string
 * in one call, lay its items out in visual order and keep the result for
 * drawing, measuring and caret positions.
 *
 * glyph arrays hold the items in visual order; characters stay in logical
 * order, with clusters relative to their item's first glyph.
 */
#include "usp10_private.h"

#define STRING_MAGIC 0x53535355 /* "USSS" */

#ifndef COLOR_HIGHLIGHT
#define COLOR_HIGHLIGHT 13
#define COLOR_HIGHLIGHTTEXT 14
#endif

typedef struct USP_STRING {
    DWORD Magic;
    HDC Dc;
    DWORD Flags;
    int Width;
    SCRIPT_CACHE Cache;
    int Chars; /* kept after SSA_CLIP. */
    WCHAR *Text;
    int Items;
    SCRIPT_ITEM *Item; /* Items + 1, the last one marks the end. */
    int *Visual;       /* item at each visual position. */
    int *First;        /* first glyph of each item. */
    int *Count;
    int Glyphs;
    WORD *Glyph;
    SCRIPT_VISATTR *Attr;
    int *Advance;
    int *Justify; /* the advances used for drawing. */
    GOFFSET *Offset;
    WORD *Cluster;
    SCRIPT_LOGATTR *LogAttr;
    int Hotkey;
    LONG Ascent;
    SIZE Size;
} USP_STRING;

static void free_string(USP_STRING *s)
{
    if (!s)
        return;
    ScriptFreeCache(&s->Cache);
    UspFree(s->Text);
    UspFree(s->Item);
    UspFree(s->Visual);
    UspFree(s->First);
    UspFree(s->Count);
    UspFree(s->Glyph);
    UspFree(s->Attr);
    if (s->Justify != s->Advance)
        UspFree(s->Justify);
    UspFree(s->Advance);
    UspFree(s->Offset);
    UspFree(s->Cluster);
    UspFree(s->LogAttr);
    s->Magic = 0;
    UspFree(s);
}

static USP_STRING *get_string(SCRIPT_STRING_ANALYSIS ssa)
{
    USP_STRING *s = ssa;
    return s && s->Magic == STRING_MAGIC ? s : nullptr;
}

/* stars for passwords, prefix ampersands made invisible. */
static WCHAR *prepare_text(const void *string, int length, int charset, DWORD flags, int *chars, int *hotkey)
{
    WCHAR *text;
    int n = length;
    if (charset != -1) {
        CHARSETINFO info;
        UINT cp = TranslateCharsetInfo((DWORD *)(ULONG_PTR)(UINT)charset, &info, TCI_SRCCHARSET) ? info.ciACP : CP_ACP;
        n = MultiByteToWideChar(cp, 0, string, length, nullptr, 0);
        if (n <= 0 || !(text = UspAlloc((SIZE_T)n * sizeof(WCHAR))))
            return nullptr;
        MultiByteToWideChar(cp, 0, string, length, text, n);
    } else {
        if (!(text = UspAlloc((SIZE_T)n * sizeof(WCHAR))))
            return nullptr;
        memcpy(text, string, (SIZE_T)n * sizeof(WCHAR));
    }
    *hotkey = -1;
    if (flags & (SSA_HOTKEY | SSA_HIDEHOTKEY)) {
        for (int i = 0; i < n; i++) {
            if (text[i] != '&')
                continue;
            text[i] = 0x200B;
            /* a doubled one shows a single ampersand. */
            if (i + 1 < n && text[i + 1] == '&')
                i++;
            else if (i + 1 < n && *hotkey < 0)
                *hotkey = i + 1;
        }
    }
    if (flags & SSA_PASSWORD) {
        for (int i = 0; i < n; i++)
            text[i] = '*';
    }
    *chars = n;
    return text;
}

/* windows aligns negative stops right; here every stop aligns left. */
static int tab_value(int v)
{
    return v < 0 ? -v : v;
}

/* without tab definitions, stops fall every eight average characters. */
static int next_tab(const SCRIPT_TABDEF *tabs, int average, int x)
{
    int interval = 8 * average, origin = 0;
    if (interval <= 0)
        interval = 64;
    if (tabs) {
        int scale = tabs->iScale ? tabs->iScale : average * 4;
        origin = tabs->iTabOrigin * scale / 4;
        if (tabs->cTabStops == 1 && tabs->pTabStops[0]) {
            int each = tab_value(tabs->pTabStops[0]) * scale / 4;
            if (each > 0)
                interval = each;
        } else {
            for (int i = 0; i < tabs->cTabStops; i++) {
                int stop = tab_value(tabs->pTabStops[i]) * scale / 4 - origin;
                if (stop > x)
                    return stop;
            }
        }
    }
    int p = x + origin;
    return (p / interval + 1) * interval - origin;
}

static HRESULT analyse(USP_STRING *s, int chars, const SCRIPT_CONTROL *control, const SCRIPT_STATE *state,
                       const int *dx, const SCRIPT_TABDEF *tabs)
{
    s->Chars = chars;
    s->Item = UspAlloc((SIZE_T)(chars + 2) * sizeof(SCRIPT_ITEM));
    if (!s->Item)
        return E_OUTOFMEMORY;
    HRESULT hr = ScriptItemize(s->Text, chars, chars + 2, control, state, s->Item, &s->Items);
    if (FAILED(hr))
        return hr;
    if (s->Flags & SSA_BREAK) {
        if (!(s->LogAttr = UspAlloc((SIZE_T)chars * sizeof(SCRIPT_LOGATTR))))
            return E_OUTOFMEMORY;
        for (int i = 0; i < s->Items; i++) {
            SCRIPT_ITEM *it = &s->Item[i];
            ScriptBreak(s->Text + it->iCharPos, it[1].iCharPos - it->iCharPos, &it->a, s->LogAttr + it->iCharPos);
        }
    }
    if (!(s->Flags & SSA_GLYPHS))
        return S_OK;

    int items = s->Items;
    BYTE *levels = UspAlloc((SIZE_T)items);
    s->Visual = UspAlloc((SIZE_T)items * sizeof(int));
    s->First = UspAlloc((SIZE_T)items * sizeof(int));
    s->Count = UspAlloc((SIZE_T)items * sizeof(int));
    int capacity = 2 * chars + 16 * items;
    s->Glyph = UspAlloc((SIZE_T)capacity * sizeof(WORD));
    s->Attr = UspAlloc((SIZE_T)capacity * sizeof(SCRIPT_VISATTR));
    s->Advance = UspAlloc((SIZE_T)capacity * sizeof(int));
    s->Offset = UspAlloc((SIZE_T)capacity * sizeof(GOFFSET));
    s->Cluster = UspAlloc((SIZE_T)chars * sizeof(WORD));
    if (!levels || !s->Visual || !s->First || !s->Count || !s->Glyph || !s->Attr || !s->Advance || !s->Offset ||
        !s->Cluster) {
        UspFree(levels);
        return E_OUTOFMEMORY;
    }
    for (int i = 0; i < items; i++)
        levels[i] = (BYTE)s->Item[i].a.s.uBidiLevel;
    ScriptLayout(items, levels, s->Visual, nullptr);
    UspFree(levels);

    /* item by item, in visual order. */
    int g = 0;
    for (int v = 0; v < items; v++) {
        int i = s->Visual[v];
        SCRIPT_ITEM *it = &s->Item[i];
        int start = it->iCharPos, n = it[1].iCharPos - start, count = 0;
        hr = ScriptShape(s->Dc, &s->Cache, s->Text + start, n, capacity - g, &it->a, s->Glyph + g, s->Cluster + start,
                         s->Attr + g, &count);
        /* no font to fall back on: use the nominal glyphs. */
        if (hr == USP_E_SCRIPT_NOT_IN_FONT) {
            it->a.eScript = SCRIPT_UNDEFINED;
            hr = ScriptShape(s->Dc, &s->Cache, s->Text + start, n, capacity - g, &it->a, s->Glyph + g,
                             s->Cluster + start, s->Attr + g, &count);
        }
        if (FAILED(hr))
            return hr;
        hr = ScriptPlace(s->Dc, &s->Cache, s->Glyph + g, count, s->Attr + g, &it->a, s->Advance + g, s->Offset + g,
                         nullptr);
        if (FAILED(hr))
            return hr;
        s->First[i] = g;
        s->Count[i] = count;
        g += count;
    }
    s->Glyphs = g;
    s->Justify = s->Advance;

    if (dx) {
        if (!(s->Justify = UspAlloc((SIZE_T)capacity * sizeof(int))))
            return E_OUTOFMEMORY;
        for (int i = 0; i < items; i++) {
            SCRIPT_ITEM *it = &s->Item[i];
            int start = it->iCharPos;
            if (s->Count[i])
                ScriptApplyLogicalWidth(dx + start, it[1].iCharPos - start, s->Count[i], s->Cluster + start,
                                        s->Attr + s->First[i], s->Advance + s->First[i], &it->a, nullptr,
                                        s->Justify + s->First[i]);
        }
    }

    TEXTMETRICW tm = {};
    GetTextMetricsW(s->Dc, &tm);
    s->Ascent = tm.tmAscent;
    if (s->Flags & SSA_TAB) {
        /* a tab glyph stretches from where the glyphs before it end to the next stop. */
        BYTE *tab = UspAlloc((SIZE_T)g + 1);
        if (!tab)
            return E_OUTOFMEMORY;
        for (int c = 0; c < chars; c++) {
            if (s->Text[c] != '\t')
                continue;
            for (int i = 0; i < items; i++) {
                if (c >= s->Item[i].iCharPos && c < s->Item[i + 1].iCharPos && s->Count[i])
                    tab[s->First[i] + s->Cluster[c]] = 1;
            }
        }
        int x = 0;
        for (int k = 0; k < g; k++) {
            if (tab[k])
                s->Justify[k] = next_tab(tabs, tm.tmAveCharWidth, x) - x;
            x += s->Justify[k];
        }
        UspFree(tab);
    }

    int total = 0;
    for (int k = 0; k < g; k++)
        total += s->Justify[k];
    if ((s->Flags & SSA_FIT) && g && total != s->Width) {
        int *fitted = UspAlloc((SIZE_T)capacity * sizeof(int));
        if (!fitted)
            return E_OUTOFMEMORY;
        ScriptJustify(s->Attr, s->Justify, g, s->Width - total, 0, fitted);
        if (s->Justify != s->Advance)
            UspFree(s->Justify);
        s->Justify = fitted;
        total = 0;
        for (int k = 0; k < g; k++)
            total += s->Justify[k];
    }
    s->Size.cx = total;
    s->Size.cy = tm.tmHeight;
    return S_OK;
}

/* ends on a cluster boundary. */
static int fitting_chars(USP_STRING *s, int width)
{
    int *dx = UspAlloc((SIZE_T)s->Chars * sizeof(int));
    if (!dx)
        return s->Chars;
    ScriptStringGetLogicalWidths(s, dx);
    int x = 0, kept = 0;
    for (int i = 0; i < s->Items; i++) {
        int end = s->Item[i + 1].iCharPos;
        for (int c = s->Item[i].iCharPos; c < end;) {
            int n = 1, w = dx[c];
            while (c + n < end && s->Cluster[c + n] == s->Cluster[c])
                w += dx[c + n++];
            if (x + w > width) {
                UspFree(dx);
                return kept;
            }
            x += w;
            c += n;
            kept = c;
        }
    }
    UspFree(dx);
    return kept;
}

USPAPI ScriptStringAnalyse(HDC dc, const void *string, int length, int max_glyphs, int charset, DWORD flags,
                           int width, SCRIPT_CONTROL *control, SCRIPT_STATE *state, const int *dx,
                           SCRIPT_TABDEF *tabs, const BYTE *in_class, SCRIPT_STRING_ANALYSIS *ssa)
{
    if (!ssa)
        return E_INVALIDARG;
    *ssa = nullptr;
    if (!string || length < 1 || length > 0xFFFF || (tabs && !tabs->pTabStops && tabs->cTabStops > 0))
        return E_INVALIDARG;
    if (dx)
        flags |= SSA_PIDX;

    USP_STRING *s = UspAlloc(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->Magic = STRING_MAGIC;
    s->Dc = dc;
    s->Flags = flags;
    s->Width = width;
    int chars;
    s->Text = prepare_text(string, length, charset, flags, &chars, &s->Hotkey);
    if (!s->Text) {
        free_string(s);
        return E_OUTOFMEMORY;
    }

    SCRIPT_CONTROL sc = control ? *control : (SCRIPT_CONTROL){0};
    SCRIPT_STATE st = state ? *state : (SCRIPT_STATE){0};
    /* the reading order, mirrored for a right to left layout. */
    st.uBidiLevel = (st.uBidiLevel & ~1) | (((flags & SSA_RTL) != 0) ^ ((flags & SSA_LAYOUTRTL) != 0));
    if (flags & SSA_DZWG)
        st.fDisplayZWG = 1;
    if (flags & SSA_GCP)
        st.fGcpClusters = 1;

    HRESULT hr = analyse(s, chars, &sc, &st, dx, tabs);
    if (SUCCEEDED(hr) && (flags & SSA_CLIP) && (flags & SSA_GLYPHS) && s->Size.cx > width) {
        int kept = fitting_chars(s, width);
        if (kept < chars) {
            /* the kept part is analysed again on its own. */
            USP_STRING *t = UspAlloc(sizeof(*t));
            if (!t) {
                hr = E_OUTOFMEMORY;
            } else {
                *t = (USP_STRING){.Magic = STRING_MAGIC, .Dc = dc, .Flags = flags, .Width = width,
                                  .Text = s->Text, .Hotkey = s->Hotkey < kept ? s->Hotkey : -1};
                s->Text = nullptr;
                free_string(s);
                s = t;
                hr = kept ? analyse(s, kept, &sc, &st, dx, tabs) : S_OK;
            }
        }
    }
    if (FAILED(hr)) {
        free_string(s);
        return hr;
    }
    *ssa = s;
    return S_OK;
}

USPAPI ScriptStringFree(SCRIPT_STRING_ANALYSIS *ssa)
{
    if (!ssa || !get_string(*ssa))
        return E_INVALIDARG;
    free_string(*ssa);
    *ssa = nullptr;
    return S_OK;
}

USPAPI_(const SIZE *) ScriptString_pSize(SCRIPT_STRING_ANALYSIS ssa)
{
    USP_STRING *s = get_string(ssa);
    return s ? &s->Size : nullptr;
}

USPAPI_(const int *) ScriptString_pcOutChars(SCRIPT_STRING_ANALYSIS ssa)
{
    USP_STRING *s = get_string(ssa);
    return s ? &s->Chars : nullptr;
}

USPAPI_(const SCRIPT_LOGATTR *) ScriptString_pLogAttr(SCRIPT_STRING_ANALYSIS ssa)
{
    USP_STRING *s = get_string(ssa);
    return s ? s->LogAttr : nullptr;
}

/* positions outside the string give the first or last item. */
static int item_of(const USP_STRING *s, int cp)
{
    int i = 0;
    while (i < s->Items - 1 && s->Item[i + 1].iCharPos <= cp)
        i++;
    return i;
}

static int item_x(const USP_STRING *s, int item)
{
    int x = 0;
    for (int k = 0; k < s->First[item]; k++)
        x += s->Justify[k];
    return x;
}

static int item_width(const USP_STRING *s, int item)
{
    int w = 0;
    for (int k = 0; k < s->Count[item]; k++)
        w += s->Justify[s->First[item] + k];
    return w;
}

USPAPI ScriptStringGetOrder(SCRIPT_STRING_ANALYSIS ssa, UINT *order)
{
    USP_STRING *s = get_string(ssa);
    if (!s || !order || !s->Glyph)
        return E_INVALIDARG;
    for (int i = 0; i < s->Items; i++) {
        for (int c = s->Item[i].iCharPos; c < s->Item[i + 1].iCharPos; c++)
            order[c] = (UINT)(s->First[i] + s->Cluster[c]);
    }
    return S_OK;
}

USPAPI ScriptStringCPtoX(SCRIPT_STRING_ANALYSIS ssa, int cp, BOOL trailing, int *x)
{
    USP_STRING *s = get_string(ssa);
    if (!s || !x || !s->Glyph || !s->Items)
        return E_INVALIDARG;
    int i = item_of(s, cp), start = s->Item[i].iCharPos, n = s->Item[i + 1].iCharPos - start;
    if (!s->Count[i]) {
        *x = item_x(s, i);
        return S_OK;
    }
    int in = 0;
    HRESULT hr = ScriptCPtoX(cp - start, trailing, n, s->Count[i], s->Cluster + start, s->Attr + s->First[i],
                             s->Justify + s->First[i], &s->Item[i].a, &in);
    if (FAILED(hr))
        return hr;
    *x = item_x(s, i) + in;
    return S_OK;
}

USPAPI ScriptStringXtoCP(SCRIPT_STRING_ANALYSIS ssa, int x, int *cp, int *trailing)
{
    USP_STRING *s = get_string(ssa);
    if (!s || !cp || !trailing || !s->Glyph || !s->Items)
        return E_INVALIDARG;
    int left = 0, v = 0;
    for (; v < s->Items - 1; v++) {
        int w = item_width(s, s->Visual[v]);
        if (x < left + w)
            break;
        left += w;
    }
    int i = s->Visual[v], start = s->Item[i].iCharPos, n = s->Item[i + 1].iCharPos - start;
    if (!s->Count[i]) {
        *cp = start;
        *trailing = 0;
        return S_OK;
    }
    int in = 0;
    HRESULT hr = ScriptXtoCP(x - left, n, s->Count[i], s->Cluster + start, s->Attr + s->First[i],
                             s->Justify + s->First[i], &s->Item[i].a, &in, trailing);
    if (FAILED(hr))
        return hr;
    *cp = start + in;
    return S_OK;
}

USPAPI ScriptStringGetLogicalWidths(SCRIPT_STRING_ANALYSIS ssa, int *dx)
{
    USP_STRING *s = get_string(ssa);
    if (!s || !dx || !s->Glyph)
        return E_INVALIDARG;
    for (int i = 0; i < s->Items; i++) {
        int start = s->Item[i].iCharPos, n = s->Item[i + 1].iCharPos - start;
        if (s->Count[i])
            ScriptGetLogicalWidths(&s->Item[i].a, n, s->Count[i], s->Justify + s->First[i], s->Cluster + start,
                                   s->Attr + s->First[i], dx + start);
        else
            memset(dx + start, 0, (SIZE_T)n * sizeof(int));
    }
    return S_OK;
}

/* no script here rejects sequences, so every string is valid. */
USPAPI ScriptStringValidate(SCRIPT_STRING_ANALYSIS ssa)
{
    return get_string(ssa) ? S_OK : E_INVALIDARG;
}

static void draw_items(USP_STRING *s, int left, int y, UINT options, const RECT *clip)
{
    int x = left;
    for (int v = 0; v < s->Items; v++) {
        int i = s->Visual[v];
        if (s->Count[i])
            ScriptTextOut(s->Dc, &s->Cache, x, y, options, clip, &s->Item[i].a, nullptr, 0, s->Glyph + s->First[i],
                          s->Count[i], s->Advance + s->First[i],
                          s->Justify != s->Advance ? s->Justify + s->First[i] : nullptr, s->Offset + s->First[i]);
        x += item_width(s, i);
    }
}

typedef DWORD(WINAPI *GET_SYS_COLOR)(int index);

static COLORREF sys_color(int index, COLORREF fallback)
{
    HMODULE user = GetModuleHandleW(L"user32.dll");
    GET_SYS_COLOR get = user ? (GET_SYS_COLOR)(void *)GetProcAddress(user, "GetSysColor") : nullptr;
    return get ? get(index) : fallback;
}

USPAPI ScriptStringOut(SCRIPT_STRING_ANALYSIS ssa, int x, int y, UINT options, const RECT *rect, int min_sel,
                       int max_sel, BOOL disabled)
{
    USP_STRING *s = get_string(ssa);
    if (!s || ((options & ETO_CLIPPED) && !rect) || !(s->Flags & SSA_GLYPHS))
        return E_INVALIDARG;
    HDC dc = s->Dc;
    UINT align = GetTextAlign(dc);
    if (align & TA_UPDATECP) {
        POINT cp;
        if (GetCurrentPositionEx(dc, &cp)) {
            x = cp.x;
            y = cp.y;
        }
    }
    /* items are drawn left to right whatever the alignment. */
    int left = x;
    if ((align & TA_CENTER) == TA_CENTER)
        left -= s->Size.cx / 2;
    else if (align & TA_RIGHT)
        left -= s->Size.cx;
    SetTextAlign(dc, align & ~(UINT)(TA_UPDATECP | TA_CENTER));
    int top = y;
    if ((align & TA_BASELINE) == TA_BASELINE)
        top -= s->Ascent;
    else if (align & TA_BOTTOM)
        top -= s->Size.cy;

    UINT item_options = options & ~(UINT)ETO_OPAQUE;
    if ((options & ETO_OPAQUE) && rect)
        ExtTextOutW(dc, 0, 0, ETO_OPAQUE, rect, nullptr, 0, nullptr);
    else
        item_options = options;
    BOOL only_hotkey = (s->Flags & SSA_HOTKEYONLY) == SSA_HOTKEYONLY;
    if (s->Glyphs && !only_hotkey)
        draw_items(s, left, y, item_options, rect);

    /* the hotkey underline sits a pixel under the baseline, in the text color. */
    if (s->Hotkey >= 0 && (s->Flags & SSA_HOTKEY)) {
        int a, b;
        if (SUCCEEDED(ScriptStringCPtoX(s, s->Hotkey, FALSE, &a)) &&
            SUCCEEDED(ScriptStringCPtoX(s, s->Hotkey, TRUE, &b))) {
            if (a > b) {
                int t = a;
                a = b;
                b = t;
            }
            HBRUSH brush = CreateSolidBrush(GetTextColor(dc));
            HGDIOBJ old = SelectObject(dc, brush);
            PatBlt(dc, left + a, top + s->Ascent + 1, b - a, 1, PATCOPY);
            SelectObject(dc, old);
            DeleteObject(brush);
        }
    }

    /* redraw the selection over a highlight, item by item. */
    if (min_sel < max_sel && s->Glyphs) {
        RECT box = {left, top, left + ((s->Flags & SSA_FIT) ? s->Width : s->Size.cx), top + s->Size.cy};
        if (options & ETO_CLIPPED)
            box = *rect;
        COLORREF old_bk = SetBkColor(dc, sys_color(COLOR_HIGHLIGHT, 0x00D77800));
        COLORREF old_text = disabled ? 0 : SetTextColor(dc, sys_color(COLOR_HIGHLIGHTTEXT, 0x00FFFFFF));
        for (int i = 0; i < s->Items; i++) {
            int start = s->Item[i].iCharPos, end = s->Item[i + 1].iCharPos;
            int lo = min_sel > start ? min_sel : start, hi = max_sel < end ? max_sel : end;
            if (lo >= hi || !s->Count[i])
                continue;
            int a, b;
            ScriptStringCPtoX(s, lo, FALSE, &a);
            ScriptStringCPtoX(s, hi - 1, TRUE, &b);
            RECT span = {left + (a < b ? a : b), box.top, left + (a < b ? b : a), box.bottom};
            if (span.left < box.left)
                span.left = box.left;
            if (span.right > box.right)
                span.right = box.right;
            if (span.left >= span.right)
                continue;
            ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &span, nullptr, 0, nullptr);
            draw_items(s, left, y, (options & ~(UINT)ETO_OPAQUE) | ETO_CLIPPED, &span);
        }
        SetBkColor(dc, old_bk);
        if (!disabled)
            SetTextColor(dc, old_text);
    }

    SetTextAlign(dc, align);
    if (align & TA_UPDATECP)
        MoveToEx(dc, x + s->Size.cx, y, nullptr);
    return S_OK;
}
