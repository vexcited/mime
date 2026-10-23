/*
 * reading: EM_STREAMIN of plain text and rtf, and the rtf reader used by
 * streaming, SetText and pasting. the reader keeps windows' quirks:
 * keywords are case insensitive and at most 32 letters, \uc takes 0 to
 * 2 only, braces cancel a pending \u fallback, \~ is a plain space, \line
 * a line break inside the paragraph, unknown keywords after \* skip their
 * group while other unknown destinations show their text, and a stream
 * filling the whole story merges its last paragraph mark into the
 * story's own.
 */
#include "richedit_private.h"

enum { DEST_TEXT, DEST_SKIP, DEST_FONTTBL, DEST_COLORTBL, DEST_FLDINST, DEST_PN, DEST_FIELD };

typedef struct RTF_FONT {
    int id;
    BYTE charset, pitch;
    UINT cp;
    WCHAR face[LF_FACESIZE];
} RTF_FONT;

typedef struct RTF_STATE {
    RE_FMT fmt;
    PARAFORMAT2 pf;
    LONG li, fi;
    int font;
    UINT cp;
    int uc;
    int dest;
    BOOL table_font;
    BOOL field_result;
    int field;
} RTF_STATE;

typedef struct RTF_FIELD {
    char inst[512];
    UINT inst_len;
    UINT result_start;
    BOOL hyperlink, has_result;
} RTF_FIELD;

typedef struct RTF {
    RE *e;
    const BYTE *p, *end;
    RTF_STATE *stack;
    int depth, cap;
    RTF_STATE st;
    RTF_FONT *fonts;
    int font_count, font_cap;
    COLORREF *colors;
    BOOL *auto_colors;
    int color_count, color_cap;
    BYTE r, g, b;
    BOOL rgb_set;
    int deff;
    UINT doc_cp;
    int skip;
    BOOL star;
    LONG error;
    BOOL stop;
    BOOL ended;
    /* output. */
    WCHAR *text;
    WORD *cf, *pf;
    UINT n, out_cap;
    BYTE bytes[512];
    UINT byte_count;
    /* tabs being declared. */
    BYTE tab_align, tab_leader;
    /* numbering a \pn group declares. */
    WORD pn_numbering, pn_style, pn_start, pn_tab;
    BOOL pn_set;
    RTF_FIELD fields[4];
    int field_count;
    BOOL whole;
} RTF;

/* ------------------------------------------------------------------ output */

static BOOL grow(RTF *r, UINT extra)
{
    if (r->n + extra <= r->out_cap)
        return TRUE;
    UINT cap = r->out_cap ? r->out_cap : 256;
    while (cap < r->n + extra)
        cap *= 2;
    WCHAR *t = ReRealloc(r->text, cap * sizeof(WCHAR));
    if (!t)
        return FALSE;
    r->text = t;
    WORD *c = ReRealloc(r->cf, cap * sizeof(WORD));
    if (!c)
        return FALSE;
    r->cf = c;
    WORD *p = ReRealloc(r->pf, cap * sizeof(WORD));
    if (!p)
        return FALSE;
    r->pf = p;
    r->out_cap = cap;
    return TRUE;
}

static WORD current_pf(RTF *r)
{
    PARAFORMAT2 pf;
    memcpy(&pf, &r->st.pf, sizeof(pf));
    pf.dxStartIndent = r->st.li + r->st.fi;
    pf.dxOffset = -r->st.fi;
    return ReAddPara(r->e, &pf);
}

static void emit(RTF *r, WCHAR c)
{
    if (r->stop || !grow(r, 1))
        return;
    if (r->st.dest != DEST_TEXT && r->st.dest != DEST_FIELD)
        return;
    r->text[r->n] = c;
    r->cf[r->n] = ReAddFormat(r->e, &r->st.fmt);
    r->pf[r->n] = 0xFFFF;
    r->n++;
}

static void emit_para(RTF *r, WCHAR mark)
{
    if (!r->e->multiline) {
        r->stop = TRUE;
        return;
    }
    UINT at = r->n;
    emit(r, mark);
    if (mark == RE_CR && r->n > at)
        r->pf[at] = current_pf(r);
}

/* decodes the pending 8-bit bytes in the state's code page. */
static void flush_bytes(RTF *r)
{
    if (!r->byte_count)
        return;
    UINT n = r->byte_count;
    r->byte_count = 0;
    if (r->st.dest == DEST_FLDINST) {
        RTF_FIELD *f = r->field_count ? &r->fields[r->field_count - 1] : nullptr;
        if (f)
            for (UINT i = 0; i < n && f->inst_len + 1 < sizeof(f->inst); i++)
                f->inst[f->inst_len++] = (char)r->bytes[i];
        return;
    }
    if (r->st.cp == 42) {
        for (UINT i = 0; i < n; i++)
            emit(r, r->bytes[i]);
        return;
    }
    WCHAR w[512];
    int k = MultiByteToWideChar(r->st.cp, 0, (const char *)r->bytes, (int)n, w, 512);
    if (k <= 0) {
        for (UINT i = 0; i < n; i++)
            emit(r, r->bytes[i]);
        return;
    }
    for (int i = 0; i < k; i++)
        emit(r, w[i]);
}

