/*
 * the filters that resize a picture before it is sampled: the area filter of the high quality interpolation modes
 * and the box average that shrinks a big picture first. docs/gdiplus.md has the rules the numbers come from.
 *
 * everything is in 16.16 fractions of a pixel. The area filter takes the weight of a pixel as the difference of two
 * entries of a table, the running integral of the kernel, so the weights of a window always add up to one.
 */
#include "image_private.h"

/* ------------------------------------------------------------------ the kernels */

enum {
    CUBIC_ENTRIES = 1025,  /* the kernel reaches two pixel units each side, entries 1/128 of a unit apart */
    CUBIC_CENTER = 512,
    CUBIC_SHIFT = 8,
    LINEAR_ENTRIES = 513,  /* it reaches one pixel unit each side */
    LINEAR_CENTER = 256,
    LINEAR_SHIFT = 9,
};

static INT g_cubic_integral[CUBIC_ENTRIES];
static INT g_linear_integral[LINEAR_ENTRIES];
static volatile LONG g_tables_ready;

/* the cubic convolution kernel with the sharpest parameter, -1, which is zero past two pixels. */
static double cubic_kernel(double x)
{
    if (x < 0.0)
        x = -x;
    if (x <= 1.0)
        return x * x * x - 2.0 * x * x + 1.0;
    if (x < 2.0)
        return -x * x * x + 5.0 * x * x - 8.0 * x + 4.0;
    return 0.0;
}

static double triangle_kernel(double x)
{
    if (x < 0.0)
        x = -x;
    return x < 1.0 ? 1.0 - x : 0.0;
}

/* the running integral of a kernel from first on in steps of 1/128, scaled so that all of it is 65536. */
static void integrate(double (*kernel)(double), double first, INT entries, INT *table)
{
    double sum[CUBIC_ENTRIES];
    sum[0] = 0.0;
    for (INT i = 1; i < entries; i++) {
        double x0 = first + (double)(i - 1) / 128.0, x1 = first + (double)i / 128.0;
        sum[i] = sum[i - 1] + (kernel(x0) + kernel(x1)) * 0.5 / 128.0;
    }
    double total = sum[entries - 1];
    for (INT i = 0; i < entries; i++)
        table[i] = (INT)(sum[i] / total * 65536.0 + 0.5);
    table[entries - 1] = 65536;
}

static void tables_prepare(void)
{
    if (g_tables_ready)
        return;
    integrate(cubic_kernel, -4.0, CUBIC_ENTRIES, g_cubic_integral);
    integrate(triangle_kernel, -2.0, LINEAR_ENTRIES, g_linear_integral);
    g_tables_ready = 1;
}

/* ------------------------------------------------------------------ the area filter */

/* one axis of the result: where its first line is in the picture, how far apart the lines are and what each reaches. */
typedef struct Axis {
    INT start;     /* the position in the picture of the first line of the result */
    INT step;      /* the distance in the picture between two lines of the result */
    INT support;   /* how far the kernel reaches each side of a position */
    INT inv;       /* one over the support */
    INT first;     /* the device line of the first line of the result */
    INT count;     /* lines in the result */
} Axis;

typedef struct Stretch {
    const UINT32 *px;
    INT w, h;
    SSIZE_T stride;
    INT wrap;
    UINT32 clamp;
    BOOL clear;        /* the clamp color is clear, so what lies outside weighs nothing */
    Axis x, y;
    const INT *table;
    INT table_last, table_center, table_shift;
    /* the weights of each column of the result, the same for every row of the picture */
    INT *col_left;
    INT *col_len;
    INT *col_weights;
    INT col_window;
    /* the rows filtered across, kept for the next lines of the result */
    UINT32 *rows;
    INT *row_key;
    INT row_slots;
    INT line_slots;
    INT *line_weights;
    UINT32 *clamp_row;
} Stretch;

static BOOL inside(float v) { return v >= -32768.0f && v <= 32767.0f; }

/* a position stays a 32 bit number when it is moved by the support. */
static BOOL reach_fits(INT start, INT support)
{
    INT64 p = (INT64)start - 0x8000;
    return p >= -2147483648LL && p <= 2147483647LL && p - support >= -2147483648LL && p + support <= 2147483647LL;
}

