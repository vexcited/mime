#include "tiff_internal.h"

int32_t tiff_ratio_fixed(uint32_t num, uint32_t den)
{
    if (!den)
        return 0;
    uint32_t q = num / den, r = num % den;
    if (q >= 32768)
        return INT32_MAX;
    uint32_t frac = 0;
    uint64_t rem = r;
    for (int i = 0; i < 16; i++) {
        rem <<= 1;
        frac <<= 1;
        if (rem >= den) {
            rem -= den;
            frac |= 1;
        }
    }
    return (int32_t)(q << 16 | frac);
}

/* a shifted left by 16 and divided by b for a >= 0 and b > 0, without a 64 bit division. */
int64_t tiff_fixed_div(int64_t a, int64_t b)
{
    uint64_t n = (uint64_t)a << 16, d = (uint64_t)b, q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= 1ull << i;
        }
    }
    return (int64_t)q;
}

static uint32_t get_uint(const MimeTiffIfd *ifd, uint16_t tag, uint32_t def)
{
    uint64_t v;
    if (!mime_tiff_ifd_uint(ifd, tag, &v) || v > 0xffffffffu)
        return def;
    return (uint32_t)v;
}

/* a per sample tag: the value of the first sample, with ok cleared when the samples differ. */
static uint32_t get_uniform(const MimeTiffIfd *ifd, uint16_t tag, uint32_t def, uint32_t samples, bool *ok)
{
    MimeTiffEntry e;
    uint64_t first;
    if (mime_tiff_ifd_find(ifd, tag, &e) != MIME_TIFF_OK || !mime_tiff_entry_uint(&e, 0, &first))
        return def;
    uint64_t n = e.count < samples ? e.count : samples;
    for (uint64_t i = 1; i < n; i++) {
        uint64_t v;
        if (!mime_tiff_entry_uint(&e, i, &v) || v != first)
            *ok = false;
    }
    return first > 0xffffffffu ? def : (uint32_t)first;
}

static bool get_rational(const MimeTiffIfd *ifd, uint16_t tag, uint32_t *num, uint32_t *den)
{
    MimeTiffEntry e;
    int64_t n, d;
    if (mime_tiff_ifd_find(ifd, tag, &e) != MIME_TIFF_OK || !mime_tiff_entry_rational(&e, 0, &n, &d) || n < 0 ||
        d <= 0)
        return false;
    *num = (uint32_t)n;
    *den = (uint32_t)d;
    return true;
}

static unsigned expected_color_samples(unsigned photometric)
{
    switch (photometric) {
    case MIME_TIFF_PHOTOMETRIC_RGB:
    case MIME_TIFF_PHOTOMETRIC_YCBCR:
    case MIME_TIFF_PHOTOMETRIC_CIELAB:
        return 3;
    case MIME_TIFF_PHOTOMETRIC_SEPARATED:
        return 4;
    default:
        return 1;
    }
}

