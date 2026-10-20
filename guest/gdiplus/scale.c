/*
 * the filters of the high quality interpolation modes. A picture drawn with HighQualityBilinear or
 * HighQualityBicubic through a plain scale, with or without a translation, is not sampled at points: each pixel of
 * the result is a weighted sum of the picture pixels under a kernel that is as wide as the pixel is, so a shrunk
 * picture is filtered and an enlarged one is smooth.
 *
 * the weights come from the integral of the kernel. A pixel k of the picture weighs what the kernel holds between
 * the edge k - 1 and the edge k, measured from the position of the result pixel, and that is the difference of two
 * values of a table of the integral. The rows of the result are made from the picture rows first
 * horizontally, then the filtered rows are combined by the same weights vertically.
 */
#include "image_private.h"

/* ------------------------------------------------------------------ the tables of the integral */

/*
 * the integral of the bicubic kernel from -1 to x, as 16.16, for x = (i - 511.5) / 256 and i up to 1024. The kernel
 * is the cubic convolution with the parameter -1 stretched to two units, and the integral is the sum of its values
 * every 1/128 of a unit, which is how the table was made.
 */
#define CUBIC_ENTRIES 1025
#define LINEAR_ENTRIES 513

static INT cubic_cdf[CUBIC_ENTRIES];
static INT linear_cdf[LINEAR_ENTRIES];
static BOOL tables_ready;

static double sharp_kernel(double t)
{
    if (t < 0.0)
        t = -t;
    if (t < 1.0)
        return t * t * t - 2.0 * t * t + 1.0;
    if (t < 2.0)
        return -t * t * t + 5.0 * t * t - 8.0 * t + 4.0;
    return 0.0;
}

static void make_tables(void)
{
    double sum = 0.0;
    for (INT i = 0; i < CUBIC_ENTRIES; i++) {
        if (i > 256) {
            sum += sharp_kernel(-2.0 + (double)(i - 256) / 128.0) / 128.0;
            double scaled = sum * 65536.0 + 0.5;
            INT v = (INT)scaled;
            if ((double)v > scaled)
                v--;
            cubic_cdf[i] = v;
        } else {
            cubic_cdf[i] = 0;
        }
    }
    /* the integral of the triangle, every 1/128 from -1 to 1 */
    for (INT i = 0; i < LINEAR_ENTRIES; i++) {
        double x = (double)(i - 256) / 128.0, c;
        if (x <= -1.0)
            c = 0.0;
        else if (x < 0.0)
            c = (1.0 + x) * (1.0 + x) / 2.0;
        else if (x < 1.0)
            c = 1.0 - (1.0 - x) * (1.0 - x) / 2.0;
        else
            c = 1.0;
        double scaled = c * 65536.0 + 0.5;
        INT v = (INT)scaled;
        if ((double)v > scaled)
            v--;
        linear_cdf[i] = v;
    }
    tables_ready = TRUE;
}

/* ------------------------------------------------------------------ the filter */

typedef struct CacheRow {
    INT row;        /* the picture row the slot holds, -1 for none */
    UINT stamp;     /* the result row that used it last */
    UINT32 *data;
} CacheRow;

struct Stretch {
    BOOL cubic;
    const UINT32 *px;
    INT w, h;
    SSIZE_T stride;
    INT wrap;
    UINT32 clamp;
    BOOL clear;             /* the clamp color is clear, rows beyond the picture count for nothing */
    INT first, count;       /* the columns of the result the rows are made for */
    INT base_y;             /* the result row the vertical position starts at */
    INT x_start, x_step, x_support, x_inv;
    INT y_start, y_step, y_support, y_inv;
    INT slots;
    CacheRow *cache;
    UINT32 *rows;
    INT *weights;
    const UINT32 **ptrs;
    UINT stamp;
    const INT *table;
    INT shift, center;
};

static BOOL fixed16_ok(float v) { return v >= -32768.0f && v <= 32767.0f; }

/* the positions and the support must stay in 32 bits when the support is added to them */
static BOOL kernel_range_ok(INT position, INT support)
{
    long long p = (long long)position - 0x8000;
    if (p < -0x80000000LL || p > 0x7fffffffLL)
        return FALSE;
    long long lo = p - support, hi = p + support;
    return lo >= -0x80000000LL && lo <= 0x7fffffffLL && hi >= -0x80000000LL && hi <= 0x7fffffffLL;
}