/*
 * the lines of the result along one axis. The picture is read from s0 for a length slen, and it goes to the device
 * from d0 for a length dlen, either of which can be negative for a mirror. Line n of the result is the device line
 * first + n.
 */
static BOOL axis_make(Axis *a, float s0, float slen, float d0, float dlen, BOOL cubic)
{
    if (dlen == 0.0f)
        return FALSE;
    a->step = fix16(slen / dlen);
    if (a->step == 0)
        return FALSE;
    /* the edges in sixteenths of a pixel */
    INT e0 = floor_int(d0 * 16.0f + 0.5f), e1 = floor_int((slen * (dlen / slen) + d0) * 16.0f + 0.5f);
    if (e0 > e1) {
        INT t = e0;
        e0 = e1;
        e1 = t;
    }
    INT lo = e0 >> 4, hi = (e1 + 15) >> 4;
    a->first = lo;
    a->count = hi - lo + 1;
    float scale = (float)a->step * (1.0f / 65536.0f);
    a->start = fix16(((float)lo - d0) * scale + s0);
    float reach = ((cubic ? 2.0f : 1.0f) * slen) / dlen;
    a->support = fix16(reach);
    if (a->support < 0)
        a->support = -a->support;
    INT least = cubic ? 0x20000 : 0x10000;
    if (a->support < least)
        a->support = least;
    a->inv = fix16(65536.0f / (float)a->support);
    return reach_fits(a->start, a->support);
}

/* the sums of the four channels made into a pixel: rounded, the alpha held to 255 and the colors to the alpha. */
static UINT32 pack_sums(INT a, INT r, INT g, INT b)
{
    a = (a + 0x8000) >> 16;
    r = (r + 0x8000) >> 16;
    g = (g + 0x8000) >> 16;
    b = (b + 0x8000) >> 16;
    if (a > 255)
        a = 255;
    if (r > a)
        r = a;
    if (g > a)
        g = a;
    if (b > a)
        b = a;
    if (a < 0)
        a = 0;
    if (r < 0)
        r = 0;
    if (g < 0)
        g = 0;
    if (b < 0)
        b = 0;
    return (UINT32)a << 24 | (UINT32)r << 16 | (UINT32)g << 8 | (UINT32)b;
}

static INT wrap_line(INT i, INT n, BOOL mirror)
{
    INT r = i >= 0 ? i % n : n - ((~i) % n) - 1;
    if (mirror && (((i - r) / n) & 1))
        r = n - r - 1;
    return r;
}

static INT table_at(const Stretch *s, INT64 idx)
{
    INT64 i = (idx >> s->table_shift) + s->table_center;
    if (i < 0)
        i = 0;
    if (i > s->table_last)
        i = s->table_last;
    return s->table[i];
}

/* the weights of the lines from left to right around position c: each is the kernel's mass between the line and the one before. */
static INT line_weights(const Stretch *s, const Axis *a, INT64 c, INT left, INT count, INT *out)
{
    INT64 idx = ((INT64)a->inv * (((INT64)left << 16) - c)) >> 16;
    INT prev = 0;
    for (INT k = 0; k < count; k++) {
        INT t = table_at(s, idx);
        out[k] = t - prev;
        prev = t;
        idx += a->inv;
    }
    return count;
}

void stretch_free(Stretch *s)
{
    if (!s)
        return;
    gp_free(s->col_left);
    gp_free(s->col_len);
    gp_free(s->col_weights);
    gp_free(s->rows);
    gp_free(s->row_key);
    gp_free(s->line_weights);
    gp_free(s->clamp_row);
    gp_free(s);
}

