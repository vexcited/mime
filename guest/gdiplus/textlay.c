/*
 * string layout: characters become items, items become lines of positioned glyphs.
 *
 * the quick path handles a single line of plain characters on a device that only scales, in the font's design
 * units. the general path works on a grid of 2048 units per em and handles everything else: wrapping, trimming,
 * tabs, hotkeys, fallback fonts and right to left text. both produce a GTextLayout in layout rectangle units.
 */
#include "text_private.h"

/* ------------------------------------------------------------------ character classes */

enum {
    CL_SPACE = 1 << 0,
    CL_TAB = 1 << 1,
    CL_NEWLINE = 1 << 2,
    CL_HIDDEN = 1 << 3,   /* no glyph and no width. */
    CL_LETTER = 1 << 4,
    CL_HYPHEN = 1 << 5,
    CL_EMDASH = 1 << 6,
    CL_IDEO = 1 << 7,
    CL_OPEN = 1 << 8,
    CL_CLOSE = 1 << 9,
    CL_ZWSP = 1 << 10,
    CL_BLANK = CL_SPACE | CL_TAB | CL_HIDDEN,
};

enum {
    BIDI_NEUTRAL,
    BIDI_L,
    BIDI_R,
    BIDI_NUMBER,
};

static BOOL in_range(UINT32 c, UINT32 lo, UINT32 hi) { return c >= lo && c <= hi; }

static BOOL is_open_punctuation(UINT32 c)
{
    return c == 0x3008 || c == 0x300A || c == 0x300C || c == 0x300E || c == 0x3010 || c == 0x3014 || c == 0x3016 ||
           c == 0x3018 || c == 0x301A || c == 0x301D || c == 0xFF08 || c == 0xFF3B || c == 0xFF5B;
}

static BOOL is_close_punctuation(UINT32 c)
{
    return c == 0x3001 || c == 0x3002 || c == 0x3009 || c == 0x300B || c == 0x300D || c == 0x300F || c == 0x3011 ||
           c == 0x3015 || c == 0x3017 || c == 0x3019 || c == 0x301B || c == 0x301E || c == 0x301F || c == 0xFF09 ||
           c == 0xFF0C || c == 0xFF0E || c == 0xFF3D || c == 0xFF5D || c == 0xFF01 || c == 0xFF1A || c == 0xFF1B ||
           c == 0xFF1F;
}

static UINT16 classify(UINT32 c)
{
    if (c == 0x09)
        return CL_SPACE | CL_TAB;
    if (c == 0x0A || c == 0x0D || c == 0x85 || c == 0x2028 || c == 0x2029)
        return CL_NEWLINE;
    if (c < 0x20 || in_range(c, 0x7F, 0x9F) || c == 0xAD || in_range(c, 0x200C, 0x200F) ||
        in_range(c, 0x202A, 0x202E) || in_range(c, 0x2060, 0x2064) || in_range(c, 0x2066, 0x206F) || c == 0xFEFF ||
        c == 0xFFFF)
        return CL_HIDDEN;
    if (c == 0x200B)
        return CL_HIDDEN | CL_ZWSP;
    if (c == 0x20 || c == 0x1680 || in_range(c, 0x2000, 0x2006) || in_range(c, 0x2008, 0x200A) || c == 0x205F ||
        c == 0x3000)
        return CL_SPACE;
    if (c == 0x2D || c == 0x2010)
        return CL_HYPHEN;
    if (c == 0x2014)
        return CL_EMDASH;
    UINT16 cls = 0;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c < 0x2000 && c != 0xD7 && c != 0xF7))
        cls |= CL_LETTER;
    if (in_range(c, 0x2E80, 0x2FDF) || in_range(c, 0x3040, 0x30FF) || in_range(c, 0x3100, 0x312F) ||
        in_range(c, 0x3400, 0x4DBF) || in_range(c, 0x4E00, 0x9FFF) || in_range(c, 0xF900, 0xFAFF) ||
        in_range(c, 0x20000, 0x2FFFF))
        cls |= CL_IDEO | CL_LETTER;
    if (is_open_punctuation(c))
        cls |= CL_OPEN;
    if (is_close_punctuation(c))
        cls |= CL_CLOSE;
    return cls;
}

/* a character's own bidi direction. */
static int bidi_type(UINT32 c)
{
    if (in_range(c, 0x30, 0x39) || in_range(c, 0x660, 0x669) || in_range(c, 0x6F0, 0x6F9))
        return BIDI_NUMBER;
    if (in_range(c, 0x0590, 0x08FF) || in_range(c, 0xFB1D, 0xFDFF) || in_range(c, 0xFE70, 0xFEFF) ||
        in_range(c, 0x10800, 0x10FFF) || c == 0x200F)
        return BIDI_R;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0xC0)
        return BIDI_L;
    return BIDI_NEUTRAL;
}