static INT round_fixed(float v) { return floor_int(v + 0.5f); }

struct Stretch *stretch_create(const SpanSetup *setup, BOOL cubic)
{
    if (!tables_ready)
        make_tables();
    if (setup->wrap > 4 || setup->wrap < 0)
        return nullptr;
    struct Stretch *s = gp_alloc_zero(sizeof(*s));
    if (!s)
        return nullptr;
    s->cubic = cubic;
    s->px = setup->px;
    s->w = setup->w;
    s->h = setup->h;
    s->stride = setup->stride;
    s->wrap = setup->wrap;
    s->clamp = setup->clamp;
    s->clear = setup->wrap == 4 && setup->clamp == 0;
    s->table = cubic ? cubic_cdf : linear_cdf;
    s->shift = cubic ? 8 : 9;
    s->center = cubic ? 512 : 256;
    float sx = setup->src[0], sy = setup->src[1], sw = setup->src[2], sh = setup->src[3];
    float dx = setup->dest[0], dy = setup->dest[1], dw = setup->dest[2], dh = setup->dest[3];
    if (!fixed16_ok(sx) || !fixed16_ok(sy) || !fixed16_ok(sw) || !fixed16_ok(sh) || !fixed16_ok(dx) ||
        !fixed16_ok(dy) || !fixed16_ok(dw) || !fixed16_ok(dh))
        goto fail;
    {
        float to_dest_x = dw / sw, to_dest_y = dh / sh;
        s->x_step = round_fixed((sw / dw) * 65536.0f);
        s->y_step = round_fixed((sh / dh) * 65536.0f);
        if (s->x_step == 0 || s->y_step == 0)
            goto fail;
        /* the edges of the result in sixteenths of a pixel */
        INT xa = round_fixed(dx * 16.0f), xb = round_fixed((sw * to_dest_x + dx) * 16.0f);
        INT ya = round_fixed(dy * 16.0f), yb = round_fixed((sh * to_dest_y + dy) * 16.0f);
        if (xa > xb) {
            INT t = xa;
            xa = xb;
            xb = t;
        }
        if (ya > yb) {
            INT t = ya;
            ya = yb;
            yb = t;
        }
        s->first = (xa + 15) >> 4;
        INT xl = (INT)((UINT)xa << 12), xr = (INT)((UINT)xb << 12), yl = (INT)((UINT)ya << 12),
            yr = (INT)((UINT)yb << 12);
        /* where the first column starts in the picture */
        float edge = dx;
        INT base;
        if (s->x_step >= 0) {
            base = xl >> 16;
        } else {
            edge = dx + dw;
            base = (INT)((UINT)xr + 0xffffu) >> 16;
        }
        s->x_start = round_fixed((((edge - (float)base) * (float)s->x_step) * 0.000015258789f + sx) * 65536.0f);
        float unit = cubic ? 2.0f : 1.0f;
        INT min_support = cubic ? 0x20000 : 0x10000;
        s->x_support = round_fixed(((unit * sw) / dw) * 65536.0f);
        if (s->x_support < 0)
            s->x_support = -s->x_support;
        if (s->x_support < min_support)
            s->x_support = min_support;
        s->x_inv = round_fixed((65536.0f / (float)s->x_support) * 65536.0f);
        /* and the first row */
        s->base_y = (INT16)(yl >> 16);
        if (s->y_step >= 0) {
            s->y_start = round_fixed(((((float)s->base_y - dy) * (float)s->y_step) * 0.000015258789f + sy) * 65536.0f);
        } else {
            INT lowest = (INT)((UINT)yr + 0xffffu) >> 16;
            s->y_start = round_fixed((sy - (((float)lowest - (dh + dy)) * (float)s->y_step) * 0.000015258789f) * 65536.0f);
        }
        s->y_support = round_fixed(((unit * sh) / dh) * 65536.0f);
        if (s->y_support < 0)
            s->y_support = -s->y_support;
        if (s->y_support < min_support)
            s->y_support = min_support;
        s->y_inv = round_fixed((65536.0f / (float)s->y_support) * 65536.0f);
        s->count = ((INT)((UINT)xr + 0xffffu) >> 16) - (INT16)(xl >> 16) + 1;
        s->slots = 2 * (((INT)((UINT)s->y_support + 0xffffu)) >> 16) + 1;
    }
    if (!kernel_range_ok(s->x_start, s->x_support) || !kernel_range_ok(s->y_start, s->y_support))
        goto fail;
    if (s->count <= 0 || s->slots <= 0 || (SIZE_T)s->count > 0x1000000 / (SIZE_T)(s->slots + 1))
        goto fail;
    s->cache = gp_alloc_zero(sizeof(CacheRow) * (SIZE_T)s->slots);
    s->rows = gp_alloc((SIZE_T)s->slots * (SIZE_T)s->count * 4);
    s->weights = gp_alloc(sizeof(INT) * (SIZE_T)(s->slots + 2));
    s->ptrs = gp_alloc(sizeof(UINT32 *) * (SIZE_T)(s->slots + 2));
    if (!s->cache || !s->rows || !s->weights || !s->ptrs)
        goto fail;
    for (INT i = 0; i < s->slots; i++) {
        s->cache[i].row = -1;
        s->cache[i].data = s->rows + (SSIZE_T)i * s->count;
    }
    return s;
fail:
    stretch_free(s);
    return nullptr;
}