Stretch *stretch_create(const SamplerSetup *u, BOOL cubic)
{
    tables_prepare();
    if ((UINT)u->wrap > 4 || u->w <= 0 || u->h <= 0)
        return nullptr;
    for (INT i = 0; i < 4; i++)
        if (!inside(u->src[i]) || !inside(u->dest[i]))
            return nullptr;
    Stretch *s = gp_alloc_zero(sizeof(*s));
    if (!s)
        return nullptr;
    s->px = u->px;
    s->w = u->w;
    s->h = u->h;
    s->stride = u->stride;
    s->wrap = u->wrap;
    s->clamp = u->clamp;
    s->clear = u->wrap == 4 && u->clamp == 0;
    s->table = cubic ? g_cubic_integral : g_linear_integral;
    s->table_last = (cubic ? CUBIC_ENTRIES : LINEAR_ENTRIES) - 1;
    s->table_center = cubic ? CUBIC_CENTER : LINEAR_CENTER;
    s->table_shift = cubic ? CUBIC_SHIFT : LINEAR_SHIFT;
    if (!axis_make(&s->x, u->src[0], u->src[2], u->dest[0], u->dest[2], cubic) ||
        !axis_make(&s->y, u->src[1], u->src[3], u->dest[1], u->dest[3], cubic))
        goto fail;
    s->col_window = (INT)(((INT64)s->x.support * 2) >> 16) + 3;
    s->line_slots = (INT)(((INT64)s->y.support + 0xffff) >> 16) * 2 + 1;
    s->row_slots = s->line_slots + 2;
    if (s->col_window > 4096 || s->line_slots > 4096 || s->x.count > 70000)
        goto fail;
    SIZE_T row_bytes = (SIZE_T)s->x.count * sizeof(UINT32);
    if (row_bytes * (SIZE_T)s->row_slots > (64u << 20) || (SIZE_T)s->x.count * (SIZE_T)s->col_window > (16u << 20))
        goto fail;

    s->col_left = gp_alloc(sizeof(INT) * (SIZE_T)s->x.count);
    s->col_len = gp_alloc(sizeof(INT) * (SIZE_T)s->x.count);
    s->col_weights = gp_alloc(sizeof(INT) * (SIZE_T)s->x.count * (SIZE_T)s->col_window);
    s->rows = gp_alloc(row_bytes * (SIZE_T)s->row_slots);
    s->row_key = gp_alloc(sizeof(INT) * (SIZE_T)s->row_slots);
    s->line_weights = gp_alloc(sizeof(INT) * (SIZE_T)(s->line_slots + 2));
    if (!s->col_left || !s->col_len || !s->col_weights || !s->rows || !s->row_key || !s->line_weights)
        goto fail;
    for (INT i = 0; i < s->row_slots; i++)
        s->row_key[i] = -1;
    if (u->wrap == 4 && !s->clear) {
        s->clamp_row = gp_alloc(row_bytes);
        if (!s->clamp_row)
            goto fail;
        for (INT i = 0; i < s->x.count; i++)
            s->clamp_row[i] = u->clamp;
    }
    for (INT i = 0; i < s->x.count; i++) {
        INT64 c = (INT64)s->x.start - 0x8000 + (INT64)i * s->x.step;
        INT left = (INT)((c - s->x.support + 0xffff) >> 16);
        INT right = (INT)((c + s->x.support + 0xffff) >> 16);
        INT n = right - left + 1;
        if (n < 0)
            n = 0;
        if (n > s->col_window)
            n = s->col_window;
        s->col_left[i] = left;
        s->col_len[i] = n;
        line_weights(s, &s->x, c, left, n, s->col_weights + (SIZE_T)i * s->col_window);
    }
    return s;
fail:
    stretch_free(s);
    return nullptr;
}

/* a row of the picture filtered across to the columns of the result. */
static void filter_across(const Stretch *s, const UINT32 *row, UINT32 *out)
{
    BOOL mirror = s->wrap == 1 || s->wrap == 3;
    for (INT i = 0; i < s->x.count; i++) {
        INT left = s->col_left[i], n = s->col_len[i];
        const INT *wt = s->col_weights + (SIZE_T)i * s->col_window;
        INT a = 0, r = 0, g = 0, b = 0;
        BOOL safe = left >= 0 && left + n <= s->w;
        for (INT j = 0; j < n; j++) {
            INT k = left + j;
            UINT32 p;
            if (safe || (k >= 0 && k < s->w))
                p = row[k];
            else if (s->wrap == 4)
                p = s->clamp;
            else
                p = row[wrap_line(k, s->w, mirror)];
            INT weight = wt[j];
            a += weight * (INT)(p >> 24);
            r += weight * (INT)((p >> 16) & 255);
            g += weight * (INT)((p >> 8) & 255);
            b += weight * (INT)(p & 255);
        }
        out[i] = pack_sums(a, r, g, b);
    }
}

