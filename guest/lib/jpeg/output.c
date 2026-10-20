/*
 * from the planes of the components to pixels: the components with fewer samples than the picture are
 * interpolated linearly between the centers of their samples, a sample of the plane covering the area of
 * hmax / h by vmax / v pixels, and the colors are converted with the transforms of JFIF.
 *
 * the picture is made row by row. a component at the size of the picture is used from the plane as it is, the
 * others are brought to the width of the picture in a line buffer first, with a path of their own for the usual
 * halving of the width and height.
 */
#include "dec.h"

typedef struct Line {
    const JpegComp *c;
    int mode;
    const uint8_t *row; /* the line of this output row. */
    uint8_t *buf;
    /* the generic path. */
    int32_t *x0, *x1;
    uint8_t *xf;
} Line;

enum { LINE_DIRECT, LINE_H2V1, LINE_H2V2, LINE_H1V2, LINE_GENERIC };

/* the two samples around a position and the weight of the second in 1/256, for output sample x of a ratio. */
static void plan_axis(uint32_t x, uint32_t num, uint32_t den, uint32_t limit, bool fancy, int32_t *i0, int32_t *i1,
                      uint8_t *frac)
{
    if (!fancy) {
        uint32_t i = (uint32_t)(((uint64_t)x * num) / den);
        if (i >= limit)
            i = limit - 1;
        *i0 = *i1 = (int32_t)i;
        *frac = 0;
        return;
    }
    /* the center of output pixel x is at x + 1/2, in samples that is (x + 1/2) * num / den and the sample i has
     * its center at i + 1/2. */
    int64_t pos = (int64_t)(((uint64_t)(2 * x + 1) * num) << 15) / (int64_t)den - 0x8000;
    int64_t i = pos >> 16;
    int a = (int)(i < 0 ? 0 : i >= (int64_t)limit ? (int64_t)limit - 1 : i);
    int b = (int)(i + 1 < 0 ? 0 : i + 1 >= (int64_t)limit ? (int64_t)limit - 1 : i + 1);
    *i0 = a;
    *i1 = b;
    *frac = (uint8_t)((pos >> 8) & 255);
}

/* each sample of the row gives two pixels: three quarters of itself and a quarter of the neighbor on that side. */
static void line_h2v1(const uint8_t *in, uint32_t cw, uint8_t *out, uint32_t w)
{
    for (uint32_t i = 0; i < cw && 2 * i < w; i++) {
        int c = in[i], p = in[i ? i - 1 : 0], n = in[i + 1 < cw ? i + 1 : cw - 1];
        out[2 * i] = (uint8_t)((3 * c + p + 1) >> 2);
        if (2 * i + 1 < w)
            out[2 * i + 1] = (uint8_t)((3 * c + n + 2) >> 2);
    }
}

/* the vertical sums 3 * near + far of two rows, then the same triangle over them: sixteenths in all. */
static void line_h2v2(const uint16_t *sum, uint32_t cw, uint8_t *out, uint32_t w)
{
    for (uint32_t i = 0; i < cw && 2 * i < w; i++) {
        int c = sum[i], p = sum[i ? i - 1 : 0], n = sum[i + 1 < cw ? i + 1 : cw - 1];
        out[2 * i] = (uint8_t)((3 * c + p + 8) >> 4);
        if (2 * i + 1 < w)
            out[2 * i + 1] = (uint8_t)((3 * c + n + 7) >> 4);
    }
}

static const uint8_t *plane_row(const JpegComp *c, uint32_t r)
{
    if (r >= c->ch)
        r = c->ch - 1;
    return c->plane + (size_t)r * c->pw;
}

