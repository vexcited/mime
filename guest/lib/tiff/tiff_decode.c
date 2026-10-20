#include "../png/inflate.h"
#include "tiff_internal.h"

typedef struct Dec {
    const MimeTiffFrame *f;
    const MimeAllocator *a;
    unsigned flags;
    unsigned bits;     /* bits per sample in the file. */
    unsigned spp;      /* samples per pixel. */
    unsigned spp_buf;  /* samples per pixel in a decoded chunk, 1 for planar files. */
    unsigned unit;     /* bytes per sample for 8 bits and more, else 0. */
    bool ycc;          /* subsampled or not, ycbcr data converted by this decoder. */
    bool odd;          /* depths from 3 to 15 bits, expanded to 16 bits. */
    bool planar;       /* planar data merged into chunky pixels. */
    bool be;
    unsigned yh, yv;
    uint8_t *buf;
    size_t buf_size;
    uint8_t *rev;
    size_t rev_cap;
    uint8_t *tmp;
    size_t tmp_cap;
    TiffLzwDec *lzw;
    MimeInflate *inf;
    TiffCcitt *ccitt;
    uint32_t ccitt_width;
    int32_t *ycc_tab; /* y, cr to r, cb to b, cr to g, cb to g. */
    bool data_error;
} Dec;

static bool supported_compression(unsigned c)
{
    switch (c) {
    case MIME_TIFF_COMPRESSION_NONE:
    case MIME_TIFF_COMPRESSION_CCITT_RLE:
    case MIME_TIFF_COMPRESSION_CCITT_G3:
    case MIME_TIFF_COMPRESSION_CCITT_G4:
    case MIME_TIFF_COMPRESSION_LZW:
    case MIME_TIFF_COMPRESSION_DEFLATE:
    case MIME_TIFF_COMPRESSION_DEFLATE_OLD:
    case MIME_TIFF_COMPRESSION_PACKBITS:
        return true;
    default:
        return false;
    }
}

/* the geometry of a decoded chunk buffer. */
typedef struct Geo {
    uint32_t buf_w;     /* pixels per row. */
    uint32_t buf_rows;  /* pixel rows. */
    size_t row_bytes;   /* bytes of a row, or of a row of units for ycbcr. */
    uint32_t rows;      /* rows of row_bytes in the buffer. */
    size_t need;
} Geo;

static bool chunk_geo(const Dec *d, const MimeTiffChunk *c, Geo *g)
{
    const MimeTiffFrame *f = d->f;
    g->buf_w = f->tile_width ? f->tile_width : f->width;
    g->buf_rows = f->tile_height ? f->tile_height : c->height;
    uint64_t row;
    if (d->ycc) {
        uint32_t units = (g->buf_w - 1) / d->yh + 1;
        row = (uint64_t)units * (d->yh * d->yv + 2);
        g->rows = (g->buf_rows - 1) / d->yv + 1;
    } else {
        row = ((uint64_t)g->buf_w * d->spp_buf * d->bits + 7) / 8;
        g->rows = g->buf_rows;
    }
    if (row > 0x7fffffffu)
        return false;
    uint64_t need = row * g->rows;
    if (need > MIME_TIFF_MAX_IMAGE_BYTES)
        return false;
    g->row_bytes = (size_t)row;
    g->need = (size_t)need;
    return true;
}

