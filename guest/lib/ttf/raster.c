/* outline flattening and exact area coverage rasterization. */
#include "ttf.h"
#include "ttf_internal.h"

/* largest flattening error, in pixels. */
#define FLATNESS 0.04f
#define MAX_SUBDIVISIONS 1024
/* keeps float to int conversions defined. */
#define COORD_LIMIT 4194304.0f
#define MAX_BITMAP_SIDE 16384

typedef struct Path Path;

struct Path {
    /* font units to pixels: X = xx * x + xy * y + tx, Y = yx * x + yy * y + ty. */
    float xx, xy, yx, yy, tx, ty;
    void (*line)(Path *p, float x0, float y0, float x1, float y1);

    /* contour state. */
    unsigned count;
    bool have_start, have_first_off, have_pending;
    float start_x, start_y, first_x, first_y, pend_x, pend_y, cur_x, cur_y;

    /* box mode. */
    bool any;
    float min_x, min_y, max_x, max_y;
    /* the box of every point, off the curve ones too. */
    bool cany;
    float cmin_x, cmin_y, cmax_x, cmax_y;

    /* raster mode. */
    float *acc;
    int width, height;

    /* scan mode. */
    void *segs;
    float flat;
};

static float clampf(float v)
{
    if (!(v > -COORD_LIMIT))
        return -COORD_LIMIT;
    if (v > COORD_LIMIT)
        return COORD_LIMIT;
    return v;
}

static int ifloor(float v)
{
    v = clampf(v);
    int i = (int)v;
    return (float)i > v ? i - 1 : i;
}

static int iceil(float v)
{
    v = clampf(v);
    int i = (int)v;
    return (float)i < v ? i + 1 : i;
}

static float absf(float v)
{
    return v < 0 ? -v : v;
}

static void emit_quad(Path *p, float x0, float y0, float cx, float cy, float x1, float y1)
{
    /* distance from the curve midpoint to the chord midpoint, shrinking with the square of the steps. */
    float dev = (absf(x0 - 2 * cx + x1) + absf(y0 - 2 * cy + y1)) * 0.25f;
    int n = 1;
    float flat = p->flat > 0 ? p->flat : FLATNESS;
    while (n < MAX_SUBDIVISIONS && dev > flat * (float)(n * n))
        n++;
    float px = x0, py = y0;
    float step = 1.0f / (float)n;
    for (int i = 1; i < n; i++) {
        float t = step * (float)i, u = 1 - t;
        float x = u * u * x0 + 2 * u * t * cx + t * t * x1;
        float y = u * u * y0 + 2 * u * t * cy + t * t * y1;
        p->line(p, px, py, x, y);
        px = x;
        py = y;
    }
    p->line(p, px, py, x1, y1);
}

static void path_point(void *ctx, float fx, float fy, bool on)
{
    Path *p = ctx;
    float x = clampf(p->xx * fx + p->xy * fy + p->tx);
    float y = clampf(p->yx * fx + p->yy * fy + p->ty);

    if (!p->cany) {
        p->cmin_x = p->cmax_x = x;
        p->cmin_y = p->cmax_y = y;
        p->cany = true;
    }
    if (x < p->cmin_x) p->cmin_x = x;
    if (x > p->cmax_x) p->cmax_x = x;
    if (y < p->cmin_y) p->cmin_y = y;
    if (y > p->cmax_y) p->cmax_y = y;

    if (p->count++ == 0) {
        if (on) {
            p->start_x = p->cur_x = x;
            p->start_y = p->cur_y = y;
            p->have_start = true;
        } else {
            p->first_x = x;
            p->first_y = y;
            p->have_first_off = true;
        }
        return;
    }
    if (!p->have_start) {
        /* the contour began off-curve, so its start is this point or the implied midpoint. */
        if (on) {
            p->start_x = p->cur_x = x;
            p->start_y = p->cur_y = y;
        } else {
            p->start_x = p->cur_x = (p->first_x + x) * 0.5f;
            p->start_y = p->cur_y = (p->first_y + y) * 0.5f;
            p->pend_x = x;
            p->pend_y = y;
            p->have_pending = true;
        }
        p->have_start = true;
        return;
    }
    if (on) {
        if (p->have_pending)
            emit_quad(p, p->cur_x, p->cur_y, p->pend_x, p->pend_y, x, y);
        else
            p->line(p, p->cur_x, p->cur_y, x, y);
        p->cur_x = x;
        p->cur_y = y;
        p->have_pending = false;
    } else {
        if (p->have_pending) {
            float mx = (p->pend_x + x) * 0.5f, my = (p->pend_y + y) * 0.5f;
            emit_quad(p, p->cur_x, p->cur_y, p->pend_x, p->pend_y, mx, my);
            p->cur_x = mx;
            p->cur_y = my;
        }
        p->pend_x = x;
        p->pend_y = y;
        p->have_pending = true;
    }
}