static void make_line(Jpeg *j, Line *ln, uint32_t y, uint16_t *sum, bool fancy)
{
    const JpegComp *c = ln->c;
    uint32_t w = j->out_width;
    switch (ln->mode) {
    case LINE_DIRECT:
        ln->row = plane_row(c, y);
        return;
    case LINE_H2V1:
        line_h2v1(plane_row(c, y), c->cw, ln->buf, w);
        break;
    case LINE_H1V2:
    case LINE_H2V2: {
        uint32_t near = y >> 1;
        uint32_t far = (y & 1) ? near + 1 : (near ? near - 1 : 0);
        const uint8_t *a = plane_row(c, near), *b = plane_row(c, far);
        if (ln->mode == LINE_H1V2) {
            for (uint32_t x = 0; x < w; x++)
                ln->buf[x] = (uint8_t)((3 * a[x] + b[x] + 2) >> 2);
        } else {
            for (uint32_t i = 0; i < c->cw; i++)
                sum[i] = (uint16_t)(3 * a[i] + b[i]);
            line_h2v2(sum, c->cw, ln->buf, w);
        }
        break;
    }
    default: {
        int32_t y0, y1;
        uint8_t fy;
        plan_axis(y, (uint32_t)c->v * (uint32_t)c->n, (uint32_t)j->vmax * (uint32_t)j->scale, c->ch, fancy, &y0, &y1, &fy);
        const uint8_t *a = plane_row(c, (uint32_t)y0), *b = plane_row(c, (uint32_t)y1);
        for (uint32_t x = 0; x < w; x++) {
            int32_t i0 = ln->x0[x], i1 = ln->x1[x];
            int fx = ln->xf[x];
            int top = a[i0] * (256 - fx) + a[i1] * fx;
            int bottom = b[i0] * (256 - fx) + b[i1] * fx;
            ln->buf[x] = (uint8_t)((top * (256 - fy) + bottom * fy + 0x8000) >> 16);
        }
        break;
    }
    }
    ln->row = ln->buf;
}

/* ------------------------------------------------------------------ color */