static int validate(const MimeTiffFrame *f)
{
    unsigned bits = f->bits_per_sample;
    if (bits == 0 || f->sample_format == 0 || !supported_compression(f->compression))
        return MIME_TIFF_ERR_UNSUPPORTED;
    if (f->fill_order != 1 && f->fill_order != 2)
        return MIME_TIFF_ERR_UNSUPPORTED;
    switch (f->photometric) {
    case MIME_TIFF_PHOTOMETRIC_WHITE_IS_ZERO:
    case MIME_TIFF_PHOTOMETRIC_BLACK_IS_ZERO:
    case MIME_TIFF_PHOTOMETRIC_RGB:
    case MIME_TIFF_PHOTOMETRIC_PALETTE:
    case MIME_TIFF_PHOTOMETRIC_SEPARATED:
    case MIME_TIFF_PHOTOMETRIC_YCBCR:
    case MIME_TIFF_PHOTOMETRIC_CIELAB:
        break;
    default:
        return MIME_TIFF_ERR_UNSUPPORTED;
    }
    bool whole = bits == 1 || bits == 2 || bits == 4 || bits == 8 || bits == 16 || bits == 32 || bits == 64;
    if (!whole && (bits < 3 || bits > 15 || f->photometric == MIME_TIFF_PHOTOMETRIC_PALETTE))
        return MIME_TIFF_ERR_UNSUPPORTED;
    if (!whole && f->sample_format != MIME_TIFF_SAMPLE_UINT)
        return MIME_TIFF_ERR_UNSUPPORTED;
    if (f->sample_format == MIME_TIFF_SAMPLE_FLOAT && bits != 16 && bits != 32 && bits != 64)
        return MIME_TIFF_ERR_UNSUPPORTED;
    if (f->compression >= MIME_TIFF_COMPRESSION_CCITT_RLE && f->compression <= MIME_TIFF_COMPRESSION_CCITT_G4) {
        if (bits != 1 || f->samples_per_pixel != 1)
            return MIME_TIFF_ERR_UNSUPPORTED;
        uint32_t w = f->tile_width ? f->tile_width : f->width;
        if (w > (1u << 20) || (f->compression == MIME_TIFF_COMPRESSION_CCITT_G3 && (f->t4_options & 2)))
            return MIME_TIFF_ERR_UNSUPPORTED;
    }
    if (f->photometric == MIME_TIFF_PHOTOMETRIC_YCBCR) {
        if (bits != 8 || f->samples_per_pixel != 3 || f->planar != 1 || f->ycbcr_h > 4 || f->ycbcr_v > 4 ||
            (f->predictor != 1 && (f->ycbcr_h != 1 || f->ycbcr_v != 1)))
            return MIME_TIFF_ERR_UNSUPPORTED;
    }
    if (f->planar == 2 && f->samples_per_pixel > 1 && (bits < 8 || (bits & 7)))
        return MIME_TIFF_ERR_UNSUPPORTED;
    if (f->predictor == 2) {
        if (bits != 8 && bits != 16 && bits != 32 && bits != 64)
            return MIME_TIFF_ERR_UNSUPPORTED;
    } else if (f->predictor == 3) {
        if (bits != 16 && bits != 32 && bits != 64)
            return MIME_TIFF_ERR_UNSUPPORTED;
    } else if (f->predictor != 1) {
        return MIME_TIFF_ERR_UNSUPPORTED;
    }
    if (f->tile_width) {
        uint32_t across = (f->width - 1) / f->tile_width + 1;
        unsigned sp = f->planar == 2 ? 1 : f->samples_per_pixel;
        if (across > 1 && f->photometric != MIME_TIFF_PHOTOMETRIC_YCBCR &&
            ((uint64_t)f->tile_width * sp * bits) % 8 != 0)
            return MIME_TIFF_ERR_UNSUPPORTED;
        if (across > 1 && f->photometric == MIME_TIFF_PHOTOMETRIC_YCBCR && f->tile_width % f->ycbcr_h != 0)
            return MIME_TIFF_ERR_UNSUPPORTED;
    }
    return MIME_TIFF_OK;
}

static void *dec_alloc(Dec *d, size_t n)
{
    return mime_alloc(d->a, n ? n : 1);
}

static bool grow(Dec *d, uint8_t **p, size_t *cap, size_t need)
{
    if (*cap >= need)
        return true;
    uint8_t *n = dec_alloc(d, need);
    if (!n)
        return false;
    mime_free(d->a, *p);
    *p = n;
    *cap = need;
    return true;
}

static void dec_free(Dec *d)
{
    mime_free(d->a, d->buf);
    mime_free(d->a, d->rev);
    mime_free(d->a, d->tmp);
    mime_free(d->a, d->lzw);
    mime_free(d->a, d->inf);
    mime_free(d->a, d->ccitt);
    mime_free(d->a, d->ycc_tab);
}

