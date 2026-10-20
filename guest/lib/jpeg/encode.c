/*
 * the encoder: the layout of the components, the color transform and chroma subsampling, the quantization and the
 * markers around the entropy coded data of entropy.c.
 *
 * a picture is cut into rows of MCUs. a row at a time is converted to planar samples, the planes with fewer
 * samples are averaged down by whole factors, and every block goes through the transform and the quantization.
 * a baseline file with the tables of the standard is written as the rows are made, the others keep every block of
 * the picture for the passes over it that their tables or scans need.
 */
#include "enc.h"

void *enc_alloc(Enc *e, size_t size)
{
    if (e->mem_count >= (int)(sizeof e->mem / sizeof e->mem[0]))
        return NULL;
    void *p = mime_alloc(e->alloc, size ? size : 1);
    if (p)
        e->mem[e->mem_count++] = p;
    return p;
}

static void *enc_alloc_array(Enc *e, uint64_t count, uint64_t item)
{
    uint64_t total = count * item;
    if (item && total / item != count)
        return NULL;
    if (total > (uint64_t)(SIZE_MAX / 2))
        return NULL;
    return enc_alloc(e, (size_t)total);
}

/* ------------------------------------------------------------------ color */

#define Y_R 19595
#define Y_G 38470
#define Y_B 7471
#define CB_R (-11058)
#define CB_G (-21710)
#define CR_G (-27439)
#define CR_B (-5329)
#define HALF 32768
#define BIAS_128 ((128 << 16) + 32767)

static inline void rgb_to_ycc(int r, int g, int b, uint8_t *y, uint8_t *cb, uint8_t *cr)
{
    *y = (uint8_t)((Y_R * r + Y_G * g + Y_B * b + HALF) >> 16);
    *cb = (uint8_t)((CB_R * r + CB_G * g + HALF * b + BIAS_128) >> 16);
    *cr = (uint8_t)((HALF * r + CR_G * g + CR_B * b + BIAS_128) >> 16);
}

/* one row of pixels to the planes of the components. */
static void convert_row(const Enc *e, const uint8_t *src, uint8_t *const *dst, uint32_t w)
{
    int bpp = e->format == MIME_JPEG_PIXEL_BGR24 ? 3 : 4;
    switch (e->mode) {
    case MODE_GRAY:
        if (e->format == MIME_JPEG_PIXEL_GRAY8) {
            __builtin_memcpy(dst[0], src, w);
        } else {
            for (uint32_t x = 0; x < w; x++, src += bpp)
                dst[0][x] = (uint8_t)((Y_R * src[2] + Y_G * src[1] + Y_B * src[0] + HALF) >> 16);
        }
        break;
    case MODE_YCC:
        for (uint32_t x = 0; x < w; x++, src += bpp)
            rgb_to_ycc(src[2], src[1], src[0], &dst[0][x], &dst[1][x], &dst[2][x]);
        break;
    case MODE_RGB:
        for (uint32_t x = 0; x < w; x++, src += bpp) {
            dst[0][x] = src[2];
            dst[1][x] = src[1];
            dst[2][x] = src[0];
        }
        break;
    case MODE_CMYK:
        /* the files hold the inverted values, 255 is no ink. */
        for (uint32_t x = 0; x < w; x++, src += 4) {
            dst[0][x] = (uint8_t)(255 - src[0]);
            dst[1][x] = (uint8_t)(255 - src[1]);
            dst[2][x] = (uint8_t)(255 - src[2]);
            dst[3][x] = (uint8_t)(255 - src[3]);
        }
        break;
    default:
        for (uint32_t x = 0; x < w; x++, src += 4) {
            rgb_to_ycc(src[0], src[1], src[2], &dst[0][x], &dst[1][x], &dst[2][x]);
            dst[3][x] = (uint8_t)(255 - src[3]);
        }
        break;
    }
}

/* the average of the samples of each factor by factor group, the rows being made to the padded width. */
static void downsample(const uint8_t *src, size_t src_stride, uint8_t *dst, size_t dst_stride, uint32_t out_w,
                       uint32_t out_h, int fx, int fy)
{
    int shift = 0;
    while ((1 << shift) < fx * fy)
        shift++;
    int round = (fx * fy) >> 1;
    for (uint32_t y = 0; y < out_h; y++) {
        for (uint32_t x = 0; x < out_w; x++) {
            int sum = 0;
            for (int dy = 0; dy < fy; dy++) {
                const uint8_t *p = src + (size_t)(y * (uint32_t)fy + (uint32_t)dy) * src_stride + x * (uint32_t)fx;
                for (int dx = 0; dx < fx; dx++)
                    sum += p[dx];
            }
            dst[y * dst_stride + x] = (uint8_t)((sum + round) >> shift);
        }
    }
}

