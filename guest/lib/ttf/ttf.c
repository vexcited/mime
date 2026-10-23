/* truetype font file parsing. */
#include "ttf.h"
#include "ttf_internal.h"

#define TAG(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/* composite nesting and total work per walk, against hostile fonts. */
#define MAX_COMPOSITE_DEPTH 8
#define MAX_GLYPH_VISITS 4096
#define MAX_WALK_POINTS (1u << 20)

static bool has(MimeTtfTable t, uint64_t off, uint64_t len)
{
    return off <= t.size && len <= t.size - off;
}

static uint8_t u8(MimeTtfTable t, uint64_t off)
{
    return has(t, off, 1) ? t.data[off] : 0;
}

static uint16_t u16(MimeTtfTable t, uint64_t off)
{
    if (!has(t, off, 2))
        return 0;
    return (uint16_t)((t.data[off] << 8) | t.data[off + 1]);
}

static int16_t s16(MimeTtfTable t, uint64_t off)
{
    return (int16_t)u16(t, off);
}

static uint32_t u32(MimeTtfTable t, uint64_t off)
{
    if (!has(t, off, 4))
        return 0;
    const uint8_t *p = t.data + off;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static MimeTtfTable sub(MimeTtfTable t, uint64_t off, uint64_t len)
{
    MimeTtfTable r = {0, 0};
    if (off > t.size)
        return r;
    if (len > t.size - off)
        len = t.size - off;
    r.data = t.data + off;
    r.size = (uint32_t)len;
    return r;
}

static MimeTtfTable whole(const void *data, size_t size)
{
    MimeTtfTable t;
    t.data = data;
    t.size = size > 0xffffffffu ? 0xffffffffu : (uint32_t)size;
    return t;
}

static bool sfnt_version_ok(uint32_t v)
{
    return v == 0x00010000 || v == TAG('t', 'r', 'u', 'e') || v == TAG('O', 'T', 'T', 'O');
}

unsigned mime_ttf_face_count(const void *data, size_t size)
{
    if (!data)
        return 0;
    MimeTtfTable file = whole(data, size);
    uint32_t tag = u32(file, 0);
    if (tag == TAG('t', 't', 'c', 'f')) {
        uint32_t n = u32(file, 8);
        uint32_t fit = file.size >= 12 ? (file.size - 12) / 4 : 0;
        if (n > fit)
            n = fit;
        return n > 0xffff ? 0xffff : n;
    }
    return sfnt_version_ok(tag) ? 1 : 0;
}

/* cmap subtable preference, higher wins, 0 rejects. */
static int cmap_score(uint16_t platform, uint16_t encoding, uint16_t format)
{
    if (format != 0 && format != 4 && format != 6 && format != 12 && format != 13)
        return 0;
    if (platform == 3 && encoding == 10)
        return 7;
    if (platform == 0 && (encoding == 4 || encoding == 6))
        return 6;
    if (platform == 3 && encoding == 1)
        return 5;
    if (platform == 0)
        return 4;
    if (platform == 3 && encoding == 0)
        return 3;
    if (platform == 1 && encoding == 0 && format == 0)
        return 1;
    return 0;
}

static void select_cmap(MimeTtfFont *f)
{
    MimeTtfTable c = f->cmap;
    uint16_t count = u16(c, 2);
    int best = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t rec = 4 + 8ull * i;
        if (!has(c, rec, 8))
            break;
        uint16_t platform = u16(c, rec);
        uint16_t encoding = u16(c, rec + 2);
        uint32_t off = u32(c, rec + 4);
        if (!has(c, off, 4))
            continue;
        uint16_t format = u16(c, off);
        int score = cmap_score(platform, encoding, format);
        if (score <= best)
            continue;
        uint64_t len;
        if (format == 12 || format == 13)
            len = u32(c, off + 4);
        else if (format == 4)
            len = c.size - off; /* the 16 bit length overflows in large fonts. */
        else
            len = u16(c, off + 2);
        best = score;
        f->cmap_sub = sub(c, off, len);
        f->cmap_format = format;
        f->cmap_symbol = platform == 3 && encoding == 0;
    }
}

