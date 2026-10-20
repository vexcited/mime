#include "../png/deflate.h"
#include "tiff_internal.h"

#define DEFAULT_STRIP_BYTES 8192u
#define MAX_SPP 16u

static bool enc_reserve(MimeTiffEncoder *e, size_t extra)
{
    TiffBuf b = {.alloc = e->alloc, .p = e->buf, .len = e->len, .cap = e->cap, .be = e->big_endian};
    bool ok = tiff_buf_reserve(&b, extra);
    e->buf = b.p;
    e->cap = b.cap;
    if (!ok)
        e->failed = true;
    return ok;
}

static bool enc_align(MimeTiffEncoder *e, size_t align)
{
    size_t pad = (align - (e->len & (align - 1))) & (align - 1);
    if (!pad)
        return true;
    if (!enc_reserve(e, pad))
        return false;
    memset(e->buf + e->len, 0, pad);
    e->len += pad;
    return true;
}

int mime_tiff_encoder_init(MimeTiffEncoder *e, const MimeAllocator *alloc, unsigned flags)
{
    if (!e || !alloc)
        return MIME_TIFF_ERR_PARAM;
    memset(e, 0, sizeof *e);
    e->alloc = alloc;
    e->big_endian = (flags & MIME_TIFF_ENCODE_BIG_ENDIAN) != 0;
    if (!enc_reserve(e, 64))
        return MIME_TIFF_ERR_NOMEM;
    e->buf[0] = e->buf[1] = e->big_endian ? 'M' : 'I';
    tiff_wr16(e->buf + 2, 42, e->big_endian);
    memset(e->buf + 4, 0, 4);
    e->len = 8;
    e->last_next_slot = 4;
    return MIME_TIFF_OK;
}

void mime_tiff_encoder_abort(MimeTiffEncoder *e)
{
    if (!e)
        return;
    mime_free(e->alloc, e->buf);
    e->buf = NULL;
    e->len = e->cap = 0;
    e->failed = true;
}

int mime_tiff_encoder_finish(MimeTiffEncoder *e, void **out, size_t *out_len)
{
    if (!e || !out || !out_len)
        return MIME_TIFF_ERR_PARAM;
    *out = NULL;
    if (e->failed || !e->frames) {
        int rc = e->failed ? MIME_TIFF_ERR_NOMEM : MIME_TIFF_ERR_PARAM;
        mime_tiff_encoder_abort(e);
        return rc;
    }
    *out = e->buf;
    *out_len = e->len;
    e->buf = NULL;
    e->len = e->cap = 0;
    e->failed = true;
    return MIME_TIFF_OK;
}

typedef struct Plan {
    uint32_t width, height;
    unsigned bits, spp, unit;
    unsigned compression, predictor;
    size_t row_bytes;
    uint32_t rps;
    uint32_t strips;
} Plan;