void stretch_free(struct Stretch *s)
{
    if (!s)
        return;
    gp_free(s->cache);
    gp_free(s->rows);
    gp_free(s->weights);
    gp_free(s->ptrs);
    gp_free(s);
}

/* the index of a pixel in a row or a column of n, wrapped as the wrap mode says for the axis. */
static INT wrap_axis(INT mode, BOOL vertical, INT i, INT n)
{
    BOOL flip = vertical ? (mode == 2 || mode == 3) : (mode == 1 || mode == 3);
    INT r = i < 0 ? n - (~i % n) - 1 : i % n;
    if (flip && (((i - r) / n) & 1))
        r = n - r - 1;
    return r;
}

static inline UINT32 pack_colors(INT a, INT r, INT g, INT b)
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

/* one row of the picture made as wide as the result, each pixel from the window of picture pixels around it. */
static void stretch_row(const struct Stretch *s, const UINT32 *row, UINT32 *out)
{
    INT c = (INT)((UINT)s->x_start - 0x8000u);
    for (INT i = 0; i < s->count; i++, c = (INT)((UINT)c + (UINT)s->x_step)) {
        INT right = (INT)((UINT)c + (UINT)s->x_support + 0xffffu) >> 16;
        INT left = (INT)((UINT)c - (UINT)s->x_support + 0xffffu) >> 16;
        INT idx = (INT)(((long long)s->x_inv * (long long)(INT)(((UINT)left << 16) - (UINT)c)) >> 16);
        INT prev = 0, a = 0, r = 0, g = 0, b = 0;
        for (INT k = left; k <= right; k++) {
            INT t = s->table[(idx >> s->shift) + s->center];
            INT wgt = t - prev;
            prev = t;
            idx = (INT)((UINT)idx + (UINT)s->x_inv);
            UINT32 p;
            if ((UINT)k < (UINT)s->w) {
                p = row[k];
            } else if (s->wrap == 4) {
                p = s->clamp;
            } else {
                p = row[wrap_axis(s->wrap, FALSE, k, s->w)];
            }
            a += wgt * (INT)(p >> 24);
            r += wgt * (INT)((p >> 16) & 255);
            g += wgt * (INT)((p >> 8) & 255);
            b += wgt * (INT)(p & 255);
        }
        out[i] = pack_colors(a, r, g, b);
    }
}

/* the filtered row of a picture row, from the cache when a result row before it needed it too. */
static const UINT32 *cached_row(struct Stretch *s, INT row)
{
    CacheRow *hit = nullptr, *free_slot = nullptr;
    for (INT i = 0; i < s->slots; i++) {
        CacheRow *c = &s->cache[i];
        if (c->row == row) {
            hit = c;
            break;
        }
        if (!free_slot && c->stamp != s->stamp)
            free_slot = c;
    }
    if (hit) {
        hit->stamp = s->stamp;
        return hit->data;
    }
    if (!free_slot)
        return nullptr;
    free_slot->row = row;
    free_slot->stamp = s->stamp;
    stretch_row(s, s->px + (SSIZE_T)row * s->stride, free_slot->data);
    return free_slot->data;
}

