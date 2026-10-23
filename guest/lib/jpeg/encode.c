/*
 * the encoder: component layout, color transform and chroma subsampling, quantization, and the markers around the
 * entropy coded data of entropy.c.
 *
 * the picture is cut into MCU rows. each row is converted to planar samples, subsampled planes are averaged down by
 * whole factors, and every block is transformed and quantized. a baseline file with the standard tables is written
 * row by row, other files keep every block of the picture for the passes their tables or scans need.
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

/*
 * the JFIF transform with 15 bit fractions. luma is 0.299 red, 0.587 green and 0.114 blue, the chroma are the blue
 * and red differences from it, scaled and centered on 128. each set of weights sums to one, so gray keeps its level
 * and its chroma stays at 128.
 */
#define Y_R 9798
#define Y_G 19235
#define Y_B 3735
#define CB_R (-5529)
#define CB_G (-10855)
#define CB_B 16384
#define CR_R 16384
#define CR_G (-13720)
#define CR_B (-2664)
#define Y_ROUND 16384
#define C_BIAS ((128 << 15) + 16383)

static inline uint8_t luma_of(int r, int g, int b)
{
    return (uint8_t)((Y_R * r + Y_G * g + Y_B * b + Y_ROUND) >> 15);
}

static inline void rgb_to_ycc(int r, int g, int b, uint8_t *y, uint8_t *cb, uint8_t *cr)
{
    *y = luma_of(r, g, b);
    *cb = (uint8_t)((CB_R * r + CB_G * g + CB_B * b + C_BIAS) >> 15);
    *cr = (uint8_t)((CR_R * r + CR_G * g + CR_B * b + C_BIAS) >> 15);
}

#ifdef MIME_JPEG_SSE2
/* eight samples as 16 bit values. */
static inline __m128i load8(const uint8_t *p)
{
    return _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)p), _mm_setzero_si128());
}

/* four pixels of b, g, r, or b, g, r and another byte, as channels in 32 bit lanes. */
static inline void split4(const uint8_t *src, int bpp, __m128i *b, __m128i *g, __m128i *r)
{
    __m128i v = _mm_loadu_si128((const __m128i *)src);
    if (bpp == 3) {
        const __m128i m0 = _mm_set_epi32(0, 0, 0, 0x00FFFFFF), m1 = _mm_set_epi32(0, 0, 0x00FFFFFF, 0);
        const __m128i m2 = _mm_set_epi32(0, 0x00FFFFFF, 0, 0), m3 = _mm_set_epi32(0x00FFFFFF, 0, 0, 0);
        v = _mm_or_si128(_mm_or_si128(_mm_and_si128(v, m0), _mm_and_si128(_mm_slli_si128(v, 1), m1)),
                         _mm_or_si128(_mm_and_si128(_mm_slli_si128(v, 2), m2), _mm_and_si128(_mm_slli_si128(v, 3), m3)));
    }
    const __m128i ff = _mm_set1_epi32(0xFF);
    *b = _mm_and_si128(v, ff);
    *g = _mm_and_si128(_mm_srli_epi32(v, 8), ff);
    *r = _mm_and_si128(_mm_srli_epi32(v, 16), ff);
}

/* eight pixels to the three planes. one pair multiply applies the weights of two channels. */
static inline void ycc8(const uint8_t *src, int bpp, uint8_t *y, uint8_t *cb, uint8_t *cr)
{
    __m128i b0, g0, r0, b1, g1, r1;
    split4(src, bpp, &b0, &g0, &r0);
    split4(src + 4 * bpp, bpp, &b1, &g1, &r1);
    __m128i b = _mm_packs_epi32(b0, b1), g = _mm_packs_epi32(g0, g1), r = _mm_packs_epi32(r0, r1);
    __m128i rg_lo = _mm_unpacklo_epi16(r, g), rg_hi = _mm_unpackhi_epi16(r, g);
    __m128i bz_lo = _mm_unpacklo_epi16(b, _mm_set1_epi16(1)), bz_hi = _mm_unpackhi_epi16(b, _mm_set1_epi16(1));
    const __m128i bias = _mm_set1_epi32(C_BIAS);
    /* red and green pairs against two weights, blue and one against blue's weight and the rounding. */
#define WEIGHTS(wr, wg) _mm_set1_epi32((int)(((uint32_t)(uint16_t)(wg) << 16) | (uint16_t)(wr)))
    const __m128i krg_y = WEIGHTS(Y_R, Y_G), kb_y = WEIGHTS(Y_B, Y_ROUND);
    const __m128i krg_cb = WEIGHTS(CB_R, CB_G), kb_cb = WEIGHTS(CB_B, 0);
    const __m128i krg_cr = WEIGHTS(CR_R, CR_G), kb_cr = WEIGHTS(CR_B, 0);
#undef WEIGHTS
    __m128i yl = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(rg_lo, krg_y), _mm_madd_epi16(bz_lo, kb_y)), 15);
    __m128i yh = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(rg_hi, krg_y), _mm_madd_epi16(bz_hi, kb_y)), 15);
    __m128i cbl = _mm_srai_epi32(
        _mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(rg_lo, krg_cb), _mm_madd_epi16(bz_lo, kb_cb)), bias), 15);
    __m128i cbh = _mm_srai_epi32(
        _mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(rg_hi, krg_cb), _mm_madd_epi16(bz_hi, kb_cb)), bias), 15);
    __m128i crl = _mm_srai_epi32(
        _mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(rg_lo, krg_cr), _mm_madd_epi16(bz_lo, kb_cr)), bias), 15);
    __m128i crh = _mm_srai_epi32(
        _mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(rg_hi, krg_cr), _mm_madd_epi16(bz_hi, kb_cr)), bias), 15);
    __m128i yy = _mm_packs_epi32(yl, yh), cbv = _mm_packs_epi32(cbl, cbh), crv = _mm_packs_epi32(crl, crh);
    _mm_storel_epi64((__m128i *)y, _mm_packus_epi16(yy, yy));
    _mm_storel_epi64((__m128i *)cb, _mm_packus_epi16(cbv, cbv));
    _mm_storel_epi64((__m128i *)cr, _mm_packus_epi16(crv, crv));
}
#endif