static inline uint8_t clamp_u8(int v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

#define K_CR_R 91881  /* 1.402 */
#define K_CB_B 116130 /* 1.772 */
#define K_CR_G 46802  /* 0.714136 */
#define K_CB_G 22554  /* 0.344136 */

static void ycc_to_bgr(const uint8_t *y, const uint8_t *cb, const uint8_t *cr, uint8_t *out, uint32_t w)
{
    for (uint32_t x = 0; x < w; x++) {
        int yy = y[x], u = cb[x] - 128, v = cr[x] - 128;
        out[3 * x] = clamp_u8(yy + ((K_CB_B * u + 32768) >> 16));
        out[3 * x + 1] = clamp_u8(yy - ((K_CB_G * u + K_CR_G * v + 32768) >> 16));
        out[3 * x + 2] = clamp_u8(yy + ((K_CR_R * v + 32768) >> 16));
    }
}

static void convert_row(Jpeg *j, int cs, Line *ln, uint8_t *out)
{
    uint32_t w = j->out_width;
    bool raw = (j->flags & MIME_JPEG_RAW_COLOR) != 0;
    const uint8_t *l0 = ln[0].row, *l1 = j->ncomp > 1 ? ln[1].row : NULL, *l2 = j->ncomp > 2 ? ln[2].row : NULL;
    const uint8_t *l3 = j->ncomp > 3 ? ln[3].row : NULL;
    if (j->ncomp == 1) {
        __builtin_memcpy(out, l0, w);
    } else if (j->ncomp == 3) {
        if (raw) {
            for (uint32_t x = 0; x < w; x++) {
                out[3 * x] = l0[x];
                out[3 * x + 1] = l1[x];
                out[3 * x + 2] = l2[x];
            }
        } else if (cs == MIME_JPEG_CS_RGB) {
            for (uint32_t x = 0; x < w; x++) {
                out[3 * x] = l2[x];
                out[3 * x + 1] = l1[x];
                out[3 * x + 2] = l0[x];
            }
        } else {
            ycc_to_bgr(l0, l1, l2, out, w);
        }
    } else {
        /* the stored values are inverted: 255 is no ink. */
        if (raw) {
            for (uint32_t x = 0; x < w; x++) {
                out[4 * x] = l0[x];
                out[4 * x + 1] = l1[x];
                out[4 * x + 2] = l2[x];
                out[4 * x + 3] = l3[x];
            }
        } else if (cs == MIME_JPEG_CS_YCCK) {
            for (uint32_t x = 0; x < w; x++) {
                int yy = l0[x], u = l1[x] - 128, v = l2[x] - 128;
                out[4 * x] = clamp_u8(yy + ((K_CR_R * v + 32768) >> 16));
                out[4 * x + 1] = clamp_u8(yy - ((K_CB_G * u + K_CR_G * v + 32768) >> 16));
                out[4 * x + 2] = clamp_u8(yy + ((K_CB_B * u + 32768) >> 16));
                out[4 * x + 3] = (uint8_t)(255 - l3[x]);
            }
        } else {
            for (uint32_t x = 0; x < w; x++) {
                out[4 * x] = (uint8_t)(255 - l0[x]);
                out[4 * x + 1] = (uint8_t)(255 - l1[x]);
                out[4 * x + 2] = (uint8_t)(255 - l2[x]);
                out[4 * x + 3] = (uint8_t)(255 - l3[x]);
            }
        }
    }
}

int jpeg_output(Jpeg *j, uint8_t *dest, size_t stride)
{
    Line ln[4];
    uint32_t w = j->out_width, h = j->out_height;
    bool fancy = (j->flags & MIME_JPEG_NO_FANCY) == 0;
    int cs = jpeg_color_space(j);
    uint32_t max_cw = 1;
    __builtin_memset(ln, 0, sizeof ln);
    for (int i = 0; i < j->ncomp; i++) {
        const JpegComp *c = &j->comp[i];
        Line *l = &ln[i];
        l->c = c;
        /* the samples of the component against those of the output: positions scale by hn / hd and vn / vd. */
        uint32_t hn = (uint32_t)c->h * (uint32_t)c->n, hd = (uint32_t)j->hmax * (uint32_t)j->scale;
        uint32_t vn = (uint32_t)c->v * (uint32_t)c->n, vd = (uint32_t)j->vmax * (uint32_t)j->scale;
        bool full_w = hn == hd, full_v = vn == vd;
        if (full_w && full_v && c->cw >= w && c->ch >= h) {
            l->mode = LINE_DIRECT;
            continue;
        }
        if (fancy && hn * 2 == hd && full_v && c->cw * 2 >= w && c->ch >= h)
            l->mode = LINE_H2V1;
        else if (fancy && hn * 2 == hd && vn * 2 == vd && c->cw * 2 >= w && c->ch * 2 + 1 >= h)
            l->mode = LINE_H2V2;
        else if (fancy && full_w && vn * 2 == vd && c->cw >= w && c->ch * 2 + 1 >= h)
            l->mode = LINE_H1V2;
        else
            l->mode = LINE_GENERIC;
        l->buf = jpeg_alloc(j, (size_t)w + 64);
        if (!l->buf)
            return MIME_JPEG_ERR_NOMEM;
        if (c->cw > max_cw)
            max_cw = c->cw;
        if (l->mode == LINE_GENERIC) {
            l->x0 = jpeg_alloc(j, (size_t)w * 9);
            if (!l->x0)
                return MIME_JPEG_ERR_NOMEM;
            l->x1 = (int32_t *)((uint8_t *)l->x0 + (size_t)w * 4);
            /* the weights are bytes after the two index arrays, which are 4 byte aligned as sizes of w * 4. */
            l->xf = (uint8_t *)l->x0 + (size_t)w * 8;
            for (uint32_t x = 0; x < w; x++)
                plan_axis(x, (uint32_t)c->h * (uint32_t)c->n, (uint32_t)j->hmax * (uint32_t)j->scale, c->cw, fancy,
                          &l->x0[x], &l->x1[x], &l->xf[x]);
        }
    }
    uint16_t *sum = jpeg_alloc(j, ((size_t)max_cw + 8) * 2);
    if (!sum)
        return MIME_JPEG_ERR_NOMEM;
    for (uint32_t y = 0; y < h; y++) {
        for (int i = 0; i < j->ncomp; i++)
            make_line(j, &ln[i], y, sum, fancy);
        convert_row(j, cs, ln, dest + (size_t)y * stride);
    }
    return 0;
}
