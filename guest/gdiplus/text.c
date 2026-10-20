/*
 * text: laying out a string in a rectangle and drawing, measuring or turning it into a path. the layout works in
 * units of an em split in 2048 parts like gdi+ does, with the advances of the glyphs widened by the tracking of the
 * format and rounded to whole units. a single line that fits is laid out in the design units of the font instead,
 * which is what gdi+ does for it too and gives the same numbers on fonts of 2048 units.
 */
#include "text_private.h"

#define NO_LIMIT 16777216.0f

/* the rows are the inverse of the text contrast tables, see text_contrast.inc. */
static const UINT8 contrast_inv[12 * 256] = {
#include "text_contrast.inc"
};

/* ------------------------------------------------------------------ characters */

enum { CK_NORMAL, CK_SPACE, CK_TAB, CK_NEWLINE, CK_HIDDEN, CK_TRAIL };

typedef struct Char {
    GFace *face;
    UINT16 glyph;
    UINT8 kind;
    UINT8 level;    /* bidirectional level. */
    float adv;      /* in layout units. */
} Char;

typedef struct Line {
    INT start, end;     /* the characters the line takes, a newline included. */
    INT shown_end;      /* characters from start that are drawn: before the cut and the trailing spaces. */
    float width;        /* of the shown characters and an ellipsis. */
    float ox;           /* the shift of the alignment. */
    float top;
    float height, ascent, descent;
    INT trim;           /* the trimming that cut the line, 0 when it fits. */
    BOOL ellipsis;
    INT cut_from, cut_to;   /* characters not shown in the middle of a path ellipsis. */
    float ell_x;
    INT consumed;       /* the characters counted as fitted. */
} Line;

typedef struct Layout {
    const GStrFmt *fmt;
    GFamily *family;
    GFace *face;
    INT style;
    float size;
    BOOL fast, vertical, rtl, bidi, null_format;
    float grid, S, q;
    INT flags, trimming;
    WCHAR *text;
    INT len;
    Char *ch;
    float *pre;
    float *xs;
    INT *vis;
    INT *hot;
    INT hot_count;
    Line *lines;
    INT nlines, cap_lines;
    float margin_l, margin_r;
    float limit;
    float lw, lh;               /* the rectangle in layout units, swapped for vertical text. */
    float lw_world, lh_world;
    float height_lu;
    float left, right;
    INT chars_fitted;
    BOOL ell_ready;
    GFace *ell_face;
    UINT16 ell_glyph[3];
    INT ell_n;
    float ell_adv[3];
    float ell_width;
} Layout;

static float rnd(const Layout *L, float v)
{
    return L->fast ? v : mime_vec_floorf(v + 0.5f);
}

static BOOL is_newline(WCHAR c)
{
    return c == 0x0A || c == 0x0D || c == 0x85 || c == 0x2028 || c == 0x2029;
}

/* the spaces a line may break after and that hang over its end when they do not fit. */
static BOOL is_break_space(WCHAR c)
{
    return c == 0x20 || c == 0x1680 || (c >= 0x2000 && c <= 0x2006) || (c >= 0x2008 && c <= 0x200A) || c == 0x205F ||
           c == 0x3000;
}

/* controls and formatting characters, which have no width and no mark. */
static BOOL is_hidden(WCHAR c)
{
    if (c < 0x20)
        return c != 0x09 && c != 0x0A && c != 0x0D;
    return (c >= 0x7F && c <= 0x9F && c != 0x85) || c == 0xAD || (c >= 0x200B && c <= 0x200F) ||
           (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x206F) || c == 0xFEFF || c == 0xFFFF;
}

static BOOL is_cjk(UINT32 c)
{
    return (c >= 0x2E80 && c <= 0x303E) || (c >= 0x3041 && c <= 0x33FF) || (c >= 0x3400 && c <= 0x4DBF) ||
           (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xA000 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7AF) ||
           (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFFEF) ||
           (c >= 0x20000 && c <= 0x3FFFF);
}

/* characters that may not start a line of ideographs, and that may not end one. */
static BOOL cjk_no_start(UINT32 c)
{
    static const WCHAR set[] = L"\x3001\x3002\xff0c\xff0e\xff1a\xff1b\xff01\xff1f\xff09\x3009\x300b\x300d\x300f\x3011"
                               L"\x3015\xff3d\xff5d\x30fc\x3005\x309b\x309c\x30fd\x30fe\x309d\x309e";
    for (const WCHAR *p = set; *p; p++)
        if (*p == c)
            return TRUE;
    return FALSE;
}

static BOOL cjk_no_end(UINT32 c)
{
    static const WCHAR set[] = L"\xff08\x3008\x300a\x300c\x300e\x3010\x3014\xff3b\xff5b";
    for (const WCHAR *p = set; *p; p++)
        if (*p == c)
            return TRUE;
    return FALSE;
}

static BOOL is_alnum(WCHAR c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0xC0;
}

/* scripts that need shaping or reordering, where gdi+ takes the general path through the layout. */
static BOOL is_complex(UINT32 c)
{
    return c < 0x20 || (c >= 0x0300 && c <= 0x036F) || (c >= 0x0590 && c <= 0x109F) || (c >= 0x1780 && c <= 0x18AF) ||
           (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) || (c >= 0x2028 && c <= 0x2029) ||
           (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xD800 && c <= 0xDFFF) || (c >= 0xFB1D && c <= 0xFDFF) ||
           (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xFE70 && c <= 0xFEFF) || c == 0x85;
}

/* the break opportunity after the character at i, which is the position i + 1 as the start of the next line. */
static BOOL break_after(const Layout *L, INT i)
{
    const WCHAR *t = L->text;
    WCHAR c = t[i];
    INT next = i + 1;
    if (next >= L->len)
        return TRUE;
    if (L->ch[i].kind == CK_SPACE || L->ch[i].kind == CK_TAB)
        return TRUE;
    if (c == 0x200B)
        return TRUE;
    WCHAR n = t[next];
    if (c == '-' || c == 0x2010 || c == 0x2013) {
        if (i > 0 && is_alnum(t[i - 1]) && n != ' ' && !(n >= '0' && n <= '9') && !is_newline(n))
            return TRUE;
        return FALSE;
    }
    if (c == 0x2014)
        return TRUE;
    if (n == 0x2014)
        return TRUE;
    UINT32 a = c, b = n;
    if (c >= 0xD800 && c < 0xDC00 && n >= 0xDC00 && n < 0xE000)
        return FALSE;
    if (is_cjk(a) && is_cjk(b) && !cjk_no_start(b) && !cjk_no_end(a))
        return TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ setting a layout up */

static void layout_free(Layout *L)
{
    gp_free(L->text);
    gp_free(L->ch);
    gp_free(L->pre);
    gp_free(L->xs);
    gp_free(L->vis);
    gp_free(L->hot);
    gp_free(L->lines);
    memset(L, 0, sizeof(*L));
}

static GFace *fallback_face(Layout *L, UINT32 cp, UINT16 *glyph)
{
    GFamily *f = gp_family_fallback(cp, L->family);
    if (!f)
        return nullptr;
    GFace *face = gp_family_face(f, L->style);
    if (!face)
        return nullptr;
    UINT16 g = gp_face_glyph(face, cp);
    if (!g)
        return nullptr;
    *glyph = g;
    return face;
}

/*
 * the characters of the string: the glyph each takes, from the font or a fallback, and its advance. the
 * advance of a glyph is its design advance widened by the tracking, in whole layout units.
 */
static GpStatus layout_chars(Layout *L, const WCHAR *src, INT len)
{
    L->len = len;
    L->text = gp_alloc(sizeof(WCHAR) * (SIZE_T)(len + 1));
    L->ch = gp_alloc_zero(sizeof(Char) * (SIZE_T)(len + 1));
    L->pre = gp_alloc(sizeof(float) * (SIZE_T)(len + 2));
    L->xs = gp_alloc(sizeof(float) * (SIZE_T)(len + 2));
    L->vis = gp_alloc(sizeof(INT) * (SIZE_T)(len + 2));
    if (!L->text || !L->ch || !L->pre || !L->xs || !L->vis)
        return OutOfMemory;
    memcpy(L->text, src, sizeof(WCHAR) * (SIZE_T)len);
    L->text[len] = 0;
    /* the ampersand of a hotkey is not shown, and with the show prefix the character after it is underlined. */
    if (L->fmt->hotkey) {
        for (INT i = 0; i < len; i++) {
            if (L->text[i] != '&')
                continue;
            L->text[i] = 0xFFFF;
            i++;
            if (L->fmt->hotkey == HotkeyPrefixShow && i < len && L->text[i] != '&') {
                INT *hot = gp_realloc(L->hot, sizeof(INT) * (SIZE_T)(L->hot_count + 1));
                if (!hot)
                    return OutOfMemory;
                L->hot = hot;
                L->hot[L->hot_count++] = i - 1;
            }
        }
    }
    float tracking = L->fmt->tracking;
    GFace *primary = L->face;
    for (INT i = 0; i < len; i++) {
        Char *c = &L->ch[i];
        WCHAR w = L->text[i];
        UINT32 cp = w;
        c->face = primary;
        if (w >= 0xDC00 && w < 0xE000 && i > 0 && L->text[i - 1] >= 0xD800 && L->text[i - 1] < 0xDC00) {
            c->kind = CK_TRAIL;
            continue;
        }
        if (w >= 0xD800 && w < 0xDC00 && i + 1 < len && L->text[i + 1] >= 0xDC00 && L->text[i + 1] < 0xE000)
            cp = 0x10000 + (((UINT32)w - 0xD800) << 10) + (L->text[i + 1] - 0xDC00);
        if (is_newline(w)) {
            c->kind = CK_NEWLINE;
            continue;
        }
        if (w == 0x09) {
            c->kind = CK_TAB;
            continue;
        }
        if (is_hidden(w)) {
            c->kind = CK_HIDDEN;
            continue;
        }
        c->kind = is_break_space(w) ? CK_SPACE : CK_NORMAL;
        UINT16 glyph = gp_face_glyph(primary, cp);
        if (!glyph && !primary->symbol && !(L->flags & StringFormatFlagsNoFontFallback) && c->kind == CK_NORMAL) {
            UINT16 g = 0;
            GFace *fb = fallback_face(L, cp, &g);
            if (fb) {
                c->face = fb;
                glyph = g;
            }
        }
        c->glyph = glyph;
        GFace *face = c->face;
        float a6, adv = (float)gp_face_advance(face, glyph);
        if (L->fast) {
            a6 = tracking;
        } else {
            float q = (L->size / (float)face->upem) * L->S;
            a6 = q * tracking;
        }
        c->adv = mime_vec_floorf((float)((double)adv * (double)a6) + 0.5f);
    }
    return Ok;
}

/* the bidirectional levels, for text with strong right to left characters or a right to left format. */
static BOOL is_strong_r(UINT32 c)
{
    return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF) ||
           (c >= 0x10800 && c <= 0x10FFF);
}

