/*
 * the opentype shaper behind IDWriteTextAnalyzer::GetGlyphs and GetGlyphPlacements: the character map, the glyph
 * substitution and positioning tables, and the clusters that come out of them.
 */
#include "text_private.h"

typedef struct Tbl {
    const BYTE *d;
    UINT32 n;
    void *ctx;
} Tbl;

struct DwOtFont {
    IDWriteFontFace *face;
    DwFace *own;
    UINT16 upem;
    UINT16 glyph_count;
    Tbl gdef, gsub, gpos;
    BOOL has_kern_feature;
};

static UINT32 rd16(const Tbl *t, UINT32 off)
{
    if (off + 2 > t->n)
        return 0;
    return DwBe16(t->d + off);
}

static INT32 rds16(const Tbl *t, UINT32 off) { return (INT16)rd16(t, off); }

static UINT32 rd32(const Tbl *t, UINT32 off)
{
    if (off + 4 > t->n)
        return 0;
    return DwBe32(t->d + off);
}

static void load_table(IDWriteFontFace *face, UINT32 tag, Tbl *t)
{
    const void *data = nullptr;
    UINT32 size = 0;
    void *ctx = nullptr;
    BOOL exists = FALSE;
    memset(t, 0, sizeof(*t));
    if (SUCCEEDED(face->lpVtbl->TryGetFontTable(face, tag, &data, &size, &ctx, &exists)) && exists) {
        t->d = data;
        t->n = size;
        t->ctx = ctx;
    }
}

DwOtFont *DwOtOpen(IDWriteFontFace *face)
{
    DwOtFont *f = DwAllocZero(sizeof(*f));
    if (!f)
        return nullptr;
    f->face = face;
    face->lpVtbl->AddRef(face);
    f->own = DwFaceFrom(face);
    DWRITE_FONT_METRICS m;
    face->lpVtbl->GetMetrics(face, &m);
    f->upem = m.designUnitsPerEm ? m.designUnitsPerEm : 2048;
    f->glyph_count = face->lpVtbl->GetGlyphCount(face);
    load_table(face, DW_TAG_LE('G', 'D', 'E', 'F'), &f->gdef);
    load_table(face, DW_TAG_LE('G', 'S', 'U', 'B'), &f->gsub);
    load_table(face, DW_TAG_LE('G', 'P', 'O', 'S'), &f->gpos);
    return f;
}

void DwOtClose(DwOtFont *f)
{
    if (!f)
        return;
    if (f->gdef.d)
        f->face->lpVtbl->ReleaseFontTable(f->face, f->gdef.ctx);
    if (f->gsub.d)
        f->face->lpVtbl->ReleaseFontTable(f->face, f->gsub.ctx);
    if (f->gpos.d)
        f->face->lpVtbl->ReleaseFontTable(f->face, f->gpos.ctx);
    f->face->lpVtbl->Release(f->face);
    DwFree(f);
}

UINT16 DwOtUnitsPerEm(const DwOtFont *f) { return f->upem; }

INT32 DwOtAdvance(const DwOtFont *f, UINT16 glyph)
{
    if (f->own) {
        int advance = 0;
        mime_ttf_glyph_hmetrics(&f->own->data->ttf, glyph, &advance, nullptr);
        return advance;
    }
    DWRITE_GLYPH_METRICS gm;
    if (SUCCEEDED(f->face->lpVtbl->GetDesignGlyphMetrics(f->face, &glyph, 1, &gm, FALSE)))
        return (INT32)gm.advanceWidth;
    return 0;
}

/* ------------------------------------------------------------------ shared table structures */

static int coverage_index(const Tbl *t, UINT32 off, UINT32 gid)
{
    if (!off)
        return -1;
    UINT32 format = rd16(t, off);
    if (format == 1) {
        UINT32 count = rd16(t, off + 2);
        UINT32 lo = 0, hi = count;
        while (lo < hi) {
            UINT32 mid = (lo + hi) / 2;
            UINT32 g = rd16(t, off + 4 + mid * 2);
            if (g == gid)
                return (int)mid;
            if (g < gid)
                lo = mid + 1;
            else
                hi = mid;
        }
    } else if (format == 2) {
        UINT32 count = rd16(t, off + 2);
        UINT32 lo = 0, hi = count;
        while (lo < hi) {
            UINT32 mid = (lo + hi) / 2;
            UINT32 r = off + 4 + mid * 6;
            UINT32 first = rd16(t, r), last = rd16(t, r + 2);
            if (gid < first)
                hi = mid;
            else if (gid > last)
                lo = mid + 1;
            else
                return (int)(rd16(t, r + 4) + gid - first);
        }
    }
    return -1;
}

