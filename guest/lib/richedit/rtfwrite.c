/*
 * writing: EM_STREAMOUT of plain text and rtf, and the rtf writer the
 * clipboard shares. the rtf matches what windows writes: a font and a color
 * table, \viewkind4\uc1, then paragraphs with their format differences,
 * characters outside the code page as \u with a ? fallback, and the final
 * paragraph mark as \par.
 */
#include "richedit_private.h"

typedef struct WBUF {
    char *data;
    UINT n, cap;
    BOOL failed;
} WBUF;

static void put(WBUF *o, const char *s, UINT n)
{
    if (o->failed)
        return;
    if (o->n + n + 1 > o->cap) {
        UINT cap = o->cap ? o->cap * 2 : 1024;
        while (cap < o->n + n + 1)
            cap *= 2;
        char *d = ReRealloc(o->data, cap);
        if (!d) {
            o->failed = TRUE;
            return;
        }
        o->data = d;
        o->cap = cap;
    }
    memcpy(o->data + o->n, s, n);
    o->n += n;
    o->data[o->n] = 0;
}

static void puts_(WBUF *o, const char *s) { put(o, s, (UINT)lstrlenA(s)); }

static void printf_(WBUF *o, const char *format, LONG v)
{
    char buffer[64];
    wsprintfA(buffer, format, v);
    puts_(o, buffer);
}

typedef struct WFONT {
    WCHAR face[LF_FACESIZE];
    BYTE charset, pitch;
} WFONT;

static int font_index(WFONT *fonts, int *count, const CHARFORMAT2W *cf, BOOL add)
{
    for (int i = 0; i < *count; i++)
        if (!lstrcmpiW(fonts[i].face, cf->szFaceName) && fonts[i].charset == cf->bCharSet)
            return i;
    if (!add || *count >= 256)
        return 0;
    WFONT *f = &fonts[(*count)++];
    memcpy(f->face, cf->szFaceName, sizeof(f->face));
    f->charset = cf->bCharSet;
    f->pitch = cf->bPitchAndFamily;
    return *count - 1;
}

static int color_index(COLORREF *colors, int *count, COLORREF c, BOOL add)
{
    for (int i = 0; i < *count; i++)
        if (colors[i] == c)
            return i + 1;
    if (!add || *count >= 256)
        return 0;
    colors[(*count)++] = c;
    return *count;
}

static const char *family(BYTE pitch)
{
    switch (pitch & 0xF0) {
    case FF_ROMAN:
        return "\\froman";
    case FF_SWISS:
        return "\\fswiss";
    case FF_MODERN:
        return "\\fmodern";
    case FF_SCRIPT:
        return "\\fscript";
    case FF_DECORATIVE:
        return "\\fdecor";
    }
    return "\\fnil";
}

static UINT charset_cp(BYTE charset)
{
    CHARSETINFO info;
    if (charset == SYMBOL_CHARSET)
        return 42;
    if (charset == DEFAULT_CHARSET || !TranslateCharsetInfo((DWORD *)(UINT_PTR)charset, &info, TCI_SRCCHARSET))
        return 1252;
    return info.ciACP;
}

static void put_text(WBUF *o, WCHAR c, UINT cp)
{
    if (c == '\\' || c == '{' || c == '}') {
        char s[3] = {'\\', (char)c, 0};
        puts_(o, s);
        return;
    }
    if (c >= 0x20 && c < 0x80) {
        char s[2] = {(char)c, 0};
        puts_(o, s);
        return;
    }
    if (c == '\t') {
        puts_(o, "\\tab ");
        return;
    }
    char bytes[4];
    int n = 0;
    BOOL lossy = FALSE;
    if (cp == 42) {
        if (c < 0x100) {
            bytes[0] = (char)c;
            n = 1;
        }
    } else {
        n = WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, &c, 1, bytes, 4, nullptr, &lossy);
    }
    if (n > 0 && !lossy) {
        for (int i = 0; i < n; i++)
            printf_(o, "\\'%02x", (BYTE)bytes[i]);
        return;
    }
    printf_(o, "\\u%d?", (SHORT)c);
}