static BOOL is_strong_l(UINT32 c)
{
    if (is_strong_r(c))
        return FALSE;
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c != 0xD7 && c != 0xF7 && c < 0x2000) ||
           (c >= 0x3040 && c < 0xFB00 && !(c >= 0xFB1D));
}

static void layout_levels(Layout *L)
{
    INT base = L->rtl ? 1 : 0;
    BOOL any_r = FALSE;
    for (INT i = 0; i < L->len; i++)
        if (is_strong_r(L->text[i]))
            any_r = TRUE;
    L->bidi = L->rtl || any_r;
    if (!L->bidi)
        return;
    /* strong characters first, then the neutrals take the direction of their neighbours or the base direction. */
    INT n = L->len;
    UINT8 *dir = gp_alloc_zero((SIZE_T)n + 1);
    if (!dir) {
        L->bidi = FALSE;
        return;
    }
    enum { D_N = 0, D_L = 1, D_R = 2, D_EN = 3 };
    for (INT i = 0; i < n; i++) {
        WCHAR c = L->text[i];
        if (is_strong_r(c))
            dir[i] = D_R;
        else if (c >= '0' && c <= '9')
            dir[i] = D_EN;
        else if (is_strong_l(c))
            dir[i] = D_L;
    }
    /* digits after a right to left character count as part of it. */
    UINT8 last_strong = base ? D_R : D_L;
    for (INT i = 0; i < n; i++) {
        if (dir[i] == D_L || dir[i] == D_R)
            last_strong = dir[i];
        else if (dir[i] == D_EN && last_strong == D_L)
            dir[i] = D_L;
    }
    for (INT i = 0; i < n;) {
        if (dir[i] != D_N) {
            i++;
            continue;
        }
        INT j = i;
        while (j < n && dir[j] == D_N)
            j++;
        UINT8 before = i > 0 ? dir[i - 1] : (base ? D_R : D_L);
        UINT8 after = j < n ? dir[j] : (base ? D_R : D_L);
        if (before == D_EN)
            before = D_R;
        if (after == D_EN)
            after = D_R;
        UINT8 d = before == after ? before : (base ? D_R : D_L);
        for (INT k = i; k < j; k++)
            L->ch[k].level = (UINT8)(d == D_R ? 1 : (base ? 2 : 0));
        i = j;
    }
    for (INT i = 0; i < n; i++) {
        if (dir[i] == D_N)
            continue;
        L->ch[i].level = (UINT8)(dir[i] == D_R ? 1 : (dir[i] == D_EN ? 2 : (base ? 2 : 0)));
    }
    gp_free(dir);
}

/* ------------------------------------------------------------------ lines */

static INT par_end(const Layout *L, INT start)
{
    INT i = start;
    while (i < L->len && L->ch[i].kind != CK_NEWLINE)
        i++;
    return i;
}

/* a carriage return and a line feed make one break. */
static INT newline_len(const Layout *L, INT pe)
{
    if (pe >= L->len)
        return 0;
    if (L->text[pe] == 0x0D && pe + 1 < L->len && L->text[pe + 1] == 0x0A)
        return 2;
    return 1;
}

/* the next tab stop after x: the stops of the format, then steps of the last one, or four ems without a format. */
static float tab_next(const Layout *L, float x)
{
    const GStrFmt *f = L->fmt;
    INT xi = (INT)x;
    INT inc;
    if (f->tab_count > 0 && f->tabs) {
        float cum = f->first_tab;
        for (INT i = 0; i < f->tab_count; i++) {
            cum += f->tabs[i];
            INT p = (INT)mime_vec_floorf(cum * L->S + 0.5f);
            if (p > xi)
                return (float)p;
        }
        inc = (INT)mime_vec_floorf(f->tabs[f->tab_count - 1] * L->S + 0.5f);
    } else if (L->null_format) {
        inc = (INT)(4.0f * L->grid);
    } else {
        inc = (INT)mime_vec_floorf(f->first_tab * L->S + 0.5f);
    }
    if (inc <= 0)
        inc = 1;
    return (float)(((xi + inc) / inc) * inc);
}

static float char_adv(const Layout *L, INT i, float x)
{
    const Char *c = &L->ch[i];
    if (c->kind == CK_TAB)
        return tab_next(L, x) - x;
    return c->adv;
}

/* the position of every character from from to to, from the start of the line. */
static void measure_range(Layout *L, INT from, INT to)
{
    float x = 0.0f;
    for (INT k = from; k < to; k++) {
        L->pre[k] = x;
        x += char_adv(L, k, x);
    }
    L->pre[to] = x;
}

static BOOL is_trailing(const Char *c)
{
    return c->kind == CK_SPACE || c->kind == CK_TAB || c->kind == CK_HIDDEN;
}

static INT strip_trailing(const Layout *L, INT start, INT end)
{
    while (end > start && is_trailing(&L->ch[end - 1]))
        end--;
    return end;
}

static void line_metrics(const Layout *L, Line *ln)
{
    const GFace *f = L->face;
    if (L->fast) {
        ln->ascent = (float)f->ascent;
        ln->descent = (float)f->descent;
        ln->height = (float)(f->ascent + f->descent);
    } else {
        ln->ascent = rnd(L, (float)f->ascent * L->q);
        ln->descent = rnd(L, (float)f->descent * L->q);
        ln->height = rnd(L, (float)(UINT16)(f->ascent + f->descent + f->gap) * L->q);
    }
}

/* the ellipsis: the glyph of the horizontal ellipsis, or three periods, with their advances. */
static void ellipsis_prepare(Layout *L)
{
    if (L->ell_ready)
        return;
    L->ell_ready = TRUE;
    GFace *f = L->face;
    UINT16 g = gp_face_glyph(f, 0x2026);
    L->ell_n = 1;
    L->ell_glyph[0] = g;
    if (!g) {
        g = gp_face_glyph(f, '.');
        L->ell_n = 3;
        L->ell_glyph[0] = L->ell_glyph[1] = L->ell_glyph[2] = g;
    }
    L->ell_face = f;
    L->ell_width = 0.0f;
    for (INT i = 0; i < L->ell_n; i++) {
        float adv = (float)gp_face_advance(f, L->ell_glyph[i]);
        L->ell_adv[i] = L->fast ? adv : mime_vec_floorf((float)((double)adv * (double)L->q) + 0.5f);
        L->ell_width += L->ell_adv[i];
    }
}

