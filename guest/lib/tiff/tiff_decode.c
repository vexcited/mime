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
    /* YCbCr to RGB: the codes mapped to the nominal ranges and the four chroma weights, all 16.16. */
    int32_t ymap[256], cbmap[256], crmap[256];
    int32_t k_cr_r, k_cb_b, k_cb_g, k_cr_g;
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


/* a product of two 16.16 numbers. */
static inline int32_t fixed_mul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b + 0x8000) >> 16);
}

/*
 * the conversion of section 21 of the specification. a code is first stretched from its reference black and white
 * to the nominal range, 0 to 255 for the luma and minus 127 to 127 for the chroma, and then red is the luma plus Cr
 * times twice the weight left by LumaRed, blue the same with Cb and LumaBlue, and green is the luma less LumaBlue
 * times blue and LumaRed times red, over LumaGreen. putting the first two into the third leaves green as the luma
 * less Cb times gb and Cr times gr, gb being LumaBlue times the weight of blue over LumaGreen and gr the same for
 * red. an empty reference range falls back to the default one.
 */
static void ycc_prepare(Dec *d)
{
    const MimeTiffFrame *f = d->f;
    static const int32_t default_bw[6] = {0, 255 << 16, 128 << 16, 255 << 16, 128 << 16, 255 << 16};
    static const int32_t nominal[3] = {255 << 16, 127 << 16, 127 << 16};
    int32_t *maps[3] = {d->ymap, d->cbmap, d->crmap};
    for (int ch = 0; ch < 3; ch++) {
        int32_t black = f->reference_black_white[2 * ch], white = f->reference_black_white[2 * ch + 1];
        if (white <= black) {
            black = default_bw[2 * ch];
            white = default_bw[2 * ch + 1];
        }
        for (int code = 0; code < 256; code++) {
            int64_t m = (((int64_t)code << 16) - black) * nominal[ch] / (white - black);
            /* a reference range of next to nothing would overflow the products. */
            maps[ch][code] = (int32_t)(m < -(256 << 16) ? -(256 << 16) : m > (256 << 16) ? (256 << 16) : m);
        }
    }
    int64_t lr = f->ycbcr_coefficients[0], lg = f->ycbcr_coefficients[1], lb = f->ycbcr_coefficients[2];
    if (lg <= 0) {
        lr = 19595;
        lg = 38470;
        lb = 7471;
    }
    d->k_cr_r = (int32_t)((2 << 16) - 2 * lr);
    d->k_cb_b = (int32_t)((2 << 16) - 2 * lb);
    d->k_cb_g = (int32_t)(lb * d->k_cb_b / lg);
    d->k_cr_g = (int32_t)(lr * d->k_cr_r / lg);
}

static inline uint8_t round_sample(int32_t v)
{
    return clamp8((v + 0x8000) >> 16);
}

/*
 * the rows r0 to r1 of a chunk made of units. a unit is the samples of the luma of a cell of yh by yv pixels, row
 * by row, followed by the one Cb and the one Cr of the cell, which every pixel of the cell shares.
 */
static void emit_ycc(const Dec *d, const MimeTiffChunk *c, const Geo *g, const uint8_t *buf, uint32_t r0, uint32_t r1,
                     uint32_t y0, uint8_t *out, size_t stride)
{
    unsigned cw = d->yh, ch = d->yv, cell = cw * ch;
    bool raw = (d->flags & MIME_TIFF_DECODE_RAW_YCBCR) != 0;
    for (uint32_t y = r0; y < r1; y++) {
        const uint8_t *unit = buf + (size_t)(y / ch) * g->row_bytes;
        const uint8_t *luma_row = unit + (size_t)(y % ch) * cw;
        uint8_t *o = out + (size_t)(c->y + y - y0) * stride + (size_t)c->x * 3;
        for (uint32_t x = 0; x < c->width; unit += cell + 2, luma_row += cell + 2) {
            uint8_t cb = unit[cell], cr = unit[cell + 1];
            int32_t red = 0, green = 0, blue = 0;
            if (!raw) {
                int32_t u = d->cbmap[cb], v = d->crmap[cr];
                red = fixed_mul(d->k_cr_r, v);
                blue = fixed_mul(d->k_cb_b, u);
                green = -fixed_mul(d->k_cb_g, u) - fixed_mul(d->k_cr_g, v);
            }
            for (unsigned i = 0; i < cw && x < c->width; i++, x++, o += 3) {
                if (raw) {
                    o[0] = luma_row[i];
                    o[1] = cb;
                    o[2] = cr;
                } else {
                    int32_t luma = d->ymap[luma_row[i]];
                    o[0] = round_sample(luma + red);
                    o[1] = round_sample(luma + green);
                    o[2] = round_sample(luma + blue);
                }
            }
        }
    }
}

/* copies the rows r0 to r1 of chunk c, which is in buf, to out whose first row is image row y0. */
static void emit(Dec *d, const MimeTiffChunk *c, const Geo *g, const uint8_t *buf, uint32_t r0, uint32_t r1,
                 uint32_t y0, uint8_t *out, size_t stride)
{
    const MimeTiffFrame *f = d->f;
    uint32_t vis_w = c->width;
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
    if (d->ycc) {
        emit_ycc(d, c, g, buf, r0, r1, y0, out, stride);
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
    if (d.ycc && !(flags & MIME_TIFF_DECODE_RAW_YCBCR))
        ycc_prepare(&d);
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