/* FALSE for characters the quick path leaves to the general one. */
static BOOL quick_character_ok(UINT32 c)
{
    if (c < 0x20 || in_range(c, 0x7F, 0x9F) || c == 0xAD || c > 0xFFFF)
        return FALSE;
    if (in_range(c, 0x200B, 0x200F) || in_range(c, 0x202A, 0x202E) || in_range(c, 0x2060, 0x2064) ||
        in_range(c, 0x2066, 0x206F) || c == 0xFEFF || in_range(c, 0xD800, 0xDFFF))
        return FALSE;
    /* combining marks, then scripts that join, reorder or stack. */
    if (in_range(c, 0x0300, 0x036F) || in_range(c, 0x0590, 0x109F) || in_range(c, 0x1700, 0x18AF) ||
        in_range(c, 0x1DC0, 0x1DFF) || in_range(c, 0x20D0, 0x20FF) || in_range(c, 0xFB1D, 0xFDFF) ||
        in_range(c, 0xFE00, 0xFE0F) || in_range(c, 0xFE20, 0xFE2F) || in_range(c, 0xFE70, 0xFEFF))
        return FALSE;
    return TRUE;
}

/* ------------------------------------------------------------------ layout state */

typedef struct GItem {
    UINT32 cp;
    UINT16 glyph;
    UINT16 cls;
    const GFace *face;      /* NULL when the item has no glyph. */
    INT src_begin, src_end;
    INT adv;                /* in grid units, a tab's is set when its line is scanned. */
    BYTE level;
    BYTE underline;
    BYTE slow;              /* the quick path cannot handle it. */
} GItem;

/* a glyph about to go into a line. */
typedef struct Rec {
    GTextGlyph g;
    INT adv;
    INT x;
    BYTE level;
    BYTE blank;
    BYTE trailing;
} Rec;

typedef struct EllipsisGlyph {
    UINT16 glyph;
    INT adv;
} EllipsisGlyph;

/* the pieces of a line: items, then maybe an ellipsis, then more items. */
typedef struct LineSpec {
    INT a0, a1;
    BOOL ellipsis;
    INT b0, b1;
} LineSpec;

#define UNLIMITED (1 << 24)

typedef struct Ctx {
    const GTextInput *in;
    GTextLayout *out;
    const GStrFmt *fmt;
    BOOL no_format;
    const GFace *face;
    GItem *items;
    INT n;
    Rec *recs;
    INT rec_capacity;
    float size, scale;      /* the em size, and 2048 over it. */
    float lead_fraction, trail_fraction, tracking;
    INT flags;
    StringTrimming trimming;
    StringAlignment align, line_align;
    BOOL nowrap, vertical, rtl, fallback, measure_trailing;
    float across_extent, along_extent;
    INT along_g, lead_g, trail_g, limit;
    INT ascent_g, descent_g, pitch_g;
    EllipsisGlyph ellipsis[3];
    INT ellipsis_count, ellipsis_width;
    INT base_level;
    BOOL has_stops;
    INT *stops;
    INT stop_count, tab_interval;
} Ctx;

/* ------------------------------------------------------------------ items */

static GpStatus item_push(Ctx *c, INT *capacity, GItem **items, GItem **slot)
{
    if (c->n == *capacity) {
        INT grown = *capacity ? *capacity * 2 : 64;
        GItem *more = gp_realloc(*items, sizeof(GItem) * (SIZE_T)grown);
        if (!more)
            return OutOfMemory;
        *items = more;
        *capacity = grown;
    }
    *slot = &(*items)[c->n++];
    memset(*slot, 0, sizeof(**slot));
    return Ok;
}

/* advance in grid units, or design units when the quick path passes a factor of 1. */
static INT scaled_advance(INT design, double factor)
{
    float v = (float)((double)design * factor);
    return (INT)mime_vec_floorf(v + 0.5f);
}

/* characters to items: strips hotkey markers, pairs surrogates and looks up each glyph. */
static GpStatus build_items(Ctx *c)
{
    const GTextInput *in = c->in;
    const WCHAR *text = in->text;
    INT len = in->length;
    INT capacity = 0;
    INT pending_begin = -1;
    BOOL pending_underline = FALSE;
    c->items = nullptr;
    c->n = 0;
    INT prefix = c->fmt->hotkey;
    for (INT i = 0; i < len; i++) {
        UINT32 cp = text[i];
        INT begin = pending_begin >= 0 ? pending_begin : i;
        BOOL underline = pending_underline;
        INT end = i + 1;
        pending_begin = -1;
        pending_underline = FALSE;
        if (prefix != HotkeyPrefixNone && cp == '&') {
            if (i + 1 >= len) {
                /* a lone & at the end is dropped and still counts. */
                if (c->n)
                    c->items[c->n - 1].src_end = len;
                continue;
            }
            if (text[i + 1] != '&') {
                pending_begin = begin;
                pending_underline = prefix == HotkeyPrefixShow;
                continue;
            }
            end = i + 2;
            i++;
        } else if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            end = i + 2;
            i++;
        } else if (cp == 0x0D && i + 1 < len && text[i + 1] == 0x0A) {
            end = i + 2;
            i++;
        }
        GItem *it;
        GpStatus status = item_push(c, &capacity, &c->items, &it);
        if (status != Ok)
            return status;
        it->cp = cp;
        it->cls = classify(cp);
        it->src_begin = begin;
        it->src_end = end;
        it->underline = underline;
        it->slow = !quick_character_ok(cp);
        if (!(it->cls & (CL_NEWLINE | CL_HIDDEN | CL_TAB))) {
            it->face = c->face;
            it->glyph = gp_face_glyph(c->face, cp);
            if (!it->glyph && c->fallback) {
                /* a missing glyph sends the string to the general path, where another font may have it. */
                UINT16 glyph;
                const GFace *other = gp_fallback_face(cp, in->style, &glyph);
                it->slow = TRUE;
                if (other) {
                    it->face = other;
                    it->glyph = glyph;
                }
            }
        }
    }
    if (c->n)
        c->items[c->n - 1].src_end = len;
    return Ok;
}