static void convert_row(const Enc *e, const uint8_t *src, uint8_t *const *dst, uint32_t w)
{
    int bpp = e->format == MIME_JPEG_PIXEL_BGR24 ? 3 : 4;
    uint32_t x = 0;
    switch (e->mode) {
    case MODE_GRAY:
        if (e->format == MIME_JPEG_PIXEL_GRAY8) {
            __builtin_memcpy(dst[0], src, w);
        } else {
            for (; x < w; x++, src += bpp)
                dst[0][x] = luma_of(src[2], src[1], src[0]);
        }
        break;
    case MODE_YCC:
#ifdef MIME_JPEG_SSE2
        /* three byte pixels are read four bytes at a time, so the last few pixels go to the code below. */
        for (; x + 10 <= w; x += 8, src += 8 * bpp)
            ycc8(src, bpp, &dst[0][x], &dst[1][x], &dst[2][x]);
#endif
        for (; x < w; x++, src += bpp)
            rgb_to_ycc(src[2], src[1], src[0], &dst[0][x], &dst[1][x], &dst[2][x]);
        break;
    case MODE_RGB:
        for (; x < w; x++, src += bpp) {
            dst[0][x] = src[2];
            dst[1][x] = src[1];
            dst[2][x] = src[0];
        }
        break;
    case MODE_CMYK:
        /* files store inverted values, 255 is no ink. */
        for (; x < w; x++, src += 4) {
            dst[0][x] = (uint8_t)(255 - src[0]);
            dst[1][x] = (uint8_t)(255 - src[1]);
            dst[2][x] = (uint8_t)(255 - src[2]);
            dst[3][x] = (uint8_t)(255 - src[3]);
        }
        break;
    default:
        for (; x < w; x++, src += 4) {
            rgb_to_ycc(src[0], src[1], src[2], &dst[0][x], &dst[1][x], &dst[2][x]);
            dst[3][x] = (uint8_t)(255 - src[3]);
        }
        break;
    }
}

/* averages each factor by factor group of samples, making rows of the padded width. */
static void downsample(const uint8_t *src, size_t src_stride, uint8_t *dst, size_t dst_stride, uint32_t out_w,
                       uint32_t out_h, int fx, int fy)
{
    int shift = 0;
    while ((1 << shift) < fx * fy)
        shift++;
    int round = (fx * fy) >> 1;
    for (uint32_t y = 0; y < out_h; y++) {
        uint32_t x = 0;
#ifdef MIME_JPEG_SSE2
        /* the usual halvings, eight samples at a time. the widths are multiples of eight. */
        if (fx <= 2 && fy <= 2) {
            const uint8_t *r0 = src + (size_t)(y * (uint32_t)fy) * src_stride;
            const uint8_t *r1 = fy == 2 ? r0 + src_stride : r0;
            const __m128i low = _mm_set1_epi16(0x00FF), rnd = _mm_set1_epi16((short)round);
            for (; x + 8 <= out_w; x += 8) {
                __m128i sum;
                if (fx == 2) {
                    __m128i a = _mm_loadu_si128((const __m128i *)(r0 + 2 * x));
                    sum = _mm_add_epi16(_mm_and_si128(a, low), _mm_srli_epi16(a, 8));
                    if (fy == 2) {
                        __m128i b = _mm_loadu_si128((const __m128i *)(r1 + 2 * x));
                        sum = _mm_add_epi16(sum, _mm_add_epi16(_mm_and_si128(b, low), _mm_srli_epi16(b, 8)));
                    }
                } else {
                    sum = _mm_add_epi16(load8(r0 + x), load8(r1 + x));
                }
                sum = _mm_srli_epi16(_mm_add_epi16(sum, rnd), shift);
                _mm_storel_epi64((__m128i *)(dst + y * dst_stride + x), _mm_packus_epi16(sum, sum));
            }
        }
#endif
        for (; x < out_w; x++) {
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
            for (uint32_t bx = 0; bx < c->bw; bx++) {
                int16_t natural[64], *zz = c->blocks + ((size_t)row * c->bw + bx) * 64;
                jpeg_fdct_quant(e->sub[i] + (size_t)by * 8 * e->sub_stride[i] + bx * 8, e->sub_stride[i],
                                e->invq[c->tq], natural);
                /* blocks are kept in coding order. */
                for (int k = 0; k < 64; k++)
                    zz[k] = natural[jpeg_zigzag[k]];
            }
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

/* ------------------------------------------------------------------ driver */

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
    e->separate = !e->progressive && (p->flags & MIME_JPEG_ENC_SEPARATE_SCANS);
    e->buffered = e->optimize || e->separate;
    e->baseline = (p->flags & MIME_JPEG_ENC_BASELINE) != 0;
    e->restart = p->restart_interval;

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