/* a line that wraps: it takes characters until the next one would not fit, and breaks where a word ends. */
static void build_wrap_line(Layout *L, INT start, Line *ln)
{
    memset(ln, 0, sizeof(*ln));
    ln->start = start;
    INT pe = par_end(L, start);
    float limit = L->limit;
    float x = 0.0f;
    INT i = start, brk = -1, end = -1;
    while (i < pe) {
        const Char *c = &L->ch[i];
        float adv = char_adv(L, i, x);
        L->pre[i] = x;
        if (i > start && c->kind != CK_SPACE && c->kind != CK_TAB && c->kind != CK_HIDDEN && c->kind != CK_TRAIL &&
            x + adv > limit) {
            end = brk > start ? brk : i;
            break;
        }
        x += adv;
        i++;
        if (break_after(L, i - 1))
            brk = i;
    }
    if (end < 0) {
        L->pre[pe] = x;
        end = pe + newline_len(L, pe);
    }
    INT shown = end < pe ? end : pe;
    ln->end = end;
    ln->consumed = end - start;
    ln->shown_end = (L->flags & StringFormatFlagsMeasureTrailingSpaces) ? shown : strip_trailing(L, start, shown);
    ln->width = L->pre[ln->shown_end];
    line_metrics(L, ln);
}

/*
 * a line cut at the width the layout allows, the rest of its paragraph dropped. an ellipsis mode puts an ellipsis
 * after what is left, and a last line that is cut for want of room shows one even when it would fit.
 */
static void build_cut_line(Layout *L, INT start, StringTrimming trimming, BOOL force_ellipsis, Line *ln)
{
    memset(ln, 0, sizeof(*ln));
    ln->start = start;
    INT pe = par_end(L, start);
    measure_range(L, start, pe);
    ln->end = pe + newline_len(L, pe);
    ln->consumed = ln->end - start;
    INT se = strip_trailing(L, start, pe);
    float limit = L->limit;
    ln->shown_end = (L->flags & StringFormatFlagsMeasureTrailingSpaces) ? pe : se;
    ln->width = L->pre[ln->shown_end];
    line_metrics(L, ln);
    if (limit >= NO_LIMIT || trimming == StringTrimmingNone)
        return;
    BOOL ell_mode = trimming == StringTrimmingEllipsisCharacter || trimming == StringTrimmingEllipsisWord ||
                    trimming == StringTrimmingEllipsisPath;
    if (ell_mode) {
        ellipsis_prepare(L);
        /* an ellipsis wider than half the line is not worth it. */
        if (trimming != StringTrimmingEllipsisPath && L->ell_width > mime_vec_floorf(limit * 0.5f)) {
            trimming = trimming == StringTrimmingEllipsisCharacter ? StringTrimmingCharacter : StringTrimmingWord;
            ell_mode = FALSE;
        }
    }
    float room = limit - (ell_mode ? L->ell_width : 0.0f);
    if (room < 0.0f)
        room = 0.0f;
    if (!ell_mode ? L->pre[se] <= limit : (!force_ellipsis && L->pre[se] <= limit))
        return;
    if (ell_mode && L->pre[se] <= room) {
        ln->trim = trimming;
        ln->ellipsis = TRUE;
        ln->cut_from = ln->cut_to = se;
        ln->shown_end = se;
        ln->ell_x = L->pre[se];
        ln->width = L->pre[se] + L->ell_width;
        return;
    }
    if (trimming == StringTrimmingEllipsisPath) {
        /* the middle goes: as much of the last segment of the path as fits stays, and the start fills the rest. */
        INT sep = -1;
        for (INT k = se - 1; k >= start; k--) {
            if (L->text[k] == '\\' || L->text[k] == '/') {
                sep = k;
                break;
            }
        }
        INT tail = sep >= start ? sep : se;
        float tail_w = L->pre[se] - L->pre[tail];
        INT head_end = start, tail_from = se;
        if (tail_w <= room) {
            float head_room = room - tail_w;
            while (head_end < tail && L->pre[head_end + 1] <= head_room)
                head_end++;
            tail_from = tail;
        } else {
            while (tail_from > start && L->pre[se] - L->pre[tail_from - 1] <= room)
                tail_from--;
        }
        ln->trim = trimming;
        ln->ellipsis = TRUE;
        ln->cut_from = head_end;
        ln->cut_to = tail_from;
        ln->shown_end = se;
        ln->ell_x = L->pre[head_end];
        ln->width = L->pre[head_end] + (L->pre[se] - L->pre[tail_from]) + L->ell_width;
        return;
    }
    INT j = start;
    while (j < se && L->pre[j + 1] <= room)
        j++;
    if (j == start && !ell_mode && start < se)
        j = start + 1;
    if (trimming == StringTrimmingWord || trimming == StringTrimmingEllipsisWord) {
        INT e = j;
        while (e > start && !(break_after(L, e - 1) && L->pre[strip_trailing(L, start, e)] <= room))
            e--;
        if (e > start)
            j = e;
    }
    INT shown = strip_trailing(L, start, j);
    ln->trim = trimming;
    ln->shown_end = shown;
    ln->width = L->pre[shown];
    if (ell_mode) {
        ln->ellipsis = TRUE;
        ln->cut_from = ln->cut_to = shown;
        ln->ell_x = L->pre[shown];
        ln->width = L->pre[shown] + L->ell_width;
    }
}

/* ------------------------------------------------------------------ the whole layout */

static BOOL push_line(Layout *L, const Line *ln)
{
    if (L->nlines == L->cap_lines) {
        INT cap = L->cap_lines ? L->cap_lines * 2 : 8;
        Line *lines = gp_realloc(L->lines, sizeof(Line) * (SIZE_T)cap);
        if (!lines)
            return FALSE;
        L->lines = lines;
        L->cap_lines = cap;
    }
    L->lines[L->nlines++] = *ln;
    return TRUE;
}

static GpStatus layout_lines(Layout *L)
{
    float room = L->lh_world * L->S;
    float fraction = 0.0f;
    /* a line is kept when the part of it that fits is the whole with a line limit, else a quarter of it. */
    if (room > 0.0f) {
        if (L->flags & StringFormatFlagsLineLimit)
            fraction = 1.0f;
        else if (L->trimming != StringTrimmingNone)
            fraction = 0.25f;
    }
    BOOL nowrap = (L->flags & StringFormatFlagsNoWrap) != 0;
    float used = 0.0f;
    INT pos = 0;
    BOOL truncated = FALSE;
    while (pos < L->len) {
        Line ln;
        if (nowrap)
            build_cut_line(L, pos, L->trimming, FALSE, &ln);
        else
            build_wrap_line(L, pos, &ln);
        if (fraction > 0.0f && ln.height * fraction > room - used) {
            truncated = TRUE;
            break;
        }
        ln.top = used;
        if (!push_line(L, &ln))
            return OutOfMemory;
        used += ln.height;
        pos = ln.end > pos ? ln.end : pos + 1;
    }
    if (truncated && !nowrap && L->trimming != StringTrimmingNone && L->nlines > 0) {
        /* the last line that fits stands for the text that does not: it is cut and may end in an ellipsis. */
        Line *last = &L->lines[L->nlines - 1];
        if (L->trimming == StringTrimmingWord) {
            WCHAR c = L->text[last->end - 1];
            if (c != 0x0A && c != 0x0D) {
                INT pe = par_end(L, last->start);
                last->trim = StringTrimmingWord;
                last->end = pe + newline_len(L, pe);
                last->consumed = last->end - last->start;
            }
        } else {
            Line cut;
            build_cut_line(L, last->start, L->trimming, TRUE, &cut);
            cut.top = last->top;
            *last = cut;
        }
    }
    L->height_lu = 0.0f;
    L->chars_fitted = 0;
    for (INT i = 0; i < L->nlines; i++) {
        L->height_lu += L->lines[i].height;
        L->chars_fitted += L->lines[i].consumed;
    }
    return Ok;
}

/* the shift of every line along its direction by the alignment, and the extent of all of them. */
static void layout_align(Layout *L)
{
    StringAlignment align = L->fmt->align;
    if (L->rtl && align != StringAlignmentCenter)
        align = align == StringAlignmentNear ? StringAlignmentFar : StringAlignmentNear;
    L->left = 3.0e38f;
    L->right = -3.0e38f;
    for (INT i = 0; i < L->nlines; i++) {
        Line *ln = &L->lines[i];
        float total = L->margin_l + ln->width + L->margin_r;
        float room = L->fast ? L->lw - total : (L->lw <= 0.0f ? -total : L->lw - total);
        ln->ox = 0.0f;
        if (align == StringAlignmentCenter)
            ln->ox = L->fast ? room * 0.5f : (float)((INT)room / 2);
        else if (align == StringAlignmentFar)
            ln->ox = room;
        if (ln->ox < L->left)
            L->left = ln->ox;
        if (ln->ox + total > L->right)
            L->right = ln->ox + total;
    }
    if (L->left > L->right)
        L->left = L->right = 0.0f;
}

/* the height of the lines in world units: with the default margins gdi+ adds an eighth of an em below them. */
static float layout_height_world(const Layout *L)
{
    float h = L->height_lu / L->S;
    if (L->null_format || L->fmt->lead_margin != 0.0f)
        h += L->size * 0.125f;
    return h;
}