static void para_props(WBUF *o, RE *e, const PARAFORMAT2 *pf)
{
    puts_(o, "\\pard");
    if (pf->wNumbering == PFN_BULLET)
        puts_(o, "{\\pntext\\'B7\\tab}{\\*\\pn\\pnlvlblt\\pnindent0{\\pntxtb\\'B7}}");
    else if (pf->wNumbering >= PFN_ARABIC && pf->wNumbering <= PFN_UCROMAN) {
        static const char *const kinds[] = {"\\pndec", "\\pnlcltr", "\\pnucltr", "\\pnlcrm", "\\pnucrm"};
        puts_(o, "{\\*\\pn\\pnlvlbody");
        puts_(o, kinds[pf->wNumbering - PFN_ARABIC]);
        if (pf->wNumberingStart > 1)
            printf_(o, "\\pnstart%d", pf->wNumberingStart);
        puts_(o, "}");
    }
    if (pf->wEffects & PFE_RTLPARA)
        puts_(o, "\\rtlpar");
    LONG fi = -pf->dxOffset, li = pf->dxStartIndent + pf->dxOffset;
    if (fi)
        printf_(o, "\\fi%d", fi);
    if (li)
        printf_(o, "\\li%d", li);
    if (pf->dxRightIndent)
        printf_(o, "\\ri%d", pf->dxRightIndent);
    if (pf->dySpaceBefore)
        printf_(o, "\\sb%d", pf->dySpaceBefore);
    if (pf->dySpaceAfter)
        printf_(o, "\\sa%d", pf->dySpaceAfter);
    switch (pf->bLineSpacingRule) {
    case 1:
        puts_(o, "\\sl360\\slmult1");
        break;
    case 2:
        puts_(o, "\\sl480\\slmult1");
        break;
    case 3:
        printf_(o, "\\sl%d", pf->dyLineSpacing);
        break;
    case 4:
        printf_(o, "\\sl-%d", pf->dyLineSpacing);
        break;
    case 5:
        printf_(o, "\\sl%d\\slmult1", pf->dyLineSpacing * 12);
        break;
    }
    for (int i = 0; i < pf->cTabCount; i++) {
        LONG t = pf->rgxTabs[i];
        static const char *const kinds[] = {"", "\\tqc", "\\tqr", "\\tqdec", "\\tb"};
        static const char *const leaders[] = {"", "\\tldot", "\\tlhyph", "\\tlul", "\\tlth", "\\tleq"};
        int kind = (t >> 24) & 0x0F, leader = (t >> 28) & 0x0F;
        if (leader < 6)
            puts_(o, leaders[leader]);
        if (kind == 4) {
            printf_(o, "\\tb%d", t & 0xFFFFFF);
            continue;
        }
        if (kind < 4)
            puts_(o, kinds[kind]);
        printf_(o, "\\tx%d", t & 0xFFFFFF);
    }
    switch (pf->wAlignment) {
    case PFA_RIGHT:
        puts_(o, "\\qr");
        break;
    case PFA_CENTER:
        puts_(o, "\\qc");
        break;
    case PFA_JUSTIFY:
        puts_(o, "\\qj");
        break;
    }
    (void)e;
}

static void effect(WBUF *o, DWORD old, DWORD now, DWORD bit, const char *on, const char *off)
{
    if ((old ^ now) & bit)
        puts_(o, (now & bit) ? on : off);
}