int mime_tiff_frame_open(const MimeTiffIfd *ifd, MimeTiffFrame *f)
{
    if (!ifd || !f)
        return MIME_TIFF_ERR_PARAM;
    memset(f, 0, sizeof *f);
    f->ifd = *ifd;

    uint64_t w, h;
    if (!mime_tiff_ifd_uint(ifd, MIME_TIFF_TAG_IMAGE_WIDTH, &w) ||
        !mime_tiff_ifd_uint(ifd, MIME_TIFF_TAG_IMAGE_LENGTH, &h) || w == 0 || h == 0 || w > 0x7fffffffu ||
        h > 0x7fffffffu)
        return MIME_TIFF_ERR_FORMAT;
    f->width = (uint32_t)w;
    f->height = (uint32_t)h;

    uint32_t spp = get_uint(ifd, MIME_TIFF_TAG_SAMPLES_PER_PIXEL, 1);
    if (spp == 0 || spp > 64)
        return MIME_TIFF_ERR_FORMAT;
    f->samples_per_pixel = (uint16_t)spp;
    bool uniform = true;
    uint32_t bits = get_uniform(ifd, MIME_TIFF_TAG_BITS_PER_SAMPLE, 1, spp, &uniform);
    f->bits_per_sample = uniform && bits <= 64 ? (uint16_t)bits : 0;
    uniform = true;
    uint32_t sf = get_uniform(ifd, MIME_TIFF_TAG_SAMPLE_FORMAT, 1, spp, &uniform);
    f->sample_format = uniform ? (uint16_t)sf : 0;
    f->compression = (uint16_t)get_uint(ifd, MIME_TIFF_TAG_COMPRESSION, 1);
    f->planar = (uint16_t)get_uint(ifd, MIME_TIFF_TAG_PLANAR_CONFIG, 1);
    f->fill_order = (uint16_t)get_uint(ifd, MIME_TIFF_TAG_FILL_ORDER, 1);
    f->predictor = (uint16_t)get_uint(ifd, MIME_TIFF_TAG_PREDICTOR, 1);
    f->orientation = (uint16_t)get_uint(ifd, MIME_TIFF_TAG_ORIENTATION, 1);
    if (f->orientation < 1 || f->orientation > 8)
        f->orientation = 1;
    f->resolution_unit = (uint16_t)get_uint(ifd, MIME_TIFF_TAG_RESOLUTION_UNIT, 2);
    f->ink_set = (uint16_t)get_uint(ifd, MIME_TIFF_TAG_INK_SET, 1);
    f->t4_options = get_uint(ifd, MIME_TIFF_TAG_T4_OPTIONS, 0);
    f->t6_options = get_uint(ifd, MIME_TIFF_TAG_T6_OPTIONS, 0);
    f->new_subfile_type = get_uint(ifd, MIME_TIFF_TAG_NEW_SUBFILE_TYPE, 0);
    get_rational(ifd, MIME_TIFF_TAG_X_RESOLUTION, &f->x_resolution_num, &f->x_resolution_den);
    get_rational(ifd, MIME_TIFF_TAG_Y_RESOLUTION, &f->y_resolution_num, &f->y_resolution_den);

    MimeTiffEntry e;
    uint64_t v;
    if (mime_tiff_ifd_find(ifd, MIME_TIFF_TAG_PHOTOMETRIC, &e) == MIME_TIFF_OK && mime_tiff_entry_uint(&e, 0, &v)) {
        f->photometric = (uint16_t)v;
    } else if (mime_tiff_ifd_find(ifd, MIME_TIFF_TAG_COLOR_MAP, &e) == MIME_TIFF_OK) {
        f->photometric = MIME_TIFF_PHOTOMETRIC_PALETTE;
    } else if (f->compression >= 2 && f->compression <= 4) {
        f->photometric = MIME_TIFF_PHOTOMETRIC_WHITE_IS_ZERO;
    } else {
        f->photometric = spp >= 3 ? MIME_TIFF_PHOTOMETRIC_RGB : MIME_TIFF_PHOTOMETRIC_BLACK_IS_ZERO;
    }

    /* extra samples. */
    unsigned color = expected_color_samples(f->photometric);
    unsigned extra = spp > color ? spp - color : 0;
    unsigned tagged = 0;
    unsigned first_kind = 0;
    if (mime_tiff_ifd_find(ifd, MIME_TIFF_TAG_EXTRA_SAMPLES, &e) == MIME_TIFF_OK) {
        tagged = e.count > spp ? spp : (unsigned)e.count;
        if (mime_tiff_entry_uint(&e, 0, &v))
            first_kind = (unsigned)v;
    }
    if (tagged > extra)
        extra = tagged;
    f->extra_samples = (uint16_t)extra;
    if (extra == 0)
        f->alpha = MIME_TIFF_ALPHA_NONE;
    else if (tagged && first_kind == 1)
        f->alpha = MIME_TIFF_ALPHA_ASSOCIATED;
    else if (tagged && first_kind == 2)
        f->alpha = MIME_TIFF_ALPHA_UNASSOCIATED;
    else
        f->alpha = MIME_TIFF_ALPHA_UNSPECIFIED;

    f->ycbcr_h = f->ycbcr_v = 2;
    if (mime_tiff_ifd_find(ifd, MIME_TIFF_TAG_YCBCR_SUBSAMPLING, &e) == MIME_TIFF_OK) {
        uint64_t sh, sv;
        if (mime_tiff_entry_uint(&e, 0, &sh) && mime_tiff_entry_uint(&e, 1, &sv) && sh >= 1 && sh <= 16 &&
            sv >= 1 && sv <= 16) {
            f->ycbcr_h = (uint16_t)sh;
            f->ycbcr_v = (uint16_t)sv;
        }
    }
    f->ycbcr_positioning = (uint16_t)get_uint(ifd, MIME_TIFF_TAG_YCBCR_POSITIONING, 1);
    f->ycbcr_coefficients[0] = 19595; /* 0.299, 0.587, 0.114. */
    f->ycbcr_coefficients[1] = 38470;
    f->ycbcr_coefficients[2] = 7471;
    if (mime_tiff_ifd_find(ifd, MIME_TIFF_TAG_YCBCR_COEFFICIENTS, &e) == MIME_TIFF_OK && e.count >= 3) {
        int32_t c[3];
        bool ok = true;
        for (unsigned i = 0; i < 3; i++) {
            int64_t n, d;
            if (!mime_tiff_entry_rational(&e, i, &n, &d) || n < 0 || d <= 0 || n > d * 4) {
                ok = false;
                break;
            }
            c[i] = tiff_ratio_fixed((uint32_t)n, (uint32_t)d);
        }
        if (ok && c[1] > 0)
            memcpy(f->ycbcr_coefficients, c, sizeof c);
    }
    static const int32_t ycc_default_bw[6] = {0, 255 << 16, 128 << 16, 255 << 16, 128 << 16, 255 << 16};
    memcpy(f->reference_black_white, ycc_default_bw, sizeof ycc_default_bw);
    if (mime_tiff_ifd_find(ifd, MIME_TIFF_TAG_REFERENCE_BLACK_WHITE, &e) == MIME_TIFF_OK && e.count >= 6) {
        int32_t c[6];
        bool ok = true;
        for (unsigned i = 0; i < 6; i++) {
            int64_t n, d;
            if (!mime_tiff_entry_rational(&e, i, &n, &d) || n < 0 || d <= 0 || n > d * 65535) {
                ok = false;
                break;
            }
            c[i] = tiff_ratio_fixed((uint32_t)n, (uint32_t)d);
        }
        if (ok)
            memcpy(f->reference_black_white, c, sizeof c);
    }

    if (f->photometric == MIME_TIFF_PHOTOMETRIC_PALETTE && f->bits_per_sample >= 1 && f->bits_per_sample <= 16 &&
        mime_tiff_ifd_find(ifd, MIME_TIFF_TAG_COLOR_MAP, &f->color_map) == MIME_TIFF_OK && f->color_map.data &&
        f->color_map.type == MIME_TIFF_TYPE_SHORT) {
        uint64_t n = 1ull << f->bits_per_sample;
        if (f->color_map.count >= n * 3)
            f->palette_entries = (uint32_t)n;
    }

    if (mime_tiff_ifd_find(ifd, MIME_TIFF_TAG_JPEG_TABLES, &e) == MIME_TIFF_OK && e.data) {
        f->jpeg_tables = e.data;
        f->jpeg_tables_size = e.size;
    }

    /* strip or tile layout. */
    uint64_t per_plane;
    uint16_t offsets_tag, counts_tag;
    if (mime_tiff_ifd_uint(ifd, MIME_TIFF_TAG_TILE_WIDTH, &w)) {
        uint64_t th;
        if (!mime_tiff_ifd_uint(ifd, MIME_TIFF_TAG_TILE_LENGTH, &th) || w == 0 || th == 0 || w > 0x7fffffffu ||
            th > 0x7fffffffu)
            return MIME_TIFF_ERR_FORMAT;
        f->tile_width = (uint32_t)w;
        f->tile_height = (uint32_t)th;
        uint32_t across = (f->width - 1) / f->tile_width + 1;
        uint32_t down = (f->height - 1) / f->tile_height + 1;
        per_plane = (uint64_t)across * down;
        offsets_tag = MIME_TIFF_TAG_TILE_OFFSETS;
        counts_tag = MIME_TIFF_TAG_TILE_BYTE_COUNTS;
    } else {
        uint32_t rps = get_uint(ifd, MIME_TIFF_TAG_ROWS_PER_STRIP, 0xffffffffu);
        if (rps == 0 || rps > f->height)
            rps = f->height;
        f->rows_per_strip = rps;
        per_plane = (f->height - 1) / rps + 1;
        offsets_tag = MIME_TIFF_TAG_STRIP_OFFSETS;
        counts_tag = MIME_TIFF_TAG_STRIP_BYTE_COUNTS;
    }
    uint64_t total = per_plane * (f->planar == 2 ? spp : 1);
    if (f->planar != 1 && f->planar != 2)
        return MIME_TIFF_ERR_FORMAT;
    if (total > 0x7fffffffu)
        return MIME_TIFF_ERR_FORMAT;
    f->chunks_per_plane = (uint32_t)per_plane;
    f->chunk_count = (uint32_t)total;
    if (mime_tiff_ifd_find(ifd, offsets_tag, &f->offsets) != MIME_TIFF_OK || !f->offsets.data ||
        f->offsets.count < total)
        return MIME_TIFF_ERR_FORMAT;
    if (mime_tiff_ifd_find(ifd, counts_tag, &f->byte_counts) == MIME_TIFF_OK && f->byte_counts.data &&
        f->byte_counts.count >= total)
        f->has_byte_counts = true;

    /* decoded pixel layout. */
    f->out_bits_per_sample = f->bits_per_sample;
    f->out_samples_per_pixel = f->samples_per_pixel;
    f->out_photometric = f->photometric;
    if (f->bits_per_sample > 2 && f->bits_per_sample < 16 && f->bits_per_sample != 4 && f->bits_per_sample != 8 &&
        f->photometric != MIME_TIFF_PHOTOMETRIC_PALETTE)
        f->out_bits_per_sample = 16;
    if (f->photometric == MIME_TIFF_PHOTOMETRIC_YCBCR && f->compression != MIME_TIFF_COMPRESSION_JPEG &&
        f->compression != MIME_TIFF_COMPRESSION_OLD_JPEG)
        f->out_photometric = MIME_TIFF_PHOTOMETRIC_RGB;
    uint64_t row_bits = (uint64_t)f->width * f->out_samples_per_pixel * f->out_bits_per_sample;
    uint64_t row_bytes = (row_bits + 7) / 8;
    if (f->out_bits_per_sample == 0 || row_bytes > 0x7fffffffu || row_bytes * f->height > MIME_TIFF_MAX_IMAGE_BYTES)
        return f->out_bits_per_sample == 0 ? MIME_TIFF_ERR_FORMAT : MIME_TIFF_ERR_NOMEM;
    f->out_row_bytes = (uint32_t)row_bytes;
    return MIME_TIFF_OK;
}