static UINT32 class_of(const Tbl *t, UINT32 off, UINT32 gid)
{
    if (!off)
        return 0;
    UINT32 format = rd16(t, off);
    if (format == 1) {
        UINT32 first = rd16(t, off + 2), count = rd16(t, off + 4);
        if (gid >= first && gid < first + count)
            return rd16(t, off + 6 + (gid - first) * 2);
    } else if (format == 2) {
        UINT32 count = rd16(t, off + 2);
        UINT32 lo = 0, hi = count;
        while (lo < hi) {
            UINT32 mid = (lo + hi) / 2;
            UINT32 r = off + 4 + mid * 6;
            UINT32 first = rd16(t, r), last = rd16(t, r + 2);
            if (gid < first)
                hi = mid;
            else if (gid > last)
                lo = mid + 1;
            else
                return rd16(t, r + 4);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ the glyph buffer */

typedef struct ShGlyph {
    UINT16 gid;
    UINT32 cp;
    UINT32 cs, ce; /* the code units this glyph stands for. */
    UINT64 mask;
    INT32 xadv, xoff, yoff;
    INT32 dx, dy; /* offset to the attachment base. */
    INT32 attach; /* index of the glyph this one is attached to, or -1. */
    UINT8 gclass; /* the glyph definition class. */
    UINT8 alt;
    UINT8 comps; /* components of a ligature. */
    UINT8 zero_width;
} ShGlyph;

typedef struct Buf {
    ShGlyph *g;
    UINT32 n, cap;
} Buf;

static BOOL buf_reserve(Buf *b, UINT32 count)
{
    if (count <= b->cap)
        return TRUE;
    UINT32 cap = b->cap ? b->cap : 16;
    while (cap < count)
        cap *= 2;
    ShGlyph *grown = DwAlloc(cap * sizeof(ShGlyph));
    if (!grown)
        return FALSE;
    if (b->n)
        memcpy(grown, b->g, b->n * sizeof(ShGlyph));
    DwFree(b->g);
    b->g = grown;
    b->cap = cap;
    return TRUE;
}

static void buf_remove(Buf *b, UINT32 index)
{
    memmove(&b->g[index], &b->g[index + 1], (b->n - index - 1) * sizeof(ShGlyph));
    b->n--;
}

static BOOL buf_insert_after(Buf *b, UINT32 index, UINT32 count)
{
    if (!buf_reserve(b, b->n + count))
        return FALSE;
    memmove(&b->g[index + 1 + count], &b->g[index + 1], (b->n - index - 1) * sizeof(ShGlyph));
    for (UINT32 k = 0; k < count; k++)
        b->g[index + 1 + k] = b->g[index];
    b->n += count;
    return TRUE;
}

/* ------------------------------------------------------------------ lookups */

typedef struct Ctx {
    const DwOtFont *font;
    const Tbl *t;
    BOOL gpos;
    BOOL rtl;
    Buf *b;
    UINT32 lookup_flag;
    UINT32 mark_set;
    UINT32 depth;
    INT32 delta; /* how much the last substitution changed the glyph count. */
} Ctx;

static UINT32 lookup_list(const Tbl *t) { return rd16(t, 8); }

static UINT32 lookup_offset(const Tbl *t, UINT32 index)
{
    UINT32 list = lookup_list(t);
    if (!list || index >= rd16(t, list))
        return 0;
    UINT32 off = rd16(t, list + 2 + index * 2);
    return off ? list + off : 0;
}

/* whether the lookup flags skip a glyph. */
static BOOL skip_glyph(const Ctx *c, const ShGlyph *g)
{
    UINT32 f = c->lookup_flag;
    if (g->gclass == 1 && (f & 0x02))
        return TRUE;
    if (g->gclass == 2 && (f & 0x04))
        return TRUE;
    if (g->gclass == 3) {
        const Tbl *gd = &c->font->gdef;
        if (f & 0x08)
            return TRUE;
        if (f & 0x10) {
            /* the mark glyph sets of a version 1.2 definition table. */
            UINT32 sets = rd16(gd, 2) >= 2 ? rd16(gd, 10 + 2) : 0;
            if (sets) {
                UINT32 count = rd16(gd, sets + 2);
                if (c->mark_set < count) {
                    UINT32 cov = rd32(gd, sets + 4 + c->mark_set * 4);
                    if (coverage_index(gd, sets + cov, g->gid) < 0)
                        return TRUE;
                }
            }
        }
        if (f & 0xFF00) {
            UINT32 off = rd16(gd, 10);
            if (off && class_of(gd, off, g->gid) != (f >> 8))
                return TRUE;
        }
    }
    return FALSE;
}

/* the next glyph at or after pos the flags do not skip, or the buffer size. */
static UINT32 next_glyph(const Ctx *c, UINT32 pos)
{
    while (pos < c->b->n && skip_glyph(c, &c->b->g[pos]))
        pos++;
    return pos;
}

/* the previous glyph before pos, or 0xFFFFFFFF. */
static UINT32 prev_glyph(const Ctx *c, UINT32 pos)
{
    while (pos > 0) {
        pos--;
        if (!skip_glyph(c, &c->b->g[pos]))
            return pos;
    }
    return 0xFFFFFFFF;
}

static BOOL apply_lookup(Ctx *c, UINT32 lookup_index, UINT32 pos);

/* nested lookups of a matched context: the positions are those of the matched glyphs. */
static void apply_records(Ctx *c, UINT32 records, UINT32 count, const UINT32 *matched, UINT32 matched_count)
{
    INT32 shift = 0;
    for (UINT32 r = 0; r < count; r++) {
        UINT32 seq = rd16(c->t, records + r * 4);
        UINT32 lookup = rd16(c->t, records + r * 4 + 2);
        if (seq >= matched_count)
            continue;
        INT64 at = (INT64)matched[seq] + shift;
        if (at < 0 || at >= (INT64)c->b->n)
            continue;
        UINT32 before = c->b->n;
        UINT32 saved_flag = c->lookup_flag, saved_set = c->mark_set;
        apply_lookup(c, lookup, (UINT32)at);
        c->lookup_flag = saved_flag;
        c->mark_set = saved_set;
        shift += (INT32)c->b->n - (INT32)before;
    }
}

typedef struct Match {
    UINT32 pos[32];
    UINT32 count;
} Match;

/* matches count glyphs after the glyph at pos, each by the test, and fills the matched positions. */
typedef BOOL (*GlyphTest)(const Ctx *c, UINT32 gid, UINT32 value, UINT32 data);

static BOOL test_glyph(const Ctx *c, UINT32 gid, UINT32 value, UINT32 data) { return gid == value; }

static BOOL test_class(const Ctx *c, UINT32 gid, UINT32 value, UINT32 data)
{
    return class_of(c->t, data, gid) == value;
}

/* forward matches of count items starting after pos; items at array + 2 * k. */
static BOOL match_forward(Ctx *c, UINT32 pos, UINT32 array, UINT32 count, GlyphTest test, UINT32 data, UINT32 stride,
                          Match *m)
{
    UINT32 at = pos;
    for (UINT32 k = 0; k < count; k++) {
        at = next_glyph(c, at + 1);
        if (at >= c->b->n)
            return FALSE;
        UINT32 v = stride == 4 ? rd32(c->t, array + k * 4) : rd16(c->t, array + k * 2);
        if (!test(c, c->b->g[at].gid, v, data))
            return FALSE;
        if (m && m->count < 32)
            m->pos[m->count++] = at;
    }
    return TRUE;
}

static BOOL match_backward(Ctx *c, UINT32 pos, UINT32 array, UINT32 count, GlyphTest test, UINT32 data)
{
    UINT32 at = pos;
    for (UINT32 k = 0; k < count; k++) {
        at = prev_glyph(c, at);
        if (at == 0xFFFFFFFF)
            return FALSE;
        if (!test(c, c->b->g[at].gid, rd16(c->t, array + k * 2), data))
            return FALSE;
    }
    return TRUE;
}

/* a context lookup of either table, with the format at sub; chained or not. */
static BOOL apply_context(Ctx *c, UINT32 sub, UINT32 pos, BOOL chained)
{
    const Tbl *t = c->t;
    UINT32 format = rd16(t, sub);
    UINT32 gid = c->b->g[pos].gid;
    Match m;
    m.count = 1;
    m.pos[0] = pos;
    if (format == 1) {
        int ci = coverage_index(t, sub + rd16(t, sub + 2), gid);
        if (ci < 0)
            return FALSE;
        UINT32 set = sub + rd16(t, sub + 6 + ci * 2);
        if (!rd16(t, sub + 6 + ci * 2))
            return FALSE;
        UINT32 rules = rd16(t, set);
        for (UINT32 r = 0; r < rules; r++) {
            UINT32 rule = set + rd16(t, set + 2 + r * 2);
            m.count = 1;
            if (!chained) {
                UINT32 gc = rd16(t, rule), sc = rd16(t, rule + 2);
                if (gc < 1 || !match_forward(c, pos, rule + 4, gc - 1, test_glyph, 0, 2, &m))
                    continue;
                apply_records(c, rule + 4 + (gc - 1) * 2, sc, m.pos, m.count);
                return TRUE;
            } else {
                UINT32 bc = rd16(t, rule);
                UINT32 backtrack = rule + 2;
                UINT32 ic = rd16(t, backtrack + bc * 2);
                UINT32 input = backtrack + bc * 2 + 2;
                UINT32 lc = rd16(t, input + (ic ? ic - 1 : 0) * 2);
                UINT32 look = input + (ic ? ic - 1 : 0) * 2 + 2;
                UINT32 sc = rd16(t, look + lc * 2);
                if (ic < 1 || !match_backward(c, pos, backtrack, bc, test_glyph, 0))
                    continue;
                if (!match_forward(c, pos, input, ic - 1, test_glyph, 0, 2, &m))
                    continue;
                if (!match_forward(c, m.pos[m.count - 1], look, lc, test_glyph, 0, 2, nullptr))
                    continue;
                apply_records(c, look + lc * 2 + 2, sc, m.pos, m.count);
                return TRUE;
            }
        }
        return FALSE;
    }
    if (format == 2) {
        if (coverage_index(t, sub + rd16(t, sub + 2), gid) < 0)
            return FALSE;
        if (!chained) {
            UINT32 class_def = sub + rd16(t, sub + 4);
            UINT32 cls = class_of(t, class_def, gid);
            UINT32 sets = rd16(t, sub + 6);
            if (cls >= sets || !rd16(t, sub + 8 + cls * 2))
                return FALSE;
            UINT32 set = sub + rd16(t, sub + 8 + cls * 2);
            UINT32 rules = rd16(t, set);
            for (UINT32 r = 0; r < rules; r++) {
                UINT32 rule = set + rd16(t, set + 2 + r * 2);
                UINT32 gc = rd16(t, rule), sc = rd16(t, rule + 2);
                m.count = 1;
                if (gc < 1 || !match_forward(c, pos, rule + 4, gc - 1, test_class, class_def, 2, &m))
                    continue;
                apply_records(c, rule + 4 + (gc - 1) * 2, sc, m.pos, m.count);
                return TRUE;
            }
            return FALSE;
        }
        UINT32 back_def = sub + rd16(t, sub + 4), in_def = sub + rd16(t, sub + 6), look_def = sub + rd16(t, sub + 8);
        UINT32 sets = rd16(t, sub + 10);
        UINT32 cls = class_of(t, in_def, gid);
        if (cls >= sets || !rd16(t, sub + 12 + cls * 2))
            return FALSE;
        UINT32 set = sub + rd16(t, sub + 12 + cls * 2);
        UINT32 rules = rd16(t, set);
        for (UINT32 r = 0; r < rules; r++) {
            UINT32 rule = set + rd16(t, set + 2 + r * 2);
            UINT32 bc = rd16(t, rule);
            UINT32 backtrack = rule + 2;
            UINT32 ic = rd16(t, backtrack + bc * 2);
            UINT32 input = backtrack + bc * 2 + 2;
            UINT32 lc = rd16(t, input + (ic ? ic - 1 : 0) * 2);
            UINT32 look = input + (ic ? ic - 1 : 0) * 2 + 2;
            UINT32 sc = rd16(t, look + lc * 2);
            m.count = 1;
            if (ic < 1 || !match_backward(c, pos, backtrack, bc, test_class, back_def))
                continue;
            if (!match_forward(c, pos, input, ic - 1, test_class, in_def, 2, &m))
                continue;
            if (!match_forward(c, m.pos[m.count - 1], look, lc, test_class, look_def, 2, nullptr))
                continue;
            apply_records(c, look + lc * 2 + 2, sc, m.pos, m.count);
            return TRUE;
        }
        return FALSE;
    }
    if (format == 3) {
        if (!chained) {
            UINT32 gc = rd16(t, sub + 2), sc = rd16(t, sub + 4);
            if (gc < 1 || coverage_index(t, sub + rd16(t, sub + 6), gid) < 0)
                return FALSE;
            UINT32 at = pos;
            for (UINT32 k = 1; k < gc; k++) {
                at = next_glyph(c, at + 1);
                if (at >= c->b->n || coverage_index(t, sub + rd16(t, sub + 6 + k * 2), c->b->g[at].gid) < 0)
                    return FALSE;
                m.pos[m.count++] = at;
            }
            apply_records(c, sub + 6 + gc * 2, sc, m.pos, m.count);
            return TRUE;
        }
        UINT32 bc = rd16(t, sub + 2);
        UINT32 backtrack = sub + 4;
        UINT32 ic = rd16(t, backtrack + bc * 2);
        UINT32 input = backtrack + bc * 2 + 2;
        UINT32 lc = rd16(t, input + ic * 2);
        UINT32 look = input + ic * 2 + 2;
        UINT32 sc = rd16(t, look + lc * 2);
        if (ic < 1 || coverage_index(t, sub + rd16(t, input), gid) < 0)
            return FALSE;
        UINT32 at = pos;
        for (UINT32 k = 0; k < bc; k++) {
            at = prev_glyph(c, at);
            if (at == 0xFFFFFFFF || coverage_index(t, sub + rd16(t, backtrack + 2 * k), c->b->g[at].gid) < 0)
                return FALSE;
        }
        at = pos;
        for (UINT32 k = 1; k < ic; k++) {
            at = next_glyph(c, at + 1);
            if (at >= c->b->n || coverage_index(t, sub + rd16(t, input + 2 + (k - 1) * 2 + 0), c->b->g[at].gid) < 0)
                return FALSE;
            if (m.count < 32)
                m.pos[m.count++] = at;
        }
        UINT32 la = m.pos[m.count - 1];
        for (UINT32 k = 0; k < lc; k++) {
            la = next_glyph(c, la + 1);
            if (la >= c->b->n || coverage_index(t, sub + rd16(t, look + 2 + k * 2), c->b->g[la].gid) < 0)
                return FALSE;
        }
        apply_records(c, look + lc * 2 + 2, sc, m.pos, m.count);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ substitutions */

static BOOL gsub_subtable(Ctx *c, UINT32 type, UINT32 sub, UINT32 pos)
{
    const Tbl *t = c->t;
    Buf *b = c->b;
    ShGlyph *g = &b->g[pos];
    UINT32 gid = g->gid;
    switch (type) {
    case 1: {
        UINT32 format = rd16(t, sub);
        int ci = coverage_index(t, sub + rd16(t, sub + 2), gid);
        if (ci < 0)
            return FALSE;
        if (format == 1)
            g->gid = (UINT16)(gid + rds16(t, sub + 4));
        else if (format == 2 && (UINT32)ci < rd16(t, sub + 4))
            g->gid = (UINT16)rd16(t, sub + 6 + ci * 2);
        else
            return FALSE;
        return TRUE;
    }
    case 2: {
        int ci = coverage_index(t, sub + rd16(t, sub + 2), gid);
        if (ci < 0 || (UINT32)ci >= rd16(t, sub + 4))
            return FALSE;
        UINT32 seq = sub + rd16(t, sub + 6 + ci * 2);
        UINT32 count = rd16(t, seq);
        if (count == 0) {
            if (b->n > 1) {
                /* the code units of a deleted glyph go to its neighbour. */
                if (pos + 1 < b->n)
                    b->g[pos + 1].cs = g->cs < b->g[pos + 1].cs ? g->cs : b->g[pos + 1].cs;
                buf_remove(b, pos);
            }
            c->delta = -1;
            return TRUE;
        }
        if (count > 1 && !buf_insert_after(b, pos, count - 1))
            return FALSE;
        for (UINT32 k = 0; k < count; k++)
            b->g[pos + k].gid = (UINT16)rd16(t, seq + 2 + k * 2);
        c->delta = (INT32)count - 1;
        return TRUE;
    }
    case 3: {
        int ci = coverage_index(t, sub + rd16(t, sub + 2), gid);
        if (ci < 0 || (UINT32)ci >= rd16(t, sub + 4))
            return FALSE;
        UINT32 set = sub + rd16(t, sub + 6 + ci * 2);
        UINT32 count = rd16(t, set);
        if (!count)
            return FALSE;
        UINT32 pick = g->alt > 0 ? g->alt - 1 : 0;
        if (pick >= count)
            pick = count - 1;
        g->gid = (UINT16)rd16(t, set + 2 + pick * 2);
        return TRUE;
    }
    case 4: {
        int ci = coverage_index(t, sub + rd16(t, sub + 2), gid);
        if (ci < 0 || (UINT32)ci >= rd16(t, sub + 4))
            return FALSE;
        UINT32 set = sub + rd16(t, sub + 6 + ci * 2);
        UINT32 count = rd16(t, set);
        for (UINT32 l = 0; l < count; l++) {
            UINT32 lig = set + rd16(t, set + 2 + l * 2);
            UINT32 result = rd16(t, lig), comps = rd16(t, lig + 2);
            if (comps < 1)
                continue;
            UINT32 at = pos, found[16], nf = 0;
            BOOL ok = TRUE;
            for (UINT32 k = 1; k < comps && ok; k++) {
                at = next_glyph(c, at + 1);
                if (at >= b->n || b->g[at].gid != rd16(t, lig + 4 + (k - 1) * 2)) {
                    ok = FALSE;
                    break;
                }
                if (nf < 16)
                    found[nf++] = at;
            }
            if (!ok)
                continue;
            ShGlyph *first = &b->g[pos];
            first->gid = (UINT16)result;
            first->comps = (UINT8)comps;
            for (UINT32 k = 0; k < nf; k++) {
                const ShGlyph *o = &b->g[found[k]];
                if (o->cs < first->cs)
                    first->cs = o->cs;
                if (o->ce > first->ce)
                    first->ce = o->ce;
            }
            for (UINT32 k = nf; k > 0; k--)
                buf_remove(b, found[k - 1]);
            c->delta = -(INT32)nf;
            return TRUE;
        }
        return FALSE;
    }
    case 5:
        return apply_context(c, sub, pos, FALSE);
    case 6:
        return apply_context(c, sub, pos, TRUE);
    case 8: {
        /* reverse chaining: backtrack glyphs are before, lookahead after. */
        if (rd16(t, sub) != 1)
            return FALSE;
        int ci = coverage_index(t, sub + rd16(t, sub + 2), gid);
        if (ci < 0)
            return FALSE;
        UINT32 bc = rd16(t, sub + 4);
        UINT32 lcp = sub + 6 + bc * 2;
        UINT32 lc = rd16(t, lcp);
        UINT32 sp = lcp + 2 + lc * 2;
        UINT32 sc = rd16(t, sp);
        if ((UINT32)ci >= sc)
            return FALSE;
        UINT32 at = pos;
        for (UINT32 k = 0; k < bc; k++) {
            at = prev_glyph(c, at);
            if (at == 0xFFFFFFFF || coverage_index(t, sub + rd16(t, sub + 6 + k * 2), b->g[at].gid) < 0)
                return FALSE;
        }
        at = pos;
        for (UINT32 k = 0; k < lc; k++) {
            at = next_glyph(c, at + 1);
            if (at >= b->n || coverage_index(t, sub + rd16(t, lcp + 2 + k * 2), b->g[at].gid) < 0)
                return FALSE;
        }
        g->gid = (UINT16)rd16(t, sp + 2 + ci * 2);
        return TRUE;
    }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ positioning */

static UINT32 value_size(UINT32 format)
{
    UINT32 n = 0;
    for (; format; format &= format - 1)
        n++;
    return n * 2;
}

/* reads a value record into the placement and advance, and returns how far it reads. */
static void read_value(const Tbl *t, UINT32 off, UINT32 format, INT32 *x, INT32 *y, INT32 *xa)
{
    *x = *y = *xa = 0;
    if (format & 1) {
        *x = rds16(t, off);
        off += 2;
    }
    if (format & 2) {
        *y = rds16(t, off);
        off += 2;
    }
    if (format & 4)
        *xa = rds16(t, off);
}

static void apply_value(ShGlyph *g, const Tbl *t, UINT32 off, UINT32 format)
{
    if (!format)
        return;
    INT32 x, y, xa;
    read_value(t, off, format, &x, &y, &xa);
    g->xoff += x;
    g->yoff += y;
    g->xadv += xa;
}

static BOOL anchor_xy(const Tbl *t, UINT32 off, INT32 *x, INT32 *y)
{
    if (!off || off + 6 > t->n)
        return FALSE;
    *x = rds16(t, off + 2);
    *y = rds16(t, off + 4);
    return TRUE;
}

static BOOL gpos_subtable(Ctx *c, UINT32 type, UINT32 sub, UINT32 pos)
{
    const Tbl *t = c->t;
    Buf *b = c->b;
    ShGlyph *g = &b->g[pos];
    UINT32 gid = g->gid;
    switch (type) {
    case 1: {
        UINT32 format = rd16(t, sub);
        int ci = coverage_index(t, sub + rd16(t, sub + 2), gid);
        if (ci < 0)
            return FALSE;
        UINT32 vf = rd16(t, sub + 4);
        if (format == 1)
            apply_value(g, t, sub + 6, vf);
        else if (format == 2 && (UINT32)ci < rd16(t, sub + 6))
            apply_value(g, t, sub + 8 + ci * value_size(vf), vf);
        else
            return FALSE;
        return TRUE;
    }
    case 2: {
        UINT32 format = rd16(t, sub);
        int ci = coverage_index(t, sub + rd16(t, sub + 2), gid);
        if (ci < 0)
            return FALSE;
        UINT32 second = next_glyph(c, pos + 1);
        if (second >= b->n)
            return FALSE;
        UINT32 vf1 = rd16(t, sub + 4), vf2 = rd16(t, sub + 6);
        UINT32 s1 = value_size(vf1), s2 = value_size(vf2);
        UINT32 gid2 = b->g[second].gid;
        if (format == 1) {
            UINT32 sets = rd16(t, sub + 8);
            if ((UINT32)ci >= sets)
                return FALSE;
            UINT32 set = sub + rd16(t, sub + 10 + ci * 2);
            UINT32 count = rd16(t, set);
            UINT32 rec = 2 + s1 + s2;
            UINT32 lo = 0, hi = count;
            while (lo < hi) {
                UINT32 mid = (lo + hi) / 2;
                UINT32 sg = rd16(t, set + 2 + mid * rec);
                if (sg == gid2) {
                    apply_value(g, t, set + 2 + mid * rec + 2, vf1);
                    apply_value(&b->g[second], t, set + 2 + mid * rec + 2 + s1, vf2);
                    return TRUE;
                }
                if (sg < gid2)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            return FALSE;
        }
        if (format == 2) {
            UINT32 d1 = sub + rd16(t, sub + 8), d2 = sub + rd16(t, sub + 10);
            UINT32 n1 = rd16(t, sub + 12), n2 = rd16(t, sub + 14);
            UINT32 k1 = class_of(t, d1, gid), k2 = class_of(t, d2, gid2);
            if (k1 >= n1 || k2 >= n2)
                return FALSE;
            UINT32 rec = s1 + s2;
            UINT32 at = sub + 16 + (k1 * n2 + k2) * rec;
            apply_value(g, t, at, vf1);
            apply_value(&b->g[second], t, at + s1, vf2);
            return TRUE;
        }
        return FALSE;
    }
    case 3: {
        /* cursive attachment: the exit of the previous glyph meets the entry of this one. */
        if (rd16(t, sub) != 1)
            return FALSE;
        int ci = coverage_index(t, sub + rd16(t, sub + 2), gid);
        if (ci < 0 || (UINT32)ci >= rd16(t, sub + 4))
            return FALSE;
        UINT32 entry = rd16(t, sub + 6 + ci * 4);
        UINT32 prev = prev_glyph(c, pos);
        if (!entry || prev == 0xFFFFFFFF)
            return FALSE;
        int pi = coverage_index(t, sub + rd16(t, sub + 2), b->g[prev].gid);
        if (pi < 0 || (UINT32)pi >= rd16(t, sub + 4))
            return FALSE;
        UINT32 exit = rd16(t, sub + 6 + pi * 4 + 2);
        INT32 ex, ey, nx, ny;
        if (!exit || !anchor_xy(t, sub + exit, &ex, &ey) || !anchor_xy(t, sub + entry, &nx, &ny))
            return FALSE;
        g->yoff = b->g[prev].yoff + ey - ny;
        return TRUE;
    }
    case 4:
    case 5:
    case 6: {
        if (rd16(t, sub) != 1)
            return FALSE;
        int mi = coverage_index(t, sub + rd16(t, sub + 2), gid);
        if (mi < 0)
            return FALSE;
        UINT32 class_count = rd16(t, sub + 6);
        UINT32 mark_array = sub + rd16(t, sub + 8);
        if ((UINT32)mi >= rd16(t, mark_array))
            return FALSE;
        UINT32 mark_class = rd16(t, mark_array + 2 + mi * 4);
        UINT32 mark_anchor = rd16(t, mark_array + 4 + mi * 4);
        INT32 mx, my;
        if (mark_class >= class_count || !anchor_xy(t, mark_array + mark_anchor, &mx, &my))
            return FALSE;
        UINT32 base = 0xFFFFFFFF;
        if (type == 6) {
            base = prev_glyph(c, pos);
            if (base == 0xFFFFFFFF || b->g[base].gclass != 3)
                return FALSE;
        } else {
            UINT32 at = pos;
            while (at > 0) {
                at--;
                if (b->g[at].gclass != 3) {
                    base = at;
                    break;
                }
            }
            if (base == 0xFFFFFFFF)
                return FALSE;
        }
        int bi = coverage_index(t, sub + rd16(t, sub + 4), b->g[base].gid);
        if (bi < 0)
            return FALSE;
        UINT32 array = sub + rd16(t, sub + 10);
        UINT32 anchor_off;
        if (type == 5) {
            if ((UINT32)bi >= rd16(t, array))
                return FALSE;
            UINT32 attach = array + rd16(t, array + 2 + bi * 2);
            UINT32 comps = rd16(t, attach);
            if (!comps)
                return FALSE;
            UINT32 comp = comps - 1;
            anchor_off = rd16(t, attach + 2 + (comp * class_count + mark_class) * 2);
            if (!anchor_off)
                return FALSE;
            anchor_off += attach;
        } else {
            if ((UINT32)bi >= rd16(t, array))
                return FALSE;
            anchor_off = rd16(t, array + 2 + (bi * class_count + mark_class) * 2);
            if (!anchor_off)
                return FALSE;
            anchor_off += array;
        }
        INT32 bx, by;
        if (!anchor_xy(t, anchor_off, &bx, &by))
            return FALSE;
        g->dx = bx - mx;
        g->dy = by - my;
        g->attach = (INT32)base;
        return TRUE;
    }
    case 7:
        return apply_context(c, sub, pos, FALSE);
    case 8:
        return apply_context(c, sub, pos, TRUE);
    }
    return FALSE;
}

static BOOL apply_lookup(Ctx *c, UINT32 lookup_index, UINT32 pos)
{
    const Tbl *t = c->t;
    if (c->depth > 6)
        return FALSE;
    UINT32 lookup = lookup_offset(t, lookup_index);
    if (!lookup)
        return FALSE;
    UINT32 type = rd16(t, lookup);
    c->lookup_flag = rd16(t, lookup + 2);
    UINT32 count = rd16(t, lookup + 4);
    c->mark_set = (c->lookup_flag & 0x10) ? rd16(t, lookup + 6 + count * 2) : 0;
    if (skip_glyph(c, &c->b->g[pos]))
        return FALSE;
    c->depth++;
    BOOL applied = FALSE;
    for (UINT32 s = 0; s < count && !applied; s++) {
        UINT32 sub = lookup + rd16(t, lookup + 6 + s * 2);
        UINT32 st = type;
        UINT32 so = sub;
        if ((c->gpos && type == 9) || (!c->gpos && type == 7)) {
            if (rd16(t, sub) != 1)
                continue;
            st = rd16(t, sub + 2);
            so = sub + rd32(t, sub + 4);
        }
        c->delta = 0;
        applied = c->gpos ? gpos_subtable(c, st, so, pos) : gsub_subtable(c, st, so, pos);
    }
    c->depth--;
    return applied;
}

/* ------------------------------------------------------------------ feature selection */

typedef struct Wanted {
    UINT32 tag; /* first letter in the high byte, as the table stores it. */
    UINT32 bit;
} Wanted;

typedef struct LookupRef {
    UINT32 index;
    UINT64 mask;
} LookupRef;

static UINT32 find_script(const Tbl *t, UINT32 script_list, const UINT32 *tags, UINT32 ntags)
{
    UINT32 count = rd16(t, script_list);
    for (UINT32 k = 0; k < ntags; k++) {
        for (UINT32 i = 0; i < count; i++) {
            if (rd32(t, script_list + 2 + i * 6) == tags[k])
                return script_list + rd16(t, script_list + 2 + i * 6 + 4);
        }
    }
    return 0;
}

static void add_lookup(LookupRef *refs, UINT32 *count, UINT32 cap, UINT32 index, UINT64 mask)
{
    for (UINT32 i = 0; i < *count; i++) {
        if (refs[i].index == index) {
            refs[i].mask |= mask;
            return;
        }
    }
    if (*count < cap) {
        refs[*count].index = index;
        refs[*count].mask = mask;
        (*count)++;
    }
}

static void collect_lookups(const Tbl *t, const UINT32 *script_tags, UINT32 nscript, UINT32 lang_tag,
                            const Wanted *wanted, UINT32 nwanted, LookupRef *refs, UINT32 *count, UINT32 cap)
{
    *count = 0;
    if (!t->d || t->n < 10)
        return;
    UINT32 script_list = rd16(t, 4), feature_list = rd16(t, 6);
    UINT32 script = find_script(t, script_list, script_tags, nscript);
    if (!script)
        return;
    UINT32 lang = 0;
    if (lang_tag) {
        UINT32 lcount = rd16(t, script + 2);
        for (UINT32 i = 0; i < lcount; i++) {
            if (rd32(t, script + 4 + i * 6) == lang_tag) {
                lang = script + rd16(t, script + 4 + i * 6 + 4);
                break;
            }
        }
    }
    if (!lang) {
        UINT32 def = rd16(t, script);
        if (!def)
            return;
        lang = script + def;
    }
    UINT32 required = rd16(t, lang + 2);
    UINT32 fcount = rd16(t, lang + 4);
    UINT32 total = rd16(t, feature_list);
    for (UINT32 i = 0; i <= fcount; i++) {
        UINT32 findex = i == fcount ? required : rd16(t, lang + 6 + i * 2);
        if (findex == 0xFFFF || findex >= total)
            continue;
        UINT32 tag = rd32(t, feature_list + 2 + findex * 6);
        UINT32 feature = feature_list + rd16(t, feature_list + 2 + findex * 6 + 4);
        UINT64 mask = 0;
        if (i == fcount) {
            mask = 1; /* the required feature is always on. */
        } else {
            for (UINT32 w = 0; w < nwanted; w++)
                if (wanted[w].tag == tag)
                    mask |= 1ull << wanted[w].bit;
        }
        if (!mask)
            continue;
        UINT32 lcount = rd16(t, feature + 2);
        for (UINT32 l = 0; l < lcount; l++)
            add_lookup(refs, count, cap, rd16(t, feature + 4 + l * 2), mask);
    }
    /* by lookup index. */
    for (UINT32 a = 1; a < *count; a++) {
        LookupRef r = refs[a];
        UINT32 b = a;
        while (b > 0 && refs[b - 1].index > r.index) {
            refs[b] = refs[b - 1];
            b--;
        }
        refs[b] = r;
    }
}

/* the opentype script tags to try for a directwrite script, in order. */
static UINT32 ot_script_tags(UINT32 script, UINT32 *tags)
{
    UINT32 iso = DwScriptTag(script);
    char c[4] = {(char)(iso & 255), (char)((iso >> 8) & 255), (char)((iso >> 16) & 255), (char)(iso >> 24)};
    UINT32 n = 0;
    c[0] = (char)(c[0] | 0x20);
    UINT32 tag = ((UINT32)(UINT8)c[0] << 24) | ((UINT32)(UINT8)c[1] << 16) | ((UINT32)(UINT8)c[2] << 8) | (UINT8)c[3];
    if (tag == 0x6B617461 || tag == 0x68697261) /* katakana, hiragana. */
        tag = 0x6B616E61;
    if (tag == 0x7A7A7A7A || tag == 0x7A797979 || tag == 0x7A696E68) /* unknown, common, inherited. */
        tag = 0;
    if (tag)
        tags[n++] = tag;
    tags[n++] = 0x44464C54; /* DFLT. */
    tags[n++] = 0x64666C74; /* dflt. */
    tags[n++] = 0x6C61746E; /* latn. */
    return n;
}

static UINT32 ot_language_tag(const WCHAR *locale)
{
    if (!locale || !locale[0])
        return 0;
    static const struct {
        const char *code;
        const char *tag;
    } map[] = {
        {"en", "ENG "}, {"de", "DEU "}, {"fr", "FRA "}, {"es", "ESP "}, {"it", "ITA "}, {"pt", "PTG "},
        {"nl", "NLD "}, {"tr", "TRK "}, {"pl", "PLK "}, {"cs", "CSY "}, {"ro", "ROM "}, {"vi", "VIT "},
        {"az", "AZE "}, {"ru", "RUS "}, {"bg", "BGR "}, {"sr", "SRB "}, {"uk", "UKR "}, {"el", "ELL "},
        {"he", "IWR "}, {"ar", "ARA "}, {"fa", "FAR "}, {"ja", "JAN "}, {"ko", "KOR "}, {"th", "THA "},
        {"hi", "HIN "}, {"lt", "LTH "}, {"lv", "LVI "}, {"mt", "MTS "}, {"fi", "FIN "}, {"sv", "SVE "},
        {"da", "DAN "}, {"no", "NOR "}, {"nb", "NOR "}, {"is", "ISL "}, {"hu", "HUN "}, {"zh", "ZHS "},
    };
    char lang[3] = {0};
    for (int i = 0; i < 2; i++) {
        WCHAR w = locale[i];
        if (w >= 'A' && w <= 'Z')
            w = (WCHAR)(w + 32);
        if (w < 'a' || w > 'z')
            return 0;
        lang[i] = (char)w;
    }
    for (UINT32 i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (map[i].code[0] == lang[0] && map[i].code[1] == lang[1]) {
            const char *tag = map[i].tag;
            return ((UINT32)(UINT8)tag[0] << 24) | ((UINT32)(UINT8)tag[1] << 16) | ((UINT32)(UINT8)tag[2] << 8) |
                   (UINT8)tag[3];
        }
    }
    return 0;
}

#define OTAG(a, b, c, d) (((UINT32)(a) << 24) | ((UINT32)(b) << 16) | ((UINT32)(c) << 8) | (UINT32)(d))

/* ------------------------------------------------------------------ the shaper */

static BOOL is_default_ignorable(UINT32 cp)
{
    return cp == 0xAD || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
           (cp >= 0x2060 && cp <= 0x206F) || cp == 0xFEFF || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x34F ||
           (cp >= 0xE0000 && cp <= 0xE0FFF);
}

static BOOL is_zero_width(UINT32 cp)
{
    return (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x206F) ||
           cp == 0xFEFF || (cp >= 0xFE00 && cp <= 0xFE0F);
}

void DwShapedFree(DwShaped *s)
{
    DwFree(s->glyphs);
    DwFree(s->cluster_map);
    DwFree(s->text_flags);
    DwFree(s->glyph_flags);
    DwFree(s->advances);
    DwFree(s->offset_x);
    DwFree(s->offset_y);
    memset(s, 0, sizeof(*s));
}

HRESULT DwShape(const DwShapeInput *in, DwShaped *out)
{
    memset(out, 0, sizeof(*out));
    DwOtFont *font = in->font;
    UINT32 length = in->length;
    Buf b = {0};
    if (!buf_reserve(&b, length + 4))
        return E_OUTOFMEMORY;
    /* a glyph for each code point, with the cluster it starts. */
    UINT32 base_mask = 1;
    for (UINT32 i = 0; i < length;) {
        UINT32 units;
        UINT32 cp = DwCodePointAt(in->text, length, i, &units);
        ShGlyph *g = &b.g[b.n++];
        memset(g, 0, sizeof(*g));
        UINT32 mapped = cp;
        if (in->digit_zero && cp >= '0' && cp <= '9')
            mapped = *in->digit_zero + (cp - '0');
        if (in->rtl)
            mapped = DwMirror(mapped);
        g->cp = cp;
        g->cs = i;
        g->ce = i + units;
        g->attach = -1;
        g->mask = base_mask;
        UINT16 gid = 0;
        font->face->lpVtbl->GetGlyphIndices(font->face, &mapped, 1, &gid);
        if (!gid && mapped != cp) {
            UINT32 original = cp;
            font->face->lpVtbl->GetGlyphIndices(font->face, &original, 1, &gid);
        }
        if ((!gid && is_default_ignorable(cp)) || is_zero_width(cp)) {
            UINT32 space = ' ';
            font->face->lpVtbl->GetGlyphIndices(font->face, &space, 1, &gid);
        }
        g->gid = gid;
        g->zero_width = is_zero_width(cp);
        i += units;
    }
    /* each glyph begins as its own code points; the clusters are joined below from the grapheme runs. */
    {
        UINT32 gi = 0;
        for (UINT32 i = 0; i < length && gi < b.n;) {
            UINT32 cl = DwClusterLength(in->text, length, i);
            UINT32 end = i + cl;
            while (gi < b.n && b.g[gi].cs < end) {
                b.g[gi].cs = i;
                b.g[gi].ce = end;
                gi++;
            }
            i = end;
        }
    }
    /* a base with a mark after it becomes the one character that has both, if the font has it. */
    for (UINT32 i = 0; i + 1 < b.n;) {
        if (b.g[i].cs != b.g[i + 1].cs || DwCategory(b.g[i + 1].cp) != DW_GC_MN || b.g[i].zero_width) {
            i++;
            continue;
        }
        UINT32 composed = DwCompose(b.g[i].cp, b.g[i + 1].cp);
        UINT16 gid = 0;
        if (composed)
            font->face->lpVtbl->GetGlyphIndices(font->face, &composed, 1, &gid);
        if (!gid) {
            i++;
            continue;
        }
        b.g[i].cp = composed;
        b.g[i].gid = gid;
        buf_remove(&b, i + 1);
    }
    /* the glyph classes. */
    for (UINT32 i = 0; i < b.n; i++) {
        UINT32 off = rd16(&font->gdef, 4);
        UINT32 cls = font->gdef.d && off ? class_of(&font->gdef, off, b.g[i].gid) : 0;
        if (!cls && !off) {
            UINT8 gc = DwCategory(b.g[i].cp);
            if (gc == DW_GC_MN || gc == DW_GC_ME)
                cls = 3;
        }
        b.g[i].gclass = (UINT8)cls;
    }
    /* the features. */
    Wanted wanted[48];
    UINT32 nwanted = 0;
    static const UINT32 sub_defaults[] = {OTAG('r', 'v', 'r', 'n'), OTAG('c', 'c', 'm', 'p'), OTAG('l', 'o', 'c', 'l'),
                                          OTAG('r', 'l', 'i', 'g'), OTAG('c', 'a', 'l', 't'), OTAG('r', 'c', 'l', 't'),
                                          OTAG('l', 'i', 'g', 'a'), OTAG('c', 'l', 'i', 'g')};
    static const UINT32 pos_defaults[] = {OTAG('k', 'e', 'r', 'n'), OTAG('d', 'i', 's', 't'), OTAG('c', 'u', 'r', 's'),
                                          OTAG('m', 'a', 'r', 'k'), OTAG('m', 'k', 'm', 'k')};
    UINT64 all_default_bits = 0;
    for (UINT32 i = 0; i < sizeof(sub_defaults) / 4; i++) {
        /* the optional ones are left out when the caller names the features itself. */
        if (in->exclusive &&
            (sub_defaults[i] == OTAG('c', 'a', 'l', 't') || sub_defaults[i] == OTAG('r', 'c', 'l', 't') ||
             sub_defaults[i] == OTAG('l', 'i', 'g', 'a') || sub_defaults[i] == OTAG('c', 'l', 'i', 'g')))
            continue;
        wanted[nwanted].tag = sub_defaults[i];
        wanted[nwanted].bit = nwanted + 1;
        all_default_bits |= 1ull << wanted[nwanted].bit;
        nwanted++;
    }
    for (UINT32 i = 0; i < sizeof(pos_defaults) / 4; i++) {
        if (in->exclusive && pos_defaults[i] == OTAG('k', 'e', 'r', 'n'))
            continue;
        wanted[nwanted].tag = pos_defaults[i];
        wanted[nwanted].bit = nwanted + 1;
        all_default_bits |= 1ull << wanted[nwanted].bit;
        nwanted++;
    }
    if (in->rtl) {
        static const UINT32 rtl_tags[] = {OTAG('r', 't', 'l', 'a'), OTAG('r', 't', 'l', 'm')};
        for (UINT32 i = 0; i < 2; i++) {
            wanted[nwanted].tag = rtl_tags[i];
            wanted[nwanted].bit = nwanted + 1;
            all_default_bits |= 1ull << wanted[nwanted].bit;
            nwanted++;
        }
    }
    for (UINT32 i = 0; i < b.n; i++)
        b.g[i].mask |= all_default_bits;
    /* the features of the caller, per range. */
    for (UINT32 f = 0; f < in->feature_count; f++) {
        const DwFeatureRange *fr = &in->features[f];
        UINT32 tag = DwTagSwap(fr->tag);
        UINT32 w;
        for (w = 0; w < nwanted; w++)
            if (wanted[w].tag == tag)
                break;
        if (w == nwanted) {
            if (nwanted >= 47)
                continue;
            wanted[nwanted].tag = tag;
            wanted[nwanted].bit = nwanted + 1;
            nwanted++;
        }
        for (UINT32 i = 0; i < b.n; i++) {
            if (b.g[i].cs >= fr->end || b.g[i].ce <= fr->start)
                continue;
            if (fr->value)
                b.g[i].mask |= 1ull << wanted[w].bit;
            else
                b.g[i].mask &= ~(1ull << wanted[w].bit);
            if (fr->value > 1)
                b.g[i].alt = (UINT8)fr->value;
        }
    }
    UINT32 script_tags[4];
    UINT32 nscript = ot_script_tags(in->script, script_tags);
    UINT32 lang_tag = ot_language_tag(in->locale);
    LookupRef refs[256];
    UINT32 nrefs;
    Ctx c = {font, &font->gsub, FALSE, in->rtl, &b, 0, 0, 0, 0};
    if (font->gsub.d) {
        collect_lookups(&font->gsub, script_tags, nscript, lang_tag, wanted, nwanted, refs, &nrefs, 256);
        for (UINT32 r = 0; r < nrefs; r++) {
            UINT32 lookup = lookup_offset(&font->gsub, refs[r].index);
            if (!lookup)
                continue;
            BOOL reverse =
                rd16(&font->gsub, lookup) == 8 ||
                (rd16(&font->gsub, lookup) == 7 && rd16(&font->gsub, lookup + rd16(&font->gsub, lookup + 6) + 2) == 8);
            if (reverse) {
                for (UINT32 pos = b.n; pos > 0; pos--)
                    if (b.g[pos - 1].mask & refs[r].mask)
                        apply_lookup(&c, refs[r].index, pos - 1);
                continue;
            }
            for (UINT32 pos = 0; pos < b.n;) {
                if (b.g[pos].mask & refs[r].mask) {
                    c.delta = 0;
                    UINT32 before = b.n;
                    if (apply_lookup(&c, refs[r].index, pos)) {
                        /* a ligature or a deleted glyph leaves pos pointing at the next glyph already. */
                        INT32 change = (INT32)b.n - (INT32)before;
                        if (change > 0)
                            pos += (UINT32)change;
                    }
                }
                pos++;
            }
        }
    }
    /* the glyph classes again, now that glyphs changed. */
    for (UINT32 i = 0; i < b.n; i++) {
        UINT32 off = rd16(&font->gdef, 4);
        UINT32 cls = font->gdef.d && off ? class_of(&font->gdef, off, b.g[i].gid) : 0;
        if (!cls && !off) {
            UINT8 gc = DwCategory(b.g[i].cp);
            if (gc == DW_GC_MN || gc == DW_GC_ME)
                cls = 3;
        }
        b.g[i].gclass = (UINT8)cls;
    }
    /* advances. */
    for (UINT32 i = 0; i < b.n; i++) {
        b.g[i].xadv = DwOtAdvance(font, b.g[i].gid);
        if (b.g[i].zero_width || (b.g[i].gclass == 3 && !font->gdef.d && b.g[i].xadv < 0))
            b.g[i].xadv = 0;
    }
    BOOL kerned = FALSE;
    if (font->gpos.d && !in->sideways) {
        Ctx pc = {font, &font->gpos, TRUE, in->rtl, &b, 0, 0, 0, 0};
        collect_lookups(&font->gpos, script_tags, nscript, lang_tag, wanted, nwanted, refs, &nrefs, 256);
        for (UINT32 r = 0; r < nrefs; r++) {
            for (UINT32 pos = 0; pos < b.n; pos++)
                if (b.g[pos].mask & refs[r].mask)
                    apply_lookup(&pc, refs[r].index, pos);
        }
        kerned = nrefs > 0;
        for (UINT32 i = 0; i < b.n; i++) {
            if (b.g[i].attach >= 0 && (UINT32)b.g[i].attach < b.n) {
                const ShGlyph *base = &b.g[b.g[i].attach];
                b.g[i].xoff += b.g[i].dx + base->xoff;
                b.g[i].yoff += b.g[i].dy + base->yoff;
                if (!in->rtl)
                    for (UINT32 k = (UINT32)b.g[i].attach; k < i; k++)
                        b.g[i].xoff -= b.g[k].xadv;
                b.g[i].xadv = 0;
            }
        }
    }
    if (!kerned && !font->gpos.d) {
        /* the kern table, for fonts without positioning rules. */
        IDWriteFontFace1 *f1 = nullptr;
        if (SUCCEEDED(font->face->lpVtbl->QueryInterface(font->face, &IID_IDWriteFontFace1, (void **)&f1)) && f1) {
            BOOL kern_on = !in->exclusive;
            for (UINT32 f = 0; f < in->feature_count; f++)
                if (DwTagSwap(in->features[f].tag) == OTAG('k', 'e', 'r', 'n'))
                    kern_on = in->features[f].value != 0;
            if (kern_on && f1->lpVtbl->HasKerningPairs(f1) && b.n > 1) {
                UINT16 *ids = DwAlloc(b.n * sizeof(UINT16));
                INT32 *adj = DwAlloc(b.n * sizeof(INT32));
                if (ids && adj) {
                    for (UINT32 i = 0; i < b.n; i++)
                        ids[i] = b.g[i].gid;
                    if (SUCCEEDED(f1->lpVtbl->GetKerningPairAdjustments(f1, b.n, ids, adj)))
                        for (UINT32 i = 0; i + 1 < b.n; i++)
                            b.g[i].xadv += adj[i];
                }
                DwFree(ids);
                DwFree(adj);
            }
            f1->lpVtbl->Release(f1);
        }
    }
    /* the clusters: glyphs whose code units overlap form one. */
    out->glyph_count = b.n;
    out->glyphs = DwAlloc((b.n + 1) * sizeof(UINT16));
    out->cluster_map = DwAllocZero((length + 1) * sizeof(UINT16));
    out->text_flags = DwAllocZero(length + 1);
    out->glyph_flags = DwAllocZero((b.n + 1) * sizeof(UINT16));
    out->advances = DwAlloc((b.n + 1) * sizeof(INT32));
    out->offset_x = DwAlloc((b.n + 1) * sizeof(INT32));
    out->offset_y = DwAlloc((b.n + 1) * sizeof(INT32));
    if (!out->glyphs || !out->cluster_map || !out->text_flags || !out->glyph_flags || !out->advances ||
        !out->offset_x || !out->offset_y) {
        DwShapedFree(out);
        DwFree(b.g);
        return E_OUTOFMEMORY;
    }
    UINT32 group_start_glyph = 0, group_end = 0;
    for (UINT32 i = 0; i < b.n; i++) {
        const ShGlyph *g = &b.g[i];
        BOOL start = FALSE;
        if (i == 0 || g->cs >= group_end) {
            start = TRUE;
            group_start_glyph = i;
            group_end = g->ce;
        } else if (g->ce > group_end) {
            group_end = g->ce;
        }
        out->glyphs[i] = g->gid;
        out->advances[i] = g->xadv;
        out->offset_x[i] = g->xoff;
        out->offset_y[i] = g->yoff;
        UINT16 flags = 0;
        UINT32 justification = 2;
        if (DwIsWhitespace(g->cp))
            justification = 4;
        if (g->cp < 0x20 && g->cp != 9 && g->cp != 10 && g->cp != 13 && g->cp != 11 && g->cp != 12)
            justification = 0;
        if (g->zero_width)
            justification = 0;
        flags |= justification & 15;
        if (start)
            flags |= 1 << 4;
        if (g->gclass == 3 && !start)
            flags |= 1 << 5;
        if (g->zero_width)
            flags |= 1 << 6;
        out->glyph_flags[i] = flags;
        UINT32 s = g->cs, e = group_end;
        if (e > length)
            e = length;
        for (UINT32 u = s; u < e; u++)
            out->cluster_map[u] = (UINT16)group_start_glyph;
    }
    /* flags of the code units: shaping can break after a character unless it is merged into a glyph with the next ones.
     */
    for (UINT32 u = 0; u < length;) {
        UINT32 cluster_first = u;
        while (cluster_first > 0 && out->cluster_map[cluster_first - 1] == out->cluster_map[u])
            cluster_first--;
        UINT32 cluster_last = u;
        while (cluster_last + 1 < length && out->cluster_map[cluster_last + 1] == out->cluster_map[u])
            cluster_last++;
        UINT32 glyphs = 0;
        UINT32 g0 = out->cluster_map[u];
        UINT32 g1 = cluster_last + 1 < length ? out->cluster_map[cluster_last + 1] : b.n;
        glyphs = g1 > g0 ? g1 - g0 : 0;
        UINT32 points = 0;
        for (UINT32 q = cluster_first; q <= cluster_last;) {
            UINT32 units;
            DwCodePointAt(in->text, length, q, &units);
            q += units;
            points++;
        }
        BOOL merged = glyphs < points;
        UINT32 units;
        UINT32 cp = DwCodePointAt(in->text, length, u, &units);
        for (UINT32 k = 0; k < units; k++) {
            BOOL last_unit = k + 1 == units;
            BOOL last_char = u + units > cluster_last;
            out->text_flags[u + k] = (UINT8)(((last_unit && (last_char || !merged)) ? 4 : 0) |
                                             (units == 1 && DwCategory(cp) == DW_GC_ZS ? 1 : 0));
        }
        u += units;
    }
    DwFree(b.g);
    return S_OK;
}