/* whether the format is one that makes the layout take the general path, decided from what it holds. */
static BOOL fast_text_ok(const Layout *L, const WCHAR *src, INT len)
{
    INT amp = -1;
    BOOL digits = FALSE;
    for (INT i = 0; i < len; i++) {
        UINT32 c = src[i];
        if (is_complex(c) || is_hidden(src[i]) || (c >= 0x200B && c <= 0x200F) || (c >= 0x2060 && c <= 0x206F) ||
            c == 0xFEFF)
            return FALSE;
        if (c >= '0' && c <= '9')
            digits = TRUE;
        if (c == '&' && amp < 0 && L->fmt->hotkey)
            amp = i;
    }
    /* a second ampersand after a hotkey is more than the quick path handles, but "&&" is just one. */
    if (amp >= 0 && amp + 1 < len) {
        for (INT j = amp + 2; j < len; j++)
            if (src[j] == '&')
                return FALSE;
    }
    if (digits && (L->fmt->digit_substitute == StringDigitSubstituteNational ||
                   L->fmt->digit_substitute == StringDigitSubstituteTraditional))
        return FALSE;
    return TRUE;
}

/*
 * lays the string out. fast asks for the quick path of a single line that fits, which is refused with
 * NotImplemented when the string, format or transform need more.
 */
static GpStatus layout_try(Layout *L, const WCHAR *src, INT len, GFamily *family, INT style, float size, float lw,
                           float lh, const GStrFmt *fmt_in, const MimeVecMatrix *dev, BOOL fast)
{
    memset(L, 0, sizeof(*L));
    L->null_format = fmt_in == nullptr;
    L->fmt = fmt_in ? fmt_in : gp_strfmt_default();
    L->family = family;
    L->style = style;
    L->size = size;
    L->flags = L->fmt->flags;
    L->trimming = L->fmt->trimming;
    L->vertical = (L->flags & StringFormatFlagsDirectionVertical) != 0;
    /* a right to left paragraph is one that is not vertical. */
    L->rtl = (L->flags & StringFormatFlagsDirectionRightToLeft) && !L->vertical;
    L->face = gp_family_face(family, style);
    if (!L->face)
        return InvalidParameter;
    L->fast = fast;
    if (fast) {
        if (!dev || !(dev->m11 > 0.0f) || dev->m12 != 0.0f || dev->m21 != 0.0f || dev->m22 == 0.0f)
            return NotImplemented;
        if ((L->flags & (StringFormatFlagsDirectionRightToLeft | StringFormatFlagsDirectionVertical | 0x40000000)) ||
            L->fmt->range_count > 0)
            return NotImplemented;
        if (!fast_text_ok(L, src, len))
            return NotImplemented;
        float scale16 = mime_vec_floorf(size / (float)L->face->upem * dev->m11 * 65536.0f + 0.5f);
        if (scale16 > 65536.0f)
            return NotImplemented;
    }
    L->lw_world = L->vertical ? lh : lw;
    L->lh_world = L->vertical ? lw : lh;
    L->grid = fast ? (float)L->face->upem : 2048.0f;
    L->S = fast ? (float)L->face->upem / size : (float)(2048.0 / (double)size);
    L->q = fast ? 1.0f : (size / (float)L->face->upem) * L->S;
    L->lw = fast ? L->lw_world * L->S : mime_vec_floorf(L->lw_world * L->S + 0.5f);
    L->lh = fast ? 0.0f : mime_vec_floorf(L->lh_world * L->S + 0.5f);
    if (fast) {
        L->margin_l = L->grid * L->fmt->lead_margin;
        L->margin_r = L->grid * L->fmt->trail_margin;
    } else {
        L->margin_l = mime_vec_floorf(size * L->S * L->fmt->lead_margin + 0.5f);
        L->margin_r = mime_vec_floorf(size * L->S * L->fmt->trail_margin + 0.5f);
    }
    BOOL nowrap_plain = (L->flags & StringFormatFlagsNoWrap) && L->trimming == StringTrimmingNone;
    if (fast || L->lw <= 0.0f || nowrap_plain) {
        L->limit = NO_LIMIT;
    } else {
        L->limit = L->lw - L->margin_l - L->margin_r;
        if (L->limit < 0.0f)
            L->limit = 0.0f;
    }
    GpStatus st = layout_chars(L, src, len);
    if (st != Ok)
        return st;
    if (fast && !(L->flags & StringFormatFlagsNoFontFallback) && !L->face->symbol) {
        for (INT i = 0; i < L->len; i++)
            if (L->ch[i].kind == CK_NORMAL && !L->ch[i].glyph)
                return NotImplemented;
    }
    if (!fast)
        layout_levels(L);
    st = layout_lines(L);
    if (st != Ok)
        return st;
    if (fast) {
        /* a line wider than the rectangle needs wrapping or trimming, which the general path does. */
        BOOL limited = !(L->flags & StringFormatFlagsNoWrap) || L->trimming != StringTrimmingNone;
        if (L->nlines != 1)
            return NotImplemented;
        float w = L->lines[0].width / L->S + (L->margin_l + L->margin_r) / L->S;
        if (limited && L->lw_world > 0.0f && w > L->lw_world)
            return NotImplemented;
    }
    layout_align(L);
    return Ok;
}

static GpStatus layout_build(Layout *L, const WCHAR *src, INT len, GFamily *family, INT style, float size, float lw,
                             float lh, const GStrFmt *fmt, const MimeVecMatrix *dev, BOOL allow_fast)
{
    if (allow_fast) {
        GpStatus st = layout_try(L, src, len, family, style, size, lw, lh, fmt, dev, TRUE);
        if (st != NotImplemented)
            return st;
        layout_free(L);
    }
    return layout_try(L, src, len, family, style, size, lw, lh, fmt, dev, FALSE);
}

/* ------------------------------------------------------------------ drawing */

static TextRenderingHint effective_hint(const GGraphics *g)
{
    TextRenderingHint hint = g->text_hint;
    if (hint == TextRenderingHintSystemDefault) {
        BOOL smoothing = FALSE;
        SystemParametersInfoW(0x4A, 0, &smoothing, 0);
        if (smoothing) {
            UINT type = 0;
            SystemParametersInfoW(0x200A, 0, &type, 0);
            hint = (type & 2) ? TextRenderingHintClearTypeGridFit : TextRenderingHintAntiAliasGridFit;
        } else {
            hint = TextRenderingHintSingleBitPerPixelGridFit;
        }
    }
    /* targets of eight bits or fewer take one bit per pixel. */
    if (hint >= TextRenderingHintAntiAliasGridFit && g->target && (g->target->format & 0xFF00) <= 0x800)
        hint = TextRenderingHintSingleBitPerPixelGridFit;
    return hint;
}

typedef struct TextFill {
    GGraphics *g;
    GSource *src;
    GSurface surf;
    const MimeVecMask *clip;
    const MimeVecMask *extra;
    UINT32 *buf;
    UINT8 *cov8;
    UINT8 *mrow;
    INT cap;
    const UINT8 *lut;
    BOOL modified;
    CompositingMode mode;
} TextFill;

static void mask_row(const MimeVecMask *m, INT y, INT x, INT n, UINT8 *cv, UINT8 *scratch)
{
    memset(scratch, 255, (SIZE_T)n);
    mime_vec_mask_apply_row(m, y, x, n, scratch);
    for (INT i = 0; i < n; i++)
        if (scratch[i] != 255)
            cv[i] = (UINT8)((cv[i] * (UINT)scratch[i] + 127) / 255);
}

static void text_span(void *user, INT y, INT x0, INT count, const UINT8 *cov, UINT cmax)
{
    TextFill *t = user;
    INT x = x0, left = count;
    while (left > 0) {
        INT n = left > t->cap ? t->cap : left;
        const UINT8 *in = cov + (x - x0);
        for (INT i = 0; i < n; i++) {
            UINT c = in[i];
            UINT c8 = cmax == 1 ? (c ? 255 : 0) : (c * 255 + cmax / 2) / cmax;
            if (c8 > 255)
                c8 = 255;
            t->cov8[i] = t->lut ? t->lut[c8] : (UINT8)c8;
        }
        if (t->clip)
            mask_row(t->clip, y, x, n, t->cov8, t->mrow);
        if (t->extra)
            mask_row(t->extra, y, x, n, t->cov8, t->mrow);
        if (t->src->constant) {
            for (INT i = 0; i < n; i++)
                t->buf[i] = t->src->color;
        } else {
            t->src->fetch(t->src, x, y, n, t->buf);
        }
        if (t->mode == CompositingModeSourceOver) {
            for (INT i = 0; i < n; i++)
                t->buf[i] = gp_scale_pargb(t->buf[i], t->cov8[i]);
            gp_compose_over(&t->surf, x, y, n, t->buf);
        } else {
            gp_compose_copy(&t->surf, x, y, n, t->buf, t->cov8, 255);
        }
        t->modified = TRUE;
        x += n;
        left -= n;
    }
}