void stretch_run(struct Stretch *s, INT y, INT x0, INT x1, UINT32 *out)
{
    for (INT i = 0; i < x1 - x0; i++)
        out[i] = 0;
    INT lo = x0 > s->first ? x0 : s->first, hi = x1 < s->first + s->count ? x1 : s->first + s->count;
    if (hi <= lo)
        return;
    INT row_y = y > s->base_y ? y : s->base_y;
    INT c = (INT)((UINT)s->y_step * (UINT)(row_y - s->base_y) + (UINT)s->y_start - 0x8000u);
    INT right = (INT)((UINT)c + (UINT)s->y_support + 0xffffu) >> 16;
    INT left = (INT)((UINT)c - (UINT)s->y_support + 0xffffu) >> 16;
    if (right < left)
        return;
    s->stamp++;
    INT idx = (INT)(((long long)s->y_inv * (long long)(INT)(((UINT)left << 16) - (UINT)c)) >> 16);
    INT prev = 0;
    INT n = s->slots;
    for (INT j = 0; j < n; j++) {
        INT r = left + j;
        INT wgt = 0;
        if (r <= right) {
            INT t = s->table[(idx >> s->shift) + s->center];
            wgt = t - prev;
            prev = t;
            idx = (INT)((UINT)idx + (UINT)s->y_inv);
        }
        s->weights[j] = wgt;
        s->ptrs[j] = nullptr;
        if (r > right) {
            wgt = 0;
        } else if ((UINT)r < (UINT)s->h) {
            s->ptrs[j] = cached_row(s, r);
        } else if (s->clear) {
            s->weights[j] = 0;
        } else if (s->wrap != 4) {
            s->ptrs[j] = cached_row(s, wrap_axis(s->wrap, TRUE, r, s->h));
        }
        /* the clamp color rows have no pointer, their pixels are the color */
    }
    UINT cb = s->clamp;
    for (INT x = lo; x < hi; x++) {
        INT a = 0, r = 0, g = 0, b = 0;
        for (INT j = 0; j < n; j++) {
            INT wgt = s->weights[j];
            if (!wgt)
                continue;
            UINT32 p = s->ptrs[j] ? s->ptrs[j][x - s->first] : cb;
            a += wgt * (INT)(p >> 24);
            r += wgt * (INT)((p >> 16) & 255);
            g += wgt * (INT)((p >> 8) & 255);
            b += wgt * (INT)(p & 255);
        }
        out[x - x0] = pack_colors(a, r, g, b);
    }
}

/* ------------------------------------------------------------------ the area average of a big shrink */

/*
 * a picture drawn much smaller is first averaged down to about the size it will have. Each result pixel is the mean
 * of the picture pixels its extent covers, with the weights of a box in 16.16: whole pixels count 1 and the pixels
 * at the ends count by the part that lies inside. The sums are in premultiplied bytes, the mean is rounded.
 */
typedef struct Area {
    INT w, h;           /* the picture */
    INT nw, nh;         /* the result */
    INT step_x, inv_x, step_y, inv_y;
} Area;

/* one picture row made nw wide. */
static void area_line(const Area *a, const UINT32 *src, UINT32 *dst)
{
    if (a->nw == a->w) {
        memcpy(dst, src, (SIZE_T)a->w * 4);
        return;
    }
    UINT acc[4] = {0, 0, 0, 0};
    UINT wx = (UINT)a->step_x;
    INT j = 0;
    for (INT i = 0; i < a->w; i++) {
        UINT32 p = src[i];
        UINT c[4] = {p & 255, (p >> 8) & 255, (p >> 16) & 255, p >> 24};
        if (wx > 0x10000u) {
            for (INT k = 0; k < 4; k++)
                acc[k] += c[k] << 16;
            wx -= 0x10000u;
            continue;
        }
        UINT32 out = 0;
        for (INT k = 0; k < 4; k++) {
            UINT sum = wx * c[k] + acc[k];
            acc[k] = (c[k] << 16) - wx * c[k];
            UINT v = (UINT)((((unsigned long long)(UINT)a->inv_x * sum) >> 16) + 0x8000u) >> 16;
            out |= (v & 255) << (8 * k);
        }
        dst[j++] = out;
        if (j == a->nw)
            return;
        wx = wx + (UINT)a->step_x - 0x10000u;
    }
}