static void path_end(void *ctx)
{
    Path *p = ctx;
    if (p->have_start) {
        if (p->have_first_off) {
            if (p->have_pending) {
                float mx = (p->pend_x + p->first_x) * 0.5f, my = (p->pend_y + p->first_y) * 0.5f;
                emit_quad(p, p->cur_x, p->cur_y, p->pend_x, p->pend_y, mx, my);
                p->cur_x = mx;
                p->cur_y = my;
            }
            emit_quad(p, p->cur_x, p->cur_y, p->first_x, p->first_y, p->start_x, p->start_y);
        } else if (p->have_pending) {
            emit_quad(p, p->cur_x, p->cur_y, p->pend_x, p->pend_y, p->start_x, p->start_y);
        } else {
            p->line(p, p->cur_x, p->cur_y, p->start_x, p->start_y);
        }
    }
    p->count = 0;
    p->have_start = p->have_first_off = p->have_pending = false;
}

static void box_line(Path *p, float x0, float y0, float x1, float y1)
{
    if (!p->any) {
        p->min_x = p->max_x = x0;
        p->min_y = p->max_y = y0;
        p->any = true;
    }
    if (x0 < p->min_x) p->min_x = x0;
    if (x0 > p->max_x) p->max_x = x0;
    if (y0 < p->min_y) p->min_y = y0;
    if (y0 > p->max_y) p->max_y = y0;
    if (x1 < p->min_x) p->min_x = x1;
    if (x1 > p->max_x) p->max_x = x1;
    if (y1 < p->min_y) p->min_y = y1;
    if (y1 > p->max_y) p->max_y = y1;
}

/*
 * adds a line piece within one row, with signed height d. each cell gets the part
 * of d covering the pixel right of the line and the next cell the rest, so a
 * prefix sum along the row yields coverage.
 */
static void acc_span(float *row, int width, float xa, float xb, float d)
{
    float w = (float)width;
    if (xa < 0) xa = 0;
    if (xa > w) xa = w;
    if (xb < 0) xb = 0;
    if (xb > w) xb = w;
    if (xa > xb) {
        float t = xa;
        xa = xb;
        xb = t;
    }
    int i0 = (int)xa;
    if (xb <= (float)(i0 + 1)) {
        float area = d * (1 - ((xa + xb) * 0.5f - (float)i0));
        row[i0] += area;
        row[i0 + 1] += d - area;
        return;
    }
    int i1 = (int)xb;
    if ((float)i1 == xb)
        i1--;
    float per_x = d / (xb - xa);
    float left = d;

    float part = per_x * ((float)(i0 + 1) - xa);
    float area = part * ((float)(i0 + 1) - xa) * 0.5f;
    row[i0] += area;
    row[i0 + 1] += part - area;
    left -= part;
    for (int i = i0 + 1; i < i1; i++) {
        row[i] += per_x * 0.5f;
        row[i + 1] += per_x * 0.5f;
        left -= per_x;
    }
    float frac = xb - (float)i1;
    area = left * (1 - frac * 0.5f);
    row[i1] += area;
    row[i1 + 1] += left - area;
}

static void raster_line(Path *p, float x0, float y0, float x1, float y1)
{
    if (y0 == y1)
        return;
    float dir = 1;
    if (y0 > y1) {
        float t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
        dir = -1;
    }
    float h = (float)p->height;
    if (y1 <= 0 || y0 >= h)
        return;
    float dxdy = (x1 - x0) / (y1 - y0);
    float ys = y0 < 0 ? 0 : y0;
    float ye = y1 > h ? h : y1;
    int row = (int)ys;
    int last = iceil(ye) - 1;
    if (last >= p->height)
        last = p->height - 1;
    int stride = p->width + 2;
    for (; row <= last; row++) {
        float ya = (float)row > ys ? (float)row : ys;
        float yb = (float)(row + 1) < ye ? (float)(row + 1) : ye;
        if (yb <= ya)
            continue;
        float xa = x0 + (ya - y0) * dxdy;
        float xb = x0 + (yb - y0) * dxdy;
        acc_span(p->acc + (size_t)row * (size_t)stride, p->width, xa, xb, (yb - ya) * dir);
    }
}