/* fills the polygons of glyphs with a brush, through the contrast of the text and the clip of the graphics. */
static GpStatus text_fill(GGraphics *g, const MimeVecPolys *polys, const GBrush *brush, BOOL aliased,
                          const MimeVecMask *extra)
{
    MimeVecBounds b;
    if (!mime_vec_polys_bounds(polys, &b))
        return Ok;
    if (b.left < -4.0e6f)
        b.left = -4.0e6f;
    if (b.top < -4.0e6f)
        b.top = -4.0e6f;
    if (b.right > 4.0e6f)
        b.right = 4.0e6f;
    if (b.bottom > 4.0e6f)
        b.bottom = 4.0e6f;
    RECT area = {(INT)mime_vec_floorf(b.left) - 1, (INT)mime_vec_floorf(b.top) - 1, (INT)mime_vec_ceilf(b.right) + 2,
                 (INT)mime_vec_ceilf(b.bottom) + 2};
    const MimeVecMask *clip;
    if (!gp_clip_narrow(g, &area, &clip))
        return Ok;
    if (extra) {
        MimeVecRect eb = mime_vec_mask_bounds(extra);
        if (area.left < eb.left)
            area.left = eb.left;
        if (area.top < eb.top)
            area.top = eb.top;
        if (area.right > eb.right)
            area.right = eb.right;
        if (area.bottom > eb.bottom)
            area.bottom = eb.bottom;
        if (area.right <= area.left || area.bottom <= area.top)
            return Ok;
    }
    GSource *src;
    GpStatus status = gp_source_create(g, brush, &area, &src);
    if (status != Ok)
        return status;
    TextFill t;
    memset(&t, 0, sizeof(t));
    t.g = g;
    t.src = src;
    t.clip = clip;
    t.extra = extra;
    t.mode = g->compositing_mode;
    t.cap = 1024;
    t.buf = gp_alloc(sizeof(UINT32) * (SIZE_T)t.cap);
    t.cov8 = gp_alloc((SIZE_T)t.cap);
    t.mrow = gp_alloc((SIZE_T)t.cap);
    UINT8 lut[256];
    if (!aliased && g->text_contrast >= 1 && g->text_contrast <= 12) {
        const UINT8 *inv = contrast_inv + 256 * (g->text_contrast - 1);
        for (INT c = 0; c < 256; c++)
            lut[c] = (UINT8)(255 - inv[255 - c]);
        t.lut = lut;
    }
    if (!t.buf || !t.cov8 || !t.mrow) {
        gp_free(t.buf);
        gp_free(t.cov8);
        gp_free(t.mrow);
        gp_source_free(src);
        return OutOfMemory;
    }
    status = gp_surface_acquire(g, &area, &t.surf);
    if (status == Ok) {
        status = gp_raster(polys, MIME_VEC_FILL_WINDING, aliased ? MIME_VEC_AA_NONE : MIME_VEC_AA_8X4, &area, text_span,
                           &t);
        gp_surface_release(g, &t.surf, t.modified);
    }
    gp_free(t.buf);
    gp_free(t.cov8);
    gp_free(t.mrow);
    gp_source_free(src);
    return status;
}

/* the glyphs of a drawing, as polygons in device space. */
typedef struct Emit {
    MimeVecPolys *polys;
    MimeVecPath *path;      /* glyphs are added to a path instead, in the space of the layout. */
    MimeVecMatrix post;     /* from the space of the layout to the device. */
    float size;
    BOOL bold_sim, italic_sim;
    BOOL snap;              /* glyph origins and decorations land on whole device pixels. */
    float snap_dx, snap_dy;
} Emit;

static GpStatus emit_status(int e)
{
    return e == MIME_VEC_OK ? Ok : (e == MIME_VEC_NOMEM ? OutOfMemory : InvalidParameter);
}

/* a glyph outline through a matrix into the sink: flattened polygons, or the figures of a path. */
static GpStatus emit_outline(Emit *E, const MimeVecPath *p, const MimeVecMatrix *f)
{
    MimeVecMatrix m;
    mime_vec_matrix_multiply(&m, f, &E->post);
    if (E->path) {
        MimeVecPoint *pts = gp_alloc(sizeof(MimeVecPoint) * p->count);
        if (!pts)
            return OutOfMemory;
        for (UINT i = 0; i < p->count; i++)
            pts[i] = mime_vec_xform(&m, p->pts[i].x, p->pts[i].y);
        int e = mime_vec_path_add_raw(E->path, pts, p->types, p->count);
        gp_free(pts);
        return emit_status(e);
    }
    m.dx += E->snap_dx;
    m.dy += E->snap_dy;
    int e = mime_vec_flatten(E->polys, p->pts, p->types, p->count, &m, 0.15f);
    return e == MIME_VEC_NOMEM ? OutOfMemory : Ok;
}

static GpStatus emit_glyph(Emit *E, GFace *face, UINT16 glyph, float wx, float wy)
{
    const MimeVecPath *p = gp_face_outline(face, glyph);
    if (!p)
        return Ok;
    if (E->snap) {
        MimeVecPoint o = mime_vec_xform(&E->post, wx, wy);
        E->snap_dx = mime_vec_floorf(o.x + 0.5f) - o.x;
        E->snap_dy = mime_vec_floorf(o.y + 0.5f) - o.y;
    }
    float k = E->size / (float)face->upem;
    MimeVecMatrix f = {k, 0.0f, E->italic_sim ? k * 0.2126f : 0.0f, -k, wx, wy};
    GpStatus st = emit_outline(E, p, &f);
    if (st == Ok && E->bold_sim) {
        /* a simulated bold widens the stems by a thirty second of the em. */
        f.dx += E->size / 32.0f;
        st = emit_outline(E, p, &f);
    }
    return st;
}