static void select_kern(MimeTtfFont *f)
{
    MimeTtfTable k = f->kern;
    if (u16(k, 0) != 0)
        return;
    uint16_t count = u16(k, 2);
    uint64_t off = 4;
    for (uint32_t i = 0; i < count && has(k, off, 14); i++) {
        uint16_t length = u16(k, off + 2);
        uint16_t coverage = u16(k, off + 4);
        /* format 0, horizontal, not minimum, not cross-stream. */
        if ((coverage & 0xff07) == 0x0001) {
            uint32_t pairs = u16(k, off + 6);
            uint64_t fit = (k.size - (off + 14)) / 6;
            if (pairs > fit)
                pairs = (uint32_t)fit;
            f->kern_pairs = sub(k, off + 14, pairs * 6ull);
            f->kern_count = pairs;
            return;
        }
        if (length < 6)
            return;
        off += length;
    }
}

bool mime_ttf_open(MimeTtfFont *font, const void *data, size_t size, unsigned face_index)
{
    if (!font)
        return false;
    __builtin_memset(font, 0, sizeof(*font));
    if (!data)
        return false;
    MimeTtfTable file = whole(data, size);
    uint64_t base = 0;
    uint32_t version = u32(file, 0);
    if (version == TAG('t', 't', 'c', 'f')) {
        if (face_index >= mime_ttf_face_count(data, size))
            return false;
        base = u32(file, 12 + 4ull * face_index);
        version = u32(file, base);
    } else if (face_index != 0) {
        return false;
    }
    if (!sfnt_version_ok(version) || !has(file, base, 12))
        return false;

    uint16_t num_tables = u16(file, base + 4);
    if (!has(file, base + 12, 16ull * num_tables))
        return false;
    for (uint32_t i = 0; i < num_tables; i++) {
        uint64_t rec = base + 12 + 16ull * i;
        uint32_t tag = u32(file, rec);
        uint32_t off = u32(file, rec + 8);
        uint32_t len = u32(file, rec + 12);
        if (!has(file, off, len))
            continue;
        MimeTtfTable t = sub(file, off, len);
        switch (tag) {
        case TAG('h', 'e', 'a', 'd'): font->head = t; break;
        case TAG('h', 'h', 'e', 'a'): font->hhea = t; break;
        case TAG('h', 'm', 't', 'x'): font->hmtx = t; break;
        case TAG('m', 'a', 'x', 'p'): font->maxp = t; break;
        case TAG('l', 'o', 'c', 'a'): font->loca = t; break;
        case TAG('g', 'l', 'y', 'f'): font->glyf = t; break;
        case TAG('c', 'm', 'a', 'p'): font->cmap = t; break;
        case TAG('k', 'e', 'r', 'n'): font->kern = t; break;
        case TAG('O', 'S', '/', '2'): font->os2 = t; break;
        case TAG('n', 'a', 'm', 'e'): font->name = t; break;
        case TAG('p', 'o', 's', 't'): font->post = t; break;
        default: break;
        }
    }

    if (font->head.size < 54 || font->hhea.size < 36 || font->maxp.size < 6)
        goto fail;
    font->units_per_em = u16(font->head, 18);
    if (font->units_per_em < 16 || font->units_per_em > 16384)
        goto fail;
    font->num_glyphs = u16(font->maxp, 4);
    if (font->num_glyphs == 0)
        goto fail;
    font->num_hmetrics = u16(font->hhea, 34);
    if (font->num_hmetrics > font->num_glyphs)
        font->num_hmetrics = font->num_glyphs;
    if (font->num_hmetrics == 0 || font->hmtx.size < 4u * font->num_hmetrics)
        goto fail;

    font->loca_format = s16(font->head, 50);
    if (font->glyf.data && font->loca.data && (font->loca_format == 0 || font->loca_format == 1)) {
        uint64_t need = (font->num_glyphs + 1ull) * (font->loca_format ? 4 : 2);
        font->has_outlines = font->loca.size >= need;
    }

    select_cmap(font);
    select_kern(font);
    font->data = data;
    font->size = size;
    return true;

fail:
    __builtin_memset(font, 0, sizeof(*font));
    return false;
}

uint16_t mime_ttf_glyph_count(const MimeTtfFont *font)
{
    return font ? font->num_glyphs : 0;
}

bool mime_ttf_has_outlines(const MimeTtfFont *font)
{
    return font && font->has_outlines;
}