/* ------------------------------------------------------------------ bidirectional levels */

/* bidi levels for one paragraph, a simplified Unicode bidi algorithm. */
static void assign_levels(Ctx *c, INT a, INT b)
{
    GItem *items = c->items;
    BYTE *cls = nullptr;
    INT count = b - a;
    if (count <= 0)
        return;
    BOOL any = c->base_level != 0;
    for (INT i = a; i < b && !any; i++)
        any = bidi_type(items[i].cp) == BIDI_R;
    if (!any)
        return;
    cls = gp_alloc((SIZE_T)count);
    if (!cls)
        return;
    int base_dir = c->base_level ? BIDI_R : BIDI_L;
    int previous = base_dir;
    for (INT i = 0; i < count; i++) {
        int t = bidi_type(items[a + i].cp);
        if (t == BIDI_NUMBER) {
            /* a number follows the direction of the text before it. */
            t = previous == BIDI_R ? BIDI_NUMBER : BIDI_L;
        } else if (t == BIDI_L || t == BIDI_R) {
            previous = t;
        }
        cls[i] = (BYTE)t;
    }
    /* neutrals take their neighbours' direction when both agree, else the paragraph's. */
    for (INT i = 0; i < count;) {
        if (cls[i] != BIDI_NEUTRAL) {
            i++;
            continue;
        }
        INT j = i;
        while (j < count && cls[j] == BIDI_NEUTRAL)
            j++;
        int left = i ? (cls[i - 1] == BIDI_NUMBER ? BIDI_R : cls[i - 1]) : base_dir;
        int right = j < count ? (cls[j] == BIDI_NUMBER ? BIDI_R : cls[j]) : base_dir;
        int dir = left == right ? left : base_dir;
        for (INT k = i; k < j; k++)
            cls[k] = (BYTE)dir;
        i = j;
    }
    for (INT i = 0; i < count; i++) {
        int level;
        if (cls[i] == BIDI_NUMBER)
            level = 2;
        else if (cls[i] == BIDI_R)
            level = 1;
        else
            level = c->base_level ? 2 : 0;
        items[a + i].level = (BYTE)level;
    }
    gp_free(cls);
}

/* ------------------------------------------------------------------ tabs and breaks */

static INT next_tab_stop(const Ctx *c, INT pen)
{
    if (c->has_stops) {
        for (INT i = 0; i < c->stop_count; i++) {
            if (c->stops[i] > pen)
                return c->stops[i];
        }
    }
    INT interval = c->tab_interval > 0 ? c->tab_interval : 1;
    return (pen / interval + 1) * interval;
}

static INT item_advance(const Ctx *c, GItem *it, INT pen)
{
    if (it->cls & CL_TAB)
        it->adv = next_tab_stop(c, pen) - pen;
    return it->adv;
}

/* sums the advances of a range from the line start, settling tab widths. */
static INT settle(Ctx *c, INT a, INT b)
{
    INT pen = 0;
    for (INT i = a; i < b; i++)
        pen += item_advance(c, &c->items[i], pen);
    return pen;
}

/* range width without trailing blanks. */
static INT visible_width(const Ctx *c, INT a, INT b)
{
    INT end = b;
    if (!c->measure_trailing) {
        while (end > a && (c->items[end - 1].cls & CL_BLANK))
            end--;
    }
    INT sum = 0;
    for (INT i = a; i < end; i++)
        sum += c->items[i].adv;
    return sum;
}

static BOOL can_break_before(const GItem *items, INT i)
{
    const GItem *a = &items[i - 1], *b = &items[i];
    if (a->cls & (CL_SPACE | CL_ZWSP))
        return TRUE;
    if ((a->cls | b->cls) & CL_EMDASH)
        return TRUE;
    if ((a->cls & CL_HYPHEN) && i >= 2 && (items[i - 2].cls & CL_LETTER) && (b->cls & CL_LETTER))
        return TRUE;
    if ((a->cls & (CL_IDEO | CL_OPEN | CL_CLOSE)) && (b->cls & (CL_IDEO | CL_OPEN | CL_CLOSE)) &&
        !(b->cls & CL_CLOSE) && !(a->cls & CL_OPEN))
        return TRUE;
    return FALSE;
}