GpStatus area_scale(const GBitmap *b, struct GAttrs *attrs, INT nw, INT nh, INT top, INT bottom, UINT32 **out)
{
    *out = nullptr;
    Area a;
    a.w = b->width;
    a.h = b->height;
    a.nw = nw > a.w ? a.w : nw;
    a.nh = nh > a.h ? a.h : nh;
    if (a.nw < 1 || a.nh < 1 || top < 0 || bottom > a.nh || bottom <= top)
        return InvalidParameter;
    a.step_x = (INT)((double)a.w * 65536.0 / (double)a.nw);
    a.inv_x = (INT)((double)a.nw * 65536.0 / (double)a.w);
    a.step_y = (INT)((double)a.h * 65536.0 / (double)a.nh);
    a.inv_y = (INT)((double)a.nh * 65536.0 / (double)a.h);
    SIZE_T row_bytes = (SIZE_T)a.nw * 4;
    UINT32 *result = gp_alloc(row_bytes * (SIZE_T)(bottom - top));
    UINT32 *line = gp_alloc((SIZE_T)a.w * 4);
    UINT32 *tmp = gp_alloc(row_bytes);
    UINT *acc = gp_alloc_zero(row_bytes * 4);
    if (!result || !line || !tmp || !acc) {
        gp_free(result);
        gp_free(line);
        gp_free(tmp);
        gp_free(acc);
        return OutOfMemory;
    }
    BOOL adjust = attrs && gp_attrs_active(attrs, ColorAdjustTypeBitmap);
    double ratio = (double)a.h / (double)a.nh;
    float edge = (float)((double)top * ratio);
    INT first = floor_int(edge);
    INT row, last_line;
    UINT wy = (UINT)a.step_y;
    if (a.nh == a.h) {
        first = top;
        row = top;
        last_line = bottom;
    } else {
        float frac = (float)((double)top * ratio - (double)first);
        row = top;
        if (frac > 0.0f) {
            row = top - 1;
            wy = (UINT)floor_int(frac * 65536.0f + 0.5f);
        }
        last_line = ceil_int((float)((double)bottom * ratio));
    }
    if (last_line > a.h)
        last_line = a.h;
    for (INT l = first; l < last_line && row < bottom; l++) {
        gp_bitmap_read_argb(b, 0, l, a.w, line);
        if (adjust)
            gp_attrs_apply(attrs, ColorAdjustTypeBitmap, line, a.w);
        for (INT x = 0; x < a.w; x++)
            line[x] = gp_premultiply(line[x]);
        area_line(&a, line, tmp);
        UINT32 *dst = row >= top ? result + (SSIZE_T)(row - top) * a.nw : nullptr;
        if (a.nh == a.h) {
            memcpy(dst, tmp, row_bytes);
            row++;
            continue;
        }
        if (wy > 0x10000u) {
            for (INT x = 0; x < a.nw; x++)
                for (INT k = 0; k < 4; k++)
                    acc[x * 4 + k] += ((tmp[x] >> (8 * k)) & 255) << 16;
            wy -= 0x10000u;
            continue;
        }
        for (INT x = 0; x < a.nw; x++) {
            UINT32 o = 0;
            for (INT k = 0; k < 4; k++) {
                UINT p = (tmp[x] >> (8 * k)) & 255;
                UINT sum = wy * p + acc[x * 4 + k];
                acc[x * 4 + k] = (p << 16) - wy * p;
                UINT v = (UINT)((((unsigned long long)(UINT)a.inv_y * sum) >> 16) + 0x8000u) >> 16;
                o |= (v & 255) << (8 * k);
            }
            if (dst)
                dst[x] = o;
        }
        row++;
        wy = wy + (UINT)a.step_y - 0x10000u;
    }
    gp_free(line);
    gp_free(tmp);
    gp_free(acc);
    *out = result;
    return Ok;
}