static bool params_ok(const MimeTtfRenderParams *r)
{
    /* the negated comparisons also reject nan. */
    if (!(r->embolden >= 0) || !(r->embolden < 1e4f))
        return false;
    if (r->hinter && !r->use_matrix) {
        if (!(r->oblique > -16 && r->oblique < 16))
            return false;
    } else if (r->use_matrix) {
        if (!(absf(r->xx) < 1e4f) || !(absf(r->xy) < 1e4f) || !(absf(r->yx) < 1e4f) || !(absf(r->yy) < 1e4f))
            return false;
    } else {
        if (!(r->scale_x > 0) || !(r->scale_y > 0) || !(r->scale_x < 1e4f) || !(r->scale_y < 1e4f))
            return false;
        if (!(r->oblique > -16 && r->oblique < 16))
            return false;
    }
    if (!(absf(r->shift_x) < 1e6f) || !(absf(r->shift_y) < 1e6f))
        return false;
    return true;
}

static bool walk(const MimeTtfFont *font, const MimeTtfRenderParams *r, uint16_t glyph, const MimeTtfSink *sink)
{
    if (r->hinter)
        return mime_ttf_hint_walk(r->hinter, glyph, sink);
    return mime_ttf_walk_outline(font, glyph, sink);
}

static void path_init(Path *p, const MimeTtfRenderParams *r, float tx, float ty)
{
    __builtin_memset(p, 0, sizeof(*p));
    if (r->hinter && !r->use_matrix) {
        /* the hinted outline is already in pixels with y up. */
        p->xx = 1;
        p->xy = r->oblique;
        p->yx = 0;
        p->yy = -1;
    } else if (r->use_matrix) {
        p->xx = r->xx;
        p->xy = r->xy;
        p->yx = r->yx;
        p->yy = r->yy;
    } else {
        p->xx = r->scale_x;
        p->xy = r->scale_x * r->oblique;
        p->yx = 0;
        p->yy = -r->scale_y;
    }
    if (r->over_x > 1) {
        p->xx *= (float)r->over_x;
        p->xy *= (float)r->over_x;
    }
    if (r->over_y > 1) {
        p->yx *= (float)r->over_y;
        p->yy *= (float)r->over_y;
    }
    p->tx = tx;
    p->ty = ty;
}

bool mime_ttf_glyph_box_ex(const MimeTtfFont *font, uint16_t glyph, const MimeTtfRenderParams *params, int *x0,
                           int *y0, int *x1, int *y1)
{
    int b[4] = {0, 0, 0, 0};
    bool ok = false;
    if (font && params && params_ok(params)) {
        Path p;
        path_init(&p, params, params->shift_x, params->shift_y);
        p.line = box_line;
        MimeTtfSink sink = {path_point, path_end, &p};
        ok = walk(font, params, glyph, &sink);
        if (ok && p.any) {
            b[0] = ifloor(p.min_x);
            b[1] = ifloor(p.min_y);
            b[2] = iceil(p.max_x + params->embolden);
            b[3] = iceil(p.max_y);
            if (b[2] - b[0] > MAX_BITMAP_SIDE || b[3] - b[1] > MAX_BITMAP_SIDE) {
                ok = false;
                b[0] = b[1] = b[2] = b[3] = 0;
            }
        }
    }
    if (x0) *x0 = b[0];
    if (y0) *y0 = b[1];
    if (x1) *x1 = b[2];
    if (y1) *y1 = b[3];
    return ok;
}