static uint32_t cmap_lookup(const MimeTtfFont *font, uint32_t cp)
{
    MimeTtfTable s = font->cmap_sub;
    switch (font->cmap_format) {
    case 0:
        return cp < 256 ? u8(s, 6 + cp) : 0;
    case 6: {
        uint32_t first = u16(s, 6), count = u16(s, 8);
        if (cp < first || cp - first >= count)
            return 0;
        return u16(s, 10 + 2ull * (cp - first));
    }
    case 4: {
        if (cp > 0xffff)
            return 0;
        uint32_t segs = u16(s, 6) / 2;
        uint32_t lo = 0, hi = segs;
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            if (u16(s, 14 + 2ull * mid) < cp)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo >= segs)
            return 0;
        uint64_t start_off = 16 + 2ull * segs + 2ull * lo;
        uint32_t start = u16(s, start_off);
        if (cp < start)
            return 0;
        uint16_t delta = u16(s, 16 + 4ull * segs + 2ull * lo);
        uint64_t ro_off = 16 + 6ull * segs + 2ull * lo;
        uint16_t ro = u16(s, ro_off);
        if (ro == 0)
            return (cp + delta) & 0xffff;
        uint16_t g = u16(s, ro_off + ro + 2ull * (cp - start));
        return g ? (uint32_t)(g + delta) & 0xffff : 0;
    }
    case 12:
    case 13: {
        uint32_t groups = u32(s, 12);
        uint32_t fit = s.size >= 16 ? (s.size - 16) / 12 : 0;
        if (groups > fit)
            groups = fit;
        uint32_t lo = 0, hi = groups;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            uint64_t g = 16 + 12ull * mid;
            uint32_t start = u32(s, g), end = u32(s, g + 4);
            if (cp < start)
                hi = mid;
            else if (cp > end)
                lo = mid + 1;
            else
                return font->cmap_format == 12 ? u32(s, g + 8) + (cp - start) : u32(s, g + 8);
        }
        return 0;
    }
    default:
        return 0;
    }
}

uint16_t mime_ttf_glyph_index(const MimeTtfFont *font, uint32_t codepoint)
{
    if (!font || !font->cmap_sub.data)
        return 0;
    uint32_t g = cmap_lookup(font, codepoint);
    /* symbol fonts map their characters into the private use area at f000. */
    if (g == 0 && font->cmap_symbol && codepoint <= 0xff)
        g = cmap_lookup(font, 0xf000 | codepoint);
    return g < font->num_glyphs ? (uint16_t)g : 0;
}

void mime_ttf_glyph_hmetrics(const MimeTtfFont *font, uint16_t glyph, int *advance, int *left_bearing)
{
    int adv = 0, lsb = 0;
    if (font && font->num_hmetrics && glyph < font->num_glyphs) {
        if (glyph < font->num_hmetrics) {
            adv = u16(font->hmtx, 4ull * glyph);
            lsb = s16(font->hmtx, 4ull * glyph + 2);
        } else {
            adv = u16(font->hmtx, 4ull * (font->num_hmetrics - 1));
            lsb = s16(font->hmtx, 4ull * font->num_hmetrics + 2ull * (glyph - font->num_hmetrics));
        }
    }
    if (advance)
        *advance = adv;
    if (left_bearing)
        *left_bearing = lsb;
}

int mime_ttf_kerning(const MimeTtfFont *font, uint16_t left, uint16_t right)
{
    if (!font || !font->kern_count)
        return 0;
    uint32_t key = ((uint32_t)left << 16) | right;
    uint32_t lo = 0, hi = font->kern_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t k = u32(font->kern_pairs, 6ull * mid);
        if (k < key)
            lo = mid + 1;
        else if (k > key)
            hi = mid;
        else
            return s16(font->kern_pairs, 6ull * mid + 4);
    }
    return 0;
}

static bool glyph_data(const MimeTtfFont *font, uint16_t glyph, MimeTtfTable *out)
{
    out->data = 0;
    out->size = 0;
    if (!font->has_outlines || glyph >= font->num_glyphs)
        return false;
    uint64_t start, end;
    if (font->loca_format == 0) {
        start = 2ull * u16(font->loca, 2ull * glyph);
        end = 2ull * u16(font->loca, 2ull * glyph + 2);
    } else {
        start = u32(font->loca, 4ull * glyph);
        end = u32(font->loca, 4ull * glyph + 4);
    }
    if (start > end || end > font->glyf.size)
        return false;
    if (start < end)
        *out = sub(font->glyf, start, end - start);
    return true;
}

/* yMax of a glyph header, 0 when unavailable. */
static int glyph_top(const MimeTtfFont *font, uint32_t codepoint)
{
    MimeTtfTable g;
    if (!glyph_data(font, mime_ttf_glyph_index(font, codepoint), &g) || g.size < 10)
        return 0;
    return s16(g, 8);
}