/* end of the wrapped line starting at pos. */
static INT wrap_line(Ctx *c, INT pos, INT pe)
{
    INT pen = 0, last_break = -1;
    for (INT i = pos; i < pe; i++) {
        GItem *it = &c->items[i];
        INT adv = item_advance(c, it, pen);
        if (c->limit < UNLIMITED && i > pos && !(it->cls & CL_BLANK) && pen + adv > c->limit)
            return last_break > pos ? last_break : i;
        pen += adv;
        if (i + 1 < pe && can_break_before(c->items, i + 1))
            last_break = i + 1;
    }
    return pe;
}

/* ------------------------------------------------------------------ trimming */

static BOOL is_ellipsis_mode(StringTrimming m)
{
    return m == StringTrimmingEllipsisCharacter || m == StringTrimmingEllipsisWord || m == StringTrimmingEllipsisPath;
}

/* what shows of pos..pe when the line does not fit, force always adds the ellipsis. */
static void cut_line(Ctx *c, INT pos, INT pe, BOOL force, LineSpec *spec)
{
    StringTrimming mode = c->trimming;
    spec->a0 = pos;
    spec->a1 = pe;
    spec->ellipsis = FALSE;
    spec->b0 = spec->b1 = 0;
    settle(c, pos, pe);
    if (is_ellipsis_mode(mode) && c->ellipsis_width > c->limit / 2)
        mode = mode == StringTrimmingEllipsisWord ? StringTrimmingWord : StringTrimmingCharacter;
    BOOL ellipsis = is_ellipsis_mode(mode);
    if (visible_width(c, pos, pe) <= c->limit && !(force && ellipsis))
        return;
    INT room = c->limit - (ellipsis ? c->ellipsis_width : 0);
    if (room < 0)
        room = 0;
    GItem *items = c->items;

    if (mode == StringTrimmingEllipsisPath) {
        INT sep = -1;
        for (INT i = pe - 1; i >= pos; i--) {
            if (items[i].cp == '\\' || items[i].cp == '/') {
                sep = i;
                break;
            }
        }
        if (sep >= 0) {
            INT tail_width = visible_width(c, sep, pe);
            spec->ellipsis = TRUE;
            if (tail_width <= room) {
                INT pen = 0, h = pos;
                while (h < sep && pen + items[h].adv <= room - tail_width) {
                    pen += items[h].adv;
                    h++;
                }
                spec->a0 = pos;
                spec->a1 = h;
                spec->b0 = sep;
                spec->b1 = pe;
            } else {
                /* even the last segment is too long, so the end of the line shows. */
                INT pen = 0, s = pe;
                while (s > pos && pen + items[s - 1].adv <= room) {
                    pen += items[s - 1].adv;
                    s--;
                }
                spec->a0 = spec->a1 = pos;
                spec->b0 = s;
                spec->b1 = pe;
            }
            return;
        }
        mode = StringTrimmingEllipsisCharacter;
    }

    INT pen = 0, t = pos;
    while (t < pe && pen + items[t].adv <= room) {
        pen += items[t].adv;
        t++;
    }
    if (t < pe && (mode == StringTrimmingWord || mode == StringTrimmingEllipsisWord)) {
        INT p = t;
        while (p > pos && !(p < pe && can_break_before(items, p)))
            p--;
        if (p > pos)
            t = p;
        if (ellipsis) {
            while (t > pos && (items[t - 1].cls & CL_BLANK))
                t--;
        }
    }
    spec->a1 = t;
    spec->ellipsis = ellipsis;
}

/* ------------------------------------------------------------------ emitting lines */

static GpStatus line_reserve(GTextLayout *L, INT glyphs)
{
    if (L->line_count == L->line_capacity) {
        INT grown = L->line_capacity ? L->line_capacity * 2 : 8;
        GTextLine *more = gp_realloc(L->lines, sizeof(GTextLine) * (SIZE_T)grown);
        if (!more)
            return OutOfMemory;
        L->lines = more;
        L->line_capacity = grown;
    }
    if (L->glyph_count + glyphs > L->glyph_capacity) {
        INT grown = L->glyph_capacity ? L->glyph_capacity : 64;
        while (grown < L->glyph_count + glyphs)
            grown *= 2;
        GTextGlyph *more = gp_realloc(L->glyphs, sizeof(GTextGlyph) * (SIZE_T)grown);
        if (!more)
            return OutOfMemory;
        L->glyphs = more;
        L->glyph_capacity = grown;
    }
    return Ok;
}

static GpStatus rec_reserve(Ctx *c, INT count)
{
    if (count <= c->rec_capacity)
        return Ok;
    INT grown = c->rec_capacity ? c->rec_capacity : 64;
    while (grown < count)
        grown *= 2;
    Rec *more = gp_realloc(c->recs, sizeof(Rec) * (SIZE_T)grown);
    if (!more)
        return OutOfMemory;
    c->recs = more;
    c->rec_capacity = grown;
    return Ok;
}

static void reverse_recs(Rec *r, INT a, INT b)
{
    for (b--; a < b; a++, b--) {
        Rec t = r[a];
        r[a] = r[b];
        r[b] = t;
    }
}