static void char_props(WBUF *o, const CHARFORMAT2W *prev, const CHARFORMAT2W *cf, WFONT *fonts, int *font_count,
                       COLORREF *colors, int *color_count, BOOL first)
{
    if (first || lstrcmpiW(prev->szFaceName, cf->szFaceName) || prev->bCharSet != cf->bCharSet)
        printf_(o, "\\f%d", font_index(fonts, font_count, cf, FALSE));
    DWORD p = first ? 0 : prev->dwEffects, n = cf->dwEffects;
    BOOL color_changed = first ? !(n & CFE_AUTOCOLOR)
                               : ((p ^ n) & CFE_AUTOCOLOR) || (!(n & CFE_AUTOCOLOR) && prev->crTextColor != cf->crTextColor);
    if (color_changed)
        printf_(o, "\\cf%d", (n & CFE_AUTOCOLOR) ? 0 : color_index(colors, color_count, cf->crTextColor, FALSE));
    BOOL back_changed = first ? !(n & CFE_AUTOBACKCOLOR)
                              : ((p ^ n) & CFE_AUTOBACKCOLOR) ||
                                    (!(n & CFE_AUTOBACKCOLOR) && prev->crBackColor != cf->crBackColor);
    if (back_changed)
        printf_(o, "\\highlight%d",
                (n & CFE_AUTOBACKCOLOR) ? 0 : color_index(colors, color_count, cf->crBackColor, FALSE));
    if (first)
        p = 0;
    effect(o, p, n, CFE_BOLD, "\\b", "\\b0");
    effect(o, p, n, CFE_ITALIC, "\\i", "\\i0");
    effect(o, p, n, CFE_UNDERLINE, "\\ul", "\\ulnone");
    effect(o, p, n, CFE_STRIKEOUT, "\\strike", "\\strike0");
    effect(o, p, n, CFE_PROTECTED, "\\protect", "\\protect0");
    effect(o, p, n, CFE_HIDDEN, "\\v", "\\v0");
    effect(o, p, n, CFE_SMALLCAPS, "\\scaps", "\\scaps0");
    effect(o, p, n, CFE_ALLCAPS, "\\caps", "\\caps0");
    if ((p ^ n) & (CFE_SUBSCRIPT | CFE_SUPERSCRIPT))
        puts_(o, (n & CFE_SUBSCRIPT) ? "\\sub" : (n & CFE_SUPERSCRIPT) ? "\\super" : "\\nosupersub");
    if (first ? cf->yOffset != 0 : prev->yOffset != cf->yOffset) {
        if (cf->yOffset >= 0)
            printf_(o, "\\up%d", cf->yOffset / 10);
        else
            printf_(o, "\\dn%d", -cf->yOffset / 10);
    }
    if (first || prev->yHeight != cf->yHeight)
        printf_(o, "\\fs%d", (cf->yHeight + 5) / 10);
    puts_(o, " ");
}

char *ReWriteRtf(RE *e, UINT from, UINT to, UINT *size)
{
    if (to > e->len)
        to = e->len;
    if (from > to)
        from = to;
    WFONT *fonts = ReAlloc(256 * sizeof(WFONT));
    COLORREF *colors = ReAlloc(256 * sizeof(COLORREF));
    WBUF o;
    memset(&o, 0, sizeof(o));
    if (!fonts || !colors) {
        ReFree(fonts);
        ReFree(colors);
        return nullptr;
    }
    int font_count = 0, color_count = 0;
    WORD first_cf = from < e->len ? e->cfi[from] : e->def_cf;
    font_index(fonts, &font_count, &e->fmts[first_cf].cf, TRUE);
    for (UINT i = from; i < to; i++) {
        const CHARFORMAT2W *cf = &e->fmts[e->cfi[i]].cf;
        font_index(fonts, &font_count, cf, TRUE);
        if (!(cf->dwEffects & CFE_AUTOCOLOR))
            color_index(colors, &color_count, cf->crTextColor, TRUE);
        if (!(cf->dwEffects & CFE_AUTOBACKCOLOR))
            color_index(colors, &color_count, cf->crBackColor, TRUE);
    }
    UINT cp = charset_cp(fonts[0].charset);
    printf_(&o, "{\\rtf1\\ansi\\ansicpg%d", (LONG)(cp == 42 ? 1252 : cp));
    printf_(&o, "\\deff0\\deflang%d{\\fonttbl", (LONG)LANGIDFROMLCID(e->fmts[e->def_cf].cf.lcid));
    for (int i = 0; i < font_count; i++) {
        printf_(&o, "{\\f%d", i);
        puts_(&o, family(fonts[i].pitch));
        if (fonts[i].pitch & 0x0F)
            printf_(&o, "\\fprq%d", fonts[i].pitch & 0x0F);
        printf_(&o, "\\fcharset%d ", fonts[i].charset);
        for (const WCHAR *f = fonts[i].face; *f; f++)
            put_text(&o, *f, 1252);
        puts_(&o, ";}");
    }
    puts_(&o, "}\r\n");
    if (color_count) {
        puts_(&o, "{\\colortbl ;");
        for (int i = 0; i < color_count; i++) {
            printf_(&o, "\\red%d", GetRValue(colors[i]));
            printf_(&o, "\\green%d", GetGValue(colors[i]));
            printf_(&o, "\\blue%d;", GetBValue(colors[i]));
        }
        puts_(&o, "}\r\n");
    }
    puts_(&o, "\\viewkind4\\uc1");
    WORD last_pf = 0xFFFF;
    const CHARFORMAT2W *prev = nullptr;
    WORD prev_cf = 0xFFFF;
    BOOL para_start = TRUE;
    for (UINT i = from; i < to; i++) {
        if (para_start) {
            WORD pf = e->pfi[ReParaEnd(e, i)];
            if (pf != last_pf) {
                para_props(&o, e, &e->paras[pf]);
                last_pf = pf;
                prev_cf = 0xFFFF;
            }
            para_start = FALSE;
        }
        WORD k = e->cfi[i];
        if (k != prev_cf) {
            const CHARFORMAT2W *cf = &e->fmts[k].cf;
            char_props(&o, prev ? prev : cf, cf, fonts, &font_count, colors, &color_count, prev == nullptr);
            prev = cf;
            prev_cf = k;
        }
        WCHAR c = e->text[i];
        if (c == RE_CR) {
            puts_(&o, "\\par\r\n");
            para_start = TRUE;
        } else if (c == RE_VT) {
            puts_(&o, "\\line ");
        } else if (c == RE_FF) {
            puts_(&o, "\\page ");
        } else {
            put_text(&o, c, charset_cp(e->fmts[k].cf.bCharSet));
        }
    }
    puts_(&o, "}\r\n");
    ReFree(fonts);
    ReFree(colors);
    if (o.failed) {
        ReFree(o.data);
        return nullptr;
    }
    *size = o.n;
    return o.data;
}