/* the filtered row for a row of the picture, made when it is not kept already. */
static const UINT32 *filtered_row(Stretch *s, INT key)
{
    INT slot = key % s->row_slots;
    UINT32 *dst = s->rows + (SIZE_T)slot * s->x.count;
    if (s->row_key[slot] != key) {
        filter_across(s, s->px + (SSIZE_T)key * s->stride, dst);
        s->row_key[slot] = key;
    }
    return dst;
}

void stretch_run(Stretch *s, INT y, INT x0, INT x1, UINT32 *out)
{
    INT n = x1 - x0;
    if (n <= 0)
        return;
    memset(out, 0, sizeof(UINT32) * (SIZE_T)n);
    INT lo = x0 > s->x.first ? x0 : s->x.first;
    INT hi = x1 < s->x.first + s->x.count ? x1 : s->x.first + s->x.count;
    if (hi <= lo)
        return;
    INT line = y > s->y.first ? y : s->y.first;
    INT64 c = (INT64)s->y.start + (INT64)s->y.step * (line - s->y.first) - 0x8000;
    INT left = (INT)((c - s->y.support + 0xffff) >> 16);
    INT right = (INT)((c + s->y.support + 0xffff) >> 16);
    if (right < left)
        return;
    INT count = right - left + 1;
    if (count > s->line_slots)
        count = s->line_slots;
    line_weights(s, &s->y, c, left, count, s->line_weights);
    BOOL mirror = s->wrap == 2 || s->wrap == 3;

    INT acc[4 * 256];
    for (INT base = lo; base < hi; base += 256) {
        INT m = hi - base > 256 ? 256 : hi - base;
        memset(acc, 0, sizeof(INT) * 4 * (SIZE_T)m);
        INT col = base - s->x.first;
        for (INT j = 0; j < count; j++) {
            INT weight = s->line_weights[j];
            INT r = left + j;
            const UINT32 *row;
            if (weight == 0)
                continue;
            if (r >= 0 && r < s->h)
                row = filtered_row(s, r);
            else if (s->wrap != 4)
                row = filtered_row(s, wrap_line(r, s->h, mirror));
            else if (s->clamp_row)
                row = s->clamp_row;
            else
                continue;
            for (INT i = 0; i < m; i++) {
                UINT32 p = row[col + i];
                INT *sum = acc + 4 * i;
                sum[0] += weight * (INT)(p >> 24);
                sum[1] += weight * (INT)((p >> 16) & 255);
                sum[2] += weight * (INT)((p >> 8) & 255);
                sum[3] += weight * (INT)(p & 255);
            }
        }
        for (INT i = 0; i < m; i++)
            out[base - x0 + i] = pack_sums(acc[4 * i], acc[4 * i + 1], acc[4 * i + 2], acc[4 * i + 3]);
    }
}

/* ------------------------------------------------------------------ the box average */

/* a row of premultiplied pixels averaged across to nw pixels, each of the pixels under one counting by its part. */
static void average_across(const UINT32 *row, INT w, INT nw, UINT64 step, UINT64 inv, UINT32 *out)
{
    if (nw == w) {
        memcpy(out, row, sizeof(UINT32) * (SIZE_T)w);
        return;
    }
    UINT64 acc[4] = {0, 0, 0, 0};
    UINT64 weight = step;
    INT made = 0;
    memset(out, 0, sizeof(UINT32) * (SIZE_T)nw);
    for (INT i = 0; i < w && made < nw; i++) {
        UINT64 c[4] = {row[i] & 255u, (row[i] >> 8) & 255u, (row[i] >> 16) & 255u, row[i] >> 24};
        if (weight > 0x10000) {
            for (INT k = 0; k < 4; k++)
                acc[k] += c[k] << 16;
            weight -= 0x10000;
            continue;
        }
        UINT32 px = 0;
        for (INT k = 0; k < 4; k++) {
            UINT64 sum = weight * c[k] + acc[k];
            acc[k] = (c[k] << 16) - weight * c[k];
            px |= (UINT32)((((inv * sum) >> 16) + 0x8000) >> 16 & 255u) << (8 * k);
        }
        out[made++] = px;
        weight = weight + step - 0x10000;
    }
}