/* display order: reverses runs from the highest level down to the lowest odd one. */
static void reorder_recs(Rec *r, INT n)
{
    int highest = 0, lowest_odd = 255;
    for (INT i = 0; i < n; i++) {
        if (r[i].level > highest)
            highest = r[i].level;
        if ((r[i].level & 1) && r[i].level < lowest_odd)
            lowest_odd = r[i].level;
    }
    for (int level = highest; level >= lowest_odd && level > 0; level--) {
        for (INT i = 0; i < n;) {
            if (r[i].level < level) {
                i++;
                continue;
            }
            INT j = i;
            while (j < n && r[j].level >= level)
                j++;
            reverse_recs(r, i, j);
            i = j;
        }
    }
}

static GpStatus rec_add_items(Ctx *c, INT *n, INT a, INT b)
{
    GpStatus status = rec_reserve(c, *n + (b - a) + 1);
    if (status != Ok)
        return status;
    for (INT i = a; i < b; i++) {
        const GItem *it = &c->items[i];
        if (it->cls & (CL_NEWLINE | CL_HIDDEN))
            continue;
        Rec *r = &c->recs[(*n)++];
        memset(r, 0, sizeof(*r));
        r->g.glyph = it->glyph;
        r->g.face = it->face;
        r->g.hotkey = it->underline;
        r->g.src_begin = it->src_begin;
        r->g.src_end = it->src_end;
        r->adv = it->adv;
        r->level = it->level;
        r->blank = (it->cls & (CL_SPACE | CL_TAB)) != 0;
    }
    return Ok;
}

static GpStatus emit_line(Ctx *c, const LineSpec *spec, INT src_begin, INT src_end)
{
    GTextLayout *L = c->out;
    INT n = 0;
    GpStatus status = rec_add_items(c, &n, spec->a0, spec->a1);
    if (status == Ok && spec->ellipsis) {
        status = rec_reserve(c, n + 4 + (spec->b1 - spec->b0));
        for (INT i = 0; status == Ok && i < c->ellipsis_count; i++) {
            Rec *r = &c->recs[n++];
            memset(r, 0, sizeof(*r));
            r->g.glyph = c->ellipsis[i].glyph;
            r->g.face = c->face;
            r->g.src_begin = r->g.src_end = -1;
            r->adv = c->ellipsis[i].adv;
            r->level = (BYTE)c->base_level;
        }
    }
    if (status == Ok && spec->ellipsis)
        status = rec_add_items(c, &n, spec->b0, spec->b1);
    if (status != Ok)
        return status;

    /* trailing blanks hang over the line end and do not count in its width. */
    INT total = 0, hanging = 0;
    for (INT i = 0; i < n; i++)
        total += c->recs[i].adv;
    if (!c->measure_trailing) {
        for (INT i = n - 1; i >= 0 && c->recs[i].blank; i--) {
            c->recs[i].trailing = TRUE;
            hanging += c->recs[i].adv;
        }
    }
    reorder_recs(c->recs, n);
    INT x = 0, lead_hang = 0;
    for (INT i = 0; i < n; i++) {
        c->recs[i].x = x;
        x += c->recs[i].adv;
    }
    for (INT i = 0; i < n && c->recs[i].trailing; i++)
        lead_hang += c->recs[i].adv;
    INT width = total - hanging;

    status = line_reserve(L, n);
    if (status != Ok)
        return status;
    GTextLine *line = &L->lines[L->line_count++];
    memset(line, 0, sizeof(*line));
    line->first = L->glyph_count;
    line->count = n;
    line->src_begin = src_begin;
    line->src_end = src_end;
    float s = c->scale;
    for (INT i = 0; i < n; i++) {
        GTextGlyph *g = &L->glyphs[L->glyph_count++];
        *g = c->recs[i].g;
        g->x = (float)(c->recs[i].x - lead_hang) / s;
        g->advance = (float)c->recs[i].adv / s;
    }
    line->width = (float)width / s;

    /* where the line starts along the rectangle. */
    INT whole = c->lead_g + width + c->trail_g;
    INT room = c->along_g <= 0 ? -whole : c->along_g - whole;
    StringAlignment align = c->align;
    if (c->rtl && !c->vertical && align != StringAlignmentCenter)
        align = align == StringAlignmentNear ? StringAlignmentFar : StringAlignmentNear;
    INT shift = align == StringAlignmentNear ? 0 : (align == StringAlignmentCenter ? room / 2 : room);
    line->shift = (float)shift / s;
    return Ok;
}

/* ------------------------------------------------------------------ the general path */

static void line_pop(GTextLayout *L)
{
    if (L->line_count > 0) {
        L->line_count--;
        L->glyph_count = L->lines[L->line_count].first;
    }
}