static void add_byte(RTF *r, BYTE b)
{
    if (r->skip > 0) {
        r->skip--;
        return;
    }
    if (r->st.dest == DEST_FONTTBL) {
        /* the font name, up to its semicolon. */
        if (r->font_count && r->st.table_font) {
            RTF_FONT *f = &r->fonts[r->font_count - 1];
            if (b == ';') {
                r->st.table_font = FALSE;
                return;
            }
            UINT len = (UINT)lstrlenW(f->face);
            if (len + 1 < LF_FACESIZE) {
                f->face[len] = b;
                f->face[len + 1] = 0;
            }
        }
        return;
    }
    if (r->st.dest == DEST_COLORTBL) {
        if (b == ';') {
            if (r->color_count == r->color_cap) {
                int cap = r->color_cap ? r->color_cap * 2 : 16;
                COLORREF *c = ReRealloc(r->colors, cap * sizeof(COLORREF));
                BOOL *a = ReRealloc(r->auto_colors, cap * sizeof(BOOL));
                if (c)
                    r->colors = c;
                if (a)
                    r->auto_colors = a;
                if (!c || !a)
                    return;
                r->color_cap = cap;
            }
            r->colors[r->color_count] = RGB(r->r, r->g, r->b);
            r->auto_colors[r->color_count] = !r->rgb_set;
            r->color_count++;
            r->r = r->g = r->b = 0;
            r->rgb_set = FALSE;
        }
        return;
    }
    if (r->st.dest == DEST_SKIP || r->st.dest == DEST_PN)
        return;
    if (r->byte_count == sizeof(r->bytes))
        flush_bytes(r);
    r->bytes[r->byte_count++] = b;
}

/* ------------------------------------------------------------------ fonts and colors */

static UINT charset_cp(RTF *r, BYTE charset)
{
    switch (charset) {
    case ANSI_CHARSET:
        return 1252;
    case DEFAULT_CHARSET:
        return r->doc_cp;
    case SYMBOL_CHARSET:
        return 42;
    case 77:
        return 10000;
    case SHIFTJIS_CHARSET:
        return 932;
    case HANGEUL_CHARSET:
        return 949;
    case JOHAB_CHARSET:
        return 1361;
    case GB2312_CHARSET:
        return 936;
    case CHINESEBIG5_CHARSET:
        return 950;
    case GREEK_CHARSET:
        return 1253;
    case TURKISH_CHARSET:
        return 1254;
    case VIETNAMESE_CHARSET:
        return 1258;
    case HEBREW_CHARSET:
        return 1255;
    case ARABIC_CHARSET:
        return 1256;
    case BALTIC_CHARSET:
        return 1257;
    case RUSSIAN_CHARSET:
        return 1251;
    case THAI_CHARSET:
        return 874;
    case EASTEUROPE_CHARSET:
        return 1250;
    case OEM_CHARSET:
        return 437;
    }
    return r->doc_cp;
}

static RTF_FONT *find_font(RTF *r, int id)
{
    for (int i = 0; i < r->font_count; i++)
        if (r->fonts[i].id == id)
            return &r->fonts[i];
    return nullptr;
}

static void select_font(RTF *r, int id)
{
    RTF_FONT *f = find_font(r, id);
    r->st.font = id;
    if (!f)
        return;
    CHARFORMAT2W *cf = &r->st.fmt.cf;
    if (f->face[0]) {
        memcpy(cf->szFaceName, f->face, sizeof(cf->szFaceName));
        cf->szFaceName[LF_FACESIZE - 1] = 0;
    }
    cf->bCharSet = f->charset;
    cf->bPitchAndFamily = f->pitch;
    r->st.cp = f->cp ? f->cp : charset_cp(r, f->charset);
}

static void set_color(RTF *r, int index, BOOL back)
{
    CHARFORMAT2W *cf = &r->st.fmt.cf;
    BOOL automatic = index < 0 || index >= r->color_count || r->auto_colors[index];
    COLORREF c = automatic ? 0 : r->colors[index];
    if (back) {
        cf->dwEffects = automatic ? cf->dwEffects | CFE_AUTOBACKCOLOR : cf->dwEffects & ~CFE_AUTOBACKCOLOR;
        cf->crBackColor = c;
    } else {
        cf->dwEffects = automatic ? cf->dwEffects | CFE_AUTOCOLOR : cf->dwEffects & ~CFE_AUTOCOLOR;
        cf->crTextColor = c;
    }
}

static void set_plain(RTF *r)
{
    CHARFORMAT2W *cf = &r->st.fmt.cf;
    LCID lcid = cf->lcid;
    memset(&r->st.fmt, 0, sizeof(r->st.fmt));
    cf->cbSize = sizeof(*cf);
    cf->dwMask = CFM_ALL2;
    cf->yHeight = 240;
    cf->wWeight = FW_NORMAL;
    cf->dwEffects = CFE_AUTOCOLOR | CFE_AUTOBACKCOLOR;
    cf->lcid = lcid;
    memcpy(cf->szFaceName, r->e->fmts[r->e->def_cf].cf.szFaceName, sizeof(cf->szFaceName));
    cf->bCharSet = r->e->fmts[r->e->def_cf].cf.bCharSet;
    cf->bPitchAndFamily = r->e->fmts[r->e->def_cf].cf.bPitchAndFamily;
    select_font(r, r->deff);
}