static int plan_frame(const MimeTiffFrameSpec *s, Plan *p)
{
    unsigned bits = s->bits_per_sample, spp = s->samples_per_pixel;
    if (s->width == 0 || s->height == 0 || s->width > 0x7fffffffu || s->height > 0x7fffffffu)
        return MIME_TIFF_ERR_PARAM;
    if (!(bits == 1 || bits == 2 || bits == 4 || bits == 8 || bits == 16 || bits == 32 || bits == 64) || spp == 0 ||
        spp > MAX_SPP || s->extra_samples > spp)
        return MIME_TIFF_ERR_PARAM;
    if (s->sample_format > 4 || (s->sample_format != 0 && s->sample_format != 1 && bits < 16))
        return MIME_TIFF_ERR_PARAM;
    unsigned comp = s->compression ? s->compression : MIME_TIFF_COMPRESSION_NONE;
    switch (comp) {
    case MIME_TIFF_COMPRESSION_NONE:
    case MIME_TIFF_COMPRESSION_LZW:
    case MIME_TIFF_COMPRESSION_PACKBITS:
    case MIME_TIFF_COMPRESSION_DEFLATE:
    case MIME_TIFF_COMPRESSION_DEFLATE_OLD:
        break;
    case MIME_TIFF_COMPRESSION_CCITT_RLE:
    case MIME_TIFF_COMPRESSION_CCITT_G3:
    case MIME_TIFF_COMPRESSION_CCITT_G4:
        if (bits != 1 || spp != 1 || s->width > (1u << 20))
            return MIME_TIFF_ERR_PARAM;
        break;
    default:
        return MIME_TIFF_ERR_UNSUPPORTED;
    }
    unsigned pred = s->predictor == 0 ? 1 : s->predictor;
    if (comp != MIME_TIFF_COMPRESSION_LZW && comp != MIME_TIFF_COMPRESSION_DEFLATE &&
        comp != MIME_TIFF_COMPRESSION_DEFLATE_OLD)
        pred = 1;
    if (pred == 2 && bits < 8)
        pred = 1;
    if (pred == 3 && bits < 16)
        return MIME_TIFF_ERR_PARAM;
    if (pred > 3)
        return MIME_TIFF_ERR_PARAM;
    if (s->photometric == MIME_TIFF_PHOTOMETRIC_PALETTE && (!s->color_map || spp != 1 || bits > 8))
        return MIME_TIFF_ERR_PARAM;
    uint64_t row = ((uint64_t)s->width * spp * bits + 7) / 8;
    if (row > 0x7fffffffu || row * s->height > MIME_TIFF_MAX_IMAGE_BYTES)
        return MIME_TIFF_ERR_NOMEM;
    p->width = s->width;
    p->height = s->height;
    p->bits = bits;
    p->spp = spp;
    p->unit = bits >= 8 ? bits / 8 : 0;
    p->compression = comp;
    p->predictor = pred;
    p->row_bytes = (size_t)row;
    uint32_t rps = s->rows_per_strip;
    if (rps == 0) {
        size_t r = DEFAULT_STRIP_BYTES / p->row_bytes;
        rps = r ? (uint32_t)r : 1;
    }
    if (rps > s->height)
        rps = s->height;
    p->rps = rps;
    p->strips = (s->height + rps - 1) / rps;
    if (p->strips > (1u << 22))
        return MIME_TIFF_ERR_PARAM;
    return MIME_TIFF_OK;
}

typedef struct Work {
    uint8_t *sbuf;
    size_t sbuf_cap;
    uint8_t *tmp;
    TiffLzwEnc *lzw;
    TiffCcitt *ccitt;
} Work;