GpStatus area_scale(const GBitmap *b, struct GAttrs *attrs, INT nw, INT nh, INT top, INT bottom, UINT32 **out,
                    INT *out_w)
{
    INT w = b->width, h = b->height;
    if (nw > w)
        nw = w;
    if (nh > h)
        nh = h;
    if (nw < 1 || nh < 1 || top < 0 || bottom > nh || bottom <= top)
        return InvalidParameter;
    UINT64 step_x = (UINT64)((double)w * 65536.0 / (double)nw), inv_x = (UINT64)((double)nw * 65536.0 / (double)w);
    UINT64 step_y = (UINT64)((double)h * 65536.0 / (double)nh), inv_y = (UINT64)((double)nh * 65536.0 / (double)h);
    double ratio = (double)h / (double)nh;

    INT rows = bottom - top;
    UINT32 *result = gp_alloc_zero(sizeof(UINT32) * (SIZE_T)nw * (SIZE_T)rows);
    UINT32 *line = gp_alloc(sizeof(UINT32) * (SIZE_T)w);
    UINT32 *across = gp_alloc(sizeof(UINT32) * (SIZE_T)nw);
    UINT64 *acc = gp_alloc_zero(sizeof(UINT64) * 4 * (SIZE_T)nw);
    if (!result || !line || !across || !acc) {
        gp_free(result);
        gp_free(line);
        gp_free(across);
        gp_free(acc);
        return OutOfMemory;
    }
    BOOL adjust = attrs && gp_attrs_active(attrs, ColorAdjustTypeBitmap);

    INT first, last, row;
    INT64 room;    /* what is left of the line of the result in 16.16 lines of the picture */
    if (nh == h) {
        first = top;
        last = bottom;
        row = top;
        room = 0x10000;
    } else {
        first = floor_int((float)((double)top * ratio));
        float part = (float)((double)top * ratio - (double)first);
        if (part > 0.0f) {
            room = fix16(part);
            row = top - 1;
        } else {
            room = (INT64)step_y;
            row = top;
        }
        last = ceil_int((float)((double)bottom * ratio));
        if (last > h)
            last = h;
    }
    for (INT l = first; l < last && row < bottom; l++) {
        gp_bitmap_read_argb(b, 0, l, w, line);
        if (adjust)
            gp_attrs_apply(attrs, ColorAdjustTypeBitmap, line, w);
        for (INT i = 0; i < w; i++)
            line[i] = gp_premultiply(line[i]);
        average_across(line, w, nw, step_x, inv_x, across);
        if (nh == h) {
            memcpy(result + (SIZE_T)(l - top) * nw, across, sizeof(UINT32) * (SIZE_T)nw);
            row++;
            continue;
        }
        if (room > 0x10000) {
            for (INT i = 0; i < nw; i++)
                for (INT k = 0; k < 4; k++)
                    acc[4 * i + k] += (UINT64)((across[i] >> (8 * k)) & 255u) << 16;
            room -= 0x10000;
            continue;
        }
        for (INT i = 0; i < nw; i++) {
            UINT32 px = 0;
            for (INT k = 0; k < 4; k++) {
                UINT64 c = (across[i] >> (8 * k)) & 255u;
                UINT64 sum = (UINT64)room * c + acc[4 * i + k];
                acc[4 * i + k] = (c << 16) - (UINT64)room * c;
                px |= (UINT32)((((inv_y * sum) >> 16) + 0x8000) >> 16 & 255u) << (8 * k);
            }
            if (row >= top)
                result[(SIZE_T)(row - top) * nw + i] = px;
        }
        row++;
        room = room + (INT64)step_y - 0x10000;
    }
    gp_free(line);
    gp_free(across);
    gp_free(acc);
    *out = result;
    *out_w = nw;
    return Ok;
}