void mime_ttf_metrics(const MimeTtfFont *font, MimeTtfMetrics *m)
{
    __builtin_memset(m, 0, sizeof(*m));
    if (!font || !font->units_per_em)
        return;
    MimeTtfTable head = font->head, hhea = font->hhea, os2 = font->os2, post = font->post;

    m->units_per_em = font->units_per_em;
    m->x_min = s16(head, 36);
    m->y_min = s16(head, 38);
    m->x_max = s16(head, 40);
    m->y_max = s16(head, 42);
    uint16_t mac_style = u16(head, 44);
    m->ascender = s16(hhea, 4);
    m->descender = s16(hhea, 6);
    m->line_gap = s16(hhea, 8);
    m->max_advance = u16(hhea, 10);

    m->typo_ascender = m->ascender;
    m->typo_descender = m->descender;
    m->typo_line_gap = m->line_gap;
    m->win_ascent = m->ascender;
    m->win_descent = -m->descender;
    m->weight = (mac_style & 1) ? 700 : 400;
    m->bold = mac_style & 1;
    m->italic = (mac_style & 2) != 0;
    m->strikeout_size = font->units_per_em / 20;
    m->strikeout_position = font->units_per_em / 4;

    if (os2.size >= 78) {
        uint16_t version = u16(os2, 0);
        uint16_t selection = u16(os2, 62);
        m->avg_char_width = s16(os2, 2);
        m->weight = u16(os2, 4);
        m->strikeout_size = s16(os2, 26);
        m->strikeout_position = s16(os2, 28);
        m->italic = (selection & 0x01) || (mac_style & 2);
        m->bold = (selection & 0x20) || (mac_style & 1);
        m->use_typo_metrics = (selection & 0x80) != 0;
        m->typo_ascender = s16(os2, 68);
        m->typo_descender = s16(os2, 70);
        m->typo_line_gap = s16(os2, 72);
        m->win_ascent = u16(os2, 74);
        m->win_descent = u16(os2, 76);
        if (version >= 2 && os2.size >= 90) {
            m->x_height = s16(os2, 86);
            m->cap_height = s16(os2, 88);
        }
    }
    if (m->x_height <= 0)
        m->x_height = glyph_top(font, 'x');
    if (m->cap_height <= 0)
        m->cap_height = glyph_top(font, 'H');
    if (m->avg_char_width <= 0) {
        int adv = 0;
        mime_ttf_glyph_hmetrics(font, mime_ttf_glyph_index(font, 'x'), &adv, 0);
        m->avg_char_width = adv;
    }

    if (post.size >= 16) {
        m->italic_angle = (int32_t)u32(post, 4);
        m->underline_position = s16(post, 8);
        m->underline_thickness = s16(post, 10);
        m->fixed_pitch = u32(post, 12) != 0;
    } else {
        m->underline_position = -font->units_per_em / 10;
        m->underline_thickness = font->units_per_em / 20;
    }
}

static int name_score(uint16_t platform, uint16_t encoding, uint16_t language)
{
    bool english = language == 0x409;
    if (platform == 3 && encoding == 1)
        return english ? 10 : 8;
    if (platform == 3 && encoding == 10)
        return english ? 9 : 7;
    if (platform == 3 && encoding == 0)
        return english ? 6 : 5;
    if (platform == 0)
        return 4;
    if (platform == 1 && encoding == 0)
        return language == 0 ? 3 : 2;
    return 0;
}

bool mime_ttf_name(const MimeTtfFont *font, unsigned name_id, uint16_t *buffer, size_t capacity, size_t *length)
{
    if (length)
        *length = 0;
    if (!font)
        return false;
    MimeTtfTable n = font->name;
    uint16_t count = u16(n, 2);
    uint16_t storage = u16(n, 4);
    int best = 0;
    MimeTtfTable str = {0, 0};
    bool wide = false;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t rec = 6 + 12ull * i;
        if (!has(n, rec, 12))
            break;
        if (u16(n, rec + 6) != name_id)
            continue;
        uint16_t platform = u16(n, rec), encoding = u16(n, rec + 2);
        int score = name_score(platform, encoding, u16(n, rec + 4));
        uint64_t off = (uint64_t)storage + u16(n, rec + 10);
        uint16_t len = u16(n, rec + 8);
        if (score <= best || !has(n, off, len))
            continue;
        best = score;
        str = sub(n, off, len);
        wide = platform != 1;
    }
    if (!best)
        return false;

    size_t total = wide ? str.size / 2 : str.size;
    size_t copy = total < capacity ? total : capacity;
    for (size_t i = 0; i < copy && buffer; i++)
        buffer[i] = wide ? u16(str, 2 * i) : u8(str, i);
    if (buffer && total < capacity)
        buffer[total] = 0;
    if (length)
        *length = total;
    return true;
}