/* 1 when the compressed data was bad, a negative value for an allocation failure. */
static int decompress(Dec *d, const uint8_t *raw, size_t rawn, uint8_t *dst, size_t need, const Geo *g)
{
    const MimeTiffFrame *f = d->f;
    size_t got = 0;
    bool err = false;
    switch (f->compression) {
    case MIME_TIFF_COMPRESSION_NONE:
        got = rawn < need ? rawn : need;
        memcpy(dst, raw, got);
        break;
    case MIME_TIFF_COMPRESSION_PACKBITS:
        got = tiff_packbits_decode(raw, rawn, dst, need);
        break;
    case MIME_TIFF_COMPRESSION_LZW:
        if (!d->lzw && !(d->lzw = dec_alloc(d, sizeof *d->lzw)))
            return -1;
        got = tiff_lzw_decode(d->lzw, raw, rawn, dst, need, &err);
        break;
    case MIME_TIFF_COMPRESSION_DEFLATE:
    case MIME_TIFF_COMPRESSION_DEFLATE_OLD: {
        if (!d->inf && !(d->inf = dec_alloc(d, sizeof *d->inf)))
            return -1;
        int rc = mime_inflate_with(d->inf, raw, rawn, dst, need, &got, MIME_INFLATE_ZLIB | MIME_INFLATE_NO_CHECKSUM);
        if (rc == MIME_INFLATE_ERR_NOMEM)
            return -1;
        if (rc != MIME_INFLATE_OK && !(rc == MIME_INFLATE_ERR_OUTPUT && got >= need))
            err = true;
        break;
    }
    default: {
        if (!d->ccitt || d->ccitt_width != g->buf_w) {
            mime_free(d->a, d->ccitt);
            d->ccitt = dec_alloc(d, tiff_ccitt_state_size(g->buf_w));
            if (!d->ccitt)
                return -1;
            d->ccitt_width = g->buf_w;
            tiff_ccitt_init(d->ccitt, g->buf_w, true);
        }
        bool e2;
        uint32_t rows = tiff_ccitt_decode(d->ccitt, f->compression, f->compression == MIME_TIFF_COMPRESSION_CCITT_G4
                                                                            ? f->t6_options
                                                                            : f->t4_options,
                                          raw, rawn, dst, g->row_bytes, g->buf_rows, &e2);
        err = e2;
        got = (size_t)rows * g->row_bytes;
        if ((d->flags & MIME_TIFF_DECODE_CCITT_PHOTOMETRIC) && f->photometric == MIME_TIFF_PHOTOMETRIC_BLACK_IS_ZERO)
            for (size_t i = 0; i < need; i++)
                dst[i] = (uint8_t)~dst[i];
        break;
    }
    }
    if (f->compression < MIME_TIFF_COMPRESSION_CCITT_RLE || f->compression > MIME_TIFF_COMPRESSION_CCITT_G4) {
        if (got < need) {
            memset(dst + got, 0, need - got);
            err = true;
        }
    }
    return err ? 1 : 0;
}

static void postprocess(Dec *d, uint8_t *buf, const Geo *g)
{
    const MimeTiffFrame *f = d->f;
    if (f->predictor == 3) {
        for (uint32_t r = 0; r < g->rows; r++)
            tiff_float_undo(buf + (size_t)r * g->row_bytes, (size_t)g->buf_w * d->spp_buf, d->spp_buf, d->unit,
                            d->tmp);
        return;
    }
    if (d->be && d->unit > 1)
        tiff_swap_units(buf, g->need / d->unit, d->unit);
    if (f->predictor == 2)
        for (uint32_t r = 0; r < g->rows; r++)
            tiff_predictor_undo(buf + (size_t)r * g->row_bytes, (size_t)g->buf_w * d->spp_buf, d->spp_buf, d->unit);
}

/* decodes a chunk into dst, which holds g->need bytes. */
static int decode_chunk(Dec *d, const MimeTiffChunk *c, uint8_t *dst, const Geo *g)
{
    int rc;
    if (!c->data || c->size == 0) {
        memset(dst, 0, g->need);
        d->data_error = true;
        return 0;
    }
    const uint8_t *raw = c->data;
    if (d->f->fill_order == 2) {
        if (!grow(d, &d->rev, &d->rev_cap, c->size))
            return -1;
        memcpy(d->rev, raw, c->size);
        tiff_reverse_bits(d->rev, c->size);
        raw = d->rev;
    }
    rc = decompress(d, raw, c->size, dst, g->need, g);
    if (rc < 0)
        return rc;
    if (rc)
        d->data_error = true;
    postprocess(d, dst, g);
    return 0;
}