int mime_tiff_frame_open_index(const MimeTiff *tiff, uint32_t index, MimeTiffFrame *frame)
{
    MimeTiffIfd ifd;
    int rc = mime_tiff_ifd_index(tiff, index, &ifd);
    return rc == MIME_TIFF_OK ? mime_tiff_frame_open(&ifd, frame) : rc;
}

int mime_tiff_frame_tag(const MimeTiffFrame *frame, uint16_t tag, MimeTiffEntry *entry)
{
    return mime_tiff_ifd_find(&frame->ifd, tag, entry);
}

uint32_t mime_tiff_frame_palette(const MimeTiffFrame *f, uint16_t *rgb, uint32_t cap)
{
    uint32_t n = f->palette_entries < cap ? f->palette_entries : cap;
    for (uint32_t i = 0; i < n; i++) {
        for (unsigned c = 0; c < 3; c++) {
            uint64_t v = 0;
            mime_tiff_entry_uint(&f->color_map, (uint64_t)c * f->palette_entries + i, &v);
            rgb[i * 3 + c] = (uint16_t)v;
        }
    }
    return n;
}

int mime_tiff_frame_chunk(const MimeTiffFrame *f, uint32_t index, MimeTiffChunk *c)
{
    if (!f || !c)
        return MIME_TIFF_ERR_PARAM;
    if (index >= f->chunk_count)
        return MIME_TIFF_ERR_RANGE;
    uint32_t local = index % f->chunks_per_plane;
    c->plane = index / f->chunks_per_plane;
    if (f->tile_width) {
        uint32_t across = (f->width + f->tile_width - 1) / f->tile_width;
        uint32_t tx = local % across, ty = local / across;
        c->x = tx * f->tile_width;
        c->y = ty * f->tile_height;
        c->width = f->width - c->x < f->tile_width ? f->width - c->x : f->tile_width;
        c->height = f->height - c->y < f->tile_height ? f->height - c->y : f->tile_height;
    } else {
        c->x = 0;
        c->y = local * f->rows_per_strip;
        c->width = f->width;
        c->height = f->height - c->y < f->rows_per_strip ? f->height - c->y : f->rows_per_strip;
    }
    uint64_t off = 0, cnt = 0, size = f->ifd.tiff.size;
    mime_tiff_entry_uint(&f->offsets, index, &off);
    c->data = NULL;
    c->size = 0;
    if (off >= size)
        return MIME_TIFF_OK;
    uint64_t avail = size - off;
    bool counted = f->has_byte_counts && mime_tiff_entry_uint(&f->byte_counts, index, &cnt) && cnt != 0;
    if (!counted) {
        cnt = avail;
        uint64_t next;
        if (index + 1 < f->chunk_count && mime_tiff_entry_uint(&f->offsets, index + 1, &next) && next > off &&
            next - off < cnt)
            cnt = next - off;
    }
    if (cnt > avail)
        cnt = avail;
    c->data = f->ifd.tiff.data + off;
    c->size = (size_t)cnt;
    return MIME_TIFF_OK;
}