static GpStatus layout_general(Ctx *c)
{
    GTextLayout *L = c->out;
    INT n = c->n;
    INT len = c->in->length;
    BOOL cut_lines = c->nowrap && c->trimming != StringTrimmingNone && c->limit < UNLIMITED;
    float room = c->across_extent > 0.0f ? c->scale * c->across_extent : 0.0f;
    float fraction = 0.0f;
    if (room > 0.0f)
        fraction = (c->flags & StringFormatFlagsLineLimit) ? 1.0f : (c->trimming != StringTrimmingNone ? 0.25f : 0.0f);
    float used = 0.0f;
    BOOL dropped = FALSE;
    INT cursor = 0;
    INT last_pos = 0, last_end = 0, last_pe = 0, last_para_end = 0;
    BOOL last_newline = FALSE;
    GpStatus status = Ok;

    for (INT ps = 0; ps <= n && status == Ok && !dropped;) {
        INT pe = ps;
        while (pe < n && !(c->items[pe].cls & CL_NEWLINE))
            pe++;
        BOOL has_newline = pe < n;
        if (!has_newline && ps == n && n > 0)
            break;
        INT para_end = has_newline ? c->items[pe].src_end : (n ? c->items[n - 1].src_end : len);
        assign_levels(c, ps, pe);
        INT pos = ps;
        do {
            LineSpec spec;
            INT e;
            if (c->nowrap) {
                e = pe;
                if (cut_lines) {
                    cut_line(c, pos, pe, FALSE, &spec);
                } else {
                    settle(c, pos, pe);
                    spec = (LineSpec){pos, pe, FALSE, 0, 0};
                }
            } else {
                e = wrap_line(c, pos, pe);
                spec = (LineSpec){pos, e, FALSE, 0, 0};
            }
            if (fraction > 0.0f && (float)c->pitch_g * fraction > room - used) {
                dropped = TRUE;
                break;
            }
            INT src_end = e < pe ? c->items[e].src_begin : para_end;
            status = emit_line(c, &spec, cursor, src_end);
            if (status != Ok)
                break;
            cursor = src_end;
            used += (float)c->pitch_g;
            last_pos = pos;
            last_end = e;
            last_pe = pe;
            last_para_end = para_end;
            last_newline = has_newline;
            pos = e;
        } while (pos < pe);
        if (!has_newline)
            break;
        ps = pe + 1;
    }
    if (status != Ok)
        return status;

    /* when text was dropped, the last shown line gets the trimming. */
    if (dropped && !c->nowrap && c->trimming != StringTrimmingNone && L->line_count > 0) {
        GTextLine *last = &L->lines[L->line_count - 1];
        if (c->trimming == StringTrimmingWord) {
            if (!(last_end == last_pe && last_newline))
                last->src_end = last_para_end;
        } else {
            INT src_begin = last->src_begin;
            line_pop(L);
            LineSpec spec;
            cut_line(c, last_pos, last_pe, TRUE, &spec);
            status = emit_line(c, &spec, src_begin, last_para_end);
            if (status != Ok)
                return status;
        }
    }

    /* line tops and the block height. */
    for (INT i = 0; i < L->line_count; i++)
        L->lines[i].top = (float)i * (float)c->pitch_g / c->scale;
    BOOL eighth = L->line_count > 0 && (c->no_format || c->lead_fraction != 0.0f);
    L->text_height = (float)L->line_count * (float)c->pitch_g / c->scale + (eighth ? c->size * 0.125f : 0.0f);
    L->fitted = L->line_count ? L->lines[L->line_count - 1].src_end : 0;
    INT across = (INT)mime_vec_floorf(c->across_extent * c->scale + 0.5f);
    INT block = (INT)mime_vec_floorf(L->text_height * c->scale + 0.5f);
    INT free_room = across - block;
    INT shift = c->line_align == StringAlignmentNear ? 0 : (c->line_align == StringAlignmentCenter ? free_room / 2 : free_room);
    L->block_shift = (float)shift / c->scale;
    return Ok;
}

/* ------------------------------------------------------------------ the quick path */