bool mime_ttf_glyph_control_extent(const MimeTtfFont *font, uint16_t glyph, const MimeTtfRenderParams *params,
                                   float *x0, float *x1)
{
    float lo = 0, hi = 0;
    bool ok = false;
    if (font && params && params_ok(params)) {
        Path p;
        path_init(&p, params, params->shift_x, params->shift_y);
        p.line = box_line;
        MimeTtfSink sink = {path_point, path_end, &p};
        ok = walk(font, params, glyph, &sink);
        if (ok && p.cany) {
            lo = p.cmin_x;
            hi = p.cmax_x;
        }
    }
    if (x0) *x0 = lo;
    if (x1) *x1 = hi;
    return ok;
}

bool mime_ttf_render_glyph_ex(const MimeTtfFont *font, const MimeAllocator *alloc, uint16_t glyph,
                              const MimeTtfRenderParams *params, uint8_t *out, int width, int height, int stride)
{
    if (!alloc || !out || width < 0 || height < 0 || width > MAX_BITMAP_SIDE || height > MAX_BITMAP_SIDE)
        return false;
    if (height > 0 && stride < width)
        return false;
    int bx0, by0, bx1, by1;
    if (!mime_ttf_glyph_box_ex(font, glyph, params, &bx0, &by0, &bx1, &by1))
        return false;
    for (int y = 0; y < height; y++)
        __builtin_memset(out + (size_t)y * (size_t)stride, 0, (size_t)width);
    if (bx0 == bx1 || width == 0 || height == 0)
        return true;

    size_t row_len = (size_t)width + 2;
    size_t cells = row_len * (size_t)height;
    float *acc = mime_alloc(alloc, cells * sizeof(float));
    if (!acc)
        return false;
    for (size_t i = 0; i < cells; i++)
        acc[i] = 0;

    /* emboldening overlays copies shifted right at most half a pixel apart. */
    int copies = 1;
    if (params->embolden > 0)
        copies = 1 + iceil(params->embolden * 2);
    bool ok = true;
    for (int k = 0; k < copies && ok; k++) {
        float dx = copies > 1 ? params->embolden * (float)k / (float)(copies - 1) : 0;
        Path p;
        path_init(&p, params, params->shift_x - (float)bx0 + dx, params->shift_y - (float)by0);
        p.line = raster_line;
        p.acc = acc;
        p.width = width;
        p.height = height;
        MimeTtfSink sink = {path_point, path_end, &p};
        ok = walk(font, params, glyph, &sink);
    }

    if (ok) {
        for (int y = 0; y < height; y++) {
            const float *row = acc + (size_t)y * row_len;
            uint8_t *dst = out + (size_t)y * (size_t)stride;
            float sum = 0;
            for (int x = 0; x < width; x++) {
                sum += row[x];
                float v = absf(sum);
                dst[x] = v >= 1 ? 255 : (uint8_t)(v * 255.0f + 0.5f);
            }
        }
    }
    mime_free(alloc, acc);
    return ok;
}

bool mime_ttf_glyph_bitmap_box(const MimeTtfFont *font, uint16_t glyph, float scale_x, float scale_y,
                               float shift_x, float shift_y, int *x0, int *y0, int *x1, int *y1)
{
    MimeTtfRenderParams r = {scale_x, scale_y, shift_x, shift_y, 0, 0};
    return mime_ttf_glyph_box_ex(font, glyph, &r, x0, y0, x1, y1);
}

bool mime_ttf_render_glyph(const MimeTtfFont *font, const MimeAllocator *alloc, uint16_t glyph, float scale_x,
                           float scale_y, float shift_x, float shift_y, uint8_t *out, int width, int height,
                           int stride)
{
    MimeTtfRenderParams r = {scale_x, scale_y, shift_x, shift_y, 0, 0};
    return mime_ttf_render_glyph_ex(font, alloc, glyph, &r, out, width, height, stride);
}

/* ------------------------------------------------------------------ bilevel scan conversion */

static int dfloor(double v)
{
    if (!(v > -4194304.0))
        return -4194304;
    if (v > 4194304.0)
        return 4194304;
    int i = (int)v;
    return (double)i > v ? i - 1 : i;
}

static int dceil(double v)
{
    return -dfloor(-v);
}

typedef struct Seg {
    double x0, y0, x1, y1;
} Seg;

typedef struct SegList {
    const MimeAllocator *alloc;
    Seg *v;
    size_t count, cap;
    bool failed;
} SegList;

typedef struct Cross {
    double x;
    int dir;
} Cross;