void enc_quantize_row(Enc *e, uint32_t mrow)
{
    uint32_t rows = (uint32_t)e->vmax * 8;
    uint32_t y0 = mrow * rows;
    uint32_t fw = e->mcux * 8 * (uint32_t)e->hmax;
    for (uint32_t r = 0; r < rows; r++) {
        uint32_t y = y0 + r < e->height ? y0 + r : e->height - 1;
        uint8_t *dst[4];
        for (int i = 0; i < e->ncomp; i++)
            dst[i] = e->full[i] + (size_t)r * e->full_stride;
        if (y0 + r > e->height - 1 && r > 0) {
            for (int i = 0; i < e->ncomp; i++)
                __builtin_memcpy(dst[i], dst[i] - e->full_stride, fw);
            continue;
        }
        convert_row(e, e->pixels + (size_t)y * e->stride, dst, e->width);
        for (int i = 0; i < e->ncomp; i++)
            __builtin_memset(dst[i] + e->width, dst[i][e->width - 1], fw - e->width);
    }
    for (int i = 0; i < e->ncomp; i++) {
        EncComp *c = &e->comp[i];
        if (e->sub[i] != e->full[i]) {
            downsample(e->full[i], e->full_stride, e->sub[i], e->sub_stride[i], e->mcux * 8 * (uint32_t)c->h,
                       (uint32_t)c->v * 8, e->hmax / c->h, e->vmax / c->v);
        }
        for (int by = 0; by < c->v; by++) {
            uint32_t row = e->buffered ? mrow * (uint32_t)c->v + (uint32_t)by : (uint32_t)by;
            for (uint32_t bx = 0; bx < c->bw; bx++)
                jpeg_fdct_quant(e->sub[i] + (size_t)by * 8 * e->sub_stride[i] + bx * 8, e->sub_stride[i],
                                e->invq[c->tq], c->blocks + ((size_t)row * c->bw + bx) * 64);
        }
    }
}

/* ------------------------------------------------------------------ markers */

static void put_segment_header(Enc *e, int marker, size_t payload)
{
    enc_put_u8(e, 0xFF);
    enc_put_u8(e, (unsigned)marker);
    enc_put_u16(e, (unsigned)payload + 2);
}

static unsigned comp_id(const Enc *e, int i)
{
    return e->mode == MODE_RGB ? (unsigned)"RGB"[i] : (unsigned)(i + 1);
}

static void write_headers(Enc *e, const MimeJpegEncodeParams *p)
{
    bool wide[2] = {false, false};
    enc_put_u8(e, 0xFF);
    enc_put_u8(e, M_SOI);
    if (e->mode == MODE_RGB || e->mode == MODE_CMYK || e->mode == MODE_YCCK) {
        static const uint8_t adobe_tail[] = {0, 100, 0, 0, 0, 0};
        put_segment_header(e, M_APP14, 12);
        enc_put_bytes(e, "Adobe", 5);
        enc_put_bytes(e, adobe_tail, sizeof adobe_tail);
        enc_put_u8(e, e->mode == MODE_YCCK ? 2 : 0);
    } else if (!(p->flags & MIME_JPEG_ENC_NO_JFIF)) {
        unsigned unit = p->density_unit, dx = p->x_density, dy = p->y_density;
        if (!dx || !dy || unit > 2) {
            unit = 0;
            dx = dy = 1;
        }
        put_segment_header(e, M_APP0, 14);
        enc_put_bytes(e, "JFIF\0", 5);
        enc_put_u8(e, 1);
        enc_put_u8(e, 1);
        enc_put_u8(e, unit);
        enc_put_u16(e, dx);
        enc_put_u16(e, dy);
        enc_put_u16(e, 0);
    }
    for (uint32_t i = 0; i < p->segment_count; i++) {
        put_segment_header(e, p->segments[i].marker, p->segments[i].size);
        enc_put_bytes(e, p->segments[i].data, p->segments[i].size);
    }
    if (p->comment_size) {
        put_segment_header(e, M_COM, p->comment_size);
        enc_put_bytes(e, p->comment, p->comment_size);
    }
    for (int t = 0; t < e->nq; t++) {
        for (int i = 0; i < 64; i++)
            if (e->qt[t][i] > 255)
                wide[t] = true;
        put_segment_header(e, M_DQT, 1 + (wide[t] ? 128u : 64u));
        enc_put_u8(e, (wide[t] ? 0x10u : 0u) | (unsigned)t);
        for (int k = 0; k < 64; k++) {
            if (wide[t])
                enc_put_u16(e, e->qt[t][jpeg_zigzag[k]]);
            else
                enc_put_u8(e, e->qt[t][jpeg_zigzag[k]]);
        }
    }
    put_segment_header(e, e->progressive ? M_SOF2 : e->wide_tables ? M_SOF1 : M_SOF0, 6 + 3 * (size_t)e->ncomp);
    enc_put_u8(e, 8);
    enc_put_u16(e, e->height);
    enc_put_u16(e, e->width);
    enc_put_u8(e, (unsigned)e->ncomp);
    for (int i = 0; i < e->ncomp; i++) {
        enc_put_u8(e, comp_id(e, i));
        enc_put_u8(e, (unsigned)(e->comp[i].h << 4 | e->comp[i].v));
        enc_put_u8(e, (unsigned)e->comp[i].tq);
    }
}