/* hands bytes to the application in chunks. FALSE once it fails or takes less. */
static BOOL write_out(EDITSTREAM *es, const BYTE *data, UINT size, LONG *written)
{
    while (size) {
        LONG chunk = size > 4096 ? 4096 : (LONG)size;
        LONG done = 0;
        DWORD r = es->pfnCallback(es->dwCookie, (LPBYTE)data, chunk, &done);
        es->dwError = r;
        if (r)
            return FALSE;
        if (done != chunk) {
            if (done > 0)
                *written += done;
            es->dwError = (DWORD)0x80030070;
            return FALSE;
        }
        *written += done;
        data += done;
        size -= (UINT)done;
    }
    return TRUE;
}

LONG ReStreamOut(RE *e, UINT flags, EDITSTREAM *es)
{
    if (!es || !es->pfnCallback)
        return 0;
    es->dwError = 0;
    BOOL sel = (flags & SFF_SELECTION) != 0;
    UINT from = sel ? ReSelMin(e) : 0, to = sel ? ReSelMax(e) : e->len;
    LONG written = 0;
    if (flags & SF_RTF) {
        UINT size = 0;
        char *rtf = ReWriteRtf(e, from, to, &size);
        if (!rtf)
            return 0;
        write_out(es, (const BYTE *)rtf, size, &written);
        ReFree(rtf);
        return written;
    }
    if (!(flags & (SF_TEXT | SF_TEXTIZED)))
        return 0;
    if (to > e->len - 1)
        to = e->len - 1;
    UINT n = 0;
    WCHAR *s = ReRangeText(e, from, to, TRUE, &n);
    if (!s)
        return 0;
    UINT cp = (flags & SF_UNICODE) ? 1200 : (flags & SF_USECODEPAGE) ? HIWORD(flags) : CP_ACP;
    if (cp == 1200) {
        write_out(es, (const BYTE *)s, n * sizeof(WCHAR), &written);
    } else {
        int k = n ? WideCharToMultiByte(cp, 0, s, (int)n, nullptr, 0, nullptr, nullptr) : 0;
        char *bytes = ReAlloc((SIZE_T)k + 1);
        if (bytes) {
            if (k)
                WideCharToMultiByte(cp, 0, s, (int)n, bytes, k, nullptr, nullptr);
            write_out(es, (const BYTE *)bytes, (UINT)k, &written);
            ReFree(bytes);
        }
    }
    ReFree(s);
    return written;
}