static void seg_line(Path *p, float x0, float y0, float x1, float y1)
{
    SegList *l = p->segs;
    if (l->failed || (y0 == y1 && x0 == x1))
        return;
    if (l->count == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 128;
        Seg *v = mime_alloc(l->alloc, cap * sizeof(Seg));
        if (!v) {
            l->failed = true;
            return;
        }
        if (l->count)
            __builtin_memcpy(v, l->v, l->count * sizeof(Seg));
        if (l->v)
            mime_free(l->alloc, l->v);
        l->v = v;
        l->cap = cap;
    }
    l->v[l->count++] = (Seg){x0, y0, x1, y1};
}

static void cross_sort(Cross *c, int n)
{
    for (int i = 1; i < n; i++) {
        Cross t = c[i];
        int j = i;
        while (j > 0 && c[j - 1].x > t.x) {
            c[j] = c[j - 1];
            j--;
        }
        c[j] = t;
    }
}

/* the crossings of the line at coordinate c, along axis 0 for x or 1 for y, sorted along the other axis. */
static int line_crossings(const Seg *segs, size_t n, int axis, double c, Cross *out)
{
    int k = 0;
    for (size_t i = 0; i < n; i++) {
        double a0 = axis ? segs[i].x0 : segs[i].y0, a1 = axis ? segs[i].x1 : segs[i].y1;
        double b0 = axis ? segs[i].y0 : segs[i].x0, b1 = axis ? segs[i].y1 : segs[i].x1;
        if (a0 == a1)
            continue;
        double lo = a0 < a1 ? a0 : a1, hi = a0 < a1 ? a1 : a0;
        if (c < lo || c >= hi)
            continue;
        out[k].x = b0 + (c - a0) * (b1 - b0) / (a1 - a0);
        out[k].dir = a1 > a0 ? 1 : -1;
        k++;
    }
    cross_sort(out, k);
    return k;
}

/*
 * the segments each line of an axis may cross, in their order, so the crossings come in the same order as from all the
 * segments. a segment goes to the lines whose centre is near its extent, which line_crossings then tests exactly.
 */
typedef struct Buckets {
    uint32_t *start; /* lines + 1 offsets into list. */
    uint32_t *list;
    int lines;
} Buckets;

static void seg_lines(const Seg *s, int axis, int lines, int *first, int *last)
{
    double a0 = axis ? s->x0 : s->y0, a1 = axis ? s->x1 : s->y1;
    double lo = a0 < a1 ? a0 : a1, hi = a0 < a1 ? a1 : a0;
    if (a0 == a1) {
        *first = 0, *last = -1;
        return;
    }
    /* a nan crosses every line, as the exact test lets it. */
    if (lo != lo || hi != hi) {
        *first = 0, *last = lines - 1;
        return;
    }
    int f = dfloor(lo - 0.5), l = dceil(hi - 0.5);
    *first = f < 0 ? 0 : f >= lines ? lines : f;
    *last = l < 0 ? -1 : l >= lines ? lines - 1 : l;
}

static bool buckets_build(const MimeAllocator *alloc, const Seg *segs, size_t n, int axis, int lines, Buckets *b)
{
    b->lines = lines;
    b->list = nullptr;
    b->start = mime_alloc(alloc, ((size_t)lines + 1) * sizeof(uint32_t));
    if (!b->start)
        return false;
    __builtin_memset(b->start, 0, ((size_t)lines + 1) * sizeof(uint32_t));
    int (*range)[2] = mime_alloc(alloc, (n ? n : 1) * sizeof(*range));
    if (!range) {
        mime_free(alloc, b->start);
        b->start = nullptr;
        return false;
    }
    size_t total = 0;
    for (size_t i = 0; i < n; i++) {
        seg_lines(&segs[i], axis, lines, &range[i][0], &range[i][1]);
        for (int k = range[i][0]; k <= range[i][1]; k++)
            b->start[k + 1]++;
        total += range[i][1] >= range[i][0] ? (size_t)(range[i][1] - range[i][0] + 1) : 0;
    }
    if (total > 0xFFFFFFFFu || !(b->list = mime_alloc(alloc, (total ? total : 1) * sizeof(uint32_t)))) {
        mime_free(alloc, range);
        mime_free(alloc, b->start);
        b->start = nullptr;
        return false;
    }
    for (int k = 0; k < lines; k++)
        b->start[k + 1] += b->start[k];
    /* start[k] runs ahead while filling, then is shifted back. */
    for (size_t i = 0; i < n; i++)
        for (int k = range[i][0]; k <= range[i][1]; k++)
            b->list[b->start[k]++] = (uint32_t)i;
    mime_free(alloc, range);
    for (int k = lines; k > 0; k--)
        b->start[k] = b->start[k - 1];
    b->start[0] = 0;
    return true;
}