/* ------------------------------------------------------------------ the driver */

static int setup(Enc *e, const MimeJpegEncodeParams *p)
{
    static const uint8_t luma_h[5] = {2, 1, 2, 1, 4}, luma_v[5] = {2, 1, 1, 2, 1};
    int sub = p->subsampling;
    int hs = luma_h[sub], vs = luma_v[sub];
    switch (e->mode) {
    case MODE_GRAY:
        e->ncomp = 1;
        e->comp[0] = (EncComp){.h = 1, .v = 1};
        break;
    case MODE_YCC:
        e->ncomp = 3;
        e->comp[0] = (EncComp){.h = hs, .v = vs};
        e->comp[1] = (EncComp){.h = 1, .v = 1, .tq = 1, .cls = 1};
        e->comp[2] = (EncComp){.h = 1, .v = 1, .tq = 1, .cls = 1};
        break;
    case MODE_RGB:
        e->ncomp = 3;
        for (int i = 0; i < 3; i++)
            e->comp[i] = (EncComp){.h = 1, .v = 1};
        break;
    case MODE_CMYK:
        e->ncomp = 4;
        for (int i = 0; i < 4; i++)
            e->comp[i] = (EncComp){.h = 1, .v = 1};
        break;
    default:
        e->ncomp = 4;
        e->comp[0] = (EncComp){.h = hs, .v = vs};
        e->comp[1] = (EncComp){.h = 1, .v = 1, .tq = 1, .cls = 1};
        e->comp[2] = (EncComp){.h = 1, .v = 1, .tq = 1, .cls = 1};
        e->comp[3] = (EncComp){.h = hs, .v = vs};
        break;
    }
    e->hmax = e->vmax = 1;
    e->nq = 1;
    for (int i = 0; i < e->ncomp; i++) {
        e->hmax = jpeg_max(e->hmax, e->comp[i].h);
        e->vmax = jpeg_max(e->vmax, e->comp[i].v);
        e->nq = jpeg_max(e->nq, e->comp[i].tq + 1);
    }
    e->mcux = jpeg_ceil_div(e->width, 8u * (uint32_t)e->hmax);
    e->mcuy = jpeg_ceil_div(e->height, 8u * (uint32_t)e->vmax);
    e->progressive = (p->flags & MIME_JPEG_ENC_PROGRESSIVE) != 0;
    e->optimize = e->progressive || (p->flags & MIME_JPEG_ENC_OPTIMIZE);
    e->buffered = e->optimize;
    e->baseline = (p->flags & MIME_JPEG_ENC_BASELINE) != 0;
    e->restart = p->restart_interval;

    /* the quantization tables. */
    int quality = p->quality ? p->quality : 75;
    for (int t = 0; t < e->nq; t++) {
        const uint16_t *user = t == 0 ? p->qtable_luma : p->qtable_chroma;
        if (user) {
            for (int i = 0; i < 64; i++) {
                if (user[i] == 0)
                    return MIME_JPEG_ERR_PARAM;
                if (p->flags & MIME_JPEG_ENC_ZIGZAG_QTABLE)
                    e->qt[t][jpeg_zigzag[i]] = user[i];
                else
                    e->qt[t][i] = user[i];
            }
        } else {
            mime_jpeg_quality_table(quality, t == 1, e->qt[t]);
        }
        for (int i = 0; i < 64; i++) {
            if (e->baseline && e->qt[t][i] > 255)
                e->qt[t][i] = 255;
            if (e->qt[t][i] > 255)
                e->wide_tables = true;
        }
        jpeg_fdct_prepare(e->qt[t], e->invq[t]);
    }

    /* the buffers. */
    e->full_stride = (size_t)e->mcux * 8 * (size_t)e->hmax;
    for (int i = 0; i < e->ncomp; i++) {
        EncComp *c = &e->comp[i];
        c->bw = e->mcux * (uint32_t)c->h;
        c->bh = e->mcuy * (uint32_t)c->v;
        c->cbw = jpeg_ceil_div(jpeg_ceil_div(e->width * (uint32_t)c->h, (uint32_t)e->hmax), 8);
        c->cbh = jpeg_ceil_div(jpeg_ceil_div(e->height * (uint32_t)c->v, (uint32_t)e->vmax), 8);
        e->full[i] = enc_alloc_array(e, e->full_stride, (uint64_t)e->vmax * 8);
        if (!e->full[i])
            return MIME_JPEG_ERR_NOMEM;
        if (c->h == e->hmax && c->v == e->vmax) {
            e->sub[i] = e->full[i];
            e->sub_stride[i] = e->full_stride;
        } else {
            e->sub_stride[i] = (size_t)e->mcux * 8 * (size_t)c->h;
            e->sub[i] = enc_alloc_array(e, e->sub_stride[i], (uint64_t)c->v * 8);
            if (!e->sub[i])
                return MIME_JPEG_ERR_NOMEM;
        }
        uint64_t rows = e->buffered ? c->bh : (uint64_t)c->v;
        c->blocks = enc_alloc_array(e, (uint64_t)c->bw * rows, 128);
        if (!c->blocks)
            return MIME_JPEG_ERR_NOMEM;
    }
    return 0;
}