static BOOL layout_quick(Ctx *c, BOOL *done)
{
    const GTextInput *in = c->in;
    *done = FALSE;
    if (in->force_general || !in->has_matrix)
        return TRUE;
    const MimeVecMatrix *m = &in->w2d;
    if (!(m->m11 > 0.0f) || m->m12 != 0.0f || m->m21 != 0.0f || m->m22 == 0.0f)
        return TRUE;
    if (!c->no_format) {
        if (c->flags & (StringFormatFlagsDirectionRightToLeft | StringFormatFlagsDirectionVertical |
                        GP_FMT_UNDOCUMENTED_40000000))
            return TRUE;
        if (c->fmt->range_count)
            return TRUE;
    }
    BOOL digits = FALSE;
    for (INT i = 0; i < c->n; i++) {
        if (c->items[i].slow)
            return TRUE;
        if (c->items[i].cp >= '0' && c->items[i].cp <= '9')
            digits = TRUE;
    }
    if (digits && !c->no_format &&
        (c->fmt->digit_substitute == StringDigitSubstituteNational ||
         c->fmt->digit_substitute == StringDigitSubstituteTraditional))
        return TRUE;
    INT em = c->face->em;
    float glyph_scale = mime_vec_floorf(c->size / (float)em * m->m11 * 65536.0f + 0.5f);
    if (glyph_scale > 65536.0f)
        return TRUE;

    GTextLayout *L = c->out;
    if (line_reserve(L, c->n) != Ok)
        return TRUE;
    float to_world = c->size / (float)em;
    GTextLine *line = &L->lines[0];
    memset(line, 0, sizeof(*line));
    line->first = L->glyph_count;
    /* positions are sums of advances rounded in design units. */
    INT x = 0;
    for (INT i = 0; i < c->n; i++) {
        const GItem *it = &c->items[i];
        INT adv = scaled_advance(gp_face_advance(it->face, it->glyph), c->tracking);
        GTextGlyph *g = &L->glyphs[L->glyph_count++];
        memset(g, 0, sizeof(*g));
        g->glyph = it->glyph;
        g->face = it->face;
        g->hotkey = it->underline;
        g->src_begin = it->src_begin;
        g->src_end = it->src_end;
        g->x = (float)x * to_world;
        g->advance = (float)adv * to_world;
        x += adv;
    }
    /* the width excludes trailing blanks. */
    INT visible = c->n;
    if (!c->measure_trailing) {
        while (visible > 0 && (c->items[visible - 1].cls & CL_SPACE))
            visible--;
    }
    float width = 0.0f;
    if (visible > 0)
        width = L->glyphs[line->first + visible - 1].x + L->glyphs[line->first + visible - 1].advance;
    float lead = c->size * c->lead_fraction, trail = c->size * c->trail_fraction;
    float rect_along = in->w;
    BOOL limits = !(c->nowrap && c->trimming == StringTrimmingNone);
    if (limits && rect_along > 0.0f && width + lead + trail > rect_along) {
        L->glyph_count = 0;
        return TRUE;
    }
    line->count = c->n;
    line->src_begin = 0;
    line->src_end = in->length;
    line->width = width;
    float whole = lead + width + trail;
    float room = rect_along <= 0.0f ? -whole : rect_along - whole;
    line->shift = c->align == StringAlignmentNear ? 0.0f : (c->align == StringAlignmentCenter ? room * 0.5f : room);
    line->top = 0.0f;
    L->line_count = 1;
    L->quick = TRUE;
    L->lead = lead;
    L->trail = trail;
    L->ascent = (float)c->face->ascent * to_world;
    L->descent = (float)c->face->descent * to_world;
    L->pitch = (float)(c->face->ascent + c->face->descent + c->face->gap) * to_world;
    BOOL eighth = c->no_format || c->lead_fraction != 0.0f;
    L->text_height = (float)(c->face->ascent + c->face->descent) * to_world + (eighth ? c->size * 0.125f : 0.0f);
    L->fitted = in->length;
    float free_room = in->h - L->text_height;
    L->block_shift = c->line_align == StringAlignmentNear ? 0.0f
                     : (c->line_align == StringAlignmentCenter ? free_room * 0.5f : free_room);
    *done = TRUE;
    return TRUE;
}

/* ------------------------------------------------------------------ entry points */

void gp_text_layout_free(GTextLayout *layout)
{
    gp_free(layout->glyphs);
    gp_free(layout->lines);
    memset(layout, 0, sizeof(*layout));
}

/* the ellipsis glyph, or three dots, with grid advances from design advances and no tracking. */
static void ellipsis_prepare(Ctx *c)
{
    UINT16 glyph = gp_face_glyph(c->face, 0x2026);
    UINT32 cp = 0x2026;
    c->ellipsis_count = 1;
    if (!glyph) {
        cp = '.';
        glyph = gp_face_glyph(c->face, cp);
        c->ellipsis_count = 3;
    }
    float q = c->size / (float)c->face->em * c->scale;
    c->ellipsis_width = 0;
    for (INT i = 0; i < c->ellipsis_count; i++) {
        c->ellipsis[i].glyph = glyph;
        c->ellipsis[i].adv = scaled_advance(gp_face_advance(c->face, glyph), q);
        c->ellipsis_width += c->ellipsis[i].adv;
    }
}

static void tabs_prepare(Ctx *c)
{
    const GStrFmt *f = c->fmt;
    float s = c->scale;
    if (c->no_format) {
        c->tab_interval = 8192;
        return;
    }
    if (f->tab_count > 0) {
        c->stops = gp_alloc(sizeof(INT) * (SIZE_T)f->tab_count);
        if (c->stops) {
            float cumulative = f->first_tab;
            for (INT i = 0; i < f->tab_count; i++) {
                cumulative += f->tabs[i];
                c->stops[i] = (INT)mime_vec_floorf(cumulative * s + 0.5f);
            }
            c->stop_count = f->tab_count;
            c->has_stops = TRUE;
            c->tab_interval = (INT)mime_vec_floorf(f->tabs[f->tab_count - 1] * s + 0.5f);
        }
    } else {
        c->tab_interval = (INT)mime_vec_floorf(f->first_tab * s + 0.5f);
    }
}