/* unpacks count samples of bits bits each, most significant bit first, to 16 bit values spanning the full range. */
static void unpack_odd(const uint8_t *src, uint8_t *dst, size_t count, unsigned bits)
{
    uint32_t acc = 0;
    unsigned cnt = 0;
    uint32_t mask = (1u << bits) - 1;
    for (size_t i = 0; i < count; i++) {
        while (cnt < bits) {
            acc = (acc << 8) | *src++;
            cnt += 8;
        }
        uint32_t v = (acc >> (cnt - bits)) & mask;
        cnt -= bits;
        uint32_t x = v << (16 - bits);
        for (unsigned s = bits; s < 16; s <<= 1)
            x |= x >> s;
        uint16_t o = (uint16_t)x;
        memcpy(dst + i * 2, &o, 2);
    }
}

static inline uint8_t clamp8(int32_t v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
}

static bool ycc_tables(Dec *d)
{
    const MimeTiffFrame *f = d->f;
    d->ycc_tab = dec_alloc(d, 5 * 256 * sizeof(int32_t));
    if (!d->ycc_tab)
        return false;
    int32_t *y_tab = d->ycc_tab, *cr_r = y_tab + 256, *cb_b = cr_r + 256, *cr_g = cb_b + 256, *cb_g = cr_g + 256;
    int64_t lr = f->ycbcr_coefficients[0], lg = f->ycbcr_coefficients[1], lb = f->ycbcr_coefficients[2];
    const int32_t *rb = f->reference_black_white;
    int64_t ys = 255 << 16, cs = 127 << 16;
    int64_t yr = (int64_t)rb[1] - rb[0], cbr = (int64_t)rb[3] - rb[2], crr = (int64_t)rb[5] - rb[4];
    int64_t y_scale = yr > 0 ? tiff_fixed_div(ys, yr) : 1 << 16;
    int64_t cb_scale = cbr > 0 ? tiff_fixed_div(cs, cbr) : 1 << 16;
    int64_t cr_scale = crr > 0 ? tiff_fixed_div(cs, crr) : 1 << 16;
    /* the weights of the chroma samples in green follow from the luma weights. */
    int64_t two = 2 << 16;
    int64_t fr = two - 2 * lr, fb = two - 2 * lb;
    int64_t kr = tiff_fixed_div((lr * fr) >> 16, lg);
    int64_t kb = tiff_fixed_div((lb * fb) >> 16, lg);
    for (int v = 0; v < 256; v++) {
        int64_t yv = (((int64_t)v << 16) - rb[0]) * y_scale >> 16;
        int64_t cbv = (((int64_t)v << 16) - rb[2]) * cb_scale >> 16;
        int64_t crv = (((int64_t)v << 16) - rb[4]) * cr_scale >> 16;
        y_tab[v] = (int32_t)yv;
        cr_r[v] = (int32_t)(crv * fr >> 16);
        cb_b[v] = (int32_t)(cbv * fb >> 16);
        cr_g[v] = (int32_t)(-(crv * kr >> 16));
        cb_g[v] = (int32_t)(-(cbv * kb >> 16));
    }
    return true;
}

