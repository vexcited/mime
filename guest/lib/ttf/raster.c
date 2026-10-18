/*
 * outline flattening and exact area coverage rasterization.
 */
#include "ttf.h"
#include "ttf_internal.h"

/* largest flattening error tolerated, in pixels. */
#define FLATNESS 0.04f
#define MAX_SUBDIVISIONS 256
/* coordinate clamp keeping float to int conversions defined. */
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

    /* raster mode. */
    float *acc;
    int width, height;
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
    while (n < MAX_SUBDIVISIONS && dev > FLATNESS * (float)(n * n))
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
 * adds a line piece contained in one row, with signed height d. each cell
 * receives the part of d covering the pixel to the right of the line, and
 * the next cell the rest, so a prefix sum along the row yields coverage.
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
    if (r->use_matrix) {
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

static void path_init(Path *p, const MimeTtfRenderParams *r, float tx, float ty)
{
    __builtin_memset(p, 0, sizeof(*p));
    if (r->use_matrix) {
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
        ok = mime_ttf_walk_outline(font, glyph, &sink);
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
        ok = mime_ttf_walk_outline(font, glyph, &sink);
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