GpStatus gp_text_layout(const GTextInput *in, GTextLayout *out)
{
    Ctx c;
    memset(&c, 0, sizeof(c));
    memset(out, 0, sizeof(*out));
    /* a size the grid cannot hold gives no text at all. */
    if (!(in->size > 1.0e-4f) || in->size > 1.0e7f || in->length <= 0) {
        out->x = in->x;
        out->y = in->y;
        out->w = in->w;
        out->h = in->h;
        return Ok;
    }
    c.in = in;
    c.out = out;
    c.no_format = in->format == nullptr;
    c.fmt = in->format ? in->format : gp_strfmt_default();
    c.flags = c.no_format ? 0 : c.fmt->flags;
    c.trimming = c.fmt->trimming;
    c.align = c.no_format ? StringAlignmentNear : c.fmt->align;
    c.line_align = c.no_format ? StringAlignmentNear : c.fmt->line_align;
    c.lead_fraction = c.fmt->lead_margin;
    c.trail_fraction = c.fmt->trail_margin;
    c.tracking = c.fmt->tracking;
    c.size = in->size;
    c.nowrap = (c.flags & StringFormatFlagsNoWrap) != 0;
    c.vertical = (c.flags & StringFormatFlagsDirectionVertical) != 0;
    c.rtl = (c.flags & StringFormatFlagsDirectionRightToLeft) != 0;
    c.fallback = !(c.flags & StringFormatFlagsNoFontFallback);
    c.measure_trailing = (c.flags & StringFormatFlagsMeasureTrailingSpaces) != 0;
    c.base_level = c.rtl ? 1 : 0;
    c.along_extent = c.vertical ? in->h : in->w;
    c.across_extent = c.vertical ? in->w : in->h;
    INT slot = 0;
    c.face = gp_family_face(in->family, in->style, &slot);
    if (!c.face)
        return FontStyleNotFound;
    out->face = c.face;
    out->slot = slot;
    out->size = in->size;
    out->vertical = c.vertical;
    out->right_to_left = c.rtl;
    out->no_clip = (c.flags & StringFormatFlagsNoClip) != 0;
    out->x = in->x;
    out->y = in->y;
    out->w = in->w;
    out->h = in->h;

    GpStatus status = build_items(&c);
    BOOL quick_done = FALSE;
    if (status == Ok)
        layout_quick(&c, &quick_done);
    if (status != Ok || quick_done)
        goto done;

    /* the grid of the general path. */
    c.scale = (float)(2048.0 / (double)c.size);
    c.along_g = (INT)mime_vec_floorf(c.along_extent * c.scale + 0.5f);
    c.lead_g = (INT)mime_vec_floorf(c.size * c.scale * c.lead_fraction + 0.5f);
    c.trail_g = (INT)mime_vec_floorf(c.size * c.scale * c.trail_fraction + 0.5f);
    if (c.along_g <= 0 || (c.nowrap && c.trimming == StringTrimmingNone)) {
        c.limit = UNLIMITED;
    } else {
        c.limit = c.along_g - c.lead_g - c.trail_g;
        if (c.limit < 0)
            c.limit = 0;
    }
    {
        float q = c.size / (float)c.face->em * c.scale;
        float factor = q * c.tracking;
        for (INT i = 0; i < c.n; i++) {
            GItem *it = &c.items[i];
            if (!it->face)
                continue;
            double f = it->face == c.face ? (double)factor : (double)(c.size / (float)it->face->em * c.scale * c.tracking);
            it->adv = scaled_advance(gp_face_advance(it->face, it->glyph), f);
        }
        c.ascent_g = scaled_advance(c.face->ascent, q);
        c.descent_g = scaled_advance(c.face->descent, q);
        c.pitch_g = scaled_advance(c.face->ascent + c.face->descent + c.face->gap, q);
    }
    ellipsis_prepare(&c);
    tabs_prepare(&c);
    out->lead = (float)c.lead_g / c.scale;
    out->trail = (float)c.trail_g / c.scale;
    out->ascent = (float)c.ascent_g / c.scale;
    out->descent = (float)c.descent_g / c.scale;
    out->pitch = (float)c.pitch_g / c.scale;
    status = layout_general(&c);

done:
    gp_free(c.items);
    gp_free(c.recs);
    gp_free(c.stops);
    if (status != Ok)
        gp_text_layout_free(out);
    return status;
}

void gp_text_layout_box(const GTextLayout *L, RectF *box)
{
    float left = 0.0f, right = 0.0f;
    if (L->line_count) {
        left = L->lines[0].shift;
        right = left + L->lead + L->lines[0].width + L->trail;
        for (INT i = 1; i < L->line_count; i++) {
            float s = L->lines[i].shift;
            float r = s + L->lead + L->lines[i].width + L->trail;
            if (s < left)
                left = s;
            if (r > right)
                right = r;
        }
    }
    float block = L->text_height;
    if (L->vertical) {
        float room = L->w - block;
        float shift = L->block_shift;
        box->Y = L->y + left;
        box->Height = right - left;
        box->X = L->right_to_left ? L->x + (room - shift) : L->x + shift;
        box->Width = block;
    } else {
        box->X = L->x + left;
        box->Width = right - left;
        box->Y = L->y + L->block_shift;
        box->Height = block;
    }
    if (!L->no_clip) {
        if (L->w > 0.0f && box->Width > L->w) {
            box->Width = L->w;
            box->X = L->x;
        }
        if (L->h > 0.0f && box->Height > L->h) {
            box->Height = L->h;
            if (!L->quick || L->vertical)
                box->Y = L->y;
        }
    }
}