static int encode_strip(MimeTiffEncoder *e, const MimeTiffFrameSpec *s, const Plan *p, Work *w, const uint8_t *pixels,
                        size_t stride, uint32_t rows)
{
    size_t need = (size_t)rows * p->row_bytes;
    unsigned comp = p->compression;
    bool swap = e->big_endian && p->unit > 1 && p->predictor != 3;

    if (comp >= MIME_TIFF_COMPRESSION_CCITT_RLE && comp <= MIME_TIFF_COMPRESSION_CCITT_G4) {
        TiffByteSink sink = {.alloc = e->alloc, .buf = &e->buf, .len = &e->len, .cap = &e->cap};
        tiff_ccitt_encode(w->ccitt, comp, s->group3_two_dim != 0, s->group3_fill != 0,
                          s->ccitt_photometric && s->photometric == MIME_TIFF_PHOTOMETRIC_BLACK_IS_ZERO, pixels, stride, rows, &sink);
        if (sink.failed) {
            e->failed = true;
            return MIME_TIFF_ERR_NOMEM;
        }
        return MIME_TIFF_OK;
    }
    if (comp == MIME_TIFF_COMPRESSION_NONE && p->predictor == 1 && !swap) {
        if (!enc_reserve(e, need))
            return MIME_TIFF_ERR_NOMEM;
        for (uint32_t r = 0; r < rows; r++) {
            memcpy(e->buf + e->len, pixels + (size_t)r * stride, p->row_bytes);
            e->len += p->row_bytes;
        }
        return MIME_TIFF_OK;
    }
    if (comp == MIME_TIFF_COMPRESSION_PACKBITS && p->predictor == 1 && !swap) {
        /* each row is packed on its own, so the rows are read in place. */
        if (!enc_reserve(e, need + need / 128 + (size_t)rows * 2 + 16))
            return MIME_TIFF_ERR_NOMEM;
        for (uint32_t r = 0; r < rows; r++)
            e->len += tiff_packbits_encode_row(pixels + (size_t)r * stride, p->row_bytes, e->buf + e->len);
        return MIME_TIFF_OK;
    }

    uint8_t *sb = w->sbuf;
    for (uint32_t r = 0; r < rows; r++)
        memcpy(sb + (size_t)r * p->row_bytes, pixels + (size_t)r * stride, p->row_bytes);
    size_t samples = (size_t)p->width * p->spp;
    if (p->predictor == 2)
        for (uint32_t r = 0; r < rows; r++)
            tiff_predictor_apply(sb + (size_t)r * p->row_bytes, samples, p->spp, p->unit);
    else if (p->predictor == 3)
        for (uint32_t r = 0; r < rows; r++)
            tiff_float_apply(sb + (size_t)r * p->row_bytes, samples, p->spp, p->unit, w->tmp);
    if (swap)
        tiff_swap_units(sb, need / p->unit, p->unit);

    switch (comp) {
    case MIME_TIFF_COMPRESSION_NONE:
        if (!enc_reserve(e, need))
            return MIME_TIFF_ERR_NOMEM;
        memcpy(e->buf + e->len, sb, need);
        e->len += need;
        break;
    case MIME_TIFF_COMPRESSION_PACKBITS:
        if (!enc_reserve(e, need + need / 128 + (size_t)rows * 2 + 16))
            return MIME_TIFF_ERR_NOMEM;
        for (uint32_t r = 0; r < rows; r++)
            e->len += tiff_packbits_encode_row(sb + (size_t)r * p->row_bytes, p->row_bytes, e->buf + e->len);
        break;
    case MIME_TIFF_COMPRESSION_LZW:
        if (!enc_reserve(e, tiff_lzw_bound(need)))
            return MIME_TIFF_ERR_NOMEM;
        e->len += tiff_lzw_encode(w->lzw, sb, need, e->buf + e->len);
        break;
    default: {
        void *z;
        size_t zn;
        int level = s->deflate_level ? s->deflate_level : 6;
        if (level > 9)
            level = 9;
        if (mime_deflate_zlib(sb, need, level, e->alloc, &z, &zn) != MIME_DEFLATE_OK) {
            e->failed = true;
            return MIME_TIFF_ERR_NOMEM;
        }
        if (!enc_reserve(e, zn)) {
            mime_free(e->alloc, z);
            return MIME_TIFF_ERR_NOMEM;
        }
        memcpy(e->buf + e->len, z, zn);
        e->len += zn;
        mime_free(e->alloc, z);
        break;
    }
    }
    return MIME_TIFF_OK;
}

typedef struct Tags {
    MimeTiffWEntry *list;
    uint32_t n;
} Tags;

static void tag_add(Tags *t, uint16_t tag, uint16_t type, uint32_t count, const void *data)
{
    MimeTiffWEntry *e = &t->list[t->n++];
    memset(e, 0, sizeof *e);
    e->tag = tag;
    e->type = type;
    e->count = count;
    e->data = data;
}

static void tag_string(Tags *t, uint16_t tag, const char *s)
{
    if (s)
        tag_add(t, tag, MIME_TIFF_TYPE_ASCII, (uint32_t)strlen(s) + 1, s);
}