static void buckets_free(const MimeAllocator *alloc, Buckets *b)
{
    if (b->start)
        mime_free(alloc, b->start);
    if (b->list)
        mime_free(alloc, b->list);
    b->start = b->list = nullptr;
}

/* line_crossings at the centre of a line, through its bucket when there is one. */
static int crossings_at(const Seg *segs, size_t n, const Buckets *b, int axis, int line, Cross *out)
{
    double c = line + 0.5;
    if (!b || !b->start || line < 0 || line >= b->lines)
        return line_crossings(segs, n, axis, c, out);
    int k = 0;
    for (uint32_t j = b->start[line]; j < b->start[line + 1]; j++) {
        const Seg *s = &segs[b->list[j]];
        double a0 = axis ? s->x0 : s->y0, a1 = axis ? s->x1 : s->y1;
        double b0 = axis ? s->y0 : s->x0, b1 = axis ? s->y1 : s->x1;
        if (a0 == a1)
            continue;
        double lo = a0 < a1 ? a0 : a1, hi = a0 < a1 ? a1 : a0;
        if (c < lo || c >= hi)
            continue;
        out[k].x = b0 + (c - a0) * (b1 - b0) / (a1 - a0);
        out[k].dir = a1 > a0 ? 1 : -1;
        k++;
    }
    cross_sort(out, k);
    return k;
}

/* tolerance for a centre exactly on an edge. */
#define SCAN_EPS 1e-7
/* the curves are flattened finer than the display ones, a sample can be decided by a hundredth of a pixel. */
#define SCAN_FLATNESS 0.002f
/* how far around a dropout the next scan lines must show the contours going on. */
#define STUB_MARGIN 1.5

static bool scan_enabled(const MimeTtfScan *s)
{
    uint32_t c = s->control & 0xFFFF;
    uint32_t threshold = c & 0xFF;
    uint32_t ppem = (uint32_t)s->ppem & 0xFF;
    return ((c & 0x100) && ppem <= threshold) || (threshold == 0xFF && (c & 0x100)) || ((c & 0x200) && s->rotated) ||
           ((c & 0x400) && s->stretched);
}

/* the dropout rules of a scan type, bit 1 turns the control off, bit 2 is smart and bit 0 leaves stubs out. */
static unsigned scan_rules(const MimeTtfScan *s)
{
    if (!scan_enabled(s))
        return 2;
    return s->type & 0xFFFF;
}

/* true when the crossings at the line next to a dropout show the contours go on. */
static bool continues(const Seg *segs, size_t n, const Buckets *b, Cross *tmp, int axis, int line, int lines, double lo,
                      double hi)
{
    if (line < 0 || line >= lines)
        return false;
    int k = crossings_at(segs, n, b, axis, line, tmp);
    int hit = 0;
    for (int i = 0; i < k; i++)
        if (tmp[i].x >= lo - STUB_MARGIN && tmp[i].x <= hi + STUB_MARGIN)
            hit++;
    return hit >= 2;
}

/* the box of the runs the scan found, relative to the box of the glyph: columns x0 to x1 and rows y0 to y1. */
typedef struct RunBox {
    int x0, y0, x1, y1;
} RunBox;