/* outline walking. */

typedef struct Xform {
    float a, b, c, d, e, f; /* x2 = a x + c y + e, y2 = b x + d y + f. */
} Xform;

typedef struct Budget {
    uint32_t visits, points;
} Budget;

typedef struct Walk {
    const MimeTtfFont *font;
    const MimeTtfSink *sink;
    Budget *budget;
} Walk;

typedef struct Capture {
    uint32_t index, seen;
    float x, y;
    bool found;
} Capture;

static void capture_point(void *ctx, float x, float y, bool on_curve)
{
    Capture *c = ctx;
    if (c->seen++ == c->index) {
        c->x = x;
        c->y = y;
        c->found = true;
    }
}

static void capture_end(void *ctx)
{
}

static bool walk_glyph(const Walk *w, uint16_t glyph, const Xform *m, unsigned depth, uint32_t component_limit);

static bool walk_simple(const Walk *w, MimeTtfTable g, uint32_t contours, const Xform *m)
{
    if (contours == 0)
        return true;
    uint64_t ends = 10;
    uint64_t ins_len_off = ends + 2ull * contours;
    if (!has(g, ends, 2ull * contours + 2))
        return false;
    int32_t prev = -1;
    for (uint32_t i = 0; i < contours; i++) {
        int32_t e = u16(g, ends + 2ull * i);
        if (e <= prev)
            return false;
        prev = e;
    }
    uint32_t points = (uint32_t)prev + 1;
    if (points > w->budget->points)
        return false;
    w->budget->points -= points;
    uint64_t flags = ins_len_off + 2 + u16(g, ins_len_off);

    /* first pass sizes the coordinate arrays. */
    uint64_t p = flags, x_bytes = 0, y_bytes = 0;
    for (uint32_t i = 0; i < points;) {
        if (!has(g, p, 1))
            return false;
        uint8_t fl = g.data[p++];
        uint32_t run = 1;
        if (fl & 8) {
            if (!has(g, p, 1))
                return false;
            run += g.data[p++];
        }
        if (run > points - i)
            run = points - i;
        x_bytes += (uint64_t)run * ((fl & 2) ? 1 : (fl & 16) ? 0 : 2);
        y_bytes += (uint64_t)run * ((fl & 4) ? 1 : (fl & 32) ? 0 : 2);
        i += run;
    }
    uint64_t px = p, py = p + x_bytes;
    if (!has(g, py, y_bytes))
        return false;

    p = flags;
    uint8_t fl = 0;
    uint32_t repeat = 0;
    int32_t x = 0, y = 0;
    uint32_t contour = 0;
    uint32_t next_end = u16(g, ends);
    for (uint32_t i = 0; i < points; i++) {
        if (repeat) {
            repeat--;
        } else {
            fl = g.data[p++];
            if (fl & 8)
                repeat = g.data[p++];
        }
        if (fl & 2) {
            int v = g.data[px++];
            x += (fl & 16) ? v : -v;
        } else if (!(fl & 16)) {
            x += (int16_t)((g.data[px] << 8) | g.data[px + 1]);
            px += 2;
        }
        if (fl & 4) {
            int v = g.data[py++];
            y += (fl & 32) ? v : -v;
        } else if (!(fl & 32)) {
            y += (int16_t)((g.data[py] << 8) | g.data[py + 1]);
            py += 2;
        }
        float fx = (float)x, fy = (float)y;
        w->sink->point(w->sink->ctx, m->a * fx + m->c * fy + m->e, m->b * fx + m->d * fy + m->f, fl & 1);
        if (i == next_end) {
            w->sink->end(w->sink->ctx);
            if (++contour < contours)
                next_end = u16(g, ends + 2ull * contour);
        }
    }
    return true;
}

static float f2dot14(MimeTtfTable g, uint64_t off)
{
    return (float)s16(g, off) / 16384.0f;
}

static bool find_point(const Walk *w, uint16_t glyph, const Xform *m, unsigned depth, uint32_t component_limit,
                       uint32_t index, float *x, float *y)
{
    Capture cap = {index, 0, 0, 0, false};
    MimeTtfSink sink = {capture_point, capture_end, &cap};
    Walk sub_walk = {w->font, &sink, w->budget};
    if (!walk_glyph(&sub_walk, glyph, m, depth, component_limit) || !cap.found)
        return false;
    *x = cap.x;
    *y = cap.y;
    return true;
}