/* copies the rows r0 to r1 of chunk c, which is in buf, to out whose first row is image row y0. */
static void emit(Dec *d, const MimeTiffChunk *c, const Geo *g, const uint8_t *buf, uint32_t r0, uint32_t r1,
                 uint32_t y0, uint8_t *out, size_t stride)
{
    const MimeTiffFrame *f = d->f;
    uint32_t vis_w = c->width;
    if (d->ycc) {
        unsigned yh = d->yh, yv = d->yv, ub = yh * yv + 2;
        const int32_t *yt = d->ycc_tab, *crr = yt + 256, *cbb = crr + 256, *crg = cbb + 256, *cbg = crg + 256;
        bool raw = (d->flags & MIME_TIFF_DECODE_RAW_YCBCR) != 0;
        for (uint32_t y = r0; y < r1; y++) {
            const uint8_t *brow = buf + (size_t)(y / yv) * g->row_bytes;
            unsigned iy = y % yv;
            uint8_t *o = out + (size_t)(c->y + y - y0) * stride + (size_t)c->x * 3;
            uint32_t x = 0;
            for (const uint8_t *u = brow; x < vis_w; u += ub) {
                unsigned cb = u[yh * yv], cr = u[yh * yv + 1];
                for (unsigned ix = 0; ix < yh && x < vis_w; ix++, x++, o += 3) {
                    unsigned yy = u[iy * yh + ix];
                    if (raw) {
                        o[0] = (uint8_t)yy;
                        o[1] = (uint8_t)cb;
                        o[2] = (uint8_t)cr;
                    } else {
                        int32_t Y = yt[yy];
                        o[0] = clamp8((Y + crr[cr] + 0x8000) >> 16);
                        o[1] = clamp8((Y + crg[cr] + cbg[cb] + 0x8000) >> 16);
                        o[2] = clamp8((Y + cbb[cb] + 0x8000) >> 16);
                    }
                }
            }
        }
        return;
    }
    if (d->planar) {
        unsigned bs = d->unit, spp = d->spp;
        unsigned plane = c->plane;
        for (uint32_t y = r0; y < r1; y++) {
            const uint8_t *s = buf + (size_t)y * g->row_bytes;
            uint8_t *o = out + (size_t)(c->y + y - y0) * stride + ((size_t)c->x * spp + plane) * bs;
            size_t step = (size_t)spp * bs;
            if (bs == 1)
                for (uint32_t i = 0; i < vis_w; i++)
                    o[i * step] = s[i];
            else if (bs == 2)
                for (uint32_t i = 0; i < vis_w; i++)
                    memcpy(o + i * step, s + i * 2, 2);
            else if (bs == 4)
                for (uint32_t i = 0; i < vis_w; i++)
                    memcpy(o + i * step, s + i * 4, 4);
            else
                for (uint32_t i = 0; i < vis_w; i++)
                    memcpy(o + i * step, s + i * 8, 8);
        }
        return;
    }
    if (d->odd) {
        size_t count = (size_t)vis_w * d->spp;
        for (uint32_t y = r0; y < r1; y++)
            unpack_odd(buf + (size_t)y * g->row_bytes, out + (size_t)(c->y + y - y0) * stride + (size_t)c->x * d->spp * 2,
                       count, d->bits);
        return;
    }
    size_t xbytes = (size_t)(((uint64_t)c->x * d->spp * d->bits) >> 3);
    size_t cbytes = (size_t)(((uint64_t)vis_w * d->spp * d->bits + 7) >> 3);
    for (uint32_t y = r0; y < r1; y++)
        memcpy(out + (size_t)(c->y + y - y0) * stride + xbytes, buf + (size_t)y * g->row_bytes, cbytes);
    (void)f;
}