int mime_tiff_encoder_add_frame(MimeTiffEncoder *e, const MimeTiffFrameSpec *s, const void *pixels, size_t stride)
{
    if (!e || !s || !pixels || e->failed || !e->buf)
        return MIME_TIFF_ERR_PARAM;
    Plan p;
    int rc = plan_frame(s, &p);
    if (rc != MIME_TIFF_OK)
        return rc;
    if (stride < p.row_bytes)
        return MIME_TIFF_ERR_PARAM;
    if (s->extra_tag_count && !s->extra_tags)
        return MIME_TIFF_ERR_PARAM;

    Work w = {0};
    uint32_t *offs = NULL;
    MimeTiffWEntry *list = NULL;
    uint32_t list_cap = s->extra_tag_count + 40;
    size_t sbuf_need = 0;

    bool needs_sbuf = !(p.compression >= MIME_TIFF_COMPRESSION_CCITT_RLE &&
                        p.compression <= MIME_TIFF_COMPRESSION_CCITT_G4) &&
                      !((p.compression == MIME_TIFF_COMPRESSION_NONE || p.compression == MIME_TIFF_COMPRESSION_PACKBITS) &&
                        p.predictor == 1 && !(e->big_endian && p.unit > 1));
    if (needs_sbuf)
        sbuf_need = p.row_bytes * p.rps;
    /* offsets, counts and the tag list in one block. */
    offs = mime_alloc(e->alloc, (size_t)p.strips * 8 + (size_t)list_cap * sizeof(MimeTiffWEntry) + 8);
    if (!offs)
        return MIME_TIFF_ERR_NOMEM;
    uint32_t *counts = offs + p.strips;
    list = (MimeTiffWEntry *)(counts + p.strips);
    if (needs_sbuf) {
        w.sbuf = mime_alloc(e->alloc, sbuf_need ? sbuf_need : 1);
        if (!w.sbuf) {
            rc = MIME_TIFF_ERR_NOMEM;
            goto fail;
        }
        if (p.predictor == 3) {
            w.tmp = mime_alloc(e->alloc, p.row_bytes);
            if (!w.tmp) {
                rc = MIME_TIFF_ERR_NOMEM;
                goto fail;
            }
        }
    }
    if (p.compression == MIME_TIFF_COMPRESSION_LZW) {
        w.lzw = mime_alloc(e->alloc, sizeof *w.lzw);
        if (!w.lzw) {
            rc = MIME_TIFF_ERR_NOMEM;
            goto fail;
        }
    }
    if (p.compression >= MIME_TIFF_COMPRESSION_CCITT_RLE && p.compression <= MIME_TIFF_COMPRESSION_CCITT_G4) {
        w.ccitt = mime_alloc(e->alloc, tiff_ccitt_state_size(p.width));
        if (!w.ccitt) {
            rc = MIME_TIFF_ERR_NOMEM;
            goto fail;
        }
    }

    /* uncompressed data has a known size, so the buffer grows once. */
    if (p.compression == MIME_TIFF_COMPRESSION_NONE && !enc_reserve(e, p.row_bytes * p.height + 4096)) {
        rc = MIME_TIFF_ERR_NOMEM;
        goto fail;
    }
    const uint8_t *src = pixels;
    for (uint32_t i = 0; i < p.strips; i++) {
        uint32_t y0 = i * p.rps;
        uint32_t rows = p.height - y0 < p.rps ? p.height - y0 : p.rps;
        if (!enc_align(e, 2)) {
            rc = MIME_TIFF_ERR_NOMEM;
            goto fail;
        }
        if (w.ccitt)
            tiff_ccitt_init(w.ccitt, p.width, false);
        size_t start = e->len;
        rc = encode_strip(e, s, &p, &w, src + (size_t)y0 * stride, stride, rows);
        if (rc != MIME_TIFF_OK)
            goto fail;
        offs[i] = (uint32_t)start;
        counts[i] = (uint32_t)(e->len - start);
    }

    /* the ifd. */
    Tags t = {.list = list, .n = 0};
    uint32_t width = p.width, height = p.height, rps = p.rps;
    uint16_t bps[MAX_SPP], sfmt[MAX_SPP], extra[MAX_SPP];
    uint16_t compression = (uint16_t)p.compression, photometric = s->photometric;
    uint16_t spp = (uint16_t)p.spp, planar = 1, predictor = (uint16_t)p.predictor;
    uint16_t orientation = s->orientation, res_unit = s->resolution_unit, sample_format = s->sample_format;
    uint32_t xres[2] = {s->x_resolution_num, s->x_resolution_den}, yres[2] = {s->y_resolution_num, s->y_resolution_den};
    uint32_t t4 = (s->group3_two_dim ? 1u : 0u) | (s->group3_fill ? 4u : 0u), t6 = 0;
    uint32_t subfile = s->new_subfile_type;
    for (unsigned i = 0; i < p.spp; i++) {
        bps[i] = (uint16_t)p.bits;
        sfmt[i] = sample_format ? sample_format : 1;
        extra[i] = 0;
    }
    if (s->extra_samples)
        extra[0] = s->alpha == MIME_TIFF_ALPHA_ASSOCIATED ? 1 : s->alpha == MIME_TIFF_ALPHA_UNASSOCIATED ? 2 : 0;

    for (uint32_t i = 0; i < s->extra_tag_count; i++)
        t.list[t.n++] = s->extra_tags[i];
    if (subfile)
        tag_add(&t, MIME_TIFF_TAG_NEW_SUBFILE_TYPE, MIME_TIFF_TYPE_LONG, 1, &subfile);
    tag_add(&t, MIME_TIFF_TAG_IMAGE_WIDTH, MIME_TIFF_TYPE_LONG, 1, &width);
    tag_add(&t, MIME_TIFF_TAG_IMAGE_LENGTH, MIME_TIFF_TYPE_LONG, 1, &height);
    tag_add(&t, MIME_TIFF_TAG_BITS_PER_SAMPLE, MIME_TIFF_TYPE_SHORT, spp, bps);
    tag_add(&t, MIME_TIFF_TAG_COMPRESSION, MIME_TIFF_TYPE_SHORT, 1, &compression);
    tag_add(&t, MIME_TIFF_TAG_PHOTOMETRIC, MIME_TIFF_TYPE_SHORT, 1, &photometric);
    tag_string(&t, MIME_TIFF_TAG_IMAGE_DESCRIPTION, s->description);
    tag_string(&t, MIME_TIFF_TAG_MAKE, s->make);
    tag_string(&t, MIME_TIFF_TAG_MODEL, s->model);
    tag_add(&t, MIME_TIFF_TAG_STRIP_OFFSETS, MIME_TIFF_TYPE_LONG, p.strips, offs);
    if (orientation)
        tag_add(&t, MIME_TIFF_TAG_ORIENTATION, MIME_TIFF_TYPE_SHORT, 1, &orientation);
    tag_add(&t, MIME_TIFF_TAG_SAMPLES_PER_PIXEL, MIME_TIFF_TYPE_SHORT, 1, &spp);
    tag_add(&t, MIME_TIFF_TAG_ROWS_PER_STRIP, MIME_TIFF_TYPE_LONG, 1, &rps);
    tag_add(&t, MIME_TIFF_TAG_STRIP_BYTE_COUNTS, MIME_TIFF_TYPE_LONG, p.strips, counts);
    if (xres[1])
        tag_add(&t, MIME_TIFF_TAG_X_RESOLUTION, MIME_TIFF_TYPE_RATIONAL, 1, xres);
    if (yres[1])
        tag_add(&t, MIME_TIFF_TAG_Y_RESOLUTION, MIME_TIFF_TYPE_RATIONAL, 1, yres);
    tag_add(&t, MIME_TIFF_TAG_PLANAR_CONFIG, MIME_TIFF_TYPE_SHORT, 1, &planar);
    if (p.compression == MIME_TIFF_COMPRESSION_CCITT_G3)
        tag_add(&t, MIME_TIFF_TAG_T4_OPTIONS, MIME_TIFF_TYPE_LONG, 1, &t4);
    if (p.compression == MIME_TIFF_COMPRESSION_CCITT_G4)
        tag_add(&t, MIME_TIFF_TAG_T6_OPTIONS, MIME_TIFF_TYPE_LONG, 1, &t6);
    if (res_unit)
        tag_add(&t, MIME_TIFF_TAG_RESOLUTION_UNIT, MIME_TIFF_TYPE_SHORT, 1, &res_unit);
    tag_string(&t, MIME_TIFF_TAG_SOFTWARE, s->software);
    tag_string(&t, MIME_TIFF_TAG_DATE_TIME, s->date_time);
    tag_string(&t, MIME_TIFF_TAG_ARTIST, s->artist);
    tag_string(&t, MIME_TIFF_TAG_HOST_COMPUTER, s->host_computer);
    if (predictor != 1)
        tag_add(&t, MIME_TIFF_TAG_PREDICTOR, MIME_TIFF_TYPE_SHORT, 1, &predictor);
    if (s->color_map && s->photometric == MIME_TIFF_PHOTOMETRIC_PALETTE)
        tag_add(&t, MIME_TIFF_TAG_COLOR_MAP, MIME_TIFF_TYPE_SHORT, 3u << p.bits, s->color_map);
    if (s->extra_samples)
        tag_add(&t, MIME_TIFF_TAG_EXTRA_SAMPLES, MIME_TIFF_TYPE_SHORT, s->extra_samples, extra);
    if (sample_format && sample_format != 1)
        tag_add(&t, MIME_TIFF_TAG_SAMPLE_FORMAT, MIME_TIFF_TYPE_SHORT, spp, sfmt);
    tag_string(&t, MIME_TIFF_TAG_COPYRIGHT, s->copyright);
    if (s->exif_ifd) {
        tag_add(&t, MIME_TIFF_TAG_EXIF_IFD, MIME_TIFF_TYPE_LONG, 1, NULL);
        t.list[t.n - 1].sub = s->exif_ifd;
    }
    if (s->gps_ifd) {
        tag_add(&t, MIME_TIFF_TAG_GPS_IFD, MIME_TIFF_TYPE_LONG, 1, NULL);
        t.list[t.n - 1].sub = s->gps_ifd;
    }

    MimeTiffWIfd node = {.entries = t.list, .entry_count = t.n};
    TiffBuf tb = {.alloc = e->alloc, .p = e->buf, .len = e->len, .cap = e->cap, .be = e->big_endian};
    uint32_t off;
    rc = tiff_write_ifd(&tb, &node, 0, &off);
    e->buf = tb.p;
    e->len = tb.len;
    e->cap = tb.cap;
    if (rc != MIME_TIFF_OK) {
        e->failed = true;
        goto fail;
    }
    tiff_wr32(e->buf + e->last_next_slot, off, e->big_endian);
    unsigned count = tiff_rd16(e->buf + off, e->big_endian);
    e->last_next_slot = (size_t)off + 2 + (size_t)count * 12;
    e->frames++;
    rc = MIME_TIFF_OK;

fail:
    if (rc != MIME_TIFF_OK && rc != MIME_TIFF_ERR_PARAM)
        e->failed = true;
    mime_free(e->alloc, offs);
    mime_free(e->alloc, w.sbuf);
    mime_free(e->alloc, w.tmp);
    mime_free(e->alloc, w.lzw);
    mime_free(e->alloc, w.ccitt);
    return rc;
}

int mime_tiff_encode(const MimeTiffFrameSpec *spec, const void *pixels, size_t stride, unsigned flags,
                     const MimeAllocator *alloc, void **out, size_t *out_len)
{
    MimeTiffEncoder e;
    int rc = mime_tiff_encoder_init(&e, alloc, flags);
    if (rc != MIME_TIFF_OK)
        return rc;
    rc = mime_tiff_encoder_add_frame(&e, spec, pixels, stride);
    if (rc != MIME_TIFF_OK) {
        mime_tiff_encoder_abort(&e);
        return rc;
    }
    return mime_tiff_encoder_finish(&e, out, out_len);
}