static bool walk_composite(const Walk *w, uint16_t self, MimeTtfTable g, const Xform *m, unsigned depth,
                           uint32_t component_limit)
{
    if (depth >= MAX_COMPOSITE_DEPTH)
        return false;
    uint64_t off = 10;
    uint16_t flags;
    uint32_t index = 0;
    do {
        if (index >= component_limit)
            break;
        if (!has(g, off, 4))
            return false;
        flags = u16(g, off);
        uint16_t child = u16(g, off + 2);
        off += 4;
        int32_t arg1, arg2;
        if (flags & 0x0001) {
            if (!has(g, off, 4))
                return false;
            arg1 = (flags & 0x0002) ? s16(g, off) : u16(g, off);
            arg2 = (flags & 0x0002) ? s16(g, off + 2) : u16(g, off + 2);
            off += 4;
        } else {
            if (!has(g, off, 2))
                return false;
            arg1 = (flags & 0x0002) ? (int8_t)u8(g, off) : u8(g, off);
            arg2 = (flags & 0x0002) ? (int8_t)u8(g, off + 1) : u8(g, off + 1);
            off += 2;
        }
        Xform local = {1, 0, 0, 1, 0, 0};
        if (flags & 0x0008) {
            if (!has(g, off, 2))
                return false;
            local.a = local.d = f2dot14(g, off);
            off += 2;
        } else if (flags & 0x0040) {
            if (!has(g, off, 4))
                return false;
            local.a = f2dot14(g, off);
            local.d = f2dot14(g, off + 2);
            off += 4;
        } else if (flags & 0x0080) {
            if (!has(g, off, 8))
                return false;
            local.a = f2dot14(g, off);
            local.b = f2dot14(g, off + 2);
            local.c = f2dot14(g, off + 4);
            local.d = f2dot14(g, off + 6);
            off += 8;
        }

        if (flags & 0x0002) {
            float dx = (float)arg1, dy = (float)arg2;
            if ((flags & 0x0800) && !(flags & 0x1000)) {
                local.e = local.a * dx + local.c * dy;
                local.f = local.b * dx + local.d * dy;
            } else {
                local.e = dx;
                local.f = dy;
            }
        } else {
            /* align point arg2 of the component onto point arg1 of the glyph so far. */
            Xform identity = {1, 0, 0, 1, 0, 0};
            float px, py, cx, cy;
            if (!find_point(w, self, &identity, depth + 1, index, (uint32_t)arg1, &px, &py) ||
                !find_point(w, child, &local, depth + 1, 0xffffffffu, (uint32_t)arg2, &cx, &cy))
                return false;
            local.e = px - cx;
            local.f = py - cy;
        }

        Xform total;
        total.a = m->a * local.a + m->c * local.b;
        total.b = m->b * local.a + m->d * local.b;
        total.c = m->a * local.c + m->c * local.d;
        total.d = m->b * local.c + m->d * local.d;
        total.e = m->a * local.e + m->c * local.f + m->e;
        total.f = m->b * local.e + m->d * local.f + m->f;
        if (!walk_glyph(w, child, &total, depth + 1, 0xffffffffu))
            return false;
        index++;
    } while (flags & 0x0020);
    return true;
}

static bool walk_glyph(const Walk *w, uint16_t glyph, const Xform *m, unsigned depth, uint32_t component_limit)
{
    if (w->budget->visits == 0)
        return false;
    w->budget->visits--;
    MimeTtfTable g;
    if (!glyph_data(w->font, glyph, &g))
        return false;
    if (g.size == 0)
        return true;
    if (g.size < 10)
        return false;
    int contours = s16(g, 0);
    if (contours >= 0)
        return walk_simple(w, g, (uint32_t)contours, m);
    return walk_composite(w, glyph, g, m, depth, component_limit);
}

bool mime_ttf_walk_outline(const MimeTtfFont *font, uint16_t glyph, const MimeTtfSink *sink)
{
    if (!font || !font->has_outlines || !sink)
        return false;
    Budget budget = {MAX_GLYPH_VISITS, MAX_WALK_POINTS};
    Walk w = {font, sink, &budget};
    Xform identity = {1, 0, 0, 1, 0, 0};
    return walk_glyph(&w, glyph, &identity, 0, 0xffffffffu);
}