static void set_pard(RTF *r)
{
    memset(&r->st.pf, 0, sizeof(r->st.pf));
    r->st.pf.wAlignment = PFA_LEFT;
    r->st.li = r->st.fi = 0;
    r->tab_align = r->tab_leader = 0;
}

/* ------------------------------------------------------------------ groups */

static BOOL push_state(RTF *r)
{
    if (r->depth == r->cap) {
        int cap = r->cap ? r->cap * 2 : 16;
        RTF_STATE *s = ReRealloc(r->stack, cap * sizeof(RTF_STATE));
        if (!s)
            return FALSE;
        r->stack = s;
        r->cap = cap;
    }
    r->stack[r->depth++] = r->st;
    return TRUE;
}

static void end_field(RTF *r)
{
    if (!r->field_count)
        return;
    RTF_FIELD *f = &r->fields[--r->field_count];
    if (!f->hyperlink)
        return;
    /* the url: the first quoted string, or the first word, after HYPERLINK. */
    char url[512];
    UINT k = 0;
    const char *s = f->inst;
    while (*s == ' ')
        s++;
    s += 9;
    while (*s == ' ')
        s++;
    if (*s == '"') {
        s++;
        while (*s && *s != '"' && k + 1 < sizeof(url)) {
            if (*s == '\\' && s[1])
                s++;
            url[k++] = *s++;
        }
    } else {
        while (*s && *s != ' ' && k + 1 < sizeof(url))
            url[k++] = *s++;
    }
    url[k] = 0;
    if (!k || f->result_start > r->n)
        return;
    WCHAR wurl[512];
    int wn = MultiByteToWideChar(r->st.cp == 42 ? 1252 : r->st.cp, 0, url, (int)k, wurl, 511);
    if (wn <= 0)
        return;
    UINT rs = f->result_start, rn = r->n - rs;
    if (r->e->v41) {
        /* msftedit: the result becomes a friendly link. */
        for (UINT i = rs; i < r->n; i++) {
            RE_FMT fm;
            memcpy(&fm, &r->e->fmts[r->cf[i]], sizeof(fm));
            fm.cf.dwEffects |= CFE_LINK;
            r->cf[i] = ReAddFormat(r->e, &fm);
        }
        return;
    }
    /* riched20: the result, then the url in angle brackets, or just the url when they are the same. */
    BOOL same = rn == (UINT)wn;
    for (UINT i = 0; same && i < rn; i++)
        same = r->text[rs + i] == wurl[i];
    if (same)
        r->n = rs;
    else
        emit(r, ' ');
    emit(r, '<');
    for (int i = 0; i < wn; i++)
        emit(r, wurl[i]);
    emit(r, '>');
}

static void pop_state(RTF *r)
{
    flush_bytes(r);
    if (!r->depth) {
        r->ended = TRUE;
        return;
    }
    RTF_STATE inner = r->st;
    r->st = r->stack[--r->depth];
    if (inner.dest == DEST_FONTTBL && r->st.dest != DEST_FONTTBL && r->st.font < 0)
        select_font(r, r->deff);
    if (inner.dest == DEST_PN && r->st.dest != DEST_PN && r->pn_set) {
        r->st.pf.wNumbering = r->pn_numbering;
        r->st.pf.wNumberingStyle = r->pn_style;
        r->st.pf.wNumberingStart = r->pn_start;
        r->st.pf.wNumberingTab = r->pn_tab;
        r->pn_set = FALSE;
    }
    if (inner.dest == DEST_FIELD && r->st.dest != DEST_FIELD && inner.field != r->st.field)
        end_field(r);
    if (inner.field_result && !r->st.field_result && r->field_count) {
        RTF_FIELD *f = &r->fields[r->field_count - 1];
        f->has_result = TRUE;
    }
}

/* ------------------------------------------------------------------ keywords */

static BOOL eq(const char *a, const char *b) { return !lstrcmpA(a, b); }

static void toggle(RTF *r, DWORD effect, BOOL has, LONG value)
{
    CHARFORMAT2W *cf = &r->st.fmt.cf;
    if (!has || value)
        cf->dwEffects |= effect;
    else
        cf->dwEffects &= ~effect;
    if (effect == CFE_BOLD)
        cf->wWeight = (cf->dwEffects & CFE_BOLD) ? FW_BOLD : FW_NORMAL;
}

static void underline(RTF *r, BYTE type, BOOL has, LONG value)
{
    CHARFORMAT2W *cf = &r->st.fmt.cf;
    if (has && !value) {
        cf->dwEffects &= ~CFE_UNDERLINE;
        cf->bUnderlineType = CFU_UNDERLINENONE;
        return;
    }
    cf->dwEffects |= CFE_UNDERLINE;
    cf->bUnderlineType = type;
}