int mime_tiff_frame_decode_rows(const MimeTiffFrame *f, const MimeAllocator *alloc, unsigned flags, uint32_t y,
                                uint32_t count, void *out_v, size_t stride)
{
    if (!f || !alloc || (count && !out_v))
        return MIME_TIFF_ERR_PARAM;
    if (y > f->height || count > f->height - y)
        return MIME_TIFF_ERR_RANGE;
    int rc = validate(f);
    if (rc != MIME_TIFF_OK)
        return rc;
    if (stride < f->out_row_bytes)
        return MIME_TIFF_ERR_PARAM;
    if (count == 0)
        return MIME_TIFF_OK;
    uint8_t *out = out_v;

    Dec d = {.f = f, .a = alloc, .flags = flags};
    d.bits = f->bits_per_sample;
    d.spp = f->samples_per_pixel;
    d.planar = f->planar == 2 && d.spp > 1;
    d.spp_buf = f->planar == 2 ? 1 : d.spp;
    d.unit = (d.bits >= 8 && (d.bits & 7) == 0) ? d.bits / 8 : 0;
    d.be = f->ifd.tiff.big_endian;
    d.ycc = f->photometric == MIME_TIFF_PHOTOMETRIC_YCBCR;
    d.odd = f->out_bits_per_sample != f->bits_per_sample;
    d.yh = f->ycbcr_h;
    d.yv = f->ycbcr_v;
    uint32_t y1 = y + count;

    /* the largest chunk buffer and the temporary row for the floating point predictor. */
    MimeTiffChunk c0;
    Geo gmax = {0};
    mime_tiff_frame_chunk(f, 0, &c0);
    {
        MimeTiffChunk probe = c0;
        probe.height = f->tile_height ? f->tile_height : f->rows_per_strip;
        if (!chunk_geo(&d, &probe, &gmax))
            return MIME_TIFF_ERR_NOMEM;
    }
    bool direct_ok = !f->tile_width && !d.planar && !d.ycc && !d.odd && stride == f->out_row_bytes &&
                     gmax.row_bytes == f->out_row_bytes;
    if (f->predictor == 3) {
        d.tmp_cap = gmax.row_bytes;
        d.tmp = dec_alloc(&d, d.tmp_cap);
        if (!d.tmp) {
            rc = MIME_TIFF_ERR_NOMEM;
            goto done;
        }
    }
    if (d.ycc && !(flags & MIME_TIFF_DECODE_RAW_YCBCR) && !ycc_tables(&d)) {
        rc = MIME_TIFF_ERR_NOMEM;
        goto done;
    }
    if (d.ycc && (flags & MIME_TIFF_DECODE_RAW_YCBCR) && !(d.ycc_tab = dec_alloc(&d, 5 * 256 * sizeof(int32_t)))) {
        rc = MIME_TIFF_ERR_NOMEM;
        goto done;
    }
    if (d.planar)
        for (uint32_t r = 0; r < count; r++)
            memset(out + (size_t)r * stride, 0, f->out_row_bytes);

    uint32_t first_chunk, last_chunk; /* within a plane. */
    uint32_t across = 1;
    if (f->tile_width) {
        across = (f->width + f->tile_width - 1) / f->tile_width;
        first_chunk = (y / f->tile_height) * across;
        last_chunk = ((y1 - 1) / f->tile_height + 1) * across;
    } else {
        first_chunk = y / f->rows_per_strip;
        last_chunk = (y1 - 1) / f->rows_per_strip + 1;
    }
    uint32_t planes = f->planar == 2 ? f->samples_per_pixel : 1;
    for (uint32_t p = 0; p < planes; p++) {
        for (uint32_t local = first_chunk; local < last_chunk; local++) {
            MimeTiffChunk c;
            uint32_t idx = p * f->chunks_per_plane + local;
            mime_tiff_frame_chunk(f, idx, &c);
            Geo g;
            if (!chunk_geo(&d, &c, &g)) {
                rc = MIME_TIFF_ERR_NOMEM;
                goto done;
            }
            uint32_t r0 = y > c.y ? y - c.y : 0;
            uint32_t r1 = c.height;
            if (y1 < c.y + c.height)
                r1 = y1 - c.y;
            if (direct_ok && p == 0 && r0 == 0 && r1 == c.height) {
                uint8_t *dst = out + (size_t)(c.y - y) * stride;
                rc = decode_chunk(&d, &c, dst, &g);
                if (rc < 0) {
                    rc = MIME_TIFF_ERR_NOMEM;
                    goto done;
                }
                continue;
            }
            if (!grow(&d, &d.buf, &d.buf_size, g.need)) {
                rc = MIME_TIFF_ERR_NOMEM;
                goto done;
            }
            rc = decode_chunk(&d, &c, d.buf, &g);
            if (rc < 0) {
                rc = MIME_TIFF_ERR_NOMEM;
                goto done;
            }
            emit(&d, &c, &g, d.buf, r0, r1, y, out, stride);
        }
    }
    rc = MIME_TIFF_OK;
    if (d.data_error && !(flags & MIME_TIFF_DECODE_TOLERANT))
        rc = MIME_TIFF_ERR_DATA;
done:
    dec_free(&d);
    return rc;
}

int mime_tiff_frame_decode(const MimeTiffFrame *f, const MimeAllocator *alloc, unsigned flags, void *out,
                           size_t stride)
{
    if (!f)
        return MIME_TIFF_ERR_PARAM;
    return mime_tiff_frame_decode_rows(f, alloc, flags, 0, f->height, out, stride);
}