int mime_jpeg_encode(const uint8_t *pixels, uint32_t width, uint32_t height, size_t stride, int format,
                     const MimeJpegEncodeParams *params, const MimeAllocator *alloc, uint8_t **out, size_t *length)
{
    MimeJpegEncodeParams none;
    Enc *e;
    int rc = 0;
    if (!params) {
        __builtin_memset(&none, 0, sizeof none);
        params = &none;
    }
    if (!pixels || !alloc || !out || !length)
        return MIME_JPEG_ERR_PARAM;
    if (width == 0 || height == 0 || width > MIME_JPEG_MAX_DIM || height > MIME_JPEG_MAX_DIM ||
        (uint64_t)width * height > MIME_JPEG_MAX_PIXELS)
        return MIME_JPEG_ERR_TOO_LARGE;
    if (format < MIME_JPEG_PIXEL_GRAY8 || format > MIME_JPEG_PIXEL_CMYK32)
        return MIME_JPEG_ERR_PARAM;
    size_t bpp = format == MIME_JPEG_PIXEL_GRAY8 ? 1 : format == MIME_JPEG_PIXEL_BGR24 ? 3 : 4;
    if (stride < (size_t)width * bpp)
        return MIME_JPEG_ERR_PARAM;
    if (params->quality < 0 || params->quality > 100 || params->subsampling < 0 ||
        params->subsampling > MIME_JPEG_SUBSAMPLE_411 || params->restart_interval > 65535 ||
        params->comment_size > 65533 || (params->comment_size && !params->comment) ||
        (params->segment_count && !params->segments))
        return MIME_JPEG_ERR_PARAM;
    for (uint32_t i = 0; i < params->segment_count; i++) {
        const MimeJpegAppSegment *s = &params->segments[i];
        if (!((s->marker >= M_APP0 && s->marker <= 0xEF) || s->marker == M_COM) || s->size > 65533 ||
            (s->size && !s->data))
            return MIME_JPEG_ERR_PARAM;
    }

    e = mime_alloc(alloc, sizeof *e);
    if (!e)
        return MIME_JPEG_ERR_NOMEM;
    __builtin_memset(e, 0, sizeof *e);
    e->alloc = alloc;
    e->pixels = pixels;
    e->width = width;
    e->height = height;
    e->stride = stride;
    e->format = format;
    if (format == MIME_JPEG_PIXEL_GRAY8)
        e->mode = MODE_GRAY;
    else if (format == MIME_JPEG_PIXEL_CMYK32)
        e->mode = (params->flags & MIME_JPEG_ENC_YCCK) ? MODE_YCCK : MODE_CMYK;
    else if (params->flags & MIME_JPEG_ENC_GRAYSCALE)
        e->mode = MODE_GRAY;
    else
        e->mode = (params->flags & MIME_JPEG_ENC_RGB) ? MODE_RGB : MODE_YCC;

    rc = setup(e, params);
    if (!rc && e->buffered)
        for (uint32_t mrow = 0; mrow < e->mcuy; mrow++)
            enc_quantize_row(e, mrow);
    if (!rc) {
        write_headers(e, params);
        if (e->progressive) {
            rc = enc_entropy_progressive(e);
        } else {
            rc = enc_entropy_sequential(e);
        }
        enc_put_u8(e, 0xFF);
        enc_put_u8(e, M_EOI);
        if (!rc && e->oom)
            rc = MIME_JPEG_ERR_NOMEM;
    }
    if (!rc) {
        *out = e->buf;
        *length = e->pos;
        e->buf = NULL;
    }
    mime_free(alloc, e->buf);
    for (int i = 0; i < e->mem_count; i++)
        mime_free(alloc, e->mem[i]);
    mime_free(alloc, e);
    return rc;
}