static bool scan_core(const MimeTtfFont *font, const MimeAllocator *alloc, uint16_t glyph,
                      const MimeTtfRenderParams *params, const MimeTtfScan *scan, uint8_t *out, int width, int height,
                      int stride, RunBox *runs)
{
    if (!alloc || !out || !scan || width < 0 || height < 0 || width > MAX_BITMAP_SIDE || height > MAX_BITMAP_SIDE)
        return false;
    if (height > 0 && stride < width)
        return false;
    int bx0, by0, bx1, by1;
    if (!mime_ttf_glyph_box_ex(font, glyph, params, &bx0, &by0, &bx1, &by1))
        return false;
    for (int y = 0; y < height; y++)
        __builtin_memset(out + (size_t)y * (size_t)stride, 0, (size_t)width);
    runs->x0 = runs->y0 = runs->x1 = runs->y1 = 0;
    if (bx0 == bx1 || width == 0 || height == 0)
        return true;

    SegList list = {alloc, nullptr, 0, 0, false};
    Path p;
    path_init(&p, params, params->shift_x - (float)bx0, params->shift_y - (float)by0);
    p.line = seg_line;
    p.segs = &list;
    p.flat = SCAN_FLATNESS;
    MimeTtfSink sink = {path_point, path_end, &p};
    bool ok = walk(font, params, glyph, &sink) && !list.failed;
    Cross *cross = ok ? mime_alloc(alloc, (list.count + 1) * sizeof(Cross) * 2) : nullptr;
    if (!cross) {
        if (list.v)
            mime_free(alloc, list.v);
        return false;
    }
    Cross *tmp = cross + list.count + 1;
    const Seg *segs = list.v;
    size_t n = list.count;

    unsigned rules = scan_rules(scan);
    Buckets buckets[2] = {};
    buckets_build(alloc, segs, n, 0, height, &buckets[0]);
    if (!(rules & 2))
        buckets_build(alloc, segs, n, 1, width, &buckets[1]);

    /* rule 1 and 2: samples whose centre is inside or on the outline are on. */
    bool any_run = false;
    for (int y = 0; y < height; y++) {
        int k = crossings_at(segs, n, &buckets[0], 0, y, cross);
        int wind = 0;
        double start = 0;
        for (int i = 0; i < k; i++) {
            int before = wind;
            wind += cross[i].dir;
            if (before == 0 && wind != 0) {
                start = cross[i].x;
            } else if (before != 0 && wind == 0) {
                double a = start - 0.5 - SCAN_EPS, b = cross[i].x - 0.5 + SCAN_EPS;
                int first = dceil(a), last = dfloor(b);
                if (!any_run) {
                    runs->x0 = first;
                    runs->x1 = last + 1;
                    runs->y0 = y;
                    runs->y1 = y + 1;
                    any_run = true;
                } else {
                    if (first < runs->x0)
                        runs->x0 = first;
                    if (last + 1 > runs->x1)
                        runs->x1 = last + 1;
                    if (y < runs->y0)
                        runs->y0 = y;
                    if (y + 1 > runs->y1)
                        runs->y1 = y + 1;
                }
                if (first < 0)
                    first = 0;
                if (last >= width)
                    last = width - 1;
                for (int x = first; x <= last; x++)
                    out[(size_t)y * (size_t)stride + (size_t)x] = 1;
            }
        }
    }

    if (!(rules & 2)) {
        bool smart = (rules & 4) != 0, stubs = (rules & 1) != 0;
        for (int pass = 0; pass < 2; pass++) {
            int lines = pass ? width : height, along = pass ? height : width;
            const Buckets *b = &buckets[pass];
            for (int line = 0; line < lines; line++) {
                int k = crossings_at(segs, n, b, pass, line, cross);
                int wind = 0;
                double start = 0;
                double span[64][2];
                int spans = 0;
                for (int i = 0; i < k; i++) {
                    int before = wind;
                    wind += cross[i].dir;
                    if (before == 0 && wind != 0) {
                        start = cross[i].x;
                    } else if (before != 0 && wind == 0 && spans < 64) {
                        span[spans][0] = start;
                        span[spans][1] = cross[i].x;
                        spans++;
                    }
                }
                /* the runs are looked at from the end, so the first dropout wins the sample. */
                for (int s = 0; s < spans; s++) {
                    int si = spans - 1 - s;
                    double lo_x = span[si][0], hi_x = span[si][1];
                    int first = dceil(lo_x - 0.5 - SCAN_EPS), last = dfloor(hi_x - 0.5 + SCAN_EPS);
                    if (first <= last)
                        continue;
                    /* no centre inside, so the span lies between samples nr and nr + 1. */
                    int nr = first - 1;
                    if (pass == 0 ? (nr < 0 || nr + 1 >= along) : (nr < -1 || nr + 1 > along))
                        continue;
                    if (stubs && !(continues(segs, n, b, tmp, pass, line - 1, lines, nr, nr + 1.0 + SCAN_EPS) &&
                                   continues(segs, n, b, tmp, pass, line + 1, lines, nr, nr + 1.0 + SCAN_EPS)))
                        continue;
                    uint8_t *base = pass ? out + (size_t)line : out + (size_t)line * (size_t)stride;
                    size_t step = pass ? (size_t)stride : 1;
                    if ((nr >= 0 && base[(size_t)nr * step]) || (nr + 1 < along && base[(size_t)(nr + 1) * step]))
                        continue;
                    int pick = 0;
                    if (smart)
                        pick = dfloor((lo_x + hi_x) * 0.5) > nr ? 1 : 0;
                    /* the bitmap ends where the runs do, a sample beyond is left off down and pulled in across. */
                    int at = nr + pick;
                    if (pass == 0) {
                        if (at < runs->x0)
                            at = runs->x0;
                        if (at >= runs->x1)
                            at = runs->x1 - 1;
                    } else if (at < runs->y0 || at >= runs->y1) {
                        continue;
                    }
                    uint8_t *dst = pass ? out + (size_t)at * (size_t)stride + (size_t)line
                                        : out + (size_t)line * (size_t)stride + (size_t)at;
                    *dst = 1;
                }
            }
        }
    }
    buckets_free(alloc, &buckets[0]);
    buckets_free(alloc, &buckets[1]);
    mime_free(alloc, cross);
    mime_free(alloc, list.v);
    return true;
}