static GpStatus emit_rect(Emit *E, float x, float y, float w, float h)
{
    MimeVecPoint pts[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    for (INT i = 0; i < 4; i++)
        pts[i] = mime_vec_xform(&E->post, pts[i].x, pts[i].y);
    if (E->snap && !E->path) {
        /* a decoration on the grid: edges on whole pixels, at least one pixel thick. */
        float l = mime_vec_floorf(pts[0].x + 0.5f), r = mime_vec_floorf(pts[1].x + 0.5f);
        float t = mime_vec_floorf(pts[0].y + 0.5f), b = mime_vec_floorf(pts[3].y + 0.5f);
        if (r < l + 1.0f)
            r = l + 1.0f;
        if (b < t + 1.0f)
            b = t + 1.0f;
        pts[0].x = pts[3].x = l;
        pts[1].x = pts[2].x = r;
        pts[0].y = pts[1].y = t;
        pts[2].y = pts[3].y = b;
    }
    if (E->path)
        return emit_status(mime_vec_path_add_polygon(E->path, pts, 4));
    return emit_status(mime_vec_polys_add_sub(E->polys, pts, 4, true));
}

/* reverses the runs of characters from the highest level down, which puts the characters in visual order. */
static void visual_order(Layout *L, INT start, INT end)
{
    INT n = end - start;
    UINT8 maxlv = 0, minodd = 255;
    for (INT i = 0; i < n; i++) {
        L->vis[i] = start + i;
        UINT8 lv = L->ch[start + i].level;
        if (lv > maxlv)
            maxlv = lv;
        if ((lv & 1) && lv < minodd)
            minodd = lv;
    }
    if (minodd == 255)
        return;
    for (INT lv = maxlv; lv >= minodd; lv--) {
        INT i = 0;
        while (i < n) {
            if (L->ch[L->vis[i]].level >= lv) {
                INT j = i;
                while (j < n && L->ch[L->vis[j]].level >= lv)
                    j++;
                for (INT a = i, b = j - 1; a < b; a++, b--) {
                    INT t = L->vis[a];
                    L->vis[a] = L->vis[b];
                    L->vis[b] = t;
                }
                i = j;
            } else {
                i++;
            }
        }
    }
}

/* the x of every character of a line from its start, in visual order and with a path ellipsis closed up. */
static void line_positions(Layout *L, const Line *ln)
{
    INT pe = par_end(L, ln->start);
    INT lim = ln->end < pe ? ln->end : pe;
    measure_range(L, ln->start, lim);
    INT stop = ln->shown_end;
    if (!L->bidi) {
        for (INT k = ln->start; k < lim; k++)
            L->xs[k] = L->pre[k];
    } else {
        visual_order(L, ln->start, stop);
        float x = 0.0f;
        for (INT i = 0; i < stop - ln->start; i++) {
            INT k = L->vis[i];
            L->xs[k] = x;
            x += L->pre[k + 1] - L->pre[k];
        }
        for (INT k = stop; k < lim; k++)
            L->xs[k] = x;
    }
    if (ln->trim == StringTrimmingEllipsisPath && ln->ellipsis && ln->cut_to > ln->cut_from) {
        for (INT k = ln->cut_to; k < stop; k++)
            L->xs[k] = L->xs[k] - L->pre[ln->cut_to] + ln->ell_x + L->ell_width;
    }
}

static BOOL shown_char(const Line *ln, INT k)
{
    if (k < ln->start || k >= ln->shown_end)
        return FALSE;
    return !(ln->ellipsis && k >= ln->cut_from && k < ln->cut_to);
}

/* the advance of a shown character, from the measured positions. */
static float shown_adv(const Layout *L, INT k)
{
    return L->pre[k + 1] - L->pre[k];
}

/* adds a line's glyphs, decorations and ellipsis. x0 and baseline are in the space of the layout. */
static GpStatus emit_line(Layout *L, const Line *ln, Emit *E, float x0, float baseline)
{
    line_positions(L, ln);
    float S = L->S;
    GpStatus st = Ok;
    for (INT k = ln->start; k < ln->shown_end && st == Ok; k++) {
        const Char *c = &L->ch[k];
        if (!shown_char(ln, k) || c->kind != CK_NORMAL || !c->glyph)
            continue;
        st = emit_glyph(E, c->face, c->glyph, x0 + L->xs[k] / S, baseline);
    }
    if (ln->ellipsis && st == Ok) {
        float x = x0 + ln->ell_x / S;
        for (INT i = 0; i < L->ell_n && st == Ok; i++) {
            st = emit_glyph(E, L->ell_face, L->ell_glyph[i], x, baseline);
            x += L->ell_adv[i] / S;
        }
    }
    if (st != Ok)
        return st;
    GFace *face = L->face;
    float k = L->size / (float)face->upem;
    /* a decoration is at least a device pixel thick. */
    MimeVecMatrix *pm = &E->post;
    float device = mime_vec_sqrtf(pm->m21 * pm->m21 + pm->m22 * pm->m22);
    float min_thick = E->path ? 0.0f : (device > 0.0f ? 1.0f / device : 1.0f);
    float width = ln->width / S;
    if (L->style & FontStyleUnderline && width > 0.0f) {
        float th = (float)face->underline_thickness * k;
        if (th < min_thick)
            th = min_thick;
        st = emit_rect(E, x0, baseline - (float)face->underline_pos * k, width, th);
    }
    if ((L->style & FontStyleStrikeout) && width > 0.0f && st == Ok) {
        float th = (float)face->strike_thickness * k;
        if (th < min_thick)
            th = min_thick;
        st = emit_rect(E, x0, baseline - (float)face->strike_pos * k, width, th);
    }
    /* a hotkey is underlined under the character after its ampersand. */
    for (INT h = 0; h < L->hot_count && st == Ok; h++) {
        INT t = L->hot[h] + 1;
        if (t < ln->start || t >= ln->end || !shown_char(ln, t) || L->ch[t].kind == CK_HIDDEN)
            continue;
        float th = (float)face->underline_thickness * k;
        if (th < min_thick)
            th = min_thick;
        st = emit_rect(E, x0 + L->xs[t] / S, baseline - (float)face->underline_pos * k, shown_adv(L, t) / S, th);
    }
    return st;
}

/* the shift the line alignment gives the block of lines, in layout units. */
static float block_shift(const Layout *L)
{
    StringAlignment la = L->fmt->line_align;
    if (la == StringAlignmentNear)
        return 0.0f;
    float H = layout_height_world(L);
    if (L->fast) {
        float v = L->lh_world - H;
        return (la == StringAlignmentCenter ? v * 0.5f : v) * L->S;
    }
    float v = mime_vec_floorf(L->lh_world * L->S + 0.5f) - mime_vec_floorf(H * L->S + 0.5f);
    return la == StringAlignmentCenter ? (float)((INT)v / 2) : v;
}

static BOOL axis_aligned(const MimeVecMatrix *m)
{
    return (m->m12 == 0.0f && m->m21 == 0.0f) || (m->m11 == 0.0f && m->m22 == 0.0f);
}

/* the matrix from the space of a vertical layout, where lines run along x, to the space the text is drawn in. */
static void vertical_matrix(float x0, float y0, MimeVecMatrix *m)
{
    m->m11 = 0.0f;
    m->m12 = 1.0f;
    m->m21 = -1.0f;
    m->m22 = 0.0f;
    m->dx = x0;
    m->dy = y0;
}

static GpStatus layout_emit(Layout *L, float lx, float ly, float lw, float lh, const MimeVecMatrix *dev, BOOL gridfit,
                            MimeVecPolys *polys, MimeVecPath *path)
{
    Emit E;
    memset(&E, 0, sizeof(E));
    E.polys = polys;
    E.path = path;
    E.size = L->size;
    E.bold_sim = (L->style & FontStyleBold) && !L->face->bold;
    E.italic_sim = (L->style & FontStyleItalic) && !L->face->italic;
    float ox = lx, oy = ly;
    if (L->vertical) {
        MimeVecMatrix v;
        vertical_matrix(lx + lw, ly, &v);
        mime_vec_matrix_multiply(&E.post, &v, dev);
        ox = oy = 0.0f;
    } else {
        E.post = *dev;
    }
    E.snap = gridfit && !path && axis_aligned(&E.post);
    float block = block_shift(L);
    for (INT i = 0; i < L->nlines; i++) {
        const Line *ln = &L->lines[i];
        float x0 = ox + (ln->ox + L->margin_l) / L->S;
        float baseline;
        if (L->fast) {
            float asc_px = mime_vec_floorf((float)L->face->ascent * (L->size / (float)L->face->upem) * dev->m22 + 0.5f);
            baseline = oy + block / L->S + asc_px / dev->m22;
        } else {
            baseline = oy + (block + ln->top + ln->ascent) / L->S;
        }
        GpStatus st = emit_line(L, ln, &E, x0, baseline);
        if (st != Ok)
            return st;
    }
    return Ok;
}

/* the mask of a rectangle of the layout space, which text is clipped to. */
static GpStatus rect_mask(const MimeVecMatrix *dev, float x, float y, float w, float h, MimeVecMask *mask)
{
    MimeVecPoint pts[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    for (INT i = 0; i < 4; i++)
        pts[i] = mime_vec_xform(dev, pts[i].x, pts[i].y);
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    GpStatus st = emit_status(mime_vec_polys_add_sub(&polys, pts, 4, true));
    if (st == Ok) {
        MimeVecRect clip = {-MIME_VEC_CLIP_LIMIT / 4, -MIME_VEC_CLIP_LIMIT / 4, MIME_VEC_CLIP_LIMIT / 4,
                            MIME_VEC_CLIP_LIMIT / 4};
        mime_vec_mask_init(mask, &gp_allocator);
        st = emit_status(mime_vec_mask_set_polys(mask, &polys, MIME_VEC_FILL_WINDING, MIME_VEC_AA_NONE, &clip));
    }
    mime_vec_polys_free(&polys);
    return st;
}

/* ------------------------------------------------------------------ drawing and measuring a string */

/* gdi+ refuses to draw clear type text over a destination that is replaced and not blended. */
static BOOL text_mode_refused(const GGraphics *g)
{
    return g->compositing_mode == CompositingModeSourceCopy && effective_hint(g) == TextRenderingHintClearTypeGridFit;
}

static GpStatus draw_string(GGraphics *g, const WCHAR *string, INT length, GFont *font, const RectF *rc,
                            const GStrFmt *fmt, const GBrush *brush)
{
    if (text_mode_refused(g))
        return InvalidParameter;
    BOOL noclip = fmt && (fmt->flags & StringFormatFlagsNoClip);
    /* a rectangle with a negative side shows nothing, unless it is not one at all or text may leave it. */
    if (rc->Width != 0.0f && rc->Height != 0.0f && !noclip && (rc->Width < 0.0f || rc->Height < 0.0f))
        return Ok;
    float size = gp_font_world_size(font, g);
    if (!(size > 0.0f))
        return InvalidParameter;
    MimeVecMatrix dev = gp_graphics_w2d_base(g);
    Layout L;
    GpStatus st = layout_build(&L, string, length, font->family, font->style, size, rc->Width, rc->Height, fmt, &dev,
                               TRUE);
    if (st != Ok) {
        layout_free(&L);
        return st;
    }
    TextRenderingHint hint = effective_hint(g);
    BOOL aliased = hint == TextRenderingHintSingleBitPerPixelGridFit || hint == TextRenderingHintSingleBitPerPixel;
    BOOL gridfit = hint == TextRenderingHintSingleBitPerPixelGridFit || hint == TextRenderingHintAntiAliasGridFit ||
                   hint == TextRenderingHintClearTypeGridFit;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    st = layout_emit(&L, rc->X, rc->Y, rc->Width, rc->Height, &dev, gridfit, &polys, nullptr);
    if (st == Ok) {
        MimeVecMask clip;
        BOOL have = FALSE;
        if (!noclip && rc->Width != 0.0f && rc->Height != 0.0f) {
            st = rect_mask(&dev, rc->X, rc->Y, rc->Width, rc->Height, &clip);
            have = st == Ok;
        }
        if (st == Ok)
            st = text_fill(g, &polys, brush, aliased, have ? &clip : nullptr);
        if (have)
            mime_vec_mask_free(&clip);
    }
    mime_vec_polys_free(&polys);
    layout_free(&L);
    return st;
}

static BOOL empty_text(const WCHAR *string, INT length)
{
    return length == 0 || (length == -1 && !*string);
}

static INT text_length(const WCHAR *string, INT length)
{
    return length == -1 ? (INT)wcslen(string) : length;
}

WINGDIPAPI GpStatus WINAPI GdipDrawString(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font,
                                          const RectF *layout, const GpStringFormat *format, const GpBrush *brush)
{
    if (!string || !layout)
        return InvalidParameter;
    GGraphics *g = gp_graphics_cast(graphics);
    if (!g)
        return InvalidParameter;
    if (g->got_dc)
        return ObjectBusy;
    const GBrush *b = gp_brush_cast(brush);
    if (!b)
        return InvalidParameter;
    if (empty_text(string, length))
        return Ok;
    GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    const GStrFmt *fmt = nullptr;
    if (format) {
        fmt = gp_strfmt_cast(format);
        if (!fmt)
            return InvalidParameter;
    }
    if (length < -1)
        return InvalidParameter;
    return draw_string(g, string, text_length(string, length), f, layout, fmt, b);
}

WINGDIPAPI GpStatus WINAPI GdipMeasureString(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font,
                                             const RectF *layout, const GpStringFormat *format, RectF *bounds,
                                             INT *fitted, INT *lines)
{
    if (!string || !layout || !bounds)
        return InvalidParameter;
    GGraphics *g = gp_graphics_cast(graphics);
    if (!g)
        return InvalidParameter;
    if (g->got_dc)
        return ObjectBusy;
    if (empty_text(string, length)) {
        bounds->X = bounds->Y = bounds->Width = bounds->Height = 0.0f;
        if (fitted)
            *fitted = 0;
        if (lines)
            *lines = 0;
        return Ok;
    }
    GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    const GStrFmt *fmt = nullptr;
    if (format) {
        fmt = gp_strfmt_cast(format);
        if (!fmt)
            return InvalidParameter;
    }
    if (length < -1)
        return InvalidParameter;
    float size = gp_font_world_size(f, g);
    if (!(size > 0.0f))
        return InvalidParameter;
    MimeVecMatrix dev = gp_graphics_w2d_base(g);
    Layout L;
    GpStatus st = layout_build(&L, string, text_length(string, length), f->family, f->style, size, layout->Width,
                               layout->Height, fmt, &dev, TRUE);
    if (st != Ok) {
        layout_free(&L);
        return st;
    }
    float H = layout_height_world(&L);
    float extent = (L.right - L.left) / L.S;
    float from = L.left / L.S;
    StringAlignment la = L.fmt->line_align;
    *bounds = *layout;
    if (L.vertical) {
        float room = bounds->Width - H;
        float shift = la == StringAlignmentCenter ? room * 0.5f : (la == StringAlignmentFar ? room : 0.0f);
        bounds->Height = extent;
        bounds->Y = from + layout->Y;
        if (L.flags & StringFormatFlagsDirectionRightToLeft)
            bounds->X = (room - shift) + layout->X;
        else
            bounds->X = layout->X + shift;
        bounds->Width = H;
    } else {
        bounds->Width = extent;
        bounds->X = from + layout->X;
        float room = bounds->Height - H;
        if (la == StringAlignmentCenter)
            bounds->Y += room * 0.5f;
        else if (la == StringAlignmentFar)
            bounds->Y += room;
        bounds->Height = H;
    }
    if (!(L.flags & StringFormatFlagsNoClip)) {
        /* a box wider or taller than the rectangle is cut to it, and starts at its edge. */
        if (layout->Width > 0.0f && bounds->Width > layout->Width) {
            bounds->Width = layout->Width;
            bounds->X = layout->X;
        }
        if (layout->Height > 0.0f && bounds->Height > layout->Height) {
            bounds->Height = layout->Height;
            /* the quick path leaves the top where the alignment put it. */
            if (!L.fast || L.vertical)
                bounds->Y = layout->Y;
        }
    }
    if (fitted)
        *fitted = L.chars_fitted;
    if (lines)
        *lines = L.nlines;
    layout_free(&L);
    return Ok;
}

/* ------------------------------------------------------------------ the regions of character ranges */

static GpStatus range_rects(Layout *L, INT start, INT end, const RectF *rc, GpRegion *region)
{
    float block = block_shift(L);
    float S = L->S;
    for (INT i = 0; i < L->nlines; i++) {
        const Line *ln = &L->lines[i];
        INT a = start > ln->start ? start : ln->start;
        INT b = end < ln->end ? end : ln->end;
        if (a >= b)
            continue;
        line_positions(L, ln);
        float top = (block + ln->top) / S;
        float height = (ln->ascent + ln->descent) / S;
        BOOL open = FALSE;
        float x0 = 0.0f, x1 = 0.0f;
        for (INT k = a; k <= b; k++) {
            BOOL use = k < b && shown_char(ln, k);
            float cx0 = 0.0f, cx1 = 0.0f;
            if (use) {
                cx0 = L->xs[k];
                cx1 = cx0 + shown_adv(L, k);
            }
            if (use && open && cx0 <= x1 + 0.5f && cx1 >= x0 - 0.5f) {
                if (cx0 < x0)
                    x0 = cx0;
                if (cx1 > x1)
                    x1 = cx1;
                continue;
            }
            if (open) {
                float u = (ln->ox + L->margin_l + x0) / S, w = (x1 - x0) / S;
                RectF r;
                if (L->vertical) {
                    r.X = rc->X + rc->Width - (top + height);
                    r.Y = rc->Y + u;
                    r.Width = height;
                    r.Height = w;
                } else {
                    r.X = rc->X + u;
                    r.Y = rc->Y + top;
                    r.Width = w;
                    r.Height = height;
                }
                GpStatus st = GdipCombineRegionRect(region, (const GpRectF *)&r, CombineModeUnion);
                if (st != Ok)
                    return st;
                open = FALSE;
            }
            if (use) {
                open = TRUE;
                x0 = cx0;
                x1 = cx1;
            }
        }
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipMeasureCharacterRanges(GpGraphics *graphics, const WCHAR *string, INT length,
                                                      const GpFont *font, const RectF *layout,
                                                      const GpStringFormat *format, INT count, GpRegion **regions)
{
    if (!string || !layout)
        return InvalidParameter;
    GGraphics *g = gp_graphics_cast(graphics);
    if (!g)
        return InvalidParameter;
    if (g->got_dc)
        return ObjectBusy;
    const GStrFmt *fmt = gp_strfmt_cast(format);
    if (!fmt || !regions || empty_text(string, length))
        return InvalidParameter;
    GFont *f = gp_font_cast(font);
    if (!f || length < -1)
        return InvalidParameter;
    if (count < fmt->range_count)
        return InvalidParameter;
    float size = gp_font_world_size(f, g);
    if (!(size > 0.0f))
        return InvalidParameter;
    INT len = text_length(string, length);
    Layout L;
    GpStatus st = layout_try(&L, string, len, f->family, f->style, size, layout->Width, layout->Height, fmt, nullptr,
                             FALSE);
    if (st != Ok) {
        layout_free(&L);
        return st;
    }
    for (INT r = 0; r < fmt->range_count && st == Ok; r++) {
        GRegion *region = gp_region_cast(regions[r]);
        if (!region) {
            st = InvalidParameter;
            break;
        }
        GdipSetEmpty(region);
        INT first = fmt->ranges[r].First, span = fmt->ranges[r].Length;
        if (span == 0)
            continue;
        INT start = span >= 0 ? first : first + span;
        INT n = span >= 0 ? span : -span;
        if (start < 0 || start > len || n > len - start) {
            st = InvalidParameter;
            break;
        }
        st = range_rects(&L, start, start + n, layout, region);
        if (st == Ok && !(L.flags & StringFormatFlagsNoClip))
            st = GdipCombineRegionRect(region, (const GpRectF *)layout, CombineModeIntersect);
    }
    layout_free(&L);
    return st;
}

/* ------------------------------------------------------------------ text as a path */

static GpStatus add_path_string(GPath *p, const WCHAR *string, INT length, GFamily *family, INT style, float em,
                                const RectF *rc, const GStrFmt *fmt)
{
    if (!(em > 0.0f))
        return InvalidParameter;
    /* a rectangle with a negative extent along the lines holds nothing. */
    float along = fmt && (fmt->flags & StringFormatFlagsDirectionVertical) ? rc->Height : rc->Width;
    if (along < 0.0f)
        return Ok;
    Layout L;
    GpStatus st = layout_try(&L, string, length, family, style, em, rc->Width, rc->Height, fmt, nullptr, FALSE);
    if (st == Ok) {
        MimeVecMatrix identity;
        mime_vec_matrix_identity(&identity);
        MimeVecPath add;
        mime_vec_path_init(&add, &gp_allocator);
        st = layout_emit(&L, rc->X, rc->Y, rc->Width, rc->Height, &identity, FALSE, nullptr, &add);
        if (st == Ok && add.count)
            st = emit_status(mime_vec_path_append(&p->path, &add, false));
        mime_vec_path_free(&add);
    }
    layout_free(&L);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipAddPathString(GpPath *path, const WCHAR *string, INT length, const GpFontFamily *family,
                                             INT style, REAL size, const RectF *layout, const GpStringFormat *format)
{
    if (!string || !layout)
        return InvalidParameter;
    GPath *p = gp_path_cast(path);
    if (!p)
        return InvalidParameter;
    if (empty_text(string, length))
        return Ok;
    GFamily *fam = gp_family_cast(family);
    if (!fam)
        return InvalidParameter;
    const GStrFmt *fmt = nullptr;
    if (format) {
        fmt = gp_strfmt_cast(format);
        if (!fmt)
            return InvalidParameter;
    }
    if (length < -1)
        return InvalidParameter;
    return add_path_string(p, string, text_length(string, length), fam, style, size, layout, fmt);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathStringI(GpPath *path, const WCHAR *string, INT length,
                                              const GpFontFamily *family, INT style, REAL size, const Rect *layout,
                                              const GpStringFormat *format)
{
    if (!layout)
        return InvalidParameter;
    RectF r = {(float)layout->X, (float)layout->Y, (float)layout->Width, (float)layout->Height};
    return GdipAddPathString(path, string, length, family, style, size, &r, format);
}

/* ------------------------------------------------------------------ strings of glyphs */

typedef struct GlyphRun {
    GFace *face;
    float size;
    INT count;
    UINT16 *glyph;
    MimeVecPoint *origin;       /* in the local space of the matrix. */
} GlyphRun;

static void run_free(GlyphRun *r)
{
    gp_free(r->glyph);
    gp_free(r->origin);
    memset(r, 0, sizeof(*r));
}

/* the glyphs and where each stands for a driver string. positions are the origins, or the first one only. */
static GpStatus run_make(GlyphRun *r, GGraphics *g, const UINT16 *text, INT length, GFont *font,
                         const PointF *positions, INT flags, const GMatrix *matrix)
{
    memset(r, 0, sizeof(*r));
    r->face = gp_family_face(font->family, font->style);
    if (!r->face)
        return InvalidParameter;
    r->size = gp_font_world_size(font, g);
    if (!(r->size > 0.0f))
        return InvalidParameter;
    INT n = length;
    if (n == -1) {
        n = 0;
        while (text[n])
            n++;
    }
    r->count = n;
    r->glyph = gp_alloc(sizeof(UINT16) * (SIZE_T)(n + 1));
    r->origin = gp_alloc(sizeof(MimeVecPoint) * (SIZE_T)(n + 1));
    if (!r->glyph || !r->origin)
        return OutOfMemory;
    for (INT i = 0; i < n; i++)
        r->glyph[i] = (flags & DriverStringOptionsCmapLookup) ? gp_face_glyph(r->face, text[i]) : text[i];
    float k = r->size / (float)r->face->upem;
    BOOL vertical = (flags & DriverStringOptionsVertical) != 0;
    if (flags & DriverStringOptionsRealizedAdvance) {
        /* the position is where the first glyph is after the matrix, the others follow by their advances. */
        MimeVecPoint o = {positions[0].X, positions[0].Y};
        if (matrix) {
            MimeVecMatrix inv;
            if (!gp_matrix_inverse(&inv, &matrix->m))
                return InvalidParameter;
            o = mime_vec_xform(&inv, o.x, o.y);
        }
        for (INT i = 0; i < n; i++) {
            r->origin[i] = o;
            float adv = (float)gp_face_advance(r->face, r->glyph[i]) * k;
            if (vertical)
                o.y += adv;
            else
                o.x += adv;
        }
    } else {
        for (INT i = 0; i < n; i++) {
            r->origin[i].x = positions[i].X;
            r->origin[i].y = positions[i].Y;
        }
    }
    return Ok;
}

static BOOL driver_args(const void *matrix_in, const GMatrix **matrix)
{
    *matrix = nullptr;
    if (!matrix_in)
        return TRUE;
    *matrix = gp_matrix_cast(matrix_in);
    return *matrix != nullptr;
}

WINGDIPAPI GpStatus WINAPI GdipDrawDriverString(GpGraphics *graphics, const UINT16 *text, INT length,
                                                const GpFont *font, const GpBrush *brush, const PointF *positions,
                                                INT flags, const GpMatrix *matrix)
{
    GGraphics *g = gp_graphics_cast(graphics);
    if (!g)
        return InvalidParameter;
    if (g->got_dc)
        return ObjectBusy;
    const GMatrix *m;
    if (!driver_args(matrix, &m))
        return InvalidParameter;
    const GBrush *b = gp_brush_cast(brush);
    if (!b)
        return InvalidParameter;
    if (!text || !positions)
        return InvalidParameter;
    if (length == -1 && !(flags & DriverStringOptionsCmapLookup))
        return InvalidParameter;
    if (length == 0 || (length == -1 && !*text))
        return Ok;
    GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    /* a driver string has no underline or strikeout. */
    if (f->style & (FontStyleUnderline | FontStyleStrikeout))
        return InvalidParameter;
    if (length < -1)
        return InvalidParameter;
    if (text_mode_refused(g))
        return InvalidParameter;
    GlyphRun run;
    GpStatus st = run_make(&run, g, text, length, f, positions, flags, m);
    if (st == Ok) {
        MimeVecPolys polys;
        mime_vec_polys_init(&polys, &gp_allocator);
        Emit E;
        memset(&E, 0, sizeof(E));
        E.polys = &polys;
        E.size = run.size;
        E.bold_sim = (f->style & FontStyleBold) && !run.face->bold;
        E.italic_sim = (f->style & FontStyleItalic) && !run.face->italic;
        MimeVecMatrix dev = gp_graphics_w2d_base(g);
        if (m)
            mime_vec_matrix_multiply(&E.post, &m->m, &dev);
        else
            E.post = dev;
        for (INT i = 0; i < run.count && st == Ok; i++)
            st = emit_glyph(&E, run.face, run.glyph[i], run.origin[i].x, run.origin[i].y);
        if (st == Ok) {
            TextRenderingHint hint = effective_hint(g);
            st = text_fill(g, &polys, b,
                           hint == TextRenderingHintSingleBitPerPixelGridFit || hint == TextRenderingHintSingleBitPerPixel,
                           nullptr);
        }
        mime_vec_polys_free(&polys);
    }
    run_free(&run);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipMeasureDriverString(GpGraphics *graphics, const UINT16 *text, INT length,
                                                   const GpFont *font, const PointF *positions, INT flags,
                                                   const GpMatrix *matrix, RectF *bounds)
{
    GGraphics *g = gp_graphics_cast(graphics);
    if (!g)
        return InvalidParameter;
    if (g->got_dc)
        return ObjectBusy;
    const GMatrix *m;
    if (!driver_args(matrix, &m))
        return InvalidParameter;
    if (!text || !positions || !bounds)
        return InvalidParameter;
    if (length == -1 && !(flags & DriverStringOptionsCmapLookup))
        return InvalidParameter;
    if (length == 0 || (length == -1 && !*text)) {
        bounds->X = bounds->Y = bounds->Width = bounds->Height = 0.0f;
        return Ok;
    }
    GFont *f = gp_font_cast(font);
    if (!f || length < -1)
        return InvalidParameter;
    GlyphRun run;
    GpStatus st = run_make(&run, g, text, length, f, positions, flags, m);
    if (st == Ok) {
        /* the box of the glyph cells: the advance across and from the ascent to the descent. */
        float k = run.size / (float)run.face->upem;
        float asc = (float)run.face->ascent * k, desc = (float)run.face->descent * k;
        BOOL vertical = (flags & DriverStringOptionsVertical) != 0;
        float l = 3.0e38f, t = 3.0e38f, r = -3.0e38f, b = -3.0e38f;
        for (INT i = 0; i < run.count; i++) {
            float adv = (float)gp_face_advance(run.face, run.glyph[i]) * k;
            float x0 = run.origin[i].x, y0 = run.origin[i].y - asc, x1 = x0 + adv, y1 = run.origin[i].y + desc;
            if (vertical) {
                x0 = run.origin[i].x - desc;
                x1 = run.origin[i].x + asc;
                y0 = run.origin[i].y;
                y1 = y0 + adv;
            }
            if (x0 < l)
                l = x0;
            if (y0 < t)
                t = y0;
            if (x1 > r)
                r = x1;
            if (y1 > b)
                b = y1;
        }
        if ((flags & DriverStringOptionsRealizedAdvance) && m) {
            MimeVecPoint c[4] = {{l, t}, {r, t}, {r, b}, {l, b}};
            l = t = 3.0e38f;
            r = b = -3.0e38f;
            for (INT i = 0; i < 4; i++) {
                MimeVecPoint q = mime_vec_xform(&m->m, c[i].x, c[i].y);
                if (q.x < l)
                    l = q.x;
                if (q.y < t)
                    t = q.y;
                if (q.x > r)
                    r = q.x;
                if (q.y > b)
                    b = q.y;
            }
        }
        bounds->X = l;
        bounds->Y = t;
        bounds->Width = r - l;
        bounds->Height = b - t;
    }
    run_free(&run);
    return st;
}