static BOOL special_char(RTF *r, const char *w)
{
    static const struct {
        const char *word;
        WCHAR c;
    } table[] = {
        {"tab", '\t'},       {"bullet", 0x2022}, {"lquote", 0x2018},   {"rquote", 0x2019}, {"ldblquote", 0x201C},
        {"rdblquote", 0x201D}, {"endash", 0x2013}, {"emdash", 0x2014}, {"enspace", 0x2002}, {"emspace", 0x2003},
        {"qmspace", 0x2005}, {"zwj", 0x200D},     {"zwnj", 0x200C},     {"ltrmark", 0x200E}, {"rtlmark", 0x200F},
        {"zwbo", 0x200B},
    };
    for (UINT i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (eq(w, table[i].word)) {
            flush_bytes(r);
            if (r->st.dest == DEST_FLDINST)
                return TRUE;
            emit(r, table[i].c);
            return TRUE;
        }
    return FALSE;
}

static void keyword(RTF *r, const char *w, BOOL has, LONG v)
{
    CHARFORMAT2W *cf = &r->st.fmt.cf;
    BOOL star = r->star;
    r->star = FALSE;
    if (eq(w, "bin")) {
        flush_bytes(r);
        LONG n = has && v > 0 ? v : 0;
        if (r->skip > 0)
            r->skip--;
        if ((LONG)(r->end - r->p) < n)
            n = (LONG)(r->end - r->p);
        r->p += n;
        return;
    }
    if (r->skip > 0) {
        r->skip--;
        return;
    }
    if (eq(w, "u")) {
        flush_bytes(r);
        WCHAR c = (WCHAR)(v < 0 ? v + 65536 : v);
        if (cf->bCharSet == SYMBOL_CHARSET) {
            if (c >= 0xF000 && c <= 0xF0FF)
                c = (WCHAR)(c & 0xFF);
        }
        if (r->st.dest == DEST_TEXT || r->st.dest == DEST_FIELD)
            emit(r, c);
        else if (r->st.dest == DEST_FONTTBL && r->font_count && r->st.table_font) {
            RTF_FONT *f = &r->fonts[r->font_count - 1];
            UINT len = (UINT)lstrlenW(f->face);
            if (len + 1 < LF_FACESIZE) {
                f->face[len] = c;
                f->face[len + 1] = 0;
            }
        }
        r->skip = r->st.uc;
        return;
    }
    flush_bytes(r);
    if (special_char(r, w))
        return;
    /* destinations. */
    if (eq(w, "fonttbl")) {
        r->st.dest = DEST_FONTTBL;
        return;
    }
    if (eq(w, "colortbl")) {
        r->st.dest = DEST_COLORTBL;
        r->r = r->g = r->b = 0;
        r->rgb_set = FALSE;
        return;
    }
    if (eq(w, "stylesheet") || eq(w, "info") || eq(w, "pict") || eq(w, "object") || eq(w, "pntext") ||
        eq(w, "pntxta") || eq(w, "pntxtb") || eq(w, "listtext") || eq(w, "xe") || eq(w, "fchars") ||
        eq(w, "lchars") || eq(w, "macpict") || eq(w, "pmmetafile")) {
        r->st.dest = DEST_SKIP;
        return;
    }
    if (eq(w, "field")) {
        if (r->field_count < 4) {
            RTF_FIELD *f = &r->fields[r->field_count++];
            memset(f, 0, sizeof(*f));
            r->st.field = r->field_count;
        }
        r->st.dest = DEST_FIELD;
        return;
    }
    if (eq(w, "fldinst")) {
        r->st.dest = DEST_FLDINST;
        return;
    }
    if (eq(w, "fldrslt")) {
        if (r->field_count) {
            RTF_FIELD *f = &r->fields[r->field_count - 1];
            f->inst[f->inst_len] = 0;
            const char *s = f->inst;
            while (*s == ' ')
                s++;
            static const char word[] = "hyperlink";
            UINT i = 0;
            while (i < 9 && s[i] && (s[i] | 0x20) == word[i])
                i++;
            f->hyperlink = i == 9 && (s[9] == ' ' || s[9] == 0 || s[9] == '"');
            f->result_start = r->n;
        }
        r->st.dest = DEST_FIELD;
        r->st.field_result = TRUE;
        return;
    }
    if (eq(w, "pn")) {
        r->st.dest = DEST_PN;
        r->pn_set = TRUE;
        r->pn_numbering = PFN_BULLET;
        r->pn_style = 0;
        r->pn_start = 1;
        r->pn_tab = 0;
        return;
    }
    if (r->st.dest == DEST_FONTTBL) {
        RTF_FONT *f = r->font_count ? &r->fonts[r->font_count - 1] : nullptr;
        if (eq(w, "f") && has) {
            if (r->font_count == r->font_cap) {
                int cap = r->font_cap ? r->font_cap * 2 : 8;
                RTF_FONT *fonts = ReRealloc(r->fonts, cap * sizeof(RTF_FONT));
                if (!fonts)
                    return;
                r->fonts = fonts;
                r->font_cap = cap;
            }
            f = &r->fonts[r->font_count++];
            memset(f, 0, sizeof(*f));
            f->id = (int)v;
            f->charset = DEFAULT_CHARSET;
            r->st.table_font = TRUE;
            return;
        }
        if (!f)
            return;
        if (eq(w, "fcharset"))
            f->charset = (BYTE)v;
        else if (eq(w, "cpg"))
            f->cp = (UINT)v;
        else if (eq(w, "fprq"))
            f->pitch = (BYTE)((f->pitch & 0xF0) | (v & 0x0F));
        else if (eq(w, "fnil"))
            f->pitch = (BYTE)((f->pitch & 0x0F) | FF_DONTCARE);
        else if (eq(w, "froman"))
            f->pitch = (BYTE)((f->pitch & 0x0F) | FF_ROMAN);
        else if (eq(w, "fswiss"))
            f->pitch = (BYTE)((f->pitch & 0x0F) | FF_SWISS);
        else if (eq(w, "fmodern"))
            f->pitch = (BYTE)((f->pitch & 0x0F) | FF_MODERN);
        else if (eq(w, "fscript"))
            f->pitch = (BYTE)((f->pitch & 0x0F) | FF_SCRIPT);
        else if (eq(w, "fdecor"))
            f->pitch = (BYTE)((f->pitch & 0x0F) | FF_DECORATIVE);
        else if (eq(w, "ftech") && f->charset == DEFAULT_CHARSET)
            f->charset = SYMBOL_CHARSET;
        return;
    }
    if (r->st.dest == DEST_COLORTBL) {
        if (eq(w, "red"))
            r->r = (BYTE)v, r->rgb_set = TRUE;
        else if (eq(w, "green"))
            r->g = (BYTE)v, r->rgb_set = TRUE;
        else if (eq(w, "blue"))
            r->b = (BYTE)v, r->rgb_set = TRUE;
        return;
    }
    if (r->st.dest == DEST_PN) {
        if (eq(w, "pnlvlblt"))
            r->pn_numbering = PFN_BULLET;
        else if (eq(w, "pndec"))
            r->pn_numbering = PFN_ARABIC;
        else if (eq(w, "pnlcltr"))
            r->pn_numbering = PFN_LCLETTER;
        else if (eq(w, "pnucltr"))
            r->pn_numbering = PFN_UCLETTER;
        else if (eq(w, "pnlcrm"))
            r->pn_numbering = PFN_LCROMAN;
        else if (eq(w, "pnucrm"))
            r->pn_numbering = PFN_UCROMAN;
        else if (eq(w, "pnstart"))
            r->pn_start = (WORD)v;
        else if (eq(w, "pnindent"))
            r->pn_tab = (WORD)v;
        return;
    }
    /* the document. */
    if (eq(w, "rtf") || eq(w, "urtf")) {
        if (eq(w, "urtf"))
            r->doc_cp = CP_UTF8, r->st.cp = CP_UTF8;
        return;
    }
    if (eq(w, "ansi")) {
        r->doc_cp = 1252;
        r->st.cp = 1252;
    } else if (eq(w, "mac")) {
        r->doc_cp = 10000;
        r->st.cp = 10000;
    } else if (eq(w, "pc")) {
        r->doc_cp = 437;
        r->st.cp = 437;
    } else if (eq(w, "pca")) {
        r->doc_cp = 850;
        r->st.cp = 850;
    } else if (eq(w, "ansicpg")) {
        if (r->doc_cp != CP_UTF8) {
            r->doc_cp = (UINT)v;
            r->st.cp = (UINT)v;
        }
    } else if (eq(w, "deff")) {
        if (v >= 0)
            r->deff = (int)v;
    } else if (eq(w, "deflang")) {
        cf->lcid = (LCID)v;
    } else if (eq(w, "uc")) {
        if (v >= 0 && v <= 2)
            r->st.uc = (int)v;
    } else if (eq(w, "par") || eq(w, "sect")) {
        emit_para(r, RE_CR);
    } else if (eq(w, "line")) {
        emit_para(r, RE_VT);
    } else if (eq(w, "page")) {
        emit_para(r, RE_FF);
    } else if (eq(w, "row")) {
        emit_para(r, RE_CR);
    } else if (eq(w, "cell")) {
        emit(r, '\t');
    } else if (eq(w, "pard")) {
        set_pard(r);
    } else if (eq(w, "plain")) {
        set_plain(r);
    } else if (eq(w, "f")) {
        if (v == -1 && has)
            r->st.dest = DEST_SKIP;
        else
            select_font(r, (int)v);
    } else if (eq(w, "fs")) {
        if (v > 0)
            cf->yHeight = v * 10;
    } else if (eq(w, "b")) {
        toggle(r, CFE_BOLD, has, v);
    } else if (eq(w, "i")) {
        toggle(r, CFE_ITALIC, has, v);
    } else if (eq(w, "ul")) {
        underline(r, CFU_UNDERLINE, has, v);
    } else if (eq(w, "ulnone")) {
        cf->dwEffects &= ~CFE_UNDERLINE;
        cf->bUnderlineType = CFU_UNDERLINENONE;
    } else if (eq(w, "ulw")) {
        underline(r, CFU_UNDERLINEWORD, has, v);
    } else if (eq(w, "uldb")) {
        underline(r, CFU_UNDERLINEDOUBLE, has, v);
    } else if (eq(w, "uld")) {
        underline(r, CFU_UNDERLINEDOTTED, has, v);
    } else if (eq(w, "uldash")) {
        underline(r, CFU_UNDERLINEDASH, has, v);
    } else if (eq(w, "uldashd")) {
        underline(r, CFU_UNDERLINEDASHDOT, has, v);
    } else if (eq(w, "uldashdd")) {
        underline(r, CFU_UNDERLINEDASHDOTDOT, has, v);
    } else if (eq(w, "ulwave")) {
        underline(r, CFU_UNDERLINEWAVE, has, v);
    } else if (eq(w, "ulth")) {
        underline(r, CFU_UNDERLINETHICK, has, v);
    } else if (eq(w, "ulhair")) {
        underline(r, CFU_UNDERLINEHAIRLINE, has, v);
    } else if (eq(w, "strike")) {
        toggle(r, CFE_STRIKEOUT, has, v);
    } else if (eq(w, "protect")) {
        toggle(r, CFE_PROTECTED, has, v);
    } else if (eq(w, "v")) {
        toggle(r, CFE_HIDDEN, has, v);
    } else if (eq(w, "scaps")) {
        toggle(r, CFE_SMALLCAPS, has, v);
    } else if (eq(w, "caps")) {
        toggle(r, CFE_ALLCAPS, has, v);
    } else if (eq(w, "outl")) {
        toggle(r, CFE_OUTLINE, has, v);
    } else if (eq(w, "shad")) {
        toggle(r, CFE_SHADOW, has, v);
    } else if (eq(w, "embo")) {
        toggle(r, CFE_EMBOSS, has, v);
    } else if (eq(w, "impr")) {
        toggle(r, CFE_IMPRINT, has, v);
    } else if (eq(w, "disabled")) {
        toggle(r, CFE_DISABLED, has, v);
    } else if (eq(w, "revised")) {
        toggle(r, CFE_REVISED, has, v);
    } else if (eq(w, "sub")) {
        cf->dwEffects = (cf->dwEffects & ~CFE_SUPERSCRIPT) | CFE_SUBSCRIPT;
    } else if (eq(w, "super")) {
        cf->dwEffects = (cf->dwEffects & ~CFE_SUBSCRIPT) | CFE_SUPERSCRIPT;
    } else if (eq(w, "nosupersub")) {
        cf->dwEffects &= ~(CFE_SUBSCRIPT | CFE_SUPERSCRIPT);
    } else if (eq(w, "up")) {
        cf->yOffset = (has ? v : 6) * 10;
    } else if (eq(w, "dn")) {
        cf->yOffset = -(has ? v : 6) * 10;
    } else if (eq(w, "cf")) {
        set_color(r, (int)v, FALSE);
    } else if (eq(w, "highlight") || eq(w, "chcbpat")) {
        set_color(r, (int)v, TRUE);
    } else if (eq(w, "lang")) {
        cf->lcid = (LCID)v;
    } else if (eq(w, "expndtw")) {
        cf->sSpacing = (SHORT)v;
    } else if (eq(w, "kerning")) {
        cf->wKerning = (WORD)(v * 10);
    } else if (eq(w, "li")) {
        r->st.li = v;
    } else if (eq(w, "ri")) {
        r->st.pf.dxRightIndent = v;
    } else if (eq(w, "fi")) {
        r->st.fi = v;
    } else if (eq(w, "ql")) {
        r->st.pf.wAlignment = PFA_LEFT;
    } else if (eq(w, "qr")) {
        r->st.pf.wAlignment = PFA_RIGHT;
    } else if (eq(w, "qc")) {
        r->st.pf.wAlignment = PFA_CENTER;
    } else if (eq(w, "qj")) {
        r->st.pf.wAlignment = PFA_JUSTIFY;
    } else if (eq(w, "sb")) {
        r->st.pf.dySpaceBefore = v;
    } else if (eq(w, "sa")) {
        r->st.pf.dySpaceAfter = v;
    } else if (eq(w, "sl")) {
        if (v == 0 || v == 1000) {
            r->st.pf.bLineSpacingRule = 0;
            r->st.pf.dyLineSpacing = 0;
        } else {
            r->st.pf.bLineSpacingRule = v > 0 ? 3 : 4;
            r->st.pf.dyLineSpacing = v > 0 ? v : -v;
        }
    } else if (eq(w, "slmult")) {
        if (v && r->st.pf.dyLineSpacing) {
            r->st.pf.bLineSpacingRule = 5;
            r->st.pf.dyLineSpacing /= 12;
        }
    } else if (eq(w, "tqc")) {
        r->tab_align = 1;
    } else if (eq(w, "tqr")) {
        r->tab_align = 2;
    } else if (eq(w, "tqdec")) {
        r->tab_align = 3;
    } else if (eq(w, "tb")) {
        r->tab_align = 4;
    } else if (eq(w, "tldot")) {
        r->tab_leader = 1;
    } else if (eq(w, "tlhyph")) {
        r->tab_leader = 2;
    } else if (eq(w, "tlul")) {
        r->tab_leader = 3;
    } else if (eq(w, "tlth")) {
        r->tab_leader = 4;
    } else if (eq(w, "tleq")) {
        r->tab_leader = 5;
    } else if (eq(w, "tx")) {
        if (r->st.pf.cTabCount < MAX_TAB_STOPS && v >= 0 && v < 0x1000000)
            r->st.pf.rgxTabs[r->st.pf.cTabCount++] =
                (v & 0xFFFFFF) | ((LONG)(r->tab_align + 16 * r->tab_leader) << 24);
        r->tab_align = r->tab_leader = 0;
    } else if (eq(w, "rtlpar")) {
        r->st.pf.wEffects |= PFE_RTLPARA;
    } else if (eq(w, "ltrpar")) {
        r->st.pf.wEffects &= ~PFE_RTLPARA;
    } else if (eq(w, "keep")) {
        r->st.pf.wEffects |= PFE_KEEP;
    } else if (eq(w, "keepn")) {
        r->st.pf.wEffects |= PFE_KEEPNEXT;
    } else if (eq(w, "pagebb")) {
        r->st.pf.wEffects |= PFE_PAGEBREAKBEFORE;
    } else if (star) {
        /* an unknown destination marked ignorable skips its group. */
        r->st.dest = DEST_SKIP;
    }
}

/* ------------------------------------------------------------------ the lexer */

static int hex(BYTE c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static BOOL letter(BYTE c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static void control(RTF *r)
{
    if (r->p >= r->end) {
        r->error = (LONG)0x80070026;
        return;
    }
    BYTE c = *r->p++;
    if (letter(c)) {
        char word[40];
        int n = 0;
        word[n++] = (char)(c | 0x20);
        while (r->p < r->end && letter(*r->p)) {
            if (n >= 32) {
                r->error = -7;
                return;
            }
            word[n++] = (char)(*r->p++ | 0x20);
        }
        word[n] = 0;
        BOOL has = FALSE, negative = FALSE;
        LONG v = 0;
        if (r->p < r->end && *r->p == '-') {
            negative = TRUE;
            r->p++;
        }
        while (r->p < r->end && *r->p >= '0' && *r->p <= '9') {
            has = TRUE;
            v = v * 10 + (*r->p++ - '0');
        }
        if (negative)
            v = -v;
        if (negative && !has)
            has = TRUE;
        if (r->p < r->end && *r->p == ' ')
            r->p++;
        keyword(r, word, has, v);
        return;
    }
    switch (c) {
    case '\'': {
        int a = r->p < r->end ? hex(r->p[0]) : -1;
        int b = r->p + 1 < r->end ? hex(r->p[1]) : -1;
        if (a < 0 || b < 0) {
            r->error = -14;
            return;
        }
        r->p += 2;
        BYTE value = (BYTE)(a * 16 + b);
        if ((value == '\r' || value == '\n') && r->st.dest == DEST_TEXT && !r->skip) {
            flush_bytes(r);
            emit_para(r, RE_CR);
            return;
        }
        r->star = FALSE;
        add_byte(r, value);
        return;
    }
    case '*':
        r->star = TRUE;
        return;
    case '\r':
    case '\n':
        if (r->skip > 0) {
            r->skip--;
            return;
        }
        flush_bytes(r);
        emit_para(r, RE_CR);
        return;
    case '~':
        add_byte(r, ' ');
        return;
    case '_':
        add_byte(r, '-');
        return;
    case '-':
        if (r->skip > 0) {
            r->skip--;
            return;
        }
        flush_bytes(r);
        emit(r, 0x00AD);
        return;
    case ':':
    case '|':
        return;
    case '{':
    case '}':
    case '\\':
        add_byte(r, c);
        return;
    default:
        add_byte(r, c);
        return;
    }
}

static void parse(RTF *r)
{
    while (r->p < r->end && !r->error && !r->stop && !r->ended) {
        BYTE c = *r->p++;
        switch (c) {
        case '{':
            flush_bytes(r);
            r->skip = 0;
            if (!push_state(r))
                r->error = -9;
            break;
        case '}':
            flush_bytes(r);
            r->skip = 0;
            r->star = FALSE;
            pop_state(r);
            break;
        case '\\':
            control(r);
            break;
        case '\r':
        case '\n':
            break;
        case 0:
            add_byte(r, ' ');
            break;
        default:
            r->star = FALSE;
            add_byte(r, c);
            break;
        }
    }
    flush_bytes(r);
    if (!r->error && !r->stop && !r->ended)
        r->error = (LONG)0x80070026;
}

LONG ReReadRtf(RE *e, const char *data, UINT size, UINT cp, UINT count, LONG *error)
{
    *error = 0;
    const BYTE *p = (const BYTE *)data, *end = p + size;
    if (size < 5 || p[0] != '{' || p[1] != '\\' ||
        !((p[2] == 'r' && size >= 5 && p[3] == 't' && p[4] == 'f') ||
          (p[2] == 'u' && size >= 6 && p[3] == 'r' && p[4] == 't' && p[5] == 'f'))) {
        *error = -16;
        return 0;
    }
    RTF *r = ReAlloc(sizeof(RTF));
    if (!r) {
        *error = -9;
        return 0;
    }
    r->e = e;
    r->p = p + 1;
    r->end = end;
    r->deff = 0;
    r->doc_cp = 1252;
    r->whole = cp == 0 && count >= e->len - 1;
    WORD start_cf = r->whole ? e->def_cf : (cp < e->len ? ReInsertFormat(e) : e->def_cf);
    memcpy(&r->st.fmt, &e->fmts[start_cf], sizeof(r->st.fmt));
    r->st.fmt.autolink = 0;
    WORD start_pf = r->whole ? e->def_pf : e->pfi[ReParaEnd(e, cp + count)];
    memcpy(&r->st.pf, &e->paras[start_pf], sizeof(r->st.pf));
    r->st.li = r->st.pf.dxStartIndent + r->st.pf.dxOffset;
    r->st.fi = -r->st.pf.dxOffset;
    r->st.font = -1;
    r->st.cp = 1252;
    r->st.uc = 1;
    r->st.dest = DEST_TEXT;
    parse(r);
    while (r->field_count)
        end_field(r);
    /* the last paragraph's format, and every character's paragraph format. */
    WORD last_pf = current_pf(r);
    BOOL merge = r->whole && !r->error && r->n && r->text[r->n - 1] == RE_CR;
    if (merge) {
        last_pf = r->pf[r->n - 1];
        r->n--;
    }
    WORD tail_pf = r->whole ? last_pf : e->pfi[ReParaEnd(e, cp + count)];
    WORD current = tail_pf;
    for (UINT i = r->n; i-- > 0;) {
        if (r->text[i] == RE_CR)
            current = r->pf[i];
        r->pf[i] = current;
    }
    UINT n = r->n;
    if (grow(r, 1))
        ReReplace(e, cp, count, r->text, n, r->cf, r->pf, UNDO_NONE);
    if (r->whole) {
        UINT last = e->len - 1;
        UINT ps = ReParaStart(e, last);
        for (UINT i = ps; i <= last; i++)
            e->pfi[i] = last_pf;
        if (merge || !n)
            e->cfi[last] = r->cf && n ? r->cf[n - 1] : e->cfi[last];
        e->pend_change = TRUE;
    }
    if (r->error && r->error != -16)
        *error = r->error;
    LONG result = (LONG)n;
    ReFree(r->stack);
    ReFree(r->fonts);
    ReFree(r->colors);
    ReFree(r->auto_colors);
    ReFree(r->text);
    ReFree(r->cf);
    ReFree(r->pf);
    ReFree(r);
    return result;
}

/* ------------------------------------------------------------------ EM_STREAMIN */

static BYTE *read_all(EDITSTREAM *es, UINT chunk, UINT *size)
{
    UINT cap = 0, n = 0;
    BYTE *data = nullptr;
    es->dwError = 0;
    for (;;) {
        if (n + chunk + 1 > cap) {
            UINT next = cap ? cap * 2 : chunk * 2 + 1;
            while (next < n + chunk + 1)
                next *= 2;
            BYTE *d = ReRealloc(data, next);
            if (!d)
                break;
            data = d;
            cap = next;
        }
        LONG got = 0;
        DWORD r = es->pfnCallback(es->dwCookie, data + n, (LONG)chunk, &got);
        es->dwError = r;
        if (r || got <= 0)
            break;
        n += (UINT)got;
    }
    if (data)
        data[n] = 0;
    *size = n;
    return data;
}

LONG ReStreamIn(RE *e, UINT flags, EDITSTREAM *es)
{
    if (!es || !es->pfnCallback)
        return 0;
    BOOL rtf = (flags & SF_RTF) != 0;
    if (rtf && e->plain)
        return 0;
    if (!rtf && !(flags & SF_TEXT))
        return 0;
    BOOL sel = (flags & SFF_SELECTION) != 0;
    UINT from = sel ? ReSelMin(e) : 0, to = sel ? ReSelMax(e) : e->len - 1;
    if (to > e->len - 1)
        to = e->len - 1;
    if (!sel)
        ReClearUndo(e);
    UINT size = 0;
    BYTE *data = read_all(es, rtf ? 4092 : 4094, &size);
    LONG result = 0;
    if (rtf) {
        LONG error = 0;
        result = ReReadRtf(e, data ? (const char *)data : "", size, from, to - from, &error);
        if (!es->dwError && error)
            es->dwError = (DWORD)error;
        if (!sel)
            e->pend_change = TRUE;
    } else if (data) {
        UINT cp = (flags & SF_UNICODE) ? 1200 : (flags & SF_USECODEPAGE) ? HIWORD(flags) : CP_ACP;
        const BYTE *p = data;
        UINT n = size;
        if (cp != 1200 && n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
            cp = CP_UTF8;
            p += 3;
            n -= 3;
        }
        WCHAR *w;
        UINT count;
        if (cp == 1200) {
            count = n / 2;
            w = ReAlloc(((SIZE_T)count + 1) * sizeof(WCHAR));
            if (w)
                memcpy(w, p, count * sizeof(WCHAR));
        } else {
            int k = n ? MultiByteToWideChar(cp, 0, (const char *)p, (int)n, nullptr, 0) : 0;
            w = ReAlloc(((SIZE_T)k + 1) * sizeof(WCHAR));
            if (w && k)
                MultiByteToWideChar(cp, 0, (const char *)p, (int)n, w, k);
            count = (UINT)(k > 0 ? k : 0);
        }
        if (w) {
            UINT k = 0;
            for (UINT i = 0; i < count; i++) {
                WCHAR c = w[i];
                if (c == '\r' && i + 1 < count && w[i + 1] == '\n')
                    i++;
                if (c == '\n')
                    c = '\r';
                if (c == '\r' && !e->multiline)
                    break;
                w[k++] = c;
            }
            WORD cf = sel ? ReInsertFormat(e) : e->cfi[0];
            if (e->fmts[cf].autolink) {
                RE_FMT f;
                memcpy(&f, &e->fmts[cf], sizeof(f));
                f.autolink = 0;
                cf = ReAddFormat(e, &f);
            }
            ReReplaceText(e, from, to - from, w, k, cf, UNDO_NONE);
            ReFree(w);
        }
        result = (LONG)size;
    }
    ReFree(data);
    if (e->len - 1 > e->limit)
        e->limit = e->len - 1;
    ReScanUrls(e);
    if (sel) {
        UINT at = rtf ? from + (UINT)result : e->active;
        ReSetSelection(e, at, at);
    } else {
        e->yscroll = e->xscroll = 0;
        ReSetSelection(e, 0, 0);
    }
    return result;
}