bool mime_ttf_scan_glyph_ex(const MimeTtfFont *font, const MimeAllocator *alloc, uint16_t glyph,
                            const MimeTtfRenderParams *params, const MimeTtfScan *scan, uint8_t *out, int width,
                            int height, int stride)
{
    RunBox runs;
    return scan_core(font, alloc, glyph, params, scan, out, width, height, stride, &runs);
}

void mime_ttf_bitmap_free(const MimeAllocator *alloc, MimeTtfBitmap *bitmap)
{
    if (bitmap->data)
        mime_free(alloc, bitmap->data);
    __builtin_memset(bitmap, 0, sizeof(*bitmap));
}

bool mime_ttf_scan_bitmap(const MimeTtfFont *font, const MimeAllocator *alloc, uint16_t glyph,
                          const MimeTtfRenderParams *params, const MimeTtfScan *scan, MimeTtfBitmap *out)
{
    __builtin_memset(out, 0, sizeof(*out));
    int x0, y0, x1, y1;
    if (!mime_ttf_glyph_box_ex(font, glyph, params, &x0, &y0, &x1, &y1))
        return false;
    if (x0 == x1 || y0 == y1)
        return true;
    int w = x1 - x0, h = y1 - y0;
    uint8_t *bits = mime_alloc(alloc, (size_t)w * (size_t)h);
    if (!bits)
        return false;
    RunBox runs;
    if (!scan_core(font, alloc, glyph, params, scan, bits, w, h, w, &runs)) {
        mime_free(alloc, bits);
        return false;
    }
    /* the bitmap spans the runs, even the ones too short to have a sample. */
    int tx0 = runs.x0, ty0 = runs.y0, tx1 = runs.x1, ty1 = runs.y1;
    bool any = runs.y1 > runs.y0;
    if (!any || tx0 >= tx1 || ty0 >= ty1) {
        mime_free(alloc, bits);
        return true;
    }
    if (tx0 < 0) tx0 = 0;
    if (tx1 > w) tx1 = w;
    int tw = tx1 - tx0, th = ty1 - ty0;
    uint8_t *tight = mime_alloc(alloc, (size_t)tw * (size_t)th);
    if (!tight) {
        mime_free(alloc, bits);
        return false;
    }
    for (int y = 0; y < th; y++)
        __builtin_memcpy(tight + (size_t)y * (size_t)tw, bits + (size_t)(y + ty0) * (size_t)w + (size_t)tx0, (size_t)tw);
    mime_free(alloc, bits);
    out->x0 = x0 + tx0;
    out->y0 = y0 + ty0;
    out->x1 = x0 + tx1;
    out->y1 = y0 + ty1;
    out->stride = tw;
    out->data = tight;
    return true;
}
